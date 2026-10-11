#include <string.h>
#include "unity.h"
#include "storage_ring.h"
#include "record.h"

void setUp(void) {}
void tearDown(void) {}

static void test_seq_to_segment_index_dir(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, ring_seg_of(0));
    TEST_ASSERT_EQUAL_UINT32(0, ring_seg_of(3599));
    TEST_ASSERT_EQUAL_UINT32(3599, ring_index_of(3599));
    TEST_ASSERT_EQUAL_UINT32(1, ring_seg_of(3600));
    TEST_ASSERT_EQUAL_UINT32(0, ring_index_of(3600));
    TEST_ASSERT_EQUAL_UINT32(0, ring_dir_of(499));
    TEST_ASSERT_EQUAL_UINT32(1, ring_dir_of(500));
    for (uint32_t seq = 0; seq < 10u * 1000 * 1000; seq += 12347) {
        TEST_ASSERT_EQUAL_UINT32(seq, ring_seq_of(ring_seg_of(seq), ring_index_of(seq)));
    }
}

static void test_segment_geometry(void)
{
    TEST_ASSERT_EQUAL_UINT32(115200, RING_SEG_BYTES);
    TEST_ASSERT_EQUAL_UINT32(225, RING_SEG_BYTES / 512);
    TEST_ASSERT_EQUAL_UINT32(512, RING_BLOCK_BYTES);
}

static void test_paths_are_8_3(void)
{
    char buf[40];
    TEST_ASSERT_GREATER_THAN(0, ring_seg_path(buf, sizeof(buf), "/sdcard", 123));
    TEST_ASSERT_EQUAL_STRING("/sdcard/D000/S000123.BIN", buf);
    ring_seg_path(buf, sizeof(buf), "/sdcard", 500);
    TEST_ASSERT_EQUAL_STRING("/sdcard/D001/S000500.BIN", buf);
    ring_dir_path(buf, sizeof(buf), "/sdcard", 7);
    TEST_ASSERT_EQUAL_STRING("/sdcard/D007", buf);
    TEST_ASSERT_EQUAL_INT(-1, ring_seg_path(buf, 10, "/sdcard", 1));
}

static void test_parse_names(void)
{
    uint32_t v;
    TEST_ASSERT_TRUE(ring_parse_seg_name("S000123.BIN", &v));
    TEST_ASSERT_EQUAL_UINT32(123, v);
    TEST_ASSERT_FALSE(ring_parse_seg_name("S00012.BIN", &v));
    TEST_ASSERT_FALSE(ring_parse_seg_name("S00012X.BIN", &v));
    TEST_ASSERT_FALSE(ring_parse_seg_name("META0.BIN", &v));
    TEST_ASSERT_TRUE(ring_parse_dir_name("D012", &v));
    TEST_ASSERT_EQUAL_UINT32(12, v);
    TEST_ASSERT_FALSE(ring_parse_dir_name("DATA", &v));
    TEST_ASSERT_FALSE(ring_parse_dir_name("D01", &v));
}

static void test_read_idx_wraps_after_segment_deletion(void)
{
    uint32_t lost;
    /* Unsent data starts in segment 2; segments 0..4 are deleted. */
    uint32_t read_idx = ring_seq_of(2, 1000);
    uint32_t oldest = ring_seq_of(5, 0);
    TEST_ASSERT_EQUAL_UINT32(oldest, ring_clamp_read_idx(read_idx, oldest, &lost));
    TEST_ASSERT_EQUAL_UINT32(2600 + 2 * 3600, lost);

    /* read_idx already past the deleted segments: nothing lost. */
    TEST_ASSERT_EQUAL_UINT32(ring_seq_of(6, 5), ring_clamp_read_idx(ring_seq_of(6, 5), oldest, &lost));
    TEST_ASSERT_EQUAL_UINT32(0, lost);
}

static void fill_valid(record_t *blk, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        record_init_invalid(&blk[i]);
        blk[i].ts = (uint32_t)(1000 + i);
        record_seal(&blk[i]);
    }
}

static void test_recovery_keeps_valid_prefix(void)
{
    record_t blk[RING_BLOCK_RECORDS];
    fill_valid(blk, RING_BLOCK_RECORDS);
    TEST_ASSERT_EQUAL_size_t(16, ring_block_keep(blk, RING_BLOCK_RECORDS));

    /* Torn write: last three records are garbage. */
    memset(&blk[13], 0, 3 * sizeof(record_t));
    TEST_ASSERT_EQUAL_size_t(13, ring_block_keep(blk, RING_BLOCK_RECORDS));

    /* A bad record in the middle is not trailing: it stays and counts as a CRC error on read. */
    fill_valid(blk, RING_BLOCK_RECORDS);
    blk[5].crc16 ^= 1;
    TEST_ASSERT_EQUAL_size_t(16, ring_block_keep(blk, RING_BLOCK_RECORDS));

    memset(blk, 0, sizeof(blk));
    TEST_ASSERT_EQUAL_size_t(0, ring_block_keep(blk, RING_BLOCK_RECORDS));
}

static void test_recovery_keeps_flush_padding(void)
{
    record_t blk[RING_BLOCK_RECORDS];
    fill_valid(blk, 10);
    for (int i = 10; i < 16; i++) {
        record_make_padding(&blk[i]);
    }
    TEST_ASSERT_EQUAL_size_t(16, ring_block_keep(blk, RING_BLOCK_RECORDS));
}

static ring_meta_t meta(uint32_t gen, uint32_t read_idx)
{
    ring_meta_t m = {.magic = RING_META_MAGIC, .gen = gen, .read_idx = read_idx};
    ring_meta_seal(&m);
    return m;
}

static void test_meta_slot_selection(void)
{
    ring_meta_t s[2] = {meta(7, 100), meta(8, 200)};
    bool loaded[2] = {true, true};
    TEST_ASSERT_EQUAL_INT(1, ring_meta_pick(s, loaded));

    s[1].read_idx = 999;    /* torn write of the newer slot */
    TEST_ASSERT_EQUAL_INT(0, ring_meta_pick(s, loaded));

    s[1] = meta(8, 200);
    loaded[1] = false;
    TEST_ASSERT_EQUAL_INT(0, ring_meta_pick(s, loaded));

    loaded[0] = false;
    TEST_ASSERT_EQUAL_INT(-1, ring_meta_pick(s, loaded));

    ring_meta_t wrap[2] = {meta(0xFFFFFFFFu, 1), meta(0, 2)};
    bool both[2] = {true, true};
    TEST_ASSERT_EQUAL_INT(1, ring_meta_pick(wrap, both));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_seq_to_segment_index_dir);
    RUN_TEST(test_segment_geometry);
    RUN_TEST(test_paths_are_8_3);
    RUN_TEST(test_parse_names);
    RUN_TEST(test_read_idx_wraps_after_segment_deletion);
    RUN_TEST(test_recovery_keeps_valid_prefix);
    RUN_TEST(test_recovery_keeps_flush_padding);
    RUN_TEST(test_meta_slot_selection);
    return UNITY_END();
}
