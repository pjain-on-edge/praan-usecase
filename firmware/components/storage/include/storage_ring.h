#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "app_types.h"

#define RING_SEG_RECORDS    3600u
#define RING_BLOCK_RECORDS  16u
#define RING_BLOCK_BYTES    (RING_BLOCK_RECORDS * sizeof(record_t))
#define RING_SEGS_PER_DIR   500u
#define RING_SEG_BYTES      (RING_SEG_RECORDS * sizeof(record_t))

_Static_assert(RING_BLOCK_BYTES == 512, "one block must be one SD sector");
_Static_assert(RING_SEG_RECORDS % RING_BLOCK_RECORDS == 0, "blocks must not straddle segments");

static inline uint32_t ring_seg_of(uint32_t seq)   { return seq / RING_SEG_RECORDS; }
static inline uint32_t ring_index_of(uint32_t seq) { return seq % RING_SEG_RECORDS; }
static inline uint32_t ring_dir_of(uint32_t seg)   { return seg / RING_SEGS_PER_DIR; }
static inline uint32_t ring_seq_of(uint32_t seg, uint32_t index) { return seg * RING_SEG_RECORDS + index; }

/* "<base>/D000" and "<base>/D000/S000123.BIN". Return snprintf length, or -1 on truncation. */
int ring_dir_path(char *buf, size_t len, const char *base, uint32_t dir);
int ring_seg_path(char *buf, size_t len, const char *base, uint32_t seg);
bool ring_parse_dir_name(const char *name, uint32_t *dir);
bool ring_parse_seg_name(const char *name, uint32_t *seg);

/* Number of leading records of the last block worth keeping after a power cut:
 * trailing records that are neither valid nor padding are dropped. */
size_t ring_block_keep(const record_t *blk, size_t n);

/* After old segments are deleted, moves read_idx up to oldest_seq and reports
 * how many unacknowledged slots were lost. */
uint32_t ring_clamp_read_idx(uint32_t read_idx, uint32_t oldest_seq, uint32_t *lost);

#define RING_META_MAGIC 0x4D4E5250u   /* "PRNM" */

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t gen;
    uint32_t read_idx;
    uint16_t crc16;
} ring_meta_t;

void ring_meta_seal(ring_meta_t *m);
bool ring_meta_valid(const ring_meta_t *m);
/* Index (0/1) of the valid slot with the newest generation, or -1. */
int ring_meta_pick(const ring_meta_t slots[2], const bool loaded[2]);
