#include "menuvlist.h"
#include <string.h>

// The providers a linked library has added (menuvlist.h): a few, kept as pointers to the library's own static lists.
#define VLISTS_MAX 4
static const MenuVList *g_lists[VLISTS_MAX];

bool menu_vlist_add(const MenuVList *l)
{
    for (int i = 0; l && i < VLISTS_MAX; i++) {
        if (g_lists[i] == l) {
            return true;
        }
        if (!g_lists[i]) {
            g_lists[i] = l;
            return true;
        }
    }
    return false;
}

const MenuVList *menu_vlist(const char *op)
{
    for (int i = 0; op && i < VLISTS_MAX && g_lists[i]; i++) {
        if (!strcmp(g_lists[i]->op, op)) {
            return g_lists[i];
        }
    }
    return 0;
}
