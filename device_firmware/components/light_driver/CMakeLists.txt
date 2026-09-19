set(srcs "light_driver.c" "ws2812_driver.c")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_ledc esp_driver_gpio app_storage
                       PRIV_REQUIRES driver esp_driver_spi esp_timer freertos)
