// The table of the CBM program loader library (app/loaders/cbm_prg.cc): its table "cbmprg" (T239.3 D12, base 3.3), answered by api->lib->find under its export name. This file is part of the library: libtab.cc (from the TOML B of the app) calls cbmprg_bind once, when the loader is made.
// The bodies call the code of the library with the core's context that is given here once, so no member of a table takes one.
#include "libexports.h"
#include "libexp_util.h"
#include "cbmprg_api.h"
#include "cbm_prg.h"
#include "ctx.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static CoreCtx *g_c;

// ---- cbmprg (cbmprg_api.h) ---------------------------------------------------------------------------------------------------------------------------

// The machine of the caller as the parameters of cbm_prg.cc: the members its `size` covers are read, the ones past it are 0 (members are only appended inside a major). A size that does
// not reach max_size, the last member of the first layout, is no machine. The memory is the running core's, through its io.
static bool prg_params(const CbmPrgMachine *m, CbmPrgParams *p)
{
    if (!m || !g_c || !g_c->io || m->size < offsetof(CbmPrgMachine, max_size) + sizeof(m->max_size)) {
        return false;
    }
    CbmPrgMachine all;
    memset(&all, 0, sizeof(all));
    memcpy(&all, m, m->size < sizeof(all) ? m->size : sizeof(all));
    memset(p, 0, sizeof(*p));
    for (int i = 0; i < CBM_BASIC_STARTS_MAX; i++) {
        p->basic_starts[i] = all.basic_starts[i];
    }
    p->nbasic_starts = all.nbasic_starts;
    p->txttab = all.txttab;
    p->txttab_min = all.txttab_min;
    p->txttab_max = all.txttab_max;
    p->vartab = all.vartab;
    p->arytab = all.arytab;
    p->strend = all.strend;
    p->kbd_buffer = all.kbd_buffer;
    p->kbd_count = all.kbd_count;
    p->ready_ms = all.ready_ms;
    p->p00_header = all.p00_header;
    p->min_size = all.min_size;
    p->max_size = all.max_size;
    p->mem_ddr = all.mem_ddr;
    return true;
}

static int prg_place(const uint8_t *data, uint32_t n, const CbmPrgMachine *m, bool is_p00, CbmPrgPlaced *out, char *msg, int msglen)
{
    REASON(msg, msglen);
    msg[0] = 0;
    CbmPrgParams p;
    if (!data || !out) {
        snprintf(msg, msglen, "No program given.");
        return 1;
    }
    if (!prg_params(m, &p)) {
        memset(out, 0, sizeof(*out));
        snprintf(msg, msglen, "No machine given.");
        return 1;
    }
    CbmPrgResult r;
    int rc = cbm_prg_place(g_c->io, data, n, p, is_p00, &r, msg, msglen);
    out->load = r.load;
    out->end = r.end;
    out->basic = r.basic;
    out->start = r.start;
    return rc;
}
static void prg_type_start(const CbmPrgMachine *m, const CbmPrgPlaced *r)
{
    CbmPrgParams p;
    if (r && prg_params(m, &p)) {
        CbmPrgResult res;
        res.load = r->load;
        res.end = r->end;
        res.basic = r->basic;
        res.start = r->start;
        cbm_prg_type_start(g_c->io, p, res);
    }
}
static int prg_script_check(const char *text, char *msg, int msglen)
{
    REASON(msg, msglen);
    msg[0] = 0;
    return cbm_prg_script_check(text ? text : "", msg, msglen);
}
static int prg_script_step(const CbmPrgMachine *m, const char *script, int *pos)
{
    CbmPrgParams p;
    return script && pos && prg_params(m, &p) ? cbm_prg_script_step(g_c->io, p, script, pos) : 0;
}
static uint16_t prg_relink(uint8_t *prog, uint32_t n, uint16_t from, uint16_t to) { return cbm_prg_relink(prog, n, from, to); }
static bool prg_key_for(uint8_t petscii, uint8_t *idx, bool *shift)
{
    CbmKey k;
    if (!idx || !shift || !cbm_key_for(petscii, &k)) {
        return false;
    }
    *idx = k.idx;
    *shift = k.shift;
    return true;
}
static void prg_mem_write(const CbmPrgMachine *m, uint16_t addr, const void *data, uint32_t n)
{
    CbmPrgParams p;
    if (data && prg_params(m, &p)) {
        cbm_prg_mem_write(g_c->io, p, addr, data, n);
    }
}
static void prg_mem_read(const CbmPrgMachine *m, uint16_t addr, void *data, uint32_t n)
{
    CbmPrgParams p;
    if (!data) {
        return;
    }
    if (!prg_params(m, &p)) {
        memset(data, 0, n);
        return;
    }
    cbm_prg_mem_read(g_c->io, p, addr, data, n);
}

static const CbmPrgApi g_prg = {
    sizeof(CbmPrgApi), prg_place, prg_type_start, prg_script_check, prg_script_step, prg_relink, prg_key_for, prg_mem_write, prg_mem_read,
};

void cbmprg_bind(CoreCtx *c, LibLoader &ld)
{
    g_c = c;
    ld.add_export(CBMPRG_API_NAME, CBMPRG_API_MAJOR, &g_prg);
}
