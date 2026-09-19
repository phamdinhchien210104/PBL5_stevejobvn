/*
 * ESP32-C3 Smart Light Driver Layer
 * Practice: Section 6.5 Adding Drivers to Smart Light Project
 * - 6.5.1 Button Driver (GPIO 9 Boot button, BUTTON_PRESS_UP callback)
 * - 6.5.2 LED Dimming Driver & NVS Persistence (LIGHT_STATUS_STORE_KEY)
 * - 6.5.3 API Control System (light_driver_set_switch, rgb, ctb, breath, blink)
 * - Mapped to Hardware WS2812B NeoPixel 8-bit LED strip
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

// Bảng màu sắc mẫu để đổi màu khi test trên chip
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
static app_light_mode_t s_light_mode = LIGHT_MODE_NORMAL;

/**
 * @brief Hàm callback khi nhả nút bấm vật lý (BUTTON_PRESS_UP)
 * Mục 6.5.1: Thực hiện lật trạng thái (toggle) bật/tắt đèn khi nhả nút bấm Boot
 */
static void push_btn_cb(void *arg)
{
    bool cur_state = light_driver_get_switch();
    bool new_state = !cur_state;
    ESP_LOGI(TAG, "==> [Nút Boot GPIO%d - BUTTON_PRESS_UP] Lật trạng thái đèn: %s -> %s",
             LIGHT_BUTTON_GPIO,
             cur_state ? "BẬT" : "TẮT",
             new_state ? "BẬT" : "TẮT");

    app_driver_set_state(new_state);
}

/**
 * @brief Hàm callback khi nhấn đúp nút (BUTTON_DOUBLE_CLICK)
 * Tiện ích hỗ trợ test nhanh: Đổi màu kế tiếp trong bảng màu và lưu Flash
 */
static void double_click_cb(void *arg)
{
    // Nếu đèn đang tắt thì bật lên trước
    if (!light_driver_get_switch()) {
        light_driver_set_switch(true);
    }
    app_driver_next_color();
}

/**
 * @brief Hàm callback khi nhấn giữ nút (BUTTON_LONG_PRESS_START)
 * Tiện ích hỗ trợ test nhanh: Bật/tắt hiệu ứng thở (Breathing Mode)
 */
static void long_press_cb(void *arg)
{
    app_driver_toggle_mode();
}

/**
 * @brief Đổi sang màu kế tiếp trong bảng màu mẫu
 */
void app_driver_next_color(void)
{
    s_color_idx = (s_color_idx + 1) % NUM_COLORS;
    ESP_LOGI(TAG, "==> [Double Click] Đổi màu LED: %s (R:%d, G:%d, B:%d)",
             s_colors[s_color_idx].name,
             s_colors[s_color_idx].r,
             s_colors[s_color_idx].g,
             s_colors[s_color_idx].b);

    // Gọi API của light_driver theo Mục 6.5.3 (tự động ghi đè trạng thái mới xuống Flash)
    light_driver_set_rgb(s_colors[s_color_idx].r,
                         s_colors[s_color_idx].g,
                         s_colors[s_color_idx].b);
}

/**
 * @brief Chuyển đổi giữa chế độ sáng bình thường và hiệu ứng Dimming thở (Breathing)
 */
void app_driver_toggle_mode(void)
{
    if (s_light_mode == LIGHT_MODE_NORMAL) {
        s_light_mode = LIGHT_MODE_DIMMING;
        ESP_LOGI(TAG, "==> [Long Press] Kích hoạt hiệu ứng Dimming (Thở / Breathing)");
        light_driver_breath_start(s_colors[s_color_idx].r,
                                 s_colors[s_color_idx].g,
                                 s_colors[s_color_idx].b);
    } else {
        s_light_mode = LIGHT_MODE_NORMAL;
        ESP_LOGI(TAG, "==> [Long Press] Trở về chế độ sáng tĩnh bình thường (Normal)");
        light_driver_breath_stop();
    }
}

app_light_mode_t app_driver_get_mode(void)
{
    return s_light_mode;
}

int app_driver_set_state(bool state)
{
    // Gọi API của light_driver theo Mục 6.5.3 (tự động cập nhật NVS Flash)
    return light_driver_set_switch(state);
}

bool app_driver_get_state(void)
{
    return light_driver_get_switch();
}

/**
 * @brief Khởi tạo Button Driver và Light Driver
 * Đáp ứng đầy đủ quy trình kỹ thuật Mục 6.5.1 và 6.5.2
 */
void app_driver_init(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  Khởi tạo Tầng Driver (Button + Light Driver + NVS)       ");
    ESP_LOGI(TAG, "==========================================================");

    /* 1. Cấu hình Driver Nút bấm vật lý (Mục 6.5.1) */
    ESP_LOGI(TAG, "1. Cấu hình Nút Boot: GPIO %d, Active Level %d",
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
        // Đăng ký hàm phản hồi push_btn_cb qua sự kiện BUTTON_PRESS_UP theo đúng yêu cầu
        iot_button_register_cb(btn_handle, BUTTON_PRESS_UP, push_btn_cb);

        // Đăng ký thêm BUTTON_SINGLE_CLICK dự phòng và các tương tác nhanh
        iot_button_register_cb(btn_handle, BUTTON_SINGLE_CLICK, push_btn_cb);
        iot_button_register_cb(btn_handle, BUTTON_DOUBLE_CLICK, double_click_cb);
        iot_button_register_cb(btn_handle, BUTTON_LONG_PRESS_START, long_press_cb);

        ESP_LOGI(TAG, "Đã đăng ký callback nút bấm thành công:");
        ESP_LOGI(TAG, " - Nhả nút (BUTTON_PRESS_UP) / Nhấn 1 lần: Lật trạng thái Bật/Tắt đèn");
        ESP_LOGI(TAG, " - Nhấn 2 lần (Double click): Đổi màu sắc (RGB)");
        ESP_LOGI(TAG, " - Nhấn giữ (Long press): Bật/Tắt hiệu ứng thở (Breathing)");
    } else {
        ESP_LOGE(TAG, "Khởi tạo iot_button thất bại!");
    }

    /* 2. Khởi tạo bộ thông số LEDC / Light Driver (Mục 6.5.2) */
    ESP_LOGI(TAG, "2. Cấu hình bộ thông số driver_config cho WS2812B (GPIO %d, %d hạt)",
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

    // Kích hoạt phần cứng bằng lệnh ESP_ERROR_CHECK
    // Bên dưới light_driver_init sẽ tự động gọi app_storage_get("light_status") để khôi phục Flash
    ESP_ERROR_CHECK(light_driver_init(&driver_config));

    ESP_LOGI(TAG, "Hoàn tất khởi tạo tầng Driver Layer! Đèn hiện tại: %s",
             light_driver_get_switch() ? "BẬT (ON)" : "TẮT (OFF)");
}
