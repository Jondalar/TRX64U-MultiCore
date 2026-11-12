# OPT-rvmul: rvlite register file -> multiplier path

**Status:** spec 2026-10-05, branch `opt-rvmul`. Patch `tools/patches/gideon/0006-rvlite-mul-pipeline.patch`,
bench `designs/c64_basic/sim/rvmul`.

## 1. The path

`ult/i_cpu/i_core/i_gprf/a/ram_reg` (register file block RAM) -> sign-extension LUT ->
`ult/i_cpu/i_core/i_exec/exec_r_reg[mul_result]/PCIN[*]` (cascaded DSP48, P register = `exec_r.mul_result`),
2 logic levels, ~9.0 ns of data path in a 10 ns period (`cpu_mmcm`, 100 MHz).

Failures that came from it with unrelated changes: run 404 (t44-open, a generic changed elsewhere) -0.317 ns,
runs 413/415 (opt-kb, keyboard) -0.563 ns, rtc-counter -0.24 ns; main passes with +0.010 ns.

## 2. Cause

`execute.vhd` forms the product combinationally from `gprf_i.dat_a_o/dat_b_o`, the block RAM's output
(`gprf.vhd` reads synchronously at decode, the data is valid in the execute cycle), and registers it in `exec_r`.
So one clock period holds: BRAM clock-to-out, a LUT (bit 32 of each operand depends on the operation), the
multiplier cascade (a 33x33 product is several DSP48s chained through PCIN) and the P register. The DSP48 input
registers (AREG/BREG) and the M register are unused, and the BRAM->DSP route length is whatever placement
gives: 99.9 % of the slices are used, so any change elsewhere moves it.

## 3. Options

| | Option | Verdict |
|---|---|---|
| A | Register the operands (AREG/BREG), register the product (MREG), hold the MUL/MULH* instruction in execute for the extra cycles | **chosen**: removes the BRAM->DSP combinational path structurally; costs 2 cycles per multiply only |
| B | Register the register file's read (BRAM output register) | rejected: rvlite has no forwarding, the hazard logic assumes the data of the instruction in `decode_o` arrives with it; every instruction would get a cycle (IPC, hazard logic changes) |
| C | Pblock / floorplan for `ult/i_cpu` | rejected as the fix: placement luck again, in a design that is 99.9 % full |
| (0006 before) | Form the product in writeback from registered operands | reverted: the DSP then drives the register file's write data (DSP -> writeback mux -> BRAM), -2.26 ns |

## 4. Fix (A)

- `multiply.vhd`: `a_r`/`b_r` (33 bit, sign-extended per operation; enable `capture`) and `result <= a_r * b_r`
  are registers; the DSP absorbs them as AREG/BREG and MREG; `exec_r.mul_result` stays the P register.
- `execute.vhd`: `mul_inst` = valid, not flushed, `reg_write_sel` is `WB_MUL_L/H`. A counter `mul_cnt`
  runs 0, 1, 2 while `mul_inst`: cycle 0 captures the operands, cycle 1 forms the product, cycle 2 takes it into
  `exec_r` (together with all other fields, when the writeback stage accepts it, as before).
  In cycles 0 and 1 (`mul_hold`) `exec_r` gets a bubble (`valid`, `reg_write`, `do_jump` = 0; the PC of the held
  instruction stays in it for `dbg_pc`) and `rdy_o` is low, so decode and fetch wait and the register file is not
  re-read. The counter returns to 0 when the instruction leaves, or when `mul_inst` falls (a flush).
- Same ISA: results of MUL/MULH/MULHSU/MULHU identical (DIV/REM are still decoded as MULH, the operation bits are
  used exactly as before). No new hazards: the held instruction's sources were already clear when it became
  valid, the bubble writes nothing, and `reg_rd_d` follows `exec_o` as before.
- Cost: 2 cycles per multiply. `mul_cnt` 2 FF, 66 FF operand registers (absorbed into the DSPs), the stage hold
  adds a term to `rdy_o`.

## 5. Test plan

1. `designs/c64_basic/sim/rvmul` (GHDL, tier 1 in `ci/sims.txt`): the vendored `core` with the patches runs random
   RV32IM programs (`gen.py`): all four multiplies with edge-value operands (0, 1, -1, 0x80000000, 0x7FFFFFFF, ...),
   back-to-back and dependent multiplies, a multiply followed by ALU / branch / store, load-use into a multiply,
   multiplies on the wrong path after a taken branch and `jal`, same operand twice, rd = x0; data bus with 1-4 wait
   cycles (writeback stalls) and without. The ordered store log and the final data memory are compared with a
   reference interpreter. PASS = 0 mismatches over all programs. The bench fails with `c_mul_hold` = 1 (checked).
2. The same bench on the unpatched core (`RV_SRC=...`) passes: the model is right.
3. CI (`opt-rvmul`): WNS of the whole design and of the worst `cpu_mmcm` path, compared with main-latest.
4. Cherry-pick test: `opt-kb`'s `c64u_keyboard.v` change on top (`opt-rvmul-kb`).
5. Board: the app runs with the patched CPU (menu, REST version, D64 directory).

Simulation note: `decode_comb.vhd`'s process lists only some of the fields of `instruction` it reads (not rd, rs1,
rs2, func7), so in simulation an instruction that differs from its predecessor in those fields alone is decoded
stale. Synthesis is unaffected. `run.sh` simulates a copy with `process(all)`.

## 6. Results

- Bench `rvmul`: 12 seeds x {wait states, none}, 1500 random instructions each, 0 mismatches (store log and data
  memory); the unpatched core passes the same bench; `c_mul_hold` = 1 fails it. Cost in the bench: about +45 %
  cycles on a 40 %-multiply mix (2 per multiply); the app multiplies rarely.
- CI run 418 (`opt-rvmul`, 666272b): lint, sims (with rvmul), app, bit-c64u, package, bit-ue2 all green.
  Whole-design WNS (manifest) +0.340 ns (main-latest +0.010). Clock group `cpu_mmcm` WNS +0.389 ns, was +0.019 ns on
  main; its worst path is no longer the register file -> multiplier path (FDRE -> RAMD32). `exec_r.mul_result` does not
  appear among the five worst paths of the report; the path no longer exists in its old form (BRAM -> DSP).
- Area: Slice LUTs 27644 -> 27660 (+16; rvlite_wrapper 2045 -> 2081 LUT-ish rows incl. +62 FF), slice registers
  29658 -> 29744 (+86), DSP48E1 13 -> 13, block RAM 62 -> 62. Slices 99.96 % -> 99.99 %.
- Board (C64U, SRAM only): `tools/load.sh c64_basic opt-rvmul-latest`, `boot staged` of the package's `ultimate.app`
  (header length matches), REST `/v1/version` answers, speaker off, `/v1/drives` lists drive A, `create_d64` makes a
  35-track image and `mount` mounts it.
- Cherry-pick test, CI run 419 (`opt-rvmul-kb` = opt-rvmul + opt-kb's `c64u_keyboard.v` commit d0eafca and ci print
  a24df28): all stages green, whole-design WNS +0.027 ns (opt-kb on main failed runs 413/415 with -0.563 ns on this path).
  Slice LUTs 27275, slice registers 29964.

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

OPT-rvmul: rvlite register file -> multiplier path pipelined

Milestone M1, status ✅. Open (one line, as in the TODO table): –

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- Task text: rvlite register file -> multiplier path pipelined (docs/specs/OPT-rvmul.md, patch tools/patches/gideon/0006-rvlite-mul-pipeline.patch, bench sim/rvmul): run 418 WNS +0.340. In main (merge af00fc6); the app CPU runs on it (Alex's board test 2026-10-06).
