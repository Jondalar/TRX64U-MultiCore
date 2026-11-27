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
#ifdef TEST_APP_APPLE2                   // the Apple IIe's app (run.sh apple2app) has its own core only; its swap is b6 of the register that holds the monitor items (its own test), so it has no row below, only its key
extern const CoreDesc apple2_desc;       // cores.h lists the shared app's cores; the Apple IIe is its own app (cores/apple2/app/lib)
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
#if !defined(TEST_APP_APPLE2)   // the Atari's, the 264's and the Apple IIe's swap have another form (their own key is checked below): no row
    const Row rows[] = {
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
#ifdef TEST_APP_APPLE2
    CHECK_EQ(count_key(apple2_desc, "Joystick ports"), 1);
#endif
    DONE("test_joyswap");
}
