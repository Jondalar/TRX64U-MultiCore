// T191: the host's time through the hand-over page into a core application's clock (CoreIo::wall_time of coreapp/src/hw.cc).
// Real handover.cc, rtc_dummy.cc and coreapp/src/walltime.h; ntpstub/ stands in for FreeRTOS. The chain under test is what the
// application does at start: copy the page, walltime_adopt(), and later wall_time() = fattime_to_wall(get_fattime()).
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "rtc_dummy.h"
#include "task.h"
#include "handover.h"
#include "walltime.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

TickType_t g_ticks;
uint8_t trx_io_caps(void) { return 1; }      // the TRX64 bitstream: no seconds counter, the clock runs in software

// what hw.cc's wall_time does
static bool wall(uint8_t t[6]) { return fattime_to_wall(get_fattime(), t); }

int main()
{
    Handover h;
    uint8_t t[6];

    // S9 (the board finding): a core application that was never handed a time reads 1980, so wall_time is false and the Amiga loads the fallback date
    handover_fill(&h, "/Usb0/cores/amiga1200.cor", "Amiga 1200", "1", 0x12345678u, 0, "amiga1200");
    CHECK(handover_valid(&h));
    CHECK(!walltime_adopt(&h));                     // a block without a time sets nothing
    CHECK(!wall(t));

    // the host's time in the block: the clock reads it, and keeps running
    handover_set_time(&h, 2026, 11, 7, 18, 30, 15);
    CHECK(handover_valid(&h));                      // an older reader still accepts the block
    CHECK(walltime_adopt(&h));
    CHECK(wall(t));
    CHECK(t[0] == 26 && t[1] == 11 && t[2] == 7 && t[3] == 18 && t[4] == 30 && t[5] == 14);   // FAT stamps count seconds / 2
    g_ticks += 90 * 1000;                           // 90 s later
    CHECK(wall(t));
    CHECK(t[0] == 26 && t[1] == 11 && t[2] == 7 && t[3] == 18 && t[4] == 31 && t[5] == 44);

    // noise and stale bytes behind the id are no time: a flipped byte, a block of another core, an old host's block (nothing behind the id)
    for (unsigned i = offsetof(Handover, time_sum); i < offsetof(Handover, config_sum); i++) {      // T198: the config tail behind the time has its own check
        Handover g = h;
        ((uint8_t *)&g)[i] ^= 0x04;
        int a, b, c, d, e, f;
        CHECK(!handover_time(&g, &a, &b, &c, &d, &e, &f));
    }
    Handover other = h;                             // the time of one start laid over the block of another
    strcpy(other.core_name, "Amiga 500");
    other.sum = handover_sum(&other);
    int a, b, c, d, e, f;
    CHECK(!handover_time(&other, &a, &b, &c, &d, &e, &f));
    Handover old;
    handover_fill(&old, "/Usb0/cores/amiga1200.cor", "Amiga 1200", "1", 0x12345678u, 0, "amiga1200");
    memset((uint8_t *)&old + offsetof(Handover, time_sum), 0xFF, sizeof(Handover) - offsetof(Handover, time_sum));
    CHECK(!handover_time(&old, &a, &b, &c, &d, &e, &f));

    // an implausible date with a matching check is refused (the check is not the only gate)
    Handover bad;
    handover_fill(&bad, "p", "n", "v", 1);
    handover_set_time(&bad, 1999, 1, 1, 0, 0, 0);
    CHECK(!handover_time(&bad, &a, &b, &c, &d, &e, &f));
    handover_set_time(&bad, 2026, 13, 1, 0, 0, 0);
    CHECK(!handover_time(&bad, &a, &b, &c, &d, &e, &f));
    handover_set_time(&bad, 2026, 1, 1, 24, 0, 0);
    CHECK(!handover_time(&bad, &a, &b, &c, &d, &e, &f));

    // an invalid page (noise) hands over nothing, whatever its time words say
    Handover z;
    memset(&z, 0xFF, sizeof(z));
    CHECK(!walltime_adopt(&z));
    CHECK(!walltime_adopt(0));
    Handover n = h;                                 // a good time in a page whose magic is damaged: the page is noise, the time is not taken
    n.magic ^= 1;
    CHECK(!walltime_adopt(&n));
    n = h;
    n.sum ^= 1;                                     // (the time check is mixed with sum, so this one also fails it; the magic case above is the page test)
    CHECK(!walltime_adopt(&n));

    // fattime_to_wall itself: never-set clock and impossible dates
    CHECK(!fattime_to_wall(0, t));
    CHECK(!fattime_to_wall((46u << 25) | (0u << 21) | (1u << 16), t));   // month 0
    CHECK(fattime_to_wall((46u << 25) | (1u << 21) | (1u << 16), t) && t[0] == 26);

    if (fails) {
        printf("FAIL %d check(s)\n", fails);
        return 1;
    }
    printf("PASS walltime\n");
    return 0;
}
