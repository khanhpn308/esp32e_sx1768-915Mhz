#include "sx1276.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"

#include "board_pins.h"
#include "sx1276_regs.h"

static const char *s_tag = "sx1276";
static spi_device_handle_t s_spi_handle = NULL;
static bool s_is_initialized = false;

esp_err_t sx1276_reset_device(void)
{
    const gpio_config_t rst_conf = {
        .pin_bit_mask = (1ULL << BOARD_LORA_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&rst_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to configure RST GPIO (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    /* Drive RESET low */
    ret = gpio_set_level(BOARD_LORA_RST, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to drive RST low (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    /* SX1276 datasheet section 7.2.2 requires NRESET low for at least 100 us.
     * vTaskDelay(pdMS_TO_TICKS(10) + 1) guarantees at least 10 ms delay without busy-waiting. */
    vTaskDelay(pdMS_TO_TICKS(10) + 1);

    /* Release RESET high */
    ret = gpio_set_level(BOARD_LORA_RST, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to drive RST high (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    /* SX1276 datasheet section 7.2.2 requires at least 5 ms wait after reset before SPI communication.
     * vTaskDelay(pdMS_TO_TICKS(10) + 1) provides 10 ms for the crystal oscillator to fully stabilize. */
    vTaskDelay(pdMS_TO_TICKS(10) + 1);

    return ESP_OK;
}

esp_err_t sx1276_read_register(uint8_t reg, uint8_t *out_val)
{
    if (out_val == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));
    trans.length = 16U;
    trans.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
    trans.tx_data[0] = (uint8_t)(reg & SX1276_SPI_READ_MASK);
    trans.tx_data[1] = 0x00U;

    esp_err_t ret = spi_device_transmit(s_spi_handle, &trans);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "SPI read failed at reg 0x%02X (err: %s)", reg, esp_err_to_name(ret));
        return ret;
    }

    *out_val = trans.rx_data[1];
    return ESP_OK;
}

esp_err_t sx1276_write_register(uint8_t reg, uint8_t val)
{
    if (s_spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));
    trans.length = 16U;
    trans.flags = SPI_TRANS_USE_TXDATA;
    trans.tx_data[0] = (uint8_t)(reg | SX1276_SPI_WRITE_MASK);
    trans.tx_data[1] = val;

    esp_err_t ret = spi_device_transmit(s_spi_handle, &trans);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "SPI write failed at reg 0x%02X (err: %s)", reg, esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t sx1276_init_device(void)
{
    /* Step 1: Perform hardware reset sequence */
    esp_err_t ret = sx1276_reset_device();
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Hardware reset failed (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    /* Step 2: Add SPI device to bus if not already added */
    if (s_spi_handle == NULL) {
        const spi_device_interface_config_t devcfg = {
            .clock_speed_hz = 4 * 1000 * 1000,
            .mode = 0,
            .spics_io_num = BOARD_LORA_CS,
            .queue_size = 1,
            .flags = 0,
            .pre_cb = NULL,
            .post_cb = NULL,
        };

        ret = spi_bus_add_device(BOARD_LORA_SPI_HOST, &devcfg, &s_spi_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(s_tag, "Failed to add SPI device to bus (err: %s)", esp_err_to_name(ret));
            return ret;
        }
    }

    /* Step 3: Read RegVersion (0x42) */
    uint8_t version = 0U;
    ret = sx1276_read_register(SX1276_REG_VERSION, &version);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to read RegVersion register (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    /* Step 4: Validate silicon version against expected 0x12 (RAD-004) */
    if (version != SX1276_REG_VERSION_EXPECTED) {
        ESP_LOGE(s_tag, "SX1276 silicon version mismatch: expected 0x%02X, got 0x%02X",
                 SX1276_REG_VERSION_EXPECTED, version);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Step 5: Log success */
    ESP_LOGI(s_tag, "SX1276 Silicon Version: 0x%02X", version);
    s_is_initialized = true;

    return ESP_OK;
}

esp_err_t sx1276_deinit_device(void)
{
    if (s_spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = spi_bus_remove_device(s_spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to remove SPI device (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    s_spi_handle = NULL;
    s_is_initialized = false;
    return ESP_OK;
}
