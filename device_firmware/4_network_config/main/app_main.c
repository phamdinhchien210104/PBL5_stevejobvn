/*
 * ESP32 Smart Light Example - Chapter 4: Smart Wi-Fi Provisioning (BLE)
 *
 * Demonstrates:
 * 1. Unified Wi-Fi Provisioning via Bluetooth Low Energy (BLE)
 * 2. Visual QR Code generation on terminal and Fallback URL
 * 3. Dynamic Visual LED feedback on WS2812B NeoPixel 8-bit strip:
 *    - Breathing Cyan: Waiting for smartphone BLE connection
 *    - Breathing Yellow: Credentials received, connecting to AP
 *    - Solid Green: Provisioned & Got IP successfully
 *    - Solid Red: Provisioning failure (wrong password/SSID not found)
 * 4. Automatic BLE stack deinitialization (memory reclamation) upon success
 * 5. Multi-gesture physical button control
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

#if __has_include("network_provisioning/manager.h")
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"
#define wifi_prov_mgr_config_t                      network_prov_mgr_config_t
#define wifi_prov_mgr_init                          network_prov_mgr_init
#define wifi_prov_scheme_ble                        network_prov_scheme_ble
#define WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BLE NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BLE
#define wifi_prov_mgr_is_provisioned                network_prov_mgr_is_wifi_provisioned
#define wifi_prov_security_t                        network_prov_security_t
#define wifi_prov_scheme_ble_set_service_uuid       network_prov_scheme_ble_set_service_uuid
#define wifi_prov_mgr_endpoint_create               network_prov_mgr_endpoint_create
#define wifi_prov_mgr_start_provisioning            network_prov_mgr_start_provisioning
#define wifi_prov_mgr_endpoint_register             network_prov_mgr_endpoint_register
#define wifi_prov_mgr_deinit                        network_prov_mgr_deinit
#define WIFI_PROV_EVENT                             NETWORK_PROV_EVENT
#define WIFI_PROV_START                             NETWORK_PROV_START
#define WIFI_PROV_CRED_RECV                         NETWORK_PROV_WIFI_CRED_RECV
#define WIFI_PROV_CRED_FAIL                         NETWORK_PROV_WIFI_CRED_FAIL
#define WIFI_PROV_CRED_SUCCESS                      NETWORK_PROV_WIFI_CRED_SUCCESS
#define WIFI_PROV_END                               NETWORK_PROV_END
#define wifi_prov_sta_fail_reason_t                 network_prov_wifi_sta_fail_reason_t
#define WIFI_PROV_STA_AUTH_ERROR                    NETWORK_PROV_WIFI_STA_AUTH_ERROR
#define WIFI_PROV_STA_AP_NOT_FOUND                  NETWORK_PROV_WIFI_STA_AP_NOT_FOUND
#define wifi_prov_mgr_reset_sm_state_on_failure     network_prov_mgr_reset_wifi_sm_state_on_failure

#if defined(CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_1)
#define APP_PROV_SEC_MODE                           NETWORK_PROV_SECURITY_1
#define APP_PROV_POP                                "abcd1234"
#elif defined(CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_0)
#define APP_PROV_SEC_MODE                           NETWORK_PROV_SECURITY_0
#define APP_PROV_POP                                NULL
#else
#define APP_PROV_SEC_MODE                           0
#define APP_PROV_POP                                NULL
#endif

#else
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#define APP_PROV_SEC_MODE                           WIFI_PROV_SECURITY_1
#define APP_PROV_POP                                "abcd1234"
#define wifi_prov_mgr_reset_sm_state_on_failure     wifi_prov_mgr_reset_sm_state_on_failure
#endif

#include "qrcode.h"

#include "app_storage.h"
#include "app_priv.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define PROV_QR_VERSION     "v1"
#define PROV_TRANSPORT_BLE  "ble"
#define QRCODE_BASE_URL     "https://espressif.github.io/esp-jumpstart/qrcode.html"

static const char *TAG = "network_config";

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;
static bool s_is_connected = false;
static char s_ip_str[16] = "0.0.0.0";

static const char *wifi_reason_to_str(uint8_t reason)
{
    switch (reason) {
        case WIFI_REASON_UNSPECIFIED:              return "Unspecified (1)";
        case WIFI_REASON_AUTH_EXPIRE:              return "Auth Expired (2) - Router hết hạn/từ chối xác thực (Band Steering/Nguồn yếu)";
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

/**
 * @brief Lấy tên dịch vụ thiết bị BLE (dạng PROV_XXXXXX theo 3 byte cuối MAC)
 */
static void get_device_service_name(char *service_name, size_t max)
{
    uint8_t eth_mac[6];
    const char *ssid_prefix = "PROV_";
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, max, "%s%02X%02X%02X",
             ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}

/**
 * @brief Tạo và in mã QR Code trực quan trên terminal kèm URL dự phòng
 */
static void wifi_prov_print_qr(const char *name, const char *pop, const char *transport)
{
    if (!name || !transport) {
        ESP_LOGW(TAG, "Không thể tạo payload QR code do thiếu tham số.");
        return;
    }
    char payload[150] = {0};
    if (pop) {
        snprintf(payload, sizeof(payload), "{\"ver\":\"%s\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\"}",
                 PROV_QR_VERSION, name, pop, transport);
    } else {
        snprintf(payload, sizeof(payload), "{\"ver\":\"%s\",\"name\":\"%s\",\"transport\":\"%s\"}",
                 PROV_QR_VERSION, name, transport);
    }

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  QUÉT MÃ QR DƯỚI ĐÂY BẰNG ỨNG DỤNG 'ESP BLE Provisioning'");
    ESP_LOGI(TAG, "  Tên thiết bị BLE      : %s", name);
    ESP_LOGI(TAG, "  Mã bảo mật (PoP)      : %s", pop ? pop : "(Không có)");
    ESP_LOGI(TAG, "==========================================================");

    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    esp_qrcode_generate(&cfg, payload);

    ESP_LOGI(TAG, "----------------------------------------------------------");
    ESP_LOGI(TAG, "Nếu mã QR trên terminal bị mờ hoặc khó quét, hãy mở URL:");
    ESP_LOGI(TAG, "%s?data=%s", QRCODE_BASE_URL, payload);
    ESP_LOGI(TAG, "----------------------------------------------------------");
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
        case WIFI_PROV_START:
            ESP_LOGI(TAG, "==> [PROV] Đang phát quảng bá BLE. Chờ smartphone quét mã...");
            app_driver_set_prov_status(PROV_STATUS_WAITING);
            break;

        case WIFI_PROV_CRED_RECV: {
            wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
            ESP_LOGI(TAG, "==========================================================");
            ESP_LOGI(TAG, "==> [PROV] ĐÃ NHẬN THÔNG TIN WI-FI TỪ ĐIỆN THOẠI!");
            ESP_LOGI(TAG, "    Tên Wi-Fi (SSID) : %s", (char *)wifi_sta_cfg->ssid);
            ESP_LOGI(TAG, "    Đang cấu hình tối ưu 2.4GHz & kết nối Access Point...");
            ESP_LOGI(TAG, "==========================================================");

            /* Tinh chỉnh cấu hình Wi-Fi Station nhận được từ app điện thoại để tương thích router 2 băng tần */
            wifi_sta_cfg->scan_method = WIFI_ALL_CHANNEL_SCAN;
            wifi_sta_cfg->sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
            wifi_sta_cfg->threshold.authmode = WIFI_AUTH_WPA2_PSK;
            wifi_sta_cfg->threshold.rssi = -127;
            wifi_sta_cfg->pmf_cfg.capable = false;
            wifi_sta_cfg->pmf_cfg.required = false;
            wifi_sta_cfg->disable_wpa3_compatible_mode = 1;
            wifi_sta_cfg->failure_retry_cnt = 3;

            /* Tắt Power Save & giảm TX Power để ổn định nguồn 3.3V cho ESP32-C3 SuperMini */
            esp_wifi_set_ps(WIFI_PS_NONE);
            esp_wifi_set_max_tx_power(48); // 12 dBm

            app_driver_set_prov_status(PROV_STATUS_CONNECTING);
            break;
        }

        case WIFI_PROV_CRED_FAIL: {
            wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
            ESP_LOGE(TAG, "==========================================================");
            ESP_LOGE(TAG, "==> [PROV] KẾT NỐI WI-FI THẤT BẠI!");
            if (*reason == WIFI_PROV_STA_AUTH_ERROR) {
                ESP_LOGE(TAG, "    Lý do: SAI MẬT KHẨU WI-FI HOẶC LỖI XÁC THỰC ROUTER!");
                ESP_LOGW(TAG, "    Vui lòng kiểm tra và nhập lại đúng mật khẩu trên app điện thoại.");
            } else {
                ESP_LOGE(TAG, "    Lý do: KHÔNG TÌM THẤY ROUTER / AP!");
                ESP_LOGW(TAG, "    Lưu ý: ESP32-C3 chỉ hỗ trợ băng tần Wi-Fi 2.4GHz (không hỗ trợ 5GHz).");
            }
            ESP_LOGE(TAG, "==========================================================");
            app_driver_set_prov_status(PROV_STATUS_FAILED);
            /* Reset state machine để app điện thoại nhận mã lỗi và cho phép nhập lại */
            wifi_prov_mgr_reset_sm_state_on_failure();
            break;
        }

        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "==> [PROV] Xác thực thông tin Wi-Fi thành công! Đang chờ cấp IP...");
            break;

        case WIFI_PROV_END:
            ESP_LOGI(TAG, "==> [PROV] Quy trình cấp phát hoàn tất. Giải phóng bộ nhớ BLE Stack...");
            wifi_prov_mgr_deinit();
            /* Đăng ký WIFI_EVENT để tự động reconnect nếu router bị mất kết nối khi đang chạy */
            esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
            break;

        default:
            break;
        }
    } else if (event_base == PROTOCOMM_TRANSPORT_BLE_EVENT) {
        switch (event_id) {
        case PROTOCOMM_TRANSPORT_BLE_CONNECTED:
            ESP_LOGI(TAG, "==> [BLE] Smartphone đã kết nối Bluetooth LE với Đèn!");
            break;
        case PROTOCOMM_TRANSPORT_BLE_DISCONNECTED:
            ESP_LOGI(TAG, "==> [BLE] Smartphone đã ngắt kết nối Bluetooth LE.");
            break;
        default:
            break;
        }
    } else if (event_base == PROTOCOMM_SECURITY_SESSION_EVENT) {
        switch (event_id) {
        case PROTOCOMM_SECURITY_SESSION_SETUP_OK:
            ESP_LOGI(TAG, "==> [SEC] Bắt tay bảo mật Security 1 (PoP) THÀNH CÔNG!");
            break;
        case PROTOCOMM_SECURITY_SESSION_INVALID_SECURITY_PARAMS:
            ESP_LOGE(TAG, "==> [SEC] Tham số bảo mật không hợp lệ!");
            break;
        case PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH:
            ESP_LOGE(TAG, "==> [SEC] SAI MÃ BẢO MẬT (PoP)! Mã Proof of Possession đúng là: abcd1234");
            break;
        default:
            break;
        }
    } else if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_set_ps(WIFI_PS_NONE);
            esp_wifi_set_max_tx_power(48); // 12 dBm
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)event_data;
            s_is_connected = false;
            ESP_LOGW(TAG, "==> [Wi-Fi Disconnected] Mã lý do: %d - %s",
                     disconn->reason, wifi_reason_to_str(disconn->reason));
            if (s_retry_num < 10) {
                s_retry_num++;
                ESP_LOGW(TAG, "Mất kết nối Wi-Fi. Nghỉ 1.5s và thử kết nối lại (lần %d/10)...", s_retry_num);
                vTaskDelay(pdMS_TO_TICKS(1500));
                esp_wifi_connect();
            } else {
                ESP_LOGE(TAG, "Không thể kết nối lại Wi-Fi sau 10 lần thử!");
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        s_is_connected = true;
        s_retry_num = 0;
        ESP_LOGI(TAG, "==========================================================");
        ESP_LOGI(TAG, "  ĐÈN ĐÃ KẾT NỐI WI-FI THÀNH CÔNG!                       ");
        ESP_LOGI(TAG, "  Địa chỉ IP được cấp: %s", s_ip_str);
        ESP_LOGI(TAG, "==========================================================");
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        app_driver_set_prov_status(PROV_STATUS_SUCCESS);
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

    /* 1. Country code configuration for Vietnam: channels 1 to 13 */
    wifi_country_t country = {
        .cc = "VN",
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_AUTO,
    };
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));

    /* Đăng ký các event handler cho Provisioning, BLE transport, Security và IP */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
}

static void wifi_station_initialize(void)
{
    /* Đăng ký handler WIFI_EVENT cho chế độ Station thông thường khi thiết bị đã được cấp phát */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    /* Start Wi-Fi in station mode */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    /* Tinh chỉnh cấu hình lưu trong Flash NVS sang WPA2/HT20/PMF-off/All-Channel-Scan */
    wifi_config_t sta_cfg;
    if (esp_wifi_get_config(WIFI_IF_STA, &sta_cfg) == ESP_OK) {
        sta_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        sta_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        sta_cfg.sta.threshold.rssi = -127;
        sta_cfg.sta.pmf_cfg.capable = false;
        sta_cfg.sta.pmf_cfg.required = false;
        sta_cfg.sta.disable_wpa3_compatible_mode = 1;
        sta_cfg.sta.failure_retry_cnt = 3;
        esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
        ESP_LOGI(TAG, "wifi_station_initialize: Đã cập nhật cấu hình NVS sang tối ưu 2.4GHz (SSID: %s)", sta_cfg.sta.ssid);
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    /* Tắt Power Save & giảm TX Power sau khi khởi động Wi-Fi */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(48)); // 12 dBm

    ESP_LOGI(TAG, "wifi_station_initialize: Đang kết nối Router Wi-Fi (C3 SuperMini 2.4GHz Tuned)...");
    esp_wifi_connect();

    /* Waiting until either connection is established (WIFI_CONNECTED_BIT) or failed (WIFI_FAIL_BIT) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Đã kết nối thành công tới Router (IP: %s)", s_ip_str);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Không thể kết nối tới Router Wi-Fi!");
    } else {
        ESP_LOGE(TAG, "Sự kiện Wi-Fi bất thường");
    }
}

static void wifi_prov_mgr_initialize(void)
{
    /* Cấu hình Provisioning Manager với scheme BLE và thử lại tối đa 10 lần */
    wifi_prov_mgr_config_t config = {
        .network_prov_wifi_conn_cfg = {
            .wifi_conn_attempts = 10,
        },
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BLE
    };

    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    if (!provisioned) {
        ESP_LOGI(TAG, "Thiết bị chưa được cấu hình Wi-Fi. Bắt đầu BLE Provisioning...");

        char service_name[16];
        get_device_service_name(service_name, sizeof(service_name));

        wifi_prov_security_t security = APP_PROV_SEC_MODE;
        const char *pop = APP_PROV_POP;
        const char *service_key = NULL;

        uint8_t custom_service_uuid[] = {
            0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
            0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
        };
        wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);

        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, pop, service_name, service_key));

        /* In mã QR Code trực quan trên màn hình terminal */
        wifi_prov_print_qr(service_name, pop, PROV_TRANSPORT_BLE);
        app_driver_set_prov_status(PROV_STATUS_WAITING);
    } else {
        ESP_LOGI(TAG, "==========================================================");
        ESP_LOGI(TAG, "  Thiết bị ĐÃ ĐƯỢC CẤP PHÁT MẠNG trước đó!               ");
        ESP_LOGI(TAG, "  Tự động kết nối Wi-Fi từ Flash NVS, giải phóng BLE...  ");
        ESP_LOGI(TAG, "==========================================================");

        wifi_prov_mgr_deinit();
        app_driver_set_prov_status(PROV_STATUS_CONNECTING);
        wifi_station_initialize();
    }
}

void app_main(void)
{
    int i = 0;
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  PBL5 Smart Light - Chapter 4: Smart Wi-Fi Provisioning  ");
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

    /* 4. Wi-Fi Provisioning initialization */
    ESP_LOGI(TAG, "Bắt đầu khởi tạo quy trình Cấp phát mạng...");
    wifi_prov_mgr_initialize();

    while (1) {
        if (s_is_connected) {
            ESP_LOGI(TAG, "[%02d] Smart Light running [ONLINE] | IP: %s", i++, s_ip_str);
        } else {
            ESP_LOGW(TAG, "[%02d] Smart Light running [PROVISIONING / WAITING]", i++);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
