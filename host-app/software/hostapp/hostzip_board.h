// The machine behind the loader of applications on the host (T197 P3): the FreeRTOS heap, the instruction cache sweep and the call into a placed image, the same
// as the core application's libboard.cc, and the files over the FileManager (coreapp/src/fm_fs.cc). Board code; the flow is host_core_flow_zip.cc.
#ifndef HOST_HOSTZIP_BOARD_H
#define HOST_HOSTZIP_BOARD_H

#include "host_core_flow_zip.h"

HcZipEnv *hostzip_env(const HcFlowEnv *base);       // the one environment of the board (made on the first call)

#endif
