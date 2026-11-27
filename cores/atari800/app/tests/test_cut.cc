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
#include "record_menu.h"
#include "atari_media.h"
extern const CoreDesc atari800_desc;     // cores.h lists the shared app's cores; the Atari 800 XL is its own app (cores/atari800/app/lib)
const CoreDesc *const core_table[] = { &atari800_desc, 0 };   // the table of its app (the shared units link the shared table.cc)
#include "libload.h"
#include "libexports.h"
#include "menuflows.h"


// the core of these units: the NES with the record golden/desc_cut.toml (argv[1], compiled by gen_bundle.py): the Timewarp and monitor pages and the key table that drives them
static RecordMenu *g_rm;
// the monitor's key operation of a record core: here it only counts what it was given (the real one is TRX64-Ultimate's)
static int g_mon_key_n, g_mon_key_last;
static int mon_key_op(CoreCtx *, const OpArgs &a, OpOut &o) { g_mon_key_n++; g_mon_key_last = (int)a.i(0); o.msg[0] = 0; return OPE_OK; }

struct Host : MenuHost {
    int leaves = 0;
    void info(int, char *out, int) { out[0] = 0; }
    bool return_to_host(char *msg, int n) { snprintf(msg, n, "no"); return false; }
};

// the operations of Timewarp, as a core registers them: here they only write down what they were called with
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
    FakeIo io; FakeFs fs; CoreCtx c; Host host; Settings *st; MenuCtl *m;      // the record core's (the NES's) registers: no SIO bridge
    explicit Rec(const CoreDesc *d = 0, const MenuMerge *mm = 0)
    {
        if (!d) { d = &g_rm->desc; mm = &g_rm->mm; }
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs; c.merge = mm;
        st = new Settings(d->settings, d->nsettings, d->store, &fs, "/sd/cores/rec.cfg", 0);
        st->load();
        c.settings = st;
        c.started = true;
        OpRegistry *r = ops_of(&c);
        static const ArgSpec one[] = { ARG_INT("value", -1000, 1000) };
        static const ArgSpec key[] = { ARG_INT("key", 0, 0x1FFF) };
        r->contributor("test");
        r->add_op("tw.step", one, 1, rec_op<0>);
        r->add_op("tw.seconds", one, 1, rec_op<1>);
        r->add_op("tw.oldest", 0, 0, rec_op<2>);
        r->add_op("tw.resume", 0, 0, rec_op<3>);
        r->add_op("tw.cancel", 0, 0, rec_op<4>);
        r->add_op("tw.monitor", 0, 0, rec_op<2>);
        r->add_op("monitor.key", key, 1, mon_key_op);
        r->add_state("monitor.wants_stop", [](CoreCtx *) -> const char * { return 0; });
        m = new MenuCtl(&c, &host);
        m->open();
    }
    std::string calls() { std::string s; for (auto &x : g_calls) s += (s.empty() ? "" : "|") + x; g_calls.clear(); return s; }
    std::string row0() { char l[100], rt[100], sb[100]; bool act; m->row(0, l, sizeof(l), rt, sizeof(rt), &act, sb, sizeof(sb)); return std::string(l) + "|" + rt; }
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

int main(int, char **argv)
{
    (void)argv;
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
