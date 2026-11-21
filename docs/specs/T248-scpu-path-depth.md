# T248 — c64scpu: the class table in LUTs and the decimal adder with a carry select, so the path into the 65C816 is shorter

**Status:** IN PROGRESS (2026-11-21). Asked by Alex, 2026-11-21.
Open (one line, as in the TODO table): IN PROGRESS 11-21 (Alex: "1+2"); branch t248-scpu-depth (on t241-s2-irq)
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
  - **P1** The class table is a LUT ROM behind an address register, not a block RAM. Same values, same clock.
  - **P2** The decimal adder computes each nibble for carry in 0 and for carry in 1 in parallel, the binary sums with `+` (carry
    chain); the incoming carry only selects. The same outputs for every input.
  - **P3** The build writes a timing report of the paths from `scpu_mem` into the CPU, so S4 can be read.

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
- **D1 (P1):** `scpu_cfgtab` keeps its ports. `a` goes into a 12-bit register at every clk2x edge; `q = t[a_q]` comes from a LUT
  ROM. The block RAM latched its address at the same edge, so the class arrives in the same clock as before.
- **D2 (P2):** `s816_addsubbcd` keeps its entity and ports. Per nibble: the binary sum for carry in 0 and for carry in 1 with `+`,
  the decimal correction of `s816_bcdadder` on each, then the incoming carry selects S, CO and VO. `tools/scpu_vendor816.py` makes
  the file and lists the change as a difference, as `cores/c64scpu/PROVENANCE.md` does.
- **D3:** branch `t248-scpu-depth` from `t241-s2-irq`. The vendored CPU there differs from `integration` (the RTI change of T241 S2).

## §4 Out

- Lever 3, unless S4 fails (§3).
- No change to the cycle timing of `scpu_mem.v`, none to the multicycle recipe of `build_mono.tcl`.

## §5 Built

Nothing yet.
