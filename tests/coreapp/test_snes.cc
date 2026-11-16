// T142 S9/S10/S12: the SNES descriptor (coreapp/cores/snes.cc) against a model of the register block of cores/snes/trx/snes_regs.v: the boot ROM that goes into
// the S-SMP, an image run from a file (header decisions in the registers, the image, its mirror and the RAMs in the DDR2 window, the order of it all, the
// release last), the cartridges the core refuses (the machine is not touched), the battery RAM and its save file (loaded at the start, written when the game
// changed it, written again after a failure), the region setting. The descriptor under test is the real one; the images are made here.
#include <string>
#include <vector>
#include "check.h"
#include "fakes.h"
#include "boot.h"
#include "engines.h"
#include "cores.h"
#include "snes_regs.h"
#include "snes_rom.h"
#include "ops_glue.h"

struct SnesRig {
    FakeIo io;
    FakeFs *fs;
    CoreCtx c;
    Settings *st;
    explicit SnesRig(FakeFs *f) : fs(f)
    {
        memset(&c, 0, sizeof(c));
        c.desc = &snes_desc;
        c.io = &io;
        c.fs = fs;
        snprintf(c.cor_path, sizeof(c.cor_path), "/sd/cores/snes.cor");
        st = new Settings(snes_desc.settings, snes_desc.nsettings, snes_desc.store, fs, "", 0);
        st->load();
        c.settings = st;
    }
};

// A LoROM image of `bytes` bytes with a header at $7FC0; chipset, size bytes and country as given; every byte of the body is a function of its address.
static std::vector<uint8_t> image(uint32_t bytes, uint8_t chipset, uint8_t romsz, uint8_t ramsz, uint8_t country, const char *title = "TEST CART", uint32_t place = SNES_HDR_LO, uint8_t mode = 0x20)
{
    std::vector<uint8_t> v(bytes);
    for (uint32_t i = 0; i < bytes; i++) v[i] = (uint8_t)(i * 7 + (i >> 8) * 13 + 1);
    uint8_t *h = &v[place];
    memset(h, ' ', 21);
    memcpy(h, title, strlen(title));
    h[0x15] = mode; h[0x16] = chipset; h[0x17] = romsz; h[0x18] = ramsz; h[0x19] = country;
    uint16_t sum = 0x4321, comp = (uint16_t)~sum;
    h[0x1C] = (uint8_t)comp; h[0x1D] = (uint8_t)(comp >> 8); h[0x1E] = (uint8_t)sum; h[0x1F] = (uint8_t)(sum >> 8);
    h[0x3C] = 0x00; h[0x3D] = 0x80;
    return v;
}

static int run(SnesRig &r, const char *path, char *msg = 0, int msglen = 0)
{
    OpResult res;
    OpIn in = in_str("path", path);
    int rc = ops_call(&r.c, "snes.run", &in, 1, &res);
    if (msg) snprintf(msg, msglen, "%s", res.msg);
    return rc;
}
// byte `off` of a file, -1 when the file is not there or too short (a crash would not be a red check)
static int fbyte(FakeFs &fs, const char *path, size_t off)
{
    auto it = fs.files.find(lower(path));
    return it == fs.files.end() || off >= it->second.size() ? -1 : it->second[off];
}
static uint32_t rd24(SnesRig &r, uint32_t reg) { return r.io.regs[reg] | r.io.regs[reg + 1] << 8 | r.io.regs[reg + 2] << 16; }
static int count_writes(SnesRig &r, uint32_t reg, uint8_t v) { int n = 0; for (auto &w : r.io.log) n += (w.reg == reg && w.v == v); return n; }

int main()
{
    // --- the boot ROM is embedded (T142 Q3): no ROM file is asked for, and the start of the machine writes the 64 bytes into the S-SMP's port
    {
        FakeFs fs;
        SnesRig r(&fs);
        CHECK_EQ(snes_desc.nroms, 0);
        static const char *hex = "cdefbde800c61dd0fc8faaf48fbbf578ccf4d0fb2f19ebf4d0fc7ef4d00be4f5cbf4d700fcd0f3ab0110ef7ef410ebbaf6da00baf4c4f4dd5dd0db1f0000c0ff";
        char msg[160] = "";
        CHECK_EQ(snes_desc.init(&r.c, msg, sizeof(msg)), 0);
        for (int i = 0; i < 64; i++) {
            unsigned v = 0;
            sscanf(hex + 2 * i, "%2x", &v);
            CHECK_EQ(r.io.regs[SNES_IPL + i], (uint8_t)v);
        }
    }
    // --- a LoROM cartridge of 1 MiB with 8 KiB of battery RAM, NTSC: the order of the writes and what lands where
    {
        FakeFs fs;
        auto img = image(0x100000, 0x02, 0x0A, 0x03, 0x01);
        fs.put("/sd/games/zelda.sfc", img);
        SnesRig r(&fs);
        memset(&r.io.ddr[SNES_W_WRAM], 0xA5, SNES_WRAM_BYTES);                // what the last game left in the work RAM
        char msg[200] = "";
        CHECK_EQ(run(r, "/sd/games/zelda.sfc", msg, sizeof(msg)), OPE_OK);
        CHECK_EQ(r.io.log.front().reg, SNES_CTRL);
        CHECK_EQ(r.io.log.front().v, SNES_CTRL_HOLD);
        CHECK(r.c.program_loaded);
        CHECK_EQ(r.io.regs[SNES_ROM_TYPE], 0x00);
        CHECK_EQ(rd24(r, SNES_ROM_MASK), 0xFFFFFu);
        CHECK_EQ(rd24(r, SNES_RAM_MASK), 0x1FFFu);
        CHECK_EQ(r.io.regs[SNES_SYSTEM] & SNES_SYS_PAL, 0);
        CHECK_EQ(r.io.regs[SNES_CART_BASE + 3], 0x01);
        CHECK(!memcmp(&r.io.ddr[SNES_W_ROM], img.data(), img.size()));
        for (uint32_t i = 0; i < SNES_WRAM_BYTES; i += 977) CHECK_EQ(r.io.ddr[SNES_W_WRAM + i], 0);
        for (uint32_t i = 0; i < 0x2000; i += 61) CHECK_EQ(r.io.ddr[SNES_W_BSRAM + i], 0xFF);
        // the machine is released last (CTRL = 0 after every register of the cartridge)
        size_t last_cfg = 0, release = 0;
        for (size_t i = 0; i < r.io.log.size(); i++) {
            if (r.io.log[i].reg == SNES_ROM_MASK) last_cfg = i;
            if (r.io.log[i].reg == SNES_CTRL && r.io.log[i].v == 0) release = i;
        }
        CHECK(release > last_cfg);
        CHECK(!r.io.in_reset);
    }
    // --- the battery RAM: the game writes it, the service saves it after 5 s, the next run of the image loads it
    {
        FakeFs fs;
        auto img = image(0x100000, 0x02, 0x0A, 0x03, 0x01);
        fs.put("/sd/games/zelda.sfc", img);
        SnesRig r(&fs);
        CHECK_EQ(run(r, "/sd/games/zelda.sfc"), OPE_OK);
        for (int i = 0; i < 100; i++) r.io.ddr[SNES_W_BSRAM + 0x100 + i] = (uint8_t)(i + 1);        // the game's save, as the core's write-through leaves it
        OpResult res;
        r.io.delay_ms(1000);
        CHECK_EQ(ops_call(&r.c, "snes.service", 0, 0, &res), OPE_OK);
        CHECK(!fs.has("/sd/cores/snes/saves/zelda.srm"));                                              // not yet 5 s
        r.io.delay_ms(5000);
        CHECK_EQ(ops_call(&r.c, "snes.service", 0, 0, &res), OPE_OK);
        CHECK(fs.has("/sd/cores/snes/saves/zelda.srm"));
        CHECK_EQ(fs.has("/sd/cores/snes/saves/zelda.srm") ? fs.files[lower("/sd/cores/snes/saves/zelda.srm")].size() : 0, 0x2000u);
        CHECK_EQ(fbyte(fs, "/sd/cores/snes/saves/zelda.srm", 0x100), 1);
        CHECK_EQ(fbyte(fs, "/sd/cores/snes/saves/zelda.srm", 0x100 + 99), 100);
        CHECK_EQ(fbyte(fs, "/sd/cores/snes/saves/zelda.srm", 0), 0xFF);
        // unchanged: not written again
        int w = fs.writes + (int)fs.save_sizes.size();
        r.io.delay_ms(6000);
        ops_call(&r.c, "snes.service", 0, 0, &res);
        CHECK_EQ(fs.writes + (int)fs.save_sizes.size(), w);
        // a changed byte is written
        r.io.ddr[SNES_W_BSRAM + 0x100] = 0x77;
        r.io.delay_ms(6000);
        ops_call(&r.c, "snes.service", 0, 0, &res);
        CHECK_EQ(fbyte(fs, "/sd/cores/snes/saves/zelda.srm", 0x100), 0x77);
        // another start of the application: the save is loaded into the window
        SnesRig r2(&fs);
        CHECK_EQ(run(r2, "/sd/games/zelda.sfc"), OPE_OK);
        CHECK_EQ(r2.io.ddr[SNES_W_BSRAM + 0x100], 0x77);
        CHECK_EQ(r2.io.ddr[SNES_W_BSRAM + 0x101], 2);
        CHECK_EQ(r2.io.ddr[SNES_W_BSRAM], 0xFF);
        // a different cartridge: the first one's save is written before the machine starts over, to its own file, and the second gets its own
        r2.io.ddr[SNES_W_BSRAM + 0x200] = 0x33;
        fs.put("/sd/games/other.sfc", image(0x80000, 0x02, 0x09, 0x03, 0x01));
        CHECK_EQ(run(r2, "/sd/games/other.sfc"), OPE_OK);
        CHECK_EQ(fbyte(fs, "/sd/cores/snes/saves/zelda.srm", 0x200), 0x33);
        CHECK(!fs.has("/sd/cores/snes/saves/other.srm"));
        CHECK_EQ(r2.io.ddr[SNES_W_BSRAM + 0x100], 0xFF);
    }
    // --- a game's write just before a reset is saved by the reset (the init hook flushes)
    {
        FakeFs fs;
        fs.put("/sd/games/a.sfc", image(0x80000, 0x02, 0x09, 0x02, 0x01));
        SnesRig r(&fs);
        CHECK_EQ(run(r, "/sd/games/a.sfc"), OPE_OK);
        r.io.ddr[SNES_W_BSRAM + 5] = 0x42;
        char msg[100];
        snes_desc.init(&r.c, msg, sizeof(msg));
        CHECK(fs.has("/sd/cores/snes/saves/a.srm"));
        CHECK_EQ(fbyte(fs, "/sd/cores/snes/saves/a.srm", 5), 0x42);
    }
    // --- a medium that refuses the save: the notice says so, the next service tries again and succeeds when the medium is writable
    {
        FakeFs fs;
        fs.put("/sd/games/a.sfc", image(0x80000, 0x02, 0x09, 0x02, 0x01));
        SnesRig r(&fs);
        CHECK_EQ(run(r, "/sd/games/a.sfc"), OPE_OK);
        r.io.ddr[SNES_W_BSRAM + 5] = 0x42;
        fs.read_only = true;
        OpResult res;
        r.io.delay_ms(6000);
        ops_call(&r.c, "snes.service", 0, 0, &res);
        CHECK(r.c.notice_pending && strstr(r.c.notice, "a.srm") != 0);
        CHECK(!fs.has("/sd/cores/snes/saves/a.srm"));
        r.c.notice_pending = false;
        r.io.delay_ms(6000);
        ops_call(&r.c, "snes.service", 0, 0, &res);                   // tried again, told once
        CHECK(!r.c.notice_pending);
        fs.read_only = false;
        r.io.delay_ms(6000);
        ops_call(&r.c, "snes.service", 0, 0, &res);
        CHECK(fs.has("/sd/cores/snes/saves/a.srm"));
    }
    // --- T180 D4 / D1 (S7): a save the medium refuses keeps the game loaded and stops every exit (reset, reload, restart, another game); the same exits go through once
    // the medium is back, with the save in its file
    {
        FakeFs fs;
        fs.put("/sd/games/a.sfc", image(0x80000, 0x02, 0x09, 0x02, 0x01));
        fs.put("/sd/games/b.sfc", image(0x80000, 0x02, 0x09, 0x02, 0x01));
        SnesRig r(&fs);
        CHECK_EQ(run(r, "/sd/games/a.sfc"), OPE_OK);
        r.io.ddr[SNES_W_BSRAM + 5] = 0x42;
        char msg[300];
        CHECK(snes_desc.media_pending(&r.c));
        fs.read_only = true;
        CHECK(media_writeback(&r.c, WB_WHY_RESET, msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "a.srm") != 0 && strstr(msg, "stays loaded") != 0);
        CHECK(snes_desc.media_pending(&r.c));
        size_t ev = r.io.events.size();
        CHECK(boot_cold_reset(&r.c, msg, sizeof(msg)) != 0);
        CHECK(boot_reload(&r.c, msg, sizeof(msg)) != 0);
        CHECK(boot_restart(&r.c, msg, sizeof(msg)) != 0);
        CHECK_EQ(r.io.events.size(), ev);                                                    // the machine was never held
        CHECK(run(r, "/sd/games/b.sfc", msg, sizeof(msg)) != OPE_OK);                         // D1: another game does not replace this one
        CHECK(strstr(msg, "save") != 0);
        CHECK(r.c.program_loaded);
        CHECK_EQ(r.io.ddr[SNES_W_BSRAM + 5], 0x42);                                          // the battery RAM is still the game's
        CHECK_EQ(r.io.events.size(), ev);
        fs.read_only = false;
        CHECK_EQ(media_writeback(&r.c, WB_WHY_RESET, msg, sizeof(msg)), 0);
        CHECK_EQ(fbyte(fs, "/sd/cores/snes/saves/a.srm", 5), 0x42);
        CHECK(!snes_desc.media_pending(&r.c));
        CHECK_EQ(run(r, "/sd/games/b.sfc"), OPE_OK);
    }
    // --- a cartridge without RAM: nothing is saved, nothing is read
    {
        FakeFs fs;
        fs.put("/sd/games/b.sfc", image(0x80000, 0x00, 0x09, 0x00, 0x01));
        SnesRig r(&fs);
        CHECK_EQ(run(r, "/sd/games/b.sfc"), OPE_OK);
        CHECK_EQ(rd24(r, SNES_RAM_MASK), 0u);
        OpResult res;
        r.io.delay_ms(6000);
        ops_call(&r.c, "snes.service", 0, 0, &res);
        CHECK(!fs.has("/sd/cores/snes/saves/b.srm"));
        CHECK_EQ(fs.writes, 0);
    }
    // --- S12: the cartridges with a coprocessor are refused with the chip's name, and the machine is not touched
    {
        FakeFs fs;
        fs.put("/sd/games/sa1.sfc", image(0x100000, 0x35, 0x0A, 0x03, 0x01));
        fs.put("/sd/games/gsu.sfc", image(0x100000, 0x15, 0x0A, 0x03, 0x01));
        fs.put("/sd/games/junk.sfc", std::vector<uint8_t>(0x100000, 0x00));
        SnesRig r(&fs);
        char msg[300];
        CHECK(run(r, "/sd/games/sa1.sfc", msg, sizeof(msg)) != OPE_OK);
        CHECK(strstr(msg, "SA-1") != 0);
        CHECK(run(r, "/sd/games/gsu.sfc", msg, sizeof(msg)) != OPE_OK);
        CHECK(strstr(msg, "Super FX") != 0);
        CHECK(run(r, "/sd/games/junk.sfc", msg, sizeof(msg)) != OPE_OK);
        CHECK(strstr(msg, "header") != 0);
        CHECK(run(r, "/sd/games/missing.sfc", msg, sizeof(msg)) != OPE_OK);
        CHECK(r.io.log.empty());
        CHECK_EQ(r.io.reset_asserts, 0);
        CHECK(!r.c.program_loaded);
    }
    // --- T218 S1-S4: a DSP-n cartridge. No firmware: refused, naming the file(s) and the folder, machine untouched. A separate file in the core's roms folder: the 8192 bytes go to
    // the chip's port in order and the mapper byte selects the chip. Appended to the image: used first, even when a file exists, and the image's size is the ROM's.
    {
        std::vector<uint8_t> fwa(SNES_DSP_FW_BYTES), fwb(SNES_DSP_FW_BYTES);
        for (uint32_t i = 0; i < SNES_DSP_FW_BYTES; i++) { fwa[i] = (uint8_t)(i * 7 + 1); fwb[i] = (uint8_t)(i * 13 + 5); }
        auto fw_stream = [](SnesRig &r) { std::vector<uint8_t> o; for (auto &w : r.io.log) if (w.reg == SNES_DSPFW) o.push_back(w.v); return o; };
        {
            FakeFs fs;
            fs.put("/sd/games/mk.sfc", image(0x80000, 0x03, 0x09, 0x00, 0x01, "SUPER MARIO KART"));
            SnesRig r(&fs);
            char msg[300];
            CHECK(run(r, "/sd/games/mk.sfc", msg, sizeof(msg)) != OPE_OK);                // S3
            CHECK(strstr(msg, "dsp1b.bin") != 0 && strstr(msg, "dsp1.bin") != 0 && strstr(msg, "DSP-1") != 0 && strstr(msg, "append") != 0);
            CHECK(r.io.log.empty());
            CHECK(!r.c.program_loaded);
            fs.put("/sd/cores/snes/roms/dsp1b.bin", fwa);                                 // S1
            CHECK_EQ(run(r, "/sd/games/mk.sfc"), OPE_OK);
            CHECK(fw_stream(r) == fwa);
            CHECK_EQ(count_writes(r, SNES_DSPFW_REWIND, 0), 1);
            CHECK_EQ(count_writes(r, SNES_ROM_TYPE, 0x80), 1);
        }
        {
            FakeFs fs;                                                                    // S2: appended wins over the file
            auto img = image(0x80000, 0x03, 0x09, 0x00, 0x01, "SUPER MARIO KART");
            std::vector<uint8_t> both = img;
            both.insert(both.end(), fwb.begin(), fwb.end());
            fs.put("/sd/games/mk2.sfc", both);
            fs.put("/sd/cores/snes/roms/dsp1b.bin", fwa);
            SnesRig r(&fs);
            CHECK_EQ(run(r, "/sd/games/mk2.sfc"), OPE_OK);
            CHECK(fw_stream(r) == fwb);
            CHECK_EQ(rd24(r, SNES_ROM_MASK), 0x7FFFFu);
            CHECK(!memcmp(&r.io.ddr[SNES_W_ROM], img.data(), img.size()));
        }
        {   // S8 (D6): a firmware of our own in valid uPD7725 words (jump / load / operation, no real firmware), once low byte first, once with every word reversed
            std::vector<uint8_t> lo(SNES_DSP_FW_BYTES), hi(SNES_DSP_FW_BYTES);
            for (uint32_t i = 0; i < 2048; i++) {
                uint32_t w = i % 3 == 0 ? (2u << 22 | 0x100u << 13 | (i & 0x7FF) << 2)                // JP with the jump-always branch field 100000000
                           : i % 3 == 1 ? (3u << 22 | (i * 37 & 0xFFFF) << 6 | 3)                      // LD
                                        : (i * 0x10101u & 0x3FFFFF);                                   // OP
                for (int k = 0; k < 3; k++) { lo[3 * i + k] = (uint8_t)(w >> (8 * k)); hi[3 * i + 2 - k] = (uint8_t)(w >> (8 * k)); }
            }
            for (uint32_t i = 0; i < 1024; i++) {
                uint16_t d = (uint16_t)(i * 77 + 0x1234);
                lo[6144 + 2 * i] = (uint8_t)d; lo[6145 + 2 * i] = (uint8_t)(d >> 8);
                hi[6144 + 2 * i] = (uint8_t)(d >> 8); hi[6145 + 2 * i] = (uint8_t)d;
            }
            for (int variant = 0; variant < 3; variant++) {                                        // 0 low first, 1 reversed as a file, 2 reversed and appended
                FakeFs fs;
                auto img = image(0x80000, 0x03, 0x09, 0x00, 0x01, "SUPER MARIO KART");
                if (variant == 2) {
                    img.insert(img.end(), hi.begin(), hi.end());
                } else {
                    fs.put("/sd/cores/snes/roms/dsp1b.bin", variant ? hi : lo);
                }
                fs.put("/sd/games/mk8.sfc", img);
                SnesRig r(&fs);
                CHECK_EQ(run(r, "/sd/games/mk8.sfc"), OPE_OK);
                CHECK(fw_stream(r) == lo);
            }
            std::vector<uint8_t> rnd(SNES_DSP_FW_BYTES);                                           // neither: random bytes are written as they are
            uint32_t x = 12345;
            for (auto &b : rnd) { x = x * 1664525u + 1013904223u; b = (uint8_t)(x >> 24); }
            FakeFs fs;
            fs.put("/sd/cores/snes/roms/dsp1b.bin", rnd);
            fs.put("/sd/games/mk9.sfc", image(0x80000, 0x03, 0x09, 0x00, 0x01, "SUPER MARIO KART"));
            SnesRig r(&fs);
            CHECK_EQ(run(r, "/sd/games/mk9.sfc"), OPE_OK);
            CHECK(fw_stream(r) == rnd);
        }
        {
            FakeFs fs;                                                                    // S4: the title picks the chip and its file; a wrong size file is not taken
            fs.put("/sd/games/dm.sfc", image(0x100000, 0x03, 0x0A, 0x00, 0x01, "DUNGEON MASTER"));
            fs.put("/sd/roms/snes/dsp2.bin", fwb);
            fs.put("/sd/roms/snes/dsp1b.bin", fwa);
            fs.put("/sd/games/g.sfc", image(0x100000, 0x03, 0x0A, 0x00, 0x01, "SD GUNDAM GX"));
            fs.put("/sd/roms/snes/dsp3.bin", std::vector<uint8_t>(100, 1));
            SnesRig r(&fs);
            CHECK_EQ(run(r, "/sd/games/dm.sfc"), OPE_OK);
            CHECK(fw_stream(r) == fwb);
            CHECK_EQ(count_writes(r, SNES_ROM_TYPE, 0x90), 1);
            char msg[300];
            r.io.log.clear();
            CHECK(run(r, "/sd/games/g.sfc", msg, sizeof(msg)) != OPE_OK);
            CHECK(strstr(msg, "dsp3.bin") != 0);
            CHECK(r.io.log.empty());
        }
        {
            FakeFs fs;                                                                    // S7: the setting "DSP firmware" (index 3) overrides the title for every DSP cartridge, not for others
            fs.put("/sd/games/mk.sfc", image(0x80000, 0x03, 0x09, 0x00, 0x01, "SUPER MARIO KART"));
            fs.put("/sd/games/plain.sfc", image(0x80000, 0x00, 0x09, 0x00, 0x01));
            SnesRig r(&fs);
            r.st->set(3, 3);                                                              // DSP-3
            char msg[300];
            CHECK(run(r, "/sd/games/mk.sfc", msg, sizeof(msg)) != OPE_OK);
            CHECK(strstr(msg, "dsp3.bin") != 0 && strstr(msg, "DSP-3") != 0 && strstr(msg, "dsp1b.bin") == 0);
            CHECK(r.io.log.empty());
            fs.put("/sd/cores/snes/roms/dsp3.bin", fwb);
            CHECK_EQ(run(r, "/sd/games/mk.sfc"), OPE_OK);
            CHECK(fw_stream(r) == fwb);
            CHECK_EQ(count_writes(r, SNES_ROM_TYPE, 0xA0), 1);
            r.io.log.clear();
            CHECK_EQ(run(r, "/sd/games/plain.sfc"), OPE_OK);
            CHECK(fw_stream(r).empty());
            CHECK_EQ(count_writes(r, SNES_ROM_TYPE, 0x00), 1);
        }
    }
    // --- an image whose size is not a power of two: the part of the window the mask reaches is the cartridge's mirror (1.5 MiB: 1.5-2 MiB repeat 1-1.5 MiB)
    {
        FakeFs fs;
        auto img = image(0x180000, 0x00, 0x0B, 0x00, 0x01);
        fs.put("/sd/games/odd.sfc", img);
        SnesRig r(&fs);
        CHECK_EQ(run(r, "/sd/games/odd.sfc"), OPE_OK);
        CHECK_EQ(rd24(r, SNES_ROM_MASK), 0x1FFFFFu);
        CHECK(!memcmp(&r.io.ddr[SNES_W_ROM], img.data(), img.size()));
        CHECK(!memcmp(&r.io.ddr[SNES_W_ROM + 0x180000], &img[0x100000], 0x80000));
    }
    // --- the region: Auto follows the header, NTSC and PAL override it
    {
        FakeFs fs;
        fs.put("/sd/games/eu.sfc", image(0x80000, 0x00, 0x09, 0x00, 0x02));
        SnesRig r(&fs);
        CHECK_EQ(run(r, "/sd/games/eu.sfc"), OPE_OK);
        CHECK(r.io.regs[SNES_SYSTEM] & SNES_SYS_PAL);
        r.st->set(0, 1);                                                    // "Video standard" = NTSC
        CHECK_EQ(run(r, "/sd/games/eu.sfc"), OPE_OK);
        CHECK_EQ(r.io.regs[SNES_SYSTEM] & SNES_SYS_PAL, 0);
        fs.put("/sd/games/us.sfc", image(0x80000, 0x00, 0x09, 0x00, 0x01));
        r.st->set(0, 2);                                                    // PAL
        CHECK_EQ(run(r, "/sd/games/us.sfc"), OPE_OK);
        CHECK(r.io.regs[SNES_SYSTEM] & SNES_SYS_PAL);
        r.st->set(0, 0);
        CHECK_EQ(run(r, "/sd/games/us.sfc"), OPE_OK);
        CHECK_EQ(r.io.regs[SNES_SYSTEM] & SNES_SYS_PAL, 0);
        r.st->set(1, 1);                                                    // "512 wide"
        CHECK_EQ(run(r, "/sd/games/us.sfc"), OPE_OK);
        CHECK(r.io.regs[SNES_SYSTEM] & SNES_SYS_NOBLEND);
    }
    // --- the S-RTC cartridge (ExHiROM 5 MiB) and the OBC-1 (LoROM): the mapper byte the chip decoding needs, and the clock in the registers
    {
        FakeFs fs;
        fs.put("/sd/games/rtc.sfc", image(0x500000, 0x55, 0x0C, 0x03, 0x00, "RTC CART", SNES_HDR_EX, 0x25));
        fs.put("/sd/games/obc.sfc", image(0x100000, 0x25, 0x0A, 0x03, 0x01));
        SnesRig r(&fs);
        r.io.regs[SNES_RTC_TGL] = 0;
        CHECK_EQ(run(r, "/sd/games/rtc.sfc"), OPE_OK);
        CHECK_EQ(r.io.regs[SNES_ROM_TYPE], 0x0A);
        CHECK_EQ(rd24(r, SNES_ROM_MASK), 0x7FFFFFu);
        CHECK_EQ(r.io.regs[SNES_RTC_TGL], 1);                                   // the toggle: the chip takes the time
        CHECK_EQ(r.io.regs[SNES_RTC + 4], 0x01);                                // month 1 (no clock on the board: 2026-01-01)
        CHECK_EQ(r.io.regs[SNES_RTC + 5], 0x26);
        CHECK_EQ(run(r, "/sd/games/obc.sfc"), OPE_OK);
        CHECK_EQ(r.io.regs[SNES_ROM_TYPE], 0xC0);
        CHECK_EQ(r.io.regs[SNES_RTC_TGL], 0);                                   // toggled again
    }
    // --- the picture window and the freeze register are the core's
    {
        CHECK_EQ(snes_desc.video.w, 512u);
        CHECK_EQ(snes_desc.video.h, 448u);
        CHECK_EQ(snes_desc.video.fb_base, 0x01000000u + SNES_W_FB);
        CHECK_EQ(snes_desc.video.fb_stride, (uint32_t)SNES_FB_STRIDE_V);
        CHECK_EQ(snes_desc.freeze.reg_status, SNES_STATUS);
        CHECK(!memcmp(snes_desc.id, "SNES", 4));
    }
    // --- the sound check (T142 5): the peak registers say whether the core makes sound and whether the host takes it; the check starts the next measurement
    {
        FakeFs fs;
        SnesRig r(&fs);
        auto put16 = [&](uint32_t reg, unsigned v) { r.io.regs[reg] = (uint8_t)v; r.io.regs[reg + 1] = (uint8_t)(v >> 8); };
        auto check = [&](unsigned dsp, unsigned out, unsigned hands, char *msg) {
            put16(SNES_PEAK_DSP, dsp); put16(SNES_PEAK_OUT, out); put16(SNES_HANDS, hands);
            r.io.log.clear();
            OpResult res;
            int rc = ops_call(&r.c, "snes.sound", 0, 0, &res);
            snprintf(msg, 240, "%s", res.msg);
            return rc;
        };
        char m[240];
        CHECK_EQ(check(12000, 11900, 1000, m), OPE_OK);                          // the first check: 1000 frames since the start
        CHECK(strstr(m, "core 12000, to host 11900 of 32768, 1000 frames") != 0 && strstr(m, "reaches the host") != 0);
        CHECK_EQ(count_writes(r, SNES_PEAK_DSP, 0), 1);                          // the measurement starts again
        CHECK_EQ(check(0, 0, 1500, m), OPE_OK);                                   // 500 frames later, nothing played
        CHECK(strstr(m, "500 frames") != 0 && strstr(m, "core made no sound") != 0);
        CHECK_EQ(check(9000, 0, 2000, m), OPE_OK);
        CHECK(strstr(m, "lost before the host") != 0);
        CHECK_EQ(check(9000, 8800, 2000, m), OPE_OK);                             // the host asked for nothing since the last check
        CHECK(strstr(m, "0 frames") != 0 && strstr(m, "takes no audio") != 0);
        CHECK_EQ(check(9000, 8800, 100, m), OPE_OK);                              // the counter wraps (16 bit): 100 - 2000 = 63636 frames, not a hang
        CHECK(strstr(m, "63636 frames") != 0 && strstr(m, "reaches the host") != 0);
        CHECK(snes_desc.ntasks == 5 && !strcmp(snes_desc.tasks[2].op, "snes.sound") && !strcmp(snes_desc.tasks[3].op, "menu.pad_map_1") && !strcmp(snes_desc.tasks[4].op, "menu.pad_map_2"));
    }
    DONE("snes");
}
