# T217 — Start a .prg on the SuperCPU and C128 cores: from the stick and from inside a disk image

**Status:** IN PROGRESS (2026-11-15; PROPOSED 2026-11-12). P1, P2, P3 (128 mode), P4 built; the C64-mode half waits for Alex (open question 1).
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
| S10 | C128 C64-mode program ($0801) | OPEN: question 1; until decided the start is refused with a reason (`refuse_c64_basic`) |

## §3 Decisions

- D1 (Alex 2026-11-12, "own task", translated): the PRG case is not part of T206; it is this task.
- D2 (Alex 2026-11-15, "Ja bitte", translated): the task covers P1-P4 as above.
- D3 (design, follows the code): on the SuperCPU the program, the BASIC pointers and the TXTTAB read go to / from the 65816's SRAM through the DDR2 window
  (`SCPU_SRAM_WIN_OFF` = 0x02000000, the window offset of 0x03000000), and also to the RAM window (the C64 side shows it); the CPU's write-through cache is
  invalidated afterwards (ROM_CTRL.b0 flush, as for the ROM images). The start (RUN / SYS n) is typed through the key matrix as in T206 (READY from the screen), not through
  the keyboard buffer. No gateware change.

## §4 Out

- C64-mode starts on the C128 (open question 1).
- No tape, no cartridge-style start.

## Open questions for Alex

1. C128: a C64 program (BASIC at `$0801`, or a C64 ML file) needs C64 mode, and a reset always starts 128 mode. How should a program start reach C64 mode: (a) type `GO64` and `Y` through the key matrix after READY, then place and run as on the C64, (b) a setting that makes the machine start in C64 mode, (c) refuse (current behaviour, with a message)?

## §5 Build notes
