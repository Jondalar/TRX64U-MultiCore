# 43. EF3 scope: no freezer modes, no USB, complete dump included

Date: 2026-10-07

Status: Accepted

## Context

Scope questions of the T51 spec (section 6/7).

## Decision

Alex 2026-10-07: EF3 freezer modes (AR/RR/NP/SS5) are left out of the emulation (not "later"). EF3 USB stays out for good. A complete EF3 dump (8 MB, type 90, slot 0 included) by trex_crt is part of T51.

## Consequences

trex_crt dumps the whole EF3 as one type 90 CRT. Q4 of the T51 spec is answered.

## Links

- [T51](../specs/README.md#belonged-to-trx64-ultimate)
- [T57](../specs/README.md#belonged-to-trx64-ultimate)
- [TRX64-Ultimate/docs/specs/T51-ef3.md](../specs/README.md#belonged-to-trx64-ultimate)
