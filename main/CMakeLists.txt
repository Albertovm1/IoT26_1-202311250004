idf_component_register(
    SRCS
        "main.c"
        "ds18b20.c"
        "zigbee_handler.c"
    INCLUDE_DIRS "."
    REQUIRES
        espressif__esp-zigbee-lib
        nvs_flash
        esp_timer
        driver
)