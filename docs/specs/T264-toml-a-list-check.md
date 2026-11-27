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
- **D3 (Alex, 2026-11-27, on the P2 list: every lane core's decode is this repository's `*_regs` module, none third party): P2 for
  every core, in one bundle.** Alex (translated): "Yes, but then best all cores at once ... it can just compute one after another
  until it is through once. Only for T259's analogue out do we compute everything again." So the decodes of all cores (the 14 lane
  cores and apple2, c64scpu, snes once they have their TOML A, T239.3 D61) take the generated constants on one branch; the builder
  builds every key once; T259's analogue output is the next full rebuild.
- **D4 (Alex, 2026-11-27, on D3): P2 is built now, with the last core moves, and everything is built once.** Alex (translated): "No,
  finish the move and T264, and we build everything once; that has to work, otherwise we have too many touched points and it
  grinds." One branch carries T239.3 D61 (apple2, c64scpu, snes) and T264 (P1-P4, P2 for every core); one pipeline run builds every
  key. T259's analogue output (every core, T259 D12) is the next full build, on its own.
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

**Red cores fixed in the TOML A (no RTL change):**
- ddt_novic: `geom.lines` was 8 bit, the RTL answers GEOM_LINES 16 bit at 035-036; widened to 16 bit (`cores/ddt_novic/app/gateware.toml`).
- cpc: `geom` started at 031 with a 16 bit `frame_clks`; the RTL has a 32 bit size (0x99900) at 030-033 and src_w/src_h/bufs at 034-038; block moved to 030, `frame_clks` 32 bit, the others re-offset.
- zx: same size word at 030-033; block moved to 030, `frame_clks` 32 bit, byte 034 named `rsvd34` (the RTL reads constant 0), `lines` at 035, `pix_w` at 037.
- diag: added `ident.ticks_us_x256` (00C-00F, 32 bit; `PD_TICKS` now takes its offset from it); `capcnt.lines` 3 bytes became one 32 bit read (cap_lines, 12C-12F).
- neogeo: added `neo_video.seen_cols` and `seen_lines` (0CC-0CF, 16 bit each; block size 0x10).

**Open:** the red cores are fixed (above); P2 conversion of our own decodes (Alex's go on the table); apple2's decode, c64scpu and snes have no TOML A yet
(their moves, T239.3 D61, take `--toml-a`); the matrix proof of S3 and S4 needs a push.

### P2 built (2026-11-27, branch t264-p2 on core-moves; D3, D4)

**List check, VHDL gap:** `tools/toml_a_list.py` reads `16#010#` literals, `when a to b` range choices, `o := to_integer(unsigned(off))` variable selectors and the `>= and <` fallback of
`a2_regs.vhd`; fixture `vhdl variable selector, 16#..# literals, 'to' range` in `tests/toml_a_list`. apple2 was 0 decoded bytes, now 159/159; one real gap: byte 0x07 (constant 0x0F) was decoded and not named,
`ident.rsvd7` added to `cores/apple2/app/gateware.toml`. It also resolves the generated constants (`R_x`, `R_x + 3`, `R_x >> 2`) to their numbers, so a converted decode is compared like any other.

**Generator (S2):** `tools/toml_a_gen.py gen|check|convert CORE`. Deviation from the spec text (`<core>_regs.vh` / `<core>_regs_pkg.vhd`): the constants are a block of `localparam [11:0] R_<BLOCK>_<PROP> = 12'h...;`
(Verilog) / `constant R_... : integer := 16#...#;` (VHDL) **inside the decode module itself**, between `>>> generated` / `<<< generated` marker lines. Reason: an include file needs `-include_dirs` in every flow that reads the
module (13 mono scripts, core_sources.tcl, rtl_check_sources.tcl, every sim script; Vivado runs in host/ and does not search the including file's directory, the A1200 case of 2026-11-17), and a VHDL package
needs a file-order entry in each; the block in the module is read by every flow already, nothing to find, nothing a same-named file can shadow. Not built as files: Alex to say if he wants the file form instead.
Byte k of a register is `R_X + k` (array element: `R_X + i*stride + k` as a number); the page stays the literal of the guard; a word-granular choice is `R_X >> 2`. VHDL selectors became `to_integer(unsigned(off))`
(vc20, atari800). Constants only for modules whose decode is this repository's (not cores/c64u, cores/common) and that have an offset decode.

**Lint (S2):** `ci/lint.sh` (group tree): `tests/toml_a_gen/test_toml_a_gen.py` (10 lines ok: no block, stale block, unused constant, Verilog and VHDL) and `tools/toml_a_gen.py check --all` (16 PASS); `ci/lane.sh` step 4 runs
`toml_a_gen.py check <id>` after the list check. Red: block missing, block differs from what `gen` writes (generate and compare), constant not used in the decode (a constant inside a word unit or inside an
`off >= R_a && off < R_b` range is covered by that decode).

| core | decode (generated block) | list check | gen check | proof it is the same decode |
|---|---|---|---|---|
| c264 | `cores/c264/rtl/trx/c264_regs.v` (61) | PASS 44/44 | PASS | yosys equiv, `c264_glue` bench PASS (66 s) |
| pet | `cores/pet/rtl/trx/pet_regs.v` (155) | PASS 96/96 | PASS | yosys equiv |
| zx | `cores/zx/trx/zx_regs.v` (113, ranges and `off - base` too) | PASS 116/116 | PASS | yosys equiv |
| nes | `cores/nes/trx/nes_regs.v` (81) | PASS 75/75 | PASS | yosys equiv |
| ddt_novic | `cores/ddt_mandel/trx/ddt_regs.v` (54) | PASS 43/43 | PASS | yosys equiv |
| cpc | `cores/cpc/trx/cpc_regs.v` (137) | PASS 108/108 | PASS | yosys equiv |
| diag | `cores/diag/rtl/diag_regs.v` (111) | PASS 187/187 | PASS | yosys equiv |
| zxnext | `cores/zxnext/trx/zxn_regs.v` (129) | PASS 99/99 | PASS | yosys equiv |
| neogeo | `cores/neogeo/trx/neo_regs.v` (232) | PASS 196/196 | PASS | yosys equiv |
| amiga, amiga1200 | `cores/amiga/rtl/trx/amiga_regs.v`, `cores/amiga1200/rtl/amiga_regs.v` (65 / 73; word units) | PASS 79 / 93 | PASS | yosys equiv |
| c128 | `cores/c128/c128_regs.v` page E (101); `c64u_io.v` page 0 is the sibling repository's copy, not converted | PASS 103/103 | PASS | yosys equiv, `c128_regs` bench PASS |
| c64scpu | `cores/c64scpu/c64scpu_regs.v` page E (76); same `c64u_io.v` | PASS 93/93 | PASS | yosys equiv |
| vc20 | `cores/vc20/rtl/trx/vc20_regs.vhd` (55) | PASS 41/41 | PASS | text equivalence (below) |
| atari800 | `cores/atari800/trx/a8_regs.vhd` (77; `off(11 downto 4) = x"0F"` by hand as a range) | PASS 70/70 | PASS | text equivalence, the one hand edit |
| apple2 | `cores/apple2/rtl/trx/a2_regs.vhd` (139; variable `o` renamed `off_i`) | PASS 159/159 | PASS | text equivalence |

Proof of "same offsets": Verilog files: `yosys` `equiv_make` / `equiv_simple -seq 5` / `equiv_induct` / `equiv_status -assert` between the file before and after (a mutant with one offset moved is DIFF). VHDL: no
GHDL (no container runtime on this Mac, nothing run): the file before and after, comments stripped, constants resolved to numbers, `x"010"` written `16#010#`, compared as token streams: identical. VHDL is therefore
**not elaborated** (GHDL `--std=93c -fsynopsys -fexplicit -frelaxed-rules` in `debian:trixie-slim` is the next step); the new constructs are `case to_integer(unsigned(off)) is` with `when R_X + 1 =>` / `when R_A to R_B + 3 =>`.
yosys `read_verilog -noautowire` + `check -assert` on all converted Verilog files: no implicit net (diag: nine pre-existing "rom_q has no driver", the ROM is another module).
Not run: the long benches (diag_probe 900 s, diag_co 2400 s, diag_avregs/avmap 1500 s), no Vivado.

**snes (coordinator, 2026-11-27, after core-snes merged into core-moves):** `toml_a_gen.py convert snes` replaced 129 literals, 0 left; `toml_a_list: snes: PASS (155 TOML A bytes, 155 decoded bytes)`, `toml_a_gen: snes: PASS`; yosys `equiv_status -assert` against the file before: "Proved 422 previously unproven $equiv cells", none left.
**Left as literal, on purpose:** page 6 (the host's pixel layer): the `off == 12'h088..08B` staging compares in both `amiga_regs.v`; the page guards (`page == 4'hE`). **Open:** the c128/c64scpu page 0 in `c64u_io.v` (a copy of TRX64-Ultimate's file: Alex decides), the matrix proof of the lint lines (no push), GHDL for the three VHDL cores.

