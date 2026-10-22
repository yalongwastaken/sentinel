/**
 * @file main.c
 * @author Anthony Yalong
 * @brief Main hub: samples the local sensors, shows them on the LCD, and links to the remote node over BLE.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "nvs_flash.h"
#include "hcsr501.h"
#include "hcsr04.h"
#include "dht11.h"
#include "hd44780_pcf8574.h"
#include "crash_limit.h"
#include "remote_link.h"
#include "main_hub_system_config.h"

static const char *TAG = "MAIN_HUB";

// every task reads or writes these only while holding the mutex, and never
// holds it across sensor or I2C I/O
static struct {
    bool local_motion_detected;
    float distance_cm;
    bool distance_valid;
    float temperature_celsius;
    float relative_humidity_percent;
    bool temperature_humidity_valid;
    bool remote_motion_detected;
    bool remote_motion_valid;
    SemaphoreHandle_t mutex;
} latest_readings;

static hcsr501_sensor_t hcsr501_sensor;
static hcsr04_sensor_t hcsr04_sensor;
static dht11_sensor_t dht11_sensor;
static hd44780_t lcd;

static esp_err_t init_nvs_flash(void);
static void start_task_or_abort(TaskFunction_t task_function, const char *task_name, UBaseType_t priority);
esp_err_t create_i2c_master_bus(i2c_master_bus_handle_t *bus);
void local_motion_task(void *pvParameters);
void distance_task(void *pvParameters);
void temperature_humidity_task(void *pvParameters);
void display_refresh_task(void *pvParameters);
static esp_err_t refresh_display(void);
static void record_remote_link(bool remote_motion_valid, bool remote_motion_detected);

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
    ESP_ERROR_CHECK(hd44780_clear(&lcd));
    ESP_ERROR_CHECK(hd44780_set_cursor(&lcd, 0, 0));
    ESP_ERROR_CHECK(hd44780_print(&lcd, "Security System"));
    ESP_ERROR_CHECK(hd44780_set_cursor(&lcd, 0, 1));
    ESP_ERROR_CHECK(hd44780_print(&lcd, "Initializing..."));
    vTaskDelay(pdMS_TO_TICKS(STARTUP_MESSAGE_MS));

    ESP_ERROR_CHECK(hcsr501_init(&hcsr501_sensor, HCSR501_GPIO_PIN));
    ESP_ERROR_CHECK(hcsr04_init(&hcsr04_sensor, HCSR04_TRIGGER_GPIO_PIN, HCSR04_ECHO_GPIO_PIN, HCSR04_ECHO_TIMEOUT_US));
    ESP_ERROR_CHECK(dht11_init(&dht11_sensor, DHT11_GPIO_PIN));

    ESP_ERROR_CHECK(remote_link_start(record_remote_link));

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
        bool distance_valid = hcsr04_measure_distance_cm(&hcsr04_sensor, &distance_cm) == ESP_OK;
        xSemaphoreTake(latest_readings.mutex, portMAX_DELAY);
        latest_readings.distance_valid = distance_valid;
        if (distance_valid) {
            latest_readings.distance_cm = distance_cm;
        }
        xSemaphoreGive(latest_readings.mutex);

        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(DISTANCE_TASK_PERIOD_MS));
    }
}

void temperature_humidity_task(void *pvParameters)
{
    TickType_t last_wake_tick = xTaskGetTickCount();
    while (1) {
        dht11_reading_t reading;
        bool temperature_humidity_valid = dht11_read_temperature_and_humidity(&dht11_sensor, &reading) == ESP_OK;
        xSemaphoreTake(latest_readings.mutex, portMAX_DELAY);
        latest_readings.temperature_humidity_valid = temperature_humidity_valid;
        if (temperature_humidity_valid) {
            latest_readings.temperature_celsius = reading.temperature_celsius;
            latest_readings.relative_humidity_percent = reading.relative_humidity_percent;
        }
        xSemaphoreGive(latest_readings.mutex);

        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(TEMPERATURE_HUMIDITY_TASK_PERIOD_MS));
    }
}

void display_refresh_task(void *pvParameters)
{
    TickType_t last_wake_tick = xTaskGetTickCount();
    bool display_reset_required = false;
    while (1) {
        esp_err_t display_error = display_reset_required ? hd44780_reset(&lcd) : ESP_OK;
        if (display_error == ESP_OK) {
            display_error = refresh_display();
        }
        if (display_error != ESP_OK) {
            if (!display_reset_required) {
                ESP_LOGW(TAG, "display_available=false error=%s", esp_err_to_name(display_error));
            }
            display_reset_required = true;
            // a slow failed transaction must not trigger immediate catch-up retries
            last_wake_tick = xTaskGetTickCount();
        } else if (display_reset_required) {
            ESP_LOGI(TAG, "display_available=true");
            display_reset_required = false;
        }

        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(DISPLAY_REFRESH_TASK_PERIOD_MS));
    }
}

static esp_err_t refresh_display(void)
{
    xSemaphoreTake(latest_readings.mutex, portMAX_DELAY);
    bool local_motion = latest_readings.local_motion_detected;
    bool remote_motion_valid = latest_readings.remote_motion_valid;
    bool remote_motion = latest_readings.remote_motion_detected;
    float distance_cm = latest_readings.distance_cm;
    bool distance_valid = latest_readings.distance_valid;
    float temperature_celsius = latest_readings.temperature_celsius;
    float relative_humidity_percent = latest_readings.relative_humidity_percent;
    bool temperature_humidity_valid = latest_readings.temperature_humidity_valid;
    xSemaphoreGive(latest_readings.mutex);

    char remote_motion_symbol = !remote_motion_valid ? '-' : (remote_motion ? 'Y' : 'N');
    ESP_RETURN_ON_ERROR(hd44780_clear(&lcd), TAG, "display clear failed");
    ESP_RETURN_ON_ERROR(hd44780_set_cursor(&lcd, 0, 0), TAG, "display first-row cursor failed");
    ESP_RETURN_ON_ERROR(hd44780_printf(&lcd, "M:%c R:%c D:", local_motion ? 'Y' : 'N', remote_motion_symbol), TAG,
                        "display motion write failed");
    esp_err_t distance_write_error =
        distance_valid ? hd44780_printf(&lcd, "%.0fcm", distance_cm) : hd44780_print(&lcd, "--cm");
    ESP_RETURN_ON_ERROR(distance_write_error, TAG, "display distance write failed");
    ESP_RETURN_ON_ERROR(hd44780_set_cursor(&lcd, 0, 1), TAG, "display second-row cursor failed");
    return temperature_humidity_valid
               ? hd44780_printf(&lcd, "T:%.0fC H:%.0f%%", temperature_celsius, relative_humidity_percent)
               : hd44780_print(&lcd, "T:--C H:--%");
}

// remote_link calls this, on NimBLE's host task, only when its view of the remote changes
static void record_remote_link(bool remote_motion_valid, bool remote_motion_detected)
{
    xSemaphoreTake(latest_readings.mutex, portMAX_DELAY);
    latest_readings.remote_motion_valid = remote_motion_valid;
    latest_readings.remote_motion_detected = remote_motion_detected;
    xSemaphoreGive(latest_readings.mutex);

    if (remote_motion_valid) {
        ESP_LOGI(TAG, "remote_motion_valid=true remote_motion=%s", remote_motion_detected ? "true" : "false");
    } else {
        ESP_LOGI(TAG, "remote_motion_valid=false");
    }
}
