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
 * @brief Handler xử lý endpoint dữ liệu tùy chọn (Custom Data)
 */
esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                   uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    if (inbuf) {
        ESP_LOGI(TAG, "Nhận dữ liệu tùy chỉnh từ app: %.*s", (int)inlen, (char *)inbuf);
    }
    char response[] = "SUCCESS";
    *outbuf = (uint8_t *)strdup(response);
    if (*outbuf == NULL) {
        ESP_LOGE(TAG, "Hệ thống hết bộ nhớ heap");
        return ESP_ERR_NO_MEM;
    }
    *outlen = strlen(response) + 1;
    return ESP_OK;
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
            ESP_LOGI(TAG, "==> [PROV] Đã nhận thông tin Wi-Fi từ điện thoại!");
            ESP_LOGI(TAG, "    SSID : %s", (char *)wifi_sta_cfg->ssid);
            app_driver_set_prov_status(PROV_STATUS_CONNECTING);
            break;
        }

        case WIFI_PROV_CRED_FAIL: {
            wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
            ESP_LOGE(TAG, "==> [PROV] Kết nối Wi-Fi thất bại sau khi nhận thông tin!");
            if (*reason == WIFI_PROV_STA_AUTH_ERROR) {
                ESP_LOGE(TAG, "    Lý do: SAI MẬT KHẨU WI-FI. Vui lòng nhập lại trên app điện thoại.");
            } else {
                ESP_LOGE(TAG, "    Lý do: KHÔNG TÌM THẤY ROUTER / AP.");
            }
            app_driver_set_prov_status(PROV_STATUS_FAILED);
            break;
        }

        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "==> [PROV] Đã xác thực thành công thông tin Wi-Fi!");
            break;

        case WIFI_PROV_END:
            ESP_LOGI(TAG, "==> [PROV] Quy trình cấp phát hoàn tất. Giải phóng BLE Stack...");
            wifi_prov_mgr_deinit();
            break;

        default:
            break;
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_is_connected = false;
        if (s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Thử kết nối lại Router Wi-Fi (lần %d/5)...", s_retry_num);
        } else {
            ESP_LOGE(TAG, "Mất kết nối Wi-Fi sau 5 lần thử!");
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
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

    /* Register our event handler for Wi-Fi, IP and Provisioning related events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
}

static void wifi_station_initialize(void)
{
    /* Start Wi-Fi in station mode */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_station_initialize: Đang kết nối Router Wi-Fi...");

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
    /* Cấu hình Provisioning Manager với scheme BLE */
    wifi_prov_mgr_config_t config = {
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

        wifi_prov_mgr_endpoint_create("custom-data");
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, pop, service_name, service_key));
        wifi_prov_mgr_endpoint_register("custom-data", custom_prov_data_handler, NULL);

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
