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
#include "fakes.h"
#include "cores.h"
#include "menuctl.h"
#include "menuintent.h"
#include "filemgr.h"
#include "keyboard.h"
#include "ops_glue.h"
#include "volume.h"
#include "record_menu.h"
#include "libload.h"
#include "libexports.h"
#include "menuflows.h"

extern const CoreDesc nes_desc;          // cores.h lists the shared app's cores; the NES is its own app (cores/nes/app/lib)
#include "libslot.h"
// The slot table of this unit's app (libslot.h, base 4.0): the slots of the native builds (tests/coreapp/libtab_native.cc) and a library with a text screen, "textgrid" major 1 (the
// model grid). The base reaches the grid by name through the slot table (LibLoader::find_export), never through an API call (T239.3 D58).
const void *c64cart_table(void);
const void *cbm1541_table(void);
const void *sid_table(void);
const void *tempdisk_table(void);
const void *cbmprg_table(void);
const void *mouse_table(void);
const void *pad_table(void);
const void *padkeys_table(void);
static const void *slot_textgrid(void) { return &g_mg_table; }
const LibSlot g_libslot[] = {
    { "c64cart", "c64cart", 1, c64cart_table, 0 },
    { "cbm1541", "cbm1541", 1, cbm1541_table, 0 },
    { "sid", "sid", 1, sid_table, 0 },
    { "tempdisk", "tempdisk", 1, tempdisk_table, 0 },
    { "cbmprg", "cbmprg", 1, cbmprg_table, 0 },
    { "mouse", "mouse", 1, mouse_table, 0 },
    { "pad", "pad", 1, pad_table, 0 },
    { "padkeys", "padkeys", 1, padkeys_table, 0 },
    { "gridlib", "textgrid", 1, slot_textgrid, 0 },
};
const unsigned g_libslot_n = 9;
const void *g_libslot_bound[9];

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


int main(int, char **argv)
{
    static CoreDesc base = nes_desc;
    base.needs_program = 0;                                     // the menu of a core with no program loaded does not close
    g_rm = new RecordMenu(&base, argv[1]);
    setvbuf(stdout, 0, _IONBF, 0);
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
        CHECK_EQ(r.m->key(0x60), MenuCtl::R_CLOSE);        CHECK_STR(r.calls().c_str(), "tw.cancel");      // the left arrow key of the C64 keyboard is ESC here
        CHECK(open_by_home_row(*r.m, "Timewarp..."));
        CHECK_EQ(r.m->key(KEY_BREAK), MenuCtl::R_CLOSE);   CHECK_STR(r.calls().c_str(), "tw.cancel");
        // the leave intent of the menu hotkey closes at once, whatever the page
        CHECK(open_by_home_row(*r.m, "Timewarp..."));
        MenuIntent lv; lv.id = MI_LEAVE; lv.arg = 0; lv.key = 0; lv.stage = KS_MAIN; lv.row = -1;
        CHECK_EQ(r.m->apply(lv), MenuCtl::R_CLOSE);
    }
    // ---- the terminal screen: every key is the grid's library's, what closes the menu is the menu's (S5, S6) ----------------------------------------------------------------------------------
    {
        Rec r;
        CHECK(open_by_home_row(*r.m, "Monitor..."));
        CHECK(r.m->layout_is("terminal"));
        int n0 = g_mon_key_n;
        CHECK_EQ(r.m->key('x'), MenuCtl::R_REDRAW);
        CHECK_EQ(g_mon_key_n, n0 + 1);
        CHECK_EQ(g_mon_key_last, 'x');
        CHECK_EQ(r.m->key('2'), MenuCtl::R_REDRAW);              // a digit is the monitor's, not a tab number
        CHECK_EQ(g_mon_key_last, '2');
        CHECK(r.m->layout_is("terminal"));
        CHECK_EQ(r.m->key(KEY_UP), MenuCtl::R_REDRAW);
        CHECK_EQ(g_mon_key_last, KEY_UP);
        n0 = g_mon_key_n;
        CHECK_EQ(r.m->key(KEY_F10), MenuCtl::R_CLOSE);           // the menu button and RUN/STOP (while the machine stands) are not the monitor's
        CHECK_EQ(r.m->key(KEY_MENU), MenuCtl::R_CLOSE);
        CHECK_EQ(r.m->key(KEY_BREAK), MenuCtl::R_CLOSE);
        CHECK_EQ(g_mon_key_n, n0);
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
    // ---- the base's chain to a library's text screen: the export "textgrid" of the app's slot table, the menu's finder is LibLoader::find_export, and the calls reach the table
    {
        Rec r;
        g_mg.reset();
        menu_set_export_finder(LibLoader::find_export);
        const TextGrid *g = menu_grid("textgrid");
        CHECK(g != 0 && g->size == sizeof(TextGrid));
        CHECK(menu_grid("nothing") == 0);
        if (g) {
            g->render();
            CHECK_EQ(g_mg.n_render, 1);
            CHECK_EQ(g->sig(), g_mg.sig);
            CHECK_EQ(g->cols(), 80);
            CHECK_EQ(g->rows(), 0);
            CHECK_STR(g->line(0), "");                                  // no rows: "" for a row outside the grid
            CHECK_EQ(g->attr(3), TG_NORMAL);
        }
        CHECK(menu_grid_enter("textgrid"));
        CHECK_EQ(g_mg.n_enter, 1);
        g_mg.tick_answer = true;
        CHECK(menu_grid_tick("textgrid"));
        CHECK(!menu_grid_tick("textgrid"));
        CHECK_EQ(g_mg.n_tick, 2);
        CHECK(!menu_grid_enter("nothing") && !menu_grid_tick("nothing"));
        menu_set_export_finder(0);
    }
    // ---- base 3.3: the key table is the core's record (descrec.h section 18), no longer menukeytab.cc ---------------------------------------------------------------------------
    {
        // the rows that menukeytab.cc held as KS_MONITOR and KS_TIMELINE, as they stood, against what the record of menu.toml reads back (every field of every row, in the order)
        struct Ref { const char *layout; int lo, hi, intent, arg; const char *op; unsigned flags; const char *when; };
        static const Ref ref[] = {
            { "terminal", KEY_F10, KEY_F10, MI_DECLINE, 0, 0, 0, 0 },
            { "terminal", KEY_SCRLOCK, KEY_SCRLOCK, MI_DECLINE, 0, 0, 0, 0 },
            { "terminal", KEY_MENU, KEY_MENU, MI_DECLINE, 0, 0, 0, 0 },
            { "terminal", KEY_BREAK, KEY_BREAK, MI_DECLINE, 0, 0, 0, "!monitor.wants_stop" },
            { "terminal", -1, -1, MI_OP, 0, "monitor.key", KF_KEYARG | KF_DIRECTIVE | KF_DIRECT, 0 },
            { "timeline", KEY_LEFT, KEY_LEFT, MI_OP, -1, "tw.step", KF_ARG | KF_STATUS, 0 },
            { "timeline", KEY_RIGHT, KEY_RIGHT, MI_OP, 1, "tw.step", KF_ARG | KF_STATUS, 0 },
            { "timeline", KEY_UP, KEY_UP, MI_OP, -1, "tw.seconds", KF_ARG | KF_STATUS, 0 },
            { "timeline", KEY_DOWN, KEY_DOWN, MI_OP, 1, "tw.seconds", KF_ARG | KF_STATUS, 0 },
            { "timeline", KEY_PAGEUP, KEY_PAGEUP, MI_OP, -10, "tw.seconds", KF_ARG | KF_STATUS, 0 },
            { "timeline", KEY_F1, KEY_F1, MI_OP, -10, "tw.seconds", KF_ARG | KF_STATUS, 0 },
            { "timeline", KEY_PAGEDOWN, KEY_PAGEDOWN, MI_OP, 10, "tw.seconds", KF_ARG | KF_STATUS, 0 },
            { "timeline", KEY_F7, KEY_F7, MI_OP, 10, "tw.seconds", KF_ARG | KF_STATUS, 0 },
            { "timeline", KEY_HOME, KEY_HOME, MI_OP, 0, "tw.oldest", KF_STATUS, 0 },
            { "timeline", KEY_CTRL_O, KEY_CTRL_O, MI_OP, 0, "tw.monitor", KF_DIRECTIVE, 0 },
            { "timeline", KEY_BREAK, KEY_BREAK, MI_OP, 0, "tw.cancel", KF_CLOSE, 0 },
            { "timeline", KEY_ESCAPE, KEY_ESCAPE, MI_OP, 0, "tw.cancel", KF_CLOSE, 0 },
            { "timeline", KEY_RETURN, KEY_RETURN, MI_OP, 0, "tw.resume", KF_STATUS_OK | KF_CLOSE, 0 },
            { "timeline", 0x20, 0x7E, MI_SWALLOW, 0, 0, 0, 0 },
        };
        const DescRecord &rec = g_rm->rec;
        CHECK_EQ(rec.nkeys, (int)(sizeof(ref) / sizeof(ref[0])));
        for (int i = 0; i < rec.nkeys && i < (int)(sizeof(ref) / sizeof(ref[0])); i++) {
            const CoreKey &k = rec.keys[i];
            CHECK_STR(k.layout, ref[i].layout);
            CHECK(k.node == 0);
            CHECK_EQ(k.row.stage, KS_CORE);
            CHECK_EQ(k.row.lo, ref[i].lo);
            CHECK_EQ(k.row.hi, ref[i].hi);
            CHECK_EQ(k.row.intent, ref[i].intent);
            CHECK_EQ(k.row.arg, ref[i].arg);
            CHECK_STR(k.row.op, ref[i].op);
            CHECK_EQ(k.row.flags, ref[i].flags);
            CHECK_STR(k.row.when, ref[i].when);
        }
        // menukeytab.cc has none of them any more: the table of the image holds no row of the stage KS_CORE
        int n;
        const KeyRow *rows = menu_keymap(&n);
        for (int i = 0; i < n; i++) {
            CHECK(rows[i].stage != KS_CORE);
        }
        // the intent a key resolves to on the Timewarp page is the record's row (stage KS_CORE, a number after the table's own), and applying it runs the operation
        Rec r;
        CHECK(open_by_home_row(*r.m, "Timewarp..."));
        g_calls.clear();
        MenuIntent in = r.m->resolve(KEY_PAGEUP);
        CHECK_EQ(in.id, MI_OP);
        CHECK_EQ(in.arg, -10);
        CHECK_EQ(in.stage, KS_CORE);
        CHECK(in.row >= n && in.row < n + rec.nkeys);
        CHECK_EQ(r.m->apply(in), MenuCtl::R_REDRAW);
        CHECK_STR(r.calls().c_str(), "tw.seconds -10");
    }
    {
        // rows of another record: a row for one tab by its id (node), the first row that matches takes the key, a state condition, a layout row on a tab of another layout does nothing,
        // and a core with no key table has the keys of its screen go to the menu's grammar
        static MenuMerge *mm = new MenuMerge(g_rm->mm);
        static CoreKey rows[5];
        memset(rows, 0, sizeof(rows));
        auto row = [](CoreKey &k, const char *layout, const char *node, int key, int arg, const char *op, unsigned flags, const char *when) {
            k.layout = layout; k.node = node;
            k.row.stage = KS_CORE; k.row.lo = k.row.hi = (int16_t)key; k.row.intent = MI_OP; k.row.arg = arg; k.row.op = op; k.row.flags = (uint16_t)flags; k.row.when = when;
        };
        row(rows[0], 0, "timewarp", 'x', 7, "tw.step", KF_ARG, 0);                    // the Timewarp page by its id
        row(rows[1], "timeline", 0, 'x', 2, "tw.seconds", KF_ARG, 0);                 // never reached for x: the row before takes it
        row(rows[2], "timeline", 0, 'g', 0, "tw.oldest", 0, "!no.such.state");          // counts while that state has none
        row(rows[3], "timeline", 0, 'h', 0, "tw.oldest", 0, "no.such.state");          // counts while it has a text: never here
        row(rows[4], 0, "monitor", 'x', 0, "tw.cancel", 0, 0);                          // the Monitor page, not the Timewarp page
        mm->keys = rows;
        mm->nkeys = 5;
        Rec r;
        r.c.merge = mm;
        CHECK(open_by_home_row(*r.m, "Timewarp..."));
        g_calls.clear();
        CHECK_EQ(r.m->key('x'), MenuCtl::R_REDRAW);  CHECK_STR(r.calls().c_str(), "tw.step 7");
        CHECK_EQ(r.m->key('g'), MenuCtl::R_REDRAW);  CHECK_STR(r.calls().c_str(), "tw.oldest");
        r.m->key('h');                                CHECK_STR(r.calls().c_str(), "");
        r.m->key(KEY_LEFT);                           CHECK_STR(r.calls().c_str(), "");           // no row for it: the Timewarp keys are the record's, the table of the image has none
        CHECK(open_by_home_row(*r.m, "Monitor..."));
        r.m->key('x');                                CHECK_STR(r.calls().c_str(), "tw.cancel");  // the rows of the Timewarp page do not count here, the row of the Monitor page does
        mm->nkeys = 0;                                                                         // no key table at all
        int n0 = g_mon_key_n;
        r.m->key('x');
        CHECK_EQ(g_mon_key_n, n0);                                                          // the monitor takes no key: nothing says it should
        // a remote intent encoded and decoded (uiapp.cc "@id.arg.key.stage.row") applies the same row
        mm->nkeys = 5;
        CHECK(open_by_home_row(*r.m, "Timewarp..."));
        MenuIntent in = r.m->resolve('x');
        MenuIntent again = in;
        CHECK_EQ(again.stage, KS_CORE);
        CHECK_EQ(r.m->apply(again), MenuCtl::R_REDRAW);
        CHECK_STR(r.calls().c_str(), "tw.step 7");
    }
    // ---- base 3.3: a task with job = true runs as the stepped operation, with the progress line and a cancel ------------------------------------------------------------------
    {
        static CoreDesc jd = g_rm->desc;
        static Task tasks[DR_TASKS + 2];
        memcpy(tasks, jd.tasks, sizeof(Task) * (size_t)jd.ntasks);
        memset(&tasks[jd.ntasks], 0, sizeof(Task));
        tasks[jd.ntasks].label = "Dump all";
        tasks[jd.ntasks].op = "test.job";
        tasks[jd.ntasks].group = "top";
        tasks[jd.ntasks].job = true;                                        // what `job = true` of a [[task]] reads back as
        jd.tasks = tasks;
        jd.ntasks++;
        struct J {
            static int &steps() { static int n; return n; }
            static int &step_flag() { static int n; return n; }
            static int &mode() { static int n; return n; }               // 0 runs to the end, 2 fails at the third step
            static int op(CoreCtx *c, const OpArgs &, OpOut &o)
            {
                steps()++;
                step_flag() += c->job_step ? 1 : 0;
                if (c->job_cancel) {
                    c->job_cancel = false;
                    snprintf(o.msg, o.msglen, "Cancelled: nothing was kept.");
                    return OPE_OK;
                }
                c->job_pct += 25;
                if (mode() == 2 && steps() == 3) {
                    snprintf(o.msg, o.msglen, "The ring is gone");
                    return OPE_FAILED;
                }
                o.more = c->job_pct < 100;
                o.msg[0] = 0;
                return OPE_OK;
            }
        };
        Rec r(&jd, &g_rm->mm);
        OpRegistry *reg = ops_of(&r.c);
        reg->contributor("test");
        reg->add_op("test.job", 0, 0, J::op);
        // RETURN on the row starts the job: nothing is called yet (the menu's tick calls the operation, under the core lock, one step at a time) and the progress screen is up
        CHECK(open_by_home_row(*r.m, "Dump all"));
        CHECK(r.m->busy());
        CHECK_EQ(J::steps(), 0);
        CHECK_EQ(r.m->progress(), 0);
        CHECK_STR(r.row0().c_str(), "Dump all|0%");
        CHECK(strstr(r.m->status(), "Dump all") != 0 && strstr(r.m->status(), "cancels") != 0);
        CHECK_EQ(r.m->key('a'), MenuCtl::R_NONE);                           // keys wait while it works
        CHECK(r.m->tick());
        CHECK_EQ(J::steps(), 1);
        CHECK(r.m->busy());
        CHECK_EQ(r.m->progress(), 25);
        CHECK_STR(r.m->status(), "Dump all: 25%  (<- cancels)");
        CHECK_STR(r.row0().c_str(), "Dump all|25%");
        r.m->tick();
        r.m->tick();
        CHECK_EQ(r.m->progress(), 75);
        CHECK(r.m->busy());
        r.m->tick();                                                        // the step that answers no `more`
        CHECK_EQ(J::steps(), 4);
        CHECK_EQ(J::step_flag(), 4);                                        // every call ran as a step (CoreCtx::job_step)
        CHECK(!r.m->busy());
        CHECK_STR(r.m->status(), "Dump all: done");
        CHECK(!r.m->status_bad());
        // cancelled in the middle: the next step sees the cancel and ends the job
        J::steps() = 0;
        r.c.job_pct = 0;
        CHECK(open_by_home_row(*r.m, "Dump all"));
        r.m->tick();
        r.m->tick();
        CHECK(r.m->busy());
        CHECK_EQ(r.m->key(KEY_ESCAPE), MenuCtl::R_REDRAW);
        CHECK(r.m->busy());                                                 // dropped by the next tick, which is where the lock is taken
        r.m->tick();
        CHECK(!r.m->busy());
        CHECK_EQ(J::steps(), 3);
        CHECK_STR(r.m->status(), "Cancelled: nothing was kept.");
        // cancelled before the first step: the operation is never called
        J::steps() = 0;
        CHECK(open_by_home_row(*r.m, "Dump all"));
        CHECK_EQ(r.m->key(KEY_ESCAPE), MenuCtl::R_REDRAW);
        r.m->tick();
        CHECK(!r.m->busy());
        CHECK_EQ(J::steps(), 0);
        CHECK_STR(r.m->status(), "Cancelled: Dump all");                    // the task, not a file: this job writes none
        // a step that fails ends the job with its sentence, as a failure
        J::steps() = 0;
        J::mode() = 2;
        CHECK(open_by_home_row(*r.m, "Dump all"));
        r.m->tick();
        r.m->tick();
        r.m->tick();
        CHECK(!r.m->busy());
        CHECK_STR(r.m->status(), "The ring is gone");
        CHECK(r.m->status_bad());
    }
    DONE("test_cut");
}
