/*
 * Smart Light Driver (WS2818/WS2812B NeoPixel Hardware Mapping with NVS Persistence)
 * Compliant with ESP32-C3 Wireless Adventure - Chapter 6, Section 6.5
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_log.h"
#include "esp_err.h"

#include "light_driver.h"
#include "ws2812_driver.h"
#include "app_storage.h"

#define TAG "light_driver"

#define LIGHT_PARAM_CHECK(con) do { \
        if (!(con)) { \
            ESP_LOGE(TAG, "<ESP_ERR_INVALID_ARG> !(%s)", #con); \
            return ESP_ERR_INVALID_ARG; \
        } \
    } while(0)

#define LIGHT_ERROR_CHECK(con, err, format, ...) do { \
        if (con) { \
            if(*format != '\0') \
                ESP_LOGW(TAG, "<%s> " format, esp_err_to_name(err), ##__VA_ARGS__); \
            return err; \
        } \
    } while(0)

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

/**
 * @brief Trạng thái lưu trữ của hệ thống đèn
 */
typedef struct {
    uint8_t mode;
    uint8_t on;
    uint16_t hue;
    uint8_t saturation;
    uint8_t value;
    uint8_t color_temperature;
    uint8_t brightness;
    uint32_t fade_period_ms;
    uint32_t blink_period_ms;
} light_status_t;

#define LIGHT_STATUS_STORE_KEY "light_status"

static light_status_t g_light_status = {0};
static uint8_t s_current_r = 255;
static uint8_t s_current_g = 255;
static uint8_t s_current_b = 255;

// Biến điều khiển hiệu ứng breathing / blinking
static TimerHandle_t s_effect_timer = NULL;
typedef enum {
    EFFECT_NONE = 0,
    EFFECT_BREATH,
    EFFECT_BLINK
} light_effect_type_t;

static light_effect_type_t s_active_effect = EFFECT_NONE;
static uint8_t s_effect_base_r = 255;
static uint8_t s_effect_base_g = 255;
static uint8_t s_effect_base_b = 255;
static float s_breath_elapsed_ms = 0.0f;
static bool s_blink_state = false;

/* Chuyển đổi màu HSV sang RGB */
static esp_err_t hsv2rgb(uint16_t hue, uint8_t saturation, uint8_t value,
                         uint8_t *red, uint8_t *green, uint8_t *blue)
{
    hue = hue % 360;
    uint16_t hi = (hue / 60) % 6;
    uint16_t F = 100 * hue / 60 - 100 * hi;
    uint16_t P = value * (100 - saturation) / 100;
    uint16_t Q = value * (10000 - F * saturation) / 10000;
    uint16_t T = value * (10000 - saturation * (100 - F)) / 10000;

    switch (hi) {
        case 0: *red = value; *green = T;     *blue = P;     break;
        case 1: *red = Q;     *green = value; *blue = P;     break;
        case 2: *red = P;     *green = value; *blue = T;     break;
        case 3: *red = P;     *green = Q;     *blue = value; break;
        case 4: *red = T;     *green = P;     *blue = value; break;
        case 5: *red = value; *green = P;     *blue = Q;     break;
        default: return ESP_FAIL;
    }

    *red   = (uint8_t)(*red * 255 / 100);
    *green = (uint8_t)(*green * 255 / 100);
    *blue  = (uint8_t)(*blue * 255 / 100);

    return ESP_OK;
}

/* Chuyển đổi RGB sang HSV */
static void rgb2hsv(uint8_t red, uint8_t green, uint8_t blue,
                    uint16_t *h, uint8_t *s, uint8_t *v)
{
    double r = red / 255.0;
    double g = green / 255.0;
    double b = blue / 255.0;

    double max_val = MAX(r, MAX(g, b));
    double min_val = MIN(r, MIN(g, b));
    double delta = max_val - min_val;

    *v = (uint8_t)(max_val * 100.0 + 0.5);

    if (max_val == 0.0 || delta == 0.0) {
        *s = 0;
        *h = 0;
        return;
    }

    *s = (uint8_t)((delta / max_val) * 100.0 + 0.5);

    double hue = 0.0;
    if (r == max_val) {
        hue = (g - b) / delta;
    } else if (g == max_val) {
        hue = 2.0 + (b - r) / delta;
    } else {
        hue = 4.0 + (r - g) / delta;
    }

    hue *= 60.0;
    if (hue < 0.0) {
        hue += 360.0;
    }

    *h = (uint16_t)(hue + 0.5);
}

/* Chuyển đổi CTB (Color Temperature & Brightness) sang RGB */
static void ctb2rgb(uint8_t ct, uint8_t brightness, uint8_t *r, uint8_t *g, uint8_t *b)
{
    // ct: 0 (Ấm nhất / Warm White 2700K) -> 100 (Lạnh nhất / Cool White 6500K)
    float warm_ratio = (100.0f - ct) / 100.0f;
    float cool_ratio = ct / 100.0f;

    // Màu trắng ấm: {255, 180, 100}, Màu trắng lạnh: {200, 225, 255}
    float raw_r = (255.0f * warm_ratio + 200.0f * cool_ratio) * (brightness / 100.0f);
    float raw_g = (180.0f * warm_ratio + 225.0f * cool_ratio) * (brightness / 100.0f);
    float raw_b = (100.0f * warm_ratio + 255.0f * cool_ratio) * (brightness / 100.0f);

    *r = (uint8_t)(raw_r > 255.0f ? 255 : raw_r);
    *g = (uint8_t)(raw_g > 255.0f ? 255 : raw_g);
    *b = (uint8_t)(raw_b > 255.0f ? 255 : raw_b);
}

/* Ánh xạ và xuất ra phần cứng WS2812B */
static void hardware_apply_light(void)
{
    if (!g_light_status.on) {
        ws2812_clear();
        ws2812_refresh();
        return;
    }

    // Đang bật -> xuất màu tương ứng với chế độ
    switch (g_light_status.mode) {
        case MODE_RGB: {
            float ratio = g_light_status.brightness / 100.0f;
            ws2812_set_all_brightness(s_current_r, s_current_g, s_current_b, ratio);
            break;
        }
        case MODE_HSV: {
            uint8_t r, g, b;
            hsv2rgb(g_light_status.hue, g_light_status.saturation, g_light_status.value, &r, &g, &b);
            ws2812_set_all(r, g, b);
            break;
        }
        case MODE_CTB: {
            uint8_t r, g, b;
            ctb2rgb(g_light_status.color_temperature, g_light_status.brightness, &r, &g, &b);
            ws2812_set_all(r, g, b);
            break;
        }
        default: {
            ws2812_set_all(s_current_r, s_current_g, s_current_b);
            break;
        }
    }
    ws2812_refresh();
}

/* Timer Callback cho hiệu ứng Thở (Breathing) và Nhấp nháy (Blink) */
static void effect_timer_callback(TimerHandle_t xTimer)
{
    if (!g_light_status.on) {
        ws2812_clear();
        ws2812_refresh();
        return;
    }

    if (s_active_effect == EFFECT_BREATH) {
        const float cycle_ms = 2400.0f; // Chu kỳ thở 2.4s
        const float step_ms = 30.0f;
        const float pi = 3.14159265f;

        float angle = (s_breath_elapsed_ms / cycle_ms) * 2.0f * pi;
        float factor = 0.05f + 0.95f * (0.5f * (1.0f - cosf(angle)));

        ws2812_set_all_brightness(s_effect_base_r, s_effect_base_g, s_effect_base_b, factor);
        ws2812_refresh();

        s_breath_elapsed_ms += step_ms;
        if (s_breath_elapsed_ms >= cycle_ms) {
            s_breath_elapsed_ms = 0.0f;
        }
    } else if (s_active_effect == EFFECT_BLINK) {
        s_blink_state = !s_blink_state;
        if (s_blink_state) {
            ws2812_set_all(s_effect_base_r, s_effect_base_g, s_effect_base_b);
        } else {
            ws2812_clear();
        }
        ws2812_refresh();
    }
}

/* Khởi tạo Driver Đèn và Khôi phục NVS */
esp_err_t light_driver_init(light_driver_config_t *config)
{
    LIGHT_PARAM_CHECK(config);

    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "   Khởi tạo Light Driver (Section 6.5)           ");
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "LEDC Config mapping -> GPIO Red:%d Green:%d Blue:%d Cold:%d Warm:%d",
             config->gpio_red, config->gpio_green, config->gpio_blue, config->gpio_cold, config->gpio_warm);
    ESP_LOGI(TAG, "Freq: %u Hz, Fade Period: %u ms, Blink Period: %u ms",
             (unsigned int)config->freq_hz, (unsigned int)config->fade_period_ms, (unsigned int)config->blink_period_ms);

    // 1. Khởi tạo phần cứng thanh LED WS2812S (8 bóng) trên GPIO 4 (hoặc chân gpio_green)
    int ws2812_gpio = config->gpio_green > 0 ? config->gpio_green : 4;
    esp_err_t ret = ws2812_init(ws2812_gpio, 8);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khởi tạo WS2812S (8 bóng) phần cứng thất bại: %s", esp_err_to_name(ret));
    }

    // 2. Hiện thực hóa cơ chế khôi phục trạng thái từ Flash (NVS Persistence)
    memset(&g_light_status, 0, sizeof(light_status_t));
    if (app_storage_get(LIGHT_STATUS_STORE_KEY, &g_light_status, sizeof(light_status_t)) != ESP_OK) {
        ESP_LOGW(TAG, "Chưa tìm thấy trạng thái đèn trong Flash, thiết lập cấu hình mặc định...");
        g_light_status.mode              = MODE_RGB;
        g_light_status.on                = 1;
        g_light_status.hue               = 360;
        g_light_status.saturation        = 0;
        g_light_status.value             = 100;
        g_light_status.color_temperature = 50;
        g_light_status.brightness        = 100;
        g_light_status.fade_period_ms    = config->fade_period_ms ? config->fade_period_ms : 100;
        g_light_status.blink_period_ms   = config->blink_period_ms ? config->blink_period_ms : 1500;

        s_current_r = 255;
        s_current_g = 255;
        s_current_b = 255;

        // Ghi cấu hình mặc định xuống Flash
        app_storage_set(LIGHT_STATUS_STORE_KEY, &g_light_status, sizeof(light_status_t));
    } else {
        ESP_LOGI(TAG, "Đã khôi phục trạng thái từ Flash NVS thành công!");
        ESP_LOGI(TAG, "Trạng thái: %s | Chế độ: %d | Độ sáng: %d%% | Nhiệt độ màu: %d%%",
                 g_light_status.on ? "BẬT (ON)" : "TẮT (OFF)",
                 g_light_status.mode, g_light_status.brightness, g_light_status.color_temperature);
        ESP_LOGI(TAG, "HSV: Hue=%d, Saturation=%d, Value=%d",
                 g_light_status.hue, g_light_status.saturation, g_light_status.value);
    }

    // 3. Cập nhật ngay trạng thái đèn ra phần cứng
    hardware_apply_light();

    return ESP_OK;
}

esp_err_t light_driver_deinit(void)
{
    light_driver_breath_stop();
    light_driver_blink_stop();
    ws2812_clear();
    ws2812_refresh();
    return ESP_OK;
}

esp_err_t light_driver_config(uint32_t fade_period_ms, uint32_t blink_period_ms)
{
    g_light_status.fade_period_ms = fade_period_ms;
    g_light_status.blink_period_ms = blink_period_ms;
    return app_storage_set(LIGHT_STATUS_STORE_KEY, &g_light_status, sizeof(light_status_t));
}

esp_err_t light_driver_set_switch(bool status)
{
    g_light_status.on = status ? 1 : 0;
    ESP_LOGI(TAG, "[API] light_driver_set_switch: %s", status ? "BẬT (ON)" : "TẮT (OFF)");

    if (s_active_effect == EFFECT_NONE) {
        hardware_apply_light();
    }

    return app_storage_set(LIGHT_STATUS_STORE_KEY, &g_light_status, sizeof(light_status_t));
}

bool light_driver_get_switch(void)
{
    return g_light_status.on != 0;
}

esp_err_t light_driver_set_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    light_driver_breath_stop();
    light_driver_blink_stop();

    s_current_r = red;
    s_current_g = green;
    s_current_b = blue;

    g_light_status.mode = MODE_RGB;
    g_light_status.on   = 1;

    rgb2hsv(red, green, blue, &g_light_status.hue, &g_light_status.saturation, &g_light_status.value);

    ESP_LOGI(TAG, "[API] light_driver_set_rgb: R=%d, G=%d, B=%d", red, green, blue);
    hardware_apply_light();

    return app_storage_set(LIGHT_STATUS_STORE_KEY, &g_light_status, sizeof(light_status_t));
}

esp_err_t light_driver_set_hsv(uint16_t hue, uint8_t saturation, uint8_t value)
{
    LIGHT_PARAM_CHECK(hue <= 360);
    LIGHT_PARAM_CHECK(saturation <= 100);
    LIGHT_PARAM_CHECK(value <= 100);

    light_driver_breath_stop();
    light_driver_blink_stop();

    g_light_status.mode       = MODE_HSV;
    g_light_status.on         = 1;
    g_light_status.hue        = hue;
    g_light_status.saturation = saturation;
    g_light_status.value      = value;

    hsv2rgb(hue, saturation, value, &s_current_r, &s_current_g, &s_current_b);

    ESP_LOGI(TAG, "[API] light_driver_set_hsv: Hue=%d, Sat=%d, Val=%d -> R=%d, G=%d, B=%d",
             hue, saturation, value, s_current_r, s_current_g, s_current_b);
    hardware_apply_light();

    return app_storage_set(LIGHT_STATUS_STORE_KEY, &g_light_status, sizeof(light_status_t));
}

esp_err_t light_driver_set_ctb(uint8_t color_temperature, uint8_t brightness)
{
    LIGHT_PARAM_CHECK(color_temperature <= 100);
    LIGHT_PARAM_CHECK(brightness <= 100);

    light_driver_breath_stop();
    light_driver_blink_stop();

    g_light_status.mode              = MODE_CTB;
    g_light_status.on                = 1;
    g_light_status.color_temperature = color_temperature;
    g_light_status.brightness        = brightness;

    ctb2rgb(color_temperature, brightness, &s_current_r, &s_current_g, &s_current_b);

    ESP_LOGI(TAG, "[API] light_driver_set_ctb: ColorTemp=%d, Brightness=%d -> R=%d, G=%d, B=%d",
             color_temperature, brightness, s_current_r, s_current_g, s_current_b);
    hardware_apply_light();

    return app_storage_set(LIGHT_STATUS_STORE_KEY, &g_light_status, sizeof(light_status_t));
}

esp_err_t light_driver_set_hue(uint16_t hue)
{
    return light_driver_set_hsv(hue, g_light_status.saturation, g_light_status.value);
}

esp_err_t light_driver_set_saturation(uint8_t saturation)
{
    return light_driver_set_hsv(g_light_status.hue, saturation, g_light_status.value);
}

esp_err_t light_driver_set_value(uint8_t value)
{
    return light_driver_set_hsv(g_light_status.hue, g_light_status.saturation, value);
}

esp_err_t light_driver_set_color_temperature(uint8_t color_temperature)
{
    return light_driver_set_ctb(color_temperature, g_light_status.brightness);
}

esp_err_t light_driver_set_brightness(uint8_t brightness)
{
    LIGHT_PARAM_CHECK(brightness <= 100);
    g_light_status.brightness = brightness;
    hardware_apply_light();
    return app_storage_set(LIGHT_STATUS_STORE_KEY, &g_light_status, sizeof(light_status_t));
}

uint16_t light_driver_get_hue(void)
{
    return g_light_status.hue;
}

uint8_t light_driver_get_saturation(void)
{
    return g_light_status.saturation;
}

uint8_t light_driver_get_value(void)
{
    return g_light_status.value;
}

esp_err_t light_driver_get_hsv(uint16_t *hue, uint8_t *saturation, uint8_t *value)
{
    if (hue) *hue = g_light_status.hue;
    if (saturation) *saturation = g_light_status.saturation;
    if (value) *value = g_light_status.value;
    return ESP_OK;
}

uint8_t light_driver_get_color_temperature(void)
{
    return g_light_status.color_temperature;
}

uint8_t light_driver_get_brightness(void)
{
    return g_light_status.brightness;
}

esp_err_t light_driver_get_ctb(uint8_t *color_temperature, uint8_t *brightness)
{
    if (color_temperature) *color_temperature = g_light_status.color_temperature;
    if (brightness) *brightness = g_light_status.brightness;
    return ESP_OK;
}

uint8_t light_driver_get_mode(void)
{
    return g_light_status.mode;
}

/* Hiệu ứng thở (Breathing Effect) */
esp_err_t light_driver_breath_start(uint8_t red, uint8_t green, uint8_t blue)
{
    light_driver_blink_stop();

    s_effect_base_r = red;
    s_effect_base_g = green;
    s_effect_base_b = blue;
    s_active_effect = EFFECT_BREATH;
    s_breath_elapsed_ms = 0.0f;
    g_light_status.on = 1;

    ESP_LOGI(TAG, "[API] light_driver_breath_start: R=%d, G=%d, B=%d", red, green, blue);

    if (s_effect_timer == NULL) {
        s_effect_timer = xTimerCreate("eff_timer", pdMS_TO_TICKS(30), pdTRUE, NULL, effect_timer_callback);
    }
    xTimerChangePeriod(s_effect_timer, pdMS_TO_TICKS(30), 0);
    xTimerStart(s_effect_timer, 0);

    return ESP_OK;
}

esp_err_t light_driver_breath_stop(void)
{
    if (s_active_effect == EFFECT_BREATH) {
        s_active_effect = EFFECT_NONE;
        if (s_effect_timer != NULL) {
            xTimerStop(s_effect_timer, 0);
        }
        ESP_LOGI(TAG, "[API] light_driver_breath_stop");
        hardware_apply_light();
    }
    return ESP_OK;
}

/* Hiệu ứng nhấp nháy (Blink Effect) */
esp_err_t light_driver_blink_start(uint8_t red, uint8_t green, uint8_t blue)
{
    light_driver_breath_stop();

    s_effect_base_r = red;
    s_effect_base_g = green;
    s_effect_base_b = blue;
    s_active_effect = EFFECT_BLINK;
    s_blink_state = false;
    g_light_status.on = 1;

    uint32_t half_period_ms = g_light_status.blink_period_ms / 2;
    if (half_period_ms < 100) half_period_ms = 500;

    ESP_LOGI(TAG, "[API] light_driver_blink_start: R=%d, G=%d, B=%d, Chu kỳ=%u ms",
             red, green, blue, (unsigned int)g_light_status.blink_period_ms);

    if (s_effect_timer == NULL) {
        s_effect_timer = xTimerCreate("eff_timer", pdMS_TO_TICKS(half_period_ms), pdTRUE, NULL, effect_timer_callback);
    }
    xTimerChangePeriod(s_effect_timer, pdMS_TO_TICKS(half_period_ms), 0);
    xTimerStart(s_effect_timer, 0);

    return ESP_OK;
}

esp_err_t light_driver_blink_stop(void)
{
    if (s_active_effect == EFFECT_BLINK) {
        s_active_effect = EFFECT_NONE;
        if (s_effect_timer != NULL) {
            xTimerStop(s_effect_timer, 0);
        }
        ESP_LOGI(TAG, "[API] light_driver_blink_stop");
        hardware_apply_light();
    }
    return ESP_OK;
}

esp_err_t light_driver_fade_brightness(uint8_t brightness)
{
    return light_driver_set_brightness(brightness);
}

esp_err_t light_driver_fade_hue(uint16_t hue)
{
    return light_driver_set_hue(hue);
}

esp_err_t light_driver_fade_warm(uint8_t color_temperature)
{
    return light_driver_set_color_temperature(color_temperature);
}

esp_err_t light_driver_fade_stop(void)
{
    return ESP_OK;
}
