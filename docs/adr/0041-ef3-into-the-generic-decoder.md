# 41. EF3 goes into the generic decoder: banking and EasyFlash behaviour only

Date: 2026-10-07

Status: Accepted (supersedes [ADR-0034](0034-ef3-as-dedicated-block-full-emulation.md))

## Context

The dedicated EF3 block (ADR-0034) was built and CI-green; Alex's goal is to debug his EAPI bug with the REAL EF3 on the port (T16) and the monitor, not to use EF3 in simulation.

## Decision

Alex 2026-10-07 (reversal, supersedes the dedicated block and the menu scope): EF3 goes INTO the generic decoder (T33/PROP-002 descriptor v2: third register R2, 9-bit bank, chip-order flash address, IO1 read of R2), banking and EasyFlash behaviour ONLY: no menu/system-area boot, no `$DE0F` modes, no KERNAL replacement (kovr reverted), no buttons/menu actions, no reset-surviving state; c64_ef3.v deleted. Start slot (Alex 2026-10-07): no feature - emulation starts at slot 0, a program switches with `$DE01`; accepted difference: a real EF3 keeps its slot until power-off, ours resets to slot 0 on a C64 reset.

## Consequences

Full emulation parked at tags park/ef3-full and park/ef3-full-app ("keep it in git"). Built 2026-10-07 on the reduced scope: descriptor v2 in c64_cartridge (+61 LUT / +10 FF yosys vs 625), `ef3_image.py system/cmp`; tests cart_unit 198/0, carts.py 18/18, geom 25/0, trex_crt host 34,248/0. Not run: Vivado, hardware. Board steps: spec T51 section 7. Not merged to main.

## Links

- [T51](../../docs/specs/T51-ef3.md#status-and-history)
- [T33](../../docs/specs/T33-generic-cart-decoder.md#status-and-history)
- [T57](../../docs/specs/T57-trex-crt-both-cores.md#status-and-history)
- [docs/specs/T51-ef3.md](../../docs/specs/T51-ef3.md)
- [docs/specs/PROP-002-cartridge-mapping-descriptor.md](../../docs/specs/PROP-002-cartridge-mapping-descriptor.md)
