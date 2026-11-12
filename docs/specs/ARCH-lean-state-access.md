# ARCH — Lean state access: one state bus for T12, T16.2, T20, T21

> Note (2026-10-01): `cores/icepi` is now our own code (`cores/icepi/ORIGIN.md`);
> the "icepi patch NNNN" numbers in this and the other specs name change sets, which
> are made as direct edits and commits, not as patch files.

**Status:** draft architecture, 2026-10-01. Partly built (branch arch-p1, see §15 "As
built"): the IO block pages 0/1/5 in the C64 clock, the JTAG page, the state bus with
capture shadows for 6510, VIC, CIA ×2 and the cartridge's banking registers, the capture
control, the colour RAM and ROM second ports and the freeze session responder. Replaces the separate
state-access hardware of T20 (`T20-delta-ring.md`: shadow chains, ring engine, TW block),
T21 (`T21-monitor.md`: CPU state port, comparators, bitmaps, flow stack, trace client, TRXD
block), T12 (`T12-freeze-overlay.md`: peek port, `shadow_io` files, `c64_text`, still layer,
TV block) and the register/dirty-map part of T16.2 (`T16.2-app-cartridge-writeback.md`, CW
block). Those specs keep their feature content (verbs, ring policy, file formats, freeze
semantics, write-back service) and point here for the hardware. T16 keeps its design; its patch number and app-side register address follow this plan (§13).

**One sentence:** every chip gets one word-addressed, side-effect-free read and staged-write
port on a shared **state bus**, usable only while the T12 B gate is shut; a small
**engine** (DDR2 client) moves words, logs and memory blocks; the RISC-V sequences
everything else in software; the per-frame snapshot is a **one-clock shadow capture**: every
bit the state bus reads has a shadow flip-flop loaded by clock enable at the frame's snap
edge, and the engine copies the shadows to DDR2 during the following frame, so the machine
never stops for it.

**Marks.** **[TRX64: file]** = concept copied from our emulator, paths relative to
`/path/to/Development/C64/Tools/TRX64/`. Hardware facts are from this repo's code, paths
relative to its root. Numbers from `designs/c64_basic/build/utilization*.rpt` (build m10,
post-route, 2026-10-01).

---

## 1. Why

| | LUT | FF | BRAM tiles | DSP |
|---|---|---|---|---|
| XC7A50T | 32,600 | 65,200 | 75 | 120 |
| today (m10) | 19,793 (60.7 %) | 18,761 (28.8 %) | 54 | 10 |
| T20 + T21 + T12 as specified | +9,300 … +11,700 | +8,300 | +14.5 | +3 |
| + T16.2 CW block (its §4.3) | +≈ 800 | +≈ 700 | +0.5 | — |
| **sum** | **29,900-32,300 (92-99 %)** | 27,800 (43 %) | **69 (92 %)** | 13 |

LUT and BRAM run out; FF and DSP do not (46,000 FF and 110 DSP left). The target here is
**≤ 80 % LUT (26,080)** and **≥ 10 BRAM tiles free** with all four features in, measured
against the same m10 baseline (§11).

## 2. Principles (approved 2026-10-01)

1. **One state bus per chip, paused only, read and write.** 6510 (incl. mid-instruction
   state, read-only), VIC, CIA ×2 (TOD/ICR without read side effects), SID, colour RAM,
   CPU port, REU, cartridge (incl. the T16.2 flash responder and 93C86), drive CPU, VIAs,
   floppy. T20 (capture/restore), T21 (registers, step), T12 (session reads) and T16.2
   (responder state for snapshots) all use the same bus. Whatever drives it is software on
   the RISC-V; the per-frame capture is a one-clock shadow load plus a hardware copy of the
   shadows into the ring (§4, §6.3), which the app reads later without a deadline.
2. **FFs instead of LUTs** for anything that only holds state: capture shadows (clock
   enable, no mux), staging registers for writes, overlay valid bits, SRL→FF where a value
   must be readable; comparators,
   counters and IO bridges are shared or cut to the minimum.
3. **Software where timing allows:** text screen and still into the LVGL pixel layer,
   capture/restore sequencing, undo/redo order, deltas, dirty maps, quiet timer, counters
   derivable from events, digests, all file formats, flow frames, observer conditions.
4. **ROMs:** not from DDR2 (§7.8: a miss would stall the CPU or the VIC, which breaks
   cycle exactness); instead the ROM BRAM is repacked to what is used (−3 tiles).

---

## 3. Facts this design rests on (code)

| # | Fact | Where |
|---|---|---|
| F1 | The C64 runs on `gclk` from `BUFGCE bg_c64`; the gate stops after a phi2 falling edge (`xc_phi2_n`); resume only when `shut == FRAME-1`, `FRAME = 628992` (312 × 63 × 32 dot4x). A bus DMA byte while halted opens the clock for its cycle and sets `held`. | `top.v:120`, `c64_core.v` gate |
| F2 | Pixel clock = 125/26 × dot4x exactly (external CDCEL824 fed from a second MMCM; GTP refclk); scaler output 2688 × 1125 pixel clocks = 3,024,000 = **628,992 dot4x = one VIC frame**. One output line = 559.104 dot4x. | `top.v:10-14,107-133,862-917`, `vic_scaler.v` |
| F3 | `vic_scaler` locks once (`locked` set on the first `frame_seen`, never re-locked) and then free-runs; the VIC writes a 64-line ring (`ring[{py[5:0],px[8:0]}]`, 4 RAMB36), output line y reads VIC line `Y0 + y/4`, lock at VIC line `ALIGN = 20` px 0. | `vic_scaler.v` |
| F4 | From F3: since the scaler never re-locks, a stop of n dot4x leaves the VIC n behind its locked position unless the stop lasts whole frames (today's resume rule); the picture shows a wrong line beyond ≈ 6,400 dot4x of lag. A per-frame snapshot that stops the machine would therefore need a change to the video timing. User decision 2026-10-01: no such change, no per-frame stop. | derived from `vic_scaler.v` |
| F6 | `c64_mem` port A reads and writes the same address in one `always` block: the BRAM is read-first, so `ram_lo_q/ram_hi_q` hold the **old** byte of every write for free. Port B (`dbg_clk` = `clk`) runs while paused. | `c64_mem.v` |
| F7 | The ROM array is 16 K × 16 (two 16 K × 8 lanes, 8 RAMB36) but holds 20 KB: char at 0x0000, KERNAL 0x2000, BASIC 0x4000; 0x1000-0x1FFF and 0x6000-0x7FFF unused. Blank in the bitstream; the app writes it through its ROM apertures (port B). | `c64_mem.v`, `ultimate_sys.vhd` |
| F8 | A cartridge line-cache miss holds the CPU with RDY (`cpu_rdy = … && !(ext_cart_en && ext_cart_wait)`) while VIC/CIA/SID run on; the REU waits for `reu_mem_rvalid`. Both make machine timing depend on DDR2 latency: **the machine is not deterministic** with an EasyFlash or an REU in use. Decision 2026-10-01: the swapped-in icepi cartridge (F11) keeps `cart_fill`'s line cache with a fixed answer time and never stalls the machine on DDR2; the REU stays as it is (§14 item 8). | `cart_slot.v:174`, `c64_top.v:368`, `c64_reu.v` `ST_READ_REU_D` |
| F9 | Drive A runs on `cpu_clk` (100 MHz) with `tick_16MHz/tick_4MHz` from `fractional_div` in `ultimate_sys.vhd`, masked by `halt` (= `c64_halted` through 2 FFs). Its IEC inputs cross clock domains. | `drive_unit.vhd:94-96`, `ultimate_sys.vhd:454`, `top.v:375` |
| F10 | Every ITU interrupt bit has a meaning in Gideon's app: `irq_in` 3 TAPE, 4 CMDIF (UCI), 7 RESET; `irq_high` 0 ACIA, 1-2 WD177x (drives A/B), 3 WiFi, 4 BLING, 5 HDMI, 6 UNLOCK, 7 GURU (`software/system/itu.h:33-47`). Our design drives only `irq_in` 2, 5, 6 and `irq_high` 3, 4. There is no unclaimed line. Decision 2026-10-01: borrow `irq_in(4)` (CMDIF, the UCI, absent until T28); T28 finds the lasting solution (TODO.md notes it). T28 (2026-10-02): the UCI takes `irq_in(4)` back; the IO block moves to `irq_high(2)` (drive B's WD177x, and we have no drive B; a drive B would need it back). | `ultimate_sys.vhd:377,404` |
| F11 | icepi's `c64_cartridge` with `c64_flash_amd` / `c64_eeprom_93c86` is vendored but not built today (patch 0030 put `cart_slot`/`cart_logic` on `ext_cart_*`). User decision 2026-10-01: swap icepi's model back in (separate task). As vendored it has no cache (ROM and cart RAM go through the main `mem_cmd` port); the swap (cart agent) keeps `cart_fill`'s line cache with a fixed answer time instead, the 93C86 array is in block RAM, and the firmware applies flash operations and pulses `flash_ack`. | `c64_core.v`, `c64_cartridge.v:23-81,431-449` |

---

## 4. The per-frame snapshot: one-clock shadow capture

User decision 2026-10-01: no per-frame stop, no change to the HDMI timing, no change to the
C64's speed. The machine is never stopped to take a snapshot.

### 4.1 Mechanism

- **Shadow per read bit.** Every bit that the state bus reads (the R columns of §5.4) has a
  shadow FF on the **ungated** clock of its domain (`clk` for the C64, whose `gclk` is the
  same MMCM output through the BUFGCE, so the path is synchronous; `cpu_clk` for the drive):
  `D = live bit`, `CE = sb_capture`. On the ungated clock the shadow can load even when no
  `gclk` edge follows, which the stop case needs (§4.3). The D input is a plain route from the live FF and the
  CE is one global net, so a shadow costs **one FF and no LUT**. The state-bus mux tree
  (already in the design for the paused read) reads the shadows instead of the live bits;
  there is no live-read path (§4.3 says why none is needed).
- **Capture edge** (`sb_capture` high for the one `clk` cycle after the chosen `gclk` edge,
  while the live bits hold that edge's result; the machine runs on): the first
  6510 instruction boundary on VIC line 0 at or after cycle 12 that meets the snap condition
  of §5.4 (border FFs set, no sprite pixel in the last 6 pixels, no VIC access in the last
  CPU cycle, REU idle; frames in which the REU was active are flagged "not exact", §14 item
  8). The capture is a phi2-falling edge, the same kind of edge the gate
  stops on, so a captured block and a stopped machine are the same kind of state, and a
  restore (which loads at a stop, §5.6) is exact. The instruction boundary is not needed for
  the hardware (the 6510's mid-instruction state is in the read set) but costs nothing and
  keeps every frame dumpable to `.c64re` without a lossy CPU step (T20 §4.1). If the
  condition does not hold by line 15, the frame has no chip block and is flagged.
- **Copy** (engine, §6.3): after the capture the sequencer reads the ≈ 129 words
  (≈ 520 B) over the state bus into log stream 2 while the machine runs: ≈ 111 C64 words at
  1 clk plus 18 drive words at 4 clk (quasi-static read, §5.1) ≈ 185 clk ≈ 6 µs, against
  a budget of 20 ms. The app sees "capture done" (IRQ cause or poll) whenever it likes; it has
  no deadline.
- **Consistency with the write log.** At the capture edge the capture logic pushes a frame
  marker into stream 0 and stream 1 in the same clock (both taps are in `sys_clk`, the
  marker goes in ahead of any write of the next edge). The marker carries the frame number
  and the capture generation; the chip block in stream 2 carries the same pair. So RAM
  deltas and chip state meet exactly at the capture edge: undo of frame k = the records
  between markers k and k+1.
- **Drive.** Its shadows load on the `cpu_clk` edge after `sb_capture` crosses (2 FFs,
  ≤ 30 ns later). The drive is not phase-locked to the C64 anyway (F9); the offset is
  inside the up-to-one-tick drive phase looseness T20 already accepts. Drive RAM writes are
  logged with a marker on the same synchronized pulse.
- **Memories** (C64 RAM, colour RAM, drive RAM, cart RAM) are not shadowed: they are covered
  by the log and by keyframes with the software fix-up (T20 §4.3), all positioned by the
  markers.

### 4.2 Race between the copy and the next capture

The shadows are a single buffer. Captures come from two sources: the frame capture (every
20 ms) and the stop capture (§4.3). Rule: **capture generation + skip/tear flag.**

- `CAP_GEN` (16 bit) counts captures. The sequencer latches it when it starts a copy and
  compares it when it ends; the block's trailer carries both values.
- A frame capture while a copy is still running is **skipped** (it cannot happen at 6 µs
  against 20 ms, but the rule makes it safe for any cadence); the skipped frame is flagged.
- A stop capture (debugger, freeze, T12 B pause) is never skipped: if it lands inside a
  running copy (a 6 µs window per frame), the copy ends with `CAP_GEN` changed, the engine
  writes the block as **torn** and the frame is flagged "no chip block" — the T20 case for
  a frame whose snap condition failed. Probability ≈ 6 µs / 20 ms ≈ 0.03 % per random stop. Accepted (§14 item 2); the deterministic sweep of §12 A3 measures the exact window and how often it matters.
- A double buffer (two shadows per bit and a bank bit in the mux) would avoid the torn
  case at +3,460 FF and ≈ +0.3 LUT per bit for the extra mux level; not worth it for a
  0.03 % window.

### 4.3 Reading while stopped (T21, T12, restore checks)

While the gate is shut (`halted`), `sb_capture` is held high: the shadows follow the
frozen live bits on every `clk`, including after a bus-DMA cycle, a step or `COMMIT_NOW`.
T21 and the T12 responder therefore read the shadows and see the live machine; no
live-read mux is needed. A staged write becomes visible right after the edge that loads
it. The start of a stop counts as a capture for `CAP_GEN` (§4.2).

### 4.4 Alternatives and why not

| Option | Why not |
|---|---|
| A short stop per frame with a frame-phase servo (previous version of this document) | Needs a longer HDMI frame (one extra line) and a 0.089 % slower C64, and stalls the SID for ≈ 6 µs every frame. Rejected by the user. |
| Shadow chain with shift/load mux per bit (T20's first draft) | ≈ 1-1.5 LUT per bit for the shift and load muxes. The CE capture here has no mux in front of the shadow; the only mux is the read tree, which the paused read needs anyway. |
| Double-buffered shadows | §4.2: +3,460 FF and ≈ +1,000 LUT of mux for a 0.03 % torn-block window. |
| Minimal subset per frame + deterministic replay | The machine is not deterministic with an EasyFlash or an REU (F8), and the drive's IEC inputs cross clock domains (F9). Kept as a check (T20 §7), not as the storage format. |
| Keyframes every N s with a whole-frame pause | A black frame every N s; per-frame scrub still needs per-frame state. |

---

## 5. The state bus

### 5.1 Signals (`sys_clk` = dot4x domain, from the engine or the IO bridge)

| Signal | Width | Dir | Meaning |
|---|---|---|---|
| `sb_addr` | 12 | → chips | `[11:7]` chip id, `[6:0]` word |
| `sb_rd` | 1 | → | read strobe; data valid 2 clk later (one pipeline register stage after the mux tree) |
| `sb_rdata` | 32 | ← | OR of the chips' outputs (each chip drives 0 unless selected) |
| `sb_we` | 1 | → | write `sb_wdata` into the chip's **staging** word `sb_addr` |
| `sb_wdata` | 32 | → | |
| `sb_load` | 32 | → | per chip id: on the next `gclk` edge every writable FF of that chip takes its staging value instead of its next state |
| `sb_capture` | 1 | → | shadow clock enable on `clk`: one cycle after the frame snap edge, and continuously while halted (§4.1, §4.3); synchronized into `cpu_clk` for the drive |
| `sb_frozen` | 1 | → | `halted && !dma cycle` (gate shut); **writes** outside it are ignored and set `ST_ERR`. Reads are allowed at any time: they return the shadows, which change only at a capture |

Drive side (`cpu_clk`): the same signals behind a **quasi-static bridge**: between two
captures the drive's shadows do not change, so the sys_clk address is held 4 clk before the
data is sampled (false path with a `set_max_delay` constraint), and `sb_load_drv` is a toggle
synchronized into `cpu_clk` and applied on the next `cpu_clk` edge while the drive is
halted (with Gideon patch 0003, §13, `halt` freezes every drive FF, so that edge changes
nothing else).

### 5.2 Semantics

- **Read** = the shadow FFs through a mux tree; no strobe reaches the chip, so no side
  effect (no ICR clear, no TOD latch, no collision clear, no REU status clear, no SID OSC3
  change). The shadows hold the state of the last capture: the frame snap edge while
  running, the stopping edge while stopped (§4.3). Mid-instruction 6502 state is readable
  at any stop.
- **Write** = staging register (FF, written by `sb_we` with a per-word decoded enable; no
  mux in front of it: the data comes from the shared `sb_wdata`). `sb_load` makes the
  chip's FFs take staging at the **next opening edge** of the gate (step, run, or an
  explicit one-tick `COMMIT_NOW`). At a stop right after phi2 falls, that edge is phi1
  tick 1, on which no programmer-visible register and no 6502 register changes, so a
  register write is exactly "set while paused" and costs no machine time. A full restore
  loads every chip at once: the machine continues from the captured instant one edge late.
  `COMMIT_NOW` (one extra tick, reported) is for a debugger that wants to read back.
- **Not writable** (read-only words): 6502 internals that do not matter at an instruction
  boundary (address latches, carries, `h_reg`), derived values. Restores happen at
  instruction boundaries only (T20 snaps there); `state` is written only as the boundary
  value or as `fetch` for T21's PC redirect.
- **Memories** are not on the bus: C64 RAM (port B), colour RAM (second port, patch 0051),
  ROM (port B, read), drive RAM (DDR2 + `drive_mem` flush), cart image/RAM (DDR2), EEPROM
  (RAMB18 port B). The engine (§6) moves them.
- **Caches** (any cartridge-image cache, the `drive_mem` line cache) are invalidated by
  `sb_load` of their chip.

### 5.3 Cost model

From the Artix-7 slice: a LUT6 is a 4:1 mux, two LUTs + MUXF7 an 8:1, four + F8 a 16:1.

| Per state bit | LUT | FF |
|---|---|---|
| readable (shadow + its share of the mux tree) | 0.30 | +1 (shadow, `CE = sb_capture`) |
| writable | +0.55 (LUT6_2: two `load ? stg : d` muxes per LUT, 5 shared inputs; often absorbed into the FF's own D LUT) | +1 (staging) |
| per chip | 15 (address compare, word decode for staging enables, output AND) | 0 |
| per SRL bit made readable | −1/depth LUT (the SRL disappears), +1 FF | +1 |

### 5.4 Chip map

Bit counts are from the code (inventory 2026-10-01 of `cores/icepi`, `cores/gideon` and
`designs/c64_basic`, cross-checked against `utilization_hier.rpt` and the SRL report in
`build/vivado.log`). **R** = bits the bus reads, each with a capture shadow (all
architectural state; registers that the chips' own read paths could show are shadowed too,
because the copy runs while the machine runs on); **W** = bits the bus writes (state that the bus
replay of §5.6 cannot set, or that changes by itself). Registers a C64 bus write can set are
restored by that replay, not by the state bus.

| Id | Chip | R bits | W bits | Words | What is in it | Excluded (and why) |
|---|---|---|---|---|---|---|
| 0 | glue | 40 | 12 | 2 | `last_bus` (open bus, arbiter), `reu_override`, `cpu_paused`, reset pending ×2, input sampler state, cycle counter (read) | `ext_dma_*`, memory handshake (idle at a stop), LEDs |
| 1 | 6510 | 194 | 150 | 7 | A X Y S P PC (56), `$00/$01` + 3 floating bits + `fall6/fall7` 2 × 19 (57), at-`sync` pending bits `i_reg`, `dreg`, `trigger_upd`, `copy_d2p`, `flags_update`, `irq_inhibit`, `vectoring_i`, `intg_i`, `nmi_d2`, `nmi_act`, `resetting` (19), wrapper latches live at the stop `din_r`, `rdy_r`, `irq_r`, `nmi_r`, `pin_r` (12), `state` (5, written only as the IB value or `fetch` for a redirect); read-only: `adl/adh`, carries, `h_reg` and the rest (T21 mid-instruction view) | — |
| 2 | VIC | 775 + 480 | 535 + 480 | 25 + 15 | `char_buf` 64 × 12 LUTRAM (40 entries used; read through a second LUTRAM port, no shadow needed: it is written only by badline c-accesses on lines 48-247, so it is static while the copy runs on line 0; written while frozen through a second BUFGCE "restore clock" on its write port, ≈ 40 LUT, §14 item 4), registers (269, read only: kawari's read case is clocked, so not usable while frozen), IRQ/collision latches (22), per-sprite MC/MCBASE/YE/DMA/display/active/halt/XE/MMC and `sprite_pixels_shifting` 8 × 32 (408), `char_shifting` (12), border FFs + delay bits (13), position: `raster_x/line/line_d`, `xpos`, `sprite_raster_x`, `cycle_type`, `sprite_cnt`, `refresh_cnt`, `idle_cnt`, `refc`, `char_buf_counter` (≈ 80) | sprite delay pipelines 252 bits (snap condition: no sprite pixel in the last 6 pixels),  267 register-copy bits + 6 pending pulses (snap condition: no VIC access in the last CPU cycle), phase rings `phi_gen`/`phi_phase_start` (identical at every stop) |
| 3 | CIA 1 | 230 + 128 | 185 | 12 | timer pipeline (16), CR pipeline (4), TOD prescaler/alarm_eq (22), serial (34), ICR flags/mask/delays (20), FLAG edge (3), PC shift (3), timer latches (32, write by replay), TOD alarm (24), `clatch` (24), timer counters (32) and TOD clock (24) in W because they move during the replay, 50 Hz divider `tod_cnt/tod_out` (26) | — |
| 4 | CIA 2 | 204 + 128 | 160 | 11 | as CIA 1 without the TOD divider | — |
| — | CIA 1/2 registers | (in the rows above) | 0 | (above) | the 16 bus registers as `cia_core` holds them (DDR, timer counters, CR, TOD clock, SDR, ICR flags; PRA/PRB pins are derived), shadowed like the rest | — |
| 5 | SID ×2 | 930 | 530 | 30 | R: all voice and filter registers (400, write-only on the bus, so the replay needs them from here) plus W. W: what the CPU can read: voice 3 of each SID (noise 39, envelope 48, `st` 2, `wav_prev` 29, osc19/pulse 4), the accumulators of all 6 voices (sync/ring chain, 144), POT (27), `dbus` value + age (36), `count_us` (10), `primed` (2), pending bus-write latch (19), `mono_timer` (18). The voice-state SRL rings are built as FFs (`shreg_extract = "no"`), so these sit in fixed FF stages at every stop. | audio-only state: voices 1-2 envelopes, noise, `wav_prev`, filter integrators, DCA, the snd_clk side (≈ 920 bits). After a restore those voices continue from the live machine's envelope. Not restored by decision (§14 item 3; saves ≈ 780 LUT). |
| 6 | REU | 125 | 64 | 4 | registers (69) + autoload shadows (54) + `r_int` (2) read; W: shadows, `r_int`, `r_cmd` (8; the replay must not start a transfer) | engine state: snaps only with `state == IDLE` (a transfer holds the CPU, so no instruction boundary comes before its end anyway) |
| 7 | cartridge (icepi, after the swap) | 160 | 120 | 5 | `reg_0`, `reg_1` (16), AR registers + freeze latches + u5 counter (23), `c64_flash_amd` states, busy, toggle, pending event (42), `c64_eeprom_93c86` shift state (70); W leaves out what the replay sets (`reg_*`, AR `$DE00/01`) | memory handshake (`rom_enable`, `ram_enable`, `ee_issued`) |
| 8 | colour RAM | — | — | — | not a word chip: second BRAM port on `clk` (patch 0051) for the engine and the session responder | — |
| 9-12 | drive CPU, VIA 1, VIA 2, floppy/timing | 550 | 520 | 18 | drive 6502N (A 56, at-`sync` bits incl. live `so_d` 20), VIA register files (252) and hidden timer/shift/handshake state (72), `cpu_part` live `ext_rdata`/`io_select` (9), `c1541_timing` `pre_cnt`/`behind` (6), floppy rotation and read state (`bit_timer`, shift/PLL/byte-ready, `track_i`, noise LFSR, `offset_count`, `drv_rdata`, ≈ 105); config (17) read only (the app sets it) | VIA input synchronizers/edge history (36; settle in 2 clocks after a stop), `drive_mem` cache (invalidated), param RAM (app data), dirty bits, `floppy_sound` |

**Totals:** R ≈ 3,940 bits (3,460 in shadow FFs, 480 `char_buf` read directly), in ≈ 129
words (C64 111 words at 1 clk, drive 18 words at 4 clk ⇒ a copy of ≈ 185 clk ≈ 6 µs,
≈ 520 B per frame). W ≈ 2,760 bits.

**Snap condition** (hardware, ≈ 20 LUT, decides the capture edge: checked at each
instruction boundary from line 0 cycle 12 on, where no badline and no sprite BA window can be open): border FFs set, no
sprite pixel in the last 6 pixels, no VIC access in the last CPU cycle, REU idle. If it does
not hold by line 15 (open borders with sprites in them, a long REU transfer), the frame gets
no chip block and is flagged; it is still covered by the log and by its neighbours' blocks.

### 5.5 Chain map

A generated table (`docs/specs/ARCH-state-map.md`, made by a script from the patched HDL in
phase A2) lists per chip and word: bit → signal, width, R/W, reset value, and the `.c64re`
field it maps to (T20 §9.3). The capture table of §6.3, the app's `.c64re` writer and T21's
`r`/`io` views all read the same table. Completeness gate as T20 §13.1-1: every sequential
cell under the C64 and drive hierarchies is in the map or on a reviewed exclusion list (the
"Excluded" column above) **[TRX64: the self-enumerating gate of the checkpoint fidelity tests]**.

### 5.6 Restore = bus replay + hidden load

Restoring a chip block (T20 §8.1 step 5, `.c64re` load) needs no write path for anything a
C64 bus write can set. Order, all while the machine is stopped at an instruction boundary
(the CPU is then in a read cycle, so RDY parks it without a write):

1. **Replay** (software builds the list from the block, engine issues it as T12 B bus DMA
   with Ultimax forced so I/O is visible): VIC $D000-$D02E (not $D019/$D01E/$D01F), CIA
   latches/DDR/PR/CR (start bits as stored)/TOD alarm, SID voice and filter registers, REU
   $DF02-$DF0A, cartridge `$DE00/$DF00` registers; then one idle cycle (no VIC access in the
   last cycle). ≈ 200 machine cycles; whatever they do to counters, timers, IRQ flags and the
   CPU is overwritten in step 3. **[TRX64: the coarse-cut register replay of
   `c64re_snapshot.rs` restore]**
2. **Memories** (`APPLY` / `RAM_IN`, §6.2).
3. **Hidden load**: `SB_IN` writes the W words into staging, `sb_load` for all chips; the
   load takes effect on the next opening edge (§5.2), which is the resume.

The W columns above are therefore the only bits with a load mux.

---

## 6. The engine (one DDR2 client, `ui_clk`)

One block, `trx_engine.v`, replaces T20's ring engine + mover + client "r", T21's trace
client + FIFO + high window, and T12's capture requester. It owns **one** `mig_arb` command
port (`cart_fill`-style `e_*`: commands back to back, reads pipelined), priority after
`cart_fill` and the drive, before the CPU/JTAG pair and the video client (§7.5). Its jobs:

### 6.1 Log drain (always on while armed)

One record FIFO, sys_clk → ui_clk, **512 × 64 bit (1 RAMB36)**, drained in 128-bit MIG
writes into up to four circular DDR2 areas (`LOG_BASE/SIZE/WR[n]`). Sources, each with a
2-bit stream tag in the FIFO word (not stored):

| Stream | Producer (dedicated, §7) | Record (format owned by the feature spec) |
|---|---|---|
| 0 | C64 RAM write tap (both ports) | T20 §4.2 stream A (4 B, packed two per word) |
| 1 | colour RAM tap, cart RAM tap on the cartridge model's memory port (old byte by read-before-write), drive RAM tap, EEPROM tap, `LOG_PUT` (flash changes made by the CW ISR, §7.4) | T20 §4.2 stream B (8 B) |
| 2 | frame marker + chip block words from the capture sequencer (§6.3), input log | T20 §4.4 index / §6.1 block, §7 input records |
| 3 | T21 trace former (bus cycles, instruction records, LOG actions) | T21 §5.9 (8 B) |

Overflow sets `LOG_GAP[n]` and counts `DROPPED[n]` (T20 §4.6 rule). At worst case
(T20 11.8 MB/s + T21 bus trace 8 MB/s) the drain needs < 2 % of MIG peak.

### 6.2 Mover (commanded by the RISC-V, one job at a time, `ENG_CMD`)

| Op | Source → destination | Used by |
|---|---|---|
| `COPY` | DDR2 → DDR2 (16 B units) | T20 keyframe slots, `.c64re` staging below 64 MB (the app's CPU sees only 64 MB, T20 §5.1), T21 trace/REU reads |
| `RAM_OUT` / `RAM_IN` | C64 RAM port B ↔ DDR2, colour RAM port ↔ DDR2 | T20 keyframes (with the T20 §4.3 fix-up done in software from the log), T12 session backup of $0400-$0FFF + colour |
| `APPLY` | a list of stream-0/1 records in DDR2 → memories, old bytes in reverse (undo) or new bytes forward (redo) | T20 seek/step (§8.1 step 4); 1 byte/clk on port B: a 7,000-record frame in 0.25 ms as T20 §8.3 |
| `SB_OUT` / `SB_IN` | state bus words ↔ DDR2 via the capture table (§6.3) | T20 restore of a chip block, T21 `.c64re` dump, T12 nothing |
| `FILL` | constant → DDR2 range | ring and slot clears, test patterns |

Engine writes into machine memory are not logged (T20 §8.1 step 3). Everything about *which*
records, frames and slots is software (T20 index, retention, marks).

### 6.3 Capture sequencer (sys_clk, the only state-bus master in hardware)

A 64-entry table (LUTRAM, written by software) of `{chip id, first word, count}`. After a
frame capture (every `CAPTURE_EVERY` frames, at the edge chosen by the snap condition,
§4.1) it walks the table **while the machine runs**, reading one shadow word per clk into
stream 2, does the drive entries over the quasi-static bridge (4 clk/word), and closes the
block with `{frame, CAP_GEN at start, CAP_GEN at end}` (§4.2: skipped or torn blocks are
flagged). It also raises the "capture done" IRQ cause. `SB_IN` walks the same table writing staging words from a DDR2
block, then raises `sb_load` for the listed chips. ≈ 60 LUT.

---

## 7. Dedicated hardware that stays (and why)

### 7.1 Gate and stops (`c64_core.v`)

- T12 B's whole-frame resume rule (`shut`) stays. **`RESUME_PHASE`** (T20 first draft §8.1
  step 6, ≈ 30 LUT, 20 FF): after a restore the VIC stands at the block's snap position, so
  the resume condition becomes `shut == (FRAME − 1 + RESUME_PHASE) mod FRAME`.
- **Capture control** (≈ 30 LUT, 40 FF): `sb_capture` one `clk` after the frame snap edge
  (snap condition, `CAPTURE_EVERY`, skip while a copy runs) and held while halted (§4.3);
  `CAP_GEN`; frame markers into streams 0/1.
- **Instruction-boundary stop**: `sync && !intg_i` at the end of a fetch (T21 §3.3) from
  the 6502N debug taps (patch 0060-cpu); IPC latch (16 FF) is a state-bus word.
- **Step**: `STEP_CYCLES n`, `STEP_INSTR n` (one 32-bit down-counter shared by both).
- **Counters**: phi2 cycle counter 48 bit (T20 `cycles`, T21 `CYCLES`, CW `EVT_TIME`),
  instructions retired 32 bit, drive cycles 32 bit. One cycle comparator (`CYC_CMP`).
- **Halt sources**: `machine_ctl[3] | (fw_c64_stop & !own) | dbg_halt | capture` (T21 §6.1).

### 7.2 Debug (T21): bitmap + two watch units

| Part | Kept | Cut (and what replaces it) |
|---|---|---|
| Exec bitmap 64 K × 1 | yes, 2 RAMB36 (any number of `bk`) | — |
| Access bitmaps load/store | — | 2 watch units; more watches = soft (T21 §4.4.1 level 2) |
| Comparators | **2 watch units** (C64), each: source select {CPU address, raster line+x}, range lo/hi, type exec/read/write/any-IB, data value+mask, one register qualifier (A/X/Y/S/P, op ==,!=,<,>=), PREV_RET qualifier, one-shot, chain-AND to the other, PASS 16 bit, HITS 32 bit, action stop/count/log | units 2-5; raster comparator (a watch unit source); `n`/`ret`/`until` take one unit while running |
| Drive | 1 exec comparator (16-bit address), step, halt | 2 full drive units |
| Flow stack | — | software: frames from the trace (instruction records carry an "interrupt entered" flag) or by soft stops; `focus`/`sf`/`nf` are soft (they run while paused anyway) |
| Session scratch 2 KB RAMB18 | — | a firmware-owned 2 KB buffer in the app's DDR2 (`trx->scratch`) |
| Trace FIFO + client 3 + high window | — | engine stream 3 + `COPY` |

### 7.3 Write-log taps (T20)

C64 RAM: port A writes with the read-first old byte (F6), port B writes flagged external;
colour RAM tap (patch 0051); drive RAM in a RAMB18 read-first with write-through (T20
§4.2, 0.5 tile); cart RAM on the cartridge model's memory port (read-before-write); EEPROM
writes from the 93C86 model; flash changes by `LOG_PUT` from software. Record formers
≈ 120 LUT total.

### 7.4 Cartridge write-back (T16.2), minimum hardware

User decision 2026-10-01: the cartridge model becomes icepi's own (`cores/icepi/
c64_cartridge.v` with `c64_flash_amd.v` and `c64_eeprom_93c86.v`; the swap is done
separately), and write-back is the app's job, driven by interrupt and timer. The hardware
gives only what software cannot do itself:

| Part | Kept | Why it cannot be software | Cut (and what replaces it) |
|---|---|---|---|
| Flash responder | `c64_flash_amd` as vendored, config inputs (`flash_magic1/2`, `mask`, `id_*`, `enable`) as **write-only FF registers** | autoselect/status must be answered inside the C64 bus cycle (CWBI §7.1.4) | T16.2's chip records ×4, half/linear address modes: the swapped model has its own per-type address case |
| Pending operation | the responder's own `evt_cmd/chip/addr/data` (one operation; the chip stays BUSY until `ack`), latched on `flash_evt_pulse`, `ACK` strobe (toggle into `gclk`, applied at the next open edge) | the C64 waits on status bits | the 16-entry event queue: with one pending operation per board the queue only ever held OBSERVE/NOTIFY; OBSERVE (unlock on an unknown pair) is a profiling aid → the `cartwb.u2a` probe (T16.2 §8.4) |
| Applying program/erase | — | — | the hardware executor: the ISR writes the image in DDR2 with CPU stores (AND for program, `$FF` for erase), then ACKs. Then one `CART_INVAL` strobe (all lines of `cart_fill`'s line cache, which the swap keeps). |
| EEPROM | `c64_eeprom_93c86` with its array (RAMB18, Gideon window 0x1004C000, `CAPAB_EEPROM`) | the 93C86 applies its writes itself during C64 bus cycles | — |
| Change signals | `WR_COUNT` 16 bit (+1 per responder event and per EEPROM write: it doubles as the EEPROM seqlock), Gideon's EEPROM dirty byte, IRQ cause bits "flash op pending" and "EEPROM written" | an EEPROM write is never seen by software otherwise | dirty bitmaps in hardware (2 × 769 bits live + latch): the ISR applies every flash change itself, so its software map is exact; `GEN/QUIET_MS/QUIET_THRESH/CNT_*`: software (ISR counts, FreeRTOS tick for the quiet time) |
| Consistency during the copy | — | — | `HOLD`: the app copies dirty blocks before it ACKs the next operation (the C64 cannot start another one before), and the EEPROM copy is checked against `WR_COUNT` (seqlock) |
| Snapshot state | responder states, pending op, 93C86 shift state, mapper registers | | readback registers (`FSM`, `BANK`, `MODE`, `CART_TYPE`) → state-bus chip "cart" (§5.4) |

T20 coupling: flash changes are made by CPU stores, so no hardware tap sees them; the ISR
pushes one stream-1 record per program and one range record per erase through the
`LOG_PUT` register (§10), and asks the engine to `COPY` the erased sector's old bytes into
the ring first (T16.2 open question 12 answered: range record + old sector). EEPROM writes
are tapped in hardware (stream 1). Cart RAM writes by the C64 go through the swapped
model's memory port; that port gets the stream-1 tap (§7.3).

### 7.5 Video: pixel layer + still capture (T12 P3/P4, T24)

- **Pixel layer** scanout from DDR2 over its own `mig_arb` client "v" (hard deadline, so
  not inside the engine), **RGB565 only** (decision 2026-10-02, replaces ARGB8888: half
  the DDR2 reads, no per-pixel alpha multiply), 960 × 540 at scale 2 (scale 1-3), colour
  key + global alpha, page flip at frame start, vsync status; blend `bg + a·(fg − bg)` in 3
  DSP48 (T12 §5.3). Two 1,920 B line buffers in one 256 × 128 BRAM (2 RAMB36).
- **Still capture** packer on `pix_color/pix_x/pix_y` into two 54,000 B slots in DDR2
  (T12 §5.2 capture side, same client). **No still layer and no text layer in hardware**:
  the RISC-V draws the dimmed still and the C64 text screen into the pixel layer (§9.2).
- **As built (branch pixel-layer, `pix_layer.v`, 2026-10-01).** Register map in the
  module header (page 0x6xxx below; `PX_ABASE` and T12's own `PX_IRQ_EN/STAT`
  dropped: the two causes are `IRQ_CAUSE` b6 vsync / b7 flip done in page 0). Scale 1-3
  by repeat counters, W ≤ 960, any 16-byte aligned address and stride. Line buffers
  one 256 × 128 BRAM (2 RAMB36). RGB565 since 2026-10-02 (branch pix565): 565 widened to
  888 by bit replication, `PX_KEY` (one RGB565 value, transparent), global alpha in 3
  multipliers (one per channel), exact at 0 and 255; 9 pipeline stages. Priority in `mig_arb`: `cart_fill`, drive,
  then video and the RISC-V/REU pair in turn, video before the pair once a line fetch is
  5.3 µs old (`v_urgent`); video reads in flight ≤ 8 (`V_READS`),
  since cart and drive reads queued behind them in MIG wait for them. A line not
  fetched when it starts is shown transparent in full and counted (`PX_STATUS`), never
  in part. Still capture built (cheap: ≈ 500 FF of CDC buffers): `STILL_BASE` default
  0x5050000 is outside the app's 64 MB, so the app points it into its own memory to
  read the slots. Bench: `designs/c64_basic/sim/pixlayer`.

### 7.6 Freeze session responder (T12 §3, trimmed)

| Part | Kept | Cut (and what replaces it) |
|---|---|---|
| Session start/end detection (`halt_req && ultimax`, `SH_MODE`) | yes | — |
| PLA decode copy (`c64_bus_arbiter.v:474`) | yes (≈ 40 LUT) | — |
| Session copy (4,200 clk) into VIC/CIA files + scratch RAMB36 | — | reads fall through to the **state bus** (zero cycles, side-effect free); only written registers are held in an **overlay**: 128 × 8 LUTRAM (VIC 64, CIA1 16, CIA2 16, SID 32) + 128 valid FFs cleared in one clock at session end |
| Scratch RAMB36 for $0400-$0FFF + colour | — | writes go to the real RAM/colour RAM; the engine copies those 4 KB to DDR2 at session start (`RAM_OUT`, 0.13 ms) and back at session end (`RAM_IN`); same bit-identity, 0 BRAM |
| Commit on reset (dirty VIC replay as bus DMA) | yes, shorter: replay the overlay's valid VIC entries as bus DMA (T12 §3.6) | — |
| VIC read formatting (raster from live counters, constant OR masks) | yes (≈ 30 LUT) | patch 0051's side-effect-free copy of kawari's read case (≈ 250 LUT): the raw registers come from the state bus |
| CIA1 keyboard matrix + joystick merge on overlay PA/PB | yes (≈ 60 LUT, T12 §3.4) | — |
| `c64_text` fetcher, line buffers, palette | — | software renderer (§9.2) |
| Digest CRC-32 | — | software (RISC-V reads the state bus and RAM; test mode only) |

### 7.7 One IO block, one JTAG page, one interrupt

T20 (0x101A0000 TW), T16 (0x101B0000 slot), T16.2 (0x101B0000 CW), T21 (0x101C0000 TRXD +
0x101D0000 window) and T12 (0x101E0000 TV) each planned an IO bridge (T21: 150 LUT / 120
FF each). Here: **one bridge** for 0x101A0000-0x101AFFFF (pages §10), one JTAG mirror page
(`dbg_addr[23:12] = 0x101`), and **one ITU line** with a cause register (`irq_high(2)` since T28, F10).
Unmapped pages read 0; on Gideon's bitstream the whole block reads 0 (`ID` = 0 → absent).

### 7.8 ROMs: repack, not DDR2

- **DDR2 + cache, rejected.** The VIC reads the char ROM in its own half-cycle, every
  cycle of a text line; a miss cannot be waited for without stretching the cycle. KERNAL
  and BASIC are CPU-only, but a miss would either stall the CPU alone (the F8 problem:
  machine timing depends on DDR2 latency) or the whole machine (`vic_stall`: every miss leaves the VIC behind the scaler's locked position, F4; the cartridge swap avoids stalls for the same reason, §14 item 6). A cache
  that never misses is the ROM itself.
- **Repack (accepted).** The 16 K × 16 array holds 20 KB in 32 KB (F7). Three arrays:
  char 2 K × 16 (1 RAMB36), KERNAL 4 K × 16 (2), BASIC 4 K × 16 (2), selected by
  `waddr[13:12]` of the word address (≈ 16 LUT): **8 → 5 tiles**. The ROM port B for the session responder and
  T21's `rom` lens comes with the same change (`c64_mem.v`, ours, no patch).
- Optional, not counted: the scaler ring addressed as `line·400 + x` (400 of 512 columns
  used) fits 3 RAMB36 instead of 4 (≈ 30 LUT for the address).

---

## 8. How each feature uses it

### 8.1 T20 (ring, dump, restore, rewind)

| T20 need | Here |
|---|---|
| per-frame chip block (§4.1 "snap, not pause") | one-clock shadow capture at the snap edge, copied by the sequencer during the next frame (§4, §6.3); stream 2, positioned by the frame markers in streams 0/1 |
| write log streams A/B (§4.2) | taps §7.3 → engine stream 0/1; cart flash by `LOG_PUT` (§7.4) |
| keyframes with fix-up (§4.3) | `RAM_OUT` while running; the fix-up (undo of the copy window's records onto the copy) in software |
| index, retention, marks, cut, discard (§4.4-4.5) | software (`.u2a` timewarp) |
| restore sequence (§8.1) | pause at the snap point (§7.1) → register replay (§5.6 step 1) → `APPLY` (undo or keyframe + redo) → `SB_IN` + `sb_load` → caches invalidated by the load → resume with `RESUME_PHASE` (§7.1) |
| dump `.c64re`, ring save, load (§9-10) | software; state words named by the generated chain map (§5.5) |
| input log + replay (§7) | input sampler stays (≈ 40 LUT, stream 2); replay mux at the core boundary (≈ 60 LUT) |
| paused-frame buffer (§5.1, §11.3) | still capture (§7.5) + software draw into the pixel layer |
| TW register block, mover, client "r" | page 1/2 of the one IO block (§10), engine |

### 8.2 T21 (monitor)

| T21 need | Here |
|---|---|
| CPU state port §5.4 (read any time paused, write at IB) | state-bus chip 1; writes staged, applied at the next opening edge |
| `SET_PC` redirect | staging PC + state := fetch, then one cycle (T21 §3.3 unchanged) |
| PAUSE_CB/IB, STEP_*, RUN_CYCLES, counters | §7.1 |
| `bk` (any number) | exec bitmap |
| watches, `n`, `ret`, `until`, `rb`, observers | 2 watch units + soft conditions (T21 §4.4.1 level 2 for everything else) |
| side-effect-free `io`, `reu`, `pot`, `io 1541`, colour RAM | state bus and colour port, from phase 1 (no bus-cycle phase) |
| flow stack, `focus`, `sf`, `nf`, `bt` frames | software (trace records or soft stops) |
| trace ring, `chis`, `whowrote`, `triage`, `tracering` | engine stream 3 |
| drive: state port, step, breakpoint | drive state bus, 1 exec comparator, step |
| `screen`, picture while paused (Q2) | software renderer into the pixel layer (§9.2) |
| `RUN_NOALIGN` (Q4) | still open in T21: every resume waits for whole-frame alignment (≤ 20 ms) |
| JTAG `dbg` verbs, IO block 0x101C0000 | the one IO block / JTAG page |

### 8.3 T12 (freeze on the pause)

| T12 need | Here |
|---|---|
| zero `gclk` edges, bit-identical (§1.3 req. 1) | session responder (§7.6): reads through the state bus, writes into the overlay or into RAM backed up by the engine |
| Gideon's app unmodified (req. 3) | the responder answers its DMA-window accesses exactly as T12 §3.2-3.4 specify; nothing in the app changes |
| menu in the HDMI box (req. 2) | software renderer in **our** firmware (trex/main, a module of ours beside the app, not a change to Gideon's freeze code): reads $0400 / colour / charset / $D018 through the DMA window, which the responder serves, and draws into the pixel layer (§9.2) |
| dimmed still (P3) | still capture (§7.5) + renderer |
| pixel layer (P4) | §7.5 |
| peek port (patch 0051) | state bus (patch 0051 shrinks to the colour RAM second port) |
| digest | software, test mode |

### 8.4 T16.2 (write-back)

Everything in T16.2 §7-§8 (service, profiles, triggers, journal, conflicts, power-off, REST,
`cartwb.u2a`) stays software as specified; its hardware is §7.4 here. The ISR now also
applies program/erase with CPU stores and keeps the dirty map.

---

## 9. Software split

### 9.1 Firmware (trex/main) vs `.u2a`

| Firmware (minimal, needs an interrupt or must survive app returns) | `.u2a` |
|---|---|
| TRX block detection (`ID` at 0x101A0000), one ITU handler dispatching by `IRQ_CAUSE` | timewarp: transport, index, marks, `.c64re` / `.c64rering` / `.trxring` |
| CW service: ISR (apply program/erase, ACK, dirty map, `LOG_PUT`), quiet timer, write-back (T16.2 §7) | trxmon: all verbs, assembler, observers, flow frames from trace |
| freeze renderer (§9.2) and session status | cartwb inspector |
| `trx` ABI table: ownership, `wait_stop`, `scratch` (2 KB), engine job submit/wait, state-bus word access helpers | |

### 9.2 Renderer (C64 text and still into the pixel layer)

- Rules of T12 §4.1 (TRX64 `render.rs`: mode from $D011/$D016, screen/char base from $D018
  and the CIA2 pins `PRA | ~DDRA`, char ROM in banks 0/2) **[TRX64: crates/trx64-core/src/
  render.rs, c64re_snapshot.rs:2084]**; standard, multicolour, ECM text; other modes fill
  with $D021.
- Output 320 × 200 at 2 × 2 into the RGB565 layer (box 1280 × 800 on screen, as
  T12 §4.4), over the still drawn at 2 × 2 and halved (T12's dim).
- Cost: one full redraw reads ≈ 4,000 bytes through the DMA window (≈ 1-2 ms) and writes
  256 k pixels (≈ 1 MB, order 10 ms at the RISC-V's store rate); afterwards only changed
  cells (the renderer keeps the last screen/colour copy). The still is drawn once per freeze.
- Show/hide rule: T12 §4.5's `show_latch` sequence, evaluated in software on the overlay
  contents read back from the responder (page 0x5xxx, §10).

---

## 10. Register map: one IO block 0x101A0000-0x101AFFFF

(As built: offsets inside the pages and the JTAG page in §15.3.)

Byte-wide IO bus, toggle handshake into `sys_clk`, 32-bit registers little endian, T21 §5.1
rules (reading byte 0 latches, writing byte 3 commits). 0x101A0000-0x101AFFFF is unused
by Gideon's app (its highest region is `U2P_IO_BASE + 0x90000` = 0x10190000; +0xB0000 and
+0xC0000 are used only by his separate factory-tester bitstream).

**JTAG.** Mirror at `dbg_addr` 0x101000-0x101FFF: one 4 KB page at a time, chosen by
`JTAG_PAGE` in page 0. `top.v` today decodes the whole 0x1xxxxx range as debug registers
(`dbg_is_reg <= dbg_addr[23:20] == 4'h1`, read value by `case (dbg_addr[3:0])` plus
`dbg_addr[7:4]` overrides), so 0x101xxx would alias the existing registers on reads. The
decode becomes:

- `dbg_is_reg <= dbg_addr[23:12] == 12'h100` (the existing registers, 0x100000-0x100FFF);
  within it the read mux also requires `dbg_addr[11:8] == 0` and decodes `dbg_addr[7:4]`
  for every group (0 general, 1 USB, 2 drive/IEC, 3 T16 slot), so no register answers at an
  address it does not own (writes already compare the full 24-bit address);
- `dbg_is_trx <= dbg_addr[23:12] == 12'h101`: reads and writes go to the TRX bridge
  (`JTAG_PAGE` + `dbg_addr[11:0]`), its data joins the `rdata` mux in `top.v:761`;
- 0x102000-0x1FFFFF reads 0.

| Page (4 KB) | Content |
|---|---|
| 0x0xxx | `ID` 'TRX2', `CAPS`, `JTAG_PAGE`, `IRQ_EN`, `IRQ_CAUSE` (W1C: stop, engine job done, log gap, capture done, flash op pending, EEPROM written, pixel-layer vsync/flip), owner byte (T21 §5.2), gate control, `RESUME_PHASE`, capture control (`CAPTURE_EVERY`, `CAP_GEN`, skipped/torn counts), counters (cycles 48, instructions 32, drive cycles 32), `CYC_CMP` |
| 0x1xxx | state bus: `SB_ADDR`, `SB_DATA` (read = word at `SB_ADDR`, write = staging), `SB_LOAD` (chip mask), `SB_COMMIT_NOW`, `SB_STATUS` (frozen, error); auto-increment of the word index |
| 0x2xxx | engine: `ENG_CMD`, `ENG_SRC/DST/LEN/ARG`, `ENG_STATUS`; log areas `LOG_BASE/SIZE/WR/DROPPED[0..3]`, `LOG_CTRL` (taps on/off, external writes, I/O writes, input replay), `LOG_PUT` (64-bit record into stream 1), capture table (64 entries), `CAPTURE_EVERY`, `KEYFRAME` helpers |
| 0x3xxx | debug: stop record (T21 §5.3 minus flow fields), exec bitmap port (`BMP_IDX/DATA`), watch unit 0/1 (T21 §5.6 layout minus bank qualifier), drive comparator, step commands |
| 0x4xxx | cartridge: flash config (write-only), pending event (cmd, chip, addr, data), `ACK`, `CART_INVAL`, `WR_COUNT` |
| 0x5xxx | freeze session: `SH_CTRL`, `SH_STATUS`, `SH_ACCESSES`, `SH_REAL_CYCLES`, `SH_GCLK`, `SH_UNMAPPED`, overlay readback (128 bytes + valid) |
| 0x6xxx | video: pixel layer (T12 §6.4 minus formats 1-2), still capture (`STILL_CTRL/BASE/STATUS`); as built: `pix_layer.v` header |
| 0x7xxx | T16 slot registers (T16 §7.2's five at 0x101A7000; moved there from T16's 0x101B0000) |

---

## 11. Resource budget (estimates; baseline m10)

State-bus rows use the cost model of §5.3 on the bit counts of §5.4: read 0.30 LUT + 1 FF
(shadow) per R bit, write 0.55 LUT + 1 FF (staging) per W bit, 15 LUT per chip. Other rows
are sized from the RTL they replace in the old specs, reduced as stated. Uncertainty
±25 %; phase A1 measures CPU + CIA and rescales the rest before A2 starts.

| Part | LUT | FF | BRAM tiles | DSP |
|---|---|---|---|---|
| state bus: glue | 34 | 52 | | |
| 6510 | 156 | 344 | | |
| VIC (+ snap condition 20, `char_buf` second port + restore clock 40) | 602 | 1,320 | | |
| CIA ×2 (registers now shadowed and in the mux) | 427 | 1,035 | | |
| SID ×2 (SRL rings → FF: −146 LUT, +1,040 FF) | 431 | 2,500 | | |
| REU | 88 | 189 | | |
| cartridge (icepi) | 129 | 280 | | |
| colour RAM port B (patch 0051) | 10 | | 0 (existing RAMB18) | |
| drive: CPU, VIAs, floppy, timing + quasi-static bridge + complete halt — **first to drop if space runs out** (§14 item 7) | 561 | 1,070 | | |
| bus fabric (OR tree, pipeline, staging decode) | 120 | 50 | | |
| **state bus subtotal** (of it 3,460 FF shadows, 2,280 FF staging) | **2,558** | **6,840** | **0** | |
| gate: IB/cycle stops, step, counters 48/32/32, `CYC_CMP`, `RESUME_PHASE`, capture control | 280 | 240 | | |
| debug: exec bitmap, 2 watch units, drive comparator, stop record, trace former | 360 | 400 | 2 | |
| engine: log FIFO + drain, mover, capture sequencer, MIG client "e" | 490 | 500 | 1 | |
| write-log taps + frame markers + input log + replay mux + `LOG_PUT` | 255 | 200 | 0.5 (drive RAM) | |
| freeze session responder | 260 | 250 | | |
| video: client "v", pixel layer RGB565 + key, global alpha, still capture | 500 | 560 | 2 | 3 |
| cartridge write-back registers (§7.4) | 40 | 130 | | |
| flash responder + 93C86 (part of the icepi swap; net vs `cart_slot`/`cart_logic` to be measured there) | ≈ 250 | ≈ 200 | 0.5 (EEPROM) | |
| one IO block + JTAG page + IRQ cause | 150 | 150 | | |
| ROM repack | 16 | | −3 | |
| **new total** | **≈ 5,160** | **≈ 9,410** | **+3** | **+3** |
| T16 slot port (T16 §11, unchanged) | 300 | 250 | | |

| | LUT | FF | BRAM | DSP |
|---|---|---|---|---|
| today (m10) | 19,793 (60.7 %) | 18,761 (28.8 %) | 54 / 75 | 10 |
| + this design | **24,960 (76.5 %)** | 28,170 (43.2 %) | **57 (18 free)** | 13 |
| + this design without the exact drive | 24,400 (74.8 %) | 27,100 (41.6 %) | 57 | 13 |
| + this design + T16 | **25,260 (77.5 %)** | 28,420 (43.6 %) | 57 | 13 |
| at +25 % on all estimates (without / with T16) | 26,250 (80.5 %) / 26,620 (81.7 %) | | | |
| first version of ARCH (servo, per-frame stop) | 24,870 (76.3 %) | 24,710 (37.9 %) | 57 | 13 |
| old plan (T20 + T21 + T12 + T16.2 CW) | 29,900-32,300 (92-99 %) | 27,800 | 69 | 13 |

**Delta of the shadow capture to the first version:** LUT +39 (servo −110, `RESUME_PHASE` +30, capture
control +30, CIA/REU registers into the shadowed mux +89); FF +3,450 (3,460 shadows,
servo −70, new control +60); BRAM and DSP unchanged. FF stay below 45 %, ≈ 36,800 free. The §14 decisions add ≈ +40 LUT, +10 FF (`char_buf`).

Where the LUTs went compared with the old specs:

| Old | Old LUT | Here | LUT |
|---|---|---|---|
| T20 shadow chains (4,600 state bits × 1-1.5) | 4,600-7,000 | state bus with CE shadows (R 3,460 × 0.3, W 2,280 × 0.55, `char_buf`) | 2,558 |
| T21 trace client + FIFO + high window (400), T20 ring engine + mover + client "r" (not itemized in T20) | ≥ 400 | engine | 490 |
| T21 6 comparators + access bitmaps + raster cmp + flow stack + drive debug | ≈ 1,260 | 2 watch units + 1 drive comparator (+ bitmap) | 360 |
| T12 peek 250 + shadow files/copy 450 + digest 60 + `c64_text` 450 + still layer 400 | 1,610 | responder 260 + still capture (in video) | ≈ 380 |
| T20 TW + T21 TRXD + T12 TV + T16.2 CW bridges | ≈ 600 | one bridge | 150 |
| T16.2 chip records, queue, executor, dirty maps, counters | ≈ 550 | config + event latch + counter | 40 |

---

## 12. Build order (every phase: simulation first, then the board)

Verilator/iverilog benches under `designs/c64_basic/sim/`, at most one at a time, niced.
Board steps are Alex's.

| Phase | Content | Simulation proof | Board proof |
|---|---|---|---|
| A0 | ROM repack (§7.8: −3 tiles); `RESUME_PHASE` in the gate | `sim/pause` with a non-zero `RESUME_PHASE`: frame lock kept | Lorenz suite unchanged; picture locked after a pause |
| A1 | One IO block + JTAG page + the borrowed ITU line; state bus skeleton with shadows and capture control, chips glue (patch 0066), CPU (0060), CIA ×2 (0063); IB stop, steps, counters | `tb_sb`: after a stop every shadow word == the same value through the bus path where one exists; a running capture equals a stop at the same edge; staged write + load at the next edge changes exactly the written FFs; a snap/load identity run (T20 §13.1-2) for CPU+CIA | `trxdbg.py sb dump` on a running program |
| A2 | Remaining chips: VIC (patch 0061), SID (0062, SRL→FF for voice state), REU (0064), cart (after the icepi swap, 0065), colour RAM port (0051), drive (Gideon patches 0003 and 0004, quasi-static bridge); chain-map generator (§5.5); completeness gate (T20 §13.1-1) | T20 §13.1-3 round trip with the capture table, scenarios (i)-(v) | — |
| A3 | Engine: log FIFO + drain, `COPY`, `RAM_IN/OUT`, `APPLY`, `SB_IN/OUT`, `FILL`; capture sequencer; write-log taps; input log | T20 §13.1-4/5/6; capture every frame for 1,000 frames: blocks equal a reference run that stops at the same edges; `gclk` never gated by a capture; **torn-block sweep** (deterministic): a stop is requested at every `clk` offset 0…N from the capture edge, one run per offset over the same program; the bench counts torn/skipped blocks per offset and checks that no block is ever mixed — the result is the exact torn window in clocks | per-frame write counts (T20 §13.2-1); torn/skipped counters over a 1-hour T21 session with random stops (how often it matters in practice) |
| A4 | Debug: exec bitmap, 2 watch units, drive comparator, trace former (stream 3) | T21 §9.1 tests 1-11 with the reduced unit count | T21 §9.3 |
| A5 | Freeze responder (overlay, keyboard, PLA copy, backup through the engine) | T12 §8.1 tests 1-7, 10 (digest in the bench) | T12 §8.2 1-7 with the menu on Gideon's C64 screen path (renderer in A6) |
| A6 | Video client "v": still capture, pixel layer; firmware renderer | T12 §8.1-11/12 | menu in the box over the dimmed still |
| A7 | CW minimum (§7.4) on the swapped icepi cartridge | T16.2 §10.2 tests 1-4, 6-8, 11-12 (5, 9, 10 dropped with OBSERVE, hardware dirty map and HOLD) | T16.2 §10.4 |
| A8 | Software: timewarp, trxmon, cartwb `.u2a`; CW service in firmware | host tests, ue2emu where models exist | T20 §13.2, T21 §9.3, T16.2 §10.4 |

A0 is independent of the rest and can go first; A1 gives T21 phase 1 its CPU registers and
T12 its side-effect-free reads.

---

## 13. Patches to vendored cores

All as patch files under `tools/patches/`, applied by the vendor scripts. Pattern for every
W register, so the patches stay mechanical and small: new ports `sb_*`, a generated include
with the word mux and the staging words, and **one line per register at the end of its
clocked block**: `if (sb_load) r <= sb_stg_r;` (VHDL: `if sb_load = '1' then r <= stg_r; end
if;`), so no existing assignment changes.

| Patch | Core | Content | Size (est.) |
|---|---|---|---|
| icepi 0051-colour-ram-port-b | `c64_ram`/`c64_top` | colour RAM true dual port, port B on `clk`; write tap out | ≈ 30 lines |
| icepi 0060-cpu-state-bus | `cpu6510.v`, `6502n` (`proc_registers`, `proc_control`, `proc_interrupt`) | R/W words of §5.4 chip 1; taps `sync`, `intg_i`, `i_reg`, address/data/R-W/RDY for stops and watch units; `fsm_encoding` fixed on `state` | ≈ 150 |
| icepi 0061-vic-state-bus | kawari `vicii.v`, `sprites.v`, `raster.v`, `border.v`, `cycles.v`, `matrix.v`, `bus_access.v`, `addressgen_spartan.v`, `pixel_sequencer.v`, `registers_scaled.v` | chip 2 R/W; snap-condition outputs (border, sprite activity, VIC access in the last cycle) | ≈ 250 |
| icepi 0062-sid-state-bus | reDIP `sid_control.sv`, `sid_waveform.sv`, `sid_envelope.sv`, `sid_pot.sv`, `sid_api.sv`, `c64_redip_sid.v` | `shreg_extract = "no"` and no retiming on the voice rings; chip 5 R/W | ≈ 150 |
| icepi 0063-cia-state-bus | reDIP `cia_*.sv`, `c64_redip_cia.v` | chips 3/4 R/W; register read with the state-bus address and no strobe | ≈ 150 |
| icepi 0064-reu-state-bus | `c64_reu.v` | chip 6 | ≈ 40 |
| icepi 0065-cart-state-bus | `c64_cartridge.v`, `c64_flash_amd.v`, `c64_eeprom_93c86.v` (after the swap patch) | chip 7; cart RAM write tap | ≈ 60 |
| icepi 0066-top-state-bus | `c64_top.v`, `c64_bus_arbiter.v` | chip 0; routes the `sb_*` fabric and taps out of `c64_top` | ≈ 80 |
| gideon 0003-drive-halt-complete | `floppy_stream.vhd`, `via6522.vhd`, `drive_registers.vhd` | gate `p_clock_div`/`bit_tick`, VIA edge capture/IFR OR and `write_busy` with `halt` (inventory: today the disk keeps turning and VIA edges still set IFR while halted) | ≈ 20 |
| gideon 0004-drive-state-bus | `drv_` 6502N copy, `via6522.vhd`, `floppy_stream.vhd`, `floppy_mem.vhd`, `c1541_timing.vhd`, `cpu_part_1541.vhd` | chips 9-12 R/W | ≈ 200 |

Ours, not patches: `c64_core.v` (`RESUME_PHASE`, capture control, stops, counters, fabric),
`top.v` (engine, video client, IO block), `c64_mem.v` (ROM repack, ROM
port B), `drive_unit.vhd` (bridge), `ultimate_sys.vhd` (one region), `mig_arb` (clients "e",
"v"); new `trx_engine.v`, `trx_io.v`, `trx_dbg.v`, `freeze_resp.v`, `pix_layer.v`.

**T16 (decided 2026-10-01):** T16's slot wiring is icepi patch **0070** (was 0060, which is
the CPU state bus here), and its app-side slot registers are page 0x7xxx of the TRX block
(0x101A7000, was 0x101B0000); T16 is updated accordingly.

---

## 14. Decisions (2026-10-01; formerly open questions)

1. **Interrupt line:** `irq_in(4)` (CMDIF, the UCI) was borrowed until T28; since T28 the IO
   block's line is ITU `irq_high(2)` (drive B's, absent here) and `irq_in(4)` is the UCI's
   again (T28 §4.3).
2. **Torn blocks:** accepted, no double buffer. §12 A3 adds a deterministic torn-block sweep
   in simulation and a board counter to measure whether and how often it matters.
3. **SID voices 1-2 audio-only state:** not restored (saves ≈ 780 LUT); those voices resume
   from the live envelopes after a rewind.
4. **VIC `char_buf`:** restored (second LUTRAM read port + restore clock on its write port,
   ≈ 40 LUT).
5. **93C86 array:** block RAM (RAMB18).
6. **Cartridge swap:** done by the cart agent; it keeps `cart_fill`'s line cache with a
   fixed answer time and never stalls the machine on DDR2.
7. **Drive:** exact restore (≈ 560 LUT, 1,070 FF) is built, and is the **first candidate to
   drop** if space runs out (then "reset + remount", partial fidelity).
8. **REU:** no determinism work; frames with REU activity are flagged "not exact" in the
   ring (restore stays exact, a re-run of that frame may differ).
9. **IO addresses:** 0x101A0000-0x101AFFFF is free in Gideon's app; JTAG page 0x101 with
   the tighter decode of §10.
10. **T16:** slot wiring renumbered to icepi patch 0070, slot registers in page 0x7xxx (§13).

No open questions remain in this document.

---

## Appendix: concepts copied from TRX64

| Here | From |
|---|---|
| two read lanes: side-effect-free peek vs. live read (state-bus reads, T12 responder) | `crates/trx64-core/src/vic.rs` `read_reg`/`read_reg_mut`, `ciacore.rs` `peek` |
| one state interface per component (`get_state`/`set_state`), chip blocks as the snapshot unit | `crates/trx64-core/src/cart.rs` `CartMapper`, `c64re_snapshot.rs` |
| restore of registers by replay, coarse VIC cut as the fallback for foreign files | `crates/trx64-core/src/c64re_snapshot.rs` |
| write record `{addr, old, new}`, undo contract (log streams) | `crates/trx64-core/src/delta_ring.rs` |
| cadence, window, checkpoint ring policy (software side) | `crates/trx64-core/src/checkpoint_ring.rs`, `MONITOR.md` |
| self-enumerating completeness gate (chain map) | TRX64 snapshot fidelity tests, `crates/trx64-core/tests/snapshot_roundtrip_fidelity.rs` |
| text rendering rules for the software renderer | `crates/trx64-core/src/render.rs` |
| "synthesis state vs. PCM transport" (why audio-only state may be left out) | `docs/concepts-snapshots-scenarios-overlays.md` §0 |

---

## 15. As built (branch arch-p1, 2026-10-01/02)

What exists in RTL and simulation; the board build is the main session's. Code is the
reference: `designs/c64_basic/c64_core.v` (state bus, capture control), `trx_sys.v`
(pages in the C64 clock), `trx_io.v` (pages in the app's clock, forwarding),
`freeze_resp.v` (DMA window and session responder), `top.v` (JTAG decode, gate).

### 15.1 Against the phases (§12)

| Phase | Built | Not yet |
|---|---|---|
| A0 | — | ROM repack (only the ROM block's second port is in, `c64_mem.v` `dbg_rom`), `RESUME_PHASE` |
| A1 | one IO block (pages 0/1/4/5) + JTAG page + `irq_in(4)` (cause bits b0 stop, b3 capture); state bus with shadows for glue (chip 0), 6510 (1: A X Y S P PC, opcode-fetch cycle, $00, $01 latch, $01 as read), CIA ×2 (3/4: the 16 registers as a read returns them, port pins); capture control (`CAP_EVERY`, `CAP_GEN`, frames without a snap) | the 6510's mid-instruction state and at-`sync` bits, CIA hidden state (timer latches, TOD alarm, ICR mask, serial, delays); staged writes and `sb_load`; IB stop, steps, counters |
| A2 | VIC registers $D000-$D02F as read (chip 2), colour RAM second port, cartridge banking registers `{reg_1, reg_0}` (chip 7) | VIC internals (sprite/position/border state, `char_buf`), SID, REU, flash responder/93C86 state, drive; chain-map generator; completeness gate |
| A3 | — | engine, log taps, capture sequencer (the shadows are read by software over the bus) |
| A5 | freeze session responder (§7.6): detection, PLA copy, overlay 128 × 8 + valid bits, keyboard/joysticks, VIC read formatting, commit on reset, counters | the engine backup of $0400-$0FFF/colour RAM (decision 15.2-2) |

### 15.2 Decisions made while building

1. **Snap condition:** armed at line 16 (every frame with the log on, every `CAP_EVERY`
   frames for a capture), snapped at the first phi2 falling edge on line 0 from x 96
   (cycle 12) that ends an opcode fetch (6502N `sync`) while kawari's border flip-flops
   and their 5-dot delays are set, no sprite pixel is in the sprite delay pipeline
   (`sprites.v` `sprite_quiet`), the CPU half that ends did not select the VIC
   (`c64_top.v` `vic_acc`) and the REU is idle (branch t20-ring, M-c). A fetch that an
   interrupt drops (6502N `intg`) is a valid snap: the boundary before the IRQ/NMI
   sequence with the interrupt pending, as T20 §4.1 has it; the marker says so (b4).
   The edge is the one a stop would stop on, so a running capture equals a stop on
   that edge (sim/statebus test 2). No snap by line 16 → counted, no capture; the
   frame's boundary marker is then at line 16 (b0 = 0).
2. **No engine backup for the freeze session:** the session writes $0400-$0FFF and colour
   RAM for real; the app's own `restore_io()` puts them back from the backups it read in
   the session, which came from the real RAM, exactly as on Gideon's hardware. Bit
   identity holds for every freeze that ends with `restore_io()` (sim/statebus test 4). A
   reset from the menu leaves the menu's bytes in RAM, as on Gideon's hardware (the reset
   reinitializes anyway). The engine backup can be added with A3 without changing the
   responder's interface.
3. **Shadow words hold read images, not raw registers:** VIC and CIA words are what a CPU
   read would return (unused bits 1, raster in $D011/$D012, CIA ports as latched, TOD as a
   read shows it), built by wiring from the chips' own registers, so the image costs no
   LUT beyond the mux. Raw hidden state for restores comes with the W words of A1/A2.
4. **Overlay reads:** one LUT RAM read port for the responder (walking the entries a read
   needs, one per clock) and one for `trx_sys` (readback), instead of eight ports.
5. **The app's C64_STOP pauses only the CPU by default:** `GATE_CTRL` b0 (reset 0) makes it a
   machine pause with the session responder. Until our firmware draws the menu (T12 P2)
   the machine pause would hide Gideon's C64-screen menu; the firmware sets b0 once it
   draws it. JTAG register 9 bit 4 still forces CPU-only.
6. **JTAG page:** words, not bytes: 0x101000-0x1013FF = byte offset `{JTAG_PAGE, addr[9:0],
   00}` of the IO block; 0x101400 = `JTAG_PAGE` from any page (so JTAG can always get back to
   page 0); only the pages in the C64 clock (0 from 0x100, 1, 5; ID at 0) are reachable this
   way, pages 0 (below 0x100) and 4 live in the app's clock and read 0 over JTAG.
7. **32-bit registers over the byte bus:** writing byte 3 commits (bytes 0-2 collected),
   reading byte 0 latches (side effects such as `SB_DATA`'s increment happen there).

### 15.3 Register map as built

Page 0 (app's clock, `trx_io.v`): 000 `ID` "TRX2"; 004 `CAPS` 0xFF (b0 page 0 control, b1
state bus, b2 engine, b3 debug, b4 cartridge, b5 freeze session, b6 video, b7 the pixel layer is RGB565: format 1,
`PX_KEY`); 005 `CAPS2` 0x03 (b0 the engine's drain writes stream 2's F4 only after an F3
marker, T20 §16.6; older bitstreams read 0; b1 `BUILD` is there); 008-00B `BUILD` the
bitstream's USR_ACCESS build stamp (T15 §4.2); 010 `IRQ_EN`, 014 `IRQ_CAUSE` (W1C):
b0 the machine stopped, b3 a capture loaded the shadows, b4 flash operation pending, b5
EEPROM written, b6 video frame start (vsync, 45 lines before the first visible line), b7
page flip done. Page 4: cartridge (T16.2 minimum, `trx_io.v` header).

Page 6 (app's clock, `pix_layer.v`; not reachable over JTAG, item 6): 32-bit registers,
byte 3 commits, byte 0 latches. Only STILL_STATUS and PX_STATUS read; every other register is write-only
and reads 0 (OPT-pix.md).

| Address | Name | Access | Content |
|---|---|---|---|
| 0x101A6060 | STILL_CTRL | W | b0 capture (reset 1) |
| 0x101A6064 | STILL_BASE | W | DDR2 byte address of slot 0 (16-byte aligned, reset 0x05050000); slot 1 at +0x10000 |
| 0x101A6068 | STILL_STATUS | R | b0 valid, b1 slot of the last complete frame, b2 that frame is 60 Hz (400 x 240; 0: 400 x 270; T12 §5.2), b31:8 frames captured |
| 0x101A6080 | PX_CTRL | W | b0 enable, b3:1 format (1 RGB565, little-endian, R 15:11 G 10:5 B 4:0; others: off), b5:4 scale (0/1 → 1, 2, 3); reset 0x22 |
| 0x101A6084 | PX_FRONT | W | DDR2 byte address of the shown buffer (16-byte aligned); a flip makes the shown buffer PX_BACK's (PX_FRONT does not follow) |
| 0x101A6088 | PX_BACK | W | show this buffer from the next frame start |
| 0x101A6090 | PX_SIZE | W | {height 16, width 16} (reset 540, 960; width ≤ 960) |
| 0x101A6094 | PX_STRIDE | W | bytes per line, multiple of 16 (reset 1920) |
| 0x101A6098 | PX_POS | W | {y0 16, x0 16} output pixel of the top left |
| 0x101A609C | PX_GALPHA | W | b7:0 global alpha (reset 255): `out = bg + a·(fg − bg)/256`, a = G + G[7] |
| 0x101A60A0 | PX_KEY | W | b15:0 colour key (RGB565), b16 key on: pixels equal to it are transparent (reset 0: off) |
| 0x101A60A4 | PX_STATUS | R | b0 flip pending, b1 vertical blank, b15:8 underrun lines (wrapping), b31:16 frames (wrapping) |
| 0x101A60C0 | TX_CTRL | RW | C64 text layer (`c64_text.v`, T12 §13): b0 enable, b1 opaque background; R b31:24 = 1 |
| 0x101A60C4 | TX_STATUS | R | b0 shown now, b15:8 lines not drawn for a late fetch (wrapping) |

Pages in the C64 clock (`trx_sys.v`):

| Address | Name | Access | Content |
|---|---|---|---|
| 0x101A0100 | GATE_CTRL | RW | b0 FRZ: the app's C64_STOP pauses the machine and opens sessions (reset 0 = CPU-only pause); R b8 halted, b9 gate shut |
| 0x101A0104 | CAP_CTRL | RW | b7:0 frame capture every n frames (0 = off) |
| 0x101A0108 | CAP_STAT | R | {frames without a snap (16), CAP_GEN (16)} |
| 0x101A010C | JTAG_PAGE | RW | b3:0 (also JTAG 0x101400) |
| 0x101A1000 | SB_ADDR | RW | b11:0 {chip, word} |
| 0x101A1004 | SB_DATA | R | the word; reading increments SB_ADDR. W: refused (no staging yet), sets SB_STATUS b1 |
| 0x101A1008 | SB_STATUS | R/W | b0 gate shut (shadows follow the machine), b1 write refused (W clears) |
| 0x101A5000 | SH_CTRL | RW | b1:0 SH_MODE (0 off, 1 auto (reset), 2 every pause); W b8 abort |
| 0x101A5004 | SH_STATUS | R | b0 session, b1 commit running, b2 show_latch (T12 §4.5, over the session's VIC writes), b31:16 sessions started |
| 0x101A5008 | SH_ACCESSES | R | bus accesses answered in sessions |
| 0x101A500C | SH_REAL | R | bus cycles the machine ran in sessions (commit only) |
| 0x101A5010 | SH_GCLK | R | C64 clock edges in sessions while paused |
| 0x101A5014 | SH_UNMAPPED | R | cartridge, IO1/IO2, open Ultimax ranges |
| 0x101A5080-50FF | overlay | R | 128 bytes: VIC 0-63, CIA 1 64-79, CIA 2 80-95, SID 96-127 |
| 0x101A5100-510F | valid | R | bit n = overlay byte n written in this session |

State bus words (chip `[11:7]`, word `[6:0]`; `c64_core.v` header): 0 glue `{CAP_GEN, 0,
held, cart present, EXROM, GAME, halted}`; 1 6510 w0 `{S,Y,X,A}`, w1 `{sync, P, PC}`, w2
`{$01 read, $01 latch, $00}`; 2 VIC w0-w11 = $D000-$D02F; 3/4 CIA w0-w3 = registers 0-15, w4
`{DDRB, DDRA, PB out, PA out}`; 7 cartridge w0 `{reg_1, reg_0}`.

### 15.4 Simulation

`sim/statebus` (new, `tb_sb_top` = `c64_core` + `freeze_resp` + `trx_sys`): capture in one
clock while running, the machine's clock never stops, equal to a stop on the same edge;
state bus reads cost no clock, ICR flag and timers untouched (a real bus read clears ICR,
the bus follows); VIC/CIA words equal bus reads; the app's freeze sequence in a session
(stop, backup_io, init_io, menu, keyboard/joysticks, dma_transfer_frozen ROM/RAM, restore_io,
resume): 0 C64 clock edges, 0 bus cycles, CRC over RAM + state + colour RAM equal before and
after, 5 frames later equal to a run without the freeze; a stop without Ultimax is real;
commit on reset; CPU-only default. `sim/pause` (state bus still while paused, follows DMA
cycles), `sim/dma_io` (ROM and colour RAM second ports equal bus reads), `sim/cart`,
`sim/cart_unit`: green.

Pixel layer, RGB565 (2026-10-02): `sim/pixlayer` checks every output pixel of 8 frames
exactly (565 → 888 by bit replication, global alpha 255/128, `PX_KEY` on and off with every
seventh source pixel the key's value, scale 1/2/3, flip, underrun under a `cart_fill` hog
shown never in part and counted, still slot = VIC window): PASS. `sim/lat` +vid with
120 reads per line: no late line. Timing (m11: WNS 0.015 ns on the pixel clock, the
ARGB path SRL → `fg − bg` → DSP48 B without M register): the pixel path is now 9 stages,
BRAM output register, lane mux, key compare, `fg − bg` into a register (DSP A), the
product in M and again in P, the add; no stage has more than one adder or one 16-bit
compare after a register, so a few hundred ps of slack are expected (to be confirmed by
the next Vivado run).

### 15.5 Resource estimate (not synthesized; Vivado runs in the main session)

| Part | LUT | FF |
|---|---|---|
| shadows (VIC ≈ 300 non-constant bits, CIA 2 × 160, 6510 81, cart 16, glue 3) | — | ≈ 720 |
| state bus mux (≈ 30 words × 32, many bits constant) + pipeline | ≈ 200 | ≈ 45 |
| capture control | ≈ 30 | ≈ 50 |
| freeze responder (PLA, FSM, overlay 2 ports, valid bits + 128:1 mux, keyboard, formatting, 5 counters) | ≈ 400 | ≈ 450 |
| `trx_sys` (registers, read mux, byte/JTAG ops) + `trx_io` forwarding + JTAG decode | ≈ 180 | ≈ 200 |
| colour RAM / ROM second ports | ≈ 10 | — |
| **sum** | **≈ 820** | **≈ 1,470** |

Against §11 for the same parts (state bus 6510/VIC/CIA read side + responder + bridge share):
lower, because no staging/load muxes are built yet.

### 15.6 Open

- Staged writes and `sb_load` (restore, T20/T21), the W bits of §5.4, hidden CIA/6510 state.
- Engine (A3) and with it the $0400-$0FFF/colour RAM backup, the capture sequencer, log taps.
- `RESUME_PHASE`, IB stop/steps/counters (A0/A1). (The full snap condition is in, §15.2-1.)
- SID, REU, drive, VIC internals on the bus (A2); chain map and completeness gate.
- JTAG reach of the pages in the app's clock (0 below 0x100, 4).
- The freeze renderer and setting `GATE_CTRL` b0 in firmware (T12 P2, follow-up).

### 15.7 Engine, frame ring (branch t20-ring, 2026-10-02)

`designs/c64_basic/trx_engine.v` (its header has the record formats and page 2's map):
one `mig_arb` client "e" (after `cart_fill`, the drive and an urgent video client, before
the rest; reads on `c_rd_data`), page 2 of the IO block through `trx_sys` (C64 clock),
interrupt causes b1 (engine job done) and b2 (records lost) in `IRQ_CAUSE`, `CAPS` b2.

- **Markers.** With `LOG_CTRL` b0 a snap point is looked for every frame (`c64_core.v`
  `mk`); every frame boundary writes a marker into stream 2: frame number, `CAP_GEN`,
  phi2 cycle count (48 bit), VIC position, and the record counts of streams 0/1 at the
  boundary (written by the drain, which sees the FIFO in order). Frames of the
  `CAP_CTRL` cadence also load the shadows; a capture while the previous block is still
  being copied is skipped (`cap_block`) and flagged.
- **Capture sequencer.** After a capture it walks a 64-entry table (`{count, first
  word}`, reset: the 27 words built today) over the state bus, one word per 4 clocks,
  third master after the freeze responder and `SB_DATA`; header and tail carry `CAP_GEN`
  at start and end (torn when they differ).
- **Drain.** One 512 × 66 FIFO (C64 clock → `ui_clk`, one RAMB36); every record is one
  masked 128-bit MIG write into its stream's circular area. Areas are powers of two
  aligned to their size (`LOG_BASE`, `LOG_MASK` = size − 1; record n at `LOG_BASE |
  (n × 4 or 8 & LOG_MASK)`, so the drain keeps only a record count per stream). Reset
  layout inside T20 §5.1's 0x5C00000-0x7FFFFFF: stream 2 4 MB at 0x5C00000, stream 1
  8 MB at 0x6000000, stream 3 (T21, not built) 8 MB at 0x6800000, stream 0 16 MB at
  0x7000000. No merging of records into one word yet (worst case ≈ 1 M MIG writes/s,
  1.3 % of the command slots). Configuration registers are write-only (ARCH §2 rule 2).
- **Mover.** `COPY` and `FILL`, one 16-byte word at a time (≈ 48 MB/s): the app reads the
  ring by copying it below 64 MB.
- **Write-log taps (M-b).** Stream 0: C64 RAM, `c64_mem` port A (read-first: the old
  byte of the write's own edge) and the second port (JTAG, freeze responder) as writes
  from outside (`LOG_CTRL` b1; the marker of that frame gets flag b3). Stream 1 (T20
  §4.2 extended records, spaces numbered 1 colour RAM, 2 cartridge RAM, 3 cartridge
  flash, 4 EEPROM, 5 drive A RAM, 6 I/O, 7 GCR): colour RAM port A (made read-first in
  `c64_top.v`, `ram_color_dout` unchanged) and its second port as outside (b3); cartridge
  RAM from `cart_slot`'s write queue (b2), whose old byte `cart_fill` reads from DDR2
  before the write (`rbw`, only while the tap is on) and hands to the drain in order;
  `LOG_PUT` for flash changes made by software. Ordering at the boundary: every tap's
  record carries the frame parity of its write edge; a marker goes into the FIFO only
  after every record of the frame it ends, records of the next frame wait behind it.
  One holding register per source; a record that finds its register full is lost and
  counted (`DROPPED`, marker flag b2, IRQ cause b2). Not built: EEPROM (93C86), drive
  RAM, I/O, GCR taps; the input log.
- **App side (later round), what it must do:** set the areas (inside 0x5000000-0x7FFFFFF,
  powers of two aligned to their size, not overlapping), `LOG_CTRL` = 0x10F (restart, all taps), `CAP_CTRL` = cadence; on
  IRQ b3/b2 or by polling `LOG_CNT[2]`: walk stream 2 from its last position (records
  F1-F4 per frame, B1 … B2 per chip block, torn blocks dropped), build the T20 §4.4 index
  from F1/F2 (adding up F2's 24-bit cycle steps into the 64-bit count) and F4 (stream 0/1 record numbers, mod 2^28, position = `LOG_BASE |
  (number × record size & LOG_MASK)`), copy what it needs below 64 MB with `COPY` (16-byte units: it reads the
  whole 16-byte words around the records). Keyframes, undo/redo, retention and the
  `.c64re`/`.c64rering` writers are software (T20 §8-§10); `LOG_PUT` the flash
  operations in T16.2's ISR (one record per program, `{0, 0, data, old, 3, chip addr}`).
- **Torn window (M-c, `tb_ring snap` test 7).** A stop asked for at every clock from
  −40 to +160 around a capture's shadow load, one run each from the same state: −40 … 0
  no block (the stop wins the edge, no capture), +1 … +128 a torn block (flagged), from
  +129 on a whole block equal to the run without a stop; never a whole block that
  differs. So the window is the copy's 128 clocks (4.1 µs: 27 words at about 4 clocks
  each plus header and tail) per captured frame, 0.02 % of a frame; pipelining the
  sequencer's state-bus reads would cut it to about a third.
- **Resources** (yosys `synth_xilinx -family xc7`, module alone; Vivado usually maps
  10-20 % fewer LUTs). Leaner pass (2026-10-02), before → after:

  | Part | LUT | LUT as RAM | FF |
  |---|---|---|---|
  | `trx_engine.v` | 1,338 → 1,141 | 54 → 35 | 1,967 → 1,737 |
  | `mig_arb` (main before T20: 403) | 631 → 456 | 1 | 392 |
  | `cart_fill` (before T20: 164) | 240 | 22 | 177 |
  | `trx_sys` (before: 449) / `trx_io` (126) | 484 / 139 | — | 265 / 231 |

  T20's share is ≈ 1,176 (engine) + 53 (`mig_arb` client "e") + 76 (read-before-write) +
  48 (pages, forwarding) + ≈ 40 (taps, snap condition) ≈ 1,390 LUT (was ≈ 1,820), ≈ 1,830
  FF, 1 RAMB36. Against §11 (engine 490 + taps 255 + snap 20 = 765): ≈ +630. Cuts made:
  one 24-bit phi2 counter for markers and stream-1 records (F2 carries its low 24 bits,
  the app adds the steps up; the 48-bit counter, the second in-frame counter and
  `CYCLES` are gone); torn/skipped counters dropped (the flags are in stream 2); the
  old-byte miss counter is one sticky bit (`LOG_STAT` b4); readback only for status,
  `FRAME`, `DROPPED`, `LOG_CNT`; the capture table on one LUT RAM port (written only
  while no block is copied); the mover keeps one offset and uses `ENG_SRC/DST/LEN/ARG`
  as written (no copies); `mig_arb`'s datapath on one encoded select (one 4:1 LUT per
  bit; this alone saves 175 LUT, and would save some on main too). Tried and not taken:
  one holding register for both colour ports (+12 LUT, −47 FF). What is left of the
  excess: the COPY data path into the 128-bit MIG write (≈ 100), the FIFO input mux over
  seven sources (≈ 70), the drain's per-stream address (≈ 75), three 28-bit stream
  counters, the cartridge byte select in `cart_fill` (≈ 40). The next step down would be a
  high window for the app's CPU onto the upper DDR2 instead of `COPY` (T21's first idea),
  if a CPU-side window is cheaper than the mover in Vivado's numbers.
- **Simulation** `sim/ring` (`tb_ring_top` = the statebus machine + engine + `mig_arb` +
  MIG model, two clocks): 24 frames with a capture every frame: one marker per frame,
  consecutive numbers, snap on line 0 from x 96, cycle steps of one frame, every block
  equal to the shadows read over the bus afterwards, none torn or skipped, stream 2 wraps,
  C64 clock never gated; log off stops the records; `COPY` / `FILL` exact. Write log
  (`tb_ring log`, an EasyFlash running our own loop: `INC $DF00,X`, stores to $0400 and
  $D800, `JSR`): 8 frames, ≈ 1,500 C64 RAM and ≈ 1,500 stream-1 records per frame, a
  JTAG poke and a `LOG_PUT` on the way; replaying streams 0/1 forward from a reading
  taken with the CPU paused finds every old byte equal to memory and ends on the second
  reading (C64 RAM, colour RAM, cartridge RAM), undo gives the first; every marker's
  counts equal the bench's own count of writes up to its boundary edge; none lost.
  Snap condition (`tb_ring snap` test 6): program B with an X/Y-expanded sprite still
  drawn on line 0 (Y = $2C, lines 300-311 and on into the next frame): snaps at x 143-175
  instead of 103-127, every frame still has one.
  `sim/statebus`, `sim/trx_io`, `sim/pixlayer`, `tb_mig_arb`, `sim/cart` (all types),
  `sim/cart_unit`, `sim/dma_io`, `sim/pause`: pass.

### 15.8 Restore (branch t20-restore, M-d, 2026-10-02)

**Loaded: every flip-flop of the 6510 and of both CIAs**, not only the ones §5.4 lists as
W. Each core exports its flip-flops as one vector and loads all of them from staging on
the next clock (`sb_ld`; one line at the end of each clocked block):

- 6502N: `proc_registers` (102 bits: A X Y S P PC, address and data latches, IR, the
  carries), `proc_control` (22: state, muxes, at-`sync` bits, `intg`), `proc_interrupt`
  (6); `cpu6510.v` (79: bus/pin latches, $00/$01, floating bits and their counters,
  RDY/IRQ/NMI latches). 209 bits, state-bus chip 1 words 8-14.
- reDIP CIA: every module (ports without the pin samples, timers, control, TOD with its
  edge detector, serial, interrupt, the core's `phi2_prev`/`res`, the CNT and /FLAG edge
  detectors) and the wrapper's 50 Hz TOD divider: 363 bits, chips 3/4 words 8-19.
- The capture table's reset value copies them with every block (58 words).

Loading everything instead of §5.6's register replay + W subset was chosen because the
CIA's pipeline flip-flops (`phi2_prev`, edge detectors, `ir_clr`, delayed flags) are
sampled on every clock and would otherwise pick up the stopped machine's values on the
load edge.

**The load edge is the snap edge itself.** `STOP_POS` stops the machine on line 0 one
edge *before* the phi2 fall that ends a given cycle (a phase counter `ph` in the gate
finds that edge). The resume edge is then that phi2 fall. The VIC makes its normal step
on it while the CPU and CIAs take the state captured after the snap's edge, which was the
same edge of an earlier frame. So no chip is one edge ahead, and the VIC's position
needs no load and `RESUME_PHASE` = 0. A position stop also disarms the frame snap, as a
snap would.

**`RESUME_PHASE`** (page 0 0x110): the next stop ends that many dot4x clocks after whole
frames, once. The gate then counts frames from there. This is for a restore onto a
different VIC position, which needs the VIC's position on the state bus (not built).

**Restore sequence** (as `tb_ring restore` plays it; software in the app later):
1. stop anywhere; log off;
2. RAM: undo stream 0 from the current count back to frame N's F4 (port B pokes). The
   colour RAM would be undone the same way from stream 1 through its second port (the
   test program writes none);
3. VIC registers by bus replay from N's block. These are read images: $D012/$D013/$D014/
   $D019/$D01E/$D01F are skipped, and $D011 bit 7 is the raster bit, not the compare bit.
   The CPU is held from the first DMA on;
4. hold the CPU (CPU-only pause), arm `STOP_POS` with N's cycle (F3 x / 8), let the
   machine run to it (at most a frame);
5. `SB_DATA` writes the 31 words of N's block into staging (SB_ADDR at chip 1 w8, 3 w8,
   4 w8), `SB_LOAD` = 0x1A, release the CPU hold, restart the log, release `STOP_POS`
   (b9). The first edge loads.

**Register map additions:**

| Address | Name | Access | Content |
|---|---|---|---|
| page 0 0x110 | RESUME_PHASE | RW | b19:0 offset for the next stop's end; R: still pending |
| page 0 0x114 | STOP_POS | RW | b5:0 cycle, b8 arm (one shot), W b9 release the hold; R b16 holding |
| page 1 0x004 | SB_DATA | W | staged word at SB_ADDR (hidden words only), then increments; refused unless the gate is shut |
| page 1 0x00C | SB_LOAD | RW | b1 CPU, b3 CIA 1, b4 CIA 2: load at the next opening edge; R: armed |

**Simulation** (`tb_ring restore`): a program without the SID (its own IRQ handler on CIA 1
timer A, CIA 2 timer B running, TOD and timer reads, $D020 writes, RAM written all over)
is restored to frame N after 8 more frames. The 6 frames that follow equal the 6 frames
after N in the original run: every word of every chip block (all 58), all 13,236 C64 RAM
write records (address, old, new), the frame cycle counts, the snap positions and the
flags. With `TB_RING_NOLOAD=1` (the same run without the load), the first frame already
differs. `RESUME_PHASE` 0, 1000 and 123457, then 0 again: the clock is off for whole
frames, 1000 clocks, 123457 clocks, then whole frames. Torn window with 58 words:
+1 … +256 clocks (8.1 µs). Regressions: ring run/log/snap, statebus, dma_io, pause, cart and
trx_io pass. Lorenz disk 3 (CIA, IRQ, NMI) in `sim/lorenz` gives output identical to main.
That output includes 8 failures (ICR bit 7 one cycle early: ICR01, IMR, CIA1TA/TB, CIA2TA/TB,
CIA1TAB, IRQ/NMI) that main's bench has too; the board passes them (T10). The first 16 tests
of disk 1 pass.

**Resources** (yosys, module alone, before → after): `c64_redip_cia` 410 → 687 LUT
(352 → 379 FF), per CIA; `cpu6510` with the 6502N 854 → 862 LUT. Yosys folds most CPU
loads into the flip-flops' own D LUTs, so check that against Vivado. `c64_core` gains
935 shadow and 935 staging FF and 31 words in the state-bus mux (≈ 250 LUT, estimated).
In all ≈ +850 LUT and ≈ +1,900 FF. Against §11's CPU + CIA share (156 + 427 LUT), about
+300 LUT more than planned. The cut, if needed: §5.4's W subset with a register replay
for the CIAs, so the CIA's pipeline bits sampled on every clock (here the reason for
loading everything) must then be shown to settle.

**Open:**
- VIC: its position and internals (raster counters, VC/RC, sprites, refresh counter,
  IRQ/collision latches) are not on the bus. Because of that, a restore must stop at N's
  own line-0 cycle, and a frame whose $D019/$D01E/$D01F latches differ from what the run
  to that position produces is not exact. A raster compare other than the KERNAL's would
  need the VIC's write-side registers (W words).
- SID (voice 3 and accumulators), REU registers, cartridge model and drive are not loaded
  yet. The test program avoids them.
- Colour RAM and cartridge RAM undo, and keyframes, need the engine's `APPLY`/`RAM_IN`.
  The bench does C64 RAM with JTAG pokes.
- The sequencer reads one word per 4 clocks. Pipelining it would shrink the torn window
  about ×3.

### 15.9 Restore with the subset (branch t20-subset, 2026-10-02)

The full load of §15.8 cost +1,361 LUT in Vivado (25,097, 77.0 %). Two changes bring
it down without losing the bit identity of `tb_ring restore`:

1. **`c64_core` coding.** Staged writes are now a clock enable per word, with the data
   straight from `sb_wdata`; the hidden-state readback is a table indexed by the word
   number. Before, an index computed by subtraction made yosys build a mux per staging
   bit. yosys: `c64_core` 2,139 → 1,040 LUT, no change in function.
2. **Subset load** (default; `SB_FULL` defined = every flip-flop as in §15.8, for A/B;
   the bench builds it with `SB_FULL=1 build.sh` into `build_full/`). No longer loaded:
   - 6502N: ADL/ADH, H, the PC/index carries, `ready_d1`, `adh_clash`, `so_d`, the IRQ/NMI
     pad synchronizers `irq_c`/`nmi_c`/`nmi_d1`, `dummy_cycle`. The load is always at the
     end of an opcode fetch (a frame snap), and from there each of them is written
     before it is read, or is sampled again from its input.
   - `cpu6510`: the pin sample `pin_r` (taken again on the load edge).
   - CIA: `phi2_prev` and `res` (they take their live values on the load edge), and,
     replayed over the bus instead, the port registers (PRA/PRB/DDRA/DDRB latches), both
     timer latches, the ICR mask and the TOD alarm. None of these changes without a CPU
     write, so the run to the stop position (CPU held) leaves them as replayed. The
     shadows still hold all of them, so the replay values come from the chip block.
   - **Replay sequence**, per CIA, from N's block, before the run to the stop position:
     DDRA, DDRB, PRA, PRB; timer A lo/hi, timer B lo/hi (latches); ICR $7F then
     $80 | mask; CRB = $80, alarm hours, minutes, seconds, tenths. CRB itself, the
     counters, the clock and every pipeline bit are loaded afterwards.

**Results.**
- `tb_ring restore` uses a test program that now also reads CIA 2's PRB, DDRB and stopped
  timer A every loop, and rewrites them, the ICR mask bits 3-4 and the alarm every
  ≈ 2 frames. The 6 restored frames are bit-identical: all 58 words of every block and
  every RAM write.
- Without the replay (`TB_RING_NOREPLAY=1`) the first frame differs: 6 words (CIA 2
  ports, timer A) and 205 writes. That shows the replay is needed and correct.
- With `SB_FULL` it is bit-identical without the replay.
- Regressions: ring run/log/snap, statebus, dma_io, pause, cart pass.
- The mask and alarm replay are not observable in this test: no source those mask bits
  enable fires, and CIA 2's TOD is not running. Their correctness rests on the replay
  being plain register writes.

**Resources** (yosys, whole `c64_core`):

| | LUT | FF |
|---|---|---|
| before M-d (ca119fd) | 13,288 | 8,285 |
| M-d as merged (full load) | 15,563 (+2,275) | 10,252 |
| + `c64_core` recoding | 14,464 (+1,176) | 10,252 |
| + subset (this branch) | 14,235 (+947) | 10,252 |

What is left of the +947: `c64_core` +502 (state-bus readback of 31 words, staging
decode, stop position, capture), `cia_tod` +98 (clock, latch, pipeline loads),
`cpu6510` +67 (port, floating bits, fall-off counters), the rest ≈ +280 over the CIA and
6502N modules. The next cuts would be: the fall-off counters (38 bits: a restore would
then let the floating CPU-port bits fall off early, a known `.c64re` "partial" field
anyway); the TOD latch (only matters between an hours read and a tenths read); and
reading the hidden words through the sequencer only (no `SB_DATA` readback path),
which T21 would then miss.

### 15.10 Debug hardware (branch t21-dbg, 2026-10-02)

T21 §5.1 has the register map as built; `trx_dbg.v` (in `c64_core`) and `drv_dbg.v` the
details. The gate (`c64_core.v`) gets four inputs from the debugger: `hold` (part of `run`),
`brk` (close on this edge), `keep` (a step keeps the gate open past `halted`) and `go` (open
it for a step, and release `held`). A step is not a resume: `shut` keeps counting only the
left-out edges, so the final RUN realigns (sim/dbg test 9: 1000 single-cycle steps leave RAM,
raster, CIA timers and the frame lock as a run without them). `c64_top` exports RDY and "the
CPU has the bus"; Gideon patch 0004 gives the drive 6502's sync to `drv_dbg`.

Resources (yosys `synth_xilinx`, modules alone): `trx_dbg` 531 LUT, 508 FF, 2 RAMB36 (of it
watch units ≈ 220, counters ≈ 100, read mux ≈ 150); `drv_dbg` 111 LUT, 234 FF; `trx_sys` +39,
`trx_engine` +15, `trx_io` +8 LUT. `cycles`/`instrs` carry `use_dsp` so Vivado may put them
into two DSP48 (≈ −80 LUT). Against §11's 360 LUT for debug (+ the gate's counters and steps)
this is above plan; cuts that would not lose a verb: raster source on one unit only (−16),
DCYCLES 16 bit (−16), drive step count 8 bit (−8).
