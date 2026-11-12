#ifndef NETSHIM_SEMPHR_H
#define NETSHIM_SEMPHR_H
#include "FreeRTOS.h"
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks);
int xSemaphoreGive(SemaphoreHandle_t s);
#endif
