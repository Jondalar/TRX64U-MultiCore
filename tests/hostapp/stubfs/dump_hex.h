// bench: disk.cc dumps partition tables to the console on the board; here it does not
#pragma once
#include <stdint.h>
static inline void dump_hex_relative(const void *, int) { }
static inline void dump_hex(const void *, int) { }
