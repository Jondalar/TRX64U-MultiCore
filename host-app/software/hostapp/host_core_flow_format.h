// The format of a medium as the core runs it (T111, T192 D23): refusals, the two-step confirmation with the typed name, the format with its progress page and
// the result page. The pages are data (HcPage); the flow runs to its end on the UI task and reads its answers through the environment.
#ifndef HOST_HOSTCORE_FLOW_FORMAT_H
#define HOST_HOSTCORE_FLOW_FORMAT_H

#include "host_core_flow.h"

// What the screen adds to the flow's environment.
struct HcFormatEnv {
    const HcFlowEnv *base;
    int (*boot_rows)(void *ctx, const char *medium);     // boot menu rows whose file lives on the medium
    void (*begin)(void *ctx);                            // the first page is about to come up (no refusal): the screen blocks everything else
    void (*end)(void *ctx);                              // the last page is gone: the screen closes it and lets go
    void *ctx;
};

enum HcFormatOutcome {
    HCFO_REFUSED,         // a note was set, nothing was shown
    HCFO_NOT_CONFIRMED,   // the person left the confirmation, nothing was written
    HCFO_RAN              // the format ran (whatever its result): the medium may have changed under the screen
};

// name: the root entry's name as the browser lists it.
HcFormatOutcome hc_format_run(const HcFormatEnv &env, const char *name);
class HcJob;
HcJob *hc_format_job(const HcFormatEnv &env, const char *name);        // the same as a job: the caller steps it (host_core_job.h) and deletes it
HcFormatOutcome hc_format_outcome(const HcJob *finished_job);          // how a finished format job ended
int hc_format_runs(void);                                // formats started (the bench's write counter)
const char *hc_format_why(void);                         // the reason text of the last format

#endif
