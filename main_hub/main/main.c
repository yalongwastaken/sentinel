/**
 * @file main.c
 * @author Anthony Yalong
 * @brief Main hub: samples the local sensors, shows them on the LCD, and links to the remote node over BLE.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "hcsr501.h"
#include "hcsr04.h"
#include "dht11.h"
#include "hd44780_pcf8574.h"
#include "crash_limit.h"
#include "remote_motion_ble.h"
#include "main_hub_system_config.h"

static const char *TAG = "MAIN_HUB";

// every task reads or writes these only while holding the mutex, and never
// holds it across sensor or I2C I/O
static struct {
    bool local_motion_detected;
    float distance_cm;
    float temperature_celsius;
    float relative_humidity_percent;
    bool remote_motion_detected;
    bool remote_connected;
    SemaphoreHandle_t mutex;
} latest_readings;

static hcsr501_sensor_t hcsr501_sensor;
static hcsr04_sensor_t hcsr04_sensor;
static dht11_sensor_t dht11_sensor;
static hd44780_t lcd;

static const ble_uuid128_t motion_service_uuid = BLE_UUID128_INIT(REMOTE_MOTION_BLE_SERVICE_UUID_BYTES);
static const ble_uuid128_t motion_characteristic_uuid =
    BLE_UUID128_INIT(REMOTE_MOTION_BLE_MOTION_CHARACTERISTIC_UUID_BYTES);

// only NimBLE's host task touches these, through the callbacks below, so they need no lock
static uint8_t own_address_type;
static struct remote_link {
    uint16_t connection_handle;
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    uint16_t motion_value_handle;
    uint16_t motion_notification_config_handle;
} remote_link = {.connection_handle = BLE_HS_CONN_HANDLE_NONE};

static esp_err_t init_nvs_flash(void);
static void start_task_or_abort(TaskFunction_t task_function, const char *task_name, UBaseType_t priority);
esp_err_t create_i2c_master_bus(i2c_master_bus_handle_t *bus);
void local_motion_task(void *pvParameters);
void distance_task(void *pvParameters);
void temperature_humidity_task(void *pvParameters);
void display_refresh_task(void *pvParameters);
static void start_scanning_for_remote(void);
static int handle_gap_event(struct ble_gap_event *event, void *arg);
static void run_nimble_host_task(void *param);
static void handle_ble_host_sync(void);
static void handle_ble_host_reset(int reason);

// every startup step aborts on failure, so the board reboots and retries until
// crash_limit stops a build that keeps failing the same way
void app_main(void)
{
    ESP_ERROR_CHECK(init_nvs_flash());
    crash_limit_verdict_t crash_verdict;
    ESP_ERROR_CHECK(crash_limit_record_last_reset(&crash_verdict));
    if (crash_verdict.limit_reached) {
        ESP_LOGE(TAG, "stopped: this firmware crashed %d times by %s; flash a fix", CRASH_LIMIT_MAX_CRASHES_PER_TYPE,
                 crash_verdict.crash_type);
        return;
    }

    latest_readings.mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(latest_readings.mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    i2c_master_bus_handle_t i2c_bus;
    ESP_ERROR_CHECK(create_i2c_master_bus(&i2c_bus));
    ESP_ERROR_CHECK(
        hd44780_init(&lcd, i2c_bus, PCF8574_I2C_ADDRESS, PCF8574_I2C_CLOCK_HZ, LCD1602_COLUMNS, LCD1602_ROWS));
    hd44780_clear(&lcd);
    hd44780_set_cursor(&lcd, 0, 0);
    hd44780_print(&lcd, "Security System");
    hd44780_set_cursor(&lcd, 0, 1);
    hd44780_print(&lcd, "Initializing...");
    vTaskDelay(pdMS_TO_TICKS(STARTUP_MESSAGE_MS));

    ESP_ERROR_CHECK(hcsr501_init(&hcsr501_sensor, HCSR501_GPIO_PIN));
    ESP_ERROR_CHECK(hcsr04_init(&hcsr04_sensor, HCSR04_TRIGGER_GPIO_PIN, HCSR04_ECHO_GPIO_PIN, HCSR04_ECHO_TIMEOUT_US));
    ESP_ERROR_CHECK(dht11_init(&dht11_sensor, DHT11_GPIO_PIN));

    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = handle_ble_host_sync;
    ble_hs_cfg.reset_cb = handle_ble_host_reset;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    nimble_port_freertos_init(run_nimble_host_task);

    start_task_or_abort(local_motion_task, "local_motion_task", LOCAL_MOTION_TASK_PRIORITY);
    start_task_or_abort(distance_task, "distance_task", DISTANCE_TASK_PRIORITY);
    start_task_or_abort(temperature_humidity_task, "temperature_humidity_task", TEMPERATURE_HUMIDITY_TASK_PRIORITY);
    start_task_or_abort(display_refresh_task, "display_refresh_task", DISPLAY_REFRESH_TASK_PRIORITY);

    ESP_LOGI(TAG, "main hub running");
}

static esp_err_t init_nvs_flash(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static void start_task_or_abort(TaskFunction_t task_function, const char *task_name, UBaseType_t priority)
{
    BaseType_t created = xTaskCreate(task_function, task_name, TASK_STACK_BYTES, NULL, priority, NULL);
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

esp_err_t create_i2c_master_bus(i2c_master_bus_handle_t *bus)
{
    i2c_master_bus_config_t bus_settings = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO_PIN,
        .scl_io_num = I2C_SCL_GPIO_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = I2C_GLITCH_FILTER_CYCLES,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&bus_settings, bus);
}

void local_motion_task(void *pvParameters)
{
    TickType_t last_wake_tick = xTaskGetTickCount();
    while (1) {
        bool local_motion = hcsr501_is_motion_detected(&hcsr501_sensor);

        xSemaphoreTake(latest_readings.mutex, portMAX_DELAY);
        bool local_motion_changed = local_motion != latest_readings.local_motion_detected;
        latest_readings.local_motion_detected = local_motion;
        xSemaphoreGive(latest_readings.mutex);

        if (local_motion_changed) {
            ESP_LOGI(TAG, "motion=%s", local_motion ? "true" : "false");
        }

        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(LOCAL_MOTION_TASK_PERIOD_MS));
    }
}

void distance_task(void *pvParameters)
{
    TickType_t last_wake_tick = xTaskGetTickCount();
    while (1) {
        float distance_cm;
        if (hcsr04_measure_distance_cm(&hcsr04_sensor, &distance_cm) == ESP_OK) {
            xSemaphoreTake(latest_readings.mutex, portMAX_DELAY);
            latest_readings.distance_cm = distance_cm;
            xSemaphoreGive(latest_readings.mutex);
        }

        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(DISTANCE_TASK_PERIOD_MS));
    }
}

void temperature_humidity_task(void *pvParameters)
{
    TickType_t last_wake_tick = xTaskGetTickCount();
    while (1) {
        dht11_reading_t reading;
        if (dht11_read_temperature_and_humidity(&dht11_sensor, &reading) == ESP_OK) {
            xSemaphoreTake(latest_readings.mutex, portMAX_DELAY);
            latest_readings.temperature_celsius = reading.temperature_celsius;
            latest_readings.relative_humidity_percent = reading.relative_humidity_percent;
            xSemaphoreGive(latest_readings.mutex);
        }

        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(TEMPERATURE_HUMIDITY_TASK_PERIOD_MS));
    }
}

void display_refresh_task(void *pvParameters)
{
    TickType_t last_wake_tick = xTaskGetTickCount();
    while (1) {
        xSemaphoreTake(latest_readings.mutex, portMAX_DELAY);
        bool local_motion = latest_readings.local_motion_detected;
        bool remote_connected = latest_readings.remote_connected;
        bool remote_motion = latest_readings.remote_motion_detected;
        float distance_cm = latest_readings.distance_cm;
        float temperature_celsius = latest_readings.temperature_celsius;
        float relative_humidity_percent = latest_readings.relative_humidity_percent;
        xSemaphoreGive(latest_readings.mutex);

        char remote_motion_symbol = !remote_connected ? '-' : (remote_motion ? 'Y' : 'N');
        hd44780_clear(&lcd);
        hd44780_set_cursor(&lcd, 0, 0);
        hd44780_printf(&lcd, "M:%c R:%c D:%.0fcm", local_motion ? 'Y' : 'N', remote_motion_symbol, distance_cm);
        hd44780_set_cursor(&lcd, 0, 1);
        hd44780_printf(&lcd, "T:%.0fC H:%.0f%%", temperature_celsius, relative_humidity_percent);

        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(DISPLAY_REFRESH_TASK_PERIOD_MS));
    }
}

static void run_nimble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void handle_ble_host_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &own_address_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "no usable ble address: nimble_rc=%d", rc);
        return;
    }
    start_scanning_for_remote();
}

static void handle_ble_host_reset(int reason) { ESP_LOGE(TAG, "ble host reset: nimble_reason=%d", reason); }

static void record_remote_link(bool remote_connected, bool remote_motion_detected)
{
    xSemaphoreTake(latest_readings.mutex, portMAX_DELAY);
    bool connected_changed = remote_connected != latest_readings.remote_connected;
    bool motion_changed = remote_motion_detected != latest_readings.remote_motion_detected;
    latest_readings.remote_connected = remote_connected;
    latest_readings.remote_motion_detected = remote_motion_detected;
    xSemaphoreGive(latest_readings.mutex);

    if (connected_changed) {
        ESP_LOGI(TAG, "remote_connected=%s", remote_connected ? "true" : "false");
    }
    if (remote_connected && (motion_changed || connected_changed)) {
        ESP_LOGI(TAG, "remote_motion=%s", remote_motion_detected ? "true" : "false");
    }
}

// the remote side failed or misbehaved, which is no reason to stop the hub: drop
// the connection, and the disconnect event starts a fresh scan
static void drop_remote_link(const char *reason)
{
    ESP_LOGW(TAG, "dropping the remote link: %s", reason);
    ble_gap_terminate(remote_link.connection_handle, BLE_ERR_REM_USER_CONN_TERM);
}

static void start_scanning_for_remote(void)
{
    struct ble_gap_disc_params scan_params = {.passive = 1, .filter_duplicates = 1};
    int rc = ble_gap_disc(own_address_type, BLE_HS_FOREVER, &scan_params, handle_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "scan failed to start: nimble_rc=%d", rc);
    }
}

static bool advertises_motion_service(const struct ble_gap_disc_desc *advertisement)
{
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, advertisement->data, advertisement->length_data) != 0) {
        return false;
    }
    for (int i = 0; i < fields.num_uuids128; i++) {
        if (ble_uuid_cmp(&fields.uuids128[i].u, &motion_service_uuid.u) == 0) {
            return true;
        }
    }
    return false;
}

static void connect_to_remote(const ble_addr_t *remote_address)
{
    int rc = ble_gap_disc_cancel();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "scan failed to stop: nimble_rc=%d", rc);
        return;
    }
    rc = ble_gap_connect(own_address_type, remote_address, REMOTE_CONNECT_TIMEOUT_MS, NULL, handle_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "connection failed to start: nimble_rc=%d", rc);
        start_scanning_for_remote();
    }
}

static void record_remote_motion_payload(const struct os_mbuf *payload_buffer)
{
    uint8_t payload;
    uint16_t payload_length;
    bool remote_motion_detected;
    bool payload_is_valid = ble_hs_mbuf_to_flat(payload_buffer, &payload, sizeof(payload), &payload_length) == 0 &&
                            payload_length == REMOTE_MOTION_BLE_MOTION_PAYLOAD_BYTES &&
                            remote_motion_ble_decode_motion(payload, &remote_motion_detected) == ESP_OK;
    if (!payload_is_valid) {
        drop_remote_link("the remote sent an invalid motion payload");
        return;
    }
    record_remote_link(true, remote_motion_detected);
}

static int handle_motion_read(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr,
                              void *arg)
{
    if (error->status != 0) {
        drop_remote_link("reading the current motion failed");
        return 0;
    }
    record_remote_motion_payload(attr->om);
    return 0;
}

static int handle_subscribed(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr,
                             void *arg)
{
    if (error->status != 0) {
        drop_remote_link("subscribing to motion failed");
        return 0;
    }
    // notifications arrive only on change, so read the current value once
    if (ble_gattc_read(conn_handle, remote_link.motion_value_handle, handle_motion_read, NULL) != 0) {
        drop_remote_link("reading the current motion failed to start");
    }
    return 0;
}

static int handle_descriptor_discovered(uint16_t conn_handle, const struct ble_gatt_error *error,
                                        uint16_t chr_val_handle, const struct ble_gatt_dsc *descriptor, void *arg)
{
    if (error->status == 0) {
        if (ble_uuid_u16(&descriptor->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16) {
            remote_link.motion_notification_config_handle = descriptor->handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE || remote_link.motion_notification_config_handle == 0) {
        drop_remote_link("the motion characteristic has no notification descriptor");
        return 0;
    }

    static const uint8_t enable_notifications[] = {0x01, 0x00};
    if (ble_gattc_write_flat(conn_handle, remote_link.motion_notification_config_handle, enable_notifications,
                             sizeof(enable_notifications), handle_subscribed, NULL) != 0) {
        drop_remote_link("subscribing to motion failed to start");
    }
    return 0;
}

static int handle_characteristic_discovered(uint16_t conn_handle, const struct ble_gatt_error *error,
                                            const struct ble_gatt_chr *characteristic, void *arg)
{
    if (error->status == 0) {
        remote_link.motion_value_handle = characteristic->val_handle;
        return 0;
    }
    if (error->status != BLE_HS_EDONE || remote_link.motion_value_handle == 0) {
        drop_remote_link("the motion characteristic was not found");
        return 0;
    }
    if (ble_gattc_disc_all_dscs(conn_handle, remote_link.motion_value_handle, remote_link.service_end_handle,
                                handle_descriptor_discovered, NULL) != 0) {
        drop_remote_link("descriptor discovery failed to start");
    }
    return 0;
}

static int handle_service_discovered(uint16_t conn_handle, const struct ble_gatt_error *error,
                                     const struct ble_gatt_svc *service, void *arg)
{
    if (error->status == 0) {
        remote_link.service_start_handle = service->start_handle;
        remote_link.service_end_handle = service->end_handle;
        return 0;
    }
    if (error->status != BLE_HS_EDONE || remote_link.service_end_handle == 0) {
        drop_remote_link("the motion service was not found");
        return 0;
    }
    if (ble_gattc_disc_chrs_by_uuid(conn_handle, remote_link.service_start_handle, remote_link.service_end_handle,
                                    &motion_characteristic_uuid.u, handle_characteristic_discovered, NULL) != 0) {
        drop_remote_link("characteristic discovery failed to start");
    }
    return 0;
}

static int handle_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        if (advertises_motion_service(&event->disc)) {
            connect_to_remote(&event->disc.addr);
        }
        break;

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            start_scanning_for_remote();
            break;
        }
        remote_link = (struct remote_link){.connection_handle = event->connect.conn_handle};
        if (ble_gattc_disc_svc_by_uuid(event->connect.conn_handle, &motion_service_uuid.u, handle_service_discovered,
                                       NULL) != 0) {
            drop_remote_link("service discovery failed to start");
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        remote_link = (struct remote_link){.connection_handle = BLE_HS_CONN_HANDLE_NONE};
        record_remote_link(false, false);
        start_scanning_for_remote();
        break;

    case BLE_GAP_EVENT_NOTIFY_RX:
        if (event->notify_rx.attr_handle == remote_link.motion_value_handle) {
            record_remote_motion_payload(event->notify_rx.om);
        }
        break;
    }
    return 0;
}
