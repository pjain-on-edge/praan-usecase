#include "esp_log.h"
#include "esp_attr.h"
#include "ds3231.h"
#include "ds3231_time.h"

#define DS3231_ADDR         0x68
#define DS3231_TIMEOUT_MS   50

#define REG_TIME            0x00
#define REG_CONTROL         0x0E
#define REG_STATUS          0x0F
#define REG_TEMP_MSB        0x11

#define STATUS_OSF          0x80
#define STATUS_EN32KHZ      0x08

static const char *TAG = "ds3231";

static i2c_master_dev_handle_t s_dev;
static bool s_present;
static TaskHandle_t s_sqw_task;
static uint32_t s_sqw_bit;

static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, DS3231_TIMEOUT_MS);
}

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), DS3231_TIMEOUT_MS);
}

esp_err_t ds3231_init(i2c_master_bus_handle_t bus, uint32_t scl_hz, bool *osf)
{
    s_present = false;
    if (i2c_master_probe(bus, DS3231_ADDR, DS3231_TIMEOUT_MS) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err;
    if (!s_dev) {
        const i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = DS3231_ADDR,
            .scl_speed_hz = scl_hz,
        };
        err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
        if (err != ESP_OK) {
            return err;
        }
    }

    /* EOSC=0 (oscillator on), INTCN=0 (SQW instead of alarm interrupt), RS=00 (1 Hz).
     * Power-on default is INTCN=1, which would leave SQW silent. */
    err = reg_write(REG_CONTROL, 0x00);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t status = 0;
    err = reg_read(REG_STATUS, &status, 1);
    if (err != ESP_OK) {
        return err;
    }
    /* Writing OSF as 1 leaves it unchanged; only a valid set_time clears it. */
    err = reg_write(REG_STATUS, status & (uint8_t)~STATUS_EN32KHZ);
    if (err != ESP_OK) {
        return err;
    }
    *osf = (status & STATUS_OSF) != 0;
    s_present = true;
    ESP_LOGI(TAG, "found at 0x%02x, OSF=%d", DS3231_ADDR, *osf);
    return ESP_OK;
}

bool ds3231_present(void)
{
    return s_present;
}

esp_err_t ds3231_read_time(uint32_t *epoch)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    /* One burst: the chip latches all time registers at the START, so a
     * rollover between bytes cannot produce a torn value. */
    uint8_t regs[DS3231_TIME_REGS];
    esp_err_t err = reg_read(REG_TIME, regs, sizeof(regs));
    if (err != ESP_OK) {
        return err;
    }
    return ds3231_regs_to_epoch(regs, epoch) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t ds3231_set_time(uint32_t epoch)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[1 + DS3231_TIME_REGS] = {REG_TIME};
    if (!ds3231_epoch_to_regs(epoch, &buf[1])) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = i2c_master_transmit(s_dev, buf, sizeof(buf), DS3231_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t status = 0;
    err = reg_read(REG_STATUS, &status, 1);
    if (err != ESP_OK) {
        return err;
    }
    return reg_write(REG_STATUS, status & (uint8_t)~(STATUS_OSF | STATUS_EN32KHZ));
}

esp_err_t ds3231_read_temp(int16_t *t_cdeg)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t raw[2];
    esp_err_t err = reg_read(REG_TEMP_MSB, raw, sizeof(raw));
    if (err != ESP_OK) {
        return err;
    }
    *t_cdeg = (int16_t)((int8_t)raw[0] * 100 + (raw[1] >> 6) * 25);
    return ESP_OK;
}

static void IRAM_ATTR sqw_isr(void *arg)
{
    (void)arg;
    BaseType_t woken = pdFALSE;
    xTaskNotifyFromISR(s_sqw_task, s_sqw_bit, eSetBits, &woken);
    portYIELD_FROM_ISR(woken);
}

esp_err_t ds3231_sqw_attach(gpio_num_t pin, TaskHandle_t task, uint32_t notify_bit)
{
    s_sqw_task = task;
    s_sqw_bit = notify_bit;
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,     /* external 10 k */
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    esp_err_t err = gpio_config(&io);
    if (err == ESP_OK) {
        err = gpio_isr_handler_add(pin, sqw_isr, NULL);
    }
    return err;
}
