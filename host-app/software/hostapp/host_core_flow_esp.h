// The ESP32 flows as the core runs them (T152 D8/S8/S9; T192.2 P2): looking into a .cfw/.ue2 for its ESP32 image set (read only) and writing that set into the
// ESP32 (U on the inspection page). The board's flash is never touched. The pages are data; the texts and the order of the keys are the old function's.
#ifndef HOST_HOSTCORE_FLOW_ESP_H
#define HOST_HOSTCORE_FLOW_ESP_H

#include "host_core_flow.h"

struct HcEspEnv {
    const HcFlowEnv *base;
    void (*yes_key)(void *ctx, int key);        // the key the service takes as "yes" for the page drawn next (the screen resets it after one draw)
    void *ctx;
};

void hc_inspect_run(const HcEspEnv &env, const char *path);
class HcJob;
HcJob *hc_inspect_job(const HcEspEnv &env, const char *path);     // the same as a job: the caller steps it (host_core_job.h) and deletes it
int hc_esp_writes(void);                        // writes into the ESP32 started (the bench's counter)
const char *hc_inspect_line(void);              // the first line of the last inspection page (the bench reads it)

#endif
