#include "pub_inflight.h"

static pub_inflight_entry_t *at(pub_inflight_t *t, uint8_t i)
{
    return &t->e[(t->head + i) % PUB_INFLIGHT_CAP];
}

void pub_inflight_reset(pub_inflight_t *t)
{
    t->head = 0;
    t->n = 0;
}

uint8_t pub_inflight_count(const pub_inflight_t *t)
{
    return t->n;
}

bool pub_inflight_push(pub_inflight_t *t, int msg_id, uint32_t seq0, uint32_t count, bool acked)
{
    if (t->n == PUB_INFLIGHT_CAP) {
        return false;
    }
    *at(t, t->n++) = (pub_inflight_entry_t){
        .msg_id = msg_id, .seq0 = seq0, .count = count, .acked = acked,
    };
    return true;
}

bool pub_inflight_ack(pub_inflight_t *t, int msg_id)
{
    for (uint8_t i = 0; i < t->n; i++) {
        pub_inflight_entry_t *e = at(t, i);
        if (!e->acked && e->msg_id == msg_id) {
            e->acked = true;
            return true;
        }
    }
    return false;
}

uint32_t pub_inflight_advance(pub_inflight_t *t, uint32_t *read_idx)
{
    uint32_t popped = 0;
    while (t->n > 0 && at(t, 0)->acked) {
        const pub_inflight_entry_t *e = at(t, 0);
        *read_idx = e->seq0 + e->count;
        t->head = (t->head + 1) % PUB_INFLIGHT_CAP;
        t->n--;
        popped++;
    }
    return popped;
}
