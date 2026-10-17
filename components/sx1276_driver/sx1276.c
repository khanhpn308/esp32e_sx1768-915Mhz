#include "sx1276.h"

#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_attr.h"

#include "board_pins.h"
#include "sx1276_regs.h"

#define SX1276_STATE_TRANSITION_TIMEOUT_MS  (50U)
#define SX1276_RETRY_INTERVAL_MS            (5U)
#define SX1276_MAX_RETRIES                  (SX1276_STATE_TRANSITION_TIMEOUT_MS / SX1276_RETRY_INTERVAL_MS)
#define SX1276_FIFO_MAX_TRANSFER_SIZE       (256U)
#define SX1276_POLL_INTERVAL_MS             (10U)

static const char *s_tag = "sx1276";
static spi_device_handle_t s_spi_handle = NULL;
static bool s_is_initialized = false;
static sx1276_state_t s_current_state = SX1276_STATE_UNINIT;

static DMA_ATTR uint8_t s_fifo_tx_buf[SX1276_FIFO_MAX_TRANSFER_SIZE];
static DMA_ATTR uint8_t s_fifo_rx_buf[SX1276_FIFO_MAX_TRANSFER_SIZE];

/**
 * @brief Internal helper to write RegOpMode and wait for status with bounded retries.
 *
 * @param[in] target_mode Value to write to RegOpMode.
 * @param[in] mask Bitmask applied to read-back value.
 * @param[in] expected Expected masked value.
 *
 * @return
 *      - ESP_OK: Transition verified successfully
 *      - ESP_ERR_TIMEOUT: Transition timed out after bounded retries
 *      - Other esp_err_t codes from SPI communication
 */
static esp_err_t sx1276_transition_op_mode(uint8_t target_mode, uint8_t mask, uint8_t expected)
{
    if (!s_is_initialized || s_spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = sx1276_write_register(SX1276_REG_OP_MODE, target_mode);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to write RegOpMode (err: %s)", esp_err_to_name(ret));
        s_current_state = SX1276_STATE_FAULT;
        return ret;
    }

    /* Bounded retry loop (ERR-005, RTOS-007) */
    uint8_t read_val = 0U;
    for (uint32_t retry = 0U; retry < SX1276_MAX_RETRIES; retry++) {
        ret = sx1276_read_register(SX1276_REG_OP_MODE, &read_val);
        if (ret == ESP_OK && ((read_val & mask) == expected)) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(SX1276_RETRY_INTERVAL_MS) + 1);
    }

    ESP_LOGE(s_tag, "OpMode transition timeout: target 0x%02X, expected 0x%02X, actual 0x%02X, mask 0x%02X",
             target_mode, expected, read_val, mask);
    s_current_state = SX1276_STATE_FAULT;
    return ESP_ERR_TIMEOUT;
}

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

    /* Step 5: Log success and set initial post-reset state */
    ESP_LOGI(s_tag, "SX1276 Silicon Version: 0x%02X", version);
    s_is_initialized = true;
    s_current_state = SX1276_STATE_STANDBY;

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
    s_current_state = SX1276_STATE_UNINIT;
    return ESP_OK;
}

sx1276_state_t sx1276_get_state(void)
{
    return s_current_state;
}

esp_err_t sx1276_set_fsk_sleep_mode(void)
{
    const uint8_t mask = SX1276_OP_MODE_LONGRANGEMODE_MASK | SX1276_OP_MODE_MASK;
    const uint8_t expected = SX1276_MODE_FSK_SLEEP;

    esp_err_t ret = sx1276_transition_op_mode(SX1276_MODE_FSK_SLEEP, mask, expected);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to enter FSK Sleep mode (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    s_current_state = SX1276_STATE_SLEEP;
    ESP_LOGI(s_tag, "FSK Sleep: PASS");
    return ESP_OK;
}

esp_err_t sx1276_set_lora_sleep_mode(void)
{
    /* RAD-005 constraint: LongRangeMode can only be written while in Sleep mode */
    if (s_current_state != SX1276_STATE_SLEEP) {
        ESP_LOGE(s_tag, "Cannot switch LongRangeMode: device is not in SLEEP mode");
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t mask = SX1276_OP_MODE_LONGRANGEMODE_MASK | SX1276_OP_MODE_MASK;
    const uint8_t expected = SX1276_MODE_LORA_SLEEP;

    esp_err_t ret = sx1276_transition_op_mode(SX1276_MODE_LORA_SLEEP, mask, expected);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to enter LoRa Sleep mode (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    s_current_state = SX1276_STATE_SLEEP;
    ESP_LOGI(s_tag, "LoRa Sleep: PASS");
    return ESP_OK;
}

esp_err_t sx1276_set_lora_standby_mode(void)
{
    const uint8_t mask = SX1276_OP_MODE_LONGRANGEMODE_MASK | SX1276_OP_MODE_MASK;
    const uint8_t expected = SX1276_MODE_LORA_STDBY;

    esp_err_t ret = sx1276_transition_op_mode(SX1276_MODE_LORA_STDBY, mask, expected);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to enter LoRa Standby mode (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    s_current_state = SX1276_STATE_STANDBY;
    ESP_LOGI(s_tag, "LoRa Standby: PASS");
    return ESP_OK;
}

esp_err_t sx1276_set_frequency(uint32_t freq_hz)
{
    if (!s_is_initialized || s_spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 64-bit integer arithmetic to prevent overflow: Frf = (freq_hz * 2^19) / 32000000 (TYP-004) */
    const uint64_t frf = ((uint64_t)freq_hz << 19) / 32000000ULL;
    const uint8_t msb = (uint8_t)((frf >> 16) & 0xFFU);
    const uint8_t mid = (uint8_t)((frf >> 8) & 0xFFU);
    const uint8_t lsb = (uint8_t)(frf & 0xFFU);

    esp_err_t ret = sx1276_write_register(SX1276_REG_FRF_MSB, msb);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = sx1276_write_register(SX1276_REG_FRF_MID, mid);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = sx1276_write_register(SX1276_REG_FRF_LSB, lsb);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Read back each byte and compare */
    uint8_t read_msb = 0U;
    uint8_t read_mid = 0U;
    uint8_t read_lsb = 0U;

    ret = sx1276_read_register(SX1276_REG_FRF_MSB, &read_msb);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = sx1276_read_register(SX1276_REG_FRF_MID, &read_mid);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = sx1276_read_register(SX1276_REG_FRF_LSB, &read_lsb);
    if (ret != ESP_OK) {
        return ret;
    }

    if (read_msb != msb || read_mid != mid || read_lsb != lsb) {
        ESP_LOGE(s_tag, "RegFrf mismatch: expected %02X %02X %02X, got %02X %02X %02X",
                 msb, mid, lsb, read_msb, read_mid, read_lsb);
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

esp_err_t sx1276_set_modem_config(uint8_t bw, uint8_t cr, uint8_t sf, bool explicit_header, bool crc_enabled)
{
    if (!s_is_initialized || s_spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* RegModemConfig1: [7:4] Bw, [3:1] CodingRate, [0] ImplicitHeaderModeOn */
    const uint8_t mc1 = (uint8_t)(((bw & 0x0FU) << SX1276_MODEM_CONFIG_1_BW_SHIFT) |
                                  ((cr & 0x07U) << SX1276_MODEM_CONFIG_1_CR_SHIFT) |
                                  (explicit_header ? 0x00U : SX1276_MODEM_CONFIG_1_IMPLICIT_HEADER_MASK));

    esp_err_t ret = sx1276_write_register(SX1276_REG_MODEM_CONFIG_1, mc1);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t read_mc1 = 0U;
    ret = sx1276_read_register(SX1276_REG_MODEM_CONFIG_1, &read_mc1);
    if (ret != ESP_OK) {
        return ret;
    }
    if (read_mc1 != mc1) {
        ESP_LOGE(s_tag, "RegModemConfig1 mismatch: expected 0x%02X, actual 0x%02X, mask 0xFF", mc1, read_mc1);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* RegModemConfig2: [7:4] SpreadingFactor, [3] TxContinuousMode (0), [2] RxPayloadCrcOn, [1:0] SymbTimeout MSB (0) */
    const uint8_t mc2 = (uint8_t)(((sf & 0x0FU) << SX1276_MODEM_CONFIG_2_SF_SHIFT) |
                                  (crc_enabled ? SX1276_MODEM_CONFIG_2_RX_PAYLOAD_CRC_MASK : 0x00U));

    ret = sx1276_write_register(SX1276_REG_MODEM_CONFIG_2, mc2);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t read_mc2 = 0U;
    ret = sx1276_read_register(SX1276_REG_MODEM_CONFIG_2, &read_mc2);
    if (ret != ESP_OK) {
        return ret;
    }
    if (read_mc2 != mc2) {
        ESP_LOGE(s_tag, "RegModemConfig2 mismatch: expected 0x%02X, actual 0x%02X, mask 0xFF", mc2, read_mc2);
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

esp_err_t sx1276_set_sync_word(uint8_t sync_word)
{
    if (!s_is_initialized || s_spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = sx1276_write_register(SX1276_REG_SYNC_WORD, sync_word);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t read_sw = 0U;
    ret = sx1276_read_register(SX1276_REG_SYNC_WORD, &read_sw);
    if (ret != ESP_OK) {
        return ret;
    }
    if (read_sw != sync_word) {
        ESP_LOGE(s_tag, "RegSyncWord mismatch: expected 0x%02X, actual 0x%02X, mask 0xFF", sync_word, read_sw);
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

esp_err_t sx1276_set_preamble_length(uint16_t preamble_length)
{
    if (!s_is_initialized || s_spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t msb = (uint8_t)((preamble_length >> 8) & 0xFFU);
    const uint8_t lsb = (uint8_t)(preamble_length & 0xFFU);

    esp_err_t ret = sx1276_write_register(SX1276_REG_PREAMBLE_MSB, msb);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = sx1276_write_register(SX1276_REG_PREAMBLE_LSB, lsb);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t read_msb = 0U;
    uint8_t read_lsb = 0U;
    ret = sx1276_read_register(SX1276_REG_PREAMBLE_MSB, &read_msb);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = sx1276_read_register(SX1276_REG_PREAMBLE_LSB, &read_lsb);
    if (ret != ESP_OK) {
        return ret;
    }

    if (read_msb != msb || read_lsb != lsb) {
        ESP_LOGE(s_tag, "Preamble mismatch: expected %02X %02X, got %02X %02X", msb, lsb, read_msb, read_lsb);
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

esp_err_t sx1276_set_power_config(sx1276_pa_pin_t pa_pin, int8_t power_dbm)
{
    if (!s_is_initialized || s_spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t pa_config = 0U;
    if (pa_pin == SX1276_PA_PIN_BOOST) {
        if (power_dbm < 2 || power_dbm > 17) {
            ESP_LOGE(s_tag, "Invalid power_dbm %d for PA_BOOST (allowed: 2 to 17 dBm)", power_dbm);
            return ESP_ERR_INVALID_ARG;
        }
        /* Pout = 2 + OutputPower -> OutputPower = power_dbm - 2 */
        const uint8_t output_power = (uint8_t)(power_dbm - 2);
        pa_config = (uint8_t)(SX1276_PA_CONFIG_PA_SELECT_MASK |
                              (0x04U << SX1276_PA_CONFIG_MAX_POWER_SHIFT) |
                              (output_power & SX1276_PA_CONFIG_OUTPUT_POWER_MASK));

        /* Default PA DAC setting (0x84) for standard power up to +17 dBm */
        esp_err_t ret = sx1276_write_register(SX1276_REG_PA_DAC, SX1276_PA_DAC_DEFAULT);
        if (ret != ESP_OK) {
            return ret;
        }
    } else if (pa_pin == SX1276_PA_PIN_RFO) {
        if (power_dbm < -4 || power_dbm > 14) {
            ESP_LOGE(s_tag, "Invalid power_dbm %d for RFO (allowed: -4 to 14 dBm)", power_dbm);
            return ESP_ERR_INVALID_ARG;
        }
        int8_t out_pwr = power_dbm;
        if (out_pwr < 0) {
            out_pwr = 0;
        }
        pa_config = (uint8_t)((0x07U << SX1276_PA_CONFIG_MAX_POWER_SHIFT) |
                              ((uint8_t)out_pwr & SX1276_PA_CONFIG_OUTPUT_POWER_MASK));
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = sx1276_write_register(SX1276_REG_PA_CONFIG, pa_config);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t read_pa = 0U;
    ret = sx1276_read_register(SX1276_REG_PA_CONFIG, &read_pa);
    if (ret != ESP_OK) {
        return ret;
    }

    const uint8_t pa_mask = SX1276_PA_CONFIG_PA_SELECT_MASK | SX1276_PA_CONFIG_OUTPUT_POWER_MASK;
    if ((read_pa & pa_mask) != (pa_config & pa_mask)) {
        ESP_LOGE(s_tag, "RegPaConfig mismatch: expected 0x%02X, actual 0x%02X, mask 0x%02X",
                 pa_config, read_pa, pa_mask);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Report unconfirmed hardware PA RF routing per RAD-010 / DEC-04 */
    ESP_LOGW(s_tag, "PA routing hardware status: PA_PATH_UNCONFIRMED (TX inactive in Phase 2)");

    return ESP_OK;
}

esp_err_t sx1276_configure_radio(const sx1276_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_is_initialized || s_spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Device must be in LoRa Standby or Sleep before configuring RF registers */
    if (s_current_state != SX1276_STATE_STANDBY && s_current_state != SX1276_STATE_SLEEP) {
        ESP_LOGE(s_tag, "Cannot configure radio: device is not in SLEEP or STANDBY");
        return ESP_ERR_INVALID_STATE;
    }

    /* 1. Carrier Frequency */
    esp_err_t ret = sx1276_set_frequency(config->frequency_hz);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to set frequency (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    /* 2. Modem configuration */
    ret = sx1276_set_modem_config(config->bandwidth, config->coding_rate,
                                 config->spreading_factor, config->explicit_header,
                                 config->crc_enabled);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to set modem config (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    /* 3. Sync word */
    ret = sx1276_set_sync_word(config->sync_word);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to set sync word (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    /* 4. Preamble length */
    ret = sx1276_set_preamble_length(config->preamble_length);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to set preamble length (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    /* 5. Power amplifier configuration */
    ret = sx1276_set_power_config(config->pa_pin, config->tx_power_dbm);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to set power config (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t sx1276_verify_radio_config(const sx1276_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_is_initialized || s_spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    bool has_error = false;

    /* 1. Verify RegFrf */
    const uint64_t frf = ((uint64_t)config->frequency_hz << 19) / 32000000ULL;
    const uint8_t exp_msb = (uint8_t)((frf >> 16) & 0xFFU);
    const uint8_t exp_mid = (uint8_t)((frf >> 8) & 0xFFU);
    const uint8_t exp_lsb = (uint8_t)(frf & 0xFFU);

    uint8_t read_msb = 0U;
    uint8_t read_mid = 0U;
    uint8_t read_lsb = 0U;

    esp_err_t ret = sx1276_read_register(SX1276_REG_FRF_MSB, &read_msb);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = sx1276_read_register(SX1276_REG_FRF_MID, &read_mid);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = sx1276_read_register(SX1276_REG_FRF_LSB, &read_lsb);
    if (ret != ESP_OK) {
        return ret;
    }

    if (read_msb != exp_msb || read_mid != exp_mid || read_lsb != exp_lsb) {
        ESP_LOGE(s_tag, "RegFrf mismatch: expected %02X %02X %02X, got %02X %02X %02X",
                 exp_msb, exp_mid, exp_lsb, read_msb, read_mid, read_lsb);
        has_error = true;
    }

    /* 2. Verify RegModemConfig1 */
    const uint8_t exp_mc1 = (uint8_t)(((config->bandwidth & 0x0FU) << SX1276_MODEM_CONFIG_1_BW_SHIFT) |
                                      ((config->coding_rate & 0x07U) << SX1276_MODEM_CONFIG_1_CR_SHIFT) |
                                      (config->explicit_header ? 0x00U : SX1276_MODEM_CONFIG_1_IMPLICIT_HEADER_MASK));
    uint8_t read_mc1 = 0U;
    ret = sx1276_read_register(SX1276_REG_MODEM_CONFIG_1, &read_mc1);
    if (ret != ESP_OK) {
        return ret;
    }
    if (read_mc1 != exp_mc1) {
        ESP_LOGE(s_tag, "RegModemConfig1 mismatch: expected 0x%02X, actual 0x%02X, mask 0xFF", exp_mc1, read_mc1);
        has_error = true;
    }

    /* 3. Verify RegModemConfig2 */
    const uint8_t exp_mc2 = (uint8_t)(((config->spreading_factor & 0x0FU) << SX1276_MODEM_CONFIG_2_SF_SHIFT) |
                                      (config->crc_enabled ? SX1276_MODEM_CONFIG_2_RX_PAYLOAD_CRC_MASK : 0x00U));
    uint8_t read_mc2 = 0U;
    ret = sx1276_read_register(SX1276_REG_MODEM_CONFIG_2, &read_mc2);
    if (ret != ESP_OK) {
        return ret;
    }
    if (read_mc2 != exp_mc2) {
        ESP_LOGE(s_tag, "RegModemConfig2 mismatch: expected 0x%02X, actual 0x%02X, mask 0xFF", exp_mc2, read_mc2);
        has_error = true;
    }

    /* 4. Verify RegSyncWord */
    uint8_t read_sw = 0U;
    ret = sx1276_read_register(SX1276_REG_SYNC_WORD, &read_sw);
    if (ret != ESP_OK) {
        return ret;
    }
    if (read_sw != config->sync_word) {
        ESP_LOGE(s_tag, "RegSyncWord mismatch: expected 0x%02X, actual 0x%02X, mask 0xFF", config->sync_word, read_sw);
        has_error = true;
    }

    /* 5. Verify Preamble */
    const uint8_t exp_pre_msb = (uint8_t)((config->preamble_length >> 8) & 0xFFU);
    const uint8_t exp_pre_lsb = (uint8_t)(config->preamble_length & 0xFFU);
    uint8_t read_pre_msb = 0U;
    uint8_t read_pre_lsb = 0U;
    ret = sx1276_read_register(SX1276_REG_PREAMBLE_MSB, &read_pre_msb);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = sx1276_read_register(SX1276_REG_PREAMBLE_LSB, &read_pre_lsb);
    if (ret != ESP_OK) {
        return ret;
    }
    if (read_pre_msb != exp_pre_msb || read_pre_lsb != exp_pre_lsb) {
        ESP_LOGE(s_tag, "Preamble mismatch: expected %02X %02X, got %02X %02X",
                 exp_pre_msb, exp_pre_lsb, read_pre_msb, read_pre_lsb);
        has_error = true;
    }

    /* 6. Verify RegPaConfig */
    uint8_t read_pa = 0U;
    ret = sx1276_read_register(SX1276_REG_PA_CONFIG, &read_pa);
    if (ret != ESP_OK) {
        return ret;
    }
    const uint8_t pa_mask = SX1276_PA_CONFIG_PA_SELECT_MASK | SX1276_PA_CONFIG_OUTPUT_POWER_MASK;
    uint8_t exp_pa = 0U;
    if (config->pa_pin == SX1276_PA_PIN_BOOST) {
        const uint8_t output_power = (uint8_t)(config->tx_power_dbm - 2);
        exp_pa = (uint8_t)(SX1276_PA_CONFIG_PA_SELECT_MASK | (output_power & SX1276_PA_CONFIG_OUTPUT_POWER_MASK));
    } else {
        int8_t out_pwr = config->tx_power_dbm;
        if (out_pwr < 0) {
            out_pwr = 0;
        }
        exp_pa = (uint8_t)((uint8_t)out_pwr & SX1276_PA_CONFIG_OUTPUT_POWER_MASK);
    }
    if ((read_pa & pa_mask) != (exp_pa & pa_mask)) {
        ESP_LOGE(s_tag, "RegPaConfig mismatch: expected 0x%02X, actual 0x%02X, mask 0x%02X",
                 exp_pa, read_pa, pa_mask);
        has_error = true;
    }

    if (has_error) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Print expected result report matching Section 5 */
    ESP_LOGI(s_tag, "RF Frequency: %" PRIu32 " Hz", config->frequency_hz);
    ESP_LOGI(s_tag, "RegFrf: %02X %02X %02X [PASS]", exp_msb, exp_mid, exp_lsb);
    ESP_LOGI(s_tag, "RegModemConfig1: 0x%02X [PASS]", exp_mc1);
    ESP_LOGI(s_tag, "RegModemConfig2: 0x%02X [PASS]", exp_mc2);
    ESP_LOGI(s_tag, "RegSyncWord: 0x%02X [PASS]", config->sync_word);
    ESP_LOGI(s_tag, "Preamble: %u symbols [PASS]", config->preamble_length);
    ESP_LOGI(s_tag, "RF Config Read-back: PASS");

    return ESP_OK;
}

esp_err_t sx1276_set_lora_tx_mode(void)
{
    if (!s_is_initialized || s_spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = sx1276_write_register(SX1276_REG_OP_MODE, SX1276_MODE_LORA_TX);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to enter LoRa TX mode (err: %s)", esp_err_to_name(ret));
        s_current_state = SX1276_STATE_FAULT;
        return ret;
    }

    s_current_state = SX1276_STATE_TX;
    return ESP_OK;
}

esp_err_t sx1276_set_lora_rx_continuous_mode(void)
{
    const uint8_t mask = SX1276_OP_MODE_LONGRANGEMODE_MASK | SX1276_OP_MODE_MASK;
    const uint8_t expected = SX1276_MODE_LORA_RXCONTINUOUS;

    esp_err_t ret = sx1276_transition_op_mode(SX1276_MODE_LORA_RXCONTINUOUS, mask, expected);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to enter LoRa RX Continuous mode (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    s_current_state = SX1276_STATE_RX_CONTINUOUS;
    return ESP_OK;
}

esp_err_t sx1276_write_fifo(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0U || length > 255U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_is_initialized || s_spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const size_t total_len = 1U + length;
    if (total_len > SX1276_FIFO_MAX_TRANSFER_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    s_fifo_tx_buf[0] = (uint8_t)(SX1276_REG_FIFO | SX1276_SPI_WRITE_MASK);
    memcpy(&s_fifo_tx_buf[1], data, length);

    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));
    trans.length = (size_t)(total_len * 8U);
    trans.tx_buffer = s_fifo_tx_buf;
    trans.rx_buffer = NULL;

    esp_err_t ret = spi_device_transmit(s_spi_handle, &trans);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "SPI write FIFO failed (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t sx1276_read_fifo(uint8_t *out_data, size_t length, size_t capacity)
{
    if (out_data == NULL || length == 0U || length > capacity || length > 255U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_is_initialized || s_spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const size_t total_len = 1U + length;
    if (total_len > SX1276_FIFO_MAX_TRANSFER_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(s_fifo_tx_buf, 0, total_len);
    s_fifo_tx_buf[0] = (uint8_t)(SX1276_REG_FIFO & SX1276_SPI_READ_MASK);

    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));
    trans.length = (size_t)(total_len * 8U);
    trans.tx_buffer = s_fifo_tx_buf;
    trans.rx_buffer = s_fifo_rx_buf;

    esp_err_t ret = spi_device_transmit(s_spi_handle, &trans);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "SPI read FIFO failed (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    memcpy(out_data, &s_fifo_rx_buf[1], length);
    return ESP_OK;
}

esp_err_t sx1276_read_irq_flags(uint8_t *out_flags)
{
    if (out_flags == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return sx1276_read_register(SX1276_REG_IRQ_FLAGS, out_flags);
}

esp_err_t sx1276_clear_irq_flags(uint8_t flags_to_clear)
{
    return sx1276_write_register(SX1276_REG_IRQ_FLAGS, flags_to_clear);
}

esp_err_t sx1276_clear_all_irq_flags(void)
{
    return sx1276_write_register(SX1276_REG_IRQ_FLAGS, SX1276_IRQ_ALL_MASK);
}

esp_err_t sx1276_read_packet_metrics(sx1276_rx_metrics_t *out_metrics)
{
    if (out_metrics == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t raw_snr_u8 = 0U;
    esp_err_t ret = sx1276_read_register(SX1276_REG_PKT_SNR_VALUE, &raw_snr_u8);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to read RegPktSnrValue (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    uint8_t raw_rssi_u8 = 0U;
    ret = sx1276_read_register(SX1276_REG_PKT_RSSI_VALUE, &raw_rssi_u8);
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to read RegPktRssiValue (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    /* SX1276 Datasheet Section 5.5.5: SNR is signed two's complement, 0.25 dB per LSB */
    const int8_t raw_snr = (int8_t)raw_snr_u8;
    out_metrics->raw_snr = raw_snr;
    out_metrics->raw_rssi = raw_rssi_u8;
    out_metrics->snr_x100_db = (int16_t)((int16_t)raw_snr * 25);

    /* HF port (915 MHz): RSSI = -157 + RegPktRssiValue */
    int16_t rssi = (int16_t)(SX1276_RSSI_OFFSET_HF_DBM + (int16_t)raw_rssi_u8);
    if (raw_snr < 0) {
        /* When SNR < 0 dB: PacketRSSI(dBm) = -157 + PacketRssi + PacketSnr * 0.25 */
        rssi = (int16_t)(rssi + (int16_t)(raw_snr / 4));
    }
    out_metrics->rssi_dbm = rssi;

    return ESP_OK;
}

esp_err_t sx1276_send_packet(const uint8_t *payload, size_t length, uint32_t timeout_ms)
{
    if (payload == NULL || length == 0U || length > 255U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_is_initialized || s_spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Step 1: Put transceiver in Standby mode (RAD-012) */
    esp_err_t ret = sx1276_set_lora_standby_mode();
    if (ret != ESP_OK) {
        ESP_LOGE(s_tag, "Failed to enter Standby mode for TX (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    /* Step 2: Clear stale IRQ flags */
    ret = sx1276_clear_all_irq_flags();
    if (ret != ESP_OK) {
        return ret;
    }

    /* Step 3: Read FIFO TX base address (RAD-008) */
    uint8_t tx_base = 0U;
    ret = sx1276_read_register(SX1276_REG_FIFO_TX_BASE_ADDR, &tx_base);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Step 4: Set FIFO address pointer to TX base address */
    ret = sx1276_write_register(SX1276_REG_FIFO_ADDR_PTR, tx_base);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Step 5: Write payload into FIFO */
    ret = sx1276_write_fifo(payload, length);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Step 6: Write payload length */
    ret = sx1276_write_register(SX1276_REG_PAYLOAD_LENGTH, (uint8_t)length);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Step 7: Switch to TX mode (RegOpMode = 0x83) */
    ret = sx1276_set_lora_tx_mode();
    if (ret != ESP_OK) {
        return ret;
    }

    /* Step 8: Poll RegIrqFlags with bounded timeout (ERR-005, RTOS-007) */
    const uint32_t poll_interval_ms = SX1276_POLL_INTERVAL_MS;
    const uint32_t max_iterations = (timeout_ms + poll_interval_ms - 1U) / poll_interval_ms;
    bool tx_done = false;

    for (uint32_t iter = 0U; iter < max_iterations; iter++) {
        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms) + 1);

        uint8_t irq_flags = 0U;
        ret = sx1276_read_irq_flags(&irq_flags);
        if (ret != ESP_OK) {
            ESP_LOGE(s_tag, "Failed to read IRQ flags during TX poll (err: %s)", esp_err_to_name(ret));
            (void)sx1276_set_lora_standby_mode();
            return ret;
        }

        if ((irq_flags & SX1276_IRQ_TX_DONE_MASK) != 0U) {
            tx_done = true;
            break;
        }
    }

    if (!tx_done) {
        ESP_LOGE(s_tag, "TX timeout (%" PRIu32 " ms expired without TxDone)", timeout_ms);
        (void)sx1276_set_lora_standby_mode();
        return ESP_ERR_TIMEOUT;
    }

    /* Step 9: Clear TxDone flag (write-1-to-clear) */
    ret = sx1276_clear_irq_flags(SX1276_IRQ_TX_DONE_MASK);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Step 10: Switch radio back to Standby */
    ret = sx1276_set_lora_standby_mode();
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

esp_err_t sx1276_start_rx_continuous(void)
{
    if (!s_is_initialized || s_spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Step 1: Put radio in Standby */
    esp_err_t ret = sx1276_set_lora_standby_mode();
    if (ret != ESP_OK) {
        return ret;
    }

    /* Step 2: Configure RX FIFO base address to 0x00 */
    ret = sx1276_write_register(SX1276_REG_FIFO_RX_BASE_ADDR, 0x00U);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Step 3: Set FIFO address pointer to base address */
    ret = sx1276_write_register(SX1276_REG_FIFO_ADDR_PTR, 0x00U);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Step 4: Clear all previous IRQ flags */
    ret = sx1276_clear_all_irq_flags();
    if (ret != ESP_OK) {
        return ret;
    }

    /* Step 5: Enter RX_CONTINUOUS mode */
    ret = sx1276_set_lora_rx_continuous_mode();
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

esp_err_t sx1276_poll_rx_packet(uint8_t *out_payload, size_t capacity, size_t *out_length, sx1276_rx_metrics_t *out_metrics, uint32_t timeout_ms)
{
    if (out_payload == NULL || out_length == NULL || capacity == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_is_initialized || s_spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t poll_interval_ms = SX1276_POLL_INTERVAL_MS;
    const uint32_t max_iterations = (timeout_ms + poll_interval_ms - 1U) / poll_interval_ms;

    for (uint32_t iter = 0U; iter < max_iterations; iter++) {
        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms) + 1);

        uint8_t irq_flags = 0U;
        esp_err_t ret = sx1276_read_irq_flags(&irq_flags);
        if (ret != ESP_OK) {
            return ret;
        }

        if ((irq_flags & SX1276_IRQ_RX_DONE_MASK) != 0U) {
            /* Check PayloadCrcError (RAD-007) */
            if ((irq_flags & SX1276_IRQ_PAYLOAD_CRC_ERROR_MASK) != 0U) {
                (void)sx1276_clear_irq_flags(SX1276_IRQ_RX_DONE_MASK | SX1276_IRQ_PAYLOAD_CRC_ERROR_MASK);
                return ESP_ERR_INVALID_CRC;
            }

            /* Read number of bytes received */
            uint8_t nb_bytes = 0U;
            ret = sx1276_read_register(SX1276_REG_RX_NB_BYTES, &nb_bytes);
            if (ret != ESP_OK) {
                (void)sx1276_clear_irq_flags(SX1276_IRQ_RX_DONE_MASK);
                return ret;
            }

            if ((size_t)nb_bytes > capacity) {
                ESP_LOGE(s_tag, "RX packet size (%u) exceeds buffer capacity (%zu)", nb_bytes, capacity);
                (void)sx1276_clear_irq_flags(SX1276_IRQ_RX_DONE_MASK);
                return ESP_ERR_INVALID_SIZE;
            }

            /* Read start address of last packet received */
            uint8_t rx_current_addr = 0U;
            ret = sx1276_read_register(SX1276_REG_FIFO_RX_CURRENT_ADDR, &rx_current_addr);
            if (ret != ESP_OK) {
                (void)sx1276_clear_irq_flags(SX1276_IRQ_RX_DONE_MASK);
                return ret;
            }

            /* Set FIFO address pointer to start of received packet */
            ret = sx1276_write_register(SX1276_REG_FIFO_ADDR_PTR, rx_current_addr);
            if (ret != ESP_OK) {
                (void)sx1276_clear_irq_flags(SX1276_IRQ_RX_DONE_MASK);
                return ret;
            }

            /* Read payload bytes from FIFO */
            if (nb_bytes > 0U) {
                ret = sx1276_read_fifo(out_payload, (size_t)nb_bytes, capacity);
                if (ret != ESP_OK) {
                    (void)sx1276_clear_irq_flags(SX1276_IRQ_RX_DONE_MASK);
                    return ret;
                }
            }

            *out_length = (size_t)nb_bytes;

            /* Read RSSI and SNR metrics */
            if (out_metrics != NULL) {
                ret = sx1276_read_packet_metrics(out_metrics);
                if (ret != ESP_OK) {
                    ESP_LOGW(s_tag, "Failed to read packet metrics (err: %s)", esp_err_to_name(ret));
                }
            }

            /* Clear RxDone flag */
            ret = sx1276_clear_irq_flags(SX1276_IRQ_RX_DONE_MASK);
            if (ret != ESP_OK) {
                return ret;
            }

            return ESP_OK;
        }
    }

    return ESP_ERR_TIMEOUT;
}
