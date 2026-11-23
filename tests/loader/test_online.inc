// T239.3 L-3: the online search as a sub-app (apps/online), included by test_libload.cc before main. What is real: apps/online's sources, compiled for the host (the entry under another name,
// -DONLINE_ENTRY_NAME), loaded by the real loader from a file that carries the menu record the real tool compiled from apps/online/app.toml and the export list the Makefile gives it;
// the registry, the loader's table of api->net (add_services) and the account of the image. What is a model: the network (a script of canned HTTP answers behind SvcNet) and the heap.
// What a RISC-V instruction does with the image is the board's.
#include "online_impl.h"
#include "online_http.h"
#include "online_json.h"
#include <deque>

extern "C" int online_lib_register(const LibApi *api, LibRecord *out);

// ---------------------------------------------------------------------------------------------------------------------- the model of the network

struct ONet {
    std::vector<std::string> hosts, reqs;
    std::deque<std::string> replies;            // the whole answer of each connection to come
    std::string cur;
    size_t pos = 0, step = 512;
    int quiet_every = 0, calls = 0;             // every n-th recv says "nothing waiting"
    bool refuse = false, hang = false;          // hang: after the data nothing comes and the connection stays open
    int opened = 0, closed = 0;
};
static ONet g_on;

static int on_connect(const char *host, uint16_t port, uint32_t)
{
    g_on.hosts.push_back(std::string(host) + ":" + std::to_string(port));
    if (g_on.refuse || g_on.replies.empty()) {
        return -3;
    }
    g_on.cur = g_on.replies.front();
    g_on.replies.pop_front();
    g_on.pos = 0;
    g_on.opened++;
    return 100 + g_on.opened;
}
static int on_send(int, const void *b, int n)
{
    g_on.reqs.push_back(std::string((const char *)b, (size_t)n));
    return n;
}
static int on_recv(int, void *b, int n)
{
    if (g_on.quiet_every && ++g_on.calls % g_on.quiet_every == 0) {
        return -5;
    }
    if (g_on.pos >= g_on.cur.size()) {
        return g_on.hang ? -5 : 0;
    }
    size_t k = std::min(std::min(g_on.step, g_on.cur.size() - g_on.pos), (size_t)n);
    memcpy(b, g_on.cur.data() + g_on.pos, k);
    g_on.pos += k;
    return (int)k;
}
static int on_wait(const int *, int, uint32_t) { return 1; }
static int on_close(int) { g_on.closed++; return 0; }
static int on_none1(uint16_t, int) { return -1; }
static int on_none2(int) { return -5; }
static int on_peer(int, char *, int) { return -1; }
static int on_resolve(const char *, char *, int) { return -1; }
static const SvcNet g_onet = { sizeof(SvcNet), on_none1, on_none2, on_recv, on_send, on_close, on_wait, on_peer, on_connect, on_resolve };

// mode 0: Content-Length, 1: chunked, 2: no length, the connection closes at the end
static std::string http(const std::string &body, int mode, int status = 200)
{
    std::string h = "HTTP/1.1 " + std::to_string(status) + (status == 200 ? " OK" : " Not Found") + "\r\nContent-Type: application/json\r\nServer: test\r\n";
    if (mode == 0) {
        return h + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    }
    if (mode == 1) {
        h += "Transfer-Encoding: chunked\r\n\r\n";
        for (size_t i = 0; i < body.size(); i += 97) {
            std::string c = body.substr(i, 97);
            char sz[16];
            snprintf(sz, sizeof(sz), "%zx", c.size());
            h += std::string(sz) + "\r\n" + c + "\r\n";
        }
        return h + "0\r\n\r\n";
    }
    return h + "\r\n" + body;
}

static const char *PRESETS = "[{\"type\":\"category\",\"values\":[{\"aqlKey\":\"games\",\"name\":\"Games\"},{\"aqlKey\":\"demos\",\"name\":\"Demos\"}]},"
                             "{\"type\":\"rating\",\"values\":[{\"aqlKey\":\"5\",\"name\":\"5 stars\"}]}]";
static const char *RESULTS = "[ {\"name\":\"Jumpman Junior\",\"id\":\"237033\",\"category\":0,\"group\":\"Wattenscheider Cracking Service\",\"year\":1989,\"rating\":0},\n"
                             "  {\"name\":\"Jumpman Junior\",\"id\":\"232391\",\"category\":0,\"group\":\"Wattenscheider Cracking Service\",\"year\":1989},\n"
                             "  {\"name\":\"Zak \\\"McKracken\\\" \\u00e9\",\"id\":\"5\",\"category\":7} ]";
static const char *ENTRIES = "{\n  \"contentEntry\" : [ { \"path\" : \"jumpmanjunior-wcs.d64\", \"id\" : 0, \"size\" : 174848 }, { \"path\" : \"readme/info.txt\", \"id\" : 1 } ],\n  \"isContentByItself\" : false\n}";

static std::string binary_body()
{
    std::string b;
    for (int i = 0; i < 300; i++) { b.push_back((char)(i * 7)); }
    return b;
}

static bool starts(const std::string &s, const char *p) { return s.compare(0, strlen(p), p) == 0; }
static uint8_t *on_alloc(uint32_t n) { return (uint8_t *)malloc(n); }

// ---------------------------------------------------------------------------------------------------------------------- the client alone

static void *on_tree_alloc(uint32_t n, void *) { return malloc(n); }
static void *mem_malloc(uint32_t n) { return malloc(n); }
static void mem_free(void *p) { free(p); }
static void *mem_realloc(void *p, uint32_t n) { return realloc(p, n); }
static const SvcMem g_omem = { sizeof(SvcMem), mem_malloc, mem_free, mem_realloc };
static uint32_t g_oclock;
static uint32_t on_ms(void) { g_oclock += 2000; return g_oclock; }              // every look at the clock is two seconds later: a connection that stays quiet is given up
static SvcSys g_osys;

static void online_client()
{
    memset(&g_osys, 0, sizeof(g_osys));
    g_osys.size = sizeof(g_osys);
    g_osys.ms = on_ms;
    HttpEnv env = { &g_onet, &g_omem, &g_osys };
    HttpOut o;
    std::string big;
    for (int i = 0; i < 20000; i++) { big.push_back((char)('a' + i % 26)); }

    for (int mode = 0; mode < 3; mode++) {                        // the three ways a body ends: length, chunks, the close
        g_on = ONet();
        g_on.quiet_every = 3;
        g_on.step = 1000;
        g_on.replies.push_back(http(big, mode));
        CHECK_EQ(http_get(env, "example.org", "/x/y?q=%28a%29", "Ultimate", 1u << 20, &o), HTTP_OK);
        CHECK(o.body && o.len == big.size() && !memcmp(o.body, big.data(), big.size()) && o.status == 200);
        free(o.body);
        CHECK_EQ(g_on.reqs.size(), 1u);
        CHECK(starts(g_on.reqs[0], "GET /x/y?q=%28a%29 HTTP/1.1\r\nAccept-e"));
        CHECK(g_on.reqs[0].find("\r\nHost: example.org\r\n") != std::string::npos && g_on.reqs[0].find("\r\nClient-Id: Ultimate\r\n") != std::string::npos);
        CHECK(g_on.reqs[0].find("\r\nConnection: close\r\n\r\n") + 8 == g_on.reqs[0].size() - 2 || g_on.reqs[0].size() - g_on.reqs[0].rfind("\r\n\r\n") == 4);
        CHECK(g_on.hosts[0] == "example.org:80");
        CHECK_EQ(g_on.closed, 1);
    }
    // a status that is no 2xx; a body longer than the caller takes; an answer that ends early; no answer at all; garbage; a connection that stays quiet; no memory
    g_on = ONet();
    g_on.replies.push_back(http("nope", 0, 404));
    CHECK_EQ(http_get(env, "example.org", "/", "Ultimate", 1000, &o), HTTP_STATUS);
    CHECK(o.status == 404 && o.body == 0);
    g_on.replies.push_back(http(big, 0));
    CHECK_EQ(http_get(env, "example.org", "/", "Ultimate", 1000, &o), HTTP_TOO_BIG);
    g_on.replies.push_back(http(big, 2));
    CHECK_EQ(http_get(env, "example.org", "/", "Ultimate", 1000, &o), HTTP_TOO_BIG);
    g_on.replies.push_back(http(big, 0).substr(0, 5000));
    CHECK_EQ(http_get(env, "example.org", "/", "Ultimate", 1u << 20, &o), HTTP_BROKEN);
    g_on.replies.push_back(http(big, 1).substr(0, 5000));
    CHECK_EQ(http_get(env, "example.org", "/", "Ultimate", 1u << 20, &o), HTTP_BROKEN);
    g_on.replies.push_back("not http at all\r\n\r\n");
    CHECK_EQ(http_get(env, "example.org", "/", "Ultimate", 1u << 20, &o), HTTP_BROKEN);
    g_on.replies.push_back(http(big, 0).substr(0, 5000));
    g_on.hang = true;
    CHECK_EQ(http_get(env, "example.org", "/", "Ultimate", 1u << 20, &o), HTTP_BROKEN);
    g_on.hang = false;
    g_on.refuse = true;
    CHECK_EQ(http_get(env, "example.org", "/", "Ultimate", 1u << 20, &o), HTTP_NO_NET);
    CHECK_EQ(g_on.opened, g_on.closed);                           // every connection that was made was closed, on every road out

    // the JSON reader: nesting, escapes, numbers, and what is no JSON
    {
        std::string t = "{\"a\":[1,-2,3.5e2,true,false,null],\"s\":\"x\\ty\\u00e9\\\"z\\\\\",\"o\":{\"k\":{\"n\":42}},\"e\":[],\"f\":{}}";
        std::vector<char> v(t.begin(), t.end());
        JTree j = json_parse(v.data(), (int)v.size(), on_tree_alloc, 0);
        CHECK(j.root && j.root->type == J_OBJ && j.nodes == 14);
        const JNode *a = json_member(j.root, "a");
        CHECK(a && a->type == J_LIST && a->child && a->child->num == 1 && a->child->next->num == -2 && a->child->next->next->num == 3);
        CHECK(a->child->next->next->next->type == J_BOOL && a->child->next->next->next->num == 1);
        CHECK(a->child->next->next->next->next->num == 0 && a->child->next->next->next->next->next->type == J_NULL);
        CHECK_STR(json_text(j.root, "s"), "x\ty\xE9\"z\\");
        CHECK_EQ(json_int(json_member(json_member(j.root, "o"), "k"), "n", -1), 42);
        CHECK_EQ(json_int(j.root, "missing", 7), 7);
        CHECK_STR(json_text(j.root, "a"), "");
        CHECK(json_member(j.root, "e") && json_member(j.root, "e")->child == 0);
        free(j.pool);
    }
    {
        const char *bad[] = { "", "[1,]", "{\"a\":1,}", "[1 2]", "\"open", "{\"a\" 1}", "[1] x", "[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]", "tru", "{\"a\":\"\\u12\"}" };
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            std::string t = bad[i];
            std::vector<char> v(t.begin(), t.end());
            v.push_back(0);
            JTree j = json_parse(v.data(), (int)t.size(), on_tree_alloc, 0);
            CHECK(j.root == 0);
            free(j.pool);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------- the sub-app in the loader

static void online_subapp()
{
    g_on = ONet();
    // the file as the Makefile packs it: a library of format 3, resident, needs base 3.3, exports "online" major 1, carries the menu record of app.toml
    Img img("online", "O");
    img.needs.push_back(std::make_pair(std::string("base"), U2A3_VERSION(3, 3)));
    img.exp = exp_bytes("online", 1);
    std::vector<uint8_t> file = with_menu(img, read_file("online_menu.bin"));
    // a base before 3.3 refuses it, and nothing stays (a loader made last is the one api->lib->find answers for: this one goes first)
    {
        World old(2);
        old.m.twins["online"] = [](const LibHost::Entry &, const LibApi *api, LibRecord *rec) { return online_lib_register(api, rec); };
        refused("online on base 3.2", old, file, LIB_NEEDS, "online", "base", "3.3");
    }
    World w(U2A_BASE_MINOR);
    w.ld->add_services();                                       // the loader's own api->net (the account of the image) over the model of the network
    w.ld->set_net(&g_onet);
    w.m.twins["online"] = [](const LibHost::Entry &, const LibApi *api, LibRecord *rec) { return online_lib_register(api, rec); };
    std::string heap0 = w.m.shape();
    char err[LIB_ERR_MAX] = "";

    int h = -1;
    CHECK_EQ(w.load(file, &h, err), LIB_OK);
    const LibInfo *info = w.ld->info(h);
    CHECK(info && info->menu);
    if (!info || !info->menu) {
        return;
    }
    w.m.code_addr = info->image_addr + 4;                       // the calls of the test below are made "by the code of the image": its account

    // the operations and state values the form binds to, owned by the sub-app
    static const char *ops[] = { "online.set", "online.library", "online.presets", "online.cycle", "online.clear", "online.search" };
    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
        const OpDef *d = w.reg.find_op(ops[i]);
        CHECK(d && !strcmp(d->owner, "online"));
    }
    CHECK_EQ(w.reg.nops(), 6);
    CHECK_EQ(w.reg.nstates(), 13 + 36);
    for (const char *s : { "online.library", "online.v.name", "online.results", "online.folder", "online.p11.hide" }) {
        CHECK(w.reg.find_state(s) != 0);
    }
    // every operation and state a node of the record names is one the sub-app registers: the record was compiled from the file the sub-app is packed with
    const DescRecord *m = info->menu;
    CHECK_EQ(m->rec.nnodes, 20);
    for (int i = 0; i < m->rec.nnodes; i++) {
        CHECK(m->nodes[i].op && w.reg.find_op(m->nodes[i].op) != 0);
        CHECK(!strncmp(m->nodes[i].anchor, "F3/online", 9));
    }
    // the tab "online" of F3 with its layout and number, as the route's own tab was
    CHECK_EQ(m->rec.ntabs, 1);
    CHECK_STR(m->ctabs[0].area, "F3");
    CHECK_STR(m->ctabs[0].id, "online");
    CHECK_STR(m->ctabs[0].name, "Online");
    CHECK_EQ(m->ctabs[0].widget, TW_NODES);
    CHECK_EQ(m->ctabs[0].n, 3);
    CHECK(LIB_RECORD_COVERS(m->rec.struct_size, tab_layouts) && m->rec.tab_layouts && m->rec.tab_layouts[0]);
    CHECK_STR(m->rec.tab_layouts[0], "split;title=Assembly64 and CommoServe;side=Search");

    // the table the file manager reads "/Online" through: found by name and major, the size of the table
    uint32_t sz = 0;
    const OnlineApi *t = (const OnlineApi *)w.ld->svc_lib()->find(ONLINE_API_NAME, U2A3_VERSION(ONLINE_API_MAJOR, 0), &sz);
    CHECK(t == &online_table && sz == sizeof(OnlineApi));
    CHECK(w.ld->svc_lib()->find(ONLINE_API_NAME, U2A3_VERSION(2, 0), &sz) == 0);
    CHECK(t && !t->has_results());

    // the form: the choices from the server (the answer has a length)
    g_on.replies.push_back(http(PRESETS, 0));
    CHECK_STR(call(w, "online.presets").c_str(), "2 fields to choose from");
    CHECK_EQ(g_on.reqs.size(), 1u);
    CHECK(starts(g_on.reqs[0], "GET /leet/search/aql/presets HTTP/1.1\r\nAccept-encoding"));
    CHECK(g_on.reqs[0].find("\r\nHost: hackerswithstyle.se\r\n") != std::string::npos && g_on.reqs[0].find("\r\nClient-Id: Ultimate\r\n") != std::string::npos);
    CHECK(g_on.hosts[0] == "hackerswithstyle.se:80");
    CHECK_STR(w.reg.state(0, "online.p0.title", 0), "Category");
    CHECK_STR(w.reg.state(0, "online.p0.value", 0), "Any");
    CHECK(w.reg.state(0, "online.p0.hide", 0) == 0);            // shown
    CHECK_STR(w.reg.state(0, "online.p2.hide", 0), "yes");      // no third field
    CHECK(w.reg.state(0, "online.loaded", 0) != 0 && w.reg.state(0, "online.unloaded", 0) == 0);
    {                                                           // a cycle takes the number of the field
        OpIn in = in_int("slot", 0);
        OpResult r;
        CHECK_EQ(w.reg.call(0, "online.cycle", &in, 1, &r), OPE_OK);
        CHECK_STR(r.msg, "category: Games");
        in = in_int("slot", 9);
        CHECK_EQ(w.reg.call(0, "online.cycle", &in, 1, &r), OPE_ARG);
    }
    CHECK_STR(w.reg.state(0, "online.p0.value", 0), "Games");
    {
        OpIn in[2] = { in_str("field", "name"), in_str("text", "  jumpman ") };
        OpResult r;
        CHECK_EQ(w.reg.call(0, "online.set", in, 2, &r), OPE_OK);
        CHECK_STR(r.msg, "name: jumpman");
        CHECK_STR(w.reg.state(0, "online.name", 0), "jumpman");
        in[0] = in_str("field", "colour");
        CHECK_EQ(w.reg.call(0, "online.set", in, 2, &r), OPE_ARG);
    }
    CHECK_STR(call(w, "online.library").c_str(), "CommoServe is searched");
    CHECK_STR(w.reg.state(0, "online.library", 0), "CommoServe");

    // the search: the answer in chunks, the quiet moments of a real connection; the query as assembly_search builds it, the client id of the library chosen
    g_on.replies.push_back(http(RESULTS, 1));
    g_on.quiet_every = 4;
    g_on.step = 120;
    CHECK_STR(call(w, "online.search").c_str(), "3 found");
    CHECK(starts(g_on.reqs[1], "GET /leet/search/aql?query=%28name%3a%22jumpman%22%29%20%26%20%28category%3agames%29 HTTP/1.1\r\nAccept-encoding"));
    CHECK(g_on.reqs[1].find("\r\nClient-Id: Commodore\r\n") != std::string::npos);
    CHECK_STR(w.reg.state(0, "online.results", 0), "3 found");
    CHECK(t->has_results());
    g_on.quiet_every = 0;
    g_on.step = 512;

    // the folder "/Online": the results as folders (a name is unique), a result's files from the server once, a file from the server once
    FsEntry e[8];
    int n = t->list("", e, 8);
    CHECK_EQ(n, 3);
    std::string l1 = "Jumpman Junior (Wattenscheider Cracking Service, 1989)";
    CHECK(n == 3 && e[0].dir && l1 == e[0].name);
    CHECK(n == 3 && e[1].dir && l1.substr(0, 48) + " #2" == e[1].name);
    CHECK(n == 3 && std::string("Zak \"McKracken\" \xE9") == e[2].name);
    uint64_t size = 1;
    bool dir = false;
    CHECK(t->stat("", &size, &dir) && dir);
    CHECK(t->stat(l1.c_str(), &size, &dir) && dir && size == 0);
    size_t before = g_on.reqs.size();
    g_on.replies.push_back(http(ENTRIES, 0));
    n = t->list(l1.c_str(), e, 8);
    CHECK_EQ(n, 2);
    CHECK(n == 2 && std::string("jumpmanjunior-wcs.d64") == e[0].name && e[0].size == 174848 && !e[0].dir);
    CHECK(n == 2 && std::string("info.txt") == e[1].name && e[1].size == 0);
    CHECK_EQ(g_on.reqs.size(), before + 1);
    CHECK(starts(g_on.reqs[before], "GET /leet/search/entries/237033/0 HTTP/1.1\r\nAccept-encodi"));
    CHECK(t->stat((l1 + "/jumpmanjunior-wcs.d64").c_str(), &size, &dir) && !dir && size == 174848);
    CHECK(!t->stat((l1 + "/nothing.bin").c_str(), &size, &dir));
    CHECK(!t->stat("no such result", &size, &dir));
    CHECK_EQ(g_on.reqs.size(), before + 1);                    // the entries were taken once

    g_on.replies.push_back(http(binary_body(), 2));
    uint32_t len = 0;
    uint8_t *b = t->fetch((l1 + "/jumpmanjunior-wcs.d64").c_str(), on_alloc, &len);
    CHECK(b && len == 300 && binary_body() == std::string((const char *)b, len));
    free(b);
    CHECK_EQ(g_on.reqs.size(), before + 2);
    CHECK(starts(g_on.reqs[before + 1], "GET /leet/search/bin/237033/0/0 HTTP/1.1\r\nAccept-enc"));
    b = t->fetch((l1 + "/jumpmanjunior-wcs.d64").c_str(), on_alloc, &len);        // the same file again: kept, no second download
    CHECK(b && len == 300 && binary_body() == std::string((const char *)b, len));
    free(b);
    CHECK_EQ(g_on.reqs.size(), before + 2);
    CHECK(t->fetch(l1.c_str(), on_alloc, &len) == 0);          // a result is no file
    CHECK(t->fetch((l1 + "/nothing.bin").c_str(), on_alloc, &len) == 0);

    // no network: the form says so, the folder says so, nothing is half done
    g_on.refuse = true;
    {
        OpResult r;
        CHECK_EQ(w.reg.call(0, "online.presets", 0, 0, &r), OPE_FAILED);
        CHECK_STR(r.msg, "No answer from the server (network?)");
        CHECK_EQ(w.reg.call(0, "online.search", 0, 0, &r), OPE_FAILED);
        CHECK(std::string(r.msg).find("Connection failed: No answer from the server") == 0);
    }
    CHECK_EQ(t->list("Zak \"McKracken\" \xE9", e, 8), -1);
    CHECK_STR(t->error(), "No answer from the server (network?)");
    g_on.refuse = false;
    // the server answers, but not with a list
    g_on.replies.push_back(http("<html>", 0, 503));
    {
        OpResult r;
        CHECK_EQ(w.reg.call(0, "online.search", 0, 0, &r), OPE_FAILED);
        CHECK(std::string(r.msg).find("The server answered 503") != std::string::npos);
    }
    CHECK_STR(call(w, "online.clear").c_str(), "The form is empty");
    CHECK_STR(w.reg.state(0, "online.name", 0), "Any");

    // every connection made was closed, none is on the account of the image, and the image gives all of its memory back when it goes (S14)
    CHECK_EQ(g_on.opened, g_on.closed);
    CHECK_EQ(w.ld->account_sockets(h), 0);
    CHECK(w.ld->account_bytes(h) > 0);                          // the kept download and the form's blocks are on its account
    w.m.code_addr = 0;
    CHECK_EQ(w.ld->unload(h, err, sizeof(err)), LIB_OK);
    CHECK(w.m.shape() == heap0);
    CHECK(w.reg.find_op("online.search") == 0 && w.reg.find_state("online.results") == 0);
    CHECK(w.ld->svc_lib()->find(ONLINE_API_NAME, U2A3_VERSION(ONLINE_API_MAJOR, 0), &sz) == 0);
}
