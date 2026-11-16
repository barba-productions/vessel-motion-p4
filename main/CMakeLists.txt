set(PRIVATE_COMPONENTS esp_eth esp_netif esp_event esp_http_server esp_http_client
    esp_partition esp_app_format app_update esp_timer nvs_flash fatfs sdmmc esp_driver_gpio
    esp_driver_i2c esp_driver_sdmmc esp_hw_support mbedtls json esp_system spi_flash
    joltwallet__littlefs)
if("$ENV{VESSEL_WIRELESS_ALLOWED}" STREQUAL "1")
    list(APPEND PRIVATE_COMPONENTS esp_wifi espressif__esp_wifi_remote espressif__esp_hosted)
endif()
idf_component_register(SRCS "main.c" "motion.c" "sensor.c" "network.c" "webserver.c" "storage.c" "store.c" "wifi_mgr.c" "time_mgr.c" "ota.c" "settings.c" "overview.c"
                       INCLUDE_DIRS "." PRIV_REQUIRES ${PRIVATE_COMPONENTS})
target_compile_definitions(${COMPONENT_LIB} PRIVATE WIRELESS_ALLOWED=$ENV{VESSEL_WIRELESS_ALLOWED})
