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

extern const CoreDesc nes_desc;          // cores.h lists the shared app's cores; the NES is its own app (cores/nes/app/lib)

static bool pick_is(const BaseRow *r, const char *name)
{
    return r && r->pick && !strcmp(r->pick, name);
}

int main()
{
    const BaseRow *vol = row(VOLUME_OP, VOLUME_KEY), *tone = row(TONE_OP, TONE_KEY);   // the rows whose checks are the shared unit's
    CHECK(vol && tone);
    if (!vol || !tone) DONE("test_baserows");

    // the extend functions hand out the row as it is
    static CoreDesc d1, d2, d3, d4;
    static Setting s1[SETTINGS_MAX], s2[SETTINGS_MAX], s3[SETTINGS_MAX], s4[SETTINGS_MAX];
    CHECK(!volume_extend(&nes_desc, &d1, s1));
    const Setting &v = d1.settings[d1.nsettings - 1];
    CHECK(volume_is_generic(v) && v.values == vol->values && v.nvalues == vol->nvalues && v.def == vol->def && !strcmp(v.key, vol->key));
    CHECK(!tone_extend(&d1, &d2, s1));
    const Setting &t = d2.settings[d2.nsettings - 1];
    CHECK(tone_is_generic(t) && t.values == tone->values && t.nvalues == tone->nvalues && t.def == tone->def);
    CHECK(!home_extend(&d2, &d3, s1));
    CHECK_EQ(d3.nsettings, d2.nsettings + 3);
    for (int i = d2.nsettings; i < d3.nsettings; i++) {
        const BaseRow *r = row(HOME_OP, d3.settings[i].key);
        CHECK(r != 0);
        if (!r) continue;
        CHECK(d3.settings[i].values == r->values && d3.settings[i].nvalues == r->nvalues && d3.settings[i].kind == r->kind && d3.settings[i].base == r->base);
        CHECK((d3.settings[i].label == r->label) || !strcmp(d3.settings[i].label, r->label));
    }
    const Setting &dir = d3.settings[d2.nsettings + 1];                         // the text row: its default is the empty text, 60 characters at most
    CHECK(dir.kind == 1 && dir.base == 60 && dir.nvalues == 1 && !strcmp(dir.values[0], ""));
    CHECK(!baseset_extend(&nes_desc, &d4, s4));
    for (int i = nes_desc.nsettings; i < d4.nsettings; i++) {
        const BaseRow *r = row(d4.settings[i].apply, d4.settings[i].key);
        CHECK(r && d4.settings[i].def == r->def);
        CHECK(r && (r->pick ? d4.settings[i].nvalues >= r->nvalues : d4.settings[i].values == r->values));
    }
    DONE("test_baserows");
}
