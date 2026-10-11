#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_bt.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "board_pins.h"
#include "app_types.h"
#include "app_events.h"
#include "app_nvs.h"
#include "crc16.h"
#include "health.h"
#include "status_led.h"
#include "sensor_mgr.h"
#include "storage.h"
#include "net.h"
#include "publisher.h"
#include "button.h"

#define RECORD_QUEUE_LEN    32
#define NVS_KEY_BOOT_COUNT  "boot"
#define CPU_MAX_MHZ         160
#define CPU_MIN_MHZ         40

static const char *TAG = "main";

static StaticQueue_t s_record_q_buf;
static uint8_t s_record_q_storage[RECORD_QUEUE_LEN * sizeof(record_t)];

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS layout changed, erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static uint32_t bump_boot_count(void)
{
    uint32_t count = 0;
    app_nvs_get_u32(APP_NVS_NS, NVS_KEY_BOOT_COUNT, &count);
    count++;
    app_nvs_set_u32(APP_NVS_NS, NVS_KEY_BOOT_COUNT, count);
    return count;
}

static void init_power(void)
{
    const esp_pm_config_t pm = {
        .max_freq_mhz = CPU_MAX_MHZ,
        .min_freq_mhz = CPU_MIN_MHZ,
#if CONFIG_APP_LIGHT_SLEEP
        .light_sleep_enable = true,
#endif
    };
    esp_err_t err = esp_pm_configure(&pm);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "power management not configured: %s", esp_err_to_name(err));
    }
}

static void log_err(const char *what, esp_err_t err)
{
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: %s", what, esp_err_to_name(err));
    }
}

static void on_button_short(void)
{
    log_err("prov_req", net_request_provisioning());
    storage_prepare_reboot();
    esp_restart();
}

static void on_button_long(void)
{
    /* SD data and the sequence floor are kept: they belong to the device, not
     * to the user configuration. */
    storage_prepare_reboot();
    log_err("erase Wi-Fi credentials", net_erase_credentials());
    log_err("erase app NVS", app_nvs_erase_namespace(APP_NVS_NS));
    esp_restart();
}

void app_main(void)
{
    init_nvs();
    app_events_init();
    uint32_t boot_count = bump_boot_count();

    health_init(boot_count);
    if (!crc16_init()) {
        ESP_LOGW(TAG, "ROM CRC16 does not match CCITT-FALSE, using software CRC");
    }
    init_power();

    log_err("LED init", led_init(PIN_LED_R, PIN_LED_G, PIN_LED_B));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    bool provision = false;
    ESP_ERROR_CHECK(net_init(&provision));
    if (!provision) {
        /* BLE is only used for provisioning at boot; give its ~60 KB back. */
        log_err("BT memory release", esp_bt_mem_release(ESP_BT_MODE_BLE));
    }

    const sensor_mgr_config_t sensors = {
        .i2c_sda = PIN_I2C_SDA,
        .i2c_scl = PIN_I2C_SCL,
        .i2c_hz = CONFIG_APP_I2C_FREQ_HZ,
        .rtc_sqw = PIN_RTC_SQW,
        .pms = {
            .uart = PMS_UART_NUM,
            .tx = PIN_PMS_TX,
            .rx = PIN_PMS_RX,
            .set = PIN_PMS_SET,
            .reset = PIN_PMS_RESET,
        },
    };
    log_err("sensor init", sensor_mgr_init(&sensors));

    const storage_config_t sd = {
        .clk = PIN_SD_CLK, .cmd = PIN_SD_CMD,
        .d0 = PIN_SD_D0, .d1 = PIN_SD_D1, .d2 = PIN_SD_D2, .d3 = PIN_SD_D3,
        .cd = PIN_SD_CD,
        .pwr_fail = PIN_PWR_GOOD,
    };
    log_err("storage init", storage_init(&sd));

    QueueHandle_t records = xQueueCreateStatic(RECORD_QUEUE_LEN, sizeof(record_t),
                                               s_record_q_storage, &s_record_q_buf);
    ESP_ERROR_CHECK(storage_start(records));
    ESP_ERROR_CHECK(sensor_mgr_start(records));

    net_set_time_sync_cb(sensor_mgr_time_synced);
    log_err("network start", net_start(provision));

    ESP_ERROR_CHECK(pub_init());
    ESP_ERROR_CHECK(pub_start());
    const button_callbacks_t buttons = {
        .on_short = on_button_short,
        .on_long = on_button_long,
        .on_long_held = led_set_hold_indicator,
    };
    log_err("button init", button_init(PIN_BUTTON, &buttons));
    ESP_ERROR_CHECK(led_start(button_poll));

    health_register_task("sensor", sensor_mgr_task());
    health_register_task("storage", storage_task_handle());
    health_register_task("publish", pub_task_handle());
    health_register_task("led", led_task_handle());

    ESP_LOGI(TAG, "boot complete in %lld ms", esp_timer_get_time() / 1000);
}
