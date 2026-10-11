#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pms5003.h"

#define PMS_BAUD                9600
#define PMS_RX_BUF              256
#define PMS_WARMUP_US           (30 * 1000000LL)
#define PMS_RESET_PULSE_MS      10
#define PMS_RESET_SETTLE_US     (1000 * 1000LL)
#define PMS_FAILS_BEFORE_RESET  3
#define PMS_PROBE_TIMEOUT_MS    200

static const char *TAG = "pms";

static const uint8_t CMD_PASSIVE[7] = {0x42, 0x4D, 0xE1, 0x00, 0x00, 0x01, 0x70};
static const uint8_t CMD_READ[7]    = {0x42, 0x4D, 0xE2, 0x00, 0x00, 0x01, 0x71};

static pms_config_t s_cfg;
static int64_t s_start_us;
static int64_t s_request_us;
static int64_t s_reset_us;
static bool s_resetting;
static uint8_t s_fail_streak;
static uint32_t s_reset_count;

static TickType_t ms_to_ticks_ceil(int64_t ms)
{
    return (TickType_t)((ms * configTICK_RATE_HZ + 999) / 1000);
}

static void pms_send(const uint8_t cmd[7])
{
    uart_flush_input(s_cfg.uart);
    uart_write_bytes(s_cfg.uart, cmd, 7);
}

static void pms_start_reset(void)
{
    gpio_set_level(s_cfg.reset, 0);
    vTaskDelay(ms_to_ticks_ceil(PMS_RESET_PULSE_MS) + 1);
    gpio_set_level(s_cfg.reset, 1);
    s_reset_us = esp_timer_get_time();
    s_resetting = true;
    s_fail_streak = 0;
    s_reset_count++;
    ESP_LOGW(TAG, "reset pulse after %d bad reads", PMS_FAILS_BEFORE_RESET);
}

esp_err_t pms_init(const pms_config_t *cfg)
{
    s_cfg = *cfg;

    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << cfg->set | 1ULL << cfg->reset,
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(cfg->reset, 1);
    gpio_set_level(cfg->set, 1);

    const uart_config_t uc = {
        .baud_rate = PMS_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        /* XTAL keeps the baud rate exact while DFS scales APB. */
        .source_clk = UART_SCLK_XTAL,
    };
    err = uart_driver_install(cfg->uart, PMS_RX_BUF, 0, 0, NULL, 0);
    if (err == ESP_OK) {
        err = uart_param_config(cfg->uart, &uc);
    }
    if (err == ESP_OK) {
        err = uart_set_pin(cfg->uart, cfg->tx, cfg->rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err != ESP_OK) {
        return err;
    }

    s_start_us = esp_timer_get_time();
    /* Passive mode is not retained across power cycles. */
    pms_send(CMD_PASSIVE);
    uint8_t ack[8];
    int n = uart_read_bytes(cfg->uart, ack, sizeof(ack), ms_to_ticks_ceil(PMS_PROBE_TIMEOUT_MS));
    uart_flush_input(cfg->uart);
    if (n <= 0) {
        ESP_LOGW(TAG, "no reply to passive-mode command");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "passive mode set");
    return ESP_OK;
}

esp_err_t pms_request(void)
{
    if (s_resetting) {
        if (esp_timer_get_time() - s_reset_us < PMS_RESET_SETTLE_US) {
            return ESP_ERR_INVALID_STATE;
        }
        s_resetting = false;
        s_start_us = esp_timer_get_time();
        pms_send(CMD_PASSIVE);
        return ESP_ERR_INVALID_STATE;
    }
    pms_send(CMD_READ);
    s_request_us = esp_timer_get_time();
    return ESP_OK;
}

/* Reads until a full frame starting with the 0x42 0x4D header is buffered. */
static bool pms_collect(uint8_t *buf, int64_t deadline_us)
{
    size_t have = 0;
    while (have < PMS_FRAME_LEN) {
        int64_t remaining_ms = (deadline_us - esp_timer_get_time()) / 1000;
        if (remaining_ms <= 0) {
            return false;
        }
        int n = uart_read_bytes(s_cfg.uart, buf + have, PMS_FRAME_LEN - have, ms_to_ticks_ceil(remaining_ms));
        if (n <= 0) {
            continue;
        }
        have += (size_t)n;

        size_t i = 0;
        while (i < have && !(buf[i] == 0x42 && (i + 1 == have || buf[i + 1] == 0x4D))) {
            i++;
        }
        if (i > 0) {
            memmove(buf, buf + i, have - i);
            have -= i;
        }
    }
    return true;
}

esp_err_t pms_read(pms_data_t *out, uint32_t timeout_ms)
{
    uint8_t frame[PMS_FRAME_LEN];
    esp_err_t err;
    if (!pms_collect(frame, s_request_us + (int64_t)timeout_ms * 1000)) {
        err = ESP_ERR_TIMEOUT;
    } else {
        pms_status_t st = pms_parse(frame, out);
        err = st == PMS_OK ? ESP_OK : st == PMS_ERR_CHECKSUM ? ESP_ERR_INVALID_CRC : ESP_ERR_INVALID_RESPONSE;
        if (st != PMS_OK) {
            ESP_LOGD(TAG, "bad frame: status %d", st);
        }
    }

    if (err == ESP_OK) {
        s_fail_streak = 0;
    } else if (++s_fail_streak >= PMS_FAILS_BEFORE_RESET) {
        pms_start_reset();
    }
    return err;
}

bool pms_warming_up(void)
{
    return s_resetting || esp_timer_get_time() - s_start_us < PMS_WARMUP_US;
}

uint32_t pms_reset_count(void)
{
    return s_reset_count;
}
