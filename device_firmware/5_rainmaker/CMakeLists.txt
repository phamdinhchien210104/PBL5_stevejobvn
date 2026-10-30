# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Keep the project name stable; use -DPROJECT_VER=1.1.0 for the OTA image.
if(NOT DEFINED PROJECT_VER)
    set(PROJECT_VER "1.0.0")
endif()

option(APP_ENABLE_POLICE_EFFECT "Include the Police lighting effect in the OTA demo image" OFF)

add_compile_options(-fdiagnostics-color=always)

# Prune unneeded ESP-IDF components to prevent GCC 15 ICE crash on esp_lcd and accelerate build
set(COMPONENTS main button app_storage light_driver app_wifi)

# Add common application components from device_firmware/components
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../components/light_driver
                        ${CMAKE_CURRENT_LIST_DIR}/../components/button
                        ${CMAKE_CURRENT_LIST_DIR}/../components/app_storage
                        ${CMAKE_CURRENT_LIST_DIR}/../components/app_wifi
                        )

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(5_rainmaker)
