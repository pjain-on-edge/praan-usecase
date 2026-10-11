#pragma once
#include <stdint.h>

/* Records are written to SD and to the wire in native byte order. */
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "record_t layout assumes a little-endian target");

typedef struct __attribute__((packed)) {
    uint32_t ts;            /* UTC epoch s; uptime s if FLAG_TIME_INVALID */
    uint16_t flags;
    int16_t  t_cdeg;        /* SHT30 (or BME280 fallback), 0.01 °C */
    uint16_t rh_cpct;       /* 0.01 %RH */
    uint16_t p_dhpa;        /* BME280, 0.1 hPa */
    int16_t  bme_t_cdeg;    /* diagnostics */
    uint16_t bme_rh_cpct;   /* diagnostics */
    uint16_t pm1, pm25, pm10;       /* atmospheric, µg/m³ */
    uint16_t n03, n05, n10, n25;    /* counts per 0.1 L */
    uint16_t crc16;         /* CRC-16/CCITT-FALSE over bytes 0..29 */
} record_t;
_Static_assert(sizeof(record_t) == 32, "record_t must be 32 bytes");

#define RECORD_CRC_LEN      30
#define REC_I16_INVALID     INT16_MIN
#define REC_U16_INVALID     0xFFFFu

#define FLAG_TRH_FALLBACK       (1u << 0)
#define FLAG_TRH_INVALID        (1u << 1)
#define FLAG_PRESS_INVALID      (1u << 2)
#define FLAG_PM_INVALID         (1u << 3)
#define FLAG_PM_WARMUP          (1u << 4)
#define FLAG_TIME_INVALID       (1u << 5)
#define FLAG_TICK_FALLBACK      (1u << 6)
#define FLAG_SD_FALLBACK        (1u << 7)
#define FLAG_BME_DIAG_INVALID   (1u << 8)

#define EVT_WIFI_UP         (1u << 0)
#define EVT_MQTT_UP         (1u << 1)
#define EVT_TIME_VALID      (1u << 2)
#define EVT_SD_OK           (1u << 3)
#define EVT_PROV_ACTIVE     (1u << 4)
#define EVT_OTA_ACTIVE      (1u << 5)
#define EVT_FAULT_SENSOR    (1u << 6)
#define EVT_FAULT_SD        (1u << 7)
