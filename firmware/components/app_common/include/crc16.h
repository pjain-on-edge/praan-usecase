#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout. */
uint16_t crc16_ccitt(const void *data, size_t len);
uint16_t crc16_ccitt_sw(const void *data, size_t len);

/* Selects the ROM implementation if it reproduces the check value. */
bool crc16_init(void);
