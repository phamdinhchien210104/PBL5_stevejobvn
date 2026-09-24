/*
 * ESP32 Smart Light Example - Chapter 4: Smart Wi-Fi Provisioning (BLE)
 *
 * Driver implementation:
 * 1. Physical Boot button with Multi-Gestures (Click, Double Click, Long Press)
 * 2. WS2812B NeoPixel 8-Bit Strip via Hardware SPI2 DMA @ 3.2MHz
 * 3. Dynamic Visual LED indication for BLE Provisioning states
 */

#include <stdio.h>
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "iot_button.h"
#include "light_driver.h"

#include DEVELOPMENT_BOARD
#include "app_priv.h"

static const char *TAG = "app_driver";

// Chu kỳ và tham số Dimming vô cấp (Stepless Dimming)
#define DIM_STEP_DIVIDER_TICKS 12   /**< 12 ticks x 5ms = 60ms mỗi nhịp điều chỉnh độ sáng */
#define DIM_STEP_PERCENT       2    /**< Thay đổi 2% mỗi nhịp (~2.88 giây quét toàn dải 5% - 100%) */
#define DIM_MIN_PERCENT        5    /**< Ngưỡng sáng tối thiểu an toàn (không tắt đen) */
#define DIM_MAX_PERCENT        100  /**< Ngưỡng sáng tối đa */

typedef enum {
    DIM_DIR_DOWN = 0,
    DIM_DIR_UP   = 1,
} dim_direction_t;

static bool g_output_state = true;
static uint8_t s_color_index = 0;

// Biến trạng thái quản lý Dimming vô cấp
static bool s_is_dimming = false;
static dim_direction_t s_dim_direction = DIM_DIR_DOWN;
static uint32_t s_dim_tick_counter = 0;
static uint8_t s_current_brightness = 100;

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
 * @brief Callback khi nhấn 1 lần (Single click): Bật / Tắt đèn chốt trạng thái
 */
static void single_click_cb(void *arg)
{
    g_output_state = !g_output_state;
    ESP_LOGI(TAG, "==> [Single Click GPIO%d] Lật trạng thái Bật/Tắt (Chốt): %s",
             LIGHT_BUTTON_GPIO,
             g_output_state ? "BẬT (ON)" : "TẮT (OFF)");
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
    s_color_index = (s_color_index + 1) % NUM_COLORS;
    uint8_t r = s_colors[s_color_index][0];
    uint8_t g = s_colors[s_color_index][1];
    uint8_t b = s_colors[s_color_index][2];
    ESP_LOGI(TAG, "==> [Double Click] Đổi màu [%d/%d] -> R=%d, G=%d, B=%d",
             s_color_index + 1, (int)NUM_COLORS, r, g, b);
    light_driver_set_rgb(r, g, b);
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
    if (!g_output_state) {
        g_output_state = true;
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

int IRAM_ATTR app_driver_set_state(bool state)
{
    g_output_state = state;
    return light_driver_set_switch(state);
}

bool app_driver_get_state(void)
{
    return light_driver_get_switch();
}

/**
 * @brief Điều khiển màu đèn WS2812B phản hồi trực quan trạng thái BLE Provisioning
 */
void app_driver_set_prov_status(prov_status_t status)
{
    switch (status) {
    case PROV_STATUS_WAITING:
        // Đang phát BLE chờ smartphone quét mã / kết nối: Thở Xanh dương (Cyan)
        ESP_LOGI(TAG, "==> [BLE Prov LED] Đang phát quảng bá BLE... (Đèn thở Xanh dương)");
        light_driver_set_switch(true);
        light_driver_breath_start(0, 180, 255);
        break;

    case PROV_STATUS_CONNECTING:
        // Đã nhận SSID/Pass từ smartphone, đang thử kết nối Router: Thở Vàng/Cam
        ESP_LOGI(TAG, "==> [BLE Prov LED] Đã nhận Wi-Fi từ điện thoại, đang kết nối Router... (Đèn thở Vàng)");
        light_driver_set_switch(true);
        light_driver_breath_start(255, 160, 0);
        break;

    case PROV_STATUS_SUCCESS:
        // Cấp phát và kết nối IP thành công: Sáng Xanh lá cây tĩnh
        ESP_LOGI(TAG, "==> [BLE Prov LED] Cấp phát thành công! Đã có IP. (Đèn xanh lá)");
        light_driver_breath_stop();
        light_driver_set_switch(true);
        light_driver_set_rgb(0, 255, 0);
        break;

    case PROV_STATUS_FAILED:
        // Thất bại do sai mật khẩu hoặc router từ chối: Bật màu Đỏ cảnh báo
        ESP_LOGE(TAG, "==> [BLE Prov LED] Cấp phát thất bại (sai mật khẩu Wi-Fi)! (Đèn đỏ cảnh báo)");
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
    ESP_LOGI(TAG, "  Khởi tạo Tầng Driver Chương 4: Smart Wi-Fi Provisioning ");
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

    /* 2. Cấu hình Thanh LED WS2812B (8 hạt trên GPIO 4 qua Hardware SPI DMA) */
    ESP_LOGI(TAG, "2. Cấu hình Thanh LED WS2812B: Chân DIN GPIO %d, Số hạt: %d",
             LIGHT_WS2818_GPIO, LIGHT_WS2818_NUM_LEDS);

    light_driver_config_t driver_config = {
        .gpio_ws2812 = LIGHT_WS2818_GPIO,
        .num_leds    = LIGHT_WS2818_NUM_LEDS,
    };
    ESP_ERROR_CHECK(light_driver_init(&driver_config));

    // Khởi tạo trạng thái dimming theo độ sáng vừa khôi phục từ Flash
    s_current_brightness = light_driver_get_brightness();
    if (s_current_brightness == 0) {
        s_current_brightness = 100;
    }
    s_dim_direction = (s_current_brightness >= 50) ? DIM_DIR_DOWN : DIM_DIR_UP;

    g_output_state = light_driver_get_switch();
    ESP_LOGI(TAG, "Khởi tạo driver thành công! Trạng thái đèn hiện tại: %s | Độ sáng: %d%%",
             g_output_state ? "BẬT (ON)" : "TẮT (OFF)",
             s_current_brightness);
}
