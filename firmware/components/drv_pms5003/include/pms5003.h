#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "pms_parse.h"

typedef struct {
    uart_port_t uart;
    gpio_num_t tx, rx, set, reset;
} pms_config_t;

/* Configures UART and control pins and puts the sensor in passive mode.
 * ESP_ERR_NOT_FOUND means no reply; the driver stays usable and keeps retrying. */
esp_err_t pms_init(const pms_config_t *cfg);
/* Flushes RX and sends a passive read request. Returns ESP_ERR_INVALID_STATE
 * while a reset is in progress. */
esp_err_t pms_request(void);
/* Waits for the frame until timeout_ms after the last request. Three consecutive
 * failures pulse RESET; the sensor is re-armed one second later. */
esp_err_t pms_read(pms_data_t *out, uint32_t timeout_ms);
/* True for 30 s after start, wake or reset. */
bool pms_warming_up(void);
uint32_t pms_reset_count(void);
