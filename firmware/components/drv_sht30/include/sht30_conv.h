#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

uint8_t sht30_crc8(const uint8_t *data, size_t len);
int16_t sht30_t_cdeg(uint16_t raw);
uint16_t sht30_rh_cpct(uint16_t raw);
/* raw = T msb, T lsb, T crc, RH msb, RH lsb, RH crc. False on CRC mismatch. */
bool sht30_decode(const uint8_t raw[6], int16_t *t_cdeg, uint16_t *rh_cpct);
