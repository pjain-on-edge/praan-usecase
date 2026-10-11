#include "esp_log.h"
#include "bme280.h"
#include "bme280_comp.h"

#define BME280_ADDR         0x76
#define BME280_TIMEOUT_MS   50
#define BME280_CHIP_ID      0x60

#define REG_CALIB_TP        0x88
#define REG_CHIP_ID         0xD0
#define REG_CALIB_H         0xE1
#define REG_CTRL_HUM        0xF2
#define REG_CTRL_MEAS       0xF4
#define REG_CONFIG          0xF5
#define REG_DATA            0xF7

#define OSRS_X1             0x01
#define MODE_FORCED         0x01
#define CTRL_MEAS_FORCED    (OSRS_X1 << 5 | OSRS_X1 << 2 | MODE_FORCED)

/* An ADC that skipped a conversion reports these reset values. */
#define ADC_PT_SKIPPED      0x80000
#define ADC_H_SKIPPED       0x8000

static const char *TAG = "bme280";

static i2c_master_dev_handle_t s_dev;
static bool s_present;
static bme280_calib_t s_cal;

static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, BME280_TIMEOUT_MS);
}

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), BME280_TIMEOUT_MS);
}

esp_err_t bme280_init(i2c_master_bus_handle_t bus, uint32_t scl_hz)
{
    s_present = false;
    if (i2c_master_probe(bus, BME280_ADDR, BME280_TIMEOUT_MS) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err;
    if (!s_dev) {
        const i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = BME280_ADDR,
            .scl_speed_hz = scl_hz,
        };
        err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
        if (err != ESP_OK) {
            return err;
        }
    }

    uint8_t id = 0;
    err = reg_read(REG_CHIP_ID, &id, 1);
    if (err != ESP_OK) {
        return err;
    }
    if (id != BME280_CHIP_ID) {
        ESP_LOGE(TAG, "chip id 0x%02x, expected 0x%02x (BMP280 has no humidity)", id, BME280_CHIP_ID);
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t tp[BME280_CALIB_TP_LEN], h[BME280_CALIB_H_LEN];
    err = reg_read(REG_CALIB_TP, tp, sizeof(tp));
    if (err == ESP_OK) {
        err = reg_read(REG_CALIB_H, h, sizeof(h));
    }
    if (err == ESP_OK) {
        err = reg_write(REG_CONFIG, 0x00);  /* filter off, standby irrelevant in forced mode */
    }
    /* ctrl_hum only latches on the next ctrl_meas write, so it must go first. */
    if (err == ESP_OK) {
        err = reg_write(REG_CTRL_HUM, OSRS_X1);
    }
    if (err != ESP_OK) {
        return err;
    }
    bme280_parse_calib(tp, h, &s_cal);
    s_present = true;
    ESP_LOGI(TAG, "found at 0x%02x", BME280_ADDR);
    return ESP_OK;
}

bool bme280_present(void)
{
    return s_present;
}

esp_err_t bme280_trigger(void)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    return reg_write(REG_CTRL_MEAS, CTRL_MEAS_FORCED);
}

esp_err_t bme280_read(bme280_reading_t *out)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t data[BME280_DATA_LEN];
    esp_err_t err = reg_read(REG_DATA, data, sizeof(data));
    if (err != ESP_OK) {
        return err;
    }
    int32_t adc_p, adc_t, adc_h;
    bme280_unpack(data, &adc_p, &adc_t, &adc_h);
    if (adc_t == ADC_PT_SKIPPED || adc_p == ADC_PT_SKIPPED || adc_h == ADC_H_SKIPPED) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    bme280_comp_t c;
    bme280_compensate(&s_cal, data, &c);
    out->t_cdeg = (int16_t)c.t_cdeg;
    out->p_dhpa = (uint16_t)((c.p_q24_8 / 256) / 10);
    out->rh_cpct = (uint16_t)((c.h_q22_10 * 100) / 1024);
    return ESP_OK;
}
