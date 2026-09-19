/*
 * ESP32 Smart Light - Chapter 3: Wi-Fi Connection
 * Driver Layer & Wi-Fi Visual Feedback System
 * - Button Driver (Boot button GPIO 0 / GPIO 9)
 * - WS2812B NeoPixel 8-bit LED strip on GPIO 4 via Hardware SPI2 DMA
 * - NVS State Persistence & Wi-Fi Status Indicators
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "iot_button.h"
#include "light_driver.h"

#include DEVELOPMENT_BOARD
#include "app_priv.h"

#define TAG "app_driver"

// Bảng màu mẫu hỗ trợ người dùng đổi màu nhanh
typedef struct {
    const char *name;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} color_item_t;

static const color_item_t s_colors[] = {
    {"Trắng ấm (Warm White)", 255, 160, 80 },
    {"Đỏ (Red)",               255, 0,   0  },
    {"Xanh lá (Green)",        0,   255, 0  },
    {"Xanh dương (Blue)",      0,   0,   255},
    {"Vàng (Yellow)",          255, 200, 0  },
    {"Tím (Purple)",           200, 0,   255},
    {"Xanh ngọc (Cyan)",       0,   255, 255},
    {"Trắng lạnh (Cool White)",255, 255, 255},
};
#define NUM_COLORS (sizeof(s_colors) / sizeof(s_colors[0]))
static size_t s_color_idx = 0;
static bool s_is_breathing = false;

static void push_btn_cb(void *arg)
{
    bool cur_state = light_driver_get_switch();
    bool new_state = !cur_state;
    ESP_LOGI(TAG, "[Nút Boot GPIO%d] Lật trạng thái đèn: %s -> %s",
             LIGHT_BUTTON_GPIO,
             cur_state ? "BẬT" : "TẮT",
             new_state ? "BẬT" : "TẮT");
    light_driver_set_switch(new_state);
}

static void double_click_cb(void *arg)
{
    if (!light_driver_get_switch()) {
        light_driver_set_switch(true);
    }
    s_color_idx = (s_color_idx + 1) % NUM_COLORS;
    ESP_LOGI(TAG, "[Double Click] Đổi màu LED: %s (R:%d, G:%d, B:%d)",
             s_colors[s_color_idx].name,
             s_colors[s_color_idx].r,
             s_colors[s_color_idx].g,
             s_colors[s_color_idx].b);
    light_driver_set_rgb(s_colors[s_color_idx].r,
                         s_colors[s_color_idx].g,
                         s_colors[s_color_idx].b);
}

static void long_press_cb(void *arg)
{
    if (!s_is_breathing) {
        s_is_breathing = true;
        ESP_LOGI(TAG, "[Long Press] Bật hiệu ứng Breathing (Thở)");
        light_driver_breath_start(s_colors[s_color_idx].r,
                                 s_colors[s_color_idx].g,
                                 s_colors[s_color_idx].b);
    } else {
        s_is_breathing = false;
        ESP_LOGI(TAG, "[Long Press] Tắt hiệu ứng Breathing (Thở)");
        light_driver_breath_stop();
    }
}

int app_driver_set_state(bool state)
{
    return light_driver_set_switch(state);
}

bool app_driver_get_state(void)
{
    return light_driver_get_switch();
}

/**
 * @brief Điều khiển màu đèn WS2812B phản hồi trực quan trạng thái kết nối Wi-Fi
 */
void app_driver_set_wifi_status(wifi_status_t status)
{
    switch (status) {
    case WIFI_STATUS_CONNECTING:
        ESP_LOGI(TAG, "==> [Wi-Fi LED Feedback] Đang kết nối AP... (Đèn thở Vàng/Cam)");
        light_driver_set_switch(true);
        light_driver_breath_start(255, 160, 0);
        break;

    case WIFI_STATUS_CONNECTED:
        ESP_LOGI(TAG, "==> [Wi-Fi LED Feedback] Đã nhận IP thành công! (Đèn xanh lá)");
        light_driver_breath_stop();
        light_driver_set_switch(true);
        light_driver_set_rgb(0, 255, 0);
        break;

    case WIFI_STATUS_FAILED:
        ESP_LOGE(TAG, "==> [Wi-Fi LED Feedback] Kết nối thất bại sau số lần thử! (Đèn đỏ cảnh báo)");
        light_driver_breath_stop();
        light_driver_set_switch(true);
        light_driver_set_rgb(255, 0, 0);
        break;

    default:
        break;
    }
}

void app_driver_init(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  Khởi tạo Tầng Driver Chương 3: Wi-Fi & Smart Light      ");
    ESP_LOGI(TAG, "==========================================================");

    /* 1. Cấu hình Nút nhấn vật lý (Nút Boot trên S3 GPIO 0 / C3 GPIO 9) */
    ESP_LOGI(TAG, "1. Cấu hình Nút bấm: GPIO %d, Active Level %d",
             LIGHT_BUTTON_GPIO, LIGHT_BUTTON_ACTIVE_LEVEL);

    button_config_t btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num     = LIGHT_BUTTON_GPIO,
            .active_level = LIGHT_BUTTON_ACTIVE_LEVEL,
        },
    };

    button_handle_t btn_handle = iot_button_create(&btn_cfg);
    if (btn_handle) {
        iot_button_register_cb(btn_handle, BUTTON_PRESS_UP, push_btn_cb);
        iot_button_register_cb(btn_handle, BUTTON_SINGLE_CLICK, push_btn_cb);
        iot_button_register_cb(btn_handle, BUTTON_DOUBLE_CLICK, double_click_cb);
        iot_button_register_cb(btn_handle, BUTTON_LONG_PRESS_START, long_press_cb);

        ESP_LOGI(TAG, "Đã đăng ký callback nút bấm:");
        ESP_LOGI(TAG, " - Nhấn 1 lần (Click / Release): Bật/Tắt đèn");
        ESP_LOGI(TAG, " - Nhấn 2 lần (Double Click): Đổi màu sắc (RGB)");
        ESP_LOGI(TAG, " - Nhấn giữ (Long Press): Bật/Tắt hiệu ứng thở");
    } else {
        ESP_LOGE(TAG, "Khởi tạo iot_button thất bại!");
    }

    /* 2. Cấu hình Driver Thanh LED WS2812B 8-Bit NeoPixel trên GPIO 4 */
    ESP_LOGI(TAG, "2. Cấu hình Thanh LED WS2812B: Chân DIN GPIO %d, Số hạt: %d",
             LIGHT_WS2818_GPIO, LIGHT_WS2818_NUM_LEDS);

    light_driver_config_t driver_config = {
        .gpio_ws2812     = LIGHT_WS2818_GPIO,
        .num_leds        = LIGHT_WS2818_NUM_LEDS,
        .gpio_red        = LIGHT_GPIO_RED,
        .gpio_green      = LIGHT_GPIO_GREEN,
        .gpio_blue       = LIGHT_GPIO_BLUE,
        .gpio_cold       = LIGHT_GPIO_COLD,
        .gpio_warm       = LIGHT_GPIO_WARM,
        .fade_period_ms  = LIGHT_FADE_PERIOD_MS,
        .blink_period_ms = LIGHT_BLINK_PERIOD_MS,
        .freq_hz         = LIGHT_FREQ_HZ,
        .clk_cfg         = LEDC_USE_APB_CLK,
        .duty_resolution = LEDC_TIMER_11_BIT,
    };

    ESP_ERROR_CHECK(light_driver_init(&driver_config));
    ESP_LOGI(TAG, "Khởi tạo driver thành công! Trạng thái đèn hiện tại: %s",
             light_driver_get_switch() ? "BẬT" : "TẮT");
}
