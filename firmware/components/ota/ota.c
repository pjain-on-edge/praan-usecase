#include <string.h>
#include <stdatomic.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota.h"
#include "storage.h"
#include "app_events.h"

/* TLS handshake plus the HTTP client run on this stack. */
#define OTA_STACK_SIZE      8192
#define OTA_PRIO            5
#define OTA_CORE            0
#define OTA_URL_MAX         256
#define OTA_HTTP_TIMEOUT_MS 15000

static const char *TAG = "ota";

static StaticTask_t s_tcb;
static StackType_t s_stack[OTA_STACK_SIZE];
static TaskHandle_t s_task;
static char s_url[OTA_URL_MAX];
static atomic_bool s_running;

/* Created once and kept: re-creating a static task after vTaskDelete() races
 * the idle task's cleanup of the same TCB. */
static void ota_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGI(TAG, "downloading %s", s_url);
        const esp_http_client_config_t http = {
            .url = s_url,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = OTA_HTTP_TIMEOUT_MS,
            .keep_alive_enable = true,
        };
        const esp_https_ota_config_t cfg = {.http_config = &http};
        esp_err_t err = esp_https_ota(&cfg);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "image written, rebooting into it");
            storage_prepare_reboot();
            esp_restart();
        }
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
        xEventGroupClearBits(app_events(), EVT_OTA_ACTIVE);
        atomic_store(&s_running, false);
    }
}

esp_err_t ota_start(const char *url)
{
    if (strlen(url) >= sizeof(s_url)) {
        return ESP_ERR_INVALID_ARG;
    }
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_running, &expected, true)) {
        return ESP_ERR_INVALID_STATE;
    }
    strcpy(s_url, url);
    if (!s_task) {
        s_task = xTaskCreateStaticPinnedToCore(ota_task, "ota", OTA_STACK_SIZE, NULL, OTA_PRIO,
                                               s_stack, &s_tcb, OTA_CORE);
    }
    xEventGroupSetBits(app_events(), EVT_OTA_ACTIVE);
    xTaskNotifyGive(s_task);
    return ESP_OK;
}

void ota_confirm_if_pending(void)
{
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "new image confirmed, rollback cancelled: %s", esp_err_to_name(err));
    }
}
