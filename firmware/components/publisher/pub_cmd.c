#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "pub_cmd.h"
#include "net.h"
#include "ota.h"
#include "status_led.h"

static const char *TAG = "pub_cmd";

/* The id is echoed into the ack JSON unescaped, so only plain characters pass. */
static bool copy_id(const cJSON *id, char *out)
{
    if (!cJSON_IsString(id) || strlen(id->valuestring) > PUB_CMD_ID_MAX) {
        return false;
    }
    for (const char *p = id->valuestring; *p; p++) {
        bool plain = (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                     *p == '-' || *p == '_' || *p == '.' || *p == ':';
        if (!plain) {
            return false;
        }
    }
    strcpy(out, id->valuestring);
    return true;
}

static void run(const char *cmd, const cJSON *root, pub_cmd_result_t *res)
{
    if (strcmp(cmd, "reboot") == 0) {
        res->reboot = true;
    } else if (strcmp(cmd, "ota") == 0) {
        const cJSON *url = cJSON_GetObjectItemCaseSensitive(root, "url");
        if (!cJSON_IsString(url) || strncmp(url->valuestring, "https://", 8) != 0) {
            res->err = "url must be https://";
            return;
        }
        esp_err_t err = ota_start(url->valuestring);
        if (err != ESP_OK) {
            res->err = err == ESP_ERR_INVALID_STATE ? "ota already running" : "ota start failed";
            return;
        }
    } else if (strcmp(cmd, "led") == 0) {
        const cJSON *en = cJSON_GetObjectItemCaseSensitive(root, "enable");
        if (!cJSON_IsBool(en)) {
            res->err = "enable must be bool";
            return;
        }
        if (led_set_enabled(cJSON_IsTrue(en)) != ESP_OK) {
            res->err = "nvs write failed";
            return;
        }
    } else if (strcmp(cmd, "provision") == 0) {
        if (net_request_provisioning() != ESP_OK) {
            res->err = "nvs write failed";
            return;
        }
        res->reboot = true;
    } else if (strcmp(cmd, "resync_time") == 0) {
        net_resync_time();
    } else {
        res->err = "unknown command";
        return;
    }
    res->ok = true;
    res->err = "";
}

void pub_cmd_execute(const char *json, size_t len, pub_cmd_result_t *res)
{
    *res = (pub_cmd_result_t){.ok = false, .err = "bad json"};
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        return;
    }
    if (!copy_id(cJSON_GetObjectItemCaseSensitive(root, "id"), res->id)) {
        res->err = "bad id";
    } else {
        const cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
        if (cJSON_IsString(cmd)) {
            run(cmd->valuestring, root, res);
        } else {
            res->err = "missing cmd";
        }
    }
    ESP_LOGI(TAG, "cmd id=%s ok=%d err=%s", res->id, res->ok, res->err);
    cJSON_Delete(root);
}
