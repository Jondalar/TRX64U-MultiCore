// Start-up time stamps for the console (T101 S9): "[t+1234 ms] text", the time since the scheduler started, in ticks (5 ms
// at 200 Hz). The lines the boards printed before carried none, so nobody could say where the seconds before the first picture went.
// Both applications use it for their own start-up lines and one line per phase; the vendored firmware lines in between stay as they are.
#ifndef TRX_STARTLOG_H
#define TRX_STARTLOG_H

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"

static inline unsigned startlog_ms(void) { return (unsigned)(xTaskGetTickCount() * portTICK_PERIOD_MS); }

#define STARTLOG(...) do { printf("[t+%u ms] ", startlog_ms()); printf(__VA_ARGS__); printf("\n"); } while (0)

#endif
