#include "libtab.h"
#include "desc.h"
#include <stdio.h>

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
        if (!e.register_ops) {
            continue;
        }
        bool want = !e.capability;
        for (unsigned k = 0; d && e.capability && e.capability[k] && !want; k++) {
            want = desc_has_capability(d, e.capability[k]);
        }
        if (want) {
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

void libtab_core(CoreCtx *c, const CoreDesc *d, const DescRecord *rec)
{
    for (unsigned i = 0; i < g_libtab_n; i++) {
        if (g_libtab[i].core) {
            g_libtab[i].core(c, d, rec);
        }
    }
}

void libtab_link(LibLoader &ld, bool (*menu)(void *user, const char *name, DescRecord **rec, uint8_t **bytes), void *user, void (*say)(const char *line), int *loaded,
                 int *refused, char *first_refusal, int first_len)
{
    for (unsigned i = 0; i < g_libtab_n; i++) {
        const LibEntry &e = g_libtab[i];
        if (!e.reg) {
            continue;
        }
        DescRecord *rec = 0;
        uint8_t *bytes = 0;
        if (menu && !menu(user, e.name, &rec, &bytes)) {
            rec = 0;
            bytes = 0;
        }
        char err[LIB_ERR_MAX];
        int rc = ld.link(e.name, e.reg, e.heap_keep, e.heap_peak, rec, bytes, err, sizeof(err));     // the loader owns the record from the call on, refused or not
        if (rc == LIB_OK) {
            if (loaded) {
                (*loaded)++;
            }
            continue;
        }
        if (refused && !(*refused)++ && first_refusal && first_len > 0) {
            snprintf(first_refusal, first_len, "%s", err);
        }
        if (say) {
            say(err);
        }
    }
}
