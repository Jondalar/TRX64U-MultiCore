// Just enough of FreeRTOS for coreapp/src/netfs.cc on the Mac (tests/loader/run.sh puts this directory on the include path): a tick count of 0.
#ifndef LOADER_NETFSSTUB_FREERTOS_H
#define LOADER_NETFSSTUB_FREERTOS_H
#include <stdint.h>
#define portTICK_PERIOD_MS 1
typedef unsigned TickType_t;
#endif
