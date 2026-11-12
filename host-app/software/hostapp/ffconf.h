// FatFs configuration of the host application: the Ultimate application's (chan_fat/full/ffconf.h, a submodule we do not edit) with
// f_expand switched on. It is found before the original because the Makefile puts -I software/hostapp first (the core app, whose
// VPATH lists chan_fat/full first, keeps the original). FF_USE_EXPAND only adds the function; no structure changes (T115 D2 flash file).
#include "../chan_fat/full/ffconf.h"

#undef FF_USE_EXPAND
#define FF_USE_EXPAND 1
