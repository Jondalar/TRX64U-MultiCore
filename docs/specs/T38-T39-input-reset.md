# T38/T39 — App keyboard/joystick injection and the C64 reset interrupt

**Status:** spec 2026-10-03, branch `t38-input`. **Tasks:** TODO.md T38, T39.
**Milestone:** M1 (drop-in for Gideon's core: what his app does through these registers works).
**Shares with:** T44 §8a (the flash unlock reads the *physical* keys only), T28 (§8: `irq_in(7)`),
T16 (the cartridge port's reset sense pin, later).

## 1. Goal

- T38: what the app writes to `MATRIX_KEYB` (0x10100300) and `C64_JOY1/2_SWOUT`
  (0x10180030/31) reaches the C64's CIA 1, together with the C64U's own keyboard, joysticks
  and RESTORE. Today both are only RAM in `ultimate_sys.vhd`'s register pages.
- T39: the app gets its C64 reset interrupt (ITU `irq_in(7)`) and the reset sense bit
  (`C64_CLOCK_DETECT` bit 4). Today `irq_in(7)` is 0 and the bit reads 0.

Non-goals: paddles and mouse (0x10180032-37), `MATRIX_WASD_TO_JOY` (0x1010030B) and
`U64II_KEYB_JOY` (0x10100406, port swap): their semantics live in Gideon's closed U64 core and
the app's defaults leave them off (WASD 0, swap 0 for `CFG_JOYSWAP` 0). See §7.

## 2. What the app does (read from the code, `software/ultimate-app/software`)

**MATRIX_KEYB** (`system/u2p.h:19`, base `U2P_IO_BASE + 0x300` = 0x10100300), written byte
by byte by `Keyboard_USB::applyMatrixState` (`io/usb/keyboard_usb.cc:216-230`):

| Byte | Content |
|---|---|
| 0-7 | `matrix[c]` bit `r` = key at CIA 1 PA column `c`, PB row `r` pressed (1 = down). From `usb2matrix`: code `n` → `out[(n & 0x38) >> 3] |= 1 << (n & 7)`; RETURN (USB 0x28) is code 0x01 = column 0, row 1. USB keys and REST taps (`rest_matrix_state | rest_matrix_overlay`) ORed by the app |
| 8 | bit 0: reset (`usb_reset`: all four left modifiers Ctrl+Shift+Alt+Win, `keyboard_usb.cc:404`) |
| 9 | bit 0: RESTORE (USB F12, REST `restore`/tap with restore, held `hold_ticks`) |
| 10 | bit 0: freeze (USB F11) |

The app writes all of them on every state change and zeros 0-7 in `setMatrix`; with the menu
open (`matrixEnabled` false) it writes its USB part as 0, REST injection stays.
REST: `POST /v1/machine:input {"events":[{"kind":"keyboard","inputs":["a"],"transition":"tap"}]}`
(`api/route_input.cc:821`); `{"kind":"joystick","port":2,"inputs":["fire"],...}`.

**Joystick** (`system/u64.h:137-138`): `C64_JOY1_SWOUT` (+0x30) = port 1, `C64_JOY2_SWOUT`
(+0x31) = port 2, written by `JoystickOutput::apply` (`io/c64/joystick_output.cc:130-131`) as
`port | 0xE0`: bits 4:0 = up, down, left, right, fire, **0 = pressed**. Released = 0xFF; the
app never reads them back. Port 1 is CIA 1 PB ($DC01), port 2 PA ($DC00).

**Reset interrupt** (`portable/riscv/riscv_main.c:85-94`): on ITU pending bit 7 the trap
handler calls `ResetInterruptHandlerCmdIf` and `ResetInterruptHandlerU64`. Both are linked
on the C64U (`command_intf.cc`, `u64_config.cc` in `target/u64ii/riscv/ultimate/Makefile`;
the T28 §8 remark that the weak empty one is used is wrong and is corrected with this task):

- CmdIf (`io/command_interface/command_intf.cc:29-36, 98-109`): enables bit 7
  (`ITU_IRQ_ENABLE`), and on a reset aborts the UCI command in flight, tells the command
  targets (`c64_reset_pending`) and resets the sampler.
- U64 (`u64/u64_config.cc:1048-1078, 2141-2144`): re-applies the registered settings (SID
  mapping, sockets, mixer, UltiSIDs, joystick/paddle registers), except once after
  `SidAutoConfig` (`skipReset`).

So the interrupt does real work: without it a reset by the user leaves a running UCI command
and the SID player's mapping in place. ITU bit 7 is edge triggered (`g_edge_init` "10000101"
in our ITU instance, `itu.vhd:237-241`: a rising `irq_in(7)` sets the flag).

On Gideon's U2+ (`fpga/fpga_top/ultimate_fpga/vhdl_source/ultimate_logic_32.vhd:498, 531,
1469`) `irq_in(7)` is the spike-filtered, inverted RESET pin of the cartridge port: every C64
reset, whoever pulls it (the Ultimate's own C64_MODE reset included). `C64_CLOCK_DETECT`
bit 4 (`cart_slot_registers.vhd:108`, `status.reset_in`) is the same signal; the app polls it
once after releasing reset (`u64_config.cc:675-677`, `while (c64_reset_detect())`).

## 3. Design

### 3.1 ultimate_sys.vhd (cpu_clk)

- Latches, written by the same page writes that fill the read-back RAM:
  `app_keys` (64) from page 1 bytes 0-7, `app_reset`/`app_restore`/`app_freeze` = bit 0 of
  bytes 8/9/10; `app_joy1`/`app_joy2` = **not** bits 4:0 of page 3 bytes 0x30/0x31 (active
  high pressed, released after reset). All cleared by the RISC-V reset.
- Out as ports, level, active high. The **physical** matrix input `keys` stays what it is
  (C64U keyboard only, for the app's own scan through KEYB_COL/KEYB_ROW and for T44 §8a).
- In: `c64_reset_sense` (level, any clock), two flip-flops into cpu_clk, to `irq_in(7)` and
  to `C64_CLOCK_DETECT` bit 4.

### 3.2 app_input.v (new, sys_clk = C64 clock)

Two flip-flops for every app bit (they change at most every few ms; bits of one write
arriving a clock apart are harmless, as with the physical keys), then the merge:

```
keys    = (ovl_own ? 0 : phys_keys) | app_keys
joy1/2  = phys_joy1/2 | app_joy1/2
restore = phys_restore | app_restore
reset   = app_reset            (ORed into c64_reset_req in top.v)
freeze  = app_freeze           (to the cartridge model's freeze input)
```

All active high = pressed, so the OR is the wired AND of the active-low lines: a key or
direction is down if either side holds it, nobody wins. `ovl_own` keeps gating only the
physical keys (the app zeros its own USB part itself while its menu is open; a REST tap is
meant to reach the C64). Ghosting: the app's keys go into the same `ext_keys` matrix model as
the physical ones (`cores/icepi/c64_top.v`, one-level pull between PA and PB), so both get the
same treatment; a joystick on port 1 shows up as phantom keys during the KERNAL scan, as on a
real C64.

### 3.3 top.v

- `app_input` between the physical paths and `c64_core`: its `keys/joy1/joy2/restore` go to
  the core in place of `keys_s2`/`pins_s2`; `keys_s2`, `keys_cpu_s2` and `pins_s2` keep the
  physical state for the app's scan, the freeze responder and the debug registers.
- `c64_reset_req = machine_ctl[0] | app_s2[0] | app_reset` (held while byte 8 is set).
- Reset sense: `core_reset_req` (what reaches the C64's chips: JTAG, C64_MODE, USB reset;
  deferred inside a freeze session as today) into `ultimate_sys.c64_reset_sense`. When T16
  wires the port's reset sense pin (P20, active high), it is ORed in there.

### 3.4 c64_core.v / cart_slot.v

`freeze` input passed to `c64_cartridge.freeze` (today tied 0): the icepi model takes a
rising edge (`c64_top.v:1457`, `c64_keyboard.v:193-206`), so byte 10 held = one push.

## 4. Simulation

- `sim/input/run.sh` (GHDL): `ultimate_sys` with a BFM CPU (as `sim/usb`): byte writes to
  0x10100300-0A and 0x10180030/31 appear on the ports with the right polarity; a pulse on
  `c64_reset_sense` sets ITU pending bit 7 once (edge), `C64_CLOCK_DETECT` bit 4 follows the
  level.
- `sim/input/build_core.sh` (Verilator): `c64_core` + `app_input` (as `sim/uci`): the KERNAL
  boots to READY; app matrix taps type `A`, then a short line; RESTORE through byte 9 calls
  the NMI vector; a 6510 loop at $C000 reads $DC00/$DC01 with PA released: app joystick bits,
  physical bits and both together arrive (wired AND).

## 5. Board test (if the NAS bitstream is up)

`POST /v1/machine:input` taps type into BASIC; a joystick event changes `PEEK(56320)`;
`PUT /v1/machine:reset` logs "U64 reset handler" on the console (the U64 handler's printf).

## 6. LUT cost

About 80 LUT expected: the OR into the 64 key bits and 11 joystick/restore bits (may fold into
the matrix model's LUTs), the write decode, the CLOCK_DETECT bit. Flip-flops: 75 latches,
150 synchronizers, 2 for the reset sense. Yosys estimate in §8.

## 7. Open

- Paddles/mouse 0x10180032-37, `MATRIX_WASD_TO_JOY`, `U64II_KEYB_JOY` swap: closed-core
  semantics, not built (needs a TODO entry; ID to be assigned on merge).
- `C64_CLOCK_DETECT` bit 5 (NMI sense) still 0.
- Port reset sense pin (T16) to be ORed into `c64_reset_sense`.

## 8. As built (branch `t38-input`, 2026-10-03)

- `ultimate_sys.vhd`: ports `app_keys`, `app_reset`, `app_restore`, `app_freeze`, `app_joy1/2`
  (out), `c64_reset_sense` (in). Latches written beside the page RAM (read-back unchanged),
  cleared by the RISC-V reset. `app_restore` = byte 9 bit 0 **or C64_MODE bit 4** (the app's
  NMI, `C64_MODE_NMI` in `monitor_debug_u64.cc`, `monitor_file_io.cc`, `c64.cc:678`; it was
  latched but went nowhere either). `irq_in(7)` and `C64_CLOCK_DETECT` bit 4 = the sense after
  two flip-flops.
- `app_input.v` (new, in `build.tcl`), in `top.v` as `ain`; `c64_reset_req` gets
  `c64_app_reset`; `c64_core`'s `keys/joy1/joy2/restore` come from it, plus a new
  `freeze_btn` port. `keys_s2`, `keys_cpu_s2` (ultimate_sys `keys`, the KEYB_ROW scan, T44) and
  `pins_s2` (freeze responder, debug registers) are still the physical state only.
- `cart_slot.v`: input `freeze`, rising edge in its clock (gclk) to `c64_cartridge.freeze`.
- `ultimate_sys.c64_reset_sense` = `core_reset_req` (freeze_resp's output to the C64).

**Simulation** (`designs/c64_basic/sim/input/run.sh`):

- GHDL `tb_input` (ultimate_sys, BFM CPU): matrix bytes 0-7, 8/9/10, MODE NMI, the 32-bit
  WASD store on 0x30B-0x30E leaves 0-10 alone, joysticks 0xEF/0xEB → `10000`/`10100`, page
  read-back; reset sense: CLOCK_DETECT 0x01 → 0x11 → 0x01, ITU active bit 7 on the rising
  edge only (clear while held stays clear), set again by the next pulse. PASS.
- Verilator `tb_input_core` (c64_core + app_input, KERNAL): READY after 2.2 s; app taps type
  `A`; with `ovl_own` and a physical X held, DEL + `PRINT1+1` RETURN → ` 2` (the X stays out);
  $DC00/$DC01 with PA released: app fire/up EF/FE, plus physical left/right EB/F6, physical
  only FB/F7, released FF/FF; app RESTORE and physical RESTORE each one NMI; the reset byte
  holds the CPU (loop stops) and lets go. PASS, 3.2 s simulated, 58 s wall.

**LUT estimate** (yosys `synth_xilinx`, pessimistic): `app_input` 75 LUT (64 LUT3 for the
keys, but top.v already had 64 LUTs for `ovl_own ? 0 : keys_s2`, so net about 11), the
ultimate_sys latches modelled in Verilog 85 LUT (yosys builds the CE/reset muxes in LUTs;
Vivado uses the flip-flops' CE/R pins). Net expected 30-100 LUT.
**Measured** (CI run 282, `t38-input-8306032f`, post-route): total 25,314 LUT against 25,346 on
main-latest (place-and-route noise, net about 0); `ain` (app_input) 42 LUT, 154 FF;
`ultimate_sys` 6,132 (main 6,136). Timing met (WNS +0.015 ns, WHS +0.051 ns).

**Board** (2026-10-03, C64U, bitstream `t38-input-8306032f` in SRAM, app trx64/main 45be87f6
over JTAG):

- `PUT /v1/machine:reset`: the console prints "U64 reset handler. Resetting Settings" (T39:
  ITU bit 7 reaches `ResetInterruptHandlerU64`; also once at boot).
- `POST /v1/machine:input` keyboard taps type `PRINT 1+1` RETURN into BASIC: ` 2`, READY.
- Joystick events, read back with `machine:readmem` $DC00/$DC01: idle 7F/FF; port 2 fire 6F;
  port 2 left+up 7A; port 1 fire PB EF (and the KERNAL scan types phantom row-4 keys while it
  is held, as on a real C64); released 7F/FF.
- `run_stop`+`restore` tap after `$D020` ← 0: the border is back to 14 and the screen
  cleared (RESTORE through MATRIX_KEYB byte 9 to the NMI).
- Not tested on the board: USB keyboard (none attached), the freeze byte (no freezer
  cartridge), the reset byte (needs Ctrl+Shift+Alt+Win on a USB keyboard), the physical keys
  with app keys at the same time (nobody at the machine).

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

### T38

Keyboard matrix and joystick injection from the app

Milestone M1, status ✅. Open (one line, as in the TODO table): Paddles/mouse, WASD-to-joystick, port swap -> T47

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- Task text: Keyboard matrix and joystick injection from the app (spec docs/specs/T38-T39-input-reset.md): MATRIX_KEYB and joysticks merged into the core (wired AND), physical matrix kept separate for T44 §8a, app RESTORE/NMI, freeze byte to the cartridge; board-tested 2026-10-03 (REST taps, joysticks, RESTORE). Not built: paddles/mouse, WASD-to-joystick, port swap -> T47

### T39

C64 reset sense interrupt for the app

Milestone M1, status ✅. Open (one line, as in the TODO table): Port's P20 reset sense ORed in with T16

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- Task text: C64 reset sense interrupt for the app: irq_in(7) and CLOCK_DETECT b4 from the core reset; board-tested 2026-10-03 (U64 reset handler runs). OR the port's P20 reset sense in with T16
