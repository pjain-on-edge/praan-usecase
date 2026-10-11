#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Topics, MQTT client and queues. The client connects once Wi-Fi is up and the
 * clock is valid (TLS certificate validation needs real time). */
esp_err_t pub_init(void);
esp_err_t pub_start(void);
TaskHandle_t pub_task_handle(void);
