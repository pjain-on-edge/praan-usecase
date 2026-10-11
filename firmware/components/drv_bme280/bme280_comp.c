#include "bme280_comp.h"

static uint16_t u16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

void bme280_parse_calib(const uint8_t tp[BME280_CALIB_TP_LEN], const uint8_t h[BME280_CALIB_H_LEN],
                        bme280_calib_t *cal)
{
    cal->t1 = u16le(&tp[0]);
    cal->t2 = (int16_t)u16le(&tp[2]);
    cal->t3 = (int16_t)u16le(&tp[4]);
    cal->p1 = u16le(&tp[6]);
    cal->p2 = (int16_t)u16le(&tp[8]);
    cal->p3 = (int16_t)u16le(&tp[10]);
    cal->p4 = (int16_t)u16le(&tp[12]);
    cal->p5 = (int16_t)u16le(&tp[14]);
    cal->p6 = (int16_t)u16le(&tp[16]);
    cal->p7 = (int16_t)u16le(&tp[18]);
    cal->p8 = (int16_t)u16le(&tp[20]);
    cal->p9 = (int16_t)u16le(&tp[22]);
    cal->h1 = tp[25];   /* 0xA1; 0xA0 is unused */

    cal->h2 = (int16_t)u16le(&h[0]);
    cal->h3 = h[2];
    /* H4 and H5 are 12-bit signed values sharing the nibbles of 0xE5. */
    cal->h4 = (int16_t)((int16_t)(int8_t)h[3] * 16 | (h[4] & 0x0F));
    cal->h5 = (int16_t)((int16_t)(int8_t)h[5] * 16 | (h[4] >> 4));
    cal->h6 = (int8_t)h[6];
}

void bme280_unpack(const uint8_t d[BME280_DATA_LEN], int32_t *adc_p, int32_t *adc_t, int32_t *adc_h)
{
    *adc_p = (int32_t)((uint32_t)d[0] << 12 | (uint32_t)d[1] << 4 | d[2] >> 4);
    *adc_t = (int32_t)((uint32_t)d[3] << 12 | (uint32_t)d[4] << 4 | d[5] >> 4);
    *adc_h = (int32_t)((uint32_t)d[6] << 8 | d[7]);
}

int32_t bme280_comp_t_fine(const bme280_calib_t *cal, int32_t adc_t)
{
    int32_t var1 = ((((adc_t >> 3) - ((int32_t)cal->t1 << 1))) * ((int32_t)cal->t2)) >> 11;
    int32_t var2 = (((((adc_t >> 4) - ((int32_t)cal->t1)) * ((adc_t >> 4) - ((int32_t)cal->t1))) >> 12) *
                    ((int32_t)cal->t3)) >> 14;
    return var1 + var2;
}

int32_t bme280_comp_temp(int32_t t_fine)
{
    return (t_fine * 5 + 128) >> 8;
}

uint32_t bme280_comp_press(const bme280_calib_t *cal, int32_t t_fine, int32_t adc_p)
{
    int64_t var1 = (int64_t)t_fine - 128000;
    int64_t var2 = var1 * var1 * (int64_t)cal->p6;
    var2 = var2 + ((var1 * (int64_t)cal->p5) * 131072);
    var2 = var2 + ((int64_t)cal->p4 * 34359738368LL);
    var1 = ((var1 * var1 * (int64_t)cal->p3) >> 8) + ((var1 * (int64_t)cal->p2) * 4096);
    var1 = ((((int64_t)1) << 47) + var1) * ((int64_t)cal->p1) >> 33;
    if (var1 == 0) {
        return 0;
    }
    int64_t p = 1048576 - adc_p;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)cal->p9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)cal->p8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + ((int64_t)cal->p7 << 4);
    return (uint32_t)p;
}

uint32_t bme280_comp_hum(const bme280_calib_t *cal, int32_t t_fine, int32_t adc_h)
{
    int32_t v = t_fine - 76800;
    v = (((((adc_h << 14) - ((int32_t)cal->h4 << 20) - ((int32_t)cal->h5 * v)) + 16384) >> 15) *
         (((((((v * (int32_t)cal->h6) >> 10) * (((v * (int32_t)cal->h3) >> 11) + 32768)) >> 10) + 2097152) *
           (int32_t)cal->h2 + 8192) >> 14));
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * (int32_t)cal->h1) >> 4);
    if (v < 0) {
        v = 0;
    } else if (v > 419430400) {
        v = 419430400;
    }
    return (uint32_t)(v >> 12);
}

void bme280_compensate(const bme280_calib_t *cal, const uint8_t data[BME280_DATA_LEN], bme280_comp_t *out)
{
    int32_t adc_p, adc_t, adc_h;
    bme280_unpack(data, &adc_p, &adc_t, &adc_h);
    int32_t t_fine = bme280_comp_t_fine(cal, adc_t);
    out->t_cdeg = bme280_comp_temp(t_fine);
    out->p_q24_8 = bme280_comp_press(cal, t_fine, adc_p);
    out->h_q22_10 = bme280_comp_hum(cal, t_fine, adc_h);
}
