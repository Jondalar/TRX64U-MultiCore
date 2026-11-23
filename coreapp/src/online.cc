// The Assembly64 / CommoServe search (online.h). The query is built the way assembly_search.cc's send_query() builds it: (field:"value") joined by &, drop-down fields unquoted by their
// aqlKey, a rating as >=; the library field selects the client id ("Ultimate" is Assembly64, "Commodore" is CommoServe).
#include "online.h"
#include "online_api.h"
#include "ops_glue.h"
#include "a64_assembly.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_PRESETS 12
#define MAX_VALUES 48
#define MAX_RES 200
#define MAX_ENT 64

namespace {

struct Preset {
    char type[20];
    int n;
    char key[MAX_VALUES][32];
    char name[MAX_VALUES][28];
    int sel;                                   // -1 any
};

struct Res {
    char label[56];
    char id[16];
    int cat;
};

struct Ent {
    char name[56];
    int idx;
    uint32_t size;
};

char q_name[48], q_group[48], q_handle[48], q_event[48];
int q_lib;                                      // 0 Assembly64, 1 CommoServe
Preset pres[MAX_PRESETS];
int npres;
bool pres_loaded;
Res res[MAX_RES];
int nres;
int ent_for = -1;
Ent ent[MAX_ENT];
int nent;
uint8_t *dl_b;
uint32_t dl_len;
int dl_res = -1, dl_idx = -1;
char err[96];

const char *lib_id(void) { return q_lib ? "Commodore" : "Ultimate"; }

void clean(char *dst, int n, const char *src)
{
    int j = 0;
    for (; *src && j < n - 1; src++) {
        unsigned char ch = (unsigned char)*src;
        dst[j++] = (ch == '/' || ch == '\\' || ch < 32) ? '-' : (char)ch;
    }
    dst[j] = 0;
}

const char *jstr(JSON_Object *o, const char *key)
{
    JSON *j = o->get(key);
    return j && j->type() == eString ? ((JSON_String *)j)->get_string() : "";
}

int jint(JSON_Object *o, const char *key, int def)
{
    JSON *j = o->get(key);
    return j && j->type() == eInteger ? ((JSON_Integer *)j)->get_value() : def;
}

bool load_presets(void)
{
    if (pres_loaded) {
        return true;
    }
    assembly.set_client_id(lib_id());
    JSON *p = assembly.get_presets();
    if (!p || p->type() != eList) {
        snprintf(err, sizeof(err), "No answer from the server (network?)");
        return false;
    }
    JSON_List *l = (JSON_List *)p;
    npres = 0;
    for (int i = 0; i < l->get_num_elements() && npres < MAX_PRESETS; i++) {
        JSON *e = (*l)[i];
        if (!e || e->type() != eObject) {
            continue;
        }
        JSON_Object *o = (JSON_Object *)e;
        JSON *v = o->get("values");
        const char *type = jstr(o, "type");
        if (!type[0] || !v || v->type() != eList) {
            continue;
        }
        Preset &pr = pres[npres++];
        memset(&pr, 0, sizeof(pr));
        snprintf(pr.type, sizeof(pr.type), "%s", type);
        pr.sel = -1;
        JSON_List *vl = (JSON_List *)v;
        for (int k = 0; k < vl->get_num_elements() && pr.n < MAX_VALUES; k++) {
            JSON *ve = (*vl)[k];
            if (!ve || ve->type() != eObject) {
                continue;
            }
            snprintf(pr.key[pr.n], sizeof(pr.key[0]), "%s", jstr((JSON_Object *)ve, "aqlKey"));
            snprintf(pr.name[pr.n], sizeof(pr.name[0]), "%s", jstr((JSON_Object *)ve, "name"));
            pr.n++;
        }
    }
    pres_loaded = npres > 0;
    return pres_loaded;
}

void add_term(char *q, int qn, const char *field, const char *value, bool drop)
{
    int l = (int)strlen(q);
    snprintf(q + l, qn - l, "%s(%s:%s%s%s%s)", l ? " & " : "", field, drop ? "" : "\"", (drop && !strcasecmp(field, "rating") && value[0] != '>') ? ">=" : "", value, drop ? "" : "\"");
}

void drop_entries(void)
{
    ent_for = -1;
    nent = 0;
}

void drop_download(void)
{
    free(dl_b);
    dl_b = 0;
    dl_len = 0;
    dl_res = dl_idx = -1;
}

// ---- operations ---------------------------------------------------------------------------------------------------------------------------------------------
int op_set(CoreCtx *, const OpArgs &a, OpOut &o)
{
    const char *f = a.s(0);
    const char *t = a.s(1);
    while (*t == ' ') {
        t++;
    }
    char *dst = !strcmp(f, "name") ? q_name : !strcmp(f, "group") ? q_group : !strcmp(f, "handle") ? q_handle : !strcmp(f, "event") ? q_event : 0;
    if (!dst) {
        return OPE_ARG;
    }
    snprintf(dst, 48, "%s", t);
    for (char *p = dst + strlen(dst); p > dst && p[-1] == ' '; ) {
        *--p = 0;
    }
    snprintf(o.msg, o.msglen, "%s: %s", f, dst[0] ? dst : "any");
    return OPE_OK;
}

int op_library(CoreCtx *, const OpArgs &, OpOut &o)
{
    q_lib = !q_lib;
    snprintf(o.msg, o.msglen, "%s is searched", q_lib ? "CommoServe" : "Assembly64");
    return OPE_OK;
}

int op_presets(CoreCtx *, const OpArgs &, OpOut &o)
{
    pres_loaded = false;
    if (!load_presets()) {
        snprintf(o.msg, o.msglen, "%s", err);
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "%d fields to choose from", npres);
    return OPE_OK;
}

int op_cycle(CoreCtx *, const OpArgs &a, OpOut &o)
{
    int s = a.i(0);
    if (s < 0 || s >= npres) {
        return OPE_ARG;
    }
    Preset &p = pres[s];
    p.sel = p.sel + 1 >= p.n ? -1 : p.sel + 1;
    snprintf(o.msg, o.msglen, "%s: %s", p.type, p.sel < 0 ? "Any" : p.name[p.sel]);
    return OPE_OK;
}

int op_clear(CoreCtx *, const OpArgs &, OpOut &o)
{
    q_name[0] = q_group[0] = q_handle[0] = q_event[0] = 0;
    for (int i = 0; i < npres; i++) {
        pres[i].sel = -1;
    }
    snprintf(o.msg, o.msglen, "The form is empty");
    return OPE_OK;
}

int op_search(CoreCtx *c, const OpArgs &, OpOut &o)
{
    char q[400];
    q[0] = 0;
    if (q_name[0]) add_term(q, sizeof(q), "name", q_name, false);
    if (q_group[0]) add_term(q, sizeof(q), "group", q_group, false);
    if (q_handle[0]) add_term(q, sizeof(q), "handle", q_handle, false);
    if (q_event[0]) add_term(q, sizeof(q), "event", q_event, false);
    for (int i = 0; i < npres; i++) {
        if (pres[i].sel >= 0 && pres[i].key[pres[i].sel][0]) {
            add_term(q, sizeof(q), pres[i].type, pres[i].key[pres[i].sel], true);
        }
    }
    if (!q[0]) {
        snprintf(o.msg, o.msglen, "Queries cannot be empty: fill in a field");
        return OPE_FAILED;
    }
    assembly.set_client_id(lib_id());
    JSON *r = assembly.send_query(q);
    if (!r) {
        snprintf(o.msg, o.msglen, "Connection failed: no answer from the server (network?)");
        return OPE_FAILED;
    }
    drop_entries();
    drop_download();
    nres = 0;
    if (r->type() == eList) {
        JSON_List *l = (JSON_List *)r;
        for (int i = 0; i < l->get_num_elements() && nres < MAX_RES; i++) {
            JSON *e = (*l)[i];
            if (!e || e->type() != eObject) {
                continue;
            }
            JSON_Object *ob = (JSON_Object *)e;
            char sum[200];
            int year = jint(ob, "year", 0);
            const char *grp = jstr(ob, "group");
            snprintf(sum, sizeof(sum), "%s", jstr(ob, "name"));
            if (grp[0]) {
                int l2 = (int)strlen(sum);
                if (year) {
                    snprintf(sum + l2, sizeof(sum) - l2, " (%s, %d)", grp, year);
                } else {
                    snprintf(sum + l2, sizeof(sum) - l2, " (%s)", grp);
                }
            }
            Res &x = res[nres];
            clean(x.label, sizeof(x.label), sum);
            for (int k = 0; k < nres; k++) {                       // a folder name is unique
                if (!strcasecmp(res[k].label, x.label)) {
                    int ll = (int)strlen(x.label);
                    snprintf(x.label + (ll > 48 ? 48 : ll), 8, " #%d", i + 1);
                    break;
                }
            }
            snprintf(x.id, sizeof(x.id), "%s", jstr(ob, "id"));
            x.cat = jint(ob, "category", 0);
            nres++;
        }
    }
    delete r;
    if (!nres) {
        snprintf(o.msg, o.msglen, "Nothing found");
        return OPE_FAILED;
    }
    snprintf(c->lib.want_dir, sizeof(c->lib.want_dir), "/Online");
    snprintf(o.msg, o.msglen, "%d found", nres);
    return OPE_OK;
}

// ---- states -----------------------------------------------------------------------------------------------------------------------------------------------------
const char *st_library(CoreCtx *) { return q_lib ? "CommoServe" : "Assembly64"; }
const char *any(const char *s) { return s[0] ? s : "Any"; }
const char *st_name(CoreCtx *) { return any(q_name); }
const char *st_group(CoreCtx *) { return any(q_group); }
const char *st_handle(CoreCtx *) { return any(q_handle); }
const char *st_event(CoreCtx *) { return any(q_event); }
const char *st_vname(CoreCtx *) { return q_name[0] ? q_name : " "; }
const char *st_vgroup(CoreCtx *) { return q_group[0] ? q_group : " "; }
const char *st_vhandle(CoreCtx *) { return q_handle[0] ? q_handle : " "; }
const char *st_vevent(CoreCtx *) { return q_event[0] ? q_event : " "; }
const char *st_loaded(CoreCtx *) { return pres_loaded ? "yes" : 0; }
const char *st_unloaded(CoreCtx *) { return pres_loaded ? 0 : "yes"; }
const char *st_results(CoreCtx *)
{
    static char b[40];
    if (!nres) {
        return "None yet";
    }
    snprintf(b, sizeof(b), "%d found", nres);
    return b;
}

template <int N> const char *st_ptitle(CoreCtx *)
{
    static char t[24];
    if (N >= npres) {
        return "";
    }
    snprintf(t, sizeof(t), "%s", pres[N].type);
    if (t[0] >= 'a' && t[0] <= 'z') {
        t[0] = (char)(t[0] - 32);
    }
    return t;
}
template <int N> const char *st_pvalue(CoreCtx *) { return N < npres ? (pres[N].sel < 0 ? "Any" : pres[N].name[pres[N].sel]) : ""; }
template <int N> const char *st_phide(CoreCtx *) { return N < npres ? 0 : "yes"; }

}   // namespace

#define PRESET_STATES(N) \
    r.add_state("online.p" #N ".title", st_ptitle<N>); \
    r.add_state("online.p" #N ".value", st_pvalue<N>); \
    r.add_state("online.p" #N ".hide", st_phide<N>);

void online_register(OpRegistry &r)
{
    static const ArgSpec field_args[] = { ARG_STRING("field", 12), ARG_STRING("text", 47) };
    static const ArgSpec slot_arg[] = { ARG_INT("slot", 0, MAX_PRESETS - 1) };
    r.contributor("online");
    r.add_op("online.set", field_args, 2, op_set);
    r.add_op("online.library", 0, 0, op_library);
    r.add_op("online.presets", 0, 0, op_presets);
    r.add_op("online.cycle", slot_arg, 1, op_cycle);
    r.add_op("online.clear", 0, 0, op_clear);
    r.add_op("online.search", 0, 0, op_search);
    r.add_state("online.library", st_library);
    r.add_state("online.name", st_name);
    r.add_state("online.group", st_group);
    r.add_state("online.handle", st_handle);
    r.add_state("online.event", st_event);
    r.add_state("online.v.name", st_vname);
    r.add_state("online.v.group", st_vgroup);
    r.add_state("online.v.handle", st_vhandle);
    r.add_state("online.v.event", st_vevent);
    r.add_state("online.loaded", st_loaded);
    r.add_state("online.unloaded", st_unloaded);
    r.add_state("online.results", st_results);
    PRESET_STATES(0) PRESET_STATES(1) PRESET_STATES(2) PRESET_STATES(3) PRESET_STATES(4) PRESET_STATES(5)
    PRESET_STATES(6) PRESET_STATES(7) PRESET_STATES(8) PRESET_STATES(9) PRESET_STATES(10) PRESET_STATES(11)
}

// ---- the folder /Online -----------------------------------------------------------------------------------------------------------------------------------
bool online_has_results(void) { return nres > 0; }
const char *online_error(void) { return err; }

static int find_res(const char *label)
{
    for (int i = 0; i < nres; i++) {
        if (!strcasecmp(res[i].label, label)) {
            return i;
        }
    }
    return -1;
}

static bool load_entries(int ri)
{
    if (ent_for == ri) {
        return true;
    }
    assembly.set_client_id(lib_id());
    JSON *j = assembly.request_entries(res[ri].id, res[ri].cat);
    if (!j) {
        snprintf(err, sizeof(err), "No answer from the server (network?)");
        return false;
    }
    nent = 0;
    if (j->type() == eObject) {
        JSON *ce = ((JSON_Object *)j)->get("contentEntry");
        if (ce && ce->type() == eList) {
            JSON_List *l = (JSON_List *)ce;
            for (int i = 0; i < l->get_num_elements() && nent < MAX_ENT; i++) {
                JSON *e = (*l)[i];
                if (!e || e->type() != eObject) {
                    continue;
                }
                JSON_Object *o = (JSON_Object *)e;
                const char *path = jstr(o, "path");
                const char *base = strrchr(path, '/');
                clean(ent[nent].name, sizeof(ent[nent].name), base ? base + 1 : path);
                ent[nent].idx = jint(o, "id", i);
                ent[nent].size = (uint32_t)jint(o, "size", 0);
                if (ent[nent].name[0]) {
                    nent++;
                }
            }
        }
    }
    delete j;
    ent_for = ri;
    return true;
}

int online_list(const char *rel, FsEntry *out, int max)
{
    while (*rel == '/') {
        rel++;
    }
    int n = 0;
    if (!*rel) {
        for (int i = 0; i < nres && n < max; i++) {
            memset(&out[n], 0, sizeof(out[n]));
            snprintf(out[n].name, sizeof(out[n].name), "%s", res[i].label);
            out[n].dir = true;
            n++;
        }
        return n;
    }
    int ri = find_res(rel);
    if (ri < 0 || !load_entries(ri)) {
        return -1;
    }
    for (int i = 0; i < nent && n < max; i++) {
        memset(&out[n], 0, sizeof(out[n]));
        snprintf(out[n].name, sizeof(out[n].name), "%s", ent[i].name);
        out[n].size = ent[i].size ? ent[i].size : 0;
        n++;
    }
    return n;
}

bool online_stat(const char *rel, uint64_t *size, bool *dir)
{
    while (*rel == '/') {
        rel++;
    }
    *size = 0;
    *dir = true;
    if (!*rel) {
        return true;
    }
    const char *sl = strchr(rel, '/');
    char lab[64];
    {
        size_t ll = sl ? (size_t)(sl - rel) : strlen(rel);
        ll = ll < sizeof(lab) - 1 ? ll : sizeof(lab) - 1;
        memcpy(lab, rel, ll);
        lab[ll] = 0;
    }
    int ri = find_res(lab);
    if (ri < 0) {
        return false;
    }
    if (!sl || !sl[1]) {
        return true;
    }
    if (!load_entries(ri)) {
        return false;
    }
    for (int i = 0; i < nent; i++) {
        if (!strcasecmp(ent[i].name, sl + 1)) {
            *dir = false;
            *size = ent[i].size;
            return true;
        }
    }
    return false;
}

uint8_t *online_fetch(const char *rel, uint32_t *len)
{
    while (*rel == '/') {
        rel++;
    }
    const char *sl = strchr(rel, '/');
    if (!sl) {
        return 0;
    }
    char lab[64];
    {
        size_t ll = (size_t)(sl - rel) < sizeof(lab) - 1 ? (size_t)(sl - rel) : sizeof(lab) - 1;
        memcpy(lab, rel, ll);
        lab[ll] = 0;
    }
    int ri = find_res(lab);
    if (ri < 0 || !load_entries(ri)) {
        return 0;
    }
    int ei = -1;
    for (int i = 0; i < nent; i++) {
        if (!strcasecmp(ent[i].name, sl + 1)) {
            ei = i;
        }
    }
    if (ei < 0) {
        return 0;
    }
    if (dl_b && dl_res == ri && dl_idx == ent[ei].idx) {
        uint8_t *c = (uint8_t *)malloc(dl_len ? dl_len : 1);
        if (c) {
            memcpy(c, dl_b, dl_len);
            *len = dl_len;
        }
        return c;
    }
    drop_download();
    assembly.set_client_id(lib_id());
    assembly.request_binary(res[ri].id, res[ri].cat, ent[ei].idx);
    a64_grow_t *g = assembly.take_download();
    if (!g || g->failed || !g->b) {
        snprintf(err, sizeof(err), g && g->failed ? "The file is too big for RAM" : "The download failed (network?)");
        if (g) {
            free(g->b);
            free(g);
        }
        return 0;
    }
    dl_b = g->b;
    dl_len = g->len;
    dl_res = ri;
    dl_idx = ent[ei].idx;
    free(g);
    uint8_t *c = (uint8_t *)malloc(dl_len ? dl_len : 1);
    if (c) {
        memcpy(c, dl_b, dl_len);
        *len = dl_len;
    }
    return c;
}

// The table the file manager reads the folder "/Online" through (online_api.h), given to the loader when the library is bound (libtab.h).
static const OnlineApi g_online = { sizeof(OnlineApi), online_has_results, online_list, online_stat, online_fetch, online_error };

void online_bind(CoreCtx *, LibLoader &ld)
{
    ld.add_export(ONLINE_API_NAME, ONLINE_API_MAJOR, &g_online);
}
