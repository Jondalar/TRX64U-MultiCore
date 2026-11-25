// T260 S7 proof: the A/V panel (cores/diag/sd/panels/avout.toml, run by cores/diag/app/lib/panel.cc through CoreDialogs::edit_list) writes the same registers, in the same order, as the
// C code of the T259 A/V page did - per shipped preset and per RIGHT on each row. tests/coreapp/golden/diag_av_trace.txt is the record of that page (it was written by that C code,
// UPDATE_GOLDEN=1 writes the file from the panel).
// The native model of the dialogs (ScriptDlg) plays the person: the first edit_list returns the row "Load preset" (the panel asks the picker, loads the file, tells the count), the second
// presses RIGHT, and LEFT to put the row back, on every value row, the last returns -1 (ESC).
#include "check.h"
#include "fakes.h"
#include "boot.h"
#include "ops_glue.h"
#include "portdiag.h"
#include "portdiag_regs.h"
#include "diagtest.h"
#include "panel.h"
#include <fstream>
#include <sstream>
#include <functional>

static std::string slurp(const std::string &p)
{
    std::ifstream f(p);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

struct ScriptDlg : CoreDialogs {
    int calls = 0;
    int load_row = -1;
    std::string pick_path;
    std::function<void(const char *)> on_notice;
    std::function<void(int, DlgRow *, void (*)(void *, int, int), void *)> on_rows;
    bool spawn(void (*)(void *), void *) override { return false; }
    int ask(const char *, const char *, int) override { return -1; }
    int choose(const char *, const char *, const char *const *, int, int) override { return -1; }
    void notice(const char *, const char *text) override { if (on_notice) on_notice(text); }
    bool pick(const char *, char *out, int outlen) override { snprintf(out, outlen, "%s", pick_path.c_str()); return true; }
    bool stamp(char[16]) override { return false; }
    int edit_list(const char *, const char *, DlgRow *rows, int n, int, void (*cb)(void *, int, int), void *ud) override
    {
        if (calls++ == 0) {
            for (int i = 0; i < n; i++) { if (!strcmp(rows[i].label, "Load preset")) { load_row = i; } }
            return load_row;
        }
        if (on_rows) { on_rows(n, rows, cb, ud); }
        return -1;
    }
};

struct Rig {
    FakeIo io;
    FakeFs fs;
    CoreCtx c;
    Rig()
    {
        memset(&c, 0, sizeof(c));
        c.desc = &portdiag_desc;
        c.io = &io;
        c.fs = &fs;
        snprintf(c.core_version, sizeof(c.core_version), "0.1.0");
        snprintf(c.cor_path, sizeof(c.cor_path), "/SD/cores/diag.cor");
    }
    void dump(std::string &out)
    {
        char b[64];
        for (auto &w : io.log) {
            snprintf(b, sizeof(b), "w %03X %02X\n", (unsigned)w.reg, (unsigned)w.v);
            out += b;
        }
        snprintf(b, sizeof(b), "vfmt %02X\n", (unsigned)io.vfmt);
        out += b;
        io.log.clear();
    }
};

static Panel g_panel;

int main()
{
    std::string root = std::string(COREAPP_DIR) + "/../cores/diag/sd/";
    std::string ptext = slurp(root + "panels/avout.toml");
    char err[160];
    CHECK(panel_parse(ptext.data(), (uint32_t)ptext.size(), &g_panel, portdiag_reg_by_name, err, sizeof(err)) == 0);
    if (err[0]) { printf("%s\n", err); }
    CHECK_EQ(g_panel.nrows, DT_AV_N + 3);                       // the 24 knobs of the walks and the three action rows
    CHECK_STR(g_panel.name, "A/V output");

    std::string out = "# T260 S7 golden: the A/V page's register writes (portdiag.cc as of T259), `w <register> <value>` in order\n";
    const char *presets[] = { "Sony-PAL", "NTSC-50.rix01", "NTSC-60.rix02" };
    for (const char *pn : presets) {
        Rig r;
        std::string pt = std::string("/SD/cores/diag/presets/") + pn + ".toml";
        r.fs.put_text(pt, slurp(root + "presets/" + pn + ".toml"));
        ScriptDlg dlg;
        dlg.pick_path = pt;
        std::string notice;
        dlg.on_notice = [&](const char *t) { notice = t; out += std::string("preset ") + pn + "\n"; r.dump(out); };
        dlg.on_rows = [&](int n, DlgRow *rows, void (*cb)(void *, int, int), void *ud) {
            for (int k = 0; k < n; k++) {
                if (rows[k].action) { continue; }
                std::string before = rows[k].value;
                r.io.log.clear();
                cb(ud, k, DLG_EDIT_RIGHT);
                out += std::string("right ") + pn + " / " + rows[k].label + " -> " + rows[k].value + "\n";
                r.dump(out);
                cb(ud, k, DLG_EDIT_LEFT);                        // back, so every row starts from the preset
                CHECK_STR(rows[k].value, before.c_str());
                r.io.log.clear();
            }
        };
        r.io.log.clear();
        panel_run(&r.c, &dlg, g_panel, "/SD/cores/diag");
        CHECK(strstr(notice.c_str(), "24 rows loaded, 0 with a value the row does not have"));
        CHECK_EQ(dlg.calls, 2);                                 // the list with the action it returned, then the list the script walks (it answers -1: ESC)
    }
    std::string gp = std::string(GOLDEN_DIR) + "/diag_av_trace.txt";
    if (getenv("UPDATE_GOLDEN")) {
        std::ofstream(gp) << out;
        printf("wrote %s (%d bytes)\n", gp.c_str(), (int)out.size());
    } else {
        std::string g = slurp(gp);
        CHECK(!g.empty());
        CHECK(g == out);
        if (g != out) {
            std::ofstream("/tmp/diag_av_trace.got") << out;
        }
    }
    DONE("test_diag_avtrace");
}
