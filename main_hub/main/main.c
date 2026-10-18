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
#include "pir.h"
#include "hcsr04.h"
#include "dht11.h"
#include "lcd_i2c.h"
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
    bool motion_detected;
    float distance_cm;
    float temperature;
    float humidity;
    bool remote_motion_detected;
    bool remote_connected;
    SemaphoreHandle_t mutex;
} sensor_data;

// ============================================================================
// sensor instances
// ============================================================================

static pir_sensor_t pir_sensor;
static hcsr04_sensor_t ultrasonic_sensor;
static dht11_sensor_t dht11_sensor;
static lcd_handle_t lcd;

// ble connection handle
static uint16_t ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;

// ============================================================================
// function prototypes
// ============================================================================

/**
 * @brief initialize i2c master bus
 *
 * @param bus receives the handle of the new bus
 * @return esp_err_t ESP_OK on success
 */
esp_err_t i2c_master_init(i2c_master_bus_handle_t *bus);

/**
 * @brief pir motion sensor task - polls sensor every 100ms
 *
 * @param pvParameters task parameters
 */
void pir_task(void *pvParameters);

/**
 * @brief ultrasonic distance sensor task - polls sensor every 200ms
 *
 * @param pvParameters task parameters
 */
void ultrasonic_task(void *pvParameters);

/**
 * @brief dht11 temperature/humidity sensor task - polls sensor every 3s
 *
 * @param pvParameters task parameters
 */
void dht11_task(void *pvParameters);

/**
 * @brief lcd display task - updates display every 1s
 *
 * @param pvParameters task parameters
 */
void lcd_task(void *pvParameters);

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
static int ble_gap_event(struct ble_gap_event *event, void *arg);

/**
 * @brief ble host task - runs nimble stack
 *
 * @param param task parameters
 */
static void ble_host_task(void *param);

/**
 * @brief callback when ble stack is synced
 */
static void ble_on_sync(void);

/**
 * @brief callback when ble stack resets
 *
 * @param reason reset reason
 */
static void ble_on_reset(int reason);

// ============================================================================
// main application
// ============================================================================

void app_main(void)
{
    // error management
    esp_err_t ret;

    ESP_LOGI(TAG, "main security hub starting...");

    // initialize nvs
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize nvs");
        return;
    }

    // create mutex for sensor data
    sensor_data.mutex = xSemaphoreCreateMutex();
    if (sensor_data.mutex == NULL) {
        ESP_LOGE(TAG, "failed to create mutex");
        return;
    }

    // initialize sensor data
    sensor_data.motion_detected = false;
    sensor_data.distance_cm = 0.0f;
    sensor_data.temperature = 0.0f;
    sensor_data.humidity = 0.0f;
    sensor_data.remote_motion_detected = false;
    sensor_data.remote_connected = false;

    // initialize i2c bus
    i2c_master_bus_handle_t i2c_bus;
    ret = i2c_master_init(&i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize i2c");
        return;
    }

    // initialize lcd
    ret = lcd_init(&lcd, i2c_bus, LCD_ADDR, I2C_MASTER_FREQ_HZ, LCD_COLUMNS, LCD_ROWS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize lcd");
        return;
    }

    // display startup message
    lcd_clear(&lcd);
    lcd_set_cursor(&lcd, 0, 0);
    lcd_print(&lcd, "Security System");
    lcd_set_cursor(&lcd, 0, 1);
    lcd_print(&lcd, "Initializing...");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // initialize pir sensor
    ret = pir_init(&pir_sensor, PIR_GPIO_PIN, PIR_DEBOUNCE_TIME_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize pir sensor");
        return;
    }

    // initialize ultrasonic sensor
    ret = hcsr04_init(&ultrasonic_sensor, HCSR04_PIN_TRIG, HCSR04_PIN_ECHO, HCSR04_TIMEOUT_US);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize ultrasonic sensor");
        return;
    }

    // initialize dht11 sensor
    ret = dht11_init(&dht11_sensor, DHT11_GPIO_PIN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize dht11 sensor");
        return;
    }

    // initialize ble
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize nimble");
        return;
    }

    // configure ble host
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    // initialize gatt services
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // start ble host task
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "all sensors and ble initialized");

    // create sensor tasks
    xTaskCreate(pir_task, "pir_task", 4096, NULL, 5, NULL);
    xTaskCreate(ultrasonic_task, "ultrasonic_task", 4096, NULL, 4, NULL);
    xTaskCreate(dht11_task, "dht11_task", 4096, NULL, 3, NULL);
    xTaskCreate(ble_client_task, "ble_client_task", 4096, NULL, 3, NULL);
    xTaskCreate(lcd_task, "lcd_task", 4096, NULL, 2, NULL);

    ESP_LOGI(TAG, "all tasks created - system running");
}

// ============================================================================
// helper functions
// ============================================================================

esp_err_t i2c_master_init(i2c_master_bus_handle_t *bus)
{
    // i2c config
    i2c_master_bus_config_t config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = I2C_MASTER_GLITCH_FILTER_CYCLES,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&config, bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to create i2c master bus");
        return ret;
    }

    ESP_LOGI(TAG, "i2c master initialized");
    return ESP_OK;
}

// ============================================================================
// sensor tasks
// ============================================================================

void pir_task(void *pvParameters)
{
    ESP_LOGI(TAG, "pir task started");

    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        // read pir sensor
        bool motion = pir_read(&pir_sensor);

        // update shared data
        if (xSemaphoreTake(sensor_data.mutex, portMAX_DELAY) == pdTRUE) {
            sensor_data.motion_detected = motion;
            xSemaphoreGive(sensor_data.mutex);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));
    }

    vTaskDelete(NULL);
}

void ultrasonic_task(void *pvParameters)
{
    ESP_LOGI(TAG, "ultrasonic task started");

    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        // read ultrasonic sensor
        esp_err_t ret = hcsr04_read_distance(&ultrasonic_sensor);

        if (ret == ESP_OK) {
            float distance = hcsr04_get_last_distance(&ultrasonic_sensor);

            // update shared data
            if (xSemaphoreTake(sensor_data.mutex, portMAX_DELAY) == pdTRUE) {
                sensor_data.distance_cm = distance;
                xSemaphoreGive(sensor_data.mutex);
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(200));
    }

    vTaskDelete(NULL);
}

void dht11_task(void *pvParameters)
{
    ESP_LOGI(TAG, "dht11 task started");

    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        // read dht11 sensor
        esp_err_t ret = dht11_read(&dht11_sensor);

        if (ret == ESP_OK) {
            float temp = dht11_get_temperature(&dht11_sensor);
            float humidity = dht11_get_humidity(&dht11_sensor);

            // update shared data
            if (xSemaphoreTake(sensor_data.mutex, portMAX_DELAY) == pdTRUE) {
                sensor_data.temperature = temp;
                sensor_data.humidity = humidity;
                xSemaphoreGive(sensor_data.mutex);
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(3000));
    }

    vTaskDelete(NULL);
}

void lcd_task(void *pvParameters)
{
    ESP_LOGI(TAG, "lcd task started");

    TickType_t last_wake = xTaskGetTickCount();

    // initialize display variables
    bool motion = false;
    bool remote_motion = false;
    float distance = 0.0f;
    float temp = 0.0f;
    float humidity = 0.0f;

    while (1) {
        // read shared sensor data
        if (xSemaphoreTake(sensor_data.mutex, portMAX_DELAY) == pdTRUE) {
            motion = sensor_data.motion_detected;
            remote_motion = sensor_data.remote_motion_detected;
            distance = sensor_data.distance_cm;
            temp = sensor_data.temperature;
            humidity = sensor_data.humidity;
            xSemaphoreGive(sensor_data.mutex);
        }

        // update lcd display
        lcd_clear(&lcd);

        // line 1: motion and distance
        lcd_set_cursor(&lcd, 0, 0);
        lcd_printf(&lcd, "M:%c D:%.0fcm", motion ? 'Y' : 'N', distance);

        // line 2: temperature and humidity
        lcd_set_cursor(&lcd, 0, 1);
        lcd_printf(&lcd, "T:%.0fC H:%.0f%%", temp, humidity);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }

    vTaskDelete(NULL);
}

// ============================================================================
// ble functions
// ============================================================================

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_on_sync(void) { ESP_LOGI(TAG, "ble stack synced"); }

static void ble_on_reset(int reason) { ESP_LOGE(TAG, "ble reset, reason: %d", reason); }

static int ble_gap_event(struct ble_gap_event *event, void *arg)
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
            ble_conn_handle = event->connect.conn_handle;

            // update connection status
            if (xSemaphoreTake(sensor_data.mutex, portMAX_DELAY) == pdTRUE) {
                sensor_data.remote_connected = true;
                xSemaphoreGive(sensor_data.mutex);
            }
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected from remote node");
        ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;

        // update connection status
        if (xSemaphoreTake(sensor_data.mutex, portMAX_DELAY) == pdTRUE) {
            sensor_data.remote_connected = false;
            xSemaphoreGive(sensor_data.mutex);
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
        if (ble_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
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