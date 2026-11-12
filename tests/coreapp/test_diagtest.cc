// T175: the test files of the diag core (coreapp/src/diagtest.cc) against a model of the probe engine behind CoreIo. What is proven: the TOML schema
// (the files that ship parse; every kind of mistake is refused with its line), the step list (exact words, microseconds to clocks), the FIFO decode,
// the thresholds, both result files byte for byte, the card that refuses a write (no half result), the refusal of a test whose precondition is not
// met, the suite, the examples written once. The model has the register map of section 3 and the step-word fields of cores/portdiag_regs.h.
#include <algorithm>
#include <functional>
#include <fstream>
#include <sstream>
#include "check.h"
#include "fakes.h"
#include "diagtest.h"
#include "portdiag_regs.h"

#include "diag_engine.h"

struct Ui : DiagUi {
    std::function<int(int)> answer;                    // by prompt index
    std::vector<std::string> prompts, shown, progress_;
    std::function<int(int, int *)> cans;               // a question with a choice of answers: idx, the index picked -> DUI_*; without it the default (YES = the first)
    int choice(const char *t, const char *text, int idx, int total, int timeout_s, const char *const *items, int n, int *pick) override
    {
        if (!cans) {
            return DiagUi::choice(t, text, idx, total, timeout_s, items, n, pick);
        }
        prompts.push_back(text);
        return cans(idx, pick);
    }
    int prompt(const char *, const char *text, int idx, int, int) override
    {
        prompts.push_back(text);
        return answer ? answer(idx) : DUI_CONTINUE;
    }
    void progress(const char *t) override { progress_.push_back(t); }
    void show(const char *, const char *t) override { shown.push_back(t); }
};

static uint32_t g_ms;
static uint32_t now_ms() { return g_ms; }
static Engine *g_eng;
static void delay_ms(uint32_t n) { g_ms += n; if (g_eng) g_eng->settle(); }
static bool stamp_fixed(char out[16]) { strcpy(out, "20261102-143015"); return true; }
static bool stamp_none(char[16]) { return false; }

struct Rig {
    Engine eng;
    FakeFs fs;
    Ui ui;
    DiagEnv env;
    Rig(bool rtc = true)
    {
        memset(&env, 0, sizeof(env));
        env.io = &eng;
        g_eng = &eng;
        env.fs = &fs;
        env.ui = &ui;
        env.now_ms = now_ms;
        env.delay_ms = delay_ms;
        env.stamp = rtc ? stamp_fixed : stamp_none;
        env.core_version = "0.1.0";
        env.folder = "/SD/cores/diag";
        g_ms = 0;
    }
    DiagOutcome out;
    int run(const char *name) { g_eng = &eng; return diag_run_file(&env, (std::string("/SD/cores/diag/tests/") + name).c_str(), &out); }
};

static std::string slurp(const std::string &rel)
{
    std::ifstream f(std::string(COREAPP_DIR) + "/../" + rel);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

static bool parse(const std::string &t, DiagFile *f, DtErr *e) { return dt_parse(t.data(), (uint32_t)t.size(), f, e) == 0; }

static DiagFile g_f;

static std::string err_of(const std::string &text, int *line)
{
    DtErr e;
    if (parse(text, &g_f, &e)) { *line = -2; return "parsed"; }
    *line = e.line;
    return e.msg;
}

// a small valid test used by several checks below
static const char *SMALL =
    "[test]\nname = \"Small\"\nid = \"small\"\nversion = 3\nneeds = [\"stick\"]\n"
    "[[prompt]]\nid = \"p\"\ntext = \"Hold fire\"\ntimeout_s = 5\n"
    "[[step]]\nop = \"drive\"\npins = [\"p2_fire\"]\nlevel = \"low\"\n"
    "[[step]]\nop = \"wait\"\nus = 20\n"
    "[[step]]\nop = \"sample\"\ninto = \"s1\"\n"
    "[[step]]\nop = \"drive\"\npins = [\"p2_fire\"]\nlevel = \"release\"\n"
    "[[step]]\nop = \"wait_user\"\nprompt = \"p\"\n"
    "[[step]]\nop = \"time_to_flip\"\ncmp = [0, 2]\ntimeout_us = 400\ninto = \"t\"\n"
    "[[eval]]\nid = \"fire_low\"\nvalue = \"s1.pins\"\nmask = 512\neq = 0\nverdict = \"fire follows\"\nfail = \"fire stays high\"\n"
    "[[eval]]\nid = \"fast\"\nvalue = \"t\"\nmin_us = 5\nmax_us = 100\nverdict = \"flips in time\"\n"
    "[result]\nformat = \"toml\"\nkeys = [\"s1\", \"t\"]\n";

static void test_parse_shipped()
{
    const char *names[] = { "pins", "paddle", "stick2", "cd32", "joystick", "cd32keys" };
    int steps[] = { 53, 0, 0, 0, 0, 0 };
    (void)steps;
    for (const char *n : names) {
        std::string text = slurp(std::string("cores/diag/sd/tests/") + n + ".toml");
        CHECK(!text.empty());
        DtErr e;
        bool ok = parse(text, &g_f, &e);
        if (!ok) printf("  %s.toml line %d: %s\n", n, e.line, e.msg);
        CHECK(ok);
    }
    DtErr e;
    parse(slurp("cores/diag/sd/tests/pins.toml"), &g_f, &e);
    CHECK_EQ(g_f.kind, DT_KIND_TEST);
    CHECK_STR(g_f.test.id, "pins");
    CHECK_EQ(g_f.test.nevals, 31);
    CHECK_EQ(g_f.test.nslots, 12);
    parse(slurp("cores/diag/sd/tests/stick2.toml"), &g_f, &e);
    CHECK_EQ(g_f.test.nprompts, 4);
    CHECK_EQ(g_f.test.prompts[1].timeout_s, 300);
    parse(slurp("cores/diag/sd/tests/cd32.toml"), &g_f, &e);
    CHECK_EQ(g_f.test.nrequires, 1);
    CHECK_STR(g_f.test.requires[0], "pins.drivable");
    parse(slurp("cores/diag/sd/tests/joystick.toml"), &g_f, &e);
    CHECK_EQ(g_f.kind, DT_KIND_SUITE);
    CHECK_EQ(g_f.suite.ntests, 2);
    CHECK_STR(g_f.suite.tests[1], "stick2.toml");
    // the example tests the app carries are the files of the repository
    Rig r;
    CHECK_EQ(diag_install_examples(&r.fs, "/SD/cores/diag"), 7);
    for (const char *n : names) {
        CHECK_STR(r.fs.text(std::string("/SD/cores/diag/tests/") + n + ".toml").c_str(), slurp(std::string("cores/diag/sd/tests/") + n + ".toml").c_str());
    }
}

static void test_parse_errors()
{
    int line;
    // control: the valid file parses
    CHECK_STR(err_of(SMALL, &line).c_str(), "parsed");
    std::string base = SMALL;
    auto bad = [&](const std::string &from, const std::string &to, int want_line, const char *want_msg) {
        std::string t = base;
        size_t p = t.find(from);
        CHECK(p != std::string::npos);
        t.replace(p, from.size(), to);
        std::string m = err_of(t, &line);
        bool ok = line == want_line && m.find(want_msg) != std::string::npos;
        if (!ok) printf("  wanted line %d '%s', got line %d '%s'\n", want_line, want_msg, line, m.c_str());
        CHECK(ok);
    };
    // line numbers: [test] 1, name 2, id 3, version 4, needs 5, [[prompt]] 6 .. id 7, text 8, timeout 9, [[step]] 10 op 11 pins 12 level 13
    bad("op = \"drive\"", "op = \"jump\"", 11, "no step 'jump'");
    bad("level = \"low\"", "levl = \"low\"", 13, "does not belong to the step 'drive'");
    bad("version = 3", "versio = 3", 4, "unknown key 'versio' in [test]");
    bad("[test]", "[tset]", 1, "unknown section [tset]");
    bad("[[prompt]]", "[prompt]", 6, "unknown section");
    bad("level = \"low\"", "level = \"high\"", 10, "never drives a pin high");
    bad("pins = [\"p2_fire\"]\nlevel = \"low\"", "pins = [\"p2_fyre\"]\nlevel = \"low\"", 12, "'p2_fyre' is not a pin name");
    bad("us = 20", "us = twenty", 16, "neither a string");
    bad("timeout_s = 5", "timeout_s = 99999", 9, "0 .. 3600");
    bad("prompt = \"p\"", "prompt = \"q\"", 24, "no [[prompt]] before it defines");
    bad("into = \"s1\"", "into = \"t\"", 27, "used by two steps");
    bad("value = \"s1.pins\"", "value = \"s9.pins\"", 32, "which no step stores into");
    bad("value = \"s1.pins\"", "value = \"s1.pinz\"", 32, "fields are pins, cmp, ts and flags");
    bad("min_us = 5\nmax_us = 100", "min_us = 5\nmax_us = 100\nbogus = 1", 44, "unknown key 'bogus'");
    bad("verdict = \"flips in time\"", "verdict2 = \"x\"", 44, "unknown key");
    bad("keys = [\"s1\", \"t\"]", "keys = [\"s1\", \"nope\"]", 0, "no step stores into");
    bad("pins = [\"p2_fire\"]\nlevel = \"low\"", "pins = [\"p2_fire\"\nlevel = \"low\"", 12, "closed with ]");
    bad("name = \"Small\"", "name = \"Small", 2, "not closed");
    bad("cmp = [0, 2]", "cmp = [0, 7]", 29, "0 .. 3");
    // a time slot is compared in microseconds only if a time_to_flip made it
    bad("value = \"t\"\nmin_us = 5", "value = \"s1.pins\"\nmin_us = 5", 39, "are for a time_to_flip slot");
    // files that are not tests
    CHECK_STR(err_of("", &line).c_str(), "[test] needs an id");
    CHECK_STR(err_of("[test]\nid = \"x\"\n", &line).c_str(), "a test has no [[step]]");
    CHECK_STR(err_of("kind = \"mix\"\n", &line).c_str(), "kind must be \"test\" or \"suite\"");
    CHECK_EQ(line, 1);
    CHECK_STR(err_of("kind = \"suite\"\n[suite]\nid = \"x\"\n", &line).c_str(), "a suite needs [suite] with id and tests = [..]");
    CHECK_STR(err_of("kind = \"suite\"\n[suite]\nid = \"x\"\ntests = [\"a/b.toml\"]\n", &line).c_str(), "tests lists file names of the same folder (at most 16, no path)");
    // a loop that runs off the end, and a loop in a loop
    CHECK_STR(err_of("[test]\nid=\"a\"\n[[step]]\nop=\"loop\"\ncount=2\nbody=3\n[[step]]\nop=\"sample\"\n", &line).c_str(), "the loop wants 3 steps after it, the file has fewer");
    CHECK_STR(err_of("[test]\nid=\"a\"\n[[step]]\nop=\"loop\"\ncount=2\nbody=2\n[[step]]\nop=\"loop\"\ncount=2\nbody=1\n[[step]]\nop=\"sample\"\n", &line).c_str(), "a loop body holds no loop, wait_user or end");
    // 256 steps do not fit the step RAM
    std::string many = "[test]\nid=\"a\"\n";
    for (int i = 0; i < 256; i++) many += "[[step]]\nop=\"sample\"\n";
    CHECK_STR(err_of(many, &line).c_str(), "more than 255 steps (the step RAM holds 256 words)");
    // nothing is run for a refused file
    Rig r;
    r.fs.put_text("/SD/cores/diag/tests/bad.toml", "[test]\nid = \"b\"\n[[step]]\nop = \"jump\"\n");
    CHECK_EQ(r.run("bad.toml"), -1);
    CHECK_STR(r.out.msg, "bad.toml line 4: the engine has no step 'jump' (end, drive, wait, sample, edges, time_to_flip, padctl, loop, wait_user)");
    CHECK_EQ(r.eng.run_count, 0);
    CHECK_EQ(r.eng.wptr, 0);
    CHECK(!r.fs.has("/SD/cores/diag/results/b_20261102-143015.toml"));
}

static void test_compile()
{
    DtErr e;
    CHECK(parse(SMALL, &g_f, &e));
    DtProgram p;
    // 100 ticks per microsecond: 20 us = 2000 ticks, 400 us = 40000
    CHECK_EQ(dt_compile(g_f.test, 25600, &p, &e), 0);
    CHECK_EQ(p.n, 7);
    CHECK_EQ(p.words[0], 0x10000200u);                 // DRIVE, low (level bits 0), pin 9 (p2_fire)
    CHECK_EQ(p.words[1], 0x200007D0u);                 // WAIT 2000
    CHECK_EQ(p.words[2], 0x30000000u);                 // SAMPLE
    CHECK_EQ(p.words[3], 0x10080200u);                 // DRIVE, release: the level bit of pin 9 is bit 19
    CHECK_EQ(p.words[4], 0x80000000u);                 // WAIT_USER prompt 0
    CHECK_EQ(p.words[5], 0x55009C40u);           // TIME_TO_FLIP, comparators 0 and 2 (mask 5), 40000
    CHECK_EQ(p.words[6], 0x00000000u);                 // END
    CHECK_EQ(p.timeout_ticks[1], 40000u);
    // a clock of 12.5 ticks per microsecond rounds a time up, never down
    CHECK_EQ(dt_compile(g_f.test, 3200, &p, &e), 0);
    CHECK_EQ(p.words[1], 0x200000FAu);                 // 20 us * 12.5 = 250
    CHECK_EQ(dt_compile(g_f.test, 3210, &p, &e), 0);   // 12.539 ticks per us: 250.78 -> 251
    CHECK_EQ(p.words[1] & 0xFFFFFF, 251u);
    // a clock the engine cannot count in 24 bits
    CHECK_EQ(dt_compile(g_f.test, 0, &p, &e), -1);
    std::string t = SMALL;
    t.replace(t.find("us = 20"), 7, "us = 20000000");
    CHECK(parse(t, &g_f, &e) == false);                 // refused by the parser: 24 bits
    // S2: a changed threshold changes the verdict, not the step list or the engine's words
    CHECK(parse(SMALL, &g_f, &e));
    DiagFile a = g_f;
    DtProgram pa, pb;
    t = SMALL;
    t.replace(t.find("max_us = 100"), 12, "max_us = 3");
    DiagFile *b = new DiagFile;
    CHECK(parse(t, b, &e));
    CHECK_EQ(dt_compile(a.test, 25600, &pa, &e), 0);
    CHECK_EQ(dt_compile(b->test, 25600, &pb, &e), 0);
    CHECK(!memcmp(&pa, &pb, sizeof(pa)));
    delete b;
}

// the step words of PADCTL are the engine's: b0 DIS level, b1 DIS enable, b2 ADJ level, b3 ADJ enable (cores/diag/rtl/probe.v op 6)
static void test_padctl_words()
{
    DtErr e;
    DtProgram p;
    auto word = [&](const char *args) {
        std::string t = std::string("[test]\nid=\"p\"\n[[step]]\nop=\"padctl\"\n") + args;
        CHECK(parse(t, &g_f, &e));
        CHECK_EQ(dt_compile(g_f.test, 25600, &p, &e), 0);
        return p.words[0];
    };
    CHECK_EQ(word("dis=1\nadj=0\n"), 0x6000000Bu);     // DIS high and driven, ADJ low and driven
    CHECK_EQ(word("dis=0\n"), 0x60000002u);             // DIS low and driven, ADJ left alone (released)
    CHECK_EQ(word("adj=1\n"), 0x6000000Cu);             // ADJ high and driven, DIS released
    CHECK_EQ(word("dis=1\nadj=1\n"), 0x6000000Fu);
}

// STATUS shows the DONE of the run before for a few clocks after the RUN write (ABORT sets DONE, only RUN clears it): the runner waits before it polls
static void test_stale_done()
{
    Rig r;
    r.eng.lag_on = true;
    r.fs.put_text("/SD/cores/diag/tests/pins.toml", slurp("cores/diag/sd/tests/pins.toml"));
    CHECK_EQ(r.run("pins.toml"), 0);
    CHECK_EQ(r.out.nerror, 0);
    CHECK_EQ(r.run("pins.toml"), 0);                   // the second run starts behind a DONE: it must not end at once with an empty FIFO
    CHECK_EQ(r.out.nerror, 0);
    std::string t = r.fs.text("/SD/cores/diag/results/pins_20261102-143015.toml");
    CHECK(t.find("status = \"done\"") != std::string::npos);
    CHECK_EQ(r.eng.driven, 0u);
}

static void test_plan_loop()
{
    DtErr e;
    const char *t =
        "[test]\nid=\"l\"\n[[step]]\nop=\"loop\"\ncount=3\nbody=3\n[[step]]\nop=\"drive\"\npins=[\"p2_fire\"]\n[[step]]\nop=\"sample\"\ninto=\"bits\"\n"
        "[[step]]\nop=\"drive\"\npins=[\"p2_fire\"]\nlevel=\"release\"\n[[step]]\nop=\"edges\"\npin=\"p1_up\"\nclocks=50\ninto=\"e\"\n";
    CHECK(parse(t, &g_f, &e));
    int8_t slot_of[512];
    CHECK_EQ(dt_plan(g_f.test, slot_of), 4);
    CHECK_EQ(slot_of[0], 0);
    CHECK_EQ(slot_of[1], 0);
    CHECK_EQ(slot_of[2], 0);
    CHECK_EQ(slot_of[3], 1);
    DtProgram p;
    CHECK_EQ(dt_compile(g_f.test, 25600, &p, &e), 0);
    CHECK_EQ(p.words[0], 0x70000303u);                 // LOOP: 3 repeats of the next 3 words
    CHECK_EQ(p.words[4], 0x90000001u);                 // MASK: pin 0 (p1_up)
    CHECK_EQ(p.words[5], 0x40000032u);                 // EDGES, 50 clocks
    // 513 words do not fit the FIFO
    std::string big = "[test]\nid=\"b\"\n[[step]]\nop=\"loop\"\ncount=300\nbody=1\n[[step]]\nop=\"sample\"\n[[step]]\nop=\"loop\"\ncount=300\nbody=1\n[[step]]\nop=\"sample\"\n";
    CHECK(parse(big, &g_f, &e));
    CHECK_EQ(dt_plan(g_f.test, slot_of), -1);
    // the engine, a loop in it: the FIFO holds three samples and the edges count, in plan order
    Rig r;
    r.fs.put_text("/SD/cores/diag/tests/l.toml", t);
    CHECK_EQ(r.run("l.toml"), 0);
    CHECK_EQ(r.out.nerror, 0);
    std::string toml = r.fs.text("/SD/cores/diag/results/l_20261102-143015.toml");
    CHECK(toml.find("status = \"done\"") != std::string::npos);
    CHECK(toml.find("[slot.bits]\nkind = \"sample\"\nvalues = [") != std::string::npos);
    CHECK(toml.find("\nfifo = [") != std::string::npos);
    CHECK_EQ(r.eng.driven, 0u);
}

// the engine run of SMALL: fire follows, the comparators flip after 12.5 us
static const char *EXP_TOML =
    "# TRX64U Port Diagnostics result (T175). Raw words are the engine's FIFO words in order; see the spec for their layout.\n"
    "[meta]\ntest = \"small\"\nname = \"Small\"\nversion = 3\ncore = \"0.1.0\"\ndate = \"20261102-143015\"\nticks_per_us_x256 = 25600\nstatus = \"done\"\n"
    "\n[prompt]\np = \"OK\"\n"
    "\n[slot.s1]\nkind = \"sample\"\nvalues = [131104704]\nts = [2000]\npins = [511]\ncmp = [0]\n"
    "\n[slot.t]\nkind = \"time\"\nvalues = [1250]\n"
    "\n[raw]\nfifo = [131104704, 1250]\n"
    "\n[eval]\nfire_low = \"PASS: fire follows\"\nfast = \"PASS: flips in time\"\n";
static const char *EXP_TXT =
    "TRX64U Port Diagnostics\n=======================\n"
    "Test:    Small (small, version 3)\nCore:    0.1.0\nDate:    2026-11-02 14:30:15\nClock:   100.00 ticks per microsecond\nNeeds:   stick\nStatus:  done\n"
    "\nPrompts\n  p        OK        Hold fire\n"
    "\nMeasurements\n  s1[0]          sample  t=2000  P1 U1 D1 L1 R1 F1  P2 U1 D1 L1 R1 F0  CMP 0 0 0 0\n  t[0]           time    1250 ticks = 12.50 us\n"
    "\nVerdicts\n  PASS   fire follows\n  PASS   flips in time\n\n2 passed, 0 not passed\n";

static void test_run_files()
{
    Rig r;
    r.fs.put_text("/SD/cores/diag/tests/small.toml", SMALL);
    r.eng.charge[2] = 1250;
    CHECK_EQ(r.run("small.toml"), 0);
    CHECK_EQ(r.out.ntests, 1);
    CHECK_EQ(r.out.npass, 1);
    CHECK_STR(r.fs.text("/SD/cores/diag/results/small_20261102-143015.toml").c_str(), EXP_TOML);
    CHECK_STR(r.fs.text("/SD/cores/diag/results/small_20261102-143015.txt").c_str(), EXP_TXT);
    CHECK_STR(r.out.last_toml, "/SD/cores/diag/results/small_20261102-143015.toml");
    // the engine was loaded with the compiled list, run once, aborted at the end (every output released)
    CHECK_EQ(r.eng.run_count, 1);
    CHECK_EQ(r.eng.abort_writes, 2);
    CHECK_EQ(r.eng.driven, 0u);
    CHECK_EQ(r.ui.prompts.size(), 1u);
    CHECK_STR(r.ui.prompts[0].c_str(), "Hold fire");
    CHECK_EQ(r.ui.shown.size(), 1u);
    CHECK(r.ui.shown[0].find("2 passed, 0 not passed") != std::string::npos);
    CHECK(r.ui.shown[0].find("Report: results/small_20261102-143015.txt") != std::string::npos);
    // the same run with the thresholds of the file changed: the verdict follows, the words loaded do not
    std::vector<uint32_t> w1(r.eng.steps, r.eng.steps);
    Rig r2;
    std::string t = SMALL;
    t.replace(t.find("max_us = 100"), 12, "max_us = 10");
    r2.fs.put_text("/SD/cores/diag/tests/small.toml", t);
    r2.eng.charge[2] = 1250;
    CHECK_EQ(r2.run("small.toml"), 0);
    CHECK_EQ(r2.out.nfail, 1);
    std::string x = r2.fs.text("/SD/cores/diag/results/small_20261102-143015.toml");
    CHECK(x.find("fast = \"FAIL: flips in time - not met\"") != std::string::npos);
    CHECK(!memcmp(r.eng.steps, r2.eng.steps, sizeof(r.eng.steps)));
    // no flip within the timeout is no number
    Rig r3;
    r3.fs.put_text("/SD/cores/diag/tests/small.toml", SMALL);
    CHECK_EQ(r3.run("small.toml"), 0);
    std::string y = r3.fs.text("/SD/cores/diag/results/small_20261102-143015.toml");
    CHECK(y.find("fast = \"NOFLIP: no flip within the timeout\"") != std::string::npos);
    std::string yt = r3.fs.text("/SD/cores/diag/results/small_20261102-143015.txt");
    CHECK(yt.find("t[0]           time    no flip within 40000 ticks") != std::string::npos);
    // a pin that cannot be driven: fire stays high, the first eval fails with its own text
    Rig r4;
    r4.fs.put_text("/SD/cores/diag/tests/small.toml", SMALL);
    r4.eng.drivable = 0;
    r4.eng.charge[0] = 500;
    CHECK_EQ(r4.run("small.toml"), 0);
    std::string z = r4.fs.text("/SD/cores/diag/results/small_20261102-143015.toml");
    CHECK(z.find("fire_low = \"FAIL: fire stays high\"") != std::string::npos);
    // no clock: a counter names the file
    Rig r5(false);
    r5.fs.put_text("/SD/cores/diag/tests/small.toml", SMALL);
    CHECK_EQ(r5.run("small.toml"), 0);
    CHECK(r5.fs.has("/SD/cores/diag/results/small_n000001.toml"));
    CHECK(r5.fs.text("/SD/cores/diag/results/small_n000001.txt").find("Date:    n000001 (no clock)") != std::string::npos);
}

static void test_card()
{
    // S7 control: the card refuses writes - a visible reason, no half file
    Rig r;
    r.fs.put_text("/SD/cores/diag/tests/small.toml", SMALL);
    r.fs.read_only = true;
    CHECK_EQ(r.run("small.toml"), -1);
    CHECK(strstr(r.out.msg, "cannot write /SD/cores/diag/results/small_20261102-143015.toml") != 0);
    CHECK(!r.fs.has("/SD/cores/diag/results/small_20261102-143015.toml"));
    CHECK(!r.fs.has("/SD/cores/diag/results/small_20261102-143015.txt"));
    // S7 sweep: the card gives out after k writes, for every k. Either both files are there and the run said 0, or neither is and a reason is shown
    int failed_runs = 0, good_runs = 0;
    for (int k = 0; k < 8; k++) {
        Rig q;
        q.fs.put_text("/SD/cores/diag/tests/small.toml", SMALL);
        q.fs.mut_budget = k;
        int rc = q.run("small.toml");
        bool a = q.fs.has("/SD/cores/diag/results/small_20261102-143015.toml"), b = q.fs.has("/SD/cores/diag/results/small_20261102-143015.txt");
        if (rc == 0) { CHECK(a && b); good_runs++; }
        else {
            CHECK(!b);
            CHECK(strstr(q.out.msg, "cannot write") != 0);
            CHECK(!a || strstr(q.out.msg, "stays: the card refuses changes") != 0);      // a leftover is only possible when the delete was refused too, and then it is said
            failed_runs++;
        }
    }
    CHECK(failed_runs >= 2 && good_runs >= 1);
    // a test file that is not there
    Rig m;
    CHECK_EQ(m.run("nothing.toml"), -1);
    CHECK(strstr(m.out.msg, "cannot read") != 0);
}

static void test_prompts()
{
    // a prompt the person skips or lets run out is marked, the run goes on and the eval that needed it fails on its own
    Rig r;
    r.fs.put_text("/SD/cores/diag/tests/small.toml", SMALL);
    r.ui.answer = [](int) { return DUI_TIMEOUT; };
    r.eng.charge[0] = 500;
    CHECK_EQ(r.run("small.toml"), 0);
    CHECK(r.fs.text("/SD/cores/diag/results/small_20261102-143015.toml").find("p = \"MISSED\"") != std::string::npos);
    Rig s;
    s.fs.put_text("/SD/cores/diag/tests/small.toml", SMALL);
    s.ui.answer = [](int) { return DUI_SKIP; };
    CHECK_EQ(s.run("small.toml"), 0);
    CHECK(s.fs.text("/SD/cores/diag/results/small_20261102-143015.toml").find("p = \"SKIPPED\"") != std::string::npos);
    // abort at the prompt: no verdicts, the outputs released, the files say so
    Rig a;
    a.fs.put_text("/SD/cores/diag/tests/small.toml", SMALL);
    a.ui.answer = [](int) { return DUI_ABORT; };
    CHECK_EQ(a.run("small.toml"), 0);
    std::string x = a.fs.text("/SD/cores/diag/results/small_20261102-143015.toml");
    CHECK(x.find("status = \"aborted\"") != std::string::npos);
    CHECK(x.find("[eval]") == std::string::npos);
    CHECK_EQ(a.eng.driven, 0u);
    CHECK_EQ(a.out.nerror, 1);
    // the person's answer reaches the machine between the prompts: stick2 with a button 2 on pin 9's comparator
    Rig k;
    k.fs.put_text("/SD/cores/diag/tests/stick2.toml", slurp("cores/diag/sd/tests/stick2.toml"));
    k.ui.answer = [&k](int idx) {
        k.eng.forced_low = idx == 2 ? 0x200 : 0;           // the prompts are numbered from 1: the second is button 1, pin 6 low
        k.eng.cmp_now = idx == 3 ? 4 : 0;                  // the third is button 2: comparator 2
        return DUI_CONTINUE;
    };
    CHECK_EQ(k.run("stick2.toml"), 0);
    std::string t = k.fs.text("/SD/cores/diag/results/stick2_20261102-143015.toml");
    CHECK(t.find("idle_high = \"PASS: port 2 lines idle high\"") != std::string::npos);
    CHECK(t.find("b1_pin6 = \"PASS: button 1 pulls pin 6 low\"") != std::string::npos);
    CHECK(t.find("b2_moves = \"PASS: button 2 is seen") != std::string::npos);      // an Amiga stick: the second button is on a pot line, seen by a comparator
    CHECK(t.find("rel_pins = \"PASS") != std::string::npos);
    CHECK(k.fs.text("/SD/cores/diag/results/stick2_20261102-143015.txt").find("b2[0]          sample  t=0  P1 U1 D1 L1 R1 F1  P2 U1 D1 L1 R1 F1  CMP 0 0 1 0") != std::string::npos);
    // the same stick, nothing pressed at the second prompt: button 1 is not seen
    Rig n;
    n.fs.put_text("/SD/cores/diag/tests/stick2.toml", slurp("cores/diag/sd/tests/stick2.toml"));
    CHECK_EQ(n.run("stick2.toml"), 0);
    CHECK(n.fs.text("/SD/cores/diag/results/stick2_20261102-143015.toml").find("b1_pin6 = \"FAIL") != std::string::npos);
    CHECK(n.fs.text("/SD/cores/diag/results/stick2_20261102-143015.toml").find("b2_moves = \"FAIL: button 2 moves neither") != std::string::npos);   // control: nothing moves, so it fails
    // button 2 on a port pin alone passes as well
    Rig m;
    m.fs.put_text("/SD/cores/diag/tests/stick2.toml", slurp("cores/diag/sd/tests/stick2.toml"));
    m.ui.answer = [&m](int idx) { m.eng.forced_low = idx == 3 ? 0x020 : 0; return DUI_CONTINUE; };
    CHECK_EQ(m.run("stick2.toml"), 0);
    CHECK(m.fs.text("/SD/cores/diag/results/stick2_20261102-143015.toml").find("b2_moves = \"PASS") != std::string::npos);
}

// A pad behind the pot levers (T175 cd32 v2). The model: with the lever setting `works` (a PADCTL argument) the pad shifts, pin 5 reads low and pin 9 carries
// blue, red (0 while held), five buttons, the ID 1, then 0; with any other setting nothing shifts and the comparators show `idle`.
struct PadModel {
    int works = -1;                                    // the PADCTL argument that lets the pad shift, -1: none
    bool red = false;
    uint32_t idle = 0xF;
    uint32_t operator()(const Engine &e) const
    {
        if (works < 0 || (int)e.padctl != works) return idle;
        static const int bits[10] = { 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 };    // read 0 blue, read 1 red, ..., read 7 the ID, then zeros
        int pos = e.rises < 10 ? e.rises : 9;
        int b = pos == 1 && red ? 0 : bits[pos];
        return (uint32_t)(b << 3) | 3;                                    // cmp2 (pin 5) low, cmp3 = the data
    }
};

static std::string run_cd32(PadModel *pm)
{
    Rig r;
    r.fs.put_text("/SD/cores/diag/tests/pins.toml", slurp("cores/diag/sd/tests/pins.toml"));
    r.fs.put_text("/SD/cores/diag/tests/cd32.toml", slurp("cores/diag/sd/tests/cd32.toml"));
    CHECK_EQ(r.run("pins.toml"), 0);
    r.eng.cmp_fn = [pm](const Engine &e) { return (*pm)(e); };
    r.ui.answer = [pm](int idx) { pm->red = idx == 2; return DUI_CONTINUE; };       // the second question is the red button held; the first and the third are not
    CHECK_EQ(r.run("cd32.toml"), 0);
    CHECK_EQ(r.ui.prompts.size(), 3u);
    return r.fs.text("/SD/cores/diag/results/cd32_20261102-143015.toml");
}

static void test_cd32_v2()
{
    // CONTROL: the tester's board (discussion 30): every sample equal, CMP 0 0 1 0, whatever is held and whichever lever. v1 passed this; v2 must fail every setting
    PadModel same;
    same.idle = 4;
    std::string x = run_cd32(&same);
    const char *lev[] = { "d0", "d1", "d0a0", "d0a1", "d1a0", "d1a1" };
    for (const char *l : lev) {
        CHECK(x.find(std::string(l) + "_ok = \"FAIL") != std::string::npos);
        CHECK(x.find(std::string(l) + "_reads = \"FAIL") != std::string::npos);
        CHECK(x.find(std::string(l) + "_ok = \"PASS") == std::string::npos);
    }
    CHECK(x.find("status = \"done\"") != std::string::npos);
    CHECK(x.find("[slot.n_d1]") != std::string::npos);                                // the raw samples are in the file even when every eval fails
    // a pad that shifts under DIS high alone (PADCTL arg 3): only d1 works, red held changes read 1, pin 5 is low, the ID bit follows
    PadModel good;
    good.works = 3;
    std::string y = run_cd32(&good);
    CHECK(y.find("d1_ok = \"PASS: WORKS: d1 (DIS high, ADJ released) reads the CD32 pad\"") != std::string::npos);
    CHECK(y.find("d1_reads = \"PASS") != std::string::npos);
    CHECK(y.find("d1_pin5 = \"PASS") != std::string::npos);
    CHECK(y.find("d1_id = \"PASS") != std::string::npos);
    for (const char *l : lev) {
        if (strcmp(l, "d1")) {
            CHECK(y.find(std::string(l) + "_ok = \"FAIL") != std::string::npos);     // the other settings do not
            CHECK(y.find(std::string(l) + "_pin5 = \"FAIL") != std::string::npos);
        }
    }
    // a line held low by a lever (pin 9 low in both runs) is not a pad either: idle fails, ok fails
    PadModel stuck;
    stuck.idle = 0x3;
    std::string z = run_cd32(&stuck);
    CHECK(z.find("d1_idle = \"FAIL") != std::string::npos);
    CHECK(z.find("d1_ok = \"FAIL") != std::string::npos);
    // the example writer replaces an unedited v1 file and leaves an edited one
    Rig w;
    w.fs.put_text("/SD/cores/diag/tests/cd32.toml", slurp("cores/diag/superseded/cd32.v1.toml"));
    w.fs.put_text("/SD/cores/diag/tests/stick2.toml", slurp("cores/diag/superseded/stick2.v1.toml") + "# mine\n");
    CHECK_EQ(diag_install_examples(&w.fs, "/SD/cores/diag"), 6);                       // cd32 replaced + pins, paddle, joystick, cd32keys, avout written; stick2 edited, kept
    CHECK_STR(w.fs.text("/SD/cores/diag/tests/cd32.toml").c_str(), slurp("cores/diag/sd/tests/cd32.toml").c_str());
    CHECK(w.fs.text("/SD/cores/diag/tests/stick2.toml").find("# mine") != std::string::npos);
    CHECK_EQ(diag_install_examples(&w.fs, "/SD/cores/diag"), 0);                       // the second start writes nothing
}

static void test_requires_suite()
{
    // S6 control: cd32 stops without a PASS of pins.drivable (no result at all, then a result that says no), runs with one
    const std::string cd32 = slurp("cores/diag/sd/tests/cd32.toml");
    Rig r;
    r.fs.put_text("/SD/cores/diag/tests/cd32.toml", cd32);
    CHECK_EQ(r.run("cd32.toml"), 0);
    CHECK_EQ(r.out.nrefused, 1);
    CHECK_EQ(r.eng.run_count, 0);
    CHECK(r.ui.prompts.empty());
    std::string x = r.fs.text("/SD/cores/diag/results/cd32_20261102-143015.toml");
    CHECK(x.find("status = \"refused\"") != std::string::npos);
    CHECK(x.find("run pins.toml first") != std::string::npos);
    CHECK(x.find("fifo = []") != std::string::npos);
    // the pins test on a board that drives nothing says drivable is not met; cd32 stops on it
    Rig p;
    p.fs.put_text("/SD/cores/diag/tests/pins.toml", slurp("cores/diag/sd/tests/pins.toml"));
    p.fs.put_text("/SD/cores/diag/tests/cd32.toml", cd32);
    p.eng.drivable = 0;
    CHECK_EQ(p.run("pins.toml"), 0);
    std::string pt = p.fs.text("/SD/cores/diag/results/pins_20261102-143015.toml");
    CHECK(pt.find("drivable = \"FAIL: the fire pins cannot both be driven") != std::string::npos);
    CHECK(pt.find("stuck_p1_up = \"PASS") != std::string::npos);
    CHECK(pt.find("ok_p1_up = \"FAIL: p1_up cannot be driven\"") != std::string::npos);
    CHECK_EQ(p.run("cd32.toml"), 0);
    CHECK_EQ(p.out.nrefused, 1);
    CHECK(p.fs.text("/SD/cores/diag/results/cd32_20261102-143015.toml").find("cannot run on this board: pins.drivable is not PASS in pins_20261102-143015.toml") != std::string::npos);
    // a pin that reads low by itself is "stuck", and is not "driven"
    Rig s;
    s.fs.put_text("/SD/cores/diag/tests/pins.toml", slurp("cores/diag/sd/tests/pins.toml"));
    s.eng.forced_low = 1u << 3;                         // p1_right
    CHECK_EQ(s.run("pins.toml"), 0);
    std::string st = s.fs.text("/SD/cores/diag/results/pins_20261102-143015.toml");
    CHECK(st.find("stuck_p1_right = \"FAIL: p1_right reads low without being driven (stuck)\"") != std::string::npos);
    CHECK(st.find("ok_p1_right = \"FAIL: p1_right cannot be driven\"") != std::string::npos);
    CHECK(st.find("ok_p1_up = \"PASS: p1_up can be driven\"") != std::string::npos);
    CHECK(st.find("drivable = \"PASS") != std::string::npos);
    // a board that drives everything: pins passes, and cd32 then runs: 8 clock pulses, 8 samples, the prompt shown
    Rig d;
    d.fs.put_text("/SD/cores/diag/tests/pins.toml", slurp("cores/diag/sd/tests/pins.toml"));
    d.fs.put_text("/SD/cores/diag/tests/cd32.toml", cd32);
    CHECK_EQ(d.run("pins.toml"), 0);
    CHECK(d.fs.text("/SD/cores/diag/results/pins_20261102-143015.toml").find("drivable = \"PASS") != std::string::npos);
    CHECK_EQ(d.run("cd32.toml"), 0);
    CHECK_EQ(d.out.nfail, 1);                            // a board that shows nothing reads no pad: the test fails (v1 passed on any data)
    CHECK_EQ(d.eng.run_count, 2);
    std::string ct = d.fs.text("/SD/cores/diag/results/cd32_20261102-143015.toml");
    CHECK(ct.find("[slot.n_d1]") != std::string::npos);
    CHECK(ct.find("d1_ok = \"FAIL") != std::string::npos);
    // the suite: both tests in a row, a pair of files each, and one for the suite
    Rig u;
    u.fs.put_text("/SD/cores/diag/tests/pins.toml", slurp("cores/diag/sd/tests/pins.toml"));
    u.fs.put_text("/SD/cores/diag/tests/stick2.toml", slurp("cores/diag/sd/tests/stick2.toml"));
    u.fs.put_text("/SD/cores/diag/tests/joystick.toml", slurp("cores/diag/sd/tests/joystick.toml"));
    CHECK_EQ(u.run("joystick.toml"), 0);
    CHECK_EQ(u.out.ntests, 2);
    CHECK(u.fs.has("/SD/cores/diag/results/pins_20261102-143015.toml"));
    CHECK(u.fs.has("/SD/cores/diag/results/pins_20261102-143015.txt"));
    CHECK(u.fs.has("/SD/cores/diag/results/stick2_20261102-143015.toml"));
    CHECK(u.fs.has("/SD/cores/diag/results/stick2_20261102-143015.txt"));
    CHECK(u.fs.has("/SD/cores/diag/results/joystick_20261102-143015.toml"));
    CHECK(u.fs.text("/SD/cores/diag/results/joystick_20261102-143015.txt").find("Suite Joystick ports\n") == 0);
    CHECK_EQ(u.ui.prompts.size(), 4u);                   // only stick2 asks
    CHECK_EQ(u.ui.shown.size(), 1u);                      // one result view for the suite
    // a suite that names a file that is not there goes on and says so
    Rig v;
    v.fs.put_text("/SD/cores/diag/tests/suite.toml", "kind = \"suite\"\n[suite]\nid = \"s\"\ntests = [\"gone.toml\"]\n");
    CHECK_EQ(v.run("suite.toml"), 0);
    CHECK_EQ(v.out.nerror, 1);
    CHECK(v.fs.text("/SD/cores/diag/results/s_20261102-143015.txt").find("not run  cannot read") != std::string::npos);
}

static void test_engine_errors()
{
    // the engine answers as something else: nothing is loaded
    Rig r;
    r.fs.put_text("/SD/cores/diag/tests/small.toml", SMALL);
    r.eng.tpus256 = 0;
    CHECK_EQ(r.run("small.toml"), 0);
    CHECK_EQ(r.out.nerror, 1);
    CHECK(r.fs.text("/SD/cores/diag/results/small_20261102-143015.toml").find("reports no clock") != std::string::npos);
    CHECK_EQ(r.eng.wptr, 0);
    // a FIFO that overflowed is an error, not a verdict
    Rig o;
    o.fs.put_text("/SD/cores/diag/tests/l.toml", "[test]\nid=\"l\"\n[[step]]\nop=\"loop\"\ncount=500\nbody=1\n[[step]]\nop=\"sample\"\ninto=\"x\"\n[[eval]]\nvalue=\"x\"\nmin=0\nverdict=\"v\"\n");
    CHECK_EQ(o.run("l.toml"), 0);
    CHECK_EQ(o.out.nerror, 0);                            // 500 fit
    Rig q;
    q.fs.put_text("/SD/cores/diag/tests/l.toml", "[test]\nid=\"l\"\n[[step]]\nop=\"loop\"\ncount=500\nbody=1\n[[step]]\nop=\"sample\"\ninto=\"x\"\n[[step]]\nop=\"loop\"\ncount=20\nbody=1\n[[step]]\nop=\"sample\"\n");
    CHECK_EQ(q.run("l.toml"), 0);
    CHECK(q.fs.text("/SD/cores/diag/results/l_20261102-143015.toml").find("more than 512 words") != std::string::npos);
    // the engine pushed a different count than the step list: refused, no verdict
    Rig w;
    w.fs.put_text("/SD/cores/diag/tests/small.toml", SMALL);
    w.eng.on_prompt = nullptr;
    w.ui.answer = [&w](int) { w.eng.fifo.push_back(7); return DUI_CONTINUE; };
    CHECK_EQ(w.run("small.toml"), 0);
    CHECK(w.fs.text("/SD/cores/diag/results/small_20261102-143015.toml").find("the engine pushed 3 words, the step list pushes 2") != std::string::npos);
}

static void test_examples()
{
    Rig r;
    CHECK_EQ(diag_install_examples(&r.fs, "/SD/cores/diag"), 7);
    CHECK(r.fs.has("/SD/cores/diag/tests/pins.toml"));
    // the person edits one; the next start leaves it alone and writes only what is missing
    r.fs.put_text("/SD/cores/diag/tests/pins.toml", "# mine\n");
    r.fs.remove("/SD/cores/diag/tests/cd32.toml");
    CHECK_EQ(diag_install_examples(&r.fs, "/SD/cores/diag"), 1);
    CHECK_STR(r.fs.text("/SD/cores/diag/tests/pins.toml").c_str(), "# mine\n");
    CHECK(r.fs.has("/SD/cores/diag/tests/cd32.toml"));
    // a card that cannot be written
    Rig ro;
    ro.fs.read_only = true;
    CHECK_EQ(diag_install_examples(&ro.fs, "/SD/cores/diag"), -1);
    // the live vector
    Rig l;
    l.eng.forced_low = 0x201;
    l.eng.cmp_now = 5;
    char pt[120];
    dt_pins_text(diag_live(&l.eng), pt, sizeof(pt));
    CHECK_STR(pt, "P1 U0 D1 L1 R1 F1  P2 U1 D1 L1 R1 F0  CMP 1 0 1 0");
}

// T201 S4, S9, S11: the shipped A/V walk (v2). Each step sets its combination, asks its questions, records YES / NO / MISSED / SKIPPED or the choice's label, the result files carry the combination; the page's values come back at the end.
static int g_avset[DT_AV_N], g_avcalls, g_avdone;
static std::vector<std::pair<int, int>> g_avlog;
static std::vector<int> g_avfrz;
static int av_get_t(void *, int k) { return dt_av_knobs[k].def; }
static void av_set_t(void *, int k, int idx) { g_avset[k] = idx; g_avlog.push_back({ k, idx }); g_avcalls++; }
static void av_done_t(void *) { g_avdone++; }
static void av_freeze_t(void *, bool on) { g_avfrz.push_back(on ? 1 : 0); }
static size_t count_of(const std::string &s, const char *needle)
{
    size_t n = 0, p = 0, l = strlen(needle);
    while ((p = s.find(needle, p)) != std::string::npos) {
        n++;
        p += l;
    }
    return n;
}
static void av_hooks(Rig &r)
{
    r.env.av_get = av_get_t;
    r.env.av_set = av_set_t;
    r.env.av_done = av_done_t;
    r.env.av_freeze = av_freeze_t;
}
static void test_avout()
{
    // S11: the shipped v2 file, 36 steps, both questions every time (no gate). Calls come in pairs: step 1: Q1 NO, Q2 other | step 2: Q1 YES, Q2 yellow (good) | step 3: Q1 timeout, Q2 blue
    // | step 4: Q1 YES, Q2 blue (still, wrong colour: listed, not good) | the rest NO, other
    Rig r;
    av_hooks(r);
    r.fs.put_text("/SD/cores/diag/tests/avout.toml", slurp("cores/diag/sd/tests/avout.toml"));
    int asks = 0;
    r.ui.answer = [&](int) {
        asks++;
        int step = (asks + 1) / 2;
        return step == 2 || step == 4 ? DUI_CONTINUE : step == 3 ? DUI_TIMEOUT : DUI_SKIP;
    };
    r.ui.cans = [&](int, int *pick) {
        asks++;
        int step = (asks + 1) / 2;
        *pick = step == 2 ? 0 : (step == 3 || step == 4) ? 1 : 2;
        return DUI_CONTINUE;
    };
    g_avlog.clear();
    g_avfrz.clear();
    g_avdone = 0;
    CHECK_EQ(r.run("avout.toml"), 0);
    CHECK_EQ(asks, 72);                                // 36 steps x 2 questions: nothing is skipped
    CHECK_EQ(g_avdone, 1);
    CHECK_EQ(g_avfrz.size(), (size_t)37);              // frozen before each of the 36 steps' questions, released once at the end
    CHECK_EQ(g_avfrz.front(), 1);
    CHECK_EQ(g_avfrz.back(), 0);
    CHECK_EQ(std::count(g_avfrz.begin(), g_avfrz.end(), 1), 36);
    // the first step sets the whole preset: the base values are what the walk applies, not the NTSC defaults of burst and blank
    int first[DT_AV_N];
    for (int k = 0; k < DT_AV_N; k++) first[k] = -1;
    for (auto &e : g_avlog) if (first[e.first] < 0) first[e.first] = e.second;
    CHECK_EQ(dt_av_knobs[AVK_STD].val[first[AVK_STD]], 1);
    CHECK_EQ(dt_av_knobs[AVK_GROUPS].val[first[AVK_GROUPS]], 0);
    CHECK_EQ(dt_av_knobs[AVK_BITS_A].val[first[AVK_BITS_A]], 0);
    CHECK_EQ(dt_av_knobs[AVK_BITS_B].val[first[AVK_BITS_B]], 0);
    CHECK_EQ(dt_av_knobs[AVK_BITS_C].val[first[AVK_BITS_C]], 1);
    CHECK_EQ(dt_av_knobs[AVK_VSIGN].val[first[AVK_VSIGN]], 0);
    CHECK_EQ(dt_av_knobs[AVK_TRIM].val[first[AVK_TRIM]], 2);
    CHECK_EQ(dt_av_knobs[AVK_BSTART].val[first[AVK_BSTART]], 16);
    CHECK_EQ(dt_av_knobs[AVK_BEND].val[first[AVK_BEND]], 90);
    CHECK_EQ(dt_av_knobs[AVK_BAMP].val[first[AVK_BAMP]], 32);
    CHECK_EQ(dt_av_knobs[AVK_GAIN].val[first[AVK_GAIN]], 8);
    CHECK_EQ(dt_av_knobs[AVK_SYNC].val[first[AVK_SYNC]], 8);
    CHECK_EQ(dt_av_knobs[AVK_BLANK].val[first[AVK_BLANK]], 88);
    CHECK_EQ(dt_av_knobs[AVK_WHITE].val[first[AVK_WHITE]], 9);
    // the sweep knobs: the first step is 263 lines, equalizing off, 3 lines, position 0; the last is 262 lines, on, 6 lines, +4
    CHECK_EQ(dt_av_knobs[AVK_LINES].val[first[AVK_LINES]], 0);
    CHECK_EQ(dt_av_knobs[AVK_EQ].val[first[AVK_EQ]], 0);
    CHECK_EQ(dt_av_knobs[AVK_VLEN].val[first[AVK_VLEN]], 6);
    CHECK_EQ(dt_av_knobs[AVK_VPOS].val[first[AVK_VPOS]], 0);
    CHECK_EQ(dt_av_knobs[AVK_LINES].val[g_avset[AVK_LINES]], 1);
    CHECK_EQ(dt_av_knobs[AVK_EQ].val[g_avset[AVK_EQ]], 1);
    CHECK_EQ(dt_av_knobs[AVK_VLEN].val[g_avset[AVK_VLEN]], 12);
    CHECK_EQ(dt_av_knobs[AVK_VPOS].val[g_avset[AVK_VPOS]], 4);
    CHECK_EQ(r.out.npass, 1);
    std::string t = r.fs.text("/SD/cores/diag/results/avout_20261102-143015.toml");
    std::string x = r.fs.text("/SD/cores/diag/results/avout_20261102-143015.txt");
    CHECK(!t.empty() && !x.empty());
    CHECK(t.find("version = 2") != std::string::npos);
    CHECK(t.find("steps_planned = 36") != std::string::npos);
    CHECK(t.find("Does the picture stand still (no rolling, no jumping)?") != std::string::npos);
    CHECK(t.find("Which colour is the bar right of the white one?") != std::string::npos);
    CHECK(t.find("good = [\"YES\", \"yellow\"]") != std::string::npos);
    CHECK(t.find("[knobs]") != std::string::npos);
    CHECK(t.find("Standard = \"NTSC 60 Hz\"") != std::string::npos);
    CHECK(t.find("Groups = \"Y=A, C=B, CVBS=C\"") != std::string::npos);
    CHECK(t.find("Bits A = \"MSB first\"") != std::string::npos);
    CHECK(t.find("Bits C = \"Reversed\"") != std::string::npos);
    CHECK(t.find("V sign = \"Normal\"") != std::string::npos);
    CHECK(t.find("Subcarrier trim = \"+60 Hz\"") != std::string::npos);
    CHECK(t.find("Burst start = \"16\"") != std::string::npos);
    CHECK(t.find("Burst end = \"90\"") != std::string::npos);
    CHECK(t.find("Burst level = \"100 %\"") != std::string::npos);
    CHECK(t.find("Chroma gain = \"50 %\"") != std::string::npos);
    CHECK(t.find("Sync level = \"8\"") != std::string::npos);
    CHECK(t.find("Blank level = \"88\"") != std::string::npos);
    CHECK(t.find("White level = \"9/16\"") != std::string::npos);
    CHECK(t.find("Chroma = ") == std::string::npos);
    CHECK_EQ(count_of(t, "\n[step."), (size_t)36);
    CHECK(t.find("[step.36]") != std::string::npos);
    CHECK(t.find("Equalizing = \"On\"") != std::string::npos);
    CHECK(t.find("Vsync length = \"2.5 lines\"") != std::string::npos);
    CHECK(t.find("Vsync position = \"-4\"") != std::string::npos);
    CHECK(t.find("Lines 60 Hz = \"262\"") != std::string::npos);
    CHECK(t.find("answers = [\"NO\", \"other\"]") != std::string::npos);
    CHECK(t.find("answers = [\"YES\", \"yellow\"]") != std::string::npos);
    CHECK(t.find("answers = [\"MISSED\", \"blue\"]") != std::string::npos);
    CHECK(t.find("answers = [\"YES\", \"blue\"]") != std::string::npos);
    CHECK(t.find("status = \"done\"") != std::string::npos);
    CHECK(t.find("steps_asked = 36") != std::string::npos);
    CHECK(t.find("steps_first_yes = 2") != std::string::npos);
    CHECK(t.find("steps_good = 1") != std::string::npos);
    // the .txt: the two steps whose first answer was YES with the full combination and all answers; only YES + yellow is GOOD
    CHECK_EQ(count_of(x, "\nStep "), (size_t)2);
    CHECK(x.find("\nStep 2 GOOD: Standard=NTSC 60 Hz, Groups=Y=A, C=B, CVBS=C, ") != std::string::npos);
    CHECK(x.find("Equalizing=Off") != std::string::npos);
    CHECK(x.find("; answers: YES, yellow\n") != std::string::npos);
    CHECK(x.find("\nStep 4: ") != std::string::npos);
    CHECK(x.find("; answers: YES, blue\n") != std::string::npos);
    CHECK(x.find("Step 1:") == std::string::npos && x.find("Step 3:") == std::string::npos);
    CHECK(x.find("36 combinations walked, 2 with the first answer YES, 1 good.") != std::string::npos);
    // ESC at a question ends the walk, status aborted, the freeze released, the page's values back
    Rig e;
    av_hooks(e);
    e.fs.put_text("/SD/cores/diag/tests/avout.toml", slurp("cores/diag/sd/tests/avout.toml"));
    static int asked;
    asked = 0;
    e.ui.answer = [](int) { asked++; return DUI_ABORT; };
    g_avdone = 0;
    g_avfrz.clear();
    CHECK_EQ(e.run("avout.toml"), 0);
    CHECK_EQ(asked, 1);                                 // ESC at question 1: question 2 is not asked
    CHECK_EQ(g_avdone, 1);
    CHECK_EQ(g_avfrz.back(), 0);
    CHECK(e.fs.text("/SD/cores/diag/results/avout_20261102-143015.toml").find("status = \"aborted\"") != std::string::npos);
    CHECK_EQ(e.out.nerror, 1);
    // an application without the A/V page refuses
    Rig n;
    n.fs.put_text("/SD/cores/diag/tests/avout.toml", slurp("cores/diag/sd/tests/avout.toml"));
    CHECK_EQ(n.run("avout.toml"), -1);
    CHECK(std::string(n.out.msg).find("no A/V page") != std::string::npos);
    // a card that refuses the result writes nothing, and no question is asked
    Rig w;
    av_hooks(w);
    w.fs.put_text("/SD/cores/diag/tests/avout.toml", slurp("cores/diag/sd/tests/avout.toml"));
    w.fs.read_only = true;
    CHECK_EQ(w.run("avout.toml"), -1);
    CHECK_EQ(w.ui.prompts.size(), (size_t)0);
    // a card that gives up in the middle of the walk: the walk stops, the files hold what was written, an error says so
    Rig m;
    av_hooks(m);
    m.fs.put_text("/SD/cores/diag/tests/avout.toml", slurp("cores/diag/sd/tests/avout.toml"));
    m.ui.answer = [](int) { return DUI_CONTINUE; };
    m.ui.cans = [](int, int *pick) { *pick = 0; return DUI_CONTINUE; };
    m.fs.write_fail_at = 4;                            // the header pair and two pieces, then the card is gone
    g_avdone = 0;
    CHECK_EQ(m.run("avout.toml"), -1);
    CHECK_EQ(g_avdone, 1);
    CHECK(std::string(m.out.msg).find("cannot go on writing") != std::string::npos);

    // S9: a 384-step walk (4 standards x 6 groups x 2 x 2 x 2 x 2 bit orders / V sign) writes every step: the .toml passes the old 40 KB buffer and ends in [result], the .txt lists every step whose first answer is YES
    Rig b;
    av_hooks(b);
    b.fs.put_text("/SD/cores/diag/tests/long.toml",
                  "kind = \"avtest\"\n[test]\nname = \"long\"\nid = \"long\"\nversion = 1\n[ask]\nquestions = [\"Colour?\", \"Rolls?\"]\ngood = [\"YES\", \"NO\"]\ntimeout_s = 5\nfirst_gate = true\nstop_after_ok = false\n"
                  "[sweep]\nstandard = [\"PAL 50 Hz\", \"NTSC 60 Hz\", \"PAL 60 Hz\", \"NTSC 50 Hz\"]\n"
                  "groups = [\"Y=A, C=B, CVBS=C\", \"Y=A, C=C, CVBS=B\", \"Y=B, C=A, CVBS=C\", \"Y=C, C=A, CVBS=B\", \"Y=B, C=C, CVBS=A\", \"Y=C, C=B, CVBS=A\"]\n"
                  "bits_a = [\"MSB first\", \"Reversed\"]\nbits_b = [\"MSB first\", \"Reversed\"]\nbits_c = [\"MSB first\", \"Reversed\"]\nv_sign = [\"Normal\", \"Inverted\"]\n");
    int step = 0, q1yes = 0;
    b.ui.answer = [&](int idx) {                       // the first question of a step (a new idx): YES at every third step; the second question (the same idx): NO
        if (idx != step) {
            step = idx;
            if (idx % 3 == 0) {
                q1yes++;
                return DUI_CONTINUE;
            }
            return DUI_SKIP;
        }
        return DUI_SKIP;
    };
    CHECK_EQ(b.run("long.toml"), 0);
    std::string lt = b.fs.text("/SD/cores/diag/results/long_20261102-143015.toml");
    std::string lx = b.fs.text("/SD/cores/diag/results/long_20261102-143015.txt");
    CHECK(lt.size() > 40 * 1024);                      // longer than the v1 buffer
    CHECK(b.fs.write_sizes.size() > 10);               // written in pieces
    CHECK_EQ(count_of(lt, "\n[step."), (size_t)384);
    CHECK(lt.find("[step.384]") != std::string::npos);
    CHECK(lt.find("steps_asked = 384") != std::string::npos);
    CHECK(lt.find("status = \"done\"") != std::string::npos);
    CHECK(lt.rfind("[result]") > lt.rfind("[step.384]"));
    CHECK_EQ(q1yes, 128);
    CHECK_EQ(count_of(lx, "\nStep "), (size_t)128);
    CHECK(lx.find("\nStep 3 GOOD: ") != std::string::npos);
    CHECK(lx.find("\nStep 384 GOOD: ") != std::string::npos);
    CHECK(lx.find("\nStep 2:") == std::string::npos && lx.find("\nStep 2 ") == std::string::npos);
    CHECK(lx.find("384 combinations walked, 128 with the first answer YES, 128 good.") != std::string::npos);
    CHECK_EQ(b.out.npass, 1);
}

int main()
{
    test_parse_shipped();
    test_parse_errors();
    test_compile();
    test_padctl_words();
    test_plan_loop();
    test_stale_done();
    test_run_files();
    test_card();
    test_prompts();
    test_requires_suite();
    test_cd32_v2();
    test_engine_errors();
    test_examples();
    test_avout();
    DONE("test_diagtest");
}
