# T140 — The board puts out an analogue picture: S-Video (Y/C) and composite, PAL and NTSC

**Status:** PROPOSED (2026-10-28), asked by Alex. C128 build and bench done 2026-10-30 (branch t140-c128-av, §5); no Vivado run, no board. 2026-11-13 status check: built in part and merged (C128 AV, #19/#21 fixed 11-09, #67 board pass 11-11); open: hang after warm reset, load stop, T170 SRQ (as row). 'no Vivado run, no board' above is outdated. 2026-11-15 sync: re-read against the code - D23 (menu on the analogue picture) is still not built (`cores/c128/mono/top.v:1424-1431` feeds `c128_avout` only the VIC's `pix_color`; no overlay or pixel layer reaches `AVID_*`); T170 SRQ still not wired (`c64_top_c128.v:934-936,991-993`, `mono/top.v:742`; the line cites in items 6/10 are of 2026-10-31); S-Video/composite PAL+NTSC board measurement is on the 11-14 session list, no result recorded.
**Repo:** TRX64U-MultiCore — the host (`host/`: the encoder and the pins; a host change, Alex approves it, `host_pin` in
the same commit), the host app (T139 settings page, D5), large cores (their copy of `top.v`).
**Number:** T140.
**Origin:** Alex, 2026-10-28 (translated), after Rick's report about UltiSID in HDMI mode: "We do not have S-Video because
I have no hardware. How complex would it be to drive the port and put out a signal? 576x480 or whatever it is ... so
PAL (and NTSC)?" Later: "Create a spec, yes, we know the pins. Is Gideon's code open and GPL there, or do we then have
the clean-room problem? What does MiSTer do?"
**Depends on:** T139 D5 (the settings row comes with this), T105/T107 (picture modes).
**Milestone:** M3.

---

## §1 The problem

The C64U has the C64's A/V port (luma, chroma, composite) behind a THS8136 triple 10-bit video DAC. Our host never
drives it: the pins are in `host/c64u.xdc` (Gideon's published constraints, issue #615) and in no RTL file of this
repository (`grep AVID` over `host/` and `cores/`: none). So the MultiCore line has HDMI only; a CRT or a capture box on
the A/V port stays dark.

What is known (read, not guessed):
- Pins (`host/c64u.xdc`, 1.8 V, drive 4): `AVID_R[9:2]`, `AVID_G[9:2]`, `AVID_B[9:2]` (8 bits each, bits 1:0 not wired),
  `AVID_CLK` C22, `AVID_SYNCn` F18, `AVID_FB` E18.
- Boundary scan of Gideon's firmware (`1541ultimate-tools/docs/c64u-pinmap.md`, 2026-09-30) saw three behaviours on
  those balls: a carrier that is zero for greys (chroma), a level per colour (luma), and a third channel (composite).
  Which of R/G/B carries which signal is a hypothesis from that measurement.
- Gideon's encoder: the U64 FPGA design is published as a binary only (no source in his GPL repository); there is
  nothing to read and so no clean-room question. We write our own or take an open one.
- MiSTer: `Template_MiSTer/sys/yc_out.sv` (Mike Simone, 2022, GPL v2 or later) turns 24-bit RGB with sync and a
  colour-burst window into luma plus quadrature-modulated chroma, or composite, PAL or NTSC, from a phase accumulator.
  MiSTer puts it out through the VGA DAC of its analogue I/O board. GPL v2+ fits our other third-party cores.

Parts:
- **P1 Pins and channel map**: which DAC channel is luma, chroma, composite; bit order; clock edge; what SYNCn and FB do.
- **P2 Timing**: 15 kHz PAL (625 lines, 50 Hz) and NTSC (525 lines, 60 Hz), interlaced or 240p/288p progressive (what
  the C64 itself puts out).
- **P3 Encoder**: RGB -> Y, C, CVBS with colour burst, PAL phase alternation.
- **P4 Picture source**: the host's own picture (the menu) and each core's picture (DFX through the pixel layer, large
  cores through their copy), scaled or native.
- **P5 Settings**: the row on the F8 page (T139 D5), boot.cfg.
- **P6 Area and timing**: `pb_l1` is at 77.6 % LUT, 24.5 of 25 BRAM tiles (run 1016).

## §2 Scenarios

```gherkin
Scenario: S1 the channel map
  Given a test pattern on each DAC channel in turn (ramp on one, mid-level on the others)
  When  measured at the A/V port (scope or capture box)
  Then  luma, chroma and composite are named per channel, with bit order; recorded in §5 with the measurement

Scenario: S2 the encoder in simulation
  Given the encoder bench with colour bars
  When  a PAL and an NTSC frame are simulated
  Then  burst phase, subcarrier frequency (4.43361875 / 3.579545 MHz) and the Y/U/V of each bar match a model within 1 LSB
  And   control: the PAL phase alternation switched off -> the bench fails

Scenario: S3 picture on a CRT or capture box
  Given the host menu, then a core (C64 large, Amiga), PAL and NTSC
  Then  stable colour picture on the S-Video and composite input (photo or capture, by Alex)

Scenario: S4 fits
  Then  the host routes with WNS >= 0 and pb_l1 within its budget
```

| # | Solves | Test | Where |
|---|---|---|---|
| S1 | P1 | board, measurement | board session (needs a CRT, scope or capture box) |
| S2 | P2, P3 | encoder bench (iverilog/GHDL) | `ci/sims.txt` |
| S3 | P4, P5 | board photo | board session |
| S4 | P6 | Vivado report | CI |

## §3 Decision

- **D1** MiSTer `yc_out.sv` (Q2), first in the C128 large core with its native 15 kHz picture (Q3/Q4), HDMI and analogue at
  the same time (Q5). Build started 2026-10-30 (Alex: "Yes please"); his A/V cable arrives the same day.
- **D23** (Alex, 2026-10-31, after the Sony test showed no menu on the analogue output): "Yes please, for the C128 in any case. Basically
  everything that supports analogue out should then show a menu too." (translated) The menu overlay goes onto the analogue picture of every
  core with an analogue output, as Gideon's firmware does.
- **D24** (Alex, 2026-10-31, after the Sony findings and the utilization of run 1079): "send an agent that does the analogue out things and
  rebuilds the core, optimised for more space, as much as possible. Because we should support the analogue ports for the floppies (serial
  port)." (translated) Scope: Y/CVBS onto the right DAC channels, the PAL V switch, a System Mode setting in the C128 menu (NTSC), the menu on
  the analogue picture (D23, text overlay), and free as much room in the C128 large core as can be freed (candidates from run 1079: C128 RAM
  128 KB = 32 RAMB36 into DDR2, the ESP32 link 1,126 LUT, tape 814 LUT, JTAG debug 692 LUT) so real drives on the IEC port fit (T170).
- **D25** (Alex, 2026-10-31, after the agent left the RAM move to him: VIC data due within phi1 = 16 clocks, DDR2 fetch measured 15-27 clocks,
  T113 5.4): "Yes, into the DDR2 please, we had already decided that earlier." (translated) The C128's 128 KB RAM moves into DDR2; the VIC
  side needs what makes its fetches safe (prefetch / line cache / reserved slot), the CPU may wait.
- **D26** (Alex, 2026-10-31): "the agent with T140 should do all of that together, so rebuild and finalise the C128 before it goes
  anywhere." (translated) One branch `t140-d24` carries the whole C128 rework - D24 (analogue fixes, System Mode, JTAG flag), D25 (RAM in
  DDR2), T170 (SRQ / fast serial to real drives, the bus log of T170 D2) and D23 (menu on the analogue output) - and is merged once,
  after its Vivado run and the board check of the analogue picture.
  Then, Alex (2026-10-31): "and then, when it is done, the scaler and the HDMI modes" (translated): the C128 also gets the HDMI modes and
  the scaler of T107 D11 as the first large core on the shared include that T164 designs (D11d), before the branch is merged.
- **D27** (coordinator, 2026-10-31, on D25): four VIC slots instead of two - the whole RAM block 0 (4 banks x 16 KiB = 16 RAMB36) stays mirrored in block RAM.
  Reason: with two slots the VIC reads $FF on the first touch of a bank or with three banks in rotation, i.e. picture faults in demos; the VIC in C64 mode
  always sees block 0, so with block 0 whole there is no first-touch miss and no eviction there. Built as `c128_ramd` `NSLOT = 4, PIN = 1` (slot i = bank i,
  nothing is allocated, swept once after configuration). A VIC read of block 1 (MMU VIC block bit, C128 mode only) answers $FF and is counted (`n_vic_blk1`).
  Expected freed: 14 RAMB36 (32 - 16 - 2 for the CPU cache), Vivado decides.
- **D27 finding, 2026-11-04 (branch c128-vicglitch; measured, the fix is PROPOSED and Alex's to decide).** The stated limit "a VIC read of RAM block 1 answers $FF" is what
  PETSCII Robots C128 (issue #19; board 2026-11-04: every character cell a solid block in its colour RAM colour, the glyphs missing) runs into: the game sets
  MMU RCR `$D506` = `$45`/`$41` (bit 6: the VIC reads RAM block 1; VIC bank 3, bitmap `$C000`, matrix `$E000`, sprites `$E400` of block 1; it never writes `$D018`, `$D011`,
  `$DD00` after the loader, `$D030` not at all). `c128_ramd.v:178` (`rd_word = s_hit ? s_word : (r_vic ? 16'hFFFF : c_word)`): slots 0-3 carry the tags 0-3 (`:98`), the VIC's
  tag of block 1 is 4-7, no slot matches. Bench `sim/run_vicblk.sh` (programs `vicblk0`/`vicblk1`: the same multicolour bitmap picture in block 0 / block 1, VIC pointed
  at it): bram build, both blocks: pixel hash of frame 10 `b51521dc` = `b51521dc`; ddr build: block 0 `b51521dc`, block 1 `38515a6d` (49,944 of 157,248 pixels differ, every cell
  one colour-RAM colour), `+VICCHK`: 76,168 of 76,168 block-1 VIC reads answered `ffff` (first: frame 9, line 101, x 249, the idle g-access `$3FFF` of VIC bank 1, want `3c3e`),
  0 wrong in block 0 after the start-up sweep. The game itself (`sim/run_robots.sh`, real ROMs from outside, RAM image of the loader injected, the game's own start routine `$416D`, frame 150, pictures in
  `sim/vicglitch/pics/`): bram build shows the game screen (frame, "INSERT MAP DISK AND HIT A KEY"), ddr build the board photo's picture (62,852 of 157,248 pixels differ; solid
  cells in colour RAM colours); pixel hash `d46c23df` (bram) against `dfd8e8c9` (ddr); `+VICCHK`: 1,028,528 of 1,028,528 block-1 VIC reads wrong (first: frame 125, line 265, x 209,
  block 1 word `$7FFF` = the idle g-access `$FFFF` of VIC bank 3, got `ffff`, want `ff17`). Not the cause of issue #21 (text screen, block 0): bench `sim/run_vicrom.sh` (static text
  screen, endless ROM line misses, KERNAL-style `$D015`/`$D030` writes) gives one pixel hash for every frame in both builds, 23,804 ROM misses per 700 ms. Proposed fix: one more slot, allocated on demand for block 1 (`c128_ramd`: `NSLOT = 5`, slots 0-3 pinned as now, slot 4
  allocated when the VIC's tag has been block 1 and stable for 4 clocks, swept by the page that missed first, about 1 ms of $FF at a switch): +4 RAMB36 (16 KiB), ahead of 61 of
  75 estimated in section 10; a second full set of 4 slots (+16 RAMB36) does not fit.
- **D28 (Alex, 2026-11-05: "then let's try it, so 2", translated; options in the chat: (1) VDC 16 KiB + block 1 pinned whole, (2) a fifth
  slot allocated early, (3) VIC from DDR2 with line prefetch):** the D27 finding is fixed with a fifth slot for RAM block 1 (`c128_ramd`
  `NSLOT = 5`, slots 0-3 pinned as now, +4 RAMB36), allocated and swept as soon as the program selects block 1 for the VIC (MMU RCR
  `$D506` bit 6 written, or the VIC bank changed while bit 6 is set), not only at the VIC's first block-1 read; the VDC keeps 64 KiB.
  A VIC read of block 1 before the sweep has reached its address still answers `$FF` (residual, measured and reported).
- **D29 (Alex, 2026-11-06, board: the D28 build hides the menu overlay and the start looks hung, the 126c2078 build does not; "the VIC fix was the most important part. Without it the 128 is not usable", translated):** D28 stays, no revert. The regression is hunted on the D28 build: first experiment, the memories Vivado moved from block RAM to LUTs under the tighter BRAM budget (host register `pages`, the UART receive FIFO) are pinned to block RAM, built and tried on the board; the cause is not proven yet (code reading rules out D28's logic in the menu, the builds differ in BRAM-to-LUTRAM moves and placement at 99.62 % slices).
- **D30 (Alex, 2026-11-07, board test of the D29 build, branch c128-d28ovl 41da4d374: menu visible again, JiffyDOS boots and loads; still
  one character row of wrong data flickering for 1-3 frames in Attack of the PETSCII Robots (title and game), and loading stops after a while
  in stock mode; "A, but maybe you will still fix the bugs", translated):** the next tester release carries the D29 build with both faults
  listed as known; fixes go in if they are proven (bench, Vivado, board) before the release, otherwise in the next one.

## §3a Open questions

- **Q1** Answered 2026-10-28 (Alex, with a photo of the back of his Sony TV, translated: "The Sony gaming TV still has
  that. So it works."): the test display is a Sony LCD TV with two SCART AV inputs (AV1, AV2), component (Y/Pb/Pr) and
  VGA. No separate S-Video or composite jack: composite through SCART; S-Video (Y/C) through SCART only if that input
  takes Y/C (the TV's setting). Needs a cable from the C64 8-pin A/V DIN to SCART (with luma/chroma wired for Y/C).
- **Q2** Answered 2026-10-28 (Alex, translated): "Yes, that suits me, it has to go on the vendor list then. You can build it
  in right away if you like. I'll get the cable later." -> MiSTer's `yc_out.sv` (GPL v2+), licence inventory row H21.
- **Q2a** (Alex, 2026-10-30, translated: "is ok"): the C128 ships `c128_yc.v`, our rebuild of `yc_out.sv` (same numbers, GPL v2+ like its source,
  `cores/c128/PROVENANCE.md`), instead of `yc_out.sv` itself, so the core places; `vendor/mister/yc_out.sv` stays in the tree as the source it is derived from (no build or bench reads it now).
- **Q2b** (Alex, 2026-10-30, translated: "C128: Ethernet and WiFi off."): run 253 still did not place (6,001 slices needed, 5,931 free) with
  `c128_yc.v`; for the analogue test the C128 build drops Ethernet (`i_eth`, 1,231 LUT) and the ESP32 WiFi bridge (`i_wifi`, 1,142 LUT) of
  `ultimate_sys` (a C128 fork, `host/` stays as it is). The C128 has no network then (no FTP/REST while it runs). Built: §5, branch t140-c128-nonet.
- **Q2c** (Alex, 2026-10-30, "ja"): only Ethernet goes; the WiFi UART stays, because the ESP32's case buttons (menu, freeze, power off,
  reboot) arrive over it. Ethernet alone frees 1,231 LUT against the ~280 missing.
- **Q3** Answered 2026-10-28 (Alex: "Ja" to the proposal): first in the C128 large core - the VIC-IIe's native 15 kHz
  picture into the encoder, no scaler, in the core's own copy of `top.v`; the host stays untouched. The host for every
  DFX core is a later decision of its own.
- **Q4** Picture: 15 kHz native (the core's own lines, no scaler) or the scaled picture of the pixel layer downscaled.
- **Q5** Answered 2026-10-30 (Alex: "Yes please. At the same time."): HDMI and analogue run at the same time from the same
  picture, no switch.
- **Q4** for the C128 follows from Q3: the native 15 kHz picture.

## §4 Out

- RGB/SCART: the port has no RGB lines.

## §5 Build notes

### 2026-10-31, board at the Sony TV (c128.cor of main-76817bbe, S-Video SCART cable on AV2; Alex at the TV)

- Reference first: Gideon 3.15a on the same cable shows a black-and-white picture (the cable puts Y on the SCART video pin; this Sony takes no
  Y/C on SCART, so only luma is seen). Correct for a clean luma pin.
- C128: picture stable, placed right, no rolling, in C128 mode (40 columns) and C64 mode. **But the luma pin carries a colour carrier**: the TV
  decodes colour and shows the composite lattice even on flat areas - the Y and CVBS words are most likely swapped onto the DAC channels
  (`mono/top.v:1383/1385`; `:116` says the order inside a channel was never measured; the levels look right, so the bit order seems fine).
- **PAL V axis inverted**: blue right (C64 mode background), the C128 light green border shows magenta - colours that live on V flip, U ones
  do not: the V switch is off by one line against the burst swing, or has the wrong sign.
- No menu on the analogue output (the overlay is HDMI only; Gideon shows his menu on analogue too) - open point for Alex.
- NTSC not testable: the C128 core app has no System Mode setting (`coreapp/cores/c128.cc`), so `vmode_sys` stays PAL.
- Colour can only be judged with a composite (cinch) cable or a set that takes Y/C.

### 2026-10-30, branch t140-c128-av: the C128 large core drives the A/V port (built, bench green; no Vivado run, no board)

**Pins and channels (P1).** `host/c64u.xdc` (Gideon's issue-615 constraints): `AVID_R/G/B[9:2]`, 24 balls, 1.8 V, drive 4; `AVID_CLK`
C22, `AVID_SYNCn` F18, `AVID_FB` E18. The names `R/G/B` do not name the DAC channels: the boundary scan (`1541ultimate-tools/docs/c64u-pinmap.md`,
2026-09-30; its table, not a guess of ours) sorts the 24 balls into three groups of 8 that cut across the names - the two balls it did
not list (C19 = B9, E19 = B6) complete the luma group. Gideon's app (`u64_config.cc`) only offers "CVBS + SVideo" or "RGB" and writes
`C64_PHASE_INCR` / `C64_VIDEOFORMAT`; it names no pin. Used (MSB first, `cores/c128/mono/top.v`):
- luma Y: G9 C20, R9 B22, G7 D21, G6 E22, G4 F21, G3 G22, B9 C19, B6 E19 (C20 is the MSB by the colour scan: it is high from luma 12/32 up)
- chroma C: R4 A19, R3 B18, B8 D20, B7 D19, B5 C18, B4 F19, B3 C17, B2 F20
- composite CV: R8 B21, R7 A21, R6 B20, R5 A20, R2 A18, G8 D22, G5 E21, G2 G21
- `AVID_SYNCn` = 1 (the sync is in the Y and CV words, the DAC's own sync input is not used), `AVID_FB` = 0, `AVID_CLK` = sys_clk inverted
  (ODDR), so the DAC takes the words half a clock after they changed. **Not measured:** the bit order inside a channel (only C20 is
  evidenced), what SYNCn and FB do on the THS8136, the DAC full scale and the port's resistor network. S1 settles them.

**Clocking (P3).** The encoder runs on `sys_clk` (dot4x: 31.5278 / 32.7257 / 32.7652 / 31.5848 MHz for System Mode 0..3, mode_ctl.v). MiSTer
feeds `yc_out` with a phase increment for its clock; the same here: `PHASE_INC = fsc / f_clk x 2^40`, one constant per System Mode
(PAL 4.43361875 MHz in modes 0 and 2, NTSC 3.579545 MHz in 1 and 3): `0x24000d4db3`, `0x1c00587f82`, `0x22a4022593`, `0x1d0345e24a`. The standard
follows the core's System Mode (`vmode_sys`), so PAL/NTSC and 50/60 Hz are the ones the HDMI picture and the C128 run at. No second
clock, no clock crossing: the pixel stream is the scaler's own (`pix_color/x/y`, sys_clk).

**Picture and timing (P2/P4, Q4).** `cores/c128/mono/c128_avout.v`: the VIC's raster position (loaded at every dot, free-running while the VIC is
paused so the sync stays in the freeze menu) gives hsync (37/38 dots = 4.7 us), the 400 x 272/240 window (X0 96, the scaler's own), three
serrated vsync lines and the burst window; palette = a second `pal_ram` on the app's palette writes (the colours of the HDMI picture).
15 kHz native (63/65 cycles x 8 dots, 312/263 lines, progressive), no scaler. Levels (8 bit, full scale = 1 V assumed): Y sync 0, blank 77 (PAL) / 73 (NTSC),
white +175; C 128 +- chroma, burst 2 x 39; CV = clamp(Y + C - 128).

**Code.** `vendor/mister/yc_out.sv` (byte for byte, `PROVENANCE.md`, licence row H21). `cores/c128/mono/top.v` = the core's own copy of `host/top.v`
(`top.v.diff`: ports `AVID_*`, the `c128_avout` instance, the ODDR); `build_mono.tcl` reads it, `c128_avout.v`, `yc_out.sv` and
`mono/avid_pins.xdc` (the pin lines of `c64u.xdc`). `host/`, `contract/`, `cores/c64ref`, the DFX cores: untouched. HDMI path: unchanged
(the scaler and everything after it read the same wires; the new block only reads `pix_*`, `vmode_sys`, the palette write bus).

**Bench (S2).** `cores/c128/sim/run_avout.sh` (Verilator, `ci/sims.txt` `c128_avout`, lane large:c128), 4 System Modes, each 3-5 frames of a
16-bar VIC model; run on the builder (trx64-sim-env:4):
```
M0 PAL : 1173 lines, 9 serrated, 3 frames, D=12 clocks, 10491 checks ok, 0 failed
M1 NTSC: 1181 lines, 15 serrated, 5 frames, D=12 clocks, 11306 checks ok, 0 failed
M2 PAL : 1182 lines, 15 serrated, 5 frames, D=12 clocks, 11315 checks ok, 0 failed
M3 NTSC: 1176 lines, 9 serrated, 3 frames, D=12 clocks, 10514 checks ok, 0 failed
ctl| AVOUT_MUT_NOSYNC: fails as it must (21 FAIL lines, first: FAIL M3 frames seen)
ctl| AVOUT_MUT_NOBURST: fails as it must (11602 FAIL lines, first: FAIL M2 burst amplitude)
ctl| AVOUT_MUT_NOPAL: fails as it must (2314 FAIL lines, first: FAIL M2 PAL burst moves by +-90 degrees (0.891744 deg, raster line 5))
ctl| AVOUT_MUT_BADINC: fails as it must (4668 FAIL lines, first: FAIL M2 burst frequency 3600971.958744 Hz, fsc +-2 %)
PASS run_avout
```
Checked per line: sync width and period (HT x 4 clocks), 3 serrated lines at raster line 296 (312 lines) / 12 (263 lines), LT lines per frame, Y = blank
level after the sync and outside the window, burst amplitude 60-110 pp and frequency fsc +-2 %, C = 128 in the sync and after the burst, 16 bar luma levels
(BT.601 coefficients of yc_out, +-3 codes), grey bars without chroma, colour bars with chroma at fsc +-2 %, picture position against the sync (+-6 clocks), PAL burst
phase +-90 degrees alternating line to line, NTSC burst not moving. What the bench does not prove: the DAC levels, the pin order, any picture on a screen (S1, S3).

**Area (P6) - an estimate, no Vivado run yet.** yc_out: 2 multipliers 11 x 11 (2 DSP48), three 64 x 8 sine ROMs, about eight 20-bit adders,
about 350 FF; c128_avout: counters and compares, two small pipelines, `pal_ram` (24 LUTRAM): about 600-750 LUT, 500-600 FF, 2 DSP, 0 block RAM
(about 2 % of the XC7A50T's LUTs). Every stage is one adder or one multiplier at 31-33 MHz (period 30 ns): no timing risk expected; the CI hierarchy
report (`utilization_hier_synth.rpt`, `avout`) replaces this paragraph.

**For the board test (S1/S3).** Cable to the A/V DIN, then (a) boundary scan with the new bitstream and `video_colors.py` (border colour stepped): every ball now has a
known code per colour, so the bit order inside each channel falls out of the 16 colours (Y word = BLK + 11/16 of the palette luma); (b) a scope on Y/C/CV: sync tip,
blank, burst amplitude (0.3 V pp expected for 1 V full scale - if the amplitude is off, only the constants in `c128_avout.v` change); (c) whether S-Video and
composite work together on the one port, what `AVID_SYNCn`/`AVID_FB` need (0/1 swapped if the picture has double sync or is blanked); (d) the picture on the Sony (S3),
in the freeze menu too (sync holds), and a System Mode switch (NTSC <-> PAL) with the screen locking again.

- **2026-10-30, run 1059: the C128 does not place.** `ERROR: [Place 30-487] ... 8150 slices in the device, of which 5960 slices are
  available, however, the unplaced instances require 6087 slices`; post-synthesis BRAM 75 of 75. The last green C128 (3539a8b6) was at
  28,619 of 32,600 LUT (87.8 %), 8,150 of 8,150 slices, 75 of 75 BRAM, so the encoder (estimate 600-750 LUT) does not fit as built.
  Asked whether to move it to the A1200 (72.8 % LUT) or stay, Alex (translated): "It can stay in the C128. It is only a test for now."
  So: make room in the C128 for it (shared palette, smaller encoder parts, other savings), measured, not guessed.
- **2026-10-30, room (branch t140-c128-room): the encoder rebuilt as `cores/c128/mono/c128_yc.v`** (yc_out.sv's numbers; `vendor/mister/yc_out.sv` stays
  untouched, no longer read by the C128). Cuts: the colour matrix (306R+601G+117B, x504, x898, x11/16) as multiplications by constants in 6 DSP48
  instead of shift-add chains in LUT; the 24 bit RGB register, the Y/C stages and the second 11/16 adder chain of c128_avout gone (latency 12 -> 9
  clocks); PHASE_INC 40 -> 26 bit (0.49 Hz resolution, subcarrier off by <= 0.27 Hz = 0.08 ppm). Measured with yosys `synth_xilinx` (same tool
  on old and new, so the difference counts, not the absolute figure; Vivado packs about 10-20 % tighter): c128_avout + encoder + pal_ram 647 LUT +
  6 RAM32M + 2 DSP (about 690 LUT-eq) -> 431 LUT + 6 RAM32M + 8 DSP (about 457 LUT-eq): **-230 LUT-eq, -34 %**. The palette is not shared with the
  scaler (it lives in host/vic_scaler, immutable); pal_ram is 24 LUT, so nothing to gain. Equivalence: with WIDTH = 40 the DAC words equal the old
  chain's delayed by 3 clocks in all four System Modes (random palette and pixels, 4.7 M samples, 0 differences; a first version with a 10 bit
  constant 898 differed in 70 % of the samples, so the compare can fail); `run_avout.sh` PASS, all 4 controls fail. **Not measured in Vivado**:
  the builder ran CI containers; whether Vivado puts the sum of three products in a DSP cascade (else about +100 LUT) shows in the next synth.
  Still short of the asked ~1,000 LUT: see the report; the rest needs a decision (jtag_dbg 683 LUT, flow knobs).

### 2026-10-30, branch t140-c128-nonet: Q2b/Q2c built - the C128 large core without Ethernet, the WiFi UART kept (bench green; no Vivado run, no board)

`cores/c128/mono/ultimate_sys.vhd` is a copy of `host/ultimate_sys.vhd` (diff in `mono/ultimate_sys.vhd.diff`: only the Ethernet part differs);
`build_mono.tcl` swaps it into Gideon's VHDL list in place of the host's file, `host/` is untouched. `i_eth` (`ethernet_interface`) is replaced by a tie-off;
`i_wifi` (`uart_dma`) is the host's instance, unchanged (Q2b dropped it too; Q2c, Alex 2026-10-30, put it back for the ESP32 case buttons). Measured in the hier
report of the last green run (`mono-c128-158eff2d1a147751`, before the A/V port): `i_eth` 1,231 LUT / 784 FF / 1 DSP - about 1,231 LUT dropped against the ~280 missing.

**How the slave answers.** The Ethernet I/O window 0x060800-0x06083F stays decoded by the host's own region logic and answers like the UCI window already does
(T103): `ack` one clock after any read or write, data 0, so the RISC-V never waits. The DMA port (`eth_mem_req`) is the idle record - the 4-port memory arbiter stays,
its Ethernet port is constant. Interrupts `eth_irq_rx/tx` are 0; `eth_tx_*` 0 (top.v's `rmii_transceiver` stays and keeps the PHY quiet; the core's packet port can still
use it). The WiFi UART (window 0x060900-0x06090F, DMA port, IRQ, `WIFI_TXD`/`WIFI_RTS`, `wifi_event`) is as in the host.

**Software side (read in Gideon's firmware, `1541ultimate/software`, nothing changed in coreapp).** The only user of the Ethernet MAC is `RmiiInterface`
(`rmii_interface.cc:50`), and it starts only when `CAPAB_ETH_RMII` (bit 24 of the ITU capability word) is set; the fork of `top.v` sets `HOST_CAPS` to
`28'h4E40202` (the host has `5E40202`), so the driver, its 48 KB buffer, the PHY poll and DHCP are not started. No other code tests the bit.

**Consequence: the C128 has no Ethernet** (no FTP, REST, trxmon over the wire while it runs); the ESP32 link works as in the host, so the case buttons (menu, freeze,
power off, reboot) keep working. Not checked on the board.

**LUT estimate.** Run 253: 29,673 LUT, 6,001 slices needed, 5,931 available. Minus 1,231 LUT = about 28,440 LUT (-4 %); the slice need scales to about 5,760,
roughly 170 slices under the limit by this rough scaling (the tie-off adds a few LUT; packing is not linear). Vivado decides; this is not a run.

**Bench `c128_nonet`** (`cores/c128/sim/nonet/run.sh`, GHDL, `ci/sims.txt`, lane large:c128): `tb_nonet` = the port map of `tb_dma_bus` around the fork, a BFM in place of
the RISC-V writes and reads every address of the Ethernet window (64) plus the ICAP ID register next door, checks ack within 20 clocks and read data 0
(not X), then lets noise run on `eth_rx_*` and checks `eth_tx_*`, the two Ethernet IRQs and the DMA request (the IRQs through the ITU registers, the DMA through the
`mem_request` port) stay idle. Source guards: `mono/top.v` has `HOST_CAPS` with bit 24 off; the fork has no `ethernet_interface` but DOES have `uart_dma`, the host's
file has both. Three negative controls (each must fail with its own message): Ethernet ack removed, `eth_irq_rx` high, the fork without `uart_dma` (fails the guard).
Run on the builder (trx64-sim-env:4):
```
ok   the fork has no ethernet_interface but keeps uart_dma, the host's file has both
ok   fork: every access to 0x060800-0x06083F acked, reads 0, eth pins/irq/dma idle
ok   control eth_ack: the bench reports 'no ack at eth' (as it must)
ok   control eth_irq: the bench reports 'Ethernet interrupt active' (as it must)
ok   control nouart: the fork without uart_dma fails the source guard (as it must)
PASS
```

### 2026-10-31, branch t140-d24: D24 - the Sony findings, the System Mode, room (benches green on the builder; no Vivado run, no board)

**Board findings (Alex, Sony TV, S-Video SCART cable with Y to the SCART video pin).** Gideon 3.15a: clean black-and-white luma on that cable. Our C128: a coloured
picture with the composite lattice (the luma pin carried the CVBS word). PAL: blue right, light green magenta (V colours flip, U colours do not). Sync and position
stable in C128 and C64 mode. No menu on the analogue output; no System Mode setting in the C128 core app.

**1. Y and CV on the right pin groups (`cores/c128/mono/c128_avpins.v`, `SWAP_YCV = 1`).** Evidence read: `host/c64u.xdc` names the 24 balls `AVID_R/G/B[9:2]` but the
boundary scan (`1541ultimate-tools/docs/c64u-pinmap.md`, `pinmap/runs/video_colors.json`) sorts them into three groups of eight that cut across those names, and it
named the groups by behaviour (steady for greys = chroma, a level per colour = luma, "mostly high" = a third channel, "probably composite"). Fitting the scan's
per-colour patterns of its "luma" group to the VIC's 16 colours (BT.601 luma, any offset and scale) reaches 76/96 matches in our bit order and 84/96 at best over all bit
orders (C20 alone fits the top bit exactly): the scan does not decide luma against composite. The board does: with the Y word on that group the luma pin showed the
composite word. So the group the scan listed third (B21 A21 B20 A20 A18 D22 E21 G21) is the port's luma pin and the first (C20 B22 D21 E22 F21 G22 C19 E19) the
composite pin; chroma (A19 B18 D20 D19 C18 F19 C17 F20) is unchanged and not yet seen on its own pin (Gideon's picture on that cable was black and white). The bit order
inside a group is the old one (the levels looked right). Bench `c128_avpins` (`sim/run_avpins.sh`, iverilog + `check_avpins.py`): one-hot words, every ball's bit
checked against the board's group assignment, balls read from `avid_pins.xdc`; control SWAP_YCV = 0 (the first build) fails; source guard that `mono/top.v` has no AVID
assign of its own. `top.v.diff` regenerated.

**2. PAL V switch.** `c128_yc.v` against `vendor/mister/yc_out.sv`, line by line: the colour matrix (306/601/117, 504, 898), the carrier, the burst phases (+96 / +160 of
256 = 135 / 225 degrees), `PAL_FLIP`, `PAL_line_count` and the `u +- v` sum are the same; no arithmetic difference. The old bench only checked the burst moving by +-90
degrees, never the V sign against the burst. New in `tb_c128_avout.sv`: a decoder the way a TV works (the two consecutive bursts' bisector is the -U axis, the burst's
side of it names the line with +V, every colour bar's (U, V) is rotated into that frame) compared with the colour's (0.492 (B-Y), 0.877 (R-Y)) within 22 degrees, per
line, all colour bars (blue = U-heavy and light green = V-heavy among them, counted). Result: `c128_yc` as it was (yc_out's V sign) decodes right under the standard reading
"burst at 135 degrees on the +V line", so the fault is not an arithmetic slip; the board says the TV reads the V axis inverted against that burst swing. Built as one
parameter: `c128_yc #(.VINV)` / `c128_avout #(.PAL_VINV = 1)` inverts the V sign of every PAL line against the same burst (NTSC untouched); for a TV that identifies the V
switch from the burst swing and for one that assumes the line parity it is the same inversion. The bench decoder follows (`TB_EXPVINV`). Controls: encoder at yc_out's sign
while the bench expects the board's (`VINV_OLD`) fails with 802 hue lines; both at yc_out's sign (`VINV_STD`) passes, so the decoder is not rigged to one side. **Open for
the board:** if the picture is still wrong after VINV, the V sign is not the cause (next suspect: the vsync lines carry no burst and the 312-line field has an even line
count, so the V-switch phase is not tied to the field start as in a 625-line signal). Run on the builder (trx64-sim-env:4):
```
M0 PAL : 1173 lines, 9 serrated, 3 frames, D=9 clocks, 10822 checks ok, 0 failed
M2 PAL : 1182 lines, 15 serrated, 5 frames, D=9 clocks, 11866 checks ok, 0 failed
ctl| AVOUT_MUT_NOSYNC / NOBURST / NOPAL / BADINC: fail as they must
ctl| VINV_OLD: fails as it must (802 hue FAIL lines, first: FAIL M0 hue of bar 2: U/V angle -102.210487 deg, expected 100.403702 (line type V-, raster line 36))
ok | VINV_STD: encoder and bench both at yc_out.sv's V sign pass (the decoder follows the burst swing)
PASS run_avout
```

**3. System Mode in the C128 core app (`coreapp/cores/c128.cc`).** Setting "System Mode" = PAL | NTSC | PAL-60 | NTSC-50 (the first four of the C64 Ultimate's `sysmodes`; the
two "/L" variants are timing-table variants and are out here), live. Operation `c128.set_sysmode`: `picture_set_system()` (new, `picture.cc`: the host's PLL and scan
registers through the call a change of the Picture makes; `video_current` and the Video follow; no change, no call) and the new `CoreIo::video_format` (`hw.cc`:
`C64_VIDEOFORMAT`) with 0x00 / 0x2B / 0x2A / 0x01 (u64.h bits, `color_timings.cc`), which `host/mode_ctl.v` turns into `vmode` = {b5 != b0, b0} = 0..3, which
`c128_avout.v` turns into the PAL / NTSC carrier constants (bench `c128_avout` measures 4.4336 MHz in modes 0 and 2, 3.5795 MHz in 1 and 3; control `BADINC` fails). The test
(`tests/coreapp/test_c128.cc`, 936 checks) reads `mode_ctl.v`'s own line (stale-model guard), checks the byte of each of the four values and the vmode it gives, one PLL call
per change and none for the same value, the system kept across a Picture change, the stored value applied at the next start; 6 controls (`c128_sys_*` in `run.sh`).
`videomode.h/.cc`: `VID_SYS_PAL60/NTSC50` and their names; golden `menu_c128.txt` has the new item. The default is PAL, so a host set to NTSC is put back to PAL when the
C128 starts (the core's setting rules). Not checked on the board.

**4. Room.** Run 1079 (not re-run): 27,670 LUT of 32,600, BRAM 75/75.
- *C128 RAM 128 KB into DDR2: NOT built, the budget does not hold.* The VIC reads this RAM every cycle: phi1 of a 1 MHz cycle is 16 clocks of the 31.5 MHz core clock, a
  badline adds 40 c-accesses, sprites 2-3 accesses per sprite and line, glyph/bitmap data is addressed from the c-access of the same line. The DDR2 line fetch measured in
  this repo is 15-27 clocks (T113 5.4: "a miss holds 19 clocks (about 0.6 us)"; "the fetch is 15-27 clocks in the model"), i.e. as long as or longer than phi1 and, in 2 MHz
  mode (a cycle is 16 clocks), longer than the CPU's whole cycle. The ROM cache gets away with it only because the CPU alone waits and the VIC never does (a stalled VIC
  loses its raster phase, T113 5.4/5.5); RAM cannot do that, the VIC needs its data in phi1. Prefetching the VIC's line is a redesign of the VIC fetch path with no margin at
  badlines. The partial form, RAM block 1 only (16 RAMB36), is reachable by the VIC too (VIC bank select). Would free 32 (or 16) RAMB36; risk high; the proof would be the
  C128 boot from DDR2 RAM with the screen RAM compared. A decision for Alex, not built.
- *ESP32 link `i_wifi` (1,126 LUT): kept.* Ethernet is already out and the case buttons (menu, freeze, power off, reboot) arrive over this UART (Q2c); dropping the block needs
  the firmware's ESP driver changed to a button-only path - firmware and board work, not a gateware switch. Frees ~1,126 LUT; risk: the case buttons.
- *Tape `c2n` (814 LUT): kept.* No cheaper path known; `TAPE` is a define in `core.toml` `TRX_DEFS` and drops the datasette completely. Frees ~814 LUT; a feature decision.
- *JTAG debug `dbg` (692 LUT): built behind the flag.* `core.toml` `mono_env` has `TRX_TOP_OFF=NO_JTAG_DBG` (the host's own switch: `jtag_dbg`, the debug registers, the console
  read-out and the JTAG DDR2 port fall away); a dev build that needs `tools/trxdbg.sh` uses `cores/c128/mono/build.sh --jtag`. Bench `c128_variant` (source guards, 2 controls).
  Frees ~690 LUT (run 1079 figure; Vivado decides). Cost: the release core cannot be debugged over JTAG.
- Built: ~690 LUT. Not built (decisions): ~1,126 + ~814 LUT, 16-32 RAMB36.

**5. Menu on the analogue picture (D23) - design, not built.** The room after 4 is ~690 LUT (unmeasured) and the overlay needs ~250 LUT, so it waits for the coordinator's next
synth report. Design: a 40 x 25 character overlay in `c128_avout.v`, in the 15 kHz path before the encoder (the encoder takes the palette index, so text and background cells
substitute palette indices: no second colour path). The font is the C128 character ROM the core already holds (`c128_chr.v`, 8 KB in its 2 tiles; a second read port or time
sharing with the VIC's fetch, no new block RAM); the character buffer is 1000 x 7 bit in distributed RAM (~110 LUT), written by the core app through three registers of page E
(address, data, enable) like `CHR_ADDR/CHR_DATA`. The application renders the menu it already has (menutree/menuctl entries) as text lines into that buffer when the menu opens
and clears it on close; the pixel layer is not involved. Cells 8 x 8 dots of the 400-dot window. Scenarios when built: a fixed buffer and the font give the expected luma at the
cell positions; control: enable off -> the picture unchanged.

**6. T170 check: IEC_SRQ and CIA 1 SP/CNT.** Today they do not reach the CIAs: `cores/c128/c64_top_c128.v:921-923, 978-980` ties both CIAs' `sp_in` and `cnt_in` to 1 and leaves
`sp_out` open; `mono/top.v:728` ties `IEC_SRQ_O` to 0 and has no `IEC_SRQ_I` input (the ball K14 is in `host/c64u.xdc` only; `host/top.v` does not read it either). Fast serial
needs `IEC_SRQ_I` as an input port into CIA 1's `cnt_in`/`sp_in`, CIA 1's `cnt_out`/`sp_out` driving `IEC_SRQ_O`; recorded in T170, not built.

### 2026-10-31, branch t140-d24: D25 - the C128 RAM into the DDR2 (design first; built after this section)

**1. Inventory of the RAM's users (read from the code).** One command per half cycle (16 clocks, `sdram_pending` at the RAS/CAS pulse pair of the kawari VIC, `c64_bus_arbiter_c128.v`),
alternately the VIC's and the CPU's; the data is taken in the clock after the command (`c64_mem_c128.v`), the lane by `mem_raddr_lsb`. Users: (a) the VIC (`vic_write_ab` = AEC low, the last
`else` of the arbiter): c-access, g-access, p/s-access, refresh and idle reads, 16 bit word address `{ram_bank = map_vic_bank, {va15,va14}, 12 bit}`; its data is sampled at `phi_phase_start_dav`
(`bus_access.v`), the address is valid from phase 3/6 of the 16 (`addressgen_spartan.v`: row at `MUX_ROW`, column at `PAL_MUX_COL`) - about 10 clocks from address to sample; (b) the 8502 and the
Z80 (the CPU half; in the 2 MHz mode also the VIC half, `fast_lcyc`), reads and writes, block by the MMU (`map_ram_bank`); (c) the app's DMA bus cycles (`ext_dma_cyc`, the CPU's path); (d) the
second port (`dbg_*`): the app's window (`dma_win_c64u`, in `layer2_c128.v`), the host's text layer / APPLY / JTAG through the host's mux (`host/top.v` `port_req`), read data one clock after the
address; (e) the turbo port `tb_*` (NO_TURBO: tied off); (f) the REU (NO_REU) and the cartridge RAM (not served: reads $FF). The VDC's 64 KiB stay where they are. The ROM stays as it is
(DDR2 behind `c128_romc`, T113 5.4).

**2. Why the VIC cannot fetch on demand.** The address is known about 10 clocks before the sample; the DDR2 line fetch is 15-27 clocks (T113 5.4/5.7: "a miss holds 19 clocks", longest 27). The
dependent chain is worse: the c-access byte is the g-access address of the next half cycle (`addressgen`: `{cb, char_ptr, rc}`), so a text badline has no lead time at all, and a stall of the
VIC loses its raster phase (T113 5.7). What the VIC reads is a function of its registers (`$D011/$D016/$D018`, the sprite registers, CIA 2 bank bits, the MMU's VIC block) and of its own state
(VC, RC, MCBASE), i.e. only a second copy of the VIC's address generator could predict it - rejected: no hard guarantee at FLI/linecrunch/sprite tricks, and the same logic twice.

**3. The options, with the numbers.**

| Option | BRAM tiles kept | VIC guarantee | Verdict |
|---|---|---|---|
| (a) line prefetch from a predicted address | 1-2 | none at badlines (c-access to g-access in 16 clocks), breaks at tricks | rejected |
| (c) fixed DDR2 slot per phi1 with a priority port | 0 | none: the latency, not the slot, is 15-27 > 16 clocks; the host's arbiter cannot be changed (frozen) | rejected |
| (b) write-through mirror of the VIC bank in use | 4 (16 KiB) | hard while the bank stays; a bank switch costs a refill | base of the choice |
| **(b+) bank slots: N mirrors of 16 KiB banks, LRU, filled by a page sweep, demand first** | 4 per slot + 2 (CPU cache) | hard for every bank that is resident: a VIC read of a resident page is the same one-clock BRAM read as before | **chosen, N = 2** |

**4. The design (`c128_ramd.v`, core clock; `c128_ddrx.v`, ui_clk).**
- *Truth is the DDR2* (`RAM_BASE` byte 0x01C20000, 128 KiB, next to the ROM image at 0x01C00000, below the frame buffers at 0x01D00000). Every RAM write goes through: slot BRAM (if its bank is a slot,
  whatever the page's state), CPU cache line (if present, byte-updated), and an 8 entry write FIFO to the DDR2 (`e_*` write, byte mask, in order). The CPU is held (RDY / Z80 enable) at 5 entries;
  a read of the DDR2 starts only when the FIFO is empty and no write is open, so a fetch never returns a byte older than a write already given. A write to the line being fetched or copied marks the
  fetch dirty: it is dropped (cache) or fetched again (slot).
- *VIC reads* come from the slots only, the clock after the command, exactly as the block RAM gave them. A VIC read whose bank is not resident, or whose 256 byte page is not valid yet, gets $FF and
  is counted (`vic_miss`); nothing waits, the raster is never touched. **The guarantee, stated exactly:** the VIC reads the byte the BRAM build would read whenever its page is resident-valid. A
  bank becomes a slot when `{map_vic_bank, va15, va14}` (CIA 2 PA0/1 with the DDR, the same bits the arbiter forms its address from) has been stable for 4 clocks; the sweep then fetches its 64 pages
  (a page = 16 lines of 16 bytes, demand pages first - the page of the last missed VIC read -, then in address order). Failures, all counted: the first touch of a bank (up to one bank sweep
  = 1,024 lines at about 22 clocks = 0.7 ms = 11 raster lines, a page about 11 us), three banks used in rotation with N = 2 (each switch to the evicted bank refills), and the one-access CAS
  glitch of a bank switch 1 <-> 2 (`va_delay`): that access reads bank 3, which is no slot, and gets $FF where the block RAM build read bank 3's byte. Commands never allocate a slot (the glitch
  would evict one); only the stable bank bits do.
- *CPU reads*: slot hit (page valid) or cache hit = the same one-clock answer; otherwise `rom_miss`'s mechanism: the miss pulse holds the CPU (RDY low, the Z80's enable) and it repeats the read
  in its next cycle (T113 5.7); the cache line is fetched meanwhile. The cache is direct mapped, 512 lines of 16 bytes (8 KiB, 2 RAMB36, the ROM cache's scheme, index xor top tag bits), byte-updated by writes.
  A CPU miss is a lost cycle: **cycle-exact code that is not cache- or slot-resident can slip by the held cycles**; code in a resident bank (bank 0 is almost always one) does not.
- *The second port* (window, text layer, APPLY, JTAG) becomes a one-job sequencer in the quiet clocks between two commands (4 clocks after a command, ready long before the next one 16 clocks later);
  a read of a non-resident byte starts the cache fetch and is repeated. The app's window is forked (`dma_win_c128.v`, `port_ack`) so it waits for the answer; the other users of the
  host's mux keep the fixed one-clock timing and get the last answer on a miss (they are the C64 line's; the C128 app uses the window only).
- *DDR2 engine*: one command open at a time on the host's `e_*` client (ROM line fetch, RAM line fetch, RAM write), `c128_ddrx.v` replaces `c128_rome.v` (toggle handshakes as before); the VDC frame
  writer waits while it owns the port, as it did for the ROM.

**5. Budget with the repository's numbers.** Core clock 31.5 MHz: phi1 = 16 clocks, a command per 16 clocks, a CPU cycle 32 (16 at 2 MHz). DDR2 line read 15-27 clocks (T113 5.4, sim 293-693 ns at
ui_clk 75 MHz plus 4-6 clocks of toggle synchronisers); write accepted in about 12 clocks (command, `e_rdy`, back). Per half cycle the memory port carries at most one RAM access, so one
BRAM read port per structure suffices. Write rate: at most one RAM write per CPU cycle, typically one per 3-6 cycles; the engine retires one per ~14 clocks, the FIFO (8) absorbs the bursts
of 3 (JSR, interrupt entry, RMW). Page fill: 16 lines x (22 fetch + 8 copy, the copy overlaps nothing in this version) = about 480 clocks = 15 us. **Room:** 32 RAMB36 (run 1079) become 2 slots x 4 + cache 2 = 10,
**22 tiles freed**; 4 slots (block 0 whole) would free 14 and make the VIC hard-safe for all four banks of a block. LUT: about +600 (FSM, tags, 2 x 64 valid bits, FIFO, comparators, `c128_ddrx`), from the
sources' size; Vivado decides. The BRAM build stays selectable (`RAM_DDR2` undefined) so every bench can compare the two.

**6. What the proof must show** (benches in 5 below, each with a control that fails): the machine boots to the same screen from both builds (VIC pixel hash of several frames, VDC frame, screen RAM);
C64 mode boots; a VIC stress with badlines, 8 sprites, a CIA 2 bank switch in mid frame and a raster IRQ whose cycle count equals the BRAM build's; the same under the slowest DDR2 the model has
(latency injection); with the sweep disabled the picture must differ (control).

**7. D25 hand-over (agent stopped on the coordinator's order; WIP).**
- *Built, benched:* `cores/c128/c128_ramd.v` (2 VIC bank slots + 8 KiB CPU cache + 8 entry write FIFO + port-job sequencer) and `c128_ddrx.v` (ROM fetch / RAM fetch / RAM write on `e_*`). Unit bench `sim/run_ramd.sh` (iverilog, `tb_ramd.v`): PASS at model latency and at 52..92 ui clocks; six controls fail as they must (NOSWEEP, NOWT, NOCUPD, NODIRTY, CMDALLOC, NOWAITFIFO). Measured there: a bank sweep takes ~34k core clocks (~1.07 ms, ~33 clocks per 16 byte line: fetch and 8 copy clocks are not overlapped); a page ~17 us; after the sweep and with CPU writes into the bank, 0 VIC misses and 0 wrong bytes; two resident banks alternate without a miss; a glitch command does not allocate. (The estimate of 0.7 ms in 5 was too low: 1.07 ms, ~17 raster lines for the first touch of a bank.)
- *Wired (WIP commits 5a36267aa and c828cb1a6; compiled and run by the next agent, see 8):* `c64_mem_c128d.v` (selected by `RAM_DDR2`), arbiter outputs `ram_vic` / `vic_bank_tag`, CPU hold `ram_hold` in `c64_top_c128.v` (also a missed DMA-window bus read is repeated), `c64_core_c128.v` / `layer2_c128.v` ports and `c128_ddrx` instead of `c128_rome`, `dma_win_c128.v` (waits for `port_ack`), `core.toml` TRX_DEFS has `RAM_DDR2`, bench plumbing (`tb_c128.v`, `mk_tb_ports.py`, `build_machine.sh`). `ramd` runs on the ungated clock so the window and the FIFO work while the machine is stopped.
- *Written (run in 8):* `sim/run_ramddr.sh` (bram vs ddr build of the same program: pixel hash of frame 10, CIA-timed loop count, VIC misses after frame 10, lost writes; slow-DDR2 run; controls NOSWEEP and NOWT; five machine programs) and program `vicstress` in `tools/gen_c128_test.py` (bitmap + 8 sprites, banks 0/1, CIA 2 switches at lines 96/176; generator output checked, program not run). Registered in 8.
- *Next step (as of that agent, done in 8 except Vivado):* on the builder (no GITEA containers): rsync the worktree, `sh cores/c128/sim/run_ramddr.sh` in `trx64-sim-env`; fix compile errors of the wiring first (expect port/typing slips); then register `c128_ramddr` in the three `ci/` tables; real-ROM BASIC 7.0 boot compare with `run_realrom.sh` and `C128_SIM_DEFS` incl. `RAM_DDR2` (local ROMs). Then Vivado (coordinator): expected 22 RAMB36 freed (32 -> 2 x 4 + 2), LUT about +600 (estimate); the counters went into `c128_regs.v` in 8 (core app display still open).
- *Known limits to decide:* first touch of a bank and 3 banks in rotation give $FF reads (counted); CPU cache misses hold the CPU (cycle-exact code outside resident banks slips); the other users of the host's port mux (text layer, APPLY, JTAG) get best-effort data on a miss; VIC fetch of the CAS-glitch access reads $FF instead of bank 3. NSLOT = 4 (whole block 0) would free 14 tiles and remove the first-touch case for block 0.
- *D24 open proofs not finished:* `tests/hostapp/run.sh` ran green; `tests/coreapp/run.sh` stopped at control `desc_one_form` ("mutant does not build", undefined `*_desc` symbols of snes/neogeo/ddt cores, cause not found, not shown to come from this branch - rerun, then check on main); `ci/trxapps.sh` and `tools/prepush.sh` not run.

**8. D27 (coordinator): the whole RAM block 0 pinned in block RAM (2026-10-31, second agent).** `c128_ramd` has `PIN` (the C128 instance is `NSLOT = 4, PIN = 1`): slot i is
bank i of block 0 for good (tags 0..3), no allocation, no LRU decision, swept once after configuration (4 x ~34k core clocks = ~4.3 ms at start, the VIC reads $FF in that time and it is
counted). 16 RAMB36 for the slots + 2 for the CPU cache = 18, so 14 freed of the 32 (estimate; Vivado decides). A VIC read of block 1 (MMU VIC block bit, C128 mode, never used by the
KERNAL) answers $FF and is counted in `n_vic_blk1` (and `n_vic_miss`); making it correct would need a second set of slots or a DDR2 fetch inside phi1, neither is cheap, so it is a stated limit.
The CAS glitch of a bank switch reads another bank of block 0 and is now answered correctly (it is a slot). Unit bench `run_ramd.sh` has the pinned variant (`-DPIN`: P1 four banks
rotated without a miss, P2 glitch plus CPU writes into all banks, P3 block 1 counted; at model latency, at 52..92 ui clocks; controls NOSWEEP and NOWT fail).
- *Wiring compiled and run (builder, `trx64-sim-env:4`, Verilator 5.053).* The previous agent's wiring (`RAM_DDR2`, `c64_mem_c128d.v`, `ram_vic`/`vic_bank_tag`, `ram_hold`, `c128_ddrx` in `layer2_c128.v`, `core.toml`)
  compiled at once. `sh cores/c128/sim/run_ramddr.sh`, all modes, PASS (`c128_ramddr` in `ci/sims.txt`/`sims.lane`/`sims.deps`):
  `bram: hash 2881c70d, loop f9eb, program PASS` / `ddr: hash 2881c70d, loop f9eb, VIC misses 51 at the end = 51 after frame 10, writes lost 0, program PASS` (checks 1-4 PASS);
  slow DDR2 (reads 90..150 ui clocks): same hash, 98 = 98 misses, 0 lost; controls: NOSWEEP differs (hash a4e4653e, 23,363 VIC misses), NOWT differs (08dad377); machine programs boot, mmu, fast,
  c64mode, z80back PASS with the RAM in the DDR2. The 51 misses are the start: the pinned slots are swept once (~4.3 ms), the VIC reads $FF meanwhile; none after.
- *What had to change in the proof, and why.* (a) `vicstress` did not finish its setup in 260 ms at 1 MHz (signature $A5 never written, in the block RAM build too): the setup now runs at 2 MHz ($D030 = 1,
  display off) and takes ~160 ms. (b) The first hash differed (49f1389a against 36326cd3). `+PIXLINES=1` (one hash per line) showed that exactly one line differed: 176. The program polls `$D012` and writes
  CIA 2 when it sees line 96 / 176, so the cycle of that line at which the bank flips depends on the cycles the CPU lost before (a cache miss holds it): a measured property of the test program, not of the RAM.
  Lines 94..99 and 174..179 are now left out of the hash (`+PIXSKIP_LO/_HI`, `+PIXSKIP2_LO/_HI`, `C128_SKIP`); all other lines and everything the banks show from the next line on must be identical, and the
  NOWT/NOSWEEP controls still differ. The loop count (check 2) is equal in the block RAM and the DDR2 build (f9eb): once the code is resident the CPU is not slowed; with the slow DDR2 it is not asked (f996).
- *Counters in the register page.* `c128_regs.v` 0C0-0C9 (16 bit each): VIC reads answered $FF, CPU reads held, DDR2 line reads, writes lost (must be 0), VIC reads of block 1; replaces the 64 `c128_keep` LUT1 that
  held the counters; bench `c128_regs` reads them. The core application does not show them yet.
- *Limits, after D27.* Block 1 for the VIC (MMU) reads $FF (counted); the first ~4.3 ms after configuration the VIC reads $FF (display off: DDR2 content of the previous core is not cleared - the C128 RAM at
  power-on is whatever the DDR2 held, KERNAL/BASIC initialise what they use, to be checked on the board); CPU misses outside the caches hold the CPU; the host-mux users of the second port (text layer, APPLY, JTAG)
  get best-effort data on a miss. Real-ROM boot compare (`run_realrom.sh` with `RAM_DDR2`) needs the local ROMs and was not run. Vivado: not run (coordinator).
- *Hand-over (second agent, stopped on the coordinator's order after ~2.5 h).* Compiles and passes on the builder (`tools/benches.sh c128`, HEAD 4cbb9b77c): c128_machine, keys, pause, vdc, pixw, romc, ramd (253 s), ramddr (754 s),
  z80ba, reset, cold, phase, chr, regs, variant PASS; c128_scan PASS after PROVENANCE named the new files; c128_nonet "FAIL (exit 1) in 0 s" is the same on main with `-f` (the bench clone has no apps/ultimate submodule;
  main shows "cached PASS" from CI) - not this branch. Application tests in `ci/trxapps.sh` (u64-build-env:ci, own clone plus the submodule): apptests hostapp, coreapp, loader PASS (425 s / 1,810 s / 23 s); then the host app
  render bench failed with `render_nolock: No such file` - the branch lacked main's af5306d4f (`build_now`); cherry-picked as eb4a2f299. The rerun (render benches, then the RISC-V builds of host app and core app, `ci/trxapps.sh`)
  was stopped at the apptests: NOT PROVEN for the RISC-V build. On the Mac `tests/coreapp/run.sh` aborts at control romreq_stale (BSD sed has no `0,/re/`), the same on main; run it on the builder only.
  `tools/prepush.sh` not run. *Next step:* on the builder rebuild a clone of HEAD with apps/ultimate (+ software/lwip, httpd), run `ci/trxapps.sh` with CI_HANDOFF, CI_APPTESTS_OK and CI_TRXAPPS_CACHE on a mounted
  directory (so a rerun is cached); then `tools/prepush.sh`; then Vivado (coordinator): expect ~14 RAMB36 freed, LUT about +600 minus the 64 keep LUTs. Also open: core app display of 0C0-0C9, real-ROM boot compare (local ROMs).

**9. Third agent (2026-11-01): merge of main, proof, and what the merge of t164 needs.** `git merge main` into `t140-d24` (conflict only in this spec: main's D26 (Alex, one branch, then scaler/HDMI) keeps its number, our pin decision is D27 now).
Proven on the builder from a clone of the merge commit (the submodule at its pin 32e5fd5 plus lwip/httpd copied in): `ci/trxapps.sh` in `u64-build-env:ci` rc=0 in 30 min - apptests hostapp, coreapp, loader PASS (230 s / 589 s / 10 s; these are the whole `tests/hostapp/run.sh`, `tests/coreapp/run.sh`),
render benches PASS (753 s), fs64 benches PASS, RISC-V builds of host app (845,984 byte `.app`) and core app (1,545,816 byte `.app`, size check ok). The c128 sim lines of `ci/sims.txt`, run by their scripts: machine, keys, pause, scan, vdc, pixw, romc, ramd, ramddr, z80ba,
reset, cold, phase, chr, regs, avout, avpins, variant all rc=0; `c128_nonet` fails on this branch and on main alike (git "dubious ownership" of the bench clone). `tools/prepush.sh`: lint ok for df07f4dc.
*What the merge of t164 (774497b1d) needs.* A trial `git merge-tree` of t164 into this branch: text conflicts only in `coreapp/src/hw.cc` and `coreapp/src/picture.h` (the C128 analogue/System Mode additions against t164's HDMI mode work in the core app); `cores/c128/mono/build_mono.tcl`,
`build.sh` and `top.v` merge without a conflict. t164 gives the core `cores/c128/mono/host/` (47 host files, `FROM`: taken from host/ at aa45528c2, hdmi_timing fix in): `build_mono.tcl` reads every host file as `$hd/<file>` (`set hd ../cores/c128/mono/host`) and adds `trx_gain.v`; a host fix reaches the core only by copying it in
and updating `FROM` (Alex, T164 3a Q1: "Copies instead."). Consequences for D26/D23: (a) any new host-file read of this branch's `build_mono.tcl` must be written `$hd/...` (today none is left bare after the merge); (b) t164's `top.v` still ties `vic_scaler` to a fixed mode (`.ce(1'b1), .hact(1920), .vact(1080), .xact0(768), .yact0(45)`,
comment "T164 WP8 connects the mode"): the HDMI modes and the scaler of T107 D11 are this core's job - the mode input has to come from the host's mode register as in the other large cores, and the 4x/4.5x scale numbers need the C128 picture (VIC 40 columns and VDC 80 columns, not the C64 geometry); (c) the overlay stub (`mono/stubs/overlay_stub.v`)
replaces `$hd/overlay.v`, so the menu on the HDMI picture (not only the analogue one of D23) needs the real `overlay.v` back (3.5 RAMB36; budget after D27 to be read from the Vivado run); (d) `cores/c128/sim/run_avout.sh` reads `host/pal_ram.v`, the copy `mono/host/pal_ram.v` is the one the build uses (t164 made the ported-tree scanners skip `mono/host`); (e) the C128 flat build's cache key must not move on `host/` any more, `ci/plan.py` follows the copies (t164 dca931e53).

**10. t164 merge (2026-11-01, branch t164-c128).** `t140-d24` merged into `t164` and the C128 put on the T164 host copy (T164 spec §5 "WP18 the C128 onto the host copy"): the HDMI side is now `hdmi_out` of the A500's copy
(modes, 1080p VIC-locked rows with `c64_type` 1, scaler, volume, `esp_ctl`, port clock); `mono/top.v` keeps only the A/V port (`c128_avout`, `c128_avpins`, DAC clock); the Ethernet cut is `NO_NET`
(`mono/ultimate_sys.vhd` and its diff are gone; the WiFi UART stays, Q2c); `c128_nonet` tests the copy with `g_net = 0`. D26's "scaler and HDMI modes" is therefore in the code; Vivado and the board are open.
*Per decision, what is still open (list made 2026-11-01 against the code):*
- **D23 menu on the analogue picture:** not built (section 5 above is a design). Just work once the Vivado run gives room (about 250 LUT, 1000 x 7 bit distributed RAM, three page-E registers, an application renderer); the design is Alex's decided D23 + section 5.
- **D24 items 1, 2, 3, 4:** built and benched (pins, PAL V sign, System Mode op + `picture_set_system`, JTAG flag). Open: board check of the three on the Sony (Alex/testers).
- **D24 room beyond JTAG:** ESP link and tape are NOT cut (no decision to cut); with D25/D27 the estimate is 61 of 75 RAMB36, about 28,000 LUT (T164 spec §5 WP18 c128). Design decision needed only if Vivado shows it does not fit.
- **D25/D27 RAM in the DDR2:** built and benched; Vivado open (freed tiles are an estimate). Just work: the core application does not show the counters 0C0-0C9 yet; the real-ROM boot compare (`run_realrom.sh` with `RAM_DDR2`) needs the local ROMs (Alex or a tester runs it). Stated limits stay (block 1 for the VIC reads $FF; first ~4.3 ms after configuration).
- **D26:** HDMI modes and scaler: in the code with this merge (1080p locked rows; the 4x/4.5x numbers are the C64 scaler's, and the 80-column VDC picture goes through the pixel layer as before). Not proven by a bench that elaborates the top (none exists); Vivado run and board photo open. Design decision needed: whether the real `overlay.v` (3.5 RAMB36) should return for a menu on the HDMI picture in place of the stub (today the menu is drawn on the pixel layer).
- **T170 (nothing built):** just work after room is known: route `IEC_SRQ_I` (new port in `mono/top.v`, the ball K14 is in `host/c64u.xdc`) to CIA 1 `cnt_in` / `sp_in` and CIA 1 `cnt_out` / `sp_out` to `IEC_SRQ_O` (today `c64_top_c128.v` ties `sp_in` / `cnt_in` to 1 and the top ties `IEC_SRQ_O` to 0). Design decision needed: the bus log of T170 D2 (record format, ring size, where it is stored and who reads it: block RAM is the scarce part; the bug report file of T162). Board proof is the testers' (D2): checklists T122 / T166 not written yet.

**11. C128 black picture on the board (2026-11-02, branch t164-c128pic; package t164-c83a83c2, run 1139).** Symptom (Alex): HDMI signal, menu and settings work, the C128 picture is black, no message.
- *Cause (found by reading and bench, not yet by a board read):* `c128_ramd` (D25/D27) starts its sweep of the pinned RAM with the first clock after configuration (`s_act` = all ones, `s_pv` = 0, ungated clock `dbg_clk`, no reset, no timeout: `F_WAIT` waits for `m_done_tgl`
  for ever). The DDR2 side `c128_ddrx` is held in reset by `ui_rst || !ddr_calib` (`layer2_c128.v`), and its reset branch resynchronised the request synchronisers to the level of the request toggle and cleared the `pend` flags: the first request, made while the controller
  was still calibrating (milliseconds), was forgotten. `c128_ramd` stays in `F_WAIT` for good: no CPU line miss can be served (`cm_req` waits for `F_IDLE`), the CPU stops at its first RAM read, the VIC reads $FF from slots whose pages are never valid, the VIC registers keep
  their reset values (display off, border and background colour 0): a black picture on the scaler path, and the same machine does not draw the VDC either. The application sees nothing wrong (the ROM is found and written, the release is done). `tb_c128` drove `.ui_rst(1'b0)`, so no bench
  ever had the reset; this package is the first board contact of D25/D27 (section 10 above: "Vivado open").
- *HDMI side exonerated by reading:* `cores/c128/mono/host` differs from the A500's copy only in the `trx_` renames, the two `$readmemh` paths of `overlay.v` and `FROM`; `hdmi_out` is connected port for port in `mono/top.v` (script check: nothing missing); the scaler path
  is the VIC pixels, `c64_halted` and the locked rows only, and a lost lock still shows the ring (`s0_win` does not depend on `locked`). Black from the scaler needs an all-zero ring (VIC stream black), `blank` or an unlocked row, none of which the HDMI files can cause.
- *Fix:* `c128_ddrx.v`: in reset the synchronisers run on and the `pend` flags are kept; a command open at the reset (S_ISSUE, S_WAIT) is asked again. Mutation `DDRX_MUT_SWALLOW` is the old behaviour.
- *Bench:* `cores/c128/sim/run_ramddr.sh` modes `calib` and `ctl_swallow`; `tb_c128.v` `+UIRST_US=<n>` (ddrx in reset for n us from time 0). Builder, `trx64-sim-env:4`: `ctl_swallow: program boot FAIL, DDR2 line fetches 1` (control fails as it must); `calib: program boot PASS, DDR2 line fetches 4113` (4096 are the sweep).
- *Board read that decides it:* the core application's console line `[c128] rom: hits H, fetches F, ...` (every 2 s while a counter moves; the C128 page E is not readable without JTAG). With this cause `hits` stays 0 (the CPU never fetches from the ROM cache); with the fix it grows within a second of the release. A `--jtag` build also shows the RAM counters 0C0-0C9 (`n_fetch` 0 or 1 = this cause).

**12. D28 (2026-11-05, branch t140-d28): the fifth VIC slot for RAM block 1 (WIP: the robots `nodyn` control and a final rerun of run_vicblk on the last source were still open).**
`c128_ramd` `NSLOT = 5, PIN = 1, NDYN = 1` (`c64_mem_c128d.v`): slots 0-3 pinned to block 0, slot 4 allocatable. The VIC's selection `vic_tag` ({MMU RCR bit 6, VA15, VA14}) is the early trigger: a bank that is no slot, stable for 4 clocks, takes slot 4 (victim only among the NDYN last slots), the sweep starts with the page the VIC missed first; fallback: the VIC's first read of a block 1 bank while slot 4 was never taken. CPU writes go through `wr_o = o_wr && m[g]` as in the pinned slots; each slot RAM keeps one write port. A miss answers $FF and is counted in `n_vic_blk1`. `SW`/ages widened for 5 slots. Mutations `RAMD_MUT_NOEARLY`, `RAMD_MUT_NODYN`.
- *Unit bench (builder, trx64-sim-env:4) `run_ramd.sh`:* new `-DDYN` variant (P3/P4: selection alone takes slot 4 20 clocks later; no VIC miss and no wrong word after the sweep with CPU writes into the bank; glitch does not move it; bank 5 -> 7 re-sweeps, 507 reads answered $FF and counted, 0 wrong); `dyn| PASS tb_ramd`, slow DDR2 PASS; controls fail: NOSWEEP (12 FAIL), NOWT (310), NOEARLY (4: "selecting block 1 bank 1 did not take slot 4").
- *`run_vicblk.sh` (ddr, frame 10):* block 0 `b51521dc`, block 1 `b51521dc` = bram; `vicchk|259466|91|76168|36`: 36 of 76,168 block-1 reads wrong, `vicwin|slot taken 185825.3 us|first blk1 read 185825.4|first wrong 185825.4|last wrong 185865.9|window 40.6 us`. Control nodyn: block 1 `38515a6d` (76,168 of 76,168 wrong), as in D27. Control noearly: 36 wrong, window 40.6 us, i.e. NOT larger.
- *Finding on the early trigger:* the VIC reads in every phi1, so its first block 1 read comes 0.1 us after the selection; the early trigger and the first-read fallback give the same residual in both machine benches (vicblk 36 = 36, robots 37 = 37). The machine control therefore only shows `noearly >= ddr`; the unit bench P3 (selection without a VIC read) is where NOEARLY fails. The residual is the sweep window of the demanded pages, 40-46 us, not 1 ms.
- *`run_robots.sh cmp` (real ROMs from outside, frame 150):* bram `d46c23df`, ddr `d46c23df` (equal); ddr `vicchk|5736849|86|4138128|37`, `vicwin|slot taken 2507764.5|first blk1 read 2507764.6|first wrong 2507764.6|last wrong 2507810.2|window 45.6 us`; noearly `d46c23df`, 37 wrong. nodyn (D27 build, expected `dfd8e8c9`) was still running when this was written.
- *`run_vicrom.sh`:* unchanged result, one hash `051b75f1` per program in bram and ddr, `PASS run_vicrom` (14.5 min with builds).
- *Registration:* `c128_vicblk` and `c128_vicrom` in `ci/sims.txt`/`sims.lane`/`sims.deps` (timeouts 3600) and out of `ci/sims_excluded.txt`; `run_robots.sh` stays excluded (needs ROMs and the game image). `ci/lint.sh` not run.
- *Vivado (builder, vivado-env:2024.1, `cores/c128/mono/build.sh`, source of this branch):* route complete, `WNS 0.037 ns` (timing.rpt), Slice LUTs 27,374 of 32,600 (83.97 %), Slice 8,119 of 8,150 (99.62 %), Block RAM Tile 72.5 of 75 (RAMB36 60, RAMB18 25), `ramd`: RAMB36 20 (4 per slot), RAMB18 17, LUT 1,163. Two tiles and a half are left; the slice count is at the limit. A first attempt with `docker run --user` hung after synthesis (futex wait, 0 % CPU); the run as root with `--init`, as the CI does, went through.


**13. D29 (2026-11-06, branch c128-d28ovl): block RAM pinned for the two memories Vivado moved to LUTRAM in the D28 build.**
- *Change:* `pages` (`cores/c128/mono/host/ultimate_sys.vhd`, 2048 x 8, one write port, read address = write address, registered read) gets `ram_style "block"`; the UART receive FIFO (`sync_fifo` in `uart_dma.vhd`, 511 x 8, one write, own read pointer, `g_storage_xil` was "auto") gets `g_storage_xil => "block"` through a copy of `uart_dma.vhd` that `build_mono.tcl` writes into the build directory (`vendor/gideon` and the host stay as they are). The sid waveform memory is not touched.
- *Why LUTRAM before:* neither RAM has a second write port or a differing write address, so nothing in the code forces LUTRAM; "auto" let Vivado choose LUTRAM at 72.5 of 75 tiles. The attribute is the whole fix.
- *Vivado (builder, vivado-env:2024.1, `cores/c128/mono/build.sh`, HEAD 41da4d374):* route complete (`nets with routing errors: 0`), `WNS 0.228 ns`, `WHS 0.053 ns` (timing.rpt), Slice LUTs 27,109 (83.16 %), Slice 8,122 of 8,150 (99.66 %), Block RAM Tile 72.5 of 75, RAMB36 60, RAMB18 25 (the same tile count as D28). `utilization_hier.rpt`: `ult` (`pages`) RAMB18 3 / LUTRAM 532 as in D27 (D28: 1 / 884); `i_rxfifo` RAMB18 1 / LUTRAM 0 as in D27 (D28: 0 / 96); log `ult/pages_reg` and `ult/i_wifi/i_rxfifo/data_array_reg` "implemented as a Block RAM".
- *Side effect:* the pressure moved on. The sid waveform memory is now RAMB18 0 and 703 LUT (D27: 4 and 444; D28: 2 and 568); the tile total did not move. Not touched (clean room), so whether it matters for the menu is open until the board says.
- *timing.rpt D27 vs D28 (read, same constraint set):* identical check_timing counts (0 no_clock, 0 unconstrained internal, 0 loops, 0 generated-clock issues), same clock list, same inter-clock pairs (pix to HDMI TXOUTCLK, sys to sid2x and back); no CDC path is new. What changed is slack and endpoint count: cpu_mmcm WNS 0.300 to 0.078 (21,089 to 24,365 endpoints), sys_mmcm 3.718 to 2.760, sys to sid2x 1.363 to 0.683, pix_mmcm 0.053 to 0.037; the D29 build has WNS 0.228.

**14. D30 (2026-11-07, branch c128-d28ovl): the bank-flip hypothesis for the one wrong character row, measured on the title/loader phase (WIP, NOT decided).**
- *Bench:* `tb_c128.v` +VICEVT (bench only: `vtag`, `take`, `full`, `evsum`, `evbad` lines per slot-4 take, wrong block-1 reads by cycle type / badline / line); `run_robots.sh ddr 16000` with real ROMs (VICE C128 dir), `C128_ARGS=+VICEVT`, 802 frames (13.5 s of game time after the inject at 2.5 s).
- *Result (the game as `run_robots` starts it: it stops in the level load, "SEARCHING FOR LEVEL-A-128" because +NODEV):* `take|2|2507764.5 us|frame 125 line 265 x 209|slot4 tag 100 -> 111`, `full|take 2|sweep complete 1002.4 us after the take (0.05 frames)`, `evbad|... line 265 ... ctype 4 badline 0` (g-access, bottom border, no badline), `evsum|take 2|tag 111|... wrong blk1 37 (c 0 g 29 other 8)`, `vicchk|15584824|86|13986103|37`, `vicwin|... window 45.6 us`, `vicevt|takes 2|end 16000000.0 us|frame 802`. Only ONE take after the start in 802 frames: `vtag` changes only at frame 125 (000 -> 011 -> 111), the VIC bank never flips afterwards. The 37 wrong reads are 0 c-accesses, 0 on a badline.
- *So:* in this phase the hypothesis (repeated flips, wrong badline row) is NOT reproduced: no repeat, no badline. In-game is not covered. A full sweep takes ~1.0 ms, so a flip would give ~1 ms of wrong demanded pages (not 40 us) unless the flip lands on the pages already swept.
- *Code reading (GAME1):* the game writes `$D506` only as $41/$45 (same VIC block bit 6, same bank) and has no `STA $DD00` outside the loader; so no bank flip by those stores, a flip through other writes is open.
- *Tooling for the in-game run (committed, not yet run to the end):* `C128_LEVEL=LEVEL-A-128` (mk_inject.py) puts the level at block 1 $9D00 and replaces the `JSR $FFD5` at GAME1 $564B by CLC,NOP,NOP. The run (`ddr 10000`) was stopped at frame 125 by the 4 h limit.
- *Not examined:* stock-mode load stop (sim has no drive, +NODEV); fix costs (see handover message, estimates only).

**15. D31 (2026-11-07, branch c128-iovic): the IO path against the VIC's fetches - read only, WIP, NOTHING measured yet.**
- *Drive on the board:* `drive_unit.vhd` (Gideon's 1541 6502 + app side `d1541_wd.cc` for the 1581; image, ROM, RAM in the DDR2) = `mig_arb` client 2 (`cores/c128/mono/top.v` arb instance, `cores/common/drive_mem.vhd`). The C128 RAM/ROM use the engine client `e_*` (`c128_ddrx.v`).
- *Path table (read from code):*

| Requester | Port | Priority | Wait for the VIC |
|---|---|---|---|
| cart_fill | `c_*` | 1 | none: VIC never uses it |
| drive A (disk bytes, ROM, WD regs) | client 2, `drive_mem.vhd` | 2 (`pending[2]` blocks eng and video: `mono/host/ddr2_port.v:150-154`) | none: VIC does not wait on the DDR2 |
| engine = C128 RAM line fetch, RAM write, ROM line (`c128_ddrx.v:75-95`) | `e_*` | 3 (`ddr2_port.v:151`) | delays CPU holds and slot sweeps only |
| video (pix_layer), RISC-V, REU/JTAG | v_*, client 0/1 | below | - |
| VIC c/g/p/s/refresh | slot BRAM `c128_ramd.v:133-149,188-189`, answer next clock | no DDR2 at all | late data does not exist: valid page = BRAM word, else $FF (`c128_ramd.v:189`), counted `:355-359` |

- *Reading result:* the arbiter issues one command per ui_clk without waiting for read data, so the drive delays the engine by a few ui clocks plus queued read latency, not by its 670 ns. The VIC gets wrong data only from (1) an invalid page / non-resident bank answered $FF (slot 4 take or re-take, `c128_ramd.v:296-311,350-353`, sweep window 40-46 us measured in D28/D30; a row of mostly $FF characters on a badline fits picture `b_199_201.png`), or (2) a stale slot. Longer DDR2 latency only widens (1) when a take happens. The unit/machine benches with 52..92 ui clock latency (D25) already show 0 wrong reads.
- *Candidate for (a), not proven:* the stock KERNAL serial routines (`E573`/`E59F`, ACPTR `E43E`) run SEI, save/clear $D015 and $D030 and rewrite CIA 2 $DD00 per byte; a bank/RCR change during the load would re-take slot 4 and give the $FF row. The sim has no device (+NODEV), so this path never ran. `tb_c128.v` has only a stub device (DATA low under ATN). Needed: a bus-level serial device in the bench serving LEVEL-A-128 so the game's own `JSR $FFD5` runs with the display on, +VICEVT/+VICCHK logging `vtag`/`take`.
- *Other finding (separate):* fast serial is not wired (T170, item 6): CIA 1 SP/CNT tied high (`c64_top_c128.v:924-927,981-984`), `IEC_SRQ_O = 0` (`mono/top.v:740`); the KERNAL's ACPTR takes the fast byte when `$DC0D` bit 3 is set (`E486-E4B5`), so a 1571/1581 burst cannot complete: candidate for "stock mode load stops".
- *Not done:* bench with the drive path, traffic run, negative control, fix. No sim result exists for this item. Builder scratch `~/scratch/vicglitch` (synced tree, ROMs) left in place.
- *Measured (2026-11-07, build `ddr`, `tb_c128.v` +DEV8 +IOLOG +VICEVT, `run_robots.sh` with `C128_DEV=LEVEL-A-128 C128_DEVLEN=2048`, no `C128_LEVEL`, so the game's own `JSR $FFD5` runs):*
  - *Device:* bus-level device 8 in `tb_c128.v` (`dv_*`, armed at the inject time; before that the line is empty as in +NODEV so the boot attempt of 1.5 s ends as before). Followed from the C128 KERNAL (318020-05): LISTEN/CIOUT `$E343-$E412` (device pulls DATA low at ATN, releases it when CLK is released, 8 bits LSB first, frame acknowledge = DATA low after the 8th bit, EOI = CLK held high until the host's DATA pulse), TKSA `$E4E0-$E4F5` (turnaround: host holds DATA low, device pulls CLK low), ACPTR `$E43E-$E4CE` (CLK high = ready, host releases DATA, 13-pass EOI window, bits sampled on CLK high), UNLSN `$E515`. Bit timing is the device's own: 30 us pulses were lost (a badline holds the 8502 up to ~43 us; the first run stalled in ACPTR), 70 us data setup + 70 us CLK high works (about 1.6 ms per byte). The file is truncated to 2048 bytes of the 8962 (the sim runs about 1 simulated second per 10 minutes); LISTEN 8 / `$F0` / name `LEVEL-A-128` / UNLISTEN / TALK 8 / `$60` is the game's sequence (it also sends a command-channel string `U0...` first).
  - *Result with the device:* the LOAD runs: TALK at 2796.3 ms, 1862 of 2048 bytes at the 5.8 s end of the run (stream still going), 3800 `$D030` writes (2 per byte, `$FC`/`$00`, bit 0 stays 0), `$DD00` 6538 writes. Slot takes and vtag changes during the load: **none** (the 2 takes and 2 vtags are all at 2507.2-2507.8 ms = the inject). VIC reads wrong in block 1: 37 in total, all inside 2507764.6-2507810.2 us (the inject's take, window 45.6 us, as in D30); **0 wrong reads in the load window** (2796 ms to 5800 ms, ~3.4M VIC reads in block 1).
  - *Control (+NODEV, same binary, same arguments, 3.6 s):* no device, no transfer (the game ends in its error path); the same 2 takes, the same 37 wrong reads at the same time (`vicchk|2730932|86|1132211|37`), 0 afterwards. So the load adds nothing in this path.
  - *What the load writes (`+IOLOG`, first changes with time):* `$DD00` bank bits (1..0): no change after the inject (c7 -> 94 at 2507.2 ms); `$D506` 05 -> 45 at 2507.8 ms, then 45 -> 00 at 2611.8 ms (before the load); `$D011` 3b -> 00 at 2587.7 ms and `$D018` 80 -> 00 at 2587.8 ms: **the game blanks its own screen (DEN off) and sets RCR 0 before it calls LOAD**. During the load only `$D030` changes. KERNAL serial writes to `$DD00` change bits 3..5 only.
  - *Verdict:* for this path (the game's level load, device present, 1862 bytes) the slot/bank hypothesis is dead: the stream causes no take, no tag change, no wrong VIC read. The bench is **not** the visible-screen case: the game's screen is off during its LOAD (D011 = 0), so no c-accesses occur. Not covered: Alex's symptom as he sees it (a text row with ~15 wrong characters while loading from the BASIC prompt or a JiffyDOS load, screen on) - that needs a `LOAD"LEVEL-A-128",8` typed at the prompt (keys) with this device, display on; and a check of the raw `mem_vic_tag` (unfiltered, `vt_stable` hides a 1..3 clock glitch) and of the bank *address* (`VICCHK` compares data for the requested address only, a wrong bank in the address is invisible to it). Also still open from above: fast serial (T170 item 6) as the cause of "stock load stops". Builder `~/scratch/vicglitch` holds the synced tree and logs `dev5.runlog`.

**16. Issue #19 short glitches (2026-11-08, branch c128-vicglitch): the VIC latched the MMU's $FF00 register - found and fixed in simulation; board pass 2026-11-09.**
- *Board (Alex, 2026-11-09, package c128-vicglitch-a2bcbb2e, release build with Z80):* PETSCII Robots in 40 columns without the glitches, and no flicker while loading ("1 and 2 are fixed!!!", translated): #19 and #21 fixed. 80 columns: no picture errors.
- *Board (Alex, debug build):* 5-10 glitches per frame, each one raster line high and 8 pixels wide, elsewhere every frame, more while loading; `vic_ff` stays 381, `wfifo_full` 0,
  frame length 628,992, `vic_blk1` 0, screen RAM correct. So the VIC latched a valid but wrong byte, and not from a slot miss.
- *Cause (code):* `c128_map.v:56,71,131` decodes `cs_mmu_hi` ($FF00-$FF04, C128 mode) from the CPU's bus address in every clock, and the arbiter's data bus mux took it
  without asking who owns the bus: `c64_bus_arbiter_c128.v:409` (before the fix) `else if (map_cs_mmu_hi && !bus_we) bus = map_mmu_do;`, ahead of `ram_select`. The 8502 puts
  the address of its next cycle out at `phi2_l` in the VIC half (`c64_top_c128.v:680` `cpu_ce_x`, `vicii_c128.v:461`), so in the VIC half of every cycle in which the CPU reads $FF00-$FF04 the byte the VIC
  latches at the end of that half (`bus_access.v` DATA_DAV = 0) was the MMU register, not RAM: one g-access = 8 pixels of one line. While BA holds the CPU on such a read
  (a badline) every c- and g-access of the row gets it: a whole character row (fits D30's "one character row of wrong data"). The C128 KERNAL's IRQ entry ($FF17) reads
  $FF00 on every interrupt, so a music IRQ and the serial routines while loading give the rate seen. Independent of the RAM path: the block RAM build has it too. C64
  mode is not affected (`cs_mmu_hi` only in C128 mode). Section 15's statement "the VIC gets wrong data only from (1) or (2)" was incomplete: it covered c128_ramd only.
- *Fix:* `cpu_side` = the decode is in its CPU branch (`c64_bus_arbiter_c128.v:589`), and the mux line becomes `map_cs_mmu_hi && !bus_we && cpu_side` (`:410`). The CPU's own
  read is unchanged: the 8502 takes its data at `phi2_n` and `fast_cap` (`cpu6510_c128.v:157-162`, `vicii_c128.v:464`), both in clocks where the decode is in the CPU branch (AEC still high
  at `phi2_n`; the 2 MHz capture at ph 23 only for a cycle that is not io_class, and $FF00-$FF04 is io_class, `c128_map.v:165`). Cost: no flip-flop, no block RAM;
  one more input on the select of one mux term, estimated 1-3 LUT (not built in Vivado).
- *Bench (`sim/run_vicbus.sh`, registered as `c128_vicbus`):* program `vicmmu` (`tools/gen_c128_test.py`: the vicblk0 multicolour bitmap, then a loop of `LDA $FF00` and
  rewrites of the bitmap, the video matrix and the colour RAM with their own values); `tb_c128 +VICBUS` compares every byte the VIC latches (c/g/p/s, RAM, at the
  sample clock) against a reference memory built from every RAM write command, and the colour nibble of each c-access against the colour RAM; `+FRAMEHASH` per frame.
  Builder, trx64-sim-env:4, 300 ms simulated. Before the fix: bram `vicbus|91316|5244|0`, ddr `vicbus|91304|5227|0`, 4 different hashes in frames 11-14 in both builds;
  first lines (ddr) `vicbus|bad|frame 9|line 92 x 259|ctype 4|blk 0 addr 7fff|got 3e want 3c|...|cpu addr ff00 R` and `...|line 97 x 51|ctype 0|blk 0 addr 47fe|got 3e
  want a4|...|cpu addr ff00 R` ($3E = the MMU's CR, every wrong byte with the CPU on $FF00). After: bram `vicbus|91316|0|0`, ddr `vicbus|91304|0|0`, one hash
  (`ae5cb8fa`) in frames 11-14, `PASS run_vicbus`. No negative control (not flash or persistence, T174 D5.4).
- *Read and ruled out for this symptom (`c128_ramd.v`):* a slot read and a CPU write in the same clock cannot happen (one operation per clock, `:79-88`); the slot is
  written at the CPU's command (`:128-133`), the write FIFO only feeds the DDR2 (`:190-206`), so there is no window in which the VIC reads a slot without a write the
  FIFO still holds; the pinned slots are never swept again after configuration (`:312-313` takes only slot 4, `vic_ff` constant says slot 4 was not re-taken); a sweep
  starts only with an empty FIFO (`:250,348`) and fetches a line again after a write to it (`:330,367`). Colour RAM (`c64_top_c128.v:1792-1806`): the VIC reads at
  `sdram_pending` of its half, the CPU writes in its own; no shared clock. Bank: `ram_bank_r = map_vic_bank` in the VIC branch (`c64_bus_arbiter_c128.v:694`), the MMU
  register changes at `phi2_n` only. `c64_mem_c128d.v:94-98`: one command per half cycle, so `romc_valid` never meets a VIC answer.
- *Open, not the #19 symptom (persistent, not per frame), not measured:* `fill` (`c128_ddrx.v:32,100`) is shared by the ROM and the RAM line reads; `c128_ramd` copies a
  RAM line into a slot from it over at least 8 clocks (`c128_ramd.v:120,362-371`) after its 3-clock synchroniser, while `c128_ddrx` may start the next ROM read 8 ui clocks
  (75 MHz) after the RAM line (`:104-107`). A ROM line landing before the copy ends would write ROM bytes into the slot; only during a sweep (start-up of slots 0-3, a
  take of slot 4). Not seen in any bench so far.
- *Existing benches:* `tools/benches.sh c128 -f` on d6fb55e70 (builder): all 24 PASS (`c128_vicblk` 443 s, `c128_vicrom` 1021 s, `c128_vicbus` 279 s, `c128_ramddr` 401 s).
- *Not proven:* the board. A Vivado run of the fix (route, slices at 99.6 %) and a board look at PETSCII Robots C128 (title with music, and while loading).

**17. Open (2026-11-09, board): 80 columns, no READY after a reset.** Alex: with 40/80 DISPLAY on 80 and the VDC shown, a reset (case rocker and the menu's Reset alike) shows the start banner (`COMMODORE BASIC V7.0 122365 BYTES FREE` ... `ALL RIGHTS RESERVED`) but no `READY.`; after a restart of the core it works ("I suspect the reset does not initialise something properly", translated). Not measured yet. Hypothesis to check in simulation: the VDC (the real 8563 has no reset pin; the KERNAL sets it up) is left by the reset in a state the KERNAL waits on (status bit 7, e.g. a block copy/fill still running). Bench: boot in 80 columns, reset, expect READY.

  *Findings (2026-11-09; cause not found, candidate fix built below):*
  - *Reset paths read:* the application's Reset/rocker pulses the core's `cpu_reset_req` (`c64_core_c128.v:291` `cpu_rst_in`), which reaches only the CPUs/MMU/VIC reset lines (`c64_top_c128.v:527,589`). The VDC (`c128_x.v:51-53`, `vdc_top.sv`) is wired to the system `rst` (`c64_top_c128.v:2057`), which a warm reset does not raise; only the cold start (`c128_cold_start`, `C128_COLD`) resets it. So after a warm reset the VDC keeps every register, the fill/copy state and its RAM, as the real chip does; the KERNAL must re-initialise it. Hypothesis (unverified): some VDC state the KERNAL's reset does not rewrite (R24 copy bit, R30/R31 pipeline, R28/R25 flags, busy/erase in `vdc_ramiface.sv:130`) is left over.
  - *Scenario:* `cores/c128/sim/run_vdcreset.sh` (local, real ROMs: `C128_ROMDIR=<dir> VR_T1=<us> VR_T2=<us> sh cores/c128/sim/run_vdcreset.sh [real|real_cache]`; new tb plusarg `+VSHOT_AT` prints the VDC text screen). It runs `+D80`, shows the VDC screen shortly before the reset at T1, resets 50 ms, shows it at T2. About 3.6 min of builder CPU per simulated second.
  - *Result so far (builder, mode real, T1 = 5.0 s, T2 = 8.0 s):* before the reset the VDC shows the banner but NO `READY.`; after the reset the same. So the sim does not yet separate "reset" from "first boot": either READY comes later than 5 s in 80 columns in the sim, or the sim lacks READY on the first boot too (then the sim differs from the board's cold start). Not resolved: needs a PC trace around 4.99 s (`+TRACE +TRFROM`) to see what the 8502 waits on, and a run with a later T1 (>= 9 s) to see if READY appears on first boot. (Superseded by the run without drive below.)
  - *Sim result without drive (2026-11-09, Alex: the KERNAL talks to the drives at start, READY comes well over 5 s on the board; "leave the drive out", translated):* `VR_ARGS="+NODEV +PROBE=500" VR_T1=4000000 VR_T2=6000000` (mode real): `READY.` in 80 columns before the reset and again after it, `PASS run_vdcreset`. So the sim does NOT reproduce the board; with a drive it was not run (about 3.6 builder-min per simulated second). The cause is therefore not measured. `VR_ARGS` in the script passes extra plusargs.
  - *Candidate fix, to be proven on the board (Alex, 2026-11-09: "or just build"):* the VDC's control state is cleared by the warm reset too, RAM and registers stay as on the real chip: `c64_top_c128.v` passes `cpu_reset_i` (the registered CPU reset: high for the rocker/menu Reset, and also at power-up and cold start) as `warm` to `c128_x` -> `vdc_top.ctl_reset` -> `vdc_ramiface.ctl_reset`, which sets `cpuAction <= CA_IDLE` (a CPU access left half-way no longer holds status bit 7 low). Review 2026-11-09: the first build also cleared `start_erase`/`erasing`, which cancelled the power-up erase of the VDC RAM (`cpu_reset_i` is high at power-up too); now the clear only acts when no erase is pending or running (`ctl_reset && !start_erase && !erasing`). Cost estimate: 1 port, ~3 LUT, 0 FF. Benches on the builder (`tools/benches.sh c128_vdc c128_reset c128_cold -f`, commit 695990205): `c128_vdc PASS 8s`, `c128_reset PASS 55s`, `c128_cold PASS 81s`. Open: if the board still shows no READY after the reset, the suspect moves to the IEC/drive side (the KERNAL's boot-disk check after a warm reset, the drive is not reset), which the sim without drive cannot show.

**18. Decided (2026-11-09, Alex: "Auto makes sense", translated): "Picture shown" gets "Auto" as its default.** The C128 drives two monitors; we have one HDMI output.
Auto shows the screen the KERNAL uses: the active-screen flag at `$D7` bit 7 (set at the start from 40/80 DISPLAY, toggled by ESC X / SWAPPER), so a person
switches only 40/80 DISPLAY and the picture follows. "40 columns" and "80 columns" stay as fixed values. To build: core app only (`coreapp/cores/c128.cc`
reads `$00D7` of RAM block 0 in its service and writes MON_SEL), render bench + emulator, then board.
- *Decided (Alex, 2026-11-09, translated: "Yes, if it fits, that would be the cleanest, I think"):* Auto also detects which chip shows a picture, so a program
  that drives the VIC without touching `$D7` (PETSCII Robots C128 after an 80-column start: music, the VDC still shows the banner) gets the VIC picture.
  The detection is gateware (activity of the VIC and the VDC per frame, read by the app) and is built with T194 on `c128-scale`, if it fits the area;
  the app part of Auto (`$D7`) on `c128-autopic` first, the detection joins it there.
- *The gateware (built on `c128-scale`, T194 §3.2, bench `c128_act`):* `cores/c128/c128_act.v` counts per VIC frame, from the raster line's wrap to 0 to the next one, the CPU's writes to the
  VIC's registers ($D000-$D02E, the turbo CPU's posted writes too), to the colour RAM ($D800-$DBFF) and to the VDC's data register ($D601), 8 bit each, saturating at 255, and samples DEN
  ($D011 b4) at the end. Why writes and not pictures: the VDC's hardware cursor blinks by itself, so a changing VDC picture proves nothing; a CPU write to $D601 does. Registers of the C128's
  page E (`c128_regs.v`, `coreapp/cores/c128_regs.h`), read only, of the last complete frame: `0D0 ACT_VIC`, `0D1 ACT_CRAM`, `0D2 ACT_VDC`, `0D3 ACT_FLAGS` (b0 = DEN), `0D4 ACT_SEQ` (frame
  number, wraps). Read ACT_SEQ before and after the other four: if it is the same, they are one frame's. A held machine (Pause, Freeze) stops the counting; the values stay.
- *Rule the app should apply (proposal, to be calibrated on the board against the counts of an idle BASIC in 40 and in 80 columns):* look at each new frame number; keep the sums of the last 8
  frames. `vdc` = sum(ACT_VDC); `vic` = sum(ACT_VIC) + sum(ACT_CRAM), taken as 0 while DEN was 0 in all of them. (1) Only one is above 0: that chip's picture (VIC: 40 columns, VDC: 80). (2) Both
  above 0 (the KERNAL's own interrupt may write the VIC's registers in either mode, a game may use both): the `$D7` flag decides, as without detection. (3) Both 0 (a still picture): no change.
  Switch after two evaluations in a row agree (about 270 ms), so that a single stray write does not flip the picture. A fixed "40 columns" / "80 columns" setting ignores all of it.
- *Area (estimate by structure, T194 §5):* `c128_act` about 35 LUT and 60 FF (three 8-bit saturating counters, three latches, a frame number, edge detectors), the five bytes crossed into the bus clock
  in `c128_regs` about 12 LUT and 120 FF (two samples and a held copy of 40 bits, as the other status words).


*Built (2026-11-09, branch c128-autopic, `coreapp/cores/c128.cc` only; no gateware, no other core, nothing generic):*
- Setting "Picture shown" has three values: 0 "40 columns (VIC-IIe)", 1 "80 columns (VDC)", 2 "Auto" = the default. Saved settings store the label, so old files with 0/1 load as before.
  `c128.set_monitor` takes 0..2.
- Auto: the service (`op_service`, every 20 ms) reads physical `$0000D7` of the RAM window (`ram_read`, block 0) and writes MON_SEL when the wanted picture
  (`$D7` bit 7) differs from the one it last wrote (`auto_seen`). Choosing Auto applies at once; a reset or cold start sets MON_SEL 0 and `auto_seen` -1, so the
  service writes the KERNAL's value as soon as it is there; the release also clears `$D7` in the RAM (Auto only), so the value from before a warm reset does not show the VDC for a moment. One function decides, `auto_decide(c64, z80, d7_80, activity)`; `activity` (-1 none, 0 VIC, 1 VDC) is fed by the gateware's per-frame activity registers (branch c128-scale, T194 5.1: `act_poll`, 8-frame window, a verdict after two frames in a row, it wins over `$D7`). Bench numbers: T194 5.1; board calibration open.
- C64 mode (`C128_STATUS` b0): `$D7` is another variable there, Auto shows the VIC (0). Z80 mode (b1): nothing writes `$D7`, the picture stays as it is.
- Monitor-switch key (CTRL+C=+V; the core toggles MON_SEL): in Auto it still flips the picture, and the service leaves it because the wanted value did not change;
  the next change of `$D7` (ESC X, a mode change) takes the picture again. With a fixed value the key works as before and `$D7` is ignored.
- Limit: the address is physical RAM block 0 `$00D7`, the CPU's `$00D7` only while the MMU page-0 pointer ($D507/$D508) is at its reset value; the app cannot read the MMU.
  A program that moves page 0 (GEOS 128, some CP/M-less tools) is not followed. Not measured on the board.
- Tests: `tests/coreapp/test_c128.cc` (default Auto, $D7 on/off, low bits, key flip kept until $D7 changes, C64 and Z80 mode, fixed value ignores $D7, Auto chosen again,
  reset); golden `menu_c128.txt`; render bench `c128-kit-settings.png` shows the row "Picture shown = Auto". Open: board pass.

- *Board 2026-11-09 (package c128-scale-06069827):* Fill in 40 columns works (Alex: "Fill is great", translated; #63). Auto in an idle 80-column BASIC showed the VDC briefly, then flipped to the VIC (grey screen, green border) until a key was typed: the KERNAL's IRQ rewrites a few VIC registers every frame in 80 columns too. Fix: only VIC register writes above ACT_VIC_IDLE = 16 per frame and the colour RAM count for the VIC (`coreapp/cores/c128.cc` act_poll; test_c128 covers the idle IRQ case). The threshold is an estimate, to be checked on the board.
- *Measured and fixed (2026-11-09, sim `run_vdcreset.sh` with the real ROMs, `+NODEV +ACTDBG`, builder; package c128-scale-ad63d3c1 still flipped):* per frame (latched c128_act values, beside the bench's own count of the strobe's rising edges and of the CPU's writes):
  boot to 1 s: VDC 255 (saturated), VIC 0, colour RAM 0; 1.0-1.5 s (screen set-up): VIC 5-47, colour RAM up to 255, VDC 45-255; **idle READY in 80 columns (2.0-5.2 s): VIC 23 on every frame, colour RAM 0, VDC 0, DEN 1.**
  The old threshold ACT_VIC_IDLE 16 was below the idle's 23 writes, so the VIC verdict held at READY (cause 1). Cause 2: the gateware's frame was the VIC's output vsync, which ran 44.8 ms in the sim (46 writes per "frame"); the frame is now the raster wrap to line 0
  (19.9 ms, 50 Hz), so the counts do not depend on the output timing. Strobes: one CPU write = one rising edge (edges 23 = CPU writes 24 in the same frame incl. one RAM write under I/O; the strobe is high 17 clocks per write, 391 clocks / 23), no extra counts from the chip select's half cycles.
  New ACT_VIC_IDLE 40 (idle 23, margin for bursts); the colour RAM and VDC rules are unchanged. A key typed in the sim (matrix bit held 3.5-3.7 s) did not register, so typing is **not measured**; PETSCII-Robots-like activity (colour RAM every frame, no VDC) gives the VIC in `test_c128.cc`.
  A 40-column program that writes under 40 VIC registers per frame and no colour RAM is not seen as VIC (the `$D7` flag decides). Board pass open.

**19. Issue #67: CP/M Plus in 80 columns shows "horrible graphics" (2026-11-10, branch c128-cpm67; analysis only, NOTHING measured, no code changed).** Alex: wait for #63 (T194), then revisit the C128. No CP/M disk image is in `cores/c128/sim/media` (the directory does not exist), so no sim was run.
- *The photo of the issue (read, 4000 x 3000, HDMI monitor):* the picture is the VDC's 80-column text screen, in the right place, glyphs are sharp 8 x 8 cells of the right shape, colour white on black. The text is `BOOTING CP/M PLUS` and a CPMLDR-style memory table (`BNKBIOS3 SPR ...`), but **whole character cells are blank** (`BO TING CP   P`: the second O, `/M` and `LUS` are gone, the cells keep their place, the same code O is shown once and missing once). So: timing (R0-R9, R22), charset RAM, attribute enable and the capture window are not the problem (a wrong width or height would shift or cut the whole picture, not blank single cells); the VDC RAM or its attribute bytes at those cells hold the wrong bytes, or the cells were drawn from RAM they never received. Whether the first line is printed by the 8502 (KERNAL BOOT message) or by the Z80 is not known from the photo; it decides cause 1 vs 2.
- *Ranked causes (all unproven; the first needs one sim run with the user's CP/M image, the others are excluded or small):*
  1. **A Z80 bus cycle reaches the VDC more than once (or not at the right one) - gateware, about 5-10 LUT, one register stage.** `cpu_z80.vhd:57-66` holds `we`, `rd`, `addr`, `dout` until the next `latch`; `latch` follows `z80_ce` (`c64_top_c128.v:635-642`), which is withheld while `vic_ba` or `z80_ba_q` is low or `rom_wait_q`/`ram_hold` is up. The VDC's strobe is `bus_en = vic_phi2_n && !vic_stall` (`c64_top_c128.v:2069`), not qualified by `z80_ce`, and `vdc_top.sv:376` / `vdc_ramiface.sv:158-179` act on every `bus_en` with `cs && we`: in a cycle where the Z80 is held, the same write is applied again, a repeated `$D601` write at R31 adds a byte and moves UA by 2 (R31 reads move UA twice as well: `vdc_ramiface.sv:158-162`). That produces doubled or shifted cells, not blank ones, so it explains the photo only together with a BIOS that does not set UA per cell (CP/M's console routine, not read). The 8502 is not affected: its write is one clocked cycle (`phi2_n` gates the CPU model). Proof: the existing `tb_c128_vdc.sv` / `run_vdcreset.sh` plus a Z80 program (`tools/c128asm.py`) writing 80 bytes to R31 with `OUT (C),A` while the VIC shows a badline screen and while RAM misses hold the CPU; count `ramiface` writes against Z80 `OUT`s. Fix: one-shot the VDC strobe per latch (`z80_lat`), costs a Vivado run; the coordinator decides.
  2. **A write into the VDC while it is still `busy` replaces the pending one - already in the MiSTer VDC, not a bug there, but our Z80 timing exposes it.** `vdc_ramiface.sv:158-179`: R31 / R30 / R18 / R19 writes overwrite `cpuAction` and `wda` unconditionally; the real KERNAL polls `$D600` bit 7 before every data write, the CP/M BIOS code is not read (it may poll once per string, or rely on the real chip's speed: the real VDC takes a CPU write in the column slot of `en_int` times and is ready again before a 2 MHz Z80 can issue the next OUT; ours takes the CPU slot only after screen/attribute fetches: `vdc_ramiface.sv:262-293`, in a full 80-column row the free slots are the refresh/idle columns). The status bit read by `IN` is `~busy` (`vdc_top.sv:400`), `busy` is `cpuAction != CA_IDLE` (`vdc_ramiface.sv:127`) - read, correct. Fix if confirmed: none in the VDC (matches upstream); would need a CP/M BIOS look and a sim; gateware only if the slot rule differs from the chip.
  3. **Reset of the VDC engine / T140 5.17 (same family).** After `BOOT` nothing resets the VDC, but `warm` clears `cpuAction` on any CPU reset including the Z80 start (`vdc_ramiface.sv:381-383`); the CP/M boot sector switches CPU modes through the MMU, not a reset - no code path found that hits it. Weak.
  4. **VDC features CP/M needs that are missing: none found.** Read register by register (`vdc_top.sv:113-248`): R0-R9, R8 interlace bits are stored (`reg_im`), R10/R11 cursor, R12-R15, R18-R19, R20/R21 attribute start, R22, R23, R24 (copy, reverse, blink rate, smooth scroll), R25 (text/bitmap, attributes, semi-graphic, double width, hss), R26, R27, R28 (charset base and RAM size), R29, R30-R33 (fill, copy), R34-R36, R37 (8568 only) all exist; block copy and fill are in `vdc_ramiface.sv:78-110, 200-258`; the shipped build has the 64 KiB VDC (`mono/core.toml` `TRX_DEFS=VDC64`, `c128_x.v:46`, `shuffleAddr` `vdc_ramiface.sv:78-92` implements the 16 K and 64 K views). Not implemented: interlace in the video (`reg_im` is read only by `vdc_signals_v.sv`, the picture stays 640 x 200), the light pen is wired off (`lp_n = 1`), 8568 mode. None of these blanks single cells.
  5. **The capture window (`c128_pixw.v`, `layer2_c128.v:723`) is fixed to 640 x 200.** A BIOS that programs another R1/R6/R9 (not R22 here: the glyphs are 8 wide) would shorten a line (no `last_word`, the line is never written: `c128_pixw.v:96-101,122`) or a frame (`row_n == LINES - 1` never true, no frame flip: `:128`), which stalls or tears the whole picture. The photo shows a complete, stable 80-column picture, so this is not what the issue shows; a CP/M with a different row count (R6 != 25 or R9 != 7) would still need the window to follow the VDC's displayed size (app/gateware, about 30 LUT, T194 area).
- *What would be certain:* one run of `run_vdcreset.sh`-style sim with the real ROMs and a CP/M Plus boot disk (a `.d81` of CP/M system disk, user's own, local only, in `cores/c128/sim/media`) with a probe that logs every `vdc_ramiface` write (`regA`, `db_in`, `ram_addr`) next to the CPU's writes at `$D600/$D601`; the first missing cell on the 80-column picture names the cause (extra writes, replaced writes, or none = capture).
- *Decision for Alex:* (a) supply a CP/M image for the builder (the repo cannot hold it) so the cause is measured before any gateware change; (b) or build cause 1's fix speculatively (a Vivado run, T140 item 4's room of about 690 LUT is enough). No app-side fix exists: the Z80/VDC bytes never pass through the app.
- **Result 2026-11-10 (branch c128-cpm67, measured in simulation; NOT yet on the board, not yet through Vivado).** Cause: **every Z80 `OUT` to the VDC data register is followed by a phantom READ of the same register.** `cpu_z80` holds addr/rd/we for a whole latch period, an IN/OUT cycle spans two periods, and the VDC strobe `bus_en = vic_phi2_n && !vic_stall` fires in every 1 MHz cycle, not once per Z80 cycle. The second strobe has `cs`, `rs = 1`, `we = 0`: `vdc_ramiface.sv:158-162` takes it as a read of R31, moves UA once more and sets `cpuAction = CA_READ`, which **replaces the pending `CA_WRITE`** of the byte just written whenever the column engine has not yet found a free slot (an 80-column row: the screen/attribute/char fetches take most slots) - the character cell never reaches the VDC RAM, and UA advances by 2 for the cells that do. (This is cause 1 with a different mechanism than guessed - not a repeated write - and the symptom of cause 2, the replaced pending action.) The same mechanism repeats a write or read for every cycle in which `z80_ce` is withheld (BA low, ROM wait).
  - *Evidence (bench `cores/c128/sim/run_z80vdc.sh`, program `z80vdc` of `tools/gen_c128_test.py`: the Z80 writes 64 bytes with `OUT (C),A` into VDC RAM, VIC display on, polling status bit 7 before every access like the KERNAL; probe `+VDCPROBE` in `tb_c128.v`):* before the fix `VDC strobes with a write: 652, Z80 writes to $D6xx: 648`; 392 reads of R31 against 256 writes of R31 (every `|cw|rs1|r31|` is followed one cycle later by `|cr|r31`); UA moves 4000, 4002, 4004, 4005, 4007 ... for consecutive writes; `|cw|rs1|r31|d42|act0|ua4004` is followed by `st|ua4005|ca0>2|ra3` (the engine is busy fetching), `cr|r31`, `ca2>1` (the pending write became a read) and no `rw|a4004` - the byte is lost; VDC RAM `$4000..` = `40ff41ff00430044004500ff47...` (every second cell empty, some bytes gone) where `40 41 42 ...` was written.
  - *Fix (gateware, `cores/c128/c64_top_c128.v`):* (1) `x_bus_en`: the strobe of `c128_x` (VDC, VIC-IIe) is taken once per latched Z80 cycle (`z80_fire`: every write, a read whose address, direction or IORQ differs from the previous latch) and not at all for the Z80's idle second period; the 8502 keeps the strobe of every cycle. (2) `z80_vdc_q`: the VDC's `db_out` follows its registers while `cs` is held, so the byte of an R31 read (DA) was already replaced by the next one when the Z80 took it a T-state later; the Z80 gets the byte the strobe saw (measured: reads of R31 returned `41 42 .. 4f 4f 51 ..` instead of `40 41 ..` with fix (1) alone).
  - *After the fix:* `run_z80vdc.sh` PASS: strobes 648 = Z80 writes 648; `$4000` (poll before every access), `$4200` (R18/R19/R31 per cell, polled), `$4300` (64 bytes read back with `IN` and written again) hold `40..7F` exactly. `$4100` (writes back to back with no status poll) still loses 4 of 64: a second write while the first is still pending replaces it - the chip's own rule (software must poll bit 7), reported, not a PASS criterion.
  - *Not measured:* the CP/M disk itself. `run_cpm.sh` boots `cpm.system.622-3297432.d64` from a real 1541 (`c64_c1541` of vendor/icepi in `tb_c128.v`, `-DSIM_DRIVE`, `+DRIVE`) with the real ROMs; the machine simulates at 4.5 ms of machine time per second, the CP/M boot needs tens of seconds (the first run reached 8.5 s without the CP/M text yet) - run it overnight on the fixed build to see the 80-column text complete. Cells that CP/M writes without polling would still be lost (as on the chip). The same two-strobe pattern exists for every other chip with read side effects (CIA ICR, SID) - not touched here.
  - *Needs Vivado + board:* the fix is a ~12 FF / few LUT change in the host-side wiring of the C128 core; one core build, then Alex's CP/M disk on the board.
  - *Benches after the fix (builder, 2026-11-11):* `run_machine.sh` (boot, mmu, xkeys, fast, c64mode, iec, z80back and the five cache_* programs), `run_z80ba.sh`, `run_reset.sh`, `run_cold.sh`, `run_pause.sh`, `run_z80vdc.sh`: all PASS. `run_vdcreset.sh` with the real ROMs, shortened to 4.5 s / 9 s: the 80-column banner "COMMODORE BASIC V7.0 122365 BYTES FREE" is on the VDC before and after the reset, but READY. is not on the screen yet at 4.5 s, so the script's own PASS line was not reached (a timing cut of this run, not a result; the 8502 path is unchanged by the fix: `x_bus_en` equals the old strobe when `z80_act = 0`). Not run: `run_vdc.sh` (VDC alone, the top level is not in it).

5.19 board 2026-11-11 (Alex, c128.cor of branch c128-cpm67 on the board SD, CP/M 3 system disk `cpm.system.622-3297432.d64` on drive 8, 40/80 = 80
columns): the 80-column picture is clean ("BOOTING CP/M PLUS", the SPR table, "58K TPA", then "CP/M 3.0 On the Commodore 128 28 MAY 87 / 80 column
display") - the #67 fault is gone on the board. Separate fault: with the disk mounted after the core start and a reset, the boot hung after "58K TPA"
with the BIOS status "02 19" (track 2, sector 19 in progress); after "Reload core" (disk already mounted) CP/M booted through, slowly (1541). Static
analysis (agent, not measured): the BIOS has burst-read loops that wait without a timeout on CIA1 SDR; this core ties CIA1 SP/CNT to 1 and leaves the
drive's SRQ unconnected (`c64_top_c128.v:933,935`); why a boot after a warm reset takes that path is open - same family as 5.17.
