# C64U flash layout

Source: TODO.md "Flash layout" (read from the C64U by JTAG, 2026-09-30); moved here 2026-10-03.

W25Q128, 16 MB; ranges from `w25q_flash.cc` (U64-II 50T), use from the dump:

| range | holds | used |
|---|---|---|
| 0x000000-0x220000 | **since 2026-10-03: TRX64-Ultimate bitstream** (T15; Gideon's in the backup dump) — was: Gideon's bitstream (u64_mk2_artix, Vivado 2024.1, built 2026-09-10) | up to 0x217300, 2.19 MB uncompressed; slot full |
| 0x220000-0x400000 | **since 2026-10-03: our Ultimate app** (T15) — was: Gideon's Ultimate app | up to 0x362700, 1.29 MB; 0.63 MB left at the end of the slot |
| 0x400000-0xFE8000 | FAT12 flash drive (ROMS, CARTS, HTML, CONFIG, APPS) | 320 of 3041 clusters (1.3 MB), data up to 0x556000 |
| 0xFE8000-0x1000000 | config | 16 of 24 sectors |

- **MultiCore line (state 2026-10-26):** 0x000000 holds the host bitstream with the null core in the partition
  (`null_full.bit`, T123: no machine in the flash), 0x220000 the host application (`hostapp.app`); both come from
  `trx64host_update_*` (`docs/board-tooling.md`, "Installing from Commodore firmware 1.1.0"). Cores are not in the
  flash: a `.cor` is loaded through the ICAP from the stick or SD card; a large `.cor` and a `.cfw` start by IPROG from
  one scratch file `/Flash/legacy/fullcore.bit` on the flash drive (0x400000-0xFE8000), where `/Flash/boot.cfg` lives too.
  The paragraphs "No other cores in flash" and "Gideon's updaters" below describe the TRX64-Ultimate monolith line.
- Boot: the boot ROM in the bitstream loads the app image at 0x220000 (Gideon's format,
  `{dest, length, run}` then the data); ours only when the bitstream at 0x000000 carries
  the running bitstream's build stamp (`docs/specs/T15-flash-boot.md`).
- No other cores in flash (monolith line): guests (Gideon's C64U core, other cores) are soft-booted by
  the host into SRAM and never stored in flash (Alex, 2026-10-03; T26). Gideon's
  updater rewrites 0x0 and 0x220000.
- Full dump kept as recovery backup: tools fork `pinmap/runs/flash-dump/`
  (gitignored, holds the config), SHA-256 `c1b42533…bfcf7`.

## Gideon's updaters

From TODO.md section 7 (2026-10-01):

- Gideon's `.ue2` updaters are standalone programs that write fixed addresses
  (bitstream 0x0, app 0x220000 / 0x3C0000 on 100T, ESP32 firmware, flash-drive
  files; `update_u64ii.cc`). Run one only on purpose, as the way back (T49 section 6.2): it replaces our host. Monolith line: our build of the Ultimate app treats a
  `.ue2` as a package, pulls out the blobs (checked against their `CHK_*` sums) and
  keeps Gideon's bitstream as a guest file (SD/USB) for the host to soft-boot, app and
  ROMs onto SD, ESP32 image separately. An original Commodore/Gideon firmware can still overwrite 0x0: keep a
  way back (MultiBoot fallback, JTAG + dump).
