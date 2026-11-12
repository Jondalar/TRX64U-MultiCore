# 8. Timewarp is off while the REU is enabled (A08)

Date: 2026-10-04

Status: Accepted

## Context

Done-audit 2026-10-04, item A08: the ring cannot follow REU traffic.

## Decision

REU enabled = no ring, no Timewarp (T20 spec decision). Sub-decisions: SID after a rewind not restored, accepted (A08.1); drive A restored by the ring later, not M1 (A08.2); cartridge in the ring: M1 restores the bank selection of every cart type, changed flash/EEPROM content staying after a rewind accepted, "ok, auch wenn nicht optimal" (A08.3); VIC internals after a rewind accepted (A08.4).

## Consequences

Built 2026-10-07 as app branch a08-reu-timewarp-app: REU on = ring off and frames dropped, REU off again = ring restarts; menu popup, REST 409, trxmon -32001, one central `trx_ring_reu_sync()`. Board test open (docs/archive/board-tests/board-tests-pending.md T20-REU).

## Links

- [T20](../../docs/specs/T20-delta-ring.md#t20)
- [docs/specs/T20-delta-ring.md](../../docs/specs/T20-delta-ring.md) (A08 as built)
- [docs/done-audit-2026-10-04.md](../../docs/done-audit-2026-10-04.md)
