#ifndef LOADER_NETFSSTUB_TASK_H
#define LOADER_NETFSSTUB_TASK_H
#include "FreeRTOS.h"
static inline TickType_t xTaskGetTickCount(void) { return 0; }
#endif
