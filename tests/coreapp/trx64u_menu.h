// T104.2 D10: the C64 descriptor has no menu tables; its menu is the record the application carries (c64_desc.menu_rec). The tests that need the tasks,
// the file types, the media slots or the settings of the C64 take them from here: the same steps the board makes (run.cc: menu_build extends c64_desc with
// Picture, Volume, Persist, Home and the hotkeys; bundle.cc: descrec_apply puts the record's own menu in front of those items; contrib_merge adds the rest).
#ifndef TESTS_TRX64U_MENU_H
#define TESTS_TRX64U_MENU_H

#include "cores.h"
#include "descrec.h"
#include "contrib.h"
#include "menubuild.h"
#include "picture.h"

namespace trx64u {

struct Menu {
    DescRecord rec;
    CoreDesc ext;                 // c64_desc after menu_build, the base the record is applied over
    Setting store[SETTINGS_MAX];
    CoreDesc own_raw;             // the record over the raw c64_desc: what desc_check reads (it refuses the application's own keys in a core's table)
    CoreDesc own;
    CoreDesc desc;                // c64_desc with the record's own menu merged in
    MenuMerge mm;
};

inline Menu *menu()
{
    static Menu *m = 0;
    if (!m) {
        m = new Menu;
        char why[200];
        if (descrec_parse(c64_desc.menu_rec, c64_desc.menu_rec_n, &m->rec, why, sizeof(why)) != 0) {
            fprintf(stderr, "the built-in menu record of the C64 is bad: %s\n", why);
            abort();
        }
        PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };
        if (menu_build(&c64_desc, &m->ext, m->store, &env)) {
            fprintf(stderr, "menu_build refused an item of the C64\n");
            abort();
        }
        descrec_apply(m->rec, &m->ext, &m->own);
        {
            static DescRecord raw;                   // a copy: descrec_apply writes the base's settings behind the record's own
            raw = m->rec;
            descrec_apply(raw, &c64_desc, &m->own_raw);
        }
        Contributor c = { "description", CR_DESC, &m->rec.rec };
        contrib_merge(&m->own, m->rec.has_route ? &m->rec.skel : 0, &m->rec.ov, &c, 1, 0, &m->desc, &m->mm);
    }
    return m;
}

inline const CoreDesc *desc() { return &menu()->desc; }
inline const CoreDesc *checked() { return &menu()->own_raw; }     // the C64's descriptor as desc_check sees it: the record, without the application's items
inline const MenuMerge *merge() { return &menu()->mm; }

}

#endif
