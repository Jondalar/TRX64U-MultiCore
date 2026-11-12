// Host-test stub (T164 D1): the tick counter of rtc_dummy.cc, driven by the test.
#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
#define configTICK_RATE_HZ 1000
