# 21. No hard 26,000 LUT gate

Date: 2026-10-05

Status: Accepted

Formerly: decision 5. Old references to it resolve here.

## Context

The budget had been a fixed LUT gate.

## Decision

Alex 2026-10-05: **no hard 26,000 gate** ("wenn es baut, ist es ok"). The yardstick is the release build: timing met (WNS >= 0 in every domain). The target stays headroom; the optimisation branches (opt-bling -473, opt-frz -144, opt-kb open) and release without JTAG (-761) go in.

## Consequences

Timing (WNS) decides, not a LUT count; the LUT figures are tracked in docs/lut-budget.md.

## Links

- [docs/architecture-reset-2026-10-05.md](../../docs/architecture-reset-2026-10-05.md)
- [docs/lut-budget.md](../../docs/lut-budget.md)
