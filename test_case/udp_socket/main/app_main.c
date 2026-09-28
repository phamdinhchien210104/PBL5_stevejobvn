/*
 * ESP32 Smart Light Example - UDP Socket Server & Client (Mục 8.3.3)
 *
 * Modernized for ESP-IDF v6.0.2 & GCC 15
 * Features:
 * - Dual-Target Support (ESP32-S3 GPIO 0 / ESP32-C3 GPIO 9, WS2812B GPIO 4)
 * - UDP Socket Server (Default Port 3333):
 *     SO_REUSEADDR enabled
 *     Commands supported:
 *       - "Open the light"  / "ON"  -> Turn ON LED strip & reply with ACK
 *       - "Close the light" / "OFF" -> Turn OFF LED strip & reply with ACK
 *       - "Toggle"                  -> Toggle ON/OFF state & reply with ACK
 *       - "Color"                   -> Cycle through 5 color presets & reply with ACK
 *       - "Status"                  -> Query current state, brightness, and color
 *     Application-level ACK returned to client via sendto()
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

#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "app_storage.h"
#include "app_priv.h"

#include DEVELOPMENT_BOARD

#define TAG "udp_socket"

#ifndef CONFIG_UDP_WIFI_SSID
#define CONFIG_UDP_WIFI_SSID "Minh Toan"
#endif

#ifndef CONFIG_UDP_WIFI_PASSWORD
#define CONFIG_UDP_WIFI_PASSWORD "21012004"
#endif

#ifndef CONFIG_UDP_MAX_RETRY
#define CONFIG_UDP_MAX_RETRY 10
#endif

#ifndef CONFIG_UDP_PORT
#define CONFIG_UDP_PORT 3333
#endif

#ifndef CONFIG_UDP_IS_CLIENT
#define CONFIG_UDP_IS_CLIENT 0
#endif

#ifndef CONFIG_UDP_SERVER_HOST
#define CONFIG_UDP_SERVER_HOST "192.168.1.100"
#endif

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "==> [Wi-Fi] Bắt đầu kết nối tới AP SSID: %s...", CONFIG_UDP_WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_retry_num++;
        ESP_LOGW(TAG, "==> [Wi-Fi] Kết nối thất bại lần [%d/%d]! Đang thử lại...",
                 s_retry_num, CONFIG_UDP_MAX_RETRY);
        if (s_retry_num < CONFIG_UDP_MAX_RETRY) {
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
        ESP_LOGI(TAG, "  - UDP Server Port: %d", CONFIG_UDP_PORT);
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
            .ssid = CONFIG_UDP_WIFI_SSID,
            .password = CONFIG_UDP_WIFI_PASSWORD,
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
    ESP_LOGE(TAG, "Không thể kết nối tới AP SSID: %s!", CONFIG_UDP_WIFI_SSID);
    return false;
}

/**
 * @brief Chuẩn hóa chuỗi (cắt khoảng trắng và đưa về chữ thường)
 */
static void trim_and_lower(char *str)
{
    int len = strlen(str);
    while (len > 0 && (str[len - 1] == '\r' || str[len - 1] == '\n' || isspace((unsigned char)str[len - 1]))) {
        str[--len] = '\0';
    }
    for (int i = 0; i < len; i++) {
        str[i] = (char)tolower((unsigned char)str[i]);
    }
}

/**
 * @brief Xử lý lệnh điều khiển UDP và chuẩn bị gói tin ACK
 */
static void handle_udp_command(const char *raw_cmd, char *response_buf, size_t max_resp_len)
{
    char cmd[128];
    strncpy(cmd, raw_cmd, sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    trim_and_lower(cmd);

    ESP_LOGI(TAG, "[UDP CMD] Phân tích lệnh: \"%s\"", cmd);

    if (strstr(cmd, "open the light") != NULL || strcmp(cmd, "on") == 0 || strcmp(cmd, "turn on") == 0) {
        app_driver_set_state(true);
        snprintf(response_buf, max_resp_len,
                 "Open the light OK | State=ON, Brightness=%u%%, Color=%s\r\n",
                 app_driver_get_brightness(), app_driver_get_color_name());
    } else if (strstr(cmd, "close the light") != NULL || strcmp(cmd, "off") == 0 || strcmp(cmd, "turn off") == 0) {
        app_driver_set_state(false);
        snprintf(response_buf, max_resp_len, "Close the light OK | State=OFF\r\n");
    } else if (strcmp(cmd, "toggle") == 0) {
        app_driver_toggle_state();
        snprintf(response_buf, max_resp_len,
                 "Toggle OK | State=%s\r\n",
                 app_driver_get_state() ? "ON" : "OFF");
    } else if (strcmp(cmd, "color") == 0 || strcmp(cmd, "next") == 0) {
        app_driver_next_color();
        snprintf(response_buf, max_resp_len,
                 "Color OK | Current Color=%s\r\n",
                 app_driver_get_color_name());
    } else if (strcmp(cmd, "status") == 0 || strcmp(cmd, "get") == 0) {
        snprintf(response_buf, max_resp_len,
                 "Status OK | State=%s, Brightness=%u%%, Color=%s\r\n",
                 app_driver_get_state() ? "ON" : "OFF",
                 app_driver_get_brightness(),
                 app_driver_get_color_name());
    } else {
        snprintf(response_buf, max_resp_len,
                 "ERR: Unknown command '%s'. Supported: 'Open the light', 'Close the light', 'Toggle', 'Color', 'Status'\r\n",
                 raw_cmd);
    }
}

/**
 * @brief Nhiệm vụ UDP Server nền (Receiver / Smart Light Device)
 */
static void udp_server_task(void *pvParameters)
{
    char rx_buffer[256];
    char resp_buffer[256];
    int opt = 1;

    while (1) {
        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "Không thể tạo socket UDP: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        // Kích hoạt SO_REUSEADDR
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in server_addr = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = htonl(INADDR_ANY),
            .sin_port = htons(CONFIG_UDP_PORT),
        };

        int err = bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Lỗi bind socket UDP tới cổng %d: errno %d", CONFIG_UDP_PORT, errno);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        ESP_LOGI(TAG, "UDP Server đã sẵn sàng lắng nghe datagram tại cổng %d", CONFIG_UDP_PORT);

        while (1) {
            struct sockaddr_in source_addr;
            socklen_t addr_len = sizeof(source_addr);

            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0,
                               (struct sockaddr *)&source_addr, &addr_len);
            if (len < 0) {
                ESP_LOGE(TAG, "recvfrom thất bại: errno %d", errno);
                break;
            }

            rx_buffer[len] = '\0';
            char sender_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &source_addr.sin_addr, sender_ip, sizeof(sender_ip));
            uint16_t sender_port = ntohs(source_addr.sin_port);

            ESP_LOGI(TAG, "--------------------------------------------------");
            ESP_LOGI(TAG, "[UDP RECV] Nhận %d bytes từ %s:%d: \"%s\"",
                     len, sender_ip, sender_port, rx_buffer);

            // Xử lý lệnh điều khiển và cập nhật trạng thái đèn
            handle_udp_command(rx_buffer, resp_buffer, sizeof(resp_buffer));

            // Gửi gói tin ACK tầng ứng dụng ngược lại cho Client
            int sent = sendto(sock, resp_buffer, strlen(resp_buffer), 0,
                              (struct sockaddr *)&source_addr, addr_len);
            if (sent < 0) {
                ESP_LOGE(TAG, "Gửi ACK thất bại: errno %d", errno);
            } else {
                ESP_LOGI(TAG, "[UDP ACK] Đã phản hồi tới %s:%d -> %s",
                         sender_ip, sender_port, resp_buffer);
            }
            ESP_LOGI(TAG, "--------------------------------------------------");
        }

        close(sock);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelete(NULL);
}

/**
 * @brief Nhiệm vụ UDP Client kiểm thử phát datagram và chờ nhận ACK
 */
static void udp_client_task(void *pvParameters)
{
    char rx_buffer[128];
    const char *payload = "Open the light";

    while (1) {
        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "[UDP CLIENT] Không thể tạo socket: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        struct timeval timeout = {
            .tv_sec = 2,
            .tv_usec = 0
        };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        struct sockaddr_in dest_addr = {
            .sin_family = AF_INET,
            .sin_port = htons(CONFIG_UDP_PORT),
        };
        inet_aton(CONFIG_UDP_SERVER_HOST, &dest_addr.sin_addr.s_addr);

        ESP_LOGI(TAG, "[UDP CLIENT] Gửi datagram tới %s:%d: \"%s\"",
                 CONFIG_UDP_SERVER_HOST, CONFIG_UDP_PORT, payload);

        sendto(sock, payload, strlen(payload), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0,
                           (struct sockaddr *)&from_addr, &from_len);
        if (len > 0) {
            rx_buffer[len] = '\0';
            ESP_LOGI(TAG, "[UDP CLIENT] ==> Nhận ACK: \"%s\"", rx_buffer);
        } else {
            ESP_LOGW(TAG, "[UDP CLIENT] Không nhận được ACK trong 2 giây.");
        }

        close(sock);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "    PBL5 Smart Light - Mục 8.3.3: UDP Socket Server/Client");
    ESP_LOGI(TAG, "    UDP Port         : %d", CONFIG_UDP_PORT);
    ESP_LOGI(TAG, "    SO_REUSEADDR     : ENABLED");
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

    // 4. Khởi chạy tác vụ UDP
#if CONFIG_UDP_IS_CLIENT
    ESP_LOGI(TAG, "Khởi chạy chế độ: UDP CLIENT");
    xTaskCreate(udp_client_task, "udp_client_task", 4096, NULL, 5, NULL);
#else
    ESP_LOGI(TAG, "Khởi chạy chế độ: UDP SERVER (Smart Light Device)");
    xTaskCreate(udp_server_task, "udp_server_task", 4096, NULL, 5, NULL);
#endif

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
