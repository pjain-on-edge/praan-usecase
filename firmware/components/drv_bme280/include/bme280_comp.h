#pragma once
#include <stdint.h>

#define BME280_CALIB_TP_LEN 26  /* 0x88..0xA1 */
#define BME280_CALIB_H_LEN  7   /* 0xE1..0xE7 */
#define BME280_DATA_LEN     8   /* 0xF7..0xFE */

typedef struct {
    uint16_t t1; int16_t t2, t3;
    uint16_t p1; int16_t p2, p3, p4, p5, p6, p7, p8, p9;
    uint8_t h1; int16_t h2; uint8_t h3; int16_t h4, h5; int8_t h6;
} bme280_calib_t;

typedef struct {
    int32_t t_cdeg;     /* 0.01 °C */
    uint32_t p_q24_8;   /* Pa, Q24.8 */
    uint32_t h_q22_10;  /* %RH, Q22.10 */
} bme280_comp_t;

void bme280_parse_calib(const uint8_t tp[BME280_CALIB_TP_LEN], const uint8_t h[BME280_CALIB_H_LEN],
                        bme280_calib_t *cal);
/* Splits the 0xF7..0xFE burst into 20-bit P, 20-bit T and 16-bit H ADC values. */
void bme280_unpack(const uint8_t data[BME280_DATA_LEN], int32_t *adc_p, int32_t *adc_t, int32_t *adc_h);

/* Bosch reference integer compensation (BME280 datasheet 4.2.3). Temperature must
 * be compensated first: pressure and humidity depend on its t_fine. */
int32_t bme280_comp_t_fine(const bme280_calib_t *cal, int32_t adc_t);
int32_t bme280_comp_temp(int32_t t_fine);
uint32_t bme280_comp_press(const bme280_calib_t *cal, int32_t t_fine, int32_t adc_p);
uint32_t bme280_comp_hum(const bme280_calib_t *cal, int32_t t_fine, int32_t adc_h);

void bme280_compensate(const bme280_calib_t *cal, const uint8_t data[BME280_DATA_LEN], bme280_comp_t *out);
