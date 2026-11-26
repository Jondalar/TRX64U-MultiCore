#include "hooklib.h"
#include "hookhost.h"
#include "libslot.h"
#include <new>
#include <stdio.h>
#include <string.h>

namespace {
LibLoader *g_ld;
int g_handle = -1;

typedef char token_size_check[(sizeof(LibLoader::Scope) <= HOOK_TOKEN_BYTES && alignof(LibLoader::Scope) <= 8) ? 1 : -1];

void enter(void *, void *token)
{
    new (token) LibLoader::Scope(g_ld, g_handle);
}

void leave(void *, void *token)
{
    ((LibLoader::Scope *)token)->~Scope();
}
}

int hooks_attach_lib(LibLoader *ld, const char *name, char *why, int whylen)
{
    hooks_detach();
    if (why && whylen > 0) {
        why[0] = 0;
    }
    // base 4.0 (libslot.h): the export "corehooks" of the [[library]] of that name in the TOML B; a library linked with `register` delivered it at start (LibLoader::link)
    // and holds a slot, whose account the calls go on
    int h = ld && name ? ld->handle_of(name) : -1;
    for (unsigned i = 0; name && i < g_libslot_n; i++) {
        if (strcmp(g_libslot[i].lib, name) || strcmp(g_libslot[i].name, COREHOOKS_NAME) || g_libslot[i].major != (uint32_t)COREHOOKS_MAJOR) {
            continue;
        }
        uint32_t size = 0;
        const void *t = lib_slot(i, &size);
        g_ld = ld;
        g_handle = h;
        if (hooks_attach_table(t, size, enter, leave, 0)) {
            snprintf(why, whylen, "the library \"%s\" declares its table corehooks, but it has no content", name);
            return -1;
        }
        return 0;
    }
    snprintf(why, whylen, "the library \"%s\" does not export corehooks at major %d", name, COREHOOKS_MAJOR);
    return -1;
}
