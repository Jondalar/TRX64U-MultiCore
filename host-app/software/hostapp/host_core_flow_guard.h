// T265 P2 (S3): the repair of the host application after the recovery program started it (host_core_flow_guard.cc). The job shows that the application in the flash was
// replaced by another firmware's updater, checks that this application fits the host in the flash, writes it back (header last, read back), then the recovery copy and the
// guard record. No LVGL, no FreeRTOS: a page, a key, the flash layer.
#ifndef HOST_HOSTCORE_FLOW_GUARD_H
#define HOST_HOSTCORE_FLOW_GUARD_H

#include "host_core_flow.h"
#include "guard.h"

class HcJob;
// stage: where the recovery program left the image (gi.stage_addr); the caller deletes the job. ok: the application, the copy and the record are written.
HcJob *hc_repair_job(const HcFlowEnv &env, const GuardInfo &gi, const uint8_t *stage);
// the medium of the recovery block as words for a page: "the flash drive", "the SD card", "a USB stick"
const char *hc_repair_medium(uint32_t medium);

#endif
