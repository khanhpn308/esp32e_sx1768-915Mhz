#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"

#include "board_pins.h"
#include "sx1276.h"

static const char *s_tag = "app_main";

void app_main(void)
{
    ESP_LOGI(s_tag, "==================================================");
    ESP_LOGI(s_tag, "  ESP32 + SX1276 Phase 1: SPI Smoke Test");
    ESP_LOGI(s_tag, "==================================================");

    /* Step 1: Initialize the SPI master bus with locked board pins */
    esp_err_t ret = board_init_spi_bus();
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "SPI bus initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(s_tag, "SPI bus initialized on host %d (SCK=%d, MOSI=%d, MISO=%d)",
             BOARD_LORA_SPI_HOST, BOARD_LORA_SCK, BOARD_LORA_MOSI, BOARD_LORA_MISO);

    /* Step 2: Initialize SX1276 (hardware reset + add SPI device + RegVersion check) */
    ret = sx1276_init_device();
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Phase 1 SPI smoke test FAILED: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(s_tag, "Phase 1 SPI smoke test PASSED: SX1276 communication verified successfully");
}
