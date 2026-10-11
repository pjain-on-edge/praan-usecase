#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PUB_INFLIGHT_CAP 4

/* One publish awaiting PUBACK. It covers the sequence slots [seq0, seq0 + count),
 * including empty slots skipped while reading, so acking it releases all of them. */
typedef struct {
    int msg_id;
    uint32_t seq0;
    uint32_t count;
    bool acked;
} pub_inflight_entry_t;

/* FIFO in sequence order. */
typedef struct {
    pub_inflight_entry_t e[PUB_INFLIGHT_CAP];
    uint8_t head;
    uint8_t n;
} pub_inflight_t;

void pub_inflight_reset(pub_inflight_t *t);
uint8_t pub_inflight_count(const pub_inflight_t *t);
bool pub_inflight_push(pub_inflight_t *t, int msg_id, uint32_t seq0, uint32_t count, bool acked);
/* Marks the entry acked. False if msg_id is not in flight (status, cmd ack,
 * or a stale PUBACK from before a reconnect). */
bool pub_inflight_ack(pub_inflight_t *t, int msg_id);
/* Pops acked entries from the oldest end only, so read_idx never passes an
 * unacknowledged record. Returns the number popped. */
uint32_t pub_inflight_advance(pub_inflight_t *t, uint32_t *read_idx);
