#include "unity.h"
#include "sht30_conv.h"

void setUp(void) {}
void tearDown(void) {}

static void make_frame(uint8_t f[6], uint16_t t_raw, uint16_t rh_raw)
{
    f[0] = (uint8_t)(t_raw >> 8);
    f[1] = (uint8_t)t_raw;
    f[2] = sht30_crc8(&f[0], 2);
    f[3] = (uint8_t)(rh_raw >> 8);
    f[4] = (uint8_t)rh_raw;
    f[5] = sht30_crc8(&f[3], 2);
}

static void test_temperature_endpoints(void)
{
    TEST_ASSERT_EQUAL_INT16(-4500, sht30_t_cdeg(0));
    TEST_ASSERT_EQUAL_INT16(13000, sht30_t_cdeg(0xFFFF));
    /* 25 °C is raw 26214.0 exactly: -45 + 175 * 26214 / 65535 = 25.0 */
    TEST_ASSERT_INT16_WITHIN(1, 2500, sht30_t_cdeg(26214));
}

static void test_humidity_endpoints(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, sht30_rh_cpct(0));
    TEST_ASSERT_EQUAL_UINT16(10000, sht30_rh_cpct(0xFFFF));
    TEST_ASSERT_EQUAL_UINT16(5000, sht30_rh_cpct(32768));
}

static void test_decode_valid_frame(void)
{
    uint8_t f[6];
    make_frame(f, 26214, 32768);
    int16_t t;
    uint16_t rh;
    TEST_ASSERT_TRUE(sht30_decode(f, &t, &rh));
    TEST_ASSERT_INT16_WITHIN(1, 2500, t);
    TEST_ASSERT_EQUAL_UINT16(5000, rh);
}

static void test_decode_rejects_bad_crc(void)
{
    uint8_t f[6];
    int16_t t = 0;
    uint16_t rh = 0;
    make_frame(f, 26214, 32768);
    f[2] ^= 0x01;
    TEST_ASSERT_FALSE(sht30_decode(f, &t, &rh));
    make_frame(f, 26214, 32768);
    f[4] ^= 0x80;
    TEST_ASSERT_FALSE(sht30_decode(f, &t, &rh));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_temperature_endpoints);
    RUN_TEST(test_humidity_endpoints);
    RUN_TEST(test_decode_valid_frame);
    RUN_TEST(test_decode_rejects_bad_crc);
    return UNITY_END();
}
