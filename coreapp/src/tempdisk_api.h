// The export "tempdisk" (major 1): the Temp RAM disk and the file manager's temp folder settings, the shared temp RAM disk library of T239.3 D12 (today coreapp/src/tempdisk.cc;
// the base mounts "Temp" itself at start, tempdisk_init in coreapp.cc). A library of this repository puts the table into LibRecord::exports (base 3.2); the TRX64-Ultimate
// base library (its menu's "Temp Auto Cleanup" and "Temp Subfolders" items) finds it with SvcLib::find("tempdisk", 1, &size). Not a member of LibApi.
//
// Rules: `size` first; members only appended inside a major. No call takes a CoreCtx. Task: lock (the call is made from the apply of the two settings, a handler).
#ifndef COREAPP_TEMPDISK_API_H
#define COREAPP_TEMPDISK_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TempDiskApi {
    uint32_t size;
    // Sets the file manager's two switches, set_temp_auto_cleanup_enabled and set_temp_use_cache_subfolder_enabled: the values of the settings "Temp Auto Cleanup" and
    // "Temp Subfolders". The caller makes the call again on every change of either setting. The Temp disk itself is mounted by the base at the start.
    void (*set)(bool auto_cleanup, bool subfolders);
} TempDiskApi;

#ifdef __cplusplus
}
#endif

#endif
