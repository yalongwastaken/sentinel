/**
 * @file main.c
 * @author Anthony Yalong
 * @brief Remote node: samples its PIR sensor and notifies the hub over BLE whenever motion changes.
 */

#include <stdatomic.h>
#include <stdlib.h>
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
#include "crash_limit.h"
#include "remote_motion_ble.h"
#include "remote_node_system_config.h"

static const char *TAG = "REMOTE_NODE";

static const ble_uuid128_t motion_service_uuid = BLE_UUID128_INIT(REMOTE_MOTION_BLE_SERVICE_UUID_BYTES);
static const ble_uuid128_t motion_characteristic_uuid =
    BLE_UUID128_INIT(REMOTE_MOTION_BLE_MOTION_CHARACTERISTIC_UUID_BYTES);

static hcsr501_sensor_t hcsr501_sensor;
// the sensor task is the only writer; NimBLE reads this from its host task
static atomic_bool motion_detected = false;
static uint16_t motion_characteristic_handle;

static esp_err_t init_nvs_flash(void);
static void abort_on_nimble_error(int rc, const char *operation);
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
        .uuid = &motion_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){{
                                                           .uuid = &motion_characteristic_uuid.u,
                                                           .access_cb = handle_motion_characteristic_access,
                                                           .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                                                           .val_handle = &motion_characteristic_handle,
                                                       },
                                                       {0}},
    },
    {0}};

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

    ESP_ERROR_CHECK(hcsr501_init(&hcsr501_sensor, HCSR501_GPIO_PIN));

    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = handle_ble_host_sync;
    ble_hs_cfg.reset_cb = handle_ble_host_reset;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    abort_on_nimble_error(ble_gatts_count_cfg(gatt_services), "counting the gatt services");
    abort_on_nimble_error(ble_gatts_add_svcs(gatt_services), "adding the gatt services");
    abort_on_nimble_error(ble_svc_gap_device_name_set(REMOTE_MOTION_BLE_DEVICE_NAME), "setting the device name");
    nimble_port_freertos_init(run_nimble_host_task);

    BaseType_t created = xTaskCreate(motion_sample_task, "motion_sample_task", MOTION_SAMPLE_TASK_STACK_BYTES, NULL,
                                     MOTION_SAMPLE_TASK_PRIORITY, NULL);
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_LOGI(TAG, "remote node running");
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

// NimBLE reports failure as a nonzero int; any failure here leaves the hub unable
// to see this node, so the board aborts and retries like a failed startup
static void abort_on_nimble_error(int rc, const char *operation)
{
    if (rc != 0) {
        ESP_LOGE(TAG, "%s failed: nimble_rc=%d", operation, rc);
        abort();
    }
}

// the service UUID and the name together exceed a 31-byte advertisement, so the
// UUID the hub scans for is advertised and the name rides in the scan response
static void start_advertising(void)
{
    struct ble_hs_adv_fields advertised_fields = {0};
    advertised_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    advertised_fields.uuids128 = &motion_service_uuid;
    advertised_fields.num_uuids128 = 1;
    advertised_fields.uuids128_is_complete = 1;
    abort_on_nimble_error(ble_gap_adv_set_fields(&advertised_fields), "setting the advertisement");

    struct ble_hs_adv_fields scan_response_fields = {0};
    scan_response_fields.name = (const uint8_t *)REMOTE_MOTION_BLE_DEVICE_NAME;
    scan_response_fields.name_len = strlen(REMOTE_MOTION_BLE_DEVICE_NAME);
    scan_response_fields.name_is_complete = 1;
    abort_on_nimble_error(ble_gap_adv_rsp_set_fields(&scan_response_fields), "setting the scan response");

    struct ble_gap_adv_params advertising_params = {0};
    advertising_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    advertising_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    abort_on_nimble_error(
        ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &advertising_params, handle_gap_event, NULL),
        "starting advertising");
}

static int handle_motion_characteristic_access(uint16_t conn_handle, uint16_t attr_handle,
                                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    uint8_t payload = remote_motion_ble_encode_motion(atomic_load(&motion_detected));
    return os_mbuf_append(ctxt->om, &payload, REMOTE_MOTION_BLE_MOTION_PAYLOAD_BYTES) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int handle_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "hub_connected=true");
        } else {
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "hub_connected=false");
        start_advertising();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == motion_characteristic_handle) {
            ESP_LOGI(TAG, "hub_subscribed=%s", event->subscribe.cur_notify ? "true" : "false");
        }
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
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

static void handle_ble_host_sync(void) { start_advertising(); }

static void handle_ble_host_reset(int reason) { ESP_LOGE(TAG, "ble host reset: nimble_reason=%d", reason); }

static void motion_sample_task(void *pvParameters)
{
    TickType_t last_wake_tick = xTaskGetTickCount();
    while (1) {
        bool motion = hcsr501_is_motion_detected(&hcsr501_sensor);
        if (motion != atomic_load(&motion_detected)) {
            atomic_store(&motion_detected, motion);
            ESP_LOGI(TAG, "motion=%s", motion ? "true" : "false");
            // notifies every subscribed hub, reading the new value through the access callback
            ble_gatts_chr_updated(motion_characteristic_handle);
        }

        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(MOTION_SAMPLE_TASK_PERIOD_MS));
    }
}
