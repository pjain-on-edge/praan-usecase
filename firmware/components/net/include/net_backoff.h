#pragma once
#include <stdint.h>

#define NET_BACKOFF_MAX_S 60

/* 1, 2, 4 ... 60 s for attempt 0, 1, 2 ..., scaled by a jitter of ±20 % taken
 * from rnd. */
uint32_t net_backoff_ms(uint32_t attempt, uint32_t rnd);
