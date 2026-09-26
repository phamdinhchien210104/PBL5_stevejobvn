/*
 * ESP32 Smart Light Example - Chapter 8.5.3: Bluetooth GATT Server Local Control
 *
 * Biến ESP32 thành Bluetooth LE GATT Server để điều khiển cục bộ không cần Router Wi-Fi:
 * - Tên thiết bị quảng bá (Advertising Name): ESP32C3-LIGHT (hoặc ESP32S3-LIGHT)
 * - Dịch vụ Đọc trạng thái đèn (Read Service 0x00FF -> Characteristic 0xFF01)
 * - Dịch vụ Ghi lệnh điều khiển đèn (Write Service 0x00EE -> Characteristic 0xEE01)
 *   + Ghi 00 (0x00 hoặc '0') -> Tắt đèn (app_driver: Light OFF)
 *   + Ghi 01 (0x01 hoặc '1') -> Bật đèn (app_driver: Light ON)
 *   + Ghi 0B (11)             -> Đổi màu đèn tiếp theo (8 màu RGB)
 *   + Ghi 2B ('+')            -> Tăng độ sáng +20%
 *   + Ghi 2D ('-')            -> Giảm độ sáng -20%
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_log_buffer.h"
#include "nvs_flash.h"
#include "esp_bt.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

#include "sdkconfig.h"
#include "app_priv.h"
#include "app_storage.h"

#define TAG "GATTS_DEMO"

#if CONFIG_IDF_TARGET_ESP32S3
#define TEST_DEVICE_NAME            "ESP32S3-LIGHT"
#else
#define TEST_DEVICE_NAME            "ESP32C3-LIGHT"
#endif

/* Bảng dịch vụ GATT theo đúng chuẩn Mục 8.5.3 */
#define GATTS_SERVICE_UUID_READ_STATUS    0x00FF
#define GATTS_CHAR_UUID_READ_STATUS       0xFF01
#define GATTS_NUM_HANDLE_READ             4

#define GATTS_SERVICE_UUID_WRITE_STATUS   0x00EE
#define GATTS_CHAR_UUID_WRITE_STATUS      0xEE01
#define GATTS_NUM_HANDLE_WRITE            4

#define PROFILE_NUM                       2
#define PROFILE_A_APP_ID                  0  /* Profile A: Đọc trạng thái (UUID 0x00FF / 0xFF01) */
#define PROFILE_B_APP_ID                  1  /* Profile B: Ghi lệnh điều khiển (UUID 0x00EE / 0xEE01) */

struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
static void gatts_profile_b_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

static struct gatts_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gatts_cb = gatts_profile_a_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,
    },
    [PROFILE_B_APP_ID] = {
        .gatts_cb = gatts_profile_b_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,
    },
};

static uint8_t adv_config_done = 0;
#define adv_config_flag      (1 << 0)
#define scan_rsp_config_flag (1 << 1)

/* 128-bit Service UUID của Service 0x00EE (Write Control):
 * UUID 16-bit: 0x00EE -> Base UUID 128-bit chuẩn Bluetooth:
 * 000000ee-0000-1000-8000-00805f9b34fb (Little Endian)
 */
static uint8_t s_adv_service_uuid128[16] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0xEE, 0x00, 0x00, 0x00,
};

/* Cấu hình dữ liệu phát quảng bá (Advertising Data):
 * Chứa Flags (3 bytes) + Tên thiết bị (15 bytes) = 18 bytes <= 31 bytes
 * service_uuid_len = 0 để đảm bảo (service_uuid_len & 0xf == 0)
 */
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp        = false,
    .include_name        = true,
    .include_txpower     = false,
    .min_interval        = 0x0020, /* 20ms */
    .max_interval        = 0x0040, /* 40ms */
    .appearance          = 0x00,
    .manufacturer_len    = 0,
    .p_manufacturer_data = NULL,
    .service_data_len    = 0,
    .p_service_data      = NULL,
    .service_uuid_len    = 0,
    .p_service_uuid      = NULL,
    .flag                = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

/* Cấu hình dữ liệu phản hồi quét (Scan Response Data):
 * Chứa Service UUID 128-bit (16 bytes: 16 & 0xf == 0) + TX Power (3 bytes) = 21 bytes <= 31 bytes
 */
static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp        = true,
    .include_name        = false,
    .include_txpower     = true,
    .appearance          = 0x00,
    .manufacturer_len    = 0,
    .p_manufacturer_data = NULL,
    .service_data_len    = 0,
    .p_service_data      = NULL,
    .service_uuid_len    = sizeof(s_adv_service_uuid128),
    .p_service_uuid      = s_adv_service_uuid128,
    .flag                = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t adv_params = {
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
        adv_config_done &= (~adv_config_flag);
        if (adv_config_done == 0) {
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;

    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        adv_config_done &= (~scan_rsp_config_flag);
        if (adv_config_done == 0) {
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Khởi động phát sóng quảng bá BLE (Advertising) thất bại!");
        } else {
            ESP_LOGI(TAG, "Đang phát sóng quảng bá BLE: Tên '%s' (Chờ smartphone kết nối...)", TEST_DEVICE_NAME);
        }
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Dừng phát sóng quảng bá BLE thất bại!");
        }
        break;

    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(TAG, "Cập nhật thông số kết nối: min_int=%d, max_int=%d, latency=%d, timeout=%d",
                 param->update_conn_params.min_int,
                 param->update_conn_params.max_int,
                 param->update_conn_params.latency,
                 param->update_conn_params.timeout);
        break;

    default:
        break;
    }
}

/* =========================================================================
 * PROFILE A: DỊCH VỤ ĐỌC TRẠNG THÁI ĐÈN (SERVICE 0x00FF / CHAR 0xFF01)
 * ========================================================================= */
static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT: {
        ESP_LOGI(TAG, "[Profile A] Đăng ký App ID %d (Service 0x%04X Đọc trạng thái)",
                 param->reg.app_id, GATTS_SERVICE_UUID_READ_STATUS);

        gl_profile_tab[PROFILE_A_APP_ID].service_id.is_primary = true;
        gl_profile_tab[PROFILE_A_APP_ID].service_id.id.inst_id = 0x00;
        gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID_READ_STATUS;

        ESP_ERROR_CHECK(esp_ble_gap_set_device_name(TEST_DEVICE_NAME));
        ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&adv_data));
        adv_config_done |= adv_config_flag;

        ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&scan_rsp_data));
        adv_config_done |= scan_rsp_config_flag;

        esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_A_APP_ID].service_id, GATTS_NUM_HANDLE_READ);
        break;
    }

    case ESP_GATTS_CREATE_EVT: {
        ESP_LOGI(TAG, "[Profile A] Dịch vụ 0x%04X đã tạo (Service Handle: %d)",
                 GATTS_SERVICE_UUID_READ_STATUS, param->create.service_handle);

        gl_profile_tab[PROFILE_A_APP_ID].service_handle = param->create.service_handle;
        gl_profile_tab[PROFILE_A_APP_ID].char_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_A_APP_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID_READ_STATUS;

        esp_ble_gatts_start_service(gl_profile_tab[PROFILE_A_APP_ID].service_handle);

        esp_gatt_char_prop_t prop = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
        esp_err_t ret = esp_ble_gatts_add_char(gl_profile_tab[PROFILE_A_APP_ID].service_handle,
                                               &gl_profile_tab[PROFILE_A_APP_ID].char_uuid,
                                               ESP_GATT_PERM_READ,
                                               prop,
                                               NULL, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[Profile A] Thêm Characteristic 0x%04X thất bại: %s",
                     GATTS_CHAR_UUID_READ_STATUS, esp_err_to_name(ret));
        }
        break;
    }

    case ESP_GATTS_ADD_CHAR_EVT: {
        ESP_LOGI(TAG, "[Profile A] Thêm Characteristic 0x%04X thành công (Char Handle: %d)",
                 GATTS_CHAR_UUID_READ_STATUS, param->add_char.attr_handle);
        gl_profile_tab[PROFILE_A_APP_ID].char_handle = param->add_char.attr_handle;
        gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
        esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_A_APP_ID].service_handle,
                                     &gl_profile_tab[PROFILE_A_APP_ID].descr_uuid,
                                     ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                     NULL, NULL);
        break;
    }

    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        gl_profile_tab[PROFILE_A_APP_ID].descr_handle = param->add_char_descr.attr_handle;
        ESP_LOGI(TAG, "[Profile A] Thêm Descriptor CCCD thành công (Descr Handle: %d)", param->add_char_descr.attr_handle);
        break;

    case ESP_GATTS_READ_EVT: {
        ESP_LOGI(TAG, "GATT_READ_EVT, conn_id %d, trans_id %" PRIu32 ", handle %d",
                 param->read.conn_id, param->read.trans_id, param->read.handle);
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
        rsp.attr_value.handle = param->read.handle;
        rsp.attr_value.len = 1;
        rsp.attr_value.value[0] = app_driver_get_state() ? 0x01 : 0x00;
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_OK, &rsp);
        ESP_LOGI(TAG, "==> [BLE Read] Trả về trạng thái đèn: 0x%02X (%s)",
                 rsp.attr_value.value[0], rsp.attr_value.value[0] ? "BẬT" : "TẮT");
        break;
    }

    case ESP_GATTS_CONNECT_EVT: {
        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        conn_params.latency = 0;
        conn_params.max_int = 0x20;    /* 40ms */
        conn_params.min_int = 0x10;    /* 20ms */
        conn_params.timeout = 400;     /* 4000ms */
        ESP_LOGI(TAG, "ESP_GATTS_CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x",
                 param->connect.conn_id,
                 param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
                 param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
        gl_profile_tab[PROFILE_A_APP_ID].conn_id = param->connect.conn_id;
        esp_ble_gap_update_conn_params(&conn_params);
        break;
    }

    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TAG, "ESP_GATTS_DISCONNECT_EVT, disconnect reason 0x%x", param->disconnect.reason);
        esp_ble_gap_start_advertising(&adv_params);
        break;

    default:
        break;
    }
}

/* =========================================================================
 * PROFILE B: DỊCH VỤ GHI LỆNH ĐIỀU KHIỂN (SERVICE 0x00EE / CHAR 0xEE01)
 * ========================================================================= */
static void gatts_profile_b_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT: {
        ESP_LOGI(TAG, "[Profile B] Đăng ký App ID %d (Service 0x%04X Ghi lệnh điều khiển)",
                 param->reg.app_id, GATTS_SERVICE_UUID_WRITE_STATUS);

        gl_profile_tab[PROFILE_B_APP_ID].service_id.is_primary = true;
        gl_profile_tab[PROFILE_B_APP_ID].service_id.id.inst_id = 0x00;
        gl_profile_tab[PROFILE_B_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_B_APP_ID].service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID_WRITE_STATUS;

        esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_B_APP_ID].service_id, GATTS_NUM_HANDLE_WRITE);
        break;
    }

    case ESP_GATTS_CREATE_EVT: {
        ESP_LOGI(TAG, "[Profile B] Dịch vụ 0x%04X đã tạo (Service Handle: %d)",
                 GATTS_SERVICE_UUID_WRITE_STATUS, param->create.service_handle);

        gl_profile_tab[PROFILE_B_APP_ID].service_handle = param->create.service_handle;
        gl_profile_tab[PROFILE_B_APP_ID].char_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_B_APP_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID_WRITE_STATUS;

        esp_ble_gatts_start_service(gl_profile_tab[PROFILE_B_APP_ID].service_handle);

        esp_gatt_char_prop_t prop = ESP_GATT_CHAR_PROP_BIT_WRITE |
                                    ESP_GATT_CHAR_PROP_BIT_WRITE_NR |
                                    ESP_GATT_CHAR_PROP_BIT_READ;
        esp_err_t ret = esp_ble_gatts_add_char(gl_profile_tab[PROFILE_B_APP_ID].service_handle,
                                               &gl_profile_tab[PROFILE_B_APP_ID].char_uuid,
                                               ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                               prop,
                                               NULL, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[Profile B] Thêm Characteristic 0x%04X thất bại: %s",
                     GATTS_CHAR_UUID_WRITE_STATUS, esp_err_to_name(ret));
        }
        break;
    }

    case ESP_GATTS_ADD_CHAR_EVT: {
        ESP_LOGI(TAG, "[Profile B] Thêm Characteristic 0x%04X thành công (Char Handle: %d)",
                 GATTS_CHAR_UUID_WRITE_STATUS, param->add_char.attr_handle);
        gl_profile_tab[PROFILE_B_APP_ID].char_handle = param->add_char.attr_handle;
        gl_profile_tab[PROFILE_B_APP_ID].descr_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_B_APP_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
        esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_B_APP_ID].service_handle,
                                     &gl_profile_tab[PROFILE_B_APP_ID].descr_uuid,
                                     ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                     NULL, NULL);
        break;
    }

    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        gl_profile_tab[PROFILE_B_APP_ID].descr_handle = param->add_char_descr.attr_handle;
        ESP_LOGI(TAG, "[Profile B] Thêm Descriptor CCCD thành công (Descr Handle: %d)", param->add_char_descr.attr_handle);
        break;

    case ESP_GATTS_READ_EVT: {
        ESP_LOGI(TAG, "GATT_READ_EVT, conn_id %d, trans_id %" PRIu32 ", handle %d",
                 param->read.conn_id, param->read.trans_id, param->read.handle);
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
        rsp.attr_value.handle = param->read.handle;
        rsp.attr_value.len = 1;
        rsp.attr_value.value[0] = app_driver_get_state() ? 0x01 : 0x00;
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_OK, &rsp);
        break;
    }

    case ESP_GATTS_WRITE_EVT: {
        ESP_LOGI(TAG, "GATT_WRITE_EVT, conn_id %d, trans_id %" PRIu32 ", handle %d",
                 param->write.conn_id, param->write.trans_id, param->write.handle);
        ESP_LOGI(TAG, "GATT_WRITE_EVT, value len %d, value :", param->write.len);
        ESP_LOG_BUFFER_HEX(TAG, param->write.value, param->write.len);

        if (param->write.len > 0) {
            uint8_t val = param->write.value[0];
            /* Theo yêu cầu Mục 8.5.3:
             * - Gửi 00 -> app_driver: Light OFF và tắt đèn
             * - Gửi 01 -> app_driver: Light ON và bật đèn
             * - Đồng thời hỗ trợ tính năng mở rộng: 11 (đổi màu), '+' (tăng sáng), '-' (giảm sáng)
             */
            if (val == 0x00 || val == '0') {
                app_driver_set_state(false);
            } else if (val == 0x01 || val == '1') {
                app_driver_set_state(true);
            } else if (val == 11 || val == 0x0B || (param->write.len >= 2 && param->write.value[0] == '1' && param->write.value[1] == '1')) {
                app_driver_next_color();
                ESP_LOGI(TAG, "==> [BLE Write] Đổi màu đèn tiếp theo -> %s", app_driver_get_color_name());
            } else if (val == '+') {
                app_driver_adjust_brightness(+20);
                ESP_LOGI(TAG, "==> [BLE Write] Tăng độ sáng (+20%%) -> %d%%", app_driver_get_brightness());
            } else if (val == '-') {
                app_driver_adjust_brightness(-20);
                ESP_LOGI(TAG, "==> [BLE Write] Giảm độ sáng (-20%%) -> %d%%", app_driver_get_brightness());
            } else {
                ESP_LOGW(TAG, "==> [BLE Write] Lệnh không xác định: 0x%02X", val);
            }
        }

        if (param->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        }
        break;
    }

    case ESP_GATTS_CONNECT_EVT:
        gl_profile_tab[PROFILE_B_APP_ID].conn_id = param->connect.conn_id;
        break;

    default:
        break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gatts_if = gatts_if;
        } else {
            ESP_LOGE(TAG, "Đăng ký App ID %04x thất bại, status %d", param->reg.app_id, param->reg.status);
            return;
        }
    }

    for (int idx = 0; idx < PROFILE_NUM; idx++) {
        if (gatts_if == ESP_GATT_IF_NONE || gatts_if == gl_profile_tab[idx].gatts_if) {
            if (gl_profile_tab[idx].gatts_cb) {
                gl_profile_tab[idx].gatts_cb(event, gatts_if, param);
            }
        }
    }
}

static void app_ble_init(void)
{
    esp_err_t ret;

#if CONFIG_IDF_TARGET_ESP32
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
#endif

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khởi tạo BT Controller thất bại: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Kích hoạt BLE Controller thất bại: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khởi tạo Bluedroid stack thất bại: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Kích hoạt Bluedroid stack thất bại: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Đăng ký GATTS Callback thất bại: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Đăng ký GAP Callback thất bại: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_ble_gatts_app_register(PROFILE_A_APP_ID);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Đăng ký Profile A thất bại: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_ble_gatts_app_register(PROFILE_B_APP_ID);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Đăng ký Profile B thất bại: %s", esp_err_to_name(ret));
        return;
    }

    esp_ble_gatt_set_local_mtu(500);
}

void app_main(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "   PBL5 Smart Light - Mục 8.5.3: BLE GATT Local Control   ");
    ESP_LOGI(TAG, "   Bluetooth Device Name: %s                     ", TEST_DEVICE_NAME);
    ESP_LOGI(TAG, "   - Service Đọc (Read) : UUID 0x%04X (Char 0x%04X)       ", GATTS_SERVICE_UUID_READ_STATUS, GATTS_CHAR_UUID_READ_STATUS);
    ESP_LOGI(TAG, "   - Service Ghi (Write): UUID 0x%04X (Char 0x%04X)       ", GATTS_SERVICE_UUID_WRITE_STATUS, GATTS_CHAR_UUID_WRITE_STATUS);
    ESP_LOGI(TAG, "   LED WS2812B GPIO 4 SPI DMA | Boot Button Gestures      ");
    ESP_LOGI(TAG, "==========================================================");

    /* 1. Khởi tạo NVS Flash */
    ESP_LOGI(TAG, "[1/3] Khởi tạo NVS Storage...");
    app_storage_init();

    /* 2. Khởi tạo Hardware Driver (WS2812B & Button) */
    ESP_LOGI(TAG, "[2/3] Khởi tạo Hardware Driver...");
    app_driver_init();

    /* 3. Khởi tạo Bluetooth LE GATT Server */
    ESP_LOGI(TAG, "[3/3] Khởi tạo Bluetooth Controller & GATT Server...");
    app_ble_init();

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  BLUETOOTH GATT LOCAL CONTROL SERVER ĐÃ SẴN SÀNG!        ");
    ESP_LOGI(TAG, "  Mở nRF Connect trên iPhone/Android:                     ");
    ESP_LOGI(TAG, "  1. Quét tìm và kết nối tới '%s'                ", TEST_DEVICE_NAME);
    ESP_LOGI(TAG, "  2. Chọn Service 0x00EE -> Characteristic 0xEE01        ");
    ESP_LOGI(TAG, "  3. Gửi byte 00 (TẮT) hoặc byte 01 (BẬT)                ");
    ESP_LOGI(TAG, "==========================================================");

    int count = 0;
    while (1) {
        ESP_LOGI(TAG, "[Heartbeat #%02d] Light: %s (%d%%) | Free Heap: %lu bytes",
                 ++count,
                 app_driver_get_state() ? "ON" : "OFF",
                 (int)app_driver_get_brightness(),
                 (unsigned long)esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
