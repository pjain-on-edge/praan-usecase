#pragma once
#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pms5003.h"

typedef struct {
    gpio_num_t i2c_sda;
    gpio_num_t i2c_scl;
    uint32_t i2c_hz;
    gpio_num_t rtc_sqw;
    pms_config_t pms;
} sensor_mgr_config_t;

/* I2C bus, SHT30, BME280, DS3231 (time base, settimeofday) and PMS5003.
 * A missing sensor is logged and sampled as invalid; it never fails boot. */
esp_err_t sensor_mgr_init(const sensor_mgr_config_t *cfg);
/* Creates sensor_task and attaches the SQW interrupt. Sampling starts here. */
esp_err_t sensor_mgr_start(QueueHandle_t out_queue);
TaskHandle_t sensor_mgr_task(void);
/* SNTP has set the system clock: sensor_task corrects the RTC and re-anchors. */
void sensor_mgr_time_synced(void);
