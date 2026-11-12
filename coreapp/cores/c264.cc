// The Commodore 264 descriptor (T90, T104; Alex 2026-10-22: "apart from trx64 and c64u, switch everything to the generic core app for now"). It
// replaces the C16 build of Gideon's application (fork branch t90-c16app, software/io/c16/c16.cc): KERNAL and BASIC (and the Plus/4's two
// function ROMs) through the 16 KB aperture with ROM_SEL, model / RAM size / joystick swap as register settings taken in reset, Reset and Reload
// ROMs, a .prg run or loaded through the PRG library (cbm_run.cc), a 16 or 32 KB cartridge image, and the picture: four 384 x 288 buffers the
// core's frame writer fills, shown 3 x 3 on the pixel layer. The register block is cores/c264/rtl/trx/c264_regs.v. What the old application
// did that this one does not: see the table in T90 section 5. The 1541 drive A is the cbm1541 library (T104.1 D15, cbm1541.cc), spliced in
// through cbm1541_desc.h.
#include "hk_defaults.h"
#include "ctx.h"
#include "memops.h"
#include "boot.h"
#include "c264_regs.h"
#include "cbm_run.h"
#include "cbm1541.h"
#include "cbm1541_desc.h"
#include "cores.h"
#include <stdio.h>
#include <string.h>

// ---- the machine's state in the application ----------------------------------------------------------------------------------
struct C264State {
    CbmRun run;
    uint8_t cart_bits;                                  // which cartridge halves are in the core (they stay across resets)
    char cart_name[56];
    char cart_path[FS_PATH_MAX];                                // T180 D2: where the image came from, for the stored mounts
};

static C264State *c264_state(CoreCtx *c)
{
    if (!c->state) {
        c->state = new C264State();
        memset(c->state, 0, sizeof(C264State));
    }
    return (C264State *)c->state;
}

// ---- settings ------------------------------------------------------------------------------------------------------------------
// Model, RAM size and the joystick swap are bits of the core's own registers, taken at its next reset (c264_regs.v: MODEL and RAM_SIZE are
// the reset-time inputs of the core).
#define KEY_MODEL "Model (at next reset)"
#define KEY_RAM   "RAM (at next reset)"
#define KEY_JOY   "Joystick ports (at next reset)"
static const char *const model_names[] = { "C16 / C116", "Plus/4" };
static const char *const ram_names[]   = { "16 KB (C16, C116)", "64 KB" };
static const char *const joy_names[]   = { "Joystick 1 = port 1", "Swapped" };
static const RegBind b_model = { C264_MODEL, 0, 1, 0 };
static const RegBind b_ram   = { C264_RAM_SIZE, 0, 1, 0 };
static const RegBind b_joy   = { C264_JOY_SWAP, 0, 1, 0 };

static const Setting settings[3 + CBM1541_NSETTINGS] = {
    { KEY_MODEL, model_names, 2, 1, WHEN_RESET, 0, &b_model, 0, 0, 0, 0, "Model" },
    { KEY_RAM,   ram_names,   2, 1, WHEN_RESET, 0, &b_ram, 0, 0, 0, 0, "RAM" },
    { KEY_JOY,   joy_names,   2, 0, WHEN_RESET, 0, &b_joy, 0, 0, 0, 0, "Joystick ports" },
    CBM1541_SETTINGS,
};

// ---- ROMs ----------------------------------------------------------------------------------------------------------------------
// One array at a time through the 16 KB aperture: ROM_SEL names it (the core takes the value a few clocks later; the read back says it did), the
// bytes go in and are read back.
static int put_array(CoreCtx *c, int sel, const uint8_t *data, uint32_t n, const char *role, char *msg, int msglen)
{
    static uint8_t back[C264_ROM_BYTES];
    if (n != C264_ROM_BYTES) {
        snprintf(msg, msglen, "%s: %u bytes, the machine takes %u", role, (unsigned)n, (unsigned)C264_ROM_BYTES);
        return 1;
    }
    c->io->wr(C264_ROM_SEL, (uint8_t)sel);
    if (c->io->rd(C264_ROM_SEL) != sel) {
        snprintf(msg, msglen, "C16 ROMs: the core did not take ROM_SEL for the %s", role);
        return 1;
    }
    c->io->rom_write(0, data, C264_ROM_BYTES);
    c->io->rom_read(0, back, C264_ROM_BYTES);
    if (memcmp(back, data, C264_ROM_BYTES) != 0) {
        snprintf(msg, msglen, "C16 ROMs: the core did not take the %s (read-back differs)", role);
        return 1;
    }
    return 0;
}

template <int ARR> static int op_put_rom(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    static const char *const role[] = { "KERNAL", "BASIC", "function ROM low", "function ROM high" };
    return put_array(c, ARR, a.p(0), a.len(0), role[ARR], o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static const ArgSpec rom_arg[] = { ARG_BLOB("data") };

// The names the old application tried (the VICE names and the Commodore part numbers); PAL images only. The two function ROMs are the Plus/4's
// 3-plus-1: without them the Plus/4 model has its BASIC and no built-in programs.
static const RomSpec roms[5] = {
    { "KERNAL", { "kernal.bin", "kernal", "kernal-318004-05.bin", "kernal.318004-05.bin", "318004-05.bin", 0 }, { C264_ROM_BYTES, 0 }, true, 0, "c264.put_kernal" },
    { "BASIC", { "basic.bin", "basic", "basic-318006-01.bin", "basic.318006-01.bin", "318006-01.bin", 0 }, { C264_ROM_BYTES, 0 }, true, 0, "c264.put_basic" },
    { "Function ROM low (Plus/4)", { "3plus1lo.bin", "3plus1-317053-01.bin", "3-plus-1.317053-01.bin", "func-lo.bin", "317053-01.bin", 0 },
      { C264_ROM_BYTES, 0 }, false, 0, "c264.put_func_lo" },
    { "Function ROM high (Plus/4)", { "3plus1hi.bin", "3plus1-317054-01.bin", "3-plus-1.317054-01.bin", "func-hi.bin", "317054-01.bin", 0 },
      { C264_ROM_BYTES, 0 }, false, 0, "c264.put_func_hi" },
    CBM1541_ROM,
};

// ---- control -------------------------------------------------------------------------------------------------------------------
static int c264_init(CoreCtx *c, char *, int)
{
    cbm_run_clear(&c264_state(c)->run);                // a reset ends a program that was still waiting for READY
    c->io->mode_reset(true);
    return 0;
}

static int c264_release(CoreCtx *c, char *, int)
{
    c->io->mode_reset(false);
    return 0;
}

// ---- .prg ----------------------------------------------------------------------------------------------------------------------
// The numbers of the old application (c16.cc load_prg / type_keys): BASIC starts at $1001, the start pointer pair at $2B with its window
// $0400-$2000, the three end pointers behind it, the keyboard buffer at $0527 with its count at $EF.
static const char *refuse_wrap(CoreCtx *c, uint32_t load, uint32_t len)
{
    int i = c->settings ? c->settings->find(KEY_RAM) : -1;
    if (i >= 0 && c->settings->value(i) == 0 && load + len > 0x4000) {          // the 16 KB are mirrored through the 64 KB space: this would wrap
        return "This program needs the 64 KB RAM setting (C16 Settings).";
    }
    return 0;
}

static CbmRunMachine machine(void)
{
    CbmRunMachine m;
    memset(&m, 0, sizeof(m));
    m.tag = "c264";
    m.prg.basic_starts[0] = 0x1001;
    m.prg.nbasic_starts = 1;
    m.prg.txttab = C264_TXTTAB;
    m.prg.txttab_min = 0x0400;
    m.prg.txttab_max = 0x2000;
    m.prg.vartab = 0x2D; m.prg.arytab = 0x2F; m.prg.strend = 0x31;
    m.prg.kbd_buffer = C264_KBD_BUF; m.prg.kbd_count = C264_KBD_COUNT;
    m.prg.p00_header = 0x1A;
    m.prg.min_size = 3;
    m.prg.max_size = 0x1A + 2 + 0x10000;
    m.ready_ms = 2200;                                  // with the settle time the old application's fixed 2.5 s after a reset
    m.settle_ms = 300;
    m.giveup_ms = 4000;
    m.refuse = refuse_wrap;
    return m;
}

static int op_run(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return cbm_run_queue(c, &c264_state(c)->run, machine(), a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static int op_load(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return cbm_load_now(c, &c264_state(c)->run, machine(), a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static int op_service(CoreCtx *c, const OpArgs &, OpOut &o)
{
    int rc = cbm_run_service(c, &c264_state(c)->run, machine());
    o.more = cbm1541_service(c);                        // the drive's write-back, on the same task (T104.1 D15 poll)
    return rc;
}
static int op_type(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return cbm_run_queue_script(c, &c264_state(c)->run, machine(), a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static const ArgSpec text_arg[] = { ARG_STRING("text", 63) };
static const ArgSpec path_arg[] = { ARG_PATH("path") };
static const char *prg_name(CoreCtx *c) { return cbm_run_name(&c264_state(c)->run); }
static const char *prg_pending(CoreCtx *c) { return cbm_run_pending(&c264_state(c)->run); }

// ---- cartridge images ----------------------------------------------------------------------------------------------------------
// A .bin of 16384 or 32768 bytes: the first 16 KB are the low half ($8000), the next 16 KB the high half. The bank register at $FDD0-$FDDF is live
// only in the Plus/4 model, so the model is set to Plus/4 (the setting, kept in the .cfg) and the machine reset. There is no cartridge port.
static int cart_load(CoreCtx *c, const char *path, char *msg, int msglen)
{
    static uint8_t img[2 * C264_ROM_BYTES];
    C264State *st = c264_state(c);
    CoreFile *f = c->fs->open(path, false);
    if (!f) {
        snprintf(msg, msglen, "cannot open %s", path);
        return 1;
    }
    uint64_t n = f->size();
    if (n != C264_ROM_BYTES && n != 2 * C264_ROM_BYTES) {
        c->fs->close(f);
        snprintf(msg, msglen, "A cartridge image is a .bin of 16384 or 32768 bytes (this one is %u).", (unsigned)fs_size32(n));
        return 1;
    }
    bool ok = f->read(0, img, (uint32_t)n);
    c->fs->close(f);
    if (!ok) {
        snprintf(msg, msglen, "cannot read %s (the medium is gone?)", path);
        return 1;
    }
    if (put_array(c, C264_ARR_CART_LO, img, C264_ROM_BYTES, "cartridge image", msg, msglen)) {
        return 1;
    }
    if (n == 2 * C264_ROM_BYTES && put_array(c, C264_ARR_CART_HI, img + C264_ROM_BYTES, C264_ROM_BYTES, "cartridge image (high half)", msg, msglen)) {
        return 1;
    }
    st->cart_bits = n == C264_ROM_BYTES ? 0x01 : 0x03;
    c->io->wr(C264_CART, st->cart_bits);
    const char *slash = strrchr(path, '/');
    fs_short_name(st->cart_name, sizeof(st->cart_name), slash ? slash + 1 : path);
    snprintf(st->cart_path, sizeof(st->cart_path), "%s", path);
    mount_notify(c, 1);                                 // T180 D2: the slots of the 264 are Drive A and Cartridge
    int mi = c->settings ? c->settings->find(KEY_MODEL) : -1;
    if (mi >= 0) {
        c->settings->set(mi, 1);                        // Plus/4
    }
    printf("[c264] cartridge image %s, %u bytes; model Plus/4, reset\n", st->cart_name, (unsigned)n);
    return boot_cold_reset(c, msg, msglen);
}
static int op_cart_load(CoreCtx *c, const OpArgs &a, OpOut &o) { return cart_load(c, a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }

static int cart_eject(CoreCtx *c, char *msg, int msglen)
{
    C264State *st = c264_state(c);
    st->cart_bits = 0;
    st->cart_name[0] = 0;
    st->cart_path[0] = 0;
    c->io->wr(C264_CART, 0);
    mount_notify(c, 1);                                 // T180 D2
    return boot_cold_reset(c, msg, msglen);
}
static int op_cart_eject(CoreCtx *c, const OpArgs &, OpOut &o) { return cart_eject(c, o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }

// T180 D2, D5: the two slots of the 264: Drive A and the cartridge image (the .bin's path)
static const char *const slot_names[] = { "Drive A", "Cartridge" };
static const char *slot_spec(CoreCtx *c, int i)
{
    if (i == 0) {
        return cbm1541_slot_spec(c);
    }
    C264State *st = c264_state(c);
    return st->cart_bits && st->cart_path[0] ? st->cart_path : 0;
}
static int slot_mount(CoreCtx *c, int i, const char *spec, char *msg, int msglen)
{
    return i == 0 ? cbm1541_slot_mount(c, spec, msg, msglen) : cart_load(c, spec, msg, msglen);
}
static int slot_eject(CoreCtx *c, int i, char *msg, int msglen)
{
    return i == 0 ? cbm1541_slot_eject(c, msg, msglen) : cart_eject(c, msg, msglen);
}
static const MountSlots mount_slots = { 2, slot_names, slot_spec, slot_mount, slot_eject };
static const char *cart_name(CoreCtx *c) { C264State *st = c264_state(c); return st->cart_name[0] ? st->cart_name : 0; }

static const OpBind file_bind[] = { B_FILE("path") };
static const FileAction prg_actions[] = {
    { "Run PRG", "c264.run_prg", file_bind, 1, true, 0, 0 },
    { "Load PRG (no reset, no start)", "c264.load_prg", file_bind, 1, false, 0, 0 },
};
static const FileAction bin_actions[] = { { "Load as cartridge (Plus/4)", "c264.cart_load", file_bind, 1, true, 0, 0 } };
static const FileType filetypes[3 + CBM1541_NFILETYPES] = {
    { ".prg", prg_actions, 2 },
    { ".p00", prg_actions, 2 },
    { ".bin", bin_actions, 1 },
    CBM1541_FILETYPES,
};

static const Task tasks[3 + CBM1541_NTASKS] = {
    { "Reset", "machine.cold_reset", 0, 0, true },
    { "Reload ROMs, reset", "machine.reload_roms", 0, 0, true },
    { "Eject cartridge, reset", "c264.cart_eject", 0, 0, true },
    CBM1541_TASKS,
};

static const MediaSlot media[] = {
    { "Program", "c264.program", 0, 0, 0 },
    { "Cartridge", "c264.cartridge", 0, 0, 0, 0, ".bin", "Load as cartridge (Plus/4)", 0, "Eject cartridge, reset" },
    CBM1541_MEDIA,
};

static const MemRegion c264_mem_regions[] = { { 0, 0x10000, MK_RAM, 0, 0, "RAM" } };
static const MemMap c264_mem = { c264_mem_regions, 1, true, 0 };

static void c264_register(OpRegistry &r)
{
    r.contributor("c264");
    MEM_OPS(r, c264_mem, 0xFFFF);
    r.add_op("c264.put_kernal", rom_arg, 1, op_put_rom<C264_ARR_KERNAL>);
    r.add_op("c264.put_basic", rom_arg, 1, op_put_rom<C264_ARR_BASIC>);
    r.add_op("c264.put_func_lo", rom_arg, 1, op_put_rom<C264_ARR_FUNC_LO>);
    r.add_op("c264.put_func_hi", rom_arg, 1, op_put_rom<C264_ARR_FUNC_HI>);
    r.add_op("c264.run_prg", path_arg, 1, op_run);
    r.add_op("c264.load_prg", path_arg, 1, op_load);
    r.add_op("c264.cart_load", path_arg, 1, op_cart_load);
    r.add_op("c264.cart_eject", 0, 0, op_cart_eject);
    r.add_op("c264.service", 0, 0, op_service);
    r.add_op("c264.type_script", text_arg, 1, op_type);
    r.add_state("c264.program", prg_name);
    r.add_state("c264.pending", prg_pending);
    r.add_state("c264.cartridge", cart_name);
    cbm1541_register(r);
}

// The 264 core carries the 1541 (drive_l2) on the serial bus; the cbm1541 library serves it (see vc20.cc).
static const char *const caps[] = { "iec", "drive_a" };

extern const CoreDesc c264_desc;
const CoreDesc c264_desc = {
    { 'C', '2', '6', '4' },
    "Commodore 264 (C16, Plus/4)",
    "Commodore 264",
    "C264",
    "c16",
    "C16 Settings",
    1,
    roms, 5,
    settings, 3 + CBM1541_NSETTINGS,
    tasks, 3 + CBM1541_NTASKS,
    filetypes, 3 + CBM1541_NFILETYPES,
    media, 3,
    // 384 x 288 shown 3 x 3 (1152 x 864, 4:3), centred on 1920 x 1080; the buffers are the core's reset values of FB_BASE / FB_STRIDE
    { 384, 288, 3, 384, 108, C264_FB_ADDR, C264_FB_STRIDE_BYTES, 4, C264_FB_LATEST, C264_FRAME_SEQ, C264_FB_BASE, C264_FB_STRIDE },
    { C264_STATUS, C264_ST_HELD, 200 },
    c264_init, c264_release, 0, 20,
    "c264.service", 20, false,
    0, 0,
    c264_register,
    caps, 2,
    false, false, 0, 0, 0, 0, 0, 0, 0,
    "c264.type_script",
    "c264.run_prg",
    0x10000,
    0, 0, 0, 0,                                // cold_start, id_feat_reg, id_feat_mask, id_feat_val
    0, 0,                                      // menu_rec, menu_rec_n
    0,                                         // core_id
    0, 0,                                      // report_regs, n
    0, false, 0, 0, false,                     // rawkeys, own_volume, pad, live_ms, menu_first
    cbm1541_media_flush, cbm1541_media_pending,   // T180 D4: the drive's write-back
    &mount_slots,                              // T180 D2, D5: Drive A and Cartridge
    &hk_c264,                              // T181: the default hotkey map (hk_defaults.h)
    0,                                     // alt_video
    "Model,RAM",                               // T212 D13: machine_info
};
