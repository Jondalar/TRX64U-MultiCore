// The Assembly64 / CommoServe search as a sub-app (online_api.h gives the folder "/Online" to the file manager, the menu record app.toml gives the form). The query is built the way
// assembly_search.cc's send_query() builds it: (field:"value") joined by &, drop-down fields unquoted by their aqlKey, a rating as >=; the library field selects the client id
// ("Ultimate" is Assembly64, "Commodore" is CommoServe). The state of the search (the form, the choices, the results, the last download) is in this file's own variables: the
// sub-app is the only place that has it. The client is online_http.cc and online_json.cc over api->net (T239.3 D12, P3-1: online search has no global object in the base).
#include "online_impl.h"
#include "online_http.h"
#include "online_json.h"
#include "otext.h"

#define MAX_VALUES 48
#define MAX_RES 200
#define MAX_ENT 64

#define URL_SEARCH   "/leet/search/aql?query="
#define URL_PATTERNS "/leet/search/aql/presets"
#define URL_ENTRIES  "/leet/search/entries"
#define URL_DOWNLOAD "/leet/search/bin"

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

const LibApi *g_api;
char q_name[48], q_group[48], q_handle[48], q_event[48];
int q_lib;                                      // 0 Assembly64, 1 CommoServe
Preset pres[ONLINE_PRESETS];
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

HttpEnv env(void)
{
    HttpEnv e = { g_api ? g_api->net : 0, g_api ? g_api->mem : 0, g_api ? g_api->sys : 0 };
    return e;
}

void *tree_alloc(uint32_t n, void *)
{
    return g_api && g_api->mem ? g_api->mem->malloc(n) : 0;
}

void clean(char *dst, int n, const char *src)
{
    int j = 0;
    for (; *src && j < n - 1; src++) {
        unsigned char ch = (unsigned char)*src;
        dst[j++] = (ch == '/' || ch == '\\' || ch < 32) ? '-' : (char)ch;
    }
    dst[j] = 0;
}

// what the person reads when a request failed
void say_failed(int rc, const HttpOut &o)
{
    err[0] = 0;
    if (rc == HTTP_STATUS) {
        ot_copy(err, sizeof(err), "The server answered ");
        ot_cat_int(err, sizeof(err), o.status);
    } else if (rc == HTTP_TOO_BIG) {
        ot_copy(err, sizeof(err), "The answer is too big for RAM");
    } else if (rc == HTTP_NO_ROOM) {
        ot_copy(err, sizeof(err), "No memory for the answer");
    } else {
        ot_copy(err, sizeof(err), "No answer from the server (network?)");
    }
}

// URL encoding as the firmware's url_encode does it: letters, digits and _-.* stay, every other byte is %xx
void url_cat(char *dst, int n, const char *src)
{
    static const char hex[] = "0123456789abcdef";
    for (; *src; src++) {
        unsigned char c = (unsigned char)*src;
        char one[4];
        if (c == '_' || c == '-' || c == '.' || c == '*' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            one[0] = (char)c;
            one[1] = 0;
        } else {
            one[0] = '%';
            one[1] = hex[c >> 4];
            one[2] = hex[c & 15];
            one[3] = 0;
        }
        ot_cat(dst, n, one);
    }
}

// One request whose answer is JSON: the tree is in `t` (the text is `*text`, the nodes `t->pool`); both are freed with json_free. False: say_failed has said why, or the answer is no JSON.
struct JReply {
    uint8_t *text;
    JTree t;
};

bool json_get(const char *path, JReply *r)
{
    r->text = 0;
    r->t.root = 0;
    r->t.pool = 0;
    HttpOut o;
    int rc = http_get(env(), ONLINE_HOST, path, lib_id(), ONLINE_JSON_MAX, &o);
    if (rc != HTTP_OK) {
        say_failed(rc, o);
        return false;
    }
    r->text = o.body;
    r->t = json_parse((char *)o.body, (int)o.len, tree_alloc, 0);
    if (!r->t.root) {
        ot_copy(err, sizeof(err), "The answer of the server is not readable");
        g_api->mem->free(r->text);
        r->text = 0;
        return false;
    }
    return true;
}

void json_free(JReply *r)
{
    if (r->t.pool) {
        g_api->mem->free(r->t.pool);
    }
    if (r->text) {
        g_api->mem->free(r->text);
    }
    r->t.pool = 0;
    r->text = 0;
    r->t.root = 0;
}

bool load_presets(void)
{
    if (pres_loaded) {
        return true;
    }
    JReply jr;
    if (!json_get(URL_PATTERNS, &jr)) {                // the presets are always taken from Assembly64, whichever library is selected
        return false;
    }
    bool listed = jr.t.root->type == J_LIST;
    npres = 0;
    for (const JNode *e = listed ? jr.t.root->child : 0; e && npres < ONLINE_PRESETS; e = e->next) {
        const JNode *v = json_member(e, "values");
        const char *type = json_text(e, "type");
        if (e->type != J_OBJ || !type[0] || !v || v->type != J_LIST) {
            continue;
        }
        Preset &pr = pres[npres++];
        memset(&pr, 0, sizeof(pr));
        ot_copy(pr.type, sizeof(pr.type), type);
        pr.sel = -1;
        for (const JNode *ve = v->child; ve && pr.n < MAX_VALUES; ve = ve->next) {
            if (ve->type != J_OBJ) {
                continue;
            }
            ot_copy(pr.key[pr.n], sizeof(pr.key[0]), json_text(ve, "aqlKey"));
            ot_copy(pr.name[pr.n], sizeof(pr.name[0]), json_text(ve, "name"));
            pr.n++;
        }
    }
    json_free(&jr);
    pres_loaded = npres > 0;
    if (!pres_loaded) {
        ot_copy(err, sizeof(err), "The server gave no choices");
    }
    return pres_loaded;
}

void add_term(char *q, int qn, const char *field, const char *value, bool drop)
{
    if (q[0]) {
        ot_cat(q, qn, " & ");
    }
    ot_cat(q, qn, "(");
    ot_cat(q, qn, field);
    ot_cat(q, qn, ":");
    if (!drop) {
        ot_cat(q, qn, "\"");
    } else if (ot_ieq(field, "rating") && value[0] != '>') {
        ot_cat(q, qn, ">=");
    }
    ot_cat(q, qn, value);
    if (!drop) {
        ot_cat(q, qn, "\"");
    }
    ot_cat(q, qn, ")");
}

void drop_entries(void)
{
    ent_for = -1;
    nent = 0;
}

void drop_download(void)
{
    if (dl_b && g_api) {
        g_api->mem->free(dl_b);
    }
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
    ot_copy(dst, 48, t);
    for (char *p = dst + strlen(dst); p > dst && p[-1] == ' '; ) {
        *--p = 0;
    }
    o.msg[0] = 0;
    ot_cat(o.msg, o.msglen, f);
    ot_cat(o.msg, o.msglen, ": ");
    ot_cat(o.msg, o.msglen, dst[0] ? dst : "any");
    return OPE_OK;
}

int op_library(CoreCtx *, const OpArgs &, OpOut &o)
{
    q_lib = !q_lib;
    ot_copy(o.msg, o.msglen, q_lib ? "CommoServe is searched" : "Assembly64 is searched");
    return OPE_OK;
}

int op_presets(CoreCtx *, const OpArgs &, OpOut &o)
{
    pres_loaded = false;
    if (!load_presets()) {
        ot_copy(o.msg, o.msglen, err);
        return OPE_FAILED;
    }
    o.msg[0] = 0;
    ot_cat_int(o.msg, o.msglen, npres);
    ot_cat(o.msg, o.msglen, " fields to choose from");
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
    ot_copy(o.msg, o.msglen, p.type);
    ot_cat(o.msg, o.msglen, ": ");
    ot_cat(o.msg, o.msglen, p.sel < 0 ? "Any" : p.name[p.sel]);
    return OPE_OK;
}

int op_clear(CoreCtx *, const OpArgs &, OpOut &o)
{
    q_name[0] = q_group[0] = q_handle[0] = q_event[0] = 0;
    for (int i = 0; i < npres; i++) {
        pres[i].sel = -1;
    }
    ot_copy(o.msg, o.msglen, "The form is empty");
    return OPE_OK;
}

int op_search(CoreCtx *, const OpArgs &, OpOut &o)
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
        ot_copy(o.msg, o.msglen, "Queries cannot be empty: fill in a field");
        return OPE_FAILED;
    }
    char path[1024];
    path[0] = 0;
    ot_cat(path, sizeof(path), URL_SEARCH);
    url_cat(path, sizeof(path), q);
    JReply jr;
    if (!json_get(path, &jr)) {
        ot_copy(o.msg, o.msglen, "Connection failed: ");
        ot_cat(o.msg, o.msglen, err);
        return OPE_FAILED;
    }
    drop_entries();
    drop_download();
    nres = 0;
    int i = 0;
    for (const JNode *e = jr.t.root->type == J_LIST ? jr.t.root->child : 0; e && nres < MAX_RES; e = e->next, i++) {
        if (e->type != J_OBJ) {
            continue;
        }
        char sum[200];
        int year = json_int(e, "year", 0);
        const char *grp = json_text(e, "group");
        ot_copy(sum, sizeof(sum), json_text(e, "name"));
        if (grp[0]) {
            ot_cat(sum, sizeof(sum), " (");
            ot_cat(sum, sizeof(sum), grp);
            if (year) {
                ot_cat(sum, sizeof(sum), ", ");
                ot_cat_int(sum, sizeof(sum), year);
            }
            ot_cat(sum, sizeof(sum), ")");
        }
        Res &x = res[nres];
        clean(x.label, sizeof(x.label), sum);
        for (int k = 0; k < nres; k++) {                       // a folder name is unique
            if (ot_ieq(res[k].label, x.label)) {
                int ll = ot_len(x.label);
                x.label[ll > 48 ? 48 : ll] = 0;
                ot_cat(x.label, sizeof(x.label), " #");
                ot_cat_int(x.label, sizeof(x.label), i + 1);
                break;
            }
        }
        ot_copy(x.id, sizeof(x.id), json_text(e, "id"));
        x.cat = json_int(e, "category", 0);
        nres++;
    }
    json_free(&jr);
    if (!nres) {
        ot_copy(o.msg, o.msglen, "Nothing found");
        return OPE_FAILED;
    }
    // the Library opens on "/Online": lib.show takes the folder of the file a state names (online.folder), a base operation (libfiles.cc)
    if (g_api && g_api->ops && g_api->ops->call) {
        OpIn in = in_str("state", "online.folder");
        OpResult rr;
        g_api->ops->call("lib.show", &in, 1, &rr);
    }
    o.msg[0] = 0;
    ot_cat_int(o.msg, o.msglen, nres);
    ot_cat(o.msg, o.msglen, " found");
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
const char *st_folder(CoreCtx *) { return "/Online/results"; }          // lib.show opens the folder of this path: "/Online"
const char *st_results(CoreCtx *)
{
    static char b[40];
    if (!nres) {
        return "None yet";
    }
    b[0] = 0;
    ot_cat_int(b, sizeof(b), nres);
    ot_cat(b, sizeof(b), " found");
    return b;
}

template <int N> const char *st_ptitle(CoreCtx *)
{
    static char t[24];
    if (N >= npres) {
        return "";
    }
    ot_copy(t, sizeof(t), pres[N].type);
    if (t[0] >= 'a' && t[0] <= 'z') {
        t[0] = (char)(t[0] - 32);
    }
    return t;
}
template <int N> const char *st_pvalue(CoreCtx *) { return N < npres ? (pres[N].sel < 0 ? "Any" : pres[N].name[pres[N].sel]) : ""; }
template <int N> const char *st_phide(CoreCtx *) { return N < npres ? 0 : "yes"; }

// ---- the folder /Online -----------------------------------------------------------------------------------------------------------------------------------
bool tab_has_results(void) { return nres > 0; }
const char *tab_error(void) { return err; }

int find_res(const char *label)
{
    for (int i = 0; i < nres; i++) {
        if (ot_ieq(res[i].label, label)) {
            return i;
        }
    }
    return -1;
}

bool load_entries(int ri)
{
    if (ent_for == ri) {
        return true;
    }
    char path[160];
    path[0] = 0;
    ot_cat(path, sizeof(path), URL_ENTRIES "/");
    url_cat(path, sizeof(path), res[ri].id);
    ot_cat(path, sizeof(path), "/");
    ot_cat_int(path, sizeof(path), res[ri].cat);
    JReply jr;
    if (!json_get(path, &jr)) {
        return false;
    }
    nent = 0;
    const JNode *ce = json_member(jr.t.root, "contentEntry");
    int i = 0;
    for (const JNode *e = ce && ce->type == J_LIST ? ce->child : 0; e && nent < MAX_ENT; e = e->next, i++) {
        if (e->type != J_OBJ) {
            continue;
        }
        const char *p = json_text(e, "path");
        const char *base = strrchr(p, '/');
        clean(ent[nent].name, sizeof(ent[nent].name), base ? base + 1 : p);
        ent[nent].idx = json_int(e, "id", i);
        ent[nent].size = (uint32_t)json_int(e, "size", 0);
        if (ent[nent].name[0]) {
            nent++;
        }
    }
    json_free(&jr);
    ent_for = ri;
    return true;
}

int tab_list(const char *rel, FsEntry *out, int max)
{
    while (*rel == '/') {
        rel++;
    }
    int n = 0;
    if (!*rel) {
        for (int i = 0; i < nres && n < max; i++) {
            memset(&out[n], 0, sizeof(out[n]));
            ot_copy(out[n].name, sizeof(out[n].name), res[i].label);
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
        ot_copy(out[n].name, sizeof(out[n].name), ent[i].name);
        out[n].size = ent[i].size;
        n++;
    }
    return n;
}

bool tab_stat(const char *rel, uint64_t *size, bool *dir)
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
        if (ot_ieq(ent[i].name, sl + 1)) {
            *dir = false;
            *size = ent[i].size;
            return true;
        }
    }
    return false;
}

// The bytes of a file of a result in a block of the caller's allocator (the base frees it with its own free: a block of this sub-app's account is no block of the base's).
uint8_t *tab_fetch(const char *rel, uint8_t *(*alloc)(uint32_t n), uint32_t *len)
{
    while (*rel == '/') {
        rel++;
    }
    const char *sl = strchr(rel, '/');
    if (!sl || !alloc) {
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
        if (ot_ieq(ent[i].name, sl + 1)) {
            ei = i;
        }
    }
    if (ei < 0) {
        return 0;
    }
    if (!(dl_b && dl_res == ri && dl_idx == ent[ei].idx)) {
        drop_download();
        char path[160];
        path[0] = 0;
        ot_cat(path, sizeof(path), URL_DOWNLOAD "/");
        url_cat(path, sizeof(path), res[ri].id);
        ot_cat(path, sizeof(path), "/");
        ot_cat_int(path, sizeof(path), res[ri].cat);
        ot_cat(path, sizeof(path), "/");
        ot_cat_int(path, sizeof(path), ent[ei].idx);
        HttpOut o;
        int rc = http_get(env(), ONLINE_HOST, path, lib_id(), ONLINE_DOWNLOAD_MAX, &o);
        if (rc != HTTP_OK) {
            say_failed(rc, o);
            if (rc == HTTP_BROKEN || rc == HTTP_NO_NET) {
                ot_copy(err, sizeof(err), "The download failed (network?)");
            }
            return 0;
        }
        dl_b = o.body;
        dl_len = o.len;
        dl_res = ri;
        dl_idx = ent[ei].idx;
    }
    uint8_t *c = alloc(dl_len ? dl_len : 1);
    if (c) {
        memcpy(c, dl_b, dl_len);
        *len = dl_len;
    }
    return c;
}

}   // namespace

#define PRESET_STATES(N) \
    { "online.p" #N ".title", st_ptitle<N> }, \
    { "online.p" #N ".value", st_pvalue<N> }, \
    { "online.p" #N ".hide", st_phide<N> },

static const ArgSpec field_args[] = { ARG_STRING("field", 12), ARG_STRING("text", 47) };
static const ArgSpec slot_arg[] = { ARG_INT("slot", 0, ONLINE_PRESETS - 1) };

const LibOpDecl online_ops[] = {
    { "online.set", field_args, 2, op_set },
    { "online.library", 0, 0, op_library },
    { "online.presets", 0, 0, op_presets },
    { "online.cycle", slot_arg, 1, op_cycle },
    { "online.clear", 0, 0, op_clear },
    { "online.search", 0, 0, op_search },
};
const int online_nops = (int)(sizeof(online_ops) / sizeof(online_ops[0]));

const LibStateDecl online_states[] = {
    { "online.library", st_library },
    { "online.name", st_name },
    { "online.group", st_group },
    { "online.handle", st_handle },
    { "online.event", st_event },
    { "online.v.name", st_vname },
    { "online.v.group", st_vgroup },
    { "online.v.handle", st_vhandle },
    { "online.v.event", st_vevent },
    { "online.loaded", st_loaded },
    { "online.unloaded", st_unloaded },
    { "online.results", st_results },
    { "online.folder", st_folder },
    PRESET_STATES(0) PRESET_STATES(1) PRESET_STATES(2) PRESET_STATES(3) PRESET_STATES(4) PRESET_STATES(5)
    PRESET_STATES(6) PRESET_STATES(7) PRESET_STATES(8) PRESET_STATES(9) PRESET_STATES(10) PRESET_STATES(11)
};
const int online_nstates = (int)(sizeof(online_states) / sizeof(online_states[0]));

const OnlineApi online_table = { sizeof(OnlineApi), tab_has_results, tab_list, tab_stat, tab_fetch, tab_error };

void online_attach(const LibApi *api)
{
    g_api = api;
}
