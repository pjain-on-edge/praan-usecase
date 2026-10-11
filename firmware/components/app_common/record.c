#include <string.h>
#include "record.h"
#include "crc16.h"

void record_init_invalid(record_t *r)
{
    memset(r, 0, sizeof(*r));
    r->t_cdeg = REC_I16_INVALID;
    r->rh_cpct = REC_U16_INVALID;
    r->p_dhpa = REC_U16_INVALID;
    r->bme_t_cdeg = REC_I16_INVALID;
    r->bme_rh_cpct = REC_U16_INVALID;
    r->pm1 = r->pm25 = r->pm10 = REC_U16_INVALID;
    r->n03 = r->n05 = r->n10 = r->n25 = REC_U16_INVALID;
}

void record_seal(record_t *r)
{
    r->crc16 = crc16_ccitt(r, RECORD_CRC_LEN);
}

bool record_crc_ok(const record_t *r)
{
    return r->crc16 == crc16_ccitt(r, RECORD_CRC_LEN);
}

bool record_is_padding(const record_t *r)
{
    const uint8_t *p = (const uint8_t *)r;
    for (size_t i = 0; i < sizeof(*r); i++) {
        if (p[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

void record_make_padding(record_t *r)
{
    memset(r, 0xFF, sizeof(*r));
}
