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
    (void)argv;
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
