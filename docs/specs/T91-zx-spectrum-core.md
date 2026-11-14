# T91 — A ZX Spectrum core, ported from the MiSTer core, run as a swappable core in the T79 host/core split

**Status:** BUILT, benched, application written (2026-10-22, §5); decided 2026-10-13 (Q1-Q6 answered, §3 end). Not routed, not on a board.
**Status check 2026-11-13:** MERGED into T119 (2026-10-28, bed3d6e54; TODO row T91 is merged): built and benched, on the board since 2026-10-22/23 (picture path through the DDR2 frame buffer, black screen after a short white); the open board finding lives in T119.
**Repo:** TRX64-Ultimate (gateware under new `designs/zxspectrum/` plus shared parts under `cores/zxcpc/`, the core's
application on Gideon's base as for the VC-20 and the Amiga, tools). The sources are read from outside (MiSTer-devel);
nothing is copied into this repo by this spec.
**Number:** T91. Split 2026-10-13 (Alex, Q1/Q2): this spec is the **ZX Spectrum only**; the Amstrad CPC is its own spec,
T92 (`T92-cpc-core.md`, branch `t92-cpc`), which depends on this one.
**Milestone:** M3 (other cores; `docs/other-cores.md`). Alex's call.
**Origin:** Alex, 2026-10-12 night: position 8 of the core order is "ZX Spectrum / Amstrad (Schneider) CPC" (`docs/todo/T79.md`);
2026-10-13: "write the spec"; "the Nano cores are already there" (checked: they are not, see D2); Q1/Q2 answered: two cores,
Spectrum first, "and then the CPC in its own spec please".
**Depends on:** T79 (host/core split, contract `T79.1-host-core-contract.md`), T83 (VC-20: the precedent for ROMs from files,
the VIC-II-shaped picture and the app on Gideon's base), T85 (Amiga: Freeze), T84 (`.cor`), T87 (D15/D16: the slim C64 is the
reference every core must route against). Floorplan B (`pb_l2`) must stay as measured.

Template: `docs/specs/_TEMPLATE.md`. Citations: `T79.1:N`, `T83:N`, `T85:N` are lines of those specs on their branches
(`dfx-trial`, `origin/t83-vc20`, `origin/t85-amiga`, read 2026-10-13); `layer2.v`, `vic_scaler.v` and `T59-palette.md`
are read on this branch (`t91-zxcpc`, from `integ-m1` 73e5f20). Source files are from the scratch clone
`specsrc/ZX-Spectrum_MiSTer` (33774ad, 2026-10-12), read 2026-10-13. (The CPC's clone is cited in T92.)
**(source)** = read in a file named next to it. **(computed)** = arithmetic on stated numbers. **(estimate)** = derived,
not measured. **(measured)** = taken from a report or a run. **(hardware fact)** = published machine documentation, not a file.

---

## §1 The problem

**Who:** Alex, who wants the C64 Ultimate board to be a host for many machines (core order, `T79.md`, 2026-10-12 night:
position 8 = "ZX Spectrum / Amstrad CPC"; split into T91 and T92 on 2026-10-13).

**What he wants:** pick "ZX Spectrum" in the core selector (T81/T84), the board swaps layer 2 by partial reconfiguration,
and the machine boots to its BASIC with the C64U's keyboard and joysticks, picture, sound, a tape to load from and a
snapshot to start.

**Today:** two cores exist or are being built against the contract (VC-20 T83, Amiga T85); the reference every core must
route against is the slim C64: 13,643 LUT, 4,300 slices (96 % of `pb_l2`'s 4,500), 39 BRAM tiles (T87 D15/D16, quoted from
`docs/specs/T87-ci-layer-caches.md` §5). `pb_l2` has 18,000 LUT, 4,500 slices, 50 RAMB36, 80 DSP (task text, T83 D12). No
Z80 machine exists in the repo. The edge still has the VIC-II shaped video (`pix_color[3:0]`, `pix_x[9:0]`, `pix_y[8:0]`,
T83 D5); a 16-entry, app-written, 24-bit palette sits behind the 4-bit colour index (`vic_scaler.v` ports
`pal_we/pal_addr/pal_wdata`; `T59-palette.md` §3).

**What the source is.** There is **no Spectrum core in MiSTle-Dev** (Till Harbaum's organisation has 17 repositories on
2026-10-13: C64Nano, VIC20Nano, C16Nano, NanoApple2, A2600Nano, NanoMig, NanoMac, MiSTeryNano, MiniMig, snescore and
tooling; GitHub API listing, checked by this spec). "The Nano cores are already there" holds for the VC-20, C16, Apple II
and Amiga, not for the Spectrum. The maintained lineage is MiSTer-devel: `ZX-Spectrum_MiSTer` (Sorgelig, built on parts of
Till's MiST Spectrum core, README). A third-party Tang Nano 20K port exists and is looked at in D2.

**Why it matters:** the Spectrum is a Z80 machine on Alex's list, small enough for `pb_l2` (D13) and with no DDR2 use for the
CPU, which makes it the cheapest core after the VC-20. It tests two things the earlier cores did not: a Z80 (T80) with a
contention-driven memory system, and a core that serves tapes as pulse streams. The shared modules it builds first
(T80, YM2149, NCO, key translator, tape pulse engine) are what the CPC core (T92) reuses.

**Break the problem into parts, MECE** (§2 is checked against this list):

- **P1 Shape.** One core or two; what is shared.
- **P1 Shape.** Two cores (Q1); what is shared with the CPC core T92.
- **P3 Porting surface.** Kept, replaced by host services, replaced by the app.
- **P4 Clocks, reset and frame lock** (contract C1).
- **P5 Memory and ROMs** (C3 and the app's ROM path).
- **P6 Video** (C4).
- **P7 Audio** (C5).
- **P8 Input** (C6): keyboard, joysticks.
- **P9 Tape.**
- **P10 Snapshots.**
- **P11 Registers, identity, app, ROM files, swap and Freeze** (C2/C8/C9).
- **P12 Size and timing** against `pb_l2` and the C64 reference.

## §2 Scenarios — what proves it is solved

Machine: **ZX** = ZX Spectrum core. Benches are named after the existing layout (`designs/c64_basic/sim/<core>/`);
all of them are **to be written**. Test programs are our own Z80 sources (assembled by a script in `tools/`); nothing
third-party is checked in, and no ROM is in the repo, CI or a `.cor`. Third-party suites (ZEXALL, Fuse tests) are
run by hand on the board or in simulation from files the user supplies.

```gherkin
Scenario: S1 the shape is decided and bookkept before gateware starts (P1)
  Given Q1 (one core or two) is answered by Alex
  When  the first line of gateware is committed
  Then  §3 D1 carries the answer, the TODO table has one row per core (T91, T92), and the shared modules live once under cores/zxcpc/
  And   no module (T80, YM2149, key translator, tape engine) exists as two copies in the tree

Scenario: S2 the licence list exists and nothing blocked is taken (P2)
  Given the per-file table in D2
  When  a file is merged under cores/ or designs/
  Then  its header is kept and its row exists; keyboard.sv (non-commercial clause) and every ROM byte are absent
  And   ci/package.sh output contains no file whose path matches zx or cpc (TREX-internal, T83 D13)

Scenario: S3 the core elaborates on Xilinx and a Z80 test ROM passes in simulation (P3)
  Given the kept modules, inferred RAM, MiSTer framework (HPS, sys/, SDRAM, PLL) removed
  When  the core is synthesised out of context for xc7a50t and simulated running our Z80 test ROM
  Then  Vivado reports no black box; the ROM's checks (flags, block moves, IM2, R register, DAA, undocumented flags) read back right
  And   the TRX64, VC-20 and Amiga builds are unchanged

Scenario: S4 the clocks are exact and the frame is the scaler's frame (P4)
  Given the identity entry of each machine (ratios in D4)
  When  the app releases core_rst
  Then  the ZX frame = 628,992 sys_clk, counted by the ring counter on the board
  And   the T-state clock is within 0.21 % (ZX 128K), 0.09 % (ZX 48K) of the real machine and the scaler stays locked for 10 minutes

Scenario: S5 paging and ROM banking behave as on the real machines (P5)
  Given the BRAM RAM and ROM of each core, filled by the app from files
  When  our test ROM pages every RAM bank (ZX port $7FFD) and reads the ROM windows
  Then  each bank keeps its own data, the shadow screen (ZX bank 7) displays, ROM selection returns the right image
  And   a model switch (ZX 48K/128K) rewrites the ROM BRAM and the machine cold-boots into the new model

Scenario: S6 contention is the real contention (P5)
  Given the ZX core in 48K and in 128K mode
  When  a timed loop reads $4000 and $8000 (our test ROM, border colour toggled by frame)
  Then  the T-states per frame differ by the published pattern (48K: delays 6,5,4,3,2,1,0,0 from T-state 14335 of each line, hardware fact)

Scenario: S7 the Spectrum picture is right (P6)
  Given the ZX core on the HDMI output with the app-written 16-entry palette
  When  our test screen runs (15 colours, bright, FLASH, border bands changed per raster line)
  Then  the frame hash of the colour-index stream in simulation matches the expected image and FLASH toggles every 16 frames
  And   the picture on the board (Alex's photo) is centred, undistorted, 4:3 on the 1080p frame

Scenario: S8 sound (P7)
  Given beeper (ZX), AY-3-8912 (ZX 128K)
  When  our test ROM plays a beeper tone, an AY tone and a noise
  Then  the audio stream's tone periods are within 0.3 % of the programmed value (after the frame lock) and noise has no tone peak
  And   the output is exactly 0 while Freeze holds

Scenario: S9 keys and joysticks reach the machine (P8)
  Given keys[63:0], joy1, joy2 from the host
  When  each mapped C64U key is pressed (table in D8) and each joystick direction and fire
  Then  our test ROM reads the matching ZX half-rows (and Kempston bits) and nothing else
  And   shifted combinations (Symbol Shift) inject exactly the key set the table lists

Scenario: S10 a tape loads at real speed and stops when told (P9)
  Given a TAP/TZX written by our tool (content ours, standard ROM loader format)
  When  the app converts it to the pulse stream and the user types LOAD ""
  Then  the program loads, the loaded bytes read back equal to the tape's data through the DMA window, and it starts
  And   a bad checksum block gives the ROM's "R Tape loading error"

Scenario: S11 snapshots and disks (P10)
  Given SNA (48K, 128K) and Z80 (v1, v2, v3) snapshots made by our tool
  When  the app loads the snapshot
  Then  registers, I/R, IFF, IM, SP, PC, paging port and all RAM equal the file and the program runs on
  And   a snapshot that is not a 48K or 128K machine is refused with a message

Scenario: S12 the app offers what exists and nothing else (P11)
  Given a ZX core loaded (identity "ZXSP")
  When  the user opens the menu and calls REST
  Then  reset, model, tape, snapshot, joystick and sound items exist; monitor, SID, REU, cartridge writer, Timewarp and C64-address machine:readmem are absent
  And   a missing ROM file shows its name on the overlay and the core is not started (no silent black screen)

Scenario: S13 Freeze stops the machine and resumes exactly (P11)
  Given a running ZX machine
  When  the user chooses Freeze (menu open, 62 ms to minutes) and resumes
  Then  the machine stood still (no CPU clock, no frame, no AY/beeper, tape position unchanged) and the resumed run is cycle-identical to a never-paused run (hash per frame, as T85 cmp_pause)
  And   the picture lock holds after resume (no position error; T83 D16/D16a)

Scenario: S14 the core fits and routes against the locked host (P12)
  Given the T87 CI with the slim C64 host
  When  the core is built as a partial
  Then  out-of-context synthesis reports LUT within D13's range and BRAM <= 47 tiles, and the partial routes with WNS >= 0
  And   the swap round trip ZX -> C64 -> ZX works; the C64 reference still routes (host unchanged)
```

| # | Solves | Scenario | Test (bench / CI stage / board script) | Where it runs |
|---|---|---|---|---|
| S1 | P1 | shape recorded, no duplicate modules | `tools/check_todo.py` + a tree check in `ci/lint` (to be written) | CI `lint` |
| S2 | P2 | licence table, no blocked file, no ROM | `ci/package.sh` self-test row + repo scan (to be written) | CI `package` |
| S3 | P3 | elaboration + Z80 test ROM | `designs/c64_basic/sim/zxspectrum/run_z80.sh` → `ci/sims.txt` row | CI `sims`, `synth` |
| S4 | P4 | clocks and frame count | `sim/zxspectrum/tb_clk`; board: frame counter over REST/JTAG script | CI `sims`; board (autonomous) |
| S5 | P5 | paging, ROM banking, model switch | `sim/zxspectrum/tb_page` with our `paging.asm`; board script reads RAM through the DMA window | CI `sims`; board |
| S6 | P5 | contention | `sim/zxspectrum/tb_contend` (T-state counters in the bench) | CI `sims` |
| S7 | P6 | Spectrum picture | `sim/zxspectrum/tb_video` frame hashes; board photo (Alex) | CI `sims`; board, by hand (one photo) |
| S8 | P7 | sound | `sim/zxspectrum/tb_audio` (period measurement) | CI `sims` |
| S9 | P8 | keys and joysticks | `sim/zxspectrum/tb_keys` against the D8 table; board: typed line, photo | CI `sims`; board |
| S10 | P9 | tape, error | `tools/mktape.py` (our tape) + `sim/zxspectrum/tb_tape`; board script | CI `sims`; board |
| S11 | P10 | snapshots | `tools/mksnap.py` + `sim/zxspectrum/tb_media`; board script | CI `sims`; board |
| S12 | P11 | app gate, missing ROM | app unit test (host build of the app); board script reads the menu state over REST | CI `app`; board |
| S13 | P11 | Freeze | `sim/zxspectrum/run_pause.sh` (as `sim/amiga/run_pause.sh`, `cmp_pause.py`) | CI `sims` |
| S14 | P12 | size, routing, swap | T87 core stage reports; board script for the swap round trip | CI `synth`/`bit`; board |

Every scenario solves one part; every part has one. Negatives: S2 (blocked file), S3/S14 (other cores unchanged), S12
(missing ROM, C64 features absent), S13 (no position error).

## §3 Decision

Facts first, then D1-D14, then the risks and the open questions.

**ZX-Spectrum_MiSTer structure (source).** `ZX-Spectrum.sv` wraps `ULA` (`rtl/ula.sv`, 348 lines: video counters,
contention, interrupt, port $FE, floating bus `port_ff`, Timex and ULA+ modes), `T80pa` (Z80), `ym2149` + `turbosound`
(AY), `tape.sv`/`tzxplayer.vhd`/`pzxplayer.sv` (tape in hardware), `snap_loader.sv`, `keyboard.sv` (PS/2 to matrix), `mouse.v`,
`divmmc.v`, `wd1793.sv`/`u765.sv`/`gs.v`/`saa1099.sv`/`jt12` (disk and sound extras). All ROM and RAM live in one
SDRAM (`ram_addr` is 25 bits; ROM pages at `{3'b101, page_rom, addr[13:0]}`, RAM banks below, `ZX-Spectrum.sv:424-437`),
the ULA reads the screen from a second 32 KB dual-port RAM `vram` that mirrors the writes to banks 5 and 7 (`:474-484`).
`clk_sys` is 112 MHz and `clk_56` 56 MHz (`rtl/pll.v`); the 7 MHz pixel enable is every 16th clock (`:156-158`). Frame
(source): 448 x 312 pixel clocks for 48K (`hc == 447`, `vc == 311`) = 69,888 T-states; 456 x 311 for 128K (`hc == 455`,
`vc == 310`) = 70,908 T-states (`ula.sv:102-104`). Boot ROM is a combined `boot.rom` (README: "copy boot.rom to
games/Spectrum/"). `T80pa` has a register-load port `DIRSet`/`DIR[211:0]` (IFF, IM, IY, HL', DE', BC', IX, HL, DE, BC, PC,
SP, R, I, F', A', F, A; `T80pa.vhd:88-89`), which `snap_loader.sv` uses for snapshots.

- **D1 Shape (P1). Two cores, two partials, one shared source tree. (Decided: Q1 "b", Alex 2026-10-13.)** Reasons:
  (a) the machines need different video paths (D6): the Spectrum fits the host's 16-entry palette exactly, the CPC does not;
  (b) different memory maps and ROM sets (D5), different media (D9/D10); (c) one core would carry both machines' logic: LUT about 7,000-13,000
  (D13, sum less the shared Z80 and AY, **estimate**) against the C64 reference's 13,643, the top of which would be in the range
  that did not route in T83's measurement (88.6 % LUT failed, 78.0 % routed, T79.1 §7 as quoted in T85 D10); BRAM does not add up
  either way, because only one partial is loaded at a time; (d) T87 gives every core its own cache entry, so two small partials
  rebuild independently; (e) the only arguments for one core were one `.cor`, one app and one build. **Shared once under
  `cores/zxcpc/`: `T80`, `ym2149`, the NCO (D4), the key translator (D8), the tape pulse engine (D9), the audio averager (D7), the
  register-window skeleton and identity block, all in one source tree and D-numbered here. T91 builds them first and T92 (CPC,
  `T92-cpc-core.md`) reuses them; a module T92 needs that is not here is added to the tree once, never copied.** CPC-only
  parts (gate array, CRTC, PPI, `u765`, the frame writer, the DDR2 sector service) are T92's. Apps: one app per core on
  Gideon's base with `#ifdef`, one shared C++ part for tape-to-pulse conversion, key injection and ROM loading (decided for
  every core).
- **D2 Source and licence (P2). MiSTer ZX-Spectrum, read-only study; our own `cores/zxcpc/` and `designs/zxspectrum/`.** What was looked at and why this one:

  | Candidate | Finding (2026-10-13) | Verdict |
  |---|---|---|
  | `MiSTle-Dev/*` | no Spectrum or CPC repository (17 repos) | does not exist |
  | `RetroSilicon/ZXnano` (Tang Nano 20K, Spectrum 48K/128K/Pentagon, Timex, ULA+, DivMMC, TurboSound) | repository holds `README.md`, a bitstream (`zx-nano.fs`) and `zx-nano.bin` (ROM image with Sinclair ROMs); **no HDL source**; README says "MIT" but no LICENSE file | not usable; and its ROM image is the kind of file we never take |
  | `Jefffr/CPCTang` (Tang Nano 20K, CPC 6128) | source present, no licence stated, a port of Amstrad_MiSTer (README); status in its README: CPU, ROM load, video over HDMI work; keyboard, FDC, audio, OSD not implemented | not taken; its `PLAN.md` log is used as a (third-party, Gowin) size data point in D13 |
  | `MiSTer-devel/ZX-Spectrum_MiSTer` | maintained, cycle-exact ULA with contention, 48K/128K/+3/Pentagon, mixed per-file licences, no repo LICENSE file | **taken (Spectrum)** |

  Per-file terms as written in each header (read, not from the README). No file I read in the ZX repository carries the
  "Replay hardware only" clause of the VC-20 files; one carries a non-commercial clause and is not taken. (The CPC
  repository's files and terms are in T92 D2.)

  | File(s) | Terms in the header | Used? |
  |---|---|---|
  | `ZX-Spectrum.sv` | GPL v2 or later (Sorgelig) | read for wiring; the top is rewritten (HPS, `sys/`, OSD, SDRAM are framework) |
  | `rtl/ula.sv` | GPL v3 or later (Sorgelig) | **ZX, kept** |
  | `rtl/T80/*.vhd` | BSD-style, Daniel Wallner 2001-2002, Sorgelig changes 2018, no extra clause | **kept** (Z80) |
  | `rtl/ym2149.sv` | BSD-3-clause wording, MikeJ 2005 + Sorgelig, no extra clause | **kept** (AY) |
  | `rtl/snap_loader.sv`, `turbosound.sv`, `wd1793.sv`, `mouse.v` | GPL v2 or later (Sorgelig / Ilia Sharin / Slavinsky) | `snap_loader` read as reference for the DIR sequence; the rest not taken |
  | `rtl/tape.sv` | GPL v3 or later (Till Harbaum) | not taken (the app builds pulses, D9); read as reference |
  | `rtl/keyboard.sv` | BSD wording (Mike Stirling, Sorgelig) **plus "License is granted for non-commercial use only"** | **not taken** (PS/2 matrix; ours is C64-matrix based) |
  | `rtl/tzxplayer.vhd`, `pzxplayer.sv`, `divmmc.v`, `dpram.v` | no licence text (tzxplayer: "by Gyorgy Szombathelyi") | not taken |
  | `rtl/jt12/*`, `gs.v`, `saa1099.sv`, `sdram.sv`, `ddram.sv` | GPL v3 / not read / GPL | not taken (Out) |
  | MiSTer `sys/` framework | not read | not taken |

  Handling: every kept file keeps its header; our own modules are written clean (tables from the machine documentation,
  not copied). ROMs are never merged (D5, D11). The cores are TREX-internal (D14, as T83 D13 and T85 D1).
- **D3 Porting surface (P3).** *Kept:* `ULA` (without Timex/ULA+/Pentagon timing/snow), `T80pa`, `ym2149` (one AY).
  *Replaced by host services:* HDMI, scaler, OSD, PS/2/USB (the host gives the C64 matrix), PLLs (D4), SD/HPS (`ioctl_*`,
  `sd_*`), the status/OSD bits. *Replaced by our blocks:* SDRAM/DDRAM controllers (D5: block RAM), `tape.sv`/`tzxplayer`
  (pulse engine, D9), `snap_loader` (app + `DIR` port, D10), `keyboard.sv` (key translator, D8), `crt_filter` (host).
  *Vendor-specific, to be inferred for Artix-7:* the Quartus `dpram`, `altsyncram`, PLL IP wrappers, `.mif` init (none
  needed: ROMs come from files). This list is what the first elaboration (S3) must confirm; anything not read in detail is
  dropped with the framework.
- **D4 Clocks, reset and frame lock (P4, C1).** The core stays on `sys_clk` (31.5278 MHz PAL, = 17.734475 x 16/9, the repo's
  value) with clock enables from a phase accumulator, as the VC-20 (`vc20_nco`) and the Amiga (T85 D3 b); no extra host clock.
  Ratios (computed):

  | Machine | Real clock | Enable from `sys_clk` | To hit the scaler's frame (628,992 `sys_clk`) |
  |---|---|---|---|
  | ZX 128K | pixel 7.09379 MHz = 17.734475 x 2/5 = `sys_clk` x 9/40; CPU 3.546895 MHz = x 9/80; AY 1.7734475 MHz | exact 9/40, 9/80, 9/160 | 70,908 T x 80/9 = 630,293 `sys_clk` per frame, 1,301 too many: run **+0.207 %** fast |
  | ZX 48K | pixel 7.000 MHz, CPU 3.500 MHz (14 MHz crystal) | accumulator 14/31.5278 | 69,888 T = 629,550 `sys_clk`, 558 too many: **+0.089 %** |

  Running fast by the last column makes the machine's frame exactly the host's 50.12 Hz, so the scaler's lock holds; the
  cost is pitch, speed and tape timing +0.09 to +0.21 %, the same trade as the VC-20's +0.17 % (T83). The enables are
  consumed by logic that is slower than `sys_clk`, so no multicycle path is needed. Reset: `core_rst` stretched; the Z80
  starts after ROM and RAM init (D5); a model switch is a cold boot. NTSC (60 Hz Spectrum) is not in the first build (§4).
- **D5 Memory and ROMs (P5, C3).** **Everything that the Z80 touches is block RAM inside `pb_l2`; DDR2 is only for media.**
  Reason: the CPU runs at 3.5 MHz with memory contention built into the machine (ULA wait states); a DDR2
  read (about 0.4 us measured for a 32-bit client read, `ddr2_port.v` header as quoted in T85 D4) does not fit a Z80 memory
  cycle without an elastic CPU clock, which would also stretch the ULA phases the contention model depends on. No
  elastic clock, no line cache, no real-time port needed.

  | Block | ZX | RAMB36 (4 KB each, **computed**) |
  |---|---|---|
  | RAM | 8 banks x 16 KB (128K; 48K uses 3) = 128 KB, true dual-port: CPU on A, ULA screen fetch on B (replaces the MiSTer `vram` mirror, which would cost 8 more tiles) | 32 |
  | ROM | one 32 KB area: 48K BASIC (16 KB) or 128K ROM0+ROM1 (32 KB), loaded for the chosen model | 8 |
  | Small | tape FIFO, key table, register window (LUTRAM where possible) | 1 |
  | **Total** | | **about 41** of 50 |

  ROM images are written by the app into the ROM block RAM before the core is released, from files (D11); "ZX 48K and 128K
  share one 32 KB area" is why the model switch rewrites the ROM and reboots. The ROM BRAM is writable only by the app's
  window and is read-only to the Z80 (a write is ignored, as on the machine). Pentagon and +2A/+3 (four ROMs, 64 KB) do not
  fit this budget or are Out (§4, Q3). DDR2 window (the host checks it, C3): the tape pulse stream (whole tape converted up
  front, error above 4 MB); nothing else.
- **D6 Video (P6, C4). The Spectrum uses the host's palette path.** *Superseded 2026-10-23 by T119 (Alex): the ZX writes an
  RGB565 frame buffer for the pixel layer through `e_*`, `pix_*` is a dummy raster; the text below is the history.* (The CPC uses the host's pixel layer instead, T92 D6:
  the machines differ in how many colours are on screen at once and who changes them.)
  *ZX (palette path, as the VC-20, T83 D5).* The Spectrum has 15 distinct colours (8 normal, 7 bright; black twice) and
  the colour of every pixel is fixed by attribute bits that the ULA resolves per pixel (FLASH included). The core emits the
  4-bit index 0-15 as `pix_color` with raster `pix_x`/`pix_y` and the app writes the 16-entry Spectrum palette
  (`C64_PALETTE`, `T59-palette.md`; pure Spectrum colours: 0/215 levels, bright 255) when the core is loaded. The host,
  scaler and overlay stay unchanged (Q3 of T83: V1b first stage). Geometry (computed): one Spectrum pixel is 10/9 of a scaler
  dot (the dot is 4 `sys_clk`, 7.8819 MHz; the pixel 7.0938 MHz), so the 352-pixel line (256 + 48 + 48 border) is 391 dots in
  the scaler's 400-dot window, and 270 of the 296 lines are shown (192 + 39 above and below); the core maps pixel k to dot
  floor(k x 10/9), one pixel in nine is two dots wide. The exact window is found on the board by photo (as T83's 94/9
  origin). ULA+ (64 colours), Timex hi-res and hi-colour need more than 16 colours or a different pixel clock: Out (Q3).
- **D7 Audio (P7, C5).** Beeper (port $FE bit 4 = EAR out, bit 3 = MIC) and, in 128K, one AY-3-8912 (`ym2149`, ports
  $FFFD/$BFFD, clock 1.7734475 MHz; the 48K machine has none). Both
  are averaged over 656 `sys_clk` (= 48.06 kHz, as the VC-20) into signed 24 bit with `audio_sample_en`; the host mixes. The
  beeper is mixed in at a fixed level (a 1-bit level, the ear/mic combination as the machine does it); the AY's three channels
  are summed mono at first (the A-B-C stereo layouts of the MiSTer core are a setting for later, not in the first build).
  Tape playback sound (the ear signal on the speaker while loading) is a switch in the app. No filter in the first build; the
  host's own filtering stays.
- **D8 Input (P8, C6).** The host delivers `keys[63:0]`, `joy1/joy2[4:0]`, `restore` (C6); the core has one **key translator**
  (a table in LUTRAM: `(C64U key, shift state)` to the set of target keys pressed) and writes the result into the machine's own
  key matrix. *ZX (40 keys, 8 half-rows):* letters and digits direct; CAPS SHIFT = left SHIFT; SYMBOL SHIFT = C= key
  (and right SHIFT acts as CAPS SHIFT too); ENTER = RETURN; SPACE; BREAK (CAPS+SPACE) = RUN/STOP; cursor keys, DEL (CAPS+5/6/7/8,
  CAPS+0) from the C64 cursor and INST/DEL keys; punctuation by the symbol printed on the C64 key cap, produced as the
  Spectrum's SYMBOL-SHIFT combination (for example C64 `,` -> SYM+N, `.` -> SYM+M, `;` -> SYM+O, `:` -> SYM+Z, `=` -> SYM+L,
  `+` -> SYM+K, `-` -> SYM+J, `*` -> SYM+B, `/` -> SYM+V; shifted ones likewise); the full 64 x 2 table is written with the
  core. 128K-only keys (EDIT, TRUE/INV VIDEO, GRAPH, CAPS LOCK) as CAPS/SYM combinations the 128K ROM understands.
  *Joysticks (ZX):* the app chooses what port 2 (and port 1) act as: Kempston (port $1F, bits RLDUF, active high), Sinclair
  1 (keys 6-0) or Sinclair 2 (keys 1-5) or Cursor (5, 6, 7, 8, 0), the last three injected as key presses; Kempston mouse and
  the second fire button are Out (one fire on the C64 port). USB keyboard and mouse through the app are not in the first
  build (the VC-20/Amiga answered "C64U matrix only", T85 Q6; same here by the task text).
- **D9 Tape (P9).** *No tape player in the core's HDL beyond a small pulse engine.* The app reads `.tap` or `.tzx` from
  SD/USB and converts the whole tape into a **pulse stream** of
  durations in 3.5 MHz T-states (an escape for long gaps) in the core's DDR2 window, with a block index (for pause blocks, "stop
  the tape" blocks and block-wise rewind). The engine in the core (about 150-300 LUT, **estimate**; shared with T92) reads the
  stream with the general port ahead of the playback position into a small FIFO and drives the tape input: ULA port $FE bit 6.
  The Spectrum has no motor, the app's play / pause /
  stop / rewind do it. Real speed only (turbo is Q5); a long tape is therefore minutes, snapshots are the fast path.
  The 0.09-0.21 % frame stretch (D4) scales tape pulses with the T-state clock, so the loader sees the correct ratio. Tape
  SAVE (recording the ear/mic line to a `.tap`) is Q6, later: the engine would need an edge-capture ring back
  to the window and the app would write the container. MiSTer's own `tzxplayer.vhd`/`tape.sv` are not taken (D2).
- **D10 Snapshots (P10).** The app parses `.sna` (48K and 128K) and `.z80` (v1 with RLE, v2, v3;
  48K and 128K), writes the RAM banks into the core's RAM through the app's window (about 128 KB over the I/O bus; the same
  path that loads ROMs), sets the paging register and the machine model, and loads all registers through a **register-load
  port** on `T80pa` (`DIRSet`/`DIR`, the 212-bit register file the source exposes) in the same clock where the core is held in
  reset and released; the AY and ULA border registers come from the 128K snapshot's fields. 48K machines run SNA/Z80 snapshots
  from the 48K model. No `+3` or Beta disk on the ZX (Q3).
- **D11 Registers, identity, ROM files, app and swap (P11, C2/C9).** Same rules as the VC-20 and the Amiga (T83 D8-D11, D13;
  T85 D9): all `SLOT_*`, IEC and user pins idle (the machine has none of those; no 1541), `core_id` "ZXSP",
  contract version, clock table (D4), geometry notes (D6), window size (D5); a config window (model, joystick mode, AY mix,
  tape control, the key/joystick injection, reset, `DIR` load strobe); everything else answers "absent". The
  application is the VC-20's kind (T83 D11a): an Ultimate-app image on Gideon's base (`build_zx.sh`,
  switch `TRX_CORE=zxspectrum`, off by default), packed with the partial in a `.cor`, which initialises its own core
  (loads the ROMs from files, writes the palette, sets the tape registers) and shows only what exists: reset, model,
  tape (insert / play / stop / rewind / eject), snapshot, joystick mode, sound, Freeze. No monitor, SID, REU, turbo,
  cartridge writer, Timewarp; the gate is the identity block (T83 D11). **ROM files** (the app reads them at core start from
  `/usb*/roms/<core>/` or `/sd/roms/<core>/`, or the flash `roms` area; never in the repository, CI or `.cor`; sizes are the
  machine's):

  | Core | File | Size | Content |
  |---|---|---|---|
  | ZX | `/roms/zx/48.rom` | 16,384 | 48K BASIC |
  | ZX | `/roms/zx/128.rom` | 32,768 | 128K ROM 0 (editor/menu) + ROM 1 (48K BASIC for 128) |

  A model is offered in the menu only if its files exist; a missing file is reported by name (S12). The MiSTer
  Spectrum combined `boot.rom` (README) is not the format; the app may additionally
  accept it as a convenience, decided at the build. The file names are this spec's choice; sizes are fixed by the machine.
  A swap is a cold boot, nothing of the previous core survives (T79 decoupler).
- **D12 Freeze (P11, C8). Stop everything at a clock edge; the picture follows its frame rule.** Alex, 2026-10-13: "when I
  choose FREEZE it MUST freeze. Like the C64." The core takes the contract's machine pause (`c64_halt_req`, T85 Freeze).
  All enables (CPU, ULA, AY, tape engine, beeper) are held at the next **frame boundary** (at most 20 ms later),
  because the palette path's scaler only keeps its lock if the raster stops and starts on whole frames (`vic_scaler.v` header:
  "c64_core resumes the VIC after whole frames"; T83 D16/D16a lost position without that rule); inside a frame the held
  state is exact because `T80` and the ULA advance only on their enables. `paused` is raised when the enables have been
  held and the tape FIFO refill and the window access are quiet (as T85's `quiet` chain); the app then shows the menu over
  the host's black raster. Audio ramps to zero, as the Amiga. The tape engine keeps its position, so a tape resumes exactly.
  A reset takes the pause away. Verified by the T85 method (S13).
- **D13 Size and timing (P12).** **Estimate, no measurement exists.** The MiSTer repo publishes no resource figures;
  nothing here has been synthesised for Xilinx. Basis and range:
  *Third-party data point (measured by its author in Gowin LUT4, not reproduced):* CPCTang's `PLAN.md` logs 3,255 LUT4 /
  1,222 FF / 1 BSRAM with the Z80, gate array, CRTC, MMU and PPI connected to a 64 KB RAM, and 4,548 LUT4 / 2,027 FF / 3 BSRAM
  with the HDMI scandoubler added; its own budget table lists the T80 at 1,500-2,000, the `u765` at 1,500-2,000 and the whole
  motherboard at 8,000-10,000 (an upper estimate it did not reach). I assume 0.6 to 1.0 Xilinx LUT6 per Gowin LUT4 (as T85
  D10, **assumption**, no source).

  | Block | ZX (LUT, estimate) |
  |---|---|
  | Z80 (`T80pa`) | 1,000-1,800 |
  | ULA | 500-900 |
  | AY (`ym2149`) | 400-700 |
  | Glue: NCO, key translator, joystick, tape engine + DDR2 client, audio averager, registers + identity, palette/pixel stream | 1,200-2,100 |
  | **Total** | **about 3,100-5,500** |
  | of `pb_l2` (18,000) | 17-31 % |
  | of the C64 reference (13,643) | 23-40 % |

  FF about equal to LUT or less (CPCTang: 0.37-0.45 per LUT4). DSP: 0 of 80 (T80's ALU and the AY volume tables are LUT logic;
  not verified). BRAM: **about 41 of 50** (D5, computed); the C64 reference uses 39. Timing: the logic runs on `sys_clk`
  (31.5 MHz) with 3.5 MHz enables, far below T83's and T85's cases (35.5 and 28.4 MHz single-cycle paths); the edge, not the
  logic, is the risk, as for the VC-20. The first out-of-context synthesis (S14) decides; the host does not change, so layer 1
  is not touched.
- **D14 What follows from earlier decisions (no new decision):** TREX-internal only (T83 D13, T85 Q1): private Gitea, never in
  a release, update package or public download, behind its own switch; ROMs never in the repository, CI or `.cor`, the app loads
  them from `/roms/<core>` (T83 D4); plain machine, no TRX64 ring/monitor/snapshots, but Freeze works like the C64 (T83 Q6 changed
  2026-10-13); PAL first (T83 Q5); the app is on Gideon's base, one app per core (`#ifdef`); keyboard from the C64 matrix,
  joysticks from the C64 ports; video through the palette path (D6); media served by the app from SD/USB; one
  core clock family from the host (T79.1 C1); peripherals are the host's; the register bus is 8 bits and carries control only,
  bulk data (pulse stream) goes through the memory ports (T79.1:48); a swap is a cold boot.

**Risks (named, not decided).**

- **R1 Mid-frame palette on the palette path (ZX).** ZX is safe because colours are resolved in the core. If Alex later wants
  ULA+ (64 colours, palette written per line) the ZX needs the pixel-layer path too (Q3).
- **R2 Lock relearning.** Both pausing rule (D12) and running +0.09/+0.21 % fast (D4) depend on the scaler holding its lock; T83
  lost the position twice (D16, D16a). Mitigation: only whole-frame stops for the ZX; the host's relock on release exists (dfx-trial a5f8252, T79 history) but is not relied on.
- **R3 Tape fidelity.** Pulse stream plus real-speed playback: TZX blocks that are not pulse sequences (direct recording,
  CSW, generalised data) are converted by the app; unsupported block types are refused with a message, not skipped silently.
- **R4 Z80 core.** `T80pa` is the shared Z80 of both MiSTer cores and has a known undocumented-flag and block-instruction
  history (Sorgelig's 2018 fixes); S3 runs ZEXALL by hand to find out. A cycle-accurate alternative (A-Z80, GPL) is bigger
  and not read.
- **R5 Block RAM above the reference.** The Spectrum needs about 41 RAMB36 (D13), above the slim C64 reference's 39
  (T87 D15/D16), which is what the locked host is routed with; whether it routes against that host is measured at the first
  build. Levers: the 128K banks with fewer, wider tiles, or the ROMs shared in one tile pair.

**Open questions for Alex (one line each, not decided here):**

1. Q1: One core with a Spectrum/CPC personality switch (about 7,000-13,000 LUT in one partial, one `.cor`), or two cores (this spec: smaller partials, own caches, the CPC gets T92)?
   **Answered (Alex, 2026-10-13):** "b": two cores (two partials, shared modules in one source tree); the CPC gets T92.
2. Q2: Which first: the Spectrum (smaller, palette path, no DDR2 video, no disk) or the CPC (pixel layer, FDC, 46 BRAM)?
   **Answered (Alex, 2026-10-13):** "a, and then the CPC in its own spec please": the Spectrum first; the CPC moves into its own spec T92 (split out of this one).
3. Q3: Spectrum models and extras in the first build: 48K and 128K (+2 as 128K) with beeper and AY, Kempston/Sinclair/Cursor, tape and snapshots. Out unless you say otherwise: +2A/+3 (disk), Pentagon/Scorpion, Timex/ULA+, DivMMC/ESXDOS, TR-DOS/Beta disk, Multiface, mouse, turbo speeds.
   **Answered (Alex, 2026-10-13):** "a": as proposed: 48K and 128K (+2 as 128K), beeper + AY, Kempston/Sinclair/Cursor, tape and snapshots; the listed extras stay out.
4. Q4: CPC models and extras in the first build: **moved to T92** (`T92-cpc-core.md`, which carries the question and the answer).
   **Answered (Alex, 2026-10-13):** "a": as proposed; moves to T92 with the CPC.
5. Q5: Tape loading in the first build: real speed only (snapshots and disks as the fast path), or also a turbo/fast load for the standard ROM loader (MiSTer's direct byte injection: needs the core to watch the ROM loader's address)?
   **Answered (Alex, 2026-10-13):** "a": tape loads at real speed only in the first build (Spectrum and CPC); snapshots and disks are the fast path.
6. Q6: Tape SAVE (record the machine's tape output to a `.tap`/`.cdt` on the stick) in the first build, or later?
   **Answered (Alex, 2026-10-13):** "b": tape SAVE (.tap/.cdt) later, not in the first build.

## §4 Out

What this spec does not do, and why. Anything listed here is decided with Alex by the questions above or by the earlier
decisions in D14; nothing is left out by omission.

- **NTSC / 60 Hz machines.** PAL first (T83 Q5). The Spectrum's NTSC timing is its own step.
- **Real cartridges, expansion ports, tape deck, light pen, the real 3" drive, printer, serial/user port pins.** The C64U's
  ports do not match; all `SLOT_*`, IEC and user pins stay idle (D11). The media are files served by the app.
- **ZX Spectrum Next, ZX81, SAM Coupe, Spectrum 16K (real, with the 16 KB bank).** Not on the list; the 48K model covers
  16K programs.
- **Everything the source has beyond the machine** (Q3): Pentagon/Scorpion/Profi, +2A/+3 and Beta/+D/DivMMC disk
  systems, Multiface, General Sound, TurboSound-FM, SAA1099, Kempston mouse. Each is a later step with its own scope decision.
- **A USB keyboard or mouse through the app,** AY stereo layouts, scanline/CRT filters (the host does the scaling), and
  turbo CPU speeds beyond the machine's own.
- **TRX64 ring, monitor, Timewarp, snapshots of the machine state** (plain machines, T83 Q6). Freeze (D12) is in.
- **A host change.** The host is routed with the slim C64 (T87 D15/D16); the core asks for nothing of it (the palette path
  exists). If the Spectrum's geometry needs a scaler parameter after the board test, that is a separate
  host-rebuild question, not decided here.
- **The Amstrad CPC.** Its own spec, T92 (`T92-cpc-core.md`): gate array, CRTC, PPI, `u765`, the pixel-layer picture, DSK
  disks, CDT tapes.
- **Clean-room limits.** Gideon's UltiSID, Copper, lp_filter and sampler sources and the VHDL of `ultisid_on_mister_v77` are not
  opened for this work.

## §5 Decisions taken while building

(Written 2026-10-22 on branch `t91-t92-cores`, which merges the never-pushed `t92-core` (it holds `t91-core`) into main ff984647. Numbers are
bench output lines of this day, run with `sh cores/zx/sim/run.sh <modes>` as `ci/sims.txt` names them.)

- **2026-10-22, what exists.** Gateware `cores/zx` (ULA and AY as vendored in `rtl/`, the Z80 as `gen/t80pa.v` from `tools/zx_gen.sh`, our parts in
  `shared/` and `trx/`, `layer2_zx.v`, `core.toml`, `core_sources.tcl`), six bench rows plus the pin check in `ci/sims.txt`, the application side
  (`coreapp/cores/zx.cc`, `zx_snap.*`, `tape_pulses.*`, `tape_deck.*`, `zx_regs.h`), the logo (`cores/zx/logo`, made by `tools/core_logos/build.py`). Not
  done and not measurable here: routing (LUT, BRAM, WNS: S14 is CI's), the board (S7's photo, S9's typed line, the swap round trip).
- **2026-10-22, scenarios, where each is green.**

  | S | Proof | Result (source line) |
  |---|---|---|
  | S1 | `ci/lint.sh` "zx/cpc shared modules once" | one copy of T80, YM2149, NCO, key translator, tape engine under `cores/zx/shared` |
  | S2 | `ci/lint.sh` "zx/cpc: no ROM, media or snapshot" | no ROM, media image or snapshot in the tree; the application's tests make theirs in a temporary directory |
  | S3 | bench `zx_cpu` | `PASS z80s: 254 result bytes equal the expected ones after 75 frames (2 known deviations)`: the two are T80's SCF/CCF X/Y flags; ZEXALL by hand not run |
  | S4 | `zx_machine` | `PASS clk48: frame = 628992 sys_clk, 139776 pixel clocks ... clock 0.088183 % off`; `PASS clk128: ... 141816 pixel clocks ... clock 0.206327 % off` |
  | S5 | `zx_cpu` | `PASS page48`, `PASS page128`, `PASS page128 window: RAM banks and ROM read through the app's window equal the Z80's`, `PASS model 48K` / `model 128K` |
  | S6 | `zx_machine` | `PASS cont48` and `PASS cont128: the model predicts 8 later interrupts from the first`; control `mut_nocont` fails |
  | S7 | `zx_video` | `PASS video48: 49152 paper pixels (15 colours, bright, FLASH) and 270 border lines equal the Spectrum rules`, `PASS shadow128`; the photo is open |
  | S8 | `zx_io` | `PASS audio128: beeper and AY tone periods within 0.3 %` (beeper +0.032 %, AY tone -0.003 % of the programmed value, silence exactly 0) |
  | S9 | `zx_io` | `PASS keys48: 790 key sets (matrix, joysticks, injection) read back exactly as the table lists them`; control `mut_keyrow` fails; the typed line is open |
  | S10 | `zx_io` and `tests/coreapp` | `PASS tape48 window: block A loads at tape speed and passes its XOR, block B (one bit flipped) is refused`; the converter: `test_tape` |
  | S11 | `zx_snap` and `tests/coreapp` | `PASS snap48` (SNA and Z80), `PASS snapint`, `PASS snap128` (SNA and Z80); the application's reader against `tools/zx_snap.py`: `test_zx_snap` |
  | S12 | `tests/coreapp` `test_zx`, golden `menu_zx.txt`, bench `zx_pins` | the menu holds reset, model, joysticks, tape sound, tape and snapshot entries and nothing of the C64 machine; a missing ROM is named; `PASS pins` |
  | S13 | `zx_pause` | `PASS pause hashes: all 24 machine frames of the paused run equal the reference run's`; control `mut_nohold` fails |
  | S14 | CI core stage | open: the first route |

- **2026-10-22, D11 is superseded: the application is the generic one.** The text of D11 and D14 (Gideon's base, `build_zx.sh`, `TRX_CORE=zxspectrum`)
  predates T84 D12, T93 and T104. `core.toml` says `app = "blueprint"`; the descriptor is `coreapp/cores/zx.cc`, in the table of `coreapp/src/table.cc`,
  and `ci/trxapps.sh` copies the one image to `zx.app` for `pack_cores_mc.sh`. Id `ZXSP`, folder `roms/zx`, rail title `SPECTRUM`.
- **2026-10-22, found: the host's ROM aperture cannot load a ROM of 32 KB.** `host/ultimate_sys.vhd` (region `r_rom`) hands the core `dma_addr` bits 13:0
  contiguously and puts the C64 ROM block's own layout above them (BASIC at `0x4000`, KERNAL at `0x2000`, character ROM at `0`); offsets `0x1000-0x1FFF`
  and `0x6000-0x7FFF` of a 16-bit ROM address are unreachable. `zx_win.v` took the 16-bit address, which the bench could give and the real host could
  not. Changed (the core only, the host is frozen): WIN_PAGE (`0x0D4`) is three bits, b0 the 64 KB RAM half as before, b2:1 `ROM_SEL`, the 16 KB bank the
  aperture shows; the ROM address is `{ROM_SEL, dma_addr[13:0]}`. The bench's `win_acc` does what the application does (bank in the register, `01` in
  `dma_addr[15:14]` as the host puts something there): `PASS page128 window`, `PASS model 128K` stand on it. Applies to `cpc_win.v` the same (T92 §5).
- **2026-10-22, board: black HDMI, scaler unlocked; the host's scaler is now in the bench (S14, branch t91-zxpicture).** Comparison read from code, not comments:

  | Signal | ZX (zx_core.v) | CPC (cpc_core.v) | What `vic_scaler.v` needs |
  |---|---|---|---|
  | lock marker | `px==0 && py==20` for one `sys_clk`, only if the ULA runs a visible line | `dx==0 && dy==20` for four `sys_clk`, free running raster | `py==ALIGN(20) && px==0` at vclk = `sys_clk`, toggles `frame_tgl` per clock it holds |
  | pix_* outside the picture | `ox=503, oy=0` (blanking), real pixels in the line | black raster 504 x 312 always | any; ring written every clock |
  | pixel clock | `ce_pix` 2/9 of `sys_clk`, stops with `hold_q` | none (raster from `dph`), pixels via DDR | none |
  | blank | `c64_halted` = `hold_q & quiet` | same | only blacks the window, not the lock |
  | reset/hold release | `rst_q`, `rcnt` 255 clocks, app CTRL.b0 | same pattern | `relock` after the swap, then one marker |
  | contract ports | pix_color/x/y 4/10/9 bit direct | same | same |

  Bench `scaler`/`scaler128` (tb_zx.v S14 instantiates `host/vic_scaler.v` + `pal_ram.v` as top.v wires them): `PASS scaler: the host's vic_scaler locks to the core's marker and shows a picture` (locked at machine frame 1, 8,757,498 non-black pixels), `scaler128` PASS (4,917,120), control `mut_nomark` (`-DZX_MUT_NOMARK`) `FAIL scaler ... did not lock (locked 0)` as required. The same bench prints the host's raster measurement: 286 lines, 2304 clocks (marker period 628,992 mod 4096). **The board's "57 lines / 48 clocks" cannot be produced by the pix_* pattern the bench sees**, so the ULA path on the board does not run as in the sim; the cause is not found by reading or by the sim (no synth trimming of the picture registers, no implicit nets in the Verilator lint, 128K and 48K both lock). Not proven, hypotheses by what the JTAG words split: frames (`trx 2`) not moving = `ce_pix`/hold; `trx 5` lines moving but markers 0 = `vcm`/`shown` (VSync/VBlank); lines 0 = HBlank stuck. Words 5-7 added to the proof-of-life block for that read; word 0 corrected to the ID value.
- **2026-10-22, ROM files: one complete set starts the core.** `RomSpec.set` (a bit mask) in `coreapp/src/desc.h`, evaluated in `romfind.cc`: `48.rom`
  alone, `128.rom` alone or both start the core; neither is named on the screen after ten seconds (a wrong size at once); the sentence names the ROM of the
  closest set. The "Model" setting is a static list (the descriptor is const data), so D11's "a model is offered only if its files exist" is built as:
  a model whose file is missing is not run, the other runs and the screen says so (`c->notice`); with neither, the start is refused with the name.
- **2026-10-22, the picture's colours.** The palette path needs the 16 colours written to the host's scaler (`C64_PALETTE`, T59): `CoreIo::palette()`
  (new, a no-op where a core has no use for it; `hw.cc` stores R, G, B at `C64_PALETTE + 4n`). The descriptor has `video.w == 0` (the application owns no
  buffer, T104.2 D5); `zx_init` writes the machine's levels (215, bright 255) in the core's index order `{bright, green, red, blue}`.
- **2026-10-22, snapshots (S11, D10).** `.sna` (48K, 128K, including the 147,487-byte form when the paged bank is 5 or 2) and `.z80` v1/v2/v3, plain or packed.
  Refused with the reason, the machine untouched: a file of another size, a 48K SNA whose SP is in ROM or `0xFFFF`, a 128K file shorter than its port says, a
  Z80 of another hardware mode (+3, Pentagon, ...), a missing page, damaged packing. The load holds the machine with CTRL b0 alone (the host's reset line
  would keep it in reset, `zx_core.v` `rst_core`), writes model and ROM bank by bank, the RAM through the window with `C64_DMA_MEMONLY` on (bank 3 spans
  `$D000`), the register image, AY, 7FFD, border, then `SNAP_GO`, waits for the busy bit (200 ms limit), releases. A snapshot also works while Freeze holds the
  machine (the register load does not need a CPU clock, `T80.vhd` `DIRSet`); not tested on a board. The "Model" setting is not changed by a snapshot: a later reset
  starts the setting's model.
- **2026-10-22, tapes (S10, D9).** `.tap`, `.tzx` converted whole into the stream of `zxcpc_tape.v` at window offset `0x800000` (4 MB = 2,097,152 words
  limit, refused above). TZX blocks 10-15, 20-22, 24/25 (one loop), 2A (stops the tape when the machine that runs is a 48K), 2B, 30-35, 5A are read;
  18 (CSW), 19, 23, 26-28, the C64 blocks and unknown ids are refused with the block named. A pulse under 16 T-states (a turbo loader) is refused (Q5). A block
  index (256 entries) gives the menu's previous/next block and rewind. The ear on the speaker is the setting "Tape sound" (D7), off by default.
  "Insert tape" does not start it (the machine has to be at `LOAD ""` first): "Tape: Play" (F7) does, and the service takes the play bit back at a stop marker
  or the end. No tape SAVE (Q6), no auto-typing of `LOAD ""`.
- **2026-10-22, settings.** Model (at reset), Joystick port 1 and 2 (none, Kempston, Sinclair 1, Sinclair 2, Cursor; defaults none and Kempston, as the C64U's
  port 2 is the usual one), Tape sound. No AY mix (D7: stereo layouts later), no ULA+/Timex (Q3).
- **2026-10-22, `core.toml`.** `order = 11` (position 8 is the NES's now, 10 the Apple IIe's), contract 2 = `contract/VERSION` (`tools/contract_ports.py --check`: 247
  ports match), `experimental = true` (a failing bench is reported, it does not fail the stage).
- **2026-10-22, open.** (1) The first CI route: LUT, BRAM (D5 computed 41 of 50), WNS; the NES precedent needed `nes_timing.tcl` for its clock-enabled
  machine (T95 5.15), whether this core does is the first route's answer. (2) `host/layers.txt` has no `ignore:` line for `cores/zx/sim`, so the benches stay
  in the core's key and a bench edit rebuilds the core; adding the line is a host-file change and was not made. (3) The board: palette and picture position
  (S7), keys, snapshot and tape on the real host aperture, the swap round trip.

---
<!--
Closing: Status → DONE (date) + which scenarios are green where; TODO row ✅ and history entry,
in the same commit as the code.
-->
