// The NES descriptor (T93 D3, T95): no ROMs, one setting, the .nes file that is the program. The image goes into the DDR2 window
// (PRG at +0, CHR at +0x200000 of CART_BASE), the header into the INES registers, all while the reset is held; the core derives
// its mapper flags word and masks from the header bytes (nes_flags.v). The controllers read the C64 joysticks and keys themselves (cores/nes/trx/nes_pad.v); the USB pads (T156) are a register per player that the service writes (pad.h, PadSpec below).
#include "hk_defaults.h"
#include "ctx.h"
#include "memops.h"
#include "boot.h"
#include "engines.h"
#include "nes_regs.h"
#include "ines.h"
#include "cores.h"
#include "pad.h"
#include <stdio.h>
#include <string.h>

struct NesState {
    char name[56];
    char path[FS_PATH_MAX];               // T180 D2: where it came from, for the stored mounts
    InesInfo info;
};

static NesState *nes_state(CoreCtx *c)
{
    if (!c->state) {
        c->state = new NesState();
        memset(c->state, 0, sizeof(NesState));
    }
    return (NesState *)c->state;
}

static const char *const sys_names[] = { "NTSC", "PAL", "Dendy" };
static int op_system(CoreCtx *c, const OpArgs &a, OpOut &) { c->io->wr(NES_SYSTEM, (uint8_t)a.i(0)); return OPE_OK; }
static const ArgSpec system_arg[] = { ARG_INT("value", 0, 2) };
static const char *const joy_names[] = { "Normal", "Swapped" };
static int op_joy(CoreCtx *c, const OpArgs &a, OpOut &) { c->io->wr(NES_JOY, (uint8_t)a.i(0)); return OPE_OK; }
static const ArgSpec joy_arg[] = { ARG_INT("value", 0, 1) };
static const char *const padmap_names[] = { "Default", "Custom" };
static const Setting settings[3] = {
    { "Console type", sys_names, 3, 0, WHEN_RESET, "nes.set_system" },
    { "Joystick ports", joy_names, 2, 0, WHEN_LIVE, "nes.set_joy" },
    { "Pad map",      padmap_names, 2, 0, WHEN_LIVE, "pad.set_map" },
};

// T156: the USB pads. Bit order of the register is the controller's own (A B Select Start Up Down Left Right); the default map is D3 with the SNES
// layout's A (right) and B (bottom).
static const char *const pad_names[8] = { "A", "B", "Select", "Start", "Up", "Down", "Left", "Right" };
static const uint8_t pad_def[8] = { PAD_D3_RIGHT, PAD_D3_BOTTOM, PAD_D3_SELECT, PAD_D3_START, PAD_SRC_UP, PAD_SRC_DOWN, PAD_SRC_LEFT, PAD_SRC_RIGHT };
static const PadSpec pad_spec = { { NES_PAD1, NES_PAD2 }, 8, pad_names, pad_def };

static int nes_init(CoreCtx *c, char *, int)
{
    c->io->wr(NES_CTRL, NES_CTRL_HOLD);
    c->io->mode_reset(true);
    return 0;
}

static int nes_release(CoreCtx *c, char *, int)
{
    c->io->wr(NES_CTRL, 0);
    c->io->mode_reset(false);
    return 0;
}

static bool copy_image(CoreCtx *c, CoreFile *f, uint32_t file_off, uint32_t bytes, uint32_t win_off)
{
    static uint8_t chunk[32768];
    for (uint32_t o = 0; o < bytes; o += sizeof(chunk)) {
        uint32_t n = bytes - o < sizeof(chunk) ? bytes - o : sizeof(chunk);
        if (!f->read(file_off + o, chunk, n)) {
            return false;
        }
        c->io->ddr_write(win_off + o, chunk, n);
    }
    return true;
}

// The file hook of ".nes": parse, refuse what the core cannot run (the machine is not touched then), hold, header, image, SYSTEM,
// release.
static int nes_run(CoreCtx *c, const char *path, int, char *msg, int msglen)
{
    NesState *st = nes_state(c);
    MediaLoading loading(c);                               // T226 S1
    CoreFile *f = c->fs->open(path, false);
    if (!f) {
        snprintf(msg, msglen, "cannot open %s", path);
        return 1;
    }
    uint8_t h[16];
    memset(h, 0, sizeof(h));
    if (f->size() >= 16 && !f->read(0, h, 16)) {
        c->fs->close(f);
        snprintf(msg, msglen, "cannot read %s", path);
        return 1;
    }
    InesInfo info = ines_parse(h, fs_size32(f->size()));
    if (!info.ok) {
        c->fs->close(f);
        snprintf(msg, msglen, "%s", info.err);
        return 1;
    }
    nes_init(c, msg, msglen);                              // hold: from here on the old program is gone
    c->program_loaded = false;
    st->path[0] = 0;
    c->io->delay_ms(2);                                    // the caches flush when the reset begins
    for (int i = 0; i < 16; i++) {
        c->io->wr(NES_INES + i, h[i]);
    }
    for (int i = 0; i < 4; i++) {                          // CART_BASE = 0x01000000: the window's start
        c->io->wr(NES_CART_BASE + i, (uint8_t)(0x01000000u >> (8 * i)));
    }
    bool ok = copy_image(c, f, info.prg_off, info.prg_bytes, NES_W_PRG) &&
              (info.chr_bytes == 0 || copy_image(c, f, info.chr_off, info.chr_bytes, NES_W_CHR));
    c->fs->close(f);
    if (!ok) {
        snprintf(msg, msglen, "cannot read %s (the medium is gone?). The machine stays held.", path);
        mount_notify(c, 0);                                // the old program is gone and the slot is empty (T180 D2)
        return 1;
    }
    st->info = info;
    const char *slash = strrchr(path, '/');
    fs_short_name(st->name, sizeof(st->name), slash ? slash + 1 : path);
    c->program_loaded = true;
    snprintf(st->path, sizeof(st->path), "%s", path);                // T180 D2: stored by the settings pass of boot_finish below (the Persist item's apply)
    printf("[nes] %s: mapper %u, PRG %u KB, CHR %u KB%s, flags %08x%08x\n", st->name, info.mapper, (unsigned)(info.prg_bytes >> 10),
           (unsigned)(info.chr_bytes >> 10), info.chr_ram ? " (CHR RAM)" : "", (unsigned)(info.flags >> 32), (unsigned)info.flags);
    return boot_finish(c, msg, msglen);
}

static int op_run(CoreCtx *c, const OpArgs &a, OpOut &o) { return nes_run(c, a.s(0), 0, o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static const ArgSpec run_args[] = { ARG_PATH("path") };

static const Task tasks[] = {
    { "Reset", "machine.cold_reset", 0, 0, true },
    { "Map buttons, pad 1", "menu.pad_map_1", 0, 0, false, 0, 0, false, 0, "input" },
    { "Map buttons, pad 2", "menu.pad_map_2", 0, 0, false, 0, 0, false, 0, "input" },
};

static const OpBind run_bind[] = { B_FILE("path") };
static const FileAction nes_actions[] = { { "Run", "nes.run", run_bind, 1, true } };
static const FileType filetypes[] = { { ".nes", nes_actions, 1 } };

static const char *cart_name(CoreCtx *c)
{
    NesState *st = nes_state(c);
    return c->program_loaded ? st->name : 0;
}
static const MediaSlot media[] = { { "Cartridge", "nes.cartridge.image", 0, 0, 0 } };

// T180 D2, D5: the cartridge for the stored mounts and the media sets: the .nes file's path. Emptying the slot holds the machine and leaves it with no program
// (the start waits for one, as at a first start).
static const char *const slot_names[] = { "Cartridge" };
static const char *slot_spec(CoreCtx *c, int)
{
    NesState *st = nes_state(c);
    return c->program_loaded && st->path[0] ? st->path : 0;
}
static int slot_mount(CoreCtx *c, int, const char *spec, char *msg, int msglen)
{
    return nes_run(c, spec, 0, msg, msglen);
}
static int slot_eject(CoreCtx *c, int, char *msg, int msglen)
{
    nes_init(c, msg, msglen);
    c->program_loaded = false;
    nes_state(c)->path[0] = 0;
    mount_notify(c, 0);
    return 0;
}
static const MountSlots mount_slots = { 1, slot_names, slot_spec, slot_mount, slot_eject };

// The built-in contributor of this core (T104.1 D14).
static const MemMap nes_mem = { 0, 0, false, "The NES core's CPU RAM is not in a window the application reaches (the DDR2 window holds the cartridge image, not the machine)" };

static void nes_register(OpRegistry &r)
{
    r.contributor("nes");
    MEM_OPS(r, nes_mem, 0xFFFF);
    r.add_op("nes.set_system", system_arg, 1, op_system);
    r.add_op("nes.set_joy", joy_arg, 1, op_joy);
    r.add_op("nes.run", run_args, 1, op_run);
    r.add_state("nes.cartridge.image", cart_name);
}

// The NES has a cartridge and nothing a drive library could serve (T104.1 Q10).
static const char *const caps[] = { "cartridge", "pad" };

extern const CoreDesc nes_desc;
const CoreDesc nes_desc = {
    { 'N', 'E', 'S', '_' },
    "Nintendo Entertainment System",
    "Nintendo Entertainment System",
    "NES",
    "nes",
    "NES Settings",
    1,
    0, 0,
    settings, 3,
    tasks, 3,
    filetypes, 1,
    media, 1,
    // 256 x 224 at scale 3 is 768 x 672, centred on 1920 x 1080 (T95 D7)
    { 256, 224, 3, 576, 204, 0x01000000u + NES_W_FB, 0x100000u, 4, NES_FB_LATEST, NES_FRAME_SEQ, NES_FB_BASE, NES_FB_STRIDE },
    { NES_STATUS, NES_ST_HELD, 200 },
    nes_init, nes_release, 0, 5,
    "pad.poll", 16, false,
    "Choose a .nes file in Library (F3)", ".nes",
    nes_register,
    caps, 2,
    false,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     // id_shared ... own_volume
    &pad_spec,
    0, false,                                  // live_ms, menu_first
    0, 0,                                      // media_flush, media_pending
    &mount_slots,                              // T180 D2, D5: Cartridge
    &hk_console,                           // T181: the default hotkey map (hk_defaults.h)
};
