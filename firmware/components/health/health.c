#include <stdatomic.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "health.h"

static const char *TAG = "health";

static _Atomic uint32_t s_ctr[HC_COUNT];
static _Atomic int32_t s_gauge[HG_COUNT];
static health_task_hwm_t s_tasks[HEALTH_MAX_TASKS];
static TaskHandle_t s_task_handles[HEALTH_MAX_TASKS];
static size_t s_n_tasks;
static const char *s_reset_reason = "unknown";
static uint32_t s_boot_count;

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "poweron";
    case ESP_RST_EXT:       return "external";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "int_wdt";
    case ESP_RST_TASK_WDT:  return "task_wdt";
    case ESP_RST_WDT:       return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "sdio";
    case ESP_RST_USB:       return "usb";
    case ESP_RST_JTAG:      return "jtag";
    default:                return "unknown";
    }
}

void health_init(uint32_t boot_count)
{
    s_boot_count = boot_count;
    s_reset_reason = reset_reason_str(esp_reset_reason());
    ESP_LOGI(TAG, "boot #%lu, reset reason: %s", (unsigned long)boot_count, s_reset_reason);
}

void health_inc(health_counter_t c)
{
    atomic_fetch_add(&s_ctr[c], 1);
}

void health_add(health_counter_t c, uint32_t n)
{
    atomic_fetch_add(&s_ctr[c], n);
}

void health_set(health_counter_t c, uint32_t v)
{
    atomic_store(&s_ctr[c], v);
}

uint32_t health_get(health_counter_t c)
{
    return atomic_load(&s_ctr[c]);
}

void health_gauge_set(health_gauge_t g, int32_t v)
{
    atomic_store(&s_gauge[g], v);
}

/* Called from app_main only, before the reporting task runs. */
void health_register_task(const char *name, TaskHandle_t task)
{
    if (s_n_tasks < HEALTH_MAX_TASKS && task) {
        s_tasks[s_n_tasks].name = name;
        s_task_handles[s_n_tasks] = task;
        s_n_tasks++;
    }
}

const char *health_reset_reason(void)
{
    return s_reset_reason;
}

uint32_t health_boot_count(void)
{
    return s_boot_count;
}

int health_build_json(char *buf, size_t len, uint32_t unsent)
{
    health_snapshot_t s = {
        .uptime_s = (uint32_t)(esp_timer_get_time() / 1000000),
        .heap_free = (uint32_t)esp_get_free_heap_size(),
        .heap_min = (uint32_t)esp_get_minimum_free_heap_size(),
        .unsent = unsent,
        .n_tasks = s_n_tasks,
    };
    for (int i = 0; i < HC_COUNT; i++) {
        s.ctr[i] = atomic_load(&s_ctr[i]);
    }
    for (int i = 0; i < HG_COUNT; i++) {
        s.gauge[i] = atomic_load(&s_gauge[i]);
    }
    for (size_t i = 0; i < s_n_tasks; i++) {
        s.tasks[i].name = s_tasks[i].name;
        /* ESP-IDF stack units are bytes. */
        s.tasks[i].free_bytes = (uint32_t)uxTaskGetStackHighWaterMark(s_task_handles[i]);
    }
    return health_format_json(buf, len, &s);
}
