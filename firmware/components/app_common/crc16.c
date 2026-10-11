#include "crc16.h"

#define CRC16_CHECK_VALUE 0x29B1u

uint16_t crc16_ccitt_sw(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint16_t crc = 0xFFFF;

    while (len--) {
        crc ^= (uint16_t)(*p++) << 8;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

#ifdef ESP_PLATFORM
#include "esp_rom_crc.h"

static bool s_use_rom;

/* The ROM routine inverts on entry and exit, so ~f(~0xFFFF) yields CCITT-FALSE. */
static uint16_t crc16_rom(const void *data, size_t len)
{
    return (uint16_t)~esp_rom_crc16_be((uint16_t)~0xFFFFu, data, len);
}

bool crc16_init(void)
{
    static const char check[] = "123456789";
    s_use_rom = crc16_rom(check, 9) == CRC16_CHECK_VALUE;
    return s_use_rom;
}

uint16_t crc16_ccitt(const void *data, size_t len)
{
    return s_use_rom ? crc16_rom(data, len) : crc16_ccitt_sw(data, len);
}
#else
bool crc16_init(void)
{
    return false;
}

uint16_t crc16_ccitt(const void *data, size_t len)
{
    return crc16_ccitt_sw(data, len);
}
#endif
