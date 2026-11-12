# 44. Gideon's unmodified app never runs on our bitstream

Date: 2026-10-07

Status: Accepted

## Context

T60's ROM_SEL reset value 0 (= 0x3C00000) and the app probe kept a compatibility path for Gideon's own layout.

## Decision

Alex 2026-10-07: Gideon's vanilla Ultimate app never runs on our bitstream ("NIEMALS") - no test of it; the compatibility for his layout (ROM_SEL reset value 0 = 0x3C00000, the app probe) is not a requirement and may be simplified later.

## Consequences

No tests, no compatibility work for the vanilla app on our core; the reverse (our app on Gideon's bitstream, T57) stays supported.

## Links

- [T60](../../docs/specs/T60-reu-16mb.md#status-and-history)
- [T57](../../docs/specs/T57-trex-crt-both-cores.md#status-and-history)
