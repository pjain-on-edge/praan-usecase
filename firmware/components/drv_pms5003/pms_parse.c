#include "pms_parse.h"

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

pms_status_t pms_parse(const uint8_t f[PMS_FRAME_LEN], pms_data_t *out)
{
    if (f[0] != 0x42 || f[1] != 0x4D) {
        return PMS_ERR_HEADER;
    }
    if (be16(&f[2]) != PMS_FRAME_LEN - 4) {
        return PMS_ERR_LENGTH;
    }
    uint16_t sum = 0;
    for (int i = 0; i < PMS_FRAME_LEN - 2; i++) {
        sum = (uint16_t)(sum + f[i]);
    }
    if (sum != be16(&f[30])) {
        return PMS_ERR_CHECKSUM;
    }

    /* Bytes 4..9 are the CF=1 "standard particle" values; 10..15 are atmospheric. */
    pms_data_t d = {
        .pm1 = be16(&f[10]),
        .pm25 = be16(&f[12]),
        .pm10 = be16(&f[14]),
        .n03 = be16(&f[16]),
        .n05 = be16(&f[18]),
        .n10 = be16(&f[20]),
        .n25 = be16(&f[22]),
    };
    if (d.pm1 > d.pm25 || d.pm25 > d.pm10 || d.pm10 >= 1000) {
        return PMS_ERR_IMPLAUSIBLE;
    }
    *out = d;
    return PMS_OK;
}
