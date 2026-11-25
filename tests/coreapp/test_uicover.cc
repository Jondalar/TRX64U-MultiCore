// T192.1 / D21 API coverage: the exposure of the core application is GENERATED from its declaration (the menu tree, the descriptor's settings, tasks, media and
// file actions, the ops table), so this test walks the declaration of EVERY core and asserts that nothing is declared without being reachable over the API, and
// that nothing the declaration names is missing from the registry:
//   - every area/tab of the tree opens by `open target` and its snapshot lists every row (total == the controller's rows, ids valid and unique), and every
//     row can be selected by its id (a row that takes no focus says so);
//   - every setting of the descriptor can be set by `set` with every one of its values (never `gone`);
//   - every operation of the registry is reachable by the `op` intent: a probe with an argument no operation has is refused (never run) or, for a destructive
//     one, asked and answered no; none answers `gone`, none runs;
//   - every operation and state the declaration names (tasks, settings' apply, file actions, media eject, contributed nodes) is in the registry, so the walk
//     above saw it.
// A core that adds a row, setting, task or operation is covered with no line here; one that names an operation it never registers fails here.
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
#include "uiapp.h"
#include "uic.h"

struct Host : MenuHost {
    void info(int, char *out, int) { out[0] = 0; }
    bool return_to_host(char *, int) { return false; }
};

extern const CoreDesc *const core_table[];
#ifdef TEST_APP_ATARI800                 // the Atari's app is its own build (run.sh a8app, the unit a8_uicover): its table is its core only
extern const CoreDesc atari800_desc;     // cores.h lists the shared app's cores; the Atari 800 XL is its own app (cores/atari800/app/lib)
const CoreDesc *const core_table[] = { &atari800_desc, 0 };
#endif
#ifdef TEST_APP_C264                     // the Commodore 264's app is its own build (run.sh c264app): its table is its core only
extern const CoreDesc c264_desc;         // no shared header declares it (cores/c264/app/lib holds this one)
const CoreDesc *const core_table[] = { &c264_desc, 0 };
#endif

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
static void pump() { bool was = g_ui; g_ui = true; uiapp_pass(false); g_ui = was; }
static void pl_sleep(int) { pump(); }
static bool env_open(void *) { return g_open; }
static void env_do_open(void *) { M->open(); g_open = true; }
static void env_key(void *, int k) { if (M->key(k) == MenuCtl::R_CLOSE) g_open = false; }
static void env_intent(void *, const MenuIntent *in) { if (M->apply(*in) == MenuCtl::R_CLOSE) g_open = false; }
static bool env_false(void *) { return false; }
static MenuCtl *env_active(void *) { return M; }

static std::string snap()
{
    static char b[16000];
    uint32_t rev;
    int n = uic_snapshot(uiapp_service(), b, sizeof(b), &rev);
    return n > 0 ? std::string(b, n) : std::string("");
}
static UicReply send(const std::string &json)
{
    UicIntent in; UicReply r;
    memset(&r, 0, sizeof(r));
    if (!uic_intent_parse(json.c_str(), (int)json.size(), &in)) { r.r = UR_BAD; snprintf(r.why, sizeof(r.why), "unparsable"); }
    else uic_submit(uiapp_service(), in, &r);
    return r;
}
static std::string jstr(const std::string &s)
{
    std::string o = "\"";
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
    return o + "\"";
}
static std::string field(const std::string &s, const char *k)
{
    char b[400]; uic_json_get(s.c_str(), (int)s.size(), k, b, sizeof(b));
    return b;
}
struct Item { std::string id, kind, json; bool enabled = true, dim = false; };
static std::vector<Item> items(const std::string &s)
{
    std::vector<Item> v;
    size_t a = s.find("\"items\":[");
    if (a == std::string::npos) return v;
    size_t p = a + 9;
    while (p < s.size() && s[p] != ']') {
        if (s[p] != '{') { p++; continue; }
        int depth = 0; size_t q = p; bool str = false;
        for (; q < s.size(); q++) {
            if (str) { if (s[q] == '\\') q++; else if (s[q] == '"') str = false; continue; }
            if (s[q] == '"') str = true;
            else if (s[q] == '{') depth++;
            else if (s[q] == '}' && --depth == 0) break;
        }
        Item it; it.json = s.substr(p, q - p + 1);
        it.id = field(it.json, "id"); it.kind = field(it.json, "kind");
        it.enabled = it.json.find("\"enabled\":false") == std::string::npos;
        it.dim = it.json.find("\"dim\":true") != std::string::npos;
        v.push_back(it);
        p = q + 1;
    }
    return v;
}

struct Cov { std::string core; int tabs = 0, hidden_tabs = 0, rows = 0, exposed = 0, selected = 0, settings = 0, values = 0, values_ok = 0, ops = 0, ops_reach = 0, refs = 0, refs_ok = 0; };

static bool is_op_name(const char *n) { return n && n[0] && strncmp(n, "ui.", 3) != 0; }   // "ui.*": the sheet's own actions (copy, move, rename, delete): rows of the sheet

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);
    std::vector<Cov> table;
    for (const CoreDesc *const *pd = core_table; *pd; pd++) {
        const CoreDesc *d = *pd;
        Cov cv; cv.core = d->cor_name ? d->cor_name : "?";
        FakeIo io; FakeFs fs; CoreCtx c;
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs;
        char cor[80]; snprintf(cor, sizeof(cor), "/sd/cores/%s.cor", d->cor_name ? d->cor_name : "x");
        for (char *q = cor; *q; q++) if (*q == ' ' || *q == '(' || *q == ')' || *q == ',') *q = '_';
        snprintf(c.cor_path, sizeof(c.cor_path), "%s", cor);
        Settings st(d->settings, d->nsettings, d->store, &fs, "/sd/cores/cov.cfg", 0);
        st.load();
        c.settings = &st;
        fs.put("/usb0/x/a.bin", std::vector<uint8_t>(64, 1));
        M = new MenuCtl(&c, &HOST);
        g_open = false;
        UiAppEnv env = { 0, env_open, env_do_open, env_key, env_intent, env_false, env_false, env_active };
        uiapp_reset_for_test();
        g_ui = true; uiapp_init(&c, &env, &PLAT); g_ui = false;
        CHECK(send("{\"do\":\"open\",\"menu\":true}").r == UR_TAKEN);
        const MenuTree &t = M->tree();

        // ---- every area/tab opens; its snapshot lists every row; every row is selectable by id ---------------------------------------------------------------------------------
        for (int a = 0; a < t.nareas; a++) {
            for (int ti = 0; ti < t.a[a].ntabs; ti++) {
                const MenuTab &tab = t.a[a].tabs[ti];
                if (!tab.id) continue;
                std::string spec = std::string(t.a[a].key) + "/" + tab.id;
                UicReply r = send("{\"do\":\"open\",\"target\":" + jstr(spec) + "}");
                if (r.r != UR_TAKEN) { cv.hidden_tabs++; if (!M->tab_is_hidden(a, ti)) { printf("FAIL %s: open %s answered %s %s\n", cv.core.c_str(), spec.c_str(), uic_r_name(r.r), r.why); g_fails++; } continue; }
                cv.tabs++;
                std::string s = snap();
                CHECK(field(s, "screen") == spec);
                std::vector<Item> it = items(s);
                int total = atoi(field(s.substr(s.find("\"rows\":") + 7), "total").c_str());
                CHECK_EQ(total, M->rows());
                bool lib = M->browsing();
                int expect = 0;
                if (!lib) {
                    for (int i = 0; i < M->rows() && i < 128; i++) { MenuCtl::Item x; M->item(i, &x); if (!x.hidden) expect++; }
                    if (M->picking() || M->chooser()) expect = (int)it.size();
                } else {
                    expect = M->rows() < 24 ? M->rows() : 24;
                }
                if (it.size() != (size_t)expect) { printf("FAIL %s %s: snapshot lists %d items, the controller has %d rows\n", cv.core.c_str(), spec.c_str(), (int)it.size(), expect); g_fails++; }
                std::set<std::string> ids;
                for (auto &x : it) {
                    CHECK(uic_id_valid(x.id.c_str()));
                    if (!ids.insert(x.id).second) { printf("FAIL %s %s: duplicate id %s\n", cv.core.c_str(), spec.c_str(), x.id.c_str()); g_fails++; }
                }
                cv.rows += (int)total; cv.exposed += (int)it.size();
                for (auto &x : it) {
                    UicReply sr = send("{\"do\":\"select\",\"item\":" + jstr(x.id) + "}");
                    bool nofocus = !x.enabled || x.dim || x.kind == "value" || x.kind == "heading" || x.kind == "info" || x.kind == "row";
                    if (sr.r == UR_TAKEN) cv.selected++;
                    else if (!(sr.r == UR_REFUSED && nofocus && sr.why[0])) { printf("FAIL %s %s: select %s answered %s %s\n", cv.core.c_str(), spec.c_str(), x.id.c_str(), uic_r_name(sr.r), sr.why); g_fails++; }
                    if (sr.r == UR_TAKEN && M->chooser()) { send("{\"do\":\"key\",\"key\":\"back\"}"); }
                }
                // leave anything a select may have opened
                send("{\"do\":\"key\",\"key\":\"back\"}");
                if (M->entering() || M->picking() || M->chooser()) send("{\"do\":\"key\",\"key\":\"back\"}");
            }
        }

        // ---- every setting, every value ----------------------------------------------------------------------------------------------------------------------------------------------------
        for (int i = 0; i < d->nsettings; i++) {
            const Setting &s = d->settings[i];
            cv.settings++;
            std::vector<std::string> vals;
            if (s.kind == 0) for (int k = 0; k < s.nvalues; k++) vals.push_back(s.values[k]);
            else if (s.kind == 3) vals.push_back("");
            else vals.push_back("x");
            for (auto &v : vals) {
                cv.values++;
                UicReply r = send("{\"do\":\"set\",\"key\":" + jstr(s.key) + ",\"value\":" + jstr(v) + "}");
                bool ok = r.r == UR_TAKEN || r.r == UR_HELD || (r.r == UR_REFUSED && r.why[0]);
                if (ok) cv.values_ok++;
                else { printf("FAIL %s: set %s=%s answered %s %s\n", cv.core.c_str(), s.key, v.c_str(), uic_r_name(r.r), r.why); g_fails++; }
            }
        }

        // ---- every operation of the registry is reachable and none runs on a probe ---------------------------------------------------------------------------------------------------
        OpRegistry *reg = ops_of(&c);
        for (int i = 0; reg && i < reg->nops(); i++) {
            const OpDef *od = reg->op_at(i);
            cv.ops++;
            UicReply r = send("{\"do\":\"op\",\"item\":" + jstr(od->name) + ",\"args\":{\"zz_probe\":1}}");
            if (r.r == UR_ASKED) {                                                     // destructive: the question stands, the answer is no
                std::string q = r.q;
                UicReply a = send("{\"do\":\"answer\",\"q\":" + jstr(q) + ",\"choice\":\"no\"}");
                CHECK(a.r == UR_TAKEN);
                pump();
                cv.ops_reach++;
            } else if (r.r == UR_REFUSED) {
                cv.ops_reach++;
            } else {
                printf("FAIL %s: op %s answered %s %s (a probe must be refused or asked, never run, never gone)\n", cv.core.c_str(), od->name, uic_r_name(r.r), r.why);
                g_fails++;
            }
        }

        // ---- what the declaration names is in the registry ---------------------------------------------------------------------------------------------------------------------------------------
        auto ref_op = [&](const char *what, const char *name) {
            if (!is_op_name(name)) return;
            cv.refs++;
            if (reg && reg->find_op(name)) cv.refs_ok++;
            else { printf("FAIL %s: %s names the operation %s, which is not registered (the menu would call it, the API cannot see it)\n", cv.core.c_str(), what, name); g_fails++; }
        };
        auto ref_state = [&](const char *what, const char *name) {
            if (!name || !name[0]) return;
            cv.refs++;
            if (reg && reg->find_state(name)) cv.refs_ok++;
            else { printf("FAIL %s: %s names the state %s, which is not registered\n", cv.core.c_str(), what, name); g_fails++; }
        };
        for (int i = 0; i < d->ntasks; i++) { ref_op("a task", d->tasks[i].op); ref_state("a task's prompt", d->tasks[i].ask_state); }
        for (int i = 0; i < d->nsettings; i++) ref_op("a setting's apply", d->settings[i].apply);
        for (int i = 0; i < d->nmedia; i++) { ref_state("a medium", d->media[i].state); ref_op("a medium's eject", d->media[i].eject_op); }
        for (int f = 0; f < d->nfiletypes; f++) {
            const FileType &ft = d->filetypes[f];
            for (int j = 0; j < ft.nactions; j++) ref_op("a file action", ft.actions[j].op);
            for (int j = 0; j < ft.ninner; j++) ref_op("an inner file action", ft.inner[j].op);
        }
        for (int i = 0; i < t.nnodes; i++) { ref_op("a contributed node", t.nodes[i].op); ref_state("a contributed state line", t.nodes[i].state); }

        table.push_back(cv);
        delete M; M = 0;
    }
    printf("\nAPI coverage (D21): every row of the declaration is a row of the snapshot, every setting value and every operation has an intent\n");
    printf("%-34s %5s %6s %8s %9s | %5s %6s %6s | %4s %6s | %4s %6s\n", "core", "tabs", "rows", "exposed", "selected", "sets", "values", "ok", "ops", "reach", "refs", "ok");
    Cov tot;
    for (auto &c : table) {
        printf("%-34.34s %5d %6d %8d %9d | %5d %6d %6d | %4d %6d | %4d %6d\n", c.core.c_str(), c.tabs, c.rows, c.exposed, c.selected, c.settings, c.values, c.values_ok, c.ops, c.ops_reach, c.refs, c.refs_ok);
        tot.tabs += c.tabs; tot.rows += c.rows; tot.exposed += c.exposed; tot.selected += c.selected; tot.settings += c.settings; tot.values += c.values; tot.values_ok += c.values_ok;
        tot.ops += c.ops; tot.ops_reach += c.ops_reach; tot.refs += c.refs; tot.refs_ok += c.refs_ok;
    }
    printf("%-34s %5d %6d %8d %9d | %5d %6d %6d | %4d %6d | %4d %6d\n", "all cores", tot.tabs, tot.rows, tot.exposed, tot.selected, tot.settings, tot.values, tot.values_ok, tot.ops, tot.ops_reach, tot.refs, tot.refs_ok);
    DONE("uicover");
}
