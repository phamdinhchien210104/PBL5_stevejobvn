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
#define CONFIG_ESP_WIFI_SSID "Hoang Long"
#endif

#ifndef CONFIG_ESP_WIFI_PASSWORD
#define CONFIG_ESP_WIFI_PASSWORD "12341234"
#endif

#ifndef CONFIG_ESP_MAXIMUM_RETRY
#define CONFIG_ESP_MAXIMUM_RETRY 5
#endif

/* The event group allows multiple bits for each event:
 * - WIFI_CONNECTED_BIT: Connected to the AP with an IP
 * - WIFI_FAIL_BIT: Failed to connect after maximum retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi_connection";

/* FreeRTOS event group to signal when we are connected */
static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi Station started, connecting to AP SSID: %s...", CONFIG_ESP_WIFI_SSID);
        app_driver_set_wifi_status(WIFI_STATUS_CONNECTING);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Thử kết nối lại AP (lần %d/%d)...", s_retry_num, CONFIG_ESP_MAXIMUM_RETRY);
            app_driver_set_wifi_status(WIFI_STATUS_CONNECTING);
        } else {
            ESP_LOGE(TAG, "Kết nối AP thất bại sau %d lần thử!", CONFIG_ESP_MAXIMUM_RETRY);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            app_driver_set_wifi_status(WIFI_STATUS_FAILED);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "==========================================================");
        ESP_LOGI(TAG, "  ĐÃ KẾT NỐI WI-FI THÀNH CÔNG!                           ");
        ESP_LOGI(TAG, "  Địa chỉ IP được cấp: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "==========================================================");
        s_retry_num = 0;
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
    /* Start Wi-Fi in station mode */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_station_initialize finished.");

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
        ESP_LOGI(TAG, "[%02d] Smart Light running, Wi-Fi connected to: %s", i++, CONFIG_ESP_WIFI_SSID);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
