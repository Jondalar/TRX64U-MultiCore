# 1. Safety and applicability

## 1.1 Purpose of this manual

1.1.1 This manual tells you how to install the TRX64U MultiCore firmware, how to start the cores and how to go back.

1.1.2 Read chapter 2 completely before you start. Read chapter 14 before you flash.

## 1.2 Applicability

1.2.1 The firmware is for the **Ultimate 64 Elite II** and the **C64 Ultimate**. Both have the same mainboard with the
XC7A50T FPGA.

1.2.2 The updater reads the FPGA type. It refuses every type other than XC7A50T. It shows "No bitstream for this
board. Nothing written." and writes nothing.

1.2.3 The updater also checks the flash chip. It refuses a flash chip other than the W25Q128 with "Not the expected
flash (W25Q128). Nothing written."

## 1.3 What you need

1.3.1 A board as in 1.2.

1.3.2 A USB stick or an SD card, formatted FAT32 or exFAT. The installer needs free space on it for the backup (2.3.1).

1.3.3 An HDMI screen. The picture is HDMI. The analogue output (S-video, composite) is built into the C128 core. Its board check is open: the colours and the picture position are not confirmed (3.6.5). The core menu of `trx64u` has the row
`Analog video mode`. The other cores have no analogue picture.

1.3.4 A PC to prepare the stick.

1.3.5 Optional: a network cable, for the web page and FTP.

1.3.6 Optional: a USB-Blaster. You need it only for the way back by cable (14.5). You do not need it to install.

1.3.7 The ROMs of the machines you want to use. The firmware contains no ROM (chapter 12).

## 1.4 Warnings

> **WARNING.** The update writes the flash of the board: the bitstream at address `0x000000` and the host
> application at `0x220000`. A power loss during the write can leave the board unable to start. Do not remove
> power while the updater runs.

> **WARNING.** The raw backup and the restore bundle that the installer writes hold the configuration pages of the flash. Keep the
> medium safe.

> **WARNING.** FTP, the web page and the JSON status of the host ask for no password. The remote-control routes (`/v1/ui/...`) need a token. The first client that asks while no token exists gets it, and the
> board keeps it in a file (`/Flash/trx/ui.token` for the host). Use the board only in a network that you trust.

> **CAUTION.** A `.cor` fits only the host it was built for. Use the `.cor` files of the same release as the
> update package.

> **NOTE.** This firmware contains no ROM, Kickstart or disk image.

## 1.5 Limits

1.5.1 Every core is experimental (`experimental = true` in its `core.toml`) except the Apple IIe. The TRX64 core comes from the TRX64 Ultimate project and has no descriptor here. The table in the README of the project gives the state of each core and
the evidence. This manual follows `main` as of 2026-11-13.

1.5.2 The shipped bitstreams are the debug build. reSID8, UCI, streams, REU, sampler and turbo are off in them.
