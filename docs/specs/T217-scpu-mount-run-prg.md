# T217 — Start a .prg on the SuperCPU and C128 cores: from the stick and from inside a disk image

**Status:** IN PROGRESS (2026-11-15; PROPOSED 2026-11-12). P1, P2, P3 (128 mode), P4 built; the C64-mode half (D4) is built; the C128 board steps S7, S8 and S10 closed as done 2026-11-18 (shipped and in use, §5); open: the SCPU benches S1-S4 and S6's job (= S1) in the §5 table (S1 has a board pass, 2026-11-15).
**Repo:** TRX64U-MultiCore — `coreapp/cores/c64scpu.cc`, `coreapp/cores/c128.cc`, the shared `cbm_run` job and the PRG library (`app/loaders/cbm_prg.*`).
**Number:** T217 (registry: the table in `TODO.md`).
**Origin:** found while building T206 (SCPU Run Disk, decision A, branch `t206-scpu-run`); Alex, 2026-11-12: "own task" (translated)
  to the question whether the PRG case goes into T206.
**Depends on:** T206 (key-matrix typing for the SCPU, `CoreIo::matrix_keys`).
**Milestone:** M2.

---

## §1 The problem

Alex, 2026-11-15 (translated): "Gap in the SuperCPU menu (and other cores too?): I cannot start a .prg ... how does that work?" - then agreed ("Ja bitte") to extend
this task to "start a .prg on SCPU and C128, directly from the stick and from inside a disk image". The parts:

- **P1** direct .prg on the SuperCPU: the file list had `.d64`/`.d81` with an inner `.prg` and no `.prg` row (PET, VIC-20, C16/Plus4 have "Load" and "Load + run").
- **P2** (the original problem) Mount & Run of a .prg inside a disk image on the SuperCPU waits for READY but does not reach the 65816. Cause (read from code):
  `cbm_run_service` read TXTTAB `$2B` through the RAM window (`ram_read`) and `cbm_prg_place` wrote the program, VARTAB/ARYTAB/STREND (`$2D/$2F/$31`) and
  `cbm_prg_type_start` the keyboard buffer (`$0277`, count `$C6`) through the RAM window. The window is the C64 RAM; the 65816 reads bank 0 only from its own SRAM
  (`scpu_mem.v` `d_rk = RK_MEM, d_rp = sram(...)`, the DDR2 at 0x03000000). The only link between the two is the mirror: a CPU write to bank 0 in the pages of `mir_tab`
  (reset `optim` 0xC7 gives `$02-$FF`) also goes to the C64 RAM - one way, CPU to window (`scpu_mem.v` `d_mir`); an application write never reaches the SRAM, and page 0
  is not mirrored at all, so `$2B` read back as the cold-start fill, BASIC never "came up", and after 5 s the notice "did not reach READY" came.
- **P3** direct .prg on the C128. 128 mode: BASIC 7 text at `$1C01`, TXTTAB `$2D`, VARTAB/ARYTAB/STREND `$2F/$31/$33`, keyboard buffer `$034A`, count `$D0`. C64 mode: as the C64
  (`$0801`, `$2B`, `$0277`/`$C6`) - a reset always starts 128 mode, so a C64-mode program needs the mode switched first (open question 1).
- **P4** Mount & Run of a .prg inside a disk image on the C128 (`run_op` was empty: the row was not offered).

## §2 Scenarios

Proof of the gateware side is a bench with the REAL ROMs (local, never in CI, ROMs from the directory in the environment; never committed). The placement is made by
the same code the board runs (`app/loaders/cbm_prg.cc`, compiled natively into the bench's writer), the machine runs the real KERNAL and BASIC, and the marker is a byte the
program itself pokes (C64 RAM `$C000`, a mirrored page, so the window sees it) and the screen.

| # | Scenario | Proof |
|---|---|---|
| S1 | SCPU: a BASIC .prg (`10 POKE 49152,42`) placed as the app does (SRAM through the DDR2 window + cache flush) and started with typed RUN: the 65816 runs it | bench `c64scpu_prgrun`: marker `$C000` = 42 |
| S2 | SCPU negative control: the same placement through the RAM window only (the old way): the program is NOT run | bench `c64scpu_prgrun` control: marker stays 0 |
| S3 | SCPU: a machine-language .prg (SYS) placed at `$C000` runs | bench `c64scpu_prgrun` ML case |
| S4 | SCPU: the BASIC pointers the app wrote (`$2D/$2F/$31`) are seen by the 65816 (LIST / RUN finds the program, `$2D` = end of program) | bench `c64scpu_prgrun`: SRAM page 0 after the run, screen shows READY after the program |
| S5 | SCPU: `.prg`/`.p00` rows "Load" / "Load + run" in the file browser | ue2emu pictures, UX review |
| S6 | SCPU: Mount & Run of a .prg in a .d64/.d81 uses the same job (inner row, `run_op`) | ue2emu picture of the inner row; S1 proves the job |
| S7 | C128 128 mode: BASIC .prg at `$1C01` placed with the app code and typed start (keyboard buffer `$034A/$D0`) runs on the real C128 ROM | bench `c128_prgrun`: marker |
| S8 | C128: ML .prg runs by SYS | bench `c128_prgrun` |
| S9 | C128: file rows `.prg`/`.p00` and the inner Mount & Run on `.d64`/`.d81` | ue2emu pictures |
| S10 | Given the C128, a C64 BASIC .prg ($0801); When "Load + run" (file browser, .prg/.p00) or Mount & Run inside a .d64/.d81; Then the machine goes to C64 mode (GO64) and the program runs there (a "Load" of it stays refused with a reason) | board step (below); ue2emu's c128 profile is a register stub (tests/coreapp/emu/profiles/c128.toml) without the machine, so no picture of a running program exists |

## §3 Decisions

- D1 (Alex 2026-11-12, "own task", translated): the PRG case is not part of T206; it is this task.
- D2 (Alex 2026-11-15, "Ja bitte", translated): the task covers P1-P4 as above.
- D3 (design, follows the code): on the SuperCPU the program, the BASIC pointers and the TXTTAB read go to / from the 65816's SRAM through the DDR2 window
  (`SCPU_SRAM_WIN_OFF` = 0x02000000, the window offset of 0x03000000), and also to the RAM window (the C64 side shows it); the CPU's write-through cache is
  invalidated afterwards (ROM_CTRL.b0 flush, as for the ROM images). The start (RUN / SYS n) is typed through the key matrix as in T206 (READY from the screen), not through
  the keyboard buffer. No gateware change.

## §4 Out

- No tape, no cartridge-style start.
- D4 (Alex 2026-11-15, question 1 "A"): a C64 program opened on the C128 (load address `$0801`) switches the machine to C64 mode (GO64) and starts the .prg there; S10 becomes a scenario to build. A non-`$0801` file keeps the 128-mode path.

## Open questions for Alex

1. (answered: D4) C128: a C64 program (BASIC at `$0801`, or a C64 ML file) needs C64 mode, and a reset always starts 128 mode. How should a program start reach C64 mode: (a) type `GO64` and `Y` through the key matrix after READY, then place and run as on the C64, (b) a setting that makes the machine start in C64 mode, (c) refuse (current behaviour, with a message)?

## §5 Build notes

### Build notes (2026-11-15, first session)

- **P2 cause (code):** `coreapp/cores/cbm_run.cc` `cbm_run_service` (old: `ram_read(m.prg.txttab)`), `app/loaders/cbm_prg.cc` `cbm_prg_place` (pointer and program writes) and
  `cbm_prg_type_start` (`$0277`/`$C6`) all went through the RAM window = the C64 RAM. The 65816 reads bank 0 from its SRAM (DDR2 0x03000000): `cores/c64scpu/scpu_mem.v:171-180`
  (`d_rk = RK_MEM, d_rp = sram(...)`); the only link is the CPU-to-window mirror `scpu_mem.v:148-168` (`mir_tab`, reset `optim` 0xC7 = pages `$02-$FF`) and `:220` (`d_mir`).
  Page 0 is not mirrored and an application write never reaches the SRAM. Fixed in the application, no gateware: `CbmPrgParams.mem_ddr` (`c64scpu_prg.h`, `SCPU_SRAM_WIN_OFF`)
  sends program, pointers and the TXTTAB read through the DDR2 window, `mem_sync` (`cbm_run.cc`) flushes the write-through cache (`scpu_cache.v`, ROM_CTRL.b0), the start is typed
  through the key matrix (`place_and_type`).
- **Built:** P1 `.prg`/`.p00` rows (Load, Load + run) on the SuperCPU; P2 as above; P3 `.prg`/`.p00` rows on the C128 (128 mode, keyboard buffer path, C64 BASIC at `$0801` refused);
  P4 `run_op` and the inner rows on `.d64`/`.d81` of the C128. `tools/dev.sh riscv` builds (run 2026-11-15, rc 0).
- **Benches (local, real ROMs, never in CI):** `cores/c64scpu/sim/run_prgrun.sh` (cases basic, ml, old = control), `cores/c128/sim/run_prgrun.sh` (basic, ml, none = control), writer
  `tools/cbm_prg_sim.cc` (the application's own library code and parameter headers), tb replay `+PRG_AT`. Test programs: `cores/c64scpu/sim/prgrun_gen.py`, and its copy `cores/c128/sim/prgrun_gen.py` for the C128 bench (own copy per core, review of integ-1116).
- **Pictures:** ue2emu, `tests/ui/features_cores/t217_prg_c128.feature`, `t217_prg_c64scpu.feature` (commit 7ef47665f); UX review PASS, minor wording note: "Load + run" against "Mount & Run".

### Status at hand-over (2026-11-15; Alex stopped the benches: "no benches before a merge")

| # | Scenario | Evidence |
|---|---|---|
| S1 | SCPU BASIC .prg runs on the 65816 | OPEN: bench `run_prgrun.sh basic` written, not finished (one run reached READY with TXTTAB `$0801` in the SRAM, but the replay parse was wrong then and is fixed; the run was stopped). Board step for Alex. |
| S1 board | SCPU .prg "Load + run" from the file browser | board PASS 2026-11-15 (Alex, translated: "start PRG in SUPER CPU works!"), c64scpu.cor f9e6656c (run-1965 bitstream + this app) |
| S2 | SCPU old way does not run it (control) | bench `run_prgrun.sh old`: PASS line seen, but the replay applied 0 bytes at that time, so it proves nothing; open |
| S3 | SCPU ML by SYS | OPEN (bench written, not run) |
| S4 | SCPU pointers seen by the CPU | OPEN (second marker `PEEK(45)` in the basic case) |
| S5 | SCPU `.prg`/`.p00` rows | ue2emu pictures, ux-review PASS (`t217-c64scpu-prg-sheet.png`) |
| S6 | SCPU inner Mount & Run | inner rows picture PASS; the job itself = S1 (open) |
| S7, S8 | C128 BASIC / ML .prg | bench PASS 2026-11-16 (`run_prgrun.sh basic`, `ml`, see "Board 2026-11-16"); board closed as done 2026-11-18 (last entry) |
| S9 | C128 rows + inner Mount & Run | ue2emu pictures, ux-review PASS |
| S10 | C128 C64-mode program | bench PASS 2026-11-16 (`run_prgrun.sh go64`); board 11-16 failed (GO64 Y), fixed, board PASS 11-16 in 40/80 columns (the "D4 board" line of §5), the rest closed as done 2026-11-18 (last entry) |

`tools/benches.sh c64scpu -f` on the commit with the new tb plusargs: emu, map, speed, mirror, irq, mdust PASS; `c64scpu_base` FAIL after 10 s in that run while memory was exhausted
(cc1plus segfault), and PASS standalone (`run_base.sh`). The C128 set was not run. Not checked: the SCPU's real TXTTAB/READY timing on the board, the 5 s wait of
`ready_ms`, the C128 keyboard buffer numbers (`$034A`/`$D0`) and RAM-window coherence with the 8502's line cache on the board; no native app test was added (pipeline).

### S10 build notes (2026-11-15, D4)

- **How the switch works (phases 0-3 as built 2026-11-15; phases 1, 4, 5 and the READY / question waits replaced 2026-11-16, see the last section)** (`coreapp/cores/cbm_run.cc` `service_c64`, set up by `CbmRunMachine::c64_load` etc. in `c128.cc` `machine()`): `cbm_run_queue` sets `CbmRun.c64` when the file's load
  address equals `c64_load` (`$0801`) and skips the refuse hook for that file; the machine is reset (it comes up in 128 mode). Phase 0: TXTTAB `$2D` = `$1C01` (the 128-mode READY, `c128_prg_params`), settle 500 ms,
  then `GO64\r` goes into the keyboard buffer `$034A/$D0`. Phase 1: 400 ms after the buffer was taken, `Y\r` (GO64's ARE YOU SURE? answer; the RETURN is read as a line or left in a buffer the reset forgets). Phase 2: wait for
  `C128_STATUS` b0 (`C128_REG(0x030)`, "C64 mode", an existing register; no new one). Phase 3: at least 1500 ms after the switch, TXTTAB `$2B` = `$0801` (`c128_prg_params_c64`) and the screen at `$0400` shows READY alone,
  settle 500 ms, then `cbm_prg_place` + `cbm_prg_type_start` with the C64 numbers (keyboard buffer `$0277/$C6`, RUN or SYS), as the C64 core does. Every wait has a give-up with a notice.
  `Load` (no start) of a `$0801` file stays refused ("use Load + run"); a file at any other address keeps the 128-mode path (D4). `max_size` of the 128 parameters now spans `$0801`.
- **Evidence:** `cbm_run.cc` and `c128.cc` pass a native `clang++ -fsyntax-only` (host headers). `tools/dev.sh riscv` was queued 2026-11-15 but the builder's app slots wait behind a running Vivado (large:amiga1200), so the RISC-V build result was OPEN then. Result 2026-11-16: `tools/dev.sh riscv --branch t217-prg-run` rc=0 (hostapp + coreapp link, no fix needed); behaviour stays a board step. ue2emu has no running C128, so no picture of the program.
- **Not checked, board step for Alex (closed as done 2026-11-18, §5):** (1) copy a BASIC .prg (`10 PRINT"HELLO":GOTO 10` at `$0801`) to the stick, C128 core, F3 file browser, "Load + run": the screen goes to C64 mode and the program runs;
  the same for a .prg inside a .d64 (Mount & Run). (2) Unverified numbers: whether GO64 takes Y without RETURN or needs it, the 400 ms gap, the 1500 ms `c64_ready_ms`, and that the RAM window sees the C64-mode screen at `$0400`
  and the pointers in block 0 after the switch. If the notice "did not enter its second mode" appears, the typed answer did not reach GO64; "did not reach READY" means TXTTAB or the screen test did not match.

### Board 2026-11-16: GO64 Y not typed (D4 fix)

Alex, translated: "Load and run from the C128 on a C64 file needs GO64, (ARE YOU SURE?) Y, RETURN, then load the file, then RUN. Right now it does not even type the Y, and after that it breaks."

- **Cause (measured, not the line cache):** the editor takes no key while the KERNAL's boot attempt (a disk read after the banner) has not ended. TXTTAB `$2D` is `$1C01` from the banner on, so the old phase 0 typed `GO64\r`
  500 ms after TXTTAB, into a machine that was not at READY; the old phase 1 waited 5 s for the count `$D0` to reach 0 and gave up with "did not take the typed mode switch" when the boot attempt lasted longer: `Y` never went in.
  Log lines (`exp.sh`, real ROMs, the 5 s PRG_AT replay, no `+NODEV`, the stub device never answers the boot attempt): after 14 s the screen holds the banner and no READY, `$D0` = 02 (the second piece overwrote the
  first count: nothing was taken), no typed text on the screen. The same run with `+NODEV` (the attempt ends): the screen shows `READY.`, `GO64`, `ARE YOU SURE?`, and the app's own timing (TXTTAB found, +500 ms GO64, `$D0` = 0, +400 ms Y)
  reaches the C64 screen (`**** COMMODORE 64 BASIC V2 ****`, `READY.`) in 40 and in 80 columns (`a40`/`a80`, `ARE YOU SURE?Y` on the VDC). So the timing of the typed steps is right on a machine that is at READY; what failed is typing without
  looking at the machine. The first guess (the RAM window reading `$D0` stale through the 8502's line cache) was not the cause: block 0 is pinned in slots, the window's port uses the same command path as the CPU (`c128_ramd.v`). The board's boot
  attempt time is not measured; the cause on the board is this mechanism by the symptom, not by a board measurement.
- **Bench bug found on the way:** `run_prgrun.sh` ran without `+NODEV`, so none of its cases could ever reach READY (the earlier `basic` run: 0 markers, READY lines 0). Fixed; the `none` control pattern was `ram[0b00]`, the log prints `ram[00b00]`: fixed.
- **Fix** (`coreapp/cores/cbm_run.cc` `service_c64`, `CbmRunMachine::ask_*`, `c128.cc` `machine()`, `c128_prg.h`): phase 0 waits for READY on the 40-column screen (`screen_at` at `$0400`; the 80-column picture is the VDC's and cannot be read: then 6000 ms after TXTTAB, see below) and settle;
  `GO64\r` is typed; phase 1 waits until the count is 0 (up to 20 s: it waits out the boot attempt); phase 4 waits for `ARE YOU SURE?` on the 40-column screen (screen codes in `c128_prg.h`, the bench waits for the same codes) and types `Y\r` only then
  (80 columns or not found: 1500 ms after the first piece was taken); phase 5 waits until that is taken (5 s); then the mode bit, the C64's READY, placement, RUN as before. Log lines: `switch text taken`, `the answer typed (question on the screen | not readable, waited)`, `answer taken`.
- **Bench:** `cores/c128/sim/run_prgrun.sh go64` (new; 14 s of simulated time, not in the default set): `tools/cbm_prg_sim.cc c128 go64` writes the script with the application's own strings and numbers (`C128_SWITCH_TEXT`, `c128_go64_ask`, the C64 parameters); `tb_c128.v` got `5 ms`, `6 n hex`
  (wait for screen codes) and `7 addr value` (wait for a RAM byte), `prgrun_gen.py` the `$0801` program (`POKE 49152,42` / `POKE 49153,PEEK(45)`). The bench's waits are the tb's, not the application's code: the application's polling is checked by the RISC-V build and the board.
  Result (builder, real ROMs): `PASS prgrun go64` - screen wait for the question found after 0 ms of polling, C64 banner after 2260 ms, READY after 30 ms, `ram[0c000] = 2a`, `ram[0c001] = 24`, TXTTAB `01 08`. With the same `+NODEV` fix: `PASS prgrun basic` (`ram[00b00] = 2a`, `ram[00b01] = 22`, TXTTAB `01 1c`),
  `PASS prgrun ml` (`ram[00b02] = 2a`); `none` shows the control's `FAIL: ram[00b00] = 00` line (its check pattern was wrong, fixed, not rerun). S7, S8 and S10 therefore have a bench pass; the board step stays open (closed as done 2026-11-18, §5).
- **Build:** `tools/dev.sh riscv --branch t217-go64` rc=0.
- **Not checked, board step for Alex (closed as done 2026-11-18, §5):** C128 core, F3 file browser, "Load + run" on a `$0801` BASIC .prg, in 40 and in 80 columns, and with a drive/disk image mounted (the boot attempt). The boot attempt on the board: "4-5 s kommt hin" (Alex 2026-11-16: 4-5 s is about right), so the 80-column blind READY wait is 6000 ms (5 s + 1 s); the 20 s phase-1 limit stays.
- D4 board (Board test 2026-11-16, package main-13c2cde9 (run 2344), Alex in the test list): C128 Load + run of a C64 .prg in 40 and 80 columns - PASS (GO64, "ARE YOU SURE?" Y, program runs in C64 mode).

**2026-11-18 - board steps closed as done (shipped and in use):** S7, S8 and S10 on the board (C128 .prg start: 128 mode BASIC and ML, the C64-mode start
with a disk mounted) - Alex 2026-11-15: "Under A there is nothing I would not class as DONE! That has been in the builds for days/weeks, we had bugs and
re-tests in part"; Alex 2026-11-18, shown these steps as open: "that is old stuff that has been running for ages, we had this already 3-4 days ago" and
"tick it off in the TODOs/specs, otherwise it shows up again with some agent" (translated).
