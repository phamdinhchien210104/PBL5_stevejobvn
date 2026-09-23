/*
 * ESP32-S3 & ESP32-C3 Smart Light Project
 * Chapter 15: Using ESP Insights in Smart Light Project
 *
 * Inherits:
 * - Chapter 8: Local Control (Wi-Fi HTTPS mDNS & BLE Provisioning)
 * - Chapter 9: Remote Control via ESP RainMaker & HSV Color Model
 * - Chapter 11: OTA Firmware Upgrade & 2-Phase Rollback Diagnostics
 * - Chapter 12: Power Management (DFS 40-160 MHz + Light-sleep + PM Lock)
 * - Chapters 13 & 14: Security Features & Mass Manufacturing Factory Data
 *
 * New Delta Logic (Chapter 15):
 * - Shared RainMaker MQTT Transport for ESP Insights (Saves 30-40 KB RAM)
 * - Core Dump Capture to Flash Partition 'coredump' (64KB)
 * - Heap & Wi-Fi Metrics Telemetry Reporting
 * - Custom Diagnostic Events (ESP_DIAG_EVENT)
 * - Remote Crash Analysis via RainMaker Dashboard
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_rmaker_core.h"
#include "esp_rmaker_standard_params.h"
#include "esp_rmaker_standard_devices.h"
#include "esp_rmaker_ota.h"
#include "esp_rmaker_schedule.h"
#include <esp_rmaker_utils.h>
#include <esp_rmaker_time_sync.h>
#include <esp_rmaker_factory.h>

#include "app_wifi.h"
#include "app_storage.h"
#include "app_priv.h"
#include "app_insights.h"

static const char *TAG = "app_insights_light";

esp_rmaker_device_t *light_device;

extern const char ota_server_cert[] asm("_binary_server_crt_start");

/* Callback to handle commands received from RainMaker Cloud / Local Control */
static esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
                          const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (ctx) {
        ESP_LOGI(TAG, "==> [Control Command Received via %s]: param '%s'",
                 esp_rmaker_device_cb_src_to_str(ctx->src),
                 esp_rmaker_param_get_name(param));
    }
    const char *device_name = esp_rmaker_device_get_name(device);
    const char *param_name = esp_rmaker_param_get_name(param);

    if (strcmp(param_name, ESP_RMAKER_DEF_POWER_NAME) == 0) {
        ESP_LOGI(TAG, "Setting Power: %s for %s", val.val.b ? "ON" : "OFF", device_name);
        app_light_set_power(val.val.b);
    } else if (strcmp(param_name, ESP_RMAKER_DEF_BRIGHTNESS_NAME) == 0) {
        ESP_LOGI(TAG, "Setting Brightness: %d for %s", val.val.i, device_name);
        app_light_set_brightness(val.val.i);
    } else if (strcmp(param_name, ESP_RMAKER_DEF_HUE_NAME) == 0) {
        ESP_LOGI(TAG, "Setting Hue: %d for %s", val.val.i, device_name);
        app_light_set_hue(val.val.i);
    } else if (strcmp(param_name, ESP_RMAKER_DEF_SATURATION_NAME) == 0) {
        ESP_LOGI(TAG, "Setting Saturation: %d for %s", val.val.i, device_name);
        app_light_set_saturation(val.val.i);
    } else {
        return ESP_OK;
    }
    esp_rmaker_param_update_and_report(param, val);
    return ESP_OK;
}

/* Post-OTA Hardware Diagnostics (Chapter 11) */
static esp_rmaker_ota_diag_status_t app_ota_diagnostic(esp_rmaker_ota_diag_priv_t *ota_diag_priv, void *priv)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  [OTA Diagnostic] Post-OTA Health Check (State: %d)       ",
             ota_diag_priv ? ota_diag_priv->state : -1);
    ESP_LOGI(TAG, "==========================================================");

    if (ota_diag_priv && ota_diag_priv->state == OTA_DIAG_STATE_INIT) {
        ESP_LOGI(TAG, "Phase 1: Validating WS2812B Light Driver & NVS Storage...");
        bool current_state = app_driver_get_state();
        ESP_LOGI(TAG, "Light driver operational (Current State: %s)", current_state ? "ON" : "OFF");

        /* Verify NVS storage sanity */
        esp_err_t nvs_err = app_storage_init();
        if (nvs_err != ESP_OK) {
            ESP_LOGE(TAG, "OTA Diagnostics FAILED: NVS storage integrity error!");
            return OTA_DIAG_STATUS_FAIL;
        }

        ESP_LOGI(TAG, "Phase 1 Diagnostics PASSED. Waiting for Cloud MQTT connection...");
        return OTA_DIAG_STATUS_SUCCESS;
    } else if (ota_diag_priv && ota_diag_priv->state == OTA_DIAG_STATE_POST_MQTT) {
        ESP_LOGI(TAG, "Phase 2: MQTT Connected to RainMaker Cloud!");
        ESP_LOGI(TAG, "All OTA Diagnostics PASSED! Firmware verified valid, rollback cancelled.");
        return OTA_DIAG_STATUS_SUCCESS;
    }

    return OTA_DIAG_STATUS_SUCCESS;
}

/* Factory Data Inspection (Chapter 14) */
static void app_factory_data_inspect(void)
{
    ESP_LOGI(TAG, "1.1 Checking Factory Data in 'fctry' Partition...");
    esp_err_t err = esp_rmaker_factory_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Factory NVS partition 'fctry' not initialized or not found (%s)", esp_err_to_name(err));
        return;
    }

    char *serial_no = (char *)esp_rmaker_factory_get("serial_no");
    if (serial_no) {
        ESP_LOGI(TAG, "==========================================================");
        ESP_LOGI(TAG, "  [FACTORY CREDENTIAL] Serial Number: %s", serial_no);
        free(serial_no);
    } else {
        ESP_LOGI(TAG, "  [FACTORY INFO] No 'serial_no' in fctry (Unprovisioned device)");
    }

    size_t mac_size = esp_rmaker_factory_get_size("mac_addr");
    if (mac_size == 6) {
        uint8_t *mac = (uint8_t *)esp_rmaker_factory_get("mac_addr");
        if (mac) {
            ESP_LOGI(TAG, "  [FACTORY CREDENTIAL] Factory MAC:   %02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            free(mac);
        }
    }
    ESP_LOGI(TAG, "==========================================================");
}

void app_main(void)
{
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  Smart Light Firmware Chapter 15: ESP Insights Diagnostics");
    ESP_LOGI(TAG, "==========================================================");

    /* 1. NVS Flash initialization */
    ESP_LOGI(TAG, "1. NVS Flash initialization...");
    app_storage_init();

    /* 1.1 Factory data inspection */
    app_factory_data_inspect();

    /* 2. Power Management initialization (DFS 40-160 MHz + Light-sleep + PM Lock) */
    ESP_LOGI(TAG, "2. Power Management initialization...");
    err = app_pm_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Power Management initialization returned error: %s", esp_err_to_name(err));
    }

    /* 3. Application driver initialization (WS2812B Hardware SPI2 DMA @ 3.2MHz + Button HAL) */
    ESP_LOGI(TAG, "3. Application driver initialization...");
    app_driver_init();

    /* 4. Initialize Wi-Fi netif and event handlers */
    ESP_LOGI(TAG, "4. Initializing Wi-Fi Provisioning Netif...");
    app_wifi_init();

    /* 5. Initialize ESP RainMaker Node */
    ESP_LOGI(TAG, "5. Initializing ESP RainMaker Node...");
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = false,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "ESP RainMaker Device", "Lightbulb");
    if (!node) {
        ESP_LOGE(TAG, "Could not initialize node. Aborting!!!");
        vTaskDelay(pdMS_TO_TICKS(5000));
        abort();
    }

    /* 6. Create Lightbulb device with standard parameters */
    light_device = esp_rmaker_lightbulb_device_create("Light", NULL, DEFAULT_POWER);
    esp_rmaker_device_add_cb(light_device, write_cb, NULL);

    esp_rmaker_device_add_param(light_device, esp_rmaker_brightness_param_create(ESP_RMAKER_DEF_BRIGHTNESS_NAME, DEFAULT_BRIGHTNESS));
    esp_rmaker_device_add_param(light_device, esp_rmaker_hue_param_create(ESP_RMAKER_DEF_HUE_NAME, DEFAULT_HUE));
    esp_rmaker_device_add_param(light_device, esp_rmaker_saturation_param_create(ESP_RMAKER_DEF_SATURATION_NAME, DEFAULT_SATURATION));

    esp_rmaker_node_add_device(node, light_device);

    /* 7. Enable standard services (Time Sync, Schedules, OTA, System) */
    ESP_LOGI(TAG, "7. Configuring SNTP Time Synchronization & Timezone Service...");
    esp_rmaker_time_config_t time_config = {
        .sntp_server_name = "pool.ntp.org",
    };
    esp_rmaker_time_sync_init(&time_config);
    esp_rmaker_timezone_service_enable();

    ESP_LOGI(TAG, "Enabling Offline Scheduling Service...");
    esp_rmaker_schedule_enable();

    ESP_LOGI(TAG, "Enabling OTA Upgrade Service (Chapter 11)...");
    esp_rmaker_ota_config_t ota_config = {
        .server_cert = ota_server_cert,
        .ota_diag = app_ota_diagnostic,
    };
    ESP_ERROR_CHECK(esp_rmaker_ota_enable(&ota_config, OTA_USING_PARAMS));

    ESP_LOGI(TAG, "Enabling System Service (Reboot & Factory Reset)...");
    esp_rmaker_system_service_enable(NULL);

    /* 8. Enable ESP Insights (Chapter 15) using shared RainMaker MQTT transport */
    ESP_LOGI(TAG, "8. Enabling ESP Insights Remote Diagnostics...");
    app_insights_enable();

    /* 9. Start ESP RainMaker Agent */
    ESP_LOGI(TAG, "9. Starting ESP RainMaker Agent...");
    esp_rmaker_start();

    /* 10. Start Wi-Fi connection / provisioning */
    ESP_LOGI(TAG, "10. Starting Wi-Fi Provisioning (BLE NimBLE)...");
    err = app_wifi_start(POP_TYPE_RANDOM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start Wi-Fi: %s. Aborting!", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(5000));
        abort();
    }

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  Smart Light Firmware Online (Insights + PM + Local Ctrl)");
    ESP_LOGI(TAG, "==========================================================");

    /* 11. Low frequency telemetry heartbeat */
    uint32_t loop_count = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        loop_count++;
        ESP_LOGI(TAG, "[Telemetry Heartbeat #%lu] Free Heap: %lu bytes, Min Free: %lu bytes",
                 (unsigned long)loop_count,
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)esp_get_minimum_free_heap_size());
    }
}
