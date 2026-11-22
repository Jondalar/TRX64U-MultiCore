# T248 — c64scpu: the class table in LUTs and the decimal adder with a carry select, so the path into the 65C816 is shorter

**Status:** IN PROGRESS (2026-11-21). Asked by Alex, 2026-11-21.
Open (one line, as in the TODO table): IN PROGRESS 11-21: S1-S3, S5 PASS; S4 FAIL, +0.648 ns of +3 (was +0.015); next step with Alex
**Repo:** TRX64U-MultiCore — the core `cores/c64scpu/`: `scpu_cfgtab.v` (made by `tools/scpu_cfgtab.py`) and
`cpu/s816_addsubbcd.vhd` (made by `tools/scpu_vendor816.py`). No application code.
**Number:** T248.
**Origin:** Alex, 2026-11-21 (translated), on the c64scpu critical path: "Then with sense and reason at the stages, the depth?
A second path maybe?" On the three levers he was shown: "Let's start like that. 1+2."
**Depends on:** T182 (the core, the multicycle recipe of `cores/c64scpu/mono/build_mono.tcl`), T241 S2 (branch `t241-s2-irq`:
the build this starts from).
**Milestone:** M2.

---

## §1 The problem

- **Who:** Alex, and every later change to the c64scpu core.
- **What happens today:** the c64scpu bitstream of run 44854024836 (T241 S2) meets timing with a WNS of +0.015 ns.
  - The five worst setup paths of the chip start at the class table (`cpu_0/smem/tab/q_reg`, a RAMB18) and end in the 65C816
    (`cpu/P_reg[1]`; the CE of `cpu/s816_addrgen/PCr_reg[*]`).
  - Clock `sid2x_mmcm` (the CPU's clk2x), period 15.859 ns. Path 1: 20 LUT levels, 15.028 ns (logic 4.934 ns, route 10.094 ns).
  - The sections of path 1 (`timing.rpt` of the run):

    | Section | Where | LUT levels | Time |
    |---|---|--:|--:|
    | Class table read (RAMB18 without output register) and its route | `scpu_cfgtab.v` | - | 3.472 ns |
    | Class -> `rd_mem` -> data mux | `scpu_mem.v`, `cpu65816_c64scpu.v` | 5 | 3.384 ns |
    | Data byte into the ALU | `s816_mcode` | 2 | 1.282 ns |
    | Decimal adder | `s816_addsubbcd`, `s816_bcdadder` | 10 | 5.685 ns |
    | Result -> Z flag -> `P_reg[1]` | `s816_cpu` | 3 | 1.204 ns |

  - The paths to the CE of `PCr` (places 2 to 5) go through the same table, data mux and adder.
  - The adder is built from single full-adder cells (`s816_bit_adder`, sums of products) in 4-bit ripple groups, two groups per
    nibble (binary sum, decimal correction), the nibbles in series. No `+`, so Vivado does not use the carry chain.
- **Why it matters:** every change to the core can turn this path red. Vivado 2024.1 segfaulted twice in the post-route
  `phys_opt_design -directive AggressiveExplore` of this design (T241 S2 §5).
- **What it is not:** the density. The chip uses 71.66 % of its LUTs; the congestion report says "No congestion windows are found
  above level 5"; the C128 routed at 87.27 % (T183).
- **Why not a register stage:** at 20 MHz the CPU's enable comes 3 clk2x after the previous one (63.056 / 20 = 3.15 clocks per
  cycle). A fourth clock limits the CPU to 15.8 MHz.
- **The parts:**
  - **P1** The class table is a LUT ROM in front of its output register, not a block RAM. Same values, same clock.
  - **P2** The decimal adder computes each nibble for carry in 0 and for carry in 1 in parallel, the binary sums with `+` (carry
    chain); the incoming carry only selects. The same outputs for every input.
  - **P3** The build writes a timing report of the paths from `scpu_mem` into the CPU (appended to `timing.rpt`, which the
    pipeline publishes, and one log line), so S4 can be read.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 (P1) the same classes in the same clock
  Given the class table of cores/c64scpu/scpu_cfgtab.txt
  When the generated scpu_cfgtab is read at all 4096 addresses
  Then q is, one clock edge after the address, the class the .txt gives for that address

Scenario: S2 (P2) the same sums for 8 bits, every input
  Given the decimal adder of the SNES core's 65C816 as the reference (made from cores/snes/rtl/65C816)
  When the new adder and the reference add and subtract every pair of 8-bit operands, with carry in 0 and 1, binary and decimal
  Then S, CO and VO are equal in every case

Scenario: S3 (P2) the same sums for 16 bits
  Given the same reference
  When both compute at least 4,000,000 pseudo-random 16-bit cases, add and subtract, binary and decimal, carry in 0 and 1
  Then S, CO and VO are equal in every case

Scenario: S4 (P1, P2, P3) the path into the CPU is shorter
  Given the next c64scpu build of the pipeline from this branch
  When its report of the paths from scpu_mem (registers, class table, cache) into the CPU is read
  Then the worst of these paths has a slack of at least +3.000 ns, and the design meets timing

Scenario: S5 (P1, P2) the board runs as before
  Given the .cor of S4 on the board
  When the board steps of T241 S2 run (Master's Boom in play, the DMAgic intro, Metal Dust)
  Then they behave as with the .cor of run 44854024836
```

S1 to S3 are benches run by hand (not in the pipeline). S4 is the pipeline's report. S5 is the board test of T241 S2, run with
the .cor of S4.

## §3 Decision

- Alex, 2026-11-21 (translated): "Let's start like that. 1+2." Of the three levers he was shown, lever 1 (the table in LUTs) and
  lever 2 (the adder with a carry select) are built.
- Lever 3 (read the table one clock earlier, a register before the data mux) is not built now. It is built only if S4 fails.
- **D1 (P1):** `scpu_cfgtab` keeps its ports and its registered output: `q <= t[a]` at every clk2x edge, the table in LUTs
  (`rom_style = "distributed"`) in front of `q`, so the class arrives in the same clock as from the block RAM. The ROM sits on
  the address side, which has two periods from the CPU (`cpu_r -> smm_r`) and one from the configuration registers; the path
  into the CPU starts at the flip-flop `q`. The first form (a 12-bit address register `a_q`, then `q = t[a_q]`) kept the whole ROM
  on the one-period path into the CPU and was changed after the review of 2026-11-21 (a guess there: -0.5 to +0.9 ns for that
  form, about 2 ns for this one).
- **D2 (P2):** `s816_addsubbcd` keeps its entity and ports. Per nibble: the binary sum for carry in 0 and for carry in 1 with `+`,
  the decimal correction of `s816_bcdadder` on each, then the incoming carry selects S, CO and VO. `tools/scpu_vendor816.py` makes
  the file and lists the change as a difference, as `cores/c64scpu/PROVENANCE.md` does.
- **D3:** branch `t248-scpu-depth` from `t241-s2-irq`. The vendored CPU there differs from `integration` (the RTI change of T241 S2).

## §4 Out

- Lever 3, unless S4 fails (§3).
- No change to the cycle timing of `scpu_mem.v`, none to the multicycle recipe of `build_mono.tcl`.

## §5 Built

- **2026-11-21, branch `t248-scpu-depth`:** 11 commits on `t241-s2-irq` (fdc9959d), head 1131711e, pushed; run 44875582642.
  - **P1:** `tools/scpu_cfgtab.py` writes `scpu_cfgtab` as a `(* rom_style = "distributed" *)` table with
    `always @(posedge clk) q <= t[a];` (7176ec77). The first form, an address register `a_q` and `q = t[a_q]` (dc31fcd9), was
    changed after the review (D1). `--from-txt` writes the .v from the .txt without the VICE oracle. `--check`: "ok: the table
    equals the oracle", "ok: cores/c64scpu/scpu_cfgtab.v is what scpu_cfgtab.txt makes".
  - **P2:** `tools/scpu_vendor816.py` writes `s816_addsubbcd` as a carry select (f3185d07, bcbeb4fa): per nibble the function
    `nibble` (the binary sum with `+`, then the decimal correction of `s816_bcdadder`) for carry in 0 and for carry in 1; the real
    carry selects S, CO and VO. `s816_bcdadder.vhd` is still made and read, no longer instantiated. `--check`: "ok:
    cores/c64scpu/cpu is what tools/scpu_vendor816.py makes". GHDL `-a --std=93c -fsynopsys -fexplicit -frelaxed-rules` of
    `s816_pkg.vhd` and `s816_addsubbcd.vhd`: no output, exit 0.
  - **P3:** `build_mono.tcl` appends the five worst setup paths from `cpu_0/smem` into `cpu_0/cpu` to `timing.rpt` and writes the
    log line `=== T248 smem->cpu worst setup slack: ...` (b98158e6, e5404222).
  - **S1 PASS:** `cores/c64scpu/sim/run_cfgtab.sh` (iverilog, image `trx64-sim-env:5` on the builder), on the final form of P1:
    "S1 PASS 4096/4096", exit 0.
  - **S2 PASS, S3 PASS:** `cores/c64scpu/sim/run_addsub_equiv.sh` (GHDL 6.0.0, image `trx-ghdl6`; the reference is made from
    upstream `AddSubBCD.vhd` and `BCDAdder.vhd` by the script's `--ref`), on the code of bcbeb4fa (1131711e changed only
    comments in that file): "S2 PASS 524288/524288", "S3 PASS 4194304/4194304".
  - Both benches run by hand; `ci/sims_excluded.txt` lists them (827d91d5).
  - **Review (one agent): FAIL**, fixed before the push: two lint checks (the benches were not listed; a script comment held a
    login on an internal host) and the timing of P1's first form. The commit with the login was rebuilt out of the branch, so no
    pushed commit holds it.
- **S4 FAIL (run 44875582642, green, 2026-11-21):** "=== T248 smem->cpu worst setup slack: 0.648 ns
  (l2/c64/c64/cpu_0/smem/b_a_reg[13]/C -> l2/c64/c64/cpu_0/cpu/X_reg[13]/D)", against the +3.000 ns of S4.
  - The design's WNS is +0.432 ns (it was +0.015). Its worst path is now in the host's HDMI scaler
    (`hout/mtim/yact0_reg[5]` -> `hout/scaler/vacc_reg[*]/R`, 6 levels, clock `pix_mmcm`), not in the core.
  - The class table is off the path: the worst `sid2x_mmcm` path starts at `b_a` (the sampled CPU address). Its sections:
    `b_a` clock-to-out 0.478 ns; the address decode to `rd_mem` 5 LUT levels, 5.162 ns; the data mux 0.713 ns; in the CPU the
    adder for 16 bits and the result mux to `X_reg[13]` 11 LUT levels, 7.896 ns, no CARRY4. 17 levels, 14.249 ns, route 79 %.
  - LUT 23,344 (it was 23,360), slices 96.7 %.
- **S5 (board, Alex, 2026-11-21):** `/SD/cores/c64scpu_t248.cor`, the `.cor` of run 44875582642. The DMAgic intro runs, Metal Dust
  runs, Master's Boom in play crashes as with the earlier cores (T241 §5 on `t241-s2-irq`). The `.cor` of run 44854024836 was not on
  the board; against the earlier board runs nothing changed. S5 PASS.
- **Open:** the next step after S4 (with Alex).
