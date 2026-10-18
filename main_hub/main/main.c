/**
 * @file main.c
 * @author Anthony Yalong
 * @brief main security hub - multi-sensor integration with lcd display and ble client
 */

// imports
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "hcsr501.h"
#include "hcsr04.h"
#include "dht11.h"
#include "hd44780_pcf8574.h"
#include "main_hub_system_config.h"

static const char *TAG = "MAIN_HUB";

// ble configuration
#define REMOTE_DEVICE_NAME "ESP32_REMOTE"
#define REMOTE_SERVICE_UUID 0x180A
#define REMOTE_CHAR_UUID 0x2A58

// ============================================================================
// shared sensor data
// ============================================================================

static struct {
    bool local_motion_detected;
    float distance_cm;
    float temperature_celsius;
    float relative_humidity_percent;
    bool remote_motion_detected;
    bool remote_connected;
    SemaphoreHandle_t mutex;
} latest_readings;

// ============================================================================
// sensor instances
// ============================================================================

static hcsr501_sensor_t hcsr501_sensor;
static hcsr04_sensor_t hcsr04_sensor;
static dht11_sensor_t dht11_sensor;
static hd44780_t lcd;

// ble connection handle
static uint16_t remote_connection_handle = BLE_HS_CONN_HANDLE_NONE;

// ============================================================================
// function prototypes
// ============================================================================

/**
 * @brief create the i2c master bus the lcd is wired to
 *
 * @param bus receives the handle of the new bus
 * @return esp_err_t ESP_OK on success
 */
esp_err_t create_i2c_master_bus(i2c_master_bus_handle_t *bus);

/**
 * @brief pir motion sensor task - polls sensor every LOCAL_MOTION_TASK_PERIOD_MS
 *
 * @param pvParameters task parameters
 */
void local_motion_task(void *pvParameters);

/**
 * @brief ultrasonic distance sensor task - polls sensor every DISTANCE_TASK_PERIOD_MS
 *
 * @param pvParameters task parameters
 */
void distance_task(void *pvParameters);

/**
 * @brief dht11 temperature/humidity sensor task - polls sensor every TEMPERATURE_HUMIDITY_TASK_PERIOD_MS
 *
 * @param pvParameters task parameters
 */
void temperature_humidity_task(void *pvParameters);

/**
 * @brief lcd display task - updates display every DISPLAY_REFRESH_TASK_PERIOD_MS
 *
 * @param pvParameters task parameters
 */
void display_refresh_task(void *pvParameters);

/**
 * @brief ble client task - scans and connects to remote node
 *
 * @param pvParameters task parameters
 */
void ble_client_task(void *pvParameters);

/**
 * @brief ble gap event handler
 *
 * @param event gap event
 * @param arg user argument
 * @return int status code
 */
static int handle_gap_event(struct ble_gap_event *event, void *arg);

/**
 * @brief ble host task - runs nimble stack
 *
 * @param param task parameters
 */
static void run_nimble_host_task(void *param);

/**
 * @brief callback when ble stack is synced
 */
static void handle_ble_host_sync(void);

/**
 * @brief callback when ble stack resets
 *
 * @param reason reset reason
 */
static void handle_ble_host_reset(int reason);

// ============================================================================
// main application
// ============================================================================

void app_main(void)
{
    // error management
    esp_err_t err;

    ESP_LOGI(TAG, "main security hub starting...");

    // initialize nvs
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize nvs");
        return;
    }

    // create mutex for sensor data
    latest_readings.mutex = xSemaphoreCreateMutex();
    if (latest_readings.mutex == NULL) {
        ESP_LOGE(TAG, "failed to create mutex");
        return;
    }

    // initialize sensor data
    latest_readings.local_motion_detected = false;
    latest_readings.distance_cm = 0.0f;
    latest_readings.temperature_celsius = 0.0f;
    latest_readings.relative_humidity_percent = 0.0f;
    latest_readings.remote_motion_detected = false;
    latest_readings.remote_connected = false;

    // initialize i2c bus
    i2c_master_bus_handle_t i2c_bus;
    err = create_i2c_master_bus(&i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize i2c");
        return;
    }

    // initialize lcd
    err = hd44780_init(&lcd, i2c_bus, PCF8574_I2C_ADDRESS, PCF8574_I2C_CLOCK_HZ, LCD1602_COLUMNS, LCD1602_ROWS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize lcd");
        return;
    }

    // display startup message
    hd44780_clear(&lcd);
    hd44780_set_cursor(&lcd, 0, 0);
    hd44780_print(&lcd, "Security System");
    hd44780_set_cursor(&lcd, 0, 1);
    hd44780_print(&lcd, "Initializing...");
    vTaskDelay(pdMS_TO_TICKS(STARTUP_MESSAGE_MS));

    // initialize pir sensor
    err = hcsr501_init(&hcsr501_sensor, HCSR501_GPIO_PIN, HCSR501_DEBOUNCE_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize pir sensor");
        return;
    }

    // initialize ultrasonic sensor
    err = hcsr04_init(&hcsr04_sensor, HCSR04_TRIGGER_GPIO_PIN, HCSR04_ECHO_GPIO_PIN, HCSR04_ECHO_TIMEOUT_US);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize ultrasonic sensor");
        return;
    }

    // initialize dht11 sensor
    err = dht11_init(&dht11_sensor, DHT11_GPIO_PIN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize dht11 sensor");
        return;
    }

    // initialize ble
    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize nimble");
        return;
    }

    // configure ble host
    ble_hs_cfg.sync_cb = handle_ble_host_sync;
    ble_hs_cfg.reset_cb = handle_ble_host_reset;

    // initialize gatt services
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // start ble host task
    nimble_port_freertos_init(run_nimble_host_task);

    ESP_LOGI(TAG, "all sensors and ble initialized");

    // create sensor tasks
    xTaskCreate(local_motion_task, "local_motion_task", TASK_STACK_BYTES, NULL, LOCAL_MOTION_TASK_PRIORITY, NULL);
    xTaskCreate(distance_task, "distance_task", TASK_STACK_BYTES, NULL, DISTANCE_TASK_PRIORITY, NULL);
    xTaskCreate(temperature_humidity_task, "temperature_humidity_task", TASK_STACK_BYTES, NULL,
                TEMPERATURE_HUMIDITY_TASK_PRIORITY, NULL);
    xTaskCreate(ble_client_task, "ble_client_task", TASK_STACK_BYTES, NULL, BLE_CLIENT_TASK_PRIORITY, NULL);
    xTaskCreate(display_refresh_task, "display_refresh_task", TASK_STACK_BYTES, NULL, DISPLAY_REFRESH_TASK_PRIORITY,
                NULL);

    ESP_LOGI(TAG, "all tasks created - system running");
}

// ============================================================================
// helper functions
// ============================================================================

esp_err_t create_i2c_master_bus(i2c_master_bus_handle_t *bus)
{
    // i2c config
    i2c_master_bus_config_t bus_settings = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO_PIN,
        .scl_io_num = I2C_SCL_GPIO_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = I2C_GLITCH_FILTER_CYCLES,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_settings, bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to create i2c master bus");
        return err;
    }

    ESP_LOGI(TAG, "i2c master initialized");
    return ESP_OK;
}

// ============================================================================
// sensor tasks
// ============================================================================

void local_motion_task(void *pvParameters)
{
    ESP_LOGI(TAG, "pir task started");

    TickType_t last_wake_tick = xTaskGetTickCount();

    while (1) {
        // read pir sensor
        bool local_motion = hcsr501_read_level_and_count_motion_events(&hcsr501_sensor);

        // update shared data
        if (xSemaphoreTake(latest_readings.mutex, portMAX_DELAY) == pdTRUE) {
            latest_readings.local_motion_detected = local_motion;
            xSemaphoreGive(latest_readings.mutex);
        }

        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(LOCAL_MOTION_TASK_PERIOD_MS));
    }

    vTaskDelete(NULL);
}

void distance_task(void *pvParameters)
{
    ESP_LOGI(TAG, "ultrasonic task started");

    TickType_t last_wake_tick = xTaskGetTickCount();

    while (1) {
        // read ultrasonic sensor
        esp_err_t err = hcsr04_measure_distance(&hcsr04_sensor);

        if (err == ESP_OK) {
            float distance_cm = hcsr04_get_last_distance_cm(&hcsr04_sensor);

            // update shared data
            if (xSemaphoreTake(latest_readings.mutex, portMAX_DELAY) == pdTRUE) {
                latest_readings.distance_cm = distance_cm;
                xSemaphoreGive(latest_readings.mutex);
            }
        }

        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(DISTANCE_TASK_PERIOD_MS));
    }

    vTaskDelete(NULL);
}

void temperature_humidity_task(void *pvParameters)
{
    ESP_LOGI(TAG, "dht11 task started");

    TickType_t last_wake_tick = xTaskGetTickCount();

    while (1) {
        // read dht11 sensor
        esp_err_t err = dht11_read_temperature_and_humidity(&dht11_sensor);

        if (err == ESP_OK) {
            float temperature_celsius = dht11_get_last_temperature_celsius(&dht11_sensor);
            float relative_humidity_percent = dht11_get_last_relative_humidity_percent(&dht11_sensor);

            // update shared data
            if (xSemaphoreTake(latest_readings.mutex, portMAX_DELAY) == pdTRUE) {
                latest_readings.temperature_celsius = temperature_celsius;
                latest_readings.relative_humidity_percent = relative_humidity_percent;
                xSemaphoreGive(latest_readings.mutex);
            }
        }

        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(TEMPERATURE_HUMIDITY_TASK_PERIOD_MS));
    }

    vTaskDelete(NULL);
}

void display_refresh_task(void *pvParameters)
{
    ESP_LOGI(TAG, "lcd task started");

    TickType_t last_wake_tick = xTaskGetTickCount();

    // initialize display variables
    bool local_motion = false;
    bool remote_motion = false;
    float distance_cm = 0.0f;
    float temperature_celsius = 0.0f;
    float relative_humidity_percent = 0.0f;

    while (1) {
        // read shared sensor data
        if (xSemaphoreTake(latest_readings.mutex, portMAX_DELAY) == pdTRUE) {
            local_motion = latest_readings.local_motion_detected;
            remote_motion = latest_readings.remote_motion_detected;
            distance_cm = latest_readings.distance_cm;
            temperature_celsius = latest_readings.temperature_celsius;
            relative_humidity_percent = latest_readings.relative_humidity_percent;
            xSemaphoreGive(latest_readings.mutex);
        }

        // update lcd display
        hd44780_clear(&lcd);

        // line 1: motion and distance
        hd44780_set_cursor(&lcd, 0, 0);
        hd44780_printf(&lcd, "M:%c D:%.0fcm", local_motion ? 'Y' : 'N', distance_cm);

        // line 2: temperature and humidity
        hd44780_set_cursor(&lcd, 0, 1);
        hd44780_printf(&lcd, "T:%.0fC H:%.0f%%", temperature_celsius, relative_humidity_percent);

        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(DISPLAY_REFRESH_TASK_PERIOD_MS));
    }

    vTaskDelete(NULL);
}

// ============================================================================
// ble functions
// ============================================================================

static void run_nimble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void handle_ble_host_sync(void) { ESP_LOGI(TAG, "ble stack synced"); }

static void handle_ble_host_reset(int reason) { ESP_LOGE(TAG, "ble reset, reason: %d", reason); }

static int handle_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        // device discovered during scan
        if (event->disc.length_data > 0) {
            // check if this is our remote node
            ESP_LOGI(TAG, "device discovered");
        }
        break;

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "connected to remote node");
            remote_connection_handle = event->connect.conn_handle;

            // update connection status
            if (xSemaphoreTake(latest_readings.mutex, portMAX_DELAY) == pdTRUE) {
                latest_readings.remote_connected = true;
                xSemaphoreGive(latest_readings.mutex);
            }
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected from remote node");
        remote_connection_handle = BLE_HS_CONN_HANDLE_NONE;

        // update connection status
        if (xSemaphoreTake(latest_readings.mutex, portMAX_DELAY) == pdTRUE) {
            latest_readings.remote_connected = false;
            xSemaphoreGive(latest_readings.mutex);
        }
        break;
    }

    return 0;
}

void ble_client_task(void *pvParameters)
{
    ESP_LOGI(TAG, "ble client task started");

    // wait for ble stack to sync
    vTaskDelay(pdMS_TO_TICKS(1000));

    while (1) {
        // if not connected, try to scan/connect
        if (remote_connection_handle == BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGI(TAG, "scanning for remote node...");

            // TODO: implement ble scanning and connection
            // 1. scan for devices
            // 2. filter by name/service uuid
            // 3. connect to remote node
            // 4. discover services
            // 5. read motion characteristic

            vTaskDelay(pdMS_TO_TICKS(5000));
        } else {
            // TODO: implement characteristic reading
            // connected - periodically read motion characteristic from remote node

            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    vTaskDelete(NULL);
}
