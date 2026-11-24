// The Assembly64 / CommoServe search as a sub-app (online_api.h gives the folder "/Online" to the file manager, the menu record app.toml gives the form). The query is built the way
// assembly_search.cc's send_query() builds it: (field:"value") joined by &, drop-down fields unquoted by their aqlKey, a rating as >=; the library field selects the client id
// ("Ultimate" is Assembly64, "Commodore" is CommoServe). The state of the search (the form, the choices, the results, the last download) is in this file's own variables: the
// sub-app is the only place that has it. The client is online_http.cc and online_json.cc over api->net (T239.3 D12, P3-1: online search has no global object in the base).
// Every request to the server runs on a job (api->job->start): api.md 1.3.9 and 4.8.13.2 forbid api->net connect, send and recv on the menu task and the executor task. The form's operations
// (online.presets, online.search) are the node attribute `job`: the menu calls them again while they answer `more`. The folder "/Online" (list, stat, fetch of online_api.h) is called on the menu
// task; it answers from what is kept and starts a prefetch of the rest, which a later call finds ready. One job at a time (Work).
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
#define WORK_STACK 32768u                  // the stack of the job: the HTTP client, the JSON reader (J_DEPTH 12) and the parsers above

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
SvcMutex *g_mx;                                 // guards the hand-over of a finished job's result (collect)
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

enum { W_PRESETS = 1, W_SEARCH, W_ENTRIES, W_DOWNLOAD };

// The one request in flight. The job writes only here, and only while state is 1; the menu and executor tasks never read the result before state is 2, and take it over (collect) into the
// variables above. state: 0 idle, 1 the job runs, 2 the job has ended and its result is not taken yet.
struct Work {
    volatile int state;
    volatile bool abandon;                      // the person cancelled: the result is not wanted
    SvcJobH *jh;                                // the job's own handle, for api->job->cancelled
    int kind;
    int form;                                   // the form operation (W_PRESETS, W_SEARCH) that a stepping menu waits for, 0 none
    char path[1024];
    const char *cid;                            // the Client-Id: the library the request is for
    uint32_t max;
    int ri, ei;                                 // W_ENTRIES: the result; W_DOWNLOAD: the result and the server's index of the entry
    bool ok;
    char err[96];
    Preset pres[ONLINE_PRESETS];
    int npres;
    Res res[MAX_RES];
    int nres;
    Ent ent[MAX_ENT];
    int nent;
    uint8_t *body;
    uint32_t len;
};
Work g_w;

// What the last finished form request (W_PRESETS, W_SEARCH) came to, for the operation that waits for it.
struct Out {
    bool fresh;
    int kind;
    bool ok;
};
Out g_out;

// What a failed prefetch (W_ENTRIES, W_DOWNLOAD) said: told once to the next call that asks for the same thing, which then asks the server again.
struct PfErr {
    int kind, ri, ei;
    char text[96];
};
PfErr g_pf;

const char *lib_id(void) { return q_lib ? "Commodore" : "Ultimate"; }

// The job is asked to end: the person cancelled the request, or the base asks the job (api->job->cancelled: the sub-app is being unloaded)
bool work_cancelled(void *)
{
    return g_w.abandon || (g_api && g_api->job && g_api->job->cancelled && g_w.jh && g_api->job->cancelled(g_w.jh));
}

HttpEnv env(void)
{
    HttpEnv e = { g_api ? g_api->net : 0, g_api ? g_api->mem : 0, g_api ? g_api->sys : 0, work_cancelled, 0 };
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
void say_failed(char *dst, int n, int rc, const HttpOut &o)
{
    dst[0] = 0;
    if (rc == HTTP_STATUS) {
        ot_copy(dst, n, "The server answered ");
        ot_cat_int(dst, n, o.status);
    } else if (rc == HTTP_TOO_BIG) {
        ot_copy(dst, n, "The answer is too big for RAM");
    } else if (rc == HTTP_NO_ROOM) {
        ot_copy(dst, n, "No memory for the answer");
    } else {
        ot_copy(dst, n, "No answer from the server (network?)");
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

bool json_get(const char *path, const char *cid, JReply *r, char *why, int whyn)
{
    r->text = 0;
    r->t.root = 0;
    r->t.pool = 0;
    HttpOut o;
    int rc = http_get(env(), ONLINE_HOST, path, cid, ONLINE_JSON_MAX, &o);
    if (rc != HTTP_OK) {
        say_failed(why, whyn, rc, o);
        return false;
    }
    r->text = o.body;
    r->t = json_parse((char *)o.body, (int)o.len, tree_alloc, 0);
    if (!r->t.root) {
        ot_copy(why, whyn, "The answer of the server is not readable");
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

// The parsers fill the arrays they are given: the job's own (Work) while it runs, never the ones the menu reads.
bool parse_presets(const JReply &jr, Preset *out, int *n)
{
    bool listed = jr.t.root->type == J_LIST;
    *n = 0;
    for (const JNode *e = listed ? jr.t.root->child : 0; e && *n < ONLINE_PRESETS; e = e->next) {
        const JNode *v = json_member(e, "values");
        const char *type = json_text(e, "type");
        if (e->type != J_OBJ || !type[0] || !v || v->type != J_LIST) {
            continue;
        }
        Preset &pr = out[(*n)++];
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
    return *n > 0;
}

void parse_results(const JReply &jr, Res *out, int *n)
{
    *n = 0;
    int i = 0;
    for (const JNode *e = jr.t.root->type == J_LIST ? jr.t.root->child : 0; e && *n < MAX_RES; e = e->next, i++) {
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
        Res &x = out[*n];
        clean(x.label, sizeof(x.label), sum);
        for (int k = 0; k < *n; k++) {                       // a folder name is unique
            if (ot_ieq(out[k].label, x.label)) {
                int ll = ot_len(x.label);
                x.label[ll > 48 ? 48 : ll] = 0;
                ot_cat(x.label, sizeof(x.label), " #");
                ot_cat_int(x.label, sizeof(x.label), i + 1);
                break;
            }
        }
        ot_copy(x.id, sizeof(x.id), json_text(e, "id"));
        x.cat = json_int(e, "category", 0);
        (*n)++;
    }
}

void parse_entries(const JReply &jr, Ent *out, int *n)
{
    *n = 0;
    const JNode *ce = json_member(jr.t.root, "contentEntry");
    int i = 0;
    for (const JNode *e = ce && ce->type == J_LIST ? ce->child : 0; e && *n < MAX_ENT; e = e->next, i++) {
        if (e->type != J_OBJ) {
            continue;
        }
        const char *p = json_text(e, "path");
        const char *base = strrchr(p, '/');
        clean(out[*n].name, sizeof(out[*n].name), base ? base + 1 : p);
        out[*n].idx = json_int(e, "id", i);
        out[*n].size = (uint32_t)json_int(e, "size", 0);
        if (out[*n].name[0]) {
            (*n)++;
        }
    }
}

// ---- the job ------------------------------------------------------------------------------------------------------------------------------------------------
// The function of the job: the request of g_w, then the answer in g_w. The only task that connects, sends and receives.
int work_job(SvcJobH *j, void *)
{
    Work &w = g_w;
    w.jh = j;
    w.ok = false;
    w.err[0] = 0;
    w.body = 0;
    if (w.kind == W_DOWNLOAD) {
        HttpOut o;
        int rc = http_get(env(), ONLINE_HOST, w.path, w.cid, w.max, &o);
        if (rc == HTTP_OK) {
            w.body = o.body;
            w.len = o.len;
            w.ok = true;
        } else {
            say_failed(w.err, sizeof(w.err), rc, o);
            if (rc == HTTP_BROKEN || rc == HTTP_NO_NET) {
                ot_copy(w.err, sizeof(w.err), "The download failed (network?)");
            }
        }
    } else {
        JReply jr;
        if (json_get(w.path, w.cid, &jr, w.err, sizeof(w.err))) {
            if (w.kind == W_PRESETS) {
                w.ok = parse_presets(jr, w.pres, &w.npres);
                if (!w.ok) {
                    ot_copy(w.err, sizeof(w.err), "The server gave no choices");
                }
            } else if (w.kind == W_SEARCH) {
                parse_results(jr, w.res, &w.nres);
                w.ok = true;
            } else {
                parse_entries(jr, w.ent, &w.nent);
                w.ok = true;
            }
            json_free(&jr);
        }
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    w.state = 2;
    return 0;
}

void drop_entries(void);
void drop_download(void);

// Takes over the result of a job that has ended. Any task that looks at the state of the search calls it first; the mutex keeps two of them from taking the same result.
void collect(void)
{
    if (g_w.state != 2) {
        return;
    }
    const SvcSync *sy = g_api ? g_api->sync : 0;
    bool locked = g_mx && sy && sy->mutex_lock && sy->mutex_lock(g_mx, SYNC_FOREVER);
    if (g_w.state == 2) {
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        bool want = g_w.ok && !g_w.abandon;
        int k = g_w.kind;
        if (want && k == W_PRESETS) {
            memcpy(pres, g_w.pres, sizeof(pres));
            npres = g_w.npres;
            pres_loaded = true;
        } else if (want && k == W_SEARCH) {
            memcpy(res, g_w.res, sizeof(res));
            nres = g_w.nres;
            drop_entries();
            drop_download();
            g_pf.kind = 0;                                  // a failure told for an index of the old results is not the new results'
        } else if (want && k == W_ENTRIES) {
            memcpy(ent, g_w.ent, sizeof(ent));
            nent = g_w.nent;
            ent_for = g_w.ri;
        } else if (want && k == W_DOWNLOAD) {
            drop_download();
            dl_b = g_w.body;
            dl_len = g_w.len;
            dl_res = g_w.ri;
            dl_idx = g_w.ei;
            g_w.body = 0;
        }
        if (g_w.body && g_api && g_api->mem) {              // a download nobody wants
            g_api->mem->free(g_w.body);
            g_w.body = 0;
        }
        if (!g_w.ok && !g_w.abandon) {
            ot_copy(err, sizeof(err), g_w.err);
            if (k == W_ENTRIES || k == W_DOWNLOAD) {
                g_pf.kind = k;
                g_pf.ri = g_w.ri;
                g_pf.ei = g_w.ei;
                ot_copy(g_pf.text, sizeof(g_pf.text), g_w.err);
            }
        }
        if (k == W_PRESETS || k == W_SEARCH) {
            g_out.fresh = true;
            g_out.kind = k;
            g_out.ok = want;
        }
        g_w.state = 0;
    }
    if (locked) {
        sy->mutex_unlock(g_mx);
    }
}

// Starts the job for a request. False, with the reason in `why`: another request is in flight (or its result is not taken: call collect first), or the base gave no task.
bool work_begin(int kind, const char *path, const char *cid, uint32_t max, int ri, int ei, char *why, int whyn)
{
    if (!g_api || !g_api->job || !g_api->job->start) {
        ot_copy(why, whyn, "No task for the request");
        return false;
    }
    const SvcSync *sy = g_api->sync;
    bool locked = g_mx && sy && sy->mutex_lock && sy->mutex_lock(g_mx, SYNC_FOREVER);   // the menu task (prefetch) and an op run by REST or a hotkey both start requests
    if (g_w.state != 0) {
        if (locked) {
            sy->mutex_unlock(g_mx);
        }
        ot_copy(why, whyn, "The server is being asked for something else: try again in a moment");
        return false;
    }
    g_w.kind = kind;
    ot_copy(g_w.path, sizeof(g_w.path), path);
    g_w.cid = cid;
    g_w.max = max;
    g_w.ri = ri;
    g_w.ei = ei;
    g_w.abandon = false;
    g_w.jh = 0;
    g_w.state = 1;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    bool started = g_api->job->start(work_job, 0, WORK_STACK);   // an ended job that nobody joined gives its slot to this one (api.md 4.6.6); the menu task never joins (4.6.11.3)
    if (!started) {
        g_w.state = 0;
    }
    if (locked) {
        sy->mutex_unlock(g_mx);
    }
    if (!started) {
        ot_copy(why, whyn, "No free task for the request");
        return false;
    }
    return true;
}

// Does the menu step this operation (api.md 4.14.9)? A caller that does not (REST, a hotkey) is not made to wait: the request goes out and the answer lands in the states.
bool stepping(void)
{
    return g_api && g_api->ctx && g_api->ctx->job_step && g_api->ctx->job_step();
}

// A form operation (online.presets, online.search). First call: starts the job and, for a stepping menu, answers `more`. Calls after that: still running -> `more`; ended -> *done is true and
// g_out says how it came to. The person's cancel (api.md 4.14.10) gives the request up: the job ends by itself, its result is dropped.
int form_step(int kind, const char *path, uint32_t max, OpOut &o, bool *done)
{
    *done = false;
    o.more = false;
    if (g_w.form == kind && stepping() && g_api->ctx && g_api->ctx->job_cancelled && g_api->ctx->job_cancelled()) {
        g_w.abandon = true;                                 // before collect: a result that came in the same tick is dropped, the answer is true
        g_w.form = 0;
        collect();
        ot_copy(o.msg, o.msglen, "Cancelled");
        return OPE_OK;
    }
    collect();
    if (g_w.form == kind && stepping()) {
        if (g_out.fresh && g_out.kind == kind) {
            g_out.fresh = false;
            g_w.form = 0;
            *done = true;
            return OPE_OK;
        }
        if (g_w.state != 0) {
            o.more = true;
            return OPE_OK;
        }
        g_w.form = 0;                                       // the job is gone without an answer
        ot_copy(o.msg, o.msglen, "The request was lost");
        return OPE_FAILED;
    }
    if (g_w.form != 0 && !stepping()) {                     // the menu is stepping a request: a call from REST or a hotkey neither takes its answer nor starts another
        ot_copy(o.msg, o.msglen, "The server is being asked for something else: try again in a moment");
        return OPE_FAILED;
    }
    char why[96];
    g_out.fresh = false;
    if (!work_begin(kind, path, lib_id(), max, 0, 0, why, sizeof(why))) {
        ot_copy(o.msg, o.msglen, why);
        return OPE_FAILED;
    }
    if (stepping()) {
        g_w.form = kind;
        o.more = true;
    } else {
        ot_copy(o.msg, o.msglen, kind == W_SEARCH ? "Asking the server: the results come to online.results" : "Asking the server: the choices come when it answers");
    }
    return OPE_OK;
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
    bool done;
    int rc = form_step(W_PRESETS, URL_PATTERNS, ONLINE_JSON_MAX, o, &done);
    if (!done) {
        return rc;
    }
    if (!g_out.ok) {
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
    char path[1024];
    path[0] = 0;
    if (g_w.form != W_SEARCH) {                                // the first call builds the query; the calls after it only wait
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
        ot_cat(path, sizeof(path), URL_SEARCH);
        url_cat(path, sizeof(path), q);
    }
    bool done;
    int rc = form_step(W_SEARCH, path, ONLINE_JSON_MAX, o, &done);
    if (!done) {
        return rc;
    }
    if (!g_out.ok) {
        ot_copy(o.msg, o.msglen, "Connection failed: ");
        ot_cat(o.msg, o.msglen, err);
        return OPE_FAILED;
    }
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
    // D41 (L-3c): and the menu goes there, after the job: the Library is the first tab of F3 (the request is the main loop's, api.md 4.17.19). A base without the call leaves the menu on this tab.
    if (g_api && LIB_API_COVERS(g_api, overlay) && g_api->overlay && g_api->overlay->size >= __builtin_offsetof(SvcOverlay, goto_screen) + sizeof(g_api->overlay->goto_screen) && g_api->overlay->goto_screen) {
        g_api->overlay->goto_screen("F3");
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
const char *st_loaded(CoreCtx *) { collect(); return pres_loaded ? "yes" : 0; }
const char *st_unloaded(CoreCtx *) { collect(); return pres_loaded ? 0 : "yes"; }
const char *st_folder(CoreCtx *) { return "/Online/results"; }          // lib.show opens the folder of this path: "/Online"
const char *st_results(CoreCtx *)
{
    static char b[40];
    collect();
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
    collect();
    if (N >= npres) {
        return "";
    }
    ot_copy(t, sizeof(t), pres[N].type);
    if (t[0] >= 'a' && t[0] <= 'z') {
        t[0] = (char)(t[0] - 32);
    }
    return t;
}
template <int N> const char *st_pvalue(CoreCtx *) { collect(); return N < npres ? (pres[N].sel < 0 ? "Any" : pres[N].name[pres[N].sel]) : ""; }
template <int N> const char *st_phide(CoreCtx *) { collect(); return N < npres ? 0 : "yes"; }

// ---- the folder /Online -----------------------------------------------------------------------------------------------------------------------------------
// Called on the menu task (netfs.cc): none of these connects, sends or receives. What is kept is answered at once; what is not kept is asked for by a prefetch (one job, Work) and the call says
// so (tab_error) and fails; a later call finds it ready, or the reason it failed (told once: the call after that asks again).
bool tab_has_results(void)
{
    collect();
    return nres > 0;
}
const char *tab_error(void) { return err; }
// D41 (L-3c): the prefetch of a result's files or of a file's bytes is still running (or has ended and is not taken over yet): the file manager waits and lists the folder again instead of failing once.
bool tab_loading(void)
{
    return g_w.state != 0 && (g_w.kind == W_ENTRIES || g_w.kind == W_DOWNLOAD);
}

int find_res(const char *label);
// The job that runs is for this file of a result (the result's entries, or the file's download).
bool tab_loading_for(const char *rel)
{
    while (*rel == '/') {
        rel++;
    }
    const char *sl = strchr(rel, '/');
    if (!sl || g_w.state == 0 || (g_w.kind != W_ENTRIES && g_w.kind != W_DOWNLOAD)) {
        return false;
    }
    char lab[64];
    size_t ll = (size_t)(sl - rel) < sizeof(lab) - 1 ? (size_t)(sl - rel) : sizeof(lab) - 1;
    memcpy(lab, rel, ll);
    lab[ll] = 0;
    const int ri = find_res(lab);
    if (ri < 0 || ri != g_w.ri) {
        return false;
    }
    if (g_w.kind == W_ENTRIES) {
        return true;
    }
    for (int i = 0; i < nent; i++) {
        if (ot_ieq(ent[i].name, sl + 1)) {
            return ent[i].idx == g_w.ei;
        }
    }
    return false;
}

int find_res(const char *label)
{
    for (int i = 0; i < nres; i++) {
        if (ot_ieq(res[i].label, label)) {
            return i;
        }
    }
    return -1;
}

// Asks for the request in the background, unless it is asked for already; the words say what the caller gets to read in tab_error. Always false: the caller does not have it yet.
bool prefetch(int kind, int ri, int ei, const char *path, uint32_t max, const char *loading)
{
    if (g_pf.kind == kind && g_pf.ri == ri && g_pf.ei == ei) {
        ot_copy(err, sizeof(err), g_pf.text);
        g_pf.kind = 0;
        return false;
    }
    if (g_w.state != 0 && g_w.kind == kind && g_w.ri == ri && g_w.ei == ei) {
        ot_copy(err, sizeof(err), loading);
        return false;
    }
    char why[96];
    ot_copy(err, sizeof(err), work_begin(kind, path, lib_id(), max, ri, ei, why, sizeof(why)) ? loading : why);
    return false;
}

bool have_entries(int ri)
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
    return prefetch(W_ENTRIES, ri, -1, path, ONLINE_JSON_MAX, "Loading the files of this result: open it again in a moment");
}

int tab_list(const char *rel, FsEntry *out, int max)
{
    collect();
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
    if (ri < 0 || !have_entries(ri)) {
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
    collect();
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
    if (!have_entries(ri)) {
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

// The bytes of a file of a result in a block of the caller's allocator (the base frees it with its own free: a block of this sub-app's account is no block of the base's). A file that is not
// kept yet is downloaded in the background: null, and tab_error says so; the call after the download has ended gets the bytes.
uint8_t *tab_fetch(const char *rel, uint8_t *(*alloc)(uint32_t n), uint32_t *len)
{
    collect();
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
    if (ri < 0 || !have_entries(ri)) {
        return 0;
    }
    int ei = -1;
    for (int i = 0; i < nent; i++) {
        if (ot_ieq(ent[i].name, sl + 1)) {
            ei = i;
        }
    }
    if (ei < 0) {
        ot_copy(err, sizeof(err), "No such file in this result");
        return 0;
    }
    if (!(dl_b && dl_res == ri && dl_idx == ent[ei].idx)) {
        drop_download();                                    // the job never touches dl_b: the old file goes before the new one grows, so one download is the most at once
        char path[160];
        path[0] = 0;
        ot_cat(path, sizeof(path), URL_DOWNLOAD "/");
        url_cat(path, sizeof(path), res[ri].id);
        ot_cat(path, sizeof(path), "/");
        ot_cat_int(path, sizeof(path), res[ri].cat);
        ot_cat(path, sizeof(path), "/");
        ot_cat_int(path, sizeof(path), ent[ei].idx);
        prefetch(W_DOWNLOAD, ri, ent[ei].idx, path, ONLINE_DOWNLOAD_MAX, "Downloading the file: open it again in a moment");
        return 0;
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

const OnlineApi online_table = { sizeof(OnlineApi), tab_has_results, tab_list, tab_stat, tab_fetch, tab_error, tab_loading, tab_loading_for };

void online_attach(const LibApi *api)
{
    g_api = api;
    if (!g_mx && api->sync && api->sync->mutex_new) {
        g_mx = api->sync->mutex_new();
    }
}
