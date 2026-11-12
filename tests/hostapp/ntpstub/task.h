#pragma once
#include "FreeRTOS.h"
extern TickType_t g_ticks;
static inline TickType_t xTaskGetTickCount(void) { return g_ticks; }
