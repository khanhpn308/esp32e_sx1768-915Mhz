#include <stdio.h>
#include <inttypes.h>

#include "esp_err.h"
#include "esp_log.h"

#include "board_pins.h"
#include "sx1276.h"

static const char *s_tag = "app_main";

void app_main(void)
{
    ESP_LOGI(s_tag, "==================================================");
    ESP_LOGI(s_tag, "  ESP32 + SX1276 Phase 2: RF Config (915 MHz)");
    ESP_LOGI(s_tag, "==================================================");

    /* Step 1: Initialize SPI bus */
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

    /* Step 4: Configure RF parameters for 915 MHz baseline demo profile */
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

    ESP_LOGI(s_tag, "Phase 2 RF Configuration verified successfully");
}
