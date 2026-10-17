idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES board_config sx1276_driver esp_driver_spi esp_driver_gpio freertos
)
