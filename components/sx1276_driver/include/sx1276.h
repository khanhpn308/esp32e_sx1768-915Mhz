#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
    SX1276_STATE_TX,            /**< Radio transmitting packet */
    SX1276_STATE_RX_CONTINUOUS, /**< Radio listening for packets */
    SX1276_STATE_FAULT,         /**< Radio in fault state after timeout or communication failure */
} sx1276_state_t;

/**
 * @brief Received packet signal quality metrics (RSSI and SNR).
 */
typedef struct {
    int8_t raw_snr;             /**< Raw RegPktSnrValue (signed two's complement, 0.25 dB per LSB) */
    uint8_t raw_rssi;           /**< Raw RegPktRssiValue */
    int16_t snr_x100_db;        /**< Scaled SNR in 0.01 dB (e.g. +500 = +5.00 dB, -250 = -2.50 dB) */
    int16_t rssi_dbm;           /**< Estimated packet RSSI in dBm */
} sx1276_rx_metrics_t;

/**
 * @brief Power amplifier output pin selection (RAD-010, DEC-04).
 */
typedef enum {
    SX1276_PA_PIN_RFO = 0,      /**< RFO pin, max +14 dBm */
    SX1276_PA_PIN_BOOST = 1,    /**< PA_BOOST pin, max +17 dBm (or +20 dBm with PA_DAC) */
} sx1276_pa_pin_t;

/**
 * @brief SX1276 DIO0 pin mapping in LoRa mode (RegDioMapping1 bits [7:6]).
 */
typedef enum {
    SX1276_DIO0_MAP_RX_DONE = 0,    /**< 00b: DIO0 mapped to RxDone */
    SX1276_DIO0_MAP_TX_DONE = 1,    /**< 01b: DIO0 mapped to TxDone */
    SX1276_DIO0_MAP_CAD_DONE = 2,   /**< 10b: DIO0 mapped to CadDone */
} sx1276_dio0_mapping_t;

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

/* --- Phase 3 TX/RX Polling and FIFO APIs --- */

/**
 * @brief Transition SX1276 to LoRa TX mode (RegOpMode = 0x83).
 *
 * Writes RegOpMode to start packet transmission.
 *
 * @note Context: Task context only. Device must have FIFO loaded with packet data.
 *
 * @return
 *      - ESP_OK: Mode write succeeded
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - Other esp_err_t codes from SPI communication
 */
esp_err_t sx1276_set_lora_tx_mode(void);

/**
 * @brief Transition SX1276 to LoRa Continuous Receive mode (RegOpMode = 0x85).
 *
 * Writes RegOpMode to start continuous packet reception and verifies transition.
 *
 * @note Context: Task context only.
 *
 * @return
 *      - ESP_OK: Transition to RX continuous mode verified
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - ESP_ERR_TIMEOUT: Transition timed out after bounded retries
 *      - Other esp_err_t codes from SPI communication
 */
esp_err_t sx1276_set_lora_rx_continuous_mode(void);

/**
 * @brief Write data bytes into the SX1276 FIFO buffer via SPI burst write.
 *
 * Uses static DMA-aligned buffers to transmit 1 byte address (0x80) followed by data.
 * The internal FIFO address pointer auto-increments with each byte written.
 *
 * @note Context: Task context only. Buffer ownership: caller retains ownership.
 *
 * @param[in] data Pointer to payload data buffer. Must not be NULL.
 * @param[in] length Number of bytes to write (1 to 255).
 *
 * @return
 *      - ESP_OK: Bytes written to FIFO successfully
 *      - ESP_ERR_INVALID_ARG: data is NULL or length is 0 or > 255
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - Other esp_err_t codes from SPI communication
 */
esp_err_t sx1276_write_fifo(const uint8_t *data, size_t length);

/**
 * @brief Read data bytes from the SX1276 FIFO buffer via SPI burst read.
 *
 * Uses static DMA-aligned buffers to transmit 1 byte address (0x00) and clock in data.
 * The internal FIFO address pointer auto-increments with each byte read.
 *
 * @note Context: Task context only. Buffer ownership: written into caller buffer.
 *
 * @param[out] out_data Pointer to buffer to receive payload bytes. Must not be NULL.
 * @param[in] length Number of bytes to read (1 to 255).
 * @param[in] capacity Capacity of out_data buffer in bytes (must be >= length).
 *
 * @return
 *      - ESP_OK: Bytes read from FIFO successfully
 *      - ESP_ERR_INVALID_ARG: out_data is NULL, length is 0 or > capacity or > 255
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - Other esp_err_t codes from SPI communication
 */
esp_err_t sx1276_read_fifo(uint8_t *out_data, size_t length, size_t capacity);

/**
 * @brief Read the RegIrqFlags (0x12) register.
 *
 * @note Context: Task context only.
 *
 * @param[out] out_flags Pointer to receive 8-bit IRQ flags value. Must not be NULL.
 *
 * @return
 *      - ESP_OK: IRQ flags read successfully
 *      - ESP_ERR_INVALID_ARG: out_flags is NULL
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - Other esp_err_t codes from SPI communication
 */
esp_err_t sx1276_read_irq_flags(uint8_t *out_flags);

/**
 * @brief Clear specific IRQ flags in RegIrqFlags (0x12).
 *
 * SX1276 RegIrqFlags uses write-1-to-clear semantics.
 *
 * @note Context: Task context only.
 *
 * @param[in] flags_to_clear Bitmask of flags to clear (e.g. SX1276_IRQ_TX_DONE_MASK).
 *
 * @return
 *      - ESP_OK: IRQ flags cleared successfully
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - Other esp_err_t codes from SPI communication
 */
esp_err_t sx1276_clear_irq_flags(uint8_t flags_to_clear);

/**
 * @brief Clear all IRQ flags in RegIrqFlags (0x12) by writing 0xFF.
 *
 * @note Context: Task context only.
 *
 * @return
 *      - ESP_OK: All IRQ flags cleared successfully
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - Other esp_err_t codes from SPI communication
 */
esp_err_t sx1276_clear_all_irq_flags(void);

/**
 * @brief Read packet signal quality metrics (SNR and RSSI) from last received packet.
 *
 * Reads RegPktSnrValue (0x19) and RegPktRssiValue (0x1A), converts SNR from 2's complement
 * (0.25 dB per LSB) and computes HF-band RSSI using Semtech SX1276 datasheet formula.
 *
 * @note Context: Task context only. Must be called after RxDone is confirmed.
 *
 * @param[out] out_metrics Pointer to sx1276_rx_metrics_t struct. Must not be NULL.
 *
 * @return
 *      - ESP_OK: Metrics read and converted successfully
 *      - ESP_ERR_INVALID_ARG: out_metrics is NULL
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - Other esp_err_t codes from SPI communication
 */
esp_err_t sx1276_read_packet_metrics(sx1276_rx_metrics_t *out_metrics);

/**
 * @brief Transmit a raw payload and poll RegIrqFlags until TxDone or timeout.
 *
 * Sets Standby mode, resets FIFO pointer to TX base address, writes payload,
 * sets RegPayloadLength, enters TX mode, and polls RegIrqFlags every 10 ms
 * (yielding CPU with vTaskDelay) until TxDone is confirmed or timeout expires.
 * On completion, clears TxDone and returns transceiver to Standby mode.
 *
 * @note Context: Task context only.
 *
 * @param[in] payload Pointer to byte buffer to transmit. Must not be NULL.
 * @param[in] length Number of bytes to transmit (1 to 255).
 * @param[in] timeout_ms Maximum time to wait for TxDone in milliseconds.
 *
 * @return
 *      - ESP_OK: Packet transmitted successfully and TxDone confirmed
 *      - ESP_ERR_INVALID_ARG: payload is NULL or length is 0 or > 255
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - ESP_ERR_TIMEOUT: Timeout expired before TxDone was asserted
 *      - Other esp_err_t codes from SPI communication
 */
esp_err_t sx1276_send_packet(const uint8_t *payload, size_t length, uint32_t timeout_ms);

/**
 * @brief Initialize continuous receive mode on SX1276.
 *
 * Sets Standby mode, configures RegFifoRxBaseAddr to 0x00, points RegFifoAddrPtr to 0x00,
 * clears all previous IRQ flags, and enters RX_CONTINUOUS mode.
 *
 * @note Context: Task context only.
 *
 * @return
 *      - ESP_OK: Continuous RX mode started successfully
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - Other esp_err_t codes from SPI communication
 */
esp_err_t sx1276_start_rx_continuous(void);

/**
 * @brief Poll RegIrqFlags for an incoming packet in continuous receive mode.
 *
 * Polls RegIrqFlags every 10 ms (yielding CPU with vTaskDelay) up to timeout_ms.
 * When RxDone is detected:
 * - Checks PayloadCrcError: if corrupted, clears flags and returns ESP_ERR_INVALID_CRC.
 * - If valid: reads RegRxNbBytes, points RegFifoAddrPtr to RegFifoRxCurrentAddr,
 *   reads payload into out_payload, reads RSSI/SNR metrics, and clears RxDone.
 *
 * @note Context: Task context only.
 *
 * @param[out] out_payload Buffer to receive packet payload. Must not be NULL.
 * @param[in] capacity Maximum capacity of out_payload buffer in bytes.
 * @param[out] out_length Pointer to receive actual number of bytes read. Must not be NULL.
 * @param[out] out_metrics Pointer to receive packet signal metrics (can be NULL if not needed).
 * @param[in] timeout_ms Maximum time to wait for a packet in milliseconds.
 *
 * @return
 *      - ESP_OK: Packet received and validated successfully
 *      - ESP_ERR_INVALID_ARG: out_payload or out_length is NULL, or capacity is 0
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - ESP_ERR_TIMEOUT: Timeout expired with no packet received
 *      - ESP_ERR_INVALID_CRC: Packet received but PayloadCrcError was asserted
 *      - ESP_ERR_INVALID_SIZE: Packet length exceeds buffer capacity
 *      - Other esp_err_t codes from SPI communication
 */
esp_err_t sx1276_poll_rx_packet(uint8_t *out_payload, size_t capacity, size_t *out_length, sx1276_rx_metrics_t *out_metrics, uint32_t timeout_ms);

/* --- Phase 4 DIO0 Interrupt and Event-Driven Radio APIs --- */

/**
 * @brief Configure SX1276 DIO0 pin mapping in RegDioMapping1 (0x40).
 *
 * Performs read-modify-write on RegDioMapping1 to configure DIO0 bits [7:6]
 * while preserving DIO1..DIO3. Verifies the written mapping via read-back.
 *
 * @note Context: Task context only. Device must be initialized.
 *
 * @param[in] mapping Mapping value from sx1276_dio0_mapping_t (e.g. SX1276_DIO0_MAP_RX_DONE).
 *
 * @return
 *      - ESP_OK: Mapping written and verified successfully
 *      - ESP_ERR_INVALID_ARG: Invalid mapping argument
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - ESP_ERR_INVALID_RESPONSE: Read-back verification failed
 *      - Other esp_err_t codes from SPI communication
 */
esp_err_t sx1276_configure_dio0_mapping(sx1276_dio0_mapping_t mapping);

/**
 * @brief Initialize BOARD_LORA_DIO0 GPIO interrupt and register ISR handler.
 *
 * Configures BOARD_LORA_DIO0 as input with rising-edge interrupt (GPIO_INTR_POSEDGE).
 * Installs GPIO ISR service (if not already installed) and attaches an ISR handler
 * that wakes the specified FreeRTOS task via vTaskNotifyGiveFromISR() and portYIELD_FROM_ISR().
 *
 * @note Context: Task context only. Task handle must be non-NULL (RTOS-009).
 *
 * @param[in] task_to_notify FreeRTOS TaskHandle_t to notify upon DIO0 rising edge. Must not be NULL.
 *
 * @return
 *      - ESP_OK: ISR initialized and handler registered successfully
 *      - ESP_ERR_INVALID_ARG: task_to_notify is NULL
 *      - Other esp_err_t codes from gpio_config, gpio_install_isr_service, or gpio_isr_handler_add
 */
esp_err_t sx1276_init_dio0_isr(TaskHandle_t task_to_notify);

/**
 * @brief Remove DIO0 GPIO ISR handler and disable interrupt on BOARD_LORA_DIO0.
 *
 * Detaches the ISR handler and disables interrupts on BOARD_LORA_DIO0.
 *
 * @note Context: Task context only.
 *
 * @return
 *      - ESP_OK: Handler removed and interrupt disabled successfully
 *      - Other esp_err_t codes from gpio_isr_handler_remove or gpio_intr_disable
 */
esp_err_t sx1276_deinit_dio0_isr(void);

/**
 * @brief Process radio hardware event following DIO0 notification or polling.
 *
 * Reads RegIrqFlags (0x12), checks for PayloadCrcError and RxDone:
 * - If PayloadCrcError: clears IRQ flags and returns ESP_ERR_INVALID_CRC.
 * - If RxDone: reads packet length from RegRxNbBytes, checks capacity, sets FIFO pointer,
 *   reads payload from FIFO, reads RSSI/SNR metrics, clears RxDone, and returns ESP_OK.
 * - If neither: clears any spurious flags and returns ESP_ERR_NOT_FOUND.
 *
 * @note Context: Radio owner task context only.
 *
 * @param[out] out_payload Buffer to receive packet payload. Must not be NULL.
 * @param[in] capacity Maximum capacity of out_payload buffer in bytes.
 * @param[out] out_length Pointer to receive actual number of bytes read. Must not be NULL.
 * @param[out] out_metrics Pointer to receive packet signal metrics (can be NULL if not needed).
 *
 * @return
 *      - ESP_OK: Packet received and processed successfully
 *      - ESP_ERR_INVALID_ARG: out_payload or out_length is NULL, or capacity is 0
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - ESP_ERR_INVALID_CRC: Packet received but PayloadCrcError was asserted
 *      - ESP_ERR_INVALID_SIZE: Packet length exceeds buffer capacity
 *      - ESP_ERR_NOT_FOUND: No packet completion event found in IRQ flags
 *      - Other esp_err_t codes from SPI communication
 */
esp_err_t sx1276_handle_radio_event(uint8_t *out_payload, size_t capacity, size_t *out_length, sx1276_rx_metrics_t *out_metrics);

/**
 * @brief Wait for an incoming packet via DIO0 interrupt and read payload from FIFO.
 *
 * Blocks the calling task using ulTaskNotifyTake() with the specified timeout.
 * When awakened by DIO0 ISR (or upon timeout with late-arriving event):
 * delegates to sx1276_handle_radio_event() to validate and extract the packet.
 * If DIO0 ISR is not initialized, falls back to sx1276_poll_rx_packet().
 *
 * @note Context: Radio owner task context only. Device must be in RX_CONTINUOUS mode.
 *
 * @param[out] out_payload Buffer to receive packet payload. Must not be NULL.
 * @param[in] capacity Maximum capacity of out_payload buffer in bytes.
 * @param[out] out_length Pointer to receive actual number of bytes read. Must not be NULL.
 * @param[out] out_metrics Pointer to receive packet signal metrics (can be NULL if not needed).
 * @param[in] timeout_ms Maximum time to wait for a packet in milliseconds.
 *
 * @return
 *      - ESP_OK: Packet received and validated successfully
 *      - ESP_ERR_INVALID_ARG: out_payload or out_length is NULL, or capacity is 0
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - ESP_ERR_TIMEOUT: Timeout expired with no packet received
 *      - ESP_ERR_INVALID_CRC: Packet received but PayloadCrcError was asserted
 *      - ESP_ERR_INVALID_SIZE: Packet length exceeds buffer capacity
 *      - Other esp_err_t codes from SPI communication
 */
esp_err_t sx1276_receive_packet(uint8_t *out_payload, size_t capacity, size_t *out_length, sx1276_rx_metrics_t *out_metrics, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
