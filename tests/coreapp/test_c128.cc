// T113 5.5 on the generic application: the Commodore 128 descriptor (coreapp/cores/c128.cc) against a model of the core's register page E
// (byte offsets 0xE000 + off from the core window, T113 section 5.5) and of the DDR2 window. The 72 KiB system ROM image at window offset
// 0x00C00000 and the flush of the ROM line cache behind it, D4080 taken only in the hold, MON_SEL live and the pixel layer that follows it,
// the picture (640 x 400 or woven 640 x 480 by FB_LATEST b2, 2 x 2, three buffers), Freeze's held bit, the menu (1541 and 1581 pieces), and the USB keyboard's extra keys.
// The descriptor is the real one. Offsets are literal numbers on purpose: this is the bench's own reading of the table in the spec.
#include <algorithm>
#include "cbm_rig.h"
#include "drive_model.h"
#include "cbm1541.h"
#include "regset.h"
#include "sha256.h"
#include "c128_keys.h"
#include "videomode.h"
#include "picture.h"
#include "menuctl.h"
#include "keyboard.h"
#include "engines.h"
#include "rawkeys.h"
#include "hotkeys.h"
#include <utility>
#include <stdio.h>

// picture.cc calls the board's video_apply (video_stub.cc has a weak one that does nothing): this bench records the System Modes it is given
static std::vector<std::pair<int, int>> g_applied;
void video_apply(int sys, int hdmi) { g_applied.push_back({ sys, hdmi }); }

#define T_PAGE        0xE000u
#define T_MON_SEL     (T_PAGE + 0x020)
#define T_D4080       (T_PAGE + 0x021)
#define T_XKEYS       (T_PAGE + 0x028)
#define T_XSPECIAL    (T_PAGE + 0x02B)
#define T_STATUS      (T_PAGE + 0x030)
#define T_ROM_CTRL    (T_PAGE + 0x040)
#define T_ROM_BASE    (T_PAGE + 0x050)
#define T_FB_LATEST   (T_PAGE + 0x060)
#define T_FRAME_SEQ   (T_PAGE + 0x061)
#define T_FB_BASE     (T_PAGE + 0x064)
#define T_FB_STRIDE   (T_PAGE + 0x068)
#define T_ROM_BYTES   73728u
#define T_ROM_WIN     0x00C00000u
#define T_FB_ADDR     0x01D00000u
#define T_FB_STRIDE_BYTES 0x00100000u

const CoreDesc *const core_table[] = { &c128_desc, 0 };

struct C128Io : FakeIo {
    uint8_t e[0x100];
    int flushes = 0;
    int busy_reads = 0;                   // how many reads of ROM_CTRL see the walker busy after a flush
    int busy_left = 0;
    bool busy_stuck = false;
    bool deaf = false;                    // the DDR2 takes no writes
    uint32_t rom_base = 0x01C00000u;
    int d4080_running = 0;                // writes of D4080 while the machine ran
    int mon_running = 0;
    uint8_t status_bits = 0;              // C128_STATUS b0 C64 mode, b1 Z80 (besides the held bit)
    uint8_t chr[8192];                    // the character ROM store (T113 5.7): CHR_ADDR / CHR_DATA, byte i = image byte 0x10000 + i
    uint32_t chr_addr = 0;
    bool chr_deaf = false;                // the store takes no writes
    int chr_writes = 0, dbg_clears = 0;
    struct Dw { uint32_t off, n; };
    std::vector<Dw> dw;
    DriveModel dm;                        // drive A as the cbm1541 library sees it (the drive ROM area, the type register)
    void drv_wr(uint32_t off, uint8_t v) { dm.wr(off, v); }
    bool drv_present() { return dm.present; }
    uint8_t drv_rd(uint32_t off) { return dm.rd(off); }
    void ddr_abs_write(uint32_t addr, const void *p, uint32_t n) { dm.abs_write(addr, p, n); }
    void ddr_abs_read(uint32_t addr, void *p, uint32_t n) { dm.abs_read(addr, p, n); }
    uint8_t *drv_buffer(uint32_t bytes, uint32_t *phys) { return dm.buffer(bytes, phys); }
    C128Io() { memset(e, 0, sizeof(e)); memset(chr, 0, sizeof(chr)); }
    uint8_t rd(uint32_t reg)
    {
        if (reg < T_PAGE) return FakeIo::rd(reg);
        uint32_t off = reg - T_PAGE;
        if (off == 0x40) {
            if (busy_stuck) return 1;
            if (busy_left > 0) { busy_left--; return 1; }
            return 0;
        }
        if (off >= 0x50 && off <= 0x53) return (uint8_t)(rom_base >> (8 * (off - 0x50)));
        if (off == 0x30) return (stopped ? 0x08 : 0x00) | status_bits;
        if (off == 0x90) return (uint8_t)chr_addr;
        if (off == 0x91) return (uint8_t)(chr_addr >> 8);
        if (off == 0x92) { uint8_t b = chr[chr_addr & 8191]; chr_addr = (chr_addr + 1) & 8191; return b; }
        return off < sizeof(e) ? e[off] : 0;
    }
    void wr(uint32_t reg, uint8_t v)
    {
        if (reg < T_PAGE) { FakeIo::wr(reg, v); return; }
        log.push_back({ reg, v, t });
        uint32_t off = reg - T_PAGE;
        if (off == 0x40) {
            if (v & 1) { flushes++; events.push_back("flush"); busy_left = busy_reads; }
            return;
        }
        if (off >= 0x50 && off <= 0x53) return;                   // read only
        if (off == 0x90) { chr_addr = (chr_addr & 0x1F00) | v; return; }
        if (off == 0x91) { chr_addr = (chr_addr & 0xFF) | ((uint32_t)(v & 0x1F) << 8); return; }
        if (off == 0x92) { if (!chr_deaf) chr[chr_addr & 8191] = v; chr_writes++; chr_addr = (chr_addr + 1) & 8191; return; }
        if (off == 0xB8) { dbg_clears++; return; }
        if (off == 0x21 && !in_reset) d4080_running++;
        if (off == 0x20 && !in_reset) mon_running++;
        if (off < sizeof(e)) e[off] = v;
    }
    void ddr_write(uint32_t off, const void *p, uint32_t n)
    {
        if (off < 0x00D00000u) {                                  // the picture's buffers (Video::init) are not the ROM
            dw.push_back({ off, n });
            events.push_back("ddr_write");
        }
        if (!deaf) FakeIo::ddr_write(off, p, n);
    }
    void mode_reset(bool on)
    {
        FakeIo::mode_reset(on);
        if (on) e[0x20] = 0;                                      // the core resets MON_SEL with the machine
    }
};

static Bytes pattern(int role, size_t n)
{
    Bytes v(n);
    for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(i * 7 + role * 61 + (i >> 8) + (i >> 13));
    return v;
}

#define IMG "/usb0/roms/c128/boot0.rom"

struct Rig {
    C128Io io;
    FakeFs fs;
    PxRec px;
    CoreCtx c;
    Settings *st;
    Video *vid;
    Rig()
    {
        memset(&c, 0, sizeof(c));
        c.desc = &c128_desc; c.io = &io; c.fs = &fs;
        st = new Settings(c128_desc.settings, c128_desc.nsettings, c128_desc.store, &fs, "/sd/cores/c128.cfg", 0);
        st->load();
        c.settings = st;
        vid = new Video(c128_desc.video, &px, &io);
        vid->set_alt(c128_desc.alt_video);                  // T194: the second picture, as run.cc gives it
        c.video = vid;
    }
    void image(int role = 1) { fs.put(IMG, pattern(role, T_ROM_BYTES)); }
    bool boot()
    {
        BoundedUi ui;
        BootResult r = boot_start(&c, &ui);
        return r == BOOT_RUNNING;
    }
    void set(const char *key, int v) { st->set(st->find(key), v); }
    // the live setting as the menu applies it
    void apply(const char *key) { regset_apply_setting(&c, st->find(key)); }
    Bytes window(uint32_t off, uint32_t n) { return Bytes(io.ddr.begin() + off, io.ddr.begin() + off + n); }
};

static uint32_t reg32(C128Io &io, uint32_t r) { return io.e[r - T_PAGE] | (io.e[r - T_PAGE + 1] << 8) | (io.e[r - T_PAGE + 2] << 16) | ((uint32_t)io.e[r - T_PAGE + 3] << 24); }

// the expected extra matrix, written out here once more: name, USB usage codes, K column, PB row
struct Want { const char *name; uint8_t u0, u1; int col, row; };
static const Want want[] = {
    { "HELP", 0x4B, 0, 0, 0 },      { "KP 8", 0x60, 0, 0, 1 },     { "KP 5", 0x5D, 0, 0, 2 },     { "TAB", 0x2B, 0, 0, 3 },
    { "KP 2", 0x5A, 0, 0, 4 },      { "KP 4", 0x5C, 0, 0, 5 },     { "KP 7", 0x5F, 0, 0, 6 },     { "KP 1", 0x59, 0, 0, 7 },
    { "ESC", 0x29, 0, 1, 0 },       { "KP +", 0x57, 0, 1, 1 },     { "KP -", 0x56, 0, 1, 2 },     { "LINE FEED", 0x4E, 0, 1, 3 },
    { "KP ENTER", 0x58, 0, 1, 4 },  { "KP 6", 0x5E, 0, 1, 5 },     { "KP 9", 0x61, 0, 1, 6 },     { "KP 3", 0x5B, 0, 1, 7 },
    { "ALT", 0xE2, 0xE6, 2, 0 },    { "KP 0", 0x62, 0, 2, 1 },     { "KP .", 0x63, 0, 2, 2 },     { "CURSOR UP", 0x52, 0, 2, 3 },
    { "CURSOR DOWN", 0x51, 0, 2, 4 }, { "CURSOR LEFT", 0x50, 0, 2, 5 }, { "CURSOR RIGHT", 0x4F, 0, 2, 6 }, { "NO SCROLL", 0x48, 0, 2, 7 },
};

int main()
{
    OpResult res;

    // ---- the start: the image goes to the window at 0x00C00000 and nowhere else, the cache is flushed after the last byte, the machine is
    // held while it happens
    {
        Rig r;
        r.image();
        CHECK(r.boot());
        CHECK(r.window(T_ROM_WIN, T_ROM_BYTES) == pattern(1, T_ROM_BYTES));
        CHECK_EQ(r.window(T_ROM_WIN - 1, 1)[0], 0);                       // not one byte below ...
        CHECK_EQ(r.window(T_ROM_WIN + T_ROM_BYTES, 1)[0], 0);              // ... or above
        CHECK_EQ(r.io.dw.size(), 1u);
        CHECK_EQ(r.io.dw[0].off, T_ROM_WIN);
        CHECK_EQ(r.io.dw[0].n, T_ROM_BYTES);
        CHECK_EQ(r.io.flushes, 1);
        // the character ROMs (image bytes 0x10000..0x11FFF) are in the VIC's store, 8 KiB, written once, and nothing of the rest
        {
            Bytes img1 = pattern(1, T_ROM_BYTES);
            CHECK_EQ(r.io.chr_writes, 8192);
            CHECK(memcmp(r.io.chr, img1.data() + 0x10000, 8192) == 0);
            CHECK(r.io.dbg_clears >= 1);                                   // the frame meter starts at the release
        }
        int w = -1, f = -1, rel = -1;
        for (size_t i = 0; i < r.io.events.size(); i++) {
            if (r.io.events[i] == "ddr_write") w = (int)i;
            if (r.io.events[i] == "flush") f = (int)i;
            if (r.io.events[i] == "mode_unreset") rel = (int)i;
        }
        CHECK(w >= 0 && f > w && rel > f);                                  // image, then flush, then the release
        CHECK_STR(ev_first(r.io.events), "mode_reset");
        CHECK_EQ(r.io.d4080_running, 0);
        CHECK_EQ(r.io.mon_running, 0);
        // the picture: buffers announced; the default picture is the 40 columns (T194): the VIC's capture on the layer, format 3, 400 x 270, rows of 208 bytes, no buffer named
        CHECK_EQ(reg32(r.io, T_FB_BASE), T_FB_ADDR);
        CHECK_EQ(reg32(r.io, T_FB_STRIDE), T_FB_STRIDE_BYTES);
        r.vid->show();
        CHECK_EQ(r.vid->picture(), 1);
        CHECK(r.vid->visible());
        CHECK_EQ(r.px.last(PX_SIZE), (270u << 16) | 400u);
        CHECK_EQ(r.px.last(PX_STRIDE), 208u);
        CHECK_EQ(r.px.last(PX_CTRL) & 0x0Fu, 7u);                           // format 3, on (the scale bits are the old host's layout, not read by the gateware)
        CHECK_EQ(r.px.count(PX_FRONT), 0);
        // 80 columns: the layer set up for the VDC's picture at 2 x 2 on 1920 x 1080: 640 x 400 (1280 x 800) when FB_LATEST b2 is clear, woven 640 x 480 when set
        // three buffers: buffer 2 is flipped to (layer on first), buffer 3 does not exist
        r.set("Picture shown", 1);
        r.apply("Picture shown");
        CHECK(r.vid->visible());
        CHECK_EQ(r.vid->picture(), 0);
        // T113 D11: before the core has reported a picture the layer is the descriptor's 640 x 480 (the woven picture), at 2 x 2 (1280 x 960) centred on 1080 lines
        CHECK_EQ(r.px.last(PX_SIZE), (480u << 16) | 640u);
        CHECK_EQ(r.px.last(PX_STRIDE), 1280u);
        CHECK_EQ(r.px.last(PX_POS), (60u << 16) | 320u);
        CHECK_EQ(r.px.last(PX_FRONT), T_FB_ADDR);
        CHECK_EQ(r.px.last(PX_CTRL), Video::ctrl_value(2, true));
        {   // the service follows the newest picture: FB_LATEST b2 clear = 400 rows (1280 x 800, y 140), set = 480 rows woven (y 60)
            r.io.e[0x60] = 0x82;
            CHECK_EQ(ops_call(&r.c, "c128.service", 0, 0, &res), OPE_OK);
            CHECK_EQ(r.px.last(PX_SIZE), (400u << 16) | 640u);
            CHECK_EQ(r.px.last(PX_POS), (140u << 16) | 320u);
            r.io.e[0x60] = 0x86;
            CHECK_EQ(ops_call(&r.c, "c128.service", 0, 0, &res), OPE_OK);
            CHECK_EQ(r.px.last(PX_SIZE), (480u << 16) | 640u);
            CHECK_EQ(r.px.last(PX_POS), (60u << 16) | 320u);
            CHECK_EQ(r.px.last(PX_CTRL), Video::ctrl_value(2, true));
        }
        r.io.e[0x60] = 0x82; r.io.e[0x61] = 5;
        CHECK(r.vid->poll());
        CHECK_EQ(r.px.last(PX_BACK), T_FB_ADDR + 2 * T_FB_STRIDE_BYTES);
        r.io.e[0x60] = 0x83; r.io.e[0x61] = 6;
        CHECK(!r.vid->poll());
        // Freeze: the descriptor's held bit is the one the core raises while C64_STOP is on
        CHECK_EQ(freeze_status(&r.io, c128_desc.freeze) & c128_desc.freeze.mask, 0);
        r.io.stop(true);
        CHECK_EQ(freeze_status(&r.io, c128_desc.freeze) & c128_desc.freeze.mask, c128_desc.freeze.mask);
        r.io.stop(false);
    }
    // ---- a blob of the wrong size is refused before the window is touched; so is an image the DDR2 does not keep
    {
        Rig r;
        r.image();
        CHECK(r.boot());
        r.io.dw.clear(); r.io.flushes = 0;
        Bytes img = pattern(2, T_ROM_BYTES);
        Bytes big = pattern(2, T_ROM_BYTES + 1);
        for (uint32_t bad : { T_ROM_BYTES - 1, T_ROM_BYTES + 1, 65536u, 0u }) {
            OpIn small = in_blob("data", big.data(), bad);
            CHECK_EQ(ops_call(&r.c, "c128.put_rom", &small, 1, &res), OPE_FAILED);
            CHECK(strstr(res.msg, "73728") != 0);
        }
        CHECK(r.io.dw.empty());
        CHECK_EQ(r.io.flushes, 0);
        CHECK(r.window(T_ROM_WIN, T_ROM_BYTES) == pattern(1, T_ROM_BYTES));  // the image of the start is still the one in the window
        r.io.deaf = true;
        OpIn full = in_blob("data", img.data(), (uint32_t)img.size());
        CHECK_EQ(ops_call(&r.c, "c128.put_rom", &full, 1, &res), OPE_FAILED);
        CHECK(strstr(res.msg, "read-back") != 0);
        CHECK_EQ(r.io.flushes, 0);                                          // no flush for an image that did not land
        r.io.deaf = false;
        // a core that keeps its ROM somewhere else (another version) gets nothing written
        r.io.dw.clear();
        r.io.rom_base = 0x01E00000u;
        CHECK_EQ(ops_call(&r.c, "c128.put_rom", &full, 1, &res), OPE_FAILED);
        CHECK(strstr(res.msg, "01E00000") != 0);
        CHECK(r.io.dw.empty());
        r.io.rom_base = 0x01C00000u;
        // the flush: the walker is busy for a few reads, then done; a walker that never finishes ends in a reason, not a hang
        r.io.busy_reads = 5;
        CHECK_EQ(ops_call(&r.c, "c128.put_rom", &full, 1, &res), OPE_OK);
        CHECK_EQ(r.io.flushes, 1);
        CHECK_EQ(r.io.busy_left, 0);
        CHECK(r.window(T_ROM_WIN, T_ROM_BYTES) == img);
        r.io.busy_stuck = true;
        uint32_t t0 = r.io.t;
        CHECK_EQ(ops_call(&r.c, "c128.put_rom", &full, 1, &res), OPE_FAILED);
        CHECK(strstr(res.msg, "flush") != 0);
        CHECK(r.io.t - t0 < 1000);
        r.io.busy_stuck = false;
        // the character store that does not take the bytes ends in a reason; one that does holds the new image's character ROMs
        Bytes img5 = pattern(5, T_ROM_BYTES);                                // another image: the store still holds the old one's bytes
        OpIn full5 = in_blob("data", img5.data(), (uint32_t)img5.size());
        r.io.chr_deaf = true;
        CHECK_EQ(ops_call(&r.c, "c128.put_rom", &full5, 1, &res), OPE_FAILED);
        CHECK(strstr(res.msg, "character") != 0);
        r.io.chr_deaf = false;
        CHECK_EQ(ops_call(&r.c, "c128.put_rom", &full5, 1, &res), OPE_OK);
        CHECK(memcmp(r.io.chr, img5.data() + 0x10000, 8192) == 0);
    }
    // ---- the ROM manager: a file of the wrong size is told by name and size; the image under other names; Reload puts the new file
    {
        Rig q;
        q.fs.put(IMG, Bytes(32768, 1));
        RomResult rr[8];
        char summary[300];
        CHECK_EQ(rom_scan(&q.fs, c128_desc.folder, c128_desc.roms, c128_desc.nroms, rr, summary, sizeof(summary), 0), ROM_WRONG_SIZE);
        CHECK(strstr(summary, "73728") != 0);
        Rig n;
        n.fs.put("/usb0/roms/c128/C128.ROM", pattern(3, T_ROM_BYTES));
        CHECK(n.boot());
        CHECK(n.window(T_ROM_WIN, T_ROM_BYTES) == pattern(3, T_ROM_BYTES));
        Rig r;
        r.image();
        CHECK(r.boot());
        r.fs.put(IMG, pattern(9, T_ROM_BYTES));
        CHECK_EQ(ops_call(&r.c, "machine.reload_roms", 0, 0, &res), OPE_OK);
        CHECK(r.window(T_ROM_WIN, T_ROM_BYTES) == pattern(9, T_ROM_BYTES));
        CHECK_EQ(r.io.flushes, 2);
        // Reset leaves the image alone
        r.io.dw.clear();
        CHECK_EQ(ops_call(&r.c, "machine.cold_reset", 0, 0, &res), OPE_OK);
        CHECK(r.io.dw.empty());
        CHECK_EQ(r.io.flushes, 2);
    }
    // ---- the menu's file action: .rom -> load as system image (exactly 72 KiB), from any folder
    {
        Rig r;
        r.image();
        CHECK(r.boot());
        const FileAction *act = 0;
        for (int i = 0; i < c128_desc.nfiletypes; i++) {
            if (!strcmp(c128_desc.filetypes[i].ext, ".rom")) act = &c128_desc.filetypes[i].actions[0];
        }
        CHECK(act != 0);
        if (act) {
            r.fs.put("/usb0/other/custom.rom", pattern(4, T_ROM_BYTES));
            r.fs.put("/usb0/other/short.rom", pattern(4, 16384));
            CHECK_EQ(ops_run_bound(&r.c, act->op, act->bind, act->nbind, "/usb0/other/custom.rom", &res), OPE_OK);
            CHECK(r.window(T_ROM_WIN, T_ROM_BYTES) == pattern(4, T_ROM_BYTES));
            CHECK_EQ(r.io.flushes, 2);
            CHECK_EQ(ops_run_bound(&r.c, act->op, act->bind, act->nbind, "/usb0/other/short.rom", &res), OPE_FAILED);
            CHECK(strstr(res.msg, "73728") != 0);
            CHECK_EQ(ops_run_bound(&r.c, act->op, act->bind, act->nbind, "/usb0/other/none.rom", &res), OPE_FAILED);
            CHECK(r.window(T_ROM_WIN, T_ROM_BYTES) == pattern(4, T_ROM_BYTES));
            CHECK_EQ(r.io.flushes, 2);
        }
    }
    // ---- the settings: D4080 is a register the core takes at its reset, written only in the hold; MON_SEL is live and drives the layer
    {
        Rig r;
        r.image();
        CHECK(r.boot());
        CHECK_EQ(r.io.e[0x21], 0);                                          // the start wrote the default: 40 columns
        r.set("40/80 DISPLAY (at next reset)", 1);
        CHECK_EQ(r.io.e[0x21], 0);                                          // choosing writes nothing
        const Task *reset = 0;
        for (int i = 0; i < c128_desc.ntasks; i++) if (c128_desc.tasks[i].label && !strcmp(c128_desc.tasks[i].label, "Reset")) reset = &c128_desc.tasks[i];
        CHECK(reset != 0);
        if (reset) {
            CHECK_EQ(ops_run_bound(&r.c, reset->op, reset->bind, reset->nbind, 0, &res), OPE_OK);
            CHECK_EQ(r.io.e[0x21], 1);
            CHECK_EQ(r.io.d4080_running, 0);
        }
        // MON_SEL live: the register, the layer on, and a layer that is given back by show() (menu close) stays on
        r.set("Picture shown", 1);
        r.apply("Picture shown");
        CHECK_EQ(r.io.e[0x20], 1);
        CHECK(r.vid->visible());
        CHECK_EQ(r.px.last(PX_CTRL), Video::ctrl_value(2, true));
        r.vid->hide(); r.vid->show();
        CHECK(r.vid->visible());
        // and back: the layer shows the VIC's capture, and show() after a menu brings that one back
        r.set("Picture shown", 0);
        r.apply("Picture shown");
        CHECK_EQ(r.io.e[0x20], 0);
        CHECK(r.vid->visible());
        CHECK_EQ(r.vid->picture(), 1);
        r.vid->hide(); r.vid->show();
        CHECK(r.vid->visible());
        CHECK_EQ(r.vid->picture(), 1);
        CHECK_EQ(r.px.last(PX_CTRL) & 0x0Fu, 7u);
        // a Reset: the core resets MON_SEL, the setting writes the person's choice again before the release
        r.set("Picture shown", 1);
        CHECK_EQ(ops_call(&r.c, "machine.cold_reset", 0, 0, &res), OPE_OK);
        CHECK_EQ(r.io.e[0x20], 1);
        CHECK(r.vid->visible());
        // the monitor-switch key: the core toggles MON_SEL on its own, the service moves the layer
        r.io.e[0x20] = 0;
        CHECK_EQ(ops_call(&r.c, "c128.service", 0, 0, &res), OPE_OK);
        CHECK_EQ(r.vid->picture(), 1);
        CHECK(r.vid->visible());
        r.io.e[0x20] = 1;
        CHECK_EQ(ops_call(&r.c, "c128.service", 0, 0, &res), OPE_OK);
        CHECK(r.vid->visible());
        // the menu is open: the layer is the menu's, the service must not take it back
        r.vid->hide();
        r.c.menu_open = true;
        r.io.e[0x20] = 0;
        CHECK_EQ(ops_call(&r.c, "c128.service", 0, 0, &res), OPE_OK);
        r.io.e[0x20] = 1;
        CHECK_EQ(ops_call(&r.c, "c128.service", 0, 0, &res), OPE_OK);
        CHECK(!r.vid->visible());
        r.c.menu_open = false;
    }
    // ---- T194 D12: Picture shown = 40 / 80 (the start picture), Auto switch on/off, Auto switch time, the 40/80 hotkey. The rules are proven on the board, not by sims
    // (Alex, 2026-11-09); here the functions the board runs get the register words and the capture's bytes.
    {
        // the settings: defaults, stored and read back, an old "Auto" label
        Rig r;
        r.image();
        CHECK(r.boot());
        int im = r.c.settings->find("Picture shown"), ia = r.c.settings->find("Auto switch"), it = r.c.settings->find("Auto switch time");
        CHECK(im >= 0 && ia >= 0 && it >= 0);
        CHECK_EQ(r.c.settings->value(im), 0);                               // 40 columns
        CHECK_EQ(r.c.settings->value(ia), 1);                               // Auto switch on
        CHECK_EQ(r.c.settings->value(it), 1);                               // 1 s
        CHECK_EQ(c128_desc.settings[im].nvalues, 2);
        CHECK_EQ(c128_desc.settings[it].nvalues, 4);
        CHECK(!strcmp(c128_desc.settings[it].values[0], "0.5 s") && !strcmp(c128_desc.settings[it].values[3], "3 s"));
        CHECK_EQ(r.io.regs[0x64], 0x00); CHECK_EQ(r.io.regs[0x65], 0x00); CHECK_EQ(r.io.regs[0x66], 0xCC); CHECK_EQ(r.io.regs[0x67], 0x01);   // STILL_BASE 0x01CC0000, inside the app's window
        CHECK_EQ(r.io.e[0x20], 0);
        r.set("Picture shown", 1); r.apply("Picture shown");
        r.set("Auto switch", 0); r.apply("Auto switch");
        r.set("Auto switch time", 3); r.apply("Auto switch time");
        CHECK_EQ(r.io.e[0x20], 1);
        Settings back(c128_desc.settings, c128_desc.nsettings, c128_desc.store, &r.fs, "/sd/cores/c128.cfg", 0);
        back.load();
        CHECK_EQ(back.value(back.find("Picture shown")), 1);
        CHECK_EQ(back.value(back.find("Auto switch")), 0);
        CHECK_EQ(back.value(back.find("Auto switch time")), 3);
        // a Reset puts the chosen start picture up again
        CHECK_EQ(ops_call(&r.c, "machine.cold_reset", 0, 0, &res), OPE_OK);
        CHECK_EQ(r.io.e[0x20], 1);
    }
    {
        Rig r;                                                              // an older .cfg with Picture shown=Auto: the label is not in the list, the default (40, Auto switch on) holds
        std::string txt = "Picture shown=Auto\n";
        r.fs.put("/sd/cores/c128.cfg", Bytes(txt.begin(), txt.end()));
        r.st->load();
        CHECK_EQ(r.c.settings->value(r.c.settings->find("Picture shown")), 0);
        CHECK_EQ(r.c.settings->value(r.c.settings->find("Auto switch")), 1);
    }
    // the rig for the rules: frames of 20 ms with the activity words, the capture's slot 0 with a byte the test changes
    struct Run {
        Rig r;
        uint8_t seq = 0, pic = 0;
        uint32_t frames = 0;
        void start(int shown, int auto_on = 1, int tsel = 1)
        {
            r.image();
            CHECK(r.boot());
            r.set("Picture shown", shown); r.apply("Picture shown");
            r.set("Auto switch", auto_on); r.apply("Auto switch");
            r.set("Auto switch time", tsel); r.apply("Auto switch time");
            r.io.regs[0x68] = 0x01;                                        // STILL_STATUS: valid, slot 0, 270 rows
            r.io.regs[0x69] = r.io.regs[0x6A] = r.io.regs[0x6B] = 0;
        }
        int mon() { return r.io.e[0x20]; }
        // ms of frames: vic = register writes, cram = colour RAM writes, vdc = data register writes, newpic: the capture changes every frame
        void go(int ms, int vic, int cram, int vdc, bool newpic = false, int den = 1)
        {
            for (int t = 0; t < ms; t += 20) {
                r.io.delay_ms(20);
                r.io.e[0xD0] = (uint8_t)vic; r.io.e[0xD1] = (uint8_t)cram; r.io.e[0xD2] = (uint8_t)vdc; r.io.e[0xD3] = (uint8_t)den; r.io.e[0xD4] = ++seq;
                frames++;
                r.io.regs[0x69] = (uint8_t)frames; r.io.regs[0x6A] = (uint8_t)(frames >> 8);
                if (newpic) { r.io.ddr[0xCC0000 + 5] = ++pic; }
                OpResult rr;
                CHECK_EQ(ops_call(&r.c, "c128.service", 0, 0, &rr), OPE_OK);
            }
        }
    };
    {   // idle READY in 80 columns stays 80: the IRQ's 23 register writes, a still capture, the VDC written now and then
        Run u; u.start(1);
        u.go(10000, 23, 0, 0);
        CHECK_EQ(u.mon(), 1);
        u.go(10000, 39, 0, 2);
        CHECK_EQ(u.mon(), 1);
    }
    {   // the Robots case: boot in 80, "WAKING UP THE ROBOTS" on the VDC, then the game draws on the VIC: VIC picture one switch time after the last VDC write, not before
        Run u; u.start(1);
        u.go(3000, 23, 0, 0);
        u.go(2000, 23, 0, 40);                                              // the VDC text
        CHECK_EQ(u.mon(), 1);
        u.go(900, 23, 120, 0, true);                                        // the game draws: colour RAM, the picture changes; the VDC has been quiet for 900 ms
        CHECK_EQ(u.mon(), 1);
        u.go(200, 23, 120, 0, true);                                        // 1.1 s of VDC quiet
        CHECK_EQ(u.mon(), 0);
        CHECK_EQ(u.r.vid->picture(), 1);
        u.go(5000, 23, 120, 0, true);                                       // it stays
        CHECK_EQ(u.mon(), 0);
    }
    {   // c128.auto_diag: the board's read-only view of the Auto watching (the Robots sequence, read at each stage)
        Run u; u.start(1);
        u.go(3000, 23, 0, 0);
        u.go(2000, 23, 0, 40);
        u.go(900, 23, 120, 0, true);
        uint32_t rev = 0;
        const char *d = ops_of(&u.r.c)->state(&u.r.c, "c128.auto_diag", &rev);
        CHECK(d != 0);
        CHECK(strstr(d, "shown=1 auto=on") != 0);
        CHECK(strstr(d, "sw=1000") != 0);
        CHECK(strstr(d, "quiet=0 oth=1 dec=-2") != 0);                      // the VIC is active, the VDC has been quiet for 900 ms only
        CHECK(strstr(d, "last(vic=23 cram=120 vdc=0") != 0);
        CHECK(strstr(d, "frv=100 ") != 0);                                  // 2000 ms of 20 ms frames with VDC writes
        u.go(200, 23, 120, 0, true);
        d = ops_of(&u.r.c)->state(&u.r.c, "c128.auto_diag", &rev);
        CHECK(strstr(d, "shown=0 ") != 0);                                  // switched; the watching restarted (counters back at 0 is not claimed, the picture is)
        CHECK(strstr(d, "hash(ok=") != 0 && strstr(d, "still=00") != 0);
    }
    {   // the picture alone (no register or colour RAM write, DMA / RAM-written bitmap) is VIC activity; the same picture again is none
        Run u; u.start(1);
        u.go(3000, 23, 0, 0);
        u.go(3000, 23, 0, 0, false);
        CHECK_EQ(u.mon(), 1);
        u.go(2000, 23, 0, 0, true);
        CHECK_EQ(u.mon(), 0);
    }
    {   // both active: no flicker, over 20 s the picture never changes
        Run u; u.start(1);
        u.go(500, 23, 0, 10);
        for (int i = 0; i < 100; i++) {
            u.go(100, 23, 90, 10, true);
            CHECK_EQ(u.mon(), 1);
        }
        Run w; w.start(0);
        w.go(500, 23, 90, 0, true);
        for (int i = 0; i < 100; i++) {
            w.go(100, 23, 90, 10, true);
            CHECK_EQ(w.mon(), 0);
        }
    }
    {   // and back: the VIC shown, the program goes to the VDC and the VIC goes quiet
        Run u; u.start(0);
        u.go(2000, 23, 90, 0, true);
        CHECK_EQ(u.mon(), 0);
        u.go(900, 23, 0, 30);
        CHECK_EQ(u.mon(), 0);                                               // the VIC was written 0 ms ago... the first frame of this stretch was 20 ms after its last
        u.go(300, 23, 0, 30);
        CHECK_EQ(u.mon(), 1);
    }
    {   // Auto switch off: never
        Run u; u.start(1, 0);
        u.go(3000, 23, 0, 0);
        u.go(10000, 23, 150, 0, true);
        CHECK_EQ(u.mon(), 1);
        Run w; w.start(0, 0);
        w.go(10000, 23, 0, 40);
        CHECK_EQ(w.mon(), 0);
    }
    {   // the switch time: 3 s is not over after 2.5 s, 0.5 s is over after 0.6 s
        Run u; u.start(1, 1, 3);
        u.go(1000, 23, 0, 0);
        u.go(100, 23, 0, 5);
        u.go(2500, 23, 120, 0, true);
        CHECK_EQ(u.mon(), 1);
        u.go(600, 23, 120, 0, true);
        CHECK_EQ(u.mon(), 0);
        Run w; w.start(1, 1, 0);
        w.go(1000, 23, 0, 0);
        w.go(100, 23, 0, 5);
        w.go(600, 23, 120, 0, true);
        CHECK_EQ(w.mon(), 0);
    }
    {   // the hotkey flips by hand, Auto keeps watching: the VIC still draws, the VDC is quiet -> back to the VIC after the switch time, not at once
        Run u; u.start(0);
        u.go(2000, 23, 90, 0, true);
        char line[64];
        HkLoop lp;
        CHECK(lp.perform(&u.r.c, 1u << HKA_CORE, line, sizeof(line)));
        CHECK_EQ(u.mon(), 1);
        CHECK(strstr(line, "80") != 0);
        u.go(900, 23, 90, 0, true);
        CHECK_EQ(u.mon(), 1);
        u.go(300, 23, 90, 0, true);
        CHECK_EQ(u.mon(), 0);
        u.go(300, 23, 0, 0);                                                // the program goes idle (a still picture; the last change is sampled)
        CHECK(lp.perform(&u.r.c, 1u << HKA_CORE, line, sizeof(line)));      // and by hand again, the program idle now: it stays
        CHECK_EQ(u.mon(), 1);
        u.go(5000, 23, 0, 0);
        CHECK_EQ(u.mon(), 1);
        // with Auto off the hotkey still flips
        u.r.set("Auto switch", 0); u.r.apply("Auto switch");
        CHECK(lp.perform(&u.r.c, 1u << HKA_CORE, line, sizeof(line)));
        CHECK_EQ(u.mon(), 0);
        CHECK_EQ(u.r.vid->picture(), 1);
    }
    {   // the hotkey's item and default key in the F8 list; C64 mode shows the VIC, Z80 mode leaves the picture
        CoreDesc o; static Setting store[SETTINGS_MAX];
        CHECK(!hk_extend(&c128_desc, &o, store));
        CHECK_EQ(o.nsettings, c128_desc.nsettings + 1 + HK_NACT);
        int k = -1;
        for (int i = 0; i < o.nsettings; i++) if (!strcmp(o.settings[i].key, "Hotkey picture 40/80")) k = i;
        CHECK(k >= 0);
        if (k >= 0) { CHECK_EQ(o.settings[k].def, 19); CHECK(!strcmp(o.settings[k].label, "Picture 40 / 80")); }   // matrix key D (18) + 1
        uint8_t regs[HK_NREGS]; hk_regs(c128_desc.hotkeys->def, regs);
        CHECK(regs[2 + 18 / 8] & (1u << (18 % 8)));
        Run u; u.start(1);
        u.r.io.status_bits = 0x02;                                          // Z80
        u.go(5000, 23, 150, 0, true);
        CHECK_EQ(u.mon(), 1);
        u.r.io.status_bits = 0x01;                                          // C64 mode
        u.go(100, 23, 0, 0);
        CHECK_EQ(u.mon(), 0);
    }
    // ---- drive A: 1541 and 1581. The tasks of both pieces, the .d81 type, the 1581 ROM, the media slot
    {
        const Task *d81 = 0, *d64 = 0;
        for (int i = 0; i < c128_desc.ntasks; i++) {
            const char *l = c128_desc.tasks[i].label;
            if (l && !strcmp(l, "Create blank D81")) d81 = &c128_desc.tasks[i];
            if (l && !strcmp(l, "Create blank D64")) d64 = &c128_desc.tasks[i];
        }
        CHECK(d64 != 0);
        CHECK(d81 != 0);
        if (d81) {
            CHECK_STR(d81->op, "drive.create");
            CHECK_STR(d81->ask, "Name of the new D81");
            CHECK_STR(d81->ask_state, "drive.suggest.d81");
        }
        bool has81 = false, has_rom81 = false;
        for (int i = 0; i < c128_desc.nfiletypes; i++) has81 = has81 || !strcmp(c128_desc.filetypes[i].ext, ".d81");
        for (int i = 0; i < c128_desc.nroms; i++) has_rom81 = has_rom81 || !strcmp(c128_desc.roms[i].role, "1581 drive");
        CHECK(has81);
        CHECK(has_rom81);
        CHECK(desc_has_capability(&c128_desc, "drive_a"));
        CHECK(desc_has_capability(&c128_desc, "iec"));
        CHECK_EQ(c128_desc.nmedia, 1);
        CHECK_STR(c128_desc.media[0].name, "Drive A:");
        CHECK_STR(c128_desc.roms[0].put_op, "c128.put_rom");
        CHECK_EQ(c128_desc.roms[0].sizes[0], T_ROM_BYTES);
        CHECK(c128_desc.roms[0].required);
        CHECK(!memcmp(c128_desc.id, "C128", 4));
        // T113 5.6: "Restart core" clears the 64 KiB RAM block the application's RAM window reaches (block 1 and the VDC's RAM it does not)
        CHECK_EQ(c128_desc.cold_ram_bytes, 65536u);
    }

    // ---- the USB keyboard's extra keys: every one of the 24 matrix cells has exactly one key (ALT two: left and right), no other code gets
    // one, no C64 key is an extra key; CAPS LOCK, 40/80 and the monitor switch are XSPECIAL bits
    {
        bool cell[3][8] = {};
        uint8_t known[256] = {};
        int n = (int)(sizeof(want) / sizeof(want[0]));
        CHECK_EQ(n, 24);
        for (int i = 0; i < n; i++) {
            for (uint8_t u : { want[i].u0, want[i].u1 }) {
                if (!u) continue;
                C128XKey k;
                CHECK(c128_xkey_for(u, &k));
                CHECK_EQ(k.col, want[i].col);
                CHECK_EQ(k.row, want[i].row);
                CHECK_EQ(c128_xspecial_for(u), 0);
                CHECK(!known[u]);
                known[u] = 1;
            }
            CHECK(!cell[want[i].col][want[i].row]);                          // no two keys on one cell
            cell[want[i].col][want[i].row] = true;
        }
        for (int col = 0; col < 3; col++) for (int row = 0; row < 8; row++) CHECK(cell[col][row]);
        known[0x39] = known[0x44] = known[0x45] = 1;
        CHECK_EQ(c128_xspecial_for(0x39), 0x01);
        CHECK_EQ(c128_xspecial_for(0x44), 0x02);
        CHECK_EQ(c128_xspecial_for(0x45), 0x04);
        for (int u = 0; u < 256; u++) {
            C128XKey k;
            if (!known[u]) {
                CHECK(!c128_xkey_for((uint8_t)u, &k));
                CHECK_EQ(c128_xspecial_for((uint8_t)u), 0);
            } else if (u != 0x39 && u != 0x44 && u != 0x45) {
                CHECK(c128_xkey_for((uint8_t)u, &k));
            }
        }
        // the C64's own keys by name: letters, digits, RETURN, DEL, SPACE, punctuation, F1-F8, HOME, INS, shifts, CTRL, C=, the keypad's / and *
        for (int u = 0x04; u <= 0x38; u++) { C128XKey k; CHECK(!c128_xkey_for((uint8_t)u, &k) || u == 0x29 || u == 0x2B); }
        for (uint8_t u : { 0x28, 0x2A, 0x2C, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x4A, 0x49, 0x4C, 0x4D, 0xE0, 0xE1, 0xE3, 0xE4, 0xE5, 0xE7, 0x54, 0x55 }) {
            C128XKey k;
            CHECK(!c128_xkey_for(u, &k));
            CHECK_EQ(c128_xspecial_for(u), 0);
        }
        // pressed together: HELP + KP 8 + LINE FEED + NO SCROLL, CAPS LOCK; written to the core's page
        Rig r;
        uint8_t down[] = { 0x4B, 0x60, 0x4E, 0x48, 0x39, 0x04 };
        c128_xkeys_apply(&r.io, down, (int)sizeof(down));
        CHECK_EQ(r.io.e[0x28], 0x03);                                       // K0: rows 0 and 1
        CHECK_EQ(r.io.e[0x29], 0x08);                                       // K1: row 3
        CHECK_EQ(r.io.e[0x2A], 0x80);                                       // K2: row 7
        CHECK_EQ(r.io.e[0x2B], 0x01);
        c128_xkeys_apply(&r.io, down, 0);                                    // all released: the levels fall
        CHECK(r.io.e[0x28] == 0 && r.io.e[0x29] == 0 && r.io.e[0x2A] == 0 && r.io.e[0x2B] == 0);
    }
    // ---- T141 P2, S1/S2: a USB report reaches the extra-key registers through the application's USB hook (rawkeys_usb_report) and the core's poll
    // (engines_poll), not through a direct call of c128_xkeys_apply. Written from the table `want` and the XSPECIAL bits, not read from c128.cc.
    {
        Rig r;
        engines_of(&r.c);
        auto rep = [](uint8_t mods, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0) {
            uint8_t x[8] = { mods, 0, a, b, c, 0, 0, 0 };
            rawkeys_usb_report(x);
        };
        auto poll = [&]() { for (int i = 0; i < 3; i++) engines_poll(&r.c); };
        auto regs = [&](uint8_t k0, uint8_t k1, uint8_t k2, uint8_t sp) {
            return r.io.e[0x28] == k0 && r.io.e[0x29] == k1 && r.io.e[0x2A] == k2 && r.io.e[0x2B] == sp;
        };
        CHECK(!rawkeys_active());                                           // F10 stays a menu key on the C128 (no raw key queue)
        CHECK(regs(0, 0, 0, 0));
        for (int i = 0; i < 24; i++) {
            for (uint8_t u : { want[i].u0, want[i].u1 }) {
                if (!u) continue;
                if (u >= 0xE0) rep((uint8_t)(1u << (u - 0xE0))); else rep(0, u);   // ALT is in the modifier byte
                poll();
                uint8_t k[3] = { 0, 0, 0 };
                k[want[i].col] = (uint8_t)(1u << want[i].row);
                if (!regs(k[0], k[1], k[2], 0)) printf("  press %s (usage %02X): K0..K2 = %02X %02X %02X\n", want[i].name, u, r.io.e[0x28], r.io.e[0x29], r.io.e[0x2A]);
                CHECK(regs(k[0], k[1], k[2], 0));
                rep(0);
                poll();
                if (!regs(0, 0, 0, 0)) printf("  release %s (usage %02X) left a bit\n", want[i].name, u);
                CHECK(regs(0, 0, 0, 0));
            }
        }
        // the special keys: CAPS LOCK, F11 = 40/80, F12 = monitor switch, as levels
        rep(0, 0x39); poll(); CHECK(regs(0, 0, 0, 0x01));
        rep(0, 0x44); poll(); CHECK(regs(0, 0, 0, 0x02));
        rep(0, 0x45); poll(); CHECK(regs(0, 0, 0, 0x04));
        rep(0); poll(); CHECK(regs(0, 0, 0, 0));
        // typing: the letters, digits, RETURN, SPACE, F1, the left shift and CTRL are the C64 matrix's, they set no extra key
        rep(0x02, 0x04, 0x1E, 0x28); poll(); CHECK(regs(0, 0, 0, 0));
        rep(0x01, 0x2C, 0x3A); poll(); CHECK(regs(0, 0, 0, 0));
        rep(0);
        // together: HELP + KP 8 (K0 rows 0, 1), LINE FEED (K1 row 3), left ALT (K2 row 0)
        rep(0x04, 0x4B, 0x60, 0x4E); poll(); CHECK(regs(0x03, 0x08, 0x01, 0));
        rep(0x04, 0x60); poll(); CHECK(regs(0x02, 0, 0x01, 0));              // HELP and LINE FEED up
        rep(0); poll(); CHECK(regs(0, 0, 0, 0));
        // once: the page is written when the keys change, not on every poll
        size_t n0 = r.io.log.size();
        rep(0, 0x29); poll();
        size_t n1 = r.io.log.size();
        CHECK(n1 > n0);
        poll(); poll();
        CHECK_EQ(r.io.log.size(), n1);
        rep(0); poll();
        // SCROLL LOCK is the application's menu key: it is not an extra key and it is not sent
        rep(0, 0x47); poll(); CHECK(regs(0, 0, 0, 0));
        rep(0);
        // the menu: a key down is released in the machine when the menu opens, USB keys go nowhere while it is open, and a key still held at the close
        // is ignored until it has been up
        rep(0, 0x29); poll(); CHECK(regs(0, 0x01, 0, 0));
        rawkeys_menu(&r.c, true);
        CHECK(regs(0, 0, 0, 0));
        rep(0, 0x4B); rawkeys_menu(&r.c, false);                             // HELP is held across the close
        poll(); CHECK(regs(0, 0, 0, 0));
        rep(0, 0x4B, 0x2B); poll(); CHECK(regs(0x08, 0, 0, 0));              // TAB comes, HELP stays ignored
        rep(0); poll(); CHECK(regs(0, 0, 0, 0));
        rep(0, 0x4B); poll(); CHECK(regs(0x01, 0, 0, 0));                    // up once, then pressed: HELP works again
        rep(0); poll();
        // an ErrorRollOver report says nothing: what is down stays down
        rep(0, 0x29); poll();
        { uint8_t x[8] = { 0, 0, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01 }; rawkeys_usb_report(x); }
        poll(); CHECK(regs(0, 0x01, 0, 0));
        rep(0); poll();
    }
    // ---- T140 D24 item 3: the System Mode. Four values (PAL, NTSC, PAL-60, NTSC-50 in the order of t_video_mode); each one hands the host's PLL the mode (the call a
    // change of the Picture makes) and writes C64_VIDEOFORMAT with the bits host/mode_ctl.v turns into the vmode the core (and the analogue encoder: c128_avout.v, PAL
    // 4.43361875 MHz in 0 and 2, NTSC 3.579545 MHz in 1 and 3) runs in. The chain from the byte to vmode is checked against mode_ctl.v's own line.
    {
        int si = -1;
        for (int i = 0; i < c128_desc.nsettings; i++) if (!strcmp(c128_desc.settings[i].key, "System Mode")) si = i;
        CHECK(si >= 0);
        if (si >= 0) {
            CHECK_EQ(c128_desc.settings[si].nvalues, 4);
            CHECK_STR(c128_desc.settings[si].values[0], "PAL");
            CHECK_STR(c128_desc.settings[si].values[1], "NTSC");
            CHECK_STR(c128_desc.settings[si].values[2], "PAL-60");
            CHECK_STR(c128_desc.settings[si].values[3], "NTSC-50");
            CHECK_EQ(c128_desc.settings[si].when, WHEN_LIVE);
        }
        // mode_ctl.v: the requested vmode is {b5 != b0, b0} of the format byte (any other line there and this model is stale)
        {
            std::string path = std::string(COREAPP_DIR) + "/../host/mode_ctl.v";
            FILE *f = fopen(path.c_str(), "r");
            CHECK(f != 0);
            bool found = false;
            char line[400];
            while (f && fgets(line, sizeof(line), f)) {
                if (strstr(line, "wire [1:0] fmt_mode = {fmt_s2[5] != fmt_s2[0], fmt_s2[0]};")) found = true;
            }
            if (f) fclose(f);
            CHECK(found);
        }
        static const uint8_t fmt_want[4] = { 0x00, 0x2B, 0x2A, 0x01 };     // u64.h: CYCLES_65 0x20, NTSC_FREQ 0x08, 60_HZ 0x02, NTSC_ENCODING 0x01
        Rig r;
        r.image();
        CHECK(r.boot());
        CHECK_EQ(r.io.vfmt, 0);                                              // the start applies the stored value: PAL
        for (int m : { 1, 2, 3, 0, 1 }) {
            g_applied.clear();
            r.set("System Mode", m);
            r.apply("System Mode");
            CHECK_EQ(r.io.vfmt, fmt_want[m]);
            int vmode = (((r.io.vfmt >> 5) & 1) != (r.io.vfmt & 1) ? 2 : 0) | (r.io.vfmt & 1);   // mode_ctl.v's expression
            CHECK_EQ(vmode, m);
            CHECK_EQ(g_applied.size(), 1u);                                  // the host's PLL was given the mode once ...
            if (g_applied.size() == 1) CHECK_EQ(g_applied[0].first, m);
            int s = -1, h = -1;
            CHECK(video_unpack(video_current, &s, &h));                      // ... and the application knows it (a later Picture change keeps the system)
            CHECK_EQ(s, m);
            // the same value again: no second PLL change (the screen would lose its sync), the format byte is rewritten harmlessly
            g_applied.clear();
            r.apply("System Mode");
            CHECK(g_applied.empty());
            CHECK_EQ(r.io.vfmt, fmt_want[m]);
        }
        // a Picture change after NTSC keeps NTSC
        r.set("System Mode", 1);
        r.apply("System Mode");
        g_applied.clear();
        picture_apply(&r.c, 2);                                              // 720p
        CHECK(!g_applied.empty());
        if (!g_applied.empty()) CHECK_EQ(g_applied.back().first, 1);
        // the stored choice is applied at the next start
        {
            Rig q;
            q.image();
            q.set("System Mode", 3);
            CHECK(q.boot());
            CHECK_EQ(q.io.vfmt, 0x01);
        }
    }

    // ---- T185 D4: the ROM set. The kit's parts are synthetic (the C64 part: CBMBASIC at offset 4, reset vector $FCE2; the C64 KERNAL alone: vector $FCE2;
    // the C128 part: no BASIC, vector $FF3D); no ROM byte of any machine is in this file. The files are SELECTED by the person (the use_jiffy_* operations
    // behind the settings rows and the browser's file actions); nothing is searched by name.
    {
        auto c64part = [](int role) { Bytes v = pattern(role, 16384); memcpy(&v[4], "CBMBASIC", 8); v[0x3FFC] = 0xE2; v[0x3FFD] = 0xFC; return v; };
        auto c64k8 = [](int role) { Bytes v = pattern(role, 8192); v[0x1FFC] = 0xE2; v[0x1FFD] = 0xFC; return v; };
        auto c128part = [](int role) { Bytes v = pattern(role, 16384); memset(&v[4], 0x11, 8); v[0x3FFC] = 0x3D; v[0x3FFD] = 0xFF; return v; };
        auto cat = [](Bytes a, const Bytes &b) { a.insert(a.end(), b.begin(), b.end()); return a; };
        const char *K = "/usb0/kit/";
        auto put = [&](Rig &r, const char *name, const Bytes &b) { r.fs.put(std::string(K) + name, b); };
        auto path = [&](const char *name) { return std::string(K) + name; };
        // the selection as the settings row / the file action makes it
        auto sel = [&](Rig &r, const char *op, const char *name, OpResult *o) {
            memset(o, 0, sizeof(*o));
            std::string full = path(name);                              // the argument points into it: it must outlive the call
            OpIn in = in_str("path", full.c_str());
            return ops_call(&r.c, op, &in, 1, o);
        };
        auto txt = [&](Rig &r, const char *key) { return std::string(r.st->text(r.st->find(key))); };
        auto setrom = [&](Rig &r, int v, OpResult *o) {
            memset(o, 0, sizeof(*o));
            r.set("ROM set", v);
            OpIn in = in_int("value", v);
            ops_call(&r.c, "c128.set_romset", &in, 1, o);
        };
        Bytes std1 = pattern(1, T_ROM_BYTES);
        Bytes want = std1;                                                  // the standard image with the kit's 16 KiB parts over their regions
        { Bytes a = c64part(2), b = c128part(3); memcpy(&want[0], a.data(), 16384); memcpy(&want[0x4000], b.data(), 16384); }

        // the rows: kind 3, WHEN_RESET, nothing selected by default, the ROM set itself Standard by default
        {
            const char *keys[4] = { "JiffyDOS C128 ROM", "JiffyDOS C64 ROM", "JiffyDOS 1541 ROM", "JiffyDOS 1581 ROM" };
            const char *ops[4] = { "c128.use_jiffy_c128", "c128.use_jiffy_c64", "c128.use_jiffy_1541", "c128.use_jiffy_1581" };
            for (int k = 0; k < 4; k++) {
                int idx = -1;
                for (int i = 0; i < c128_desc.nsettings; i++) if (!strcmp(c128_desc.settings[i].key, keys[k])) idx = i;
                CHECK(idx >= 0);
                if (idx >= 0) {
                    CHECK_EQ(c128_desc.settings[idx].kind, 3);
                    CHECK_EQ(c128_desc.settings[idx].when, WHEN_RESET);
                    CHECK_STR(c128_desc.settings[idx].apply, ops[k]);
                    CHECK_STR(c128_desc.settings[idx].values[0], "");
                }
            }
            for (int i = 0; i < c128_desc.nsettings; i++) if (!strcmp(c128_desc.settings[i].key, "ROM set")) { CHECK_EQ(c128_desc.settings[i].def, 0); CHECK_EQ(c128_desc.settings[i].when, WHEN_RESET); CHECK_STR(c128_desc.settings[i].values[0], "Standard"); }
        }
        // Stock (the default): the image byte-identical to the file, written once, flushed once; the kit files lying in the roms folder under the old names
        // are NOT taken (no search); nothing is said, no kit file is read
        {
            Rig r;
            r.image();
            r.fs.put("/usb0/roms/c128/jiffydos_c64.bin", c64part(2));
            r.fs.put("/usb0/roms/c128/jiffydos_c128.bin", c128part(3));
            CHECK(r.boot());
            CHECK(r.window(T_ROM_WIN, T_ROM_BYTES) == std1);
            CHECK_EQ(r.io.dw.size(), 1u);
            CHECK_EQ(r.io.flushes, 1);
            CHECK_EQ(r.io.chr_writes, 8192);
            CHECK_EQ(r.c.notice[0], 0);
            CHECK(!r.c.notice_pending);
            // JiffyDOS chosen, nothing selected: the files lying there are still not taken; the line says so; the machine runs on the standard image
            Rig j;
            j.image();
            j.fs.put("/usb0/roms/c128/jiffydos_c64.bin", c64part(2));
            j.fs.put("/usb0/roms/c128/jiffydos_c128.bin", c128part(3));
            j.set("ROM set", 1);
            CHECK(j.boot());
            CHECK(j.window(T_ROM_WIN, T_ROM_BYTES) == std1);
            CHECK(j.c.started);
            CHECK(strstr(j.c.notice, "no file selected, the machine starts Stock") != 0);
            CHECK(j.c.notice_pending);
        }
        // the selection: each accepted size, kept in the setting (and the .cfg), taken at the start with ROM set JiffyDOS, the parts at their offsets
        {
            Rig r;
            r.image();
            put(r, "c64.bin", c64part(2));
            put(r, "c128.bin", c128part(3));
            OpResult o;
            CHECK_EQ(sel(r, "c128.use_jiffy_c128", "c128.bin", &o), OPE_OK);
            CHECK(strstr(o.msg, "c128.bin selected") != 0);
            CHECK_EQ(sel(r, "c128.use_jiffy_c64", "c64.bin", &o), OPE_OK);
            CHECK_STR(txt(r, "JiffyDOS C128 ROM").c_str(), "/usb0/kit/c128.bin");
            CHECK_STR(txt(r, "JiffyDOS C64 ROM").c_str(), "/usb0/kit/c64.bin");
            Settings again(c128_desc.settings, c128_desc.nsettings, c128_desc.store, &r.fs, "/sd/cores/c128.cfg", 0);
            again.load();
            CHECK_STR(again.text(again.find("JiffyDOS C128 ROM")), "/usb0/kit/c128.bin");
            // selected but Stock chosen: the window keeps the standard image and nothing is said
            CHECK(r.boot());
            CHECK(r.window(T_ROM_WIN, T_ROM_BYTES) == std1);
            CHECK_EQ(r.c.notice[0], 0);
            setrom(r, 1, &o);                                                // the window follows at once
            CHECK(r.window(T_ROM_WIN, T_ROM_BYTES) == want);
            CHECK(strstr(o.msg, "JiffyDOS C128 ROM not selected") == 0 && strstr(o.msg, "JiffyDOS C64 ROM not selected") == 0);
            setrom(r, 0, &o);
            CHECK(r.window(T_ROM_WIN, T_ROM_BYTES) == std1);
            CHECK_EQ(o.msg[0], 0);
            // a start with JiffyDOS: the parts by sha-free byte compare, the characters written once, flush after the last write and before the release
            Rig j;
            j.image();
            put(j, "c64.bin", c64part(2));
            put(j, "c128.bin", c128part(3));
            sel(j, "c128.use_jiffy_c128", "c128.bin", &o);
            sel(j, "c128.use_jiffy_c64", "c64.bin", &o);
            j.set("ROM set", 1);
            CHECK(j.boot());
            CHECK(j.window(T_ROM_WIN, T_ROM_BYTES) == want);
            CHECK_EQ(j.io.chr_writes, 8192);
            CHECK(memcmp(j.io.chr, std1.data() + 0x10000, 8192) == 0);
            int w_last = -1, f_last = -1, rel = -1;
            for (size_t i = 0; i < j.io.events.size(); i++) {
                if (j.io.events[i] == "ddr_write") w_last = (int)i;
                if (j.io.events[i] == "flush") f_last = (int)i;
                if (j.io.events[i] == "mode_unreset") rel = (int)i;
            }
            CHECK(w_last >= 0 && f_last > w_last && rel > f_last);
            // the C128DCR kit's one file (32768: the C64 part first) does the work of both
            Rig d;
            d.image();
            put(d, "dcr.bin", cat(c64part(2), c128part(3)));
            CHECK_EQ(sel(d, "c128.use_jiffy_c128", "dcr.bin", &o), OPE_OK);
            d.set("ROM set", 1);
            CHECK(d.boot());
            CHECK(d.window(T_ROM_WIN, T_ROM_BYTES) == want);
            // the C128 part alone: the C64 part stays standard, and the message says so
            Rig h;
            h.image();
            put(h, "c128.bin", c128part(3));
            sel(h, "c128.use_jiffy_c128", "c128.bin", &o);
            h.set("ROM set", 1);
            CHECK(h.boot());
            Bytes hwant = std1;
            memcpy(&hwant[0x4000], c128part(3).data(), 16384);
            CHECK(h.window(T_ROM_WIN, T_ROM_BYTES) == hwant);
            CHECK(strstr(h.c.notice, "not selected") == 0 && h.c.notice[0] == 0);   // an unselected part is no notice
            // the C64 part alone: without the C128 part the whole image stays standard
            Rig a;
            a.image();
            put(a, "c64.bin", c64part(2));
            sel(a, "c128.use_jiffy_c64", "c64.bin", &o);
            a.set("ROM set", 1);
            CHECK(a.boot());
            CHECK(a.window(T_ROM_WIN, T_ROM_BYTES) == std1);
            CHECK(strstr(a.c.notice, "not selected") == 0 && a.c.notice[0] == 0);
            // the window held the standard image all along: choosing Standard writes nothing again
            size_t dwa = a.io.dw.size();
            setrom(a, 0, &o);
            CHECK_EQ(a.io.dw.size(), dwa);
            CHECK_EQ(o.msg[0], 0);
            // the C64 KERNAL alone (8192): at +0x2000, the BASIC half of the C64 part stays standard
            Rig k;
            k.image();
            put(k, "k8.bin", c64k8(4));
            put(k, "c128.bin", c128part(3));
            CHECK_EQ(sel(k, "c128.use_jiffy_c64", "k8.bin", &o), OPE_OK);
            sel(k, "c128.use_jiffy_c128", "c128.bin", &o);
            k.set("ROM set", 1);
            CHECK(k.boot());
            Bytes kwant = std1;
            memcpy(&kwant[0x2000], c64k8(4).data(), 8192);
            memcpy(&kwant[0x4000], c128part(3).data(), 16384);
            Bytes kw = k.window(T_ROM_WIN, T_ROM_BYTES);
            CHECK(kw == kwant);
            CHECK(memcmp(&kw[0], &std1[0], 0x2000) == 0);                    // nothing lands at +0x0000
            // clearing: "" selects nothing again
            { OpIn in = in_str("path", ""); CHECK_EQ(ops_call(&k.c, "c128.use_jiffy_c64", &in, 1, &o), OPE_OK); }
            CHECK_STR(txt(k, "JiffyDOS C64 ROM").c_str(), "");
            CHECK(strstr(o.msg, "no file selected") != 0);
        }
        // a selected file that is gone at the start: that part stays Stock, the line gives the reason
        {
            Rig r;
            r.image();
            put(r, "c64.bin", c64part(2));
            put(r, "c128.bin", c128part(3));
            OpResult o;
            sel(r, "c128.use_jiffy_c128", "c128.bin", &o);
            sel(r, "c128.use_jiffy_c64", "c64.bin", &o);
            r.fs.files.erase(path("c64.bin"));
            r.set("ROM set", 1);
            CHECK(r.boot());
            Bytes hwant = std1;
            memcpy(&hwant[0x4000], c128part(3).data(), 16384);
            CHECK(r.window(T_ROM_WIN, T_ROM_BYTES) == hwant);
            CHECK(strstr(r.c.notice, "JiffyDOS C64 ROM: cannot open the file") != 0);
            r.fs.files.erase(path("c128.bin"));
            setrom(r, 1, &o);
            CHECK(r.window(T_ROM_WIN, T_ROM_BYTES) == std1);
            CHECK(strstr(o.msg, "JiffyDOS C128 ROM: cannot open the file") != 0);
        }
        // refused sizes and contents: the reason, nothing kept, nothing written
        {
            Rig r;
            r.image();
            CHECK(r.boot());
            size_t dws = r.io.dw.size();
            OpResult o;
            Bytes bad = c128part(3); bad[0x3FFD] = 0x80;
            put(r, "s16383", Bytes(16383, 0x55));
            put(r, "s8192", Bytes(8192, 0x55));
            put(r, "zero", Bytes(16384, 0x00));
            put(r, "c64part", c64part(2));
            put(r, "c128part", c128part(3));
            put(r, "badvec", bad);
            { Bytes k = c64k8(4); k[0x1FFC] = 0x3D; k[0x1FFD] = 0xFF; put(r, "k8bad", k); }
            put(r, "s16384", Bytes(16384, 1));
            put(r, "dcr_rev", cat(c128part(3), c64part(2)));
            put(r, "k8ok", c64k8(4));
            struct Ref { const char *op, *file, *word; int rc; };
            Ref refs[] = {
                { "c128.use_jiffy_c128", "s16383", "16383 bytes", OPE_FAILED },
                { "c128.use_jiffy_c128", "s8192", "8192 bytes", OPE_FAILED },
                { "c128.use_jiffy_c128", "zero", "empty", OPE_FAILED },
                { "c128.use_jiffy_c128", "c64part", "C64 part", OPE_FAILED },
                { "c128.use_jiffy_c128", "badvec", "reset vector", OPE_FAILED },
                { "c128.use_jiffy_c128", "dcr_rev", "C64", OPE_FAILED },
                { "c128.use_jiffy_c128", "k8ok", "8192 bytes", OPE_FAILED },
                { "c128.use_jiffy_c64", "c128part", "no C64 BASIC", OPE_FAILED },
                { "c128.use_jiffy_c64", "s16383", "16383 bytes", OPE_FAILED },
                { "c128.use_jiffy_c64", "k8bad", "$FCE2", OPE_FAILED },
                { "c128.use_jiffy_1541", "s8192", "upper half", OPE_FAILED },
                { "c128.use_jiffy_1541", "s16383", "16383 bytes", OPE_FAILED },
                { "c128.use_jiffy_1541", "zero", "empty", OPE_FAILED },
                { "c128.use_jiffy_1581", "s16384", "32768", OPE_FAILED },
                { "c128.use_jiffy_c128", "nofile", "cannot open", OPE_FAILED },
            };
            for (size_t i = 0; i < sizeof(refs) / sizeof(refs[0]); i++) {
                CHECK_EQ(sel(r, refs[i].op, refs[i].file, &o), refs[i].rc);
                if (!strstr(o.msg, refs[i].word)) { printf("  refusal %zu (%s %s): \"%s\"\n", i, refs[i].op, refs[i].file, o.msg); }
                CHECK(strstr(o.msg, refs[i].word) != 0);
                CHECK(strstr(o.msg, refs[i].file) != 0);                     // the file's name is in the line
            }
            CHECK_STR(txt(r, "JiffyDOS C128 ROM").c_str(), "");
            CHECK_STR(txt(r, "JiffyDOS C64 ROM").c_str(), "");
            CHECK_STR(txt(r, "JiffyDOS 1541 ROM").c_str(), "");
            CHECK_STR(txt(r, "JiffyDOS 1581 ROM").c_str(), "");
            CHECK_EQ(r.io.dw.size(), dws);
            // control: the right file in the same place is taken
            CHECK_EQ(sel(r, "c128.use_jiffy_c128", "c128part", &o), OPE_OK);
            CHECK_STR(txt(r, "JiffyDOS C128 ROM").c_str(), "/usb0/kit/c128part");
            // a refused file replaces nothing already selected
            CHECK_EQ(sel(r, "c128.use_jiffy_c128", "badvec", &o), OPE_FAILED);
            CHECK_STR(txt(r, "JiffyDOS C128 ROM").c_str(), "/usb0/kit/c128part");
        }
        // drives: the 1541 and 1581 ROMs selected; with JiffyDOS drive A holds the JiffyDOS ROM of its type, with Stock the stock one; not selected: the line, stock
        {
            Bytes s41 = pattern(5, 32768), j41 = pattern(7, 32768), s81 = pattern(6, 32768), j81 = pattern(8, 32768);
            auto rig = [&](bool j41f, bool j81f, bool kit = true) {
                Rig *r = new Rig();
                r->io.dm.multi = true;
                r->image();
                OpResult o;
                if (kit) { put(*r, "c64.bin", c64part(2)); put(*r, "c128.bin", c128part(3)); sel(*r, "c128.use_jiffy_c64", "c64.bin", &o); sel(*r, "c128.use_jiffy_c128", "c128.bin", &o); }
                r->fs.put("/usb0/a.d81", Bytes(819200, 0));
                r->fs.put("/usb0/a.d64", Bytes(174848, 0));
                r->fs.put("/usb0/roms/c128/1541.rom", s41);
                r->fs.put("/usb0/roms/c128/1581.rom", s81);
                if (j41f) { put(*r, "j41.bin", j41); CHECK_EQ(sel(*r, "c128.use_jiffy_1541", "j41.bin", &o), OPE_OK); }
                if (j81f) { put(*r, "j81.bin", j81); CHECK_EQ(sel(*r, "c128.use_jiffy_1581", "j81.bin", &o), OPE_OK); }
                return r;
            };
            Rig *r = rig(true, true);
            CHECK(r->boot());
            CHECK(r->io.dm.rom() == s41);                                    // Stock, the selection made: stock
            OpResult o;
            setrom(*r, 1, &o);
            CHECK(r->io.dm.rom() == j41);
            CHECK_EQ(o.msg[0], 0);
            OpIn m1[3] = { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/a.d81"), in_str("mode", "rw") };
            CHECK_EQ(ops_call(&r->c, "drive.mount", m1, 3, &res), OPE_OK);
            CHECK(r->io.dm.rom() == j81);
            setrom(*r, 0, &o);
            CHECK(r->io.dm.rom() == s81);
            setrom(*r, 1, &o);
            CHECK(r->io.dm.rom() == j81);
            OpIn m2[3] = { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/a.d64"), in_str("mode", "rw") };
            CHECK_EQ(ops_call(&r->c, "drive.mount", m2, 3, &res), OPE_OK);
            CHECK(r->io.dm.rom() == j41);
            setrom(*r, 0, &o);
            CHECK(r->io.dm.rom() == s41);
            delete (CbmDrive *)r->c.drive; r->c.drive = 0;
            delete r;
            // the start with JiffyDOS: the drive holds the JiffyDOS ROM from the first cycle
            r = rig(true, true);
            r->set("ROM set", 1);
            CHECK(r->boot());
            CHECK(r->io.dm.rom() == j41);
            delete (CbmDrive *)r->c.drive; r->c.drive = 0;
            delete r;
            // JiffyDOS chosen, no 1541 file selected: no line, the stock ROM stays; the 1581's file is taken
            r = rig(false, true);
            CHECK(r->boot());
            setrom(*r, 1, &o);
            CHECK(strstr(o.msg, "not selected") == 0);
            CHECK(r->io.dm.rom() == s41);
            CHECK_EQ(ops_call(&r->c, "drive.mount", m1, 3, &res), OPE_OK);
            setrom(*r, 1, &o);
            CHECK(r->io.dm.rom() == j81);
            delete (CbmDrive *)r->c.drive; r->c.drive = 0;
            delete r;
            // nothing selected at all: one line, Stock everywhere
            r = rig(false, false, false);
            CHECK(r->boot());
            setrom(*r, 1, &o);
            CHECK_STR(o.msg, "ROM set JiffyDOS: no file selected, the machine starts Stock");
            CHECK(r->io.dm.rom() == s41);
            delete (CbmDrive *)r->c.drive; r->c.drive = 0;
            delete r;
            // "Set as 1541 ROM" in the browser replaces the stock 1541 ROM; with JiffyDOS chosen the drive keeps its JiffyDOS ROM
            r = rig(true, true);
            CHECK(r->boot());
            Bytes other = pattern(11, 32768);
            r->fs.put("/usb0/other/d.rom", other);
            const FileAction *act = 0;
            for (int i = 0; i < c128_desc.nfiletypes; i++) {
                if (!strcmp(c128_desc.filetypes[i].ext, ".rom")) act = &c128_desc.filetypes[i].actions[1];
            }
            CHECK(act != 0);
            if (act) {
                CHECK_STR(act->op, "c128.set_drive_rom");
                CHECK_EQ(ops_run_bound(&r->c, act->op, act->bind, act->nbind, "/usb0/other/d.rom", &res), OPE_OK);
                CHECK(r->io.dm.rom() == other);
                setrom(*r, 1, &o);
                CHECK(r->io.dm.rom() == j41);
            }
            delete (CbmDrive *)r->c.drive; r->c.drive = 0;
            delete r;
        }
        // the file actions of the browser: .rom and .bin offer the four "Use as JiffyDOS" choices, bound to the operations, the path is the file
        {
            const char *want_ops[4] = { "c128.use_jiffy_c128", "c128.use_jiffy_c64", "c128.use_jiffy_1541", "c128.use_jiffy_1581" };
            const char *exts[2] = { ".rom", ".bin" };
            for (int e = 0; e < 2; e++) {
                const FileType *ft = 0;
                for (int i = 0; i < c128_desc.nfiletypes; i++) if (!strcmp(c128_desc.filetypes[i].ext, exts[e])) ft = &c128_desc.filetypes[i];
                CHECK(ft != 0);
                if (!ft) continue;
                for (int k = 0; k < 4; k++) {
                    bool found = false;
                    for (int a = 0; a < ft->nactions; a++) {
                        if (!strcmp(ft->actions[a].op, want_ops[k])) found = true;
                    }
                    CHECK(found);
                }
            }
            Rig r;
            r.image();
            r.fs.put("/usb0/x/kit.bin", c128part(3));
            const FileType *ft = 0;
            for (int i = 0; i < c128_desc.nfiletypes; i++) if (!strcmp(c128_desc.filetypes[i].ext, ".bin")) ft = &c128_desc.filetypes[i];
            if (ft) {
                for (int a = 0; a < ft->nactions; a++) {
                    if (!strcmp(ft->actions[a].op, "c128.use_jiffy_c128")) {
                        CHECK_EQ(ops_run_bound(&r.c, ft->actions[a].op, ft->actions[a].bind, ft->actions[a].nbind, "/usb0/x/kit.bin", &res), OPE_OK);
                    }
                }
            }
            CHECK_STR(txt(r, "JiffyDOS C128 ROM").c_str(), "/usb0/x/kit.bin");
        }
        // the browser's image replaces the standard one underneath the selected parts
        {
            Rig r;
            r.image();
            r.fs.put("/usb0/kit/c128.bin", c128part(3));
            OpResult o;
            sel(r, "c128.use_jiffy_c128", "c128.bin", &o);
            r.set("ROM set", 1);
            CHECK(r.boot());
            Bytes std9 = pattern(9, T_ROM_BYTES), want9 = std9;
            memcpy(&want9[0x4000], c128part(3).data(), 16384);
            OpIn in9 = in_blob("data", std9.data(), (uint32_t)std9.size());
            CHECK_EQ(ops_call(&r.c, "c128.put_rom", &in9, 1, &res), OPE_OK);
            CHECK(r.window(T_ROM_WIN, T_ROM_BYTES) == want9);
            CHECK(memcmp(r.io.chr, std9.data() + 0x10000, 8192) == 0);
        }
    }
    // ---- T185 D4: the file selector of the settings rows (MenuCtl). F5 Settings: the four rows say "Not selected"; RETURN opens the selector with "No file" on
    // top; a file is taken when the core's operation accepts it (the selector closes, the row shows the file's name), refused with the reason it stays open
    // and the status line says why; "No file" clears. Control: the refused file leaves the selection as it was.
    {
        struct Host : MenuHost {
            void info(int, char *out, int) { out[0] = 0; }
            bool return_to_host(char *msg, int n) { snprintf(msg, n, "no"); return false; }
        };
        Rig r;
        Host host;
        r.image();
        Bytes good = pattern(3, 16384); memset(&good[4], 0x11, 8); good[0x3FFC] = 0x3D; good[0x3FFD] = 0xFF;
        r.fs.put("/usb0/kit/good.bin", good);
        r.fs.put("/usb0/kit/short.bin", Bytes(8192, 0x55));
        r.fs.put("/usb0/kit/other.bin", good);
        MenuCtl m(&r.c, &host);
        r.st->load();
        m.open();
        m.key(KEY_F5);
        m.key('2');
        auto left = [&](int i) { char l[300], v[100]; bool a; m.row(i, l, 300, v, 100, &a); return std::string(l); };
        auto value = [&](int i) { char l[300], v[100]; bool a; m.row(i, l, 300, v, 100, &a); return std::string(v); };
        auto find = [&](const char *t) { for (int i = 0; i < m.rows(); i++) if (left(i) == t) return i; return -1; };
        auto go = [&](const char *t) { int i = find(t); CHECK(i >= 0); if (i < 0) return false; for (int g = 0; g < 400 && m.selected() > i; g++) m.key(KEY_UP); for (int g = 0; g < 400 && m.selected() < i; g++) m.key(KEY_DOWN); return true; };
        const char *rows4[4] = { "JiffyDOS C128 ROM", "JiffyDOS C64 ROM", "JiffyDOS 1541 ROM", "JiffyDOS 1581 ROM" };
        for (int k = 0; k < 4; k++) {
            int i = find(rows4[k]);
            CHECK(i >= 0);
            if (i >= 0) CHECK_STR(value(i).c_str(), "Not selected");
        }
        CHECK(go("JiffyDOS C128 ROM"));
        m.key(KEY_RETURN);
        CHECK(m.picking());
        CHECK_STR(left(0).c_str(), "No file");
        CHECK(strstr(m.status(), "JiffyDOS C128 ROM") != 0);
        // to the folder: from where the selector opened, by the names of the rows
        if (find("kit/") < 0 && go("usb0/")) m.key(KEY_RETURN);
        CHECK(go("kit/"));
        m.key(KEY_RETURN);
        CHECK(find("good.bin") >= 0 && find("short.bin") >= 0);              // files are listed (the folder browser of a task lists none)
        // the refused file: the selector stays, the status says why, nothing is selected
        CHECK(go("short.bin"));
        m.key(KEY_RETURN);
        CHECK(m.picking());
        CHECK(m.status_bad());
        CHECK(strstr(m.status(), "short.bin") != 0 && strstr(m.status(), "8192 bytes") != 0);
        CHECK_STR(r.st->text(r.st->find("JiffyDOS C128 ROM")), "");
        // the good one: taken, the selector closes
        CHECK(go("good.bin"));
        m.key(KEY_RETURN);
        CHECK(!m.picking());
        CHECK_STR(r.st->text(r.st->find("JiffyDOS C128 ROM")), "/usb0/kit/good.bin");
        CHECK_STR(value(find("JiffyDOS C128 ROM")).c_str(), "good.bin");
        CHECK(!m.status_bad());
        // reopened: it opens in the folder of the file selected, "No file" clears it
        CHECK(go("JiffyDOS C128 ROM"));
        m.key(KEY_RETURN);
        CHECK(m.picking());
        CHECK(find("good.bin") >= 0);
        for (int g = 0; g < 400 && m.selected() > 0; g++) m.key(KEY_UP);
        m.key(KEY_RETURN);
        CHECK(!m.picking());
        CHECK_STR(r.st->text(r.st->find("JiffyDOS C128 ROM")), "");
        CHECK_STR(value(find("JiffyDOS C128 ROM")).c_str(), "Not selected");
        // cancelling (ESC) changes nothing
        CHECK(go("JiffyDOS 1581 ROM"));
        m.key(KEY_RETURN);
        CHECK(m.picking());
        m.key(KEY_ESCAPE);
        CHECK(!m.picking());
        CHECK_STR(r.st->text(r.st->find("JiffyDOS 1581 ROM")), "");
    }

    // ---- T206 S1: Run Disk. The disk is mounted, the machine reset, and LOAD"*",8,1 / RUN typed once BASIC 7 is up ($1C01 in TXTTAB $2D/$2E), into
    // the C128's keyboard buffer KEYD $034A with its count NDX $D0 (ten keys at most per piece) - not into the C64's $0277 / $C6.
    {
        Rig r;
        r.io.dm.multi = true;
        r.image();
        r.fs.put("/usb0/run.d64", Bytes(174848, 0));
        r.fs.put("/usb0/run.d81", Bytes(819200, 0));
        r.fs.put("/usb0/roms/c128/1541.rom", pattern(5, 32768));
        r.fs.put("/usb0/roms/c128/1581.rom", pattern(6, 32768));
        CHECK(r.boot());
        CHECK(c128_desc.type_op && !strcmp(c128_desc.type_op, "c128.type_script"));
        for (const char *ext : { ".d64", ".g64", ".d81" }) {
            for (int i = 0; i < c128_desc.nfiletypes; i++) {
                if (strcmp(c128_desc.filetypes[i].ext, ext)) continue;
                bool run = false;
                for (int k = 0; k < c128_desc.filetypes[i].nactions; k++) run = run || !strcmp(c128_desc.filetypes[i].actions[k].label, "Run Disk");
                CHECK(run);
            }
        }
        // two machines: BASIC 7 (the RAM already holds TXTTAB $1C01 from the run before, so nothing may be typed before the 2 s of ready_ms), and the
        // machine in C64 mode (TXTTAB $0801: the READY of another BASIC, nothing is typed, the notice says so)
        for (int machine = 0; machine < 2; machine++) {
            for (const char *img : { "/usb0/run.d64", "/usb0/run.d81" }) {
                r.io.events.clear();
                r.io.ram[0x2D] = 0x01; r.io.ram[0x2E] = (uint8_t)(machine ? 0x08 : 0x1C);
                r.io.ram[0xD0] = 0;
                r.io.ram[0xC6] = 0;
                OpIn in[2] = { in_str("slot", "iec:drive_a"), in_str("path", img) };
                OpResult res;
                memset(&res, 0, sizeof(res));
                CHECK_EQ(ops_call(&r.c, "drive.mount_run", in, 2, &res), OPE_OK);
                bool reset = false;
                for (auto &e : r.io.events) reset = reset || e == "mode_reset";
                CHECK(reset);
                std::string typed;
                uint32_t t0 = r.io.now_ms();
                bool early = false;
                for (int i = 0; i < 400; i++) {
                    r.io.delay_ms(50);
                    ops_call(&r.c, "c128.service", 0, 0, &res);
                    if (r.io.ram[0xD0]) {
                        if (r.io.now_ms() - t0 < 2000) early = true;
                        typed += std::string((const char *)&r.io.ram[0x034A], r.io.ram[0xD0]);
                        r.io.ram[0xD0] = 0;
                    }
                }
                CHECK(!early);                                                   // not before ready_ms, even with a pointer that already says BASIC
                CHECK_EQ(r.io.ram[0xC6], 0);                                      // the C64's buffer is not used
                if (machine == 1) {
                    CHECK(typed.empty());                                         // TXTTAB $0801: not the C128's BASIC 7
                    CHECK(r.c.notice_pending && strstr(r.c.notice, "did not reach READY"));
                    r.c.notice_pending = false;
                } else {
                    CHECK(typed == "LOAD\"*\",8,1\rRUN\r");
                }
            }
        }
    }

    DONE("test_c128");
}
