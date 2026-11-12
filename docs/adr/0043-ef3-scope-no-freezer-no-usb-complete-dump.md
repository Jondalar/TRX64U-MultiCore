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

- [T51](../../docs/specs/T51-ef3.md#status-and-history)
- [T57](../../docs/specs/T57-trex-crt-both-cores.md#status-and-history)
- [docs/specs/T51-ef3.md](../../docs/specs/T51-ef3.md)
