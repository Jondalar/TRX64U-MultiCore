// T119 5.13: the ZX Spectrum core application's start and model switch (coreapp/cores/zx.cc + boot.cc), the real descriptor against a model of the
// register block: CTRL b0 hold, MODEL, the ROM through WIN_PAGE + the aperture. The ROMs are 16,384 / 32,768 bytes; when the environment names
// a folder with the genuine 48.rom / 128.rom (ZX_ROM_DIR, never in the repository) those bytes are used, else a pattern of the same size.
// Every scenario checks the same thing the board needs: the core ends released (CTRL b0 clear, host reset line off), the model register
// and the ROM image are the chosen model's, and the ROM went in while the core was held.
#include <string>
#include <fstream>
#include <iterator>
#include "check.h"
#include "fakes.h"
#include "boot.h"
#include "cores.h"
#include "zx_regs.h"

struct Ui : BootUi {
    int msgs = 0, ticks = 0;
    void message(const char *, const char *, bool) { msgs++; }
    void clear() { }
    void tick() { ticks++; }
};

static std::vector<uint8_t> rom_bytes(const char *name, int n, int seed)
{
    std::vector<uint8_t> v(n);
    const char *dir = getenv("ZX_ROM_DIR");
    if (dir) {
        std::ifstream f(std::string(dir) + "/" + name, std::ios::binary);
        std::vector<uint8_t> r((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if ((int)r.size() == n) return r;
    }
    for (int i = 0; i < n; i++) v[i] = (uint8_t)(i * seed + (i >> 8) + 1);
    return v;
}

struct ZxIo : FakeIo {
    ZxIo() { rom_sel_reg = ZX_WIN_PAGE; }
    bool held() { return (regs[ZX_CTRL] & ZX_CTRL_HOLD) || in_reset; }
};

struct Rig {
    ZxIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *v;
    std::vector<uint8_t> r48, r128;
    Rig(const char *cfg_model)
    {
        memset(&c, 0, sizeof(c));
        r48 = rom_bytes("48.rom", 16384, 7);
        r128 = rom_bytes("128.rom", 32768, 13);
        fs.put("/SD/cores/zx/roms/48.rom", r48);
        fs.put("/SD/cores/zx/roms/128.rom", r128);
        if (cfg_model) {
            std::string t = std::string("[ZX Spectrum Settings]\nModel=") + cfg_model + "\n\n";
            fs.put_text("/SD/cores/zx.cfg", t.c_str());
        }
        strcpy(c.cor_path, "/SD/cores/zx.cor");
        c.desc = &zx_desc;
        c.io = &io;
        c.fs = &fs;
        st = new Settings(zx_desc.settings, zx_desc.nsettings, zx_desc.store, &fs, "/SD/cores/zx.cfg", 0);
        st->load();
        v = new Video(zx_desc.video, &px, &io);
        c.settings = st;
        c.video = v;
    }
    bool rom_is(int model)
    {
        const std::vector<uint8_t> &a = model ? r128 : r48;
        for (size_t off = 0; off < a.size(); off += 16384) {
            const std::vector<uint8_t> &arr = io.rom[model ? (off ? 2 : 0) : 0];
            if (memcmp(&arr[0], &a[off], 16384) != 0) return false;
        }
        return true;
    }
    void dump(const char *tag)
    {
        printf("seq| %s:", tag);
        int n = 0;
        for (auto &w : io.log) {
            if (w.reg == ZX_CTRL || w.reg == ZX_MODEL || w.reg == ZX_WIN_PAGE) {
                if (n++ < 40) printf(" %03x=%02x", w.reg, w.v);
            }
        }
        printf("  | resets %d releases %d, final CTRL %02x MODEL %d held %d\n", io.reset_asserts, io.reset_releases, io.regs[ZX_CTRL], io.regs[ZX_MODEL], io.held());
    }
};

static void expect_running(Rig &r, int model, const char *tag)
{
    printf("%s: model %d  ", tag, model);
    r.dump(tag);
    CHECK(!r.io.held());
    CHECK_EQ(r.io.regs[ZX_CTRL] & ZX_CTRL_HOLD, 0);
    CHECK_EQ(r.io.regs[ZX_MODEL], model);
    CHECK(r.rom_is(model));
    CHECK(r.c.started);
}

int main()
{
    char msg[240];
    // cold start, 48K (the default) and 128K
    for (int m = 0; m < 2; m++) {
        Rig r(m ? "128K" : "48K");
        Ui ui;
        CHECK_EQ(boot_start(&r.c, &ui), BOOT_RUNNING);
        CHECK_EQ(ui.msgs, 0);
        expect_running(r, m, m ? "cold128" : "cold48");
        CHECK_EQ(r.io.reset_releases, 1);
    }
    // no cfg at all: the default model
    {
        Rig r(0);
        Ui ui;
        CHECK_EQ(boot_start(&r.c, &ui), BOOT_RUNNING);
        expect_running(r, 0, "cold_default");
    }
    // a running 128K, the menu sets Model = 48K (taken at the next reset), then the reset: 128K -> 48K and back
    for (int m = 0; m < 2; m++) {
        Rig r(m ? "48K" : "128K");
        Ui ui;
        CHECK_EQ(boot_start(&r.c, &ui), BOOT_RUNNING);
        expect_running(r, m ? 0 : 1, "pre");
        r.io.log.clear();
        r.io.reset_asserts = r.io.reset_releases = 0;
        int mi = 0;                                           // S_MODEL
        r.st->set(mi, m ? 1 : 0);                             // 128K <-> 48K (value index = model)
        CHECK_EQ(boot_cold_reset(&r.c, msg, sizeof(msg)), 0);
        expect_running(r, m ? 1 : 0, m ? "switch48to128" : "switch128to48");
        CHECK_EQ(r.io.reset_releases, 1);
        // the ROM went in while the core was held: no ROM write after the last release
        CHECK_STR(r.io.events.back().c_str(), "mode_unreset");
    }
    // a model whose ROM is missing: the other one runs, released all the same
    {
        Rig r("48K");
        r.fs.files.erase("/sd/cores/zx/roms/48.rom");
        Ui ui;
        CHECK_EQ(boot_start(&r.c, &ui), BOOT_RUNNING);
        expect_running(r, 1, "no48");
    }
    DONE("boot_zx");
}
