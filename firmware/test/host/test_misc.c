#include <string.h>
#include "unity.h"
#include "net_backoff.h"
#include "button_sm.h"
#include "led_pattern.h"
#include "health_fmt.h"
#include "app_types.h"

void setUp(void) {}
void tearDown(void) {}

static void test_backoff_doubles_and_caps(void)
{
    /* rnd % 41 == 20 means no jitter */
    TEST_ASSERT_EQUAL_UINT32(1000, net_backoff_ms(0, 20));
    TEST_ASSERT_EQUAL_UINT32(2000, net_backoff_ms(1, 20));
    TEST_ASSERT_EQUAL_UINT32(32000, net_backoff_ms(5, 20));
    TEST_ASSERT_EQUAL_UINT32(60000, net_backoff_ms(6, 20));
    TEST_ASSERT_EQUAL_UINT32(60000, net_backoff_ms(1000, 20));
}

static void test_backoff_jitter_bounds(void)
{
    TEST_ASSERT_EQUAL_UINT32(800, net_backoff_ms(0, 0));
    TEST_ASSERT_EQUAL_UINT32(1200, net_backoff_ms(0, 40));
    TEST_ASSERT_EQUAL_UINT32(48000, net_backoff_ms(9, 0));
    TEST_ASSERT_EQUAL_UINT32(72000, net_backoff_ms(9, 40));
    for (uint32_t r = 0; r < 1000; r++) {
        uint32_t ms = net_backoff_ms(3, r * 2654435761u);
        TEST_ASSERT_TRUE(ms >= 6400 && ms <= 9600);
    }
}

static btn_sm_t sm;

static btn_event_t press(uint32_t at_us, uint32_t duration_us, btn_event_t *held_evt)
{
    memset(&sm, 0, sizeof(sm));
    /* Pressed edge at at_us, then polled every 50 ms until release. */
    *held_evt = BTN_EVT_NONE;
    for (uint32_t t = at_us + 50000; t - at_us < duration_us; t += 50000) {
        btn_event_t e = btn_sm_step(&sm, true, at_us, t);
        if (e != BTN_EVT_NONE) {
            *held_evt = e;
        }
    }
    uint32_t release = at_us + duration_us;
    TEST_ASSERT_EQUAL(BTN_EVT_NONE, btn_sm_step(&sm, false, release, release + 10000));
    return btn_sm_step(&sm, false, release, release + 60000);
}

static void test_button_short_long_cancel(void)
{
    btn_event_t held;
    TEST_ASSERT_EQUAL(BTN_EVT_SHORT, press(1000000, 300000, &held));
    TEST_ASSERT_EQUAL(BTN_EVT_NONE, held);
    TEST_ASSERT_EQUAL(BTN_EVT_CANCEL, press(1000000, 3000000, &held));
    TEST_ASSERT_EQUAL(BTN_EVT_LONG, press(1000000, 6000000, &held));
    TEST_ASSERT_EQUAL(BTN_EVT_LONG_HELD, held);
}

static void test_button_survives_timer_wrap(void)
{
    btn_event_t held;
    TEST_ASSERT_EQUAL(BTN_EVT_LONG, press(0xFFFFFFFFu - 1000000u, 5500000, &held));
    TEST_ASSERT_EQUAL(BTN_EVT_SHORT, press(0xFFFFFFFFu - 100000u, 400000, &held));
}

static void test_button_bounce_ignored(void)
{
    memset(&sm, 0, sizeof(sm));
    /* Edges keep arriving: level is never trusted. */
    for (uint32_t t = 0; t < 400000; t += 10000) {
        TEST_ASSERT_EQUAL(BTN_EVT_NONE, btn_sm_step(&sm, (t / 10000) & 1, t, t + 5000));
    }
    TEST_ASSERT_FALSE(sm.pressed);
}

static void test_led_priorities(void)
{
    uint32_t all = EVT_FAULT_SD | EVT_FAULT_SENSOR | EVT_OTA_ACTIVE | EVT_PROV_ACTIVE;
    TEST_ASSERT_EQUAL(LED_ST_SD_FAULT, led_select(all, true, true, false));
    TEST_ASSERT_EQUAL(LED_ST_SENSOR_FAULT, led_select(all & ~EVT_FAULT_SD, true, true, false));
    TEST_ASSERT_EQUAL(LED_ST_OTA, led_select(EVT_OTA_ACTIVE | EVT_PROV_ACTIVE, true, true, false));
    TEST_ASSERT_EQUAL(LED_ST_PROVISIONING, led_select(EVT_PROV_ACTIVE, true, false, false));
    TEST_ASSERT_EQUAL(LED_ST_CONNECTING, led_select(EVT_WIFI_UP, true, false, false));
    TEST_ASSERT_EQUAL(LED_ST_OFFLINE, led_select(EVT_WIFI_UP, true, true, false));
    TEST_ASSERT_EQUAL(LED_ST_ONLINE, led_select(EVT_WIFI_UP | EVT_MQTT_UP, true, true, false));
    TEST_ASSERT_EQUAL(LED_ST_BUTTON_HOLD, led_select(EVT_FAULT_SD, true, true, true));
}

static void test_led_disabled_shows_faults_only(void)
{
    TEST_ASSERT_EQUAL(LED_ST_SD_FAULT, led_select(EVT_FAULT_SD, false, true, false));
    TEST_ASSERT_EQUAL(LED_ST_SENSOR_FAULT, led_select(EVT_FAULT_SENSOR, false, true, false));
    TEST_ASSERT_EQUAL(LED_ST_OFF, led_select(EVT_OTA_ACTIVE, false, true, false));
    TEST_ASSERT_EQUAL(LED_ST_OFF, led_select(EVT_WIFI_UP | EVT_MQTT_UP, false, true, false));
}

static void test_led_online_blip(void)
{
    led_rgb_t off = led_render(LED_ST_ONLINE, 0, false);
    led_rgb_t on = led_render(LED_ST_ONLINE, 0, true);
    TEST_ASSERT_EQUAL_UINT8(0, off.g);
    TEST_ASSERT_EQUAL_UINT8(255, on.g);
}

static void test_status_json(void)
{
    const health_status_t st = {
        .fw = "1.0.0", .reset = "poweron", .boot = 3, .unsent = 42, .sd_free_mb = 900, .rssi = -61,
    };
    char buf[192];
    TEST_ASSERT_GREATER_THAN(0, health_format_status(buf, sizeof(buf), &st));
    TEST_ASSERT_EQUAL_STRING("{\"online\":true,\"fw\":\"1.0.0\",\"reset\":\"poweron\",\"boot\":3,"
                             "\"unsent\":42,\"sd_free_mb\":900,\"rssi\":-61}", buf);
    TEST_ASSERT_EQUAL_INT(-1, health_format_status(buf, 20, &st));
}

static void test_health_json_fits_worst_case(void)
{
    health_snapshot_t s;
    memset(&s, 0xFF, sizeof(s));
    s.n_tasks = 4;
    const char *names[4] = {"sensor", "storage", "publish", "led"};
    for (int i = 0; i < 4; i++) {
        s.tasks[i].name = names[i];
    }
    s.gauge[HG_RTC_TEMP_CDEG] = -4000;
    char buf[512];
    TEST_ASSERT_GREATER_THAN(0, health_format_json(buf, sizeof(buf), &s));
    TEST_ASSERT_EQUAL_INT(-1, health_format_json(buf, 100, &s));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_backoff_doubles_and_caps);
    RUN_TEST(test_backoff_jitter_bounds);
    RUN_TEST(test_button_short_long_cancel);
    RUN_TEST(test_button_survives_timer_wrap);
    RUN_TEST(test_button_bounce_ignored);
    RUN_TEST(test_led_priorities);
    RUN_TEST(test_led_disabled_shows_faults_only);
    RUN_TEST(test_led_online_blip);
    RUN_TEST(test_status_json);
    RUN_TEST(test_health_json_fits_worst_case);
    return UNITY_END();
}
