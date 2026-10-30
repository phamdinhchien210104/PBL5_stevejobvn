/* Local HTTPS OTA demo, based on Espressif's public-domain advanced OTA example. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"

static const char *TAG = "local_ota";
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");

static void log_running_firmware(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Running project=%s version=%s ESP-IDF=%s", app->project_name,
             app->version, app->idf_ver);
    ESP_LOGI(TAG, "Running partition=%s address=0x%08" PRIx32,
             running->label, running->address);
}

/* A local-only demo need not depend on an Internet NTP server. Until real time
 * is available, use the build timestamp to check the local certificate dates.
 * Generate certificates before building A/B. Production should use trusted time.
 */
static void seed_demo_clock(void)
{
    if (time(NULL) >= 1704067200) { /* Already set to 2024 or later. */
        return;
    }
    const esp_app_desc_t *app = esp_app_get_description();
    struct tm build_time = { .tm_isdst = -1 };
    char timestamp[64];
    snprintf(timestamp, sizeof(timestamp), "%s %s", app->date, app->time);
    if (strptime(timestamp, "%b %d %Y %H:%M:%S", &build_time) != NULL) {
        struct timeval tv = { .tv_sec = mktime(&build_time) };
        if (settimeofday(&tv, NULL) == 0) {
            ESP_LOGW(TAG, "Demo TLS clock seeded from firmware build time: %s", timestamp);
        }
    }
}

static esp_err_t validate_image_header(const esp_app_desc_t *incoming)
{
    const esp_app_desc_t *running = esp_app_get_description();
    ESP_LOGI(TAG, "Offered project=%.*s version=%.*s", (int)sizeof(incoming->project_name),
             incoming->project_name, (int)sizeof(incoming->version), incoming->version);
    if (strncmp(incoming->project_name, running->project_name, sizeof(incoming->project_name)) != 0) {
        ESP_LOGE(TAG, "Rejected: firmware belongs to a different project");
        return ESP_ERR_INVALID_ARG;
    }
    if (strncmp(incoming->version, running->version, sizeof(incoming->version)) == 0) {
        ESP_LOGI(TAG, "Same version; keeping the running firmware");
        return ESP_ERR_INVALID_VERSION;
    }
    return ESP_OK;
}

static esp_err_t perform_update(void)
{
    if (strncmp(CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL, "https://", 8) != 0) {
        ESP_LOGE(TAG, "Configure an https:// firmware URL in menuconfig");
        return ESP_ERR_INVALID_ARG;
    }
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (target == NULL) {
        ESP_LOGE(TAG, "No OTA slot. Flash the demo partition table over USB first");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "Checking %s", CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL);
    ESP_LOGI(TAG, "Update target=%s capacity=%" PRIu32 " bytes", target->label, target->size);

    esp_http_client_config_t http_config = {
        .url = CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL,
        .cert_pem = (const char *)server_cert_pem_start,
        .timeout_ms = CONFIG_EXAMPLE_OTA_RECV_TIMEOUT,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_config = { .http_config = &http_config };
    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_app_desc_t incoming;
    err = esp_https_ota_get_img_desc(handle, &incoming);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot read image descriptor: %s", esp_err_to_name(err));
        goto abort_update;
    }
    err = validate_image_header(&incoming);
    if (err != ESP_OK) {
        goto abort_update;
    }

    int last_progress = -10;
    int last_bytes = 0;
    const int image_size = esp_https_ota_get_image_size(handle);
    do {
        err = esp_https_ota_perform(handle);
        const int received = esp_https_ota_get_image_len_read(handle);
        if (image_size > 0) {
            int progress = (int)((int64_t)received * 100 / image_size);
            if (progress >= last_progress + 10) {
                ESP_LOGI(TAG, "Download %d%% (%d/%d bytes)", progress, received, image_size);
                last_progress = progress;
            }
        } else if (received - last_bytes >= 65536) {
            ESP_LOGI(TAG, "Downloaded %d bytes", received);
            last_bytes = received;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    } while (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Download failed: %s", esp_err_to_name(err));
        goto abort_update;
    }
    if (!esp_https_ota_is_complete_data_received(handle)) {
        ESP_LOGE(TAG, "Incomplete image; keeping the running firmware");
        err = ESP_ERR_INVALID_SIZE;
        goto abort_update;
    }

    /* finish() frees the handle, including on failure: do not abort it again. */
    err = esp_https_ota_finish(handle);
    if (err == ESP_OK) {
        const esp_partition_t *boot = esp_ota_get_boot_partition();
        ESP_LOGI(TAG, "OTA SUCCESS. Next boot=%s. Restarting...", boot->label);
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_restart();
    }
    ESP_LOGE(TAG, "Image validation/finalization failed: %s", esp_err_to_name(err));
    return err;

abort_update:
    esp_https_ota_abort(handle);
    return err;
}

static void ota_task(void *arg)
{
    while (1) {
        ESP_LOGI(TAG, "Next OTA check in %d seconds", CONFIG_EXAMPLE_OTA_CHECK_INTERVAL);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_EXAMPLE_OTA_CHECK_INTERVAL * 1000));
        perform_update();
    }
}

void app_main(void)
{
    log_running_firmware();
    seed_demo_clock();
    /* For this demo rollback is off by default. Support a user-enabled rollback
     * configuration without leaving the new image pending on every boot.
     */
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
    }
#endif
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());
    if (xTaskCreate(ota_task, "local_ota", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Cannot create OTA task");
        abort();
    }
}
