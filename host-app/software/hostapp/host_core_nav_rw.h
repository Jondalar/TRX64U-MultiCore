// The writable state of host_core_nav.h, for the core's own files only (the pages and the model that live beside it). The screen includes host_core_nav.h and reads;
// hostui.cc must not include this file or call hc_nav_rw (tools/check_host_cut.py rule N1).
#ifndef HOST_HOSTCORE_NAV_RW_H
#define HOST_HOSTCORE_NAV_RW_H

#include "host_core_nav.h"

HcNav &hc_nav_rw(void);
const HcNavPlatform &hc_nav_plat(void);

#endif
