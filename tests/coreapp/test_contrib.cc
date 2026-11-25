// T104.1 D4, D5, D8, S3: the menu merge. What a description, a library and an application contribute lands in the tab its anchor names, in an order
// that does not depend on which was loaded first; what cannot be placed is refused by name and the rest is merged; settings land by group, a range
// setting hands the operation the number and not the index; file actions join the descriptor's per extension and a size set filters them; hide
// and rename work on a node's id; and the menu a person drives - rows, a button that calls its operation, a line that shows a state, a row whose
// condition fails - is the tree the merge made. The default route with nothing contributed is every golden dump (test_desc); here the same route
// written as a description has to give the same dump.
//
//   test_contrib DESC_TEST.bin DESC_DEFAULT.bin       (compiled from tests/coreapp/golden/*.toml by tools/descrec.py)
#include <string>
#include <algorithm>
#include <vector>
#include <fstream>
#include <sstream>
#include "check.h"
#include "model_grid.h"
#include "fakes.h"
#include "test_core.h"
#include "contrib.h"
#include "descrec.h"
#include "menuctl.h"
#include "listgrow.h"
#include "ops_glue.h"
#include "desc_check.h"
#include "keyboard.h"

const CoreDesc *const core_table[] = { &testcore::desc, 0 };

static std::vector<uint8_t> slurp(const char *p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    std::string t = s.str();
    return std::vector<uint8_t>(t.begin(), t.end());
}

// ---- what the contributors do ---------------------------------------------------------------------------------------------------
static int g_beeps, g_mode = -1, g_bus = -1, g_beeper = -1, g_shape = -1, g_files;
static std::string g_file, g_state = "idle";
static int op_beep(CoreCtx *, const OpArgs &, OpOut &) { g_beeps++; return OPE_OK; }
static int op_mode(CoreCtx *, const OpArgs &a, OpOut &) { g_mode = a.i(0); return OPE_OK; }
static int op_bus(CoreCtx *, const OpArgs &a, OpOut &) { g_bus = a.i(0); return OPE_OK; }
static int op_beeper(CoreCtx *, const OpArgs &a, OpOut &) { g_beeper = a.i(0); return OPE_OK; }
static int op_shape(CoreCtx *, const OpArgs &a, OpOut &) { g_shape = a.i(0); return OPE_OK; }
static int op_file(CoreCtx *, const OpArgs &a, OpOut &) { g_files++; g_file = a.s(0); return OPE_OK; }
static const char *st_state(CoreCtx *) { return g_state.c_str(); }
static const char *const mode_labels[] = { "fast", "slow" };
static const ArgSpec mode_arg[] = { ARG_ENUM("mode", mode_labels, 2) };
static const ArgSpec bus_arg[] = { ARG_INT("value", 8, 11) };
static const ArgSpec on_arg[] = { ARG_INT("value", 0, 1) };
static const ArgSpec shape_arg[] = { ARG_INT("value", 0, 2) };
static const ArgSpec file_arg[] = { ARG_PATH("path") };
static std::string g_slot_called;
static int op_slot(CoreCtx *, const OpArgs &a, OpOut &) { g_slot_called = a.s(0); return OPE_OK; }
static const ArgSpec slot_arg[] = { ARG_SLOT("slot") };
static int g_polls;
static int g_jobcalls, g_jobpct = 0, g_jobsteps = 3;                  // zl.jobstep: answers `more` for g_jobsteps calls, reporting g_jobpct
static int op_jobstep(CoreCtx *c, const OpArgs &, OpOut &o) { g_jobcalls++; c->job_pct = g_jobpct; o.more = g_jobcalls < g_jobsteps; return OPE_OK; }
static int op_poll(CoreCtx *, const OpArgs &, OpOut &o) { g_polls++; o.more = false; return OPE_OK; }
static const char *st_slot_a(CoreCtx *) { return "disk-a"; }
static const char *st_slot_b(CoreCtx *) { return 0; }
static std::string g_gate;                                        // "zl.gate": a row that hides itself while it has a value
static const char *st_gate(CoreCtx *) { return g_gate.empty() ? 0 : g_gate.c_str(); }

// T262 P1/P2/P5: a list of rows the library keeps, the operation that takes a row, the one that asks for more, the choice of a pick node, the search of a source
static std::string g_rows, g_choices, g_chosen, g_take_answer, g_found;
static int g_taken = -1, g_more_calls = 0, g_find_calls = 0;
static int op_take(CoreCtx *, const OpArgs &a, OpOut &o) { g_taken = a.i(0); g_take_answer = a.n > 1 ? a.s(1) : "-"; snprintf(o.msg, o.msglen, "took %d", (int)a.i(0)); return OPE_OK; }
static int g_phase = -1;
static int op_phase(CoreCtx *, const OpArgs &a, OpOut &) { g_phase = a.i(0); g_chosen = a.s(1); return OPE_OK; }
static const ArgSpec phase_args[] = { ARG_INT("phase", 0, 100), ARG_STRING("answer", 64) };
static int op_chosen(CoreCtx *, const OpArgs &a, OpOut &) { g_chosen = a.s(0); return OPE_OK; }
static int op_more(CoreCtx *, const OpArgs &, OpOut &)
{
    g_more_calls++;
    std::string add;
    const int base = 200 * g_more_calls;
    for (int i = 0; i < 200; i++) add += "\tResult " + std::to_string(base + i) + "\n";
    g_rows = g_rows.substr(0, g_rows.rfind("+")) + add + (g_more_calls < 2 ? "+More results" : "");
    return OPE_OK;
}
static int op_jobfind(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    g_jobcalls++;
    o.more = g_jobcalls < g_jobsteps && !c->job_cancel;
    if (!o.more) g_rows = "\tSlow one\n";
    g_found = a.s(0);
    return OPE_OK;
}
static int op_find(CoreCtx *, const OpArgs &a, OpOut &) { g_find_calls++; g_found = a.s(0); g_rows = "\tOne\tx\n\tTwo\n"; return OPE_OK; }
static const char *st_rows(CoreCtx *) { return g_rows.empty() ? 0 : g_rows.c_str(); }
static const char *st_choices(CoreCtx *) { return g_choices.empty() ? 0 : g_choices.c_str(); }
static const ArgSpec take_args[] = { ARG_INT("index", 0, 1000000), ARG_STRING("answer", 64) };
static const ArgSpec chosen_args[] = { ARG_STRING("value", 64) };
static const ArgSpec find_args[] = { ARG_STRING("query", 200) };

static void add_lib_ops(OpRegistry *r)
{
    r->contributor("zlib");
    r->add_op("zl.beep", 0, 0, op_beep);
    r->add_op("zl.mode", mode_arg, 1, op_mode);
    r->add_op("zl.set_bus", bus_arg, 1, op_bus);
    r->add_op("zl.set_beeper", on_arg, 1, op_beeper);
    r->add_op("zl.file", file_arg, 1, op_file);
    r->add_op("test.set_bus", bus_arg, 1, op_bus);
    r->add_op("test.set_shape", shape_arg, 1, op_shape);
    r->add_state("zl.state", st_state);
    r->add_op("zl.slotop", slot_arg, 1, op_slot);
    r->add_op("zl.poll", 0, 0, op_poll);
    r->add_op("zl.jobstep", 0, 0, op_jobstep);
    r->add_state("zl.drive_a.image", st_slot_a);
    r->add_state("zl.drive_b.image", st_slot_b);
    r->add_state("zl.gate", st_gate);
    r->add_op("zl.take", take_args, 2, op_take);
    r->add_op("zl.chosen", chosen_args, 1, op_chosen);
    r->add_op("zl.phase", phase_args, 2, op_phase);
    r->add_op("zl.more", 0, 0, op_more);
    r->add_op("zl.find", find_args, 1, op_find);
    r->add_op("zl.jobfind", find_args, 1, op_jobfind);
    r->add_state("zl.rows", st_rows);
    r->add_state("zl.choices", st_choices);
}

static const OpBind b_fast[] = { B_STR("mode", "fast") };
static const OpBind b_file[] = { B_FILE("path") };
static const char *const ext_tst[] = { ".tst" };
static const char *const ext_zl[] = { ".zl", ".ZL2" };
static const uint32_t sizes_4_8[] = { 4, 8 };
static const char *const beeper_values[] = { "Off", "On" };

// alpha: a library
static const LibNodeDecl alpha_nodes[] = {
    { "F5/settings", LN_OP, "Beep", "zl.beep", 0, 20 },
    { "F5/settings", LN_OP, "Mode fast", "zl.mode", 0, 10, 0, 0, 0, b_fast, 1 },
    { "F5/settings", LN_STATE, "State", 0, "zl.state", 30 },
    { "F7/settings", LN_OP, "Quick beep", "zl.beep", 0, 5, "zl.state != empty", 0, 0, 0, 0, true },
    { "F7/settings", LN_OP, "Only when busy", "zl.beep", 0, 6, "zl.state == busy" },
    { "F5/settings/audio", LN_GROUP, "Audio", 0, 0, 1 },
};
static const LibFileActionDecl alpha_fa[] = {
    { ext_tst, 1, "Sized action", "zl.file", b_file, 1, sizes_4_8, 2, false },
    { ext_zl, 2, "Open zl", "zl.file", b_file, 1, 0, 0, true },
};
static const LibSettingDecl alpha_set[] = {
    { 0, "Beeper", beeper_values, 2, 0, 0, 1, "zl.set_beeper" },
    { "drive_x", "Bus", 0, 0, 8, 11, 10, "zl.set_bus" },
};
static LibRecord alpha_rec()
{
    LibRecord r;
    memset(&r, 0, sizeof(r));
    r.nodes = alpha_nodes; r.nnodes = 6;
    r.file_actions = alpha_fa; r.nfile_actions = 2;
    r.settings = alpha_set; r.nsettings = 2;
    return r;
}
// zapp: an application, same anchor, lower order key than alpha's nodes
static const LibNodeDecl zapp_nodes[] = { { "F5/settings", LN_OP, "App button", "zl.beep", 0, 0 } };
static LibRecord zapp_rec()
{
    LibRecord r;
    memset(&r, 0, sizeof(r));
    r.nodes = zapp_nodes; r.nnodes = 1;
    return r;
}
// beta: a second library
static const LibNodeDecl beta_nodes[] = { { "F5/settings", LN_OP, "Beta button", "zl.beep", 0, 10 }, { "F5/settings", LN_OP, "Hidden one", "zl.beep", 0, 1, 0, 0, "lib_hidden" },
                                          { "F5/settings", LN_OP, "Old label", "zl.beep", 0, 2, 0, 0, "lib_renamed" } };
static LibRecord beta_rec()
{
    LibRecord r;
    memset(&r, 0, sizeof(r));
    r.nodes = beta_nodes; r.nnodes = 3;
    return r;
}

// ---- the rig ---------------------------------------------------------------------------------------------------------------------
struct Host : MenuHost {
    void info(int w, char *out, int n) { snprintf(out, n, "value %d", w); }
    bool return_to_host(char *msg, int n) { snprintf(msg, n, "no"); return false; }
};

struct Rig {
    FakeIo io; FakeFs fs; PxRec px; Host host; CoreCtx c; Settings *st; Video *vid; MenuCtl *m;
    CoreDesc merged; MenuMerge *mm;
    int refused;
    Rig(const Contributor *cs, int n, const Skeleton *skel = 0, const Overrides *ov = 0, const CoreDesc *base = &testcore::desc)
    {
        memset(&c, 0, sizeof(c));
        c.desc = base; c.io = &io; c.fs = &fs;
        OpRegistry *reg = ops_of(&c);
        add_lib_ops(reg);
        mm = new MenuMerge;
        refused = contrib_merge(base, skel, ov, cs, n, reg, &merged, mm);
        c.desc = &merged; c.merge = mm;
        st = new Settings(merged.settings, merged.nsettings, base->store, &fs, "/usb0/cores/t.cfg", 0);
        st->load();
        c.settings = st;
        vid = new Video(base->video, &px, &io);
        c.video = vid;
        fs.put_size("/usb0/g/four.tst", 4);
        fs.put_size("/usb0/g/six.tst", 6);
        fs.put_size("/usb0/g/x.zl", 100);
        m = new MenuCtl(&c, &host);
    }
    ~Rig() { delete m; delete mm; delete st; delete vid; delete c.ops; }
    // the labels of the entries of the tab `tab` of area `area`
    std::vector<std::string> tab(const char *area, const char *id)
    {
        std::vector<std::string> v;
        const MenuTree &t = m->tree();
        for (int a = 0; a < t.nareas; a++) {
            if (strcmp(t.a[a].key, area)) continue;
            for (int b = 0; b < t.a[a].ntabs; b++) {
                if (strcmp(t.a[a].tabs[b].id, id)) continue;
                for (int i = t.a[a].tabs[b].first; i < t.a[a].tabs[b].first + t.a[a].tabs[b].count; i++) v.push_back(t.e[i].label);
            }
        }
        return v;
    }
    std::string dump()
    {
        static char buf[16384];
        menu_dump(&merged, &m->tree(), buf, sizeof(buf));
        return buf;
    }
    std::string left(int i) { char l[100], r[100]; bool a; m->row(i, l, 100, r, 100, &a); return l; }
    std::string right(int i) { char l[100], r[100]; bool a; m->row(i, l, 100, r, 100, &a); return r; }
    std::string sub(int i) { char l[100], r[100], s[100]; bool a; m->row(i, l, 100, r, 100, &a, s, 100); return s; }
    bool active(int i) { char l[100], r[100]; bool a; m->row(i, l, 100, r, 100, &a); return a; }
    int find_row(const char *s) { for (int i = 0; i < m->rows(); i++) if (left(i) == s) return i; return -1; }
    void go_to(const char *s) { int r = find_row(s); if (r < 0) { printf("FAIL no row %s\n", s); g_fails++; return; } while (m->selected() > r) m->key(KEY_UP); while (m->selected() < r) m->key(KEY_DOWN); }
    bool has_err(const char *part) { for (int i = 0; i < mm->nerr; i++) if (strstr(mm->err[i], part)) return true; return false; }
};

static std::string join(const std::vector<std::string> &v)
{
    std::string s;
    for (size_t i = 0; i < v.size(); i++) s += (i ? " | " : "") + v[i];
    return s;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);                                 // a mutant may crash: what it said before has to be out
    CHECK(argc == 3);
    std::vector<uint8_t> desc_bin = slurp(argv[1]), def_bin = slurp(argv[2]);
    CHECK(!desc_check(&testcore::desc));

    LibRecord ra = alpha_rec(), rz = zapp_rec(), rb = beta_rec();
    Contributor alpha = { "alpha", CR_LIB, &ra }, zapp = { "zapp", CR_APP, &rz }, beta = { "beta", CR_LIB, &rb };

    // ---- nothing contributed: the descriptor's own, default route (every golden dump in test_desc is this)
    {
        Rig r(0, 0);
        CHECK_EQ(r.refused, 0);
        CHECK_EQ(r.merged.nsettings, testcore::desc.nsettings);
        CHECK_EQ(r.merged.nfiletypes, testcore::desc.nfiletypes);
        CHECK_STR(join(r.tab("F5", "settings")).c_str(), "Sound | Speed");
        CHECK_STR(join(r.tab("F1", "home")).c_str(), "Do it | Slot");          // T212: the core's tasks stand on Home, below them the drive rows
    }

    // ---- the nodes of a library, an application and a second library at one anchor
    {
        Contributor cs[] = { zapp, beta, alpha };                 // loaded in whatever order
        Rig r(cs, 3);
        CHECK_EQ(r.refused, 0);
        // the settings first: the descriptor's, then the contributed ones in merge order (alpha's Beeper; its Bus names a group no tab lists, so it
        // joins the ungrouped ones); then the nodes. Libraries before applications (D8 4); inside one rank the order key, and where two keys are
        // equal the contributor's name (alpha before beta), never the order they were loaded in.
        std::vector<std::string> got = r.tab("F5", "settings");
        CHECK_STR(join(got).c_str(), "Sound | Speed | Beeper | Bus | Audio | Hidden one | Old label | Mode fast | Beta button | Beep | State | App button");
        CHECK(got.back() == "App button");
        // the same contributors in the other input orders give the same tree and the same settings (point 3 of 2026-10-20)
        Contributor cs2[] = { alpha, beta, zapp }, cs3[] = { beta, zapp, alpha };
        Rig r2(cs2, 3), r3(cs3, 3);
        CHECK(r.dump() == r2.dump());
        CHECK(r.dump() == r3.dump());
        for (int i = 0; i < r.merged.nsettings; i++) {
            CHECK_STR(r.merged.settings[i].key, r2.merged.settings[i].key);
        }
    }

    // ---- hide and rename by id (the description's overrides)
    {
        static const char *const hide[] = { "lib_hidden" };
        static const NodeRename ren[] = { { "lib_renamed", "New label" }, { "nothing", "x" } };
        Overrides ov = { hide, 1, ren, 2 };
        Contributor cs[] = { beta };
        Rig r(cs, 1, 0, &ov);
        std::string g = join(r.tab("F5", "settings"));
        CHECK(g.find("Hidden one") == std::string::npos);
        CHECK(g.find("New label") != std::string::npos && g.find("Old label") == std::string::npos);
        CHECK_EQ(r.refused, 0);                                    // hiding is not a refusal
        // a node without an id has the id of its operation: hiding that hides it
        static const char *const hide_op[] = { "zl.beep" };
        Overrides ov2 = { hide_op, 1, 0, 0 };
        Contributor ca[] = { alpha };
        Rig r2(ca, 1, 0, &ov2);
        std::vector<std::string> t2 = r2.tab("F5", "settings");
        CHECK(std::find(t2.begin(), t2.end(), "Beep") == t2.end());
        CHECK(join(r2.tab("F5", "settings")).find("Mode fast") != std::string::npos);   // another operation
    }

    // ---- hide and rename reach settings (by key) and file actions (by label) too (Alex, 2026-10-26)
    {
        static const char *const hide[] = { "Beeper", "Sized action" };
        static const NodeRename ren[] = { { "Bus", "Bus ID" }, { "Open zl", "Open ZL" } };
        Overrides ov = { hide, 2, ren, 2 };
        Contributor ca[] = { alpha };
        Rig r(ca, 1, 0, &ov);
        bool beeper = false, bus = false, busid = false;
        for (int i = 0; i < r.merged.nsettings; i++) {
            beeper |= !strcmp(r.merged.settings[i].key, "Beeper");
            bus |= !strcmp(r.merged.settings[i].key, "Bus");
            busid |= !strcmp(r.merged.settings[i].key, "Bus ID");
        }
        CHECK(!beeper && !bus && busid);
        CHECK_EQ(r.refused, 0);
        const FileType *tst = 0, *zl = 0;
        for (int i = 0; i < r.merged.nfiletypes; i++) {
            if (!strcmp(r.merged.filetypes[i].ext, ".tst")) tst = &r.merged.filetypes[i];
            if (!strcmp(r.merged.filetypes[i].ext, ".zl")) zl = &r.merged.filetypes[i];
        }
        CHECK(tst && tst->nactions == 1);                            // only the descriptor's Mount
        CHECK(zl && zl->nactions == 1 && !strcmp(zl->actions[0].label, "Open ZL"));
        // the key of a renamed setting can collide: refused by name, as any key
        static const NodeRename clash[] = { { "Bus", "Sound" } };
        Overrides ov2 = { 0, 0, clash, 1 };
        Rig r2(ca, 1, 0, &ov2);
        CHECK(r2.has_err("\"Sound\" has a key that is taken"));
    }

    // ---- refusals by name, everything else merged (S3)
    {
        static const LibNodeDecl bad_nodes[] = {
            { "F9/nowhere", LN_OP, "Lost", "zl.beep", 0, 0 },
            { "F3/browse", LN_OP, "On the browser", "zl.beep", 0, 0 },
            { "F1/home", LN_OP, "On the info", "zl.beep", 0, 0 },
            { "F5/settings", LN_OP, "Ghost op", "zl.ghost", 0, 0 },
            { "F5/settings", LN_OP, "Needs warp", "zl.beep", 0, 0, 0, "warp" },
            { "F5/settings", LN_SETTING, "A setting node", 0, 0, 0 },
            { "F5/settings", LN_WIDGET, "A widget node", 0, 0, 0 },
            { "F5/settings", LN_OP, "Unbound", "zl.mode", 0, 0 },
            { "F5", LN_OP, "No tab", "zl.beep", 0, 0 },
            { "F5/settings", LN_STATE, "Ghost state", 0, "zl.nostate", 0 },
            { "F5/settings", LN_OP, "Fine", "zl.beep", 0, 0 },
        };
        static const LibSettingDecl bad_set[] = {
            { 0, "Sound", beeper_values, 2, 0, 0, 0, "zl.set_beeper" },           // the descriptor's key
            { 0, "Picture", beeper_values, 2, 0, 0, 0, "zl.set_beeper" },        // the application's key
            { 0, "Wide", 0, 0, 0, 40, 0, "zl.set_bus" },                          // 41 values
            { 0, "NoOp", beeper_values, 2, 0, 0, 0, "zl.nothing" },
            { 0, "Narrow", 0, 0, 0, 3, 0, "zl.set_beeper" },                      // the operation takes 0 .. 1
            { 0, "Fine too", beeper_values, 2, 0, 0, 1, "zl.set_beeper" },
        };
        static const char *const bad_ext[] = { "tst" };
        static const LibFileActionDecl bad_fa[] = {
            { bad_ext, 1, "No dot", "zl.file", b_file, 1, 0, 0, false },
            { ext_zl, 2, "Ghost", "zl.ghost", b_file, 1, 0, 0, false },
            { ext_zl, 2, "Fine", "zl.file", b_file, 1, 0, 0, false },
        };
        LibRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.nodes = bad_nodes; rec.nnodes = 11;
        rec.settings = bad_set; rec.nsettings = 6;
        rec.file_actions = bad_fa; rec.nfile_actions = 3;
        Contributor cs[] = { { "bad", CR_LIB, &rec } };
        Rig r(cs, 1);
        CHECK_EQ(r.refused, 10 + 5 + 2);
        CHECK(r.has_err("anchors F9/nowhere, which the route does not have"));
        CHECK(r.has_err("F3/browse") && r.has_err("file browser"));
        CHECK(r.has_err("F1/home") && r.has_err("information lines"));
        CHECK(r.has_err("zl.ghost"));
        CHECK(r.has_err("capability \"warp\""));
        CHECK(r.has_err("is a setting; a setting is declared as one"));
        CHECK(r.has_err("names a widget"));
        CHECK(r.has_err("leaves argument \"mode\" of zl.mode unbound"));
        CHECK(r.has_err("anchor is area/tab"));
        CHECK(r.has_err("zl.nostate"));
        CHECK(r.has_err("\"Sound\" has a key that is taken"));
        CHECK(r.has_err("\"Picture\" has a key that is taken"));
        CHECK(r.has_err("\"Wide\" has 41 values"));
        CHECK(r.has_err("zl.nothing"));
        CHECK(r.has_err("does not take the values of setting \"Narrow\""));
        CHECK(r.has_err("does not start with a dot"));
        // what was sound is there
        CHECK(join(r.tab("F5", "settings")).find("Fine") != std::string::npos);
        CHECK_EQ(r.merged.nsettings, testcore::desc.nsettings + 1);
        CHECK_STR(r.merged.settings[testcore::desc.nsettings].key, "Fine too");
        bool zl = false;
        for (int i = 0; i < r.merged.nfiletypes; i++) zl |= !strcmp(r.merged.filetypes[i].ext, ".zl") && r.merged.filetypes[i].nactions == 1;
        CHECK(zl);                                                 // "Fine" arrived, "Ghost" did not
        // refused ones are never in the tree
        CHECK(join(r.tab("F5", "settings")).find("Lost") == std::string::npos);
        // a core that has the capability takes the node
        CoreDesc d = testcore::desc;
        static const char *const caps[] = { "warp" };
        d.caps = caps; d.ncaps = 1;
        Rig r2(cs, 1, 0, 0, &d);
        CHECK(!r2.has_err("capability \"warp\""));
        CHECK(join(r2.tab("F5", "settings")).find("Needs warp") != std::string::npos);
    }

    // ---- settings: a range hands the operation the number, a list the index; groups choose the tab
    {
        Contributor cs[] = { alpha };
        Rig r(cs, 1);
        int bus = -1, beeper = -1;
        for (int i = 0; i < r.merged.nsettings; i++) {
            if (!strcmp(r.merged.settings[i].key, "Bus")) bus = i;
            if (!strcmp(r.merged.settings[i].key, "Beeper")) beeper = i;
        }
        CHECK(bus >= 0 && beeper >= 0);
        if (bus < 0 || beeper < 0) {
            DONE("test_contrib");
        }
        const Setting &b = r.merged.settings[bus];
        CHECK_EQ(b.nvalues, 4); CHECK_EQ(b.base, 8); CHECK_EQ(b.def, 2);
        CHECK_STR(b.values[0], "8"); CHECK_STR(b.values[3], "11");
        CHECK_EQ(r.st->value(bus), 2);                              // the default, 10
        r.m->open();
        r.m->key(KEY_F5); r.m->key('1');
        CHECK_STR(r.m->title(), "TEST");
        r.go_to("Bus");
        CHECK_STR(r.right(r.m->selected()).c_str(), "10");
        g_bus = -1;
        r.m->key(KEY_RETURN);                                       // one step: index 3, the number 11
        CHECK_EQ(g_bus, 11);
        CHECK_STR(r.right(r.m->selected()).c_str(), "11");
        r.m->key(KEY_RETURN);                                       // wraps to index 0: the number 8
        CHECK_EQ(g_bus, 8);
        CHECK_EQ(r.st->value(bus), 0);
        std::string cfg = r.fs.text("/usb0/cores/t.cfg");
        CHECK(cfg.find("Bus") != std::string::npos);                // written to the .cfg at once, by the key the library gave it
        g_beeper = -1;
        r.go_to("Beeper");
        r.m->key(KEY_RETURN);                                       // default On (index 1) -> Off
        CHECK_EQ(g_beeper, 0);
    }

    // ---- file actions: appended per extension after the descriptor's, a size set filters
    {
        Contributor cs[] = { alpha };
        Rig r(cs, 1);
        const FileType *tst = 0, *zl = 0;
        for (int i = 0; i < r.merged.nfiletypes; i++) {
            if (!strcmp(r.merged.filetypes[i].ext, ".tst")) tst = &r.merged.filetypes[i];
            if (!strcmp(r.merged.filetypes[i].ext, ".zl")) zl = &r.merged.filetypes[i];
        }
        CHECK(tst && zl);
        CHECK_EQ(tst->nactions, 2);
        CHECK_STR(tst->actions[0].label, "Mount");                  // the descriptor's first
        CHECK_STR(tst->actions[1].label, "Sized action");
        CHECK_EQ(tst->actions[1].nsize_in, 2);
        CHECK_EQ(zl->nactions, 1);
        int zl2 = 0;
        for (int i = 0; i < r.merged.nfiletypes; i++) zl2 += !strcasecmp(r.merged.filetypes[i].ext, ".zl2");
        CHECK_EQ(zl2, 1);
        CHECK(testcore::desc.nfiletypes == 1 && testcore::desc.filetypes[0].nactions == 1);   // the descriptor itself is untouched
        // the browser: a file of 4 bytes offers both actions, one of 6 only the first. RETURN on a file opens the sheet: the type's actions, then the eight of every file (View ... Delete)
        r.m->open();
        r.m->key(KEY_F3);                                           // the Library opens on the media level (T104.2), the stick is a row
        CHECK_STR(r.m->title(), "/");
        r.go_to("usb0/"); r.m->key(KEY_RETURN);
        CHECK_STR(r.m->title(), "/usb0");
        r.go_to("g/"); r.m->key(KEY_RETURN);
        CHECK(r.find_row("four.tst") >= 0 && r.find_row("six.tst") >= 0 && r.find_row("x.zl") >= 0);
        r.go_to("four.tst");
        r.m->key(KEY_RETURN);
        CHECK(r.m->chooser());
        CHECK_EQ(r.m->rows(), 2 + 8);
        CHECK(r.find_row("Mount") == 0 && r.find_row("Sized action") == 1 && r.find_row("View") == 2);
        r.m->key(KEY_DOWN);
        g_files = 0;
        r.m->key(KEY_RETURN);                                       // "Sized action": zl.file with the path
        CHECK_EQ(g_files, 1);
        CHECK(g_file == "/usb0/g/four.tst");
        r.go_to("six.tst");
        int mounted = testcore::g_mounted;
        r.m->key(KEY_RETURN);                                       // one action fits this size: the sheet has that one first
        CHECK(r.m->chooser());
        CHECK_EQ(r.m->rows(), 1 + 8);
        CHECK(r.find_row("Sized action") < 0 && r.find_row("Mount") == 0);
        r.m->key(KEY_RETURN);
        CHECK(!r.m->chooser());
        CHECK_EQ(testcore::g_mounted, mounted + 1);
        r.go_to("x.zl");
        r.m->key(KEY_RETURN);
        CHECK(r.m->chooser());
        CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_CLOSE);           // closes_menu came through
        CHECK_EQ(g_files, 2);
    }

    // ---- the menu a person drives: buttons, state lines, groups, conditions
    {
        Contributor cs[] = { alpha, zapp };
        Rig r(cs, 2);
        r.m->open();
        r.m->key(KEY_F5); r.m->key('1');
        int beep = r.find_row("Beep");
        CHECK(beep >= 0);
        CHECK(r.active(beep));
        g_beeps = 0;
        r.go_to("Beep");
        CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_REDRAW);
        CHECK_EQ(g_beeps, 1);
        CHECK(!r.m->status_bad());
        // an argument bound by the node
        g_mode = -1;
        r.go_to("Mode fast");
        r.m->key(KEY_RETURN);
        CHECK_EQ(g_mode, 0);
        // a state line shows the value and cannot be activated
        int sl = r.find_row("State");
        CHECK(sl >= 0 && !r.active(sl));
        CHECK_STR(r.right(sl).c_str(), "idle");
        g_state = "busy";
        CHECK_STR(r.right(sl).c_str(), "busy");
        CHECK(r.m->heading(sl));                                      // 6bd837e8d: a state line takes no focus, so no RETURN reaches it
        // a condition: "Only when busy" works while the state says so
        r.m->key(KEY_F7);                                              // T212 D6: the library's buttons stand in the APP area (the Quick tab is gone)
        int ob = r.find_row("Only when busy");
        CHECK(ob >= 0 && r.active(ob));
        g_state = "idle";
        CHECK(!r.active(ob));
        CHECK(r.sub(ob).find("Not available") != std::string::npos);
        r.go_to("Only when busy");
        g_beeps = 0;
        CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_NONE);
        CHECK_EQ(g_beeps, 0);
        g_state = "busy";
        r.m->key(KEY_RETURN);
        CHECK_EQ(g_beeps, 1);
        // a button that closes the menu
        r.go_to("Quick beep");
        CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_CLOSE);
        CHECK_EQ(r.refused, 0);
    }

    // ---- the route of a description: tabs, numbers, groups, nodes of its own
    {
        DescRecord *dr = new DescRecord;
        char why[200];
        CHECK_EQ(descrec_parse(desc_bin.data(), (uint32_t)desc_bin.size(), dr, why, sizeof(why)), 0);
        Contributor cs[] = { { "description", CR_DESC, &dr->rec }, alpha, beta };
        Rig r(cs, 3, dr->has_route ? &dr->skel : 0, &dr->ov);
        CHECK(dr->has_route);
        // the printed numbers are the description's: Tools is 3, Drives 4, though Drives comes first in the list
        const MenuTree &t = r.m->tree();
        int a5 = -1;
        for (int a = 0; a < t.nareas; a++) if (!strcmp(t.a[a].key, "F5")) a5 = a;
        CHECK(a5 >= 0);
        CHECK_STR(t.a[a5].name, "TEST");                            // no name in the description: the core's short title
        std::string order;
        for (int b = 0; b < t.a[a5].ntabs; b++) order += std::string(b ? "," : "") + t.a[a5].tabs[b].id + ":" + std::to_string(t.a[a5].tabs[b].n);
        CHECK_STR(order.c_str(), "summary:1,settings:2,drives:4,tools:3");
        r.m->open();
        r.m->key(KEY_F5);
        r.m->key('3');
        CHECK_STR(r.m->title(), "Tools");
        r.m->key('4');
        CHECK_STR(r.m->title(), "Drives");
        // the description's own nodes, setting, file action; the heading, the setting of its group
        CHECK_STR(join(r.tab("F5", "tools")).c_str(), "Run self test | Slot");
        std::string drives = join(r.tab("F5", "drives"));
        CHECK_STR(drives.c_str(), "Drive Bus ID | Drive A");       // the setting of the group first, the heading from the node behind it
        std::string settings = join(r.tab("F5", "settings"));
        CHECK(settings.find("Shape") != std::string::npos);        // ungrouped: with the descriptor's own
        CHECK(settings.find("Drive Bus ID") == std::string::npos);
        CHECK(settings.find("Bus") != std::string::npos);          // alpha's "Bus" names group drive_x, which no tab lists: lands here
        // the description's overrides hide beta's node and rename another
        CHECK(settings.find("Hidden one") == std::string::npos);
        CHECK(settings.find("Renamed by the description") != std::string::npos);
        CHECK_EQ(r.refused, 0);
        CHECK_EQ(r.mm->npolls, 1);                                  // the description's own poll
        CHECK_STR(r.mm->polls[0].owner, "description"); CHECK_EQ(r.mm->polls[0].period_ms, 250);
        delete dr;
    }

    // ---- the default route written as a description is the default route
    {
        DescRecord *dr = new DescRecord;
        char why[200];
        CHECK_EQ(descrec_parse(def_bin.data(), (uint32_t)def_bin.size(), dr, why, sizeof(why)), 0);
        CHECK(dr->has_route);
        Rig plain(0, 0), described(0, 0, &dr->skel, 0);
        CHECK(plain.dump() == described.dump());
        CHECK(plain.dump().find("tab 1 TEST") != std::string::npos);
        delete dr;
    }

    // ---- T239.3 L-3: a contributed tab is drawn by the layout its record names (LibRecord::tab_layouts, one string for each tab of `tabs`); a null entry, a record without the array
    // and a record built before it (its struct_size ends in front of the member) leave the tab a flat list
    {
        static const LibTabDecl tabs[] = {
            { "F5", "looked", "Looked", TW_NODES, 0, 0 },
            { "F5", "flat", "Flat", TW_NODES, 0, 0 },
        };
        static const char *const looks[] = { "split;title=Look;side=Side", 0 };
        static const LibNodeDecl lnodes[] = {                                      // a tab without a node is dropped from the strip
            { "F5/looked", LN_OP, "In looked", "zl.beep", 0, 1 },
            { "F5/flat", LN_OP, "In flat", "zl.beep", 0, 1 },
        };
        LibRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.struct_size = sizeof(rec);
        rec.tabs = tabs; rec.ntabs = 2;
        rec.nodes = lnodes; rec.nnodes = 2;
        rec.tab_layouts = looks;
        Contributor cs[] = { { "looker", CR_LIB, &rec } };
        for (int variant = 0; variant < 3; variant++) {
            LibRecord use = rec;
            if (variant == 1) {
                use.tab_layouts = 0;                                          // no array
            } else if (variant == 2) {
                use.struct_size = (uint32_t)offsetof(LibRecord, tab_layouts);  // a record of the base before 3.3 ends in front of it
            }
            Contributor c1[] = { { "looker", CR_LIB, &use } };
            Rig r(c1, 1);
            const MenuTree &t = r.m->tree();
            int a5 = -1;
            for (int a = 0; a < t.nareas; a++) if (!strcmp(t.a[a].key, "F5")) a5 = a;
            const char *looked = "?", *flat = "?";
            bool seen_looked = false, seen_flat = false;
            for (int b = 0; a5 >= 0 && b < t.a[a5].ntabs; b++) {
                if (!strcmp(t.a[a5].tabs[b].id, "looked")) { looked = t.a[a5].tabs[b].layout; seen_looked = true; }
                if (!strcmp(t.a[a5].tabs[b].id, "flat")) { flat = t.a[a5].tabs[b].layout; seen_flat = true; }
            }
            CHECK(seen_looked && seen_flat);
            if (variant == 0) {
                CHECK(looked && !strcmp(looked, "split;title=Look;side=Side"));
            } else {
                CHECK(looked == 0);
            }
            CHECK(flat == 0);
        }
        (void)cs;
    }

    // ---- contributed tabs (T104.1 D8, Alex 2026-10-26): appended after the area's own, the next free number, refused by name
    {
        static const LibTabDecl tabs[] = {
            { "F5", "late", "Late", TW_NODES, 0, 2 },                          // 2 is Video's number (checked while the strip still has room)
            { "F5", "extras", "Extras", TW_NODES, 0, 0 },
            { "F5", "special", "Special", TW_SETTINGS, "special", 0 },
            { "F9", "nowhere", "Nowhere", TW_NODES, 0, 0 },
            { "F5", "extras", "Again", TW_NODES, 0, 0 },                      // the id is taken
            { "F5", "fifth", "Fifth", TW_NODES, 0, 0 },
            { "F5", "sixth", "Sixth", TW_NODES, 0, 0 },
            { "F5", "seventh", "Seventh", TW_NODES, 0, 0 },
            { "F5", "eighth", "Eighth", TW_NODES, 0, 0 },
            { "F5", "ninth", "Ninth", TW_NODES, 0, 0 },                        // the strip has room for eight
            { "F5", "", "NoId", TW_NODES, 0, 0 },
        };
        static const LibNodeDecl nodes[] = {
            { "F5/extras", LN_OP, "In extras", "zl.beep", 0, 1 },
            { "F5/special", LN_OP, "In special", "zl.beep", 0, 1 },
            { "F5/ninth", LN_OP, "In the ninth", "zl.beep", 0, 1 },
        };
        static const LibSettingDecl sets[] = { { "special", "Volume", beeper_values, 2, 0, 0, 0, "zl.set_beeper" }, { 0, "Plain", beeper_values, 2, 0, 0, 0, "zl.set_beeper" } };
        LibRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.tabs = tabs; rec.ntabs = 11;
        rec.nodes = nodes; rec.nnodes = 3;
        rec.settings = sets; rec.nsettings = 2;
        Contributor cs[] = { { "tabber", CR_LIB, &rec } };
        Rig r(cs, 1);
        const MenuTree &t = r.m->tree();
        int a5 = -1;
        for (int a = 0; a < t.nareas; a++) if (!strcmp(t.a[a].key, "F5")) a5 = a;
        std::string order;
        for (int b = 0; b < t.a[a5].ntabs; b++) order += std::string(b ? "," : "") + t.a[a5].tabs[b].id + ":" + std::to_string(t.a[a5].tabs[b].n);
        // the area's own tabs keep their places and numbers; the new ones follow with the next free numbers
        CHECK_STR(order.c_str(), "settings:1,extras:2,special:3");
        CHECK_STR(join(r.tab("F5", "extras")).c_str(), "In extras");
        CHECK_STR(join(r.tab("F5", "special")).c_str(), "Volume | In special");          // the setting of the group is in the new tab ...
        CHECK(join(r.tab("F5", "settings")).find("Volume") == std::string::npos);
        CHECK(join(r.tab("F5", "settings")).find("Plain") != std::string::npos);     // ... the ungrouped one is not
        CHECK(r.has_err("area F9, which the route does not have"));
        CHECK(r.has_err("has a tab \"extras\" already"));
        CHECK(r.has_err("a tab numbered 2 already"));
        CHECK(r.has_err("\"ninth\" does not fit"));
        CHECK(r.has_err("a tab needs an area, an id, a name and a widget"));
        CHECK(r.has_err("anchors F5/ninth, which the route does not have"));         // its node is refused with it
        CHECK(r.has_err("area F5 has 8 tabs") || r.has_err("tabs; tab \"ninth\""));
        // the number key reaches a contributed tab
        r.m->open();
        r.m->key(KEY_F5);
        r.m->key('3');
        CHECK_STR(r.m->title(), "Special");
        g_beeps = 0;
        r.go_to("In special");
        r.m->key(KEY_RETURN);
        CHECK_EQ(g_beeps, 1);
        // the contribution of a description: its F7 tab, "More"
        DescRecord *dr = new DescRecord;
        char why[200];
        CHECK_EQ(descrec_parse(desc_bin.data(), (uint32_t)desc_bin.size(), dr, why, sizeof(why)), 0);
        Contributor cd[] = { { "description", CR_DESC, &dr->rec } };
        Rig r2(cd, 1, &dr->skel, &dr->ov);
        CHECK_EQ(r2.refused, 0);
        CHECK_STR(join(r2.tab("F7", "more")).c_str(), "Again");
        const MenuTree &t2 = r2.m->tree();
        int a7 = -1;
        for (int a = 0; a < t2.nareas; a++) if (!strcmp(t2.a[a].key, "F7")) a7 = a;
        CHECK_EQ(t2.a[a7].ntabs, 1); CHECK_EQ(t2.a[a7].tabs[0].n, 1);      // T212: the empty APP settings tab is left out, "More" is the one tab
        delete dr;
    }

    // ---- the poll of a contributor (D6): served, with the operation checked
    {
        static LibRecord good, noop, args, noperiod;
        memset(&good, 0, sizeof(good)); memset(&noop, 0, sizeof(noop)); memset(&args, 0, sizeof(args)); memset(&noperiod, 0, sizeof(noperiod));
        good.poll.op = "zl.poll"; good.poll.period_ms = 10;
        noop.poll.op = "zl.nopoll"; noop.poll.period_ms = 10;
        args.poll.op = "zl.mode"; args.poll.period_ms = 10;
        noperiod.poll.op = "zl.poll"; noperiod.poll.period_ms = 0;
        Contributor cs[] = { { "lib1", CR_LIB, &good }, { "lib2", CR_LIB, &noop }, { "lib3", CR_LIB, &args }, { "lib4", CR_LIB, &noperiod } };
        Rig r(cs, 4);
        CHECK_EQ(r.mm->npolls, 1);
        CHECK_STR(r.mm->polls[0].owner, "lib1"); CHECK_STR(r.mm->polls[0].op, "zl.poll"); CHECK_EQ(r.mm->polls[0].period_ms, 10);
        CHECK(!r.mm->polls[0].when_frozen);
        CHECK(r.has_err("poll operation \"zl.nopoll\" is not registered"));
        CHECK(r.has_err("takes arguments; a poll takes none"));
        CHECK(r.has_err("has no period"));
        CHECK_EQ(r.refused, 3);
        // the operation is a plain one: the poll task calls it
        OpResult o;
        CHECK_EQ(ops_call(&r.c, r.mm->polls[0].op, 0, 0, &o), OPE_OK);
        CHECK_EQ(g_polls, 1);
        // base 3.3: the declaration when_frozen goes on to the poll sink with the rest of the poll
        static LibRecord frozen;
        memset(&frozen, 0, sizeof(frozen));
        frozen.poll.op = "zl.poll"; frozen.poll.period_ms = 5; frozen.poll.when_frozen = true;
        Contributor cf[] = { { "lib5", CR_LIB, &frozen } };
        Rig rf(cf, 1);
        CHECK_EQ(rf.mm->npolls, 1);
        CHECK(rf.mm->polls[0].when_frozen);
    }

    // ---- $slot: a declaration is made once for each drive slot the core has (D15)
    {
        static const OpBind b_slot[] = { B_STR("slot", "$slot") };
        static const LibNodeDecl nodes[] = {
            { "F5/settings", LN_OP, "Reset $slot", "zl.slotop", 0, 1, "zl.$slot.image != empty", 0, "reset_$slot", b_slot, 1 },
            { "F5/settings", LN_STATE, "Image $slot", 0, "zl.$slot.image", 2 },
            { "F5/settings", LN_OP, "Once", "zl.beep", 0, 3 },
        };
        static const LibSettingDecl sets[] = { { "$slot", "Bus $slot", 0, 0, 8, 11, 8, "zl.set_bus" } };
        static const LibFileActionDecl fas[] = { { ext_zl, 2, "Mount in $slot", "zl.slotop", b_slot, 1, 0, 0, false } };
        LibRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.file_actions = fas; rec.nfile_actions = 1;
        rec.nodes = nodes; rec.nnodes = 3;
        rec.settings = sets; rec.nsettings = 1;
        Contributor cs[] = { { "drv", CR_LIB, &rec } };
        static const char *const caps2[] = { "sio", "drive_a", "drive_b" };
        CoreDesc d2 = testcore::desc;
        d2.caps = caps2; d2.ncaps = 3;
        Rig r(cs, 1, 0, 0, &d2);
        std::string tab = join(r.tab("F5", "settings"));
        CHECK(tab.find("Bus drive_a") != std::string::npos && tab.find("Bus drive_b") != std::string::npos);
        CHECK(tab.find("Reset drive_a | Image drive_a") != std::string::npos || (tab.find("Reset drive_a") != std::string::npos && tab.find("Reset drive_b") != std::string::npos));
        CHECK(tab.find("Image drive_a") != std::string::npos && tab.find("Image drive_b") != std::string::npos);
        CHECK(tab.find("Once") != std::string::npos);
        CHECK(tab.find("$slot") == std::string::npos);
        int once = 0;
        for (const std::string &l : r.tab("F5", "settings")) once += l == "Once";
        CHECK_EQ(once, 1);                                                // a declaration without $slot is not repeated
        CHECK_EQ(r.refused, 0);
        // each instance has its own binding, state and condition
        r.m->open();
        r.m->key(KEY_F5); r.m->key('1');
        g_slot_called.clear();
        r.go_to("Reset drive_b");
        CHECK(!r.active(r.find_row("Reset drive_b")));                    // drive_b has no image (state null = empty): the condition holds for != empty only when there is one
        CHECK(r.active(r.find_row("Reset drive_a")));
        r.go_to("Reset drive_a");
        r.m->key(KEY_RETURN);
        CHECK_STR(g_slot_called.c_str(), "drive_a");
        CHECK_STR(r.right(r.find_row("Image drive_a")).c_str(), "disk-a");
        // the id is expanded too: hide one instance
        static const char *const hide[] = { "reset_drive_a" };
        Overrides ov = { hide, 1, 0, 0 };
        Rig r2(cs, 1, 0, &ov, &d2);
        std::string t2 = join(r2.tab("F5", "settings"));
        CHECK(t2.find("Reset drive_a") == std::string::npos && t2.find("Reset drive_b") != std::string::npos);
        // a file action per slot
        int n_mount = 0;
        for (int i = 0; i < r.merged.nfiletypes; i++) {
            for (int k = 0; k < r.merged.filetypes[i].nactions; k++) n_mount += !strncmp(r.merged.filetypes[i].actions[k].label, "Mount in drive_", 15);
        }
        CHECK_EQ(n_mount, 4);                                               // two extensions, two slots
        // a core with no drive gets none of them, and no error
        Rig r3(cs, 1);
        std::string t3 = join(r3.tab("F5", "settings"));
        CHECK(t3.find("drive_") == std::string::npos && t3.find("Once") != std::string::npos);
        CHECK_EQ(r3.refused, 0);
        CHECK_EQ(contrib_slots(&d2, (const char **)new const char *[4], 4), 2);
    }

    // ---- a tab switch that runs the enter of a text grid: the tree follows what it changed, not the pump's next tick (T239.3 P7); the grid is the model of a library's text screen
    {
        // a cursor key or a tab key asks no state of the registry; but go() ran the screen's start (the `enter` of the grid the layout "terminal" names), which
        // switched a state a node's `hide` reads. The row is gone from the settings tab at once.
        static const SkelTab tabs5[] = { { "settings", "Settings", 1, TW_SETTINGS, 0, 0 }, { "mon", "Mon", 2, TW_NODES, 0, "terminal;grid=model" }, { "bare", "Bare", 3, TW_NODES, 0, "terminal" } };
        static const SkelArea areas[] = { { "F5", "Five", false, tabs5, 3 } };
        static const Skeleton sk = { areas, 1 };
        static const LibNodeDecl gn[] = {
            { "F5/settings", LN_OP, "Gated", "zl.beep", 0, 1, 0, 0, 0, 0, 0, false, "hide=@zl.gate" },
            { "F5/settings", LN_OP, "Plain", "zl.beep", 0, 2 },
            { "F5/mon", LN_OP, "Mon row", "zl.beep", 0, 1 },
            { "F5/bare", LN_OP, "Bare row", "zl.beep", 0, 1 },
        };
        LibRecord gr;
        memset(&gr, 0, sizeof(gr));
        gr.nodes = gn; gr.nnodes = 4;
        Contributor cg[] = { { "gated", CR_LIB, &gr } };
        g_gate.clear();
        mg_install();
        g_mg.on_enter = []() { g_gate = "on"; };
        Rig r(cg, 1, &sk);
        r.m->open();
        r.m->key(KEY_F5);
        CHECK_STR(join(r.tab("F5", "settings")).c_str(), "Sound | Speed | Gated | Plain");
        CHECK_EQ(g_mg.n_enter, 0);
        r.m->key('2');                                              // the tab of the terminal: its grid's enter sets zl.gate
        CHECK_EQ(g_mg.n_enter, 1);
        CHECK_STR(g_gate.c_str(), "on");
        CHECK_STR(join(r.tab("F5", "settings")).c_str(), "Sound | Speed | Plain");  // before the fix the tree showed "Sound | Speed | Gated | Plain" until the pump's tick
        // a pass of the menu loop calls the grid's tick while the node is shown; its answer says the screen changed
        CHECK_EQ(g_mg.n_tick, 0);
        g_mg.tick_answer = true;
        CHECK(r.m->pump(true));
        CHECK_EQ(g_mg.n_tick, 1);
        CHECK(!r.m->pump(true));                                    // the grid has nothing new: no redraw is asked for
        CHECK_EQ(g_mg.n_tick, 2);
        CHECK_EQ(g_mg.n_enter, 1);                                  // the passes did not enter the node again
        CHECK(r.m->goto_spec("F5/bare"));                           // a terminal without a grid parameter: nothing is entered, nothing is ticked
        r.m->pump(true);
        CHECK(g_mg.n_enter == 1 && g_mg.n_tick == 2);
        CHECK(r.m->goto_spec("F5/settings"));                       // away from the terminal: no tick
        r.m->pump(true);
        CHECK_EQ(g_mg.n_tick, 2);
        CHECK(r.m->goto_spec("F5/mon"));                            // entered again: enter again
        CHECK_EQ(g_mg.n_enter, 2);
        mg_uninstall();                                             // no finder: the name finds nothing, the node is entered and ticked by nobody
        CHECK(r.m->goto_spec("F5/settings"));
        CHECK(r.m->goto_spec("F5/mon"));
        r.m->pump(true);
        CHECK(g_mg.n_enter == 2 && g_mg.n_tick == 2);
        g_gate.clear();
    }

    // ---- cond_holds
    {
        struct S { static const char *get(void *, const char *n) { return !strcmp(n, "a") ? "x" : !strcmp(n, "e") ? "empty" : 0; } };
        CHECK(cond_holds(0, S::get, 0));
        CHECK(cond_holds("", S::get, 0));
        CHECK(cond_holds("a", S::get, 0));
        CHECK(!cond_holds("none", S::get, 0));
        CHECK(cond_holds("a == x", S::get, 0));
        CHECK(!cond_holds("a == y", S::get, 0));
        CHECK(cond_holds("a != y", S::get, 0));
        CHECK(!cond_holds("a != x", S::get, 0));
        CHECK(cond_holds("none == empty", S::get, 0));               // no value is `empty`
        CHECK(cond_holds("e == empty", S::get, 0));
        CHECK(!cond_holds("a == empty", S::get, 0));
        CHECK(cond_holds("a!=empty", S::get, 0));
    }

    // ---- T262 P3: a tab with `show=@state` is on the strip only while the state has a value; its number stays its own; the tab on screen that goes away leaves for the first shown tab
    {
        static const LibTabDecl tabs[] = {
            { "F5", "looked", "Looked", TW_NODES, 0, 0 },
            { "F5", "flat", "Flat", TW_NODES, 0, 0 },
        };
        static const char *const looks[] = { "split;title=Look;side=Side;show=@zl.gate", 0 };
        static const LibNodeDecl lnodes[] = {
            { "F5/looked", LN_OP, "In looked", "zl.beep", 0, 1 },
            { "F5/flat", LN_OP, "In flat", "zl.beep", 0, 1 },
        };
        LibRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.struct_size = sizeof(rec);
        rec.tabs = tabs; rec.ntabs = 2;
        rec.nodes = lnodes; rec.nnodes = 2;
        rec.tab_layouts = looks;
        Contributor cs[] = { { "shower", CR_LIB, &rec } };
        g_gate.clear();
        Rig r(cs, 1);
        const MenuTree &t = r.m->tree();
        int a5 = -1, tl = -1, tf = -1;
        for (int a = 0; a < t.nareas; a++) if (!strcmp(t.a[a].key, "F5")) a5 = a;
        for (int b = 0; a5 >= 0 && b < t.a[a5].ntabs; b++) {
            if (!strcmp(t.a[a5].tabs[b].id, "looked")) tl = b;
            if (!strcmp(t.a[a5].tabs[b].id, "flat")) tf = b;
        }
        CHECK(tl >= 0 && tf >= 0);
        const int nl = t.a[a5].tabs[tl].n, nf = t.a[a5].tabs[tf].n;
        r.m->open();
        r.m->key(KEY_F5);
        CHECK(r.m->tab_is_hidden(a5, tl) && !r.m->tab_is_hidden(a5, tf));        // the state has no value: off the strip
        r.m->key('0' + nl);
        CHECK(r.m->tab() != tl);                                                  // its digit does nothing
        CHECK(r.m->goto_spec("F5/looked") && r.m->tab() != tl);                   // a goto does not land on it
        g_gate = "on";
        r.io.delay_ms(2100); r.m->pump(true);                                     // what the machine holds changed outside the menu: seen at the next look
        CHECK(!r.m->tab_is_hidden(a5, tl));                                       // on the strip
        CHECK_EQ(t.a[a5].tabs[tl].n, nl);                                         // its number is the same
        CHECK(nf != nl);
        r.m->key('0' + nl);
        CHECK_EQ(r.m->tab(), tl);
        g_gate.clear();                                                           // the state empties while its tab is on screen
        r.io.delay_ms(2100); r.m->pump(true);
        CHECK(r.m->tab_is_hidden(a5, tl) && r.m->tab() != tl && r.m->area() == a5);
        CHECK(!r.m->tab_is_hidden(a5, r.m->tab()));                              // on a tab that is shown
        g_gate.clear();
    }

    // ---- T262 P8: the focused row of a screen hides (its `hide` state got a value): the focus goes to the next row that takes it, else to the one before
    {
        static const LibTabDecl tabs[] = { { "F5", "scr", "Scr", TW_NODES, 0, 0 } };
        static const char *const looks[] = { "split;title=Scr;side=Side", 0 };
        static const LibNodeDecl nodes[] = {
            { "F5/scr", LN_OP, "Row A", "zl.beep", 0, 1 },
            { "F5/scr", LN_OP, "Row B", "zl.beep", 0, 2, 0, 0, 0, 0, 0, false, "hide=@zl.gate" },
            { "F5/scr", LN_OP, "Row C", "zl.beep", 0, 3 },
            { "F5/scr", LN_OP, "Row D", "zl.beep", 0, 4, 0, 0, 0, 0, 0, false, "hide=@zl.gate" },
        };
        LibRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.struct_size = sizeof(rec);
        rec.tabs = tabs; rec.ntabs = 1;
        rec.nodes = nodes; rec.nnodes = 4;
        rec.tab_layouts = looks;
        Contributor cs[] = { { "hider", CR_LIB, &rec } };
        for (int variant = 0; variant < 2; variant++) {
            g_gate.clear();
            Rig r(cs, 1);
            r.m->open();
            CHECK(r.m->goto_spec("F5/scr"));
            CHECK(r.m->screen());
            const int want = variant == 0 ? r.find_row("Row B") : r.find_row("Row D");
            CHECK(want >= 0);
            while (r.m->selected() < want) r.m->key(KEY_DOWN);
            CHECK_EQ(r.m->selected(), want);
            g_gate = "on";                                                         // B and D hide
            r.io.delay_ms(2100); r.m->pump(true);
            const int now = r.m->selected();
            if (variant == 0) {
                CHECK_EQ(now, r.find_row("Row C"));                                // B hid: the next visible row
            } else {
                CHECK_EQ(now, r.find_row("Row C"));                                // D hid, nothing after it: the one before
            }
            g_gate.clear();
        }
    }

    // ---- T262 P6: a row of the core's key table that names itself ("legend") is in the foot legend of the tab it is for, while its state holds
    {
        static const LibTabDecl tabs[] = { { "F5", "keyed", "Keyed", TW_NODES, 0, 0 }, { "F5", "other", "Other", TW_NODES, 0, 0 } };
        static const LibNodeDecl nodes[] = { { "F5/keyed", LN_OP, "Row", "zl.beep", 0, 1 }, { "F5/other", LN_OP, "Row", "zl.beep", 0, 1 } };
        LibRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.struct_size = sizeof(rec);
        rec.tabs = tabs; rec.ntabs = 2;
        rec.nodes = nodes; rec.nnodes = 2;
        Contributor cs[] = { { "keyer", CR_LIB, &rec } };
        static CoreKey keys[3];
        memset(keys, 0, sizeof(keys));
        for (int i = 0; i < 3; i++) {
            keys[i].node = i == 2 ? "other" : "keyed";
            keys[i].row.stage = KS_CORE;
            keys[i].row.intent = MI_OP;
            keys[i].row.op = "zl.beep";
        }
        keys[0].row.lo = keys[0].row.hi = 0x0F; keys[0].legend = "Clear";               // CTRL+O
        keys[1].row.lo = keys[1].row.hi = 'x'; keys[1].legend = "Go on"; keys[1].row.when = "zl.gate";
        keys[2].row.lo = keys[2].row.hi = 'q'; keys[2].legend = "Elsewhere";
        g_gate.clear();
        Rig r(cs, 1);
        r.mm->keys = keys; r.mm->nkeys = 3;
        r.m->open();
        CHECK(r.m->goto_spec("F5/keyed"));
        char leg[200];
        r.m->footer_legend(leg, sizeof(leg));
        CHECK(strstr(leg, "CTRL+O CLEAR") != 0);                                         // the row names its key
        CHECK(strstr(leg, "X GO ON") == 0 && strstr(leg, "ELSEWHERE") == 0);             // its state holds not / it is for another tab
        g_gate = "on";
        r.m->footer_legend(leg, sizeof(leg));
        CHECK(strstr(leg, "CTRL+O CLEAR") != 0 && strstr(leg, "X GO ON") != 0);
        CHECK(r.m->goto_spec("F5/other"));
        r.m->footer_legend(leg, sizeof(leg));
        CHECK(strstr(leg, "Q ELSEWHERE") != 0 && strstr(leg, "CLEAR") == 0);
        g_gate.clear();
    }

    // ---- T262 P4: a node's job shows the node's own words while it runs and a percentage only once the operation reports one (not "Building the image 0%")
    {
        static const LibTabDecl tabs[] = { { "F5", "jobs", "Jobs", TW_NODES, 0, 0 } };
        static const LibNodeDecl nodes[] = {
            { "F5/jobs", LN_OP, "Search", "zl.jobstep", 0, 1, 0, 0, 0, 0, 0, false, "job;busy=Asking the server" },
            { "F5/jobs", LN_OP, "Fetch", "zl.jobstep", 0, 2, 0, 0, 0, 0, 0, false, "job" },
        };
        LibRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.struct_size = sizeof(rec);
        rec.tabs = tabs; rec.ntabs = 1;
        rec.nodes = nodes; rec.nnodes = 2;
        Contributor cs[] = { { "jobber", CR_LIB, &rec } };
        Rig r(cs, 1);
        r.m->open();
        CHECK(r.m->goto_spec("F5/jobs"));
        r.go_to("Search");
        g_jobcalls = 0; g_jobpct = 0;
        r.m->key(KEY_RETURN);                                                    // the job starts
        CHECK(r.m->busy());
        CHECK_STR(r.left(0).c_str(), "Asking the server");                        // the node's `busy` text, not "Building the image"
        CHECK_STR(r.right(0).c_str(), "");                                        // no percentage: the operation reported none
        CHECK(strstr(r.m->status(), "Asking the server") != 0 && strstr(r.m->status(), "Building") == 0);
        CHECK(r.m->tick() && r.m->busy());                                        // a step that reports nothing
        CHECK_STR(r.right(0).c_str(), "");
        CHECK(strstr(r.m->status(), "Asking the server") != 0 && strstr(r.m->status(), "%") == 0);
        g_jobpct = 40;
        CHECK(r.m->tick() && r.m->busy());                                        // a step that reports 40
        CHECK_STR(r.right(0).c_str(), "40%");
        CHECK_STR(r.left(0).c_str(), "Asking the server");
        CHECK(r.m->tick() && !r.m->busy());                                       // the last step
        // a node with no `busy` is called what it is labelled
        r.go_to("Fetch");
        g_jobcalls = 0; g_jobpct = 0;
        r.m->key(KEY_RETURN);
        CHECK(r.m->busy());
        CHECK_STR(r.left(0).c_str(), "Fetch");
        CHECK_STR(r.right(0).c_str(), "");
        while (r.m->tick()) {}
    }

    // ---- T262 P2: a node with `pick = "@state"` lists the state's rows (47 of them: the old list stopped at 40); RETURN on a row runs the node's operation with the value as its text
    {
        static const OpBind b_val[] = { B_TEXT("value") };
        static const LibNodeDecl nodes[] = { { "F5/settings", LN_OP, "Year", "zl.chosen", 0, 1, 0, 0, 0, b_val, 1, false, "pick=@zl.choices" } };
        LibRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.struct_size = sizeof(rec);
        rec.nodes = nodes; rec.nnodes = 1;
        Contributor cs[] = { { "picker", CR_LIB, &rec } };
        g_choices.clear();
        for (int y = 1; y <= 47; y++) g_choices += std::to_string(1900 + y) + "\tYear " + std::to_string(1900 + y) + (y % 2 ? "\tleft\n" : "\n");
        g_chosen.clear();
        Rig r(cs, 1);
        r.m->open();
        r.m->key(KEY_F5);
        r.go_to("Year");
        CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_REDRAW);
        CHECK(r.m->picking());
        CHECK_EQ(r.m->rows(), 47);                                                  // every value, no cap
        CHECK_STR(r.left(0).c_str(), "Year 1901");
        CHECK_STR(r.right(0).c_str(), "left");
        r.m->key(KEY_END);
        CHECK_EQ(r.m->selected(), 46);
        CHECK_STR(r.left(46).c_str(), "Year 1947");
        r.m->key(KEY_PAGEUP);
        CHECK_EQ(r.m->selected(), 36);
        r.m->key(KEY_END);
        r.m->key(KEY_RETURN);                                                        // the list closes, the operation got the value
        CHECK(!r.m->picking());
        CHECK_STR(g_chosen.c_str(), "1947");
        r.go_to("Year");
        r.m->key(KEY_RETURN);
        r.m->key(KEY_ESCAPE);                                                        // a cancel runs nothing
        CHECK(!r.m->picking());
        CHECK_STR(g_chosen.c_str(), "1947");
        g_chosen.clear();
        // the base's own call (ops_ask_list) shows the same list, with the same size
        g_choices.clear();
        for (int y = 0; y < 500; y++) g_choices += "v" + std::to_string(y) + "\n";
        ops_ask_list(&r.c, "zl.phase", 7, "Many", "zl.choices");
        CHECK(r.m->take_prompt());
        CHECK_EQ(r.m->rows(), 500);
        r.m->key(KEY_END);
        r.m->key(KEY_RETURN);                                                        // the phase and the value reach the operation that asked
        CHECK_EQ(g_phase, 7);
        CHECK_STR(g_chosen.c_str(), "v499");
    }

    // ---- T262 review 2: a question left over from an earlier call is never raised by a later, unrelated menu call
    {
        static const OpBind b_val[] = { B_TEXT("value") };
        static const LibNodeDecl nodes[] = { { "F5/settings", LN_OP, "Many", "zl.chosen", 0, 1, 0, 0, 0, b_val, 1, false, "pick=@zl.choices" },
                                             { "F5/settings", LN_OP, "Beep", "zl.beep", 0, 2 } };
        LibRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.struct_size = sizeof(rec);
        rec.nodes = nodes; rec.nnodes = 2;
        Contributor cs[] = { { "stale", CR_LIB, &rec } };
        g_choices = "a\nb\n";
        Rig r(cs, 1);
        r.m->open();
        r.m->key(KEY_F5);
        ops_ask_list(&r.c, "zl.phase", 7, "Old", "zl.choices");                      // an operation of another door asked, nobody took it
        r.go_to("Beep");
        int beeps = g_beeps;
        r.m->key(KEY_RETURN);
        CHECK_EQ(g_beeps, beeps + 1);
        CHECK(!r.m->picking());
        CHECK_EQ(r.c.want_prompt.kind, 0);
        printf("PASS a stale question is not raised by a later menu call\n");
    }

    // ---- T262 P2: the rows are bounded by the memory budget, not by a number: with a heap that is nearly full the list is cut at the end of a line and says so; with room it is whole
    {
        static const OpBind b_val[] = { B_TEXT("value") };
        static const LibNodeDecl nodes[] = { { "F5/settings", LN_OP, "Many", "zl.chosen", 0, 1, 0, 0, 0, b_val, 1, false, "pick=@zl.choices" } };
        LibRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.struct_size = sizeof(rec);
        rec.nodes = nodes; rec.nnodes = 1;
        Contributor cs[] = { { "budget", CR_LIB, &rec } };
        g_choices.clear();
        for (int y = 0; y < 1000; y++) g_choices += "value " + std::to_string(y) + "\n";           // about 10 KB
        Rig r(cs, 1);
        r.m->open();
        r.m->key(KEY_F5);
        r.go_to("Many");
        r.m->key(KEY_RETURN);
        CHECK_EQ(r.m->rows(), 1000);                                                 // no heap figure: only the hard limits
        CHECK(strstr(r.m->status(), "not shown") == 0);
        r.m->key(KEY_ESCAPE);
        ListGrow::free_heap() = []() -> uint32_t { return 4000; };                   // a quarter of it: 1000 bytes of text
        r.go_to("Many");
        r.m->key(KEY_RETURN);
        CHECK(r.m->rows() > 50 && r.m->rows() < 200);
        CHECK(r.left(r.m->rows() - 1).substr(0, 6) == "value ");                      // whole lines only
        CHECK(strstr(r.m->status(), "not shown") != 0);
        {                                                                            // T262 review 3: the text and the offset of every row (an int) stay within the quarter
            size_t bytes = 0;
            for (int i = 0; i < r.m->rows(); i++) bytes += r.left(i).size() + 1 + sizeof(int);
            CHECK(bytes <= 1000);
            printf("PASS the list's text and offsets stay within the quarter of the heap (%zu of 1000)\n", bytes);
        }
        ListGrow::free_heap() = 0;
        r.m->key(KEY_ESCAPE);
        g_choices.clear();
    }

    // ---- T262 P1: a node of kind `list`: the rows come from the library at run time (200 of them, none declared), RETURN on a row calls the operation with its index, the last row asks for more
    {
        static const LibNodeDecl nodes[] = { { "F5/settings", LN_LIST, "Results", "zl.take", "zl.rows", 1, 0, 0, 0, 0, 0, false, "more=zl.more" } };
        LibRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.struct_size = sizeof(rec);
        rec.nodes = nodes; rec.nnodes = 1;
        Contributor cs[] = { { "lister", CR_LIB, &rec } };
        g_rows.clear();
        g_taken = -1; g_more_calls = 0;
        Rig r(cs, 1);
        CHECK(!r.has_err("Results"));
        r.m->open();
        r.m->key(KEY_F5);
        const int row = r.find_row("Results");
        CHECK(row >= 0);
        CHECK_STR(r.right(row).c_str(), "None yet");                                  // no rows yet
        for (int i = 0; i < 200; i++) g_rows += "id" + std::to_string(i) + "\tResult " + std::to_string(i) + "\t" + std::to_string(i * 3) + " KB\t" + (i % 2 ? "amber" : "muted") + "\tside one " + std::to_string(i) + "\tside two\n";
        g_rows += "+More results";
        CHECK_STR(r.right(row).c_str(), "200 >");                                     // T262 D4: the '+' row is not a result
        r.go_to("Results");
        CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_REDRAW);
        CHECK(r.m->picking());
        CHECK_EQ(r.m->rows(), 201);                                                   // 200 results and the row that asks for more
        CHECK_STR(r.left(0).c_str(), "Result 0");
        CHECK_STR(r.right(5).c_str(), "15 KB");
        CHECK_EQ(r.m->list_side_n(5), 2);                                             // what the row says about itself, for the side panel (not the foot)
        { char sl[80]; r.m->list_side(5, 0, sl, sizeof(sl)); CHECK_STR(sl, "side one 5"); r.m->list_side(5, 1, sl, sizeof(sl)); CHECK_STR(sl, "side two"); }
        CHECK_STR(r.sub(5).c_str(), "RETURN opens this row, <- goes back");
        CHECK_EQ(r.m->list_tone(5), 3);
        CHECK_EQ(r.m->list_tone(4), 0);
        CHECK_STR(r.left(200).c_str(), "More results");
        r.m->key(KEY_DOWN); r.m->key(KEY_DOWN); r.m->key(KEY_DOWN);
        r.m->key(KEY_RETURN);                                                         // RETURN on the third row after the first: the operation gets its index and its value
        CHECK_EQ(g_taken, 3);
        CHECK_STR(g_take_answer.c_str(), "id3");
        CHECK(r.m->picking());                                                        // the list stays up
        CHECK_STR(r.m->status(), "took 3");
        r.m->key(KEY_END);
        CHECK_EQ(r.m->selected(), 200);
        r.m->key(KEY_RETURN);                                                         // more: the library appends, the list is read again and keeps the cursor
        CHECK_EQ(g_more_calls, 1);
        CHECK_EQ(r.m->rows(), 401);
        CHECK_EQ(r.m->selected(), 200);
        CHECK_STR(r.left(200).c_str(), "Result 200");
        r.m->key(KEY_ESCAPE);
        CHECK(!r.m->picking());
        // T262 D4: the tone `disabled` greys the whole row (its active flag is what the page draws dim); a note is no row, so the node counts 2
        {
            g_rows = "a\tOpen one\t1 KB\tcyan\nb\tGone\t2 KB\tdisabled\n!A note\n+More";
            Rig rd(cs, 1);
            rd.m->open();
            rd.m->key(KEY_F5);
            const int rw = rd.find_row("Results");
            CHECK_STR(rd.right(rw).c_str(), "2 >");
            rd.go_to("Results");
            rd.m->key(KEY_RETURN);
            CHECK(rd.m->picking());
            char l[100], rt[100]; bool act = false;
            rd.m->row(0, l, 100, rt, 100, &act); CHECK(act);
            rd.m->row(1, l, 100, rt, 100, &act); CHECK(!act);
            CHECK_EQ(rd.m->list_tone(1), 5);
            CHECK_STR(rt, "2 KB");
        }
        g_rows.clear();
        // a record that is nothing but its rows state (no operation for a row) is refused by name
        static const LibNodeDecl bad[] = { { "F5/settings", LN_LIST, "Bare list", 0, "zl.rows", 1 } };
        LibRecord br;
        memset(&br, 0, sizeof(br));
        br.nodes = bad; br.nnodes = 1;
        Contributor cb[] = { { "bad", CR_LIB, &br } };
        Rig rb(cb, 1);
        CHECK(rb.has_err("without an operation for a row"));
        g_rows.clear();
    }

    // ---- T262 D4 (13): `ask` holds 24 characters and refuses file-name characters, as before; `ask_max` and `ask_any` make it a free text of its own limit
    {
        static const OpBind b_text[] = { B_TEXT("value") };
        static const LibNodeDecl nodes[] = {
            { "F5/settings", LN_OP, "Name it", "zl.chosen", 0, 1, 0, 0, 0, b_text, 1, false, "ask=Name" },
            { "F5/settings", LN_OP, "Say it", "zl.chosen", 0, 2, 0, 0, 0, b_text, 1, false, "ask=Text;ask_max=100;ask_any" },
        };
        LibRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.struct_size = sizeof(rec);
        rec.nodes = nodes; rec.nnodes = 2;
        Contributor cs[] = { { "asker", CR_LIB, &rec } };
        Rig r(cs, 1);
        CHECK(!r.has_err("Say it"));
        r.m->open();
        r.m->key(KEY_F5);
        const char *typed = "a:b/c?d*e<f>g|h 123456789012345678901234567890";                          // 47 characters, the first 7 of them are not file name characters
        r.go_to("Name it");
        r.m->key(KEY_RETURN);
        CHECK(r.m->entering());
        for (const char *p = typed; *p; p++) r.m->key(*p);
        r.m->key(KEY_RETURN);
        CHECK_EQ((int)g_chosen.size(), 24);                                                  // the file name: 24 characters, none of the refused ones
        CHECK(g_chosen.find_first_of("/\\:*?\"<>|") == std::string::npos);
        r.go_to("Say it");
        r.m->key(KEY_RETURN);
        CHECK(r.m->entering());
        for (const char *p = typed; *p; p++) r.m->key(*p);
        r.m->key(KEY_RETURN);
        CHECK_STR(g_chosen.c_str(), typed);                                                  // the free text: every character, all 47 (the limit is 100)
    }

    static const SkelTab f3tabs[] = { { "browse", "Browse", 1, TW_BROWSER, 0, 0 }, { "search", "Search", 2, TW_BROWSER, 0, 0 } };
    static const SkelArea f3areas[] = { { "F3", "Library", false, f3tabs, 2 } };
    static const Skeleton f3sk = { f3areas, 1 };

    // ---- T262 review 6: open() lands on the first tab that is shown; an area whose every tab is off keeps its screen when a state changes
    {
        static const SkelTab offtabs[] = { { "a", "A", 1, TW_NODES, 0, "split;show=@zl.drive_b.image" }, { "b", "B", 2, TW_NODES, 0, "split;show=@zl.drive_b.image" } };
        static const SkelTab mixtabs[] = { { "a", "A", 1, TW_NODES, 0, "split;show=@zl.drive_b.image" }, { "b", "B", 2, TW_NODES, 0, 0 } };
        static const SkelArea offareas[] = { { "F5", "Lab", false, offtabs, 2 } };
        static const SkelArea mixareas[] = { { "F5", "Lab", false, mixtabs, 2 } };
        static const Skeleton offsk = { offareas, 1 };
        static const Skeleton mixsk = { mixareas, 1 };
        static const OpBind b_val[] = { B_TEXT("value") };
        static const LibNodeDecl nodes[] = { { "F5/a", LN_OP, "Many", "zl.chosen", 0, 1, 0, 0, 0, b_val, 1, false, "pick=@zl.choices" },
                                             { "F5/a", LN_OP, "Hider", "zl.beep", 0, 2, 0, 0, 0, 0, 0, false, "hide=@zl.gate" },
                                             { "F5/b", LN_OP, "In b", "zl.beep", 0, 1 } };
        LibRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.struct_size = sizeof(rec);
        rec.nodes = nodes; rec.nnodes = 3;
        Contributor cs[] = { { "lab", CR_LIB, &rec } };
        g_gate.clear();
        g_choices = "a\nb\n";
        {
            Rig r(cs, 1, &mixsk);
            r.m->open();
            CHECK_EQ(r.m->tab(), 1);                                                 // not the tab that is off
        }
        {
            Rig r(cs, 1, &offsk);
            r.m->open();
            CHECK_EQ(r.m->tab(), 0);
            r.go_to("Many");
            r.m->key(KEY_RETURN);
            CHECK(r.m->picking());
            g_gate = "on";                                                           // a state of the tree changes: the signature is not the same
            r.io.delay_ms(2100); r.m->pump(true);
            CHECK(r.m->picking());                                                   // the screen was not reset: nothing moved
            g_gate.clear();
        }
        printf("PASS open() skips a tab that is off; an area with every tab off is not reset again\n");
    }

    // ---- T262 review 4: a rebuild of the tree (the list of libraries changed) closes an open node pick and takes the search form back to This device
    {
        static const OpBind b_val[] = { B_TEXT("value") };
        static const LibNodeDecl pn[] = { { "F5/settings", LN_OP, "Many", "zl.chosen", 0, 1, 0, 0, 0, b_val, 1, false, "pick=@zl.choices" } };
        static const LibNodeDecl sn[] = { { "F3/search", LN_LIST, "Online results", "zl.take", "zl.rows", 1, 0, 0, 0, 0, 0, false, "search=Online;find=zl.find;fields=Group,Handle;more=zl.more" } };
        LibRecord pr, sr;
        memset(&pr, 0, sizeof(pr)); memset(&sr, 0, sizeof(sr));
        pr.struct_size = sr.struct_size = sizeof(pr);
        pr.nodes = pn; pr.nnodes = 1;
        sr.nodes = sn; sr.nnodes = 1;
        Contributor pc[] = { { "rebuilt", CR_LIB, &pr } };
        Contributor sc[] = { { "rebuilt", CR_LIB, &sr } };
        g_choices = "a\nb\n";
        g_chosen.clear();
        {
            Rig r(pc, 1);
            r.m->open();
            r.m->key(KEY_F5);
            r.go_to("Many");
            r.m->key(KEY_RETURN);
            CHECK(r.m->picking());
            r.m->rebuild();                                                          // the tree is made again under the open list
            CHECK(!r.m->picking());
            r.m->key(KEY_RETURN);
            CHECK(g_chosen.empty());                                                 // no operation ran on a node of the old tree
        }
        Rig r(sc, 1, &f3sk);
        r.m->open();
        CHECK(r.m->goto_spec("F3/search"));
        r.m->key('/');
        CHECK(r.m->search_form());
        r.m->key(KEY_RETURN);
        CHECK_STR(r.right(0).c_str(), "Online");
        r.m->rebuild();
        CHECK_STR(r.right(0).c_str(), "This device");
        printf("PASS a rebuild closes a node pick and resets the search source\n");
    }

    // ---- T262 P5: a library is a source of the F3 search: one form (Source, Name contains, the source's fields, Search); a source's results open as the list of P1; no source: the search is what it was
    {
        static const LibNodeDecl nodes[] = { { "F3/search", LN_LIST, "Online results", "zl.take", "zl.rows", 1, 0, 0, 0, 0, 0, false, "search=Online;find=zl.find;fields=Group,Handle;more=zl.more" } };
        LibRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.struct_size = sizeof(rec);
        rec.nodes = nodes; rec.nnodes = 1;
        Contributor cs[] = { { "online", CR_LIB, &rec } };
        g_rows.clear(); g_found.clear(); g_find_calls = 0; g_taken = -1;
        {
            Rig plain(0, 0, &f3sk);                                                           // no library: RETURN asks for a name at once, as before
            plain.m->open();
            CHECK(plain.m->goto_spec("F3/search") && plain.m->search_tab());
            CHECK_EQ(plain.m->search_source_count(), 0);
            plain.m->key(KEY_RETURN);
            CHECK(plain.m->entering() && !plain.m->search_form() && !plain.m->picking());
        }
        Rig r(cs, 1, &f3sk);
        CHECK(!r.has_err("search"));
        r.m->open();
        CHECK(r.m->goto_spec("F3/search") && r.m->search_tab());
        CHECK_EQ(r.m->search_source_count(), 1);
        CHECK(r.m->tab_id() != 0 && r.tab("F3", "search").size() <= 1);                // the source takes no row of the tab
        r.m->key(KEY_RETURN);                                                          // T262 D4: with a source, RETURN still asks for a name, as without one
        CHECK(r.m->entering() && !r.m->search_form() && !r.m->picking());
        r.m->key(KEY_ESCAPE);
        CHECK(!r.m->entering());
        r.m->key('/');                                                                 // the form is offered separately
        CHECK(r.m->search_form() && r.m->picking() && !r.m->entering());
        CHECK_EQ(r.m->rows(), 3);                                                      // Source, Name contains, Search
        CHECK_STR(r.left(0).c_str(), "Source"); CHECK_STR(r.right(0).c_str(), "This device");
        CHECK_STR(r.left(1).c_str(), "Name contains"); CHECK_STR(r.left(2).c_str(), "Search");
        r.m->key(KEY_RETURN);                                                          // the next source
        CHECK_STR(r.right(0).c_str(), "Online");
        CHECK_EQ(r.m->rows(), 5);                                                      // + the two fields of the source
        CHECK_STR(r.left(2).c_str(), "Group"); CHECK_STR(r.left(3).c_str(), "Handle"); CHECK_STR(r.left(4).c_str(), "Search");
        r.m->key(KEY_DOWN);
        r.m->key(KEY_RETURN);                                                          // type the name
        CHECK(r.m->entering());
        r.m->key('a'); r.m->key('b');
        r.m->key(KEY_RETURN);
        CHECK(!r.m->entering() && r.m->search_form());
        CHECK_STR(r.right(1).c_str(), "ab");
        CHECK_STR(r.right(2).c_str(), "Any");
        CHECK_STR(r.m->search_form_text(), "ab");                                      // the idle Search tab shows what the form holds
        r.m->key(KEY_DOWN); r.m->key(KEY_DOWN);
        r.m->key(KEY_RETURN);                                                          // the field Handle
        r.m->key('x');
        r.m->key(KEY_RETURN);
        CHECK_STR(r.right(3).c_str(), "x");
        r.m->key(KEY_END);
        CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_REDRAW);                              // Search: the operation got the name, a TAB and each field
        CHECK_EQ(g_find_calls, 1);
        CHECK_STR(g_found.c_str(), "ab\t\tx");
        CHECK(!r.m->search_form() && r.m->picking());                                  // the rows of the source: the list of P1
        CHECK_STR(r.m->title(), "Online results");
        CHECK_EQ(r.m->rows(), 2);
        CHECK_STR(r.left(0).c_str(), "One"); CHECK_STR(r.right(0).c_str(), "x");
        r.m->key(KEY_DOWN);
        r.m->key(KEY_RETURN);
        CHECK_EQ(g_taken, 1);
        r.m->key(KEY_ESCAPE);
        CHECK(!r.m->picking());
        r.m->key('/');                                                                 // the form again: it kept the source and the texts
        CHECK(r.m->search_form());
        CHECK_STR(r.right(0).c_str(), "Online"); CHECK_STR(r.right(1).c_str(), "ab");
        r.m->key(KEY_UP);
        r.m->key(KEY_RETURN);                                                          // the next source after the last: This device
        CHECK_STR(r.right(0).c_str(), "This device");
        CHECK_EQ(r.m->rows(), 3);
        r.m->key(KEY_END);
        r.m->key(KEY_RETURN);                                                          // the search of the media: no call of the library, the query is the Library's
        CHECK_EQ(g_find_calls, 1);
        CHECK(!r.m->search_form() && !r.m->picking());
        CHECK_STR(r.m->search_query(), "ab");
        r.m->key('/');
        r.m->key(KEY_ESCAPE);                                                          // <- leaves the form
        CHECK(!r.m->search_form());
    }

    // ---- T262 P5: a source with `job` searches as a job with its own words; the rows open when it has ended, not when it was cancelled
    {
        static const LibNodeDecl nodes[] = { { "F3/search", LN_LIST, "Slow results", "zl.take", "zl.rows", 1, 0, 0, 0, 0, 0, false, "search=Slow;find=zl.jobfind;job;busy=Asking the server" } };
        LibRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.struct_size = sizeof(rec);
        rec.nodes = nodes; rec.nnodes = 1;
        Contributor cs[] = { { "slow", CR_LIB, &rec } };
        g_rows.clear(); g_jobcalls = 0; g_jobsteps = 3; g_jobpct = 0;
        for (int variant = 0; variant < 2; variant++) {
            Rig r(cs, 1, &f3sk);
            r.m->open();
            r.m->goto_spec("F3/search");
            r.m->key('/');
            r.m->key(KEY_RETURN);                                                      // Slow
            r.m->key(KEY_END);
            g_jobcalls = 0;
            r.m->key(KEY_RETURN);
            CHECK(r.m->busy());
            char l[100], rt[100]; bool a;
            r.m->row(0, l, 100, rt, 100, &a);
            CHECK_STR(l, "Asking the server");                                         // the node's words, no percentage before the operation reports one
            CHECK_STR(rt, "");
            r.m->tick();
            if (variant == 1) {
                r.m->key(KEY_ESCAPE);                                                  // cancelled: no list
            }
            while (r.m->tick()) {}
            CHECK(!r.m->busy());
            CHECK(r.m->picking() == (variant == 0));
            CHECK(g_jobcalls >= 2);
            r.m->key(KEY_ESCAPE);
        }
        g_rows.clear();
    }

    DONE("test_contrib");
}
