// The core application's wall clock (T191). The host app hands its local time over in the hand-over page; the application sets its
// software clock (rtc_dummy.cc) from it at start, and CoreIo::wall_time reads that clock through the FAT timestamp. Header-only so a
// host test runs the real thing against the real rtc_dummy.cc.
#ifndef COREAPP_WALLTIME_H
#define COREAPP_WALLTIME_H

#include <stdint.h>
#include "handover.h"

extern "C" void rtc_adopt_local_time(int year, int mon, int day, int h, int m, int s);

// A valid block that carries a time sets the clock; true when it did.
static inline bool walltime_adopt(const Handover *h)
{
    int y, mo, d, hh, mi, s;
    if (!h || !handover_valid(h) || !handover_time(h, &y, &mo, &d, &hh, &mi, &s)) {
        return false;
    }
    rtc_adopt_local_time(y, mo, d, hh, mi, s);
    return true;
}

// FatFs timestamp (bits 31:25 year since 1980, 24:21 month, 20:16 day, 15:11 hour, 10:5 minute, 4:0 seconds / 2) to t = {year-2000, month, day,
// hour, minute, second}. False for a clock that was never set (reads 1980) or an impossible date.
static inline bool fattime_to_wall(uint32_t f, uint8_t t[6])
{
    unsigned year = ((f >> 25) & 0x7F) + 1980;
    if (year < 2020) {
        return false;
    }
    t[0] = (uint8_t)(year - 2000);
    t[1] = (uint8_t)((f >> 21) & 0x0F);
    t[2] = (uint8_t)((f >> 16) & 0x1F);
    t[3] = (uint8_t)((f >> 11) & 0x1F);
    t[4] = (uint8_t)((f >> 5) & 0x3F);
    t[5] = (uint8_t)((f & 0x1F) * 2);
    return t[1] >= 1 && t[1] <= 12 && t[2] >= 1;
}

#endif
