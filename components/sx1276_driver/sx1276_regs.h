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
#define SX1276_SPI_WRITE_MASK           (0x80U) /* Bit 7 = 1 for SPI write */
#define SX1276_SPI_READ_MASK            (0x7FU) /* Bit 7 = 0 for SPI read */

/* Common registers */
#define SX1276_REG_FIFO                 (0x00U)
#define SX1276_REG_OP_MODE              (0x01U)
#define SX1276_REG_FRF_MSB              (0x06U)
#define SX1276_REG_FRF_MID              (0x07U)
#define SX1276_REG_FRF_LSB              (0x08U)
#define SX1276_REG_PA_CONFIG            (0x09U)
#define SX1276_REG_PA_RAMP              (0x0AU)
#define SX1276_REG_OCP                  (0x0BU)
#define SX1276_REG_LNA                  (0x0CU)
#define SX1276_REG_FIFO_ADDR_PTR        (0x0DU)
#define SX1276_REG_FIFO_TX_BASE_ADDR    (0x0EU)
#define SX1276_REG_FIFO_RX_BASE_ADDR    (0x0FU)
#define SX1276_REG_FIFO_RX_CURRENT_ADDR (0x10U)
#define SX1276_REG_IRQ_FLAGS_MASK       (0x11U)
#define SX1276_REG_IRQ_FLAGS            (0x12U)
#define SX1276_REG_VERSION              (0x42U)

/* Expected silicon version */
#define SX1276_REG_VERSION_EXPECTED     (0x12U)

#ifdef __cplusplus
}
#endif
