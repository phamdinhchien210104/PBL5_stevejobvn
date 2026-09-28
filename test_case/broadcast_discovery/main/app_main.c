/*
 * ESP32 Smart Light Example - Broadcast Discovery (Mục 8.2.1)
 *
 * Modernized for ESP-IDF v6.0.2 & GCC 15
 * Features:
 * - Dual-Target Support (ESP32-S3 GPIO 0 / ESP32-C3 GPIO 9, WS2812B GPIO 4)
 * - UDP Broadcast Discovery Server (Default Port 3333):
 *     Receives: "Are you Espressif IOT Smart Light"
 *     Replies : "ESP32-C3 Smart Light https 443" via Unicast
 *     Visual feedback: Pulses Green when discovered
 * - Robust Wi-Fi Station Engine with WPA2/WPA3 Personal, PMF, VN Country Code
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

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

#include DEVELOPMENT_BOARD

#define TAG "broadcast_discovery"

#ifndef CONFIG_BROADCAST_WIFI_SSID
#define CONFIG_BROADCAST_WIFI_SSID "Minh Toan"
#endif

#ifndef CONFIG_BROADCAST_WIFI_PASSWORD
#define CONFIG_BROADCAST_WIFI_PASSWORD "21012004"
#endif

#ifndef CONFIG_BROADCAST_MAX_RETRY
#define CONFIG_BROADCAST_MAX_RETRY 10
#endif

#ifndef CONFIG_BROADCAST_PORT
#define CONFIG_BROADCAST_PORT 3333
#endif

#ifndef CONFIG_BROADCAST_IS_CLIENT
#define CONFIG_BROADCAST_IS_CLIENT 0
#endif

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;

static const char *DISCOVERY_QUERY = "Are you Espressif IOT Smart Light";
static const char *DISCOVERY_REPLY = "ESP32-C3 Smart Light https 443";

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "==> [Wi-Fi] Bắt đầu kết nối tới AP SSID: %s...", CONFIG_BROADCAST_WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_retry_num++;
        ESP_LOGW(TAG, "==> [Wi-Fi] Kết nối thất bại lần [%d/%d]! Đang thử lại...",
                 s_retry_num, CONFIG_BROADCAST_MAX_RETRY);
        if (s_retry_num < CONFIG_BROADCAST_MAX_RETRY) {
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
        ESP_LOGI(TAG, "  - Cổng Broadcast : UDP Port %d", CONFIG_BROADCAST_PORT);
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
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.authmode = WIFI_AUTH_OPEN,
            .threshold.rssi = -127,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
            .disable_wpa3_compatible_mode = 0,
            .failure_retry_cnt = 3,
        },
    };
    strncpy((char *)wifi_config.sta.ssid, CONFIG_BROADCAST_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, CONFIG_BROADCAST_WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(48);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(35000));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

static void broadcast_server_task(void *pvParameters)
{
    char rx_buffer[128];
    struct sockaddr_in from_addr;
    socklen_t from_addr_len = sizeof(from_addr);

    while (1) {
        int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) {
            ESP_LOGE(TAG, "Không thể tạo socket UDP: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        int opt = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in server_addr = {
            .sin_family      = AF_INET,
            .sin_port        = htons(CONFIG_BROADCAST_PORT),
            .sin_addr.s_addr = htonl(INADDR_ANY),
        };

        if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            ESP_LOGE(TAG, "Bind socket thất bại port %d: errno %d", CONFIG_BROADCAST_PORT, errno);
            close(sockfd);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        ESP_LOGI(TAG, "UDP Broadcast Server đang lắng nghe trên cổng %d...", CONFIG_BROADCAST_PORT);

        while (1) {
            memset(rx_buffer, 0, sizeof(rx_buffer));
            int len = recvfrom(sockfd, rx_buffer, sizeof(rx_buffer) - 1, 0,
                               (struct sockaddr *)&from_addr, &from_addr_len);

            if (len < 0) {
                ESP_LOGE(TAG, "Lỗi recvfrom: errno %d", errno);
                break;
            }

            char sender_ip[16];
            inet_ntoa_r(from_addr.sin_addr, sender_ip, sizeof(sender_ip));
            uint16_t sender_port = ntohs(from_addr.sin_port);

            ESP_LOGI(TAG, "Receive udp broadcast from %s:%u, data is %s",
                     sender_ip, sender_port, rx_buffer);

            if (strstr(rx_buffer, DISCOVERY_QUERY)) {
                int sent = sendto(sockfd, DISCOVERY_REPLY, strlen(DISCOVERY_REPLY), 0,
                                  (struct sockaddr *)&from_addr, from_addr_len);
                if (sent < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                } else {
                    ESP_LOGI(TAG, "Message sent successfully -> Phản hồi unicast tới %s:%u: '%s'",
                             sender_ip, sender_port, DISCOVERY_REPLY);
                    // Nhấp nháy xanh lá phản hồi thị giác
                    app_driver_set_color(0, 255, 0);
                }
            }
        }

        close(sockfd);
    }
    vTaskDelete(NULL);
}

static void broadcast_client_task(void *pvParameters)
{
    int opt_val = 1;
    char rx_buffer[128];
    struct sockaddr_in from_addr;
    socklen_t from_addr_len = sizeof(from_addr);

    struct sockaddr_in dest_addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(CONFIG_BROADCAST_PORT),
        .sin_addr.s_addr = htonl(INADDR_BROADCAST),
    };

    while (1) {
        int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd >= 0) {
            setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &opt_val, sizeof(opt_val));

            ESP_LOGI(TAG, "==> [Client] Đang gửi gói tin UDP Broadcast tìm kiếm Smart Light...");
            sendto(sockfd, DISCOVERY_QUERY, strlen(DISCOVERY_QUERY), 0,
                   (struct sockaddr *)&dest_addr, sizeof(dest_addr));

            struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
            setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            memset(rx_buffer, 0, sizeof(rx_buffer));
            int len = recvfrom(sockfd, rx_buffer, sizeof(rx_buffer) - 1, 0,
                               (struct sockaddr *)&from_addr, &from_addr_len);
            if (len > 0) {
                ESP_LOGI(TAG, "Receive udp unicast from %s:%d, data is %s",
                         inet_ntoa(from_addr.sin_addr), ntohs(from_addr.sin_port), rx_buffer);
            }
            close(sockfd);
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "   PBL5 Smart Light - Mục 8.2.1: Broadcast Discovery      ");
    ESP_LOGI(TAG, "   Role: %s | Port: %d                                    ",
             CONFIG_BROADCAST_IS_CLIENT ? "Client (Sender)" : "Server (Receiver)",
             CONFIG_BROADCAST_PORT);
    ESP_LOGI(TAG, "   WS2812B GPIO %d | Boot Button GPIO %d                  ",
             LIGHT_WS2818_GPIO, LIGHT_BUTTON_GPIO);
    ESP_LOGI(TAG, "==========================================================");

    app_storage_init();
    app_driver_init();
    wifi_initialize();

    while (!wifi_station_connect()) {
        ESP_LOGW(TAG, "Chưa kết nối Wi-Fi. Đang thử lại sau 5 giây...");
        s_retry_num = 0;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_wifi_connect();
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                               WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                               pdFALSE, pdFALSE,
                                               pdMS_TO_TICKS(35000));
        if (bits & WIFI_CONNECTED_BIT) break;
    }

#if CONFIG_BROADCAST_IS_CLIENT
    xTaskCreate(broadcast_client_task, "bcast_cli", 4096, NULL, 5, NULL);
#else
    xTaskCreate(broadcast_server_task, "bcast_srv", 4096, NULL, 5, NULL);
#endif

    uint32_t cnt = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        cnt++;
        ESP_LOGI(TAG, "[Heartbeat #%02" PRIu32 "] Light: %s (%u%%) | Free Heap: %" PRIu32 " bytes",
                 cnt, app_driver_get_state() ? "ON" : "OFF",
                 app_driver_get_brightness(), esp_get_free_heap_size());
    }
}
