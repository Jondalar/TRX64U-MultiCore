// The Apple //e descriptor (T89 §5 2026-10-19, T93 D3): ROMs from roms/apple2, the monitor settings, the resets, Disk II drives from the file
// browser (apple2_disk.cc) and the CAPS LOCK that the C64U's SHIFT LOCK drives. The register block is cores/apple2/rtl/trx/a2_regs.vhd.
#include "hk_defaults.h"
#include "ctx.h"
#include "memops.h"
#include "boot.h"
#include "apple2_regs.h"
#include "apple2_disk.h"
#include "cores.h"
#include "newdisk.h"
#include <stdio.h>
#include <string.h>

// ---- settings ------------------------------------------------------------------------------------------------------------
enum { S_DISPLAY = 0, S_PALETTE, S_SCAN, S_CHARSET, S_JOY, S_CAPS, S_LED, S_COUNT };

static const char *const monitor_names[] = { "Colour", "Black and white", "Green", "Amber" };
static const char *const palette_names[] = { "Original", "IIgs", "AppleWin" };
static const char *const scan_names[]    = { "Off", "On" };
static const char *const charset_names[] = { "First 4 KB", "Second 4 KB" };
static const char *const joy_names[]     = { "Normal", "Swapped" };
static const char *const led_names[]   = { "On", "Off" };   // T159 D-all: the A1200's "LED Load Indicator" (base operation led.load), case LED 1 = the gateware's drive activity
static const char *const caps_names[]    = { "On", "Off", "Follow SHIFT LOCK" };
enum { CAPS_ON = 0, CAPS_OFF, CAPS_FOLLOW };

// One register holds the monitor items: every item rereads all of them (as the Atari's flags).
static void a2_cfg(CoreCtx *c)
{
    Settings *s = c->settings;
    uint8_t v = (uint8_t)(s->value(S_DISPLAY) & 3);
    v |= (uint8_t)((s->value(S_PALETTE) & 3) << 2);
    v |= (uint8_t)((s->value(S_SCAN) & 1) << 4);
    v |= (uint8_t)((s->value(S_CHARSET) & 1) << 5);
    v |= (uint8_t)((s->value(S_JOY) & 1) << 6);
    c->io->wr(A2_CFG, v);
}

// CAPS LOCK of the machine: on, off, or the C64U's SHIFT LOCK (T89 §5). The keyboard board holds SHIFT in the matrix for a locked key unless
// lock_off, so lock_off is set in every mode: the Apple has no SHIFT LOCK, only a CAPS LOCK that capitalises letters.
static void a2_caps(CoreCtx *c)
{
    A2State *st = a2_state(c);
    int mode = c->settings ? c->settings->value(S_CAPS) : CAPS_FOLLOW;
    bool locked = (c->io->bling_flags() & 0x02) != 0;
    bool want = mode == CAPS_ON ? true : mode == CAPS_OFF ? false : locked;
    if ((c->io->bling_flags() & 0x01) == 0) {
        c->io->bling_lock_off(true);
    }
    if (!st->caps_known || st->caps_last != (want ? 1 : 0)) {
        c->io->wr(A2_CAPS, want ? 1 : 0);
        st->caps_last = want ? 1 : 0;
        st->caps_known = true;
    }
}

static int op_cfg(CoreCtx *c, const OpArgs &, OpOut &) { a2_cfg(c); return OPE_OK; }
static int op_caps(CoreCtx *c, const OpArgs &, OpOut &) { a2_caps(c); return OPE_OK; }
static const ArgSpec cfg_arg[] = { ARG_INT("value", 0, 3) };

static const Setting settings[S_COUNT] = {
    { "Display",          monitor_names, 4, 0, WHEN_LIVE, "apple2.set_cfg" },
    { "Palette",          palette_names, 3, 0, WHEN_LIVE, "apple2.set_cfg" },
    { "Scanlines",        scan_names,    2, 0, WHEN_LIVE, "apple2.set_cfg" },
    { "Character ROM set", charset_names, 2, 0, WHEN_LIVE, "apple2.set_cfg" },
    { "Joystick ports",   joy_names,     2, 0, WHEN_LIVE, "apple2.set_cfg" },
    { "CAPS LOCK",        caps_names,    3, CAPS_FOLLOW, WHEN_LIVE, "apple2.set_caps" },
    { "LED Load Indicator", led_names, 2, 0, WHEN_LIVE, "led.load" },
};

static bool a2_any_mounted(CoreCtx *c)
{
    A2State *st = a2_state(c);
    return st->drive[0].file || st->drive[1].file;
}

// ---- ROMs ----------------------------------------------------------------------------------------------------------------
// The aperture shows one array at a time (WIN_SEL); a 16 KB array is contiguous in it (a2_win.vhd), the file goes in as it is.
static int put_array(CoreCtx *c, int sel, const uint8_t *data, uint32_t size, const char *role, char *msg, int msglen)
{
    static uint8_t back[A2_SYSROM_BYTES];
    c->io->wr(A2_WIN_SEL, (uint8_t)sel);
    c->io->rom_write(0, data, size);
    c->io->rom_read(0, back, size);
    c->io->wr(A2_WIN_SEL, A2_SEL_SYSROM);
    if (memcmp(back, data, size) != 0) {
        snprintf(msg, msglen, "%s: the core did not take the ROM (read-back differs)", role);
        return 2;
    }
    return 0;
}

// The monitor's RESET vector $FA62 at $3FFC (T89 S4): a file of another layout is refused, not guessed at.
static bool check_sysrom(const uint8_t *d, uint32_t size)
{
    return size == A2_SYSROM_BYTES && d[0x3FFC] == 0x62 && d[0x3FFD] == 0xFA;
}

static int op_put_sys(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return put_array(c, A2_SEL_SYSROM, a.p(0), a.len(0), "System ROM", o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static int op_put_chr(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return put_array(c, A2_SEL_CHRROM, a.p(0), a.len(0), "Video ROM", o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

// The Disk II boot ROM sits in slot 6 and the monitor boots from it at every cold start: with no disk in the drive it would spin for
// ever and never reach BASIC. So the core's array holds the ROM only while a disk is mounted (zeros: no card, the monitor goes to
// BASIC, as the boot bench does); the file is kept here for the mount.
static int put_diskrom_core(CoreCtx *c, bool present, char *msg, int msglen)
{
    A2State *st = a2_state(c);
    uint8_t zeros[A2_DISKROM_BYTES];
    memset(zeros, 0, sizeof(zeros));
    return put_array(c, A2_SEL_DISKROM, present ? st->diskrom : zeros, A2_DISKROM_BYTES, "Disk II ROM", msg, msglen);
}

static int op_put_diskrom(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    A2State *st = a2_state(c);
    memcpy(st->diskrom, a.p(0), A2_DISKROM_BYTES);
    st->have_diskrom = true;
    return put_diskrom_core(c, a2_any_mounted(c), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static const ArgSpec rom_arg[] = { ARG_BLOB("data") };

static const RomSpec roms[] = {
    { "System ROM", { "apple2e.rom", 0 },       { A2_SYSROM_BYTES, 0 },  true,  check_sysrom, "apple2.put_sys" },
    { "Video ROM",  { "apple2e-video.rom", 0 }, { A2_CHRROM_BYTES, 0 },  true,  0,            "apple2.put_chr" },
    { "Disk II ROM", { "diskii.rom", 0 },       { A2_DISKROM_BYTES, 0 }, false, 0,            "apple2.put_diskrom" },
};

// ---- control -------------------------------------------------------------------------------------------------------------
static int a2_init(CoreCtx *c, char *, int)
{
    a2_flush_all(c);                             // a dirty track goes out first: the core drops it when the reset begins (a2_trk.vhd)
    c->io->wr(A2_CTRL, A2_CTRL_HOLD);
    c->io->mode_reset(true);                     // the host's reset is a cold one in the core: the power-up byte, then the long reset
    return 0;
}

static int a2_release(CoreCtx *c, char *, int)
{
    a2_write_ctl(c);
    a2_state(c)->caps_known = false;             // the register is written again
    a2_caps(c);
    c->io->wr(A2_CTRL, 0);
    c->io->mode_reset(false);
    a2_state(c)->diag_at = c->io->now_ms();
    a2_state(c)->diag_stage = 0;
    return 0;
}

// CTRL-RESET: the machine keeps its RAM and the monitor takes the warm path (a valid power-up byte).
static int a2_warm_reset(CoreCtx *c, char *, int)
{
    a2_flush_all(c);
    c->io->wr(A2_CTRL, A2_CTRL_HOLD);
    c->io->delay_ms(A2_WARM_MS);
    c->io->wr(A2_CTRL, 0);
    c->started = true;
    return 0;
}

// ---- disks -----------------------------------------------------------------------------------------------------------------
static int op_eject(CoreCtx *c, const OpArgs &a, OpOut &o) { return a2_eject(c, a.i(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }

// A disk needs the card ROM: the file is checked first, so a missing ROM changes nothing. The argument `boot` ends it in a cold start
// (T108 D2 for the Atari); without it the disk is only swapped in, for a game that asks for its next one (T108 D6).
static int mount_floppy(CoreCtx *c, const char *path, int drive, char *msg, int msglen)
{
    A2State *st = a2_state(c);
    if (!st->have_diskrom) {
        snprintf(msg, msglen, "diskii.rom is not in the core's roms folder - the Disk II needs its boot ROM (reload the ROMs once it is there)");
        return 1;
    }
    if (a2_mount(c, path, drive, msg, msglen)) {
        return 1;
    }
    if (put_diskrom_core(c, true, msg, msglen)) {
        return 1;
    }
    a2_write_ctl(c);
    return 0;
}
static int op_mount(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    if (mount_floppy(c, a.s(0), a.i(1), o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    if (a.i(2) != 0) {
        return boot_cold_reset(c, o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
    }
    return OPE_OK;
}
static int op_hdd_eject(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return a2_hdd_eject(c, a.i(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

// A hard disk volume in slot 7 (T89.1 D4, D7): the card's ROM goes into the core with the unit, the Autostart ROM boots slot 7 before slot 6
// at the next cold start. `boot` ends it in one.
static int op_hdd_mount(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    if (a2_hdd_mount(c, a.s(0), a.i(1), o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    if (a.i(2) != 0) {
        return boot_cold_reset(c, o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
    }
    return OPE_OK;
}

// T180 D2, D5: the four slots of the Apple II, the words of the menu without the colon. The text is the image's path.
static const char *const slot_names[] = { "D1", "D2", "S7.1", "S7.2" };
static const char *slot_spec(CoreCtx *c, int i)
{
    A2State *st = a2_state(c);
    const char *p = i < A2_DRIVES ? (st->drive[i].file ? st->drive[i].path : 0) : (st->unit[i - A2_DRIVES].file ? st->unit[i - A2_DRIVES].path : 0);
    return p && p[0] ? p : 0;
}
static int slot_mount(CoreCtx *c, int i, const char *spec, char *msg, int msglen)
{
    return i < A2_DRIVES ? mount_floppy(c, spec, i, msg, msglen) : a2_hdd_mount(c, spec, i - A2_DRIVES, msg, msglen);
}
static int slot_eject(CoreCtx *c, int i, char *msg, int msglen)
{
    return i < A2_DRIVES ? a2_eject(c, i, msg, msglen) : a2_hdd_eject(c, i - A2_DRIVES, msg, msglen);
}
static const MountSlots mount_slots = { 4, slot_names, slot_spec, slot_mount, slot_eject };
static const ArgSpec drive_arg[] = { ARG_INT("drive", 0, 1) };
static const ArgSpec mount_args[] = { ARG_PATH("path"), ARG_INT("drive", 0, 1), ARG_INT("boot", 0, 1) };
static const ArgSpec unit_arg[] = { ARG_INT("unit", 0, 1) };
static const ArgSpec hdd_mount_args[] = { ARG_PATH("path"), ARG_INT("unit", 0, 1), ARG_INT("boot", 0, 1) };

// What the core says about itself 1 s and 3 s after a start, on the console: the first board run (2026-10-19) had a picture nobody could
// explain, and the application is the only one who can read these (T89 §5).
static void a2_diag(CoreCtx *c)
{
    A2State *st = a2_state(c);
    if (st->diag_stage >= 2) {
        return;
    }
    uint32_t due = st->diag_at + (st->diag_stage == 0 ? 1000u : 3000u);
    if ((int32_t)(c->io->now_ms() - due) < 0) {
        return;
    }
    CoreIo *io = c->io;
    uint32_t miss = io->rd(0x0B0) | io->rd(0x0B1) << 8 | io->rd(0x0B2) << 16 | (uint32_t)io->rd(0x0B3) << 24;
    uint32_t stall = io->rd(0x0B4) | io->rd(0x0B5) << 8 | io->rd(0x0B6) << 16 | (uint32_t)io->rd(0x0B7) << 24;
    printf("[apple2] +%ums: status %02x frames %u cpu %04x fb %02x seq %u overruns %u aux miss %u stall %u\n",
           (unsigned)(io->now_ms() - st->diag_at), io->rd(A2_STATUS), io->rd(A2_FRAMES) | io->rd(A2_FRAMES + 1) << 8,
           io->rd(0x0AC) | io->rd(0x0AD) << 8, io->rd(A2_FB_LATEST), io->rd(A2_FRAME_SEQ), io->rd(0x0C2) | io->rd(0x0C3) << 8,
           (unsigned)miss, (unsigned)stall);
    st->diag_stage++;
}

static int op_service(CoreCtx *c, const OpArgs &, OpOut &o)
{
    a2_caps(c);
    a2_diag(c);
    o.more = a2_service(c);
    return OPE_OK;
}

static const OpBind drive0[] = { B_INT("drive", 0) }, drive1[] = { B_INT("drive", 1) };
static const OpBind unit0[] = { B_INT("unit", 0) }, unit1[] = { B_INT("unit", 1) };

static const OpBind new_dsk[] = { B_TEXT("path"), B_STR("format", "apple2_dsk"), B_STR("name", "") };

static const Task tasks[] = {
    { "Reset (Ctrl-Reset)",         "machine.warm_reset",   0, 0,      true },
    { "Cold start",                 "machine.cold_reset",   0, 0,      true },
    { "Reload ROMs, cold start",    "machine.reload_roms",  0, 0,      true },
    { "Eject D1:",                  "apple2.eject",         drive0, 1, false },
    { "Eject D2:",                  "apple2.eject",         drive1, 1, false },
    { "Eject S7 unit 1",            "apple2.hdd_eject",     unit0,  1, false },
    { "Eject S7 unit 2",            "apple2.hdd_eject",     unit1,  1, false },
    { "Create blank DSK",           "disk.create",          new_dsk, 3, false, "Name of the new DSK", "disk.suggest.apple2_dsk", true },
};

static const OpBind mount0b[] = { B_FILE("path"), B_INT("drive", 0), B_INT("boot", 1) }, mount0[] = { B_FILE("path"), B_INT("drive", 0), B_INT("boot", 0) },
                    mount1[] = { B_FILE("path"), B_INT("drive", 1), B_INT("boot", 0) };
static const FileAction disk_actions[] = {
    { "Mount D1: and cold start", "apple2.mount", mount0b, 3, true },
    { "Mount D1:",                "apple2.mount", mount0,  3, false },
    { "Mount D2:",                "apple2.mount", mount1,  3, false },
};
// ProDOS hard disk volumes (T89.1): .hdv, a big .po, .2mg
static const OpBind hmount0b[] = { B_FILE("path"), B_INT("unit", 0), B_INT("boot", 1) }, hmount0[] = { B_FILE("path"), B_INT("unit", 0), B_INT("boot", 0) },
                    hmount1[] = { B_FILE("path"), B_INT("unit", 1), B_INT("boot", 0) };
static const FileAction hdd_actions[] = {
    { "Mount S7 unit 1 and cold start", "apple2.hdd_mount", hmount0b, 3, true },
    { "Mount S7 unit 1",                "apple2.hdd_mount", hmount0,  3, false },
    { "Mount S7 unit 2",                "apple2.hdd_mount", hmount1,  3, false },
};
// a .po is a floppy at 143,360 bytes and a volume at any other size; a .2mg of a floppy is 64 bytes longer than the image it holds
static const uint32_t po_floppy[] = { 143360 };
static const uint32_t mg_floppy[] = { 143360 + 64, 232960 + 64 };
static const FileAction po_actions[] = {
    { "Mount D1: and cold start", "apple2.mount", mount0b, 3, true, 0, 0, po_floppy, 1 },
    { "Mount D1:",                "apple2.mount", mount0,  3, false, 0, 0, po_floppy, 1 },
    { "Mount D2:",                "apple2.mount", mount1,  3, false, 0, 0, po_floppy, 1 },
    { "Mount S7 unit 1 and cold start", "apple2.hdd_mount", hmount0b, 3, true },
    { "Mount S7 unit 1",                "apple2.hdd_mount", hmount0,  3, false },
    { "Mount S7 unit 2",                "apple2.hdd_mount", hmount1,  3, false },
};
static const FileAction mg_actions[] = {
    { "Mount D1: and cold start", "apple2.mount", mount0b, 3, true, 0, 0, mg_floppy, 2 },
    { "Mount D1:",                "apple2.mount", mount0,  3, false, 0, 0, mg_floppy, 2 },
    { "Mount D2:",                "apple2.mount", mount1,  3, false, 0, 0, mg_floppy, 2 },
    { "Mount S7 unit 1 and cold start", "apple2.hdd_mount", hmount0b, 3, true },
    { "Mount S7 unit 1",                "apple2.hdd_mount", hmount0,  3, false },
    { "Mount S7 unit 2",                "apple2.hdd_mount", hmount1,  3, false },
};
static const FileType filetypes[] = {
    { ".dsk", disk_actions, 3 }, { ".do", disk_actions, 3 }, { ".nib", disk_actions, 3 }, { ".woz", disk_actions, 3 },
    { ".po", po_actions, 6 }, { ".2mg", mg_actions, 6 }, { ".hdv", hdd_actions, 3 },
};

static const char *d1(CoreCtx *c) { return a2_drive_name(c, 0); }
static const char *d2(CoreCtx *c) { return a2_drive_name(c, 1); }
static const char *s71(CoreCtx *c) { return a2_unit_name(c, 0); }
static const char *s72(CoreCtx *c) { return a2_unit_name(c, 1); }
static const MediaSlot media[] = {
    { "D1:", "apple2.d1.image", "apple2.eject", drive0, 1 },
    { "D2:", "apple2.d2.image", "apple2.eject", drive1, 1 },
    { "S7.1:", "apple2.s71.image", "apple2.hdd_eject", unit0, 1 },
    { "S7.2:", "apple2.s72.image", "apple2.hdd_eject", unit1, 1 },
};

// The built-in contributor of this core (T104.1 D14).
static const MemRegion apple2_mem_regions[] = { { 0, 0x10000, MK_RAM, 0, 0, "main RAM" }, { 0x10000, 0x10000, MK_DDR, A2_W_AUX, 0, "aux RAM" } };
static const MemMap apple2_mem = { apple2_mem_regions, 2, false, 0 };

static void apple2_register(OpRegistry &r)
{
    r.contributor("apple2");
    MEM_OPS(r, apple2_mem, 0x1FFFF);
    r.add_op("apple2.set_cfg", cfg_arg, 1, op_cfg);
    r.add_op("apple2.set_caps", cfg_arg, 1, op_caps);
    r.add_op("apple2.put_sys", rom_arg, 1, op_put_sys);
    r.add_op("apple2.put_chr", rom_arg, 1, op_put_chr);
    r.add_op("apple2.put_diskrom", rom_arg, 1, op_put_diskrom);
    r.add_op("apple2.eject", drive_arg, 1, op_eject);
    r.add_op("apple2.mount", mount_args, 3, op_mount);
    r.add_op("apple2.hdd_mount", hdd_mount_args, 3, op_hdd_mount);
    r.add_op("apple2.hdd_eject", unit_arg, 1, op_hdd_eject);
    r.add_op("apple2.service", 0, 0, op_service);
    r.add_state("apple2.d1.image", d1);
    r.add_state("apple2.d2.image", d2);
    r.add_state("apple2.s71.image", s71);
    r.add_state("apple2.s72.image", s72);
    newdisk_register(r);
}

// A floppy drive on slot 6 and a hard disk on slot 7 (no SIO, no IEC, no cartridge): a library of another machine's media is refused here.
static const char *const caps[] = { "floppy" };

extern const CoreDesc apple2_desc;
// T162 D3: the state block of the bug report (reads without side effects only)
static const ReportReg report_regs[] = {
    { "ID", A2_ID0, 4 }, { "CAPS2", A2_CAPS2, 1 }, { "CTRL", A2_CTRL, 1 }, { "CFG", A2_CFG, 1 }, { "CAPS", A2_CAPS, 1 },
    { "STATUS (b0 reset, b1 held, b2 cold)", A2_STATUS, 1 }, { "FRAMES", A2_FRAMES, 2 }, { "FB_LATEST", A2_FB_LATEST, 1 },
    { "FRAME_SEQ", A2_FRAME_SEQ, 1 }, { "WIN_SEL", A2_WIN_SEL, 1 },
};

const CoreDesc apple2_desc = {
    { 'A', 'P', 'L', '2' },
    "Apple IIe",
    "Apple IIe",
    "APPLE II",
    "apple2",
    "Apple IIe Settings",
    1,
    roms, 3,
    settings, S_COUNT,
    tasks, 8,
    filetypes, 7,
    media, 4,
    // 560 x 384 at scale 2 is 1120 x 768, centred on the 1920 x 1080 output (the board kit's window: 400, 156)
    { 560, 384, 2, 400, 156, 0x01000000u + A2_W_FB, 0x100000u, 4, A2_FB_LATEST, A2_FRAME_SEQ, A2_FB_BASE, A2_FB_STRIDE },
    { A2_STATUS, A2_ST_HELD, 200 },
    a2_init, a2_release, a2_warm_reset, 10,
    "apple2.service", 1, true,
    0, 0,
    apple2_register,
    caps, 1,
    false,                                     // board_caps
    false, 0, 0, 0, 0, 0, 0, 0,                // id_shared, reset_writes, n, romset, window_media, sector_media, mouse, fbwin
    0, 0,                                      // type_op, run_op
    0, 0, 0, 0, 0,                             // cold_ram_bytes, cold_start, id_feat_reg, id_feat_mask, id_feat_val
    0, 0,                                      // menu_rec, menu_rec_n
    0,                                         // core_id
    report_regs, (int)(sizeof(report_regs) / sizeof(report_regs[0])),
    0, false, 0, 0, false,                     // rawkeys, own_volume, pad, live_ms, menu_first
    a2_media_flush, a2_media_pending,          // T180 D4: the floppy and slot 7 write-back
    &mount_slots,                              // T180 D2, D5: D1, D2, S7.1, S7.2
    &hk_typing,                            // T181: the default hotkey map (hk_defaults.h)
};
