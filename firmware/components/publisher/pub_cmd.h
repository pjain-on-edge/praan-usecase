#pragma once
#include <stdbool.h>
#include <stddef.h>

#define PUB_CMD_ID_MAX 32

typedef struct {
    char id[PUB_CMD_ID_MAX + 1];
    bool ok;
    const char *err;
    bool reboot;    /* ack first, then flush storage and restart */
} pub_cmd_result_t;

void pub_cmd_execute(const char *json, size_t len, pub_cmd_result_t *res);
