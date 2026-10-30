set(ota_ca "${PROJECT_DIR}/.local/certs/ca_cert.pem")
if(NOT EXISTS "${ota_ca}")
    message(FATAL_ERROR
        "Prepare the local OTA certificate first: python scripts/ota_server.py prepare --host <PC-IP>")
endif()

idf_component_register(SRCS "advanced_https_ota_example.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES app_update esp_app_format esp_event esp_http_client
                                     esp_https_ota esp_netif esp_wifi nvs_flash protocol_examples_common
                       EMBED_TXTFILES "${ota_ca}")
