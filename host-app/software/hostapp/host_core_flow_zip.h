// Unzip in the File Browser (T197 P3): a .zip is unpacked by the same unzip.u2a the cores run (apps/unzip), loaded from <medium>/cores/apps and "/SD/cores/apps" by the loader
// of coreapp/src (appindex, libload); its library miniz is linked into the host app (hostlibslot.cc, base 4.0: no library file is loaded). The flow is a job like the others
// (host_core_job.h): it finds the application, asks which of the three actions, loads the application, runs it and shows how it ended. What the application asks while it works
// (message, choose, the bar) is drawn as pages of this flow. No LVGL, no FreeRTOS: the machine behind the loader (heap, instruction cache, the call into the image) and the files
// are the environment's.
#ifndef HOST_HOSTCORE_FLOW_ZIP_H
#define HOST_HOSTCORE_FLOW_ZIP_H

#include "host_core_flow.h"
#include "fsif.h"
#include "libload.h"

struct HcZipEnv {
    const HcFlowEnv *base;
    CoreFs *fs;                    // the files (the board: fm_fs())
    LibHost *libhost;              // heap, code placement and the call into an image (the board: hostzip_board.cc; a bench: its own)
    void *rig;                     // the loader, the registry and the index: made on the first use, kept
};

class HcJob;
HcJob *hc_zip_job(HcZipEnv &env, const char *path);               // the caller steps it (host_core_job.h) and deletes it
void hc_zip_rig_free(HcZipEnv &env);                               // the bench's end of a scenario (the board keeps the rig)
int hc_zip_runs(void);                                             // applications started (the bench's counter)
const char *hc_zip_why(void);                                      // the first line of the last refusal or failure

#endif
