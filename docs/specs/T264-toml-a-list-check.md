# T264 — Every core lane checks in seconds that its TOML A names the registers its RTL decodes; our own cores take their addresses from the TOML A

**Status:** IN PROGRESS (2026-11-27, D2), ACCEPTED (2026-11-26), asked by Alex.
**Repo:** TRX64U-MultiCore — `tools/` (the list check, the generator), `ci/lint.sh` and `ci/lane.sh` (step 1 of each lane), the RTL of
our own cores (`cores/<core>/rtl`, `cores/common`) for the generated constants.
**Number:** T264.
**Origin:** Alex, 2026-11-26 (translated), on round 4 of `tools/rtl_check.py` (T239.3 P1-3): "The point is that TOML A should match
the bitstream. So why iterate again? Doesn't a simple list compare or a meta-format check make more sense?" On the proposal (the
TOML A as the one source with generated address constants for our own and new cores, a list compare as the net for third-party cores,
the interpreter frozen): "Ja. Bitte." And: "Der rtl Check sollte optional da bleiben." ("The rtl check should stay, optional.")
**Depends on:** T239.3 (TOML A, D30, D55), T239.5 (the matrix lanes, step 1).
**Milestone:** M2.

---

## §1 The problem

- **Who:** each core lane of the matrix (T239.5 S7, step 1 "the TOML A against the RTL") and whoever changes a core's decode or its
  TOML A.
- **What they need:** a red lane when the TOML A names a register the RTL does not decode, or the RTL decodes a register the TOML A
  does not name - before the app reads garbage on the board. The emulator never shows it (its register answers are hand-written in
  the profile); the board shows it only for the function under test.
- **What happens today:** `tools/rtl_check.py` parses VHDL and Verilog itself and runs the IO request symbolically through the decode,
  per byte and per direction. `emit` on apple2 and snes took 14-70 min per core (run8 logs: 834 s snes, apple2 over 45 min), each new
  decoder style is a special case, and three reviews found false PASSes; round 4 took eight runs. Its one real find so far:
  `c64_basic` `cmd_if.command_start` at +0x4 has no read path (FW's `command_intf.cc` uses offset 0).
- **Why it matters:** step 1 must be seconds, not an hour, and must not need a new round for every decoder style.

Parts:
- **P1 The list compare (every core):** the addresses a core's decode compares the request with, read from its RTL text, against the
  addresses of its TOML A.
- **P2 One source for our own cores:** the TOML A generates the address constants (a Verilog include, a VHDL package) and the decode
  uses them, so the map and the decode cannot drift.
- **P3 The interpreter:** `tools/rtl_check.py` stays as an optional tool, frozen at round 4; it is not a pipeline step.
- **P4 The lane:** step 1 runs P1 (P2's check for our own cores) in seconds.

## §2 Scenarios

- **S1 (P1):** `tools/toml_a_list.py check <core>` reads the decode module(s) the core names (TOML A / `core.toml`) and collects the
  constants the address is compared with (`case` choices on the address, `addr == x` / `addr = x"..."` compares, page selects), maps
  them into the core's window and compares the two sets. A TOML A address the RTL does not decode, or a decoded RTL address the TOML A
  does not name, is red with the address and both sides' names. Proof: unit fixtures (a moved offset, a removed register, an extra
  register, a VHDL and a Verilog decoder) and a run over every core of `ci/core_toml.py lanes`, each in seconds (report line with the
  time).
- **S2 (P2):** for each of our own cores the TOML A generates `<core>_regs.vh` / `<core>_regs_pkg.vhd`; its decode uses those
  constants. Lint is red when the generated file is not current (generate and diff) and when a generated constant is not used in the
  decode. Proof: the two red cases as fixtures; the converted cores green.
- **S3 (P3):** `tools/rtl_check.py` and `tests/rtl_check` stay; no lane or lint step runs them. Its tests run only when the tool or its
  tests change (`ci/plan.py`). Proof: the lane and lint scripts no longer call it; a change to `tools/rtl_check.py` starts its tests.
- **S4 (P4):** step 1 of every core lane runs S1 (or S2's check) and its time is in the lane log. Proof: the matrix run's step-1 lines.

## §3 Decisions

- **D1 (Alex, 2026-11-26):** the TOML A as the one source with generated address constants for our own and new cores; a list compare
  as the net for third-party cores; the interpreter frozen - "Ja. Bitte." - and kept as an optional tool: "Der rtl Check sollte optional
  da bleiben."
- Which cores are "our own" (their decode written here, not a port) is read from the code and each core's provenance while building
  and shown to Alex before any decode is converted.
- **D2 (Alex, 2026-11-27): build it now, before the last core moves.** Alex, translated: "Then do T264", after the moves of apple2,
  c64scpu and snes (T239.3 D61) had spent their time in `rtl_check.py emit` and `check` (no TOML A for c64scpu: the scan saw a write
  and a read at every byte). Built first: P1, P3, P4, and the moves' gates (`tools/core_move.py`) on P1 instead of the interpreter.
  P2 starts with D1's step: the list of our own cores, read from the code and each core's provenance, goes to Alex before any decode
  is converted.
## §4 Out

- Further work on the interpreter's semantics (directions, strobes, variables): frozen at round 4 (D1).

## §5 Build notes

Built 2026-11-27 (P1, P3, P4 and the P2 inventory; P2's conversion waits for Alex, D1).

**What was built**
- `tools/toml_a_list.py` (`check CORE [--toml-a F] [--src DIR]`, `check --all`, `selftest`): loads the TOML A with `rtl_check.parse_toml_a` (the maps resolved), takes the
  module of each block of the core window (0x101A0000), finds that module's source by name (own folder, `cores/common`, any core), reads its text and collects the
  offsets the address is compared with: `case (off)` / `case off is` choices (also `off[11:2]` word cases, taken per unit), `off == lit` / `off = x".."`, slice compares
  (`off[11:4] == 8'h04`), `off >= a && off < b [&& !off[n]]`; the page from a `page == N` / `addr[15:12] == N` guard or a wire that holds it. Byte sets of both sides
  are compared (TOML A: every byte of every property incl. `count`/`stride`). Red lines name the address and the register (TOML A) or `file:line` (RTL).
- Not compared, and printed: page 6 (the host's pixel layer, forwarded), the drive window 0x1002xxxx (Gideon's drive decoder behind `drv_io_*`), direction and values.
  A module from `cores/c64u` or `cores/common` (c128's `c64u_io`) is compared one way, TOML A -> RTL (its decode carries parameter-gated features of the C64U). A module
  whose source has no offset constants is printed as UNCHECKED (c128 `layer2_c128`, the ident block).
- Fixtures `tests/toml_a_list/` (a Verilog and a VHDL decoder: green, moved offset, removed register, extra RTL register, extra TOML A register; ranges and word cases);
  run by `ci/lint.sh` group tree.
- P3: `ci/lint.sh` runs `tests/rtl_check` only in the new lint group `rtlchk`, which `ci/dispatch.py` adds when `ci/plan.py` reports `rtlchk` (a change to
  `tools/rtl_check.py` or `tests/rtl_check`; a plan with no base names everything). No lane runs the interpreter. `rtl_check.py lint-src` (the reverse lint for
  I/O literals, `ci/lint.sh` group sdk) stays: it is no decode run, 0.39 s.
- P4: `ci/lane.sh core:<id> compare` (step 4) runs `toml_a_list.py check <id>` and fails the lane on red; the time is in the line.
- `tools/core_move.py gates` runs the list compare (`--toml-a` the app's file); `apply CORE --toml-a FILE` takes a hand-written TOML A (no emit, no layer2 needed);
  emit stays optional. `.claude/skills/core-move/SKILL.md` says so and branches from `origin/integration`.

**Proof lines**
- S1: `python3 tests/toml_a_list/test_toml_a_list.py` -> 11 lines `ok` (verilog and vhdl: green, moved offset, removed, extra RTL, extra TOML A; ranges), under 1 s.
  `python3 tools/toml_a_list.py check --all`, 14 cores of `core_toml.py lanes`, whole run 0.7 s (per core below).

| core | result | seconds |
|---|---|---|
| vc20 | PASS | 0.02 |
| atari800 | PASS | 0.01 |
| ddt_novic | RED 1 (RTL decodes 0x101A0036, `ddt_regs.v:105`, not named) | 0.12 |
| amiga | PASS | 0.02 |
| amiga1200 | PASS | 0.02 |
| c128 | PASS (c64u_io one way; layer2_c128 UNCHECKED) | 0.26 |
| c264 | PASS | 0.01 |
| cpc | RED 2 (0x101A0030, 0x101A0033 read, not named: `cpc_regs.v:205`) | 0.01 |
| diag | RED 2 (0x101A000C-F `diag_regs.v:181`, 0x101A012F) | 0.02 |
| neogeo | RED 1 (0x101A00CC-CF `neo_regs.v:224`) | 0.02 |
| nes | PASS | 0.01 |
| pet | PASS | 0.02 |
| zx | RED 2 (0x101A0030, 0x101A0033-34 `zx_regs.v:184`) | 0.02 |
| zxnext | PASS | 0.05 |

  The six reds are real by D-rule (a decoded byte the TOML A does not name: a byte of a counter or constant the TOML A leaves out of its register's width); the TOML A of
  those cores was not touched. Until Alex decides (name the bytes, or a rule for a register's unnamed bytes), step 4 of those six lanes is red.
- S3: `grep -n rtl_check ci/lane.sh` finds only a comment; `ci/lint.sh` calls `tests/rtl_check` only under `gcheck rtlchk`; `ci/test_orchestrate.py` -> `test_orchestrate: ok`.
  Not proven: that a change to `tools/rtl_check.py` starts group rtlchk in a real run (`plan.py` `changed_code`; no CI run was made).
- S4: `python3 tools/core_move.py gates vc20` -> `toml_a_list: vc20: PASS (41 TOML A bytes, 41 decoded bytes) in 0.02 s`; the lane line is the same call. Not proven in a
  matrix run (no push).
- S2 (P2) not started: it waits for the table below, shown to Alex.

**P2 inventory: whose decode is it** (read from the code: the decode of the address at the core window `cw_*` is the `*_regs` module; that port exists only in this
repository's contract, no upstream file has it, so no ported file holds it. Git history is one snapshot commit (2026-11-12) for all of them, no help. Headers and
PROVENANCE/NOTICE agree, and are only the second witness.)

| core | decode file | decode is | style |
|---|---|---|---|
| c264, pet, vc20, atari800 | `c264_regs.v`, `pet_regs.v`, `vc20_regs.vhd`, `a8_regs.vhd` | ours | case on `off`, page 0 |
| c128 | `c128_regs.v` (page E; `c128/PROVENANCE.md` lists it as ours) + `cores/c64u/c64u_io.v` (page 0, copy of TRX64-Ultimate's `trx_io.v`) | ours + sibling-repo copy | case and `off ==` |
| c64scpu | `c64scpu_regs.v` (cut from c128_regs.v, PROVENANCE) | ours | case on `off`, page E; no TOML A yet |
| amiga, amiga1200 | `amiga_regs.v` (two copies) | ours | word case `off[11:2]` + byte compares |
| zxnext, neogeo, diag, cpc, zx, nes, snes | `zxn_regs.v`, `neo_regs.v`, `diag_regs.v`, `cpc_regs.v`, `zx_regs.v`, `nes_regs.v`, `snes_regs.v` | ours | case on `off`, ranges (diag, cpc, zx) |
| ddt_novic (ddt_mandel) | `ddt_regs.v` (NOTICE: ours) | ours | case on `off` |
| apple2 | no `*_regs`: the decode is in `layer2_apple2.v` / `a2_core.vhd` | not read yet | not read |
| all with a drive | `cores/common/drive_l2.vhd` wraps Gideon's drive (vendor/gideon) | ported (Gideon) | drive window, outside the compare |

  Every register decode of a core window, bar apple2 (not read) and the drive window, is written here. No third-party decode was found among the 14 lane cores.

**Open:** the six red cores (Alex: name the bytes or a rule); P2 conversion of our own decodes (Alex's go on the table); apple2's decode, c64scpu and snes have no TOML A yet
(their moves, T239.3 D61, take `--toml-a`); the matrix proof of S3 and S4 needs a push.
