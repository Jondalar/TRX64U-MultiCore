// The test files of the diag core (T175 section 3 D4, section 6 Q1, Q2; T260 v2) and the run that turns one into measurements: the TOML is parsed, its pin names are resolved against the
// pin table of the bitstream and it is compiled to the probe engine's step list (dtv2.h), the engine runs, the result FIFO is decoded, the thresholds of the file are evaluated, and two files are
// written - the raw result as TOML and a readable report. Pure: the engine is a CoreIo, the card a CoreFs, the person a DiagUi, so
// tests/coreapp/test_diagtest.cc runs all of it against a model of the engine.
#ifndef COREAPP_DIAGTEST_H
#define COREAPP_DIAGTEST_H

#include <stdint.h>
#include "io.h"
#include "fsif.h"
#include "panel.h"
#include "dtv2.h"

#define DT_STR        160              // a text value of a file
#define DT_NAME       24               // an id, a slot name
#define DT_PROMPTS    16
#define DT_SRC_STEPS  255              // the step RAM holds 256 words; the last is END
#define DT_SLOTS      32
#define DT_EVALS      48
#define DT_VERD       96               // a verdict text
#define DT_ALL        8
#define DT_LIST       16
#define DT_PATH       96
#define DT_FILE_MAX   16384            // the longest test file

struct DtErr {
    int line;                          // 1-based, 0: not about a line of the file
    char msg[120];
};

#define DT_PN         16               // a pin name of the table (15 characters and the NUL)
#define DT_NAMES      384              // the pin names a test file uses, in one pool (drive pins, edge pins, eval pins, capture lanes)
#define DT_EPINS      32               // the pins of one eval or one capture: bit j of the value is the j-th name
#define DT_CAP_MAX    4096             // the samples of all captures of one test
enum { DT_OP_END = 0, DT_OP_DRIVE, DT_OP_WAIT, DT_OP_SAMPLE, DT_OP_EDGES, DT_OP_TTE, DT_OP_LOOP, DT_OP_WAIT_USER, DT_OP_WRITE_REG, DT_OP_CAPTURE, DT_OP_CAPWAIT };
enum { DT_SLOT_SAMPLE = 1, DT_SLOT_EDGES = 2, DT_SLOT_TIME = 3, DT_SLOT_CAP = 4 };
enum { DT_F_RAW = 0, DT_F_PINS, DT_F_TS };
#define DT_SAMPLE_WORDS 5              // a SAMPLE pushes t and the four words of the snapshot

struct DtPrompt {
    char id[DT_NAME];
    char text[DT_STR];
    int timeout_s;                     // 0: waits for ever
};

struct DtStep {
    uint8_t op;
    uint8_t level;                     // drive: DV2_REL / LOW / HIGH / HIZ
    uint8_t edge;                      // edges, time_to_edge: DV2_ANY / RISING / FALLING
    uint8_t in_us;                     // `n` is microseconds (else clocks)
    uint8_t npins;                     // drive, capture: how many names; edges, time_to_edge: 1
    uint8_t regbytes;                  // write_reg: 1, or 2 for a 16 bit property (two writes, low byte first)
    uint16_t pfirst;                   // the first of the names in DiagTest::names
    uint16_t body;                     // loop: the following steps
    uint16_t reg;                      // write_reg: the register offset
    uint32_t count;                    // loop: repeats
    uint32_t n;                        // wait, edges: duration; time_to_edge: timeout; capture: the period
    uint32_t value;                    // write_reg
    uint32_t samples;                  // capture
    int8_t prompt;                     // wait_user
    int8_t slot;                       // into
    int line;
};

struct DtSlot {
    char name[DT_NAME];
    uint8_t kind;                      // DT_SLOT_*
};

struct DtEval {
    char id[DT_NAME];
    char value[DT_NAME];               // the slot, "" for an eval that only combines others
    int8_t slot;
    uint8_t field;                     // DT_F_*, of a sample slot
    uint8_t npins;                     // field pins: the names in DiagTest::names from pfirst; bit j of the value is the j-th
    uint16_t pfirst;
    uint16_t index;
    bool each;                         // every value of the slot must hold, not only `index`
    bool has_mask, has_eq, has_min, has_max, has_min_us, has_max_us;
    uint32_t mask;
    int64_t eq, min, max, min_us, max_us;
    char differs[DT_NAME];             // the value must differ from the same field of the first value of this slot
    int8_t differs_slot;
    char verdict[DT_VERD];
    char fail[DT_VERD];
    char all[DT_ALL][DT_NAME];
    int nall;
    int line;
};

enum { DT_KIND_TEST = 0, DT_KIND_SUITE = 1 };

struct DiagTest {
    char name[DT_STR], id[DT_NAME];
    int version;
    bool kbd_only;                                          // [test] menu_input = "keyboard": the joystick ports feed no menu key while the test runs
    char needs[DT_LIST][48]; int nneeds;                   // what the person needs plugged in, for the report
    char requires[DT_LIST][DT_NAME * 2]; int nrequires;     // "<test>.<eval id>": the newest result of that test must show the eval as PASS
    DtPrompt prompts[DT_PROMPTS]; int nprompts;
    DtStep steps[DT_SRC_STEPS]; int nsteps;
    DtSlot slots[DT_SLOTS]; int nslots;
    DtEval evals[DT_EVALS]; int nevals;
    char keys[DT_SLOTS][DT_NAME]; int nkeys;                // [result] keys: the slots written out; none listed: all
    char names[DT_NAMES][DT_PN]; int nnames;                // the pin names, resolved against the table by dt_compile
};

struct DiagSuite {
    char name[DT_STR], id[DT_NAME];
    char tests[DT_LIST][DT_PATH]; int ntests;
    bool stop_on_fail;
};

struct DiagFile {
    int kind;
    DiagTest test;
    DiagSuite suite;
};

// Parses a test or suite file. 0, else -1 with the line and the reason (nothing of `out` is to be used). An unknown section, key or op is an error.
int dt_parse(const char *text, uint32_t len, DiagFile *out, DtErr *err);

// The step list of a test: the words of dtv2.h (a drive of n pins is n words, an edges / time_to_edge / capture two) plus END. `pins` is the bitstream's table: every name is resolved against it
// (an unknown name is an error with the line). `tpus256` is the engine's clock in ticks per microsecond x 256 (PD_TICKS); times in the file are microseconds. 0 on success, else -1 with the reason.
struct DtProgram {
    uint32_t words[256];
    int n;
    uint16_t word_of[DT_SRC_STEPS];        // the word each source step starts at
    uint32_t timeout_ticks[DT_SLOTS];      // per slot of kind TIME
    uint8_t pin_of[DT_NAMES];              // the table index of every name of the pool
    uint8_t cap_lane[DT_EPINS];            // the pin index of capture lane j (0x7F: unused), written to CAP_SEL before RUN
    int ncap_lanes;                        // 0: the test has no capture
    uint32_t cap_line0[DT_SLOTS];          // per capture slot: its first line of the ring (a capture starts on the line after the one before)
    uint32_t cap_samples[DT_SLOTS];
    uint32_t cap_off[DT_SLOTS];            // per capture slot: where its samples start in DtResult::cap
    uint32_t cap_total;                    // samples of all captures
};
int dt_compile(const DiagTest &t, const DtPins &pins, uint32_t tpus256, DtProgram *p, DtErr *err);

// What the result FIFO delivers over the whole run, in order: the slot of each word (a loop repeats its body). The runner pops the FIFO while the engine waits at a wait_user and while it runs, so the
// 512 words of the hardware FIFO bind per segment (the steps between two wait_user), the words of the whole run are bound by DT_WORDS_MAX.
// Returns the number of words; -1 when the run pushes more than DT_WORDS_MAX; -2 when one segment alone pushes more than 512 (*seg = its number from 1, *seg_line = the line of the wait_user that
// opens it, 0 for the first segment).
#define DT_WORDS_MAX 1024
#define DT_FIFO_HW   512
int dt_plan(const DiagTest &t, int8_t slot_of[DT_WORDS_MAX], int *seg = 0, int *seg_line = 0);

// Result of a run.
enum { DT_PASS = 0, DT_FAIL = 1, DT_NOFLIP = 2, DT_NODATA = 3 };
enum { DT_PR_NONE = 0, DT_PR_OK, DT_PR_MISSED, DT_PR_SKIPPED };
struct DtResult {
    char status[16];                   // done, aborted, timeout, refused, error
    char reason[DT_STR];               // refused, error: why
    uint32_t tpus256;
    uint32_t fifo[DT_WORDS_MAX]; int nfifo;
    int8_t slot_of[DT_WORDS_MAX];
    int expect;                        // the words the step list pushes
    uint32_t timeout_ticks[DT_SLOTS];  // per time_to_edge slot: a value this large is 'no edge'
    const DtProgram *prog;             // the compiled test the values belong to (the pins of the evals, the capture slots)
    const DtPins *pins;                // the table of the bitstream (names in the files)
    uint32_t cap[DT_CAP_MAX]; int ncap;    // the capture samples read back from the ring, per slot at prog->cap_off
    int refused;                       // the engine's REF_CNT: steps it refused (not drivable, guard, ...)
    int refused_word;                  // REF_PC, when refused > 0
    bool guard_trip;                   // a guarded output was released because GAME or EXROM went low
    uint8_t prompt_state[DT_PROMPTS];
    uint8_t verdict[DT_EVALS];
    int npass, nfail;
};

// The value an eval looks at (a sample slot's field, the raw word otherwise); false when the slot has no such value.
bool dt_value(const DiagTest &t, const DtResult &r, const DtEval &e, int index, int64_t *out);
// Evaluates every eval of the test against the decoded FIFO.
void dt_evaluate(const DiagTest &t, DtResult *r);
// The level of a pin (its table index) in a snapshot of four words (pins 31..0, 63..32, 95..64, 127..96).
static inline bool dt_snap_bit(const uint32_t w[4], int pin) { return (w[(pin >> 5) & 3] >> (pin & 31)) & 1; }

// The two files of a run. Returned length, -1 when it does not fit.
int dt_write_toml(const DiagTest &t, const DtResult &r, const char *core_version, const char *stamp, char *out, int cap);
int dt_write_text(const DiagTest &t, const DtResult &r, const char *core_version, const char *stamp, char *out, int cap);

// ---- the run ----
enum { DUI_CONTINUE = 0, DUI_SKIP, DUI_TIMEOUT, DUI_ABORT };
class DiagUi {
public:
    virtual ~DiagUi() { }
    // T201: a question of an A/V test; the answer is DUI_CONTINUE for YES, DUI_SKIP for NO, DUI_TIMEOUT, DUI_ABORT. The default is the prompt.
    virtual int question(const char *title, const char *text, int idx, int total, int timeout_s) { return prompt(title, text, idx, total, timeout_s); }
    // A question with a text the test rewrites (T196 S7): `text` is the test's buffer, live(ud) refills it about once a second while the question is open, the dialog
    // changes the text in place. The answer as question(); the default asks once.
    virtual int question_live(const char *title, const char *text, void (*live)(void *), void *ud)
    {
        (void)live; (void)ud;
        return question(title, text, 1, 1, 0);
    }
    // T201 v2: a question with a choice of answers (items[0..n-1]); DUI_CONTINUE with *pick = the index, DUI_TIMEOUT, DUI_ABORT. The default asks it as YES / NO: YES picks 0, NO picks the last item.
    virtual int choice(const char *title, const char *text, int idx, int total, int timeout_s, const char *const *items, int n, int *pick)
    {
        (void)items;
        int a = question(title, text, idx, total, timeout_s);
        *pick = a == DUI_CONTINUE ? 0 : n - 1;
        return a;
    }
    // A prompt of the test; the answer is DUI_*. `timeout_s` 0: waits for ever.
    virtual int prompt(const char *title, const char *text, int idx, int total, int timeout_s) = 0;
    virtual void progress(const char *text) = 0;
    // The readable result; returns when the person has read it.
    virtual void show(const char *title, const char *text) = 0;
};

// ---- T201: the knobs of the A/V test picture (one row of the A/V panel each, one key of an A/V test file each; the panel file, panels/avout.toml, defines them: panel.h) ----
#define DT_AV_N 24
enum { AVK_STD = 0, AVK_GROUPS, AVK_BITS_A, AVK_BITS_B, AVK_BITS_C, AVK_VSIGN, AVK_TRIM, AVK_BSTART, AVK_BEND, AVK_BAMP, AVK_GAIN, AVK_SYNC, AVK_BLANK, AVK_WHITE,
       AVK_EQ, AVK_VLEN, AVK_VPOS, AVK_LINES, AVK_MAP, AVK_MODE, AVK_PCH, AVK_RCH, AVK_SYNCN, AVK_FB };      // T259: the last six are registers 060-065 in this order
#define DT_AV_ABSENT (-100000)          // a preset file does not name the knob
#define DT_AV_BADVAL (-100001)          // ... names a label the knob does not have

struct DiagEnv {
    CoreIo *io;

    CoreFs *fs;
    DiagUi *ui;
    uint32_t (*now_ms)(void);
    void (*delay_ms)(uint32_t);
    bool (*stamp)(char out[16]);       // "YYYYMMDD-HHMMSS" from the RTC; false: none, a counter is used
    const char *core_version;
    const char *folder;                // "/SD/cores/diag"
    uint32_t run_timeout_ms;           // one test without a prompt open; 0: 60 s
    uint32_t counter;                  // the stamp when there is no RTC
    // T201, an A/V test: the application's hooks onto the A/V panel. av_get: the label index the page holds for a knob; av_set: puts a knob into the gateware (the standard also
    // sets the System Mode) without touching the page's stored value; av_done: puts the page's own values back after the walk. Null in a run of a probe test.
    // av_panel gives the A/V panel file's rows (panel.h; the first DT_AV_N rows are the walk's knobs, in the order of the AVK_ enum), or null with the reason in `why`.
    const Panel *(*av_panel)(void *ud, char *why, int n);
    int (*av_get)(void *ud, int knob);
    void (*av_set)(void *ud, int knob, int idx);
    void (*av_done)(void *ud);
    void (*av_freeze)(void *ud, bool on);    // T201 v2: the marker stands still while a question is open (register 053 bit 1); av_done clears it as well. May be null.
    void *av_ud;
    // T175: a test with menu_input = "keyboard" switches the joystick ports off as menu keys from its start until its result dialog (on = false), and back on. May be null.
    void (*joy_menu)(void *ud, bool on);
    void *joy_ud;
    // T196 S7, a test with kind = "xpad": the XInput pad's counters. xpad_begin sets the poll interval and the ack switch and zeroes the counters, xpad_reset zeroes them,
    // xpad_text writes the counters as lines of text, xpad_end puts the production values back. Null in an application with no XInput driver.
    void (*xpad_begin)(void *ud, int poll_ms, bool ack_input);
    void (*xpad_reset)(void *ud);
    int (*xpad_text)(void *ud, char *out, int cap);
    void (*xpad_end)(void *ud);
    void *xpad_ud;
};

struct DiagOutcome {
    int ntests, npass, nfail, nrefused, nerror;
    char last_toml[DT_PATH + 32], last_txt[DT_PATH + 32];
    char msg[200];                     // one line for the status
};

// Runs a test or suite file (a path on the card). 0 when it ran to the end (verdicts may still fail), else a reason in outcome->msg.
int diag_run_file(DiagEnv *env, const char *path, DiagOutcome *out);
// The bitstream's pin table, read through the registers once and kept for the live page (null when the core has none). diag_pins_forget() drops it (another core was loaded).
const DtPins *diag_pins(CoreIo *io);
void diag_pins_forget(void);
// The live page: the snapshot of the bank (PD_SNAP) as four words.
void diag_live(CoreIo *io, uint32_t w[4]);


#endif
