// The Commodore 128 descriptor (T113 section 5.5, T104). One system ROM image (72 KiB, the boot0.rom layout; one file, or made from VICE's six part files, T258) goes through the DDR2 window
// into the ROM line cache's backing store, then ROM_CTRL.b0 flushes the cache. With the setting ROM set = JiffyDOS (T185) the kit files the person selected
// (one row each in the settings, or the file action in the browser) are written over the C64 and C128 KERNAL parts of that image first, and drive A takes
// the JiffyDOS DOS ROM of its type. Settings: the 40/80 DISPLAY switch (D4080, taken at reset)
// and the picture shown (MON_SEL: the VIC-IIe's capture, or the VDC's 640 x 400, or 640 x 480 (woven in interlace, or a frame of more than 200 lines, T257), both on the pixel layer: Integer, Fit or Fill as for every core), which the
// Auto switch (T194 D12: on/off, time) moves to the chip that is being used, and a core hotkey (40/80) flips by hand.
// Drive A is the cbm1541 library with the 1541 and the 1581 (cbm1541_desc.h, CBM1581_*). The register page is c128_regs.h; the extra keys of
// the USB keyboard are c128_xkey_for (below), delivered by rawkeys.cc in held mode (T141 P2). Run Disk (T206) resets the machine and types the start at
// READY (c128.type_script, the shared cbm_run job). T217: Load / Load + run of a .prg and the inner Mount & Run (run_op). Not in this descriptor: cartridge port, REU, .tap streamer (T257 D11: the gateware has no emulated cartridge and no streamer), see T113 section 5.5 "Open".
#include "hk_defaults.h"
#include "ctx.h"
#include "boot.h"
#include "c128_regs.h"
#include "c128_keys.h"
#include "c128_prg.h"
#include "cbm1541.h"
#include "cbm1541_desc.h"
#include "cbm_run.h"
#include "cores.h"
#include "rawkeys.h"
#include "drive_rom_pick.h"
#include "video.h"
#include "picture.h"
#include "hotkeys.h"
#include <stdio.h>
#include <string.h>

// ---- the machine's state in the application ------------------------------------------------------------------------------------
// The ROM set (T185): the standard image the person supplied is kept whole (`std`); the JiffyDOS kit files are read when the machine starts with the
// setting JiffyDOS and kept at their regions of the image in `jf` (+0x0000 the C64 BASIC + KERNAL part, of which the 8 KiB KERNAL alone lies at +0x2000;
// +0x4000 the C128 editor + Z80 BIOS + KERNAL part, T185 D1, D4). What goes into the window is `std`, or `std` with the kit's parts written over their
// regions when the setting is JiffyDOS. `gen` counts the changes of what the person supplied; `put_gen` and
// `put_mode` say what the window holds, so a reset writes the image again only when it differs.
#define IMG_PART 0x4000u                                // the size of one kit part
struct C128State {
    CbmRun run;                                         // T206: Run Disk types LOAD"*",8,1 and RUN at READY (cbm_run.cc)
    int mon_seen;                                       // the MON_SEL value the picture layer follows; -1: none yet
    int auto_seen;                                      // C64 mode: 0 once the VIC has been put up (a flip by hand then stays); -1: not in C64 mode / none yet
    int act_seq;                                        // Auto: the last ACT_SEQ taken in; -1: none yet
    uint8_t den_prev;                                   // DEN of the frame before (a frame that turns the screen on counts for the VIC)
    uint32_t t_last[2];                                 // Auto: when chip 0 (VIC) / 1 (VDC) last showed activity (now_ms), valid if t_has
    uint8_t t_has[2];
    uint32_t sw_ms;                                     // when the picture last changed (start, Auto, hotkey, monitor key): activity counts only after it
    int shown;                                          // the MON_SEL value Auto last saw: a different one is a switch by hand
    uint32_t hash_ms;                                   // when the VIC picture was last sampled
    uint32_t hash, hash_frames;                         // its hash and the capture's frame counter then
    uint8_t hash_ok;
    // c128.auto_diag (board diagnostic, read-only): the last frame's words and counters since the last act_reset
    uint8_t dg_v, dg_cr, dg_d, dg_f;                    // the last ACT words taken in
    uint32_t dg_st;                                     // the last STILL_STATUS read by act_hash
    uint32_t dg_hchg;                                   // now_ms of the last hash change
    uint32_t n_fr, n_fr_vdc, n_mark_vic, n_mark_vdc;    // frames taken, frames with VDC writes, VIC marks (registers/colour RAM), VDC marks
    uint32_t n_hs, n_hchg, n_hdrop, n_hnonew, n_hinv;   // hash samples taken, changes, dropped (ring overtook), skipped (no new frame), skipped (no capture valid)
    uint32_t dbg_t, dbg_h, dbg_m, dbg_w, dbg_fmin, dbg_fmax;     // the console line of the counters (debug_line)
    uint8_t romset;                                     // the setting: 0 Standard, 1 JiffyDOS
    uint8_t have_img;                                   // `std` holds an image
    uint8_t chr_ok;                                     // the VIC's character store holds the character ROMs of `std`
    uint8_t jf_c64, jf_c128;                            // the kit's parts that were selected and taken
    uint32_t jf_c64_off;                                // where the C64 part starts inside its 16 KiB region: 0, or 0x2000 for the KERNAL alone
    int put_mode;                                       // -1: nothing put yet
    uint32_t gen, put_gen;
    uint8_t std[C128_ROM_BYTES];
    uint8_t jf[2 * IMG_PART];
    DriveRomPick d1541, d1581;
};
static const DriveRomKind kind1541 = { "drive.put_rom", "c128", "1541", false, true, true };
static const DriveRomKind kind1581 = { "drive.put_rom1581", "c128", "1581", true, true };

static C128State *c128_state(CoreCtx *c)
{
    if (!c->state) {
        C128State *s = new C128State();
        memset(s, 0, sizeof(C128State));
        s->mon_seen = -1;
        s->auto_seen = -1;
        s->act_seq = -1;
        s->put_mode = -1;
        c->state = s;
    }
    return (C128State *)c->state;
}

// ---- settings ------------------------------------------------------------------------------------------------------------------
#define KEY_D4080 "40/80 DISPLAY (at next reset)"
#define KEY_MON   "Picture shown"
#define KEY_AUTO  "Auto switch"
#define KEY_AUTOT "Auto switch time"
#define KEY_ROMSET "ROM set"
static const char *const romset_names[] = { "Standard", "JiffyDOS" };
// T185 D4: the four kit files are selected by the person (kind 3: a file selector, the path in the .cfg); none is selected by default
#define KEY_JF_C128 "JiffyDOS C128 ROM"
#define KEY_JF_C64  "JiffyDOS C64 ROM"
#define KEY_JF_1541 "JiffyDOS 1541 ROM"
#define KEY_JF_1581 "JiffyDOS 1581 ROM"
static const char *const kit_none[] = { "" };
static const char *const d4080_names[] = { "40 columns", "80 columns" };
// T194 D12: the picture shown at the start and after a Reset: 0 = 40 columns (VIC-IIe), 1 = 80 columns (VDC) = the value of MON_SEL b0. A .cfg of an older version
// may hold "Auto" (T140 18): that label is not in the list, so the item falls back to its default (40 columns, the picture Auto started with) and Auto switch is on by default.
static const char *const mon_names[]   = { "40 columns (VIC-IIe)", "80 columns (VDC)" };
static const char *const onoff_names[] = { "Off", "On" };
static const char *const autot_names[] = { "0.5 s", "1 s", "2 s", "3 s" };
static const uint32_t autot_ms[] = { 500, 1000, 2000, 3000 };
#define AUTOT_DEF 1

static const char *const joy_names[]   = { "Normal", "Swapped" };
static const char *const dset_names[]  = { "Off", "External datasette" };
// T140 D24: the System Mode, the four the C64 Ultimate offers (menu.toml sysmodes): the VIC's line and frame, the machine's clock and the colour standard of the
// analogue output (cores/c128/mono/c128_avout.v) follow it. Values in the order of t_video_mode.
#define KEY_SYSMODE "System Mode"
static const char *const sysmode_names[] = { "PAL", "NTSC", "PAL-60", "NTSC-50" };
// C64_VIDEOFORMAT per mode (u64.h, color_timings.cc): b0 NTSC colour encoding, b1 60 Hz, b3 NTSC colour clock, b5 65 cycles per line. host/mode_ctl.v
// takes {b5 != b0, b0} as the mode: PAL 63 cycles 0, NTSC 65 cycles 1, PAL-60 65 cycles 2, NTSC-50 63 cycles 3.
static const uint8_t sysmode_fmt[4] = { 0x00, 0x2B, 0x2A, 0x01 };

static const Setting settings[12 + CBM1541_NSETTINGS] = {
    { KEY_D4080, d4080_names, 2, 0, WHEN_RESET, "c128.set_d4080", 0, 0, 0, 0, 0, "40/80 DISPLAY" },
    { KEY_MON,   mon_names,   2, 0, WHEN_LIVE, "c128.set_monitor", 0 },
    { KEY_AUTO,  onoff_names, 2, 1, WHEN_LIVE, "c128.set_auto", 0 },
    { KEY_AUTOT, autot_names, 4, AUTOT_DEF, WHEN_LIVE, "c128.set_auto_time", 0 },
    { "Joystick ports", joy_names, 2, 0, WHEN_LIVE, "c128.set_joy", 0 },
    { "Datasette port", dset_names, 2, 0, WHEN_LIVE, "c128.set_dset", 0 },
    { KEY_SYSMODE, sysmode_names, 4, 0, WHEN_LIVE, "c128.set_sysmode", 0 },
    { KEY_ROMSET, romset_names, 2, 0, WHEN_RESET, "c128.set_romset", 0 },
    { KEY_JF_C128, kit_none, 1, 0, WHEN_RESET, "c128.use_jiffy_c128", 0, 0, 0, 3 },
    { KEY_JF_C64,  kit_none, 1, 0, WHEN_RESET, "c128.use_jiffy_c64",  0, 0, 0, 3 },
    { KEY_JF_1541, kit_none, 1, 0, WHEN_RESET, "c128.use_jiffy_1541", 0, 0, 0, 3 },
    { KEY_JF_1581, kit_none, 1, 0, WHEN_RESET, "c128.use_jiffy_1581", 0, 0, 0, 3 },
    CBM1541_SETTINGS,
};

// The pixel layer shows the picture the core shows (T194): the VDC's buffers in 80 columns, the VIC-IIe's capture (the gateware's format 3, c128_vic below) in 40.
// Video::select_picture switches the one layer between them; Picture, the HDMI mode and Smoothing act on both, and every later show() (menu close, Freeze)
// brings the selected one back.
static void follow_picture(CoreCtx *c, int mon80)
{
    c128_state(c)->mon_seen = mon80;
    if (!c->video) {
        return;
    }
    c->video->select_picture(mon80 ? 0 : 1);
    if (c->started && !c->menu_open && !c->frozen) {
        c->video->show();
    }
}

// Auto switch (T194 D12, replaces T140 18's Auto value): the app watches the picture that is NOT shown. It switches to the other chip when that one shows activity
// after the last change of the picture AND the shown chip has been quiet for the switch time (its own last activity, or the last change if later). Activity:
//   VDC: the CPU's writes to its data register in a frame (c128_act.v, ACT_VDC; its hardware cursor blinks without writes, so no picture hash);
//   VIC: colour RAM writes, VIC register writes above ACT_VIC_IDLE per frame, or a change of the hash of the captured VIC picture (act_hash, every HASH_MS).
// Both chips active: the shown one is never quiet, so nothing flips. The monitor key (the core toggles MON_SEL) and the 40/80 hotkey are changes by hand: the
// watching goes on, activity counts again from that moment. C64 mode shows the VIC, Z80 mode leaves the picture.
#define ACT_VIC_IDLE 40                                 // VIC register writes per frame the KERNAL's IRQ makes by itself: 23, constant, at READY in 80 columns (sim 2026-11-09, T140 18); with margin
#define HASH_MS 100                                     // the VIC picture is sampled this often
#define HASH_ROW_STEP 4                                 // every 4th row of the capture: a character cell is 8 rows, so a changed cell still shows in one of them
#define C128_STILL_ROW_BYTES 208                        // T194 D4: 13 words of 16 bytes per row
// The capture lies in the application's DDR2 window (0x01000000-0x01FFFFFF): STILL_BASE (gateware default 0x05050000) is above what the RISC-V can address, so the
// start moves it to the free 256 KB-aligned hole below the VDC buffers (the ROM image 0x01C00000, the RAM 0x01C20000 + 128 KB); slot n at + n x 0x10000.
#define C128_STILL_DDR   0x01CC0000u
#define C128_STILL_BASE  0x6064u                        // pix_layer.v: W 32 bit, byte 3 commits
#define C128_STILL_STATUS 0x6068u                       // R: b0 valid, b1 60 Hz (240 rows), b3:2 slot of the last complete frame, b31:8 frames captured

static uint32_t act_switch_ms(CoreCtx *c)
{
    int i = c->settings ? c->settings->find(KEY_AUTOT) : -1;
    int v = i >= 0 ? c->settings->value(i) : AUTOT_DEF;
    return autot_ms[v >= 0 && v < 4 ? v : AUTOT_DEF];
}

static bool auto_on(CoreCtx *c)
{
    int i = c->settings ? c->settings->find(KEY_AUTO) : -1;
    return i < 0 || c->settings->value(i) != 0;
}

static void act_mark(C128State *s, int chip, uint32_t now)
{
    s->t_last[chip] = now;
    s->t_has[chip] = 1;
}

// Start of the watching (start, reset, Auto switch turned on, a change of the picture): nothing seen yet.
static void act_reset(C128State *s)
{
    s->act_seq = -1;
    s->t_has[0] = s->t_has[1] = 0;
    s->hash_ok = 0;
    s->den_prev = 0;
    s->n_fr = s->n_fr_vdc = s->n_mark_vic = s->n_mark_vdc = 0;
    s->n_hs = s->n_hchg = s->n_hdrop = s->n_hnonew = s->n_hinv = 0;
}

static void act_poll(CoreCtx *c, uint32_t now)
{
    C128State *s = c128_state(c);
    int q0 = c->io->rd(C128_ACT_SEQ);
    if (q0 == s->act_seq) {
        return;
    }
    uint8_t v = c->io->rd(C128_ACT_VIC), cr = c->io->rd(C128_ACT_CRAM), d = c->io->rd(C128_ACT_VDC), f = c->io->rd(C128_ACT_FLAGS);
    if (c->io->rd(C128_ACT_SEQ) != q0) {
        return;                                         // the words straddle a frame: take them at the next service
    }
    s->act_seq = q0;
    s->dg_v = v; s->dg_cr = cr; s->dg_d = d; s->dg_f = f;
    s->n_fr++;
    s->n_fr_vdc += d ? 1u : 0u;
    // The KERNAL's IRQ rewrites a few VIC registers every frame from its shadows ($D011, $D016, $D018, the raster split) also in 80 columns, so a
    // handful of register writes is no sign of a 40-column program (board 2026-11-09: an idle 80-column BASIC flipped to the VIC; the sim measures 23 per raster frame there, the gateware's frame is the VIC's raster wrap). Only the writes
    // above ACT_VIC_IDLE per frame and the colour RAM count for the VIC, and only while the screen is on (DEN, this frame or the one before).
    unsigned vs = (unsigned)cr + (v > ACT_VIC_IDLE ? (unsigned)(v - ACT_VIC_IDLE) : 0u);
    uint8_t den = f & 1;
    if (vs && (den || s->den_prev)) {
        act_mark(s, 0, now);
        s->n_mark_vic++;
    }
    if (d) {
        act_mark(s, 1, now);
        s->n_mark_vdc++;
    }
    s->den_prev = den;
}

// The hash of the VIC's picture (T194 D4: the capture of every frame, in the slot STILL_STATUS names). A change of it is VIC activity. Only every HASH_ROW_STEP-th row
// is read (68 of 270 rows = 14 KB per sample, 10 samples per second).
static void act_hash(CoreCtx *c, uint32_t now)
{
    C128State *s = c128_state(c);
    if (s->hash_ok && (uint32_t)(now - s->hash_ms) < HASH_MS) {
        return;
    }
    s->hash_ms = now;
    uint32_t st = 0;
    for (int i = 0; i < 4; i++) {
        st |= (uint32_t)c->io->rd(C128_STILL_STATUS + (uint32_t)i) << (8 * i);       // byte 0 first: it takes the other three
    }
    s->dg_st = st;
    if (!(st & 1)) {
        s->n_hinv++;
        return;                                         // no capture yet
    }
    if (s->hash_ok && (st >> 8) == s->hash_frames) {
        s->n_hnonew++;
        return;                                         // no new frame (the machine is paused)
    }
    uint32_t rows = (st & 2) ? 240 : 270;
    uint32_t base = C128_STILL_DDR - 0x01000000u + ((st >> 2) & 3) * 0x10000u;
    static uint32_t roww[C128_STILL_ROW_BYTES / 4];        // words: the hash reads 32-bit loads, so the buffer is aligned for them
    uint8_t *row = (uint8_t *)roww;
    uint32_t h = 2166136261u;
    for (uint32_t y = 0; y < rows; y += HASH_ROW_STEP) {
        c->io->ddr_read(base + y * C128_STILL_ROW_BYTES, row, C128_STILL_ROW_BYTES);
        for (unsigned i = 0; i < C128_STILL_ROW_BYTES / 4; i++) {
            h = (h ^ roww[i]) * 16777619u;
        }
    }
    uint32_t st2 = 0;
    for (int i = 0; i < 4; i++) {
        st2 |= (uint32_t)c->io->rd(C128_STILL_STATUS + (uint32_t)i) << (8 * i);
    }
    if ((uint32_t)((st2 >> 8) - (st >> 8)) >= 2) {
        s->n_hdrop++;
        return;                                         // the ring of three went round under the read (a long preemption): a torn hash is no activity
    }
    s->n_hs++;
    if (s->hash_ok && h != s->hash) {
        act_mark(s, 0, now);
        s->n_hchg++;
        s->dg_hchg = now;
    }
    s->hash = h;
    s->hash_frames = st >> 8;
    s->hash_ok = 1;
}

// The one place that decides what Auto does. Returns 0/1 (the picture to show), or -2: leave the picture. C64 mode: the VIC; Z80 mode (CP/M): leave.
// Otherwise: switch when the other chip was active after the last change and the shown one has been quiet for the switch time (`shown_quiet`).
static int auto_decide(int mode_c64, int mode_z80, int shown, bool other_active, bool shown_quiet)
{
    if (mode_z80) {
        return -2;
    }
    if (mode_c64) {
        return 0;
    }
    return (other_active && shown_quiet) ? 1 - shown : -2;
}

static void auto_set(CoreCtx *c, int w, uint32_t now)
{
    C128State *s = c128_state(c);
    c->io->wr(C128_MON_SEL, (uint8_t)w);
    s->shown = w;
    s->sw_ms = now;
    follow_picture(c, w);
}

// What auto_follow() decides from the state at `now` (also read by c128.auto_diag).
struct AutoEval {
    uint8_t st;
    int shown, other, w;
    bool other_active, shown_quiet;
    uint32_t qfrom;
};
static void auto_eval(CoreCtx *c, uint32_t now, uint8_t st, int shown, AutoEval *e)
{
    C128State *s = c128_state(c);
    e->st = st;                                         // the caller's reads: auto_follow decides on the values it read before act_poll/act_hash
    e->shown = shown;
    e->other = 1 - e->shown;
    e->other_active = s->t_has[e->other] && (int32_t)(s->t_last[e->other] - s->sw_ms) > 0;
    e->qfrom = (s->t_has[e->shown] && (int32_t)(s->t_last[e->shown] - s->sw_ms) > 0) ? s->t_last[e->shown] : s->sw_ms;
    e->shown_quiet = (uint32_t)(now - e->qfrom) >= act_switch_ms(c);
    e->w = auto_decide(e->st & 0x01, e->st & 0x02, e->shown, e->other_active, e->shown_quiet);
}

static void auto_follow(CoreCtx *c)
{
    C128State *s = c128_state(c);
    uint32_t now = c->io->now_ms();
    uint8_t st = c->io->rd(C128_STATUS);
    int shown = c->io->rd(C128_MON_SEL) & 1;
    if (shown != s->shown) {                            // the monitor key (or a reset) changed it: a change by hand, the watching starts from here
        s->shown = shown;
        s->sw_ms = now;
    }
    act_poll(c, now);
    if (!(st & 0x03)) {
        act_hash(c, now);
    }
    AutoEval e;
    auto_eval(c, now, st, shown, &e);
    int w = e.w;
    if (w < 0) {
        s->auto_seen = -1;
        return;
    }
    if (st & 0x01) {                                    // C64 mode: the VIC once, a later flip by hand stays until the mode changes
        if (s->auto_seen == 0) {
            return;
        }
        s->auto_seen = 0;
    } else {
        s->auto_seen = -1;
    }
    if (w != shown) {
        auto_set(c, w, now);
    }
}

// c128.auto_diag: one line to read on the board while Auto is watching (GET /v1/state/c128.auto_diag). Ages are "now - last activity" in ms (-1: none since the last
// reset / switch). shown 0 = VIC (40), 1 = VDC (80). dec: what auto_decide returns now (-2 leave, -1.. 0/1 the picture).
//   auto on|off, sw = switch time ms, since_sw = now - last change of the picture, vic_age / vdc_age, quiet = the shown one has been quiet for sw, oth = the other was
//   active after the last change, last frame words vic/cram/vdc/flags(DEN) with the frame number seq, fr = frames taken, frv = frames with VDC writes,
//   mv / md = VIC / VDC marks, hash ok / value / frame counter hf / change count hc and its age hc_age, hs samples, drop (ring overtook), nonew, inv (no capture),
//   st = raw STILL_STATUS (b0 valid, b1 60 Hz, b3:2 slot, b31:8 frames).
static char diag_buf[512];
static const char *state_auto_diag(CoreCtx *c)
{
    C128State *s = c128_state(c);
    uint32_t now = c->io->now_ms();
    AutoEval e;
    auto_eval(c, now, c->io->rd(C128_STATUS), c->io->rd(C128_MON_SEL) & 1, &e);
    long va = s->t_has[0] ? (long)(uint32_t)(now - s->t_last[0]) : -1;
    long da = s->t_has[1] ? (long)(uint32_t)(now - s->t_last[1]) : -1;
    long ha = s->n_hchg ? (long)(uint32_t)(now - s->dg_hchg) : -1;
    snprintf(diag_buf, sizeof(diag_buf),
             "shown=%d auto=%s st=%02x sw=%u since_sw=%u vic_age=%d vdc_age=%d quiet=%d oth=%d dec=%d "
             "last(vic=%u cram=%u vdc=%u fl=%u seq=%d) fr=%u frv=%u mv=%u md=%u "
             "hash(ok=%u v=%08x hf=%u hc=%u hc_age=%d hs=%u drop=%u nonew=%u inv=%u) still=%08x",
             e.shown, auto_on(c) ? "on" : "off", (unsigned)e.st, (unsigned)act_switch_ms(c), (unsigned)(uint32_t)(now - s->sw_ms), (int)va, (int)da,
             e.shown_quiet ? 1 : 0, e.other_active ? 1 : 0, e.w, (unsigned)s->dg_v, (unsigned)s->dg_cr, (unsigned)s->dg_d, (unsigned)s->dg_f, s->act_seq,
             (unsigned)s->n_fr, (unsigned)s->n_fr_vdc, (unsigned)s->n_mark_vic, (unsigned)s->n_mark_vdc,
             (unsigned)s->hash_ok, (unsigned)s->hash, (unsigned)s->hash_frames, (unsigned)s->n_hchg, (int)ha, (unsigned)s->n_hs, (unsigned)s->n_hdrop,
             (unsigned)s->n_hnonew, (unsigned)s->n_hinv, (unsigned)s->dg_st);
    return diag_buf;
}

static int op_set_monitor(CoreCtx *c, const OpArgs &a, OpOut &)
{
    int v = a.i(0) ? 1 : 0;
    C128State *s = c128_state(c);
    s->auto_seen = -1;
    act_reset(s);
    auto_set(c, v, c->io->now_ms());
    return OPE_OK;
}
static const ArgSpec mon_arg[] = { ARG_INT("value", 0, 1) };

// Auto switch on: the watching starts now. Off: nothing is read. The time is read when the decision is made.
static int op_set_auto(CoreCtx *c, const OpArgs &, OpOut &)
{
    C128State *s = c128_state(c);
    act_reset(s);
    s->sw_ms = c->io->now_ms();
    return OPE_OK;
}
static int op_set_auto_time(CoreCtx *, const OpArgs &, OpOut &)
{
    return OPE_OK;
}
static const ArgSpec onoff_arg[] = { ARG_INT("value", 0, 1) };
static const ArgSpec autot_arg[] = { ARG_INT("value", 0, 3) };

// The 40/80 hotkey (T181, HkSpec::core_op): the other picture, as the 40/80 key of the real machine's monitor switch. Auto keeps watching from here.
static int op_toggle_picture(CoreCtx *c, const OpArgs &, OpOut &o)
{
    int w = 1 - (c->io->rd(C128_MON_SEL) & 1);
    auto_set(c, w, c->io->now_ms());
    snprintf(o.msg, (size_t)o.msglen, "Picture %s", w ? "80 columns" : "40 columns");
    return OPE_OK;
}

// T214: an external datasette on the C2N port (same device as on the PET, GitHub #26). PIN_CTRL b0 MOTOR and b1 WRITE driven, b4 READ / SENSE are inputs (0x13);
// b2 / b3 stay off (the gateware has no .tap streamer since T257 D11; there is nothing else on the port). Off releases every pin. WRITE out is not proven by a SAVE on real tape yet.
static int op_set_dset(CoreCtx *c, const OpArgs &a, OpOut &)
{
    c->io->wr(C128_PIN_CTRL, (uint8_t)(a.i(0) ? 0x13 : 0x00));
    return OPE_OK;
}

// T157: the swap bit is on the core's own page E, so an operation, as D4080.
static int op_set_joy(CoreCtx *c, const OpArgs &a, OpOut &)
{
    c->io->wr(C128_JOY, (uint8_t)(a.i(0) ? 1 : 0));
    return OPE_OK;
}

// The host's PLL and scan registers follow the mode (the call a change of the Picture makes), then the format byte tells the host's mode controller, which
// halts the machine, reprograms the system clock and restarts the VIC in the new timing (host/mode_ctl.v, T40).
static int op_set_sysmode(CoreCtx *c, const OpArgs &a, OpOut &)
{
    int sys = a.i(0);
    picture_set_system(c, sys);
    c->io->video_format(sysmode_fmt[sys & 3]);
    return OPE_OK;
}
static const ArgSpec sysmode_arg[] = { ARG_INT("value", 0, 3) };

// D4080 is an operation, not a RegBind: the register is on the core's own page E, and a bound register has to lie in page 0 (desc_check.cc
// REG_LIMIT). The application commits it in the hold (WHEN_RESET), the core takes it at its reset.
static int op_set_d4080(CoreCtx *c, const OpArgs &a, OpOut &)
{
    c->io->wr(C128_D4080, (uint8_t)(a.i(0) ? 1 : 0));
    return OPE_OK;
}

// ---- the system ROM image ------------------------------------------------------------------------------------------------------
// Written through the DDR2 window at the offset ROM_BASE names, read back and compared, then ROM_CTRL.b0 clears the line cache's valid bits (a
// line fetched before is stale) and the walker's busy bit is waited out. The character ROMs go to the VIC's store unless `skip_chr` says it holds them.
static int write_window(CoreCtx *c, const uint8_t *data, uint32_t n, bool skip_chr, char *msg, int msglen)
{
    uint32_t base = 0;
    for (int i = 0; i < 4; i++) {
        base |= (uint32_t)c->io->rd(C128_ROM_BASE + (uint32_t)i) << (8 * i);
    }
    if (base != C128_ROM_DDR_ADDR) {
        snprintf(msg, msglen, "C128 system ROM: the core keeps its ROM at 0x%08X, this application writes 0x%08X (version mismatch)",
                 (unsigned)base, (unsigned)C128_ROM_DDR_ADDR);
        return 1;
    }
    c->io->ddr_write(C128_ROM_WIN_OFF, data, n);
    static uint8_t back[4096];
    for (uint32_t o = 0; o < n; o += sizeof(back)) {
        uint32_t m = n - o < sizeof(back) ? n - o : (uint32_t)sizeof(back);
        c->io->ddr_read(C128_ROM_WIN_OFF + o, back, m);
        if (memcmp(back, data + o, m) != 0) {
            snprintf(msg, msglen, "C128 system ROM: the DDR2 did not take the image (read-back differs at +0x%X)", (unsigned)o);
            return 1;
        }
    }
    c->io->wr(C128_ROM_CTRL, 0x01);
    uint32_t t0 = c->io->now_ms();
    while (c->io->rd(C128_ROM_CTRL) & 0x01) {
        if (c->io->now_ms() - t0 > 100) {
            snprintf(msg, msglen, "C128 system ROM: the ROM cache flush did not finish");
            return 1;
        }
        c->io->delay_ms(1);
    }
    if (skip_chr) {
        return 0;
    }
    // the character ROMs (image bytes 0x10000..0x11FFF) go into the VIC's own store (T113 5.7): written through CHR_ADDR / CHR_DATA, read back
    const uint8_t *chr = data + C128_CHR_IMG_OFF;
    c->io->wr(C128_CHR_ADDR, 0);
    c->io->wr(C128_CHR_ADDR + 1, 0);
    for (uint32_t i = 0; i < C128_CHR_BYTES; i++) {
        c->io->wr(C128_CHR_DATA, chr[i]);
    }
    c->io->wr(C128_CHR_ADDR, 0);
    c->io->wr(C128_CHR_ADDR + 1, 0);
    for (uint32_t i = 0; i < C128_CHR_BYTES; i++) {
        if (c->io->rd(C128_CHR_DATA) != chr[i]) {
            snprintf(msg, msglen, "C128 character ROM: the store did not take the image (read-back differs at +0x%X)", (unsigned)i);
            return 1;
        }
    }
    return 0;
}

// ---- the ROM set (T185) --------------------------------------------------------------------------------------------------------
// What was selected of JiffyDOS lies in `jf` at the regions of the image (D1, D4). With the setting JiffyDOS the C128 part (+0x4000) is the one that has to
// be there; without it the machine starts on the standard image. The C64 part (+0x0000, or the KERNAL alone at +0x2000) is overlaid when it was taken.

// The image the window holds for the setting; `buf` is the room for the overlaid one.
static const uint8_t *image_for(const C128State *s, uint8_t *buf)
{
    if (s->romset != 1 || !s->jf_c128) {
        return s->std;
    }
    memcpy(buf, s->std, C128_ROM_BYTES);
    if (s->jf_c64) {
        memcpy(buf + s->jf_c64_off, s->jf + s->jf_c64_off, IMG_PART - s->jf_c64_off);
    }
    memcpy(buf + IMG_PART, s->jf + IMG_PART, IMG_PART);
    return buf;
}

// The window gets the image the setting and the supplied files make, when it does not hold it already.
static int place(CoreCtx *c, char *msg, int msglen)
{
    C128State *s = c128_state(c);
    if (!s->have_img) {
        return 0;
    }
    int mode = (s->romset == 1 && s->jf_c128) ? 1 : 0;
    if (s->put_mode == mode && s->put_gen == s->gen) {
        return 0;
    }
    static uint8_t comp[C128_ROM_BYTES];
    const uint8_t *img = image_for(s, comp);
    if (write_window(c, img, C128_ROM_BYTES, s->chr_ok != 0, msg, msglen)) {
        s->put_mode = -1;
        return 1;
    }
    s->chr_ok = 1;
    s->put_mode = mode;
    s->put_gen = s->gen;
    return 0;
}

// The standard image: size checked before anything is touched, kept, then placed.
static int put_image(CoreCtx *c, const uint8_t *data, uint32_t n, char *msg, int msglen)
{
    if (n != C128_ROM_BYTES) {
        snprintf(msg, msglen, "C128 system ROM image: %u bytes, the machine takes %u", (unsigned)n, (unsigned)C128_ROM_BYTES);
        return 1;
    }
    C128State *s = c128_state(c);
    memcpy(s->std, data, n < C128_ROM_BYTES ? n : C128_ROM_BYTES);
    s->have_img = 1;
    s->chr_ok = 0;
    s->gen++;
    return place(c, msg, msglen);
}

static int op_put_rom(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return put_image(c, a.p(0), a.len(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static const ArgSpec rom_arg[] = { ARG_BLOB("data") };

// A file picked in the browser: the image as it is, size checked before anything is read.
static int op_load_rom(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    static uint8_t img[C128_ROM_BYTES];
    CoreFile *f = c->fs->open(a.s(0), false);
    if (!f) {
        snprintf(o.msg, o.msglen, "cannot open %s", a.s(0));
        return OPE_FAILED;
    }
    uint64_t n = f->size();
    if (n != C128_ROM_BYTES) {
        c->fs->close(f);
        snprintf(o.msg, o.msglen, "A C128 system ROM image is %u bytes (this one is %u).", (unsigned)C128_ROM_BYTES, (unsigned)fs_size32(n));
        return OPE_FAILED;
    }
    bool ok = f->read(0, img, C128_ROM_BYTES);
    c->fs->close(f);
    if (!ok) {
        snprintf(o.msg, o.msglen, "cannot read %s (the medium is gone?)", a.s(0));
        return OPE_FAILED;
    }
    return put_image(c, img, C128_ROM_BYTES, o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static const ArgSpec path_arg[] = { ARG_PATH("path") };

// ---- the JiffyDOS files of the kit (T185 D1, D4) -------------------------------------------------------------------------------
// The person selects each file (a row of the settings or the file action in the browser); nothing is searched. Four roles:
//   C128  16384 bytes (the C128 KERNAL chip U35: editor + Z80 BIOS + KERNAL -> image +0x4000), or 32768 (the C128DCR kit's one file: the C64 part first)
//   C64   8192 (the C64 KERNAL alone, reset vector $FCE2 -> image +0x2000) or 16384 (BASIC + KERNAL, "CBMBASIC" at offset 4 -> image +0x0000)
//   1541  16384 or 32768 (the sizes the drive takes), or 8192 (the upper half of the ROM, the chip 901229: drive_pick_apply puts it over the stock ROM's lower 8 KiB)
//   1581  32768
// Each part ends in the 6502 vectors; the reset vector (last four bytes: offset size-4) points into the KERNAL at $E000 or above. What tells the C64 part
// from the C128 part is, in the 16 KiB form, the C64 BASIC at its start (the text "CBMBASIC" at offset 4; the C128's part has none); the 8 KiB KERNAL has no
// BASIC, so it has to carry the C64 KERNAL's own cold start $FCE2 (the C128 KERNAL's is $FF3D).
enum { KIT_C128 = 0, KIT_C64, KIT_1541, KIT_1581, KIT_N };
#define KIT_MAX (2 * IMG_PART)
static const char *const kit_key[KIT_N] = { KEY_JF_C128, KEY_JF_C64, KEY_JF_1541, KEY_JF_1581 };

static bool jf_basic_at4(const uint8_t *d)
{
    return memcmp(d + 4, "CBMBASIC", 8) == 0;
}

static bool all_same(const uint8_t *d, uint32_t n)
{
    if (!n) return true;                                // nothing to read: empty
    for (uint32_t i = 1; i < n; i++) {
        if (d[i] != d[0]) return false;
    }
    return d[0] == 0x00 || d[0] == 0xFF;
}

static const char *jf_part_fault(const uint8_t *d, uint32_t size, bool c64_part)
{
    if (all_same(d, size)) {
        return "it is empty (all 0x00 or all 0xFF)";
    }
    uint32_t vec = (uint32_t)d[size - 4] | ((uint32_t)d[size - 3] << 8);
    if (vec < 0xE000u) {
        return "its reset vector is below $E000";
    }
    if (c64_part && size == IMG_PART / 2) {
        return vec == 0xFCE2u ? 0 : "reset vector not $FCE2: not a C64 KERNAL";
    }
    if (c64_part && !jf_basic_at4(d)) {
        return "no C64 BASIC in it: not the C64 part";
    }
    if (!c64_part && jf_basic_at4(d)) {
        return "it is the C64 part, not the C128 part";
    }
    return 0;
}

// The sizes a role takes (before the file is read), and the check of its content. Returns null when the file is what the role takes.
static bool kit_size_ok(int which, uint64_t n)
{
    switch (which) {
    case KIT_C128: return n == IMG_PART || n == 2 * IMG_PART;
    case KIT_C64:  return n == IMG_PART / 2 || n == IMG_PART;
    case KIT_1541: return n == ROM_HALF || n == 16384u || n == 32768u;
    default:       return n == 32768u;
    }
}

// One status line each (UX review 2026-11-07): which file sizes are which kit part is in the T185 spec, not on the screen.
static void kit_size_why(int which, uint64_t n, char *why, int wl)
{
    static const char *const want[KIT_N] = { "16384 or 32768", "8192 or 16384", "8192, 16384 or 32768", "32768" };
    snprintf(why, wl, "%u bytes, needs %s", (unsigned)fs_size32(n), want[which]);
}

static const char *kit_fault(int which, const uint8_t *d, uint32_t n)
{
    if (which == KIT_C128) {
        if (n == 2 * IMG_PART) {
            const char *f = jf_part_fault(d, IMG_PART, true);
            return f ? f : jf_part_fault(d + IMG_PART, IMG_PART, false);
        }
        return jf_part_fault(d, IMG_PART, false);
    }
    if (which == KIT_C64) {
        return jf_part_fault(d, n, true);
    }
    return all_same(d, n) ? "it is empty (all 0x00 or all 0xFF)" : 0;
}

// The selected file, read whole and checked. 0: `buf` (KIT_MAX bytes) holds `*n` bytes of the right role; else `why` says what is wrong.
static int kit_read(CoreCtx *c, int which, const char *path, uint8_t *buf, uint32_t *n, char *why, int wl)
{
    CoreFile *f = c->fs->open(path, false);
    if (!f) {
        snprintf(why, wl, "cannot open the file");
        return 1;
    }
    uint64_t size = f->size();
    if (!kit_size_ok(which, size)) {
        c->fs->close(f);
        kit_size_why(which, size, why, wl);
        return 1;
    }
    bool ok = f->read(0, buf, (uint32_t)size);
    c->fs->close(f);
    if (!ok) {
        snprintf(why, wl, "cannot read the file (the medium is gone?)");
        return 1;
    }
    const char *bad = kit_fault(which, buf, (uint32_t)size);
    if (bad) {
        snprintf(why, wl, "%s", bad);
        return 1;
    }
    *n = (uint32_t)size;
    return 0;
}

static const char *base_name(const char *p)
{
    const char *b = strrchr(p, '/');
    return b ? b + 1 : p;
}

// The selection (the setting's operation, from the browser's file action and from the settings row's file selector): the file is checked when it is chosen
// and refused with the reason; kept, its path is in the .cfg and the machine takes the file at its next start with ROM set JiffyDOS. "" clears it.
static int use_kit(CoreCtx *c, const OpArgs &a, int which, OpOut &o)
{
    const char *path = a.s(0);
    int idx = c->settings ? c->settings->find(kit_key[which]) : -1;
    if (idx < 0) {
        snprintf(o.msg, o.msglen, "The settings are not loaded yet");
        return OPE_FAILED;
    }
    if (path[0]) {
        static uint8_t buf[KIT_MAX];
        uint32_t n;
        char why[200];
        if (kit_read(c, which, path, buf, &n, why, sizeof(why))) {
            snprintf(o.msg, o.msglen, "%s: %s: %s", kit_key[which], base_name(path), why);
            return OPE_FAILED;
        }
    }
    bool ok = c->settings->set_text(idx, path);
    if (strcmp(c->settings->text(idx), path) != 0) {
        c->settings->set_text(idx, "");
        snprintf(o.msg, o.msglen, "%s: the path is too long to keep", kit_key[which]);
        return OPE_FAILED;
    }
    if (path[0]) {
        snprintf(o.msg, o.msglen, "%s: %s selected, used from the next start with ROM set JiffyDOS", kit_key[which], base_name(path));
    } else {
        snprintf(o.msg, o.msglen, "%s: no file selected", kit_key[which]);
    }
    return ok ? OPE_OK : OPE_IO;
}
static int op_use_jf_c128(CoreCtx *c, const OpArgs &a, OpOut &o) { return use_kit(c, a, KIT_C128, o); }
static int op_use_jf_c64(CoreCtx *c, const OpArgs &a, OpOut &o) { return use_kit(c, a, KIT_C64, o); }
static int op_use_jf_1541(CoreCtx *c, const OpArgs &a, OpOut &o) { return use_kit(c, a, KIT_1541, o); }
static int op_use_jf_1581(CoreCtx *c, const OpArgs &a, OpOut &o) { return use_kit(c, a, KIT_1581, o); }
static const ArgSpec kit_arg[] = { ARG_STRING("path", FS_PATH_MAX - 1) };

static bool keep_bytes(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    bool differs = memcmp(dst, src, n) != 0;
    memcpy(dst, src, n);
    return differs;
}

static void say(char *msg, int msglen, const char *a, const char *b, const char *c)
{
    int w = (int)strlen(msg);
    if (w < msglen) {
        snprintf(msg + w, msglen - w, "%s%s%s%s", w ? "; " : "", a, b, c);
    }
}

// The start with ROM set JiffyDOS: the selected files are read (the files are checked again, they may be gone or changed) into the kit's regions and the
// drives' JiffyDOS ROMs. Nothing selected: one line. What is missing or refused is in `msg`; the C128 part is the one that is needed, without it the whole
// image stays standard.
static void load_kits(CoreCtx *c, char *msg, int msglen)
{
    C128State *s = c128_state(c);
    static uint8_t buf[KIT_MAX];
    char why[200];
    const char *path[KIT_N];
    bool have[KIT_N];
    uint32_t n[KIT_N];
    int nsel = 0;
    bool changed = false;
    bool c128ok = false, c64ok = false;
    uint32_t c64off = 0;
    for (int k = 0; k < KIT_N; k++) {
        int idx = c->settings ? c->settings->find(kit_key[k]) : -1;
        path[k] = idx >= 0 ? c->settings->text(idx) : "";
        have[k] = false;
        n[k] = 0;
        if (!path[k][0]) {
            continue;
        }
        nsel++;
        if (kit_read(c, k, path[k], buf, &n[k], why, sizeof(why))) {
            say(msg, msglen, kit_key[k], ": ", why);
            continue;
        }
        have[k] = true;
        if (k == KIT_C128) {
            if (n[k] == 2 * IMG_PART) {
                changed |= keep_bytes(s->jf, buf, IMG_PART);
                c64ok = true;
                c64off = 0;
            }
            changed |= keep_bytes(s->jf + IMG_PART, buf + (n[k] == 2 * IMG_PART ? IMG_PART : 0), IMG_PART);
            c128ok = true;
        } else if (k == KIT_C64) {
            uint32_t off = n[k] == IMG_PART ? 0 : IMG_PART - n[k];
            changed |= keep_bytes(s->jf + off, buf, n[k]);
            c64ok = true;
            c64off = off;
        } else {
            DriveRomPick &p = k == KIT_1541 ? s->d1541 : s->d1581;
            if (p.n_jiffy != n[k] || memcmp(p.jiffy_rom, buf, n[k]) != 0) {
                memcpy(p.jiffy_rom, buf, n[k]);
                p.n_jiffy = n[k];
                p.applied = DR_NONE;
            }
        }
    }
    for (int k = KIT_1541; k < KIT_N; k++) {
        DriveRomPick &p = k == KIT_1541 ? s->d1541 : s->d1581;
        if (!have[k] && p.n_jiffy) {
            p.n_jiffy = 0;
            p.applied = DR_NONE;
        }
    }
    if (c128ok != s->jf_c128 || c64ok != s->jf_c64 || (c64ok && c64off != s->jf_c64_off)) {
        changed = true;
    }
    s->jf_c128 = c128ok;
    s->jf_c64 = c64ok;
    s->jf_c64_off = c64ok ? c64off : 0;
    if (changed) {
        s->gen++;
    }
    if (nsel == 0) {
        snprintf(msg, msglen, "ROM set JiffyDOS: no file selected, the machine starts Stock");
        return;
    }
}

// ---- the drive ROMs (D2): the stock and the JiffyDOS ROM of each type are kept, drive A holds the one the ROM set chooses ------------
// The JiffyDOS ROMs come from the selected files (load_kits), which say what is missing; the drive's own line is a refusal of the bytes only.
static void apply_drives(CoreCtx *c, char *msg, int msglen)
{
    C128State *s = c128_state(c);
    char m1[OP_MSG_MAX], m2[OP_MSG_MAX];
    m1[0] = m2[0] = 0;
    drive_pick_apply(c, s->d1541, kind1541, s->romset == 1, m1, sizeof(m1));
    drive_pick_apply(c, s->d1581, kind1581, s->romset == 1, m2, sizeof(m2));
    if (msg && msglen) {
        snprintf(msg, msglen, "%s%s%s", m1, m1[0] && m2[0] ? "; " : "", m2);
    }
}

static int keep_drive(CoreCtx *c, DriveRomPick &p, const DriveRomKind &k, const uint8_t *d, uint32_t n, OpOut &o)
{
    return drive_pick_keep(c, p, k, d, n, false, c128_state(c)->romset == 1, o);
}
static int op_put_drive1541(CoreCtx *c, const OpArgs &a, OpOut &o) { return keep_drive(c, c128_state(c)->d1541, kind1541, a.p(0), a.len(0), o); }
static int op_put_drive1581(CoreCtx *c, const OpArgs &a, OpOut &o) { return keep_drive(c, c128_state(c)->d1581, kind1581, a.p(0), a.len(0), o); }
// The 1541 ROM of a file picked in the browser goes the same way as the one of the start (the library's own operation would bypass the choice).
static int op_set_drive_rom(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    C128State *s = c128_state(c);
    return drive_pick_keep_file(c, s->d1541, kind1541, a.s(0), s->romset == 1, o);
}

// The ROM set (WHEN_RESET): written in the hold. With JiffyDOS the selected files are read; the window gets the image the choice and the files make (only
// when it differs from what it holds), the drive the DOS ROMs. What the choice lacks is told in the message and on the screen's notice line; the machine
// starts either way, Stock where a file is missing. Standard reads no kit file and says nothing about them.
static int op_set_romset(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    C128State *s = c128_state(c);
    s->romset = a.i(0) ? 1 : 0;
    char all[OP_MSG_MAX];
    all[0] = 0;
    if (s->romset == 1) {
        load_kits(c, all, sizeof(all));
    }
    if (place(c, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    char dm[OP_MSG_MAX];
    apply_drives(c, dm, sizeof(dm));
    if (dm[0]) {
        int w = (int)strlen(all);
        snprintf(all + w, sizeof(all) - w, "%s%s", w ? "; " : "", dm);
    }
    if (all[0]) {
        snprintf(o.msg, o.msglen, "%s", all);
        snprintf(c->notice, sizeof(c->notice), "%s", all);
        c->notice_pending = true;
        printf("[c128] ROM set: %s\n", all);
    }
    return OPE_OK;
}
static const ArgSpec romset_arg[] = { ARG_INT("value", 0, 1) };

// The system image is the boot0.rom file, or VICE's six files (T258): the parts at their offsets in the image, found by VICE's names.
static const RomSpec roms[9] = {
    { "C128 system ROM image", { "boot0.rom", "c128.rom", "c128-system.rom", "c128_system.rom", 0 }, { C128_ROM_BYTES, 0 }, true, 0, "c128.put_rom" },
    { "1541 drive", { "1541.rom", "1541.bin", "1541-ii.bin", "1541-ii.rom", "1541ii.rom", "1541c.rom", "dos1541.rom", "dos1541-325302-01+901229-05.bin", 0 }, { 16384, 32768, 0 }, false, 0, "c128.put_drive_rom", 0, true },
    { "1581 drive", { "1581.rom", "1581.bin", "dos1581.rom", 0 }, { 32768, 0 }, false, 0, "c128.put_drive_rom1581", 0 },
    { "C64 BASIC", { "basic64-901226-01.bin", 0 }, { 8192, 0 }, false, 0, 0, 0, false, 1, 0x00000 },
    { "C64 KERNAL", { "kernal64-901227-03.bin", 0 }, { 8192, 0 }, false, 0, 0, 0, false, 1, 0x02000 },
    { "C128 editor and KERNAL", { "kernal-318020-05.bin", 0 }, { 16384, 0 }, false, 0, 0, 0, false, 1, 0x04000 },
    { "C128 BASIC low", { "basiclo-318018-04.bin", 0 }, { 16384, 0 }, false, 0, 0, 0, false, 1, 0x08000 },
    { "C128 BASIC high", { "basichi-318019-04.bin", 0 }, { 16384, 0 }, false, 0, 0, 0, false, 1, 0x0C000 },
    { "Character ROM", { "chargen-315079-01.bin", 0 }, { 8192, 0 }, false, 0, 0, 0, false, 1, 0x10000 },
};

// ---- control -------------------------------------------------------------------------------------------------------------------
static int c128_init(CoreCtx *c, char *, int)
{
    cbm_run_clear(&c128_state(c)->run);                 // a reset ends a typed start that was still waiting for READY
    c->io->mode_reset(true);
    return 0;
}

// The core resets MON_SEL with the machine; the setting says which picture the person chose, so it is written again before the release.
static int c128_release(CoreCtx *c, char *, int)
{
    int i = c->settings ? c->settings->find(KEY_MON) : -1;
    int v = (i >= 0 && c->settings->value(i) == 1) ? 1 : 0;
    // The VIC picture's capture is read by act_hash(): STILL_BASE (a 32 bit register, byte 3 commits) goes to the hole in the application's DDR2 window.
    for (int b = 0; b < 4; b++) {
        c->io->wr(C128_STILL_BASE + (uint32_t)b, (uint8_t)(C128_STILL_DDR >> (8 * b)));
    }
    c->io->wr(C128_MON_SEL, (uint8_t)v);
    follow_picture(c, v);
    { C128State *st = c128_state(c); st->auto_seen = -1; act_reset(st); st->shown = v; st->sw_ms = c->io->now_ms(); }
    c->io->wr(C128_DBG_CLR, 1);                         // the frame meter starts from here (the first frames after a reset are not counted either)
    c->io->mode_reset(false);
    return 0;
}

// The cold start of the machine (T113 5.6): the core's COLD line resets every register of the machine - CPU, Z80, MMU, VIC-IIe with its raster
// phase, VDC, CIAs, SIDs, the key layer and this page's settings - like a power-on, and releases at the frame phase of the power-on start,
// so the host scaler's lock still fits. What it does not reach: the contents of the RAM blocks and of the VDC's RAM (block RAM keeps its bits;
// the restart clears the 64 KiB block the RAM window reaches, `cold_ram_bytes`) and the ROM image in the DDR2.
// The page's settings are back at their reset values after it; the restart writes them again (the shared sequence does, regset_apply(all)).
static int c128_cold_start(CoreCtx *c, char *msg, int msglen)
{
    c->io->wr(C128_COLD, 0x01);
    uint32_t t0 = c->io->now_ms();
    for (;;) {
        c->io->delay_ms(2);
        if (!(c->io->rd(C128_COLD) & 0x01)) {
            break;
        }
        if (c->io->now_ms() - t0 > C128_COLD_MAX_MS) {
            snprintf(msg, msglen, "C128 cold start: the core did not finish it in %d ms", C128_COLD_MAX_MS);
            return 1;
        }
    }
    { C128State *st = c128_state(c); st->auto_seen = -1; act_reset(st); st->sw_ms = c->io->now_ms(); }
    c128_state(c)->mon_seen = -1;                       // MON_SEL is 0 again: the picture layer follows the register at the next service
    return 0;
}

// The service: the person can switch the picture with the monitor-switch key (the core toggles MON_SEL), so the layer follows the register.
static uint32_t rd24(CoreCtx *c, uint32_t r, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        v |= (uint32_t)c->io->rd(r + (uint32_t)i) << (8 * i);
    }
    return v;
}

// The page-E registers cannot be read over JTAG (only pages 0, 1 and 5 of the IO block are: host/top.v), so the counters of the ROM line cache and of the
// VIC's frame length go to the console log, at most every 2 s and only when one moved: "[c128] rom: hits H, fetches F, CPU cycles held W, longest fetch L
// clocks; VIC frame min..max clocks" (a frame is 628992 clocks in PAL when nothing stops the VIC).
static void debug_line(CoreCtx *c)
{
    C128State *s = c128_state(c);
    uint32_t t = c->io->now_ms();
    if (s->dbg_t != 0 && (uint32_t)(t - s->dbg_t) < 2000) {
        return;
    }
    s->dbg_t = t ? t : 1;
    uint32_t h = rd24(c, C128_ROM_HITS, 4), m = rd24(c, C128_ROM_MISSES, 4), w = rd24(c, C128_ROM_WAITS, 4), fm = rd24(c, C128_ROM_FILLMAX, 2);
    uint32_t fmin = rd24(c, C128_FR_MIN, 3), fmax = rd24(c, C128_FR_MAX, 3);
    if (h != s->dbg_h || m != s->dbg_m || w != s->dbg_w || fmin != s->dbg_fmin || fmax != s->dbg_fmax) {
        s->dbg_h = h; s->dbg_m = m; s->dbg_w = w; s->dbg_fmin = fmin; s->dbg_fmax = fmax;
        printf("[c128] rom: hits %u, fetches %u, CPU cycles held %u, longest fetch %u clocks; VIC frame %u..%u clocks\n",
               (unsigned)h, (unsigned)m, (unsigned)w, (unsigned)fm, (unsigned)fmin, (unsigned)fmax);
    }
}

// T206: what the typed start waits for and types into. The C128 in 128 mode (a reset always starts it there: nothing holds the C= key; in C64 mode BASIC's
// TXTTAB would be $0801 and the start would be dropped with the "did not reach READY" notice): BASIC 7 text starts at $1C01 and TXTTAB is the pointer pair
// $2D/$2E (VARTAB $2F, ARYTAB $31, STREND $33), the keyboard buffer KEYD is $034A (ten keys) with its count NDX at $D0 - the C128 Programmer's Reference
// Guide's memory map; no C128 KERNAL source is in this repository to read them from, so they are unverified until the board step. ready_ms is not measured: the first look at READY is
// taken 2 s after the reset (the 128-mode boot with the 40/80 init and the RAM test is slower than the C64's), 500 ms settle; Alex's board step S3 measures it.
// T217 P3 / D4: a program file is started in 128 mode (the mode a reset always starts in), except a file with the C64's load address $0801 (BASIC or a C64
// ML file placed there): that one is started in C64 mode (cbm_run.cc service_c64: GO64 typed at the 128 READY, the answer Y, the status register's b0 says C64 mode,
// then the C64's own READY, then placement and RUN as on a C64). "Load" (no start) of such a file stays in 128 mode and is refused with the reason.
static const char *refuse_c64_basic(CoreCtx *, uint32_t load, uint32_t)
{
    if (load == 0x0801) {
        return "This is a C64 program (load address $0801): use Load + run, which starts it in C64 mode";
    }
    return 0;
}

static CbmRunMachine machine(void)
{
    CbmRunMachine m;
    memset(&m, 0, sizeof(m));
    m.tag = "c128";
    m.refuse = refuse_c64_basic;
    c128_prg_params(&m.prg);
    m.c64_load = 0x0801;
    c128_prg_params_c64(&m.c64_prg);
    m.switch_text = C128_SWITCH_TEXT;
    m.ask_screen = 0x0400;                              // the 40-column screen; in 80 columns the question is on the VDC and cannot be read: the blind waits
    m.ask = c128_go64_ask;
    m.ask_len = sizeof(c128_go64_ask);
    m.ask_blind_ms = 1500;
    m.ready_blind_ms = 6000;                           // the boot attempt after the banner takes 4-5 s on the board (Alex 2026-11-16), plus 1 s
    m.mode_reg = C128_STATUS;
    m.mode_mask = 0x01;                                 // C128_STATUS b0: C64 mode
    m.c64_screen = 0x0400;
    m.c64_ready_ms = 1500;
    m.ready_ms = 2000;
    m.settle_ms = 500;
    m.giveup_ms = 6000;
    return m;
}

// T217: "Load" / "Load + run" of a .prg, and Mount & Run of a file inside a disk image (run_op): the shared cbm_run job (PRG library, keyboard buffer)
static int op_run(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return cbm_run_queue(c, &c128_state(c)->run, machine(), a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static int op_load(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return cbm_load_now(c, &c128_state(c)->run, machine(), a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

static int op_type(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return cbm_run_queue_script(c, &c128_state(c)->run, machine(), a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static const ArgSpec text_arg[] = { ARG_STRING("text", 63) };


// T113 D11, T257: the VDC's buffers are 640 x 480, 480 rows when FB_LATEST b2 is set (the newest complete picture has all 480 rows: a woven pair, or a frame of more than 200 lines that is not
// interlaced), else 400 rows (200 lines doubled).
// The layer follows the newest picture; the height is the layer's geometry, so the layout (Picture, HDMI mode) is made for it.
static void c128_rows(CoreCtx *c)
{
    uint8_t l = c->io->rd(C128_FB_LATEST);
    if (l & 0x80) {
        c->video->set_rows((l & 4) ? 480u : 400u);
    }
}

static int op_service(CoreCtx *c, const OpArgs &, OpOut &o)
{
    int rc = cbm_run_service(c, &c128_state(c)->run, machine());
    if (c->started) {
        debug_line(c);
        int v = c->io->rd(C128_MON_SEL) & 1;
        if (v != c128_state(c)->mon_seen) {
            follow_picture(c, v);
        }
        if (c->video && c->video->picture() == 0) {
            c128_rows(c);
        }
        if (auto_on(c)) {
            auto_follow(c);
        }
    }
    o.more = cbm1541_service(c);
    return rc;
}

// ---- menu ----------------------------------------------------------------------------------------------------------------------
static const OpBind file_bind[] = { B_FILE("path") };
// T185 D4: the JiffyDOS kit files are chosen here or in the settings; the file is checked when chosen and refused with the reason
#define KIT_ACTIONS \
    { "Use as JiffyDOS C128 ROM", "c128.use_jiffy_c128", file_bind, 1, false, 0, 0 }, \
    { "Use as JiffyDOS C64 ROM",  "c128.use_jiffy_c64",  file_bind, 1, false, 0, 0 }, \
    { "Use as JiffyDOS 1541 ROM", "c128.use_jiffy_1541", file_bind, 1, false, 0, 0 }, \
    { "Use as JiffyDOS 1581 ROM", "c128.use_jiffy_1581", file_bind, 1, false, 0, 0 }
static const FileAction rom_actions[] = {
    { "Load as system ROM image (72 KiB)", "c128.load_rom", file_bind, 1, true, 0, 0 },
    { "Set as 1541 ROM", "c128.set_drive_rom", file_bind, 1, false, 0, 0 },
    KIT_ACTIONS,
};
static const FileAction bin_actions[] = { KIT_ACTIONS };
static const FileAction prg_actions[] = {
    { "Load", "c128.load_prg", file_bind, 1, false, 0, 0 },
    { "Load + run", "c128.run_prg", file_bind, 1, true, 0, 0 },
};
// T206: the disk actions are the library's (cbm1541_desc.h): the same names and order as in every core with a drive. T217 P4: the files inside the images get
// Mount & Run (run_op) and Real Run (type_op).
static const FileType filetypes[7] = {
    { ".rom", rom_actions, 6, 0, 0, 0 },
    { ".bin", bin_actions, 4, 0, 0, 0 },
    { ".prg", prg_actions, 2, 0, 0, 0 },
    { ".p00", prg_actions, 2, 0, 0, 0 },
    { ".d64", c1541_d64_actions, 5, ".prg", c1541_inner_actions, 2 },
    { ".g64", c1541_g64_actions, 4, 0, 0, 0 },
    { ".d81", c1581_disk_actions, 4, ".prg", c1541_inner_actions, 2 },
};

static const Task tasks[2 + CBM1541_NTASKS + CBM1581_NTASKS] = {
    { "Reset", "machine.cold_reset", 0, 0, true },
    { "Reload ROMs, reset", "machine.reload_roms", 0, 0, true },
    CBM1541_TASKS,
    CBM1581_TASKS,
};

static const MediaSlot media[] = {
    CBM1541_1581_MEDIA,
};

static void c128_register(OpRegistry &r)
{
    r.contributor("c128");
    r.add_op("c128.put_rom", rom_arg, 1, op_put_rom);
    r.add_op("c128.load_rom", path_arg, 1, op_load_rom);
    r.add_op("c128.put_drive_rom", rom_arg, 1, op_put_drive1541);
    r.add_op("c128.put_drive_rom1581", rom_arg, 1, op_put_drive1581);
    r.add_op("c128.use_jiffy_c128", kit_arg, 1, op_use_jf_c128);
    r.add_op("c128.use_jiffy_c64", kit_arg, 1, op_use_jf_c64);
    r.add_op("c128.use_jiffy_1541", kit_arg, 1, op_use_jf_1541);
    r.add_op("c128.use_jiffy_1581", kit_arg, 1, op_use_jf_1581);
    r.add_op("c128.set_drive_rom", path_arg, 1, op_set_drive_rom);
    r.add_op("c128.set_romset", romset_arg, 1, op_set_romset);
    r.add_op("c128.set_monitor", mon_arg, 1, op_set_monitor);
    r.add_op("c128.set_auto", onoff_arg, 1, op_set_auto);
    r.add_op("c128.set_auto_time", autot_arg, 1, op_set_auto_time);
    r.add_op("c128.toggle_picture", 0, 0, op_toggle_picture);
    r.add_op("c128.set_d4080", mon_arg, 1, op_set_d4080);
    r.add_op("c128.set_joy", mon_arg, 1, op_set_joy);
    r.add_op("c128.set_dset", mon_arg, 1, op_set_dset);
    r.add_op("c128.set_sysmode", sysmode_arg, 1, op_set_sysmode);
    r.add_op("c128.service", 0, 0, op_service);
    r.add_op("c128.type_script", text_arg, 1, op_type);
    r.add_op("c128.run_prg", path_arg, 1, op_run);
    r.add_op("c128.load_prg", path_arg, 1, op_load);
    r.add_state("c128.auto_diag", state_auto_diag);
    cbm1541_register(r);
}

static const char *const caps[] = { "iec", "drive_a", "iec_trace" };

// T141 P2: the USB keyboard's extra keys. The application hands the usages down (rawkeys.h, held mode); c128_xkeys_apply writes XKEYS / XSPECIAL.
static void c128_keys_held(CoreCtx *c, const uint8_t *usages, int n)
{
    c128_xkeys_apply(c->io, usages, n);
}
static const RawKeySpec c128_rawkeys = { 0, 0, 0, 0, 0xFF, 0, c128_keys_held };

// T194: the VIC-IIe's picture is the gateware's capture of the VIC's window (400 x 270, 60 Hz family 400 x 240), shown by the pixel layer in its format 3 (4 bit through
// the palette, rows of 13 words = 208 bytes, no buffers: the layer takes the newest complete one itself). scale 1 and x0 / y0 are only its size and centre in 1920 x 1080 (the layer's b5:4 are not read); the layout makes the rest.
static const VideoDesc c128_vic = { 400, 270, 1, 760, 405, 0, 0, 0, 0, 0, 0, 0, 0, 3, 240 };

// T181 + T194 D12: the CBM map and, as the core's own function, CTRL + D (D = DISPLAY) flips the shown picture 40 / 80.
#define HKK_D 18
static const HkSpec hk_c128 = {
    { HKK_CTRL, HK_NONE, { HK_RESTORE, HKK_PLUS, HKK_MINUS, HKK_R, HKK_J, HKK_P, HKK_D } }, 0, 0, HKO_DIGITS,
    "c128.toggle_picture", "Hotkey picture 40/80", "Picture 40 / 80" };

extern const CoreDesc c128_desc;
const CoreDesc c128_desc = {
    { 'C', '1', '2', '8' },
    "Commodore 128",
    "Commodore 128",
    "C128",
    "c128",
    "C128 Settings",
    1,
    roms, 9,
    settings, 12 + CBM1541_NSETTINGS,
    tasks, 2 + CBM1541_NTASKS + CBM1581_NTASKS,
    filetypes, 7,
    media, 1,
    // the VDC's picture: 640 x 480 is the largest (interlace woven, T113 D11, or a frame of more than 200 lines, T257), shown 2 x 2 (1280 x 960) centred on 1920 x 1080;
    // a frame of 200 lines without interlace shows 400 rows (c128_rows, 1280 x 800 as before); the buffers are the core's reset values of FB_BASE / FB_STRIDE
    { 640, 480, 2, 320, 60, C128_FB_ADDR, C128_FB_STRIDE_BYTES, 3, C128_FB_LATEST, C128_FRAME_SEQ, C128_FB_BASE, C128_FB_STRIDE },
    { C128_STATUS, C128_ST_HELD, 200, false },
    c128_init, c128_release, 0, 50,
    "c128.service", 20, false,
    0, 0,
    c128_register,
    caps, 3,
    false, false, 0, 0, 0, 0, 0, 0, 0,
    "c128.type_script",                                 // type_op (T206: Run Disk)
    "c128.run_prg",                                     // run_op (T217: Load + run, and Mount & Run of a file inside a disk image)
    65536,                                              // cold_ram_bytes: RAM block 0, what the RAM window reaches (block 1 and the VDC's RAM it does not)
    // TODO(T113 5.6, coordinator at merge): once CoreDesc has the hook, add  .cold_start = c128_cold_start  here (the descriptor is positional: the new field's slot)
    0, 0, 0, 0,                                // cold_start, id_feat_reg, id_feat_mask, id_feat_val
    0, 0,                                      // menu_rec, menu_rec_n
    0,                                         // core_id
    0, 0,                                      // report_regs, n
    &c128_rawkeys, false, 0, 0, false,         // rawkeys (T141), own_volume, pad, live_ms, menu_first
    cbm1541_media_flush, cbm1541_media_pending,   // T180 D4: the drive's write-back
    &cbm1541_mount_slots,                      // T180 D2, D5: Drive A
    &hk_c128,                              // T181: the hotkey map with the 40/80 function (T194 D12)
    &c128_vic,                             // T194: the 40-column picture
    false, 0,                              // no_tone, rom_key
    "40/80 DISPLAY",                       // T212 D13: machine_info
};

// ---- the USB keyboard's extra keys ---------------------------------------------------------------------------------------------
// The extra matrix columns K0-K2 are $D02F bits 0-2 (low = selected); the rows are the CIA1 PB lines, PB0..PB7 = row 0..7. The cells are the
// C128's own keyboard matrix as I know it from the C128 Programmer's Reference Guide and VICE's C128 keymap, NOT read from a source in this
// repository: the 24 cells (and which row each key takes inside its column) stay unverified until a board run presses each key (T113 5.5
// "Open"). The USB codes are HID usages (page 0x07); which USB key stands for HELP, LINE FEED and NO SCROLL is our choice (Scroll Lock stays
// the application's menu key, F10 too).
struct XMap {
    uint8_t usage;
    int8_t col, row;
};
static const XMap xmap[] = {
    { 0x4B, 0, 0 },   // HELP (USB Page Up)
    { 0x60, 0, 1 },   // keypad 8
    { 0x5D, 0, 2 },   // keypad 5
    { 0x2B, 0, 3 },   // TAB
    { 0x5A, 0, 4 },   // keypad 2
    { 0x5C, 0, 5 },   // keypad 4
    { 0x5F, 0, 6 },   // keypad 7
    { 0x59, 0, 7 },   // keypad 1
    { 0x29, 1, 0 },   // ESC
    { 0x57, 1, 1 },   // keypad +
    { 0x56, 1, 2 },   // keypad -
    { 0x4E, 1, 3 },   // LINE FEED (USB Page Down)
    { 0x58, 1, 4 },   // keypad ENTER
    { 0x5E, 1, 5 },   // keypad 6
    { 0x61, 1, 6 },   // keypad 9
    { 0x5B, 1, 7 },   // keypad 3
    { 0xE2, 2, 0 },   // ALT (left)
    { 0xE6, 2, 0 },   // ALT (right)
    { 0x62, 2, 1 },   // keypad 0
    { 0x63, 2, 2 },   // keypad .
    { 0x52, 2, 3 },   // cursor up
    { 0x51, 2, 4 },   // cursor down
    { 0x50, 2, 5 },   // cursor left
    { 0x4F, 2, 6 },   // cursor right
    { 0x48, 2, 7 },   // NO SCROLL (USB Pause)
};

bool c128_xkey_for(uint8_t usage, C128XKey *out)
{
    for (unsigned i = 0; i < sizeof(xmap) / sizeof(xmap[0]); i++) {
        if (xmap[i].usage == usage) {
            if (out) {
                out->col = xmap[i].col;
                out->row = xmap[i].row;
            }
            return true;
        }
    }
    return false;
}

uint8_t c128_xspecial_for(uint8_t usage)
{
    switch (usage) {
    case 0x39: return C128_XS_CAPSLK;     // CAPS LOCK
    case 0x44: return C128_XS_D4080;      // F11: 40/80 DISPLAY
    case 0x45: return C128_XS_MONITOR;    // F12: monitor switch
    default: return 0;
    }
}

void c128_xkeys_compose(const uint8_t *down, int n, uint8_t xkeys[3], uint8_t *xspecial)
{
    xkeys[0] = xkeys[1] = xkeys[2] = 0;
    uint8_t sp = 0;
    for (int i = 0; i < n; i++) {
        C128XKey k;
        if (c128_xkey_for(down[i], &k)) {
            xkeys[k.col] |= (uint8_t)(1u << k.row);
        }
        sp |= c128_xspecial_for(down[i]);
    }
    *xspecial = sp;
}

void c128_xkeys_apply(CoreIo *io, const uint8_t *down, int n)
{
    uint8_t xk[3], sp;
    c128_xkeys_compose(down, n, xk, &sp);
    for (int i = 0; i < 3; i++) {
        io->wr(C128_XKEYS + (uint32_t)i, xk[i]);
    }
    io->wr(C128_XSPECIAL, sp);
}
