# 0045. OPT-ddr2 is done without the cold-start series

Date: 2026-10-07

Status: Accepted

## Context

The own DDR2 controller (OPT-ddr2, [ADR-0022](0022-d06-own-ddr2-controller.md)) passed a 4-hour run on the ddr2_test design (6,095 rounds, 0 errors, spec §8) and runs in c64_basic since 2026-10-06 (main ba060ae); every JTAG load on 2026-10-07 calibrated "ok, window 29 / 28 taps". Spec §7.10/§9.5 still listed a cold-start series (10 power-ons) and a really warm case in c64_basic.

## Decision

Alex, 2026-10-07, asked whether the 4-hour run plus the day's loads are enough: "reicht". The cold-start series and the warm case are not run; OPT-ddr2 is done.

## Consequences

- TODO OPT-ddr2 ✅; board tests T5-COLD and T5-WARM dropped from docs/archive/board-tests/board-tests-pending.md.
- A calibration problem on a cold board would show up as a failed `trxdbg ddr2` / DDR_CAL0 after a load; then it is a bug, not an open test.

## Links

TODO OPT-ddr2, TRX64-Ultimate/docs/specs/OPT-ddr2.md
