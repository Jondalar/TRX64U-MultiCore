# 12. App DMA into I/O while the C64 runs is built in M1 (G06)

Date: 2026-10-04

Status: Accepted

## Context

Gap audit G06: Gideon's core lets the app DMA into $D000-$DFFF while the C64 runs (boot hotkeys via CIA1, UNLOCK $D038 poke, REST readmem/writemem to I/O).

## Decision

Alex 2026-10-04: build it in M1 and give real LUT/timing numbers.

## Consequences

Built as cycle steal (spec G06-dma-cycle-steal.md); CI run 403: c64_basic 27,636 LUT (-8 vs main-latest), WNS +0.014 ns. In main 2026-10-06. Open: boot hotkeys at power-on on the board, REU interleave.

## Links

- [T63](../../docs/specs/G06-dma-cycle-steal.md#status-and-history)
- [docs/specs/G06-dma-cycle-steal.md](../../docs/specs/G06-dma-cycle-steal.md)
