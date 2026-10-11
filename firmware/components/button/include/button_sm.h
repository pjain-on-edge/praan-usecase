#pragma once
#include <stdbool.h>
#include <stdint.h>

#define BTN_DEBOUNCE_US     (50 * 1000u)
#define BTN_SHORT_MAX_US    (2 * 1000000u)
#define BTN_LONG_MIN_US     (5 * 1000000u)

typedef enum {
    BTN_EVT_NONE,
    BTN_EVT_SHORT,          /* released before 2 s */
    BTN_EVT_LONG_HELD,      /* still held, 5 s reached */
    BTN_EVT_LONG,           /* released after 5 s or more */
    BTN_EVT_CANCEL,         /* released between 2 and 5 s */
} btn_event_t;

typedef struct {
    bool pressed;
    bool long_held;
    uint32_t press_us;
} btn_sm_t;

/* Timestamps are 32-bit microseconds and compared wrap-safe. The level is only
 * trusted once no edge has been seen for BTN_DEBOUNCE_US. */
btn_event_t btn_sm_step(btn_sm_t *sm, bool pressed, uint32_t last_edge_us, uint32_t now_us);
