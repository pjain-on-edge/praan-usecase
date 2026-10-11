#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "app_types.h"

typedef struct {
    gpio_num_t clk, cmd, d0, d1, d2, d3;
    gpio_num_t cd;      /* card detect, active low */
    gpio_num_t pwr_fail; /* power-good from the supply, open drain, low on input loss; GPIO_NUM_NC if absent */
} storage_config_t;

/* Mounts the card, recovers the newest segment and loads read_idx from the meta
 * slots. Without a card, records are kept in RAM and the mount is retried. */
esp_err_t storage_init(const storage_config_t *cfg);
esp_err_t storage_start(QueueHandle_t in_queue);
TaskHandle_t storage_task_handle(void);

/* Copies up to n consecutive records starting at the first stored record at or
 * after *seq. *seq is moved past slots that hold no record (flush padding, deleted
 * segments, records lost while the card was out). Returns ESP_ERR_INVALID_STATE
 * when the records live on a card that is currently unavailable. */
esp_err_t storage_read(uint32_t *seq, uint16_t n, record_t *out, uint16_t *count);

uint32_t storage_write_idx(void);
uint32_t storage_read_idx(void);
/* Monotonic: values below the current read_idx are ignored. */
void storage_advance_read_idx(uint32_t read_idx);
esp_err_t storage_persist_read_idx(void);

/* Pads and writes the partial block. For planned reboots. */
esp_err_t storage_flush(void);
/* storage_flush() plus persisting read_idx. */
void storage_prepare_reboot(void);
uint32_t storage_free_mb(void);
