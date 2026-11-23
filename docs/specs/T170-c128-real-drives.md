# T170 — The C128 core works with real drives on the board's serial (IEC) port, 1541 and 1571 with fast serial

**Status:** IN PROGRESS (2026-11-23). Built 2026-11-23 (agent, branch `t170-c128-iec`): the fast serial wiring, the SRQ line on the real port, the bus trace and its place in the bug report. Open: the builder benches, the Vivado run, the testers' board steps (D2).
Open (one line, as in the TODO table): IN PROGRESS 11-23: built (fast serial, SRQ, bus trace in report); open: builder benches, Vivado, testers' drives (D2)
**Repo:** TRX64U-MultiCore — `cores/c128/` (large core), the generic core application (`coreapp/src/`), `tools/` (contract check).
**Number:** T170 (TODO table).
**Origin:** Alex, 2026-10-31, at the Sony TV: "we should support the analogue ports for the floppies (serial port)" (translated); the room for it
comes from T140 D24. Alex, 2026-11-23 (translated): "C128: its own agent please, or several; it should do all of it, only not the black/white."
**Depends on:** T140 D24 (room in the C128 large core), T162 (the bug report).
**Milestone:** M3.

---

## §1 The problem

- P1 `cores/c128/mono/top.v` joins the C128, the app's drive A and the board's IEC port; whether a real 1541 on the port loads, and with which drive
  number next to drive A, was never tested on the board.
- P2 A real 1571 talks fast serial (burst) with the C128: SRQ as the clock and the CIA 1 serial register as data. The port has `IEC_SRQ_I` / `_O`
  (`host/c64u.xdc:460,465`). Before this work the core did not use them: both CIAs had `sp_in` and `cnt_in` tied to 1 (`c64_top_c128.v`), `IEC_SRQ_O` was tied to 0 and
  the top had no `IEC_SRQ_I` port. The KERNAL takes a fast byte when `$DC0D` bit 3 is set (T140 §5, "Other finding": ACPTR at `$E486-$E4B5`), so a 1571 burst could not complete.
- P3 The core is at 85 % LUT and (estimated) 61 of 75 RAMB36 after T140 D25/D27: anything added needs room first (T140 D24).
- P4 Nobody on the team has a drive on the bench (D2). When a tester's load fails, nothing records what the bus did.

## §2 Scenarios

Proof kinds: **bench** = a bench of `ci/sims.txt` on the builder (the coordinator runs it); **board** = a step for a tester with a real drive (D2; the checklists of
T122 / T166); **unit** = a native test. Negative controls only for flash and persistence (T174 D5.4, Alex 2026-11-08); this work has neither, so the "SRQ cut"
control of the first draft is not required and is not built.

```gherkin
Scenario: S1 a real 1541 on the port, C128 mode
  Given a real 1541 on the board's serial port as device 8 (drive A switched off) or 9, a disk in it
  When  LOAD"$",8 (or 9) and LIST are typed, then a program is loaded and run
  Then  the directory lists, the program loads and runs
  # board (tester, D2). Regression of the slow serial path in the core: bench c128_machine, program "iec" (sh cores/c128/sim/run_machine.sh iec c64mode boot)

Scenario: S2 a real 1571 loads with fast serial, faster than slow serial
  Given a real 1571 on the port, a disk with a long file (about 100 blocks)
  When  the file is loaded with the drive in 1571 mode and again in 1541 mode (OPEN15,8,15,"U0>M0": slow serial)
  Then  the burst load works and takes clearly less time than the slow one; the bug report's trace counts SRQ edges (the pin and the line reach the core)
  # board (tester, D2: both times to the issue). The wiring: bench c128_iec (every input of the wiring against the MEGA65 equations; SRQ from the port reaches the core and
  # not drive A; the core's SRQ pull reaches the port). The CIA: bench c128_iec_cia (a stand-in sends 0xA5 on DATA with SRQ as the clock: SDR 0xA5, ICR SP and FLAG set; the CIA sends 0x5A: eight SRQ edges, DATA sampled on them)

Scenario: S3 drive A and a real drive on different numbers
  Given drive A with a disk as device 8 and a real drive on the port as device 9
  When  LOAD"$",8 and LOAD"$",9
  Then  each lists its own disk; neither disturbs the other
  # board (tester). Bench c128_iec: a pull of the board's DATA reaches the core, drive A and the port as one wired AND (the bus part)

Scenario: S4 the bus trace is in the bug report
  Given a load with a real drive (S1 or S2), good or failed
  When  F8 -> Bug report is chosen right after it
  Then  the file has a section "Serial port trace (T170)": the totals, the edge counts of ATN, CLK, DATA and SRQ, the state now, and the newest 96 events with their time
        and the fast serial direction; a core without the page has no section
  # unit: test_iectrace (the page found by "IECT", the events in order, saturated time, a wrapped ring, the counters from one snapshot); test_corereport (the section only with
  # the capability "iec" and the page). Gateware: bench c128_iec (the page: events and their times, a burst the ring leaves out and the counters do not, the wrap, snapshot, clear). board (tester): the file of S1 / S2 on the SD card

Scenario: S5 a C128 in C64 mode and the other cores are not disturbed
  Given the C128 in C64 mode (GO64) with a real 1541, and another core with a real port started afterwards
  Then  C64 mode loads as before (FSDIR in C64 mode never routes the bus to CIA 1); the other core's report has no trace section
  # bench c128_iec (C64 mode: CNT and SP in stay high), bench c128_iec_cia, c128_machine "c64mode"; unit test_corereport (a core whose page does not answer "IECT")
```

## §3 Decision

- **D1** (Alex, 2026-10-31): "NO 1571 on the C128 as an emulation." (translated) Only real drives on the port get fast serial; the
  emulated drive A stays a 1541.
- **D2** (Alex, 2026-10-31): "I have no drives in hardware. The testers have to test that, and we may need dumps/logs." (translated)
  Proof on the board comes from the testers (T122, T166 checklists). The core therefore records what happened on the bus: a trace of
  ATN / CLK / DATA / SRQ edges with timestamps and the FSDIR state, written by the core app into the bug report file (T162), so a
  tester's failed load can be read here without the drive.

The design below was chosen while building (agent, 2026-11-23); Alex's decisions are D1 and D2 only.

- **D3 Wiring.** The equations of the MEGA65 C128 core (`fpga64_sid_iec.vhd:1189-1202`, PROVENANCE.md) in `cores/c128/c128_iecfs.v`: FSDIR ($D505 bit 3, the MMU's `fsdiro`, now an output of
  `c128_map` and the arbiter) = 1: CIA 1's CNT drives SRQ, its SP pulls DATA; FSDIR = 0 in C128 mode: the bus's SRQ is CIA 1's CNT, the bus's DATA its SP; FLAG of CIA 1 also sees SRQ. `c64_top_c128.v`
  ties them (`cia1_*`); CIA 2 stays as it was (user port). Polarity of the core's bus (1 = released) is kept.
- **D4 The line on the port.** `cores/c128/iec_bus_c128.v` is a copy of `iec_bus_c64u.v` with SRQ (in and out); drive A has no SRQ. `IEC_SRQ_O = 1` pulls low like the other `_O`; `IEC_SRQ_I = 1` while low.
  The ball K14 is constrained in `cores/c128/mono/iec_srq_pins.xdc` (the host's `pins.xdc` is not touched). `layer2_c128.v` gets the two ports beyond the contract's 247: the flat build has no partition, so
  the core's descriptor says so (`layer2_extra` in `cores/c128/mono/core.toml`, a field of `tools/core_toml.py`), and `tools/contract_ports.py` checks that the 247 are unchanged and the extra ones are exactly those named.
- **D5 The trace.** `cores/c128/c128_iectrace.v`: a ring of 1024 events of 32 bits (one RAMB36), written in `sys_clk`: [31:16] ticks since the event before (saturating), [9] C128 mode, [8] FSDIR,
  [7:4] the board port's lines, [3:0] the bus as the core hears it. Every change of ATN, CLK, FSDIR or the mode is an event; DATA and SRQ changes too, except while SRQ moves (changed less than 256 ticks = 8 us ago, a burst): the
  first edge and the state after the quiet are events. Counters of every edge of the four lines (32 bit) count what the ring leaves out. Without that rule one 20 KB burst would overwrite the ring many times
  over; with it the ring holds the last about 1000 slow events or the last 250 bytes' handshakes, which is where a hang shows. Registers: a page of its own at `cw_addr` 0xF000 (page F of the core window), in `cpu_clk`,
  read after a snapshot (a toggle handshake) so the 32-bit values do not tear; text of record: the header of `c128_iectrace.v`.
- **D6 The application.** Generic, no core data (T192 D24): `coreapp/src/iectrace.{h,cc}` is called by `corereport.cc` when the descriptor has the capability "iec" **and** the page answers the ID "IECT" at
  0xF000; it prints the newest 96 events (about 4 KB of the 28 KB report). `tools/check_d24.py` is green; `coreapp/cores/c128.cc` is not touched. Another core can offer the same page.
- **D7 Time base.** The ticks are the core's `sys_clk`; the page reports it (`TICK_HZ`: 31,527,778 PAL, 32,727,273 NTSC from the System Mode - the nominal values, the NTSC value is the VIC's clock from `vicii_c128.v`, not measured).

## §4 Out

- An emulated 1571 (D1).
- A clear of the trace by the application (the register exists for benches and JTAG; the report only reads).
- The trace of drive A's internal bus (it is inside the core; the trace is of the lines at the core's edge, including drive A's pulls, as the C128 hears them).
- CIA 2's SP / CNT (user port: not on the board).
- A separate 1581 proof: a real 1581 uses the same wiring (burst), but only the 1541 and 1571 are in §2.
- A negative control (SRQ cut): not a flash or persistence operation (see §2).

## §5 Built

- **2026-11-23, branch `t170-c128-iec` (agent; nothing pushed, no Vivado run, no board).**
  - Gateware: `c128_iecfs.v`, `iec_bus_c128.v`, `c128_iectrace.v`; `c128_map.v` / `c64_bus_arbiter_c128.v` export FSDIR; `c64_top_c128.v` / `c64_core_c128.v` carry SRQ and FSDIR; `layer2_c128.v`
    (ports, bus, page F); `mono/top.v` (port `IEC_SRQ_I`, `IEC_SRQ_O` from the core, off with `core_decouple`), `mono/iec_srq_pins.xdc`, `mono/top.v.diff` regenerated (it was one line stale); `core_sources.tcl`.
  - Application: `coreapp/src/iectrace.{h,cc}`, `corereport.cc` (the section before "Heap"), the RISC-V Makefile.
  - Tests: `cores/c128/sim/tb_iec.v` (iverilog, **PASS, 38 checks, run here once**), `tb_iec_cia.sv` (Verilator: **written and linted, not run here** - first run is the coordinator's, `sh cores/c128/sim/run_iec_cia.sh`; if a check fails, look at
    the bench's timing first), `tests/coreapp/test_iectrace.cc` (24 checks, PASS) and a block in `test_corereport.cc` (94 checks, PASS). Both benches are in `ci/sims.txt`, `sims.deps`, `sims.lane`.
  - Checks green: `tools/check_todo.py`, `check_d24.py`, `check_c128_tree.py` (and its selftest), `core_toml.py --check`, `contract_ports.py --check` (c128: 247 ports, 2 extra), `ci/orchestrate.py check`; yosys parse of every changed Verilog file (no new implicit net).
  - Bug found by the bench while building: the trace tap of the port's lines had SRQ and ATN swapped; fixed (`iec_bus_c128.v`).
- **Cost estimate (no Vivado; the last numbers are T164 §5 WP18: 27,670 LUT = 84.9 % in run 1079, about 27,900 LUT = 85.6 % and 61 of 75 RAMB36 estimated since, WNS -0.095 in run 1142).**
  `c128_iectrace`: about 350 LUT (four 32-bit edge counters about 130, the register read mux about 70, event logic and the 16-bit delta about 60, snapshot control and CDC about 40, the rest glue), about 480 FF
  (counters 128, snapshot 170, the rest), **1 RAMB36** (1024 x 32), 0 DSP. `iec_bus_c128` +8 LUT, +14 FF; `c128_iecfs` about 6 LUT; routing of FSDIR and SRQ about 10 LUT. **Total about 380 LUT (+1.2 % of the 32,600), about 500 FF, 1 RAMB36.**
  Next to T257 (the estimate in the research report: 60-100 LUT, about 10 FF, no new BRAM tile): about 28,400 LUT = 87 %, 62 of 75 RAMB36. It fits by these numbers; it is not a placement
  guarantee - the C128 was red on WNS in run 1142 with this density, and Vivado decides. If it does not close: drop the 32-bit counters to 16 bit (about -100 LUT, -64 FF), then the snapshot of the DATA counter.
- **Risks to look at in the first Vivado run and the first board run:**
  - `IEC_SRQ_I` is a new input (K14, LVCMOS33, no pull): its idle level on the board with no drive is not known from the boundary scan. A line stuck low means no FLAG edge and no CNT clock, nothing worse.
  - The report reads page F of the core window on every core that has the capability "iec" (the TRX64U, SCPU, C264, VC20 too) to find the ID bytes; a core whose window does not acknowledge unknown pages would stall there.
    Cores built on the c64u window acknowledge every access that is not a page of its own one clock later and read 0 (`cores/c64u/c64u_io.v`, header); the C264 and VC20 windows were not read for it.
  - The trace has not been seen with a drive. Whether the burst rule (8 us) fits a real 1571's bit clock is a guess from the protocol: the counters are right either way; only the ring's content changes.
- **Open:** builder benches (`c128_iec`, `c128_iec_cia`, `c128_machine iec c64mode boot`), the Vivado run, the testers' board steps S1-S3 with a real 1541 and 1571 (D2), the checklist lines in T122 / T166.
