# T263 — C64-vicx runs as its own core: the trx64 C64 without ring and monitor, plus the VIC-X graphics and memory extension

**Status:** PROPOSED (2026-11-26). Alex, 2026-11-26 (translated): "Write a spec: C64-vicx. Basically like the SuperCPU, but with the
6502 CPU, all TRX64 building blocks that are standard (reSID, floppy, etc.). No focus on ring, monitor etc. Specs and Vivado:
https://github.com/NebulaSleuth/VIC-X/". D1-D8 decided; Q8-Q11 start from the proposals after D8; Q12 open; spec only, nothing is built (D7).
Open (one line, as in the TODO table): PROPOSED 11-26, spec only (D7); D1-D8 decided (trx64 w/o ring, 50T+100T, large, VRAM DDR2); Q12 name open
**Repo:** TRX64U-MultiCore: a new core directory `cores/<id>/` (gateware, `core.toml`, its own app under `cores/<id>/app/`) and a copy
of the VIC-X sources (Q8). The trx64 gateware it copies stays in TRX64-Ultimate; nothing is changed there.
**Number:** T263.
**Origin:** Alex, 2026-11-26: "Spec schreiben: C64-vicx. Quasi wie supercpu, aber halt 6502 CPU, alle Bausteine von trx64 die Standard
sind (reSID, Floppy, usw). Kein Fokus auf Ring, Monitor usw. Specs und vivado https://github.com/NebulaSleuth/VIC-X/" (translation in
the status line). On the base, the same day: "Quelle sollte der trx64 Core sein ... ohne Ring, Monitor usw." (D1).
**Depends on:** T239.3 (SDK, a core's own app, TOML A and B, D55 core move, D58 libraries linked through TOML B), T239.5 (matrix
pipeline, D13, D15, D16), T182 (the SuperCPU core: own large core, own app), T103 (the trx64 core without ring and monitor as
`cores/c64u`; dropped 2026-11-13, never built).
**Milestone:** M3.

---

## §1 The problem

### 1.1 What C64-vicx is

1.1.1 C64-vicx is a C64 core with the VIC-X block. VIC-X is a third-party graphics and memory extension for FPGA C64s: a 256-colour
palette, two tile or bitmap layers, a sprite engine, 128 KB of VRAM, up to 16 MB of XRAM shared with the REU, and a DMA engine with
blitter operations (VIC-X `README.md:3-7`). A program that does not unlock VIC-X sees a stock C64 (`README.md:6-7`).

1.1.2 "Like the SuperCPU" means its own core with its own app, as `c64scpu` (T182). `c64scpu` is a large core: host and core flat in
one bitstream (`cores/c64scpu/mono/core.toml:3-4`), delivered as `.cfw` (`:10`). Its tree is a fork of `cores/c128` cut back to a C64
with its 6510 (`cores/c64scpu/PROVENANCE.md:3-6`); the C128 files are copies of the `cores/c64u` files (`:15`).

1.1.3 The CPU is the 6510 of the trx64 core (`cpu6510`, TRX64-Ultimate `cores/icepi/c64_top.v:536`), not a 65816.

### 1.2 VIC-X as read

Source: https://github.com/NebulaSleuth/VIC-X, branch `main`, commit `98b9eebb` (2026-11-21), read through `gh api`.

- **Register spec:** https://extrastrength.com/vic-x-docs.html, "Draft specification, version 0.1", read 2026-11-26. Unlock: `$56 $58
  $21` to `$D06F`; `$00` relocks; every reset locks; while locked, `$D040-$D06F` mirror the VIC-II. Direct registers at `$D040-$D06F`:
  ID, VERSION, CAP_VRAM/XRAM/SPR/SPRLINE/FEAT (`$D043-$D047`), CTRL (`$D048`), IRQ (`$D049-$D04B`), two ports (`$D04C-$D053`), two XRAM
  windows (`$D054-$D059`), DMA_CMD (`$D05A`), CAP_BLIT (`$D05B`). Section "Operations in every build": copy, fill, masked copy. VRAM:
  "128 KB target, 64 KB minimum". Optional: 64 sprite entries instead of 128, 16 a line instead of 32, the blitter extras. On the
  Commodore 64 Ultimate's XC7A50T: "whether it fits beside the rest of that machine is not known". The page states no licence.
- **Reference model:** the VICE fork https://github.com/NebulaSleuth/vice-vicx, branch `vicx` (`bbd26dc6`, 2026-11-19)
  (`README.md:22-27`). The MiSTer core with the block: https://github.com/NebulaSleuth/C64_MiSTer, branch `vicx` (`687d1525`)
  (`README.md:191-194`). Neither was read.
- **The block** (`hdl/vicx.vhd`, entity `vicx`, `:122-188`): one clock. CPU bus: `vic_cs`, `cpu_addr` (offset in `$D000-$D3FF`),
  `cpu_wr`/`cpu_rd` strobes, `claim` (VIC-X owns the access), `cpu_dout`, `irq`. Video per pixel: `vic_idx`, `vic_rgb`, `pix_ce`,
  `line_start`, `frame_start`, `raster_y`, the VIC-II flags `vic_bg`, `vic_win`, `vic_border`; out `rgb_out` (24 bit). External VRAM:
  `vx_*` (25-bit address, 64-bit read of an 8-byte block, 16-bit write with byte enables, request and acknowledge). `cpu_halt` (a DMA
  touches C64 RAM). XRAM windows: `bus_addr` in, `win_hit`/`win_addr` out. `xram_mb` (REU size in MB) and `stock_rgb` in. Generics
  `VRAM_BITS`, `CAP_SPR`, `CAP_SPRLINE`, `CAP_FEAT`, `CAP_BLIT`, `EXT_VRAM`, `VRAM_BASE`, `XRAM_BASE`, `C64_BASE` (`:123-137`). The
  file header (`:3-10`) says the sprite engine and the XRAM windows "come later"; the entity has their generics and ports and
  `vicx_sprites.vhd` exists, so that header line is stale.
- Plain VHDL, inferred block RAM, no vendor IP. GHDL 6 with `--std=08` (`hdl/run_sim.sh`); Vivado reads it as VHDL-2008
  (`hdl/vivado_xc7.tcl:42-44`).
- Steps 1-5 are built in the fork and the block. The DMA engine and the XRAM windows need `EXT_VRAM` (`hdl/README.md:160-163`). On the
  MiSTer: t01-t28 pass as far as they run on hardware, six demos run, 83 % of the logic, 431 of 553 block RAMs (`hdl/README.md:178-182`).
- **VIC-X's own Vivado numbers**, not measured by us (`hdl/README.md:54-61`): the block alone, out of context, `xc7a50tfgg484-1`, 77 MHz,
  `EXT_VRAM` true (VRAM not counted), 2026-11-20:

| Build | LUT | FF | BRAM36 | DSP | of XC7A50T | Slack at 77 MHz |
|---|---|---|---|---|---|---|
| Full (the MiSTer build) | 9,550 | 5,872 | 15 | 10 | 29 % | +0.05 ns |
| Sprites 64/16 | 8,531 | 4,675 | 9 | 10 | 26 % | +0.03 ns |
| Layers and sprites 64/16, no DMA | 6,801 | 3,798 | 8.5 | 6 | 21 % | +0.52 ns |
| Layers only | 5,311 | 2,212 | 4.5 | 6 | 16 % | +0.71 ns |

  Per part (`hdl/README.md:63-71`): core about 1,600 LUT; each layer about 1,830; sprites 64/16 1,490, 128/32 another 1,019; DMA
  engine, XRAM windows and blitter 1,730.
- **Tests:** GHDL testbenches `hdl/tb/tb_vicx_{step1,step2,step3,step4,wide,dma}.vhd`, run by `hdl/run_sim.sh`; with external VRAM they
  pass against memory latencies up to 40 clocks and fail at 200 (`hdl/README.md:36-42`). Conformance programs `tests/t01_locked.s` to
  `tests/t35_list.s` (ca65), run by `tests/run-tests.py` in the headless VICE fork: the result goes to VICE's debug cartridge `$D7FF`,
  exit code 0 = pass (`run-tests.py:1-20`, `tests/lib/harness.inc:1-7`). Assembled with `-D HW_RESULT`, a test shows PASS on a green
  border or FAIL and its code on a red border (`harness.inc`, `tests/hw/run_mister.py`). t06 needs VICE's monitor, t09 VICE snapshots;
  20 tests check pixels of the exit screenshot (`; PIXEL:`); t27, t28 and t31-t35 run with the REU at 16 MB (`; RUN: -vicx -reu
  -reusize 16384`). `tests/stock-compat.py` runs VICE's self-checking VIC-II programs (`tests/stock-compat.txt`) in stock VICE and in
  the fork and requires the same results (`README.md:211-221`). `tests/hw/vram_check.s` and `reu_check.s` check VRAM and a 16 MB REU.
- **Demos:** each is a PRG and its data on a D64 (or D81), loaded with its own fast loader on a 1541; each needs an REU of 2 MB or more
  (`demos/README.md:45-78`).

### 1.3 The trx64 core today, and what D1 takes out

1.3.1 "The trx64 core" is the C64 gateware of TRX64-Ultimate: `designs/c64_basic`, `origin/main` `23d25554` (2026-11-23), read only.
It is flat: host and C64 in one bitstream (`top.v:612` `ultimate_sys`, `:1355` `c64_core`, `:1550` `vic_scaler`). Last measured build:
27,593 LUT (84.6 %), 61 of 75 block RAM tiles, WNS +0.104 ns (TRX64-Ultimate `docs/specs/U19-app-riscv-caches.md:81`, CI `54dc9f9e`).
Run 1924 used 8,147 of 8,150 slices (`U16-hdmi-modes.md:284-285`).

1.3.2 This repository held a copy as `cores/trx64` until 2026-11-08 (T115 D12). `cores/c64u` is that copy without "the ring, the TRX64
engine, the debugger, the lens unit, the state bus" (`cores/c64u/layer2_c64u.v:1-7`; `c64u_io.v:1-4`: "Derived from trx64/trx_io.v
... with the pages of the ring, the engine, the state bus and the debugger gone"). It is a partial layer 2 (contract 2), has no
`core.toml` and was never built (T103).

1.3.3 The modules of `designs/c64_basic`, by what their code does:

| Module | Instance | What it does | D1 |
|---|---|---|---|
| `trx_engine.v` | `top.v:1443` | frame ring: RAM write log, frame markers, chip blocks into DDR2 through `mig_arb`'s engine client (`trx_engine.v:2-14`, `top.v:771`); instruction trace (`:93-95`); mover COPY/FILL (`:80`) | out (ring) |
| state bus, staging, restore | `c64_core.v:870-876`, `trx_sys.v` page 1 | capture and restore of chip state for the ring | out (ring) |
| `trx_dbg.v` | `c64_core.v:878` | stops, steps, counters, exec bitmap, watch units (`trx_dbg.v:2-5`) | out (monitor) |
| `drv_dbg.v` | `top.v:1434` | drive A comparator, step, halt (`drv_dbg.v:2-12`) | out (monitor) |
| `dma_win.v` | `top.v:1280` | the app's DMA window into the C64 (`dma_win.v:2-26`); the lens unit, a look without a bus cycle (`:28-35`) | window stays, lens unit out |
| `trx_io.v` | `top.v:801` | the app's IO block: pages 1 state bus, 2 engine, 3 debug, 5 lens unit; pages 0 ID and interrupt, 4 cartridge, 6 video, 7 expansion port, 8 sampler (`trx_io.v:3-8`, `:27-31`) | split by page |
| `trx_sys.v` | `top.v:1474` | C64-clock pages: gate and freeze, capture control, turbo status (page 0), state bus (1), lens unit (5), expansion port (7) (`trx_sys.v:2-4`, `:17-40`) | split by page |
| `jtag_dbg.v`, `rv_dbg.v`, `con_ring.v` | `top.v:1160`, `:1148`, `:1170` | board debug aids: JTAG master, RISC-V tap, console ring | Q7 |

1.3.4 The standard blocks stay: the 6510 (`cores/icepi/c64_top.v:536`), the Kawari VIC-II (`:425`), the SID, reDIP:SID (`:659`; the
8-SID build is the default, `designs/c64_basic/build.tcl:80-83`), the REU (`:1217`), the cartridge (`:1289`; `c64_core.v:646`
`cart_slot`, `:717` `slot_port`), turbo (`:1602`), RAM and ROM (`c64_core.v:738`), the sampler (`:531`), the IEC bus (`top.v:682`) with
drive A, a 1541 (`ultimate_sys.vhd:1430` `drive_unit`), the UCI (`ultimate_sys.vhd:621`), the SID sockets (`top.v:939`), the UDP
streams (`:1176`, `:1185`), and the host part (RISC-V system, USB, SD, Ethernet, HDMI, overlay, pixel layer).

### 1.4 The parts

- **P1 Base.** The trx64 machine without the modules marked "out" in 1.3.3 (D1), as a core of this repository with its own lane and
  cache key.
- **P2 Bus and IRQ.** VIC-X sits on `$D000-$D3FF` beside the VIC-II and takes an access only with `claim`. The core's read mux takes
  `cpu_dout`; the block's header says it is valid three clocks after `cpu_rd` (`vicx.vhd:16-25`; not checked against its code). `irq`
  joins the VIC-II's IRQ line (spec page, "IRQ"). The turbo registers `$D030`, `$D031`, `$D07A`, `$D0BC` (`docs/layering-map.md:74`)
  are outside `$D040-$D06F`.
- **P3 Picture.** VIC-X needs per pixel the VIC-II colour index, the core's RGB for it, `pix_ce` at least four clocks apart, line and
  frame starts, the raster line and the three flags background, 320x200 window, border (`vicx.vhd` ports `:158-167`). At the C64 clock
  of 31.5278 MHz (`top.v:184`) a VIC pixel lasts four clocks. Whether the Kawari VIC gives the three flags is not read. VIC-X gives
  24-bit RGB. The trx64 picture path carries the 4-bit index: `vic_scaler.v` keeps 64 lines in "32K x 4 bits of block RAM"
  (`vic_scaler.v:5-6`), and the UDP video stream sends the index (`top.v:1176`).
- **P4 VRAM.** In block RAM: 128 KB = 32 RAMB36, 64 KB (the spec's minimum) = 16 (arithmetic). Outside: the `vx_*` port to DDR2; only
  then do the DMA engine and the XRAM windows work (`hdl/README.md:160-163`). DDR2 latency of the T142 bench model: 293-693 ns (T142 S2;
  T142 D4: a model, not a board figure), 9-22 clocks at 31.5 MHz, against the 40 clocks VIC-X passes. Without `trx_engine` its
  `mig_arb` client (`top.v:771`) is free.
- **P5 XRAM and REU.** XRAM is the REU's memory and exists only while the REU is on; `CAP_XRAM` is the REU size in MB (spec page; port
  `xram_mb`). The trx64 REU uses 16 MB of DDR2 from byte 16 MB (`top.v:293-299`). A CPU access in an XRAM window becomes a DDR2 access.
  The trx64 C64 RAM is block RAM that answers in one clock (`c64_mem.v:1-9`); the turbo CPU (T29) runs faster than 1 MHz.
- **P6 DMA.** VIC-X reaches C64 RAM through `vx_*` at `C64_BASE` (`vicx.vhd:137`, `:1088`) and raises `cpu_halt` while a descriptor touches
  C64 RAM; the spec: "halts the CPU the way the REU does, one byte per cycle". The trx64 CPU hold exists (`c64_core.v:549`
  `cpu_pause_req || held || dma_busy`). The C64 RAM's second port is shared by outside users and the turbo CPU (`c64_mem.v:10-17`); the
  DMA needs a path to it.
- **P7 Standard blocks.** The blocks of 1.3.4 work as in the trx64 core. A stock program sees no difference while VIC-X is locked.
- **P8 Area and timing.** The whole design on the XC7A50T: 32,600 LUT, 75 block RAM tiles, 120 DSP (`hdl/README.md:50-52`).
- **P9 App.** Its own app from the SDK, born in the matrix (T239.5 D13): `core.toml`, TOML A (`gateware.toml`) proven by
  `tools/rtl_check.py`, TOML B (`sdk.toml`) that links its libraries (T239.3 D55, D58), `desc.toml`. The person sees the core in the boot
  menu and starts a stock C64. Its menu has the standard settings (drive, SID, REU size, cartridge) and the VIC-X settings. The ROMs
  (KERNAL, BASIC, character, 1541) come from the person, as for every C64 core.
- **P10 Source.** A copy of the VIC-X sources in this repository that can be renewed from upstream.
- **P11 Licence.** VIC-X (1.5) in one bitstream with Gideon's GPL-3.0 code (`vendor/gideon/LICENSE.txt:1-2`), our GPL-3.0-or-later
  code and the CERN-OHL-S-2.0 reDIP parts (`LICENSING.md`).

### 1.5 Licence of VIC-X, as found

- `LICENSE`: the text of the GNU GPL "Version 2, June 1991", nothing added. Its line 299 ("(at your option) any later version") is in
  the appendix "How to Apply These Terms", a template, not a grant.
- `README.md:225-227`: "VIC-X is free software, released under the GNU General Public License, version 2 or (at your option) any later
  version, the same terms as VICE and the MiSTer Commodore 64 core it is built into. See `LICENSE`."
- The VHDL files (`hdl/vicx.vhd`, `vicx_layer.vhd`, `vicx_sprites.vhd`, `hdl/tb/*`) have no licence or copyright line.
- GitHub reports `GPL-2.0`; it reads the `LICENSE` text only.
- The README is the only place that says "or any later version". With it, VIC-X can be combined with GPL-3.0 code and the bitstream is
  distributed under GPL-3.0. With "version 2 only" it could not be.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: a stock program sees a stock C64
  Given C64-vicx after power-on, VIC-X locked
  When  a self-checking VIC-II program of VIC-X's tests/stock-compat.txt runs
  Then  it reports the same result as on the trx64 core
  And   $D040-$D06F read as VIC-II mirrors

Scenario: a program unlocks VIC-X and draws a bitmap layer
  Given C64-vicx after power-on
  When  t10_bitmap, assembled with -D HW_RESULT, runs
  Then  the border is green and the top line shows PASS
```

| # | Solves | Scenario | Test (bench / board step) | Where it runs |
|---|---|---|---|---|
| S1 | P1 | The routed design has no cell of `trx_engine`, `trx_dbg`, `drv_dbg` or the lens unit | `utilization_hier.rpt` of the core's Vivado run | CI, core lane |
| S2 | P2 | t01, t03, t04, t05, t07, t08 pass (unlock, ID and caps, ports, palette, colour remap); t04 expects the full build's capability values (`README.md:204-205`) | conformance runner (Q10) | Q10 |
| S3 | P3 | t10-t26, t29, t30 pass with their pixel checks | conformance runner (Q10) | Q10 |
| S4 | P3 | The demo `sixteen` shows its picture on HDMI; photo | by hand, Alex | board session |
| S5 | P4 | `tests/hw/vram_check.s` passes: all VRAM both ways, step-640 reads, write then read on the other port | conformance runner (Q10) | Q10 |
| S6 | P5 | t27 passes with the REU at 16 MB; `tests/hw/reu_check.s` passes at 16 MB | conformance runner (Q10) | Q10 |
| S7 | P6 | t28 and t31-t35 pass with the REU at 16 MB; the demo `bigworld` runs from its D64 (photo) | runner (Q10); by hand, Alex | Q10; board session |
| S8 | P7 | VIC-X locked: every program of `tests/stock-compat.txt` gives the same result as on the trx64 core | conformance runner (Q10) | Q10 |
| S9 | P7 | A stock program loads from a D64 on drive 8; a SID tune plays (heard); an REU program runs; a cartridge starts | by hand, Alex | board session |
| S10 | P8 | The flat build routes with LUT ≤ 85 % (27,710), block RAM ≤ 74 tiles, WNS ≥ 0, 0 routing errors (the budget of T182 S14, T182 §5.3) | `utilization.rpt`, `timing_summary`, `route_status.rpt` | CI, core lane |
| S11 | P9 | `tools/rtl_check.py check` passes for the core's `gateware.toml`; the app compiles against it; `tools/check_d24.py` and `tools/core_toml.py lanes` pass | lint and app stage | CI, matrix |
| S12 | P9 | The core's matrix lane is green: bitstream, app, `.cor` | the run's report | CI, matrix |
| S13 | P9 | The core starts from the boot menu; its menu shows the standard and the VIC-X settings | `ux-review` in ue2emu, then by hand, Alex | ue2emu; board session |
| S14 | P10 | Every line of VIC-X's `hdl/run_sim.sh` passes on our copy (block RAM and external VRAM) | `cores/<id>/sim/vicx/run.sh` in our GHDL container | `ci/sims.txt` |
| S15 | P11 | The copy has a NOTICE with the upstream commit and the licence statement of 1.5; the package's licence list names VIC-X | review, by reading | before the merge |

## §3 Decision

- **D1 (Alex, 2026-11-26):** "Quelle sollte der trx64 Core sein ... ohne Ring, Monitor usw." (translated: "The source should be the
  trx64 core ... without ring, monitor etc."). The base is the trx64 C64 of 1.3. The modules marked "out" in 1.3.3 do not go in; the
  standard blocks of 1.3.4 stay.

- **D2 (Alex, 2026-11-26): both boards are looked at, the 50T and the 100T; on the 50T, if needed, the repurposed standard blocks give way,
  not VIC-X.** Asked whether the core targets the 100T only, the 50T with blocks given up, or both: "Erstmal beides anschauen. Und ggfs bei
  den umfunktionierten reduzieren." (translated: "Look at both first. And reduce, if needed, among the repurposed ones.") Asked whether
  "the repurposed ones" are the blocks taken over from the trx64 core: "Ne, steaming, sampler usw. Genau." (translated: "No, streaming,
  sampler etc. Exactly."). So VIC-X stays the full build where it fits; on the 50T the levers of Q2 (UDP streams, sampler, turbo, the JTAG
  debug master, the number of SIDs) give way in that order of need, until it fits; on the 100T nothing gives way. Q2 is answered by this
  for the 50T (A); which blocks exactly is measured, not guessed (the first Vivado run of each board).
- **D3 (Alex, 2026-11-26): a large core.** Asked Q1 (large and flat, host and core in one bitstream, or partial in `pb_l2`, where even
  `c64ref` plus the smallest VIC-X build does not fit): "Ja ein large Core." (translated: "Yes, a large core."). Host and core are flat in
  one bitstream with the core's own copy of the host files, as `cores/c64scpu/mono/host`.
- **D4 (Alex, 2026-11-26): VRAM in the DDR2, one design for both boards.** Asked Q3 (VRAM in block RAM, in the DDR2, or per board): "B. Und
  so Kram haben wir doch für die scpu auch schon gehabt." (translated: "B. And we already had that kind of thing for the SuperCPU.") VIC-X
  runs with `EXT_VRAM => true`; its `vx_*` port reaches the DDR2 through a client of the memory arbiter, built the way `c64scpu` keeps its RAM
  and ROM in the DDR2 behind slot and line caches (`cores/c64scpu/mono/core.toml:19`, `cores/c64scpu/PROVENANCE.md:21`) - that path is
  read and reused before anything new is written. The XRAM (the REU's memory) is in the DDR2 already.
- **D5 (Alex, 2026-11-26): the picture path is B or C, chosen by measurement; A is out.** Asked Q4 (widen the scaler ring, fewer lines in a
  wider ring, or a DDR2 frame buffer): "Naja wie wir das Bild bekommen muss sich dann zeigen. Er hat ja für mister gebaut. Anderer Chip. Ich
  tendiere zu b oder c" (translated: "Well, how we get the picture will have to show then. He built for the MiSTer. A different chip. I lean
  towards b or c"). VIC-X's video interface was made for the MiSTer's C64 core; ours differs. The first build measures both: B's block RAM
  and the depth proof of `tools/hdmi_modes.py check` for the shorter ring, C's DDR2 bandwidth beside VRAM, XRAM and the C64's RAM; the
  result goes to Alex before the choice is built.
- **D6 (Alex, 2026-11-26): turbo first, as on the MiSTer.** Asked Q5 (a window access holds the CPU until the DDR2 answers, turbo off
  while VIC-X is unlocked, or turbo first with VIC-X taking what is left): "C". The turbo keeps its priority; VIC-X's XRAM windows and DMA
  take the memory cycles the turbo leaves (`hdl/README.md:205-206`).
- **D7 (Alex, 2026-11-26): the copy is TRX64-Ultimate's `c64_basic` at a pinned commit; spec only, nothing is built now.** Asked Q6
  (`c64_basic` pinned, or this repository's older `cores/c64u`): "A, aber bitte NICHTS bauen jetzt, nur specs schreiben." (translated: "A,
  but please build NOTHING now, only write specs."). The core starts from `designs/c64_basic` at a pinned commit (today `23d25554`), with
  ring and monitor cut by hand. No branch, no agent and no build of T263 starts until Alex says so; until then only this spec changes.
- **D8 (Alex, 2026-11-26): the board debug aids only in development builds; the rest shows in the first Vivado route.** Asked Q7 (what
  "etc." covers besides ring and monitor: `jtag_dbg`, `rv_dbg`, `con_ring` out, in development builds only, or in): "Erstmal B ... gibt es
  noch echt fragen ? Weil das zeigt sich erst alles wenn wir das ding das erste mal durch Vivado routen, also was geht und wie am besten"
  (translated: "B for now ... are there still real questions? Because all of that only shows when we route the thing through Vivado the
  first time, what works and how best"). The debug aids are built only with a development switch, as `cores/c64scpu/mono/build.sh --jtag`.
  Q8-Q11 are not asked: each takes the proposal below as its starting point, and the first Vivado route and the first build show
  whether it holds; a change goes to Alex then.
- **Starting points for Q8-Q11 (coordinator, 2026-11-26, after D8; not decisions of Alex):** Q8 A (a copy at a pinned commit with a renew
  script, as `tools/gideon_sources.sh`); Q9 A (the README's "or any later version" as the grant, recorded in the copy's NOTICE with the
  commit); Q10 B for CI (the Verilator machine bench with a `$D7FF` result register, as `cores/c64scpu/sim`) and C for the board step;
  Q11 A (the SDK's shared libraries, the VIC-X settings as data of the core's TOML, a "VIC-X off" setting as VICE's `VICXEnable`).

**Open questions for Alex** (one at a time):

- **Q1 Large or partial.** Answered: D3 (large).
  A: large: host and core flat in one bitstream, with the core's own copy of the host files (as `cores/c64scpu/mono/host`, T164 3a Q1).
  The trx64 core is built this way.
  B: partial in `pb_l2`: 13,600 LUT, 40 tiles, 40 DSP (T164 §5, WP19 result, run 1122). `c64ref`, a C64 without SID and REU, takes 9,212 LUT there;
  with the smallest VIC-X build (5,311) that is 14,523 LUT, above the partition.
- **Q2 Which VIC-X build, and which standard blocks give way.** LUT arithmetic: trx64 27,593 (1.3.1) minus ring, debugger and freeze
  responder 2,400-3,000 (T103 §1 "Area estimate": Vivado run 275 931 + 962 + 531; yosys figures up to about 3,000) = 24,593-25,193; plus:

  | VIC-X build | LUT | of 32,600 | Inside the spec |
  |---|---|---|---|
  | Full | 34,143-34,743 | 105-107 % | yes |
  | Sprites 64/16 | 33,124-33,724 | 102-103 % | yes |
  | No DMA | 31,394-31,994 | 96-98 % | no: copy, fill and masked copy are required in every build |
  | Layers only | 29,904-30,504 | 92-94 % | no |

  Not counted: the glue (bus, flags, DDR2 adapter) and the picture path (Q4). The highest flat LUT count that ever routed on the 50T is
  28,451 (87.27 %, the C128, T183 §1); S10's budget is 27,710. By this arithmetic no VIC-X build fits beside the whole standard set. For
  comparison, `c64scpu` step 1 (a C64 with its 6510 from the C128 tree, no turbo, RAM in DDR2) routed at 21,958 LUT and 53 tiles
  (`cores/c64scpu/mono/reports/s1/utilization.rpt`); with the full block that is 31,508 (96.7 %). Figures for the levers inside the
  standard set: JTAG debug master about 690 LUT (`cores/c64scpu/mono/core.toml:25-26`); UCI and UDP streams about 740, mixed tools
  (`docs/layering-map.md:103-104`); turbo 530-660, an estimate (T103); sampler about 560 (T103); the SID block of `c64ref` 2,976 LUT,
  3 RAMB, 20 DSP (run 1104, T164 3b); 8 SIDs against one SID is not measured. Block RAM: 61 tiles minus the ring's two FIFOs (one tile each,
  `trx_engine.v:8-9`, `:93-95`) and the debugger (about 2, yosys, T103) = about 57, plus 15 / 9 / 8.5 / 4.5 for the builds above, plus Q3 and Q4.
  A: full VIC-X; standard blocks give way until it fits.
  B: sprites 64/16 (inside the spec); fewer standard blocks give way.
  C: a build below the spec's required set (no DMA, or layers only); still above S10's budget by arithmetic, so blocks give way too.
- **Q3 VRAM inside or outside.** Answered: D4 (DDR2).
  A: block RAM, 32 tiles (128 KB) or 16 (64 KB); with the full block about 57 + 15 + 16 = 88 of 75 tiles. No DMA engine and no XRAM
  windows (`hdl/README.md:160-163`).
  B: DDR2 through the client that `trx_engine` leaves free (`top.v:771`), or a new `mig_arb` client; an adapter for 8-byte reads and
  16-bit writes. The upper 64 MB are reserved only for the trx64 ring (T138 D2, Alex 2026-10-28), apart from the host's still capture
  at `0x5050000-0x506FFFF`; or a range in the app's lower 64 MB.
- **Q4 The picture path from the index to RGB.** Narrowed: D5 (B or C, by measurement).
  A: widen the scaler's 64-line ring from 4 bits to RGB565 (16 tiles) or RGB888 (24 tiles) instead of 4 (arithmetic).
  B: fewer lines in a wider ring; the depth is proven by `tools/hdmi_modes.py check` (`vic_scaler.v:30-32`), so the proof changes.
  C: a DDR2 frame buffer through the pixel layer, as the VC-20 and the ZX draw (T119).
  The UDP video stream (`top.v:1176`) needs the same answer.
- **Q5 XRAM windows and turbo.** Answered: D6 (C).
  A: a window access holds the CPU until DDR2 answers; turbo stays on.
  B: turbo off while VIC-X is unlocked.
  C: as on the MiSTer: turbo first, VIC-X takes what is left (`hdl/README.md:205-206`).
- **Q6 Which copy of the trx64 core.** Answered: D7 (A).
  A: TRX64-Ultimate `designs/c64_basic` at a pinned commit (now `23d25554`): the shipped state with the U16 HDMI rows and the U19
  caches. Ring and monitor are cut here by hand: `c64_core.v` and the state bus have no build switch for them
  (`docs/layering-map.md:94`). A later trx64 fix arrives only with a new copy.
  B: this repository's `cores/c64u` (1.3.2): ring, engine, state bus, debugger and lens unit are already out. It is older than U16 and
  U19, a partial that was never built, and carries T103's stock additions (UCI, streams, tape).
- **Q7 What "etc." covers besides ring and monitor:** Answered: D8 (development builds only). the board debug aids `jtag_dbg` (about 690 LUT), `rv_dbg`, `con_ring`.
  A: out. B: in dev builds only, as `c64scpu` (`TRX_TOP_OFF=NO_JTAG_DBG`, `cores/c64scpu/mono/core.toml:25-27`). C: in.
- **Q8 Where the VIC-X sources live.** The spec is "Draft specification, version 0.1"; renewals are to be expected.
  A: a copy in this repository at a pinned commit, with a renew script as `tools/gideon_sources.sh` (T120).
  B: a git submodule; the repository has none since T120.
  C: a copy inside `cores/<id>/`, renewed by hand (copies, not links: Alex, 2026-11-08).
- **Q9 Licence (1.5).**
  A: take the README's "or any later version" as the grant, record it with the commit in the copy's NOTICE, distribute under GPL-3.0.
  B: first ask the author for the statement in `LICENSE` or as SPDX lines in the files; build meanwhile, ship nothing.
  C: keep the core TREX-internal until it is settled, as `c64scpu`'s third-party files (`cores/c64scpu/PROVENANCE.md:5-6`, T113 Q9).
- **Q10 Where the conformance programs run.**
  A: the headless VICE fork in a container on the builder. It proves the reference model and the programs, not our gateware; the fork's
  build is documented for MSYS2 on Windows only (`README.md:116-127`).
  B: our Verilator machine bench, as `cores/c64scpu/sim` (`run_base.sh`, `check_screen.py`), with a `$D7FF` result register in the
  testbench. It proves our gateware in CI; it needs ROMs (synthetic in CI, real ones only locally) and a frame dump for the pixel
  checks; t06 and t09 do not apply.
  C: the board over REST, as T250: load the PRG, read `$0400-$0406` and `$D020` (the `HW_RESULT` output); pixel checks need a frame grab.
- **Q11 The app's libraries and VIC-X settings.**
  A: the SDK's shared libraries (`cbm1541`, `cbmrun`, the cartridge library) and a VIC-X settings group, built here.
  B: trx64u's library from TRX64-Ultimate, linked through TOML B (T239.3 D58), without ring, monitor and online.
  Also: whether the menu has a "VIC-X off" setting, as VICE's `VICXEnable` (off: the unlock does nothing, t02).
- **Q12 Name and id.** A: id `c64vicx`, name "C64 with VIC-X", `cores/c64vicx`. B: `c64x`, the MiSTer core's name "C64X"
  (`README.md:191-194`). C: another name. "VIC-X" is a working name (`README.md:3`).

## §4 Out

- The ring, the monitor and the debugger: in the gateware (1.3.3, "out") and in the app (ring, monitor and trxmon go with trx64u into
  its library in TRX64-Ultimate, T239.3 D2). Alex, 2026-11-26: "Kein Fokus auf Ring, Monitor usw." (translated: "No focus on ring,
  monitor etc."), and D1: "ohne Ring, Monitor usw." Nothing else is out; every other open point is a question in §3.

## §5 Decisions taken while building

Not started.
