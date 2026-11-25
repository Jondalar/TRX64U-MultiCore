// T180 D2 and D5 for every core (docs/specs/done/T180-media-housekeeping.md, scenarios S2, S3 and S8): the cores whose media engine has a mount state of its own - the Apple IIe
// (D1 D2 S7.1 S7.2), the Atari (D1-D4, the cartridge, a .xex boot disk), the CPC (A B Tape), the Spectrum (Tape), the drive library of the VC-20, the C128, the C264 and the C64
// (Drive A, read/write, read only, unlinked) with the cartridge ports of the C64 and the C264, the NES, the SNES and the Neo Geo (the game, a .neo file or a ROM set folder) -
// keep their mounts in the core's .cfg ("Persist medium mounts"), mount them again in boot_start BEFORE the core is released, leave a slot whose medium is gone empty with a note,
// and take a named media set in and out with one write-back first and exactly one reset. The descriptors and their operations are the real ones; the machines are models of the
// register blocks (FakeIo, the drive model, the C64 model). A "restart" is a new application run on the same files: new context, new settings read from the .cfg, boot_start.
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "check.h"
#if defined(TEST_APP_VC20) || defined(TEST_APP_C264) || defined(TEST_APP_ZX) || defined(TEST_APP_CPC) || defined(TEST_APP_C128) || defined(TEST_APP_ATARI800) || defined(TEST_APP_NES)
#define MME_OWN_APP 1          // an app of its own is its own build (run.sh vc20app, c264app, zxapp, cpcapp, c128app, a8app, nesapp) with its core only
#endif
#if defined(TEST_APP_ZX) || defined(TEST_APP_CPC) || defined(TEST_APP_ATARI800) || defined(TEST_APP_NES)
#define MME_NO_DRIVE 1         // the app has no 1541 library and no drive window
#endif
#include "c64_model.h"
#include "boot.h"
#include "cores.h"
#include "settings.h"
#include "ops_glue.h"
#ifndef MME_OWN_APP          // stands on the shared app's cores; the VIC-20's app is its own build (run.sh vc20app) with its core only
#include "c64_cart.h"
#endif
#ifndef MME_NO_DRIVE
#include "drive_model.h"
#include "disk_build.h"
#endif
#include "menutree.h"
#include "mediamounts.h"
#include "engines.h"
#ifndef MME_OWN_APP          // stands on the shared app's cores; the VIC-20's app is its own build (run.sh vc20app) with its core only
#include "apple2_disk.h"
#include "apple2_regs.h"
#include "neogeo_regs.h"
#include "c64_regs.h"
#endif
#ifdef TEST_APP_C264
#include "c264_regs.h"
extern const CoreDesc c264_desc;                // the Commodore 264 is its own app (cores/c264/app/lib): no shared header declares it
#endif
#ifdef TEST_APP_ATARI800         // the Atari's cases (D1-D4, the cartridge, the .xex): its app (run.sh a8app, the unit a8_mounts)
#include "atari800_regs.h"
#endif
#ifdef TEST_APP_NES
#include "nes_regs.h"
#endif
#ifndef MME_NO_DRIVE
#include "d1541_drive.h"
#endif

#ifdef TEST_APP_VC20
const CoreDesc *const core_table[] = { &vc20_desc, 0 };
#elif defined(TEST_APP_C264)
const CoreDesc *const core_table[] = { &c264_desc, 0 };
#elif defined(TEST_APP_ZX)
extern const CoreDesc zx_desc;                  // cores.h lists the shared app's cores; the ZX Spectrum is its own app (cores/zx/app/lib)
const CoreDesc *const core_table[] = { &zx_desc, 0 };
#elif defined(TEST_APP_CPC)
extern const CoreDesc cpc_desc;                 // cores.h lists the shared app's cores; the CPC is its own app (cores/cpc/app/lib)
const CoreDesc *const core_table[] = { &cpc_desc, 0 };
#elif defined(TEST_APP_C128)
extern const CoreDesc c128_desc;                // cores.h lists the shared app's cores; the C128 is its own app (cores/c128/app/lib)
const CoreDesc *const core_table[] = { &c128_desc, 0 };
#elif defined(TEST_APP_ATARI800)
extern const CoreDesc atari800_desc;            // cores.h lists the shared app's cores; the Atari 800 XL is its own app (cores/atari800/app/lib)
const CoreDesc *const core_table[] = { &atari800_desc, 0 };
#elif defined(TEST_APP_NES)
extern const CoreDesc nes_desc;                 // cores.h lists the shared app's cores; the NES is its own app (cores/nes/app/lib)
const CoreDesc *const core_table[] = { &nes_desc, 0 };
#else
const CoreDesc *const core_table[] = { &apple2_desc, &zxnext_desc, &snes_desc, &neogeo_desc,
                                       0 };
#endif

typedef std::vector<uint8_t> Bytes;

// ---- the machines ------------------------------------------------------------------------------------------------------------------------
#ifdef MME_NO_DRIVE
struct GIo : FakeIo { };                              // the Spectrum, the CPC and the NES have no drive behind a window
#else
struct GIo : FakeIo {                                    // a register file, the arrays and a drive behind the drive window
    DriveModel dm;
    DRIVE_IO_FORWARD(dm)
};
#endif

#ifndef MME_OWN_APP          // stands on the shared app's cores; the VIC-20's app is its own build (run.sh vc20app) with its core only
struct NeoIo : FakeIo {                                  // the Neo Geo's register block with its copy engine (tests/coreapp/test_neogeo.cc)
    NeoIo() { ddr.assign(0x07000000, 0); }
    void ddr_write(uint32_t off, const void *p, uint32_t n) override { if ((uint64_t)off + n <= 0x03000000ull) memcpy(&ddr[off], p, n); }
    void ddr_read(uint32_t off, void *p, uint32_t n) override { if ((uint64_t)off + n <= 0x03000000ull) memcpy(p, &ddr[off], n); else memset(p, 0, n); }
    uint32_t r32(uint32_t reg) const { return regs[reg] | regs[reg + 1] << 8 | regs[reg + 2] << 16 | (uint32_t)regs[reg + 3] << 24; }
    void on_wr(uint32_t reg, uint8_t) override
    {
        if (reg != NEO_MV_GO) return;
        uint8_t cmd = regs[NEO_MV_CMD];
        if (cmd & NEO_MV_COPY) {
            uint32_t src = r32(NEO_CP_SRC), dst = r32(NEO_CP_DST), len = r32(NEO_CP_LEN);
            int mode = cmd & 3;
            for (uint32_t i = 0; i < len; i++) {
                uint32_t d = dst + i;
                bool w = mode == 0 || (mode == 1 && (d & 3) < 2) || (mode == 2 && (d & 3) >= 2);
                if (w) ddr.at(d - 0x01000000u) = ddr.at(src + i - 0x01000000u);
            }
        }
        regs[NEO_MV_DONE]++;
    }
};

// The cartridge port of a C64 core (T239.3 P1-8: the C64 core is TRX64-Ultimate's; this stub core has the one slot "Cartridge" the library c64_cart.cc serves, on the C64 model): the reset's hold and
// release, the end of the SID player's cartridge with it, and the slot's three calls into the library.
static int cart_init(CoreCtx *c, char *, int) { c->io->mode_reset(true); c64_cart_player_end(c); return 0; }
static int cart_release(CoreCtx *c, char *, int) { c->io->mode_reset(false); return 0; }
static const char *const cart_slot_names[] = { "Cartridge" };
static const char *cart_slot_spec(CoreCtx *, int) { return c64_cart_active() && c64_cart_path()[0] ? c64_cart_path() : 0; }
static int cart_slot_mount(CoreCtx *c, int, const char *spec, char *msg, int msglen) { return c64_cart_run(c, spec, msg, msglen) ? 1 : 0; }
static int cart_slot_eject(CoreCtx *c, int, char *msg, int msglen) { return c64_cart_remove(c, msg, msglen) ? 1 : 0; }
static const MountSlots cart_slots = { 1, cart_slot_names, cart_slot_spec, cart_slot_mount, cart_slot_eject };
static CoreDesc make_cart_core()
{
    CoreDesc d;
    memset(&d, 0, sizeof(d));
    strcpy(d.id, "CRT_");
    d.cor_name = "Cartridge Port";
    d.title = "Cartridge Port";
    d.short_title = "CRT";
    d.folder = "cartport";
    d.store = "cartport";
    d.init = cart_init;
    d.release = cart_release;
    d.reset_ms = 1;
    d.mount_slots = &cart_slots;
    return d;
}
static CoreDesc g_cart_core = make_cart_core();
#endif

// ---- what a core's profile says --------------------------------------------------------------------------------------------------------
struct Run;
struct Case {
    std::string key;                                     // the slot's word in the .cfg: "Mount D1"
    std::string a, b;                                    // two media for the slot (files made by the profile's world)
    std::string pre;                                     // what the stored text starts with besides the path ("ro ", "xex ")
    bool drops = false;                                  // a replacement whose file cannot be read to the end has already let the old medium go
    std::function<int(Run &, const std::string &, char *, int)> mount;      // the engine's own operation, as the menu calls it
    std::function<int(Run &, char *, int)> eject;        // ... null: the slot's own eject
};
struct Watch { std::function<uint32_t(CoreIo *)> read; uint32_t want; const char *what; };
struct Prof {
    std::string name, cfg;
    const CoreDesc *base;
    bool cart = false;                                   // the cartridge port of the C64 library: its state lives in the application, so a run that ends removes it
    bool game = false;                                   // the cartridge is the program: without one the start waits for it
    std::function<CoreIo *()> make_io;
    std::function<void(FakeFs &)> world;
    std::function<void(Run &)> prep;
    std::vector<Case> cases;
    std::vector<Watch> watch;                            // sampled at the first release, with the `a` media mounted
};

// what the core saw when the application released it, and the hooks of the run (the descriptor is a copy: the original hold and release run inside the counting ones)
static core_hook_t g_init, g_release;
static int g_holds, g_releases;
static std::vector<std::string> g_rel_specs;
static std::vector<uint32_t> g_rel_watch;
static const std::vector<Watch> *g_watch;
static bool g_flush_fail;
static int g_flush_calls;
static int w_init(CoreCtx *c, char *m, int n) { g_holds++; return g_init ? g_init(c, m, n) : 0; }
static int w_release(CoreCtx *c, char *m, int n)
{
    if (g_releases++ == 0) {
        g_rel_specs.clear();
        for (int i = 0; i < mounts_slots(c); i++) {
            const char *s = mounts_slot_spec(c, i);
            g_rel_specs.push_back(s ? s : "(empty)");
        }
        g_rel_watch.clear();
        for (const Watch &w : *g_watch) g_rel_watch.push_back(w.read(c->io));
    }
    return g_release ? g_release(c, m, n) : 0;
}
static int fake_flush(CoreCtx *, char *msg, int n)
{
    g_flush_calls++;
    if (g_flush_fail) { snprintf(msg, n, "forced: the medium refuses the write-back"); return 1; }
    return 0;
}
static bool fake_pending(CoreCtx *) { return false; }

struct Ui : BootUi {
    int n = 0;
    void message(const char *, const char *, bool) { }
    void clear() { }
    void tick() { if (++n > 50) { printf("FAIL boot_start was still waiting after %d passes\n", n); exit(1); } }
};

struct Run {
    FakeFs &fs;
    const Prof &p;
    std::unique_ptr<CoreIo> io;
    CoreDesc desc;
    Setting items[SETTINGS_MAX];
    CoreCtx c;
    Settings *st = 0;
    Ui ui;
    Run(FakeFs &f, const Prof &prof, bool fake_hooks = false) : fs(f), p(prof)
    {
        io.reset(p.make_io());
        memset(&c, 0, sizeof(c));
        const char *why = persist_extend(p.base, &desc, items);
        if (why) { printf("FAIL persist_extend: %s\n", why); g_fails++; }
        desc.nroms = 0;
        desc.romset = 0;
        desc.cold_start = 0;
        desc.reset_ms = 1;
        g_init = desc.init;
        g_release = desc.release;
        desc.init = w_init;
        desc.release = w_release;
        if (fake_hooks) {
            desc.media_flush = fake_flush;
            desc.media_pending = fake_pending;
        }
        g_watch = &p.watch;
        g_holds = g_releases = 0;
        g_rel_specs.clear();
        c.desc = &desc;
        c.io = io.get();
        c.fs = &fs;
        st = new Settings(desc.settings, desc.nsettings, desc.store, &fs, p.cfg.c_str(), 0);
        st->load();
        c.settings = st;
        snprintf(c.cfg_path, sizeof(c.cfg_path), "%s", p.cfg.c_str());
        c.mount_changed = mounts_engine_changed;
        if (p.prep) p.prep(*this);
    }
    ~Run()
    {
#ifndef MME_OWN_APP
        if (p.cart) {                                     // the cartridge port of the C64 keeps its state in the application: a start again has none
            char m[160];
            c.mount_changed = 0;
            c.media_restoring = true;
            c64_cart_remove(&c, m, sizeof(m));
        }
#endif
        delete st;
    }
    BootResult boot() { return boot_start(&c, &ui); }
    int set(const char *key, const char *value)
    {
        OpIn in[2] = { in_str("key", key), in_str("value", value) };
        OpResult res;
        return ops_call(&c, "settings.set", in, 2, &res);
    }
    int op(const char *name, const OpIn *in, int n, char *msg = 0, int msglen = 0)
    {
        OpResult res;
        int rc = ops_call(&c, name, in, n, &res);
        if (msg) snprintf(msg, msglen, "%s", res.msg);
        return rc;
    }
    std::string spec(int i) { const char *s = mounts_slot_spec(&c, i); return s ? s : "(empty)"; }
    std::string cfg() { return fs.text(p.cfg); }
};

static bool has(const std::string &text, const std::string &s) { return text.find(s) != std::string::npos; }
static int count(const std::string &text, const char *s)
{
    int n = 0;
    for (size_t k = text.find(s); k != std::string::npos; k = text.find(s, k + 1)) n++;
    return n;
}

// ---- the media ----------------------------------------------------------------------------------------------------------------------
static Bytes patt(uint32_t seed, size_t n)
{
    Bytes v(n);
    uint32_t x = seed * 2654435761u + 1;
    for (size_t i = 0; i < n; i++) { x = x * 1664525u + 1013904223u; v[i] = (uint8_t)(x >> 24); }
    return v;
}
#ifdef TEST_APP_ATARI800
static Bytes atr(int sectors, uint32_t seed)
{
    Bytes v(16, 0);
    uint32_t paras = (uint32_t)sectors * 128 / 16;
    v[0] = 0x96; v[1] = 0x02; v[2] = paras & 0xFF; v[3] = (paras >> 8) & 0xFF; v[4] = 128; v[6] = (paras >> 16) & 0xFF;
    Bytes d = patt(seed, (size_t)sectors * 128);
    v.insert(v.end(), d.begin(), d.end());
    return v;
}
#endif
#ifndef TEST_APP_VC20          // the tape file is the shared app's and the ZX Spectrum's app's
static Bytes cdt() { return Bytes{ 'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 1, 20, 0x12, 100, 0, 5, 0, 0x20, 0xE8, 0x03, 0x12, 200, 0, 2, 0 }; }
#endif
#ifdef TEST_APP_CPC          // the CPC's disk image
static Bytes dsk(uint32_t seed)
{
    const char *magic = "MV - CPCEMU Disk-File\r\nDisk-Info\r\n";
    Bytes v(1024, 0);
    memcpy(v.data(), magic, strlen(magic));
    Bytes d = patt(seed, 512);
    memcpy(&v[512], d.data(), 512);
    return v;
}
#endif
#ifndef MME_NO_DRIVE          // the Spectrum's app has no drive: no disk image, no 1541 library
static Bytes d64(const char *name)
{
    Disk d(false);
    (void)name;
    return d.img;
}
#endif
#ifdef TEST_APP_NES          // the NES's game file
static Bytes nes_file(uint32_t seed)
{
    Bytes v(16, 0);
    memcpy(v.data(), "NES\x1A", 4);
    v[4] = 1; v[5] = 1;
    Bytes d = patt(seed, 16384 + 8192);
    v.insert(v.end(), d.begin(), d.end());
    return v;
}
#endif
#ifndef MME_OWN_APP          // stands on the shared app's cores; the VIC-20's app is its own build (run.sh vc20app) with its core only
static Bytes snes_file(uint32_t seed)
{
    Bytes v(0x80000);
    for (uint32_t i = 0; i < v.size(); i++) v[i] = (uint8_t)(i * 7 + seed + (i >> 8) * 13 + 1);
    uint8_t *h = &v[0x7FC0];
    memset(h, ' ', 21);
    memcpy(h, "TEST CART", 9);
    h[0x15] = 0x20; h[0x16] = 0; h[0x17] = 0x09; h[0x18] = 0; h[0x19] = 1;
    uint16_t sum = 0x4321, comp = (uint16_t)~sum;
    h[0x1C] = (uint8_t)comp; h[0x1D] = (uint8_t)(comp >> 8); h[0x1E] = (uint8_t)sum; h[0x1F] = (uint8_t)(sum >> 8);
    h[0x3C] = 0x00; h[0x3D] = 0x80;
    return v;
}
static Bytes neo_file(uint32_t seed)
{
    Bytes P = patt(seed, 0x3000), S = patt(seed + 1, 0x2000), M = patt(seed + 2, 0x1000), V1 = patt(seed + 3, 0x4000), V2 = patt(seed + 4, 0x2000), C = patt(seed + 5, 0x4000);
    memcpy(&P[0x100], "NEO-GEO", 7);
    Bytes f(4096, 0);
    memcpy(f.data(), "NEO\x01", 4);
    uint32_t sz[6] = { (uint32_t)P.size(), (uint32_t)S.size(), (uint32_t)M.size(), (uint32_t)V1.size(), (uint32_t)V2.size(), (uint32_t)C.size() };
    for (int i = 0; i < 6; i++) for (int b = 0; b < 4; b++) f[4 + 4 * i + b] = (uint8_t)(sz[i] >> (8 * b));
    for (auto *v : { &P, &S, &M, &V1, &V2, &C }) f.insert(f.end(), v->begin(), v->end());
    return f;
}
static const char *NEO_XML =
    "<?xml version=\"1.0\"?>\n<romsets>\n"
    "  <romset name=\"aoft\" pcm=\"1\" altname=\"Art of Fighting\" publisher=\"SNK\" year=\"1992\">\n"
    "    <file name=\"p1.p1\" type=\"P\" index=\"4\" offset=\"0\"/>\n"
    "    <file name=\"s1.s1\" type=\"S\" index=\"8\" offset=\"0\"/>\n"
    "    <file name=\"c1.c1\" type=\"C\" index=\"64\" offset=\"0\"/>\n"
    "    <file name=\"c2.c2\" type=\"C\" index=\"65\" offset=\"0\"/>\n"
    "    <file name=\"m1.m1\" index=\"9\"/>\n"
    "    <file name=\"v1.v1\" index=\"16\"/>\n"
    "  </romset>\n</romsets>\n";
static void crt_chip(Bytes &b, uint16_t load, uint16_t size, uint8_t fill)
{
    const char *sig = "CHIP";
    b.insert(b.end(), sig, sig + 4);
    uint32_t len = 16u + size;
    b.push_back((uint8_t)(len >> 24)); b.push_back((uint8_t)(len >> 16)); b.push_back((uint8_t)(len >> 8)); b.push_back((uint8_t)len);
    b.push_back(0); b.push_back(0); b.push_back(0); b.push_back(0);
    b.push_back((uint8_t)(load >> 8)); b.push_back((uint8_t)load); b.push_back((uint8_t)(size >> 8)); b.push_back((uint8_t)size);
    for (int i = 0; i < size; i++) b.push_back((uint8_t)(fill + i));
}
static Bytes crt8(uint8_t fill)                         // an 8 KB cartridge (EXROM 0, GAME 1): CART_TYPE 0x41
{
    Bytes b;
    const char *sig = "C64 CARTRIDGE   ";
    b.insert(b.end(), sig, sig + 16);
    b.push_back(0); b.push_back(0); b.push_back(0); b.push_back(0x40);
    b.push_back(1); b.push_back(0); b.push_back(0); b.push_back(0);
    b.push_back(0); b.push_back(1);
    b.resize(0x40, 0);
    crt_chip(b, 0x8000, 0x2000, fill);
    return b;
}

#endif
// ---- operations as the menu calls them --------------------------------------------------------------------------------------------------
static int mount_op(Run &r, const char *op, const std::vector<OpIn> &in, char *msg, int n) { return r.op(op, in.data(), (int)in.size(), msg, n) ? 1 : 0; }
#ifndef MME_OWN_APP          // stands on the shared app's cores; the VIC-20's app is its own build (run.sh vc20app) with its core only

static Case apple_drive(int d)
{
    Case k;
    const char *nm = d ? "D2" : "D1";
    k.key = std::string("Mount ") + nm;
    k.a = d ? "/usb0/c.dsk" : "/usb0/a.dsk";
    k.b = d ? "/usb0/d.dsk" : "/usb0/b.dsk";
    k.mount = [d](Run &r, const std::string &path, char *m, int n) { return mount_op(r, "apple2.mount", { in_str("path", path.c_str()), in_int("drive", d), in_int("boot", 0) }, m, n); };
    k.eject = [d](Run &r, char *m, int n) { return mount_op(r, "apple2.eject", { in_int("drive", d) }, m, n); };
    return k;
}
static Case apple_unit(int u)
{
    Case k;
    k.key = u ? "Mount S7.2" : "Mount S7.1";
    k.a = u ? "/usb0/g.hdv" : "/usb0/h.hdv";
    k.b = u ? "/usb0/i.hdv" : "/usb0/j.hdv";
    k.mount = [u](Run &r, const std::string &path, char *m, int n) { return mount_op(r, "apple2.hdd_mount", { in_str("path", path.c_str()), in_int("unit", u), in_int("boot", 0) }, m, n); };
    k.eject = [u](Run &r, char *m, int n) { return mount_op(r, "apple2.hdd_eject", { in_int("unit", u) }, m, n); };
    return k;
}
#endif
#ifdef TEST_APP_ATARI800
static Case atari_drive(int d)
{
    Case k;
    k.key = std::string("Mount D") + std::to_string(d + 1);
    k.a = "/usb0/a" + std::to_string(d) + ".atr";
    k.b = "/usb0/b" + std::to_string(d) + ".atr";
    k.mount = [d](Run &r, const std::string &path, char *m, int n) { return mount_op(r, "atari800.mount", { in_str("path", path.c_str()), in_int("drive", d), in_int("boot", 0) }, m, n); };
    k.eject = [d](Run &r, char *m, int n) { return mount_op(r, "atari800.eject", { in_int("drive", d) }, m, n); };
    return k;
}
#endif
#ifdef TEST_APP_CPC          // the CPC's two drives
static Case cpc_drive(int d)
{
    Case k;
    k.key = d ? "Mount B" : "Mount A";
    k.drops = true;
    k.a = d ? "/usb0/c.dsk" : "/usb0/a.dsk";
    k.b = d ? "/usb0/d.dsk" : "/usb0/b.dsk";
    k.mount = [d](Run &r, const std::string &path, char *m, int n) { return mount_op(r, "cpc.mount", { in_str("path", path.c_str()), in_int("drive", d), in_int("boot", 0) }, m, n); };
    k.eject = [d](Run &r, char *m, int n) { return mount_op(r, "cpc.eject", { in_int("drive", d) }, m, n); };
    return k;
}
#endif
#ifndef TEST_APP_VC20          // the tape case is the shared app's and the ZX Spectrum's and the CPC's apps'
static Case tape_case(const char *pfx, const char *ext)
{
    Case k;
    k.key = "Mount Tape";
    k.drops = true;
    k.a = std::string("/usb0/a.") + ext;
    k.b = std::string("/usb0/b.") + ext;
    std::string ins = std::string(pfx) + ".tape_insert", ej = std::string(pfx) + ".tape_eject";
    k.mount = [ins](Run &r, const std::string &path, char *m, int n) { return mount_op(r, ins.c_str(), { in_str("path", path.c_str()), in_int("play", 0) }, m, n); };
    k.eject = [ej](Run &r, char *m, int n) { return mount_op(r, ej.c_str(), {}, m, n); };
    return k;
}
#endif
#ifndef MME_NO_DRIVE          // the Spectrum's app has no drive: no disk image, no 1541 library
static Case cbm_drive()
{
    Case k;
    k.key = "Mount Drive A";
    k.drops = true;
    k.a = "/usb0/a.d64";
    k.b = "/usb0/b.d64";
    k.mount = [](Run &r, const std::string &path, char *m, int n) { return mount_op(r, "drive.mount", { in_str("slot", "iec:drive_a"), in_str("path", path.c_str()), in_str("mode", "rw") }, m, n); };
    k.eject = [](Run &r, char *m, int n) { return mount_op(r, "drive.eject", { in_str("slot", "iec:drive_a") }, m, n); };
    return k;
}
#endif
#if !defined(MME_OWN_APP) || defined(TEST_APP_NES)          // the game cores: the shared app's SNES and Neo Geo, the NES's app
static Case game_case(const char *run_op, const char *a, const char *b)
{
    Case k;
    k.key = "Mount Cartridge";
    k.drops = true;
    k.a = a;
    k.b = b;
    std::string op = run_op;
    k.mount = [op](Run &r, const std::string &path, char *m, int n) { return mount_op(r, op.c_str(), { in_str("path", path.c_str()) }, m, n); };
    return k;                                            // no eject operation: the slot's own
}
#endif

static std::vector<Prof> profiles()
{
    std::vector<Prof> v;
#ifndef MME_OWN_APP          // stands on the shared app's cores; the VIC-20's app is its own build (run.sh vc20app) with its core only
    {   // ---- Apple IIe: two floppies and two slot 7 volumes
        Prof p;
        p.name = "apple2"; p.cfg = "/usb0/cores/apple2/apple2.cfg"; p.base = &apple2_desc;
        p.make_io = [] { GIo *io = new GIo; io->regs[A2_CAPS2] = A2_CAPS2_BITS; return (CoreIo *)io; };
        p.world = [](FakeFs &fs) {
            for (const char *n : { "a", "b", "c", "d" }) fs.put(std::string("/usb0/") + n + ".dsk", Bytes(143360, (uint8_t)n[0]));
            for (const char *n : { "h", "g", "i", "j" }) fs.put(std::string("/usb0/") + n + ".hdv", patt((uint32_t)n[0], 512 * 64));
        };
        p.prep = [](Run &r) { a2_state(&r.c)->have_diskrom = true; };
        p.cases = { apple_drive(0), apple_drive(1), apple_unit(0), apple_unit(1) };
        p.watch = { { [](CoreIo *io) { return (uint32_t)(io->rd(A2_HDD_CTL) & 3); }, 3, "both slot 7 units mounted at the release" } };
        v.push_back(p);
    }
#endif
#ifdef TEST_APP_ATARI800
    {   // ---- Atari 800 XL: D1-D4 and the cartridge
        Prof p;
        p.name = "atari800"; p.cfg = "/usb0/cores/atari800/atari800.cfg"; p.base = &atari800_desc;
        p.make_io = [] { return (CoreIo *)new GIo; };
        p.world = [](FakeFs &fs) {
            for (int d = 0; d < 4; d++) {
                fs.put("/usb0/a" + std::to_string(d) + ".atr", atr(80, (uint32_t)d + 1));
                fs.put("/usb0/b" + std::to_string(d) + ".atr", atr(40, (uint32_t)d + 9));
            }
            fs.put("/usb0/g.rom", patt(1, 8192));
            fs.put("/usb0/h.rom", patt(2, 16384));
            fs.put("/usb0/x.xex", Bytes{ 0xFF, 0xFF, 0x00, 0x06, 0x02, 0x06, 0xA9, 0x01, 0x60 });
        };
        p.cases = { atari_drive(0), atari_drive(1), atari_drive(2), atari_drive(3) };
        Case cart;
        cart.key = "Mount Cartridge";
        cart.a = "/usb0/g.rom";
        cart.b = "/usb0/h.rom";
        cart.mount = [](Run &r, const std::string &path, char *m, int n) { return mount_op(r, "atari800.insert_cart", { in_str("path", path.c_str()) }, m, n); };
        cart.eject = [](Run &r, char *m, int n) { return mount_op(r, "atari800.remove_cart", {}, m, n); };
        p.cases.push_back(cart);
        p.watch = { { [](CoreIo *io) { return (uint32_t)io->rd(A8_CART1); }, 0x01, "the 8 KB cartridge's type written while the core is held" } };
        v.push_back(p);
    }
#endif
#ifdef TEST_APP_CPC
    {   // ---- Amstrad CPC: A, B, the tape
        Prof p;
        p.name = "cpc"; p.cfg = "/usb0/cores/cpc/cpc.cfg"; p.base = &cpc_desc;
        p.make_io = [] { GIo *io = new GIo; io->regs[0x0A4] = 2 << 2; return (CoreIo *)io; };     // CPC_STATUS: the 6128 runs
        p.world = [](FakeFs &fs) {
            for (const char *n : { "a", "b", "c", "d" }) fs.put(std::string("/usb0/") + n + ".dsk", dsk((uint32_t)n[0]));
            fs.put("/usb0/a.cdt", cdt());
            fs.put("/usb0/b.cdt", cdt());
        };
        p.prep = [](Run &r) { Bytes rom(16384, 0x5A); OpIn in = in_blob("data", rom.data(), (uint32_t)rom.size()); OpResult res; ops_call(&r.c, "cpc.put_amsdos", &in, 1, &res); };
        p.cases = { cpc_drive(0), cpc_drive(1), tape_case("cpc", "cdt") };
        v.push_back(p);
    }
#endif
#ifdef TEST_APP_NES
    {   // ---- NES: the game is the cartridge
        Prof p;
        p.name = "nes"; p.cfg = "/usb0/cores/nes/nes.cfg"; p.base = &nes_desc; p.game = true;
        p.make_io = [] { return (CoreIo *)new GIo; };
        p.world = [](FakeFs &fs) { fs.put("/usb0/a.nes", nes_file(1)); fs.put("/usb0/b.nes", nes_file(2)); };
        p.cases = { game_case("nes.run", "/usb0/a.nes", "/usb0/b.nes") };
        p.watch = { { [](CoreIo *io) { return (uint32_t)io->rd(NES_INES + 4); }, 1, "the iNES header in the core before the release" } };
        v.push_back(p);
    }
#endif
#ifdef TEST_APP_ZX
    {   // ---- ZX Spectrum: the tape
        Prof p;
        p.name = "zx"; p.cfg = "/usb0/cores/zx/zx.cfg"; p.base = &zx_desc;
        p.make_io = [] { return (CoreIo *)new GIo; };
        p.world = [](FakeFs &fs) { fs.put("/usb0/a.tzx", cdt()); fs.put("/usb0/b.tzx", cdt()); };
        p.cases = { tape_case("zx", "tzx") };
        v.push_back(p);
    }
#endif
#if !defined(MME_NO_DRIVE) && defined(MME_OWN_APP)   // the Commodore cores with the drive library are apps of their own: the shared app has none left
#ifdef TEST_APP_VC20
    for (int k = 0; k < 1; k++) {
#elif defined(TEST_APP_C264)
    for (int k = 2; k < 3; k++) {
#elif defined(TEST_APP_C128)
    for (int k = 1; k < 2; k++) {
#endif
    // ---- the Commodore cores with the drive library: the VC-20, the C128, the C264 (with the cartridge image)
        Prof p;
#ifdef TEST_APP_VC20
        const CoreDesc *d = &vc20_desc;
        p.name = "vc20";
#elif defined(TEST_APP_C264)
        const CoreDesc *d = &c264_desc;
        p.name = "c264";
#elif defined(TEST_APP_C128)
        const CoreDesc *d = &c128_desc;
        p.name = "c128";
#endif
        p.cfg = std::string("/usb0/cores/") + p.name + "/" + p.name + ".cfg";
        p.base = d;
        p.make_io = [] { return (CoreIo *)new GIo; };
        p.world = [](FakeFs &fs) {
            fs.put("/usb0/a.d64", d64("A")); fs.put("/usb0/b.d64", d64("B"));
            fs.put("/usb0/a.bin", patt(1, 16384)); fs.put("/usb0/b.bin", patt(2, 32768));
        };
        p.cases = { cbm_drive() };
        if (k == 2) {
#ifdef TEST_APP_C264                   // the 264's cartridge image: its app's
            Case cart;
            cart.key = "Mount Cartridge";
            cart.a = "/usb0/a.bin";
            cart.b = "/usb0/b.bin";
            cart.mount = [](Run &r, const std::string &path, char *m, int n) { return mount_op(r, "c264.cart_load", { in_str("path", path.c_str()) }, m, n); };
            cart.eject = [](Run &r, char *m, int n) { return mount_op(r, "c264.cart_eject", {}, m, n); };
            p.cases.push_back(cart);
            p.watch = { { [](CoreIo *io) { return (uint32_t)io->rd(C264_CART); }, 1, "the 16 KB cartridge image's bit set in the core before the release" } };
#endif
        }
        v.push_back(p);
    }
#endif
#ifndef MME_OWN_APP          // stands on the shared app's cores; the VIC-20's app is its own build (run.sh vc20app) with its core only
    {   // ---- the cartridge port of the C64 library (c64_cart.cc), on the C64 model
        Prof p;
        p.name = "c64cart"; p.cfg = "/usb0/cores/cartport/cartport.cfg"; p.base = &g_cart_core; p.cart = true;
        p.make_io = [] { return (CoreIo *)new C64Model; };
        p.world = [](FakeFs &fs) { fs.put("/usb0/a.crt", crt8(0x10)); fs.put("/usb0/b.crt", crt8(0x20)); };
        Case cart;
        cart.key = "Mount Cartridge";
        cart.drops = true;
        cart.a = "/usb0/a.crt";
        cart.b = "/usb0/b.crt";
        cart.mount = [](Run &r, const std::string &path, char *m, int n) { ((C64Model *)r.io.get())->stop(true); int rc = r.c.desc->mount_slots->mount(&r.c, 0, path.c_str(), m, n); ((C64Model *)r.io.get())->stop(false); return rc; };
        cart.eject = [](Run &r, char *m, int n) { ((C64Model *)r.io.get())->stop(true); int rc = r.c.desc->mount_slots->eject(&r.c, 0, m, n); ((C64Model *)r.io.get())->stop(false); return rc; };
        p.cases.push_back(cart);
        p.watch = { { [](CoreIo *io) { return (uint32_t)io->cart_rd(C64_CARTREG_TYPE); }, 0x41, "the cartridge type set before the release" } };
        v.push_back(p);
    }
    {   // ---- SNES, Neo Geo: the game is the cartridge (the NES's is the unit nes_mounts)
        Prof s;
        s.name = "snes"; s.cfg = "/usb0/cores/snes/snes.cfg"; s.base = &snes_desc; s.game = true;
        s.make_io = [] { return (CoreIo *)new GIo; };
        s.world = [](FakeFs &fs) { fs.put("/usb0/a.sfc", snes_file(1)); fs.put("/usb0/b.sfc", snes_file(2)); };
        s.prep = [](Run &r) { snprintf(r.c.cor_path, sizeof(r.c.cor_path), "/usb0/cores/snes/snes.cor"); };
        s.cases = { game_case("snes.run", "/usb0/a.sfc", "/usb0/b.sfc") };
        v.push_back(s);
        Prof n;
        n.name = "neogeo"; n.cfg = "/usb0/cores/neogeo/neogeo.cfg"; n.base = &neogeo_desc; n.game = true;
        n.make_io = [] { return (CoreIo *)new NeoIo; };
        n.world = [](FakeFs &fs) { fs.put("/usb0/a.neo", neo_file(1)); fs.put("/usb0/b.neo", neo_file(2)); };
        n.prep = [](Run &r) { snprintf(r.c.cor_path, sizeof(r.c.cor_path), "/usb0/cores/neogeo/neogeo.cor"); };
        n.cases = { game_case("neogeo.run", "/usb0/a.neo", "/usb0/b.neo") };
        v.push_back(n);
    }
#endif
    return v;
}

// ---- the menu: the item in F5 (Storage) and the two rows in the media tab (F5) -------------------------------------------------------------------------
static bool tree_has(const MenuTree &t, const char *area, EntryKind kind, const char *label)
{
    for (int a = 0; a < t.nareas; a++) {
        if (strcmp(t.a[a].key, area)) continue;
        for (int b = 0; b < t.a[a].ntabs; b++) {
            const MenuTab &tb = t.a[a].tabs[b];
            for (int i = tb.first; i < tb.first + tb.count; i++) {
                if (t.e[i].kind == kind && !strcmp(t.e[i].label, label)) return true;
            }
        }
    }
    return false;
}

static void menu_rows(const Prof &p)
{
    FakeFs fs;
    Run r(fs, p);
    static MenuTree t;
    menu_build(&r.desc, &t, true, false, r.c.merge);
    bool persist = false;
    for (int i = 0; i < r.desc.nsettings; i++) persist |= persist_is_generic(r.desc.settings[i]);
    CHECK(persist);                                                          // the item is a setting of the core
    // the row: the generic item stands on F7 (the app's Storage settings, T212 D1/D6) in every core (the application's own items follow the core's settings, as the board builds them); its label is its key
    CHECK(tree_has(t, "F7", EK_SETTING, PERSIST_KEY));
    CHECK(!tree_has(t, "F6", EK_SETTING, PERSIST_KEY) && !tree_has(t, "F8", EK_SETTING, PERSIST_KEY));
    // T205 D1: the configs are on Home (the Summary tab is gone), in every core
    CHECK(tree_has(t, "F1", EK_SAVE_SET, "Save as config..."));
    CHECK(tree_has(t, "F1", EK_LOAD_SET, "Load config..."));
    CHECK(!tree_has(t, "F5", EK_SAVE_SET, "Save as config..."));
    CHECK(!tree_has(t, "F8", EK_SAVE_SET, "Save as config..."));
}

// ---- S2 -------------------------------------------------------------------------------------------------------------------------------------
static void s2_persist(const Prof &p)
{
    char msg[240];
    FakeFs fs;
    p.world(fs);
    {
        Run a(fs, p);
        CHECK_EQ(a.set(PERSIST_KEY, "Yes"), OPE_OK);
        BootResult br = a.boot();
        CHECK_EQ(br, p.game ? BOOT_WAITING_PROGRAM : BOOT_RUNNING);
        for (const Case &k : p.cases) {
            CHECK_EQ(k.mount(a, k.a, msg, sizeof(msg)), 0);
            if (strstr(msg, "forced")) break;
        }
        std::string cfg = a.cfg();
        CHECK(has(cfg, "Persist medium mounts=Yes"));
        for (const Case &k : p.cases) CHECK(has(cfg, k.key + "=" + k.pre + k.a));
        CHECK_EQ(count(cfg, "Mount "), (int)p.cases.size());
        // a replacement is stored as it happens (D1: one store, the new medium); the slot reads the new path
        for (size_t i = 0; i < p.cases.size(); i++) {
            const Case &k = p.cases[i];
            CHECK_EQ(k.mount(a, k.b, msg, sizeof(msg)), 0);
            std::string cfg2 = a.cfg();
            CHECK(has(cfg2, k.key + "=" + k.pre + k.b));
            CHECK(!has(cfg2, k.key + "=" + k.pre + k.a));
        }
        CHECK_EQ(count(a.cfg(), "Mount "), (int)p.cases.size());
        // an eject is stored (the key goes) and the next mount of `a` brings it back
        for (size_t i = 0; i < p.cases.size(); i++) {
            const Case &k = p.cases[i];
            int rc = k.eject ? k.eject(a, msg, sizeof(msg)) : p.base->mount_slots->eject(&a.c, (int)i, msg, sizeof(msg));
            CHECK_EQ(rc, 0);
            CHECK(!has(a.cfg(), k.key + "="));
            CHECK_STR(a.spec((int)i).c_str(), "(empty)");
            CHECK_EQ(k.mount(a, k.a, msg, sizeof(msg)), 0);
            CHECK(has(a.cfg(), k.key + "=" + k.pre + k.a));
        }
    }
    {   // the next start: nothing mounted before the start; mounted again when the core is released
        Run b(fs, p);
        for (size_t i = 0; i < p.cases.size(); i++) CHECK_STR(b.spec((int)i).c_str(), "(empty)");
        BootResult br = b.boot();
        CHECK_EQ(br, BOOT_RUNNING);
        CHECK_EQ(g_releases, 1);
        for (size_t i = 0; i < p.cases.size(); i++) {
            const Case &k = p.cases[i];
            std::string want = k.pre + k.a;
            CHECK_STR(b.spec((int)i).c_str(), want.c_str());
            CHECK_STR(i < g_rel_specs.size() ? g_rel_specs[i].c_str() : "(none)", want.c_str());          // there at the first cycle: mounted BEFORE the release
        }
        for (size_t i = 0; i < p.watch.size(); i++) {
            CHECK_EQ(i < g_rel_watch.size() ? g_rel_watch[i] : 0xFFFFu, p.watch[i].want);
            if (i < g_rel_watch.size() && g_rel_watch[i] != p.watch[i].want) printf("  (%s)\n", p.watch[i].what);
        }
        CHECK(!b.c.notice_pending);
        CHECK(b.c.started);
    }
    {   // persist off (the default): nothing stored, nothing mounted
        FakeFs fs2;
        p.world(fs2);
        {
            Run a(fs2, p);
            a.boot();
            for (const Case &k : p.cases) CHECK_EQ(k.mount(a, k.a, msg, sizeof(msg)), 0);
            CHECK(!has(a.cfg(), "Mount "));
        }
        {
            Run b(fs2, p);
            BootResult br = b.boot();
            CHECK_EQ(br, p.game ? BOOT_WAITING_PROGRAM : BOOT_RUNNING);
            for (size_t i = 0; i < p.cases.size(); i++) CHECK_STR(b.spec((int)i).c_str(), "(empty)");
            // switched on later: what is mounted now is stored at once; switched off: gone again
            for (const Case &k : p.cases) CHECK_EQ(k.mount(b, k.a, msg, sizeof(msg)), 0);
            CHECK_EQ(b.set(PERSIST_KEY, "Yes"), OPE_OK);
            CHECK_EQ(count(b.cfg(), "Mount "), (int)p.cases.size());
            CHECK_EQ(b.set(PERSIST_KEY, "No"), OPE_OK);
            CHECK(!has(b.cfg(), "Mount "));
            CHECK(has(b.cfg(), "Persist medium mounts=No"));
        }
        {   // keys in the file with the item off (an older file, an edit): they do not act
            std::string txt = std::string("[") + p.base->store + "]\n";
            for (const Case &k : p.cases) txt += k.key + "=" + k.pre + k.a + "\n";
            fs2.put_text(p.cfg, txt);
            Run c(fs2, p);
            BootResult br = c.boot();
            CHECK_EQ(br, p.game ? BOOT_WAITING_PROGRAM : BOOT_RUNNING);
            for (size_t i = 0; i < p.cases.size(); i++) CHECK_STR(c.spec((int)i).c_str(), "(empty)");
            CHECK(!c.c.notice_pending);
        }
    }
    printf("S2 %s: %d slot(s) stored, restored before the release, replaced, ejected\n", p.name.c_str(), (int)p.cases.size());
}

// ---- S3 -------------------------------------------------------------------------------------------------------------------------------------
static void s3_missing(const Prof &p)
{
    char msg[240];
    FakeFs fs;
    p.world(fs);
    {
        Run a(fs, p);
        a.set(PERSIST_KEY, "Yes");
        a.boot();
        for (const Case &k : p.cases) CHECK_EQ(k.mount(a, k.a, msg, sizeof(msg)), 0);
    }
    for (const Case &k : p.cases) fs.files.erase(lower(k.a));                // every stored medium is gone
    {
        Run b(fs, p);
        BootResult br = b.boot();
        CHECK_EQ(br, p.game ? BOOT_WAITING_PROGRAM : BOOT_RUNNING);
        CHECK_EQ(g_releases, p.game ? 0 : 1);                               // the core started (a game core waits for its program, as at a first start)
        for (size_t i = 0; i < p.cases.size(); i++) CHECK_STR(b.spec((int)i).c_str(), "(empty)");
        CHECK(b.c.notice_pending);                                           // the note, shown when the menu opens
        CHECK(strstr(b.c.notice, "Stored media not mounted") != 0);
        for (const Case &k : p.cases) CHECK(strstr(b.c.notice, k.key.c_str() + 6) != 0);        // it names the slots
        std::string cfg = b.cfg();                                           // nothing was forgotten: the stick may come back
        for (const Case &k : p.cases) CHECK(has(cfg, k.key + "=" + k.pre + k.a));
        // an eject of an empty slot is the person's word: its key goes, the others stay; a mount by hand in one slot leaves the other keys
        for (size_t i = 0; i < p.cases.size(); i++) {
            const Case &k = p.cases[i];
            int rc = k.eject ? k.eject(b, msg, sizeof(msg)) : p.base->mount_slots->eject(&b.c, (int)i, msg, sizeof(msg));
            CHECK_EQ(rc, 0);
            CHECK(!has(b.cfg(), k.key + "="));
            for (size_t j = i + 1; j < p.cases.size(); j++) CHECK(has(b.cfg(), p.cases[j].key + "=" + p.cases[j].pre + p.cases[j].a));
        }
        CHECK(!has(b.cfg(), "Mount "));
        const Case &k0 = p.cases[0];
        CHECK_EQ(k0.mount(b, k0.b, msg, sizeof(msg)), 0);
        CHECK(has(b.cfg(), k0.key + "=" + k0.pre + k0.b));
    }
    printf("S3 %s: every medium gone: the slots empty with a note, %s, the keys kept\n", p.name.c_str(), p.game ? "the start waits for a program" : "the core released once");
}

// a replacement that fails after the old medium has gone leaves the slot empty: that is stored too (the file is read to the end after the old one is let go)
static void s2_failed_replace(const Prof &p)
{
    char msg[240];
    for (size_t i = 0; i < p.cases.size(); i++) {
        const Case &k = p.cases[i];
        if (!k.drops) continue;
        FakeFs fs;
        p.world(fs);
        Run a(fs, p);
        a.set(PERSIST_KEY, "Yes");
        a.boot();
        bool found = false;
        for (int budget = 0; budget < 24 && !found; budget++) {
            fs.read_budget = -1;
            CHECK_EQ(k.mount(a, k.a, msg, sizeof(msg)), 0);
            CHECK(has(a.cfg(), k.key + "=" + k.pre + k.a));
            fs.read_budget = budget;
            int rc = k.mount(a, k.b, msg, sizeof(msg));
            fs.read_budget = -1;
            if (rc != 0 && a.spec((int)i) == "(empty)") {
                found = true;
                CHECK(!has(a.cfg(), k.key + "="));                          // the slot is empty and the next start does not bring the old medium back
            }
        }
        CHECK(found);
        printf("S2 %s: a replacement that failed after the old medium went leaves %s empty and stored as such\n", p.name.c_str(), k.key.c_str() + 6);
    }
}

// ---- S8 -------------------------------------------------------------------------------------------------------------------------------------
static void s8_sets(const Prof &p)
{
    char msg[240];
    FakeFs fs;
    p.world(fs);
    std::string setdir = p.cfg.substr(0, p.cfg.rfind('/')) + "/configs";
    Run a(fs, p);
    a.set(PERSIST_KEY, "Yes");
    a.boot();
    // the set: every slot mounted but the last (it stays empty in the set)
    size_t nset = p.cases.size() > 1 ? p.cases.size() - 1 : p.cases.size();
    for (size_t i = 0; i < nset; i++) CHECK_EQ(p.cases[i].mount(a, p.cases[i].a, msg, sizeof(msg)), 0);
    OpIn nm = in_str("name", "Set One");
    CHECK_EQ(a.op("config.save", &nm, 1, msg, sizeof(msg)), OPE_OK);
    std::string set = fs.text(setdir + "/Set One.cfg");
    CHECK_EQ(count(set, "Mount "), (int)nset);
    for (size_t i = 0; i < nset; i++) CHECK(has(set, p.cases[i].key + "=" + p.cases[i].pre + p.cases[i].a));
    CHECK_STR(ops_state(&a.c, "config.count"), "1");
    // change every slot
    for (size_t i = 0; i < p.cases.size(); i++) CHECK_EQ(p.cases[i].mount(a, p.cases[i].b, msg, sizeof(msg)), 0);
    int releases = g_releases;
    CHECK_EQ(a.op("config.load", &nm, 1, msg, sizeof(msg)), OPE_OK);
    for (size_t i = 0; i < p.cases.size(); i++) {
        std::string want = i < nset ? p.cases[i].pre + p.cases[i].a : std::string("(empty)");
        CHECK_STR(a.spec((int)i).c_str(), want.c_str());                     // as saved; what the set leaves empty is unmounted
    }
    if (p.game && nset == p.cases.size()) CHECK(strstr(msg, "core reset") != 0);
    CHECK_EQ(g_releases - releases, p.game && nset < p.cases.size() ? 0 : 1);   // the core was reset, once
    std::string cfg = fs.text(setdir + "/Set One.cfg");                      // T198 D5: the run keeps its mounts in the config it loaded; the core's own .cfg is not written
    for (size_t i = 0; i < p.cases.size(); i++) {
        if (i < nset) CHECK(has(cfg, p.cases[i].key + "=" + p.cases[i].pre + p.cases[i].a)); else CHECK(!has(cfg, p.cases[i].key + "="));
    }
    // a set naming a medium that is not there: that slot is empty with a note, the rest is mounted, the core resets
    if (nset > 1) {
        nm = in_str("name", "Set Two");                                       // the run is in "Set One" (D5): its mounts go into that file, so the state to come back to is another name
        CHECK_EQ(a.op("config.save", &nm, 1, msg, sizeof(msg)), OPE_OK);
        for (size_t i = 0; i < p.cases.size(); i++) CHECK_EQ(p.cases[i].mount(a, p.cases[i].b, msg, sizeof(msg)), 0);
        fs.files.erase(lower(p.cases[0].a));
        releases = g_releases;
        CHECK_EQ(a.op("config.load", &nm, 1, msg, sizeof(msg)), OPE_OK);
        CHECK(strstr(msg, "Left empty") && strstr(msg, p.cases[0].key.c_str() + 6));
        CHECK_STR(a.spec(0).c_str(), "(empty)");                              // not what it held before (b)
        for (size_t i = 1; i < nset; i++) CHECK_STR(a.spec((int)i).c_str(), (p.cases[i].pre + p.cases[i].a).c_str());
        CHECK_EQ(g_releases - releases, 1);
        fs.put(p.cases[0].a, Bytes());                                        // (the file is not needed again; the next part makes its own world)
    }
    // no such set
    OpIn none = in_str("name", "Nope");
    CHECK_EQ(a.op("config.load", &none, 1, msg, sizeof(msg)), OPE_FAILED);
    CHECK(strstr(msg, "Nope") != 0);
    printf("S8 %s: a set of %d medi%s saved, loaded as saved with %d reset(s), a missing medium left empty\n", p.name.c_str(), (int)nset, nset == 1 ? "um" : "a", 1);
}

// a write-back that fails: nothing is changed and the core is not reset (the engines' hook is the descriptor's media_flush)
static void s8_writeback(const Prof &p)
{
    char msg[240];
    FakeFs fs;
    p.world(fs);
    Run a(fs, p, true);
    a.set(PERSIST_KEY, "Yes");
    a.boot();
    for (const Case &k : p.cases) CHECK_EQ(k.mount(a, k.a, msg, sizeof(msg)), 0);
    OpIn nm = in_str("name", "Set Two");
    CHECK_EQ(a.op("config.save", &nm, 1, msg, sizeof(msg)), OPE_OK);
    for (const Case &k : p.cases) CHECK_EQ(k.mount(a, k.b, msg, sizeof(msg)), 0);
    int releases = g_releases;
    g_flush_fail = true;
    g_flush_calls = 0;
    CHECK_EQ(a.op("config.load", &nm, 1, msg, sizeof(msg)), OPE_FAILED);
    g_flush_fail = false;
    CHECK(g_flush_calls >= 1);                                               // the engines' write-back was asked first
    CHECK(strstr(msg, "forced") != 0);
    for (size_t i = 0; i < p.cases.size(); i++) CHECK_STR(a.spec((int)i).c_str(), (p.cases[i].pre + p.cases[i].b).c_str());   // nothing changed
    CHECK_EQ(g_releases - releases, 0);                                      // and no reset
    CHECK_EQ(a.op("config.load", &nm, 1, msg, sizeof(msg)), OPE_OK);       // the medium is back: the same call goes through
    for (size_t i = 0; i < p.cases.size(); i++) CHECK_STR(a.spec((int)i).c_str(), (p.cases[i].pre + p.cases[i].a).c_str());
    printf("S8 %s: a refused write-back keeps every slot and does not reset\n", p.name.c_str());
}

// ---- what is particular to an engine ---------------------------------------------------------------------------------------------------------
static const Prof &prof(const std::vector<Prof> &v, const char *name)
{
    for (const Prof &p : v) if (p.name == name) return p;
    printf("FAIL no profile %s\n", name);
    exit(1);
}

// the Atari's .xex boot disk lives in D1 under its own word; an ATR mounted over it takes it away again
#ifdef TEST_APP_ATARI800
static void atari_xex(const Prof &p)
{
    char msg[240];
    FakeFs fs;
    p.world(fs);
    {
        Run a(fs, p);
        a.set(PERSIST_KEY, "Yes");
        a.boot();
        OpIn in = in_str("path", "/usb0/x.xex");
        CHECK_EQ(a.op("atari800.run_xex", &in, 1, msg, sizeof(msg)), OPE_OK);
        CHECK_STR(a.spec(0).c_str(), "xex /usb0/x.xex");
        CHECK(has(a.cfg(), "Mount D1=xex /usb0/x.xex"));
    }
    {
        Run b(fs, p);
        b.boot();
        CHECK_EQ(g_releases, 1);
        CHECK_STR(b.spec(0).c_str(), "xex /usb0/x.xex");
        CHECK_STR(g_rel_specs.empty() ? "(none)" : g_rel_specs[0].c_str(), "xex /usb0/x.xex");
        CHECK_EQ(p.cases[0].mount(b, "/usb0/a0.atr", msg, sizeof(msg)), 0);          // an ATR over it
        CHECK(has(b.cfg(), "Mount D1=/usb0/a0.atr") && !has(b.cfg(), "xex"));
    }
    printf("Atari: the .xex boot disk is stored as \"xex <path>\" and restored into D1\n");
}

// SvcCtx::mount_notify marks a slot (CoreCtx::mounts_dirty[i] = engine slot i); the pass stores it once the start has mounted what the .cfg stores
static void notify_pass(const Prof &p)
{
    char msg[240];
    FakeFs fs;
    p.world(fs);
    Run a(fs, p);
    a.set(PERSIST_KEY, "Yes");
    a.c.mount_changed = 0;                                                       // a change that no engine call reports: only the mark does
    a.boot();
    CHECK_EQ(p.cases[0].mount(a, "/usb0/a0.atr", msg, sizeof(msg)), 0);
    CHECK(!has(a.cfg(), "Mount D1=/usb0/a0.atr"));
    mounts_pass(&a.c);
    CHECK(!has(a.cfg(), "Mount D1=/usb0/a0.atr"));                               // nothing marked: nothing stored
    a.c.mounts_ready = false;
    a.c.mounts_dirty[0] = 1;
    mounts_pass(&a.c);
    CHECK(a.c.mounts_dirty[0] == 1 && !has(a.cfg(), "Mount D1=/usb0/a0.atr"));     // the start is not done: the mark waits
    a.c.mounts_ready = true;
    mounts_pass(&a.c);
    CHECK(a.c.mounts_dirty[0] == 0 && has(a.cfg(), "Mount D1=/usb0/a0.atr"));
    printf("Atari: a slot marked by mount_notify is stored on the next pass\n");
}

// the drive library's disk is read/write, read only or unlinked: the word in front of the path
#endif
#ifndef MME_NO_DRIVE          // the Spectrum's app has no drive
static void drive_modes(const Prof &p)
{
    char msg[240];
    for (const char *mode : { "ro", "unlinked" }) {
        FakeFs fs;
        p.world(fs);
        std::string want = std::string(mode) + " /usb0/a.d64";
        {
            Run a(fs, p);
            a.set(PERSIST_KEY, "Yes");
            a.boot();
            CHECK_EQ(a.op("drive.mount", std::vector<OpIn>{ in_str("slot", "iec:drive_a"), in_str("path", "/usb0/a.d64"), in_str("mode", mode) }.data(), 3, msg, sizeof(msg)), OPE_OK);
            CHECK_STR(a.spec(0).c_str(), want.c_str());
            CHECK(has(a.cfg(), "Mount Drive A=" + want));
        }
        {
            Run b(fs, p);
            b.boot();
            CHECK_EQ(g_releases, 1);
            CHECK_STR(b.spec(0).c_str(), want.c_str());
            CHECK_STR(g_rel_specs.empty() ? "(none)" : g_rel_specs[0].c_str(), want.c_str());
            CbmDrive *d = (CbmDrive *)b.c.drive;
            CHECK(d != 0);
            CHECK_EQ(d ? d->mount_mode() : -1, !strcmp(mode, "ro") ? MM_RO : MM_UNLINKED);
        }
    }
    printf("%s: read only and unlinked disks come back in their mode\n", p.name.c_str());
}
#endif

// the CPC 464 has no drive: stored disks stay unmounted with a note, the tape comes back
#ifdef TEST_APP_CPC
static void cpc_464(const Prof &p)
{
    char msg[240];
    FakeFs fs;
    p.world(fs);
    {
        Run a(fs, p);
        a.set(PERSIST_KEY, "Yes");
        a.boot();
        for (const Case &k : p.cases) CHECK_EQ(k.mount(a, k.a, msg, sizeof(msg)), 0);
    }
    {
        Run b(fs, p);
        CHECK_EQ(b.set("Model", "CPC 464"), OPE_OK);
        b.boot();
        CHECK_EQ(g_releases, 1);
        CHECK_STR(b.spec(0).c_str(), "(empty)");
        CHECK_STR(b.spec(1).c_str(), "(empty)");
        CHECK_STR(b.spec(2).c_str(), "/usb0/a.cdt");
        CHECK(b.c.notice_pending && strstr(b.c.notice, "A: The CPC 464 has no disk drive") != 0 && strstr(b.c.notice, "B: The CPC 464 has no disk drive") != 0);
        CHECK(strstr(b.c.notice, "Tape") == 0);
        CHECK(has(b.cfg(), "Mount A=/usb0/a.dsk"));                              // kept for the 6128 that may come back
    }
    printf("cpc: a 464 restores the tape and leaves A and B empty with a note\n");
}

#endif
#ifdef TEST_APP_C264
// the C264 cartridge is a Plus/4 thing: restoring one sets the model, applied before the release
static void c264_model(const Prof &p)
{
    char msg[240];
    FakeFs fs;
    p.world(fs);
    {
        Run a(fs, p);
        a.set(PERSIST_KEY, "Yes");
        a.boot();
        CHECK_EQ(p.cases[1].mount(a, p.cases[1].a, msg, sizeof(msg)), 0);
    }
    {
        Run b(fs, p);
        CHECK_EQ(b.set("Model (at next reset)", "C16 / C116"), OPE_OK);
        int mi = b.st->find("Model (at next reset)");
        CHECK_EQ(b.st->value(mi), 0);
        b.boot();
        CHECK_STR(b.spec(1).c_str(), "/usb0/a.bin");
        CHECK_EQ(b.st->value(mi), 1);
    }
    printf("c264: the restored cartridge sets the Plus/4 model\n");
}

#endif
#ifndef MME_OWN_APP
// a Neo Geo ROM set: a folder and the set's name, restored through the same operation
static void neo_folder(const Prof &p)
{
    char msg[240];
    FakeFs fs;
    p.world(fs);
    fs.put("/usb0/roms/neogeo/romsets.xml", Bytes(NEO_XML, NEO_XML + strlen(NEO_XML)));
    for (const char *f : { "p1.p1", "s1.s1", "m1.m1" }) fs.put(std::string("/usb0/games/aoft/") + f, patt(7, f[0] == 'p' ? 0x10000 : f[0] == 's' ? 0x4000 : 0x8000));
    for (const char *f : { "c1.c1", "c2.c2", "v1.v1" }) fs.put(std::string("/usb0/games/aoft/") + f, patt(8, 0x20000));
    {
        Run a(fs, p);
        a.set(PERSIST_KEY, "Yes");
        a.boot();
        OpIn in = in_str("path", "/usb0/games/aoft/aoft");
        CHECK_EQ(a.op("neogeo.run_dir", &in, 1, msg, sizeof(msg)), OPE_OK);
        CHECK_STR(a.spec(0).c_str(), "folder /usb0/games/aoft/aoft");
        CHECK(has(a.cfg(), "Mount Cartridge=folder /usb0/games/aoft/aoft"));
    }
    {
        Run b(fs, p);
        BootResult br = b.boot();
        CHECK_EQ(br, BOOT_RUNNING);
        CHECK_EQ(g_releases, 1);
        CHECK_STR(b.spec(0).c_str(), "folder /usb0/games/aoft/aoft");
        CHECK_STR(g_rel_specs.empty() ? "(none)" : g_rel_specs[0].c_str(), "folder /usb0/games/aoft/aoft");
        CHECK_EQ(p.cases[0].mount(b, "/usb0/a.neo", msg, sizeof(msg)), 0);           // a .neo file over it
        CHECK(has(b.cfg(), "Mount Cartridge=/usb0/a.neo"));
    }
    printf("neogeo: a ROM set folder is stored as \"folder <folder>/<set>\" and restored\n");
}

#endif
#if !defined(MME_OWN_APP) || defined(TEST_APP_NES)
// a game core's set that leaves the cartridge empty: nothing to run, no reset, the message says so
static void game_empty_set(const Prof &p)
{
    char msg[240];
    FakeFs fs;
    p.world(fs);
    Run a(fs, p);
    a.set(PERSIST_KEY, "Yes");
    a.boot();
    OpIn empty = in_str("name", "Empty");
    CHECK_EQ(a.op("config.save", &empty, 1, msg, sizeof(msg)), OPE_OK);       // nothing mounted: an empty set
    CHECK_EQ(count(fs.text(p.cfg.substr(0, p.cfg.rfind('/')) + "/configs/Empty.cfg"), "Mount "), 0);
    CHECK_EQ(p.cases[0].mount(a, p.cases[0].a, msg, sizeof(msg)), 0);
    CHECK(a.c.program_loaded);
    int releases = g_releases;
    CHECK_EQ(a.op("config.load", &empty, 1, msg, sizeof(msg)), OPE_OK);
    CHECK(!a.c.program_loaded);
    CHECK_STR(a.spec(0).c_str(), "(empty)");
    CHECK(strstr(msg, "no program") != 0);
    CHECK_EQ(g_releases - releases, 0);
    CHECK(!has(fs.text(p.cfg.substr(0, p.cfg.rfind('/')) + "/configs/Empty.cfg"), "Mount Cartridge"));   // D5: the run is in the config now
    CHECK(has(a.cfg(), "Mount Cartridge"));                                   // D4: the core's own .cfg is as it was
    printf("%s: a set with no cartridge leaves the core waiting for a program\n", p.name.c_str());
}

#endif
int main()
{
    setvbuf(stdout, 0, _IONBF, 0);
    std::vector<Prof> profs = profiles();
    for (const Prof &p : profs) {
        menu_rows(p);
        s2_persist(p);
        s2_failed_replace(p);
        s3_missing(p);
        s8_sets(p);
        s8_writeback(p);
#if !defined(MME_OWN_APP) || defined(TEST_APP_NES)
        if (p.game) game_empty_set(p);
#endif
    }
#ifdef TEST_APP_VC20
    drive_modes(prof(profs, "vc20"));
#elif defined(TEST_APP_C264)
    drive_modes(prof(profs, "c264"));
    c264_model(prof(profs, "c264"));
#elif defined(TEST_APP_ZX)
                                                       // the Spectrum's profile has the tape only: the sets of S2 to S8 above are its checks
#elif defined(TEST_APP_CPC)
    cpc_464(prof(profs, "cpc"));
#elif defined(TEST_APP_C128)
    drive_modes(prof(profs, "c128"));
#elif defined(TEST_APP_NES)
                                                       // the NES's profile has the cartridge only: the sets of S2 to S8 above and the empty set are its checks
#elif defined(TEST_APP_ATARI800)
    atari_xex(prof(profs, "atari800"));
    notify_pass(prof(profs, "atari800"));
#else
    neo_folder(prof(profs, "neogeo"));
#endif
    DONE("test_media_mounts_engines");
}
