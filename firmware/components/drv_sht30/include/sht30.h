#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/* Probes 0x44. May be called again later to pick up a late-attached sensor. */
esp_err_t sht30_init(i2c_master_bus_handle_t bus, uint32_t scl_hz);
bool sht30_present(void);
/* Starts a single-shot high-repeatability measurement; result ready after 15.5 ms. */
esp_err_t sht30_trigger(void);
esp_err_t sht30_read(int16_t *t_cdeg, uint16_t *rh_cpct);
