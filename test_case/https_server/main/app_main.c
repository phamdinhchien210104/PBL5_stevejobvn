/*
 * ESP32 Smart Light Example - Chapter 8.3 & 8.4: HTTP & HTTPS Web Server
 *
 * Modernized for ESP-IDF v6.0.2 & GCC 15
 * Features:
 * - Dual HTTP (Port 80) and HTTPS (Port 443 TLS) support
 * - REST API Endpoints:
 *     GET  /light : Returns JSON with light status, brightness, RGB color
 *     POST /light : Updates light status, brightness, color, or triggers gestures
 * - Interactive Web Dashboard at GET / (HTML5 + CSS Glassmorphism + Live JS Control)
 * - Dual-Target Support (ESP32-S3 GPIO 0 / ESP32-C3 GPIO 9, WS2812B GPIO 4)
 * - Robust Wi-Fi Station Engine with WPA2/WPA3 Personal, PMF, VN Country Code, HT20, Power Tuning
 */

#include <stdio.h>
#include <stdlib.h>
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
#include "esp_https_server.h"
#include "esp_http_server.h"

#include DEVELOPMENT_BOARD

#define TAG "https_server"

/* Cấu hình Wi-Fi dự phòng nếu chưa cấu hình qua menuconfig */
#ifndef CONFIG_HTTPS_SERVER_WIFI_SSID
#define CONFIG_HTTPS_SERVER_WIFI_SSID "Minh Toan"
#endif

#ifndef CONFIG_HTTPS_SERVER_WIFI_PASSWORD
#define CONFIG_HTTPS_SERVER_WIFI_PASSWORD "21012004"
#endif

#ifndef CONFIG_HTTPS_SERVER_MAX_RETRY
#define CONFIG_HTTPS_SERVER_MAX_RETRY 10
#endif

#ifndef CONFIG_HTTPS_SERVER_SUPPORT_TLS
#define CONFIG_HTTPS_SERVER_SUPPORT_TLS 1
#endif

#ifndef CONFIG_HTTPS_SERVER_PORT_HTTP
#define CONFIG_HTTPS_SERVER_PORT_HTTP 80
#endif

#ifndef CONFIG_HTTPS_SERVER_PORT_HTTPS
#define CONFIG_HTTPS_SERVER_PORT_HTTPS 443
#endif

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;
static httpd_handle_t s_server = NULL;

static const char *wifi_reason_to_str(uint8_t reason)
{
    switch (reason) {
    case 1:   return "UNSPECIFIED";
    case 2:   return "AUTH_EXPIRE";
    case 3:   return "AUTH_LEAVE";
    case 4:   return "ASSOC_EXPIRE";
    case 5:   return "ASSOC_TOOMANY";
    case 6:   return "NOT_AUTHED";
    case 7:   return "NOT_ASSOCED";
    case 8:   return "ASSOC_LEAVE";
    case 9:   return "ASSOC_NOT_AUTHED";
    case 15:  return "4WAY_HANDSHAKE_TIMEOUT (Sai mật khẩu hoặc lỗi bắt tay 4-bước)";
    case 200: return "BEACON_TIMEOUT";
    case 201: return "NO_AP_FOUND (Không tìm thấy tên Wi-Fi! Cần bật 'Tối đa hóa tương thích' nếu là iPhone Hotspot)";
    case 202: return "AUTH_FAIL (Xác thực thất bại)";
    case 203: return "ASSOC_FAIL (Kết nạp thất bại)";
    case 204: return "HANDSHAKE_TIMEOUT (Hết thời gian bắt tay)";
    case 205: return "CONNECTION_FAIL";
    case 206: return "AP_TSF_RESET";
    default:  return "UNKNOWN";
    }
}

/* =========================================================================
 * WI-FI STATION EVENT HANDLER & INITIALIZATION
 * ========================================================================= */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "==> [Wi-Fi] Bắt đầu kết nối tới AP SSID: %s...", CONFIG_HTTPS_SERVER_WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        uint8_t reason = disc ? disc->reason : 0;
        s_retry_num++;

        ESP_LOGW(TAG, "==> [Wi-Fi] Ngắt kết nối/Thất bại lần [%d/%d]! Mã lý do: %d (%s)",
                 s_retry_num, CONFIG_HTTPS_SERVER_MAX_RETRY, reason, wifi_reason_to_str(reason));

        if (s_retry_num < CONFIG_HTTPS_SERVER_MAX_RETRY) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "==> [Wi-Fi] Đã thử %d lần nhưng không thể kết nối tới AP '%s'",
                     CONFIG_HTTPS_SERVER_MAX_RETRY, CONFIG_HTTPS_SERVER_WIFI_SSID);
            if (reason == 201) {
                ESP_LOGE(TAG, "----------------------------------------------------------");
                ESP_LOGE(TAG, "💡 CHẨN ĐOÁN LỖI 201 (NO_AP_FOUND):                       ");
                ESP_LOGE(TAG, "   Nếu bạn đang phát Wi-Fi từ iPhone (Personal Hotspot):   ");
                ESP_LOGE(TAG, "   1. Vào Cài đặt -> Điểm truy cập cá nhân.                ");
                ESP_LOGE(TAG, "   2. BẬT mục 'Tối đa hóa khả năng tương thích'           ");
                ESP_LOGE(TAG, "      (Maximize Compatibility) để iPhone phát sóng 2.4GHz! ");
                ESP_LOGE(TAG, "      (Mặc định iPhone chỉ phát 5GHz mà ESP32 không thấy) ");
                ESP_LOGE(TAG, "   3. Mở sáng màn hình iPhone ở trang Cài đặt Hotspot.    ");
                ESP_LOGE(TAG, "----------------------------------------------------------");
            } else if (reason == 15 || reason == 204) {
                ESP_LOGE(TAG, "----------------------------------------------------------");
                ESP_LOGE(TAG, "💡 CHẨN ĐOÁN LỖI %d (HANDSHAKE TIMEOUT):                   ", reason);
                ESP_LOGE(TAG, "   Kiểm tra lại mật khẩu Wi-Fi hoặc tắt/bật lại Hotspot.   ");
                ESP_LOGE(TAG, "----------------------------------------------------------");
            }
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_retry_num = 0;

        wifi_ap_record_t ap_info;
        esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);

        ESP_LOGI(TAG, "==========================================================");
        ESP_LOGI(TAG, "==> [Wi-Fi] KẾT NỐI THÀNH CÔNG! THÔNG SỐ TRẠNG THÁI MẠNG:  ");
        ESP_LOGI(TAG, "  - Tên Wi-Fi (SSID) : %s", CONFIG_HTTPS_SERVER_WIFI_SSID);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  - BSSID (MAC AP)   : %02X:%02X:%02X:%02X:%02X:%02X (Kênh %d, Sóng %d dBm)",
                     ap_info.bssid[0], ap_info.bssid[1], ap_info.bssid[2],
                     ap_info.bssid[3], ap_info.bssid[4], ap_info.bssid[5],
                     ap_info.primary, ap_info.rssi);
        }
        ESP_LOGI(TAG, "  - Địa chỉ IP cấp   : " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "  - Địa chỉ Mask     : " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "  - Địa chỉ Gateway  : " IPSTR, IP2STR(&event->ip_info.gw));
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

    /* Cấu hình mã quốc gia VN (hỗ trợ toàn bộ kênh 1 - 13) */
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
    strncpy((char *)wifi_config.sta.ssid, CONFIG_HTTPS_SERVER_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, CONFIG_HTTPS_SERVER_WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Tối ưu hóa băng thông HT20, tắt Modem Sleep & hạ TX Power cho ESP32-C3 SuperMini */
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(48); // 12 dBm

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(35000));

    if (bits & WIFI_CONNECTED_BIT) {
        return true;
    }
    return false;
}

/* =========================================================================
 * REST API & WEB DASHBOARD URI HANDLERS
 * ========================================================================= */

/**
 * @brief GET / : Trả về trang Web Dashboard điều khiển đèn tương tác trực tiếp
 */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    static const char html_template[] =
        "<!DOCTYPE html>"
        "<html lang='vi'>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
        "<title>PBL5 Smart Light Dashboard</title>"
        "<style>"
        "body{font-family:system-ui,-apple-system,sans-serif;background:#0f172a;color:#f8fafc;margin:0;padding:20px;display:flex;justify-content:center;align-items:center;min-height:100vh;}"
        ".card{background:rgba(30,41,59,0.85);backdrop-filter:blur(16px);border:1px solid rgba(255,255,255,0.1);border-radius:18px;padding:28px;max-width:440px;width:100%;box-shadow:0 20px 40px rgba(0,0,0,0.5);text-align:center;}"
        "h1{font-size:22px;margin:0 0 4px;font-weight:700;color:#38bdf8;}"
        "p.sub{font-size:12px;color:#94a3b8;margin:0 0 20px;}"
        ".status-badge{display:inline-block;padding:8px 18px;border-radius:999px;font-weight:700;font-size:14px;letter-spacing:1px;margin-bottom:20px;transition:all 0.3s;}"
        ".status-on{background:rgba(34,197,94,0.2);color:#4ade80;border:1px solid #22c55e;box-shadow:0 0 16px rgba(34,197,94,0.4);}"
        ".status-off{background:rgba(239,68,68,0.2);color:#f87171;border:1px solid #ef4444;}"
        ".btn-group{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:20px;}"
        "button{border:none;border-radius:10px;padding:12px;font-weight:600;font-size:14px;cursor:pointer;transition:transform 0.1s,opacity 0.2s;}"
        "button:active{transform:scale(0.96);}"
        ".btn-on{background:#22c55e;color:#fff;}"
        ".btn-off{background:#ef4444;color:#fff;}"
        ".btn-cycle{grid-column:span 2;background:#6366f1;color:#fff;}"
        ".slider-container{margin:18px 0;text-align:left;}"
        ".slider-label{display:flex;justify-content:space-between;font-size:13px;color:#cbd5e1;margin-bottom:6px;}"
        "input[type=range]{width:100%;accent-color:#38bdf8;cursor:pointer;}"
        ".palette{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-top:14px;}"
        ".color-dot{height:38px;border-radius:8px;cursor:pointer;border:2px solid transparent;transition:all 0.2s;}"
        ".color-dot:hover{transform:scale(1.08);border-color:#fff;}"
        ".footer{margin-top:22px;font-size:11px;color:#64748b;}"
        "</style>"
        "</head>"
        "<body>"
        "<div class='card'>"
        "<h1>PBL5 SMART LIGHT</h1>"
        "<p class='sub'>Mục 8.3 & 8.4: Web Server Local Control (ESP-IDF v6.0.2)</p>"
        "<div id='badge' class='status-badge status-on'>ĐÈN ĐANG BẬT</div>"
        "<div class='btn-group'>"
        "<button class='btn-on' onclick='sendCtrl({status:true})'>BẬT ĐÈN</button>"
        "<button class='btn-off' onclick='sendCtrl({status:false})'>TẮT ĐÈN</button>"
        "<button class='btn-cycle' onclick='sendCtrl({action:\"next_color\"})'>🎨 ĐỔI MÀU (8 MÀU RGB)</button>"
        "</div>"
        "<div class='slider-container'>"
        "<div class='slider-label'><span>Độ sáng</span><span id='bri-val'>100%</span></div>"
        "<input type='range' id='bri' min='5' max='100' value='100' onchange='sendCtrl({brightness:parseInt(this.value)})'>"
        "</div>"
        "<div class='slider-label'><span>Chọn màu nhanh:</span></div>"
        "<div class='palette'>"
        "<div class='color-dot' style='background:#f43f5e' onclick='sendColor(255,0,0)'></div>"
        "<div class='color-dot' style='background:#10b981' onclick='sendColor(0,255,0)'></div>"
        "<div class='color-dot' style='background:#3b82f6' onclick='sendColor(0,0,255)'></div>"
        "<div class='color-dot' style='background:#eab308' onclick='sendColor(255,255,0)'></div>"
        "<div class='color-dot' style='background:#d946ef' onclick='sendColor(255,0,255)'></div>"
        "<div class='color-dot' style='background:#06b6d4' onclick='sendColor(0,255,255)'></div>"
        "<div class='color-dot' style='background:#f97316' onclick='sendColor(255,128,0)'></div>"
        "<div class='color-dot' style='background:#f8fafc' onclick='sendColor(255,255,255)'></div>"
        "</div>"
        "<div class='footer'>ESP32 Hardware SPI2 DMA @ 3.2MHz | Boot Button HAL</div>"
        "</div>"
        "<script>"
        "async function fetchState(){"
        " try{"
        "  let res=await fetch('/light');"
        "  if(res.ok){"
        "   let d=await res.json();"
        "   let b=document.getElementById('badge');"
        "   if(d.status){b.className='status-badge status-on';b.innerText='ĐÈN ĐANG BẬT';}"
        "   else{b.className='status-badge status-off';b.innerText='ĐÈN ĐANG TẮT';}"
        "   document.getElementById('bri-val').innerText=d.brightness+'%';"
        "   document.getElementById('bri').value=d.brightness;"
        "  }"
        " }catch(e){console.error(e);}"
        "}"
        "async function sendCtrl(body){"
        " await fetch('/light',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});"
        " fetchState();"
        "}"
        "function sendColor(r,g,b){sendCtrl({r:r,g:g,b:b});}"
        "setInterval(fetchState,2000);fetchState();"
        "</script>"
        "</body>"
        "</html>";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, html_template, strlen(html_template));
    return ESP_OK;
}

/**
 * @brief GET /light : Trả về JSON trạng thái hiện tại của đèn
 */
static esp_err_t light_get_handler(httpd_req_t *req)
{
    bool is_on = app_driver_get_state();
    uint8_t brightness = app_driver_get_brightness();
    uint8_t r = 0, g = 0, b = 0;
    app_driver_get_rgb(&r, &g, &b);
    uint8_t color_idx = app_driver_get_color_index();
    const char *color_name = app_driver_get_color_name();

    char json_resp[256];
    snprintf(json_resp, sizeof(json_resp),
             "{\"status\":%s,\"brightness\":%u,\"color_index\":%u,\"color_name\":\"%s\","
             "\"color\":{\"r\":%u,\"g\":%u,\"b\":%u}}",
             is_on ? "true" : "false",
             brightness,
             color_idx,
             color_name,
             r, g, b);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_resp, strlen(json_resp));
    ESP_LOGI(TAG, "==> [HTTP GET /light] Phản hồi JSON: %s", json_resp);
    return ESP_OK;
}

/**
 * @brief POST /light : Nhận payload JSON điều khiển trạng thái đèn
 */
static esp_err_t light_set_handler(httpd_req_t *req)
{
    char buf[256] = {0};
    int total_len = req->content_len;
    int cur_len = 0;

    if (total_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload quá lớn (> 255 bytes)");
        return ESP_FAIL;
    }

    while (cur_len < total_len) {
        int received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';
    ESP_LOGI(TAG, "==> [HTTP POST /light] Nhận lệnh từ client: %s", buf);

    /* Phân tích cú pháp lệnh JSON */
    if (strstr(buf, "\"status\":true") || strstr(buf, "\"status\": true") || strstr(buf, "\"status\":1")) {
        app_driver_set_state(true);
    } else if (strstr(buf, "\"status\":false") || strstr(buf, "\"status\": false") || strstr(buf, "\"status\":0")) {
        app_driver_set_state(false);
    }

    if (strstr(buf, "\"action\":\"next_color\"") || strstr(buf, "\"action\": \"next_color\"")) {
        app_driver_next_color();
    } else if (strstr(buf, "\"action\":\"toggle\"") || strstr(buf, "\"action\": \"toggle\"")) {
        app_driver_toggle_state();
    } else if (strstr(buf, "\"action\":\"brightness_up\"") || strstr(buf, "\"action\": \"brightness_up\"")) {
        app_driver_adjust_brightness(+10);
    } else if (strstr(buf, "\"action\":\"brightness_down\"") || strstr(buf, "\"action\": \"brightness_down\"")) {
        app_driver_adjust_brightness(-10);
    }

    /* Đổi màu RGB thủ công nếu có các trường r, g, b */
    char *r_pos = strstr(buf, "\"r\":");
    if (!r_pos) r_pos = strstr(buf, "\"red\":");
    char *g_pos = strstr(buf, "\"g\":");
    if (!g_pos) g_pos = strstr(buf, "\"green\":");
    char *b_pos = strstr(buf, "\"b\":");
    if (!b_pos) b_pos = strstr(buf, "\"blue\":");

    if (r_pos && g_pos && b_pos) {
        int r_val = atoi(r_pos + (r_pos[1] == 'e' ? 6 : 4));
        int g_val = atoi(g_pos + (g_pos[1] == 'r' ? 8 : 4));
        int b_val = atoi(b_pos + (b_pos[1] == 'l' ? 7 : 4));
        if (r_val >= 0 && r_val <= 255 && g_val >= 0 && g_val <= 255 && b_val >= 0 && b_val <= 255) {
            app_driver_set_color((uint8_t)r_val, (uint8_t)g_val, (uint8_t)b_val);
        }
    }

    /* Đổi độ sáng nếu có trường brightness */
    char *bri_pos = strstr(buf, "\"brightness\":");
    if (bri_pos) {
        int bri_val = atoi(bri_pos + 13);
        if (bri_val >= 5 && bri_val <= 100) {
            app_driver_set_brightness((uint8_t)bri_val);
        }
    }

    /* Phản hồi JSON xác nhận */
    bool is_on = app_driver_get_state();
    uint8_t brightness = app_driver_get_brightness();
    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"result\":\"success\",\"status\":%s,\"brightness\":%u}",
             is_on ? "true" : "false", brightness);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

/* Khai báo bảng URI endpoints */
static const httpd_uri_t uri_root = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = root_get_handler,
};

static const httpd_uri_t uri_light_get = {
    .uri      = "/light",
    .method   = HTTP_GET,
    .handler  = light_get_handler,
};

static const httpd_uri_t uri_light_set = {
    .uri      = "/light",
    .method   = HTTP_POST,
    .handler  = light_set_handler,
};

static void register_uri_endpoints(httpd_handle_t server)
{
    httpd_register_uri_handler(server, &uri_root);
    httpd_register_uri_handler(server, &uri_light_get);
    httpd_register_uri_handler(server, &uri_light_set);
    ESP_LOGI(TAG, "Đã đăng ký các URI Endpoints: GET /, GET /light, POST /light");
}

/* =========================================================================
 * HTTP & HTTPS SERVER LIFECYCLE
 * ========================================================================= */

#if CONFIG_HTTPS_SERVER_SUPPORT_TLS
/**
 * @brief Khởi động máy chủ HTTPS cổng 443 với mã hóa TLS
 */
static esp_err_t start_https_server(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  Khởi động HTTPS Server cổng %d với bảo mật TLS         ", CONFIG_HTTPS_SERVER_PORT_HTTPS);
    ESP_LOGI(TAG, "==========================================================");

    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
    conf.port_secure = CONFIG_HTTPS_SERVER_PORT_HTTPS;

    /* Nạp chứng chỉ X.509 và Private Key (khắc phục chuẩn ESP-IDF v6.0.2) */
    extern const unsigned char cacert_pem_start[] asm("_binary_cacert_pem_start");
    extern const unsigned char cacert_pem_end[]   asm("_binary_cacert_pem_end");
    conf.servercert = cacert_pem_start;
    conf.servercert_len = cacert_pem_end - cacert_pem_start;

    extern const unsigned char prvtkey_pem_start[] asm("_binary_prvtkey_pem_start");
    extern const unsigned char prvtkey_pem_end[]   asm("_binary_prvtkey_pem_end");
    conf.prvtkey_pem = prvtkey_pem_start;
    conf.prvtkey_len = prvtkey_pem_end - prvtkey_pem_start;

    ESP_LOGI(TAG, "Đã nạp chứng chỉ Server cert: %u bytes, Private key: %u bytes",
             (unsigned int)conf.servercert_len, (unsigned int)conf.prvtkey_len);

    esp_err_t ret = httpd_ssl_start(&s_server, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khởi động HTTPS Server thất bại: %s", esp_err_to_name(ret));
        return ret;
    }

    register_uri_endpoints(s_server);
    ESP_LOGI(TAG, "HTTPS Server đã chạy thành công trên cổng %d!", CONFIG_HTTPS_SERVER_PORT_HTTPS);
    return ESP_OK;
}
#else
/**
 * @brief Khởi động máy chủ HTTP chuẩn cổng 80 (không mã hóa)
 */
static esp_err_t start_http_server(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  Khởi động HTTP Web Server cổng %d (Plaintext)          ", CONFIG_HTTPS_SERVER_PORT_HTTP);
    ESP_LOGI(TAG, "==========================================================");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CONFIG_HTTPS_SERVER_PORT_HTTP;
    config.lru_purge_enable = true;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khởi động HTTP Server thất bại: %s", esp_err_to_name(ret));
        return ret;
    }

    register_uri_endpoints(s_server);
    ESP_LOGI(TAG, "HTTP Server đã chạy thành công trên cổng %d!", CONFIG_HTTPS_SERVER_PORT_HTTP);
    return ESP_OK;
}
#endif

/* =========================================================================
 * APPLICATION ENTRY POINT (app_main)
 * ========================================================================= */
void app_main(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "   PBL5 Smart Light - Mục 8.3 & 8.4: HTTP & HTTPS Server  ");
    ESP_LOGI(TAG, "   Protocol: %s | Port: %d                               ",
             CONFIG_HTTPS_SERVER_SUPPORT_TLS ? "HTTPS (TLS 1.2/1.3)" : "HTTP (Plaintext)",
             CONFIG_HTTPS_SERVER_SUPPORT_TLS ? CONFIG_HTTPS_SERVER_PORT_HTTPS : CONFIG_HTTPS_SERVER_PORT_HTTP);
    ESP_LOGI(TAG, "   WS2812B GPIO %d (8 LEDs) | Boot Button GPIO %d         ",
             LIGHT_WS2818_GPIO, LIGHT_BUTTON_GPIO);
    ESP_LOGI(TAG, "==========================================================");

    /* 1. Khởi tạo Flash NVS */
    ESP_LOGI(TAG, "[1/4] Khởi tạo Flash NVS Storage...");
    app_storage_init();

    /* 2. Khởi tạo Tầng Driver Phần Cứng */
    ESP_LOGI(TAG, "[2/4] Khởi tạo Hardware Driver (WS2812B SPI DMA & Button HAL)...");
    app_driver_init();

    /* 3. Khởi tạo và kết nối Wi-Fi Station */
    ESP_LOGI(TAG, "[3/4] Khởi tạo Wi-Fi Station...");
    wifi_initialize();

    while (!wifi_station_connect()) {
        ESP_LOGW(TAG, "==> [Wi-Fi] Chưa kết nối được tới AP '%s'. Đang thử lại sau 5 giây...", CONFIG_HTTPS_SERVER_WIFI_SSID);
        s_retry_num = 0;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_wifi_connect();
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                               WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                               pdFALSE, pdFALSE,
                                               pdMS_TO_TICKS(35000));
        if (bits & WIFI_CONNECTED_BIT) {
            break;
        }
    }

    /* 4. Khởi động Web Server */
    ESP_LOGI(TAG, "[4/4] Khởi chạy Web Server...");
#if CONFIG_HTTPS_SERVER_SUPPORT_TLS
    start_https_server();
#else
    start_http_server();
#endif

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  WEB SERVER LOCAL CONTROL SẴN SÀNG PHỤC VỤ!              ");
    ESP_LOGI(TAG, "  Mở trình duyệt trên máy tính/điện thoại:                ");
    ESP_LOGI(TAG, "  - Giao diện điều khiển Web: %s://<IP-THIET-BI>/        ",
             CONFIG_HTTPS_SERVER_SUPPORT_TLS ? "https" : "http");
    ESP_LOGI(TAG, "  - API đọc trạng thái JSON : %s://<IP-THIET-BI>/light   ",
             CONFIG_HTTPS_SERVER_SUPPORT_TLS ? "https" : "http");
    ESP_LOGI(TAG, "==========================================================");

    /* Vòng lặp giám sát Heartbeat & tài nguyên */
    uint32_t heartbeat_cnt = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        heartbeat_cnt++;
        ESP_LOGI(TAG, "[Heartbeat #%02" PRIu32 "] Light: %s (%u%%) | Màu: %s | Free Heap: %" PRIu32 " bytes",
                 heartbeat_cnt,
                 app_driver_get_state() ? "BẬT (ON)" : "TẮT (OFF)",
                 app_driver_get_brightness(),
                 app_driver_get_color_name(),
                 esp_get_free_heap_size());
    }
}
