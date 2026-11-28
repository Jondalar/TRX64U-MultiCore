// T260 P5: the panel file (cores/diag/app/lib/panel.cc): the parser and its refusals, the register rows (whole, some bits, two bytes, signed), the steps, the presets as text, Defaults, and the
// list the person edits on the native model of CoreDialogs. The A/V panel's own register writes are proven by test_diag_avtrace (golden).
#include "check.h"
#include "fakes.h"
#include "portdiag.h"
#include "panel.h"
#include <fstream>
#include <sstream>
#include <functional>

static Panel g_p;

static int parse(const char *t, char *err = 0)
{
    char e[160];
    int rc = panel_parse(t, (uint32_t)strlen(t), &g_p, portdiag_reg_by_name, e, sizeof(e));
    if (err) { snprintf(err, 160, "%s", e); }
    return rc;
}

static int g_sys = -1;
static void sys_hook(CoreCtx *, int v) { g_sys = v; }

struct Dlg : CoreDialogs {
    int calls = 0;
    std::string text_in, noticed;
    bool spawn(void (*)(void *), void *) override { return false; }
    int ask(const char *, const char *, int) override { return -1; }
    int choose(const char *, const char *, const char *const *, int, int) override { return -1; }
    void notice(const char *, const char *t) override { noticed = t; }
    bool pick(const char *, char *, int) override { return false; }
    bool stamp(char[16]) override { return false; }
    bool text(const char *, char *buf, int max) override { snprintf(buf, max, "%s", text_in.c_str()); return !text_in.empty(); }
    std::function<int(int, DlgRow *, int, void (*)(void *, int, int), void *)> script;
    int edit_list(const char *, const char *, DlgRow *rows, int n, int sel, void (*cb)(void *, int, int), void *ud) override { return script(calls++, rows, n, cb, ud); }
};

int main()
{
    FakeIo io;
    FakeFs fs;
    CoreCtx c;
    memset(&c, 0, sizeof(c));
    c.io = &io;
    c.fs = &fs;
    panel_set_system_hook(sys_hook);
    char err[160];

    // the shipped A/V panel names registers of the TOML A: all of them resolve (a name that does not is a parse error)
    {
        std::ifstream f(std::string(COREAPP_DIR) + "/../cores/diag/sd/panels/avout.toml");
        std::stringstream s;
        s << f.rdbuf();
        std::string t = s.str();
        CHECK(panel_parse(t.data(), (uint32_t)t.size(), &g_p, portdiag_reg_by_name, err, sizeof(err)) == 0);
        CHECK_EQ(g_p.nrows, 27);
        CHECK_STR(g_p.row[0].key, "Standard");
        CHECK(g_p.row[0].system && g_p.row[0].nvfmt == 4 && g_p.row[0].vfmt[1] == 0x2B);
        CHECK_STR(g_p.row[26].action, "defaults");
    }
    PanelReg g;
    CHECK(portdiag_reg_by_name("av.bend", &g) && g.bytes == 2 && g.mask == 0xFFFF);
    CHECK(portdiag_reg_by_name("av.syncb.l262", &g) && g.shift == 1 && g.mask == 1);
    CHECK(!portdiag_reg_by_name("av.nothing", &g));

    // refusals, each with its line
    CHECK(parse("kind = \"panel\"\n[[row]]\nkey = \"A\"\nreg = \"av.nothing\"\n", err) != 0 && strstr(err, "line 4") && strstr(err, "no register called"));
    CHECK(parse("kind = \"test\"\n", err) != 0);
    CHECK(parse("kind = \"panel\"\n[[row]]\nkey = \"A\"\nreg = 0x50\nnames = [\"x\", \"y\"]\nvalues = [1]\n", err) != 0 && strstr(err, "same number"));
    CHECK(parse("kind = \"panel\"\n[[row]]\nkey = \"A\"\naction = \"launch\"\n", err) != 0 && strstr(err, "load, save or defaults"));
    CHECK(parse("kind = \"panel\"\n[[row]]\nkey = \"A\"\nreg = 0x50\nmin = 9\nmax = 3\n", err) != 0 && strstr(err, "empty"));
    CHECK(parse("kind = \"panel\"\n[[row]]\nkey = \"A\"\nreg = 0x50\nnames = [\"x\"]\ndefault = \"z\"\n", err) != 0 && strstr(err, "no name of the row"));
    CHECK(parse("kind = \"panel\"\n[panel]\nname = \"N\"\n", err) != 0 && strstr(err, "no row"));
    CHECK(parse("kind = \"panel\"\n[[row]]\nkey = \"A\"\nreg = \"av.std\"\nbytes = 2\n", err) != 0 && strstr(err, "register number only"));

    // register rows: a number with bytes, shift and mask; signed; steps that wrap; names with values
    CHECK(parse("kind = \"panel\"\n[panel]\nname = \"T\"\npreset_kind = \"tp\"\n"
                "[[row]]\nkey = \"Word\"\nreg = 0x100\nbytes = 2\nmin = 0\nmax = 1000\nstep = 300\ndefault = 7\n"
                "[[row]]\nkey = \"Nib\"\nreg = 0x102\nshift = 4\nmask = 0xF\nnames = [\"a\", \"b\", \"c\"]\nvalues = [1, 5, 9]\ndefault = \"b\"\n"
                "[[row]]\nkey = \"Sgn\"\nreg = 0x103\nsigned = true\nmin = -3\nmax = 3\n"
                "[[row]]\nkey = \"Save preset\"\naction = \"save\"\n", err) == 0);
    CHECK_EQ(g_p.nrows, 4);
    const PanelRow &w = g_p.row[0], &nib = g_p.row[1], &sg = g_p.row[2];
    io.regs[0x102] = 0xA3;
    panel_put(&c, nib, 9);
    CHECK_EQ(io.regs[0x102], 0x93);                              // only the high nibble moved
    CHECK_EQ(panel_get(&c, nib), 9);
    panel_put(&c, w, 0x1234);                                    // clamped to 1000 = 0x3E8, little endian
    CHECK(io.regs[0x100] == 0xE8 && io.regs[0x101] == 0x03);
    panel_step(&c, w, 1);                                        // 1000 + 300 > max: wraps to min
    CHECK_EQ(panel_get(&c, w), 0);
    panel_step(&c, w, -1);
    CHECK_EQ(panel_get(&c, w), 1000);
    panel_step(&c, nib, 1);                                      // 9 is the last name: wraps to the first
    CHECK_EQ(panel_get(&c, nib), 1);
    panel_step(&c, nib, -1);
    CHECK_EQ(panel_get(&c, nib), 9);
    panel_put(&c, nib, 4);                                       // a value with no name shows as #n and the next step goes to the first name
    char tx[48];
    panel_text(&c, nib, tx, sizeof(tx));
    CHECK_STR(tx, "#4");
    panel_step(&c, nib, 1);
    CHECK_EQ(panel_get(&c, nib), 1);
    panel_put(&c, sg, -2);
    CHECK_EQ(io.regs[0x103], 0xFE);
    CHECK_EQ(panel_get(&c, sg), -2);
    panel_text(&c, sg, tx, sizeof(tx));
    CHECK_STR(tx, "-2");
    panel_put(&c, sg, -99);                                      // below min: clamped
    CHECK_EQ(panel_get(&c, sg), -3);

    // presets: written as text, read back in the order of the panel; a name the row lacks is counted; the file's other lines are not rows
    panel_put(&c, nib, 5);
    panel_put(&c, w, 600);
    char pb[1200];
    int n = panel_preset_write(&c, g_p, "mine", pb, sizeof(pb));
    CHECK(n > 0 && strstr(pb, "kind = \"tp\"") && strstr(pb, "Nib = \"b\"") && strstr(pb, "Word = \"#600\""));
    CHECK(!strstr(pb, "Save preset"));
    panel_put(&c, nib, 1);
    panel_put(&c, w, 0);
    int bad = -1;
    CHECK_EQ(panel_preset_load(&c, g_p, pb, &bad), 3);
    CHECK_EQ(bad, 0);
    CHECK(panel_get(&c, nib) == 5 && panel_get(&c, w) == 600 && panel_get(&c, sg) == -3);
    CHECK_EQ(panel_preset_load(&c, g_p, "Nib = \"B\"\nSgn = \"#1\"\nWord = \"#x\"\n", &bad), 3);   // a label is found without regard to case
    CHECK_EQ(panel_preset_load(&c, g_p, "[preset]\nNib = \"zz\"\n", &bad), 0);
    CHECK_EQ(bad, 1);
    panel_defaults(&c, g_p);
    CHECK(panel_get(&c, w) == 7 && panel_get(&c, nib) == 5 && panel_get(&c, sg) == -3);     // Word 7, Nib "b" = 5, Sgn no default: its min

    // the Standard row: the System Mode hook and the video format byte follow the value
    CHECK(parse("kind = \"panel\"\n[[row]]\nkey = \"S\"\nreg = 0x50\nnames = [\"a\", \"b\"]\nsystem = true\nvideo_format = [0x10, 0x20]\n", err) == 0);
    panel_put(&c, g_p.row[0], 1);
    CHECK(g_sys == 1 && io.vfmt == 0x20 && io.regs[0x50] == 1);

    // the list: RIGHT on a row calls back and the row's text follows; an action row answers its index and the panel does it (Save: asks for a name, writes the file)
    CHECK(parse("kind = \"panel\"\n[panel]\nname = \"T\"\npresets = \"pre\"\npreset_kind = \"tp\"\n"
                "[[row]]\nkey = \"Nib\"\nreg = 0x102\nshift = 4\nmask = 0xF\nnames = [\"a\", \"b\", \"c\"]\n"
                "[[row]]\nkey = \"Save preset\"\naction = \"save\"\n[[row]]\nkey = \"Defaults\"\naction = \"defaults\"\n", err) == 0);
    io.regs[0x102] = 0;
    Dlg d;
    d.text_in = "my one";
    d.script = [&](int call, DlgRow *rows, int nr, void (*cb)(void *, int, int), void *ud) -> int {
        if (call == 0) {
            CHECK_EQ(nr, 3);
            CHECK(rows[0].value[0] == 'a' && !rows[0].action && rows[1].action && !rows[1].value[0]);
            cb(ud, 0, DLG_EDIT_RIGHT);
            CHECK_STR(rows[0].value, "b");
            return 1;                                                    // Save preset
        }
        if (call == 1) { return 2; }                                     // Defaults: row 0 has no default, so it takes the first name
        CHECK_STR(rows[0].value, "a");
        return -1;
    };
    panel_run(&c, &d, g_p, "/SD/cores/diag");
    CHECK_EQ(d.calls, 3);
    CHECK(strstr(d.noticed.c_str(), "Preset written to /SD/cores/diag/pre/my one.toml") != 0);
    char back[600];
    int len = fs.load("/SD/cores/diag/pre/my one.toml", back, sizeof(back) - 1);
    CHECK(len > 0);
    back[len > 0 ? len : 0] = 0;
    CHECK(strstr(back, "Nib = \"b\"") != 0);                             // the file was written after RIGHT, before Defaults
    DONE("test_panel");
}
