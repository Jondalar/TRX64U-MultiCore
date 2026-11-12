#include "rest.h"
#include "svc.h"
#include "ops_glue.h"
#include "wsproto.h"
#include "netalloc.h"
#include "uic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// ---- the alias table (Q2) ------------------------------------------------------------------------------------------------------
// The paths of Gideon's REST that our own tests and scripts use, each pointing at an operation of this service layer. An alias whose operation no
// contributor of this core registered answers 501 and names it, so a script finds out what the core lacks instead of getting a 404.
// `machine:reset` is the CPU reset that keeps the RAM and `machine:reboot` the full restart (T79.1 PoC 3/4: reset writes the ROM images back,
// reboot initialises them all); `readmem` and `writemem` need an operation that a core registers for its own memory (none does yet).
const RestAlias rest_aliases[] = {
    { "PUT", "/v1/machine:reset", "machine.warm_reset", 0, 0 },
    { "PUT", "/v1/machine:reboot", "machine.cold_reset", 0, 0 },
    { "PUT", "/v1/machine:reload_roms", "machine.reload_roms", 0, 0 },
    { "GET", "/v1/machine:readmem", "machine.read_mem", 0, 0 },
    { "PUT", "/v1/machine:writemem", "machine.write_mem", 0, 0 },
    { "POST", "/v1/machine:writemem", "machine.write_mem", 0, 0 },
    { "PUT", "/v1/runners:run_prg", "prg.run|c64.run_prg", "file", "path" },
    { "PUT", "/v1/runners:load_prg", "prg.load|c64.load_prg", "file", "path" },
    { "GET", "/v1/configs/{_}/{key}", "settings.get", 0, 0 },
    { "PUT", "/v1/configs/{_}/{key}", "settings.set", 0, 0 },
    { "PUT", "/v1/configs:reset_to_default", "settings.reset_to_host", 0, 0 },
};
const int rest_naliases = (int)(sizeof(rest_aliases) / sizeof(rest_aliases[0]));

// ---- responses -------------------------------------------------------------------------------------------------------------------
static const char *reason(int status)
{
    switch (status) {
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 202: return "Accepted";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 414: return "URI Too Long";
    case 422: return "Unprocessable Entity";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    }
    return "Error";
}

static int respond(char *out, int cap, int status, const char *body, int blen)
{
    int h = snprintf(out, cap, "HTTP/1.1 %d %s\r\nServer: TRX64 core app\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                     status, reason(status), blen);
    if (h < 0 || h + blen >= cap) {
        return -1;
    }
    memcpy(out + h, body, blen);
    out[h + blen] = 0;
    return h + blen;
}

// A body built in place: it starts at out + BODY_AT, and the header is written in front of it once its length is known. A page of a listing is
// then not held twice (the task's stack is small).
#define BODY_AT 192
static int respond_inplace(char *out, int cap, int status, int blen)
{
    char head[BODY_AT];
    int h = snprintf(head, sizeof(head), "HTTP/1.1 %d %s\r\nServer: TRX64 core app\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                     status, reason(status), blen);
    if (h < 0 || h >= BODY_AT || BODY_AT + blen >= cap) {
        return -1;
    }
    memmove(out + h, out + BODY_AT, blen);
    memcpy(out, head, h);
    out[h + blen] = 0;
    return h + blen;
}

static int respond_text(char *out, int cap, int status, const char *msg)
{
    char b[300];
    int p = 0;
    json_put(b, sizeof(b), &p, "{\"code\":%d,\"msg\":", status == 404 ? OPE_NOT_FOUND : OPE_ARG);
    json_put_string(b, sizeof(b), &p, msg);
    json_put(b, sizeof(b), &p, ",\"more\":false}");
    return respond(out, cap, status, b, p);
}

int rest_serve_bad(const HttpReq &q, char *out, int cap)
{
    return respond_text(out, cap, q.bad_status ? q.bad_status : 400, "The request could not be read");
}

// ---- arguments from the query and the body ----------------------------------------------------------------------------------------
// Fills `o` from "a=1&b=x" (percent decoded). Later members win over earlier ones of the same name.
static bool form_into(const char *s, JsonObj *o, char *why, int whylen)
{
    while (s && *s) {
        const char *amp = strchr(s, '&');
        int len = amp ? (int)(amp - s) : (int)strlen(s);
        const char *eq = (const char *)memchr(s, '=', len);
        if (len > 0) {
            int kl = eq ? (int)(eq - s) : len;
            char key[JSON_KEY_MAX];
            if (kl >= (int)sizeof(key) || !http_unescape(s, kl, key, sizeof(key), true)) {
                snprintf(why, whylen, "a name in the arguments is too long or badly escaped");
                return false;
            }
            int vl = eq ? len - kl - 1 : 0;
            char *dst = o->buf + o->used;
            if (vl >= JSON_BUF_MAX - o->used || (vl && !http_unescape(eq + 1, vl, dst, JSON_BUF_MAX - o->used, true))) {
                snprintf(why, whylen, "a value in the arguments is too long or badly escaped");
                return false;
            }
            if (!vl) {
                dst[0] = 0;
            }
            int slot = -1;
            for (int k = 0; k < o->n; k++) {
                if (!strcmp(o->key[k], key)) {
                    slot = k;
                }
            }
            if (slot < 0) {
                if (o->n >= JSON_KV_MAX) {
                    snprintf(why, whylen, "more arguments than this adapter keeps");
                    return false;
                }
                slot = o->n++;
                memcpy(o->key[slot], key, strlen(key) + 1);
            }
            memset(&o->v[slot], 0, sizeof(o->v[slot]));
            o->v[slot].t = J_STR;
            o->v[slot].s = dst;
            o->used += vl + 1;
        }
        s = amp ? amp + 1 : 0;
    }
    return true;
}

static bool args_of(const HttpReq &q, JsonObj *o, char *why, int whylen)
{
    memset(o, 0, sizeof(*o));
    if (!form_into(q.query, o, why, whylen)) {
        return false;
    }
    if (q.body_len) {
        const char *b = q.body;
        while (*b == ' ' || *b == '\r' || *b == '\n') b++;
        if (*b == '{') {
            JsonObj j;
            if (json_parse_object(q.body, q.body_len, &j, why, whylen)) {
                return false;
            }
            for (int k = 0; k < j.n; k++) {
                int slot = -1;
                for (int m = 0; m < o->n; m++) {
                    if (!strcmp(o->key[m], j.key[k])) {
                        slot = m;
                    }
                }
                if (slot < 0) {
                    if (o->n >= JSON_KV_MAX) {
                        snprintf(why, whylen, "more arguments than this adapter keeps");
                        return false;
                    }
                    slot = o->n++;
                    memcpy(o->key[slot], j.key[k], strlen(j.key[k]) + 1);
                }
                o->v[slot] = j.v[k];
                if (j.v[k].t == J_STR) {
                    int l = (int)strlen(j.v[k].s) + 1;
                    if (o->used + l > JSON_BUF_MAX) {
                        snprintf(why, whylen, "the arguments are longer than this adapter keeps");
                        return false;
                    }
                    memcpy(o->buf + o->used, j.v[k].s, l);
                    o->v[slot].s = o->buf + o->used;
                    o->used += l;
                }
            }
        } else if (!form_into(q.body, o, why, whylen)) {
            return false;
        }
    }
    return true;
}

// ---- routes ------------------------------------------------------------------------------------------------------------------------
static int from_of(const HttpReq &q)
{
    JsonObj o;
    char why[64];
    memset(&o, 0, sizeof(o));
    if (!form_into(q.query, &o, why, sizeof(why))) {
        return 0;
    }
    const JsonVal *v = o.get("from");
    return v && v->t == J_STR ? atoi(v->s) : 0;
}

static int list_ops(CoreCtx *c, const HttpReq &q, char *out, int cap)
{
    OpRegistry *reg = ops_of(c);
    char *body = out + BODY_AT;
    int bcap = cap - BODY_AT - 1;
    int p = 0, from = from_of(q), i = from;
    json_put(body, bcap, &p, "{\"count\":%d,\"ops\":[", reg->nops());
    for (; i < reg->nops(); i++) {
        int mark = p;
        if ((i > from && !json_put(body, bcap - 40, &p, ",")) || !svc_put_op(body, bcap - 40, &p, *reg->op_at(i))) {
            p = mark;
            break;
        }
    }
    json_put(body, bcap, &p, "],\"next\":");
    if (i < reg->nops() && i > from) {
        json_put(body, bcap, &p, "%d}", i);
    } else {
        json_put(body, bcap, &p, "null}");
    }
    return respond_inplace(out, cap, 200, p);
}

static int list_states(CoreCtx *c, const HttpReq &q, char *out, int cap)
{
    OpRegistry *reg = ops_of(c);
    char *body = out + BODY_AT;
    int bcap = cap - BODY_AT - 1;
    int p = 0, from = from_of(q), i = from;
    json_put(body, bcap, &p, "{\"count\":%d,\"state\":[", reg->nstates());
    for (; i < reg->nstates(); i++) {
        int mark = p;
        if ((i > from && !json_put(body, bcap - 40, &p, ",")) || !svc_put_state(c, body, bcap - 40, &p, *reg->state_at(i))) {
            p = mark;
            break;
        }
    }
    json_put(body, bcap, &p, "],\"next\":");
    if (i < reg->nstates() && i > from) {
        json_put(body, bcap, &p, "%d}", i);
    } else {
        json_put(body, bcap, &p, "null}");
    }
    return respond_inplace(out, cap, 200, p);
}

static int list_events(CoreCtx *c, char *out, int cap)
{
    OpRegistry *reg = ops_of(c);
    char body[2048];
    int p = 0;
    json_put(body, sizeof(body), &p, "{\"count\":%d,\"events\":[", reg->nevents());
    for (int i = 0; i < reg->nevents(); i++) {
        if ((i && !json_put(body, sizeof(body), &p, ",")) || !svc_put_event(body, sizeof(body), &p, *reg->event_at(i))) {
            break;
        }
    }
    json_put(body, sizeof(body), &p, "]}");
    return respond(out, cap, 200, body, p);
}

static int respond_result(char *out, int cap, const OpResult &r)
{
    char b[OP_MSG_MAX * 2 + 64 + 400];
    int p = 0;
    svc_put_result(b, sizeof(b), &p, r);
    return respond(out, cap, svc_http_status(r.code), b, p);
}

// The alias that matches method and path, with the segment arguments filled into `o`. Null: none.
static const RestAlias *match_alias(const char *method, const char *path, JsonObj *o)
{
    for (int a = 0; a < rest_naliases; a++) {
        const RestAlias &al = rest_aliases[a];
        if (strcmp(al.method, method)) {
            continue;
        }
        const char *pat = al.pattern, *s = path;
        JsonObj t;
        memset(&t, 0, sizeof(t));
        bool ok = true;
        while (ok && *pat && *s) {
            if (*pat == '{') {
                const char *close = strchr(pat, '}');
                if (!close) {
                    ok = false;
                    break;
                }
                char name[JSON_KEY_MAX];
                int nl = (int)(close - pat - 1);
                if (nl <= 0 || nl >= (int)sizeof(name)) {
                    ok = false;
                    break;
                }
                memcpy(name, pat + 1, nl);
                name[nl] = 0;
                const char *seg_end = strchr(s, '/');
                int sl = seg_end ? (int)(seg_end - s) : (int)strlen(s);
                if (sl == 0) {
                    ok = false;
                    break;
                }
                if (strcmp(name, "_")) {
                    if (t.n >= JSON_KV_MAX || t.used + sl + 1 > JSON_BUF_MAX) {
                        ok = false;
                        break;
                    }
                    memcpy(t.buf + t.used, s, sl);
                    t.buf[t.used + sl] = 0;
                    memcpy(t.key[t.n], name, nl + 1);
                    t.v[t.n].t = J_STR;
                    t.v[t.n].s = t.buf + t.used;
                    t.used += sl + 1;
                    t.n++;
                }
                pat = close + 1;
                s += sl;
            } else if (*pat == *s) {
                pat++;
                s++;
            } else {
                ok = false;
            }
        }
        if (ok && !*pat && !*s) {
            // hand the segment arguments to the caller's object, behind what is already there
            for (int k = 0; k < t.n && o->n < JSON_KV_MAX; k++) {
                int l = (int)strlen(t.v[k].s) + 1;
                if (o->used + l > JSON_BUF_MAX) {
                    break;
                }
                memcpy(o->buf + o->used, t.v[k].s, l);
                memcpy(o->key[o->n], t.key[k], strlen(t.key[k]) + 1);
                o->v[o->n].t = J_STR;
                o->v[o->n].s = o->buf + o->used;
                o->used += l;
                o->n++;
            }
            return &al;
        }
    }
    return 0;
}

static bool path_known(const char *path)
{
    for (int a = 0; a < rest_naliases; a++) {
        JsonObj t;
        memset(&t, 0, sizeof(t));
        // the same path under another method: the pattern with every method
        const RestAlias &al = rest_aliases[a];
        if (match_alias(al.method, path, &t)) {
            return true;
        }
    }
    return false;
}

// Gideon's clients look for "errors": an empty list is success; ours add the result of the operation
static int alias_reply(char *out, int cap, const OpResult &r)
{
    char b[OP_MSG_MAX * 2 + 96 + 400];
    int p = 0;
    json_put(b, sizeof(b), &p, "{\"errors\":[");
    if (r.code) {
        json_put_string(b, sizeof(b), &p, r.msg);
    }
    json_put(b, sizeof(b), &p, "],\"result\":");
    svc_put_result(b, sizeof(b), &p, r);
    json_put(b, sizeof(b), &p, "}");
    return respond(out, cap, svc_http_status(r.code), b, p);
}

static int serve_alias(CoreCtx *c, const HttpReq &q, JsonObj *o, const RestAlias *al, char *out, int cap)
{
    OpRegistry *reg = ops_of(c);
    char names[96];
    snprintf(names, sizeof(names), "%s", al->ops);
    const char *op = 0;
    for (char *tok = strtok(names, "|"); tok; tok = strtok(0, "|")) {
        if (reg->find_op(tok)) {
            op = tok;
            break;
        }
    }
    OpResult r;
    if (!op) {
        svc_fail(&r, OPE_UNSUPPORTED, "%s needs the operation %s, which this core did not register", q.path, al->ops);
        return alias_reply(out, cap, r);
    }
    if (rest_ui_op_destructive && rest_ui_op_destructive(op)) {                   // an alias carries its arguments in the path: it cannot wait for an answer
        svc_fail(&r, OPE_FAILED, "%s asks for a confirmation first: POST /v1/ops/%s and answer the question it opens", q.path, op);
        return alias_reply(out, cap, r);
    }
    if (al->from) {
        for (int k = 0; k < o->n; k++) {
            if (!strcmp(o->key[k], al->from) && !o->get(al->to)) {
                snprintf(o->key[k], JSON_KEY_MAX, "%s", al->to);
            }
        }
    }
    svc_call(c, op, o, &r);
    return alias_reply(out, cap, r);
}

UicService *rest_uic;
bool (*rest_ui_op_destructive)(const char *op);
bool (*rest_ui_op_ask)(const char *op, const char *query, const char *body, int body_len, char *reply, int cap, int *status);

int rest_op_exec(CoreCtx *c, const char *name, const char *query, const char *body, int body_len, OpResult *r)
{
    HttpReq *q = net_new<HttpReq>();
    JsonObj *a = net_new<JsonObj>();
    if (!q || !a) {                                                       // no memory (the board's `new` would halt): the call fails, it does not run
        net_delete(q);
        net_delete(a);
        svc_fail(r, OPE_FAILED, "no memory for the request");
        return OPE_FAILED;
    }
    memset(q, 0, sizeof(*q));
    snprintf(q->query, sizeof(q->query), "%s", query ? query : "");
    int bl = body_len < HTTP_BODY_MAX ? body_len : HTTP_BODY_MAX;
    if (bl > 0) memcpy(q->body, body, bl);
    q->body_len = bl;
    char why[160];
    int rc;
    if (!args_of(*q, a, why, sizeof(why))) {
        svc_fail(r, OPE_ARG, "%s", why);
        rc = OPE_ARG;
    } else {
        rc = svc_call(c, name, a, r);
    }
    net_delete(a);
    net_delete(q);
    return rc;
}
int svc_call_net(CoreCtx *c, const char *name, const JsonObj *args, OpResult *r)
{
    if (rest_ui_op_destructive && rest_ui_op_destructive(name)) {
        svc_fail(r, OPE_FAILED, "%s asks for a confirmation first: POST /v1/ops/%s and answer the question it opens", name, name);
        return r->code;
    }
    return svc_call(c, name, args, r);
}
int (*rest_report)(CoreCtx *c, char *body, int cap, char *stamp, int stamplen);

// T162: the report as a download - plain text, and the browser saves it under <YYYYMMDD-HHMMSS>.txt
static int respond_report(CoreCtx *c, char *out, int cap)
{
    if (!rest_report) {
        return respond_text(out, cap, 501, "This build has no bug report");
    }
    char stamp[24];
    int n = rest_report(c, out + BODY_AT, cap - BODY_AT, stamp, sizeof(stamp));
    if (n < 0) {
        return respond_text(out, cap, 503, "The bug report is being made or does not fit; try again");
    }
    char head[BODY_AT];
    int h = snprintf(head, sizeof(head), "HTTP/1.1 200 OK\r\nServer: TRX64 core app\r\nContent-Type: text/plain; charset=utf-8\r\n"
                     "Content-Disposition: attachment; filename=\"%s.txt\"\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", stamp, n);
    if (h < 0 || h >= BODY_AT) {
        return -1;
    }
    memmove(out + h, out + BODY_AT, n);
    memcpy(out, head, h);
    out[h + n] = 0;
    return h + n;
}

static char s_start_what[200];
static volatile bool s_start_gate, s_start_done;

void rest_starting(const char *what)
{
    if (s_start_done) {
        return;
    }
    snprintf(s_start_what, sizeof(s_start_what), "%s", what ? what : "");   // a request may read it meanwhile: the text can be torn, never unterminated
    s_start_gate = true;
}

void rest_started(void)
{
    s_start_done = true;
    s_start_gate = false;
}

bool rest_start_gate(void)
{
    return s_start_gate;
}

int rest_serve(CoreCtx *c, const HttpReq &q, char *out, int cap, bool *ws)
{
    *ws = false;
    if (s_start_gate) {                                                   // before the token gate exists: nothing of the service is reached
        char what[sizeof(s_start_what)];
        memcpy(what, s_start_what, sizeof(what));
        what[sizeof(what) - 1] = 0;
        char b[300];
        int p = 0;
        const char *msg = what[0] ? what : "Starting";
        int keep = (int)strlen(msg);                                      // too long for b: cut the text until the string and the tail fit, the JSON stays valid
        for (;;) {
            char cut[sizeof(s_start_what)];
            memcpy(cut, msg, keep);
            cut[keep] = 0;
            p = 0;
            bool ok = json_put(b, sizeof(b), &p, "{\"code\":-1,\"msg\":") && json_put_string(b, sizeof(b), &p, cut)
                      && json_put(b, sizeof(b), &p, ",\"starting\":true,\"more\":false}");
            if (ok || keep == 0) {
                break;
            }
            keep -= keep > 16 ? 16 : keep;
        }
        return respond(out, cap, 503, b, p);
    }
    if (rest_uic) {                                                       // T192.1 D-G: the token gate comes before every route (S2)
        const bool is_ws_path = !strcmp(q.path, "/v1/ws") || !strcmp(q.path, "/v1/ui/ws");
        if (uic_gate(rest_uic, q.method, q.path, q.tok_q, q.tok_bearer, q.tok_x) != 0) {
            return respond_text(out, cap, 401, is_ws_path ? "A token is needed: ws://host/v1/ui/ws?t=<token>" : "A token is needed: t=<token>, Authorization: Bearer <token> or X-Trx-Token");
        }
    }
    if (rest_uic && !strcmp(q.path, "/")) {
        if (strcmp(q.method, "GET")) {
            return respond_text(out, cap, 405, "GET only");
        }
        char b[260];
        int p = 0;
        json_put(b, sizeof(b), &p, "{\"app\":\"TRX64 core application\",\"hello\":\"/v1/ui/hello\",\"login\":\"POST /v1/ui/grant (the first time), then t=<token>\"}");
        return respond(out, cap, 200, b, p);
    }
    if (q.ws_upgrade && !strcmp(q.method, "GET") && (!strcmp(q.path, "/v1/ws") || (rest_uic && !strcmp(q.path, "/v1/ui/ws")))) {
        char acc[32];
        ws_accept_key(q.ws_key, acc);
        int n = snprintf(out, cap, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", acc);
        if (n < 0 || n >= cap) {
            return -1;
        }
        *ws = true;
        return n;
    }
    const bool get = !strcmp(q.method, "GET"), post = !strcmp(q.method, "POST");
    if (strncmp(q.path, "/v1/", 4) != 0 && strcmp(q.path, "/v1")) {
        return respond_text(out, cap, 404, "Nothing here: the interface is under /v1/");
    }
    if (!strcmp(q.path, "/v1/info")) {
        if (!get) {
            return respond_text(out, cap, 405, "GET only");
        }
        char b[300];
        int p = 0;
        OpRegistry *reg = ops_of(c);
        json_put(b, sizeof(b), &p, "{\"api\":1,\"core\":");
        json_put_string(b, sizeof(b), &p, c->desc ? c->desc->cor_name : "");
        json_put(b, sizeof(b), &p, ",\"version\":");
        json_put_string(b, sizeof(b), &p, c->core_version);
        json_put(b, sizeof(b), &p, ",\"ops\":%d,\"state\":%d,\"events\":%d}", reg->nops(), reg->nstates(), reg->nevents());
        return respond(out, cap, 200, b, p);
    }
    if (rest_uic && strncmp(q.path, "/v1/ui/", 7) == 0) {
        int status = 200;
        int n = uic_http(rest_uic, q.method, q.path, q.body, q.body_len, out + BODY_AT, cap - BODY_AT - 1, &status);
        if (n >= 0) {
            return respond_inplace(out, cap, status, n);
        }
        if (n == -1) {
            return respond_text(out, cap, 507, "The answer does not fit");
        }
    }
    if (!strcmp(q.path, "/v1/report")) {
        return get ? respond_report(c, out, cap) : respond_text(out, cap, 405, "GET only");
    }
    if (!strcmp(q.path, "/v1/ops")) {
        return get ? list_ops(c, q, out, cap) : respond_text(out, cap, 405, "GET only");
    }
    if (!strcmp(q.path, "/v1/state")) {
        return get ? list_states(c, q, out, cap) : respond_text(out, cap, 405, "GET only");
    }
    if (!strcmp(q.path, "/v1/events")) {
        return get ? list_events(c, out, cap) : respond_text(out, cap, 405, "GET only");
    }
    if (!strncmp(q.path, "/v1/ops/", 8)) {
        const char *name = q.path + 8;
        if (get) {
            const OpDef *d = ops_of(c)->find_op(name);
            if (!d) {
                OpResult r;
                svc_fail(&r, OPE_NOT_FOUND, "No operation \"%s\" is registered", name);
                return respond_result(out, cap, r);
            }
            char b[1500];
            int p = 0;
            svc_put_op(b, sizeof(b), &p, *d);
            return respond(out, cap, 200, b, p);
        }
        if (!post) {
            return respond_text(out, cap, 405, "GET describes an operation, POST calls it");
        }
        if (rest_ui_op_destructive && rest_ui_op_ask && rest_ui_op_destructive(name)) {     // D18, D19a: no REST path runs a destructive operation unasked
            char b[300];
            int status = 202;
            rest_ui_op_ask(name, q.query, q.body, q.body_len, b, sizeof(b), &status);
            return respond(out, cap, status, b, (int)strlen(b));
        }
        JsonObj a;
        char why[160];
        OpResult r;
        if (!args_of(q, &a, why, sizeof(why))) {
            svc_fail(&r, OPE_ARG, "%s", why);
            return respond_result(out, cap, r);
        }
        svc_call(c, name, &a, &r);
        return respond_result(out, cap, r);
    }
    if (!strncmp(q.path, "/v1/state/", 10)) {
        if (!get) {
            return respond_text(out, cap, 405, "GET only");
        }
        const StateDef *d = ops_of(c)->find_state(q.path + 10);
        if (!d) {
            return respond_text(out, cap, 404, "No such state value");
        }
        char b[600];
        int p = 0;
        svc_put_state(c, b, sizeof(b), &p, *d);
        return respond(out, cap, 200, b, p);
    }
    // the alias table
    JsonObj a;
    char why[160];
    memset(&a, 0, sizeof(a));
    if (!args_of(q, &a, why, sizeof(why))) {
        OpResult r;
        svc_fail(&r, OPE_ARG, "%s", why);
        return respond_result(out, cap, r);
    }
    const RestAlias *al = match_alias(q.method, q.path, &a);
    if (al) {
        return serve_alias(c, q, &a, al, out, cap);
    }
    if (path_known(q.path)) {
        return respond_text(out, cap, 405, "That path takes another method");
    }
    return respond_text(out, cap, 404, "No such route");
}
