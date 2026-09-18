#include "esp_err.h"
#include "button_adc.h"

esp_err_t __attribute__((weak)) button_adc_init(const button_adc_config_t *config)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t __attribute__((weak)) button_adc_deinit(adc1_channel_t channel, int button_index)
{
    return ESP_ERR_NOT_SUPPORTED;
}

uint8_t __attribute__((weak)) button_adc_get_key_level(void *button_index)
{
    return 0;
}
