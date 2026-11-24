// A core whose menu is a record (T239.3 P1-8, in place of trx64u_menu.h): a base descriptor and the record bytes go in; the descriptor with the record merged in, the one
// desc_check reads and the merge come out. The steps are the board's (run.cc: menu_build extends the base with Picture, Volume, Persist, Home and the hotkeys; bundle.cc:
// descrec_apply puts the record's own menu in front of those items, hk_extend_tasks adds the hotkey items of its tasks; contrib_merge adds the rest). The record is one of
// tests/coreapp/golden/*.toml compiled by gen_bundle.py; a test names its .bin on the command line.
#ifndef TESTS_RECORD_MENU_H
#define TESTS_RECORD_MENU_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <iterator>
#include <vector>
#include "cores.h"
#include "descrec.h"
#include "contrib.h"
#include "hotkeys.h"
#include "menubuild.h"
#include "picture.h"

struct RecordMenu {
    std::vector<uint8_t> bytes;
    DescRecord rec;
    CoreDesc ext;                 // the base after menu_build, the record is applied over it
    Setting store[SETTINGS_MAX];
    CoreDesc own_raw;             // the record over the raw base: what desc_check reads (it refuses the application's own keys in a core's table)
    CoreDesc own;
    CoreDesc desc;                // the base with the record's own menu merged in
    MenuMerge mm;

    // the file of a compiled record (gen_bundle.py's output); aborts when it is missing or bad, as a test cannot go on without it
    RecordMenu(const CoreDesc *base, const char *path)
    {
        std::ifstream f(path, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        char why[200];
        if (bytes.empty() || descrec_parse(bytes.data(), (int)bytes.size(), &rec, why, sizeof(why)) != 0) {
            fprintf(stderr, "the record %s is bad: %s\n", path, bytes.empty() ? "no such file" : why);
            abort();
        }
        PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };
        if (menu_build(base, &ext, store, &env)) {
            fprintf(stderr, "menu_build refused an item of the base\n");
            abort();
        }
        descrec_apply(rec, &ext, &own);
        if (const char *w = hk_extend_tasks(&own, rec.csettings, DR_CSETS)) {      // bundle.cc runs it here: the hotkey items of the record's tasks (T228 R1)
            fprintf(stderr, "hk_extend_tasks: %s\n", w);
            abort();
        }
        {
            static DescRecord raw;                   // a copy: descrec_apply writes the base's settings behind the record's own
            if (rec.sec) {
                fprintf(stderr, "the record has data sections: it cannot be copied byte for byte\n");
                abort();
            }
            memcpy((void *)&raw, &rec, sizeof(raw));
            descrec_apply(raw, base, &own_raw);
        }
        Contributor c = { "description", CR_DESC, &rec.rec };
        contrib_merge(&own, rec.has_route ? &rec.skel : 0, &rec.ov, &c, 1, 0, &desc, &mm);
        mm.keys = rec.keys;                          // bundle.cc bundle_merge does the same: the description's key table (base 3.3)
        mm.nkeys = rec.nkeys;
    }
    const CoreDesc *checked() const { return &own_raw; }
};

#endif
