// A flow of the host as a job that is stepped (T192.2 P2, T192 D23, T192.2-S3/S4): `step(input)` runs the flow from where it waited to the next place where it waits again
// (for a key, for the clock) or to its end, and says which. The page it drew last is data (`page`), the state names where the flow is (probing, asking, running, result).
// A flow that waits for an answer does nothing until one arrives: a wait with no key (from the step or the environment) changes nothing and writes nothing.
//
// The flows are the straight-line code they were, with each wait a place where the function returns and, at the next step, continues (HJ_* below: the lines of a
// function are the labels; everything a flow keeps across a wait is a member of its job). The old blocking call (hc_update_run, ...) makes the job and steps it to its end
// on the calling task, with the environment's idle and sleep between the steps exactly where the old wait loops had them. No LVGL, no FreeRTOS.
#ifndef HOST_HOSTCORE_JOB_H
#define HOST_HOSTCORE_JOB_H

#include <stdint.h>
#include <string.h>
#include "host_core_flow.h"

enum HcJobState {
    HCJ_PROBING = 0,        // reading and checking: nothing is written, nothing is asked yet
    HCJ_ASKING,             // a question is up: it waits for the answer
    HCJ_RUNNING,            // the work: from the first write on, no key stops it (can_cancel false)
    HCJ_RESULT,             // the page that says how it ended (or why nothing was done) waits for a key
    HCJ_DONE
};

struct HcJobIn {
    int key;                // a key for the wait the flow is at; -1: none (a wait then asks the environment's key source, as the old wait loops did). The first step starts the flow and takes none.
};

class HcJob {
public:
    explicit HcJob(const HcFlowEnv &e) : state(HCJ_PROBING), ok(false), can_cancel(true), pages(0), kind(0), env(e), pc(0), in_(), in_used_(false)
    {
        memset(&page, 0, sizeof(page));
        in_.key = -1;
    }
    virtual ~HcJob() { }
    // One step. Returns true when the flow has ended (`ok` says whether it did what it was asked to).
    bool step(const HcJobIn &in);
    bool done() const { return state == HCJ_DONE; }

    int state;              // HcJobState
    bool ok;                // DONE: the flow wrote (or made) what it was asked to
    bool can_cancel;        // a key still ends it (false from the first erase on)
    uint32_t pages;         // pages drawn
    int kind;               // what the screen calls it
    HcPage page;            // the page drawn last

    void note_page(const HcPage &p);                        // called by the page drawing helper
    int poll_key();                                          // this step's key once, then the environment's key source (blocking-style: the service runs)
    int poll_key_nowait();                                   // the same, with no service pass
protected:
    virtual bool run() = 0;                                  // from `pc` to the next wait (false) or to the end (true)
    const HcFlowEnv &env;
    int pc;
    bool finish(bool okv)
    {
        ok = okv;
        state = HCJ_DONE;
        pc = -1;
        return true;
    }
    int jin_key() const { return in_.key; }

private:
    HcJobIn in_;
    bool in_used_;
};

// Runs a job to its end on the calling task (the old blocking call). Deletes it. Returns its `ok`.
bool hc_job_run(HcJob *j);

// The job being stepped now (null outside a step): the page helpers hand their pages to it.
HcJob *hc_job_current(void);

// ---- the waits of a flow: lines of its run() ------------------------------------------------------------------------------------------------
// A flow's locals that live across a wait are members; a variable declared with an initialiser may not stand between the begin and a label. Each wait has its own line.
#define HJ_BEGIN() switch (pc) { case 0:;
#define HJ_END() } return finish(ok)
// the flow waits here (a loop's back-edge); the next step continues after it
// a sub-step (a struct with its own step(job)) runs until it has its answer
#define HJ_SUB(sub) do { pc = __LINE__; case __LINE__:; if (!(sub).step(*this)) { return false; } } while (0)
#define HJ_YIELD() do { pc = __LINE__; return false; case __LINE__:; } while (0)
// a page is up and waits for a key: every pass says so to the bench's hook, asks the key source once, and idles and sleeps when there is none (the old wait_key)
#define HJ_WAIT_KEY(var) do { pc = __LINE__; case __LINE__:; hcf::hook(env, -1, 0, 0); var = poll_key(); if (var < 0) { hcf::idle(env); hcf::sleep_ms(env, 20); return false; } } while (0)

#endif
