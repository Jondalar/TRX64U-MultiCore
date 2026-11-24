// The test files of the diag core (T175 section 3 D4, section 6 Q1, Q2) and the run that turns one into measurements: the TOML is parsed and compiled to the
// probe engine's step list (cores/portdiag_regs.h), the engine runs, the result FIFO is decoded, the thresholds of the file are evaluated, and two files are
// written - the raw result as TOML and a readable report. Pure: the engine is a CoreIo, the card a CoreFs, the person a DiagUi, so
// tests/coreapp/test_diagtest.cc runs all of it against a model of the engine.
#ifndef COREAPP_DIAGTEST_H
#define COREAPP_DIAGTEST_H

#include <stdint.h>
#include "io.h"
#include "fsif.h"

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

enum { DT_OP_END = 0, DT_OP_DRIVE, DT_OP_WAIT, DT_OP_SAMPLE, DT_OP_EDGES, DT_OP_TIME_TO_FLIP, DT_OP_PADCTL, DT_OP_LOOP, DT_OP_WAIT_USER };
enum { DT_SLOT_SAMPLE = 1, DT_SLOT_EDGES = 2, DT_SLOT_TIME = 3 };
enum { DT_F_RAW = 0, DT_F_PINS, DT_F_CMP, DT_F_TS, DT_F_FLAGS };

struct DtPrompt {
    char id[DT_NAME];
    char text[DT_STR];
    int timeout_s;                     // 0: waits for ever
};

struct DtStep {
    uint8_t op;
    uint8_t level;                     // drive: 0 low, 1 release
    uint8_t sel;                       // edges: the pin; time_to_flip: the CMP mask
    uint8_t in_us;                     // `n` is microseconds (else clocks)
    uint16_t pad;                      // padctl: arg bits
    uint16_t body;                     // loop: the following steps
    uint32_t count;                    // loop: repeats
    uint32_t mask;                     // drive: pin mask
    uint32_t n;                        // wait, edges: duration; time_to_flip: timeout
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

// The step list of a test: one word per step plus END. `tpus256` is the engine's clock in ticks per microsecond x 256 (the registers PD_TICKS); times in the
// file are microseconds. 0 on success, else -1 with the reason.
struct DtProgram {
    uint32_t words[256];
    int n;
    uint16_t word_of[DT_SRC_STEPS];        // the word each source step starts at (an edges step is a MASK and an EDGES)
    uint32_t timeout_ticks[DT_SLOTS];      // per slot of kind TIME
};
int dt_compile(const DiagTest &t, uint32_t tpus256, DtProgram *p, DtErr *err);

// What the result FIFO holds, in order: the slot of each word (a loop repeats its body), -1 for none. The number of words, -1 when more than the FIFO holds.
int dt_plan(const DiagTest &t, int8_t slot_of[512]);

// Result of a run.
enum { DT_PASS = 0, DT_FAIL = 1, DT_NOFLIP = 2, DT_NODATA = 3 };
enum { DT_PR_NONE = 0, DT_PR_OK, DT_PR_MISSED, DT_PR_SKIPPED };
struct DtResult {
    char status[16];                   // done, aborted, timeout, refused, error
    char reason[DT_STR];               // refused, error: why
    uint32_t tpus256;
    uint32_t fifo[512]; int nfifo;
    int8_t slot_of[512];
    int expect;                        // the words the step list pushes
    uint32_t timeout_ticks[DT_SLOTS];  // per time_to_flip slot: a value this large is 'no flip'
    uint8_t prompt_state[DT_PROMPTS];
    uint8_t verdict[DT_EVALS];
    int npass, nfail;
};

// The value an eval looks at (a sample slot's field, the raw word otherwise); false when the slot has no such value.
bool dt_value(const DiagTest &t, const DtResult &r, const DtEval &e, int index, int64_t *out);
// Evaluates every eval of the test against the decoded FIFO.
void dt_evaluate(const DiagTest &t, DtResult *r);
// The pins of a sample or the live vector as text: "P1 U1 D1 L1 R1 F1  P2 U1 D1 L1 R1 F1  CMP 0101".
void dt_pins_text(uint32_t live14, char *out, int n);

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

// ---- T201: the knobs of the A/V test picture (one row of the A/V page each, one key of an A/V test file each) ----
#define DT_AV_N 24
enum { AVK_STD = 0, AVK_GROUPS, AVK_BITS_A, AVK_BITS_B, AVK_BITS_C, AVK_VSIGN, AVK_TRIM, AVK_BSTART, AVK_BEND, AVK_BAMP, AVK_GAIN, AVK_SYNC, AVK_BLANK, AVK_WHITE,
       AVK_EQ, AVK_VLEN, AVK_VPOS, AVK_LINES, AVK_MAP, AVK_MODE, AVK_PCH, AVK_RCH, AVK_SYNCN, AVK_FB };      // T259: the last six are registers 060-065 in this order
struct AvKnob {
    const char *key;                   // the row's name on the page and in the settings file
    const char *const *labels;         // the values in user terms, as the page shows them and a test file names them
    uint8_t n, def;
    const int16_t *val;                // the register value of each label (the trim: the signed byte)
    int16_t lo, hi;                    // T259: the register's whole range, what the playground accepts
};
extern const AvKnob dt_av_knobs[DT_AV_N];
// T259: a knob by its key ("v_sign" or "V sign"), -1 if none; a preset file's text into one register value per knob (DT_AV_ABSENT: the file does not name it, DT_AV_BADVAL: a label the knob does not
// have; a value "#n" is a free register value); the label of a knob that has exactly this register value, -1 if none.
#define DT_AV_ABSENT (-100000)
#define DT_AV_BADVAL (-100001)
int dt_av_knob_of(const char *key);
void dt_av_preset_parse(const char *text, int32_t raw[DT_AV_N]);
int dt_av_idx_of_raw(int k, int32_t raw);

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
    // T201, an A/V test: the application's hooks onto the A/V page. av_get: the label index the page holds for a knob; av_set: puts a knob into the gateware (the standard also
    // sets the System Mode) without touching the page's stored value; av_done: puts the page's own values back after the walk. Null in a run of a probe test.
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
// The live line of the idle page: reads PD_LIVE.
uint32_t diag_live(CoreIo *io);

#endif
