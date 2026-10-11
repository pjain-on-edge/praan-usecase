#include <stdatomic.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/ledc.h"
#include "status_led.h"
#include "led_pattern.h"
#include "app_events.h"
#include "app_nvs.h"

/* 3 KB rather than 2 KB: the button tick hook runs the reboot paths (SD flush,
 * NVS erase) in this task's context. */
#define LED_STACK_SIZE      3072
#define LED_PRIO            2
#define LED_CORE            1
#define LED_PERIOD_MS       50
#define LED_FREQ_HZ         5000
#define LED_RES             LEDC_TIMER_10_BIT
#define LED_DUTY_FULL       ((1u << 10) - 1)
#define NVS_KEY_LED_EN      "led_en"

static const char *TAG = "led";

static StaticTask_t s_tcb;
static StackType_t s_stack[LED_STACK_SIZE];
static TaskHandle_t s_task;
static void (*s_tick_hook)(void);

static const ledc_channel_t s_channels[3] = {LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2};
static uint32_t s_max_duty;
static uint32_t s_duty[3] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
static atomic_bool s_enabled = true;
static atomic_bool s_blip;
static atomic_bool s_hold;
static bool s_ever_online;

static void led_apply(led_rgb_t c)
{
    const uint8_t level[3] = {c.r, c.g, c.b};
    for (int i = 0; i < 3; i++) {
        uint32_t duty = level[i] * s_max_duty / 255;
        if (duty != s_duty[i]) {
            s_duty[i] = duty;
            ledc_set_duty(LEDC_LOW_SPEED_MODE, s_channels[i], duty);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, s_channels[i]);
        }
    }
}

esp_err_t led_init(gpio_num_t r, gpio_num_t g, gpio_num_t b)
{
    s_max_duty = LED_DUTY_FULL * CONFIG_APP_LED_MAX_DUTY_PCT / 100;
    uint32_t en = 1;
    app_nvs_get_u32(APP_NVS_NS, NVS_KEY_LED_EN, &en);
    atomic_store(&s_enabled, en != 0);

    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LED_RES,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = LED_FREQ_HZ,
        /* XTAL is unaffected by DFS, so brightness does not jump with APB. */
        .clk_cfg = LEDC_USE_XTAL_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        return err;
    }
    const gpio_num_t pins[3] = {r, g, b};
    for (int i = 0; i < 3; i++) {
        const ledc_channel_config_t ch = {
            .gpio_num = pins[i],
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = s_channels[i],
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .flags.output_invert = 1,   /* common anode, active low */
        };
        err = ledc_channel_config(&ch);
        if (err != ESP_OK) {
            return err;
        }
    }
    led_apply((led_rgb_t){255, 255, 255});
    return ESP_OK;
}

static void led_task(void *arg)
{
    (void)arg;
    for (;;) {
        EventBits_t bits = xEventGroupGetBits(app_events());
        if ((bits & (EVT_WIFI_UP | EVT_MQTT_UP)) == (EVT_WIFI_UP | EVT_MQTT_UP)) {
            s_ever_online = true;
        }
        led_state_t st = led_select(bits, atomic_load(&s_enabled), s_ever_online, atomic_load(&s_hold));
        uint32_t t_ms = (uint32_t)(esp_timer_get_time() / 1000);
        led_apply(led_render(st, t_ms, atomic_exchange(&s_blip, false)));

        if (s_tick_hook) {
            s_tick_hook();
        }
        vTaskDelay(pdMS_TO_TICKS(LED_PERIOD_MS));
    }
}

esp_err_t led_start(void (*tick_hook)(void))
{
    s_tick_hook = tick_hook;
    s_task = xTaskCreateStaticPinnedToCore(led_task, "led", LED_STACK_SIZE, NULL, LED_PRIO,
                                           s_stack, &s_tcb, LED_CORE);
    return s_task ? ESP_OK : ESP_FAIL;
}

TaskHandle_t led_task_handle(void)
{
    return s_task;
}

esp_err_t led_set_enabled(bool enabled)
{
    atomic_store(&s_enabled, enabled);
    ESP_LOGI(TAG, "status LED %s", enabled ? "enabled" : "disabled (faults only)");
    return app_nvs_set_u32(APP_NVS_NS, NVS_KEY_LED_EN, enabled);
}

void led_blip(void)
{
    atomic_store(&s_blip, true);
}

void led_set_hold_indicator(bool on)
{
    atomic_store(&s_hold, on);
}
