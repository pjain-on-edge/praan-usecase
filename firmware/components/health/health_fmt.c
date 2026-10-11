#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include "health_fmt.h"

static bool append(size_t len, size_t *pos, int n)
{
    if (n < 0 || (size_t)n >= len - *pos) {
        return false;
    }
    *pos += (size_t)n;
    return true;
}

int health_format_json(char *buf, size_t len, const health_snapshot_t *s)
{
    size_t pos = 0;
    if (!append(len, &pos, snprintf(buf, len,
            "{\"up\":%" PRIu32 ",\"heap\":%" PRIu32 ",\"heap_min\":%" PRIu32 ",\"stack\":{",
            s->uptime_s, s->heap_free, s->heap_min))) {
        return -1;
    }
    for (size_t i = 0; i < s->n_tasks; i++) {
        if (!append(len, &pos, snprintf(buf + pos, len - pos, "%s\"%s\":%" PRIu32,
                i ? "," : "", s->tasks[i].name, s->tasks[i].free_bytes))) {
            return -1;
        }
    }
    const uint32_t *c = s->ctr;
    if (!append(len, &pos, snprintf(buf + pos, len - pos,
            "},\"unsent\":%" PRIu32
            ",\"err\":{\"sht30\":%" PRIu32 ",\"bme280\":%" PRIu32 ",\"ds3231\":%" PRIu32 ",\"pms\":%" PRIu32 "}"
            ",\"pms_resets\":%" PRIu32 ",\"dropped\":%" PRIu32 ",\"lost\":%" PRIu32 ",\"crc_err\":%" PRIu32
            ",\"sd_err\":%" PRIu32 ",\"mqtt_reconn\":%" PRIu32 ",\"rtc_t\":%" PRId32 ",\"cycle_ms\":%" PRId32 "}",
            s->unsent,
            c[HC_SHT30_ERR], c[HC_BME280_ERR], c[HC_DS3231_ERR], c[HC_PMS_ERR],
            c[HC_PMS_RESETS], c[HC_RECORDS_DROPPED], c[HC_RECORDS_LOST], c[HC_CRC_ERRORS],
            c[HC_SD_ERRORS], c[HC_MQTT_RECONNECTS], s->gauge[HG_RTC_TEMP_CDEG], s->gauge[HG_CYCLE_MS]))) {
        return -1;
    }
    return (int)pos;
}

int health_format_status(char *buf, size_t len, const health_status_t *st)
{
    int n = snprintf(buf, len,
            "{\"online\":true,\"fw\":\"%s\",\"reset\":\"%s\",\"boot\":%" PRIu32 ",\"unsent\":%" PRIu32
            ",\"sd_free_mb\":%" PRIu32 ",\"rssi\":%d}",
            st->fw, st->reset, st->boot, st->unsent, st->sd_free_mb, st->rssi);
    return (n < 0 || (size_t)n >= len) ? -1 : n;
}
