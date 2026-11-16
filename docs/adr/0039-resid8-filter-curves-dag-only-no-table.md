# 39. reSID8: Filter Curve = Dag's curves only; curve table out; formula stays in hardware

Date: 2026-10-07

Status: Accepted

## Context

T25.5 first kept all seven UltiSID tables and added reDIP's own curves (decision 3 of the T25.5 spec). A loadable curve table in hardware cost a RAMB36 and LUTs, and a table-only hardware (all curves computed by the app) was evaluated in spec section 10.

## Decision

Alex 2026-10-07: "Wir nehmen Dags, und nur diese" - Filter Curve offers only values 0-7 (Chip Model, the five 6581 presets, 6581 Custom, 8580); Gideon's seven tables leave the reSID8 menu (overturns T25.5 decision 3 as first written). "Formel in Hardware lassen" - decision 9: `LEGACY_FC = 2` stays, no table-only hardware. "Tabelle raus, wir brauchen nur das 'filter off'" - the loadable curve table leaves the reSID8 hardware (filter source 3, its RAM and read path, the curve write port into sid_multi). Decisions 1, 2, 4-8 of the spec confirmed. Decision 4: "Wenn ich auf TRX64 bin mit anderer Hardware, dann ist mir Gideons Config erstmal egal."

## Consequences

Built 2026-10-07 (t25-resid8-dag + app t25-resid8-dag-app, merged into integ-m1): sid_multi 1,736 -> 1,704 LUT, 4.5 -> 3.5 BRAM tiles; CI run 502 green, WNS +0.113 ns, 25,976 LUT, 68.5 BRAM tiles. REST legacy table values answer the unknown-value error. The `sidcv_*` window stays for the T25.1 engine and the UltiSID path without RESID8. T69 profiles therefore use Dag's parameters and Filter On/Off only. No board yet.

## Links

- [T25](../todo/T25.md)
- [T69](../todo/T69.md)
- [T25.5-resid8-menus.md](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T25.5-resid8-menus.md)
