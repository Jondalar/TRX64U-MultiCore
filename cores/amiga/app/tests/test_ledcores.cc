// T159 D-all (Alex, 2026-11-02: "the LED load indicator as in the A1200, just the same 1:1"): every core with a floppy / hard disk LED source carries the A1200's
// setting "LED Load Indicator" (On / Off, default On, live, base operation led.load). For each core: the setting is there exactly once, the start sequence
// (regset_apply with every setting) writes U64_CASELED_SELECT once with 0x04 (LED 1 = drive A activity, LED 2 = On) and never again, Off after On
// writes the restoring 0x00 once, and the .cfg keeps the value. Without the setting nobody writes the selector and the LED shows the bring-up meaning.
#include "check.h"
#include "fakes.h"
#include "cores.h"
#include "ops_glue.h"
#include "regset.h"

int main()
{
    extern const CoreDesc amiga_desc;
    const CoreDesc *const cores[] = { &amiga_desc };
    for (const CoreDesc *dp : cores) {
        const CoreDesc &d = *dp;
        int n = 0;
        for (int i = 0; i < d.nsettings; i++) n += strcmp(d.settings[i].key, "LED Load Indicator") == 0;
        CHECK_EQ(n, 1);
        FakeIo io; FakeFs fs; PxRec px;
        CoreCtx c; memset(&c, 0, sizeof(c));
        c.desc = &d; c.io = &io; c.fs = &fs;
        char cfg[80]; snprintf(cfg, sizeof(cfg), "/sd/cores/%s.cfg", d.folder ? d.folder : "x");
        Settings st(d.settings, d.nsettings, d.store, &fs, cfg, 0);
        st.load();
        c.settings = &st;
        Video vid(d.video, &px, &io);
        c.video = &vid;
        int i = st.find("LED Load Indicator");
        CHECK(i >= 0);
        if (i < 0) { printf("no LED setting in %s\n", d.cor_name ? d.cor_name : d.title); continue; }
        CHECK_EQ(st.item(i).when, WHEN_LIVE);
        CHECK_EQ(st.item(i).nvalues, 2);
        CHECK_STR(st.item(i).values[0], "On");
        CHECK_STR(st.item(i).values[1], "Off");
        CHECK_EQ(st.value(i), 0);
        // the start sequence applies every setting: the selector is written once, 0x04
        regset_apply(&c, true);
        CHECK_EQ(io.led_log.size(), 1);
        CHECK(io.led_log.size() == 1 && io.led_log[0].v == 0x04);
        regset_apply(&c, true);
        CHECK_EQ(io.led_log.size(), 1);
        // Off after On: the restoring 0x00 once
        st.set(i, 1);
        regset_apply_setting(&c, i);
        CHECK(io.led_log.size() == 2 && io.led_log[1].v == 0x00);
        CHECK(fs.text(cfg).find("LED Load Indicator=Off") != std::string::npos);
    }
    DONE("test_ledcores");
}
