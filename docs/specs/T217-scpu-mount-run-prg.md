# T217 — Mount & Run of a PRG inside a disk image starts the program on the SuperCPU core

**Status:** PROPOSED (2026-11-12).
**Repo:** TRX64U-MultiCore — `coreapp/cores/c64scpu.cc`, the shared `cbm_run` job in the core app.
**Number:** T217 (registry: the table in `TODO.md`).
**Origin:** found while building T206 (SCPU Run Disk, decision A, branch `t206-scpu-run`); Alex, 2026-11-12: "own task" (translated)
  to the question whether the PRG case goes into T206.
**Depends on:** T206 (key-matrix typing for the SCPU, `CoreIo::matrix_keys`).
**Milestone:** M2.

---

## §1 The problem

- On the SuperCPU core, "Mount & Run" of a .prg inside a d64/d81 (`c64scpu.run_prg` -> `cbm_run_queue`) waits for READY by reading
  `$2B/$2C`, places the program with `ram_write` (`cbm_prg_place`, BASIC pointers in page 0) and types RUN into the keyboard buffer
  `$0277/$C6`.
- The 65816 reads bank 0 from the SuperCPU's own SRAM; a C64-RAM write reaches it only in the pages `mir_tab` mirrors (`scpu_mem.v`),
  and page 0 is not among them. So the READY check, the BASIC pointers and the typed RUN do not reach the CPU: the program is not
  started (read from code, not yet seen at the board).
- T206 fixed the same split for Run Disk by typing through the key matrix; the PRG path still uses the old way.

## §2 Scenarios

(to be written with the design)

## §3 Decisions

- D1 (Alex 2026-11-12, "own task", translated): the PRG case is not part of T206; it is this task.

## §4 Out

## §5 Build notes
