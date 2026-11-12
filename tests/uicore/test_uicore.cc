// Unit tests of uicore (T192.1 MVP): the service, the token, the dispatcher, events, jobs, questions - against a fake port and a fake domain.
// The "UI task" is simulated: g_ui is true while the harness runs UI-task code; the network side runs with it false, and its waits (sleep_ms) pump
// uic_service() the way the real UI loop would.
#include "uic.h"
#include "uic_priv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) { g_pass++; } else { g_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_S(a, b) do { std::string x_ = (a), y_ = (b); if (x_ == y_) { g_pass++; } else { g_fail++; printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, x_.c_str(), y_.c_str()); } } while (0)

// ---- fakes -------------------------------------------------------------------------------------------------------------------------------
struct Env {
    uint32_t t = 1000;
    bool ui = false;
    bool have_file = false;
    std::string file;
    bool fail_save = false;
    std::vector<std::string> log;              // order of events: "save", "answer"
    UicService *svc = nullptr;
    int locks = 0;
};
static Env E;

static uint32_t p_now(void *) { return E.t; }
static void p_lock(void *) { E.locks++; }
static void p_unlock(void *) {}
static void pump();
static void p_sleep(void *, int ms) { E.t += ms; pump(); }
static bool p_ui(void *) { return E.ui; }
static int p_entropy(void *, uint8_t *o, int n) { for (int i = 0; i < n && i < 16; i++) o[i] = (uint8_t)(i * 7 + 3); return n < 16 ? n : 16; }
static int p_load(void *, char *b, int cap) { if (!E.have_file) return -1; int n = (int)E.file.size(); if (n > cap) n = cap; memcpy(b, E.file.data(), n); return n; }
static bool p_save(void *, const char *b, int n) { if (E.fail_save) return false; E.log.push_back("save"); E.file.assign(b, n); E.have_file = true; return true; }
static bool p_remove(void *) { E.have_file = false; return true; }

struct Dom : UicDomain {
    std::string owner = "menu";
    std::vector<std::string> items{ "set:sound", "op:reset", "fs:a.d64", "fs:b.d64" };
    int sel = 0;
    bool asking = false;
    bool two_step = false;                        // answering "step1" leaves a second question ("step2") on the screen

    std::string asked_item;
    std::vector<std::string> acts;               // what actually ran
    std::vector<UicJobInfo> jobs_now;
    bool job_ok = true;
    bool cancel_ok = true;
    std::string text_set;
    const char *app() override { return "core"; }
    const char *keys_owner() override { return owner.c_str(); }
    void snapshot(UicW &w) override {
        w.ks("screen", "home");
        w.kopen("items", '[');
        for (size_t i = 0; i < items.size(); i++) { w.sep(); w.open('{'); w.ks("id", items[i].c_str()); w.kb("focused", (int)i == sel); w.close('}'); }
        w.close(']');
    }
    int find(const char *id) { for (size_t i = 0; i < items.size(); i++) if (items[i] == id) return (int)i; return -1; }
    void intent(const UicIntent &in, UicReply *r) override {
        if (in.kind == UD_SELECT || in.kind == UD_ACTIVATE) {
            int i = find(in.item);
            if (i < 0) { r->r = UR_GONE; snprintf(r->why, sizeof(r->why), "gone"); return; }
            sel = i;
            if (in.kind == UD_ACTIVATE) {
                if (items[i] == "op:reset") { asking = true; asked_item = items[i]; r->r = UR_ASKED; return; }
                acts.push_back(items[i]);
            }
            r->r = UR_TAKEN;
        } else if (in.kind == UD_KEY) {
            acts.push_back(std::string("key:") + in.key + (in.local ? ":local" : ":remote"));
            r->r = UR_TAKEN;
        } else if (in.kind == UD_SET) {
            acts.push_back(std::string("set:") + in.key + "=" + in.value);
            r->r = std::string(in.value) == "hold" ? UR_HELD : UR_TAKEN;
        } else if (in.kind == UD_OP) {
            asking = true; asked_item = in.item; r->r = UR_ASKED;
        } else {
            r->r = UR_TAKEN;
        }
    }
    bool question(UicQuestion *q) override {
        if (!asking) return false;
        snprintf(q->text, sizeof(q->text), "Run %s?", asked_item.c_str());
        q->destructive = true;
        return true;
    }
    void answer(const char *c) override {
        acts.push_back(std::string("answer:") + c + ":" + asked_item);
        if (two_step && asked_item == "step1" && std::string(c) == "yes") { asked_item = "step2"; return; }        // the next page asks again at once
        asking = false;
    }
    int jobs(UicJobInfo *o, int max) override { int n = 0; for (auto &j : jobs_now) if (n < max) o[n++] = j; return n; }
    bool cancel_job(const char *) override { return cancel_ok; }
    void job_result(const char *, bool *ok, char *t, int n) override { *ok = job_ok; snprintf(t, n, "%s", job_ok ? "done fine" : "broke"); }
    void set_text(const char *t) override { text_set = t; }
};

static Dom D;
static UicService S;
static char SNAP[6000];
static UicPort PORT = { nullptr, p_now, p_lock, p_unlock, p_sleep, p_ui, p_entropy, p_load, p_save, p_remove };

static void ui_pass() { E.ui = true; uic_service(&S); E.ui = false; }
static void pump() { ui_pass(); }
static void fresh(bool keep_file = false)
{
    bool hf = E.have_file; std::string f = E.file;
    E = Env();
    if (keep_file) { E.have_file = hf; E.file = f; }
    D = Dom();
    memset(&S, 0, sizeof(S));
    uic_init(&S, &PORT, &D, SNAP, sizeof(SNAP));
    E.svc = &S;
    ui_pass();
}
static UicReply send(const char *json)
{
    UicIntent in; UicReply r;
    if (!uic_intent_parse(json, (int)strlen(json), &in)) { memset(&r, 0, sizeof(r)); r.r = UR_BAD; snprintf(r.why, sizeof(r.why), "parse"); return r; }
    uic_submit(&S, in, &r);
    return r;
}
static std::string http(const char *m, const char *path, const char *body, int *status)
{
    static char out[8000];
    int n = uic_http(&S, m, path, body ? body : "", body ? (int)strlen(body) : 0, out, sizeof(out), status);
    if (n < 0) { *status = n; return ""; }
    return std::string(out, n);
}
static std::string grant_token()
{
    int st; std::string b = http("POST", "/v1/ui/grant", 0, &st);
    if (st != 200) return "";
    char t[64]; uic_json_get(b.c_str(), (int)b.size(), "token", t, sizeof(t));
    return t;
}
static bool has(const std::string &s, const char *x) { return s.find(x) != std::string::npos; }

// ---- tests -------------------------------------------------------------------------------------------------------------------------------
static void t_json()
{
    printf("json writer and reader\n");
    char b[200];
    UicW w(b, sizeof(b));
    w.open('{'); w.ks("a", "q\"\\\n\x01 \xc3\xa9 \xff"); w.ki("n", -5); w.kb("t", true); w.close('}');
    CHECK_S(b, "{\"a\":\"q\\\"\\\\\\u000a\\u0001 \xc3\xa9 ?\",\"n\":-5,\"t\":true}");
    char v[40];
    const char *js = "{ \"x\": {\"item\":\"no\"}, \"item\": \"yes\\\"q\", \"n\": 12, \"b\": true, \"arr\":[1,\"item\"], \"e\":\"\\u0041\" }";
    CHECK(uic_json_get(js, (int)strlen(js), "item", v, sizeof(v)) == 1 && std::string(v) == "yes\"q");
    CHECK(uic_json_get(js, (int)strlen(js), "n", v, sizeof(v)) == 2 && std::string(v) == "12");
    CHECK(uic_json_get(js, (int)strlen(js), "b", v, sizeof(v)) == 3 && std::string(v) == "true");
    CHECK(uic_json_get(js, (int)strlen(js), "e", v, sizeof(v)) == 1 && std::string(v) == "A");
    CHECK(uic_json_get(js, (int)strlen(js), "nope", v, sizeof(v)) == 0);
    UicW tiny(b, 8);
    tiny.str("0123456789");
    CHECK(!tiny.ok && tiny.length() == -1);
}

static void t_ids()
{
    printf("T192.1-S4/S5 ids\n");
    char a[64], b[64], c[64];
    uic_id_make("set:", "sound", a, sizeof(a));
    CHECK_S(a, "set:sound");
    uic_id_make("fs:", "My File.D64", a, sizeof(a));
    uic_id_make("fs:", "My File.D64", b, sizeof(b));
    CHECK_S(a, b);
    uic_id_make("fs:", "My_File.D64", c, sizeof(c));
    CHECK(std::string(a) != std::string(c));       // same body after folding, different original: different id
    CHECK(uic_id_valid(a) && uic_id_valid(c));
    char longname[300]; memset(longname, 'x', 299); longname[299] = 0;
    uic_id_make("fs:", longname, a, sizeof(a));
    uic_id_make("fs:", longname, b, sizeof(b));
    CHECK(strlen(a) <= UIC_ID_MAX && uic_id_valid(a) && std::string(a) == b);
    longname[298] = 'y';
    uic_id_make("fs:", longname, b, sizeof(b));
    CHECK(std::string(a) != b);                    // cut names differing behind the cut still differ
    // ids are derived from content: inserting an entry above changes no other id
    const char *names[] = { "a.d64", "b.d64", "c.d64" };
    char before[3][64], after[3][64], ins[64];
    for (int i = 0; i < 3; i++) uic_id_make("fs:", names[i], before[i], 64);
    uic_id_make("fs:", "new.d64", ins, 64);
    for (int i = 0; i < 3; i++) uic_id_make("fs:", names[i], after[i], 64);
    for (int i = 0; i < 3; i++) CHECK_S(before[i], after[i]);
    // select by an old id after a reorder lands on the same entry; a vanished id answers gone and moves nothing
    fresh();
    std::swap(D.items[2], D.items[3]);
    UicReply r = send("{\"do\":\"select\",\"id\":1,\"item\":\"fs:b.d64\"}");
    CHECK(r.r == UR_TAKEN && D.sel == 2);
    int was = D.sel;
    r = send("{\"do\":\"select\",\"id\":2,\"item\":\"fs:vanished\"}");
    CHECK(r.r == UR_GONE && D.sel == was);
}

static void t_token()
{
    printf("T192.1-S14..S17, S1..S3 token\n");
    fresh();
    int st;
    std::string h = http("GET", "/v1/ui/hello", 0, &st);
    CHECK(st == 200 && has(h, "\"grant\":true") && has(h, "\"ui\":2"));
    CHECK(uic_gate(&S, "GET", "/v1/ops", 0, 0, 0) == 401);       // before any token everything but the public routes is refused
    std::string tok = grant_token();
    CHECK(tok.size() == 32);
    CHECK(E.log.size() == 1 && E.log[0] == "save" && E.file.substr(0, 32) == tok);   // S1: on disk before the answer left
    CHECK(grant_token() == "");                                   // S14 control: a second fresh login must not get it
    h = http("GET", "/v1/ui/hello", 0, &st);
    CHECK(has(h, "\"grant\":false"));
    // S15/S16: token as parameter, as bearer, as header; wrong digit refused
    CHECK(uic_gate(&S, "GET", "/v1/ops", 0, 0, 0) == 401);
    CHECK(uic_gate(&S, "GET", "/v1/ops", tok.c_str(), 0, 0) == 0);
    CHECK(uic_gate(&S, "GET", "/v1/ops", 0, tok.c_str(), 0) == 0);
    CHECK(uic_gate(&S, "GET", "/v1/ops", 0, 0, tok.c_str()) == 0);
    std::string bad = tok; bad[31] = bad[31] == '0' ? '1' : '0';
    CHECK(uic_gate(&S, "GET", "/v1/ops", bad.c_str(), 0, 0) == 401);
    CHECK(uic_gate(&S, "GET", "/v1/ops", (tok + "0").c_str(), 0, 0) == 401);     // longer
    CHECK(uic_gate(&S, "GET", "/v1/ops", tok.substr(0, 31).c_str(), 0, 0) == 401);   // shorter
    CHECK(uic_gate(&S, "GET", "/v1/ops", "", 0, 0) == 401);
    // S2: the public routes are exactly these; the table walk with no token
    const char *routes[][2] = { { "GET", "/" }, { "GET", "/v1/ui/hello" }, { "POST", "/v1/ui/grant" }, { "GET", "/v1/info" }, { "GET", "/v1/ops" }, { "POST", "/v1/ops/x" },
        { "GET", "/v1/state" }, { "GET", "/v1/state/x" }, { "GET", "/v1/events" }, { "GET", "/v1/ui/state" }, { "POST", "/v1/ui/intent" }, { "GET", "/v1/ws" }, { "GET", "/v1/ui/ws" },
        { "GET", "/v1/report" }, { "PUT", "/v1/machine:reset" }, { "GET", "/v1/boot" }, { "GET", "/v1/ui/grant" }, { "POST", "/v1/ui/hello" }, { "GET", "/index.html" } };
    int pub = 0;
    for (auto &r : routes) { bool open = uic_gate(&S, r[0], r[1], 0, 0, 0) == 0; pub += open; CHECK(open == uic_route_public(r[0], r[1])); }
    CHECK(pub == 3);
    // S3: the token is in no snapshot, event, ack or job text
    ui_pass();
    D.items.push_back("x");
    uic_toast(&S, "hello");
    UicReply r = send("{\"do\":\"key\",\"id\":9,\"key\":\"ret\"}");
    char snap[6000]; uint32_t rev;
    int n = uic_snapshot(&S, snap, sizeof(snap), &rev);
    CHECK(n > 0 && !has(snap, tok.c_str()));
    for (uint32_t i = 0; i < S.ev_count; i++) CHECK(!has(S.ev[(S.ev_head + i) % UIC_EVENTS].text, tok.c_str()));
    char ack[300]; UicIntent in; memset(&in, 0, sizeof(in)); uic_ack_json(in, r, false, ack, sizeof(ack));
    CHECK(!has(ack, tok.c_str()));
    // S17: the file deleted: the next fresh login gets a NEW token, the old one stops working
    p_remove(0);
    E.t += 2500; ui_pass();                                        // the UI task looks at the file every two seconds; one miss may be a busy medium, two are a deleted file
    E.t += 2500; ui_pass();
    CHECK(uic_gate(&S, "GET", "/v1/ops", tok.c_str(), 0, 0) == 401);
    std::string t2 = grant_token();
    CHECK(t2.size() == 32 && t2 != tok);
    CHECK(uic_gate(&S, "GET", "/v1/ops", t2.c_str(), 0, 0) == 0 && uic_gate(&S, "GET", "/v1/ops", tok.c_str(), 0, 0) == 401);
    // S1: a crash between the write and the answer leaves the file: after the restart a fresh login cannot be granted again
    fresh(true);
    CHECK(E.have_file);
    CHECK(grant_token() == "");
    ui_pass();
    CHECK(uic_gate(&S, "GET", "/v1/ops", t2.c_str(), 0, 0) == 0);   // and the token that was written is the one that works
    // a file that cannot be written grants nothing, and leaves no half token
    fresh();
    E.fail_save = true;
    CHECK(grant_token() == "" && uic_gate(&S, "GET", "/v1/ops", "0123456789abcdef0123456789abcdef", 0, 0) == 401);
}

static void t_dispatch()
{
    printf("T192.1-S6..S9, S15, S17 dispatcher\n");
    fresh();
    // S6: activate of an asking item produces the question and runs nothing before the answer
    UicReply r = send("{\"do\":\"activate\",\"id\":1,\"item\":\"op:reset\"}");
    CHECK(r.r == UR_ASKED);
    ui_pass();
    CHECK(S.q_open && D.acts.empty());
    // S15: a second intent that needs a question is refused, never merged
    r = send("{\"do\":\"activate\",\"id\":2,\"item\":\"op:reset\"}");
    CHECK(r.r == UR_BLOCKED && D.acts.empty());
    UicIntent op; memset(&op, 0, sizeof(op)); op.kind = UD_OP; snprintf(op.item, sizeof(op.item), "x");
    uic_submit(&S, op, &r);
    CHECK(r.r == UR_BUSY);
    // S13: the first answer wins
    std::string qid = S.q_id;
    r = send(("{\"do\":\"answer\",\"id\":3,\"q\":\"" + qid + "\",\"choice\":\"yes\"}").c_str());
    CHECK(r.r == UR_TAKEN && D.acts.size() == 1 && D.acts[0] == "answer:yes:op:reset");
    r = send(("{\"do\":\"answer\",\"id\":4,\"q\":\"" + qid + "\",\"choice\":\"no\"}").c_str());
    CHECK(r.r == UR_GONE && D.acts.size() == 1);                   // the late answer is not applied
    // the screen answers first: the remote answer is gone
    D.acts.clear();
    r = send("{\"do\":\"activate\",\"item\":\"op:reset\"}");
    ui_pass();
    CHECK(S.q_open);
    qid = S.q_id;
    D.asking = false;                                               // RETURN on the screen
    ui_pass();
    CHECK(!S.q_open);
    r = send(("{\"do\":\"answer\",\"q\":\"" + qid + "\",\"choice\":\"yes\"}").c_str());
    CHECK(r.r == UR_GONE && D.acts.empty());
    // S8: a modal owns the keys: select/activate/set are blocked and change nothing, key still goes through
    for (const char *o : { "dialog", "monitor", "picture", "search", "job", "closed" }) {
        D.owner = o; D.sel = 1; D.acts.clear();
        UicReply a = send("{\"do\":\"activate\",\"item\":\"fs:a.d64\"}");
        UicReply b = send("{\"do\":\"select\",\"item\":\"fs:b.d64\"}");
        UicReply c = send("{\"do\":\"set\",\"key\":\"sound\",\"value\":\"on\"}");
        CHECK(a.r == UR_BLOCKED && b.r == UR_BLOCKED && c.r == UR_BLOCKED && D.sel == 1 && D.acts.empty());
        CHECK(send("{\"do\":\"key\",\"key\":\"down\"}").r == UR_TAKEN);
    }
    D.owner = "menu"; D.acts.clear();
    // S9: set honours the hold; the reply says held
    r = send("{\"do\":\"set\",\"id\":7,\"key\":\"sound\",\"value\":\"hold\"}");
    CHECK(r.r == UR_HELD);
    r = send("{\"do\":\"set\",\"key\":\"sound\",\"value\":\"on\"}");
    CHECK(r.r == UR_TAKEN && D.acts.back() == "set:sound=on");
    CHECK(send("{\"do\":\"set\"}").r == UR_BAD);
    CHECK(send("{\"do\":\"nonsense\"}").r == UR_BAD);
    // S17: an intent applied off the UI task is refused and counted, nothing changes
    UicIntent in; memset(&in, 0, sizeof(in)); in.kind = UD_ACTIVATE; snprintf(in.item, sizeof(in.item), "fs:a.d64");
    D.acts.clear();
    E.ui = false;
    uic_apply(&S, in, &r);
    CHECK(r.r == UR_BAD && S.violations == 1 && D.acts.empty());
    // local input wins: a key of the board while a remote intent waits is applied, and the remote intent - made for a screen that key changed - is refused (T192.3-S10)
    D.acts.clear();
    UicIntent rem; memset(&rem, 0, sizeof(rem)); rem.kind = UD_KEY; snprintf(rem.key, sizeof(rem.key), "down");
    int idx = 0; S.slot[idx].in = rem; S.slot[idx].gen = S.local_gen; S.slot[idx].state = 1;     // queued remote, not yet drained
    UicIntent loc = rem; snprintf(loc.key, sizeof(loc.key), "up");
    E.ui = true; UicReply lr; uic_local(&S, loc, &lr); uic_service(&S); E.ui = false;
    CHECK(D.acts.size() == 1 && D.acts[0] == "key:up:local");
    CHECK(S.slot[idx].state == 2 && S.slot[idx].out.r == UR_BLOCKED && strstr(S.slot[idx].out.why, "local input") != 0);
    S.slot[idx].state = 0;
    // control: a remote intent queued AFTER the local key is applied (the screen it was made for is the one that stands)
    D.acts.clear();
    E.ui = true; uic_local(&S, loc, &lr); E.ui = false;
    S.slot[idx].in = rem; S.slot[idx].gen = S.local_gen; S.slot[idx].state = 1;
    E.ui = true; uic_service(&S); E.ui = false;
    CHECK(D.acts.size() == 2 && D.acts[0] == "key:up:local" && D.acts[1] == "key:down:remote");
    S.slot[idx].state = 0;
}

static void t_events()
{
    printf("T192.1-S10, S11, S16 events and clients\n");
    fresh();
    E.ui = true; uic_note(&S); E.ui = false; ui_pass();
    UicClient c; uic_client_open(&S, &c);
    char m[8000];
    int n = uic_client_next(&S, &c, m, sizeof(m));
    CHECK(n > 0 && has(m, "\"t\":\"hello\"") && has(m, "\"ui\":2"));
    n = 0; for (int i = 0; i < 3 && n == 0; i++) { ui_pass(); n = uic_client_next(&S, &c, m, sizeof(m)); }
    CHECK(n > 0 && has(m, "\"t\":\"snap\"") && has(m, "\"ui\":2") && has(m, "\"items\""));
    uint32_t snap_rev = c.snap_rev;
    // events after the snapshot, strictly increasing rev
    E.ui = true; uic_toast(&S, "one"); uic_toast(&S, "two"); E.ui = false;
    uint32_t last = snap_rev;
    int got = 0;
    while ((n = uic_client_next(&S, &c, m, sizeof(m))) > 0) {
        char v[16]; uic_json_get(m, n, "rev", v, sizeof(v));
        uint32_t rv = (uint32_t)atoi(v);
        if (has(m, "\"t\":\"toast\"")) { CHECK(rv > last); last = rv; got++; }
        if (has(m, "\"t\":\"snap\"")) { break; }
    }
    CHECK(got == 2);
    // a state change makes a snapshot with the newer rev
    E.ui = true; uic_note(&S); E.ui = false; ui_pass();
    n = uic_client_next(&S, &c, m, sizeof(m));
    CHECK(n > 0 && has(m, "\"t\":\"snap\""));
    // S11: a stalled client: the ring is bounded, the UI task's pass does not grow, the client is resynced and closed
    UicClient slow; uic_client_open(&S, &slow);
    uic_client_next(&S, &slow, m, sizeof(m));                       // hello
    for (int i = 0; i < 3; i++) { ui_pass(); if (uic_client_next(&S, &slow, m, sizeof(m)) > 0) break; }
    int locks0 = E.locks;
    E.ui = true; for (int i = 0; i < 200; i++) uic_toast(&S, "burst"); E.ui = false;
    CHECK(S.ev_count == UIC_EVENTS && S.overflows > 100);
    CHECK(E.locks - locks0 < 200 * 4);                             // a constant number of critical sections per event, nobody waits on the client
    n = uic_client_next(&S, &slow, m, sizeof(m));
    CHECK(n > 0 && has(m, "\"t\":\"resync\"") && slow.closing);
    CHECK(uic_client_next(&S, &slow, m, sizeof(m)) == 0);
    uic_client_close(&S, &slow);
    // the fast client sees the burst's tail, not a resync, when it keeps up
    uic_client_close(&S, &c);
    // the message side: sub, get, intent
    UicClient c2; uic_client_open(&S, &c2);
    n = uic_client_in(&S, &c2, "{\"t\":\"sub\",\"push\":\"note\"}", 24, m, sizeof(m));
    CHECK(has(m, "\"push\":\"note\"") && !c2.snap_on);
    n = uic_client_in(&S, &c2, "{\"t\":\"get\"}", 11, m, sizeof(m));
    CHECK(n > 0 && has(m, "{\"t\":\"snap\",\"ui\":2"));
    const char *im = "{\"do\":\"select\",\"id\":\"c1\",\"item\":\"fs:a.d64\"}";
    n = uic_client_in(&S, &c2, im, (int)strlen(im), m, sizeof(m));
    CHECK(has(m, "\"t\":\"ack\"") && has(m, "\"id\":\"c1\"") && has(m, "\"r\":\"taken\""));
    n = uic_client_in(&S, &c2, "{\"x\":1}", 7, m, sizeof(m));
    CHECK(has(m, "\"t\":\"error\""));
    uic_client_close(&S, &c2);
}

static void t_jobs()
{
    printf("T192.1-S12, S14 jobs and question timeout\n");
    fresh();
    UicJobInfo j; memset(&j, 0, sizeof(j));
    snprintf(j.key, sizeof(j.key), "menu.job"); snprintf(j.kind, sizeof(j.kind), "create"); snprintf(j.title, sizeof(j.title), "Blank disk");
    snprintf(j.step, sizeof(j.step), "Writing"); j.done = 10; j.total = 100; j.cancelable = true;
    D.jobs_now.push_back(j);
    ui_pass();
    char snap[6000]; uic_snapshot(&S, snap, sizeof(snap), 0);
    CHECK(has(snap, "\"phase\":\"running\"") && has(snap, "\"done\":10") && has(snap, "\"total\":100") && has(snap, "\"step\":\"Writing\"") && has(snap, "\"cancelable\":true"));
    D.jobs_now[0].done = 55;
    ui_pass();
    CHECK(has(S.ev[(S.ev_head + S.ev_count - 1) % UIC_EVENTS].text, "\"done\":55"));
    UicReply r = send("{\"do\":\"cancel\",\"job\":\"j1\"}");
    CHECK(r.r == UR_TAKEN);
    D.jobs_now.clear();
    ui_pass();
    uic_snapshot(&S, snap, sizeof(snap), 0);
    CHECK(has(snap, "\"phase\":\"cancelled\""));
    // a job that cannot cancel says so and refuses; nothing changes
    fresh();
    char id[8];
    E.ui = true; CHECK(uic_job_begin(&S, "fw_write", "Firmware", false, id)); E.ui = false;
    uint32_t rev0 = S.rev;
    r = send("{\"do\":\"cancel\",\"job\":\"j1\"}");
    CHECK(r.r == UR_REFUSED && !uic_job_cancel_requested(&S, id) && S.rev == rev0);
    E.ui = true; uic_job_step(&S, id, 3, 8, "block 3"); uic_job_end(&S, id, true, "ok"); E.ui = false;
    ui_pass();
    uic_snapshot(&S, snap, sizeof(snap), 0);
    CHECK(has(snap, "\"phase\":\"done\"") && has(snap, "\"result\":{\"ok\":true"));
    // a cancelable producer job polls the flag
    E.ui = true; uic_job_begin(&S, "copy", "Copy", true, id); E.ui = false;
    r = send("{\"do\":\"cancel\"}");
    CHECK(r.r == UR_TAKEN && uic_job_cancel_requested(&S, id));
    CHECK(send("{\"do\":\"cancel\",\"job\":\"j99\"}").r == UR_GONE);
    // S14: a question with a timeout takes its default when nobody answers; a remote answer before that wins
    fresh();
    UicQuestion q; memset(&q, 0, sizeof(q)); snprintf(q.text, sizeof(q.text), "Keep this mode?"); snprintf(q.def, sizeof(q.def), "no"); q.timeout_ms = 8000;
    D.asking = true; D.asked_item = "picture";
    fresh();
    E.ui = true; CHECK(uic_ask(&S, q, id)); CHECK(!uic_ask(&S, q, id)); E.ui = false;
    D.asking = true; D.asked_item = "picture";
    E.t += 7000; ui_pass();
    CHECK(S.q_open && D.acts.empty());
    E.t += 1500; ui_pass();
    CHECK(!S.q_open && D.acts.size() == 1 && D.acts[0] == "answer:no:picture");
    fresh();
    E.ui = true; uic_ask(&S, q, id); E.ui = false;
    D.asking = true; D.asked_item = "picture";
    r = send("{\"do\":\"answer\",\"choice\":\"yes\"}");
    E.t += 9000; ui_pass();
    CHECK(r.r == UR_TAKEN && D.acts.size() == 1 && D.acts[0] == "answer:yes:picture");
    // the domain closes the question itself (the screen's arm ended): once, with by and choice; a late yes finds none; the answer is never given to the domain
    fresh();
    E.ui = true; CHECK(uic_ask(&S, q, id)); E.ui = false;
    uint32_t ev0 = S.ev_count;
    E.ui = true; CHECK(uic_close(&S, "screen", "")); CHECK(!uic_close(&S, "screen", "")); E.ui = false;
    CHECK(!S.q_open && S.ev_count == ev0 + 1 && strstr(S.ev[(S.ev_head + S.ev_count - 1) % UIC_EVENTS].text, "\"by\":\"screen\""));
    r = send("{\"do\":\"answer\",\"choice\":\"yes\"}");
    CHECK(r.r == UR_GONE && D.acts.empty());
}

// M3: a question that the screen replaces by another (the two steps of Cores and ROMs, a page that changes under an open question) is a new question with a new id
static void t_two_step()
{
    printf("two-step questions\n");
    fresh();
    D.two_step = true; D.asking = true; D.asked_item = "step1";
    ui_pass();
    CHECK(S.q_open);
    std::string id1 = S.q_id;
    UicReply r = send("{\"do\":\"answer\",\"choice\":\"yes\"}");
    CHECK(r.r == UR_TAKEN);
    ui_pass();                                                      // the screen now asks the second question
    CHECK(S.q_open && std::string(S.q_id) != id1 && std::string(S.q.text) == "Run step2?");
    r = send(("{\"do\":\"answer\",\"q\":\"" + id1 + "\",\"choice\":\"yes\"}").c_str());
    CHECK(r.r == UR_GONE);                                          // the old id is closed
    r = send("{\"do\":\"answer\",\"choice\":\"yes\"}");
    CHECK(r.r == UR_TAKEN);
    ui_pass();
    CHECK(!S.q_open);
    CHECK(D.acts.size() == 2 && D.acts[0] == "answer:yes:step1" && D.acts[1] == "answer:yes:step2");
    // the same question still on the screen right after its answer does not open again
    fresh();
    D.two_step = false; D.asking = true; D.asked_item = "same";
    ui_pass();
    std::string idA = S.q_id;
    send("{\"do\":\"answer\",\"choice\":\"yes\"}");
    D.asking = true; D.asked_item = "same";                          // not cleared yet
    ui_pass();
    CHECK(!S.q_open);
    D.asking = false;
    ui_pass();
    D.asking = true; D.asked_item = "same";                          // a real new ask of the same text afterwards opens
    ui_pass();
    CHECK(S.q_open && std::string(S.q_id) != idA);
    // an open question the screen changes: closed, the new one opens under a new id
    D.asked_item = "other";
    std::string idB = S.q_id;
    ui_pass();
    CHECK(S.q_open && std::string(S.q_id) != idB && std::string(S.q.text) == "Run other?");
    D.two_step = false;
}

static void t_history()
{
    printf("history (epic D19d)\n");
    fresh();
    CHECK(send("{\"do\":\"history\",\"dir\":\"prev\"}").r == UR_GONE);
    uic_history_add(&S, "alpha"); uic_history_add(&S, "beta"); uic_history_add(&S, "beta"); uic_history_add(&S, "gamma");
    CHECK(S.hist_n == 3);
    UicReply r = send("{\"do\":\"history\",\"dir\":\"prev\"}");
    CHECK(r.r == UR_TAKEN && std::string(r.text) == "gamma" && D.text_set == "gamma");
    r = send("{\"do\":\"history\",\"dir\":\"prev\"}"); CHECK(std::string(r.text) == "beta");
    r = send("{\"do\":\"history\",\"dir\":\"prev\"}"); CHECK(std::string(r.text) == "alpha");
    r = send("{\"do\":\"history\",\"dir\":\"prev\"}"); CHECK(std::string(r.text) == "alpha");
    r = send("{\"do\":\"history\",\"dir\":\"next\"}"); CHECK(std::string(r.text) == "beta");
    r = send("{\"do\":\"history\",\"dir\":\"next\"}"); r = send("{\"do\":\"history\",\"dir\":\"next\"}"); CHECK(std::string(r.text) == "");
    for (int i = 0; i < 100; i++) { char b[20]; snprintf(b, sizeof(b), "e%d", i); uic_history_add(&S, b); }
    CHECK(S.hist_n == UIC_HISTORY && std::string(S.hist[UIC_HISTORY - 1]) == "e99");
    uic_history_clear(&S);
    CHECK(send("{\"do\":\"history\",\"dir\":\"prev\"}").r == UR_GONE);
    CHECK(!E.have_file);                                            // RAM only: nothing was written anywhere
}

static void t_silent()
{
    printf("a change that reached no uic_note\n");
    fresh();
    char snap[6000]; uint32_t r1, r2, r3;
    uic_snapshot(&S, snap, sizeof(snap), &r1);
    uic_snapshot(&S, snap, sizeof(snap), &r2);
    CHECK(r2 == r1);                                   // nothing changed: the same rev
    D.items.push_back("op:new");                       // the app changed something and told nobody
    uic_snapshot(&S, snap, sizeof(snap), &r3);
    CHECK(r3 > r2 && has(snap, "op:new") && has(snap, ("\"rev\":" + std::to_string(r3)).c_str()));
}

static void t_http()
{
    printf("routes\n");
    fresh();
    std::string tok = grant_token();
    int st;
    std::string b = http("GET", "/v1/ui/state", 0, &st);
    CHECK(st == 200 && has(b, "\"ui\":2") && has(b, "\"app\":\"core\"") && has(b, "\"rev\":") && has(b, "\"keys_owner\":\"menu\"") && has(b, "\"dialog\":null") && has(b, "\"jobs\":[]"));
    b = http("POST", "/v1/ui/intent", "{\"do\":\"activate\",\"id\":5,\"item\":\"op:reset\"}", &st);
    CHECK(st == 200 && has(b, "\"r\":\"asked\"") && has(b, "\"id\":5"));
    b = http("GET", "/v1/ui/state", 0, &st);
    CHECK(has(b, "\"dialog\":{\"q\":\"q1\"") && has(b, "\"destructive\":true"));
    b = http("POST", "/v1/ui/intent", "{\"do\":\"select\",\"item\":\"gone\"}", &st);
    CHECK(st == 409 || st == 404);
    b = http("POST", "/v1/ui/intent", "nonsense", &st);
    CHECK(st == 400);
    b = http("PUT", "/v1/ui/intent", "{}", &st);
    CHECK(st == 405);
    CHECK(http("GET", "/v1/other", 0, &st) == "" && st == -2);
    // a snapshot that does not fit answers an error object, not garbage
    char small[40]; uint32_t rev;
    CHECK(uic_snapshot(&S, small, sizeof(small), &rev) == -1);
}

int main()
{
    t_json();
    t_ids();
    t_token();
    t_dispatch();
    t_events();
    t_jobs();
    t_two_step();
    t_history();
    t_silent();
    t_http();
    printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
