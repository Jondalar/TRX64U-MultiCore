# TRX64U MultiCore - User Manual

Multi-core FPGA firmware for the C64 Ultimate (C64U). Other boards: see 1.2.

This manual follows `main` as of 2026-11-13 (commit `5a87975ed`).

## Contents

| Chapter | Title | Use it to |
|---|---|---|
| [1](01-safety-and-applicability.md) | Safety and applicability | Know the warnings, the boards and what you need |
| [2](02-installation.md) | Installation | Install the host from the Commodore firmware |
| [3](03-first-start.md) | First start | Open the host menu, add a core and start it |
| [4](04-boot-menu.md) | Boot menu (F1) | Start an entry, read the badges, set direct boot |
| [5](05-file-browser.md) | File Browser (F3) | Add, start, copy, move, delete, format |
| [6](06-edit-menu.md) | Edit menu (F5) | Order, rename and remove entries, set the default |
| [7](07-network-web-colours.md) | Network, web page and colour test (F7) | Find the address, use FTP and the web page |
| [8](08-settings.md) | Settings (F8) | Set direct boot, picture, flash protection, ROM folders |
| [9](09-update-restore-package.md) | Update, restore and package (U, R, P) | Update the host, restore the vendor firmware, package a firmware |
| [10](10-bug-report.md) | Bug report | Write the report of a fault |
| [11](11-cores.md) | Cores | Find ROMs, media, keys and state of each core |
| [12](12-roms.md) | ROMs | Find legal sources and the folders |
| [13](13-full-firmwares-cfw.md) | Full firmwares (.cfw) | Start a complete firmware from the host |
| [14](14-recovery-and-way-back.md) | Recovery and the way back | Restore the previous state |
| [15](15-troubleshooting.md) | Troubleshooting | Find the cause of a symptom |
| [16](16-glossary.md) | Glossary | Look up a term |

## Conventions

0.1 Each paragraph has a number: chapter, section, paragraph (for example 2.3.1).

0.2 A **WARNING** is a risk of data loss or of damage to the board. A **CAUTION** is a risk of a failed step.
A **NOTE** is a fact that helps.

0.3 Key names in **bold** are keys of the C64 keyboard or of a USB keyboard. **RETURN**, **DEL** and **F1** to **F8**
are the same on both.

0.4 Names in `code type` are file names, folder names or text that the screen shows.

0.5 The figures of the host application are screens of the real application that runs in the ue2 emulator (`tools/ui_emu.sh`), cut to the menu: 960 x 540 pixels of the
1920 x 1080 picture, or the size of the mode where a figure shows a compact layout. The figures of a core application (chapter 11) are rendered from the application code by the render bench
(`tests/coreapp/render`) unless a caption says otherwise. The files are in `docs/manual/img/`. The names of files, cores and addresses in the
figures are test data. The emulator has no version stamp and no host key, so the header chips `app` and `host` carry no text in a figure. A figure shows the screen, not the exact content of your card.
