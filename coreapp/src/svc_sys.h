// Base 3.3, members added to `SvcSys` (svctab.h; T239.3 section 5, boundary rows 4.3, 11.6).
//
// They are appended to SvcSys after `machine_handover`, in this order, with the names below, when 3.3 is built (svctab.h is changed then; another branch
// holds it until 3.2 is in). SvcSys::size covers them, so a library tests `U2P_HAS`-style (api->sys->size) before it calls one. SvcSysAdd is the same
// list as a struct of its own, so this header compiles alone and the TRX64-Ultimate libraries have the signatures now.
//
// A call without a context argument acts on the running core. No call takes a CoreCtx.
#ifndef COREAPP_SVC_SYS_H
#define COREAPP_SVC_SYS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SvcSysAdd {
    // The 1 ms hardware counter of the ITU (getMsTimer, itu.h:122): free running, uint16_t, so it wraps every 65.536 s. Use differences: take a span as
    // `(uint16_t)(now - then)`, valid for spans under 65 s; never compare two readings with < or >. It is no replacement for `sys.ms` (5 ms ticks, 32 bit). Result: the
    // count. Task: any.
    uint16_t (*ms_timer)(void);
    // Gives the processor to a ready task of the same priority and returns at once when none is ready (taskYIELD). `sleep_ms(0)` is no yield: it sleeps one
    // tick of 5 ms. Task: any, not between sync.enter and sync.leave.
    void (*yield)(void);
    // A busy wait of `us` microseconds (CoreIo::delay_us; the unit is 10 us, rounded down, below 10 it returns at once). The task does not sleep and no other
    // task of its priority runs meanwhile. Meant for a few microseconds up to a few milliseconds (the SID chips' handshakes, SIO timing); a longer wait is
    // sleep_ms. Task: any.
    void (*delay_us)(uint32_t us);
    // The wall clock of the board: t = { year 0-99 (2000 + t[0]), month 1-12, day 1-31, hour, minute, second }. False when the board has no clock or it was never
    // set (the year reads before 2020); t is then not written. Replaces get_fattime and rtc_time_valid. Task: any.
    bool (*wall_time)(uint8_t t[6]);
    // The version text of the application image that runs (the stamp field "TRXVSTAMP" of the image, the string app_version_string() gives: what version.toml
    // of the application and the commit make), and the short git hash of its commit (app_version_hash). Both are zero terminated, owned by the base, valid
    // for the life of the application and never null. A library writes them into what it saves ("appVersion" of a .c64re). Task: any.
    // app_git is kept beside app_version for the same reason: build identity of the image, which no library can read; the ring and the monitor both read it.
    const char *(*app_version)(void);
    const char *(*app_git)(void);
} SvcSysAdd;

#ifdef __cplusplus
}
#endif

#endif
