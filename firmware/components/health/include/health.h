#pragma once
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "health_fmt.h"

void health_init(uint32_t boot_count);
void health_inc(health_counter_t c);
void health_add(health_counter_t c, uint32_t n);
void health_set(health_counter_t c, uint32_t v);
uint32_t health_get(health_counter_t c);
void health_gauge_set(health_gauge_t g, int32_t v);

void health_register_task(const char *name, TaskHandle_t task);

const char *health_reset_reason(void);
uint32_t health_boot_count(void);

int health_build_json(char *buf, size_t len, uint32_t unsent);
