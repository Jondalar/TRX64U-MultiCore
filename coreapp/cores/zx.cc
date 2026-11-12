// The ZX Spectrum descriptor (T91 D11, T93 D3, T104): ROMs from roms/zx, the model and the joystick settings, snapshots (.sna, .z80) and
// tapes (.tap, .tzx) from the file browser. The register block is cores/zx/trx/zx_regs.v; the picture is an RGB565 frame
// buffer the core writes into the DDR2 for the host's pixel layer (T119, as the C264's), so the descriptor has the buffers and the application
// sets the layer up; the host's scaler path is idle. Tape and snapshot formats are tape_pulses.cc and zx_snap.cc.
#include "hk_defaults.h"
#include "ctx.h"
#include "memops.h"
#include "boot.h"
#include "zx_regs.h"
#include "zx_snap.h"
#include "tape_deck.h"
#include "cores.h"
#include <stdio.h>
#include <string.h>

// ---- the machine's state in the application -----------------------------------------------------------------------------------
static uint8_t rom48[ZX_ROM48_BYTES];
static uint8_t rom128[ZX_ROM128_BYTES];

struct ZxState {
    bool hold;                    // CTRL b0, the application's reset hold
    bool ear;                     // CTRL b2, the tape on the speaker
    bool have48, have128;         // the ROM files that were found
    TapeDeck deck;
};

// The tape engine's registers (zx_regs.h) for the deck.
static const TapeRegs tape_regs = { ZX_TAPE_CTRL, ZX_TAPE_BASE, ZX_TAPE_POS, ZX_TAPE_SEEK, ZX_TAPE_STATUS, ZX_W_TAPE, 0 };         // slot 0 of mount_slots: Tape

static ZxState *zx_state(CoreCtx *c)
{
    if (!c->state) {
        c->state = new ZxState();
        memset(c->state, 0, sizeof(ZxState));
    }
    return (ZxState *)c->state;
}

static void write_ctrl(CoreCtx *c)
{
    ZxState *st = zx_state(c);
    c->io->wr(ZX_CTRL, (uint8_t)((st->hold ? ZX_CTRL_HOLD : 0) | (st->ear ? ZX_CTRL_EAR : 0)));
}

// ---- settings ------------------------------------------------------------------------------------------------------------
enum { S_MODEL = 0, S_JOY1, S_JOY2, S_EAR, S_COUNT };

static const char *const model_names[] = { "48K", "128K" };
static const char *const joy_names[]   = { "None", "Kempston", "Sinclair 1 (keys 6-0)", "Sinclair 2 (keys 1-5)", "Cursor (5 6 7 8 0)" };
static const char *const ear_names[]   = { "Off", "On" };

// The ROM of a model goes into the core's ROM area: 16 KB at the start for the 48K, 32 KB (ROM 0 and ROM 1) for the 128K. Read back.
static int put_model_rom(CoreCtx *c, int model, char *msg, int msglen)
{
    static uint8_t back[ZX_BANK_BYTES];
    const uint8_t *src = model ? rom128 : rom48;
    uint32_t n = model ? ZX_ROM128_BYTES : ZX_ROM48_BYTES;
    bool same = true;
    for (uint32_t off = 0; off < n; off += ZX_BANK_BYTES) {     // the aperture is 16 KB wide: one bank at a time (zx_regs.h)
        c->io->wr(ZX_WIN_PAGE, (uint8_t)ZX_ROM_SEL(off / ZX_BANK_BYTES));
        c->io->rom_write(0, src + off, ZX_BANK_BYTES);
        c->io->rom_read(0, back, ZX_BANK_BYTES);
        same = same && memcmp(back, src + off, ZX_BANK_BYTES) == 0;
    }
    c->io->wr(ZX_WIN_PAGE, 0);
    if (!same) {
        snprintf(msg, msglen, "the core did not take the %s ROM (read-back differs)", model ? "128K" : "48K");
        return 1;
    }
    return 0;
}

// The model of a reset (a setting taken in reset) or of a snapshot. A model whose ROM file is missing is not run; when the other one is
// there it runs instead and the screen says so (T91 D11: a model is offered only if its files exist).
static int select_model(CoreCtx *c, int want, int *got, char *msg, int msglen)
{
    ZxState *st = zx_state(c);
    bool have = want ? st->have128 : st->have48;
    bool other = want ? st->have48 : st->have128;
    if (!have) {
        if (!other) {
            snprintf(msg, msglen, "no ROM: %s is not in the core's roms folder", want ? "128.rom" : "48.rom");
            return 1;
        }
        snprintf(c->notice, sizeof(c->notice), "%s is not in the core's roms folder - the %s runs instead", want ? "128.rom" : "48.rom", want ? "48K" : "128K");
        c->notice_pending = true;
        want = !want;
    }
    c->io->wr(ZX_MODEL, (uint8_t)want);
    *got = want;
    return put_model_rom(c, want, msg, msglen);
}

static int op_model(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    int got;
    return select_model(c, a.i(0), &got, o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static int op_joy1(CoreCtx *c, const OpArgs &a, OpOut &) { c->io->wr(ZX_JOY1, (uint8_t)a.i(0)); return OPE_OK; }
static int op_joy2(CoreCtx *c, const OpArgs &a, OpOut &) { c->io->wr(ZX_JOY2, (uint8_t)a.i(0)); return OPE_OK; }
static int op_ear(CoreCtx *c, const OpArgs &a, OpOut &)
{
    zx_state(c)->ear = a.i(0) != 0;
    write_ctrl(c);
    return OPE_OK;
}
static const ArgSpec model_arg[] = { ARG_INT("value", 0, 1) };
static const ArgSpec joy_arg[]   = { ARG_INT("value", 0, 4) };
static const ArgSpec ear_arg[]   = { ARG_INT("value", 0, 1) };

static const Setting settings[S_COUNT] = {
    { "Model",              model_names, 2, 0, WHEN_RESET, "zx.set_model" },
    { "Joystick port 1",    joy_names,   5, 0, WHEN_LIVE,  "zx.set_joy1" },
    { "Joystick port 2",    joy_names,   5, 1, WHEN_LIVE,  "zx.set_joy2" },
    { "Tape sound",         ear_names,   2, 0, WHEN_LIVE,  "zx.set_ear" },
};

// ---- ROMs ----------------------------------------------------------------------------------------------------------------
// The files are kept here and go into the core with the model (set_model): the one ROM area holds one model's image at a time.
static int op_put_rom48(CoreCtx *c, const OpArgs &a, OpOut &)
{
    memcpy(rom48, a.p(0), ZX_ROM48_BYTES);
    zx_state(c)->have48 = true;
    return OPE_OK;
}
static int op_put_rom128(CoreCtx *c, const OpArgs &a, OpOut &)
{
    memcpy(rom128, a.p(0), ZX_ROM128_BYTES);
    zx_state(c)->have128 = true;
    return OPE_OK;
}
static const ArgSpec rom_arg[] = { ARG_BLOB("data") };

// One complete set is enough (desc.h `set`): a Spectrum with the 48K ROM only, or with the 128K ROM only, starts.
static const RomSpec roms[] = {
    { "48K ROM",  { "48.rom", 0 },  { ZX_ROM48_BYTES, 0 },  false, 0, "zx.put_rom48", 1 },
    { "128K ROM", { "128.rom", 0 }, { ZX_ROM128_BYTES, 0 }, false, 0, "zx.put_rom128", 2 },
};

// ---- control -------------------------------------------------------------------------------------------------------------
static int zx_init(CoreCtx *c, char *, int)
{
    ZxState *st = zx_state(c);
    st->hold = true;
    write_ctrl(c);
    deck_stop(c, &st->deck, tape_regs);            // a tape that was playing stops with the machine; it stays where it is
    c->io->mode_reset(true);
    return 0;
}

static int zx_release(CoreCtx *c, char *, int)
{
    ZxState *st = zx_state(c);
    st->hold = false;
    write_ctrl(c);
    c->io->mode_reset(false);
    return 0;
}

// ---- snapshots -----------------------------------------------------------------------------------------------------------
// The bench's sequence (tb_zx.v snap_load, tools/zx_snap.py load_script): the machine is held by CTRL b0 alone - the host's reset line stays
// off, because the load takes the machine out of the hold itself and a reset line would keep it in -, the model and its ROM, the banks
// through the RAM window, the register image, then SNAP_GO; the core is busy for a few microseconds and the Z80 runs when it is done.
static int zx_snap_run(CoreCtx *c, const char *path, char *msg, int msglen)
{
    ZxState *st = zx_state(c);
    bool z80;
    if (!zx_snap_kind(path, &z80)) {
        snprintf(msg, msglen, "%s is neither a .sna nor a .z80 snapshot", path);
        return 1;
    }
    CoreFile *f = c->fs->open(path, false);
    if (!f) {
        snprintf(msg, msglen, "cannot open %s", path);
        return 1;
    }
    ZxSnap s;
    int rc = zx_snap_parse(f, z80, &s, msg, msglen);
    c->fs->close(f);
    if (rc) {
        return 1;                                 // nothing of the machine has been touched
    }
    bool have = s.model == 128 ? st->have128 : st->have48;
    if (!have) {
        snprintf(msg, msglen, "the snapshot is a %dK Spectrum and %s is not in the core's roms folder", s.model, s.model == 128 ? "128.rom" : "48.rom");
        return 1;
    }
    CoreIo *io = c->io;
    st->hold = true;
    write_ctrl(c);
    deck_stop(c, &st->deck, tape_regs);
    int model;
    if (select_model(c, s.model == 128, &model, msg, msglen)) {
        return 1;
    }
    io->ram_memonly(true);                        // bank 3 spans $D000-$DFFF: those bytes are RAM here, not a bus cycle of the host's PLA
    for (int b = 0; b < 8; b++) {
        if (!(s.banks & (1 << b))) {
            continue;
        }
        io->wr(ZX_WIN_PAGE, (uint8_t)(b >> 2));
        io->ram_write((uint32_t)(b & 3) * ZX_BANK_BYTES, zx_snap_bank(b), ZX_BANK_BYTES);
    }
    io->ram_memonly(false);
    io->wr(ZX_WIN_PAGE, 0);
    for (int k = 0; k < 27; k++) {
        io->wr(ZX_SNAP_DIR + k, s.dir[k]);
    }
    io->wr(ZX_SNAP_7FFD, s.p7ffd);
    io->wr(ZX_SNAP_BORDER, s.border);
    for (int k = 0; k < 16; k++) {
        io->wr(ZX_SNAP_AY + k, s.ay[k]);
    }
    io->wr(ZX_SNAP_AYSEL, s.ay_sel);
    io->wr(ZX_SNAP_GO, 1);
    io->delay_us(100);                            // the start crosses into the machine's clock: busy is seen a few clocks after the write
    uint32_t t0 = io->now_ms();
    while (io->rd(ZX_STATUS) & ZX_ST_SNAPBUSY) {
        if (io->now_ms() - t0 > 200) {
            snprintf(msg, msglen, "the snapshot load did not finish (the core stays held)");
            return 1;
        }
        io->delay_us(100);
    }
    st->hold = false;
    write_ctrl(c);
    c->started = true;
    const char *slash = strrchr(path, '/');
    printf("[zx] snapshot %s: %dK, PC %02x%02x\n", slash ? slash + 1 : path, s.model, s.dir[9], s.dir[8]);
    return 0;
}

static int op_snap(CoreCtx *c, const OpArgs &a, OpOut &o) { return zx_snap_run(c, a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static const ArgSpec path_arg[] = { ARG_PATH("path") };

// ---- tape ----------------------------------------------------------------------------------------------------------------
// The deck is tape_deck.cc's.

static int op_tape_insert(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    bool m128 = (c->io->rd(ZX_STATUS) & 0x08) != 0;       // the machine that runs now decides what "stop the tape if in 48K mode" means
    return deck_insert(c, &zx_state(c)->deck, tape_regs, a.s(0), a.i(1) != 0, !m128, o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static int op_tape_play(CoreCtx *c, const OpArgs &, OpOut &o) { return deck_play(c, &zx_state(c)->deck, tape_regs, o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_tape_stop(CoreCtx *c, const OpArgs &, OpOut &) { deck_stop(c, &zx_state(c)->deck, tape_regs); return OPE_OK; }
static int op_tape_rewind(CoreCtx *c, const OpArgs &, OpOut &) { deck_rewind(c, &zx_state(c)->deck, tape_regs); return OPE_OK; }
static int op_tape_next(CoreCtx *c, const OpArgs &, OpOut &o) { return deck_block_step(c, &zx_state(c)->deck, tape_regs, 1, o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_tape_prev(CoreCtx *c, const OpArgs &, OpOut &o) { return deck_block_step(c, &zx_state(c)->deck, tape_regs, -1, o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_tape_eject(CoreCtx *c, const OpArgs &, OpOut &) { deck_eject(c, &zx_state(c)->deck, tape_regs); return OPE_OK; }
static int op_service(CoreCtx *c, const OpArgs &, OpOut &) { deck_service(c, &zx_state(c)->deck, tape_regs); return OPE_OK; }
static const char *tape_image(CoreCtx *c) { return deck_image(&zx_state(c)->deck); }

// T180 D2, D5: the one slot, Tape, for the stored mounts and the media sets: the tape's path. The model that decides the "stop the tape if in 48K mode" blocks is
// the one the register says (the machine has not run yet while the stored media are mounted at the start: the last run's).
static const char *const slot_names[] = { "Tape" };
static const char *slot_spec(CoreCtx *c, int)
{
    ZxState *st = zx_state(c);
    return st->deck.loaded && st->deck.path[0] ? st->deck.path : 0;
}
static int slot_mount(CoreCtx *c, int, const char *spec, char *msg, int msglen)
{
    bool m128 = (c->io->rd(ZX_STATUS) & 0x08) != 0;
    return deck_insert(c, &zx_state(c)->deck, tape_regs, spec, false, !m128, msg, msglen);
}
static int slot_eject(CoreCtx *c, int, char *, int)
{
    deck_eject(c, &zx_state(c)->deck, tape_regs);
    return 0;
}
static const MountSlots mount_slots = { 1, slot_names, slot_spec, slot_mount, slot_eject };

static const OpBind file_play[] = { B_FILE("path"), B_INT("play", 1) }, file_only[] = { B_FILE("path"), B_INT("play", 0) },
                    snap_bind[] = { B_FILE("path") };

static const Task tasks[] = {
    { "Reset",                      "machine.cold_reset",   0, 0, true },
    { "Reload ROMs, reset",         "machine.reload_roms",  0, 0, true },
    { "Tape: Play",                 "zx.tape_play",         0, 0, true },
    { "Tape: Stop",                 "zx.tape_stop",         0, 0, false },
    { "Tape: Rewind",               "zx.tape_rewind",       0, 0, false },
    { "Tape: Previous block",       "zx.tape_prev",         0, 0, false },
    { "Tape: Next block",           "zx.tape_next",         0, 0, false },
    { "Tape: Eject",                "zx.tape_eject",        0, 0, false },
};

static const FileAction snap_actions[] = { { "Run snapshot", "zx.snap", snap_bind, 1, true } };
static const FileAction tape_actions[] = {
    // the machine has to be at LOAD "" before the tape runs: the first action only inserts, "Tape: Play" starts it from the menu
    { "Insert tape", "zx.tape_insert", file_only, 2, false },
    { "Insert tape and play", "zx.tape_insert", file_play, 2, true },
};
static const FileType filetypes[] = {
    { ".sna", snap_actions, 1 }, { ".z80", snap_actions, 1 }, { ".tap", tape_actions, 2 }, { ".tzx", tape_actions, 2 },
};

static const MediaSlot media[] = {
    { "Tape", "zx.tape.image", "zx.tape_eject", 0, 0 },
};

static const ArgSpec insert_args[] = { ARG_PATH("path"), ARG_INT("play", 0, 1) };

static const MemRegion zx_mem_regions[] = { { 0, 0x20000, MK_RAMPAGE, 0, ZX_WIN_PAGE, "RAM (128 KB)" } };
static const MemMap zx_mem = { zx_mem_regions, 1, false, 0 };

static void zx_register(OpRegistry &r)
{
    r.contributor("zx");
    MEM_OPS(r, zx_mem, 0x1FFFF);
    r.add_op("zx.set_model", model_arg, 1, op_model);
    r.add_op("zx.set_joy1", joy_arg, 1, op_joy1);
    r.add_op("zx.set_joy2", joy_arg, 1, op_joy2);
    r.add_op("zx.set_ear", ear_arg, 1, op_ear);
    r.add_op("zx.put_rom48", rom_arg, 1, op_put_rom48);
    r.add_op("zx.put_rom128", rom_arg, 1, op_put_rom128);
    r.add_op("zx.snap", path_arg, 1, op_snap);
    r.add_op("zx.tape_insert", insert_args, 2, op_tape_insert);
    r.add_op("zx.tape_play", 0, 0, op_tape_play);
    r.add_op("zx.tape_stop", 0, 0, op_tape_stop);
    r.add_op("zx.tape_rewind", 0, 0, op_tape_rewind);
    r.add_op("zx.tape_prev", 0, 0, op_tape_prev);
    r.add_op("zx.tape_next", 0, 0, op_tape_next);
    r.add_op("zx.tape_eject", 0, 0, op_tape_eject);
    r.add_op("zx.service", 0, 0, op_service);
    r.add_state("zx.tape.image", tape_image);
}

// A tape deck (no disk, no SIO, no IEC, no cartridge): a library of another machine's media is refused here.
static const char *const caps[] = { "tape" };

extern const CoreDesc zx_desc;
const CoreDesc zx_desc = {
    { 'Z', 'X', 'S', 'P' },
    "ZX Spectrum 48K / 128K",
    "ZX Spectrum",
    "SPECTRUM",
    "zx",
    "ZX Spectrum Settings",
    1,
    roms, 2,
    settings, S_COUNT,
    tasks, 8,
    filetypes, 4,
    media, 1,
    // 344 x 280 shown 3 x 3 (1032 x 840), centred on 1920 x 1080; the buffers are the core's reset values of FB_BASE / FB_STRIDE
    { 344, 280, 3, 444, 120, ZX_FB_ADDR, ZX_FB_STRIDE_BYTES, 4, ZX_FB_LATEST, ZX_FRAME_SEQ, ZX_FB_BASE, ZX_FB_STRIDE },
    { ZX_STATUS, ZX_ST_HELD, 200 },
    zx_init, zx_release, 0, 10,
    "zx.service", 100, false,
    0, 0,
    zx_register,
    caps, 1,
    false, false, 0, 0, 0, 0, 0, 0, 0,        // board_caps, id_shared, reset_writes, n, romset, window_media, sector_media, mouse, fbwin
    0, 0,                                      // type_op, run_op
    0, 0, 0, 0, 0,                             // cold_ram_bytes, cold_start, id_feat_reg, id_feat_mask, id_feat_val
    0, 0,                                      // menu_rec, menu_rec_n
    0,                                         // core_id
    0, 0,                                      // report_regs, n
    0, false, 0, 0, false,                     // rawkeys, own_volume, pad, live_ms, menu_first
    0, 0,                                      // media_flush, media_pending
    &mount_slots,                              // T180 D2, D5: Tape
    &hk_typing,                            // T181: the default hotkey map (hk_defaults.h)
};
