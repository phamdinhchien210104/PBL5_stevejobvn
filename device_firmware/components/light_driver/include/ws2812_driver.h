/*
 * WS2818 / WS2812 Driver for ESP32-C3
 * Uses Hardware SPI to transmit precise 800kHz NRZ waveform without jitter.
 */

#ifndef __WS2812_DRIVER_H__
#define __WS2812_DRIVER_H__

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_color_t;

/**
 * @brief Khởi tạo driver WS2818 / WS2812 bằng phần cứng SPI
 * 
 * @param gpio_num Chân GPIO nối vào chân I (Data In) của LED (mặc định GPIO 4)
 * @param num_leds Số lượng hạt LED được nối
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t ws2812_init(int gpio_num, uint16_t num_leds);

/**
 * @brief Đặt màu cho một hạt LED cụ thể
 * 
 * @param index Chỉ số LED (0 .. num_leds - 1)
 * @param r Giá trị màu Đỏ (0 - 255)
 * @param g Giá trị màu Xanh lá (0 - 255)
 * @param b Giá trị màu Xanh dương (0 - 255)
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t ws2812_set_pixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Đặt màu cho toàn bộ các hạt LED
 * 
 * @param r Giá trị màu Đỏ (0 - 255)
 * @param g Giá trị màu Xanh lá (0 - 255)
 * @param b Giá trị màu Xanh dương (0 - 255)
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t ws2812_set_all(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Đặt màu và tỉ lệ độ sáng cho toàn bộ các hạt LED
 * 
 * @param r Giá trị màu Đỏ (0 - 255)
 * @param g Giá trị màu Xanh lá (0 - 255)
 * @param b Giá trị màu Xanh dương (0 - 255)
 * @param brightness_ratio Tỉ lệ độ sáng từ 0.0f đến 1.0f
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t ws2812_set_all_brightness(uint8_t r, uint8_t g, uint8_t b, float brightness_ratio);

/**
 * @brief Tắt toàn bộ LED (màu 0, 0, 0)
 * 
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t ws2812_clear(void);

/**
 * @brief Gửi dữ liệu màu ra chân GPIO của LED
 * 
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t ws2812_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /**< __WS2812_DRIVER_H__ */
