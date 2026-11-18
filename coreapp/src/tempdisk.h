// The Temp RAM disk and the file manager's temp folder settings (tempdisk.cc).
#ifndef COREAPP_TEMPDISK_H
#define COREAPP_TEMPDISK_H

void tempdisk_init(void);                                     // mounts "Temp" in the file manager's root (once)
void tempdisk_set(bool auto_cleanup, bool subfolders);        // "Temp Auto Cleanup", "Temp Subfolders"

#endif
