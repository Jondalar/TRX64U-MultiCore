# OPT-mig: a smaller MIG (DDR2 controller) by configuration

**Status:** spec 2026-10-05, branch `opt-mig`. Decision (Alex 2026-10-05): option A first (configuration only);
the own lean controller (option B, ~-2,200 LUT) is a later project. Board test: Alex, 2026-10-06 evening.

## 1. What costs (main 448fc36, `utilization_hier.rpt`)

`mig_ddr2` 3,399 LUT: calibration ~1,990 (init 744, read leveling 540, temperature tracking 255 + XADC
wrapper 50, DQS search 135, write calibration 143, rest 125), controller 580 (4 bank machines ~300, arbiter 196,
rank/column 62), PHY wrapper 346, UI layer 293 (write data buffer 238), top 218.

Gideon's own C64U core does not use MIG, very likely: his public constraints (issue 615) put the DDR2 pins on
HSTL_II_18 / DIFF_HSTL_II_18 with his own `SDRAM_*` names; MIG always generates SSTL18_II for DDR2.

## 2. Change (`constraints/mig_ddr2.prj`, both designs)

| setting | was | now | expected |
|---|---|---|---|
| `XADC_En` | Enabled | **Disabled**: MIG gets `device_temp_i[11:0]`; we tie it to a constant (0x9F1 = 40 C) | XADC wrapper -50; the PHY's temperature tracking sees a constant and never moves the delays (measured result in §3) |
| `BankMachineCnt` | 4 | **2** | about -150 (two bank machines plus a narrower arbiter) |

`Ordering` stays Strict, data width 16, 300 MHz, burst 8, address map BANK_ROW_COLUMN.

Risks: (1) temperature drift: no compensation of the DQS capture delays over temperature; DDR2 at 300 MHz on
this board, to be watched in long runs (ring, REU, Lorenz) at operating temperature. (2) bandwidth: two banks
open at a time instead of four; the clients (CPU, engine ring, REU, cartridge fill, drive, mover) are mostly
sequential; the ring's DROPPED counter and the app's DDR2 test show it.

## 3. Result

CI run 432 (opt-mig 941f129, on main 8423823): c64_basic **26,962 LUT** (main 27,275: **-313**), slices 8,146
(99.95 %), WNS **+0.200 ns** (main +0.027); ddr2_test WNS +1.460.

| part | main | opt-mig |
|---|---:|---|
| `mig_ddr2` | 3,401 | 3,076 |
| controller `mc0` (bank machines) | 580 | 296 (bank_mach0 513 -> 232) |
| XADC wrapper `u_tempmon` | 50 | 23 |
| PHY temperature tracking `ddr_phy_tempmon_0` | 255 | 254: stays, a constant input does not remove it |

The temperature tracking would need MIG's TEMP_MON_EN off, which the .prj does not offer.

## 4. Tests

CI build (timing met); `ddr2_test` design memtest on the board; c64_basic: REU test, Timewarp ring with
DROPPED = 0 over a few minutes of a game, Lorenz subset; one long run (an hour) warm.

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

OPT-mig: MIG without XADC, 2 bank machines

Milestone M1, status ✅. Open (one line, as in the TODO table): –

Decisions: [ADR-0022 Own lean DDR2 controller instead of MIG](../adr/0022-d06-own-ddr2-controller.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- Task text: MIG without XADC, 2 bank machines (docs/specs/OPT-mig.md, option A, -313 LUT, WNS +0.200). In main (merge 8c95f66).
- **Superseded by OPT-ddr2: MIG is gone from c64_basic.**
