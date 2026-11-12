# 3. Milestones M1, M2, M3

Date: 2026-10-03

Status: Accepted

## Context

The work had no agreed order beyond single rows.

## Decision

Alex 2026-10-03:
- **M1** drop-in replacement for Gideon's core plus the TRX64 functions (ring, monitor, UCI, turbo): everything Gideon's core does, booting from flash without JTAG.
- **M2** decoupling from Gideon: own UI (LVGL) and own API; Gideon's app only the base layer.
- **M3** platform: boot chain with host and guests, other cores, source release.
- extra, no milestone.

## Consequences

Every TODO row carries its milestone (column M). Rows move between milestones only by Alex's call (e.g. T50 and T44 left M1 on 2026-10-04, see ADR-0009).

## Links

- [TODO.md](../../TODO.md) (milestone sections)
