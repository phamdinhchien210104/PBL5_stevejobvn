/*
 * ESP32-C3 CoAP Light Control
 *
 * ESP-IDF 5.4.2
 * libcoap 4.3.x
 *
 * Wi-Fi
 *   ↓
 * CoAP UDP Server
 *   ↓
 * /light
 *   ├── GET  -> ON / OFF
 *   └── PUT  -> ON / OFF
 *
 * Example:
 *   GET /light
 *   PUT /light with "ON"
 *   PUT /light with "OFF"
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include <coap3/coap.h>

#include "app_priv.h"


/* ============================================================
 * Configuration
 * ============================================================ */

#define WIFI_SSID       "Ryan"
#define WIFI_PASSWORD   "quy43200411"

#define WIFI_MAX_RETRY  5

#define COAP_PORT       5683


/* ============================================================
 * TAG
 * ============================================================ */

static const char *TAG = "COAP_LIGHT";


/* ============================================================
 * Wi-Fi
 * ============================================================ */

static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;


/* ============================================================
 * LED state
 * ============================================================ */

static bool led_state = false;


/* ============================================================
 * Wi-Fi Event Handler
 * ============================================================ */

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START) {

        ESP_LOGI(TAG, "Wi-Fi STA started");

        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {

        if (s_retry_num < WIFI_MAX_RETRY) {

            esp_wifi_connect();

            s_retry_num++;

            ESP_LOGW(TAG,
                     "Retry Wi-Fi connection (%d/%d)",
                     s_retry_num,
                     WIFI_MAX_RETRY);

        } else {

            xEventGroupSetBits(
                s_wifi_event_group,
                WIFI_FAIL_BIT
            );

        }

    } else if (event_base == IP_EVENT &&
               event_id == IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG,
                 "Got IP address: " IPSTR,
                 IP2STR(&event->ip_info.ip));

        s_retry_num = 0;

        xEventGroupSetBits(
            s_wifi_event_group,
            WIFI_CONNECTED_BIT
        );
    }
}


/* ============================================================
 * Wi-Fi Initialization
 * ============================================================ */

static void wifi_initialize(void)
{
    ESP_LOGI(TAG, "Initializing Wi-Fi...");

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(
        esp_wifi_init(&cfg)
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL
        )
    );

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,

            .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_STA)
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config
        )
    );

    ESP_ERROR_CHECK(
        esp_wifi_start()
    );

    ESP_LOGI(TAG,
             "Connecting to Wi-Fi: %s",
             WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY
    );

    if (bits & WIFI_CONNECTED_BIT) {

        ESP_LOGI(TAG,
                 "Wi-Fi connected successfully");

    } else if (bits & WIFI_FAIL_BIT) {

        ESP_LOGE(TAG,
                 "Failed to connect to Wi-Fi");

    } else {

        ESP_LOGE(TAG,
                 "Unexpected Wi-Fi event");
    }
}


/* ============================================================
 * CoAP GET /light
 *
 * Client:
 *   GET coap://ESP32_IP/light
 *
 * Response:
 *   ON
 *   OFF
 * ============================================================ */

static void esp_coap_get(
    coap_resource_t *resource,
    coap_session_t *session,
    const coap_pdu_t *request,
    const coap_string_t *query,
    coap_pdu_t *response)
{
    const char *status;

    (void)resource;
    (void)session;
    (void)request;
    (void)query;

    if (led_state) {
        status = "ON";
    } else {
        status = "OFF";
    }

    ESP_LOGI(TAG,
             "CoAP GET /light -> %s",
             status);

    /*
     * HTTP-like:
     * 2.05 Content
     */
    coap_pdu_set_code(
        response,
        COAP_RESPONSE_CODE_CONTENT
    );

    /*
     * Response payload
     */
    coap_add_data(
        response,
        strlen(status),
        (const uint8_t *)status
    );
}


/* ============================================================
 * CoAP PUT /light
 *
 * Client:
 *   PUT /light
 *   Payload = ON
 *
 * or
 *
 *   PUT /light
 *   Payload = OFF
 * ============================================================ */

static void esp_coap_put(
    coap_resource_t *resource,
    coap_session_t *session,
    const coap_pdu_t *request,
    const coap_string_t *query,
    coap_pdu_t *response)
{
    size_t size = 0;

    const uint8_t *data = NULL;

    (void)resource;
    (void)session;
    (void)query;

    /*
     * Get request payload
     */
    if (!coap_get_data(
            request,
            &size,
            &data)) {

        ESP_LOGW(TAG,
                 "CoAP PUT: no payload");

        coap_pdu_set_code(
            response,
            COAP_RESPONSE_CODE_BAD_REQUEST
        );

        return;
    }

    if (size == 0 || data == NULL) {

        ESP_LOGW(TAG,
                 "CoAP PUT: empty payload");

        coap_pdu_set_code(
            response,
            COAP_RESPONSE_CODE_BAD_REQUEST
        );

        return;
    }


    /* ========================================================
     * Command ON
     * ======================================================== */

    if (size == 2 &&
        memcmp(data, "ON", 2) == 0) {

        led_state = true;

        /*
         * Control physical LED
         */
        app_driver_set_state(true);

        ESP_LOGI(TAG,
                 "CoAP PUT /light -> ON");

        /*
         * 2.04 Changed
         */
        coap_pdu_set_code(
            response,
            COAP_RESPONSE_CODE_CHANGED
        );

    }


    /* ========================================================
     * Command OFF
     * ======================================================== */

    else if (size == 3 &&
             memcmp(data, "OFF", 3) == 0) {

        led_state = false;

        /*
         * Control physical LED
         */
        app_driver_set_state(false);

        ESP_LOGI(TAG,
                 "CoAP PUT /light -> OFF");

        /*
         * 2.04 Changed
         */
        coap_pdu_set_code(
            response,
            COAP_RESPONSE_CODE_CHANGED
        );

    }


    /* ========================================================
     * Unknown command
     * ======================================================== */

    else {

        ESP_LOGW(TAG,
                 "Unknown CoAP command");

        ESP_LOG_BUFFER_HEXDUMP(
            TAG,
            data,
            size,
            ESP_LOG_WARN
        );

        coap_pdu_set_code(
            response,
            COAP_RESPONSE_CODE_BAD_REQUEST
        );
    }
}


/* ============================================================
 * CoAP Server
 * ============================================================ */

static void coap_server_task(void *pvParameters)
{
    coap_context_t *ctx = NULL;

    coap_resource_t *resource = NULL;

    coap_address_t serv_addr;

    coap_endpoint_t *endpoint = NULL;

    (void)pvParameters;


    ESP_LOGI(TAG,
             "Starting CoAP server...");


    /* --------------------------------------------------------
     * Initialize CoAP library
     * -------------------------------------------------------- */

    coap_startup();


    /* --------------------------------------------------------
     * Initialize server address
     *
     * Use IPv6 as in the original libcoap example.
     * ESP-IDF/LwIP can support IPv6 socket endpoint.
     * -------------------------------------------------------- */

    coap_address_init(&serv_addr);

    serv_addr.addr.sin6.sin6_family = AF_INET6;

    serv_addr.addr.sin6.sin6_port =
        htons(COAP_PORT);


    /* --------------------------------------------------------
     * Create CoAP context
     * -------------------------------------------------------- */

    ctx = coap_new_context(NULL);

    if (ctx == NULL) {

        ESP_LOGE(TAG,
                 "Failed to create CoAP context");

        coap_cleanup();

        vTaskDelete(NULL);

        return;
    }


    /* --------------------------------------------------------
     * Create UDP endpoint
     * -------------------------------------------------------- */

    endpoint = coap_new_endpoint(
        ctx,
        &serv_addr,
        COAP_PROTO_UDP
    );

    if (endpoint == NULL) {

        ESP_LOGE(TAG,
                 "Failed to create CoAP UDP endpoint");

        coap_free_context(ctx);

        coap_cleanup();

        vTaskDelete(NULL);

        return;
    }


    /* --------------------------------------------------------
     * Create /light resource
     * -------------------------------------------------------- */

    resource = coap_resource_init(
        coap_make_str_const("light"),
        0
    );

    if (resource == NULL) {

        ESP_LOGE(TAG,
                 "Failed to create /light resource");

        coap_free_context(ctx);

        coap_cleanup();

        vTaskDelete(NULL);

        return;
    }


    /* --------------------------------------------------------
     * Register GET handler
     * -------------------------------------------------------- */

    coap_register_handler(
        resource,
        COAP_REQUEST_GET,
        esp_coap_get
    );


    /* --------------------------------------------------------
     * Register PUT handler
     * -------------------------------------------------------- */

    coap_register_handler(
        resource,
        COAP_REQUEST_PUT,
        esp_coap_put
    );


    /* --------------------------------------------------------
     * Add resource to CoAP context
     * -------------------------------------------------------- */

    coap_add_resource(
        ctx,
        resource
    );


    ESP_LOGI(TAG,
             "================================");

    ESP_LOGI(TAG,
             "CoAP server started");

    ESP_LOGI(TAG,
             "Port: %d",
             COAP_PORT);

    ESP_LOGI(TAG,
             "Resource: /light");

    ESP_LOGI(TAG,
             "GET  /light -> LED status");

    ESP_LOGI(TAG,
             "PUT  /light -> ON / OFF");

    ESP_LOGI(TAG,
             "================================");


    /* --------------------------------------------------------
     * CoAP event loop
     * -------------------------------------------------------- */

    while (1) {

        int result;

        result = coap_run_once(
            ctx,
            1000
        );

        if (result < 0) {

            ESP_LOGE(TAG,
                     "coap_run_once() failed");

            break;
        }
    }


    /* --------------------------------------------------------
     * Cleanup
     * -------------------------------------------------------- */

    ESP_LOGI(TAG,
             "Stopping CoAP server");

    coap_free_context(ctx);

    coap_cleanup();

    vTaskDelete(NULL);
}


/* ============================================================
 * Main
 * ============================================================ */

void app_main(void)
{
    ESP_LOGI(TAG,
             "================================");

    ESP_LOGI(TAG,
             "ESP32-C3 CoAP Light Control");

    ESP_LOGI(TAG,
             "ESP-IDF 5.4.2");

    ESP_LOGI(TAG,
             "================================");


    /* --------------------------------------------------------
     * Initialize NVS
     * -------------------------------------------------------- */

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_ERROR_CHECK(
            nvs_flash_erase()
        );

        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);


    /* --------------------------------------------------------
     * Initialize LED driver
     * -------------------------------------------------------- */

    ESP_LOGI(TAG,
             "Initializing LED driver...");

    app_driver_init();


    /* --------------------------------------------------------
     * Make sure LED starts OFF
     * -------------------------------------------------------- */

    led_state = false;

    app_driver_set_state(false);


    /* --------------------------------------------------------
     * Initialize Wi-Fi
     * -------------------------------------------------------- */

    wifi_initialize();


    /* --------------------------------------------------------
     * Start CoAP server
     * -------------------------------------------------------- */

    xTaskCreate(
        coap_server_task,
        "coap_server",
        8192,
        NULL,
        5,
        NULL
    );


    ESP_LOGI(TAG,
             "Application started");
}