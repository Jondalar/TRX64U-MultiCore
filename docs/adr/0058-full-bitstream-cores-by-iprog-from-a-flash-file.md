# 0058. Full-bitstream cores start by IPROG from a flash-drive file

Date: 2026-10-21

Status: Accepted (amends [0055](0055-cores-load-through-icap-not-the-esp32.md) and [T84](../specs/done/T84-boot-chain.md) D7/Q3; the work is [T115](../specs/done/T115-full-bitstream-cores.md))

## Context

ADR-0055 left foreign full bitstreams out: "Foreign full bitstreams (Gideon's original) are not offered (T84 Q3: they need their own flash layout)". The T115 PoC (2026-10-21, board, host run 406 in flash) showed they need neither their own layout nor the ESP32: Gideon's 50T bitstream as one contiguous file in our flash drive starts by `WBSTAR` + `IPROG` (BOOTSTS 0x105, no fallback), DDR2 survives the reconfiguration, and his application, staged in DDR2 under the measured address map of his controller (only CPU bits 6:3 differ) with the boot magic at 0xFFF8, boots 3.15 with network and REST.

## Decision

A `.cfw` lies beside the `.cor` bundles and is picked like a core (Alex, Q1: "The .cfw should lie beside the .cor and be selectable, and then go through the loader transiently into flash and then be started", translated). The host writes its 50T bitstream into one scratch file of the flash drive (rewritten only when it differs, checked for one run of clusters on every start), stages the application in DDR2 under the map the bitstream needs (Gideon's controller: `gideon_bl4`; a TRX64 build, recognised by its AXSS write: identity), sets the boot magic and sends IPROG with `WBSTAR` at the file. A power cycle returns to the host. This covers Gideon's original and the large TRX64 release `.cfw` (Q4). Every `.cfw` starts, no version list (Q3); nothing is done against Gideon's updater overwriting the host, no warning (Q2: "whoever flashes over it has bad luck", translated).

## Consequences

- Cores larger than `pb_l2` have a route: a full bitstream with its application, no gateware change in the host.
- The host shares the flash drive with a foreign firmware for one session; T115 S6 measures with a flash dump before and after that nothing of ours is lost (R2).
- A new DDR2 layout in a later Gideon version needs a new measured map (R3).
- No way back from the foreign firmware other than a power cycle.
