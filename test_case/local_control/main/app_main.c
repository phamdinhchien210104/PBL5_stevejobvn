/*
 * ESP32-S3 & ESP32-C3 Smart Light Project
 * Chapter 8: Local Control in Smart Light Project (Practice 8.5)
 *
 * Implements:
 * 1. 8.5.1 Local Control Server over Wi-Fi + HTTPS + mDNS (esp_local_ctrl)
 * 2. 8.5.2 Client Verification Interface & Status Property (JSON payload)
 * 3. 8.5.3 Fallback Local Control Server over Bluetooth LE (GATT Server)
 *
 * Hardware:
 * - ESP32-S3-DevKitC-1-N16R8 (Boot GPIO 0, LED GPIO 4)
 * - ESP32-C3-DevKitM-1 (Boot GPIO 9, LED GPIO 4)
 * - WS2812B NeoPixel 8-LED strip via Hardware SPI2 DMA @ 3.2MHz
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

#include "mdns.h"
#include "esp_local_ctrl.h"
#include "esp_https_server.h"

#include "app_storage.h"
#include "app_priv.h"

#if CONFIG_BT_ENABLED
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#endif

/* Fallback macro definitions if not configured via Kconfig menuconfig */
#ifndef CONFIG_LOCAL_CTRL_WIFI_SSID
#define CONFIG_LOCAL_CTRL_WIFI_SSID "MyHomeWiFi"
#endif
#ifndef CONFIG_LOCAL_CTRL_WIFI_PASSWORD
#define CONFIG_LOCAL_CTRL_WIFI_PASSWORD "12345678"
#endif
#ifndef CONFIG_LOCAL_CTRL_MAXIMUM_RETRY
#define CONFIG_LOCAL_CTRL_MAXIMUM_RETRY 5
#endif
#ifndef CONFIG_LOCAL_CTRL_MDNS_HOST_NAME
#define CONFIG_LOCAL_CTRL_MDNS_HOST_NAME "my_esp_ctrl_device"
#endif
#ifndef CONFIG_LOCAL_CTRL_BLE_DEVICE_NAME
#define CONFIG_LOCAL_CTRL_BLE_DEVICE_NAME "ESP32-LOCAL-LIGHT"
#endif

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "local_ctrl";

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;

/* =========================================================================
 * 1. WI-FI STATION & EVENT SYNCHRONIZATION
 * ========================================================================= */

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "==> [Wi-Fi] Bắt đầu kết nối tới AP SSID: %s...", CONFIG_LOCAL_CTRL_WIFI_SSID);
        app_driver_set_wifi_status(WIFI_STATUS_CONNECTING);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < CONFIG_LOCAL_CTRL_MAXIMUM_RETRY) {
            s_retry_num++;
            ESP_LOGW(TAG, "==> [Wi-Fi] Mất kết nối! Đang thử lại lần [%d/%d]...",
                     s_retry_num, CONFIG_LOCAL_CTRL_MAXIMUM_RETRY);
            app_driver_set_wifi_status(WIFI_STATUS_CONNECTING);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "==> [Wi-Fi] Kết nối thất bại sau %d lần thử lại!", CONFIG_LOCAL_CTRL_MAXIMUM_RETRY);
            app_driver_set_wifi_status(WIFI_STATUS_FAILED);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;

        wifi_ap_record_t ap_info = {0};
        char bssid_str[24] = "N/A";
        char ssid_str[33] = CONFIG_LOCAL_CTRL_WIFI_SSID;
        int channel = 0;
        int rssi = 0;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            snprintf(ssid_str, sizeof(ssid_str), "%s", (char *)ap_info.ssid);
            snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     ap_info.bssid[0], ap_info.bssid[1], ap_info.bssid[2],
                     ap_info.bssid[3], ap_info.bssid[4], ap_info.bssid[5]);
            channel = ap_info.primary;
            rssi = ap_info.rssi;
        }

        ESP_LOGI(TAG, "==========================================================");
        ESP_LOGI(TAG, "==> [Wi-Fi] KẾT NỐI THÀNH CÔNG! ĐÃ CÓ ĐỊA CHỈ IP:        ");
        ESP_LOGI(TAG, "  - Tên Wi-Fi (SSID) : %s", ssid_str);
        ESP_LOGI(TAG, "  - BSSID (MAC AP)   : %s (Kênh %d, Sóng %d dBm)", bssid_str, channel, rssi);
        ESP_LOGI(TAG, "  - Địa chỉ IP cấp   : " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "  - Địa chỉ Mask     : " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "  - Địa chỉ Gateway  : " IPSTR, IP2STR(&event->ip_info.gw));
        ESP_LOGI(TAG, "==========================================================");
        s_retry_num = 0;
        app_driver_set_wifi_status(WIFI_STATUS_CONNECTED);
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

    /* Cấu hình quốc gia Việt Nam (kênh 1 - 13) */
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

static void wifi_station_start(void)
{
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_LOCAL_CTRL_WIFI_SSID,
            .password = CONFIG_LOCAL_CTRL_WIFI_PASSWORD,
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .threshold.rssi = -127,
            .pmf_cfg = {
                .capable = false,
                .required = false
            },
            .disable_wpa3_compatible_mode = 1,
            .failure_retry_cnt = 3,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Tối ưu hóa băng thông HT20, tắt Modem Sleep & hạ TX Power 12dBm cho ESP32-C3 SuperMini */
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(48); // 12 dBm

    ESP_LOGI(TAG, "wifi_station_start hoàn tất. Đang chờ đồng bộ kết nối...");
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Đã sẵn sàng mạng LAN cho Local Control Server!");
    } else {
        ESP_LOGW(TAG, "Wi-Fi kết nối thất bại! Thiết bị vẫn có thể nhận điều khiển qua BLE.");
    }
}

/* =========================================================================
 * 2. LOCAL CONTROL SERVER QUA WI-FI + HTTPS + MDNS (MỤC 8.5.1 & 8.5.2)
 * ========================================================================= */

#define PROPERTY_NAME_STATUS "status"
static char s_light_status_json[64] = "{\"status\": true}";

enum property_types {
    PROP_TYPE_TIMESTAMP = 0,
    PROP_TYPE_INT32,
    PROP_TYPE_BOOLEAN,
    PROP_TYPE_STRING,
};

static esp_err_t get_property_values(size_t props_count,
                                     const esp_local_ctrl_prop_t props[],
                                     esp_local_ctrl_prop_val_t prop_values[],
                                     void *usr_ctx)
{
    for (size_t i = 0; i < props_count; i++) {
        ESP_LOGI(TAG, "==> [HTTPS Local Ctrl] Yêu cầu đọc thuộc tính [%d/%d]: '%s'",
                 (int)(i + 1), (int)props_count, props[i].name);
        if (strncmp(PROPERTY_NAME_STATUS, props[i].name, strlen(PROPERTY_NAME_STATUS)) == 0) {
            bool state = app_driver_get_state();
            snprintf(s_light_status_json, sizeof(s_light_status_json), "{\"status\": %s}", state ? "true" : "false");
            prop_values[i].size = strlen(s_light_status_json);
            prop_values[i].data = s_light_status_json;
            prop_values[i].free_fn = NULL;
            ESP_LOGI(TAG, "    Giá trị phản hồi: %s", s_light_status_json);
        } else {
            ESP_LOGW(TAG, "    Không tìm thấy thuộc tính '%s'!", props[i].name);
            prop_values[i].size = 0;
            prop_values[i].data = NULL;
            prop_values[i].free_fn = NULL;
        }
    }
    return ESP_OK;
}

static esp_err_t set_property_values(size_t props_count,
                                     const esp_local_ctrl_prop_t props[],
                                     const esp_local_ctrl_prop_val_t prop_values[],
                                     void *usr_ctx)
{
    for (size_t i = 0; i < props_count; i++) {
        ESP_LOGI(TAG, "==> [HTTPS Local Ctrl] Yêu cầu ghi thuộc tính [%d/%d]: '%s' (Kích thước: %d bytes)",
                 (int)(i + 1), (int)props_count, props[i].name, (int)prop_values[i].size);
        if (strncmp(PROPERTY_NAME_STATUS, props[i].name, strlen(PROPERTY_NAME_STATUS)) == 0) {
            char val_buf[64] = {0};
            size_t copy_len = prop_values[i].size < sizeof(val_buf) - 1 ? prop_values[i].size : sizeof(val_buf) - 1;
            if (copy_len > 0 && prop_values[i].data != NULL) {
                memcpy(val_buf, prop_values[i].data, copy_len);
            }
            val_buf[copy_len] = '\0';

            ESP_LOGI(TAG, "    Dữ liệu nhận được từ Client: '%s' (Byte 0: 0x%02X)",
                     val_buf, (uint8_t)val_buf[0]);

            bool new_state = false;
            if ((copy_len == 1 && ((uint8_t)val_buf[0] == 0x01 || val_buf[0] == '1')) ||
                strstr(val_buf, "true") != NULL ||
                strstr(val_buf, "\"status\": true") != NULL) {
                new_state = true;
            } else if ((copy_len == 1 && ((uint8_t)val_buf[0] == 0x00 || val_buf[0] == '0')) ||
                       strstr(val_buf, "false") != NULL ||
                       strstr(val_buf, "\"status\": false") != NULL) {
                new_state = false;
            } else {
                ESP_LOGW(TAG, "Không nhận diện được giá trị! val_buf='%s'", val_buf);
                return ESP_ERR_INVALID_ARG;
            }

            app_driver_set_state(new_state);
            snprintf(s_light_status_json, sizeof(s_light_status_json), "{\"status\": %s}", new_state ? "true" : "false");
            if (new_state) {
                app_driver_pulse_feedback(0, 255, 0);
            }

            ESP_LOGI(TAG, "==> [HTTPS Local Ctrl] Cập nhật đèn thành công: %s", new_state ? "BẬT (ON)" : "TẮT (OFF)");
        }
    }
    return ESP_OK;
}

static void esp_local_ctrl_service_start(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  Khởi động Local Control HTTPS Server & mDNS Discovery  ");
    ESP_LOGI(TAG, "==========================================================");

    /* 1. Khởi tạo HTTPS Server SSL Config và nạp chứng chỉ */
    httpd_ssl_config_t https_conf = HTTPD_SSL_CONFIG_DEFAULT();

    extern const unsigned char cacert_pem_start[] asm("_binary_cacert_pem_start");
    extern const unsigned char cacert_pem_end[]   asm("_binary_cacert_pem_end");
    https_conf.servercert = cacert_pem_start;
    https_conf.servercert_len = cacert_pem_end - cacert_pem_start;

    extern const unsigned char prvtkey_pem_start[] asm("_binary_prvtkey_pem_start");
    extern const unsigned char prvtkey_pem_end[]   asm("_binary_prvtkey_pem_end");
    https_conf.prvtkey_pem = prvtkey_pem_start;
    https_conf.prvtkey_len = prvtkey_pem_end - prvtkey_pem_start;

    ESP_LOGI(TAG, "Đã nạp chứng chỉ SSL Server (%d bytes) và Private Key (%d bytes)",
             (int)https_conf.servercert_len, (int)https_conf.prvtkey_len);

    /* 2. Cấu hình Dịch vụ esp_local_ctrl */
    esp_local_ctrl_config_t config = {
        .transport = ESP_LOCAL_CTRL_TRANSPORT_HTTPD,
        .transport_config = {
            .httpd = &https_conf
        },
#ifndef PROTOCOM_SEC0
#ifdef ESP_LOCAL_CTRL_PROTO_SEC0
#define PROTOCOM_SEC0 ESP_LOCAL_CTRL_PROTO_SEC0
#endif
#endif
        .proto_sec = {
            .version = PROTOCOM_SEC0,
            .custom_handle = NULL,
        },
        .handlers = {
            .get_prop_values = get_property_values,
            .set_prop_values = set_property_values,
            .usr_ctx         = NULL,
            .usr_ctx_free_fn = NULL
        },
        .max_properties = 10
    };

    /* 3. Khởi tạo mDNS Service Discovery */
    ESP_LOGI(TAG, "Khởi tạo mDNS hostname: %s.local", CONFIG_LOCAL_CTRL_MDNS_HOST_NAME);
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(CONFIG_LOCAL_CTRL_MDNS_HOST_NAME));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32 Smart Light Local Control"));
    ESP_ERROR_CHECK(mdns_service_add("ESP-Local-Control", "_esp_local_ctrl", "_tcp", 443, NULL, 0));

    /* 4. Khởi động dịch vụ điều khiển cục bộ */
    ESP_ERROR_CHECK(esp_local_ctrl_start(&config));
    ESP_LOGI(TAG, "esp_local_ctrl service đã chạy trên HTTPS port 443!");

    /* 5. Đăng ký thuộc tính điều khiển đèn 'status' */
    esp_local_ctrl_prop_t status = {
        .name        = PROPERTY_NAME_STATUS,
        .type        = PROP_TYPE_STRING,
        .size        = 0,
        .flags       = 0,
        .ctx         = NULL,
        .ctx_free_fn = NULL
    };
    ESP_ERROR_CHECK(esp_local_ctrl_add_property(&status));
    ESP_LOGI(TAG, "Đã đăng ký thuộc tính điều khiển cục bộ: '%s' (JSON format)", PROPERTY_NAME_STATUS);
    ESP_LOGI(TAG, "Đường dẫn Endpoint nội bộ: https://%s.local/esp_local_ctrl/control", CONFIG_LOCAL_CTRL_MDNS_HOST_NAME);
    ESP_LOGI(TAG, "==========================================================");
}

/* =========================================================================
 * 3. FALLBACK LOCAL CONTROL SERVER QUA BLUETOOTH LE (MỤC 8.5.3)
 * ========================================================================= */

#if CONFIG_BT_ENABLED

#define BLE_TAG "ble_local_ctrl"
#define GATTS_SERVICE_UUID_LIGHT    0x00FF
#define GATTS_CHAR_UUID_WRITE_LIGHT 0x0001
#define GATTS_CHAR_UUID_READ_LIGHT  0x0002
#define GATTS_NUM_HANDLE_LIGHT      6

static uint16_t s_light_service_handle = 0;
static uint16_t s_char_write_handle = 0;
static uint16_t s_char_read_handle = 0;

static uint8_t s_adv_service_uuid128[16] = {
    /* LSB <---------------------------------------------------> MSB */
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
};

static esp_ble_adv_data_t s_ble_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0020,
    .max_interval = 0x0040,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(s_adv_service_uuid128),
    .p_service_uuid = s_adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t s_ble_adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&s_ble_adv_params);
        ESP_LOGI(BLE_TAG, "==> [BLE GAP] Bắt đầu phát quảng bá BLE thiết bị: %s", CONFIG_LOCAL_CTRL_BLE_DEVICE_NAME);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(BLE_TAG, "Phát quảng bá BLE thất bại!");
        }
        break;
    default:
        break;
    }
}

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT: {
        ESP_LOGI(BLE_TAG, "==> [BLE GATTS] Đăng ký GATT Profile thành công.");
        esp_ble_gap_set_device_name(CONFIG_LOCAL_CTRL_BLE_DEVICE_NAME);
        esp_ble_gap_config_adv_data(&s_ble_adv_data);

        esp_gatt_srvc_id_t service_id = {
            .is_primary = true,
            .id = {
                .inst_id = 0x00,
                .uuid = {
                    .len = ESP_UUID_LEN_16,
                    .uuid = { .uuid16 = GATTS_SERVICE_UUID_LIGHT },
                },
            },
        };
        esp_ble_gatts_create_service(gatts_if, &service_id, GATTS_NUM_HANDLE_LIGHT);
        break;
    }

    case ESP_GATTS_CREATE_EVT: {
        s_light_service_handle = param->create.service_handle;
        ESP_LOGI(BLE_TAG, "==> [BLE GATTS] Service tạo thành công (Handle: %d). Khởi động service...", s_light_service_handle);
        esp_ble_gatts_start_service(s_light_service_handle);

        /* Thêm Characteristic 1: Ghi lệnh Bật/Tắt (UUID 0x0001, Write) */
        esp_bt_uuid_t char_write_uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid = { .uuid16 = GATTS_CHAR_UUID_WRITE_LIGHT },
        };
        esp_ble_gatts_add_char(s_light_service_handle, &char_write_uuid,
                               ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_WRITE,
                               NULL, NULL);

        /* Thêm Characteristic 2: Đọc trạng thái (UUID 0x0002, Read) */
        esp_bt_uuid_t char_read_uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid = { .uuid16 = GATTS_CHAR_UUID_READ_LIGHT },
        };
        esp_ble_gatts_add_char(s_light_service_handle, &char_read_uuid,
                               ESP_GATT_PERM_READ,
                               ESP_GATT_CHAR_PROP_BIT_READ,
                               NULL, NULL);
        break;
    }

    case ESP_GATTS_ADD_CHAR_EVT: {
        if (param->add_char.char_uuid.uuid.uuid16 == GATTS_CHAR_UUID_WRITE_LIGHT) {
            s_char_write_handle = param->add_char.attr_handle;
            ESP_LOGI(BLE_TAG, "Đã thêm Characteristic Ghi (UUID 0x%04X, Handle %d)",
                     GATTS_CHAR_UUID_WRITE_LIGHT, s_char_write_handle);
        } else if (param->add_char.char_uuid.uuid.uuid16 == GATTS_CHAR_UUID_READ_LIGHT) {
            s_char_read_handle = param->add_char.attr_handle;
            ESP_LOGI(BLE_TAG, "Đã thêm Characteristic Đọc (UUID 0x%04X, Handle %d)",
                     GATTS_CHAR_UUID_READ_LIGHT, s_char_read_handle);
        }
        break;
    }

    case ESP_GATTS_READ_EVT: {
        ESP_LOGI(BLE_TAG, "==> [BLE GATTS] Nhận yêu cầu Đọc trạng thái đèn từ Smartphone (Handle: %d)",
                 param->read.handle);
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
        rsp.attr_value.handle = param->read.handle;
        rsp.attr_value.offset = param->read.offset;
        if (param->read.offset >= 1) {
            rsp.attr_value.len = 0;
        } else {
            rsp.attr_value.len = 1;
            rsp.attr_value.value[0] = app_driver_get_state() ? 0x01 : 0x00;
        }

        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_OK, &rsp);
        break;
    }

    case ESP_GATTS_WRITE_EVT: {
        ESP_LOGI(BLE_TAG, "==> [BLE GATTS] Nhận lệnh Ghi từ Smartphone (Handle: %d, Len: %d)",
                 param->write.handle, param->write.len);
        if (param->write.handle == s_char_write_handle && param->write.len > 0) {
            uint8_t val = param->write.value[0];
            bool new_state = (val == 0x01 || val == '1');
            ESP_LOGI(BLE_TAG, "    Giá trị ghi nhận: 0x%02X -> Đèn %s", val, new_state ? "BẬT" : "TẮT");
            app_driver_set_state(new_state);
            if (new_state) {
                app_driver_pulse_feedback(0, 255, 0);
            }
        }
        if (param->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id,
                                        ESP_GATT_OK, NULL);
        }
        break;
    }

    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(BLE_TAG, "==> [BLE GATTS] Smartphone đã kết nối BLE thành công (Conn ID: %d)", param->connect.conn_id);
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(BLE_TAG, "==> [BLE GATTS] Smartphone đã ngắt kết nối BLE. Khởi động lại phát quảng bá...");
        esp_ble_gap_start_advertising(&s_ble_adv_params);
        break;

    default:
        break;
    }
}

static void ble_local_ctrl_init(void)
{
    ESP_LOGI(BLE_TAG, "==========================================================");
    ESP_LOGI(BLE_TAG, "  Khởi tạo Fallback BLE GATT Local Control (Mục 8.5.3)    ");
    ESP_LOGI(BLE_TAG, "==========================================================");

#if CONFIG_IDF_TARGET_ESP32
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
#endif

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_profile_event_handler));
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(0));

    ESP_LOGI(BLE_TAG, "BLE GATT Server khởi tạo thành công (Device Name: %s)", CONFIG_LOCAL_CTRL_BLE_DEVICE_NAME);
}

#endif /* CONFIG_BT_ENABLED */

/* =========================================================================
 * 4. HÀM MAIN CHÍNH
 * ========================================================================= */

void app_main(void)
{
    /* Tối ưu hóa mức độ log (Observability & Signal-to-Noise Ratio):
     * Ẩn các log debug/thủ tục nội bộ từ Wi-Fi PHY và API driver để làm sạch màn hình terminal */
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("light_driver", ESP_LOG_WARN);

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "   PBL5 Smart Light - Chương 8: Điều Khiển Cục Bộ (8.5)   ");
    ESP_LOGI(TAG, "   Hỗ trợ đa mục tiêu: ESP32-S3 & ESP32-C3                ");
    ESP_LOGI(TAG, "   LED WS2812B GPIO 4 SPI DMA | Boot Button Gestures      ");
    ESP_LOGI(TAG, "==========================================================");

    /* 1. Khởi tạo NVS Flash */
    ESP_LOGI(TAG, "[1/5] Khởi tạo NVS Flash Storage...");
    app_storage_init();

    /* 2. Khởi tạo Driver Đèn WS2812B và Nút bấm Boot */
    ESP_LOGI(TAG, "[2/5] Khởi tạo Hardware Driver (WS2812B & Button HAL)...");
    app_driver_init();

    /* 3. Khởi tạo và kết nối Wi-Fi Station */
    ESP_LOGI(TAG, "[3/5] Khởi tạo Wi-Fi Station...");
    wifi_initialize();
    wifi_station_start();

    /* 4. Khởi động Local Control Server qua Wi-Fi + HTTPS + mDNS (Mục 8.5.1) */
    ESP_LOGI(TAG, "[4/5] Khởi động Local Control HTTPS Server & mDNS...");
    esp_local_ctrl_service_start();

#if CONFIG_BT_ENABLED
    /* 5. Khởi động Fallback Local Control Server qua BLE GATT (Mục 8.5.3) */
    ESP_LOGI(TAG, "[5/5] Khởi động Fallback Local Control Server qua BLE...");
    ble_local_ctrl_init();
#endif

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  HỆ THỐNG ĐIỀU KHIỂN CỤC BỘ ĐÃ HOẠT ĐỘNG HOÀN HẢO!       ");
    ESP_LOGI(TAG, "  - Kênh Wi-Fi HTTPS: https://%s.local/esp_local_ctrl/control", CONFIG_LOCAL_CTRL_MDNS_HOST_NAME);
    ESP_LOGI(TAG, "  - Kênh BLE GATT   : %s (Service 0x00FF, Write 0x0001)  ", CONFIG_LOCAL_CTRL_BLE_DEVICE_NAME);
    ESP_LOGI(TAG, "==========================================================");

    int count = 0;
    while (1) {
        ESP_LOGI(TAG, "[Heartbeat #%02d] Light Status: %s | Free Heap: %lu bytes",
                 ++count,
                 app_driver_get_state() ? "ON" : "OFF",
                 (unsigned long)esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
