// The SNK Neo Geo (MVS / AES) descriptor (T143): the BIOS, the zoom table and the board fix ROM are the user's files in the core's roms folder (the ROM manager finds them), the
// game is a .neo file or a folder of a cartridge's files with an entry in the user's romsets.xml (neoset.cc), all of it put into the DDR2 where the core reads it. The machine's
// settings are bits of the register block; the backup RAM and the memory card are saved to <core folder>/saves/<game>.sav (T148 D5; <medium>/saves/neogeo/<game>.sav when the application started without a hand-over; 72 KB: the backup RAM, then the card, or the XRAM of
// the games that have one - the layout of the MiSTer core's save file). The register block is cores/neogeo/trx/neo_regs.v, the picture the core's own frame writer (320 x 224, four
// buffers) which the host's pixel layer shows. No ROM, no BIOS and no game is part of this application or of the repository.
#include "hk_defaults.h"
#include "ctx.h"
#include "memops.h"
#include "boot.h"
#include "romfind.h"
#include "corefolder.h"
#include "neogeo_regs.h"
#include "neoset.h"
#include "cores.h"
#include "engines.h"
#include "pad.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEO_ROM_ABS  (NEO_DDR_BASE + NEO_W_ROM)        // rom_base: the system ROM at +0, the board fix ROM at +0x20000
#define NEO_M1_ABS   (NEO_DDR_BASE + NEO_W_M1)
#define NEO_SAVE_BYTES (0x10000u + 0x2000u)            // backup RAM 64 KB, then 8 KB: the memory card (2 KB, padded) or the XRAM

struct NeoState {
    char name[FS_NAME_MAX + 1];         // the game now loaded (a .neo file: its whole name without the extension, the save is named from it)
    char spec[FS_PATH_MAX + 8];                     // T180 D2: how it was started, for the stored mounts: the .neo file's path, or "folder <folder>/<set name>"
    char save_path[FS_PATH_MAX];
    char old_save_path[FS_PATH_MAX];            // T148: the save of the older layout, read when the new place has none, never written
    bool have_bios[3];                  // the BIOS files found: 0 MVS (sp-s2.sp1), 1 AES (neo-epo.sp1), 2 Universe BIOS
    bool lo_pending;                    // the zoom table is in the DDR2 and has to be moved into the core's block RAM before the release
    bool save_failed;                // T180 D4: the last save did not reach the file: it is tried again (service, flush)
bool images_pending;                // the memory card / XRAM images of the save are in the DDR2 and have to be moved in
    bool xram;                          // the game has XRAM (its save holds that instead of a card)
    bool dirty_seen;
    uint32_t dirty_ms;
    uint32_t rtc_ms;
    uint32_t log_ms;
    uint32_t last_late;
    bool loaded;
};

static NeoState *neo_state(CoreCtx *c)
{
    if (!c->state) {
        c->state = new NeoState();
        memset(c->state, 0, sizeof(NeoState));
    }
    return (NeoState *)c->state;
}

// ---- settings: bits of the register block (the byte is the OR of what its settings contribute) ---------------------------
static const char *const sys_names[]   = { "Console (AES)", "Arcade (MVS)" };
static const char *const bios_names[]  = { "Universe BIOS", "Original BIOS" };
static const char *const video_names[] = { "NTSC", "PAL" };
static const char *const card_names[]  = { "Plugged", "Unplugged" };
static const char *const dip_names[]   = { "Off", "On" };
static const char *const pad_names[]   = { "Normal", "Swapped" };
static const RegBind b_sys   = { NEO_SYSTEM, 0, 1, 0 };
static const RegBind b_video = { NEO_SYSTEM, 1, 1, 0 };
static const RegBind b_card  = { NEO_SYSTEM, 2, 1, 0 };
static const RegBind b_dip0  = { NEO_DIP, 0, 1, 0 };
static const RegBind b_dip1  = { NEO_DIP, 1, 1, 0 };
static const RegBind b_dip2  = { NEO_DIP, 2, 1, 0 };
static const RegBind b_pad   = { NEO_PAD, 0, 1, 0 };
static const char *const padmap_names[] = { "Default", "Custom" };

#define KEY_SYSTEM "System type"
static const Setting settings[] = {
    { KEY_SYSTEM,            sys_names,   2, 0, WHEN_RESET, 0, &b_sys },
    { "BIOS",                bios_names,  2, 0, WHEN_RESET, "neogeo.set_bios" },
    { "Video mode",          video_names, 2, 0, WHEN_RESET, 0, &b_video },
    { "Memory card",         card_names,  2, 0, WHEN_RESET, 0, &b_card },
    { "[DIP] Settings menu", dip_names,   2, 0, WHEN_LIVE,  0, &b_dip0 },
    { "[DIP] Free play",     dip_names,   2, 0, WHEN_LIVE,  0, &b_dip1 },
    { "[DIP] Freeze",        dip_names,   2, 0, WHEN_LIVE,  0, &b_dip2 },
    { "Joystick ports",      pad_names,   2, 0, WHEN_LIVE,  0, &b_pad },
    { "Pad map",             padmap_names, 2, 0, WHEN_LIVE, "pad.set_map" },
};

// T156: the USB pads. The register is the machine's joystick word (neogeo.sv, neo_pad.v). The default map is the positional one of the MiSTer core (neogeo.sv "jp":
// A = the pad's bottom button, B = right, C = left, D = top, as on the Neo Geo CD pad; Coin = L), D3 with the SNES layout printed on the pad.
static const char *const padbtn_names[11] = { "Right", "Left", "Down", "Up", "A", "B", "C", "D", "Start", "Select", "Coin" };
static const uint8_t padbtn_def[11] = { PAD_SRC_RIGHT, PAD_SRC_LEFT, PAD_SRC_DOWN, PAD_SRC_UP, PAD_D3_BOTTOM, PAD_D3_RIGHT, PAD_D3_LEFT, PAD_D3_TOP,
                                        PAD_D3_START, PAD_D3_SELECT, PAD_D3_L };
static const PadSpec pad_spec = { { NEO_PAD1, NEO_PAD2 }, 11, padbtn_names, padbtn_def };

static bool setting_value(CoreCtx *c, const char *key, int *v)
{
    int i = c->settings ? c->settings->find(key) : -1;
    if (i < 0) {
        return false;
    }
    *v = c->settings->value(i);
    return true;
}

// ---- the BIOS side -------------------------------------------------------------------------------------------------------------
static int stage_bios(CoreCtx *c, int slot, const OpArgs &a, OpOut &o)
{
    if (a.len(0) != 0x20000) {
        snprintf(o.msg, o.msglen, "A Neo Geo BIOS is 131072 bytes (this one is %u).", (unsigned)a.len(0));
        return OPE_FAILED;
    }
    c->io->ddr_write(NEO_W_BIOS + (uint32_t)slot * 0x20000u, a.p(0), 0x20000);
    neo_state(c)->have_bios[slot] = true;
    return OPE_OK;
}
static int op_put_bios_mvs(CoreCtx *c, const OpArgs &a, OpOut &o) { return stage_bios(c, 0, a, o); }
static int op_put_bios_aes(CoreCtx *c, const OpArgs &a, OpOut &o) { return stage_bios(c, 1, a, o); }
static int op_put_bios_uni(CoreCtx *c, const OpArgs &a, OpOut &o) { return stage_bios(c, 2, a, o); }
static const ArgSpec rom_arg[] = { ARG_BLOB("data") };

// The zoom table: the first 64 KB are the core's LO ROM; they go into the core's block RAM at the release.
static int op_put_lo(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    (void)o;
    c->io->ddr_write(NEO_W_IMG, a.p(0), 0x10000);
    neo_state(c)->lo_pending = true;
    return OPE_OK;
}

// The board fix ROM, rearranged as the S ROM is, at +0x20000 of the ROM space.
static int op_put_sfix(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    static uint8_t buf[0x20000];
    memcpy(buf, a.p(0), 0x20000);
    neo_fix_rearrange(buf, 0x20000);
    if (!neo_put(c, NEO_ROM_ABS + 0x20000, buf, 0x20000, 0)) {
        snprintf(o.msg, o.msglen, "Neo Geo fix ROM: the DDR2 did not take it");
        return OPE_FAILED;
    }
    return OPE_OK;
}

// The system ROM the machine runs: the Universe BIOS or the original, as the setting says; the original is the MVS one (sp-s2.sp1) for the arcade and the AES one (neo-epo.sp1)
// for the console. A BIOS that is not there is passed over for the other. Copied from its place in the staging area into the ROM space (128 KB).
// The folder the user is told to put files in (T148 D4): the core's own roms folder, "roms/neogeo" when the application started without a hand-over.
static const char *rom_dir_text(CoreCtx *c, char *buf, int buflen)
{
    if (!rom_own_dir(c->cor_path, buf, buflen)) {
        snprintf(buf, buflen, "roms/neogeo");
    }
    return buf;
}

static int select_bios(CoreCtx *c, char *msg, int msglen)
{
    NeoState *st = neo_state(c);
    int mvs = 0, pref = 0;
    setting_value(c, KEY_SYSTEM, &mvs);
    setting_value(c, "BIOS", &pref);
    int orig = mvs ? 0 : 1;
    int order[2] = { pref == 0 ? 2 : orig, pref == 0 ? orig : 2 };
    int slot = -1;
    for (int i = 0; i < 2 && slot < 0; i++) {
        if (st->have_bios[order[i]]) {
            slot = order[i];
        }
    }
    if (slot < 0) {
        char dirbuf[96];
        snprintf(msg, msglen, "No Neo Geo BIOS: put %s, %s or uni-bios.rom in %s", "sp-s2.sp1", "neo-epo.sp1", rom_dir_text(c, dirbuf, sizeof(dirbuf)));
        return 1;
    }
    static uint8_t buf[16384];
    for (uint32_t o = 0; o < 0x20000; o += sizeof(buf)) {
        c->io->ddr_read(NEO_W_BIOS + (uint32_t)slot * 0x20000u + o, buf, sizeof(buf));
        c->io->ddr_write(NEO_W_ROM + o, buf, sizeof(buf));
    }
    printf("[neogeo] BIOS: %s\n", slot == 2 ? "Universe BIOS" : slot == 0 ? "sp-s2.sp1 (MVS)" : "neo-epo.sp1 (AES)");
    return 0;
}

static const ArgSpec bios_arg[] = { ARG_INT("value", 0, 1) };
static int op_set_bios(CoreCtx *c, const OpArgs &, OpOut &o)
{
    return select_bios(c, o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

// ---- the image mover and the saves -----------------------------------------------------------------------------------------
static bool mover(CoreCtx *c, uint8_t cmd)
{
    uint8_t before = c->io->rd(NEO_MV_DONE);
    c->io->wr(NEO_MV_CMD, cmd);
    c->io->wr(NEO_MV_GO, 1);
    uint32_t t0 = c->io->now_ms();
    while (c->io->rd(NEO_MV_DONE) == before) {
        if (c->io->now_ms() - t0 > 500) {
            return false;
        }
        c->io->delay_ms(1);
    }
    return true;
}

static void save_path_of(CoreCtx *c, const char *game_path, const char *name, char *out, int outlen)
{
    // T148 D5: <core folder>/saves/<game>.sav; the old place on the medium of the game is remembered for one read
    char file[FS_NAME_MAX + 8];
    size_t sl = strlen(name) > FS_NAME_MAX - 4 ? FS_NAME_MAX - 4 : strlen(name);      // "<name>.sav" is a FAT name too
    memcpy(file, name, sl);
    memcpy(file + sl, ".sav", 5);
    NeoState *st = neo_state(c);
    st->old_save_path[0] = 0;
    bool own = corefolder_save_path(c->cor_path, file, out, outlen);
    char root[64];
    const char *p = game_path[0] == '/' ? strchr(game_path + 1, '/') : 0;
    int n = p ? (int)(p - game_path) : 0;
    char *dst = own ? st->old_save_path : out;
    int dstlen = own ? (int)sizeof(st->old_save_path) : outlen;
    if (n <= 0 || n >= (int)sizeof(root)) {
        if (!own) {
            out[0] = 0;
        }
        return;
    }
    memcpy(root, game_path, (size_t)n);
    root[n] = 0;
    snprintf(dst, dstlen, "%s/saves/neogeo/%s", root, file);
}

static bool save_dirs(CoreCtx *c, const char *path)
{
    char d[FS_PATH_MAX];
    snprintf(d, sizeof(d), "%s", path);
    char *s = strrchr(d, '/');
    if (!s) {
        return false;
    }
    *s = 0;
    char *s2 = strrchr(d, '/');
    if (!s2) {
        return false;
    }
    *s2 = 0;
    c->fs->mkdir(d);                                // <medium>/saves
    *s2 = '/';
    c->fs->mkdir(d);                                // <medium>/saves/neogeo
    return true;
}

// The save of the game into the DDR2 images, before the machine starts: a missing file leaves the images blank.
static void load_save(CoreCtx *c)
{
    NeoState *st = neo_state(c);
    static uint8_t buf[NEO_SAVE_BYTES];
    memset(buf, 0, sizeof(buf));
    if (st->save_path[0]) {
        int n = c->fs->load(st->save_path, buf, sizeof(buf));
        if (n < 0 && st->old_save_path[0]) {
            n = c->fs->load(st->old_save_path, buf, sizeof(buf));        // T148: a save of the older layout, taken over once (written to the new place at the next save)
            if (n >= 0) {
                printf("[neogeo] save %s: %d bytes (older place)\n", st->old_save_path, n);
                n = -1;
            }
        }
        if (n >= 0) {
            printf("[neogeo] save %s: %d bytes\n", st->save_path, n);
        }
    }
    c->io->ddr_write(NEO_W_BACKUP, buf, 0x10000);
    c->io->ddr_write(st->xram ? NEO_W_XRAM : NEO_W_MEMCARD, buf + 0x10000, st->xram ? 0x2000 : 0x800);
    st->images_pending = true;
}

// Backup RAM is in the DDR2 as the machine writes it (write through); the card and the XRAM are in block RAM and are moved out first.
static bool save_now(CoreCtx *c, char *msg, int msglen)
{
    NeoState *st = neo_state(c);
    if (!st->loaded || !st->save_path[0]) {
        return true;
    }
    c->io->wr(NEO_SAVE_ACK, 1);                      // taken: a later write sets the flag again
    st->save_failed = true;                          // until the file has it
    if (!mover(c, (uint8_t)(NEO_MV_SAVE | (st->xram ? NEO_MV_XRAM : NEO_MV_MEMCARD)))) {
        snprintf(msg, msglen, "Neo Geo: the card image did not come out of the core");
        return false;
    }
    static uint8_t buf[NEO_SAVE_BYTES];
    memset(buf, 0, sizeof(buf));
    c->io->ddr_read(NEO_W_BACKUP, buf, 0x10000);
    c->io->ddr_read(st->xram ? NEO_W_XRAM : NEO_W_MEMCARD, buf + 0x10000, st->xram ? 0x2000 : 0x800);
    if (!corefolder_ensure(c->fs, c->cor_path)) {
        save_dirs(c, st->save_path);                 // no hand-over (developer start): the old place on the medium of the game
    }
    if (!c->fs->save(st->save_path, buf, sizeof(buf))) {
        snprintf(msg, msglen, "Neo Geo: cannot write %s", st->save_path);
        return false;
    }
    st->save_failed = false;
    printf("[neogeo] saved %s\n", st->save_path);
    return true;
}

// T180 D4: the backup RAM and the card are in their file before the game is let go. Written only when the game wrote (or an earlier write failed): a save
// that did not change is no reason to stop (a read only card would block every reset).
static bool save_needed(CoreCtx *c)
{
    NeoState *st = neo_state(c);
    return st->loaded && st->save_path[0] && (st->save_failed || st->dirty_seen || (c->io->rd(NEO_STATUS) & NEO_ST_DIRTY));
}

static int neo_media_flush(CoreCtx *c, char *msg, int msglen)
{
    if (!save_needed(c)) {
        return 0;
    }
    MediaSaving saving(c, true);
    if (!save_now(c, msg, msglen)) {
        size_t n = strlen(msg);
        snprintf(msg + n, (size_t)msglen - n, ". The game stays loaded.");
        return 1;
    }
    neo_state(c)->dirty_seen = false;
    return 0;
}

static bool neo_media_pending(CoreCtx *c)
{
    return save_needed(c);
}

static int op_save_now(CoreCtx *c, const OpArgs &, OpOut &o)
{
    return save_now(c, o.msg, o.msglen) ? OPE_OK : OPE_FAILED;
}

// ---- control -------------------------------------------------------------------------------------------------------------------
static int neo_init(CoreCtx *c, char *, int)
{
    c->io->wr(NEO_CTRL, NEO_HOLD);
    c->io->mode_reset(true);
    CoreIo *io = c->io;
    neo_wr32(io, NEO_ROM_BASE, NEO_ROM_ABS);
    neo_wr32(io, NEO_M1_BASE, NEO_M1_ABS);
    neo_wr32(io, NEO_IMG_BASE, NEO_DDR_BASE + NEO_W_IMG);
    neo_wr32(io, NEO_V1_BASE, NEO_ROM_ABS + 0x400000u);
    neo_wr32(io, NEO_V2_BASE, NEO_ROM_ABS + 0x1400000u);
    io->wr(NEO_C_START, 3);
    return 0;
}

// The last things before the machine runs: the system ROM it will find, the zoom table and the card images in their block RAMs.
static int neo_release(CoreCtx *c, char *msg, int msglen)
{
    NeoState *st = neo_state(c);
    if (select_bios(c, msg, msglen)) {
        return 1;
    }
    if (st->lo_pending && !mover(c, NEO_MV_LO)) {
        snprintf(msg, msglen, "Neo Geo: the zoom table did not reach the core");
        return 1;
    }
    st->lo_pending = false;
    if (st->images_pending) {
        if (!mover(c, st->xram ? NEO_MV_XRAM : NEO_MV_MEMCARD)) {
            snprintf(msg, msglen, "Neo Geo: the memory card image did not reach the core");
            return 1;
        }
        st->images_pending = false;
    }
    c->io->wr(NEO_SAVE_ACK, 1);                      // the loads are no change of the save
    c->io->wr(NEO_CTRL, 0);
    c->io->mode_reset(false);
    return 0;
}

// ---- games ----------------------------------------------------------------------------------------------------------------------
static void xml_path_of(CoreCtx *c, const char *game_path, char *out, int outlen)
{
    // T148 D2: romsets.xml lies in the core's own roms folder. A file of the older places (roms/neogeo/ on the medium the game is on, else on the first root that has one)
    // is copied there once, verified, and the copy is used; without a hand-over (developer start) the older place is used where it lies.
    char own[96], src[128], root[64];
    uint64_t sz;
    bool dir;
    bool have_own = rom_own_dir(c->cor_path, own, sizeof(own));
    if (have_own) {
        snprintf(out, outlen, "%s/romsets.xml", own);
        if (c->fs->stat(out, &sz, &dir) && !dir) {
            return;
        }
    }
    src[0] = 0;
    const char *p = game_path[0] == '/' ? strchr(game_path + 1, '/') : 0;
    int n = p ? (int)(p - game_path) : 0;
    if (n > 0 && n < (int)sizeof(root)) {
        memcpy(root, game_path, (size_t)n);
        root[n] = 0;
        snprintf(src, sizeof(src), "%s/roms/neogeo/romsets.xml", root);
        if (!(c->fs->stat(src, &sz, &dir) && !dir)) {
            src[0] = 0;
        }
    }
    for (int r = 0; !src[0] && rom_roots[r]; r++) {
        snprintf(src, sizeof(src), "%s/roms/neogeo/romsets.xml", rom_roots[r]);
        if (!(c->fs->stat(src, &sz, &dir) && !dir)) {
            src[0] = 0;
        }
    }
    if (!src[0]) {
        out[0] = 0;
        return;
    }
    if (have_own) {
        uint32_t max;
        uint8_t *scratch = rom_scratch(&max);
        corefolder_ensure(c->fs, c->cor_path);
        if (corefolder_copy_verified(c->fs, src, out, scratch, max)) {
            return;
        }
    }
    snprintf(out, outlen, "%s", src);
}

static const char *base_name(const char *path, char *buf, int buflen, bool strip_ext)
{
    const char *s = strrchr(path, '/');
    s = s ? s + 1 : path;
    snprintf(buf, buflen, "%s", s);
    if (strip_ext) {
        char *dot = strrchr(buf, '.');
        if (dot) {
            *dot = 0;
        }
    }
    return buf;
}

// A set's parts hold whole file paths (T180 #38: 29 KB): taken from the heap for the one start, zeroed as a static would have been, given back with the scope.
struct NeoPartsHeap {
    NeoParts *p;
    NeoPartsHeap() : p((NeoParts *)calloc(1, sizeof(NeoParts))) { }
    ~NeoPartsHeap() { free(p); }
};

static int start_game(CoreCtx *c, NeoParts &parts, const char *game_path, const char *name, const char *spec, char *msg, int msglen)
{
    NeoState *st = neo_state(c);
    NeoPlan plan;
    if (neo_plan(parts, NEO_ROM_ABS, NEO_M1_ABS, &plan, msg, msglen)) {
        return 1;
    }
    if (neo_media_flush(c, msg, msglen)) {            // T180 D1, D4: the game that is leaving is in its file first; a save that fails keeps it loaded
        return 1;
    }
    neo_init(c, msg, msglen);                        // hold: from here on the old game is gone
    c->program_loaded = false;
    st->loaded = false;
    st->spec[0] = 0;
    c->io->delay_ms(2);
    if (neo_load(c, parts, plan, msg, msglen)) {
        mount_notify(c, 0);                          // the old game is gone and the slot is empty (T180 D2)
        return 1;
    }
    snprintf(st->name, sizeof(st->name), "%s", name);
    st->xram = parts.info.xram != 0;
    save_path_of(c, game_path, name, st->save_path, sizeof(st->save_path));
    st->loaded = true;
    load_save(c);
    c->program_loaded = true;
    snprintf(st->spec, sizeof(st->spec), "%s", spec);                // T180 D2: stored by the settings pass of boot_finish below (the Persist item's apply)
    printf("[neogeo] %s: ROM space %u MB, C ROM from %u MB, set ends %u MB, cfg %08x (%s)\n", name, (unsigned)((plan.end - NEO_ROM_ABS) >> 20), (unsigned)plan.c_start,
           (unsigned)((plan.end - NEO_ROM_ABS) >> 20), (unsigned)plan.cfg, parts.info.found ? "romsets.xml entry" : "no entry in romsets.xml: no cartridge chip");
    return boot_finish(c, msg, msglen);
}

// A .neo file: the chips from the entry of the same name in romsets.xml (without one: none, and the message says so).
static int run_file(CoreCtx *c, const char *path, char *msg, int msglen)
{
    MediaLoading loading(c);                               // T226 S1
    NeoPartsHeap held;
    if (!held.p) {
        snprintf(msg, msglen, "No memory to read the game's files");
        return 1;
    }
    NeoParts &parts = *held.p;
    char name[FS_NAME_MAX + 1], xml[200];
    base_name(path, name, sizeof(name), true);
    NeoSetInfo info;
    xml_path_of(c, path, xml, sizeof(xml));
    if (!xml[0] || !neo_xml_find(c->fs, xml, name, &info)) {
        memset(&info, 0, sizeof(info));
        snprintf(info.name, sizeof(info.name), "%s", name);
        info.rom_wait = -1;
        info.p_wait = -1;
    }
    if (neo_parts_neo(c->fs, path, info, &parts, msg, msglen)) {
        return 1;
    }
    int rc = start_game(c, parts, path, name, path, msg, msglen);
    if (!rc && !info.found) {
        snprintf(msg, msglen, "%s: no entry in romsets.xml, loaded without cartridge chips (some games need them)", name);
    }
    return rc;
}
static int op_run(CoreCtx *c, const OpArgs &a, OpOut &o) { return run_file(c, a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static const ArgSpec run_args[] = { ARG_PATH("path") };

// A folder of a cartridge's files: path = "<folder>/<set name>" (the menu appends what the person typed to the folder it chose); an empty name is the folder's own.
static int run_dir(CoreCtx *c, const char *arg, char *msg, int msglen)
{
    MediaLoading loading(c);                               // T226 S1
    NeoPartsHeap held;
    if (!held.p) {
        snprintf(msg, msglen, "No memory to read the game's files");
        return 1;
    }
    NeoParts &parts = *held.p;
    char dir[200], name[NEO_NAME], xml[200];
    snprintf(dir, sizeof(dir), "%s", arg);
    char *slash = strrchr(dir, '/');
    if (!slash) {
        snprintf(msg, msglen, "Choose a folder");
        return 1;
    }
    snprintf(name, sizeof(name), "%s", slash + 1);
    *slash = 0;
    if (!name[0]) {
        base_name(dir, name, sizeof(name), false);
    }
    NeoSetInfo info;
    xml_path_of(c, dir, xml, sizeof(xml));
    if (!xml[0]) {
        char dirbuf[96];
        snprintf(msg, msglen, "romsets.xml is missing in %s (it tells which files a game is made of)", rom_dir_text(c, dirbuf, sizeof(dirbuf)));
        return 1;
    }
    if (!neo_xml_find(c->fs, xml, name, &info)) {
        snprintf(msg, msglen, "cannot read %s", xml);
        return 1;
    }
    if (neo_parts_xml(c->fs, dir, info, &parts, msg, msglen)) {
        return 1;
    }
    char spec[FS_PATH_MAX + 8];
    snprintf(spec, sizeof(spec), "folder %s", arg);
    return start_game(c, parts, dir, name, spec, msg, msglen);
}
static int op_run_dir(CoreCtx *c, const OpArgs &a, OpOut &o) { return run_dir(c, a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static const ArgSpec run_dir_args[] = { ARG_STRING("path", 200) };

// the prefill of the name prompt: the folder's own name
static const char *offer_name(CoreCtx *c)
{
    static char text[NEO_NAME];
    text[0] = 0;
    if (c->media_dir[0]) {
        base_name(c->media_dir, text, sizeof(text), false);
    }
    return text;
}

// ---- the clock, the saves, the counters ----------------------------------------------------------------------------------------
static uint8_t bcd(unsigned v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

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
    uint8_t regs[7] = { bcd(t[5] % 60), bcd(t[4] % 60), bcd(t[3] % 24), (uint8_t)(dow + 1), bcd(t[2]), bcd(m), bcd(t[0]) };
    for (int i = 0; i < 7; i++) {
        c->io->wr(NEO_RTC + (uint32_t)i, regs[i]);
    }
}

// Once a second the clock; a changed save is written two seconds after the change (a game writes it in bursts); every few seconds one line of the core's counters when they moved.
static int op_service(CoreCtx *c, const OpArgs &, OpOut &o)
{
    pad_pace(c);                                              // the USB pads (T156): every period (16 ms); the rest below keeps its own timers
    NeoState *st = neo_state(c);
    uint32_t now = c->io->now_ms();
    if (!st->rtc_ms || now - st->rtc_ms >= 1000) {
        write_rtc(c);
        st->rtc_ms = now ? now : 1;
    }
    if (!c->started || !st->loaded) {
        return OPE_OK;
    }
    uint8_t status = c->io->rd(NEO_STATUS);
    if ((status & NEO_ST_DIRTY) || st->save_failed) {
        if (!st->dirty_seen) {
            st->dirty_seen = true;
            st->dirty_ms = now;
        } else if (now - st->dirty_ms >= 2000) {
            st->dirty_seen = false;
            bool told = st->save_failed;                 // an earlier try failed too: said already
            if (!save_now(c, o.msg, o.msglen) && !told) {
                snprintf(c->notice, sizeof(c->notice), "%s", o.msg);
                c->notice_pending = true;
            }
        }
    } else {
        st->dirty_seen = false;
    }
    if (!st->log_ms || now - st->log_ms >= 5000) {
        st->log_ms = now ? now : 1;
        uint32_t late = neo_rd32(c->io, NEO_C_LATE) + neo_rd32(c->io, NEO_S_LATE);
        if (late != st->last_late) {
            st->last_late = late;
            printf("[neogeo] held %u clocks (late words: %u sprite, %u fix); waits max %u / %u clocks, c_* read max %u ui clocks; 68k reads %u\n", (unsigned)neo_rd32(c->io, NEO_STALL_CLK),
                   (unsigned)neo_rd32(c->io, NEO_C_LATE), (unsigned)neo_rd32(c->io, NEO_S_LATE), (unsigned)c->io->rd(NEO_C_MAX), (unsigned)c->io->rd(NEO_S_MAX),
                   (unsigned)c->io->rd(NEO_C_LAT_MAX), (unsigned)neo_rd32(c->io, NEO_P_READS));
            printf("[neogeo] line caches (hit/in flight/miss): sprites %u/%u/%u (%u hints), fix %u/%u/%u (%u hints), 68k %u/%u/%u\n",
                   (unsigned)neo_rd32(c->io, NEO_PF), (unsigned)neo_rd32(c->io, NEO_PF + 4), (unsigned)neo_rd32(c->io, NEO_PF + 8), (unsigned)neo_rd32(c->io, NEO_PF + 12),
                   (unsigned)neo_rd32(c->io, NEO_PF + 16), (unsigned)neo_rd32(c->io, NEO_PF + 20), (unsigned)neo_rd32(c->io, NEO_PF + 24), (unsigned)neo_rd32(c->io, NEO_PF + 28),
                   (unsigned)neo_rd32(c->io, NEO_PF + 32), (unsigned)neo_rd32(c->io, NEO_PF + 36), (unsigned)neo_rd32(c->io, NEO_PF + 40));
        }
    }
    return OPE_OK;
}

// ---- menu ----------------------------------------------------------------------------------------------------------------------
static const OpBind run_bind[] = { B_FILE("path") };
static const OpBind dir_bind[] = { B_TEXT("path") };

static const Task tasks[] = {
    { "Reset",                                   "machine.cold_reset", 0, 0, true },
    { "Load a ROM set from a folder",            "neogeo.run_dir",     dir_bind, 1, true, "Set name (romsets.xml)", "neogeo.set.offer", true, "Use this folder" },
    { "Save the memory card and backup RAM now", "neogeo.save_now",    0, 0, false },
    { "Map buttons, pad 1",                      "menu.pad_map_1",     0, 0, false, 0, 0, false, 0, "input" },
    { "Map buttons, pad 2",                      "menu.pad_map_2",     0, 0, false, 0, 0, false, 0, "input" },
};
static const FileAction neo_actions[] = { { "Run", "neogeo.run", run_bind, 1, true } };
static const FileType filetypes[] = { { ".neo", neo_actions, 1 } };

static const char *cart_name(CoreCtx *c)
{
    NeoState *st = neo_state(c);
    return st->loaded ? st->name : 0;
}
static const MediaSlot media[] = { { "Cartridge", "neogeo.cartridge.image", 0, 0, 0, 0, ".neo", "Run", "Load a ROM set from a folder", "Load a ROM set from a folder,Save the memory card and backup RAM now" } };

// T180 D2, D5: the cartridge for the stored mounts and the media sets: the .neo file's path, or "folder <folder>/<set name>" for a ROM set. Emptying the slot
// writes the saves back, holds the machine and leaves it with no program (the start waits for one, as at a first start).
static const char *const slot_names[] = { "Cartridge" };
static const char *slot_spec(CoreCtx *c, int)
{
    NeoState *st = neo_state(c);
    return st->loaded && st->spec[0] ? st->spec : 0;
}
static int slot_mount(CoreCtx *c, int, const char *spec, char *msg, int msglen)
{
    return !strncmp(spec, "folder ", 7) ? run_dir(c, spec + 7, msg, msglen) : run_file(c, spec, msg, msglen);
}
static int slot_eject(CoreCtx *c, int, char *msg, int msglen)
{
    NeoState *st = neo_state(c);
    if (neo_media_flush(c, msg, msglen)) {
        return 1;
    }
    neo_init(c, msg, msglen);
    c->program_loaded = false;
    st->loaded = false;
    st->spec[0] = 0;
    mount_notify(c, 0);
    return 0;
}
static const MountSlots mount_slots = { 1, slot_names, slot_spec, slot_mount, slot_eject };

static const RomSpec roms[] = {
    { "MVS system ROM",       { "sp-s2.sp1", "sp-s1.sp1", 0 },                 { 0x20000, 0 }, false, 0, "neogeo.put_bios_mvs", 0x1 },
    { "AES system ROM",       { "neo-epo.sp1", "neo-po.sp1", 0 },              { 0x20000, 0 }, false, 0, "neogeo.put_bios_aes", 0x2 },
    { "Universe BIOS",        { "uni-bios.rom", 0 },                           { 0x20000, 0 }, false, 0, "neogeo.put_bios_uni", 0x4 },
    { "Zoom table (LO ROM)",  { "000-lo.lo", 0 },                              { 0x10000, 0x20000, 0 }, true, 0, "neogeo.put_lo" },
    { "Board fix ROM",        { "sfix.sfix", 0 },                              { 0x20000, 0 }, true, 0, "neogeo.put_sfix" },
};

static const MemMap neogeo_mem = { 0, 0, false, "The Neo Geo's RAMs are inside the core; the DDR2 holds the ROM space and the saves, not the machine's memory" };

static void neo_register(OpRegistry &r)
{
    r.contributor("neogeo");
    MEM_OPS(r, neogeo_mem, 0xFFFFFF);
    r.add_op("neogeo.set_bios", bios_arg, 1, op_set_bios);
    r.add_op("neogeo.put_bios_mvs", rom_arg, 1, op_put_bios_mvs);
    r.add_op("neogeo.put_bios_aes", rom_arg, 1, op_put_bios_aes);
    r.add_op("neogeo.put_bios_uni", rom_arg, 1, op_put_bios_uni);
    r.add_op("neogeo.put_lo", rom_arg, 1, op_put_lo);
    r.add_op("neogeo.put_sfix", rom_arg, 1, op_put_sfix);
    r.add_op("neogeo.run", run_args, 1, op_run);
    r.add_op("neogeo.run_dir", run_dir_args, 1, op_run_dir);
    r.add_op("neogeo.save_now", 0, 0, op_save_now);
    r.add_op("neogeo.service", 0, 0, op_service);
    r.add_state("neogeo.cartridge.image", cart_name);
    r.add_state("neogeo.set.offer", offer_name);
}

// A cartridge and a memory card.
static const char *const caps[] = { "cartridge", "pad" };

extern const CoreDesc neogeo_desc;
const CoreDesc neogeo_desc = {
    { 'N', 'E', 'O', '_' },
    "SNK Neo Geo",
    "SNK Neo Geo",
    "NEO GEO",
    "neogeo",
    "Neo Geo Settings",
    1,
    roms, 5,
    settings, 9,
    tasks, 5,
    filetypes, 1,
    media, 1,
    // 320 x 224 at scale 3 is 960 x 672, centred on 1920 x 1080
    { 320, 224, 3, 480, 204, NEO_DDR_BASE + NEO_W_FB, NEO_FB_STRIDE_B, 4, NEO_FB_LATEST, NEO_FRAME_SEQ, NEO_FB_BASE, NEO_FB_STRIDE },
    { NEO_STATUS, NEO_ST_HELD, 200 },
    neo_init, neo_release, 0, 20,
    "neogeo.service", 16, false,
    "Choose a .neo file in Library (F3), or Load a ROM set from a folder", ".neo",
    neo_register,
    caps, 2,
    false,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     // id_shared ... own_volume
    &pad_spec,
    0, false,                                  // live_ms, menu_first
    neo_media_flush, neo_media_pending,        // T180 D4: the backup RAM's and the card's save
    &mount_slots,                              // T180 D2, D5: Cartridge
    &hk_console,                           // T181: the default hotkey map (hk_defaults.h)
    0, true, 0,                            // alt_video, no_tone (T189 D6: neogeo's flat build has no tone filter until it places), rom_key
    "System type,BIOS",                    // T212 D13: machine_info
};
