# 34. EF3 as a dedicated c64_ef3 block with full emulation

Date: 2026-10-06

Status: Superseded by [ADR-0041](0041-ef3-into-the-generic-decoder.md)

## Context

T51 was planned as a night build (2026-10-06/07) on its own branch t51-ef3 from main, spec first, into main only after a board test.

## Decision

Planned (Alex 2026-10-06): EasyFlash 3 emulation, writable 8 MB (CRT type 90, PROP-002 section 8.4; skoe's ef3.vhdl): slots + system area/menu via the generic descriptor (T33), MX29LV640 flash handler + EAPI in the app, KERNAL-replacement mode ($DE0F = 2) in the core, freezer modes later. Built overnight as a dedicated `c64_ef3` block (LAYOUT b5, 8 MB window, 23-bit flash events, KERNAL replacement through the PLA, cart-made C64 reset, buttons as menu actions); spec decisions D1-D13 in T51-ef3.md; CI run 479 green (23,498 LUT, WNS +0.166 ns).

## Consequences

Reversed the next day (ADR-0041); the full emulation is parked at tags park/ef3-full / park/ef3-full-app.

## Links

- [T51](../../docs/specs/T51-ef3.md#status-and-history)
- [docs/specs/T51-ef3.md](../../docs/specs/T51-ef3.md)
