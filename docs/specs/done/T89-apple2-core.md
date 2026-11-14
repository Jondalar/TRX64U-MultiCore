# T89 — An Apple IIe core, ported from NanoApple2, runs as a core in the T79 host/core split

**Status:** DECIDED (2026-10-13): all 10 open questions answered by Alex; not built.
**Status check 2026-11-13:** DONE (Alex 2026-10-30), no longer "not built": the Apple IIe .cor runs on the board, Wasteland boots (2026-10-20), `cores/apple2/core.toml` experimental = false; rest open to contributors.
**Repo:** TRX64-Ultimate (gateware under a new `designs/apple2/` and `cores/apple2/`, app side, tools). The sources are read from
outside (MiSTle-Dev/NanoApple2, MiSTer-devel/Apple-II_MiSTer); nothing is copied into this repo by this spec.
**Number:** T89 (registry: the TODO table in `TODO.md`).
**Milestone:** M2.
**Origin:** Alex, 2026-10-13: an Apple II core as another core behind the T79 host, "the Nano cores are already there"
(NanoApple2, `docs/other-cores.md`, candidates picked 2026-10-01); "spec only, no gateware code"; Apple II NTSC artifact
colours on HDMI are wanted.
**Depends on:** T79 (host/core split, contract `T79.1-host-core-contract.md`), T81/T84 (core selector, `.cor` bundle), T83
(first non-C64 core: VC-20, the model for the swap, ROM window, Freeze and app), T85 (Amiga spec: the host pixel layer as the
video path), T87 D15/D16 (the locked host is routed with the slim C64; every other core must route against it).

Template: `docs/specs/_TEMPLATE.md`. Citations of the contract (`C1`-`C9`) are by section of `T79.1-host-core-contract.md`
(dfx-trial worktree); sizes and line counts of NanoApple2 and Apple-II_MiSTer are from the `main` of both on 2026-10-13, read
in a scratch clone outside this repo.

---

## §1 The problem

**Who:** Alex, who wants the C64 Ultimate board to be a host for more than one machine (T79.1: "Layer 2 (a core: TRX64, later
VC20, Minimig, ...)").

**What he wants:** pick "Apple IIe" in the core selector, the board swaps layer 2 by partial reconfiguration, and an Apple IIe
boots to the `]` prompt or from a floppy image on the SD card, with the C64U keyboard, a joystick, sound, a 5.25" drive and the
famous NTSC artifact colours (HGR green/violet/orange/blue from a monochrome bit stream) on the HDMI picture.

**Today:** there is no Apple II anything. The swap works with TRX64 (T79.1 PoC 4/5) and a VC-20 runs behind it (T83); the third
core (T85, Amiga) is specified. The edge is still TRX64's: 228 ports, 2,180 bits, a VIC-II shaped video port, a DDR2 port pair,
the 8-bit register bus. An Apple II shares one thing with the C64: both are 6502 machines on a 14.31818 MHz crystal
(C64 NTSC CPU clock 1,022,727 Hz, Apple 14,318,181 Hz / 14 = 1,022,727 Hz, T40-ntsc.md in TRX64-Ultimate/docs/specs table), so the host's NTSC
clock mode (sys_clk 32.7273 MHz, T40) is an exact 16/7 of the Apple master clock.

**What the sources are.**

- **NanoApple2** (Till Harbaum's MiSTle family, Stefan Voss's port, Gowin FPGAs, VHDL + Verilog). It is an **Apple IIe** (the
  README title: "Nano Apple IIe"; `apple2.vhd` header "Top level of an Apple //e", Szombathelyi György, from Stephen Edwards'
  Apple ][+), 64K main + 64K aux RAM, 80 columns, double hires, language card, 6502 (T65) or 65C02 (`R65Cx2`), Disk II x2
  from raw `.nib` images over an SD sector protocol, a ProDOS HDD (`.hdv`), Mockingboard A, Super Serial Card, AppleMouse
  card, scanlines, colour/green/amber/B&W, four colour palettes. It needs a microcontroller (FPGA Companion) for USB, OSD, SD.
  Its RAM lives in **SDRAM** (`sdram.v`), its ROMs in Gowin pROM IP **with the Apple ROM contents inside**
  (`roms/27128.mi` 16,384 bytes, `roms/video8k.mi` 8,192 bytes, `roms/key.mi`; these are Apple's code and are not touched here).
  Colour: `vga_controller.vhd` looks at the last six bits of the 1-bit video stream at 14.318 MHz and picks one of **16 palette
  colours** per 14 MHz pixel (Linards Ticmanis / Newsdee colour theory), 564 x 2 columns per line.
- **Apple-II_MiSTer** (the same family one step upstream, Verilog, newer: files dated 2026). The same machine logic, plus: a
  **real NTSC composite decoder** (`rtl/video/composite_decoder.sv` 783 lines, MIT, comb filter, hue/saturation presets), a
  **WOZ flux-level disk controller** (`rtl/woz/`, about 4,200 lines, GPL-3), an own 65C02/NMOS core (Jamie Blanks, tested
  against SingleStepTests), save states, no-slot clock, Saturn 128K RAM card, joy2key. RAM through the MiSTer SDRAM/DDRAM.
- Neither has resource figures in the repo (no report, no README line). Neither has been built or simulated by us.

**Why it matters:** the Apple IIe is the first core whose video is a **1-bit stream that is turned into colour by the display
chain**; it tests the contract's video cut (C4) with the one picture the VIC-II path cannot carry (§3 D5), and it is a
cheap core (a 6502, one RAM, simple I/O) whose cost is dominated by block RAM, which is the scarce resource of `pb_l2`
(§3 D4, D12).

**Break the problem into parts, MECE** (§2 is checked against this list):

- **P1 Source and licence.** What is used, under which terms, what is blocked.
- **P2 Porting surface.** What is kept, what the host replaces, what Gowin primitives must go.
- **P3 Clocks and reset** (C1).
- **P4 Memory and ROMs** (C3 and the app's ROM path).
- **P5 Video** (C4): text/lores/hires/double hires, artifact colours, frame rate, geometry.
- **P6 Audio** (C5): the speaker click.
- **P7 Keyboard and game port** (C6): keys, Open/Closed Apple, joystick, paddles.
- **P8 Media:** Disk II floppies, ProDOS hard disk.
- **P9 Slot cards and RAM expansion:** language card, 80-column/aux card, Mockingboard, mouse card, SSC, Saturn/RamWorks.
- **P10 Connector pins** (C7): cartridge slot, IEC, user-facing lines.
- **P11 Registers and identity** (C2, C9).
- **P12 App side, swap and Freeze** (T81/T84, C8).
- **P13 Size and timing** against `pb_l2` and the locked host.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 the sources are clean and no Apple code is in the repo (P1)
  Given the files chosen in §3 D1 and the licence table
  When  gateware derived from them is about to be committed under cores/apple2/
  Then  every file's header terms are recorded in cores/apple2/NOTICE and none is "unclear" or "no copying" (open files are not merged)
  And   a CI check finds no Apple ROM bytes in the repo, the CI artefacts or the .cor (the reset vector of the 16 KB system ROM,
        the 256-byte Disk II boot ROM and the character ROM are matched by length+hash and by scanning the tree)

Scenario: S2 the core elaborates on Xilinx and boots in simulation (P2)
  Given the kept machine logic, Gowin pROM/SDPB/PLL/IP removed, RAM and ROM inferred, ROM contents loaded by the bench from local files
  When  the design is synthesised out of context for xc7a50t and simulated
  Then  Vivado reports no black box and no Gowin cell
  And   the bench reaches the Applesoft "]" prompt and the "APPLE ][" (IIe: "Apple //e") banner and PEEK/POKE round trips main and aux RAM
  And   the TRX64 build is unchanged (same LUT, timing met)

Scenario: S3 the machine runs on exact Apple clocks (P3)
  Given the NTSC entry of the identity block (sys_clk 32.7257 MHz, the host's NTSC mode)
  When  the app programs the host clock and releases core_rst
  Then  the 14M enable is 7 of 16 sys_clk (long-term 14.31751 MHz, -48 ppm = the host's own error), PHI0 is 1,022,727 Hz class,
        a line is 912 14M clocks (65 CPU cycles, the last one long), a frame 262 lines = 238,944 14M clocks = 59.92 Hz
  And   the same counts are measured on the board with the ring counter and the bench agrees to the clock

Scenario: S4 ROMs come from files, not from the bitstream (P4)
  Given a layer 2 with no ROM contents baked in, /roms/apple2/ with apple2e.rom (16,384 B), apple2e-video.rom (8,192 B), diskii.rom (256 B)
  When  the app writes them through the ROM window and resets the core
  Then  the screen shows the Apple //e banner and the "]" prompt, "PR#6" with no disk shows the drive spinning
  And   a missing or wrongly sized file leaves the core in reset and the overlay names the file and the expected size (no silent boot)
  And   the reset vector at offset $3FFC of apple2e.rom is checked by the app (it must read $FA62) before the file is accepted

Scenario: S5 the picture is straight, colour correct and does not tear (P5)
  Given the core's text, lores (GR), hires (HGR), double hires (DHGR) and 80-column modes
  When  a bench pattern (HGR colour bars; alternating bit patterns 1010..., 11011011...) and the Apple "COLOR TEST" lores page are shown
  Then  the HGR bars show the artifact colours in the right order (violet, green, blue, orange, white at the right byte parity) and
        the lores page shows the 16 colours (checked against the palette table in §3 D5, per pixel, from a captured frame)
  And   text is 40/80 columns of crisp cells, flash and inverse work, no tearing at the frame boundary, the overlay opens over it
  And   a frame is repeated or dropped at most once per 10.3 s (the computed beat of §3 D5), never twice in a row
  And   the TRX64 picture after the swap back is pixel-identical to before the first swap

Scenario: S6 the speaker is heard at the right pitch (P6)
  Given PRINT CHR$(7), and a POKE loop toggling $C030
  When  the core delivers samples to the host mixer
  Then  the bell is a 1 kHz burst of about 0.1 s measured from the captured stream (frequency from the zero crossings, within 0.1 %)
  And   no TRX64 SID volume or filter setting changes it

Scenario: S7 keyboard works (P7)
  Given the C64U's keyboard matrix delivered by the host
  When  Alex types a BASIC line with SHIFT, CTRL, ESC, arrows, DELETE, RETURN and the Apple punctuation set, and holds a key
  Then  every key of the §3 D7 table produces its ASCII byte at $C000 (bit 7 set), $C010 clears it, AKD ($C010 bit 7) follows,
        auto-repeat starts after the real delay and rate, CTRL-RESET (the mapped RESTORE) performs a warm reset
  And   Open Apple / Closed Apple read at $C061/$C062 bit 7 from the mapped keys

Scenario: S8 game port: joystick, buttons, paddles (P7)
  Given a digital joystick in port 1 (and 2)
  When  a BASIC program prints PDL(0), PDL(1) and PEEK(-16287) while the stick is moved and fire pressed
  Then  PDL reads 0 left/up, about 127 centred, 255 right/down; PB0 is port 1's fire (and Open Apple), PB1 is port 2's (and Closed Apple)
  And   the PDL timing is the real 558 one-shot: PDL(n) returns after the real number of cycles for the value

Scenario: S9 boot and write a floppy (P8)
  Given a DOS 3.3 system master as .dsk, .do, .po or .nib on the SD card
  When  the app mounts it in drive 1, the user types PR#6, runs CATALOG, then SAVE HELLO and LOAD HELLO after a remount
  Then  the disk boots, the catalogue lists, the saved program is in the image file on the card after unmount (write-back)
  And   a second drive works (mount in drive 2, "CATALOG,D2"), and the drive LED/overlay shows the active drive
  And   no image bytes are checked in (third-party media, run by hand)

Scenario: S10 hard disk (P8, if Open question 6 says yes)
  Given a 32 MB .hdv ProDOS volume on the card and the HDD slot card
  When  the core cold-starts
  Then  ProDOS boots from the volume (slot 7) and reads/writes blocks through the app

Scenario: S11 128K, 80 columns, double hires, language card (P9)
  Given an Applesoft program that POKEs aux RAM through the soft switches, PR#3, an 80-column text page, a DHGR test screen, and
        a language-card test that bank-switches $D000-$FFFF RAM
  When  they run
  Then  they behave as on a real IIe (bytes written to aux read back through the aux switches; main is unchanged)

Scenario: S12 Mockingboard and mouse (P9, if Open questions 7 and 8 say yes)
  Given the Mockingboard A in slot 4, an AppleMouse card in slot 5
  When  a Mockingboard demo plays a tone on each AY chip, the app injects mouse movement and buttons
  Then  both AY chips are audible at the right pitch (frequency from the stream) and the mouse card reports the movement

Scenario: S13 the port is idle and no C64 device exists (P10)
  Given the Apple core running
  When  the expansion port pins are probed and a C64 cartridge is plugged
  Then  the core drives nothing on the slot or the IEC bus, the cartridge changes nothing the Apple shows
  And   nothing in the core answers CIA-, SID- or VIC-style accesses

Scenario: S14 windows answer, identity reads APL2 (P11)
  Given the Apple core and the Ultimate app
  When  the app reads the identity block and touches every window it knows
  Then  identity reads "APL2" with the contract version and the capabilities (pixel layer video, mono audio, two joysticks, NTSC clock entry)
  And   windows the core does not have read as absent (0, request echoed); no read stalls the RISC-V (the NO_SAMPLER lesson)

Scenario: S15 swap C64 -> Apple -> C64 and the app's guard (P12)
  Given the C64 core running with a program on screen
  When  the core selector picks Apple IIe, then later TRX64 again
  Then  Alex's photos show the C64 screen, the Apple //e "]" prompt, the C64 screen again; REST answers throughout
  And   the app offers no SID/REU/UCI/monitor/cartridge-writer action while the Apple core is loaded

Scenario: S16 Freeze stops the machine exactly (P12)
  Given the Apple core running a program with disk activity and sound
  When  Alex chooses Freeze and later leaves the menu
  Then  the machine is stopped between two whole CPU cycles (disk head position, speaker, timers held), the last picture stays,
        sound is 0, the menu shows full screen; leaving the menu resumes exactly where it stopped
  And   a bench with two identical machines, one paused 64 ms at an arbitrary sys_clk, ends bit-identical (the VC-20 `tb_vc20_pause` pattern)
  And   a reset while frozen still works

Scenario: S17 the core fits pb_l2 and closes timing against the locked host (P13)
  Given pb_l2 (18,000 LUT, 50 RAMB36, 80 DSP) and the host routed with the slim C64 (T87 D15/D16)
  When  the Apple partial bitstream is routed against that locked host
  Then  WNS >= 0, the report lists the measured LUT/FF/BRAM/DSP (which replace the estimate in §3 D12), and the host is not rebuilt for the Apple
```

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 | Clean sources, no Apple code in repo | review gate (NOTICE before first commit) + `tools/check_no_roms.sh` (new) in the CI `lint` stage | review, CI |
| S2 | P2 | Elaborates on Xilinx, boots in sim | `designs/apple2/sim/boot_prompt` (new, GHDL on the VHDL; ROMs supplied locally, not checked in) → `ci/sims.txt` row | CI `sims` |
| S3 | P3 | Exact clocks | `sim/clk_counts` (line/frame/PHI0 counts); board: ring counter by hand | CI `sims`, board |
| S4 | P4 | ROMs from the app | `sim/boot_prompt` with the ROM window model; board by REST/overlay | CI `sims`, board |
| S5 | P5 | Picture, artifact colours, frame beat | `sim/video_frames` (frame model of the pixel writer, palette per pixel) ; board photos | CI `sims`, board |
| S6 | P6 | Speaker pitch | `sim/audio_tone` (frequency from samples) | CI `sims` |
| S7 | P7 | Keyboard | `sim/keys` (every table entry, repeat timing); board by hand | CI `sims`, board |
| S8 | P7 | Game port | `sim/gameport` (PDL cycle counts, buttons); board by hand | CI `sims`, board |
| S9 | P8 | Floppy boot + write-back | board with a DOS 3.3 image (third-party, by hand); app side by REST script | board |
| S10 | P8 | Hard disk | board with an .hdv (by hand) | board |
| S11 | P9 | 128K / 80 col / DHR / language card | `sim/aux_lc` (bench program in 6502 code written by us, no Apple ROM calls) ; board by hand | CI `sims`, board |
| S12 | P9 | Mockingboard, mouse | `sim/ay_tone` (frequency); board by hand | CI `sims`, board |
| S13 | P10 | Idle pins | `sim/pins_idle`; board probe | CI `sims`, board |
| S14 | P11 | Windows, identity | `sim/windows` + app run on the board | CI `sims`, board |
| S15 | P12 | Swap and back, app guard | board by hand (Alex's photos); guard by REST script | board |
| S16 | P12 | Freeze | `sim/apple2/tb_a2_pause` (two machines) ; board by hand | CI `sims`, board |
| S17 | P13 | Fit and timing | Gitea CI `pipeline.yml` build stages and reports | CI build |

Third-party ROMs (Apple system, character, Disk II boot ROMs), DOS 3.3/ProDOS images and test programs are supplied locally and
never checked in. The bench programs of S11 are written by us in 6502 code (no ROM call), so CI does not need an Apple ROM.

## §3 Decision

Facts first, then D1-D15. Each D names its part. **Items marked (Q n) wait for Alex's answer to Open question n**; the rest is
decided by the standing decisions of T83/T85 (not re-asked) or by what the code says.

**Standing decisions that apply (T83/T85, repeated for the reader, not re-asked):** TREX-internal only (private Gitea,
never in a release or an update package, build behind its own switch `TRX_CORE=apple2`, off by default, `ci/package.sh` strips
everything named `apple2`); ROMs and OS images never in the repo, CI or `.cor` (the app loads them from `/roms/apple2`
on USB, SD or flash); a plain machine, no TRX64 ring/monitor/snapshots, **but Freeze works like the C64** (T83 Q6, changed
2026-10-13); the application is one app per core on Gideon's Ultimate base (`#ifdef`, `.cor` bundle); keyboard from the C64
matrix, joysticks from the C64 ports; media from SD/USB served by the app; swap is a cold boot.

**What NanoApple2 is, from its files** (facts the decisions rest on):

- The machine is `apple2.vhd` (475 lines) + `timing_generator.vhd` (193) + `video_generator.vhd` (119) + a `R65C02` and a
  `T65` core selected by a `cpu` pin + soft-switch/MMU logic inside `apple2.vhd` (ALTZP, RAMRD/RAMWRT, STORE80, PAGE2,
  language-card `HRAM_*`). It runs on **one 14.31818 MHz clock** (`CLK_14M`) with `CLK_2M`, `PHASE_ZERO` and the RAS/CAS/Q3
  phases derived from it (`PALMODE` selects 312 lines instead of 262, `timing_generator.vhd` V_RESET).
- RAM interface: a 16-bit word per address, **low byte main RAM, high byte aux RAM** (`ram_do(15 downto 0)`, `aux`,
  `ram_we`, `ram_addr` 16 bit); the address is the CPU's in PHASE_ZERO and the video scanner's otherwise (`apple2.vhd` lines
  157-168). In the Nano an SDRAM controller serves this in the 2 MHz cycle (`sdram.v`, `clkref = CLK_2M`). This is the
  **only** thing that has to be replaced under the machine: a 64K x 16 memory.
- Video: `video_generator.vhd` emits the 1-bit `VIDEO` stream at 14.318 MHz from the character ROM (`video_rom_addr`
  12 bit + `ROMSWITCH`), `HBL`, `VBL`, `COLOR_LINE`, `TEXT_MODE`; `vga_controller.vhd` turns it into RGB (D5).
- Floppy: `disk_ii.vhd` (252 lines) + `drive_ii.vhd` (180) serve a **pre-nibblized track of 0x1A00 = 6,656 bytes** per drive
  (35 tracks = 232,960 bytes `.nib`); `floppy_track.sv` (159 lines) moves tracks between a track buffer and the SD card by
  sector LBA (`lba <= track * 13`, 13 x 512 = 6,656), including write-back of a dirty track.
- HDD: `hdd.vhd` (206 lines) is a ProDOS block device on the AppleWin register interface, CPU halted during a command
  (`CPU_WAIT`).
- Reset: a cold reset writes 0 to RAM $03F4 (the power-up byte) so that the monitor goes to the disk boot routine.

**Licence (read from the files, not from READMEs).** Both repositories are described as GPL-3.0 (NanoApple2 has a GPLv3
`LICENSE`; Apple-II_MiSTer has none in the tree; its `Apple-II.sv` says GPL v2 or later (Sorgelig), `apple2_top.v` GPL-3 (W. Soltys)). Per file:

| File(s) | Terms as written in the file header | Use |
|---|---|---|
| NanoApple2 `apple2.vhd`, `timing_generator.vhd`, `video_generator.vhd`, `disk_ii.vhd`, `drive_ii.vhd`, `keyboard.vhd`, `vga_controller.vhd` | credit lines only (Szombathelyi György, based on Stephen A. Edwards, Columbia University `apple2fpga`); **no licence text in the file** | machine logic; **terms not in the file** (see below) |
| `floppy_track.sv` | Sorgelig 2016, "Lesser GNU General Public License ... version 3" | to be replaced (D8) |
| `R65Cx2.vhd` | table-driven 65Cx2 by A. Daly, derivative of Peter Wendrich's FPGA64 (2005-2008); no terms | not used if T65 passes (D2) |
| `T65/*.vhd` | change log only (as T83 D1); no terms | used (already vendored for the VC-20) |
| `hdd.vhd` | Steven A. Wilson, "based on the AppleWin interface"; no terms in the header | HDD (Q6) |
| `ssc.vhd`, `mouse/applemouse.vhd` | (c)2025 Gyorgy Szombathelyi; no terms; `jt6805` GPL-3 (Jose Tejada), `pia6821.vhd` "adheres to the GNU public license" (John E. Kent) | Q8 only |
| `mockingboard/YM2149.sv` | MikeJ 2005 + Sorgelig 2016, BSD-style, **no "Replay only" clause** | Q7 only |
| `mockingboard/jt49/*` | GPL-3 (Jose Tejada) | alternative AY for Q7 |
| `mockingboard/via6522.vhd` | Gideon's Logic Architectures: "**do not copy (use in your own projects) without written permission of the author**" | **not used**: Apple-II_MiSTer's `via6522.v` (BSD-style, Thomas Skibo 2011) instead |
| `misc/scandoubler.v` | GPL-3 (Till Harbaum); `hdmi/*`, `video.v`, `sysctrl`, `hid`, `sd_*`, `ws2812`, `dualshock2`, `osd_*`, `mcu_spi`, flash/SDRAM | board parts, dropped (host does all of it) |
| `src/tang/*/nanoapple2.vhd` (the tops) | GPL-3 header (W. Soltys 2014, Stefan Voss 2025) | read as the wiring diagram only; our top is new |
| `roms/*.mi`, `src/gowin_prom/*`, `mouse/applemouse_*rom*`, `ssc_rom.vhd`, `disk_ii_rom.vhd`, `hdd_rom.vhd` | **Apple (and card) ROM contents** | **never touched** (D4) |
| Apple-II_MiSTer `rtl/video/composite_decoder.sv`, `apple_composite.sv`, `vga_controller.v`, `video_pipeline.sv` | SPDX **MIT**, Jamie Blanks / Newsdee 2026 | donor for Q2 |
| Apple-II_MiSTer `rtl/woz/woz_floppy_controller.sv`, `flux_drive.v` | **GPL-3 or later**, Alan Steremberg 2026 | donor for Q5 |
| Apple-II_MiSTer `rtl/cpu/*` | "Copyright (c) 2026 Jamie Blanks", **no terms** | not used |
| Apple-II_MiSTer `rtl/no_slot_clock.sv` | BSD-2 (jtflanagan 2023) | not wanted |

**What this means for our tree (GPL, TREX-internal anyway):** the files with MIT, BSD, GPL-3 and BSD-style headers are usable.
The machine logic itself (the Edwards-derived files) has **no licence text in the file**; the repositories declare GPL-3.0 as a
whole, which covers Szombathelyi's and Voss's own additions, and Edwards' original page on the Columbia site is not read here.
Because the core never leaves TREX (standing decision), the working position is "GPL-3.0 as declared by the repositories,
credit lines kept in every file, `NOTICE` lists every file with its header terms"; if the core is ever released the Edwards
terms must be clarified first (Out). There is **no "Replay-only" clause** in anything read here (unlike the VC-20 sources).

- **D1 Source: NanoApple2 as the base, Apple-II_MiSTer as the donor for parts (Alex: "the Nano cores are already there").**
  Read-only study; the port is our own `cores/apple2/` (upstream files with headers intact + NOTICE) and `designs/apple2/`
  (our wiring). *Why the Nano as base:* it is the one Alex named; it is VHDL like the VC-20 and Amiga ports; its RAM interface
  is a plain 16-bit word (above); its disk side is a track buffer (nibble stream) that the app can fill. *What the MiSTer core
  adds and where we look at it:* a real composite decoder (Q2), WOZ (Q5), a tested CPU (not used: no terms), save states (not
  wanted: no snapshots). *What lost:* using the MiSTer core as the base: Verilog conversion of the same machine, more
  optional logic (save states, OSD overlay, joy2key, floppy sound) that has to be stripped, a flux disk engine of 3,500+ lines
  as the only disk path, and it brings its own SDRAM/DDRAM glue (`Apple-II.sv`). The machine logic is the same lineage; the
  Nano is the smaller starting point.
- **D2 Porting surface (P2).** *Kept as machine logic:* `apple2.vhd`, `timing_generator.vhd`, `video_generator.vhd`,
  `disk_ii.vhd`, `drive_ii.vhd`, one CPU. *CPU: one core, not two.* The Nano carries both `T65` (6502) and `R65C02`
  (selected by `cpu`) and so pays for two CPUs. The enhanced IIe is a 65C02 machine; we keep **T65 in Mode "01" (65C02)**,
  already vendored for the VC-20, and run the 65C02 extended-opcode functional test (Klaus Dormann, supplied locally,
  not checked in) in simulation (S2); if T65's 65C02 mode fails it, `R65Cx2` (terms unclear) is the fallback and the terms
  are clarified before it is merged. A 6502-only (II+/unenhanced) CPU switch is Q1. *Replaced by host services:* HDMI, OSD,
  scandoubler, SD card and loader, MCU/HID/`sysctrl`, LED, PLLs, DualShock. *Replaced by the contract:* ROM loading (D4),
  media (D8), keyboard (D7). *Gowin-specific, must be replaced for Artix-7:* `Gowin_PLL_*`/`rPLL`/`pll_init` (clocks from the
  host, D3); `gowin_prom_apple2`, `gowin_prom_key`, `gowin_prom_uc`, `gowin_sdpb_8k*`, `gowin_sdpb_palette*`,
  `gowin_dpb_*`, `gowin_sp_128b` (IP wrappers with `.ipc`; become inferred block RAM `(* ram_style = "block" *)`, ROMs
  initialised empty and filled by the app); `sdram.v` / `sdram_tn20k.sv` (replaced by block RAM, D4). The first elaboration
  (S2) must confirm this list; files not read in detail (`hid.v`, `sysctrl.v`) are dropped with the board part.
- **D3 Clocks and reset (P3, C1).** Per C1 the core asks for its frequencies in the identity block and the app programs the
  host clock before releasing `core_rst`. The Apple's whole machine is counted in 14.318 MHz **enables**, not in a clock of
  its own: video, disk, CPU phases and the speaker are all counted in 14M enables (the machine has no time-based logic
  except the speaker's audio window, D6). So the core runs on `sys_clk` with an NCO enable. Identity asks for the **host's
  NTSC entry (sys_clk 32.7257 MHz, T40: `mmcm_sys` NTSC setting, -48 ppm)**, in which the 14M enable is **7 of 16 sys_clk
  exactly** (14.3175 MHz, -48 ppm, the host's own error and not ours); the same NCO form (add 7, fire at 16) as the VC-20's
  `vc20_nco`. `PHASE_ZERO`/`CLK_2M` come out of the machine's own timing generator as in the Nano. No second MMCM, no host
  change. The second core clock (`sid2x`) is unused. `PALMODE` stays tied to NTSC (Q10). Reset: `core_rst` stretched by the
  core; a cold reset writes the power-up byte as the Nano does; **warm reset** (CTRL-RESET) and **cold reset** (menu) are two
  different requests (D7, D13).
- **D4 Memory and ROMs (P4, C3).** RAM in **block RAM inside `pb_l2`**: one **64K x 16 true dual-port RAM** (low byte main,
  high byte aux, exactly what `ram_do` expects) = 128 KB = **32 RAMB36** (derived: 4 KB usable per tile at x9). Port A is the
  machine (CPU and video address as today, one access per 14M enable; the registered read data is available one clock
  after the address, which fits inside the 2 MHz cycle that the SDRAM needed: to be proven in S2 against the Nano's
  `clkref` timing), port B is the host window (the app reads and writes RAM through the DMA/ROM window, `dma_*`). ROMs
  are block RAMs filled by the app: system ROM 16 KB (4 tiles, derived), character ROM 8 KB (2 tiles; two 4 KB sets, the
  `ROMSWITCH` pin selects), Disk II boot ROM 256 B and card ROMs (1 tile together). **No ROM bytes in the bitstream**
  (the partial carries no Apple code and is shippable inside TREX). The DDR2 ports are used by the base core **only for the
  video frame buffers (D5)**; RAM does not use them (the Apple's 1 MHz bus does not need DDR2 and a 700 ns read round trip
  would eat the cycle, T85 D4 numbers). *Plan B, specified now, built only if S17 says the block RAM does not route:* the 64 KB
  aux RAM moves to DDR2 behind the real-time port with a per-line video prefetch (saves 16 tiles; costs a line buffer and
  makes the CPU's aux accesses latency-bound, ~0.4-0.7 us against a 0.98 us cycle, from the numbers T85 collected; not built, not
  measured). Whether to start with plan A or plan B is Open question 4. RAM expansion beyond 128 KB (Saturn/RamWorks) would
  not fit in block RAM at all and goes to DDR2 (Q9).
  **ROM files (the app loads them, names and sizes):** in `/roms/apple2/` on USB, SD or flash:
  `apple2e.rom` **16,384 bytes** (the Enhanced Apple IIe system ROM image for $C000-$FFFF as the Nano/MiSTer cores load it; the
  app checks that the bytes at offset $3FFC/$3FFD read $62 $FA, the monitor's RESET vector $FA62), `apple2e-video.rom`
  **8,192 bytes** (the IIe character ROM, two 4 KB sets), `diskii.rom` **256 bytes** (the 16-sector Disk II boot ROM, P5A),
  optional `hdd.rom` 256 bytes (Q6), `mouse.rom` and `mouse-mcu.rom` (Q8), optional `palette.pal` (D5 custom palette). Where the
  user's dump has a different layout the app refuses the file (S4) rather than guessing.
- **D5 Video (P5, C4). The obvious path (the VIC-II frame) is the wrong one here.**
  *Why not the scaler's index path (what T83 D5 did):* it stores a 400 x 270 window of a 4-bit index per VIC dot (122 ns at
  NTSC, 8.18 MHz; 127 ns at PAL) with a 16-entry app-written palette. The Apple's pixel is 69.8 ns (14M) or 139.6 ns (hires dot). 40
  columns are 560 14M pixels = 39.1 us = 320 scaler dots; a hires dot would fall on 1.1 scaler dots, giving unequal pixel
  widths of 1 and 2 dots in text and a shimmering edge (computed, not shown). The colour fringes of the artifact method need
  the 14M resolution, which a 122 ns grid cannot carry. The index path would give a recognisable but visibly wrong picture.
  *The pixel layer path (decided, as T85 D5 for the Amiga):* the host's pixel layer reads an **RGB565 frame buffer from DDR2**
  (`pix_layer.v` header: source W x H with W <= 960, uniform scale S = 1..3, opaque, below the freeze menu text, double buffer
  with a page flip at the frame start, registers on trx_io page 6). The core writes the frame; the app sets the layer. No host
  change. *Format:* one source row per Apple video line, written **twice** (line doubling is physical because the layer scales
  uniformly): **560 x 384** (560 14M columns, 192 lines x 2), S = 2 -> 1120 x 768 on the 1920 x 1080 frame, centred. This is
  **9 % wider than 4:3** (1120/768 = 1.458; the real monitor shows 560 x 192 on 4:3, i.e. 0.457 pixel aspect, which a doubled-line
  uniform scale cannot reproduce exactly); an exact 4:3 needs the core to resample 560 -> 512 columns in the line buffer
  (blurs the artifact fringes). Which one is Open question 3. A scanline option (second row of each pair at 50 %) is a few LUT.
  *Colour, stage 1 (decided): the digital artifact decode of the Nano* (`vga_controller.vhd`): the last six VIDEO bits ->
  one of **16 palette colours per 14M pixel**, four selectable 16-colour palettes (original, IIgs, //e, custom `palette.pal`),
  colour/green/amber/B&W monitors; the palette is a 16 x 24-bit table in LUTs/one tile instead of Gowin SDPB. The 16 colours are
  written as RGB565 (widened by the host with bit repetition; <= 3 levels off the 8-bit palette, estimate). Cost: small.
  *Colour, stage 2 (Open question 2):* the **real NTSC composite decoder** of the MiSTer core (MIT, 783 lines, 4 samples per
  subcarrier, notch + optional comb, hue/saturation/contrast, presets) on the 14M sample stream, producing 8-bit RGB. It shows
  the true fringing and the hue knob; it is a DSP/line-store user (resource estimate in D12). Both stages produce the same
  14M pixel stream; stage 2 replaces the palette lookup, so stage 1 is not thrown away.
  *Frame rate and the scaler lock:* the Apple NTSC frame is 262 x 912 = 238,944 14M clocks = 59.92 Hz (S3); the host runs
  the NTSC mode at 65 x 263 lines (59.826 Hz, T40), so the layer shows 59.826 Hz frames of a 59.92 Hz source: **one frame is
  repeated or dropped every 10.3 s** (computed: 14,318,181.8 / 238,944 = 59.9227 Hz against 59.826 Hz, 0.0968 Hz = 0.16 %). The core still emits a dummy VIC-II
  shaped raster (65 x 263, colour 0, a few LUT, as T85 D5) so that the host scaler locks and the HDMI output runs; the app
  sets the layer. The host must be in its NTSC System Mode while the Apple core is loaded (the identity entry selects it, D3).
  *If the host cannot run NTSC 1080p60 for a non-C64 core* (not verified here; T40 says it does for the C64), the layer is shown
  at 50 Hz and one in six frames is dropped; S5 measures this first.
  *The dummy raster, the beat and the buffers:* DDR2 footprint = 2 x 560 x 384 x 2 B = **860 KB** (derived) inside the
  16 MB window at byte 0x1000000 that T85 D4 names (the REU/expansion block, unused by this core); traffic = 560 x 384 x 2 B x 59.9
  Hz = 25.8 MB/s written by the core (32-bit writes, C3: 107,520 per frame, 6.4 M/s) and the same read by the layer (computed).
  The layer's priority and line-fetch behaviour as a game-speed video source has never been measured (T85 said the same).
  *Not on the HDMI:* analogue composite out (gap G11 of the contract, host work).
- **D6 Audio (P6, C5).** The Apple's only sound is the 1-bit speaker toggle at $C030 (`speaker`). The core averages the
  speaker level over fixed `sys_clk` windows (682 clocks at NTSC = 47.99 kHz, computed; the T83 form of 656 at 31.5278 MHz),
  scales to signed 24 bit and delivers `audio_l = audio_r` with `audio_sample_en` (C5); the host resamples and mixes. A
  first-order low-pass at about 12 kHz takes the edge off (our own, not a copy). No SID: `sid_*` ignored. Mockingboard output
  (Q7) is added to the same sample. Exactly 0 while frozen (D14).
- **D7 Keyboard and game port (P7, C6).** *Keyboard:* the host delivers `keys[63:0]`, the physical C64U matrix. The
  IIe keyboard encoder is replaced by a **table in the core: (matrix position, shift state) -> ASCII byte** for the C64 keycap
  layout (symbolic, like VICE's symbolic mapping: the key that prints `"` on the C64 gives `"`, not the Apple key at that
  place), written from the Apple //e key set and the C64U matrix (not copied; the Nano's `keyboard.vhd` converts USB
  scan codes, a different thing). The table has 128 entries and replaces the Apple keyboard ROM (no Apple ROM needed for keys).
  Keys the C64 does not have: **ESC** from RUN/STOP, **TAB** from CTRL-I (the real IIe generates TAB as CTRL-I), **DELETE** from
  INST/DEL, the four arrows from the two CRSR keys (+SHIFT), **CAPS LOCK** from SHIFT LOCK; **CTRL** from the C64 CTRL key
  (clears bits 6:5 of a letter as the encoder does); **Open Apple** from C= (also port-1 fire), **Closed Apple** from a second
  key chosen with Alex at the table review (S7); the keys the Apple has and the C64 lacks (brackets, backslash, backtick,
  tilde, vertical bar, braces, underscore) get C64 key combinations listed in the table and printed on a keymap sheet in the
  app. **RESTORE = RESET**: alone it is CTRL-RESET (warm reset, the effective reset of a IIe); with C= it is the self-test
  combination (Open Apple + CTRL + RESET); the cold start is a menu entry. Auto-repeat after about 0.5 s at about 10 Hz
  in the core (the IIe repeats in the encoder), counted in 14M enables so it freezes with the machine. *Game port:* the core
  implements the real 558 one-shot timers: `PDL_strobe` ($C070) starts all four, `GAMEPORT` bits 7 return while the timer
  runs; the timer length is `value x 11 cycles` as on the machine. Joystick port 1 -> paddles 0/1 (X/Y) + PB0, port 2 -> paddles
  2/3 + PB1; the digital stick gives **0 (left/up), 127 (centred), 255 (right/down)** (a digital stick cannot be analogue: T47
  paddles are "won't do for now", so no injected analog values; the register for them can be added by T47's own decision).
  PB0/PB1 are wired-OR with Open/Closed Apple exactly as the real machine (they share the pin). The app can swap the ports.
- **D8 Media (P8).** *Floppies:* the core keeps `disk_ii` and `drive_ii` (the nibble track engine, two drives) and the
  track buffers (2 x 6,656 B in block RAM, 2 x 2 tiles, derived). `floppy_track.sv` and the SD protocol are **dropped**; in their
  place a **track window**: when the machine selects a track the core raises "track needed" (drive, track, write-pending) in a
  register and an IRQ line (`irq[3:0]`, C2); the app (which already reads files from SD/USB) fills the buffer through the
  register window (bulk through the window, the contract's rule), clears the flag, and takes a dirty track back
  when the head moves or the motor stops (the Nano's `dirty`/`active` logic is kept as the trigger). The app does the format work in
  software, which the Nano cannot: **.nib** (232,960 B) is copied; **.dsk/.do** (143,360 B, DOS order) and **.po** (ProDOS order)
  are **nibblized/de-nibblized by the app on the RISC-V** (the standard 6-and-2 encoding with the DOS 3.3 or ProDOS sector
  skew) so that all three work, which is more than the Nano (.nib only). The app owns the files: write-back updates the
  image on SD/USB; a changed image is flushed on unmount, reset and Freeze exit. *WOZ* (bit-exact, copy-protected disks, flux
  timing) needs the MiSTer flux-level controller (GPL-3, +3,500 lines, own track storage of up to ~13 KB per track): Q5.
  *Hard disk:* `hdd.vhd` is a ProDOS block device on slot 7 that stalls the CPU during a command; the block transfer goes
  through a 512-byte buffer served by the app (block number and buffer address in registers, IRQ, app copies 512 B between
  the volume file and the buffer): Q6 for in or out. `.hdv` is a raw ProDOS volume (32 MB maximum in the Nano); `.2mg` is
  accepted by the app by skipping the 64-byte header (the app, not the core).
- **D9 Slot cards and expansion (P9).** *In every build:* language card (the `HRAM_*` bank logic is part of `apple2.vhd`), the
  80-column/aux-RAM "slot 3" behaviour of the IIe, double hires, the soft switches. *Per Open question:* Mockingboard A in slot 4
  (Q7: two AY-3-8913 = `YM2149.sv` BSD-style, two 6522 = the MiSTer `via6522.v`, BSD-style), AppleMouse card in slot 5 (Q8: `applemouse.vhd` + 6805 MCU
  `jt6805` + PIA + two ROMs; the mouse data comes from the app's USB mouse as for the Amiga), Saturn/RamWorks RAM above 128 KB
  (Q9). *Out (§4):* Super Serial Card (no serial pins, no modem), printer card, Z80 card, any real card.
- **D10 Connector pins (P10, C7).** Real Apple II cards **cannot** work on the C64U's cartridge port: different bus
  (the Apple slot is a 50-pin edge with its own signal set, no common pinout with the C64's 44 contacts), different
  voltages for DMA/INH and no way to give a card its select lines (`DEVICE SELECT`, `IO SELECT`, `IO STROBE` per slot) from
  24 outputs. Decision (as T83 D9): all `SLOT_*` outputs idle (`oe` off, transceiver direction/enable disabled), nothing is
  sampled from the slot, IEC and user-facing lines idle, no C64 device exists in the core. The slots' behaviour is
  **inside** the core (D9). Not checked: the board's XDC for spare FPGA pins.
- **D11 Registers and identity (P11, C2/C9).** One window of the app's I/O space (`cw_*`) as for the VC-20/Amiga plus the
  windows the app already knows. The Apple core answers: **identity** (`core_id` "APL2", contract version, capabilities: pixel
  layer video, mono audio, two joysticks, NTSC clock entry; build hash); **ROM/RAM window** (D4: RAM at the Apple address,
  ROMs at their offsets); **config register** (6502/65C02 if Q1 says yes, monitor type, palette select, language card,
  scanlines, ROMSWITCH, reset (warm/cold) request); **track window and IRQ** (D8); **HDD registers** (Q6); **frame-buffer
  base and flip request** (D5); **keyboard injection** (the app can type a string, as T83 D15's run-PRG did). **Everything else
  answers as an absent device:** reads return 0, the request toggle is echoed (a stub that never answers stalled the RISC-V
  for ever, T79.1). Interrupts: `irq[0]` track needed, `irq[1]` HDD command, `irq[2]` frame done.
- **D12 Size and timing (P13).** **Estimate, no measurement exists** and no figure is published by either source. Bounds
  from the sources: NanoApple2 has a build for the Tang Nano 20k (GW2AR-18, 20,736 LUT4; whether every feature is enabled there was not
  checked), so the machine with its slot cards and the HDMI/OSD/SD board parts fits in 20,736 Gowin LUT4: an **upper
  bound**, not an estimate (it contains board parts we do not take and it carries two CPUs). The estimate below is from component size
  (NanoApple2 VHDL line counts and the T83/T85 as-built experience that glue is 1,500-3,000 LUT; **assumptions** marked):

  | Part | LUT estimate | BRAM (RAMB36) | DSP |
  |---|---|---|---|
  | CPU (T65 mode 65C02) | 500-900 (**assumption**; T83 estimated 400-500 for the 6502 mode) | 0 | 0 |
  | Apple IIe logic (timing, video generator, MMU, soft switches, language card) | 800-1,400 (**assumption**) | 0 | 0 |
  | Disk II + two drives + track window | 400-800 | 4 (track buffers) + 1 (boot ROM and card ROMs) | 0 |
  | Keyboard table, game port timers, speaker/audio | 300-600 | 0 (LUT ROM) | 0 |
  | Video: digital palette decode + pixel writer + DDR2 client + line buffer + dummy raster | 500-900 | 1 | 0 |
  | Registers, identity, NCO, freeze hold, IRQ | 300-500 | 0 | 0 |
  | **Base (RAM, ROMs, floppies, keyboard, joystick, audio, video stage 1)** | **about 2,800-5,100 LUT** | RAM 32 + system ROM 4 + char ROM 2 + track buffers 4 + boot/card ROMs 1 + line buffer 1 = **44** | 0 |
  | + HDD (Q6) | +300-500 | +1 (sector buffer) | 0 |
  | + Mockingboard (Q7): 2 x AY, 2 x VIA, mixer | +1,000-1,800 (**assumption**) | 0 | 0 |
  | + AppleMouse (Q8): 6805 MCU, PIA, glue | +700-1,200 (**assumption**) | +2 (MCU ROM, card ROM) | 0 |
  | + composite decoder (Q2 stage 2): notch, comb line store, demodulator, YIQ->RGB matrix | +1,500-3,500 (**assumption**) | +2 (line store 2^10 samples) | 10-20 (**assumption**, signed multipliers in 24-bit samples) |
  | **Everything of the above** | **about 6,300-12,100 LUT** | **49** | up to 20 |

  Against `pb_l2`: 18,000 LUT (the LUT range is 35-67 %), 50 RAMB36, 80 DSP. **The binding resource is block RAM, not LUT:**
  the base is 44 of 50 tiles (86-88 %), and with HDD, mouse and decoder 49 (98 %). The measured references are the
  TRX64 slim C64 at 13,643 LUT, 4,300 slices (96 %), 39 BRAM (T87 D15/D16) which is the host's routing partner, and the
  old full-core experiment (88.6 % LUT, 94 % BRAM) that **did not route** (T79.1 §7 runs 708/720). Alex's rule that a
  core should not be denser than that C64 (13,643 LUT / 39 BRAM) is met on LUT by every variant, **not on BRAM** by any
  variant, because 128 KB of RAM alone is 32 tiles. The levers, in order: (1) plan B (aux RAM into DDR2, -16 tiles, D4); (2) HDD
  buffer and mouse/char ROM into LUT-RAM; (3) track buffers served as 512 B sectors from DDR2 instead of two 6,656 B buffers
  (-4 tiles; the app already holds the track); (4) drop the decoder line store (no comb: -2); (5) no 80-column/aux (not an
  option: it is the IIe). Timing: the 14M enable on a 32.7 MHz clock leaves 2.3 clocks per 14M slot, so a path that was one
  14.3 MHz cycle in the Nano is 2-3 `sys_clk` here; the machine must be written with enables (no multicycle constraints
  needed) and the CPU runs 1-in-14: low timing risk; the edge, not the logic, is the risk (S17). Host change: **none**
  (video uses the existing pixel layer; clocks use the existing NTSC mode).
- **D13 App side and swap (P12).** The application is the VC-20's kind (T83 D11a): an Ultimate-app image on Gideon's base
  (`build_apple2.sh`, `TRX_CORE=apple2`), packed with the partial into an `apple2.cor`, which initialises its own core and shows
  only what exists for this machine. It must: (1) set the NTSC System Mode and the layer (position, scale 2, base, key colour
  off, alpha 255); (2) read `/roms/apple2/*`, check name, size and the $FA62 vector, write them through the ROM window, report
  what is missing in the overlay; (3) serve the track window (nibblize/de-nibblize, write-back; D8) and the HDD blocks (Q6);
  (4) offer an overlay menu: reset warm/cold, insert/eject disk 1/2 (browse SD/USB for .dsk/.do/.po/.nib), HDD volume (Q6), monitor
  type (colour/B&W/green/amber), palette (original/IIgs/IIe/custom), scanlines, character ROM US/UK, 6502/65C02 (Q1), joystick
  port swap, keymap sheet; (5) save the configuration (the app's config store); (6) deliver mouse deltas (Q8). It must **not**
  offer or run: monitor, SID/sampler/UCI/REU/turbo, the cartridge menu and writer, C64 readmem/writemem, any peek of C64 I/O.
  The gate is the identity block (T83 D11), not a compile-time list. The cartridge-writer, REST and menu code for the C64
  stays untouched in the `#ifdef`.
- **D14 Freeze (P12, C8). Stop the whole machine at a bus-cycle boundary.** Choosing Freeze (the app's `C64_STOP`, the JTAG pause,
  the host's halts reach `halt_req`, as T83) stops the 14M enable of **the machine**: the enable is held low from the
  next PHI0 boundary, so CPU, timing generator, video generator, the disk's rotation counter, the speaker window and the
  keyboard repeat counter all stop in the same clock; resuming releases the enable at the next 14M edge, the cycle that
  was interrupted is not started. Because the frame buffers are **double buffered in DDR2**, the layer keeps showing the last
  complete frame; the pixel writer stops mid-frame and the unfinished back buffer is simply never shown (it resumes into
  the same buffer and the flip happens at the next frame start). The **dummy raster is not stopped** (it belongs to the host's
  scaler lock; a stopped scaler loses its lock, which cost the VC-20 a bug on 2026-10-13: T83 §5). Sound exactly 0 while held
  (the speaker window forced to the centre); a reset takes the hold away. This is more exact than the VC-20 (where the 6561
  had to keep running) because the Apple's video is regenerated from RAM on the machine's own phases and carries no state
  the CPU depends on beyond what the enable already holds. The 2-machine bench (S16) must show bit-identical end states.
- **D15 What follows from earlier decisions (no new decision):** one clock family from the host (T79.1 C1); video is
  the core's, composited by the host (T79.1 C4 within the existing pixel layer); peripherals are the host's, the core gets the
  matrix and the joysticks (T79.1 2a); the register bus is 8 bits and carries control only, bulk data through the memory
  ports or windows (T79.1:48); a swap is a cold boot, nothing survives (decoupler); TREX-internal (T83 D13); the core is a
  plain machine plus Freeze (T83 Q6); `irq[3:0]` is the event path.

**Open questions for Alex (one line each, not decided here):**

1. Q1: Which model is the target: the **Enhanced Apple IIe** (65C02, 128 KB, 80 columns, double hires; both sources are IIe cores) only, or also an Apple II+ / unenhanced 6502 mode (a CPU switch and a different ROM, same hardware)?
   **Answered (Alex, 2026-10-13):** "a": Enhanced Apple IIe only; no II+ / unenhanced 6502 mode.
2. Q2: Colour, stage 2: build the **real NTSC composite decoder** (MiSTer, MIT; true fringes, hue/saturation knobs; +1,500-3,500 LUT, +2 BRAM, 10-20 DSP estimated) in this spec, or only the Nano's digital 16-colour artifact decode (stage 1, always built)?
   **Answered (Alex, 2026-10-13):** "a": only the Nano's digital 16-colour artifact decode (stage 1); no NTSC composite decoder. Reference title: Wasteland (standard hires).
3. Q3: Pixel aspect: write 560 x 384 at uniform 2x (exact pixels, picture **9 % wider than 4:3**) or resample 560 -> 512 columns for an exact 4:3 (blurs the fringes)?
   **Answered (Alex, 2026-10-13):** "a": 560 x 384 at uniform 2x, exact pixels; the picture 9 % wider than 4:3 is accepted.
4. Q4: Block RAM: the base core needs 44 of 50 tiles (88 %) against the slim-C64 reference of 39; **accept and measure routing (plan A)**, or start with the aux RAM in DDR2 (plan B, -16 tiles, latency-bound, not built or measured)?
   **Answered (Alex, 2026-10-13):** "b": plan B from the start: the 64 KB aux RAM in DDR2 (-16 block RAM tiles), its latency to be handled and measured.
5. Q5: Floppy formats: the app converts .dsk/.do/.po/.nib (nibble engine, no copy protection); is **WOZ** (bit-exact flux controller, GPL-3, +3,500 lines, own track storage) in scope?
   **Answered (Alex, 2026-10-13):** "b, not a for now, put it in the spec as phase 2": WOZ (bit-exact flux controller) is not in the first build; it is planned as phase 2 (§4).
6. Q6: Is the **ProDOS hard disk** (.hdv/.2mg, slot 7, the app serves blocks) in scope?
   **Answered (Alex, 2026-10-13):** "b": the ProDOS hard disk (.hdv/.2mg, slot 7) is phase 2 (§4).
7. Q7: Is the **Mockingboard A** (two AY-3-8913 + two 6522, slot 4) in scope?
   **Answered (Alex, 2026-10-13):** "b": the Mockingboard A (slot 4) is phase 2 (§4).
8. Q8: Is the **AppleMouse card** (slot 5, USB mouse from the app, 6805 MCU + ROMs) in scope?
   **Answered (Alex, 2026-10-13):** "a": the AppleMouse card (slot 5, USB mouse from the app) is in the first build.
9. Q9: **RAM beyond 128 KB** (Saturn 128K / RamWorks, would live in DDR2): in, later or never?
   **Answered (Alex, 2026-10-13):** "b": RAM beyond 128 KB (Saturn/RamWorks in DDR2) is phase 2 (§4).
10. Q10: **PAL** (50 Hz, 312 lines, European IIe) after NTSC: a later step, or never? (NTSC first is the host's 60 Hz mode and the artifact-colour demand.)
   **Answered (Alex, 2026-10-13):** "a": PAL (50 Hz, 312 lines, European IIe) comes as a later phase after NTSC (§4).

---
<!--
Closing: Status → DONE (date) + which scenarios are green where; TODO row ✅ and history entry,
in the same commit as the code.
-->
## §4 Out

- **Phase 2 (Alex, Q5):** WOZ images through the bit-exact flux controller (MiSTer, GPL-3), with its own track storage;
  planned after the first build, not dropped. **Now in [T89.1](../T89.1-apple2-disk-hdd.md)** (Alex, 2026-10-29).
- **Phase 2 (Alex, Q6):** the ProDOS hard disk (.hdv/.2mg, slot 7, the app serves blocks). **Now in [T89.1](../T89.1-apple2-disk-hdd.md)** (Alex, 2026-10-29).
- **Phase 2 (Alex, Q7):** the Mockingboard A (two AY-3-8913 + two 6522, slot 4).
- **Phase 2 (Alex, Q9):** RAM beyond 128 KB (Saturn 128K / RamWorks, in DDR2).
- **Later phase (Alex, Q10):** PAL (50 Hz, 312 lines, European IIe) after NTSC.

- **Real Apple II cards on the cartridge port** (D10) and any slot hardware that needs pins: SSC / modem, printer, Z80 card.
- **Analogue paddles and analogue joysticks** (T47 is "won't do for now"): digital stick gives 0/127/255 only.
- **Composite / S-video / RGB analogue out** (contract gap G11, host work).
- **Save states, the TRX64 ring, monitor, trace, `.c64re` snapshots, UCI, SID, REU, turbo, sampler, cartridge writer** for the
  Apple: C64 features. Freeze is in.
- **No-slot clock, floppy sound, drive-status overlay, joy2key, on-screen keyboard** of the MiSTer core: not wanted (the app's
  overlay can show drive state later).
- **Apple IIgs, Apple III, Laser 128, Franklin, Pravetz** clones and their ROMs.
- **Releasing the core** (public build, update package): the Edwards-derived machine files carry no licence text; the
  terms must be clarified before any release (S1 keeps them in NOTICE).
- **Gowin primitives kept in any form**, the Tang boards' MCU/HID/OSD: dropped.
- **Copying any NanoApple2 or Apple-II_MiSTer file into this repo** before the S1 gate (NOTICE) exists, and **any Apple ROM
  content** at any time.

## §5 Decisions taken while building

- **2026-10-19, the .cor with the generic application (Alex).** Alex: "So let's build a .cor please. Including the app. Use the
  standard LVGL app for it. Please!!" (translated). The Apple //e therefore runs the T104 generic application (`coreapp/`,
  descriptor `coreapp/cores/apple2.cc`, core.toml `app = "blueprint"`), not an Ultimate-app image on Gideon's base: this
  supersedes D13's application kind. ROMs come from the stick (`roms/apple2/`: `apple2e.rom`, `apple2e-video.rom`, `diskii.rom`),
  never from the repository or the .cor. Core order 10, `experimental = true`.
- **2026-10-19, the descriptor (`coreapp/cores/apple2.cc`).** ROMs from `roms/apple2/`: `apple2e.rom` (16 KB, reset vector $FA62
  checked), `apple2e-video.rom` (8 KB), `diskii.rom` (256 B, optional). The slot-6 ROM is in the core's array only while a disk is mounted:
  the boot bench reaches `]` because that array is empty, and with the ROM present and no disk the monitor spins in the Disk II boot for
  ever (the board photo of 2026-10-19; real hardware does the same). "Mount D1: and cold start" ends in a cold start, "Mount D1:" swaps without one (Alex 2026-10-20, T108 D6), D2: only mounts. Settings: Display
  (colour, B&W, green, amber), Palette, Scanlines, Character ROM set, Joystick ports (one CFG register), CAPS LOCK (On, Off, Follow SHIFT LOCK).
  **Default On**: with the C64U's SHIFT LOCK up the letters would come out lower case, which Applesoft rejects, and a USB keyboard has no
  SHIFT LOCK at all; "Follow SHIFT LOCK" is the decision of the entry above, the default is mine and open for Alex. The application sets
  the keyboard board's `lock_off` in every mode (`CoreIo::bling_lock_off`, host/bling_io.v +2). Picture 560 x 384 at scale 2, window
  (400, 156). Tests: `tests/coreapp/test_apple2.cc` (43 checks, eight controls `a2_*`), menu golden `menu_apple2.txt`.
- **2026-10-19, a game to try, so floppy (Alex).** Alex: "I wanted at least one game to try it out, Wasteland or so"
  (translated). P8 / S9 is built with the application: .dsk/.do/.po/.nib from the stick are mounted from the file browser into
  Disk II drive 1 (drive 2 when cheap), booted, written back on unmount; the application nibblizes (6-and-2, DOS 3.3 or ProDOS sector
  order) and serves the track window.

- **2026-10-19, the track window as built (S9, library `coreapp/cores/apple2_disk.*`, `apple2_nib.*`).** Measured from the code:
  `drive_ii.vhd` walks a ring of 0x1A00 = 6,656 bytes (TRACK_ADDR wraps at 0x19FF), one nibble per 32 us, 35 tracks; `a2_trk.vhd`
  opens a request (STATUS b0, b1 = take the track out, REQ_TRACK the track) and closes it on a write to DRV+3; the buffer is the
  aperture with WIN_SEL 3 / 4 at offset 0. The application writes 16 physical sectors of 416 bytes (address field D5 AA 96 + volume,
  track, sector, checksum in 4-and-4 + DE AA EB, 6 x FF, data field D5 AA AD + 342 6-and-2 nibbles + checksum + DE AA EB, 47 x FF),
  DOS 3.3 order for .dsk/.do, ProDOS for .po, a .nib track as it is. The save side scans the ring circularly (a guest rewrite can
  sit across the wrap), needs all 16 sectors with good address and data checksums for the track asked, else the track is dropped
  with a notice (use .nib for copy-protected disks); the request is acknowledged in every case. After an acknowledge the service waits
  until the status shows the request closed (the status lags through two synchronisers; a second ack would close the next request).
  Tests `tests/coreapp/test_apple2_disk.cc` (golden `golden/apple2_nib.txt` from `gen_apple2_golden.py`, an independent POSTNIB-style
  decoder, all 35 tracks both orders, service against a model of the window, write protect, eject flush) with eight negative controls
  `a2d_*` in `run.sh`.
- **2026-10-19, files without licence text (Alex).** The R65Cx2 CPU (`cores/apple2/rtl`, branch `t89-core`) names its
  author but carries no licence terms. Alex, 2026-10-19: "We use them, as all MiSTer and daughter projects do too" (translated): used TREX-internal like the other third-party cores, NOTICE kept.
- **2026-10-19, board run without application: the sim does not reproduce the picture or the reset (fork A).** The board showed the
  banner, then the whole text screen filled with one inverse glyph, and neither CTRL-RESET nor `PRINT 1+1` through KEY_INJ gave `]` / `2`.
  Measured in GHDL with the same real ROMs and the kit's own register/window replay (boardkit `kit.stim`, `tb_kit_b`), the aux RAM in the
  DDR2 model (`AUX_DDR=1`, C_LAT 40), cold reset 20,000 and 4,194,304 clocks: (a) cold start: banner on text row 1, rest blank, the Disk II
  ROM of slot 6 spins, no `]` - as on a real //e without a disk; (b) the frame buffer (read back and rendered): "Apple //e" in white on
  black, nothing else, a pre-filled (0xA5) buffer is overwritten whole (every pixel is written), so no stale-DDR fill; (c) one CTRL-RESET pulse of
  200 ms: `]` on the bottom row, the cursor, `PRINT 1+1` typed through KEY_INJ gives `2` on the text page AND in the frame buffer. Without
  `diskii.rom` in array 2 (`tb_a2_boot`, `A2_DISK` unset) the machine goes straight to `]` (slot 6 has no card signature). So the logic
  and the picture path are right in simulation with the board's memory arrangement; what differs on the board is physical (the kit did not program
  the host's NTSC clock, no application ran the pixel layer, DDR2 calibration/latency, what the host's pixel layer shows below black). Only
  the board can say which; the application therefore reads and logs STATUS, FRAMES twice, CPUADDR, AUX_MISS/AUX_STALL, FB_OVERRUNS after the
  start. The sequence the application uses (registers of `coreapp/cores/apple2_regs.h`): start = CTRL 1, C64_MODE reset, the ROMs through
  the aperture with WIN_SEL 0/1 (and 2 only when a disk image is mounted, 256 zero bytes otherwise so slot 6 is empty and the machine ends
  at `]`), CFG, CAPS, CTRL 0, C64_MODE unreset (cold reset 0.29 s runs inside the core); warm reset = CTRL 1, at least 20 ms, CTRL 0; cold
  reset while running = CTRL b2; typing = KEY_INJ with a 7 bit code (CR = 0x0D) while KEY_QUEUED < 16, one code every 20 ms or more.
  Regression (local, ROMs outside the repository): `A2_ROMS=<dir> A2_DISK=1 A2_DDR=1 sh cores/apple2/sim/run_boot.sh` puts `diskii.rom` in slot 6, sees the
  banner without `]`, presses CTRL-RESET for 200 ms and then runs the POKE/PEEK checks: green on the builder (aux RAM in the DDR2 model). Its
  control `NORESET` (core ignores CTRL-RESET, must fail) is in the script but was not run to the end (the run was stopped).
- **2026-10-19, CAPS LOCK default = follow SHIFT LOCK (Alex: "the C64 has it exactly like that ... press once = on, LED
  lights, press again off ... what is the problem now? We had that this morning", translated).** The descriptor's default
  was "On"; it is "Follow SHIFT LOCK" now, "On" and "Off" stay as options.
- **2026-10-19, CAPS LOCK is a register (built).** `a2_regs.vhd` 0x0A2 CAPS (W b0, 1 at power-up) drives the key glue's CAPS
  LOCK; SHIFT + LEFT-ARROW is the underscore again (the toggle is gone, the C64's key had no other use). The application passes
  the keyboard's SHIFT LOCK (`bling_io` 0x10100802 b1, with `lock_off` = b0 set) to it. Bench `apple2_keys` (GHDL, builder): power-up 1,
  0 gives lower case and STATUS b3 follows, 1 capitals again; controls `CAPSREG` (register ignored) and `CAPSLOW` fail as they must.
- **2026-10-19, the Apple's extra keys (Alex).** Open Apple = C=, Closed Apple = HOME (the branch's choice, not
  objected to). CAPS LOCK = the C64U's SHIFT LOCK (Alex: "CAPS = SHIFT LOCK?", translated): the C64U keyboard is
  digital, `host/bling_keyboard.v` toggles the lock itself (code 0x41), lights the 71st LED and holds left SHIFT in
  the matrix unless `lock_off`. So the Apple's application sets `lock_off` (SHIFT LOCK holds no SHIFT) and passes the
  `locked` state to the core as CAPS LOCK, which only capitalises letters, as on the //e; the LED still shows it. The
  branch's SHIFT + cursor left for CAPS goes.
