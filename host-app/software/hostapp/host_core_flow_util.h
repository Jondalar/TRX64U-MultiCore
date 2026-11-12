// What every flow of the core shares (host_core_flow.cc): the refusal page, the reading page, the restart page, the abort key, the waiting for a key and the sentence and
// percent texts of the pages. The texts are the old hostui.cc's, word for word.
#ifndef HOST_HOSTCORE_FLOW_UTIL_H
#define HOST_HOSTCORE_FLOW_UTIL_H

#include "host_core_flow.h"
#include "host_core_job.h"
#include "fwconfirm.h"

namespace hcf {

void flog(const HcFlowEnv &e, const char *fmt, ...);                      // one console line ("[host] ..."), printf-style
void hook(const HcFlowEnv &e, int phase, uint64_t a, uint64_t b);         // the bench's probe point (a no-op on the board)
void draw(const HcFlowEnv &e, const HcPage &pg, bool force = true);
void idle(const HcFlowEnv &e);                                            // between keys: the service runs
void sleep_ms(const HcFlowEnv &e, int ms);
const char *base_of(const char *path);
const char *fw_pct(char *out, int n, unsigned pct);                       // "37 %"
const char *fw_sentence(char *out, int n, const char *why);               // a reason as a sentence
const char *fw_why_line(const char *why);                                 // the second line of a refusal after the check
FwcKey fw_key_class(int k);
bool abort_key(const HcFlowEnv &e);                                       // ESC, RUN/STOP or LEFT is pressed (reads the keys)
// A page that says no, before anything is written: amber, one reason, then "Nothing was written. The flash is as it was." (a job draws it and waits for the key itself)
void fw_refuse_draw(const HcFlowEnv &e, const char *title, const char *l0_in, const char *l1, const char *l2);   // the same page, without the wait (a job waits itself)
// The page of a file being read and checked; the context of the staging callbacks (txu_stage, fw_restore_stage, fw_file_sha, ...).
struct ReadUi {
    const HcFlowEnv *env;
    char title[72];
    char what[96];
};
void read_cb(void *ctx, uint32_t done, uint32_t total);                  // FwStageProgress for a ReadUi
bool abort_cb(void *ctx);                                                // abort callback for a ReadUi

// ---- the two shared waits of the firmware flows, as steps -------------------------------------------------------------------------------------------
// The two questions ("Y", then RETURN within its time): build the page with `page(pg, ask, confirm, now)`, then step until `done` (`go`: the second RETURN came in its time).
typedef void (*AskPageFn)(HcPage &pg, const void *ask, const FwConfirm &c, uint32_t now);
struct AskSub {
    const HcFlowEnv *env;
    const void *ask;
    AskPageFn page;
    bool go;
    void begin(const HcFlowEnv &e, const void *a, AskPageFn fn)
    {
        env = &e;
        ask = a;
        page = fn;
        go = false;
        pc = 0;
    }
    bool step(HcJob &j);                      // true when the answer is in
private:
    int pc;
    FwConfirm c;
    HcPage pg;
    FwcState hooked;
    uint32_t shown_left;
};
// A written firmware or update starts with the next power-up (T152 D11): the page counts down, then the board is asked to power-cycle itself.
struct RestartSub {
    void begin(const HcFlowEnv &e, const HcPage &p, int ln, const char *w)
    {
        env = &e;
        pg = p;
        line = ln;
        what = w;
        pc = 0;
    }
    bool step(HcJob &j);                      // true when the page was left
private:
    const HcFlowEnv *env;
    HcPage pg;
    int line;
    const char *what;
    int pc;
    uint32_t t0, t1;
    int shown;
    const char *why;
};

}  // namespace hcf

#endif
