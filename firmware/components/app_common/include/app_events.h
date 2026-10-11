#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "app_types.h"

void app_events_init(void);
EventGroupHandle_t app_events(void);
