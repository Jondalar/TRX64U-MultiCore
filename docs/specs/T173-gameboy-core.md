# T173 — A Game Boy and a Game Boy Color cartridge play on the C64U as a core, with battery saves

**Status:** PROPOSED (2026-11-05, Alex). Study only: nothing is vendored, nothing is built. Every "proposed, coordinator" is the coordinator's
proposal, not a decision of Alex's; §3a lists what blocks.
**Repo:** TRX64U-MultiCore — a new core `cores/gameboy/` (gateware, `core.toml`, glue), its core application `coreapp/cores/gameboy.cc`, a
vendor script and a generator script in `tools/`, benches in `cores/gameboy/sim/`.
**Number:** T173 (registry: `TODO.md`).
**Origin:** Alex, 2026-11-01, with the link https://github.com/MiSTer-devel/Gameboy_MiSTer: "Do we have the core in the TODO list?", then "Then
please add it" (translated). Boot ROMs (Alex, 2026-11-01, translated): "it uses open source ROMs, SameBoy". Alex, 2026-11-05 (translated): "the
last cores that are missing: ... GameBoy (color / advance). Prepare the specs, so we can build them bit by bit." (the Advance is T183).
**Depends on:** T142 (the DDR2 cartridge path with write-through line caches that hold the machine on a miss), T95 (frame writer, pad, audio
glue of the NES), T104 (generic core application), T120 (licence inventory). Nothing else.
**Milestone:** M3.

---

## §1 The problem

Alex wants the last 8-bit handhelds on the list: a Game Boy (DMG) and a Game Boy Color (CGB) as a core that plays the user's cartridge files with
sound, picture, buttons and battery saves, without any Nintendo ROM in the repository or a release. What there is, what it needs, what it costs:

### What there is (cloned 2026-11-05, shallow, read only; nothing executed)

| Source | Commit / date | Licence (file headers read, not only the README) | What it is |
|---|---|---|---|
| `MiSTer-devel/Gameboy_MiSTer` | `f6796f3a`, 2026-09-21 | **repo level: no licence** (GitHub API `license: null`, no `LICENSE` file). Headers: `rtl/gb.v`, `rtl/video.v`, `rtl/sprites.v` "Copyright (c) 2015 Till Harbaum", GNU GPL **v3 or later**; `rtl/sdram.sv`, `rtl/ddram.sv` GPL (Sorgelig); `rtl/T80/*.vhd` "Copyright (c) 2001-2002 Daniel Wallner", BSD-style redistribution terms; **no header at all**: `gbc_snd.vhd`, `sgb.v`, `lcd.v`, `hdma.v`, `timer.v`, `link.v`, `cart.v`, `speedcontrol.vhd`, `mappers/*`, the savestate files. | The machine: CPU (T80 variant `GBse`), `gb.v` (bus, DMA, timer glue), `video.v` + `sprites*.v` + `lcd.v` (DMG and CGB PPU), `gbc_snd.vhd` (4 channels), `hdma.v` (CGB), `cart.v` + `mappers/` (16 mapper modules), `sgb.v` (Super Game Boy), `link.v`, savestates, cheats, MegaDuck, Workboy |
| `BootROMs/` in the same repo | same | each `.asm` carries the MIT licence text of SameBoy ("Copyright (c) 2015-2023 Lior Halphon"); the README says the sources are adapted from SameBoy | Open boot ROMs for DMG, MGB, SGB, SGB2, CGB, CGB0 (`bin/`: 256 B for DMG/MGB/SGB, 2.2 KB for CGB), built into `cgb_boot.mif` (4 KB), plus MiSTer additions (fast boot, GBA mode flag set from the menu) |
| `mist-devel/gameboy` | n/a (named by the README, not cloned) | the repo is "a port of Gameboy for MiST" | origin of `gb.v`, `video.v`, `sprites.v` (the GPL-3+ headers above) |

**Licence verdict (P1):** usable TREX-internal as GPL v3, the way the other MiSTer cores here are (`cores/nes/rtl/NOTICE`,
`cores/snes/rtl/NOTICE`): the files that say anything say GPL v3-or-later or a permissive BSD-style/MIT text; the repository itself declares
nothing, and most of the machine's files (sound, SGB, mappers, LCD) carry no header. So the licence of those files rests on the port's lineage
and on the repository's silence, which is weaker than the SNES's `LICENSE`. The `NOTICE` has to say that in so many words, with the commit and the
list of headerless files; the T120 inventory gets the rows. Nothing here goes public without that being resolved (the repository is private,
`CLAUDE.md`). The embedded boot ROMs are MIT; the MIT text must stay in the copied `.asm`/`.bin` files and in `NOTICE`.

### What the machine needs (S: read from the source above)

| Memory | Size | In MiSTer | Notes |
|---|---|---|---|
| Cartridge ROM | 32 KiB to 8 MiB (MBC5 up to 8 MiB) | SDRAM | CPU reads one byte per M-cycle; 4.19 MHz CPU, 1 M-cycle = 4 clocks = 954 ns (single speed), 477 ns (CGB double speed) |
| Cartridge RAM | 0 to 128 KiB (MBC5), 512 x 4 bit (MBC2) | SDRAM/DDR; `cart.v:431,445` only 128 B in block RAM | battery backed |
| VRAM | 2 x 8 KiB (`gb.v:827,842`) | block RAM | 4 tiles |
| WRAM | 32 KiB (`gb.v:949`, `dpram #(15)`), 8 banks of 4 KiB on the CGB | block RAM | 8 tiles |
| HRAM | 127 B (`gb.v:914`) | block RAM | <1 |
| OAM | 160 B (`sprites.v:104,118`) | block RAM | <1 |
| Boot ROM | 4 KiB (`gb.v:1019`, `cgb_boot.mif`) | block RAM | 1 tile |
| Colour correction LUT | 2^15 x 24 bit = 96 KiB (`lcd.v:353`, `lcd_color_lut.mif`, 252 KB file) | block RAM | **24 to 32 tiles** (depends on the aspect Vivado picks); 60 to 80 % of a DFX core's 40 tiles |
| SGB | tile RAM 4 K x 16 (`sgb.v:521`), `sys_pal_ram` 2 K x 15, `attr_files_ram` 2025 x 16, tile map 896 x 16 | block RAM | about 5 tiles; plus the 256 x 224 border picture |

Clocks (`rtl/pll.v` retrieval info): 33.554432 MHz system clock (8 x 4.194304 MHz), 67.1 MHz RAM clock, 100.66 MHz. `speedcontrol.vhd` makes the
CPU enable from the system clock and has `pause`, `cart_act`, `save_act`, `DMA_on` inputs: the hook a DDR2 miss needs (hold the machine) is already
in the machine.

### Which shape the core takes here (S: `CLAUDE.md`, `docs/layering-map.md`, `cores/nes/mono/core.toml`, `contract/layer2_ports.txt`)

A DFX core (`pb_l2`: **13,600 LUT, 40 block RAM tiles, 40 DSP** since T164's second cut; `nes` at 13,181 LUT no longer fits and became a large
core) gets, among the 247 contract ports: the DDR2 client `c_*`, the audio ports, `pix_*`, the `cw_*` register window and the application's
DMA window `win_*`. The picture of a DFX core is written by the core into frame buffers in DDR2 (RGB565; `cores/nes/trx/nes_pixw.v`, rows
through the host's `e_` port) and read by the host's `pix_layer`. 15-bit Game Boy Color pixels fit RGB565. The SNES shows what a large core
does: its own copy of `pix_layer`, cartridge in DDR2 behind line caches, the core application loads ROM and saves (`cores/snes/trx/snes_mem.v`,
`coreapp/cores/snes.cc`).

### Parts (MECE; every §2 scenario solves exactly one)

- **P1 Licence and provenance** of every file that goes in (verdict above).
- **P2 The machine runs**: LR35902 CPU, timer, interrupts, DMA/HDMA, CGB double speed, DMG and CGB modes, from our own test programs.
- **P3 Area fit**: logic and DSP against `pb_l2` (or the large-core ceiling if it does not fit).
- **P4 Block RAM** against 40 tiles.
- **P5 Cartridge**: header, mappers, ROM and RAM in DDR2 behind caches with DDR2 latency (293-693 ns, T142 S2), loaded by the core application.
- **P6 Boot ROM**: the machine starts without a Nintendo ROM.
- **P7 Picture**: 160 x 144 into the frame buffer, DMG shades, CGB colour, geometry on the HDMI image.
- **P8 Sound**: four channels to the host's audio ports.
- **P9 Input**: eight buttons from the C64U's joystick port and keyboard.
- **P10 Saves and RTC**: battery RAM and the MBC3 real-time clock, written back.
- **P11 Extras** that MiSTer has and Alex may or may not want (SGB, link port, savestates, rewind, cheats, special mappers): all open, §3a.

### The numbers per part

**P3 Logic area — [E], not measured.** No resource report exists in the repository (`Gameboy.srf` lists suppressed messages only). Bracket from the
parts, with the warning from this project that estimates move both ways (the NES: estimated about 3,200 LUT, routed 13,114, T95 §5.17; the SNES:
estimated centre 13,000, measured about 10,350 for core and glue together, T142 §5: 21,769 LUT whole design minus the host's 11,417):
T80/GBse CPU 1,500-2,500, PPU with sprites and CGB palettes 2,000-3,500, sound 1,200-2,000, mappers/DMA/timer/cart 1,200-2,000, boot ROM and
glue to the machine 500 = **DMG+CGB machine 6,500-10,500 LUT**; our glue (DDR2 client and caches, frame writer, registers, pad, audio; the
NES's was 1,569 LUT and about 11 tiles, T142 §1) 1,500-2,000. **Total 8,000-12,500 LUT against 13,600 in `pb_l2`: plausible as DFX, with the
top of the range at the edge where the NES fell over.** SGB adds 1,500-2,500 LUT [E]. The colour correction by LUT does not fit (P4); done by
arithmetic it needs 3 multiplications per channel pair in DSP slices (few of the 40).

**P4 Block RAM — [E] from the sizes above.** Without the colour LUT and without SGB: VRAM 4 + WRAM 8 + HRAM/OAM 1 + boot ROM 1 + ROM line cache
(8 KiB) 2 + cart RAM cache (2 KiB) 1 + frame writer 2 + sound/misc 1 = **about 20 tiles**; with SGB about 25; with the 96 KiB colour LUT as upstream
has it 44 to 52 — over the line. The colour LUT must go or be computed (§3 T4). Cartridge ROM and RAM are in DDR2, not block RAM.

**P5 Cartridge — [S].** The header (`$0143` CGB flag, `$0146` SGB flag, `$0147` cartridge type, `$0148` ROM size, `$0149` RAM size) is snooped in
`cart.v` from the download stream; here the application loads the file straight into DDR2, so the header values reach the core as registers
(the SNES way: `rom_type`, `rom_mask`, `ram_mask`). Access pattern: sequential code and bank switches; the NES/SNES direct-mapped line cache with
write-through and a hold on a miss applies unchanged. At 954 ns per M-cycle (477 ns in double speed) DDR2's 293-693 ns is the same order as
one bus cycle, so a miss holds the machine for at most one to two M-cycles; the stall cost is the miss rate times that, [E] below 3 % with a
next-line prefetch; **S4 measures it**.

**P6 Boot ROM — [S] for the files, [E] for the behaviour.** The open DMG boot ROM loads the logo bitmap from the cartridge header (`dmg_boot.asm:68-82`);
whether it compares the logo (the original locks on a mismatch) is not read yet and S6 decides it from the machine's behaviour. The MiSTer
additions (fast boot, GBA-mode flag) are controlled by checksums in `Gameboy.sv` (`MISTER_CGB0_CHECKSUM`, `ORIGINAL_CGB_CHECKSUM`) when a user ROM is loaded.

**P7 Picture — [S].** 160 x 144, 59.73 Hz, DMG 4 shades through a palette, CGB 15-bit. MiSTer scales and borders on the framework side; here the
frame writer (`nes_pixw.v` pattern, RGB565, 160 x 144, 320 B a row) and the geometry registers of T118 do it. SGB is 256 x 224 with a border.

**P8 Sound — [S].** `gbc_snd.vhd` (68 KB of VHDL, 4 channels, stereo mix) to `audio_l/r/audio_sample_en` as `cores/nes/trx/nes_audio.v`.

**P9 Input — [S].** Eight buttons; the NES pad glue (`nes_pad.v`) is the pattern: joystick = D-pad + A, keyboard = B, Select, Start (map is Q7).

**P10 Saves and RTC — [S].** Battery RAM to `<core folder>/saves/<image name>.sav` written back when it changed (the SNES way, `snes.cc`); MBC3
RTC: `cart.v` takes `RTC_time` and has `RTC_savedtimeOut` and `RTC_timestampOut` so it can count the time the console was off; those must be kept
in a second save file and the real time has to come from the host; where the host gets its wall clock is not read yet (Q10).

## §2 Scenarios — what proves it is solved

The test material is our own: test programs written for this project and assembled by a small script (as `cores/nes/sim/mkcart.py`); no
third-party ROM goes into the repository (`CLAUDE.md`). Third-party test ROMs and games are hand steps on the board (Alex's files).

```gherkin
Scenario: S2 the CPU, timer and interrupts do what the instruction set says
  Given a self-checking test program of ours (every opcode group, DAA, flags, HALT, EI delay, timer and interrupt dispatch) as a cartridge in an ideal memory
  When  the bench runs it on the machine in DMG mode and in CGB mode (single and double speed)
  Then  the program stores the signature 0xA5 and the check count at 0xC000 (WRAM) and the bench reads exactly those values
  And   the control, one opcode of the program patched to give a wrong flag, ends with 0xFF at 0xC000 and the bench reports FAIL
```

```gherkin
Scenario: S4 DDR2 latency changes the speed, never the result
  Given the S2 program plus a bank-switching program, ROM and cartridge RAM served by the DDR2 model with 293-693 ns latency behind the line caches
  When  both run to their end
  Then  WRAM, VRAM and cartridge RAM dumps are bit for bit those of the run with an ideal zero-latency memory
  And   the machine is held for less than 3 % of its clocks (reported), and the control (cache lookup bypassed so a stale line is served) changes a dump byte and fails
```

| # | Solves | Scenario | Test (bench / board step) | Where it runs |
|---|---|---|---|---|
| S1 | P1 | The vendored tree equals the upstream commit (`SHA256SUMS`), the `NOTICE` lists GPL v3, the headerless files and MIT for the boot ROMs; the lint checks the inventory row (T120). Control: one byte of a vendored file changed -> lint red | `tools/vendor_gameboy.sh --check`, `ci/lint.sh` | CI lint |
| S2 | P2 | see Gherkin above | `cores/gameboy/sim/cpu/run.sh` | `ci/sims.txt` tier 2 |
| S3 | P3, P4 | The DFX build of the core routes in `pb_l2` with <= 13,600 LUT and <= 40 tiles, WNS >= 0, from `utilization_pb_l2.rpt`. Control: the upstream colour LUT left in must push the tiles over 50 and fail the lane (proves the check bites) | Vivado report of the core stage | CI `bitstream` (builder) |
| S4 | P5 | see Gherkin above | `cores/gameboy/sim/mem_ref/run.sh` (ideal) vs `run_ddr2.sh` | `ci/sims.txt` tier 2 |
| S5 | P5 | A generated ROM, every bank filled with its own number, with the header of MBC1, MBC2, MBC3, MBC5 (and every further mapper Q2 admits): a program switches banks in every legal order; reads equal the bank number; ROM-size and RAM-size edge cases (32 KiB without mapper, 8 MiB, MBC1 mode 1 wrap) wrap as the header says. Control: header cartridge-type byte changed -> the bank read-back mismatches | `cores/gameboy/sim/mappers/run.sh` | tier 2 |
| S6 | P6 | With the open boot ROMs the machine reaches `$0100` of our test cartridge with the post-boot register values of DMG (A=`$01`) and CGB (A=`$11`), and writes `$FF50`. Control: the boot ROM image with one byte changed does not reach `$0100` in the run time the clean one needs | `cores/gameboy/sim/boot/run.sh` | tier 2 |
| S7 | P7 | A test program draws a DMG tile pattern with all four shades and a CGB palette test (8 BG + 8 OBJ palettes, priority, flips, 8x16 sprites, 10 sprites per line); the bench takes the frame buffer words and compares their hash with the golden hash of the first run that was checked by eye on the board; the 4 shades differ and the DMG palette setting changes the colours. Control: swapped BGR order flips the hash | `cores/gameboy/sim/video/run.sh`, board step B1 | tier 2, board |
| S8 | P8 | A program starts each channel in turn; the audio samples at the port show the expected frequency (zero-crossing count) per channel and nothing for the muted ones. Control: the channel enable mask zero -> the bench must see silence (a dead capture would pass this one, so the other half checks a non-zero RMS) | `cores/gameboy/sim/sound/run.sh` | tier 2 |
| S9 | P9 | The joystick port directions and fire and the keyboard keys produce the eight `P1` register bit patterns, with the select lines; no stuck button after release. Control: swap two keys -> the bench fails | `cores/gameboy/sim/pad/run.sh` | tier 2 |
| S10 | P10 | The core application writes the battery RAM to the `.sav` file after a change and only then; a second run with that file starts with the same RAM; the MBC3 clock keeps counting across a power-off by the saved timestamp. Control: save file removed -> the RAM is empty and the test says so | app test (`ci/apptests.sh`) + `cores/gameboy/sim/saves/run.sh` | app tests |
| S11 | P2, P5, P7, P8 | Alex's commercial cartridge (a DMG game, a CGB game, an MBC5 game with battery) plays: picture, sound, buttons, a save survives a core restart and a power-off. Control: none beyond the test itself; it is a hand step | board step B2, with a photo | board session |

Rules kept: every part has a scenario (P11 is §3a), every scenario one part (S11 closes the use case on the board as the use-case test and names P2 as its
main part; it is not a bench). The board steps are batched into one session when the machine is finished (`docs/board-tests-pending.md`), not before.

## §3 Decision

Nothing here is decided with Alex. Technical proposals follow, **proposed, coordinator**.

**T1 One core for DMG and CGB** (Q1): `cores/gameboy/`, the machine is the upstream one, which selects DMG or CGB mode itself from the header flag
`$0143` (`isGBC`); one boot ROM set (DMG, CGB, optionally SGB).
**T2 Source base:** the upstream `rtl/` is copied pristine by `tools/vendor_gameboy.sh` (with `SHA256SUMS`, like `tools/vendor_snes.sh`); the VHDL
parts (T80, `gbc_snd.vhd`, `speedcontrol.vhd`, `dpram.vhd`) go through the GHDL-to-Verilog route of `tools/nes_gen.py` / `tools/snes_gen.py`
into a tracked `gen/`; the MiSTer-only parts (savestates, cheats, rewind, `ig_kb`, MegaDuck laptop, Workboy, `sdram.sv`, `ddram.sv`, `pll`) are
not copied. Lost: SNEStang-style reductions do not exist for the Game Boy, so there is no alternative base.
**T3 Shape:** DFX first (S3 decides), large core as the fallback with the same glue; the lane is `core.toml` + `sources` as the NES; the
descriptor `experimental = true` until the board has seen it.
**T4 Colour:** the 96 KiB `lcd_color_lut` is not instantiated; the CGB colour is either the raw 15 -> 16 bit expansion or the MiSTer correction as
a fixed matrix in DSP slices (Q5). No 32-tile LUT.
**T5 Memory:** ROM and cartridge RAM in the core's DDR2 region (client `c_*`), a direct-mapped write-through line cache per stream (ROM 8 KiB, RAM
2 KiB) that takes the clock enable away from the whole machine on a miss, through `speedcontrol.vhd`'s `pause`; ROM at +0 (8 MiB), RAM at
+0x800000, frame buffers above, 4 buffers of 160 x 144 x 2 B; a prefetch of the next line.
**T6 Clock:** CPU enable from a phase accumulator on the host clock (4.194304 MHz, 8.388608 MHz in double speed), as `nes_core.v`; no second PLL.
**T7 Boot ROM:** the MIT SameBoy boot ROMs (`BootROMs/bin`) embedded as a `.mif`/`$readmemh` image in block RAM, with the MIT text in `NOTICE`; an
original boot ROM (user's file, `dmg_boot.bin` / `cgb_boot.bin` in `<core folder>/roms/`) replaces it when present, never in the repository or a release.
**T8 Picture:** `gb_pixw.v` after `nes_pixw.v` (RGB565, 160 x 144), DMG shades by a palette register of the core application (grey, green, a
user choice later), geometry via the T118 registers.
**T9 Input and sound:** `gb_pad.v` after `nes_pad.v`, `gb_audio.v` after `nes_audio.v`.
**T10 Core application:** `coreapp/cores/gameboy.cc` after `nes.cc` and `snes.cc`: file pick (`.gb`, `.gbc`), header read and register set, ROM into DDR2, save
file in the core's `saves/` folder, RTC file; a `core.toml` `roms = [...]` list of the optional boot ROM names. Battery RAM is written back on change.
**T11 Verification:** only our own test programs, assembled by a script; benches in iverilog with the GHDL-converted VHDL as the NES does; board
steps by hand with Alex's cartridges. Public test ROMs (Blargg, Mooneye, acid2) only as hand steps unless Q9 allows them in `sim/`.

## §3a Open questions for Alex (one per line, to be asked one at a time, in the order they block)

1. **Q1** One core for the Game Boy and the Game Boy Color (the machine switches itself by the cartridge's header), or two cores?
2. **Q2** Which mappers must v1 carry: MBC1/2/3 (with clock)/5 only, or also HuC1/HuC3, MBC6/7, Pocket Camera, TAMA5, MMM01 and the Sachen/Wisdom Tree set?
3. **Q3** Is the Super Game Boy in v1 (256 x 224 border, +5 tiles, +1,500-2,500 LUT) or later?
4. **Q4** DFX with a large-core fallback if S3 fails (proposed), or large from the start?
5. **Q5** CGB colours: the raw 15-bit expansion, or the LCD colour correction as a computed matrix; and which DMG palette by default (grey or green)?
6. **Q6** Boot: the open SameBoy ROMs run with their boot animation, or fast boot by default?
7. **Q7** Key map: joystick = D-pad + A, keyboard = B, Select, Start (as the NES), or other?
8. **Q8** Link port (needs pins, no second console here), savestates, rewind and cheats: all out of v1?
9. **Q9** May public test ROMs (dmg-acid2, cgb-acid2, Blargg, Mooneye; licences to be checked per ROM) go into `cores/gameboy/sim/`, or only be a hand step on the board?
10. **Q10** MBC3 clock: the time while the console is off from the host's real-time clock, or frozen while off?

## §4 Out

Decided with Alex later; the candidates are the questions above.

## §5 Decisions taken while building

(empty)

---
