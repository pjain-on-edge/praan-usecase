#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

typedef struct {
    void (*on_short)(void);
    void (*on_long)(void);
    void (*on_long_held)(bool held);
} button_callbacks_t;

/* Both-edge interrupt on an active-low button. The GPIO ISR service must be
 * installed. Callbacks run from button_poll(), never from the ISR. */
esp_err_t button_init(gpio_num_t pin, const button_callbacks_t *cbs);
/* Call periodically (50 ms is enough for the 50 ms debounce). */
void button_poll(void);
