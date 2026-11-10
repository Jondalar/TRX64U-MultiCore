#include "webui.h"
#include "videomode.h"
#include "hostvideo.h"
#include "bugreport.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---- the mailbox (T106 D2) ---------------------------------------------------------------------
// The UI task writes the snapshot and takes commands; the web tasks read the snapshot and post commands. Every access to the shared state below is in a
// critical section (the longest is the copy of the snapshot, about 4.5 KB).
#ifndef WEBUI_NO_BOARD
#include "FreeRTOS.h"
#include "task.h"
#define WEB_LOCK() portENTER_CRITICAL()
#define WEB_UNLOCK() portEXIT_CRITICAL()
#else
#define WEB_LOCK() do { } while (0)
#define WEB_UNLOCK() do { } while (0)
#endif

static WebSnapshot g_snap;
static volatile bool g_snap_valid;
static WebCmd g_cmd;
static volatile bool g_cmd_waiting;
static char g_note[96];

void web_publish(const WebSnapshot *s)
{
    WEB_LOCK();
    g_snap = *s;
    g_snap_valid = true;
    WEB_UNLOCK();
}

bool web_take(WebCmd *out)
{
    WEB_LOCK();
    bool had = g_cmd_waiting;
    if (had) {
        *out = g_cmd;
        g_cmd_waiting = false;
    }
    WEB_UNLOCK();
    return had;
}

void web_result(const char *text)
{
    WEB_LOCK();
    snprintf(g_note, sizeof(g_note), "%s", text ? text : "");
    WEB_UNLOCK();
}

const char *web_note(void) { return g_note; }

void web_queue_for_test(const WebCmd *c)
{
    web_post(c);
}

bool web_post(const WebCmd *c)
{
    WEB_LOCK();
    bool free_slot = !g_cmd_waiting;
    if (free_slot) {
        g_cmd = *c;
        g_cmd_waiting = true;
    }
    WEB_UNLOCK();
    return free_slot;
}

void web_copy(WebSnapshot *s, char *note, int notelen)
{
    WEB_LOCK();
    *s = g_snap;
    snprintf(note, notelen, "%s", g_note);
    WEB_UNLOCK();
}

bool web_has_net(char *ip, int len)
{
    WEB_LOCK();
    bool up = g_snap_valid && g_snap.net;
    if (up) {
        snprintf(ip, len, "%s", g_snap.ip);
    }
    WEB_UNLOCK();
    return up;
}

// ---- small helpers -------------------------------------------------------------------------------
#define EMIT(...) do { int w_ = snprintf(out + n, max - n, __VA_ARGS__); \
                       if (w_ < 0 || w_ >= max - n) { out[n] = 0; return n; } n += w_; } while (0)

// HTML-escapes a bundle name - it is the user's text, not ours. Four rotating buffers, because two
// escaped strings in one printf would otherwise be the same buffer twice.
static const char *esc(const char *s)
{
    static char buf4[4][200];
    static int turn;
    char *buf = buf4[turn++ & 3];
    const int max = (int)sizeof(buf4[0]);
    int n = 0;
    for (; s && *s && n < max - 7; s++) {
        switch (*s) {
        case '<': memcpy(buf + n, "&lt;", 4); n += 4; break;
        case '>': memcpy(buf + n, "&gt;", 4); n += 4; break;
        case '&': memcpy(buf + n, "&amp;", 5); n += 5; break;
        case '"': memcpy(buf + n, "&quot;", 6); n += 6; break;
        default: buf[n++] = *s; break;
        }
    }
    buf[n] = 0;
    return buf;
}

// One field out of "a=1&b=2". Returns false when the key is not there.
static bool field(const char *body, const char *key, char *out, int max)
{
    int kl = (int)strlen(key);
    for (const char *p = body; p && *p; ) {
        const char *amp = strchr(p, '&');
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            const char *v = p + kl + 1;
            int len = (int)((amp ? amp : v + strlen(v)) - v);
            if (len > max - 1) {
                len = max - 1;
            }
            // percent decoding, enough for a path: %XX and +
            int o = 0;
            for (int i = 0; i < len && o < max - 1; i++) {
                if (v[i] == '%' && i + 2 < len) {
                    char h[3] = { v[i + 1], v[i + 2], 0 };
                    out[o++] = (char)strtol(h, 0, 16);
                    i += 2;
                } else {
                    out[o++] = (v[i] == '+') ? ' ' : v[i];
                }
            }
            out[o] = 0;
            return true;
        }
        if (!amp) {
            break;
        }
        p = amp + 1;
    }
    return false;
}

bool web_form_field(const char *body, const char *key, char *out, int max) { return field(body, key, out, max); }

static int field_int(const char *body, const char *key, int def)
{
    char v[24];
    return field(body, key, v, sizeof(v)) ? atoi(v) : def;
}

// ---- the page ------------------------------------------------------------------------------------
int web_page(const WebSnapshot *s, const char *note, char *out, int max)
{
    int n = 0;
    EMIT("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
         "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
         "<title>TRX64 host</title><style>"
         "body{background:#070a12;color:#f4f2fb;font:15px system-ui,sans-serif;margin:0;padding:16px}"
         "h1{font-size:19px;margin:0 0 4px}h2{font-size:13px;color:#9da6bd;margin:20px 0 6px;"
         "text-transform:uppercase;letter-spacing:.08em}"
         "a{color:#8376ff}.p{background:#0d1220;border:1px solid #273047;border-radius:4px;padding:10px;margin:8px 0}"
         ".r{display:flex;gap:8px;align-items:center;padding:6px 0;border-bottom:1px solid #273047}"
         ".r:last-child{border:0}.g{color:#9da6bd;font-size:13px}.k{color:#6de7ff}"
         "button{background:#8376ff;color:#070a12;border:0;border-radius:3px;padding:5px 10px;font:inherit}"
         "button.s{background:#111827;color:#f4f2fb;border:1px solid #273047}"
         "select,input{background:#111827;color:#f4f2fb;border:1px solid #273047;border-radius:3px;padding:4px;font:inherit}"
         "</style></head><body>");
    EMIT("<h1>TRX64 <span class=\"g\">host</span></h1>");
    EMIT("<div class=\"g\">app %s &middot; host %s &middot; %s</div>",
         esc(s->app), s->host_key, s->net ? s->ip : "no network");
    if (note && *note) {
        EMIT("<div class=\"p\"><b>%s</b></div>", esc(note));
    }
    if (s->q_text[0]) {                                       // a question this page raised (D19a): nothing runs before Yes
        EMIT("<div class=\"p\"><b>%s</b><div class=\"r\">", esc(s->q_text));
        EMIT("<form method=\"post\" action=\"/v1/answer\"><input type=\"hidden\" name=\"q\" value=\"%s\">"
             "<input type=\"hidden\" name=\"choice\" value=\"yes\"><button>Yes</button></form>", esc(s->q_id));
        EMIT("<form method=\"post\" action=\"/v1/answer\"><input type=\"hidden\" name=\"q\" value=\"%s\">"
             "<input type=\"hidden\" name=\"choice\" value=\"no\"><button class=\"s\">No</button></form></div></div>", esc(s->q_id));
    }

    EMIT("<h2>Boot menu</h2><div class=\"p\">");
    if (!s->n) {
        EMIT("<div class=\"g\">empty &mdash; nothing starts by itself</div>");
    }
    for (int i = 0; i < s->n; i++) {
        EMIT("<div class=\"r\"><div style=\"flex:1\"><b>%s</b><div class=\"g\">%s &middot; %s</div></div>",
             esc(s->name[i]), esc(s->path[i]), s->state[i]);
        if (s->startable[i]) {
            EMIT("<form method=\"post\" action=\"/v1/boot\"><input type=\"hidden\" name=\"index\" value=\"%d\">"
                 "<button>start</button></form>", i);
        }
        EMIT("<form method=\"post\" action=\"/v1/menu/remove\"><input type=\"hidden\" name=\"index\" value=\"%d\">"
             "<button class=\"s\">remove</button></form></div>", i);
    }
    EMIT("<form method=\"post\" action=\"/v1/countdown\" class=\"r\"><span class=\"g\">countdown</span>"
         "<input name=\"seconds\" value=\"%d\" size=\"3\"><button class=\"s\">set</button></form>", s->countdown);
    EMIT("</div>");

    EMIT("<h2>Picture</h2><div class=\"p\"><form method=\"post\" action=\"/v1/video\" class=\"r\">");
    EMIT("<select name=\"hdmi\"><option value=\"-1\"%s>from the screen</option>", s->vid_def < 0 ? " selected" : "");
    int hl[VID_HDMI_COUNT];
    int hn = hv_list((uint16_t)s->vid_mask, s->vid_sys, hl);
    for (int k = 0; k < hn; k++) {
        EMIT("<option value=\"%d\"%s>%s</option>", hl[k], hl[k] == s->vid_def ? " selected" : "", video_hdmi_name(hl[k]));
    }
    EMIT("</select><button>apply</button></form>"
         "<div class=\"g\">HDMI only &mdash; only the modes the screen names are listed. Now: %s. The choice is kept in "
         "Flash:/trx/host.cfg; a change goes back after 8 s unless a key is pressed on the host.</div></div>",
         s->vid_hdmi < 0 ? "?" : video_hdmi_name(s->vid_hdmi));

    EMIT("<h2>Media</h2><div class=\"p\"><div class=\"r\"><span class=\"k\">%s</span><span class=\"k\">%s</span>"
         "<span style=\"flex:1\" class=\"g\">%s</span></div></div>",
         s->usb ? "USB" : "", s->sd ? "SD" : "", esc(s->media));

    EMIT("<h2>Also</h2><div class=\"p\" class=\"g\"><div class=\"g\">FTP is open on port 21 for files. "
         "The same facts as JSON: <a href=\"/v1/status\">/v1/status</a>.</div></div>");
    EMIT("</body></html>");
    return n;
}

// ---- the JSON ------------------------------------------------------------------------------------
static const char *jesc(const char *s)
{
    static char buf4[4][200];
    static int turn;
    char *buf = buf4[turn++ & 3];
    const int max = (int)sizeof(buf4[0]);
    int n = 0;
    for (; s && *s && n < max - 3; s++) {
        if (*s == '"' || *s == '\\') {
            buf[n++] = '\\';
            buf[n++] = *s;
        } else if ((unsigned char)*s >= 0x20) {
            buf[n++] = *s;
        }
    }
    buf[n] = 0;
    return buf;
}

int web_json(const WebSnapshot *s, const char *note, char *out, int max)
{
    int n = 0;
    EMIT("{\"app\":\"%s\",\"host_key\":\"%s\",\"network\":%s,\"ip\":\"%s\",",
         jesc(s->app), s->host_key, s->net ? "true" : "false", s->ip);
    EMIT("\"video\":{\"system\":%d,\"hdmi\":%d,\"system_name\":\"%s\",\"hdmi_name\":\"%s\",\"default\":%d,\"offered\":[",
         s->vid_sys, s->vid_hdmi, video_sys_name(s->vid_sys), video_hdmi_name(s->vid_hdmi), s->vid_def);
    {
        int hl[VID_HDMI_COUNT];
        int hn = hv_list((uint16_t)s->vid_mask, s->vid_sys, hl);
        for (int k = 0; k < hn; k++) {
            EMIT("%s%d", k ? "," : "", hl[k]);
        }
    }
    EMIT("]},");
    EMIT("\"media\":{\"usb\":%s,\"sd\":%s,\"text\":\"%s\"},",
         s->usb ? "true" : "false", s->sd ? "true" : "false", jesc(s->media));
    EMIT("\"countdown\":%d,\"default\":%d,\"note\":\"%s\",\"menu\":[", s->countdown, s->def,
         jesc(note ? note : ""));
    for (int i = 0; i < s->n; i++) {
        EMIT("%s{\"name\":\"%s\"", i ? "," : "", jesc(s->name[i]));
        EMIT(",\"path\":\"%s\",\"state\":\"%s\",\"startable\":%s}",
             jesc(s->path[i]), s->state[i], s->startable[i] ? "true" : "false");
    }
    EMIT("]");
    if (s->q_text[0]) {
        EMIT(",\"question\":{\"id\":\"%s\",\"text\":\"%s\"}", jesc(s->q_id), jesc(s->q_text));
    }
    EMIT("}");
    return n;
}

// ---- T162: GET /v1/report ------------------------------------------------------------------------
WebGet web_get_route(const char *uri)
{
    if (!uri) {
        return WEBGET_NONE;
    }
    const char *q = strchr(uri, '?');
    size_t pl = q ? (size_t)(q - uri) : strlen(uri);
    if (pl == 10 && !strncmp(uri, "/v1/status", 10)) {
        return WEBGET_STATUS;
    }
    if (pl == 10 && !strncmp(uri, "/v1/report", 10)) {
        return WEBGET_REPORT;
    }
    return WEBGET_NONE;
}

int web_report_head(uint32_t fattime, bool time_valid, char *out, int max)
{
    char stamp[BR_STAMP_LEN];
    br_stamp(stamp, fattime, time_valid);
    return snprintf(out, max, "Content-Type: text/plain; charset=utf-8\r\nContent-Disposition: attachment; filename=\"%s.txt\"\r\n", stamp);
}

// ---- the routes ----------------------------------------------------------------------------------
bool web_parse(const char *uri, const char *body, WebCmd *cmd, const WebSnapshot *s, int *status)
{
    memset(cmd, 0, sizeof(*cmd));
    *status = 404;
    if (!uri) {
        return false;
    }
    const char *q = strchr(uri, '?');
    char path[96];
    int pl = (int)(q ? (size_t)(q - uri) : strlen(uri));
    if (pl > (int)sizeof(path) - 1) {
        pl = (int)sizeof(path) - 1;
    }
    memcpy(path, uri, pl);
    path[pl] = 0;
    if (!body || !*body) {
        body = q ? q + 1 : "";
    }

    if (!strcmp(path, "/v1/video")) {
        int hdmi = field_int(body, "hdmi", -2);
        if (hdmi < -1 || hdmi >= VID_HDMI_COUNT) {
            *status = 400;
            return false;
        }
        cmd->kind = WEB_VIDEO;
        cmd->a = 0;
        cmd->b = hdmi;
        *status = 202;
        return true;
    }
    if (!strcmp(path, "/v1/boot")) {
        int i = field_int(body, "index", -1);
        if (i < 0 || i >= s->n) {
            *status = 400;
            return false;
        }
        if (!s->startable[i]) {
            *status = 409;                   // it is in the menu but cannot be started
            return false;
        }
        cmd->kind = WEB_BOOT;
        cmd->a = i;
        *status = 202;
        return true;
    }
    if (!strcmp(path, "/v1/menu/remove") || !strcmp(path, "/v1/default")) {
        int i = field_int(body, "index", -1);
        if (i < 0 || i >= s->n) {
            *status = 400;
            return false;
        }
        cmd->kind = path[4] == 'd' ? WEB_DEFAULT : WEB_REMOVE;
        cmd->a = i;
        *status = 202;
        return true;
    }
    if (!strcmp(path, "/v1/countdown")) {
        int sec = field_int(body, "seconds", -1);
        if (sec < 0 || sec > 60) {
            *status = 400;
            return false;
        }
        cmd->kind = WEB_COUNTDOWN;
        cmd->a = sec;
        *status = 202;
        return true;
    }
    return false;
}

#ifndef WEBUI_NO_BOARD
#include "webnet.h"
#include "nettransport.h"
#include "netalloc.h"

namespace {

bool g_up;

class HostWs : public NetWs {            // no RTTI in the image: the kind is a field
public:
    bool ui;
    explicit HostWs(bool u) : ui(u) { }
};

class WebWs : public HostWs {
public:
    WebWs() : HostWs(false) { }
    WebnetWs w;
    int handle(const char *msg, int n, char *out, int cap) { return webnet_ws_handle(&w, msg, n, out, cap); }
    int next(char *out, int cap) { return webnet_ws_next(&w, out, cap); }
};

// The host application's side of the shared transport (coreapp/src/nettransport.h): the answers are webnet.cc's.
// The UI socket /v1/ui/ws: a client session of the service (webnet_ui_*).
class UiWsConn : public HostWs {
public:
    UicClient *c;
    UiWsConn() : HostWs(true), c(0) { }
    int handle(const char *msg, int n, char *out, int cap) { return webnet_ui_handle(c, msg, n, out, cap); }
    int next(char *out, int cap) { return webnet_ui_next(c, out, cap); }
    bool done() { return webnet_ui_done(c); }
};

class HostNet : public NetApp {
public:
    int serve(const HttpReq &q, char *out, int cap, bool *ws) { return webnet_serve(q, out, cap, ws); }
    int serve_bad(const HttpReq &q, char *out, int cap) { return webnet_serve_bad(q, out, cap); }
    NetWs *ws_open_path(const char *path)
    {
        if (!strcmp(path, "/v1/ui/ws")) {
            UiWsConn *w = net_new<UiWsConn>();
            if (!w) {
                return 0;
            }
            w->c = webnet_ui_open();
            if (!w->c) {
                net_delete(w);
                return 0;
            }
            return w;
        }
        return ws_open();
    }
    NetWs *ws_open()
    {
        WebWs *w = net_new<WebWs>();
        if (w) {
            webnet_ws_open(&w->w);
        }
        return w;
    }
    void ws_close(NetWs *w)
    {
        if (static_cast<HostWs *>(w)->ui) {
            UiWsConn *u = static_cast<UiWsConn *>(w);
            webnet_ui_close(u->c);
            net_delete(u);
        } else {
            net_delete(static_cast<WebWs *>(w));
        }
    }
};
HostNet g_net;

// D6: no address, nothing to serve
bool net_up(char *ip, int len)
{
    return web_has_net(ip, len);
}

}  // namespace

void web_start(void)
{
    if (g_up) {
        return;
    }
    g_up = true;
    NetConfig cfg;
    cfg.port = WEB_PORT;
    cfg.clients_max = 4;                     // a browser opens several connections at once
    cfg.ws_max = 2;                          // the other two slots stay for plain requests
    cfg.out_max = 28672;                     // the bug report (HOST_REPORT_MAX) goes out in one piece with its headers
    cfg.share_out = true;                    // ... in one buffer for all clients
    cfg.stack_words = 2 * configMINIMAL_STACK_SIZE;
    cfg.ws_msg_max = 12288;                  // the UI service's snapshot goes out in one message
    cfg.name = "web";
    nettransport_start(&g_net, cfg, net_up);
}
#endif
