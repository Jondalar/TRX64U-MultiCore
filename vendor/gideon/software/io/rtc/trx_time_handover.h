#ifndef TRX_TIME_HANDOVER_H
#define TRX_TIME_HANDOVER_H
// TRX64 Ultimate: the app hands its wall-clock time to the updater it starts with "Run Update"
// (docs/specs/T49-update-packages.md). Four words at a fixed DDR address inside the updater's
// window (0x03000000-0x03FFFFFF, update_trx/linker.x) that nothing there owns: the loaded image
// ends below 0x03600000, .data/.bss/heap start there and grow up from it, the stack starts at
// 0x03FFFFFC and grows down. The updater reads and clears them first thing in ultimate_main.
// The updater has no network and no RTC of its own on our bitstream.
#include <stdint.h>

#define TRX_TIME_HANDOVER_ADDR  0x03F00000u
#define TRX_TIME_HANDOVER_MAGIC 0x54585254u     // "TRXT"
#define TRX_TIME_HANDOVER_SALT  0x5A17C0DEu

struct trx_time_handover {
    uint32_t magic;
    uint32_t date;      // calendar year << 16 | month << 8 | day
    uint32_t time;      // hour << 16 | minute << 8 | second
    uint32_t check;     // magic ^ date ^ time ^ salt
};

// local time as the app shows it; year is the calendar year (2026), not an offset
static inline void trx_time_handover_put(volatile struct trx_time_handover *p, int year, int mon, int day, int h, int m, int s)
{
    uint32_t d = ((uint32_t)year << 16) | ((uint32_t)mon << 8) | (uint32_t)day;
    uint32_t t = ((uint32_t)h << 16) | ((uint32_t)m << 8) | (uint32_t)s;
    p->magic = TRX_TIME_HANDOVER_MAGIC;
    p->date = d;
    p->time = t;
    p->check = TRX_TIME_HANDOVER_MAGIC ^ d ^ t ^ TRX_TIME_HANDOVER_SALT;
}

// no valid time: nothing a stale earlier record could pass for
static inline void trx_time_handover_clear(volatile struct trx_time_handover *p)
{
    p->magic = 0;
    p->date = 0;
    p->time = 0;
    p->check = 0;
}

// true and the fields when the record is intact and the date is plausible; the record is cleared either way
static inline bool trx_time_handover_take(volatile struct trx_time_handover *p, int *year, int *mon, int *day, int *h, int *m, int *s)
{
    uint32_t mg = p->magic, d = p->date, t = p->time, c = p->check;
    trx_time_handover_clear(p);
    if (mg != TRX_TIME_HANDOVER_MAGIC || c != (mg ^ d ^ t ^ TRX_TIME_HANDOVER_SALT)) {
        return false;
    }
    *year = (int)(d >> 16);
    *mon = (int)((d >> 8) & 0xFF);
    *day = (int)(d & 0xFF);
    *h = (int)(t >> 16);
    *m = (int)((t >> 8) & 0xFF);
    *s = (int)(t & 0xFF);
    return *year >= 2020 && *year <= 2099 && *mon >= 1 && *mon <= 12 && *day >= 1 && *day <= 31 && *h < 24 && *m < 60 && *s < 60;
}

#ifdef __cplusplus
extern "C" {
#endif
// rtc_dummy.cc (the TRX64 targets): has the clock been set (SNTP, the menu, or adopted from the app)?
bool rtc_time_valid(void);
// rtc_dummy.cc: take a local time (calendar year) as the clock, software mode on every bitstream
void rtc_adopt_local_time(int year, int mon, int day, int h, int m, int s);
#ifdef __cplusplus
}
#endif
#endif
