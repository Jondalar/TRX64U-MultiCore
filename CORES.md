# Overview on Cores state and general view on cores

State 2026-11-09 (tester pre-release `main-2bd67963` plus what is on `main` since).

**Support:** 

we support **only TRX64** (`trx64u.cor`, `trx64g.cor`). 

Every other core is provided as it is: it is a port of an existing open core, built into the MultiCore host and the common core menu. We fix what we break in the integration (menu, media, picture, sound, input), but we do not maintain or extend the machines themselves, and we do not build new cores on request. 

For now bug reports are welcome; whether and when a core gets fixed is not promised. Before this goes public this rule will change and NO CORES will be supported further. 

**Level:** 
good = used daily, no known big problems 
runs = works on the board, known open items
beta = starts and is usable, clear gaps 
alpha = built, partly or not seen on the board.

**State:** what we are doing on it now: 
n/a = nothing planned 
planned 
in progress 
done.

| Core | Short description | Top features | What does not / never work | Level | State |
|---|---|---|---|---|---|
| TRX64 (`trx64u` / `trx64g`) | C64 with our TRX64 gateware; `trx64u` with our menu, `trx64g` with the Ultimate application | Update/backup/restore, cartridges (EF1, EF3, MegaByter, GMod2), Ultimate menu in `trx64g` | - | good | in progress |
| VIC-20 | VIC-20 (PAL) | Picture, sound, `.d64` on the 1541 with autostart | Start click stays | runs | n/a |
| PET | Commodore PET (2001, 3032, 4032, 8032, SuperPET) | Starts on the board, demos run; IEEE disk drive; needs your own PET ROMs | Provided as is: no further work planned. Models and cassette not checked | beta | n/a |
| C264 | Commodore 16 / Plus/4 | 64K, 1541 drive, PRG and cartridges | Plus/4 model not tested | runs | n/a |
| C128 | Commodore 128, C128 and C64 mode, Z80 | 40 and 80 columns, JiffyDOS, 1541/1581, C128 extra keys on USB and C64 keyboard; #19/#21 fixed on `main` | No cartridge port, no REU; Fill/Fit in 40 columns not yet (#63) | beta | in progress |
| C64 + SuperCPU | C64 with 65816 and SuperRAM | Real SuperCPU ROM boots to READY in simulation | Not in releases; REU with SuperCPU not finished; needs your own SuperCPU ROM | alpha | in progress |
| Amiga 500 | OCS/ECS, 68000 | Kickstart 1.3, `.adf`, USB mouse settings, RTC option | - | runs | n/a |
| Amiga 1200 | AGA, 68020 | `.adf` and `.hdf`, Workbench, scale/smoothing/picture modes, USB mouse, RTC option | Second pad button (#22) open | runs | in progress |
| Atari 800 XL | Atari 800 XL | `.atr` boots, BASIC | - | runs | done |
| NES | Nintendo Entertainment System | Games run, USB pad | Picture about 93 % of the height | runs | done |
| Apple IIe | Apple IIe | `.dsk`, `.nib`, WOZ, saves, ProDOS from a hard disk image | Disk swap while running not tested | runs | done |
| CPC | Amstrad CPC 464 / 664 / 6128 | BASIC 1.1, `CAT`, games with sound | Tape, 464/664 not tried | beta | done |
| ZX Spectrum | 48K / 128K | 48K and 128K run | Tape: pulses shorter than 16 T-states refused | runs | done |
| ZX Spectrum Next | ZX Spectrum Next | Boots from an SD card image | Older `TBBLUE.FW` images stop | runs | done |
| SNES | Super Nintendo | Games run, sound, USB pad, no ROM needed | - | runs | done |
| Neo Geo | SNK Neo Geo MVS / AES | Playable, sound, USB pad, saves | - | runs | n/a |
| DDT demos | mandelbrUlt and Look Ma, No VIC as showcase cores | Both run | No media, demo only | good | n/a |
| Port Diagnostics | No machine: measures joystick/paddle ports from TOML test files | Built and benched | Not on the board yet; packed into the release like every large core that is not build only (experimental) | alpha | in progress |

Corrections welcome in the replies, with the bundle name and a photo.
