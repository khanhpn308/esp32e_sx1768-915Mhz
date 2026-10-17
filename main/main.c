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

#define NODE_TX_TOTAL_PACKETS       (10U)
#define NODE_TX_INTERVAL_MS         (2000U)
#define NODE_TX_TIMEOUT_MS          (3000U)

#define GATEWAY_RX_WINDOW_MS        (60000U)
#define GATEWAY_RX_POLL_SLICE_MS    (1000U)

#define RADIO_TASK_STACK_SIZE       (4096U)
#define RADIO_TASK_PRIORITY         (5U)

static const char *s_tag = "app_main";

static StaticTask_t s_radio_task_tcb;
static StackType_t s_radio_task_stack[RADIO_TASK_STACK_SIZE];
static TaskHandle_t s_radio_task_handle = NULL;

#if defined(CONFIG_APP_ROLE_GATEWAY_RX)
static uint8_t s_rx_buf[256];
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
            ESP_LOGI(s_tag, "Gateway RX: Packet [%" PRIu32 "] received via DIO0! Length: %zu bytes", rx_packets, rx_len);
            ESP_LOGI(s_tag, "Gateway RX: Payload: '%.*s'", (int)rx_len, (const char *)s_rx_buf);

            const int16_t snr_int = (int16_t)(metrics.snr_x100_db / 100);
            const int16_t snr_frac = (int16_t)(metrics.snr_x100_db % 100);
            const uint16_t snr_frac_abs = (uint16_t)(snr_frac < 0 ? -snr_frac : snr_frac);
            ESP_LOGI(s_tag, "Gateway RX: Metrics: RSSI = %d dBm (raw: 0x%02X), SNR = %d.%02u dB (raw: %d)",
                     metrics.rssi_dbm, metrics.raw_rssi, snr_int, (unsigned int)snr_frac_abs, metrics.raw_snr);
        } else if (ret == ESP_ERR_INVALID_CRC) {
            crc_errors++;
            ESP_LOGW(s_tag, "Gateway RX: Payload CRC error detected (count: %" PRIu32 ")! Discarded packet.", crc_errors);
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
    ESP_LOGI(s_tag, "  ESP32 + SX1276 Phase 4: DIO0 Interrupt & FreeRTOS");
    ESP_LOGI(s_tag, "==================================================");

    /* Step 1: Initialize SPI bus (Phase 1 Baseline) */
    esp_err_t ret = board_init_spi_bus();
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "SPI bus initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(s_tag, "SPI bus initialized on host %d", BOARD_LORA_SPI_HOST);

    /* Step 2: Initialize SX1276 (hardware reset + add SPI device + RegVersion check 0x12) */
    ret = sx1276_init_device();
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "SX1276 initialization failed: %s", esp_err_to_name(ret));
        return;
    }

    /* Step 3: State machine transitions: FSK Sleep -> LoRa Sleep -> LoRa Standby (RAD-005) */
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

    /* Step 4: Configure RF parameters for 915 MHz baseline demo profile (Phase 2 Baseline) */
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

    /* Step 5: Read back and verify radio configuration registers */
    ret = sx1276_verify_radio_config(&rf_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Radio configuration verification failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(s_tag, "Phase 1 and 2 Baselines verified successfully");

    /* Step 6: Create and start FreeRTOS Radio Owner Task statically (MEM-001, RTOS-002) */
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
