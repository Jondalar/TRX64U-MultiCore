// Base 3.3, group `board` (T239.3 section 5; boundary rows 4.4, 11.5), the base operations a library calls by name, and the SID sockets. The services of the board and of the
// video of the application, each a function of the base today. A call is here only with its second user named (boundary 1.8); none is a register. It joins
// LibApi as `const SvcBoard *board`.
//
// Not in the group, by decision: the UI look, the name squeeze and the keyboard joystick (settings of the generic application, svc_settings.h), the colour timing
// table (the TRX64-Ultimate library keeps its own copy), raw I2C (the one use is board.sid_socket), the REST gate (the state "net.gated").
//
// Contracts of the whole group:
//   - No call takes a CoreCtx.
//   - Task: the task of a handler, hook, poll or job; a call that waits says so. None is allowed from an interrupt.
#ifndef COREAPP_SVC_BOARD_H
#define COREAPP_SVC_BOARD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// The SID sockets' supply: the regulator of one socket (bits 1:0 of its nibble in the control byte: 00, 10, 11).
enum {
    SID_SOCKET_OFF = 0,           // 00: the socket is not supplied
    SID_SOCKET_ON = 1,            // 10: supplied
    SID_SOCKET_ON_TYPE1 = 2       // 11: supplied for a 6581 (the type 1 of the detection)
};

typedef struct SvcBoard {
    uint32_t size;
    // The board's capability bits as the FPGA reports them: the ITU capability register of the bitstream, four bytes (getFpgaCapabilities). The meaning of a bit is the
    // bitstream's; a library keeps its own constants for the bits it tests (D8). Board identity, like is_commodore. Second users: the UCI library, the base. Task: any.
    uint32_t (*fpga_caps)(void);
    // True on the board sold under Commodore's name (isCommodoreBoard, product.cc), false on the other badge of the same board. Board identity that no library can derive; a
    // single library user today (a first-run default of the menu style), kept because the base uses it too (a64_assembly.h). Task: any.
    bool (*is_commodore)(void);
    // One request to the ESP32 control module (power mode, wake on Wi-Fi, power off, power cycle, GET commands) over the board's one UART with one reply table;
    // a library cannot own it. `cmd` is the command, `args` its `nargs` bytes (at most 100). Result: with timeout_ms > 0 the number of bytes of the whole answer
    // packet copied to `reply` (at most maxreply; the first byte is the command, a 4 byte header and a 4 byte esp_err follow), -1 no answer in time, -2 nargs out
    // of range, -3 no link or no buffer. With timeout_ms <= 0 it sends and does not wait: 0 sent (the power-off commands), -3 not sent. Waits up to timeout_ms
    // and 1 s for a buffer. The caller calls media.flush_for_core first for a power off or cycle. Task: any that may block.
    int (*esp32_rpc)(uint8_t cmd, const uint8_t *args, int nargs, uint8_t *reply, int maxreply, int timeout_ms);
    // The picture layer takes a new system mode (picture_set_system): `sys` is the video mode (the index of the 6 colour timings: PAL, NTSC, ...) of the System Mode
    // setting; the layer's HDMI mode and the Picture row are picked again. Second users: c128, c64scpu, portdiag. Task: lock.
    void (*video_set_system)(int sys);
    // The video PLL for system mode `sys` with the colour clock's offset `ppm` (-100 .. 100), then the HDMI PLL reset: SetVideoPll and ResetHdmiPll as the one pair a
    // library calls. ppm 0 is the nominal clock. Single library user; kept because no library touches a PLL register and the base's video path programs the same PLL
    // (videomode.cc, offset 0). Task: lock.
    void (*video_set_pll)(int sys, int ppm);
    // The supply of one SID socket (the expander 0x40 on the 1.8 V I2C channel; the base keeps the control byte of both sockets, replaces this socket's nibble and writes
    // it under the "SID Configuration" lock, which the keyboard scan shares since T253; register 1 gets the byte, register 3 gets 0). socket 0 or 1; state SID_SOCKET_*;
    // shunt: the 1 kOhm shunt, bit 2 of the nibble; filter_caps: the socket's filter capacitor option, as the core's own setting for it holds it (0 or 1); the base inverts it into bit 3. The nibble of the
    // other socket is untouched, the control byte starts at 0 when the application starts. Result: 0 done, or nothing to do when the board has no expander;
    // < 0 a bad argument (socket outside 0..1, state outside SID_SOCKET_*), nothing written. Waits for the bus lock. Task: lock.
    int (*sid_socket)(int socket, int state, bool shunt, bool filter_caps);
    // The case LEDs' source select (CoreIo::case_led_select): the high nibble is LED 1, the low nibble LED 0, each a source code of the board. Two users: the base's
    // load light (ledload.cc) and a core's LED settings. Task: lock.
    void (*case_led_select)(uint8_t v);
} SvcBoard;

// ---- operations of the base that a library calls with SvcOps::call (no member of the group) -------------------------------------------------------------------
//   "machine.cold_reset"        hold, the reset-time settings, release: boot_cold_reset. Result: OPE_FAILED with the reason in msg.
//   "machine.put_builtin_rom"   new in 3.3. Argument "put_op" (text): the put operation of a ROM role of the descriptor (c64.put_kernal, c64.put_basic, drive.put_rom, ...).
//                               The built-in image of that role is put back through it (boot_put_builtin_rom). OPE_FAILED with the reason in msg.
//   "media.flush_for_core"      the mounted media written back first (media_writeback; its WbWhy is ignored there): OPE_FAILED with the reason in msg when one cannot be
//                               written, and the caller then leaves the machine on. The code that powers off or cycles the board calls it first.
// They run on the calling task without the executor and without taking the core lock (SvcOps::call).

#ifdef __cplusplus
}
#endif

#endif
