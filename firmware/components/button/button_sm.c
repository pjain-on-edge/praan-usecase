#include "button_sm.h"

btn_event_t btn_sm_step(btn_sm_t *sm, bool pressed, uint32_t last_edge_us, uint32_t now_us)
{
    if (now_us - last_edge_us < BTN_DEBOUNCE_US) {
        return BTN_EVT_NONE;
    }
    if (pressed && !sm->pressed) {
        sm->pressed = true;
        sm->long_held = false;
        sm->press_us = last_edge_us;
        return BTN_EVT_NONE;
    }
    if (pressed) {
        if (!sm->long_held && now_us - sm->press_us >= BTN_LONG_MIN_US) {
            sm->long_held = true;
            return BTN_EVT_LONG_HELD;
        }
        return BTN_EVT_NONE;
    }
    if (!sm->pressed) {
        return BTN_EVT_NONE;
    }
    sm->pressed = false;
    uint32_t held = last_edge_us - sm->press_us;
    if (held >= BTN_LONG_MIN_US) {
        return BTN_EVT_LONG;
    }
    return held < BTN_SHORT_MAX_US ? BTN_EVT_SHORT : BTN_EVT_CANCEL;
}
