// The model the service sees (hostmodel.h) over the core's state: the rows, the fields, the texts, the cursor and the arm of a destructive row action. It draws nothing and decides
// nothing of its own: every change goes through the same functions as the board's keys (host_core_ui.h). T192 D21, T192.2 P1. No LVGL, no FreeRTOS.
#ifndef HOST_HOSTCORE_MODEL_H
#define HOST_HOSTCORE_MODEL_H

#include "hostmodel.h"

HostModel *hc_model(void);

#endif
