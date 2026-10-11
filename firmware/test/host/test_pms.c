#include <string.h>
#include "unity.h"
#include "pms_parse.h"

void setUp(void) {}
void tearDown(void) {}

static void put16(uint8_t *f, int off, uint16_t v)
{
    f[off] = (uint8_t)(v >> 8);
    f[off + 1] = (uint8_t)v;
}

static void fix_checksum(uint8_t *f)
{
    uint16_t sum = 0;
    for (int i = 0; i < 30; i++) {
        sum = (uint16_t)(sum + f[i]);
    }
    put16(f, 30, sum);
}

/* CF=1 fields carry different values so a parser reading the wrong offsets fails. */
static void make_frame(uint8_t f[PMS_FRAME_LEN], uint16_t pm1, uint16_t pm25, uint16_t pm10)
{
    memset(f, 0, PMS_FRAME_LEN);
    f[0] = 0x42;
    f[1] = 0x4D;
    put16(f, 2, 28);
    put16(f, 4, 111);
    put16(f, 6, 222);
    put16(f, 8, 333);
    put16(f, 10, pm1);
    put16(f, 12, pm25);
    put16(f, 14, pm10);
    put16(f, 16, 3000);
    put16(f, 18, 900);
    put16(f, 20, 200);
    put16(f, 22, 20);
    fix_checksum(f);
}

static void test_valid_frame_uses_atmospheric_values(void)
{
    uint8_t f[PMS_FRAME_LEN];
    make_frame(f, 12, 18, 21);
    pms_data_t d;
    TEST_ASSERT_EQUAL(PMS_OK, pms_parse(f, &d));
    TEST_ASSERT_EQUAL_UINT16(12, d.pm1);
    TEST_ASSERT_EQUAL_UINT16(18, d.pm25);
    TEST_ASSERT_EQUAL_UINT16(21, d.pm10);
    TEST_ASSERT_EQUAL_UINT16(3000, d.n03);
    TEST_ASSERT_EQUAL_UINT16(900, d.n05);
    TEST_ASSERT_EQUAL_UINT16(200, d.n10);
    TEST_ASSERT_EQUAL_UINT16(20, d.n25);
}

static void test_bad_header(void)
{
    uint8_t f[PMS_FRAME_LEN];
    pms_data_t d;
    make_frame(f, 1, 2, 3);
    f[1] = 0x4E;
    fix_checksum(f);
    TEST_ASSERT_EQUAL(PMS_ERR_HEADER, pms_parse(f, &d));
}

static void test_bad_length(void)
{
    uint8_t f[PMS_FRAME_LEN];
    pms_data_t d;
    make_frame(f, 1, 2, 3);
    put16(f, 2, 20);
    fix_checksum(f);
    TEST_ASSERT_EQUAL(PMS_ERR_LENGTH, pms_parse(f, &d));
}

static void test_bad_checksum(void)
{
    uint8_t f[PMS_FRAME_LEN];
    pms_data_t d;
    make_frame(f, 1, 2, 3);
    f[13] ^= 0x01;
    TEST_ASSERT_EQUAL(PMS_ERR_CHECKSUM, pms_parse(f, &d));
}

static void test_implausible_values(void)
{
    uint8_t f[PMS_FRAME_LEN];
    pms_data_t d;
    make_frame(f, 30, 20, 40);      /* pm1 > pm2.5 */
    TEST_ASSERT_EQUAL(PMS_ERR_IMPLAUSIBLE, pms_parse(f, &d));
    make_frame(f, 10, 50, 40);      /* pm2.5 > pm10 */
    TEST_ASSERT_EQUAL(PMS_ERR_IMPLAUSIBLE, pms_parse(f, &d));
    make_frame(f, 10, 20, 1000);    /* pm10 out of range */
    TEST_ASSERT_EQUAL(PMS_ERR_IMPLAUSIBLE, pms_parse(f, &d));
    make_frame(f, 10, 20, 999);
    TEST_ASSERT_EQUAL(PMS_OK, pms_parse(f, &d));
}

static void test_failure_leaves_output_untouched(void)
{
    uint8_t f[PMS_FRAME_LEN];
    pms_data_t d = {.pm25 = 77};
    make_frame(f, 30, 20, 40);
    pms_parse(f, &d);
    TEST_ASSERT_EQUAL_UINT16(77, d.pm25);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_frame_uses_atmospheric_values);
    RUN_TEST(test_bad_header);
    RUN_TEST(test_bad_length);
    RUN_TEST(test_bad_checksum);
    RUN_TEST(test_implausible_values);
    RUN_TEST(test_failure_leaves_output_untouched);
    return UNITY_END();
}
