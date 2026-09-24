/*
 * ESP32 Smart Light Example - Chapter 3: Wi-Fi Connection
 *
 * Demonstrates:
 * 1. Wi-Fi Station mode initialization and connection
 * 2. FreeRTOS Event Groups for connection synchronization (WIFI_CONNECTED_BIT, WIFI_FAIL_BIT)
 * 3. Dynamic visual feedback via WS2812B NeoPixel 8-bit LED strip (Connecting, Connected, Failed)
 * 4. Physical button control (Boot button GPIO 0 / GPIO 9)
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

#include "app_storage.h"
#include "app_priv.h"

#ifndef CONFIG_ESP_WIFI_SSID
#define CONFIG_ESP_WIFI_SSID "Minh Toan"
#endif

#ifndef CONFIG_ESP_WIFI_PASSWORD
#define CONFIG_ESP_WIFI_PASSWORD "14052004"
#endif

#ifndef CONFIG_ESP_WIFI_BSSID
#define CONFIG_ESP_WIFI_BSSID ""
#endif

#ifndef CONFIG_ESP_MAXIMUM_RETRY
#define CONFIG_ESP_MAXIMUM_RETRY 10
#endif

static bool parse_mac_address(const char *mac_str, uint8_t *mac_out)
{
    if (!mac_str || strlen(mac_str) == 0) {
        return false;
    }
    unsigned int val[6];
    if (sscanf(mac_str, "%x:%x:%x:%x:%x:%x",
               &val[0], &val[1], &val[2], &val[3], &val[4], &val[5]) == 6 ||
        sscanf(mac_str, "%x-%x-%x-%x-%x-%x",
               &val[0], &val[1], &val[2], &val[3], &val[4], &val[5]) == 6) {
        for (int i = 0; i < 6; i++) {
            mac_out[i] = (uint8_t)val[i];
        }
        return true;
    }
    return false;
}

/* The event group allows multiple bits for each event:
 * - WIFI_CONNECTED_BIT: Connected to the AP with an IP
 * - WIFI_FAIL_BIT: Failed to connect after maximum retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi_connection";

/* FreeRTOS event group to signal when we are connected */
static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;
static bool s_is_connected = false;
static char s_ip_str[16] = "0.0.0.0";

static const char *wifi_reason_to_str(uint8_t reason)
{
    switch (reason) {
        case WIFI_REASON_UNSPECIFIED:              return "Unspecified (1)";
        case WIFI_REASON_AUTH_EXPIRE:              return "Auth Expired (2) - Router hết hạn/từ chối xác thực (Band Steering)";
        case WIFI_REASON_AUTH_LEAVE:               return "Auth Leave (3)";
        case WIFI_REASON_ASSOC_TOOMANY:            return "Too many stations (5) - AP quá tải";
        case WIFI_REASON_ASSOC_NOT_AUTHED:         return "Assoc not authed (9)";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:   return "4-way Handshake Timeout (15) - Sai mật khẩu";
        case WIFI_REASON_BEACON_TIMEOUT:           return "Beacon Timeout (200) - Mất tín hiệu Beacon AP";
        case WIFI_REASON_NO_AP_FOUND:              return "AP Not Found (201) - Không tìm thấy SSID";
        case WIFI_REASON_AUTH_FAIL:                return "Auth Failed (202) - Sai mật khẩu hoặc chế độ bảo mật";
        case WIFI_REASON_ASSOC_FAIL:               return "Association Failed (203) - Kết hợp AP thất bại (Band Steering)";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:        return "Handshake Timeout (204) - Sai mật khẩu";
        case WIFI_REASON_CONNECTION_FAIL:          return "Connection Failed (205) - Lỗi kết nối AP";
        default:                                   return "Other / Unknown Reason";
    }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi Station stack đã khởi động.");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *) event_data;
        s_is_connected = false;
        ESP_LOGW(TAG, "==> [Wi-Fi Disconnected] Mã lý do: %d - %s",
                 disconn->reason, wifi_reason_to_str(disconn->reason));

        if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
            s_retry_num++;
            ESP_LOGW(TAG, "Nghỉ 1.5s và thử kết nối lại AP (lần %d/%d)...", s_retry_num, CONFIG_ESP_MAXIMUM_RETRY);
            app_driver_set_wifi_status(WIFI_STATUS_CONNECTING);
            vTaskDelay(pdMS_TO_TICKS(1500));
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Kết nối AP thất bại sau %d lần thử!", CONFIG_ESP_MAXIMUM_RETRY);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            app_driver_set_wifi_status(WIFI_STATUS_FAILED);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        esp_ip4addr_ntoa(&event->ip_info.ip, s_ip_str, sizeof(s_ip_str));

        wifi_ap_record_t ap_info = {0};
        char bssid_str[24] = "N/A";
        char ssid_str[33] = CONFIG_ESP_WIFI_SSID;
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
        ESP_LOGI(TAG, "  ĐÃ KẾT NỐI WI-FI THÀNH CÔNG!                           ");
        ESP_LOGI(TAG, "  - Tên Wi-Fi (SSID) : %s", ssid_str);
        ESP_LOGI(TAG, "  - BSSID (MAC AP)   : %s (Kênh %d, Sóng %d dBm)", bssid_str, channel, rssi);
        ESP_LOGI(TAG, "  - Địa chỉ IP cấp   : %s", s_ip_str);
        ESP_LOGI(TAG, "==========================================================");
        s_retry_num = 0;
        s_is_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        app_driver_set_wifi_status(WIFI_STATUS_CONNECTED);
    }
}

static void wifi_initialize(void)
{
    s_wifi_event_group = xEventGroupCreate();

    /* Initialize TCP/IP */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Initialize the event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Initialize Wi-Fi including netif with default config */
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Register our event handler for Wi-Fi and IP related events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
}

static void wifi_station_initialize(void)
{
    /* 1. Country code configuration for Vietnam: channels 1 to 13 */
    wifi_country_t country = {
        .cc = "VN",
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_AUTO,
    };
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));

    /* 2. Configure Station mode */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    uint8_t manual_bssid[6] = {0};
    bool has_manual_bssid = parse_mac_address(CONFIG_ESP_WIFI_BSSID, manual_bssid);

    /* 3. Wi-Fi Station Config tuned for ESP32-C3 SuperMini & Dual-Band networks */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .threshold.rssi = -127,
            .pmf_cfg = {
                .capable = false,
                .required = false,
            },
            .disable_wpa3_compatible_mode = 1,
            .failure_retry_cnt = 3,
        },
    };

    if (has_manual_bssid) {
        wifi_config.sta.bssid_set = true;
        memcpy(wifi_config.sta.bssid, manual_bssid, 6);
        ESP_LOGI(TAG, "--> [BSSID Manual] Đã cấu hình khóa BSSID thủ công: %02X:%02X:%02X:%02X:%02X:%02X",
                 manual_bssid[0], manual_bssid[1], manual_bssid[2],
                 manual_bssid[3], manual_bssid[4], manual_bssid[5]);
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* 4. Enforce HT20 bandwidth (20MHz) for maximum 2.4GHz RF stability and noise rejection */
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20);

    /* 5. Tắt Power Save để radio luôn bật 100% độ nhạy */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    /* 6. Giới hạn TX Power xuống 48 (12.0 dBm) để triệt tiêu sụt áp 3.3V trên ESP32-C3 SuperMini */
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(48));
    int8_t cur_power = 0;
    if (esp_wifi_get_max_tx_power(&cur_power) == ESP_OK) {
        ESP_LOGI(TAG, "Đã thiết lập Wi-Fi TX Power cho ESP32-C3 SuperMini: %d (%.2f dBm)",
                 cur_power, cur_power * 0.25f);
    }

    /* 8. Quét chẩn đoán sóng AP 'Minh Toan' trước khi kết nối */
    ESP_LOGI(TAG, "--> Đang quét chẩn đoán AP '%s' (băng tần 2.4GHz)...", CONFIG_ESP_WIFI_SSID);
    wifi_scan_config_t scan_cfg = {
        .ssid = (uint8_t *)CONFIG_ESP_WIFI_SSID,
        .bssid = has_manual_bssid ? manual_bssid : NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    esp_err_t scan_err = esp_wifi_scan_start(&scan_cfg, true);
    wifi_ap_record_t best_ap = {0};
    bool found_ap = false;

    if (scan_err == ESP_OK) {
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);
        ESP_LOGI(TAG, "--> Số lượng node AP tìm thấy cho SSID '%s': %d", CONFIG_ESP_WIFI_SSID, ap_count);
        if (ap_count > 0) {
            wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
            if (ap_records) {
                esp_wifi_scan_get_ap_records(&ap_count, ap_records);
                int best_idx = 0;
                int8_t max_rssi = -128;

                for (int j = 0; j < ap_count; j++) {
                    const char *auth_desc = "Khác";
                    switch (ap_records[j].authmode) {
                        case WIFI_AUTH_OPEN: auth_desc = "OPEN"; break;
                        case WIFI_AUTH_WEP: auth_desc = "WEP"; break;
                        case WIFI_AUTH_WPA_PSK: auth_desc = "WPA_PSK"; break;
                        case WIFI_AUTH_WPA2_PSK: auth_desc = "WPA2_PSK"; break;
                        case WIFI_AUTH_WPA_WPA2_PSK: auth_desc = "WPA_WPA2_PSK"; break;
                        case WIFI_AUTH_WPA3_PSK: auth_desc = "WPA3_PSK"; break;
                        case WIFI_AUTH_WPA2_WPA3_PSK: auth_desc = "WPA2_WPA3_PSK"; break;
                        default: break;
                    }
                    ESP_LOGI(TAG, "    [AP %d] BSSID: %02x:%02x:%02x:%02x:%02x:%02x | Kênh: %d | Sóng RSSI: %d dBm | Bảo mật: %s (%d)",
                             j + 1,
                             ap_records[j].bssid[0], ap_records[j].bssid[1], ap_records[j].bssid[2],
                             ap_records[j].bssid[3], ap_records[j].bssid[4], ap_records[j].bssid[5],
                             ap_records[j].primary, ap_records[j].rssi, auth_desc, ap_records[j].authmode);

                    if (ap_records[j].rssi > max_rssi) {
                        max_rssi = ap_records[j].rssi;
                        best_idx = j;
                    }
                }
                best_ap = ap_records[best_idx];
                found_ap = true;
                free(ap_records);
            }
        } else {
            ESP_LOGW(TAG, "--> CẢNH BÁO: Không quét thấy bất kỳ sóng 2.4GHz nào có tên '%s'!", CONFIG_ESP_WIFI_SSID);
        }
    } else {
        ESP_LOGW(TAG, "Quét Wi-Fi thất bại: %s", esp_err_to_name(scan_err));
    }

    /* 9. Tự động liên kết BSSID tốt nhất nếu người dùng không gán thủ công */
    if (!has_manual_bssid && found_ap) {
        wifi_config.sta.bssid_set = true;
        memcpy(wifi_config.sta.bssid, best_ap.bssid, 6);
        wifi_config.sta.channel = best_ap.primary;
        ESP_LOGI(TAG, "--> [BSSID Auto-Lock] Tự động khóa BSSID sóng 2.4GHz mạnh nhất: %02X:%02X:%02X:%02X:%02X:%02X (Kênh: %d, RSSI: %d dBm)",
                 best_ap.bssid[0], best_ap.bssid[1], best_ap.bssid[2],
                 best_ap.bssid[3], best_ap.bssid[4], best_ap.bssid[5],
                 best_ap.primary, best_ap.rssi);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    }

    /* 10. Bắt đầu kết nối AP */
    app_driver_set_wifi_status(WIFI_STATUS_CONNECTING);
    if (wifi_config.sta.bssid_set) {
        ESP_LOGI(TAG, "Bắt đầu kết nối Wi-Fi Station tới SSID: %s [BSSID: %02X:%02X:%02X:%02X:%02X:%02X, Kênh: %d]...",
                 CONFIG_ESP_WIFI_SSID,
                 wifi_config.sta.bssid[0], wifi_config.sta.bssid[1], wifi_config.sta.bssid[2],
                 wifi_config.sta.bssid[3], wifi_config.sta.bssid[4], wifi_config.sta.bssid[5],
                 wifi_config.sta.channel);
    } else {
        ESP_LOGI(TAG, "Bắt đầu kết nối Wi-Fi Station tới SSID: %s...", CONFIG_ESP_WIFI_SSID);
    }
    ESP_ERROR_CHECK(esp_wifi_connect());

    /* Waiting until either connection is established (WIFI_CONNECTED_BIT) or failed (WIFI_FAIL_BIT) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Kết nối thành công tới SSID: %s", CONFIG_ESP_WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Không thể kết nối tới SSID: %s", CONFIG_ESP_WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "Sự kiện Wi-Fi bất thường");
    }
}

void app_main(void)
{
    int i = 0;
    /* Tối ưu hóa mức độ log (Observability & Signal-to-Noise Ratio):
     * Ẩn các log debug/thủ tục nội bộ từ Wi-Fi PHY và API driver để làm sạch màn hình terminal */
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("light_driver", ESP_LOG_WARN);

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  PBL5 Smart Light - Chapter 3: Wi-Fi Station Connection  ");
    ESP_LOGI(TAG, "==========================================================");

    /* 1. NVS Flash initialization */
    ESP_LOGI(TAG, "Khởi tạo NVS Flash...");
    app_storage_init();

    /* 2. Application driver initialization (WS2812B & Button) */
    ESP_LOGI(TAG, "Khởi tạo Driver Đèn WS2812B & Nút Bấm...");
    app_driver_init();

    /* 3. Wi-Fi Stack initialization */
    ESP_LOGI(TAG, "Khởi tạo Wi-Fi Stack...");
    wifi_initialize();

    /* 4. Wi-Fi Station Mode connection */
    ESP_LOGI(TAG, "Bắt đầu kết nối Wi-Fi Station Mode...");
    wifi_station_initialize();

    while (1) {
        if (s_is_connected) {
            ESP_LOGI(TAG, "[Heartbeat #%02d] ONLINE | SSID: %s | IP: %s | Free Heap: %lu bytes",
                     ++i, CONFIG_ESP_WIFI_SSID, s_ip_str, (unsigned long)esp_get_free_heap_size());
        } else {
            ESP_LOGW(TAG, "[Heartbeat #%02d] MẤT KẾT NỐI (Đang thử kết nối lại %s...)", ++i, CONFIG_ESP_WIFI_SSID);
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
