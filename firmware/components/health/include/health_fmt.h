#pragma once
#include <stddef.h>
#include <stdint.h>

typedef enum {
    HC_SHT30_ERR,
    HC_BME280_ERR,
    HC_DS3231_ERR,
    HC_PMS_ERR,
    HC_PMS_RESETS,
    HC_RECORDS_DROPPED,
    HC_RECORDS_LOST,
    HC_CRC_ERRORS,
    HC_SD_ERRORS,
    HC_MQTT_RECONNECTS,
    HC_COUNT
} health_counter_t;

typedef enum {
    HG_RTC_TEMP_CDEG,
    HG_CYCLE_MS,
    HG_COUNT
} health_gauge_t;

#define HEALTH_MAX_TASKS 6

typedef struct {
    const char *name;
    uint32_t free_bytes;
} health_task_hwm_t;

typedef struct {
    uint32_t uptime_s;
    uint32_t heap_free;
    uint32_t heap_min;
    uint32_t unsent;
    uint32_t ctr[HC_COUNT];
    int32_t gauge[HG_COUNT];
    health_task_hwm_t tasks[HEALTH_MAX_TASKS];
    size_t n_tasks;
} health_snapshot_t;

typedef struct {
    const char *fw;
    const char *reset;
    uint32_t boot;
    uint32_t unsent;
    uint32_t sd_free_mb;
    int rssi;
} health_status_t;

/* Both return the string length, or -1 if buf was too small. */
int health_format_json(char *buf, size_t len, const health_snapshot_t *s);
int health_format_status(char *buf, size_t len, const health_status_t *st);
