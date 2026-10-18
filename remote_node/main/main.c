/**
 * @file main.c
 * @author Anthony Yalong
 * @brief remote sensor node - ble server with pir motion detection
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "pir.h"
#include "remote_node_system_config.h"

static const char *TAG = "REMOTE_NODE";

// sensor data
static uint8_t motion_detected = 0;
static uint16_t motion_char_handle;

// ============================================================================
// function prototypes
// ============================================================================

/**
 * @brief advertise ble service for remote node discovery
 */
static void ble_advertise(void);

/**
 * @brief handle gatt characteristic read requests
 *
 * @param conn_handle connection handle
 * @param attr_handle attribute handle
 * @param ctxt gatt access context
 * @param arg user argument
 * @return int status code
 */
static int motion_char_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

/**
 * @brief handle ble gap events (connect, disconnect, advertising)
 *
 * @param event gap event structure
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
 * @brief callback when ble stack is synced and ready
 */
static void ble_on_sync(void);

/**
 * @brief callback when ble stack resets
 *
 * @param reason reset reason code
 */
static void ble_on_reset(int reason);

/**
 * @brief sensor reading task - polls pir sensor and updates motion data
 *
 * @param pvParameters task parameters
 */
static void sensor_task(void *pvParameters);

// ============================================================================
// gatt service definition
// ============================================================================

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]){{
                                                           .uuid = BLE_UUID16_DECLARE(BLE_MOTION_CHAR_UUID),
                                                           .access_cb = motion_char_access,
                                                           .flags = BLE_GATT_CHR_F_READ,
                                                           .val_handle = &motion_char_handle,
                                                       },
                                                       {0}},
    },
    {0}};

// ============================================================================
// main application
// ============================================================================

void app_main(void)
{
    // error management
    esp_err_t ret;

    ESP_LOGI(TAG, "remote sensor node starting...");

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

    // initialize nimble
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize nimble");
        return;
    }

    // configure host
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    // initialize gatt services
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    // set device name
    ble_svc_gap_device_name_set(BLE_DEVICE_NAME);

    // start ble host task
    nimble_port_freertos_init(ble_host_task);

    // create sensor reading task
    xTaskCreate(sensor_task, "sensor_task", SENSOR_TASK_STACK_SIZE, NULL, SENSOR_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "initialization complete");
}

// ============================================================================
// ble functions
// ============================================================================

static void ble_advertise(void)
{
    struct ble_hs_adv_fields fields = {0};

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)BLE_DEVICE_NAME;
    fields.name_len = strlen(BLE_DEVICE_NAME);
    fields.name_is_complete = 1;

    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);

    ESP_LOGI(TAG, "ble advertising started");
}

static int motion_char_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        os_mbuf_append(ctxt->om, &motion_detected, sizeof(motion_detected));
        ESP_LOGI(TAG, "motion data read: %d", motion_detected);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "client connected");
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "client disconnected, restarting advertising");
        ble_advertise();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "advertising complete, restarting");
        ble_advertise();
        break;
    }
    return 0;
}

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_on_sync(void)
{
    ESP_LOGI(TAG, "ble stack synced");
    ble_advertise();
}

static void ble_on_reset(int reason) { ESP_LOGE(TAG, "ble reset, reason: %d", reason); }

// ============================================================================
// sensor task
// ============================================================================

static void sensor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "sensor task started");

    // initialize pir
    pir_sensor_t pir;
    pir_init(&pir, PIR_GPIO_PIN, PIR_DEBOUNCE_TIME_MS);

    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        // read pir sensor (simulated for now)
        motion_detected = (motion_detected == 0) ? 1 : 0;

        ESP_LOGI(TAG, "motion status: %d", motion_detected);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS));
    }

    vTaskDelete(NULL);
}