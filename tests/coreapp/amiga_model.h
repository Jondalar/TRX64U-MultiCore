// A model of the Amiga core's register block for the native benches (T104 5): what the application can see of cores/amiga/rtl/trx/amiga_regs.v.
// The configuration bytes and the mount registers are plain registers (FakeIo); on top of that the model has the dirty-track log of the floppies
// (a FIFO whose head moves on only after a settle time - the head crosses two synchronisers), and the hard disk mailbox.
#ifndef TESTS_COREAPP_AMIGA_MODEL_H
#define TESTS_COREAPP_AMIGA_MODEL_H

#include <deque>
#include "fakes.h"
#include "ctx.h"
#include "cores.h"
#include "amiga_regs.h"

// the Amiga record a test of "the Amiga" runs on: the A1200's in the A1200's own app (run.sh a12app, TEST_APP_A1200), the A500's in the shared app (which has no A1200 since T213.3)
#ifdef TEST_APP_A1200
#define AM_REC amiga1200_desc
#else
#define AM_REC amiga_desc
#endif

struct AmigaIo : FakeIo {
    struct Entry { uint8_t track, drive; };
    std::deque<Entry> dlog;
    bool overflow = false;
    uint64_t settle_until = 0;             // a pop was seen: until then the head registers still show the old head
    Entry stale_head = { 0, 0 };
    bool stale_nonempty = false;
    int pops = 0, lost = 0;                // pops that removed an entry; pops swallowed because the head had not moved on
    int mount_writes[4] = { 0, 0, 0, 0 };
    // T141 RAWKEY (the A1200 build): amiga_input's queue of 16 events; one leaves per 1.2 ms; a read says in b0 that 8 more fit
    std::vector<uint8_t> rawkeys;          // every event the application wrote, in order
    int rk_depth = 0, rk_max = 0, rk_lost = 0;
    uint64_t rk_acc_us = 0;
    void advance(uint32_t u)
    {
        FakeIo::advance(u);
        rk_acc_us += u;
        while (rk_depth > 0 && rk_acc_us >= 1200) {
            rk_depth--;
            rk_acc_us -= 1200;
        }
        if (rk_depth == 0) {
            rk_acc_us = 0;
        }
    }
    // T191 cores/amiga/rtl/trx/amiga_rtc.v as the application sees it: a nibble file loaded through VAL and IDX, the loader hold, the LOADED flag
    uint8_t rtc_nib[13] = { 0 }, rtc_val = 0;
    bool rtc_lhold = false, rtc_loaded = false;
    int rtc_commits = 0, rtc_unheld = 0;           // IDX writes; nibble writes made while the loader hold was not set
    bool have_time = false;
    uint8_t tm[6] = { 26, 11, 6, 14, 5, 9 };       // 2026-11-06 14:05:09 (a Friday)
    bool wall_time(uint8_t t[6]) { if (have_time) memcpy(t, tm, 6); return have_time; }
    std::vector<uint32_t> rdlog;           // every register the application read (T104 5.15: the A500 test checks them against its gateware)

    void log_track(int drive, int track) { dlog.push_back({ (uint8_t)track, (uint8_t)drive }); }
    bool settled() const { return us >= settle_until; }

    // the posted hard disk request
    void post(int unit, bool write, uint32_t lba)
    {
        regs[AM_MBOX_STATUS] = (uint8_t)(1 | (write ? 2 : 0) | (unit << 2));
        for (int i = 0; i < 4; i++) {
            regs[AM_MBOX_LBA + i] = (uint8_t)(lba >> (8 * i));
        }
    }
    bool pending() const { return regs[AM_MBOX_STATUS] & 1; }

    uint8_t rd(uint32_t reg)
    {
        rdlog.push_back(reg);
        if (reg == AM_RTC_STAT) {
            return (uint8_t)(rtc_loaded ? 1 : 0);
        }
        if (reg == AM_RAWKEY) {
            return (uint8_t)(rk_depth <= 8 ? 1 : 0);
        }
        if (reg == AM_DLOG_STATUS) {
            bool ne = settled() ? !dlog.empty() : stale_nonempty;
            return (uint8_t)((ne ? 1 : 0) | (overflow ? 2 : 0));
        }
        if (reg == AM_DLOG_TRACK || reg == AM_DLOG_DRIVE) {
            Entry h = settled() ? (dlog.empty() ? Entry{ 0, 0 } : dlog.front()) : stale_head;
            return reg == AM_DLOG_TRACK ? h.track : h.drive;
        }
        return FakeIo::rd(reg);
    }

    void on_wr(uint32_t reg, uint8_t v)
    {
        if (reg == AM_RTC_VAL) {
            rtc_val = v & 15;
            return;
        }
        if (reg == AM_RTC_IDX) {
            rtc_commits++;
            uint8_t i = v & 15;
            if (i < 13) {
                if (!rtc_lhold) rtc_unheld++;
                rtc_nib[i] = rtc_val;
            } else if (i == 14) {
                rtc_lhold = rtc_val & 1;
            } else if (i == 15) {
                rtc_loaded = true;
            }
            return;
        }
        if (reg == AM_RAWKEY) {
            rawkeys.push_back(v);
            if (rk_depth >= 16) {
                rk_lost++;                 // the gateware drops an event that does not fit
            } else {
                rk_depth++;
            }
            if (rk_depth > rk_max) {
                rk_max = rk_depth;
            }
            return;
        }
        if (reg == AM_DLOG_POP) {
            if (!settled()) {
                lost++;                    // a pop while the head had not moved on: the entry that would have been next is lost
                if (!dlog.empty()) { dlog.pop_front(); }
            } else if (!dlog.empty()) {
                stale_head = dlog.front();
                stale_nonempty = true;
                dlog.pop_front();
                pops++;
            }
            settle_until = us + 8;         // the head moves on through two synchronisers
        } else if (reg == AM_DLOG_STATUS && (v & 2)) {
            overflow = false;
        } else if (reg == AM_MBOX_DONE) {
            regs[AM_MBOX_STATUS] &= (uint8_t)~1;
        } else if (reg >= AM_FLOPPY_MOUNT && reg < AM_FLOPPY_MOUNT + 0x40 && ((reg - AM_FLOPPY_MOUNT) % 0x10) == 0) {
            mount_writes[(reg - AM_FLOPPY_MOUNT) / 0x10]++;
        }
    }

    // the Amiga's pages as a core application sees them
    uint32_t window(uint32_t off) const { return off; }
};

// A context with the real descriptor, in-memory settings and no hand-over.
struct AmigaRig {
    AmigaIo io;
    FakeFs *fs;
    CoreCtx c;
    Settings *st = 0;
    PxRec px;
    explicit AmigaRig(FakeFs *f, const CoreDesc *d = &AM_REC) : fs(f)
    {
        memset(&c, 0, sizeof(c));
        c.desc = d;
        c.io = &io;
        c.fs = fs;
        st = new Settings(d->settings, d->nsettings, d->store, fs, "", 0);
        st->load();
        c.settings = st;
    }
};

#endif
