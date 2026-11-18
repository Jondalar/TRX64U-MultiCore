# TI-99/4A core (experimental)

A community-built core, ported from [zeldin/Mega99](https://github.com/zeldin/Mega99) (LGPL-3.0) by rix with Claude's help,
as an early effort and a proof that a core can be built on this host without the maintainers' involvement.
It is **experimental**: it boots to the TI title screen and TI BASIC and takes keyboard input on a C64 Ultimate. Everything
else below is untested or not done. No support is promised (see CONTRIBUTING.md).

## ROMs (not in the repository)
Copy the console ROM and the three GROMs to the stick, with the names of MAME's `ti99_4a` set:
`994a_rom_hb.u610`, `994a_rom_lb.u611` (console ROM, high and low bytes, 4096 bytes each),
`994a_grom0.u500`, `994a_grom1.u501`, `994a_grom2.u502` (6144 bytes each). A wrong size or a read-back mismatch is
reported in the core menu.

## What works (C64 Ultimate on release 0.2.0, 1920x1080 HDMI)
- Boot to the title screen, TI BASIC, typing and running programs; TMS9918 picture (280x240, 3x by default; the Scale setting fits or fills the screen).
- Keyboard, mapped by the symbols printed on the C64 keys: `Shift+2` is `"`, `Shift+7` is `'`, `Shift+8` / `Shift+9` are `(` `)`,
  `Shift+/` is `?`, `Shift+:` / `Shift+;` are `[` `]`, `:` and `+ - * @ ^` as printed. C= is FCTN (C= + P is also `"`),
  CTRL is CTRL, left-arrow is QUIT (FCTN+=), DEL / Shift+DEL are DEL / INS, HOME is BEGIN, RUN/STOP is CLEAR, cursor keys as FCTN+S/D/E/X.
- Host features of the generic application: reset, reload ROMs, settings menu, hotkey to the host menu.

## Not tested yet / not done
- Sound (TMS9919 and speech), joysticks, the turbo and 32K RAM card settings.
- Cartridges (`.rpk`), disks (the disk controller DSR), tape, the p-code card; the TIPI and speech parts are left out of the build.
- PAL / NTSC timing in all four System Modes (the clock follows the System Mode, but only one mode was run on a screen).
- A logo, and the TI-specific host hotkey map (the C64 one is used).

## How it is built
`cores/ti99/rtl/src` is Mega99 trimmed to fit the 13,600 LUT / 40 BRAM partition (diff in `rtl/mega99-trims.patch`, files and sha256 in `rtl/NOTICE`).
`rtl/trx` is ours: `ti99_win.v` (the app's ROM aperture to Mega99's wishbone map), `ti99_regs.v`, `ti99_keys.v`,
`ti99_pixw.v` (picture into the DDR2 frame buffer), `clkgen_nco.v` (clock enables that follow the System Mode), `ti99_core.v`.
The benches are in `sim/` (`iverilog -g2012`). The application side is `coreapp/cores/ti99.cc` and `ti99_regs.h`.

The host's ROM aperture hands the core block-relative addresses (CHAR 0x0000, KERNAL 0x2000, BASIC 0x4000); `ti99_win.v` undoes that and
the register `WBADD` (0x0A2) selects which GROM slot an image goes to. Anyone porting another core will meet this.

## Licence
Mega99 files: LGPL-3.0 (`rtl/src/LICENSE-Mega99.txt`). Our files: as in LICENSING.md. See `rtl/NOTICE` and PROVENANCE.md.
