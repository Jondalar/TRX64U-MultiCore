// T192 D21: the host's API from its declaration (hostdecl.cc), walked against a fake model, with the real service (uicore) underneath.
//  - coverage: every page, item, list, row action, keymap entry names ids and actions that exist; every declared action reaches the model through an intent
//    (the key path or a field step), the destructive ones ask first; nothing the keymap or the lists name is missing from the action table.
//  - behaviour: select / activate / set / op / key / open, the confirmation gate (D19a), a flow's page as a question, the text entry (armed text, typed text),
//    a pick list, the job, the key queue order (navigation before focus before keys).
// The "UI task" is simulated as in tests/uicore: the network side's waits pump hostapi_pass().
#include "hostdomain.h"
#include "hostdecl.h"
#include "host_core.h"
#include "keyboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <set>

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) { g_pass++; } else { g_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_S(a, b) do { std::string x_ = (a), y_ = (b); if (x_ == y_) { g_pass++; } else { g_fail++; printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, x_.c_str(), y_.c_str()); } } while (0)

static bool g_ui = false;

// ---- the fake model ---------------------------------------------------------------------------------------------------------------------
struct Fake : HostModel {
    int v = HVW_MENU, md = HMODE_NORMAL;
    bool wifi = false, cores = false, pic = false;
    std::vector<std::string> entries{ "Amiga 1200", "VIC-20", "VIC-20" };      // two entries share a name: the ids must still differ
    std::vector<std::string> files{ "..", "cores", "a.cor", "b.cfw", "x.txu" };
    int esel = 1, fsel = 2;
    std::vector<std::string> log;                                              // what the model was asked to do, in order
    std::vector<int> keys;                                                     // keys pushed to the model
    std::string path = "/SD";
    bool direct = false;
    int view() override { return v; }
    int mode() override { return md; }
    bool wifi_page() override { return wifi; }
    bool cores_page() override { return cores; }
    bool picture_pending() override { return pic; }
    bool key(int k) override { keys.push_back(k); return true; }
    int rows(const char *s) override { return !strcmp(s, "entry") ? (int)entries.size() : !strcmp(s, "file") ? (int)files.size() : !strcmp(s, "cmline") ? 2 : 0; }
    bool row(const char *s, int i, HRow *r) override {
        memset(r, 0, sizeof(*r));
        r->enabled = true;
        if (!strcmp(s, "entry") && i < (int)entries.size()) {
            snprintf(r->ident, sizeof(r->ident), "%s", entries[i].c_str());
            snprintf(r->title, sizeof(r->title), "%s", entries[i].c_str());
            snprintf(r->badge, sizeof(r->badge), "hash ok");
            r->startable = true;
            return true;
        }
        if (!strcmp(s, "file") && i < (int)files.size()) {
            snprintf(r->ident, sizeof(r->ident), "%s", files[i].c_str());
            snprintf(r->title, sizeof(r->title), "%s", files[i].c_str());
            r->dir = i < 2;
            return true;
        }
        if (!strcmp(s, "cmline") && i < 2) {
            snprintf(r->ident, sizeof(r->ident), "%d", i);
            snprintf(r->title, sizeof(r->title), "line %d", i);
            return true;
        }
        return false;
    }
    int cursor(const char *s) override { return !strcmp(s, "entry") ? esel : !strcmp(s, "file") ? fsel : 0; }
    void focus(const char *s, int i) override {
        log.push_back(std::string("focus ") + s + " " + std::to_string(i));
        if (!strcmp(s, "entry")) esel = i; else if (!strcmp(s, "file")) fsel = i;
    }
    bool field(const char *k, HField *f) override {
        memset(f, 0, sizeof(*f));
        f->visible = true;
        f->enabled = true;
        std::string key = k;
        if (key == "settings.secs") f->visible = false;                          // autostart is off in this state
        if (key == "settings.direct") { snprintf(f->value, sizeof(f->value), "%s", direct ? "on" : "off"); f->on = direct; }
        else snprintf(f->value, sizeof(f->value), "v:%s", k);
        return true;
    }
    void field_focus(const char *k) override { log.push_back(std::string("ffocus ") + k); }
    int field_set(const char *k, const char *val, char *why, int n) override {
        log.push_back(std::string("set ") + k + "=" + val);
        if (!strcmp(val, "bad")) { snprintf(why, n, "refused by the model"); return 1; }
        if (!strcmp(k, "settings.direct")) direct = !strcmp(val, "on");
        return 0;
    }
    int field_step(const char *k, int dir, char *, int) override { log.push_back(std::string("step ") + k + " " + std::to_string(dir)); return 0; }
    bool text(const char *what, char *out, int n) override {
        out[0] = 0;
        if (!strcmp(what, "path") && v == HVW_BROWSE) { snprintf(out, n, "%s", path.c_str()); return true; }
        if (!strcmp(what, "bpath")) { snprintf(out, n, "%s", path.c_str()); return true; }
        if (!strcmp(what, "footer")) { snprintf(out, n, "UP/DOWN select"); return true; }
        if (!strcmp(what, "note")) { snprintf(out, n, "ready"); return true; }
        if (!strcmp(what, "cursor") && v == HVW_SET) { snprintf(out, n, "settings.direct"); return true; }
        return false;
    }
    int browse_goto(const char *p, char *why, int n) override {
        if (p[1] == 'x') { snprintf(why, n, "not there"); return 1; }
        log.push_back(std::string("goto ") + p);
        return 0;
    }
    // ---- the arm the screen owns (T192 D21), as hostui.cc keeps it: a token, ended by a key, a page change, a reload
    uint32_t tok = 0, seq = 0;
    int arm_act = 0, arm_row = -1;
    std::string arm_name;
    std::string bail;                                                          // the first key bails with these words (boot.cfg, a folder with files, no WiFi module)
    std::string busy;                                                          // a modal state: the screen does not leave it
    std::vector<std::string> ran;                                              // what the screen executed
    int arm(int act, int row, const char *ident, uint32_t *token, char *why, int n) override {
        log.push_back("arm " + std::to_string(act) + " " + std::to_string(row) + " " + ident);
        if (!busy.empty()) { snprintf(why, n, "%s", busy.c_str()); return HARM_BUSY; }
        end_arm(HARM_END_GONE);
        v = act == HARM_ENTRY_REMOVE ? HVW_EDIT : act == HARM_FILE_DELETE ? HVW_BROWSE : HVW_SET;
        if (act == HARM_ENTRY_REMOVE) esel = row; else if (act == HARM_FILE_DELETE) fsel = row; else wifi = true;
        if (!bail.empty()) { snprintf(why, n, "%s", bail.c_str()); return HARM_REFUSED; }
        tok = ++seq; arm_act = act; arm_row = row; arm_name = ident;
        *token = tok;
        return HARM_ARMED;
    }
    bool confirm(uint32_t token, char *why, int n) override {
        if (!tok || tok != token) { snprintf(why, n, "Not done: the screen is not armed for it any more"); return false; }
        tok = 0;
        log.push_back("confirm");
        ran.push_back(arm_name.empty() ? "wifi.forget" : arm_name);
        return true;
    }
    void disarm(uint32_t token) override { if (tok && tok == token) { tok = 0; log.push_back("disarm"); } }
    bool armed(uint32_t token) override { return tok && tok == token; }
    void end_arm(int how) {                                                    // the screen ends its arm by itself
        if (!tok) return;
        uint32_t t = tok;
        tok = 0;
        bool was = g_ui;
        g_ui = true;
        hostapi_arm_end(t, how);
        g_ui = was;
    }
    // a key as on_key meets it: the confirming key runs the action (yes), another key ends the arm (no from the board, gone from the queue)
    void press(int k, bool local) {
        if (!tok) return;
        bool confirming = k == KEY_DELETE || k == KEY_BACK;
        if (confirming) ran.push_back(arm_name);
        end_arm(confirming ? HARM_END_YES : local ? HARM_END_NO : HARM_END_GONE);
    }
};

static Fake M;
static uint32_t g_t = 1000;
static std::string g_file;
static bool g_have_file = false;

static bool g_in_flow = false;                  // a flow waits in a nested loop: the service runs from its wait, not from the main loop
static void pump() { bool was = g_ui; g_ui = true; if (g_in_flow) hostapi_waiting(); else hostapi_pass(false); g_ui = was; }

static void p_lock(void) {}
static void p_unlock(void) {}
static bool p_ui(void) { return g_ui; }
static void p_sleep(int ms) { g_t += ms; pump(); }
static int p_entropy(uint8_t *o, int n) { for (int i = 0; i < n && i < 16; i++) o[i] = (uint8_t)(i * 5 + 1); return n < 16 ? n : 16; }
static uint32_t p_now(void) { return g_t; }
static int p_load(char *b, int cap) { if (!g_have_file) return -1; int n = (int)g_file.size(); if (n > cap) n = cap; memcpy(b, g_file.data(), n); return n; }
static bool p_save(const char *b, int n) { g_file.assign(b, n); g_have_file = true; return true; }
static bool p_remove(void) { g_have_file = false; return true; }

// ---- helpers -----------------------------------------------------------------------------------------------------------------------------
static std::string g_sub[5][2] = { { "@A@", "" }, { "@V1@", "" }, { "@V2@", "" }, { "@PG@", "" }, { "@PO@", "" } };

static UicReply submit(const char *js_in)
{
    std::string js_s = js_in;
    for (auto &p : g_sub) {
        size_t at;
        while (!p[1].empty() && (at = js_s.find(p[0])) != std::string::npos) js_s.replace(at, p[0].size(), p[1]);
    }
    const char *js = js_s.c_str();
    UicIntent in;
    CHECK(uic_intent_parse(js, (int)strlen(js), &in));
    UicReply r;
    uic_submit(hostapi_service(), in, &r);
    return r;
}

static std::string snap()
{
    static char b[20000];
    uint32_t rev;
    int n = uic_snapshot(hostapi_service(), b, sizeof(b), &rev);
    CHECK(n > 0);
    return n > 0 ? std::string(b, n) : std::string();
}

static bool has(const std::string &s, const char *needle) { return s.find(needle) != std::string::npos; }

static void drain_keys() { M.keys.clear(); M.log.clear(); int k; while ((k = hostapi_pop_key()) >= 0) { M.keys.push_back(k); } }
static std::vector<int> popped() { std::vector<int> v; int k; while ((k = hostapi_pop_key()) >= 0) v.push_back(k); return v; }

static std::string reply_r(const UicReply &r) { return uic_r_name(r.r); }

static UicReply answer_q(const char *choice, const char *q)
{
    UicIntent an;
    memset(&an, 0, sizeof(an));
    an.kind = UD_ANSWER;
    snprintf(an.choice, sizeof(an.choice), "%s", choice);
    snprintf(an.q, sizeof(an.q), "%s", q);
    UicReply ar;
    uic_submit(hostapi_service(), an, &ar);
    return ar;
}

// The `closed` events of question `q` in the ring: how many, and by whom with which choice (the last one).
static int closed_count(const std::string &q)
{
    int n = 0;
    for (uint32_t i = 0; i < hostapi_service()->ev_count; i++) {
        const char *t = hostapi_service()->ev[(hostapi_service()->ev_head + i) % UIC_EVENTS].text;
        if (strstr(t, "\"t\":\"closed\"") && strstr(t, (std::string("\"q\":\"") + q + "\"").c_str())) n++;
    }
    return n;
}
static std::string closed_field(const std::string &q, const char *field)
{
    std::string out;
    for (uint32_t i = 0; i < hostapi_service()->ev_count; i++) {
        const char *t = hostapi_service()->ev[(hostapi_service()->ev_head + i) % UIC_EVENTS].text;
        if (!strstr(t, "\"t\":\"closed\"") || !strstr(t, (std::string("\"q\":\"") + q + "\"").c_str())) continue;
        const char *f = strstr(t, (std::string("\"") + field + "\":\"").c_str());
        if (f) {
            f += strlen(field) + 4;
            out.assign(f, strchr(f, '"') - f);
        }
    }
    return out;
}
static std::string closed_by(const std::string &q) { return closed_field(q, "by"); }
static std::string closed_choice(const std::string &q) { return closed_field(q, "choice"); }

// A key at the board as host_getch_nowait does it: the dispatcher, then the queue, then the screen takes it.
static void board_key(int k)
{
    hostapi_local_key(k);
    int q = hostapi_pop_key();
    if (q >= 0) M.press(q, hostapi_key_local());
    pump();
}

// The id of the nth row (0 based) whose title is `title` in the snapshot: the ids come from the framework's own rule, so the test reads them.
static std::string id_of(const std::string &s, const char *title, int nth = 0)
{
    std::string t = std::string("\"title\":\"") + title + "\"";
    size_t at = 0;
    for (int i = 0; i <= nth; i++) {
        at = s.find(t, i ? at + 1 : 0);
        if (at == std::string::npos) return "";
    }
    size_t idp = s.rfind("\"id\":\"", at);
    if (idp == std::string::npos) return "";
    idp += 6;
    return s.substr(idp, s.find('"', idp) - idp);
}

// ---- coverage: the declaration against itself ----------------------------------------------------------------------------------------
static void coverage_static()
{
    const HostDecl &d = host_decl();
    std::set<std::string> names;
    for (int i = 0; i < d.nactions; i++) {
        const HAction &a = d.actions[i];
        CHECK(a.name && a.name[0] && uic_id_valid(a.name));
        CHECK(names.insert(a.name).second);                                    // unique
        CHECK(a.title && a.title[0]);
        if (a.kind == HA_KEYS) CHECK(hc_act_by_name(a.name) != HCA_NONE);       // a key action is an intent of host_core
        if (a.kind == HA_FIELD) CHECK(a.field && a.field[0]);
        if (a.flags & HAF_ROW) CHECK(a.src != 0);                               // a row action names the list its row comes from
        if (a.flags & HAF_DESTRUCTIVE) CHECK(a.ask && a.ask[0]);                // a destructive action says what it asks
        if (a.flags & HAF_TEXT) CHECK(a.kind == HA_KEYS);
    }
    std::set<std::string> ids;
    for (int i = 0; i < d.nitems; i++) {
        char id[64];
        hd_item_id(d.items[i], id, sizeof(id));
        CHECK(uic_id_valid(id));                                                // every item has a valid id
        CHECK(ids.insert(id).second);                                           // ... and no two share it
        CHECK(hd_page(d, d.items[i].page) != 0);
        CHECK(d.items[i].field && d.items[i].field[0]);
        if (d.items[i].activate) CHECK(hd_action(d, d.items[i].activate) != 0);  // ... and what it activates exists
        if (d.items[i].kind == HI_TEXT || d.items[i].kind == HI_TOGGLE || d.items[i].kind == HI_VALUE || d.items[i].kind == HI_CHOICE) {
            // a settable item either has an activate action or takes a value through UD_SET (the model's field_set)
            CHECK(d.items[i].activate || d.items[i].kind == HI_TEXT);
        }
    }
    for (int i = 0; i < d.npages; i++) {
        CHECK(d.pages[i].id && d.pages[i].id[0]);
        if (d.pages[i].activate) CHECK(hd_action(d, d.pages[i].activate) != 0);
        if (d.pages[i].list) CHECK(d.pages[i].prefix && d.pages[i].prefix[0]);
        if (d.pages[i].nav) CHECK(hd_page_of_view(d, d.pages[i].view) == &d.pages[i]);
    }
    {
        int nk;
        const HcKey *km = hc_keymap(&nk);
        for (int i = 0; i < nk; i++) {
            CHECK(hc_act_name(km[i].act)[0]);                                    // every key of the one key map takes a named intent
        }
    }
    for (const char *const *a = d.row_actions_entry; *a; a++) CHECK(hd_action(d, *a) != 0);
    for (const char *const *a = d.row_actions_edit; *a; a++) CHECK(hd_action(d, *a) != 0);
    for (const char *const *a = d.row_actions_file; *a; a++) CHECK(hd_action(d, *a) != 0);
    // every action is offered somewhere: by a row list, by a page item, by the keymap, or globally
    for (int i = 0; i < d.nactions; i++) {
        const HAction &a = d.actions[i];
        bool seen = (a.flags & HAF_GLOBAL) != 0;
        if (a.kind == HA_KEYS) { for (int c = 0; c <= HCX_GLOBAL && !seen; c++) seen = hc_act_in_ctx(c, hc_act_by_name(a.name)); }     // a key of the key map reaches it
        for (int k = 0; k < d.nitems && !seen; k++) seen = a.kind == HA_FIELD && !strcmp(d.items[k].field, a.field);                   // a step of an item's field
        seen = seen || (a.kind == HA_FIELD && !strcmp(a.field, "pick.here"));                                                       // the folder pick's own step
        for (int k = 0; k < d.nitems && !seen; k++) seen = d.items[k].activate && !strcmp(d.items[k].activate, a.name);
        for (const char *const *r = d.row_actions_entry; *r && !seen; r++) seen = !strcmp(*r, a.name);
        for (const char *const *r = d.row_actions_edit; *r && !seen; r++) seen = !strcmp(*r, a.name);
        for (const char *const *r = d.row_actions_file; *r && !seen; r++) seen = !strcmp(*r, a.name);
        for (int k = 0; k < d.npages && !seen; k++) seen = d.pages[k].activate && !strcmp(d.pages[k].activate, a.name);
        if (!seen) printf("  action %s is offered nowhere\n", a.name);
        CHECK(seen);
    }
    { int nk; hc_keymap(&nk); printf("  declaration: %d pages, %d items, %d actions, %d keys in the key map\n", d.npages, d.nitems, d.nactions, nk); }
}

// Every action, by intent, reaches the model: a key sequence, a field step, or (destructive) a question first.
static void coverage_reach()
{
    const HostDecl &d = host_decl();
    int reached = 0;
    for (int i = 0; i < d.nactions; i++) {
        const HAction &a = d.actions[i];
        M = Fake();
        M.v = a.view >= 0 && a.view != HVW_COLORS ? a.view : HVW_MENU;
        if (a.mode >= 0) M.md = a.mode;
        M.wifi = a.field && !strncmp(a.field, "wifi.", 5);
        drain_keys();
        std::string js = std::string("{\"do\":\"op\",\"item\":\"") + a.name + "\"";
        if (a.flags & HAF_ROW) {
            const HPage *pg = 0;
            for (int p = 0; p < d.npages && !pg; p++) if (d.pages[p].list && !strcmp(d.pages[p].list, a.src) && (a.view < 0 || d.pages[p].view == a.view)) pg = &d.pages[p];
            CHECK(pg != 0);
            if (pg) {
                M.v = pg->view;
                std::string sn = snap();
                js += std::string(",\"target\":\"") + id_of(sn, !strcmp(a.src, "entry") ? "VIC-20" : "a.cor") + "\"";
            }
        }
        if (a.flags & HAF_TEXT) js += ",\"value\":\"New\"";
        js += "}";
        UicReply r = submit(js.c_str());
        bool ok = r.r == UR_TAKEN || r.r == UR_ASKED;
        if (!ok) printf("  action %s: %s (%s)\n", a.name, uic_r_name(r.r), r.why);
        CHECK(ok);
        std::vector<int> pre;
        if (r.r == UR_ASKED) {
            CHECK(a.flags & HAF_DESTRUCTIVE);                                   // only a destructive action asks
            pre = popped();                                                     // ... and no key moved before the answer: the screen was asked to arm
            CHECK(pre.empty() && !M.log.empty() && M.log.back().compare(0, 4, "arm ") == 0);
            UicIntent an;
            memset(&an, 0, sizeof(an));
            an.kind = UD_ANSWER;
            snprintf(an.choice, sizeof(an.choice), "yes");
            UicReply ar;
            uic_submit(hostapi_service(), an, &ar);
            CHECK(ar.r == UR_TAKEN);
            pump();
        } else {
            CHECK(!(a.flags & HAF_DESTRUCTIVE) || (a.flags & HAF_FLOW));       // a destructive action never runs unasked (the Cores page asks on its own)
        }
        std::vector<int> ks = popped();                                      // the commands run as they are popped
        ks.insert(ks.begin(), pre.begin(), pre.end());
        bool reached_it = !ks.empty() || !M.log.empty();
        if (!reached_it) printf("  action %s reached nothing\n", a.name);
        CHECK(reached_it);
        reached += reached_it;
        pump();
        if (r.r == UR_ASKED) CHECK(M.ran.size() == 1);                       // the yes made the screen run the armed action
        // an intent of host_core for a keys action: it is the last thing pushed (the navigation and the focus come before it)
        if (a.kind == HA_KEYS && r.r != UR_ASKED) {
            CHECK(!ks.empty() && ks.back() == HC_ACT_KEY(hc_act_by_name(a.name)));
        }
    }
    printf("  %d of %d actions reached the model by intent\n", reached, d.nactions);
}

// Every item of every page is in the snapshot of its page (when the model says it is visible), and every row of a list has an id and the page's acts.
static void coverage_snapshot()
{
    const HostDecl &d = host_decl();
    for (int p = 0; p < d.npages; p++) {
        M = Fake();
        const HPage &pg = d.pages[p];
        M.v = pg.view;
        M.wifi = !strcmp(pg.id, "settings/wifi");
        M.cores = !strcmp(pg.id, "settings/cores");
        std::string s = snap();
        CHECK(has(s, (std::string("\"screen\":\"") + pg.id + "\"").c_str()));
        for (int i = 0; i < d.nitems; i++) {
            if (strcmp(d.items[i].page, pg.id)) continue;
            char id[64];
            hd_item_id(d.items[i], id, sizeof(id));
            bool visible = strcmp(d.items[i].field, "settings.secs");              // the fake hides the delay
            bool in = has(s, (std::string("\"id\":\"") + id + "\"").c_str());
            if (visible != in) printf("  item %s on %s: visible=%d in snapshot=%d\n", id, pg.id, visible, in);
            CHECK(visible == in);
        }
        if (pg.list && !strcmp(pg.list, "entry")) {
            std::string a = id_of(s, "Amiga 1200"), v1 = id_of(s, "VIC-20", 0), v2 = id_of(s, "VIC-20", 1);
            CHECK(!a.empty() && !v1.empty() && !v2.empty());
            CHECK(v1 != v2);                                                       // the second VIC-20 has its own id
            CHECK(has(s, "\"acts\":["));
        }
        if (pg.list && !strcmp(pg.list, "file")) {
            CHECK(!id_of(s, "a.cor").empty());
            CHECK(has(s, "\"path\":\"/SD\""));
        }
        CHECK(has(s, "\"ops\":["));
    }
}

// M4: a snapshot is one WebSocket message (12288 bytes on the host): a browser page of long names is cut by size, keeps the focused row and says `more`
static void snapshot_size()
{
    M = Fake();
    M.v = HVW_BROWSE;
    M.files.clear();
    for (int i = 0; i < 60; i++) {
        char n[100];
        snprintf(n, sizeof(n), "a-very-long-file-name-number-%02d-%s.cor", i, std::string(48, 'x').c_str());
        M.files.push_back(n);
    }
    for (int cur : { 0, 30, 59 }) {
        M.fsel = cur;
        std::string s = snap();
        CHECK(s.size() < 12288 - 80);
        CHECK(cur != 30 || s.size() > 6000);                                  // a full window of long names is big, and still one message
        char want[40];
        snprintf(want, sizeof(want), "number-%02d-", cur);
        size_t f = s.find("\"focused\":true");
        CHECK(f != std::string::npos);                                       // the focused row is in the snapshot
        CHECK(s.find(want) != std::string::npos);
        CHECK(has(s, "\"focus\":\"fs:") && s.back() == '}');               // valid end: not cut in the middle
        CHECK(has(s, "\"ops\":["));                                         // what follows the rows is there too
    }
}

// ---- behaviour ---------------------------------------------------------------------------------------------------------------------------
static void behaviour()
{
    // select and activate a row; the list focus runs before the keys; the page's activate action takes the row
    M = Fake();
    M.v = HVW_MENU;
    {
        std::string s0 = snap();
        g_sub[0][1] = id_of(s0, "Amiga 1200");
        g_sub[1][1] = id_of(s0, "VIC-20", 0);
        g_sub[2][1] = id_of(s0, "VIC-20", 1);
    }
    UicReply r = submit("{\"do\":\"select\",\"item\":\"@A@\"}");
    CHECK_S(reply_r(r), "taken");
    CHECK(M.esel == 0);
    r = submit("{\"do\":\"activate\",\"item\":\"@V2@\"}");
    CHECK_S(reply_r(r), "taken");
    pump();
    std::vector<int> ks = popped();
    CHECK(ks.size() == 1 && ks[0] == HC_ACT_KEY(HCA_ENTRY_START));      // the row's activate is the intent, not a key
    CHECK(M.esel == 2);
    r = submit("{\"do\":\"activate\",\"item\":\"entry:nope\"}");
    CHECK_S(reply_r(r), "gone");

    // an action of another view: the navigation key goes first, then the focus, then the keys
    drain_keys();
    M.v = HVW_MENU;
    r = submit("{\"do\":\"op\",\"item\":\"entry.move_up\",\"target\":\"@V1@\"}");
    CHECK_S(reply_r(r), "taken");
    pump();
    ks = popped();
    CHECK(ks.size() == 2 && ks[0] == HC_ACT_KEY(HCA_NAV_EDIT) && ks[1] == HC_ACT_KEY(HCA_ENTRY_MOVE_UP));
    CHECK(M.log.size() == 1 && M.log[0] == "focus entry 1");

    // the settings: set by id, by field key, by short name; a refusal carries the model's word
    drain_keys();
    M.v = HVW_SET;
    r = submit("{\"do\":\"set\",\"item\":\"settings.direct\",\"value\":\"on\"}");
    CHECK_S(reply_r(r), "taken");
    CHECK(M.direct);
    r = submit("{\"do\":\"set\",\"key\":\"direct\",\"value\":\"off\"}");
    CHECK_S(reply_r(r), "taken");
    CHECK(!M.direct);
    r = submit("{\"do\":\"set\",\"item\":\"settings.direct\",\"value\":\"bad\"}");
    CHECK_S(reply_r(r), "refused");
    CHECK_S(r.why, "refused by the model");
    r = submit("{\"do\":\"set\",\"item\":\"nothing.like.it\",\"value\":\"1\"}");
    CHECK_S(reply_r(r), "gone");
    r = submit("{\"do\":\"set\",\"item\":\"settings.wifi\",\"value\":\"1\"}");     // a button has no value
    CHECK_S(reply_r(r), "refused");
    r = submit("{\"do\":\"activate\",\"item\":\"settings.direct\"}");              // activating a toggle steps the field after the page's own navigation
    CHECK_S(reply_r(r), "taken");
    pump();
    popped();
    CHECK(!M.log.empty() && M.log.back() == "step settings.direct 1");

    // the confirmation gate (T192 D21): the question is the screen's own arm. Asking arms through the model (no key is queued); no / back / the timeout drop the arm by its
    // token; yes confirms by it. The service's question closes exactly once, whoever ended the arm.
    drain_keys();
    M = Fake();
    M.v = HVW_EDIT;
    r = submit("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"@A@\"}");
    CHECK_S(reply_r(r), "asked");
    CHECK(r.q[0] != 0);
    std::string q1 = r.q;
    ks = popped();
    CHECK(ks.empty());                                                            // the arm is made by the model's call, not by keys
    CHECK(M.log.size() == 1 && M.log[0] == "arm 1 0 Amiga 1200" && M.tok != 0);
    std::string s = snap();
    CHECK(has(s, "Remove this entry from the boot menu? Amiga 1200"));
    CHECK(has(s, "\"destructive\":true"));
    UicIntent an;
    memset(&an, 0, sizeof(an));
    an.kind = UD_ANSWER;
    UicReply ar, r2;
    // no over the API
    snprintf(an.choice, sizeof(an.choice), "no");
    snprintf(an.q, sizeof(an.q), "%s", q1.c_str());
    uic_submit(hostapi_service(), an, &ar);
    CHECK_S(reply_r(ar), "taken");
    pump();
    CHECK(popped().empty());                                                      // no key leaves: nothing can leak into another page
    CHECK(M.tok == 0 && M.log.back() == "disarm" && M.ran.empty());
    CHECK(closed_count(q1) == 1 && closed_by(q1) == "remote" && closed_choice(q1) == "no");
    uic_submit(hostapi_service(), an, &ar);
    CHECK_S(reply_r(ar), "gone");                                                 // a late answer finds no question
    CHECK(closed_count(q1) == 1);
    // yes over the API: the screen runs it, still armed with that token
    r = submit("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"@A@\"}");
    CHECK_S(reply_r(r), "asked");
    std::string q2 = r.q;
    snprintf(an.choice, sizeof(an.choice), "yes");
    snprintf(an.q, sizeof(an.q), "%s", q2.c_str());
    uic_submit(hostapi_service(), an, &ar);
    CHECK_S(reply_r(ar), "taken");
    pump();
    CHECK(popped().empty());
    CHECK(M.ran.size() == 1 && M.ran[0] == "Amiga 1200" && M.tok == 0);
    CHECK(closed_count(q2) == 1 && closed_by(q2) == "remote" && closed_choice(q2) == "yes");
    // the row by its index (the T106 page), and a row that is not there
    M = Fake();
    r = submit("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"#1\"}");
    CHECK_S(reply_r(r), "asked");
    CHECK(M.log.size() == 1 && M.log[0] == "arm 1 1 VIC-20");
    CHECK_S(reply_r(answer_q("no", r.q)), "taken");
    pump();
    r = submit("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"#9\"}");
    CHECK_S(reply_r(r), "gone");
    // back from the API is the no; once
    M = Fake();
    r = submit("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"@V1@\"}");
    CHECK_S(reply_r(r), "asked");
    std::string q3 = r.q;
    r = submit("{\"do\":\"back\"}");
    CHECK_S(reply_r(r), "taken");
    pump();
    CHECK(popped().empty() && M.tok == 0 && M.log.back() == "disarm");
    CHECK(closed_count(q3) == 1 && closed_by(q3) == "remote" && closed_choice(q3) == "no");
    CHECK(!has(snap(), "Remove this entry"));
    // the 60 s timeout is the no, once; no key leaves (a rename field or a Cores page ASK of the user is not touched)
    r = submit("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"@V1@\"}");
    CHECK_S(reply_r(r), "asked");
    std::string q4 = r.q;
    g_t += 61000;
    pump();
    pump();
    CHECK(popped().empty() && M.tok == 0 && M.log.back() == "disarm" && M.ran.empty());
    CHECK(closed_count(q4) == 1 && closed_by(q4) == "timeout" && closed_choice(q4) == "no");
    // a key at the board answers: the confirming key is yes (and the screen runs it), another key is no and acts as it always does
    M = Fake();
    r = submit("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"@V1@\"}");
    std::string q5 = r.q;
    board_key(KEY_DELETE);
    CHECK(M.ran.size() == 1 && M.ran[0] == "VIC-20" && M.tok == 0);
    CHECK(closed_count(q5) == 1 && closed_by(q5) == "screen" && closed_choice(q5) == "yes");
    CHECK(!has(snap(), "Remove this entry"));
    r = submit("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"@V1@\"}");
    std::string q6 = r.q;
    board_key(KEY_DOWN);
    CHECK(M.ran.size() == 1 && M.tok == 0);                                       // nothing more was run
    CHECK(closed_count(q6) == 1 && closed_by(q6) == "screen" && closed_choice(q6) == "no");
    CHECK_S(reply_r(answer_q("yes", q6.c_str())), "gone");                        // the late yes finds the question closed; nothing runs
    CHECK(M.ran.size() == 1);
    // the board's key and the API's yes in a race: the screen's key wins, the yes finds no question, nothing is run twice
    M = Fake();
    r = submit("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"@V1@\"}");
    std::string q7 = r.q;
    board_key(KEY_BACK);                                                          // BACK is DEL on the C64 keyboard: yes
    CHECK_S(reply_r(answer_q("yes", q7.c_str())), "gone");
    CHECK(M.ran.size() == 1 && closed_count(q7) == 1);

    // review 1: nothing the API does to the screen while the question is open lets a yes act anywhere else. Any navigation takes the arm away: the question closes
    // (by the screen, no choice) and a yes finds none.
    M = Fake();
    M.v = HVW_BROWSE;
    r = submit("{\"do\":\"op\",\"item\":\"browse.delete\",\"target\":\"#2\"}");
    CHECK_S(reply_r(r), "asked");
    std::string q8 = r.q;
    CHECK(M.v == HVW_BROWSE && M.log[0] == "arm 2 2 a.cor");
    r = submit("{\"do\":\"open\",\"target\":\"edit\"}");                            // the API opens the Edit page
    CHECK_S(reply_r(r), "taken");
    pump();
    ks = popped();
    CHECK(ks.size() == 1 && ks[0] == HC_ACT_KEY(HCA_NAV_EDIT));
    M.press(ks[0], false);                                                        // the screen takes the key: the arm is gone
    M.v = HVW_EDIT;
    pump();
    CHECK(closed_count(q8) == 1 && closed_by(q8) == "screen" && closed_choice(q8).empty());
    CHECK_S(reply_r(answer_q("yes", q8.c_str())), "gone");
    pump();
    CHECK(M.ran.empty() && popped().empty());                                     // the Edit page's entry is not removed, nothing queued
    // the reverse: an entry question, the API goes to the browser
    M = Fake();
    M.v = HVW_EDIT;
    r = submit("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"@V1@\"}");
    std::string q9 = r.q;
    r = submit("{\"do\":\"open\",\"target\":\"browse\"}");
    pump();
    ks = popped();
    CHECK(ks.size() == 1 && ks[0] == HC_ACT_KEY(HCA_NAV_BROWSE));
    M.press(ks[0], false);
    pump();
    CHECK(closed_count(q9) == 1 && closed_by(q9) == "screen" && closed_choice(q9).empty());
    CHECK_S(reply_r(answer_q("yes", q9.c_str())), "gone");
    CHECK(M.ran.empty());
    // the screen drops the arm by itself (a list reload, a page change of its own): the question closes at the next pass even if no word was sent
    M = Fake();
    r = submit("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"@V1@\"}");
    std::string q10 = r.q;
    M.tok = 0;
    pump();
    CHECK(closed_count(q10) == 1 && closed_by(q10) == "screen" && closed_choice(q10).empty());
    // a screen that calls back at once closes it at once, and only once
    r = submit("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"@V1@\"}");
    std::string q11 = r.q;
    M.end_arm(HARM_END_GONE);
    CHECK(closed_count(q11) == 1);
    pump();
    CHECK(closed_count(q11) == 1);

    // review 2/3: the arm can bail (boot.cfg, a folder with files, no WiFi module): the API sees `refused` with the screen's words, no question is raised or left open, no key is queued
    M = Fake();
    M.v = HVW_BROWSE;
    M.bail = "Flash:/boot.cfg is the boot menu itself and is not deleted here";
    r = submit("{\"do\":\"op\",\"item\":\"browse.delete\",\"target\":\"#2\"}");
    CHECK_S(reply_r(r), "refused");
    CHECK_S(r.why, "Flash:/boot.cfg is the boot menu itself and is not deleted here");
    CHECK(r.q[0] == 0 && !has(snap(), "Delete this file?") && popped().empty() && M.tok == 0);
    M.bail = "The folder is not empty: only empty folders are deleted";
    r = submit("{\"do\":\"op\",\"item\":\"browse.delete\",\"target\":\"#2\"}");
    CHECK_S(reply_r(r), "refused");
    CHECK_S(r.why, "The folder is not empty: only empty folders are deleted");
    M.bail = "";                                                                  // the slot was released: the next ask is not busy
    r = submit("{\"do\":\"op\",\"item\":\"browse.delete\",\"target\":\"#2\"}");
    CHECK_S(reply_r(r), "asked");
    answer_q("no", r.q);
    pump();
    M = Fake();
    M.bail = "There is no WiFi module";
    r = submit("{\"do\":\"op\",\"item\":\"wifi.forget\"}");
    CHECK_S(reply_r(r), "refused");
    CHECK_S(r.why, "There is no WiFi module");
    CHECK(!has(snap(), "Forget all stored access points?") && popped().empty());
    M.bail = "";
    r = submit("{\"do\":\"op\",\"item\":\"wifi.forget\"}");
    CHECK_S(reply_r(r), "asked");
    CHECK(M.log.back() == "arm 3 -1 ");
    answer_q("yes", r.q);
    pump();
    CHECK(M.ran.size() == 1 && M.ran[0] == "wifi.forget");
    // a modal state: the screen is not left, no arm, busy with the reason
    M = Fake();
    M.busy = "the browser waits for an answer or a folder pick";
    r = submit("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"@V1@\"}");
    CHECK_S(reply_r(r), "busy");
    CHECK_S(r.why, "the browser waits for an answer or a folder pick");
    CHECK(M.tok == 0 && popped().empty() && !has(snap(), "Remove this entry"));

    // a second ask while one is open: busy, and the screen does not move
    M = Fake();
    M.v = HVW_MENU;
    r = submit("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"@V1@\"}");
    CHECK_S(reply_r(r), "asked");
    size_t nlog = M.log.size();
    int v0 = M.v;
    r2 = submit("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"@V2@\"}");
    CHECK_S(reply_r(r2), "busy");
    CHECK(M.log.size() == nlog && M.v == v0 && M.arm_row == 1);                  // no second arm call, the first arm stands
    r2 = submit("{\"do\":\"op\",\"item\":\"browse.delete\",\"target\":\"#2\"}");
    CHECK_S(reply_r(r2), "busy");
    CHECK(M.log.size() == nlog && M.v == v0);
    // while a question is open nothing else is taken
    r2 = submit("{\"do\":\"key\",\"key\":\"down\"}");
    CHECK_S(reply_r(r2), "blocked");
    r2 = submit("{\"do\":\"back\"}");
    CHECK_S(reply_r(r2), "taken");
    pump();
    // keys that wait first: the ask does not jump the queue (the arm would be taken away by them)
    M = Fake();
    r = submit("{\"do\":\"key\",\"key\":\"down\"}");
    CHECK_S(reply_r(r), "taken");
    r = submit("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"@V1@\"}");
    CHECK_S(reply_r(r), "busy");
    CHECK(M.log.empty());
    popped();
    // the user types in a field: the keys are not ours, and the screen is not left
    M = Fake();
    g_in_flow = true;
    hostapi_entry("Rename menu entry", "/SD/vic.cor", "VIC", "RETURN saves");
    hostapi_waiting();
    r = submit("{\"do\":\"op\",\"item\":\"entry.remove\",\"target\":\"@V1@\"}");
    CHECK_S(reply_r(r), "blocked");
    CHECK(M.log.empty() && M.tok == 0);
    r = submit("{\"do\":\"op\",\"item\":\"browse.delete\",\"target\":\"#2\"}");
    CHECK_S(reply_r(r), "blocked");
    CHECK(M.log.empty());
    hostapi_entry_end();
    g_in_flow = false;
    drain_keys();
    M = Fake();
    M.v = HVW_MENU;

    // a page of a flow that waits: a question; its yes key is the page's; progress is a job step, not a question
    drain_keys();
    M.v = HVW_BROWSE;
    HostPageInfo pi;
    memset(&pi, 0, sizeof(pi));
    snprintf(pi.title, sizeof(pi.title), "Install update.txu?");
    snprintf(pi.line[0], sizeof(pi.line[0]), "This writes the host.");
    pi.nlines = 1;
    snprintf(pi.hint, sizeof(pi.hint), "Y writes it    any other key cancels");
    pi.bar = -1;
    g_in_flow = true;
    CHECK(hostapi_flow_begin("update", "update.txu", false));
    hostapi_page(pi, 'y', KEY_ESCAPE, true);
    hostapi_waiting();                                                              // the flow waits in host_getch
    s = snap();
    CHECK(has(s, "\"jobs\":[{"));
    CHECK(has(s, "Install update.txu? This writes the host."));
    CHECK(has(s, "\"kind\":\"page\""));
    CHECK(has(s, "\"id\":\"yes\",\"label\":\"Yes\",\"key\":\"y\"") && has(s, "\"id\":\"no\",\"label\":\"No\",\"key\":\"esc\""));   // L8: the keys advertised are the ones answered
    CHECK_S(std::string(hostapi_service()->dom->keys_owner()), "dialog");
    an.kind = UD_ANSWER;
    snprintf(an.choice, sizeof(an.choice), "yes");
    an.q[0] = 0;
    uic_submit(hostapi_service(), an, &ar);
    hostapi_waiting();
    ks = popped();
    CHECK(ks.size() == 1 && ks[0] == 'y');
    pi.bar = 40;
    snprintf(pi.line[0], sizeof(pi.line[0]), "Writing the host");
    hostapi_page(pi, KEY_RETURN, KEY_ESCAPE, true);
    hostapi_flow_progress(40, 100, "writing");
    s = snap();
    CHECK(has(s, "\"kind\":\"progress\""));
    CHECK(has(s, "\"done\":40"));
    r = submit("{\"do\":\"op\",\"item\":\"entry.default\",\"target\":\"@V1@\"}");
    CHECK_S(reply_r(r), "blocked");                                                 // the keys belong to the job
    hostapi_page_end();
    hostapi_flow_end(true, "written");
    s = snap();
    CHECK(has(s, "\"phase\":\"done\""));
    CHECK(has(s, "written"));
    g_in_flow = false;
    CHECK_S(std::string(hostapi_service()->dom->keys_owner()), "menu");

    // L4: a flow that was asked to cancel ends as cancelled, and the abort key reaches it
    drain_keys();
    g_in_flow = true;
    CHECK(hostapi_flow_begin("copy", "big.cor", true));
    r = submit("{\"do\":\"cancel\"}");
    CHECK_S(reply_r(r), "taken");
    pump();
    ks = popped();
    CHECK(ks.size() == 1 && ks[0] == KEY_ESCAPE);
    hostapi_flow_end(true, "");
    g_in_flow = false;
    s = snap();
    CHECK(has(s, "\"phase\":\"cancelled\""));

    // L4: the local abort key (ESC at the board) ends a cancelable flow as cancelled, like the cancel intent; a flow that cannot be cancelled still ends as done
    drain_keys();
    g_in_flow = true;
    CHECK(hostapi_flow_begin("copy", "local.cor", true));
    hostapi_local_key(KEY_ESCAPE);
    pump();
    ks = popped();
    CHECK(ks.size() == 1 && ks[0] == KEY_ESCAPE);          // the flow sees the key once (the tick does not push a second)
    hostapi_flow_end(true, "");
    g_in_flow = false;
    s = snap();
    CHECK(has(s, "\"phase\":\"cancelled\""));
    drain_keys();
    g_in_flow = true;
    CHECK(hostapi_flow_begin("flash", "fw.bin", false));
    hostapi_local_key(KEY_ESCAPE);
    pump();
    popped();
    hostapi_flow_end(true, "");
    g_in_flow = false;
    s = snap();
    CHECK(has(s, "\"phase\":\"done\""));

    // text entry: an action with a text arms it; the entry that opens takes it, then RETURN; typed text goes in by key; a password is never in the snapshot
    drain_keys();
    M.v = HVW_EDIT;
    r = submit("{\"do\":\"op\",\"item\":\"entry.rename\",\"target\":\"@V1@\",\"value\":\"New\"}");
    CHECK_S(reply_r(r), "taken");
    pump();
    ks = popped();
    CHECK(ks.size() == 1 && ks[0] == HC_ACT_KEY(HCA_ENTRY_RENAME));
    g_in_flow = true;
    hostapi_entry("Rename menu entry", "/SD/vic.cor", "VIC-20", "RETURN saves");           // the flow opens its field
    ks = popped();
    CHECK(ks.size() == 5 && ks[0] == KEY_CLEAR && ks[1] == 'N' && ks[2] == 'e' && ks[3] == 'w' && ks[4] == KEY_RETURN);
    hostapi_entry("Rename menu entry", "/SD/vic.cor", "New", "RETURN saves");
    hostapi_waiting();
    s = snap();
    CHECK(has(s, "\"kind\":\"entry\""));
    CHECK(has(s, "\"typed\":\"New\""));
    r = submit("{\"do\":\"key\",\"key\":\"text\",\"value\":\"ab\"}");
    CHECK_S(reply_r(r), "taken");
    ks = popped();
    CHECK(ks.size() == 2 && ks[0] == 'a' && ks[1] == 'b');
    r = submit("{\"do\":\"text\",\"value\":\"xy\",\"submit\":true}");
    CHECK_S(reply_r(r), "taken");
    ks = popped();
    CHECK(ks.size() == 4 && ks[0] == KEY_CLEAR && ks[1] == 'x' && ks[2] == 'y' && ks[3] == KEY_RETURN);
    r = submit("{\"do\":\"set\",\"item\":\"settings.direct\",\"value\":\"on\"}");
    CHECK_S(reply_r(r), "blocked");                                                 // a dialog owns the keys: no setting meanwhile
    hostapi_entry_end();
    g_in_flow = false;
    hostapi_history("New");
    r = submit("{\"do\":\"history\",\"dir\":\"prev\"}");
    CHECK_S(reply_r(r), "taken");
    CHECK_S(r.text, "New");

    // a pick list: the rows are items; choosing one moves the cursor there and presses RETURN
    drain_keys();
    g_in_flow = true;
    const char *aps[] = { "Home", "Guest", "Office" };
    hostapi_pick("Access points", aps, 3, 0, "3 access points found");
    hostapi_waiting();
    s = snap();
    g_sub[3][1] = id_of(s, "Guest");
    g_sub[4][1] = id_of(s, "Office");
    CHECK(!g_sub[3][1].empty() && !g_sub[4][1].empty());
    r = submit("{\"do\":\"activate\",\"item\":\"@PO@\"}");
    CHECK_S(reply_r(r), "taken");
    hostapi_waiting();
    ks = popped();
    CHECK(ks.size() == 3 && ks[0] == KEY_DOWN && ks[1] == KEY_DOWN && ks[2] == KEY_RETURN);
    hostapi_pick_end();
    g_in_flow = false;

    // the page that asks over the settings (Cores and ROMs): a question with the page's keys
    drain_keys();
    M.v = HVW_SET;
    g_in_flow = true;
    hostapi_ask("Delete these 3 entries? Press Y.", 'y', KEY_ESCAPE);
    hostapi_waiting();
    s = snap();
    CHECK(has(s, "Delete these 3 entries? Press Y."));
    snprintf(an.choice, sizeof(an.choice), "yes");
    uic_submit(hostapi_service(), an, &ar);
    hostapi_waiting();
    ks = popped();
    CHECK(ks.size() == 1 && ks[0] == 'y');
    hostapi_ask("", 0, 0);
    hostapi_waiting();
    // D19a: the foreign-firmware confirm. The board's own RETURN question has no time-out; the one a remote client raised (web.start) answers no after 60 s
    drain_keys();
    hostapi_ask("Start X? Way back is a power cycle.", KEY_RETURN, KEY_ESCAPE, false);
    hostapi_waiting();
    CHECK(hostapi_service()->q_open);
    g_t += 120000;
    hostapi_waiting();
    CHECK(hostapi_service()->q_open && popped().empty());                         // still asked, nobody answered
    hostapi_ask("", 0, 0);
    hostapi_waiting();
    CHECK(!hostapi_service()->q_open);
    hostapi_ask("Start X? Way back is a power cycle.", KEY_RETURN, KEY_ESCAPE, true);
    hostapi_waiting();
    CHECK(hostapi_service()->q_open);
    g_t += 59000;
    hostapi_waiting();
    CHECK(hostapi_service()->q_open && popped().empty());                         // not yet
    g_t += 2000;
    hostapi_waiting();
    ks = popped();
    CHECK(ks.size() == 1 && ks[0] == KEY_ESCAPE);                                 // 60 s: the no key, once
    hostapi_waiting();
    CHECK(popped().empty());
    hostapi_ask("", 0, 0);
    hostapi_waiting();
    g_in_flow = false;

    // keys: a name, a character, an unknown name; open: a page, a path, a gone screen; back
    drain_keys();
    hostapi_pass(false);
    r = submit("{\"do\":\"key\",\"key\":\"f3\"}");
    CHECK_S(reply_r(r), "taken");
    r = submit("{\"do\":\"key\",\"key\":\"r\"}");
    CHECK_S(reply_r(r), "taken");
    r = submit("{\"do\":\"key\",\"key\":\"nonsense\"}");
    CHECK_S(reply_r(r), "bad");
    ks = popped();
    CHECK(ks.size() == 2 && ks[0] == KEY_F3 && ks[1] == 'r');
    r = submit("{\"do\":\"open\",\"target\":\"settings\"}");
    CHECK_S(reply_r(r), "taken");
    r = submit("{\"do\":\"open\",\"target\":\"browse:/SD/cores\"}");
    CHECK_S(reply_r(r), "taken");
    r = submit("{\"do\":\"open\",\"target\":\"settings/wifi\"}");
    CHECK_S(reply_r(r), "taken");
    r = submit("{\"do\":\"open\",\"target\":\"nowhere\"}");
    CHECK_S(reply_r(r), "gone");
    pump();
    ks = popped();
    CHECK(ks.size() == 3 && ks[0] == HC_ACT_KEY(HCA_NAV_SETTINGS) && ks[1] == HC_ACT_KEY(HCA_NAV_BROWSE) && ks[2] == HC_ACT_KEY(HCA_NAV_SETTINGS));
    CHECK(!M.log.empty());
    r = submit("{\"do\":\"back\"}");
    CHECK_S(reply_r(r), "taken");
    ks = popped();
    CHECK(ks.size() == 1 && ks[0] == HC_ACT_KEY(HCA_BACK));

    // the board's own key enters through the same dispatcher and comes out of the queue
    hostapi_local_key(KEY_DOWN);
    CHECK(hostapi_pop_key() == KEY_DOWN);
    CHECK(hostapi_pop_key() < 0);

    // L5: the board's key goes in front of remote keys and has room when the remote ones are refused
    drain_keys();
    int accepted = 0;
    for (int i = 0; i < 400; i++) {
        r = submit("{\"do\":\"key\",\"key\":\"down\"}");
        if (r.r != UR_TAKEN) { CHECK_S(reply_r(r), "busy"); break; }
        accepted++;
    }
    CHECK(accepted > 100 && accepted < 400);                                        // the remote queue fills up and refuses; a reserve stays for the board
    hostapi_local_key(KEY_RETURN);
    CHECK(hostapi_pop_key() == KEY_RETURN);                                         // in front of all that wait
    CHECK((int)popped().size() == accepted);
    for (int i = 0; i < accepted; i++) { submit("{\"do\":\"key\",\"key\":\"down\"}"); }
    for (int i = 0; i < 40; i++) hostapi_local_key(KEY_UP);                         // more than the reserve: the keys that find no room are told, not dropped silently
    bool told = false;
    for (uint32_t i = 0; i < hostapi_service()->ev_count; i++) {
        if (strstr(hostapi_service()->ev[i].text, "Key dropped")) told = true;
    }
    CHECK(told);
    CHECK((int)popped().size() == accepted + 16);
    // an op the declaration does not know
    r = submit("{\"do\":\"op\",\"item\":\"entry.explode\"}");
    CHECK_S(reply_r(r), "gone");
    // an op that needs a row, with none given and none under the cursor
    M.v = HVW_BROWSE;
    r = submit("{\"do\":\"op\",\"item\":\"browse.add\",\"target\":\"fs:ghost\"}");
    CHECK_S(reply_r(r), "gone");
    // the mode of the browser: pick_here only in the pick mode
    M.md = HMODE_NORMAL;
    r = submit("{\"do\":\"op\",\"item\":\"browse.pick_here\"}");
    CHECK_S(reply_r(r), "refused");
    M.md = HMODE_PICK;
    r = submit("{\"do\":\"op\",\"item\":\"browse.pick_here\"}");
    CHECK_S(reply_r(r), "taken");
}

// The token file: without a token the routes are closed, a grant writes /Flash/trx/ui.token's contents, the gate takes it.
static void token()
{
    UicService *s = hostapi_service();
    CHECK(uic_gate(s, "GET", "/v1/ui/state", 0, 0, 0) == 401);
    CHECK(uic_gate(s, "GET", "/v1/ui/hello", 0, 0, 0) == 0);
    CHECK(uic_gate(s, "GET", "/", 0, 0, 0) == 0);
    char body[200];
    int status = 0;
    int n = uic_http(s, "POST", "/v1/ui/grant", "", 0, body, sizeof(body), &status);
    CHECK(n > 0 && status == 200);
    std::string b(body, n > 0 ? n : 0);
    size_t at = b.find("\"token\":\"");
    CHECK(at != std::string::npos);
    std::string tok = at != std::string::npos ? b.substr(at + 9, 32) : "";
    CHECK(g_have_file && g_file.size() == 33);
    CHECK_S(g_file.substr(0, 32), tok);
    CHECK(uic_gate(s, "GET", "/v1/ui/state", tok.c_str(), 0, 0) == 0);
    CHECK(uic_gate(s, "GET", "/v1/ui/state", 0, tok.c_str(), 0) == 0);
    CHECK(uic_gate(s, "GET", "/v1/ui/state", "00000000000000000000000000000000", 0, 0) == 401);
    n = uic_http(s, "POST", "/v1/ui/grant", "", 0, body, sizeof(body), &status);
    CHECK(status == 403);                                                           // a token exists: no second grant
    // L3: no token file is read while a flow runs (it may be writing the flash); afterwards the two-miss rule ends the token of a deleted file
    CHECK(hostapi_flow_begin("update", "x.txu", false));
    g_have_file = false;
    for (int i = 0; i < 4; i++) { g_t += 2500; pump(); }
    CHECK(uic_gate(s, "GET", "/v1/ui/state", tok.c_str(), 0, 0) == 0);
    hostapi_flow_end(true, "done");
    for (int i = 0; i < 4; i++) { g_t += 2500; pump(); }
    CHECK(uic_gate(s, "GET", "/v1/ui/state", tok.c_str(), 0, 0) == 401);
}

int main()
{
    HostApiPlatform plat = { p_lock, p_unlock, p_ui, p_sleep, p_entropy, p_now, p_load, p_save, p_remove };
    g_ui = true;
    hostapi_init(&M, &plat);
    printf("declaration\n");
    coverage_static();
    printf("every action by intent\n");
    coverage_reach();
    printf("the snapshot of every page\n");
    coverage_snapshot();
    printf("snapshot size\n");
    snapshot_size();
    printf("behaviour\n");
    behaviour();
    printf("token\n");
    token();
    printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
