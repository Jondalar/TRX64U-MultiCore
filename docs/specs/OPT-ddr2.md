# OPT-ddr2: our own lean DDR2 controller instead of MIG

**Status:** spec 2026-10-05, branch `opt-ddr2`. Decision (Alex 2026-10-05, architecture reset decision 6):
option B, "das lohnt sich richtig". Built first in the `ddr2_test` design with a board memory test, swapped
into `c64_basic` only after that test holds. OPT-mig (option A, -313) is merged and stays until the swap.
Steps 2 and 3 are built (§7): `ddr2_lean`, the models and benches (green in iverilog), the `ddr2_test` design;
§8: board, 4-hour run met (Q1). Step 6, the swap into `c64_basic`, is built (§9, branch `ddr2-swap`): benches green,
not yet built in Vivado, not yet on a board in `c64_basic`; §7.10 lists what the benches cannot show.

## 1. Why

`mig_ddr2` after OPT-mig: 3,076 LUT (c64_basic, CI run 432), about 2,000 of it calibration that runs once at
power-on (init 744, read leveling 539, temperature tracking 254, DQS search, write calibration), plus a PHY
built on PHASER/IN_FIFO/OUT_FIFO hard blocks, one PLL and one MMCM. The chip's slices are at 99.95 %.

Gideon's C64U / U64 Elite II core does not use MIG: his bootloader for that platform
(`1541ultimate/target/u64ii/riscv/bootloader/Makefile:16` builds `software/portable/riscv/ddr2_calibrator_u64ii.c`)
initialises the DDR2 and calibrates the read path **in software** through a small register block (IDELAY taps,
bitslip, read delay, DQS readback), and his public constraints put the same balls on HSTL_I_18 / HSTL_II_18
(MIG would force SSTL18_II). His FPGA source for that platform is closed; his Altera and Lattice DDR2 controllers
(`fpga/altera/ddr2_ctrl*.vhd`, `mem_io.vhd`, GPL, readable) show the same idea: DQ sampled on a free-running,
tunable clock, DQS read as data only to find the alignment, no DQS-clocked capture.

Target: **800-1,200 LUT** for controller + PHY (estimate, measured by the first build), i.e. about -2,000.

## 2. Facts it builds on (research 2026-10-05)

| item | value | source |
|---|---|---|
| memory | MT47H64M16HR-25E, x16, 128 MB used (A13 = F4 held low), 8 banks, row 13 / col 10 bits | `constraints/mig_ddr2.prj` |
| balls | bank 35 (HR, VCCO 1.8 V, internal Vref 0.9 V), the same 46 balls as Gideon's | prj, `constraints/c64u.xdc:58` |
| MIG today | 300 MHz memory clock (DDR2-600), 4:1, `ui_clk` 75 MHz, CL5 AL0 BL8, RTT 50 ohm | prj |
| Gideon (Artix) | BL4, CL4, AL3, WR4, RTT 150 ohm, reduced drive; memory clock unknown | `ddr2_calibrator_u64ii.c:230-231` |
| user side | `mig_arb` speaks MIG's native `app_*`: 27-bit word address, 128-bit data, 16-bit mask, `app_rdy`/`app_wdf_rdy`, reads returned in order, up to 32 in flight (8 for video) | `ddr2_port.v` |
| clocks | `clk200` from `mmcm_ddr` (MIG's sys_clk and IDELAYCTRL ref); all 5 MMCMs used, MIG owns 1 MMCM + 1 PLL internally; 2 of 5 PLLs used in total | top.v, util report |
| bandwidth needed | video 52 MB/s, ring <= 12 MB/s, trace 8, capture 3, mover ~48, RISC-V/REU small but latency-bound (~0.4 us read) | T12 §5.3, T20 §5.4, ddr2_port.v |

## 3. Design

### 3.1 Interface (unchanged for the rest of the chip)

The new module `ddr2_lean` keeps MIG's port list as `mig_arb` uses it: `app_addr/cmd/en/wdf_data/wdf_mask/
wdf_wren/wdf_end/rdy/wdf_rdy/rd_data/rd_data_valid`, `ui_clk` **75 MHz**, `ui_clk_sync_rst`,
`init_calib_complete`. Reads come back in command order. One command = one 128-bit word = one BL8 burst.
So `mig_arb`, `cart_fill`, the engine, `pix_layer` and every clock crossing stay as they are, and
`sim/mig_ui_model.v` stays the benches' model (its bank-machine count fixed to the new behaviour).

### 3.2 Clocks

- Memory clock **150 MHz** (DDR2-300), controller = `ui_clk` 75 MHz (2:1). Below MIG's 300 MHz, near Gideon's
  Altera designs (125-133 MHz), and inside DDR2-800 parts' DLL range (>= 125 MHz). Peak 600 MB/s at one BL8
  per two controller clocks: about 3x what the clients need in the worst case.
- One **PLLE2** (MIG's PLL is freed) from `clk200`: VCO 1200 MHz; 150 MHz (CK, command/address), 150 MHz at
  90 deg (write DQ/DQS), 300 MHz for the 4:1 SERDES fast clocks if used, 75 MHz `ui_clk`. MIG's MMCM is freed too
  (one MMCM spare after the swap).
- `IDELAYCTRL` on `clk200` (as today).

### 3.3 PHY

- Command/address/CK: ODDR/OSERDES on the 150 MHz clock, HSTL_I_18 (Gideon's standard), CK differential.
- Write: DQ/DM via OSERDES (4:1 on 300 MHz, or ODDR on the 90 deg clock); DQS generated as a pattern, as in
  Gideon's `mem_io.vhd`. HSTL_II_18 / DIFF_HSTL_II_18.
- Read: DQ and DQS through **IDELAYE2 (VAR_LOAD or VARIABLE, 32 taps)** into ISERDES (or IDDR) on the free-running
  read clock; DQS sampled as data. A read-delay select (0-3 controller-clock slots) and a bitslip align the
  captured burst. The data delays move together with the DQS delay (Gideon's scheme).
- Terminations: ODT on the DRAM (EMR RTT), the FPGA side as Gideon's standards give it.

### 3.4 Controller

- **Open-row per bank** or **closed-page with auto-precharge** (Gideon: closed page, ACTIVE + READ/WRITE with
  A10 in one slot pair). First build: closed page (simplest, deterministic latency); open-row only if the
  measured bandwidth/latency needs it. 8 bank timers (tRCD, tRP, tRAS, tWR, tWTR, tRTP, tRRD, tFAW) in
  controller clocks at 150 MHz.
- Refresh every 7.8 us (all-bank), postponed only while a burst is in flight; refresh has priority over new
  commands when due (no starvation, as MIG's `app_rdy` low).
- Read data FIFO in order; `app_rdy` low while the command queue (depth 2-4) is full or a refresh runs.
- Init sequence in hardware or by the boot ROM (§3.5): CKE, 200 us, PALL, EMR2/EMR3, EMR (DLL on, RTT, AL), MR
  (DLL reset, CL, BL8, WR), PALL, 2x REF, MR (DLL reset off), OCD default/exit, 200 cycles.

### 3.5 Calibration: software, in our boot ROM (decided otherwise at the swap: hardware, §9.1)

As Gideon: a small register block (command register, IDELAY step/load, read delay, bitslip, DQS readback,
control: CK, CKE, ODT, refresh enable) and the search done by the RISC-V **before the app is loaded** (the app
lives in DDR2). Our boot ROM (T15, `bootrom_pkg.vhd`) gets the init + DQS window search (Gideon's algorithm:
step all taps, find the longest window of the DQS pattern, take its middle; else next read delay; else bitslip),
then a short RAM test, then `init_calib_complete`. Code space in the boot ROM BRAM, no LUT for a calibration
state machine. Result (taps, read delay, bitslip, window width) readable by the app and by trxdbg for the board
statistics. No temperature tracking (MIG's 254 LUT); the window width shows the margin, the long warm test proves
it.

### 3.6 Simulation

No DDR2 model exists locally. Write a behavioural DDR2 model (x16, BL8/BL4, CL/AL, banks, refresh timing checks,
DQS/DQ timing with a configurable board delay) in Verilog, `designs/ddr2_test/sim/ddr2_model.v`, from the JEDEC
DDR2 rules and the MT47H64M16 data sheet timings. Xilinx primitives (IDELAYE2, ISERDES/OSERDES or IDDR/ODDR) get
small behavioural models for iverilog. Benches: init sequence and timing checks, calibration finds the window for
several board delays, random read/write/mask traffic against a reference memory, refresh under load, `mig_arb`
traffic through the unchanged interface.

## 4. Steps

1. Spec (this).
2. `ddr2_lean` + PHY + model + benches (iverilog, green).
3. `ddr2_test` design with `ddr2_lean` instead of MIG; its tester extended: random addresses, write masks,
   refresh stress, error count and first failing address readable over JTAG (today only LEDs); calibration in a
   tiny RISC-V-less version (hardware-driven sweep in the test design only, or the test design gets a register
   port for trxdbg to run the boot ROM's algorithm from the Mac).
4. CI build (builder), board: memory test on Alex's C64U cold, warm, for hours; window width logged.
5. (Optional) Till's UE2 if it is ever at hand; not a gate (Q1).
6. Swap in `c64_basic`: MIG out, `ddr2_lean` in, boot ROM calibration; full bench set, Lorenz subset, ring,
   REU, long run. Measured LUT/slices/WNS against main.

## 5. Decisions

| # | Question | Decision |
|---|---|---|
| Q1 | Acceptance before the swap into c64_basic | **At least 4 hours warm memory test on Alex's C64U** (Alex 2026-10-05). No second board is permanently available; Till's UE2 only if it happens. Board-to-board margin is therefore not measured: the calibration logs its window width on every boot, so a narrow window shows up in the field. |

## 6. Open questions (with Alex, one at a time)

- Q2 (after the first build) 150 MHz enough, or try 200 MHz for latency?

## 7. As built (2026-10-05, steps 2 and 3)

Files: `designs/c64_basic/ddr2_lean.v` (controller + PHY, 3 helper modules in the same file),
`designs/ddr2_test/` (`top.v`, `cal_seq.v`, `ddr2_tester.v`, `pins.xdc`, `build.tcl`), simulation in
`designs/ddr2_test/sim/` (`ddr2_model.v`, `xilinx_models.v`, `tb_ddr2_lean.v`, `tb_mig_arb_lean.v`,
`tb_ddr2_test.v`, `run.sh`), `tools/ddr2_status.py` (JTAG status of the test design), benches in
`ci/sims.txt` / `ci/sims.deps`. `c64_basic`'s `top.v` and MIG usage are untouched; `ddr2_port.v`/`mig_arb`
are used as they are (in the benches).

### 7.1 Clocks and the slot grid

One `PLLE2_BASE` in `ddr2_lean`, input `clk200` (the BUFG'd 200 MHz that is also the IDELAYCTRL reference),
all phases 0:

| item | value |
|---|---|
| VCO | CLKIN1 5.0 ns, DIVCLK_DIVIDE 1, CLKFBOUT_MULT 6: **1200 MHz** (7-series -1 PLL range 800-1600) |
| CLKOUT0 | DIVIDE 4: **300 MHz** -> BUFG `clk300`: CLK of all 44 OSERDESE2 and 18 ISERDESE2 |
| CLKOUT1 | DIVIDE 16: **75 MHz** -> BUFG `ui_clk`: CLKDIV of the SERDES, IDELAYE2 `C`, the whole controller |
| 150 MHz, 90 deg | **not used** (spec §3.2 had them): see below |

Both BUFGs carry `CLOCK_DELAY_GROUP` so CLK and CLKDIV of the SERDES keep equal insertion delay. The memory clock
CK is a *pattern* on an OSERDES (`8'hCC`: 0,0,1,1,0,0,1,1 over the eight slots of a ui_clk cycle), not a clock.
The 600 Mbit/s slot (1.667 ns) is a quarter of a memory clock, so every phase relation the DDR2 wants (CK in the
middle of the command, DQS on the CK edge, DQ centred on DQS) is a shift of a pattern by whole slots: no 90 deg
clock, no second clock domain, no clock crossing in the fabric.

Per cycle n (ui_clk, 13.33 ns = 2 memory clocks, slots 0-7). What the controller registers in cycle n goes out
in the next one, for all pins alike:

* command: slot 0 (CK 0) ACTIVE, slot 1 (CK 1) READ/WRITE + A10 (auto-precharge); every value held 4 slots, CK
  rises 2 slots in: **3.33 ns setup and hold** at the DRAM. AL = 1: ACTIVE-to-READ 1 CK + AL 1 CK = 13.3 ns >= tRCD
  12.5 ns. CS# is low always, CKE, ODT are static register bits.
* write: DQS rises on a CK edge WL = RL - 1 = 3 CK after the WRITE (tDQSS = 0): the data is launched in cycles n+2
  (beats 0-3, slots 0/1 = preamble, DQS = 0,0,1,1,0,0,1,1) and n+3 (beats 4-7); slot 0 of every cycle repeats the
  last beat of the cycle before (DQ is centred on DQS: window [DQS edge - 1 slot, + 1 slot]). Consecutive writes
  2 cycles apart give a continuous strobe. DQS/DQ drive: `oewin` (below).
* read: DQ and DQS pass IDELAYE2 (VAR_LOAD, 32 taps of 78 ps at 200 MHz) into ISERDESE2 (DDR, 8 bits, CLK
  clk300): eight samples of 1.667 ns per cycle, the **odd** ones (1,3,5,7) are the four beats of the cycle (one
  sample per 3.33 ns beat; the even ones exist because the primitive gives them and BITSLIP needs them).
  `ddr2_des8` gives `s[0]` = oldest = Q8 of the primitive.

### 7.2 Controller (`ddr2_lean`)

* Port: MIG's `app_*` as `mig_arb` uses it (`app_wdf_end` ignored, `app_rd_data_end` = valid), `ui_clk`,
  `ui_clk_sync_rst` (PLL locked and IDELAYCTRL ready, +32 cycles), `init_calib_complete` (= register bit).
* Command queue 2 entries (strict order), write data 2 words, `app_rdy` low while 2 commands wait,
  `app_wdf_rdy` low while 2 words are held. A write issues only with its data held. Data and command may come in any order.
* **Closed page, one slot pair per access**; an access issues every 2 cycles at best (the data bus: BL8 = 4 CK).
  Address map: `bank = word_addr[5:3]` (**consecutive 16-byte words go to consecutive banks**, unlike MIG's
  bank-major `BANK_ROW_COLUMN`: a stream rotates over the 8 banks and keeps the bus full), column
  `word_addr[12:6]` (+ 3 zero bits), row `word_addr[25:13]`; A10 = 1 on every column command, A13 (board pin F4) is held
  low outside.
* Timings in controller cycles (13.33 ns), from the MT47H64M16HR-25E values of `mig_ddr2.prj`
  (tRCD 12.5, tRP 12.5, tRAS 40, tRFC 127.5, tRRD 10, tFAW 45, tWTR 7.5, tRTP 7.5, tWR 15 ns, tRC 55):

| rule | derived | in the code |
|---|---|---|
| ACTIVE to the same bank after a read | tRAS 40 + tRP 12.5 = 52.5; internal precharge of a BL8 AP read comes AL + 4 - 2 + 2 CK after the READ | 5 cycles (66.7 ns) |
| ... after a write | WL 3 + 4 + tWR 3 CK + tRP 2 CK = 12 CK after the WRITE | 7 cycles (14 CK) |
| tRRD, tFAW | one access per cycle at most: ACTIVEs >= 13.3 ns apart, the 5th >= 4 cycles after the 1st (53 ns >= 45) | by construction, checked in the model |
| read -> read, write -> write | BL8 = 4 CK | 2 cycles |
| read -> write | RL + 4 + 1 - WL = 6 CK on the pins | 6 cycles (the drive window of the write begins early, see `oewin`) |
| write -> read | WL + 4 + tWTR (2 CK) = 9 CK to the READ | 6 cycles (margin for the late end of the drive window) |
| refresh | all banks idle, REFRESH in slot 0, tRFC 127.5 | 10 cycles before the next ACTIVE; interval **560 cycles = 7.47 us** (register), at priority over new accesses |
| read latency | command -> `app_rd_data_valid` | `C0 (2) + dmax + 3` cycles after the launch, 8 for dmax = 3 (about 110 ns plus the queue) |

* Read data: lane words (4 beats x 8 bits) are caught per lane at cycle `n + C0 + dly` (A) and the one after (B)
  and copied to the output together; the two lanes may differ by one cycle of `dly` (the output holds for two
  cycles). Strict order is by construction: every read has the same latency.
* **Init in the registers, not in hardware**, as Gideon's calibrator: no LUT for a state machine that runs once, the
  c64_basic boot ROM (T15) was to drive the same writes before the app is loaded (decided otherwise at the swap: `cal_seq.v`
  replays the sequence in hardware in `c64_basic` too, §9.1), and the sequence (waits, EMR values, OCD
  skipped as his) can be changed without a bitstream in the register block. `ddr2_test` has no CPU either.

### 7.3 Register block (`ddr2_lean` ports `r_*`, ui_clk, word addresses)

| addr | name | access | bits |
|---|---|---|---|
| 0x0 | status | r | 31:16 = `DD20`; 7 calib done, 6 refresh due, 5 direct command pending, 4 DQS capture valid, 3 calibration read busy, 2 ui reset, 1 IDELAYCTRL ready, 0 PLL locked |
| 0x1 | ctrl | rw | 0 CK on, 1 ODT pin, 2 refresh on, 3 CKE, 4 calibration done (= `init_calib_complete`, opens `app_rdy`) |
| 0x2 | cmd | w | one command in slot 0, only while calib done = 0 and refresh off: 18:16 {RAS#,CAS#,WE#} (MRS 000, REF 001, PRE 010, ACT 011, WR 100, RD 101, NOP 111), 15:13 BA, 12:0 A |
| 0x3 | tap | rw | 4:0 IDELAY tap of lane 0 (DQS0 and DQ[7:0]), 12:8 lane 1; a write loads both |
| 0x4 | rddly | rw | 2:0 read delay lane 0 (cycles from `n + C0`), 6:4 lane 1; 0-5 |
| 0x5 | bitslip | w / r | write 0/1 = one BITSLIP pulse for lane 0/1 (all 9 ISERDES of the lane); read: pulses given, 3:0 lane 0, 7:4 lane 1 |
| 0x6-0x9 | DQS capture | r | lane 0 low, high, lane 1 low, high: 8 words of 8 samples of the DQS pin, word 0 = cycle `n + C0` of the last calibration read, bit = sample (0 = earliest) |
| 0xA | calrd | w | one calibration read of bank 0, row 0, column 0 (only with calib done = 0); the capture is valid (status bit 4) a few cycles later |
| 0xB | refi | rw | refresh interval in cycles, reset 560 |
| 0xC | refcnt | r | refreshes issued |
| 0xD | oewin | rw | 1:0 cycles of DQ/DQS drive before the data, 3:2 after it (0-3), reset 0x9 = 1 before, 2 after (board, §8) |

`oewin`: the OSERDESE2 tri-state path (T1, BUF mode) and its data path are both registered in CLKDIV, but how many
cycles apart they come out is not something this code could check. The drive is on from `E` cycles before the first
data cycle to `L` cycles after the last one, the DQS is held low, DQ carries the previous beat (junk) in the extra
cycles; both sides are harmless to the DRAM (no DQS edges) and the turn-around gaps (6 cycles) keep the drive away from
the DRAM's own read data. The model passes with T one cycle early or late either way (`-DTQ_DLY=13336`,
`-DOQ_DLY=13336`).

### 7.4 Calibration (`cal_seq.v`, the algorithm for the boot ROM)

1. Init: CK on, 27 us, CKE, 200 us, PALL, EMR2 = EMR3 = 0, EMR 0x004A (DLL on, AL 1, RTT 150, reduced drive; ODT
   pin stays low as in Gideon's calibrator), MR 0x0533 (BL8, CL3, WR3, DLL reset), PALL, REF, REF, MR 0x0433, 400
   cycles, CK|CKE|refresh on. 16-cycle waits after every command (213 ns, tRFC 127.5).
2. For each of the **8 bitslip positions** (one common BITSLIP pulse for both lanes between positions), for each tap
   0..31 (both lanes): load the tap, one calibration read, read the DQS capture. DQS is read as data. With the
   beat samples (odd bits) a **hit at word index a (1..5)** means: bits 1,3,5,7 of word `a` and of word `a+1` are
   1,0,1,0 (DQS high in beat 0, low in beat 1, ...) and bit 7 of word `a-1` is 0 (DQS preamble low). That is
   "the burst's first beat sits at sample 1 of word `a`" with the sample inside the DQS half period; the window of taps
   where this holds is where the DQ eye is as well (DQ is edge aligned with DQS).
3. Per lane and `a` the longest run of hit taps (at least 2) is the window; the tap is its middle; if the scan range
   cuts it (32 taps = 2.5 ns, a window is 2.7 ns = about 34 taps) 17 taps in from the cut end (first = 0: `last - 17`,
   last = 31: `first + 17`, both = 15; a visible part shorter than 10 taps fails). Gideon's rule is the same idea
   with 9 and his 1-sample-per-beat window; this one is mine.
4. A position is usable when the same `a` has a window on both lanes; **all eight are scanned and the position whose
   tap is farthest from the ends of the range wins** (neighbours are 1.67 ns apart and cut the window in different places);
   (position + 1) more BITSLIP pulses reach it from the last one (eight are a cycle), then tap, read delay, calib done.
   No window at any position: fail (`cal_ok` = 0, the test design never starts).
5. Result: tap per lane, read delay, bitslip position, window width (taps, 78 ps) and a flag for "cut by the range" per
   lane, positions that had a window. The width is the margin to log on every boot; no temperature tracking.

### 7.5 The `ddr2_test` design

`top.v`: MMCM 50 -> 200 MHz (as before), `ddr2_lean`, `cal_seq`, `ddr2_tester`, `jtag_dbg` (BSCANE2 USER1, as c64_basic),
LEDs as before (board LED blinks, case LED 1 = calibration ok, case LED 2 = rounds ok / blinking = error).

`ddr2_tester.v` (parameter AW = 23 bursts bits = 128 MB): one round = pass 0 write P1, pass 1 write P2 with random write
masks (every 16th address all bytes masked, the next none), `hold` clocks, pass 2 read + compare with the merge,
pass 3 (optional) pingpong: per address write P3, read it back, compare. All patterns are hashes of address and
round (nothing stored). Order sequential or LFSR (maximal LFSR over the AW bits, address 0 last: every address once),
alternating every round by default. Counts rounds, error words, commands; keeps the first error (burst, round, pass,
expected and actual 128 bits). Refresh stress: `ddr2_status.py refi 40`; retention: `hold`.

JTAG register map (word addresses of the debug port; `tools/ddr2_status.py` prints it, `trxdbg.Dbg.read()` reads it):

| addr | content |
|---|---|
| 0x00 | ID `DD2A0002` |
| 0x01 | status: 0 init done, 1 calibration done, 2 calibration ok, 3 controller up, 4 tester run, 5 errors seen, 6 first error kept, 7 ui reset |
| 0x02 | 4:0 tap lane 0, 12:8 tap lane 1, 18:16 read delay, 27:24 bitslip position |
| 0x03 | 5:0 window width lane 0, 13:8 lane 1 (taps), 16/17 cut by the scan range, 23:20 positions that had a window |
| 0x04 / 0x05 / 0x11 | rounds / error words / commands |
| 0x06 .. 0x08 | first error: burst, round, pass |
| 0x09-0x0C / 0x0D-0x10 | first error: expected / actual data, 32 bits each, low first |
| 0x12 / 0x13 | pass and order now / AW |
| 0x20 | control rw (reset 0x1E): 0 LFSR order, 1 alternate the order every round, 2 pingpong pass, 3 masks, 4 run |
| 0x21 / 0x22 | hold rw (clocks between pass 1 and 2) / any write: clear the counters and the first error |
| 0x100-0x10F | ddr2_lean's register block (after the calibration; before it `cal_seq` owns it) |

Pins and constraints (`pins.xdc`): the 46 balls of `mig_ddr2.prj` (bank 35) with Gideon's standards from
`constraints/c64u.xdc`: **DQ, DM HSTL_II_18; DQS/DQS# DIFF_HSTL_II_18; CK, CK# (two single-ended pins as in his XDC), address,
BA, RAS#, CAS#, WE#, CS#, CKE, ODT HSTL_I_18**; `INTERNAL_VREF 0.9` for bank 35; `SLEW FAST` on all of them; no input
termination. Clocks: the generated clocks of the MMCM and the PLL are one group against the JTAG TCK; the
`_s_reg[0]` synchronizers (LEDs, `lk_s`) are false paths; the pads of the DDR2 are not timed (reads calibrated, writes a
fixed pattern with 1.67 ns margins).

### 7.6 Simulation

`xilinx_models.v`: PLLE2_BASE / MMCME2_BASE (free-running clocks: 1667 ps slots, 3334 ps clk300, 13336 ps ui_clk),
BUFG, IBUF, OBUF, IOBUF, IOBUFDS, IDELAYCTRL, IDELAYE2 (VAR_LOAD, 78 ps/tap), ISERDESE2 (DDR 8, samples on both
CLK edges, `BITSLIP` moves the window one sample back, an unknown sample resolves to a random bit), OSERDESE2 (DDR 8,
BUF tri-state), BSCANE2 stub. They implement what the RTL assumes of the silicon (§7.10); `-DTQ_DLY` / `-DOQ_DLY`
move T or OQ later.

`ddr2_model.v`: x16 DDR2 with MRS/EMRS (CL, AL, BL, WR, DLL, OCD, DQS#), 8 banks, BL8, AP, REFRESH, the init order and the
200 us / 200 clock waits, per-bank timers (tRCD with AL, tRAS, tRP, tRC, tWR, tRTP), tRRD, tFAW, tRFC, tMRD, tWTR,
read/write turn-around, data bus overlap, tDQSS +-0.25 tCK and polarity, DQ setup/hold against DQS (250 ps), command
setup/hold against CK, refresh interval (9 x tREFI), banks open at MRS/REF; every violation is a `DDR2 ERROR` line
and counts. Read data: DQS/DQ `DQSCK` after the CK edge, DQ unknown from tQHS (250-370 ps per bit) before to tDQSQ
(100-280 ps per bit) after every DQS edge, DQS low one clock before and half a clock after. `ddr2_board` adds the
flight times (`WR_DLY` FPGA -> DRAM for CK, command and write data, `RD_DLY` back, transport delays) and a contention
check (DRAM and FPGA drive DQ together). The model is mine, from the rules, not Micron's: the checks are only as good as
that (a mutation run shows they bite: tRC 4 -> 2 cycles gives "ACTIVE inside tRP/tRC", write -> read 5 -> 3 data errors and
tWTR errors, a late write data word data errors and tDQSS/tDS errors).

`run.sh <bench>` (`cal`, `traffic`, `refresh`, `arb`, `test`), in `ci/sims.txt` (tier 1: cal, arb, refresh; tier 2: traffic, test).

### 7.7 Bench results (iverilog, `nice -n 15`, 2026-10-05, this commit)

`run.sh cal`, six board delays (FPGA -> DRAM / DRAM -> FPGA, ps), 3,000 commands each; the calibration picks bitslip
position and read delay by itself, the traffic then runs on what it found:
```
CAL: ok=1 dly=3 bs=1 tap0=15 tap1=15 width0=32 width1=32 trunc=11 hits=3 wr_dly=100 rd_dly=100
PASS ddr2_lean traffic: wr_dly 100 rd_dly 100, 3000 commands, 0 mismatches, 42 refreshes, longest gap 7534 ns, 0 DRAM errors
CAL: ok=1 dly=3 bs=1 tap0=10 tap1=10 width0=28 width1=28 trunc=11 hits=2 wr_dly=500 rd_dly=500
PASS ddr2_lean traffic: wr_dly 500 rd_dly 500, 3000 commands, 0 mismatches, 42 refreshes, longest gap 7534 ns, 0 DRAM errors
CAL: ok=1 dly=3 bs=0 tap0=13 tap1=13 width0=31 width1=31 trunc=11 hits=2 wr_dly=1500 rd_dly=900
PASS ddr2_lean traffic: wr_dly 1500 rd_dly 900, 3000 commands, 0 mismatches, 42 refreshes, longest gap 7561 ns, 0 DRAM errors
CAL: ok=1 dly=4 bs=7 tap0=15 tap1=15 width0=32 width1=32 trunc=11 hits=3 wr_dly=100 rd_dly=3500
PASS ddr2_lean traffic: wr_dly 100 rd_dly 3500, 3000 commands, 0 mismatches, 42 refreshes, longest gap 7548 ns, 0 DRAM errors
CAL: ok=1 dly=4 bs=7 tap0=15 tap1=15 width0=32 width1=32 trunc=11 hits=3 wr_dly=3000 rd_dly=300
PASS ddr2_lean traffic: wr_dly 3000 rd_dly 300, 3000 commands, 0 mismatches, 42 refreshes, longest gap 7548 ns, 0 DRAM errors
CAL: ok=1 dly=4 bs=6 tap0=15 tap1=15 width0=32 width1=32 trunc=11 hits=3 wr_dly=2500 rd_dly=2500
PASS ddr2_lean traffic: wr_dly 2500 rd_dly 2500, 3000 commands, 0 mismatches, 42 refreshes, longest gap 7561 ns, 0 DRAM errors
```
(The widths are the sim's: the DRAM model's DQS edges are clean, the window is a whole 3.33 ns half period, cut by the scan
range on both sides for most delays: `trunc=11`. On the board it will be shorter.)

`run.sh traffic`: 100,000 random commands (50 % reads, writes full and masked, working set, sequential runs, bank 0
hammering, data before/after the command by up to 3 clocks) against a reference memory, then 40,000 back to back with other
delays and the data/command offset up to 12 clocks:
```
TRAFFIC: 100000 commands (49954 writes, 50046 reads) in 5868933 ns, mismatches 0, outstanding 0
DRAM: refreshes 804, activates 100256, reads 50302, writes 49954, longest refresh gap 7561 ns, errors 0, contention 0
PASS ddr2_lean traffic: wr_dly 500 rd_dly 500, 100000 commands, 0 mismatches, 804 refreshes, longest gap 7561 ns, 0 DRAM errors
TRAFFIC: 40000 commands (22200 writes, 17800 reads) in 2324638 ns, mismatches 0, outstanding 0
PASS ddr2_lean traffic: wr_dly 1200 rd_dly 1700, 40000 commands, 0 mismatches, 330 refreshes, longest gap 7561 ns, 0 DRAM errors
```
`run.sh refresh` (refresh interval 40 cycles = 0.53 us under back-to-back load):
```
DRAM: refreshes 5681, activates 40256, reads 17916, writes 22340, longest refresh gap 626 ns, errors 0, contention 0
PASS ddr2_lean traffic: wr_dly 500 rd_dly 500, 40000 commands, 0 mismatches, 5681 refreshes, longest gap 626 ns, 0 DRAM errors
```
`run.sh arb`, the real `mig_arb` + `cpu_mem_bridge` + `ddr2_port` + drive port + cart_fill + video client + engine client
(first the functional list of `sim/tb_mig_arb.v`, then 60,000 cycles of everything at once with checked data):
```
LOAD: cpu 1194 port 1142 drive 3301 cart lines 1124 video reads 3087 engine reads 1602; DRAM refreshes 127 reads 10576 writes 4525, longest refresh gap 7548 ns
PASS mig_arb on ddr2_lean: functional list and mixed load, 0 errors
```
`run.sh test`, the `ddr2_test` design (AW = 10) with the JTAG map forced through the bus, a bit flipped in the DRAM model
during the retention hold, clear, more rounds:
```
CAL0 00030f0f: tap0 15 tap1 15 dly 3 bs 0        CAL1 00332020: width0 32 width1 32 trunc 11 hits 3
ok   one error word: 00000001          ok   first error burst: 00000309 (777)       ok   first error data differs in bit 77
ok   first error round: 00000000       ok   first error pass (read): 00000002       ok   errors after clear: 00000000
rounds 8 ... ok   errors at the end: 00000000     commands 40957
DRAM: refreshes 401, activates 46352, reads 18688, writes 27664, longest refresh gap 7548 ns
PASS ddr2_test: calibration, 8 rounds (both orders, masks, pingpong), injected bit flip found and cleared, JTAG map
```
Also run by hand: `-DTQ_DLY=13336` and `-DOQ_DLY=13336` (the tri-state a cycle late / early against the data), 6,000 commands each: PASS,
0 mismatches, 0 DRAM errors; DQSCK 0 and 400 ps, 15,000 back-to-back commands with offsets up to 12 clocks, 6,000 commands with up to
400 idle clocks: PASS. `ci/sims.sh --check-deps ddr2_arb`: PASS (the declared deps are complete).
Speed: 100,000 commands take 5.9 ms of simulated time with idle clocks and a read/write mix, i.e. about 59 ns per command
(a stream of reads or of writes to rotating banks is limited by the bus at 26.7 ns per 16 bytes, 600 MB/s; the mixed figure is the
turn-around gaps of 6 cycles).

### 7.8 Size (yosys `synth_xilinx -flatten`, an estimate: Vivado packs differently)

`ddr2_lean` alone (controller, PHY, register block, PLL, 44 OSERDESE2 / 18 ISERDESE2 / 18 IDELAYE2 as blackboxes):
**595 LUT (202 of them LUT6), 1,056 FF, 30 CARRY4**, no RAM, no DSP. Target of §1 was 800-1,200; MIG after OPT-mig is 3,076 LUT
(Vivado, c64_basic CI run 432). The FFs are the 2 write words (288), the lane read buffers (256) and the DQS capture
(128), the launch registers (about 200) and the queue. The design that goes into `c64_basic` after the swap adds only the
glue of §6 step 6.

`ddr2_test`, yosys flat, without IO buffers:

| | LUT | FF | CARRY4 | DSP48 | RAM32M |
|---|---|---|---|---|---|
| old: tester + MMCM + LEDs (MIG not in yosys: its 3,076 LUT are the Vivado figure of c64_basic) | 214 | 160 | 25 | 0 | 0 |
| new `top` | 3,722 | 3,213 | 339 | 6 | 8 |
| of it `ddr2_lean` | 595 | 1,056 | 30 | 0 | 0 |
| of it `cal_seq` | 889 | 568 | 32 | 0 | 4 |
| of it `ddr2_tester` (AW 23) | 1,680 | 1,213 | 257 | 6 | 4 |
| rest: JTAG register mux, `jtag_dbg`, MMCM, LEDs | about 560 | about 380 | | | |

The test design is bigger than before because the tester hashes (24 `mix` instances for the patterns, in LUTs and DSPs) and
`cal_seq` keeps 10 x 32 hit masks; neither goes into `c64_basic`. Compared with the old design plus MIG (about 3.3 k LUT) the
new one is about 400 LUT bigger, with the controller itself 2.5 k LUT smaller than MIG.

### 7.9 Deviations from §3

* §3.2 had PLL outputs 150, 150 at 90 deg, 300, 75: built are **300 and 75 only** (VCO 1200 MHz); CK, command and the 90 deg
  write phase are patterns on the 600 Mbit/s slot grid (§7.1).
* §3.4: closed page as planned, but the address map interleaves **banks on the low burst bits** (not MIG's); command queue
  2 entries; refresh 560 cycles (7.47 us), not exactly 7.8 us (a refresh that has to wait for a busy bank still stays under 7.8).
* §3.3 "read-delay select 0-3 slots": 0-5 cycles, per lane, plus BITSLIP (8 positions) and the odd-sample choice.
* §3.5: the algorithm of §7.4 differs from Gideon's in three things: it uses all eight bitslip positions and takes the
  best, its window rule is for a 34-tap window, and it needs the same `a` on both lanes.
* The calibration needs the DQS capture registers (128 FF) and a calibration-read path in the controller; both stay in the
  c64_basic build (`cal_seq` calibrates every boot, §9.1).

### 7.10 What the benches cannot show: to check on the board

1. **ISERDESE2 Q order** (Q8 = oldest sample) and BITSLIP behaviour (the model: window moves one sample back per pulse;
   the primitive alternates +1 / -3 and visits all eight positions in eight pulses, which `cal_seq` only needs). A wrong Q
   order makes the DQS calibration pass but the data words come out with the beats of a cycle reversed: the tester finds it
   at once (errors in the first round, every word).
2. **OSERDESE2 T (BUF) against OQ** (see `oewin`); DQS driven low up to 2 cycles before/after a burst.
3. IO: HSTL_II_18 without FPGA termination and ODT off (as Gideon's calibrator); drive/slew (`SLEW FAST`, no `DRIVE`);
   whether IOBUFDS with `DIFF_HSTL_II_18` and CK/CK# as two HSTL_I_18 pins pass the DRC of the 50T's pin pairs (E1/D1, P5/P4,
   M1/L1).
4. IDELAYE2: `VAR_LOAD` with `LD` for one cycle, `HIGH_PERFORMANCE_MODE`, `IDATAIN` from the IOBUF; one `IDELAYCTRL`
   (REFCLK `clk200`, RST = MMCM not locked) for the 18 delays, `IODELAY_GROUP` on all of them.
5. `CLKB` of the ISERDES is `~clk300` (an inverter on the clock net that Vivado has to absorb into the IOLOGIC; LiteX does the
   same); the BUFG-clocked 600 Mbit/s SERDES in an HR bank of the -1 part; `CLOCK_DELAY_GROUP` as the only phase alignment
   of CLK and CLKDIV.
6. The DQS-as-data pattern needs DQS to be driven low in the preamble and to read as noise (not as 1,0,1,0) when idle; the
   window width on the board (taps) is unknown: the choice rule assumes about 34 taps.
7. Timing closure of the controller at 75 MHz in Vivado (the yosys depth looks like 6-8 LUT levels), 2 BUFG more, one PLL
   (c64_basic frees MIG's PLL and MMCM), placement of 44 OSERDES + 18 ISERDES + 18 IDELAYE2 in bank 35's I/O columns.
8. Tolerances of the DRAM at 150 MHz (CL3/AL1, DLL on at tCK 6.67 ns is inside the data sheet's DLL range 5-8 ns for CL3; checked
   by the model's rules, not by the part).
9. tRC 55 ns is taken from the -25E data sheet from memory (the .prj gives only tRAS 40 + tRP 12.5); the controller uses
   66.7 ns either way.
10. The tester's hash logic (about 3,700 LUT in yosys, a test design) at 75 MHz.

### 7.11 Next

Step 4: CI build of `ddr2_test` (builder), board: cold, warm, hours; read the window width and `oewin` first.

## 8. Board, first night (2026-10-06, Alex's C64U, SRAM, CI run 439)

Calibration: ok on the first try. Bitslip position 4, read delay 5, tap 11 on both lanes, window 29 taps
(2.26 ns) on both lanes, cut by the 32-tap scan range (the real window is wider); two positions with a window.

Tester with the reset drive window 0xA (2 before, 2 after): 64.7 M error words in 7 rounds, all in the pingpong
pass (write then read of the same burst); first error burst 0: the upper four beats shifted by one beat. Without
the pingpong pass (ctl 0x1A): 84 rounds, 2.1 G commands, 0 errors, so reads, writes, masks, both orders and refresh
work. Sweep of oewin with the full tester (about 40 s each, ~16 rounds):

| after \ before | 0 | 1 | 2 |
|---|---|---|---|
| 0 | errors | errors | errors |
| 1 | 29,006 errors | 0 | errors |
| 2 | 0 | **0** | errors (old reset) |
| 3 | 0 | 0 | errors |

Two cycles of drive before a write's data reach back into the previous read's data on the bus; no drive after
the data cuts the write's last beats. The middle of the clean region, **0x9 (1 before, 2 after)**, is the new
reset value. The simulation model did not show this (it accepted 2 before): the bus turnaround on the board is
the real test. 4-hour run with 0x9 (log `~/trx64-ddr2-night/log2.txt`), 02:44-07:05, full tester (LFSR and sequential order,
write masks, pingpong pass, refresh at 7.47 us): **6,095 rounds, 0 error words**, 26 status reads all `0x1F`
(calibration ok, controller up, tester running), no recalibration. **Acceptance Q1 met** (at least 4 hours on
Alex's C64U). Not covered: a cold start series (several power-ons) and a really warm case; the window width of
each boot is logged once the boot ROM calibrates in c64_basic (step 6).

## 9. The swap into `c64_basic` (as built, 2026-10-06, branch `ddr2-swap`)

### 9.1 Calibration: the hardware sweep stays (option a), not the boot ROM (option b)

What depends on DDR2 being ready, read from the code: the RISC-V runs from the 8 KB boot BRAM (`bootrom_8k`) from the
end of configuration; the ROM (`software/bootrom/bootrom.c`, 2,893 bytes of 8,192) first spins on `0x10100100` bit 0 =
`ddr_ready` = `ddr_calib` through the two flip-flops `calib_cpu` (`top.v`), then does the DDR2 test, the magic and the
flash boot. Nothing else waits for DDR2 in the CPU; the rest of the chip holds `ui_rst || !ddr_calib` (`mig_arb`
`calib_done`, the lens unit, the engine, `pix_layer`). So (b) was possible: the CPU does run before DDR2 and the ROM has
5 KB left.

Chosen: **(a), `cal_seq.v` in hardware** (the sequence that ran 4 hours in `ddr2_test`), because
* it is the proven path: bit for bit the logic that calibrated on the board (the shift-register rewrite of §9.2 is
  checked against the board version by a bench, `cal_equiv`); (b) would be a new C port of 276 lines of state machine,
  new bus access from the 100 MHz CPU clock into `ddr2_lean`'s 75 MHz register block (a handshake in `ultimate_sys` or
  `trx_io`, both of which the benches model), and every iteration of it costs a bitstream/ROM build and a board visit;
* it needs no boot ROM change: `bootrom_pkg.vhd` is unchanged (the container build gives the same file; only comments
  of `bootrom.c` changed), the ROM's wait loop is the contract it always had;
* it does not depend on the CPU: a calibration result is there over JTAG even if the CPU or the ROM is broken;
* the cost is the sequencer's LUT: 889 LUT (yosys) as it was in `ddr2_test`, **376 after §9.2**; (b) would save those
  376 LUT (yosys) and cost the risks above. If LUT is ever the blocker, (b) is the way to get them; the register block
  is unchanged for it.

Fits: `cal_seq` runs on reset release of `ui_clk` (PLL locked, IDELAYCTRL ready, +32 cycles): init (about 17.7 k cycles,
the 200 us wait), 8 positions x 32 taps (one calibration read each, 30 cycles), evaluation: **0.37 ms** from the reset release
to `init_calib_complete` in the bench (`run.sh ctrl`, `CAL_RES ... at 372700000` ps). A wrong calibration is not retried: see §9.5.

### 9.2 Files

| file | change |
|---|---|
| `designs/c64_basic/ddr2_ctrl.v` (new) | `ddr2_lean` + `cal_seq` behind MIG's port list (minus the `app_sr/ref/zq` and temperature ports) and `cal_res[63:0]`; instance `ddr` in `top.v` where `mig` was |
| `designs/c64_basic/cal_seq.v` (moved from `designs/ddr2_test/`) | same sequence; the hit masks (10 x 32 FF, written through a 32:1 decoder, read through a 320:1 mux) and the ten results per (lane, a) are shift registers now: no write decoder, no read mux. **889 -> 376 LUT, 568 -> 359 FF** (yosys). `ddr2_test` uses the same file |
| `designs/ddr2_test/sim/cal_seq_ref.v`, `tb_cal_equiv.v` (new) | the board version, frozen, and a bench that feeds both the same random register-block answers (240 calibrations: windows of any start and length cut by the range, dropouts, spurious hits at other read delays, no window at all) and compares every output and every register write on every clock |
| `designs/c64_basic/top.v` | `mig_ddr2` -> `ddr2_ctrl`; `ddr_calib`, `ui_clk`, `ui_rst`, the `app_*` wires unchanged; JTAG `0x100050-0x100052` (below); comments (the five MMCMs: four are ours now) |
| `designs/c64_basic/trx_io.v` | `ddr_cal[63:0]` input, page 0 `0x020-0x027` |
| `designs/c64_basic/build.tcl` | the MIG IP step (`trx_ip mig_ddr2`, the `.prj` copy with `<TargetFPGA>`) is gone; `ddr2_ctrl.v ddr2_lean.v cal_seq.v` read; `read_xdc ddr2_pins.xdc` |
| `designs/c64_basic/ddr2_pins.xdc` (new) | the DDR2 pins of `ddr2_test`'s `pins.xdc`, unchanged and now shared by both builds: the 46 balls, DQ/DM `HSTL_II_18`, DQS `DIFF_HSTL_II_18`, CK/CK#/addr/ctrl `HSTL_I_18`, `INTERNAL_VREF 0.9` bank 35, `SLEW FAST`, A13 (F4) `HSTL_I_18` held low (was `SSTL18_II`), the pads and `lk_s` false paths |
| `designs/c64_basic/pins.xdc` | the A13 lines moved; the clock group of `mmcm_ddr/CLKOUT1` `-include_generated_clocks` now carries `clk300` and `ui_clk` of `ddr2_lean`'s PLL (MIG's was inside the same group) |
| `tools/trxdbg.py` | `ddr2` subcommand and a line in `regs` |
| `designs/c64_basic/sim/mig_ui_model.v` | header: it is the `app_*` stand-in, no longer the controller; no bench depends on the difference (code untouched) |
| `software/bootrom/bootrom.c`, `ultimate_sys.vhd`, `ddr2_port.v`, `T15-flash-boot.md` | comments that said "MIG calibration" |
| `constraints/mig_ddr2.prj` | **stays**: `tools/pincheck.sh/.tcl` read its PADName list to check the pins against Gideon's XDC; nothing builds it any more (`ddr2_test` does not, `c64_basic` does not) |

Clocks: MIG's PLL and MMCM are gone; `ddr2_lean`'s `PLLE2_BASE` takes `clk200` (`mmcm_ddr/CLKOUT1`, BUFG, as in
`ddr2_test`; the same MMCM now also gives the audio MCLK and the CPU's 100 MHz, VCO 1000 MHz as before), IDELAYCTRL
inside `ddr2_lean` on `clk200`. Reset contract: `ddr2_ctrl` is wired exactly like the MIG was: `sys_rst = ~clk200_locked`,
`ui_clk_sync_rst` = `ui_rst`, `init_calib_complete` = `ddr_calib`; every `ui_rst || !ddr_calib` user is untouched.

### 9.3 The calibration result: where to read it

`cal_res` (`ddr2_ctrl.v`; bits are the register map of `ddr2_test` 0x02/0x03 plus flags), static once CAL0 bit 29 is set:

| word | bits |
|---|---|
| CAL0 | [4:0] IDELAY tap lane 0, [12:8] lane 1 (78 ps), [18:16] read delay, [27:24] bitslip position (on failure the position reached), [28] init done, [29] calibration done, [30] calibration ok, [31] controller up (`init_calib_complete`) |
| CAL1 | [5:0] window width lane 0, [13:8] lane 1 (taps), [16] / [17] window cut by the 32-tap scan range, [23:20] bitslip positions that had a window for lane 0 |

* JTAG: `0x100050` = `DD2C0001` (id; older bitstreams read 0), `0x100051` CAL0, `0x100052` CAL1;
  `tools/trxdbg.sh ddr2` prints it (exit status 1 when not ok), `tools/trxdbg.sh regs` has a one-line summary.
* App: `trx_io` page 0 `0x101A0020-23` CAL0, `0x101A0024-27` CAL1 (byte reads, little endian). Older bitstreams read 0.
* A ddr2_test board value for comparison (§8): position 4, read delay 5, tap 11/11, window 29/29 taps cut by the range:
  CAL0 `F4050B0B`, CAL1 `00231D1D` (the same values by the encoding of §9.3). Every boot logs its window width here.
* No synchronizer on the way (static word): the readers in `sys_clk` / `cpu_clk` see changing bits only while the
  calibration still runs; the app and the ROM are past that point (the ROM waits for `ddr_ready`).

### 9.4 Benches (iverilog, `nice -n 15`, this commit; `ci/sims.txt`: `ddr2_ctrl`, `cal_equiv` added)

`run.sh ctrl` (`ddr2_ctrl` as `top.v` instantiates it; with a DRAM model, then with no DRAM on the pins):
```
CAL_RES f0030f0f 00332020  done 1 ok 1 calib 1 at 372700000
PASS ddr2_ctrl: calibration ok (res f0030f0f 00332020), 600 words written and read back, 0 mismatches
CAL_RES 37000000 00000000  done 1 ok 0 calib 0 at 371900000
PASS ddr2_ctrl: no DRAM -> cal_done 1, cal_ok 0, init_calib_complete 0, app_rdy 0, result word 37000000
```
`run.sh equiv`: `PASS cal_equiv: 240 random calibrations (68 ok, 172 without a window), 7028365 clocks compared, 0 mismatches`
(a mutation of the result slot, `chv[0] <= cho ^ 1`, gives 6,969,886 mismatches: the bench bites).
`run.sh cal`, `refresh`, `test`, `traffic`, `arb` with the shift-register `cal_seq`: the six `CAL:` lines and every `PASS` line are those of §7.7
(same taps, read delays, positions, widths, command and refresh counts, e.g. `PASS ddr2_lean traffic: wr_dly 500 rd_dly 500, 100000 commands, 0 mismatches, 804 refreshes`,
`PASS ddr2_test: calibration, 8 rounds ...`, `PASS mig_arb on ddr2_lean: functional list and mixed load, 0 errors`).
Also PASS: `trx_io` (new check of DDR_CAL0/1), `cart_unit` (145 checks), `mig_arb` (on `mig_ui_model`), `ring_f4`, `dbgsess`.
Not run here: ultsys, statebus, input, pixlayer and the other Verilator/ghdl benches (none reads a changed file except through
the `trx_io` port, comments, or `build.tcl`'s list); CI tier 2 runs them.
 No bench instantiates `top.v` (all use their own wrappers around the units, with `mig_ui_model`): the
top-level wiring itself is only checked by the syntax lint and by the Vivado build; `ddr2_ctrl` is checked by `run.sh ctrl`
and the real `mig_arb` on `ddr2_lean` by `run.sh arb`.

### 9.5 What happens when the calibration fails, and the first boot's risks

* No window at any bitslip position, or the DQS capture never valid: `cal_seq` ends in `S_FAIL`: `cal_done` = 1,
  `cal_ok` = 0, CAL0 bit 30 = 0, `init_calib_complete` stays 0. **The machine hangs by design**: the boot ROM prints
  `DDR2: ` on the console and spins on `ddr_ready`; `mig_arb` and everything on `ui_clk` stay in reset; the C64 has no ROMs
  (they come from the app in DDR2); a JTAG DDR2 access would never complete. **No retry and no fallback** (the sweep is
  deterministic: it would fail again; a cold power-cycle or a new bitstream load starts it again). The JTAG registers are in
  `sys_clk` and work: `trxdbg ddr2` says "FAILED (no window found)" with the position it stopped at; PLL or IDELAYCTRL never ready
  reads as CAL0 = 0 (init not done: the sequencer is still in reset).
* A "successful" calibration on a marginal window: the width is in CAL1 (§8: 29 taps cut by the range, the DQS pulse is about
  34 taps); the choice rule accepts windows of at least 10 taps cut by the range, 12 taps whole.
* What the benches cannot show (§7.10 still stands, now inside the full chip): first Vivado run of `ddr2_lean` at 99.95 %
  slices and with 75 MHz logic around it (timing of `ui_clk` paths into `mig_arb`, two BUFGs (`clk300`, `ui_clk`) and one PLL in place of what the MIG used),
  placement of the 44 OSERDES / 18 ISERDES / 18 IDELAYE2 against the rest of the design, the shared MMCM's `clk200` jitter next to
  the fractional audio MCLK output; **video/ring bandwidth and latency through a 2-entry in-order controller instead of the
  MIG's 32 in flight** (the `arb` bench checks data, not deadlines; spec §2 estimates 3x headroom); the cold-start series and a
  really warm case (§8 did neither); the address map differs from MIG's (banks interleave on the low burst bits), which only matters
  to anything that assumed bank-major locality.
* `tools/pincheck` compares `c64u.xdc` with `mig_ddr2.prj`, not the new `ddr2_pins.xdc`: the pins are byte-identical to
  `ddr2_test`'s (which ran on the board), nothing else checks them.

### 9.6 Size (yosys `synth_xilinx -flatten`, estimates; Vivado packs differently and counted 3,076 LUT for the MIG in c64_basic)

| | LUT | FF | CARRY4 |
|---|---|---|---|
| `ddr2_lean` alone | 595 | 1,056 | 30 |
| `cal_seq` before §9.2 / now (alone) | 889 / 376 | 568 / 359 | 32 / 28 |
| **`ddr2_ctrl` (lean + cal_seq, flat)** | **825** (+4 MUXF) | 1,350 | 58 |
| the MIG, as counted by Vivado (CI run 432) | 3,076 | - | - |

About **-2,250 LUT** against the MIG if Vivado lands near yosys (the real number comes from the CI build). Without §9.2 the
sum would be 1,360 LUT.


## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

OPT-ddr2: Own lean DDR2 controller instead of MIG

Milestone M1, status 🔨. Open (one line, as in the TODO table): –

Decisions: [ADR-0022 Own lean DDR2 controller instead of MIG](../adr/0022-d06-own-ddr2-controller.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- **Board 2026-10-08 morning (Claude alone, CI run 514): 30 min REU load passed.** Own test program at 64 MHz: 256-byte stash + fetch + compare over 1 MB of REU, about 1,300 passes/s; 2,393,030 passes (about 1.2 GB), 0 errors. HDMI picture not observed; ring off during the run (A08).
- **2026-10-07: DONE (Alex: "reicht") - the 4-hour ddr2_test run plus the day's c64_basic loads (all "ok, window 29 / 28 taps") are enough; cold-start series and warm case dropped ([ADR-0045](../adr/0045-ddr2-done-without-cold-start-series.md)).**
- Task text: Own lean DDR2 controller instead of MIG (docs/specs/OPT-ddr2.md, decision 6): board 4-hour run 6,095 rounds 0 errors (§8); swap into c64_basic (`ddr2_ctrl` = `ddr2_lean` + hardware sweep `cal_seq`, §9).
- **In main 2026-10-06 (ba060ae; MIG removed, `mig_ddr2.prj` stays for pincheck).** Runs on the board: Alex's 2026-10-06 test (REU 16 MB, Timewarp, video, cartridges) passed on it. **Open (spec §7.10/§9.5):** the cold-start series and a really warm case (§8 did neither), not reported on the board; timing/LUT of the newest integ-m1 (row T29).
