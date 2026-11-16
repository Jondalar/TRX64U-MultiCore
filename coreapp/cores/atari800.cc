// The Atari 800 XL descriptor (T93 D3, T88): ROMs, settings, tasks, picture, Freeze and the hooks that drive the register
// block of cores/atari800/trx/a8_regs.vhd. Media (SIO, ATR, XEX, cartridges) is atari_media.cc.
#include "hk_defaults.h"
#include "ctx.h"
#include "memops.h"
#include "boot.h"
#include "atari800_regs.h"
#include "cores.h"
#include "newdisk.h"
#include "atari_media.h"
#include <stdio.h>
#include <string.h>

// ---- settings ------------------------------------------------------------------------------------------------------------
enum { S_RAM = 0, S_VIDEO, S_JOY, S_SPEED, S_LED, S_COUNT };

static const char *const ram_names[]   = { "64 KB", "128 KB", "320 KB Compy Shop", "320 KB Rambo", "576 KB Compy Shop",
                                           "576 KB Rambo", "1088 KB Rambo" };
static const char *const video_names[] = { "PAL", "NTSC" };
static const char *const joy_names[]   = { "Normal", "Swapped" };
static const char *const led_names[]   = { "On", "Off" };   // T159 D-all: the A1200's "LED Load Indicator" (base operation led.load), case LED 1 = the gateware's drive activity
static const char *const speed_names[] = { "1x", "2x", "4x", "8x" };

static void a8_flags(CoreCtx *c)
{
    uint8_t f = 0;
    if (c->settings->value(S_VIDEO) == 0) {
        f |= A8_FLAG_PAL;
    }
    if (c->settings->value(S_JOY) == 1) {
        f |= A8_FLAG_SWAP;
    }
    c->io->wr(A8_FLAGS, f);
}

// The operations behind the settings (T104.1 D5): each takes the item's value. The two flag items share one, which rereads both.
static int op_ram(CoreCtx *c, const OpArgs &a, OpOut &) { c->io->wr(A8_RAM_SEL, (uint8_t)a.i(0)); return OPE_OK; }
static int op_flags(CoreCtx *c, const OpArgs &, OpOut &) { a8_flags(c); return OPE_OK; }
static int op_speed(CoreCtx *c, const OpArgs &a, OpOut &) { c->io->wr(A8_THROTTLE, (uint8_t)(1u << a.i(0))); return OPE_OK; }
static const ArgSpec ram_arg[] = { ARG_INT("value", 0, 6) };
static const ArgSpec flags_arg[] = { ARG_INT("value", 0, 1) };
static const ArgSpec speed_arg[] = { ARG_INT("value", 0, 3) };

static const Setting settings[S_COUNT] = {
    { "RAM size",         ram_names,   7, 0, WHEN_RESET, "atari800.set_ram" },
    { "Video standard",   video_names, 2, 0, WHEN_RESET, "atari800.set_flags" },
    { "Joystick ports",   joy_names,   2, 0, WHEN_LIVE,  "atari800.set_flags" },
    { "CPU speed",        speed_names, 4, 0, WHEN_LIVE,  "atari800.set_speed" },
    { "LED Load Indicator", led_names, 2, 0, WHEN_LIVE, "led.load" },
};

// ---- ROMs ----------------------------------------------------------------------------------------------------------------
// The aperture shows one 16 KB array at a time (ROM_SEL); the host maps it contiguously, the OS file goes in as it is.
static int put_array(CoreCtx *c, int sel, const uint8_t *data, uint32_t size, const char *role, char *msg, int msglen)
{
    static uint8_t back[A8_OS_BYTES];
    c->io->wr(A8_ROM_SEL, (uint8_t)sel);
    c->io->rom_write(0, data, size);
    c->io->rom_read(0, back, size);
    if (memcmp(back, data, size) != 0) {
        snprintf(msg, msglen, "%s: the core did not take the ROM (read-back differs)", role);
        return 2;
    }
    return 0;
}

static int op_put_os(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return put_array(c, 0, a.p(0), a.len(0), "XL OS", o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static int op_put_basic(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return put_array(c, 1, a.p(0), a.len(0), "BASIC", o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static const ArgSpec rom_arg[] = { ARG_BLOB("data") };

static const RomSpec roms[] = {
    { "XL OS",  { "ATARIXL.ROM", 0 },                { A8_OS_BYTES, 0 },    true, 0, "atari800.put_os" },
    { "BASIC",  { "BASIC.ROM", "ATARIBAS.ROM", 0 },  { A8_BASIC_BYTES, 0 }, true, 0, "atari800.put_basic" },
};

// ---- control -------------------------------------------------------------------------------------------------------------
static int a8_init(CoreCtx *c, char *, int)
{
    c->io->wr(A8_CTRL, A8_CTRL_HOLD);
    c->io->mode_reset(true);
    a8_write_cart_reg(c);                        // the cartridge type is taken at the reset
    a8_state(c)->sio.flush_all();                // a drive's file is consistent before the machine starts over
    return 0;
}

static int a8_release(CoreCtx *c, char *, int)
{
    c->io->wr(A8_CTRL, 0);
    c->io->mode_reset(false);
    return 0;
}

// A reset that keeps the RAM: CTRL b2 is looked at when the reset begins (a8_core.vhd, "the sweep starts when the reset does").
static int a8_warm_reset(CoreCtx *c, char *msg, int msglen)
{
    c->io->wr(A8_CTRL, A8_CTRL_HOLD | A8_CTRL_KEEP);
    c->io->mode_reset(true);
    a8_write_cart_reg(c);
    a8_state(c)->sio.flush_all();
    boot_apply_settings(c, false);
    c->io->delay_ms(5);
    c->io->wr(A8_CTRL, 0);
    c->io->mode_reset(false);
    c->started = true;
    return 0;
}

// Tasks, file actions and slots call these by name; the drive and the file are bound by the entry that calls (desc.h).
static int op_eject(CoreCtx *c, const OpArgs &a, OpOut &o) { return a8_eject(c, a.i(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_mount(CoreCtx *c, const OpArgs &a, OpOut &o) { return a8_mount(c, a.s(0), a.i(1), a.i(2) != 0, o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_run_xex(CoreCtx *c, const OpArgs &a, OpOut &o) { return a8_run_xex(c, a.s(0), 0, o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_insert_cart(CoreCtx *c, const OpArgs &a, OpOut &o) { return a8_insert_cart(c, a.s(0), 0, o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_service(CoreCtx *c, const OpArgs &, OpOut &o) { o.more = a8_service(c); return OPE_OK; }
static const ArgSpec drive_arg[] = { ARG_INT("drive", 0, 3) };
static const ArgSpec mount_args[] = { ARG_PATH("path"), ARG_INT("drive", 0, 3), ARG_INT("boot", 0, 1) };
static const ArgSpec path_arg[] = { ARG_PATH("path") };

static const OpBind drive0[] = { B_INT("drive", 0) }, drive1[] = { B_INT("drive", 1) }, drive2[] = { B_INT("drive", 2) },
                    drive3[] = { B_INT("drive", 3) };

static const OpBind new_atr[] = { B_TEXT("path"), B_STR("format", "atr"), B_STR("name", "") };

static const Task tasks[] = {
    { "Reset",                      "machine.warm_reset",   0, 0,        true },
    { "Cold start",                 "machine.cold_reset",   0, 0,        true },
    { "Reload ROMs, cold start",    "machine.reload_roms",  0, 0,        true },
    { "Eject D1:",                  "atari800.eject",       drive0, 1,   false },
    { "Eject D2:",                  "atari800.eject",       drive1, 1,   false },
    { "Eject D3:",                  "atari800.eject",       drive2, 1,   false },
    { "Eject D4:",                  "atari800.eject",       drive3, 1,   false },
    { "Create blank ATR",           "disk.create",          new_atr, 3,  false, "Name of the new ATR", "disk.suggest.atr", true },
};

static const OpBind mount0b[] = { B_FILE("path"), B_INT("drive", 0), B_INT("boot", 1) }, mount0[] = { B_FILE("path"), B_INT("drive", 0), B_INT("boot", 0) },
                    mount1[] = { B_FILE("path"), B_INT("drive", 1), B_INT("boot", 0) },
                    mount2[] = { B_FILE("path"), B_INT("drive", 2), B_INT("boot", 0) }, mount3[] = { B_FILE("path"), B_INT("drive", 3), B_INT("boot", 0) };
static const OpBind file_only[] = { B_FILE("path") };

static const FileAction atr_actions[] = {
    // T108 D2: D1: is the drive the machine boots from, so the first (default) action starts it and closes the menu, as "Run"
    // does for a .xex. "Mount D1:" is the swap for a running game (T108 D6); the others are mounts and nothing more.
    { "Mount D1: and cold start", "atari800.mount", mount0b, 3, true }, { "Mount D1:", "atari800.mount", mount0, 3, false },
    { "Mount D2:", "atari800.mount", mount1, 3, false }, { "Mount D3:", "atari800.mount", mount2, 3, false },
    { "Mount D4:", "atari800.mount", mount3, 3, false },
};
static const FileAction xex_actions[] = { { "Run (boot disk in D1:, cold start)", "atari800.run_xex", file_only, 1, true } };
static const FileAction car_actions[] = { { "Insert cartridge, cold start", "atari800.insert_cart", file_only, 1, true } };

static const FileType filetypes[] = {
    { ".atr", atr_actions, 5 }, { ".xfd", atr_actions, 5 },
    { ".xex", xex_actions, 1 }, { ".obx", xex_actions, 1 }, { ".com", xex_actions, 1 },
    { ".car", car_actions, 1 }, { ".rom", car_actions, 1 }, { ".bin", car_actions, 1 },
};

static const char *slot_name(CoreCtx *c, int i)
{
    AtariState *st = a8_state(c);
    return st->disk[i] ? st->disk[i]->name() : 0;
}
static const char *d1(CoreCtx *c) { return slot_name(c, 0); }
static const char *d2(CoreCtx *c) { return slot_name(c, 1); }
static const char *d3(CoreCtx *c) { return slot_name(c, 2); }
static const char *d4(CoreCtx *c) { return slot_name(c, 3); }
static const char *cart(CoreCtx *c) { AtariState *st = a8_state(c); return st->cart_mode ? st->cart_name : 0; }

static const MediaSlot media[] = {
    { "D1:", "atari800.d1.image", "atari800.eject", drive0, 1, 0, ".atr,.xfd", "Mount D1:", "Create blank ATR", 0 },
    { "D2:", "atari800.d2.image", "atari800.eject", drive1, 1, 0, ".atr,.xfd", "Mount D2:", "Create blank ATR", 0 },
    { "D3:", "atari800.d3.image", "atari800.eject", drive2, 1, 0, ".atr,.xfd", "Mount D3:", "Create blank ATR", 0 },
    { "D4:", "atari800.d4.image", "atari800.eject", drive3, 1, 0, ".atr,.xfd", "Mount D4:", "Create blank ATR", 0 },
    { "Cartridge", "atari800.cartridge.image", "atari800.remove_cart", 0, 0, 0, ".car,.rom,.bin", "Insert cartridge, cold start", 0, 0, true },
};

// T180 D2, D5: the five slots for the stored mounts and the media sets (atari_media.cc): D1..D4, then the cartridge
static const char *const slot_names[] = { "D1", "D2", "D3", "D4", "Cartridge" };
static const MountSlots mount_slots = { 5, slot_names, a8_slot_spec, a8_slot_mount, a8_slot_eject };

// The built-in contributor of this core (T104.1 D14): the hooks above as operations, the slots as state values.
static const MemRegion atari800_mem_regions[] = { { 0, 0x10000, MK_RAM, 0, 0, "RAM (the 64 KB the 6502 sees)" } };
static const MemMap atari800_mem = { atari800_mem_regions, 1, false, 0 };

static void atari800_register(OpRegistry &r)
{
    r.contributor("atari800");
    MEM_OPS(r, atari800_mem, 0xFFFF);
    r.add_op("atari800.set_ram", ram_arg, 1, op_ram);
    r.add_op("atari800.set_flags", flags_arg, 1, op_flags);
    r.add_op("atari800.set_speed", speed_arg, 1, op_speed);
    r.add_op("atari800.put_os", rom_arg, 1, op_put_os);
    r.add_op("atari800.put_basic", rom_arg, 1, op_put_basic);
    r.add_op("atari800.remove_cart", 0, 0, op_hook<a8_remove_cart>);
    r.add_op("atari800.eject", drive_arg, 1, op_eject);
    r.add_op("atari800.mount", mount_args, 3, op_mount);
    r.add_op("atari800.run_xex", path_arg, 1, op_run_xex);
    r.add_op("atari800.insert_cart", path_arg, 1, op_insert_cart);
    r.add_op("atari800.service", 0, 0, op_service);
    r.add_state("atari800.d1.image", d1);
    r.add_state("atari800.d2.image", d2);
    r.add_state("atari800.d3.image", d3);
    r.add_state("atari800.d4.image", d4);
    r.add_state("atari800.cartridge.image", cart);
    newdisk_register(r);
}

// What this machine has (T104.1 Q10): the SIO bus its drives sit on, and a cartridge port. No IEC, no "drive_a": a Commodore drive
// library is refused here.
static const char *const caps[] = { "sio", "cartridge" };

extern const CoreDesc atari800_desc;
const CoreDesc atari800_desc = {
    { 'A', '8', 'X', 'L' },
    "Atari 800 XL",
    "Atari 800 XL",
    "ATARI",
    "atari800",
    "Atari 800 XL Settings",
    1,
    roms, 2,
    settings, S_COUNT,
    tasks, 8,
    filetypes, 8,
    media, 5,
    // 384 x 288 at scale 3 is 1152 x 864 on the 1920 x 1080 output (T88 D5); x0 = 384 centres the window, +42 (14 hires pixels
    // x 3) puts the picture itself in the middle (hblank hides the window's last 28 columns, so the picture sits 14 left)
    { 384, 288, 3, 426, 108, 0x01000000u + A8_W_FB, 0x100000u, 4, A8_FB_LATEST, A8_FRAME_SEQ, A8_FB_BASE, A8_FB_STRIDE },
    { A8_STATUS, A8_ST_HELD, 200 },
    a8_init, a8_release, a8_warm_reset, 5,
    "atari800.service", 5, false,
    0, 0,
    atari800_register,
    caps, 2,
    false, false, 0, 0, 0, 0, 0, 0, 0,        // board_caps, id_shared, reset_writes, n, romset, window_media, sector_media, mouse, fbwin
    0, 0,                                      // type_op, run_op
    0, 0, 0, 0, 0,                             // cold_ram_bytes, cold_start, id_feat_reg, id_feat_mask, id_feat_val
    0, 0,                                      // menu_rec, menu_rec_n
    0,                                         // core_id
    0, 0,                                      // report_regs, n
    0, false, 0, 0, false,                     // rawkeys, own_volume, pad, live_ms, menu_first
    a8_media_flush, a8_media_pending,          // T180 D4: the SIO drives' sync
    &mount_slots,                              // T180 D2, D5: D1..D4 and the cartridge
    &hk_typing,                            // T181: the default hotkey map (hk_defaults.h)
    0, false, 0,                                     // alt_video, no_tone, rom_key
    "RAM size,Video standard",                 // T212 D13: machine_info
};
