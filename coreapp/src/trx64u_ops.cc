// The operations and states of the TRX64U menu (menu.toml) that are not the C64U's hardware registers (u64cfg.cc is those): the look of the menu, the C64's cartridge
// port, REU and ROM settings, the machine's own actions. Host-owned code; the register sequences of the cartridge port are Gideon's (vendor/gideon/software/io/c64/c64.cc
// ConfigureU64SystemBus and set_emulation_flags, GPL-3.0), the fast reset patch is his (c64.cc fastresetPatch).
#include "trx64u_ops.h"
#include "ops_glue.h"
#include "ui.h"
#include "hw.h"
#include "c64_regs.h"
#include "c64_cart.h"
#include "boot.h"
#include "u64.h"
#include "iomap.h"
#include <stdio.h>
#include <string.h>
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
static int fx_ui(CoreCtx *c, const OpArgs &, OpOut &)
{
    ui_set_look(cfg_value(c, "Overlay Transparency", 1), cfg_value(c, "Color Scheme", 1));
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

static int reu_load(CoreCtx *c, const char *path, char *msg, int msglen);

static int fx_io(CoreCtx *c, const OpArgs &, OpOut &)
{
    CoreIo *io = c->io;
    io->bus_wr(0x2A, bus_mode_values[cfg_value(c, "Bus Operation Mode", 0)]);        // C64_BUS_BRIDGE
    bool cart = c64_cart_active();
    if (!cart) {
        uint8_t internal = 0, external = 0;
        bool ext_cart = ((*(volatile uint8_t *)(U64_IO_BASE + 3) & 3) != 3);       // U64_CART_DETECT: GAME and EXROM both high = nothing in the port
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
        io->cart_wr(C64_CARTREG_REU, cfg_value(c, "RAM Expansion Unit", 0) == 1 ? 1 : 0);
        io->cart_wr(0x09, (uint8_t)cfg_value(c, "REU Size", 4));
        io->cart_wr(C64_CARTREG_SAMPLER, (uint8_t)cfg_value(c, "Map Ultimate Audio $DF20-DFFF", 0));
        if (desc_has_capability(c->desc, "uci") && (!c->board || (c->board->probes & BC_UCI))) {
            CMD_IF_SLOT_ENABLE_REG = cfg_value(c, "Command Interface (UCI)", 0) ? 1 : 0;
            CMD_IF_SLOT_BASE_REG = 0x47;                                            // $DF1C
        }
    }
    fast_reset(c);
    static bool preloaded;                                               // "REU Preload": the image goes into the REU once, at the start
    if (!preloaded && !c->started && cfg_value(c, "REU Preload", 0) && cfg_value(c, "RAM Expansion Unit", 0) == 1) {
        preloaded = true;
        char m[100];
        const char *img = cfg_text(c, "REU Preload Image");
        if (img[0] && reu_load(c, img, m, sizeof(m))) {
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

static int reu_load(CoreCtx *c, const char *path, char *msg, int msglen)
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
    uint32_t off = reu_offset(c), room = 0x1000000u - off;
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

static int fx_roms(CoreCtx *c, const OpArgs &, OpOut &o)
{
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
    const char *rom = cfg_text(c, "ROM for 1541 mode");
    if (rom[0] && strcasecmp(rom, "1541.rom")) {                    // another file than the standard one
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

static int cmp_name(const void *a, const void *b) { return strcasecmp(((const FsEntry *)a)->name, ((const FsEntry *)b)->name); }

// "Swap Disk": the next disk image of the folder (by name, round and round) takes the place of this one after the swap delay, in the mode of this one
static int swap_next(CoreCtx *c, CbmDrive *d, FsEntry *ents, int cap, OpOut &o)
{
    int k = c->fs->list(d->last_dir(), ents, cap), n = 0;
    for (int i = 0; i < k; i++) {
        int l = (int)strlen(ents[i].name);
        if (!ents[i].dir && l > 4 && (!strcasecmp(ents[i].name + l - 4, ".d64") || !strcasecmp(ents[i].name + l - 4, ".g64"))) {
            ents[n++] = ents[i];
        }
    }
    if (n < 2) {
        snprintf(o.msg, o.msglen, "There is no other disk image in %s to swap to", d->last_dir());
        return OPE_FAILED;
    }
    qsort(ents, n, sizeof(FsEntry), cmp_name);
    int at = 0;
    for (int i = 0; i < n; i++) {
        if (!strcasecmp(ents[i].name, d->image_name())) {
            at = i;
        }
    }
    char next[FS_PATH_MAX];
    snprintf(next, sizeof(next), "%s/%s", d->last_dir(), ents[(at + 1) % n].name);
    int mode = !strcmp(d->mode_name(), "ro") ? MM_RO : (!strcmp(d->mode_name(), "unlinked") ? MM_UNLINKED : MM_RW);
    if (d->eject(o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    c->io->delay_ms((uint32_t)d->swap_delay_ms());
    if (d->mount(next, mode, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "Swapped to %s", ents[(at + 1) % n].name);
    return OPE_OK;
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
static const char *state_reu_on(CoreCtx *c) { return cfg_value(c, "RAM Expansion Unit", 0) != 0 ? "yes" : 0; }
static const char *state_cartridge(CoreCtx *) { return c64_cart_active() ? c64_cart_name() : 0; }
static const char *state_cartridge_path(CoreCtx *) { return c64_cart_active() ? c64_cart_path() : 0; }

static const ArgSpec value_arg[] = { ARG_INT("value", -2000, 2000) };
static const ArgSpec name_arg[] = { ARG_STRING("name", 63) };
static const ArgSpec path_arg[] = { ARG_PATH("path") };

void trx64u_register(OpRegistry &r)
{
    r.contributor("trx64u");
    r.add_op("ui.fx", value_arg, 1, fx_ui);
    r.add_op("c64.fx_io", value_arg, 1, fx_io);
    r.add_op("c64.fx_roms", value_arg, 1, fx_roms);
    r.add_op("u64.fx_power", value_arg, 1, fx_power);
    r.add_op("drive.fx_misc", value_arg, 1, fx_drive_misc);
    r.add_op("reu.load", path_arg, 1, op_reu_load);
    r.add_op("reu.preload", path_arg, 1, op_reu_preload);
    r.add_op("drive.insert_blank", 0, 0, op_insert_blank);
    r.add_op("drive.swap", 0, 0, op_swap);
    r.add_op("drive.unlink", 0, 0, op_unlink);
    r.add_state("reu.on", state_reu_on);
    r.add_state("c64.cartridge", state_cartridge);
    r.add_state("c64.cartridge_path", state_cartridge_path);
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
