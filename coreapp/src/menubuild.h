// The menu's descriptor (T187): the core's own descriptor plus the items the application adds to every core, in the one order the board shows them.
// run.cc and every bench that shows "what the board shows" call this; nothing builds its own chain.
#ifndef COREAPP_MENUBUILD_H
#define COREAPP_MENUBUILD_H

#include "desc.h"
#include "picture.h"

// out = *desc with Picture (and Scale, Smoothing), Volume, Persist medium mounts, Home Directory / Enter Home on Startup, the hotkey map and, for a core
// that names the capability i2c_switches, the I2C switches (T252, i2csw.h) appended;
// `store` holds the merged settings and has to outlive `out`. An extension that has no room or does not apply is named on stdout ("[core] <name>: <why>")
// and skipped; the return value is the first reason, null when every extension went in. If Picture is refused, `out` starts as a copy of *desc.
const char *menu_build(const CoreDesc *desc, CoreDesc *out, Setting *store, const PictureEnv *env);

#endif
