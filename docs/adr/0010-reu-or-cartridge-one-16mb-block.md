# 10. REU as on Gideon's core: one 16 MB expansion-port block, REU or cartridge, never both

Date: 2026-10-04

Status: Accepted

## Context

Gap audit G01/G02; Xander's programs need a 16 MB REU. Gideon's core serves REU (or GeoRAM) and cartridge ROM from the same expansion-port DDR2 area.

## Decision

Alex 2026-10-04 ("wie bei Gideon"): one 16 MB expansion-port block in DDR2 at Gideon's REU address 0x01000000, shared by the REU (or GeoRAM) and the cartridge ROM, never both - the user picks one; REU_ENABLE (+08, live, off = nothing at $DF00) and REU_SIZE (+09) as on Gideon's core. Restated for T51 (Alex, 2026-10-06): "never REU and a cartridge at the same time" - a real port holds one or the other; who wants both uses Gideon's firmware.

## Consequences

The whole 16 MB belongs to whichever module is plugged. The app refuses `.reu` load, preload, Save REU, runners:modplay, socket DMA and MP3-drive probes while a CRT image is in the block. The 4 MB window (EasyFlashXl, the largest CRT then) needed widening for EF3 (T51). Memory rule: Alex, "REU oder Cart, nie beides" - an exception to "Gideon measured wins".

## Links

- [T60](../../docs/specs/T60-reu-16mb.md#status-and-history)
- [T51](../../docs/specs/T51-ef3.md#status-and-history)
- [docs/specs/T60-reu-16mb.md](../../docs/specs/T60-reu-16mb.md)
- [docs/gap-audit-gideon-app.md](../../docs/gap-audit-gideon-app.md)
