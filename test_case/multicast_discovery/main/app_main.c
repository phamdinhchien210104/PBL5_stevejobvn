/*
 * ESP32 Smart Light Example - Multicast Discovery (Mục 8.2.2 & 8.2.3)
 *
 * Modernized for ESP-IDF v6.0.2 & GCC 15
 * Features:
 * - Dual-Target Support (ESP32-S3 GPIO 0 / ESP32-C3 GPIO 9, WS2812B GPIO 4)
 * - UDP Multicast Discovery Server (Default Group 232.10.11.12, Port 3333):
 *     Joins IGMP Multicast Group via IP_ADD_MEMBERSHIP
 *     Configures IP_MULTICAST_TTL = 1 (limits packet to local subnet)
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
#include "esp_netif.h"
#include "nvs_flash.h"

#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "app_storage.h"
#include "app_priv.h"

#include DEVELOPMENT_BOARD

#define TAG "multicast_discovery"

#ifndef CONFIG_MULTICAST_WIFI_SSID
#define CONFIG_MULTICAST_WIFI_SSID "Minh Toan"
#endif

#ifndef CONFIG_MULTICAST_WIFI_PASSWORD
#define CONFIG_MULTICAST_WIFI_PASSWORD "21012004"
#endif

#ifndef CONFIG_MULTICAST_MAX_RETRY
#define CONFIG_MULTICAST_MAX_RETRY 10
#endif

#ifndef CONFIG_MULTICAST_IPV4_ADDR
#define CONFIG_MULTICAST_IPV4_ADDR "232.10.11.12"
#endif

#ifndef CONFIG_MULTICAST_PORT
#define CONFIG_MULTICAST_PORT 3333
#endif

#ifndef CONFIG_MULTICAST_TTL
#define CONFIG_MULTICAST_TTL 1
#endif

#ifndef CONFIG_MULTICAST_IS_CLIENT
#define CONFIG_MULTICAST_IS_CLIENT 0
#endif

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;

static const char *DISCOVERY_QUERY = "Are you Espressif IOT Smart Light";
#if CONFIG_IDF_TARGET_ESP32S3
static const char *DISCOVERY_REPLY = "ESP32-S3 Smart Light https 443";
#else
static const char *DISCOVERY_REPLY = "ESP32-C3 Smart Light https 443";
#endif

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "==> [Wi-Fi] Bắt đầu kết nối tới AP SSID: %s...", CONFIG_MULTICAST_WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_retry_num++;
        ESP_LOGW(TAG, "==> [Wi-Fi] Kết nối thất bại lần [%d/%d]! Đang thử lại...",
                 s_retry_num, CONFIG_MULTICAST_MAX_RETRY);
        if (s_retry_num < CONFIG_MULTICAST_MAX_RETRY) {
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
        ESP_LOGI(TAG, "  - Multicast Group: %s", CONFIG_MULTICAST_IPV4_ADDR);
        ESP_LOGI(TAG, "  - Cổng UDP       : %d", CONFIG_MULTICAST_PORT);
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
            .ssid = CONFIG_MULTICAST_WIFI_SSID,
            .password = CONFIG_MULTICAST_WIFI_PASSWORD,
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
    ESP_LOGE(TAG, "Không thể kết nối tới AP SSID: %s!", CONFIG_MULTICAST_WIFI_SSID);
    return false;
}

/**
 * @brief Gia nhập nhóm Multicast IPv4 qua IGMP (IP_ADD_MEMBERSHIP)
 */
static esp_err_t join_multicast_group(int sockfd)
{
    struct ip_mreq imreq = {0};
    struct in_addr iaddr = {0};
    int err = 0;

    // Cấu hình interface mạng STA cho gói tin multicast
    esp_netif_ip_info_t ip_info = {0};
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        inet_addr_from_ip4addr(&iaddr, &ip_info.ip);
        err = setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_IF, &iaddr, sizeof(struct in_addr));
        if (err < 0) {
            ESP_LOGW(TAG, "Lỗi setsockopt IP_MULTICAST_IF: errno %d", errno);
        }
    }

    // Đăng ký gia nhập nhóm Multicast
    inet_aton(CONFIG_MULTICAST_IPV4_ADDR, &imreq.imr_multiaddr.s_addr);
    imreq.imr_interface.s_addr = htonl(INADDR_ANY);

    err = setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imreq, sizeof(struct ip_mreq));
    if (err < 0) {
        ESP_LOGE(TAG, "Gia nhập nhóm Multicast %s thất bại! errno %d", CONFIG_MULTICAST_IPV4_ADDR, errno);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "==> [IGMP] ĐÃ GIA NHẬP NHÓM MULTICAST %s THÀNH CÔNG!", CONFIG_MULTICAST_IPV4_ADDR);
    return ESP_OK;
}

/**
 * @brief Nhiệm vụ Server Multicast Discovery chạy nền (Receiver / Smart Light Device)
 */
static void multicast_server_task(void *pvParameters)
{
    char rx_buffer[128];
    int opt = 1;

    while (1) {
        int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sockfd < 0) {
            ESP_LOGE(TAG, "Không thể tạo socket UDP multicast: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        // Cho phép chia sẻ / tái sử dụng địa chỉ và cổng
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // Thiết lập TTL cho Multicast (TTL=1: chỉ trong cùng subnet)
        uint8_t ttl = CONFIG_MULTICAST_TTL;
        setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(uint8_t));

        struct sockaddr_in saddr = {
            .sin_family = AF_INET,
            .sin_port = htons(CONFIG_MULTICAST_PORT),
            .sin_addr.s_addr = htonl(INADDR_ANY),
        };

        int err = bind(sockfd, (struct sockaddr *)&saddr, sizeof(saddr));
        if (err < 0) {
            ESP_LOGE(TAG, "Không thể bind socket tới cổng %d: errno %d", CONFIG_MULTICAST_PORT, errno);
            close(sockfd);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        ESP_LOGI(TAG, "Socket UDP đã bind thành công tới cổng %d", CONFIG_MULTICAST_PORT);

        // Gia nhập nhóm Multicast
        if (join_multicast_group(sockfd) != ESP_OK) {
            close(sockfd);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        ESP_LOGI(TAG, "Sẵn sàng lắng nghe truy vấn Multicast Discovery...");

        while (1) {
            struct sockaddr_in source_addr;
            socklen_t addr_len = sizeof(source_addr);

            int len = recvfrom(sockfd, rx_buffer, sizeof(rx_buffer) - 1, 0,
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
            ESP_LOGI(TAG, "[MULTICAST RECV] Nhận gói tin từ: %s:%d | Độ dài: %d bytes",
                     sender_ip, sender_port, len);
            ESP_LOGI(TAG, "Nội dung nhận: \"%s\"", rx_buffer);

            // Kiểm tra nội dung gói tin Discovery chuẩn
            if (strstr(rx_buffer, DISCOVERY_QUERY) != NULL) {
                ESP_LOGI(TAG, "==> Khớp chuỗi Discovery! Nhấp nháy đèn xanh báo hiệu...");

                // Hiệu ứng LED xanh lá phản hồi
                uint8_t old_r, old_g, old_b;
                app_driver_get_rgb(&old_r, &old_g, &old_b);
                bool was_on = app_driver_get_state();

                app_driver_set_color(0, 255, 0); // Xanh lá sáng rực
                app_driver_set_state(true);

                // Gửi phản hồi đơn công (Unicast) ngược lại cho người gửi
                int sent = sendto(sockfd, DISCOVERY_REPLY, strlen(DISCOVERY_REPLY), 0,
                                  (struct sockaddr *)&source_addr, addr_len);
                if (sent < 0) {
                    ESP_LOGE(TAG, "Gửi unicast reply thất bại: errno %d", errno);
                } else {
                    ESP_LOGI(TAG, "[MULTICAST REPLY] Đã gửi Unicast phản hồi tới %s:%d",
                             sender_ip, sender_port);
                    ESP_LOGI(TAG, "Nội dung phản hồi: \"%s\"", DISCOVERY_REPLY);
                }

                // Giữ LED xanh 350ms rồi khôi phục màu cũ
                vTaskDelay(pdMS_TO_TICKS(350));
                app_driver_set_color(old_r, old_g, old_b);
                app_driver_set_state(was_on);
            } else {
                ESP_LOGW(TAG, "Gói tin không khớp câu lệnh discovery chuẩn, bỏ qua.");
            }
            ESP_LOGI(TAG, "--------------------------------------------------");
        }

        close(sockfd);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelete(NULL);
}

/**
 * @brief Nhiệm vụ Multicast Client (Sender) kiểm thử phát sóng truy vấn
 */
static void multicast_client_task(void *pvParameters)
{
    char rx_buffer[128];
    uint8_t ttl = CONFIG_MULTICAST_TTL;

    while (1) {
        int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sockfd < 0) {
            ESP_LOGE(TAG, "Client socket creation failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(uint8_t));

        struct timeval timeout = {
            .tv_sec = 3,
            .tv_usec = 0
        };
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        struct sockaddr_in dest_addr = {
            .sin_family = AF_INET,
            .sin_port = htons(CONFIG_MULTICAST_PORT),
        };
        inet_aton(CONFIG_MULTICAST_IPV4_ADDR, &dest_addr.sin_addr.s_addr);

        ESP_LOGI(TAG, "[CLIENT] Gửi gói tin Multicast tới %s:%d: \"%s\"",
                 CONFIG_MULTICAST_IPV4_ADDR, CONFIG_MULTICAST_PORT, DISCOVERY_QUERY);

        int sent = sendto(sockfd, DISCOVERY_QUERY, strlen(DISCOVERY_QUERY), 0,
                          (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (sent < 0) {
            ESP_LOGE(TAG, "[CLIENT] Lỗi gửi multicast: errno %d", errno);
        } else {
            struct sockaddr_in from_addr;
            socklen_t from_len = sizeof(from_addr);
            int len = recvfrom(sockfd, rx_buffer, sizeof(rx_buffer) - 1, 0,
                               (struct sockaddr *)&from_addr, &from_len);
            if (len > 0) {
                rx_buffer[len] = '\0';
                char sender_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &from_addr.sin_addr, sender_ip, sizeof(sender_ip));
                ESP_LOGI(TAG, "[CLIENT] ==> NHẬN PHẢN HỒI TỪ THIẾT BỊ %s:%d: \"%s\"",
                         sender_ip, ntohs(from_addr.sin_port), rx_buffer);
            } else {
                ESP_LOGW(TAG, "[CLIENT] Hết thời gian chờ phản hồi (3s)!");
            }
        }

        close(sockfd);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "    PBL5 Smart Light - Mục 8.2.2/8.2.3: Multicast UDP     ");
    ESP_LOGI(TAG, "    Multicast Group  : %s", CONFIG_MULTICAST_IPV4_ADDR);
    ESP_LOGI(TAG, "    UDP Port         : %d | TTL: %d", CONFIG_MULTICAST_PORT, CONFIG_MULTICAST_TTL);
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

    // 4. Khởi chạy tác vụ Discovery
#if CONFIG_MULTICAST_IS_CLIENT
    ESP_LOGI(TAG, "Khởi chạy chế độ: MULTICAST CLIENT (Sender)");
    xTaskCreate(multicast_client_task, "mcast_client_task", 4096, NULL, 5, NULL);
#else
    ESP_LOGI(TAG, "Khởi chạy chế độ: MULTICAST SERVER (Smart Light Device)");
    xTaskCreate(multicast_server_task, "mcast_server_task", 4096, NULL, 5, NULL);
#endif

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
