#include <sys/time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "driver/i2c_master.h"
#include "sensor_mgr.h"
#include "sht30.h"
#include "bme280.h"
#include "ds3231.h"
#include "pms5003.h"
#include "record.h"
#include "app_events.h"
#include "health.h"

#define SENSOR_STACK_SIZE       3072
#define SENSOR_PRIO             10
#define SENSOR_CORE             1

#define NOTIFY_SQW              (1u << 0)
#define NOTIFY_TIMER            (1u << 1)
#define NOTIFY_RESYNC           (1u << 2)

#define SQW_TIMEOUT_US          (1500 * 1000LL)
#define SQW_RECOVER_TICKS       5
#define REANCHOR_TICKS          600
#define TRIGGER_WAIT_MS         20
#define PMS_TIMEOUT_MS          150
#define CYCLE_WARN_MS           200
#define SENSOR_FAULT_US         (10 * 1000000LL)
#define REPROBE_TICKS           30
#define RTC_TEMP_TICKS          60

enum { SEN_SHT30, SEN_BME280, SEN_PMS, SEN_COUNT };

static const char *TAG = "sensor";

static StaticTask_t s_tcb;
static StackType_t s_stack[SENSOR_STACK_SIZE];
static TaskHandle_t s_task;
static QueueHandle_t s_queue;
static gpio_num_t s_sqw_pin;

static i2c_master_bus_handle_t s_bus;
static uint32_t s_i2c_hz;
static esp_pm_lock_handle_t s_pm_lock;

static esp_timer_handle_t s_fallback_timer;
static bool s_fallback;
static bool s_sqw_seen;
static uint8_t s_sqw_streak;

static bool s_rtc_osf = true;
static bool s_time_valid;
static bool s_anchor_due;
static uint32_t s_ts;
static uint32_t s_ticks_since_anchor;

static int64_t s_last_ok_us[SEN_COUNT];
static uint32_t s_tick_count;

static uint32_t uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

static void log_init(const char *name, esp_err_t err)
{
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s unavailable (%s), sampling continues without it", name, esp_err_to_name(err));
    }
}

static void fallback_timer_cb(void *arg)
{
    (void)arg;
    xTaskNotify(s_task, NOTIFY_TIMER, eSetBits);
}

esp_err_t sensor_mgr_init(const sensor_mgr_config_t *cfg)
{
    s_sqw_pin = cfg->rtc_sqw;
    s_i2c_hz = cfg->i2c_hz;

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = cfg->i2c_sda,
        .scl_io_num = cfg->i2c_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,  /* external 4.7 k */
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        s_bus = NULL;
    }

    if (s_bus) {
        log_init("SHT30", sht30_init(s_bus, s_i2c_hz));
        log_init("BME280", bme280_init(s_bus, s_i2c_hz));

        err = ds3231_init(s_bus, s_i2c_hz, &s_rtc_osf);
        log_init("DS3231", err);
        uint32_t now = 0;
        if (err == ESP_OK && !s_rtc_osf && ds3231_read_time(&now) == ESP_OK) {
            s_ts = now;
            s_time_valid = true;
            const struct timeval tv = {.tv_sec = now};
            settimeofday(&tv, NULL);
            xEventGroupSetBits(app_events(), EVT_TIME_VALID);
            ESP_LOGI(TAG, "time base from RTC: %lu", (unsigned long)now);
        } else if (err == ESP_OK) {
            ESP_LOGW(TAG, "RTC oscillator stopped: timestamps are uptime until SNTP sync");
        }
    }

    log_init("PMS5003", pms_init(&cfg->pms));

#if CONFIG_PM_ENABLE
    err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "sensor", &s_pm_lock);
    if (err != ESP_OK) {
        return err;
    }
#endif

    const esp_timer_create_args_t targs = {
        .callback = fallback_timer_cb,
        .name = "tick_fallback",
    };
    return esp_timer_create(&targs, &s_fallback_timer);
}

/* SNTP has just set the system clock; bring the RTC in line and adopt the time. */
static void apply_time_sync(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint32_t now = (uint32_t)tv.tv_sec;

    if (ds3231_present()) {
        uint32_t rtc = 0;
        bool rtc_ok = ds3231_read_time(&rtc) == ESP_OK;
        int64_t diff = (int64_t)rtc - now;
        if (!rtc_ok || s_rtc_osf || diff > 1 || diff < -1) {
            /* Writing the seconds register restarts the RTC countdown chain, so the
             * SQW phase follows this write; the residual offset is below 1 s. */
            esp_err_t err = ds3231_set_time(now);
            if (err == ESP_OK) {
                s_rtc_osf = false;
                ESP_LOGI(TAG, "RTC set from SNTP (was off by %lld s)", rtc_ok ? diff : 0);
            } else {
                health_inc(HC_DS3231_ERR);
                ESP_LOGW(TAG, "RTC write failed: %s", esp_err_to_name(err));
            }
        }
    }
    s_time_valid = true;
    s_ts = now;
    s_ticks_since_anchor = 0;
    s_anchor_due = false;
}

static void anchor_from_rtc(void)
{
    uint32_t rtc;
    if (ds3231_read_time(&rtc) != ESP_OK) {
        health_inc(HC_DS3231_ERR);
        s_ts++;
        return;
    }
    int64_t diff = (int64_t)rtc - (int64_t)(s_ts + 1);
    if (!s_anchor_due && (diff > 1 || diff < -1)) {
        ESP_LOGW(TAG, "counted time differs from RTC by %lld s, re-anchored", diff);
    }
    s_ts = rtc;
    s_ticks_since_anchor = 0;
    s_anchor_due = false;
}

static uint32_t next_timestamp(uint16_t *flags)
{
    if (s_fallback) {
        *flags |= FLAG_TICK_FALLBACK;
        if (s_time_valid) {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            return (uint32_t)tv.tv_sec;
        }
    }
    if (!s_time_valid) {
        *flags |= FLAG_TIME_INVALID;
        return uptime_s();
    }
    if (s_anchor_due || ++s_ticks_since_anchor >= REANCHOR_TICKS) {
        anchor_from_rtc();
    } else {
        s_ts++;
    }
    return s_ts;
}

static void mark_ok(int sensor)
{
    s_last_ok_us[sensor] = esp_timer_get_time();
}

static void update_fault_bit(void)
{
    int64_t now = esp_timer_get_time();
    bool fault = false;
    for (int i = 0; i < SEN_COUNT; i++) {
        fault |= now - s_last_ok_us[i] > SENSOR_FAULT_US;
    }
    if (fault) {
        xEventGroupSetBits(app_events(), EVT_FAULT_SENSOR);
    } else {
        xEventGroupClearBits(app_events(), EVT_FAULT_SENSOR);
    }
}

static void enqueue(const record_t *r)
{
    if (xQueueSend(s_queue, r, 0) == pdTRUE) {
        return;
    }
    record_t oldest;
    xQueueReceive(s_queue, &oldest, 0);
    xQueueSend(s_queue, r, 0);
    health_inc(HC_RECORDS_DROPPED);
}

static void housekeeping(void)
{
    if (s_bus && s_tick_count % REPROBE_TICKS == 0) {
        if (!sht30_present() && sht30_init(s_bus, s_i2c_hz) == ESP_OK) {
            ESP_LOGI(TAG, "SHT30 appeared");
        }
        if (!bme280_present() && bme280_init(s_bus, s_i2c_hz) == ESP_OK) {
            ESP_LOGI(TAG, "BME280 appeared");
        }
    }
    if (s_tick_count % RTC_TEMP_TICKS == 1) {
        int16_t t;
        if (ds3231_read_temp(&t) == ESP_OK) {
            health_gauge_set(HG_RTC_TEMP_CDEG, t);
        }
    }
    health_set(HC_PMS_RESETS, pms_reset_count());
}

static void sample_once(void)
{
    int64_t t0 = esp_timer_get_time();
    record_t r;
    record_init_invalid(&r);
    uint16_t flags = 0;

    if (s_pm_lock) {
        esp_pm_lock_acquire(s_pm_lock);
    }

    bool sht_started = sht30_trigger() == ESP_OK;
    bool bme_started = bme280_trigger() == ESP_OK;
    bool pms_started = pms_request() == ESP_OK;

    /* +1 tick: the wait starts at an arbitrary point inside the current tick, and
     * SHT30 needs the full 15.5 ms before it ACKs a read. */
    vTaskDelay(pdMS_TO_TICKS(TRIGGER_WAIT_MS) + 1);

    int16_t sht_t;
    uint16_t sht_rh;
    bool sht_ok = sht_started && sht30_read(&sht_t, &sht_rh) == ESP_OK;
    if (sht_ok) {
        mark_ok(SEN_SHT30);
    } else if (sht30_present()) {
        health_inc(HC_SHT30_ERR);
    }

    bme280_reading_t bme;
    bool bme_ok = bme_started && bme280_read(&bme) == ESP_OK;
    if (bme_ok) {
        mark_ok(SEN_BME280);
        r.p_dhpa = bme.p_dhpa;
        r.bme_t_cdeg = bme.t_cdeg;
        r.bme_rh_cpct = bme.rh_cpct;
    } else {
        flags |= FLAG_PRESS_INVALID | FLAG_BME_DIAG_INVALID;
        if (bme280_present()) {
            health_inc(HC_BME280_ERR);
        }
    }

    if (sht_ok) {
        r.t_cdeg = sht_t;
        r.rh_cpct = sht_rh;
    } else if (bme_ok) {
        r.t_cdeg = bme.t_cdeg;
        r.rh_cpct = bme.rh_cpct;
        flags |= FLAG_TRH_FALLBACK;
    } else {
        flags |= FLAG_TRH_INVALID;
    }

    pms_data_t pm;
    if (pms_started && pms_read(&pm, PMS_TIMEOUT_MS) == ESP_OK) {
        mark_ok(SEN_PMS);
        r.pm1 = pm.pm1;
        r.pm25 = pm.pm25;
        r.pm10 = pm.pm10;
        r.n03 = pm.n03;
        r.n05 = pm.n05;
        r.n10 = pm.n10;
        r.n25 = pm.n25;
    } else {
        flags |= FLAG_PM_INVALID;
        if (pms_started) {
            health_inc(HC_PMS_ERR);
        }
    }
    if (pms_warming_up()) {
        flags |= FLAG_PM_WARMUP;
    }

    if (s_pm_lock) {
        esp_pm_lock_release(s_pm_lock);
    }

    r.ts = next_timestamp(&flags);
    r.flags = flags;
    record_seal(&r);
    enqueue(&r);

    update_fault_bit();
    s_tick_count++;
    housekeeping();

    int32_t cycle_ms = (int32_t)((esp_timer_get_time() - t0) / 1000);
    health_gauge_set(HG_CYCLE_MS, cycle_ms);
    if (cycle_ms > CYCLE_WARN_MS) {
        ESP_LOGW(TAG, "slow cycle: %ld ms", (long)cycle_ms);
    }
    if (s_tick_count == 1) {
        ESP_LOGI(TAG, "first sample %lld ms after boot", t0 / 1000);
    }
}

static void enter_fallback(void)
{
    ESP_LOGW(TAG, "no SQW edge for 1.5 s, switching to esp_timer timebase");
    s_fallback = true;
    s_sqw_seen = false;
    s_sqw_streak = 0;
    esp_timer_start_periodic(s_fallback_timer, 1000 * 1000);
}

static void leave_fallback(void)
{
    ESP_LOGI(TAG, "SQW back for %d ticks, leaving esp_timer timebase", SQW_RECOVER_TICKS);
    esp_timer_stop(s_fallback_timer);
    s_fallback = false;
    s_anchor_due = true;
}

static void sensor_task(void *arg)
{
    (void)arg;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    int64_t last_tick_us = esp_timer_get_time();

    for (;;) {
        int64_t since_tick = esp_timer_get_time() - last_tick_us;
        TickType_t wait = since_tick >= SQW_TIMEOUT_US ? 0 :
                          pdMS_TO_TICKS((SQW_TIMEOUT_US - since_tick) / 1000) + 1;
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, wait);

        if (bits & NOTIFY_RESYNC) {
            apply_time_sync();
        }

        bool tick = false;
        if (!s_fallback) {
            if (bits & NOTIFY_SQW) {
                tick = true;
            } else if (esp_timer_get_time() - last_tick_us >= SQW_TIMEOUT_US) {
                enter_fallback();
                tick = true;
            }
        } else {
            s_sqw_seen |= (bits & NOTIFY_SQW) != 0;
            if (bits & NOTIFY_TIMER) {
                s_sqw_streak = s_sqw_seen ? s_sqw_streak + 1 : 0;
                s_sqw_seen = false;
                if (s_sqw_streak >= SQW_RECOVER_TICKS) {
                    /* Skip this timer tick; the next SQW edge samples, so no second
                     * is recorded twice. */
                    leave_fallback();
                    last_tick_us = esp_timer_get_time();
                } else {
                    tick = true;
                }
            }
        }

        if (tick) {
            last_tick_us = esp_timer_get_time();
            sample_once();
        }
        esp_task_wdt_reset();
    }
}

esp_err_t sensor_mgr_start(QueueHandle_t out_queue)
{
    s_queue = out_queue;
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < SEN_COUNT; i++) {
        s_last_ok_us[i] = now;
    }

    s_task = xTaskCreateStaticPinnedToCore(sensor_task, "sensor", SENSOR_STACK_SIZE, NULL,
                                           SENSOR_PRIO, s_stack, &s_tcb, SENSOR_CORE);

    esp_err_t err = ds3231_sqw_attach(s_sqw_pin, s_task, NOTIFY_SQW);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SQW attach failed (%s); esp_timer fallback will take over", esp_err_to_name(err));
    }
#if CONFIG_APP_LIGHT_SLEEP
    /* GPIO edge interrupts do not wake light sleep; a low level does. SQW is low
     * for half of each second, so the chip sleeps at most during the high half. */
    gpio_wakeup_enable(s_sqw_pin, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
#endif
    return ESP_OK;
}

TaskHandle_t sensor_mgr_task(void)
{
    return s_task;
}

void sensor_mgr_time_synced(void)
{
    if (s_task) {
        xTaskNotify(s_task, NOTIFY_RESYNC, eSetBits);
    }
}
