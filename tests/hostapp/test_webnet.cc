// The host application's routes on the shared transport (T192.1 Q5, S19): every route of the T106 web UI answers with the status and the body shape it had on
// Gideon's HTTPServer, and /v1/ws speaks `hello`. The requests are raw bytes through the same reader the board's sockets feed.
#include "webnet.h"
#include "webui.h"
#include "hostreport_live.h"
#include "videomode.h"
#include "wsproto.h"
#include "uic.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>

static int checks, fails;
static bool g_nomem;                                       // M6: the allocator of what a request makes (netalloc.h) fails while this is set
extern "C" void *webnet_test_alloc(size_t n) { return g_nomem ? 0 : malloc(n); }
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// the report's board side, replaced: one text, the maker held until released
static bool g_held;
static int g_released;
static const char g_report[] = "TRX64 host bug report\nsection one\n";
const char *hostreport_make(const WebSnapshot *, int *len)
{
    if (g_held) return 0;
    g_held = true;
    *len = (int)strlen(g_report);
    return g_report;
}
void hostreport_release(void) { g_held = false; g_released++; }
uint32_t hostreport_fattime(bool *valid) { *valid = true; return ((2026 - 1980) << 25) | (10 << 21) | (30 << 16) | (14 << 11) | (5 << 5) | 5; }

static WebSnapshot snap(void)
{
    WebSnapshot s;
    memset(&s, 0, sizeof(s));
    snprintf(s.app, sizeof(s.app), "6a20094");
    snprintf(s.host_key, sizeof(s.host_key), "0e8fb06e");
    snprintf(s.ip, sizeof(s.ip), "192.168.2.44");
    s.net = true; s.usb = true; s.sd = true;
    snprintf(s.media, sizeof(s.media), "Usb1, Sd");
    s.vid_sys = VID_SYS_PAL50;
    s.vid_hdmi = VID_HDMI_1080P;
    s.vid_mask = 0x0007;                    // T164: the screen names 576p, 720p and 1080p at 50 Hz
    s.vid_def = -1;
    s.countdown = 5;
    s.def = 0;
    s.n = 2;
    snprintf(s.name[0], WEB_NAME_MAX, "TRX64 Ultimate <C64>");
    snprintf(s.path[0], WEB_PATH_MAX, "/Usb1/cores/trx64.cor");
    snprintf(s.state[0], 16, "ok");
    s.startable[0] = true;
    snprintf(s.name[1], WEB_NAME_MAX, "Atari 800 XL");
    snprintf(s.path[1], WEB_PATH_MAX, "/Usb1/cores/atari800.cor");
    snprintf(s.state[1], 16, "missing");
    s.startable[1] = false;
    return s;
}


struct Resp {
    int n;
    bool ws;
    int status;
    std::string head, body;
    std::string hdr(const char *name) const
    {
        size_t p = head.find(std::string("\r\n") + name + ": ");
        if (p == std::string::npos) return "";
        p += 4 + strlen(name);
        return head.substr(p, head.find("\r\n", p) - p);
    }
};

static char out[28672];

static Resp ask(const std::string &raw, int cap = sizeof(out))
{
    Resp r;
    HttpReq q;
    http_init(&q);
    http_feed(&q, raw.data(), (int)raw.size());
    r.ws = false;
    r.n = q.state == HS_READY ? webnet_serve(q, out, cap, &r.ws) : webnet_serve_bad(q, out, cap);
    r.status = 0;
    if (r.n > 0) {
        std::string all(out, r.n);
        size_t e = all.find("\r\n\r\n");
        r.head = all.substr(0, e);
        r.body = all.substr(e + 4);
        r.status = atoi(all.c_str() + 9);
    }
    return r;
}

static std::string get(const char *path) { return std::string("GET ") + path + " HTTP/1.1\r\nHost: x\r\n\r\n"; }
static std::string post(const char *path, const char *body)
{
    char cl[32];
    snprintf(cl, sizeof(cl), "%d", (int)strlen(body));
    return std::string("POST ") + path + " HTTP/1.1\r\nHost: x\r\nContent-Length: " + cl + "\r\n\r\n" + body;
}

// ---- the UI service behind /v1/ui/ (T192 D21): a minimal domain, the real uicore ------------------------------------------------------------------
static uint32_t u_t = 100;
static std::string u_file;
static bool u_have;
static uint32_t u_now(void *) { return u_t; }
static void u_nop(void *) { }
static UicService u_svc;
static char u_snapbuf[4096];
static void u_pump(void);
static void u_sleep(void *, int ms) { u_t += ms; u_pump(); }
static bool u_ui(void *) { return true; }
static int u_ent(void *, uint8_t *o, int n) { for (int i = 0; i < n && i < 16; i++) o[i] = (uint8_t)(i * 3 + 7); return n < 16 ? n : 16; }
static int u_load(void *, char *b, int c) { if (!u_have) return -1; int n = (int)u_file.size(); if (n > c) n = c; memcpy(b, u_file.data(), n); return n; }
static bool u_save(void *, const char *b, int n) { u_file.assign(b, n); u_have = true; return true; }
static bool u_rm(void *) { u_have = false; return true; }
struct UDom : UicDomain {
    bool pending = false, removed = false;
    int row = -1;
    const char *app() override { return "host"; }
    const char *keys_owner() override { return "menu"; }
    void snapshot(UicW &w) override { w.ks("screen", "menu"); }
    void intent(const UicIntent &in, UicReply *r) override {
        if (in.kind == UD_OP && !strcmp(in.item, "entry.remove")) {          // the host's destructive op: asks, the answer runs it (hostdomain.cc does the same)
            row = atoi(in.target + 1);
            UicQuestion q;
            memset(&q, 0, sizeof(q));
            snprintf(q.text, sizeof(q.text), "Remove this entry from the boot menu? Row %d", row);
            q.destructive = true;
            pending = true;
            uic_ask(&u_svc, q, r->q);
            r->r = UR_ASKED;
            return;
        }
        r->r = UR_TAKEN;
    }
    bool question(UicQuestion *q) override {
        if (!pending) return false;
        snprintf(q->text, sizeof(q->text), "Remove this entry from the boot menu? Row %d", row);
        q->destructive = true;
        return true;
    }
    void answer(const char *c) override { pending = false; if (!strcmp(c, "yes")) removed = true; }
};
static UDom u_dom;
static void u_pump(void) { uic_service(&u_svc); }

static void ui_routes(void)
{
    UicPort port;
    memset(&port, 0, sizeof(port));
    port.now_ms = u_now; port.lock = u_nop; port.unlock = u_nop; port.sleep_ms = u_sleep; port.on_ui_task = u_ui; port.entropy = u_ent;
    port.file_load = u_load; port.file_save = u_save; port.file_remove = u_rm;
    uic_init(&u_svc, &port, &u_dom, u_snapbuf, sizeof(u_snapbuf));
    webnet_set_service(&u_svc);
    CHECK(webnet_service() == &u_svc);
    // the routes of before the service stay open, with no token
    Resp r = ask(get("/v1/status"));
    CHECK(r.status == 200);
    r = ask(get("/"));
    CHECK(r.status == 200 && r.hdr("Content-Type") == "text/html; charset=utf-8");
    // the service's: hello is public, state needs the token, grant hands it out once
    r = ask(get("/v1/ui/hello"));
    CHECK(r.status == 200 && r.body.find("\"app\":\"host\"") != std::string::npos && r.body.find("\"grant\":true") != std::string::npos);
    r = ask(get("/v1/ui/state"));
    CHECK(r.status == 401);
    r = ask(post("/v1/ui/grant", ""));
    CHECK(r.status == 200);
    size_t at = r.body.find("\"token\":\"");
    CHECK(at != std::string::npos);
    std::string tok = r.body.substr(at + 9, 32);
    CHECK(tok.size() == 32 && u_have && u_file.substr(0, 32) == tok);
    r = ask(post("/v1/ui/grant", ""));
    CHECK(r.status == 403);
    r = ask(get("/v1/ui/state"));
    CHECK(r.status == 401);
    r = ask(get(("/v1/ui/state?t=" + tok).c_str()));
    CHECK(r.status == 200 && r.body.find("\"screen\":\"menu\"") != std::string::npos && r.body.find("\"app\":\"host\"") != std::string::npos);
    r = ask("GET /v1/ui/state HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer " + tok + "\r\n\r\n");
    CHECK(r.status == 200);
    r = ask("GET /v1/ui/state HTTP/1.1\r\nHost: x\r\nX-Trx-Token: " + tok + "\r\n\r\n");
    CHECK(r.status == 200);
    r = ask(post(("/v1/ui/intent?t=" + tok).c_str(), "{\"do\":\"key\",\"key\":\"f1\"}"));
    CHECK(r.status == 200 && r.body.find("\"r\":\"taken\"") != std::string::npos);
    r = ask(post("/v1/ui/intent", "{\"do\":\"key\",\"key\":\"f1\"}"));
    CHECK(r.status == 401);                                              // no token, no intent
    // the socket: the upgrade of /v1/ui/ws needs the token, the old /v1/ws does not
    std::string up = "GET /v1/ui/ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
    r = ask(up);
    CHECK(!r.ws && r.status == 401);
    std::string up2 = up;
    up2.replace(up2.find("/v1/ui/ws"), 9, ("/v1/ui/ws?t=" + tok).c_str());
    r = ask(up2);
    CHECK(r.ws && r.status == 101);
    up2 = up;
    up2.replace(up2.find("/v1/ui/ws"), 9, "/v1/ws");
    r = ask(up2);
    CHECK(r.ws && r.status == 101);
    // a session of the socket: hello, the snapshot, then intents
    UicClient *c = webnet_ui_open();
    CHECK(c != 0);
    char m[2048];
    int n = webnet_ui_next(c, m, sizeof(m));
    CHECK(n > 0 && std::string(m, n).find("\"t\":\"hello\"") != std::string::npos);
    n = webnet_ui_handle(c, "{\"do\":\"key\",\"key\":\"f3\"}", 27, m, sizeof(m));
    CHECK(n > 0 && std::string(m, n).find("\"r\":\"taken\"") != std::string::npos);
    CHECK(!webnet_ui_done(c));
    webnet_ui_close(c);
    // D19a on the T106 page: the remove form asks, nothing runs before Yes; the page shows the question with Yes / No that answer it
    {
        WebCmd none;
        r = ask(post("/v1/menu/remove", "index=1"));
        CHECK(r.status == 202 && r.body.find("nothing is removed before Yes") != std::string::npos);
        CHECK(r.body.find("Remove this entry from the boot menu? Row 1") != std::string::npos && r.body.find("action=\"/v1/answer\"") != std::string::npos &&
              r.body.find("name=\"choice\" value=\"yes\"") != std::string::npos && r.body.find("name=\"choice\" value=\"no\"") != std::string::npos);
        CHECK(!u_dom.removed && u_dom.pending);                          // asked, nothing removed
        CHECK(!web_take(&none));                                         // and no command waits in the page's mailbox
        size_t qa = r.body.find("name=\"q\" value=\"");
        CHECK(qa != std::string::npos);
        std::string qid = r.body.substr(qa + 16, r.body.find('"', qa + 16) - (qa + 16));
        r = ask(get("/"));                                               // the page shows the open question on any later view
        CHECK(r.status == 200 && r.body.find("Remove this entry from the boot menu? Row 1") != std::string::npos);
        r = ask(post("/v1/answer", ("q=" + qid + "&choice=maybe").c_str()));
        CHECK(r.status == 400);
        r = ask(post("/v1/answer", "q=q999&choice=yes"));
        CHECK(r.status == 409 && !u_dom.removed);                        // not the page's question
        r = ask(post("/v1/answer", ("q=" + qid + "&choice=no").c_str()));
        CHECK(r.status == 200 && r.body.find("answered: no") != std::string::npos);
        CHECK(!u_dom.removed && !u_dom.pending);                         // no: intact
        r = ask(get("/"));
        CHECK(r.body.find("Remove this entry") == std::string::npos);    // the question is gone from the page
        r = ask(post("/v1/answer", ("q=" + qid + "&choice=yes").c_str()));
        CHECK(r.status == 409 && !u_dom.removed);                        // a late answer does nothing
        r = ask(post("/v1/menu/remove", "index=9"));
        CHECK(r.status == 400);
        r = ask(post("/v1/menu/remove", "index=0"));
        CHECK(r.status == 202 && !u_dom.removed);
        qa = r.body.find("name=\"q\" value=\"");
        qid = r.body.substr(qa + 16, r.body.find('"', qa + 16) - (qa + 16));
        r = ask(post("/v1/answer", ("q=" + qid + "&choice=yes").c_str()));
        CHECK(r.status == 200 && u_dom.removed);                         // yes: removed
        // the JSON of the page's facts names the open question too
        u_dom.removed = false;
        r = ask(post("/v1/menu/remove", "index=1"));
        r = ask(get("/v1/status"));
        CHECK(r.body.find("\"question\":{") != std::string::npos);
    }
    webnet_set_service(0);
    r = ask(get("/v1/ui/state"));                                         // no service: the path is the page
    CHECK(r.status == 405 || r.status == 200 || r.status == 404);
}

int main(void)
{
    WebSnapshot s = snap();
    web_publish(&s);

    // every answer: Content-Length is the body, the server names itself, the connection closes
    const char *routes[] = { "/", "/index.html", "/anything/at/all", "/v1/status", "/v1/status?x=1", "/v1/report", "/v1/video", "/v1/boot", "/v1/nope", "/v1/ws" };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        Resp r = ask(get(routes[i]));
        CHECK(r.n > 0 && r.status > 0);
        CHECK(atoi(r.hdr("Content-Length").c_str()) == (int)r.body.size());
        CHECK(r.hdr("Server") == "TRX64 host" && r.hdr("Connection") == "close");
    }

    // M6: no memory for the request's view is a 503, not a halt; a service session that cannot be allocated is none
    g_nomem = true;
    {
        Resp r = ask(get("/v1/status"));
        CHECK(r.status == 503);
        r = ask(get("/"));
        CHECK(r.status == 503);
        CHECK(webnet_ui_open() == 0);
    }
    g_nomem = false;
    CHECK(ask(get("/v1/status")).status == 200);

    // the page: GET on any path that is not under /v1/
    {
        char want[16384];
        int wn = web_page(&s, web_note(), want, sizeof(want));
        for (const char *p : { "/", "/index.html", "/anything/at/all", "/?x=1" }) {
            Resp r = ask(get(p));
            CHECK(r.status == 200 && r.hdr("Content-Type") == "text/html; charset=utf-8");
            CHECK((int)r.body.size() == wn && r.body == std::string(want, wn));
        }
    }
    // GET /v1/status: the JSON of web_json
    {
        char want[16384];
        int wn = web_json(&s, web_note(), want, sizeof(want));
        Resp r = ask(get("/v1/status"));
        CHECK(r.status == 200 && r.hdr("Content-Type") == "application/json" && r.body == std::string(want, wn));
        CHECK(r.body.find("\"host_key\":\"0e8fb06e\"") != std::string::npos && r.body.find("\"menu\":[") != std::string::npos);
        r = ask(get("/v1/status?pretty=1"));
        CHECK(r.status == 200 && r.body == std::string(want, wn));
    }
    // GET /v1/report: the text, a download name from the clock; the maker is released
    {
        int rel = g_released;
        Resp r = ask(get("/v1/report"));
        CHECK(r.status == 200 && r.hdr("Content-Type") == "text/plain; charset=utf-8" && r.body == g_report);
        CHECK(r.hdr("Content-Disposition") == "attachment; filename=\"20261030-140510.txt\"");
        CHECK(g_released == rel + 1 && !g_held);
        g_held = true;                                          // another report in the making
        r = ask(get("/v1/report"));
        CHECK(r.status == 503 && r.body == "{\"error\":503}\n");
        g_held = false;
        r = ask(get("/v1/report"), 200);                        // does not fit: -1 (the transport answers 507), and the maker is released
        CHECK(r.n == -1 && !g_held);
    }
    // other GETs under /v1/ are 405 as before; a PUT or DELETE is 404 JSON
    for (const char *p : { "/v1/video", "/v1/boot", "/v1/countdown", "/v1/nope", "/v1/menu/remove", "/v1/default" }) {
        Resp r = ask(get(p));
        CHECK(r.status == 405 && r.body == "{\"error\":405}\n" && r.hdr("Content-Type") == "application/json");
    }
    for (const char *m : { "PUT", "DELETE", "HEAD", "PATCH" }) {
        Resp r = ask(std::string(m) + " /v1/status HTTP/1.1\r\nHost: x\r\n\r\n");
        CHECK(r.status == 404 && r.body == "{\"error\":404}\n");
    }
    // POST: the forms. 200 with the page and the note, the command in the mailbox
    {
        WebCmd c;
        while (web_take(&c)) { }
        Resp r = ask(post("/v1/video", "hdmi=1"));
        CHECK(r.status == 200 && r.hdr("Content-Type") == "text/html; charset=utf-8" && r.body.find("accepted - the menu applies it in a moment") != std::string::npos);
        CHECK(web_take(&c) && c.kind == WEB_VIDEO && c.b == 1);
        CHECK(!web_take(&c));
        r = ask(post("/v1/boot", "index=0"));
        CHECK(r.status == 200 && web_take(&c) && c.kind == WEB_BOOT && c.a == 0);
        r = ask(post("/v1/menu/remove", "index=1"));
        CHECK(r.status == 200 && web_take(&c) && c.kind == WEB_REMOVE && c.a == 1);
        r = ask(post("/v1/default", "index=0"));
        CHECK(r.status == 200 && web_take(&c) && c.kind == WEB_DEFAULT && c.a == 0);
        r = ask(post("/v1/countdown", "seconds=12"));
        CHECK(r.status == 200 && web_take(&c) && c.kind == WEB_COUNTDOWN && c.a == 12);
        r = ask("POST /v1/countdown?seconds=7 HTTP/1.1\r\nHost: x\r\n\r\n");        // a form that rode in the query string
        CHECK(r.status == 200 && web_take(&c) && c.kind == WEB_COUNTDOWN && c.a == 7);
        r = ask(post("/v1/video", "hdmi=-1"));
        CHECK(r.status == 200 && web_take(&c) && c.kind == WEB_VIDEO && c.b == -1);
        // a command still waiting: the next POST is told 409 and the first stays
        r = ask(post("/v1/countdown", "seconds=3"));
        CHECK(r.status == 200);
        r = ask(post("/v1/countdown", "seconds=4"));
        CHECK(r.status == 409 && r.body == "{\"error\":409}\n");
        CHECK(web_take(&c) && c.kind == WEB_COUNTDOWN && c.a == 3 && !web_take(&c));
        r = ask(post("/v1/countdown", "seconds=4"));
        CHECK(r.status == 200 && web_take(&c) && c.a == 4);
        // refused: nothing in the mailbox
        r = ask(post("/v1/boot", "index=1"));                   // in the menu, not startable
        CHECK(r.status == 409 && r.body == "{\"error\":409}\n" && !web_take(&c));
        r = ask(post("/v1/boot", "index=9"));
        CHECK(r.status == 400 && r.body == "{\"error\":400}\n" && !web_take(&c));
        r = ask(post("/v1/countdown", "seconds=61"));
        CHECK(r.status == 400 && !web_take(&c));
        r = ask(post("/v1/video", "hdmi=99"));
        CHECK(r.status == 400 && !web_take(&c));
        r = ask(post("/v1/format", "medium=Usb0"));
        CHECK(r.status == 404 && r.body == "{\"error\":404}\n" && !web_take(&c));
        r = ask(post("/", "x=1"));
        CHECK(r.status == 404 && !web_take(&c));
    }
    // a request that never became ready: its status, JSON
    {
        Resp r = ask("GET /v1/status HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n");
        CHECK(r.status == 501 && r.body == "{\"error\":501}\n");
        r = ask("NONSENSE\r\n\r\n");
        CHECK(r.status == 400 && r.body == "{\"error\":400}\n");
        r = ask("POST /v1/boot HTTP/1.1\r\nContent-Length: 5000\r\n\r\n");
        CHECK(r.status == 413);
    }
    // WebSocket: the upgrade (RFC 6455 sample key), hello first, hello as a call, other methods -32601
    {
        const char *up = "GET /v1/ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
        Resp r = ask(up);
        CHECK(r.ws && r.status == 101 && r.hdr("Sec-WebSocket-Accept") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" && r.hdr("Upgrade") == "websocket");
        r = ask(get("/v1/ws"));
        CHECK(!r.ws && r.status == 405);                         // no upgrade asked: as before
        r = ask(std::string(up).replace(0, 10, "GET /v1/xx"));
        CHECK(!r.ws && r.status == 405);
        CHECK(webnet_authorized(*(HttpReq *)calloc(1, sizeof(HttpReq))));

        WebnetWs w;
        webnet_ws_open(&w);
        char m[1024];
        int n = webnet_ws_next(&w, m, sizeof(m));
        std::string h(m, n);
        CHECK(n > 0 && h.find("\"method\":\"hello\"") != std::string::npos && h.find("\"role\":\"host\"") != std::string::npos &&
              h.find("\"app\":\"6a20094\"") != std::string::npos && h.find("\"host_key\":\"0e8fb06e\"") != std::string::npos && h.find("\"transport\":1") != std::string::npos);
        CHECK(webnet_ws_next(&w, m, sizeof(m)) == 0);            // once
        const char *call = "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"hello\"}";
        n = webnet_ws_handle(&w, call, (int)strlen(call), m, sizeof(m));
        h.assign(m, n);
        CHECK(n > 0 && h.find("\"id\":7") != std::string::npos && h.find("\"result\":{") != std::string::npos && h.find("\"role\":\"host\"") != std::string::npos);
        const char *other = "{\"jsonrpc\":\"2.0\",\"id\":\"a\",\"method\":\"ui.state\"}";
        n = webnet_ws_handle(&w, other, (int)strlen(other), m, sizeof(m));
        h.assign(m, n);
        CHECK(n > 0 && h.find("-32601") != std::string::npos && h.find("\"id\":\"a\"") != std::string::npos);
        const char *note = "{\"jsonrpc\":\"2.0\",\"method\":\"hello\"}";
        CHECK(webnet_ws_handle(&w, note, (int)strlen(note), m, sizeof(m)) == 0);   // a notification gets no answer
        n = webnet_ws_handle(&w, "{nope", 5, m, sizeof(m));
        h.assign(m, n);
        CHECK(n > 0 && h.find("\"error\"") != std::string::npos);
        CHECK(webnet_ws_handle(&w, call, (int)strlen(call), m, 20) == -1);             // does not fit
    }
    ui_routes();
    printf("test_webnet: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
