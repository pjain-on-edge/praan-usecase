#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "mqtt_client.h"
#include "freertos/queue.h"
#include "publisher.h"
#include "pub_encode.h"
#include "pub_inflight.h"
#include "pub_cmd.h"
#include "storage.h"
#include "health.h"
#include "net.h"
#include "ota.h"
#include "status_led.h"
#include "app_events.h"
#include "app_ids.h"

_Static_assert(CONFIG_APP_INFLIGHT_MAX <= PUB_INFLIGHT_CAP, "in-flight table too small");
_Static_assert(CONFIG_APP_BACKLOG_BATCH <= PUB_BACKLOG_MAX_RECORDS, "backlog batch too large");

#define PUB_STACK_SIZE          5120
#define PUB_PRIO                6
#define PUB_CORE                0

#define N_CONNECTED             (1u << 0)
#define N_DISCONNECTED          (1u << 1)
#define N_ACK                   (1u << 2)
#define N_CMD                   (1u << 3)

#define ACK_QUEUE_LEN           8
#define LIVE_MAX_RECORDS        2
#define PERSIST_EVERY_ACKS      15
#define HEALTH_PERIOD_US        (60 * 1000000LL)
#define OTA_CONFIRM_UPTIME_US   (60 * 1000000LL)
#define INFLIGHT_TIMEOUT_US     (30 * 1000000LL)
#define REBOOT_ACK_GRACE_MS     500
#define MQTT_KEEPALIVE_S        60
#define MQTT_OUTBOX_LIMIT       (8 * 1024)
/* Below the 5 s task watchdog: publish() blocks on the socket for at most this. */
#define MQTT_NET_TIMEOUT_MS     3000
#define CMD_MAX_LEN             512
#define TOPIC_LEN               80
#define STATUS_BUF_LEN          192
#define HEALTH_BUF_LEN          512
#define ACK_BUF_LEN             128

static const char *TAG = "pub";
static const char LWT_MSG[] = "{\"online\":false}";

static StaticTask_t s_tcb;
static StackType_t s_stack[PUB_STACK_SIZE];
static TaskHandle_t s_task;

static StaticQueue_t s_ack_q_buf;
static uint8_t s_ack_q_storage[ACK_QUEUE_LEN * sizeof(uint16_t)];
static QueueHandle_t s_ack_q;

static char s_topic_telemetry[TOPIC_LEN];
static char s_topic_backlog[TOPIC_LEN];
static char s_topic_status[TOPIC_LEN];
static char s_topic_health[TOPIC_LEN];
static char s_topic_cmd[TOPIC_LEN];
static char s_topic_cmd_ack[TOPIC_LEN];

static esp_mqtt_client_handle_t s_client;
static bool s_client_started;
static bool s_mqtt_up;
static bool s_ever_connected;

/* Written by the MQTT task while s_cmd_pending is false, read by publish_task
 * while it is true. */
static char s_cmd_buf[CMD_MAX_LEN];
static size_t s_cmd_len;
static atomic_bool s_cmd_pending;

static record_t s_batch[PUB_BACKLOG_MAX_RECORDS];
static uint8_t s_backlog_buf[PUB_BACKLOG_BUF_LEN];
static char s_json_buf[PUB_TELEMETRY_BUF_LEN];
static char s_text_buf[HEALTH_BUF_LEN];

static pub_inflight_t s_inflight;
static uint32_t s_read_idx;
static uint32_t s_send_idx;
static uint32_t s_acks_since_persist;
static int64_t s_inflight_since_us;
static bool s_draining;
static int64_t s_drain_start_us;
static uint32_t s_drain_records;
static bool s_read_blocked_logged;
static bool s_ota_confirmed;

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    const esp_mqtt_event_t *ev = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        xEventGroupSetBits(app_events(), EVT_MQTT_UP);
        xTaskNotify(s_task, N_CONNECTED, eSetBits);
        break;
    case MQTT_EVENT_DISCONNECTED:
        xEventGroupClearBits(app_events(), EVT_MQTT_UP);
        xTaskNotify(s_task, N_DISCONNECTED, eSetBits);
        break;
    case MQTT_EVENT_PUBLISHED: {
        uint16_t msg_id = (uint16_t)ev->msg_id;
        if (xQueueSend(s_ack_q, &msg_id, 0) != pdTRUE) {
            ESP_LOGW(TAG, "ack queue full, PUBACK %u dropped", msg_id);
        }
        xTaskNotify(s_task, N_ACK, eSetBits);
        break;
    }
    case MQTT_EVENT_DATA: {
        bool is_cmd = (size_t)ev->topic_len == strlen(s_topic_cmd) &&
                      memcmp(ev->topic, s_topic_cmd, (size_t)ev->topic_len) == 0;
        if (!is_cmd) {
            break;
        }
        if (ev->total_data_len != ev->data_len || ev->data_len > CMD_MAX_LEN || atomic_load(&s_cmd_pending)) {
            ESP_LOGW(TAG, "command dropped (len %d, busy %d)", ev->total_data_len, atomic_load(&s_cmd_pending));
            break;
        }
        memcpy(s_cmd_buf, ev->data, (size_t)ev->data_len);
        s_cmd_len = (size_t)ev->data_len;
        atomic_store(&s_cmd_pending, true);
        xTaskNotify(s_task, N_CMD, eSetBits);
        break;
    }
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT error type %d", ev->error_handle ? (int)ev->error_handle->error_type : -1);
        break;
    default:
        break;
    }
}

static void make_topic(char *buf, const char *prefix, const char *id, const char *leaf)
{
    int n = snprintf(buf, TOPIC_LEN, "%s/%s/%s", prefix, id, leaf);
    if (n < 0 || n >= TOPIC_LEN) {
        ESP_LOGE(TAG, "topic too long: %s/%s/%s", prefix, id, leaf);
        abort();
    }
}

esp_err_t pub_init(void)
{
    const char *id = app_device_id();
    make_topic(s_topic_telemetry, CONFIG_APP_TOPIC_PREFIX, id, "telemetry");
    make_topic(s_topic_backlog, CONFIG_APP_TOPIC_PREFIX, id, "backlog");
    make_topic(s_topic_status, CONFIG_APP_TOPIC_PREFIX, id, "status");
    make_topic(s_topic_health, CONFIG_APP_TOPIC_PREFIX, id, "health");
    make_topic(s_topic_cmd, CONFIG_APP_TOPIC_PREFIX, id, "cmd");
    make_topic(s_topic_cmd_ack, CONFIG_APP_TOPIC_PREFIX, id, "cmd/ack");

    s_ack_q = xQueueCreateStatic(ACK_QUEUE_LEN, sizeof(uint16_t), s_ack_q_storage, &s_ack_q_buf);

    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_APP_MQTT_BROKER_URI,
        /* Only consulted for mqtts://. */
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.client_id = id,
        .credentials.username = CONFIG_APP_MQTT_USERNAME[0] ? CONFIG_APP_MQTT_USERNAME : NULL,
        .credentials.authentication.password = CONFIG_APP_MQTT_PASSWORD[0] ? CONFIG_APP_MQTT_PASSWORD : NULL,
        .session.keepalive = MQTT_KEEPALIVE_S,
        .session.disable_clean_session = false,
        .session.last_will = {
            .topic = s_topic_status,
            .msg = LWT_MSG,
            .msg_len = sizeof(LWT_MSG) - 1,
            .qos = 1,
            .retain = 1,
        },
        .outbox.limit = MQTT_OUTBOX_LIMIT,
        .network.timeout_ms = MQTT_NET_TIMEOUT_MS,
    };
    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        return ESP_ERR_NO_MEM;
    }
    return esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
}

static void reset_pipeline(void)
{
    s_send_idx = s_read_idx;
    pub_inflight_reset(&s_inflight);
}

static void publish_status(void)
{
    health_status_t st = {
        .fw = esp_app_get_description()->version,
        .reset = health_reset_reason(),
        .boot = health_boot_count(),
        .unsent = storage_write_idx() - s_read_idx,
        .sd_free_mb = storage_free_mb(),
        .rssi = net_rssi(),
    };
    int len = health_format_status(s_text_buf, sizeof(s_text_buf), &st);
    if (len > 0) {
        esp_mqtt_client_publish(s_client, s_topic_status, s_text_buf, len, 1, 1);
    }
}

static void publish_health(void)
{
    int len = health_build_json(s_text_buf, sizeof(s_text_buf), storage_write_idx() - s_read_idx);
    if (len > 0) {
        esp_mqtt_client_publish(s_client, s_topic_health, s_text_buf, len, 0, 0);
    } else {
        ESP_LOGW(TAG, "health JSON truncated");
    }
}

static void on_connect(void)
{
    if (s_ever_connected) {
        health_inc(HC_MQTT_RECONNECTS);
    }
    s_ever_connected = true;
    s_mqtt_up = true;
    reset_pipeline();
    esp_mqtt_client_subscribe(s_client, s_topic_cmd, 1);
    publish_status();
    ESP_LOGI(TAG, "connected, resending from %lu", (unsigned long)s_read_idx);
}

static void on_disconnect(void)
{
    s_mqtt_up = false;
    reset_pipeline();
}

static void process_acks(void)
{
    uint16_t msg_id;
    while (xQueueReceive(s_ack_q, &msg_id, 0) == pdTRUE) {
        if (pub_inflight_ack(&s_inflight, msg_id)) {
            led_blip();
        }
    }
    uint32_t popped = pub_inflight_advance(&s_inflight, &s_read_idx);
    if (popped == 0) {
        return;
    }
    s_inflight_since_us = esp_timer_get_time();
    storage_advance_read_idx(s_read_idx);
    s_acks_since_persist += popped;
    if (s_acks_since_persist >= PERSIST_EVERY_ACKS) {
        s_acks_since_persist = 0;
        if (storage_persist_read_idx() != ESP_OK) {
            ESP_LOGW(TAG, "read_idx not persisted");
        }
    }
}

static void track(int msg_id, uint32_t seq0, uint32_t count, bool acked)
{
    if (pub_inflight_count(&s_inflight) == 0) {
        s_inflight_since_us = esp_timer_get_time();
    }
    pub_inflight_push(&s_inflight, msg_id, seq0, count, acked);
}

/* Reads the next run of records at s_send_idx. Returns 0 when nothing can be
 * sent now; empty slots skipped on the way are tracked as already acked. */
static uint16_t read_next(uint16_t max)
{
    uint32_t seq = s_send_idx;
    uint16_t n = 0;
    esp_err_t err = storage_read(&seq, max, s_batch, &n);
    if (err != ESP_OK) {
        if (!s_read_blocked_logged) {
            ESP_LOGW(TAG, "backlog at %lu waits for the SD card", (unsigned long)s_send_idx);
            s_read_blocked_logged = true;
        }
        return 0;
    }
    s_read_blocked_logged = false;
    if (seq != s_send_idx) {
        track(-1, s_send_idx, seq - s_send_idx, true);
        s_send_idx = seq;
        if (pub_inflight_count(&s_inflight) >= CONFIG_APP_INFLIGHT_MAX) {
            return 0;
        }
    }
    return n;
}

static bool send_backlog_batch(void)
{
    uint16_t n = read_next(CONFIG_APP_BACKLOG_BATCH);
    if (n == 0) {
        return false;
    }
    size_t len = pub_encode_backlog(s_backlog_buf, sizeof(s_backlog_buf), s_send_idx, s_batch, n);
    int msg_id = esp_mqtt_client_publish(s_client, s_topic_backlog, (const char *)s_backlog_buf, (int)len, 1, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "backlog publish failed (%d)", msg_id);
        return false;
    }
    track(msg_id, s_send_idx, n, false);
    s_send_idx += n;
    return true;
}

static void send_live(void)
{
    uint16_t n = read_next(LIVE_MAX_RECORDS);
    if (n == 0) {
        return;
    }
    int len = pub_encode_telemetry(s_json_buf, sizeof(s_json_buf), s_send_idx, s_batch, n);
    if (len < 0) {
        ESP_LOGE(TAG, "telemetry JSON truncated");
        return;
    }
    int msg_id = esp_mqtt_client_publish(s_client, s_topic_telemetry, s_json_buf, len, 1, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "telemetry publish failed (%d)", msg_id);
        return;
    }
    track(msg_id, s_send_idx, n, false);
    s_send_idx += n;
}

static void pump(bool tick)
{
    uint32_t write_idx = storage_write_idx();
    uint32_t unsent = write_idx - s_send_idx;

    if (unsent > LIVE_MAX_RECORDS) {
        if (!s_draining) {
            s_draining = true;
            s_drain_start_us = esp_timer_get_time();
            s_drain_records = unsent;
            ESP_LOGI(TAG, "backlog of %lu records", (unsigned long)unsent);
        }
        while (pub_inflight_count(&s_inflight) < CONFIG_APP_INFLIGHT_MAX &&
               storage_write_idx() - s_send_idx > LIVE_MAX_RECORDS && send_backlog_batch()) {
        }
    } else if (unsent > 0 && tick && pub_inflight_count(&s_inflight) < CONFIG_APP_INFLIGHT_MAX) {
        send_live();
    }

    if (s_draining && storage_write_idx() == s_send_idx) {
        s_draining = false;
        ESP_LOGI(TAG, "backlog of %lu records drained in %lld ms", (unsigned long)s_drain_records,
                 (esp_timer_get_time() - s_drain_start_us) / 1000);
    }
}

static void handle_command(void)
{
    pub_cmd_result_t res;
    pub_cmd_execute(s_cmd_buf, s_cmd_len, &res);
    atomic_store(&s_cmd_pending, false);

    char ack[ACK_BUF_LEN];
    int len = snprintf(ack, sizeof(ack), "{\"id\":\"%s\",\"ok\":%s,\"err\":\"%s\"}",
                       res.id, res.ok ? "true" : "false", res.err);
    if (len > 0 && len < (int)sizeof(ack) && s_mqtt_up) {
        esp_mqtt_client_publish(s_client, s_topic_cmd_ack, ack, len, 1, 0);
    }
    if (res.reboot) {
        vTaskDelay(pdMS_TO_TICKS(REBOOT_ACK_GRACE_MS));
        storage_prepare_reboot();
        esp_restart();
    }
}

static void maybe_start_client(void)
{
    if (s_client_started) {
        return;
    }
    EventBits_t bits = xEventGroupGetBits(app_events());
    if ((bits & (EVT_WIFI_UP | EVT_TIME_VALID)) == (EVT_WIFI_UP | EVT_TIME_VALID)) {
        esp_err_t err = esp_mqtt_client_start(s_client);
        s_client_started = err == ESP_OK;
        ESP_LOGI(TAG, "MQTT client start: %s", esp_err_to_name(err));
    }
}

static void check_inflight_timeout(int64_t now)
{
    if (pub_inflight_count(&s_inflight) > 0 && now - s_inflight_since_us > INFLIGHT_TIMEOUT_US) {
        /* A PUBACK was lost (e.g. ack queue overflow). Resend from read_idx;
         * the backend deduplicates. */
        ESP_LOGW(TAG, "no PUBACK progress for %lld s, resending from %lu",
                 INFLIGHT_TIMEOUT_US / 1000000, (unsigned long)s_read_idx);
        reset_pipeline();
    }
}

static void publish_task(void *arg)
{
    (void)arg;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    const int64_t interval_us = (int64_t)CONFIG_APP_PUBLISH_INTERVAL_MS * 1000;
    int64_t next_tick = esp_timer_get_time() + interval_us;
    int64_t next_health = esp_timer_get_time() + HEALTH_PERIOD_US;
    s_read_idx = storage_read_idx();
    reset_pipeline();

    for (;;) {
        int64_t now = esp_timer_get_time();
        TickType_t wait = next_tick > now ? pdMS_TO_TICKS((next_tick - now) / 1000) + 1 : 0;
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, wait);
        esp_task_wdt_reset();

        if (bits & N_DISCONNECTED) {
            on_disconnect();
        }
        if (bits & N_CONNECTED) {
            on_connect();
        }
        process_acks();
        if (bits & N_CMD) {
            handle_command();
        }

        /* storage may have moved read_idx past segments it had to delete. */
        uint32_t stored = storage_read_idx();
        if ((int32_t)(stored - s_read_idx) > 0) {
            s_read_idx = stored;
        }
        if ((int32_t)(s_read_idx - s_send_idx) > 0) {
            reset_pipeline();
        }

        now = esp_timer_get_time();
        bool tick = now >= next_tick;
        if (tick) {
            next_tick = now - next_tick > interval_us ? now + interval_us : next_tick + interval_us;
        }

        maybe_start_client();
        if (!s_mqtt_up) {
            continue;
        }
        if (now >= next_health) {
            next_health = now + HEALTH_PERIOD_US;
            publish_health();
        }
        if (!s_ota_confirmed && now >= OTA_CONFIRM_UPTIME_US) {
            /* Reaching here means MQTT connected and 60 s passed without a task
             * watchdog trigger, which would have panicked and rolled back. */
            ota_confirm_if_pending();
            s_ota_confirmed = true;
        }
        check_inflight_timeout(now);
        pump(tick);
    }
}

esp_err_t pub_start(void)
{
    s_task = xTaskCreateStaticPinnedToCore(publish_task, "publish", PUB_STACK_SIZE, NULL,
                                           PUB_PRIO, s_stack, &s_tcb, PUB_CORE);
    return s_task ? ESP_OK : ESP_FAIL;
}

TaskHandle_t pub_task_handle(void)
{
    return s_task;
}
