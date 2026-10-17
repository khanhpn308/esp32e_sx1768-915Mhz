idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES board_config sx1276_driver app_protocol esp_driver_spi esp_driver_gpio esp_timer freertos
)

