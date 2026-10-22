#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board_pins.h"
#include "sx1276.h"
#include "app_protocol.h"

#define NODE_TX_TOTAL_PACKETS           (10U)
#define NODE_TX_INTERVAL_MS             (2000U)
#define NODE_TX_TIMEOUT_MS              (3000U)

#define GATEWAY_RX_WINDOW_MS            (60000U)
#define GATEWAY_RX_POLL_SLICE_MS        (1000U)

#define PING_PONG_TOTAL_ROUNDS          (10U)
#define PING_PONG_INTERVAL_MS           (2000U)
#define PING_PONG_TIMEOUT_MS            (2000U)
#define PING_PONG_TX_TIMEOUT_MS         (3000U)
#define PING_PONG_NODE_ID               (0x0001U)
#define PING_PONG_DEMO_WINDOW_MS        (60000U)
#define PING_PONG_RX_SLICE_MS           (1000U)

#define RADIO_TASK_STACK_SIZE           (4096U)
#define RADIO_TASK_PRIORITY             (5U)

static const char *s_tag = "app_main";

static StaticTask_t s_radio_task_tcb;
static StackType_t s_radio_task_stack[RADIO_TASK_STACK_SIZE];
static TaskHandle_t s_radio_task_handle = NULL;

#if defined(CONFIG_APP_ROLE_GATEWAY_RX)
static uint8_t s_rx_buf[256];
#endif

#if defined(CONFIG_APP_ROLE_PING_PONG)
static uint8_t s_ping_pong_tx_buf[PROTOCOL_MAX_FRAME_SIZE];
static uint8_t s_ping_pong_rx_buf[256];

typedef struct {
    uint32_t ping_sent_count;
    uint32_t pong_received_count;
    uint32_t ping_timeout_count;
    uint32_t rx_crc_error_count;
    uint32_t protocol_crc_error_count;
    uint32_t invalid_frame_count;
    uint32_t sequence_mismatch_count;
    uint32_t rtt_last_ms;
} app_ping_metrics_t;

typedef struct {
    uint32_t ping_received_count;
    uint32_t pong_sent_count;
    uint32_t rx_crc_error_count;
    uint32_t protocol_crc_error_count;
    uint32_t invalid_frame_count;
    uint32_t tx_fail_count;
} app_responder_metrics_t;
#endif

#if defined(CONFIG_APP_ROLE_NODE_TX)
static void node_run_tx_task(void)
{
    const uint8_t hello_payload[] = "HELLO LORA";
    const size_t hello_len = sizeof(hello_payload) - 1U;

    ESP_LOGI(s_tag, "==================================================");
    ESP_LOGI(s_tag, "  Node TX: Sending %u packets ('%.*s', %zu bytes)",
             NODE_TX_TOTAL_PACKETS, (int)hello_len, (const char *)hello_payload, hello_len);
    ESP_LOGI(s_tag, "==================================================");

    uint32_t tx_success = 0U;
    uint32_t tx_fail = 0U;

    for (uint32_t i = 1U; i <= NODE_TX_TOTAL_PACKETS; i++) {
        ESP_LOGI(s_tag, "Node TX: Sending packet [%" PRIu32 "/%u]...", i, NODE_TX_TOTAL_PACKETS);

        esp_err_t ret = sx1276_send_packet(hello_payload, hello_len, NODE_TX_TIMEOUT_MS);
        if (ret == ESP_OK) {
            tx_success++;
            ESP_LOGI(s_tag, "Node TX: Packet [%" PRIu32 "/%u] TX SUCCESS (TxDone confirmed via DIO0)",
                     i, NODE_TX_TOTAL_PACKETS);
        } else {
            tx_fail++;
            ESP_LOGE(s_tag, "Node TX: Packet [%" PRIu32 "/%u] TX FAILED: %s",
                     i, NODE_TX_TOTAL_PACKETS, esp_err_to_name(ret));
        }

        if (i < NODE_TX_TOTAL_PACKETS) {
            vTaskDelay(pdMS_TO_TICKS(NODE_TX_INTERVAL_MS));
        }
    }

    ESP_LOGI(s_tag, "==================================================");
    ESP_LOGI(s_tag, "  Node TX Demo Complete: %" PRIu32 " succeeded, %" PRIu32 " failed",
             tx_success, tx_fail);
    ESP_LOGI(s_tag, "==================================================");
}
#endif

#if defined(CONFIG_APP_ROLE_GATEWAY_RX)
static void gateway_run_rx_task(void)
{
    ESP_LOGI(s_tag, "==================================================");
    ESP_LOGI(s_tag, "  Gateway RX: Starting continuous receive mode");
    ESP_LOGI(s_tag, "==================================================");

    esp_err_t ret = sx1276_start_rx_continuous();
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to start RX continuous: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(s_tag, "Gateway RX: Listening for incoming packets (%" PRIu32 " ms window)...",
             (uint32_t)GATEWAY_RX_WINDOW_MS);

    uint32_t rx_packets = 0U;
    uint32_t crc_errors = 0U;
    const int64_t start_time_us = esp_timer_get_time();
    const int64_t window_us = (int64_t)GATEWAY_RX_WINDOW_MS * 1000LL;

    while ((esp_timer_get_time() - start_time_us) < window_us) {
        size_t rx_len = 0U;
        sx1276_rx_metrics_t metrics;
        memset(&metrics, 0, sizeof(metrics));

        ret = sx1276_receive_packet(s_rx_buf, sizeof(s_rx_buf), &rx_len, &metrics, GATEWAY_RX_POLL_SLICE_MS);
        if (ret == ESP_OK) {
            rx_packets++;
            ESP_LOGI(s_tag, "Gateway RX: Packet [%" PRIu32 "] received via DIO0! Length: %zu bytes",
                     rx_packets, rx_len);
            ESP_LOGI(s_tag, "Gateway RX: Payload: '%.*s'", (int)rx_len, (const char *)s_rx_buf);

            const int16_t snr_int = (int16_t)(metrics.snr_x100_db / 100);
            const int16_t snr_frac = (int16_t)(metrics.snr_x100_db % 100);
            const uint16_t snr_frac_abs = (uint16_t)(snr_frac < 0 ? -snr_frac : snr_frac);
            ESP_LOGI(s_tag, "Gateway RX: Metrics: RSSI = %d dBm (raw: 0x%02X), SNR = %d.%02u dB (raw: %d)",
                     metrics.rssi_dbm, metrics.raw_rssi, snr_int, (unsigned int)snr_frac_abs, metrics.raw_snr);
        } else if (ret == ESP_ERR_INVALID_CRC) {
            crc_errors++;
            ESP_LOGW(s_tag, "Gateway RX: Payload CRC error detected (count: %" PRIu32 ")! Discarded packet.",
                     crc_errors);
        } else if (ret == ESP_ERR_TIMEOUT) {
            /* 1s slice expired with no packet received; continue until total window ends */
        } else {
            ESP_LOGE(s_tag, "Gateway RX: Receive error: %s", esp_err_to_name(ret));
        }
    }

    (void)sx1276_set_lora_standby_mode();
    ESP_LOGI(s_tag, "==================================================");
    ESP_LOGI(s_tag, "  Gateway RX 60-Second Demo Window Completed");
    ESP_LOGI(s_tag, "  Total Packets Received: %" PRIu32, rx_packets);
    ESP_LOGI(s_tag, "  CRC Errors Discarded:   %" PRIu32, crc_errors);
    ESP_LOGI(s_tag, "==================================================");
}
#endif

#if defined(CONFIG_APP_ROLE_PING_PONG) && !defined(CONFIG_APP_PING_PONG_MODE_RESPONDER)
/**
 * @brief Run the Ping-Pong Initiator (Node) lifecycle.
 *
 * Transmits PING packets, transitions immediately to RX_CONTINUOUS, waits for PONG
 * up to 2000 ms, validates sequence/node ID/CRC16, and computes Round-Trip Time (RTT).
 */
static void app_run_ping_pong_initiator(void)
{
    ESP_LOGI(s_tag, "==================================================");
    ESP_LOGI(s_tag, "  Ping-Pong Initiator (Node): %u Rounds", PING_PONG_TOTAL_ROUNDS);
    ESP_LOGI(s_tag, "==================================================");

    app_ping_metrics_t metrics;
    memset(&metrics, 0, sizeof(metrics));

    for (uint16_t seq = 1U; seq <= (uint16_t)PING_PONG_TOTAL_ROUNDS; seq++) {
        ESP_LOGI(s_tag, "--------------------------------------------------");
        ESP_LOGI(s_tag, "Ping-Pong Initiator: Starting Round [%u/%u] (seq: %u)",
                 (unsigned int)seq, PING_PONG_TOTAL_ROUNDS, (unsigned int)seq);

        /* Step 1: Create structured PING frame */
        const protocol_frame_t ping_frame = {
            .magic = PROTOCOL_MAGIC,
            .version = PROTOCOL_VERSION_1,
            .msg_type = PROTOCOL_MSG_TYPE_PING,
            .node_id = PING_PONG_NODE_ID,
            .seq_num = seq,
            .payload_len = 4U,
            .payload = {'P', 'I', 'N', 'G'},
            .crc16 = 0U,
        };

        /* Step 2: Explicit bytewise Little-Endian serialization (PRO-003, PRO-004) */
        size_t tx_frame_len = 0U;
        esp_err_t ret = protocol_encode_frame(&ping_frame, s_ping_pong_tx_buf,
                                              sizeof(s_ping_pong_tx_buf), &tx_frame_len);
        if (ret != ESP_OK) {
            ESP_LOGE(s_tag, "Failed to encode PING frame: %s", esp_err_to_name(ret));
            metrics.invalid_frame_count++;
            continue;
        }

        /* Step 3: Record monotonic start timestamp for RTT measurement */
        const int64_t t_start_us = esp_timer_get_time();

        /* Step 4: Transmit PING via event-driven TX with DIO0 TxDone confirmation */
        ret = sx1276_send_packet(s_ping_pong_tx_buf, tx_frame_len, PING_PONG_TX_TIMEOUT_MS);
        if (ret != ESP_OK) {
            ESP_LOGE(s_tag, "PING TX failed: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(PING_PONG_INTERVAL_MS));
            continue;
        }
        metrics.ping_sent_count++;
        ESP_LOGI(s_tag, "PING sent (seq: %u, %zu bytes). TxDone confirmed via DIO0.",
                 (unsigned int)seq, tx_frame_len);

        /* Step 5: Immediately transition to RX_CONTINUOUS mode to listen for PONG */
        ret = sx1276_start_rx_continuous();
        if (ret != ESP_OK) {
            ESP_LOGE(s_tag, "Failed to switch to RX continuous: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(PING_PONG_INTERVAL_MS));
            continue;
        }

        /* Step 6: Wait for PONG with bounded timeout (2000 ms demo baseline) */
        size_t rx_len = 0U;
        sx1276_rx_metrics_t rx_metrics;
        memset(&rx_metrics, 0, sizeof(rx_metrics));

        ret = sx1276_receive_packet(s_ping_pong_rx_buf, sizeof(s_ping_pong_rx_buf),
                                    &rx_len, &rx_metrics, PING_PONG_TIMEOUT_MS);
        const int64_t t_end_us = esp_timer_get_time();
        const uint32_t rtt_ms = (uint32_t)((t_end_us - t_start_us) / 1000LL);

        /* Step 7: Process reception result */
        if (ret == ESP_OK) {
            protocol_frame_t pong_frame;
            esp_err_t dec_ret = protocol_decode_frame(s_ping_pong_rx_buf, rx_len, &pong_frame);
            if (dec_ret == ESP_OK) {
                if (pong_frame.msg_type != PROTOCOL_MSG_TYPE_PONG) {
                    metrics.invalid_frame_count++;
                    ESP_LOGW(s_tag, "Received non-PONG frame (msg_type: 0x%02X)",
                             (unsigned int)pong_frame.msg_type);
                } else if (pong_frame.node_id != PING_PONG_NODE_ID) {
                    metrics.invalid_frame_count++;
                    ESP_LOGW(s_tag, "Node ID mismatch (expected: 0x%04X, got: 0x%04X)",
                             PING_PONG_NODE_ID, pong_frame.node_id);
                } else if (pong_frame.seq_num != seq) {
                    metrics.sequence_mismatch_count++;
                    ESP_LOGW(s_tag, "Sequence mismatch (expected: %u, got: %u)",
                             (unsigned int)seq, (unsigned int)pong_frame.seq_num);
                } else {
                    /* Transaction success */
                    metrics.pong_received_count++;
                    metrics.rtt_last_ms = rtt_ms;

                    const int16_t snr_int = (int16_t)(rx_metrics.snr_x100_db / 100);
                    const int16_t snr_frac = (int16_t)(rx_metrics.snr_x100_db % 100);
                    const uint16_t snr_frac_abs = (uint16_t)(snr_frac < 0 ? -snr_frac : snr_frac);

                    ESP_LOGI(s_tag, "PONG RECEIVED! Round [%u/%u] SUCCESS | RTT: %" PRIu32 " ms | "
                             "RSSI: %d dBm | SNR: %d.%02u dB | Seq: %u",
                             (unsigned int)seq, PING_PONG_TOTAL_ROUNDS, rtt_ms,
                             rx_metrics.rssi_dbm, snr_int, (unsigned int)snr_frac_abs,
                             (unsigned int)pong_frame.seq_num);
                }
            } else if (dec_ret == ESP_ERR_INVALID_CRC) {
                metrics.protocol_crc_error_count++;
                ESP_LOGW(s_tag, "PONG rejected: Protocol CRC16 verification failed");
            } else {
                metrics.invalid_frame_count++;
                ESP_LOGW(s_tag, "PONG rejected: Invalid frame format (%s)", esp_err_to_name(dec_ret));
            }
        } else if (ret == ESP_ERR_TIMEOUT) {
            metrics.ping_timeout_count++;
            ESP_LOGW(s_tag, "PING round [%u/%u] TIMEOUT: No PONG received within %u ms",
                     (unsigned int)seq, PING_PONG_TOTAL_ROUNDS, PING_PONG_TIMEOUT_MS);
        } else if (ret == ESP_ERR_INVALID_CRC) {
            metrics.rx_crc_error_count++;
            ESP_LOGW(s_tag, "PING round [%u/%u] SX1276 Hardware PayloadCrcError on RX",
                     (unsigned int)seq, PING_PONG_TOTAL_ROUNDS);
        } else {
            ESP_LOGE(s_tag, "PING round [%u/%u] RX error: %s",
                     (unsigned int)seq, PING_PONG_TOTAL_ROUNDS, esp_err_to_name(ret));
        }

        /* Step 8: Return radio to Standby mode between rounds */
        (void)sx1276_set_lora_standby_mode();

        if (seq < (uint16_t)PING_PONG_TOTAL_ROUNDS) {
            vTaskDelay(pdMS_TO_TICKS(PING_PONG_INTERVAL_MS));
        }
    }

    ESP_LOGI(s_tag, "==================================================");
    ESP_LOGI(s_tag, "  Ping-Pong Initiator Summary Report");
    ESP_LOGI(s_tag, "==================================================");
    ESP_LOGI(s_tag, "  Total PINGs Sent:        %" PRIu32, metrics.ping_sent_count);
    ESP_LOGI(s_tag, "  Total PONGs Received:    %" PRIu32, metrics.pong_received_count);
    ESP_LOGI(s_tag, "  PING Timeouts:           %" PRIu32, metrics.ping_timeout_count);
    ESP_LOGI(s_tag, "  Hardware CRC Errors:     %" PRIu32, metrics.rx_crc_error_count);
    ESP_LOGI(s_tag, "  Protocol CRC16 Errors:   %" PRIu32, metrics.protocol_crc_error_count);
    ESP_LOGI(s_tag, "  Invalid Frame Errors:    %" PRIu32, metrics.invalid_frame_count);
    ESP_LOGI(s_tag, "  Sequence Mismatches:     %" PRIu32, metrics.sequence_mismatch_count);
    ESP_LOGI(s_tag, "  Last RTT:                %" PRIu32 " ms", metrics.rtt_last_ms);
    ESP_LOGI(s_tag, "==================================================");
}
#endif

#if defined(CONFIG_APP_ROLE_PING_PONG) && defined(CONFIG_APP_PING_PONG_MODE_RESPONDER)
/**
 * @brief Run the Ping-Pong Responder (Gateway) lifecycle.
 *
 * Operates in continuous receive mode, validates incoming PING frames, generates
 * matching PONG responses with identical node ID and sequence number, and transitions
 * seamlessly between RX and TX states.
 */
static void app_run_ping_pong_responder(void)
{
    ESP_LOGI(s_tag, "==================================================");
    ESP_LOGI(s_tag, "  Ping-Pong Responder (Gateway): Continuous RX");
    ESP_LOGI(s_tag, "==================================================");

    esp_err_t ret = sx1276_start_rx_continuous();
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to start RX continuous: %s", esp_err_to_name(ret));
        return;
    }

    app_responder_metrics_t metrics;
    memset(&metrics, 0, sizeof(metrics));

    const int64_t start_time_us = esp_timer_get_time();
    const int64_t window_us = (int64_t)PING_PONG_DEMO_WINDOW_MS * 1000LL;

    while ((esp_timer_get_time() - start_time_us) < window_us) {
        size_t rx_len = 0U;
        sx1276_rx_metrics_t rx_metrics;
        memset(&rx_metrics, 0, sizeof(rx_metrics));

        ret = sx1276_receive_packet(s_ping_pong_rx_buf, sizeof(s_ping_pong_rx_buf),
                                    &rx_len, &rx_metrics, PING_PONG_RX_SLICE_MS);
        if (ret == ESP_OK) {
            protocol_frame_t ping_frame;
            esp_err_t dec_ret = protocol_decode_frame(s_ping_pong_rx_buf, rx_len, &ping_frame);
            if (dec_ret == ESP_OK) {
                if (ping_frame.msg_type == PROTOCOL_MSG_TYPE_PING) {
                    metrics.ping_received_count++;

                    const int16_t snr_int = (int16_t)(rx_metrics.snr_x100_db / 100);
                    const int16_t snr_frac = (int16_t)(rx_metrics.snr_x100_db % 100);
                    const uint16_t snr_frac_abs = (uint16_t)(snr_frac < 0 ? -snr_frac : snr_frac);

                    ESP_LOGI(s_tag, "PING RECEIVED! [seq: %u, node_id: 0x%04X, RSSI: %d dBm, SNR: %d.%02u dB] -> Responding with PONG",
                             (unsigned int)ping_frame.seq_num, (unsigned int)ping_frame.node_id,
                             rx_metrics.rssi_dbm, snr_int, (unsigned int)snr_frac_abs);

                    /* Construct matching PONG frame echoing node_id and seq_num */
                    const protocol_frame_t pong_frame = {
                        .magic = PROTOCOL_MAGIC,
                        .version = PROTOCOL_VERSION_1,
                        .msg_type = PROTOCOL_MSG_TYPE_PONG,
                        .node_id = ping_frame.node_id,
                        .seq_num = ping_frame.seq_num,
                        .payload_len = 4U,
                        .payload = {'P', 'O', 'N', 'G'},
                        .crc16 = 0U,
                    };

                    size_t tx_frame_len = 0U;
                    esp_err_t enc_ret = protocol_encode_frame(&pong_frame, s_ping_pong_tx_buf,
                                                              sizeof(s_ping_pong_tx_buf), &tx_frame_len);
                    if (enc_ret == ESP_OK) {
                        /* sx1276_send_packet executes: Standby -> TX -> TxDone -> Standby */
                        esp_err_t tx_ret = sx1276_send_packet(s_ping_pong_tx_buf, tx_frame_len,
                                                              PING_PONG_TX_TIMEOUT_MS);
                        if (tx_ret == ESP_OK) {
                            metrics.pong_sent_count++;
                            ESP_LOGI(s_tag, "PONG SENT [seq: %u]! TxDone confirmed via DIO0.",
                                     (unsigned int)pong_frame.seq_num);
                        } else {
                            metrics.tx_fail_count++;
                            ESP_LOGE(s_tag, "PONG TX failed: %s", esp_err_to_name(tx_ret));
                        }
                    } else {
                        metrics.tx_fail_count++;
                        ESP_LOGE(s_tag, "Failed to encode PONG frame: %s", esp_err_to_name(enc_ret));
                    }

                    /* Return transceiver to continuous reception for subsequent PINGs */
                    ret = sx1276_start_rx_continuous();
                    if (ret != ESP_OK) {
                        ESP_LOGE(s_tag, "Failed to re-enter RX continuous: %s", esp_err_to_name(ret));
                    }
                } else {
                    metrics.invalid_frame_count++;
                    ESP_LOGW(s_tag, "Ignoring non-PING message type: 0x%02X",
                             (unsigned int)ping_frame.msg_type);
                }
            } else if (dec_ret == ESP_ERR_INVALID_CRC) {
                metrics.protocol_crc_error_count++;
                ESP_LOGW(s_tag, "Discarded frame: Protocol CRC16 failure");
            } else {
                metrics.invalid_frame_count++;
                ESP_LOGW(s_tag, "Discarded frame: Invalid framing (%s)", esp_err_to_name(dec_ret));
            }
        } else if (ret == ESP_ERR_INVALID_CRC) {
            metrics.rx_crc_error_count++;
            ESP_LOGW(s_tag, "Discarded packet: SX1276 hardware PayloadCrcError");
        } else if (ret == ESP_ERR_TIMEOUT) {
            /* 1s slice expired with no packet; continue listening */
        } else {
            ESP_LOGE(s_tag, "Receive error: %s", esp_err_to_name(ret));
        }
    }

    (void)sx1276_set_lora_standby_mode();

    ESP_LOGI(s_tag, "==================================================");
    ESP_LOGI(s_tag, "  Ping-Pong Responder Summary Report");
    ESP_LOGI(s_tag, "==================================================");
    ESP_LOGI(s_tag, "  Total PINGs Received:    %" PRIu32, metrics.ping_received_count);
    ESP_LOGI(s_tag, "  Total PONGs Sent:        %" PRIu32, metrics.pong_sent_count);
    ESP_LOGI(s_tag, "  Hardware CRC Errors:     %" PRIu32, metrics.rx_crc_error_count);
    ESP_LOGI(s_tag, "  Protocol CRC16 Errors:   %" PRIu32, metrics.protocol_crc_error_count);
    ESP_LOGI(s_tag, "  Invalid Frame Errors:    %" PRIu32, metrics.invalid_frame_count);
    ESP_LOGI(s_tag, "  TX Failures:             %" PRIu32, metrics.tx_fail_count);
    ESP_LOGI(s_tag, "==================================================");
}
#endif

/**
 * @brief FreeRTOS Single Radio Owner Task entry point (RTOS-001, RTOS-002).
 *
 * @param[in] pvParameters Unused task parameters.
 */
static void app_run_radio_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(s_tag, "Radio Owner Task running (priority: %u)", (unsigned int)RADIO_TASK_PRIORITY);

    /* Initialize DIO0 interrupt for this Radio Owner Task (RTOS-009) */
    esp_err_t ret = sx1276_init_dio0_isr(xTaskGetCurrentTaskHandle());
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to initialize DIO0 ISR: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

#if defined(CONFIG_APP_ROLE_NODE_TX)
    ESP_LOGI(s_tag, "Dispatching role: Node TX (Event-Driven DIO0)");
    node_run_tx_task();
#elif defined(CONFIG_APP_ROLE_GATEWAY_RX)
    ESP_LOGI(s_tag, "Dispatching role: Gateway RX (Event-Driven DIO0)");
    gateway_run_rx_task();
#elif defined(CONFIG_APP_ROLE_PING_PONG)
  #if defined(CONFIG_APP_PING_PONG_MODE_RESPONDER)
    ESP_LOGI(s_tag, "Dispatching role: Ping-Pong Responder (Gateway)");
    app_run_ping_pong_responder();
  #else
    ESP_LOGI(s_tag, "Dispatching role: Ping-Pong Initiator (Node)");
    app_run_ping_pong_initiator();
  #endif
#else
    ESP_LOGW(s_tag, "No active role selected in Kconfig");
#endif

    /* Stack high water mark check (RTOS-008) */
    UBaseType_t stack_hwm = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(s_tag, "Radio Task Stack High Water Mark: %u bytes remaining",
             (unsigned int)(stack_hwm * sizeof(StackType_t)));

    /* Cleanup ISR on role completion */
    (void)sx1276_deinit_dio0_isr();
    (void)sx1276_set_lora_standby_mode();

    ESP_LOGI(s_tag, "Radio Owner Task finished. Deleting self.");
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(s_tag, "==================================================");
    ESP_LOGI(s_tag, "  ESP32 + SX1276 Phase 5: LoRa Ping-Pong Protocol");
    ESP_LOGI(s_tag, "==================================================");

    /* Step 1: Run deterministic protocol self-tests (QA-006) */
    esp_err_t ret = protocol_run_self_tests();
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Protocol self-tests failed (%s)! Halting.", esp_err_to_name(ret));
        return;
    }

    /* Step 2: Initialize SPI bus (Phase 1 Baseline) */
    ret = board_init_spi_bus();
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "SPI bus initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(s_tag, "SPI bus initialized on host %d", BOARD_LORA_SPI_HOST);

    /* Step 3: Initialize SX1276 (hardware reset + add SPI device + RegVersion check 0x12) */
    ret = sx1276_init_device();
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "SX1276 initialization failed: %s", esp_err_to_name(ret));
        return;
    }

    /* Step 4: State machine transitions: FSK Sleep -> LoRa Sleep -> LoRa Standby (RAD-005) */
    ret = sx1276_set_fsk_sleep_mode();
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Transition to FSK Sleep failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = sx1276_set_lora_sleep_mode();
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Transition to LoRa Sleep failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = sx1276_set_lora_standby_mode();
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Transition to LoRa Standby failed: %s", esp_err_to_name(ret));
        return;
    }

    /* Step 5: Configure RF parameters for 915 MHz baseline demo profile (Phase 2 Baseline) */
    const sx1276_config_t rf_cfg = {
        .frequency_hz = 915000000U,
        .bandwidth = (uint8_t)SX1276_BW_125_KHZ,
        .coding_rate = (uint8_t)SX1276_CR_4_5,
        .spreading_factor = (uint8_t)SX1276_SF_7,
        .explicit_header = true,
        .crc_enabled = true,
        .sync_word = 0x12U,
        .preamble_length = 8U,
        .pa_pin = SX1276_PA_PIN_BOOST,
        .tx_power_dbm = 5,
    };

    ret = sx1276_configure_radio(&rf_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Radio configuration failed: %s", esp_err_to_name(ret));
        return;
    }

    /* Step 6: Read back and verify radio configuration registers */
    ret = sx1276_verify_radio_config(&rf_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Radio configuration verification failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(s_tag, "Phase 1 and 2 Baselines verified successfully");

    /* Step 7: Create and start FreeRTOS Radio Owner Task statically (MEM-001, RTOS-002) */
    s_radio_task_handle = xTaskCreateStatic(
        app_run_radio_task,
        "radio_task",
        RADIO_TASK_STACK_SIZE,
        NULL,
        RADIO_TASK_PRIORITY,
        s_radio_task_stack,
        &s_radio_task_tcb
    );
    if (s_radio_task_handle == NULL) {
        ESP_LOGE(s_tag, "Failed to create Radio Owner Task statically");
        return;
    }
    ESP_LOGI(s_tag, "Radio Owner Task created successfully (handle: %p)", (void *)s_radio_task_handle);
}
