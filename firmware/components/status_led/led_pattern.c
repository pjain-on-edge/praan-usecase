#include "led_pattern.h"
#include "app_types.h"

static const led_rgb_t BLACK  = {0, 0, 0};
static const led_rgb_t RED    = {255, 0, 0};
static const led_rgb_t PURPLE = {160, 0, 255};
static const led_rgb_t BLUE   = {0, 0, 255};
static const led_rgb_t AMBER  = {255, 100, 0};
static const led_rgb_t YELLOW = {255, 200, 0};
static const led_rgb_t GREEN  = {0, 255, 0};
static const led_rgb_t WHITE  = {255, 255, 255};

led_state_t led_select(uint32_t bits, bool enabled, bool ever_online, bool button_hold)
{
    if (button_hold) {
        return LED_ST_BUTTON_HOLD;
    }
    if (bits & EVT_FAULT_SD) {
        return LED_ST_SD_FAULT;
    }
    if (bits & EVT_FAULT_SENSOR) {
        return LED_ST_SENSOR_FAULT;
    }
    if (!enabled) {
        return LED_ST_OFF;
    }
    if (bits & EVT_OTA_ACTIVE) {
        return LED_ST_OTA;
    }
    if (bits & EVT_PROV_ACTIVE) {
        return LED_ST_PROVISIONING;
    }
    if ((bits & (EVT_WIFI_UP | EVT_MQTT_UP)) == (EVT_WIFI_UP | EVT_MQTT_UP)) {
        return LED_ST_ONLINE;
    }
    return ever_online ? LED_ST_OFFLINE : LED_ST_CONNECTING;
}

static led_rgb_t blink(led_rgb_t c, uint32_t t_ms, uint32_t period_ms)
{
    return (t_ms % period_ms) < period_ms / 2 ? c : BLACK;
}

static led_rgb_t scale(led_rgb_t c, uint32_t level)
{
    return (led_rgb_t){
        (uint8_t)(c.r * level / 255), (uint8_t)(c.g * level / 255), (uint8_t)(c.b * level / 255),
    };
}

led_rgb_t led_render(led_state_t st, uint32_t t_ms, bool blip)
{
    switch (st) {
    case LED_ST_SD_FAULT:
    case LED_ST_CONNECTING:
        return blink(st == LED_ST_SD_FAULT ? RED : YELLOW, t_ms, 250);
    case LED_ST_SENSOR_FAULT: {
        /* Two 200 ms blinks, then a 1 s pause. */
        uint32_t p = t_ms % 1800;
        return (p < 200 || (p >= 400 && p < 600)) ? RED : BLACK;
    }
    case LED_ST_OTA: {
        uint32_t p = t_ms % 2000;
        return scale(PURPLE, (p < 1000 ? p : 2000 - p) * 255 / 1000);
    }
    case LED_ST_PROVISIONING:
        return blink(BLUE, t_ms, 1000);
    case LED_ST_OFFLINE:
        return blink(AMBER, t_ms, 1000);
    case LED_ST_ONLINE:
        return blip ? GREEN : BLACK;
    case LED_ST_BUTTON_HOLD:
        return blink(WHITE, t_ms, 250);
    case LED_ST_OFF:
    default:
        return BLACK;
    }
}
