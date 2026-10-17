#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file sx1276_regs.h
 * @brief Semtech SX1276 private register map and bitfield definitions.
 *
 * All register addresses and bitmasks comply with the Semtech SX1276 Datasheet
 * (RAD-001, RAD-002).
 */

/* SPI command bitmasks */
#define SX1276_SPI_WRITE_MASK                   (0x80U) /* Bit 7 = 1 for SPI write */
#define SX1276_SPI_READ_MASK                    (0x7FU) /* Bit 7 = 0 for SPI read */

/* Common registers */
#define SX1276_REG_FIFO                         (0x00U)
#define SX1276_REG_OP_MODE                      (0x01U)
#define SX1276_REG_FRF_MSB                      (0x06U)
#define SX1276_REG_FRF_MID                      (0x07U)
#define SX1276_REG_FRF_LSB                      (0x08U)
#define SX1276_REG_PA_CONFIG                    (0x09U)
#define SX1276_REG_PA_RAMP                      (0x0AU)
#define SX1276_REG_OCP                          (0x0BU)
#define SX1276_REG_LNA                          (0x0CU)
#define SX1276_REG_FIFO_ADDR_PTR                (0x0DU)
#define SX1276_REG_FIFO_TX_BASE_ADDR            (0x0EU)
#define SX1276_REG_FIFO_RX_BASE_ADDR            (0x0FU)
#define SX1276_REG_FIFO_RX_CURRENT_ADDR         (0x10U)
#define SX1276_REG_IRQ_FLAGS_MASK               (0x11U)
#define SX1276_REG_IRQ_FLAGS                    (0x12U)
#define SX1276_REG_MODEM_CONFIG_1               (0x1DU)
#define SX1276_REG_MODEM_CONFIG_2               (0x1EU)
#define SX1276_REG_SYMB_TIMEOUT_LSB             (0x1FU)
#define SX1276_REG_PREAMBLE_MSB                 (0x20U)
#define SX1276_REG_PREAMBLE_LSB                 (0x21U)
#define SX1276_REG_PAYLOAD_LENGTH               (0x22U)
#define SX1276_REG_MAX_PAYLOAD_LENGTH           (0x23U)
#define SX1276_REG_HOP_PERIOD                   (0x24U)
#define SX1276_REG_FIFO_RX_BYTE_ADDR            (0x25U)
#define SX1276_REG_MODEM_CONFIG_3               (0x26U)
#define SX1276_REG_SYNC_WORD                    (0x39U)
#define SX1276_REG_VERSION                      (0x42U)
#define SX1276_REG_PA_DAC                       (0x4DU)

/* Expected silicon version */
#define SX1276_REG_VERSION_EXPECTED             (0x12U)

/* RegOpMode (0x01) bitfields */
#define SX1276_OP_MODE_LONGRANGEMODE_MASK       (0x80U) /* Bit 7: 1 = LoRa, 0 = FSK */
#define SX1276_OP_MODE_ACCESS_SHARED_REG        (0x40U) /* Bit 6 */
#define SX1276_OP_MODE_LOW_FREQ_MODE_ON         (0x08U) /* Bit 3: 1 = LF, 0 = HF (>862 MHz) */
#define SX1276_OP_MODE_MASK                     (0x07U) /* Bits 2:0: Mode */

#define SX1276_OP_MODE_LORA                     (0x80U) /* LongRangeMode bit set */
#define SX1276_OP_MODE_FSK                      (0x00U) /* LongRangeMode bit cleared */

#define SX1276_MODE_SLEEP                       (0x00U)
#define SX1276_MODE_STDBY                       (0x01U)
#define SX1276_MODE_FSTX                        (0x02U)
#define SX1276_MODE_TX                          (0x03U)
#define SX1276_MODE_FSRX                        (0x04U)
#define SX1276_MODE_RXCONTINUOUS                (0x05U)
#define SX1276_MODE_RXSINGLE                    (0x06U)
#define SX1276_MODE_CAD                         (0x07U)

/* Composite OpMode values */
#define SX1276_MODE_FSK_SLEEP                   (SX1276_OP_MODE_FSK | SX1276_MODE_SLEEP)
#define SX1276_MODE_FSK_STDBY                   (SX1276_OP_MODE_FSK | SX1276_MODE_STDBY)
#define SX1276_MODE_LORA_SLEEP                  (SX1276_OP_MODE_LORA | SX1276_MODE_SLEEP)
#define SX1276_MODE_LORA_STDBY                  (SX1276_OP_MODE_LORA | SX1276_MODE_STDBY)

/* RegPaConfig (0x09) bitfields */
#define SX1276_PA_CONFIG_PA_SELECT_MASK         (0x80U) /* Bit 7: 1 = PA_BOOST, 0 = RFO */
#define SX1276_PA_CONFIG_MAX_POWER_MASK         (0x70U) /* Bits 6:4 */
#define SX1276_PA_CONFIG_MAX_POWER_SHIFT        (4U)
#define SX1276_PA_CONFIG_OUTPUT_POWER_MASK      (0x0FU) /* Bits 3:0 */

/* RegPaDac (0x4D) bitfields */
#define SX1276_PA_DAC_DEFAULT                   (0x84U) /* Default up to +17 dBm */
#define SX1276_PA_DAC_BOOST_20DBM               (0x87U) /* +20 dBm mode */

/* RegModemConfig1 (0x1D) bitfields */
#define SX1276_MODEM_CONFIG_1_BW_MASK           (0xF0U) /* Bits 7:4 */
#define SX1276_MODEM_CONFIG_1_BW_SHIFT          (4U)
#define SX1276_MODEM_CONFIG_1_CR_MASK           (0x0EU) /* Bits 3:1 */
#define SX1276_MODEM_CONFIG_1_CR_SHIFT          (1U)
#define SX1276_MODEM_CONFIG_1_IMPLICIT_HEADER_MASK (0x01U) /* Bit 0 */

/* RegModemConfig2 (0x1E) bitfields */
#define SX1276_MODEM_CONFIG_2_SF_MASK           (0xF0U) /* Bits 7:4 */
#define SX1276_MODEM_CONFIG_2_SF_SHIFT          (4U)
#define SX1276_MODEM_CONFIG_2_TX_CONTINUOUS_MASK (0x08U) /* Bit 3 */
#define SX1276_MODEM_CONFIG_2_RX_PAYLOAD_CRC_MASK (0x04U) /* Bit 2 */
#define SX1276_MODEM_CONFIG_2_SYMB_TIMEOUT_MSB_MASK (0x03U) /* Bits 1:0 */

/* RegModemConfig3 (0x26) bitfields */
#define SX1276_MODEM_CONFIG_3_LOW_DATA_RATE_OPT_MASK (0x08U) /* Bit 3 */
#define SX1276_MODEM_CONFIG_3_AGC_AUTO_ON_MASK       (0x04U) /* Bit 2 */

/* Expected RF Profile Constants for 915 MHz Baseline */
#define SX1276_EXPECTED_REG_FRF_MSB             (0xE4U)
#define SX1276_EXPECTED_REG_FRF_MID             (0xC0U)
#define SX1276_EXPECTED_REG_FRF_LSB             (0x00U)
#define SX1276_EXPECTED_REG_MODEM_CONFIG_1      (0x72U)
#define SX1276_EXPECTED_REG_MODEM_CONFIG_2      (0x74U)
#define SX1276_EXPECTED_REG_SYNC_WORD           (0x12U)
#define SX1276_EXPECTED_REG_PREAMBLE_MSB        (0x00U)
#define SX1276_EXPECTED_REG_PREAMBLE_LSB        (0x08U)

#ifdef __cplusplus
}
#endif
