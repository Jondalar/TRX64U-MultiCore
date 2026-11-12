// T186: the core application's FTP client (coreapp/src/ftp_client.cc) against a scripted server model. ftp_client.cc is compiled unchanged
// on the Mac; the lwIP calls it makes are the fakes below (tests/coreapp/ftpstub), the clock is virtual (a receive timeout costs its
// timeout in g_now, nothing real).
//   S1  the data connection goes to the control peer, not to the address in the 227 reply (server behind NAT)
//   S2  a closed/dead control connection: no QUIT on it, no wait, the next operation reconnects
//   S3  a transfer broken off, or whose final 226 comes late: replies stay in step, listing and file intact
#include "check.h"
#include "ftp_client.h"
#include <deque>
#include <string>
#include <vector>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

static uint32_t g_now;                                  // virtual ms

struct Item { uint32_t at; std::string bytes; bool eof; };
struct Conn {
    bool used, data, client_closed, dead, stor;
    int timeout_ms, id;
    long sent;
    uint32_t ip; uint16_t port;
    std::deque<Item> rx;
    std::string cmd;
    Conn() : used(false), data(false), client_closed(false), dead(false), stor(false), timeout_ms(0), id(0), sent(0), ip(0), port(0) {}
};
static Conn g_c[16];
static int g_nconn;
struct Logged { int conn; std::string cmd; bool dead; };
static std::vector<Logged> g_log;
struct Connect { uint32_t ip; uint16_t port; };
static std::vector<Connect> g_connects;

struct Srv {
    std::string pasv;              // "h1,h2,h3,h4,p1,p2" of the 227 reply
    uint32_t final_delay;          // ms after the data before the final 226
    long stor_fail_after;          // data bytes the server takes before the data link breaks (-1: never)
    std::string listing, file;
} g_srv;

static void push(Conn &c, const std::string &s, uint32_t at) { Item i; i.at = at; i.bytes = s; i.eof = false; c.rx.push_back(i); }
static void push_eof(Conn &c, uint32_t at) { Item i; i.at = at; i.eof = true; c.rx.push_back(i); }
static Conn *last_data() { for (int i = g_nconn - 1; i >= 0; i--) if (g_c[i].used && g_c[i].data) return &g_c[i]; return 0; }
static Conn *ctrl_of_last() { for (int i = g_nconn - 1; i >= 0; i--) if (g_c[i].used && !g_c[i].data) return &g_c[i]; return 0; }

static void server_cmd(Conn &c, const std::string &line)
{
    std::string v = line.substr(0, line.find(' '));
    if (v == "USER") push(c, "331 pass\r\n", g_now);
    else if (v == "PASS") push(c, "230 in\r\n", g_now);
    else if (v == "TYPE") push(c, "200 ok\r\n", g_now);
    else if (v == "PWD") push(c, "257 \"/\" is cwd\r\n", g_now);
    else if (v == "CWD") push(c, "250 ok\r\n", g_now);
    else if (v == "PASV") push(c, "227 Entering Passive Mode (" + g_srv.pasv + ")\r\n", g_now);
    else if (v == "QUIT") push(c, "221 bye\r\n", g_now);
    else if (v == "LIST" || v == "RETR") {
        Conn *d = last_data();
        if (!d) { push(c, "425 no data connection\r\n", g_now); return; }
        push(c, "150 here it comes\r\n", g_now);
        push(*d, v == "LIST" ? g_srv.listing : g_srv.file, g_now);
        push_eof(*d, g_now);
        push(c, "226 done\r\n", g_now + g_srv.final_delay);
    } else if (v == "STOR") {
        Conn *d = last_data();
        if (!d) { push(c, "425 no data connection\r\n", g_now); return; }
        d->stor = true;
        push(c, "150 send it\r\n", g_now);
    } else push(c, "500 what\r\n", g_now);
}

int fk_socket(int, int, int) { Conn &c = g_c[g_nconn]; c = Conn(); c.used = true; c.id = g_nconn; return g_nconn++; }
int fk_setsockopt(int fd, int, int opt, const void *v, socklen_t) {
    if (opt == SO_RCVTIMEO) { const struct timeval *tv = (const struct timeval *)v; g_c[fd].timeout_ms = tv->tv_sec * 1000 + tv->tv_usec / 1000; }
    return 0;
}
int lwip_connect(int fd, const struct sockaddr *a, socklen_t) {
    const struct sockaddr_in *in = (const struct sockaddr_in *)a;
    Conn &c = g_c[fd]; c.ip = in->sin_addr.s_addr; c.port = ntohs(in->sin_port);
    if (c.port == 21) push(c, "220 welcome\r\n", g_now);
    else { c.data = true; Connect k = { c.ip, c.port }; g_connects.push_back(k); }
    return 0;
}
long fk_send(int fd, const void *b, size_t n, int) {
    Conn &c = g_c[fd];
    if (c.client_closed) return -1;
    if (c.data) {
        if (g_srv.stor_fail_after >= 0 && c.sent + (long)n > g_srv.stor_fail_after) {
            Conn *k = ctrl_of_last(); if (k && !k->dead) push(*k, "426 data connection broke\r\n", g_now);
            return -1;
        }
        c.sent += n; return (long)n;
    }
    c.cmd.append((const char *)b, n);
    for (;;) {
        size_t e = c.cmd.find("\r\n"); if (e == std::string::npos) break;
        std::string line = c.cmd.substr(0, e); c.cmd.erase(0, e + 2);
        Logged l = { c.id, line, c.dead }; g_log.push_back(l);
        if (!c.dead) server_cmd(c, line);
    }
    return (long)n;
}
long fk_recv(int fd, void *b, size_t n, int) {
    Conn &c = g_c[fd];
    if (c.client_closed) return -1;
    int to = c.timeout_ms ? c.timeout_ms : 10000;
    if (!c.rx.empty()) {
        Item &i = c.rx.front();
        if (i.at <= g_now + to) {
            if (i.at > g_now) g_now = i.at;
            if (i.eof) return 0;
            size_t k = i.bytes.size() < n ? i.bytes.size() : n;
            memcpy(b, i.bytes.data(), k); i.bytes.erase(0, k);
            if (i.bytes.empty()) c.rx.pop_front();
            return (long)k;
        }
    }
    g_now += to; errno = EAGAIN; return -1;
}
int lwip_close(int fd) {
    Conn &c = g_c[fd];
    c.client_closed = true;
    if (c.data && c.stor) { Conn *k = ctrl_of_last(); if (k && !k->dead) push(*k, "226 stored\r\n", g_now + g_srv.final_delay); }
    return 0;
}
int lwip_gethostbyname_r(const char *, struct hostent *h, char *buf, size_t, struct hostent **res, int *err) {
    static char *list[2]; static unsigned char addr[4] = { 192, 168, 1, 20 };
    list[0] = (char *)addr; list[1] = 0;
    memset(h, 0, sizeof(*h)); h->h_addrtype = AF_INET; h->h_length = 4; h->h_addr_list = list;
    (void)buf; *res = h; *err = 0; return 0;
}

static uint32_t ip4(int a, int b, int c, int d) { return (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24); }
static void reset(const char *pasv)
{
    g_nconn = 0; g_log.clear(); g_connects.clear(); g_now = 0;
    g_srv.pasv = pasv; g_srv.final_delay = 0; g_srv.stor_fail_after = -1;
    g_srv.listing = "-rw-r--r--   1 u g   100 Jan 01 12:00 a.prg\r\ndrwxr-xr-x   2 u g  4096 Jan 01 12:00 sub\r\n";
    g_srv.file.clear(); for (int i = 0; i < 6000; i++) g_srv.file += (char)(i * 7 + 3);
}
static int up(FTPClient &cl) { return cl.open("nas.local", 21) == 0 && cl.login("u", "p") == 0 && cl.type_binary() == 0 ? 0 : -1; }
static int quits_on_dead() { int n = 0; for (size_t i = 0; i < g_log.size(); i++) if (g_log[i].cmd == "QUIT" && g_log[i].dead) n++; return n; }
static bool list_ok(FTPClient &cl) { char buf[512]; int n = 0; return cl.list("/", buf, sizeof(buf), &n) == 0 && g_srv.listing == buf && n == (int)g_srv.listing.size(); }
static bool pwd_in_step(FTPClient &cl) { char b[32]; return cl.pwd(b, sizeof(b)) == 0 && cl.last_code() == 257 && !strcmp(b, "/"); }

int main()
{
    // ---- S1: the 227 reply carries the server's private address, the control peer is 192.168.1.20
    {
        reset("10,0,0,5,4,1");
        FTPClient cl;
        CHECK_EQ(up(cl), 0);
        CHECK(list_ok(cl));
        CHECK_EQ(g_connects.size(), 1);
        if (g_connects.size() == 1) {
            CHECK_EQ(g_connects[0].ip, ip4(192, 168, 1, 20));     // not 10.0.0.5
            CHECK_EQ(g_connects[0].port, 1025);                   // the port of the 227 reply is kept
        }
        // RETR takes the same road
        File f; int got = 0;
        CHECK_EQ(cl.retr("x.prg", &f, &got), 0);
        CHECK_EQ(got, 6000);
        CHECK(f.data == g_srv.file);
        CHECK_EQ(g_connects.size(), 2);
        if (g_connects.size() == 2) CHECK_EQ(g_connects[1].ip, ip4(192, 168, 1, 20));
    }
    // ---- S2: the control connection is gone
    {   // the peer vanished without a word: the first command runs into the timeout, nothing is written to the dead link afterwards
        reset("192,168,1,20,4,1");
        FTPClient cl;
        CHECK_EQ(up(cl), 0);
        g_c[0].dead = true;
        uint32_t t0 = g_now;
        CHECK(!cl.is_connected());
        CHECK_EQ(g_now - t0, 10000);                              // the one timeout of finding out
        size_t before = g_log.size();
        CHECK(!cl.is_connected());                                // the link is dropped: answered at once, nothing sent
        CHECK_EQ(g_now - t0, 10000);
        CHECK_EQ(g_log.size(), before);
        CHECK_EQ(cl.cwd("/x"), -1);
        CHECK_EQ(g_log.size(), before);
        CHECK_EQ(quits_on_dead(), 0);
        CHECK_EQ(up(cl), 0);                                      // the next operation reconnects
        CHECK_EQ(g_now - t0, 10000);                              // ... without another wait
        CHECK_EQ(quits_on_dead(), 0);
        CHECK(list_ok(cl));
        CHECK(pwd_in_step(cl));
    }
    {   // open() on a client whose link died unnoticed: no QUIT round trip
        reset("192,168,1,20,4,1");
        FTPClient cl;
        CHECK_EQ(up(cl), 0);
        g_c[0].dead = true;
        uint32_t t0 = g_now;
        CHECK_EQ(up(cl), 0);
        CHECK_EQ(g_now - t0, 0);
        CHECK(list_ok(cl));
    }
    {   // the server closed the connection (FIN): the command fails at once, the link is dropped, no QUIT on it
        reset("192,168,1,20,4,1");
        FTPClient cl;
        CHECK_EQ(up(cl), 0);
        g_c[0].dead = true; push_eof(g_c[0], 0);
        uint32_t t0 = g_now;
        CHECK_EQ(cl.cwd("/x"), -1);
        CHECK_EQ(g_now - t0, 0);
        CHECK(!cl.is_connected());
        CHECK_EQ(up(cl), 0);
        CHECK_EQ(quits_on_dead(), 0);
        CHECK(list_ok(cl));
    }
    {   // a live link is left with a QUIT that is not waited for
        reset("192,168,1,20,4,1");
        FTPClient cl;
        CHECK_EQ(up(cl), 0);
        uint32_t t0 = g_now;
        cl.disconnect();
        CHECK_EQ(g_now - t0, 0);
        CHECK(!cl.is_connected());
    }
    // ---- S3: replies stay in step
    {   // an upload that breaks off: the server's 426 must not become the next command's reply
        reset("192,168,1,20,4,1");
        g_srv.stor_fail_after = 3000;
        FTPClient cl;
        CHECK_EQ(up(cl), 0);
        File f; f.data = g_srv.file;
        CHECK_EQ(cl.stor("up.prg", &f), -1);
        size_t before = g_log.size();
        CHECK(!cl.is_connected());                                // no PWD sent into the pending 426
        CHECK_EQ(g_log.size(), before);
        g_srv.stor_fail_after = -1;
        CHECK_EQ(up(cl), 0);
        CHECK(pwd_in_step(cl));
        CHECK(list_ok(cl));
    }
    {   // an upload whose local file cannot be read: not reported as done
        reset("192,168,1,20,4,1");
        FTPClient cl;
        CHECK_EQ(up(cl), 0);
        File f; f.data = g_srv.file; f.fail_read = true;
        CHECK_EQ(cl.stor("up.prg", &f), -1);
        CHECK(!cl.is_connected());
    }
    {   // the 226 comes 4 s after the data channel closed (inside the 10 s reply timeout): read as the transfer's own reply
        reset("192,168,1,20,4,1");
        g_srv.final_delay = 4000;
        FTPClient cl;
        CHECK_EQ(up(cl), 0);
        CHECK(list_ok(cl));
        CHECK(pwd_in_step(cl));
        File f; int got = 0;
        CHECK_EQ(cl.retr("x.prg", &f, &got), 0);
        CHECK(f.data == g_srv.file);
        CHECK(pwd_in_step(cl));
        File u; u.data = g_srv.file;
        CHECK_EQ(cl.stor("up.prg", &u), 0);
        CHECK(pwd_in_step(cl));
    }
    {   // the 226 comes after the reply timeout: the transfer fails, the link with the late 226 in flight is not used again
        reset("192,168,1,20,4,1");
        g_srv.final_delay = 15000;
        FTPClient cl;
        CHECK_EQ(up(cl), 0);
        char buf[512]; int n = 0;
        CHECK_EQ(cl.list("/", buf, sizeof(buf), &n), -1);
        size_t before = g_log.size();
        CHECK(!cl.is_connected());
        CHECK_EQ(g_log.size(), before);
        g_srv.final_delay = 0;
        CHECK_EQ(up(cl), 0);
        CHECK(list_ok(cl));
        CHECK(pwd_in_step(cl));
        File f; int got = 0;
        CHECK_EQ(cl.retr("x.prg", &f, &got), 0);
        CHECK(f.data == g_srv.file);
    }
    DONE("test_ftp");
}
