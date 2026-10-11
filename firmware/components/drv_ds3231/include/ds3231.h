#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Probes 0x68, enables the 1 Hz SQW output and reports the oscillator-stop flag.
 * OSF stays set in the chip until ds3231_set_time() writes a valid time. */
esp_err_t ds3231_init(i2c_master_bus_handle_t bus, uint32_t scl_hz, bool *osf);
bool ds3231_present(void);
esp_err_t ds3231_read_time(uint32_t *epoch);
esp_err_t ds3231_set_time(uint32_t epoch);
esp_err_t ds3231_read_temp(int16_t *t_cdeg);

/* Falling edge on the open-drain SQW line sets notify_bit on task. The GPIO ISR
 * service must already be installed. */
esp_err_t ds3231_sqw_attach(gpio_num_t pin, TaskHandle_t task, uint32_t notify_bit);
