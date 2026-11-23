// The list of a tape image for the file browser (menuvlist.h): the entries come from the T64 lister (t64.cc), the actions are the C64 family's operations on an entry. Part of the T64 library; its
// `bind` (libtab.h) gives the list to the controller.
#include "menuvlist.h"
#include "t64.h"
#include "ctx.h"
#include "libload.h"
#include <string.h>
#include <stdio.h>

// the entries of a tape image (.t64)
static int t64_entries(CoreCtx *c, const char *file, FsEntry *out, int max)
{
    static T64Entry ents[64];
    int n = t64_list(c->fs, file, ents, 64);
    if (n < 0) {
        return -1;
    }
    int w = 0;
    for (int i = 0; i < n && w < max; i++) {
        memset(&out[w], 0, sizeof(FsEntry));
        snprintf(out[w].name, sizeof(out[w].name), "%s", ents[i].name[0] ? ents[i].name : "(no name)");
        out[w].size = ents[i].end > ents[i].start ? ents[i].end - ents[i].start : 0;
        w++;
    }
    return w;
}

static const OpBind b_run[] = { B_FILE("path"), B_ASK("index") };
static const FileAction t64_acts[] = {
    { "Run PRG", "c64.run_t64", b_run, 2, true, 0, 0, 0, 0, 'R' },
    { "DMA (load, do not run)", "c64.dma_t64", b_run, 2, false, 0, 0, 0, 0, 'A' },
};

static const MenuVList g_t64_list = { "menu.t64", "TAPE: ", "This is no tape image (T64) or it cannot be read", t64_entries, t64_acts, 2 };

void t64_bind(CoreCtx *, LibLoader &)
{
    menu_vlist_add(&g_t64_list);
}
