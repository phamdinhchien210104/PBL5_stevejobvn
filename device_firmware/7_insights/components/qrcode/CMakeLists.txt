set(QRCODE_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__qrcode")

idf_component_register(SRCS "${QRCODE_SRC_DIR}/esp_qrcode_main.c"
                            "${QRCODE_SRC_DIR}/esp_qrcode_wrapper.c"
                            "${QRCODE_SRC_DIR}/qrcodegen.c"
                       INCLUDE_DIRS "include"
                                    "${QRCODE_SRC_DIR}/include"
                                    "${QRCODE_SRC_DIR}")
