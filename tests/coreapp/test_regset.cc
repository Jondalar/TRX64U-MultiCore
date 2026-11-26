// T104 5.2 `setting, extended` and `reset_write`: several settings in one register byte, a value-to-byte table, fixed bytes with every commit.
// The expected bytes are the reference's own rule (amiga_regs() of software/io/amiga/amiga_cfg.h in the t85-amigaapp fork, ported below),
// checked over every combination of the Amiga's and the A1200's settings, not over a few examples.
#include "check.h"
#include "amiga_model.h"
#include "boot.h"
#include "regset.h"

struct Ref { uint8_t mem, fast, cpu, chipset, floppy, ide; };

// amiga_regs(chip, slow, fast, chipset, nfl, fwp) of the reference
static Ref ref_regs(int chip, int slow, int fast, int chipset, int nfl, int fwp)
{
    Ref r;
    r.mem = (uint8_t)((slow << 2) | chip);
    r.fast = (uint8_t)fast;
    r.cpu = 0;
    r.chipset = (uint8_t)(chipset == 2 ? 0x10 : chipset == 1 ? 0x08 : 0);
    r.floppy = (uint8_t)((nfl << 2) | (fwp ? 2 : 0));
    r.ide = 0x21;
    return r;
}

static void set_all(AmigaRig &r, int chip, int slow, int fast, int chipset, int nfl, int fwp)
{
    const Settings &s = *r.st;
    r.st->set(s.find("Chip RAM (at next reset)"), chip);
    r.st->set(s.find("Slow RAM (at next reset)"), slow);
    r.st->set(s.find("Fast RAM (at next reset)"), fast);
    r.st->set(s.find("Chipset (at next reset)"), chipset);
    r.st->set(s.find("Floppy drives (at next reset)"), nfl);
    r.st->set(s.find("Floppy write protect (next reset)"), fwp);
}

static int writes_to(const AmigaIo &io, uint32_t reg)
{
    int n = 0;
    for (auto &w : io.log) n += w.reg == reg;
    return n;
}

static int sweep(const CoreDesc *d, int nfast, int nchipset)
{
    FakeFs fs;
    AmigaRig r(&fs, d);
    int bad = 0;
    for (int chip = 0; chip < 4; chip++)
    for (int slow = 0; slow < 4; slow++)
    for (int fast = 0; fast < nfast; fast++)
    for (int chs = 0; chs < nchipset; chs++)
    for (int nfl = 0; nfl < 4; nfl++)
    for (int fwp = 0; fwp < 2; fwp++) {
        set_all(r, chip, slow, fast, chs, nfl, fwp);
        r.io.log.clear();
        boot_apply_settings(&r.c, false);
        Ref e = ref_regs(chip, slow, fast, chs, nfl, fwp);
        bool ok = r.io.regs[AM_MEM_CFG] == e.mem && r.io.regs[AM_FAST_CFG] == e.fast && r.io.regs[AM_CPU_CFG] == e.cpu &&
                  r.io.regs[AM_CHIPSET_CFG] == e.chipset && r.io.regs[AM_FLOPPY_CFG] == e.floppy && r.io.regs[AM_IDE_CFG] == e.ide;
        ok = ok && writes_to(r.io, AM_MEM_CFG) == 1 && writes_to(r.io, AM_FLOPPY_CFG) == 1;      // a shared byte is written once per commit
        bad += !ok;
    }
    return bad;
}

int main()
{
    // the A500's record: 4 * 4 * 3 * 2 * 4 * 2 combinations (fast: none 2M 4M; chipset: OCS ECS)
#ifndef TEST_APP_A1200
    CHECK_EQ(sweep(&amiga_desc, 3, 2), 0);
#else
    // the A1200's (its own app, a12app): fast has 5.5M and the Zorro III board, chipset has AGA
    CHECK_EQ(sweep(&amiga1200_desc, 5, 3), 0);
#endif

    // T138 S3: "Fast RAM" of the A1200 lists the Zorro III board of 32 MB as its fifth value and writes FAST_CFG 4 for it (the A500 has no such value)
    {
#ifdef TEST_APP_A1200
        const Setting &f12 = amiga1200_desc.settings[3];
        CHECK_STR(f12.key, "Fast RAM (at next reset)");
        CHECK_EQ(f12.nvalues, 5);
        CHECK_STR(f12.values[4], "32M (Zorro III)");
        CHECK_STR(f12.values[3], "5.5M");
        FakeFs fs;
        AmigaRig r(&fs, &amiga1200_desc);
        set_all(r, 3, 0, 4, 2, 1, 0);
        r.io.log.clear();
        boot_apply_settings(&r.c, false);
        CHECK_EQ(r.io.regs[AM_FAST_CFG], 0x04);
        CHECK_EQ(r.io.regs[AM_MEM_CFG], 0x03);                       // chip 2 MB, no slow RAM: the board takes nothing from the slow RAM
#else
        const Setting &f5 = amiga_desc.settings[3];
        CHECK_EQ(f5.nvalues, 3);
        for (int i = 0; i < f5.nvalues; i++) CHECK(strstr(f5.values[i], "Zorro") == 0);
#endif
    }

    // the defaults of the two records are the two builds' defaults (amiga_cfg.h): 512K chip + 512K slow, OCS / 2M chip, AGA
    {
        FakeFs fs;
#ifdef TEST_APP_A1200
        AmigaRig b(&fs, &amiga1200_desc);
        boot_apply_settings(&b.c, false);
        CHECK_EQ(b.io.regs[AM_MEM_CFG], 0x03);
        CHECK_EQ(b.io.regs[AM_FAST_CFG], 0x00);
        CHECK_EQ(b.io.regs[AM_CHIPSET_CFG], 0x10);
        CHECK_EQ(b.io.regs[AM_FLOPPY_CFG], 0x04);
        CHECK_EQ(amiga1200_desc.settings[4].nvalues, 3);              // OCS ECS AGA
        CHECK_STR(amiga1200_desc.store, "Amiga 1200 Settings");       // its own .cfg section: the A500's saved OCS never becomes its value
#else
        AmigaRig a(&fs, &amiga_desc);
        boot_apply_settings(&a.c, false);
        CHECK_EQ(a.io.regs[AM_MEM_CFG], 0x04);
        CHECK_EQ(a.io.regs[AM_CHIPSET_CFG], 0x00);
        CHECK_EQ(a.io.regs[AM_FLOPPY_CFG], 0x04);                    // two drives, not write protected
        // the A500 cannot be told to be AGA: the value is not in its list
        CHECK_EQ(amiga_desc.settings[4].nvalues, 2);
#endif
    }

    // the Kickstart choice is no register: it writes nothing; the registers it does write are the seven
    {
        FakeFs fs;
        AmigaRig r(&fs);
        r.io.log.clear();
        boot_apply_settings(&r.c, false);
        for (auto &w : r.io.log) {
            CHECK((w.reg >= AM_MEM_CFG && w.reg <= AM_IDE_CFG) || w.reg == AM_RTC_EN);
        }
        CHECK_EQ(r.io.log.size(), 7);                                // MEM FAST CHIPSET FLOPPY RTC (T191), then the two fixed bytes
    }

    // the live path: one setting changed, its register is rewritten with the other setting's bits kept
    {
        FakeFs fs;
        AmigaRig r(&fs);
        set_all(r, 3, 2, 0, 0, 1, 0);
        r.io.log.clear();
        regset_apply_setting(&r.c, r.st->find("Chip RAM (at next reset)"));
        CHECK_EQ(r.io.regs[AM_MEM_CFG], (2 << 2) | 3);
        CHECK_EQ(r.io.log.size(), 1);
        CHECK_EQ(regset_value(&r.c, AM_MEM_CFG), 0x0B);
        CHECK_EQ(regset_part(&r.c, r.st->find("Slow RAM (at next reset)")), 0x08);
    }
    DONE("test_regset");
}
