#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include "pub_encode.h"

static bool append(size_t len, size_t *pos, int n)
{
    if (n < 0 || (size_t)n >= len - *pos) {
        return false;
    }
    *pos += (size_t)n;
    return true;
}

int pub_encode_telemetry(char *buf, size_t len, uint32_t seq0, const record_t *recs, size_t n)
{
    if (n == 0 || len == 0) {
        return -1;
    }
    size_t pos = 0;
    uint32_t t0 = recs[0].ts;
    if (!append(len, &pos, snprintf(buf, len, "{\"v\":1,\"s0\":%" PRIu32 ",\"t0\":%" PRIu32 ",\"d\":[",
                                    seq0, t0))) {
        return -1;
    }
    for (size_t i = 0; i < n; i++) {
        const record_t *r = &recs[i];
        if (!append(len, &pos, snprintf(buf + pos, len - pos,
                "%s[%" PRId32 ",%d,%u,%u,%u,%u,%u,%u]", i ? "," : "",
                (int32_t)(r->ts - t0), r->t_cdeg, r->rh_cpct, r->p_dhpa,
                r->pm1, r->pm25, r->pm10, r->flags))) {
            return -1;
        }
    }
    if (!append(len, &pos, snprintf(buf + pos, len - pos, "]}"))) {
        return -1;
    }
    return (int)pos;
}

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    put_le16(p, (uint16_t)v);
    put_le16(p + 2, (uint16_t)(v >> 16));
}

size_t pub_encode_backlog(uint8_t *buf, size_t len, uint32_t seq0, const record_t *recs, uint16_t n)
{
    size_t total = PUB_BACKLOG_HDR_LEN + (size_t)n * sizeof(record_t);
    if (n == 0 || n > PUB_BACKLOG_MAX_RECORDS || total > len) {
        return 0;
    }
    memcpy(buf, "PRN1", 4);
    buf[4] = 1;
    buf[5] = (uint8_t)sizeof(record_t);
    put_le16(&buf[6], n);
    put_le32(&buf[8], seq0);
    put_le32(&buf[12], 0);
    /* record_t is packed and the target is little-endian, so the in-memory
     * layout is already the wire layout. */
    memcpy(&buf[PUB_BACKLOG_HDR_LEN], recs, (size_t)n * sizeof(record_t));
    return total;
}
