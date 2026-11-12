// CoreIo on the real core window (T93 D5). Board code.
#ifndef COREAPP_HW_H
#define COREAPP_HW_H

#include "io.h"
#include "board.h"
#include "partial.h"

CoreIo *hw_io(void);

// The write-back service's time, critical section and interrupt line on this board (writeback.h).
class WbPort;
WbPort *hw_wb_port(void);

// The host's ICAP loader and decoupler for Reload core (partial.h): the registers coreboot.cc drives, from this CPU.
PartialPort *hw_partial(void);

// The reads the board profile is made of (board.h); board_detect() takes it.
BoardProbe *hw_board_probe(void);

// The four ID bytes at the core window, read before anything else is known about the core.
void hw_read_id(uint8_t id[4]);

// A request to the ESP32 control module (rpc_calls.h: power off 0x0F, reboot 0x13, power-on mode 0x16/0x17, wake on Wi-Fi 0x18, Wi-Fi ...): the answer's bytes
// (header included) in reply, their number returned; 0 when no answer was asked for (timeout_ms 0), -1 none came in time, below that no link. Board code.
int hw_esp32_rpc(uint8_t cmd, const uint8_t *args, int nargs, uint8_t *reply, int maxreply, int timeout_ms);

// The host's menu button (ITU button 1), true once per press.
bool hw_menu_button_pressed(void);

// Must run once before the button is polled, and only from a task: it brings the ESP32 link up the way Gideon's
// application does (his DmaUART and its interrupt), which is what makes the rocker's events reach the host at all.
// It then proves the path to itself over the UART's loopback and prints the result (hw.cc, spec 5.13).
void hw_rocker_init(void);

// T93 D13: reloads the FPGA from the flash at address 0 through the ICAP (IPROG with WBSTAR 0), the end state of a power cycle: the host
// app's boot menu comes back. Does not return on success; false and a reason when the bitstream has no ICAP loader. The sequence
// is the standard 7-series one (UG470); the IPROG return is an open item of T79 and has not run on a board.
bool hw_return_to_host(char *msg, int n);

// The rocker's link to the ESP32 stands still (its UART driver and interrupt are off): before the application is left for other firmware.
void hw_rocker_quiesce(void);

// The receive DMAs stand still: RMII's receiver off, the ESP32 uart_dma's armed receive address dropped (flowctrl bit 7, soft_reset). Called with the
// interrupts off as the last step before a jump into other firmware that this one has copied over its own heap: a frame received after it would land there.
void hw_net_rx_stop(void);

// T135 D6: releases the host's expansion port lock (icap region +0x21, bit 0). The lock is set at power-on and by every IPROG back to the host; a large
// core's flat build carries the same lock, so its core application releases it before the machine runs. For a layer-2 core the host did it already
// (it is one bit). No ICAP loader in the bitstream: nothing to release, nothing written.
void hw_release_expansion_port(void);

#endif
