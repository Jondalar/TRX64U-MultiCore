// The conversion of a .cfw/.ue2 into a large .cor, as the core runs it (T137 D9; T192.2 P2): the pages are data, the order of the pages and keys is the old
// hostui.cc function's. It reads the firmware file and writes one new file "<medium>/cores/<name>.cor"; it writes no flash.
#ifndef HOST_HOSTCORE_FLOW_CONVERT_H
#define HOST_HOSTCORE_FLOW_CONVERT_H

#include "host_core_flow.h"

// What the conversion needs beyond HcFlowEnv: the icon of the firmware (2048 bytes, the screen's side knows where it comes from) and the block buffer.
struct HcConvertEnv {
    const HcFlowEnv *base;
    const uint8_t *(*icon)(void *ctx, const char *path, const char *name);
    uint8_t *(*buf)(void *ctx, uint32_t *len);
    void *ctx;
};

// true: the .cor was written and checked; its path is in dst_out (n >= FO_PATH_MAX). Every refusal is a page of its own and returns false.
bool hc_convert_run(const HcConvertEnv &env, const char *path, char *dst_out, int n);
class HcJob;
HcJob *hc_convert_job(const HcConvertEnv &env, const char *path);     // the same as a job: the caller steps it (host_core_job.h) and deletes it
const char *hc_convert_dst(const HcJob *finished_job);                // the .cor a finished convert job wrote (its path)
int hc_convert_runs(void);                  // conversions started (the bench's counter)
const char *hc_convert_why(void);           // the first line of the last refusal or failure (the bench reads it)

namespace hcf {
// A page that says no, before anything is packaged (also the inspection's refusal): amber, the reason, then "Nothing was packaged."
void cv_refuse_draw(const HcFlowEnv &e, const char *title, const char *l0, const char *l1);     // the same page without the wait
}

#endif
