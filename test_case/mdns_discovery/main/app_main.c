/*
 * ESP32 Smart Light Example - mDNS Discovery (Mục 8.2.4)
 *
 * Modernized for ESP-IDF v6.0.2 & GCC 15
 * Features:
 * - Dual-Target Support (ESP32-S3 GPIO 0 / ESP32-C3 GPIO 9, WS2812B GPIO 4)
 * - mDNS Zero-Config Service Responder (Apple Bonjour / RFC 6762):
 *     Hostname     : my_smart_light.local
 *     Instance Name: esp32c3_smart_light (or esp32s3_smart_light)
 *     Service      : _http._tcp on Port 80
 *     TXT Records  : board=esp32c3, path=/foobar
 * - Lightweight HTTP Server on Port 80 to demonstrate end-to-end resolution
 *     Visual feedback: Pulses Green when accessed via http://my_smart_light.local
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
#include "mdns.h"

#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "app_storage.h"
#include "app_priv.h"

#include DEVELOPMENT_BOARD

#define TAG "mdns_discovery"

#ifndef CONFIG_MDNS_WIFI_SSID
#define CONFIG_MDNS_WIFI_SSID "Minh Toan"
#endif

#ifndef CONFIG_MDNS_WIFI_PASSWORD
#define CONFIG_MDNS_WIFI_PASSWORD "21012004"
#endif

#ifndef CONFIG_MDNS_MAX_RETRY
#define CONFIG_MDNS_MAX_RETRY 10
#endif

#ifndef CONFIG_MDNS_HOSTNAME
#define CONFIG_MDNS_HOSTNAME "my_smart_light"
#endif

#ifndef CONFIG_MDNS_INSTANCE_NAME
#if CONFIG_IDF_TARGET_ESP32S3
#define CONFIG_MDNS_INSTANCE_NAME "esp32s3_smart_light"
#else
#define CONFIG_MDNS_INSTANCE_NAME "esp32c3_smart_light"
#endif
#endif

#ifndef CONFIG_MDNS_SERVICE_PORT
#define CONFIG_MDNS_SERVICE_PORT 80
#endif

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "==> [Wi-Fi] Bắt đầu kết nối tới AP SSID: %s...", CONFIG_MDNS_WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_retry_num++;
        ESP_LOGW(TAG, "==> [Wi-Fi] Kết nối thất bại lần [%d/%d]! Đang thử lại...",
                 s_retry_num, CONFIG_MDNS_MAX_RETRY);
        if (s_retry_num < CONFIG_MDNS_MAX_RETRY) {
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
        ESP_LOGI(TAG, "  - Tên miền mDNS  : %s.local", CONFIG_MDNS_HOSTNAME);
        ESP_LOGI(TAG, "  - Dịch vụ HTTP   : http://%s.local:%d", CONFIG_MDNS_HOSTNAME, CONFIG_MDNS_SERVICE_PORT);
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
            .ssid = CONFIG_MDNS_WIFI_SSID,
            .password = CONFIG_MDNS_WIFI_PASSWORD,
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
    ESP_LOGE(TAG, "Không thể kết nối tới AP SSID: %s!", CONFIG_MDNS_WIFI_SSID);
    return false;
}

/**
 * @brief Khởi tạo mDNS Responder và đăng ký dịch vụ HTTP + TXT Records
 */
static esp_err_t mdns_discovery_start(void)
{
    ESP_LOGI(TAG, "Khởi tạo mDNS Core Service...");
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_init thất bại: 0x%x", err);
        return err;
    }

    // Thiết lập hostname (được phân giải thành my_smart_light.local)
    err = mdns_hostname_set(CONFIG_MDNS_HOSTNAME);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_hostname_set thất bại: 0x%x", err);
        return err;
    }
    ESP_LOGI(TAG, "==> mDNS Hostname đã thiết lập: [%s.local]", CONFIG_MDNS_HOSTNAME);

    // Thiết lập instance name đại diện cho thiết bị
    err = mdns_instance_name_set(CONFIG_MDNS_INSTANCE_NAME);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_instance_name_set thất bại: 0x%x", err);
        return err;
    }
    ESP_LOGI(TAG, "==> mDNS Instance Name: [%s]", CONFIG_MDNS_INSTANCE_NAME);

    // Chuẩn bị bản ghi TXT Record metadata
#if CONFIG_IDF_TARGET_ESP32S3
    const char *board_name = "esp32s3";
#else
    const char *board_name = "esp32c3";
#endif

    mdns_txt_item_t service_txt_data[2] = {
        {"board", board_name},
        {"path", "/foobar"}
    };

    // Đăng ký dịch vụ _http._tcp trên cổng chỉ định (port 80)
    err = mdns_service_add(CONFIG_MDNS_INSTANCE_NAME, "_http", "_tcp",
                           CONFIG_MDNS_SERVICE_PORT, service_txt_data, 2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_service_add thất bại: 0x%x", err);
        return err;
    }

    ESP_LOGI(TAG, "==> Đã công bố dịch vụ mDNS: _http._tcp trên port %d", CONFIG_MDNS_SERVICE_PORT);
    ESP_LOGI(TAG, "    Metadata TXT: board=%s, path=/foobar", board_name);
    return ESP_OK;
}

/**
 * @brief Server HTTP cổng 80 phản hồi khi client truy cập qua tên miền mDNS
 */
static void http_server_task(void *pvParameters)
{
    char rx_buffer[512];
    int opt = 1;

    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Không thể tạo TCP socket cổng 80: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in saddr = {
        .sin_family = AF_INET,
        .sin_port = htons(CONFIG_MDNS_SERVICE_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    int err = bind(listen_sock, (struct sockaddr *)&saddr, sizeof(saddr));
    if (err < 0) {
        ESP_LOGE(TAG, "Không thể bind socket cổng %d: errno %d", CONFIG_MDNS_SERVICE_PORT, errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    err = listen(listen_sock, 4);
    if (err < 0) {
        ESP_LOGE(TAG, "Lỗi lắng nghe socket cổng %d: errno %d", CONFIG_MDNS_SERVICE_PORT, errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "HTTP Server sẵn sàng tại http://%s.local:%d/", CONFIG_MDNS_HOSTNAME, CONFIG_MDNS_SERVICE_PORT);

    while (1) {
        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int client_sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (client_sock < 0) {
            ESP_LOGE(TAG, "accept thất bại: errno %d", errno);
            break;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &source_addr.sin_addr, client_ip, sizeof(client_ip));
        int len = recv(client_sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len > 0) {
            rx_buffer[len] = '\0';
            ESP_LOGI(TAG, "[HTTP GET] Yêu cầu từ %s:%d", client_ip, ntohs(source_addr.sin_port));

            // Hiệu ứng LED xanh lá phản hồi trực quan
            uint8_t old_r, old_g, old_b;
            app_driver_get_rgb(&old_r, &old_g, &old_b);
            bool was_on = app_driver_get_state();

            app_driver_set_color(0, 255, 0);
            app_driver_set_state(true);

            const char *http_resp =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain; charset=utf-8\r\n"
                "Connection: close\r\n\r\n"
                "PBL5 Smart Light mDNS Discovery OK!\r\n"
                "Resolved via: my_smart_light.local:80\r\n";

            send(client_sock, http_resp, strlen(http_resp), 0);

            vTaskDelay(pdMS_TO_TICKS(350));
            app_driver_set_color(old_r, old_g, old_b);
            app_driver_set_state(was_on);
        }

        close(client_sock);
    }

    close(listen_sock);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "    PBL5 Smart Light - Mục 8.2.4: mDNS Zero-Config        ");
    ESP_LOGI(TAG, "    mDNS Hostname    : %s.local", CONFIG_MDNS_HOSTNAME);
    ESP_LOGI(TAG, "    Service Advertised: _http._tcp on Port %d", CONFIG_MDNS_SERVICE_PORT);
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

    // 4. Khởi chạy dịch vụ mDNS Discovery
    if (mdns_discovery_start() == ESP_OK) {
        ESP_LOGI(TAG, "==> mDNS Responder đã khởi động thành công!");
    }

    // 5. Khởi chạy HTTP Server nền trên cổng 80 để phản hồi truy vấn
    xTaskCreate(http_server_task, "http_server_task", 4096, NULL, 5, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
