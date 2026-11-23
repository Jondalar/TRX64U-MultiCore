// CoreFs over Gideon's FileManager (SD, USB, Flash). Board code; tests use their own CoreFs.
#ifndef COREAPP_FM_FS_H
#define COREAPP_FM_FS_H

#include "fsif.h"

CoreFs *fm_fs(void);

struct FastSeekApi;
// The fast seek library gives the CoreFs its table (fastseek_exports.cc, the library's bind entry). Without one an open file seeks the normal way (T239.3 P3-1).
void fm_fs_set_fastseek(const FastSeekApi *api);

#endif
