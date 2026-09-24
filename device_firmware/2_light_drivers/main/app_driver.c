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

// Chu kỳ và tham số Dimming vô cấp (Stepless Dimming)
#define DIM_STEP_DIVIDER_TICKS 12   /**< 12 ticks x 5ms = 60ms mỗi nhịp điều chỉnh độ sáng */
#define DIM_STEP_PERCENT       2    /**< Thay đổi 2% mỗi nhịp (~2.88 giây quét toàn dải 5% - 100%) */
#define DIM_MIN_PERCENT        5    /**< Ngưỡng sáng tối thiểu an toàn (không tắt đen) */
#define DIM_MAX_PERCENT        100  /**< Ngưỡng sáng tối đa */

typedef enum {
    DIM_DIR_DOWN = 0,
    DIM_DIR_UP   = 1,
} dim_direction_t;

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

// Biến trạng thái quản lý Dimming vô cấp
static bool s_is_dimming = false;
static dim_direction_t s_dim_direction = DIM_DIR_DOWN;
static uint32_t s_dim_tick_counter = 0;
static uint8_t s_current_brightness = 100;

/**
 * @brief Hàm callback khi nhấn 1 lần (BUTTON_SINGLE_CLICK)
 * Đảo trạng thái Bật/Tắt chốt (Latching Toggle), loại trừ hoàn toàn việc bị lật kép
 */
static void single_click_cb(void *arg)
{
    bool cur_state = light_driver_get_switch();
    bool new_state = !cur_state;
    ESP_LOGI(TAG, "==> [Single Click GPIO%d] Lật trạng thái Bật/Tắt (Chốt): %s -> %s",
             LIGHT_BUTTON_GPIO,
             cur_state ? "BẬT (ON)" : "TẮT (OFF)",
             new_state ? "BẬT (ON)" : "TẮT (OFF)");

    app_driver_set_state(new_state);
}

/**
 * @brief Hàm callback khi nhấn đúp nút (BUTTON_DOUBLE_CLICK)
 * Đổi sang màu kế tiếp trong bảng 8 màu RGB và lưu NVS
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
 * @brief Hàm callback khi bắt đầu nhấn giữ phím (BUTTON_LONG_PRESS_START)
 * Kích hoạt trạng thái Dimming vô cấp
 */
static void long_press_start_cb(void *arg)
{
    s_is_dimming = true;
    s_dim_tick_counter = 0;
    s_current_brightness = light_driver_get_brightness();

    // Nếu đèn đang tắt -> Tự động bật lên ở mức sáng tối thiểu và đặt hướng tăng
    if (!light_driver_get_switch()) {
        s_current_brightness = DIM_MIN_PERCENT;
        s_dim_direction = DIM_DIR_UP;
        light_driver_set_switch(true);
        light_driver_set_brightness(s_current_brightness);
        ESP_LOGI(TAG, "==> [Long Press Start] Đèn đang tắt -> Tự động BẬT ở mức %d%% và bắt đầu TĂNG sáng",
                 DIM_MIN_PERCENT);
    } else {
        // Nếu đã ở kịch trần MAX -> Hướng dim tự chuyển sang GIẢM
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
 * @brief Hàm callback trong lúc đang giữ phím (BUTTON_LONG_PRESS_HOLD)
 * Được iot_button gọi định kỳ mỗi 5ms (TICKS_INTERVAL).
 * Áp dụng bộ chia tần số 60ms / 2% để độ sáng tăng/giảm mượt mà và tự dừng tại biên.
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
 * @brief Hàm callback khi nhả nút bấm vật lý (BUTTON_PRESS_UP)
 * Chỉ xử lý khi vừa kết thúc nhấn giữ Dimming để chốt độ sáng và đảo chiều dimming.
 * Tuyệt đối không can thiệp vào công tắc nguồn (không tắt đèn).
 */
static void press_up_cb(void *arg)
{
    if (s_is_dimming) {
        s_is_dimming = false;
        // Đảo chiều hướng dim cho lần nhấn giữ tiếp theo
        s_dim_direction = (s_dim_direction == DIM_DIR_UP) ? DIM_DIR_DOWN : DIM_DIR_UP;
        ESP_LOGI(TAG, "==> [Long Press Release] Đã chốt độ sáng: %d%% và lưu NVS. Lần nhấn giữ tới sẽ: %s",
                 s_current_brightness,
                 s_dim_direction == DIM_DIR_UP ? "TĂNG (Up)" : "GIẢM (Down)");
    }
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
        ESP_LOGI(TAG, "==> Kích hoạt hiệu ứng Dimming (Thở / Breathing)");
        light_driver_breath_start(s_colors[s_color_idx].r,
                                 s_colors[s_color_idx].g,
                                 s_colors[s_color_idx].b);
    } else {
        s_light_mode = LIGHT_MODE_NORMAL;
        ESP_LOGI(TAG, "==> Trở về chế độ sáng tĩnh bình thường (Normal)");
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

    // Khởi tạo trạng thái dimming theo độ sáng vừa khôi phục từ Flash
    s_current_brightness = light_driver_get_brightness();
    if (s_current_brightness == 0) {
        s_current_brightness = 100;
    }
    s_dim_direction = (s_current_brightness >= 50) ? DIM_DIR_DOWN : DIM_DIR_UP;

    ESP_LOGI(TAG, "Hoàn tất khởi tạo tầng Driver Layer! Đèn hiện tại: %s | Độ sáng: %d%%",
             light_driver_get_switch() ? "BẬT (ON)" : "TẮT (OFF)",
             s_current_brightness);
}
