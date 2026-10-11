#pragma once
#include <stdbool.h>
#include "app_types.h"

/* Fills every measurement field with its invalid marker; flags and ts are zeroed. */
void record_init_invalid(record_t *r);
void record_seal(record_t *r);
bool record_crc_ok(const record_t *r);
/* Padding slots written by storage_flush() are all 0xFF. */
bool record_is_padding(const record_t *r);
void record_make_padding(record_t *r);
