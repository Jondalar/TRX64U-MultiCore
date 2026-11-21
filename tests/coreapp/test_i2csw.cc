// T252 S1: the I2C switches. The Port Diagnostics core's menu ends with the four rows (defaults = T242), a core without the capability gets none, and each
// row - applied as at the start (regset_apply_setting) and changed as from the menu or REST (settings.set) - calls its board function or the screen check.
#include "check.h"
#include "fakes.h"
#include "cores.h"
#include "menubuild.h"
#include "ops_glue.h"
#include "regset.h"
#include "settings.h"
#include "settings_ops.h"
#include "i2csw.h"
#include "picture.h"
#include "portdiag.h"
#include "diagtest.h"
#include "desc_check.h"
#include "edid.h"
#include "videomode.h"

static int g_scan = -1, g_wait = -1, g_exp = -1, g_nscan = 0, g_nexp = 0;
static void t_scan(bool on) { g_scan = on ? 1 : 0; g_nscan++; }
static void t_wait(int ticks) { g_wait = ticks; }
static void t_exp(int which) { g_exp = which; g_nexp++; }

int main()
{
    i2csw_hw.scan = t_scan;
    i2csw_hw.park_wait = t_wait;
    i2csw_hw.expander = t_exp;

    // the rows: after the core's own settings and the application's (Picture, Volume, ...), in this order, T242's behaviour as the default
    static Setting store[SETTINGS_MAX];
    CoreDesc out;
    PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };
    CHECK(!menu_build(&portdiag_desc, &out, store, &env));
    CHECK(out.nsettings >= DT_AV_N + I2CSW_N);
    const Setting *s = &out.settings[out.nsettings - I2CSW_N];
    CHECK_STR(s[0].key, I2CSW_KEY_SCAN);
    CHECK_STR(s[0].apply, I2CSW_OP_SCAN);
    CHECK_EQ(s[0].nvalues, 2);
    CHECK_STR(s[0].values[s[0].def], "On");
    CHECK_STR(s[1].key, I2CSW_KEY_POLL);
    CHECK_STR(s[1].values[s[1].def], "On");
    CHECK_STR(s[2].key, I2CSW_KEY_WAIT);
    CHECK_STR(s[2].values[s[2].def], "None");
    CHECK_STR(s[3].key, I2CSW_KEY_EXP);
    CHECK_EQ(s[3].nvalues, 3);
    CHECK_STR(s[3].values[s[3].def], "None");
    for (int k = 0; k < I2CSW_N; k++) {
        CHECK_EQ((int)s[k].when, (int)WHEN_LIVE);
        CHECK(s[k].group == 0);                        // listed with the core's own settings (its settings tab)
    }
    CHECK(!desc_check(&portdiag_desc));                // the check registers the core's operations and the rows' (no name taken twice)

    // the same core without the capability: no row
    {
        static const char *const only_pad[] = { "pad" };
        CoreDesc nocap = portdiag_desc;
        nocap.caps = only_pad;
        nocap.ncaps = 1;
        static Setting store2[SETTINGS_MAX];
        CoreDesc out2;
        CHECK(!menu_build(&nocap, &out2, store2, &env));
        for (int i = 0; i < out2.nsettings; i++) {
            CHECK(strcmp(out2.settings[i].key, I2CSW_KEY_SCAN) != 0);
        }
        CHECK_EQ(out2.nsettings, out.nsettings - I2CSW_N);
        FakeIo io2;
        CoreCtx c2;
        memset(&c2, 0, sizeof(c2));
        c2.desc = &out2;
        c2.io = &io2;
        CHECK(ops_set(&c2, I2CSW_OP_SCAN, 0) != OPE_OK);  // no operation either
        CHECK_EQ(g_nscan, 0);
    }

    // the operations, as the start applies them and as a change does
    FakeIo io;
    FakeFs fs;
    CoreCtx c;
    memset(&c, 0, sizeof(c));
    c.desc = &out;
    c.io = &io;
    c.fs = &fs;
    snprintf(c.cor_path, sizeof(c.cor_path), "/SD/cores/diag.cor");
    Settings st(out.settings, out.nsettings, out.store, &fs, "/SD/cores/diag.cfg", 0);
    st.load();
    c.settings = &st;
    int first = out.nsettings - I2CSW_N;
    for (int k = 0; k < I2CSW_N; k++) {
        regset_apply_setting(&c, first + k);           // the start (regset_apply): the defaults
    }
    CHECK_EQ(g_scan, 1);
    CHECK(picture_poll_on());
    CHECK_EQ(g_wait, 0);
    CHECK_EQ(g_exp, 0);

    OpResult r;
    CHECK_EQ(settings_set(&c, I2CSW_KEY_SCAN, "Off", &r), OPE_OK);
    CHECK_EQ(g_scan, 0);
    CHECK_EQ(settings_set(&c, I2CSW_KEY_POLL, "Off", &r), OPE_OK);
    CHECK(!picture_poll_on());
    CHECK_EQ(settings_set(&c, I2CSW_KEY_WAIT, "2 ticks", &r), OPE_OK);
    CHECK_EQ(g_wait, 2);
    CHECK_EQ(settings_set(&c, I2CSW_KEY_EXP, "Before T242", &r), OPE_OK);
    CHECK_EQ(g_exp, 1);
    CHECK_EQ(settings_set(&c, I2CSW_KEY_EXP, "PR 112", &r), OPE_OK);
    CHECK_EQ(g_exp, 2);
    CHECK_EQ(settings_set(&c, I2CSW_KEY_SCAN, "On", &r), OPE_OK);
    CHECK_EQ(g_scan, 1);
    CHECK_EQ(settings_set(&c, I2CSW_KEY_POLL, "On", &r), OPE_OK);
    CHECK(picture_poll_on());
    CHECK_EQ(settings_set(&c, I2CSW_KEY_WAIT, "None", &r), OPE_OK);
    CHECK_EQ(g_wait, 0);

    // the values stay in the core's .cfg: a second start reads them back
    CHECK_EQ(settings_set(&c, I2CSW_KEY_SCAN, "Off", &r), OPE_OK);
    Settings again(out.settings, out.nsettings, out.store, &fs, "/SD/cores/diag.cfg", 0);
    again.load();
    CHECK_STR(out.settings[first].values[again.value(first)], "Off");

    DONE("test_i2csw");
}
