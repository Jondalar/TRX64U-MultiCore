// Restoring the original firmware as a flow of the core (T137 P2/P3, D4-D7; T192.2 P2): the check page, the two questions, the write, the result. The rules are fwrestore.cc's
// (who may be restored, the check before the first erase), flashfw.cc's (the write) and fwconfirm.cc's (the two questions); this is the order of the pages.
#ifndef HOST_HOSTCORE_FLOW_RESTORE_H
#define HOST_HOSTCORE_FLOW_RESTORE_H

#include "host_core_flow.h"

struct HcRestoreEnv {
    const HcFlowEnv *base;
    bool (*file_stat)(void *ctx, const char *path, uint32_t *size, uint32_t *mtime);     // the directory entry of a file (size, FAT date << 16 | time)
    void (*refused)(void *ctx, const char *first_line);                                  // a refusal page was drawn (the bench reads the reason)
    void (*write_failed)(void *ctx, const char *why);                                    // a write failed (the bench reads the reason)
    void *ctx;
};

// Returns true when the firmware was written and verified. Every refusal is a page of its own and returns false.
bool hc_restore_run(const HcRestoreEnv &env, const char *path);
class HcJob;
HcJob *hc_restore_job(const HcRestoreEnv &env, const char *path);     // the same as a job: the caller steps it (host_core_job.h) and deletes it
int hc_restore_writes(void);                                  // writes started (the bench's counter)

#endif
