# 27. Turbo (T29) back into M1

Date: 2026-10-06

Status: Accepted (supersedes [ADR-0018](0018-d02-turbo-out-of-the-budget.md))

Formerly: decision 9. Old references to it resolve here.

## Context

Decision 2 had taken turbo out of the budget; iteration 2 of the architecture reset measured 23,709 LUT.

## Decision

Alex 2026-10-06: **back into M1** - the space is there (iteration 2: 23,709 LUT); set it up again on the new state and measure, clarify the IEC brake under turbo. Replaces "Turbo raus aus dem Budget" of 5.10.

## Consequences

T29 in main 2026-10-06 (branch t29-turbo + app t29-app); turbo 1-64 MHz passes on the board; turbo stays off by default. Open: IEC under turbo still broken.

## Links

- [T29](../../docs/specs/T29-turbo.md#status-and-history)
- [docs/specs/T29-turbo.md](../../docs/specs/T29-turbo.md)
