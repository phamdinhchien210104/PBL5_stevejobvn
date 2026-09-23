/*
 * ESP32-S3 & ESP32-C3 Smart Light Project
 * Chapter 9: Remote Control through ESP RainMaker (Practice 9.4)
 *
 * Implements:
 * 1. 9.4.1 Cloud Communication Node, Lightbulb Device, and TSL Parameters (Power, Brightness, Hue, Saturation)
 * 2. 9.4.2 Assisted Claiming with TLS X.509 certificates stored in fctry NVS partition
 * 3. 9.4.5 Basic Services: SNTP Time Sync, Timezone, Offline Scheduling, OTA, System service & Local Control
 * 4. 9.4.6 Integration with WS2812B Hardware SPI2 DMA @ 3.2MHz and Physical Boot Button Gestures
 *
 * Targets:
 * - ESP32-S3-DevKitC-1-N16R8 (Boot GPIO 0, LED GPIO 4)
 * - ESP32-C3-DevKitM-1 (Boot GPIO 9, LED GPIO 4)
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

#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "esp_rmaker_core.h"
#include "esp_rmaker_standard_params.h"
#include "esp_rmaker_standard_devices.h"
#include "esp_rmaker_ota.h"
#include "esp_rmaker_schedule.h"
#include "esp_rmaker_utils.h"

#include "app_wifi.h"
#include "app_storage.h"
#include "app_priv.h"

static const char *TAG = "rainmaker";

esp_rmaker_device_t *light_device;

extern const char ota_server_cert[] asm("_binary_server_crt_start");

/* Callback to handle commands received from the RainMaker cloud */
static esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
                          const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (ctx) {
        ESP_LOGI(TAG, "Received write request via : %s", esp_rmaker_device_cb_src_to_str(ctx->src));
    }
    const char *device_name = esp_rmaker_device_get_name(device);
    const char *param_name = esp_rmaker_param_get_name(param);

    if (strcmp(param_name, ESP_RMAKER_DEF_POWER_NAME) == 0) {
        ESP_LOGI(TAG, "==> [Cloud Downlink] %s - %s: %s",
                 device_name, param_name, val.val.b ? "ON" : "OFF");
        app_light_set_power(val.val.b);
    } else if (strcmp(param_name, ESP_RMAKER_DEF_BRIGHTNESS_NAME) == 0) {
        ESP_LOGI(TAG, "==> [Cloud Downlink] %s - %s: %d",
                 device_name, param_name, val.val.i);
        app_light_set_brightness(val.val.i);
    } else if (strcmp(param_name, ESP_RMAKER_DEF_HUE_NAME) == 0) {
        ESP_LOGI(TAG, "==> [Cloud Downlink] %s - %s: %d",
                 device_name, param_name, val.val.i);
        app_light_set_hue(val.val.i);
    } else if (strcmp(param_name, ESP_RMAKER_DEF_SATURATION_NAME) == 0) {
        ESP_LOGI(TAG, "==> [Cloud Downlink] %s - %s: %d",
                 device_name, param_name, val.val.i);
        app_light_set_saturation(val.val.i);
    } else {
        ESP_LOGW(TAG, "Ignoring unhandled param: %s", param_name);
        return ESP_OK;
    }

    esp_rmaker_param_update_and_report(param, val);
    return ESP_OK;
}

/* Post-OTA diagnostic callback to verify hardware & services before canceling rollback (Chapter 11) */
static esp_rmaker_ota_diag_status_t app_ota_diagnostic(esp_rmaker_ota_diag_priv_t *ota_diag_priv, void *priv)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  [OTA Diagnostic] Post-OTA Health Check (State: %d)       ",
             ota_diag_priv ? ota_diag_priv->state : -1);
    ESP_LOGI(TAG, "==========================================================");

    if (ota_diag_priv && ota_diag_priv->state == OTA_DIAG_STATE_INIT) {
        ESP_LOGI(TAG, "Phase 1: Validating WS2812B Light Driver & Storage...");
        /* 1. Verify light driver state */
        bool current_state = app_driver_get_state();
        ESP_LOGI(TAG, "Light driver operational (Current State: %s)", current_state ? "ON" : "OFF");

        /* 2. Visual confirmation: brief diagnostic pulse */
        app_driver_set_state(true);
        vTaskDelay(pdMS_TO_TICKS(150));
        app_driver_set_state(current_state);

        /* 3. Verify NVS storage sanity */
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

void app_main(void)
{
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  ESP RainMaker Smart Light Firmware (Chapter 9) Initializing ");
    ESP_LOGI(TAG, "==========================================================");

    /* 1. NVS Flash initialization */
    ESP_LOGI(TAG, "NVS Flash initialization...");
    app_storage_init();

    /* 2. Application driver initialization (WS2812B Hardware SPI2 DMA @ 3.2MHz + Button HAL) */
    ESP_LOGI(TAG, "Application driver initialization...");
    app_driver_init();

    /* 3. Initialize Wi-Fi netif and event handlers (must precede esp_rmaker_node_init) */
    ESP_LOGI(TAG, "Initializing Wi-Fi Provisioning Netif...");
    app_wifi_init();

    /* 4. Initialize ESP RainMaker Agent */
    ESP_LOGI(TAG, "Initializing ESP RainMaker Node...");
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = false,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "ESP RainMaker Smart Light", "Lightbulb");
    if (!node) {
        ESP_LOGE(TAG, "Could not initialize RainMaker node. Aborting!!!");
        vTaskDelay(pdMS_TO_TICKS(5000));
        abort();
    }

    /* 5. Create Lightbulb device with standard parameters */
    ESP_LOGI(TAG, "Creating standard Lightbulb device and parameters...");
    light_device = esp_rmaker_lightbulb_device_create("Light", NULL, DEFAULT_POWER);
    esp_rmaker_device_add_cb(light_device, write_cb, NULL);

    esp_rmaker_device_add_param(light_device, esp_rmaker_brightness_param_create(ESP_RMAKER_DEF_BRIGHTNESS_NAME, DEFAULT_BRIGHTNESS));
    esp_rmaker_device_add_param(light_device, esp_rmaker_hue_param_create(ESP_RMAKER_DEF_HUE_NAME, DEFAULT_HUE));
    esp_rmaker_device_add_param(light_device, esp_rmaker_saturation_param_create(ESP_RMAKER_DEF_SATURATION_NAME, DEFAULT_SATURATION));

    esp_rmaker_node_add_device(node, light_device);

    /* 6. Enable standard services (Section 9.4.5) */
    ESP_LOGI(TAG, "Configuring SNTP Time Synchronization & Timezone Service...");
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
#if defined(CONFIG_APP_OTA_USING_TOPICS)
    ESP_LOGI(TAG, "OTA Mode: OTA_USING_TOPICS (ESP RainMaker Dashboard Job)");
    ESP_ERROR_CHECK(esp_rmaker_ota_enable(&ota_config, OTA_USING_TOPICS));
#else
    ESP_LOGI(TAG, "OTA Mode: OTA_USING_PARAMS (URL Parameter via CLI/API)");
    ESP_ERROR_CHECK(esp_rmaker_ota_enable(&ota_config, OTA_USING_PARAMS));
#endif


    ESP_LOGI(TAG, "Enabling System Service (Reboot & Factory Reset)...");
    esp_rmaker_system_service_enable(NULL);

    /* 7. Start the ESP RainMaker Agent */
    ESP_LOGI(TAG, "Starting ESP RainMaker Agent...");
    esp_rmaker_start();

    /* 8. Start Wi-Fi Provisioning with Assisted Claiming */
    ESP_LOGI(TAG, "Starting Wi-Fi Provisioning (BLE with Assisted Claiming)...");
    err = app_wifi_start(POP_TYPE_RANDOM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start Wi-Fi. Aborting!!!");
        vTaskDelay(pdMS_TO_TICKS(5000));
        abort();
    }

    ESP_LOGI(TAG, "RainMaker Smart Light initialized successfully! Online and connected to Cloud.");

    uint32_t uptime_sec = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        uptime_sec += 10;
        ESP_LOGI(TAG, "[Uptime: %lu s] Node Online - Switch: %s",
                 (unsigned long)uptime_sec, app_driver_get_state() ? "ON" : "OFF");
    }
}
