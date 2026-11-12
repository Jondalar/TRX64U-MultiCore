# 13. The sampler / Ultimate Audio (G14) is built together with the SID work

Date: 2026-10-04

Status: Accepted

## Context

Gap G14: the sampler / Ultimate Audio ($DF20-$DFFF map, sample runners, MOD player from the REU) is missing on our core.

## Decision

Alex 2026-10-04: the sampler joins the SID work; build engine + sampler COMPLETE on an isolated branch, then check whether it fits. Clean room for the sampler too (register interface from the app only). Alex 2026-10-07: "Bau das mal nebenbei" - build it lean (<= 500 LUT, 1 DSP, 0 BRAM if possible), complete function.

## Consequences

Spec T25.6-sampler.md, branch t25-sampler: built and simulated 2026-10-07, merged into integ-m1 before the board session. Board 2026-10-06: sampler still missing ("Audio [Fail] module not found"); board test open. First board risk: loop points (Xander absolute vs app/Heartbeat relative to START).

## Links

- [T25](../../docs/todo/T25.md)
- [docs/specs/T25.6-sampler.md](../../docs/specs/T25.6-sampler.md)
