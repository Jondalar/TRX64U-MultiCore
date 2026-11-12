#pragma once
#include "FreeRTOS.h"
#include <stdio.h>
#include <stdlib.h>
// The bench's clock: it stands still while keys are handled and moves only while the application waits (vTaskDelay) - and by
// g_key_gap_ms for every key the bench hands over (T111: the confirmation has minimum and maximum times between its keys).
extern uint32_t g_bench_ticks;
static inline uint32_t xTaskGetTickCount(void) { return g_bench_ticks; }
// The bench has no clock of its own: a wait for a key that nobody queued would never end. It ends here, red (a control against a mutant
// that changes who asks what must not hang the bench).
static inline void vTaskDelay(uint32_t t)
{
    static unsigned waits;
    g_bench_ticks += t;
    if (++waits > 100000) {
        printf("FAIL the bench waits for a key that never comes\n");
        exit(3);
    }
}
