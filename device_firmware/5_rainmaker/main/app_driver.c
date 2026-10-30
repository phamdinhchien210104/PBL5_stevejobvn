/*
 * ESP32-S3 & ESP32-C3 Smart Light Project
 * Chapter 9: Remote Control through ESP RainMaker
 *
 * Driver Implementation:
 * 1. Physical Boot button with Multi-Gestures (Single click: Toggle, Double click: Hue cycle, Long press: Factory reset)
 * 2. WS2812B NeoPixel 8-LED strip via Hardware SPI2 DMA @ 3.2MHz on GPIO 4
 * 3. Full HSV color model integration with Cloud Device Shadow sync
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "iot_button.h"
#include "light_driver.h"
#include "ws2812_driver.h"

#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_utils.h>

#include DEVELOPMENT_BOARD
#include "app_priv.h"

static const char *TAG = "app_driver";

#define REBOOT_DELAY 2

static bool g_output_state = DEFAULT_POWER;
static uint16_t g_hue = DEFAULT_HUE;
static uint8_t g_saturation = DEFAULT_SATURATION;
static uint8_t g_brightness = DEFAULT_BRIGHTNESS;

extern esp_rmaker_device_t *light_device;

/* Preset hues for double click cycling */
static const uint16_t s_preset_hues[] = {0, 60, 120, 180, 240, 300};
#define NUM_PRESET_HUES (sizeof(s_preset_hues) / sizeof(s_preset_hues[0]))
static size_t s_hue_index = 0;
static char s_current_effect[16] = "Static";
static const char *s_effect_names[] = {
    "Static", "Blink", "Breathing", "Rainbow",
#if APP_ENABLE_POLICE_EFFECT
    "Police",
#endif
};

const char **app_light_get_effect_names(size_t *count)
{
    if (count) {
        *count = sizeof(s_effect_names) / sizeof(s_effect_names[0]);
    }
    return s_effect_names;
}

static bool is_animated_effect(void)
{
    return strcmp(s_current_effect, "Static") != 0;
}

static void stop_rainbow_task(void);
static void stop_police_timer(void);

static void push_btn_cb(void *arg)
{
    g_output_state = !g_output_state;
    ESP_LOGI(TAG, "==> [Physical Button Click]: Toggling light %s", g_output_state ? "ON" : "OFF");
    app_light_set_power(g_output_state);

    if (light_device) {
        esp_rmaker_param_t *power_param = esp_rmaker_device_get_param_by_name(light_device, ESP_RMAKER_DEF_POWER_NAME);
        if (power_param) {
            esp_rmaker_param_update_and_report(power_param, esp_rmaker_bool(g_output_state));
        }
    }
}

static void double_click_cb(void *arg)
{
    if (!g_output_state) {
        g_output_state = true;
        light_driver_set_switch(true);
        if (light_device) {
            esp_rmaker_param_t *p = esp_rmaker_device_get_param_by_name(light_device, ESP_RMAKER_DEF_POWER_NAME);
            if (p) esp_rmaker_param_update_and_report(p, esp_rmaker_bool(true));
        }
    }
    s_hue_index = (s_hue_index + 1) % NUM_PRESET_HUES;
    g_hue = s_preset_hues[s_hue_index];
    g_saturation = 100;
    ESP_LOGI(TAG, "==> [Physical Button Double Click]: Cycle Hue -> %d", g_hue);
    light_driver_set_hsv(g_hue, g_saturation, g_brightness);

    if (light_device) {
        esp_rmaker_param_t *hue_p = esp_rmaker_device_get_param_by_name(light_device, ESP_RMAKER_DEF_HUE_NAME);
        if (hue_p) esp_rmaker_param_update_and_report(hue_p, esp_rmaker_int(g_hue));
        esp_rmaker_param_t *sat_p = esp_rmaker_device_get_param_by_name(light_device, ESP_RMAKER_DEF_SATURATION_NAME);
        if (sat_p) esp_rmaker_param_update_and_report(sat_p, esp_rmaker_int(g_saturation));
    }
}

static void factory_reset_trigger(void *arg)
{
    ESP_LOGW(TAG, "==> [Physical Button Long Press]: Resetting Wi-Fi & Factory Reset in %d seconds...", REBOOT_DELAY);
    light_driver_blink_start(255, 0, 0);
    esp_rmaker_factory_reset(0, REBOOT_DELAY);
}

void app_driver_init(void)
{
    /* 1. Configure push button */
    button_config_t btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num     = LIGHT_BUTTON_GPIO,
            .active_level = LIGHT_BUTTON_ACTIVE_LEVEL,
        },
    };
    button_handle_t btn_handle = iot_button_create(&btn_cfg);
    if (btn_handle) {
        iot_button_register_cb(btn_handle, BUTTON_SINGLE_CLICK, push_btn_cb);
        iot_button_register_cb(btn_handle, BUTTON_DOUBLE_CLICK, double_click_cb);
        iot_button_register_cb(btn_handle, BUTTON_LONG_PRESS_START, factory_reset_trigger);
        ESP_LOGI(TAG, "Boot button HAL registered on GPIO %d (Click: Toggle, Double: Hue, Long: Reset)", LIGHT_BUTTON_GPIO);
    }

    /* 2. Configure WS2812B Hardware SPI2 DMA @ 3.2MHz */
    light_driver_config_t driver_config = {
        .gpio_ws2812 = LIGHT_WS2818_GPIO,
        .num_leds    = LIGHT_WS2818_NUM_LEDS,
    };
    ESP_ERROR_CHECK(light_driver_init(&driver_config));
    ESP_LOGI(TAG, "WS2812B Hardware SPI2 DMA initialized on GPIO %d (%d LEDs)", LIGHT_WS2818_GPIO, LIGHT_WS2818_NUM_LEDS);

    /* 3. Apply default initial state */
    light_driver_set_hsv(g_hue, g_saturation, g_brightness);
    light_driver_set_switch(g_output_state);
}

int IRAM_ATTR app_driver_set_state(bool state)
{
    if (g_output_state != state) {
        g_output_state = state;
        ESP_LOGI(TAG, "Hardware Light State: %s", g_output_state ? "ON" : "OFF");
        light_driver_set_switch(g_output_state);
    }
    return ESP_OK;
}

bool app_driver_get_state(void)
{
    return g_output_state;
}

/* Biến điều khiển chuyển màu mượt mà (Smooth Fading) */
static float s_current_brightness = (float)DEFAULT_BRIGHTNESS;
static float s_target_brightness  = (float)DEFAULT_BRIGHTNESS;
static float s_current_hue        = (float)DEFAULT_HUE;
static float s_target_hue         = (float)DEFAULT_HUE;
static float s_current_sat        = (float)DEFAULT_SATURATION;
static float s_target_sat         = (float)DEFAULT_SATURATION;

static TimerHandle_t s_smooth_fade_timer = NULL;
static TimerHandle_t s_lazy_save_timer   = NULL;

/* Helper chuyển đổi HSV sang RGB (0-255) */
static void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b);

/* Helper cập nhật màu nền cho hiệu ứng Blink và Breathing tức thì */
static void update_active_effect_color(void)
{
    if (g_output_state) {
        uint8_t r = 0, g = 0, b = 0;
        hsv_to_rgb(g_hue, g_saturation, g_brightness, &r, &g, &b);
        if (r == 0 && g == 0 && b == 0) {
            r = 0; g = 255; b = 255;
        }

        if (strcasecmp(s_current_effect, "Blink") == 0) {
            light_driver_blink_start(r, g, b);
        } else if (strcasecmp(s_current_effect, "Breathing") == 0) {
            light_driver_breath_start(r, g, b);
        }
    }
}

/* Timer lưu cấu hình NVS sau khi người dùng dừng thao tác thanh trượt 1.5s (tránh nghẽn Flash) */
static void lazy_save_timer_cb(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "[Lazy Save] Persisting light state to NVS: H=%d, S=%d, B=%d",
             g_hue, g_saturation, g_brightness);
    if (is_animated_effect()) {
        /* Đang chạy hiệu ứng: Không gọi light_driver_set_hsv để tránh ngắt hiệu ứng */
        return;
    }

    if (g_output_state) {
        light_driver_set_hsv(g_hue, g_saturation, g_brightness);
    } else {
        /* Đèn đang tắt: Chỉ cập nhật cấu hình nội bộ mà không bật sáng phần cứng LED */
        light_driver_set_hsv(g_hue, g_saturation, g_brightness);
        light_driver_set_switch(false);
    }
}

static void schedule_lazy_save(void)
{
    if (s_lazy_save_timer == NULL) {
        s_lazy_save_timer = xTimerCreate("lazy_save", pdMS_TO_TICKS(1500), pdFALSE, NULL, lazy_save_timer_cb);
    }
    if (s_lazy_save_timer != NULL) {
        xTimerReset(s_lazy_save_timer, 0);
    }
}

/* Timer nội suy làm mượt (Smooth Fade) chạy 50Hz (20ms/bước) */
static void smooth_fade_timer_cb(TimerHandle_t xTimer)
{
    bool is_busy = false;

    // 1. Nội suy Brightness (mượt mà từng bước 10-15%)
    float diff_b = s_target_brightness - s_current_brightness;
    if (diff_b > 0.5f || diff_b < -0.5f) {
        s_current_brightness += diff_b * 0.35f;
        is_busy = true;
    } else {
        s_current_brightness = s_target_brightness;
    }

    // 2. Nội suy Hue (chuyển màu êm dịu)
    float diff_h = s_target_hue - s_current_hue;
    if (diff_h > 1.0f || diff_h < -1.0f) {
        s_current_hue += diff_h * 0.35f;
        is_busy = true;
    } else {
        s_current_hue = s_target_hue;
    }

    // 3. Nội suy Saturation
    float diff_s = s_target_sat - s_current_sat;
    if (diff_s > 0.5f || diff_s < -0.5f) {
        s_current_sat += diff_s * 0.35f;
        is_busy = true;
    } else {
        s_current_sat = s_target_sat;
    }

    /* Không xuất màu tĩnh đè lên hiệu ứng đang chạy, kể cả Police của bản B. */
    if (is_animated_effect()) {
        if (!is_busy) {
            xTimerStop(s_smooth_fade_timer, 0);
        }
        return;
    }

    if (g_output_state) {
        uint8_t r = 0, g = 0, b = 0;
        hsv_to_rgb((uint16_t)s_current_hue, (uint8_t)s_current_sat, (uint8_t)s_current_brightness, &r, &g, &b);
        ws2812_set_all(r, g, b);
        ws2812_refresh();
    }

    if (!is_busy) {
        xTimerStop(s_smooth_fade_timer, 0);
    }
}

static void trigger_smooth_fade(void)
{
    if (s_smooth_fade_timer == NULL) {
        s_smooth_fade_timer = xTimerCreate("smooth_fade", pdMS_TO_TICKS(20), pdTRUE, NULL, smooth_fade_timer_cb);
    }
    if (s_smooth_fade_timer != NULL) {
        xTimerStart(s_smooth_fade_timer, 0);
    }
}

esp_err_t app_light_set_power(bool power)
{
    g_output_state = power;
    ESP_LOGI(TAG, "app_light_set_power: %s", power ? "ON" : "OFF");

    if (!power) {
        /* POWER OFF: Dừng triệt để tất cả hiệu ứng nền và xóa LED vật lý */
        stop_rainbow_task();
        stop_police_timer();
        light_driver_blink_stop();
        light_driver_breath_stop();
        if (s_smooth_fade_timer != NULL) {
            xTimerStop(s_smooth_fade_timer, 0);
        }
        light_driver_set_switch(false);
    } else {
        /* POWER ON: Khôi phục hiệu ứng đã cấu hình hoặc hiển thị màu tĩnh gần nhất */
        light_driver_set_switch(true);
        if (is_animated_effect()) {
            return app_light_set_effect(s_current_effect);
        } else {
            /* Hiển thị tĩnh: đồng bộ giá trị float nội suy và kích hoạt fade mượt */
            s_current_brightness = 0.0f; // Hiệu ứng bừng sáng êm khi vừa bật nguồn
            s_target_brightness = (float)g_brightness;
            s_target_hue = (float)g_hue;
            s_target_sat = (float)g_saturation;
            trigger_smooth_fade();
        }
    }
    return ESP_OK;
}

esp_err_t app_light_set(uint32_t hue, uint32_t saturation, uint32_t brightness)
{
    g_hue = (uint16_t)hue;
    g_saturation = (uint8_t)saturation;
    g_brightness = (uint8_t)brightness;

    s_target_hue = (float)g_hue;
    s_target_sat = (float)g_saturation;
    s_target_brightness = (float)g_brightness;

    if (strcasecmp(s_current_effect, "Blink") == 0 || strcasecmp(s_current_effect, "Breathing") == 0) {
        update_active_effect_color();
    } else {
        trigger_smooth_fade();
    }
    schedule_lazy_save();
    return ESP_OK;
}

esp_err_t app_light_set_brightness(uint16_t brightness)
{
    g_brightness = (uint8_t)brightness;
    s_target_brightness = (float)g_brightness;

    if (strcasecmp(s_current_effect, "Blink") == 0 || strcasecmp(s_current_effect, "Breathing") == 0) {
        update_active_effect_color();
    } else {
        trigger_smooth_fade();
    }
    schedule_lazy_save();
    return ESP_OK;
}

esp_err_t app_light_set_hue(uint16_t hue)
{
    g_hue = hue;
    s_target_hue = (float)g_hue;

    if (strcasecmp(s_current_effect, "Blink") == 0 || strcasecmp(s_current_effect, "Breathing") == 0) {
        update_active_effect_color();
    } else {
        trigger_smooth_fade();
    }
    schedule_lazy_save();
    return ESP_OK;
}

esp_err_t app_light_set_saturation(uint16_t saturation)
{
    g_saturation = (uint8_t)saturation;
    s_target_sat = (float)g_saturation;

    if (strcasecmp(s_current_effect, "Blink") == 0 || strcasecmp(s_current_effect, "Breathing") == 0) {
        update_active_effect_color();
    } else {
        trigger_smooth_fade();
    }
    schedule_lazy_save();
    return ESP_OK;
}


/* Helper chuyển đổi HSV (0-360, 0-100, 0-100) sang RGB (0-255) */
static void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    h = h % 360;
    uint16_t hi = (h / 60) % 6;
    uint16_t F = 100 * h / 60 - 100 * hi;
    uint16_t P = v * (100 - s) / 100;
    uint16_t Q = v * (10000 - F * s) / 10000;
    uint16_t T = v * (10000 - s * (100 - F)) / 10000;

    switch (hi) {
        case 0: *r = v; *g = T; *b = P; break;
        case 1: *r = Q; *g = v; *b = P; break;
        case 2: *r = P; *g = v; *b = T; break;
        case 3: *r = P; *g = Q; *b = v; break;
        case 4: *r = T; *g = P; *b = v; break;
        case 5: *r = v; *g = P; *b = Q; break;
        default: *r = 0; *g = 0; *b = 0; break;
    }

    *r = (uint8_t)(*r * 255 / 100);
    *g = (uint8_t)(*g * 255 / 100);
    *b = (uint8_t)(*b * 255 / 100);
}

/* Task xử lý hiệu ứng cầu vồng xoay dải màu (Rainbow Effect) */
static TaskHandle_t s_rainbow_task_handle = NULL;

static void rainbow_effect_task(void *arg)
{
    uint16_t rainbow_hue = 0;
    while (1) {
        light_driver_set_hsv(rainbow_hue, 100, g_brightness > 0 ? g_brightness : 50);
        rainbow_hue = (rainbow_hue + 5) % 360;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void stop_rainbow_task(void)
{
    if (s_rainbow_task_handle != NULL) {
        vTaskDelete(s_rainbow_task_handle);
        s_rainbow_task_handle = NULL;
    }
}

#if APP_ENABLE_POLICE_EFFECT
static TimerHandle_t s_police_timer = NULL;
static bool s_police_blue_phase = false;

static void police_effect_timer_cb(TimerHandle_t timer)
{
    /* Ignore a queued callback after power-off or a switch to another effect. */
    if (!g_output_state || strcmp(s_current_effect, "Police") != 0) {
        return;
    }
    uint8_t brightness = g_brightness > 100 ? 100 : g_brightness;
    uint8_t level = (uint8_t)(brightness * 255U / 100U);
    ws2812_set_all(s_police_blue_phase ? 0 : level, 0, s_police_blue_phase ? level : 0);
    ws2812_refresh();
    s_police_blue_phase = !s_police_blue_phase;
}

static esp_err_t start_police_timer(void)
{
    if (s_police_timer == NULL) {
        s_police_timer = xTimerCreate("police", pdMS_TO_TICKS(250), pdTRUE, NULL, police_effect_timer_cb);
    }
    if (s_police_timer == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTimerReset(s_police_timer, 0) != pdPASS) {
        return ESP_FAIL;
    }
    s_police_blue_phase = false;
    return ESP_OK;
}
#endif

static void stop_police_timer(void)
{
#if APP_ENABLE_POLICE_EFFECT
    if (s_police_timer != NULL) {
        xTimerStop(s_police_timer, 0);
    }
#endif
}

esp_err_t app_light_set_effect(const char *effect)
{
    if (!effect) return ESP_ERR_INVALID_ARG;

    /* Canonical names come from the same list advertised to RainMaker. This also
     * avoids overlapping strncpy when Power ON restores s_current_effect.
     */
    const char *selected = NULL;
    for (size_t i = 0; i < sizeof(s_effect_names) / sizeof(s_effect_names[0]); ++i) {
        if (strcasecmp(effect, s_effect_names[i]) == 0) {
            selected = s_effect_names[i];
            break;
        }
    }
    if (selected == NULL) {
        ESP_LOGW(TAG, "Unsupported effect: '%s'", effect);
        return ESP_ERR_INVALID_ARG;
    }

#if APP_ENABLE_POLICE_EFFECT
    /* Reserve/start the timer before changing modes so a failure leaves the
     * existing effect selected. Its callback waits for the Police mode below.
     */
    if (g_output_state && strcmp(selected, "Police") == 0) {
        esp_err_t err = start_police_timer();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Cannot start Police effect: %s", esp_err_to_name(err));
            return err;
        }
    } else
#endif
    {
        stop_police_timer();
    }

    ESP_LOGI(TAG, "app_light_set_effect: '%s'", selected);
    strcpy(s_current_effect, selected);

    stop_rainbow_task();

    /* Nếu đèn đang tắt, chỉ ghi nhận chế độ hiệu ứng cho lần bật sau, không bật phần cứng */
    if (!g_output_state) {
        light_driver_blink_stop();
        light_driver_breath_stop();
        return ESP_OK;
    }

    uint8_t r = 0, g = 0, b = 0;
    hsv_to_rgb(g_hue, g_saturation, g_brightness, &r, &g, &b);
    if (r == 0 && g == 0 && b == 0) {
        /* Đảm bảo có màu khi bật hiệu ứng */
        r = 0; g = 255; b = 255;
    }

    if (strcmp(selected, "Blink") == 0) {
        light_driver_breath_stop();
        light_driver_blink_start(r, g, b);
    } else if (strcmp(selected, "Breathing") == 0) {
        light_driver_blink_stop();
        light_driver_breath_start(r, g, b);
    } else if (strcmp(selected, "Rainbow") == 0) {
        light_driver_blink_stop();
        light_driver_breath_stop();
        if (s_rainbow_task_handle == NULL) {
            xTaskCreate(rainbow_effect_task, "rainbow_task", 2048, NULL, 5, &s_rainbow_task_handle);
        }
#if APP_ENABLE_POLICE_EFFECT
    } else if (strcmp(selected, "Police") == 0) {
        light_driver_blink_stop();
        light_driver_breath_stop();
        /* The timer writes RGB frames directly, preserving the selected HSV
         * and avoiding NVS writes on every colour change.
         */
#endif
    } else {
        /* "Static" hoặc chế độ thường: Dừng tất cả hiệu ứng và hiển thị màu tĩnh */
        light_driver_blink_stop();
        light_driver_breath_stop();
        light_driver_set_hsv(g_hue, g_saturation, g_brightness);
    }

    return ESP_OK;
}

