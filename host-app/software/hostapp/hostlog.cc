// The firmware-only half of the host app's bug report (T162): the log ring (D2: the last bytes the app printed; the linker wraps outbyte) and the
// board's name. hostreport_live.cc and the rest of the report are pure and run in the benches.
#include "hostreport_live.h"
#include "product.h"
#include <stdint.h>

const char *hostreport_board(void)
{
    return getProductString();
}

// ---- the log ring: every byte the application prints goes through outbyte(); the linker wraps it (Makefile --wrap=outbyte) ------------
#define LOG_N 24576
static char g_log[LOG_N];
static uint32_t g_head;
static bool g_wrapped;

extern "C" void __real_outbyte(int c);
extern "C" void __wrap_outbyte(int c)
{
    if (c != '\r') {
        g_log[g_head++] = (char)c;
        if (g_head == LOG_N) {
            g_head = 0;
            g_wrapped = true;
        }
    }
    __real_outbyte(c);
}

int hostlog_tail(char *out, int max)
{
    uint32_t have = g_wrapped ? LOG_N : g_head;
    uint32_t take = have < (uint32_t)max ? have : (uint32_t)max;
    uint32_t from = (g_head + LOG_N - take) % LOG_N;
    for (uint32_t i = 0; i < take; i++) {
        out[i] = g_log[(from + i) % LOG_N];
    }
    return (int)take;
}
