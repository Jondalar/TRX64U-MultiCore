// Timewarp as operations and state values (T104.2 UI, Alex 2026-10-28: everything declarative). The engine is ring.cc (TRX64-Ultimate's frame ring driver); this file is what
// the menu says to it: start and stop, scrub a frame or a second, back to live, mark, resume here, write and load a .c64re. menu.toml F2 draws it (screens.cc layout
// "timeline"), the keys of that layout call these operations (menuctl.cc).
#include "ringops.h"
#include "trx64u_ops.h"
#include "trx_ring.h"
#include "trx_c64re.h"
#include "ops_glue.h"
#include "c64_cart.h"
#include "cbm1541.h"
#include "d1541_drive.h"
#include "filemanager.h"
#include "regset.h"
#include "settings.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

static int g_glyph;                             // the bar's glyph after the last move: 0 pause, 1 back, 2 forward, 3 oldest, 4 newest
static int g_sel = -1;                          // the timeline entry the paused machine shows; -1 the live end
#define MARKS 16
static uint32_t g_mark[MARKS];                  // the seq of a marked frame
static int g_nmark;

static int newest(void) { return trx_ring_count() - 1; }
static int shown(void) { return g_sel < 0 ? newest() : g_sel; }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static int go_to(int idx, char *msg, int msglen)
{
    int n = trx_ring_count();
    if (n <= 0) {
        snprintf(msg, msglen, "The ring holds no frame yet");
        return OPE_FAILED;
    }
    idx = clampi(idx, 0, n - 1);
    int dir = idx >= shown() ? 1 : -1;
    int k = idx;
    while (k >= 0 && k < n && !trx_ring_reachable(k)) {          // the nearest frame a restore can go to, in the direction asked
        k += dir;
    }
    if (k < 0 || k >= n) {
        k = idx;
        while (k >= 0 && k < n && !trx_ring_reachable(k)) {
            k -= dir;
        }
        if (k < 0 || k >= n) {
            snprintf(msg, msglen, "No frame the ring can go back to");
            return OPE_FAILED;
        }
    }
    TrxRestoreStats st;
    int err = trx_ring_scrub_show(k, &st);
    if (err) {
        snprintf(msg, msglen, "%s", trx_ring_error(err));
        return OPE_FAILED;
    }
    g_glyph = k == shown() ? g_glyph : (dir < 0 ? 1 : 2);
    if (k >= n - 1) {
        g_glyph = 4;
    } else if (k == 0) {
        g_glyph = 3;
    }
    g_sel = k;
    trx_ring_set_pending(k);
    return OPE_OK;
}

static int op_step(CoreCtx *, const OpArgs &a, OpOut &o) { return go_to(shown() + a.i(0), o.msg, o.msglen); }
static int op_seconds(CoreCtx *, const OpArgs &a, OpOut &o) { return go_to(shown() + a.i(0) * trx_ring_fps(), o.msg, o.msglen); }

static int op_oldest(CoreCtx *, const OpArgs &, OpOut &o) { return go_to(0, o.msg, o.msglen); }

static int op_live(CoreCtx *, const OpArgs &, OpOut &o)
{
    if (trx_ring_count() <= 0) {
        snprintf(o.msg, o.msglen, "The ring holds no frame yet");
        return OPE_FAILED;
    }
    if (g_sel < 0) {
        return OPE_OK;
    }
    int rc = go_to(newest(), o.msg, o.msglen);
    g_sel = -1;
    g_glyph = 4;
    trx_ring_set_pending(-1);                                      // the unfreeze goes on from the scrub's state to the newest frame
    return rc;
}

// RUN/STOP and the left arrow: the menu closes and the machine goes on from the newest frame (a frame shown is not a place to run on from)
static int op_cancel(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    if (g_sel >= 0) {
        op_live(c, a, o);
    }
    g_glyph = 0;
    return OPE_OK;
}

static int op_resume(CoreCtx *, const OpArgs &, OpOut &o)
{
    if (g_sel >= 0) {
        trx_ring_set_pending(g_sel);
        snprintf(o.msg, o.msglen, "Running on from %s", "the shown frame");
    } else {
        snprintf(o.msg, o.msglen, "Live");
    }
    return OPE_OK;
}

// C= O on the Timewarp bar: into the monitor, on the frame shown (FW io/c64/trx_timewarp_ui.cc:284-289, 341-344): the monitor puts the machine on that entry itself
// (trx_ring_land) and holds it there; no rewind is made pending, so the timeline is not cut. The menu goes on to the monitor's screen (menu.screen's "goto").
static int op_monitor(CoreCtx *, const OpArgs &, OpOut &o)
{
    if (trx_ring_count() <= 0) {
        snprintf(o.msg, o.msglen, "The ring holds no frame yet");
        return OPE_FAILED;
    }
    int err = trx_ring_land(shown(), NULL);
    if (err) {
        snprintf(o.msg, o.msglen, "%s", trx_ring_error(err));
        return OPE_FAILED;
    }
    g_sel = -1;
    snprintf(o.msg, o.msglen, "goto F1/monitor");
    return OPE_OK;
}

static int op_start(CoreCtx *, const OpArgs &, OpOut &o)
{
    if (!trx_ring_start()) {
        const char *why = trx_ring_reu_refusal();
        snprintf(o.msg, o.msglen, "%s", why ? why : "This bitstream has no frame ring");
        return OPE_FAILED;
    }
    g_sel = -1;
    g_nmark = 0;
    snprintf(o.msg, o.msglen, "Timewarp is recording");
    return OPE_OK;
}

static int op_stop(CoreCtx *, const OpArgs &, OpOut &o)
{
    trx_ring_stop();
    snprintf(o.msg, o.msglen, "Timewarp stopped: the frames stay until it starts again");
    return OPE_OK;
}

static int op_mark(CoreCtx *, const OpArgs &, OpOut &o)
{
    TrxFrame f;
    if (!trx_ring_frame(shown(), &f)) {
        snprintf(o.msg, o.msglen, "No frame to mark");
        return OPE_FAILED;
    }
    if (g_nmark >= MARKS) {
        for (int i = 1; i < MARKS; i++) {
            g_mark[i - 1] = g_mark[i];
        }
        g_nmark = MARKS - 1;
    }
    g_mark[g_nmark++] = f.seq;
    snprintf(o.msg, o.msglen, "Marker %d set at frame %u", g_nmark, (unsigned)f.marker);
    return OPE_OK;
}

static int op_next_mark(CoreCtx *, const OpArgs &a, OpOut &o)       // value +1: the next marker towards live, -1: the one before
{
    int here = shown(), best = -1;
    for (int i = 0; i < g_nmark; i++) {
        int ix = trx_ring_find(g_mark[i]);
        if (ix < 0) {
            continue;
        }
        if (a.i(0) > 0 && ix > here && (best < 0 || ix < best)) {
            best = ix;
        }
        if (a.i(0) < 0 && ix < here && ix > best) {
            best = ix;
        }
    }
    if (best < 0) {
        snprintf(o.msg, o.msglen, "No marker that way");
        return OPE_FAILED;
    }
    return go_to(best, o.msg, o.msglen);
}

static int op_clear_marks(CoreCtx *, const OpArgs &, OpOut &o)
{
    g_nmark = 0;
    snprintf(o.msg, o.msglen, "Markers cleared");
    return OPE_OK;
}

static void rom_read(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    c64_cart_image_read((CoreCtx *)ctx, off, dst, n);
}

static void rom_write(void *ctx, uint32_t off, const uint8_t *src, uint32_t n)
{
    c64_cart_image_write((CoreCtx *)ctx, off, src, n);
}
static int cart_for_state(void *ctx, const char *path)
{
    char m[96];
    int rc = c64_cart_for_state((CoreCtx *)ctx, path, m, sizeof(m));
    if (rc) {
        printf("[C64RE] the state's cartridge: %s\n", m);
    }
    return rc;
}
// FW build_machine_mode: the file's System Mode for this run only (u64_set_system_mode over set_oneshot: nothing reaches the config); the setting's effect applies it
static int machine_set(void *ctx, int machine)
{
    CoreCtx *c = (CoreCtx *)ctx;
    int ix = c->settings ? c->settings->find("System Mode") : -1;
    if (ix < 0) {
        return 1;
    }
    if (c->settings->value(ix) == machine) {
        return 0;
    }
    c->settings->override_value(ix, machine);
    if (c->settings->value(ix) != machine) {
        return 1;
    }
    regset_apply_setting(c, ix);
    printf("[C64RE] system mode -> %d\n", machine);
    return 0;
}
// FW build_drive: drive A's image goes in before the drive's own state (a mount resets the drive); spec is cbm1541's own, "ro <path>" / "unlinked <path>" / "<path>"
static int drive_mount(void *ctx, const char *spec)
{
    char m[96];
    return cbm1541_slot_mount((CoreCtx *)ctx, spec, m, sizeof(m));
}
static void eeprom_put(void *ctx, const uint8_t *data)           // the GMod2's 93C86 array at +2048 and its valid flag at 0 (c64_cart.cc load_eeprom)
{
    uint8_t one = 1;
    CoreIo *io = ((CoreCtx *)ctx)->io;
    io->eeprom_write(2048, data, 2048);
    io->eeprom_write(0, &one, 1);
}

// The part of the file's source that does not depend on a cartridge: the System Mode, drive A (type, power, unit, image) and the REU setting.
static void src_fill(CoreCtx *c, TrxC64reSrc *src)
{
    src->machine = cfg_value(c, "System Mode", 0);
    src->reu = cfg_value(c, "RAM Expansion Unit", 0);
    src->reu_size = cfg_value(c, "REU Size", 0);
    CbmDrive *d = (CbmDrive *)c->drive;
    if (d && d->type() != DT_UNSET) {
        src->drive_type = d->type() == DT_1581 ? "1581" : "1541";      // no 1571: the drive library refuses it (T103 Q3)
        src->drive_power = d->powered();
        src->drive_unit = d->bus_id();
        src->disk_path = d->image_path();
        src->disk_mode = d->mount_mode() == MM_UNLINKED ? 2 : d->mount_mode() == MM_RO ? 1 : 0;
    }
}

// FW's progress feedback (trx_c64re_dump_ex): the share written goes into the job's percentage (CoreCtx::job_pct, which the menu's progress display reads)
// and the console. The op is one call, so the screen shows it when the menu next draws.
static void dump_progress(int done, int total, void *ctx)
{
    CoreCtx *c = (CoreCtx *)ctx;
    c->job_pct = total ? done * 100 / total : 0;
    if (total && (done == 0 || done == total || (done * 10 / total) != ((done - 1) * 10 / total))) {
        printf("[C64RE] dump %d%s\n", c->job_pct, "%");
    }
}

// FW shows the progress box while trx_c64re_dump_ex writes (trx_timewarp_ui.cc:361-376); the menu draws a progress only for an operation that answers `more` and is called
// again (CoreCtx::job_step, newdisk.cc is the pattern), so with job_step the write runs on a task of its own and the operation answers `more` until it is done, the share
// written going into CoreCtx::job_pct. Without job_step (REST, a hotkey) the write runs on the caller as before.
struct DumpJob {
    volatile int state;                         // 0 idle, 1 the task writes, 2 it is done (result in why / bytes / entries)
    CoreCtx *c;
    bool ring;                                  // .c64rering (the last seconds) instead of one frame
    int idx;
    int secs;
    char path[260];
    uint32_t bytes;
    int entries;
    const char *why;
    TrxC64reSrc src;
    uint8_t eeprom[2048];
};
static DumpJob g_dj;

static void dump_run(DumpJob *j)
{
    if (j->ring) {
        j->why = trx_c64rering_dump_ex(j->path, j->secs, 5, &j->bytes, &j->entries, dump_progress, j->c);
        return;
    }
    trx_c64re_set_source(&j->src);
    j->why = trx_c64re_dump_ex(j->path, j->idx, &j->bytes, dump_progress, j->c);
    trx_c64re_set_source(NULL);
}

static void dump_task(void *arg)
{
    DumpJob *j = (DumpJob *)arg;
    dump_run(j);
    __asm__ volatile ("fence" ::: "memory");
    j->state = 2;
    vTaskDelete(NULL);
}

// T228 R20/R21: what the file says of the machine, as FW's trx_c64re_dump has it: the System Mode, the cartridge image as it stands in DDR2 (streamed into the file), the
// GMod2's EEPROM, the image on drive A and whether drive A is powered (its CPU and VIA state go into the file), the REU setting
static void dump_prepare(CoreCtx *c, DumpJob *j)
{
    memset(&j->src, 0, sizeof(j->src));
    src_fill(c, &j->src);
    if (c64_cart_active()) {
        j->src.rom_bytes = c64_cart_image_bytes();
        j->src.rom_read = rom_read;
        j->src.rom_ctx = c;
        j->src.cart_name = c64_cart_name();
        j->src.cart_src = c64_cart_path();
        j->src.cart_type = c64_cart_crt_type();
        if (j->src.cart_type == 60) {                   // GMod2: the saved game is in the 93C86, outside the ROM window (io.h: the array is at +2048)
            c->io->eeprom_read(2048, j->eeprom, sizeof(j->eeprom));
            j->src.eeprom = j->eeprom;
        }
    }
}

static int dump_op(CoreCtx *c, const OpArgs &a, OpOut &o, bool ring)
{
    DumpJob *j = &g_dj;
    o.more = false;
    if (j->state == 1 && !c->job_step) {
        snprintf(o.msg, o.msglen, "A save is still being written");
        return OPE_FAILED;
    }
    if (j->state == 0) {
        if (!trx_target_path(c, a.s(0), ring ? ".c64rering" : ".c64re", j->path, sizeof(j->path))) {
            snprintf(o.msg, o.msglen, "No medium to save to");
            return OPE_FAILED;
        }
        j->c = c;
        j->ring = ring;
        j->idx = shown();
        j->secs = 1;
        j->bytes = 0;
        j->entries = 0;
        j->why = NULL;
        if (ring) {
            int fps = trx_ring_fps() ? trx_ring_fps() : 50;
            j->secs = trx_ring_count() / fps;
            j->secs = j->secs < 1 ? 1 : (j->secs > 30 ? 30 : j->secs);
        } else {
            dump_prepare(c, j);
        }
        if (c->job_step) {
            c->job_pct = 0;
            snprintf(c->job_path, sizeof(c->job_path), "%s", j->path);
            j->state = 1;
            if (xTaskCreate(dump_task, "Ring dump", 12 * configMINIMAL_STACK_SIZE, j, tskIDLE_PRIORITY + 1, NULL) == pdPASS) {
                o.more = true;
                return OPE_OK;
            }
            j->state = 0;                               // no task: write on the caller, without the bar
        }
        dump_run(j);
        j->state = 2;
    }
    if (j->state == 1) {                                // the task writes: the share it has written is in job_pct
        o.more = true;
        return OPE_OK;
    }
    j->state = 0;
    c->job_pct = 0;
    c->job_path[0] = 0;
    if (c->job_cancel) {                                // FW's write cannot be stopped half way: it ran to its end, the file goes
        c->job_cancel = false;
        if (!j->why) {
            FileManager::getFileManager()->delete_file(j->path);
        }
        snprintf(o.msg, o.msglen, "Cancelled: nothing was written.");
        return OPE_OK;
    }
    if (j->why) {
        snprintf(o.msg, o.msglen, "%s", j->why);
        return OPE_FAILED;
    }
    if (ring) {
        snprintf(o.msg, o.msglen, "Saved %s: %d entries, %u KB", j->path, j->entries, (unsigned)((j->bytes + 1023) >> 10));
    } else {
        snprintf(o.msg, o.msglen, "Saved %s (%u KB)", j->path, (unsigned)((j->bytes + 1023) >> 10));
    }
    return OPE_OK;
}

static int op_dump(CoreCtx *c, const OpArgs &a, OpOut &o) { return dump_op(c, a, o, false); }

// "Save .C64RE Dump" (FW apps/c64re/manifest.json, category "C64 Machine"): the machine as it stands, no ring needed; the media that are in it go inside
static int op_c64re_dump(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    char p[260];
    if (!trx_target_path(c, a.s(0), ".c64re", p, sizeof(p))) {
        snprintf(o.msg, o.msglen, "No medium to save to");
        return OPE_FAILED;
    }
    TrxC64reSrc src;
    memset(&src, 0, sizeof(src));
    src_fill(c, &src);
    if (c64_cart_active()) {
        src.cart_src = c64_cart_path();
    }
    trx_c64re_set_source(&src);
    uint32_t bytes = 0;
    const char *why = trx_c64re_dump_machine(p, &bytes);
    trx_c64re_set_source(NULL);
    if (why) {
        snprintf(o.msg, o.msglen, "%s", why);
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "Saved %s (%u KB)", p, (unsigned)((bytes + 1023) >> 10));
    return OPE_OK;
}

static const char *st_suggest_c64re(CoreCtx *) { return "c64re"; }

static int op_ringdump(CoreCtx *c, const OpArgs &a, OpOut &o) { return dump_op(c, a, o, true); }

static int op_load(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    TrxC64reSrc src;                                    // drive A's power: the file's drive state goes back only into a powered drive
    memset(&src, 0, sizeof(src));
    src_fill(c, &src);
    src.rom_ctx = c;
    src.rom_write = rom_write;
    src.cart_for_state = cart_for_state;
    src.eeprom_put = eeprom_put;
    src.machine_set = machine_set;
    src.drive_mount = drive_mount;
    trx_c64re_set_source(&src);
    const char *why = trx_c64re_load(a.s(0));
    trx_c64re_set_source(NULL);
    if (why) {
        snprintf(o.msg, o.msglen, "%s", why);
        return OPE_FAILED;
    }
    g_sel = -1;
    g_nmark = 0;
    if (trx_c64re_missed()[0]) {
        snprintf(o.msg, o.msglen, "State loaded from %s; not put back: %s", a.s(0), trx_c64re_missed());
    } else {
        snprintf(o.msg, o.msglen, "State loaded from %s", a.s(0));
    }
    return OPE_OK;
}

// the setting: Timewarp Ring Off / On, Instruction Trace Off / On
static int fx_ring(CoreCtx *c, const OpArgs &, OpOut &)
{
    ring_autostart = cfg_value(c, "Timewarp Ring", 1) != 0;
    TrxRingStatus st;
    trx_ring_status(&st);
    if (st.present) {
        if (ring_autostart && !st.on && c->started) {
            trx_ring_start();
        } else if (!ring_autostart && st.on) {
            trx_ring_stop();
        }
        trx_ring_trace_ctl(cfg_value(c, "Instruction Trace", 0) ? 1 : 0);
    }
    return OPE_OK;
}

// ---- states ---------------------------------------------------------------------------------------------------------------------------------------------
static char b1[64], b2[64], b3[64], b4[24], b5[64];

// A08: the Timewarp page is refused while the REU is on (FW io/c64/trx_timewarp_ui.cc:323-326, the popup text of io/c64/trx_ring_reu.h:10); menu.screen's `unless`
static const char *st_reu_off(CoreCtx *) { return trx_ring_reu_refusal() ? "Timewarp is off while REU is on" : 0; }

static const char *st_chip(CoreCtx *)
{
    TrxRingStatus st;
    trx_ring_status(&st);
    return !st.present ? "N/A" : (st.reu_off ? "Off: REU on" : (trx_ring_scrubbing() ? "Scrubbing" : (st.on ? "Recording" : "Stopped")));
}
static const char *st_ring(CoreCtx *)
{
    TrxRingStatus st;
    trx_ring_status(&st);
    if (!st.present) {
        return "No frame ring in this bitstream";
    }
    if (st.reu_off) {
        return "Off while the REU is enabled";
    }
    int fps = trx_ring_fps() ? trx_ring_fps() : 50;
    snprintf(b1, sizeof(b1), "%s, %d.%d s", st.on ? "Recording" : "Stopped", st.frames / fps, (st.frames % fps) * 10 / fps);
    return b1;
}
static const char *st_label(CoreCtx *)
{
    int n = trx_ring_count();
    if (n <= 0) {
        return "no frames yet";
    }
    int fps = trx_ring_fps() ? trx_ring_fps() : 50;
    int back = newest() - shown();
    if (back <= 0 && g_sel < 0) {
        return "LIVE";
    }
    snprintf(b2, sizeof(b2), "-%d.%d s", back / fps, (back % fps) * 10 / fps);
    return b2;
}
static const char *st_left(CoreCtx *)
{
    int n = trx_ring_count();
    int fps = trx_ring_fps() ? trx_ring_fps() : 50;
    snprintf(b3, sizeof(b3), "-%d.%d s", n / fps, (n % fps) * 10 / fps);
    return b3;
}
static const char *st_frame(CoreCtx *)
{
    TrxFrame f;
    if (!trx_ring_frame(shown(), &f)) {
        return "FRAME -";
    }
    snprintf(b5, sizeof(b5), "FRAME %u", (unsigned)f.marker);
    return b5;
}
static const char *st_bar(CoreCtx *)
{
    int n = trx_ring_count();
    snprintf(b4, sizeof(b4), "%d", n > 1 ? shown() * 100 / (n - 1) : 100);
    return b4;
}
// the bar of the pixel layer (trx_timewarp_ui.cc draw_bar): the position large, the frame small, the span of the ring as a track
static const char *st_pos(CoreCtx *)
{
    int n = trx_ring_count();
    if (g_sel < 0 || n <= 0) {
        return "now";
    }
    int fps = trx_ring_fps() ? trx_ring_fps() : 50;
    int f = n - 1 - g_sel;
    snprintf(b4, sizeof(b4), "-%d.%d s", f / fps, (f % fps) * 10 / fps);
    return b4;
}
static const char *st_info(CoreCtx *)
{
    int n = trx_ring_count();
    if (n < 2) {
        return "the ring holds no frame yet";
    }
    snprintf(b1, sizeof(b1), "frame %d / %d", shown() + 1, n);
    return b1;
}
static const char *st_glyph(CoreCtx *)
{
    static char g[4];
    snprintf(g, sizeof(g), "%d", g_glyph);
    return g;
}
static const char *st_track(CoreCtx *)
{
    int n = trx_ring_count();
    snprintf(b5, sizeof(b5), "%d/%d", n > 0 ? shown() : 0, n);
    return b5;
}
static const char *st_marks(CoreCtx *)
{
    static char m[32];
    snprintf(m, sizeof(m), "%d marker%s", g_nmark, g_nmark == 1 ? "" : "s");
    return g_nmark ? m : "none";
}
static const char *st_present(CoreCtx *)
{
    TrxRingStatus st;
    trx_ring_status(&st);
    return st.present ? "yes" : 0;
}
static const char *st_running(CoreCtx *)
{
    TrxRingStatus st;
    trx_ring_status(&st);
    return st.present && st.on ? "yes" : 0;
}
static const char *st_stopped(CoreCtx *)
{
    TrxRingStatus st;
    trx_ring_status(&st);
    return st.present && !st.on ? "yes" : 0;
}

static const ArgSpec value_arg[] = { ARG_INT("value", -100000, 100000) };
static const ArgSpec name_arg[] = { ARG_STRING("name", 55) };
static const ArgSpec path_arg[] = { ARG_PATH("path") };

void ringops_register(OpRegistry &r)
{
    r.contributor("timewarp");
    r.add_op("tw.step", value_arg, 1, op_step);
    r.add_op("tw.seconds", value_arg, 1, op_seconds);
    r.add_op("tw.live", 0, 0, op_live);
    r.add_op("tw.oldest", 0, 0, op_oldest);
    r.add_op("tw.resume", 0, 0, op_resume);
    r.add_op("tw.cancel", 0, 0, op_cancel);
    r.add_op("tw.monitor", 0, 0, op_monitor);
    r.add_op("tw.mark", 0, 0, op_mark);
    r.add_op("tw.next_mark", value_arg, 1, op_next_mark);
    r.add_op("tw.clear_marks", 0, 0, op_clear_marks);
    r.add_op("tw.dump", name_arg, 1, op_dump);
    r.add_op("tw.ringdump", name_arg, 1, op_ringdump);
    r.add_op("machine.save_c64re", name_arg, 1, op_c64re_dump);
    r.add_state("machine.suggest.c64re", st_suggest_c64re);
    r.add_op("tw.load", path_arg, 1, op_load);
    r.add_op("ring.start", 0, 0, op_start);
    r.add_op("ring.stop", 0, 0, op_stop);
    r.add_op("ring.fx", value_arg, 1, fx_ring);
    r.add_state("ring.reu_off", st_reu_off);
    r.add_state("tw.chip", st_chip);
    r.add_state("tw.ring", st_ring);
    r.add_state("tw.label", st_label);
    r.add_state("tw.left", st_left);
    r.add_state("tw.frame", st_frame);
    r.add_state("tw.bar", st_bar);
    r.add_state("tw.marks", st_marks);
    r.add_state("tw.pos", st_pos);
    r.add_state("tw.info", st_info);
    r.add_state("tw.glyph", st_glyph);
    r.add_state("tw.track", st_track);
    r.add_state("tw.present", st_present);
    r.add_state("tw.running", st_running);
    r.add_state("tw.stopped", st_stopped);
}
