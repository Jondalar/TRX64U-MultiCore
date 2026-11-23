#include "hooklib.h"
#include "hookhost.h"
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
    int h = ld && name ? ld->handle_of(name) : -1;
    const LibInfo *li = h >= 0 ? ld->info(h) : 0;
    if (!li || li->kind != U2A3_KIND_LIB) {
        snprintf(why, whylen, "the library \"%s\" that is the code of this core is not loaded", name ? name : "");
        return -1;
    }
    if (LIB_RECORD_COVERS(li->record.struct_size, export_list)) {
        for (int k = 0; k < li->record.nexport_list; k++) {
            const LibExportDecl &e = li->record.export_list[k];
            if (!strcmp(e.name, COREHOOKS_NAME) && e.major == (uint32_t)COREHOOKS_MAJOR) {
                g_ld = ld;
                g_handle = h;
                if (hooks_attach_table(e.table, e.size, enter, leave, 0)) {
                    snprintf(why, whylen, "the library \"%s\" lists its table corehooks, but it has no content", name);
                    return -1;
                }
                return 0;
            }
        }
    }
    snprintf(why, whylen, "the library \"%s\" does not export corehooks at major %d", name, COREHOOKS_MAJOR);
    return -1;
}
