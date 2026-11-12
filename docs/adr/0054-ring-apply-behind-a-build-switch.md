# 0054. Ring APPLY behind a build switch, the app's CPU walk as the path without it

Date: 2026-10-12

Status: Accepted (amends SLIM D2 in [0024](0024-slim-state-path.md) for builds without the switch)

## Context

SLIM D2 (Alex, 2026-10-05) kept APPLY, the ring engine's undo/redo of stream 0 into C64 RAM, in hardware (about 265-297 LUT and one block RAM in `trx_engine.v`). The app already has a CPU walk for the same job, chosen when CAPS2 b5 reads 0, board-tested in T20 §17.3: a rewind of 400 frames (1.56 M records) takes 156 ms with APPLY and 1,644 ms with the CPU walk. T80 (lean ring engine) and T79 (the core in its partition, block RAM at 47 of 50 tiles) want the room.

## Decision

Alex (2026-10-12, translated): "Yes, build S1 as a switch, and if need be we then optimise the 1.6 s into something faster."

- `TRX_FEATURES` gets `apply`. Without it the engine has no APPLY: CAPS2 b5 reads 0, the AP_* registers read idle, and the app takes its CPU walk with no code change.
- With it (the default: an unset `TRX_FEATURES` builds everything) nothing changes; SLIM D2 stays the behaviour of such builds.
- The CPU walk's speed is a follow-up (T80 S2: prepare in the background when Timewarp opens, then optimise the walk).

## Consequences

- Builds without `apply` scrub about ten times slower until T80 S2 lands.
- The trial builds of T79 (`TRX_FEATURES: none`) drop APPLY.
