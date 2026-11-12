// bench: the one RTOS call mediaglue.cc makes (the free heap the work buffer is taken from)
#pragma once
#include <stdint.h>
extern uint32_t bench_free_heap;
static inline uint32_t xPortGetFreeHeapSize(void) { return bench_free_heap; }
