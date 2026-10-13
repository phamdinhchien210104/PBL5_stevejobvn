/*
 * ESP32-S3 & ESP32-C3 Smart Light Project
 * Chapter 8: Local Control in Smart Light Project (Practice 8.5)
 *
 * Implements:
 * 1. 8.5.1 Local Control Server over Wi-Fi + HTTPS + mDNS (esp_local_ctrl)
 * 2. 8.5.2 Client Verification Interface & Status Property (JSON payload)
 * 3. 8.5.3 Fallback Local Control Server over Bluetooth LE (GATT Server)
 *
 * =========================================================================================
 * BẢNG TRA CỨU LỆNH ĐIỀU KHIỂN & CẤU HÌNH QUA BLUETOOTH LE (GATT SERVER)
 * Tên thiết bị quảng bá: ESP32C3-LIGHT (hoặc ESP32S3-LIGHT)
 * App hỗ trợ: nRF Connect, LightBlue, BLE Scanner trên Android / iOS
 * =========================================================================================
 *
 * [1] DỊCH VỤ ĐỌC TRẠNG THÁI ĐÈN (READ STATUS SERVICE):
 *     - Service UUID:        0x00FF
 *     - Characteristic UUID: 0xFF01 (Thuộc tính: READ | NOTIFY)
 *     - Giá trị trả về:
 *       + 0x01 (Byte): Đèn đang BẬT (ON)
 *       + 0x00 (Byte): Đèn đang TẮT (OFF)
 *
 * [2] DỊCH VỤ GHI LỆNH ĐIỀU KHIỂN & ĐỔI WI-FI (WRITE CONTROL SERVICE):
 *     - Service UUID:        0x00EE
 *     - Characteristic UUID: 0xEE01 (Thuộc tính: WRITE | WRITE NO RESP | READ)
 *
 *     ┌────────────────────┬──────────────┬──────────────┬────────────────────────────────────────────────────────┐
 *     │ Mục đích lệnh      │ Định dạng    │ Giá trị mẫu  │ Diễn giải chi tiết                                     │
 *     ├────────────────────┼──────────────┼──────────────┼────────────────────────────────────────────────────────┤
 *     │ 1. BẬT ĐÈN         │ Text / UTF-8 │ 1            │ Bật đèn sáng theo màu & độ sáng đã lưu                │
 *     │                    │ Hex / Byte   │ 0x01         │ (Tương đương 1 click nút Boot vật lý)                  │
 *     ├────────────────────┼──────────────┼──────────────┼────────────────────────────────────────────────────────┤
 *     │ 2. TẮT ĐÈN         │ Text / UTF-8 │ 0            │ Tắt hoàn toàn dải LED WS2812B                          │
 *     │                    │ Hex / Byte   │ 0x00         │ (Tương đương 1 click nút Boot vật lý)                  │
 *     ├────────────────────┼──────────────┼──────────────┼────────────────────────────────────────────────────────┤
 *     │ 3. ĐỔI MÀU SẮC     │ Text / UTF-8 │ 11           │ Chuyển vòng tròn 8 màu RGB: Đỏ -> Xanh lá -> Xanh dương│
 *     │                    │ Hex / Byte   │ 0x0B         │ -> Vàng -> Tím -> Cyan -> Trắng -> Cam (Double click)  │
 *     ├────────────────────┼──────────────┼──────────────┼────────────────────────────────────────────────────────┤
 *     │ 4. TĂNG ĐỘ SÁNG    │ Text / UTF-8 │ +            │ Tăng +20% độ sáng dải LED WS2812B                      │
 *     │                    │ Hex / Byte   │ 0x2B         │ (Tối đa 100%)                                          │
 *     ├────────────────────┼──────────────┼──────────────┼────────────────────────────────────────────────────────┤
 *     │ 5. GIẢM ĐỘ SÁNG    │ Text / UTF-8 │ -            │ Giảm -20% độ sáng dải LED WS2812B                      │
 *     │                    │ Hex / Byte   │ 0x2D         │ (Tối thiểu 5%)                                         │
 *     ├────────────────────┼──────────────┼──────────────┼────────────────────────────────────────────────────────┤
 *     │ 6. ĐỔI WI-FI MỚI   │ Text / UTF-8 │ SSID,PASS    │ Tự động lưu SSID & Mật khẩu vào NVS Flash vĩnh viễn:  │
 *     │    (Cứu hộ mạng)   │ Ví dụ:       │ Home,123456  │ - ESP32 tự ngắt kết nối Wi-Fi cũ                       │
 *     │                    │              │ wifi:SSID,PW │ - Kết nối sang router Wi-Fi mới                        │
 *     │                    │              │              │ - Khi có IP: TỰ ĐỘNG TẮT BLUETOOTH, BẬT HTTPS LAN!     │
 *     └────────────────────┴──────────────┴──────────────┴────────────────────────────────────────────────────────┘
 *
 * Hardware:
 * - ESP32-S3-DevKitC-1-N16R8 (Boot GPIO 0, LED GPIO 4)
 * - ESP32-C3-DevKitM-1 (Boot GPIO 9, LED GPIO 4)
 * - WS2812B NeoPixel 8-LED strip via Hardware SPI2 DMA @ 3.2MHz
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "mdns.h"
#include "esp_local_ctrl.h"
#include "esp_https_server.h"

#include "app_storage.h"
#include "app_priv.h"

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

/* Fallback macro definitions if not configured via Kconfig menuconfig */
#ifndef CONFIG_LOCAL_CTRL_WIFI_SSID
#define CONFIG_LOCAL_CTRL_WIFI_SSID "MyHomeWiFi"
#endif
#ifndef CONFIG_LOCAL_CTRL_WIFI_PASSWORD
#define CONFIG_LOCAL_CTRL_WIFI_PASSWORD "12345678"
#endif
#ifndef CONFIG_LOCAL_CTRL_MAXIMUM_RETRY
#define CONFIG_LOCAL_CTRL_MAXIMUM_RETRY 5
#endif
#ifndef CONFIG_LOCAL_CTRL_MDNS_HOST_NAME
#define CONFIG_LOCAL_CTRL_MDNS_HOST_NAME "my_esp_ctrl_device"
#endif
#ifndef CONFIG_LOCAL_CTRL_BLE_DEVICE_NAME
#define CONFIG_LOCAL_CTRL_BLE_DEVICE_NAME "ESP32-LOCAL-LIGHT"
#endif

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "local_ctrl";

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;

static char s_wifi_ssid[33] = CONFIG_LOCAL_CTRL_WIFI_SSID;
static char s_wifi_pass[65] = CONFIG_LOCAL_CTRL_WIFI_PASSWORD;

/* Prototype for BLE fallback */
static void ble_local_ctrl_init(void);
static void ble_local_ctrl_stop(void);

/* Prototype for Local Control HTTPS start */
static void esp_local_ctrl_service_start(void);

/* Prototype for dynamic Wi-Fi reconnect */
static void wifi_apply_new_credentials(const char *ssid, const char *pass);

/* =========================================================================
 * 1. WI-FI STATION & EVENT SYNCHRONIZATION
 * ========================================================================= */

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "==> [Wi-Fi] Bắt đầu kết nối tới AP SSID: %s...", s_wifi_ssid);
        app_driver_set_wifi_status(WIFI_STATUS_CONNECTING);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < CONFIG_LOCAL_CTRL_MAXIMUM_RETRY) {
            s_retry_num++;
            ESP_LOGW(TAG, "==> [Wi-Fi] Mất kết nối! Đang thử lại lần [%d/%d]...",
                     s_retry_num, CONFIG_LOCAL_CTRL_MAXIMUM_RETRY);
            app_driver_set_wifi_status(WIFI_STATUS_CONNECTING);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "==> [Wi-Fi] Kết nối thất bại sau %d lần thử lại!", CONFIG_LOCAL_CTRL_MAXIMUM_RETRY);
            app_driver_set_wifi_status(WIFI_STATUS_FAILED);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);

            /* Khi thử 5 lần thất bại (kể cả sau khi đổi Wi-Fi sai): tự động bật lại Bluetooth Fallback */
            ble_local_ctrl_init();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;

        wifi_ap_record_t ap_info = {0};
        char bssid_str[24] = "N/A";
        char ssid_str[33] = {0};
        snprintf(ssid_str, sizeof(ssid_str), "%s", s_wifi_ssid);
        int channel = 0;
        int rssi = 0;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            snprintf(ssid_str, sizeof(ssid_str), "%s", (char *)ap_info.ssid);
            snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     ap_info.bssid[0], ap_info.bssid[1], ap_info.bssid[2],
                     ap_info.bssid[3], ap_info.bssid[4], ap_info.bssid[5]);
            channel = ap_info.primary;
            rssi = ap_info.rssi;
        }

        ESP_LOGI(TAG, "==========================================================");
        ESP_LOGI(TAG, "==> [Wi-Fi] KẾT NỐI THÀNH CÔNG! ĐÃ CÓ ĐỊA CHỈ IP:        ");
        ESP_LOGI(TAG, "  - Tên Wi-Fi (SSID) : %s", ssid_str);
        ESP_LOGI(TAG, "  - BSSID (MAC AP)   : %s (Kênh %d, Sóng %d dBm)", bssid_str, channel, rssi);
        ESP_LOGI(TAG, "  - Địa chỉ IP cấp   : " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "  - Địa chỉ Mask     : " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "  - Địa chỉ Gateway  : " IPSTR, IP2STR(&event->ip_info.gw));
        ESP_LOGI(TAG, "==========================================================");
        s_retry_num = 0;
        app_driver_set_wifi_status(WIFI_STATUS_CONNECTED);
        xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        /* Tắt Bluetooth khi Wi-Fi đã kết nối thành công (Lựa chọn 1) */
        ble_local_ctrl_stop();

        /* Khởi động Local Control HTTPS Server & mDNS nếu chưa chạy */
        esp_local_ctrl_service_start();
    }
}

static void wifi_load_stored_credentials(void)
{
    char stored_ssid[33] = {0};
    char stored_pass[65] = {0};

    if (app_storage_get("wifi_ssid", stored_ssid, sizeof(stored_ssid)) == ESP_OK && strlen(stored_ssid) > 0) {
        strncpy(s_wifi_ssid, stored_ssid, sizeof(s_wifi_ssid) - 1);
        s_wifi_ssid[sizeof(s_wifi_ssid) - 1] = '\0';
        ESP_LOGI(TAG, "Đã đọc SSID lưu từ NVS: '%s'", s_wifi_ssid);
    }
    if (app_storage_get("wifi_pass", stored_pass, sizeof(stored_pass)) == ESP_OK) {
        strncpy(s_wifi_pass, stored_pass, sizeof(s_wifi_pass) - 1);
        s_wifi_pass[sizeof(s_wifi_pass) - 1] = '\0';
        ESP_LOGI(TAG, "Đã đọc Mật khẩu Wi-Fi lưu từ NVS");
    }
}

static void wifi_initialize(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Cấu hình quốc gia Việt Nam (kênh 1 - 13) */
    wifi_country_t country = {
        .cc = "VN",
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_AUTO,
    };
    esp_wifi_set_country(&country);

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
}

static esp_err_t wifi_station_start(void)
{
    wifi_load_stored_credentials();

    wifi_config_t wifi_config = {
        .sta = {
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .threshold.rssi = -127,
            .pmf_cfg = {
                .capable = false,
                .required = false
            },
            .disable_wpa3_compatible_mode = 1,
            .failure_retry_cnt = 3,
        },
    };
    strncpy((char *)wifi_config.sta.ssid, s_wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, s_wifi_pass, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Tối ưu hóa băng thông HT20, tắt Modem Sleep & hạ TX Power 12dBm cho ESP32-C3 SuperMini */
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(48); // 12 dBm

    ESP_LOGI(TAG, "wifi_station_start hoàn tất. Đang chờ đồng bộ kết nối tới SSID '%s'...", s_wifi_ssid);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Đã sẵn sàng mạng LAN cho Local Control Server!");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "==> [Wi-Fi] Không thể kết nối tới AP SSID: '%s' sau %d lần thử lại!",
                 s_wifi_ssid, CONFIG_LOCAL_CTRL_MAXIMUM_RETRY);
        return ESP_FAIL;
    }
}

static void wifi_apply_new_credentials(const char *ssid, const char *pass)
{
    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "SSID không hợp lệ!");
        return;
    }

    ESP_LOGI(TAG, "==> [Wi-Fi Đổi Cấu Hình] Lưu thông tin mới vào NVS: SSID='%s'...", ssid);
    strncpy(s_wifi_ssid, ssid, sizeof(s_wifi_ssid) - 1);
    s_wifi_ssid[sizeof(s_wifi_ssid) - 1] = '\0';
    app_storage_set("wifi_ssid", s_wifi_ssid, strlen(s_wifi_ssid) + 1);

    if (pass) {
        strncpy(s_wifi_pass, pass, sizeof(s_wifi_pass) - 1);
        s_wifi_pass[sizeof(s_wifi_pass) - 1] = '\0';
        app_storage_set("wifi_pass", s_wifi_pass, strlen(s_wifi_pass) + 1);
    } else {
        s_wifi_pass[0] = '\0';
        app_storage_set("wifi_pass", "", 1);
    }

    /* Đặt lại bộ đếm retry và cờ sự kiện */
    s_retry_num = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    wifi_config_t new_cfg = {0};
    strncpy((char *)new_cfg.sta.ssid, s_wifi_ssid, sizeof(new_cfg.sta.ssid) - 1);
    strncpy((char *)new_cfg.sta.password, s_wifi_pass, sizeof(new_cfg.sta.password) - 1);
    new_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    new_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    new_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    new_cfg.sta.threshold.rssi = -127;

    ESP_LOGI(TAG, "==> [Wi-Fi Đổi Cấu Hình] Ngắt kết nối cũ và kết nối AP mới...");
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &new_cfg);
    esp_wifi_connect();
}

/* =========================================================================
 * 2. LOCAL CONTROL SERVER QUA WI-FI + HTTPS + MDNS (MỤC 8.5.1 & 8.5.2)
 * ========================================================================= */

#define PROPERTY_NAME_STATUS "status"
static char s_light_status_json[256] = "{\"status\": true}";

enum property_types {
    PROP_TYPE_TIMESTAMP = 0,
    PROP_TYPE_INT32,
    PROP_TYPE_BOOLEAN,
    PROP_TYPE_STRING,
};

static void build_light_status_json(void)
{
    bool state = app_driver_get_state();
    uint8_t brightness = app_driver_get_brightness();
    uint8_t color_idx = app_driver_get_color_index();
    const char *color_name = app_driver_get_color_name();
    uint8_t r = 0, g = 0, b = 0;
    app_driver_get_rgb(&r, &g, &b);

    snprintf(s_light_status_json, sizeof(s_light_status_json),
             "{\"status\": %s, \"brightness\": %u, \"color\": \"%s\", \"color_idx\": %u, \"rgb\": [%u, %u, %u]}",
             state ? "true" : "false",
             (unsigned int)brightness,
             color_name,
             (unsigned int)(color_idx + 1),
             (unsigned int)r, (unsigned int)g, (unsigned int)b);
}

static esp_err_t get_property_values(size_t props_count,
                                     const esp_local_ctrl_prop_t props[],
                                     esp_local_ctrl_prop_val_t prop_values[],
                                     void *usr_ctx)
{
    for (size_t i = 0; i < props_count; i++) {
        ESP_LOGI(TAG, "==> [HTTPS Local Ctrl] Yêu cầu đọc thuộc tính [%d/%d]: '%s'",
                 (int)(i + 1), (int)props_count, props[i].name);
        if (strncmp(PROPERTY_NAME_STATUS, props[i].name, strlen(PROPERTY_NAME_STATUS)) == 0) {
            build_light_status_json();
            prop_values[i].size = strlen(s_light_status_json);
            prop_values[i].data = s_light_status_json;
            prop_values[i].free_fn = NULL;
            ESP_LOGI(TAG, "    Giá trị phản hồi: %s", s_light_status_json);
        } else {
            ESP_LOGW(TAG, "    Không tìm thấy thuộc tính '%s'!", props[i].name);
            prop_values[i].size = 0;
            prop_values[i].data = NULL;
            prop_values[i].free_fn = NULL;
        }
    }
    return ESP_OK;
}

static esp_err_t set_property_values(size_t props_count,
                                     const esp_local_ctrl_prop_t props[],
                                     const esp_local_ctrl_prop_val_t prop_values[],
                                     void *usr_ctx)
{
    for (size_t i = 0; i < props_count; i++) {
        ESP_LOGI(TAG, "==> [HTTPS Local Ctrl] Yêu cầu ghi thuộc tính [%d/%d]: '%s' (Kích thước: %d bytes)",
                 (int)(i + 1), (int)props_count, props[i].name, (int)prop_values[i].size);
        if (strncmp(PROPERTY_NAME_STATUS, props[i].name, strlen(PROPERTY_NAME_STATUS)) == 0) {
            char val_buf[128] = {0};
            size_t copy_len = prop_values[i].size < sizeof(val_buf) - 1 ? prop_values[i].size : sizeof(val_buf) - 1;
            if (copy_len > 0 && prop_values[i].data != NULL) {
                memcpy(val_buf, prop_values[i].data, copy_len);
            }
            val_buf[copy_len] = '\0';

            // Loại bỏ khoảng trắng và ký tự xuống dòng dư thừa ở cuối chuỗi
            while (copy_len > 0 && (val_buf[copy_len - 1] == '\r' || val_buf[copy_len - 1] == '\n' || val_buf[copy_len - 1] == ' ')) {
                val_buf[--copy_len] = '\0';
            }
            char *cmd = val_buf;
            while (*cmd == ' ') {
                cmd++;
            }

            ESP_LOGI(TAG, "    Lệnh nhận được từ Client: '%s' (Byte 0: 0x%02X)",
                     cmd, (uint8_t)cmd[0]);

            if (strcmp(cmd, "11") == 0 ||
                strstr(cmd, "\"action\": \"11\"") != NULL ||
                strstr(cmd, "\"action\":\"11\"") != NULL ||
                strstr(cmd, "\"action\": \"color\"") != NULL ||
                strstr(cmd, "\"action\":\"color\"") != NULL ||
                strcasecmp(cmd, "color") == 0 ||
                strcasecmp(cmd, "next") == 0) {
                /* Lệnh 11: Đổi màu đèn tiếp theo (Tương đương double click nút Boot) */
                app_driver_next_color();
                ESP_LOGI(TAG, "==> [HTTPS Local Ctrl] Nhận lệnh '11': Đổi màu sắc tiếp theo -> %s",
                         app_driver_get_color_name());
            } else if (strcmp(cmd, "+") == 0 ||
                       strcasecmp(cmd, "up") == 0 ||
                       strstr(cmd, "\"action\": \"up\"") != NULL ||
                       strstr(cmd, "\"action\":\"up\"") != NULL ||
                       strstr(cmd, "\"action\": \"+\"") != NULL) {
                /* Phím Mũi tên Lên: Tăng độ sáng (+20%) */
                app_driver_adjust_brightness(+20);
                ESP_LOGI(TAG, "==> [HTTPS Local Ctrl] Nhận phím [↑]: TĂNG độ sáng (+20%%) -> %d%%",
                         app_driver_get_brightness());
            } else if (strcmp(cmd, "-") == 0 ||
                       strcasecmp(cmd, "down") == 0 ||
                       strstr(cmd, "\"action\": \"down\"") != NULL ||
                       strstr(cmd, "\"action\":\"down\"") != NULL ||
                       strstr(cmd, "\"action\": \"-\"") != NULL) {
                /* Phím Mũi tên Xuống: Giảm độ sáng (-20%) */
                app_driver_adjust_brightness(-20);
                ESP_LOGI(TAG, "==> [HTTPS Local Ctrl] Nhận phím [↓]: GIẢM độ sáng (-20%%) -> %d%%",
                         app_driver_get_brightness());
            } else if (strcmp(cmd, "1") == 0 ||
                       (copy_len == 1 && ((uint8_t)cmd[0] == 0x01 || cmd[0] == '1')) ||
                       strcasecmp(cmd, "on") == 0 ||
                       strcasecmp(cmd, "true") == 0 ||
                       strstr(cmd, "\"status\": true") != NULL ||
                       strstr(cmd, "\"status\":true") != NULL) {
                /* Phím 1: BẬT đèn */
                app_driver_set_state(true);
                ESP_LOGI(TAG, "==> [HTTPS Local Ctrl] Nhận lệnh '1': Cập nhật đèn BẬT (ON)");
            } else if (strcmp(cmd, "0") == 0 ||
                       (copy_len == 1 && ((uint8_t)cmd[0] == 0x00 || cmd[0] == '0')) ||
                       strcasecmp(cmd, "off") == 0 ||
                       strcasecmp(cmd, "false") == 0 ||
                       strstr(cmd, "\"status\": false") != NULL ||
                       strstr(cmd, "\"status\":false") != NULL) {
                /* Phím 0: TẮT đèn */
                app_driver_set_state(false);
                ESP_LOGI(TAG, "==> [HTTPS Local Ctrl] Nhận lệnh '0': Cập nhật đèn TẮT (OFF)");
            } else {
                char *b_pos = strstr(cmd, "\"brightness\":");
                if (b_pos) {
                    int b_val = atoi(b_pos + 13);
                    app_driver_set_brightness((uint8_t)b_val);
                    ESP_LOGI(TAG, "==> [HTTPS Local Ctrl] Cập nhật độ sáng trực tiếp: %d%%", b_val);
                } else {
                    ESP_LOGW(TAG, "Không nhận diện được lệnh! cmd='%s'", cmd);
                    return ESP_ERR_INVALID_ARG;
                }
            }

            build_light_status_json();
            ESP_LOGI(TAG, "==> [HTTPS Local Ctrl] Trạng thái cập nhật: %s", s_light_status_json);
        }
    }
    return ESP_OK;
}

static void esp_local_ctrl_service_start(void)
{
    static bool s_local_ctrl_started = false;
    if (s_local_ctrl_started) {
        return;
    }
    s_local_ctrl_started = true;

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  Khởi động Local Control HTTPS Server & mDNS Discovery  ");
    ESP_LOGI(TAG, "==========================================================");

    /* 1. Khởi tạo HTTPS Server SSL Config và nạp chứng chỉ */
    httpd_ssl_config_t https_conf = HTTPD_SSL_CONFIG_DEFAULT();

    extern const unsigned char cacert_pem_start[] asm("_binary_cacert_pem_start");
    extern const unsigned char cacert_pem_end[]   asm("_binary_cacert_pem_end");
    https_conf.servercert = cacert_pem_start;
    https_conf.servercert_len = cacert_pem_end - cacert_pem_start;

    extern const unsigned char prvtkey_pem_start[] asm("_binary_prvtkey_pem_start");
    extern const unsigned char prvtkey_pem_end[]   asm("_binary_prvtkey_pem_end");
    https_conf.prvtkey_pem = prvtkey_pem_start;
    https_conf.prvtkey_len = prvtkey_pem_end - prvtkey_pem_start;

    ESP_LOGI(TAG, "Đã nạp chứng chỉ SSL Server (%d bytes) và Private Key (%d bytes)",
             (int)https_conf.servercert_len, (int)https_conf.prvtkey_len);

    /* 2. Cấu hình Dịch vụ esp_local_ctrl */
    esp_local_ctrl_config_t config = {
        .transport = ESP_LOCAL_CTRL_TRANSPORT_HTTPD,
        .transport_config = {
            .httpd = &https_conf
        },
#ifndef PROTOCOM_SEC0
#ifdef ESP_LOCAL_CTRL_PROTO_SEC0
#define PROTOCOM_SEC0 ESP_LOCAL_CTRL_PROTO_SEC0
#endif
#endif
        .proto_sec = {
            .version = PROTOCOM_SEC0,
            .custom_handle = NULL,
        },
        .handlers = {
            .get_prop_values = get_property_values,
            .set_prop_values = set_property_values,
            .usr_ctx         = NULL,
            .usr_ctx_free_fn = NULL
        },
        .max_properties = 10
    };

    /* 3. Khởi tạo mDNS Service Discovery */
    ESP_LOGI(TAG, "Khởi tạo mDNS hostname: %s.local", CONFIG_LOCAL_CTRL_MDNS_HOST_NAME);
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(CONFIG_LOCAL_CTRL_MDNS_HOST_NAME));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32 Smart Light Local Control"));
    ESP_ERROR_CHECK(mdns_service_add("ESP-Local-Control", "_esp_local_ctrl", "_tcp", 443, NULL, 0));

    /* 4. Khởi động dịch vụ điều khiển cục bộ */
    ESP_ERROR_CHECK(esp_local_ctrl_start(&config));
    ESP_LOGI(TAG, "esp_local_ctrl service đã chạy trên HTTPS port 443!");

    /* 5. Đăng ký thuộc tính điều khiển đèn 'status' */
    esp_local_ctrl_prop_t status = {
        .name        = PROPERTY_NAME_STATUS,
        .type        = PROP_TYPE_STRING,
        .size        = 0,
        .flags       = 0,
        .ctx         = NULL,
        .ctx_free_fn = NULL
    };
    ESP_ERROR_CHECK(esp_local_ctrl_add_property(&status));
    ESP_LOGI(TAG, "Đã đăng ký thuộc tính điều khiển cục bộ: '%s' (JSON format)", PROPERTY_NAME_STATUS);
    ESP_LOGI(TAG, "Đường dẫn Endpoint nội bộ: https://%s.local/esp_local_ctrl/control", CONFIG_LOCAL_CTRL_MDNS_HOST_NAME);
    ESP_LOGI(TAG, "==========================================================");
}

/* =========================================================================
 * 3. FALLBACK LOCAL CONTROL SERVER QUA BLUETOOTH LE (MỤC 8.5.3)
 * (Kế thừa hoàn chỉnh từ test_case/gatt_server)
 * ========================================================================= */

#define BLE_TAG "ble_local_ctrl"

#if CONFIG_IDF_TARGET_ESP32S3
#define BLE_DEVICE_NAME             "ESP32S3-LIGHT"
#else
#define BLE_DEVICE_NAME             "ESP32C3-LIGHT"
#endif

/* Bảng dịch vụ GATT theo đúng chuẩn test_case/gatt_server */
#define GATTS_SERVICE_UUID_READ_STATUS    0x00FF
#define GATTS_CHAR_UUID_READ_STATUS       0xFF01
#define GATTS_NUM_HANDLE_READ             4

#define GATTS_SERVICE_UUID_WRITE_STATUS   0x00EE
#define GATTS_CHAR_UUID_WRITE_STATUS      0xEE01
#define GATTS_NUM_HANDLE_WRITE            4

#define PROFILE_NUM                       2
#define PROFILE_A_APP_ID                  0  /* Profile A: Đọc trạng thái (UUID 0x00FF / 0xFF01) */
#define PROFILE_B_APP_ID                  1  /* Profile B: Ghi lệnh điều khiển (UUID 0x00EE / 0xEE01) */

struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
static void gatts_profile_b_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

static struct gatts_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gatts_cb = gatts_profile_a_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,
    },
    [PROFILE_B_APP_ID] = {
        .gatts_cb = gatts_profile_b_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,
    },
};

static esp_bd_addr_t s_remote_bda = {0};
static bool s_has_remote_bda = false;
static bool s_ble_active = false;

static uint8_t s_adv_config_done = 0;
#define ADV_CONFIG_FLAG      (1 << 0)
#define SCAN_RSP_CONFIG_FLAG (1 << 1)

/* 128-bit Service UUID của Service 0x00EE (Write Control):
 * UUID 16-bit: 0x00EE -> Base UUID 128-bit chuẩn Bluetooth:
 * 000000ee-0000-1000-8000-00805f9b34fb (Little Endian)
 */
static uint8_t s_adv_service_uuid128[16] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0xEE, 0x00, 0x00, 0x00,
};

static esp_ble_adv_data_t s_adv_data = {
    .set_scan_rsp        = false,
    .include_name        = true,
    .include_txpower     = false,
    .min_interval        = 0x0020, /* 20ms */
    .max_interval        = 0x0040, /* 40ms */
    .appearance          = 0x00,
    .manufacturer_len    = 0,
    .p_manufacturer_data = NULL,
    .service_data_len    = 0,
    .p_service_data      = NULL,
    .service_uuid_len    = 0,
    .p_service_uuid      = NULL,
    .flag                = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_data_t s_scan_rsp_data = {
    .set_scan_rsp        = true,
    .include_name        = false,
    .include_txpower     = true,
    .appearance          = 0x00,
    .manufacturer_len    = 0,
    .p_manufacturer_data = NULL,
    .service_data_len    = 0,
    .p_service_data      = NULL,
    .service_uuid_len    = sizeof(s_adv_service_uuid128),
    .p_service_uuid      = s_adv_service_uuid128,
    .flag                = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t s_ble_adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void ble_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        s_adv_config_done &= (~ADV_CONFIG_FLAG);
        if (s_adv_config_done == 0) {
            esp_ble_gap_start_advertising(&s_ble_adv_params);
        }
        break;

    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        s_adv_config_done &= (~SCAN_RSP_CONFIG_FLAG);
        if (s_adv_config_done == 0) {
            esp_ble_gap_start_advertising(&s_ble_adv_params);
        }
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(BLE_TAG, "Khởi động phát sóng quảng bá BLE thất bại!");
        } else {
            ESP_LOGI(BLE_TAG, "==> [BLE GAP] Đang phát quảng bá BLE: Tên '%s' (Chờ smartphone kết nối...)", BLE_DEVICE_NAME);
        }
        break;

    default:
        break;
    }
}

/* PROFILE A: DỊCH VỤ ĐỌC TRẠNG THÁI ĐÈN (SERVICE 0x00FF / CHAR 0xFF01) */
static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT: {
        ESP_LOGI(BLE_TAG, "[Profile A] Đăng ký App ID %d (Service 0x%04X Đọc trạng thái)",
                 param->reg.app_id, GATTS_SERVICE_UUID_READ_STATUS);

        gl_profile_tab[PROFILE_A_APP_ID].service_id.is_primary = true;
        gl_profile_tab[PROFILE_A_APP_ID].service_id.id.inst_id = 0x00;
        gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID_READ_STATUS;

        ESP_ERROR_CHECK(esp_ble_gap_set_device_name(BLE_DEVICE_NAME));
        ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&s_adv_data));
        s_adv_config_done |= ADV_CONFIG_FLAG;

        ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&s_scan_rsp_data));
        s_adv_config_done |= SCAN_RSP_CONFIG_FLAG;

        esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_A_APP_ID].service_id, GATTS_NUM_HANDLE_READ);
        break;
    }

    case ESP_GATTS_CREATE_EVT: {
        ESP_LOGI(BLE_TAG, "[Profile A] Dịch vụ 0x%04X đã tạo (Handle: %d)",
                 GATTS_SERVICE_UUID_READ_STATUS, param->create.service_handle);
        gl_profile_tab[PROFILE_A_APP_ID].service_handle = param->create.service_handle;
        gl_profile_tab[PROFILE_A_APP_ID].char_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_A_APP_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID_READ_STATUS;

        esp_ble_gatts_start_service(gl_profile_tab[PROFILE_A_APP_ID].service_handle);

        esp_gatt_char_prop_t prop = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
        esp_ble_gatts_add_char(gl_profile_tab[PROFILE_A_APP_ID].service_handle,
                               &gl_profile_tab[PROFILE_A_APP_ID].char_uuid,
                               ESP_GATT_PERM_READ,
                               prop,
                               NULL, NULL);
        break;
    }

    case ESP_GATTS_ADD_CHAR_EVT: {
        ESP_LOGI(BLE_TAG, "[Profile A] Thêm Characteristic 0x%04X thành công (Handle: %d)",
                 GATTS_CHAR_UUID_READ_STATUS, param->add_char.attr_handle);
        gl_profile_tab[PROFILE_A_APP_ID].char_handle = param->add_char.attr_handle;
        gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
        esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_A_APP_ID].service_handle,
                                     &gl_profile_tab[PROFILE_A_APP_ID].descr_uuid,
                                     ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                     NULL, NULL);
        break;
    }

    case ESP_GATTS_READ_EVT: {
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
        rsp.attr_value.handle = param->read.handle;
        rsp.attr_value.len = 1;
        rsp.attr_value.value[0] = app_driver_get_state() ? 0x01 : 0x00;
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_OK, &rsp);
        ESP_LOGI(BLE_TAG, "==> [BLE Read 0xFF01] Trả về trạng thái đèn: 0x%02X (%s)",
                 rsp.attr_value.value[0], rsp.attr_value.value[0] ? "BẬT" : "TẮT");
        break;
    }

    case ESP_GATTS_CONNECT_EVT: {
        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        memcpy(s_remote_bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        s_has_remote_bda = true;
        conn_params.latency = 0;
        conn_params.max_int = 0x20;
        conn_params.min_int = 0x10;
        conn_params.timeout = 400;
        ESP_LOGI(BLE_TAG, "==> [BLE Connect] Smartphone kết nối thành công: %02x:%02x:%02x:%02x:%02x:%02x",
                 param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
                 param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
        gl_profile_tab[PROFILE_A_APP_ID].conn_id = param->connect.conn_id;
        esp_ble_gap_update_conn_params(&conn_params);
        break;
    }

    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(BLE_TAG, "==> [BLE Disconnect] Smartphone đã ngắt kết nối.");
        s_has_remote_bda = false;
        if (s_ble_active) {
            ESP_LOGI(BLE_TAG, "==> [BLE Adv] Tiếp tục phát quảng bá chờ kết nối lại...");
            esp_ble_gap_start_advertising(&s_ble_adv_params);
        } else {
            ESP_LOGI(BLE_TAG, "==> [BLE Adv] Wi-Fi đã bật, KHÔNG phát quảng bá lại!");
        }
        break;

    default:
        break;
    }
}

/* PROFILE B: DỊCH VỤ GHI LỆNH ĐIỀU KHIỂN & CẤU HÌNH WI-FI (SERVICE 0x00EE / CHAR 0xEE01) */
static void gatts_profile_b_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT: {
        ESP_LOGI(BLE_TAG, "[Profile B] Đăng ký App ID %d (Service 0x%04X Ghi lệnh)",
                 param->reg.app_id, GATTS_SERVICE_UUID_WRITE_STATUS);

        gl_profile_tab[PROFILE_B_APP_ID].service_id.is_primary = true;
        gl_profile_tab[PROFILE_B_APP_ID].service_id.id.inst_id = 0x00;
        gl_profile_tab[PROFILE_B_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_B_APP_ID].service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID_WRITE_STATUS;

        esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_B_APP_ID].service_id, GATTS_NUM_HANDLE_WRITE);
        break;
    }

    case ESP_GATTS_CREATE_EVT: {
        ESP_LOGI(BLE_TAG, "[Profile B] Dịch vụ 0x%04X đã tạo (Handle: %d)",
                 GATTS_SERVICE_UUID_WRITE_STATUS, param->create.service_handle);
        gl_profile_tab[PROFILE_B_APP_ID].service_handle = param->create.service_handle;
        gl_profile_tab[PROFILE_B_APP_ID].char_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_B_APP_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID_WRITE_STATUS;

        esp_ble_gatts_start_service(gl_profile_tab[PROFILE_B_APP_ID].service_handle);

        esp_gatt_char_prop_t prop = ESP_GATT_CHAR_PROP_BIT_WRITE |
                                    ESP_GATT_CHAR_PROP_BIT_WRITE_NR |
                                    ESP_GATT_CHAR_PROP_BIT_READ;
        esp_ble_gatts_add_char(gl_profile_tab[PROFILE_B_APP_ID].service_handle,
                               &gl_profile_tab[PROFILE_B_APP_ID].char_uuid,
                               ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                               prop,
                               NULL, NULL);
        break;
    }

    case ESP_GATTS_ADD_CHAR_EVT: {
        ESP_LOGI(BLE_TAG, "[Profile B] Thêm Characteristic 0x%04X thành công (Handle: %d)",
                 GATTS_CHAR_UUID_WRITE_STATUS, param->add_char.attr_handle);
        gl_profile_tab[PROFILE_B_APP_ID].char_handle = param->add_char.attr_handle;
        gl_profile_tab[PROFILE_B_APP_ID].descr_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_B_APP_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
        esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_B_APP_ID].service_handle,
                                     &gl_profile_tab[PROFILE_B_APP_ID].descr_uuid,
                                     ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                     NULL, NULL);
        break;
    }

    case ESP_GATTS_READ_EVT: {
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
        rsp.attr_value.handle = param->read.handle;
        rsp.attr_value.len = 1;
        rsp.attr_value.value[0] = app_driver_get_state() ? 0x01 : 0x00;
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_OK, &rsp);
        break;
    }

    case ESP_GATTS_WRITE_EVT: {
        ESP_LOGI(BLE_TAG, "==> [BLE GATTS Write 0xEE01] Len: %d", param->write.len);
        if (param->write.len > 0) {
            /* Kiểm tra cú pháp đổi Wi-Fi qua BLE:
             * Định dạng 1: "wifi:SSID,PASSWORD" hoặc "SSID,PASSWORD"
             */
            char write_str[128] = {0};
            size_t copy_sz = param->write.len < sizeof(write_str) - 1 ? param->write.len : sizeof(write_str) - 1;
            memcpy(write_str, param->write.value, copy_sz);
            write_str[copy_sz] = '\0';

            char *wifi_prefix = strstr(write_str, "wifi:");
            char *comma_pos = strchr(write_str, ',');

            if (wifi_prefix != NULL && (comma_pos = strchr(wifi_prefix + 5, ',')) != NULL) {
                *comma_pos = '\0';
                const char *new_ssid = wifi_prefix + 5;
                const char *new_pass = comma_pos + 1;
                ESP_LOGI(BLE_TAG, "==> [BLE Wi-Fi Config] Nhận cấu hình Wi-Fi mới qua BLE: SSID='%s'", new_ssid);
                wifi_apply_new_credentials(new_ssid, new_pass);
            } else if (comma_pos != NULL && (comma_pos - write_str > 1)) {
                /* Định dạng SSID,PASS không tiền tố */
                *comma_pos = '\0';
                const char *new_ssid = write_str;
                const char *new_pass = comma_pos + 1;
                ESP_LOGI(BLE_TAG, "==> [BLE Wi-Fi Config] Nhận cấu hình Wi-Fi mới qua BLE: SSID='%s'", new_ssid);
                wifi_apply_new_credentials(new_ssid, new_pass);
            } else {
                if (!s_ble_active) {
                    ESP_LOGW(BLE_TAG, "==> [BLE Write] Bị từ chối: Đã có Wi-Fi, BLE đã bị tắt!");
                    if (param->write.need_rsp) {
                        esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_READ_NOT_PERMIT, NULL);
                    }
                    break;
                }
                /* Điều khiển đèn chuẩn: 
                 * - '11' / 0x11 / 0x0B / 'color': Đổi màu đèn tiếp theo
                 * - '0' / 0x00 / 'off': Tắt đèn
                 * - '1' / 0x01 / 'on': Bật đèn
                 * - '+': Tăng sáng (+20%)
                 * - '-': Giảm sáng (-20%)
                 */
                uint8_t val = param->write.value[0];
                bool is_color_cmd = false;

                if (param->write.len >= 2 && param->write.value[0] == '1' && param->write.value[1] == '1') {
                    is_color_cmd = true;
                } else if (param->write.len == 1 && (val == 11 || val == 0x11 || val == 0x0B)) {
                    is_color_cmd = true;
                } else if (strcasecmp(write_str, "color") == 0 || strcasecmp(write_str, "next") == 0) {
                    is_color_cmd = true;
                }

                if (is_color_cmd) {
                    app_driver_next_color();
                    ESP_LOGI(BLE_TAG, "==> [BLE Write] Đổi màu đèn tiếp theo -> %s", app_driver_get_color_name());
                } else if (param->write.len == 1 && (val == 0x00 || val == '0')) {
                    app_driver_set_state(false);
                    ESP_LOGI(BLE_TAG, "==> [BLE Write] Tắt đèn (0x00)");
                } else if (strcasecmp(write_str, "off") == 0) {
                    app_driver_set_state(false);
                    ESP_LOGI(BLE_TAG, "==> [BLE Write] Tắt đèn ('off')");
                } else if (param->write.len == 1 && (val == 0x01 || val == '1')) {
                    app_driver_set_state(true);
                    ESP_LOGI(BLE_TAG, "==> [BLE Write] Bật đèn (0x01)");
                } else if (strcasecmp(write_str, "on") == 0) {
                    app_driver_set_state(true);
                    ESP_LOGI(BLE_TAG, "==> [BLE Write] Bật đèn ('on')");
                } else if (val == '+') {
                    app_driver_adjust_brightness(+20);
                    ESP_LOGI(BLE_TAG, "==> [BLE Write] Tăng độ sáng (+20%%) -> %d%%", app_driver_get_brightness());
                } else if (val == '-') {
                    app_driver_adjust_brightness(-20);
                    ESP_LOGI(BLE_TAG, "==> [BLE Write] Giảm độ sáng (-20%%) -> %d%%", app_driver_get_brightness());
                } else {
                    ESP_LOGW(BLE_TAG, "==> [BLE Write] Lệnh không xác định: 0x%02X (str='%s', len=%d)", 
                             val, write_str, (int)param->write.len);
                }
                build_light_status_json();
            }
        }
        if (param->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id,
                                        ESP_GATT_OK, NULL);
        }
        break;
    }

    case ESP_GATTS_CONNECT_EVT:
        gl_profile_tab[PROFILE_B_APP_ID].conn_id = param->connect.conn_id;
        break;

    default:
        break;
    }
}

static void gatt_cb_router(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gatts_if = gatts_if;
        } else {
            ESP_LOGE(BLE_TAG, "Đăng ký App ID %04x thất bại, status %d", param->reg.app_id, param->reg.status);
            return;
        }
    }

    for (int idx = 0; idx < PROFILE_NUM; idx++) {
        if (gatts_if == ESP_GATT_IF_NONE || gatts_if == gl_profile_tab[idx].gatts_if) {
            if (gl_profile_tab[idx].gatts_cb) {
                gl_profile_tab[idx].gatts_cb(event, gatts_if, param);
            }
        }
    }
}

static void ble_local_ctrl_stop(void)
{
    if (!s_ble_active) {
        return;
    }
    ESP_LOGI(BLE_TAG, "==========================================================");
    ESP_LOGI(BLE_TAG, "  ĐÃ CÓ WI-FI! TIẾN HÀNH TẮT FALLBACK BLUETOOTH LE...     ");
    ESP_LOGI(BLE_TAG, "==========================================================");

    /* Đặt cờ s_ble_active = false trước để ngăn mọi thao tác kết nối lại */
    s_ble_active = false;

    /* Dừng phát sóng quảng bá BLE */
    esp_ble_gap_stop_advertising();

    /* Ngắt kết nối vật lý (GAP Link Disconnect) với Smartphone */
    if (s_has_remote_bda) {
        ESP_LOGI(BLE_TAG, "==> [BLE] Chủ động ngắt kết nối vật lý với Smartphone: %02x:%02x:%02x:%02x:%02x:%02x",
                 s_remote_bda[0], s_remote_bda[1], s_remote_bda[2],
                 s_remote_bda[3], s_remote_bda[4], s_remote_bda[5]);
        esp_ble_gap_disconnect(s_remote_bda);
        s_has_remote_bda = false;
    }

    /* Đóng kết nối GATT phía server */
    if (gl_profile_tab[PROFILE_A_APP_ID].conn_id != 0 && gl_profile_tab[PROFILE_A_APP_ID].gatts_if != ESP_GATT_IF_NONE) {
        esp_ble_gatts_close(gl_profile_tab[PROFILE_A_APP_ID].gatts_if, gl_profile_tab[PROFILE_A_APP_ID].conn_id);
    }
    if (gl_profile_tab[PROFILE_B_APP_ID].conn_id != 0 && gl_profile_tab[PROFILE_B_APP_ID].gatts_if != ESP_GATT_IF_NONE) {
        esp_ble_gatts_close(gl_profile_tab[PROFILE_B_APP_ID].gatts_if, gl_profile_tab[PROFILE_B_APP_ID].conn_id);
    }

    ESP_LOGI(BLE_TAG, "==> [BLE] Đã đóng toàn bộ kết nối và dừng phát quảng bá! Chuyển 100%% sang Wi-Fi LAN.");
}

static void ble_local_ctrl_init(void)
{
    if (s_ble_active) {
        return;
    }

    ESP_LOGI(BLE_TAG, "==========================================================");
    ESP_LOGI(BLE_TAG, "  Khởi tạo Fallback BLE GATT Local Control (Mục 8.5.3)    ");
    ESP_LOGI(BLE_TAG, "  - Tên thiết bị: %s                             ", BLE_DEVICE_NAME);
    ESP_LOGI(BLE_TAG, "  - Service Đọc (Read) : UUID 0x00FF (Char 0xFF01)       ");
    ESP_LOGI(BLE_TAG, "  - Service Ghi (Write): UUID 0x00EE (Char 0xEE01)       ");
    ESP_LOGI(BLE_TAG, "==========================================================");

    static bool s_bt_stack_inited = false;
    if (!s_bt_stack_inited) {
#if CONFIG_IDF_TARGET_ESP32
        ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
#endif
        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
        ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

        ESP_ERROR_CHECK(esp_bluedroid_init());
        ESP_ERROR_CHECK(esp_bluedroid_enable());

        ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatt_cb_router));
        ESP_ERROR_CHECK(esp_ble_gap_register_callback(ble_gap_event_handler));
        ESP_ERROR_CHECK(esp_ble_gatts_app_register(PROFILE_A_APP_ID));
        ESP_ERROR_CHECK(esp_ble_gatts_app_register(PROFILE_B_APP_ID));
        esp_ble_gatt_set_local_mtu(500);
        s_bt_stack_inited = true;
    } else {
        /* Bắt đầu phát quảng bá lại nếu stack đã init */
        esp_ble_gap_start_advertising(&s_ble_adv_params);
    }

    s_ble_active = true;
    ESP_LOGI(BLE_TAG, "BLE GATT Server Fallback khởi tạo thành công!");
}

/* FreeRTOS background task: Nhận lệnh cấu hình Wi-Fi từ bàn phím Console UART */
static void console_wifi_task(void *pvParameters)
{
    char line[128];
    while (1) {
        if (fgets(line, sizeof(line), stdin) != NULL) {
            /* Loại bỏ newline */
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
                line[--len] = '\0';
            }
            if (len == 0) continue;

            if (strncmp(line, "wifi ", 5) == 0) {
                char *ssid = line + 5;
                char *pass = strchr(ssid, ' ');
                if (pass) {
                    *pass = '\0';
                    pass++;
                }
                ESP_LOGI(TAG, "==> [Console UART] Nhận lệnh đổi Wi-Fi: SSID='%s' Mật khẩu='%s'", ssid, pass ? pass : "");
                wifi_apply_new_credentials(ssid, pass);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* =========================================================================
 * 4. HÀM MAIN CHÍNH
 * ========================================================================= */

void app_main(void)
{
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("light_driver", ESP_LOG_WARN);

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "   PBL5 Smart Light - Chương 8: Điều Khiển Cục Bộ (8.5)   ");
    ESP_LOGI(TAG, "   Hỗ trợ đa mục tiêu: ESP32-S3 & ESP32-C3                ");
    ESP_LOGI(TAG, "   LED WS2812B GPIO 4 SPI DMA | Boot Button Gestures      ");
    ESP_LOGI(TAG, "==========================================================");

    /* 1. Khởi tạo NVS Flash */
    ESP_LOGI(TAG, "[1/5] Khởi tạo NVS Flash Storage...");
    app_storage_init();

    /* 2. Khởi tạo Driver Đèn WS2812B và Nút bấm Boot */
    ESP_LOGI(TAG, "[2/5] Khởi tạo Hardware Driver (WS2812B & Button HAL)...");
    app_driver_init();

    /* Khởi chạy task UART Console lắng nghe lệnh đổi Wi-Fi: 'wifi <SSID> <PASSWORD>' */
    xTaskCreate(console_wifi_task, "console_wifi", 4096, NULL, 5, NULL);

    /* 3. Khởi tạo và kết nối Wi-Fi Station */
    ESP_LOGI(TAG, "[3/5] Khởi tạo Wi-Fi Station...");
    wifi_initialize();
    esp_err_t wifi_ret = wifi_station_start();

    if (wifi_ret == ESP_OK) {
        /* KỊCH BẢN 1: KẾT NỐI WI-FI THÀNH CÔNG */
        ESP_LOGI(TAG, "[4/5] Khởi động Local Control HTTPS Server & mDNS...");
        esp_local_ctrl_service_start();

        ESP_LOGI(TAG, "==========================================================");
        ESP_LOGI(TAG, "  HỆ THỐNG ĐIỀU KHIỂN CỤC BỘ QUA WI-FI ĐÃ SẴN SÀNG!      ");
        ESP_LOGI(TAG, "  - Kênh Wi-Fi HTTPS : https://%s.local/esp_local_ctrl/control", CONFIG_LOCAL_CTRL_MDNS_HOST_NAME);
        ESP_LOGI(TAG, "  - Trạng thái Wi-Fi : KẾT NỐI THÀNH CÔNG (ĐÃ CÓ IP)      ");
        ESP_LOGI(TAG, "==========================================================");
    } else {
        /* KỊCH BẢN 2: THỬ KẾT NỐI 5 LẦN THẤT BẠI -> KÍCH HOẠT FALLBACK */
        ESP_LOGE(TAG, "==========================================================");
        ESP_LOGE(TAG, "  [LỖI MẠNG] KẾT NỐI WI-FI THẤT BẠI SAU 5 LẦN THỬ!       ");
        ESP_LOGE(TAG, "  Thiết bị kích hoạt chế độ Fallback với 2 lựa chọn:     ");
        ESP_LOGW(TAG, "  --------------------------------------------------------");
        ESP_LOGW(TAG, "  [LỰA CHỌN 1] KẾT NỐI QUA BLUETOOTH LE TRỰC TIẾP:        ");
        ESP_LOGW(TAG, "    - Tên thiết bị BLE : %s                              ", BLE_DEVICE_NAME);
        ESP_LOGW(TAG, "    - Read Status      : Service 0x00FF -> Char 0xFF01    ");
        ESP_LOGW(TAG, "    - Write Control    : Service 0x00EE -> Char 0xEE01    ");
        ESP_LOGW(TAG, "    - App tương thích  : nRF Connect / LightBlue          ");
        ESP_LOGW(TAG, "  [LỰA CHỌN 2] ĐỔI WI-FI MỚI (LƯU VÀO NVS TỰ ĐỘNG):       ");
        ESP_LOGW(TAG, "    - Cách A (Qua BLE) : Ghi 'SSID,PASSWORD' vào 0xEE01   ");
        ESP_LOGW(TAG, "    - Cách B (Qua Serial): Gõ 'wifi <SSID> <PASSWORD>'    ");
        ESP_LOGE(TAG, "==========================================================");

        /* Khởi chạy Bluetooth LE GATT Server fallback */
        ble_local_ctrl_init();
    }

    int count = 0;
    while (1) {
        bool wifi_ok = (s_wifi_event_group && (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT));
        ESP_LOGI(TAG, "[Heartbeat #%02d] Light: %s (%d%%) | Wi-Fi: %s | Free Heap: %lu bytes",
                 ++count,
                 app_driver_get_state() ? "ON" : "OFF",
                 (int)app_driver_get_brightness(),
                 wifi_ok ? "CONNECTED" : "DISCONNECTED (No IP)",
                 (unsigned long)esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
