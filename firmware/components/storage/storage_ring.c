#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "storage_ring.h"
#include "record.h"
#include "crc16.h"

int ring_dir_path(char *buf, size_t len, const char *base, uint32_t dir)
{
    int n = snprintf(buf, len, "%s/D%03" PRIu32, base, dir);
    return (n < 0 || (size_t)n >= len) ? -1 : n;
}

int ring_seg_path(char *buf, size_t len, const char *base, uint32_t seg)
{
    int n = snprintf(buf, len, "%s/D%03" PRIu32 "/S%06" PRIu32 ".BIN", base, ring_dir_of(seg), seg);
    return (n < 0 || (size_t)n >= len) ? -1 : n;
}

static bool parse_digits(const char *s, size_t n, uint32_t *out)
{
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        v = v * 10 + (uint32_t)(s[i] - '0');
    }
    *out = v;
    return true;
}

bool ring_parse_dir_name(const char *name, uint32_t *dir)
{
    size_t len = strlen(name);
    return name[0] == 'D' && len >= 4 && len <= 8 && parse_digits(name + 1, len - 1, dir);
}

bool ring_parse_seg_name(const char *name, uint32_t *seg)
{
    return strlen(name) == 11 && name[0] == 'S' && strcmp(name + 7, ".BIN") == 0 &&
           parse_digits(name + 1, 6, seg);
}

size_t ring_block_keep(const record_t *blk, size_t n)
{
    while (n > 0 && !record_is_padding(&blk[n - 1]) && !record_crc_ok(&blk[n - 1])) {
        n--;
    }
    return n;
}

uint32_t ring_clamp_read_idx(uint32_t read_idx, uint32_t oldest_seq, uint32_t *lost)
{
    if (read_idx >= oldest_seq) {
        *lost = 0;
        return read_idx;
    }
    *lost = oldest_seq - read_idx;
    return oldest_seq;
}

void ring_meta_seal(ring_meta_t *m)
{
    m->crc16 = crc16_ccitt(m, offsetof(ring_meta_t, crc16));
}

bool ring_meta_valid(const ring_meta_t *m)
{
    return m->magic == RING_META_MAGIC && m->crc16 == crc16_ccitt(m, offsetof(ring_meta_t, crc16));
}

int ring_meta_pick(const ring_meta_t slots[2], const bool loaded[2])
{
    bool ok0 = loaded[0] && ring_meta_valid(&slots[0]);
    bool ok1 = loaded[1] && ring_meta_valid(&slots[1]);
    if (ok0 && ok1) {
        /* Wrap-safe generation compare. */
        return (int32_t)(slots[1].gen - slots[0].gen) > 0 ? 1 : 0;
    }
    return ok0 ? 0 : ok1 ? 1 : -1;
}
