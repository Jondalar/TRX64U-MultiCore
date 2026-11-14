# 16. Glossary

| Term | Meaning |
|---|---|
| Host | Layer 1. The floorplan, the boot ROM, the boot menu and the loader in the board's flash |
| Core | Layer 2. One machine and its application. A `.cor` file |
| `.cor` | A core bundle: a bitstream (partial or full), the core's application and a manifest |
| `.cfw` | A full firmware file: bitstream and application. Also the format of the installer with a pre-header and of the restore bundle |
| `.ue2` | The same package without the pre-header. The Commodore firmware 1.1.0 takes this form. Also an official firmware file that the host restores (9.4) |
| `.txu` | A MultiCore update file. The host installs it with **U** (9.3) |
| Full firmware | A complete firmware that replaces the host until the next power cycle |
| ICAP | The port inside the FPGA through which the host loads a partial bitstream |
| Contract | The version of the ports between host and core. A `.cor` fits a host with the same contract |
| Partial bitstream | The part of the FPGA design that is the core |
| Partial core | A `.cor` that holds a partial bitstream. The host loads it under the running host. It fits only the host it was routed against |
| Large core | A `.cor` that holds a full bitstream and its application. The host starts it like a full firmware (no question, 13.5.2). It carries its own copy of the host |
| Null core | The empty core in the flash image: host and no machine |
| `.u2a` | An application file that a core or the File Browser starts: `unzip.u2a` and `miniz.u2a`. They lie in `cores/apps` (2.7.1) |
| Config | A named copy of the settings of one core and of its mounted media, a file in `cores/<name>/configs/` (2.7.1) |
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
| JTAG | The programming cable port (USB-Blaster). The FPGA configures from the flash with the cable plugged in. A core that the cable loaded stays until the next power cycle |
| W25Q128 | The flash chip of the board |
| XC7A50T | The FPGA of the board |
| HDMI | The video output of the host and of every core. The C128 core has an analogue output as well, with its board check open. The menu of `trx64u` has the row `Analog video mode` |
| Compact layout | The layout of the host menu and the core menus in the modes narrower than 960 px (4.10) |
| Core folder | `cores/<name>/` beside `<name>.cor`: the settings `<name>.cfg`, `roms/`, `saves/` and `configs/` of one core (12.2) |
| EDID | The list of modes that the screen names. The host offers only these (8.4.2) |
