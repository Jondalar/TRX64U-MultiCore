# 40. Filter On/Off per engine (reSID enable_filter(false))

Date: 2026-10-07

Status: Accepted

## Context

C64RE runs with the filter off ("wir brauchen nur das filter off").

## Decision

Alex 2026-10-07: Filter On/Off per engine - reSID `enable_filter(false)`, bypass in sid_filter, trx_io 0x04C. T69 scope cut (Alex 2026-10-07): no curve tables; a profile = Dag's curve choice/base/offset + Filter On/Off.

## Consequences

Bench run_bypass.sh. The filter parametrisation C64RE/TRX64 uses is expressible as base/offset of the own 6581 curve plus Filter On/Off; a model with more parameters would need a new decision. Profiles are T69 (M2).

## Links

- [T25](../../docs/todo/T25.md)
- [T69](../../docs/todo/T69.md)
- [docs/specs/T25.5-resid8-menus.md](../../docs/specs/T25.5-resid8-menus.md)
