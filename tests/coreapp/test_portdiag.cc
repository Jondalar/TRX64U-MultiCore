// T175: the diag core's descriptor against the register map and the engine model: identity, the live rows read the live register, the start releases the
// outputs and writes the example tests once, the operations start the runner on the host's task and refuse a second one, and the application's
// own identity facts (no picture, menu first, live redraw).
#include "check.h"
#include "fakes.h"
#include "boot.h"
#include "cores.h"
#include "ops_glue.h"
#include "portdiag.h"
#include "portdiag_regs.h"
#include "diagtest.h"
#include "diag_examples.h"
#include "diag_engine.h"
#include "settings_ops.h"
#include "settings.h"
#include <fstream>
#include <sstream>
#include <vector>

static std::vector<uint8_t> slurp_bytes(const char *path)
{
    std::ifstream f(path ? path : "", std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static std::string slurp_file(const std::string &path)
{
    std::ifstream f(path);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

struct Host : CoreDialogs {
    int spawned = 0, asks = 0, notices = 0, picks = 0;
    void (*fn)(void *) = 0;
    void *arg = 0;
    bool spawn(void (*f)(void *), void *a) override { spawned++; fn = f; arg = a; return true; }
    std::vector<int> answers, timeouts;                 // scripted answers of ask() (1 yes, 0 no, -1 stop, -2 time ran out), the timeout_s each question carried
    int ask(const char *, const char *, int timeout_s) override { timeouts.push_back(timeout_s); return asks < (int)answers.size() ? answers[asks++] : (asks++, 1); }
    int choose(const char *, const char *, const char *const *, int, int timeout_s) override { timeouts.push_back(timeout_s); return 0; }
    void notice(const char *, const char *) override { notices++; }
    bool pick(const char *, char *out, int) override { picks++; strcpy(out, "/SD/cores/diag/tests/pins.toml"); return true; }
    bool stamp(char out[16]) override { strcpy(out, "20261102-143015"); return true; }
};

int main()
{
    const CoreDesc &d = portdiag_desc;
    CHECK(!memcmp(d.id, "DIAG", 4));
    CHECK_STR(d.cor_name, "Port Diagnostics");
    CHECK_STR(d.core_id, "diag");
    CHECK_STR(d.folder, "diag");
    CHECK_EQ((int)d.video.w, 0);                       // no picture of its own
    CHECK(d.menu_first);
    CHECK_EQ((int)d.live_ms, 250);
    CHECK_EQ(d.nsettings, 0);                      // the A/V rows are a panel (T260 P5), no settings page
    CHECK_EQ(d.nroms, 0);
    CHECK_EQ(d.nmedia, 4);
    CHECK_STR(d.media[0].state, "pd.port1");
    CHECK_STR(d.filetypes[0].ext, ".toml");
    CHECK_STR(d.filetypes[0].actions[0].op, "pd.run");

    Engine io;                                          // the model of engine v2: it serves the pin table the live page reads its pins from by name
    FakeFs fs;
    CoreCtx c;
    memset(&c, 0, sizeof(c));
    c.desc = &d;
    c.io = &io;
    c.fs = &fs;
    snprintf(c.core_version, sizeof(c.core_version), "0.1.0");
    snprintf(c.cor_path, sizeof(c.cor_path), "/SD/cores/diag.cor");
    OpRegistry reg;
    d.register_ops(reg);
    CHECK(!reg.error()[0]);
    c.ops = &reg;

    // the start: outputs released; the shipped tests are the delivery's folder on the card (T260 D6), no record of the .cor
    char msg[100];
    CHECK(diag_install_examples(&fs, "/SD/cores/diag") >= 17);
    CHECK_EQ(d.init(&c, msg, sizeof(msg)), 0);
    CHECK_EQ(io.regs[PD_CTRL], PD_CTRL_CLEAR);         // ABORT, then CLEAR: the last write
    bool aborted = false;
    for (auto &w : io.log) aborted |= w.reg == PD_CTRL && w.v == PD_CTRL_ABORT;
    CHECK(aborted);
    CHECK(fs.has("/SD/cores/diag/tests/pins.toml"));
    CHECK(fs.has("/SD/cores/diag/tests/joystick.toml"));
    size_t n = fs.writes;
    CHECK_EQ(d.init(&c, msg, sizeof(msg)), 0);
    CHECK_EQ((size_t)fs.writes, n);                     // the start writes nothing to the card

    // the rows read the snapshot by the names of the table: KBJOY_1_JOY0-4, KBJOY_2_JOY0-4, PADDLE_CMP0-3
    io.forced_low = 0x3E4;                             // port 1 left low, port 2 all low
    io.cmp_now = 3;                                    // comparators 0 and 1 high
    uint32_t rev;
    CHECK_STR(reg.state(&c, "pd.port1", &rev), "U1  D1  L0  R1  F1");
    CHECK_STR(reg.state(&c, "pd.port2", &rev), "U0  D0  L0  R0  F0");
    CHECK_STR(reg.state(&c, "pd.cmp", &rev), "1  1  0  0");
    io.forced_low = 0;
    io.cmp_now = 0;
    CHECK_STR(reg.state(&c, "pd.port2", &rev), "U1  D1  L1  R1  F1");

    // the operations: unattached they refuse, attached they start the runner once
    OpResult r;
    OpIn in = in_str("path", "/SD/cores/diag/tests/pins.toml");
    CHECK_EQ(reg.call(&c, "pd.run", &in, 1, &r), OPE_UNSUPPORTED);
    Host h;
    c.dialogs = &h;
    CHECK_EQ(reg.call(&c, "pd.run", &in, 1, &r), OPE_OK);
    CHECK_EQ(h.spawned, 1);
    CHECK_EQ(reg.call(&c, "pd.run", &in, 1, &r), OPE_BUSY);          // one test at a time
    CHECK_EQ(reg.call(&c, "pd.pick", 0, 0, &r), OPE_BUSY);
    CHECK_EQ(h.spawned, 1);
    // the task body runs against the engine model: the pins test runs to its end and writes its result files, and the host is free again
    h.fn(h.arg);
    CHECK(fs.has("/SD/cores/diag/results/pins_20261102-143015.toml"));
    CHECK(fs.text("/SD/cores/diag/results/pins_20261102-143015.toml").find("status = \"done\"") != std::string::npos);
    CHECK_EQ(reg.call(&c, "pd.pick", 0, 0, &r), OPE_OK);              // free again, and it picks first
    h.fn(h.arg);
    CHECK_EQ(h.picks, 1);
    fs.read_only = false;
    // the prompts through the host's question: yes = OK, no = SKIPPED, the time that ran out = MISSED, each carrying the test's timeout_s
    {
        Engine eng;
        FakeFs fs2;
        CoreCtx c2;
        memset(&c2, 0, sizeof(c2));
        c2.desc = &d;
        c2.io = &eng;
        c2.fs = &fs2;
        snprintf(c2.core_version, sizeof(c2.core_version), "0.1.0");
        snprintf(c2.cor_path, sizeof(c2.cor_path), "/SD/cores/diag.cor");
        fs2.put_text("/SD/cores/diag/tests/stick2.toml", slurp_file(std::string(COREAPP_DIR) + "/../cores/diag/sd/tests/stick2.toml"));
        Host h2;
        h2.answers = { 1, -2, 0, 1 };
        c2.dialogs = &h2;
        OpIn in2 = in_str("path", "/SD/cores/diag/tests/stick2.toml");
        OpResult r2;
        CHECK_EQ(reg.call(&c2, "pd.run", &in2, 1, &r2), OPE_OK);
        h2.fn(h2.arg);
        CHECK_EQ(h2.asks, 4);
        CHECK_EQ(h2.timeouts.size(), 4u);
        CHECK_EQ(h2.timeouts[0], 120);                    // stick2.toml: idle 120 s, the others 300 s
        CHECK_EQ(h2.timeouts[1], 300);
        std::string t = fs2.text("/SD/cores/diag/results/stick2_20261102-143015.toml");
        CHECK(t.find("idle = \"OK\"") != std::string::npos);
        CHECK(t.find("b1 = \"MISSED\"") != std::string::npos);
        CHECK(t.find("b2 = \"SKIPPED\"") != std::string::npos);
        CHECK(t.find("rel = \"OK\"") != std::string::npos);
        // ESC (-1) stops the test at the prompt
        Host h3;
        h3.answers = { -1 };
        c2.dialogs = &h3;
        CHECK_EQ(reg.call(&c2, "pd.run", &in2, 1, &r2), OPE_OK);
        h3.fn(h3.arg);
        CHECK(fs2.text("/SD/cores/diag/results/stick2_20261102-143015.toml").find("status = \"aborted\"") != std::string::npos);
    }
    // ---- T260 P5: a panel file is opened by the same operations (the file's `kind` line says panel): the list goes to the host's edit_list, the walk's hooks read the panel's rows
    {
        FakeIo io7;
        FakeFs fs7;
        CoreCtx c7;
        memset(&c7, 0, sizeof(c7));
        c7.desc = &d;
        c7.io = &io7;
        c7.fs = &fs7;
        snprintf(c7.core_version, sizeof(c7.core_version), "0.1.0");
        snprintf(c7.cor_path, sizeof(c7.cor_path), "/SD/cores/diag.cor");
        diag_install_examples(&fs7, "/SD/cores/diag");
        struct PHost : Host {
            int lists = 0, rows = 0;
            std::string title;
            int edit_list(const char *t, const char *, DlgRow *r, int n, int, void (*)(void *, int, int), void *) override { lists++; rows = n; title = t; return -1; }
        } h7;
        c7.dialogs = &h7;
        OpIn pin = in_str("path", "/SD/cores/diag/panels/avout.toml");
        OpResult r7;
        CHECK_EQ(reg.call(&c7, "pd.run", &pin, 1, &r7), OPE_OK);
        h7.fn(h7.arg);
        CHECK_EQ(h7.lists, 1);
        CHECK_EQ(h7.rows, DT_AV_N + 3);
        CHECK_STR(h7.title.c_str(), "A/V output");
        CHECK_STR(reg.state(&c7, "pd.last", &rev), "panel avout.toml");
        // a broken panel is told to the person, not opened
        fs7.put_text("/SD/cores/diag/panels/bad.toml", "kind = \"panel\"\n[[row]]\nkey = \"X\"\nreg = \"av.nothing\"\n");
        OpIn bin = in_str("path", "/SD/cores/diag/panels/bad.toml");
        CHECK_EQ(reg.call(&c7, "pd.run", &bin, 1, &r7), OPE_OK);
        int notices = h7.notices;
        h7.fn(h7.arg);
        CHECK_EQ(h7.lists, 1);
        CHECK_EQ(h7.notices, notices + 1);
        CHECK(strstr(reg.state(&c7, "pd.last", &rev), "no register called") != 0);
        // the task "Open a panel" picks first, in the panels folder
        CHECK_EQ(reg.call(&c7, "pd.panel", 0, 0, &r7), OPE_OK);
        h7.fn(h7.arg);
        CHECK_EQ(h7.picks, 1);
        // the walk's hooks (the knobs are rows of the panel file) are proven by test_diagtest; here only that a walk with no panel file on the card is refused with that reason
        FakeFs none;
        c7.fs = &none;
        none.put_text("/SD/cores/diag/tests/avtest.toml", "kind = \"avtest\"\n[test]\nname = \"x\"\nid = \"x\"\n");
        OpIn ain = in_str("path", "/SD/cores/diag/tests/avtest.toml");
        CHECK_EQ(reg.call(&c7, "pd.run", &ain, 1, &r7), OPE_OK);
        h7.fn(h7.arg);
        CHECK(strstr(reg.state(&c7, "pd.last", &rev), "panels/avout.toml is not on the card") != 0);
    }
    DONE("test_portdiag");
}
