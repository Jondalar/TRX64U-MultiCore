// The operations and states of the TRX64U menu (menu.toml) that are not the C64U's hardware registers (u64cfg.cc is those): the look of the menu, the C64's cartridge
// port, REU and ROM settings, the machine's own actions. Host-owned code; the register sequences of the cartridge port are Gideon's (vendor/gideon/software/io/c64/c64.cc
// ConfigureU64SystemBus and set_emulation_flags, GPL-3.0), the fast reset patch is his (c64.cc fastresetPatch).
#include "trx64u_ops.h"
#include "ops_glue.h"
#include "ui.h"
#include "hw.h"
#include "c64_regs.h"
#include "c64_cart.h"
#include "sidfile.h"
#include "u64cfg.h"
#include "corefolder.h"
#include "boot.h"
#include "uci.h"
#include "u64cfg.h"
#include "u64.h"
#include "iomap.h"
#include "itu.h"
#include "corefolder.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include <stdlib.h>

// ---- a setting by its key, cheaply: the key literal's address is the cache's key (a Settings object lives for the whole run, but is made again after the merge) ----
int cfg_value(CoreCtx *c, const char *key, int def)
{
    static const Settings *seen;
    static struct { const char *key; int16_t ix; } cache[96];
    static int n;
    if (!c->settings) {
        return def;
    }
    if (seen != c->settings) {
        seen = c->settings;
        n = 0;
    }
    for (int i = 0; i < n; i++) {
        if (cache[i].key == key) {
            return cache[i].ix >= 0 ? c->settings->value(cache[i].ix) : def;
        }
    }
    int ix = c->settings->find(key);
    if (n < 96) {
        cache[n].key = key;
        cache[n].ix = (int16_t)ix;
        n++;
    }
    return ix >= 0 ? c->settings->value(ix) : def;
}

// The text of a text setting ("" when empty or unknown).
const char *cfg_text(CoreCtx *c, const char *key)
{
    int ix = c->settings ? c->settings->find(key) : -1;
    const char *t = ix >= 0 ? c->settings->text(ix) : "";
    while (*t == ' ') {
        t++;
    }
    return t;
}

// ---- the look of the menu ---------------------------------------------------------------------------------------------------------------------
#include "tempdisk.h"
#include "product.h"
static int fx_ui(CoreCtx *c, const OpArgs &, OpOut &)
{
    // FW userinterface.cc:244-248: a machine that has never been set up starts with WASD Cursors on the C64 Ultimate product (isCommodoreBoard); a value the person stored stays.
    int nv = c->settings ? c->settings->find("Navigation Style") : -1;
    if (nv >= 0 && c->settings->layer(nv) == Settings::L_DEFAULT && isCommodoreBoard()) {
        c->settings->override_value(nv, 1);
    }
    ui_set_look(cfg_value(c, "Overlay Transparency", 1), cfg_value(c, "Color Scheme", 1));
    ui_set_name_squeeze(cfg_value(c, "Filename overflow squeeze", 0));
    tempdisk_set(cfg_value(c, "Temp Auto Cleanup", 1) != 0, cfg_value(c, "Temp Subfolders", 1) != 0);
    return OPE_OK;
}

// ---- the C64's cartridge port, REU, the sampler and the command interface -----------------------------------------------------------------------
static const uint8_t bus_mode_values[] = { 0x00, 0x01, 0x02, 0x03, 0x05 };     // Compatibility has write also enabled (c64.cc bus_mode_values)
#define CMD_IF_SLOT_BASE_REG   (*(volatile uint8_t *)(CMD_IF_BASE + 0x0))
#define CMD_IF_SLOT_ENABLE_REG (*(volatile uint8_t *)(CMD_IF_BASE + 0x1))

// Gideon's fast reset: the KERNAL's RAM test at $FD6C (offset 0x1D6C of the image) is replaced by a short loop that tests one page and sets the top of memory
static const uint8_t fast_patch[22] = { 0xa2, 0x00, 0xa0, 0xa0, 0xad, 0x00, 0x80, 0x49, 0xff, 0x8d, 0x00, 0x80, 0xcd, 0x00, 0x80, 0xf0, 0x02, 0xa0, 0x80, 0x4c, 0x8c, 0xfd };
static const uint8_t fast_org[47] = { 0xe6, 0xc2, 0xb1, 0xc1, 0xaa, 0xa9, 0x55, 0x91, 0xc1, 0xd1, 0xc1, 0xd0, 0x0f, 0x2a, 0x91, 0xc1, 0xd1, 0xc1, 0xd0, 0x08, 0x8a, 0x91, 0xc1, 0xc8, 0xd0, 0xe8, 0xf0, 0xe4, 0x98, 0xaa, 0xa4, 0xc2, 0x18, 0x20, 0x2D, 0xfe, 0xa9, 0x08, 0x8d, 0x82, 0x02, 0xa9, 0x04, 0x8d, 0x88, 0x02, 0x60 };
static uint8_t kernal_copy[C64_KERNAL_BYTES];
static bool kernal_known, kernal_patched;

// c64.cc calls this with every KERNAL image it puts into the aperture: the copy is what the patch is made against (the aperture is write only)
void trx64u_kernal_put(CoreCtx *c, const uint8_t *data, uint32_t n)
{
    if (n != C64_KERNAL_BYTES) {
        return;
    }
    memcpy(kernal_copy, data, n);
    kernal_known = true;
    kernal_patched = false;
    if (cfg_value(c, "Fast Reset", 0) && !memcmp(kernal_copy + 0x1d6c, fast_org, sizeof(fast_org))) {
        c->io->rom_write(C64_ROM_KERNAL + 0x1d6c, fast_patch, sizeof(fast_patch));
        kernal_patched = true;
    }
}

static void fast_reset(CoreCtx *c)
{
    if (!kernal_known) {
        return;
    }
    bool want = cfg_value(c, "Fast Reset", 0) != 0 && !memcmp(kernal_copy + 0x1d6c, fast_org, sizeof(fast_org));
    if (want && !kernal_patched) {
        c->io->rom_write(C64_ROM_KERNAL + 0x1d6c, fast_patch, sizeof(fast_patch));
        kernal_patched = true;
    } else if (!want && kernal_patched) {
        c->io->rom_write(C64_ROM_KERNAL + 0x1d6c, kernal_copy + 0x1d6c, sizeof(fast_patch));
        kernal_patched = false;
    }
}

static int reu_load(CoreCtx *c, const char *path, char *msg, int msglen, bool at_zero = true);
static const UciHost *uci_host(void);
#define TRX_SLOT_CTRL 0x7000                                       // TRX_IO_BASE + 0x7000 = 0x101A7000

// c64.cc:421: the expansion port is taken when a .crt runs, a cartridge was seen in the port at the last bus setup (trx_ext_cart_seen: GAME or EXROM pulled low) or the slot is armed for one
// (TRX_SLOT_STATUS b16, valid with b31). The REU, the Ultimate Audio mapping and the UCI are off then (the saved values stay), and the rows say "Disabled (cartridge)".
static bool g_ext_cart_seen;
static bool port_taken(CoreCtx *c)
{
    bool armed = (c->io->rd(0x7007) & 0x80) && (c->io->rd(0x7006) & 0x01);   // SLOT_STATUS +004: b31 the block is there, b16 armed for a cartridge
    return c64_cart_active() || g_ext_cart_seen || armed;
}

static int fx_io(CoreCtx *c, const OpArgs &, OpOut &)
{
    CoreIo *io = c->io;
    g_ext_cart_seen = ((*(volatile uint8_t *)(U64_IO_BASE + 3) & 3) != 3);         // U64_CART_DETECT: GAME and EXROM both high = nothing in the port
    io->wr(TRX_SLOT_CTRL, 2);                                                        // T228 R20 29: CPU writes reach a real cartridge (TRX-Ultimate trx_cart.cc:364-406 keeps it at 2)
    io->bus_wr(0x2A, bus_mode_values[cfg_value(c, "Bus Operation Mode", 0)]);        // C64_BUS_BRIDGE
    bool cart = c64_cart_active();
    if (!cart) {
        uint8_t internal = 0, external = 0;
        bool ext_cart = g_ext_cart_seen;
        if (cfg_value(c, "GAME/EXROM", 0) == 1) {                                  // Off: the external cartridge is hidden completely
            internal = 15;
        } else {
            switch (cfg_value(c, "Cartridge Preference", 0)) {
            case 0: internal = ext_cart ? 0 : 15; external = ext_cart ? 15 : 0; break;
            case 1: internal = 15; break;
            case 2: external = 15; break;
            default: {
                static const char *const keys[4] = { "Bus Sharing - ROMs", "Bus Sharing - Interrupts", "Bus Sharing - I/O1", "Bus Sharing - I/O2" };
                static const uint8_t bits[4] = { 0x04, 0x08, 0x01, 0x02 };
                for (int k = 0; k < 4; k++) {
                    int v = cfg_value(c, keys[k], 2);
                    if (v == 0 || v == 2) {
                        internal |= bits[k];
                    }
                    if (v == 1 || v == 2) {
                        external |= bits[k];
                    }
                }
            }
            }
        }
        io->bus_wr(C64_BUSREG_INTERNAL, internal);
        io->bus_wr(C64_BUSREG_EXTERNAL, external);
        // one module in the expansion port: the REU, the Ultimate Audio and the UCI are the machine's only while no cartridge runs
        bool taken = port_taken(c);                                                  // a cartridge in the port (seen or armed): off, whatever the settings say
        io->cart_wr(C64_CARTREG_REU, !taken && cfg_value(c, "RAM Expansion Unit", 0) == 1 ? 1 : 0);
        io->cart_wr(0x09, (uint8_t)cfg_value(c, "REU Size", 4));
        io->cart_wr(C64_CARTREG_SAMPLER, taken ? 0 : (uint8_t)cfg_value(c, "Map Ultimate Audio $DF20-DFFF", 0));
        if (desc_has_capability(c->desc, "uci") && (!c->board || (c->board->probes & BC_UCI))) {
            uci_start(c, uci_host());                                               // the server and its targets (uci.cc), once
            CMD_IF_SLOT_ENABLE_REG = !taken && cfg_value(c, "Command Interface (UCI)", 0) ? 1 : 0;
            CMD_IF_SLOT_BASE_REG = 0x47;                                            // $DF1C
        }
    } else if (desc_has_capability(c->desc, "uci") && (!c->board || (c->board->probes & BC_UCI))) {
        CMD_IF_SLOT_ENABLE_REG = 0;                                                 // c64.cc:426-445: a running cartridge has the port, the UCI is off (the saved value stays)
    }
    fast_reset(c);
    static bool preloaded;                                               // "REU Preload": the image goes into the REU once, at the start
    if (!preloaded && !c->started && cfg_value(c, "REU Preload", 0) && cfg_value(c, "RAM Expansion Unit", 0) == 1) {
        preloaded = true;
        char m[100];
        const char *img = cfg_text(c, "REU Preload Image");
        if (img[0] && reu_load(c, img, m, sizeof(m), false)) {
            printf("[reu] preload: %s\n", m);
        } else if (img[0]) {
            printf("[reu] %s\n", m);
        }
    }
    return OPE_OK;
}

// ---- the REU: a file into its memory (Gideon's "Load into REU" and "REU Preload"; the memory is the DDR2 window from offset 0) -----------------------------------------
static uint32_t reu_offset(CoreCtx *c)
{
    int i = cfg_value(c, "REU Preload Offset", 0);
    return i == 0 ? 0 : (128u * 1024u) << (i - 1);
}

static int reu_load(CoreCtx *c, const char *path, char *msg, int msglen, bool at_zero)
{
    uint64_t sz = 0;
    bool dir = false;
    if (!c->fs->stat(path, &sz, &dir) || dir) {
        snprintf(msg, msglen, "No file %s", path);
        return 1;
    }
    if (c64_cart_active()) {
        snprintf(msg, msglen, "A cartridge uses the REU's memory: remove it first");
        return 1;
    }
    uint32_t off = at_zero ? 0 : reu_offset(c), room = 0x1000000u - off;     // Load into REU and Play MOD: REU_MEMORY_BASE (FW filetype_reu.cc:125, 135); only the start's preload takes "REU Preload Offset"
    CoreFile *f = c->fs->open(path, false);
    if (!f) {
        snprintf(msg, msglen, "Cannot open %s", path);
        return 1;
    }
    static uint8_t buf[0x10000];
    uint32_t n = sz > room ? room : (uint32_t)sz;
    bool ok = true;
    for (uint32_t done = 0; ok && done < n;) {
        uint32_t k = n - done < sizeof(buf) ? n - done : (uint32_t)sizeof(buf);
        ok = f->read(done, buf, k);
        c->io->ddr_write(off + done, buf, k);
        done += k;
    }
    c->fs->close(f);
    if (!ok) {
        snprintf(msg, msglen, "Cannot read %s (the medium is gone?)", path);
        return 1;
    }
    snprintf(msg, msglen, "%u KB loaded into the REU at %u KB%s", (unsigned)(n >> 10), (unsigned)(off >> 10), sz > room ? " (cut: the file is larger than the room)" : "");
    return 0;
}

static int op_reu_load(CoreCtx *c, const OpArgs &a, OpOut &o) { return reu_load(c, a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
// FW filetype_reu.cc:42-45, 103-105, 143-150 (REUFILE_PLAYMOD): the sampler's registers cleared, the module into the REU, the sampler's output selected
// (audio_select.h:41-55), the module player's cartridge started with the REU, the sampler and the UCI. Without the sampler there is nothing to play it with
// (route_runners.cc:244-248: "Sampler module not available").
static int op_reu_playmod(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    if (!(getFpgaCapabilities() & CAPAB_SAMPLER)) {
        snprintf(o.msg, o.msglen, "Sampler module not available");
        return OPE_FAILED;
    }
    if (c64_cart_active()) {
        snprintf(o.msg, o.msglen, "A cartridge uses the REU's memory: remove it first");
        return OPE_FAILED;
    }
    static uint8_t player[0x4000];                                               // FW's _module_bin: the 16 KB module player, core data installed from the .cor (<core folder>/modplayer/module.bin)
    uint32_t pn = 0;
    char dir[FS_PATH_MAX], pp[FS_PATH_MAX];
    if (!corefolder_dir(c->cor_path, dir, sizeof(dir))) {
        snprintf(o.msg, o.msglen, "the MOD player is in the .cor's folder: this start has no .cor");
        return OPE_FAILED;
    }
    snprintf(pp, sizeof(pp), "%s/modplayer/module.bin", dir);
    if (CoreFile *pf = c->fs->open(pp, false)) {
        if (pf->size() == sizeof(player) && pf->read(0, player, sizeof(player))) {
            pn = sizeof(player);
        }
        c->fs->close(pf);
    }
    if (!pn) {
        snprintf(o.msg, o.msglen, "cannot read %s (installed from the core's .cor)", pp);
        return OPE_FAILED;
    }
    for (int i = 0; i < 64; i++) {
        ioWrite8(SAMPLER_BASE + i, 0);
    }
    if (reu_load(c, a.s(0), o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    ioWrite8(AUDIO_SEL_BASE + 0x00, 6);                                          // AUDIO_SELECT_LEFT: the sampler's MOD output
    ioWrite8(AUDIO_SEL_BASE + 0x01, 7);                                          // AUDIO_SELECT_RIGHT
    if (c64_cart_run_modplayer(c, player, pn, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "Playing %s", a.s(0));
    return OPE_OK;
}

// the module player's cartridge asks for the command interface at $DF1C (c64.cc:2019-2024; the same condition as fx_io)
void trx64u_uci_df1c(CoreCtx *c)
{
    if (desc_has_capability(c->desc, "uci") && (!c->board || (c->board->probes & BC_UCI))) {
        CMD_IF_SLOT_ENABLE_REG = 1;
        CMD_IF_SLOT_BASE_REG = 0x47;
    }
}

static int op_reu_preload(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    OpIn in[2] = { in_str("key", "REU Preload Image"), in_str("value", a.s(0)) };
    OpResult r;
    ops_call(c, "settings.set", in, 2, &r);
    OpIn on[2] = { in_str("key", "REU Preload"), in_str("value", "Enabled") };
    ops_call(c, "settings.set", on, 2, &r);
    snprintf(o.msg, o.msglen, "%s goes into the REU at every start", a.s(0));
    return OPE_OK;
}

// F6 "A .cfg/.usr beside a PRG, D64, G64 or CRT is loaded before it runs" (TRX-Ultimate configio.cc:460-474, filetype_prg.cc:186-190, filetype_d64.cc:150,
// filetype_g64.cc:95, filetype_crt.cc:75): the file of the same name with the extension .cfg is read like a settings file (quietly); a PRG that has none tries .usr.
void trx64u_load_assoc_cfg(CoreCtx *c, const char *path, bool usr_too)
{
    static const char *const exts[2] = { ".cfg", ".usr" };
    for (int k = 0; k < (usr_too ? 2 : 1); k++) {
        char p[FS_PATH_MAX];
        snprintf(p, sizeof(p), "%s", path);
        char *slash = strrchr(p, '/');
        char *dot = strrchr(p, '.');
        if (dot && (!slash || dot > slash)) {
            *dot = 0;
        }
        size_t l = strlen(p);
        if (l + 5 > sizeof(p)) {
            return;
        }
        strcpy(p + l, exts[k]);
        uint64_t sz = 0;
        bool dir = false;
        if (c->fs->stat(p, &sz, &dir) && !dir) {
            OpIn in[1] = { in_str("path", p) };
            OpResult r;
            ops_call(c, "settings.load_file", in, 1, &r);
            printf("[cfg] %s: %s\n", p, r.msg);
            return;
        }
    }
}

// "Run Disk": the .cfg of the disk first, then mount and run
static int op_run_disk(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    trx64u_load_assoc_cfg(c, a.s(1), false);
    OpIn in[2] = { in_str("slot", a.s(0)), in_str("path", a.s(1)) };
    OpResult r;
    int rc = ops_call(c, "drive.mount_run", in, 2, &r);
    snprintf(o.msg, o.msglen, "%s", r.msg);
    return rc ? OPE_FAILED : OPE_OK;
}

// ---- the ROM files of the machine ----------------------------------------------------------------------------------------------------------------
static int load_rom(CoreCtx *c, const char *key, const char *put_op, uint32_t size, char *msg, int msglen)
{
    const char *name = cfg_text(c, key);
    if (!name[0]) {
        return 0;
    }
    char path[200];
    if (name[0] == '/') {
        snprintf(path, sizeof(path), "%s", name);
    } else {
        snprintf(path, sizeof(path), "/flash/roms/%s", name);
    }
    static uint8_t buf[8192];
    uint64_t fsz = 0;
    bool dir = false;
    if (!c->fs->stat(path, &fsz, &dir) || dir || fsz != size) {
        snprintf(msg, msglen, "%s: %s needs a file of %u bytes", key, path, (unsigned)size);
        return 1;
    }
    int got = c->fs->load(path, buf, sizeof(buf));
    if (got != (int)size) {
        snprintf(msg, msglen, "%s: cannot read %s", key, path);
        return 1;
    }
    OpIn in[1] = { in_blob("data", buf, size) };
    OpResult r;
    if (ops_call(c, put_op, in, 1, &r)) {
        snprintf(msg, msglen, "%s", r.msg);
        return 1;
    }
    return 0;
}

// T228 R21, FW C64::list_kernals / list_basics / list_chars (c64.cc:2353-2440) and C1541::list_roms (c1541.cc): the choices of a ROM row are the
// files in /flash/roms of that ROM's size (Kernal and Basic 8192 bytes, Char 4096, a 1541 ROM 16384 or 32768), after the built-in one; the list ends
// with "Enter manually" (menuctl). The rows of the pick list (picklist.h): value TAB label.
static const char *rom_choices(CoreCtx *c, char *t, int tlen, const char *builtin, uint64_t size, uint64_t size2)
{
    static FsEntryShort ent[24];
    int p = snprintf(t, tlen, "%s\tBuilt-in\n", builtin);
    int n = c->fs->list_short("/flash/roms", ent, 24);
    for (int i = 0; i < n; i++) {
        if (ent[i].dir || ent[i].cut || (ent[i].size != size && ent[i].size != size2) || !strcasecmp(ent[i].name, builtin) ||
            p + (int)strlen(ent[i].name) + 2 >= tlen) {
            continue;
        }
        p += snprintf(t + p, tlen - p, "%s\n", ent[i].name);
    }
    return t;
}
static const char *state_roms_kernal(CoreCtx *c) { static char t[24 * 60]; return rom_choices(c, t, sizeof(t), "", C64_KERNAL_BYTES, C64_KERNAL_BYTES); }
static const char *state_roms_basic(CoreCtx *c)  { static char t[24 * 60]; return rom_choices(c, t, sizeof(t), "", C64_BASIC_BYTES, C64_BASIC_BYTES); }
static const char *state_roms_char(CoreCtx *c)   { static char t[24 * 60]; return rom_choices(c, t, sizeof(t), "", C64_CHAR_BYTES, C64_CHAR_BYTES); }
static const char *state_roms_1541(CoreCtx *c)   { static char t[24 * 60]; return rom_choices(c, t, sizeof(t), "1541.rom", 16384, 32768); }   // "1541.rom" is the built-in one (drive.fx_misc)

// the chosen file goes in; an empty choice puts the built-in file back (not at the start: the boot has put it)
static bool roms_after_reload;
static int fx_roms(CoreCtx *c, const OpArgs &, OpOut &o)
{
    static const struct { const char *key, *put_op; } roles[3] = { { "Kernal ROM", "c64.put_kernal" }, { "Basic ROM", "c64.put_basic" }, { "Char ROM", "c64.put_char" } };
    for (int i = 0; i < 3 && c->started && !roms_after_reload; i++) {
        if (!cfg_text(c, roles[i].key)[0] && boot_put_builtin_rom(c, roles[i].put_op, o.msg, o.msglen)) {
            return OPE_FAILED;
        }
    }
    if (load_rom(c, "Kernal ROM", "c64.put_kernal", C64_KERNAL_BYTES, o.msg, o.msglen) ||
        load_rom(c, "Basic ROM", "c64.put_basic", C64_BASIC_BYTES, o.msg, o.msglen) ||
        load_rom(c, "Char ROM", "c64.put_char", C64_CHAR_BYTES, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    return OPE_OK;
}

// ---- the power of the board: the ESP32 control module keeps it (u64cfg.cc has the off / cycle operations) ---------------------------------------------
#define ESP_CMD_SET_POWER_MODE   0x16
#define ESP_CMD_GET_POWER_MODE   0x17
#define ESP_CMD_SET_WAKE_ON_WIFI 0x18
#define ESP_CMD_GET_WAKE_ON_WIFI 0x19

// reply layout: header (4) + esp_err (4) + the field(s)
static bool esp_get(uint8_t cmd, int field, uint8_t *v)
{
    uint8_t r[16];
    int n = hw_esp32_rpc(cmd, 0, 0, r, sizeof(r), 300);
    if (n < 9 + field || r[4] || r[5] || r[6] || r[7]) {
        return false;
    }
    *v = r[8 + field];
    return true;
}

static int fx_power(CoreCtx *c, const OpArgs &, OpOut &o)
{
    uint8_t want, have;
    want = (uint8_t)cfg_value(c, "Power On After Power Loss", 0);
    if (!esp_get(ESP_CMD_GET_POWER_MODE, 0, &have)) {
        snprintf(o.msg, o.msglen, "The control module does not answer: power-on behaviour not stored");
        return OPE_IO;
    }
    if (have != want) {
        hw_esp32_rpc(ESP_CMD_SET_POWER_MODE, &want, 1, 0, 0, 0);
    }
    want = (uint8_t)cfg_value(c, "Wake On Wi-Fi", 0);
    if (esp_get(ESP_CMD_GET_WAKE_ON_WIFI, 0, &have) && have != want) {
        hw_esp32_rpc(ESP_CMD_SET_WAKE_ON_WIFI, &want, 1, 0, 0, 0);
    }
    return OPE_OK;
}

// ---- drive A: the settings of Gideon's c1541 store that the library has a place for, and the actions the menu adds -------------------------------------
#include "cbm1541.h"
#include "d1541_drive.h"

static int fx_drive_misc(CoreCtx *c, const OpArgs &, OpOut &o)
{
    CbmDrive *d = cbm1541_drive(c);
    if (!d) {
        return OPE_OK;                                              // no drive in this machine: nothing to set
    }
    d->set_twist(cfg_value(c, "Track Twist", 27));
    d->set_align(cfg_value(c, "GCR Save Align Tracks", 1) != 0);
    d->set_reset_with_c64(cfg_value(c, "Resets when C64 resets", 1) != 0);
    d->set_freeze(cfg_value(c, "Freezes in menu", 1) != 0);            // c1541.cc:343-344: b2 of the reset register, written at the next drive reset
    d->set_geos(cfg_value(c, "D64 Geos Copy Protection", 0));           // c1541.cc:599: the D64 -> GCR conversion of the next mount
    const char *rom = cfg_text(c, "ROM for 1541 mode");
    static char last_rom[64];                                       // the choice the drive has now; a change is applied at once, the other settings leave the ROM alone
    bool changed = strncmp(last_rom, rom, sizeof(last_rom) - 1) != 0;
    snprintf(last_rom, sizeof(last_rom), "%s", rom);
    if (changed && c->started && (!rom[0] || !strcasecmp(rom, "1541.rom"))) {      // back to the standard ROM
        if (boot_put_builtin_rom(c, "drive.put_rom", o.msg, o.msglen)) {
            return OPE_FAILED;
        }
    } else if (rom[0] && strcasecmp(rom, "1541.rom") && (changed || roms_after_reload)) {      // another file than the standard one
        char path[200];
        snprintf(path, sizeof(path), "%s%s", rom[0] == '/' ? "" : "/flash/roms/", rom);
        OpIn in[2] = { in_str("slot", "iec:drive_a"), in_str("path", path) };
        OpResult r;
        if (ops_call(c, "drive.set_rom", in, 2, &r)) {
            snprintf(o.msg, o.msglen, "%s", r.msg);
            return OPE_FAILED;
        }
    }
    return OPE_OK;
}

// F3 on a .bin / .64c of 8 KB: "Load Kernal" - the file goes into the KERNAL window for this session only, nothing is stored, and the machine resets
// (TRX-Ultimate filetype_bin.cc:95, 242-257: C64_SET_KERNAL, then MENU_C64_RESET). "Reload ROMs, reset" brings the chosen / built-in KERNAL back.
static int op_load_kernal(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    static uint8_t buf[C64_KERNAL_BYTES];
    int n = c->fs->load(a.s(0), buf, sizeof(buf));
    if (n != (int)C64_KERNAL_BYTES) {
        snprintf(o.msg, o.msglen, "%s: a KERNAL is %u bytes", a.s(0), (unsigned)C64_KERNAL_BYTES);
        return OPE_FAILED;
    }
    OpIn in[1] = { in_blob("data", buf, C64_KERNAL_BYTES) };
    OpResult r;
    if (ops_call(c, "c64.put_kernal", in, 1, &r)) {
        snprintf(o.msg, o.msglen, "%s", r.msg);
        return OPE_FAILED;
    }
    if (boot_cold_reset(c, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "KERNAL from %s for this session, machine restarted", a.s(0));
    return OPE_OK;
}

// "Reload ROMs": after the built-in files went in, the files chosen in F5 > C64 and the drive ROM go over them again
static int op_after_roms(CoreCtx *c, const OpArgs &, OpOut &o)
{
    roms_after_reload = true;
    OpArgs none;
    memset(&none, 0, sizeof(none));
    int rc = fx_roms(c, none, o);
    if (rc == OPE_OK) {
        rc = fx_drive_misc(c, none, o);
    }
    roms_after_reload = false;
    return rc;
}

// F3 on a .rom: "Set as 1541 ROM" - the file is copied to /flash/roms and its name stored in "ROM for 1541 mode" (TRX-Ultimate filetype_bin.cc:208-215, c1541.cc:1208-1214).
// /flash is writable for the app on the monolith profile (fsif.h fs_flash_rw, T228 R21); where it is not (the MultiCore host) the file's own path is stored and the message says so.
static int op_set_rom_store(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    const char *src = a.s(0);
    const char *base = strrchr(src, '/');
    base = base ? base + 1 : src;
    char dst[200];
    snprintf(dst, sizeof(dst), "/flash/roms/%s", base);
    const char *store = base;
    const char *note = "";
    if (strcasecmp(src, dst)) {                                       // the file lies in /flash/roms already: no copy onto itself
        static uint8_t buf[DRIVE_ROM_BYTES];
        int n = c->fs->load(src, buf, sizeof(buf));
        if (n != 16384 && n != 32768) {
            snprintf(o.msg, o.msglen, "%s: a drive ROM is 16 or 32 KB", src);
            return OPE_FAILED;
        }
        c->fs->mkdir("/flash/roms");
        if (!c->fs->save(dst, buf, (uint32_t)n)) {
            store = src;
            note = " (not copied to /flash/roms)";
        }
    }
    OpIn in[2] = { in_str("key", "ROM for 1541 mode"), in_str("value", store) };
    OpResult r;
    if (ops_call(c, "settings.set", in, 2, &r)) {
        snprintf(o.msg, o.msglen, "%s", r.msg);
        return OPE_FAILED;
    }
    OpIn d[2] = { in_str("slot", "iec:drive_a"), in_str("path", store == base ? dst : src) };
    if (ops_call(c, "drive.set_rom", d, 2, &r)) {
        snprintf(o.msg, o.msglen, "%s", r.msg);
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "%s is the ROM for 1541 mode%s", base, note);
    return OPE_OK;
}

// where the drive's image lies: its folder and name
static bool drive_image_path(CbmDrive *d, char *out, int n)
{
    if (!d->image_name() || !d->last_dir()[0]) {
        return false;
    }
    snprintf(out, n, "%s/%s", d->last_dir(), d->image_name());
    return true;
}

// "Insert Blank": a blank D64 made in a writable folder, mounted unlinked (nothing of it is written back), the file removed again
static int op_insert_blank(CoreCtx *c, const OpArgs &, OpOut &o)
{
    CbmDrive *d = cbm1541_drive(c, o.msg, o.msglen);
    if (!d) {
        return OPE_FAILED;
    }
    char dir[200];
    dir[0] = 0;
    FsEntry r[6];
    int k = c->fs->list("/", r, 6);
    for (int i = 0; i < k && !dir[0]; i++) {
        if (strcasecmp(r[i].name, "flash")) {
            snprintf(dir, sizeof(dir), "/%s", r[i].name);
        }
    }
    if (!dir[0]) {
        snprintf(o.msg, o.msglen, "A blank disk needs a medium to build it on (stick or card)");
        return OPE_FAILED;
    }
    char path[220];
    snprintf(path, sizeof(path), "%s/.blank.d64", dir);
    if (d->create(path, DF_D64, "BLANK", o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    int rc = d->mount(path, MM_UNLINKED, o.msg, o.msglen);
    c->fs->remove(path);
    if (rc) {
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "A blank disk is in drive A (unlinked: nothing is written to a file, use Save Disk)");
    return OPE_OK;
}

// "Swap Disk" and CTRL + D (TRX-Ultimate c1541.cc:653-727, userinterface.cc:444-445): the disk's file name has a denominating character - the last digit that
// no digit follows, or the last letter between two non-letters - and the next file that has the next character there (digits 0-9, letters A-Z, round and
// round, names compared in upper case) takes the place of this one after the swap delay, in the mode of this one. The walk is the firmware's own: its
// wrap-around lands on the second character (a `continue` before the loop's c++), so after 9 comes 1 and after Z comes B, as there.
static int swap_next(CoreCtx *c, CbmDrive *d, FsEntry *ents, int cap, OpOut &o)
{
    const char *cur = d->image_name();
    char work[FS_NAME_MAX + 1];
    int len = (int)strlen(cur);
    if (len < 5 || len > FS_NAME_MAX) {
        snprintf(o.msg, o.msglen, "No other disk in the series of %s", cur);
        return OPE_FAILED;
    }
    for (int i = 0; i <= len; i++) {
        work[i] = (char)toupper((unsigned char)cur[i]);
    }
    int index = -1;
    for (int k = len - 5; k >= 0; k--) {
        bool found = isdigit((unsigned char)work[k]) && !isdigit((unsigned char)work[k + 1]);
        if (k > 0) {
            found |= isalpha((unsigned char)work[k]) && !isalpha((unsigned char)work[k - 1]) && !isalpha((unsigned char)work[k + 1]);
        } else {
            found |= isalpha((unsigned char)work[k]) && !isalpha((unsigned char)work[k + 1]);
        }
        if (found) {
            index = k;
            break;
        }
    }
    if (index < 0) {
        snprintf(o.msg, o.msglen, "No denominating character found in %s", cur);
        return OPE_FAILED;
    }
    int k = c->fs->list(d->last_dir(), ents, cap);
    char current = work[index];
    char top = isalpha((unsigned char)current) ? 'A' : '0';
    char bottom = isalpha((unsigned char)current) ? 'Z' : '9';
    int steps = 0;                                              // the firmware's loop never ends with no match when the character is the first of its range (A or 0); here it stops after a round
    for (char ch = (char)(current + 1); ch != current && steps < 40; ch++, steps++) {
        if (ch > bottom) {
            ch = top;
            continue;
        }
        work[index] = ch;
        for (int i = 0; i < k; i++) {
            if (ents[i].dir || strcasecmp(ents[i].name, work)) {
                continue;
            }
            char next[FS_PATH_MAX];
            snprintf(next, sizeof(next), "%s/%s", d->last_dir(), ents[i].name);
            char name[FS_NAME_MAX + 1];
            snprintf(name, sizeof(name), "%s", ents[i].name);
            int mode = !strcmp(d->mode_name(), "ro") ? MM_RO : (!strcmp(d->mode_name(), "unlinked") ? MM_UNLINKED : MM_RW);
            if (d->eject(o.msg, o.msglen)) {                  // c1541.cc:712: what the head wrote goes back to the file first
                return OPE_FAILED;
            }
            c->io->delay_ms((uint32_t)d->swap_delay_ms());
            if (d->mount(next, mode, o.msg, o.msglen)) {
                return OPE_FAILED;
            }
            snprintf(o.msg, o.msglen, "Swapped to %s", name);
            return OPE_OK;
        }
    }
    snprintf(o.msg, o.msglen, "No matching image found for %s", cur);
    return OPE_FAILED;
}

static int op_swap(CoreCtx *c, const OpArgs &, OpOut &o)
{
    CbmDrive *d = cbm1541_drive(c, o.msg, o.msglen);
    char cur[FS_PATH_MAX];
    if (!d || !drive_image_path(d, cur, sizeof(cur))) {
        snprintf(o.msg, o.msglen, "There is no disk in the drive to swap");
        return OPE_FAILED;
    }
    FsEntry *ents = (FsEntry *)malloc(256 * sizeof(FsEntry));              // 70 KB while the swap runs, not for good: the names are whole (T180 #38)
    if (!ents) {
        snprintf(o.msg, o.msglen, "No memory to list the folder");
        return OPE_FAILED;
    }
    int rc = swap_next(c, d, ents, 256, o);
    free(ents);
    return rc;
}

// "Unlink": the disk stays in the drive, the file is let go (what the machine writes is not saved any more)
static int op_unlink(CoreCtx *c, const OpArgs &, OpOut &o)
{
    CbmDrive *d = cbm1541_drive(c, o.msg, o.msglen);
    char cur[FS_PATH_MAX];
    if (!d || !drive_image_path(d, cur, sizeof(cur))) {
        snprintf(o.msg, o.msglen, "There is no disk in the drive");
        return OPE_FAILED;
    }
    if (!strcmp(d->mode_name(), "unlinked")) {
        snprintf(o.msg, o.msglen, "The disk is unlinked already");
        return OPE_OK;
    }
    if (d->mount(cur, MM_UNLINKED, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "Unlinked: %s is no longer written", d->image_name());
    return OPE_OK;
}

// ---- the machine: pause, clear memory, save memory ------------------------------------------------------------------------------------------------
// Pause keeps C64_STOP asserted after the menu closes (run.cc close_menu, Freeze::leave(keep_stopped)); Resume takes the flag away and the close releases it.
static int op_pause_toggle(CoreCtx *c, const OpArgs &, OpOut &o)
{
    if (!c->started) {
        snprintf(o.msg, o.msglen, "The machine is not running yet");
        return OPE_FAILED;
    }
    c->paused = !c->paused;
    snprintf(o.msg, o.msglen, c->paused ? "Paused: the machine stands still, F7 or the menu key brings the menu back" : "Resumed");
    return OPE_OK;
}

static const char *state_pause_label(CoreCtx *c) { return c->paused ? "Resume" : "Pause"; }
static const char *state_machine(CoreCtx *c) { return !c->started ? "Waiting" : (c->paused ? "Paused" : (c->menu_open ? "Frozen" : "Running")); }

// "Reboot (Clr Mem)": the RAM is zeroed through the second port while the machine is held, then the cold reset (Gideon MENU_C64_CLEARMEM: clear_ram, start_cartridge)
static int op_clear_reboot(CoreCtx *c, const OpArgs &, OpOut &o)
{
    static uint8_t zero[4096];
    c->io->mode_reset(true);
    c->io->ram_memonly(true);
    for (uint32_t a = 0; a < 0x10000; a += sizeof(zero)) {
        c->io->ram_write(a, zero, sizeof(zero));
    }
    c->io->ram_memonly(false);
    if (boot_cold_reset(c, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "Memory cleared, machine restarted");
    return OPE_OK;
}

// where a file of the menu goes: the folder the menu was on (MenuCtl::note_folder), else the first medium; the extension is added when the name has none
bool trx_target_path(CoreCtx *c, const char *name, const char *ext, char *out, int n)
{
    char dir[200];
    snprintf(dir, sizeof(dir), "%s", c->media_dir);
    if (!dir[0]) {
        FsEntry r[4];
        int k = c->fs->list("/", r, 4);
        for (int i = 0; i < k; i++) {
            if (strcasecmp(r[i].name, "flash")) {
                snprintf(dir, sizeof(dir), "/%s", r[i].name);
                break;
            }
        }
    }
    if (!dir[0]) {
        return false;
    }
    const char *dot = strrchr(name, '.');
    int dl = (int)strlen(dir);
    snprintf(out, n, "%s%s%s%s", dir, dl && dir[dl - 1] == '/' ? "" : "/", name, dot ? "" : ext);
    return true;
}

static int write_ddr(CoreCtx *c, const char *path, uint32_t off, uint32_t len, bool ram, char *msg, int msglen)
{
    static uint8_t buf[0x10000];
    if (!c->fs->save(path, buf, 0)) {
        snprintf(msg, msglen, "cannot write %s", path);
        return 1;
    }
    CoreFile *f = c->fs->open(path, true);
    if (!f || !f->writable()) {
        if (f) {
            c->fs->close(f);
        }
        snprintf(msg, msglen, "%s: the medium cannot be written", path);
        return 1;
    }
    bool ok = true;
    for (uint32_t done = 0; ok && done < len;) {
        uint32_t n = len - done < sizeof(buf) ? len - done : (uint32_t)sizeof(buf);
        if (ram) {
            c->io->ram_memonly(true);
            c->io->ram_read(off + done, buf, n);
            c->io->ram_memonly(false);
        } else {
            c->io->ddr_read(off + done, buf, n);
        }
        ok = f->write(done, buf, n);
        done += n;
    }
    ok = ok && f->sync();
    c->fs->close(f);
    if (!ok) {
        c->fs->remove(path);
        snprintf(msg, msglen, "%s: writing failed (medium full or gone)", path);
        return 1;
    }
    snprintf(msg, msglen, "Saved %s (%u KB)", path, (unsigned)((len + 1023) >> 10));
    return 0;
}

static int op_save_ram(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    char p[260];
    if (!trx_target_path(c, a.s(0), ".bin", p, sizeof(p))) {
        snprintf(o.msg, o.msglen, "No medium to save to");
        return OPE_FAILED;
    }
    return write_ddr(c, p, 0, 0x10000, true, o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

static int op_save_reu(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    char p[260];
    if (cfg_value(c, "RAM Expansion Unit", 0) != 1) {
        snprintf(o.msg, o.msglen, "The REU is not enabled (F5 > 4 I/O)");
        return OPE_FAILED;
    }
    if (!trx_target_path(c, a.s(0), ".reu", p, sizeof(p))) {
        snprintf(o.msg, o.msglen, "No medium to save to");
        return OPE_FAILED;
    }
    return write_ddr(c, p, 0, 128u * 1024u << cfg_value(c, "REU Size", 4), false, o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

// ---- F7 #4 the GEOS RAM drives in the REU (TRX-Ultimate c64.cc:2354-2391, c64_subsys.cc:852-912, filetype_d64.cc:121-124, 161-240) -------------------------------------
// MegaPatch 3 keeps up to four RAM drives in the REU: the type byte of drive n is at $BB0E + n (GEOS 64: at $798E + n, one table further, $81 .. $84), its first
// bank at $7DC7 + n; a native one (type $84 / $A4) is as large as its track count says (the drive's table at the address kept at $BAFE + n / $BB02 + n). The REU's
// memory is the DDR2 window from offset 0; with a cartridge in the port it holds the cartridge's image and there is no drive.
#define MP3_DNP 31
static uint8_t reu_byte(CoreCtx *c, uint32_t at)
{
    uint8_t b = 0;
    c->io->ddr_read(at, &b, 1);
    return b;
}

static int mp3_type(CoreCtx *c, int n)
{
    if (c64_cart_active()) {
        return 0;
    }
    uint8_t head[4], real = reu_byte(c, 0xBB0E + (uint32_t)n);
    c->io->ddr_read(0xBB0E, head, 4);
    if (head[0] == 0x03 && head[1] == 0xA9 && head[2] == 0x06 && head[3] == 0x8D) {          // GEOS 64
        real = reu_byte(c, 0x798E + (uint32_t)n);
        if (real > 0x83) {
            real = 0;
        }
    }
    int t = 0;
    t = real == 0x81 ? 1541 : t;
    // 0x82, a 1571 RAM drive, counts as none: .d71 is out of trx64u (Alex 2026-11-18: ".d71 goes out completely")
    t = real == 0x83 ? 1581 : t;
    t = real == 0x84 || real == 0xA4 ? MP3_DNP : t;
    return reu_byte(c, 0x7DC7 + (uint32_t)n) > 0x40 ? 0 : t;
}

static uint32_t mp3_size(CoreCtx *c, int n, int type)
{
    if (type == 1541) {
        return 174848;
    }
    if (type == 1581) {
        return 819200;
    }
    uint32_t base = reu_byte(c, 0xBAFE + (uint32_t)n) | ((uint32_t)reu_byte(c, 0xBAFE + 4 + (uint32_t)n) << 8);
    return (uint32_t)reu_byte(c, base + 0x84) << 16;
}

// The command interface's view of a RAM drive (CONTROL 0x40, DOS 0x41 / 0x42): its type, its size (a native one by its track count) and where its first byte lies in the REU.
static void uci_ramdrive(CoreCtx *c, int n, int *type, uint32_t *size, uint32_t *base)
{
    *type = mp3_type(c, n);
    *size = *type ? mp3_size(c, n, *type) : 0;
    *base = (uint32_t)reu_byte(c, 0x7DC7 + (uint32_t)n) << 16;
}

template <int N> static const char *state_mp3(CoreCtx *c)
{
    switch (mp3_type(c, N)) {
    case 1541: return "1541";
    case 1581: return "1581";
    case MP3_DNP: return "DNP";
    default: return 0;
    }
}
// FW filetype_reu.cc:41: "Play MOD" is offered only with the sampler in the gateware
static const char *state_sampler(CoreCtx *) { return (getFpgaCapabilities() & CAPAB_SAMPLER) ? "yes" : 0; }
template <int N> static const char *state_nomp3(CoreCtx *c) { return mp3_type(c, N) ? 0 : "none"; }

// "Save MP3 Drv A..D": the drive's bytes into a file named by the person
static int op_save_mp3(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    int n = a.i(0), type = mp3_type(c, n);
    char p[260];
    if (!type) {
        snprintf(o.msg, o.msglen, "No GEOS RAM drive %c in the REU", 'A' + n);
        return OPE_FAILED;
    }
    if (!trx_target_path(c, a.s(1), type == 1541 ? ".d64" : type == 1581 ? ".d81" : ".dnp", p, sizeof(p))) {
        snprintf(o.msg, o.msglen, "No medium to save to");
        return OPE_FAILED;
    }
    uint32_t len = mp3_size(c, n, type), at = (uint32_t)reu_byte(c, 0x7DC7 + (uint32_t)n) << 16;
    return write_ddr(c, p, at, len, false, o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

// "Load into MP3 Drv A..D" on a .d64 / .d81 / .dnp: the drive must be of the file's type (a native one: of the file's size, at most 12 MB)
static int op_load_mp3(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    const char *path = a.s(0);
    int n = a.i(1), type = mp3_type(c, n);
    const char *dot = strrchr(path, '.');
    int want = !dot ? 0 : !strcasecmp(dot, ".d64") ? 1541 : !strcasecmp(dot, ".d81") ? 1581 : !strcasecmp(dot, ".dnp") ? MP3_DNP : 0;
    if (!type || type != want) {
        snprintf(o.msg, o.msglen, "Invalid operation: RAM drive %c is not of this type", 'A' + n);
        return OPE_FAILED;
    }
    uint32_t len = mp3_size(c, n, type), at = (uint32_t)reu_byte(c, 0x7DC7 + (uint32_t)n) << 16;
    uint64_t sz = 0;
    bool dir = false;
    if (!c->fs->stat(path, &sz, &dir) || dir) {
        snprintf(o.msg, o.msglen, "No file %s", path);
        return OPE_FAILED;
    }
    if (type == MP3_DNP) {
        if (sz != len) {
            snprintf(o.msg, o.msglen, "Size does not match");
            return OPE_FAILED;
        }
        if (len > 12u * 1024u * 1024u) {
            snprintf(o.msg, o.msglen, "DNP file too large");
            return OPE_FAILED;
        }
    }
    CoreFile *f = c->fs->open(path, false);
    if (!f) {
        snprintf(o.msg, o.msglen, "Cannot open %s", path);
        return OPE_FAILED;
    }
    static uint8_t buf[0x10000];
    uint32_t half = len, total = 0;
    bool ok = true;
    {
        uint32_t from = 0, dst = at;
        for (uint32_t done = 0; ok && done < half && from + done < sz;) {
            uint32_t k = half - done < sizeof(buf) ? half - done : (uint32_t)sizeof(buf);
            if ((uint64_t)from + done + k > sz) {
                k = (uint32_t)(sz - from - done);
            }
            ok = f->read(from + done, buf, k);
            c->io->ddr_write(dst + done, buf, k);
            done += k;
            total += k;
        }
    }
    c->fs->close(f);
    if (!ok) {
        snprintf(o.msg, o.msglen, "Cannot read %s (the medium is gone?)", path);
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "Bytes loaded: %u ($%08x)", (unsigned)total, (unsigned)total);
    return OPE_OK;
}

static int op_save_cart(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    char p[260];
    if (!trx_target_path(c, a.s(0), ".crt", p, sizeof(p))) {
        snprintf(o.msg, o.msglen, "No medium to save to");
        return OPE_FAILED;
    }
    return c64_cart_save(c, p, o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

static const char *state_suggest_ram(CoreCtx *) { return "c64ram"; }
static const char *state_suggest_reu(CoreCtx *) { return "reu"; }
static const char *state_suggest_cart(CoreCtx *) { return "cartridge"; }
static const char *state_suggest_tw(CoreCtx *) { return "timewarp"; }
static const char *state_suggest_ring(CoreCtx *) { return "ring"; }

// ---- states ----------------------------------------------------------------------------------------------------------------------------------------
// the rows of the REU (size, preload) are listed while the expansion is not Disabled
static const char *state_reu_on(CoreCtx *c) { return cfg_value(c, "RAM Expansion Unit", 0) == 1 && !port_taken(c) ? "yes" : 0; }   // T228 R20 2: the REU is there only as the REU (not GeoRAM) and while no cartridge has the port
// the three rows of the port's devices (REU, Ultimate audio, UCI) are listed while the port is free; while it is taken a row of the same name says why they are off (c64.cc:424-428)
static const char *state_port_taken(CoreCtx *c) { return port_taken(c) ? "Disabled (cartridge)" : 0; }
static const char *state_cartridge(CoreCtx *) { return c64_cart_active() ? c64_cart_name() : 0; }
static const char *state_cartridge_path(CoreCtx *) { return c64_cart_active() ? c64_cart_path() : 0; }

static const ArgSpec value_arg[] = { ARG_INT("value", -2000, 2000) };
static const ArgSpec name_arg[] = { ARG_STRING("name", 63) };
static const ArgSpec path_arg[] = { ARG_PATH("path") };
static const ArgSpec save_mp3_args[] = { ARG_INT("drive", 0, 3), ARG_STRING("name", 63) };
static const ArgSpec load_mp3_args[] = { ARG_PATH("path"), ARG_INT("drive", 0, 3) };
static const ArgSpec run_disk_args[] = { ARG_SLOT("slot"), ARG_PATH("path") };

// ---- the command interface's machine side (uci.cc): the REU and drive A -------------------------------------------------------------------------------------------
static int uci_reu(CoreCtx *c, uint32_t *size, uint32_t *offset, char *img, int imgn, char *busy, int busyn)
{
    if (cfg_value(c, "RAM Expansion Unit", 0) != 1) {
        return -2;
    }
    if (c64_cart_active()) {
        snprintf(busy, busyn, "A cartridge uses the REU's memory: remove it first");
        return -4;
    }
    *size = 0x20000u << cfg_value(c, "REU Size", 4);
    *offset = reu_offset(c);
    snprintf(img, imgn, "%s", cfg_text(c, "REU Preload Image"));
    return 0;
}

static bool uci_drive_info(CoreCtx *c, int idx, uint8_t *type, uint8_t *addr, bool *power)
{
    CbmDrive *d = idx == 0 ? cbm1541_drive(c) : 0;
    if (!d) {
        return false;
    }
    *type = (uint8_t)(d->type() == DT_UNSET ? DT_1541 : d->type());
    *addr = (uint8_t)d->bus_id();
    *power = d->powered();
    return true;
}

static int uci_drive_do(CoreCtx *c, int idx, int what, const char *path, char *msg, int msglen)
{
    static const char *const names[] = { "drive.mount", "drive.eject", "drive.swap", "drive.set_power", "drive.set_power" };
    OpIn in[3] = { in_str("slot", "iec:drive_a"), in_str("path", path ? path : ""), in_str("mode", "rw") };
    OpIn pw[1] = { in_int("value", what == 4 ? 1 : 0) };
    OpResult *r = new OpResult;
    int rc = idx != 0 ? OPE_FAILED : ops_call(c, names[what], what == 0 ? in : what == 1 ? in : what == 2 ? in + 3 : pw, what == 0 ? 3 : what == 1 ? 1 : what == 2 ? 0 : 1, r);
    snprintf(msg, msglen, "%s", r->msg);
    delete r;
    return rc == OPE_OK ? 0 : 1;
}

static void uci_ramdrive(CoreCtx *c, int n, int *type, uint32_t *size, uint32_t *base);

static bool uci_ef_clear(CoreCtx *c, uint8_t bank, bool high) { return c64_cart_ef_clear(c, bank, high); }
static int uci_sid_map(CoreCtx *c, uint8_t *data) { return u64cfg_sid_map(c, data); }
static void uci_palette(CoreCtx *c, int what, uint8_t idx, uint8_t (*rgb)[3]) { u64cfg_palette(c, what, idx, rgb); }

static const UciHost *uci_host(void)
{
    static const UciHost h = { uci_reu, uci_drive_info, uci_drive_do, uci_sid_map, "/temp/c64_memory.bin", uci_ef_clear, uci_ramdrive, uci_palette };
    return &h;
}


// ---- the SID player (T228 R21 item 7) ---------------------------------------------------------------------------------------------------------
// FW filetypes/filetype_sid.cc (Gideon Zweijtzer, GPL-3.0): FileTypeSID::execute, prepare (:600), readSongLengths (:520), load (:742), loadFile (:808),
// tryLoadStereoMus (:560), configureMusEnv (:850), the handshake with the player cartridge (software/6502/sidcrt/basecrt.asm: initMem, start). The header work
// is sidfile.cc; the auto mapping on the SIDs is u64cfg.cc (u64cfg_sid_autoconfig). The player itself is FW's cartridge program, unchanged: sidcrt.bin, muscrt.bin and
// musplayer.bin of TRX64-Ultimate's software/ultimate-app/software/6502/sidcrt (Wilfred Bos / Gideon Zweijtzer, GPL-3.0), built there by 64tass. They are core
// data: the .cor of trx64u carries them (record 8, files) and the application installs them into <core folder>/sidplayer/ at the start; nothing of them is in the
// application image. FW's cartridge is 16 KB: its own 8 KB at $8000 and a copy of the BASIC ROM at $A000 (FW embeds basic.bin); here the BASIC ROM is the one the
// machine runs with (the file the ROM manager found, kept as c64.put_basic passes it on), the song lengths at +0x3000 of the image as FW has them.
// Different from FW: (1) the RAM is written through the RAM's second port (MEMONLY), where FW writes through the DMA window while the machine is stopped: the
// bytes land in RAM under the I/O area and the ROMs, which is where the player cartridge has the PLA ($01 = $34) anyway; (2) $DD0D is not poked, the reset
// that starts the cartridge clears the CIA again; (3) the wait for the cartridge's handshake (FW: 30 x 25 ms of stop and resume) is 1000 ms of the machine's
// running time, read through the second port; (4) the cartridge before it is put back by the next reset of the application (c64_cart_player_end), where FW
// restores it when its reset interrupt comes.
#define SIDP_DIR      "sidplayer"
#define SID_WAIT_MS   1000
static uint8_t g_basic[0x2000];
static bool g_have_basic;
void trx64u_basic_put(CoreCtx *, const uint8_t *data, uint32_t n)
{
    if (n == sizeof(g_basic)) {
        memcpy(g_basic, data, n);
        g_have_basic = true;
    }
}

static bool ext_is(const char *path, const char *e)
{
    const char *d = strrchr(path, '.');
    return d && !strcasecmp(d + 1, e);
}

// FW set_extension: the part behind the last dot is replaced (or the extension added)
static void with_ext(const char *in, const char *ext, char *out, int n)
{
    snprintf(out, n, "%s", in);
    char *d = strrchr(out, '.');
    char *sl = strrchr(out, '/');
    if (d && (!sl || d > sl)) {
        *d = 0;
    }
    int l = (int)strlen(out);
    snprintf(out + l, n - l, "%s", ext);
}

static const char *file_name(const char *path)
{
    const char *sl = strrchr(path, '/');
    return sl ? sl + 1 : path;
}

struct SidFile {
    SidTune t;
    uint32_t fsize;
    uint16_t load_le;
    bool load0;                       // the header names no load address: the first two bytes of the data do
    char use[FS_PATH_MAX];            // the file whose bytes are the tune (a .str selects its .mus)
};
static SidFile g_sf;

static int sid_open(CoreCtx *c, const char *path, SidFile *sf, bool for_play, char *msg, int msglen)
{
    bool ext_mus = ext_is(path, "mus") || ext_is(path, "str");
    if (for_play && ext_mus) {
        with_ext(path, ".mus", sf->use, sizeof(sf->use));            // a .str selected: the .mus is loaded first
    } else {
        snprintf(sf->use, sizeof(sf->use), "%s", path);
    }
    CoreFile *f = c->fs->open(sf->use, false);
    if (!f) {
        snprintf(msg, msglen, "Can't open file");
        return 1;
    }
    sf->fsize = fs_size32(f->size());
    uint8_t head[SID_HDR_READ];
    uint32_t n = sf->fsize < sizeof(head) ? sf->fsize : (uint32_t)sizeof(head);
    bool ok = n == 0 || f->read(0, head, n);
    sf->load0 = false;
    sf->load_le = 0;
    if (ok && !ext_mus && n >= SID_HDR_READ) {
        uint32_t doff = ((uint32_t)head[6] << 8) | head[7];
        sf->load0 = (head[8] | head[9]) == 0 && !(head[0x77] & 1);
        uint8_t w[2];
        if (sf->load0 && sf->fsize >= doff + 2 && f->read(doff, w, 2)) {
            sf->load_le = (uint16_t)(w[0] | (w[1] << 8));
        }
    }
    c->fs->close(f);
    if (!ok) {
        snprintf(msg, msglen, "Can't read header");
        return 1;
    }
    if (!sid_parse(head, n, file_name(path), ext_mus, &sf->t)) {
        snprintf(msg, msglen, "File type error");
        return 1;
    }
    return 0;
}

static const ArgSpec sid_args[] = { ARG_PATH("path"), ARG_INT("song", 0, 65535) };

// "Play Song n" is offered for a tune with more than one song (FW fetch_context_items); the browser asks here how many entries to make
static int op_sid_songs(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    SidFile *sf = &g_sf;
    char m[80];
    int n = 0;
    if (!sid_open(c, a.s(0), sf, false, m, sizeof(m)) && sf->t.songs > 1) {
        n = sf->t.songs > 256 ? 256 : sf->t.songs;
    }
    if (o.datacap >= 2) {
        o.data[0] = (uint8_t)n;
        o.data[1] = (uint8_t)(n >> 8);
        o.ndata = 2;
    }
    return OPE_OK;
}

// FW FileTypeSID::fetch_context_items: a file whose header is not valid has no actions - 1 here when the header reads, else 0 (the count of a `repeat_op` action without "%d")
static int op_sid_valid(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    char m[80];
    int n = sid_open(c, a.s(0), &g_sf, false, m, sizeof(m)) ? 0 : 1;
    if (o.datacap >= 2) {
        o.data[0] = (uint8_t)n;
        o.data[1] = 0;
        o.ndata = 2;
    }
    return OPE_OK;
}

static int op_sid_info(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    SidFile *sf = &g_sf;
    if (sid_open(c, a.s(0), sf, false, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    int n = sid_info_text(sf->t, (char *)o.data, o.datacap - 1);
    o.data[n] = 0;
    o.ndata = n;
    return OPE_OK;
}

// ---- the job: the handshake with the cartridge, then the tune ------------------------------------------------------------------------------------
static uint8_t g_img[0x4000];
static uint8_t g_sidcrt[0x2000];                                   // SOCKET_CMD_LOADSIDCRT's player image (kept until the next LOADSIDCRT or the app's reset)
static bool g_sidcrt_set;
static int op_dma_sidcrt(CoreCtx *, const OpArgs &a, OpOut &)
{
    uint32_t n = a.len(0) > 0x2000 ? 0x2000 : a.len(0);            // FW: min(len, 0x2000) bytes over the start of the cartridge buffer, the rest of the buffer stays
    if (!g_sidcrt_set) {
        memset(g_sidcrt, 0, sizeof(g_sidcrt));
    }
    memcpy(g_sidcrt, a.p(0), n);
    g_sidcrt_set = true;
    return OPE_OK;
}
static struct {
    bool active;
    uint32_t last_ms, ran_ms;
    char path[FS_PATH_MAX];           // the file as selected (the song lengths and the .str are looked for beside it)
} g_sj;

static void ram_put(CoreIo *io, uint32_t addr, const uint8_t *p, uint32_t n)
{
    addr &= 0xFFFF;
    if (addr + n > 0x10000) {                                       // the load wraps past $FFFF (FW loadFile)
        uint32_t k = 0x10000 - addr;
        io->ram_write(addr, p, k);
        io->ram_write(0, p + k, n - k);
    } else {
        io->ram_write(addr, p, n);
    }
}

static void poke(CoreIo *io, uint32_t addr, uint8_t v)
{
    io->ram_write(addr, &v, 1);
}

// FW loadFile: from `pos` of the file to its end (at most 64 KB) into the C64's RAM at `offset`; returns the offset behind the last byte
static uint32_t load_file(CoreCtx *c, const char *path, uint32_t pos, uint32_t offset)
{
    CoreFile *f = c->fs->open(path, false);
    if (!f) {
        return offset;
    }
    uint32_t size = fs_size32(f->size());
    uint8_t buf[512];
    uint32_t left = 65536;
    while (pos < size && left) {
        uint32_t n = size - pos < sizeof(buf) ? size - pos : (uint32_t)sizeof(buf);
        if (n > left) {
            n = left;
        }
        if (!f->read(pos, buf, n)) {
            break;
        }
        ram_put(c->io, offset, buf, n);
        offset = (offset + n) & 0xFFFF;
        pos += n;
        left -= n;
    }
    c->fs->close(f);
    return offset;
}

static uint8_t mus_rd(void *user, uint32_t a)
{
    uint8_t b = 0;
    ((CoreIo *)user)->ram_read(a & 0xFFFF, &b, 1);
    return b;
}

// the tune and the player's data into the machine, with the cartridge waiting at $02 == 1
static int sid_fire(CoreCtx *c, char *msg, int msglen)
{
    CoreIo *io = c->io;
    SidFile *sf = &g_sf;
    SidTune &t = sf->t;
    io->stop(true);
    io->ram_memonly(true);
    static const uint8_t zero[512] = { 0 };
    for (uint32_t a = 0x0340; a < 0x10000; a += sizeof(zero)) {     // clear the entire memory
        io->ram_write(a, zero, 0x10000 - a < sizeof(zero) ? 0x10000 - a : (uint32_t)sizeof(zero));
    }
    uint32_t pos = t.mus && !t.sid ? 2 : t.data_offset + ((t.mus || sf->load0) ? 2 : 0);
    uint32_t off_end = load_file(c, sf->use, pos, t.start);
    if (t.mus) {
        uint32_t first_end;
        bool stereo = sid_mus_stereo(t, off_end, mus_rd, io, &first_end);
        if (stereo) {
            off_end = first_end;
        } else if (ext_is(g_sj.path, "mus") || ext_is(g_sj.path, "str")) {      // tryLoadStereoMus: the .str of the same name
            char str[FS_PATH_MAX];
            with_ext(g_sj.path, ".str", str, sizeof(str));
            CoreFile *f = c->fs->open(str, false);
            if (f) {
                c->fs->close(f);
                uint32_t e = load_file(c, str, 2, off_end);
                if (e > off_end) {
                    t.end = (uint16_t)e;
                    stereo = true;
                }
            }
        }
        static uint8_t player[0x2000];
        uint32_t pn = 0;
        char dir[FS_PATH_MAX], pp[FS_PATH_MAX];
        if (!corefolder_dir(c->cor_path, dir, sizeof(dir))) {
            io->ram_memonly(false);
            io->stop(false);
            snprintf(msg, msglen, "the SID player files are in the .cor's folder: this start has no .cor");
            return 1;
        }
        snprintf(pp, sizeof(pp), "%s/" SIDP_DIR "/musplayer.bin", dir);
        CoreFile *f = c->fs->open(pp, false);
        if (f && f->size() <= sizeof(player) && f->read(0, player, (uint32_t)f->size())) {
            pn = (uint32_t)f->size();
        }
        if (f) {
            c->fs->close(f);
        }
        if (!pn) {
            io->ram_memonly(false);
            io->stop(false);
            snprintf(msg, msglen, "cannot read %s", pp);
            return 1;
        }
        ram_put(io, 0xE000, player, pn);                            // install the MUS player
        poke(io, 0xEC6E, (uint8_t)t.start);
        poke(io, 0xEC70, (uint8_t)(t.start >> 8));
        sid_mus_header(&t, stereo);
        if (stereo) {
            poke(io, 0xFC6E, (uint8_t)off_end);
            poke(io, 0xFC70, (uint8_t)(off_end >> 8));
        }
    }
    t.hdr[0x7E] = (uint8_t)(t.end & 0xFF);
    t.hdr[0x7F] = (uint8_t)(t.end >> 8);
    SidReq req[3];
    int count = sid_requests(t, req);
    ram_put(io, t.header_location, t.hdr, 0x80);                    // the SID header goes into the C64's memory
    poke(io, 0x0164, (uint8_t)t.header_location);
    poke(io, 0x0165, (uint8_t)(t.header_location >> 8));
    static const uint16_t endptr[] = { 0x2D, 0x2F, 0x31, 0xAE };
    for (unsigned i = 0; i < sizeof(endptr) / sizeof(endptr[0]); i++) {
        poke(io, endptr[i], (uint8_t)t.end);
        poke(io, endptr[i] + 1u, (uint8_t)(t.end >> 8));
    }
    poke(io, 0x0162, 0xAA);                                         // the handshake
    poke(io, 0x00BA, 8);                                            // load drive
    poke(io, 0x0090, 0x40);                                         // load status
    poke(io, 0x0035, 0);                                            // FRESPC
    poke(io, 0x0036, 0xA0);
    io->ram_memonly(false);
    uint8_t base[3], type[3];
    for (int i = 0; i < count; i++) {
        base[i] = req[i].base;
        type[i] = req[i].type;
    }
    u64cfg_sid_autoconfig(c, count, base, type);                    // ConfigSIDs (a failure leaves the SIDs unmapped; FW goes on)
    io->stop(false);
    printf("[sid] %s: song %d of %d, $%04X-$%04X, header at $%04X, %d SID(s)\n", file_name(g_sj.path), (int)t.song, t.songs, (unsigned)t.start,
           (unsigned)t.end, (unsigned)t.header_location, count);
    return 0;
}

// FW readSongLengths: SONGLENGTHS/<name>.ssl beside the file, 512 bytes at +0x3000 of the cartridge image
static void sid_song_lengths(CoreCtx *c, SidFile *sf, const char *path)
{
    SidTune &t = sf->t;
    uint8_t *rom = g_img;
    memset(rom + 0x3000, 0, SID_SSL_BYTES);                         // no song lengths loaded unless the file is read
    char folder[FS_PATH_MAX], name[FS_PATH_MAX], ssl[FS_PATH_MAX];
    snprintf(folder, sizeof(folder), "%s", path);
    char *sl = strrchr(folder, '/');
    if (sl) {
        *sl = 0;
    }
    with_ext(file_name(path), ".ssl", name, sizeof(name));
    snprintf(ssl, sizeof(ssl), "%s/SONGLENGTHS/%s", folder, name);
    CoreFile *f = c->fs->open(ssl, false);
    if (f) {
        uint32_t n = fs_size32(f->size());
        if (n > SID_SSL_BYTES) {
            n = SID_SSL_BYTES;
        }
        f->read(0, rom + 0x3000, n);
        c->fs->close(f);
    } else if (t.hdr[0x04] == 2) {                                  // a hacked .sid: the song length of the first song only
        if ((t.hdr[0x7B] < 0xA0) && ((t.hdr[0x7B] & 0x0F) < 0x0A) && (t.hdr[0x7A] < 0xA0) && ((t.hdr[0x7A] & 0x0F) < 0x0A)) {
            int ix = (t.default_song - 1) * 2;
            rom[0x3000 + ix] = t.hdr[0x7B];
            rom[0x3001 + ix] = t.hdr[0x7A];
        }
        t.hdr[0x7A] = 0;                                            // clean up the SID header
        t.hdr[0x7B] = 0;
    }
    for (int i = 0; i < t.songs * 2 && i < SID_SSL_BYTES; i += 2) {  // not all set or invalid: the default
        uint8_t minutes = rom[0x3000 + i], seconds = rom[0x3001 + i];
        if (minutes > 0x99 || (minutes & 0x0F) > 0x09 || seconds > 0x59 || (seconds & 0x0F) > 0x09) {
            rom[0x3000 + i] = rom[0x3001 + i] = 0;
            minutes = seconds = 0;
        }
        if (!minutes && !seconds) {
            rom[0x3000 + i] = t.mus ? 3 : 5;                        // DEFAULT_SONG_LENGTH_MUS / _SID, minutes
        }
    }
}

static int op_sid_play(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    SidFile *sf = &g_sf;
    const char *path = a.s(0);
    int song = a.i(1);
    if (sid_open(c, path, sf, true, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    const char *why = 0;
    if (sid_plan(&sf->t, sf->fsize, sf->load_le, song, &why)) {
        snprintf(o.msg, o.msglen, "%s", why);
        return OPE_FAILED;
    }
    // the cartridge image: the player's 8 KB, the BASIC ROM, the song lengths (initSidCart, readSongLengths)
    char dir[FS_PATH_MAX], pp[FS_PATH_MAX];
    if (!corefolder_dir(c->cor_path, dir, sizeof(dir))) {
        snprintf(o.msg, o.msglen, "the SID player files are in the .cor's folder: this start has no .cor");
        return OPE_FAILED;
    }
    if (!g_have_basic) {
        snprintf(o.msg, o.msglen, "the BASIC ROM is not loaded");
        return OPE_FAILED;
    }
    memset(g_img, 0, sizeof(g_img));
    snprintf(pp, sizeof(pp), "%s/" SIDP_DIR "/%s", dir, sf->t.mus ? "muscrt.bin" : "sidcrt.bin");
    if (!sf->t.mus && g_sidcrt_set) {
        memcpy(g_img, g_sidcrt, 0x2000);                            // FW socket_dma.cc:182-188: the DMA-sent player stays in sid_cart until the next load (the MUS cart is not touched)
    } else {
        CoreFile *f = c->fs->open(pp, false);
        if (!f || f->size() > 0x2000 || !f->read(0, g_img, (uint32_t)f->size())) {
            if (f) {
                c->fs->close(f);
            }
            snprintf(o.msg, o.msglen, "cannot read %s (installed from the core's .cor)", pp);
            return OPE_FAILED;
        }
        c->fs->close(f);
    }
    memcpy(g_img + 0x2000, g_basic, 0x2000);
    sid_song_lengths(c, sf, path);
    // the machine as FW leaves it for the cartridge, then the cartridge and the reset
    CoreIo *io = c->io;
    io->ram_memonly(true);
    static const uint8_t zero[1024] = { 0 };
    io->ram_write(0, zero, sizeof(zero));
    poke(io, 0x0282, 0x08);                                         // make the memory test obsolete
    poke(io, 0x0284, 0xA0);
    poke(io, 0x0288, 0x04);
    poke(io, 2, 0xAA);                                              // tells the cartridge that the SID player is wanted
    poke(io, 0x0162, 0);                                            // the address of the handshake DMA load
    io->ram_memonly(false);
    snprintf(g_sj.path, sizeof(g_sj.path), "%s", path);
    if (c64_cart_run_image(c, sf->t.mus ? "MUS Player Cartridge" : "SID Player Cartridge", g_img, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    if (boot_cold_reset(c, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    g_sj.active = true;
    g_sj.last_ms = g_sj.ran_ms = 0;
    return OPE_OK;
}

void trx64u_sid_cancel(void)
{
    g_sj.active = false;
}

// the poll of c64.service: the cartridge sets $02 to 1 when it waits for the tune
void trx64u_sid_service(CoreCtx *c)
{
    if (!g_sj.active) {
        return;
    }
    uint32_t now = c->io->now_ms();
    if (g_sj.last_ms) {
        uint32_t dt = now - g_sj.last_ms;
        g_sj.ran_ms += dt > 200 ? 100 : dt;                         // a gap is the menu's: the machine did not run in it
    }
    g_sj.last_ms = now;
    uint8_t b = 0;
    c->io->ram_read(2, &b, 1);
    char msg[160];
    msg[0] = 0;
    if (b == 0x01) {
        g_sj.active = false;
        if (sid_fire(c, msg, sizeof(msg))) {
            snprintf(c->notice, sizeof(c->notice), "%s", msg);
            c->notice_pending = true;
        }
    } else if (g_sj.ran_ms > SID_WAIT_MS) {
        g_sj.active = false;
        printf("[sid] time out: the player cartridge did not answer\n");
        snprintf(c->notice, sizeof(c->notice), "Time out: the SID player cartridge did not start");
        c->notice_pending = true;
        boot_cold_reset(c, msg, sizeof(msg));                      // FW init_cartridge: the machine as it was
    }
}
// ---- the Ultimate DMA service (dmasvc.cc, FW network/socket_dma.cc:129-232): what a command of port 64 does here, by the names the server calls (dma.*). The server stages
// a PRG, a disk image or a cartridge image as a file in /Temp (FW's save_temp_file) and hands the path; the rest is the operations the menu uses. ------------------------
static int dma_call(CoreCtx *c, const char *op, const OpIn *in, int n, OpOut &o)
{
    OpResult r;
    int rc = ops_call(c, op, in, n, &r);
    snprintf(o.msg, o.msglen, "%s", r.msg);
    return rc ? OPE_FAILED : OPE_OK;
}

// SOCKET_CMD_DMA / DMARUN / DMAJUMP (FW :134-145: RUNCODE_DMALOAD, _RUN, _JUMP): 0 = into RAM at the load address, nothing started (c64.dma_prg); 1 = and run it (c64.run_prg: the machine
// is reset and the program started at READY, the menu's Run); 2 = and jump to the load address (c64.load_prg with the file's own first two bytes, FW c64_subsys.cc:1056-1060).
static int op_dma_prg(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    OpIn p[2] = { in_str("path", a.s(0)), in_int("start", 0) };
    if (a.i(1) == 0) {
        return dma_call(c, "c64.dma_prg", p, 1, o);
    }
    if (a.i(1) == 1) {
        return dma_call(c, "c64.run_prg", p, 1, o);
    }
    uint8_t b[2];
    CoreFile *f = c->fs->open(a.s(0), false);
    bool ok = f && f->size() >= 2 && f->read(0, b, 2);
    if (f) {
        c->fs->close(f);
    }
    if (!ok) {
        snprintf(o.msg, o.msglen, "No load address in %s", a.s(0));
        return OPE_FAILED;
    }
    p[1] = in_int("start", b[0] | (b[1] << 8));
    return dma_call(c, "c64.load_prg", p, 2, o);
}
// SOCKET_CMD_MOUNT_IMG / RUN_IMG (FW :178-196): the image in drive A read-write, and for RUN_IMG the real LOAD"*",8,1 and RUN (RUNCODE_MOUNT_LOAD_RUN: drive.mount_run).
static int op_dma_mount(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    if (a.i(1)) {
        OpIn in[2] = { in_str("slot", "iec:drive_a"), in_str("path", a.s(0)) };
        return dma_call(c, "drive.mount_run", in, 2, o);
    }
    OpIn in[3] = { in_str("slot", "iec:drive_a"), in_str("path", a.s(0)), in_str("mode", "rw") };
    return dma_call(c, "drive.mount", in, 3, o);
}
// SOCKET_CMD_RUN_CRT (FW :197-210, FileTypeCRT::execute_st): the cartridge is placed and the machine reset into it, as the Library's Run does.
static int op_dma_crt(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    OpIn in[1] = { in_str("path", a.s(0)) };
    return dma_call(c, "c64.run_crt", in, 1, o);
}
// SOCKET_CMD_REUWRITE (FW :165-173): the bytes into the REU's memory at the 24 bit offset, wrapping at 16 MB; dropped while a cartridge image is in that memory (FW reu_memory_usable).
static int op_dma_reu(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    if (c64_cart_active()) {
        snprintf(o.msg, o.msglen, "REU write dropped: a cartridge uses the REU's memory");
        return OPE_FAILED;
    }
    uint32_t off = (uint32_t)a.i(0), n = a.len(1);
    const uint8_t *p = a.p(1);
    while (n) {
        uint32_t k = 0x1000000u - off < n ? 0x1000000u - off : n;
        c->io->ddr_write(off, p, k);
        p += k;
        n -= k;
        off = 0;
    }
    snprintf(o.msg, o.msglen, "%u bytes written into the REU", (unsigned)a.len(1));
    return OPE_OK;
}
// SOCKET_CMD_POWEROFF (FW :161-163, MENU_C64_POWEROFF): the menu's Power off.
static int op_dma_power_off(CoreCtx *c, const OpArgs &, OpOut &o)
{
    return dma_call(c, "u64.power_off", 0, 0, o);
}
static const ArgSpec dma_prg_args[] = { ARG_PATH("path"), ARG_INT("mode", 0, 2) };
static const ArgSpec dma_mount_args[] = { ARG_PATH("path"), ARG_INT("run", 0, 1) };
static const ArgSpec dma_sidcrt_args[] = { ARG_BLOB("data") };
static const ArgSpec dma_reu_args[] = { ARG_INT("offset", 0, 0xFFFFFF), ARG_BLOB("data") };

void trx64u_register(OpRegistry &r)
{
    r.contributor("trx64u");
    r.add_op("dma.prg", dma_prg_args, 2, op_dma_prg);
    r.add_op("dma.mount", dma_mount_args, 2, op_dma_mount);
    r.add_op("dma.crt", path_arg, 1, op_dma_crt);
    r.add_op("dma.reu", dma_reu_args, 2, op_dma_reu);
    r.add_op("dma.sidcrt", dma_sidcrt_args, 1, op_dma_sidcrt);
    r.add_op("dma.power_off", 0, 0, op_dma_power_off);
    r.add_op("ui.fx", value_arg, 1, fx_ui);
    r.add_op("c64.fx_io", value_arg, 1, fx_io);
    r.add_op("c64.fx_roms", value_arg, 1, fx_roms);
    r.add_op("u64.fx_power", value_arg, 1, fx_power);
    r.add_op("drive.fx_misc", value_arg, 1, fx_drive_misc);
    r.add_op("c64.sid_play", sid_args, 2, op_sid_play);
    r.add_op("c64.sid_songs", sid_args, 2, op_sid_songs);
    r.add_op("c64.sid_valid", sid_args, 2, op_sid_valid);
    r.add_op("c64.sid_info", sid_args, 2, op_sid_info);
    r.add_op("reu.load", path_arg, 1, op_reu_load);
    r.add_op("reu.preload", path_arg, 1, op_reu_preload);
    r.add_op("reu.playmod", path_arg, 1, op_reu_playmod);
    r.add_op("drive.insert_blank", 0, 0, op_insert_blank);
    r.add_op("drive.swap", 0, 0, op_swap);
    r.add_op("machine.after_roms", 0, 0, op_after_roms);
    r.add_op("drive.set_rom_store", path_arg, 1, op_set_rom_store);
    r.add_op("drive.unlink", 0, 0, op_unlink);
    r.add_op("c64.load_kernal", path_arg, 1, op_load_kernal);
    r.add_op("drive.run_disk", run_disk_args, 2, op_run_disk);
    r.add_op("machine.save_mp3", save_mp3_args, 2, op_save_mp3);
    r.add_op("reu.load_mp3", load_mp3_args, 2, op_load_mp3);
    r.add_state("reu.mp3.a", state_mp3<0>);
    r.add_state("reu.mp3.b", state_mp3<1>);
    r.add_state("reu.mp3.c", state_mp3<2>);
    r.add_state("reu.mp3.d", state_mp3<3>);
    r.add_state("reu.sampler", state_sampler);
    r.add_state("reu.nomp3.a", state_nomp3<0>);
    r.add_state("reu.nomp3.b", state_nomp3<1>);
    r.add_state("reu.nomp3.c", state_nomp3<2>);
    r.add_state("reu.nomp3.d", state_nomp3<3>);
    r.add_state("reu.on", state_reu_on);
    r.add_state("c64.port_taken", state_port_taken);
    r.add_state("c64.cartridge", state_cartridge);
    r.add_state("c64.cartridge_path", state_cartridge_path);
    r.add_state("c64.roms_kernal", state_roms_kernal);
    r.add_state("c64.roms_basic", state_roms_basic);
    r.add_state("c64.roms_char", state_roms_char);
    r.add_state("drive.roms_1541", state_roms_1541);
    r.add_op("machine.pause_toggle", 0, 0, op_pause_toggle);
    r.add_op("machine.clear_reboot", 0, 0, op_clear_reboot);
    r.add_op("machine.save_ram", name_arg, 1, op_save_ram);
    r.add_op("machine.save_reu", name_arg, 1, op_save_reu);
    r.add_op("machine.save_cart", name_arg, 1, op_save_cart);
    r.add_state("machine.pause_label", state_pause_label);
    r.add_state("machine.state", state_machine);
    r.add_state("machine.suggest.ram", state_suggest_ram);
    r.add_state("machine.suggest.reu", state_suggest_reu);
    r.add_state("machine.suggest.cart", state_suggest_cart);
    r.add_state("machine.suggest.tw", state_suggest_tw);
    r.add_state("machine.suggest.ring", state_suggest_ring);
}
