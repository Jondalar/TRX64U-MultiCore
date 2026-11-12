// coreapp/src/nettransport.cc, the socket loop both applications' web servers run on (T192.1 Q5): the board file itself, compiled here against a shim of
// FreeRTOS and lwIP (netshim/) in which a task is a thread and a socket is a scripted connection. Covers the client path (a request fed in pieces of at
// most 256 bytes, a bad one, a cut-off one), the slots (503 when full, released after an error, a close, a WebSocket), the WebSocket slots (ws_max),
// the WebSocket loop (text, ping, close, binary, a notification), the response buffer taken under a mutex, and memory that is not there.
#include "nettransport.h"
#include "wsproto.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <new>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <sys/socket.h>

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } else if (getenv("NT_TRACE")) printf("ok %d line %d\n", checks, __LINE__); } while (0)

// ---- memory that can be taken away -----------------------------------------------------------------------------------------------
static std::atomic<size_t> g_fail_size(0);          // a nothrow allocation of exactly this size fails
static std::atomic<bool> g_fail_all(false);
static std::atomic<int> g_allocs(0);                 // nothrow allocations made through NET_ALLOC
extern "C" void *net_test_alloc(size_t n) { g_allocs++; return g_fail_all || (g_fail_size && n == g_fail_size) ? 0 : malloc(n); }  // NET_ALLOC of nettransport.cc

// ---- the shim: tasks, the critical section, the mutex -----------------------------------------------------------------------------
static pthread_mutex_t g_crit = PTHREAD_MUTEX_INITIALIZER;
void netshim_enter(void) { pthread_mutex_lock(&g_crit); }
void netshim_exit(void) { pthread_mutex_unlock(&g_crit); }

struct Start { void (*fn)(void *); void *arg; };
static void *trampoline(void *p)
{
    Start s = *(Start *)p;
    delete (Start *)p;
    s.fn(s.arg);
    return 0;
}
int xTaskCreate(void (*fn)(void *), const char *, unsigned, void *arg, unsigned, void *)
{
    pthread_t t;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    return pthread_create(&t, &a, trampoline, new Start{ fn, arg }) == 0 ? 1 : 0;
}
void vTaskDelete(void *) { pthread_exit(0); }
void vTaskDelay(unsigned ms) { usleep(ms * 1000); }

struct Sem { std::mutex m; std::condition_variable cv; bool taken; };
void *xSemaphoreCreateMutex(void) { Sem *s = new Sem; s->taken = false; return s; }
int xSemaphoreTake(void *h, unsigned ms)
{
    Sem *s = (Sem *)h;
    std::unique_lock<std::mutex> l(s->m);
    if (!s->cv.wait_for(l, std::chrono::milliseconds(ms), [s] { return !s->taken; })) return 0;
    s->taken = true;
    return 1;
}
int xSemaphoreGive(void *h)
{
    Sem *s = (Sem *)h;
    { std::lock_guard<std::mutex> l(s->m); s->taken = false; }
    s->cv.notify_one();
    return 1;
}

// ---- the shim: scripted connections ----------------------------------------------------------------------------------------------
struct Conn {
    std::string in;                  // what the client sends
    std::string later;               // ... and what it sends once the server has answered (frames behind a handshake)
    size_t pos = 0;
    size_t trickle = 1000;           // at most this many bytes per recv
    bool hold = false;               // after the input: idle (EAGAIN) until released, instead of closing
    std::atomic<bool> release{false};
    std::string sent;
    std::atomic<bool> closed{false};
};
static std::mutex g_cm;
static std::condition_variable g_ccv;
static std::deque<Conn *> g_pending;
static std::vector<Conn *> g_all;       // fd = 100 + index
static std::atomic<size_t> g_max_req(0);

extern "C" {
int shim_socket(int, int, int) { return 3; }
int shim_setsockopt(int, int, int, const void *, socklen_t) { return 0; }
int shim_bind(int, const struct sockaddr *, socklen_t) { return 0; }
int shim_listen(int, int) { return 0; }
int shim_accept(int, struct sockaddr *, socklen_t *)
{
    std::unique_lock<std::mutex> l(g_cm);
    g_ccv.wait(l, [] { return !g_pending.empty(); });
    Conn *c = g_pending.front();
    g_pending.pop_front();
    g_all.push_back(c);
    return 100 + (int)g_all.size() - 1;
}
static Conn *conn_of(int fd) { std::lock_guard<std::mutex> l(g_cm); return g_all[fd - 100]; }
ssize_t shim_recv(int fd, void *buf, size_t n, int)
{
    Conn *c = conn_of(fd);
    if (n > g_max_req) g_max_req = n;
    {
        std::lock_guard<std::mutex> l(g_cm);
        if (c->pos >= c->in.size() && !c->later.empty() && !c->sent.empty()) {
            c->in += c->later;
            c->later.clear();
        }
    }
    if (c->pos < c->in.size()) {
        size_t k = std::min(std::min(n, c->trickle), c->in.size() - c->pos);
        memcpy(buf, c->in.data() + c->pos, k);
        c->pos += k;
        return (ssize_t)k;
    }
    if (c->hold && !c->release) {
        usleep(3000);
        errno = EAGAIN;
        return -1;
    }
    return 0;
}
ssize_t shim_send(int fd, const void *buf, size_t n, int)
{
    Conn *c = conn_of(fd);
    c->sent.append((const char *)buf, n);
    return (ssize_t)n;
}
int shim_close(int fd) { conn_of(fd)->closed = true; return 0; }
}

static Conn *connect(const std::string &in, size_t trickle = 1000, bool hold = false, const std::string &later = "")
{
    Conn *c = new Conn;
    c->in = in;
    c->later = later;
    c->trickle = trickle;
    c->hold = hold;
    { std::lock_guard<std::mutex> l(g_cm); g_pending.push_back(c); }
    g_ccv.notify_one();
    return c;
}

template <class F> static bool wait_for(F f, int ms = 5000)
{
    for (int i = 0; i < ms / 2; i++) {
        if (f()) return true;
        usleep(2000);
    }
    return f();
}
static bool done(Conn *c) { return wait_for([c] { return c->closed.load(); }); }

// ---- the application under the transport -----------------------------------------------------------------------------------------
static std::mutex g_am;
static std::vector<std::string> g_reqs;       // method + path + body of every request served
static std::vector<int> g_bads;               // bad_status of every request served as bad
static std::atomic<int> g_ws_opened(0), g_ws_closed(0);
static std::atomic<bool> g_gate(true);        // /slow waits while this is false
static std::atomic<bool> g_starting(false);   // NetApp::starting(): the start gate
static std::atomic<bool> g_note(false);       // the next WebSocket session has a notification waiting

struct TWs : NetWs {
    bool noted;
    TWs() : noted(g_note.exchange(false)) { }
    int handle(const char *msg, int n, char *out, int cap) { return snprintf(out, cap, "{\"echo\":\"%.*s\"}", n, msg); }
    int next(char *out, int cap)
    {
        if (!noted) return 0;
        noted = false;
        return snprintf(out, cap, "{\"note\":1}");
    }
};
struct TApp : NetApp {
    int serve(const HttpReq &q, char *out, int cap, bool *ws)
    {
        { std::lock_guard<std::mutex> l(g_am); g_reqs.push_back(std::string(q.method) + " " + q.path + " " + std::string(q.body, q.body_len)); }
        if (g_starting && strcmp(q.path, "/slow")) {
            return snprintf(out, cap, "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 5\r\n\r\ngate!");
        }
        if (!strcmp(q.path, "/ws") && q.ws_upgrade) {
            *ws = true;
            return snprintf(out, cap, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n");
        }
        if (!strcmp(q.path, "/slow")) {
            while (!g_gate) usleep(2000);
        }
        if (!strcmp(q.path, "/huge")) return -1;
        if (!strcmp(q.path, "/big")) {
            std::string b(3000, 'x');
            return snprintf(out, cap, "HTTP/1.1 200 OK\r\nContent-Length: 3000\r\n\r\n%s", b.c_str());
        }
        return snprintf(out, cap, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
    }
    int serve_bad(const HttpReq &q, char *out, int cap)
    {
        { std::lock_guard<std::mutex> l(g_am); g_bads.push_back(q.bad_status); }
        return snprintf(out, cap, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
    }
    bool starting() { return g_starting; }
    NetWs *ws_open() { g_ws_opened++; return new TWs; }
    void ws_close(NetWs *w) { g_ws_closed++; delete static_cast<TWs *>(w); }
};
static TApp g_app;

static bool net_up(char *ip, int n) { snprintf(ip, n, "10.0.0.1"); return true; }

// ---- helpers ---------------------------------------------------------------------------------------------------------------------
static std::string get(const char *path) { return std::string("GET ") + path + " HTTP/1.1\r\nHost: t\r\n\r\n"; }
static std::string upgrade(const char *path)
{
    return std::string("GET ") + path + " HTTP/1.1\r\nHost: t\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
}
static std::string frame(int op, const std::string &p)           // a client frame: masked
{
    std::string f;
    f += (char)(0x80 | op);
    f += (char)(0x80 | p.size());
    const char mask[4] = { 1, 2, 3, 4 };
    f.append(mask, 4);
    for (size_t i = 0; i < p.size(); i++) f += (char)(p[i] ^ mask[i & 3]);
    return f;
}
static bool starts(const std::string &s, const char *p) { return s.compare(0, strlen(p), p) == 0; }
static bool has(const std::string &s, const char *p) { return s.find(p) != std::string::npos; }
// the server frames after the HTTP head: "op:payload" each
static std::vector<std::string> frames_of(const std::string &sent)
{
    std::vector<std::string> v;
    size_t p = sent.find("\r\n\r\n");
    p = p == std::string::npos ? sent.size() : p + 4;
    while (p + 2 <= sent.size()) {
        int op = sent[p] & 0x0F;
        size_t len = (unsigned char)sent[p + 1];
        v.push_back(std::to_string(op) + ":" + sent.substr(p + 2, len));
        p += 2 + len;
    }
    return v;
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    NetConfig cfg;
    cfg.port = 80;
    cfg.clients_max = 3;
    cfg.ws_max = 2;
    cfg.out_max = 4096;
    cfg.share_out = true;
    cfg.stack_words = 3200;
    cfg.ws_msg_max = 2000;
    cfg.name = "t";
    nettransport_start(&g_app, cfg, net_up);
    nettransport_start(&g_app, cfg, net_up);              // does nothing twice

    // a request fed in pieces: the reader asks for 256 bytes at most and gets 7
    {
        Conn *c = connect(get("/hello?x=1"), 7);
        CHECK(done(c) && starts(c->sent, "HTTP/1.1 200 OK") && has(c->sent, "\r\n\r\nok"));
        CHECK(g_max_req <= 256 && g_max_req > 0);
        CHECK(g_reqs.size() == 1 && g_reqs[0] == "GET /hello ");
    }
    // a POST with a body over several reads; a response of 3000 bytes in the 4096 buffer
    {
        std::string body(900, 'b');
        Conn *c = connect("POST /big HTTP/1.1\r\nHost: t\r\nContent-Length: 900\r\n\r\n" + body, 100);
        CHECK(done(c) && starts(c->sent, "HTTP/1.1 200 OK") && c->sent.size() > 3000);
        CHECK(g_reqs.back() == "POST /big " + body);
    }
    // an answer that does not fit: 507, no ws
    {
        Conn *c = connect(get("/huge"));
        CHECK(done(c) && starts(c->sent, "HTTP/1.1 507"));
    }
    // a request that is bad goes to serve_bad; a cut-off one is not served at all
    {
        Conn *c = connect("NONSENSE\r\n\r\n");
        CHECK(done(c) && starts(c->sent, "HTTP/1.1 400") && g_bads.size() == 1 && g_bads[0] == 400);
        c = connect("GET /x HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n");
        CHECK(done(c) && g_bads.size() == 2 && g_bads[1] == 501);
        size_t before = g_reqs.size();
        c = connect("GET /half HT");
        CHECK(done(c) && c->sent.empty() && g_reqs.size() == before);
        c = connect("");
        CHECK(done(c) && c->sent.empty() && g_reqs.size() == before);
    }
    // slots are released after errors and closes: far more connections than slots, one after the other
    for (int i = 0; i < 12; i++) {
        Conn *c = connect(i % 3 == 0 ? std::string("NONSENSE\r\n\r\n") : i % 3 == 1 ? get("/n") : std::string("GET /cut HT"));
        CHECK(done(c));
        if (i % 3 == 1) CHECK(starts(c->sent, "HTTP/1.1 200"));
    }
    // the WebSocket loop: the 101, a text message echoed, a ping answered, a notification, a close echoed; ws_close once
    {
        g_note = true;
        int o = g_ws_opened, cl = g_ws_closed;
        Conn *c = connect(upgrade("/ws"), 5, true, frame(WS_TEXT, "hi") + frame(WS_PING, "pp") + frame(WS_CLOSE, std::string("\x03\xE8", 2)));
        CHECK(done(c) && starts(c->sent, "HTTP/1.1 101"));
        std::vector<std::string> f = frames_of(c->sent);
        CHECK(f.size() == 4);
        if (f.size() == 4) {
            // the notification may come before or after the first reads; the three answers are in order
            bool n0 = f[0] == "1:{\"note\":1}";
            CHECK(n0 || f[1] == "1:{\"note\":1}");
            std::vector<std::string> r;
            for (size_t i = 0; i < f.size(); i++) if (f[i] != "1:{\"note\":1}") r.push_back(f[i]);
            CHECK(r.size() == 3 && r[0] == "1:{\"echo\":\"hi\"}" && r[1] == "10:pp" && r[2] == std::string("8:") + "\x03\xE8");
        }
        CHECK(g_ws_opened == o + 1 && wait_for([cl] { return g_ws_closed == cl + 1; }));
        // a binary message is not supported: closed with 1003
        c = connect(upgrade("/ws"), 1000, true, frame(WS_BINARY, "zz"));
        CHECK(done(c));
        f = frames_of(c->sent);
        CHECK(!f.empty() && f.back() == std::string("8:") + "\x03\xEB");
        // the peer goes away without a close frame
        c = connect(upgrade("/ws"), 1000, false, frame(WS_TEXT, "a"));
        CHECK(done(c) && g_ws_opened == g_ws_closed);
    }
    // the slots: two WebSocket clients (ws_max) and one slow request fill clients_max; the next connection is told 503 and closed;
    // a third WebSocket client is told 503 by the transport and never reaches ws_open
    {
        Conn *w1 = connect(upgrade("/ws"), 1000, true), *w2 = connect(upgrade("/ws"), 1000, true);
        CHECK(wait_for([] { return g_ws_opened - g_ws_closed == 2; }));
        int o = g_ws_opened;
        Conn *w3 = connect(upgrade("/ws"), 1000, true);
        CHECK(done(w3) && starts(w3->sent, "HTTP/1.1 503") && g_ws_opened == o);
        Conn *p = connect(get("/n"));                             // the third slot stays free for a plain request
        CHECK(done(p) && starts(p->sent, "HTTP/1.1 200"));
        g_gate = false;
        Conn *slow = connect(get("/slow"));
        CHECK(wait_for([] { return g_reqs.back() == "GET /slow "; }));
        Conn *full = connect(get("/n"));
        CHECK(done(full) && starts(full->sent, "HTTP/1.1 503") && full->sent.find("ok") == std::string::npos);
        g_gate = true;
        CHECK(done(slow) && starts(slow->sent, "HTTP/1.1 200"));
        w1->release = true;
        w2->release = true;
        CHECK(done(w1) && done(w2) && wait_for([] { return g_ws_opened == g_ws_closed; }));
        // everything is free again: two WebSocket clients and a plain request are served
        Conn *a = connect(upgrade("/ws"), 1000, true), *b = connect(upgrade("/ws"), 1000, true);
        CHECK(wait_for([] { return g_ws_opened - g_ws_closed == 2; }));
        Conn *q = connect(get("/n"));
        CHECK(done(q) && starts(q->sent, "HTTP/1.1 200"));
        a->release = true;
        b->release = true;
        CHECK(done(a) && done(b));
    }
    // the response buffer is one for all clients: requests at the same moment are all answered (they take turns)
    {
        std::vector<Conn *> v;
        for (int i = 0; i < 3; i++) v.push_back(connect(get("/big"), 50));
        bool all = true;
        for (size_t i = 0; i < v.size(); i++) all = all && done(v[i]) && starts(v[i]->sent, "HTTP/1.1 200") && v[i]->sent.size() > 3000;
        CHECK(all);
    }
    // memory that is not there
    {
        wait_for([] { return g_ws_opened == g_ws_closed; });
        g_fail_all = true;                                       // not even the request record: 503, closed
        Conn *c = connect(get("/n"));
        CHECK(done(c) && starts(c->sent, "HTTP/1.1 503"));
        g_fail_all = false;
        g_fail_size = 4096;                                      // the response buffer
        c = connect(get("/n"));
        CHECK(done(c) && starts(c->sent, "HTTP/1.1 503"));
        g_fail_size = 2000;                                      // a WebSocket message buffer: the connection closes, ws_close still runs
        int o = g_ws_opened;
        c = connect(upgrade("/ws"), 1000, true, frame(WS_TEXT, "hi"));
        CHECK(done(c) && starts(c->sent, "HTTP/1.1 101") && frames_of(c->sent).empty());
        CHECK(g_ws_opened == o + 1 && wait_for([] { return g_ws_opened == g_ws_closed; }));
        g_fail_size = 0;
        c = connect(get("/n"));                                  // and the slots were given back
        CHECK(done(c) && starts(c->sent, "HTTP/1.1 200"));
        Conn *a = connect(upgrade("/ws"), 1000, true), *b = connect(upgrade("/ws"), 1000, true);
        CHECK(wait_for([] { return g_ws_opened - g_ws_closed == 2; }));
        a->release = true;
        b->release = true;
        CHECK(done(a) && done(b));
    }
    // the start gate (T192.3 section 5): no allocation at all, even with every allocation failing; one client at a time, the others refused
    {
        wait_for([] { return g_ws_opened == g_ws_closed; });
        g_starting = true;
        g_fail_all = true;
        int a0 = g_allocs;
        Conn *c = connect(get("/v1/info"));
        CHECK(done(c) && starts(c->sent, "HTTP/1.1 503") && has(c->sent, "gate!"));
        c = connect(upgrade("/ws"), 1000, true);                 // an upgrade is refused the same way, the connection ends
        CHECK(done(c) && has(c->sent, "gate!") && !has(c->sent, "101"));
        g_fail_all = false;
        CHECK(g_allocs == a0);                                   // not one NET_ALLOC call: neither the request record nor a response buffer
        // a second connection while the first is still being read: refused at once, empty 503
        size_t n0 = g_reqs.size();
        g_gate = false;
        Conn *slow = connect(get("/slow"));                      // its answer waits in serve
        CHECK(wait_for([n0] { std::lock_guard<std::mutex> l(g_am); return g_reqs.size() > n0; }));
        Conn *x = connect(get("/v1/info"));
        CHECK(done(x) && starts(x->sent, "HTTP/1.1 503") && !has(x->sent, "gate!"));
        g_gate = true;
        CHECK(done(slow) && starts(slow->sent, "HTTP/1.1 200"));
        g_starting = false;
        c = connect(get("/n"));                                  // gate off: back to the normal path
        CHECK(done(c) && starts(c->sent, "HTTP/1.1 200"));
        CHECK(g_allocs > a0);
    }
    printf("test_nettransport: %d checks, %d failed\n", checks, fails);
    fflush(stdout);
    _exit(fails ? 1 : 0);                                    // threads still wait in accept; static destructors would hang on them (Linux)
}
