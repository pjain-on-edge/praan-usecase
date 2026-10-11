#include <stdio.h>
#include <stdint.h>
#include "esp_mac.h"
#include "app_ids.h"

static char s_device_id[16];
static char s_service_name[16];

static void app_ids_load(void)
{
    if (s_device_id[0]) {
        return;
    }
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_id, sizeof(s_device_id), "praan-%02x%02x%02x", mac[3], mac[4], mac[5]);
    snprintf(s_service_name, sizeof(s_service_name), "PRAAN-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

const char *app_device_id(void)
{
    app_ids_load();
    return s_device_id;
}

const char *app_service_name(void)
{
    app_ids_load();
    return s_service_name;
}
