/**
 * @file crash_limit.c
 * @author Anthony Yalong
 * @brief Per-build crash counting in NVS
 */

#include <stdio.h>
#include <string.h>
#include "crash_limit.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"

static const char *TAG = "CRASH_LIMIT";

#define CRASH_LIMIT_NVS_NAMESPACE "crash_limit"
#define IMAGE_SHA256_KEY "image_sha256"
#define IMAGE_SHA256_BYTES sizeof(((esp_app_desc_t *)0)->app_elf_sha256)

static const struct {
    esp_reset_reason_t reset_reason;
    const char *name;
} crash_types[] = {
    {ESP_RST_PANIC, "panic"},           {ESP_RST_INT_WDT, "interrupt_watchdog"}, {ESP_RST_TASK_WDT, "task_watchdog"},
    {ESP_RST_WDT, "watchdog"},          {ESP_RST_BROWNOUT, "brownout"},          {ESP_RST_PWR_GLITCH, "power_glitch"},
    {ESP_RST_CPU_LOCKUP, "cpu_lockup"}, {ESP_RST_EFUSE, "efuse_error"},
};
#define CRASH_TYPE_COUNT (sizeof(crash_types) / sizeof(crash_types[0]))

static void format_crash_count_key(esp_reset_reason_t reset_reason, char key[NVS_KEY_NAME_MAX_SIZE])
{
    snprintf(key, NVS_KEY_NAME_MAX_SIZE, "crashes_%d", (int)reset_reason);
}

static esp_err_t read_crash_count(nvs_handle_t nvs, esp_reset_reason_t reset_reason, uint8_t *crash_count)
{
    char key[NVS_KEY_NAME_MAX_SIZE];
    format_crash_count_key(reset_reason, key);
    *crash_count = 0;
    esp_err_t err = nvs_get_u8(nvs, key, crash_count);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

static esp_err_t clear_crash_counts_if_image_changed(nvs_handle_t nvs)
{
    const uint8_t *running_image_sha256 = esp_app_get_description()->app_elf_sha256;
    uint8_t stored_image_sha256[IMAGE_SHA256_BYTES];
    size_t stored_length = sizeof(stored_image_sha256);
    esp_err_t err = nvs_get_blob(nvs, IMAGE_SHA256_KEY, stored_image_sha256, &stored_length);
    bool same_image = err == ESP_OK && stored_length == IMAGE_SHA256_BYTES &&
                      memcmp(stored_image_sha256, running_image_sha256, IMAGE_SHA256_BYTES) == 0;
    if (same_image) {
        return ESP_OK;
    }
    // a hash of the wrong size can only come from some other build, so it counts as a different image
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND && err != ESP_ERR_NVS_INVALID_LENGTH) {
        return err;
    }

    ESP_RETURN_ON_ERROR(nvs_erase_all(nvs), TAG, "failed to clear the previous image's crash counts");
    return nvs_set_blob(nvs, IMAGE_SHA256_KEY, running_image_sha256, IMAGE_SHA256_BYTES);
}

static esp_err_t count_last_reset_if_crash(nvs_handle_t nvs)
{
    esp_reset_reason_t last_reset_reason = esp_reset_reason();
    for (int i = 0; i < CRASH_TYPE_COUNT; i++) {
        if (crash_types[i].reset_reason != last_reset_reason) {
            continue;
        }
        uint8_t crash_count;
        ESP_RETURN_ON_ERROR(read_crash_count(nvs, last_reset_reason, &crash_count), TAG, "failed to read crash count");
        crash_count++;
        ESP_LOGW(TAG, "crash_type=%s crash_count=%u", crash_types[i].name, crash_count);

        char key[NVS_KEY_NAME_MAX_SIZE];
        format_crash_count_key(last_reset_reason, key);
        return nvs_set_u8(nvs, key, crash_count);
    }
    return ESP_OK;
}

static esp_err_t find_crash_type_at_limit(nvs_handle_t nvs, crash_limit_verdict_t *verdict)
{
    for (int i = 0; i < CRASH_TYPE_COUNT; i++) {
        uint8_t crash_count;
        ESP_RETURN_ON_ERROR(read_crash_count(nvs, crash_types[i].reset_reason, &crash_count), TAG,
                            "failed to read crash count");
        if (crash_count >= CRASH_LIMIT_MAX_CRASHES_PER_TYPE) {
            verdict->limit_reached = true;
            verdict->crash_type = crash_types[i].name;
            return ESP_OK;
        }
    }
    return ESP_OK;
}

esp_err_t crash_limit_record_last_reset(crash_limit_verdict_t *verdict)
{
    *verdict = (crash_limit_verdict_t){.limit_reached = false, .crash_type = NULL};

    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(CRASH_LIMIT_NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "failed to open nvs");
    esp_err_t err = clear_crash_counts_if_image_changed(nvs);
    if (err == ESP_OK) {
        err = count_last_reset_if_crash(nvs);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    if (err == ESP_OK) {
        err = find_crash_type_at_limit(nvs, verdict);
    }
    nvs_close(nvs);
    return err;
}
