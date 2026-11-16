# T119 — VC-20 and ZX Spectrum draw into a DDR2 frame buffer of the pixel layer, not through the host's vic_scaler

**Status:** DECIDED (2026-10-23, Alex). 2026-11-13 status check: built and merged (VC-20 and ZX draw through the pixel layer, Merge t119-zxblack, t164-zx48e); ZX 48K picture on the board 2026-11-04 (todo); state: in progress as in row. **2026-11-15 sync:** VC-20 part proven on the board (menu and picture fit, Alex 2026-10-23), ZX 48K picture on the board 2026-11-04; the 128K path worked earlier (todo 2026-11-01); no §2 scenario table was re-checked here.
**Repo:** TRX64U-MultiCore — `cores/vc20/`, `cores/zx/` (a pixel writer each, as `cores/c264/rtl/trx/c264_pixw.v`), the
core app's per-core setup where the C264 has one. The host is not touched (frozen, `tools/host_pin.sh`).
**Number:** T119 (TODO table; T117 unused, T118 is the HDMI geometry bench on `t118-hdmigeom`).
**Origin:** Alex, 2026-10-23, on the VC-20 menu cut at all four edges: "So it would be good to do only for the
non-C64 cores, right?" and "Didn't YOU want to build into all generic app cores that they use the DDR2 and do not go
straight to the vic scaler?!" (translated).
**Depends on:** T90 (the C264 pixel writer as the model), T104 (generic core app).
**Milestone:** M3.

---

## §1 The problem

- P1 Picture path. Every non-C64 core but two writes its picture into a DDR2 frame buffer that the host's pixel
  layer scans out (A500/A1200 `amiga_fb.v`, CPC, C264 `c264_pixw.v`, Apple IIe, NES, Atari, ZX Next). VC-20 and ZX
  still hand a palette raster (`pix_color/pix_x/pix_y`) to the host's `vic_scaler`, the C64's path.
- P2 What Alex sees. The same `vc20.cor` (sha256 7f105491…, identical in releases 2895ec1a and f6f3dfdc) showed
  the menu cut by about one character on all four edges once and fitting once, both times started from the host.
  The Amiga cores, on the frame buffer path, have always fitted. T118's bench found the HDMI timing identical
  for VC-20 and A1200; only the scaler's lock time differs (389,751 vs 190,241 pixel clocks).
- P3 Cost. Each core needs a writer on the general DDR2 port `e_*`; the core's LUT budget and the DDR2
  arbitration with its other users (ZX: none today; VC-20: none) have to fit.
- P4 The app. The core app has to set the pixel layer up for these cores as it does for the C264 (base, stride,
  scale, position) and leave the vic_scaler path idle.

## §2 Scenarios

```gherkin
Scenario: S1 the VC-20 picture reaches the frame buffer (bench tb_vc20_pixw)
  Given the VC-20 core running its KERNAL to the READY screen
  When  the writer has written two frames
  Then  the frame buffer holds the border and the READY text at the pixel positions of the core's raster
  And   the mutation VC20_MUT_PIXW (one column offset) fails the bench

Scenario: S2 the ZX picture reaches the frame buffer (bench tb_zx_pixw)
  Given the ZX core running its ROM to the copyright screen
  When  the writer has written two frames
  Then  the frame buffer holds the border and the copyright line at the expected positions
  And   the mutation ZX_MUT_PIXW fails the bench

Scenario: S3 the menu fits every time (board, Alex)
  Given the release with both cores
  When  Alex starts the VC-20 from the host five times, and the ZX five times
  Then  the menu shows all four edges every time, and the machine picture is centred and complete
```

## §3 Decision

Both cores get a pixel writer modelled on `c264_pixw.v` (RGB565 into the pixel layer's buffer through `e_*`), the
core app sets the pixel layer up as for the C264, `pix_*` towards the vic_scaler goes idle. C64 cores (trx64,
c64u) stay on the vic_scaler; the large `.cfw` cores are not affected.

## §4 Out

- The host, the vic_scaler and the encoder (frozen).
- trx64 and c64u (Alex: "trx64 and the c64u core run fine").

## §5 Built

### 5.1 VC-20 (branch `t119-vc20`, agent, 2026-10-23)

- **Writer.** `cores/vc20/rtl/trx/vc20_pixw.vhd`, VHDL because the core is (the bench runs under GHDL), modelled on `c264_pixw.v`:
  line buffer in `sys_clk`, one write port (inferred as 2 RAMB36), the line goes to the `ui_clk` side by a toggle, one 128-bit MIG
  write per word, four buffers, `FB_LATEST` / `FRAME_SEQ` / `FB_OVERRUNS`, `FB_BASE` / `FB_STRIDE` registers (0C0-0CB of
  `vc20_regs.vhd`, same offsets and reset values as the C264: 0x01D00000 / 0x00100000), frame-done cause b5. Wired in
  `vc20_core.vhd` (ports `ui_clk`, `ui_rst`, `e_*`, bench-only taps `pix_tap`, `fb_tap` that the layer leaves open) and
  `layer2_vc20.v` (`e_*` no longer tied idle). Contract ports unchanged, `host/` untouched.
- **Picture size (measured from `m6561.vhd`, not from its comment).** The 6561 shows 228 x **283** pixels (hblank low 228 pixel clocks;
  vblank falls at the end of line 28 and rises at the end of line 311), not 284: the first bench run with LINES = 284 never
  completed a frame, which is what the bench is for. A VIC-20 pixel is about 5 : 3 wide, the layer scales whole numbers only, so
  the writer turns every 3 VIC-20 pixels into 5 buffer pixels (widths 2, 1, 2): 380 per line, window 376 (two dropped at each side,
  1.2 VIC-20 pixels of border), 283 lines 1:1. Buffer 376 x 283 RGB565, 752 bytes a row, 3 x 3 on the layer = 1128 x 849 at (396, 115).
- **Colours.** The 16 colours of `m6561.vhd` (col0..colF, the active set) as RGB565 (`{r, r[3], g, g[3:2], b, b[3]}` as the C264); the
  colour number comes from the 6561's `o_colour`, at the same pixel enable as hblank/vblank. The nearest-C64-index mapping and
  `CTRL` bit 2 are gone.
- **pix_\*.** As the C264: `vc20_video` now gives the idle raster (504 dots of 4 clocks, 312 lines, colour 0, free running, 628,992
  `sys_clk` a frame = the VIC-20 frame of `vc20_nco`) and the frame pulse for the frame counter. `tb_vc20` keeps its raster checks
  (period, lock line, across a reset) and loses the colour counts, which moved to the new bench.
- **App.** `coreapp/cores/vc20.cc` descriptor `{ 376, 283, 3, 396, 115, 0x01D00000, 0x00100000, 4, FB_LATEST, FRAME_SEQ, FB_BASE,
  FB_STRIDE }` as the C264's (`vc20_regs.h` has the offsets); `tests/coreapp/test_vc20.cc` checks the layer set-up (size, stride,
  position, ctrl, front buffer, flip) with two new controls (`vc20_fbscale`, `vc20_fbregs`): 194 checks, both controls red.
- **S1 bench** `tb_vc20_pixw` (`cores/vc20/sim/run_pixw.sh`, `ci/sims.txt` `vc20_pixw`, `ci/sims.deps`). No Commodore ROM may be in the
  repository, so "READY" is the hand-written test program of `gen_testrom.py` (VIC set up as the KERNAL does, white background, cyan
  border, every screen code on the screen) with a character set of non-repeating patterns written through the app's ROM window.
  The model takes a reference picture from the 6561's own outputs (colour, hblank, vblank at every pixel enable) with its own palette
  and its own 3 -> 5 rule, and compares every pixel of each reported frame in the buffer `FB_LATEST` names; it also reads the registers
  through the app's window (reset values, FB_BASE/STRIDE written and used), checks every e_ command (cmd 0, data with the command,
  mask 0, 16-byte aligned, inside the buffers; the port is busy one clock in eight), the buffer ring, no late line, and that the
  first pixel is the border's RGB565 (22395 = 0x577B). Result on the builder (GHDL 6.0.0, `trx64-sim-env:4`): 
  `frame 1..3: 0 of 106408 pixels differ`, `PASS tb_vc20_pixw`; with `VC20_MUT_PIXW` (generic `MUT_PIXW = 1`, window one pixel late)
  `15756 / 15981 / 15981 of 106408 pixels differ`, `FAIL every pixel of every reported frame ...`, `FAILED tb_vc20_pixw: 1`,
  reported as `PASS control VC20_MUT_PIXW`. 250 s alone on the idle builder; budget 1200 s. `vc20` and `tb_vc20_pause` / `_ext` still pass.
- **Size.** yosys `synth_xilinx` (not Vivado): `vc20_pixw` about 380 LUT, 594 FF, 2 RAMB36; `vc20_core` in all about 2,540 LUT,
  2,040 FF, 16 RAMB36 + 4 RAMB18 (yosys counts, Vivado will differ). Routing in CI is open.
- **Not done / open.** S3 (five starts on the board, Alex) is not run; the pause keeps writing the (frozen) picture because the 6561
  runs on during a pause (nothing is gated, so the picture is the same frame again); the 5 : 3 stretch gives unevenly wide columns
  (6, 3, 6 output pixels), a choice of mine for the aspect, to be judged on the screen. The bench uses the test program, not a
  KERNAL READY screen; a local run with real ROMs (`run_boot.sh`) was not done.

### 5.2 ZX Spectrum (branch `t119-zx`, agent, 2026-10-23)

- **5.1 The writer.** `cores/zx/trx/zx_pixw.v` is `c264_pixw.v` with the ULA's window: 344 x 280 RGB565 (688 bytes a row, 43 words, four
  buffers of 1 MB stride from `0x01D00000`, the C264's reset values; the tape stream at `0x01800000` + 4 MB does not touch them).
  The ULA's visible raster is 344 pixels wide (HBlank 312..416 / 424) with the paper 44 pixels in, and 44 + 192 + 44 lines of the
  284 (48K) / 283 (128K) between its VBlank edges; the window takes 44 border lines above and below the paper and all 344 pixels. The
  128K frame has a line less, so `y0` (lines skipped after VBlank) is 3 for the 48K and 2 for the 128K, handed in by `zx_core`; the
  paper is at column 44 / row 44 in both. `dotst` is `ce_7mp`, the clock at which the old path already sampled `{I,G,R,B}` and the
  blanking, so the pause (no pixel enable) stops the writer; `halted` waits for the writer's `quiet` as it waits for the memory.
  The colour is `{R?lv5,G?lv6,B?lv5}` with `lv` 215 / 255 of 255 (26 / 31, 53 / 63): the levels the app wrote into the host's palette
  before. Bright black is black in RGB (the machine has no other black either).
- **5.2 `e_*` is free.** `zx_mem.v` is block RAM and the tape reads through `c_*`; nothing else used `e_*`, so the writer drives it
  directly and `layer2_zx.v` no longer ties it off (no arbiter, unlike `zxn_pixw.v`).
- **5.3 `pix_*` idle as the C264 leaves it.** C264 does not park the raster: it feeds the scaler a dummy raster (504 dots of 4
  clocks, 312 lines = 628,992 clocks, colour 0, never stops) so the scaler locks. `zx_core` does the same; the old lock marker and the
  `vcm`/`cdot`/`nd` picture logic are gone. `ZX_MUT_NOMARK` now removes line 20 from the dummy raster.
- **5.4 Registers and JTAG.** `FB_LATEST 0C0`, `FRAME_SEQ 0C1`, `FB_OVERRUNS 0C2`, `FB_BASE 0C4`, `FB_STRIDE 0C8` in `zx_regs.v`
  (the C264's offsets and CDC); `DOT0` reads 0, `GEOM_LINES` 280, `PIX_W` 344. JTAG words 0-4 unchanged; 5 = `{fb_over, d_ls}`,
  6 = `{d_vis, fb_latest, fb_seq}`, 7 = `{dy, 0, dx, HBlank, VBlank, VSync, hb_p, 00}` (dummy raster and ULA flags); 8 = `{e_acc, lines_out}`, 9 = the writer's ui state (5.9), 10 / 11 = HBlank rises, blanked dots, pixel enables, line starts (5.10).
- **5.5 The app.** `coreapp/cores/zx.cc`: `write_palette` removed (the host palette is not used any more); the descriptor's video record is
  `{ 344, 280, 3, 444, 120, 0x01D00000, 0x100000, 4, FB_LATEST, FRAME_SEQ, FB_BASE, FB_STRIDE }` (3 x 3 = 1032 x 840, centred on
  1920 x 1080 as the C264's 384 x 288 is; `Video::init` writes FB_BASE / FB_STRIDE and the generic app flips on FRAME_SEQ). Not compiled
  here (no cross toolchain on this machine, the descriptor follows `c264.cc` field for field); the next CI/app build decides.
- **5.6 Benches (builder, `trx64-sim-env:4`).** `tb_zx` has a model of `e_*` (random busy one clock in eight) and turns every frame-done
  buffer back into colour indices at the old ring's place, so `video48`, `shadow128` and `pause` check what they checked before, now on
  the frame buffer; `zx_exp` maps bright black to black. New modes `pixw` (48K) and `pixw128` (shadow screen, 128K) add: paper at
  column 44 exactly, every pixel a palette colour, all 43 x 280 words of every frame written, buffers in a ring, plain write commands only,
  no late line. `mut_pixw` (`ZX_MUT_PIXW`, every pixel takes the colour of the one before: one column offset) puts the paper at column 45
  and fails. Results (`pixw`, `pixw128`, `mut_pixw`): see `docs/todo/T119.md`. The S14 scaler bench now proves the lock on the dummy raster
  (`scaler`, `scaler128`; `mut_nomark` fails it); `clk48/clk128` still measure the ULA's frame (the lock marker part is trivially true on a
  free-running raster). The pause bench counts `e_*` commands while held (0). CI line `zx_pixw` (tier 2, 2400 s).
- **5.7 Not done (ZX).** The board step S3 (five starts, centred and complete picture) is Alex's. The palette write
  of the app is gone for the ZX; nothing else in the app changes. Synthesis (Vivado) was not run; the writer alone is about 310 LUT, 600 FF, 2 RAMB36
  by Yosys `synth_xilinx` (the line buffer, 128 x 128 bit).

- **5.8 Board 2026-10-23: the picture stays black (release main-66eefd51, zx.cor with the writer, CI 949, WNS +0.039 ns).** Alex: "it goes
  white briefly and then nothing more" (translated); console of the core app: ROMs found (`/usb1/roms/zx/48.rom`, `128.rom`), "ZX Spectrum
  48K / 128K released", nothing else. Before T119 (2026-10-22) the scaler path was black too. Read against the C264 (picture works on the
  board) and the CPC (same family, boots BASIC on the board), code only:
  - `zx_pixw.v` differs from `c264_pixw.v` only in the window (`y0` port, 344 x 280, `row_w`) and `ram_style = "block"`; the `e_*` handshake
    (`e_cmd` 000, `e_wdf_mask` 0, `e_en` held until `e_rdy`, data and address stable, one command at a time) is the same code, in `ui_clk`,
    reset by `ui_rst`; nothing else drives `e_*` (`zx_mem` is block RAM, the tape uses `c_*` only, `layer2_zx.v` has no `assign e_*`).
  - The host side (`host/ddr2_port.v` `mig_arb`): `e_rdy = eng = ok & !c_en & !pending[2] & e_en & !(v_ok & v_urgent)`, a combinational
    function of `e_en`, the command and data go to MIG in the clock of `e_rdy`; the writer's `U_PUT: if (e_rdy)` is exactly this contract.
    The old bench's `e_rdy` (random, registered) accepted the same commands; the real one differs only in priority (`c_en` first).
  - Registers: `FB_LATEST 0C0`, `FRAME_SEQ 0C1`, `FB_BASE 0C4`, `FB_STRIDE 0C8` equal `zx_regs.h`; reset values `0x01D00000` / `0x00100000` equal
    `ZX_FB_ADDR` / `ZX_FB_STRIDE_BYTES`; the descriptor record is field for field the C264's; `Video::init` zeroes buffers 0-2 (the fourth lies
    past the app's window), writes the base and stride, `show()` writes PX_STRIDE 688, PX_SIZE 280 x 344, PX_POS (444, 120), scale 3.
  - The app's start (`boot_start`: `zx_init` hold + `C64_MODE_RESET`, ROM, settings incl. the model and the ROM into the core, `video->init`,
    `zx_release`) is the CPC's; `layer2_zx.v` differs from `layer2_cpc.v` in nothing that touches the picture.
  Result of reading: no difference found that explains a black layer. The bench had three gaps, now closed by `pixwhost`: it never ran the
  host's `mig_arb` + a MIG model (stalls, refresh, calibration, `ui_rst` from the model; `c_*` of the tape through the same arbiter), never
  went through the application's way in (host reset line up while CTRL b0 holds and the ROM is loaded; CTRL first, the reset line after it)
  and never read `FB_LATEST` / `FRAME_SEQ` over the register bus. Mode `pixwhost` (`-DZX_HOSTPORT`) does all three and compares the MIG
  model's memory word for word with what the `e_*` port took. **Not decided from code.** Added to the board read: JTAG words 8 and 9
  (`trxdbg ... trx 8` / `trx 9`), see below.
- **5.9 Board read for the next session (no reflash needed for words 0-7; 8 and 9 come with the next build).** Each twice, one second apart:

  | word | value | meaning |
  |---|---|---|
  | `trx 0` | `5053585A` | the ZX core is loaded (else the wrong `.cor`) |
  | `trx 1` | b0 reset, b1 held (Freeze), b2 snapshot busy, b3 128K | b0 stuck 1: the core is in reset (hold or `C64_MODE_RESET` not released); b1 stuck 1: the machine is frozen |
  | `trx 2` | frames (VSync count) | changes ~50 per second: the ULA runs; constant: no pixel enable (`ce_pix`, `hold_q`, reset) |
  | `trx 3` / `trx 4` | CPU address / PC | PC 0000 constant: CPU in reset; PC and address walking through 0000-3FFF then 4000-FFFF in order: ROM area empty (NOPs); PC in 0x15xx-0x16xx (the key wait): the machine booted |
  | `trx 5` | `{fb_over, d_ls}` | `d_ls` +15,600 per second: HBlank toggles; 0: the ULA's blanking is stuck; `fb_over` > 0: lines too late |
  | `trx 6` | `{d_vis, fb_latest, fb_seq}` | `fb_seq` +50 per second and `fb_latest` b7 = 1: the writer completes frames; `fb_seq` = 0 with `trx 5` moving: the writer never finishes a frame (look at 8 and 9) |
  | `trx 7` | `{dy, 0, dx, HBlank, VBlank, VSync, hb_p, 00}` | the dummy raster runs (dy, dx change); the ULA flags toggle |
  | `trx 8` (new) | `{e_acc, lines_out}` | `lines_out` +280 and `e_acc` +12,040 per frame: the host takes the commands; `lines_out` moves but `e_acc` does not: the port is not ready (`trx 9`) |
  | `trx 9` (new) | `{00, ui_rst, us[3:0], u_wb, u_half, ug}` | `us` = 5 (`U_PUT`) constant: waiting for `e_rdy` (the arbiter never grants); `ui_rst` = 1: the writer is held in reset |

  With `fb_seq` moving and `e_acc` counting, read the frame buffer in the DDR2 (JTAG `ddr` window, byte `0x01D00000` + n x `0x100000` for the
  buffer `fb_latest` names): non-zero RGB565 words mean the writer is fine and the fault is in the layer (PX_* are write-only) or in what
  the ULA draws; all zero means the words land elsewhere.

- **5.10 Board 2026-10-24 (release trx64mc-main-54e63c47, `zx.cor`; Alex + coordinator JTAG): 128K shows the 128 menu, 48K keeps the last 128K frame - cause NOT found.**
  Facts: Model = 128K, correct picture (first ZX picture on the board). Model = 48K (setting, then reset): `trx 1` = 0 (not reset, not held, 48K),
  frames 2565 -> 2626, PC 15EE / 15E0 (the 48K ROM's key wait: it booted), `d_ls` E7FE and `fb_over` 0 constant, `fb_seq` 72 constant, `d_vis` moves,
  `{fb_acc, lines_out}` 5B33 / 8519 constant, dummy raster runs, `trx 9` = 01AA. Read from the code, not from a comment:
  - `trx 9` = `{ui_rst 0, us 0 (U_IDLE), u_wb 3, u_half 0, ug 42}`: the ui side finished word 42 (the last of a row) of buffer 3 and waits; `fb_latest` 0x82
    names buffer 2, so buffer 3 is a frame that stopped part-way. `lines_out` 34,073 x 43 = 1,465,139 = 22 x 65,536 + 23,347 = `e_acc` 5B33: every line
    handed to the ui side was written. The writer took what it was given; the stop is upstream of it.
  - `d_ls` is `line_start = hb_p & ~HBlank` counted at `ce_7mp` (`zx_core.v`), the same edge `zx_pixw` keys on (`ls = hb_d & ~hblank`). It stands still while
    `d_vis` (same enable) moves, VSync (frames), INT (the ROM's key wait needs the interrupt) and the CPU run: the ULA's counters run, its HBlank has no falling edge
    that `ce_7mp` sees. `ula.sv` sets HBlank at `hc_next == 312` and clears it at 416 (48K) / 424 (128K) under `ce_7mn` (lines 149-161); `wide`, `overscan` and `mZX`
    are constants of the instance, nothing the CPU, the ROM or the model can reach. The only model terms between the ULA and the writer are the NCO ratio (2/9 or
    5909/26208), the ULA's own constants (447 / 311 against 455 / 310, 416 against 424) and `y0` (3 / 2, which does not touch `d_ls`).
  - `ce_7mp` is `ce_pix` two clocks late, the ULA moves HBlank on `ce_pix`: `hb_p` is stale for exactly the `ce_7mp` that follows the edge, for every spacing of
    `ce_pix` of 3 clocks or more (2/9 gives 4,5,4,5; 5909/26208 gives 4 and 5). Only `ce_pix` on consecutive clocks would lose an edge: that is the NCO's catch-up
    burst when the ratio changes with a stale accumulator (128K -> 48K: about 3,700 clocks, then the accumulator is below the denominator again).
  Reproduction attempts, all on the builder (`trx64-sim-env:4`), none shows the board's state:
  - New modes `sw48` (128K first, then a reset into 48K with the ROM rewritten, as `zx.cc` does it) and `sw128` (the other way): `PASS` both, 3,110 / 3,120 line
    starts and 10 frames written in the 10 frames before, 3,740 / 3,730 line starts and 12 frames written in the 12 after, VSync 12 of 12. The same with the real 48K ROM
    (16,384 bytes read from a file outside the tree, never in the repository): 18,716 line starts and 60 frames written in 60 frames after the switch.
  - The ULA alone (`ula.sv` + `zxcpc_nco.v`, a scratch bench, not in the tree) with a random hc, vc, INT, INTCnt, HBlank, VBlank, VSync, HSync, FlashCnt and NCO
    accumulator, the model switched at a random clock: 30 of 30 runs have lines and VSync after two frames (a run whose VSync starts high needs until vc 244 to
    clear it: no wedge).
  - `mut_sw` (`ZX_MUT_SW`: `HBlank & m128_l` into `line_start`, `vis`, the writer and the JTAG flags - the 48K's blanking never ends) is the board's picture:
    0 line starts, 0 frames written, 12 VSyncs of 12 after the switch. `FAIL sw48` = `PASS control mut_sw`. The bench can fail in the way the board did.
  Not decided: the cause. What the code and the benches rule out: a 128K-only or model-dependent term in the line-start path, the NCO burst, the writer, a stale
  state after the switch (random state), the 48K ROM's own behaviour. What is left is not in the RTL as simulated (an implementation or timing difference of the
  built bitstream, or a state of the board that the bench does not have). **Next board read** (words 10 and 11 are new, the build that carries them is needed):

  | word | value | meaning |
  |---|---|---|
  | `trx 10` | `{d_hbr, d_hbh}` | HBlank rises (at `ce_7mp`), dots with HBlank high |
  | `trx 11` | `{d_ce, d_ls}` | pixel enables, line starts |

  In 48K, twice one second apart: `d_hbr` moves with `d_ls` (one apart) = both edges are there; `d_hbr` moving and `d_ls` not = the falling edge is lost after the ULA;
  `d_hbr` constant = the ULA's HBlank does not rise; `d_ce` constant = no pixel enable at all (then the CPU would not run). `d_hbh` over `d_ce` is the share of
  blanked dots (about 0.23 for HBlank 312..416 of 448).
- **5.11 2026-11-01, t164-zx48: 48K black again, simulation of what the board read says (still no cause).** The board read of 2026-10-26 (T119 history, build b9debaba)
  says: in 48K `w10 = 0` (no HBlank rise and no dot with HBlank high ever), `w5 = 1` (one line start, the first after reset), `w2` frames run, the CPU runs the ROM (10-24: PC 15E0).
  Read from the code: `hc`/`vc` move (VSync at `vc_next == 240`, INT at `vc_next == 248, hc_next == 4`, both reached), but `HBlank <= 1` at `hc_next == 312` is never seen. What was measured:
  - `cores/zx/rtl/ula.sv` alone with `zxcpc_nco` (2/9 and 5909/26208), after Yosys + slang synthesis to gates (`synth -flatten`), simulated with Iverilog for 3 frames:
    `m128=0 hblank rises=832 vsync rises=1`, `m128=1 hblank rises=829 vsync rises=1`; Verilator on the RTL: 833 / 830 rises, 2 VSync. The synthesised ULA has the HBlank edges in 48K.
  - Whole core, `sw48` (128K run, reset into 48K with the ROM rewritten) on a Verilator build with `--x-initial unique --x-assign unique` and `+verilator+rand+reset+2`, and a second one with
    every register initialiser of `ula.sv`, `zx_core.v` and `zxcpc_nco.v` removed (what a partial bitstream without an initial-state restore would leave): seeds 1 and 2 of the second build print
    `3110 line starts, 10 frames written, 10 VSyncs; after it (model 48K): 3740 line starts, 12 frames written, 12 VSyncs in 12 frames` (its `FAIL sw48` comes from the bench's own
    check of JTAG word 10 against word 11, which assumes both counters start at 0; the counts themselves are the normal ones).
  - Random 16 KB programs as the 48K ROM (random Z80 code with arbitrary I/O and memory writes, seeds 0-3 finished before the run was stopped): `PASS sw48` each, 3,740 line starts in 12 frames.
    The CPU cannot reach the ULA's counters, as the code says.
  - `cores/zx/mono/host/top.v` is identical to `host/top.v` (diff empty); no `ce` input of the overlay / text instances is left open for this core (the C128 finding does not apply).
  Consequence: the fault is not in the RTL as simulated, not in the synthesised ULA, not in the power-up state of its registers. The remaining candidates are in what only the board has
  (the dynamic-function-exchange partial bitstream of the 2026-10-24 / 10-26 builds, its start-up state and clocking). The large-core build of this core (T164, flat, GSR as every flat build) has not run;
  its first board read with JTAG words 10 / 11 (table in 5.10) tells whether the fault went away with the DFX partition or stays. No fix is made here: a change without a failing bench would be a guess.
- **5.12 2026-11-01, t164-zx48b: the app's switch sequence through the registers and the window, still no cause; board read list.** New evidence (Alex, flat large-core
  `zx.cor`, bundle 774497b1, not DFX): cold start in 48K = HDMI present, black picture, the core app's loading bar without progress; 128K works; from a running 128K,
  Model 48K + reset leaves the 128 BASIC picture frozen. 5.11 replayed the switch with `$readmemh` into the ROM; the app does not do that. Built: modes `swapp48`
  (running 128K, then CTRL b0 + reset line, `ZX_MODEL` 0, 16,384 ROM bytes through WIN_PAGE + the ROM window, read-back of all 16,384, WIN_PAGE 0, lines down) and `coldapp48`
  (the same from power-up), control `mut_swapp` (`ZX_MUT_SW`). Builder, `trx64-sim-env:4`:
  `PASS swapp48` and `PASS coldapp48` (48K, read-back differences 0, >= 3,500 line starts / >= 10 frames written / >= 11 VSyncs in 12 frames);
  `ctl| swapp48: ROM read-back differences 0; model 48K; after the switch: 0 line starts, 0 frames written, 12 VSyncs in 12 frames` = `PASS control mut_swapp`.
  So the register and window path of `zx.cc` (zx_init, select_model, put_model_rom, zx_release) is clean in the RTL; with the 128K-only reading of the code (5.10: the line
  start path has no model term, `hc` cannot skip 312) the picture fault is not a ULA or window logic fault that the simulation can show. Read from the code: three symptoms
  of the flat build (black cold, frozen after the switch, loading bar standing) are all what a machine that is never released from the app's hold looks like
  (`rst_core` = `ctrl_s2[0]`: no ULA frame is written, the layer keeps the last buffer) - 128K works, so it would be a path only the 48K start takes in the app
  (boot loop, ROM scan, `regset_apply`), not the ULA. That is a reading, not a measurement. **Board read list for the flat build, 48K black, twice 1 s apart,
  before anything is changed** (one JTAG session; `trxdbg` words):
  | word | tells |
  |---|---|
  | `trx 1` (status `{0, m128, snap_busy, hold_q, rst}`) | rst = 1 (or hold_q): the core is held, the fault is the app / the boot, not the ULA; 0 with m128 = 0: running 48K |
  | `trx 11` `{d_ce, d_ls}` | `d_ce` moves, `d_ls` constant: pixel enables there, lines lost; `d_ce` constant: no clock enable (held or NCO) |
  | `trx 10` `{d_hbr, d_hbh}` | `d_hbr` constant while `d_ce` moves: HBlank does not rise in the built netlist (synthesis difference; only then run Vivado reports on the ULA) |
  | `trx 2`, `trx 5`, `trx 6`, `trx 8` | frames, line starts, frame buffer sequence, lines handed to the DDR2 writer |
  | core app log (serial / UART) | whether `[core] 48K ROM: ... (16384 bytes)` is printed and whether `boot_start` leaves its ROM loop |
- **5.13 2026-11-01, t164-zx48c: the core app's start and switch, 48K against 128K, recorded - clean, the hold is released in every case (no cause).** Test `tests/coreapp/test_boot_zx.cc`
  (`run boot_zx`, real `zx.cc` + `boot.cc` + `regset.cc` + `romfind.cc`, ROMs from `/SD/cores/zx/roms/` with `cor_path` `/SD/cores/zx.cor`, the genuine 48.rom 16,384 B / 128.rom 32,768 B
  when `ZX_ROM_DIR` names them, else a same-size pattern) against a register model; it records CTRL / MODEL / WIN_PAGE writes (`0a0`, `0a1`, `0d4`), the host reset line and the ROM arrays.
  Sequences (the genuine ROMs), CTRL b0 hold `01` / release `00`:
  - cold 48K: `0a0=01 0a1=00 0d4=00 0d4=00 0a0=01 0a0=00`, 1 reset assert / 1 release, final CTRL 00, MODEL 0, ROM array 0 = 48.rom
  - cold 128K: `0a0=01 0a1=01 0d4=00 0d4=02 0d4=00 0a0=01 0a0=00`, final CTRL 00, MODEL 1, ROM arrays 0 / 2 = 128.rom
  - no cfg (default = 48K): as cold 48K; 48.rom missing, 48K asked: the 128K runs, released
  - switch 128K -> 48K (Model setting, `machine.cold_reset`): `0a0=01 0a1=00 0d4=00 0d4=00 0a0=00`; 48K -> 128K: `0a0=01 0a1=01 0d4=00 0d4=02 0d4=00 0a0=00`; both: CTRL b0 up before the first ROM
    byte, down last, the reset line released once, no ROM write after the release
  The 48K path differs from the 128K one only in MODEL = 0 and one bank instead of two (`0d4=00` = the same bank 0 write as the 128K's first). `boot_start` leaves its ROM loop at the
  first pass (no `ROM files` message), the ROM read-back is equal, `zx_release` clears CTRL b0 and the reset line. So the hypothesis of 5.12 (the app never releases the 48K machine) is
  **refuted in the application's code path**; the loading bar that stood is the host start page's (`hostui.cc` `start_screen_update`, fixed on t164-progress; coordinator 2026-11-01), not a symptom of the core app, and is no evidence here. Controls (the test must fail): `boot_zx_model` (MODEL forced to 1),
  `boot_zx_rom48` (the 128K image goes in for 48K), `boot_zx_release` (the reset line never released), `boot_zx_hold` (no release at all). What the harness cannot show: the board's
  `CoreIo` (JTAG/aperture timing, a read-back that is stale) and the core app's UI task; the board read list of 5.12 stands unchanged, `trx 1` first - with the app path clean, rst = 1
  would now mean that the app was killed or blocked before `zx_release` (UART log: `[core] 48K ROM:` line, then no `[core] ...` after it), rst = 0 means the machine runs and the fault is in the picture path (words 10 / 11).
- **5.14 2026-11-02, t164-zx48d: board read of the flat large-core build after Model 48K (package t164-c83a83c2, CI run 1139, `TRX_FEATURES=jtag`) - HBlank does not rise; no cause in the RTL or in the Vivado reports.**
  Raw read, twice (`tools/trxdbg.sh trx <4n>`), first -> second:
  - w1 status `0x00` / `0x00` (rst 0, hold 0, m128 0: running 48K); w2 frames `0x0F66` -> `0x1018`; w3 cpu_a `0xFF49` -> `0xFF4A`; w4 Z80 PC `0x15EC` -> `0x10AC`
  - w5 `{fb_over, d_ls}` `0x0000285F` both; w6 `{d_vis, fb_latest, fb_seq}` `0x1C9F83F4` -> `0x6BFD83F4`; w8 `0xD3A00AE0` const; w9 `0x01EA` const
  - w10 `{d_hbr, d_hbh}` `0x285EA920` const; w11 `{d_ce, d_ls}` `0x20A0285F` -> `0x3B67285F`
  Reading, from the code and the numbers: (1) `d_hbh` 0xA920 = 43,296 = 10,334 x 112 - 17 x 65,536 with `d_hbr` = 10,334 = 0x285E: the frozen counters are exactly whole 128K
  lines (HBlank 312..424 = 112 dots), `d_ls` = `d_hbr` + 1, so the freeze is clean at the end of a 128K line with HBlank low, and since the switch not one dot with HBlank high
  reached `ce_7mp`. (2) `d_ce` and `d_vis` move: the pixel enables and the non-blanked dots run; PC, frames (VSync, hence `vc`, hence `hc` wrapping at 447) and INT run. (3) the same as the
  cold 48K read of 2026-10-26 (w10 = 0): not a switch effect, the 48K branch of `ula.sv` (HBlank at `hc_next` 312 / 416) is dead on the board while the 128K branch (312 / 424) lives.
  In the RTL the 48K branch differs from the 128K one by the clear constant only; `sw48`, `swapp48`, `coldapp48`, the Yosys gate netlist and 30 random-state runs all show the rises (5.10-5.12).
  Vivado side, run 1136 `large-zx` (`zx_mono.vivado.log`, same `cores/zx` sources as 1139: `git diff 4e38d8ab d8a589bae -- cores/zx` is empty; the reports of 1139 are not in the cache): ULA = 102 LUT / 94 FF;
  the only ULA registers removed or constant-propagated are `tmx_cfg[5:0]`, `tmx_using_ff`, `tmx_ena`, `vaddr[13]` (constants of `ulap_avail = tmx_avail = 0`); nothing on `hc`, `vc`, `HBlank`, `m128_l`, `hb_p`,
  `d_*`; no latch, no multi-driven net, no 8-3332 / 8-3333 / 8-6014 on them; Synth 8-6901 (identifier used before its declaration) on `vaddr`, `INT`, `INTCnt`, `tmx_*`, `SRegister`, `bits`, `attr`, `ff_data`
  (`ula.sv` 83-218) is the only ULA warning and none of those feeds HBlank; timing met (WNS +0.128 ns, WHS +0.052 ns, no unconstrained endpoint, no_clock 0). The built ULA could not be
  opened: `cache/multicore/bit/mono-zx-*` is root-only (0700) and no checkpoint is in `runs/*/large-zx`.
  A post-synthesis functional simulation is possible without a full run: `synth_design -top ULA` (plus `zxcpc_nco`) alone in `vivado-env`, `write_verilog -mode funcsim`, Verilator/iverilog with the unisim
  models, about 5 minutes and 1-2 GB, no place or route. **Not run; needs the coordinator's go.** If the netlist shows the rises, the fault is after synthesis (placement, routing, the board).
  Added (diagnostic, no behaviour change): JTAG words 12 `{dots between the last two HSync rises, HSync rises}` and 13 `{dots HSync high in the last line, HSync rises per frame}`, counted at `ce_pix` from the ULA's
  outputs (`zx_core.v`). Expected in 48K: 0x01C0 (448), 32, 312; in 128K: 456, 32, 311. They decide the next step with the build that carries them: line 448 and 312 lines per frame with HBlank dead = the
  HBlank flop or its compare in the netlist (take the funcsim run); line 512 / other = the `hc` wrap compare or `m128` at the ULA is not what `m128_l` says (hc counter, not HBlank).
  Benches (builder, `tools/benches.sh zx_switch -f`, commit with words 12/13): `zx_switch PASS` 1359 s - `sw48`, `sw128`, `swapp48`, `coldapp48` PASS with the new check (48K: line 448 dots, HSync 32 dots, 312 lines per frame;
  128K: 456, 32, 311), `mut_sw` and `mut_swapp` FAIL as they must. The new check is a regression guard for the words, not a repro of the board fault.
- **5.15 2026-11-02, t164-zx48e: CAUSE FOUND - Vivado 2024.1 synthesises the ULA without its 48K HBlank and HSync statements; fixed in `ula.sv`, proved in the post-synthesis netlist (funcsim).**
  Method (coordinator's go; builder, `vivado-env:2024.1`, `/cache/heavy.lock` taken, niced, `synth_design` only): `synth_design -top zx_core -mode out_of_context` of the core as `build_mono.tcl`
  reads it (`rtl/ula.sv`, `ym2149.sv`, `gen/t80pa.v`, `shared/*.v`, `trx/*.v`; zx_core uses no define of the build), `write_verilog -mode funcsim`; 6 min with the lock free, 42 RAMB36E1, 3,348 FDRE.
  The netlist is driven through the ports of `zx_core` only (`cores/zx/sim/fsim/tb_fsim.v`: the app's register and ROM-window sequence of `tb_zx` coldapp48 / swapp48, then JTAG words 1-13), Verilator, the 7-series
  cells of `fsim/cells.v` (Xilinx's own FDRE/LUT models use `tri1` inputs Verilator refuses; the functions are UG953's) and the vendor's RAMB36E1 (two ECC `assign` lines removed in the copy).
  - **Control, same bench on the RTL** (`cold48`, 1.5 M `sys_clk`): `w10 d_hbr 748 d_hbh 12256`, `w11 d_ce 7583 line_starts 749`, `w12 HSync period 448 rises 748`, `w13 HSync high 32 lines/frame 312` - `PASS`.
  - **Netlist, unfixed `ula.sv`, 48K cold:** `cold48: w1 status 00 frames 0002 | w5 d_ls 0001 | w10 d_hbr 0 d_hbh 0 | w11 d_ce 7583 line_starts 1 | w12 HSync-period 0 rises 0 | w13 HSync-high 0 lines/frame 0` -
    `FAIL cold48: HBlank dead in the netlist`: the board's picture (frames run, `d_ce` moves, HBlank and HSync never rise). 128K cold, same netlist: `d_hbr 746 ... w12 456 rises 746 ... lines/frame 311` - `PASS` (that is why the 128K works).
  - **Netlist, unfixed, 128K -> 48K (`sw48`):** before `w10 d_hbr 744 d_hbh 17792 | w11 line_starts 745 | w12 456`; after the switch (`w1 status 00`, 48K, frames 5) `w10 d_hbr 744 d_hbh 17792` unchanged, `w11 d_ce 21770 line_starts 745`, `w13 lines/frame 0`:
    frozen at a whole 128K line count, `d_ls = d_hbr + 1`, `d_ce` running - the board read of 5.14 (`0x285EA920`, `d_ls = d_hbr + 1`) reproduced; `FAIL sw48`.
  - **What the netlist does** (`HBlank_reg`: FDRE, CE = 1, D = `LUT6 HBlank_i_1(HBlank_i_2, HBlank_i_3, hc[4], hc[7], HBlank_i_4, HBlank)`, inputs of the whole cone: `hc[8:0]`, the pixel enable `ce_7mn`, `m128_l`, `HBlank`): the LUT truth tables
    evaluated for every `hc` give, with `m128 = 1`, set at `hc` 311 and clear at 423 (= `hc_next` 312 / 424 as written) and with `m128 = 0` **no set and no clear at all** (the RTL has 312 / 416). `HSync` likewise.
    Everything else the 48K needs from `m128` is alive (`hc` wrap at 447, `INTCnt`, `vc`), so frames, INT and the CPU ran on the board.
  - **Cause in the source:** `cores/zx/rtl/ula.sv` lines 153-163 of the unfixed file: `... end else if(m128) begin {HBlank 312/424, HSync 340/372} end else begin {HBlank 312/416, HSync 336/368} end`. Vivado 2024.1 `synth_design` loses the final `else`
    statements. Not an RTL fault, not the NCO, not `m128_l`, not placement (the loss is in the synthesised netlist). It does not depend on `mZX` being a constant (the ULA with a kept hierarchy, `-flatten_hierarchy none`: same), on resource sharing
    (`-resource_sharing off`) or on `-no_lc`, and not on the `Synth 8-6901` declaration-order warnings (all declarations hoisted above first use: still dead). Reproduced with the ULA alone (`fsim/ulaw.v`, a few seconds) and bisected there:
    `if(!mZX) A else if(m128) B else C` dead 48K (C); `if(m128) B else C` (no A) dead; `else if(m128) B else if(!m128) C` dead; HBlank alone (HSync statements removed) dead, HSync alone dead; **`if(!mZX) A else if(!m128) C else B` alive**
    (order swapped); **one statement pair with `m128` in the compare constants (`hc_next == (m128 ? 424 : 416)` ...) alive**. The cause in Vivado is not localised further (no message names it); the structure that triggers it is the second sibling that
    assigns what the first one assigns, on the complementary condition, behind the first `if`.
  - **Fix** (`cores/zx/rtl/ula.sv`, the `else` branch of the `if(!mZX)` chain, one pair of statements per signal with `m128 ? 128K : 48K` constants; the same timings): kept as `tools/zx_ula.patch`, applied by `tools/vendor_zx_mister.sh` after the copy (a re-vendor
    does not lose it), `rtl/SHA256SUMS` lists the patched file, `rtl/NOTICE` says "unchanged except ula.sv", the file marks the change at the place (GPL).
  - **Proof of the fix**, the whole `zx_core` synthesised again from the fixed file (6 min), same bench: `cold48` `w10 d_hbr 748 d_hbh 12256 | w11 d_ce 7583 line_starts 749 | w12 448 rises 748 | w13 32 lines/frame 312` - identical to the RTL control, `PASS`;
    `cold128` `d_hbr 746 ... 456 ... 311` `PASS`; `sw48` after the switch `d_hbr 1498 | line_starts 1499 | w12 448 rises 1498 | w13 32, 312` `PASS`. The unfixed netlist is the control that fails (above). Regression check without the whole core:
    `cores/zx/sim/fsim/run.sh [ula.sv]` (builder; ULA + `ulaw.v` synthesised in about a minute, 20 lines of each model, HBlank and HSync must rise on every line): fixed file `PASS 48K`, `PASS 128K`; the unpatched file prints `FAIL 48K` (the control).
  - Open for the board (not Alex's step yet, the build comes with the next package): 48K cold and 128K -> 48K on the flat large core with this ULA; JTAG words 10-13 expected as in the table above (48K: 448 / 32 / 312).
    The other `.sv` of the MiSTer tree that the cores read could carry the same pattern (an `else` after `else if` on complementary conditions that assigns what the sibling assigns); the ZX Next and CPC netlists were not looked at here.
