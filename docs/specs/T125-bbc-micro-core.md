# T125 — A BBC Micro runs as its own core: MODE 7 teletext, MODE 0-6, keyboard with BREAK, a disk from the stick, sound

**Status:** PROPOSED (2026-11-05, Alex).
**Repo:** TRX64U-MultiCore — a new core `cores/bbc/` (gateware, glue in `trx/`), its descriptor in `coreapp/cores/bbc.cc`; nothing in the host.
**Number:** T125 (registry: the table in `TODO.md`).
**Origin:** Alex, 2026-10-24 (translated): "please add BBC and PET to the list of cores". 2026-11-05 (translated): "Prepare the specs, so we can build them bit by bit."
**Depends on:** T119 (the pixel layer's DDR2 frame buffer writer), T104 (the generic core application), T164 (DFX budget); the decisions in §3a.
**Milestone:** M3.

---

## §1 The problem

Who: Alex and the testers at the board. What: run a BBC Micro (Acorn, 1981; 6502 at 2 MHz, 32 KB RAM, MOS + BBC BASIC in ROM) on the
C64U as a core of the multi-core host, with the user's own ROMs, a disk image from the stick, keyboard from the C64U's keys and the
picture through the host's pixel layer. Today: nothing is built; the core sits on the list (`docs/other-cores.md`, `docs/todo/T125.md`),
no source is chosen and no licence is read. The research for this spec (2026-11-05, clones read, nothing executed):

- **MiSTer-devel/BBCMicro_MiSTer** (Sorgelig's port of BeebFpga): Models B and Master 128K, 6502 or 65C02 per model (`R65Cx2.vhd`, or `T65`),
  2x 6522 (`via6522.vhd`), 6845 (`mc6845.vhd`), video ULA (`vidproc.vhd`), SAA5050 teletext (`saa5050.vhd`), SN76489 (`sn76489_audio.vhd`),
  uPD7002 ADC, 1770 (`fdc1772/`, Verilog), CMOS RTC, optional Tube co-processor, optional mouse. Disks only as `BEEB.MMB` (a container of
  many `.ssd` images) on the SD card, plus the 1770 for the Master. Shipped with ROM binaries in `roms/` (MOS, BASIC, DFS, ADFS, ...).
- **hoglet67/BeebFpga** (David Banks, from Mike Stirling's work): the same machine core in `src/common` (`bbc_micro_core.vhd`, generics for NuLA,
  SID, Music 5000, Tube, ICE debugger), top levels for Xilinx, Altera and Gowin (Tang 9K/20K); disk through an SPI SD card plus a ROM
  filesystem (MMFS); **no 8271 and no 1770 in `src/`** (the MiSTer port added the 1770).
- **Licences, read from file headers** (no LICENSE file in either repository): the machine core and the video/CRTC/teletext/keyboard files
  (Stirling: `bbc_micro_core`, `mc6845`, `vidproc`, `saa5050`, `keyboard`) are BSD-style **with "License is granted for non-commercial use
  only. A fee may not be charged for redistributions ... without specific prior written agreement from the author"**; `via6522.vhd` of the
  MiSTer port is (C) Gideon's Logic Architectures (the BeebFpga tree uses MikeJ's `m6522.vhd`, BSD-style, instead); `fdc1772` is GPL v3
  (Harbaum); the Tube files are LGPL v3; `T65` is BSD-style; `R65Cx2.vhd` is A. Daly's derivative of fpga64 (Wendrich) with only a copyright
  line; the BeebFpga `sn76489.vhd` has no header, the MiSTer one is BSD 3-clause (Hagerty).
- **ROMs** (MOS 1.20 / 3.20, BBC BASIC 2/4, DFS, ADFS) are Acorn's, now held by a successor (secondary source: Pace Micro Technology; a forum
  post says the holder tolerates use with emulators). No open replacement for the MOS is known (a web search, 2026-11-05, found only an
  annotated disassembly, not a redistributable image). Both repositories ship the binaries; **we would not**.

Break into parts (MECE):

- **P1** Source and licence: which code, under which terms, in which tree.
- **P2** The machine: 6502/65C02, memory map (RAM, 16 sideways slots, MOS), clocks and the 1 MHz / 2 MHz cycle stretching, VIAs, ADC, user/printer port.
- **P3** Picture: 6845 + video ULA modes 0-6 (RGB, 8 colours + flash), teletext MODE 7, timing, and the way into the host's pixel layer.
- **P4** Sound: SN76489 into the host's audio path.
- **P5** Input: keyboard matrix with BREAK, joystick/fire through the ADC and the user VIA.
- **P6** Disk: how an `.ssd`/`.dsd` image reaches the machine and how writes return to the stick; tape.
- **P7** ROMs: names, the user's set, the check, never in the repository.
- **P8** Area, timing and DFX/large decision.
- **P9** Application and descriptor: settings (model), media slots, F-key menus, board test.

## §2 Scenarios — what proves it is solved

Benches are proposed names; all run on the builder (`tools/benches.sh`) except where a board step is named. A bench that needs the real
ROMs reads them from a path given in the environment and is SKIPPED (not green) without; the CI benches use small assembled test ROMs.
The ROMs of the user are used by hand on the board only.

```gherkin
Scenario: S1 - the chosen source builds and its licence is on file
  Given the files vendored into cores/bbc/rtl
  When  the NOTICE is compared with the file headers
  Then  every vendored file is listed with its header's terms and none is GPL-incompatible with the repository's private, TREX-internal use
  And   a file whose header carries no terms (sn76489 of BeebFpga) fails the check until a licence is on file
```
```gherkin
Scenario: S2 - the CPU runs the machine's memory map
  Given a test ROM in the MOS slot that writes a pattern to RAM $0000-$7FFF, pages sideways slot n through $FE30 and reads back
  When  the bench runs 2 M cycles
  Then  RAM is 32 KB (Model B), the paged slot differs per n, writes to ROM slots do not stick, and the 1 MHz peripherals stretch the cycle (cycle count of 100 accesses to $FE40 equals the measured count of the source)
  And   the control (test ROM expecting 64 KB RAM) fails
```
```gherkin
Scenario: S3 - MODE 7 shows teletext
  Given the real MOS/BASIC (board) or a test ROM that fills $7C00-$7FE7 with teletext codes (bench)
  When  the machine runs two frames in MODE 7
  Then  the frame buffer holds 40x25 characters at the SAA5050 geometry: a double-height line, graphics and a flashing character match the reference dump byte for byte
  And   the control with the teletext ULA bit cleared produces the non-teletext picture and differs from the reference
```
```gherkin
Scenario: S4 - MODES 0-6 draw the right picture
  Given a test ROM that programs 6845 and video ULA registers for each of MODE 0, 1, 2, 4, 5 and a palette change mid-frame
  When  one frame per mode is rendered into the frame buffer model
  Then  the geometry (640x256, 320x256, 160x256, ...), the colours and the mid-frame palette change match the reference frames pixel for pixel
  And   a frame of another mode is rejected by the compare (control)
```
```gherkin
Scenario: S5 - the pixel layer shows it
  Given the core with the T119 writer, a stand-in DDR2 model
  When  50 frames run
  Then  the writer delivers 50 frames at 50 Hz with the line length the app declares, nothing is written outside the core's frame buffer window
```
```gherkin
Scenario: S6 - keyboard and BREAK
  Given the matrix interface of the core (64 keys) and the app's key map
  When  the bench presses SHIFT+A, then releases; then BREAK
  Then  the system VIA reads the matrix code of SHIFT and A at $FE4F scan, CA2 raises the interrupt; BREAK pulses the reset only (RAM stays, MODE is kept as the MOS does, a power-on reset clears it)
  And   BREAK is not visible in the matrix (control: a bench that reads the matrix for BREAK fails)
```
```gherkin
Scenario: S7 - sound
  Given a test ROM that programs tone channel 1 at a known divider and the noise channel
  When  1 s of audio is captured
  Then  the dominant frequency equals 4 MHz / (32 * n) within 1 Hz, noise has a flat spectrum, silence is exactly zero with all volumes at 15
  And   a divider off by one fails (control)
```
```gherkin
Scenario: S8 - a disk loads from the stick
  Given an .ssd image on the stick, mounted in drive 0 by the app, the user's DFS (and MOS) ROMs
  When  the user types *CAT, then CHAIN"..." or SHIFT+BREAK
  Then  the catalogue lists the image's files and the program runs (board step with a photo; the bench: a sector-level model reads sector 0 and 1 of a synthetic image through the controller and the bytes equal the image's)
  And   a write to the disk reaches the image file after the stick sync; a write-protected mount refuses the write and the file is unchanged (sha256 before/after)
```
```gherkin
Scenario: S9 - ROM set is checked, nothing third-party in the tree
  Given a roms folder without os12.rom
  When  the core starts
  Then  the app says which ROM is missing and the machine does not start with an empty MOS
  And   `git ls-files cores/bbc` contains no .rom/.ssd/.dsd (lint check)
```
```gherkin
Scenario: S10 - area and timing
  Given the core routed under the host
  When  the core stage runs
  Then  LUT <= 78 % of pb_l2 (13,600 LUT, the highest that ever routed), RAMB36 <= 40, WNS >= 0, 0 routing errors
  And   a deliberately oversized variant (Master 128 RAM in BRAM) is reported over budget (control: the check can fail)
```
```gherkin
Scenario: S11 - joystick / ADC and the user port
  Given a test ROM that reads the uPD7002 channel 0/1 and the fire button via the system VIA
  When  the joystick is at full left, centre, full right
  Then  the ADC returns values near $FFxx / $80xx / $00xx and fire reads as the VIA bit; the control with the joystick unplugged reads centre
```
```gherkin
Scenario: S12 - the whole on the board
  Given the .cor in the package, the user's ROM set, a disk
  When  Alex boots it
  Then  the BBC banner "BBC Computer 32K" and "BASIC" appear in MODE 7, a MODE 0 program draws, a game loads from disk and makes sound (board session, photos)
```

| # | Solves | Test | Where it runs |
|---|---|---|---|
| S1 | P1 | NOTICE check script `tools/check_notice.sh` (to be written) | lint |
| S2 | P2, P7 | `cores/bbc/sim/cpu/run.sh` | tier 1 |
| S3 | P3 | `cores/bbc/sim/teletext/run.sh`, board S12 | tier 1/2 |
| S4 | P3 | `cores/bbc/sim/video/run.sh` | tier 2 |
| S5 | P3 | `cores/bbc/sim/pixw/run.sh` | tier 1 |
| S6 | P5 | `cores/bbc/sim/keys/run.sh` | tier 1 |
| S7 | P4 | `cores/bbc/sim/sound/run.sh` | tier 1 |
| S8 | P6 | `cores/bbc/sim/disk/run.sh` + app test `coreapp` + board | tier 2 + board |
| S9 | P7 | app test, `ci/lint.sh` | CI apptests/lint |
| S10 | P8 | Vivado report line of the core stage | CI bitstream |
| S11 | P2, P5 | `cores/bbc/sim/adc/run.sh` | tier 1 |
| S12 | P9 | by hand with photos (real ROMs cannot be in CI) | board session |

Every part has a scenario (P1 S1, P2 S2 S11, P3 S3 S4 S5, P4 S7, P5 S6 S11, P6 S8, P7 S9, P8 S10, P9 S12).

## §3 Decision (proposed, coordinator; nothing here is decided with Alex before §3a is answered)

**D1 — Source: the BeebFpga machine core, taken from the MiSTer port's tree, with MikeJ's `m6522` (from BeebFpga) in place of Gideon's VIA.**
Reasons: one machine core serves both repositories, Model B and Master in one parameterised file; the MiSTer tree adds the 1770, the
RTC and the Master's 65C02; the BeebFpga tree has the Gowin/Xilinx top levels as a guide for a top without vendor RAM. Lost: writing a
clean 6502 etc. from nothing (a month of work for no gain). **Licence verdict: usable TREX-internal and non-commercial, free of charge; not
usable for a sale or a fee** (Stirling's clause). It fits `CLAUDE.md` ("third-party cores are TREX-internal", private repository, releases by hand
and free), but a public GitHub release of the gateware is a decision for Alex (Q3). Gideon's `via6522.vhd` is not taken (also keeps the
clean-room rule simple). `fdc1772` (GPL v3) and the Tube (LGPL) are taken only if Q4/Q6 say so; GPL v3 is how `cores/apple2` already stands.
The CPU: `T65` (BSD-style, known licence) first; `R65Cx2` (Wendrich derivative, licence line only a copyright) only after Q2 and only if the
header is clarified; the cycle-exact need is checked by S2.

**D2 — Tree and class.** `cores/bbc/` with `core.toml` (id `bbc`, `app = "blueprint"`, `experimental = true`, `roms`), `layer2_bbc.v`,
`core_sources.tcl` (sets `l2_dcp`), `rtl/` (vendored files + `NOTICE`), `trx/` (glue), `sim/`. **DFX if it routes under pb_l2, else large
(`delivery = "cfw"` as `cores/cpc`).** Model B alone is DFX; the Master 128K is the one that may not be (D6).

**D3 — Clocks.** The 2 MHz / 1 MHz CPU phases, the 16 MHz pixel clock of the ULA and the 4 MHz sound divider as clock enables of the
host's `sys_clk` (as the Spectrum and CPC cores do), no second PLL; the 50 Hz frame comes out of the 6845 as in the original. A video mode
whose enables do not divide the host clock cleanly is a risk to be measured in S4/S5 (the CPC solved the same with the gate array's enables).

**D4 — Memory.** Model B: 32 KB RAM in BRAM (8 RAMB36); MOS + BASIC + DFS + the sideways ROMs of the user's set in BRAM for the first
four slots (16 tiles at most), the rest (slots 4-15 as sideways RAM/ROM) behind the DDR2 cache of `cores/apple2` (aux RAM approach) when used. Master 128K: 128 KB RAM + 128 KB ROM would use 64 of 40 tiles, so the RAM and ROMs go to DDR2 behind the cache (as apple2 aux RAM); this is a measured-latency risk against the 2 MHz bus and is the reason Master is a separate step. The SAA5050 character ROM is 1-2 tiles. The only ROM data in the bitstream is what the app writes at start, from the user's files (same mechanism as `roms` of other cores).

**D5 — Picture.** Video ULA output (3-bit RGB, flash, teletext with its own RGB) is written by the T119 writer (`*_pixw`, about 380 LUT, 2 RAMB36)
into a DDR2 frame buffer of the pixel layer, one frame buffer per mode class; the app declares geometry per mode (640x256 etc.) as the CPC's
does for mid-frame changes. Palette changes mid-frame are kept (the ULA writes the colour per pixel). The scandoubler / HQ2x of the MiSTer top
are not taken (the pixel layer scales).

**D6 — Models.** Step order: Model B, 32 K, 6502, MOS 1.20 first. Master 128K (65C02, 128K, CMOS RTC, ADFS) as the second milestone of this
spec, after Q1. Model B+ is not planned (Q1).

**D7 — Keyboard.** The core takes a key matrix (row/column 8x8 incl. the 10 function keys that the BBC has) as a register window set by the
app from the host's key events (like the VC-20 matrix); no PS/2 front end (`keyboard.vhd` is not used). BREAK is a separate app key
(proposed: F12? the C64U's RUN/STOP-RESTORE map is the app's call, Q7); SHIFT+BREAK is the "boot disk" combination from the original,
produced by the matrix.

**D8 — Disk.** Proposal (A): an 8271 or 1770 sector adapter in the CPC pattern (`coreapp/cores/cpc_disk.cc`: the image copied as it is into a
slot of the core's DDR2 window, the controller reads and writes it there, dirty bits per 16 KB chunk, flush by the app). That gives real
`.ssd` (single, 100 KB/200 KB) and `.dsd` (double sided, interleaved tracks) with DFS exactly as the machine ran and keeps copy-protected
nothing (no flux). Rejected: the MMB/SPI-SD route (loads `BEEB.MMB` through MMFS; not a disk, needs a ROM nobody asked for). The Model B's
controller is the Intel 8271; neither source has it (BeebFpga: none, MiSTer: 1770 for the Master), so Model B disks need either a new 8271
(clean-room from the datasheet, about 1,000 LUT estimated) or a 1770 plus a DFS for the 1770 (Acorn 1770 DFS) in place of the standard DFS —
Q4. Formats: `.ssd` = 256-byte sectors, 10 per track, 40/80 tracks; `.dsd` = the same two sides interleaved track by track; the DFS catalogue
sits in sectors 0-1. Tape (`.uef`/cassette through the system VIA and ACIA): out of this step (Q8).

**D9 — Sound.** SN76489 clocked from the 4 MHz enable, output (3 tone + noise) to the host's audio path by the usual mixer input; the Master's
and Model B's chip are the same. No Music 5000, no SID, no speech (Q9).

**D10 — ROMs.** The roms folder of the core holds the user's files; the descriptor lists them by name (never in the repo, the CI, a `.cor` or a
package; `docs/layering-map.md`'s rule). Proposed names for Model B: `os12.rom` (MOS 1.20, 16 KB), `basic2.rom` (BASIC II, 16 KB), `dfs.rom` (DFS, 16 KB;
recommended: the 1770 or 8271 DFS that Q4 selects), optional further sideways ROMs `rom4.rom` ... `rom15.rom`; Master: `mos.rom` (MOS 3.20, 16 KB),
`basic4.rom`, `adfs.rom`, `dfs.rom`, `terminal.rom` etc. (the same 16 KB banks as MiSTer's `roms/m128`, names there). The app checks existence and 16 KB size
(S9). The SAA5050 glyph table is `saa5050_char_rom.vhd` (no licence header in the file: S1 fails until it is on file; the glyphs are the chip's, not the Acorn ROMs).

**D11 — Area (estimate, not measured, no Vivado run).** From the component sizes of comparable cores and the figures this repository holds
(CPC: 5,938 LUT and 46.5 tiles routed in the large core with its Z80/FDC/gate array; VC-20 about 2,540 LUT; T119 writer about 380 LUT): 6502
(T65 about 1,100; R65Cx2 about 700) + 2 VIAs (about 700 each = 1,400) + 6845 (about 500) + video ULA (about 400) + SAA5050 (about 400) +
SN76489 (about 300) + glue, ADC, key matrix (about 400) + pixel writer (380) is about **4,700-5,100 LUT for the Model B**, about 6,500 with
a 1770 and about 7,000 with a new 8271; Master with 65C02, RTC and DDR2 cache about 8,000-9,000. Block RAM: Model B about 8 (RAM) + 16 (ROM) +
2 (char ROM) + 2 (writer) = **28 tiles** if all four ROM slots sit in BRAM, fewer with fewer ROMs. Budget: pb_l2 is 13,600 LUT, 40 RAMB36 and 40 DSP
(`host/build.tcl`, T164 run 1119); the highest LUT use that ever routed is 78 % (`docs/layering-map.md`), which is about 10,600 LUT. **Model B (with
either disk controller) fits DFX on the estimate; the Master fits on LUT but its BRAM only with the RAM and ROM in DDR2.** The published figure
that matches: BeebFpga's Xilinx targets (Spartan 6 LX9/LX25, 5.7k LUT6) carry the whole machine; the wiki has no resource table (fetched
2026-11-05), so this is an order of magnitude, to be replaced by a Vivado report in the first area step (Plan, step 3).

## §3a Open questions for Alex (one per line, in the order they block)

1. Model B only for this spec's first release, or Master 128K as well (the Master is the second milestone in D6; B+ not at all)?
2. Source: BeebFpga/MiSTer machine core (recommended, D1) with T65 or Wendrich's R65Cx2 as CPU?
3. Licence: the non-commercial clause allows our internal, free use; may the gateware also go into a public GitHub release, or stays it internal like the Atari OS ROM?
4. Disk system: 8271 DFS (Model B as it was, needs a new 8271) or the 1770 (exists in the MiSTer tree, GPL v3) with a 1770 DFS ROM?
5. Disk transport: the CPC pattern (D8, real `.ssd`/`.dsd` in the DDR2 window, written back) or MMB container only?
6. Tube co-processor (65C02/Z80 second processor): in or out?
7. BREAK and the BBC's function keys on the C64U keys: which keys (the C64U has F1-F8, RUN/STOP, RESTORE)?
8. Tape (`.uef`, cassette port, ACIA): in this spec or out?
9. Extras of BeebFpga (Music 5000, SID, NuLA, mouse, analogue joystick through paddles): any in?
10. Is "a DFX core" a hard demand for Model B, or may it become large if the area surprises us (as CPC)?

## §4 Out

Decided with Alex later (candidates are the questions above: B+, Tube, tape, Music 5000/SID/NuLA, MMB, Master if Q1 says so).

## §5 Decisions taken while building

(empty)

---
