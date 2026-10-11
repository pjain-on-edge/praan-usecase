#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    int16_t t_cdeg;
    uint16_t rh_cpct;
    uint16_t p_dhpa;
} bme280_reading_t;

/* Probes 0x76, checks the chip ID and reads calibration once. */
esp_err_t bme280_init(i2c_master_bus_handle_t bus, uint32_t scl_hz);
bool bme280_present(void);
/* Starts one forced-mode conversion (x1/x1/x1, about 9.3 ms worst case). */
esp_err_t bme280_trigger(void);
esp_err_t bme280_read(bme280_reading_t *out);
