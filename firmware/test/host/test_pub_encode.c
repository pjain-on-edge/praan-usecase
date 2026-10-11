#include <string.h>
#include "unity.h"
#include "pub_encode.h"
#include "record.h"

void setUp(void) {}
void tearDown(void) {}

static record_t rec(uint32_t ts, int16_t t, uint16_t rh, uint16_t p, uint16_t pm1, uint16_t pm25,
                    uint16_t pm10, uint16_t flags)
{
    record_t r;
    record_init_invalid(&r);
    r.ts = ts;
    r.t_cdeg = t;
    r.rh_cpct = rh;
    r.p_dhpa = p;
    r.pm1 = pm1;
    r.pm25 = pm25;
    r.pm10 = pm10;
    r.flags = flags;
    record_seal(&r);
    return r;
}

static const char SPEC_EXAMPLE[] =
    "{\"v\":1,\"s0\":123456,\"t0\":1760169600,\"d\":[[0,2531,6512,10132,12,18,21,0],"
    "[1,2533,6510,10132,12,19,22,0]]}";

static void test_telemetry_exact_output(void)
{
    const record_t r[2] = {
        rec(1760169600, 2531, 6512, 10132, 12, 18, 21, 0),
        rec(1760169601, 2533, 6510, 10132, 12, 19, 22, 0),
    };
    char buf[PUB_TELEMETRY_BUF_LEN];
    int n = pub_encode_telemetry(buf, sizeof(buf), 123456, r, 2);
    TEST_ASSERT_EQUAL_STRING(SPEC_EXAMPLE, buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(SPEC_EXAMPLE), n);
}

static void test_telemetry_invalid_and_negative_values(void)
{
    const record_t r = rec(1000, -1250, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
                           FLAG_TRH_FALLBACK | FLAG_PM_INVALID);
    char buf[PUB_TELEMETRY_BUF_LEN];
    TEST_ASSERT_GREATER_THAN(0, pub_encode_telemetry(buf, sizeof(buf), 7, &r, 1));
    TEST_ASSERT_EQUAL_STRING("{\"v\":1,\"s0\":7,\"t0\":1000,\"d\":[[0,-1250,65535,65535,65535,65535,65535,9]]}", buf);
}

static void test_telemetry_truncation(void)
{
    const record_t r[2] = {
        rec(1760169600, 2531, 6512, 10132, 12, 18, 21, 0),
        rec(1760169601, 2533, 6510, 10132, 12, 19, 22, 0),
    };
    size_t need = strlen(SPEC_EXAMPLE) + 1;
    char buf[PUB_TELEMETRY_BUF_LEN];
    TEST_ASSERT_EQUAL_INT((int)need - 1, pub_encode_telemetry(buf, need, 123456, r, 2));
    TEST_ASSERT_EQUAL_INT(-1, pub_encode_telemetry(buf, need - 1, 123456, r, 2));
    TEST_ASSERT_EQUAL_INT(-1, pub_encode_telemetry(buf, 20, 123456, r, 2));
    TEST_ASSERT_EQUAL_INT(-1, pub_encode_telemetry(buf, sizeof(buf), 123456, r, 0));
}

static void test_telemetry_worst_case_fits_buffer(void)
{
    const record_t r[2] = {
        rec(4000000000u, INT16_MIN, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF),
        rec(4000000000u, INT16_MIN, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF),
    };
    char buf[PUB_TELEMETRY_BUF_LEN];
    TEST_ASSERT_GREATER_THAN(0, pub_encode_telemetry(buf, sizeof(buf), 4000000000u, r, 2));
}

static void test_backlog_header_layout(void)
{
    record_t r[3];
    for (int i = 0; i < 3; i++) {
        r[i] = rec(1000 + i, 2000, 5000, 10000, 1, 2, 3, 0);
    }
    uint8_t buf[PUB_BACKLOG_BUF_LEN];
    size_t n = pub_encode_backlog(buf, sizeof(buf), 0x01020304, r, 3);
    TEST_ASSERT_EQUAL_size_t(16 + 3 * 32, n);
    const uint8_t hdr[16] = {'P', 'R', 'N', '1', 1, 32, 3, 0, 0x04, 0x03, 0x02, 0x01, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(hdr, buf, 16);
    TEST_ASSERT_EQUAL_MEMORY(r, &buf[16], 3 * sizeof(record_t));
    /* ts of the first record, little-endian on the wire */
    TEST_ASSERT_EQUAL_HEX8(0xE8, buf[16]);
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[17]);
}

static void test_backlog_limits(void)
{
    static record_t r[31];
    uint8_t buf[PUB_BACKLOG_BUF_LEN];
    TEST_ASSERT_EQUAL_size_t(976, PUB_BACKLOG_BUF_LEN);
    TEST_ASSERT_EQUAL_size_t(976, pub_encode_backlog(buf, sizeof(buf), 0, r, 30));
    TEST_ASSERT_EQUAL_size_t(0, pub_encode_backlog(buf, sizeof(buf), 0, r, 31));
    TEST_ASSERT_EQUAL_size_t(0, pub_encode_backlog(buf, sizeof(buf), 0, r, 0));
    TEST_ASSERT_EQUAL_size_t(0, pub_encode_backlog(buf, 16 + 32 * 2 - 1, 0, r, 2));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_telemetry_exact_output);
    RUN_TEST(test_telemetry_invalid_and_negative_values);
    RUN_TEST(test_telemetry_truncation);
    RUN_TEST(test_telemetry_worst_case_fits_buffer);
    RUN_TEST(test_backlog_header_layout);
    RUN_TEST(test_backlog_limits);
    return UNITY_END();
}
