// A stand-in for FreeRTOS and lwIP on the Mac, just enough for coreapp/src/nettransport.cc: a task is a detached thread, the critical section one mutex,
// a socket a scripted connection (test_nettransport.cc). Not the board's headers - the test puts this directory first on the include path.
#ifndef NETSHIM_FREERTOS_H
#define NETSHIM_FREERTOS_H
#include <stdint.h>
#include <stddef.h>
#define configMINIMAL_STACK_SIZE 1600
#define portTICK_PERIOD_MS 1
#define tskIDLE_PRIORITY 0
#define pdPASS 1
#define pdTRUE 1
typedef int BaseType_t;
typedef unsigned TickType_t;
void netshim_enter(void);
void netshim_exit(void);
#define portENTER_CRITICAL() netshim_enter()
#define portEXIT_CRITICAL() netshim_exit()
#endif
