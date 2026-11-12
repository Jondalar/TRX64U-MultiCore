// T104.2 D10: the C64 descriptor has no menu tables; its menu is the record the application carries (c64_desc.menu_rec). The tests that need the tasks,
// the file types, the media slots or the settings of the C64 take them from here: the same steps bundle_merge makes for a start without a .cor.
#ifndef TESTS_TRX64U_MENU_H
#define TESTS_TRX64U_MENU_H

#include "cores.h"
#include "descrec.h"
#include "contrib.h"

namespace trx64u {

struct Menu {
    DescRecord rec;
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
        descrec_apply(m->rec, &c64_desc, &m->own);
        Contributor c = { "description", CR_DESC, &m->rec.rec };
        contrib_merge(&m->own, m->rec.has_route ? &m->rec.skel : 0, &m->rec.ov, &c, 1, 0, &m->desc, &m->mm);
    }
    return m;
}

inline const CoreDesc *desc() { return &menu()->desc; }
inline const MenuMerge *merge() { return &menu()->mm; }

}

#endif
