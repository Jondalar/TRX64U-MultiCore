// T157: the "Joystick ports" setting (Normal / Swapped) of every generic-app core that has one. For each core: the setting is there exactly once, a change
// is applied (the core's own register gets the swap bit, at once, through the setting's operation or its register bits), the .cfg keeps it
// ("Joystick ports=Swapped") and a new session reads it back. The cores that had the swap before T157 (apple2, atari800; their registers are checked in their own tests; the CPC's and the Next's are the units
// cpc_joyswap and zxnext_joyswap) are units of their apps as well (S4). zx (a choice per port) and c264 (its register is taken in reset) are named
// differently on purpose (T157 D1) and checked for their own key.
#include "check.h"
#include "fakes.h"
#include "cores.h"
#include "ops_glue.h"
#include "regset.h"
#if defined(TEST_APP_A1200) || defined(TEST_APP_AMIGA)   // the two Amiga apps (run.sh a12app with its shim, amigaapp): no file of the Amiga in any other build (T239.5 D13: the shared unit has none either)
#include "amiga_regs.h"
#endif
#ifdef TEST_APP_AMIGA
extern const CoreDesc amiga_desc;        // the Amiga 500 is its own app (cores/amiga/app/lib)
#endif
#ifdef TEST_APP_NES                      // the NES's app (run.sh nesapp) has its own core only
#include "nes_regs.h"
extern const CoreDesc nes_desc;          // cores.h lists the shared app's cores; the NES is its own app (cores/nes/app/lib)
#endif
#ifdef TEST_APP_C264                     // the Commodore 264's app (run.sh c264app) has its own core only
extern const CoreDesc c264_desc;         // the Commodore 264 is its own app (cores/c264/app/lib): no shared header declares it
#endif
#ifdef TEST_APP_C128                     // the C128's app (run.sh c128app) has its own core only
#include "c128_regs.h"
extern const CoreDesc c128_desc;         // cores.h lists the shared app's cores; the C128 is its own app (cores/c128/app/lib)
#endif
#ifdef TEST_APP_CPC                      // the CPC's app (run.sh cpcapp) has its own core only
#include "cpc_regs.h"
extern const CoreDesc cpc_desc;          // cores.h lists the shared app's cores; the CPC is its own app (cores/cpc/app/lib)
#endif
#if !defined(TEST_APP_A1200) && !defined(TEST_APP_CPC) && !defined(TEST_APP_C128) && !defined(TEST_APP_C64SCPU) && !defined(TEST_APP_ZXNEXT) && !defined(TEST_APP_NEOGEO) && !defined(TEST_APP_AMIGA) && !defined(TEST_APP_ATARI800) && !defined(TEST_APP_NES) && !defined(TEST_APP_C264)   // the A1200's, the Atari's, CPC's, C128's, Next's, Neo Geo's and Amiga 500's app have their own core only
#endif
#ifdef TEST_APP_ZXNEXT                   // the Next's app (run.sh zxnextapp) has its own core only
#include "zxnext_regs.h"
extern const CoreDesc zxnext_desc;       // cores.h lists the shared app's cores; the ZX Spectrum Next is its own app (cores/zxnext/app/lib)
#endif
#ifdef TEST_APP_NEOGEO
#include "neogeo_regs.h"
extern const CoreDesc neogeo_desc;       // cores.h lists the shared app's cores; the Neo Geo is its own app (cores/neogeo/app/lib)
#endif
#ifdef TEST_APP_APPLE2                   // the Apple IIe's app (run.sh apple2app) has its own core only; its swap is b6 of the register that holds the monitor items (its own test), so it has no row below, only its key
extern const CoreDesc apple2_desc;       // cores.h lists the shared app's cores; the Apple IIe is its own app (cores/apple2/app/lib)
#endif
#ifdef TEST_APP_ATARI800                 // the Atari's app (run.sh a8app) has its own core only; its swap is a flag of its own (its own tests), so it has no row below, only its key
extern const CoreDesc atari800_desc;     // cores.h lists the shared app's cores; the Atari 800 XL is its own app (cores/atari800/app/lib)
#endif

struct Row { const CoreDesc *d; uint32_t reg; };

static int count_key(const CoreDesc &d, const char *key)
{
    int n = 0;
    for (int i = 0; i < d.nsettings; i++) n += strcmp(d.settings[i].key, key) == 0;
    return n;
}

// the last value written to `reg` (the full register number, as the core's header names it), -1 when none
static int last_write(FakeIo &io, uint32_t reg)
{
    int v = -1;
    for (auto &w : io.log) if (w.reg == reg) v = w.v;
    return v;
}

int main()
{
    // reg: the register that holds the swap bit (b0). apple2 (b6 of A2_CFG with the monitor items) and atari800 (flags) are checked in their own tests.
#if !defined(TEST_APP_ATARI800) && !defined(TEST_APP_C264) && !defined(TEST_APP_APPLE2)   // the Atari's, the 264's and the Apple IIe's swap have another form (their own key is checked below): no row
    const Row rows[] = {
#ifdef TEST_APP_A1200
        { &amiga1200_desc, AM_JOY_CFG },
#elif defined(TEST_APP_ZXNEXT)
        { &zxnext_desc, ZXN_JOY },
#elif defined(TEST_APP_NEOGEO)
        { &neogeo_desc, NEO_PAD },
#elif defined(TEST_APP_AMIGA)
        { &amiga_desc, AM_JOY_CFG },
#elif defined(TEST_APP_CPC)
        { &cpc_desc, CPC_JOY },
#elif defined(TEST_APP_C128)
        { &c128_desc, C128_JOY },
#elif defined(TEST_APP_NES)
        { &nes_desc, NES_JOY },
#else
#endif
    };
    for (const Row &r : rows) {
        const CoreDesc &d = *r.d;
        const char *name = d.cor_name ? d.cor_name : d.title;
        CHECK_EQ(count_key(d, "Joystick ports"), 1);                       // S3: there, and once
        FakeIo io; FakeFs fs; PxRec px;
        CoreCtx c; memset(&c, 0, sizeof(c));
        c.desc = &d; c.io = &io; c.fs = &fs;
        char cfg[80]; snprintf(cfg, sizeof(cfg), "/sd/cores/%s.cfg", d.folder ? d.folder : "x");
        Settings st(d.settings, d.nsettings, d.store, &fs, cfg, 0);
        st.load();
        c.settings = &st;
        Video vid(d.video, &px, &io);
        c.video = &vid;
        int i = st.find("Joystick ports");
        CHECK(i >= 0);
        if (i < 0) { printf("no setting in %s\n", name); continue; }
        CHECK_EQ(st.item(i).when, WHEN_LIVE);
        CHECK_EQ(st.item(i).nvalues, 2);
        CHECK_STR(st.item(i).values[0], "Normal");
        CHECK_STR(st.item(i).values[1], "Swapped");
        CHECK_EQ(st.value(i), 0);
        // S1: a change reaches the core's register at once
        st.set(i, 1);
        regset_apply_setting(&c, i);
        CHECK_EQ(last_write(io, r.reg) & 1, 1);
        st.set(i, 0);
        regset_apply_setting(&c, i);
        CHECK_EQ(last_write(io, r.reg) & 1, 0);
        // the cfg round trip: the file says Swapped, a new session reads it back and a commit writes the bit again
        st.set(i, 1);
        CHECK(fs.text(cfg).find("Joystick ports=Swapped") != std::string::npos);
        Settings st2(d.settings, d.nsettings, d.store, &fs, cfg, 0);
        st2.load();
        CHECK_EQ(st2.value(st2.find("Joystick ports")), 1);
        CoreCtx c2 = c; c2.settings = &st2;
        FakeIo io2; c2.io = &io2;
        regset_apply(&c2, true);                                           // what the start sequence does with every setting
        CHECK_EQ(last_write(io2, r.reg) & 1, 1);
    }
#endif
    // the cores that keep another form: their own key is there once (T157 D1)
#ifdef TEST_APP_ATARI800
    CHECK_EQ(count_key(atari800_desc, "Joystick ports"), 1);
#endif
#ifdef TEST_APP_C264
    CHECK_EQ(count_key(c264_desc, "Joystick ports (at next reset)"), 1);
#endif
#ifdef TEST_APP_APPLE2
    CHECK_EQ(count_key(apple2_desc, "Joystick ports"), 1);
#endif
    DONE("test_joyswap");
}
