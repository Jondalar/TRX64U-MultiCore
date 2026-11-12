# 22. Own lean DDR2 controller instead of MIG

Date: 2026-10-05

Status: Accepted

Formerly: decision 6. Old references to it resolve here.

## Context

MIG is large; opt-mig (option A, -313 LUT) was merged as the cheaper step.

## Decision

Alex 2026-10-05: **Option B: own lean controller instead of MIG** ("das lohnt sich richtig"), modelled on Gideon's Altera controller; first in the ddr2_test design with a memory test on the board, only then the swap into c64_basic.

## Consequences

OPT-ddr2: board 4-hour run 6,095 rounds 0 errors; swapped into c64_basic and in main 2026-10-06; MIG is gone (`mig_ddr2.prj` stays for pincheck). T11 and OPT-mig are superseded by it; T41 (app memory path) must be re-measured.

## Links

- [OPT-ddr2](../../docs/specs/OPT-ddr2.md#status-and-history)
- [OPT-mig](../../docs/specs/OPT-mig.md#status-and-history)
- [T11](../../docs/todo/T11.md)
- [T41](../../docs/todo/T41.md)
- [docs/specs/OPT-ddr2.md](../../docs/specs/OPT-ddr2.md)
