// Host test of the host app's clock (T164 D1): SNTP time -> software clock -> FAT time stamp, in the configured zone.
// Built from the real host-app/software/io/rtc/rtc_dummy.cc and network/sntp_time.cc; ntpstub/ replaces FreeRTOS, lwIP and the settings store.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "rtc_dummy.h"
#include "task.h"
#include "trx_time_handover.h"
#include "network_config.h"
#include "sntp_time.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

TickType_t g_ticks;
uint8_t trx_io_caps(void) { return 1; }      // the TRX64 bitstream: no seconds counter, the clock runs in software
extern "C" void sntp_time_received(uint32_t sec);
static ConfigStore store;
NetworkConfig networkConfig;
static int n_init, n_stop;
static const char *names[3];
extern "C" {
void sntp_stop(void) { n_stop++; }
void sntp_init(void) { n_init++; }
void sntp_setservername(unsigned char i, const char *n) { names[i] = n; }
}

static uint32_t fat(int y, int M, int D, int h, int m, int s) { return ((y - 1980) << 25) | (M << 21) | (D << 16) | (h << 11) | (m << 5) | (s >> 1); }
static void apply(const char *posix) { networkConfig.posix = posix; start_sntp(); }

int main()
{
    networkConfig.cfg = &store;
    const char *CET = "CEST-1CET,M3.2.0/2:00:00,M11.1.0/2:00:00";
    const char *PHX = "MST7";

    // before any SNTP answer: the clock is not set; FAT time is 1980-01-01 00:00:00, not a wrapped year
    CHECK(!rtc_time_valid());
    CHECK(get_fattime() == fat(1980, 1, 1, 0, 0, 0));

    // SNTP on: servers 1-3 handed to lwIP, zone exported as TZ
    apply(CET);
    CHECK(n_init == 1 && n_stop == 1);
    CHECK(names[0] && !strcmp(names[0], "s1"));
    CHECK(names[1] && !strcmp(names[1], "s2"));
    CHECK(names[2] && !strcmp(names[2], "s3"));
    CHECK(getenv("TZ") && !strcmp(getenv("TZ"), CET));

    // 2026-07-15 12:00:00 UTC in summer time (+2) = 14:00:00
    g_ticks = 5000;
    sntp_time_received(1784116800);
    CHECK(rtc_time_valid());
    CHECK(get_fattime() == fat(2026, 7, 15, 14, 0, 0));
    // ticks since the answer advance the clock (2 s granularity of FAT)
    g_ticks += 61000;
    CHECK(get_fattime() == fat(2026, 7, 15, 14, 1, 0));
    // winter: 2026-12-15 12:00:00 UTC = 13:00:00 CET
    sntp_time_received(1797336000u);
    CHECK(get_fattime() == fat(2026, 12, 15, 13, 0, 0));
    // no DST zone: Phoenix is UTC-7 all year
    apply(PHX);
    CHECK(get_fattime() == fat(2026, 12, 15, 5, 0, 0));
    sntp_time_received(1784116800);
    CHECK(get_fattime() == fat(2026, 7, 15, 5, 0, 0));
    // a changed zone moves the stamp without a new SNTP answer
    apply(CET);
    CHECK(get_fattime() == fat(2026, 7, 15, 14, 0, 0));

    // SNTP off: nothing is started, TZ stays as it was
    store.en = 0;
    int i0 = n_init;
    apply("JST-9");
    CHECK(n_init == i0);
    CHECK(getenv("TZ") && !strcmp(getenv("TZ"), CET));

    printf(fails ? "test_ntp: %d FAILED\n" : "test_ntp: all passed\n", fails);
    return fails ? 1 : 0;
}
