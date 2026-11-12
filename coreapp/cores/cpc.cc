// The Amstrad CPC descriptor (T92 D11, T93 D3, T104): the ROM sets of the three models from roms/cpc, the model and the joystick settings, two
// DSK drives with write-back (cpc_disk.cc) and the tape (tape_deck.cc) from the file browser. The register block is cores/cpc/trx/cpc_regs.v;
// the picture is the core's own frame writer into the DDR2 (720 x 540, three buffers) which the host's pixel layer shows (T92 D6).
#include "hk_defaults.h"
#include "ctx.h"
#include "memops.h"
#include "boot.h"
#include "cpc_regs.h"
#include "cpc_disk.h"
#include "tape_deck.h"
#include "cores.h"
#include "newdisk.h"
#include <stdio.h>
#include <string.h>

// ---- the machine's state in the application -----------------------------------------------------------------------------------
enum { R_OS464 = 0, R_BAS464, R_OS664, R_BAS664, R_OS6128, R_BAS6128, R_AMS, R_COUNT };
static uint8_t rom_img[R_COUNT][CPC_ROM_BYTES];

struct ModelRoms { int os, bas, ams; };        // ams -1: none (the 464 has no disk drive)
static const ModelRoms model_roms[3] = {
    { R_OS464, R_BAS464, -1 },
    { R_OS664, R_BAS664, R_AMS },
    { R_OS6128, R_BAS6128, R_AMS },
};
static const char *const rom_file[R_COUNT] = { "os464.rom", "basic464.rom", "os664.rom", "basic664.rom", "os6128.rom", "basic6128.rom", "amsdos.rom" };

struct CpcState {
    bool hold;
    uint8_t have;                 // bit n: ROM file n was found
    TapeDeck deck;
    CpcDisks disks;
};

static CpcState *cpc_state(CoreCtx *c)
{
    if (!c->state) {
        c->state = new CpcState();
        memset(c->state, 0, sizeof(CpcState));
    }
    return (CpcState *)c->state;
}

static const TapeRegs tape_regs = { CPC_TAPE_CTRL, CPC_TAPE_BASE, CPC_TAPE_POS, CPC_TAPE_SEEK, CPC_TAPE_STATUS, CPC_W_TAPE, 2 };      // slot 2 of mount_slots: A, B, Tape

static void write_ctrl(CoreCtx *c)
{
    c->io->wr(CPC_CTRL, cpc_state(c)->hold ? CPC_CTRL_HOLD : 0);
}

// ---- settings ------------------------------------------------------------------------------------------------------------
enum { S_MODEL = 0, S_CRTC, S_JOY, S_WP, S_LED, S_COUNT };

static const char *const model_names[] = { "CPC 464", "CPC 664", "CPC 6128" };
static const char *const crtc_names[]  = { "Type 0 (HD6845S, UM6845)", "Type 1 (UM6845R)" };
static const char *const joy_names[]   = { "Normal", "Swapped" };
static const char *const led_names[]   = { "On", "Off" };   // T159 D-all: the A1200's "LED Load Indicator" (base operation led.load), case LED 1 = the gateware's drive activity
static const char *const wp_names[]    = { "No", "Yes" };

static bool model_complete(const CpcState *st, int m)
{
    const ModelRoms &r = model_roms[m];
    return (st->have & (1 << r.os)) && (st->have & (1 << r.bas)) && (r.ams < 0 || (st->have & (1 << r.ams)));
}

static int put_bank(CoreCtx *c, int bank, const uint8_t *src, char *msg, int msglen, const char *what)
{
    static uint8_t back[CPC_ROM_BYTES];
    c->io->wr(CPC_WIN_PAGE, (uint8_t)CPC_ROM_SEL(bank));
    c->io->rom_write(0, src, CPC_ROM_BYTES);
    c->io->rom_read(0, back, CPC_ROM_BYTES);
    c->io->wr(CPC_WIN_PAGE, 0);
    if (memcmp(back, src, CPC_ROM_BYTES) != 0) {
        snprintf(msg, msglen, "the core did not take the %s ROM (read-back differs)", what);
        return 1;
    }
    return 0;
}

// The model of a reset. A model whose ROM files are not all there is not run; another one is, and the screen says so (T92 D11: a model is
// offered only if its files exist). The three banks of the core's ROM area: OS, BASIC, AMSDOS.
static int select_model(CoreCtx *c, int want, char *msg, int msglen)
{
    CpcState *st = cpc_state(c);
    int m = want;
    if (!model_complete(st, m)) {
        m = -1;
        for (int k = 2; k >= 0 && m < 0; k--) {
            if (model_complete(st, k)) {
                m = k;
            }
        }
        if (m < 0) {
            snprintf(msg, msglen, "no ROM set is complete in the core's roms folder");
            return 1;
        }
        const ModelRoms &r = model_roms[want];
        const char *miss = rom_file[r.os];
        if (st->have & (1 << r.os)) {
            miss = (st->have & (1 << r.bas)) ? rom_file[r.ams] : rom_file[r.bas];
        }
        snprintf(c->notice, sizeof(c->notice), "%s is not in the core's roms folder - the %s runs instead", miss, model_names[m]);
        c->notice_pending = true;
    }
    const ModelRoms &r = model_roms[m];
    c->io->wr(CPC_MODEL, (uint8_t)m);
    if (put_bank(c, 0, rom_img[r.os], msg, msglen, "OS") || put_bank(c, 1, rom_img[r.bas], msg, msglen, "BASIC")) {
        return 1;
    }
    if (r.ams >= 0 && put_bank(c, 2, rom_img[r.ams], msg, msglen, "AMSDOS")) {
        return 1;
    }
    return 0;
}

static int op_model(CoreCtx *c, const OpArgs &a, OpOut &o) { return select_model(c, a.i(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_crtc(CoreCtx *c, const OpArgs &a, OpOut &) { c->io->wr(CPC_CRTC, (uint8_t)a.i(0)); return OPE_OK; }
static int op_joy(CoreCtx *c, const OpArgs &a, OpOut &) { c->io->wr(CPC_JOY, (uint8_t)a.i(0)); return OPE_OK; }
static int op_wp(CoreCtx *c, const OpArgs &a, OpOut &) { cpc_state(c)->disks.wp_setting = a.i(0) != 0; return OPE_OK; }
static const ArgSpec model_arg[] = { ARG_INT("value", 0, 2) };
static const ArgSpec flag_arg[]  = { ARG_INT("value", 0, 1) };

static const Setting settings[S_COUNT] = {
    { "Model",                  model_names, 3, 2, WHEN_RESET, "cpc.set_model" },
    { "CRTC type",              crtc_names,  2, 1, WHEN_RESET, "cpc.set_crtc" },
    { "Joystick ports",         joy_names,   2, 0, WHEN_LIVE,  "cpc.set_joy" },
    { "Disks write protected",  wp_names,    2, 0, WHEN_LIVE,  "cpc.set_wp" },
    { "LED Load Indicator", led_names, 2, 0, WHEN_LIVE, "led.load" },
};

// ---- ROMs ----------------------------------------------------------------------------------------------------------------
// The files are kept here and go into the core with the model (select_model): the ROM area holds one model's set at a time.
template <int N> static int op_put_rom(CoreCtx *c, const OpArgs &a, OpOut &)
{
    memcpy(rom_img[N], a.p(0), CPC_ROM_BYTES);
    cpc_state(c)->have |= (uint8_t)(1 << N);
    return OPE_OK;
}
static const ArgSpec rom_arg[] = { ARG_BLOB("data") };

// One complete set is enough (desc.h `set`): the 464 (bit 0), the 664 (bit 1) or the 6128 (bit 2); the AMSDOS ROM is the 664's and the 6128's.
static const RomSpec roms[R_COUNT] = {
    { "CPC 464 OS",      { "os464.rom", 0 },     { CPC_ROM_BYTES, 0 }, false, 0, "cpc.put_os464",    1 },
    { "CPC 464 BASIC",   { "basic464.rom", 0 },  { CPC_ROM_BYTES, 0 }, false, 0, "cpc.put_basic464", 1 },
    { "CPC 664 OS",      { "os664.rom", 0 },     { CPC_ROM_BYTES, 0 }, false, 0, "cpc.put_os664",    2 },
    { "CPC 664 BASIC",   { "basic664.rom", 0 },  { CPC_ROM_BYTES, 0 }, false, 0, "cpc.put_basic664", 2 },
    { "CPC 6128 OS",     { "os6128.rom", 0 },    { CPC_ROM_BYTES, 0 }, false, 0, "cpc.put_os6128",   4 },
    { "CPC 6128 BASIC",  { "basic6128.rom", 0 }, { CPC_ROM_BYTES, 0 }, false, 0, "cpc.put_basic6128", 4 },
    { "AMSDOS",          { "amsdos.rom", 0 },    { CPC_ROM_BYTES, 0 }, false, 0, "cpc.put_amsdos",   6 },
};

// ---- control -------------------------------------------------------------------------------------------------------------
static int cpc_init(CoreCtx *c, char *, int)
{
    CpcState *st = cpc_state(c);
    cpcd_flush_all(c, &st->disks);                // what the FDC wrote is in the files before the machine starts over
    st->hold = true;
    write_ctrl(c);
    deck_stop(c, &st->deck, tape_regs);           // a tape that was playing stops with the machine; it stays where it is
    c->io->mode_reset(true);
    return 0;
}

static int cpc_release(CoreCtx *c, char *, int)
{
    cpc_state(c)->hold = false;
    write_ctrl(c);
    c->io->mode_reset(false);
    return 0;
}

// T180 D4: CoreDesc::media_flush / media_pending
static int cpc_media_flush(CoreCtx *c, char *msg, int msglen) { return cpcd_flush_all(c, &cpc_state(c)->disks, msg, msglen) ? 0 : 1; }
static bool cpc_media_pending(CoreCtx *c) { return cpcd_pending(c, &cpc_state(c)->disks); }

// ---- disks ---------------------------------------------------------------------------------------------------------------
// The argument `boot` ends the mount in a cold start (the CPC runs RUN"DISC or |CPM from the drive the person types); without it the disk
// is only swapped in, for a program that asks for its next one.
static int mount_drive(CoreCtx *c, const char *path, int drive, char *msg, int msglen)
{
    CpcState *st = cpc_state(c);
    // the model that runs now, not the one the setting names for the next reset; while the stored media are mounted at the start the machine has not run yet, so the setting is the model
    int mi = c->media_restoring && c->settings ? c->settings->find("Model") : -1;
    bool drive_less = mi >= 0 ? c->settings->value(mi) == 0 : ((c->io->rd(CPC_STATUS) >> 2) & 3) == 0;
    if (drive_less) {
        snprintf(msg, msglen, "The CPC 464 has no disk drive - choose the 664 or the 6128 (Model, at the next reset)");
        return 1;
    }
    if (!(st->have & (1 << R_AMS))) {
        snprintf(msg, msglen, "amsdos.rom is not in the core's roms folder - the disk drive needs it (reload the ROMs once it is there)");
        return 1;
    }
    return cpcd_mount(c, &st->disks, drive, path, msg, msglen);
}
static int op_mount(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    if (mount_drive(c, a.s(0), a.i(1), o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    if (a.i(2) != 0) {
        return boot_cold_reset(c, o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
    }
    return OPE_OK;
}
static int op_eject(CoreCtx *c, const OpArgs &a, OpOut &o) { return cpcd_eject(c, &cpc_state(c)->disks, a.i(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static const ArgSpec drive_arg[] = { ARG_INT("drive", 0, 1) };
static const ArgSpec mount_args[] = { ARG_PATH("path"), ARG_INT("drive", 0, 1), ARG_INT("boot", 0, 1) };

// ---- tape ----------------------------------------------------------------------------------------------------------------
static int op_tape_insert(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return deck_insert(c, &cpc_state(c)->deck, tape_regs, a.s(0), a.i(1) != 0, false, o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static int op_tape_play(CoreCtx *c, const OpArgs &, OpOut &o) { return deck_play(c, &cpc_state(c)->deck, tape_regs, o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_tape_stop(CoreCtx *c, const OpArgs &, OpOut &) { deck_stop(c, &cpc_state(c)->deck, tape_regs); return OPE_OK; }
static int op_tape_rewind(CoreCtx *c, const OpArgs &, OpOut &) { deck_rewind(c, &cpc_state(c)->deck, tape_regs); return OPE_OK; }
static int op_tape_next(CoreCtx *c, const OpArgs &, OpOut &o) { return deck_block_step(c, &cpc_state(c)->deck, tape_regs, 1, o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_tape_prev(CoreCtx *c, const OpArgs &, OpOut &o) { return deck_block_step(c, &cpc_state(c)->deck, tape_regs, -1, o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_tape_eject(CoreCtx *c, const OpArgs &, OpOut &) { deck_eject(c, &cpc_state(c)->deck, tape_regs); return OPE_OK; }
static const ArgSpec insert_args[] = { ARG_PATH("path"), ARG_INT("play", 0, 1) };

// The tape engine stops itself at a stop marker and at the end; the FDC's dirty chunks go to the files when the writes have stopped.
static int op_service(CoreCtx *c, const OpArgs &, OpOut &)
{
    CpcState *st = cpc_state(c);
    deck_service(c, &st->deck, tape_regs);
    cpcd_poll(c, &st->disks, false);
    return OPE_OK;
}

static const char *tape_image(CoreCtx *c) { return deck_image(&cpc_state(c)->deck); }

// T180 D2, D5: A, B and Tape for the stored mounts and the media sets: the image's path
static const char *const slot_names[] = { "A", "B", "Tape" };
static const char *slot_spec(CoreCtx *c, int i)
{
    CpcState *st = cpc_state(c);
    const char *p = i < 2 ? (st->disks.d[i].mounted ? st->disks.d[i].path : "") : (st->deck.loaded ? st->deck.path : "");
    return p[0] ? p : 0;
}
static int slot_mount(CoreCtx *c, int i, const char *spec, char *msg, int msglen)
{
    return i < 2 ? mount_drive(c, spec, i, msg, msglen) : deck_insert(c, &cpc_state(c)->deck, tape_regs, spec, false, false, msg, msglen);
}
static int slot_eject(CoreCtx *c, int i, char *msg, int msglen)
{
    if (i < 2) {
        return cpcd_eject(c, &cpc_state(c)->disks, i, msg, msglen);
    }
    deck_eject(c, &cpc_state(c)->deck, tape_regs);
    return 0;
}
static const MountSlots mount_slots = { 3, slot_names, slot_spec, slot_mount, slot_eject };
static const char *disk_a(CoreCtx *c) { return cpcd_name(&cpc_state(c)->disks, 0); }
static const char *disk_b(CoreCtx *c) { return cpcd_name(&cpc_state(c)->disks, 1); }

static const OpBind drive0[] = { B_INT("drive", 0) }, drive1[] = { B_INT("drive", 1) };
static const OpBind mount0b[] = { B_FILE("path"), B_INT("drive", 0), B_INT("boot", 1) }, mount0[] = { B_FILE("path"), B_INT("drive", 0), B_INT("boot", 0) },
                    mount1[] = { B_FILE("path"), B_INT("drive", 1), B_INT("boot", 0) };
static const OpBind tape_play[] = { B_FILE("path"), B_INT("play", 1) }, tape_only[] = { B_FILE("path"), B_INT("play", 0) };

static const OpBind new_dsk[] = { B_TEXT("path"), B_STR("format", "cpc_dsk"), B_STR("name", "") };

static const Task tasks[] = {
    { "Reset",                      "machine.cold_reset",   0, 0, true },
    { "Reload ROMs, reset",         "machine.reload_roms",  0, 0, true },
    { "Tape: Play",                 "cpc.tape_play",        0, 0, true },
    { "Tape: Stop",                 "cpc.tape_stop",        0, 0, false },
    { "Tape: Rewind",               "cpc.tape_rewind",      0, 0, false },
    { "Tape: Previous block",       "cpc.tape_prev",        0, 0, false },
    { "Tape: Next block",           "cpc.tape_next",        0, 0, false },
    { "Tape: Eject",                "cpc.tape_eject",       0, 0, false },
    { "Eject A:",                   "cpc.eject",            drive0, 1, false },
    { "Eject B:",                   "cpc.eject",            drive1, 1, false },
    { "Create blank DSK",           "disk.create",          new_dsk, 3, false, "Name of the new DSK", "disk.suggest.cpc_dsk", true },
};

static const FileAction disk_actions[] = {
    { "Mount A: and reset", "cpc.mount", mount0b, 3, true },
    { "Mount A:",           "cpc.mount", mount0,  3, false },
    { "Mount B:",           "cpc.mount", mount1,  3, false },
};
static const FileAction tape_actions[] = {
    // the machine has to be at RUN" or |TAPE before the tape runs: the first action only inserts, "Tape: Play" starts it from the menu
    { "Insert tape",          "cpc.tape_insert", tape_only, 2, false },
    { "Insert tape and play", "cpc.tape_insert", tape_play, 2, true },
};
static const FileType filetypes[] = {
    { ".dsk", disk_actions, 3 }, { ".cdt", tape_actions, 2 },
};

static const MediaSlot media[] = {
    { "A:",   "cpc.a.image",    "cpc.eject",      drive0, 1, 0, ".dsk", "Mount A:", "Create blank DSK", 0 },
    { "B:",   "cpc.b.image",    "cpc.eject",      drive1, 1, 0, ".dsk", "Mount B:", "Create blank DSK", 0 },
    { "Tape", "cpc.tape.image", "cpc.tape_eject", 0, 0, 0, ".cdt", "Insert tape", 0, "Tape: Play,Tape: Stop,Tape: Rewind,Tape: Previous block,Tape: Next block" },
};

static const MemRegion cpc_mem_regions[] = { { 0, 0x20000, MK_RAMPAGE, 0, CPC_WIN_PAGE, "RAM (128 KB)" } };
static const MemMap cpc_mem = { cpc_mem_regions, 1, false, 0 };

static void cpc_register(OpRegistry &r)
{
    r.contributor("cpc");
    MEM_OPS(r, cpc_mem, 0x1FFFF);
    r.add_op("cpc.set_model", model_arg, 1, op_model);
    r.add_op("cpc.set_crtc", flag_arg, 1, op_crtc);
    r.add_op("cpc.set_joy", flag_arg, 1, op_joy);
    r.add_op("cpc.set_wp", flag_arg, 1, op_wp);
    r.add_op("cpc.put_os464", rom_arg, 1, op_put_rom<R_OS464>);
    r.add_op("cpc.put_basic464", rom_arg, 1, op_put_rom<R_BAS464>);
    r.add_op("cpc.put_os664", rom_arg, 1, op_put_rom<R_OS664>);
    r.add_op("cpc.put_basic664", rom_arg, 1, op_put_rom<R_BAS664>);
    r.add_op("cpc.put_os6128", rom_arg, 1, op_put_rom<R_OS6128>);
    r.add_op("cpc.put_basic6128", rom_arg, 1, op_put_rom<R_BAS6128>);
    r.add_op("cpc.put_amsdos", rom_arg, 1, op_put_rom<R_AMS>);
    r.add_op("cpc.mount", mount_args, 3, op_mount);
    r.add_op("cpc.eject", drive_arg, 1, op_eject);
    r.add_op("cpc.tape_insert", insert_args, 2, op_tape_insert);
    r.add_op("cpc.tape_play", 0, 0, op_tape_play);
    r.add_op("cpc.tape_stop", 0, 0, op_tape_stop);
    r.add_op("cpc.tape_rewind", 0, 0, op_tape_rewind);
    r.add_op("cpc.tape_prev", 0, 0, op_tape_prev);
    r.add_op("cpc.tape_next", 0, 0, op_tape_next);
    r.add_op("cpc.tape_eject", 0, 0, op_tape_eject);
    r.add_op("cpc.service", 0, 0, op_service);
    r.add_state("cpc.a.image", disk_a);
    r.add_state("cpc.b.image", disk_b);
    r.add_state("cpc.tape.image", tape_image);
    newdisk_register(r);
}

// Two floppy drives and a tape deck; no SIO, no IEC, no cartridge: a library of another machine's media is refused here.
static const char *const caps[] = { "floppy", "tape" };

extern const CoreDesc cpc_desc;
const CoreDesc cpc_desc = {
    { 'C', 'P', 'C', '_' },
    "Amstrad CPC 464 / 664 / 6128",
    "Amstrad CPC",
    "CPC",
    "cpc",
    "Amstrad CPC Settings",
    1,
    roms, R_COUNT,
    settings, S_COUNT,
    tasks, 11,
    filetypes, 2,
    media, 3,
    // 720 x 540 at scale 2 is 1440 x 1080 on the 1920 x 1080 output (T92 D6); three buffers of 1 MB from the core's reset value of FB_BASE
    { 720, 540, 2, 240, 0, 0x01000000u + CPC_W_FB, 0x100000u, 3, CPC_FB_LATEST, CPC_FRAME_SEQ, CPC_FB_BASE, CPC_FB_STRIDE },
    { CPC_STATUS, CPC_ST_HELD, 200 },
    cpc_init, cpc_release, 0, 10,
    "cpc.service", 50, true,
    0, 0,
    cpc_register,
    caps, 2,
    false, false, 0, 0, 0, 0, 0, 0, 0,        // board_caps, id_shared, reset_writes, n, romset, window_media, sector_media, mouse, fbwin
    0, 0,                                      // type_op, run_op
    0, 0, 0, 0, 0,                             // cold_ram_bytes, cold_start, id_feat_reg, id_feat_mask, id_feat_val
    0, 0,                                      // menu_rec, menu_rec_n
    0,                                         // core_id
    0, 0,                                      // report_regs, n
    0, false, 0, 0, false,                     // rawkeys, own_volume, pad, live_ms, menu_first
    cpc_media_flush, cpc_media_pending,        // T180 D4: the FDC's chunks and the sync
    &mount_slots,                              // T180 D2, D5: A, B, Tape
    &hk_typing,                            // T181: the default hotkey map (hk_defaults.h)
};
