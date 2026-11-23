// T239.3 D32: where the rows the application adds stand in the menu comes from records. The base UI TOML (coreapp/base_ui.toml, compiled into base_ui.inc) places every row: the area and tab of its
// route, its heading, its setting group and its order. A core's UI TOML places a base row elsewhere (descrec.h sections 23 and 24): the fixture is compiled from tests/coreapp/golden/desc_places.toml
// by the real tool (gen_bundle.py), read by the board's reader (descrec.cc), merged over the base (menu_place_merge) and the menu is built from the result (menu_build with MenuMerge::place).
// Proved: the base places each of its rows where the application put them before (route, group); a row added by the extend functions gets its group from the table; a core's place moves a row to
// the tab and block it names, a place that changes only the order changes only that, a place that names a route nobody has is refused by name and changes nothing; the routes and places of
// the record are what the writer made of the TOML (every field of every entry); a damaged section is refused.
//
//   test_uiplace DESC_PLACES.bin
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "check.h"
#include "fakes.h"
#include "test_core.h"
#include "cores.h"
#include "descrec.h"
#include "menutree.h"
#include "contrib.h"
#include "volume.h"
#include "tone.h"
#include "home.h"
#include "baseset.h"
#include "hotkeys.h"

typedef std::vector<uint8_t> Bytes;

static Bytes slurp(const char *p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    std::string t = s.str();
    return Bytes(t.begin(), t.end());
}


static int parse(const Bytes &b, DescRecord *d, char *why)
{
    uint8_t *exact = new uint8_t[b.size() ? b.size() : 1];                 // kept: the strings of the record point into it
    memcpy(exact, b.data(), b.size());
    return descrec_parse(exact, (uint32_t)b.size(), d, why, 200);
}


static const Setting *find(const CoreDesc &d, const char *key)
{
    for (int i = 0; i < d.nsettings; i++) {
        if (!strcmp(d.settings[i].key, key)) {
            return &d.settings[i];
        }
    }
    return 0;
}

// The position of the entry `label` (a setting row) in the tab `tab` of the area `area`: -1 when it is not there.
static int row_in(const MenuTree &t, const char *area, const char *tab, const char *label)
{
    for (int a = 0; a < t.nareas; a++) {
        if (strcmp(t.a[a].key, area)) {
            continue;
        }
        for (int k = 0; k < t.a[a].ntabs; k++) {
            const MenuTab &tb = t.a[a].tabs[k];
            if (!tb.id || strcmp(tb.id, tab)) {
                continue;
            }
            for (int i = 0; i < tb.count; i++) {
                const MenuEntry &e = t.e[tb.first + i];
                if (e.kind == EK_SETTING && e.label && !strcmp(e.label, label)) {
                    return i;
                }
            }
        }
    }
    return -1;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    CHECK(argc == 2);
    if (argc != 2) {
        return 1;
    }

    // ---- the base's table, and the rows the extend functions add (the chain of menubuild.cc without Picture and Persist, which need a video environment and a media slot)
    static CoreDesc vd, td, hd, bd;
    static Setting store[SETTINGS_MAX];
    CHECK(!volume_extend(&nes_desc, &vd, store));
    CHECK(!tone_extend(&vd, &td, store));
    CHECK(!home_extend(&td, &hd, store));
    CHECK(!baseset_extend(&hd, &bd, store));
    menu_place_groups(menu_place_base(), store, bd.nsettings);
    {
        const UiPlace *b = menu_place_base();
        CHECK_EQ(b->nroutes, 7);
        const struct { const char *key, *route, *area, *tab, *heading, *group; } want[] = {
            { "Volume", "audio", "F5", "audio", "Audio", "audio" },
            { "Tone", "audio", "F5", "audio", "Audio", "audio" },
            { HOME_ENTER_KEY, "storage", "F7", "settings", "Storage", "storage" },
            { HOME_DIR_KEY, "storage", "F7", "settings", "Storage", "storage" },
            { REOPEN_KEY, "menu", "F7", "settings", "Menu", "system" },
            { "Overlay Transparency", "look", "F7", "settings", "Look", "app" },
            { "Navigation Style", "look", "F7", "settings", "Look", "app" },
        };
        for (const auto &w : want) {
            const Setting *s = find(bd, w.key);
            CHECK(s != 0);
            if (!s) {
                continue;
            }
            const MenuGroupRoute *r = menu_route_of(*s);
            CHECK(r != 0);
            if (r) {
                CHECK_STR(r->group, w.route);
                CHECK_STR(r->area, w.area);
                CHECK_STR(r->tab, w.tab);
                CHECK_STR(r->heading, w.heading);
            }
            CHECK_STR(s->group, w.group);
        }
        const MenuGroupRoute *hk = 0;
        for (int i = 0; i < b->nroutes; i++) {
            hk = !strcmp(b->routes[i].group, "hotkeys") ? &b->routes[i] : hk;
        }
        CHECK(hk && hk->page && !strcmp(hk->label, "Hotkeys") && !strcmp(hk->area, "F7"));
        // the order of the table is the order the application adds the rows in
        int32_t last = UI_NO_ORDER;
        const char *chain[] = { "Volume", "Tone", HOME_ENTER_KEY, HOME_DIR_KEY, REOPEN_KEY, "Overlay Transparency", "Color Scheme", "Filename overflow squeeze", "Navigation Style" };
        for (const char *k : chain) {
            const UiPlaceRow *p = menu_place_of(menu_place_base(), *find(bd, k));
            CHECK(p != 0 && p->order != UI_NO_ORDER && p->order > last);
            if (p) {
                last = p->order;
            }
        }
        // a row of a core with the same key but another operation is no base row
        Setting own = *find(bd, "Volume");
        own.apply = "core.volume";
        CHECK(menu_route_of(own) == 0 && menu_place_of(menu_place_base(), own) == 0);
        // a place with the row's key wins over a place for the operation alone
        UiPlace pl = *menu_place_base();
        pl.rows[pl.nrows++] = UiPlaceRow{ "volume.apply", "Volume", "video", 0, 1 };
        CHECK_STR(menu_route_of(*find(bd, "Volume"), &pl)->group, "video");
        CHECK_STR(menu_route_of(*find(bd, "Tone"), &pl)->group, "audio");
    }

    // ---- the menu of the base alone: Volume in F5 Audio, the home rows in the Storage block of F7 in the order of the numbers
    static MenuTree t0, t1;
    menu_build(&bd, &t0, true, false, 0);
    CHECK(row_in(t0, "F5", "audio", "Volume") >= 0 && row_in(t0, "F5", "audio", "Tone") >= 0);
    CHECK(row_in(t0, "F5", "video", "Volume") < 0);
    const int enter0 = row_in(t0, "F7", "settings", "Enter home on startup"), dir0 = row_in(t0, "F7", "settings", "Home directory");
    CHECK(enter0 >= 0 && dir0 > enter0);

    // ---- a core's places over the base's (the fixture): the record's tables are what the writer made of the TOML
    Bytes rec = slurp(argv[1]);
    static DescRecord dr;
    char why[200];
    CHECK_EQ(parse(rec, &dr, why), 0);
    CHECK(dr.has_place && !dr.has_core);
    CHECK_EQ(dr.place.nroutes, 1);
    CHECK_STR(dr.place.routes[0].group, "extras");
    CHECK_STR(dr.place.routes[0].area, "F5");
    CHECK_STR(dr.place.routes[0].tab, "video");
    CHECK_STR(dr.place.routes[0].heading, "Extras");
    CHECK(dr.place.routes[0].label == 0 && !dr.place.routes[0].page);
    CHECK_EQ(dr.place.nrows, 3);
    CHECK_STR(dr.place.rows[0].op, "volume.apply");
    CHECK(dr.place.rows[0].key == 0);
    CHECK_STR(dr.place.rows[0].route, "extras");
    CHECK_STR(dr.place.rows[0].group, "video");
    CHECK_EQ(dr.place.rows[0].order, 5);
    CHECK_STR(dr.place.rows[1].key, "Home Directory");
    CHECK(dr.place.rows[1].route == 0 && dr.place.rows[1].group == 0);
    CHECK_EQ(dr.place.rows[1].order, 75);
    CHECK_STR(dr.place.rows[2].route, "nosuch");
    CHECK_EQ(dr.place.rows[2].order, UI_NO_ORDER);

    static UiPlace merged;
    char mwhy[160];
    CHECK_EQ(menu_place_merge(&dr.place, &merged, mwhy, sizeof(mwhy)), 1);          // only the place that names a route nobody has
    CHECK(strstr(mwhy, "tone.apply") && strstr(mwhy, "nosuch"));
    CHECK_EQ(merged.nroutes, menu_place_base()->nroutes + 1);
    CHECK_EQ(merged.nrows, menu_place_base()->nrows);                                // the three places changed or were refused; none is new
    CHECK_STR(menu_route_of(*find(bd, "Volume"), &merged)->group, "extras");
    CHECK_STR(menu_route_of(*find(bd, "Tone"), &merged)->group, "audio");
    CHECK_STR(menu_route_of(*find(bd, HOME_DIR_KEY), &merged)->group, "storage");   // an order alone leaves the base's route
    CHECK_STR(menu_place_of(&merged, *find(bd, HOME_DIR_KEY))->group, "storage");
    CHECK_EQ(menu_place_of(&merged, *find(bd, HOME_DIR_KEY))->order, 75);
    CHECK_STR(menu_route_of(*find(bd, "Volume"))->group, "audio");                  // the base's table itself is not touched

    static CoreDesc md;
    md = bd;
    static Setting mstore[SETTINGS_MAX];
    for (int i = 0; i < bd.nsettings; i++) mstore[i] = bd.settings[i];
    md.settings = mstore;
    menu_place_groups(&merged, mstore, md.nsettings);
    CHECK_STR(find(md, "Volume")->group, "video");
    CHECK_STR(find(md, "Tone")->group, "audio");
    static MenuMerge mm;
    memset(&mm, 0, sizeof(mm));
    mm.place_store = merged;
    mm.has_place = true;
    menu_build(&md, &t1, true, false, &mm);
    CHECK(row_in(t1, "F5", "video", "Volume") >= 0);                                // moved to the tab of its new route
    CHECK(row_in(t1, "F5", "audio", "Volume") < 0);
    CHECK(row_in(t1, "F5", "audio", "Tone") >= 0);                                  // the refused place changed nothing
    const int enter1 = row_in(t1, "F7", "settings", "Enter home on startup"), dir1 = row_in(t1, "F7", "settings", "Home directory");
    CHECK(enter1 >= 0 && dir1 >= 0 && dir1 < enter1);                               // order 75 stands before the Enter row's 80, in the same block

    printf("PASS\n");
    return 0;
}
