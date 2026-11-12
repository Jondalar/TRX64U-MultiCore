// CoreFs over Gideon's FileManager (SD, USB, Flash). Board code; tests use their own CoreFs.
#ifndef COREAPP_FM_FS_H
#define COREAPP_FM_FS_H

#include "fsif.h"

CoreFs *fm_fs(void);

#endif
