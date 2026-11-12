// A file that is a list for the browser (T192.3 leak 6): the entries of a tape image are rows like a folder's. A file action whose operation is the `op` of a provider opens its list;
// the provider says how to list the file, what the list is called and what an entry's actions are. The controller knows no format.
#ifndef COREAPP_MENUVLIST_H
#define COREAPP_MENUVLIST_H

#include "desc.h"
#include "fsif.h"

struct MenuVList {
    const char *op;                 // the file action that opens it ("menu.t64")
    const char *label;              // the list's name in front of the file's: "TAPE: file.t64"
    const char *bad;                // the status line when the file cannot be listed
    // Fills out[0 .. max) with the entries (name and size; no folders). Returns how many, -1 when the file is not that format or cannot be read.
    int (*list)(CoreCtx *c, const char *file, FsEntry *out, int max);
    const FileAction *acts;         // what is done with an entry: the entry's number goes to the action's B_ASK argument
    int nacts;
};

// The provider for a file action's operation, null when it opens none.
const MenuVList *menu_vlist(const char *op);

#endif
