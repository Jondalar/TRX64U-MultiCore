# 1. reDIP SID stays the default; no Copper; own SID engine only selectable

Date: 2026-10-02

Status: Superseded by [ADR-0032](0032-sid-dag-lems-redip-instead-of-own-engine.md)

## Context

T25 began as a clean-room multi-SID rebuild (UltiSID-compatible engine, decimator/low-pass, Copper write capture). The engine and decimator were built and bit-exact in simulation but did not place next to T20/T21/T28 (about 4.2k LUT).

## Decision

Alex 2026-10-02: stay with reDIP; the clean-room engine is selectable with `TRX_SID=1` and not the default. The Copper is won't-do (T25.2): no user on the C64U, write capture is covered by T20/T21; `CAPAB_COPPER` stays 0.

## Consequences

reDIP is the default SID engine. T25.1 (engine), T25.3 (low-pass filter) and T25.2 (Copper, kept for reference only) stay as specs. The `TRX_SID=1` engine survives only as a selectable build option (the UltiSID path of bitstreams without RESID8 still uses its curve window).

## Links

- [T25](../../docs/todo/T25.md) (docs/todo/T25.md)
- [docs/specs/T25-cleanroom-multisid.md](../../docs/specs/T25-cleanroom-multisid.md)
- [docs/specs/T25.1-sid-engine.md](../../docs/specs/T25.1-sid-engine.md)
- [docs/specs/T25.2-copper.md](../../docs/specs/T25.2-copper.md)
- [docs/specs/T25.3-lp-filter.md](../../docs/specs/T25.3-lp-filter.md)
