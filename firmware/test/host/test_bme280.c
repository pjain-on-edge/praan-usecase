#include "unity.h"
#include "bme280_comp.h"

void setUp(void) {}
void tearDown(void) {}

/* Worked example from the Bosch BMP280 datasheet (the T/P compensation is
 * identical in the BME280). */
static const bme280_calib_t EXAMPLE = {
    .t1 = 27504, .t2 = 26435, .t3 = -1000,
    .p1 = 36477, .p2 = -10685, .p3 = 3024, .p4 = 2855, .p5 = 140, .p6 = -7,
    .p7 = 15500, .p8 = -14600, .p9 = 6000,
    .h1 = 75, .h2 = 362, .h3 = 0, .h4 = 313, .h5 = 50, .h6 = 30,
};

static void test_temperature_datasheet_example(void)
{
    int32_t t_fine = bme280_comp_t_fine(&EXAMPLE, 519888);
    TEST_ASSERT_EQUAL_INT32(128422, t_fine);
    TEST_ASSERT_EQUAL_INT32(2508, bme280_comp_temp(t_fine));
}

static void test_pressure_datasheet_example(void)
{
    uint32_t p = bme280_comp_press(&EXAMPLE, 128422, 415148);
    /* Exact int64 result; the datasheet's floating-point value is 100653.27 Pa. */
    TEST_ASSERT_EQUAL_UINT32(25767233, p);
    TEST_ASSERT_UINT32_WITHIN(1, 100653, p / 256);
}

/* Floating-point humidity formula from the BME280 datasheet, section 8.1. */
static double ref_humidity(const bme280_calib_t *c, int32_t t_fine, int32_t adc_h)
{
    double h = (double)t_fine - 76800.0;
    h = (adc_h - ((double)c->h4 * 64.0 + (double)c->h5 / 16384.0 * h)) *
        ((double)c->h2 / 65536.0 * (1.0 + (double)c->h6 / 67108864.0 * h * (1.0 + (double)c->h3 / 67108864.0 * h)));
    h = h * (1.0 - (double)c->h1 * h / 524288.0);
    return h < 0 ? 0 : h > 100 ? 100 : h;
}

static void test_humidity_matches_float_reference(void)
{
    const int32_t adc_h[] = {20000, 26000, 30000, 36000};
    for (unsigned i = 0; i < sizeof(adc_h) / sizeof(adc_h[0]); i++) {
        /* Q22.10: 1 %RH = 1024 LSB, so 10 LSB is 0.01 %RH. */
        int32_t expect = (int32_t)(ref_humidity(&EXAMPLE, 128422, adc_h[i]) * 1024.0 + 0.5);
        int32_t got = (int32_t)bme280_comp_hum(&EXAMPLE, 128422, adc_h[i]);
        TEST_ASSERT_INT32_WITHIN(10, expect, got);
    }
}

static void test_humidity_clamped(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, bme280_comp_hum(&EXAMPLE, 128422, 0));
    TEST_ASSERT_EQUAL_UINT32(100u << 10, bme280_comp_hum(&EXAMPLE, 128422, 65535));
}

static void test_parse_calibration_layout(void)
{
    uint8_t tp[BME280_CALIB_TP_LEN] = {0};
    tp[0] = 0x70; tp[1] = 0x6B;         /* T1 = 27504 */
    tp[4] = 0x18; tp[5] = 0xFC;         /* T3 = -1000 */
    tp[22] = 0x70; tp[23] = 0x17;       /* P9 = 6000 */
    tp[24] = 0xAA;                      /* 0xA0, unused */
    tp[25] = 75;                        /* H1 */
    /* H4 = 0x139 (313), H5 = -50 (0xFCE): E4=0x13, E5=0xE9, E6=0xFC */
    const uint8_t h[BME280_CALIB_H_LEN] = {0x6A, 0x01, 0x00, 0x13, 0xE9, 0xFC, 0x1E};
    bme280_calib_t c;
    bme280_parse_calib(tp, h, &c);
    TEST_ASSERT_EQUAL_UINT16(27504, c.t1);
    TEST_ASSERT_EQUAL_INT16(-1000, c.t3);
    TEST_ASSERT_EQUAL_INT16(6000, c.p9);
    TEST_ASSERT_EQUAL_UINT8(75, c.h1);
    TEST_ASSERT_EQUAL_INT16(362, c.h2);
    TEST_ASSERT_EQUAL_INT16(313, c.h4);
    TEST_ASSERT_EQUAL_INT16(-50, c.h5);
    TEST_ASSERT_EQUAL_INT8(30, c.h6);
}

static void test_unpack_burst(void)
{
    /* adc_P = 415148 = 0x655AC, adc_T = 519888 = 0x7EED0, adc_H = 0x7530 */
    const uint8_t d[BME280_DATA_LEN] = {0x65, 0x5A, 0xC0, 0x7E, 0xED, 0x00, 0x75, 0x30};
    int32_t p, t, hum;
    bme280_unpack(d, &p, &t, &hum);
    TEST_ASSERT_EQUAL_INT32(415148, p);
    TEST_ASSERT_EQUAL_INT32(519888, t);
    TEST_ASSERT_EQUAL_INT32(30000, hum);

    bme280_comp_t out;
    bme280_compensate(&EXAMPLE, d, &out);
    TEST_ASSERT_EQUAL_INT32(2508, out.t_cdeg);
    TEST_ASSERT_EQUAL_UINT32(25767233, out.p_q24_8);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_temperature_datasheet_example);
    RUN_TEST(test_pressure_datasheet_example);
    RUN_TEST(test_humidity_matches_float_reference);
    RUN_TEST(test_humidity_clamped);
    RUN_TEST(test_parse_calibration_layout);
    RUN_TEST(test_unpack_burst);
    return UNITY_END();
}
