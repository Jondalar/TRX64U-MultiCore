// T239.3 D32, P2-4's QC entry: the rows of the seven extend functions (home_extend, baseset_extend, hk_extend, picture_extend, tone_extend, volume_extend, persist_extend) are rows of the base
// UI TOML (coreapp/base_ui.toml, compiled into base_rows.inc by tools/gen_base_ui.py): label, key, kind, values and default, and a pick for a list that changes at run time. This test reads
// the compiled table and checks it against the code that serves it: the keys and operations the code names are rows, the values the code keeps in a table of its own (the key names, the modifier
// presets, the scale names, the view's colour schemes, the volume steps) are the row's, a pick is a list the code serves, and the extend functions hand out the row as it is.
#include <string>
#include <string.h>
#include <strings.h>
#include "check.h"
#include "fakes.h"
#include "test_core.h"
#include "cores.h"
#include "menubuild.h"
#include "menutree.h"
#include "volume.h"
#include "tone.h"
#include "home.h"
#include "hotkeys.h"
#include "baseset.h"
#include "mediamounts.h"
#include "videomode.h"

static const BaseRow *row(const char *op, const char *key) { return menu_base_row(op, key); }


static bool pick_is(const BaseRow *r, const char *name)
{
    return r && r->pick && !strcmp(r->pick, name);
}

int main()
{
    int n = 0;
    const BaseRow *all = menu_base_rows(&n);
    CHECK(n > 0);
    // every row is whole and found once by its operation and key; every row has a place (the base UI TOML's own check says the same at build time)
    const UiPlace *pl = menu_place_base();
    for (int i = 0; i < n; i++) {
        const BaseRow &r = all[i];
        CHECK(r.key && r.key[0] && r.apply && r.apply[0] && r.values && r.nvalues >= 1);
        CHECK(r.def >= 0 && r.def < r.nvalues);
        CHECK(r.kind == 0 || r.kind == 1);
        CHECK(!r.pick || !strcmp(r.pick, "video.modes") || !strcmp(r.pick, "hotkeys.modifiers") || !strcmp(r.pick, "hotkeys.keys"));
        CHECK(r.pick || r.nvalues >= 2 || r.kind == 1);                    // a row with no pick is a choice among values, or a text
        for (int j = i + 1; j < n; j++) {
            CHECK(strcmp(all[j].apply, r.apply) || strcasecmp(all[j].key, r.key));
        }
        Setting s;
        menu_base_setting(s, r);
        CHECK(menu_place_of(pl, s) != 0);
        CHECK_EQ(s.when, WHEN_LIVE);
        CHECK(!s.bind && !s.group && !s.lock_state);
    }

    // the keys and operations the code names are rows
    CHECK(row(PICTURE_OP, PICTURE_KEY) && row(SCALE_OP, SCALE_KEY) && row(SMOOTH_OP, SMOOTH_KEY) && row(FOLLOW_OP, FOLLOW_KEY));
    CHECK(row(VOLUME_OP, VOLUME_KEY) && row(TONE_OP, TONE_KEY) && row(PERSIST_OP, PERSIST_KEY));
    CHECK(row(HOME_OP, HOME_DIR_KEY) && row(HOME_OP, HOME_ENTER_KEY) && row(HOME_OP, REOPEN_KEY));
    CHECK(row(HK_OP, HK_KEY_MOD));
    for (int i = 0; i < baseset_count(); i++) {
        bool found = false;
        for (int j = 0; j < n; j++) {
            found = found || !strcmp(all[j].key, baseset_key(i));
        }
        CHECK(found);
    }

    // Picture names its pick, and the row's values are what it shows until the list has filled it
    CHECK(pick_is(row(PICTURE_OP, PICTURE_KEY), "video.modes"));
    // what the code keeps in a table of its own is the row's
    const BaseRow *sc = row(SCALE_OP, SCALE_KEY);
    CHECK_EQ(sc->nvalues, VID_SCALE_COUNT);
    for (int i = 0; i < VID_SCALE_COUNT; i++) {
        CHECK_STR(sc->values[i], video_scale_name(i));
    }
    CHECK_EQ(sc->def, VID_SCALE_INTEGER);                                      // the VIC path's Fit is the code's (picture_extend)
    const BaseRow *vol = row(VOLUME_OP, VOLUME_KEY);
    CHECK_EQ(vol->nvalues, VOLUME_STEPS);
    CHECK_EQ(vol->def, VOLUME_DEFAULT);
    for (int i = 0; i < VOLUME_STEPS; i++) {
        char want[16];
        snprintf(want, sizeof(want), "%d%%", i * 5);
        CHECK_STR(vol->values[i], want);
    }
    const BaseRow *tone = row(TONE_OP, TONE_KEY);
    CHECK_EQ(tone->nvalues, TONE_STEPS);
    CHECK_EQ(tone->def, TONE_DEFAULT);
    CHECK(!row("base.scheme", 0) && !row("base.overlay", 0));                 // T239.3 D56: the menu's colour scheme and transparency are no base rows
    const BaseRow *mod = row(HK_OP, HK_KEY_MOD);
    CHECK(pick_is(mod, "hotkeys.modifiers"));
    CHECK_EQ(mod->nvalues, hk_modifier_presets());
    for (int i = 0; i < mod->nvalues; i++) {
        CHECK_STR(mod->values[i], hk_modifier_name(i));
    }
    int nkeyrows = 0;
    for (int j = 0; j < n; j++) {
        if (pick_is(&all[j], "hotkeys.keys")) {
            nkeyrows++;
            CHECK_EQ(all[j].nvalues, HK_NKEYS + 1);
            CHECK_STR(all[j].values[0], "Off");
            for (int k = 0; k < HK_NKEYS; k++) {
                CHECK_STR(all[j].values[k + 1], hk_key_name(k));
            }
        }
    }
    CHECK_EQ(nkeyrows, HKA_CORE);                                              // the six fixed actions; the core's own function and the tasks are the core's rows
    const BaseRow *mm = row("base.mouse_sens", 0);
    CHECK(mm->show_state && !strcmp(mm->show_state, "set.Mouse_Mode==Cursor keys") && mm->base == 1 && mm->nvalues == 16);
    DONE("test_baserows");
}
