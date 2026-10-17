#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file sx1276.h
 * @brief Public interface for Semtech SX1276 LoRa transceiver driver.
 *
 * Provides hardware reset, SPI register access primitives, and device
 * initialization/version validation.
 */

/**
 * @brief Perform a hardware reset sequence on the SX1276 transceiver.
 *
 * Configures the BOARD_LORA_RST pin as an output, drives it LOW for 10 ms
 * (exceeding the 100 us minimum required by SX1276 datasheet section 7.2.2),
 * releases it to HIGH, and delays for 10 ms to allow internal circuits
 * and the crystal oscillator to fully stabilize.
 *
 * @note Context: Task context only (uses vTaskDelay).
 *
 * @return
 *      - ESP_OK: Reset sequence completed successfully
 *      - ESP_ERR_INVALID_ARG: Invalid GPIO pin parameter
 *      - ESP_FAIL: Failed to configure GPIO or set output level
 */
esp_err_t sx1276_reset_device(void);

/**
 * @brief Read a single 8-bit register from the SX1276 via SPI.
 *
 * Transmits the register address with bit 7 cleared (read command) and clocks out
 * the register content using a 16-bit SPI transaction with internal buffers (MEM-013).
 *
 * @note Context: Task context. Thread-safety: single owner task or external synchronization.
 *
 * @param[in] reg 7-bit register address (0x00 to 0x7F).
 * @param[out] out_val Pointer to receive the 8-bit register value. Must not be NULL.
 *
 * @return
 *      - ESP_OK: Read completed successfully
 *      - ESP_ERR_INVALID_ARG: out_val pointer is NULL
 *      - ESP_ERR_INVALID_STATE: Driver not initialized or SPI device handle is NULL
 *      - Other esp_err_t codes from spi_device_transmit()
 */
esp_err_t sx1276_read_register(uint8_t reg, uint8_t *out_val);

/**
 * @brief Write a single 8-bit value to an SX1276 register via SPI.
 *
 * Transmits the register address with bit 7 set (write command) followed by the data byte
 * using a 16-bit SPI transaction with internal buffers (MEM-013).
 *
 * @note Context: Task context. Thread-safety: single owner task or external synchronization.
 *
 * @param[in] reg 7-bit register address (0x00 to 0x7F).
 * @param[in] val 8-bit value to write to the register.
 *
 * @return
 *      - ESP_OK: Write completed successfully
 *      - ESP_ERR_INVALID_STATE: Driver not initialized or SPI device handle is NULL
 *      - Other esp_err_t codes from spi_device_transmit()
 */
esp_err_t sx1276_write_register(uint8_t reg, uint8_t val);

/**
 * @brief Initialize the SX1276 transceiver and verify silicon version.
 *
 * Performs hardware reset, attaches the SX1276 to the initialized SPI bus
 * at 4 MHz (Mode 0) using hardware-managed CS (BOARD_LORA_CS), reads RegVersion (0x42),
 * and verifies that the silicon version equals 0x12 (RAD-004).
 *
 * @note Context: Task context only. board_init_spi_bus() must be called prior to this function.
 *
 * @return
 *      - ESP_OK: SX1276 initialized and RegVersion 0x12 confirmed
 *      - ESP_ERR_INVALID_RESPONSE: Silicon version mismatch (value read was not 0x12)
 *      - Other esp_err_t codes from GPIO reset or SPI communication
 */
esp_err_t sx1276_init_device(void);

/**
 * @brief Deinitialize the SX1276 transceiver and release SPI device resources.
 *
 * Removes the SX1276 device from the SPI bus and resets the internal handle.
 *
 * @return
 *      - ESP_OK: Device deinitialized successfully
 *      - ESP_ERR_INVALID_STATE: Device not currently initialized
 */
esp_err_t sx1276_deinit_device(void);

#ifdef __cplusplus
}
#endif
