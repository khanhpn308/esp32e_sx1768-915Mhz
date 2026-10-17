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
 * Provides hardware reset, SPI register access primitives, state machine transitions,
 * RF modem/power configuration, and register verification for 915 MHz baseline profile.
 */

/**
 * @brief Driver operational states for SX1276 state machine (RAD-012).
 */
typedef enum {
    SX1276_STATE_UNINIT = 0,    /**< Driver uninitialized or deinitialized */
    SX1276_STATE_SLEEP,         /**< Radio in Sleep mode (FSK or LoRa) */
    SX1276_STATE_STANDBY,       /**< Radio in Standby mode */
    SX1276_STATE_FAULT,         /**< Radio in fault state after timeout or communication failure */
} sx1276_state_t;

/**
 * @brief Power amplifier output pin selection (RAD-010, DEC-04).
 */
typedef enum {
    SX1276_PA_PIN_RFO = 0,      /**< RFO pin, max +14 dBm */
    SX1276_PA_PIN_BOOST = 1,    /**< PA_BOOST pin, max +17 dBm (or +20 dBm with PA_DAC) */
} sx1276_pa_pin_t;

/**
 * @brief LoRa signal bandwidth enum (RegModemConfig1 bits [7:4]).
 */
typedef enum {
    SX1276_BW_7_8_KHZ   = 0,    /**< 7.8 kHz */
    SX1276_BW_10_4_KHZ  = 1,    /**< 10.4 kHz */
    SX1276_BW_15_6_KHZ  = 2,    /**< 15.6 kHz */
    SX1276_BW_20_8_KHZ  = 3,    /**< 20.8 kHz */
    SX1276_BW_31_25_KHZ = 4,    /**< 31.25 kHz */
    SX1276_BW_41_7_KHZ  = 5,    /**< 41.7 kHz */
    SX1276_BW_62_5_KHZ  = 6,    /**< 62.5 kHz */
    SX1276_BW_125_KHZ   = 7,    /**< 125 kHz (Baseline profile) */
    SX1276_BW_250_KHZ   = 8,    /**< 250 kHz */
    SX1276_BW_500_KHZ   = 9,    /**< 500 kHz */
} sx1276_bw_t;

/**
 * @brief LoRa error coding rate enum (RegModemConfig1 bits [3:1]).
 */
typedef enum {
    SX1276_CR_4_5 = 1,          /**< 4/5 (Baseline profile) */
    SX1276_CR_4_6 = 2,          /**< 4/6 */
    SX1276_CR_4_7 = 3,          /**< 4/7 */
    SX1276_CR_4_8 = 4,          /**< 4/8 */
} sx1276_cr_t;

/**
 * @brief LoRa spreading factor enum (RegModemConfig2 bits [7:4]).
 */
typedef enum {
    SX1276_SF_6  = 6,           /**< 64 chips / symbol */
    SX1276_SF_7  = 7,           /**< 128 chips / symbol (Baseline profile) */
    SX1276_SF_8  = 8,           /**< 256 chips / symbol */
    SX1276_SF_9  = 9,           /**< 512 chips / symbol */
    SX1276_SF_10 = 10,          /**< 1024 chips / symbol */
    SX1276_SF_11 = 11,          /**< 2048 chips / symbol */
    SX1276_SF_12 = 12,          /**< 4096 chips / symbol */
} sx1276_sf_t;

/**
 * @brief RF modem and transceiver configuration parameters.
 */
typedef struct {
    uint32_t frequency_hz;      /**< Carrier frequency in Hz (e.g. 915000000U for 915.0 MHz) */
    uint8_t bandwidth;          /**< Bandwidth setting from sx1276_bw_t (e.g. SX1276_BW_125_KHZ) */
    uint8_t coding_rate;        /**< Coding rate from sx1276_cr_t (e.g. SX1276_CR_4_5) */
    uint8_t spreading_factor;   /**< Spreading factor from sx1276_sf_t (e.g. SX1276_SF_7) */
    bool explicit_header;       /**< true for explicit header mode, false for implicit */
    bool crc_enabled;           /**< true to enable payload CRC on RX/TX */
    uint8_t sync_word;          /**< Network sync word (0x12 for private LoRa network) */
    uint16_t preamble_length;   /**< Preamble length in symbols (e.g. 8) */
    sx1276_pa_pin_t pa_pin;     /**< Power amplifier output pin (SX1276_PA_PIN_BOOST) */
    int8_t tx_power_dbm;        /**< Target TX power in dBm (e.g. +5 dBm) */
} sx1276_config_t;

/* --- Phase 1 Primitives (Preserved) --- */

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

/* --- Phase 2 State Machine APIs --- */

/**
 * @brief Get the current driver operational state (RAD-012).
 *
 * @return Current driver state from sx1276_state_t.
 */
sx1276_state_t sx1276_get_state(void);

/**
 * @brief Transition SX1276 to FSK Sleep mode (RAD-005 Step 1-2).
 *
 * Writes RegOpMode (0x01) = 0x00 and verifies via bounded read-back that
 * LongRangeMode = 0 and Mode = 000 (Sleep).
 *
 * @note Context: Task context only.
 *
 * @return
 *      - ESP_OK: Transition verified successfully
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - ESP_ERR_TIMEOUT: Transition timed out after bounded retries
 *      - Other esp_err_t codes from SPI communication
 */
esp_err_t sx1276_set_fsk_sleep_mode(void);

/**
 * @brief Transition SX1276 to LoRa Sleep mode (RAD-005 Step 3-4).
 *
 * While in Sleep mode, writes RegOpMode (0x01) = 0x80 to enable LoRa mode
 * (LongRangeMode = 1, Mode = 000 Sleep). Verifies transition via bounded read-back.
 *
 * @note Context: Task context only. Device must be in Sleep mode prior to calling.
 *
 * @return
 *      - ESP_OK: Transition verified successfully
 *      - ESP_ERR_INVALID_STATE: Device not in Sleep mode or uninitialized
 *      - ESP_ERR_TIMEOUT: Transition timed out after bounded retries
 *      - Other esp_err_t codes from SPI communication
 */
esp_err_t sx1276_set_lora_sleep_mode(void);

/**
 * @brief Transition SX1276 to LoRa Standby mode (RAD-005 Step 5-6).
 *
 * Writes RegOpMode (0x01) = 0x81 (LongRangeMode = 1, Mode = 001 Standby) and
 * verifies transition via bounded read-back.
 *
 * @note Context: Task context only.
 *
 * @return
 *      - ESP_OK: Transition verified successfully
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - ESP_ERR_TIMEOUT: Transition timed out after bounded retries
 *      - Other esp_err_t codes from SPI communication
 */
esp_err_t sx1276_set_lora_standby_mode(void);

/* --- Phase 2 RF Configuration APIs --- */

/**
 * @brief Set the RF carrier frequency in Hz.
 *
 * Computes Frf using 64-bit integer arithmetic: Frf = (freq_hz * 2^19) / 32000000.
 * Writes RegFrfMsb (0x06), RegFrfMid (0x07), RegFrfLsb (0x08) and reads back each byte.
 * For 915,000,000 Hz, registers resolve to MSB=0xE4, MID=0xC0, LSB=0x00 (0xE4C000).
 *
 * @note Context: Task context only. Thread-safety: single owner task.
 *
 * @param[in] freq_hz Carrier frequency in Hz (e.g. 915000000U).
 *
 * @return
 *      - ESP_OK: Frequency written and verified
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - ESP_ERR_INVALID_RESPONSE: Read-back mismatch
 *      - Other esp_err_t codes from SPI communication
 */
esp_err_t sx1276_set_frequency(uint32_t freq_hz);

/**
 * @brief Configure LoRa modem parameters (BW, CR, SF, Header mode, CRC).
 *
 * Configures RegModemConfig1 (0x1D) and RegModemConfig2 (0x1E), and performs read-back verification.
 * For BW=125kHz, CR=4/5, Explicit Header: RegModemConfig1 = 0x72.
 * For SF=7, CRC=Enabled, TxContinuous=0: RegModemConfig2 = 0x74.
 *
 * @note Context: Task context only. Thread-safety: single owner task.
 *
 * @param[in] bw Bandwidth code (0x07 for 125 kHz).
 * @param[in] cr Coding rate code (0x01 for 4/5).
 * @param[in] sf Spreading factor (7 for SF7).
 * @param[in] explicit_header true for explicit header mode, false for implicit.
 * @param[in] crc_enabled true to enable payload CRC.
 *
 * @return
 *      - ESP_OK: Modem configuration written and verified
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - ESP_ERR_INVALID_RESPONSE: Read-back mismatch
 *      - Other esp_err_t codes from SPI communication
 */
esp_err_t sx1276_set_modem_config(uint8_t bw, uint8_t cr, uint8_t sf, bool explicit_header, bool crc_enabled);

/**
 * @brief Configure the LoRa sync word.
 *
 * Writes RegSyncWord (0x39) and reads back to verify. Default is 0x12 for private networks.
 *
 * @note Context: Task context only.
 *
 * @param[in] sync_word Network sync word (0x12 for private LoRa).
 *
 * @return
 *      - ESP_OK: Sync word written and verified
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - ESP_ERR_INVALID_RESPONSE: Read-back mismatch
 */
esp_err_t sx1276_set_sync_word(uint8_t sync_word);

/**
 * @brief Configure the LoRa preamble length in symbols.
 *
 * Writes RegPreambleMsb (0x20) and RegPreambleLsb (0x21) and verifies via read-back.
 * For 8 symbols: MSB = 0x00, LSB = 0x08.
 *
 * @note Context: Task context only.
 *
 * @param[in] preamble_length Number of preamble symbols (minimum 6, typically 8).
 *
 * @return
 *      - ESP_OK: Preamble length written and verified
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - ESP_ERR_INVALID_RESPONSE: Read-back mismatch
 */
esp_err_t sx1276_set_preamble_length(uint16_t preamble_length);

/**
 * @brief Configure power amplifier path and target TX output power.
 *
 * Configures RegPaConfig (0x09) and RegPaDac (0x4D) based on selected output path.
 * Baseline: PA_BOOST with +5 dBm bench test power (RAD-010, DEC-04).
 *
 * @note Context: Task context only. TX mode is not enabled in Phase 2 (RAD-011).
 *
 * @param[in] pa_pin Output pin selection (SX1276_PA_PIN_BOOST or SX1276_PA_PIN_RFO).
 * @param[in] power_dbm Output power in dBm (+2 to +17 dBm for PA_BOOST, -4 to +14 dBm for RFO).
 *
 * @return
 *      - ESP_OK: Power amplifier configuration written and verified
 *      - ESP_ERR_INVALID_ARG: Power level out of range for selected pin
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - ESP_ERR_INVALID_RESPONSE: Read-back mismatch
 */
esp_err_t sx1276_set_power_config(sx1276_pa_pin_t pa_pin, int8_t power_dbm);

/**
 * @brief Apply complete RF configuration profile to the SX1276 transceiver.
 *
 * Sequence applies frequency, modem configuration, sync word, preamble, and PA settings.
 *
 * @note Context: Task context only. Device must be in SLEEP or STANDBY mode.
 *
 * @param[in] config Pointer to RF configuration structure. Must not be NULL.
 *
 * @return
 *      - ESP_OK: All configuration parameters applied successfully
 *      - ESP_ERR_INVALID_ARG: config pointer is NULL
 *      - ESP_ERR_INVALID_STATE: Driver not initialized or in invalid state
 *      - Other esp_err_t codes from underlying setter functions
 */
esp_err_t sx1276_configure_radio(const sx1276_config_t *config);

/**
 * @brief Verify radio configuration registers against expected profile values.
 *
 * Performs comprehensive read-back of RegFrfMsb/Mid/Lsb, RegModemConfig1,
 * RegModemConfig2, RegSyncWord, RegPreambleMsb/Lsb, and RegPaConfig.
 * Compares against expected values using appropriate bitmasks.
 *
 * @note Context: Task context only.
 *
 * @param[in] config Pointer to expected RF configuration structure. Must not be NULL.
 *
 * @return
 *      - ESP_OK: All configuration registers verified and matched
 *      - ESP_ERR_INVALID_ARG: config pointer is NULL
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - ESP_ERR_INVALID_RESPONSE: One or more registers failed verification
 */
esp_err_t sx1276_verify_radio_config(const sx1276_config_t *config);

#ifdef __cplusplus
}
#endif
