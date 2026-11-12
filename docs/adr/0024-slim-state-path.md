# 24. SLIM: one state path, a lean write log (replaces the freeze session)

Date: 2026-10-05

Status: Accepted

## Context

The TRX64 part of the core (monitor, freeze, Timewarp) took 4,394 LUT on main 448fc36. Alex: "Ich finde das viel zu gross ... Im Grunde müsste jede Operation des C64 einfach nur per shadow in den Ring und dann da elegant weiterverarbeitet werden."

## Decision

Alex 2026-10-05, SLIM decisions D1-D6 (spec SLIM-state-path.md section 2): D1 no session responder, the app's freeze touches no C64 register ("Gideons Menü fliegt doch eh"); D2 APPLY stays in hardware; D3 all statistics counters out; D4 ring areas fixed (stream 0 16 MB at 0x7000000, stream 1 8 MB at 0x6000000, stream 2 4 MB at 0x5C00000); D5 COPY/FILL stays (not a choice: the CPU cannot address DDR2 above 64 MB); D6 LOG_PUT goes, the debug block's records stay (observer action LOG).

## Consequences

A freeze is a plain machine pause with zero machine cycles; app accesses are bus cycles (T12's shadow I/O session, commit list Q1 and the debugger-stop session Q9 are gone). In main 2026-10-06 (CI run 438: 25,974 LUT, WNS +0.132). The D1-D6 table stays in the spec.

## Links

- [SLIM](../../docs/specs/SLIM-state-path.md#status-and-history)
- [T12](../../docs/specs/T12-freeze-overlay.md#status-and-history)
- [T20](../../docs/specs/T20-delta-ring.md#t20)
- [T21](../../docs/specs/T21-monitor.md#status-and-history)
- [docs/specs/SLIM-state-path.md](../../docs/specs/SLIM-state-path.md)
- [docs/specs/ARCH-lean-state-access.md](../../docs/specs/ARCH-lean-state-access.md)
