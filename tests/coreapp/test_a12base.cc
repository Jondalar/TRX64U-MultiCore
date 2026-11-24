// T213.3 P3 (the A1200's cases of test_home, test_reopen, test_volume, test_tone and test_baseset, which run on the A500's record in the shared app since the A1200 left it): the
// application's own rows on the A1200's record. No descriptor declares Volume, Tone or the Home items; the base adds them (Volume and Tone one row each, Home three) and leaves the
// A1200's own mouse rows (operations mouse.sens / mouse.accel) alone. Built as the A1200's app (run.sh a12app, TEST_APP_A1200).
#include <string.h>
#include <strings.h>
#include "check.h"
#include "fakes.h"
#include "cores.h"
#include "settings.h"
#include "volume.h"
#include "tone.h"
#include "home.h"
#include "baseset.h"

static int count_key(const CoreDesc &d, const char *key)
{
    int n = 0;
    for (int i = 0; i < d.nsettings; i++) n += strcasecmp(d.settings[i].key, key) == 0;
    return n;
}

int main()
{
    static CoreDesc vd, td, hd, h1, h2;
    static Setting vs[SETTINGS_MAX], ts[SETTINGS_MAX], hs[SETTINGS_MAX], s1[SETTINGS_MAX], s2[SETTINGS_MAX];
    CHECK_EQ(count_key(amiga1200_desc, VOLUME_KEY), 0);                  // no descriptor declares them: the application adds them
    CHECK_EQ(count_key(amiga1200_desc, TONE_KEY), 0);
    CHECK_EQ(count_key(amiga1200_desc, HOME_DIR_KEY), 0);
    CHECK_EQ(count_key(amiga1200_desc, HOME_ENTER_KEY), 0);
    CHECK(!volume_extend(&amiga1200_desc, &vd, vs));                     // the A1200 gets the same item as every core
    CHECK_EQ(vd.nsettings, amiga1200_desc.nsettings + 1);
    CHECK_EQ(count_key(vd, VOLUME_KEY), 1);
    CHECK(volume_is_generic(vd.settings[vd.nsettings - 1]));
    CHECK(!tone_extend(&amiga1200_desc, &td, ts));
    CHECK_EQ(td.nsettings, amiga1200_desc.nsettings + 1);
    CHECK_EQ(count_key(td, TONE_KEY), 1);
    CHECK(!home_extend(&amiga1200_desc, &hd, hs));
    CHECK_EQ(hd.nsettings, amiga1200_desc.nsettings + 3);
    CHECK_EQ(count_key(hd, HOME_DIR_KEY), 1);
    CHECK_EQ(count_key(hd, HOME_ENTER_KEY), 1);
    CHECK(!home_extend(&amiga1200_desc, &h1, s1));                       // a description that has the items keeps them: no second pair
    CHECK(!home_extend(&h1, &h2, s2));
    CHECK_EQ(h2.nsettings, h1.nsettings);
    // D25: the A1200 declares Mouse Sensitivity / Acceleration itself; the application adds neither
    CHECK_EQ(count_key(amiga1200_desc, "Mouse Sensitivity"), 1);
    CHECK_EQ(count_key(amiga1200_desc, "Mouse Acceleration"), 1);
    CHECK(!baseset_adds(&amiga1200_desc, "Mouse Sensitivity") && !baseset_adds(&amiga1200_desc, "Mouse Acceleration"));
    DONE("a12base");
}
