// T192.1 MVP: the core application's UI service on the real MenuCtl and the A1200 descriptor - state with stable ids, intents through the menu's own key path,
// questions for destructive actions (menu and REST), the token on the routes. The UI task is simulated: g_ui is true while the harness runs menu-task code,
// and the network side's waits pump uiapp_pass() the way run.cc's loop would.
#include <string>
#include <vector>
#include <set>
#include "check.h"
#include "fakes.h"
#include "cores.h"
#include "menuctl.h"
#include "keyboard.h"
#include "ops_glue.h"
#include "rest.h"
#include "rpc.h"
#include "uiapp.h"
#include "uic.h"
#include "monsrv.h"

struct Host : MenuHost {
    void info(int, char *out, int) { out[0] = 0; }
    bool return_to_host(char *, int) { return false; }
};

static bool g_ui = false;
static MenuCtl *M;
static bool g_open = false;
static Host HOST;

static void pl_lock(void) {}
static void pl_unlock(void) {}
static bool pl_ui(void) { return g_ui; }
static void pl_sleep(int);
static int pl_entropy(uint8_t *o, int n) { for (int i = 0; i < n && i < 12; i++) o[i] = (uint8_t)(i * 5 + 1); return n < 12 ? n : 12; }
static const UiAppPlatform PLAT = { pl_lock, pl_unlock, pl_ui, pl_sleep, pl_entropy };

static void pump()
{
    bool was = g_ui;
    g_ui = true;
    uiapp_pass(false);                                   // the service runs the controller's clock (MenuCtl::pump): a job's chunk per pass

    g_ui = was;
}
static void pl_sleep(int) { pump(); }

static bool env_open(void *) { return g_open; }
static void env_do_open(void *) { M->open(); g_open = true; }
static int g_gk_n, g_gk_last;
static void env_key_count(void *, int k) { g_gk_n++; g_gk_last = k; }
static void env_key(void *, int k) { if (M->key(k) == MenuCtl::R_CLOSE) g_open = false; }
static void env_intent(void *, const MenuIntent *in) { if (M->apply(*in) == MenuCtl::R_CLOSE) g_open = false; }
static bool env_false(void *) { return false; }
static bool g_appdlg = false;
static bool env_appdlg(void *) { return g_appdlg; }
static MenuCtl *env_active(void *) { return M; }
// T192.3 D23: the picture question and an application's dialog are the service's questions; these stand in for the run loop's and the dialog's side
static bool g_pic = false;
static bool env_pic(void *) { return g_pic; }
static int env_pic_left(void *) { return 8; }
static DlgView g_dv;
static int g_dlg_n, g_dlg_id, g_dlg_arg;
static void env_dlg_intent(void *, int k, DlgIntent *o) { o->id = DI_CHAR; o->arg = k; }
static void env_dlg_apply(void *, const DlgIntent *in) { g_dlg_n++; g_dlg_id = in->id; g_dlg_arg = in->arg; }
static const DlgView *env_dlg_view(void *) { return &g_dv; }

static std::string snap()
{
    static char b[16000];
    UicService *s = uiapp_service();
    uint32_t rev;
    int n = uic_snapshot(s, b, sizeof(b), &rev);
    return n > 0 ? std::string(b, n) : std::string("");
}
static bool has(const std::string &s, const std::string &x) { return s.find(x) != std::string::npos; }

static std::string send(const std::string &json, UicReply *rr = 0)
{
    UicIntent in; UicReply r;
    memset(&r, 0, sizeof(r));
    if (!uic_intent_parse(json.c_str(), (int)json.size(), &in)) { r.r = UR_BAD; }
    else uic_submit(uiapp_service(), in, &r);
    if (rr) *rr = r;
    return uic_r_name(r.r);
}

struct Row { std::string id, title; };
// the items of the snapshot: id and title of each entry of "items":[...]
static std::vector<Row> items(const std::string &s)
{
    std::vector<Row> v;
    size_t a = s.find("\"items\":[");
    if (a == std::string::npos) return v;
    size_t e = s.find("],\"focus\"", a);
    if (e == std::string::npos) e = s.size();
    size_t p = a;
    while ((p = s.find("{\"id\":\"", p)) != std::string::npos && p < e) {
        size_t q = s.find('"', p + 7);
        Row r; r.id = s.substr(p + 7, q - p - 7);
        size_t t = s.find("\"title\":\"", q);
        size_t nx = s.find("{\"id\":\"", q);
        if (t != std::string::npos && (nx == std::string::npos || t < nx)) { size_t te = s.find('"', t + 9); r.title = s.substr(t + 9, te - t - 9); }
        v.push_back(r);
        p = q;
    }
    return v;
}
static std::string id_of(const std::string &s, const std::string &title)
{
    for (auto &r : items(s)) if (r.title == title) return r.id;
    return "";
}
static std::string field(const std::string &s, const char *k)
{
    char b[300]; uic_json_get(s.c_str(), (int)s.size(), k, b, sizeof(b));
    return b;
}
static std::string qid_of(const std::string &s)
{
    size_t d = s.find("\"dialog\":");
    return d == std::string::npos ? "" : field(s.substr(d + 9), "q");
}

// ---- REST through the real parser ---------------------------------------------------------------------------------------------------------------------------
static CoreCtx *CTX;
static std::string http(const std::string &method, const std::string &target, const std::string &body = "", const std::string &hdr = "", int *status = 0)
{
    std::string raw = method + " " + target + " HTTP/1.1\r\nHost: x\r\n" + hdr;
    if (!body.empty()) raw += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    raw += "\r\n" + body;
    HttpReq *q = new HttpReq;
    http_init(q);
    http_feed(q, raw.data(), (int)raw.size());
    static char out[32000];
    bool ws = false;
    int n = q->state == HS_READY ? rest_serve(CTX, *q, out, sizeof(out), &ws) : -1;
    delete q;
    if (n < 0) { if (status) *status = -1; return ""; }
    if (status) *status = atoi(out + 9);
    const char *b = strstr(out, "\r\n\r\n");
    return b ? std::string(b + 4) : std::string("");
}

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);
    FakeIo io; FakeFs fs; CoreCtx c;
    memset(&c, 0, sizeof(c));
    c.desc = &amiga1200_desc; c.io = &io; c.fs = &fs;
    snprintf(c.cor_path, sizeof(c.cor_path), "/sd/cores/amiga1200.cor");
    Settings st(c.desc->settings, c.desc->nsettings, c.desc->store, &fs, "/sd/cores/amiga1200/amiga1200.cfg", 0);
    st.load();
    c.settings = &st;
    CTX = &c;
    fs.put("/usb0/games/x.adf", std::vector<uint8_t>(900, 1));
    fs.put("/usb0/games/y.adf", std::vector<uint8_t>(900, 2));
    M = new MenuCtl(&c, &HOST);
    UiAppEnv env = { 0, env_open, env_do_open, env_key, env_intent, env_pic, env_appdlg, env_active };
    env.dialog_key_intent = env_dlg_intent; env.dialog_apply = env_dlg_apply; env.dialog_view = env_dlg_view; env.picture_left_s = env_pic_left;
    // H3: a core that is neither the Amiga 1200 nor the TRX64U (another C64 core, VIC-20 ...) gets no service: no token, no /v1/ui/state, no asking
    {
        static CoreDesc other = amiga1200_desc;
        const char *names[] = { "TRX64 Ultimate (C64)", "C64 reference", "C64 SCPU", "VC-20 (PAL)" };
        for (const char *nm : names) {
            other.cor_name = nm;
            CoreCtx oc = c;
            oc.desc = &other;
            g_ui = true;
            uiapp_init(&oc, &env, &PLAT);
            g_ui = false;
            int st0 = 0;
            CTX = &oc;
            CHECK(uiapp_service() == 0 && rest_uic == 0 && rest_ui_op_destructive == 0 && rest_ui_op_ask == 0);
            {   // no service: the board's key still reaches give_key (regression: pc 0 when the env was only kept on the Amiga) and render says -1
                UiAppEnv ce = env;
                ce.give_key = env_key_count;
                uiapp_init(&oc, &ce, &PLAT);
                g_gk_n = 0;
                uiapp_local_key(0x41);
                CHECK_EQ(g_gk_n, 1); CHECK_EQ(g_gk_last, 0x41);
                char rb[64];
                CHECK_EQ(uiapp_render(rb, sizeof(rb)), -1);
            }
            http("GET", "/v1/info", "", "", &st0); CHECK_EQ(st0, 200);                  // no token needed
            http("GET", "/v1/ui/state", "", "", &st0);
            CHECK_EQ(st0, 404);                                                          // there is no v1 state any more (T192.3, Alex 2026-11-09)
            http("GET", "/v1/ui/hello", "", "", &st0); CHECK(st0 == 404 || st0 == 405 || st0 == 501);   // the service's routes are not there
            http("POST", "/v1/ui/grant", "", "", &st0); CHECK(st0 != 200);
        }
        {   // the TRX64U (the record menu) has the service: the T153 suite reads it through the service
            other.cor_name = "TRX64U";
            CoreCtx oc = c;
            oc.desc = &other;
            g_ui = true;
            uiapp_init(&oc, &env, &PLAT);
            g_ui = false;
            CHECK(uiapp_service() != 0 && rest_uic == uiapp_service());
            uiapp_reset_for_test();
        }
        // the run loop holds a merged COPY of the Amiga descriptor: it is the same core by name, so it gets the service
        {
            static CoreDesc merged = amiga1200_desc;
            CoreCtx mc = c;
            mc.desc = &merged;
            g_ui = true;
            uiapp_init(&mc, &env, &PLAT);
            g_ui = false;
            CHECK(uiapp_service() != 0 && rest_uic == uiapp_service());
            uiapp_reset_for_test();
        }
        CTX = &c;
    }
    g_ui = true;
    uiapp_init(&c, &env, &PLAT);
    g_ui = false;
    UicService *svc = uiapp_service();
    CHECK(svc != 0 && rest_uic == svc);

    // ---- the token: nothing but the three public routes without it (T192.1-S2, S14..S16) ----------------------------------------------------------------------------
    int st_;
    std::string b = http("GET", "/v1/info", "", "", &st_);
    CHECK_EQ(st_, 401);
    b = http("GET", "/v1/ui/hello", "", "", &st_);
    CHECK_EQ(st_, 200); CHECK(has(b, "\"grant\":true")); CHECK(has(b, "\"ui\":2")); CHECK(has(b, "\"app\":\"core\""));
    http("GET", "/", "", "", &st_); CHECK_EQ(st_, 200);
    b = http("POST", "/v1/ui/grant", "", "", &st_);
    CHECK_EQ(st_, 200);
    std::string tok = field(b, "token");
    CHECK_EQ(tok.size(), 32u);
    CHECK(fs.text("/sd/cores/amiga1200/ui.token").substr(0, 32) == tok);       // on the medium before the answer left
    http("POST", "/v1/ui/grant", "", "", &st_);
    CHECK_EQ(st_, 403);                                                           // a second fresh login does not get it
    http("GET", "/v1/info", "", "", &st_); CHECK_EQ(st_, 401);
    http("GET", "/v1/info?t=" + tok, "", "", &st_); CHECK_EQ(st_, 200);
    http("GET", "/v1/info", "", "Authorization: Bearer " + tok + "\r\n", &st_); CHECK_EQ(st_, 200);
    http("GET", "/v1/info", "", "X-Trx-Token: " + tok + "\r\n", &st_); CHECK_EQ(st_, 200);
    std::string bad = tok; bad[31] = bad[31] == '0' ? '1' : '0';
    http("GET", "/v1/info?t=" + bad, "", "", &st_); CHECK_EQ(st_, 401);
    // the table walk: every route of the adapter, with no token, is refused; with it, none is
    {
        std::set<std::string> paths = { "/v1/info", "/v1/ops", "/v1/ops/x", "/v1/state", "/v1/state/x", "/v1/events", "/v1/ui/state", "/v1/ui/intent", "/v1/report", "/v1/ws", "/v1/ui/ws", "/v1/boot" };
        for (int i = 0; i < rest_naliases; i++) {
            std::string p = rest_aliases[i].pattern;
            for (size_t k; (k = p.find('{')) != std::string::npos;) p.replace(k, p.find('}') - k + 1, "x");
            paths.insert(p);
        }
        int refused = 0;
        for (auto &p : paths) {
            http("GET", p, "", "", &st_); if (st_ == 401) refused++;
            http("POST", p, "", "", &st_); if (st_ == 401) refused++;
            http("GET", p + "?t=" + tok, "", "", &st_); CHECK(st_ != 401);
        }
        CHECK_EQ(refused, (int)paths.size() * 2);
    }
    b = http("GET", "/v1/ui/state?t=" + tok, "", "", &st_);
    CHECK(st_ == 200 && has(b, "\"ui\":2") && !has(b, "\"v\":1") && !has(b, "\"seq\""));   // the only state is the service's (T192.3, Alex 2026-11-09)

    // ---- state: the menu is closed, then open; ids are content, not position (S4) ---------------------------------------------------------------------------------------
    std::string s0 = snap();
    CHECK(has(s0, "\"menu_open\":false") && has(s0, "\"keys_owner\":\"closed\""));
    CHECK_STR(send("{\"do\":\"activate\",\"item\":\"x\"}").c_str(), "blocked");
    UicReply r;
    CHECK_STR(send("{\"do\": \"open\", \"menu\": true, \"id\": 50123}", &r).c_str(), "taken");   // as python's json.dumps writes it
    CHECK(g_open);
    std::string s1 = snap();
    CHECK(has(s1, "\"menu_open\":true") && has(s1, "\"ui\":2") && has(s1, "\"keys_owner\":\"menu\""));
    std::vector<Row> it = items(s1);
    CHECK(it.size() > 2);
    std::set<std::string> uniq;
    for (auto &x : it) { uniq.insert(x.id); CHECK(uic_id_valid(x.id.c_str())); }
    CHECK_EQ(uniq.size(), it.size());
    std::string s1b = snap();
    std::vector<Row> it2 = items(s1b);
    CHECK_EQ(it.size(), it2.size());
    for (size_t i = 0; i < it.size() && i < it2.size(); i++) CHECK_STR(it[i].id.c_str(), it2[i].id.c_str());
    CHECK(field(s1, "focus").size() > 0);

    // ---- the key intent is the board's key (S7): the same script, local and remote, ends in the same place --------------------------------------------------------------
    auto sig = [&] { char b2[200]; snprintf(b2, sizeof(b2), "%d/%d/%d/%s", M->area(), M->tab(), M->selected(), M->title()); return std::string(b2); };
    const char *script[] = { "f6", "down", "down", "1", "f5", "down", "f8", "up", "f1", "down" };
    auto code_of = [](const char *k) { return !strcmp(k, "f6") ? KEY_F6 : !strcmp(k, "f3") ? KEY_F3 : !strcmp(k, "f5") ? KEY_F5 : !strcmp(k, "f8") ? KEY_F8 : !strcmp(k, "up") ? KEY_UP : !strcmp(k, "f1") ? KEY_F1 : !strcmp(k, "down") ? KEY_DOWN : !strcmp(k, "ret") ? KEY_RETURN : !strcmp(k, "back") ? KEY_ESCAPE : (int)k[0]; };
    g_ui = true; M->rebuild(); M->open();
    for (const char *k : script) { uiapp_local_key(code_of(k)); if (getenv("DBG")) printf("local %s %s\n", k, sig().c_str()); }
    g_ui = false;
    std::string via_local = sig();
    g_ui = true; M->rebuild(); M->open(); g_ui = false;
    for (const char *k : script) { std::string rr = send(std::string("{\"do\":\"key\",\"key\":\"") + k + "\"}"); if (getenv("DBG")) printf("key %s -> %s  %s\n", k, rr.c_str(), sig().c_str()); }
    CHECK_STR(sig().c_str(), via_local.c_str());

    // ---- settings: set honours the hold (S9) --------------------------------------------------------------------------------------------------------------------------------
    g_ui = true; M->open(); g_ui = false;
    const Setting &s0set = c.desc->settings[0];
    std::string key = s0set.key;
    int cur = st.value(0);
    const char *other = s0set.values[(cur + 1) % s0set.nvalues];
    CHECK_STR(send("{\"do\":\"set\",\"key\":\"" + key + "\",\"value\":\"" + other + "\"}").c_str(), "taken");
    CHECK_EQ(st.value(0), (cur + 1) % s0set.nvalues);
    st.set_autosave(0);                                                           // Auto Save Off: a change waits for Save
    CHECK_STR(send("{\"do\":\"set\",\"key\":\"" + key + "\",\"value\":\"" + s0set.values[cur] + "\"}").c_str(), "held");
    CHECK(has(snap(), "\"unsaved\":true"));
    st.set_autosave(2);
    CHECK_STR(send("{\"do\":\"set\",\"key\":\"No Such Setting\",\"value\":\"x\"}").c_str(), "gone");

    // ---- the library: mount, folders, file select; delete asks (S6) -----------------------------------------------------------------------------------------------------
    send("{\"do\":\"key\",\"key\":\"f3\"}");
    std::string sl = snap();
    if (getenv("DBG")) printf("F3: %s\n", sl.c_str());
    CHECK(has(sl, "\"path\":\"/\""));
    std::string usb = id_of(sl, "usb0");
    if (usb.empty()) usb = id_of(sl, "usb0");
    CHECK(!usb.empty());
    CHECK_STR(send("{\"do\":\"activate\",\"item\":\"" + usb + "\"}").c_str(), "taken");
    sl = snap();
    std::string games = id_of(sl, "games");
    CHECK(!games.empty());
    send("{\"do\":\"activate\",\"item\":\"" + games + "\"}");
    sl = snap();
    CHECK(has(sl, "/usb0/games"));
    std::string fx = id_of(sl, "x.adf");
    std::string fy = id_of(sl, "y.adf");
    CHECK(!fx.empty() && !fy.empty() && fx != fy);
    // the folder listing changes under it: the old id still lands on the same entry (S5); an id that is gone moves nothing
    fs.put("/usb0/games/a.adf", std::vector<uint8_t>(900, 3));
    g_ui = true; M->relist(); g_ui = false;
    CHECK_STR(send("{\"do\":\"select\",\"item\":\"" + fx + "\"}").c_str(), "taken");
    CHECK(has(snap(), "\"focus\":\"" + fx + "\""));
    int selbefore = M->selected();
    CHECK_STR(send("{\"do\":\"select\",\"item\":\"fs:nonexistent\"}").c_str(), "gone");
    CHECK_EQ(M->selected(), selbefore);
    // RETURN on a file opens its sheet: the actions are items with ids
    send("{\"do\":\"activate\",\"item\":\"" + fx + "\"}");
    sl = snap();
    if (getenv("DBG")) printf("TAB: %s\n", sl.c_str());
    CHECK(M->chooser());
    std::string del = id_of(sl, "Delete");
    CHECK(!del.empty());
    CHECK_STR(send("{\"do\":\"activate\",\"item\":\"" + del + "\"}", &r).c_str(), "asked");
    pump();
    CHECK(fs.files.count("/usb0/games/x.adf") == 1);                              // nothing ran before the answer
    sl = snap();
    CHECK(has(sl, "\"dialog\":{\"q\":") && has(sl, "Delete") && has(sl, "\"keys_owner\":\"dialog\""));
    // while the question is open nothing beneath it can be activated (S8, S15)
    CHECK_STR(send("{\"do\":\"activate\",\"item\":\"" + fy + "\"}").c_str(), "blocked");
    std::string qid = qid_of(sl);
    CHECK(!qid.empty());
    CHECK(has(sl, "\"id\":\"no\",\"label\":\"No\",\"key\":\"esc\""));       // L8: what the snapshot advertises is what an answer presses
    CHECK_STR(send("{\"do\":\"answer\",\"q\":\"" + qid + "\",\"choice\":\"no\"}").c_str(), "taken");
    pump();
    CHECK(fs.files.count("/usb0/games/x.adf") == 1);                              // no: the file stays
    CHECK_STR(send("{\"do\":\"answer\",\"q\":\"" + qid + "\",\"choice\":\"yes\"}").c_str(), "gone");   // the late answer is nobody's
    CHECK(fs.files.count("/usb0/games/x.adf") == 1);
    // ask again and answer yes: a job runs, then the file is gone
    send("{\"do\":\"activate\",\"item\":\"" + fx + "\"}");
    sl = snap();
    send("{\"do\":\"activate\",\"item\":\"" + id_of(sl, "Delete") + "\"}", &r);
    pump();
    qid = qid_of(snap());
    CHECK_STR(send("{\"do\":\"answer\",\"q\":\"" + qid + "\",\"choice\":\"yes\"}").c_str(), "taken");
    for (int i = 0; i < 60; i++) pump();
    CHECK(fs.files.count("/usb0/games/x.adf") == 0);
    CHECK(fs.files.count("/usb0/games/y.adf") == 1);
    CHECK(has(snap(), "\"phase\":\"done\""));
    // the screen answers first: <- on the screen, then the remote answer is gone
    send("{\"do\":\"key\",\"key\":\"f3\"}");
    sl = snap();
    std::string fy2 = id_of(sl, "y.adf");
    CHECK(!fy2.empty());
    if (!fy2.empty()) {
        send("{\"do\":\"activate\",\"item\":\"" + fy2 + "\"}");
        sl = snap();
        send("{\"do\":\"activate\",\"item\":\"" + id_of(sl, "Delete") + "\"}");
        pump();
        qid = qid_of(snap());
        CHECK(!qid.empty());
        g_ui = true; M->key(KEY_ESCAPE); g_ui = false;                              // <- on the screen answers no
        pump();
        CHECK_STR(send("{\"do\":\"answer\",\"q\":\"" + qid + "\",\"choice\":\"yes\"}").c_str(), "gone");
        CHECK(fs.files.count("/usb0/games/y.adf") == 1);
    }

    // ---- T192.3 S9: the intent and the key give the same question, and no operation runs before the answer -----------------------------------------------------------------------------
    {
        send("{\"do\":\"key\",\"key\":\"f3\"}");
        std::string fyn = id_of(snap(), "y.adf");
        CHECK(!fyn.empty());
        send("{\"do\":\"activate\",\"item\":\"" + fyn + "\"}");
        std::string delid = id_of(snap(), "Delete");
        CHECK_STR(send("{\"do\":\"activate\",\"item\":\"" + delid + "\"}").c_str(), "asked");
        char t1[200], t2[200];
        const char *k1 = M->dialog(t1, sizeof(t1));
        CHECK_STR(k1, "confirm");
        std::string text_by_intent = t1;
        pump();
        CHECK(fs.files.count("/usb0/games/y.adf") == 1);                          // no operation before the answer
        CHECK_STR(send("{\"do\":\"answer\",\"q\":\"" + qid_of(snap()) + "\",\"choice\":\"no\"}").c_str(), "taken");
        // the same row by the board's own keys: open the sheet again, move to Delete and press RETURN
        send("{\"do\":\"activate\",\"item\":\"" + fyn + "\"}");
        CHECK(M->chooser());
        int want = -1;
        for (int i = 0; i < M->chooser_count(); i++) if (!strcmp(M->chooser_label(i), "Delete")) want = i;
        CHECK(want >= 0);
        g_ui = true;
        for (int guard = 0; guard < 40 && M->selected() != want; guard++) uiapp_local_key(M->selected() < want ? KEY_DOWN : KEY_UP);
        uiapp_local_key(KEY_RETURN);
        g_ui = false;
        const char *k2 = M->dialog(t2, sizeof(t2));
        CHECK_STR(k2, "confirm");
        CHECK_STR(t2, text_by_intent.c_str());                                    // the same question, the same words
        pump();
        CHECK(fs.files.count("/usb0/games/y.adf") == 1);
        g_ui = true; uiapp_local_key(KEY_ESCAPE); g_ui = false;                   // the left arrow answers no
        pump();
        CHECK(fs.files.count("/usb0/games/y.adf") == 1);
    }
    // ---- T192.3 S16: the operation over REST and the menu ask the same question and end in the same state -------------------------------------------------------------------------------
    {
        send("{\"do\":\"key\",\"key\":\"f3\"}");
        std::string fyr = id_of(snap(), "y.adf");
        CHECK(!fyr.empty());
        CHECK_STR(send("{\"do\":\"select\",\"item\":\"" + fyr + "\"}").c_str(), "taken");
        int st_r = 0;
        std::string br = http("POST", "/v1/ops/files.delete_job?t=" + tok, "", "", &st_r);
        CHECK_EQ(st_r, 202);
        CHECK(has(br, "asked"));
        pump();
        char tq[200];
        CHECK_STR(M->dialog(tq, sizeof(tq)), "confirm");
        CHECK_STR(tq, "Delete y.adf?");                                              // the menu's own words for the same file
        CHECK(fs.files.count("/usb0/games/y.adf") == 1);                              // asked, not run
        CHECK_STR(send("{\"do\":\"answer\",\"q\":\"" + qid_of(snap()) + "\",\"choice\":\"yes\"}").c_str(), "taken");
        for (int i = 0; i < 60; i++) pump();
        CHECK(fs.files.count("/usb0/games/y.adf") == 0);                              // the same state the menu's delete leaves
        fs.put("/usb0/games/y.adf", std::vector<uint8_t>(900, 2));                    // the later scenarios use the file
        g_ui = true; M->relist(); g_ui = false;
    }
    // ---- T192.3 S10: the board's key wins over a remote intent that waits: the remote one is refused with "local input" ------------------------------------------------------------------
    {
        send("{\"do\":\"open\",\"menu\":true,\"target\":\"F5/settings\"}");
        g_ui = true; uiapp_local_key(KEY_DOWN); uiapp_local_key(KEY_DOWN); g_ui = false;
        UicService *sv = uiapp_service();
        int sel0 = M->selected();
        CHECK(sel0 >= 2);
        UicIntent rem; memset(&rem, 0, sizeof(rem)); rem.kind = UD_KEY; snprintf(rem.key, sizeof(rem.key), "down");
        sv->slot[0].in = rem; sv->slot[0].gen = sv->local_gen; sv->slot[0].state = 1;       // a remote key, queued and not yet applied
        g_ui = true; uiapp_local_key(KEY_UP); uiapp_pass(false); g_ui = false;
        CHECK(sv->slot[0].state == 2 && sv->slot[0].out.r == UR_BLOCKED && strstr(sv->slot[0].out.why, "local input") != 0);
        CHECK_EQ(M->selected(), sel0 - 1);                                          // only the person's key moved the cursor
        sv->slot[0].state = 0;
        // control: queued after the person's key, the remote one is applied
        g_ui = true; uiapp_local_key(KEY_UP); g_ui = false;
        sv->slot[0].in = rem; sv->slot[0].gen = sv->local_gen; sv->slot[0].state = 1;
        int sel1 = M->selected();
        g_ui = true; uiapp_pass(false); g_ui = false;
        CHECK(sv->slot[0].state == 2 && sv->slot[0].out.r == UR_TAKEN);
        CHECK_EQ(M->selected(), sel1 + 1);
        sv->slot[0].state = 0;
    }
    // ---- T192.3 S11: the snapshot carries what the top bar shows and the focus id, from the controller's state ----------------------------------------------------------------------
    {
        std::string sn = snap();
        CHECK(has(sn, "\"ui\":2") && has(sn, "\"rev\":") && has(sn, "\"keys_owner\":"));
        CHECK(has(sn, "\"topbar\":{\"core\":") && has(sn, "\"state\":\"") && has(sn, "\"media\":") && has(sn, "\"net\":"));
        std::string foc = field(sn, "focus"), fid = field(sn, "focus_id");
        CHECK(!foc.empty() && foc == fid);
        CHECK(has(sn, "\"unsaved\":") && has(sn, "\"footer\":{\"legend\":"));
    }
    // ---- T192.3 S12: the controller's changes are events: the page, the focus, the dirty mark, the media; a client that was not there reads the snapshot --------------------------------------
    {
        UicService *sv = uiapp_service();
        send("{\"do\":\"open\",\"menu\":true}");
        int v0 = st.value(0);
        st.set_autosave(2);
        st.save();                                                                 // nothing waits for a Save: the dirty mark starts clear
        g_ui = true; uiapp_pass(false); g_ui = false;
        CHECK(!M->unsaved());
        UicClient cl;
        uic_client_open(sv, &cl);
        char mb[8000];
        bool snapped = false;
        for (int i = 0; i < 10 && !snapped; i++) {                                 // the hello, then the snapshot once a pass has rendered it
            g_ui = true; uiapp_pass(false); g_ui = false;
            int nn;
            while ((nn = uic_client_next(sv, &cl, mb, sizeof(mb))) > 0) snapped |= has(std::string(mb, nn), "\"t\":\"snap\"");
        }
        CHECK(snapped);
        g_ui = true; uiapp_local_key(KEY_F3); uiapp_pass(false); g_ui = false;       // a page
        g_ui = true; uiapp_local_key(KEY_DOWN); uiapp_pass(false); g_ui = false;     // a focus
        st.set_autosave(0);
        g_ui = true; M->key(KEY_F5); M->key('2'); g_ui = false;
        std::string key0 = c.desc->settings[0].key;
        send("{\"do\":\"set\",\"key\":\"" + key0 + "\",\"value\":\"" + c.desc->settings[0].values[(st.value(0) + 1) % c.desc->settings[0].nvalues] + "\"}");
        g_ui = true; uiapp_pass(false); g_ui = false;                              // a dirty mark
        CHECK(has(snap(), "\"dirty\":true"));                                      // T192.3 S11: the row that was changed says so
        st.set_autosave(2);
        fs.put("/usb1/disk.adf", std::vector<uint8_t>(900, 7));                    // a medium that comes later
        g_ui = true; M->key(KEY_F3); M->relist(); uiapp_pass(false); g_ui = false;
        std::string all;
        int n;
        while ((n = uic_client_next(sv, &cl, mb, sizeof(mb))) > 0) all += std::string(mb, n) + "\n";
        CHECK(has(all, "\"t\":\"page\""));
        CHECK(has(all, "\"t\":\"focus\""));
        if (!has(all, "\"t\":\"dirty\"")) {
            printf("events: %s\nring: count %u head %u floor %u client ev_rev %u rev %u\n", all.c_str(), (unsigned)sv->ev_count, (unsigned)sv->ev_head, (unsigned)sv->ev_floor, (unsigned)cl.ev_rev, (unsigned)sv->rev);
            for (uint32_t i = 0; i < sv->ev_count; i++) printf("  ev %u: %s\n", (unsigned)sv->ev[(sv->ev_head + i) % UIC_EVENTS].rev, sv->ev[(sv->ev_head + i) % UIC_EVENTS].text);
        }
        CHECK(has(all, "\"t\":\"dirty\""));
        CHECK(has(all, "\"t\":\"media\""));
        uic_client_close(sv, &cl);
        st.set(0, v0);                                                              // the later scenarios start from the value they found
    }

    // ---- T192.3 D23: the picture question is a service question with its timeout; any key on the screen is its answer "yes" ----------------------------------------------------------------
    {
        send("{\"do\":\"open\",\"menu\":true}");
        int sel0 = M->selected();
        g_pic = true;
        pump();
        std::string sp = snap();
        CHECK(has(sp, "\"keys_owner\":\"picture\""));
        CHECK(has(sp, "Keep this picture?") && has(sp, "\"default\":\"no\"") && has(sp, "\"timeout_ms\":8000"));
        CHECK_STR(send("{\"do\":\"activate\",\"item\":\"x\"}").c_str(), "blocked");        // nothing beneath it can be activated
        g_ui = true; uiapp_local_key(KEY_DOWN); g_ui = false;                         // the board's key: the answer yes, not a menu key
        CHECK_EQ(M->selected(), sel0);
        g_pic = false;
        pump();
        CHECK(has(snap(), "\"dialog\":null"));
    }
    // ---- an application's yes/no dialog is a service question too; its other kinds are the `appdlg` object; every key of it is an intent of the dialog's own table --------------------------------
    {
        memset(&g_dv, 0, sizeof(g_dv));
        g_dv.kind = DK_BUTTONS; g_dv.buttons = SVC_BUTTON_YES | SVC_BUTTON_NO; g_dv.remaining_s = 5;
        snprintf(g_dv.title, sizeof(g_dv.title), "Write"); snprintf(g_dv.text, sizeof(g_dv.text), "Overwrite the file?");
        g_appdlg = true;
        pump();
        std::string sd = snap();
        CHECK(has(sd, "\"keys_owner\":\"dialog\"") && has(sd, "\"appdlg\":{\"kind\":\"buttons\"") && has(sd, "Overwrite the file?") && has(sd, "\"timeout_ms\":5000"));
        g_dlg_n = 0;
        g_ui = true; uiapp_local_key('q'); g_ui = false;
        CHECK_EQ(g_dlg_n, 1); CHECK_EQ(g_dlg_id, DI_CHAR); CHECK_EQ(g_dlg_arg, 'q');
        std::string qd = qid_of(snap());
        CHECK(!qd.empty());
        CHECK_STR(send("{\"do\":\"answer\",\"q\":\"" + qd + "\",\"choice\":\"yes\"}").c_str(), "taken");
        CHECK_EQ(g_dlg_n, 2); CHECK_EQ(g_dlg_arg, 'y');
        CHECK_STR(send("{\"do\":\"key\",\"key\":\"down\"}").c_str(), "taken");        // a remote key is the dialog's intent as well
        CHECK_EQ(g_dlg_n, 3);
        g_appdlg = false;
        memset(&g_dv, 0, sizeof(g_dv));                                              // the later scenarios (the keys belong to a dialog) ask no yes/no question
        pump();
    }
    // ---- a text field: Rename opens one; `text` fills it, `submit` presses RETURN; the entry goes into the history (D19d) ------------------------------------------------------------------
    send("{\"do\":\"key\",\"key\":\"f3\"}");
    sl = snap();
    std::string fy3 = id_of(sl, "y.adf");
    CHECK(!fy3.empty());
    send("{\"do\":\"activate\",\"item\":\"" + fy3 + "\"}");
    CHECK_STR(send("{\"do\":\"activate\",\"item\":\"" + id_of(snap(), "Rename") + "\"}").c_str(), "taken");
    sl = snap();
    CHECK(has(sl, "\"modal\":{\"kind\":\"text\"") && has(sl, "\"keys_owner\":\"dialog\""));
    CHECK_STR(send("{\"do\":\"text\",\"value\":\"z.adf\",\"submit\":true}").c_str(), "taken");
    pump();
    CHECK(fs.files.count("/usb0/games/z.adf") == 1 && fs.files.count("/usb0/games/y.adf") == 0);
    CHECK_STR(send("{\"do\":\"text\",\"value\":\"nope\"}").c_str(), "refused");        // no field is open now
    CHECK(uiapp_service()->hist_n == 1 && std::string(uiapp_service()->hist[0]) == "z.adf");
    // the history recalls it into the next field
    send("{\"do\":\"activate\",\"item\":\"" + id_of(snap(), "z.adf") + "\"}");
    send("{\"do\":\"activate\",\"item\":\"" + id_of(snap(), "Rename") + "\"}");
    CHECK_STR(send("{\"do\":\"history\",\"dir\":\"prev\"}").c_str(), "taken");
    CHECK(has(snap(), "z.adf"));
    send("{\"do\":\"key\",\"key\":\"back\"}");
    // the generic operation intent: a non-destructive operation runs at once with its arguments, a probe with an argument it has not is refused
    CHECK_STR(send("{\"do\":\"op\",\"item\":\"settings.get\",\"args\":{\"key\":\"" + key + "\"}}").c_str(), "taken");
    CHECK_STR(send("{\"do\":\"op\",\"item\":\"settings.get\",\"args\":{\"zz\":1}}").c_str(), "refused");
    CHECK_STR(send("{\"do\":\"op\",\"item\":\"no.such.op\"}").c_str(), "gone");

    // ---- REST: a destructive operation asks (D18, D19a) ----------------------------------------------------------------------------------------------------------------------------
    send("{\"do\":\"key\",\"key\":\"f6\"}");
    int before = st.value(0);
    int other_v = (before + 1) % s0set.nvalues;
    send("{\"do\":\"set\",\"key\":\"" + key + "\",\"value\":\"" + s0set.values[other_v] + "\"}");
    CHECK_EQ(st.value(0), other_v);
    b = http("POST", "/v1/ops/settings.reset_defaults?t=" + tok, "", "", &st_);
    CHECK_EQ(st_, 202); CHECK(has(b, "\"r\":\"asked\""));
    pump();
    CHECK_EQ(st.value(0), other_v);                                                // nothing ran
    sl = snap();
    CHECK(has(sl, "\"dialog\":{\"q\":") && has(sl, "\"destructive\":true"));
    b = http("POST", "/v1/ops/settings.reset_defaults?t=" + tok, "", "", &st_);   // a second one while the first is open
    CHECK_EQ(st_, 409);
    qid = qid_of(sl);
    b = http("POST", "/v1/ui/intent?t=" + tok, "{\"do\":\"answer\",\"q\":\"" + qid + "\",\"choice\":\"yes\"}", "", &st_);
    CHECK(st_ == 200 && has(b, "\"r\":\"taken\""));
    pump();
    CHECK_EQ(st.value(0), c.desc->settings[0].def);                                // the answer ran it
    // the screen can answer a REST question too
    send("{\"do\":\"set\",\"key\":\"" + key + "\",\"value\":\"" + s0set.values[other_v] + "\"}");
    b = http("POST", "/v1/ops/settings.reset_defaults?t=" + tok, "", "", &st_);
    CHECK_EQ(st_, 202);
    pump();
    g_ui = true; M->key(KEY_RETURN); g_ui = false;                                  // RETURN on the screen
    pump(); pump();
    CHECK_EQ(st.value(0), c.desc->settings[0].def);
    // a key that leaves the screen cancels a REST question too, and says so like <- does
    send("{\"do\":\"set\",\"key\":\"" + key + "\",\"value\":\"" + s0set.values[other_v] + "\"}");
    http("POST", "/v1/ops/settings.reset_defaults?t=" + tok, "", "", &st_);
    CHECK_EQ(st_, 202);
    pump();
    CHECK(M->remote_asking());
    g_ui = true; M->key(KEY_F3); g_ui = false;
    pump(); pump();
    CHECK(!M->remote_asking());
    CHECK_STR(M->status(), "Cancelled");
    CHECK_EQ(st.value(0), other_v);                                                // answered no: nothing ran
    CHECK(!snap().empty() && !has(snap(), "\"dialog\":{\"q\""));
    // a non-destructive op runs at once
    http("POST", "/v1/ops/settings.get?t=" + tok, "{\"key\":\"" + key + "\"}", "", &st_);
    CHECK(st_ == 200);

    // T192 review 3: a destructive ask answered busy because the keys belong to a dialog frees its record: the two records are not used up by refusals
    CHECK(g_open);
    g_appdlg = true;
    for (int i = 0; i < 3; i++) {
        b = http("POST", "/v1/ops/settings.reset_defaults?t=" + tok, "", "", &st_);
        CHECK_EQ(st_, 409);
        CHECK(has(b, "\"r\":\"busy\"") && has(b, "the keys belong to"));              // the intent's own busy, not the record shortage
    }
    g_appdlg = false;
    b = http("POST", "/v1/ops/settings.reset_defaults?t=" + tok, "", "", &st_);       // the clock did not move: no stale reclaim helped
    CHECK_EQ(st_, 202);
    pump();
    g_ui = true; M->key(KEY_F3); g_ui = false;
    pump(); pump();
    CHECK(!M->remote_asking());

    // ---- M1: no network path runs a destructive operation past the gate (REST, the JSON-RPC socket, the contract's reset)
    {
        OpResult res;
        JsonObj none;
        svc_call_net(&c, "machine.cold_reset", &none, &res);
        CHECK(res.code != OPE_OK && strstr(res.msg, "asks for a confirmation first") != 0);
        svc_call_net(&c, "machine.warm_reset", &none, &res);
        CHECK(res.code != OPE_OK && strstr(res.msg, "asks for a confirmation first") != 0);
        svc_call_net(&c, "settings.get", &none, &res);
        CHECK(strstr(res.msg, "asks for a confirmation first") == 0);          // an ordinary operation is called as before
        RpcSession *rs = new RpcSession;
        rpc_open(rs, &c);
        char out[1500];
        const char *m1 = "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"session/reset\",\"params\":{\"mode\":\"cold\"}}";
        int n = rpc_handle(rs, m1, (int)strlen(m1), out, sizeof(out));
        CHECK(n > 0 && strstr(out, "asks for a confirmation first") != 0);
        const char *m2 = "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"session/power\",\"params\":{\"op\":\"on\"}}";
        n = rpc_handle(rs, m2, (int)strlen(m2), out, sizeof(out));
        CHECK(n > 0 && strstr(out, "asks for a confirmation first") != 0);
        rpc_close(rs);
        delete rs;
    }

    // T192 review 1 and 2: the monitor socket refuses every restore/goto of the ring state while the service is up, and an id or method is escaped in the error
    CHECK(monsrv_gated("checkpoint/restore") && monsrv_gated("transport/goto") && monsrv_gated("mark/goto") && monsrv_gated("runtime/reverse_step"));
    CHECK(monsrv_gated("monitor/exec") && monsrv_gated("media/mount") && monsrv_gated("sandbox/run"));
    CHECK(!monsrv_gated("ping") && !monsrv_gated("transport/status") && !monsrv_gated("mark/list") && !monsrv_gated("checkpoint/list"));
    {
        char o[64];
        monsrv_json_esc("a\"b\\c\nd", o, sizeof(o));
        CHECK_STR(o, "a\\\"b\\\\c\\u000ad");
    }

    // ---- the token file deleted: the next login is a new token, the old one is dead (S17) ---------------------------------------------------------------------------------------------
    fs.remove("/sd/cores/amiga1200/ui.token");
    io.t += 2500;
    pump();
    io.t += 2500;
    pump();
    http("GET", "/v1/info?t=" + tok, "", "", &st_); CHECK_EQ(st_, 401);
    b = http("POST", "/v1/ui/grant", "", "", &st_);
    CHECK_EQ(st_, 200);
    std::string tok2 = field(b, "token");
    CHECK(tok2.size() == 32 && tok2 != tok);
    http("GET", "/v1/info?t=" + tok2, "", "", &st_); CHECK_EQ(st_, 200);
    http("GET", "/v1/info?t=" + tok, "", "", &st_); CHECK_EQ(st_, 401);
    DONE("uiapp");
}
