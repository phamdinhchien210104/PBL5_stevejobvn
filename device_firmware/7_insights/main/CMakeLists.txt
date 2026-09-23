set(srcs "app_main.c" "app_pm.c" "app_driver.c" "app_insights.c")
set(include_dirs "include")

if(CONFIG_IDF_TARGET_ESP32S3)
    set(DEVELOPMENT_BOARD "board_esp32s3_devkitc.h")
else()
    set(DEVELOPMENT_BOARD "board_esp32c3_devkitc.h")
endif()

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "${include_dirs}"
                       EMBED_TXTFILES "server.crt"
                       REQUIRES esp_rainmaker esp_insights esp_diagnostics light_driver button app_storage app_wifi esp_pm esp_timer app_update)

idf_build_set_property(COMPILE_DEFINITIONS "-DDEVELOPMENT_BOARD=\"${DEVELOPMENT_BOARD}\"" APPEND)
