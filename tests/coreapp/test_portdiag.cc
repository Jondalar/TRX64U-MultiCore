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
#include "instfiles.h"
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

struct Host : PdHost {
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
    CHECK_EQ(d.nsettings, DT_AV_N);               // T201: the A/V page
    CHECK_EQ(d.nroms, 0);
    CHECK_EQ(d.nmedia, 4);
    CHECK_STR(d.media[0].state, "pd.port1");
    CHECK_STR(d.filetypes[0].ext, ".toml");
    CHECK_STR(d.filetypes[0].actions[0].op, "pd.run");

    FakeIo io;
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

    // the start: outputs released; the examples are record 8 of diag.cor, written by the application's installer (instcore.cc) at the start of the core
    fs.put("/SD/cores/diag.cor", slurp_bytes(getenv("DIAG_COR")));
    char msg[100];
    CHECK_EQ(core_files_install(&c), 10);
    CHECK_EQ(d.init(&c, msg, sizeof(msg)), 0);
    CHECK_EQ(io.regs[PD_CTRL], PD_CTRL_CLEAR);         // ABORT, then CLEAR: the last write
    bool aborted = false;
    for (auto &w : io.log) aborted |= w.reg == PD_CTRL && w.v == PD_CTRL_ABORT;
    CHECK(aborted);
    CHECK(fs.has("/SD/cores/diag/tests/pins.toml"));
    CHECK(fs.has("/SD/cores/diag/tests/joystick.toml"));
    size_t n = fs.writes;
    CHECK_EQ(d.init(&c, msg, sizeof(msg)), 0);
    CHECK_EQ(core_files_install(&c), 0);
    CHECK_EQ((size_t)fs.writes, n);                     // the second start writes nothing

    // the rows read the live register: bits 0-4 port 1, 5-9 port 2, 10-13 the comparators
    io.regs[PD_LIVE] = 0x1F ^ 0x04;                    // port 1 left low
    io.regs[PD_LIVE + 1] = 0x04 | 0x08;                // cmp bit 0 and 1 high -> bits 10, 11
    uint32_t rev;
    CHECK_STR(reg.state(&c, "pd.port1", &rev), "U1  D1  L0  R1  F1");
    CHECK_STR(reg.state(&c, "pd.port2", &rev), "U0  D0  L0  R0  F0");
    CHECK_STR(reg.state(&c, "pd.cmp", &rev), "1  1  0  0");
    io.regs[PD_LIVE] = 0xFF;
    io.regs[PD_LIVE + 1] = 0x03;
    CHECK_STR(reg.state(&c, "pd.port2", &rev), "U1  D1  L1  R1  F1");

    // the operations: unattached they refuse, attached they start the runner once
    OpResult r;
    OpIn in = in_str("path", "/SD/cores/diag/tests/pins.toml");
    CHECK_EQ(reg.call(&c, "pd.run", &in, 1, &r), OPE_UNSUPPORTED);
    Host h;
    portdiag_attach(&h);
    CHECK_EQ(reg.call(&c, "pd.run", &in, 1, &r), OPE_OK);
    CHECK_EQ(h.spawned, 1);
    CHECK_EQ(reg.call(&c, "pd.run", &in, 1, &r), OPE_BUSY);          // one test at a time
    CHECK_EQ(reg.call(&c, "pd.pick", 0, 0, &r), OPE_BUSY);
    CHECK_EQ(h.spawned, 1);
    // the task body runs against the engine: here the register model answers no "DIAG", so the run ends with an error file and the host is free again
    h.fn(h.arg);
    CHECK(fs.has("/SD/cores/diag/results/pins_20261102-143015.toml"));
    CHECK(fs.text("/SD/cores/diag/results/pins_20261102-143015.toml").find("status = \"error\"") != std::string::npos);
    CHECK_EQ(reg.call(&c, "pd.pick", 0, 0, &r), OPE_OK);              // free again, and it picks first
    h.fn(h.arg);
    CHECK_EQ(h.picks, 1);
    // the example writer as a task
    fs.remove("/SD/cores/diag/tests/cd32.toml");
    CHECK_EQ(reg.call(&c, "pd.install", 0, 0, &r), OPE_OK);
    CHECK(fs.has("/SD/cores/diag/tests/cd32.toml"));
    fs.read_only = true;
    CHECK_EQ(reg.call(&c, "pd.install", 0, 0, &r), OPE_OK);           // all there: nothing to write
    fs.read_only = false;
    fs.remove("/SD/cores/diag/tests/cd32.toml");
    fs.read_only = true;
    CHECK_EQ(reg.call(&c, "pd.install", 0, 0, &r), OPE_FAILED);       // a read only card says so
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
        portdiag_attach(&h2);
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
        portdiag_attach(&h3);
        CHECK_EQ(reg.call(&c2, "pd.run", &in2, 1, &r2), OPE_OK);
        h3.fn(h3.arg);
        CHECK(fs2.text("/SD/cores/diag/results/stick2_20261102-143015.toml").find("status = \"aborted\"") != std::string::npos);
    }
    // ---- T201 S3: each row of the A/V page writes its register (settings.set is what a click does), every value of every row, and the page reads back what the register holds
    {
        FakeIo io3;
        FakeFs fs3;
        CoreCtx c3;
        memset(&c3, 0, sizeof(c3));
        c3.desc = &d;
        c3.io = &io3;
        c3.fs = &fs3;
        snprintf(c3.core_version, sizeof(c3.core_version), "0.1.0");
        snprintf(c3.cor_path, sizeof(c3.cor_path), "/SD/cores/diag.cor");
        OpRegistry reg3;
        d.register_ops(reg3);
        settings_register(reg3);
        c3.ops = &reg3;
        Settings st3(d.settings, d.nsettings, d.store, &fs3, "/SD/cores/diag.cfg", 0);
        st3.load();
        c3.settings = &st3;
        CHECK_EQ(d.nsettings, DT_AV_N);
        const uint8_t sysfmt[4] = { 0x00, 0x2B, 0x2A, 0x01 };
        OpResult r3;
        int rows = 0;
        for (int k = 0; k < DT_AV_N; k++) {
            const AvKnob &kn = dt_av_knobs[k];
            CHECK_STR(d.settings[k].key, kn.key);
            for (int i = 0; i < kn.n; i++) {
                CHECK_EQ(settings_set(&c3, kn.key, kn.labels[i], &r3), OPE_OK);
                int v = kn.val[i];
                switch (k) {
                case AVK_STD: CHECK_EQ(io3.regs[PD_AV_STD], v); CHECK_EQ(io3.vfmt, sysfmt[v]); break;
                case AVK_GROUPS: CHECK_EQ(io3.regs[PD_AV_PERM], v); break;
                case AVK_BITS_A: case AVK_BITS_B: case AVK_BITS_C: CHECK_EQ((io3.regs[PD_AV_REV] >> (k - AVK_BITS_A)) & 1, v); break;
                case AVK_VSIGN: CHECK_EQ(io3.regs[PD_AV_VINV] & 1, v); break;
                case AVK_TRIM: CHECK_EQ((int8_t)io3.regs[PD_AV_TRIM], v); break;
                case AVK_BSTART: CHECK_EQ(io3.regs[PD_AV_BSTART], v); break;
                case AVK_BEND: CHECK_EQ(io3.regs[PD_AV_BEND] | (io3.regs[PD_AV_BEND + 1] << 8), v); break;
                case AVK_BAMP: CHECK_EQ(io3.regs[PD_AV_BAMP], v); break;
                case AVK_GAIN: CHECK_EQ(io3.regs[PD_AV_GAIN], v); break;
                case AVK_SYNC: CHECK_EQ(io3.regs[PD_AV_SYNC], v); break;
                case AVK_BLANK: CHECK_EQ(io3.regs[PD_AV_BLANK], v); break;
                case AVK_WHITE: CHECK_EQ(io3.regs[PD_AV_WHITE], v); break;
                case AVK_EQ: CHECK_EQ(io3.regs[PD_AV_SYNCB] & 1, v); break;
                case AVK_VLEN: CHECK_EQ(io3.regs[PD_AV_VLEN], v); break;
                case AVK_VPOS: CHECK_EQ((int8_t)io3.regs[PD_AV_VPOS], v); break;
                default: CHECK_EQ((io3.regs[PD_AV_SYNCB] >> 1) & 1, v); break;
                }
                rows++;
            }
            settings_set(&c3, kn.key, kn.labels[kn.def], &r3);
        }
        CHECK(rows > 100);
        // register 053 holds two bits (V sign, freeze) and 05D two (equalizing, 262 lines): a row changes its own and leaves the other
        settings_set(&c3, "V sign", "Normal", &r3);
        CHECK_EQ(io3.regs[PD_AV_VINV], 0);
        io3.regs[PD_AV_VINV] |= 2;                         // the walk's freeze
        settings_set(&c3, "V sign", "Inverted", &r3);
        CHECK_EQ(io3.regs[PD_AV_VINV], 3);
        io3.regs[PD_AV_VINV] &= ~2;
        settings_set(&c3, "Equalizing", "On", &r3);
        settings_set(&c3, "Lines 60 Hz", "262", &r3);
        CHECK_EQ(io3.regs[PD_AV_SYNCB], 3);
        settings_set(&c3, "Equalizing", "Off", &r3);
        CHECK_EQ(io3.regs[PD_AV_SYNCB], 2);
        settings_set(&c3, "Lines 60 Hz", "263", &r3);
        CHECK_EQ(io3.regs[PD_AV_SYNCB], 0);
        settings_set(&c3, "Vsync length", "2.5 lines", &r3);
        settings_set(&c3, "Vsync position", "-8", &r3);
        CHECK_EQ(io3.regs[PD_AV_VLEN], 5);
        CHECK_EQ(io3.regs[PD_AV_VPOS], 0xF8);
        settings_set(&c3, "Vsync length", "3 lines", &r3);
        settings_set(&c3, "Vsync position", "0", &r3);
        // the bit orders of A, B and C are three bits of one register: setting one leaves the others
        settings_set(&c3, "Bits A", "Reversed", &r3);
        settings_set(&c3, "Bits C", "Reversed", &r3);
        CHECK_EQ(io3.regs[PD_AV_REV], 5);
        settings_set(&c3, "Bits A", "MSB first", &r3);
        CHECK_EQ(io3.regs[PD_AV_REV], 4);
        settings_set(&c3, "Bits C", "MSB first", &r3);

        // ---- T201 S6: a preset is written, the application restarts (new objects over the same card, the .cfg of the page is read again), the preset loads and the registers hold the same values
        settings_set(&c3, "Standard", "NTSC 60 Hz", &r3);
        settings_set(&c3, "Groups", "Y=B, C=A, CVBS=C", &r3);
        settings_set(&c3, "Bits B", "Reversed", &r3);
        settings_set(&c3, "V sign", "Normal", &r3);
        settings_set(&c3, "Subcarrier trim", "-90 Hz", &r3);
        settings_set(&c3, "Burst end", "106", &r3);
        settings_set(&c3, "Chroma gain", "150 %", &r3);
        settings_set(&c3, "White level", "14/16", &r3);
        uint8_t want[0x60];
        memcpy(want, io3.regs, sizeof(want));
        OpIn nm = in_str("name", "tester1");
        CHECK_EQ(reg3.call(&c3, "pd.av_save", &nm, 1, &r3), OPE_OK);
        CHECK(fs3.has("/SD/cores/diag/presets/tester1.toml"));
        std::string pt = fs3.text("/SD/cores/diag/presets/tester1.toml");
        CHECK(pt.find("Standard = \"NTSC 60 Hz\"") != std::string::npos);
        CHECK(pt.find("Subcarrier trim = \"-90 Hz\"") != std::string::npos);
        OpIn bad = in_str("name", "a/b");
        CHECK_EQ(reg3.call(&c3, "pd.av_save", &bad, 1, &r3), OPE_ARG);
        // restart 1: the page's own file keeps the values
        {
            Settings again(d.settings, d.nsettings, d.store, &fs3, "/SD/cores/diag.cfg", 0);
            again.load();
            CHECK_EQ(again.value(again.find("Standard")), 1);
            CHECK_EQ(again.value(again.find("Burst end")), 8);
        }
        // restart 2: a fresh gateware (reset values) and fresh page, then the preset
        FakeIo io4;
        CoreCtx c4;
        memset(&c4, 0, sizeof(c4));
        c4.desc = &d;
        c4.io = &io4;
        c4.fs = &fs3;
        OpRegistry reg4;
        d.register_ops(reg4);
        settings_register(reg4);
        c4.ops = &reg4;
        fs3.remove("/SD/cores/diag.cfg");
        Settings st4(d.settings, d.nsettings, d.store, &fs3, "/SD/cores/diag.cfg", 0);
        st4.load();
        c4.settings = &st4;
        CHECK_EQ(io4.regs[PD_AV_STD], 0);
        OpIn pth = in_str("path", "/SD/cores/diag/presets/tester1.toml");
        CHECK_EQ(reg4.call(&c4, "pd.av_load", &pth, 1, &r3), OPE_OK);
        CHECK(!memcmp(io4.regs + PD_AV_STD, want + PD_AV_STD, 0x60 - PD_AV_STD));
        CHECK_EQ(io4.vfmt, 0x2B);
        OpIn notp = in_str("path", "/SD/cores/diag/tests/pins.toml");
        fs3.put_text("/SD/cores/diag/tests/pins.toml", "kind = \"probe\"\n");
        CHECK_EQ(reg4.call(&c4, "pd.av_load", &notp, 1, &r3), OPE_ARG);       // a test file is not a preset
        fs3.read_only = true;
        CHECK_EQ(reg4.call(&c4, "pd.av_save", &nm, 1, &r3), OPE_FAILED);       // a read only card says so
        fs3.read_only = false;
    }
    DONE("test_portdiag");
}
