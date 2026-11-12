# T183 — A Game Boy Advance cartridge plays on the C64U as a large core, if it fits the XC7A50T

**Status:** PROPOSED (2026-11-05, Alex). Study only; the main question is go or no-go. Expected verdict of this study: **does not fit as the
upstream core is written; decided only by a synth-only trial (S2) that costs no core code.** Every "proposed, coordinator" is the coordinator's
proposal, not a decision of Alex's.
**Repo:** TRX64U-MultiCore — if it goes: a new large core `cores/gba/` (`mono/core.toml`, `delivery = "cfw"`, glue in `trx/`), its core
application `coreapp/cores/gba.cc`, `tools/vendor_gba.sh`, `tools/gba_gen.py`, benches in `cores/gba/sim/`.
**Number:** T183 (registry: `TODO.md`).
**Origin:** Alex, 2026-11-05 (translated): "the last cores that are missing: ... GameBoy (color / advance). Prepare the specs, so we can build
them bit by bit." (the Game Boy and Color are T173).
**Depends on:** T142 (DDR2 cartridge path, line caches, hold on a miss; the flat-build figures), T143 (second large core, same shape), T136
(own `pix_layer` copy of a large core), T120. Nothing else.
**Milestone:** M3.

---

## §1 The problem

Alex wants a Game Boy Advance. It is the first core on the list whose size is a real question: a 32-bit ARM7TDMI, a four-layer tile/affine
PPU with sprites, windows and blending, DMA, timers, six sound channels, 96 KiB of video RAM, 32 KiB + 256 KiB of work RAM and a cartridge up to
32 MiB. The study answers: does MiSTer's GBA core fit the XC7A50T beside the host, can its memories live behind our DDR2 with its 293-693 ns
latency (T142 S2), what has to be cut, and what is the BIOS.

### What there is (cloned 2026-11-05, shallow, read only; nothing executed)

| Source | Commit / date | Licence | What it is |
|---|---|---|---|
| `MiSTer-devel/GBA_MiSTer` | `93790a02`, 2026-07-22 | repo level: **GPL v2** (GitHub API `gpl-2.0`, `LICENSE` file is the GPL version 2 text of June 1991). The VHDL files carry **no header**; `GBA.sv` and `ddram.sv` carry "Copyright (C) 2019 Robert Peip" / "Sorgelig" lines. The text says version 2; "or later" is not stated anywhere I read. | VHDL, 1.03 MB in `rtl/`: `gba_cpu.vhd` (ARM7TDMI, ARM and Thumb, 183 KB), `gba_gpu*.vhd`/`gba_drawer*.vhd` (PPU, about 290 KB), `gba_memorymux.vhd` (bus, 76 KB), `gba_dma*.vhd`, `gba_sound*.vhd`, timers, serial, `gba_gpioRTCSolarGyro.vhd`, `gba_bios.vhd` (the BIOS as a 4096 x 32 array, 83 KB), `gba_savestates.vhd`, `gba_cheats.vhd`; SystemVerilog top `GBA.sv`, SDRAM and DDR controllers |

**Licence verdict (P1): usable TREX-internal as GPL v2 (only), with two actions.** (1) `NOTICE` must say GPL v2 and that the files carry no header; the
T120 inventory gets the row. Our glue is separate files with their own licence; the combination is internal, as every other core here. (2) The
**BIOS** (below). Nothing else in `rtl/` is third-party code of another licence that I found (no vendor IP in the VHDL; `pll.v` and `*.qip` are Intel
files and are not copied).

### The BIOS (P1b)

The GBA does not boot without BIOS code at `$00000000` (SWI functions such as Div, CpuSet, LZ77, ObjAffineSet, the interrupt entry). The upstream README
says: "Opensource Bios from Normmatt is included, however it has issues with some games. Original GBA BIOS can be placed to GBA folder with name
boot.rom"; the "Homebrew BIOS" menu setting avoids the logo check. What I could establish:

- The embedded array is `rtl/gba_bios.vhd` (4096 words = 16 KiB). Its header names no origin.
- `github.com/Normmatt/gba_bios`, the repository the README's wording points to, is on GitHub a **fork of a disassembly of the Nintendo BIOS**
  (description "Disassembly of the Game Boy Advance BIOS", no licence field). A disassembly of the original is derived from Nintendo's copyrighted
  code. Whether the embedded 16 KiB is that disassembly's output or an independent work is **not established** by what I read. Until it is, it
  must not go into a release, a `.cfw` or a package (`CLAUDE.md`: ROM, Kickstart and disk images never go into a release).
- `Cult-of-GBA/BIOS` (`github.com/Cult-of-GBA/BIOS`, last push 2025-04-06): "A Game Boy Advance replacement BIOS. Written completely in ARM assembly
  from scratch." Licence MIT (GitHub API `MIT`). Its README says high accuracy is a non-goal and the sound functions are not all implemented
  (TODO list). It is the clean candidate for an embedded BIOS, to be built with an ARM assembler and compared by our tests (S9).
- The original BIOS (16 KiB) is the user's file in the core's `roms/` folder, never in the tree.

### What the machine needs (S: read from the VHDL; sizes from the generics)

| Memory | Size | MiSTer | Notes |
|---|---|---|---|
| Cartridge ROM | up to 32 MiB (`Softmap_GBA_Gamerom`, 8,388,608 words) | SDRAM (README recommends it over DDR: "some games may slowdown or lose sync when using DDR-RAM") | N access 4 cycles = 238 ns, S 2 cycles = 119 ns at 16.78 MHz (16 bit bus) |
| EWRAM | 256 KiB (`Softmap_GBA_WRam`) | SDRAM/DDR | 16-bit bus, 3 cycles = 179 ns per half-word, a 32-bit access 358 ns |
| IWRAM | 32 KiB (`gba_memorymux.vhd:297`) | block RAM | 8 tiles |
| VRAM | 96 KiB (`gba_gpu_drawer.vhd:719` 4 x 8 bit x 2^14 words = 64 KiB, `:751` the upper 32 KiB) | block RAM | 24 tiles |
| BIOS | 16 KiB | block RAM | 4 tiles |
| Palette + OAM | 1 KiB + 1 KiB (`gba_gpu_drawer.vhd:789-986`, 7 x 256 B byte RAMs) | block RAM | about 3.5 tiles |
| Line buffers | 16 x (256 x 16 bit) in `gba_gpu_drawer.vhd:1701-1988` of which 6 serve the 2x resolution mode (`_hd0/_hd1`) + 2 in `gba_top.vhd:1145,1168` | block RAM | about 5 tiles without the 2x ones |
| Gamepak prefetch cache | 1024 x 32 bit (`gba_memorymux.vhd:327`) | block RAM | 1 tile |
| Save memory | SRAM 32 KiB, Flash 64/128 KiB, EEPROM 512 B/8 KiB (`Softmap_GBA_FLASH`, `_EEPROM`) | SDRAM/DDR | type found by a string scan of the ROM (`FLASH1M_V` in `GBA.sv:263`) |

Clocks: `rtl/pll.v` gives 100.663296 MHz (`clk100`, exactly 6 x 16.777216 MHz) and 50.33 MHz for video. The core's own state machines run on `clk100`;
`gba_top.vhd` has `GBA_cputurbo` ("cpu free running, all other 16 mhz"), which says the 16.78 MHz timing is clock enables on the 100 MHz clock.

### Which shape (S: `docs/specs/T142-snes.md` §1, `cores/snes/mono/core.toml`)

DFX is out for any GBA: `pb_l2` is 13,600 LUT / 40 tiles / 40 DSP. A **large core** is the only frame: host and core flat in one bitstream, started as
a `.cfw`, its own copy of `pix_layer`, DDR2 region `0x01000000 - 0x07FFFFFF` for the core (112 MiB, T142). Measured on this board (T142 §1 from the C128's
flat build, `utilization_hier.rpt`): host 11,417 LUT, 13,573 FF, 13 tiles (+3.5 with the menu overlay), 9 DSP. The **highest flat result that has ever
routed is the C128's 87.27 % = 28,451 LUT**, i.e. **17,034 LUT for core and glue together**; 100 % of 32,600 would leave 21,183, which no routed design
has reached. The SNES routed at 21,769 LUT (66.8 %) with 71 of 75 tiles (T142 §5), the Neo Geo at 26,832 (82.31 %), 72 tiles (T143 §5).

### Parts (MECE; every §2 scenario solves exactly one)

- **P1 Licence and BIOS** (above).
- **P2 Logic fit**: ARM7TDMI + PPU + DMA + sound + glue in what is left of 32,600 LUT.
- **P3 Block RAM**: against 75 tiles.
- **P4 Memory latency**: ROM and EWRAM in DDR2 at 293-693 ns against 119-358 ns bus cycles.
- **P5 Clock and timing**: a 100.66 MHz machine on the Artix-7 beside the host.
- **P6 The machine runs**: ARM and Thumb instruction set, exceptions, DMA, timers, interrupts, BIOS calls — from our own test programs.
- **P7 Picture**: 240 x 160 into the frame buffer (15-bit colour, 59.73 Hz).
- **P8 Sound**: two direct-sound FIFOs + four PSG channels.
- **P9 Input and media**: ten buttons, ROM load, save types.
- **P10 Extras** MiSTer has (2x resolution, savestates, rewind, cheats, flicker blend, shaders, solar sensor/gyro/tilt/rumble, RTC): all open.

### The numbers per part

**P2 Logic area — [E], not measured; this is the verdict part.** No resource report is in the repository (`GBA.srf` is a list of suppressed messages) and
I will not quote Intel ALM figures from memory as measured. Two independent estimates from this project's own measurements:

- *By source size against the SNES, which is measured.* Non-comment VHDL lines of the machine (counted 2026-11-05, savestates, cheats, BIOS data,
  GPIO and serial excluded): GBA about **12,150**; the SNES subset (S-CPU, S-PPU, S-DSP, S-SMP, 65C816, SPC700, PKGs) about **7,400**, which routed at
  about **10,350 LUT** core plus glue (T142 §5). Ratio 1.64 → machine about 17,000 plus glue 1,500-2,000 = **18,500-19,000 LUT**.
- *By parts.* ARM7TDMI with ARM and Thumb decoding, 31 banked registers, barrel shifter and a 32 x 32 multiplier: 3,500-5,500 LUT; the PPU with four
  background layers, text and affine modes, bitmap modes 3-5, 128 sprites with affine, windows, mosaic, alpha blending and brightness: 7,000-11,000;
  bus mux, DMA (4 channels), timers, interrupts, cartridge GPIO: 2,500-4,000; sound (2 FIFO + 4 PSG channels, mixer, bias): 1,500-2,500; glue 1,500-2,000
  → **16,000-25,000 LUT, centre about 20,000**.

Both land at **18,500-20,000 LUT against 17,034 that has ever routed (87.27 %)**. Even the low end of the by-parts bracket (16,000) sits at the ceiling, and
this project's estimates have been wrong by factors of 0.8 (SNES) and 4 (NES).

**Verdict: no-go for the core as upstream wrote it (expected 18,500-20,000 LUT against 17,034 usable). It is not impossible; it is not likely.** What would have to give,
in the order of the saving ([E]):
1. 2x resolution path (the `_hd` line buffers and the second drawers' width): 1,000-2,000 LUT, 6 tiles. Never instantiate savestates, rewind, cheats, the
   `gba_gpu_colorshade.vhd` shader (about 15 KB of VHDL), flicker blend: nothing saved against the figures above (they are not in the count), but each
   must stay out.
2. Sprite/affine throughput: the PPU is line-parallel; a re-pipelined drawer that shares one multiplier set would save thousands of LUT but is a rewrite of
   about 290 KB of VHDL, not a port.
3. The host's share: the flat build already drops Ethernet, the VIC path and the C64 text layer (`mono_env` of the NES/SNES). No further host cut is known.
4. Not free: the ARM7TDMI is the one block that cannot be traded away.
Nothing in 1-3 closes a 1,500-3,000 LUT gap reliably; it is the synth-only trial that settles it, and it costs a day, not a core.

**P3 Block RAM — [E] from the generics.** IWRAM 8 + VRAM 24 + BIOS 4 + palette/OAM 3.5 + line buffers 5 + cartridge cache 1 + frame writer 2 + save/ROM
caches (ROM 8 KiB 2, EWRAM 4 KiB 1, save RAM 1) = **about 50.5 tiles**, with EWRAM (64 tiles), ROM and saves in DDR2. Host 13 (overlay stubbed as the C128 and
Neo Geo do) → **63-64 of 75: fits**, in the range of the SNES (71) and the Neo Geo (72). Block RAM does not decide the verdict; logic does. EWRAM in block
RAM (64 tiles) is impossible.

**P4 Memory latency — [S] machine, [bench-model] DDR2.** The GBA bus mux already waits on a handshake (`sdram_read_ena`, `sdram_read_done`) and has a
prefetch buffer with a second dword (`cache.vhd`, `sdram_second_dword`), so the machine tolerates slow memory without a new hold mechanism. But the budget is
short: an N access to ROM is 238 ns, an EWRAM half-word 179 ns, our DDR2 round trip 293-693 ns. Every cache miss costs more than the bus cycle itself. MiSTer's own
README says the DDR variant (several hundred ns on the DE10-Nano) slows or desyncs some games. We hold the whole machine on a miss (the SNES way): speed drops
instead of correctness, but picture and sound are machine-timed, so frame rate and pitch fall with the miss rate. Code that runs from EWRAM (common, 256 KiB)
and random ROM reads (N accesses) miss most. [E]: 5-15 % of the clocks held on mixed software, more on EWRAM-heavy games; S5 measures it.

**P5 Clock and timing — [E].** 100.66 MHz is a real clock in the upstream design (`clk100` into every entity), not an enable over a slow clock. The SNES
timed at 21 MHz enables with multicycle paths (`cores/snes/core_timing.tcl`). Here six cycles of a 100 MHz clock separate the enables, and the
upstream VHDL was written for Cyclone V, not with multicycle paths in mind: the ARM core's 183 KB of deep combinational code (barrel shifter into ALU
into address register) at 100 MHz on a -1 Artix-7 is a risk of its own, with the host's 150 MHz DDR2 clocks beside it. Recipe like the SNES: every
clock an enable on the host's `cpu_clk` if it is fast enough; the host's clocks are read in the first step.

**P7 Picture — [S].** 240 x 160, 15-bit BGR, 59.73 Hz: 77 KB a frame, 4.6 MB/s — far below the C128's 26 MB/s. RGB565 in the frame writer, 4 buffers, the T136
pattern. The 2x mode (480 x 320, 307 KB a frame) is out of the fit.

**P8 Sound — [S].** Direct sound A/B (8 bit, DMA-fed FIFOs) + PSG 1-4, mixed with the bias level, to `audio_l/r`, 32,768 Hz mix. Pitch stalls with the miss
rate (P4) unless the sound domain is decoupled; the flat core's FIFO to the host mixer as T142 D5.

**P9 Input and media — [S].** Ten buttons (A, B, L, R, Select, Start, 4 directions): joystick = D-pad + A, keyboard the rest (as the SNES). ROM into DDR2 by
the core application (up to 32 MiB), the save type found as in `GBA.sv` (strings `EEPROM_V`, `SRAM_V`, `FLASH_V`, `FLASH512_V`, `FLASH1M_V`), the save
file `<core folder>/saves/<image name>.sav`, written back on change.

## §2 Scenarios — what proves it is solved

The first scenario is the gate: nothing but the study's own measurement is paid for before S2 is green. Test programs are our own (assembled by a script,
no third-party ROM in the repository); commercial games are hand steps.

```gherkin
Scenario: S2 the machine, without the extras, fits the board (the go/no-go gate)
  Given the vendored machine (CPU, PPU, DMA, timers, sound, memory mux), no savestates, no rewind, no cheats, no shader, no 2x path, memories as P3
  When  Vivado synthesises it alone (synth only, out of context, XC7A50T) on the builder
  Then  the report line "Slice LUTs" of the machine is at most 14,500, so that glue and host land at or below the 87.27 % of the C128 (28,451 LUT)
  And   the control: the same run with the 2x path and the shader left in must report more LUT than the clean one (a trial that cannot tell them apart proves nothing)
```

| # | Solves | Scenario | Test (bench / board step) | Where it runs |
|---|---|---|---|---|
| S1 | P1 | The vendored tree equals the upstream commit (`SHA256SUMS`); `NOTICE` says GPL v2, headerless files, and names the BIOS in use with its licence. Control: one changed byte -> lint red. The embedded BIOS is the MIT one or none | `tools/vendor_gba.sh --check`, `ci/lint.sh` | CI lint |
| S2 | P2 | see Gherkin; the number decides go or no-go with Alex | `tools/gba_area.sh` (after `tools/nes_area.sh`, T95) | builder, by hand |
| S3 | P2, P3 | The flat build routes at <= 87.3 % LUT, <= 72 tiles, WNS >= 0 on `cpu_clk` and the machine clock, from `utilization.rpt` + `utilization_hier.rpt` of `cores/gba/mono/build.sh`. Control: none beyond the reports; the lane is red when a number is over | Vivado of lane `large-gba` | CI `bitstream` |
| S4 | P6 | Our ARM and Thumb test programs (every data-processing opcode with all flag cases, shifts and rotates, MUL/MLAL, LDM/STM, SWP, branch with exchange, exceptions and banked registers, condition codes) store a signature in IWRAM; the bench reads exactly it. Control: one opcode patched to a wrong condition -> signature differs, bench FAIL | `cores/gba/sim/cpu/run.sh` | `ci/sims.txt` tier 2 |
| S5 | P4 | The S4 programs plus a ROM-walking and EWRAM-running program, ROM and EWRAM through the DDR2 model with 293-693 ns, give IWRAM/VRAM/palette dumps bit for bit equal to the run with an ideal memory; the held fraction of the clocks is reported (target below 15 %, the number is the result). Control: cache bypass serving a stale line -> a dump byte differs | `cores/gba/sim/mem_ref/run.sh` | tier 2 |
| S6 | P5 | The machine's clock closes: WNS >= 0 at the clock of S3, or the documented enables with multicycle constraints if the first fails; the constraint file names each exception. Control: one path that really is longer than its enable allows must make WNS negative | `cores/gba/core_timing.tcl` + Vivado report | CI `bitstream` |
| S7 | P7 | A test program draws mode 0 (text), mode 1/2 (affine), mode 3/4/5 bitmaps, sprites with affine, windows, alpha; the bench hashes the frame buffer words against the golden hash of the first run checked on the board by eye. Control: red/blue swapped -> hash differs | `cores/gba/sim/video/run.sh`, board step B1 | tier 2, board |
| S8 | P8 | A program plays direct sound from a DMA FIFO at a known rate and each PSG channel in turn; the audio samples show the expected frequencies per channel and silence for the others. Control: all channels disabled -> the bench demands silence AND a non-silent run before, so a dead capture fails | `cores/gba/sim/sound/run.sh` | tier 2 |
| S9 | P1b, P6 | Our test program calls the BIOS functions (Div, DivArm, Sqrt, ArcTan2, CpuSet, CpuFastSet, LZ77/Huffman/RLUnComp, BitUnPack, ObjAffineSet, BgAffineSet, Halt, VBlankIntrWait) and compares the results with values computed in the bench's script, not taken from any BIOS. Control: a result changed in the expectation -> FAIL | `cores/gba/sim/bios/run.sh` | tier 2 |
| S10 | P9 | The application loads a ROM into DDR2, finds the save type by string, creates the save on change and reloads it; keypad bits for ten buttons from the C64U's joystick and keyboard. Control: save file removed -> RAM empty and the test says so; two keys swapped -> FAIL | app tests (`ci/apptests.sh`) + `cores/gba/sim/pad/run.sh` | app tests |
| S11 | P6, P7, P8, P9 | Alex's commercial cartridges (one with SRAM, one with Flash, one with EEPROM) play: picture, sound, buttons, a save survives a restart. Hand step, with a photo | board step B2 | board session |

## §3 Decision

Nothing is decided with Alex. **Proposed, coordinator:**

**T1 Gate first:** S2 before any glue. If it reports more than 14,500 LUT for the clean machine, T183 stops and goes to §4 as "does not fit" by
Alex's decision; nothing else is built. If it reports 11,000-14,500, go on with the cuts of P2 1.-2. If it reports less, go.
**T2 Large core, flat, host unchanged,** `cores/gba/mono/` on the pattern of `cores/snes/mono` (T142 D1); lane `large-gba`; `delivery = "cfw"`.
**T3 Memories as P3:** IWRAM, VRAM, BIOS, palette, OAM, line buffers in block RAM; ROM (to 32 MiB), EWRAM (256 KiB) and the saves in DDR2 behind write-through
line caches that take the machine's enable away on a miss; ROM at the core base `0x01000000` (32 MiB), EWRAM at +0x2000000, saves above, the frame buffers at a 1 MiB stride.
**T4 Source base:** the upstream `rtl/` pristine by `tools/vendor_gba.sh`, converted by `tools/gba_gen.py` (GHDL to Verilog, every clock an enable) into a tracked `gen/`, as T142.
Not copied: `gba_savestates.vhd`, `gba_statemanager.vhd`, `reg_savestates.vhd`, `gba_cheats.vhd`, `gba_gpu_colorshade.vhd`, the `_hd` paths (generics/ports tied off),
`sdram.sv`, `ddram.sv`, `pll*`, `GBA.sv`.
**T5 BIOS:** the MIT replacement BIOS built from source, if Alex allows it (Q2) and S9 passes; otherwise the user's original `boot.rom` from `<core folder>/roms/`, no embedded BIOS.
**T6 Picture, sound, input, saves** as T142 D7 and P9: `gba_fb.v` after `nes_pixw.v`, `gba_audio.v`, `gba_pad.v`, `coreapp/cores/gba.cc` after `snes.cc`.
**T7 Verification:** only our own test programs (S4, S5, S7-S9), assembled by a script; board steps by hand with Alex's cartridges.

## §3a Open questions for Alex (one per line, to be asked one at a time, in the order they block)

1. **Q1** Is the synth-only trial of the clean machine (S2: a day on the builder, no core code) the go/no-go gate, and does a result above 14,500 LUT stop the core?
2. **Q2** BIOS: the MIT replacement BIOS (accuracy a non-goal, some sound calls missing) embedded and the user's original optional — or only the user's original?
3. **Q3** Which extras stay out for good so that the fit is measured without them: 2x resolution, shaders, flicker blend, savestates, rewind, cheats, solar sensor/gyro/tilt/rumble?
4. **Q4** The RTC of the Pokemon-class cartridges (GPIO RTC): in v1, or later?
5. **Q5** If the machine is held on DDR2 misses, are frame rate and pitch falling by up to 15 % in the worst games acceptable in v1?
6. **Q6** Key map for the ten buttons on the C64U's joystick and keyboard (A on fire, the rest on keys)?

## §4 Out

Decided with Alex later; the candidates are the questions above.

## §5 Decisions taken while building

(empty)

---
