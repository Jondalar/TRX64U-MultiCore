// T104.2 D6: the address prompt of "Load PRG and start from". A file action with `ask` shows one row - the prompt, the address in hex, a cursor -
// takes hex digits, BACK, RETURN and ESC, starts on the word the file itself holds, and hands the number to the operation through the
// binding. While it is up a digit is a digit, never a tab. The core is the Atari descriptor with two file types of a test's own (.prg: Run and
// Load-and-start-from; .p00: the same with the 0x1A byte header before the load address) whose operations record what they were given
// (T239.3 P1-8: this was made on the trx64u descriptor and its machine, which are TRX64-Ultimate's now; the prompt is the base's).
#include <string>
#include "check.h"
#include "sio_model.h"
#include "menuctl.h"
#include "cores.h"
#include "keyboard.h"
extern const CoreDesc atari800_desc;          // cores.h lists the shared app's cores; the Atari 800 XL is its own app (cores/atari800/app/lib)

const CoreDesc *const core_table[] = { &atari800_desc, 0 };

static int g_runs = 0, g_loads = 0;
static std::string g_path;
static int g_start = -1;
static int op_run(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    uint64_t sz; bool dir;
    if (!c->fs->stat(a.s(0), &sz, &dir)) { snprintf(o.msg, o.msglen, "no such file"); return OPE_FAILED; }
    g_runs++; g_path = a.s(0);
    return OPE_OK;
}
static int op_load(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    uint64_t sz; bool dir;
    if (!c->fs->stat(a.s(0), &sz, &dir)) { snprintf(o.msg, o.msglen, "no such file"); return OPE_FAILED; }
    g_loads++; g_path = a.s(0); g_start = a.i(1);
    return OPE_OK;
}
static const ArgSpec run_args[] = { ARG_PATH("path") };
static const ArgSpec load_args[] = { ARG_PATH("path"), ARG_INT("start", 0, 65535) };
static void reg(OpRegistry &r)
{
    atari800_desc.register_ops(r);
    r.contributor("prompttest");
    r.add_op("prompt.run", run_args, 1, op_run);
    r.add_op("prompt.load", load_args, 2, op_load);
}
static const OpBind run_bind[] = { B_FILE("path") };
static const OpBind load_bind[] = { B_FILE("path"), B_ASK("start") };
static const FileAction prg_actions[] = { { "Run PRG", "prompt.run", run_bind, 1, true, 0, 0 }, { "Load PRG and start from", "prompt.load", load_bind, 2, true, "Start address", 0 } };
static const FileAction p00_actions[] = { { "Run PRG", "prompt.run", run_bind, 1, true, 0, 0 }, { "Load PRG and start from", "prompt.load", load_bind, 2, true, "Start address", 0x1A } };
static const FileType prompt_types[] = { { ".prg", prg_actions, 2, 0, 0, 0 }, { ".p00", p00_actions, 2, 0, 0, 0 } };

struct Host : MenuHost {
    void info(int, char *out, int n) { snprintf(out, n, "x"); }
    bool return_to_host(char *msg, int n) { snprintf(msg, n, "no"); return false; }
};
static std::string row0(MenuCtl &m, std::string *sub = 0)
{
    char l[100], r[100], s[100];
    bool a;
    m.row(0, l, 100, r, 100, &a, s, 100);
    if (sub) *sub = s;
    return std::string(l) + "|" + r;
}

static std::vector<uint8_t> prg(uint16_t load, size_t n)
{
    std::vector<uint8_t> v(2 + n, 0xEA);
    v[0] = (uint8_t)load; v[1] = (uint8_t)(load >> 8);
    return v;
}

int main()
{
    CoreDesc d = atari800_desc;
    d.register_ops = reg;
    d.filetypes = prompt_types;
    d.nfiletypes = 2;
    SioModel io; FakeFs fs; PxRec px;
    CoreCtx c;
    memset(&c, 0, sizeof(c));
    c.desc = &d; c.io = &io; c.fs = &fs;
    Settings st(d.settings, d.nsettings, d.store, &fs, "/usb0/cores/atari800.cfg", 0);
    c.settings = &st;
    Video vid(d.video, &px, &io);
    c.video = &vid;
    st.load();
    c.started = true;
    fs.put("/usb0/x.prg", prg(0xC000, 64));
    std::vector<uint8_t> p00(0x1A, 0);
    std::vector<uint8_t> inner = prg(0x2000, 16);
    p00.insert(p00.end(), inner.begin(), inner.end());
    fs.put("/usb0/y.p00", p00);
    fs.put("/usb0/z.prg", prg(0x0000, 0x2000));
    Host host;
    MenuCtl m(&c, &host);
    m.open();
    m.key(KEY_F3);                                          // the browser: it opens on the media level; the stick is /usb0
    { char l[100], r_[100]; bool a_; for (int i = 0; i < m.rows(); i++) { m.row(i, l, 100, r_, 100, &a_); if (!strcmp(l, "usb0/")) { while (m.selected() < i) m.key(KEY_DOWN); } } }
    m.key(KEY_RETURN);
    CHECK(m.rows() >= 3);
    auto select = [&](const char *name) {
        char l[100], rr[100]; bool a;
        for (int i = 0; i < m.rows(); i++) { m.row(i, l, 100, rr, 100, &a); if (!strcmp(l, name)) { while (m.selected() > i) m.key(KEY_UP); while (m.selected() < i) m.key(KEY_DOWN); return true; } }
        printf("FAIL no row %s\n", name); g_fails++;
        return false;
    };
    auto to_prompt = [&](const char *file) {
        select(file);
        CHECK_EQ(m.key(KEY_RETURN), MenuCtl::R_REDRAW);     // two actions: the chooser
        CHECK(m.chooser());
        m.key(KEY_DOWN);
        CHECK_EQ(m.key(KEY_RETURN), MenuCtl::R_REDRAW);     // "Load PRG and start from": the prompt, nothing run yet
        CHECK(m.entering());
    };

    // the chooser has both actions; Run PRG asks nothing
    select("x.prg");
    m.key(KEY_RETURN);
    CHECK(m.chooser());
    char l[100], rr[100]; bool act;
    m.row(0, l, 100, rr, 100, &act); CHECK_STR(l, "Run PRG");
    m.row(1, l, 100, rr, 100, &act); CHECK_STR(l, "Load PRG and start from");
    m.key(KEY_ESCAPE);
    CHECK(!m.chooser());

    // the prompt starts on the file's own load address
    to_prompt("x.prg");
    CHECK_EQ(m.rows(), 1);
    CHECK_EQ(m.selected(), 0);
    std::string sub;
    CHECK_STR(row0(m, &sub).c_str(), "Start address|$C000");
    CHECK(sub.find("RETURN") != std::string::npos);
    int area = m.area(), tab = m.tab();
    // digits are digits: the tab strip does not see them. The first one replaces the default.
    CHECK_EQ(m.key('1'), MenuCtl::R_REDRAW);
    CHECK_STR(row0(m).c_str(), "Start address|$1_");
    CHECK_EQ(m.area(), area); CHECK_EQ(m.tab(), tab);
    m.key('0'); m.key('0'); m.key('0');
    CHECK_STR(row0(m).c_str(), "Start address|$1000");
    m.key('F');                                              // a fifth digit has no room
    CHECK_STR(row0(m).c_str(), "Start address|$1000");
    m.key(KEY_BACK);
    CHECK_STR(row0(m).c_str(), "Start address|$100_");
    m.key('g'); m.key(' '); m.key(KEY_UP);                  // not hex: swallowed
    CHECK_STR(row0(m).c_str(), "Start address|$100_");
    m.key('c'); m.key('A');                                  // both cases, then the room is gone
    CHECK_STR(row0(m).c_str(), "Start address|$100C");
    m.key(KEY_BACK); m.key(KEY_BACK); m.key('0');
    CHECK_STR(row0(m).c_str(), "Start address|$100_");
    m.key('0');
    // nothing has run yet
    CHECK_EQ(g_loads + g_runs, 0);
    // RETURN takes it: the operation gets $1000
    CHECK_EQ(m.key(KEY_RETURN), MenuCtl::R_CLOSE);
    CHECK(!m.entering());
    CHECK_EQ(g_loads, 1);
    CHECK_EQ(g_start, 0x1000);
    CHECK_STR(g_path.c_str(), "/usb0/x.prg");
    CHECK(strstr(m.status(), "Load PRG and start from") != 0);

    // ESC cancels: nothing happens
    to_prompt("x.prg");
    int loads = g_loads;
    m.key('4'); m.key('2');
    CHECK_EQ(m.key(KEY_ESCAPE), MenuCtl::R_REDRAW);
    CHECK(!m.entering());
    CHECK(m.chooser());                                      // back where it came from
    m.key(KEY_ESCAPE);
    CHECK(!m.chooser());
    CHECK_EQ(g_loads, loads);

    // RETURN without a digit takes the default; so does a field typed empty again
    to_prompt("x.prg");
    CHECK_EQ(m.key(KEY_RETURN), MenuCtl::R_CLOSE);
    CHECK_EQ(g_start, 0xC000);
    to_prompt("x.prg");
    m.key('7'); m.key(KEY_BACK);
    CHECK_STR(row0(m).c_str(), "Start address|$_");
    CHECK_EQ(m.key(KEY_RETURN), MenuCtl::R_CLOSE);
    CHECK_EQ(g_start, 0xC000);

    // a .P00: the default is the word behind its header
    to_prompt("y.p00");
    CHECK_STR(row0(m).c_str(), "Start address|$2000");
    m.key(KEY_ESCAPE);
    m.key(KEY_ESCAPE);

    // a function key leaves the prompt and goes where it says; the closing key closes
    to_prompt("x.prg");
    m.key('1');
    CHECK_EQ(m.key(KEY_F1), MenuCtl::R_REDRAW);
    CHECK(!m.entering());
    CHECK_EQ(m.area(), 0);
    m.key(KEY_F3);
    to_prompt("x.prg");
    CHECK_EQ(m.key(KEY_MENU), MenuCtl::R_CLOSE);
    CHECK(!m.entering());
    m.key(KEY_F3);

    // Run PRG from the chooser runs at once
    select("x.prg");
    m.key(KEY_RETURN);
    int runs = g_runs;
    CHECK_EQ(m.key(KEY_RETURN), MenuCtl::R_CLOSE);
    CHECK(!m.entering());
    CHECK_EQ(g_runs, runs + 1);

    // a file whose own load address is 0 is started at an address of its own
    to_prompt("z.prg");
    CHECK_STR(row0(m).c_str(), "Start address|$0000");
    m.key('2'); m.key('0'); m.key('0'); m.key('0');
    CHECK_EQ(m.key(KEY_RETURN), MenuCtl::R_CLOSE);
    CHECK_EQ(g_start, 0x2000);

    // a file the operation cannot start is refused at the prompt's RETURN, the menu stays
    to_prompt("x.prg");
    fs.remove("/usb0/x.prg");
    CHECK_EQ(m.key(KEY_RETURN), MenuCtl::R_REDRAW);
    CHECK(m.status_bad());
    DONE("test_prompt");
}
