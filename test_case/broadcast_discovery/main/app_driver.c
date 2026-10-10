/*
 * ESP32 Smart Light Example - Broadcast Discovery (Section 8.2.1)
 *
 * Driver implementation:
 * 1. Physical Boot button with Multi-Gestures:
 *    - Single click: Bật / Tắt đèn chốt trạng thái
 *    - Double click: Đổi 8 màu sắc (RGB cycle)
 *    - Long press hold: Dimming vô cấp (5% - 100%)
 *    - Long press release: Chốt độ sáng
 * 2. WS2812B NeoPixel 8-Bit Strip via Hardware SPI2 DMA @ 3.2MHz (GPIO 4)
 */

#include <stdio.h>
#include "esp_log.h"
#include "esp_attr.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "iot_button.h"
#include "light_driver.h"

#include DEVELOPMENT_BOARD
#include "app_priv.h"

static const char *TAG = "app_driver";

#define DIM_STEP_DIVIDER_TICKS 12
#define DIM_STEP_PERCENT       2
#define DIM_MIN_PERCENT        5
#define DIM_MAX_PERCENT        100

typedef enum {
    DIM_DIR_DOWN = 0,
    DIM_DIR_UP   = 1,
} dim_direction_t;

static bool g_output_state = true;
static uint8_t s_color_index = 0;
static bool s_is_dimming = false;
static dim_direction_t s_dim_direction = DIM_DIR_DOWN;
static uint32_t s_dim_tick_counter = 0;
static uint8_t s_current_brightness = 100;

static const uint8_t s_colors[][3] = {
    {255, 0,   0  },  /* 0: Đỏ */
    {0,   255, 0  },  /* 1: Xanh lá */
    {0,   0,   255},  /* 2: Xanh dương */
    {255, 255, 0  },  /* 3: Vàng */
    {255, 0,   255},  /* 4: Tím hồng */
    {0,   255, 255},  /* 5: Xanh lơ (Cyan) */
    {255, 128, 0  },  /* 6: Cam */
    {255, 255, 255},  /* 7: Trắng ấm */
};
#define NUM_COLORS (sizeof(s_colors) / sizeof(s_colors[0]))

static const char *s_color_names[] = {
    "Đỏ (Red)", "Xanh lá (Green)", "Xanh dương (Blue)", "Vàng (Yellow)",
    "Tím hồng (Magenta)", "Xanh lơ (Cyan)", "Cam (Orange)", "Trắng ấm (Warm White)"
};

int app_driver_next_color(void)
{
    if (!g_output_state) {
        g_output_state = true;
        light_driver_set_switch(true);
    }
    s_color_index = (s_color_index + 1) % NUM_COLORS;
    uint8_t r = s_colors[s_color_index][0];
    uint8_t g = s_colors[s_color_index][1];
    uint8_t b = s_colors[s_color_index][2];
    ESP_LOGI(TAG, "==> [Next Color] Đổi màu [%d/%d] (%s) -> R=%d, G=%d, B=%d",
             s_color_index + 1, (int)NUM_COLORS, s_color_names[s_color_index], r, g, b);
    return light_driver_set_rgb(r, g, b);
}

static void single_click_cb(void *arg)
{
    g_output_state = !g_output_state;
    ESP_LOGI(TAG, "==> [Single Click GPIO%d] Lật trạng thái: %s",
             LIGHT_BUTTON_GPIO, g_output_state ? "BẬT (ON)" : "TẮT (OFF)");
    app_driver_set_state(g_output_state);
}

static void double_click_cb(void *arg)
{
    ESP_LOGI(TAG, "==> [Double Click Nút Boot] Chuyển màu kế tiếp...");
    app_driver_next_color();
}

static void long_press_start_cb(void *arg)
{
    s_is_dimming = true;
    s_dim_tick_counter = 0;
    s_current_brightness = light_driver_get_brightness();

    if (!g_output_state) {
        g_output_state = true;
        s_current_brightness = DIM_MIN_PERCENT;
        s_dim_direction = DIM_DIR_UP;
        light_driver_set_switch(true);
        light_driver_set_brightness(s_current_brightness);
    } else {
        if (s_current_brightness >= DIM_MAX_PERCENT) {
            s_dim_direction = DIM_DIR_DOWN;
        } else if (s_current_brightness <= DIM_MIN_PERCENT) {
            s_dim_direction = DIM_DIR_UP;
        }
    }
}

static void long_press_hold_cb(void *arg)
{
    if (!s_is_dimming) return;
    s_dim_tick_counter++;
    if (s_dim_tick_counter < DIM_STEP_DIVIDER_TICKS) return;
    s_dim_tick_counter = 0;

    if (s_dim_direction == DIM_DIR_UP) {
        if (s_current_brightness < DIM_MAX_PERCENT) {
            s_current_brightness = (s_current_brightness + DIM_STEP_PERCENT >= DIM_MAX_PERCENT) ? DIM_MAX_PERCENT : s_current_brightness + DIM_STEP_PERCENT;
            light_driver_set_brightness(s_current_brightness);
        }
    } else {
        if (s_current_brightness > DIM_MIN_PERCENT) {
            s_current_brightness = (s_current_brightness <= DIM_MIN_PERCENT + DIM_STEP_PERCENT) ? DIM_MIN_PERCENT : s_current_brightness - DIM_STEP_PERCENT;
            light_driver_set_brightness(s_current_brightness);
        }
    }
}

static void press_up_cb(void *arg)
{
    if (s_is_dimming) {
        s_is_dimming = false;
        s_dim_direction = (s_dim_direction == DIM_DIR_UP) ? DIM_DIR_DOWN : DIM_DIR_UP;
        ESP_LOGI(TAG, "==> [Long Press Release] Đã chốt độ sáng: %d%%", s_current_brightness);
    }
}

int IRAM_ATTR app_driver_set_state(bool state)
{
    g_output_state = state;
    ESP_LOGI(TAG, "Light %s", state ? "ON" : "OFF");
    return light_driver_set_switch(state);
}

bool app_driver_get_state(void)
{
    return light_driver_get_switch();
}

int app_driver_toggle_state(void)
{
    return app_driver_set_state(!g_output_state);
}

int app_driver_set_color(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!g_output_state) {
        g_output_state = true;
        light_driver_set_switch(true);
    }
    return light_driver_set_rgb(red, green, blue);
}

uint8_t app_driver_get_brightness(void)
{
    return s_current_brightness;
}

int app_driver_set_brightness(uint8_t brightness)
{
    if (brightness < DIM_MIN_PERCENT) brightness = DIM_MIN_PERCENT;
    else if (brightness > DIM_MAX_PERCENT) brightness = DIM_MAX_PERCENT;
    if (!g_output_state) {
        g_output_state = true;
        light_driver_set_switch(true);
    }
    s_current_brightness = brightness;
    return light_driver_set_brightness(s_current_brightness);
}

int app_driver_adjust_brightness(int delta)
{
    return app_driver_set_brightness((int)s_current_brightness + delta);
}

uint8_t app_driver_get_color_index(void)
{
    return s_color_index;
}

const char* app_driver_get_color_name(void)
{
    return (s_color_index < NUM_COLORS) ? s_color_names[s_color_index] : "Unknown";
}

void app_driver_get_rgb(uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t idx = s_color_index % NUM_COLORS;
    if (r) *r = s_colors[idx][0];
    if (g) *g = s_colors[idx][1];
    if (b) *b = s_colors[idx][2];
}

void app_driver_init(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  Khởi tạo Tầng Driver: Broadcast Discovery (Mục 8.2.1)   ");
    ESP_LOGI(TAG, "==========================================================");

    button_config_t btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num     = LIGHT_BUTTON_GPIO,
            .active_level = LIGHT_BUTTON_ACTIVE_LEVEL,
        },
    };
    button_handle_t btn_handle = iot_button_create(&btn_cfg);
    if (btn_handle) {
        iot_button_register_cb(btn_handle, BUTTON_SINGLE_CLICK, single_click_cb);
        iot_button_register_cb(btn_handle, BUTTON_DOUBLE_CLICK, double_click_cb);
        iot_button_register_cb(btn_handle, BUTTON_LONG_PRESS_START, long_press_start_cb);
        iot_button_register_cb(btn_handle, BUTTON_LONG_PRESS_HOLD, long_press_hold_cb);
        iot_button_register_cb(btn_handle, BUTTON_PRESS_UP, press_up_cb);
    }

    light_driver_config_t driver_config = {
        .gpio_ws2812 = LIGHT_WS2818_GPIO,
        .num_leds    = LIGHT_WS2818_NUM_LEDS,
    };
    ESP_ERROR_CHECK(light_driver_init(&driver_config));

    s_current_brightness = light_driver_get_brightness();
    if (s_current_brightness == 0) s_current_brightness = 100;
    g_output_state = light_driver_get_switch();
    ESP_LOGI(TAG, "Khởi tạo driver thành công! Trạng thái đèn: %s | Độ sáng: %d%%",
             g_output_state ? "BẬT" : "TẮT", s_current_brightness);
}
