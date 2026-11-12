# 20. Release/RC without JTAG debug

Date: 2026-10-05

Status: Accepted

Formerly: decision 4. Old references to it resolve here.

## Context

jtag_dbg, rv_dbg and con_ring cost 761 LUT.

## Decision

Alex 2026-10-05: **RC/release without JTAG debug** (jtag_dbg, rv_dbg, con_ring: -761 LUT); the dev build keeps it. The budget gate applies to the release build.

## Consequences

T46 (release build switch) carries it; debugging a release board = dev bitstream in SRAM.

## Links

- [T46](../../docs/todo/T46.md)
- [docs/lut-budget.md](../../docs/lut-budget.md)
- [docs/architecture-reset-2026-10-05.md](../../docs/architecture-reset-2026-10-05.md)
