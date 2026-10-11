#pragma once
#include <stdint.h>

#define PMS_FRAME_LEN 32

typedef struct {
    uint16_t pm1, pm25, pm10;       /* atmospheric, µg/m³ */
    uint16_t n03, n05, n10, n25;    /* counts per 0.1 L */
} pms_data_t;

typedef enum {
    PMS_OK = 0,
    PMS_ERR_HEADER,
    PMS_ERR_LENGTH,
    PMS_ERR_CHECKSUM,
    PMS_ERR_IMPLAUSIBLE,
} pms_status_t;

pms_status_t pms_parse(const uint8_t frame[PMS_FRAME_LEN], pms_data_t *out);
