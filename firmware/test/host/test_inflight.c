#include "unity.h"
#include "pub_inflight.h"

void setUp(void) {}
void tearDown(void) {}

static pub_inflight_t t;

static void test_out_of_order_acks_advance_only_contiguously(void)
{
    uint32_t read_idx = 0;
    pub_inflight_reset(&t);
    TEST_ASSERT_TRUE(pub_inflight_push(&t, 11, 0, 30, false));
    TEST_ASSERT_TRUE(pub_inflight_push(&t, 12, 30, 30, false));
    TEST_ASSERT_TRUE(pub_inflight_push(&t, 13, 60, 30, false));

    TEST_ASSERT_TRUE(pub_inflight_ack(&t, 13));
    TEST_ASSERT_EQUAL_UINT32(0, pub_inflight_advance(&t, &read_idx));
    TEST_ASSERT_EQUAL_UINT32(0, read_idx);

    TEST_ASSERT_TRUE(pub_inflight_ack(&t, 11));
    TEST_ASSERT_EQUAL_UINT32(1, pub_inflight_advance(&t, &read_idx));
    TEST_ASSERT_EQUAL_UINT32(30, read_idx);

    TEST_ASSERT_TRUE(pub_inflight_ack(&t, 12));
    TEST_ASSERT_EQUAL_UINT32(2, pub_inflight_advance(&t, &read_idx));
    TEST_ASSERT_EQUAL_UINT32(90, read_idx);
    TEST_ASSERT_EQUAL_UINT8(0, pub_inflight_count(&t));
}

static void test_unknown_and_duplicate_acks_ignored(void)
{
    uint32_t read_idx = 0;
    pub_inflight_reset(&t);
    pub_inflight_push(&t, 5, 0, 2, false);
    TEST_ASSERT_FALSE(pub_inflight_ack(&t, 99));
    TEST_ASSERT_TRUE(pub_inflight_ack(&t, 5));
    TEST_ASSERT_FALSE(pub_inflight_ack(&t, 5));
    TEST_ASSERT_EQUAL_UINT32(1, pub_inflight_advance(&t, &read_idx));
    TEST_ASSERT_EQUAL_UINT32(2, read_idx);
}

static void test_capacity(void)
{
    pub_inflight_reset(&t);
    for (int i = 0; i < PUB_INFLIGHT_CAP; i++) {
        TEST_ASSERT_TRUE(pub_inflight_push(&t, 100 + i, (uint32_t)i * 30, 30, false));
    }
    TEST_ASSERT_FALSE(pub_inflight_push(&t, 200, 120, 30, false));
}

static void test_skipped_slots_are_released_in_order(void)
{
    uint32_t read_idx = 100;
    pub_inflight_reset(&t);
    pub_inflight_push(&t, 1, 100, 30, false);
    pub_inflight_push(&t, -1, 130, 16, true);     /* flush padding, nothing to send */
    pub_inflight_push(&t, 2, 146, 30, false);

    TEST_ASSERT_EQUAL_UINT32(0, pub_inflight_advance(&t, &read_idx));
    pub_inflight_ack(&t, 1);
    TEST_ASSERT_EQUAL_UINT32(2, pub_inflight_advance(&t, &read_idx));
    TEST_ASSERT_EQUAL_UINT32(146, read_idx);
}

static void test_wraps_around_ring(void)
{
    uint32_t read_idx = 0;
    pub_inflight_reset(&t);
    for (uint32_t i = 0; i < 20; i++) {
        TEST_ASSERT_TRUE(pub_inflight_push(&t, (int)i + 1, i * 10, 10, false));
        TEST_ASSERT_TRUE(pub_inflight_ack(&t, (int)i + 1));
        TEST_ASSERT_EQUAL_UINT32(1, pub_inflight_advance(&t, &read_idx));
        TEST_ASSERT_EQUAL_UINT32((i + 1) * 10, read_idx);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_out_of_order_acks_advance_only_contiguously);
    RUN_TEST(test_unknown_and_duplicate_acks_ignored);
    RUN_TEST(test_capacity);
    RUN_TEST(test_skipped_slots_are_released_in_order);
    RUN_TEST(test_wraps_around_ring);
    return UNITY_END();
}
