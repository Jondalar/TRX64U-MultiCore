// The route of the default menu with the Library drawn by its screen, as the TRX64U's record draws it (layout "library"). The Library opens on a medium and a
// medium is chosen with the chips of the path row, which only the screen has; a test of the folder walks gives its context this merge (c.merge = lib_merge()).
#ifndef TESTS_LIB_SKEL_H
#define TESTS_LIB_SKEL_H
#include "contrib.h"
#include "menuctl.h"
#include "keyboard.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>

static inline const MenuMerge *lib_merge()
{
    static const SkelTab t_home[] = { { "home", "Home", 0, TW_INFO, 0, 0 } };
    static const SkelTab t_lib[] = { { "browse", "Browse", 0, TW_BROWSER, 0, "library" } };
    static const SkelTab t_machine[] = { { "summary", "Summary", 0, TW_MEDIA, 0, 0 }, { "settings", "Settings", 0, TW_SETTINGS, 0, 0 } };
    static const SkelTab t_actions[] = { { "quick", "Quick", 0, TW_TASKS, 0, 0 } };
    static const SkelTab t_system[] = { { "expert", "Expert", 0, TW_SYSTEM, 0, 0 } };
    static const SkelArea areas[] = {
        { "F1", "Home", false, t_home, 1 },
        { "F3", "Library", false, t_lib, 1 },
        { "F5", 0, false, t_machine, 2 },
        { "F7", "Actions", false, t_actions, 1 },
        { "F8", "System", true, t_system, 1 },
    };
    static const Skeleton skel = { areas, 5 };
    static MenuMerge mm;
    if (!mm.skel) {
        memset(&mm, 0, sizeof(mm));
        mm.skel = &skel;
    }
    return &mm;
}

// The media level (T104.2, as the host's File Browser): LEFT at a medium's top goes up to "/", the list of the media; a medium is opened by its row.
static inline bool lib_nowhere(MenuCtl *m)                          // the browser on the media level
{
    for (int i = 0; i < 12 && strcmp(m->title(), "/") != 0; i++) m->key(KEY_LEFT);
    return !strcmp(m->title(), "/");
}

// Opens the medium `name` ("SD", "usb0") from wherever the browser is: up to the media level, the row, RETURN. False when the list has no such row.
static inline bool lib_medium(MenuCtl *m, const char *name)
{
    if (!lib_nowhere(m)) return false;
    for (int i = 0; i < m->rows(); i++) {
        char l[100], r[100]; bool a;
        m->row(i, l, 100, r, 100, &a);
        size_t n = strlen(l);
        if (n && l[n - 1] == '/') n--;
        if (strlen(name) == n && !strncasecmp(l, name, n)) {
            while (m->selected() > i) m->key(KEY_UP);
            while (m->selected() < i) m->key(KEY_DOWN);
            m->key(KEY_RETURN);
            return true;
        }
    }
    return false;
}
#endif
