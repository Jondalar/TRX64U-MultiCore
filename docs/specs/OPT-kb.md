# OPT-kb — make c64u_keyboard smaller and shallower

**Status:** spec 2026-10-05, branch `opt-kb`. Rule (Alex): keyboards stay as they are functionally; only the implementation changes.

## 1. What costs

`c64u_keyboard` (`kb` in top.v): 712-721 LUT, 284 FF (main-latest). Path `kb/pc` -> `kb/ins`: 23 levels, `pc` fanout 167.

Cause (read from the source and the timing report): `prog(pc)` is written as arithmetic, not as a table:
`w = (pc - 6) / 5 + 1`, `k = (pc - 6) % 5`, then `nau(w)` selected by `w`. `pc` is a runtime signal, so
synthesis builds a subtract, a divide-by-5 and a modulo-5 out of carry chains (the CARRY4 cascade in the timing
report) and a mux behind them, all to feed a 12-bit register `ins`. That ROM is a pure function of a 7-bit
input: it is at most 12 functions of 7 variables.

Other parts (keys_a/keys_b/keys 192 FF, wait/settle counters, status counter) are small by comparison.

## 2. Plan

1. Replace `prog(pc)` by a constant table (`case (pc)` with literal entries, 128 rows incl. the out-of-range
   rows that the old function maps to OP_END), generated from the old function (`tools/gen_kb_rom.py`
   is not kept: the table is checked in the bench against the old function for all 128 pcs).
2. Same registers, same state machine, same outputs, so nothing else moves.
3. Further micro-savings only where the equivalence bench stays at 0 differences.

## 3. Equivalence

`designs/c64_basic/sim/kb_equiv/`: the old module (`c64u_keyboard_old.v`, copy of main) and the new one run side by
side from the same stimuli; the bench compares `scl_low, sda_low, keys, status, parked` every clock and counts
differences (must be 0). The stimulus is a PCA9555 model (keys = a settable 8x8 matrix, diodes in either
direction, ghosting: several keys in a rectangle, SHIFT LOCK/RESTORE are just matrix positions here; RESTORE and
joysticks do not pass through this module), random NACKs from the slave, random `pause` pulses, resets in
mid-scan, and every single key in turn. Also an exhaustive check `prog_old(pc) == rom_new(pc)` for all 128 pcs.
`sim/tb_keyboard.v` must still pass.

## 4. Real number

CI (`opt-kb-latest`): `kb` row of `c64_basic.utilization_hier.rpt`, total Slice LUTs, WNS, against main-latest
(27,644 LUT, kb 721).

## 5. Result (2026-10-05)

Equivalence: `sim/kb_equiv/tb_kb_equiv.v`, 156,595,122 clocks compared, 0 differences (scl, sda, parked, status,
keys, every clock); table vs old `prog(pc)` 0 of 128 pcs differ; every key seen. `tb_keyboard` and `tb_app_i2c` PASS.
Yosys hint (synth_xilinx, not Vivado): 347 -> 206 LUT.

CI run 415 (a24df28, Vivado 2024.1): `kb` 721 -> 294 LUT, 284 -> 283 FF; total Slice LUTs 27,644 -> 27,251.
Worst path starting in `kb/` is now keys_a -> keys, 1 level (LUT2), slack +0.068 ns; the 23-level `pc` -> `ins` path is gone.
The bitstream job FAILED the timing gate: WNS -0.563 ns in `cpu_mmcm` (ult/i_cpu/i_core/i_gprf BRAM -> exec mul_result
DSP48 PCIN, 2 levels, 9.0 ns), not in kb: the placement moved, as it does on every change to a chip this full. No
package was published (run 413 and 415 give the same WNS: deterministic for these sources).

## 6. Formal equivalence: result (2026-10-05)

`designs/c64_basic/formal/kb_equiv/` (`sh run.sh`; yosys 0.69 + SBY + slang from the OSS CAD Suite; check only, not in
`ci/sims.txt`: every entry there decides the run, there is no informational kind, and the CI image was not checked for yosys/SBY).

**Proven, unbounded: new `c64u_keyboard` == `c64u_keyboard_old` for all inputs (`rst`, `sda_in`, `pause`) and all
times.** k-induction (depth 3, boolector), about 1 s. Miter: both modules on the same inputs, every output
(`scl_low, sda_low, keys, status, parked`) equal every clock. The proof is not output-only induction: it uses the
coupling invariant "every state register of the old module equals its namesake in the new one" (`div, pc, bitn, ph,
rx, settle, wait_cnt, ins, dir, col, keys_a, keys_b, sda_s, pause_s`), which is inductive and implies the outputs.
`ins` being coupled means the ROM table equals the old `prog(pc)` (the divide/modulo hardware of the old module is part
of the model) for every `pc`, 0-127, not only the 128 sampled in the bench.

Assumed, base case only: `rst` high in the first cycle, and the power-up state equal in both modules for the registers
`rst` does not clear (`rx`, `sda_s`, `pc`, `keys_a/b`, `keys`: `keys` copies `keys_a|keys_b` in the same clock that
`rst` clears them, `ins` copies `pc` in that clock). Both designs leave these undefined at power-up (no initialiser),
so equality there is a modelling choice, not a finding; their first-clock values cannot reach an output before the
first `tick` 125 clocks later.

Not proven: Vivado's synthesis of either module (this is RTL against RTL). Sanity: two broken copies of the new module
(PCA9555 read address = write address; inverted key polarity) are rejected (`sh run.sh mutant`).

Tried first, dropped: invariant-free proof with ABC PDR on the plain miter. The old module's `/5` and `%5` make the AIG
about 45,000 cells; no result after 13 minutes, killed.

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

OPT-kb: c64u_keyboard 721 -> 294 LUT, formally equivalent

Milestone M1, status ✅. Open (one line, as in the TODO table): –

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- Task text: c64u_keyboard 721 -> 294 LUT, formally equivalent (docs/specs/OPT-kb.md: SBY k-induction unbounded, plus 156,595,122 clocks compared). In main (merge 8423823); keyboard works on the board (menu, Alex's 2026-10-06 test).
