#include <string.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include "net.h"
#include "net_backoff.h"
#include "app_events.h"
#include "app_ids.h"
#include "app_nvs.h"

#define NVS_KEY_PROV_REQ    "prov_req"
#define SNTP_SERVER         "pool.ntp.org"

static const char *TAG = "net";

static esp_timer_handle_t s_retry_timer;
static uint32_t s_attempt;
static bool s_sntp_started;
static void (*s_time_cb)(void);

static void retry_cb(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

static void schedule_reconnect(void)
{
    uint32_t delay_ms = net_backoff_ms(s_attempt, esp_random());
    if (s_attempt < 31) {
        s_attempt++;
    }
    ESP_LOGI(TAG, "reconnect in %lu ms", (unsigned long)delay_ms);
    esp_timer_stop(s_retry_timer);
    esp_timer_start_once(s_retry_timer, (uint64_t)delay_ms * 1000);
}

static void sntp_synced(struct timeval *tv)
{
    ESP_LOGI(TAG, "SNTP sync: %lld", (long long)tv->tv_sec);
    xEventGroupSetBits(app_events(), EVT_TIME_VALID);
    if (s_time_cb) {
        s_time_cb();
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    EventBits_t bits = xEventGroupGetBits(app_events());
    switch (id) {
    case WIFI_EVENT_STA_START:
        if (!(bits & EVT_PROV_ACTIVE)) {
            esp_wifi_connect();
        }
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *ev = data;
        xEventGroupClearBits(app_events(), EVT_WIFI_UP);
        ESP_LOGW(TAG, "disconnected, reason %d", ev->reason);
        schedule_reconnect();
        break;
    }
    default:
        break;
    }
    (void)base;
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id != IP_EVENT_STA_GOT_IP) {
        return;
    }
    const ip_event_got_ip_t *ev = data;
    ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&ev->ip_info.ip));
    s_attempt = 0;
    xEventGroupSetBits(app_events(), EVT_WIFI_UP);
    if (!s_sntp_started) {
        s_sntp_started = true;
        esp_netif_sntp_start();
    }
}

static void on_prov_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    switch (id) {
    case WIFI_PROV_START:
        ESP_LOGI(TAG, "BLE provisioning as %s", app_service_name());
        break;
    case WIFI_PROV_CRED_RECV:
        ESP_LOGI(TAG, "credentials received for \"%s\"", (const char *)((wifi_sta_config_t *)data)->ssid);
        break;
    case WIFI_PROV_CRED_FAIL:
        ESP_LOGW(TAG, "provisioned credentials rejected by AP, waiting for new ones");
        wifi_prov_mgr_reset_sm_state_on_failure();
        break;
    case WIFI_PROV_CRED_SUCCESS:
        ESP_LOGI(TAG, "provisioning succeeded");
        break;
    case WIFI_PROV_END:
        /* The scheme handler frees the BT controller and host memory after this. */
        wifi_prov_mgr_deinit();
        xEventGroupClearBits(app_events(), EVT_PROV_ACTIVE);
        break;
    default:
        break;
    }
}

esp_err_t net_init(bool *need_provisioning)
{
    esp_err_t err = esp_netif_init();
    if (err == ESP_OK) {
        err = esp_event_loop_create_default();
    }
    if (err != ESP_OK) {
        return err;
    }
    esp_netif_create_default_wifi_sta();

    const wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wcfg);
    if (err != ESP_OK) {
        return err;
    }
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL);
    esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, on_prov_event, NULL);

    const esp_timer_create_args_t targs = {.callback = retry_cb, .name = "wifi_retry"};
    err = esp_timer_create(&targs, &s_retry_timer);
    if (err != ESP_OK) {
        return err;
    }

    esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG(SNTP_SERVER);
    sc.start = false;
    sc.sync_cb = sntp_synced;
    err = esp_netif_sntp_init(&sc);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t cfg = {0};
    bool have_creds = esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && cfg.sta.ssid[0] != '\0';
    uint32_t prov_req = 0;
    app_nvs_get_u32(APP_NVS_NS, NVS_KEY_PROV_REQ, &prov_req);
    *need_provisioning = !have_creds || prov_req;
    if (*need_provisioning) {
        ESP_LOGI(TAG, "provisioning required (%s)", have_creds ? "requested" : "no credentials");
    }
    return ESP_OK;
}

static esp_err_t start_provisioning(void)
{
    const wifi_prov_mgr_config_t cfg = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
    };
    esp_err_t err = wifi_prov_mgr_init(cfg);
    if (err != ESP_OK) {
        return err;
    }
    xEventGroupSetBits(app_events(), EVT_PROV_ACTIVE);
    /* Cleared up front: if nobody provisions, the next reboot uses the old credentials. */
    app_nvs_set_u32(APP_NVS_NS, NVS_KEY_PROV_REQ, 0);
    return wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, CONFIG_APP_PROV_POP,
                                            app_service_name(), NULL);
}

esp_err_t net_start(bool provision)
{
    esp_err_t err;
    if (provision) {
        err = start_provisioning();
    } else {
        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err == ESP_OK) {
            err = esp_wifi_start();
        }
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    }
    return err;
}

void net_set_time_sync_cb(void (*cb)(void))
{
    s_time_cb = cb;
}

void net_resync_time(void)
{
    if (s_sntp_started) {
        esp_netif_sntp_start();
    }
}

int net_rssi(void)
{
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}

esp_err_t net_request_provisioning(void)
{
    return app_nvs_set_u32(APP_NVS_NS, NVS_KEY_PROV_REQ, 1);
}

esp_err_t net_erase_credentials(void)
{
    return esp_wifi_restore();
}
