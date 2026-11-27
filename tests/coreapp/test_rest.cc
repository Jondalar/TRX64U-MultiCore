// T104.1 D3, S2, Q2, Q8: the transports. One service layer, two thin adapters. REST (rest.cc) and WS JSON-RPC (rpc.cc, wsproto.cc) take an
// operation by the name of the request, check it against the types the contributors registered, and run it through the same call the menu makes -
// so an operation registered after the adapters were written is reachable by both without a line changed in them (the test registers one), and the
// same setting changed through the menu, through REST and through JSON-RPC ends in the same state with the same words. The alias table maps the paths
// of the scripts we have to operations; a path whose operation the core did not register says so (501) and names it.
#include <string>
#include <vector>
#include <algorithm>
#include "check.h"
#include "fakes.h"
#include "test_core.h"
#include "rest.h"
#include "rpc.h"
#include "wsproto.h"
#include "svc.h"
#include "ops_glue.h"
#include "menuctl.h"
#include "keyboard.h"
#include "memops.h"
#include "rpccontract.h"
#include <fstream>
#include <set>

const CoreDesc *const core_table[] = { &testcore::desc, 0 };
#define CONTRACT_LIST GOLDEN_DIR "/trx64_contract_methods.txt"

// ---- operations the adapters know nothing about ---------------------------------------------------------------------------------
static std::string g_fresh;
static int g_fresh_calls;
static int op_fresh(CoreCtx *, const OpArgs &a, OpOut &o)
{
    g_fresh_calls++;
    char b[160];
    snprintf(b, sizeof(b), "mode=%d level=%d path=%s on=%d", (int)a.i(0), (int)a.i(1), a.s(2), (int)a.i(3));
    g_fresh = b;
    snprintf(o.msg, o.msglen, "fresh %s", b);
    return OPE_OK;
}
static int op_refuse(CoreCtx *, const OpArgs &, OpOut &o) { snprintf(o.msg, o.msglen, "I will not"); return OPE_FAILED; }
// P2-1: what a linked library registers under the name of a contract method (rpccontract.cc: "session/state" is a row, "monitor/exec" a -32601 row)
static int g_lib_calls;
static int op_lib_state(CoreCtx *, const OpArgs &, OpOut &o) { g_lib_calls++; snprintf(o.msg, o.msglen, "the library's state"); return OPE_OK; }
static int op_lib_monitor(CoreCtx *, const OpArgs &, OpOut &o) { g_lib_calls++; snprintf(o.msg, o.msglen, "the library's monitor"); return OPE_OK; }
static int op_blob(CoreCtx *, const OpArgs &, OpOut &) { return OPE_OK; }
static std::string g_run_path;
static int op_run(CoreCtx *, const OpArgs &a, OpOut &) { g_run_path = a.s(0); return OPE_OK; }
static const char *const fresh_modes[] = { "alpha", "beta", "gamma" };
static const ArgSpec fresh_args[] = { ARG_ENUM("mode", fresh_modes, 3), ARG_INT("level", -5, 5), ARG_PATH("path"), ARG_BOOL("on") };
static const ArgSpec blob_args[] = { ARG_BLOB("data") };
static const ArgSpec run_args[] = { ARG_PATH("path") };
static const ArgSpec slot_arg[] = { ARG_SLOT("slot") };
static std::string g_state_text = "one";
static const char *st_text(CoreCtx *) { return g_state_text.c_str(); }
static const char *st_none(CoreCtx *) { return 0; }

// what the contract's mapped methods call: a memory, a drive, a PRG start
static std::string g_drive;
static int op_dmount(CoreCtx *, const OpArgs &a, OpOut &) { g_drive = std::string("mount ") + a.s(0) + " " + a.s(1) + " " + std::to_string(a.i(2)); return OPE_OK; }
static int op_deject(CoreCtx *, const OpArgs &a, OpOut &) { g_drive = std::string("eject ") + a.s(0); return OPE_OK; }
static int op_dreset(CoreCtx *, const OpArgs &a, OpOut &) { g_drive = std::string("reset ") + a.s(0); return OPE_OK; }
static int op_drun(CoreCtx *, const OpArgs &a, OpOut &) { g_drive = std::string("run ") + a.s(0) + " " + a.s(1); return OPE_OK; }
static std::string g_load;
static int op_cload(CoreCtx *, const OpArgs &a, OpOut &) { g_load = std::string(a.s(0)) + " " + std::to_string(a.i(1)); return OPE_OK; }
static const char *const dmodes[] = { "rw", "ro", "unlinked" };
static const ArgSpec dmount_args[] = { ARG_SLOT("slot"), ARG_PATH("path"), ARG_ENUM("mode", dmodes, 3) };
static const ArgSpec dslot_args[] = { ARG_SLOT("slot") };
static const ArgSpec drun_args[] = { ARG_SLOT("slot"), ARG_PATH("path") };
static const ArgSpec cload_args[] = { ARG_PATH("path"), ARG_INT("start", 0, 65535) };
static const MemRegion t_regions[] = { { 0, 0x10000, MK_RAM, 0, 0, "RAM" } };
static const MemMap t_mem = { t_regions, 1, false, 0 };

static void extra(OpRegistry *r, bool mem)
{
    r->contributor("zz");
    if (mem) {
        MEM_OPS(*r, t_mem, 0xFFFF);
    }
    r->add_op("drive.mount", dmount_args, 3, op_dmount);
    r->add_op("drive.eject", dslot_args, 1, op_deject);
    r->add_op("drive.reset", dslot_args, 1, op_dreset);
    r->add_op("drive.mount_run", drun_args, 2, op_drun);
    r->add_op("prg.load", cload_args, 2, op_cload);
    r->add_op("zz.fresh", fresh_args, 4, op_fresh);
    r->add_op("zz.refuse", 0, 0, op_refuse);
    r->add_op("zz.blob", blob_args, 1, op_blob);
    r->add_op("prg.run", run_args, 1, op_run);
    r->add_state("zz.text", st_text);
    r->add_state("zz.none", st_none);
    r->add_event("zz.changed", slot_arg);
    r->add_event("zz.ping", 0);
}

struct Rig {
    FakeIo io; FakeFs fs; PxRec px; CoreCtx c; Settings *st; Video *vid;
    Rig(bool mem = true)
    {
        memset(&c, 0, sizeof(c));
        c.desc = &testcore::desc; c.io = &io; c.fs = &fs;
        snprintf(c.core_version, sizeof(c.core_version), "0.7");
        extra(ops_of(&c), mem);
        st = new Settings(testcore::desc.settings, testcore::desc.nsettings, testcore::desc.store, &fs, "/usb0/cores/t.cfg", 0);
        st->load();
        c.settings = st;
        vid = new Video(testcore::desc.video, &px, &io);
        c.video = vid;
    }
    ~Rig() { delete st; delete vid; delete c.ops; }
};

struct Resp {
    int status;
    std::string body, head;
    bool ws;
};

// A request as a client sends it, fed to the parser in small pieces (a socket gives what it has), answered by rest_serve.
static Resp http(Rig &r, const std::string &method, const std::string &target, const std::string &body = "", const std::string &ctype = "",
                 const std::string &extra_headers = "", int cap = 16384)
{
    std::string raw = method + " " + target + " HTTP/1.1\r\nHost: board\r\n" + extra_headers;
    if (!body.empty()) {
        raw += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        if (!ctype.empty()) raw += "Content-Type: " + ctype + "\r\n";
    }
    raw += "\r\n" + body;
    HttpReq *q = new HttpReq;
    http_init(q);
    for (size_t i = 0; i < raw.size() && (q->state == HS_HEAD || q->state == HS_BODY); i += 7) {
        http_feed(q, raw.data() + i, (int)std::min<size_t>(7, raw.size() - i));
    }
    std::vector<char> out(cap);
    Resp rs;
    rs.ws = false;
    int n = q->state == HS_READY ? rest_serve(&r.c, *q, out.data(), cap, &rs.ws) : rest_serve_bad(*q, out.data(), cap);
    delete q;
    rs.status = 0;
    if (n < 0) {
        rs.status = -1;
        return rs;
    }
    std::string all(out.data(), n);
    rs.status = atoi(all.c_str() + 9);
    size_t e = all.find("\r\n\r\n");
    rs.head = all.substr(0, e);
    rs.body = all.substr(e + 4);
    if (!rs.ws) {
        size_t cl = rs.head.find("Content-Length: ");
        CHECK(cl != std::string::npos);
        if (cl != std::string::npos) CHECK_EQ(atoi(rs.head.c_str() + cl + 16), (int)rs.body.size());
    }
    return rs;
}

static bool has(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }
static bool has(const std::string &s, const std::string &part) { return s.find(part) != std::string::npos; }

// A JSON-RPC message and its answer.
static std::string rpc(RpcSession *s, const std::string &msg)
{
    char out[4096];
    int n = rpc_handle(s, msg.data(), (int)msg.size(), out, sizeof(out));
    CHECK(n >= 0);
    return n > 0 ? std::string(out, n) : std::string();
}


static std::string hexs(const uint8_t *d, int n) { std::string s; char b[3]; for (int i = 0; i < n; i++) { snprintf(b, 3, "%02x", d[i]); s += b; } return s; }

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);                                 // a mutant may crash: what it said before has to be out
    // ---- HTTP: the request as the parser sees it
    {
        HttpReq *q = new HttpReq;
        auto feed = [&](const std::string &raw) {
            http_init(q);
            for (size_t i = 0; i < raw.size() && (q->state == HS_HEAD || q->state == HS_BODY); i++) http_feed(q, raw.data() + i, 1);
        };
        feed("GET /v1/state/zz.text?x=1&y=%41 HTTP/1.1\r\nHost: a\r\n\r\n");
        CHECK_EQ(q->state, HS_READY);
        CHECK_STR(q->method, "GET"); CHECK_STR(q->path, "/v1/state/zz.text"); CHECK_STR(q->query, "x=1&y=%41");
        feed("POST /v1/ops/a%20b HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello");
        CHECK_EQ(q->state, HS_READY); CHECK_STR(q->path, "/v1/ops/a b"); CHECK_STR(q->body, "hello");
        feed("POST /x HTTP/1.1\r\nContent-Length: 5\r\n\r\nhel");
        CHECK_EQ(q->state, HS_BODY);                                // waits for the rest
        feed("POST /x HTTP/1.1\r\nContent-Length: 99999\r\n\r\n");
        CHECK_EQ(q->state, HS_BAD); CHECK_EQ(q->bad_status, 413);
        feed("POST /x HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n");
        CHECK_EQ(q->state, HS_BAD); CHECK_EQ(q->bad_status, 501);
        feed("NONSENSE\r\n\r\n");
        CHECK_EQ(q->state, HS_BAD); CHECK_EQ(q->bad_status, 400);
        feed("GET /a%zz HTTP/1.1\r\n\r\n");
        CHECK_EQ(q->state, HS_BAD); CHECK_EQ(q->bad_status, 400);
        feed("GET /a%4 HTTP/1.1\r\n\r\n");
        CHECK_EQ(q->state, HS_BAD);                                 // a percent sign that is cut off
        feed("GET /x HTTP/1.1\r\nContent-Length: abc\r\n\r\n");
        CHECK_EQ(q->state, HS_BAD);
        std::string big = "GET /x HTTP/1.1\r\n";
        for (int i = 0; i < 200; i++) big += "X-Pad-" + std::to_string(i) + ": aaaaaaaaaaaa\r\n";
        feed(big + "\r\n");
        CHECK_EQ(q->state, HS_BAD); CHECK_EQ(q->bad_status, 431);
        delete q;
    }

    // ---- REST: reading
    {
        Rig r;
        Resp i = http(r, "GET", "/v1/info");
        CHECK_EQ(i.status, 200);
        CHECK(has(i.body, "\"core\":\"Test Machine\"") && has(i.body, "\"version\":\"0.7\"") && has(i.body, "\"api\":1"));
        // every registered operation is listed, with its argument types
        OpRegistry *reg = ops_of(&r.c);
        Resp o = http(r, "GET", "/v1/ops");
        CHECK_EQ(o.status, 200);
        CHECK(has(o.body, "\"count\":" + std::to_string(reg->nops())));
        for (int k = 0; k < reg->nops(); k++) CHECK(has(o.body, (std::string("\"name\":\"") + reg->op_at(k)->name + "\"").c_str()));
        CHECK(has(o.body, "\"labels\":[\"alpha\",\"beta\",\"gamma\"]"));
        CHECK(has(o.body, "{\"name\":\"level\",\"type\":\"int\",\"min\":-5,\"max\":5}"));
        CHECK(has(o.body, "{\"name\":\"on\",\"type\":\"bool\"}"));
        CHECK(has(o.body, "{\"name\":\"path\",\"type\":\"path\",\"maxlen\":519}"));
        CHECK(has(o.body, "{\"name\":\"data\",\"type\":\"blob\"}"));
        CHECK(has(o.body, "\"next\":null"));
        // paging: a small buffer cuts the page and says where the next begins; the pages together are the whole list
        std::string seen;
        int from = 0, pages = 0;
        for (; pages < 100; pages++) {
            Resp p = http(r, "GET", "/v1/ops?from=" + std::to_string(from), "", "", "", 1100);
            CHECK_EQ(p.status, 200);
            seen += p.body;
            size_t nx = p.body.rfind("\"next\":");
            CHECK(nx != std::string::npos);
            if (has(p.body.substr(nx), "null")) break;
            int next = atoi(p.body.c_str() + nx + 7);
            CHECK(next > from);
            from = next;
        }
        CHECK(pages > 1);
        for (int k = 0; k < reg->nops(); k++) CHECK(has(seen, (std::string("\"name\":\"") + reg->op_at(k)->name + "\"").c_str()));
        // one operation, an unknown one
        Resp one = http(r, "GET", "/v1/ops/zz.fresh");
        CHECK_EQ(one.status, 200); CHECK(has(one.body, "\"owner\":\"zz\""));
        Resp no = http(r, "GET", "/v1/ops/zz.nothing");
        CHECK_EQ(no.status, 404); CHECK(has(no.body, "\"code\":-2") && has(no.body, "zz.nothing"));
        // state values: the text, null for nothing, the revision counts the changes seen
        Resp s1 = http(r, "GET", "/v1/state/zz.text");
        CHECK_EQ(s1.status, 200); CHECK(has(s1.body, "\"value\":\"one\""));
        std::string rev1 = s1.body.substr(s1.body.find("\"rev\":"));
        g_state_text = "two";
        Resp s2 = http(r, "GET", "/v1/state/zz.text");
        CHECK(has(s2.body, "\"value\":\"two\"")); CHECK(s2.body.substr(s2.body.find("\"rev\":")) != rev1);
        CHECK(has(http(r, "GET", "/v1/state/zz.none").body, "\"value\":null"));
        CHECK_EQ(http(r, "GET", "/v1/state/zz.nope").status, 404);
        Resp sl = http(r, "GET", "/v1/state");
        CHECK_EQ(sl.status, 200); CHECK(has(sl.body, "\"name\":\"zz.text\"") && has(sl.body, "\"name\":\"zz.none\""));
        Resp ev = http(r, "GET", "/v1/events");
        CHECK_EQ(ev.status, 200); CHECK(has(ev.body, "\"name\":\"zz.changed\"") && has(ev.body, "\"name\":\"zz.ping\"") && has(ev.body, "\"payload\":null"));
    }

    // ---- REST: calling, and what S2 asks - an operation the adapters have never heard of
    {
        Rig r;
        g_fresh_calls = 0;
        Resp a = http(r, "POST", "/v1/ops/zz.fresh", "{\"mode\":\"beta\",\"level\":-3,\"path\":\"/usb0/a b.prg\",\"on\":true}", "application/json");
        CHECK_EQ(a.status, 200); CHECK(has(a.body, "\"code\":0"));
        CHECK_STR(g_fresh.c_str(), "mode=1 level=-3 path=/usb0/a b.prg on=1");
        CHECK(has(a.body, "fresh mode=1"));
        // the same by the query and by a form body, the label or the number
        Resp b = http(r, "POST", "/v1/ops/zz.fresh?mode=2&level=4&path=%2Fx&on=false");
        CHECK_EQ(b.status, 200); CHECK_STR(g_fresh.c_str(), "mode=2 level=4 path=/x on=0");
        Resp c = http(r, "POST", "/v1/ops/zz.fresh", "mode=gamma&level=0&path=%2Fy+z&on=1", "application/x-www-form-urlencoded");
        CHECK_EQ(c.status, 200); CHECK_STR(g_fresh.c_str(), "mode=2 level=0 path=/y z on=1");
        CHECK_EQ(g_fresh_calls, 3);
        // refused before the handler runs: the call count does not move
        Resp e1 = http(r, "POST", "/v1/ops/zz.fresh", "{\"mode\":\"delta\",\"level\":0,\"path\":\"/x\",\"on\":true}");
        CHECK_EQ(e1.status, 400); CHECK(has(e1.body, "\"code\":-1"));
        Resp e2 = http(r, "POST", "/v1/ops/zz.fresh", "{\"mode\":0,\"level\":9,\"path\":\"/x\",\"on\":true}");
        CHECK_EQ(e2.status, 400);
        Resp e3 = http(r, "POST", "/v1/ops/zz.fresh", "{\"mode\":0,\"level\":1.5,\"path\":\"/x\",\"on\":true}");
        CHECK_EQ(e3.status, 400);
        Resp e4 = http(r, "POST", "/v1/ops/zz.fresh", "{\"mode\":0,\"level\":1,\"on\":true}");
        CHECK_EQ(e4.status, 400); CHECK(has(e4.body, "path"));        // a missing argument is named
        Resp e5 = http(r, "POST", "/v1/ops/zz.fresh", "{\"mode\":0,\"level\":1,\"path\":\"/x\",\"on\":true,\"extra\":1}");
        CHECK_EQ(e5.status, 400); CHECK(has(e5.body, "extra"));
        Resp e6 = http(r, "POST", "/v1/ops/zz.fresh", "{\"mode\":0,\"level\":1,\"path\":5,\"on\":true}");
        CHECK_EQ(e6.status, 400);
        Resp e7 = http(r, "POST", "/v1/ops/zz.fresh", "{\"mode\":0,\"level\":1,\"path\":\"/x\"");
        CHECK_EQ(e7.status, 400);                                    // JSON that is not
        Resp e8 = http(r, "POST", "/v1/ops/zz.blob", "{\"data\":\"AAAA\"}");
        CHECK_EQ(e8.status, 400); CHECK(has(e8.body, "bytes do not travel over a transport"));
        CHECK_EQ(g_fresh_calls, 3);
        // an operation that ran and refused: 409 with its words
        Resp f = http(r, "POST", "/v1/ops/zz.refuse");
        CHECK_EQ(f.status, 409); CHECK(has(f.body, "\"code\":-7") && has(f.body, "I will not"));
        // an unknown operation, a wrong method
        CHECK_EQ(http(r, "POST", "/v1/ops/zz.nothing").status, 404);
        CHECK_EQ(http(r, "DELETE", "/v1/ops/zz.fresh").status, 405);
        CHECK_EQ(http(r, "POST", "/v1/ops").status, 405);
        CHECK_EQ(http(r, "GET", "/index.html").status, 404);
        CHECK_EQ(http(r, "GET", "/v1/nothing").status, 404);
        // the settings operations of the base
        Resp s = http(r, "POST", "/v1/ops/settings.set", "{\"key\":\"sound\",\"value\":\"off\"}");
        CHECK_EQ(s.status, 200); CHECK(has(s.body, "Sound: Off"));
        CHECK_EQ(r.st->value(0), 0);
        CHECK_EQ(http(r, "POST", "/v1/ops/settings.set", "{\"key\":\"Sound\",\"value\":\"Loud\"}").status, 400);
        Resp g = http(r, "POST", "/v1/ops/settings.get", "{\"key\":\"Speed\"}");
        CHECK_EQ(g.status, 200); CHECK(has(g.body, "\"msg\":\"Normal\""));
        CHECK_EQ(http(r, "POST", "/v1/ops/settings.get", "{\"key\":\"Nothing\"}").status, 404);
    }

    // ---- REST: the alias table
    {
        Rig r;
        // a path with segments: the category is ignored, the item is the key, percent escapes are decoded
        Resp s = http(r, "PUT", "/v1/configs/Test%20Settings/Speed?value=Fast");
        CHECK_EQ(s.status, 200); CHECK(has(s.body, "\"errors\":[]")); CHECK(has(s.body, "Speed: Fast"));
        CHECK_EQ(r.st->value(1), 2);
        Resp g = http(r, "GET", "/v1/configs/Test%20Settings/Speed");
        CHECK_EQ(g.status, 200); CHECK(has(g.body, "\"msg\":\"Fast\""));
        Resp bad = http(r, "PUT", "/v1/configs/Test%20Settings/Speed?value=Warp");
        CHECK_EQ(bad.status, 400); CHECK(has(bad.body, "\"errors\":[\"")); CHECK(has(bad.body, "Warp"));
        CHECK_EQ(http(r, "PUT", "/v1/configs/Test%20Settings/Nothing?value=1").status, 404);
        CHECK_EQ(http(r, "PUT", "/v1/configs:reset_to_default").status, 200);
        CHECK_EQ(r.st->value(1), 1);                                 // the default again
        // a renamed argument
        Resp run = http(r, "PUT", "/v1/runners:run_prg?file=%2Fusb0%2Fgame.prg");
        CHECK_EQ(run.status, 200); CHECK_STR(g_run_path.c_str(), "/usb0/game.prg");
        // the memory aliases reach the core's own operations (every core registers them): the bytes come back with the result
        r.io.ram[0x10] = 0xAB;
        Resp mem = http(r, "GET", "/v1/machine:readmem?address=16&length=2");
        CHECK_EQ(mem.status, 200); CHECK(has(mem.body, "\"data\":\"qwA=\"")); CHECK(has(mem.body, "\"errors\":[]"));
        Resp wm = http(r, "PUT", "/v1/machine:writemem?address=32&data=0102ff");
        CHECK_EQ(wm.status, 200); CHECK_EQ(r.io.ram[0x20], 1); CHECK_EQ(r.io.ram[0x22], 0xFF);
        Resp wm2 = http(r, "POST", "/v1/machine:writemem", "{\"address\":40,\"data\":\"7f\"}", "application/json");
        CHECK_EQ(wm2.status, 200); CHECK_EQ(r.io.ram[40], 0x7F);
        CHECK_EQ(http(r, "GET", "/v1/machine:readmem?address=65535&length=4").status, 400);
        // an alias whose operation nobody registered says what is missing
        Rig bare(false);
        Resp none = http(bare, "GET", "/v1/machine:readmem?address=0&length=16");
        CHECK_EQ(none.status, 501); CHECK(has(none.body, "machine.read_mem")); CHECK(has(none.body, "\"errors\":[\""));
        // another method on a known path: 405, an unknown path: 404
        CHECK_EQ(http(r, "GET", "/v1/machine:reset").status, 405);
        CHECK_EQ(http(r, "GET", "/v1/machine:nothing").status, 404);
        // the resets reach the base's operations (what they answer is boot.cc's: the test core has no warm reset, so it says so)
        Resp rs = http(r, "PUT", "/v1/machine:reset");
        CHECK(rs.status != 404 && rs.status != 501);
        Resp rb = http(r, "PUT", "/v1/machine:reboot");
        CHECK(rb.status != 404 && rb.status != 501);
    }

    // ---- the aliases are data: each one names an operation of the registry or of nobody
    {
        for (int i = 0; i < rest_naliases; i++) {
            CHECK(rest_aliases[i].method && rest_aliases[i].pattern && rest_aliases[i].ops);
            CHECK(!strncmp(rest_aliases[i].pattern, "/v1/", 4));
        }
        Rig r;
        OpRegistry *reg = ops_of(&r.c);
        int served = 0;
        for (int i = 0; i < rest_naliases; i++) {
            char names[96];
            snprintf(names, sizeof(names), "%s", rest_aliases[i].ops);
            for (char *t = strtok(names, "|"); t; t = strtok(0, "|")) served += reg->find_op(t) != 0;
        }
        CHECK(served >= 6);
    }

    // ---- WebSocket: the handshake and the frames
    {
        char acc[32];
        ws_accept_key("dGhlIHNhbXBsZSBub25jZQ==", acc);                // the example of RFC 6455 section 1.3
        CHECK_STR(acc, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
        uint8_t d[20];
        ws_sha1((const uint8_t *)"abc", 3, d);
        CHECK_STR(hexs(d, 20).c_str(), "a9993e364706816aba3e25717850c26c9cd0d89d");
        ws_sha1((const uint8_t *)"", 0, d);
        CHECK_STR(hexs(d, 20).c_str(), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
        const char *m56 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";    // 56 bytes: the length no longer fits the block
        ws_sha1((const uint8_t *)m56, 56, d);
        CHECK_STR(hexs(d, 20).c_str(), "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
        std::string a1000(1000, 'a');
        ws_sha1((const uint8_t *)a1000.data(), 1000, d);
        CHECK_STR(hexs(d, 20).c_str(), "291e9a6c66994949b57ba5e650361e98fc36b1ba");

        Rig r;
        Resp up = http(r, "GET", "/v1/ws", "", "", "Upgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n");
        CHECK_EQ(up.status, 101); CHECK(up.ws);
        CHECK(has(up.head, "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
        Resp noup = http(r, "GET", "/v1/ws", "", "", "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n");
        CHECK(!noup.ws); CHECK(noup.status != 101);
        Resp wrong = http(r, "GET", "/v1/nothing", "", "", "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n");
        CHECK(!wrong.ws);

        // frames: a masked text message from a client (RFC 6455 section 5.7 "Hello")
        const uint8_t hello[] = { 0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58 };
        WsFrame *f = new WsFrame;
        int code = 0;
        CHECK_EQ(ws_decode(hello, 11, f, &code), 11);
        CHECK_EQ(f->op, WS_TEXT); CHECK_EQ(f->len, 5); CHECK_STR((const char *)f->payload, "Hello");
        CHECK_EQ(ws_decode(hello, 10, f, &code), 0);                  // a byte short: more is needed
        CHECK_EQ(ws_decode(hello, 1, f, &code), 0);
        uint8_t unmasked[] = { 0x81, 0x02, 'h', 'i' };
        CHECK_EQ(ws_decode(unmasked, 4, f, &code), -1); CHECK_EQ(code, 1002);
        uint8_t reserved[] = { 0xC1, 0x80, 0, 0, 0, 0 };
        CHECK_EQ(ws_decode(reserved, 6, f, &code), -1); CHECK_EQ(code, 1002);
        uint8_t fragment[] = { 0x01, 0x80, 0, 0, 0, 0 };              // FIN clear
        CHECK_EQ(ws_decode(fragment, 6, f, &code), -1); CHECK_EQ(code, 1003);
        uint8_t binary[] = { 0x82, 0x80, 0, 0, 0, 0 };
        CHECK_EQ(ws_decode(binary, 6, f, &code), -1); CHECK_EQ(code, 1003);
        uint8_t cont[] = { 0x80, 0x80, 0, 0, 0, 0 };
        CHECK_EQ(ws_decode(cont, 6, f, &code), -1); CHECK_EQ(code, 1003);
        uint8_t huge[] = { 0x81, 0xFE, 0x10, 0x00, 0, 0, 0, 0 };      // 4096 bytes
        CHECK_EQ(ws_decode(huge, 8, f, &code), -1); CHECK_EQ(code, 1009);
        uint8_t huge64[] = { 0x81, 0xFF, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 };
        CHECK_EQ(ws_decode(huge64, 14, f, &code), -1); CHECK_EQ(code, 1009);
        uint8_t ping[] = { 0x89, 0x80, 1, 2, 3, 4 };
        CHECK_EQ(ws_decode(ping, 6, f, &code), 6); CHECK_EQ(f->op, WS_PING);
        // a 300 byte message with a 16 bit length, masked
        std::vector<uint8_t> big;
        big.push_back(0x81); big.push_back(0x80 | 126); big.push_back(300 >> 8); big.push_back(300 & 255);
        uint8_t mask[4] = { 9, 8, 7, 6 };
        for (int i = 0; i < 4; i++) big.push_back(mask[i]);
        for (int i = 0; i < 300; i++) big.push_back((uint8_t)('a' + i % 26) ^ mask[i & 3]);
        CHECK_EQ(ws_decode(big.data(), (int)big.size(), f, &code), (int)big.size());
        CHECK_EQ(f->len, 300); CHECK_EQ(f->payload[299], (uint8_t)('a' + 299 % 26));
        // encoding
        uint8_t out[400];
        CHECK_EQ(ws_encode(WS_TEXT, (const uint8_t *)"hi", 2, out, sizeof(out)), 4);
        CHECK(out[0] == 0x81 && out[1] == 2 && out[2] == 'h');
        std::string payload(300, 'x');
        CHECK_EQ(ws_encode(WS_TEXT, (const uint8_t *)payload.data(), 300, out, sizeof(out)), 304);
        CHECK(out[1] == 126 && out[2] == 1 && out[3] == 44);
        CHECK_EQ(ws_encode(WS_TEXT, (const uint8_t *)payload.data(), 300, out, 100), -1);
        delete f;
    }

    // ---- WS JSON-RPC
    {
        Rig r;
        RpcSession *s = new RpcSession;
        rpc_open(s, &r.c);
        // an operation by its name, the answer a result
        std::string a = rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"settings.set\",\"params\":{\"key\":\"Sound\",\"value\":\"Off\"}}");
        CHECK_STR(a.c_str(), "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"code\":0,\"msg\":\"Sound: Off\",\"more\":false}}");
        CHECK_EQ(r.st->value(0), 0);
        // the id is echoed as it came: a string, a number
        std::string b = rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":\"abc-7\",\"method\":\"settings.get\",\"params\":{\"key\":\"Sound\"}}");
        CHECK(has(b, "\"id\":\"abc-7\"") && has(b, "\"msg\":\"Off\""));
        // an operation that is not in any list of the adapter
        g_fresh_calls = 0;
        std::string f = rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"zz.fresh\",\"params\":{\"mode\":\"gamma\",\"level\":2,\"path\":\"/p\",\"on\":false}}");
        CHECK(has(f, "\"result\"") && has(f, "fresh mode=2 level=2 path=/p on=0"));
        CHECK_EQ(g_fresh_calls, 1);
        // errors: an operation that refused (its OpErr), a bad argument, no such method, bad JSON, bad version
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"zz.refuse\"}"), "\"error\":{\"code\":-7,\"message\":\"I will not\"}"));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"zz.fresh\",\"params\":{\"mode\":\"nope\",\"level\":0,\"path\":\"/\",\"on\":true}}"), "\"code\":-1"));
        CHECK_EQ(g_fresh_calls, 1);
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"no.such\"}"), "\"code\":-32601"));
        std::string pe = rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":");
        CHECK(has(pe, "\"code\":-32700") && has(pe, "\"id\":null"));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"1.0\",\"id\":7,\"method\":\"settings.get\"}"), "\"code\":-32600"));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":8}"), "\"code\":-32600"));
        CHECK(has(rpc(s, "[1,2]"), "\"code\":-32700"));
        // a notification (no id) runs and is not answered, not even with an error
        std::string n = rpc(s, "{\"jsonrpc\":\"2.0\",\"method\":\"settings.set\",\"params\":{\"key\":\"Sound\",\"value\":\"On\"}}");
        CHECK(n.empty()); CHECK_EQ(r.st->value(0), 1);
        CHECK(rpc(s, "{\"jsonrpc\":\"2.0\",\"method\":\"zz.refuse\"}").empty());
        // the contract names of T24 are aliases (data)
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"session/reset\"}"), "\"id\":9"));
        // the protocol's own methods
        std::string ops;
        for (int from = 0, pages = 0; pages < 50; pages++) {              // pages, as REST does
            std::string pg = rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"service.ops\",\"params\":{\"from\":" + std::to_string(from) + "}}");
            ops += pg;
            size_t nx = pg.rfind("\"next\":");
            CHECK(nx != std::string::npos);
            if (nx == std::string::npos || has(pg.substr(nx), "null")) break;
            from = atoi(pg.c_str() + nx + 7);
        }
        CHECK(has(ops, "\"name\":\"zz.fresh\"") && has(ops, "\"name\":\"settings.set\""));
        std::string st = rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"service.state\",\"params\":{\"name\":\"zz.text\"}}");
        CHECK(has(st, "\"value\":\"two\"") || has(st, "\"value\":\"one\""));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"service.state\"}"), "\"name\":\"zz.none\""));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":13,\"method\":\"service.state\",\"params\":{\"name\":\"zz.zip\"}}"), "\"code\":-2"));

        // events: a subscriber gets a notification for each emit, with the payload; an unsubscribed one nothing
        OpRegistry *reg = ops_of(&r.c);
        char note[400];
        CHECK_EQ(rpc_next(s, note, sizeof(note)), 0);
        OpIn slot = in_str("slot", "iec:drive_a");
        CHECK_EQ(reg->emit("zz.changed", &slot), 0);                  // nobody listens yet
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":14,\"method\":\"service.subscribe\",\"params\":{\"event\":\"zz.changed\"}}"), "\"result\""));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":15,\"method\":\"service.subscribe\",\"params\":{\"event\":\"zz.ping\"}}"), "\"result\""));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":16,\"method\":\"service.subscribe\",\"params\":{\"event\":\"zz.gone\"}}"), "\"error\""));
        CHECK_EQ(reg->emit("zz.changed", &slot), 1);
        CHECK(rpc_next(s, note, sizeof(note)) > 0);
        CHECK_STR(note, "{\"jsonrpc\":\"2.0\",\"method\":\"zz.changed\",\"params\":{\"slot\":\"iec:drive_a\"}}");
        CHECK_EQ(rpc_next(s, note, sizeof(note)), 0);
        CHECK_EQ(reg->emit("zz.ping", 0), 1);
        CHECK(rpc_next(s, note, sizeof(note)) > 0);
        CHECK_STR(note, "{\"jsonrpc\":\"2.0\",\"method\":\"zz.ping\",\"params\":{}}");
        // a second connection has its own queue
        RpcSession *s2 = new RpcSession;
        rpc_open(s2, &r.c);
        rpc(s2, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"service.subscribe\",\"params\":{\"event\":\"zz.changed\"}}");
        CHECK_EQ(reg->emit("zz.changed", &slot), 2);
        CHECK(rpc_next(s, note, sizeof(note)) > 0);
        CHECK(rpc_next(s2, note, sizeof(note)) > 0);
        CHECK_EQ(rpc_next(s2, note, sizeof(note)), 0);
        // unsubscribe stops it; the queue is bounded and counts what it drops
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":17,\"method\":\"service.unsubscribe\",\"params\":{\"event\":\"zz.changed\"}}"), "\"result\""));
        CHECK_EQ(reg->emit("zz.changed", &slot), 1);                  // only s2 now
        CHECK_EQ(rpc_next(s, note, sizeof(note)), 0);
        for (int i = 0; i < RPC_QUEUE + 3; i++) reg->emit("zz.changed", &slot);
        CHECK_EQ(s2->dropped, 3 + 1);                                  // the one above is waiting too: one less room
        // closing the connection takes its subscriptions back
        rpc_close(s2);
        CHECK_EQ(reg->emit("zz.changed", &slot), 0);
        rpc_close(s);
        CHECK_EQ(reg->emit("zz.ping", 0), 0);
        delete s2;
        delete s;
    }

    // ---- P2-1: the registry is asked before the contract table - a linked library's method is served, the contract answers what no operation does
    {
        Rig r;
        RpcSession *s = new RpcSession;
        rpc_open(s, &r.c);
        // without the library: the table answers (session/state is a row, monitor/exec is -32601 with its reason)
        std::string t0 = rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/state\"}");
        CHECK(has(t0, "\"backend\":\"coreapp\""));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"monitor/exec\"}"), "\"code\":-32601"));
        // the library registers operations named after the two methods
        OpRegistry *reg = ops_of(&r.c);
        reg->contributor("lib");
        reg->add_op("session.state", 0, 0, op_lib_state);
        reg->add_op("monitor.exec", 0, 0, op_lib_monitor);
        g_lib_calls = 0;
        std::string t1 = rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"session/state\"}");
        CHECK(has(t1, "the library's state") && !has(t1, "\"backend\""));
        std::string t2 = rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"monitor/exec\"}");
        CHECK(has(t2, "the library's monitor") && has(t2, "\"result\""));
        CHECK_EQ(g_lib_calls, 2);
        // a method that is in the table and that no library registered stays the table's
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"session/list\"}"), "\"sessions\""));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"debug/state\"}"), "\"code\":-32601"));
        rpc_close(s);
        delete s;
    }

    // ---- one setting, three doors: the menu, REST and JSON-RPC end in the same state with the same words
    {
        Rig m, h, j;
        struct Host : MenuHost {
            void info(int, char *o, int) { o[0] = 0; }
            bool return_to_host(char *, int) { return false; }
        } host;
        MenuCtl menu(&m.c, &host);
        menu.open();
        menu.key(KEY_F5); menu.key('2');
        for (int i = 0; i < menu.rows(); i++) { char l[60], rr[60]; bool act; menu.row(i, l, 60, rr, 60, &act); if (!strcmp(l, "Speed")) { while (menu.selected() < i) menu.key(KEY_DOWN); } }
        menu.key(KEY_RETURN);                                          // Normal -> Fast
        std::string menu_says = menu.status();
        Resp rest = http(h, "POST", "/v1/ops/settings.set", "{\"key\":\"Speed\",\"value\":\"Fast\"}");
        RpcSession *s = new RpcSession;
        rpc_open(s, &j.c);
        std::string rpc_says = rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"settings.set\",\"params\":{\"key\":\"Speed\",\"value\":\"Fast\"}}");
        CHECK_EQ(m.st->value(1), 2); CHECK_EQ(h.st->value(1), 2); CHECK_EQ(j.st->value(1), 2);
        CHECK_STR(menu_says.c_str(), "Speed: Fast (takes effect at the next reset)");
        CHECK(has(rest.body, menu_says.c_str()));
        CHECK(has(rpc_says, menu_says.c_str()));
        CHECK_STR(m.fs.text("/usb0/cores/t.cfg").c_str(), h.fs.text("/usb0/cores/t.cfg").c_str());
        CHECK_STR(m.fs.text("/usb0/cores/t.cfg").c_str(), j.fs.text("/usb0/cores/t.cfg").c_str());
        rpc_close(s);
        delete s;
    }

    // ---- the TRX64 runtime contract (rpccontract.cc): every method of the daemon's dispatch is a row, mapped or answered -32601 by name
    {
        std::ifstream f(CONTRACT_LIST);
        std::set<std::string> want, have;
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty()) want.insert(line);
        }
        for (int i = 0; i < rpc_ncontract; i++) have.insert(rpc_contract[i].method);
        CHECK_EQ(want.size(), 158u);
        CHECK(want == have);                                          // the table is the daemon's list, no more, no less
        CHECK_EQ((int)have.size(), rpc_ncontract);                    // and no method twice

        Rig r;
        RpcSession *s = new RpcSession;
        rpc_open(s, &r.c);
        int mapped = 0, refused = 0;
        for (int i = 0; i < rpc_ncontract; i++) {
            const ContractRow &row = rpc_contract[i];
            std::string a = rpc(s, std::string("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"") + row.method + "\"}");
            if (row.kind >= CK_NO_SANDBOX) {
                refused++;
                std::string want_msg = std::string("\"error\":{\"code\":-32601,\"message\":\"") + row.method + ": " + contract_reason(row.kind) + "\"}";
                CHECK(has(a, want_msg));
            } else {
                mapped++;
                CHECK(!has(a, "\"code\":-32700"));
                CHECK(!has(a, std::string("\"message\":\"") + row.method + ": " + contract_reason(CK_NO_SANDBOX)));
            }
        }
        CHECK_EQ(mapped, 15);
        CHECK_EQ(refused, 143);
        CHECK(std::string(contract_reason(CK_NO_SANDBOX)).find("local TRX64 sandbox") != std::string::npos);

        // ping, keep_alive, the session of the board
        std::string pg = rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}");
        CHECK(has(pg, "\"runtime_version\":\"trx64-runtime/2\"") && has(pg, "\"backend\":\"coreapp\"") && has(pg, "coreapp 0.7"));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"daemon/keep_alive\",\"params\":{\"seconds\":30}}"), "\"armed\":false"));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/list\"}"), "\"sessions\":[{\"id\":\"default\",\"model\":\"Test Machine\"}]"));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/create\",\"params\":{}}"), "\"attached\":true"));
        std::string stt = rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/state\"}");
        CHECK(has(stt, "\"started\":") && has(stt, "\"paused\":false"));
        r.c.started = true; r.c.frozen = true;
        stt = rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/state\"}");
        CHECK(has(stt, "\"running\":false") && has(stt, "\"paused\":true"));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/close\"}"), "\"code\":-32601"));

        // session/read_memory: ranges of {addr, len, lens}, bytes as base64, split over several reads
        for (int i = 0; i < 700; i++) r.io.ram[0x2000 + i] = (uint8_t)(i * 7 + 1);
        std::string rm = rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/read_memory\",\"params\":{\"ranges\":[{\"addr\":8192,\"len\":700,\"lens\":\"cpu\"},{\"addr\":3,\"len\":2}]}}");
        CHECK(has(rm, "\"chunks\":[{\"addr\":8192,\"len\":700,\"lens\":\"cpu\",\"bytes\":\""));
        size_t b0 = rm.find("\"bytes\":\"") + 9, b1 = rm.find('"', b0);
        std::vector<uint8_t> dec(700);
        CHECK_EQ(b64_decode(rm.substr(b0, b1 - b0).c_str(), dec.data(), 700), 700);
        bool same = true;
        for (int i = 0; i < 700; i++) same &= dec[i] == (uint8_t)(i * 7 + 1);
        CHECK(same);
        CHECK(has(rm, "{\"addr\":3,\"len\":2,\"lens\":\"cpu\",\"bytes\":\"AAA=\"}"));        // two zero bytes
        CHECK(has(rm, "\"c64Cycles\":null"));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/read_memory\",\"params\":{\"ranges\":[{\"addr\":0,\"len\":4,\"lens\":\"rom\"}]}}"), "\"code\":-32602"));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/read_memory\",\"params\":{\"ranges\":[{\"addr\":0,\"len\":7000}]}}"), "is this application's cap"));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/read_memory\",\"params\":{\"ranges\":[{\"addr\":70000,\"len\":4}]}}"), "\"code\":-32602"));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/read_memory\",\"params\":{\"ranges\":[{\"len\":4}]}}"), "needs `addr`"));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/read_memory\",\"params\":{\"ranges\":[{\"addr\":0,\"len\":0}]}}"), "`len` in 1..=65536"));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/read_memory\",\"params\":{}}"), "`ranges` must be an array"));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/read_memory\",\"params\":{\"ranges\":[{\"addr\":65535,\"len\":2}]}}"), "cross the end"));

        // the other mapped methods
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/reset\",\"params\":{\"mode\":\"cold\"}}"), "\"mode\":\"cold\""));
        std::string soft = rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/reset\",\"params\":{\"mode\":\"soft\"}}");
        CHECK(!has(soft, "-32601") && (has(soft, "\"mode\":\"soft\"") || has(soft, "session/reset: ")));      // the operation ran: its result, or its own words
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/power\",\"params\":{\"op\":\"on\"}}"), "\"powered\":true"));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/power\",\"params\":{\"op\":\"off\"}}"), "cannot be powered off"));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/power\",\"params\":{\"op\":\"x\"}}"), "\"code\":-32602"));
        g_load.clear(); g_run_path.clear();
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/load_prg\",\"params\":{\"prg_path\":\"/usb0/a.prg\",\"run_addr\":2061}}"), "\"ok\":true"));
        CHECK_STR(g_load.c_str(), "/usb0/a.prg 2061");
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"runtime/run_prg\",\"params\":{\"prg_path\":\"/usb0/b.prg\"}}"), "\"ok\":true"));
        CHECK_STR(g_run_path.c_str(), "/usb0/b.prg");
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"runtime/run_prg\",\"params\":{\"bytes_b64\":\"AAAA\"}}"), "give prg_path"));
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"runtime/run_prg\",\"params\":{}}"), "prg_path is required"));
        g_drive.clear();
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"media/mount\",\"params\":{\"path\":\"/usb0/d.d64\"}}"), "\"ok\":true"));
        CHECK_STR(g_drive.c_str(), "mount drive_a /usb0/d.d64 0");
        CHECK(has(rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"media/mount\",\"params\":{\"path\":\"/usb0/d.d64\",\"unit\":9}}"), "no drive at unit 9"));
        rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"media/open\",\"params\":{\"path\":\"/usb0/e.d64\",\"unit\":8}}");
        CHECK_STR(g_drive.c_str(), "run drive_a /usb0/e.d64");
        rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"media/unmount\",\"params\":{}}");
        CHECK_STR(g_drive.c_str(), "eject drive_a");
        rpc(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/drive_reset\"}");
        CHECK_STR(g_drive.c_str(), "reset drive_a");
        rpc_close(s);
        delete s;

        // a core without the operations a method needs: -32601, naming the operation
        FakeIo io2; FakeFs fs2; CoreCtx c2;
        memset(&c2, 0, sizeof(c2));
        c2.desc = &testcore::desc; c2.io = &io2; c2.fs = &fs2;
        RpcSession *s2 = new RpcSession;
        rpc_open(s2, &c2);
        CHECK(has(rpc(s2, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session/read_memory\",\"params\":{\"ranges\":[{\"addr\":0,\"len\":1}]}}"),
                  "needs the operation machine.read_mem, which this core did not register"));
        CHECK(has(rpc(s2, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"media/mount\",\"params\":{\"path\":\"/x\"}}"), "needs the operation drive.mount"));
        CHECK(has(rpc(s2, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"runtime/run_prg\",\"params\":{\"prg_path\":\"/x\"}}"), "needs the operation prg.run"));
        rpc_close(s2);
        delete s2;
        delete c2.ops;
    }

    // ---- the start gate (T192.3 section 5): the port is open while the start waits; 503 with the step, nothing of the service reached. Last: it is global.
    {
        Rig r;
        rest_starting("ROM files: Kickstart not found. Put a 512 KB .rom file in roms/demo");
        Resp i = http(r, "GET", "/v1/info");
        CHECK_EQ(i.status, 503);
        CHECK(has(i.body, "\"starting\":true") && has(i.body, "Kickstart not found") && has(i.body, "\"code\":-1"));
        g_fresh_calls = 0;
        Resp o = http(r, "POST", "/v1/ops/zz.fresh?mode=2&level=4&path=%2Fx&on=false");
        CHECK_EQ(o.status, 503); CHECK_EQ(g_fresh_calls, 0);         // the operation does not run during the start
        Resp w = http(r, "GET", "/v1/ws", "", "", "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n");
        CHECK_EQ(w.status, 503); CHECK(!w.ws);
        rest_starting("Starting the core");
        CHECK(has(http(r, "GET", "/v1/info").body, "Starting the core"));
        CHECK(rest_start_gate());
        {                                                           // a message that does not fit the answer is cut, the JSON stays valid (all quotes: escaped twice as long)
            std::string q(190, '"');
            rest_starting(q.c_str());
            Resp lg = http(r, "GET", "/v1/info");
            CHECK_EQ(lg.status, 503); CHECK(has(lg.body, "\"starting\":true") && has(lg.body, "\"more\":false}"));
            CHECK(lg.body.size() > 0 && lg.body[0] == '{' && lg.body.back() == '}');
        }
        rest_started();
        CHECK(!rest_start_gate());
        CHECK_EQ(http(r, "GET", "/v1/info").status, 200);
        rest_starting("ROM files: again");                          // after the start (a ROM message from the menu) the gate stays open
        CHECK_EQ(http(r, "GET", "/v1/info").status, 200);
        Resp o2 = http(r, "POST", "/v1/ops/zz.fresh?mode=2&level=4&path=%2Fx&on=false");
        CHECK_EQ(o2.status, 200); CHECK_EQ(g_fresh_calls, 1);
    }

    DONE("test_rest");
}
