# SuperCPU board suite (T241.1)

Eight measuring programs for the c64scpu core, and the SuperCPU programs of VICE-testprogs. They find by elimination
why Master's Boom freezes in play and why a reset leaves `$02a6` = 0 (NTSC) on a PAL machine.
Spec: [docs/specs/T241.1-scpu-board-suite.md](../../docs/specs/T241.1-scpu-board-suite.md).

## Files

- `scpu1_env.asm` ... `scpu8_shadow.asm`: the programs (64tass). The `.prg` files are committed.
- `lib.inc`: the shared code: screen output, the 32-bit CIA2 cycle count, the checks, the speed and mirror-mode switches.
- `gen_ops.py` writes `ops_tab.inc`, the 201 opcode cases of scpu2.
- `ref/stock_pal.txt`: every program's screen on a stock PAL C64 (c64re runtime).
- `build.sh` assembles everything. `vice.sh` fetches the VICE-testprogs SCPU programs at a pinned commit into a cache
  outside the repository. `put.sh` copies the programs and the fetched VICE programs to `/SD/tests/scpu/`
  (`BOARD=<address> tests/c64scpu/put.sh`).

Each program prints a table and a last line `n checks, m failed`. `*` marks a value that differs from the expected one.
All numbers are PHI2 cycles, counted by CIA2 timers A and B chained.

## Order of the runs

**Step 0, the reference.** Run all eight programs on a 6510 C64 core of the same board. Each must show `0 failed`;
scpu6 shows `no supercpu`. A failure here is a fault of the suite or of that core, not of the SuperCPU.

Then on the c64scpu core, in this order. Each step rules out one cause when it passes.

| Step | Program | Passes when | Then this is not the cause |
|---|---|---|---|
| 1 | `scpu1_env` | the VIC is PAL; `$02a6` = 1; the `20` row agrees with the `md` row; `$e544` takes 2000 or more | the PAL test of the KERNAL (see below) |
| 2 | `scpu2_ops` | no case differs | the cycle count of an opcode at 1 MHz |
| 3 | `scpu3_1mhz` | `off` columns exact, `spr` columns within 64 | cache misses or DDR2 at 1 MHz, sprite DMA stalls |
| 4 | `scpu4_irq` | low and high as on the C64 for all 10 sleds | the point where the CPU takes an interrupt |
| 5 | `scpu5_chain` | `all` as on the C64 in a-d; soak `early 0`, `order 0` | Boom's dispatcher edge (line 145/146) |
| 6 | `scpu6_20mhz` | all rows within their tolerance | 20 MHz speed, I/O and bus-write timing |
| 7 | `scpu7_mirror` | the table and the four rows as expected | the write-through to the C64 RAM |
| 8 | `scpu8_shadow` | all eight `$01` rows as on the C64, at both speeds | the ROM images and the RAM under ROM and I/O |

When steps 2-5 pass and Boom still freezes, CPU and interrupt timing at 1 MHz are ruled out; steps 7 and 8 come next.

**`$02a6` after a reset.** Reset the c64scpu core five times from the menu. After each reset, run `scpu1_env` first and
note `$02a6`. The CMD ROM's reset sets 20 MHz before CINT, and CINT decides PAL only when its screen clear ends at
line 256 or later. `scpu1_env` replays this from 9 start lines: the `20` row shows the outcome per start line. The
same value after every reset means the reset always reaches CINT at the same line.

## The VICE-testprogs programs (real-hardware values)

| Program | What it shows | Real SuperCPU |
|---|---|---|
| `scputest-0.prg` | rasterline test, min and max per delay 0-24 | 0-2: `2 2`; 3-22: `3 3`; 23: `3 4`; 24: `4 4` (photo in VICE-testprogs, C64C with SuperCPU v2) |
| `scputest-1.prg` | badline test, min and max per delay 0-24 | 0-2: `12 12`; 3-22: `13 13`; 23: `13 14`; 24: `14 14` (same machine) |
| `test_badline.prg` | cycles of 65536 writes to `$07e7` at 20 MHz, screen on | NTSC `$00010FA2`. PAL: `$00010CAA` (68778) expected, see scpu6 |
| `reu-check.prg` | REU stash and fetch with the SuperCPU (the REU must be on) | green border |
| `dmatest.prg` | REU stash and fetch of 16 bytes, compared by the 65816 | green border (red: fail) |
| `dmastart.prg` | REU DMA stops the 65816 at once | `PASS`, green border (`FAIL`, light red) |

`scputest.prg` is the same as `-0` and `-1` with a menu (F1, F3). The PAL value of `test_badline` comes from a model:
one write per PHI2 cycle, none in a badline's 40 DMA cycles. The model gives 69536 for NTSC (real: 69538) and 68776
for PAL.
