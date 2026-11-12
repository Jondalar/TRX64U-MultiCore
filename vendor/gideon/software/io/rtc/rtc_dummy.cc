#include "rtc_dummy.h"
#include "rtc_epoch.h"
#include "trx_time_handover.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "FreeRTOS.h"
#include "task.h"
// The TRX64 bitstream has no seconds counter at RTC_TIMER_BASE (it reads 0): there
// the time is the last value set (SNTP or the menu) plus the FreeRTOS ticks since.
// Gideon's bitstreams keep using their counter. trx_io_caps() (trx_io.cc) is weak
// here: the updater and factory test do not link trx_io.cc.
uint8_t trx_io_caps(void) __attribute__((weak));

static bool sw_forced; // the updater: handed a time by the app, so software mode on a bitstream it cannot probe

static bool trx_bitstream(void)
{
    return sw_forced || (trx_io_caps && trx_io_caps());
}
static uint32_t sw_base_sec;
static TickType_t sw_base_tick;

static uint32_t rtc_seconds(void)
{
    if (!trx_bitstream()) {
        return RTC_TIMER_SECONDS;
    }
    TickType_t now = xTaskGetTickCount();
    uint32_t whole = (uint32_t)(now - sw_base_tick) / configTICK_RATE_HZ;
    sw_base_sec += whole; // move the base on, so the tick count never wraps past it
    sw_base_tick += (TickType_t)(whole * configTICK_RATE_HZ);
    return sw_base_sec;
}

static void rtc_set_seconds(uint32_t seconds)
{
    if (!trx_bitstream()) {
        RTC_TIMER_SECONDS = seconds;
        return;
    }
    sw_base_sec = seconds;
    sw_base_tick = xTaskGetTickCount();
}

// Set by SNTP, the menu or the handover; the unset clock reads 1970 (software mode) or 0 (hardware counter that
// does not run). Anything before 2020 is not a time somebody set.
extern "C" bool rtc_time_valid(void)
{
    return rtc_seconds() >= 1577836800u;
}

const char *month_strings_short[]={ "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

const char *month_strings_long[] = { "", "January", "February", "March", "April", "May", "June",
                                    "July", "August", "September", "October", "November", "December" };

const char *weekday_strings[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

Rtc::Rtc()
{
}

Rtc::~Rtc()
{
}

void Rtc::get_time(int &y, int &M, int &D, int &wd, int &h, int &m, int &s)
{
    uint32_t sec = rtc_seconds();

    time_t now;
    struct tm timeinfo;

    now = sec;
    localtime_r(&now, &timeinfo);

    y = timeinfo.tm_year - 80;
    M = timeinfo.tm_mon + 1;
    D = timeinfo.tm_mday;
    wd = timeinfo.tm_wday;
    h = timeinfo.tm_hour;
    m = timeinfo.tm_min;
    s = timeinfo.tm_sec;
}

const char * Rtc::get_time_string(char *dest, int len)
{
    if (len < 9)
        return "";
    int y, M, D, wd, h, m, s;
    get_time(y, M, D, wd, h, m, s);
    sprintf(dest, "%02d:%02d:%02d", h, m, s);
    return dest;
}

const char * Rtc::get_date_string(char *dest, int len)
{
    if (len < 14)
        return "";
    int y, M, D, wd, h, m, s;
    get_time(y, M, D, wd, h, m, s);
    sprintf(dest, "%s %2d, %4d", month_strings_short[M], D, 1980 + y);
    return dest;
}

const char * Rtc::get_long_date(char *dest, int len)
{
    // Wednesday September 30, 2009
    if (len < 29)
        return "";
    int y, M, D, wd, h, m, s;
    get_time(y, M, D, wd, h, m, s);
    sprintf(dest, "%s %s %2d, %4d", weekday_strings[wd], month_strings_long[M], D, 1980 + y);
    return dest;
}

uint32_t Rtc::get_fat_time(void)
{
    int y, M, D, wd, h, m, s;
    get_time(y, M, D, wd, h, m, s);

    uint32_t result = 0;
    result |= (y << 25);
    result |= (M << 21);
    result |= (D << 16);
    result |= (h << 11);
    result |= (m << 5);
    result |= (s >> 1);

    return result;
}

int Rtc::get_correction(void)
{
    return 0;
}

void Rtc::set_time_in_chip(int corr_ppm, int y, int M, int D, int wd, int h, int m, int s)
{
}

void Rtc::set_time(int y, int M, int D, int wd, int h, int m, int s)
{
    struct tm timeinfo = {};
    timeinfo.tm_isdst = -1; // let mktime decide; it was uninitialised
    timeinfo.tm_year = y + 80;
    timeinfo.tm_mon = M - 1;
    timeinfo.tm_mday = D;
    timeinfo.tm_wday = wd;
    timeinfo.tm_hour = h;
    timeinfo.tm_min = m;
    timeinfo.tm_sec = s;
    time_t seconds = mktime(&timeinfo);
    rtc_set_seconds(seconds);
}

void Rtc::set_time_utc(int seconds)
{
    rtc_set_seconds(seconds);
}

Rtc rtc; // global

extern "C" void rtc_adopt_local_time(int year, int mon, int day, int h, int m, int s)
{
    sw_forced = true;
    rtc.set_time(year - RTC_EPOCH_YEAR, mon, day, 0, h, m, s);
}

extern "C" uint32_t get_fattime(void) /* 31-25: Year(0-127 org.1980), 24-21: Month(1-12), 20-16: Day(1-31) */
/* 15-11: Hour(0-23), 10-5: Minute(0-59), 4-0: Second(0-29 *2) */
{
    return rtc.get_fat_time();
}

extern "C" void get_current_time(int& wd, int& year, int& month, int& day, int& hour, int& min, int& sec)
{
    rtc.get_time(year, month, day, wd, hour, min, sec);
    year += RTC_EPOCH_YEAR; // get_time() counts years from the epoch, callers do not
}
