# Other cores: candidates

Source: TODO.md section 6 (candidates picked 2026-10-01); moved here 2026-10-03. Task: TODO.md T23.

- MiSTer or other FPGA cores: VIC-20, Plus/4, C128, Spectrum, ZX81.
- Candidates picked 2026-10-01: Till Harbaum's MiSTle cores for the Tang
  boards (github.com/MiSTle-Dev, VHDL, GPL-3.0): **VIC20Nano**, **C16Nano**
  (C16/Plus4), **NanoApple2**. Small 8-bit cores that fit the 50T and block
  RAM. Port: Gowin PLLs -> MMCMs, SDRAM -> block RAM or our DDR2 path,
  video into vic_scaler (1080p), audio to I2S/HDMI, input from our keyboard
  and joysticks instead of the FPGA-Companion MCU.
- Not now: NanoMig (Amiga), MiSTeryNano (Atari ST), NanoMac (no licence
  stated, need low-latency RAM), snescore (needs fast random ROM/RAM).
- VIC-20: MikeJ's VHDL core (2008, BSD-style licence, no ROMs included),
  described at fpgaarcade.com/kb/commodore-vic-20. Its old source link
  (github.com/FPGAArcade/replay_release/tree/master/vic20) is gone; a
  maintained copy is github.com/MiSTer-devel/VIC20_MiSTer (rtl/: m6561 VIC,
  via6522, T65 CPU, megacart, c1530 tape, iec_drive 1541). Check each file's
  licence before vendoring.
- Atari 800 XL (T88, 2026-10-13): no Atari 800 core exists among the MiSTle-Dev repositories. Base:
  github.com/MiSTer-devel/Atari800_MiSTer (`rtl/common`, Mark Watson, VHDL, non-commercial licence,
  see `cores/atari800/rtl/NOTICE`; `cpu_65xx.vhd` is "All Rights Reserved"). Third-party Tang Nano 20K port for reference:
  github.com/carlosbravoa/atari800_tang_nano20k (NTSC only). See docs/specs/T88-atari800-core.md.
- NES (T95, 2026-10-15): Brian Bennett's `fpga_nes` (github.com/brianbennett/fpga_nes, Verilog, BSD 2-clause), mapper 0 only, no DMC. Lost: `MiSTer-devel/NES_MiSTer` (GPL v3,
  cycle accurate, the mappers, SystemVerilog, SDRAM) and `nand2mario/nestang` (its Tang Nano reduction). Q1 of docs/specs/T95-nes-core.md asks which one carries the mappers.
- BBC Micro (T125) and Commodore PET (T126, merged into T134 on 2026-11-05), 2026-10-24 (Alex: "In die Liste der Cores bitte noch BBC und PET"): on the list;
  source and licence to be chosen in their specs.
- Acorn Archimedes (T165), 2026-10-31 (Alex: "Nimm ihn auf die Liste bitte" - "put it on the list, please"): on the list; a candidate
  source is MiSTer's Archie core (ARM2, MEMC, VIDC, IOC), licence and size not checked; RISC OS is a ROM that never goes into a release.
- Game Boy / Game Boy Color (T173), 2026-11-01 (Alex: "Dann bitte hinzufuegen" - "then please add it"): on the list; candidate source
  MiSTer-devel/Gameboy_MiSTer (no repository licence; file headers GPL v3+ / BSD-style / MIT for the SameBoy boot ROMs, many files headerless; read 2026-11-05); it embeds
  open boot ROMs from SameBoy (no Nintendo ROM needed). Spec: docs/specs/T173-gameboy-core.md.
- Game Boy Advance (T183), 2026-11-05 (Alex: "GameBoy (color / advance)"): candidate MiSTer-devel/GBA_MiSTer (GPL v2, VHDL without headers); a large core at best, expected not to fit the 50T
  (spec T183: 18,500-20,000 LUT estimated against 17,034 usable); BIOS: the embedded one has no stated origin, MIT replacement Cult-of-GBA/BIOS. Spec: docs/specs/T183-gba-core.md.
- SNES, Neo Geo and Atari ST (T130), 2026-10-28: the cores that would need the host's pixel layer with 32-bit pixels
  (T136 D1). Alex (translated): "Apart from the Amiga AGA core (and that one is large) I cannot think of a built core. It
  gets interesting again with SNES and (CAREFUL, on the list to check!) Neo Geo. And maybe Atari ST." Then (translated): "Though
  those will rather be large cores too, I think" - as large cores they carry their own copy of the pixel layer (as the
  A1200, T136 D1) and the host's stays as it is. Neo Geo is on the list to check: source, licence and whether it fits
  the 50T and its DDR2 are open.
