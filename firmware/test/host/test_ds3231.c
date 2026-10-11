#include "unity.h"
#include "ds3231_time.h"

void setUp(void) {}
void tearDown(void) {}

static void test_bcd_round_trip(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x59, ds3231_bin2bcd(59));
    TEST_ASSERT_EQUAL_UINT8(59, ds3231_bcd2bin(0x59));
    for (uint8_t v = 0; v < 100; v++) {
        TEST_ASSERT_EQUAL_UINT8(v, ds3231_bcd2bin(ds3231_bin2bcd(v)));
    }
}

static void test_known_epoch(void)
{
    /* 2025-10-11 08:00:00 UTC, a Saturday */
    const uint8_t regs[7] = {0x00, 0x00, 0x08, 0x06, 0x11, 0x10, 0x25};
    uint32_t epoch = 0;
    TEST_ASSERT_TRUE(ds3231_regs_to_epoch(regs, &epoch));
    TEST_ASSERT_EQUAL_UINT32(1760169600u, epoch);

    uint8_t out[7];
    TEST_ASSERT_TRUE(ds3231_epoch_to_regs(1760169600u, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(regs, out, 7);
}

static void test_leap_day(void)
{
    const uint8_t regs[7] = {0x59, 0x59, 0x23, 0x04, 0x29, 0x02, 0x24};
    uint32_t epoch = 0;
    TEST_ASSERT_TRUE(ds3231_regs_to_epoch(regs, &epoch));
    TEST_ASSERT_EQUAL_UINT32(1709251199u, epoch);
}

static void test_range_limits(void)
{
    uint8_t r[7];
    uint32_t e;
    TEST_ASSERT_TRUE(ds3231_epoch_to_regs(946684800u, r));      /* 2000-01-01 */
    TEST_ASSERT_TRUE(ds3231_regs_to_epoch(r, &e));
    TEST_ASSERT_EQUAL_UINT32(946684800u, e);
    TEST_ASSERT_TRUE(ds3231_epoch_to_regs(4102444799u, r));     /* 2099-12-31 23:59:59 */
    TEST_ASSERT_TRUE(ds3231_regs_to_epoch(r, &e));
    TEST_ASSERT_EQUAL_UINT32(4102444799u, e);
    TEST_ASSERT_FALSE(ds3231_epoch_to_regs(946684799u, r));
    TEST_ASSERT_FALSE(ds3231_epoch_to_regs(4102444800u, r));
}

static void test_round_trip_sweep(void)
{
    for (uint32_t e = 946684800u; e < 4102444800u - 7919u * 86400u; e += 7919u * 86400u + 3607u) {
        uint8_t r[7];
        uint32_t back = 0;
        TEST_ASSERT_TRUE(ds3231_epoch_to_regs(e, r));
        TEST_ASSERT_TRUE(ds3231_regs_to_epoch(r, &back));
        TEST_ASSERT_EQUAL_UINT32(e, back);
    }
}

static void test_rejects_invalid_fields(void)
{
    uint32_t e;
    const uint8_t bad_month[7] = {0x00, 0x00, 0x00, 0x01, 0x01, 0x13, 0x25};
    const uint8_t feb30[7] = {0x00, 0x00, 0x00, 0x01, 0x30, 0x02, 0x25};
    const uint8_t feb29_2025[7] = {0x00, 0x00, 0x00, 0x01, 0x29, 0x02, 0x25};
    const uint8_t bad_sec[7] = {0x60, 0x00, 0x00, 0x01, 0x01, 0x01, 0x25};
    TEST_ASSERT_FALSE(ds3231_regs_to_epoch(bad_month, &e));
    TEST_ASSERT_FALSE(ds3231_regs_to_epoch(feb30, &e));
    TEST_ASSERT_FALSE(ds3231_regs_to_epoch(feb29_2025, &e));
    TEST_ASSERT_FALSE(ds3231_regs_to_epoch(bad_sec, &e));
}

static void test_twelve_hour_mode(void)
{
    uint32_t am, pm, midnight;
    const uint8_t r_am[7] = {0x00, 0x00, 0x40 | 0x08, 0x06, 0x11, 0x10, 0x25};         /* 8 AM */
    const uint8_t r_pm[7] = {0x00, 0x00, 0x40 | 0x20 | 0x08, 0x06, 0x11, 0x10, 0x25};  /* 8 PM */
    const uint8_t r_12am[7] = {0x00, 0x00, 0x40 | 0x12, 0x06, 0x11, 0x10, 0x25};       /* 12 AM */
    TEST_ASSERT_TRUE(ds3231_regs_to_epoch(r_am, &am));
    TEST_ASSERT_TRUE(ds3231_regs_to_epoch(r_pm, &pm));
    TEST_ASSERT_TRUE(ds3231_regs_to_epoch(r_12am, &midnight));
    TEST_ASSERT_EQUAL_UINT32(1760169600u, am);
    TEST_ASSERT_EQUAL_UINT32(1760169600u + 12 * 3600, pm);
    TEST_ASSERT_EQUAL_UINT32(1760169600u - 8 * 3600, midnight);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bcd_round_trip);
    RUN_TEST(test_known_epoch);
    RUN_TEST(test_leap_day);
    RUN_TEST(test_range_limits);
    RUN_TEST(test_round_trip_sweep);
    RUN_TEST(test_rejects_invalid_fields);
    RUN_TEST(test_twelve_hour_mode);
    return UNITY_END();
}
