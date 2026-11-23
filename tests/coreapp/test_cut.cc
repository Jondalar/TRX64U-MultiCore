// T192.3 (the parity cut, D23): what the controller no longer holds is still done, by the table and the modules it points at.
//   S5/S6  the keys of the TRX64U's record menu: Timewarp's keys are the tw.* operations (with the step the table gives), the monitor's keys are the operation monitor.key (every
//          key but the ones that close the menu), RUN/STOP closes the monitor while the machine stands, the menu hotkey's leave intent closes at once
//   S4     the file manager module on its own: a folder, a sub folder and back, marks, the search, the viewer, the clipboard; and in the menu: a single DEL deletes nothing
//   S8     a row that vanished from the Library's folder is replaced by its neighbour, not by the top row
//   S15    the key table's stages are the same on every core: every row of menukeytab.cc names an intent the executor knows
#include <string>
#include <vector>
#include "check.h"
#include "model_grid.h"
#include "sio_model.h"
#include "fakes.h"
#include "cores.h"
#include "menuctl.h"
#include "menuintent.h"
#include "filemgr.h"
#include "keyboard.h"
#include "ops_glue.h"
#include "volume.h"
#include "trx64u_menu.h"
#include "atari_media.h"
#include "libload.h"
#include "libexports.h"
#include "menuflows.h"

extern int stub_mon_key_n, stub_mon_key_last;             // tests/coreapp/board_stubs.cc: what mon_key was given
extern int stub_mon_open_n, stub_mon_tick_n, stub_mon_render_n;     // what monitor_grid.cc asked of the monitor
extern bool stub_mon_tick_answer;
extern uint32_t stub_mon_sig;

// the loader of the application, as libboard.cc makes it, with no images: the table of the monitor's grid goes in through add_export and the menu finds it through LibLoader::find_export
struct GridHost : LibHost {
    void *alloc(uint32_t n) override { return malloc(n); }
    void release(void *p) override { free(p); }
    uint32_t addr_of(const void *p) override { return (uint32_t)(uintptr_t)p; }
    void sync_code(void *, uint32_t) override { }
    bool has_capability(const char *) override { return false; }
    const char *core_name() override { return "Test Core"; }
    int call_register(const Entry &, const LibApi *, LibRecord *) override { return 99; }
};

struct Host : MenuHost {
    int leaves = 0;
    void info(int, char *out, int) { out[0] = 0; }
    bool return_to_host(char *msg, int n) { snprintf(msg, n, "no"); return false; }
};

// the operations of Timewarp, as the image registers them (ringops.cc): here they only write down what they were called with
static std::vector<std::string> g_calls;
template <int N> static int rec_op(CoreCtx *, const OpArgs &a, OpOut &o)
{
    static const char *const names[] = { "tw.step", "tw.seconds", "tw.oldest", "tw.resume", "tw.cancel" };
    char b[64];
    if (a.n > 0) snprintf(b, sizeof(b), "%s %d", names[N], (int)a.i(0)); else snprintf(b, sizeof(b), "%s", names[N]);
    g_calls.push_back(b);
    snprintf(o.msg, o.msglen, "%s", N == 3 ? "Timewarp: running from here" : "");
    return OPE_OK;
}

struct Rec {
    SioModel io; FakeFs fs; CoreCtx c; Host host; Settings *st; MenuCtl *m;
    Rec()
    {
        const CoreDesc *d = trx64u::desc();
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs; c.merge = trx64u::merge();
        st = new Settings(d->settings, d->nsettings, d->store, &fs, "/sd/cores/trx64u.cfg", 0);
        st->load();
        c.settings = st;
        c.started = true;
        OpRegistry *r = ops_of(&c);
        // the names are registered already (the image's export, trx64u_ops.cc registers them with no behaviour here): their handlers are replaced by ones that write the call down
        struct { const char *name; op_fn fn; } rec[] = { { "tw.step", rec_op<0> }, { "tw.seconds", rec_op<1> }, { "tw.oldest", rec_op<2> }, { "tw.resume", rec_op<3> }, { "tw.cancel", rec_op<4> } };
        for (auto &x : rec) {
            const OpDef *o = r->find_op(x.name);
            if (!o) { printf("FAIL no operation %s in the registry\n", x.name); g_fails++; continue; }
            const_cast<OpDef *>(o)->fn = x.fn;
        }
        m = new MenuCtl(&c, &host);
        m->open();
    }
    std::string calls() { std::string s; for (auto &x : g_calls) s += (s.empty() ? "" : "|") + x; g_calls.clear(); return s; }
};

// T228 R1: no F2 and no F4 area - Timewarp and Monitor are pages of Home, opened by the Home rows "Timewarp..." and "Monitor..." (menu.screen F1/timewarp,
// F1/monitor). The menu opens, F1, the cursor on the row, RETURN: false when there is no such row or the cursor does not land on it.
static bool open_by_home_row(MenuCtl &m, const char *label)
{
    m.open();
    m.key(KEY_F1);
    for (int i = 0; i < m.rows(); i++) {
        char l[96], v[96];
        bool a = false;
        m.row(i, l, sizeof(l), v, sizeof(v), &a);
        if (!strcmp(l, label)) {
            if (!m.select_index(i)) { return false; }
            m.key(KEY_RETURN);
            return true;
        }
    }
    return false;
}

struct Lib {
    SioModel io; FakeFs fs; PxRec px; Host host; CoreCtx c; Settings *st; Video *vid; MenuCtl *m;
    Lib()
    {
        static CoreDesc gd;
        static Setting gs[SETTINGS_MAX];
        volume_extend(&atari800_desc, &gd, gs);
        const CoreDesc *d = &gd;
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs;
        st = new Settings(d->settings, d->nsettings, d->store, &fs, "/usb0/cores/atari800.cfg", 0);
        c.settings = st;
        vid = new Video(d->video, &px, &io);
        c.video = vid;
        m = new MenuCtl(&c, &host);
        st->load();
        fs.put("/usb0/games/a.atr", std::vector<uint8_t>(32, 1));
        fs.put("/usb0/games/b.atr", std::vector<uint8_t>(32, 2));
        fs.put("/usb0/games/c.atr", std::vector<uint8_t>(32, 3));
        fs.put("/usb0/games/d.atr", std::vector<uint8_t>(32, 4));
    }
    std::string left(int i) { char l[300], r[100]; bool a; m->row(i, l, 300, r, 100, &a); return l; }
    int find_row(const char *s) { for (int i = 0; i < m->rows(); i++) if (left(i) == s) return i; return -1; }
    bool go_to(const char *s) { int r = find_row(s); if (r < 0) { printf("FAIL no row %s\n", s); g_fails++; return false; } while (m->selected() > r) m->key(KEY_UP); while (m->selected() < r) m->key(KEY_DOWN); return true; }
    void enter(const char *s) { if (go_to(s)) m->key(KEY_RETURN); }
    void to_games() { m->key(KEY_F3); for (int i = 0; i < 12 && strcmp(m->title(), "/"); i++) m->key(KEY_LEFT); enter("usb0/"); enter("games/"); }
    void run_job() { int n = 0; while (m->tick() && ++n < 400) { } }
};

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);
    // ---- T228 DC5: the machine line names the REU only while it is on -------------------------------------------------------------------------------------------------
    {
        Rec r;
        Settings *st = r.c.settings;                            // the menu may have replaced the object the rig made: the one the context holds is the one the line reads
        int reu = st->find("RAM Expansion Unit"), sz = st->find("REU Size");
        CHECK(reu >= 0 && sz >= 0);
        // the build's registry has an empty stand-in for reu.on (board_stubs.cc); the state the board has: set while the expansion is not Disabled
        struct S { static const char *reu_on(CoreCtx *c) { return c->settings->value(c->settings->find("RAM Expansion Unit")) ? "yes" : 0; } };
        const_cast<StateDef *>(ops_of(&r.c)->find_state("reu.on"))->get = S::reu_on;
        char off[160], on[160];
        st->set(reu, 0);
        r.m->machine_line(off, sizeof(off));
        st->set(reu, 1);
        r.m->machine_line(on, sizeof(on));
        const char *size = trx64u::desc()->settings[sz].values[st->value(sz)];
        CHECK(strstr(on, size) != 0);
        CHECK(strstr(off, size) == 0);
    }
    // ---- Timewarp: the keys are operations (S5, S6) ------------------------------------------------------------------------------------------------------------------
    {
        Rec r;
        CHECK(open_by_home_row(*r.m, "Timewarp..."));
        CHECK(r.m->layout_is("timeline"));
        g_calls.clear();
        CHECK_EQ(r.m->key(KEY_LEFT), MenuCtl::R_REDRAW);  CHECK_STR(r.calls().c_str(), "tw.step -1");
        CHECK_EQ(r.m->key(KEY_RIGHT), MenuCtl::R_REDRAW); CHECK_STR(r.calls().c_str(), "tw.step 1");
        r.m->key(KEY_UP);                                  CHECK_STR(r.calls().c_str(), "tw.seconds -1");
        r.m->key(KEY_DOWN);                                CHECK_STR(r.calls().c_str(), "tw.seconds 1");
        r.m->key(KEY_PAGEUP);                              CHECK_STR(r.calls().c_str(), "tw.seconds -10");
        r.m->key(KEY_F1);                                  CHECK_STR(r.calls().c_str(), "tw.seconds -10");
        r.m->key(KEY_PAGEDOWN);                            CHECK_STR(r.calls().c_str(), "tw.seconds 10");
        r.m->key(KEY_F7);                                  CHECK_STR(r.calls().c_str(), "tw.seconds 10");
        r.m->key(KEY_HOME);                                CHECK_STR(r.calls().c_str(), "tw.oldest");
        CHECK_EQ(r.m->key('a'), MenuCtl::R_NONE);          CHECK_STR(r.calls().c_str(), "");             // a letter is nobody's over the bar
        CHECK_EQ(r.m->key('3'), MenuCtl::R_NONE);          CHECK_STR(r.calls().c_str(), "");
        CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_CLOSE);  CHECK_STR(r.calls().c_str(), "tw.resume");
        CHECK_STR(r.m->status(), "Timewarp: running from here");
        CHECK(open_by_home_row(*r.m, "Timewarp..."));
        CHECK_EQ(r.m->key(KEY_ESCAPE), MenuCtl::R_CLOSE);  CHECK_STR(r.calls().c_str(), "tw.cancel");
        CHECK(open_by_home_row(*r.m, "Timewarp..."));
        CHECK_EQ(r.m->key(0x60), MenuCtl::R_CLOSE);        CHECK_STR(r.calls().c_str(), "tw.cancel");      // the C64's left arrow is ESC here
        CHECK(open_by_home_row(*r.m, "Timewarp..."));
        CHECK_EQ(r.m->key(KEY_BREAK), MenuCtl::R_CLOSE);   CHECK_STR(r.calls().c_str(), "tw.cancel");
        // the leave intent of the menu hotkey closes at once, whatever the page
        CHECK(open_by_home_row(*r.m, "Timewarp..."));
        MenuIntent lv; lv.id = MI_LEAVE; lv.arg = 0; lv.key = 0; lv.stage = KS_MAIN; lv.row = -1;
        CHECK_EQ(r.m->apply(lv), MenuCtl::R_CLOSE);
    }
    // ---- the monitor: every key is trxmon's, what closes the menu is the menu's (S5, S6) ----------------------------------------------------------------------------------
    {
        Rec r;
        CHECK(open_by_home_row(*r.m, "Monitor..."));
        CHECK(r.m->layout_is("terminal"));
        int n0 = stub_mon_key_n;
        CHECK_EQ(r.m->key('x'), MenuCtl::R_REDRAW);
        CHECK_EQ(stub_mon_key_n, n0 + 1);
        CHECK_EQ(stub_mon_key_last, 'x');
        CHECK_EQ(r.m->key('2'), MenuCtl::R_REDRAW);              // a digit is the monitor's, not a tab number
        CHECK_EQ(stub_mon_key_last, '2');
        CHECK(r.m->layout_is("terminal"));
        CHECK_EQ(r.m->key(KEY_UP), MenuCtl::R_REDRAW);
        CHECK_EQ(stub_mon_key_last, KEY_UP);
        n0 = stub_mon_key_n;
        CHECK_EQ(r.m->key(KEY_F10), MenuCtl::R_CLOSE);           // the menu button and RUN/STOP (while the machine stands) are not the monitor's
        CHECK_EQ(r.m->key(KEY_MENU), MenuCtl::R_CLOSE);
        CHECK_EQ(r.m->key(KEY_BREAK), MenuCtl::R_CLOSE);
        CHECK_EQ(stub_mon_key_n, n0);
    }
    // ---- the monitor's screen is a text grid of a library (T239.3 base 3.3): the terminal node of the record names the export "textgrid"; the page's entry calls its enter once, every pass of
    // the menu loop its tick (the answer says the grid changed), and nothing names an operation of the monitor
    {
        mg_install();
        Rec r;
        CHECK_EQ(g_mg.n_enter, 0);
        CHECK(open_by_home_row(*r.m, "Monitor..."));
        CHECK(r.m->layout_is("terminal"));
        char gname[40];
        CHECK(r.m->layout_param("grid", gname, sizeof(gname)));
        CHECK_STR(gname, "textgrid");
        CHECK_EQ(g_mg.n_enter, 1);
        CHECK_EQ(g_mg.n_tick, 0);
        g_mg.tick_answer = true;
        CHECK(r.m->pump(true));
        CHECK(!r.m->pump(true));
        CHECK_EQ(g_mg.n_tick, 2);
        CHECK_EQ(g_mg.n_enter, 1);
        CHECK_EQ(r.m->key(KEY_F10), MenuCtl::R_CLOSE);
        mg_uninstall();
    }
    // ---- the board's own wiring of that screen (libboard.cc): monitor_grid.cc over mon_*, the table registered with the loader as the base does where the core has the monitor, and the
    // menu's finder is LibLoader::find_export; the drawing calls reach mon_*, enter is the operation monitor.open, tick is mon_tick
    {
        Rec r;
        GridHost gh;
        LibLoader ld(&gh, ops_of(&r.c), 3);
        CHECK(ops_of(&r.c)->find_op("monitor.open") != 0);
        CHECK(ld.add_export("textgrid", 1, monitor_textgrid(&r.c)));
        menu_set_export_finder(LibLoader::find_export);
        const TextGrid *g = menu_grid("textgrid");
        CHECK(g != 0 && g->size == sizeof(TextGrid));
        CHECK(menu_grid("nothing") == 0);
        stub_mon_open_n = stub_mon_tick_n = stub_mon_render_n = 0;
        stub_mon_sig = 0xC0FFEE;
        if (g) {
            g->render();
            CHECK_EQ(stub_mon_render_n, 1);
            CHECK_EQ(g->sig(), 0xC0FFEE);
            CHECK_EQ(g->cols(), 80);
            CHECK_EQ(g->rows(), 0);
            CHECK_STR(g->line(0), "");                                  // no rows: "" for a row outside the grid
            CHECK_EQ(g->attr(3), TG_NORMAL);
        }
        CHECK(menu_grid_enter("textgrid"));
        CHECK_EQ(stub_mon_open_n, 1);                                   // through the operation monitor.open
        stub_mon_tick_answer = true;
        CHECK(menu_grid_tick("textgrid"));
        CHECK(!menu_grid_tick("textgrid"));
        CHECK_EQ(stub_mon_tick_n, 2);
        CHECK(!menu_grid_enter("nothing") && !menu_grid_tick("nothing"));
        menu_set_export_finder(0);
    }
    // ---- the file manager module (S4) --------------------------------------------------------------------------------------------------------------------------------
    {
        Lib l;
        struct Said { std::string text; bool bad = false; } said;
        FileMgr::Port port = { &said, [](void *u, const char *t, bool b) { ((Said *)u)->text = t; ((Said *)u)->bad = b; }, [](void *) { return true; } };
        FileMgr fm(&l.c, port);
        snprintf(fm.path, sizeof(fm.path), "/usb0/games");
        fm.load_dir();
        CHECK_EQ(fm.nlist, 5);                                    // "..", a, b, c, d
        CHECK_STR(fm.list[0].name, "..");
        CHECK_STR(fm.list[1].name, "a.atr");
        fm.enter_dir("..");
        CHECK_STR(fm.path, "/usb0");
        fm.enter_dir("games");
        CHECK_STR(fm.path, "/usb0/games");
        fm.parent_dir();
        CHECK_STR(fm.path, "/usb0");
        CHECK_EQ(fm.sel, 0);
        // the marks stay with the names and are given to the operations (CoreCtx::lib)
        fm.enter_dir("games");
        fm.mark_all(true);
        CHECK(!fm.marked[0] && fm.marked[1] && fm.marked[4]);
        CHECK_EQ(l.c.lib.nmarked, 4);
        fm.mark_all(false);
        CHECK_EQ(l.c.lib.nmarked, 0);
        // the search: a part of the name, ? is any character; the rows are the files, the full path is the file's
        snprintf(fm.search_q, sizeof(fm.search_q), "?.at");
        fm.run_search();
        CHECK_EQ(fm.vmode, 1);
        CHECK_EQ(fm.nlist, 4);
        CHECK_STR(fm.vfull[0], "/usb0/games/a.atr");
        CHECK(said.text.find("4 found") == 0);
        char full[FS_PATH_MAX];
        CHECK_STR(fm.full_path(2, full, sizeof(full)), "/usb0/games/c.atr");
        fm.parent_dir();
        CHECK_EQ(fm.vmode, 0);
        CHECK_STR(fm.path, "/");
        // the quick seek lands the cursor on the first name that begins with what was typed
        snprintf(fm.path, sizeof(fm.path), "/usb0/games");
        fm.load_dir();
        l.io.t += 5000;
        fm.seek_key('c');
        CHECK_STR(fm.list[fm.sel].name, "c.atr");
        // the viewer: text lines, hex rows, the cursor clamps
        l.fs.put("/usb0/games/t.txt", std::vector<uint8_t>{ 'a', 'b', '\n', 'c', '\n' });
        fm.start_view("/usb0/games/t.txt", false);
        CHECK(fm.view && !fm.view_hex);
        char ln[200];
        fm.view_line(0, ln, sizeof(ln)); CHECK_STR(ln, "ab");
        fm.view_line(1, ln, sizeof(ln)); CHECK_STR(ln, "c");
        fm.view_scroll(VS_DOWN);
        fm.view_scroll(VS_END);
        fm.view_scroll(VS_UP);
        fm.view_scroll(VS_HOME);
        CHECK_EQ(fm.view_top, 0u);
        fm.start_view("/usb0/games/t.txt", true);
        fm.view_line(0, ln, sizeof(ln));
        CHECK(std::string(ln).find("00000000  61 62 0A 63 0A") == 0);
        // the clipboard through the operation the REST clients call
        snprintf(fm.path, sizeof(fm.path), "/usb0/games");
        fm.load_dir();
        fm.sel = 2;
        fm.lib_sync();
        fm.clip(false, 0, false);
        CHECK_EQ(l.c.lib.nclip, 1);
        CHECK_STR(l.c.lib.clip[0], "/usb0/games/b.atr");
    }
    // a single DEL deletes nothing: the question is the only way to a delete (S4)
    {
        Lib l;
        l.to_games();
        l.enter("b.atr");                                         // the sheet of the file
        CHECK(l.m->chooser());
        l.enter("Delete");
        char t[200];
        CHECK_STR(l.m->dialog(t, sizeof(t)), "confirm");
        CHECK_EQ(l.m->key(KEY_BACK), MenuCtl::R_NONE);            // DEL
        CHECK_EQ(l.m->key(KEY_BACK), MenuCtl::R_NONE);
        CHECK_STR(l.m->dialog(t, sizeof(t)), "confirm");
        l.run_job();
        CHECK(l.fs.files.count("/usb0/games/b.atr") == 1);
        l.m->key(KEY_RETURN);                                     // only RETURN says yes
        l.run_job();
        CHECK(l.fs.files.count("/usb0/games/b.atr") == 0);
    }
    // ---- S8: the row that vanished is replaced by its neighbour, not by the top row -------------------------------------------------------------------------------------------
    {
        Lib l;
        l.to_games();
        l.go_to("c.atr");
        int at = l.m->selected();
        CHECK(at > 1);
        l.fs.files.erase("/usb0/games/c.atr");
        l.io.t += 5000;
        CHECK(l.m->relist());
        CHECK(l.left(l.m->selected()) == "d.atr");                // the neighbour that slid into its place
        CHECK_EQ(l.m->selected(), at);
        // the last row vanishes: the cursor goes to the new last row
        l.go_to("d.atr");
        l.fs.files.erase("/usb0/games/d.atr");
        l.io.t += 5000;
        l.m->relist();
        CHECK(l.left(l.m->selected()) == "b.atr");
        // the row under the cursor stays under it when one above it comes and goes
        l.fs.put("/usb0/games/0.atr", std::vector<uint8_t>(32, 9));
        l.io.t += 5000;
        l.m->relist();
        CHECK(l.left(l.m->selected()) == "b.atr");
    }
    // ---- the table: every row names an intent the executor knows (S6, S15) -------------------------------------------------------------------------------------------
    {
        int n;
        const KeyRow *rows = menu_keymap(&n);
        CHECK(n > 80);
        for (int i = 0; i < n; i++) {
            CHECK(rows[i].intent > MI_NONE && rows[i].intent < MI_COUNT);
            CHECK(rows[i].stage < KS_COUNT);
            CHECK(menu_intent_name(rows[i].intent)[0] != 0);
            CHECK(rows[i].intent != MI_OP || (rows[i].op && rows[i].op[0]));
        }
        for (int id = 1; id < MI_COUNT; id++) {
            CHECK(menu_intent_name(id)[0] != 0);
            CHECK_EQ(menu_intent_by_name(menu_intent_name(id)), id);
        }
        CHECK_EQ(menu_intent_by_name("no such intent"), -1);
    }
    DONE("test_cut");
}
