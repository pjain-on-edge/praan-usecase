#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "button.h"
#include "button_sm.h"

static const char *TAG = "button";

static gpio_num_t s_pin;
static button_callbacks_t s_cbs;
static btn_sm_t s_sm;
/* 32 bits so the ISR's store and the poller's load are single, untorn accesses. */
static volatile uint32_t s_last_edge_us;

static void IRAM_ATTR button_isr(void *arg)
{
    (void)arg;
    s_last_edge_us = (uint32_t)esp_timer_get_time();
}

esp_err_t button_init(gpio_num_t pin, const button_callbacks_t *cbs)
{
    s_pin = pin;
    s_cbs = *cbs;
    s_last_edge_us = (uint32_t)esp_timer_get_time() - BTN_DEBOUNCE_US;
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&io);
    if (err == ESP_OK) {
        err = gpio_isr_handler_add(pin, button_isr, NULL);
    }
    return err;
}

void button_poll(void)
{
    bool pressed = gpio_get_level(s_pin) == 0;
    switch (btn_sm_step(&s_sm, pressed, s_last_edge_us, (uint32_t)esp_timer_get_time())) {
    case BTN_EVT_SHORT:
        ESP_LOGW(TAG, "short press: reboot into provisioning");
        if (s_cbs.on_short) {
            s_cbs.on_short();
        }
        break;
    case BTN_EVT_LONG_HELD:
        if (s_cbs.on_long_held) {
            s_cbs.on_long_held(true);
        }
        break;
    case BTN_EVT_LONG:
        ESP_LOGW(TAG, "long press: factory reset");
        if (s_cbs.on_long) {
            s_cbs.on_long();
        }
        break;
    case BTN_EVT_CANCEL:
        ESP_LOGI(TAG, "press between 2 and 5 s ignored");
        break;
    case BTN_EVT_NONE:
    default:
        break;
    }
}
