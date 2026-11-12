# 15. No 100T and no public release; T53 dropped

Date: 2026-10-04

Status: Accepted (supersedes [ADR-0004](0004-m1-release-candidate-internal-testers.md))

## Context

T53 built a 100T (UE2) bitstream (CI run 362 green). Till's UE2 turned out to be an XC7A50T (IDCODE 0x0362C093) and runs the C64U bitstream.

## Decision

Alex 2026-10-04 (late): no 100T and no public release; T53 dropped, the release candidate is cancelled ("there will be no 100T"; no 100T package build for the session at Till's on 2026-10-05). Alex 2026-10-06: T53 stays dropped - no 100T in the tester package; the only 100T machine is Commodore's C77 (2027), Till's UE2 is a 50T and takes the C64U bitstream.

## Consequences

T49 packages are 50T only for C64U and UE2 (same board and bitstream); the updater refuses FPGA type 3. T53 keeps its spec and CI stage as history.

## Links

- [T53](../../docs/specs/T53-ue2-build.md#status-and-history)
- [T49](../../docs/specs/T49-update-packages.md#status-and-history)
- [docs/specs/T53-ue2-build.md](../../docs/specs/T53-ue2-build.md)
- [docs/specs/T53-ue2-session-till.md](../../docs/specs/T53-ue2-session-till.md)
