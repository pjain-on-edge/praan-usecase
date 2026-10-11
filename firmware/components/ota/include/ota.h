#pragma once
#include "esp_err.h"

/* Starts an HTTPS OTA in its own task. ESP_ERR_INVALID_STATE if one is running. */
esp_err_t ota_start(const char *url);
/* Cancels the pending rollback of a freshly updated image. The caller decides
 * when the image has proven itself. */
void ota_confirm_if_pending(void);
