/*
 * WS2818 / WS2812 Driver for ESP32-C3
 * Uses Hardware SPI (SPI2_HOST) with 3.2MHz clock to output precise 800kHz NRZ waveform.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"
#include "ws2812_driver.h"

static const char *TAG = "ws2812_driver";

#define WS2812_SPI_HOST      SPI2_HOST
#define WS2812_SPI_CLOCK_HZ  3200000       // 3.2 MHz -> 1 bit = 312.5 ns, 4 bits = 1.25 µs
#define WS2812_RESET_BYTES   120           // 120 bytes * 8 * 312.5ns = 300 µs reset pulse (>280µs)

// Encoding 2 WS2812 bits into 1 SPI byte
// WS2812 '0' = 1000b (0x8), WS2812 '1' = 1110b (0xE)
static const uint8_t s_ws2812_byte_lut[4] = {
    0x88, // 0 0 -> 1000 1000
    0x8E, // 0 1 -> 1000 1110
    0xE8, // 1 0 -> 1110 1000
    0xEE  // 1 1 -> 1110 1110
};

static spi_device_handle_t s_spi_handle = NULL;
static rgb_color_t *s_pixel_colors = NULL;
static uint8_t *s_spi_buffer = NULL;
static uint16_t s_num_leds = 0;
static size_t s_spi_buffer_size = 0;

static void encode_byte(uint8_t val, uint8_t *out_4bytes)
{
    // val has 8 bits, each 2 bits map to 1 SPI byte via LUT
    out_4bytes[0] = s_ws2812_byte_lut[(val >> 6) & 0x03];
    out_4bytes[1] = s_ws2812_byte_lut[(val >> 4) & 0x03];
    out_4bytes[2] = s_ws2812_byte_lut[(val >> 2) & 0x03];
    out_4bytes[3] = s_ws2812_byte_lut[val & 0x03];
}

esp_err_t ws2812_init(int gpio_num, uint16_t num_leds)
{
    if (s_spi_handle != NULL) {
        // Đã khởi tạo trước đó
        return ESP_OK;
    }

    if (num_leds == 0) {
        ESP_LOGE(TAG, "Number of LEDs must be > 0");
        return ESP_ERR_INVALID_ARG;
    }

    s_num_leds = num_leds;

    // Allocate memory for logical RGB pixels
    s_pixel_colors = (rgb_color_t *)calloc(s_num_leds, sizeof(rgb_color_t));
    if (!s_pixel_colors) {
        ESP_LOGE(TAG, "Failed to allocate pixel color buffer");
        return ESP_ERR_NO_MEM;
    }

    // SPI buffer size: 24 bits per LED * 4 SPI bits/bit = 96 SPI bits = 12 bytes per LED
    // Plus reset trailing bytes
    s_spi_buffer_size = (s_num_leds * 12) + WS2812_RESET_BYTES;
    s_spi_buffer = (uint8_t *)heap_caps_calloc(1, s_spi_buffer_size, MALLOC_CAP_DMA);
    if (!s_spi_buffer) {
        ESP_LOGE(TAG, "Failed to allocate DMA SPI buffer");
        free(s_pixel_colors);
        s_pixel_colors = NULL;
        return ESP_ERR_NO_MEM;
    }

    // Configure SPI bus
    spi_bus_config_t buscfg = {
        .mosi_io_num = gpio_num,
        .miso_io_num = -1,
        .sclk_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = s_spi_buffer_size + 32,
    };

    esp_err_t ret = spi_bus_initialize(WS2812_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus initialize failed: %s", esp_err_to_name(ret));
        free(s_pixel_colors);
        s_pixel_colors = NULL;
        free(s_spi_buffer);
        s_spi_buffer = NULL;
        return ret;
    }

    // Configure SPI device
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = WS2812_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };

    ret = spi_bus_add_device(WS2812_SPI_HOST, &devcfg, &s_spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus add device failed: %s", esp_err_to_name(ret));
        spi_bus_free(WS2812_SPI_HOST);
        free(s_pixel_colors);
        s_pixel_colors = NULL;
        free(s_spi_buffer);
        s_spi_buffer = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "WS2818/WS2812 initialized on GPIO %d, LEDs count: %d, SPI DMA buffer: %u bytes",
             gpio_num, s_num_leds, (unsigned int)s_spi_buffer_size);

    // Initial clear
    ws2812_clear();
    ws2812_refresh();

    return ESP_OK;
}

esp_err_t ws2812_set_pixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index >= s_num_leds || !s_pixel_colors) {
        return ESP_ERR_INVALID_ARG;
    }
    s_pixel_colors[index].r = r;
    s_pixel_colors[index].g = g;
    s_pixel_colors[index].b = b;
    return ESP_OK;
}

esp_err_t ws2812_set_all(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_pixel_colors) {
        return ESP_ERR_INVALID_STATE;
    }
    for (uint16_t i = 0; i < s_num_leds; i++) {
        s_pixel_colors[i].r = r;
        s_pixel_colors[i].g = g;
        s_pixel_colors[i].b = b;
    }
    return ESP_OK;
}

esp_err_t ws2812_set_all_brightness(uint8_t r, uint8_t g, uint8_t b, float brightness_ratio)
{
    if (brightness_ratio < 0.0f) brightness_ratio = 0.0f;
    if (brightness_ratio > 1.0f) brightness_ratio = 1.0f;

    uint8_t scaled_r = (uint8_t)(r * brightness_ratio);
    uint8_t scaled_g = (uint8_t)(g * brightness_ratio);
    uint8_t scaled_b = (uint8_t)(b * brightness_ratio);

    return ws2812_set_all(scaled_r, scaled_g, scaled_b);
}

esp_err_t ws2812_clear(void)
{
    return ws2812_set_all(0, 0, 0);
}

esp_err_t ws2812_refresh(void)
{
    if (!s_spi_handle || !s_spi_buffer || !s_pixel_colors) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t *ptr = s_spi_buffer;
    for (uint16_t i = 0; i < s_num_leds; i++) {
        // WS2818 and WS2812 standard order is GRB (Green, Red, Blue)
        encode_byte(s_pixel_colors[i].g, ptr);
        ptr += 4;
        encode_byte(s_pixel_colors[i].r, ptr);
        ptr += 4;
        encode_byte(s_pixel_colors[i].b, ptr);
        ptr += 4;
    }

    // Reset code (all zeros for at least 280µs)
    memset(ptr, 0, WS2812_RESET_BYTES);

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = s_spi_buffer_size * 8; // length in bits
    t.tx_buffer = s_spi_buffer;

    return spi_device_transmit(s_spi_handle, &t);
}
