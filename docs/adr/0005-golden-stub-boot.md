# 5. Golden stub: a never-writable minimal bitstream at 0x0

Date: 2026-10-03

Status: Accepted

## Context

In the M3 boot chain a failed update of the main bitstream must not brick the board.

## Decision

Alex 2026-10-03: a minimal never-writable bitstream at 0x0 (RISC-V + SD/USB + SPI) starts first, offers a firmware file from SD/USB1, else MultiBoots into the main bitstream - no brick from a failed update.

## Consequences

Part of the boot-chain design (host firmware in flash, guests loaded into SRAM by the ESP32 over JTAG). Not built yet (M3).

## Links

- [T26](../../docs/todo/T26.md)
- [docs/boot-chain-concept.md](../../docs/boot-chain-concept.md)
