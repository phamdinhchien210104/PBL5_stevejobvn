/*
 * ESP32 Smart Light Example - Chapter 8: Local Control in Smart Light Project
 *
 * Driver implementation:
 * 1. Physical Boot button with Multi-Gestures (Click, Double Click, Long Press)
 * 2. WS2812B NeoPixel 8-Bit Strip via Hardware SPI2 DMA @ 3.2MHz
 * 3. Dynamic Visual LED indication for Wi-Fi & Local Control state
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

static bool g_output_state = true;
static bool s_is_breathing = false;
static uint8_t s_color_index = 0;

/* Bảng màu mẫu 8 màu RGB trực quan khi nhấn đúp nút Boot */
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

/**
 * @brief Callback khi nhấn 1 lần (Single click): Bật / Tắt đèn
 */
static void push_btn_cb(void *arg)
{
    g_output_state = !g_output_state;
    ESP_LOGI(TAG, "==> [Nút Boot - Nhấn 1 lần]: %s Đèn", g_output_state ? "BẬT" : "TẮT");
    app_driver_set_state(g_output_state);
}

/**
 * @brief Callback khi nhấn đúp (Double click): Đổi màu sắc tiếp theo
 */
static void double_click_cb(void *arg)
{
    if (!g_output_state) {
        g_output_state = true;
        light_driver_set_switch(true);
    }
    if (s_is_breathing) {
        light_driver_breath_stop();
        s_is_breathing = false;
    }
    s_color_index = (s_color_index + 1) % NUM_COLORS;
    uint8_t r = s_colors[s_color_index][0];
    uint8_t g = s_colors[s_color_index][1];
    uint8_t b = s_colors[s_color_index][2];
    ESP_LOGI(TAG, "==> [Nút Boot - Nhấn 2 lần]: Đổi màu [%d/%d] -> R=%d, G=%d, B=%d",
             s_color_index + 1, (int)NUM_COLORS, r, g, b);
    light_driver_set_rgb(r, g, b);
}

/**
 * @brief Callback khi nhấn giữ (Long press): Bật / Tắt hiệu ứng thở (Breathing)
 */
static void long_press_cb(void *arg)
{
    if (!g_output_state) {
        g_output_state = true;
        light_driver_set_switch(true);
    }
    s_is_breathing = !s_is_breathing;
    if (s_is_breathing) {
        uint8_t r = s_colors[s_color_index][0];
        uint8_t g = s_colors[s_color_index][1];
        uint8_t b = s_colors[s_color_index][2];
        ESP_LOGI(TAG, "==> [Nút Boot - Nhấn Giữ]: BẬT hiệu ứng Thở (Breathing) theo màu R=%d, G=%d, B=%d", r, g, b);
        light_driver_breath_start(r, g, b);
    } else {
        ESP_LOGI(TAG, "==> [Nút Boot - Nhấn Giữ]: TẮT hiệu ứng Thở -> Giữ sáng tĩnh");
        light_driver_breath_stop();
        light_driver_set_rgb(s_colors[s_color_index][0],
                             s_colors[s_color_index][1],
                             s_colors[s_color_index][2]);
    }
}

int app_driver_set_state(bool state)
{
    g_output_state = state;
    if (s_is_breathing) {
        light_driver_breath_stop();
        s_is_breathing = false;
    }
    ESP_LOGI(TAG, "==> [Driver] Cập nhật trạng thái đèn: %s", state ? "BẬT (ON)" : "TẮT (OFF)");
    return light_driver_set_switch(state);
}

bool app_driver_get_state(void)
{
    return g_output_state;
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
    if (s_is_breathing) {
        light_driver_breath_stop();
        s_is_breathing = false;
    }
    ESP_LOGI(TAG, "==> [Driver] Đặt màu RGB: R=%d, G=%d, B=%d", red, green, blue);
    return light_driver_set_rgb(red, green, blue);
}

void app_driver_set_wifi_status(wifi_status_t status)
{
    switch (status) {
    case WIFI_STATUS_CONNECTING:
        ESP_LOGI(TAG, "==> [Wi-Fi LED] Đang kết nối tới Router Wi-Fi... (Đèn thở Vàng/Cam)");
        light_driver_set_switch(true);
        light_driver_breath_start(255, 160, 0);
        s_is_breathing = true;
        break;

    case WIFI_STATUS_CONNECTED:
        ESP_LOGI(TAG, "==> [Wi-Fi LED] Đã có địa chỉ IP! (Đèn xanh lá cây tĩnh)");
        light_driver_breath_stop();
        s_is_breathing = false;
        light_driver_set_switch(true);
        light_driver_set_rgb(0, 255, 0);
        break;

    case WIFI_STATUS_FAILED:
        ESP_LOGE(TAG, "==> [Wi-Fi LED] Kết nối thất bại sau số lần thử lại! (Đèn đỏ cảnh báo)");
        light_driver_breath_stop();
        s_is_breathing = false;
        light_driver_set_switch(true);
        light_driver_set_rgb(255, 0, 0);
        break;

    default:
        break;
    }
}

void app_driver_pulse_feedback(uint8_t r, uint8_t g, uint8_t b)
{
    /* Nháy nhẹ LED để báo nhận lệnh thành công */
    light_driver_set_rgb(r, g, b);
}

void app_driver_init(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  Khởi tạo Tầng Driver Chương 8: Local Control Smart Light");
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
        iot_button_register_cb(btn_handle, BUTTON_SINGLE_CLICK, push_btn_cb);
        iot_button_register_cb(btn_handle, BUTTON_DOUBLE_CLICK, double_click_cb);
        iot_button_register_cb(btn_handle, BUTTON_LONG_PRESS_START, long_press_cb);

        ESP_LOGI(TAG, "Đã đăng ký callback nút bấm:");
        ESP_LOGI(TAG, " - Nhấn 1 lần (Click): Bật/Tắt đèn");
        ESP_LOGI(TAG, " - Nhấn 2 lần (Double Click): Đổi 8 màu sắc (RGB)");
        ESP_LOGI(TAG, " - Nhấn giữ (Long Press): Bật/Tắt hiệu ứng thở");
    } else {
        ESP_LOGE(TAG, "Khởi tạo iot_button thất bại!");
    }

    /* 2. Cấu hình Thanh LED WS2812B (8 hạt trên GPIO 4 qua Hardware SPI DMA) */
    ESP_LOGI(TAG, "2. Cấu hình Thanh LED WS2812B: Chân DIN GPIO %d, Số hạt: %d",
             LIGHT_WS2818_GPIO, LIGHT_WS2818_NUM_LEDS);

    light_driver_config_t driver_config = {
        .gpio_ws2812 = LIGHT_WS2818_GPIO,
        .num_leds    = LIGHT_WS2818_NUM_LEDS,
    };
    ESP_ERROR_CHECK(light_driver_init(&driver_config));

    g_output_state = light_driver_get_switch();
    ESP_LOGI(TAG, "Khởi tạo driver thành công! Trạng thái đèn ban đầu: %s",
             g_output_state ? "BẬT" : "TẮT");
}
