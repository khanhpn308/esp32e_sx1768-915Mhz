idf_component_register(
    SRCS "sx1276.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES board_config esp_driver_spi esp_driver_gpio esp_timer freertos
)
