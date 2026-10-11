#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Configures LEDC and shows the boot colour (solid white). */
esp_err_t led_init(gpio_num_t r, gpio_num_t g, gpio_num_t b);
/* Starts led_task; tick_hook (may be NULL) runs every 50 ms in its context. */
esp_err_t led_start(void (*tick_hook)(void));
TaskHandle_t led_task_handle(void);

/* Persisted in NVS. When disabled only fault states are shown. */
esp_err_t led_set_enabled(bool enabled);
/* Green 50 ms blip when online. */
void led_blip(void);
/* White fast blink while the button is held past the factory-reset threshold. */
void led_set_hold_indicator(bool on);
