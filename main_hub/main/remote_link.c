/**
 * @file remote_link.c
 * @author Anthony Yalong
 * @brief The hub's BLE link to the remote node: scans for it, subscribes to its motion, and reports what it learns
 */

#include "remote_link.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "remote_motion_ble.h"
#include "main_hub_system_config.h"

static const char *TAG = "REMOTE_LINK";

static const ble_uuid128_t motion_service_uuid = BLE_UUID128_INIT(REMOTE_MOTION_BLE_SERVICE_UUID_BYTES);
static const ble_uuid128_t motion_characteristic_uuid =
    BLE_UUID128_INIT(REMOTE_MOTION_BLE_MOTION_CHARACTERISTIC_UUID_BYTES);

// only NimBLE's host task touches these, through the callbacks below, so they need no lock
static remote_link_state_callback_t on_remote_state;
static bool reported_remote_connected;
static bool reported_remote_motion_detected;
static uint8_t own_address_type;
static struct remote_connection {
    uint16_t connection_handle;
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    uint16_t motion_value_handle;
    uint16_t motion_notification_config_handle;
} remote_connection = {.connection_handle = BLE_HS_CONN_HANDLE_NONE};

static void run_nimble_host_task(void *param);
static void handle_ble_host_sync(void);
static void handle_ble_host_reset(int reason);
static void start_scanning_for_remote(void);
static int handle_gap_event(struct ble_gap_event *event, void *arg);

esp_err_t remote_link_start(remote_link_state_callback_t state_callback)
{
    on_remote_state = state_callback;
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        return err;
    }
    ble_hs_cfg.sync_cb = handle_ble_host_sync;
    ble_hs_cfg.reset_cb = handle_ble_host_reset;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    nimble_port_freertos_init(run_nimble_host_task);
    return ESP_OK;
}

// tells the app only when its view of the remote actually changes
static void report_remote_state(bool remote_connected, bool remote_motion_detected)
{
    if (remote_connected == reported_remote_connected && remote_motion_detected == reported_remote_motion_detected) {
        return;
    }
    reported_remote_connected = remote_connected;
    reported_remote_motion_detected = remote_motion_detected;
    on_remote_state(remote_connected, remote_motion_detected);
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

// the remote side failed or misbehaved, which is no reason to stop the hub: drop
// the connection, and the disconnect event starts a fresh scan
static void drop_remote_link(const char *reason)
{
    ESP_LOGW(TAG, "dropping the remote link: %s", reason);
    ble_gap_terminate(remote_connection.connection_handle, BLE_ERR_REM_USER_CONN_TERM);
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
    report_remote_state(true, remote_motion_detected);
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
    if (ble_gattc_read(conn_handle, remote_connection.motion_value_handle, handle_motion_read, NULL) != 0) {
        drop_remote_link("reading the current motion failed to start");
    }
    return 0;
}

static int handle_descriptor_discovered(uint16_t conn_handle, const struct ble_gatt_error *error,
                                        uint16_t chr_val_handle, const struct ble_gatt_dsc *descriptor, void *arg)
{
    if (error->status == 0) {
        // discovery runs to the end of the service, so only the first CCCD belongs to motion
        if (ble_uuid_u16(&descriptor->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16 &&
            remote_connection.motion_notification_config_handle == 0) {
            remote_connection.motion_notification_config_handle = descriptor->handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE || remote_connection.motion_notification_config_handle == 0) {
        drop_remote_link("the motion characteristic has no notification descriptor");
        return 0;
    }

    static const uint8_t enable_notifications[] = {0x01, 0x00};
    if (ble_gattc_write_flat(conn_handle, remote_connection.motion_notification_config_handle, enable_notifications,
                             sizeof(enable_notifications), handle_subscribed, NULL) != 0) {
        drop_remote_link("subscribing to motion failed to start");
    }
    return 0;
}

static int handle_characteristic_discovered(uint16_t conn_handle, const struct ble_gatt_error *error,
                                            const struct ble_gatt_chr *characteristic, void *arg)
{
    if (error->status == 0) {
        remote_connection.motion_value_handle = characteristic->val_handle;
        return 0;
    }
    if (error->status != BLE_HS_EDONE || remote_connection.motion_value_handle == 0) {
        drop_remote_link("the motion characteristic was not found");
        return 0;
    }
    if (ble_gattc_disc_all_dscs(conn_handle, remote_connection.motion_value_handle,
                                remote_connection.service_end_handle, handle_descriptor_discovered, NULL) != 0) {
        drop_remote_link("descriptor discovery failed to start");
    }
    return 0;
}

static int handle_service_discovered(uint16_t conn_handle, const struct ble_gatt_error *error,
                                     const struct ble_gatt_svc *service, void *arg)
{
    if (error->status == 0) {
        remote_connection.service_start_handle = service->start_handle;
        remote_connection.service_end_handle = service->end_handle;
        return 0;
    }
    if (error->status != BLE_HS_EDONE || remote_connection.service_end_handle == 0) {
        drop_remote_link("the motion service was not found");
        return 0;
    }
    if (ble_gattc_disc_chrs_by_uuid(conn_handle, remote_connection.service_start_handle,
                                    remote_connection.service_end_handle, &motion_characteristic_uuid.u,
                                    handle_characteristic_discovered, NULL) != 0) {
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
        remote_connection = (struct remote_connection){.connection_handle = event->connect.conn_handle};
        if (ble_gattc_disc_svc_by_uuid(event->connect.conn_handle, &motion_service_uuid.u, handle_service_discovered,
                                       NULL) != 0) {
            drop_remote_link("service discovery failed to start");
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        remote_connection = (struct remote_connection){.connection_handle = BLE_HS_CONN_HANDLE_NONE};
        report_remote_state(false, false);
        start_scanning_for_remote();
        break;

    case BLE_GAP_EVENT_NOTIFY_RX:
        if (event->notify_rx.attr_handle == remote_connection.motion_value_handle) {
            record_remote_motion_payload(event->notify_rx.om);
        }
        break;
    }
    return 0;
}
