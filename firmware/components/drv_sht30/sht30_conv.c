#include "sht30_conv.h"

uint8_t sht30_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

int16_t sht30_t_cdeg(uint16_t raw)
{
    return (int16_t)(-4500 + (17500 * (int32_t)raw) / 65535);
}

uint16_t sht30_rh_cpct(uint16_t raw)
{
    int32_t rh = (10000 * (int32_t)raw) / 65535;
    if (rh < 0) {
        rh = 0;
    } else if (rh > 10000) {
        rh = 10000;
    }
    return (uint16_t)rh;
}

bool sht30_decode(const uint8_t raw[6], int16_t *t_cdeg, uint16_t *rh_cpct)
{
    if (sht30_crc8(&raw[0], 2) != raw[2] || sht30_crc8(&raw[3], 2) != raw[5]) {
        return false;
    }
    *t_cdeg = sht30_t_cdeg((uint16_t)(raw[0] << 8 | raw[1]));
    *rh_cpct = sht30_rh_cpct((uint16_t)(raw[3] << 8 | raw[4]));
    return true;
}
