# T92 — An Amstrad/Schneider CPC core, ported from the MiSTer core, run as a swappable core in the T79 host/core split

**Status:** BUILT, benched, application written (2026-10-22, §5); decided 2026-10-13 (Q1-Q5 answered, §3 end). Not routed, not on a board.
**Repo:** TRX64-Ultimate (gateware under new `designs/cpc/` plus the shared parts under `cores/zxcpc/` that T91 builds, the
core's application on Gideon's base as for the VC-20 and the Amiga, tools). The sources are read from outside
(MiSTer-devel); nothing is copied into this repo by this spec.
**Number:** T92. Split out of the combined ZX Spectrum / CPC spec on 2026-10-13 (Alex, Q1/Q2): the Spectrum stayed in T91
(`T91-zx-spectrum-core.md`), the CPC is this spec.
**Milestone:** M3 (other cores; `docs/other-cores.md`). Alex's call.
**Origin:** Alex, 2026-10-12 night: position 8 of the core order is "ZX Spectrum / Amstrad (Schneider) CPC" (`docs/todo/T79.md`);
2026-10-13: "write the spec"; Q2: "a, and then the CPC in its own spec please".
**Depends on:** **T91** (the shared modules, built first: T80, YM2149, NCO, key translator, tape pulse engine, audio averager,
register-window skeleton and identity block), T79 (host/core split, contract `T79.1-host-core-contract.md`), T83 (VC-20: the
precedent for ROMs from files and the app on Gideon's base), T85 (Amiga: the pixel-layer picture, the DDR2 sector service,
Freeze), T84 (`.cor`), T87 (D15/D16: the slim C64 is the reference every core must route against). Floorplan B (`pb_l2`) must
stay as measured.

Template: `docs/specs/_TEMPLATE.md`. Citations: `T79.1:N`, `T83:N`, `T85:N` are lines of those specs on their branches
(`dfx-trial`, `origin/t83-vc20`, `origin/t85-amiga`, read 2026-10-13); `layer2.v`, `vic_scaler.v` and `T59-palette.md`
are read on `integ-m1` 73e5f20 (this branch, `t92-cpc`, was created from `t91-zxcpc` before the split). Source files are from
the scratch clone `specsrc/Amstrad_MiSTer` (6c2c39b, 2026-06-03), read 2026-10-13. (The Spectrum's clone is cited in T91.)
**(source)** = read in a file named next to it. **(computed)** = arithmetic on stated numbers. **(estimate)** = derived,
not measured. **(measured)** = taken from a report or a run. **(hardware fact)** = published machine documentation, not a file.

---

## §1 The problem

**Who:** Alex, who wants the C64 Ultimate board to be a host for many machines (core order, `T79.md`, 2026-10-12 night:
position 8 = "ZX Spectrum / Amstrad CPC"; split into T91 and T92 on 2026-10-13).

**What he wants:** pick "Amstrad CPC" in the core selector (T81/T84), the board swaps layer 2 by partial reconfiguration,
and the machine boots to its BASIC with the C64U's keyboard and joysticks, picture, sound, a tape to load from and a disk to
start.

**Today:** two cores exist or are being built against the contract (VC-20 T83, Amiga T85); the reference every core must
route against is the slim C64: 13,643 LUT, 4,300 slices (96 % of `pb_l2`'s 4,500), 39 BRAM tiles (T87 D15/D16, quoted from
`docs/specs/T87-ci-layer-caches.md` §5). `pb_l2` has 18,000 LUT, 4,500 slices, 50 RAMB36, 80 DSP (task text, T83 D12). No
Z80 machine exists in the repo yet; the Spectrum (T91) builds the first and the Z80 and AY modules this core reuses. The edge
still has the VIC-II shaped video (`pix_color[3:0]`, `pix_x[9:0]`, `pix_y[8:0]`, T83 D5) and a pixel layer that reads an
RGB565 frame buffer from DDR2 (T85 D5); a 16-entry, app-written, 24-bit palette sits behind the 4-bit colour index
(`vic_scaler.v` ports `pal_we/pal_addr/pal_wdata`; `T59-palette.md` §3).

**What the sources are.** There is **no CPC core in MiSTle-Dev** (Till Harbaum's organisation has 17 repositories on
2026-10-13: C64Nano, VIC20Nano, C16Nano, NanoApple2, A2600Nano, NanoMig, NanoMac, MiSTeryNano, MiniMig, snescore and
tooling; GitHub API listing, checked by this spec). "The Nano cores are already there" holds for the VC-20, C16, Apple II
and Amiga, not for the CPC. The maintained lineage is MiSTer-devel: `Amstrad_MiSTer` (Sorgelig, Gyorgy Szombathelyi; "every
module has been either rewritten or replaced" from CoreAmstrad, README). A third-party Tang Nano 20K port of it exists and
is looked at in D2.

**Why it matters:** the CPC is a Z80 machine on Alex's list, small enough for `pb_l2` (D13) and with no DDR2 use for the CPU.
It tests three things the earlier cores did not: a Gate Array with wait-state contention, a 16-colour-at-a-time palette machine
whose palette changes mid-frame, and a floppy controller with write-back. The Z80, AY, key translator and tape engine come
from T91.

**Break the problem into parts, MECE** (§2 is checked against this list):

- **P1 Shape.** One core or two; what is shared.
- **P1 Shape.** A second core on the modules T91 builds; what is shared.
- **P3 Porting surface.** Kept, replaced by host services, replaced by the app.
- **P4 Clocks, reset and frame lock** (contract C1).
- **P5 Memory and ROMs** (C3 and the app's ROM path).
- **P6 Video** (C4).
- **P7 Audio** (C5).
- **P8 Input** (C6): keyboard, joysticks.
- **P9 Tape.**
- **P10 Disks.**
- **P11 Registers, identity, app, ROM files, swap and Freeze** (C2/C8/C9).
- **P12 Size and timing** against `pb_l2` and the C64 reference.

## §2 Scenarios — what proves it is solved

Machine: **CPC** = Amstrad CPC core. Benches are named after the existing layout (`designs/c64_basic/sim/<core>/`);
all of them are **to be written**. Test programs are our own Z80 sources (assembled by a script in `tools/`); nothing
third-party is checked in, and no ROM is in the repo, CI or a `.cor`. Third-party suites (ZEXALL, CPC demos) are
run by hand on the board or in simulation from files the user supplies.

```gherkin
Scenario: S1 the shape is decided and bookkept before gateware starts (P1)
  Given Q1 (one core or two) is answered by Alex (two) and the shared modules of T91 exist under cores/zxcpc/
  When  the first line of CPC gateware is committed
  Then  §3 D1 lists the modules taken from T91 and the TODO table has the row T92
  And   no module (T80, YM2149, key translator, tape engine) exists as two copies in the tree

Scenario: S2 the licence list exists and nothing blocked is taken (P2)
  Given the per-file table in D2
  When  a file is merged under cores/ or designs/
  Then  its header is kept and its row exists; every ROM byte is absent
  And   ci/package.sh output contains no file whose path matches zx or cpc (TREX-internal, T83 D13)

Scenario: S3 the core elaborates on Xilinx and a Z80 test ROM passes in simulation (P3)
  Given the kept modules, inferred RAM, MiSTer framework (HPS, sys/, SDRAM, PLL) removed
  When  the core is synthesised out of context for xc7a50t and simulated running our Z80 test ROM
  Then  Vivado reports no black box; the ROM's checks (flags, block moves, IM2, R register, DAA, undocumented flags) read back right
  And   the TRX64, VC-20 and Amiga builds are unchanged

Scenario: S4 the clocks are exact and the frame is the scaler's frame (P4)
  Given the identity entry of the machine (ratios in D4)
  When  the app releases core_rst
  Then  CPC frame = 628,992 sys_clk, counted by the ring counter on the board
  And   the T-state clock is within 0.09 % of the real machine and the scaler stays locked for 10 minutes

Scenario: S5 paging and ROM banking behave as on the real machines (P5)
  Given the BRAM RAM and ROM of each core, filled by the app from files
  When  our test ROM pages every RAM bank (CPC Gate Array RAM/ROM select) and reads the ROM windows
  Then  each bank keeps its own data, the screen at $C000 or paged displays, ROM selection returns the right image
  And   a model switch (CPC 464/664/6128) rewrites the ROM BRAM and the machine cold-boots into the new model

Scenario: S6 contention is the real contention (P5)
  Given the CPC core
  When  a timed loop runs (our test ROM, border colour toggled by frame)
  Then  the Gate Array's wait states give the 4 MHz / 1 us slot pattern (instruction times in whole microseconds)

Scenario: S7 the CPC picture is right, including mid-frame ink changes (P6)
  Given the CPC core with the pixel layer on (3 frame buffers)
  When  our test program shows mode 0, 1 and 2 screens and changes an ink every raster line (rasterbars)
  Then  the buffer hash in simulation matches per mode and the rasterbars are on the right lines
  And   a 5-minute run on the board shows no tear seam and no dropped or repeated frame beyond the counted ones

Scenario: S8 sound (P7)
  Given AY-3-8912 (CPC)
  When  our test ROM plays an AY tone and a noise
  Then  the audio stream's tone periods are within 0.3 % of the programmed value (after the frame lock) and noise has no tone peak
  And   the output is exactly 0 while Freeze holds

Scenario: S9 keys and joysticks reach the machine (P8)
  Given keys[63:0], joy1, joy2 from the host
  When  each mapped C64U key is pressed (table in D8) and each joystick direction and fire
  Then  our test ROM reads the matching CPC matrix bits and nothing else
  And   shifted combinations (CPC SHIFT and CTRL) inject exactly the key set the table lists

Scenario: S10 a tape loads at real speed and stops when told (P9)
  Given a CDT written by our tool (content ours, standard ROM loader format)
  When  the app converts it to the pulse stream and the user types |TAPE, RUN"
  Then  the program loads, the loaded bytes read back equal to the tape's data through the DMA window, and it starts
  And   the CPC motor bit stops and restarts the stream; a bad checksum block gives the ROM's "Read error"

Scenario: S11 disks (P10)
  Given a DSK image with a directory and a file
  When  the app mounts the disk (drive A and B)
  Then  CAT lists the file, LOAD reads it byte-exact
  And   CPC SAVE then eject writes the changed sectors back to the stick's file; a pulled stick reports an error and the image stays in DDR2

Scenario: S12 the app offers what exists and nothing else (P11)
  Given a CPC core loaded (identity "CPC ")
  When  the user opens the menu and calls REST
  Then  reset, model, tape, disk, joystick and sound items exist; monitor, SID, REU, cartridge writer, Timewarp and C64-address machine:readmem are absent
  And   a missing ROM file shows its name on the overlay and the core is not started (no silent black screen)

Scenario: S13 Freeze stops the machine and resumes exactly (P11)
  Given a running CPC machine
  When  the user chooses Freeze (menu open, 62 ms to minutes) and resumes
  Then  the machine stood still (no CPU clock, no frame, no AY/beeper, tape position unchanged) and the resumed run is cycle-identical to a never-paused run (hash per frame, as T85 cmp_pause)
  And   the picture lock holds after resume (no position error; T83 D16/D16a)

Scenario: S14 the core fits and routes against the locked host (P12)
  Given the T87 CI with the slim C64 host
  When  the core is built as a partial
  Then  out-of-context synthesis reports LUT within D13's range and BRAM <= 47 tiles, and the partial routes with WNS >= 0
  And   the swap round trip C64 -> CPC -> C64 works; the C64 reference still routes (host unchanged)
```

| # | Solves | Scenario | Test (bench / CI stage / board script) | Where it runs |
|---|---|---|---|---|
| S1 | P1 | shared modules from T91, no duplicates | `tools/check_todo.py` + a tree check in `ci/lint` (to be written) | CI `lint` |
| S2 | P2 | licence table, no ROM | `ci/package.sh` self-test row + repo scan (to be written) | CI `package` |
| S3 | P3 | elaboration + Z80 test ROM | `designs/c64_basic/sim/cpc/run_z80.sh` → `ci/sims.txt` row | CI `sims`, `synth` |
| S4 | P4 | clocks and frame count | `sim/cpc/tb_clk`; board: frame counter over REST/JTAG script | CI `sims`; board (autonomous) |
| S5 | P5 | paging, ROM banking, model switch | `sim/cpc/tb_page` with our `paging.asm`; board script reads RAM through the DMA window | CI `sims`; board |
| S6 | P5 | contention | `sim/cpc/tb_slots` (T-state counters in the bench) | CI `sims` |
| S7 | P6 | CPC picture, rasterbars, no tear | `sim/cpc/tb_video` buffer hashes; board 5-minute run + counters over REST | CI `sims`; board |
| S8 | P7 | sound | `sim/cpc/tb_audio` (period measurement) | CI `sims` |
| S9 | P8 | keys and joysticks | `sim/cpc/tb_keys` against the D8 table; board: typed line, photo | CI `sims`; board |
| S10 | P9 | tape, motor, error | `tools/mktape.py` (our tape) + `sim/cpc/tb_tape`; board script | CI `sims`; board |
| S11 | P10 | disks, write-back | `tools/mkdsk.py` + `sim/cpc/tb_media`; board: eject, read the file on the host | CI `sims`; board |
| S12 | P11 | app gate, missing ROM | app unit test (host build of the app); board script reads the menu state over REST | CI `app`; board |
| S13 | P11 | Freeze | `sim/cpc/run_pause.sh` (as `sim/amiga/run_pause.sh`, `cmp_pause.py`) | CI `sims` |
| S14 | P12 | size, routing, swap | T87 core stage reports; board script for the swap round trip | CI `synth`/`bit`; board |

Every scenario solves one part; every part has one. Negatives: S2 (ROM bytes), S3/S14 (other cores unchanged), S12
(missing ROM, C64 features absent), S13 (no position error).

## §3 Decision

Facts first, then D1-D14, then the risks and the open questions.

**Amstrad_MiSTer structure (source).** `Amstrad.sv` wraps `Amstrad_motherboard` (Z80 `T80pa`, `ga40010` gate array with
`video`/`casgen`/`syncgen`, `UM6845R` CRTC, `i8255` PPI, `YM2149`, MMU, `u765` FDC for two drives, keyboard matrix from PS/2
through `hid.sv`), `color_mix` (GA colour to RGB), `crt_filter`, `tzxplayer` (CDT), `sdram.v`, plus Multiface 2,
Dandanator, PlayCity, mice (`rtl/playcity`, `rtl/dandanator`, `rtl/*_mouse.v`). `clk_sys` is 64 MHz; `ce_16` is every 4th
clock, `ce_u765` every 8th (`Amstrad.sv:119-128`). Memory: one SDRAM holds ROMs and RAM; a 6128 has "64KB + 512KB"
(README) where 128 KB is the real machine. Boot ROM: `OS6128 + BASIC1.1 + AMSDOS + MF2 + OS664 + BASIC664 + AMSDOS + MF2 +
OS464 + BASIC464`, every part 16 KB (README). Frame: the CRTC is programmable; the standard PAL frame is 312 lines of 64 us
(hardware fact), 16 MHz Gate Array master clock.

- **D1 Shape (P1). A second core, its own partial, on the shared source tree T91 builds. (Decided: Q1 "b", Alex 2026-10-13.)**
  Reasons: (a) the machines need different video paths (D6): the Spectrum fits the host's 16-entry palette exactly, the CPC does not;
  (b) different memory maps and ROM sets (D5), different media (D9/D10); (c) one core would carry both machines' logic: LUT about 7,000-13,000
  (D13, sum less the shared Z80 and AY, **estimate**) against the C64 reference's 13,643, the top of which would be in the range
  that did not route in T83's measurement (88.6 % LUT failed, 78.0 % routed, T79.1 §7 as quoted in T85 D10); BRAM does not add up
  either way, because only one partial is loaded at a time; (d) T87 gives every core its own cache entry, so two small partials
  rebuild independently; (e) the only arguments for one core were one `.cor`, one app and one build. **Shared once under
  `cores/zxcpc/` and built by T91 first, reused here, never copied: `T80`, `ym2149`, the NCO (D4), the key translator (D8),
  the tape pulse engine (D9), the audio averager (D7), the register-window skeleton and identity block.** A module this core
  needs that T91 does not have is added to the tree once. CPC-only: gate array, CRTC, PPI, `u765`, the frame writer, the
  DDR2 sector service. Apps: one app per core on Gideon's base with `#ifdef`, one shared C++ part for tape-to-pulse
  conversion, key injection and ROM loading (decided for every core).
- **D2 Source and licence (P2). MiSTer Amstrad, read-only study; our own `cores/zxcpc/` and `designs/cpc/`.** What was looked at and why this one:

  | Candidate | Finding (2026-10-13) | Verdict |
  |---|---|---|
  | `MiSTle-Dev/*` | no Spectrum or CPC repository (17 repos) | does not exist |
  | `Jefffr/CPCTang` (Tang Nano 20K, CPC 6128) | source present, no licence stated, a port of Amstrad_MiSTer (README); status in its README: CPU, ROM load, video over HDMI work; keyboard, FDC, audio, OSD not implemented | not taken; its `PLAN.md` log is used as a (third-party, Gowin) size data point in D13 |
  | `MiSTer-devel/Amstrad_MiSTer` | maintained, gate array model with contention (`GA40010`), CRTC types 0/1, `u765`, mixed per-file licences, no repo LICENSE file | **taken (CPC)** |

  Per-file terms as written in each header (read, not from the README). No file I read in the CPC repository carries the
  "Replay hardware only" clause of the VC-20 files and none carries a non-commercial clause. (The Spectrum repository's
  files and terms are in T91 D2.)

  | File(s) | Terms in the header | Used? |
  |---|---|---|
  | `Amstrad.sv` | GPL v2 or later (Sorgelig) | read for wiring; the top is rewritten (HPS, `sys/`, OSD, SDRAM are framework) |
  | `rtl/T80/*.vhd` | BSD-style, Daniel Wallner 2001-2002, Sorgelig changes 2018, no extra clause | **kept** (Z80; built by T91, shared) |
  | `rtl/YM2149.sv` | BSD-3-clause wording, MikeJ 2005 + Sorgelig, no extra clause | **kept** (AY; shared with T91) |
  | `rtl/tzxplayer.vhd` | no licence text ("by Gyorgy Szombathelyi"; the CPC copy also names CoreAmstrad r004) | not taken |
  | `rtl/GA40010/ga40010.sv`, `video.sv`, `casgen.v`, `syncgen.v` | GPL v2 or later (Gyorgy Szombathelyi 2020) | **CPC, kept** |
  | `rtl/GA40010/rslatch.v`, `casgen_sync.v`, `syncgen_sync.v` | no licence text / not read | `rslatch.v` kept with the GA (it is the GA's own cell; no text, TREX-internal) |
  | `rtl/UM6845R.v`, `rtl/i8255.v`, `rtl/color_mix.sv` | GPL v2 or later (Sorgelig) | **CPC, kept** |
  | `rtl/Amstrad_MMU.v`, `rtl/Amstrad_motherboard.v`, `rtl/crt_filter.v` | "(C) 2018 Sorgelig", **no licence text** | MMU and motherboard **kept** (they are the CPC's glue; TREX-internal, D14); `crt_filter` not taken |
  | `rtl/u765/u765.sv` | GPL v2 or later (Gyorgy Szombathelyi 2017-2020) | **CPC, kept** |
  | `rtl/hid.sv`, `joydb.sv`, `sdram.v`, `playcity/`, `dandanator/`, `*_mouse.v` | GPL / not read | not taken (framework or Out) |
  | MiSTer `sys/` framework | not read | not taken |

  Handling: every kept file keeps its header; a file without licence text is kept only because the cores are TREX-internal
  (D14, as T83 D13 and T85 D1) and its row says so; releasing any of them needs the authors first. Our own modules are
  written clean (tables from the machine documentation, not copied). ROMs are never merged (D5, D11).
- **D3 Porting surface (P3).** *Kept:* `ga40010`+`video`+`casgen`+`syncgen`, `UM6845R`, `i8255`, `YM2149`, `T80pa`,
  `Amstrad_MMU`, `Amstrad_motherboard` (without Multiface 2, Dandanator, PlayCity, mice), `u765`, `color_mix` (27-ink table).
  *Replaced by host services:* HDMI, scaler, OSD, PS/2/USB (the host gives the C64 matrix), PLLs (D4), SD/HPS (`ioctl_*`,
  `sd_*`), the status/OSD bits. *Replaced by our blocks:* SDRAM/DDRAM controllers (D5: block RAM), `tzxplayer` (pulse engine,
  D9), `hid.sv` (key translator, D8), `crt_filter` (host). *Vendor-specific, to be inferred for Artix-7:* the Quartus `dpram`,
  `altsyncram`, PLL IP wrappers, `.mif` init (none needed: ROMs come from files). This list is what the first elaboration
  (S3) must confirm; anything not read in detail is dropped with the framework.
- **D4 Clocks, reset and frame lock (P4, C1).** The core stays on `sys_clk` (31.5278 MHz PAL, = 17.734475 x 16/9, the repo's
  value) with clock enables from a phase accumulator, as the VC-20 (`vc20_nco`) and the Amiga (T85 D3 b); no extra host clock.
  Ratios (computed):

  | Machine | Real clock | Enable from `sys_clk` | To hit the scaler's frame (628,992 `sys_clk`) |
  |---|---|---|---|
  | CPC | Gate Array 16 MHz, CPU 4 MHz, PSG/CRTC 1 MHz; 312 lines x 64 us | accumulator 16/31.5278 (CPU 1 enable per 7.88 clocks, jitter one `sys_clk`) | 629,550, **+0.089 %** |

  Running fast by the last column makes the machine's frame exactly the host's 50.12 Hz, so the scaler's lock holds and the
  pixel layer never has to repeat or drop a frame (the Amiga's 25 s repeat, T85 D5, does not occur here); the cost is pitch,
  speed and tape timing +0.09 %, the same trade as the VC-20's +0.17 % (T83). A CPC program that sets its own frame
  length (CRTC R4/R7) simply shows its own frame: the lock is to the output raster, see D6. The 16 MHz enable is consumed by
  logic that is slower than `sys_clk`, so no multicycle path is needed; the GA's `casgen` phases run on the enable. Reset:
  `core_rst` stretched; the Z80 starts after ROM and RAM init (D5); a model switch is a cold boot. The 60 Hz CPC is a jumper
  variant and is not in the first build (§4).
- **D5 Memory and ROMs (P5, C3).** **Everything that the Z80 touches is block RAM inside `pb_l2`; DDR2 is only for media.**
  Reason: the CPU runs at 4 MHz with memory contention built into the machine (Gate Array wait states); a DDR2
  read (about 0.4 us measured for a 32-bit client read, `ddr2_port.v` header as quoted in T85 D4) does not fit a Z80 memory
  cycle without an elastic CPU clock, which would also stretch the GA phases the contention model depends on. No
  elastic clock, no line cache, no real-time port needed.

  | Block | CPC | RAMB36 (4 KB each, **computed**) |
  |---|---|---|
  | RAM | 128 KB (6128 banks; 464/664 use 64 KB), CPU on A, GA video fetch on B | 32 |
  | ROM | OS 16 KB (lower) + BASIC 16 KB (upper 0) + AMSDOS 16 KB (upper 7) = 48 KB | 12 |
  | Small | + `u765` sector buffers (2 x 512 B), tape FIFO | 2 |
  | **Total** | | **about 46** of 50 |

  ROM images are written by the app into the ROM block RAM before the core is released, from files (D11); a model switch
  rewrites the ROM and reboots. The ROM BRAM is writable only by the app's window and is read-only to the Z80 (a write is
  ignored, as on the machine). The CPC 512 KB expansion and Multiface do not fit this budget or are Out (§4, Q3). DDR2 window
  (the host checks it, C3): tape pulse stream (whole tape converted up front, error above 4 MB), two disk images (EDSK up to
  about 0.8 MB each) and three frame buffers of 720 x 540 x 2 B = 0.78 MB each: **about 4 MB**, a window of 8 MB. Bandwidth is
  not a question (CPC picture: 38.9 MB/s write, **computed**, as the Amiga's, T85 D5).
- **D6 Video (P6, C4). The CPC uses the host's pixel layer.** (The Spectrum uses the host's palette path, T91 D6: the
  machines differ in how many colours are on screen at once and who changes them.)
  *CPC (pixel layer, as the Amiga, T85 D5).* The CPC has 27 inks of which 16 pens (mode 0) are on screen at a time, and the
  Gate Array changes pen-to-ink assignments at any CPU instruction, so rasterbars and palette flicker are standard CPC
  practice. The host's palette is written by the app over the I/O bus, not per scan line, and has 16 entries: it cannot
  follow. So the core converts ink to RGB565 itself (`color_mix`, 27 entries), packs 8 pixels into a 128-bit word and writes
  it into a DDR2 frame buffer; the host's pixel layer (`pix_layer.v`: RGB565 from DDR2, W <= 960, scale 1-3x, position, double
  buffer flip at the frame start, `T85 D5`) shows it. Format (estimate, set on the board): source 720 x 540 (a visible
  720 x 270 window = 45 us of the 64 us line, every CPC line written twice; one source pixel is one 16 MHz Gate Array
  pixel, so a mode 2 pixel is 1 source pixel, mode 1 is 2, mode 0 is 4), scale 2 = 1440 x 1080 = 4:3 as the Amiga, X0 = 240. No line buffer is needed (the writer
  emits the 8-pixel word for row 2y and row 2y+1: two commands per 8 pixels, 2 x 2.4 M commands/s, far below the controller's
  37.5 M, T85 D4). Three buffers (shown / latest / writing, `FB_BUFS = 3`, Alex's amendment of T85 Q2). The core still
  emits a black VIC-II-shaped raster so the scaler locks and the output runs (T85 D5), and because the CPC frame equals
  628,992 `sys_clk` (D4) the buffer flips once per output frame. Interlace mode (CRTC R8) is shown as non-interlaced (Out).
- **D7 Audio (P7, C5).** One AY-3-8912 (PSG in the 8255 PPI scheme, 1 MHz; `ym2149` from T91) averaged over 656 `sys_clk`
  (= 48.06 kHz, as the VC-20) into signed 24 bit with `audio_sample_en`; the host mixes. The AY's three channels are summed
  mono at first (the A-B-C stereo layouts of the MiSTer core are a setting for later, not in the first build). No filter in
  the first build; the host's own filtering stays.
- **D8 Input (P8, C6).** The host delivers `keys[63:0]`, `joy1/joy2[4:0]`, `restore` (C6); the core has one **key translator**
  (a table in LUTRAM: `(C64U key, shift state)` to the set of target keys pressed) and writes the result into the machine's own
  key matrix. *CPC (10 x 8 matrix):* letters, digits, SHIFT, CTRL, RETURN, SPACE,
  cursors, DEL/CLR as named; ESC = RUN/STOP; COPY and the keypad-ENTER on C= and left-arrow (to be confirmed at the board);
  `:` `;` `@` `[` `]` `\` `^` `-` `/` follow the printed C64 symbol the same way (table, written with the core). The CPC's joystick is
  matrix row 9 (up, down, left, right, fire 1, fire 2): port 2 -> joystick 0 (fire 1), port 1 -> joystick 1 (matrix row 6,
  the second CPC stick connector); CPC fire 2 is Out. USB keyboard and mouse through the app are not in the first build (the
  VC-20/Amiga answered "C64U matrix only", T85 Q6; same here by the task text).
- **D9 Tape (P9).** *No tape player in the core's HDL beyond a small pulse engine (T91's, shared).* The app reads `.cdt`
  (the TZX container with Amstrad timings) from SD/USB and converts the whole tape into a **pulse stream** of
  durations in 3.5 MHz T-states (an escape for long gaps) in the core's DDR2 window, with a block index (for pause blocks, "stop
  the tape" blocks and block-wise rewind). The engine in the core (about 150-300 LUT, **estimate**) reads the stream with the
  general port ahead of the playback position into a small FIFO and drives the tape input: PPI port B bit 7. The PPI motor bit
  (port C bit 4) starts and stops the stream. Real speed only (turbo is Q4); a long tape is therefore minutes, disks are the
  fast path. The 0.09 % frame stretch (D4) scales tape pulses with the T-state clock, so the loader sees the correct ratio.
  Tape SAVE (recording the CPC write line to a `.cdt`) is Q5, later: the engine would need an edge-capture ring back
  to the window and the app would write the container. MiSTer's own `tzxplayer.vhd` is not taken (D2).
- **D10 Disks (P10).** The app reads `.dsk` (standard and extended) into the DDR2 window (880 KB at most for the
  3" format; larger EDSK are refused with a message), sets drive A/B registers (mounted, base, write-protect), and an
  adapter in the core answers `u765`'s `sd_lba`/`sd_rd` requests with 512-byte reads from the window (the same shape as
  `amiga_sdc` in T85; `u765` itself parses track and sector layouts and keeps the protection-relevant behaviour, README
  "some protections"). A written sector sets a dirty bit per track; the app flushes dirty tracks to the file after an idle time
  and on eject (T85 D8: the image stays in DDR2 if the stick is gone, the flush reports on the overlay). No snapshots on the
  CPC (the `.sna` CPC snapshot format is Q3/§4).
- **D11 Registers, identity, ROM files, app and swap (P11, C2/C9).** Same rules as the VC-20 and the Amiga (T83 D8-D11, D13;
  T85 D9): all `SLOT_*`, IEC and user pins idle (the machine has none of those; no 1541), `core_id` "CPC ",
  contract version, clock table (D4), geometry notes (D6), window size (D5); a config window (model, joystick mode, AY mix,
  tape control, drive control, the key/joystick injection, reset); everything else answers "absent". The
  application is the VC-20's kind (T83 D11a): an Ultimate-app image on Gideon's base (`build_cpc.sh`,
  switch `TRX_CORE=cpc`, off by default), packed with the partial in a `.cor`, which initialises its own core
  (loads the ROMs from files, sets the tape and drive registers) and shows only what exists: reset, model,
  tape (insert / play / stop / rewind / eject), disk, joystick mode, sound, Freeze. No monitor, SID, REU, turbo,
  cartridge writer, Timewarp; the gate is the identity block (T83 D11). **ROM files** (the app reads them at core start from
  `/usb*/roms/<core>/` or `/sd/roms/<core>/`, or the flash `roms` area; never in the repository, CI or `.cor`; sizes are the
  machine's):

  | Core | File | Size | Content |
  |---|---|---|---|
  | CPC | `/roms/cpc/os6128.rom`, `basic6128.rom`, `amsdos.rom` | 16,384 each | 6128: OS, BASIC 1.1, AMSDOS |
  | CPC | `/roms/cpc/os664.rom`, `basic664.rom` | 16,384 each | 664 set (AMSDOS shared) |
  | CPC | `/roms/cpc/os464.rom`, `basic464.rom` | 16,384 each | 464 set (no AMSDOS) |

  A model is offered in the menu only if its files exist; a missing file is reported by name (S12). The MiSTer `boot.rom`
  layout (README: CPC 160 KB in the order above) is not the format; the app may additionally
  accept it as a convenience, decided at the build. The file names are this spec's choice; sizes are fixed by the machines.
  A swap is a cold boot, nothing of the previous core survives (T79 decoupler).
- **D12 Freeze (P11, C8). Stop everything at a clock edge.** Alex, 2026-10-13: "when I
  choose FREEZE it MUST freeze. Like the C64." The core takes the contract's machine pause (`c64_halt_req`, T85 Freeze).
  All enables (CPU, GA, CRTC, AY, tape engine) are held at a clock edge (no lock concern: the
  pixel layer is fed by the writer, which finishes its words and goes quiet; the black VIC-shaped raster keeps running);
  `T80` and the GA advance only on their enables, so the held state is exact. `paused` is raised when the enables have been
  held and the tape FIFO refill and the window access are quiet (as T85's `quiet` chain); the layer is switched off for the
  menu as for the Amiga, and the audio ramps to zero. The tape engine keeps its position, so a tape resumes exactly. A
  reset takes the pause away. Verified by the T85 method (S13).
- **D13 Size and timing (P12).** **Estimate, no measurement exists.** The MiSTer repo publishes no resource figures;
  nothing here has been synthesised for Xilinx. Basis and range:
  *Third-party data point (measured by its author in Gowin LUT4, not reproduced):* CPCTang's `PLAN.md` logs 3,255 LUT4 /
  1,222 FF / 1 BSRAM with the Z80, gate array, CRTC, MMU and PPI connected to a 64 KB RAM, and 4,548 LUT4 / 2,027 FF / 3 BSRAM
  with the HDMI scandoubler added; its own budget table lists the T80 at 1,500-2,000, the `u765` at 1,500-2,000 and the whole
  motherboard at 8,000-10,000 (an upper estimate it did not reach). I assume 0.6 to 1.0 Xilinx LUT6 per Gowin LUT4 (as T85
  D10, **assumption**, no source).

  | Block | CPC (LUT, estimate) |
  |---|---|
  | Z80 (`T80pa`, from T91) | 1,000-1,800 |
  | GA + CRTC + MMU + PPI | 1,400-2,400 |
  | AY (`ym2149`, from T91) | 400-700 |
  | `u765` FDC + DDR2 sector adapter | 900-2,000 |
  | Frame writer, `color_mix` | 300-600 |
  | Glue: NCO, key translator, joystick, tape engine + DDR2 client, audio averager, registers + identity, palette/pixel stream | 1,200-2,100 |
  | **Total** | **about 5,200-9,600** |
  | of `pb_l2` (18,000) | 29-53 % |
  | of the C64 reference (13,643) | 38-70 % |

  FF about equal to LUT or less (CPCTang: 0.37-0.45 per LUT4). DSP: 0 of 80 (T80's ALU and the AY volume tables are LUT logic;
  not verified). BRAM: **about 46 of 50** (D5, computed); the C64 reference uses 39, so the CPC's BRAM is denser than the
  reference (risk R1). Timing: the logic runs on `sys_clk` (31.5 MHz) with 4-16 MHz enables, far
  below T83's and T85's cases (35.5 and 28.4 MHz single-cycle paths); the edge, not the logic, is the risk, as for the VC-20.
  The first out-of-context synthesis (S14) decides; the host does not change, so layer 1 is not touched.
- **D14 What follows from earlier decisions (no new decision):** TREX-internal only (T83 D13, T85 Q1): private Gitea, never in
  a release, update package or public download, behind its own switch; ROMs never in the repository, CI or `.cor`, the app loads
  them from `/roms/<core>` (T83 D4); plain machine, no TRX64 ring/monitor/snapshots, but Freeze works like the C64 (T83 Q6 changed
  2026-10-13); PAL first (T83 Q5); the app is on Gideon's base, one app per core (`#ifdef`); keyboard from the C64 matrix,
  joysticks from the C64 ports; video through the pixel layer (D6); media served by the app from SD/USB; one
  core clock family from the host (T79.1 C1); peripherals are the host's; the register bus is 8 bits and carries control only,
  bulk data (pulse stream, disk image, frame buffer) goes through the memory ports (T79.1:48); a swap is a cold boot.

**Risks (named, not decided).**

- **R1 CPC BRAM at about 46 of 50, against 39 in the reference.** BRAM does not cost routing as LUT does, but the columns lie
  inside one pblock and T87 says a core must not be denser than the reference. Levers in order: no `u765` second sector buffer
  (share one), tape FIFO in LUTRAM, the 664/464-only variant (64 KB RAM = 16 tiles; Alex's scope, Q3). 128 KB RAM and 48 KB of
  ROM are physical; the 6128 cannot be made smaller.
- **R2 Lock relearning.** Running +0.09 % fast (D4) depends on the scaler holding its lock; T83
  lost the position twice (D16, D16a). The host's relock on release exists (dfx-trial a5f8252, T79 history) but is not relied on.
- **R3 CPC cycle exactness.** The Gate Array / `casgen` model assumes its own 16 MHz phases; our enable is a 31.5 MHz grid
  with one-clock jitter (D4). Logic sees only the enable, so it is exact; whether demos that depend on exact CPC cycle counts
  run is a by-hand check (S6, board) with user-supplied programs. The MiSTer behaviour is the reference, not a real CPC.
- **R4 Tape fidelity.** Pulse stream plus real-speed playback: TZX blocks that are not pulse sequences (direct recording,
  CSW, generalised data) are converted by the app; unsupported block types are refused with a message, not skipped silently.
- **R5 Z80 core.** `T80pa` is the shared Z80 of both MiSTer cores and has a known undocumented-flag and block-instruction
  history (Sorgelig's 2018 fixes); S3 runs ZEXALL by hand to find out. A cycle-accurate alternative (A-Z80, GPL) is bigger
  and not read.

**Open questions for Alex (one line each, answered; carried over from the combined spec, T91 numbers in brackets):**

1. Q1 (T91 Q1): One core with a Spectrum/CPC personality switch (about 7,000-13,000 LUT in one partial, one `.cor`), or two cores (smaller partials, own caches, the CPC gets T92)?
   **Answered (Alex, 2026-10-13):** "b": two cores (two partials, shared modules in one source tree); the CPC gets T92.
2. Q2 (T91 Q2): Which first: the Spectrum (smaller, palette path, no DDR2 video, no disk) or the CPC (pixel layer, FDC, 46 BRAM)?
   **Answered (Alex, 2026-10-13):** "a, and then the CPC in its own spec please": the Spectrum first; the CPC moves into its own spec T92 (split out of the combined one).
3. Q3 (T91 Q4): CPC models and extras in the first build: 464, 664, 6128 (selectable, ROM sets from files), two DSK drives with write-back, CDT, AY, two joysticks. Out unless you say otherwise: 512 KB expansion RAM, Multiface 2, Dandanator, PlayCity, mice, CPC+ (ASIC/GX4000), CPC snapshots.
   **Answered (Alex, 2026-10-13):** "a": as proposed: 464/664/6128 selectable, two DSK drives with write-back, CDT, AY, two joysticks; the listed extras stay out.
4. Q4 (T91 Q5): Tape loading in the first build: real speed only (snapshots and disks as the fast path), or also a turbo/fast load for the standard ROM loader (MiSTer's direct byte injection: needs the core to watch the ROM loader's address)?
   **Answered (Alex, 2026-10-13):** "a": tape loads at real speed only in the first build (Spectrum and CPC); snapshots and disks are the fast path.
5. Q5 (T91 Q6): Tape SAVE (record the machine's tape output to a `.tap`/`.cdt` on the stick) in the first build, or later?
   **Answered (Alex, 2026-10-13):** "b": tape SAVE (.tap/.cdt) later, not in the first build.
(T91 Q3, the Spectrum's scope, stays in T91.)

## §4 Out

What this spec does not do, and why. Anything listed here is decided with Alex by the questions above or by the earlier
decisions in D14; nothing is left out by omission.

- **NTSC / 60 Hz machines.** PAL first (T83 Q5). The CPC 60 Hz variant is its own step.
- **Real cartridges, expansion ports, tape deck, light pen, the real 3" drive, printer, serial/user port pins.** The C64U's
  ports do not match; all `SLOT_*`, IEC and user pins stay idle (D11). The media are files served by the app.
- **ZX Spectrum.** T91 (`T91-zx-spectrum-core.md`).
- **Everything the source has beyond the machine** (Q3): CPC 512 KB RAM, Multiface 2, Dandanator, PlayCity,
  mice, ASIC palette, CPC+ (ASIC/GX4000), CPC snapshots, interlace mode. Each is a later step with its own scope decision.
- **A USB keyboard or mouse through the app,** AY stereo layouts, scanline/CRT filters (the host does the scaling), and
  turbo CPU speeds beyond the machine's own.
- **TRX64 ring, monitor, Timewarp, snapshots of the machine state** (plain machines, T83 Q6). Freeze (D12) is in.
- **A host change.** The host is routed with the slim C64 (T87 D15/D16); the core asks for nothing of it (the pixel layer
  path exists).
- **Clean-room limits.** Gideon's UltiSID, Copper, lp_filter and sampler sources and the VHDL of `ultisid_on_mister_v77` are not
  opened for this work.
- Tape (CDT) for now (Alex, 2026-10-28, TODO review: "Passt" to the proposal to drop it until someone needs it).

## §5 Decisions taken while building

(Written 2026-10-22 on branch `t91-t92-cores`; numbers are bench output lines of this day, `sh cores/cpc/sim/run.sh <modes>` as `ci/sims.txt` names them. The
shared parts, the application's tape deck and the ROM aperture finding are in T91 §5.)

- **2026-10-22, what exists.** Gateware `cores/cpc` (the Gate Array, CRTC, PPI, MMU, ink table and FDC vendored in `rtl/` and `gen/`, our parts in `trx/`, the
  Spectrum core's Z80, AY and shared parts, `layer2_cpc.v`), four bench rows and the pin check, the application side (`coreapp/cores/cpc.cc`, `cpc_disk.*`,
  `cpc_regs.h`, the tape deck of T91), the logo. Not done and not measurable here: routing (S14, CI's), the board.
- **2026-10-22, scenarios, where each is green.**

  | S | Proof | Result (source line) |
  |---|---|---|
  | S1, S2 | `ci/lint.sh` | one copy of the shared modules; no ROM, media image or snapshot in the tree |
  | S3 | bench `cpc_cpu` | `PASS z80s: 254 result bytes equal the expected ones after 0 frames (2 known deviations)` |
  | S4 | `cpc_video` | `PASS clk: the CRTC frame = 628992 sys_clk, 319,488 Gate Array clocks (312 lines x 1,024), lock event every frame` |
  | S5 | `cpc_cpu` | `PASS page`, `page664`, `page464` (each with `window: RAM banks and ROM markers read through the app's window equal the Z80's`), `PASS model 6128` / `model 464` |
  | S6 | `cpc_cpu` | `PASS slots: 82 opcode fetches of 34 instructions (twice) fall on the T-states the 1 us slot rule gives`; control `mut_nowait` fails |
  | S7 | `cpc_video` | `PASS video: modes 0, 1 and 2 ... equal the CPC's rules in all 256,000 samples of each mode`, `PASS bars: an ink change in every raster line shows on its own line`; the 5-minute board run is open |
  | S8 | `cpc_io` | `PASS audio: the AY tone period is within 0.3 % of the programmed value` (-0.002 %), silence exactly 0 |
  | S9 | `cpc_io` | `PASS keys: 701 key sets (matrix, joysticks, injection) read back exactly as the table lists them`; control `mut_keyrow` fails |
  | S10 | `cpc_io` and `tests/coreapp` | `PASS tape window: the motor bit stops and restarts the stream, block A loads at tape speed and passes its XOR, block B (one bit flipped) is refused`; control `mut_tape` fails |
  | S11 | `cpc_io` and `tests/coreapp` | `PASS disk: the FDC read a sector of the DSK image from the DDR2 byte-exact, wrote one back ..., marked its 16 KB chunk dirty, the app cleared it`; the application's write-back: `test_cpc` |
  | S12 | `test_cpc`, golden `menu_cpc.txt`, bench `cpc_pins` | the menu holds reset, model, CRTC, joysticks, disks A:/B:, tape and nothing of the C64 machine; `PASS pins` |
  | S13 | `cpc_pause` | `PASS pause hashes: all 24 frames of the paused run equal the reference run's`; control `mut_nohold` fails |
  | S14 | CI core stage | open: the first route (the line buffer's last measurement on the t92-core branch: pb_l2 8,075 LUT, 46.5 BRAM tiles, commit 22f03c26) |

- **2026-10-22, D11 is superseded: the application is the generic one** (as T91 §5): `app = "blueprint"`, `coreapp/cores/cpc.cc`, `cpc.app` from `ci/trxapps.sh`.
- **2026-10-22, the core id is `CPC_`, not `CPC ` (a deviation from D11).** `desc_check` takes four printable characters (a space is none), the NES's is `NES_`. Changed in
  `cpc_regs.v` and the JTAG word of `cpc_core.v`.
- **2026-10-22, the ROM area is written bank by bank** through the host's 16 KB aperture (WIN_PAGE b2:1 `ROM_SEL`: 0 lower ROM, 1 upper ROM 0, 2 upper ROM 7), see T91 §5.
- **2026-10-22, the picture is centred (the 2026-10-19 decision, built now).** `PIX_X0` 152 -> 168: the paper of 640 columns has 40 border columns each side of the 720
  (`video: mode 0 paper starts at window column 40`, mode 1 the same, mode 2 `39`: the one-dot shift stays). The bench `video` asserts the column (40, mode 2: 39);
  the first rerun after the edit still showed 56 because the Verilator binary was stale (`ZX_REBUILD=1` made it new: a changed RTL does not rebuild a bench's binary by itself).
  Y0 is untouched (paper at line 34 of 270, 36 below).
- **2026-10-22, ROM sets (D11).** Files of one model: 464 `os464.rom` + `basic464.rom`; 664 `os664.rom` + `basic664.rom` + `amsdos.rom`; 6128 `os6128.rom` + `basic6128.rom` + `amsdos.rom`
  (16,384 bytes each; the 664 and the 6128 share one AMSDOS, so a ROM may belong to two sets: `RomSpec.set` is a mask). One complete set starts the core; the "Model"
  setting is a static list, so a model whose files are missing falls back to the highest complete one and the screen says so (T91 §5 for the reason).
- **2026-10-22, disks (D10).** `.dsk` (header `MV - CPC` or `EXTENDED CPC DSK`; anything else, under 256 bytes or over 880 KB is refused) is copied as it is into slot A (window
  `0x000000`) or B (`0x100000`); registers: base, size, then DISK_CTRL with the mount bit and the write-protect bits of both drives (a read-only file or the setting "Disks write
  protected" sets it). The poll takes the core's dirty bits (write-1-to-clear, one bit per 16 KB chunk), and writes the chunks to the file after 1 s without a new mark or
  after 5 s of marks; the last chunk is short. A write that fails is told once on the screen, the chunks stay, a retry follows every second; eject and a mount over a
  mounted drive are refused until it is written; a reset (hook `init`) flushes first. A 464 has no disk drive: the mount says so; no `amsdos.rom`: the mount says so.
  "Mount A: and reset" is the first action (it ends in a cold start), "Mount A:" and "Mount B:" swap the disk of a running machine.
- **2026-10-22, tape (D9).** `.cdt` through the deck of T91 §5 (a CDT is a TZX), the same menu entries; no motor setting is needed (the PPI's motor bit gates the stream in the core).
- **2026-10-22, settings.** Model (at reset, default 6128), CRTC type (at reset, default UM6845R, the core's reset value), Joystick ports (normal, swapped), Disks write protected.
  No AY mix (D7). Video descriptor: 720 x 540, scale 2, x0 240, three buffers of 1 MB from `0x01D00000` (the core's reset value, the application writes it too).
- **2026-10-22, `core.toml`.** `order = 12`, contract 2, `experimental = true`.
- **2026-10-22, open.** As T91 §5: the first route, the missing `ignore:` lines for the sim directories in `host/layers.txt` (host file, not touched), the board. Also: the geometry
  `x0 = 240` of the layer on the 1080p frame is the spec's estimate (D6), not yet seen.
- **2026-10-24, board: `CAT` hangs with a mounted disk; not reproduced in the bench (cause open).** Board (Alex + JTAG, release trx64mc-main-54e63c47): BASIC Ready, a DSK in A:
  (a game DSK, 204,544 bytes, and a blank one from T122, 194,816 bytes), `CAT` never returns, also with the disk in at reset; JTAG: status 0x08, frames running, PC cycling
  C95E-C962, bus FB7E. The `amsdos.rom` copied from the board shows the loop: `C95E in a,(c)` / `add a,a` / `jr nc,C95E` with BC = $FB7E, i.e. AMSDOS waits for
  bit 7 (RQM) of the main status register before writing a command byte, with no time-out. In `u765` (gen/u765.sv, state COMMAND_IDLE) RQM is
  `!image_scan_state[0] & !image_scan_state[1]`: while the image header scan runs (it reads sector 0 of the image through `sd_rd`, i.e. through `cpc_fdc` and the DDR2 port)
  the FDC answers "not ready" and ignores commands. So on the board either the scan never ends (the DDR2 read of the image does not come back, `cpc_fdc` stays busy) or
  the FDC left COMMAND_IDLE. Bench: new manual mode `rom_cat` (`sh cores/cpc/sim/run.sh rom_cat`, `CPC_ROMDIR` = the three ROM files, never in the repo; `tools/cpc_progs.py ddr40`
  makes the blank 40 track DSK as `newdisk.cc` does): the real 6128 OS, BASIC and AMSDOS ROMs, the DSK in the DDR2 model, `CAT` typed by the key-injection registers. Three runs
  (builder, ~10 frames a minute): mount at reset; mount into the running machine (frame 135); warm reset with the disk in. In all three AMSDOS runs recalibrate (07), sense
  interrupt (08), read ID (4A) and Read Data (66) against sectors C1.. of track 0, the scan ends within the first frames (`scan 0`, ready 1), `DDR reads 256` and 416.
  So the RTL as simulated is not the cause; the difference is on the board only (the host's DDR2 port with refresh and the other clients, synthesis of `u765`'s dual-port RAMs
  and track-offset RAM, or the app's mount sequence). **Not proven on the board.** To split it, read during the hang: register 0x191 `DISK_STATUS` (bit 0 = `cpc_fdc`
  busy: 1 means a DDR2 read of the image never returned, 0 means the FDC itself is waiting; bit 1 = a block beyond the image was asked for) and the first bytes of the
  image in the DDR2 at `0x01000000` (`MV - CPC`/`EXTENDED`).

- **2026-11-01, CAT hang (GitHub issue 17, main-922deb78, game disks, "insert and reboot" or insert then CAT) and sound; T164 work package (Claude).**
  **Cause of the hang not found.** No CPC ROM is on this machine, so `rom_cat` was not rerun. New bench `fdcscan` (`sim/tb_fdcscan.v`: `u765` + `cpc_fdc` + the DDR2
  model, no machine; mode `fdcscan`, CI bench `cpc_fdcscan`): the image scan after a mount with the machine's reset 3..1000 clocks after the mount (24), reset before
  the mount (24), reset anywhere in the scan (40), plain (1) and a `ui_rst` pulse of 4 ui clocks at every 25th sys clock of the scan (80): `PASS fdcscan: 169 of 169
  mount/reset/ui_rst timings ended with the block service idle and RQM up`. The same scan through the host's real `mig_arb` (`mono/host/ddr2_port.v`) with a MIG model
  (commands taken half the clocks, 15-45 clock read latency in order) and video reads in the background: 89 of 89 (scratch run, not committed). So the scan and the
  block service are sound under reset/mount timing and under the real arbiter; the board difference stays open (synthesis of `u765`'s RAMs, or something the sim does not model).
  **Found while probing:** a `ui_rst` pulse while a line is in flight left `cpc_fdc` in `S_RWAIT` with `sd_ack` high for good (26 of 80 offsets): `u765` never got another
  block, a later READ DATA would sit in its execution phase with RQM low and AMSDOS's next command byte would spin at `C95E`. `cpc_fdc` now follows `ui_rst` (3 FF
  synchroniser into its reset). Control `mut_fdcscan` (`CPC_MUT_UIRST`): `FAIL fdcscan: 143 of 169 ...` -> `PASS control mut_fdcscan`. Not shown to be the board's cause (no
  `ui_rst` pulse while a core runs was found in `host/top.v`: `ui_rst | dc_ui`). `disk` / `mut_disk` / `mut_led` unchanged: PASS / control PASS / control PASS.
  **Still needed on the board:** the 0x191 `DISK_STATUS` read from 2026-10-24 (bit 0: `cpc_fdc` busy = a DDR2 read never came back; 0 = the FDC itself waits).
  **Sound:** no symptom is written down anywhere (TODO said only "sound"; 2026-10-22: "not tried yet"). Measured instead: `audio`: `PASS audio: the AY tone period is within 0.3 %
  of the programmed value ...` (-0.002 % own, -0.090 % against the nominal 1 MHz AY clock), silence exactly 0. Design points, not defects: mono sum of A+B+C (D7; the CPC
  wires A left, C right, B both), x32 scale (three full channels 24,480). No defect found; a board listening test is the open step.

- **2026-11-01 (later), CAT hang with the real ROMs: not reproduced, observation window too short (Claude, T164).** ROMs from MiSTer's `boot.rom` (OS6128, BASIC 1.1,
  AMSDOS; kept out of the tree, builder scratch only, deleted). `rom_cat` (blank 40-track DSK, `+MOUNTAT=135 +RESET2 +OBS=100`: mount into the running machine, CAT,
  warm reset, CAT; 104 minutes of wall time, builder load 8-17) printed, per pass: `pass 0 end frame 335, pc c6e6, FDC main status 50, scan state 0, ready 1, FDC data
  writes 41, read-data commands 4, DDR reads 256` and `pass 1 end frame 625, pc fb7e, FDC main status 50, scan state 0, ready 1, FDC data writes 32, read-data commands 3,
  DDR reads 416`. The machine booted to BASIC, AMSDOS issued Recalibrate (07), Sense Int (08), Read ID (4a) and Read Data (66 00 00 00 c1 02 ...) in both passes, the scan
  finished (`scan state 0, ready 1`), u765 logged `sector found` for C1, C2 ... and the DDR2 reads went 256 -> 416 (the block service is alive). The bench's FAIL line is
  its window: CAT was typed at about frame 255, the Read Data command came at frame 307 and the window closed 28 frames later, with the FDC in the execution phase
  (status 50 = busy, DIO to the CPU, RQM low between bytes), which is normal. No hang is shown and none is excluded; the board-timing run needs `+OBS=500` or more
  (about 3 h at this load; the Verilator log is block buffered, so there is no progress to read: run it under `stdbuf -oL`). Added: `CPC_DSK=game sh
  cores/cpc/sim/run.sh rom_cat` takes a game-style Extended DSK (`tools/cpc_progs.py ddrgame`: 42 tracks, skewed sector order, 8 directory entries, 204,544 bytes) instead
  of the blank one (no game image is in the tree); not run yet. Still needed: the long `rom_cat` runs (blank and `CPC_DSK=game`) and, on the board, the 0x191 `DISK_STATUS` read
  during the hang (bit 0 busy: a DDR2 read never came back; 0: the FDC itself waits).

- **2026-10-19, the picture is centred (Alex: "Centre it", translated).** Branch `t92-core` calibrated the window at
  X0=152/Y0=31: the paper starts at column 56 of 720, leaving 56 columns of border left and 24 right. The window moves
  so both borders are equal; mode 2's one-dot earlier start (column 55 in the vendored Gate Array) stays inside that.
- **2026-10-19, mode 2's one-dot shift stays (Alex: "Leave it", translated).** In mode 2 the picture starts one dot
  earlier than in modes 0 and 1 (vendored Gate Array); not corrected.

---
<!--
Closing: Status → DONE (date) + which scenarios are green where; TODO row ✅ and history entry,
in the same commit as the code.
-->
