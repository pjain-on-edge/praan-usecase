#pragma once
#include <stdbool.h>
#include <stdint.h>

#define DS3231_TIME_REGS 7

uint8_t ds3231_bcd2bin(uint8_t bcd);
uint8_t ds3231_bin2bcd(uint8_t bin);

/* Registers 0x00..0x06 to UTC epoch. Years 2000..2099. False on out-of-range fields. */
bool ds3231_regs_to_epoch(const uint8_t regs[DS3231_TIME_REGS], uint32_t *epoch);
/* UTC epoch to registers in 24 h mode. False if the epoch is outside 2000..2099. */
bool ds3231_epoch_to_regs(uint32_t epoch, uint8_t regs[DS3231_TIME_REGS]);
