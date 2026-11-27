// T175: the test files of the diag core (cores/diag/app/lib/diagtest.cc) against a model of the probe engine behind CoreIo. What is proven: the TOML schema
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
#include "diag_examples.h"
#include "portdiag_regs.h"
#include "portdiag.h"
#include "panel.h"

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
    "[[step]]\nop = \"drive\"\npins = [\"KBJOY_2_JOY4\"]\nlevel = \"low\"\n"
    "[[step]]\nop = \"wait\"\nus = 20\n"
    "[[step]]\nop = \"sample\"\ninto = \"s1\"\n"
    "[[step]]\nop = \"drive\"\npins = [\"KBJOY_2_JOY4\"]\nlevel = \"release\"\n"
    "[[step]]\nop = \"wait_user\"\nprompt = \"p\"\n"
    "[[step]]\nop = \"time_to_edge\"\npin = \"PADDLE_CMP2\"\ntimeout_us = 400\ninto = \"t\"\n"
    "[[eval]]\nid = \"fire_low\"\nvalue = \"s1.pins\"\npins = [\"KBJOY_2_JOY4\"]\neq = 0\nverdict = \"fire follows\"\nfail = \"fire stays high\"\n"
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
    CHECK(diag_install_examples(&r.fs, "/SD/cores/diag") >= 17);      // the delivery's folder: 14 tests (T259: avprobe, avrgb, avsync, avvsync are new) and the 3 presets
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
    bad("level = \"low\"", "level = \"up\"", 10, "level must be");
    bad("us = 20", "us = twenty", 16, "neither a string");
    bad("timeout_s = 5", "timeout_s = 99999", 9, "0 .. 3600");
    bad("prompt = \"p\"", "prompt = \"q\"", 24, "no [[prompt]] before it defines");
    bad("into = \"s1\"", "into = \"t\"", 27, "used by two steps");
    bad("value = \"s1.pins\"", "value = \"s9.pins\"", 32, "which no step stores into");
    bad("value = \"s1.pins\"", "value = \"s1.pinz\"", 32, "fields are pins");
    bad("min_us = 5\nmax_us = 100", "min_us = 5\nmax_us = 100\nbogus = 1", 44, "unknown key 'bogus'");
    bad("verdict = \"flips in time\"", "verdict2 = \"x\"", 44, "unknown key");
    bad("keys = [\"s1\", \"t\"]", "keys = [\"s1\", \"nope\"]", 0, "no step stores into");
    bad("pins = [\"KBJOY_2_JOY4\"]\nlevel = \"low\"", "pins = [\"KBJOY_2_JOY4\"\nlevel = \"low\"", 12, "closed with ]");
    bad("name = \"Small\"", "name = \"Small", 2, "not closed");
    bad("pin = \"PADDLE_CMP2\"", "cmp = [0]", 29, "does not belong to the step 'time_to_edge'");
    bad("pins = [\"KBJOY_2_JOY4\"]\neq = 0", "eq = 0", 32, "needs pins = [..]");
    // a time slot is compared in microseconds only if a time_to_edge made it
    bad("value = \"t\"\nmin_us = 5", "value = \"s1.pins\"\npins = [\"KBJOY_2_JOY4\"]\nmin_us = 5", 39, "are for a time_to_edge slot");
    // files that are not tests
    CHECK_STR(err_of("", &line).c_str(), "[test] needs an id");
    CHECK_STR(err_of("[test]\nid = \"x\"\n", &line).c_str(), "a test has no [[step]]");
    CHECK_STR(err_of("kind = \"mix\"\n", &line).c_str(), "kind must be \"test\" or \"suite\"");
    CHECK_EQ(line, 1);
    CHECK_STR(err_of("kind = \"suite\"\n[suite]\nid = \"x\"\n", &line).c_str(), "a suite needs [suite] with id and tests = [..]");
    CHECK_STR(err_of("kind = \"suite\"\n[suite]\nid = \"x\"\ntests = [\"a/b.toml\"]\n", &line).c_str(), "tests lists file names of the same folder (at most 16, no path)");
    // a loop that runs off the end, and a loop in a loop
    CHECK_STR(err_of("[test]\nid=\"a\"\n[[step]]\nop=\"loop\"\ncount=2\nbody=3\n[[step]]\nop=\"sample\"\n", &line).c_str(), "the loop wants 3 steps after it, the file has fewer");
    CHECK_STR(err_of("[test]\nid=\"a\"\n[[step]]\nop=\"loop\"\ncount=2\nbody=2\n[[step]]\nop=\"loop\"\ncount=2\nbody=1\n[[step]]\nop=\"sample\"\n", &line).c_str(), "a loop body holds no loop, wait_user, capture, capwait or end");
    // 256 steps do not fit the step RAM
    std::string many = "[test]\nid=\"a\"\n";
    for (int i = 0; i < 256; i++) many += "[[step]]\nop=\"sample\"\n";
    CHECK_STR(err_of(many, &line).c_str(), "more than 255 steps (the step RAM holds 256 words)");
    // nothing is run for a refused file
    Rig r;
    r.fs.put_text("/SD/cores/diag/tests/bad.toml", "[test]\nid = \"b\"\n[[step]]\nop = \"jump\"\n");
    CHECK_EQ(r.run("bad.toml"), -1);
    CHECK_STR(r.out.msg, "bad.toml line 4: no step 'jump' (end drive wait sample edges time_to_edge loop wait_user write_reg capture capwait)");
    CHECK_EQ(r.eng.run_count, 0);
    CHECK_EQ(r.eng.wptr, 0);
    CHECK(!r.fs.has("/SD/cores/diag/results/b_20261102-143015.toml"));
}

// the bitstream's table as the app reads it from the model
static DtPins g_tab;
static const DtPins &table()
{
    Engine e;
    CHECK_EQ(dtv2_pins_read(&e, &g_tab), 0);
    return g_tab;
}

static void test_compile()
{
    DtErr e;
    CHECK(parse(SMALL, &g_f, &e));
    DtProgram p;
    const DtPins &tb = table();
    CHECK_EQ(tb.n, 121);
    // 100 ticks per microsecond: 20 us = 2000 ticks, 400 us = 40000
    CHECK_EQ(dt_compile(g_f.test, tb, 25600, &p, &e), 0);
    CHECK_EQ(p.n, 8);
    CHECK_EQ(p.words[0], dv2_drive(DV2_LOW, 59));      // KBJOY_2_JOY4 is index 59 of the table
    CHECK_EQ(p.words[0], 0x1400003Bu);
    CHECK_EQ(p.words[1], 0x200007D0u);                 // WAIT 2000
    CHECK_EQ(p.words[2], 0x30000000u);                 // SAMPLE
    CHECK_EQ(p.words[3], 0x1000003Bu);                 // DRIVE, release
    CHECK_EQ(p.words[4], 0x80000000u);                 // WAIT_USER prompt 0
    CHECK_EQ(p.words[5], 0x50000041u);                 // TIME_TO_EDGE any on PADDLE_CMP2 (index 65) ..
    CHECK_EQ(p.words[6], 40000u);                      // .. with the timeout as its second word
    CHECK_EQ(p.words[7], 0x00000000u);                 // END
    CHECK_EQ(p.timeout_ticks[1], 40000u);
    // a clock of 12.5 ticks per microsecond rounds a time up, never down
    CHECK_EQ(dt_compile(g_f.test, tb, 3200, &p, &e), 0);
    CHECK_EQ(p.words[1], 0x200000FAu);                 // 20 us * 12.5 = 250
    CHECK_EQ(dt_compile(g_f.test, tb, 3210, &p, &e), 0);   // 12.539 ticks per us: 250.78 -> 251
    CHECK_EQ(p.words[1] & 0xFFFFFF, 251u);
    CHECK_EQ(dt_compile(g_f.test, tb, 0, &p, &e), -1);
    // a wait the engine cannot count in 24 bits is refused by the compiler, a TIME_TO_EDGE timeout may be 31 bits
    std::string t = SMALL;
    t.replace(t.find("us = 20"), 7, "us = 20000000");
    CHECK(parse(t, &g_f, &e));
    CHECK_EQ(dt_compile(g_f.test, tb, 25600, &p, &e), -1);
    CHECK(strstr(e.msg, "clocks at this clock") != 0);
    // a name the bitstream's table does not have: refused with the line of the step
    t = SMALL;
    t.replace(t.find("KBJOY_2_JOY4"), 12, "KBJOY_2_JOY9");
    CHECK(parse(t, &g_f, &e));
    CHECK_EQ(dt_compile(g_f.test, tb, 25600, &p, &e), -1);
    CHECK_EQ(e.line, 10);
    CHECK_STR(e.msg, "'KBJOY_2_JOY9' is no pin of this bitstream's table");
    // S2: a changed threshold changes the verdict, not the step list or the engine's words
    CHECK(parse(SMALL, &g_f, &e));
    DiagFile a = g_f;
    DtProgram pa, pb;
    t = SMALL;
    t.replace(t.find("max_us = 100"), 12, "max_us = 3");
    DiagFile *b = new DiagFile;
    CHECK(parse(t, b, &e));
    CHECK_EQ(dt_compile(a.test, tb, 25600, &pa, &e), 0);
    CHECK_EQ(dt_compile(b->test, tb, 25600, &pb, &e), 0);
    CHECK(!memcmp(&pa, &pb, sizeof(pa)));
    delete b;
}

// S5: a test written at run time that names pins only by name and uses no op the build added for it: it compiles to the table's indexes, runs against the model, and the result file has the
// values and the verdicts. A pin the table has and no shipped test mentions (a cartridge port line behind its transceiver, an LED) is driven and read like any other.
static void test_s5_unknown_test()
{
    const char *toml =
        "[test]\nname = \"Never seen\"\nid = \"never\"\nversion = 1\n"
        "[[step]]\nop = \"drive\"\npins = [\"LED_CASE2n\", \"SLOT_ADDR12\"]\nlevel = \"low\"\n"
        "[[step]]\nop = \"wait\"\nus = 10\n"
        "[[step]]\nop = \"sample\"\ninto = \"low\"\n"
        "[[step]]\nop = \"drive\"\npins = [\"SLOT_ADDR12\"]\nlevel = \"high\"\n"
        "[[step]]\nop = \"wait\"\nus = 10\n"
        "[[step]]\nop = \"sample\"\ninto = \"high\"\n"
        "[[step]]\nop = \"edges\"\npin = \"UI_BUTTON1\"\nedge = \"falling\"\nus = 100\ninto = \"presses\"\n"
        "[[eval]]\nid = \"low_ok\"\nvalue = \"low.pins\"\npins = [\"LED_CASE2n\", \"SLOT_ADDR12\"]\neq = 0\nverdict = \"both pulled low\"\n"
        "[[eval]]\nid = \"high_ok\"\nvalue = \"high.pins\"\npins = [\"LED_CASE2n\", \"SLOT_ADDR12\"]\neq = 2\nverdict = \"addr12 high, the LED let go\"\nfail = \"wrong levels\"\n";
    Rig r;
    r.fs.put_text("/SD/cores/diag/tests/never.toml", toml);
    const DtPins &tb = table();
    int led = dtv2_pin_find(tb, "LED_CASE2n"), a12 = dtv2_pin_find(tb, "SLOT_ADDR12"), btn = dtv2_pin_find(tb, "UI_BUTTON1");
    CHECK(led >= 0 && a12 >= 0 && btn >= 0);
    CHECK_EQ(r.run("never.toml"), 0);
    CHECK_EQ(r.out.nerror, 0);
    // the step words are the table's indexes: two drives for the two pins of the first step
    CHECK_EQ(r.eng.steps[0], dv2_drive(DV2_LOW, led));
    CHECK_EQ(r.eng.steps[1], dv2_drive(DV2_LOW, a12));
    CHECK_EQ(r.eng.steps[4], dv2_drive(DV2_HIGH, a12));
    CHECK_EQ(r.eng.steps[7], dv2_edges(DV2_FALLING, btn));
    CHECK_EQ(r.eng.steps[8], 10000u);                  // 100 us at 100 ticks per us
    std::string x = r.fs.text("/SD/cores/diag/results/never_20261102-143015.toml");
    CHECK(x.find("status = \"done\"") != std::string::npos);
    CHECK(x.find("[slot.low]\nkind = \"sample\"") != std::string::npos);
    CHECK(x.find("pins = [\"LED_CASE2n\", \"SLOT_ADDR12\"]\nlevels = [\"00\"]") != std::string::npos);     // the pins its evals look at, by name, in the result
    CHECK(x.find("low_ok = \"PASS: both pulled low\"") != std::string::npos);
    CHECK(x.find("high_ok = \"PASS: addr12 high, the LED let go\"") != std::string::npos);
    CHECK(x.find("[slot.presses]\nkind = \"edges\"\nvalues = [0]") != std::string::npos);
    CHECK_EQ(r.out.npass, 1);
    CHECK_EQ(r.eng.n_driven(), 0);                       // everything released at the end
}

// S6: the T175 compiler's words (tests/coreapp/golden/diag_old_words.txt, taken from the unchanged diagtest.cc of b6a2a2212 for every shipped test of kind test) translated to v2 by
// the rule of docs/specs/T260 section 5, equal to the words of this compiler for the converted file.
//   DRIVE mask level      -> one DRIVE per pin of the mask, in bit order, mode LOW (level bit 0) or RELEASE (level bit 1); the ten port pins are table indexes 50 + bit
//   MASK m + EDGES n      -> EDGES any on that pin (index 50 + bit), then the word n
//   TIME_TO_FLIP sel n    -> TIME_TO_EDGE any on PADDLE_CMP (63 + the bit of sel), then the word n
//   PADCTL a              -> DRIVE PADDLE_DIS (67) then DRIVE PADDLE_ADJ (68): mode HIGH if the level bit is set, else LOW if the enable bit is set, else RELEASE
//   LOOP K C              -> LOOP with K recounted in words of the translated body
//   WAIT, SAMPLE, WAIT_USER, END -> the same word
static std::vector<uint32_t> translate_old(const std::vector<uint32_t> &old)
{
    std::vector<uint32_t> nw;
    std::vector<size_t> loop_at;
    size_t loop_end = 0;
    for (size_t i = 0; i < old.size(); i++) {
        uint32_t w = old[i], op = w >> 28, arg = w & 0xFFFFFF;
        if (loop_at.size() && i == loop_end) {
            nw[loop_at.back()] = (nw[loop_at.back()] & ~0xFFu) | (uint32_t)(nw.size() - loop_at.back() - 1);
            loop_at.pop_back();
        }
        switch (op) {
        case 1:                                           // DRIVE: arg[9:0] pins, arg[19:10] level (1 = release)
            for (int b = 0; b < 10; b++) {
                if (arg >> b & 1) { nw.push_back(dv2_drive((arg >> (10 + b) & 1) ? DV2_REL : DV2_LOW, 50 + b)); }
            }
            break;
        case 9: {                                         // MASK, then EDGES
            int b = 0;
            while (!(arg >> b & 1)) { b++; }
            uint32_t n = old[++i] & 0xFFFFFF;
            nw.push_back(dv2_edges(DV2_ANY, 50 + b));
            nw.push_back(n);
            break;
        }
        case 5: {                                         // TIME_TO_FLIP, sel = the comparator bits
            int b = 0;
            while (!(((w >> 24) & 15) >> b & 1)) { b++; }
            nw.push_back(dv2_tte(DV2_ANY, 63 + b));
            nw.push_back(arg);
            break;
        }
        case 6:                                           // PADCTL: b0 DIS level, b1 DIS enable, b2 ADJ level, b3 ADJ enable
            nw.push_back(dv2_drive((arg & 1) ? DV2_HIGH : (arg & 2) ? DV2_LOW : DV2_REL, 67));
            nw.push_back(dv2_drive((arg & 4) ? DV2_HIGH : (arg & 8) ? DV2_LOW : DV2_REL, 68));
            break;
        case 7:
            loop_at.push_back(nw.size());
            nw.push_back((7u << 28) | (arg & 0xFFFF00));
            loop_end = i + 1 + (arg & 0xFF);              // K counts the old words: a MASK + EDGES pair is two of them
            break;
        default: nw.push_back(w);
        }
    }
    return nw;
}

static void test_s6_old_words()
{
    std::ifstream f(std::string(GOLDEN_DIR) + "/diag_old_words.txt");
    std::string line;
    int files = 0;
    const DtPins &tb = table();
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') { continue; }
        size_t c = line.find(':');
        std::string name = line.substr(0, c);
        std::vector<uint32_t> old;
        for (const char *p = line.c_str() + c + 1; *p;) { char *e; unsigned long v = strtoul(p, &e, 16); if (e == p) { break; } old.push_back((uint32_t)v); p = e; }
        DtErr e;
        CHECK(parse(slurp("cores/diag/sd/tests/" + name), &g_f, &e));
        DtProgram p;
        CHECK_EQ(dt_compile(g_f.test, tb, 0x6400, &p, &e), 0);
        std::vector<uint32_t> want = translate_old(old), got(p.words, p.words + p.n);
        if (want != got) { printf("  S6 %s: %zu words wanted, %zu compiled\n", name.c_str(), want.size(), got.size()); for (size_t i = 0; i < want.size() && i < got.size(); i++) { if (want[i] != got[i]) { printf("  first difference at word %zu: %08x vs %08x\n", i, want[i], got[i]); break; } } }
        CHECK(want == got);
        files++;
    }
    CHECK_EQ(files, 5);                                  // cd32, cd32keys, paddle, pins, stick2 are the shipped files of kind test; joystick is a suite, the av and xpad files have their own runners
    // every shipped file of the folder is accepted by its own parser (kinds test and suite here; the others by theirs: the walks in test_avwalk, xpad in test_xpad)
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
    CHECK_EQ(r.eng.n_driven(), 0);
}

static void test_plan_loop()
{
    DtErr e;
    const char *t =
        "[test]\nid=\"l\"\n[[step]]\nop=\"loop\"\ncount=3\nbody=3\n[[step]]\nop=\"drive\"\npins=[\"KBJOY_2_JOY4\"]\n[[step]]\nop=\"sample\"\ninto=\"bits\"\n"
        "[[step]]\nop=\"drive\"\npins=[\"KBJOY_2_JOY4\"]\nlevel=\"release\"\n[[step]]\nop=\"edges\"\npin=\"KBJOY_1_JOY0\"\nclocks=50\ninto=\"e\"\n";
    CHECK(parse(t, &g_f, &e));
    int8_t slot_of[DT_WORDS_MAX];
    CHECK_EQ(dt_plan(g_f.test, slot_of), 16);            // three samples of five words, then the edges count
    for (int i = 0; i < 15; i++) { CHECK_EQ(slot_of[i], 0); }
    CHECK_EQ(slot_of[15], 1);
    DtProgram p;
    CHECK_EQ(dt_compile(g_f.test, table(), 25600, &p, &e), 0);
    CHECK_EQ(p.words[0], 0x70000303u);                 // LOOP: 3 repeats of the next 3 words
    CHECK_EQ(p.words[4], 0x40000032u);                 // EDGES any, KBJOY_1_JOY0 (index 50)
    CHECK_EQ(p.words[5], 50u);                         // the clocks, the second word
    // 513 words do not fit the FIFO
    std::string big = "[test]\nid=\"b\"\n[[step]]\nop=\"loop\"\ncount=300\nbody=1\n[[step]]\nop=\"sample\"\n";
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
    CHECK_EQ(r.eng.n_driven(), 0);
}

// the engine run of SMALL: fire follows, the comparators flip after 12.5 us
static const char *EXP_TOML =
    "# TRX64U Port Diagnostics result (T175). Raw words are the engine's FIFO words in order; see the spec for their layout.\n"
    "[meta]\ntest = \"small\"\nname = \"Small\"\nversion = 3\ncore = \"0.1.0\"\ndate = \"20261102-143015\"\nticks_per_us_x256 = 25600\nstatus = \"done\"\n"
    "\n[prompt]\np = \"OK\"\n"
    "\n[slot.s1]\nkind = \"sample\"\nvalues = [2000, 2684354559, 2013257711, 4028375032, 16777215]\nts = [2000]\nsnapshot = [\"00fffffff01c1ff877ffdfef9fffffff\"]\npins = [\"KBJOY_2_JOY4\"]\nlevels = [\"0\"]\n"
    "\n[slot.t]\nkind = \"time\"\nvalues = [1250]\n"
    "\n[raw]\nfifo = [2000, 2684354559, 2013257711, 4028375032, 16777215, 1250]\n"
    "\n[eval]\nfire_low = \"PASS: fire follows\"\nfast = \"PASS: flips in time\"\n";
static const char *EXP_TXT =
    "TRX64U Port Diagnostics\n=======================\n"
    "Test:    Small (small, version 3)\nCore:    0.1.0\nDate:    2026-11-02 14:30:15\nClock:   100.00 ticks per microsecond\nNeeds:   stick\nStatus:  done\n"
    "\nPrompts\n  p        OK        Hold fire\n"
    "\nMeasurements\n  s1[0]          sample  t=2000  KBJOY_2_JOY4=0\n  t[0]           time    1250 ticks = 12.50 us\n"
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
    CHECK_EQ(r.eng.n_driven(), 0);
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
    // no edge within the timeout is no number
    Rig r3;
    r3.fs.put_text("/SD/cores/diag/tests/small.toml", SMALL);
    CHECK_EQ(r3.run("small.toml"), 0);
    std::string y = r3.fs.text("/SD/cores/diag/results/small_20261102-143015.toml");
    CHECK(y.find("fast = \"NOFLIP: no edge within the timeout\"") != std::string::npos);
    std::string yt = r3.fs.text("/SD/cores/diag/results/small_20261102-143015.txt");
    CHECK(yt.find("t[0]           time    no edge within 40000 ticks") != std::string::npos);
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
    CHECK_EQ(a.eng.n_driven(), 0);
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
    CHECK(k.fs.text("/SD/cores/diag/results/stick2_20261102-143015.txt").find("b2[0]          sample  t=0  PADDLE_CMP0=0 PADDLE_CMP1=0 PADDLE_CMP2=1 PADDLE_CMP3=0 KBJOY_2_JOY0=1") != std::string::npos);
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

// Under engine v2 a SAMPLE pushes five words and the hardware FIFO holds 512. cd32 takes 140 samples = 700 words in all, but the runner pops the FIFO at every wait_user and while the
// engine runs, so the 512 bind per segment. cd32 runs to its result file; a test with one segment alone above 512 is refused at once, naming the segment.
static void test_cd32_v2()
{
    Rig r;
    r.fs.put_text("/SD/cores/diag/tests/pins.toml", slurp("cores/diag/sd/tests/pins.toml"));
    r.fs.put_text("/SD/cores/diag/tests/cd32.toml", slurp("cores/diag/sd/tests/cd32.toml"));
    CHECK_EQ(r.run("pins.toml"), 0);
    CHECK_EQ(r.run("cd32.toml"), 0);
    CHECK_EQ(r.out.nerror, 0);
    CHECK_EQ(r.eng.run_count, 2);
    CHECK_EQ((int)r.ui.prompts.size(), 3);
    std::string x = r.fs.text("/SD/cores/diag/results/cd32_20261102-143015.toml");
    CHECK(x.find("status = \"done\"") != std::string::npos);
    DtErr e;
    CHECK(parse(slurp("cores/diag/sd/tests/cd32.toml"), &g_f, &e));
    int8_t slot_of[DT_WORDS_MAX];
    int seg = 0, line = 0;
    CHECK_EQ(dt_plan(g_f.test, slot_of, &seg, &line), 700);   // more than the FIFO holds, in three segments of at most 512
    // one segment alone above 512 words: refused, nothing started, the segment named
    std::string big = "[test]\nid=\"seg\"\nversion=3\n[[prompt]]\nid=\"a\"\ntext=\"go\"\n"
        "[[step]]\nop=\"loop\"\ncount=60\nbody=1\n[[step]]\nop=\"sample\"\ninto=\"s0\"\n"
        "[[step]]\nop=\"wait_user\"\nprompt=\"a\"\n"
        "[[step]]\nop=\"loop\"\ncount=110\nbody=1\n[[step]]\nop=\"sample\"\ninto=\"s1\"\n";
    CHECK(parse(big, &g_f, &e));
    CHECK_EQ(dt_plan(g_f.test, slot_of, &seg, &line), -2);
    CHECK_EQ(seg, 2);
    CHECK(line > 0);
    Rig q;
    q.fs.put_text("/SD/cores/diag/tests/seg.toml", big);
    CHECK_EQ(q.run("seg.toml"), 0);
    CHECK_EQ(q.out.nerror, 1);
    CHECK_EQ(q.eng.run_count, 0);
    std::string y = q.fs.text("/SD/cores/diag/results/seg_20261102-143015.toml");
    CHECK(y.find("status = \"error\"") != std::string::npos);
    CHECK(y.find("segment 2 (after the wait_user of line") != std::string::npos);
}

// T175 S-joy (board 2026-11-15, cd32keys v1): the joystick in port 2 drives the menu, so a prompt that says "hold a direction, then press RETURN" was answered by the held
// direction (SKIPPED). The model: while the joystick is a menu input and a direction is held, the held direction answers the prompt as ESC/NO would; the keyboard's RETURN
// answers it otherwise. `menu_input = "keyboard"` switches the joystick off for the run, and back on before the result dialog.
static bool g_joy_menu, g_dir_held;
static int g_joy_calls;
static void joy_menu_cb(void *, bool on) { g_joy_menu = on; g_joy_calls++; }
static void test_menu_input()
{
    for (int kbd = 0; kbd < 2; kbd++) {
        Rig r;
        std::string t = std::string(SMALL);
        size_t at = t.find("version = 3\n") + 12;
        t.insert(at, kbd ? "menu_input = \"keyboard\"\n" : "menu_input = \"any\"\n");
        r.fs.put_text("/SD/cores/diag/tests/small.toml", t);
        r.env.joy_menu = joy_menu_cb;
        g_joy_menu = true;
        g_dir_held = true;                                  // the person holds a direction while pressing RETURN on the keyboard
        g_joy_calls = 0;
        r.ui.answer = [](int) { return g_joy_menu && g_dir_held ? DUI_SKIP : DUI_CONTINUE; };
        CHECK_EQ(r.run("small.toml"), 0);
        std::string x = r.fs.text("/SD/cores/diag/results/small_20261102-143015.toml");
        if (kbd) {
            CHECK(x.find("SKIPPED") == std::string::npos);
            CHECK(x.find("p = \"OK\"") != std::string::npos);
            CHECK_EQ(g_joy_calls, 2);                       // off at the start, on again after the run
            CHECK(g_joy_menu);                              // restored
        } else {
            CHECK(x.find("SKIPPED") != std::string::npos);  // today's behaviour: the held direction answers the prompt
            CHECK_EQ(g_joy_calls, 0);
        }
    }
    // an unknown value is an error with its line
    int line;
    std::string bad = std::string(SMALL);
    bad.insert(bad.find("version = 3\n") + 12, "menu_input = \"joy\"\n");
    CHECK_STR(err_of(bad, &line).c_str(), "menu_input is \"any\" or \"keyboard\"");
    CHECK_EQ(line, 5);
    // the three shipped tests that ask to hold something say keyboard
    const char *kb[] = { "cd32", "cd32keys", "stick2" };
    for (const char *n : kb) {
        DtErr e;
        CHECK(parse(slurp(std::string("cores/diag/sd/tests/") + n + ".toml"), &g_f, &e));
        CHECK(g_f.test.kbd_only);
    }
    DtErr e2;
    CHECK(parse(slurp("cores/diag/sd/tests/pins.toml"), &g_f, &e2));
    CHECK(!g_f.test.kbd_only);
}

// SMALL with `requires = ["pins.drivable"]` and no prompt: stands for cd32 (which the FIFO bound below keeps from running at all)
static std::string needs_pins()
{
    std::string t = SMALL;
    t.insert(t.find("needs = "), "requires = [\"pins.drivable\"]\n");
    return t;
}

static void test_requires_suite()
{
    // S6 control: a test that requires pins.drivable stops without a PASS of it (no result at all, then a result that says no), runs with one
    const std::string cd32 = needs_pins();
    Rig r;
    r.fs.put_text("/SD/cores/diag/tests/cd32.toml", cd32);
    CHECK_EQ(r.run("cd32.toml"), 0);
    CHECK_EQ(r.out.nrefused, 1);
    CHECK_EQ(r.eng.run_count, 0);
    CHECK(r.ui.prompts.empty());
    std::string x = r.fs.text("/SD/cores/diag/results/small_20261102-143015.toml");
    CHECK(x.find("status = \"refused\"") != std::string::npos);
    CHECK(x.find("run pins.toml first") != std::string::npos);
    CHECK(x.find("fifo = []") != std::string::npos);
    // the pins test on a board that drives nothing says drivable is not met; the test that requires it stops on it
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
    CHECK(p.fs.text("/SD/cores/diag/results/small_20261102-143015.toml").find("cannot run on this board: pins.drivable is not PASS in pins_20261102-143015.toml") != std::string::npos);
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
    // a board that drives everything: pins passes, and the test that requires it then runs
    Rig d;
    d.fs.put_text("/SD/cores/diag/tests/pins.toml", slurp("cores/diag/sd/tests/pins.toml"));
    d.fs.put_text("/SD/cores/diag/tests/cd32.toml", cd32);
    CHECK_EQ(d.run("pins.toml"), 0);
    CHECK(d.fs.text("/SD/cores/diag/results/pins_20261102-143015.toml").find("drivable = \"PASS") != std::string::npos);
    CHECK_EQ(d.run("cd32.toml"), 0);
    CHECK_EQ(d.eng.run_count, 2);
    CHECK_EQ(d.out.ntests, 1);
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
    o.fs.put_text("/SD/cores/diag/tests/l.toml", "[test]\nid=\"l\"\n[[step]]\nop=\"loop\"\ncount=100\nbody=1\n[[step]]\nop=\"sample\"\ninto=\"x\"\n[[eval]]\nvalue=\"x.ts\"\nmin=0\nverdict=\"v\"\n");
    CHECK_EQ(o.run("l.toml"), 0);
    CHECK_EQ(o.out.nerror, 0);                            // 500 fit
    Rig q;
    q.fs.put_text("/SD/cores/diag/tests/l.toml", "[test]\nid=\"l\"\n[[step]]\nop=\"loop\"\ncount=100\nbody=1\n[[step]]\nop=\"sample\"\ninto=\"x\"\n[[step]]\nop=\"loop\"\ncount=20\nbody=1\n[[step]]\nop=\"sample\"\n");
    CHECK_EQ(q.run("l.toml"), 0);
    CHECK(q.fs.text("/SD/cores/diag/results/l_20261102-143015.toml").find("segment 1 (before the first wait_user) pushes more than the 512 words") != std::string::npos);
    // the engine pushed a different count than the step list: refused, no verdict
    Rig w;
    w.fs.put_text("/SD/cores/diag/tests/small.toml", SMALL);
    w.eng.on_prompt = nullptr;
    w.ui.answer = [&w](int) { w.eng.fifo.push_back(7); return DUI_CONTINUE; };
    CHECK_EQ(w.run("small.toml"), 0);
    CHECK(w.fs.text("/SD/cores/diag/results/small_20261102-143015.toml").find("the engine pushed 7 words, the step list pushes 6") != std::string::npos);
}

static void test_examples()
{
    Rig r;
    CHECK(diag_install_examples(&r.fs, "/SD/cores/diag") >= 17);     // T260 D6: the delivery's folder cores/diag/sd, copied to /SD/cores/diag
    CHECK(r.fs.has("/SD/cores/diag/tests/pins.toml"));
    CHECK(r.fs.has("/SD/cores/diag/presets/Sony-PAL.toml"));
    // the live vector
    Rig l;
    l.eng.forced_low = 0x201;
    l.eng.cmp_now = 5;
    uint32_t w[4];
    diag_live(&l.eng, w);                               // the snapshot of the bank: port 1 up and port 2 fire held low from outside, comparators 0 and 2 high
    CHECK(!dt_snap_bit(w, 50) && dt_snap_bit(w, 51) && dt_snap_bit(w, 54) && dt_snap_bit(w, 55) && !dt_snap_bit(w, 59));
    CHECK(dt_snap_bit(w, 63) && !dt_snap_bit(w, 64) && dt_snap_bit(w, 65) && !dt_snap_bit(w, 66));
}

// T201 S4, S9, S11: the shipped vsync walk (v2, since T259 avvsync.toml). Each step sets its combination, asks its questions, records YES / NO / MISSED / SKIPPED or the choice's label, the result files carry the combination; the page's values come back at the end.
static int g_avset[DT_AV_N], g_avcalls, g_avdone;
static std::vector<std::pair<int, int>> g_avlog;
static std::vector<int> g_avfrz;
static Panel g_avpanel;                              // cores/diag/sd/panels/avout.toml: the walk's knobs (the app loads the same file from the card)
static const Panel *av_panel_t(void *, char *why, int n)
{
    static bool loaded;
    if (!loaded) {
        std::string t = slurp("cores/diag/sd/panels/avout.toml");
        char e[160];
        if (panel_parse(t.data(), (uint32_t)t.size(), &g_avpanel, portdiag_reg_by_name, e, sizeof(e))) {
            snprintf(why, n, "%s", e);
            return 0;
        }
        loaded = true;
    }
    return &g_avpanel;
}
static int av_get_t(void *, int k) { int i = panel_name_idx(g_avpanel.row[k], g_avpanel.row[k].def); return i < 0 ? 0 : i; }
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
    r.env.av_panel = av_panel_t;
    r.env.av_get = av_get_t;
    r.env.av_set = av_set_t;
    r.env.av_done = av_done_t;
    r.env.av_freeze = av_freeze_t;
}
static void test_avout()
{
    // S11: the shipped vsync walk (avvsync.toml, the T201 v2 walk), 36 steps, both questions every time (no gate). Calls come in pairs: step 1: Q1 NO, Q2 other | step 2: Q1 YES, Q2 yellow (good) | step 3: Q1 timeout, Q2 blue
    // | step 4: Q1 YES, Q2 blue (still, wrong colour: listed, not good) | the rest NO, other
    Rig r;
    av_hooks(r);
    r.fs.put_text("/SD/cores/diag/tests/avvsync.toml", slurp("cores/diag/sd/tests/avvsync.toml"));
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
    CHECK_EQ(r.run("avvsync.toml"), 0);
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
    CHECK_EQ(g_avpanel.row[AVK_STD].vals[first[AVK_STD]], 1);
    CHECK_EQ(g_avpanel.row[AVK_GROUPS].vals[first[AVK_GROUPS]], 0);
    CHECK_EQ(g_avpanel.row[AVK_BITS_A].vals[first[AVK_BITS_A]], 0);
    CHECK_EQ(g_avpanel.row[AVK_BITS_B].vals[first[AVK_BITS_B]], 0);
    CHECK_EQ(g_avpanel.row[AVK_BITS_C].vals[first[AVK_BITS_C]], 1);
    CHECK_EQ(g_avpanel.row[AVK_VSIGN].vals[first[AVK_VSIGN]], 0);
    CHECK_EQ(g_avpanel.row[AVK_TRIM].vals[first[AVK_TRIM]], 15);
    CHECK_EQ(g_avpanel.row[AVK_BSTART].vals[first[AVK_BSTART]], 16);
    CHECK_EQ(g_avpanel.row[AVK_BEND].vals[first[AVK_BEND]], 90);
    CHECK_EQ(g_avpanel.row[AVK_BAMP].vals[first[AVK_BAMP]], 32);
    CHECK_EQ(g_avpanel.row[AVK_GAIN].vals[first[AVK_GAIN]], 8);
    CHECK_EQ(g_avpanel.row[AVK_SYNC].vals[first[AVK_SYNC]], 8);
    CHECK_EQ(g_avpanel.row[AVK_BLANK].vals[first[AVK_BLANK]], 88);
    CHECK_EQ(g_avpanel.row[AVK_WHITE].vals[first[AVK_WHITE]], 9);
    // the sweep knobs: the first step is 263 lines, equalizing off, 3 lines, position 0; the last is 262 lines, on, 6 lines, +4
    CHECK_EQ(g_avpanel.row[AVK_LINES].vals[first[AVK_LINES]], 0);
    CHECK_EQ(g_avpanel.row[AVK_EQ].vals[first[AVK_EQ]], 0);
    CHECK_EQ(g_avpanel.row[AVK_VLEN].vals[first[AVK_VLEN]], 6);
    CHECK_EQ(g_avpanel.row[AVK_VPOS].vals[first[AVK_VPOS]], 0);
    CHECK_EQ(g_avpanel.row[AVK_LINES].vals[g_avset[AVK_LINES]], 1);
    CHECK_EQ(g_avpanel.row[AVK_EQ].vals[g_avset[AVK_EQ]], 1);
    CHECK_EQ(g_avpanel.row[AVK_VLEN].vals[g_avset[AVK_VLEN]], 12);
    CHECK_EQ(g_avpanel.row[AVK_VPOS].vals[g_avset[AVK_VPOS]], 4);
    CHECK_EQ(r.out.npass, 1);
    std::string t = r.fs.text("/SD/cores/diag/results/avvsync_20261102-143015.toml");
    std::string x = r.fs.text("/SD/cores/diag/results/avvsync_20261102-143015.txt");
    CHECK(!t.empty() && !x.empty());
    CHECK(t.find("version = 1") != std::string::npos);
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
    e.fs.put_text("/SD/cores/diag/tests/avvsync.toml", slurp("cores/diag/sd/tests/avvsync.toml"));
    static int asked;
    asked = 0;
    e.ui.answer = [](int) { asked++; return DUI_ABORT; };
    g_avdone = 0;
    g_avfrz.clear();
    CHECK_EQ(e.run("avvsync.toml"), 0);
    CHECK_EQ(asked, 1);                                 // ESC at question 1: question 2 is not asked
    CHECK_EQ(g_avdone, 1);
    CHECK_EQ(g_avfrz.back(), 0);
    CHECK(e.fs.text("/SD/cores/diag/results/avvsync_20261102-143015.toml").find("status = \"aborted\"") != std::string::npos);
    CHECK_EQ(e.out.nerror, 1);
    // an application without the A/V panel refuses
    Rig n;
    n.fs.put_text("/SD/cores/diag/tests/avvsync.toml", slurp("cores/diag/sd/tests/avvsync.toml"));
    CHECK_EQ(n.run("avvsync.toml"), -1);
    CHECK(std::string(n.out.msg).find("no A/V panel") != std::string::npos);
    // a panel file that lacks a knob of the walks (or has another key in its place) refuses the walk, naming the key it expected
    {
        Rig m;
        av_hooks(m);
        m.env.av_panel = [](void *, char *, int) -> const Panel * { static Panel bad; std::string t = slurp("cores/diag/sd/panels/avout.toml"); size_t a = t.find("key = \"Groups\""); t.replace(a, 14, "key = \"Gruppen\""); char e[160]; panel_parse(t.data(), (uint32_t)t.size(), &bad, portdiag_reg_by_name, e, sizeof(e)); return &bad; };
        m.fs.put_text("/SD/cores/diag/tests/avvsync.toml", slurp("cores/diag/sd/tests/avvsync.toml"));
        CHECK_EQ(m.run("avvsync.toml"), -1);
        CHECK(std::string(m.out.msg).find("the walks need the knob \"Groups\"") != std::string::npos);
    }
    // a card that refuses the result writes nothing, and no question is asked
    Rig w;
    av_hooks(w);
    w.fs.put_text("/SD/cores/diag/tests/avvsync.toml", slurp("cores/diag/sd/tests/avvsync.toml"));
    w.fs.read_only = true;
    CHECK_EQ(w.run("avvsync.toml"), -1);
    CHECK_EQ(w.ui.prompts.size(), (size_t)0);
    // a card that gives up in the middle of the walk: the walk stops, the files hold what was written, an error says so
    Rig m;
    av_hooks(m);
    m.fs.put_text("/SD/cores/diag/tests/avvsync.toml", slurp("cores/diag/sd/tests/avvsync.toml"));
    m.ui.answer = [](int) { return DUI_CONTINUE; };
    m.ui.cans = [](int, int *pick) { *pick = 0; return DUI_CONTINUE; };
    m.fs.write_fail_at = 4;                            // the header pair and two pieces, then the card is gone
    g_avdone = 0;
    CHECK_EQ(m.run("avvsync.toml"), -1);
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


// T259 S4: each preset sets every knob (the values of the spec's section 1), a walk names it in [base] and the result files say so; a [base] knob wins over the preset
static void test_avpresets()
{
    struct Want { const char *name; int v[DT_AV_N]; };
    //            std grp bA bB bC vs  trim bs  be  bamp gain sync blank white eq vlen vpos lines map mode pch rch syn fb
    static const Want want[3] = {
        { "Sony-PAL",      { 0, 5, 0, 0, 0, 1, 0, 28, 99, 32, 16, 0, 77, 11, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { "NTSC-50.rix01", { 3, 0, 0, 0, 1, 0, 15, 16, 90, 32, 8, 8, 88, 9, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { "NTSC-60.rix02", { 1, 0, 0, 0, 1, 0, 31, 16, 90, 32, 8, 8, 88, 16, 0, 12, 0, 0, 0, 0, 0, 0, 0, 0 } },
    };
    for (int w = 0; w < 3; w++) {
        Rig r;
        av_hooks(r);
        r.fs.put_text(std::string("/SD/cores/diag/presets/") + want[w].name + ".toml", slurp(std::string("cores/diag/sd/presets/") + want[w].name + ".toml"));
        r.fs.put_text("/SD/cores/diag/tests/pre.toml", std::string("kind = \"avtest\"\n[test]\nname = \"pre\"\nid = \"pre\"\nversion = 1\n[ask]\nquestions = [\"Ok?\"]\n[base]\npreset = \"") + want[w].name + "\"\n");
        g_avlog.clear();
        CHECK_EQ(r.run("pre.toml"), 0);
        int got[DT_AV_N];
        for (int k = 0; k < DT_AV_N; k++) got[k] = -1;
        for (auto &e : g_avlog) got[e.first] = e.second;
        for (int k = 0; k < DT_AV_N; k++) {
            CHECK(got[k] >= 0);
            if (got[k] >= 0) CHECK_EQ(g_avpanel.row[k].vals[got[k]], want[w].v[k]);
        }
        std::string t = r.fs.text("/SD/cores/diag/results/pre_20261102-143015.toml");
        std::string x = r.fs.text("/SD/cores/diag/results/pre_20261102-143015.txt");
        CHECK(t.find(std::string("preset = \"") + want[w].name + "\"") != std::string::npos);
        CHECK(x.find(std::string("Preset ") + want[w].name + "\n") != std::string::npos);
    }
    // a [base] knob wins over the preset, a missing preset and a free value refuse
    Rig r;
    av_hooks(r);
    r.fs.put_text("/SD/cores/diag/presets/NTSC-50.rix01.toml", slurp("cores/diag/sd/presets/NTSC-50.rix01.toml"));
    r.fs.put_text("/SD/cores/diag/tests/pre.toml", "kind = \"avtest\"\n[test]\nname = \"pre\"\nid = \"pre\"\nversion = 1\n[ask]\nquestions = [\"Ok?\"]\n[base]\npreset = \"NTSC-50.rix01\"\nstandard = \"PAL 50 Hz\"\nmap = \"Names\"\n");
    g_avlog.clear();
    CHECK_EQ(r.run("pre.toml"), 0);
    int std_v = -1, map_v = -1, trim_v = -99;
    for (auto &e : g_avlog) { if (e.first == AVK_STD) std_v = g_avpanel.row[AVK_STD].vals[e.second]; if (e.first == AVK_MAP) map_v = e.second; if (e.first == AVK_TRIM) trim_v = g_avpanel.row[AVK_TRIM].vals[e.second]; }
    CHECK_EQ(std_v, 0);
    CHECK_EQ(map_v, 1);
    CHECK_EQ(trim_v, 15);                              // the preset's +60 Hz stays
    CHECK_EQ(r.run("pre.toml"), 0);
    Rig m;
    av_hooks(m);
    m.fs.put_text("/SD/cores/diag/tests/pre.toml", "kind = \"avtest\"\n[test]\nname = \"pre\"\nid = \"pre\"\nversion = 1\n[ask]\nquestions = [\"Ok?\"]\n[base]\npreset = \"nothere\"\n");
    CHECK_EQ(m.run("pre.toml"), -1);
    CHECK(std::string(m.out.msg).find("not on the card") != std::string::npos);
    Rig f;
    av_hooks(f);
    f.fs.put_text("/SD/cores/diag/presets/mine.toml", "kind = \"avpreset\"\n[preset]\nname = \"mine\"\nBurst start = \"#33\"\n");
    f.fs.put_text("/SD/cores/diag/tests/pre.toml", "kind = \"avtest\"\n[test]\nname = \"pre\"\nid = \"pre\"\nversion = 1\n[ask]\nquestions = [\"Ok?\"]\n[base]\npreset = \"mine\"\n");
    CHECK_EQ(f.run("pre.toml"), -1);
    CHECK(std::string(f.out.msg).find("free value") != std::string::npos);
}

// T259 D16-bis: a walk whose file names no standard asks for it once and uses the answer; a file that names one does not ask
static void test_avstd_ask()
{
    const char *hd = "kind = \"avtest\"\n[test]\nname = \"st\"\nid = \"st\"\nversion = 1\n[ask]\nquestions = [\"Ok?\"]\n";
    Rig r;
    av_hooks(r);
    int asked = 0;
    r.ui.cans = [&](int, int *pick) { asked++; *pick = 2; return DUI_CONTINUE; };
    r.fs.put_text("/SD/cores/diag/tests/st.toml", std::string(hd) + "[base]\nmap = \"Names\"\n");
    g_avlog.clear();
    CHECK_EQ(r.run("st.toml"), 0);
    CHECK_EQ(asked, 1);
    int std_v = -1;
    for (auto &e : g_avlog) if (e.first == AVK_STD) std_v = g_avpanel.row[AVK_STD].vals[e.second];
    CHECK_EQ(std_v, g_avpanel.row[AVK_STD].vals[2]);
    std::string t = r.fs.text("/SD/cores/diag/results/st_20261102-143015.toml");
    CHECK(t.find(std::string("Standard = \"") + g_avpanel.row[AVK_STD].names[2] + "\"") != std::string::npos);
    Rig f;
    av_hooks(f);
    int asked2 = 0;
    f.ui.cans = [&](int, int *pick) { asked2++; *pick = 0; return DUI_CONTINUE; };
    f.fs.put_text("/SD/cores/diag/tests/st.toml", std::string(hd) + "[base]\nstandard = \"PAL 50 Hz\"\n");
    CHECK_EQ(f.run("st.toml"), 0);
    CHECK_EQ(asked2, 0);
}

// T259 S5: avout.toml v3 is a suite of avprobe, avrgb and avsync in this order; each writes its pair, the probe's .txt names the channel and bit order the TV locked on
static void test_avwalk()
{
    Rig r;
    av_hooks(r);
    for (const char *f : { "avout", "avprobe", "avrgb", "avsync" }) r.fs.put_text(std::string("/SD/cores/diag/tests/") + f + ".toml", slurp(std::string("cores/diag/sd/tests/") + f + ".toml"));
    int q = 0;
    // probe: 6 steps (Names A, B, C, Scan A, B, C). The TV locks on Names B (step 2, bars rise) and Scan C (step 6, bars do not); question 2 is only asked after a YES
    int lock_step = 0;
    r.ui.answer = [&](int idx) {
        q++;
        bool q1 = (idx != lock_step);
        lock_step = idx;
        if (q1) return (idx == 2 || idx == 6) ? DUI_CONTINUE : DUI_SKIP;       // question 1 of a step
        return idx == 2 ? DUI_CONTINUE : DUI_SKIP;                               // question 2 (only asked after a YES): step 2 bars rise, step 6 not
    };
    r.ui.cans = [&](int, int *pick) { *pick = 0; return DUI_CONTINUE; };
    g_avlog.clear();
    g_avdone = 0;
    CHECK_EQ(r.run("avout.toml"), 0);
    CHECK_EQ(g_avdone, 3);                              // three walks, each puts the page's values back
    CHECK_EQ(r.out.ntests, 3);
    std::string pt = r.fs.text("/SD/cores/diag/results/avprobe_20261102-143015.toml");
    std::string px = r.fs.text("/SD/cores/diag/results/avprobe_20261102-143015.txt");
    std::string rt = r.fs.text("/SD/cores/diag/results/avrgb_20261102-143015.toml");
    std::string st = r.fs.text("/SD/cores/diag/results/avsync_20261102-143015.toml");
    std::string su = r.fs.text("/SD/cores/diag/results/avout_20261102-143015.txt");
    CHECK(!pt.empty() && !px.empty() && !rt.empty() && !st.empty() && !su.empty());
    CHECK_EQ(count_of(pt, "\n[step."), (size_t)6);
    CHECK_EQ(count_of(rt, "\n[step."), (size_t)24);
    CHECK_EQ(count_of(st, "\n[step."), (size_t)32);
    // the order of the walks: the probe's steps come first in the log of set knobs, then the RGB walk's, then the pins'
    size_t first_rgb = 0, first_sync = 0;
    for (size_t i = 0; i < g_avlog.size(); i++) {
        if (!first_rgb && g_avlog[i].first == AVK_MODE && g_avlog[i].second == 2) first_rgb = i + 1;
        if (!first_sync && g_avlog[i].first == AVK_SYNCN && g_avlog[i].second != 0) first_sync = i + 1;
    }
    CHECK(first_rgb > 0 && first_sync > first_rgb);
    // every step of the probe: map, channel, answers; the summary names the inputs the TV locked on
    CHECK(pt.find("Map = \"Names\"") != std::string::npos);
    CHECK(pt.find("Map = \"Scan groups\"") != std::string::npos);
    CHECK(pt.find("Probe channel = \"C or B\"") != std::string::npos);
    CHECK(px.find("The TV locks on (channel probe):") != std::string::npos);
    CHECK(px.find("map Names, channel B or G: the bars get brighter from left to right: YES (the listed bit order)") != std::string::npos);
    CHECK(px.find("map Scan groups, channel C or B: the bars get brighter from left to right: NO (another bit order") != std::string::npos);
    CHECK_EQ(count_of(px, "\n  map "), (size_t)2);
    CHECK(rt.find("RGB picture = \"G ramp\"") != std::string::npos);
    CHECK(rt.find("Groups = \"Y=C, C=B, CVBS=A\"") != std::string::npos);
    CHECK(st.find("SYNCn = \"Inverted csync\"") != std::string::npos);
    CHECK(st.find("FB = \"Inverted window\"") != std::string::npos);
    CHECK(su.find("pass") != std::string::npos || su.find("FAIL") != std::string::npos);
    CHECK(su.find("avprobe") != std::string::npos && su.find("avrgb") != std::string::npos && su.find("avsync") != std::string::npos);
}

int main()
{
    test_parse_shipped();
    test_parse_errors();
    test_compile();
    test_s5_unknown_test();
    test_s6_old_words();
    test_plan_loop();
    test_stale_done();
    test_run_files();
    test_card();
    test_prompts();
    test_requires_suite();
    test_cd32_v2();
    test_menu_input();
    test_engine_errors();
    test_examples();
    test_avout();
    test_avpresets();
    test_avwalk();
    test_avstd_ask();
    DONE("test_diagtest");
}
