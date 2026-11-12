// The Super Nintendo descriptor (T142, T93, T104): no ROM file (the S-SMP's 64 byte boot ROM is embedded), one file type pair (.sfc and .smc, the
// cartridge image). The image goes into the DDR2 window (ROM at +0, the work RAM at +0x800000 cleared, the battery RAM at +0x820000 from the save file or
// 0xFF) and the header's decisions into the registers (mapper byte, the two masks, the region), all while the reset is held (snes_rom.cc makes them);
// the machine reads its cartridge and its RAM through caches in front of that window (cores/snes/trx/snes_mem.v). The battery RAM is written back to
// <core folder>/saves/<image name>.srm when it has changed. The register block is cores/snes/trx/snes_regs.v; the picture is the core's own frame writer
// into the DDR2 (512 x 448, four buffers) which the host's pixel layer shows. The controllers read the C64 joysticks and keys themselves (cores/snes/trx/snes_pad.v); the USB pads (T156) are a register pair per player that this file's service writes (pad.h, PadSpec below).
#include "hk_defaults.h"
#include "ctx.h"
#include "memops.h"
#include "boot.h"
#include "romfind.h"
#include "newfile.h"
#include "snes_regs.h"
#include "snes_rom.h"
#include "cores.h"
#include "engines.h"
#include "pad.h"
#include <stdio.h>
#include <string.h>

// ---- the machine's state in the application ---------------------------------------------------------------------------------
struct SnesState {
    char name[FS_NAME_MAX + 1];            // the image's whole file name: the save file is named from it
    char path[FS_PATH_MAX];                        // T180 D2: where it came from, for the stored mounts
    SnesInfo info;
    bool hdr_pal;                          // the header's region
    uint8_t video;                         // the "Video standard" setting: 0 auto, 1 NTSC, 2 PAL
    uint8_t blend_off;                     // the "Pseudo high resolution" setting
    uint8_t *shadow;                       // the battery RAM as the save file last had it
    uint32_t shadow_bytes;
    char save_path[FS_PATH_MAX];
    uint32_t last_check_ms;
    bool save_error_told;
    uint16_t last_hands;                   // SNES_HANDS at the last sound check
};

static SnesState *snes_state(CoreCtx *c)
{
    if (!c->state) {
        c->state = new SnesState();
        memset(c->state, 0, sizeof(SnesState));
    }
    return (SnesState *)c->state;
}

static void wr32(CoreCtx *c, uint32_t reg, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        c->io->wr(reg + i, (uint8_t)(v >> (8 * i)));
    }
}
static void wr24(CoreCtx *c, uint32_t reg, uint32_t v)
{
    for (int i = 0; i < 3; i++) {
        c->io->wr(reg + i, (uint8_t)(v >> (8 * i)));
    }
}

// ---- settings ---------------------------------------------------------------------------------------------------------------
// SYSTEM holds three settings' worth: the region, the pseudo high resolution blend, the S-DSP's frequency variant; it is written while the machine is held.
static void write_system(CoreCtx *c)
{
    SnesState *st = snes_state(c);
    bool pal = st->video == 2 || (st->video == 0 && c->program_loaded && st->hdr_pal);
    c->io->wr(SNES_SYSTEM, (uint8_t)((pal ? SNES_SYS_PAL : 0) | (st->blend_off ? SNES_SYS_NOBLEND : 0)));
}
static int op_video(CoreCtx *c, const OpArgs &a, OpOut &) { snes_state(c)->video = (uint8_t)a.i(0); write_system(c); return OPE_OK; }
static int op_blend(CoreCtx *c, const OpArgs &a, OpOut &) { snes_state(c)->blend_off = (uint8_t)a.i(0); write_system(c); return OPE_OK; }
static const char *const video_names[] = { "Auto (cartridge)", "NTSC", "PAL" };
static const char *const blend_names[] = { "Blend", "512 wide" };
static const ArgSpec video_arg[] = { ARG_INT("value", 0, 2) };
static const ArgSpec blend_arg[] = { ARG_INT("value", 0, 1) };
static const char *const padmap_names[] = { "Default", "Custom" };
static const Setting settings[3] = {
    { "Video standard",        video_names, 3, 0, WHEN_RESET, "snes.set_video" },
    { "Pseudo high resolution", blend_names, 2, 0, WHEN_RESET, "snes.set_blend" },
    { "Pad map",               padmap_names, 2, 0, WHEN_LIVE, "pad.set_map" },
};

// T156: the USB pads. The register's bit n is the controller's serial bit n (B Y Select Start Up Down Left Right A X L R); the default map is D3
// with the SNES layout printed on the pad.
static const char *const pad_names[12] = { "B", "Y", "Select", "Start", "Up", "Down", "Left", "Right", "A", "X", "L", "R" };
static const uint8_t pad_def[12] = { PAD_D3_BOTTOM, PAD_D3_LEFT, PAD_D3_SELECT, PAD_D3_START, PAD_SRC_UP, PAD_SRC_DOWN, PAD_SRC_LEFT, PAD_SRC_RIGHT,
                                     PAD_D3_RIGHT, PAD_D3_TOP, PAD_D3_L, PAD_D3_R };
static const PadSpec pad_spec = { { SNES_PAD1, SNES_PAD2 }, 12, pad_names, pad_def };

// ---- the S-SMP's boot ROM (T142 D9, Q3) ---------------------------------------------------------------------------------------
// PROVENANCE: the 64 bytes of constant IPLROM in rtl/SMP.vhd of MiSTer-devel/SNES_MiSTer commit bdcd33c5, line 76 (GPL-3.0; the file with these bytes cut
// out is cores/snes/rtl/SMP.vhd, tools/vendor_snes.sh), copied byte for byte. Alex allowed them to stay in, as MiSTer carries them (T142 Q3, 2026-10-29,
// CLAUDE.md: second licence exception). They sit in a small RAM of the S-SMP that nothing but this write changes, so they stay across resets; they are
// written at every start of the machine (snes_init).
static const uint8_t spc_ipl[SNES_IPL_BYTES] = {
    0xCD, 0xEF, 0xBD, 0xE8, 0x00, 0xC6, 0x1D, 0xD0, 0xFC, 0x8F, 0xAA, 0xF4, 0x8F, 0xBB, 0xF5, 0x78,
    0xCC, 0xF4, 0xD0, 0xFB, 0x2F, 0x19, 0xEB, 0xF4, 0xD0, 0xFC, 0x7E, 0xF4, 0xD0, 0x0B, 0xE4, 0xF5,
    0xCB, 0xF4, 0xD7, 0x00, 0xFC, 0xD0, 0xF3, 0xAB, 0x01, 0x10, 0xEF, 0x7E, 0xF4, 0x10, 0xEB, 0xBA,
    0xF6, 0xDA, 0x00, 0xBA, 0xF4, 0xC4, 0xF4, 0xDD, 0x5D, 0xD0, 0xDB, 0x1F, 0x00, 0x00, 0xC0, 0xFF,
};
static void write_ipl(CoreCtx *c)
{
    for (int i = 0; i < SNES_IPL_BYTES; i++) {
        c->io->wr(SNES_IPL + i, spc_ipl[i]);
        c->io->delay_us(2);                                   // each byte's strobe to the machine lasts 8 clocks
    }
}

// ---- the battery RAM and its save file ---------------------------------------------------------------------------------------
// <dir of the .cor>/<name of the .cor>/saves/<image name>.srm (T148: the core's own folder); without a .cor path (a developer's JTAG start) <root>/saves/snes/.
static bool save_dir(CoreCtx *c, char *dir, int len, char *parent, int plen)
{
    parent[0] = 0;
    const char *p = c->cor_path;
    size_t n = strlen(p);
    if (n > 4 && !strcasecmp(p + n - 4, ".cor") && p[0] == '/') {
        if ((size_t)len < n + 8 || (size_t)plen < n) {
            return false;
        }
        nf_copy(parent, plen, p, n - 4);                                // /sd/cores/snes (the board's printf has no "%.*s")
        snprintf(dir, len, "%s/saves", parent);
        return true;
    }
    snprintf(parent, plen, "%s/saves", rom_roots[5]);                   // /sd/saves
    snprintf(dir, len, "%s/snes", parent);
    return true;
}

static void make_save_path(CoreCtx *c, SnesState *st)
{
    char dir[200], parent[200];
    if (!save_dir(c, dir, sizeof(dir), parent, sizeof(parent))) {
        st->save_path[0] = 0;
        return;
    }
    char base[FS_NAME_MAX + 1];
    snprintf(base, sizeof(base), "%s", st->name);
    char *dot = strrchr(base, '.');
    if (dot) {
        *dot = 0;
    }
    if (strlen(base) > FS_NAME_MAX - 4) {                                // "<base>.srm" is a FAT name too
        base[FS_NAME_MAX - 4] = 0;
    }
    snprintf(st->save_path, sizeof(st->save_path), "%s/%s.srm", dir, base);
}

static bool read_bsram(CoreCtx *c, uint8_t *dst, uint32_t n)
{
    for (uint32_t o = 0; o < n; o += 32768) {
        uint32_t k = n - o < 32768 ? n - o : 32768;
        c->io->ddr_read(SNES_W_BSRAM + o, dst + o, k);
    }
    return true;
}

// The battery RAM in the DDR2 against the file as it last was: written when it differs. The core's write-through keeps the DDR2 current within
// microseconds of every write, so this reads what the game has written.
static int flush_save(CoreCtx *c, bool force, char *msg, int msglen)
{
    SnesState *st = snes_state(c);
    if (!c->program_loaded || !st->info.ram_bytes || !st->shadow || !st->save_path[0]) {
        return 0;
    }
    static uint8_t cur[SNES_BSRAM_MAX];
    uint32_t n = st->shadow_bytes;
    read_bsram(c, cur, n);
    if (!force && !memcmp(cur, st->shadow, n)) {
        return 0;
    }
    char dir[200], parent[200];
    if (save_dir(c, dir, sizeof(dir), parent, sizeof(parent))) {
        c->fs->mkdir(parent);
        c->fs->mkdir(dir);
    }
    if (!c->fs->save(st->save_path, cur, n)) {
        if (!st->save_error_told) {
            st->save_error_told = true;
            snprintf(c->notice, sizeof(c->notice), "The save %s could not be written (medium full or write protected). It is tried again.", st->save_path);
            c->notice_pending = true;
        }
        if (msg) {
            snprintf(msg, msglen, "cannot write %s", st->save_path);
        }
        return 1;
    }
    memcpy(st->shadow, cur, n);
    st->save_error_told = false;
    printf("[snes] saved %s (%u bytes)\n", st->save_path, (unsigned)n);
    return 0;
}

static int op_save(CoreCtx *c, const OpArgs &, OpOut &o) { return flush_save(c, true, o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }

// T180 D4: CoreDesc::media_flush / media_pending - the battery RAM is in its file before the game is let go (reset, reload, restart, return to host, power, another game).
static bool snes_media_pending(CoreCtx *c)
{
    SnesState *st = snes_state(c);
    if (!c->program_loaded || !st->info.ram_bytes || !st->shadow || !st->save_path[0]) {
        return false;
    }
    static uint8_t cur[SNES_BSRAM_MAX];
    read_bsram(c, cur, st->shadow_bytes);
    return memcmp(cur, st->shadow, st->shadow_bytes) != 0;
}

static int snes_media_flush(CoreCtx *c, char *msg, int msglen)
{
    MediaSaving saving(c, snes_media_pending(c));
    if (flush_save(c, false, msg, msglen)) {
        snprintf(msg, msglen, "The save %s could not be written. The game stays loaded.", snes_state(c)->save_path);
        return 1;
    }
    return 0;
}

// "Sound check" (T142 5, board run 1: no sound, cause not yet known): what the core made since the last check and whether the host took it. The register block
// measures at the hand-over (snes_audio.v), so a core that makes sound but a silent speaker is the path after the core (the host's I2S, the cable, the capture device).
static uint16_t rd16(CoreCtx *c, uint32_t reg) { return (uint16_t)(c->io->rd(reg) | c->io->rd(reg + 1) << 8); }
static int op_sound(CoreCtx *c, const OpArgs &, OpOut &o)
{
    SnesState *st = snes_state(c);
    uint16_t dsp = rd16(c, SNES_PEAK_DSP), out = rd16(c, SNES_PEAK_OUT), hands = rd16(c, SNES_HANDS);
    uint16_t taken = (uint16_t)(hands - st->last_hands);
    st->last_hands = hands;
    c->io->wr(SNES_PEAK_DSP, 0);                              // any value: the next check shows what happens from now on
    const char *verdict;
    if (taken == 0) {
        verdict = "the host takes no audio from the core";
    } else if (dsp == 0) {
        verdict = "the core made no sound";
    } else if (out == 0) {
        verdict = "the core's sound is lost before the host";
    } else {
        verdict = "the core's sound reaches the host; if it is silent, look behind it";
    }
    snprintf(o.msg, o.msglen, "Sound: core %u, to host %u of 32768, %u frames: %s", (unsigned)dsp, (unsigned)out, (unsigned)taken, verdict);
    return OPE_OK;
}

// The service: the pads every 16 ms, the battery RAM against its file every 5 seconds (SNEStang's 10 would lose more).
static int op_service(CoreCtx *c, const OpArgs &, OpOut &)
{
    pad_pace(c);                                              // the USB pads (T156): every period, the save below keeps its own five seconds
    SnesState *st = snes_state(c);
    uint32_t now = c->io->now_ms();
    if (c->program_loaded && st->shadow && now - st->last_check_ms >= 5000) {
        st->last_check_ms = now;
        char msg[100];
        flush_save(c, false, msg, sizeof(msg));
    }
    return OPE_OK;
}

// ---- control ------------------------------------------------------------------------------------------------------------------
static int snes_init(CoreCtx *c, char *msg, int msglen)
{
    flush_save(c, false, msg, msglen);                        // what the game wrote is in the file before the machine starts over
    c->io->wr(SNES_CTRL, SNES_CTRL_HOLD);
    c->io->mode_reset(true);
    write_ipl(c);                                             // the reset is held: the port takes the boot ROM
    wr32(c, SNES_CART_BASE, 0x01000000u);
    return 0;
}

static int snes_release(CoreCtx *c, char *, int)
{
    c->io->wr(SNES_CTRL, 0);
    c->io->mode_reset(false);
    return 0;
}

// The image's bytes into the window: the file from `off`, `n` bytes, to the window at `win`.
static bool copy_image(CoreCtx *c, CoreFile *f, uint64_t off, uint32_t n, uint32_t win)
{
    static uint8_t chunk[32768];
    for (uint32_t o = 0; o < n; o += sizeof(chunk)) {
        uint32_t k = n - o < sizeof(chunk) ? n - o : (uint32_t)sizeof(chunk);
        if (!f->read(off + o, chunk, k)) {
            return false;
        }
        c->io->ddr_write(win + o, chunk, k);
    }
    return true;
}

// bsnes' mirroring of a ROM whose size is not a power of two: the address inside the image that an address outside it shows (3 MiB: 3-4 MiB repeat 2-3 MiB).
static uint32_t mirror(uint32_t addr, uint32_t size)
{
    if (!size) {
        return 0;
    }
    uint32_t base = 0, mask = 1u << 23;
    while (addr >= size) {
        while (!(addr & mask)) {
            mask >>= 1;
        }
        addr -= mask;
        if (size > mask) {
            size -= mask;
            base += mask;
        }
        mask >>= 1;
    }
    return base + addr;
}

static void fill_ddr(CoreCtx *c, uint32_t win, uint8_t v, uint32_t n)
{
    static uint8_t chunk[32768];
    memset(chunk, v, sizeof(chunk));
    for (uint32_t o = 0; o < n; o += sizeof(chunk)) {
        c->io->ddr_write(win + o, chunk, n - o < sizeof(chunk) ? n - o : (uint32_t)sizeof(chunk));
    }
}

static uint8_t bcd(unsigned v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

// The S-RTC's time: seconds, minutes, hours, day, month, year, weekday in BCD, and the toggle that makes the chip take them.
static void write_rtc(CoreCtx *c)
{
    uint8_t t[6];                                  // year 0-99, month, day, hour, minute, second
    if (!c->io->wall_time(t)) {
        uint32_t s = c->io->now_ms() / 1000;
        t[0] = 26; t[1] = 1; t[2] = 1; t[3] = (uint8_t)((s / 3600) % 24); t[4] = (uint8_t)((s / 60) % 60); t[5] = (uint8_t)(s % 60);
    }
    static const uint8_t mt[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    unsigned y = 2000u + t[0], m = t[1] ? t[1] : 1;
    unsigned yy = m < 3 ? y - 1 : y;
    unsigned dow = (yy + yy / 4 - yy / 100 + yy / 400 + mt[m - 1] + t[2]) % 7;          // 0 = Sunday
    uint8_t regs[7] = { bcd(t[5] % 60), bcd(t[4] % 60), bcd(t[3] % 24), bcd(t[2]), bcd(m), bcd(t[0]), (uint8_t)dow };
    for (int i = 0; i < 7; i++) {
        c->io->wr(SNES_RTC + i, regs[i]);
    }
    c->io->wr(SNES_RTC_TGL, (uint8_t)(c->io->rd(SNES_RTC_TGL) ^ 1));
}

// The file hook of ".sfc" / ".smc": read the header, refuse what the core cannot run (the machine is not touched then), hold, registers, image, the work RAM
// and the battery RAM, release.
static int snes_run(CoreCtx *c, const char *path, char *msg, int msglen)
{
    SnesState *st = snes_state(c);
    CoreFile *f = c->fs->open(path, false);
    if (!f) {
        snprintf(msg, msglen, "cannot open %s", path);
        return 1;
    }
    uint64_t size = f->size();
    uint32_t copier = (size % 1024 == 512) ? 512 : 0;
    static const uint32_t places[3] = { SNES_HDR_LO, SNES_HDR_HI, SNES_HDR_EX };
    uint8_t hb[3][64];
    const uint8_t *hp[3] = { 0, 0, 0 };
    for (int i = 0; i < 3; i++) {
        if (size >= (uint64_t)copier + places[i] + 64 && f->read(copier + places[i], hb[i], 64)) {
            hp[i] = hb[i];
        }
    }
    SnesInfo info = snes_parse(hp, size);
    if (!info.ok) {
        c->fs->close(f);
        snprintf(msg, msglen, "%s", info.err);
        return 1;
    }
    if (snes_media_flush(c, msg, msglen)) {                 // T180 D1, D4: the old game's save is in its file before the new one replaces it
        c->fs->close(f);
        return 1;
    }
    snes_init(c, msg, msglen);                             // hold: from here on the old program is gone
    c->program_loaded = false;
    st->path[0] = 0;
    c->io->delay_ms(2);
    const char *slash = strrchr(path, '/');
    snprintf(st->name, sizeof(st->name), "%s", slash ? slash + 1 : path);
    st->info = info;
    st->hdr_pal = info.pal;
    make_save_path(c, st);
    c->io->wr(SNES_ROM_TYPE, info.rom_type);
    wr24(c, SNES_ROM_MASK, info.rom_mask);
    wr24(c, SNES_RAM_MASK, info.ram_mask);
    write_rtc(c);
    // the image, then the part of the 8 MiB the mask reaches that the image does not fill (mirrored as the cartridge's decoding does)
    uint32_t rom = info.rom_bytes;
    bool ok = copy_image(c, f, info.copier, rom, SNES_W_ROM);
    if (ok && rom < info.rom_mask + 1) {
        uint32_t whole = rom & ~0xFFFu;
        for (uint32_t a = whole; ok && a < info.rom_mask + 1; a += 4096) {
            uint32_t s = mirror(a, rom);
            ok = copy_image(c, f, (uint64_t)info.copier + (s & ~0xFFFu), 4096, SNES_W_ROM + a);
        }
    }
    c->fs->close(f);
    if (!ok) {
        snprintf(msg, msglen, "cannot read %s (the medium is gone?). The machine stays held.", path);
        mount_notify(c, 0);                                // the old game is gone and the slot is empty (T180 D2)
        return 1;
    }
    fill_ddr(c, SNES_W_WRAM, 0x00, SNES_WRAM_BYTES);
    // the battery RAM: the save file when it fits, else erased flash (0xFF), as SNES.sv thrashes it at the load
    delete[] st->shadow;
    st->shadow = 0;
    st->shadow_bytes = 0;
    if (info.ram_bytes) {
        st->shadow = new uint8_t[info.ram_bytes];
        st->shadow_bytes = info.ram_bytes;
        memset(st->shadow, 0xFF, info.ram_bytes);
        int n = st->save_path[0] ? c->fs->load(st->save_path, st->shadow, info.ram_bytes) : -1;
        if (n > 0 && (uint32_t)n <= info.ram_bytes) {
            // a save file shorter than the RAM: the rest stays erased
            printf("[snes] save %s (%d bytes)\n", st->save_path, n);
        } else if (n >= 0) {
            memset(st->shadow, 0xFF, info.ram_bytes);
        }
        for (uint32_t o = 0; o < info.ram_bytes; o += 32768) {
            uint32_t k = info.ram_bytes - o < 32768 ? info.ram_bytes - o : 32768;
            c->io->ddr_write(SNES_W_BSRAM + o, st->shadow + o, k);
        }
    }
    st->last_check_ms = c->io->now_ms();
    c->program_loaded = true;
    snprintf(st->path, sizeof(st->path), "%s", path);                // T180 D2: stored by the settings pass of boot_finish below (the Persist item's apply)
    printf("[snes] %s: \"%s\" %s, %u KiB ROM (mask %06X), %u KiB RAM, chipset %02X, %s\n", st->name, info.title,
           info.mapping == 0 ? "LoROM" : info.mapping == 1 ? "HiROM" : "ExHiROM", (unsigned)(info.rom_bytes >> 10), (unsigned)info.rom_mask,
           (unsigned)(info.ram_bytes >> 10), info.chipset, info.pal ? "PAL" : "NTSC");
    return boot_finish(c, msg, msglen);
}

static int op_run(CoreCtx *c, const OpArgs &a, OpOut &o) { return snes_run(c, a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static const ArgSpec run_args[] = { ARG_PATH("path") };

static const Task tasks[] = {
    { "Reset",                     "machine.cold_reset", 0, 0, true },
    { "Save the battery RAM now",  "snes.save",          0, 0, false },
    { "Sound check",               "snes.sound",         0, 0, false },
    { "Map buttons, pad 1",        "menu.pad_map_1",     0, 0, false },
    { "Map buttons, pad 2",        "menu.pad_map_2",     0, 0, false },
};

static const OpBind run_bind[] = { B_FILE("path") };
static const FileAction snes_actions[] = { { "Run", "snes.run", run_bind, 1, true } };
static const FileType filetypes[] = { { ".sfc", snes_actions, 1 }, { ".smc", snes_actions, 1 } };

static const char *cart_name(CoreCtx *c)
{
    SnesState *st = snes_state(c);
    return c->program_loaded ? st->name : 0;
}
static const MediaSlot media[] = { { "Cartridge", "snes.cartridge.image", 0, 0, 0, 0, ".sfc,.smc", "Run", 0, "Save the battery RAM now" } };

// T180 D2, D5: the cartridge for the stored mounts and the media sets: the game's path. Emptying the slot writes the save back, holds the machine and leaves it
// with no program (the start waits for one, as at a first start).
static const char *const slot_names[] = { "Cartridge" };
static const char *slot_spec(CoreCtx *c, int)
{
    SnesState *st = snes_state(c);
    return c->program_loaded && st->path[0] ? st->path : 0;
}
static int slot_mount(CoreCtx *c, int, const char *spec, char *msg, int msglen)
{
    return snes_run(c, spec, msg, msglen);
}
static int slot_eject(CoreCtx *c, int, char *msg, int msglen)
{
    SnesState *st = snes_state(c);
    if (snes_media_flush(c, msg, msglen)) {
        return 1;
    }
    snes_init(c, msg, msglen);
    c->program_loaded = false;
    st->path[0] = 0;
    mount_notify(c, 0);
    return 0;
}
static const MountSlots mount_slots = { 1, slot_names, slot_spec, slot_mount, slot_eject };

// The machine's memories the application can reach: the work RAM and the battery RAM are in the DDR2 window, current within microseconds of every write
// (the core writes through); the cartridge ROM is the image and the video and audio RAM are inside the core.
static const MemRegion snes_mem_regions[] = {
    { 0x700000, SNES_BSRAM_MAX, MK_DDR, SNES_W_BSRAM, 0, "battery RAM" },
    { 0x7E0000, SNES_WRAM_BYTES, MK_DDR, SNES_W_WRAM, 0, "WRAM" },
};
static const MemMap snes_mem = { snes_mem_regions, 2, false, 0 };

// The built-in contributor of this core (T104.1 D14).
static void snes_register(OpRegistry &r)
{
    r.contributor("snes");
    MEM_OPS(r, snes_mem, 0xFFFFFF);
    r.add_op("snes.set_video", video_arg, 1, op_video);
    r.add_op("snes.set_blend", blend_arg, 1, op_blend);
    r.add_op("snes.run", run_args, 1, op_run);
    r.add_op("snes.save", 0, 0, op_save);
    r.add_op("snes.sound", 0, 0, op_sound);
    r.add_op("snes.service", 0, 0, op_service);
    r.add_state("snes.cartridge.image", cart_name);
}

// A cartridge and nothing a library of another machine's media could serve.
static const char *const caps[] = { "cartridge", "pad" };

extern const CoreDesc snes_desc;
const CoreDesc snes_desc = {
    { 'S', 'N', 'E', 'S' },
    "Super Nintendo",
    "Super Nintendo Entertainment System",
    "SNES",
    "snes",
    "SNES Settings",
    1,
    0, 0,
    settings, 3,
    tasks, 5,
    filetypes, 2,
    media, 1,
    // 512 x 448 at scale 2 is 1024 x 896, centred on 1920 x 1080 (T142 D7)
    { 512, 448, 2, 448, 92, 0x01000000u + SNES_W_FB, SNES_FB_STRIDE_V, 4, SNES_FB_LATEST, SNES_FRAME_SEQ, SNES_FB_BASE, SNES_FB_STRIDE },
    { SNES_STATUS, SNES_ST_HELD, 200 },
    snes_init, snes_release, 0, 5,
    "snes.service", 16, false,
    "Choose a .sfc or .smc file in Library (F3)", ".sfc",
    snes_register,
    caps, 2,
    false,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     // id_shared ... own_volume
    &pad_spec,
    0, false,                                  // live_ms, menu_first
    snes_media_flush, snes_media_pending,      // T180 D4: the battery RAM's save
    &mount_slots,                              // T180 D2, D5: Cartridge
    &hk_console,                           // T181: the default hotkey map (hk_defaults.h)
};
