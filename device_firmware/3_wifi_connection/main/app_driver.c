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

// Chu kỳ và tham số Dimming vô cấp (Stepless Dimming)
#define DIM_STEP_DIVIDER_TICKS 12   /**< 12 ticks x 5ms = 60ms mỗi nhịp điều chỉnh độ sáng */
#define DIM_STEP_PERCENT       2    /**< Thay đổi 2% mỗi nhịp (~2.88 giây quét toàn dải 5% - 100%) */
#define DIM_MIN_PERCENT        5    /**< Ngưỡng sáng tối thiểu an toàn (không tắt đen) */
#define DIM_MAX_PERCENT        100  /**< Ngưỡng sáng tối đa */

typedef enum {
    DIM_DIR_DOWN = 0,
    DIM_DIR_UP   = 1,
} dim_direction_t;

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

// Biến trạng thái quản lý Dimming vô cấp
static bool s_is_dimming = false;
static dim_direction_t s_dim_direction = DIM_DIR_DOWN;
static uint32_t s_dim_tick_counter = 0;
static uint8_t s_current_brightness = 100;

/**
 * @brief Callback khi nhấn 1 lần (Single click / Release): Bật / Tắt đèn chốt trạng thái
 */
static void single_click_cb(void *arg)
{
    bool cur_state = light_driver_get_switch();
    bool new_state = !cur_state;
    ESP_LOGI(TAG, "==> [Single Click GPIO%d] Lật trạng thái Bật/Tắt (Chốt): %s -> %s",
             LIGHT_BUTTON_GPIO,
             cur_state ? "BẬT (ON)" : "TẮT (OFF)",
             new_state ? "BẬT (ON)" : "TẮT (OFF)");
    light_driver_set_switch(new_state);
}

/**
 * @brief Callback khi nhấn đúp (Double click): Đổi màu sắc tiếp theo
 */
static void double_click_cb(void *arg)
{
    if (!light_driver_get_switch()) {
        light_driver_set_switch(true);
    }
    s_color_idx = (s_color_idx + 1) % NUM_COLORS;
    ESP_LOGI(TAG, "==> [Double Click] Đổi màu LED: %s (R:%d, G:%d, B:%d)",
             s_colors[s_color_idx].name,
             s_colors[s_color_idx].r,
             s_colors[s_color_idx].g,
             s_colors[s_color_idx].b);
    light_driver_set_rgb(s_colors[s_color_idx].r,
                         s_colors[s_color_idx].g,
                         s_colors[s_color_idx].b);
}

/**
 * @brief Callback khi bắt đầu nhấn giữ (BUTTON_LONG_PRESS_START): Bắt đầu Dimming vô cấp
 */
static void long_press_start_cb(void *arg)
{
    s_is_dimming = true;
    s_dim_tick_counter = 0;
    s_current_brightness = light_driver_get_brightness();

    // Nếu đèn đang tắt -> Tự động bật lên ở mức tối thiểu và đặt hướng tăng sáng
    if (!light_driver_get_switch()) {
        s_current_brightness = DIM_MIN_PERCENT;
        s_dim_direction = DIM_DIR_UP;
        light_driver_set_switch(true);
        light_driver_set_brightness(s_current_brightness);
        ESP_LOGI(TAG, "==> [Long Press Start] Đèn đang tắt -> Tự động BẬT ở mức %d%% và bắt đầu TĂNG sáng",
                 DIM_MIN_PERCENT);
    } else {
        if (s_current_brightness >= DIM_MAX_PERCENT) {
            s_dim_direction = DIM_DIR_DOWN;
        } else if (s_current_brightness <= DIM_MIN_PERCENT) {
            s_dim_direction = DIM_DIR_UP;
        }
        ESP_LOGI(TAG, "==> [Long Press Start] Bắt đầu Dimming vô cấp (Hướng: %s, Mức sáng hiện tại: %d%%)",
                 s_dim_direction == DIM_DIR_UP ? "TĂNG (Up)" : "GIẢM (Down)",
                 s_current_brightness);
    }
}

/**
 * @brief Callback duy trì khi nhấn giữ (BUTTON_LONG_PRESS_HOLD): Điều chỉnh độ sáng mượt mà
 */
static void long_press_hold_cb(void *arg)
{
    if (!s_is_dimming) {
        return;
    }

    s_dim_tick_counter++;
    if (s_dim_tick_counter < DIM_STEP_DIVIDER_TICKS) {
        return;
    }
    s_dim_tick_counter = 0;

    if (s_dim_direction == DIM_DIR_UP) {
        if (s_current_brightness < DIM_MAX_PERCENT) {
            if (s_current_brightness + DIM_STEP_PERCENT >= DIM_MAX_PERCENT) {
                s_current_brightness = DIM_MAX_PERCENT;
                ESP_LOGI(TAG, "==> [Stepless Dimming] Đạt độ sáng cực đại (MAX %d%%) -> DỪNG", DIM_MAX_PERCENT);
            } else {
                s_current_brightness += DIM_STEP_PERCENT;
            }
            light_driver_set_brightness(s_current_brightness);
        }
    } else { // DIM_DIR_DOWN
        if (s_current_brightness > DIM_MIN_PERCENT) {
            if (s_current_brightness <= DIM_MIN_PERCENT + DIM_STEP_PERCENT) {
                s_current_brightness = DIM_MIN_PERCENT;
                ESP_LOGI(TAG, "==> [Stepless Dimming] Đạt độ sáng tối thiểu (MIN %d%%) -> DỪNG", DIM_MIN_PERCENT);
            } else {
                s_current_brightness -= DIM_STEP_PERCENT;
            }
            light_driver_set_brightness(s_current_brightness);
        }
    }
}

/**
 * @brief Callback khi nhả phím giữ (BUTTON_PRESS_UP): Chốt độ sáng, đảo chiều dimming, KHÔNG tắt đèn
 */
static void press_up_cb(void *arg)
{
    if (s_is_dimming) {
        s_is_dimming = false;
        s_dim_direction = (s_dim_direction == DIM_DIR_UP) ? DIM_DIR_DOWN : DIM_DIR_UP;
        ESP_LOGI(TAG, "==> [Long Press Release] Đã chốt độ sáng: %d%% và lưu NVS. Lần nhấn giữ tới sẽ: %s",
                 s_current_brightness,
                 s_dim_direction == DIM_DIR_UP ? "TĂNG (Up)" : "GIẢM (Down)");
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
        // Đăng ký các sự kiện phản hồi phím bấm:
        // 1. Nhấn 1 lần: Đảo trạng thái Bật/Tắt chốt (Latching Toggle)
        iot_button_register_cb(btn_handle, BUTTON_SINGLE_CLICK, single_click_cb);

        // 2. Nhấn đúp: Đổi màu sắc 8 màu RGB
        iot_button_register_cb(btn_handle, BUTTON_DOUBLE_CLICK, double_click_cb);

        // 3. Nhấn giữ: Bắt đầu và duy trì Dimming vô cấp đảo chiều
        iot_button_register_cb(btn_handle, BUTTON_LONG_PRESS_START, long_press_start_cb);
        iot_button_register_cb(btn_handle, BUTTON_LONG_PRESS_HOLD, long_press_hold_cb);

        // 4. Nhả phím giữ: Chốt độ sáng, đảo chiều dimming, KHÔNG tắt đèn
        iot_button_register_cb(btn_handle, BUTTON_PRESS_UP, press_up_cb);

        ESP_LOGI(TAG, "Đã đăng ký callback nút bấm thành công:");
        ESP_LOGI(TAG, " - Nhấn 1 lần (Single click): Lật trạng thái Bật/Tắt đèn (Chốt)");
        ESP_LOGI(TAG, " - Nhấn 2 lần (Double click): Đổi màu sắc (8 màu RGB)");
        ESP_LOGI(TAG, " - Nhấn giữ (Long press hold): Dimming vô cấp tự dừng tại 100%% hoặc 5%% (~2.8s)");
        ESP_LOGI(TAG, " - Nhả nút giữ (Release): Chốt độ sáng vào NVS, đảo chiều dim cho lần sau");
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

    // Khởi tạo trạng thái dimming theo độ sáng vừa khôi phục từ Flash
    s_current_brightness = light_driver_get_brightness();
    if (s_current_brightness == 0) {
        s_current_brightness = 100;
    }
    s_dim_direction = (s_current_brightness >= 50) ? DIM_DIR_DOWN : DIM_DIR_UP;

    ESP_LOGI(TAG, "Khởi tạo driver thành công! Trạng thái đèn hiện tại: %s | Độ sáng: %d%%",
             light_driver_get_switch() ? "BẬT (ON)" : "TẮT (OFF)",
             s_current_brightness);
}
