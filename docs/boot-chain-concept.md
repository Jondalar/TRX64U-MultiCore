# Boot chain: host firmware and guest cores (concept)

Status: concept, 2026-10-01. **Superseded 2026-10-12 by [T84](specs/done/T84-boot-chain.md)** (Alex: no ESP32 loader; cores through ICAP and DFX). The flash-protection section stays valid and is taken over by T84 D8.

## Idea

The C64U's configuration flash holds **our host**: a small bitstream plus a
firmware with an LVGL user interface. Everything else is a **guest** that the
host loads into the FPGA's configuration SRAM on demand:

- Commodore's original C64U (Gideon's bitstream and Ultimate app, unchanged),
- TRX64 Ultimate (our C64 core with Gideon's app),
- other cores (VIC-20, C16/Plus4, Apple II, ... see T23).

Guests come as packages from SD card or USB, so their number is limited by
the medium, not by the 16 MB flash. Because the host sits at the start of the
boot chain and stays there, this works like a permanent jailbreak: the
machine always starts in our code, and the original firmware becomes one
choice among others.

## Why through the ESP32

The ESP32 on the C64U is wired to the FPGA's JTAG port
(`software/u64ctrl/main/jtag.c` in 1541ultimate: TCK/TMS/TDI/TDO on GPIOs,
Xilinx `CFG_IN` 0x05, `JPROGRAM` 0x0B, `JSTART` 0x0C). It can therefore load a
bitstream into the FPGA's SRAM, as our USB-Blaster does. The 7-series FPGA
itself can only configure from its SPI flash or over JTAG, not from SD, and
an FPGA that loads its own successor over ICAP kills the logic doing the
loading. The ESP32 survives the switch, so it can finish the load.

## Flow

1. Power on: the FPGA configures from flash with the **host** bitstream; the
   host firmware (RISC-V, FreeRTOS, LVGL) shows the core selection.
2. The user picks a guest. The host reads the package from SD/USB, checks and
   unpacks it (bitstream, optional app image, ROMs, settings).
3. The host sends the bitstream to the ESP32 over the WiFi UART (5 Mbit/s,
   SLIP RPC as today), with flow control; the ESP32 buffers it in RAM/PSRAM
   or its own flash as needed.
4. The ESP32 runs `JPROGRAM`, shifts the bitstream in with `CFG_IN` and
   `JSTART`: the guest starts. A guest that needs software (TRX64 Ultimate:
   Gideon's app) gets it from the package; the host leaves it in DDR2 for the
   guest's boot ROM, or the guest's boot ROM fetches it from SD itself (DDR2
   contents are not guaranteed across reconfiguration).
5. Back to the host: a defined key combination or the rocker in the guest
   (our cores: in hardware; Gideon's: through the ESP32's button events)
   makes the ESP32 pulse `JPROGRAM` without data, so the FPGA reloads the
   host from flash. Power off/on gives the host as well.

Sizes and times: a 50T bitstream is about 2.2 MB, less with
`BITSTREAM.GENERAL.COMPRESS`. UART at 5 Mbit/s: about 4 s uncompressed. JTAG
bit-banged by the ESP32 at 2-10 MHz TCK: a few seconds.

## Package format (draft)

One file per guest, e.g. `.trxcore` (a zip or a simple TLV container):

- `manifest`: name, version, author, licence, target board/FPGA, sizes,
  SHA-256 of every part,
- `bitstream` (compressed `.bit` or `.bin`),
- optional `app` (software image and its load address),
- optional ROMs, default settings, icon/screenshot for the LVGL menu.

The host checks the hashes before anything goes to the ESP32.

## ESP32 firmware

A new ESP32 firmware is needed: Gideon's `u64ctrl` plus one RPC command
family ("receive bitstream", "program FPGA over JTAG", "reload from flash").
Power and rocker handling stay as they are.

Risk and safeguards:

- The ESP32 cannot be bricked completely: its first-stage bootloader is in
  ROM and always accepts new firmware over UART when GPIO0 (BOOT) is low at
  reset. On the C64U these lines go to the FPGA (`WIFI_BOOT` L13, `WIFI_DIS`
  M13, the UART), which is how Gideon's updater updates the ESP32.
- The real risk: the ESP32 switches the main board's supplies
  (`IO_ENABLE_V50`, `IO_ENABLE_MOD` in `button_handler.c`). A firmware that
  never turns them on leaves no FPGA to re-flash it from; then only the
  ESP32's UART pads would help (loose wires, which we avoid).
- Therefore: ESP-IDF OTA with two app partitions and rollback. Gideon's
  original stays as the factory image; ours is the OTA image and must confirm
  itself within a short time, otherwise the bootloader returns to the
  original. Keep the power/rocker code untouched.
- Before the first ESP32 flash: prove the recovery path (FPGA puts the ESP32
  into its ROM bootloader and re-flashes it with the original firmware).

## Flash

The host replaces what boots from flash, and flash writes are released (2026-10-13; `docs/board-tooling.md`): the host
bitstream (with the null core) sits at 0x000000 and the host application at 0x220000, written from a `trx64host_update_*`
update file or with openFPGALoader (`docs/c64u-flash-layout.md`). Guests are not in the flash. The original image
stays available as a guest (`.cfw`) or as the way back through its own `.ue2`.

## Protecting the host in flash

The host must survive a guest that writes the flash, above all Gideon's
firmware and its updater. Software filters are not enough for Gideon's own
bitstream (its SPI flash controller is not ours), so the protection has to be
in the flash chip:

- Gideon's firmware already uses the W25Q block protection
  (`software/io/flash/w25q_flash.cc` `protect_configure`: BP bits over the
  lower part of the device), and his updater lifts it before writing
  (`protect_disable`, status register 0). Block protect bits alone therefore
  do not help; the status register itself has to be locked.
- W25Q128 options:

  | Mechanism | Effect | For us |
  |---|---|---|
  | BP2:0 with TB (block protect) | lower range read-only, e.g. 0-4 MB | only together with a lock |
  | SRP0 + WP# low (hardware protect) | status register fixed while WP# is low | only if WP# reaches the FPGA (unknown, likely tied high) |
  | **Power-supply lock-down** (SRL = 1, SRP0 = 0) | status register fixed **until the next power cycle** | **yes** |
  | One-time program lock (SRL = 1, SRP0 = 1) | fixed for ever | no, irreversible |

- The plan: at every power-on the host boots first, sets BP/TB over its own
  region and then the power-supply lock-down, and only then starts a guest.
  Until power is removed nobody can lift the protection; erase and program
  commands to the protected range are ignored by the chip, so Gideon's
  updater fails with an error instead of overwriting the host.
- Our own guests (TRX64 etc.) already turn WREN into WRDI in hardware.
- Updating Gideon's firmware then means using it as a guest (`.cfw`) and not running his updater against the
  flash; his or Commodore's `.ue2` is run only as the deliberate way back, and it replaces the host (T49 section 6.2).
  Whether the lock-down is built is not recorded here; T84 D8 owns it.
- To check first: the exact chip variant from its JEDEC ID and status
  registers (W25Q128JV has SRL in status register 2), and where WP# goes.
- A key-based lock/unlock (as the U64's "unlock" might suggest) does not
  exist for the flash in Gideon's code: the U64 "unlock" interrupt is the
  C64-side knock sequence that enables the UCI at $DF1C. A hardware lock-down
  is stronger anyway, since no software can undo it before power-off.

## Open questions

- JTAG sharing: the ESP32 and the USB-Blaster on P5 are on the same chain;
  the ESP32 must release its pins (`jtag_disable_io()` exists) while a cable
  is used. Our note says JTAG activity at power-on stops the FPGA from
  configuring.
- Buffering: does the ESP32 module have PSRAM, or must the bitstream be
  streamed with tight flow control?
- How Gideon's firmware behaves as a guest: it expects its own boot from
  flash (bootloader, app at 0x220000); the guest package must provide the
  same state (app image in DDR2 or a boot path that reads from flash).
- Return path from Gideon's firmware: it does not know our key combination;
  the ESP32 can watch for a rocker gesture (e.g. a long press while pressing
  down) before forwarding events.

## Reference: ESP32 work upstream (2026-10-04)

GideonZ/1541ultimate issue #952 (TLS / hardware RNG offload to the ESP32): the only public
discussion so far of new ESP32-side work besides Gideon's updater code. Useful for T26 as
reference: the issue asks for plans agreed before code, features that work on every Ultimate (not only
ESP32 boards), and no architecture growth around the ESP32 ("benchmark, profile, accelerate");
a comment there lists every board's CPU, free app flash and network (C64U: rvlite
100 MHz with multiply, 728 KiB free app flash, WiFi via ESP32-S3) and notes the main CPU has no
random source (`LWIP_RAND` = unseeded `rand()`). The WiFi UART link and the ESP32's own firmware
are what T26's guest loading builds on.

**Scope (Alex, 2026-10-04):** TRX64 Ultimate targets only the U64/U64E, the U64 Elite II and the C64 Ultimate. The U2, U2+ and U2+L are out of scope; the "every Ultimate" rule stated in #952 applies to upstream contributions, not to our boot chain.
