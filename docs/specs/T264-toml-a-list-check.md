# T264 — Every core lane checks in seconds that its TOML A names the registers its RTL decodes; our own cores take their addresses from the TOML A

**Status:** ACCEPTED (2026-11-26), asked by Alex.
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

## §4 Out

- Further work on the interpreter's semantics (directions, strobes, variables): frozen at round 4 (D1).

## §5 Build notes

(not started)
