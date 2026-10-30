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
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"

#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "esp_rmaker_core.h"
#include "esp_rmaker_standard_types.h"
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

/* Callback xử lý tập lệnh từ Cloud/App RainMaker theo cơ chế Bulk Write tránh giật lag UI */
static esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_write_req_t write_req[],
                          uint8_t count, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (ctx) {
        ESP_LOGI(TAG, "Received write request via : %s (%d params)", esp_rmaker_device_cb_src_to_str(ctx->src), count);
    }
    const char *device_name = esp_rmaker_device_get_name(device);

    for (uint8_t i = 0; i < count; i++) {
        const esp_rmaker_param_t *param = write_req[i].param;
        esp_rmaker_param_val_t val = write_req[i].val;
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
        } else if (strcmp(param_name, "Effect") == 0) {
            ESP_LOGI(TAG, "==> [Cloud Downlink] %s - %s: %s",
                     device_name, param_name, val.val.s);
            esp_err_t effect_err = app_light_set_effect(val.val.s);
            if (effect_err != ESP_OK) {
                ESP_LOGW(TAG, "Effect request rejected: %s", esp_err_to_name(effect_err));
                return effect_err;
            }
        } else {
            ESP_LOGW(TAG, "Ignoring unhandled param: %s", param_name);
            continue;
        }

        /* Cập nhật giá trị vào Node Shadow cục bộ */
        esp_rmaker_param_update(param, val);
    }

    /* 
     * QUAN TRỌNG: Nếu lệnh đến từ Cloud hoặc Local Control (App điện thoại gửi xuống),
     * Cloud/App đã biết trước giá trị này. Tuyệt đối KHÔNG gọi param_update_and_report
     * để tránh bắn ngược MQTT publish lên AWS IoT Core khi vuốt nhanh thanh trượt/bảng màu,
     * dẫn đến cạn kiệt hạn ngạch ("Out of MQTT Budget. Dropping publish message").
     * Chỉ gọi report khi nguồn thay đổi xuất phát từ nút bấm vật lý / cục bộ trên node.
     */
    if (ctx && (ctx->src != ESP_RMAKER_REQ_SRC_CLOUD && ctx->src != ESP_RMAKER_REQ_SRC_LOCAL)) {
        esp_rmaker_param_update_and_report(write_req[count - 1].param, write_req[count - 1].val);
    }

    return ESP_OK;
}

static void on_wifi_sta_start(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    wifi_country_t country = {
        .cc = "VN",
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_AUTO,
    };
    esp_wifi_set_country(&country);

    /* Không gọi esp_wifi_set_config() tại đây vì Wi-Fi STA đã chuyển sang trạng thái Connecting */
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(48); // 12 dBm - chuẩn ổn định cho ESP32-C3 chống sụt áp nguồn RF
}


void app_main(void)
{
    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  ESP RainMaker Smart Light Firmware (Chapter 9) Initializing ");
    ESP_LOGI(TAG, "==========================================================");

    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Running project=%s version=%s ESP-IDF=%s partition=%s",
              app->project_name, app->version, app->idf_ver, running->label);
    ESP_LOGI(TAG, "Police effect: %s", APP_ENABLE_POLICE_EFFECT ? "enabled" : "disabled");

    /* 1. NVS Flash initialization */
    ESP_LOGI(TAG, "NVS Flash initialization...");
    app_storage_init();

    /* 2. Application driver initialization (WS2812B Hardware SPI2 DMA @ 3.2MHz + Button HAL) */
    ESP_LOGI(TAG, "Application driver initialization...");
    app_driver_init();

    /* 3. Initialize Wi-Fi netif and event handlers (must precede esp_rmaker_node_init) */
    ESP_LOGI(TAG, "Initializing Wi-Fi Provisioning Netif...");
    app_wifi_init();

    /* Đăng ký Event Handler để tinh chỉnh kết nối ổn định mỗi khi Wi-Fi STA khởi động */
    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_START, on_wifi_sta_start, NULL, NULL);

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
    esp_rmaker_device_add_bulk_cb(light_device, write_cb, NULL);

    esp_rmaker_device_add_param(light_device, esp_rmaker_brightness_param_create(ESP_RMAKER_DEF_BRIGHTNESS_NAME, DEFAULT_BRIGHTNESS));
    esp_rmaker_device_add_param(light_device, esp_rmaker_hue_param_create(ESP_RMAKER_DEF_HUE_NAME, DEFAULT_HUE));
    esp_rmaker_device_add_param(light_device, esp_rmaker_saturation_param_create(ESP_RMAKER_DEF_SATURATION_NAME, DEFAULT_SATURATION));

    /* Firmware A advertises four effects; firmware B adds Police. */
    size_t effect_count = 0;
    const char **effects = app_light_get_effect_names(&effect_count);
    esp_rmaker_param_t *effect_param = esp_rmaker_param_create("Effect", "esp.param.mode",
                                                              esp_rmaker_str("Static"),
                                                              PROP_FLAG_READ | PROP_FLAG_WRITE);
    if (effect_param) {
        esp_rmaker_param_add_ui_type(effect_param, ESP_RMAKER_UI_DROPDOWN);
        esp_rmaker_param_add_valid_str_list(effect_param, effects, effect_count);
        esp_rmaker_device_add_param(light_device, effect_param);
    }

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

    /* Dashboard OTA jobs use MQTT topics for notification, HTTPS for the image.
     * The default helper selects OTA_USING_TOPICS and the standard server trust.
     */
    ESP_LOGI(TAG, "Enabling RainMaker Dashboard OTA (topics + HTTPS)...");
    ESP_ERROR_CHECK(esp_rmaker_ota_enable_default());

    ESP_LOGI(TAG, "Enabling System Service (Reboot & Factory Reset)...");
    esp_rmaker_system_serv_config_t system_serv_config = {
        .flags = SYSTEM_SERV_FLAG_REBOOT | SYSTEM_SERV_FLAG_FACTORY_RESET,
        .reboot_seconds = 2,
        .reset_seconds = 2,
        .reset_reboot_seconds = 2,
    };
    esp_rmaker_system_service_enable(&system_serv_config);

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
