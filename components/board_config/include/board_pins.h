#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file board_pins.h
 * @brief Locked GPIO mapping and hardware configuration for ESP32 + SX1276 PCB.
 *
 * All hardware pins defined here are confirmed for the target PCB revision (HW-001).
 * Do NOT alter, swap, or reassign these pins without explicit project owner approval.
 */

#define BOARD_LORA_DIO0 GPIO_NUM_26
#define BOARD_LORA_RST  GPIO_NUM_23
#define BOARD_LORA_CS   GPIO_NUM_18
#define BOARD_LORA_SCK  GPIO_NUM_5
#define BOARD_LORA_MOSI GPIO_NUM_27
#define BOARD_LORA_MISO GPIO_NUM_19

/**
 * @brief SPI host peripheral assigned to SX1276 (SPI2_HOST = HSPI on ESP32).
 */
#define BOARD_LORA_SPI_HOST SPI2_HOST

/**
 * @brief Initialize the SPI bus for SX1276 transceiver using locked board pins.
 *
 * Configures the SPI master bus with BOARD_LORA_SCK, BOARD_LORA_MOSI,
 * and BOARD_LORA_MISO pins using BOARD_LORA_SPI_HOST.
 *
 * @return
 *      - ESP_OK: SPI bus initialized successfully
 *      - ESP_ERR_INVALID_ARG: Invalid bus configuration parameters
 *      - ESP_ERR_NO_MEM: Insufficient memory to allocate SPI bus
 *      - ESP_ERR_INVALID_STATE: SPI bus already initialized
 */
static inline esp_err_t board_init_spi_bus(void)
{
    const spi_bus_config_t buscfg = {
        .mosi_io_num = BOARD_LORA_MOSI,
        .miso_io_num = BOARD_LORA_MISO,
        .sclk_io_num = BOARD_LORA_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 256,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };

    return spi_bus_initialize(BOARD_LORA_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
}

#ifdef __cplusplus
}
#endif
