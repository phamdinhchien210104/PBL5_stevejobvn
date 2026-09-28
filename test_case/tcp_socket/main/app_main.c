/*
 * ESP32 Smart Light Example - TCP Socket Server & Client (Mục 8.3.1)
 *
 * Modernized for ESP-IDF v6.0.2 & GCC 15
 * Features:
 * - Dual-Target Support (ESP32-S3 GPIO 0 / ESP32-C3 GPIO 9, WS2812B GPIO 4)
 * - TCP Socket Server (Default Port 3333):
 *     TCP Keep-Alive: SO_KEEPALIVE, TCP_KEEPIDLE=5s, TCP_KEEPINTVL=3s, TCP_KEEPCNT=3
 *     Commands supported:
 *       - "Open the light"  / "ON"  -> Turn ON LED strip
 *       - "Close the light" / "OFF" -> Turn OFF LED strip
 *       - "Toggle"                  -> Toggle ON/OFF state
 *       - "Color"                   -> Cycle through 5 color presets
 *       - "Status"                  -> Query current state, brightness, and color
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

#define TAG "tcp_socket"

#ifndef CONFIG_TCP_WIFI_SSID
#define CONFIG_TCP_WIFI_SSID "Minh Toan"
#endif

#ifndef CONFIG_TCP_WIFI_PASSWORD
#define CONFIG_TCP_WIFI_PASSWORD "21012004"
#endif

#ifndef CONFIG_TCP_MAX_RETRY
#define CONFIG_TCP_MAX_RETRY 10
#endif

#ifndef CONFIG_TCP_PORT
#define CONFIG_TCP_PORT 3333
#endif

#ifndef CONFIG_TCP_IS_CLIENT
#define CONFIG_TCP_IS_CLIENT 0
#endif

#ifndef CONFIG_TCP_SERVER_HOST
#define CONFIG_TCP_SERVER_HOST "192.168.1.100"
#endif

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "==> [Wi-Fi] Bắt đầu kết nối tới AP SSID: %s...", CONFIG_TCP_WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_retry_num++;
        ESP_LOGW(TAG, "==> [Wi-Fi] Kết nối thất bại lần [%d/%d]! Đang thử lại...",
                 s_retry_num, CONFIG_TCP_MAX_RETRY);
        if (s_retry_num < CONFIG_TCP_MAX_RETRY) {
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
        ESP_LOGI(TAG, "  - TCP Server Port: %d", CONFIG_TCP_PORT);
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
            .ssid = CONFIG_TCP_WIFI_SSID,
            .password = CONFIG_TCP_WIFI_PASSWORD,
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
    ESP_LOGE(TAG, "Không thể kết nối tới AP SSID: %s!", CONFIG_TCP_WIFI_SSID);
    return false;
}

/**
 * @brief Chuyển chuỗi sang chữ thường và loại bỏ ký tự khoảng trắng / xuống dòng ở đuôi
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
 * @brief Xử lý lệnh nhận được từ TCP Client và chuẩn bị chuỗi phản hồi
 */
static void handle_tcp_command(const char *raw_cmd, char *response_buf, size_t max_resp_len)
{
    char cmd[128];
    strncpy(cmd, raw_cmd, sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    trim_and_lower(cmd);

    ESP_LOGI(TAG, "[TCP CMD] Phân tích lệnh: \"%s\"", cmd);

    if (strstr(cmd, "open the light") != NULL || strcmp(cmd, "on") == 0 || strcmp(cmd, "turn on") == 0) {
        app_driver_set_state(true);
        snprintf(response_buf, max_resp_len,
                 "+OK: Light is ON | Brightness: %u%% | Color: %s\r\n",
                 app_driver_get_brightness(), app_driver_get_color_name());
    } else if (strstr(cmd, "close the light") != NULL || strcmp(cmd, "off") == 0 || strcmp(cmd, "turn off") == 0) {
        app_driver_set_state(false);
        snprintf(response_buf, max_resp_len, "+OK: Light is OFF\r\n");
    } else if (strcmp(cmd, "toggle") == 0) {
        app_driver_toggle_state();
        snprintf(response_buf, max_resp_len,
                 "+OK: Light toggled -> State is %s\r\n",
                 app_driver_get_state() ? "ON" : "OFF");
    } else if (strcmp(cmd, "color") == 0 || strcmp(cmd, "next") == 0) {
        app_driver_next_color();
        snprintf(response_buf, max_resp_len,
                 "+OK: Switched color to %s\r\n",
                 app_driver_get_color_name());
    } else if (strcmp(cmd, "status") == 0 || strcmp(cmd, "get") == 0) {
        snprintf(response_buf, max_resp_len,
                 "+OK: State=%s, Brightness=%u%%, Color=%s\r\n",
                 app_driver_get_state() ? "ON" : "OFF",
                 app_driver_get_brightness(),
                 app_driver_get_color_name());
    } else {
        snprintf(response_buf, max_resp_len,
                 "-ERR: Unknown command '%s'. Supported: 'Open the light', 'Close the light', 'Toggle', 'Color', 'Status'\r\n",
                 raw_cmd);
    }
}

/**
 * @brief Nhiệm vụ TCP Server nền (Receiver / Smart Light Device)
 */
static void tcp_server_task(void *pvParameters)
{
    char rx_buffer[256];
    char resp_buffer[256];
    int opt = 1;

    while (1) {
        int listenfd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (listenfd < 0) {
            ESP_LOGE(TAG, "Không thể tạo TCP listen socket: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        // Kích hoạt SO_REUSEADDR
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in server_addr = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = htonl(INADDR_ANY),
            .sin_port = htons(CONFIG_TCP_PORT),
        };

        int err = bind(listenfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Lỗi bind socket tới cổng %d: errno %d", CONFIG_TCP_PORT, errno);
            close(listenfd);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        err = listen(listenfd, 2);
        if (err < 0) {
            ESP_LOGE(TAG, "Lỗi listen socket: errno %d", errno);
            close(listenfd);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        ESP_LOGI(TAG, "TCP Server đã sẵn sàng lắng nghe kết nối tại cổng %d", CONFIG_TCP_PORT);

        while (1) {
            struct sockaddr_in source_addr;
            socklen_t addr_len = sizeof(source_addr);
            int sock = accept(listenfd, (struct sockaddr *)&source_addr, &addr_len);
            if (sock < 0) {
                ESP_LOGE(TAG, "Lỗi accept kết nối: errno %d", errno);
                break;
            }

            // Kích hoạt tính năng TCP Keep-Alive để tự động dọn dẹp Zombie Client
            int keep_alive = 1;
            int keep_idle = 5;      // 5 giây không có dữ liệu sẽ bắt đầu gửi probe
            int keep_interval = 3;  // Gửi lại probe mỗi 3 giây
            int keep_count = 3;     // Quá 3 lần không phản hồi sẽ đóng kết nối
            setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(int));
            setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keep_idle, sizeof(int));
            setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keep_interval, sizeof(int));
            setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keep_count, sizeof(int));

            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &source_addr.sin_addr, client_ip, sizeof(client_ip));
            uint16_t client_port = ntohs(source_addr.sin_port);

            ESP_LOGI(TAG, "==> [TCP CONNECT] Chấp nhận kết nối từ Client: %s:%d", client_ip, client_port);

            // Gửi banner chào mừng
            const char *welcome = "Welcome to PBL5 Smart Light TCP Server!\r\nType 'Open the light' or 'Status'\r\n";
            send(sock, welcome, strlen(welcome), 0);

            while (1) {
                int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
                if (len < 0) {
                    ESP_LOGW(TAG, "Lỗi đọc socket hoặc mất kết nối: errno %d", errno);
                    break;
                } else if (len == 0) {
                    ESP_LOGI(TAG, "Client %s:%d đã đóng kết nối (FIN)", client_ip, client_port);
                    break;
                }

                rx_buffer[len] = '\0';
                ESP_LOGI(TAG, "Nhận %d bytes từ %s:%d: \"%s\"", len, client_ip, client_port, rx_buffer);

                // Xử lý lệnh điều khiển
                handle_tcp_command(rx_buffer, resp_buffer, sizeof(resp_buffer));

                // Phản hồi kết quả cho Client
                int sent = send(sock, resp_buffer, strlen(resp_buffer), 0);
                if (sent < 0) {
                    ESP_LOGE(TAG, "Gửi phản hồi thất bại: errno %d", errno);
                    break;
                }
            }

            shutdown(sock, 0);
            close(sock);
            ESP_LOGI(TAG, "Đã đóng kết nối client %s:%d", client_ip, client_port);
        }

        close(listenfd);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelete(NULL);
}

/**
 * @brief Nhiệm vụ TCP Client kiểm thử gửi lệnh tới server từ xa
 */
static void tcp_client_task(void *pvParameters)
{
    char rx_buffer[128];
    const char *payload = "Open the light\n";

    while (1) {
        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "[TCP CLIENT] Không thể tạo socket: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        struct sockaddr_in dest_addr = {
            .sin_family = AF_INET,
            .sin_port = htons(CONFIG_TCP_PORT),
        };
        inet_aton(CONFIG_TCP_SERVER_HOST, &dest_addr.sin_addr.s_addr);

        ESP_LOGI(TAG, "[TCP CLIENT] Đang kết nối tới %s:%d...", CONFIG_TCP_SERVER_HOST, CONFIG_TCP_PORT);
        if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
            ESP_LOGW(TAG, "[TCP CLIENT] Kết nối thất bại: errno %d", errno);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        ESP_LOGI(TAG, "[TCP CLIENT] Kết nối thành công! Đang gửi: \"%s\"", payload);
        send(sock, payload, strlen(payload), 0);

        int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len > 0) {
            rx_buffer[len] = '\0';
            ESP_LOGI(TAG, "[TCP CLIENT] Nhận phản hồi: \"%s\"", rx_buffer);
        }

        shutdown(sock, 0);
        close(sock);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "    PBL5 Smart Light - Mục 8.3.1: TCP Socket Server/Client");
    ESP_LOGI(TAG, "    TCP Port         : %d", CONFIG_TCP_PORT);
    ESP_LOGI(TAG, "    Keep-Alive       : Idle=5s, Intvl=3s, Probes=3");
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

    // 4. Khởi chạy tác vụ TCP
#if CONFIG_TCP_IS_CLIENT
    ESP_LOGI(TAG, "Khởi chạy chế độ: TCP CLIENT");
    xTaskCreate(tcp_client_task, "tcp_client_task", 4096, NULL, 5, NULL);
#else
    ESP_LOGI(TAG, "Khởi chạy chế độ: TCP SERVER (Smart Light Device)");
    xTaskCreate(tcp_server_task, "tcp_server_task", 4096, NULL, 5, NULL);
#endif

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
