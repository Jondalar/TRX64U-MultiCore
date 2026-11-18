# TRX64U MultiCore

**Exchangeable FPGA cores for the Ultimate 64 Elite II and the C64 Ultimate.**

![The boot menu of the host](docs/manual/img/first-start-boot-menu.png)

> **DISCLAIMER.** This is a hobby project. You install it at your own risk. We give **no support** and accept
> **no liability** for bricked boards or any other damage.

---

## What it does

- A host starts first and shows a boot menu. From there you start a **core**: a whole machine in the FPGA, loaded
  from a `.cor` file on a USB stick or an SD card.
- Cores: C64 (`trx64u`, `trx64g`), VIC-20, Commodore PET, Commodore 16/Plus/4, Commodore 128, Amiga 500, Amiga 1200,
  Atari 800 XL, NES, Super Nintendo, Neo Geo, Apple IIe, ZX Spectrum 48K/128K, ZX Spectrum Next, Amstrad CPC, two demo
  cores and a port diagnostics core.
- **Every core is experimental.** The state of each core is in the
  [core table on GitHub Discussions](https://github.com/Jondalar/TRX64U-MultiCore/discussions/66) and in
  [manual chapter 11](docs/manual/11-cores.md).
- The C64 cores: the code of their bitstream is in the TRX64 Ultimate project. This repository packs it into
  `trx64u.cor` and `trx64g.cor` and builds the core menu of `trx64u`; `trx64g` runs the Ultimate application.
- Video is HDMI. Analogue video (S-video, composite) is in development.
- The original firmware stays usable: start its `.cfw` from the boot menu ([chapter 13](docs/manual/13-full-firmwares-cfw.md)).
- No ROM, Kickstart or disk image is included. You bring your own ([chapter 12](docs/manual/12-roms.md)).

## Is my board supported?

- **Yes:** the Ultimate 64 Elite II and the C64 Ultimate. Both have the same mainboard (XC7A50T FPGA, W25Q128 flash).
- **No:** every other board. The installer checks the FPGA and the flash chip; on another board it writes nothing.

## How do I install it?

1. Get the release from [Releases](https://github.com/Jondalar/TRX64U-MultiCore/releases).
2. Copy the installer (`.ue2` for the Commodore firmware 1.1.0, or `.cfw`) and the `.cor` files to one folder of a USB stick.
3. In the file browser of the firmware on the board, select the installer and pick **Run Update**. Answer the two
   warranty questions with Yes.
4. The installer saves the flash to the stick, then writes the new firmware and switches the board off.
5. Switch on. The old firmware starts as before; hold **C=** at power-on for the host menu.

Every step with its screens: [manual chapter 2](docs/manual/02-installation.md). Later updates come as a `.txu`
([chapter 9](docs/manual/09-update-restore-package.md)).

## What is my risk?

- The installer writes the flash of the board. A power loss while it writes can leave the board unable to start.
- Before it writes, the installer saves the flash to the stick and keeps the old firmware as a `.cor`. **R** in the file
  browser restores it. If the host menu stays black, the way back is the installer or a USB-Blaster cable
  ([chapter 14](docs/manual/14-recovery-and-way-back.md)).
- The original updater replaces this host when `Protect Flash from updates` is off.
- By default FTP and the web page ask for no password. Use a network you trust.
- Read the [warnings in chapter 1](docs/manual/01-safety-and-applicability.md) before you start. The disclaimer above applies.

## Documentation

- [Manual](docs/manual/00-index.md): installation, first start, boot menu, cores, ROMs, recovery, troubleshooting.
- [Bug report](docs/manual/10-bug-report.md): how to send one.
- For developers: [Writing a core application](docs/dev/core-app-guide.md), [Building your own core](docs/dev/core-guide.md),
  also in the [project wiki](https://github.com/Jondalar/TRX64U-MultiCore/wiki).

## Licence

Our own code is GPL-3.0-or-later (`LICENSE`, `LICENSING.md`). Third-party parts keep their licences; the origin and the
licence of every part are in [PROVENANCE.md](PROVENANCE.md). Rights holders who see a problem: contact us, and we remove
the part concerned.
