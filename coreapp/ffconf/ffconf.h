// FatFs configuration of the core application: the Ultimate application's (chan_fat/full/ffconf.h, a submodule we do not edit) with
// the cluster link map (FF_USE_FASTSEEK) switched on, for the fast seek of coreapp/src/fastseek.cc (T104 5.16). The coreapp Makefile
// puts -I coreapp/ffconf in front of everything, so this file is found before the original and before host-app's own ffconf.h.
// FF_USE_FASTSEEK adds one pointer (FIL::cltbl) to the file object; every object of the application sees the same layout.
// The path is the file's own (vendor/), not the host-app/software/chan_fat link to it: the SDK package copies the file, not the link (T239.3 D31)
#include "../../vendor/gideon/software/chan_fat/full/ffconf.h"

#undef FF_USE_FASTSEEK
#define FF_USE_FASTSEEK 1
