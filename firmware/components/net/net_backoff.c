#include "net_backoff.h"

uint32_t net_backoff_ms(uint32_t attempt, uint32_t rnd)
{
    uint32_t base_s = attempt >= 6 ? NET_BACKOFF_MAX_S : 1u << attempt;
    if (base_s > NET_BACKOFF_MAX_S) {
        base_s = NET_BACKOFF_MAX_S;
    }
    uint32_t pct = 80 + rnd % 41;
    return base_s * 10 * pct;
}
