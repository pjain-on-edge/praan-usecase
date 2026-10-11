#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LED_ST_OFF,
    LED_ST_SD_FAULT,
    LED_ST_SENSOR_FAULT,
    LED_ST_OTA,
    LED_ST_PROVISIONING,
    LED_ST_OFFLINE,
    LED_ST_CONNECTING,
    LED_ST_ONLINE,
    LED_ST_BUTTON_HOLD,
} led_state_t;

typedef struct {
    uint8_t r, g, b;
} led_rgb_t;

/* Highest-priority state for the given event bits. With the LED disabled only
 * the two fault states remain visible. */
led_state_t led_select(uint32_t event_bits, bool enabled, bool ever_online, bool button_hold);
led_rgb_t led_render(led_state_t st, uint32_t t_ms, bool blip);
