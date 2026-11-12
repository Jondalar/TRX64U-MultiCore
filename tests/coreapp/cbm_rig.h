// What the VIC-20 and the 264 benches share (T83 / T90 on the generic application): a model of a CBM 8-bit machine behind CoreIo as far as the
// application can see it - 64 KB of RAM, a reset that keeps the RAM, a KERNAL that needs some time after the reset is released before BASIC's
// start pointer is set and some more before the editor reads the keyboard buffer, a status register whose bit 1 is "held" while C64_STOP is on -
// and the helpers that drive an operation the way the menu does (the machine held while it runs, polled after the menu closes).
//
// The model is the bench's reading of the machines, not the machines: the times are made up (the KERNAL's real ones are the first thing a
// board run measures), what it does check is the ORDER the application keeps: no keys typed before the editor is up, no expansion register
// written while the machine runs.
#ifndef TESTS_COREAPP_CBM_RIG_H
#define TESTS_COREAPP_CBM_RIG_H

#include <string>
#include <vector>
#include "check.h"
#include "fakes.h"
#include "boot.h"
#include "cores.h"
#include "ops_glue.h"
#include "freeze.h"

struct CbmIo : FakeIo {
    uint32_t boot_ms = 1500;              // reset released -> the KERNAL has set BASIC's start pointer
    uint32_t editor_ms = 900;             // ... and the editor reads the keyboard buffer from here on
    uint16_t stale_txttab = 0x1001;       // what the RAM holds from the run before, until the KERNAL's memory test wipes the zero page
    uint32_t wipe_ms = 20;
    bool booted = false, ready = false, released = false, wiped = false;
    uint64_t ran_us = 0;
    bool typed_early = false;             // keys were put into the buffer before the editor was up
    int bad_writes = 0;                   // writes to the registers listed in `taken_in_reset` while the machine ran
    std::vector<uint32_t> taken_in_reset;
    uint32_t status_reg = 0, kbd_count = 0;

    CbmIo(uint32_t status, uint32_t kbd_cnt) : status_reg(status), kbd_count(kbd_cnt)
    {
        ram[0x2B] = (uint8_t)(stale_txttab & 0xFF);
        ram[0x2C] = (uint8_t)(stale_txttab >> 8);
    }
    virtual uint16_t boot_txttab() = 0;                 // BASIC's start the KERNAL leaves, from the registers latched at the release
    virtual void latch() { }

    void on_wr(uint32_t reg, uint8_t)
    {
        for (uint32_t r : taken_in_reset) {
            if (r == reg && !in_reset) bad_writes++;
        }
    }
    uint8_t rd(uint32_t reg)
    {
        if (reg == status_reg) return (uint8_t)((in_reset ? 1 : 0) | (stopped ? 2 : 0));
        return FakeIo::rd(reg);
    }
    void mode_reset(bool on)
    {
        FakeIo::mode_reset(on);
        if (on) {
            released = booted = ready = wiped = false;
        } else {
            released = true;
            ran_us = 0;
            latch();
        }
    }
    void ram_write(uint32_t a, const void *p, uint32_t n)
    {
        if (!ready && a <= kbd_count && kbd_count < a + n) typed_early = true;
        FakeIo::ram_write(a, p, n);
    }
    void advance(uint32_t u)
    {
        FakeIo::advance(u);
        if (released && !stopped) {
            ran_us += u;
            if (!wiped && ran_us >= (uint64_t)wipe_ms * 1000) {
                wiped = true;
                ram[0x2B] = ram[0x2C] = 0;
            }
            if (!booted && ran_us >= (uint64_t)boot_ms * 1000) {
                booted = true;
                uint16_t t = boot_txttab();
                ram[0x2B] = (uint8_t)(t & 0xFF);
                ram[0x2C] = (uint8_t)(t >> 8);
            }
            if (booted && !ready && ran_us >= (uint64_t)(boot_ms + editor_ms) * 1000) {
                ready = true;
                ram[kbd_count] = 0;                     // the editor starts with an empty buffer
            }
        }
    }
};

// A boot that waits for a ROM that never comes would hang a bench whose product source was broken on purpose; this one says so and stops.
struct BoundedUi : BootUi {
    int n = 0;
    std::vector<std::string> msgs;
    void message(const char *, const char *m, bool) { msgs.push_back(m); }
    void clear() { }
    void tick()
    {
        if (++n > 300) {
            printf("FAIL boot_start was still waiting for a ROM after %d passes\n", n);
            exit(1);
        }
    }
};

typedef std::vector<uint8_t> Bytes;

static inline Bytes prg(uint16_t load, const Bytes &body)
{
    Bytes v;
    v.push_back((uint8_t)(load & 0xFF));
    v.push_back((uint8_t)(load >> 8));
    v.insert(v.end(), body.begin(), body.end());
    return v;
}

// "10 PRINT", "20 PRINT" linked for a program that starts at `at`
static inline Bytes basic_prg(uint16_t at)
{
    uint16_t l2 = (uint16_t)(at + 6), l3 = (uint16_t)(at + 12);
    Bytes b = { (uint8_t)(l2 & 255), (uint8_t)(l2 >> 8), 10, 0, 0x99, 0, (uint8_t)(l3 & 255), (uint8_t)(l3 >> 8), 20, 0, 0x99, 0, 0, 0 };
    return prg(at, b);
}

static inline const char *ev_first(const std::vector<std::string> &e) { return e.empty() ? "(none)" : e.front().c_str(); }
static inline const char *ev_last(const std::vector<std::string> &e) { return e.empty() ? "(none)" : e.back().c_str(); }

// The menu holds the machine (C64_STOP) while an operation of it runs and lets it go when it closes; the service the descriptor names is polled
// from then on, every 50 ms of the machine's time, until the job has been carried out.
struct CbmMenu {
    CoreCtx *c;
    CbmIo *io;
    const CoreDesc *d;
    const char *service, *pending;
    void begin() { io->stop(true); }
    void closed(int max_ms = 15000)
    {
        io->stop(false);
        for (int t = 0; t < max_ms && ops_state(c, pending); t += 50) {
            io->delay_ms(50);
            OpResult x;
            ops_call(c, service, 0, 0, &x);
        }
    }
    // a file action of the descriptor, called the way the browser calls it, with the machine held
    int action(const char *ext, int idx, const char *path, OpResult *r)
    {
        for (int i = 0; i < d->nfiletypes; i++) {
            if (!strcmp(d->filetypes[i].ext, ext)) {
                const FileAction &a = d->filetypes[i].actions[idx];
                begin();
                return ops_run_bound(c, a.op, a.bind, a.nbind, path, r);
            }
        }
        printf("FAIL no file type %s\n", ext);
        g_fails++;
        return -1;
    }
};

// Freeze against the model: the descriptor's PAUSED register and bit must be what the core raises when C64_STOP is on.
struct CbmFreezeOps : FreezeOps {
    CbmIo *io;
    const CoreDesc *d;
    std::vector<std::string> logs;
    CbmFreezeOps(CbmIo *i, const CoreDesc *desc) : io(i), d(desc) { }
    void stop(bool on) { io->stop(on); }
    uint8_t status() { return freeze_status(io, d->freeze); }
    void layer_to_menu() { }
    void layer_to_core() { }
    bool wait_keys_idle(uint32_t) { return true; }
    uint32_t now_ms() { return io->now_ms(); }
    void delay_ms(uint32_t ms) { io->delay_ms(ms); }
    void log(const char *t) { logs.push_back(t); }
};

#endif
