#include "libtab.h"
#include "desc.h"

void libtab_bind(CoreCtx *c, LibLoader &ld)
{
    for (unsigned i = 0; i < g_libtab_n; i++) {
        if (g_libtab[i].bind) {
            g_libtab[i].bind(c, ld);
        }
    }
}

void libtab_register_ops(OpRegistry &r, const CoreDesc *d)
{
    for (unsigned i = 0; i < g_libtab_n; i++) {
        const LibEntry &e = g_libtab[i];
        if (e.register_ops && (!e.capability || (d && desc_has_capability(d, e.capability)))) {
            e.register_ops(r);
        }
    }
}

void libtab_init(const BoardProfile &b)
{
    for (unsigned i = 0; i < g_libtab_n; i++) {
        if (g_libtab[i].init) {
            g_libtab[i].init(b);
        }
    }
}

void libtab_start(CoreCtx *c)
{
    for (unsigned i = 0; i < g_libtab_n; i++) {
        if (g_libtab[i].start) {
            g_libtab[i].start(c);
        }
    }
}

bool libtab_freeze_request(void)
{
    bool want = false;
    for (unsigned i = 0; i < g_libtab_n; i++) {                       // every library is asked: each takes its own request
        if (g_libtab[i].freeze_request && g_libtab[i].freeze_request()) {
            want = true;
        }
    }
    return want;
}
