#include "esp_log.h"
#include "sht30.h"
#include "sht30_conv.h"

#define SHT30_ADDR              0x44
#define SHT30_TIMEOUT_MS        50
#define SHT30_FAILS_BEFORE_RESET 3

static const char *TAG = "sht30";

static i2c_master_dev_handle_t s_dev;
static bool s_present;
static uint8_t s_fail_streak;

static void sht30_soft_reset(void)
{
    static const uint8_t cmd[2] = {0x30, 0xA2};
    esp_err_t err = i2c_master_transmit(s_dev, cmd, sizeof(cmd), SHT30_TIMEOUT_MS);
    ESP_LOGW(TAG, "soft reset after %u failures: %s", s_fail_streak, esp_err_to_name(err));
    s_fail_streak = 0;
}

static esp_err_t sht30_track(esp_err_t err)
{
    if (err == ESP_OK) {
        s_fail_streak = 0;
    } else if (++s_fail_streak >= SHT30_FAILS_BEFORE_RESET) {
        sht30_soft_reset();
    }
    return err;
}

esp_err_t sht30_init(i2c_master_bus_handle_t bus, uint32_t scl_hz)
{
    s_present = false;
    esp_err_t err = i2c_master_probe(bus, SHT30_ADDR, SHT30_TIMEOUT_MS);
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    if (!s_dev) {
        const i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = SHT30_ADDR,
            .scl_speed_hz = scl_hz,
        };
        err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
        if (err != ESP_OK) {
            return err;
        }
    }
    s_present = true;
    s_fail_streak = 0;
    ESP_LOGI(TAG, "found at 0x%02x", SHT30_ADDR);
    return ESP_OK;
}

bool sht30_present(void)
{
    return s_present;
}

esp_err_t sht30_trigger(void)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    /* 0x2400: high repeatability, clock stretching disabled, so a read before the
     * conversion completes is NACKed instead of holding SCL. */
    static const uint8_t cmd[2] = {0x24, 0x00};
    return sht30_track(i2c_master_transmit(s_dev, cmd, sizeof(cmd), SHT30_TIMEOUT_MS));
}

esp_err_t sht30_read(int16_t *t_cdeg, uint16_t *rh_cpct)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t raw[6];
    esp_err_t err = i2c_master_receive(s_dev, raw, sizeof(raw), SHT30_TIMEOUT_MS);
    if (err == ESP_OK && !sht30_decode(raw, t_cdeg, rh_cpct)) {
        err = ESP_ERR_INVALID_CRC;
    }
    return sht30_track(err);
}
