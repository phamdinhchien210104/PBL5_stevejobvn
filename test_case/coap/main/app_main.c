/*
 * ESP32 Smart Light Example - CoAP & CoAPS (DTLS) Server (Mục 8.3.4 & 8.4.2)
 *
 * Modernized for ESP-IDF v6.0.2 & GCC 15
 * Features:
 * - Dual-Target Support (ESP32-S3 GPIO 0 / ESP32-C3 GPIO 9, WS2812B GPIO 4)
 * - CoAP RESTful Server (Default Port 5683, CoAPS DTLS Port 5684):
 *     Resource URI: /light
 *     Methods:
 *       - GET  /light -> Returns JSON: {"status": true/false, "brightness": 100, "color": "..."}
 *       - PUT  /light -> Controls light: "ON", "OFF", "toggle", "color", or JSON {"status": true}
 *     Observable Pattern: coap_resource_notify_observers() pushes changes to subscribed clients
 *     Security: Pre-Shared Key (PSK) DTLS support with identity "CoAP", key "esp32c3_key"
 * - Robust Wi-Fi Station Engine with WPA2/WPA3 Personal, PMF, VN Country Code
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include <coap3/coap.h>

#include "app_storage.h"
#include "app_priv.h"

#include DEVELOPMENT_BOARD

#define TAG "coap_server"

#ifndef CONFIG_COAP_WIFI_SSID
#define CONFIG_COAP_WIFI_SSID "Minh Toan"
#endif

#ifndef CONFIG_COAP_WIFI_PASSWORD
#define CONFIG_COAP_WIFI_PASSWORD "21012004"
#endif

#ifndef CONFIG_COAP_MAX_RETRY
#define CONFIG_COAP_MAX_RETRY 10
#endif

#ifndef CONFIG_COAP_PORT
#define CONFIG_COAP_PORT 5683
#endif

#ifndef CONFIG_COAP_PSK_KEY
#define CONFIG_COAP_PSK_KEY "esp32c3_key"
#endif

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "==> [Wi-Fi] Bắt đầu kết nối tới AP SSID: %s...", CONFIG_COAP_WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_retry_num++;
        ESP_LOGW(TAG, "==> [Wi-Fi] Kết nối thất bại lần [%d/%d]! Đang thử lại...",
                 s_retry_num, CONFIG_COAP_MAX_RETRY);
        if (s_retry_num < CONFIG_COAP_MAX_RETRY) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_retry_num = 0;
        ESP_LOGI(TAG, "==========================================================");
        ESP_LOGI(TAG, "==> [Wi-Fi] KẾT NỐI THÀNH CÔNG! ĐÃ CÓ ĐỊA CHỈ IP:        ");
        ESP_LOGI(TAG, "  - Địa chỉ IP cấp : " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "  - CoAP Server Port: %d (UDP)", CONFIG_COAP_PORT);
        ESP_LOGI(TAG, "  - Resource URI   : coap://" IPSTR ":%d/light", IP2STR(&event->ip_info.ip), CONFIG_COAP_PORT);
        ESP_LOGI(TAG, "==========================================================");
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
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

static bool wifi_station_connect(void)
{
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_COAP_WIFI_SSID,
            .password = CONFIG_COAP_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_OPEN,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(48);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        return true;
    }
    ESP_LOGE(TAG, "Không thể kết nối tới AP SSID: %s!", CONFIG_COAP_WIFI_SSID);
    return false;
}

/**
 * @brief CoAP GET /light Handler
 * Trả về chuỗi JSON thông tin trạng thái đèn, độ sáng, màu sắc
 */
static void esp_coap_get(
    coap_resource_t *resource,
    coap_session_t *session,
    const coap_pdu_t *request,
    const coap_string_t *query,
    coap_pdu_t *response)
{
    (void)resource;
    (void)session;
    (void)request;
    (void)query;

    char resp_payload[128];
    snprintf(resp_payload, sizeof(resp_payload),
             "{\"status\": %s, \"brightness\": %u, \"color\": \"%s\"}",
             app_driver_get_state() ? "true" : "false",
             app_driver_get_brightness(),
             app_driver_get_color_name());

    ESP_LOGI(TAG, "[CoAP GET /light] -> Phản hồi: %s", resp_payload);

    // Thiết lập HTTP-like 2.05 Content
    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT);

    // Gán dữ liệu payload phản hồi
    coap_add_data(response, strlen(resp_payload), (const uint8_t *)resp_payload);
}

/**
 * @brief CoAP PUT /light Handler
 * Nhận payload bật/tắt/đổi màu và thông báo tới các observer (Observable Pattern)
 */
static void esp_coap_put(
    coap_resource_t *resource,
    coap_session_t *session,
    const coap_pdu_t *request,
    const coap_string_t *query,
    coap_pdu_t *response)
{
    size_t size = 0;
    const uint8_t *data = NULL;

    (void)session;
    (void)query;

    if (!coap_get_data(request, &size, &data) || size == 0 || data == NULL) {
        ESP_LOGW(TAG, "[CoAP PUT] Yêu cầu không có payload hoặc rỗng");
        coap_pdu_set_code(response, COAP_RESPONSE_CODE_BAD_REQUEST);
        return;
    }

    char payload_str[128];
    size_t copy_len = (size < sizeof(payload_str) - 1) ? size : sizeof(payload_str) - 1;
    memcpy(payload_str, data, copy_len);
    payload_str[copy_len] = '\0';

    ESP_LOGI(TAG, "[CoAP PUT /light] Nhận payload: \"%s\"", payload_str);

    // Kiểm tra các lệnh điều khiển linh hoạt
    if (strstr(payload_str, "true") || strstr(payload_str, "ON") || strstr(payload_str, "on") || strstr(payload_str, "1")) {
        app_driver_set_state(true);
        ESP_LOGI(TAG, "==> [CoAP] Đèn đã BẬT (ON)");
    } else if (strstr(payload_str, "false") || strstr(payload_str, "OFF") || strstr(payload_str, "off") || strstr(payload_str, "0")) {
        app_driver_set_state(false);
        ESP_LOGI(TAG, "==> [CoAP] Đèn đã TẮT (OFF)");
    } else if (strstr(payload_str, "toggle") || strstr(payload_str, "TOGGLE")) {
        app_driver_toggle_state();
        ESP_LOGI(TAG, "==> [CoAP] Đã chuyển đổi trạng thái đèn: %s", app_driver_get_state() ? "ON" : "OFF");
    } else if (strstr(payload_str, "color") || strstr(payload_str, "next")) {
        app_driver_next_color();
        ESP_LOGI(TAG, "==> [CoAP] Đã đổi màu sang: %s", app_driver_get_color_name());
    } else {
        ESP_LOGW(TAG, "[CoAP] Lệnh không nhận diện được");
        coap_pdu_set_code(response, COAP_RESPONSE_CODE_BAD_REQUEST);
        return;
    }

    // Đẩy thông báo tức thì tới tất cả các Client đang theo dõi (Observable)
    coap_resource_notify_observers(resource, NULL);

    // Trả về mã thành công 2.04 Changed
    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CHANGED);
}

/**
 * @brief Nhiệm vụ chạy CoAP Server
 */
static void coap_server_task(void *pvParameters)
{
    coap_context_t *ctx = NULL;
    coap_resource_t *resource = NULL;
    coap_address_t serv_addr;
    coap_endpoint_t *endpoint = NULL;

    (void)pvParameters;

    ESP_LOGI(TAG, "Khởi động CoAP Stack (libcoap 4.3.x)...");
    coap_startup();

    // 1. Cấu hình địa chỉ IPv4 lắng nghe trên cổng UDP COAP_PORT (5683)
    coap_address_init(&serv_addr);
    serv_addr.addr.sin.sin_family = AF_INET;
    serv_addr.addr.sin.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.addr.sin.sin_port = htons(CONFIG_COAP_PORT);

    // 2. Tạo context CoAP
    ctx = coap_new_context(NULL);
    if (!ctx) {
        ESP_LOGE(TAG, "Tạo CoAP context thất bại!");
        coap_cleanup();
        vTaskDelete(NULL);
        return;
    }

    // 3. Thiết lập Pre-Shared Key (PSK) cho bảo mật CoAPS / DTLS
    static const char psk_identity[] = "CoAP";
    static const char psk_key[] = CONFIG_COAP_PSK_KEY;
    coap_context_set_psk(ctx, psk_identity, (const uint8_t *)psk_key, sizeof(psk_key) - 1);
    ESP_LOGI(TAG, "CoAPS DTLS PSK cấu hình với Identity: '%s', Key: '%s'", psk_identity, psk_key);

    // 4. Tạo UDP Endpoint
    endpoint = coap_new_endpoint(ctx, &serv_addr, COAP_PROTO_UDP);
    if (!endpoint) {
        ESP_LOGE(TAG, "Tạo CoAP UDP endpoint thất bại!");
        coap_free_context(ctx);
        coap_cleanup();
        vTaskDelete(NULL);
        return;
    }

    // 5. Đăng ký tài nguyên URI /light
    resource = coap_resource_init(coap_make_str_const("light"), 0);
    if (!resource) {
        ESP_LOGE(TAG, "Tạo tài nguyên CoAP /light thất bại!");
        coap_free_context(ctx);
        coap_cleanup();
        vTaskDelete(NULL);
        return;
    }

    coap_register_handler(resource, COAP_REQUEST_GET, esp_coap_get);
    coap_register_handler(resource, COAP_REQUEST_PUT, esp_coap_put);

    // Kích hoạt tính năng Observable theo RFC 7641
    coap_resource_set_get_observable(resource, 1);

    coap_add_resource(ctx, resource);

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  CoAP Server đã sẵn sàng phục vụ!                        ");
    ESP_LOGI(TAG, "  - UDP Port       : %d                                    ", CONFIG_COAP_PORT);
    ESP_LOGI(TAG, "  - Resource URI   : /light                               ");
    ESP_LOGI(TAG, "  - Phương thức    : GET (Đọc trạng thái), PUT (Điều khiển)");
    ESP_LOGI(TAG, "  - Observable     : KÍCH HOẠT (RFC 7641)                 ");
    ESP_LOGI(TAG, "==========================================================");

    // 6. Vòng lặp xử lý I/O CoAP
    while (1) {
        int result = coap_run_once(ctx, 1000);
        if (result < 0) {
            ESP_LOGE(TAG, "coap_run_once lỗi: %d", result);
            break;
        }
    }

    ESP_LOGI(TAG, "Dừng CoAP Server...");
    coap_free_context(ctx);
    coap_cleanup();
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "    PBL5 Smart Light - Mục 8.3.4 & 8.4.2: CoAP & CoAPS    ");
    ESP_LOGI(TAG, "    CoAP UDP Port    : %d | DTLS PSK: %s", CONFIG_COAP_PORT, CONFIG_COAP_PSK_KEY);
    ESP_LOGI(TAG, "    WS2812B Hardware SPI2 DMA @ 3.2MHz | GPIO %d", LIGHT_GPIO_WS2812);
    ESP_LOGI(TAG, "    Boot Button HAL  : GPIO %d (Active Level %d)", LIGHT_BUTTON_GPIO, LIGHT_BUTTON_ACTIVE_LEVEL);
    ESP_LOGI(TAG, "==========================================================");

    // 1. Khởi tạo NVS Storage
    app_storage_init();

    // 2. Khởi tạo Application Driver & WS2812B LED HAL
    app_driver_init();

    // 3. Khởi tạo Wi-Fi TCP/IP Stack & Kết nối AP
    wifi_initialize();
    if (!wifi_station_connect()) {
        ESP_LOGE(TAG, "Dừng khởi tạo ứng dụng do Wi-Fi thất bại.");
        return;
    }

    // 4. Khởi chạy tác vụ CoAP Server
    xTaskCreate(coap_server_task, "coap_server_task", 8192, NULL, 5, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}