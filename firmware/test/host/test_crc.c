#include <string.h>
#include "unity.h"
#include "crc16.h"
#include "record.h"
#include "sht30_conv.h"

void setUp(void) {}
void tearDown(void) {}

static void test_crc16_check_value(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16_ccitt("123456789", 9));
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16_ccitt_sw("123456789", 9));
}

static void test_crc16_empty_is_init(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc16_ccitt("", 0));
}

static void test_record_seal_and_verify(void)
{
    record_t r;
    record_init_invalid(&r);
    r.ts = 1760169600;
    r.t_cdeg = 2531;
    record_seal(&r);
    TEST_ASSERT_TRUE(record_crc_ok(&r));
    TEST_ASSERT_EQUAL_HEX16(crc16_ccitt(&r, 30), r.crc16);

    r.pm25 ^= 1;
    TEST_ASSERT_FALSE(record_crc_ok(&r));
}

static void test_record_padding(void)
{
    record_t r;
    record_make_padding(&r);
    TEST_ASSERT_TRUE(record_is_padding(&r));
    TEST_ASSERT_FALSE(record_crc_ok(&r));
    r.crc16 = 0xFFFE;
    TEST_ASSERT_FALSE(record_is_padding(&r));
}

static void test_record_invalid_markers(void)
{
    record_t r;
    record_init_invalid(&r);
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, r.t_cdeg);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, r.rh_cpct);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, r.pm10);
    TEST_ASSERT_EQUAL_HEX16(0, r.flags);
}

static void test_sht30_crc8_datasheet_example(void)
{
    const uint8_t beef[2] = {0xBE, 0xEF};
    TEST_ASSERT_EQUAL_HEX8(0x92, sht30_crc8(beef, 2));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc16_check_value);
    RUN_TEST(test_crc16_empty_is_init);
    RUN_TEST(test_record_seal_and_verify);
    RUN_TEST(test_record_padding);
    RUN_TEST(test_record_invalid_markers);
    RUN_TEST(test_sht30_crc8_datasheet_example);
    return UNITY_END();
}
