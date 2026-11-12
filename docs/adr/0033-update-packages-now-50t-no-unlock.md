# 33. Update packages built now for the internal testers: 50T only, no unlock

Date: 2026-10-06

Status: Accepted

## Context

T49 (update packages without JTAG) was planned together with the 100T/RC (ADR-0004).

## Decision

Alex 2026-10-06: build it now for the internal testers: .cfw + .ue2 + trxmon.u2a, 50T only, for C64U and UE2 (same board and bitstream; the updater refuses FPGA type 3, writes nothing); no T+R+X unlock in the spec for now (gate open in M1).

## Consequences

Built 2026-10-06 (t49-pkg + app t49-update-app): host checker PASS, ue2emu PASS (type-3 refusal, full update with USB backup byte-exact, corrupt package, read-only stick). Open: board stages B0-B4 with Alex.

## Links

- [T49](../../docs/specs/T49-update-packages.md#status-and-history)
- [T50](../../docs/specs/T50-update-unlock.md#status-and-history)
- [docs/specs/T49-update-packages.md](../../docs/specs/T49-update-packages.md)
