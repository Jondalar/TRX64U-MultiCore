# 0047. Tape port, user port and real SID sockets move to M2

Date: 2026-10-08

Status: Accepted

## Context

The C64U board design brings no user port to the outside ("Gideon hat im Board Design NIE einen Userport nach aussen geführt"). Alex has Mike's tapecart and a WIC64; the WIC64 needs the user port, so only the tapecart could be measured. The tape port is not built in our core (T58), so T35's session could not run in M1 anyway.

## Decision

Alex (2026-10-08): "Das würde ich gerne nach M2 schieben." T35 (test session: tape port, tapecart, user port, freeze without HDMI) and T58 (tape port in our core) move from M1 to M2. The WIC64 is not testable on the C64U (no user port).

Alex, same day: "Genauso T37. nach M2." — T37 (real SID chips in the C64U's SID sockets; no SID chip available) moves to M2 too.

## Consequences

- M1 no longer waits for tape or user port work.
- When T58 is built (M2), the tapecart is the measurable device; Gideon's measured behaviour wins as before.

## Links

TODO T35, T58, T37; docs/todo/T35.md, docs/todo/T58.md, TRX64-Ultimate/docs/specs/T37-sid-sockets.md
