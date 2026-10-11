#pragma once
#include <stdbool.h>
#include "esp_err.h"

/* Netif, default event loop, Wi-Fi driver and SNTP client. Reports whether BLE
 * provisioning has to run (no stored credentials, or prov_req set). */
esp_err_t net_init(bool *need_provisioning);
/* Starts provisioning or connects as STA with the stored credentials. */
esp_err_t net_start(bool provision);

/* Called from the SNTP context after every successful sync. */
void net_set_time_sync_cb(void (*cb)(void));
void net_resync_time(void);
/* RSSI of the current AP in dBm, 0 when not associated. */
int net_rssi(void);

/* Next boot runs BLE provisioning. */
esp_err_t net_request_provisioning(void);
esp_err_t net_erase_credentials(void);
