#include "ds3231_time.h"

#define EPOCH_2000      946684800u
#define EPOCH_2100      4102444800u
#define SECS_PER_DAY    86400u

uint8_t ds3231_bcd2bin(uint8_t bcd)
{
    return (uint8_t)((bcd >> 4) * 10 + (bcd & 0x0F));
}

uint8_t ds3231_bin2bcd(uint8_t bin)
{
    return (uint8_t)((bin / 10) << 4 | (bin % 10));
}

/* Howard Hinnant's days_from_civil / civil_from_days, restricted to y >= 1970. */
static int32_t days_from_civil(int32_t y, int32_t m, int32_t d)
{
    y -= m <= 2;
    int32_t era = y / 400;
    int32_t yoe = y - era * 400;
    int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int32_t z, int32_t *y, int32_t *m, int32_t *d)
{
    z += 719468;
    int32_t era = z / 146097;
    int32_t doe = z - era * 146097;
    int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int32_t mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = yoe + era * 400 + (*m <= 2);
}

static int32_t days_in_month(int32_t y, int32_t m)
{
    static const uint8_t dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return (m == 2 && leap) ? 29 : dim[m - 1];
}

bool ds3231_regs_to_epoch(const uint8_t r[DS3231_TIME_REGS], uint32_t *epoch)
{
    int32_t sec = ds3231_bcd2bin(r[0] & 0x7F);
    int32_t min = ds3231_bcd2bin(r[1] & 0x7F);
    int32_t hour;
    if (r[2] & 0x40) {
        /* 12 h mode: bit 5 is PM. 12 AM is hour 0. */
        hour = ds3231_bcd2bin(r[2] & 0x1F) % 12 + ((r[2] & 0x20) ? 12 : 0);
    } else {
        hour = ds3231_bcd2bin(r[2] & 0x3F);
    }
    int32_t day = ds3231_bcd2bin(r[4] & 0x3F);
    int32_t mon = ds3231_bcd2bin(r[5] & 0x1F);
    int32_t year = 2000 + ds3231_bcd2bin(r[6]);

    if (sec > 59 || min > 59 || hour > 23 || mon < 1 || mon > 12 || year > 2099 ||
        day < 1 || day > days_in_month(year, mon)) {
        return false;
    }
    int32_t days = days_from_civil(year, mon, day);
    *epoch = (uint32_t)days * SECS_PER_DAY + (uint32_t)(hour * 3600 + min * 60 + sec);
    return true;
}

bool ds3231_epoch_to_regs(uint32_t epoch, uint8_t r[DS3231_TIME_REGS])
{
    if (epoch < EPOCH_2000 || epoch >= EPOCH_2100) {
        return false;
    }
    int32_t days = (int32_t)(epoch / SECS_PER_DAY);
    uint32_t sod = epoch % SECS_PER_DAY;
    int32_t y, m, d;
    civil_from_days(days, &y, &m, &d);

    r[0] = ds3231_bin2bcd((uint8_t)(sod % 60));
    r[1] = ds3231_bin2bcd((uint8_t)(sod / 60 % 60));
    r[2] = ds3231_bin2bcd((uint8_t)(sod / 3600));
    r[3] = (uint8_t)((days + 3) % 7 + 1);   /* 1 = Monday; 1970-01-01 was a Thursday */
    r[4] = ds3231_bin2bcd((uint8_t)d);
    r[5] = ds3231_bin2bcd((uint8_t)m);
    r[6] = ds3231_bin2bcd((uint8_t)(y - 2000));
    return true;
}
