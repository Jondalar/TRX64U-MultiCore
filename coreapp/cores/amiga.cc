// The Amiga (amiga) and Amiga 1200 (amiga1200) descriptors (T104 5.5): two records from ONE source with a named override list. Everything the
// machines do is a record the base serves - the Kickstart as a rom_set (romset.h), DF0-DF3 as window slots with a dirty log (winslot.h),
// HD0/HD1 as a sector server (sectors.h), the mouse as a sink (mousesink.h), the settings as bits of register bytes (regset.h). What is in
// this file is the machine's register map and values; the only code is the hold/release sequence and the names of the state values.
#include "hk_defaults.h"
#include "ctx.h"
#include "memops.h"
#include "cores.h"
#include "boot.h"
#include "engines.h"
#include "winslot.h"
#include "newdisk.h"
#include "picture.h"
#include "amiga_regs.h"
#include "amiga_keys.h"
#include "rawkeys.h"
#include "pad.h"
#include <stdio.h>
#include <string.h>

// ---- the override list: the five fields in which the A1200 differs from the A500 (T104 5.5), plus its identity -----------------------
struct AmigaVariant {
    const char *core_id;                                   // core.toml id (start.cc matches the hand-over's id with it before it looks at the name)
    const char *cor_name, *title, *short_title, *store;   // identity; the store is the .cfg section, so the A500's saved OCS never becomes the A1200's
    const char *auto_label;                                // ADR-0056: which Kickstart Auto looks for
    uint16_t want;
    int nchipset;                                          // OCS ECS (AGA)
    const char *const *chipset_names;
    int chipset_def;
    int nfast;                                             // None 2M 4M (5.5M, 32M Zorro III)
    const char *const *fast_names;
    int chip_def, slow_def;
    bool fbwin;                                            // the gateware has the display window and the crop window registers (T97 5.10)
    bool rgb32;                                            // T136: 32-bit pixels, buffers at DDR2 0x4000000, 2 MB apart (the A1200 build's pixel layer)
    bool led_load;                                         // T159: the setting "LED Load Indicator" (case LED 1 = the gateware's drive activity: motor + hard disk)
    bool usbkeys;                                          // T141: the gateware has RAWKEY (both Amiga builds, the shared amiga_input.v): a USB keyboard types into the machine
};

static const char *const chipset_a500[]  = { "OCS", "ECS" };
static const char *const chipset_a1200[] = { "OCS", "ECS", "AGA" };
static const char *const fast_a500[]     = { "None", "2M", "4M" };
static const char *const fast_a1200[]    = { "None", "2M", "4M", "5.5M", "32M (Zorro III)" };   // T138: the last is FAST_CFG 4, no Zorro II cards

// T162 D3: the state block of the bug report (reads without side effects only; the debug-log POP register is left out on purpose)
static const ReportReg report_regs[] = {
    { "FEAT", AM_FEAT, 1 }, { "CTRL (b0 hold, b1 video off)", AM_CTRL, 1 }, { "MEM_CFG (chip, slow)", AM_MEM_CFG, 1 },
    { "FAST_CFG", AM_FAST_CFG, 1 }, { "CPU_CFG", AM_CPU_CFG, 1 }, { "CHIPSET_CFG", AM_CHIPSET_CFG, 1 },
    { "FLOPPY_CFG", AM_FLOPPY_CFG, 1 }, { "IDE_CFG", AM_IDE_CFG, 1 }, { "JOY_CFG", AM_JOY_CFG, 1 },
    { "RTC_EN", AM_RTC_EN, 1 }, { "RTC_STAT (b0 loaded)", AM_RTC_STAT, 1 },
    { "STATUS (b6 paused)", AM_STATUS, 1 }, { "FB_LATEST", AM_FB_LATEST, 1 }, { "FRAME_SEQ", AM_FRAME_SEQ, 1 },
    { "MBOX_STATUS", AM_MBOX_STATUS, 1 }, { "DLOG_STATUS", AM_DLOG_STATUS, 1 },
};

static const AmigaVariant variants[2] = {
    { "amiga", "Amiga (OCS/ECS, 68000)", "Amiga 500", "AMIGA", "Amiga Settings", "Auto (1.3 first)", KS_VER_13, 2, chipset_a500, 0, 3, fast_a500, 0, 1, false, false, true, true },
    { "amiga1200", "Amiga 1200 (AGA, 68020)", "Amiga 1200", "A1200", "Amiga 1200 Settings", "Auto (3.1 first)", KS_VER_31, 3, chipset_a1200, 2, 5, fast_a1200, 3, 0, true, true, true, true },
};

// ---- the common record ---------------------------------------------------------------------------------------------------------------
static const char *const chip_names[]   = { "512K", "1M", "1.5M", "2M" };
static const char *const slow_names[]   = { "None", "512K", "1M", "1.5M" };
static const char *const floppy_names[] = { "1", "2", "3", "4" };
static const char *const no_yes[]       = { "No", "Yes" };
static const char *const joy_names[]   = { "Normal", "Swapped" };
static const char *const led_names[]   = { "On", "Off" };
static const char *const rtc_names[]   = { "Off", "On" };
static const char *const pad2_names[]  = { "Off", "On" };
static const char *const pad2_type_names[] = { "Joystick 1 button", "Joystick 2 buttons", "CD32 gamepad" };
// T190: the USB driver's sensitivity 1 .. 16 (8 = 1:1) and acceleration, as Gideon's items offer them (u64_config.cc CFG_MOUSE_SENSITIVITY 1 .. 16 default 8, CFG_MOUSE_ACCELERATION Off / Adaptive default Off)
static const uint8_t chipset_bytes[]    = { 0x00, AM_CHS_ECS, AM_CHS_AGA };     // value -> CHIPSET_CFG

static const RegBind b_chip    = { AM_MEM_CFG, 0, 3, 0 };         // chip [1:0] and slow [3:2] share MEM_CFG
static const RegBind b_slow    = { AM_MEM_CFG, 2, 3, 0 };
static const RegBind b_fast    = { AM_FAST_CFG, 0, 0xFF, 0 };
static const RegBind b_chipset = { AM_CHIPSET_CFG, 0, 0xFF, chipset_bytes };
static const RegBind b_nfl     = { AM_FLOPPY_CFG, 2, 3, 0 };      // drives - 1 [3:2] and write protect [1] share FLOPPY_CFG
static const RegBind b_fwp     = { AM_FLOPPY_CFG, 1, 1, 0 };
static const RegBind b_rtc    = { AM_RTC_EN, 0, 1, 0 };          // T191: a register of its own, no commit toggle
static const RegBind b_joy     = { AM_JOY_CFG, 0, 1, 0 };         // T157: a register of its own, no commit toggle; the byte is the swap bit
// (VOLUME_KEY: the menu's SHIFT+ / SHIFT- step this one setting, menuctl.cc)

static const RegWrite reset_writes[] = { { AM_CPU_CFG, 0x00 }, { AM_IDE_CFG, 0x21 } };

static const char *const KS_KEY = "Kickstart file (at reload)";

static const WindowMediaSpec floppies = {
    "DF", 4, AM_W_ADF, AM_ADF_STRIDE, AM_ADF_SIZE, AM_FLOPPY_MOUNT, 0x10, AM_ADF_TRACK_BYTES, AM_ADF_TRACKS,
    AM_DLOG_STATUS, AM_DLOG_TRACK, AM_DLOG_DRIVE, AM_DLOG_POP, 1000, 5000,
};
static const SectorMediaSpec hard_disks = {
    "HD", 2, AM_HDF_MOUNT, 8, AM_FEAT, 0x01, AM_MBOX_STATUS, AM_MBOX_LBA, AM_MBOX_DONE, AM_W_STAGE, 1000,
};
static const RawKeySpec rawkeys = { AM_RAWKEY, amk_hid, (uint8_t)sizeof(amk_hid), amk_mod, AMK_CAPS_USAGE, AMK_CAPS_CODE };   // T141
static const MouseSinkSpec mouse = { AM_MOUSE_DX, AM_MOUSE_DY, AM_MOUSE_BTN, 4000, 127, 2, MOUSE_COUNTERS };
// The picture window (fbwin.h): the full frame (the defaults below) centred on the middle of the display window the chipset has, smaller and still centred where it does not fit. 2 hires pixels per lores unit, origin 182, and 27 lines are what
// cores/amiga/sim/run_fbwin.sh measured on the stripes picture; the stream holds 753 pixels and 286 lines (PAL) after its blanking; the
// defaults are the fixed window of amiga_fb (36, 10, 90 words, 270 lines).
static const FbWinSpec picture_window = {
    AM_FEAT, AM_FEAT_FBWIN, AM_CTRL, AM_VIDEO_OFF, AM_DIW_H, AM_DIW_V, AM_FB_SKIP_X, AM_FB_SKIP_Y, AM_FB_COLS, AM_FB_LINES,
    2, 182, 27, 753, 286, 36, 10, 90, 270, 100, 60, 1000, 2000,
};

static const OpBind df0[] = { B_INT("slot", 0) }, df1[] = { B_INT("slot", 1) }, df2[] = { B_INT("slot", 2) }, df3[] = { B_INT("slot", 3) };
static const OpBind hd0[] = { B_INT("unit", 0) }, hd1[] = { B_INT("unit", 1) };
static const OpBind mdf0[] = { B_FILE("path"), B_INT("slot", 0) }, mdf1[] = { B_FILE("path"), B_INT("slot", 1) },
                    mdf2[] = { B_FILE("path"), B_INT("slot", 2) }, mdf3[] = { B_FILE("path"), B_INT("slot", 3) };
static const OpBind mhd0[] = { B_FILE("path"), B_INT("unit", 0) }, mhd1[] = { B_FILE("path"), B_INT("unit", 1) };

static const OpBind new_adf[] = { B_TEXT("path"), B_STR("format", "adf"), B_STR("name", "") };
// T128: a folder as a hard disk. The menu asks for the folder (the browser of a Create task), then for the size in GB; the text it hands over is "<folder>/<GB>".
static const OpBind mfold0[] = { B_TEXT("path"), B_INT("unit", 0) }, mfold1[] = { B_TEXT("path"), B_INT("unit", 1) };

static const Task tasks[] = {
    { "Reset Amiga",             "machine.cold_reset",  0, 0,    true },      // reloads the Kickstart when the choice names another file
    { "Reload Kickstart, Reset", "machine.reload_roms", 0, 0,    true },
    { "Eject DF0", "window.eject",  df0, 1, false }, { "Eject DF1", "window.eject", df1, 1, false },
    { "Eject DF2", "window.eject",  df2, 1, false }, { "Eject DF3", "window.eject", df3, 1, false },
    { "Eject HD0", "sectors.eject", hd0, 1, false }, { "Eject HD1", "sectors.eject", hd1, 1, false },
    { "Create blank ADF", "disk.create", new_adf, 3, false, "Name of the new ADF", "disk.suggest.adf", true },
    { "Mount folder as HD0", "sectors.mount_folder", mfold0, 2, false, "Size of the disk in GB", "sectors.folder.offer", true, "Use this folder" },
    { "Mount folder as HD1", "sectors.mount_folder", mfold1, 2, false, "Size of the disk in GB", "sectors.folder.offer", true, "Use this folder" },
    { "Map port 2 buttons", "menu.pad_map_port", 0, 0, false, 0, 0, false, 0, "input" },   // T225 D3: a task (shown on the Input tab, T226), not a setting: it opens a dialog
};

static const OpBind rdf0[] = { B_FILE("path") };
static const FileAction adf_actions[] = {
    { "Mount DF0", "window.mount", mdf0, 2, false }, { "Mount DF0 and reset", "amiga.mount_reset", rdf0, 1, true },
    { "Mount DF1", "window.mount", mdf1, 2, false },
    { "Mount DF2", "window.mount", mdf2, 2, false }, { "Mount DF3", "window.mount", mdf3, 2, false },
};
static const FileAction hdf_actions[] = {
    { "Mount HD0", "sectors.mount", mhd0, 2, false }, { "Mount HD1", "sectors.mount", mhd1, 2, false },
};
static const FileType filetypes[] = { { ".adf", adf_actions, 5 }, { ".hdf", hdf_actions, 2 } };

static const MediaSlot media[] = {
    // T212 D5: name, state, eject, group, the types and the action that mounts them, the tasks of an empty / a mounted slot
    { "DF0", "amiga.df0.image", "window.eject", df0, 1, 0, ".adf", "Mount DF0", "Create blank ADF", 0 },
    { "DF1", "amiga.df1.image", "window.eject", df1, 1, 0, ".adf", "Mount DF1", "Create blank ADF", 0 },
    { "DF2", "amiga.df2.image", "window.eject", df2, 1, 0, ".adf", "Mount DF2", "Create blank ADF", 0 },
    { "DF3", "amiga.df3.image", "window.eject", df3, 1, 0, ".adf", "Mount DF3", "Create blank ADF", 0 },
    { "HD0", "amiga.hd0.image", "sectors.eject", hd0, 1, 0, ".hdf", "Mount HD0", "Mount folder as HD0", "Mount folder as HD0" },
    { "HD1", "amiga.hd1.image", "sectors.eject", hd1, 1, 0, ".hdf", "Mount HD1", "Mount folder as HD1", "Mount folder as HD1" },
};

// The contributor: the names of the slots as state values. Nothing else is the core's own.
// The service: the media engines, and every 10 s one console line with the audio and catch-up counters (STAT_SEL 8-11, FEATURES b1/b2)
// while the machine runs - the old Amiga application printed them; the A1200 board run of 2026-10-24 had no sound and nothing to read.
static uint32_t am_stat(CoreCtx *c, uint8_t sel)
{
    c->io->wr(AM_STAT_SEL, sel);
    return (uint32_t)c->io->rd(AM_STAT_VAL) | ((uint32_t)c->io->rd(AM_STAT_VAL + 1) << 8) | ((uint32_t)c->io->rd(AM_STAT_VAL + 2) << 16) |
           ((uint32_t)c->io->rd(AM_STAT_VAL + 3) << 24);
}

static int op_am_poll(CoreCtx *c, const OpArgs &, OpOut &o)
{
    static uint32_t last;
    o.more = engines_poll(c);
    uint32_t now = c->io->now_ms();
    if (c->started && !c->frozen && now - last >= 10000) {
        last = now;
        uint8_t feat = c->io->rd(AM_FEAT);
        // T188 S1/S2: the core's frame counter (amiga_regs 0x2A, 16 bit) and the HDMI output's (pix_layer n_frames, b31:16 of its 0xA4 status), read
        // back to back with the clock, for tools/t188_measure.py (f_core, f_out, the beat); both wrap at 65536
        uint32_t fc = (uint32_t)c->io->rd(AM_FRAMES) | ((uint32_t)c->io->rd(AM_FRAMES + 1) << 8);
        uint32_t fo = (uint32_t)c->io->rd(PX_NFRAMES) | ((uint32_t)c->io->rd(PX_NFRAMES + 1) << 8);
        printf("[amiga] frames core %u out %u at %u ms\n", (unsigned)fc, (unsigned)fo, (unsigned)now);
        if (feat & 0x02) {
            uint32_t ou = am_stat(c, 8), mm = am_stat(c, 9), fill = am_stat(c, 10), cu = (feat & 0x04) ? am_stat(c, 11) : 0;
            printf("[amiga] audio over %u under %u, fill now %u min %u max %u (target 480), catch-up lost %u ticks, deepest debt %u\n",
                   (unsigned)(ou >> 16), (unsigned)(ou & 0xFFFF), (unsigned)(fill & 0x7FF), (unsigned)(mm & 0x7FF), (unsigned)((mm >> 16) & 0x7FF),
                   (unsigned)(cu >> 16), (unsigned)(cu & 0xFFF));
        }
    }
    return OPE_OK;
}

// T206: "Mount DF0 and reset" - the disk goes into DF0 and the machine is reset, so the Kickstart boots from it (the Kickstart asks DF0 for a bootable disk
// at start). The mount is the base's (window.mount); the reset is boot_cold_reset, as "Reset Amiga" (it reloads the Kickstart when the choice names another file).
static int op_mount_reset(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    if (win_mount(c, 0, a.s(0), o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    return boot_cold_reset(c, o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static const ArgSpec mount_reset_arg[] = { ARG_PATH("path") };

// ---- T225: a USB gamepad as game port 2 ---------------------------------------------------------------------------------------------------
// The register word (AM_PAD2_WORD) is the pad service's word: bit n = button n of pad2_btn_names, the first 5 (joystick, one button), 6 (two buttons) or
// all 11 (CD32) of them by target. The service keeps one map per pad (VID:PID, pad.h by_id); the target type is stored with it.
static const char *const pad2_btn_names[11] = { "Up", "Down", "Left", "Right", "Fire (Red)", "Button 2 (Blue)", "Green", "Yellow", "Play", "Reverse (L)", "Forward (R)" };
static const uint8_t pad2_def[11] = { PAD_SRC_UP, PAD_SRC_DOWN, PAD_SRC_LEFT, PAD_SRC_RIGHT, PAD_D3_BOTTOM, PAD_D3_RIGHT, PAD_D3_LEFT, PAD_D3_TOP, PAD_D3_START, PAD_D3_L, PAD_D3_R };
static const uint8_t pad2_count[3] = { 5, 6, 11 };
static void pad2_loaded(CoreCtx *c, int type);
static const PadSpec pad2_spec = { { AM_PAD2_WORD, 0 }, 11, pad2_btn_names, pad2_def, 1, pad2_loaded };
static int g_pad2_on, g_pad2_type;

// The gateware's mode and the service's target from the two settings. A gateware without the registers (FEAT b5) is left alone.
static void pad2_apply(CoreCtx *c)
{
    pad_ensure(c);
    pad_set_target(g_pad2_type, pad2_count[g_pad2_type]);
    if (c->io->rd(AM_FEAT) & AM_FEAT_PAD2) {
        c->io->wr(AM_PAD2_MODE, (uint8_t)(g_pad2_on ? g_pad2_type + 1 : 0));
    }
}

static int op_pad2_on(CoreCtx *c, const OpArgs &a, OpOut &) { g_pad2_on = a.i(0); pad2_apply(c); return OPE_OK; }
static int op_pad2_type(CoreCtx *c, const OpArgs &a, OpOut &) { g_pad2_type = a.i(0); pad2_apply(c); return OPE_OK; }
static const ArgSpec pad2_on_arg[] = { ARG_INT("value", 0, 1) };
static const ArgSpec pad2_type_arg[] = { ARG_INT("value", 0, 2) };

// A pad with a stored map came: the target type stored with it is the setting's now.
static void pad2_loaded(CoreCtx *c, int type)
{
    if (type < 0 || type > 2) {
        return;
    }
    int i = c->settings ? c->settings->find("Port 2 acts as") : -1;
    if (i >= 0) {
        c->settings->set(i, type);
    }
    g_pad2_type = type;
    pad2_apply(c);
}

static const MemMap amiga_mem = { 0, 0, false, "The Amiga's chip and slow RAM are the core's own SDRAM; no window of the application reaches them" };

static void amiga_register(OpRegistry &r)
{
    r.contributor("amiga");
    MEM_OPS(r, amiga_mem, 0xFFFFFF);
    r.add_op("amiga.poll", 0, 0, op_am_poll);
    r.add_op("amiga.mount_reset", mount_reset_arg, 1, op_mount_reset);
    r.add_op("amiga.pad2_on", pad2_on_arg, 1, op_pad2_on);          // T225
    r.add_op("amiga.pad2_type", pad2_type_arg, 1, op_pad2_type);
    r.add_state("amiga.df0.image", win_state<0>);
    r.add_state("amiga.df1.image", win_state<1>);
    r.add_state("amiga.df2.image", win_state<2>);
    r.add_state("amiga.df3.image", win_state<3>);
    r.add_state("amiga.hd0.image", sec_state<0>);
    r.add_state("amiga.hd1.image", sec_state<1>);
    newdisk_register(r);
}

// T191: the clock's time. The gateware keeps it until the bitstream is loaded again (RTC_STAT b0), so a core that is reset does not get it again and
// SetClock's time stays; the board's clock, or without one the fixed date of the ZX Next application plus the uptime, goes in as 13 nibbles.
static uint8_t rtc_dow(unsigned y, unsigned m, unsigned d)                  // 0 = Sunday
{
    static const uint8_t mt[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    unsigned yy = m < 3 ? y - 1 : y;
    return (uint8_t)((yy + yy / 4 - yy / 100 + yy / 400 + mt[m - 1] + d) % 7);
}

static void rtc_nibble(CoreCtx *c, uint8_t idx, uint8_t v)
{
    c->io->wr(AM_RTC_VAL, v & 15);
    c->io->wr(AM_RTC_IDX, idx);
}

static void rtc_load(CoreCtx *c)
{
    if (!(c->io->rd(AM_RTC_EN) & 1) || (c->io->rd(AM_RTC_STAT) & 1)) {
        return;
    }
    uint8_t t[6];                                  // year 0-99, month, day, hour, minute, second
    if (!c->io->wall_time(t)) {
        uint32_t s = c->io->now_ms() / 1000;
        t[0] = 26; t[1] = 1; t[2] = 1; t[3] = (uint8_t)((s / 3600) % 24); t[4] = (uint8_t)((s / 60) % 60); t[5] = (uint8_t)(s % 60);
    }
    unsigned m = (t[1] >= 1 && t[1] <= 12) ? t[1] : 1, d = t[2] ? t[2] : 1;
    uint8_t n[13] = { (uint8_t)(t[5] % 10), (uint8_t)(t[5] / 10 % 6), (uint8_t)(t[4] % 10), (uint8_t)(t[4] / 10 % 6), (uint8_t)(t[3] % 10), (uint8_t)(t[3] / 10 % 3),
                      (uint8_t)(d % 10), (uint8_t)(d / 10 % 4), (uint8_t)(m % 10), (uint8_t)(m / 10), (uint8_t)(t[0] % 10), (uint8_t)(t[0] / 10 % 10),
                      rtc_dow(2000u + t[0], m, d) };
    rtc_nibble(c, 14, 1);                          // no tick while the nibbles go in
    for (uint8_t i = 0; i < 13; i++) {
        rtc_nibble(c, i, n[i]);
    }
    rtc_nibble(c, 15, 0);                          // the second starts now; LOADED
    rtc_nibble(c, 14, 0);
}

// Hold: the disks are made consistent in their files, the machine is held (CTRL b0 flushes its cache), the images are announced again.
static int am_init(CoreCtx *c, char *, int)
{
    engines_before_hold(c);
    c->io->wr(AM_CTRL, AM_HOLD);
    c->io->mode_reset(true);
    engines_after_hold(c);
    return 0;
}

static int am_release(CoreCtx *c, char *, int)
{
    sec_sync_all(c);                             // a folder disk is on its medium before the machine reads it again (T128 D5)
    rtc_load(c);
    c->io->wr(AM_CTRL, 0);
    c->io->mode_reset(false);
    return 0;
}

static const char *const caps[] = { "floppy", "hdf", "mouse", "pad" };

// ---- the two records, built from the common part and the override list ------------------------------------------------------------------
static Setting g_settings[2][15];
static RomSetLabels g_labels[2];
static RomSetSpec g_romset[2];

static CoreDesc make(int v)
{
    const AmigaVariant &a = variants[v];
    RomSetLabels &l = g_labels[v];
    snprintf(l.text[0], ROMSET_NAME, "%s", a.auto_label);
    for (int i = 1; i <= ROMSET_MAX; i++) {
        strcpy(l.text[i], "-");
    }
    for (int i = 0; i <= ROMSET_MAX; i++) {
        l.item[i] = l.text[i];
    }
    RomSetSpec &rs = g_romset[v];
    rs = RomSetSpec();
    rs.role = "Kickstart";
    rs.ext = ".rom";
    rs.sizes[0] = 262144;
    rs.sizes[1] = 524288;
    rs.size_text = "256K or 512K";
    rs.example = "kick13.rom";
    rs.max = ROMSET_MAX;
    rs.choice_key = KS_KEY;
    rs.labels = &l;
    rs.auto_label = a.auto_label;
    rs.want = a.want;
    rs.version = ks_header_version;
    rs.pick = ks_pick;
    rs.head_bytes = KS_HEAD;
    rs.window_off = AM_W_ROM;
    rs.mirror_size = 262144;                     // a 256 KB ROM twice: at +0x780000 and +0x7C0000
    rs.mirror_stride = 0x40000;
    rs.clear_below = AM_W_ROM;
    Setting *s = g_settings[v];
    s[0] = Setting{ KS_KEY, l.item, ROMSET_MAX + 1, 0, WHEN_RESET, 0, 0, 0, 0, 0, 0, "Kickstart file" };
    s[1] = Setting{ "Chip RAM (at next reset)", chip_names, 4, a.chip_def, WHEN_RESET, 0, &b_chip, 0, 0, 0, 0, "Chip RAM" };
    s[2] = Setting{ "Slow RAM (at next reset)", slow_names, 4, a.slow_def, WHEN_RESET, 0, &b_slow, 0, 0, 0, 0, "Slow RAM" };
    s[3] = Setting{ "Fast RAM (at next reset)", a.fast_names, a.nfast, 0, WHEN_RESET, 0, &b_fast, 0, 0, 0, 0, "Fast RAM" };
    s[4] = Setting{ "Chipset (at next reset)", a.chipset_names, a.nchipset, a.chipset_def, WHEN_RESET, 0, &b_chipset, 0, 0, 0, 0, "Chipset" };
    s[5] = Setting{ "Floppy drives (at next reset)", floppy_names, 4, 1, WHEN_RESET, 0, &b_nfl, 0, 0, 0, 0, "Floppy drives" };
    s[6] = Setting{ "Floppy write protect (next reset)", no_yes, 2, 0, WHEN_RESET, 0, &b_fwp, 0, 0, 0, 0, "Floppy write protect" };
    s[7] = Setting{ "Real-time clock (at next reset)", rtc_names, 2, 0, WHEN_RESET, 0, &b_rtc, 0, 0, 0, 0, "Real-time clock" };   // T191
    s[8] = Setting{ "Joystick ports", joy_names, 2, 0, WHEN_LIVE, 0, &b_joy };
    int ns = 9;
    if (a.led_load) {                            // T159: applied by the base operation led.load (engines.cc); the selector write is ledload.cc
        s[ns++] = Setting{ "LED Load Indicator", led_names, 2, 0, WHEN_LIVE, "led.load", 0 };
    }

    // T225: a USB gamepad as game port 2 (Input tab): On = port 2 comes from the pad only (D1), the type is what the Amiga sees, the task "Map port 2 buttons" opens the dialog
    s[ns++] = Setting{ "Map port 2 to USB controller", pad2_names, 2, 0, WHEN_LIVE, "amiga.pad2_on", 0, 0, "input", 0, 0 };
    s[ns++] = Setting{ "Port 2 acts as", pad2_type_names, 3, 0, WHEN_LIVE, "amiga.pad2_type", 0, 0, "input", 0, 0, 0 };

    CoreDesc d;
    memset(&d, 0, sizeof(d));
    memcpy(d.id, "AMIG", 4);                     // the gateware's one ID word, shared by both machines
    d.cor_name = a.cor_name;
    d.title = a.title;
    d.short_title = a.short_title;
    d.folder = "amiga";                          // roms/amiga: the Kickstarts are the same files for both
    d.store = a.store;
    d.min_contract = 1;
    d.settings = s; d.nsettings = ns;
    d.tasks = tasks; d.ntasks = 12;
    d.filetypes = filetypes; d.nfiletypes = 2;
    d.media = media; d.nmedia = 6;
    d.machine_info = "Chipset,Chip RAM,Slow RAM,Fast RAM";
    // 720 x 540 at scale 2 on 1920 x 1080; the three buffers are gateware parameters (FB_BUFS, FB_BYTE0): no FB_BASE / FB_STRIDE register
    d.video = { 720, 540, 2, 240, 0, 0x01000000u + AM_W_FB, 0x100000u, 3, AM_FB_LATEST, AM_FRAME_SEQ, 0, 0, 2 };
    if (a.rgb32) {                               // T136 D3: outside the window (the application cannot clear them: Video::init skips them)
        d.video.fb_base = AM_FB32_ADDR; d.video.fb_stride = AM_FB32_STRIDE; d.video.bpp = 4;
    }
    d.freeze = { AM_STATUS, AM_ST_PAUSED, 200, false };
    d.init = am_init;
    d.release = am_release;
    d.reset_ms = 20;
    d.service_op = "amiga.poll";                 // media.poll plus the audio KPI line
    d.service_period_ms = 5;                     // an open menu is Freeze (Alex, 2026-10-20): the machine is held and no poll runs
    d.register_ops = amiga_register;
    d.caps = caps; d.ncaps = 4;
    d.id_shared = true;
    d.id_feat_reg = AM_FEAT; d.id_feat_mask = AM_FEAT_FBWIN; d.id_feat_val = a.fbwin ? AM_FEAT_FBWIN : 0;   // T115 D9: a .cfw has no bundle name
    d.core_id = a.core_id;
    d.report_regs = report_regs; d.nreport_regs = (int)(sizeof(report_regs) / sizeof(report_regs[0]));
    d.reset_writes = reset_writes; d.nreset_writes = 2;
    d.romset = &rs;
    d.window_media = &floppies;
    d.sector_media = &hard_disks;
    d.mouse = &mouse;
    d.pad = &pad2_spec;                        // T225
    d.rawkeys = a.usbkeys ? &rawkeys : 0;
    d.hotkeys = &hk_amiga;                     // T181: the default hotkey map (hk_defaults.h)
    d.fbwin = a.fbwin ? &picture_window : 0;
    return d;
}

extern const CoreDesc amiga_desc;
extern const CoreDesc amiga1200_desc;
const CoreDesc amiga_desc = make(0);
const CoreDesc amiga1200_desc = make(1);
