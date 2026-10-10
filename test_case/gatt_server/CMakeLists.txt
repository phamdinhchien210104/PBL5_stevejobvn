# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

add_compile_options(-fdiagnostics-color=always)

# Prune unneeded ESP-IDF components to accelerate build and prevent GCC 15 ICE
set(COMPONENTS main button app_storage light_driver nvs_flash bt esp_timer)

# Add common application components from device_firmware/components
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../device_firmware/components/light_driver
                        ${CMAKE_CURRENT_LIST_DIR}/../../device_firmware/components/button
                        ${CMAKE_CURRENT_LIST_DIR}/../../device_firmware/components/app_storage
                        )

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gatt_server)
