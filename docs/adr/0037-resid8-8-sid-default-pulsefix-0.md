# 37. reSID8: 8-SID reDIP is the default build, PULSE_FIX = 0

Date: 2026-10-07

Status: Accepted

## Context

Hearing test of the T25.4 build; the quirk in the 8580 final mix-in (spec 3.5) differs from the original reDIP.

## Decision

Alex 2026-10-07: PULSE_FIX = 0 everywhere (the original reDIP sound for any number of SIDs) and the 8-SID reDIP is the DEFAULT build now (`TRX_SID=redip2` = old two-SID build), so CI builds it for the hearing test.

## Consequences

CI run 481 (8-SID + T25.3 mixer default) green, WNS +0.142 ns; board 2026-10-07 hearing test passes. The app calls the engine reSID8 (T25.5); Gideon's core still shows UltiSID.

## Links

- [T25](../../docs/todo/T25.md)
- [docs/specs/T25.4-redip-ultisid.md](../../docs/specs/T25.4-redip-ultisid.md)
- [docs/specs/T25.5-resid8-menus.md](../../docs/specs/T25.5-resid8-menus.md)
