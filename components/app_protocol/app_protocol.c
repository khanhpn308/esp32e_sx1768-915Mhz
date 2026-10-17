#include <string.h>
#include "esp_log.h"
#include "app_protocol.h"

static const char *s_tag = "app_protocol";

#define PROTOCOL_CRC16_POLYNOMIAL   (0x1021U)
#define PROTOCOL_CRC16_INIT         (0xFFFFU)

uint16_t protocol_calculate_crc16(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0U) {
        return 0U;
    }

    uint16_t crc = PROTOCOL_CRC16_INIT;

    for (size_t i = 0U; i < length; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8U);
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1U) ^ PROTOCOL_CRC16_POLYNOMIAL);
            } else {
                crc = (uint16_t)(crc << 1U);
            }
        }
    }

    return crc;
}

esp_err_t protocol_validate_frame(const uint8_t *raw_data, size_t raw_len)
{
    if (raw_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Check minimum and maximum frame sizes (PRO-005, PRO-007) */
    if (raw_len < PROTOCOL_MIN_FRAME_SIZE || raw_len > PROTOCOL_MAX_FRAME_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Byte 0..1: Verify magic identifier (Little-Endian: 0x58, 0x53) */
    const uint16_t magic = (uint16_t)raw_data[0] | ((uint16_t)raw_data[1] << 8U);
    if (magic != PROTOCOL_MAGIC) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Byte 2: Verify protocol version */
    const uint8_t version = raw_data[2];
    if (version != PROTOCOL_VERSION_1) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Byte 3: Verify message type */
    const uint8_t msg_type = raw_data[3];
    if (msg_type != (uint8_t)PROTOCOL_MSG_TYPE_PING && msg_type != (uint8_t)PROTOCOL_MSG_TYPE_PONG) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Byte 8: Verify payload length */
    const uint8_t payload_len = raw_data[8];
    if (payload_len > PROTOCOL_MAX_PAYLOAD_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Match declared payload length against actual frame size (PRO-005) */
    const size_t expected_total_len = (size_t)PROTOCOL_HEADER_SIZE + (size_t)payload_len + (size_t)PROTOCOL_CRC_SIZE;
    if (raw_len != expected_total_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Verify CRC16 checksum (PRO-006) */
    const size_t crc_offset = (size_t)PROTOCOL_HEADER_SIZE + (size_t)payload_len;
    const uint16_t received_crc16 = (uint16_t)raw_data[crc_offset] | ((uint16_t)raw_data[crc_offset + 1U] << 8U);
    const uint16_t computed_crc16 = protocol_calculate_crc16(raw_data, crc_offset);

    if (received_crc16 != computed_crc16) {
        return ESP_ERR_INVALID_CRC;
    }

    return ESP_OK;
}

esp_err_t protocol_encode_frame(const protocol_frame_t *frame,
                                uint8_t *out_buffer,
                                size_t buffer_capacity,
                                size_t *out_frame_len)
{
    if (frame == NULL || out_buffer == NULL || out_frame_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (frame->magic != PROTOCOL_MAGIC || frame->version != PROTOCOL_VERSION_1) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (frame->msg_type != PROTOCOL_MSG_TYPE_PING && frame->msg_type != PROTOCOL_MSG_TYPE_PONG) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (frame->payload_len > PROTOCOL_MAX_PAYLOAD_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    const size_t total_len = (size_t)PROTOCOL_HEADER_SIZE + (size_t)frame->payload_len + (size_t)PROTOCOL_CRC_SIZE;
    if (buffer_capacity < total_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Byte 0..1: Magic (Little-Endian) */
    out_buffer[0] = (uint8_t)(frame->magic & 0x00FFU);
    out_buffer[1] = (uint8_t)((frame->magic >> 8U) & 0x00FFU);

    /* Byte 2: Version */
    out_buffer[2] = frame->version;

    /* Byte 3: Message Type */
    out_buffer[3] = (uint8_t)frame->msg_type;

    /* Byte 4..5: Node ID (Little-Endian) */
    out_buffer[4] = (uint8_t)(frame->node_id & 0x00FFU);
    out_buffer[5] = (uint8_t)((frame->node_id >> 8U) & 0x00FFU);

    /* Byte 6..7: Sequence Number (Little-Endian) */
    out_buffer[6] = (uint8_t)(frame->seq_num & 0x00FFU);
    out_buffer[7] = (uint8_t)((frame->seq_num >> 8U) & 0x00FFU);

    /* Byte 8: Payload Length */
    out_buffer[8] = frame->payload_len;

    /* Byte 9 .. 9 + payload_len - 1: Explicit bytewise copy of payload */
    for (size_t i = 0U; i < (size_t)frame->payload_len; i++) {
        out_buffer[PROTOCOL_HEADER_SIZE + i] = frame->payload[i];
    }

    /* Compute CRC16 over Header and Payload */
    const size_t crc_coverage_len = (size_t)PROTOCOL_HEADER_SIZE + (size_t)frame->payload_len;
    const uint16_t computed_crc16 = protocol_calculate_crc16(out_buffer, crc_coverage_len);

    /* Byte total_len - 2 .. total_len - 1: CRC16 (Little-Endian) */
    out_buffer[crc_coverage_len] = (uint8_t)(computed_crc16 & 0x00FFU);
    out_buffer[crc_coverage_len + 1U] = (uint8_t)((computed_crc16 >> 8U) & 0x00FFU);

    *out_frame_len = total_len;
    return ESP_OK;
}

esp_err_t protocol_decode_frame(const uint8_t *raw_data, size_t raw_len, protocol_frame_t *out_frame)
{
    if (raw_data == NULL || out_frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Strict pre-validation before field access (PRO-005, PRO-006) */
    esp_err_t ret = protocol_validate_frame(raw_data, raw_len);
    if (ret != ESP_OK) {
        return ret;
    }

    memset(out_frame, 0, sizeof(*out_frame));

    /* Byte 0..1: Magic (Little-Endian) */
    out_frame->magic = (uint16_t)raw_data[0] | ((uint16_t)raw_data[1] << 8U);

    /* Byte 2: Version */
    out_frame->version = raw_data[2];

    /* Byte 3: Message Type */
    out_frame->msg_type = (protocol_msg_type_t)raw_data[3];

    /* Byte 4..5: Node ID (Little-Endian) */
    out_frame->node_id = (uint16_t)raw_data[4] | ((uint16_t)raw_data[5] << 8U);

    /* Byte 6..7: Sequence Number (Little-Endian) */
    out_frame->seq_num = (uint16_t)raw_data[6] | ((uint16_t)raw_data[7] << 8U);

    /* Byte 8: Payload Length */
    out_frame->payload_len = raw_data[8];

    /* Byte 9 .. 9 + payload_len - 1: Explicit bytewise copy of payload */
    for (size_t i = 0U; i < (size_t)out_frame->payload_len; i++) {
        out_frame->payload[i] = raw_data[PROTOCOL_HEADER_SIZE + i];
    }

    /* Byte raw_len - 2 .. raw_len - 1: CRC16 (Little-Endian) */
    const size_t crc_offset = (size_t)PROTOCOL_HEADER_SIZE + (size_t)out_frame->payload_len;
    out_frame->crc16 = (uint16_t)raw_data[crc_offset] | ((uint16_t)raw_data[crc_offset + 1U] << 8U);

    return ESP_OK;
}

esp_err_t protocol_run_self_tests(void)
{
    ESP_LOGI(s_tag, "Running protocol self-tests (QA-006)...");

    /* Test 1: Standard CRC16-CCITT vector: "123456789" -> 0x29B1 */
    const uint8_t test_vector_123456789[] = "123456789";
    const uint16_t test_crc = protocol_calculate_crc16(test_vector_123456789, 9U);
    if (test_crc != 0x29B1U) {
        ESP_LOGE(s_tag, "Self-test 1 FAILED: CRC16 test vector mismatch (got 0x%04X, expected 0x29B1)", test_crc);
        return ESP_FAIL;
    }

    /* Test 2: Frame encoding of PING frame */
    const protocol_frame_t ping_test = {
        .magic = PROTOCOL_MAGIC,
        .version = PROTOCOL_VERSION_1,
        .msg_type = PROTOCOL_MSG_TYPE_PING,
        .node_id = 0x0001U,
        .seq_num = 0x0001U,
        .payload_len = 4U,
        .payload = {'P', 'I', 'N', 'G'},
        .crc16 = 0U,
    };

    uint8_t wire_buf[PROTOCOL_MAX_FRAME_SIZE];
    size_t wire_len = 0U;
    esp_err_t ret = protocol_encode_frame(&ping_test, wire_buf, sizeof(wire_buf), &wire_len);
    if (ret != ESP_OK || wire_len != 15U) {
        ESP_LOGE(s_tag, "Self-test 2 FAILED: Frame encoding failed (ret: %s, len: %zu)", esp_err_to_name(ret), wire_len);
        return ESP_FAIL;
    }

    /* Expected wire vector: 58 53 01 01 01 00 01 00 04 50 49 4E 47 12 0D */
    const uint8_t expected_wire[] = {
        0x58U, 0x53U, 0x01U, 0x01U, 0x01U, 0x00U, 0x01U, 0x00U, 0x04U, 0x50U, 0x49U, 0x4EU, 0x47U, 0x12U, 0x0DU
    };
    if (memcmp(wire_buf, expected_wire, sizeof(expected_wire)) != 0) {
        ESP_LOGE(s_tag, "Self-test 2 FAILED: Encoded wire buffer does not match expected test vector");
        return ESP_FAIL;
    }

    /* Test 3: Frame decoding of valid wire buffer */
    protocol_frame_t decoded_frame;
    ret = protocol_decode_frame(wire_buf, wire_len, &decoded_frame);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Self-test 3 FAILED: Decoding valid wire buffer failed: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }
    if (decoded_frame.magic != PROTOCOL_MAGIC ||
        decoded_frame.version != PROTOCOL_VERSION_1 ||
        decoded_frame.msg_type != PROTOCOL_MSG_TYPE_PING ||
        decoded_frame.node_id != 0x0001U ||
        decoded_frame.seq_num != 0x0001U ||
        decoded_frame.payload_len != 4U ||
        memcmp(decoded_frame.payload, "PING", 4U) != 0 ||
        decoded_frame.crc16 != 0x0D12U) {
        ESP_LOGE(s_tag, "Self-test 3 FAILED: Decoded frame field values mismatch");
        return ESP_FAIL;
    }

    /* Test 4: Truncated frame rejected */
    ret = protocol_decode_frame(wire_buf, 10U, &decoded_frame);
    if (ret != ESP_ERR_INVALID_SIZE) {
        ESP_LOGE(s_tag, "Self-test 4 FAILED: Truncated frame not rejected (ret: %s)", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    /* Test 5: Corrupted CRC rejected */
    wire_buf[wire_len - 1U] ^= 0xFFU;
    ret = protocol_decode_frame(wire_buf, wire_len, &decoded_frame);
    if (ret != ESP_ERR_INVALID_CRC) {
        ESP_LOGE(s_tag, "Self-test 5 FAILED: Corrupted CRC not rejected (ret: %s)", esp_err_to_name(ret));
        return ESP_FAIL;
    }
    wire_buf[wire_len - 1U] ^= 0xFFU; /* Restore */

    /* Test 6: Invalid magic rejected */
    wire_buf[0] ^= 0x01U;
    ret = protocol_decode_frame(wire_buf, wire_len, &decoded_frame);
    if (ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGE(s_tag, "Self-test 6 FAILED: Invalid magic not rejected (ret: %s)", esp_err_to_name(ret));
        return ESP_FAIL;
    }
    wire_buf[0] ^= 0x01U; /* Restore */

    /* Test 7: Mismatched payload length rejected */
    wire_buf[8] = 10U; /* Fake payload length = 10 while actual frame has 4 */
    ret = protocol_decode_frame(wire_buf, wire_len, &decoded_frame);
    if (ret != ESP_ERR_INVALID_SIZE) {
        ESP_LOGE(s_tag, "Self-test 7 FAILED: Mismatched payload length not rejected (ret: %s)", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    ESP_LOGI(s_tag, "Protocol self-tests PASSED (7/7 test cases verified)");
    return ESP_OK;
}
