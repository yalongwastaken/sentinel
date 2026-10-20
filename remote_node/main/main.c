/**
 * @file main.c
 * @author Anthony Yalong
 * @brief Remote node: samples its PIR sensor and serves the reading over a BLE GATT service.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "hcsr501.h"
#include "remote_node_system_config.h"

static const char *TAG = "REMOTE_NODE";

static uint8_t motion_detected = 0;
static uint16_t motion_characteristic_handle;

static void start_advertising(void);
static int handle_motion_characteristic_access(uint16_t conn_handle, uint16_t attr_handle,
                                               struct ble_gatt_access_ctxt *ctxt, void *arg);
static int handle_gap_event(struct ble_gap_event *event, void *arg);
static void run_nimble_host_task(void *param);
static void handle_ble_host_sync(void);
static void handle_ble_host_reset(int reason);
static void motion_sample_task(void *pvParameters);

static const struct ble_gatt_svc_def gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]){{
                                                           .uuid = BLE_UUID16_DECLARE(BLE_MOTION_CHAR_UUID),
                                                           .access_cb = handle_motion_characteristic_access,
                                                           .flags = BLE_GATT_CHR_F_READ,
                                                           .val_handle = &motion_characteristic_handle,
                                                       },
                                                       {0}},
    },
    {0}};

void app_main(void)
{
    ESP_LOGI(TAG, "remote sensor node starting...");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize nvs");
        return;
    }

    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize nimble");
        return;
    }
    ble_hs_cfg.sync_cb = handle_ble_host_sync;
    ble_hs_cfg.reset_cb = handle_ble_host_reset;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_services);
    ble_gatts_add_svcs(gatt_services);
    ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    nimble_port_freertos_init(run_nimble_host_task);

    xTaskCreate(motion_sample_task, "motion_sample_task", MOTION_SAMPLE_TASK_STACK_BYTES, NULL,
                MOTION_SAMPLE_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "initialization complete");
}

static void start_advertising(void)
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
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, handle_gap_event, NULL);

    ESP_LOGI(TAG, "ble advertising started");
}

static int handle_motion_characteristic_access(uint16_t conn_handle, uint16_t attr_handle,
                                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        os_mbuf_append(ctxt->om, &motion_detected, sizeof(motion_detected));
        ESP_LOGD(TAG, "motion data read: %d", motion_detected);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int handle_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "client connected");
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "client disconnected, restarting advertising");
        start_advertising();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "advertising complete, restarting");
        start_advertising();
        break;
    }
    return 0;
}

static void run_nimble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void handle_ble_host_sync(void)
{
    ESP_LOGI(TAG, "ble stack synced");
    start_advertising();
}

static void handle_ble_host_reset(int reason) { ESP_LOGE(TAG, "ble reset, reason: %d", reason); }

static void motion_sample_task(void *pvParameters)
{
    hcsr501_sensor_t hcsr501_sensor;
    hcsr501_init(&hcsr501_sensor, HCSR501_GPIO_PIN);

    TickType_t last_wake_tick = xTaskGetTickCount();
    while (1) {
        // read pir sensor (simulated for now)
        motion_detected = (motion_detected == 0) ? 1 : 0;
        ESP_LOGD(TAG, "motion=%s", motion_detected ? "true" : "false");

        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(MOTION_SAMPLE_TASK_PERIOD_MS));
    }
}
