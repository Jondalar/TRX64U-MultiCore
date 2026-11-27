// T93 M4: the NES hooks against a model of the register block: header into the INES registers, image into the DDR2 window,
// the order (held first, released last), the settings, and what a refused file does not touch.
#include "check.h"
#include "fakes.h"
#include "boot.h"
#include "cores.h"
#include "nes_regs.h"
#include "ops_glue.h"

extern const CoreDesc nes_desc;                 // cores.h lists the shared app's cores; the NES is its own app (cores/nes/app/lib)

// the ".nes" file action, called the way the browser calls it
static int run_file(CoreCtx *c, const char *path, char *msg, int n)
{
    const FileAction &a = nes_desc.filetypes[0].actions[0];
    OpResult r;
    int rc = ops_run_bound(c, a.op, a.bind, a.nbind, path, &r);
    snprintf(msg, n, "%s", r.msg);
    return rc;
}

struct NesIo : FakeIo { };

static std::vector<uint8_t> nes_file(int prg_pages, int chr_pages, int f6, int f7, bool trainer = false)
{
    std::vector<uint8_t> v(16, 0);
    memcpy(v.data(), "NES\x1A", 4);
    v[4] = prg_pages; v[5] = chr_pages; v[6] = f6 | (trainer ? 4 : 0); v[7] = f7;
    if (trainer) v.insert(v.end(), 512, 0xEE);
    for (int i = 0; i < prg_pages * 16384; i++) v.push_back((uint8_t)(i * 7 + 1));
    for (int i = 0; i < chr_pages * 8192; i++) v.push_back((uint8_t)(i * 11 + 2));
    return v;
}

int main()
{
    CHECK(!nes_desc.board_caps);                    // T104.2: the NES lists what it has and asks for no striking (the monolith's probes are test_board's)
    NesIo io; FakeFs fs; PxRec px;
    CoreCtx c; memset(&c, 0, sizeof(c));
    c.desc = &nes_desc; c.io = &io; c.fs = &fs;
    Settings st(nes_desc.settings, nes_desc.nsettings, nes_desc.store, &fs, "/sd/nes.cfg", 0);
    st.load();
    c.settings = &st;
    Video vid(nes_desc.video, &px, &io);
    c.video = &vid;
    char msg[300];

    // before any program: the start sequence holds and waits, never releases; a reset says what is missing and does nothing
    struct U : BootUi { void message(const char *, const char *, bool) { } void clear() { } void tick() { } } ui;
    CHECK_EQ(boot_start(&c, &ui), BOOT_WAITING_PROGRAM);
    CHECK_EQ(io.reset_releases, 0);
    CHECK(io.reset_asserts >= 1);
    size_t ev = io.events.size();
    CHECK_EQ(boot_cold_reset(&c, msg, sizeof(msg)), 1);
    CHECK(strstr(msg, ".nes") != 0);
    CHECK_EQ(io.events.size(), ev);
    CHECK(!c.started);

    // mapper 2, 128 KB PRG, CHR RAM, vertical: the file hook
    auto g = nes_file(8, 0, 0x21, 0);
    fs.put("/usb0/g.nes", g);
    st.set(0, 1);                                            // PAL
    io.events.clear();
    size_t n0 = io.log.size();
    CHECK_EQ(run_file(&c, "/usb0/g.nes", msg, sizeof(msg)), 0);
    CHECK(c.program_loaded && c.started);
    if (!c.program_loaded || !c.started) {
        // Everything below reads the state that load was supposed to produce; without it the test would
        // walk into a null name and the run would die instead of reporting (control nes_loaded, run 56).
        printf("test_nes: %d checks, %d failed\n", g_checks, g_fails);
        return 1;
    }
    CHECK_STR(io.events.front().c_str(), "mode_reset");
    CHECK_STR(io.events.back().c_str(), "mode_unreset");
    CHECK_EQ(io.reset_releases, 1);
    for (int i = 0; i < 16; i++) CHECK_EQ(io.regs[NES_INES + i], g[i]);          // the header bytes, as they are
    CHECK_EQ(io.regs[NES_SYSTEM], 1);                                            // PAL
    CHECK_EQ(io.regs[NES_CART_BASE + 3], 0x01);
    bool prg_ok = true;
    for (int i = 0; i < 131072; i++) prg_ok &= io.ddr[NES_W_PRG + i] == g[16 + i];
    CHECK(prg_ok);
    // every header byte and the SYSTEM register were written before the first image byte can matter, and CTRL is 0 last
    int hold_at = -1, rel_at = -1, ines_last = -1;
    for (size_t i = n0; i < io.log.size(); i++) {
        if (io.log[i].reg == NES_CTRL && io.log[i].v == NES_CTRL_HOLD && hold_at < 0) hold_at = (int)i;
        if (io.log[i].reg == NES_CTRL && io.log[i].v == 0) rel_at = (int)i;
        if (io.log[i].reg >= NES_INES && io.log[i].reg < NES_INES + 16) ines_last = (int)i;
    }
    CHECK(hold_at >= 0 && hold_at < ines_last && ines_last < rel_at);
    CHECK_EQ(rel_at, (int)io.log.size() - 1);
    CHECK(vid.ready());
    CHECK_STR(ops_state(&c, nes_desc.media[0].state), "g.nes");

    // PRG and CHR in their places, with a trainer in the file (skipped), CHR ROM at +0x200000
    auto t = nes_file(2, 1, 0x10, 0, true);
    fs.put("/usb0/t.nes", t);
    CHECK_EQ(run_file(&c, "/usb0/t.nes", msg, sizeof(msg)), 0);
    bool ok = true;
    for (int i = 0; i < 32768; i++) ok &= io.ddr[NES_W_PRG + i] == (uint8_t)(i * 7 + 1);
    for (int i = 0; i < 8192; i++) ok &= io.ddr[NES_W_CHR + i] == (uint8_t)(i * 11 + 2);
    CHECK(ok);
    CHECK_EQ(io.regs[NES_INES + 6], 0x14);                                       // the trainer bit stays in the header the core sees

    // a refused file does not touch the machine: not iNES, unsupported mapper (5), too short
    io.events.clear();
    n0 = io.log.size();
    fs.put("/usb0/x.nes", std::vector<uint8_t>(100, 0x55));
    CHECK_EQ(run_file(&c, "/usb0/x.nes", msg, sizeof(msg)), OPE_FAILED);
    CHECK(strstr(msg, "not an iNES") != 0);
    fs.put("/usb0/m5.nes", nes_file(2, 1, 0x50, 0));
    CHECK_EQ(run_file(&c, "/usb0/m5.nes", msg, sizeof(msg)), OPE_FAILED);
    CHECK(strstr(msg, "mapper 5") != 0);
    auto sh = nes_file(2, 1, 0, 0); sh.resize(sh.size() - 1);
    fs.put("/usb0/s.nes", sh);
    CHECK_EQ(run_file(&c, "/usb0/s.nes", msg, sizeof(msg)), OPE_FAILED);
    CHECK_EQ(run_file(&c, "/usb0/missing.nes", msg, sizeof(msg)), OPE_FAILED);
    CHECK(io.events.empty());
    CHECK_EQ(io.log.size(), n0);
    CHECK(c.program_loaded);                                                     // the old program is still the one that runs
    CHECK_STR(ops_state(&c, nes_desc.media[0].state), "t.nes");

    // the stick is pulled while the image is copied: held, told, not released, no program
    fs.put("/usb0/p.nes", nes_file(4, 1, 0x20, 0));
    io.events.clear();
    int rel0 = io.reset_releases;
    fs.read_budget = 3;                                       // the header, then two chunks of the PRG
    CHECK_EQ(run_file(&c, "/usb0/p.nes", msg, sizeof(msg)), OPE_FAILED);
    fs.read_budget = -1;
    CHECK(strstr(msg, "medium is gone") != 0);
    CHECK_EQ(io.reset_releases, rel0);
    CHECK(!c.program_loaded);
    CHECK(ops_state(&c, nes_desc.media[0].state) == 0);
    CHECK_STR(io.events.front().c_str(), "mode_reset");
    // a reset with no program does nothing, as before the first load
    ev = io.events.size();
    CHECK_EQ(boot_cold_reset(&c, msg, sizeof(msg)), 1);
    CHECK_EQ(io.events.size(), ev);
    // and a good file afterwards recovers the machine
    CHECK_EQ(run_file(&c, "/usb0/g.nes", msg, sizeof(msg)), 0);
    CHECK_EQ(io.reset_releases, rel0 + 1);
    CHECK(c.program_loaded);
    // Reset after a program is the shared cold reset: held, SYSTEM written, released
    io.events.clear();
    CHECK_EQ(boot_cold_reset(&c, msg, sizeof(msg)), 0);
    CHECK_STR(io.events.front().c_str(), "mode_reset");
    CHECK_STR(io.events.back().c_str(), "mode_unreset");
    DONE("test_nes");
}
