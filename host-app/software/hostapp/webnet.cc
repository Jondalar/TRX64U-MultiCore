#include "webnet.h"
#include "webui.h"
#include "hostreport_live.h"
#include "svcjson.h"
#include "wsproto.h"
#include "uic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "netalloc.h"

// A body built in place: it starts at out + BODY_AT and the header is written in front of it once its length is known, so a page is never held twice.
#define BODY_AT 192

static int finish(char *out, int cap, const char *status, const char *type, int blen, const char *extra)
{
    char head[BODY_AT];
    int h = snprintf(head, sizeof(head), "HTTP/1.1 %s\r\nServer: TRX64 host\r\nContent-Type: %s\r\n%sContent-Length: %d\r\nConnection: close\r\n\r\n",
                     status, type, extra, blen);
    if (h < 0 || h >= BODY_AT || BODY_AT + blen >= cap) {
        return -1;
    }
    memmove(out + h, out + BODY_AT, blen);
    memcpy(out, head, h);
    out[h + blen] = 0;
    return h + blen;
}

static int respond(char *out, int cap, const char *status, const char *type, const char *body, int len)
{
    if (BODY_AT + len >= cap) {
        return -1;
    }
    memcpy(out + BODY_AT, body, len);
    return finish(out, cap, status, type, len, "");
}

static int respond_error(char *out, int cap, int code)
{
    char msg[32];
    int n = snprintf(msg, sizeof(msg), "{\"error\":%d}\n", code);
    const char *st = code == 400 ? "400 Bad Request" : code == 405 ? "405 Method Not Allowed" : code == 409 ? "409 Conflict" :
                     code == 413 ? "413 Payload Too Large" : code == 414 ? "414 URI Too Long" : code == 431 ? "431 Request Header Fields Too Large" :
                     code == 501 ? "501 Not Implemented" : code == 503 ? "503 Service Unavailable" : "404 Not Found";
    return respond(out, cap, st, "application/json", msg, n);
}

// What a request renders from: its own copy of the snapshot and the note, taken at once.
struct View {
    WebSnapshot s;
    char note[96];
};

static int send_page(char *out, int cap, const View *v, const char *note)
{
    if (cap <= BODY_AT) {
        return -1;
    }
    int n = web_page(&v->s, note ? note : v->note, out + BODY_AT, cap - BODY_AT);
    return finish(out, cap, "200 OK", "text/html; charset=utf-8", n, "");
}

// GET /v1/report: the text (up to HOST_REPORT_MAX) goes out in the same piece as its headers; the buffer is released when it is copied.
static int send_report(char *out, int cap, const View *v)
{
    int len = 0;
    const char *text = hostreport_make(&v->s, &len);
    if (!text) {
        return respond_error(out, cap, 503);
    }
    bool valid;
    uint32_t ft = hostreport_fattime(&valid);
    char extra[200];
    web_report_head(ft, valid, extra, sizeof(extra));
    char head[BODY_AT + 120];
    int h = snprintf(head, sizeof(head), "HTTP/1.1 200 OK\r\nServer: TRX64 host\r\n%sContent-Length: %d\r\nConnection: close\r\n\r\n", extra, len);
    int r = -1;
    if (h > 0 && h + len < cap) {
        memcpy(out + h, text, len);
        memcpy(out, head, h);
        out[h + len] = 0;
        r = h + len;
    }
    hostreport_release();
    return r;
}

static UicService *g_uic;

void webnet_set_service(UicService *svc) { g_uic = svc; }
UicService *webnet_service() { return g_uic; }

bool webnet_authorized(const HttpReq &q)
{
    if (!g_uic || strncmp(q.path, "/v1/ui/", 7) != 0) {
        return true;                                   // the routes of before the service
    }
    return uic_gate(g_uic, q.method, q.path, q.tok_q, q.tok_bearer, q.tok_x) == 0;
}

// The routes of the UI service (uic_http), the socket's upgrade. -2: not ours.
static int serve_ui(const HttpReq &q, char *out, int cap, bool *ws)
{
    if (!g_uic || strncmp(q.path, "/v1/ui/", 7) != 0) {
        return -2;
    }
    if (!webnet_authorized(q)) {
        static const char b[] = "{\"error\":401,\"why\":\"A token is needed: t=<token>, Authorization: Bearer <token> or X-Trx-Token\"}";
        return respond(out, cap, "401 Unauthorized", "application/json", b, sizeof(b) - 1);
    }
    if (!strcmp(q.path, "/v1/ui/ws")) {
        if (!q.ws_upgrade || strcmp(q.method, "GET")) {
            return respond_error(out, cap, 405);
        }
        char acc[32];
        ws_accept_key(q.ws_key, acc);
        int n = snprintf(out, cap, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", acc);
        if (n < 0 || n >= cap) {
            return -1;
        }
        *ws = true;
        return n;
    }
    if (cap <= BODY_AT + 1) {
        return -1;
    }
    int status = 200;
    int n = uic_http(g_uic, q.method, q.path, q.body, q.body_len, out + BODY_AT, cap - BODY_AT - 1, &status);
    if (n == -2) {
        return respond_error(out, cap, 404);
    }
    if (n < 0) {
        return -1;
    }
    const char *st = status == 200 ? "200 OK" : status == 202 ? "202 Accepted" : status == 400 ? "400 Bad Request" : status == 401 ? "401 Unauthorized" :
                     status == 403 ? "403 Forbidden" : status == 404 ? "404 Not Found" : status == 405 ? "405 Method Not Allowed" : status == 409 ? "409 Conflict" :
                     status == 503 ? "503 Service Unavailable" : status == 504 ? "504 Gateway Timeout" : "500 Internal Server Error";
    return finish(out, cap, st, "application/json", n, "");
}

UicClient *webnet_ui_open()
{
    if (!g_uic) return 0;
    UicClient *c = net_new<UicClient>();
    if (c) uic_client_open(g_uic, c);
    return c;
}
void webnet_ui_close(UicClient *c)
{
    if (c && g_uic) uic_client_close(g_uic, c);
    net_delete(c);
}
int webnet_ui_handle(UicClient *c, const char *msg, int n, char *out, int cap) { return uic_client_in(g_uic, c, msg, n, out, cap); }
int webnet_ui_next(UicClient *c, char *out, int cap) { return uic_client_next(g_uic, c, out, cap); }
bool webnet_ui_done(UicClient *c) { return c->closing; }

int webnet_serve_bad(const HttpReq &q, char *out, int cap)
{
    return respond_error(out, cap, q.bad_status ? q.bad_status : 400);
}

// D19a for the T106 page: the question its destructive form opened. Only that one is shown on the page and only that one the page may answer (the page has no token).
static char g_web_q[12];

// Under the service's lock: g_web_q is shared by the client tasks, the question's fields are the UI task's.
static void attach_question(WebSnapshot *s)
{
    s->q_id[0] = s->q_text[0] = 0;
    if (!g_uic) {
        return;
    }
    uic_hold(g_uic);
    if (g_web_q[0]) {
        if (g_uic->q_open && !strcmp(g_uic->q_id, g_web_q)) {
            snprintf(s->q_id, sizeof(s->q_id), "%s", g_web_q);
            snprintf(s->q_text, sizeof(s->q_text), "%s", g_uic->q.text);
        } else {
            g_web_q[0] = 0;                                   // answered, timed out or closed
        }
    }
    uic_release(g_uic);
}

// POST /v1/menu/remove and POST /v1/answer with the service up. 0: not these routes.
static int serve_gate(const HttpReq &q, View *v, char *out, int cap)
{
    if (!g_uic || strcmp(q.method, "POST")) {
        return 0;
    }
    const bool rm = !strcmp(q.path, "/v1/menu/remove"), an = !strcmp(q.path, "/v1/answer");
    if (!rm && !an) {
        return 0;
    }
    UicIntent in;
    memset(&in, 0, sizeof(in));
    char val[24], note[120];
    int status = 404;
    note[0] = 0;
    if (rm) {
        if (!web_form_field(q.body, "index", val, sizeof(val)) || atoi(val) < 0 || atoi(val) >= v->s.n || !val[0]) {
            return respond_error(out, cap, 400);
        }
        in.kind = UD_OP;
        snprintf(in.item, sizeof(in.item), "entry.remove");
        snprintf(in.target, sizeof(in.target), "#%d", atoi(val));
    } else {
        char qid[12], choice[8];
        if (!web_form_field(q.body, "q", qid, sizeof(qid)) || !web_form_field(q.body, "choice", choice, sizeof(choice)) || (strcmp(choice, "yes") && strcmp(choice, "no"))) {
            return respond_error(out, cap, 400);
        }
        uic_hold(g_uic);
        bool mine = g_web_q[0] && !strcmp(qid, g_web_q);
        uic_release(g_uic);
        if (!mine) {
            return respond_error(out, cap, 409);              // not the page's question (a flow's question is the API's, with its token)
        }
        in.kind = UD_ANSWER;
        snprintf(in.q, sizeof(in.q), "%s", qid);
        snprintf(in.choice, sizeof(in.choice), "%s", choice);
    }
    UicReply r;
    uic_submit(g_uic, in, &r);
    if (rm) {
        if (r.r == UR_ASKED) {
            uic_hold(g_uic);
            snprintf(g_web_q, sizeof(g_web_q), "%s", r.q);
            uic_release(g_uic);
            status = 202;
            snprintf(note, sizeof(note), "a question is open - nothing is removed before Yes (question %s)", r.q);
        } else {
            status = r.r == UR_GONE ? 404 : r.r == UR_BAD ? 400 : 409;
            snprintf(note, sizeof(note), "not asked: %s", r.why[0] ? r.why : uic_r_name(r.r));
        }
    } else {
        status = r.r == UR_TAKEN ? 200 : 409;
        snprintf(note, sizeof(note), r.r == UR_TAKEN ? "answered: %s" : "not answered: %s", r.r == UR_TAKEN ? in.choice : (r.why[0] ? r.why : uic_r_name(r.r)));
        if (r.r == UR_TAKEN) {
            uic_hold(g_uic);
            g_web_q[0] = 0;
            uic_release(g_uic);
        }
    }
    attach_question(&v->s);
    if (rm && r.r == UR_ASKED && !v->s.q_text[0]) {           // the question text is the service's own: read it now
        uic_hold(g_uic);
        snprintf(v->s.q_id, sizeof(v->s.q_id), "%s", g_web_q);
        snprintf(v->s.q_text, sizeof(v->s.q_text), "%s", g_uic->q.text);
        uic_release(g_uic);
    }
    if (cap <= BODY_AT) {
        return -1;
    }
    int n = web_page(&v->s, note, out + BODY_AT, cap - BODY_AT);
    const char *st = status == 200 ? "200 OK" : status == 202 ? "202 Accepted" : status == 400 ? "400 Bad Request" : status == 404 ? "404 Not Found" : "409 Conflict";
    return finish(out, cap, st, "text/html; charset=utf-8", n, "");
}

static int serve_with(const HttpReq &q, View *v, char *out, int cap, bool *ws)
{
    attach_question(&v->s);
    {
        int r = serve_gate(q, v, out, cap);
        if (r) {
            return r;
        }
    }
    char uri[HTTP_PATH_MAX + HTTP_QUERY_MAX + 2];
    snprintf(uri, sizeof(uri), "%s%s%s", q.path, q.query[0] ? "?" : "", q.query);
    if (!strcmp(q.method, "GET")) {
        WebGet route = web_get_route(uri);
        if (route == WEBGET_STATUS) {
            if (cap <= BODY_AT) {
                return -1;
            }
            int n = web_json(&v->s, v->note, out + BODY_AT, cap - BODY_AT);
            return finish(out, cap, "200 OK", "application/json", n, "");
        }
        if (route == WEBGET_REPORT) {
            return send_report(out, cap, v);
        }
        if (q.ws_upgrade && !strcmp(q.path, "/v1/ws") && webnet_authorized(q)) {
            char acc[32];
            ws_accept_key(q.ws_key, acc);
            int n = snprintf(out, cap, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", acc);
            if (n < 0 || n >= cap) {
                return -1;
            }
            *ws = true;
            return n;
        }
        if (!strncmp(uri, "/v1/", 4)) {
            return respond_error(out, cap, 405);
        }
        return send_page(out, cap, v, 0);            // every other path is the page: a phone types no routes
    }
    if (!strcmp(q.method, "POST")) {
        WebCmd c;
        int status = 404;
        if (web_parse(uri, q.body, &c, &v->s, &status)) {
            if (!web_post(&c)) {
                return respond_error(out, cap, 409);   // the UI task has not taken the last command yet
            }
            return send_page(out, cap, v, "accepted - the menu applies it in a moment");
        }
        if (status == 400 && web_parse_why()[0]) {
            char msg[160];
            int n = snprintf(msg, sizeof(msg), "{\"error\":400,\"reason\":\"%s\"}\n", web_parse_why());
            return respond(out, cap, "400 Bad Request", "application/json", msg, n);
        }
        return respond_error(out, cap, status);
    }
    return respond_error(out, cap, 404);
}

int webnet_serve(const HttpReq &q, char *out, int cap, bool *ws)
{
    {
        int r = serve_ui(q, out, cap, ws);               // the service's routes need no snapshot copy of the T106 page
        if (r != -2) {
            return r;
        }
    }
    View *v = net_new<View>();          // too big for a task's stack; null (no memory) answers 503
    if (!v) {
        return respond_error(out, cap, 503);
    }
    web_copy(&v->s, v->note, sizeof(v->note));
    int r = serve_with(q, v, out, cap, ws);
    net_delete(v);
    return r;
}

// ---- WebSocket: JSON-RPC 2.0, the one method `hello` (the service layer plugs in behind it) -------------------------------------
void webnet_ws_open(WebnetWs *w)
{
    w->hello_sent = false;
    View *v = net_new<View>();
    memset(w->app, 0, sizeof(w->app));
    memset(w->build, 0, sizeof(w->build));
    memset(w->host_key, 0, sizeof(w->host_key));
    if (v) {
        web_copy(&v->s, v->note, sizeof(v->note));
        snprintf(w->app, sizeof(w->app), "%s", v->s.app);
        snprintf(w->build, sizeof(w->build), "%s", v->s.app_date);
        snprintf(w->host_key, sizeof(w->host_key), "%s", v->s.host_key);
        net_delete(v);
    }
}

static int put_hello(const WebnetWs *w, char *out, int cap, const char *id)
{
    int p = 0;
    bool ok = id ? json_put(out, cap, &p, "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":", id)
                 : json_put(out, cap, &p, "{\"jsonrpc\":\"2.0\",\"method\":\"hello\",\"params\":");
    ok = ok && json_put(out, cap, &p, "{\"role\":\"host\",\"transport\":1,\"auth\":\"none\",\"app\":") && json_put_string(out, cap, &p, w->app);
    ok = ok && json_put(out, cap, &p, ",\"build\":") && json_put_string(out, cap, &p, w->build);
    ok = ok && json_put(out, cap, &p, ",\"host_key\":") && json_put_string(out, cap, &p, w->host_key);
    ok = ok && json_put(out, cap, &p, "}}");
    return ok ? p : -1;
}

int webnet_ws_next(WebnetWs *w, char *out, int cap)
{
    if (w->hello_sent) {
        return 0;
    }
    w->hello_sent = true;
    int n = put_hello(w, out, cap, 0);
    return n < 0 ? 0 : n;
}

int webnet_ws_handle(WebnetWs *w, const char *msg, int n, char *out, int cap)
{
    JsonRpcReq r;
    int code = 0;
    char why[64];
    int p = 0;
    if (json_parse_rpc(msg, n, &r, &code, why, sizeof(why)) < 0) {
        return json_put(out, cap, &p, "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":%d,\"message\":", code) && json_put_string(out, cap, &p, why) &&
               json_put(out, cap, &p, "}}") ? p : -1;
    }
    if (!r.has_id) {
        return 0;
    }
    if (!strcmp(r.method, "hello")) {
        return put_hello(w, out, cap, r.id);
    }
    return json_put(out, cap, &p, "{\"jsonrpc\":\"2.0\",\"id\":%s,\"error\":{\"code\":-32601,\"message\":\"No such method\"}}", r.id) ? p : -1;
}
