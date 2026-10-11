#pragma once
#include <stdint.h>
#include "esp_err.h"

/* "app" namespace: boot counter, prov_req, led_en. Erased by factory reset. */
#define APP_NVS_NS          "app"
/* "storage" namespace: sequence floor. Survives factory reset, like the SD data. */
#define APP_NVS_NS_STORAGE  "storage"

esp_err_t app_nvs_get_u32(const char *ns, const char *key, uint32_t *val);
esp_err_t app_nvs_set_u32(const char *ns, const char *key, uint32_t val);
esp_err_t app_nvs_erase_namespace(const char *ns);
