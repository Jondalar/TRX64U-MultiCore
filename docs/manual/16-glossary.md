# 16. Glossary

| Term | Meaning |
|---|---|
| Host | Layer 1. The floorplan, the boot ROM, the boot menu and the loader in the board's flash |
| Core | Layer 2. One machine and its application. A `.cor` file |
| `.cor` | A core bundle: a partial bitstream and the core's application, with a manifest |
| `.cfw` | A full firmware file: bitstream and application. Also the format of the installer with a pre-header and of the restore bundle |
| `.ue2` | The same package without the pre-header. The Commodore firmware 1.1.0 takes this form. Also an official firmware file that the host restores (9.4) |
| `.txu` | A MultiCore update file. The host installs it with **U** (9.3) |
| Full firmware | A complete firmware that replaces the host until the next power cycle |
| ICAP | The port inside the FPGA through which the host loads a partial bitstream |
| Contract | The version of the ports between host and core. A `.cor` fits a host with the same contract |
| Partial bitstream | The part of the FPGA design that is the core |
| Common application | The one application that runs most cores: file browser, settings, menu |
| `boot.cfg` | The boot menu, stored in `/Flash` |
| Backup `.cor` | The old firmware as a large `.cor`, written by the installer. The boot menu shows the badge `backup`. **R** restores it (9.4) |
| Backup (`TRXBK_<date>_nnn.BIN`) | The raw copy of the flash that the installer writes before it writes the flash (2.4.4) |
| Restore bundle (`TRXBK_<date>_nnn.CFW`) | The installable package of the state before the install (14.3) |
| Direct boot | The host starts the default entry at power-on without the menu (4.7, 8.2) |
| Host key | The identity of the host bitstream. A partial `.cor` is routed against one host key (9.3.5) |
| Kickstart | The Amiga's system ROM |
| AGA | The Amiga's advanced graphics chipset (Amiga 1200) |
| OCS/ECS | The Amiga's original and enhanced chipsets (Amiga 500) |
| ROM | A file with the firmware of a machine. Not part of this release |
| JTAG | The programming cable port. Active at power-on it stops the FPGA from loading |
| W25Q128 | The flash chip of the board |
| XC7A50T | The FPGA of the board |
| HDMI | The video output of the host and of every core. Only the C128 core has an analogue output as well, in development |
| Compact layout | The layout of the host menu and the core menus in the modes narrower than 960 px (4.10) |
| Core folder | `cores/<name>/` beside `<name>.cor`: the settings `<name>.cfg`, `roms/` and `saves/` of one core (12.2) |
| EDID | The list of modes that the screen names. The host offers only these (8.4.2) |
