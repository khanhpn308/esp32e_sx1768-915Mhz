#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file app_protocol.h
 * @brief Public interface for point-to-point (P2P) binary frame protocol (PRO-001..PRO-011).
 *
 * Implements explicit bytewise Little-Endian serialization, deserialization,
 * pre-validation, and CRC16-CCITT checksumming for SX1276 LoRa P2P packets.
 */

#define PROTOCOL_MAGIC              (0x5358U)   /**< Magic frame identifier: ASCII 'S', 'X' */
#define PROTOCOL_VERSION_1          (0x01U)     /**< Baseline protocol version 1 */

#define PROTOCOL_HEADER_SIZE        (9U)        /**< Fixed header size in bytes */
#define PROTOCOL_CRC_SIZE           (2U)        /**< CRC16 size in bytes */
#define PROTOCOL_MIN_FRAME_SIZE     (PROTOCOL_HEADER_SIZE + PROTOCOL_CRC_SIZE) /**< 11 bytes */
#define PROTOCOL_MAX_PAYLOAD_SIZE   (64U)       /**< Maximum allowed payload size in bytes */
#define PROTOCOL_MAX_FRAME_SIZE     (PROTOCOL_HEADER_SIZE + PROTOCOL_MAX_PAYLOAD_SIZE + PROTOCOL_CRC_SIZE) /**< 75 */

/**
 * @brief Message type enumeration (PRO-002).
 */
typedef enum {
    PROTOCOL_MSG_TYPE_PING = 0x01,  /**< PING link test and RTT request */
    PROTOCOL_MSG_TYPE_PONG = 0x02,  /**< PONG response */
} protocol_msg_type_t;

/**
 * @brief Structured protocol frame representation (PRO-002, DEC-07).
 */
typedef struct {
    uint16_t magic;                             /**< Protocol magic word (0x5358) */
    uint8_t version;                            /**< Protocol version (0x01) */
    protocol_msg_type_t msg_type;               /**< Message type (PING or PONG) */
    uint16_t node_id;                           /**< Node device identifier */
    uint16_t seq_num;                           /**< Packet sequence number */
    uint8_t payload_len;                        /**< Payload length (0 to PROTOCOL_MAX_PAYLOAD_SIZE) */
    uint8_t payload[PROTOCOL_MAX_PAYLOAD_SIZE]; /**< Raw payload data bytes */
    uint16_t crc16;                             /**< CRC16-CCITT checksum */
} protocol_frame_t;

/**
 * @brief Calculate CRC16-CCITT (False) checksum over a buffer.
 *
 * Uses polynomial 0x1021, init 0xFFFF, refin=false, refout=false, xorout=0x0000.
 *
 * @param[in] data Pointer to input data buffer. Must not be NULL if length > 0.
 * @param[in] length Number of bytes to process.
 *
 * @return 16-bit CRC checksum, or 0 if data is NULL or length is 0.
 */
uint16_t protocol_calculate_crc16(const uint8_t *data, size_t length);

/**
 * @brief Validate a raw over-the-air frame buffer prior to decoding.
 *
 * Performs boundary checks, verifies magic, version, message type, payload length,
 * and validates CRC16 checksum over header and payload (PRO-005, PRO-006).
 *
 * @param[in] raw_data Pointer to raw frame buffer. Must not be NULL.
 * @param[in] raw_len Number of bytes in raw frame buffer.
 *
 * @return
 *      - ESP_OK: Frame is well-formed, supported, and CRC16 verified
 *      - ESP_ERR_INVALID_ARG: raw_data is NULL
 *      - ESP_ERR_INVALID_SIZE: raw_len < PROTOCOL_MIN_FRAME_SIZE, > PROTOCOL_MAX_FRAME_SIZE,
 *                              or payload_len does not match actual length
 *      - ESP_ERR_NOT_SUPPORTED: Invalid magic or unsupported protocol version
 *      - ESP_ERR_INVALID_RESPONSE: Unknown message type
 *      - ESP_ERR_INVALID_CRC: CRC16 checksum verification failed
 */
esp_err_t protocol_validate_frame(const uint8_t *raw_data, size_t raw_len);

/**
 * @brief Explicitly serialize a protocol frame into wire format (Little-Endian).
 *
 * Encodes all fields byte-by-byte in Little-Endian order, computes CRC16 over header
 * and payload, and appends CRC16 in Little-Endian order (PRO-003, PRO-004).
 *
 * @param[in] frame Pointer to structured frame to encode. Must not be NULL.
 * @param[out] out_buffer Destination buffer for serialized bytes. Must not be NULL.
 * @param[in] buffer_capacity Maximum capacity of out_buffer in bytes.
 * @param[out] out_frame_len Pointer to receive actual encoded frame length. Must not be NULL.
 *
 * @return
 *      - ESP_OK: Frame encoded successfully
 *      - ESP_ERR_INVALID_ARG: frame, out_buffer, or out_frame_len is NULL
 *      - ESP_ERR_INVALID_SIZE: buffer_capacity too small or payload_len > PROTOCOL_MAX_PAYLOAD_SIZE
 *      - ESP_ERR_NOT_SUPPORTED: Invalid magic or unsupported version
 *      - ESP_ERR_INVALID_RESPONSE: Invalid message type
 */
esp_err_t protocol_encode_frame(const protocol_frame_t *frame,
                                uint8_t *out_buffer,
                                size_t buffer_capacity,
                                size_t *out_frame_len);

/**
 * @brief Explicitly deserialize and validate a wire format frame into a structured frame.
 *
 * Validates the raw buffer via protocol_validate_frame() and unpacks fields byte-by-byte
 * in Little-Endian order (PRO-003, PRO-004, PRO-005).
 *
 * @param[in] raw_data Pointer to raw frame buffer. Must not be NULL.
 * @param[in] raw_len Number of bytes in raw frame buffer.
 * @param[out] out_frame Pointer to structured frame to populate. Must not be NULL.
 *
 * @return
 *      - ESP_OK: Frame decoded and verified successfully
 *      - ESP_ERR_INVALID_ARG: raw_data or out_frame is NULL
 *      - Other error codes propagated from protocol_validate_frame()
 */
esp_err_t protocol_decode_frame(const uint8_t *raw_data, size_t raw_len, protocol_frame_t *out_frame);

/**
 * @brief Execute deterministic self-tests for protocol encoding, decoding, and CRC16 (QA-006).
 *
 * Tests CRC16 standard test vectors, frame serialization, deserialization, truncated input,
 * corrupted CRC16, invalid magic, and mismatched lengths.
 *
 * @return
 *      - ESP_OK: All self-tests passed
 *      - ESP_FAIL: One or more self-tests failed
 */
esp_err_t protocol_run_self_tests(void);

#ifdef __cplusplus
}
#endif
