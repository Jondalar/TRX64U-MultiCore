# T47 — Paddles, 1351 mouse, joystick swap, WASD-to-joystick, NMI sense

**Status:** spec 2026-10-03, branch `m1-specs`. **Task:** TODO.md T47. **Milestone:** none (Extra, won't do for now; Alex 2026-10-09).
**Builds on:** T38/T39 (`T38-T39-input-reset.md`, built: `app_input.v`, joystick injection, reset sense).
**Shares with:** T37 (POT of real SIDs is the board's analog line), T25.1 (POT in the UltiSID engine), T16 (port NMI pin).
Evidence: code only (app `software/ultimate-app/software`, our RTL, Gideon's XDC in the tools fork). **H** = hypothesis, to be
settled by a measurement on Gideon's bitstream (M1-M7); his closed core is the reference, measured on it wins.

## 1. Goal

Everything the app does through the five "input" registers beyond T38 works on our core:

| Register | Address | What |
|---|---|---|
| `C64_PADDLE_1_X/1_Y/2_X/2_Y` | 0x10180032-35 | injected POT values per port (USB mouse as 1351, extra joystick buttons) |
| `C64_MOUSE_EN_1/2` | 0x10180036/37 | per-port: the injected POT values are valid |
| `C64_PADDLE_EN` | 0x10180013 | "Paddle Override" (config item, default Enabled) |
| `C64_PADDLE_SWAP` | 0x1018001A | POT ports swapped (follows "Joystick Swapper") |
| `U64II_KEYB_JOY` | 0x10100406 | write: swap/port select; read: joystick lines for the menu scan |
| `MATRIX_WASD_TO_JOY` | 0x1010030B (32 bit) | WASD keys act as a joystick |
| `C64_CLOCK_DETECT` bit 5 | 0x10040003 | NMI line sense |

Today all of them are RAM or constants (T38 §7): POT reads 0xFF, KEYB_JOY reads 0xFF, WASD does nothing, bit 5 reads 0. Non-goal for phase P1: **real
paddles in the ports** (the board's comparators), see §4.6 and P2.

## 2. What the app does (code)

### 2.1 Joystick Swapper (`u64_config.cc`)

`CFG_JOYSWAP` "Joystick Swapper", six values (`:269`): Normal, Swapped, WASD Port 2, WASD Port 1, WASD P2 No KB, WASD P1 No KB (the item is limited to 0..1 unless U64-II,
`:357-359`; greyed out unless `isEliteBoard()`, `:946`). `effectuate_settings` (`:1080-1095`), with `swap` = the value:

| Register | Written |
|---|---|
| `C64_PADDLE_EN` | `CFG_PADDLE_EN` (`:1087`; item "Paddle Override", default 1, `:425`, shown in "Joystick Settings", `:2966`) |
| `C64_PADDLE_SWAP` | `swap & 1` (`:1088`) |
| `U64II_KEYB_JOY` and `keyb_joy_select` | `swap & 1` (`:1091`) |
| `MATRIX_WASD_TO_JOY` and `wasd_to_joy` | `{0x00, 0x00, 0x01, 0x01, 0x03, 0x03}[swap]` (`:1092-1093`): bit 0 = WASD acts, bit 1 = "No KB" |

The hotkey `swap_joystick()` (`:2424-2440`) writes the same three (`U64II_KEYB_JOY`, `C64_PLD_JOYCTRL` (U64 v1 only), `C64_PADDLE_SWAP`) and `keyb_joy_select`.
The menu notes "hold [CTRL] to type the W, A, S, D and RETURN characters" (`:2983-2985`, UI strings: the key list is **H** until M5).

`MATRIX_WASD_TO_JOY` is forced to 0 while the app scans the keyboard for its own menu (`io/c64/keyboard_c64.cc:254`), restored at the end of the scan (`:363, :375`), and
set to 0 while REST injects keys (`io/usb/keyboard_usb.cc:253`). It is a 32-bit store at an unaligned address; only the low byte is used.

### 2.2 `U64II_KEYB_JOY` as the menu scan uses it (`io/c64/keyboard_c64.cc:268-345`)

The menu scan writes `0xFF` to `U64II_KEYB_COL`, reads `U64II_KEYB_JOY` twice and takes `row & 0x1F` as the joystick (active low: up, down, left, right, fire). With the overlay
the register is also **written** to look at the other port: `U64II_KEYB_JOY = keyb_joy_select ^ 1`, read, write back `keyb_joy_select` (`:300-309`); the code compares the read value
with the REST-injected state of "port 1" when `keyb_joy_select & 1` and of "port 2" otherwise (`:281, :302`). So: **a write of 0/1 selects which control port the next reads show** (1 = port 1, 0 = port 2), and
the read includes the REST/USB injected state, i.e. it is what the CIA sees on that port (**H**, M1). Whether the same write also swaps the physical connectors for the CIAs ("Swapped") is **H** (M1).

### 2.3 POT injection (`io/c64/joystick_output.cc`, `io/usb/usb_hid.cc`)

`JoystickOutput::apply` (`joystick_output.cc:120-137`) writes, on every state change:

| Register | Value |
|---|---|
| `C64_JOY1/2_SWOUT` | T38 |
| `C64_PADDLE_1_X/1_Y` | port 1: mouse position if a mouse is active and no extra button is down, else `joystick_potx/poty_value(port1)` (`:393-411`) |
| `C64_PADDLE_2_X/2_Y` | `joystick_potx/poty_value(port2)` |
| `C64_MOUSE_EN_1` | mouse active, or a port-1 extra button down |
| `C64_MOUSE_EN_2` | a port-2 extra button down (there is no mouse on port 2) |

- Extra buttons: joystick mask bit 5 (fire2) -> POTX, bit 6 (fire3) -> POTY (`:59-74`), value **0x80 released, 0x00 pressed** (`:63-64`); from REST and USB gamepads.
- Mouse: `usb_hid_set_mouse1_position` (`usb_hid.cc:81-95`) scales the counts by 2 ("a 1351 steps the POT value by two for each count", `:77-80`) and the pacer
  (`mouse_pot_pacer.h:138-139`) outputs `position & 0x7F`: a 7-bit value, steps of 2. Per-frame step limit 63 (`MAX_STEP`), 24 ms window (`mouse_pot_pacer.h`).
  Mouse buttons are ordinary port-1 lines: left = fire, right = up, middle = down (`usb_hid.cc:915-925`), delivered as `C64_JOY1_SWOUT`.
- `usb_hid_apply_mouse_output_enable` (`usb_hid.cc:335-342`): no mouse interface left -> `C64_MOUSE_EN_1` = 0, position cleared, port 1 lines released.
- The recovery app writes the mouse position straight to `C64_PADDLE_1_X/Y` (`usb_hid.cc:89-90`).

**H (what the core does with them):** the SID's POTX/POTY of port `p` read the injected byte when `PADDLE_EN && MOUSE_EN_p`, else the board's real paddle path; `PADDLE_SWAP` exchanges the ports.
The byte is the register value the C64 reads from `$D419/$D41A` (0x80 reads as "no button", mouse 0..0x7F), because the app already applies the 1351's step of 2 itself.

### 2.4 `C64_CLOCK_DETECT` bit 5

`C64_CD_NMI_SENSE` = 0x20 (`io/c64/c64.h:95`). The only reader is a `printf` in the monitor backend that notes "NMI launch: line already low" (`monitor/monitor_debug_u64.cc:154-157`);
`c64_get_nmi_state()` (`c64.h:522`) has no caller. The bit is 1 while the NMI line is low (`!= 0` means "already low"). Gideon's pin measurement: R17 (NMI) is driven low "the whole time the CIA 2 timer NMI stays
unacknowledged ... also low while RESTORE is down" (`1541ultimate-tools/docs/c64u-pinmap.md:220`): the sense is the **level of the wired line**, not a latched request.

## 3. What our core has / lacks (code)

| Piece | State | Where |
|---|---|---|
| Latches 0x32-37, 0x13, 0x1A, 0x10100406, 0x1010030B | RAM only (page 010/011 write RAM; T38 latched keys/joystick/reset bytes only) | `ultimate_sys.vhd:978-981` (joystick), rest none |
| `KEYB_JOY` read | constant 0xFF | `ultimate_sys.vhd:1136` |
| `CLOCK_DETECT` | `{3'b000, reset_sense, 4'h1}`: b5 = 0 | `ultimate_sys.vhd:1122-1123` |
| Joystick merge, per port | `phys | app`, 5 bits | `app_input.v:28-34`, `top.v:967-972` |
| Port swap in the core | `joy_invert` = `flags[7]`, fixed 0 | `c64_keyboard.v:380-381`, `c64_core.v:21` (`C64_FLAGS`), `c64_top.v:719` |
| POT into the SID | `joy_pot_x/y`: PA6/PA7-selected 1-bit "charged" lines from joystick bits 5/6, which `ext_joy1/2` (5 bits wide, `{5'b0, ext_joy}`) never set: POT reads 0xFF | `c64_keyboard.v:383-384`, `c64_top.v:721-722, 736-737` |
| POT consumers | reDIP: charge-time model `sid_pot.sv` (512-cycle period, 256 cycles discharge, POT = count at which `pot_charged` goes high, else 0xFF), `pot_discharge` is an output; T25.1 engine: same model in `trx_sid.v:95-112`, `$D419/$D41A` only (`:125, 129`) | `cores/icepi/c64_redip_sid/sid_api.sv:102, 306`; `cores/trx_sid/rtl/trx_sid.v` |
| WASD | none | - |
| Real paddle pins | not ports: no `PADDLE_CMP[3:0]`, `PADDLE_DIS`, `PADDLE_ADJ` in `pins.xdc` | Gideon's XDC `c64u_gideon_issue615.xdc:213-217, 408, 418, 470-474` (CMP E14, L6, N5, L4, 1.8 V; DIS N22, ADJ AA20, 3.3 V) |
| NMI level | `cpu_nmi = cia2_irq | kbd_restore_toggle | (cart_nmi && cart_present) | ext_restore` (toggle = request flag until acknowledged) | `c64_top.v:1441` |

## 4. Design

### 4.1 Registers (`ultimate_sys.vhd`, cpu_clk; all cleared by the RISC-V reset)

Latches (page 011): `paddle[0:3]` (0x32-0x35), `mouse_en[1:2]` (0x36/0x37), `paddle_en` (0x13), `paddle_swap` (0x1A); page 010: `keyb_joy` (0x10100406 write), `wasd` (low byte of 0x1010030B, bits 1:0).
Outputs to `top.v` (two flip-flops into the C64 clock as `app_input` does): `pot_val[0:3][7:0]`, `pot_en[1:2]`, `paddle_en`, `swap`, `wasd[1:0]`. Reads: `KEYB_JOY` returns `{3'b111, joy_sel[4:0]}`, `CLOCK_DETECT` bit 5 = `nmi_sense` (§4.5).

### 4.2 Swap and `KEYB_JOY` (in `app_input.v`)

`swap` := `keyb_joy[0]` (and `paddle_swap`, both written with the same value; the hardware uses `keyb_joy` and ignores the redundant register). Applied to the **merged** joystick state of each port
(physical OR app) before the CIA, i.e. `joy1_c = swap ? joy2_m : joy1_m` and vice versa, which is `joy_invert` of `c64_keyboard` driven from this signal instead of `flags[7]` (**H**: Gideon swaps the physical connectors;
the REST/USB injection is by control-port number, so whether it follows the swap is M1). `KEYB_JOY` read = the merged lines of the port the register selects (1 = port 1, 0 = port 2, §2.2), after the swap, in the CPU clock
domain (two flip-flops), active low in bits 4:0, 1 in bits 7:5. The overlay scan then navigates with a joystick, which is dead today.

### 4.3 WASD-to-joystick (in `app_input.v`, on the key vector before the CIA)

With `wasd[0]`: unless CTRL (matrix index 58, `keyboard_c64.cc:22-30`) is down, the keys W (index 9), A (10), S (13), D (18) and RETURN (1) (`keymap_normal`, `keyboard_c64.cc:32-41`) are read as up, left, down, right, fire
and OR-ed into a joystick port: port 2 for index 2 (`WASD Port 2`), port 1 for index 3, i.e. by `keyb_joy[0]` (`swap & 1`: index 2 -> 0, index 3 -> 1, `:1091-1093`). With `wasd[1]` ("No KB") those five keys are removed
from the matrix vector. The directions go through the same merge as the other joystick bits and are subject to the swap of §4.2 (**H**; M4 settles both). The key vector includes the physical C64U keyboard and `MATRIX_KEYB`
(T38); the app switches the register off for REST injection (§2.1) so hardware needs no REST special case.

### 4.4 POT generator (`pot_gen.v`)

Per SID POT input (X, Y): the selected port `p` follows CIA 1 PA7/PA6 as today (`c64_keyboard.v:383-384`: PA6 = port 1, PA7 = port 2); `p' = swap ? other : p` when `paddle_swap` (**H**).
Value `V = pot_val[p][axis]` if `paddle_en && pot_en[p]`, else "no paddle" (0xFF, the current behaviour).
- reDIP build: the SID's POT counter is not visible; the generator counts phi2 cycles from the **falling edge of `pot_discharge`** (`sid_api.sv:102`) and drives `pot_charged` high from count `V` on (V = 0: high at once, reads 0; V = 0xFF or no paddle: never,
  reads 0xFF). The SID's own count then reads back `V` (to within the one-cycle detection delay of `sid_pot.sv`; M3 checks the exact offset on Gideon's core).
- T25.1 engine: `trx_sid.v` takes `pot_val_x/y` and loads `potx <= V` at `pc == 511` (replace its `pot_x` line charge detection by a direct byte; the 512-cycle phase stays).
- Only the SID that answers `$D419/$D41A` (standard address) sees it; others read 0xFF (as built, `sid_api.sv:190`, `trx_sid.v:125`).
Values the app produces (§2.3): 0x80/0x00 from the extra buttons (a pressed button reads POT 0 ... **H**: with a 7-bit mouse value and bit 7 = 0 the same path serves the mouse), 0..0x7F from the mouse. What POT a mouse driver sees when `MOUSE_EN` is 1 and bit 7 matters is M3.

### 4.5 NMI sense

`nmi_sense` = the **level** `cia2_irq | ext_restore | (cart_nmi && cart_present)` plus the port's NMI pin when T16 arms it (`slot_port.v` `nmi`, already counted in `ext_cart_nmi`), through two flip-flops, bit 5 of `CLOCK_DETECT`. Not the request flag `kbd_restore_toggle`
(it stays set until the CPU takes the NMI; Gideon's pin is the wire). The only effect on the app is one log line, so this is cheap and low risk (M6 compares the timing on the pin).

### 4.6 Real paddles (phase P2, after the measurements)

Gideon's XDC names the POT hardware: `PADDLE_CMP[3:0]` (four inputs, 1.8 V), `PADDLE_DIS` (output, 3.3 V), `PADDLE_ADJ` (output, 3.3 V); the pin map notes "POT X/Y via the LM2901" (comparators), "no paddles or mouse at hand, static candidates only"
(`c64u-pinmap.md:393`). **H:** `PADDLE_DIS` is the SID's `pot_discharge` (shorts the board's POT capacitors), `PADDLE_CMP[k]` is `pot_charged` of pot k (X1, Y1, X2, Y2), and `PADDLE_ADJ` is a PWM/threshold or the voltage source for the
injection path (so that a *real* SID in a socket also sees a mouse). If that is right, P2 adds the three pins to `pins.xdc`/`top.v`, feeds `pot_charged` from the comparators when `!(paddle_en && pot_en[p])`, and (if ADJ is the injection)
a delta-sigma voltage per injected byte. M2 (SAMPLE, no paddle needed) tests `DIS`/`ADJ` activity against injected values before a line of P2 is written. Real SID in socket + real paddle: the line is shared, only one measuring SID may drive `PADDLE_DIS` (T37).

## 5. Measurement plan on Gideon's core

| # | Measures | Needs | How |
|---|---|---|---|
| M1 | Swap semantics: with "Joystick Swapper" Normal/Swapped, where do REST-injected port 1 / port 2 fire, the physical joysticks and `U64II_KEYB_JOY` reads land (`$DC00`/`$DC01`)? Does the injection follow the swap? | REST; physical joystick (Alex) | probe PRG reading `$DC00/$DC01` per state; REST joystick events (`route_input.cc`); the register itself is read by the menu: observe the overlay with a joystick in each port |
| M2 | `PADDLE_DIS` (N22), `PADDLE_ADJ` (AA20), `PADDLE_CMP[0..3]` over time: period/duty, dependence on injected values (extra button 0x00/0x80, mouse positions), on `Paddle Override` Enabled/Disabled, on `MOUSE_EN` | USB-Blaster; REST mouse/joystick events | SAMPLE 300 captures per state; ADJ duty vs value tests H of §4.6 |
| M3 | POT value read at `$D419/$D41A` against the injected byte (0x00, 0x01, 0x40, 0x7F, 0x80, 0xFF; mouse positions), per port, with `PADDLE_EN` 0/1, `MOUSE_EN` 0/1, swap 0/1; PA6/PA7 selection; update period; value when no paddle | REST fire2/fire3, REST mouse; probe PRG | probe PRG stores `$D419/$D41A` once per frame with each PA6/PA7 setting |
| M4 | WASD: which keys, which directions, CTRL bypass, "No KB" masks, which port per setting, does the physical C64U keyboard count, do USB keyboards count | Alex at the keyboard; USB keyboard | probe PRG showing `$DC00/$DC01` and the key matrix (`$DC00` scan) while W/A/S/D/RETURN are held, per `Joystick Swapper` value |
| M5 | Same as M4 for the matrix-changes the swapper does to the *menu*: with "WASD Port 1" does the overlay navigate with WASD? | Alex | observation |
| M6 | NMI sense timing: bit 5 against a CIA 2 timer NMI (acknowledged and not), RESTORE held, a cartridge NMI | probe PRG polling `C64_CLOCK_DETECT` via the app (`u2a` snippet or REST `readmem` of 0x10040003 is not exposed: a tiny `.u2a`) | compare the bit's level with the line-level model |
| M7 | A real paddle pair and a real 1351 in the ports: POT readings, `PADDLE_*` pins | Alex's paddles/1351 if he has them | as M2, M3 |

**Questions for Alex before the measurements:** do you have paddles, a 1351 mouse, a C64U-compatible USB mouse and USB keyboard at hand (Q1)? Is a joystick in each port available (Q2)?

## 6. Test plan

**Simulation:**
1. GHDL `tb_input` (extend): the new latches, the 32-bit WASD store, `KEYB_JOY` read mux, `CLOCK_DETECT` bit 5.
2. Verilator `tb_input_core`: (a) swap: app and physical joysticks land on the other CIA port and `KEYB_JOY` follows; (b) WASD: W/A/S/D/RETURN in `keys` produce `$DC00/$DC01` bits, CTRL bypasses, "No KB" removes the keys from the matrix; (c) `pot_gen` against both SIDs: for V in {0, 1, 0x40, 0x7F, 0x80, 0xFE, 0xFF} `$D419` equals V (within the measured offset), PA6/PA7 port select, `pot_en`/`paddle_en` gating, no paddle = 0xFF; (d) NMI sense level: CIA 2 NMI, RESTORE, ack.
3. Both SID engines (reDIP default, `TRX_SID=1`).
**Board:** B1 REST fire2 on port 2: `$D41A` reads 0x00 (pressed) / 0x80; B2 USB mouse as 1351 with a mouse test program (counts follow, no jump, the pacer's limits); B3 swap: both joysticks swap, menu follows; B4 WASD on the C64U keyboard and a USB keyboard; B5 `Paddle Override` disabled: POT 0xFF; B6 bit 5 with a stuck NMI. P2: B7 real paddles and 1351 after M2/M7.

## 7. Resources (rough, not measured)

| Block | LUT | FF | BRAM | DSP |
|---|---:|---:|---:|---:|
| register latches and synchronizers (12 bytes) | 25 | 130 | 0 | 0 |
| swap muxes and `KEYB_JOY` read | 20 | 10 | 0 | 0 |
| WASD (five key taps, CTRL, mask) | 30 | 6 | 0 | 0 |
| `pot_gen` (two counters, four byte compares, port mux) | 45 | 40 | 0 | 0 |
| NMI sense | 3 | 2 | 0 | 0 |
| Total P1 | about 125 | about 190 | 0 | 0 |
| P2 (three pins, ADJ delta-sigma if needed) | about 30 | 25 | 0 | 0 |

Calibration: T38's `app_input` measured 42 LUT, 154 FF (`T38-T39-input-reset.md` §8). Baseline 25,346 LUT (`docs/lut-budget.md`).

## 8. Open questions (Alex)

1. Paddles, a 1351 and a mouse: which of them do you own and may be plugged into the C64U for M2/M3/M7? **Answered (Alex, 2026-10-03): only a USB mouse (Dell, 2 buttons + wheel, optical). No paddles, no 1351.** So the board test covers the app's USB-mouse-to-1351 injection (POT values, buttons); real paddle pins (P2) can only be measured by SAMPLE without a device.
2. Can you hold a joystick in each control port for M1/M4, and do you want WASD tested on the C64U's own keyboard (M4)? **Answered (Alex, 2026-10-03): yes, the joystick already used for the pinmap measurements (tools fork pinmap/joy1.jsonl, joy2.jsonl). WASD on the built-in keyboard: decided by measuring Gideon's core (M4), see Q5.**
3. Should the POT of the *real* paddle pins (P2) be built before M2/M7 show what `PADDLE_ADJ` does, or stay open until they do? Proposal: open. **Decided (Alex, 2026-10-03): as proposed.**
4. Is the swap meant to apply to REST/USB-injected joystick input too (M1 answers; default until then: yes, after the merge)? **Decided (Alex, 2026-10-03): as proposed.**
5. WASD on the physical keyboard: only if M4 shows Gideon's core does it (default until then: physical and `MATRIX_KEYB` alike). **Decided (Alex, 2026-10-03): as proposed.**

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

T47: Paddles/mouse, WASD-to-joystick, port swap, NMI sense

No milestone, status ❌. Open (one line, as in the TODO table): – (won't do for now)

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- **2026-10-09 night, Alex: won't do for now, moved to Extra ("T47 == won't do bis auf weiteres, ab nach Extras").** Paddles, mouse, WASD-to-joystick, port swap and NMI sense stay unwired; M1 does not wait for them.
- **2026-10-08, measured on Gideon's core (C64U 3.15, core 1.50):** a USB mouse (Mouse Mode "Mouse") is emulated as a 1351: POTX/POTY ($D419/$D41A, REST readmem) change continuously within $00-$7F while the mouse moves (e.g. 51 2A, 65 6A, 0E 76, 7C 48); $DC00 = $7F. Our core (2026-10-07): $FF/$FF (not built).
- Task text: Paddles/mouse (0x10180032-37, 1351 mouse from USB), MATRIX_WASD_TO_JOY and the U64II_KEYB_JOY port swap: latched in ultimate_sys.vhd, not wired (Gideon's behaviour is in his closed core: measure on his bitstream first); CLOCK_DETECT b5 NMI sense Spec: docs/specs/T47-paddles-mouse.md (POT generator, swap/KEYB_JOY, WASD, NMI sense; real paddles as phase 2 after measuring PADDLE_DIS/ADJ).
