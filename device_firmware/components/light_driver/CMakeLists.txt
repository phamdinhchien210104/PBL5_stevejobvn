idf_component_register(
    SRCS
        "./light_driver.c"
        "./iot_led.c"

    INCLUDE_DIRS
        "."
        "./include"

    REQUIRES
        app_storage
        esp_driver_gptimer
        esp_driver_ledc
        esp_driver_gpio
)