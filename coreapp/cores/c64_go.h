// "Start at this address" for the C64 (T104.2 D6): the CPU's program counter set directly, so a program that was loaded over the zero
// page, the stack or the keyboard buffer - which a typed SYS needs intact - runs all the same. It is the path a debugger takes
// (TRX64-Ultimate's monitor, hw_cpu_write, T21 5.1.1): the debug block stops the machine on an
// instruction boundary, the CPU's flip-flops are staged through the state bus with PC and the fetched opcode replaced, and they load
// at the next edge the gate lets through. It needs CAPS2 b2; without it the caller types SYS.
//
// Pure: everything goes through CoreIo's register window, so tests/coreapp/ runs it against a model of the state bus.
#ifndef COREAPP_C64_GO_H
#define COREAPP_C64_GO_H

#include "io.h"

// The bitstream has the CPU write path and the pages it is made of (CAPS b1 state bus, b3 debug, CAPS2 b2).
bool c64_go_available(CoreIo *io);

// The place hook of c64_go: called with the machine stopped on an instruction boundary and the debugger holding it, to write the
// program into RAM. 0 or a reason in msg.
typedef int (*c64_go_place_t)(void *user, char *msg, int msglen);

// Stops the machine on the next instruction boundary, calls `place` (null: nothing to place), points the CPU at `pc` and runs it. A
// failure on the way lets the machine go again and hands the debugger back: C64_STOP works as before. Returns 0, or a reason.
int c64_go(CoreIo *io, uint16_t pc, c64_go_place_t place, void *user, char *msg, int msglen);

#endif
