#pragma once
#include <stddef.h>
#include <stdint.h>
#include "app_types.h"

#define PUB_TELEMETRY_BUF_LEN   256
#define PUB_BACKLOG_HDR_LEN     16
#define PUB_BACKLOG_MAX_RECORDS 30
#define PUB_BACKLOG_BUF_LEN     (PUB_BACKLOG_HDR_LEN + PUB_BACKLOG_MAX_RECORDS * sizeof(record_t))

/* {"v":1,"s0":..,"t0":..,"d":[[dt,t_cdeg,rh_cpct,p_dhpa,pm1,pm25,pm10,flags],..]}
 * Records are consecutive from seq0. Returns the length, or -1 if buf is too small. */
int pub_encode_telemetry(char *buf, size_t len, uint32_t seq0, const record_t *recs, size_t n);

/* "PRN1", version 1, record size 32, count, seq0, reserved, then raw records, all
 * little-endian. Returns the length, or 0 if buf is too small or n is out of range. */
size_t pub_encode_backlog(uint8_t *buf, size_t len, uint32_t seq0, const record_t *recs, uint16_t n);
