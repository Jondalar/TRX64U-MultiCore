#ifndef NETSHIM_TASK_H
#define NETSHIM_TASK_H
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
int xTaskCreate(TaskFunction_t fn, const char *name, unsigned stack_words, void *arg, unsigned prio, void *handle);
void vTaskDelete(void *task);
void vTaskDelay(unsigned ticks);
#endif
