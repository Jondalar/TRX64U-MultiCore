# T21 — Machine code monitor at TRX64 level

> **SLIM (2026-10-05, docs/specs/SLIM-state-path.md):** "a stop is a freeze" (§3.3.1, T12 §14.8) no longer
> means a shadow session: the debugger's stop is the gate shut and the hold, nothing else
> (`freeze_resp.v` is `dma_win.v` now: the app's DMA window and the lens unit §3.4.1). The D page of the
> window in a stop is a bus cycle (it has side effects: the monitor's side-effect-free reads of chips go
> over the state bus and the lens, `sidefx` is what the window does anyway). `trx_dbg` takes RUN and
> steps in the next clock (no `sess_busy`); LOG_PUT is gone, the observer action LOG stays.

**Status:** the hardware part is built (2026-10-02, branch `t21-dbg`, §5.1 as built, §9.1 in
`sim/dbg`); the CPU write path (register writes, `g <addr>`) is built and simulated (2026-10-03,
branch `t21-cpuwrite`, §5.1.1); `trxmon.u2a` (§7.4, §7.5) has P2 and P5 (the T20 ring verbs,
§4.10.1). **Task:** TODO.md T21.
**Depends on:** T12 B (machine pause, `c64_core.v`, `drive_unit.vhd`), the App ABI major 2
(`1541ultimate` branch `feature/app-abi-clean`, `software/app_loader/app_api.h`).
**Shares with:** T20 (`docs/specs/T20-delta-ring.md`), T12, T16.2.

**Revision 2026-10-01 (ARCH).** The debug hardware is now part of the common design in
`docs/specs/ARCH-lean-state-access.md` ("ARCH"): CPU and chip state through the shared
state bus (side-effect free from the first phase), one exec bitmap and **two** watch units
(plus one drive comparator) instead of six comparators, two access bitmaps and a flow stack,
trace through the shared engine, and one IO block at 0x101A0000. Resume still waits for
whole-frame alignment (T12 B rule; Q4 stays open). The verbs, their syntax and
output, the UI, ABI and REST stay as specified here. Parts that are gone are marked
**(see ARCH)**.

Marks used below:

- **[TRX64 …]**: copied or adapted from our emulator, `/path/to/Development/C64/Tools/TRX64`.
  The verb set, its syntax and its output are TRX64's; this spec changes what implements them,
  not what they mean.
- **[new]**: specific to the FPGA, nothing to copy.
- **(T20)**: needs T20 (reverse step, rewind, checkpoints); left to the T20 phase.

---

## 1. Goals and non-goals

### Goals

1. A debugger for the running C64 on TRX64 Ultimate, built as a new Ultimate app
   (`trxmon.u2a`, loaded by the app loader). It is our monitor; it is not a patch of
   Gideon's firmware.
2. It uses the hardware only. The machine pause of T12 B stops the machine, and the CPU
   registers are read and written in the 6502 core itself. Steps by instruction and by cycle,
   breakpoints and watchpoints are comparators on the C64 bus (PC, memory and I/O accesses,
   raster). No byte is put into C64 memory: no BRK, no trampoline, no vector, no NMI.
3. The verbs, their syntax and their output are TRX64's (`MONITOR.md`, `crates/trx64-monitor`).
   Someone who uses `trx64cli` or C64RE's `runtime_monitor` types the same lines here and gets
   the same answers. The exception is where the hardware genuinely cannot answer; there the
   verb answers with TRX64's own sentence, "not available on this host".
4. Drive A's 6502 can be inspected and stepped as `device drive8`, and it can break.
5. The same hardware block can be driven three ways: from the app, from REST (through the
   app), and from the Mac over JTAG (`tools/trxdbg.py`).

### Non-goals

- A monitor that stops the machine by writing into C64 memory (BRK opcodes, vector hooks,
  trampolines). `trxmon` is its own monitor, at parity with the TRX64 emulator's (§3.2); the
  base firmware's built-in monitor is not part of our firmware line (§11).
- Reverse execution, rewind, checkpoints and ring dumps. These are T20; the verbs are listed
  here (§4.10) so that the command set is complete, and they stay "not available" until T20
  is built.
- Symbols and labels. TRX64 holds none ("Names — none here", `MONITOR.md`); C64RE joins them
  on. The same applies here.
- A second ISA. There is no RISC-V disassembler in the monitor; the firmware CPU has its own
  JTAG/GDB path. This follows TRX64 `docs/864-the-monitor-as-a-library.md` §10.
- Clean timing for an external IEC device during a pause (docs/machine-pause.md: it does not pause).
- Gideon's FPGA. On his bitstream the debug block is absent; the app says so and returns (§7.1).

---

## 2. What exists today (read from the code)

| Piece | Where | What it gives T21 |
|---|---|---|
| Machine pause | `designs/c64_basic/c64_core.v` (gate `ce`, `halt_req`, `halted`, `held`, `shut`), `top.v` `bg_c64` BUFGCE | Stops VIC, 6510, CIAs and TOD, SID, REU, `cart_slot` and the keyboard scan right after a phi2 falling edge, mid-instruction. While paused, a bus DMA byte opens the clock for exactly one cycle. Resume waits until a whole number of PAL frames was left out (≤ 20 ms). |
| Drive halt | `drive_unit.vhd` `halt` (tick_16/4/1k gated), `top.v` `drv_halt_s` | Drive A stops with the machine pause. |
| Control | JTAG register 9 (`top.v`: bit 0 CPU reset, 1 CPU pause, 2 RISC-V reset, 3 machine pause, 4 C64_STOP = CPU-only pause; read bit 8 = paused); the app's `C64_STOP` 0x10040001 | Pause and resume. |
| DMA window | 0x10050000 (`ultimate_sys.vhd` `r_dma`) | With `C64_DMA_MEMONLY`: the RAM through `c64_mem`'s second port, no cycle. With `C64_STOP` **or a debugger stop** (T12 §14.8, `dbg_stop`): the D page and the other bus-cycle accesses are one C64 bus cycle through the PLA, or, in a session, the shadow session's answer (ROMs, I/O, colour RAM). |
| C64 CPU | `cores/icepi/cpu6510.v` wraps Gideon's **6502N** `proc_core` (`cores/icepi/6502n`); port `$00/$01` (`pdir`, `pout_i`, `pfloat`, `fall6/7`) lives in the wrapper | All registers are plain flip-flops: `proc_registers.vhd` (A X Y S P, PC, `i_reg`, `dreg`, `adl/adh`), `proc_control.vhd` (`state`, 26 states, `sync`), `proc_interrupt.vhd` (IRQ/NMI latches). |
| Drive CPU | Gideon's `cpu6502.vhd` → the same `proc_core` (`cores/gideon/fpga/6502n`) | Same core, so one debug patch serves both CPUs. |
| Drive taps | JTAG 0x100020-0x100028 (`drive_dbg`): the last drive bus cycle {ATN, DATA, CLK, SYNC, SO, IRQ, R/W, data, address}, half track, motor, VIA 2 PB, cycle count | The start of the `drive`/`io 1541` views. |
| Raster | `c64_core` `pix_x`/`pix_y` (`pix_y` = kawari's `raster_line`, icepi patch 0004) | Raster comparator, no new patch needed. |
| Overlay | `overlay.v` (Gideon's char generator regs, 4 K screen + 4 K colour RAM, one colour index transparent) | Monitor text over the HDMI picture. |
| App ABI v2 | `app_api.h`: `sys ui fs c64 media config diag` tables, `U2P_HAS`, REST `PUT /v1/apps/{id}:run` with a 4 KB `reply` | Screen, keys, files, DMA, REST entry. |
| `apps/c64re` (removed from M1, A13) | `1541ultimate/apps/c64re/src/c64re.c` | A `.c64re` writer that leaves out the 6510 registers because "they are on no bus". With ARCH's state bus (chip 1) they are. |

What does not exist: CPU register access, any comparator, a cycle or instruction counter, a
side-effect-free view of I/O registers, and a stop that a running app can wait for.

### 2.1 What the machine pause costs a debugger

- A pause stops the machine mid-instruction at whichever phi2 edge comes next. A debugger
  needs the **instruction boundary** (§3.3), so the debug block needs its own stop points.
- The first bus DMA byte during a pause lets the CPU run to a read cycle and holds it there
  with RDY (`held`). After that, every byte costs the machine one cycle: VIC, CIA timers and
  SID move. This is real DMA behaviour and correct for the firmware's freeze. For a debugger,
  any read through the bus disturbs the machine. So reads use the zero-cycle paths (RAM port, the state bus of ARCH §5) and touch the bus
  only on request (`sidefx on`). Every
  bus cycle spent is counted (`DMA_CYCLES`, ARCH §10 page 0).
- The firmware's `C64::freeze()` (`software/io/c64/c64.cc` `backup_io`) is invasive: it goes
  Ultimax, reads the VIC registers, writes `VIC_CTRL`/`BORDER`/`BACKGROUND`, mutes the SIDs,
  copies 3 KB of RAM out and its own charset in at $0800. The monitor must never be entered
  through a freeze. It enters through a plain pause: `C64_STOP` / `MENU_C64_PAUSE`, which the
  loader already uses for REST launches (`app_loader.cc:251-270`).

---

## 3. Architecture

**(see ARCH §5-§7, §10.)** The first draft's `trx_dbg.v` / `trx_dbg_drv.vhd` register file
at 0x101C0000, its CPU state port, comparator units, access bitmaps, raster comparator,
flow stack, trace client and DDR2 high window are replaced by:

| Need | ARCH part |
|---|---|
| CPU registers (C64 and drive), mid-instruction view, writes at an IB | state bus chips 1 and 9 (ARCH §5.4), writes staged and applied on the next opening edge |
| PAUSE_CB/IB, STEP_INSTR/CYCLES, RUN_CYCLES, counters, `CYC_CMP`, halt sources | gate and stops (ARCH §7.1) |
| `bk` | exec bitmap 64 K × 1 (ARCH §7.2) |
| watchpoints, `n`, `ret`, `until`, `rb`, hardware part of observers | two watch units with source select (CPU address or raster), range, data/mask, one register qualifier, PREV_RET, chain, PASS/HITS (ARCH §7.2); everything else soft |
| drive breakpoint and step | one drive exec comparator, step, halt |
| flow frames, `focus`, `sf`, `nf` | software: from trace records or by soft stops |
| side-effect-free `io`, `reu`, `pot`, `io 1541`, colour RAM | state bus, colour RAM port B |
| trace ring | engine stream 3 (ARCH §6.1) |
| trace above 64 MB (the REU memory is inside the app's 64 MB since T60) | engine `COPY` into the app's memory (no high window) |
| session scratch | a firmware-owned 2 KB buffer (`trx->scratch`) |
| register map, JTAG mirror | one IO block, pages 0, 1, 3 (ARCH §10) |

The firmware stays minimal (per the "firmware minimal, features in apps" rule). It gets
the `trx` table only for what an app cannot do itself: blocking on the stop interrupt,
releasing ownership when an app returns or crashes, the drive's line-cache flush and the
firmware's own reset path. Every verb lives in the app.

### 3.2 Our own monitor, at parity with the TRX64 emulator

`trxmon` is a monitor of its own. Its verbs, views and output follow the monitor of the TRX64 emulator
(Rust) one to one (§4), so a session reads the same on the board and in the emulator. Registers come
from the core, stops come from the gate (§3.3), and the debugger never writes C64 memory itself: no
BRK opcodes, no vector hooks, no trampolines in the machine's RAM.

### 3.3 Stop points: instruction boundary and cycle boundary [new]

The 6502N fetches the next opcode in state `fetch`, with `sync` high. During that cycle
`reg_update` (`proc_control.vhd:477`, `state = fetch and trigger_upd`) writes back the
previous instruction's result, and `pc_oper` increments PC.

- **Instruction boundary (IB)**: the phi2 falling edge that ends a `fetch` cycle in which
  no interrupt was taken (`sync = 1 and intg_i = 0`). After it, the state is `decode`. The
  previous instruction has fully retired, `i_reg` holds the next opcode, and PC (register)
  holds the next opcode in `i_reg`. The monitor reports **PC = IPC = the opcode address** at an IB stop, latched by
  the gate logic from the CPU address bus during `sync` (`IPC`, a state-bus word, ARCH §7.1); it never reports the CPU's own PC register there. Nothing of the instruction
  has executed except its opcode read.
  - If `intg_i = 1` in that fetch, the fetch is a dummy cycle and an interrupt sequence
    follows. That is not an IB. The first IB after it is the handler's first opcode.
- **Cycle boundary (CB)**: any phi2 falling edge. This is where a user pause, a cycle step or
  a watchpoint lands. The monitor shows the instruction PC, the opcode, the 6502N state name
  and the cycle within the instruction (`r` adds "mid-instruction: LDA ($fb),Y at $c012,
  state zp_indir, cycle 3").
- Register writes (`r a=…`) are accepted at an IB only. Mid-instruction writes are refused
  ("mid-instruction: step to the boundary first (z)"). Writing PC at an IB is a redirect.
  **As built (§5.1.1) both cost no cycle:** the 6502N does its own update for a cycle 9 clocks
  after the phi2 fall (`phi2_l`), so at an IB stop the opcode is latched (`din_r`) but not yet
  taken, and the previous instruction's write-back to A/X/Y/S/P is still to come. The app
  stages the CPU's flip-flops (state bus chip 1 w8-w14) with the registers as that update will
  leave them (chip 1 w4/w5) and the written ones replaced, the write-back taken out (IR = NOP,
  `reg_update`, `copy_d2p`, `flags_update` off), and for a redirect PC and `din_r` set to the new
  address and the opcode there. The staging loads on the next edge the gate lets through, which
  is not one the CPU updates on: the update then takes the new opcode and goes on. VIC and
  timers do not move. (The first draft's one-cycle redirect with `state := fetch` is not
  needed.)
- The stop latency is zero cycles. Comparators evaluate during the cycle (the address and
  `sync` are valid in phi2, the data at `phi2_n`) and pull `halt` in the same `clk` where
  `xc_phi2_n` is high, which is the clk where `c64_core` decides `ce` for the next edge. If
  timing closure forces a register, the stop lands one cycle late. The stop record then sets
  `LATE`, and exec stops are made exact again by stopping at the fetch one cycle early
  (state `fetch` is known one cycle ahead from `next_state`). This is open question Q3.

### 3.3.1 A stop IS a freeze (design rule, Alex 2026-10-04)

> A breakpoint stop IS a freeze, only under exactly deterministic conditions.

There is **one** stop mechanism, not two. A breakpoint, an observer stop, a step, a cycle
compare and a pause all end in the freeze path: the whole core paused at an exact edge (the
instruction boundary, or the exact cycle the comparator names), the shadow session of T12 (§14)
over it, the same register views and lenses, the same commit list, SIDEFX; and resume continues
bit-identically, as the freeze raster test proves (0 pixels different, same IRQ cycles). Any place
where the debugger's hold differs from the freeze path is a bug to remove, not a second mode to
patch.

What is already the same: the gate (`c64_core.v` `ce`/`halted_r`, whole frames left out
and `RESUME_PHASE` on resume), the state bus, the lens unit, `sidefx` as bus cycles. What was
different (bugs, T12 §14.8 has the design):

1. `dbg_hold` started no session. A window byte in a debugger stop was a real bus cycle
   (`p_bus`): reading $DC0D or $D019 there clears flags, a write reaches the chip at once; the
   lenses read SID, IO1/IO2 and the colour RAM as `--` because there was nothing to answer them
   without a cycle.
2. A step (`dbg_go`) opened the gate with no commit before it: a session's listed writes would
   have been lost or applied late.
3. A written I/O register was not visible to a later read (no overlay).

**Behaviour required** (T12 §14.8): a debugger stop starts a session at once (same condition as a
freeze: `SH_MODE` 1 or 2); inside it the window answers from the shadows (VIC, CIA, colour RAM,
cartridge look; SID reads $FF as in a freeze), writes are held and listed; `RUN`, `STEP_INSTR`
and `STEP_CYCLES` first end the session (the list is committed as bus cycles with the machine
held) and only then release or step the gate; the stop that ends a step or a run starts the next
session. `sidefx on` sets SH_CTRL b2: the same session then answers I/O reads with real bus
cycles. Tests: a breakpoint stop equals the freeze state at that exact cycle (every window
byte $D000-$DFFF and the cartridge look equal to the read in an app freeze of the same state);
reads in the stop have no side effect; resume (and a step) is bit-identical to a run that was
never stopped.

**Built in simulation 2026-10-04** (branch t21-p4, T12 §14.8 "As built", `sim/dbgsess`): all of the above
holds for exec bitmap, PAUSE_CB/IB and step stops; `RUN`/`STEP_*` are requests that wait for the session's
end and commit (2 clocks with an empty list, 40 per listed entry), STATUS b12 shows the request. The
app side (trxmon, the monitor screen) must not assume a window read in a stop is a bus cycle any more,
and `sidefx on` is the way to get one. Board test open.

**Window gap found and closed 2026-10-05** (T12 §14.8 point 9, `sim/ultsys`). `ultimate_sys` handed
$D000-$DFFF to the C64 side only with `C64_DMA_MEMONLY` or `C64_STOP`; a debugger stop does not set
`C64_STOP` (a stop reopened by `trx->ui_request` least of all), so in a monitor stop colour RAM read
$FF and the monitor's I/O writes (`wr d020`) were acked and dropped. `sim/dbgsess` could not see it: it
drives `dma_*` into `freeze_resp` and bypasses `ultimate_sys`. Rule now: **while a debugger stop holds the
machine, the app window's D-page accesses go to the session exactly as under `C64_STOP`** (reads: the
session's shadow view incl. colour RAM, no bus cycle; writes: the session's overlay and commit list, or
colour RAM at once, reaching the chip when the machine resumes; with SH_MODE 0 a real bus cycle on the
halted machine). trxmon takes STATUS b1 (`ST_DBG_STOP`) as "held" next to b5, for `wr` and for the colour
RAM read (`hw_session_read`).

### 3.4 How the app reaches each kind of state (the "lenses")

TRX64's lenses (`MONITOR.md` "Bank lens") mapped onto paths that cost nothing whenever
possible:

| Lens / target | Path | Cycles | Side effects |
|---|---|---|---|
| `ram` | RAM port: `c64_mem` port B (DMA window with `C64_DMA_MEMONLY=1`, or JTAG 0x000000) | 0 | none |
| `cpu` | the app works out the PLA from `$01` (state port) plus GAME/EXROM (cart taps), then RAM port, the ROM block, the state bus (I/O) or the cartridge as the `cart` lens does it (§3.4.1) | 0 | none |
| `rom` | the core's own ROM BRAM through its port B (ARCH §7.8; the core does not read the firmware's windows) | 0 | none |
| `cart` | ROML ($8000-$9FFF) and ROMH ($A000-$BFFF, $E000-$FFFF) as the cartridge answers them now, whatever the PLA maps: an emulated cartridge through the hardware look (`pk_*`, the decode a bus access would use, bank and mode included, §3.4.1), a cartridge on the real port through one bus cycle with a forced select (§3.4.1, T16 §8.4) | 0 (look) / 1 (real port) | none (look); the cartridge's own for a real one that banks on a ROM read |
| `io` with `sidefx off` (default) | state bus (ARCH §5): VIC, CIA 1/2 including ICR, TOD, collisions, without side effects; IO1/IO2 of an emulated cartridge and the REU through the look (§3.4.1) | 0 | none |
| `io` with `sidefx on` | one bus cycle with a forced I/O select while the machine is paused (§3.4.1; no `C64_STOP` of the menu needed) | 1 per byte | real (ICR clears, TOD latches, collisions clear, cartridge I/O) |
| colour RAM | second port on the colour RAM (ARCH, patch 0051) | 0 | none |
| writes `wr`/`f`/`t` to RAM | RAM port | 0 | none |
| writes to I/O | bus cycle | 1 | real, by intent |
| drive RAM/ROM | DDR2 (`drive_mem`), after flushing its line cache (`trx->drive_flush`) | 0 | none |
| drive VIAs | state bus chips 10-11 (ARCH §5.4) | 0 | none |
| REU memory | DDR2 at 0x1000000 (inside the app's memory since T60, the app reads it directly) | 0 | none |

Read side effects a bus read would have (the state bus avoids them; `--` remains only for
cartridge I/O that the state bus does not model):

- CIA: $DC0D/$DD0D (ICR) and $DC08-$DC0B/$DD08-$DD0B (TOD latch)
- VIC: $D01E/$D01F (collision registers)
- REU: $DF00 (status clears on read)
- $DE00-$DFFF for every cartridge that decodes reads

The table is the app's, keyed by the cartridge type.

#### 3.4.1 Lens reads through the look and the bus (branch `t21-lens`, 2026-10-03)

**Why.** Before this, `cart` read the DDR2 image with `bank * 16 K` and the halves ROML/ROMH,
which has been wrong since T33 (the mapping is a 37-byte descriptor: EasyFlashXl, C64MegaCart,
TwoMegabyter, mirrored one-chip carts), and a cartridge on the real port (T16) has no image at
all. REST `machine:readmem` (Gideon's path) read through the DMA window with whatever the PLA
showed and had no lens. All three now go through one piece of code.

**One code.** The lens logic (PLA from `$01` and the cartridge lines, ROM block, state bus,
look, bus cycle) is a hardware-independent C file, `software/io/c64/trx_lens.c`, compiled by the
firmware (REST) and by trxmon (`mach_peek`, `session/read_memory`, `m`, `d`) and its host tests.
Each supplies the few hardware operations as a table (state-bus block read, RAM read, ROM
block, drive RAM, LK request). Same bytes from REST and from the WebSocket by construction.

**Hardware: the lens-read unit (LK, `freeze_resp.v`, CAPS2 b4).** Two registers on page 5:

| Address | Name | Access | Content |
|---|---|---|---|
| 0x101A5020 | LK_REQ | W | b15:0 address, b17:16 class (0 ROML, 1 ROMH, 2 IO1, 3 IO2: the look's `pk_cls`), b18 REAL. The write starts a request; ignored while LK_RES b31 is set |
| 0x101A5024 | LK_RES | R | b7:0 data, b8 done, b9 hit (the cartridge or the REU drives this read), b10 ext (the real port serves it, nothing was read), b11 real (data came from a bus cycle), b12 refused (REAL and the machine is not paused), b31 busy |

- **Look (b18 = 0):** the same path as the freeze session's cartridge look (T12 §14.3): `pk_*`
  of `c64_core` at the address with the class, then cart_fill's DDR2 read for ROM and cartridge
  RAM. Combinational from the model's banking registers, no write, no side effect, no bus
  cycle. It works with the machine running (the answer is then only as consistent as the
  model's registers are at that clock) and paused. `hit` 0: open bus, data $FF. `ext` 1: the
  real port would serve it, data $FF, the app takes the REAL path.
- **REAL (b18 = 1):** one bus cycle through the same `c64_core` DMA path the window uses
  (`cyc_*`), requested by the unit itself, so it needs neither `C64_STOP` nor a session. It
  runs only with the machine paused (`halted`; else `refused`). For the cycle the unit sets
  `core_ultimax` to 0 (the physical port is bypassed while the app's Ultimax is on, T16 §13.2:
  `rom_port = rom_sel && !ultimax`) and **forces the select**: new input `dma_force[1:0]` of
  `c64_core` (1 ROML, 2 ROMH, 3 I/O for class 0, 1, 2/3) down to `c64_bus_arbiter`, which for
  that read cycle overrides the PLA's choice (`roml_select`/`romh_select`/`io_enable`) and
  nothing else. The address on the bus is the one asked for, so a real cartridge sees what the
  CPU would have put out; with the force off nothing in the arbiter changes. Forcing is what
  makes `cart` independent of the current mode (ROMH at $A000 with an 8K cartridge, ROML in
  Ultimax with `$01` mapping RAM) and `io` independent of CHARGEN.
- **Arbitration:** the unit takes its turn in `freeze_resp`'s idle state, behind the window's
  requests and the commit list, using the same `pk_*`, `pkd_*` and `cyc_*` paths; a session
  cannot end while it runs. The app polls LK_RES; a request is a few microseconds (look
  without a DDR2 read) to tens of microseconds (cache line fetch).
- CAPS2 b4 = this unit. Without it (older bitstream) `cart` answers `--` for everything and
  `sidefx on` needs the menu's `C64_STOP` as before.

**Lens semantics (the table of §3.4, byte for byte):**

- `ram`, `rom`, `drive8`: unchanged.
- `cart`: `$8000-$9FFF` is class ROML, `$A000-$BFFF` and `$E000-$FFFF` class ROMH, every other
  address reads RAM. `cart_byte(cls, a)`: look; if `ext`, REAL; if that is refused the byte is
  unreadable. No cartridge present (state bus `present` 0): $FF.
- `cpu`: the PLA decides the region as before; ROML/ROMH regions are `cart_byte` (so an
  emulated EasyFlashXl reads its mapped bank, a real cartridge is read over the port);
  I/O is `io_peek`. Unreadable (SID, colour RAM, a real-port ROM read while the machine runs)
  stays -1: `--` in the monitor, 0 in `read_memory` and REST.
- `io`: VIC and CIA from the state bus; IO1/IO2 ($DE00-$DFFF) through the look for an
  emulated cartridge and the REU (register values the way a read returns them, without the
  read's side effect; the UCI shows what is below it and the AMD responder's ID mode is not
  shown, T12 §14.3), `--` for the real port; `sidefx on`: REAL class I/O for everything in
  $D000-$DFFF.

**REST `machine:readmem`.** New optional parameter `lens=cpu|ram|rom|io|cart|drive8` and
`sidefx=0|1`. **Decision: without `lens` the call is exactly Gideon's** (DMA bus read, the
PLA as the machine has it at that moment, I/O side effects included, frozen backups for
$0400-$0FFF), so his tools and tests (`readmem_writemem_test.py`) see no change; `lens` is
ours and opt-in. Not honoured without the TRX64 bitstream (400, "lens needs the TRX64
bitstream"); unknown lens 400. With a lens: `ram`, `rom`, `drive8` do not stop the machine;
`cpu`, `cart`, `io` pause it for the call (as the default path does) and resume, unless it
is paused or frozen already, so PLA words, banking and look are one consistent view. While the
freeze menu is up `ram`/`cpu` give the user's $0400-$0FFF from the app's backups, as the default
path does. Unreadable bytes (above) are returned as 0. Response and errors as before.
While trxmon serves (`announce`d RPC port) `cpu`, `io` and `cart` answer 503 "The debugger app owns the machine
state": the state-bus window (SB_ADDR) and run control are the debugger's, and the firmware's own stop would
yield its ownership (`trx_dbg_yield`); `ram`, `rom` and `drive8` need neither and stay available. The firmware
side is `trx_lens_fw.cc` (new `C64_LENS_READ` in `c64_subsys.cc`, the same task as the other DMA commands).

**As built and measured (2026-10-04).** Code: `freeze_resp.v` (LK unit, states S_LKC, `lk_*`), `trx_sys.v` (page 5
0x020/0x024), `trx_io.v` (CAPS2 0x1F), `c64_core.v` (`dma_force`), `c64_top.v`/`c64_bus_arbiter.v` (`force_sel`);
app `software/io/c64/trx_lens.c` (shared), `trx_lens_fw.cc`, `C64_LENS_READ` in `c64_subsys.cc`, `route_machine.cc`;
trxmon `mach.c` calls the same file (`apps/trxmon/Makefile` compiles it). Simulation: `sim/cart` (all 17 types: the
look through the unit equals the bus reads, REAL with the forced select equals the look, ROMH asked at $A000 equals
$E000, look while running, REAL refused while running), `sim/slot` test `lens` (real port model: look says ext,
REAL ROML/ROMH/IO1/IO2 with the select forced and the app's Ultimax on, ROMH at $E030 with the PLA mapping KERNAL,
exactly one PHI2 pulse per byte, none for a look, refused while the machine runs), `sim/statebus` (registers by
bytes), `sim/trx_io` (CAPS2), `sim/dma_io`, slot suite 8/8; host `make -C apps/trxmon/host test` 375 checks.
Vivado (CI run 328): timing met (WNS +0.211 ns), Slice LUTs 26,764 against 26,511 on main-latest (+253), registers
+83. Board (2026-10-04, SRAM bitstream of run 328, firmware staged over JTAG with `trxdbg boot staged`):
`tests/cart/lens_board.py` on the emulated EasyFlash (bank 2) and EasyFlashXl (bank 100): 38 of 38 checks, running
and with the freeze menu up: `lens=cart` at $8000/$A000/$E000 equal the chips' bytes, `lens=cpu` equals the
cartridge where the PLA maps it and `lens=rom` at $E000, and the default read is unchanged. Finding: the default
(Gideon's) read of an emulated cartridge returns a stale byte at the first read of each cache line after a bank
change (the bus cycle is not held for the fetch); `lens` reads are exact, the test compares against the second
default read. `lens=io&sidefx=1` equals the default read at $D020/$D021/$DC00/$DD00/$D016. 64 K through a lens
takes 0.4 s (ram) to 3 s (rom, io, cpu, cart: one register round trip per byte). Not tested: a real cartridge on the
port (needs Alex; T16 §8.4 steps in the table of the T16 test plan).

**Tests.** Simulation: `sim/cart` extended: after the existing look-vs-bus check the unit
(look and REAL with the forced select, not the PLA) is compared per type, and the forced select
at an address the PLA would not select (ROMH $A000 with an 8K cartridge, ROML with `$01`
mapping RAM) is read; `sim/slot` (real port model): REAL reads of ROML, ROMH ($A000 and $E000)
and IO1 from the EasyFlash-like cartridge with the machine paused, no extra port strobe in the
look path, the forced select off changes nothing (the existing pause test). Host: trxmon
`test_main.c` gets lens cases over the fake LK. Board: §9.3.

---

## 4. Command reference

The syntax is TRX64's. Numbers are hex by default (`c000`, `$c000`). A bare line goes to the
monitor; a `/`-prefixed line is a machine command, and an unknown `/verb` does not fall through.
[TRX64 `docs/command-syntax-convention.md`]

Verb list and help text: [TRX64 `crates/trx64-monitor/src/verbs.rs` `monitor_help_text`
(line 357) and the dispatch from line 1075]. Run control and stepping semantics:
[TRX64 `crates/trx64-daemon/src/main.rs:3265-3440` (`until`, `z`, `n`, `ret`)]. Observers:
[TRX64 `crates/trx64-monitor/src/observers.rs`]. Flow: [TRX64 `crates/trx64-monitor/src/session.rs:177-280`].

Columns:

- **HW**: the hardware path that implements the verb.
  - RP = RAM port
  - BC = bus cycle
  - CS = CPU state port
  - CT = control/stop logic
  - XB/AB = exec/access bitmaps
  - CMP = comparator units
  - RAS = raster comparator
  - FS = flow stack
  - CNT = counters
  - TR = trace ring
  - DD = DDR2 direct
  - TAP = chip state taps (shared with T20)
  - FW = firmware ABI
  - APP = app only
- **Ph**: the phase from §10.

**HW codes after ARCH:** AB → CMP (two units) or soft; FS → TR or soft; CMP = two C64
watch units plus one drive exec comparator; TAP = the ARCH state bus, available from
phase 2 (no bus-cycle phase); TR = engine stream 3; RAS = a watch unit's raster source.

### 4.1 Execution


| Verb | Syntax | Semantics | HW | Ph |
|---|---|---|---|---|
| `g` | `g [addr]` | Resume (with `addr`: redirect PC first, §3.3). Returns at once with "running until something stops it" (TRX64 `RunUntil::describe`). The stop arrives later and is printed when it does (§6.3). | CT, CS | 2 |
| `x` | `x` | Resume and leave the monitor (VICE semantics; TRX64: `= g`). Releases ownership (§7.3). | CT | 2 |
| `until` | `until <addr>` | Run until PC = addr (the user's breakpoints stay active). Hardware: one comparator, exec at `addr`, one-shot. If PC already equals `addr`, step one instruction first (TRX64 main.rs:3274). | CMP, CT | 2 |
| `z` / `step` / `si` | `z [n]` | Step into: run to the next IB, n times. Interrupts are entered (VICE-correct). | CT (STEP_INSTR) | 1 |
| `n` / `next` / `so` | `n` | Step over. On a JSR (opcode $20 at PC): one comparator "exec at PC+3 and S ≥ S0", and the user's breakpoints stay on. Otherwise one instruction; if an interrupt was entered on the way, continue as `ret` until S ≥ S0 (TRX64 main.rs:3343: "runs through IRQ/NMI"). | CMP (reg qualifier), CT | 2 |
| `ret` / `return` | `ret` | Run until the current frame returns: stop at the first IB after an RTS/RTI with S > S0 (TRX64 main.rs:3399, `is_ret && sp > sp0`). Hardware: comparator "any IB, previous opcode ∈ {$60,$40}, S > S0". | CMP (PREV_RET, reg) | 2 |
| `cs` | `cs [n]` | **[new]** Cycle step: n phi2 cycles (CB stops). A cycle in which RDY holds the CPU counts as a cycle. | CT (STEP_CYCLES) | 1 |
| `focus` | `focus [auto\|main\|irq\|nmi\|brk\|clear]` | Flow focus. **Built in software (§8.13).** | FS, APP | 3 |
| `sf` / `nf` | `sf` / `nf` | Step into/over, stopping only in the focused flow (comparator flow qualifier = focus). **Built in software, single steps (§8.13).** | FS, CMP | 3 |
| `flow` | `flow` | The interrupt/trap frame stack: kind, entered at PC/cycle, return PC (TRX64 `CpuFlowFrame`). **Built in software (§8.13).** | FS | 3 |
| `bt` | `bt` | Backtrace: scan the stack page through RP for plausible JSR return addresses (the byte at ret−2 is $20), merged with the flow frames. | RP, FS | 2 (scan) / 3 (frames) |
| `reset` | `reset [cold]` | Warm reset: the firmware's `C64::reset` path through `trx->machine_reset(0)`, since it re-installs the cartridge. The lib clears its cursors and flow stack afterwards (TRX64 864 §5). `cold` = power cycle: not available (no power switch for the C64 core alone); says so. | FW | 2 |
| `model` | `model` | PAL, the VIC (kawari as a 6569), 312×63, 985248 Hz, from `C64_VIDEOFORMAT`. `model <row>` is refused (PAL only, TODO.md T40). | APP | 2 |
| `turbo` / `warp` | | Not available (T29 later). | — | — |

### 4.2 CPU

| Verb | Syntax | Semantics | HW | Ph |
|---|---|---|---|---|
| `r` | `r` | Registers in TRX64's layout (`ADDR AC XR YR SP NV-BDIZC`) plus the flow and the IRQ/NMI vectors (read through the `cpu` lens). Adds `$00/$01`, the cycle and instruction counters, the stop reason, and the mid-instruction line when not at an IB. Under `device drive8`: TRX64's 1541 layout (verbs.rs:1075ff): track/halftrack, LED, drive clock. | CS, CNT | 1 (JTAG) / 2 |
| `r` set | `r a=$42 x=$10 pc=c000 fl=…` | Write registers (a/x/y/sp/pc/fl). At an IB only; `pc=` is a redirect (§3.3). The drive is writable too (TRX64 refuses this for the drive; here it is a deliberate extension, gated by `device drive8 rw`). | CS | 2 |
| `sidefx` | `sidefx [on\|off\|toggle]` | Whether reads may take a bus cycle with side effects (§3.4). Default `off`. | APP | 2 |
| `device` | `device [c64\|drive8]` | Selects the CPU that `r`/`m`/`d`/`z`/`bk` act on. Bare = the list. Only drive A exists (unit from its config); drive9-11 are "not there". | APP | 2 |

### 4.3 Memory

All take an optional lens: `cpu` (default), `ram`, `rom`, `io`, `cart`.

| Verb | Syntax | Semantics | HW | Ph |
|---|---|---|---|---|
| `m` | `m [lens] <a> [b]` | Hex dump. TRX64 prints $20 bytes per row plus PETSCII on the wire (REST); the 40-column screen prints 8 per row (§8.2). Default length $800. | RP/DD/BC | 2 |
| `d` | `d [lens] [a] [end]` | Disassemble a range, or about 16 lines from a / PC. Illegal opcodes as in TRX64 (`trx64-static` tables). | RP/DD | 2 |
| `df` | `df [-i] [a] [n]` | Follow-disasm: walk the control flow statically; with `-i`, ask at branches. | RP | 2 |
| `sd` | `sd [n]` | Step and disassemble the real executed path, loops folded: n hardware steps, disassembled one by one (P2). Faster with the instruction trace (P4). | CT, RP / TR | 2 / 4 |
| `a` | `a <a> [instr]` | One-line assembler; `a c000` enters assemble mode (an empty line exits) [TRX64 `assembler.rs`]. | RP | 2 |
| `asm` | `asm <a> [<<TAG]` | Block assembler: labels, `=`, `*`, `<`/`>`, `+`/`-`, `.byte`, `.word`, one `*=`. | RP | 3 |
| `asm-file` | `asm-file <path> [a]` | The same from a file. | RP, FW fs | 3 |
| `wr` | `wr [lens] <a> <b..>` | Write exactly these bytes. `io` goes through BC, the rest through RP. | RP/BC | 2 |
| `f` | `f <a> <b> <d..>` | Fill. | RP | 2 |
| `t` | `t <a> <b> <dst>` | Move, overlap-safe. | RP | 2 |
| `c` | `c <a> <b> <dst>` | Compare and list the differences. | RP | 2 |
| `h` | `h <a> <b> <d..>` | Hunt; `xx` = wildcard. | RP | 2 |
| `bank` | `bank [lens]` | Show or set the sticky lens. Also prints the live banking: `$01` (LORAM/HIRAM/CHAREN), GAME/EXROM, the VIC bank from CIA 2 PA, and $D018. | CS, TAP/BC | 2 |
| `screen` | `screen` | Decode the 40×25 text screen through the real screen pointer (VIC bank + $D018). | RP, TAP/BC | 2 |
| `bitmap` | `bitmap <a> [w h] [mode]` | TRX64 renders a PNG. Here it writes `<name>.png` through `fs`; it needs a small PNG writer (stored deflate) in the app. | RP, FW fs | 4 |

### 4.4 Breakpoints, watchpoints, observers

| Verb | Syntax | Semantics | HW | Ph |
|---|---|---|---|---|
| `bk` | `bk` / `bk <a>` / `bk -<a>` | List, set or remove an exec breakpoint. Any number of them: one bit each in the exec bitmap (TRX64's `exec_watch[0x10000]` as BRAM). On the drive, they use drive comparators (2). | XB / CMP | 1 (XB) / 3 (drive) |
| `del` | `del <n..>` / `del` | Delete by number, or all. | XB, CMP | 2 |
| `obs` | `obs <name> when exec\|load\|store <a[..b]> [if <cond>] do <action> [fields]` | Conditional observer [TRX64 observers.rs]. Mapping in §4.4.1. **As built: §8.13** (`break`, `log`, `cmd`; `mark`/`trace` not). | XB/AB/CMP/RAS | 3 |
| `obs` list | `obs`, `obs log`, `obs <glob> on\|off\|del` | List, show the log ring, toggle, delete (glob names). | APP | 3 |
| `ignore` | `ignore <name> [n]` | Skip the next n hits: PASS in the comparator, or counted in software for bitmap observers. **Built (§8.13).** | CMP | 3 |
| `rb` | `rb <line> [x]` / `rb -` | **[new]** Raster break: stop when the VIC reaches raster line `line` (and pixel x). In TRX64 `rl` exists only as a condition variable; this is the short form of `obs r when raster …`. **Built (§8.13):** the cycle is `x/8`. | RAS | 3 |
| `traprules` | `traprules <path>` / `traprules [clear]` | On-trap dump rules (JSON `{pc,label,dump,decode}`), printed when that PC is reached at a breakpoint. | APP, FW fs | 3 |

#### 4.4.1 How an observer maps onto the hardware [new]

TRX64 keeps the policy in software and gates it with presence tables (observers.rs header:
"idle cost is zero … an active observer only pays the condition eval on its EXACT address").
The FPGA does the same in two levels:

1. **Hardware gate.** Exec observers use the exec bitmap (XB); load/store observers use one of the two watch units (ARCH §7.2; the access bitmaps of
   the first draft are gone); when both units are taken, the observer is soft (step and
   evaluate, slow but exact).
2. **Condition.** The app compiles the condition (`a/x/y/pc/sp/fl/rl/val/addr/cy`,
   `== != < > <= >= && || ( )`) as far as a comparator can hold it:
   - address range
   - `val` == v under a mask
   - one register op (`a/x/y/sp/fl` against a value with a mask)
   - `rl` (through the raster comparator chained with AND)
   - `cy` (the cycle comparator)

   Whatever is left is a **soft condition**. The hardware stops, the app evaluates the
   condition, and the machine resumes if it is false. A soft false hit costs the machine
   nothing (the gate) but costs wall time: the resume waits for frame alignment (≤ 20 ms),
   unless `RUN_NOALIGN` is used (Q4). The monitor prints which part ran in hardware
   (`obs` list: `hw: addr+val, soft: a>x`).
3. **Actions**:
   - `break`: halt.
   - `log [fields]`: Phase 3 soft (halt, read the fields, resume). Phase 4 in hardware: the
     comparator action LOG writes a record (regs, addr, data, cycle) into the trace ring
     without stopping, and the app formats the fields from it. `$addr[:w]` fields stay soft.
   - `cmd "…"`: soft.
   - `trace [domains]|off`: comparator action TRACE_ON/TRACE_OFF (bracket model; Phase 4).
     Domains: `c64-cpu`, `memory`, `drive8-cpu`. `iec`, `vic`, `drive-mechanism` and
     `cart-read` are not available.
   - `mark`: (T20).

### 4.5 I/O and device views

| Verb | Syntax | Semantics | HW | Ph |
|---|---|---|---|---|
| `io` | `io [1\|addr]` | VIC-II $D000-$D03F, SID $D400-$D41F, CIA1, CIA2, cartridge IO1/IO2 by mapper name: hex plus decoded state [TRX64 verbs.rs:2125]. P2: bus cycles for the registers without side effects; the refused ones show `--`, the bus cycles spent are reported. P3: from the chip taps (zero cycles; write-only VIC/SID registers shown as last written). | BC / TAP | 2 / 3 |
| `io 1541` | `io 1541` | **[new]** Drive A's VIA 1/VIA 2. P2: the `drive_dbg` taps (last bus cycle, PB last write, half track, motor, SYNC/byte-ready counts). P3: full VIA state (T20 drive taps). **Built: the `drv_dbg` part only (§8.13); the VIAs are out of the budget (ARCH 0004).** | TAP | 2 / 3 |
| `iec` | `iec` | ATN/CLK/DATA, each line's level and who pulls it (the C64, drive A, the board port), plus $DD00 and the drive's $1800 as their CPUs see them [TRX64 verbs.rs:2191]. From `iec_bus.v` (today's JTAG register 0x24, mirrored into `trx_dbg`). | TAP | 2 |
| `drive` | `drive [unit]` | Motor, track, LED, mounted image, dirty (from `drive_dbg` 0x25 and the firmware's drive object). | TAP, FW | 2 |
| `cart` | `cart` | Type, bank, mode, ACTIVE; read/write activity counters from the `cart_slot` taps. | TAP | 2 |
| `reu` | `reu` | Size, status, command, addresses, length, IRQ. P3 from the REU taps (icepi `c64_reu.v`); bus reads would clear the status. **Built through the lens unit's look (§8.13).** | TAP | 3 |
| `georam` | | Not available (no GeoRAM in the core). | — | — |
| `uci` | `uci` | Not available until T28. | — | — |
| `pot` | `pot` | The POT lines. Read-only from the SID taps; setting them is not available (the paddles come from the board). **Built: CIA 1 select lines + `$D419/A` with `sidefx on` (§8.13).** | TAP | 3 |
| `folder` | | Not available (no folder devices). | — | — |

### 4.6 Media and machine (`/` namespace and the bare TRX64 forms)

| Verb | Syntax | Semantics | HW | Ph |
|---|---|---|---|---|
| `/run`, `run` | | Resume (= `g`). | CT | 2 |
| `/pause`, `pause` | | Pause at the next IB (STOP_AT_IB). With `/pause cb`: at the next CB. | CT | 2 |
| `/step` | | `z`. | CT | 2 |
| `/reset` | `[warm]` | Warm reset as in §4.1. | FW | 2 |
| `/power` | | Not available. | — | — |
| `mount` / `/mount` | `mount <path>` | `media->mount` for disks. A `.crt`/`.prg` goes through `media->run`, which happens after the app returns (ABI v2 rule); the monitor says so. `.c64re` is (T20). | FW | 2 |
| `eject` | `eject [cart\|disk\|<unit>]` | `media->unmount`. | FW | 2 |
| `drivepower` | `drivepower [unit] [on\|off]` | Bare: reset the drive's 6502 only (drive register reset bit through `trx->drive_reset`); on/off through `config->set`. | FW | 3 |
| `recent`, `identify` | | `identify` by content (the TRX64 rules); `recent` is not available. | APP | 3 |
| `savecrt` | `savecrt ["<p>"]` | Through the firmware's CRT auto-save path (needs an ABI call; Q9). | FW | 4 |
| `swapcrt` | | Not available. | — | — |

### 4.7 Files

These are TRX64's file verbs. The memory half goes through the same path as `wr` (TRX64 864 §10.1).

| Verb | Syntax | Semantics | HW | Ph |
|---|---|---|---|---|
| `pwd` / `cd` / `ls` | `cd [dir]`, `ls [dir]` | The cwd lives in the app (the session scratch `trx->scratch`). `ls` needs `fs->opendir/readdir` (ABI addition, §7.2). **Built as `fs->dir` (§8.13).** | FW fs | 2 (`ls`: 3) |
| `mkdir` / `rmdir` | | `fs->mkdir` exists; `rmdir` needs the ABI addition. **Built as `fs->rmdir` (§8.13).** | FW fs | 2 / 3 |
| `load` | `load "<f>" [addr]` | PRG into RAM through RP. | FW fs, RP | 2 |
| `save` | `save "<f>" <a1> <a2>` | a1..a2 as a PRG (`cpu` lens: what the CPU sees). | FW fs, RP | 2 |
| `bload` / `bsave` | | Raw. | FW fs, RP | 2 |
| `dump` | `dump <p>` | **Removed from M1 (A13), T66 in M2.** A `.c64re` snapshot: `apps/c64re`'s writer plus the CPU state (state-bus chip 1); with T20, the full chip state. P2 writes "inspectable, not resumable", as that app does today. | RP, CS, FW | 2 |
| `undump` | `undump <p>` | **Removed from M1 (A13), T66 in M2.** (T20). | — | 5 |

### 4.8 Trace (Phase 4) [new hardware, TRX64 verbs]

| Verb | Syntax | Semantics | HW | Ph |
|---|---|---|---|---|
| `trace` | `trace on\|off\|status\|mark` | The trace gate: bus cycles and/or instruction records into the DDR2 ring (engine stream 3, ARCH §6.1). | TR | 4 |
| `chis` | `chis [cyc]` / `chis <s> <e>` | CPU instruction history from the ring (PC, opcode, A X Y S P, cycle). | TR | 4 |
| `whowrote` | `whowrote <addr> [n]` | Last n writers: PC, cycle, new value; the old value is the previous write to the same address in the ring, or `?`. A hit older than the ring prints `ring_exhausted: true` (TRX64 wording). | TR | 4 |
| `triage` | `triage [pc]` | Causal chain: crash or wild PC → the last RTS/RTI/JMP(ind) → the stack slot it popped → `whowrote` on that slot. Forward data only, so it is inspect-only; it does not undo anything. | TR | 4 |
| `tracering` | `tracering <s> <e> [path]` | **Removed from M1 (A13, Alex 2026-10-06; TODO T66 in M2).** Write a `.c64retrace` file (the trx64-trace format) from the ring window through `fs`. C64RE can then index and analyse it. | TR, FW fs | 4 |
| `revdepth` | `revdepth` | Reports the trace ring's depth in cycles and seconds. Setting it = `TRACE_SIZE` (it discards the ring). | TR | 4 |
| `map`, `taint`, `swimlane`, `traceindex`, `tracedb` | | Not available here: they are analyses over a trace on the host. In M1 there is no export of a trace window to the host (`tracering` removed, A13; TODO T66). (TRX64 864 §3 "defaulted services"). | — | — |

### 4.9 Session and help

| Verb | Syntax | Semantics | Ph |
|---|---|---|---|
| `help` / `?` | | TRX64's help text, filtered to the verbs this host answers, with a "not available on this host" section [TRX64 864 §11.3]. | 2 |
| `status` | | **[new as a verb; TRX64 has it as a host call]** Running/paused, the last stop (reason, PC, cycle), owner, armed breakpoints/comparators, DMA cycles spent. Answers the stop that came in after an asynchronous `g` (TRX64 864 §5.1). | 2 |
| `/quit` | | Leave the monitor. The machine stays as it is (paused or running); ownership is released (§7.3). | 2 |

### 4.10 Needs T20 (later phase, answers "needs T20" until then)

`rstep`/`reverse`, `diff`, `cdiff`, `ringdump`/`ringload`, `mark`/`marks`/`unmark`,
`goto`, `play`, `frame ±N`, `rewind`, `cadence`, `window`, `rawframe`, `undump`, the
`do mark` action, `mount <x.c64re>`. TRX64 sources:

- `MONITOR.md` sections "Reverse-debug" and "Marks & the rewind transport"
- `docs/spec-reverse-debug-crash-triage.md`
- `docs/spec-time-travel-tooling.md`

`triage` and `whowrote` come forward to Phase 4 on the bus trace (forward data). With T20
they get the old values from the delta ring and `rstep` lands on them.

#### 4.10.1 As built on T20 (2026-10-03, trxmon `ring.c`, firmware `trx_ring.cc`)

The ring's entries are frame starts (T20 snaps). The firmware gives the debugger a **land**
(`trx->ring->land`): the scrub's walk and put-back (T20 §16.7), then the debug block's
CYC_CMP armed for the very next edge while the machine holds at STOP_POS, so the release lets
exactly the load edge through and the machine stands paused on the snap, the debugger holding
it. The timeline is not cut; the log stays on, so what the debugger runs from there is undone
by the next land. A free run (`g`, `x`, `n`/`ret`/`until`, `session/run`, the firmware taking
run control back, the app ending) **commits**: the frames after the land are cut (TRX64's
intervention cut) and the ring records again. Run control is OWNER's: for the restore part
the firmware hands it to C64_STOP (which already holds the machine) and takes it back before
the release. IPC is set by the app from the CPU's state (a snap is the end of an opcode
fetch; an IB unless an interrupt drops it).

Cycles: entries carry F2's count, which is the debug block's CYCLES (both count the same
phi2 edges since configuration); restores, scrubs and commits rebase it to the load edge's
count, so consecutive entries are always one frame apart. trxmon's reported cycles
(`c64Cycles`, `r`, stops) go back with a land, as TRX64's do with a restore.

| Verb / method | As built |
|---|---|
| `rstep [n]`, `reverse`, `runtime/reverse_step {n}` | **Per instruction**, as TRX64: a replay. Land on the frame that holds the stop, STEP_CYCLES to the stop's cycle and count the instructions (INSTRS), check the stop is reached again (PC and registers), land again and STEP_INSTR to the instruction n before it; earlier frames are counted the same way when n reaches over a frame start. TRX64's "inspect-only" caveat does not apply (the CPU, CIAs and memory are exact, so `g` runs on from there and cuts); the undone writes are not listed. |
| `frame [±N]`, `transport/frame {delta}` | ±N entries by land. Forward at the head (no land open) runs N × 19,656 cycles and steps to the next IB (TRX64: one frame of emulation per step). |
| `goto <frame>|c<cycle>|<mark>`, `transport/goto`, `mark/goto` | land; frame numbers 1-based over the ring, a cycle lands at the entry at or before it. |
| `rewind`, `transport/status` | TRX64's status line and object (`mode` LIVE/REPLAY/CUT/STEP, `frameIndex`, `cursor`, `secondsBehind`, `range`). |
| `mark`/`marks`/`unmark`, `mark/set|list|drop` | names on entries (32), kept in the app; **not pinned**: a mark goes when its frame leaves the ring (keyframe slots, T20 §4.5, not built). |
| `checkpoint/list|restore|capture` | entries with TRX64's ids `cp_<frame>_<seq>` (frame = ring cycles / 19,656, as `.c64rering` writes them); `list` caps at 400 with a stride; `capture` returns the entry the machine is in (every frame start is captured anyway); `restore {then}` lands. `pin`/`unpin` answer -32001 (no slots), `clear` -32601. |
| **Removed from M1 (A13; T66 in M2):** `ringdump <p> [s] [stride]`, `ringbuffer/dump` | `trx_c64rering_dump` (T20 §10.1), now with TRX64 0.12.2's `turbo` node in the reset state. |
| **Removed from M1 (A13; T66 in M2):** `ringload <p>`, `ringbuffer/restore`, `undump`, `snapshot/undump` | `trx_c64re_load`, which now also takes the current entry of a `.c64rering` written by this board; the machine runs from it and the ring starts over (the hardware ring cannot be refilled from a file). |
| **Removed from M1 (A13; T66 in M2):** `dump <p>`, `snapshot/dump` | `.c64re` of the frame start the machine is in (not the cycle it stands on). |
| `play`, `cadence`, `window`, `rawframe`, `diff`, `cdiff`, `transport/play back` | not available (no paced replay or thumbnails; the cadence is one per frame). |

Limits: a land is as exact as a T20 restore (§16.6 there): the VIC's internals, the SID, the
REU and the drive are not put back, so a replay that depends on them (a fastloader talking to
the drive, a REU transfer) may not come back to the same stop; `rstep` checks and says so.
Frames the debugger steps across while landed get no entries (their markers are skipped at
the commit; `rewind` shows them as gaps).

Fixed on the way (T20 code): `parse_locked` read markers into the timeline while a land or a
Timewarp scrub held the machine (a status call between steps did it), so entries of a run
that is not the timeline appeared and, with a full timeline, indices moved by one per
frame; it now only reads the counts then. Restores, scrub rewinds and loads hand run control
from the debugger to C64_STOP first (they stopped at "did not reach the snap position" while
trxmon owned the machine) and leave it there; a land takes it back.

**Board (2026-10-03, C64U, a `t15-boot` bitstream without the CPU write path, firmware and
trxmon of `t21-app2`):** on the BASIC idle loop with the KERNAL IRQ, `rstep` 1000, 2000,
4000, 8000 and 5000, 10000, 20000, 40000 instructions (up to about 7 frames back, across
frame starts and IRQs) land on the exact registers, PC and cycle count recorded on the way
forward, and stepping forward again reaches the same stop; 200-560 ms each. Land plus
`cs` 4096/32768/49152/65535 and `z` a+b against `z` (a+b) from the same entry: identical
three times each. A land from the live machine plus STEP_CYCLES of the computed distance
reaches the stop's raster position exactly (F2 = CYCLES). `frame ±N`, `goto` (frame, mark,
c<cycle>), `mark`/`marks`, `rewind`, `checkpoint/list` (stride 3 over 1060 entries),
`checkpoint/restore` (then pause: `c64Cycles` = the entry's), `transport/frame`, `mark/list`,
`dump` (97 KB .c64re, 1.3 s), `ringdump` (2 s at stride 25: 4 anchors, 389 KB, 2.2 s),
`undump` and `ringload` (1.2/1.7 s, the screen comes back) work. On the `t21-cpuwrite`
bitstream (§5.1.1) the same `rstep` runs pass and a land leaves the IB flag and IPC right.

### 4.11 Instruction records = TRX64's delta ring (decision 2026-10-03)

The trace former (engine stream 3) writes one record per instruction in the layout of
TRX64's `DeltaEntry` (`crates/trx64-core/src/delta_ring.rs`, 24 bytes, LE): cycle u64 (at
retire), write_start u32 (stream 0's running record index at the instruction's first
write), pc u16 (the opcode address), write_count u16, A X Y SP P **before** the
instruction (P complete, B intact), opcode, b1, b2. The writes stay in stream 0, whose 4-byte
record is already TRX64's `WriteRec` {addr u16, old, new}; for a byte-exact undo per
instruction, stream 0 also takes the I/O writes ($D000-$DFFF, old value from the chip
shadows). A `.c64rering` then carries the ring as TRX64's `delta_ring` (`entries` /
`writes`, base64) with no conversion in the app; TRX64 answer 2026-10-03: fill it only with
one record per instruction and no gaps, else leave it empty. `cpu_history` (registers after
the instruction, `chis`) is derived from the same records by the app.

Depth: 1 s, permanently (Alex, 2026-10-03; 2026-10-04: no 2 s follow-up, T45 dropped). At
about 310,000 instructions per second × 24 B ≈ 7.5 MB, which fits the 8 MB already reserved
for stream 3 (T20 §5.1, ARCH §6.1); stream 0's 16 MB covers the writes.

#### 4.11.1 The trace former (P4), design 2026-10-04 (branch `t21-p4`)

> The former, `chis`, `whowrote`, `triage`, `rstep` from the trace and the live trace RPCs stay in M1. The `.c64retrace` export (`tracering`, `trace/build_from_ring`, the ABI call `trace_writes`, now a reserved NULL slot) was removed again (A13, Alex 2026-10-06; TODO T66).

**What is recorded.** One record per instruction the CPU starts, at the edge that ends its
opcode fetch (the IB of `trx_dbg.v`: sync, RDY, CPU on the bus, no interrupt decided). An
interrupt sequence is not a record of its own: its stack pushes count as writes of the
instruction before it, and the first instruction of the handler is the next record (the
pre-state of that record is what an undo lands on). The trace follows the log's gate
(`on`, §LOG_CTRL) and so the T29 rule (Q7): where the T20 ring does not record (turbo), no
record is made; the one place that gates it is `tr_on` in `trx_engine.v`. The first
instruction after the gate opens, a restart or a flush has no record (nothing to close).

**Where the data comes from** (all at the IB; clock = `clk`, 31.5 MHz, a phi2 cycle is 32 clocks):

| Field | Source |
|---|---|
| cycle | the engine's phi2 count (`cycles`, the one F2 carries) **after** the fetch edge: what the debug block's `CYCLES` reads at a stop on this IB, and F2 of a snap on it. 24 bits in hardware |
| pc, opcode | `trx_dbg.v` `ipc`, `op` (registered at the IB) |
| A X Y SP P | `l_cpunx` (state bus chip 1 w4/w5: the registers with the previous instruction's write-back done), sampled 12 clocks after the IB; that is the state before this instruction |
| b1, b2 | the data bus of the two CPU cycles after the fetch (cycle 2, cycle 3 that complete), raw: for a 1- or 2-byte opcode they hold whatever the dummy reads returned; the app zeroes what the opcode length leaves out (TRX64: 0 for the bytes an opcode does not have) |
| write_start, write_count | the engine's count of stream 0 records pushed into the FIFO at the IB and at the next IB: start and difference. Writes from outside (JTAG, freeze responder, REU through port B) are stream 0 records too, so a count can include them (TRX64's REU writes count the same way) |

The record of instruction I is therefore complete, and written, at the IB of I+1: after a stop
the newest record is the instruction before the one that stands at IPC.

**Record in DDR2.** Three 8-byte words (LE) per instruction, TRX64's `DeltaEntry` in 24 bytes:

| Word | Bits |
|---|---|
| 0 (bytes 0-7) | [23:0] cycle (24 bits, zero above: the firmware extends it, §4.11.2) |
| 1 (bytes 8-15) | [31:0] write_start (stream 0 record index, 28 bits), [47:32] pc, [63:48] write_count |
| 2 (bytes 16-23) | A, X, Y, SP, P, opcode, b1, b2 (A in the lowest byte) |

Area: fixed, 0x6800000, 349,525 records = 1,048,575 words = 8,388,600 bytes (the 8 MB of
stream 3 less 8 bytes, so a record never straddles the wrap). Record n sits at `0x6800000 +
24 * (n mod 349,525)`. The record count `LOG_CNT[3]` (word 0x016, read) counts records written
to DDR2 since the restart and wraps at 349,525 x 768 = 268,435,200 (a multiple of the area, so
the slot is `count mod 349,525` always). At 1 MHz and about 3.2 cycles per instruction the
area holds 1.1 s: **1 s permanently** (Alex, 2026-10-04), the whole area is used.

**Hardware.** A second dual-clock FIFO (64 bits x 512, one block RAM, `trx_afifo`) takes the
three words from the C64 clock; the drain (`ui_clk`) writes them as 8-byte masked MIG writes
in front of the mover, behind the main FIFO. The former keeps the previous instruction's
fields in registers loaded from live values (no LUT); at the next IB they are copied once and
three clocks of a 3:1 mux push the words (no wide shifters). A new IB while the three words
of the previous record are still going out (the FIFO was full) loses the new record: DROPPED
counts it and the marker's gap flag (F1 b2) says so; a record is never cut in the middle, so
the drain's word phase stays aligned.

Registers (page 2, `trx_engine.v`): `LOG_CTRL` b4 trace on (W; the app keeps what it wrote), b9
flush (W; drops the pending instruction: the app writes it with every restore, land, scrub or
load, because the machine's flow is cut there), b8 restart also zeroes the trace counters.
`LOG_CNT[3]` (0x058, word 0x016) R. `LOG_STAT` b5 R: words wait in the trace FIFO.

#### 4.11.2 The trace in the app (firmware `trx_ring.cc`, ABI `u2p_ring_api_t`, trxmon)

The firmware reads records by COPY (the same bounce buffer as the other streams) and gives
the app a **logical history**: the records that belong to the timeline the machine is on now.
The cycle is extended to the T20 timeline's 64-bit count (`TrxFrame.cycles`'s coordinate, so
`target_at_or_before` finds the frame), by the epoch of the record: every restore, land, scrub
and load rebases the cycle count (`rebase_cycles`) and starts an epoch {first physical record,
hardware count, timeline count}; of an earlier epoch only the records before the point the next
epoch starts at belong to the history (what was undone is not history). A forward land to a
frame the replays did not reach leaves a gap in the history; the firmware says so (`gap` in the
status) (the former export `tracering` left a gap-bearing window out of the delta ring, TRX64's
rule: one record per instruction and no gaps, else nothing; removed from M1, T66).

ABI (`u2p_ring_api_t`, appended; the table's `size` says whether it is there): `trace_ctl(on)`,
`trace_status()` {on, records in the history, capacity, newest cycle, lost, gap},
`trace_read(first, n, out)` (index 0 = oldest; `u2p_trace_rec_t` = TRX64's `DeltaEntry`, 24 bytes,
cycle a timeline u64, b1/b2 zeroed by the opcode's length), `trace_writes(start, n, out)`
(stream 0 records `{addr, old, new}` by physical index), `trace_whowrote(addr, limit, hits)`
(the scan of the writes of the history, newest first, done in the firmware). `u2p_trx_api_t`
also gets `video()`: lines and cycles per frame and the frame rate of the System Mode that
runs now (T40), so trxmon stops assuming PAL.

Verbs (trxmon; TRX64's text, `crates/trx64-daemon/src/main.rs` `chis`, `trx64-monitor/src/verbs.rs`
`whowrote`/`triage`, `trx64-core/src/crash_triage.rs`): `trace on|off|status|mark`, `chis [cycles]`
and `chis <c1> <c2>` (registers after the instruction = the pre-state of the next record; for
the newest, the live registers), `whowrote <addr> [n]` (old and new from stream 0's own record; an I/O address
$D000-$DFFF from the store instructions of the trace, §8.12;
`ring_exhausted: true` when the scan reaches the ring's start without n hits), `triage [pc]`,
[removed from M1, A13/T66: `tracering <c1> <c2> [path]`, the `.c64retrace` writer (CPU_STEP per record, RAM_WRITE per write, `trx64-trace` `write_delta_entry`)], `revdepth` (the depth in cycles and seconds; a
number answers that the depth is fixed), `rstep [n]` **per instruction from the trace**: the
target is the record n back, the land goes to the frame at or before its cycle and
STEP_CYCLES runs to the record's cycle (the old replay-and-count stays as the fallback when
the trace holds fewer than n records), the stop is checked against the record's pre-state.
`sd` and the rest of the P3/T55 list are not part of this.

**As built, app side (branch `t21-p4-app`, 2026-10-04).**

- **Capability.** The app looks for `CAPS2 b7` (`TRX_CAPS2_TRACE` in `trx_io.h`) before it uses the trace
  (`trx_ring_trace_present()`); without it every trace call answers "not there" and trxmon says "needs the instruction
  trace". The bitstream must set that bit with the former. LOG_CTRL: the firmware writes `LOG_ON_ALL | b4` (trace on, the
  default whenever the ring starts) and adds b9 (flush) to every write that puts the log on again after a restore, land,
  scrub, commit or load (`logctl(true)`); `trace_ctl(0)` drops b4.
- **History (firmware).** Epochs {first physical record, `tc_start`, anchor `{hw_ref, tc_ref}`}: pushed by
  `rebase_cycles` (restore, scrub rewind, commit; a commit of a land is the land's cut and adds none) and by the land
  itself (entry cycles, `land_c24`); the anchor of the open epoch follows every parsed marker, so `tc = tc_ref +
  sext24(c24 - hw_ref)` is at most a few frames from its anchor. A closed epoch keeps the records with `tc <= tc_start` of
  the next epoch (the instruction the cut stands on has no record in the next epoch: its fetch is before the snap); the
  cut is found by binary search (`ep_keep`). The history is the concatenation of the kept runs, clipped to the area
  (349,525 - 64 records); `gap` is set when a cut left no record at the cut's own cycle (within 8 cycles). Not done: the
  `.c64rering` delta ring from the trace (ringdump still writes it empty).
- **ABI (appended, `U2P_HAS(ring, trace_whowrote)`).** `u2p_ring_api_t` gets `trace_ctl`, `trace_status`, `trace_read`,
  `trace_writes`, `trace_whowrote`; structs `u2p_trace_rec_t` (24 bytes: cycle u64, write_start u32, pc u16,
  write_count u16, a x y sp p opcode b1 b2), `u2p_trace_write_t` (addr u16, old, new), `u2p_trace_hit_t` (cycle, addr,
  pc, old, new, index), `u2p_trace_status_t` (present, on, gap, records, capacity, lost, oldest/newest cycle).
  `u2p_trx_api_t` gets `video(u2p_video_t *)` (lines, cycles per line, frame cycles, cpu_hz, fps x100, hz60), read
  from the System Mode on each call. `trace_whowrote` reads the history's writes in chunks of 512 records and one stream
  0 range per chunk.
- **trxmon.** `trace.c`: `chis` (the registers after an instruction = the next record's pre-state, the newest
  record's the live ones while paused at an IB; at most the newest 500 instructions per answer), `whowrote` (old/new from
  stream 0's record, no caller chain), `triage` (crash_triage.rs: the transfer's own record holds the SP before the pop),
  [`tracering` removed from M1, A13/T66], `revdepth` (a number changes nothing),
  `trace on|off|status`; methods `runtime/who_wrote`, `runtime/crash_triage`, `runtime/set_reverse_depth` (read only),
  [`trace/build_from_ring` removed from M1, A13/T66]. `rstep` takes the record n back (`trace_rec_back`), lands on the frame at or before its cycle,
  STEP_CYCLES to it and checks PC and registers against the record. `rpc.c`, `ring.c` and `model` use `hw_video()`:
  `cyclesPerFrame` 19,656 or 17,095 and so on follow the System Mode, read each time they are used. Host tests
  (`apps/trxmon/host`, fake trace model): 445 checks.

---

## 5. Register map

**(see ARCH §10.)** The TRXD block at 0x101C0000 and the window at 0x101D0000 are gone.
Where the first draft's groups went:

| First draft | Now |
|---|---|
| §5.1 access rules (byte-wide, latch on byte 0, commit on the top byte), JTAG mirror | unchanged rules, one IO block at 0x101A0000; JTAG page 0x101 with `JTAG_PAGE` |
| §5.2 ID/CAPS/CMD/OWNER/SCOPE/STATUS, counters, `DMA_CYCLES` | page 0 (gate, owner, counters, IRQ cause) and page 3 (commands) |
| §5.3 stop record | page 3, without the flow fields |
| §5.4 CPU state port (C64 and drive) | state bus chips 1 and 9 (page 1); field list in ARCH §5.4 and the generated `ARCH-state-map.md` |
| §5.5 exec bitmap | page 3 (`BMP_IDX/DATA`); access bitmaps dropped |
| §5.6 comparator units 0-7 | page 3: watch units 0-1 (same field layout minus the bank qualifier), drive comparator |
| §5.7 raster/cycle comparators | raster = a watch unit source; `CYC_CMP` in page 0 |
| §5.8 flow stack | dropped (software) |
| §5.9 trace ring | page 2, stream 3 of the engine |
| §5.10 high window | dropped (engine `COPY`) |
| §5.11 session scratch | firmware buffer (`trx->scratch`) |
| §5.12 drive side | state bus chips 9-12 over the quasi-static bridge; drive comparator and step in page 3 |

The command codes of the first draft's §5.2 (PAUSE_CB/IB, RUN, STEP_*, RUN_CYCLES, SET_PC,
CPU_COMMIT, DRV_*, CLEAR_*, TRACE_*) are kept as the page-3 command set; `RUN_NOALIGN` stays as proposed (Q4), `FLOW_*` are gone.

### 5.1 As built (2026-10-02)

Code is the reference: `designs/c64_basic/trx_dbg.v` (C64 side, in `c64_core`, the C64's
clock) and `drv_dbg.v` (drive A, the drive's clock) carry the full map in their headers.
32-bit registers over the byte bus (`trx_sys.v` rules: byte 3 commits, byte 0 latches); JTAG
reaches them through `JTAG_PAGE` 3 (and page 0). `CAPS` b3 = page 3 present (`CAPS` 0xFF).

Page 0 (with T20's 0x100-0x114):

| Off | Name | R/W | Content |
|---|---|---|---|
| 0x118 | OWNER | W | b0: the debugger owns run control: the app's `C64_STOP` no longer stops the machine or its CPU (`top.v`) |
| 0x120 | CYCLES | R | phi2 cycles the machine ran (steps and DMA cycles included), bits 31:0; reading latches 47:32 |
| 0x124 | CYCLES_HI | R | bits 47:32 as latched |
| 0x128 | INSTRS | R | instruction boundaries passed |
| 0x130 | CYC_CMP | W | stop on the edge that ends cycle CYC_CMP (CYCLES[31:0] equals it before that edge; after the stop CYCLES = CYC_CMP + 1); writing arms it, one shot |
| 0x134 | CYC_CMP_EN | W | b31 armed (0 disarms) |

Page 3:

| Off | Name | R/W | Content |
|---|---|---|---|
| 0x000 | CMD | W | 1 PAUSE_CB, 2 PAUSE_IB, 3 RUN (releases the drive too; the resume still waits for frame alignment), 5 STEP_INSTR, 6 STEP_CYCLES (ARG; only with the gate shut; releases a CPU held by bus DMA), 0x10 DRV_PAUSE, 0x11 DRV_RUN, 0x12 DRV_STEP_INSTR (ARG; the C64 stays), 0x20 CLEAR_STOP (RUN and the steps clear STOP_NEW too) |
| 0x004 | ARG | W | b15:0 step count (0 = 1) |
| 0x008 | STATUS | R | b0 gate shut, b2 the last edge was an IB, b3 STOP_NEW, b4 CPU held by bus DMA, b5 the app's C64_STOP, b1 a debugger stop is in force (`dbg_stop`: the window's D page is open for it without `C64_STOP`), b6 hold off but still shut (waiting for frame alignment), b7 step running, b11:8 REASON, b16/b17 unit 0/1 on, b18 bitmap on, b19 CYC_CMP armed, b20 OWNER, b31:24 stops |
| 0x010 | STOP0 | R | `{POS {line 9, cycle 7}, DATA, FLAGS 3, UNIT 1, REASON 4}`; REASON 1 pause, 2 step done, 3 exec bitmap, 5 watch unit (address), 6 watch unit (raster), 7 CYC_CMP, 8 drive comparator (the C64 followed), 9 drive step done; FLAGS b0 IB, b1 the drive stopped first, b2 the step released a held CPU |
| 0x014 | STOP1 | R | `{ADDR, IPC}` |
| 0x040 | BMP_IDX | W | b10:0 |
| 0x044 | BMP_DATA | RW | 32 addresses per word; R/W increments BMP_IDX |
| 0x048 | DBG_CTRL | W | b0 exec bitmap on |
| 0x080 + 0x20·n | unit n | W | +00 CTRL (b0 enable, b3:1 type 1 IB / 2 read / 3 write / 4 read or write / 5 every cycle, b4 source raster, b6:5 action stop / count / log, b8 one-shot, b9 chain, b14 PREV_RET), +04 RANGE {HI, LO}, +08 DATA {DMASK, DVAL}, +0C REG {RMASK, RVAL, ROP b9:8 (== != < >=), RSEL b2:0 (A X Y S P)}, +10 PASS, +14 HITS (W clears) |
| 0x0B4 | HITS | R | {unit 1, unit 0}, 16 bits each (wrap) |
| 0x0C0 | DRV_CMP | W | b15:0 address, b16 enable, b17 type (0 exec at IB, 1 write) |
| 0x0C4 | DRV_STAT | R | {DPC, …, b3 comparator on, b2 step running, b1 held by the debugger, b0 halted} |
| 0x0C8 | DCYCLES | R | drive CPU cycles (may tear while the drive runs) |

The state bus gains chip 1 w3 `{7'd0, last edge was an IB, opcode at IPC, IPC}`. A stop raises
`IRQ_CAUSE` b0 (also a step's end, while the machine was already stopped). The log action puts
a stream-1 record of space 8 into the engine (`trx_dbg.v` header).

Differences from §5 and ARCH §7.1/§7.2, and why:

- **Stops are exact (Q3 answered):** every source closes the gate on the edge it sees; no `LATE`.
- **Register qualifier timing:** the qualifier sees the registers of the cycle that ends at the
  edge. On an IB edge the previous instruction's write-back to A/X/Y/P (and S by TXS) lands on
  that same edge, so for those the app re-checks after the stop (soft); S after stack
  instructions (`n`, `ret`) and every access-type hit are exact.
- **Range always:** one address is HI = LO; "any IB" is type 1 with 0000-FFFF.
- **PASS** is compared against HITS (no second counter): clear HITS when writing PASS.
- **HITS 16 bit** (both in one readable word), **CYC_CMP 32 bit**, **step count 16 bit**, the
  config registers write-only, no CYCLE in the stop record (read CYCLES right after the stop):
  each cut a read-mux word or a counter.
- **Raster source** = `{line, cycle in the line}` as one 16-bit value, so the same range compare
  serves (`rb L` = range {L,0}..{L,127}).
- **Log** goes to engine stream 1 (space 8), not a stream 3 trace former (not built).
- **Drive:** the comparator's type bit (exec or write) instead of two drive units; DCYCLES lives
  in page 3; the drive's IB does not tell an interrupt-replaced fetch apart; DRV_CMP is
  quasi-static (write it while the drive stands or is disarmed).
- **Not built:** RUN_NOALIGN (Q4), RUN_CYCLES (the app arms CYC_CMP), DMA_CYCLES, `held` kept
  off while owned and `dma_bus` following the real gate (§6.1), the stop interrupt as its own
  cause (the existing b0 is used), JAM (Q7). SET_PC / CPU_COMMIT are not commands: §5.1.1.

### 5.1.1 CPU write path as built (2026-10-03, branch `t21-cpuwrite`)

Code: `cores/icepi/6502n/proc_registers.vhd` (`nx_regs`), `cpu6510.v` (`st_nx`), `c64_core.v`
(state bus), `trx_dbg.v` (IPC_SET), `trx_io.v` (CAPS2).

- **Settled registers.** `proc_registers` exports `{P, S, Y, X, A}` as its next `clock_en`
  leaves them: the D side of those flip-flops written out as a combinational process (the
  same expressions; no reset, no load). State bus chip 1 **w4** `{S, Y, X, A}`, **w5**
  `{24'd0, P}`, live like w3 (meaningful while the gate is shut). At an IB they are the
  registers with the previous instruction's write-back done; w0/w1 are the flip-flops as they
  stand (A of `LDA #$42` still the old value: measured in `sim/dbg` test 8).
- **Writes** go through the T20 staging (page 1 `SB_DATA` at chip 1 w8-w14, `SB_LOAD` b1):
  the app's recipe is §3.3. No new command; `hw_cpu_write()` in trxmon (`hw_trx.c`) has the
  bit positions (A 7:0 … P 39:32, PC 55:40, IR 87:80, proc_control from 102: `copy_d2p` +12,
  `flags_update` +15, `trigger_upd` +16, `intg` +18; `din_r` 137:130).
- **Page 3 0x018 IPC_SET** (W) `{7'd0, IB b24, opcode b23:16, IPC b15:0}`: the reported PC,
  its opcode (PREV_RET's) and STATUS b2 after the app moved the CPU without an edge of its own
  (a redirect, or a ring land, §4.10.1).
- **CAPS2 b2** (page 0 byte 0x005, `trx_io.v`; b1 is T15's BUILD): the path is there; trxmon
  refuses writes and shows the raw flip-flops without it.
- **Simulation** (`sim/dbg` test 8, all other tests unchanged and passing; `sim/ring restore`
  still bit-identical): at the IB after `LDA #$42` w0 A = $00, w4 A = $42; an empty write
  (everything staged as it stands) then 200 instructions: RAM, registers, IPC, cycles and
  raster identical to the same 200 without it; `r a=77 x=99` then STA/STX store $77/$99 in
  8 cycles; `r x=55` keeps LDA's write-back of A; `pc=C018`: IPC and the IB flag at once,
  INC $C103 runs, 6 cycles, INSTRS +1; `fl` with C set right after CLC (whose flag write is
  still to come at the stop) reaches ADC (#1 + $10 + C = $12); the loop runs on afterwards.
- **Resources:** yosys `synth_xilinx`, `proc_core` alone: 675 → 761 LUT (+86; the D logic of
  the five registers once more, which Vivado may partly share with the flip-flops' own D
  side), FF unchanged; the two state-bus words ≈ 40 LUT in the read mux and IPC_SET ≈ 10
  (estimates): ≈ +135 LUT in all. **Vivado (CI run 297 against main's run 295, the same
  base):** Slice LUTs 25,780 → 25,864 (**+84**, 79.3 %), registers +4, BRAM and DSP
  unchanged; `c64_core` +93; WNS 0.190 ns (main 0.341 ns).
- **Board (2026-10-03, C64U, `t21-cpuwrite-latest` of CI run 297 in SRAM, firmware and
  trxmon of `t21-app2`):** a program assembled with `asm` at $C000 and started with `g c000`;
  `until c002` after the first `LDA #$42` (A was $00): `r` shows A = $42; `r a=77 x=05`, then
  `z` over `STA $0400`: $0400 = $77, cycle count +4 only; `r pc=c010`: PC and the IB at once,
  no cycle; `z 2` runs the routine there ($0402 = $99). After a ring land `r` is at an IB.
  Land, `r a=<A as it is>` and 1024 steps against the same without the write: registers,
  cycles and raster identical. `rstep` 5000…40000 exact again, the self-test passes.
- **Bench fix:** `sim/dbg/build.sh` preloads the C64 ROMs again (`rom2mem.py`,
  `-DTRX_SIM_ROMS`), which it had lost when `c64_mem.v` put the preload behind that define.

---

## 6. Hardware behaviour

### 6.1 Pause, run, step

The gate gets one more halt source. In `top.v`:

```
halt_req = machine_ctl[3]
         | (fw_c64_stop & !own_c64)
         | dbg_halt
```

Here `own_c64` is OWNER b0. `dbg_halt` is set by:

- PAUSE (at the next CB, or at the next IB)
- any stop source
- the end of a step

`dbg_halt` is cleared by RUN.

- **STEP_CYCLES n** opens the gate for n whole phi2 cycles. `c64_core` already shuts on
  `xc_phi2_n`. The step reuses the DMA path's "open for one cycle" logic without a DMA
  request.
- **STEP_INSTR n** opens the gate until n IBs have passed. A step is not a resume: it does
  not wait for frame alignment. The `shut` counter keeps counting only the left-out edges,
  so the final RUN still realigns the VIC to the 1080p frame (`c64_core.v` resume
  condition).
- **`held`.** While the debugger owns the machine, the first bus DMA does not set `held`
  beyond the current pause. RUN and the steps clear it. A step taken while `held` first
  releases RDY, and the report says the CPU had been parked at a read cycle (FLAGS).
- **The DMA window while owned:** `dma_bus` follows the real gate (`halted`), not the
  firmware's `C64_STOP` latch. A firmware or app DMA while the debugger runs the machine
  therefore takes the RAM port, and never a bus cycle on a running machine.

### 6.2 Firmware `C64_STOP` while the debugger owns the machine

`C64_STOP` keeps reading "stopped" (the stub returns `req | req<<1`), so the firmware's menu,
which believes the machine frozen, is not disturbed. When the app releases ownership, the
gate goes back to `C64_STOP`: the machine stays paused while the menu is open, and runs on
when the menu closes, as it always does.

### 6.3 Stops seen by the app

- **Polling.** A running `g` polls STATUS b3 every 1 ms tick (and `getch` for RUN/STOP).
  Optionally the stop IRQ wakes a waiting `trx->wait_stop` (Q8).
- **Asynchronous stops.** The stop record stays frozen until CLEAR_STOP. That is TRX64's
  asynchronous stop (864 §5.1): `g` answers "running until …", the stop is printed when it
  is seen, and `status` repeats it.

---

## 7. App ABI and C API

### 7.1 Root table addition (ABI 2.1)

`u2p_api_t` gains, at its end, `const u2p_trx_api_t *trx;`. It is NULL unless the ID
register reads `TRXD`. The app checks `U2P_HAS(api, trx)` and quits with "needs the TRX64
Ultimate bitstream" otherwise.

### 7.2 `u2p_trx_api_t` (firmware side; the register map is reached directly through `trx->regs`)

```c
typedef struct u2p_trx_api_s {
    uint32_t size;
    volatile uint8_t *regs;           /* 0x101A0000: ARCH §10, byte-wide */
    uint16_t version, caps;           /* copies of VERSION, CAPS */

    /* Ownership (§6.2). acquire fails with U2P_ERR_BUSY when JTAG owns it.
     * The loader calls release() when an app returns or traps, unless the
     * app set U2P_TRX_KEEP (REST sessions). */
    int  (*acquire)(uint32_t flags);  /* U2P_TRX_C64 | U2P_TRX_DRIVE | U2P_TRX_KEEP */
    void (*release)(void);

    /* Blocks the app task until STATUS.STOP_NEW or timeout; -1 on timeout.
     * Without the IRQ (Q8) it polls each tick. */
    int  (*wait_stop)(uint32_t timeout_ms);

    /* Firmware paths an app cannot take itself */
    int  (*machine_reset)(int kind);  /* 0 warm: C64::reset incl. cartridge re-install */
    int  (*drive_flush)(int drive);   /* write drive_mem's line cache back to DDR2 */
    int  (*drive_reset)(int drive);   /* 6502 only (drivepower) */
    int  (*rom_image)(int which, const uint8_t **p, int *len); /* 0 BASIC, 1 KERNAL, 2 CHAR */
    uint8_t *scratch;                 /* 2 KB, survives app returns (was the 0x800 page) */
} u2p_trx_api_t;
```

The `fs` table gains `opendir/readdir/closedir/remove` at its end (for `ls`, `rmdir`).

An SDK header `trx_dbg.h` (generated from ARCH §10 and the state map, the only copy) gives the app inline accessors:

```c
uint8_t  trx_rd8(off);
uint16_t trx_rd16(off);
uint32_t trx_rd32(off);
uint64_t trx_rd64(off);

void trx_wr8 (off, v);
void trx_wr16(off, v);
void trx_wr32(off, v);

void trx_cmd(code, arg);
```

Plus the field and bit names. A thin layer on top of it is the `MonitorHost` of TRX64 864 §3:

| TRX64 `MonitorHost` | here |
|---|---|
| `machine()` / `cpu(dev)` → `CpuView` | `mon_cpu_read(dev, regs*)`, `mon_cpu_write(dev, name, v)`, `mon_mem_read(lens, addr, buf, n)`, `mon_mem_write(lens, addr, buf, n)` (§3.4) |
| `resume(RunUntil)` → `Resumption` | RUN / RUN_CYCLES / a one-shot comparator. Always returns `Resumed`, as UE2 does (864 §5.1). |
| `step(n, over)` → `StopInfo{steps}` | STEP_INSTR, blocking (µs). `StepClass` from FLAGS b3/b4 (flow frames from the trace, ARCH §7.2). |
| `set_halted` | PAUSE_IB |
| `on_stop` | the stop record, printed by the UI loop, kept in scratch |
| `reset(kind)` | `trx->machine_reset`. `cold` is refused (864 §5: refuse rather than half-reset). |
| timeline / traces / media services | timeline (T20); traces: §5.9 (P4); media: `api->media`, `api->fs` |

### 7.3 Ownership rules

- The app acquires ownership on its first run-control verb (`g`, `z`, `bk`, …), not at start.
  Pure reading needs no ownership.
- On return or trap: the loader calls `release()`. Comparators and bitmaps are disarmed, the
  gate goes back to `C64_STOP`, and the stop record is cleared. With KEEP set (REST sessions
  or `/quit keep`), everything stays armed and the scratch holds the session. A stop while
  no app runs leaves the machine paused. Without the IRQ hook, nobody shows that stop until
  the next `trxmon` call (`status`). Q8 decides whether the firmware pops up a line.
- JTAG ownership (owner id 2) is exclusive. The app sees `U2P_ERR_BUSY`.

### 7.4 External API: the TRX64 runtime contract (revision 2026-10-02)

Decision (Alex, 2026-10-02): the monitor is reached from outside through **the same
contract C64RE uses against the TRX64 emulator**, 1:1, so C64RE drives the C64U without
code changes in its core. Contract source (TRX64 v0.12.1, `/path/to/Development/C64/Tools/TRX64`):
WebSocket JSON-RPC 2.0 on `ws://host:4312`, dispatch in
`crates/trx64-daemon/src/main.rs:6501ff`, reference `docs/wl-trx64-play-api.md`, monitor
verbs `crates/trx64-monitor/src/verbs.rs` and `MONITOR.md`; C64RE's client is
`C64ReverseEngineeringMCP/src/runtime/daemon-client.ts`.

- **Decision (Alex, 2026-10-02, supersedes the bridge idea):** C64RE abstracts both
  backends itself (C64RE spec `specs/889-a-second-backend-the-c64-ultimate.md`): the TRX64
  daemon, or a C64U = Gideon's U64 REST API (`doc/api/rest_api_openapi_u64.yaml`) for
  media, machine and input **plus** `trxmon.u2a`'s WS JSON-RPC for debug, state, ring and
  trace. C64RE finds a C64U by autodiscovery (Ultimate ident service, UDP 64, and
  `GET /v1/info`) and offers it only if the probe confirms our core and the app.
- So `trxmon.u2a` serves **only** debug/state/ring/trace; media/machine/input stay on
  Gideon's REST, unextended. A later move of everything onto our own API (our LVGL and web
  UI) remains possible and is invisible to C64RE behind its abstraction.
- **Identity for the probe:** the app (branch `trx64/main`) adds a `trx64` object to the
  ident JSON (`software/network/socket_dma.cc` ~605) and to `GET /v1/info`:
  `{"core":"TRX2", "caps":"0x000000F7", "build":"<bitstream git sha8>", "board":..., "rpc":4312}`;
  `core`/`caps` from the IO block's ID/CAPS registers at 0x101A0000, `build` from a small
  new register, `rpc` only while `trxmon.u2a` runs. `fpga_version`/`core_version` are not
  reliable (same scheme as stock). Product "Ultimate 64-II" is the same on C64U and UE2.
- `ping` answers `{runtime_version:"trx64-runtime/2", version:"trxmon <ver>", backend:"c64u"}`.
- No second `.u2a` as facade on the device.

Must-haves for the first backend (C64RE session, 2026-10-02; the media/input part is served by REST, not by the app): `ping` (answers
`runtime_version: "trx64-runtime/2"`, exact match), `session/state`,
`session/read_memory` (lenses ram|io|cpu|rom|cart), `debug/pause|continue|run`,
`session/run {cycles}` (bounded, may be coarse, must stop), `monitor/exec` (TRX64 verbs),
`session/screenshot` or `session/frame_indices`; media/input (`media/open|mount|unmount|
persist`, `session/load_prg`, `runtime/run_prg`, `session/type|key_down|key_up|
joystick_set|joystick_clear`); ring (`checkpoint/capture|list|restore|pin|unpin`, over the
T20 ring, `.c64re`/`.c64rering` compatible); trace (`trace/start_domains`,
`trace/run/status|mark|stop`, `trace/read`; the output `.c64retrace` is not in M1, A13/T66). Notifications:
`debug/breakpoint_hit`, `debug/paused|running`, `debug/observer_hit|observer_log`.

Not offered on hardware, answered with JSON-RPC error -32601 and a sentence naming the
way out ("not on C64 Ultimate hardware; use a local TRX64 sandbox"): `sandbox/*`,
`runtime/candidate_*`, `runtime/scenario_*`, `runtime/overlay_run`,
`runtime/promote_branch`, snapshot-tree branching, `vic/line_trace|frame_map|inspect/*`,
`recorder/*`, `vsf/*`, `snapshot/undump`, `session/model`, `audio/export`, `batch/*`.
Trace domains the hardware lacks are refused by name, never recorded empty. A
`capabilities` array in the `ping` result is proposed as an additive TRX64 contract change
(needs the TRX64 session's agreement; C64RE builds its side).

A/V: the WS binary frames (VIC index frames, audio) are not served by the app (lwIP on
the 100 MHz RISC-V cannot carry ~5 MB/s); the UI's live view uses the existing UDP
streams. RPC clients connect with `?av=0`.

The old app route `PUT /v1/apps/trxmon:run` (4 KB text reply) stays as a debugging aid only.

### 7.5 As built (app side, 2026-10-02, `1541ultimate` branch `t21-trxmon`)

Code is the reference: `apps/trxmon/src` (`rpc.c` methods, `mon.c` verbs, `mach.c` run
control and lenses, `hw_trx.c` registers), host tests in `apps/trxmon/host`, a client in
`tests/trxmon_client.py` here.

- **ABI 2.1** (`software/app_loader/app_api.h`): two tables at the end of the root.
  `net` (`listen`, `accept`, `recv`, `send`, `close`, `wait`; non-blocking TCP, the loader
  closes what an app leaves open) is new: an app cannot reach lwIP otherwise. `trx` is
  §7.2 cut to what the firmware must know: `regs`, `caps`, `acquire` (PAUSE_CB, then
  OWNER), `release` (bitmap, units, drive comparator and CYC_CMP off, OWNER 0, RUN; the
  loader calls it when the app returns) and `announce` (the RPC port for the ident).
  `wait_stop`, `machine_reset`, `drive_flush`, `drive_reset`, `rom_image` and `scratch`
  are not there yet (the app polls STATUS; ROMs come through the firmware's apertures).
- **Identity:** `trx64: {core, caps, board, rpc?}` in the UDP 64 ident JSON and in
  `GET /v1/info`; `build` is left out until a register carries the bitstream's sha.
- **Methods:** `ping`, `session/create|list|close|state|run|read_memory`,
  `debug/state|pause|continue|run|step|break_add|break_del|break_list`,
  `monitor/exec|state`, `daemon/keep_alive`, and `trxmon/quit` (ours). Notifications
  `debug/running|paused|stopped|breakpoint_hit|observer_hit`. `session/read_memory` also
  takes C64RE's `{addr, length}` and answers `bytes` as numbers.
- **-32601 with a sentence:** sandbox-only methods ("use a local TRX64 sandbox"),
  media/machine/input ("the Ultimate's REST API"), `checkpoint/*` and the ring ("needs
  the T20 delta ring"), `trace/*` ("needs the trace engine").
- **Breakpoints** are the exec bitmap (any number); `obs ... when load|store` takes a
  watch unit each (two), `when exec` the bitmap; only `do break`, no `if`. `until`, `n`
  over a JSR and `ret` borrow both units for the run (§3.3: S compares are exact after
  stack instructions) and answer when the stop comes (capped at 10 s, then paused).
  `session/run {cycles}` arms CYC_CMP and, if the stop is mid-instruction, steps to the
  next IB.
- **Lenses:** RAM through the DMA window with `C64_DMA_MEMONLY` set for the whole
  session; ROMs through the firmware's apertures; `io` from the state bus (VIC, CIA 1/2).
  SID and colour RAM are not on it: `m`/`io` print `--`, `read_memory` gives 0, `sidefx on`
  reads them with bus cycles while paused. *(The first version read `cart` from DDR2 0x3C00000
  with `bank * 16 K`; replaced by the look and the forced bus cycle, §3.4.1.)*
- **Not available yet:** flow frames (`r` prints `?` as the flow), `ls`/`rmdir` (the app ABI
  has no directory listing, §7.2's `fs` addition), the trace verbs, the drive's registers and
  VIAs. *(T55 built flow, `ls`/`rmdir` and the `drv_dbg` part of `io 1541` on 2026-10-08, §8.13.)*

**Added 2026-10-03** (`1541ultimate` branch `t21-app2`):

- **Registers at an IB** come from the settled words (chip 1 w4/w5) when CAPS2 b2 is set,
  so `r` after `z` over `LDA #$42` shows A = $42 (before: the old A).
- **`r a= x= y= sp= fl= pc=`** and **`g <addr>`** (TRX64's syntax and `set …` answer):
  `mach_cpu_write()` merges into the settled registers and stages them (§3.3, §5.1.1); a
  write the gate has not loaded yet is shown as made (SB_LOAD's armed bit). Refused while
  running ("pause it first"), mid-instruction, without CAPS2 b2, or when the opcode at the new
  PC is in I/O the state bus does not show.
- **ABI 2.1, `trx->ring`** (a table at the end of `u2p_trx_api_t`, NULL without the engine):
  `status`, `frame` (seq, c24, cycles, snap position, flags, target), `find`, `land`,
  `commit`, `error`, `dump` (.c64re), `save` (.c64rering), `load` (.c64re or a .c64rering's
  current entry). The loader commits an open land when the app ends.
- **P5 ring verbs and methods**: §4.10.1.
- **`device drive8`** (TRX64: a read-inspect device, r/m/d; here also `z` and `bk`, §4.2):
  `m`/`d` and `session/read_memory {space: "drive8"}` read drive A's 64 K image in DDR2
  (`drive_unit.vhd` g_ram_base 0x0EE0000; the drive's CPU writes through, so no flush), the
  VIAs ($1800-$1FFF) print `--`; `r` is TRX64's 1541 layout with the PC and cycle count
  `drv_dbg` has (its registers, VIAs and head are not on the state bus: `--` and `?`); `z [n]`
  steps the drive alone (DRV_STEP_INSTR, the C64 paused); `bk` takes the one drive
  comparator. Anything else answers TRX64's "read-inspect only".
- **Files** (TRX64's mini-shell over `api->fs`): `pwd`, `cd` (checked against the parent's
  listing), `mkdir`, `load "f" [a]`/`save "f" a1 a2` (PRG), `bload`/`bsave` (raw), `asm-file
  <p> [a]`; RAM through the RAM port; the working directory is the app's (`/USB1` at start).
- **`bt`**: TRX64's stack scan for JSR return addresses (no flow frames).
- **`a` and `asm`** (`asm6502.c`, TRX64's `assembler.rs`: `assemble_line` over the
  disassembler's own table, documented opcodes only, and `assemble_block` with labels,
  `name = e`, `*`, `<`/`>`, `+`/`-`, `.byte`/`.word`, one `*=`/`.org`, two passes): `a <a>
  [instr]` and the modal assemble mode, `asm <a> [<<TAG]` with the body in the command (RPC)
  or typed up to `end`. Host tests: all 151 documented opcodes round-trip with `d`, TRX64's
  error sentences, a block with forward references.

### 7.6 Run App from the menu (app branch `t21-p4-runapp`, 2026-10-04)

The `.u2a` file type already offers "Run App" in the file browser's context menu
(`filetype_u2a.cc` -> `APP_CMD_RUN`), and an app with a manifest `tasks` entry (trxmon:
"TRX64 Monitor (RPC 4312)", category C64 Machine) appears in the task menu through the
registry. What stopped both from the overlay (HDMI) menu was the loader's freeze rule: the C64
was running and it answered "Apps require Freeze mode". `AppLoader::run_app` now does for every
launch what it did for REST only: if the machine runs, it pauses it (`MENU_C64_PAUSE`) for the
app's run and resumes it afterwards (`handed_over` or a failed load resume it at once). trxmon
detaches (`trx->detach`), so the menu call returns as soon as `app_main` has; the machine is
resumed then and trxmon's own hold (`acquire`) takes over. Refusals keep their popups: an app
still resident in the background, an ABI or format mismatch (`load_app`), a pause that fails.

**A person ends the background app (Alex, 2026-10-04).** An app started by hand from the menu
(file browser "Run App", task menu: the command has a user interface) first ends a detached app:
the loader sets the request that `trx->stop_requested()` (appended to the trx table) reports,
and waits up to 3 s for the task to return. trxmon polls it in its serve loop next to
`trxmon/quit`. Only if the task does not end in time does the popup "An app still runs in the
background" stay. **Then it starts the app asked for** (board 2026-10-07: until then a menu start of the
same app only ended it, with the popup "TRX MONITOR ended", and nothing started; §8.12). REST launches (no user interface) are still refused with 423 while one runs,
so a script never ends a monitor session someone else uses.


---

## 8. UI

The screen is the TRX64 monitor's verbs on the overlay, nothing more: no panels, no second
layout. Every verb is `mon.c`'s (TRX64's text). The overlay is 40 x 25 characters in every HDMI
mode (`U64Config::DetermineOverlaySettings`), and `screen_open_full` gives a 40 x 23 window from
row 2; a design wider than 40 columns was Q1; the 80-column screen of 2026-10-05 is built, §8.10.

### 8.0 The scenario this exists for

1. The person opens the monitor (menu), or a C64RE session (RPC 4312) is attached, or both.
2. `bk`, `obs ... ` are set: by hand, or by the LLM over RPC (monitor/exec, debug/breakpoint/...).
3. The person leaves the monitor with `x` and plays with the full C64 picture: the overlay is
   closed, trxmon only watches the debug block (no slowdown beyond what armed stops cost in
   the hardware).
4. A breakpoint or observer fires: the machine stops (hardware), trxmon sees the stop record,
   asks the firmware to open the overlay (`trx->ui_request`, §8.5) and the monitor screen
   comes up on its own with the stop printed and the prompt ready. The RPC clients get
   their `debug/breakpoint_hit` / `debug/stopped` notifications exactly as before.
5. This also holds when trxmon was started headless (REST `action=serve`, no screen): a stop
   opens the monitor on the overlay for the person, too.

### 8.1 Entry

- Task menu (F5): category **Developer**, "TRX64 Monitor" (manifest task, action `serve`).
  Next to it sits Gideon's "Machine Code Monitor" until §8.6 takes it out of our build.
- The file browser: `.u2a` "Run App" (also with the manifest task), and Ctrl-O in the file
  browser (`tree_browser.cc`), which opens trxmon from the app registry instead of the built-in
  monitor (§8.6). Not installed: a popup says so.
- A stop while trxmon runs resident (§8.5).
- **One connection, ping with capabilities (Alex 2026-10-06, C64RE spec 889; app 2fcac32b/5390a16e):** trxmon serves exactly one RPC client on 4312. A second one gets, over WebSocket, a JSON-RPC error -32001 "port 4312 is held by <ip:port> (trxmon serves one RPC connection at a time)" and close 1013 with that reason; a plain HTTP/TCP client gets HTTP 503 with Retry-After: 5 and the same text; a holder that does not finish the upgrade within 5 s loses the place. `ping` answers `runtime_version`, `version:"trxmon <git sha>"`, `backend:"c64u"`, `build:{trxmon_git, firmware_git, firmware_version}`, `capabilities:{methods:[the dispatch table], notifications:[the five sent]}`. Open: a holder gone without FIN keeps the port (no keepalive).
- **Fixed 2026-10-06 (app 5b605bb0):** until then the manifest had no `rest` entry, so `PUT /v1/apps/trxmon:run` answered 403 ("cannot be started over REST", route_apps.cc); only `PUT /v1/apps:run_file?app=/Flash/apps/trxmon.u2a` worked. The manifest now declares `"rest": [ "serve" ]`. An installed trxmon keeps the old manifest in /Flash/config/apps.json until it is reinstalled (T67).
- REST `PUT /v1/apps/trxmon:run action=serve` has no user interface (`ctx->has_ui == 0`): the
  app detaches and serves headless as before (§7.6). With a user interface the same action opens
  the screen in the foreground; the RPC server keeps serving in the same loop.
- The loader pauses a running machine for the app's run (§7.6); the monitor starts PAUSED.
- Never through a freeze (§2.1). If the firmware is in C64-screen menu mode (it has frozen
  and backed up), the monitor warns: "entered from a freeze: VIC registers and $0800-$0FFF
  are the menu's, not the program's".

### 8.2 Screen

- `screen_open_full` (40 columns on the overlay, 23 rows: y 0..22 of the window).
- **Row 0:** status: `PAUSED IB $c012 cy 123456789 r120/031` (40 characters, cut at the right)
  or `RUNNING`, `RUNNING until $c100`, `RUNNING next`, `RUNNING ret`.
- **Rows 1-2:** `r` in short form (`  ADDR AC XR YR SP NV-BDIZC 01` / `.;C012 ..`). While the
  machine runs the registers cannot be read: the two rows say `registers: running`.
- **Rows 3-21:** the output scroll area, 19 lines, fed by a 200-line history buffer. A typed
  line is echoed with its `.` prompt, then the verb's output; a line longer than 40 characters
  wraps hard at column 40.
- **Row 22:** the command line with a `.` prompt (VICE/TRX64 style). Longer than 39: the tail is
  shown. The cursor is a reversed cell.
- **Narrow formatting.** On the overlay, `m` prints 8 bytes per row (`>C:c000 xx xx xx xx xx xx
  xx xx` and the 8 characters, exactly 40 columns, rows aligned to 8) and `d` drops the raw
  bytes column when the line would be longer than 40 columns. REST and the session log always
  get TRX64's full-width text, and the golden transcripts compare that text (§9.2). The width is
  a mode of the formatters (`mon_set_narrow`), set around the UI's own `mon_exec` calls only.
- **While running:** the window shrinks to the bottom 6 rows so that the C64 picture shows above
  it: row 17 status, rows 18-19 the registers line, rows 20-21 the last two output lines, row 22
  the command line. The rows above are drawn with colour index 0, which the overlay makes
  transparent (`overlay.v`: `r_tr`, the TRANSPARENCY register is written 0xC0), so the C64 shows
  at half brightness there. (`screen_open_full` cannot move; the window stays full size and the
  top 17 rows are blank and transparent.)
- **While paused:** the HDMI window is black (the scaler holds 64 lines; docs/machine-pause.md). The
  firmware draws the captured still and, for `screen`, the decoded text screen into the
  pixel layer (ARCH §7.5, §9.2); the monitor's own text stays on the overlay.
- Redraw: only rows that changed, at most every 100 ms; a key redraws at once.
- The 80-column screen (4 K screen RAM = 80 x 50): §8.10. This section is the 40-column fallback, used when the firmware has no `screen_open_wide` or the menu is on the C64's own screen.

### 8.3 Keys

Gideon's keymapper turns F1 into PageUp, F3 into Help, F5 into Tasks and F7 into PageDown
(the Ultimate interface; the Commodore one lays them out differently, `ui->fkey` tells);
the app reads them as those codes.

| Key | Action |
|---|---|
| RETURN | execute the line |
| cursor up/down | history (32 lines) |
| PageUp/PageDown (F1/F7) | scroll the output by a page |
| Help (F3) | `help` |
| RUN/STOP | while the machine runs: pause (at the next IB). While paused with an empty line: nothing (no accidental exit). |
| RETURN on an empty line | repeats the last `z`/`n`/`cs` while paused [UI only; not TRX64] |
| left arrow (the C64 key, code '`') | delete the line |
| DEL (backspace) | delete the last character |
| Tasks (F5) | ignored (the firmware's) |

The spec's C= + RETURN cannot be read: the firmware's keymap gives code 0 for it
(`keyboard_c64.cc`, `keymap_control`), so the repeat is a plain RETURN on an empty line (VICE
does the same).

**Known limit (firmware without `ui->getch_monitor`):** `getch` uses the default keymap. With "User
Interface / Navigation" set to WASD cursors, `w a s d` arrive as cursor keys and cannot be typed.
`getch_monitor` (§8.10) removes it.

### 8.4 Leaving

- `x` (the `g` verb: resume) leaves the screen. With **no stop armed** (no `bk`, no observer, no
  watch, no cycle compare) trxmon ends completely: the sockets close, the debug block is
  released, nothing stays in the background. With **stops armed** it does not end: it goes
  resident (§8.5) and watches. In both cases `x` also **closes the whole overlay menu** (the
  file browser trxmon was started from goes away, the C64 runs on): `ui->close_menu` (§8.8). Before,
  the browser stayed open under the running machine and a break right after `x` could only reopen the
  monitor when the person closed it (the `ui_request` waits for the menu, §8.5).
- `/quit`: leave, **disarm every stop** (the loader's release does it), the machine unchanged,
  trxmon ends completely.
- The person can always leave with keys: `x` and `/quit` are typed at the prompt, RUN/STOP
  pauses a running machine first. A screen that is gone (`getch` = -2) ends the session. The
  RPC server never blocks the key loop: the loop polls the keyboard on every pass of at most 5 ms.
- An RPC `trxmon/quit` ends trxmon whatever the screen shows.

### 8.5 Stops, the resident watch and the monitor reopening

**States of the UI session** (`ui.c`, tested on the host):

```
CLOSED --open--> PAUSED  --g/until/n/ret/x--> RUNNING (shrunk, 6 rows)
                  ^                                |
                  +---- stop (bk/obs/until/pause) -+   full size, the stop text, `r`, prompt ready
PAUSED --x, stops armed--> RESIDENT --stop--> REQUESTED --ui callback runs--> PAUSED
PAUSED --x, none armed / /quit--> ENDED
RESIDENT (headless REST serve is the same state from the start)
```

- **Monitor open, machine runs** (`g`, `until`, `n`, `ret`, `/run`): RUNNING layout (§8.2). The
  stop (breakpoint, observer, until/next/ret landing, RUN/STOP pause, a cap) puts the screen
  back to full size, prints the stop and the `r` line; the prompt is ready.
- **The stop text** for a breakpoint or observer (as built, `mon_stop_text`): `breakpoint N hit
  at .C:xxxx` / `observer NAME hit at .C:xxxx` followed by the disassembly line at PC; the
  run-to verbs print their TRX64 landing line (`mon_finish`).
- **Leave with stops armed:** trxmon detaches (`trx->detach`) and the resident serve loop
  keeps the RPC server and watches the stop record. The overlay is closed: playing is not
  disturbed. When a **breakpoint, observer or JAM stop** (reasons `breakpoint`, `observer`, `jam`; not
  `pause`/`step`/`until`/`next`/`return`, which a client asked for) comes in with no UI session
  open, the loop calls `trx->ui_request(cb, arg)` (below) once per stop. A stop over an RPC
  client's own run request (`debug/run`, `monitor/exec until`) is answered to the client as
  before and opens the screen too: the person sees where the machine stands. The resident
  loop keeps serving RPC while the request waits.
- **Headless (REST serve):** the same resident loop from the start; stops set over RPC open the
  screen for the person. If `ui_request` says unsupported (no overlay UI) the stop is only
  notified over RPC.
- **ui_request** (`u2p_trx_api_t`, appended after `stop_requested`; guarded by
  `U2P_HAS(trx, ui_request)`):

  ```c
  /* Queues "open the overlay and call fn(arg)": the firmware does what the menu button does -
   * takes the overlay, calls fn in the UI task with a valid ui table (app_api_set_ui), and when
   * fn returns draws the menu underneath again and gives the overlay back. For a detached task,
   * which must not use the ui table itself. U2P_OK: queued. U2P_ERR_BUSY: one is queued
   * already. U2P_ERR_UNSUPPORTED: no overlay user interface. Dropped when the detached task
   * ends. */
  int (*ui_request)(void (*fn)(void *arg), void *arg);
  ```

- **Loader side (as built):** `app_api_net.cc` keeps one pending `(fn, arg)`. The init-task loop
  in `ultimate.cc` (the one that polls the menu button) checks it next to the button: when a
  request is pending it calls `overlayUserInterface->run_once()` exactly as for the button.
  `UserInterface::run_once` calls the new `open_request_hook` after `menu_enter_hook`; the
  hook runs the request and `run_once` then closes the overlay again instead of entering the
  menu loop. That loop is the **queue**: while a person is in a menu or an app (`run_once`
  or `run_app` runs inside it), the loop does not poll, so the request waits and opens the
  monitor when that closes. The Freeze interface type does not matter: the monitor is the
  overlay's, never a freeze (§8.1).
- **Trxmon side:** `cb` runs the same `ui_session()` as the foreground start, on the UI task. The
  resident task must not touch the sockets meanwhile: `cb` raises `want_ui`, the resident loop
  acknowledges by parking (`in_ui`) and `cb` serves RPC itself in its key loop; when the session
  ends `cb` hands the sockets back. If the session ended with `x` and nothing armed, or `/quit`,
  `cb` sets `end` and the resident task returns from its function (trxmon is gone).
- **Persistent state:** the output history, the command history (32) and the sticky lens stay in
  the app's memory for the life of the resident task; they are gone when trxmon ends.

### 8.6 The base firmware's Machine Code Monitor is out of our build (Alex, 2026-10-04)

The TRX64 monitor replaces `software/monitor/*` ("Developer" > "Machine Code Monitor", Ctrl-O).
The sources stay in the tree (minimal upstream diff); the objects are dropped from
`target/u64ii/riscv/ultimate/Makefile`, and every hook that referenced them is a weak symbol that
links without them. Ctrl-O (`tree_browser.cc`) opens the TRX64 monitor from the app registry;
if the app is not installed a popup says so. The TRX64 monitor needs the TRX64 bitstream; on
the original bitstream trxmon refuses with its popup.

### 8.7 As built (app branch `t21-p4-app`, 2026-10-04)

- **Files:** `apps/trxmon/src/ui.c` (model: rows, keys, history), `main.c` (the session loop, the
  detached watcher, `ui_callback`), `rpc.c` (`rpc_local_exec`: a run-to verb typed at the screen
  waits like a client's; `rpc_take_unclaimed`, `rpc_take_user_stop`, `rpc_stops_armed`), `mon.c`
  (`mon_set_narrow`, `mon_stop_text`), `dis6502.c` (`dis_set_raw`). Firmware: `trx->ui_request`
  (`app_api.h`, `app_api_net.cc`), `open_request_run` (`app_loader.cc`), `UserInterface::run_once`
  and the init-task loop (`ultimate.cc`, `doIt = 3`), `AppRegistry::run_task` and the weak
  `app_open_trx_monitor` for Ctrl-O (`app_registry.cc`, `tree_browser.cc`).
- **Which stops reopen the screen:** `breakpoint` and `observer` (a stop record whose reason is one
  of those), whoever was waiting for it. Armed means: an enabled `bk`, an observer that is on, a
  drive comparator, a `debug/run` cycle cap.
- **Running layout:** `screen_open_full` cannot move or shrink, so the window stays 23 rows and
  the top 17 are drawn blank with background 0. The overlay core (`overlay.v`) draws the C64 at half
  brightness inside the whole box, so the picture above the 6 rows is dimmed, not full. Full
  brightness would need the overlay's `ACTIVE_LINES` changed, which the ABI does not offer.
- **Last cell:** the cell at column 39 of row 22 is never written. Writing it makes the firmware's
  `Screen::output` wrap and scroll the whole overlay, so the command line is 38 characters wide at
  most (the tail of a longer line shows).
- **Wrapping:** at the last space of the second half of the row, else hard at 40. The verbs'
  UTF-8 dashes and arrows become `-` and `>`, other bytes the overlay has no glyph for `?`/`.`.
- **Memory:** the 200-line output and the 32 command lines live in the app's static memory, so
  they survive a reopen while trxmon is resident and are gone when it ends (not `trx->scratch`:
  the table has none).
- **`r` rows** show the C64's registers also while `device drive8` is set; `r` typed at the prompt
  shows the drive's.
- **Hosts tests:** `make -C apps/trxmon/host test`: `trxmon_test` (540 checks: the 445 of before, the
  line editor, key map for both interfaces, wrapping, namespaces, narrow `m`/`d`, two golden
  screens, runs and stops, RUN/STOP, repeat) and `trxmon_ui_test` (38 checks: `app_main` with
  a scripted keyboard, a text window, the detached task as a thread and `ui_request` handed to
  the test: menu start, `x` with and without stops, `/quit`, reopen twice, headless, no
  `ui_request` in the firmware, a request already queued).
- **Not tested on hardware:** all of it. The firmware part (`run_once` hook, the loop, the
  overlay ownership around the callback, the 40-column drawing) has only been compiled.

### 8.8 As built: reopen after `bk` + `x` (branch `t21-p4`, app `t21-p4-app`, 2026-10-05)

Found by the UE2 session (patching `delay_ms(2)` to `(5)` made the screen return) and by a stop with no session
(T12 §14.8 point 10). Two causes and one gap:

1. **`delay_ms` was not a sleep.** `api_delay_ms` was `vTaskDelay(pdMS_TO_TICKS(ms))`; the tick is 200 Hz
   (`configTICK_RATE_HZ`), so `pdMS_TO_TICKS(2)` is 0 and `vTaskDelay(0)` is a bare yield to tasks of the same
   priority. `ui_callback` runs in `app_ui` (`tskIDLE_PRIORITY + 2`), the resident serve loop in `app_resident` at
   `tskIDLE_PRIORITY`: the loop never ran, never answered with `in_ui`, and after 1000 ms the callback gave up
   (`s_want_ui = 0`). **Fix at the root:** `api_delay_ms` (`app_api_impl.cc`) sleeps for `ceil(ms / tick)` ticks plus
   one (the first tick of `vTaskDelay(n)` ends at any time), so `ms > 0` is never shorter than `ms` and never a bare
   yield; `ms = 0` stays a yield. Documented at `delay_ms` in `app_api.h`. Other callers: `trex_crt` `ui_shim.c`
   `delay_ms(10)` (2 -> 3 ticks), `c64re` `delay_ms(20)` (4 -> 5 ticks), trxmon's own waits (2 ms = 0 ticks before, now `plat_sleep(10)` = 3 ticks);
   `plat_yield` is `delay_ms(0)` and unchanged. Nothing else in the tree calls the ABI's `delay_ms` (the firmware's
   own `delay_ms` in `software/monitor` is another function).
2. **trxmon does not depend on the rounding.** The three waits (`ui_callback` for the park and for the release, the
   serve loop while the screen runs) are `plat_wait_flag` (`mach.c`): a look at the flag, then `plat_sleep(10)`,
   until the flag, the serve loop's end or the timeout (1000 ms for the park). `plat_sleep` is
   `delay_ms(max(ms, PLAT_SLEEP_MIN_MS = 10))`, two ticks even on a firmware that still rounds down
   (3 with the fix above).
3. **`x` left the overlay on the file browser.** The `ui_request` queue is "the UI task is free" (§8.5); a menu
   the person left open with `x` kept it busy. The ABI got `ui->close_menu` (appended to `u2p_ui_api_t`, guarded
   by `U2P_HAS(ui, close_menu)`): the app asks, `run_app` (`app_loader.cc`) takes the flag when the app returned and
   answers `MENU_HIDE` for the command (`menu_response_to_action`, as `dma_load` does for a started program)
   instead of drawing the browser again; the browser's context menu hands it up, Ctrl-O (`tree_browser.cc`) returns
   the response now too. Ignored when the app started from REST (no menu) or handed the machine over. trxmon calls
   it after the screen session ended with `x` (not `/quit`, which goes back to the browser; not when the loader
   wants the app to end). A request from the firmware (`ui_request`, §8.5) closes the overlay itself.
- **Host tests:** `trxmon_test` 562 checks (+10: `plat_wait_flag` after n sleeps, already there, timeout, serve
  loop ended, waiting for a flag to drop; every sleep is at least `PLAT_SLEEP_MIN_MS`), `trxmon_ui_test` 44 (+6:
  `close_menu` once after `x` with stops armed and with none, not after `/quit` or a gone screen, not by the
  firmware's own request; no wait of the callback shorter than `PLAT_SLEEP_MIN_MS`).
- **Not tested on hardware:** `bk` + `x` + break (the UE2 session tests the firmware and `trxmon.u2a` of this branch),
  `ui->close_menu` on the board.

---

### 8.9 As built: a landed stop is a session (branch `t21-p4`, app `t21-p4-app`, 2026-10-05)

Found by the UE2 session: after `rstep` (reverse step = land on an earlier frame) `m io d800` and every other
session read showed `--`. Measured in UE2: SH_CTRL mode 1 with a session in every stop before `rstep`, mode 0 after.

- **Cause (read in the code).** `land_locked` -> `scrub_begin_locked` -> `stop_machine` writes `SH_CTRL` 0 (a land's
  bus cycles must reach the chips, not a session's overlay) and only `commit_locked` wrote the saved mode back. A
  debugger stop is a session only with `SH_MODE != 0` (T12 §14.8; `cond_dbg_on` in `freeze_resp.v`), so the stop a
  land ends in had none until the next commit. The same path serves `goto`, `frame +-N`, `rewind` and every `rstep`
  that lands; the Timewarp UI's scrub and final rewind do not use `land_locked` (they release through
  `scrub_final_locked`, which restores the mode itself) and are unchanged.
- **Fix (`trx_ring.cc`, `land_locked`).** The mode of the stop the land starts from (`sh0`) is read first. A land
  from a land (`scrubbing` already set, the stop is a session) writes `SH_CTRL` 0 before its first bus cycle; the
  first land has `stop_machine` do it. When the machine stands on the load edge, held by the debugger, `SH_CTRL`
  gets `sh0` back: the session starts then from the restored chips (the responder keeps no copy from before,
  ARCH §7.6; `block` is clear because the machine ran to the load edge). The error path writes it back after
  `PAUSE_CB`. `commit_locked` still restores `scr.sv.sh`; the value is the same.
- **Host tests:** the fake hardware has no SH_CTRL and a land leaves its `session` flag alone, so it models the
  fixed board: `test_ring` checks that a colour RAM read after `frame -3`, `goto` and `rstep` is not `--`. It cannot
  show the bug itself; that is for the UE2 and board runs.

### 8.10 As built: the 80-column screen (app branch `t21-80col-app`, 2026-10-06)

Decision 2026-10-05 ("Decision ... 80 columns" below) built. **No RTL change:** `overlay.v` already takes
`r_cpl` (0x04) and `r_nl` (0x05), 8 and 6 bits, and indexes screen RAM with `row_ptr + char_x` (12 bits);
80 x 40 = 3200 cells fit the 4 KB, and 80 x 12 px = 960 px gets scale 1 (`sx`) at 1080p. Everything is
firmware and app.

**ABI (`software/app_loader/app_api.h`, appended to `u2p_ui_api_t`, `U2P_HAS` guarded):**

- `void *(*screen_open_wide)(int rows)`: the overlay becomes 80 cells per line and `rows` lines (12..50)
  while the returned window is open; the window covers all of it from row 0 (`win_get_size_x/y` = 80,
  rows). NULL when the menu is not on the overlay (a freeze menu on the C64's screen) or a wide screen is
  open: nothing changed, the app uses `screen_open_full`. trxmon asks for 40 rows (80 x 40 = 960 x 920 px
  at 1080p).
- `int (*getch_monitor)(void)`: `getch` through `e_keymap_monitor`, the keymap Gideon's monitor reads with.

**How the mode is switched** (`Overlay`, `overlay.h`): `update_settings` keeps what the firmware asked for
as `base` and applies it through `apply()`; `begin_wide(cols, rows)` backs the 40 x 25 cells up
(`Screen::backup`, the cursor off first), writes `CHARS_PER_LINE` / `ACTIVE_LINES` and the screen size and
clears; `end_wide()` writes `base` again and restores the cells. A video-mode change while wide
(`U64Config` calls `update_settings`) re-applies with the wide size.

**Every way out puts the mode back:**

| Exit | Where the 40 x 25 comes back |
|---|---|
| `x` (nothing armed), `/quit`, `trxmon/quit`, the loader wants the app to end, `getch` = -2 | trxmon's `ui_session` ends in `screen_close`, which ends the wide mode (`api_ui_screen_close`: window deleted, `end_wide`, then the same `clear()` every window's close does) |
| `x` with stops armed (resident) | same `screen_close`; the watcher task never holds the screen |
| a break re-opens the screen (`ui_request`) | the callback closes its window; `open_request_run` calls `app_api_overlay_restore()` after the callback in any case, then `appear()` |
| the app returns without closing (a fault in the app's own code that still returns) | `run_app` calls `app_api_overlay_restore()` after the app task is done, before the UI is un-set |
| the person starts another app | `run_app` restores before the next app's task starts |
| the overlay is given back by anything (menu button, `machine_handover` for `media->run`) | `Overlay::release_ownership` calls `end_wide()` |

A crash that stops the CPU cannot be recovered by anything (no MMU); the cases above are the ones where
code still runs. After the monitor the browser draws as it did: measured in ue2emu, `/quit` shows the
file browser identical to before (25 rows, frame, status line), `x` closes the overlay and the next open
shows the same cleared screen the 40-column app left (unchanged by this work).

**Screen (variant B, `ui.c`, 80 x 40):**

```
row 0   PAUSED IB $c012 cy 123456789 r120/031            status
row 1   PC C012  A 00  X 00  Y 00  SP F6  NV-BDIZC nv-bdIzc
row 2   $00=2f $01=37  bank cpu  device c64  ring on 4095 fr  trace on 1024/349461
row 3   ---------------------------------------------   separator (text: "history -33 lines" when scrolled back)
rows 4..36  the output, 33 rows, newest at the bottom
row 37  ---------------------------------------------   separator
row 38  .command line                                    cursor = reversed cell
row 39  F1/F7 scroll  F3 help  RUN/STOP pause  x go+leave  /quit end  RET repeats z/n
```

Running: the window shrinks to the bottom 9 rows (three header rows, separator, 2 output rows,
separator, command, hint) and the rows above are colour index 0 (transparent), as in the 40-column
screen. Row 2 shows no `$00/$01`, ring or trace while the machine runs (nothing is read then). The hint
row names the function keys the interface made PageUp, PageDown and Help (`ui->fkey`; F3/F5 and F7 in the
Commodore interface). The last cell of the last row is never written (the overlay scrolls).

- **`m`** on the screen: 16 bytes a row + PETSCII (`>C:c000  xx*16  chars`, 74 columns). PETSCII column:
  `$20..$5B`, `$5D` as themselves, `$C1..$DA` as lower case, `$A0` blank, the rest `.`. **`m <addr>`**
  prints one page (the output rows less the echoed line: 32 rows = 512 bytes at 80 x 40, 18 rows of 8 at
  40 x 23) from the row `addr` is in; **`m`** alone goes on from the cursor (VICE). An explicit end is kept.
  TRX64's default (`$800`, 32 a row, ASCII) is unchanged for RPC/REST.
- **`d`**: `.C:c005  4c 00 c0  JMP $c000` with `>` instead of `.` at the PC, no `<-- PC` suffix; a page
  of 32 lines (`d` alone continues from the cursor). RPC keeps `$c005  ...` and ` <-- PC`.
- **`chis`** is one line per instruction and fits 80 columns (the longest is about 70). The static
  `help` text was reflowed so that no line is longer than 80; verbs that print a sentence (error and usage
  texts, TRX64's) still wrap softly at the last space of the second half.
- Output wraps at 80 (`ui_print`): 79 and 80 characters stay one line, 81 wrap.

**Scrollback bug (the cause, read in the code and measured on the previous trxmon in ue2emu):**
`ui_map_key` took the code from `ui->fkey` to the number of the function key and decided by the number
(F1 up, F7 down, F3 help). The firmware names keys by what they do (`KEY_PAGEUP`, `KEY_PAGEDOWN`,
`KEY_HELP`) and its interface decides which F key carries them: in the **Commodore interface** (the
C64U's default) F3 is PageUp and F5 PageDown, so F3, the key that interface labels "page up", ran `help`,
F5 did nothing, and so did the USB PageUp key (same code as F3). Measured: on the previous app F3 prints
the help text again and F5 does nothing; F1 scrolled, because F1 is Tasks there. Second defect: PageUp
counted to the history length (200) while the view stops at length minus the rows on show (181), so the
first PageDown after reaching the top moved nothing. Fix: the map takes the codes by name (any interface,
and the USB keys), `ui_key` clamps to `hn - rows`. Measured on the new app (emulator, history at 200 lines): PageUp x8
reaches `history -167 lines` (the oldest at the top), one PageDown goes to -134.

**Keys:** the session reads through `getch_monitor` (`api_ui_getch_monitor`: `keymapper(c, e_keymap_monitor)`:
the only difference to `getch` is that "Navigation Style: WASD Cursors" does not turn `w a s d` into cursor
keys). A firmware without it falls back to `getch` (and has the old limit).

**Also fixed here:** a stop that opens the screen was printed twice (`ui_show_stop` then `ui_tick`;
seen in ue2emu after `bk` + `x`); `rpc_init` forgets a stop left over from an earlier start.

**Tests:** `make -C apps/trxmon/host test`: `trxmon_test` 672 checks (80-column golden rows, device row,
running layout, no table verb wider than 80, wrapping at 79/80/81, `m` paging and continuing, narrow
paging, `d` pages and `>`, `chis` lines, scrollback to the oldest and back, the 181 case on the narrow
window, key names for both interfaces, `w a s d`), `trxmon_ui_test` 64 (every exit leaves no wide screen
open: `x` with and without stops, `/quit`, reopen x2, headless, gone screen, stop requested; fallback
without or with a refused `screen_open_wide`; `getch_monitor` and the fallback; WASD; PageUp/PageDown
and the Commodore hint). Built in `u64-build-env` (firmware `make -C target/u64ii/riscv/ultimate`, then
`trxmon.u2a`); run in ue2emu (`--board c64u`), which models the 80-column overlay (`r_cpl`, `r_nl`).
- **Not tested on the board:** all of it. Open: the overlay box is centred and scaled by the core
  (`sx`/`sy`); at 480p/720p the 80-column box is 640 px (cw 8) and doubled to 1280 by `sx`, which has not
  been looked at on HDMI.

### 8.11 80 columns in Freeze mode (app branch `t21-80freeze-app`, 2026-10-06)

Decision (Alex, 2026-10-06): "das Freeze Overlay IST MEIN UI": with Interface Type = Freeze the monitor
comes up 80 x 40 as well. Until §8.10 `screen_open_wide` refused whenever the UI's screen is not the
overlay's (`api_ui_screen_open_wide`), so in Freeze trxmon fell back to the 40 column `screen_open_full`.

**Why the overlay can be used in Freeze, and what covers it (read in `top.v`, `pix_layer.v`, `c64_text.v`):**
the layer order is scaler -> `overlay.v` -> `pix_layer.v` -> `c64_text.v`. The freeze menu on the slim core
is the C64 text screen drawn by `c64_text` (TX_CTRL b0) over the dimmed still of `pix_layer` (PX_CTRL b0,
opaque: PX_KEY off, alpha 255). Both are above the overlay, so the overlay is invisible while they are on.
`overlay.v` draws opaque cells (a colour index is transparent only where the app made one so) and its
register D (`TRANSPARENCY`) is `b7 on, b6 own keyboard, b3:0 transparency`; **own** zeroes the physical keys
that reach the C64's matrix (`app_input.v`), which the freeze menu's keyboard scan reads, so Freeze uses
`0x80` (on, not own) and never `0xC0`. **No RTL change is needed.** *(Corrected 2026-10-07, §8.12 item 1: on the slim core
the freeze menu's scan reads the physical matrix, which `own` does not mask, and `0x80` let the typed keys reach the
running C64. Freeze uses `0xC0` now.)*

**While trxmon's wide window is open in Freeze:**

| Layer | Wide | Back |
|---|---|---|
| overlay | `begin_wide(80, rows)` (as §8.10), TRANSPARENCY `0x80` | `end_wide()` (40 x 25 cells restored), TRANSPARENCY `0x00` |
| `c64_text` | TX_CTRL show off | TX_CTRL as it was (b0) |
| `pix_layer` | PX_CTRL off (the registers stay) | PX_CTRL on |

The picture below the overlay is the scaler's, black while the machine is halted (`.blank(c64_halted)`); the
dimmed still is not shown during the monitor (it is opaque and would hide the overlay; the monitor's own
background is the overlay's). The C64's freeze menu RAM stays as it is, so the menu is back at once.

**Firmware (`trx_video.cc`, `app_api_impl.cc`, `overlay.h`):** `trx_video_wide_begin()` takes the layers away
and answers false unless a freeze with the hardware text layer is up: `usable && active && hw_text &&
layer_on && menu_shown && !tw_on`, so on Gideon's bitstream (no TRX block, `usable` false) and on a
software-text layer (that one redraws and re-enables the pixel layer by itself) the refusal stays, and
`screen_open_wide` returns NULL as before (trxmon uses the 40 column screen). `trx_video_wide_end()` puts
TX_CTRL and PX_CTRL back, but only while that freeze is still up with its menu: `teardown()` and
`freeze_enter()` end the mode without writing (they set the layers themselves); `trx_video_menu_off()` (the
freeze ends under the monitor) ends it and switches the pixel layer back on, the text layer is off there anyway,
so a later freeze that keeps the layer (`active` stays across a monitor step) finds it as it expects. `Overlay::show_in_freeze()` / `hide_in_freeze()` write only TRANSPARENCY (no `enabled` change, no
C64 PLD write as `release_ownership` does). The window is created on `overlay->getScreen()`, not on the
UI's screen; keys still come from the UI's keyboard (the freeze menu's: C64 matrix and USB).

**Every way out** is the one of §8.10 table: all of them pass `api_ui_screen_close`,
`app_api_overlay_restore` or `Overlay::release_ownership`; the first two now call one function that also
undoes the freeze hardware (idempotent). `release_ownership` ends the overlay's wide mode only; a freeze
that ends under the monitor is restored by the freeze's own teardown (TX_CTRL 0, PX_CTRL off).

**Overlay on HDMI:** unchanged (the freeze branch is entered only where the UI's screen is not the overlay's).

**Tests:** `trxmon_ui_test` models a Freeze firmware (the three layers as flags): the wide screen takes the
layers away and every exit of §8.10 gives them back; a firmware that refuses falls back to 40 columns with the
layers untouched. Not testable off the board: the layer registers themselves.
- **Not tested on the board:** all of it. Risks: the overlay at 1080p over a black picture is first seen
  in Freeze here; PX_CTRL is switched off and on mid-frame (no wait for the frame start).

### 8.12 As built: fixes from the board session 2026-10-07 (app branch `t21-monfix-app`, CI run 504)

Seven findings of Alex's session (T21-RUN, T21-P4, T20-REU rows of `docs/archive/board-tests/board-tests-pending.md`). None is
board-tested yet.

1. **Keys typed at the monitor reached the C64.** In Interface Type Freeze the wide screen used TRANSPARENCY
   `0x80` (§8.11: "on, not own"), so the KERNAL read the typed keys whenever the machine ran (after `g`: `chis`
   became a SYNTAX ERROR; after `x` a held RETURN moved the BASIC cursor). The reason given in §8.11 (own zeroes
   the keys the freeze menu's scan reads) holds for the old CIA scan only: on the slim core the menu reads the
   physical matrix through `KEYB_COL`/`KEYB_ROW`, which `own` does not mask (`app_input.v`: `keys = (own ? 0 :
   phys) | app`; `top.v` `keys_cpu_s2`). `Overlay::show_in_freeze()` now writes `0xC0` (the Overlay interface
   always did). The USB keyboard was already off the matrix for every menu run (`ultimate.cc`
   `enableMatrix(false)`). Before the own bit drops (`release_ownership`, `hide_in_freeze`) the matrix is waited
   idle, at most 1 s (`Overlay::wait_keys_up`): a key still held when the C64 gets the keyboard back is a new
   key press for the KERNAL. Keys typed in the monitor never entered the KERNAL buffer, so nothing is left in
   it. Not host-testable (hardware registers); §8.11's "never `0xC0`" is void.
2. **Stale stop request.** `s_stop_req` (`app_api_net.cc`) was set by `app_resident_stop()` and cleared only by
   the next `detach()`: the app started from the menu after a background trxmon ended saw
   `stop_requested() = 1` and quit at once, and so did every later menu start until a REST serve detached.
   Now it is cleared when the resident task ends and by `app_set_current_id()` at every start with no resident
   task. `run_app` no longer returns after ending an app of the same id (popup "ended"): it ends the resident app
   (3 s) and starts the one asked for, with its screen (§7.6 as written).
3. **`whowrote` of an I/O address.** Stream 0 is the RAM write log; an I/O store is not in it, so `whowrote
   d020` found nothing and printed `ring_exhausted`. For $D000-$DFFF `trx_ring_trace_whowrote` now scans the
   trace records: the effective address of every store and read-modify-write with an absolute or zero page
   mode from opcode, operand and A X Y before the instruction (`software/io/c64/trx_ea.h`: STA STX STY, INC DEC
   ASL LSR ROL ROR, SAX, the SLO RLA SRE RRA DCP ISC family, SHA SHX SHY TAS; the opcode map is checked
   for all 256 opcodes, `trx_ea_test`). What the record cannot give: the **old value** (hit flag
   `U2P_TRACE_HIT_OLD_UNKNOWN`, printed `$??`), the **new value of a read-modify-write** (`NEW_UNKNOWN`), and
   the target of `(zp,X)`/`(zp),Y` stores and of an SH* store over a page crossing (pointer bytes / high byte
   are not in the record): those are **counted, not guessed**; `*exhausted` bit 1 (value 2) tells trxmon, which
   prints "stores through a zero page pointer ... were not checked" (JSON `pointerStoresUnchecked`). A write
   with RAM banked in at $Dxxx is found too (same instruction). `u2p_trace_hit_t.reserved` is now `flags`
   (same layout).
4. **JAM was not a stop.** Reading the CPU core (Q7): after a JAM no instruction boundary and no stop record
   ever comes, so the monitor stood on RUNNING. `mach_poll` now watches `INSTRS`: no instruction for 100,000
   cycles (about 5 frames) while the gate is open, the CPU not held (STATUS b4) and not aligning (b6) -> the
   machine is held at the next cycle (PAUSE_CB; no boundary comes) and, when the opcode at IPC is one of
   $02 $12 $22 $32 $42 $52 $62 $72 $92 $B2 $D2 $F2 and the stop is not on a boundary, the stop's reason is
   `jam` at that PC (an opcode that is not a JAM: the machine goes on, looked at again later). RUN/STOP and a
   step on a stuck CPU say `jam` too (they waited for a boundary in vain). `stop` objects carry `reason:"jam"`;
   the screen says `JAM at .C:c008 - the CPU is stuck, no interrupt wakes a JAM (opcode $02); a reset frees it`.
   Host tests: `test_jam` (detection, RUN/STOP, no false hit while running or while held), `trxmon_ui_test` 1b.
5. **The screen opened by itself.** The only caller of `ui_request` is the resident serve loop, and only for
   a user stop (`breakpoint`, `observer`); a CPU hang produced none. What was seen is therefore the armed
   observer (`obs ... store d020 do break`, set earlier in the same session): the loop iteration before the
   poked $02 still stored $D020 and the observer fired (§8.5, intended: the person armed it). Not reproduced on
   the board. A JAM now is a user stop itself (`rpc.c` `stop_came`): a resident trxmon asks for the screen and
   the stop text says why (`JAM at ...`). Decision: intended, because the person armed trxmon to watch.
6. **Freeze menu in the upper-case/graphics set after the monitor.** **Cause not proven.** On the slim core
   the text layer draws the menu from `TX_VIEW` (`$D018` bits 7:1: screen $0400, charset $0800 in RAM, case
   select; `0x0243`) and from the charset area at $0800 of the C64's RAM that `freeze_slim()` filled; nothing
   in the firmware writes `TX_VIEW` but `menu_on()`/`menu_background()`, and the VIC's own `$D018` (what
   `rstep`'s replay moves) is not read by the layer, so the suspect named in the board note is not the path.
   What can differ after a monitor run: the charset area (a land or a run of the machine rewrites RAM
   $0000-$0FFF, the menu's screen and charset live there) or `TX_VIEW`. `AppLoader::run_app` now calls
   `C64::menu_back()` when the app returned and the machine stands frozen on the slim core: `TX_VIEW` is
   written as `menu_on()` wrote it and the charset area is compared with `char_set` and rewritten; each
   difference is logged (`[TRX] menu view was ....`, `[TRX] menu charset RAM differed at +....`), so the
   next board run names the culprit. Open until the board says.
7. **A08 popup.** The popup has its own text `Timewarp is off while REU is on` (`TRX_REU_OFF_POPUP`, at most
   36 characters, checked at compile time and in `trx_ring_reu_test`); REST, RPC, trxmon and the Timewarp bar keep
   the long `TRX_REU_OFF_TEXT`.

**Host tests:** `make -C apps/trxmon/host test`: `trx_ea_test` 279, `trxmon_test` 819, `trxmon_ui_test` 74,
`trxmon_net_test` 23, all pass; `make -C software/io/c64/tests reu` ok.

### 8.13 As built: T55 monitor extras (app branch `t55-mon-app`, 2026-10-08; fixes and the live log in `t55-fix-app`, 2026-10-09)

App work on the existing gateware (no RTL change). Two firmware ABI additions carry what the app could not reach
itself (below). Host tests: `apps/trxmon/host` (`make test`, 1009 checks in `trxmon_test`).

**Observers** (`mach.c`, `cond.c`, `mon.c`; syntax `obs <name> when exec|load|store <a[..b]> | raster <line> [if <cond>] do <action>`).

- **Condition** (`if`): TRX64's grammar (`a x y pc sp fl rl val addr cy`, numbers, `== != < > <= >= && || ( )`), parsed
  into at most 24 nodes (`cond.c`, the parser is `observers.rs` `parse_cond`). **Numbers are hex, with or without `$` (Alex
  2026-10-08: "entweder IMMER mit $ oder NIE"): `val == f5`, `addr >= c000`, `x != 7`; `%1010` stays binary, there is no decimal. A
  word that is a register name reads as the register, so the number a is written `$a`. `ignore n` and the counts `obs` prints
  (`hits=`, `ignore=`, `pass=`) are hex as well. `obs` lists the parsed condition, not the typed text, with every number in hex
  at 2 / 4 / 8 digits (`val==f5`, `a==05`, `y==0a`, `addr>=d010`), so one value reads the same in every observer. A load/store observer holds as much of it as
  the watch unit can, split over the top-level `&&` conjuncts: `val == v` into DATA (mask $FF), `addr` comparisons into the
  range (narrowed, never widened), one `a|x|y|sp` against a number into the REG qualifier (`== != <` and `>=`; `>`/`<=` become
  `>=`/`<` with v+1: the first one that fits; a second register conjunct stays software). The other conjuncts are re-checked **after the stop**, from the settled registers, the stop record
  (`val` = bus data, `addr` = bus address; exec: `addr` = PC, `val` = 0, as TRX64), the VIC raster line and the cycle count;
  a false condition runs the machine on at once (`RUN`, it waits for frame alignment, up to 20 ms per false hit). `fl` stays
  software (the P of the hardware vs TRX64's status bits 4/5 is not compared). **Exec observers use the exec bitmap, which
  holds the address only, so their whole condition is software**: the unit's register qualifier sees the registers of the
  cycle that ends at the IB edge, before the previous instruction's write-back (§5.1), so using it for IB hits would reject
  true hits. `obs` lists what ran where: `hw: watch unit 0 (addr+val+reg), soft: x!=7`.
- **Actions**: `break` (the stop stays), `log [a x y sp pc fl $addr[:w] ...]`, `cmd "<monitor commands separated by ;>"`.
  `mark` and `trace` are refused (they need T20 / T66). After `log` and `cmd` the machine runs on; a stop inside `until`/`next`/`ret`
  that an observer lets go keeps the run-to armed.
- **`do log` in hardware**: the unit's action LOG (CTRL b6:5 = 2) writes a stream-1 record of space 8 (§5.1, `trx_dbg.v`
  header) and does not stop. It exists only while the frame ring runs (no Timewarp with the REU on, A08) and the record carries
  address, data, the instruction's PC and the low 15 cycle bits, **no registers**: so a log with no fields or only `pc`, with a
  condition the unit holds entirely, runs in the unit (a load/store observer, or an exec observer without condition: type IB
  over the range, the bitmap stays off). Any other log (fields `a x y sp fl`, `$addr`, a software remainder, no ring) stops
  per hit, formats the line from the registers and runs on. A hardware log without fields prints `pc=$xxxx`, TRX64's default
  line has `pc a`; ask for `a` and the observer stops per hit.
- **The log** (`obs log`, the newest 40 lines): software lines at the stop, hardware lines read from the ring, and the output
  of `do cmd` (the command echoed as `obs NAME: .cmd`, its text indented). Format `obs NAME: store $d020=$05  pc=$c005 cyc=N`.
  Hardware records come through the new ABI call `ring->log_read` (below), polled with every `mach_poll`; the cycle is rebuilt
  from the 15 low bits against the cycle counter at the read (right while a record is read within 32,768 cycles, about 33 ms:
  the serve loop polls every few ms; `obs log` drains first).
- **The live log (T55 fix, 2026-10-09; Alex: "live wie TRX64, aber mit Drossel").** Every line that goes into the log is also
  shown while the machine runs: printed on the monitor screen (`ui_tick`, a reader position `ui_t.olog_cur` in the line
  numbers `mach_olog_seq` / `mach_olog_next`) and sent to the RPC client as the notification **`debug/observer_log`**
  `{"session_id", "line"}` (`rpc_poll`; the notification is in the `ping` list). While a log or cmd observer is on, the running
  screen keeps `UI_LIVE_EXTRA_ROWS` (7) more output rows than the usual status line and two output lines.
  **The throttle** (`live_take`, `mach.c`): a bucket of `LOG_BURST` (20) lines refilled with `LOG_RATE` (60) per second, so one
  IRQ per frame (`exec $ea31`) shows whole; a hit beyond the budget is not formatted, not printed and not logged, only counted;
  the count is told as one line `+K skipped` (at most once a second while the flood goes on, and 250 ms after it ended). The
  same budget gates a `do cmd` hit (no budget: the command does not run). The screen, `obs log` and the notification show the
  same lines.
  **What costs what.** *Hardware log* (REU off, ring on): the drain (`mach_olog_drain`) reads at most `DRAIN_CALLS` (4) calls of
  at most the tokens left per poll and then moves the ring reader to the present (`log_read(NULL, 0)`); what it dropped is the
  unit's hit counter (16 bits, read every poll) less the lines shown, plus the records the area overwrote. The loop that read
  until the ring was empty (`while (n == 16)`) never ended at 160,000 stores a second and took the whole app down (bug 4,
  measured on the board 2026-10-09: the serve loop stuck in `obs log`, REST alive). *Software log* (stop per hit): on a watch unit
  whose condition the unit holds entirely, trxmon tells the unit to pass hits (PASS) after each stop so that about one stop per
  frame of **machine** time remains (`thin_adapt`: hits since the last stop divided by the machine cycles between the stops, per
  `THIN_CYC` = 19,656); the passed hits are added to `hits` and to the skipped count; a thinned unit whose hits stopped coming for
  150 ms is armed plain again. An exec observer on the bitmap and one with a software remainder cannot be thinned: each hit is a
  stop (the machine is slowed down, the app stays responsive). The hits of a hardware log are summed to 32 bits in `o->hits`.
  A holder that stopped reading is dropped after the first send that took a second (`main.c send_all`, the firmware's send waits
  up to 25 s for a full buffer) so that notifications cannot stall the app more than once.
  **The processor, measured on the board (2026-10-09).** The RISC-V runs the app's code slowly (a full redraw of the 80 x 40
  screen takes 30 to 45 ms, a poll that formats and sends log lines tens of ms; sampled PCs with `trxdbg rvpc`: 72 % of the time in
  `ui_render`/`put_row`/`m_streq`) and the app task runs above the firmware's HTTP and network tasks. With the screen up and the
  flood on, the monitor loop never waited: REST answered after 1 to 6 s. Once REST and the screen were dead and the app sat in
  `wait_status` (PC sampling): not reproduced in the full runs after the duty-cycle fix, its cause is not proven (a false JAM verdict
  of `jam_watch` on a CPU the flood stalls would give exactly that loop: `mach_pause` spins 2 x 50 ms per verdict). Three measures, all in `main.c`: (1) **duty cycle** `duty_sleep`: each pass of
  the screen loop and of the serve loop sleeps as long as it worked (not counting the wait on the sockets), at most 60 ms, so
  the app takes at most about half of the processor whatever the machine does; (2) **packets**: a frame leaves in one send
  (header and payload together), and the log notifications of 100 ms (or 16 lines) leave in one more (`srv_cork`); (3) **redraw**:
  output (the live log) is drawn at most every 150 ms, a key at once. Result on the board: REST 0.16 to 0.22 s during the flood with
  the screen up, notifications at the rate limit. A software log with the REU on shows about 12 lines a second (a stop per hit
  slows the machine; thinning keeps it at one stop per frame of machine time); the hardware log (REU off) shows the full 60.
- **`do cmd`** runs the commands through `mon_exec` at the stop, with the verbs that move the machine or edit the observers
  (`g x z n ret until cs pause obs ignore rb a asm goto frame rstep device` ...) refused in the output.
- **`ignore <name> [n]`** (default 1): PASS in the unit when the observer sits on a unit and its condition has no software
  remainder (HITS is cleared by arming, `hw_unit`), else a counter checked after each stop (always for the exec bitmap).
  `obs` shows `ignore=` / `pass=` and the hit count (for a hardware log: the unit's HITS less PASS).
- **`rb <line> [x]` / `rb -` / `rb`**: the observer `rb` on a unit with the raster source, type every cycle, range = the single
  position `{line, cycle}`; `x` selects the cycle `x/8` (cycle 0 without `x`). One `rb` at a time; `obs <name> when raster <line>`
  makes more (a range is refused: it would stop at every cycle of the range). Numbers are hex like everywhere. The line is
  checked against the video mode.

**Views**: `reu` reads $DF00-$DF0A through the lens unit's look (no bus cycle, `$DF00` is not cleared) and the size from the
firmware setting (`config->get`); layout as TRX64's `reu_report` less "armed for $FF00 / transfer pending" (not visible).
`pot` shows the CIA 1 port A select lines (state bus chip 3 w4) and `$D419/$D41A` only with `sidefx on` (the SID is not on the
state bus; REU and SID taps of ARCH §A2 are not built). `io 1541` shows what `drv_dbg` has: the last opcode fetch, drive
cycles, halted / held / stepping, comparator; no VIA values (out of budget, ARCH 0004).

**Files**: `ls [dir]` (TRX64's `d`/`-` listing, case-insensitive order, at most 160 entries) and `rmdir` through two
appended entries of the fs table, `fs->dir` (one callback per entry) and `fs->rmdir` (empty directories only), size-gated
(`U2P_HAS(fs, dir)`); `dir` is an alias of `ls`.

**Flow** (`mach_step_flow`): `z` (up to 256 steps one at a time), `n` over a non-JSR and `sf`/`nf` classify every single step as
TRX64's FlowTracker: BRK enters `brk`; an interrupt is recognised when the stack went down by three and the new IB is the
target of the NMI ($FFFA) or IRQ ($FFFE) vector; RTI pops. `flow`, `focus [auto|main|irq|nmi|brk|clear]`, `sf`/`nf` (step into/over
until the CPU is in the focused flow; bounded: 20,000 steps or 3 s, then `CAP`; `nf` runs a JSR out with the `next` units),
`r`'s last column and the landing line's `[irq]` tag use it. A free run (`g`) clears the frames, a run-to keeps them;
`ret` and `until` do not pop on an RTI they cannot see.

**`g <addr>` from the middle of an instruction** (`verb_g`): the stop was a cycle step or a unit hit (`at_ib` = 0); `g` first
finishes the instruction (`mach_step_flow`, as `z`) and then redirects; `r pc=` still refuses ("step to the boundary first"). On a
JAM no step or redirect gets out (the 6502N stays in decode): `g <addr>` says "the CPU is stuck on a JAM opcode ... only a reset
gets it out".

**Screen**: the jam state on attach (`mach_own` asks `stop_is_jam` after the hold's record: status row `JAM $c008 ...`), the
assembler prompt prefilled `a c101 ` (RETURN on the untouched prefill ends the mode; `a c101 nop` typed in assemble mode
assembles there), cursor LEFT/RIGHT/HOME/END with insert at the cursor, **completion on CTRL alone, CTRL+I and TAB** (CTRL+I is $09 = TAB;
CTRL alone is the key `KEY_CTRL_TAP` (0x1F8) of the new ABI call `ui->key_ctrl_tap`, below): verbs, then observer names, lenses,
marks, paths of the file verbs (listed through `fs->dir`; several candidates complete to their common start and are listed).

**Keepalive** (`main.c`): a WebSocket ping after 20 s without a byte from the holder, the holder dropped after 50 s of
silence (ADR 0031's open point). The RFC makes a client answer with a pong; the host net test uses 1.5 s / 3 s. A refused second
client is accepted at the HTTP level (101), told in one JSON-RPC error (-32001) and closed with 1013; it holds the place only when
its first request is answered. The text of that refusal (`refuse_text`) lacked its NUL (found with ASan in the net test, stack
overflow into the answer); fixed 2026-10-09.

**CTRL alone (T55 fix, Alex: "CTRL alone as in the trx64-cli TUI").** The C64 keyboard scan (`Keyboard_C64::scan`) gave no key for
a modifier alone (`keymap_*[0x40]` is 0). New ABI call `ui->key_ctrl_tap(on)`, appended, ask `U2P_HAS(ui, key_ctrl_tap)`: with it on,
the scan remembers "CTRL down, no shift, no C=, no other key" and, when the keys are all up, queues `KEY_CTRL_TAP` (a tap: the
release, so CTRL+key stays the control code and never completes by accident). Off for everyone else (`Keyboard::set_report_ctrl`
is a no-op in the base class, the flag defaults to off), so Gideon's UI never sees the code; the loader turns it off when the app
returns or its screen callback ends (`app_api_overlay_restore`); trxmon turns it on while its screen is open. A USB keyboard has
no such key (TAB). Not testable without the real keyboard (the REST input API builds the key codes without the matrix scan): a
human check.

**Firmware ABI additions (appended, size-gated, `app_api.h`)**: `ui->key_ctrl_tap`, `fs->dir`, `fs->rmdir` (`app_api_impl.cc`) and `ring->log_read`
(`trx_ring.cc` `trx_ring_log_read`: a reader over engine stream 1 that keeps its own position, filters space 8, drops what the
area overwrote and says how many; `max = 0` moves it to the present; a ring restart sets it back to 0). Older firmware: the
app falls back (no listing, soft logs).

**$D011 = $00 after a REST reset (analysis, cause not proven, nothing changed):** the code paths that write $D011 around a
reset are in `c64.cc`: `start_cartridge` writes `VIC_REG(17) = 0` ("screen off") before it resets (used by `MENU_C64_RESET`
only with Gideon's monitor in a debug session, which is out of our build, and by clear-memory / run-cartridge), `resume()` in the
non-slim path writes back the `vic_d011` that `stop()` read, and `restore_io()` writes `vic_backup`, which `freeze_slim` leaves all
zero when an app owns the state-bus window (it is not used by `restore_slim`). A `stop()` that read $D011 while the VIC was still in
its reset state (0) and a `resume()` after the KERNAL had set $1B would produce exactly this; so would a CPU that never ran the
KERNAL's init. Which one happened is not visible from the code. To prove it on the board: after the failing reset `m d011 d012`,
`status`, `r` (is the CPU running, where is the PC), and a second REST call chain without opening the menu first.

**Board test as a script (T55, 2026-10-09): `tests/t55_board.py`.** Everything a person used to type on the board runs over the
REST API, FTP and the RPC: `--package VER` loads the bitstream and the package's firmware, `--app` uploads the trxmon build to
`/USB1/trxmon_t55.u2a` and starts it headless (`PUT /v1/apps:run_file?app=...&action=serve`, nothing registered, nothing written to
the flash). The monitor **screen** is driven too: a breakpoint makes the detached trxmon ask the firmware for it
(`trx->ui_request`), `POST /v1/machine:input` types into it (the overlay takes the keys while it is up; CTRL+I is `ctrl` + `i`),
and the effect is read from the machine (`m`, `bk`, `obs` over RPC): prefill, cursor keys, completion and a flood with the screen
up are tested that way. What the screen *shows* cannot be read back (`machine:menu_screen` is the 40 x 25 menu, not the wide
window). The REST input builds the key codes from the matrix table without the keyboard scan, so the physical scan (and CTRL
alone) stays a human check. Prints PASS/FAIL per check and per item; exit status 0 when all passed.

## 9. Test plan

### 9.1 Simulation first (`designs/c64_basic/sim/dbg`, modelled on `sim/pause`)

`tb_dbg` boots the core (as `tb_pause boot`) and runs a 64tass test program. Assertions:

1. **Registers.** After `PAUSE_IB` at a known loop:
   - A, X, Y, S, P and IPC equal the values the program put there (it stores them to RAM
     too, and the test compares them through the RAM port);
   - STATE = decode, and IR = the opcode at IPC.
2. **Step.** STEP_INSTR 1 over each addressing mode, compared against a golden trace from
   TRX64 (`chis` on the same PRG). The cycle delta per instruction equals the opcode table,
   including page-cross and branch-taken cycles.
3. **Exec bitmap.** A bit on a loop body stops at IB with IPC = that address, cycle-exact
   against the TRX64 cycle count of the same PRG. A bit set at an address whose fetch is
   replaced by an IRQ does not stop until the real fetch (the `intg_i` rule).
4. **Comparators.**
   - A write watch on $D020 stops with ADDR/DATA right.
   - A read watch on $DC0D stops before the ICR clears: the CPU had read it, so the
     register's flags are gone in the CIA, and the app sees DATA = the pre-clear value.
   - A data mask, a register qualifier (S ≥ S0 for `n`) and PASS/HITS behave as specified,
     with the two units of ARCH §7.2.
5. **`n` over a JSR with an IRQ inside:** the machine lands at PC+3 with the S check, the IRQ
   runs through, and the user breakpoint inside the subroutine stops (TRX64 main.rs:3370
   "no `iters > 0` guard" case).
6. **`ret` from an IRQ handler** stops after the RTI. Flow frames rebuilt from trace
   records show IRQ, NMI (RESTORE) and BRK entries.
7. **Raster:** stops at line L, x X; RHITS counts frames.
8. **CPU writes (as built, §5.1.1):** register writes and the redirect cost no cycle; the
   instruction at the new PC runs in its own cycles; an empty write changes nothing over 200
   instructions; the previous instruction's write-back is kept for registers not written.
9. **Invisibility.** Pause, step 1000 cycles in single steps, run. RAM, raster and the clock
   count must equal a run without the debugger plus 1000 cycles, and the VIC realigns (the
   `sim/pause` method). CIA 1 timer A and TOD move only by the stepped cycles.
10. **DMA footprint:** `io` with `sidefx off` reads only the allowed registers, DMA_CYCLES
    counts them, and a held CPU is released by the next step.
11. **Drive:**
    - DRV_STEP_INSTR over the drive ROM's idle loop;
    - a drive comparator on $1C00 writes stops the drive and then the C64 (FLAGS b2);
    - cross-stop latency ≤ one C64 cycle.
12. **Trace (P4):** a 10,000-cycle bus trace equals the simulation's own bus log byte for
    byte; with the FIFO forced full, TDROP counts and no record is torn.
13. **Regression:** `sim/pause`, `sim/dma_io` and `tb_scaler` still pass.

### 9.2 App, off the board

- **Monitor core:** built for the host as well (`apps/trxmon/host/`), against a fake `trx`
  backed by a C 6502 model or by TRX64 over its WS `monitor/exec`.
- **Golden transcripts** (TRX64 864 §11.1): every verb in `help`, against the same PRG at
  the same stop, compared byte for byte with `trx64cli`'s output. The differences allowed are
  the listed "not available" answers.
- **`docs_agree`:** the help text, this spec's §4 and TRX64's `MONITOR.md` agree on the
  shared verbs (a script; TRX64 `docs_agree.rs` is the model).
- **ue2emu:** load the `.u2a`, check that `api->trx` is NULL and the app refuses cleanly (no
  debug block in the emulator). Also the REST launch and the reply size.

### 9.3 Board (after the simulation passes; by Alex, not by an agent)

- `trxdbg.py dbg regs|step|bk` over JTAG on a running BASIC loop, then on a game with raster
  IRQs.
- In the app:
  - `bk` in a raster IRQ, then `z` × 100 and `g` (the picture must stay locked after the
    resume);
  - `n` over a KERNAL call;
  - `ret` out of an IRQ;
  - a watch on $D020;
  - `rb 100`;
  - `device drive8` + `bk` in a fastloader's drive code.
- **Lorenz suite** (`tests/run_lorenz.sh`) with an armed but non-hitting debugger: the same
  results as without (the comparators must not disturb timing).
- **REST:** `PUT /v1/apps/trxmon:run?args=bk c000;g`, then `status`.
- **Lenses (T21 §3.4.1):** an emulated EasyFlash (`/USB1/jupiter_fracture_(PAL).crt` via `runners:run_crt`)
  and one more type (EasyFlashXl or C64MegaCart): `machine:readmem?address=8000&length=100&lens=cpu` and
  `lens=cart` (and $A000, $E000) equal the CRT's bytes of the current bank, running and with the freeze menu up;
  without `lens` the answer is as before. A real cartridge on the port (needs Alex, T16 §8.4): the same reads,
  `lens=cart` at $8000/$A000/$E000 while a game runs and with the machine frozen.

---

## 10. Phases

Hardware phases are ARCH §12 (A0-A8) **(see ARCH)**; the app phases stay:

| Phase | Content | ARCH phase | Exit criterion |
|---|---|---|---|
| P0 | This spec and ARCH; decisions | — | Alex agrees |
| P1 | CPU state bus, IB/cycle stops, steps, counters, exec bitmap, JTAG page, `trxdbg.py dbg` | A0, A1, A4 | sim tests 1-3, 8, 9, 13 |
| P2 | `trx` ABI table and loader release; `trxmon.u2a`: port of `verbs.rs` (memory, CPU, bk, run control, step/next/ret/until, `io` from the state bus, iec, drive, cart, bank, screen, files, dump, help, status), assembler, disassembler; REST session; the two watch units for `n`/`ret`/`until`/watches | A1, A2, A4 | sim 4-6, 10; golden transcripts; board run 9.3 (part) |
| P3 | Observers (hardware part on the watch units, rest soft), raster source, focus/sf/nf/bt frames in software, drive comparator and step, `asm`, `traprules`, `ls`/`rmdir` ABI; `reu`, `pot`, `io 1541` | A2, A4 | sim 7, 11; board drive tests |
| P4 | Trace (engine stream 3), `trace`, `chis`, `sd` from the ring, `whowrote`, `triage`, (`tracering` removed from M1, A13/T66), hardware LOG/TRACE actions, `bitmap`, `savecrt`; picture while paused | A3, A4, A6 | sim 12; (`.c64retrace` opens in C64RE: not in M1) |
| P5 (T20) | `rstep`, `diff`, marks, rewind transport, `ringdump`/`ringload`, `undump`, `mount .c64re`, `do mark` | A3, A8 | per T20 spec. **Built 2026-10-03** (§4.10.1) except `diff`, `do mark`, `mount .c64re`, play |

The base firmware's built-in monitor is removed (§11) in P2, before `trxmon` ships, so that only one monitor
is in the firmware line.

---

## 11. Removing the base firmware's built-in monitor from our firmware line

In the application fork (`1541ultimate`). Every hunk is checked before reverting, since later commits build on
some of them.

- **Whole files:** `software/monitor/*`, `software/test/monitor/*`,
  `target/pc/linux/machinemonitortest/`, `doc/machine_code_monitor.md`,
  `tests/e2e/monitor/*`, `tools/developer/machine-code-monitor/*`.
- **Hooks:**
  - `software/monitor/monitor_init.cc` (the menu entry under Developer)
  - `userinterface/userinterface.h:51,146` and `userinterface.cc:~618,1005` (`e_keymap_monitor`, `run_machine_monitor`)
  - `userinterface/tree_browser.cc:504-519` (CTRL+O)
  - `u64/u64_config.cc:1703-1709` (`MENU_U64_MONITOR`)
  - `io/c64/c64_subsys.cc:330,435-445,838`
  - `io/c64/c64.cc:33,1152,1269` and the `MACHINE_MONITOR` block in `C64::freeze`
  - `io/c64/c64.h:36,473`
- **Makefiles** (sources and `-DMACHINE_MONITOR=1`):
  - `target/u64/nios2/ultimate`
  - `target/u64/riscv/ultimate`
  - `target/u64ii/riscv/ultimate`
  - `target/u2plus/nios/ultimate`
  - `target/u2plus_L/riscv/ultimate`
  - `target/u2/riscv/ultimate`
- **Per hunk** (shared with other features): `u64/u64_machine.cc/.h` (`peek_cpu`/`poke_cpu`,
  `begin_stopped_session`), `u64/u64_config.h`, `userinterface/ui_elements.*`,
  `context_menu.cc`, `path_picker.*`, `browsable*.h`, `io/c64/keyboard*.{h,cc}`,
  `io/usb/keyboard_usb.*`, `usb_hid.cc`, `io/stream/keyboard_vt100.cc`,
  `api/route_input.cc`, `test/stubs/userinterface.h`, `run-tests`.

---

## 12. Resource estimate

**(see ARCH §11.)** The first draft's ≈ 2,550 LUT / 7.5 BRAM become ≈ 360 LUT / 2 BRAM of
debug logic in ARCH plus T21's share of the state bus, the gate and the IO block.

---

## 13. Open questions

1. **Overlay width.** 40 columns makes TRX64's output wrap. Should `overlay.v` and the app
   offer an 80 × 50 text mode (the screen RAM already holds 4 K)? Or stay at 40 with the
   narrow formats of §8.2?
2. **Picture while paused.** Answered by ARCH: still capture in DDR2, drawn by software into
   the pixel layer.
3. **Exact stop.** Is the comparator → `ce` path in the same clk acceptable for timing at
   31.5 MHz? If not, do we accept one cycle late for watchpoints (LATE flag) while exec stays
   exact through the one-cycle look-ahead?
4. **`RUN_NOALIGN`.** May a soft-condition resume skip the frame alignment and leave the
   HDMI picture rolling until the next aligned resume? Or is every resume aligned (up to
   20 ms per soft false hit)? (ARCH has no servo: user decision 2026-10-01.)
5. **ROM lens.** Answered: the core reads its own ROM BRAM (`c64_mem.v`); `rom` reads it
   through the port B that ARCH §7.8 adds.
6. **Colour RAM.** Answered: second port (ARCH, patch 0051).
7. **JAM.** Does the 6502N implement KIL/JAM as a stuck state the debug block can see, so
   that it can stop with reason 9 as TRX64 auto-breaks on JAM (Spec 764)?
   **Answered 2026-10-07 (read in `cores/icepi/6502n/proc_control.vhd`, board 504):** the 6502N has no JAM state
   and the debug block no JAM reason. On a JAM opcode `decode` keeps `next_state <= state`: the CPU stays in
   decode for ever, never fetches again, no `sync`, so no IB and no interrupt is taken; `CYCLES` runs on, `INSTRS`
   stands still. It is seen from outside, in the app (§8.12).
8. **Stop interrupt** (line: `irq_high(2)` since T28, ARCH §14 item 1). Should a stop raise an ITU interrupt, so that `wait_stop` blocks
   instead of polling and the firmware can show "stopped at $xxxx" when no app runs (a small
   firmware hook)? Or is polling enough?
9. **`savecrt`.** Is a `trx`/`media` ABI call into the CRT auto-save path acceptable, or does
   `savecrt` stay unavailable?
10. **Bank qualifier.** Should comparators match the PLA state (RAM vs ROM vs I/O at the same
    address; CTRL b15), using the PLA select lines from icepi's bus arbiter? This is extra
    taps in the patch.
11. **High window.** Answered: neither; the engine copies (ARCH §6.2).
12. **C64RE.** Should C64RE's `runtime_monitor` get a host for the board (through the REST
    route of §7.4), making TRX64 Ultimate a third host of the monitor library (864)?
13. **`r` on the drive.** Should TRX64 also allow `r` writes under `device drive8`, so both
    hosts behave alike? Or does T21 keep that extension private?
14. **The loader's D-2 rule.** Apps run only with the machine stopped, and the debugger then
    runs the machine itself while the app is active. Is that acceptable as the documented
    exception (only through `trx->acquire`)?

## Decision 2026-10-05 (Alex, after the first board look): 80 columns

The 40-column screen (§8 as built) works but is not usable: the verbs print TRX64's wide text and
wrap. Decided: the monitor screen uses **80 columns** (variant B, mock in the chat of 2026-10-05):
fixed rows 1-3 for status, registers, $00/$01, bank, device, ring/trace; a separator; the output
scroll area; a separator; the `.` command line; a key hint row. `m` 16 bytes per row + PETSCII,
`d` full width with labels and a `>` at the PC, `chis` one line per instruction.

Needs: an 80-column overlay mode while the monitor is open (overlay.v is ours; char RAM holds
80x50; 80 x 12 px = 960 px at 1080p), a firmware/ABI call to switch it and restore it on close.

Found at the same look, to fix with it:
- `m <addr>` on the screen shows one page (about 16 rows), `m` alone continues (VICE style);
  TRX64's $800 default stays for RPC/REST.
- Scrolling back (F1/PageUp) through the 200-line history does not work on the board: bug. **Fixed, §8.10 (the cause).**
- "Navigation Style: WASD Cursors" makes w/a/s/d untypeable; the monitor reads keys with its
  own keymap (as Gideon's monitor used e_keymap_monitor).

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

T21: Monitor at TRX64 level (trxmon)

Milestone M1, status ✅. Open (one line, as in the TODO table): – (rest moved to T55)

Decisions: [ADR-0031 trxmon accepts exactly one RPC connection](../adr/0031-trxmon-single-rpc-connection.md); [ADR-0030 No .c64re / ring dump or load and no trace export in M1 (A13)](../adr/0030-timewarp-dump-and-trace-export-out-of-m1.md); [ADR-0024 SLIM: one state path, a lean write log (replaces the freeze session)](../adr/0024-slim-state-path.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- **2026-10-08, T55 built in the app (branch t55-mon-app, spec §8.13; host tests only, NOT board-tested).** Observers with `if`, `do log|cmd`, `ignore`, `rb`, `obs log`; `reu`, `pot`, `io 1541`, `ls`/`rmdir`, `flow`/`focus`/`sf`/`nf`; JAM on attach; assembler prefill; cursor keys; completion on CTRL+I/TAB; WebSocket keepalive. Firmware ABI appended: `fs->dir`, `fs->rmdir`, `ring->log_read`. $D011 = $00 after a REST reset analysed, not proven.
- **2026-10-07 evening, Alex: "Wandern nach T55 .. ich kanns nicht mehr sehen da".** T21 is done for M1. Moved to [T55](../todo/T55.md): the "PAUSED mid" label on a monitor started over an already jammed CPU, the $D011=$00 observation after a reset following menu close (cause unproven), assembler prompt prefill `.a c101 `, keepalive for a vanished RPC holder, CTRL autocomplete, cursor movement within the input line.
- **Board 2026-10-07 evening (Alex, Freeze UI; CI run 514, package integ-m1-e2b2ca47, app 301ef8c3, trxmon.u2a built at app 9a1b996b = same trxmon source as integ-m1-app): PASSED** Run App while trxmon serves in the background (T21-RUN): the background instance ends silently (no "ended" popup, removed on purpose by the fix) and the monitor opens directly; after `x` the BASIC cursor is where it was, no extra cursor block, no ?SYNTAX ERROR (keys no longer leak to the C64). `whowrote d020` (T21-P4): loop at $C000 (LDA $D020 / ADC #1 / STA $D020 / JMP), 8 writers at $C005 13 cycles apart, values incl. $FF->$00->$F2 (carry), old value shown as $?? as designed. JAM: trxmon armed (RPC `g`) and JAM $02 poked at $C008 opens the screen by itself, "JAM at .C:c008". A trxmon that only serves in the background does not watch (only after g/bk/observer) - per §8.5, not a bug. Assembler (T21-80F): an empty line ends `a` mode, RETURN repeats `d` and `m`. Overlay mode (T21-80O/T21-TR): 80 columns, opaque background, scrollback F1. Freeze menu charset after a monitor run is normal lower case (the afternoon bug did not reproduce). **Open, found tonight:** (1) monitor started from the menu while the CPU is already jammed shows "PAUSED mid $c008" instead of "jam": the menu freeze stops the CPU before trxmon attaches and the attach path does not run `stop_is_jam`; retest after the fix. (2) Wish (Alex): in assembler mode the next prompt prefilled as `.a c101 ` (VICE style) instead of a bare `.`. (3) Open observation, cause unproven: after a REST `machine:reset` while the freeze menu/monitor had just been closed, $D011 read $00 (black screen); a second reset fixed it. Suspect: the menu restoring VIC registers after the reset. Not run tonight: `triage`, `sd`, `rstep`, §9.3 stop/step checks.
- ****Board 2026-10-07 (CI runs 500/504, Alex): PASSED 80 columns in Freeze and Overlay, opaque background, Run App from the menu, P4 trace former (`trace on`, `chis`, `whowrote` on RAM), breakpoints, `z`/`n`/`ret`/`g` with a stable picture, observers, A13 removals. Open = the bugs found that session (fix agent running, branch t21-monfix-app): keys reach the C64; stale stop request ends every later menu start; `whowrote` misses I/O writes; JAM not reported as a stop; monitor opened by itself; Freeze menu charset switched afterwards; empty line did not leave assemble mode (fixed on t21-return-app). Known limit, M2/T66: `rstep` drifts after 2 steps (VIC state not restored).**
- **Fixes built 2026-10-07 (app t21-monfix-app, merged into integ-m1-app affe98ec; spec T21 §8.12 on t21-monfix): keys owned by the monitor in Freeze too (TRANSPARENCY 0xC0, wait for key release), stop request cleared + menu start ends the resident app and starts the new one (§7.6), `whowrote` on $D000-$DFFF from the trace (trx_ea.h; old byte unknown `$??`, pointer stores counted), JAM detected (no instruction for 100k cycles + JAM opcode at IPC -> stop `jam`), a JAM opens the resident monitor's screen (intended, says JAM), A08 popup "Timewarp is off while REU is on", RETURN ends assemble mode / repeats d/m. Not solved: Freeze menu charset switch (cause not proven; mitigation `menu_back()` after run_app with UART log `[TRX] menu view/charset ...`). Board retest open.** Rest -> T55 (M2).
- **Feature wish (Alex 2026-10-07, board session): autocomplete in the monitor screen like TAB in a shell, on the CTRL key (the C64 has no TAB): completes verbs, then arguments (observer names, paths). Not planned yet; check first whether the firmware keyboard reports CTRL alone as a key.**
- **Feature wish (Alex 2026-10-07): line editing in the monitor command line: cursor left/right moves inside the line, insert/delete at the cursor (today only typing at the end and DEL).**
- **Decided (Alex 2026-10-06, for C64RE spec 889): trxmon accepts exactly ONE RPC connection; a second one is refused with a clear reason naming the held port (and the peer address); `ping` carries `capabilities` (served methods + notifications) and the real build ids (firmware git sha and trxmon git sha) instead of the constant "trxmon 0.1". Built 2026-10-06 (app t21-rpc1-app 2fcac32b + ABI 5390a16e `net->peer`, `api->firmware_git`; merged into integ-m1-app): a second client gets WS close 1013 / HTTP 503 "port 4312 is held by <peer>"; ping has `build{trxmon_git,firmware_git,firmware_version}` and `capabilities{methods,notifications}`; host tests 746+69+23 pass; not board-tested. Open: a holder that vanishes without FIN (cable pulled) blocks the port (no keepalive).**
- **2026-10-06: trxmon manifest gets `"rest": [ "serve" ]` (app 5b605bb0): REST start PUT /v1/apps/trxmon:run was 403 (found for C64RE spec 889).** Monitor at TRX64 level (docs/specs/T21-monitor.md).
- **In main 2026-10-06 (ba060ae):** hardware P1 (steps, counters, exec bitmap, 2 watch units, drive comparator); CPU write path (settled registers on the state bus, register writes and `g <addr>` at zero cycles via the T20 staging, IPC_SET, CAPS2 b2); trxmon.u2a with the TRX64 WS JSON-RPC contract incl. P5 ring verbs on T20 (rstep, frame ±N, goto, rewind, marks, checkpoint/transport/ringbuffer methods), `a`/`asm`, `device drive8`, files, `bt`; lenses (spec §3.4.1: cart/cpu lenses, REST `machine:readmem` `lens=`/`sidefx=`, trx_lens.c shared by firmware and trxmon; board PASS 2026-10-04 on emulated EF/EFXL and on a real port cart, T16 §8.4); P4 trace former (§4.11.1, app t21-p4-app: `trace on|off|status`, `chis`, `whowrote`, `triage`, `sd`); monitor screen 40 x 23 on the overlay (§8; menu Developer > TRX64 Monitor, Ctrl-O; Gideon's Machine Code Monitor removed from the u64ii build, Alex 2026-10-04; `x`/`bk` reopen fix, §8.8); 80-column screen 80 x 40 (§8.10, t21-80col, in main).
- **Board 2026-10-06 (Alex): FAILED: trxmon is 40 columns with Interface Type Freeze** ("das Freeze Overlay IST MEIN UI": 80 columns there too).
- **Built 2026-10-06 in integ-m1, NOT in main, NOT board-tested:** t21-80freeze (§8.11, app t21-80freeze-app 26c4dde2: 80 x 40 in Interface Type Freeze).
- **Superseded by SLIM 2026-10-05:** "a stop IS a freeze" as a T12 shadow session (`dbg_stop`/`dbg_rel`, sim/dbgsess, §14.8): a debugger stop is the plain machine hold, accesses are bus cycles. **A13 / T66:** `dump`/`undump`/`ringdump`/`ringload` verbs and the trace export (`tracering`, `trace/build_from_ring`, `.c64retrace`) leave M1 (trace export removed 2026-10-06, a13-trace, in integ-m1).
