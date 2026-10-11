#pragma once

/* "praan-xxxxxx" from the last 3 bytes of the STA MAC. */
const char *app_device_id(void);
/* "PRAAN-XXXXXX", BLE provisioning service name. */
const char *app_service_name(void);
