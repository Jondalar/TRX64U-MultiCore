# T168 — DDT's two C64U demo designs run as partial cores of the host, their picture on the host's HDMI

**Status:** DONE (2026-11-14, Alex: "Yes it does. Both run.", translated). Closed: Alex has seen both on the board, incl. the 6-engine ddt_mandel. Earlier status: BUILDING (2026-10-31). Status check 2026-11-13: BUILT and merged (cores/ddt_mandel, cores/ddt_novic in main); board 10-31 both run (Alex: "both look good"); the 6-engine ddt_mandel of D10 (4768d714c) is not recorded on the board.
**Repo:** TRX64U-MultiCore — two cores (`cores/ddt_mandel`, `cores/ddt_novic`), two core-app descriptors (`coreapp/cores/ddt_mandel.cc`, `ddt_novic.cc`, registers in `ddt_regs.h`), CI wiring.
**Number:** T168 (table in `TODO.md`).
**Origin:** Alex, 2026-10-31 (translated), after "we once had DDT's demo that could only output analogue and had no HDMI" (T22): "OK, can you have
an agent rebuild them as partial cores, as a showcase? That would still be nice to show the community, that their gimmicks become displayable
with it too and that a user does not have to flash." Not T22: T22 (won't do, ADR-0035/0036) was giving our GPL HDMI code to DDT's CC-BY
repositories. Here his designs come into ours as cores.
**Depends on:** the layer-2 contract v2 (`contract/layer2_ports.txt`), the pixel layer of the host (T119, T107), the generic core application (T104).
**Milestone:** M3.

---

## §1 The problem

- **Who:** Alex and the community around the C64 Ultimate. DDT (github.com/0x444454) published two FPGA designs for the C64U that replace the
  whole machine: `mandelbrUlt` (a Mandelbrot generator) and `look_ma_no_vic` (a demo). Both write straight to the board's analogue video DAC
  (THS8136, pins `AVID_*`) and have no HDMI; to run one, the user has to load a full bitstream over JTAG, and power-cycle to get the machine back.
- **What we want:** the user picks the demo in the boot menu and sees it on HDMI, without flashing anything.
- **What the code says (read 2026-10-31 at mandelbrUlt `93aee1cc`, look_ma_no_vic `1002b7f0`):**
  - `mandelbrUlt/sources/src/top.v`: input `RMII_REFCLK` (50 MHz) into an MMCM (`clock_gen_rmii_to_13m5.v`): 100 MHz `clk_calc` for the engines and 13.5 MHz
    `clk_pix` for the video. `MANDEL_CORES = 20` engines (`pixel_gen_mandelbrot.sv`: three 25 x 25 signed multiplies as 6 DSP48E1, an escape test on
    a CARRY4 adder, Q3.22) all 120 DSP of the XC7A50T. `fb_scanline_writer.sv` schedules pixels in scanline order onto free engines, retires them out of
    order into a two-line buffer (`linebuf_iter8.sv`), `fb_bram_dma.sv` commits lines into a 352 x 256 x 8 bit BRAM frame buffer
    (`framebuffer_bram.sv`, 22 RAMB36). Video: `video_timing.v` is a 864 x 312 PAL-like raster at 13.5 MHz, `mandel_video_pattern.v` reads the frame
    buffer, looks the 8 bit iteration count up in `palette_yuv_256.hex` ({Y, U, V}, 28 bit) and synthesises a PAL subcarrier (luma on `AVID_G`, chroma on
    `AVID_B`, composite on `AVID_R`). Inputs: `KBJOY_2_JOY[4:0]` (port 2, active low) and `KBJOY_RESTORE` through `joystick_buttons.sv` (debounce, pan /
    zoom / iterations / reset pulses). It also reads the splash text from `screen_matrix.v` + the C64 character ROM for 501 frames. Memory: the BRAM frame
    buffer and nothing else; no DDR2. Outputs besides video: `LED_BOARDn` (busy), `CIA_PWR_EN`, `CIA_BUFFER_EN` (joystick port rail), case LEDs.
  - `look_ma_no_vic/sources/src/top.v`: 13.5 MHz from the same MMCM; `video_timing.v` (the same 864 x 312 raster), `video_pattern.v`: a 704 x 200 picture
    made of an animated zoomed XOR luma pattern, a hue rotation along x and per frame (`hue_anim`) as PAL chroma, a 44 x 25 text screen
    (`screen_matrix.v`) and a 2048 character scroller with a zoom envelope every 512 frames (`scroller.v`), both drawn with the 2 KB character set
    `c64_chargen_ucg.hex`. No inputs, no memory except small ROMs. Two combinational dividers (`/ 255`, `2^24 / zoom`) in the frame-start update.
  - Neither design uses the HDMI pins, the slot, the SID or the DDR2.

**Parts (MECE):**
- **P1 Rights.** What licence the code has and whether it may be a part of our GPL-3.0 tree.
- **P2 Picture.** Their video leaves through the contract's frame-buffer path, not the DAC pins; the picture is the picture their logic computes.
- **P3 Clocks, input, resources.** The partition has fixed clocks and ports; the designs must live inside `pb_l2` (DSP, BRAM, LUT) and read their joystick from the contract.
- **P4 Start.** The user starts them from the boot menu.
- **P5 Fidelity and marking.** The upstream logic stays as it is; what we changed is marked.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario S1 (P1): the licence is read and recorded
  Given both repositories at the commits above
  When  their README, source headers, repository metadata and files are read
  Then  the spec §3 D1 and cores/<id>/NOTICE name the licence (CC BY 4.0), where it is written, which files are ported unchanged / changed / left out
  And   a design whose licence cannot be found or does not allow it is not ported

Scenario S2 (P2): the Mandelbrot picture reaches the frame buffer
  Given the core with its engines, run in a Verilator bench with the host's MIG port model, the app's FB registers written over the cw_* bus
  When  the renderer finishes (default view, 128 iterations) and the scan writer has written frames into the ring
  Then  the newest complete buffer in DDR2 equals, pixel for pixel (352 x 256 RGB565), the picture an independent Python model of
        pixel_gen_mandelbrot.sv (Q3.22, the same truncations) and of the palette gives
  And   the control with a palette entry changed (-DDDT_MUT_PALETTE) and the control with the column offset changed in the writer (-DDDT_MUT_PIXW) FAIL the bench
  Bench: ddt_mandel_pix

Scenario S3 (P3): the joystick drives the renderer
  Given the same bench; c64_joy2 held at fire + up (c64_joy: bit 0 up, 4 fire, 1 = pressed)
  When  the debounce time has passed and the renderer has finished
  Then  the picture equals the Python model with the scale halved (zoom in) and the centre unchanged
  And   with the up bit mapped to down (-DDDT_MUT_JOY) the picture is the zoomed-out one and the bench FAILs
  Bench: ddt_mandel_joy

Scenario S4 (P2): the look_ma_no_vic picture reaches the frame buffer
  Given the core in a bench like S2 (352 x 200 RGB565 frames)
  When  frames are written (frame 0, a later frame inside the zoom envelope: the bench time-warps the frame counter by a core parameter)
  Then  the buffer equals an independent Python model of video_pattern.v + scroller.v (background XOR, hue, 44 x 25 text screen, scroller, the zoom
        update at frame start) with the substituted 8 x 8 font, pixel for pixel
  And   the controls (-DDDT_MUT_HUE: hue phase sign, -DDDT_MUT_PIXW) FAIL
  Bench: ddt_novic_pix

Scenario S5 (P3): the partition's edge
  Given cores/ddt_mandel/layer2_ddt_mandel.v and cores/ddt_novic/layer2_ddt_novic.v
  Then  tools/contract_ports.py --check passes (247 ports, same order and widths), and the wrapper drives nothing on the slot / IEC (check_layer2_pins.py)
  And   a wrapper with one output driven (-DDDT_MUT_PINS) FAILs the pin bench
  Bench: ddt_mandel_pins, ddt_novic_pins

Scenario S6 (P3): resources
  Given the synthesis of the layer 2 for pb_l2 (60 DSP, 50 BRAM tiles, about 16,800 LUT)
  Then  the estimate in §5 is below the pblock in every resource; the Vivado core stage reports the real numbers (CI, run by the coordinator, not here)
  Test: CI `core` stage, line `=== T168 ... synth`

Scenario S7 (P4): the menu shows the cores
  Given tests/coreapp (the application against a model of the core)
  Then  both descriptors are in the core table, pass desc_check, the menu list matches the golden file, the video record is 352 x 256 / 352 x 200 at scale 3
  And   a descriptor with the wrong ID or video width FAILs the same test
  Test: tests/coreapp/run.sh (test_desc, test_memops, test_ddt)

Scenario S8 (P4): on the board
  Given a .cor of each core on the stick
  When  Alex starts it from the boot menu
  Then  the Mandelbrot appears on HDMI and pans / zooms with joystick port 2; the demo runs its text and scroller; "Return to host menu" brings the host back
  Test: board step by hand (Alex); needs the CI build of the .cor

Scenario S9 (P5): marking
  Given the ported tree
  Then  every upstream file is either byte-identical to its upstream copy (SHA256SUMS in the core directory) or has a .diff next to it and is listed as changed in NOTICE
  Test: cores/<id>/rtl/SHA256SUMS checked by the lint stage (tools/check_ddt_tree.py)
```

| # | Solves | Test | Where it runs |
|---|---|---|---|
| S1 | P1 | review of §3 D1, NOTICE | this spec |
| S2 | P2 | `ddt_mandel_pix` | `ci/sims.txt` tier 2 |
| S3 | P3 | `ddt_mandel_joy` | `ci/sims.txt` tier 2 |
| S4 | P2 | `ddt_novic_pix` | `ci/sims.txt` tier 2 |
| S5 | P3 | `ddt_mandel_pins`, `ddt_novic_pins`, lint | `ci/sims.txt` tier 1 |
| S6 | P3 | Vivado core stage | CI `bitstream` |
| S7 | P4 | `tests/coreapp/run.sh` | CI lint / app stage |
| S8 | P4 | board | by hand |
| S9 | P5 | `tools/check_ddt_tree.py` | lint |

## §3 Decision

**D1 Licence (read 2026-10-31).** Both repositories state CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/deed.en, "Please add a link to this github project")
in their `README.md` and `sources/README.md`; mandelbrUlt's `sources/src/top.v` repeats it in its header. Neither has a `LICENSE` file (the GitHub licence field
is empty), and the other source files carry no header. CC BY 4.0 allows copying, adapting and commercial use; it asks for attribution, a licence link and a
note that the work was changed. The direction we need (CC BY material inside a GPL-3.0 tree) has no copyleft obstacle: BY has no share-alike, no field
restriction and the GPL adds no further restriction on his files. What we do: his files stay under CC BY 4.0 in their own directories (`rtl/`) with a
`NOTICE` that names author, repository, commit, licence, link and every change; our wrapper files are GPL-3.0 as the rest of the tree; the combined bitstream is
distributed with the source of both. CC and the FSF advise against CC licences for software; that is a recommendation, not a prohibition, and the
choice of the licence is DDT's. Not legal advice. **One file is not ported:** `c64_chargen_ucg.hex` is the C64's character ROM (Commodore 901225-01 uppercase / graphics set,
read from the file's own name and size); ROMs never go into the tree, a release or a `.cor` (CLAUDE.md, PROVENANCE section 5). It is replaced by Gideon's 8 x 8 overlay
font that the host already carries (`host/mem/ovl_font8.mem`, GPL-3, PROVENANCE section 3), D6. The `.bit` files, the `.mov`, the images and Gideon's `C64U.xdc` of the
repositories are not used.

**D2 Cores.** `ddt_mandel` (id `DDTM`, "DDT Mandelbrot (mandelbrUlt)") and `ddt_novic` (id `DDTN`, "DDT Look Ma, No VIC"), both `experimental = true`,
`app = "blueprint"`. Their `sources` are the core's directory; `ddt_novic` also reads the shared files of `ddt_mandel/trx` (the register block and the frame writer),
like the CPC reads the Spectrum's `shared/` (a file in two cores' sources and one owner passes `ci/layers_check.sh`).

**D3 Picture path.** The designs' analogue back ends (`video_timing.v`'s sync and burst, the PAL subcarrier, the `AVID_*` registers, `THS8136` blanking) are dropped. In their
place a raster generator in `cpu_clk` (100 MHz) emits one pixel every 16 clocks on a 400 x 312 grid (50.08 Hz, the 312 lines of their raster) with the picture in the top left
352 x 256 (Mandelbrot) or 352 x 200 (look_ma_no_vic); each pixel is RGB565. `ddt_pixw.v` (from `zx_pixw.v`, our code) packs 8 pixels into a 128 bit word, writes
rows into one of four DDR2 buffers through the host's general port `e_*` (T119) and reports the newest complete buffer in `FB_LATEST` / `FRAME_SEQ`. The pixel layer
shows it; `pix_*` carries the dummy raster the scaler locks to (as the null core). The application describes the buffers (`VideoDesc`, scale 3).

**D4 Mandelbrot.** `pixel_gen_mandelbrot.sv`, `fb_scanline_writer.sv`, `fb_bram_dma.sv`, `linebuf_iter8.sv`, `framebuffer_bram.sv`, `joystick_buttons.sv`, `screen_matrix.v`
byte for byte. The control part of `top.v` (centre, scale, iteration registers, splash and palette-cycle counters) is kept as written inside our wrapper
`ddt_mandel_core.v`. **Engines: 8, not 20.** `pb_l2` owns 60 DSP48 (`host/build.tcl`, floor B: `DSP48_X0Y0:Y39` + `X1Y0:Y19`); an engine is 6; 20 engines are 120. 8 engines
(48 DSP) leave a margin for placement; the parameter `MANDEL_CORES` is DDT's own. 0.8 GigaIters/s instead of 2. The palette `palette_yuv_256.hex` holds YUV words for the PAL
modulator; the RGB source colours are in the file's own line comments, and a script recomputes the YUV words from them within the quantisation (maxerr 0.55,
S9 tool), so `tools/ddt_palette.py` writes `palette_rgb565_256.hex` from them.

**D5 look_ma_no_vic.** `scroller.v`, `screen_matrix.v` byte for byte. `video_pattern.v` is changed (`rtl/video_pattern.v.diff`): the sample coordinate `x` counts 0, 2, 4 ... 702 (every
second sample of the 704 is dropped: all text and the scroller work in pixel pairs, the XOR background differs in the lowest bit only), and the output is an RGB colour computed from the
same luma and the same hue vector instead of luma + modulated chroma (YUV to RGB with the PAL matrix at the saturation 44 / 128 the code uses; the carrier, the burst, the sync, the
blanking go). `top.v` and `video_timing.v` are replaced by our wrapper. The two dividers stay as written. The hue ramp's `floor(x * 256 / 720)` is the multiply constant `x * 91` (`>> 8`), not shifts, and is within 1 count of the exact value (§5).

**D6 Font.** `c64_font_rom` keeps its name and ports (so `scroller.v` and `video_pattern.v` need no change) but is our module (`trx/c64_font_rom.v`) reading Gideon's ASCII-indexed
8 x 8 font; the C64 screen code is mapped to ASCII (`code < 32` adds 64). The glyphs look like the overlay font, not like the C64's ROM.

**D7 Registers and application.** The core's window (`cw_*`) answers the TRX block as the null core does (ID, CAPS, CONTRACT, BUILD, IRQ, page 6 to the pixel layer) plus CTRL (reset hold, halt),
STATUS and the frame-buffer registers of `zx_regs.v` (`0C0` ... `0CB`). Freeze (`CTRL b1`, `c64_halt_req`) masks the joystick in `ddt_mandel` (the engines are not stopped: the render in progress finishes and the picture stays) and stops the frame counter of `ddt_novic`. The descriptors (`coreapp/cores/ddt_mandel.cc`, `ddt_novic.cc`): no ROM, no settings, one task "Reset", the core's icon.

**D8 Clocks.** `cpu_clk` (100 MHz MMCM output of the host) for the engines (as in DDT's design), the raster and the writer; `ui_clk` for the DDR2 side. The 100 MHz engine path
(25 x 25 multiply, CARRY4 add, compare in one cycle) closed in DDT's own run "with some timing warnings"; whether it closes in `pb_l2` is the first thing the CI core stage tells.

**D9 Partial core, 8 engines (Alex, 2026-10-31).** Asked whether `ddt_mandel` should become a large core so that all 20 engines fit (a large core leaves about 111 DSP = 18 engines,
1.8 GigaIters/s; the partial core 60 DSP = 8 engines, 0.8): "ok, then we leave it as it is" (translated). `ddt_mandel` stays a partial core with `MANDEL_CORES = 8`.

**D10 Six engines after the T164 floorplan cut (Alex, 2026-10-31).** T164 Q1 cut A moves DSP48_X1Y0:Y19 to the host, so `pb_l2` keeps 40 DSP and 8
engines (48 DSP) no longer fit. Asked: fewer engines as a partial core, or a large core with 8: "1" (fewer engines). `MANDEL_CORES = 6` (36 DSP,
about 0.6 GigaIters/s); built on the T164 branch with the new floorplan. (D4 / D9 above say 8 engines on 60 DSP: replaced by this.)
Bench with 6 engines (T164 WP19, builder): `PASS ddt_mandel: pix joy ctl_palette ctl_pixw ctl_joy ctl_ilv` (the four controls fail as they must).

## §4 Out (decided with Alex before the work; anything else is listed here by me for his call)

- DDT's `.bit` files, the `.mov`, the screenshots, `C64U.xdc` and the Vivado project files: not ported, not needed.
- The C64 character ROM (`c64_chargen_ucg.hex`): not ported (D1/D6).
- The analogue composite / S-Video output of these designs: dropped, the host has its own analogue out (T140).
- LEDs (`LED_BOARDn` busy indication), the case LEDs and the CIA joystick rail enables: the host owns them; the render-busy bit is in the status register instead.
- Audio: neither design has any.
- 20 engines: needs 120 DSP, the partition has 60 (D4). Decided with Alex 2026-10-31 (D9): `ddt_mandel` is not made a large core. A large core would leave about 111 DSP
  (18 engines, 1.8 GigaIters/s) against the partial core's 60 DSP (8 engines, 0.8); Alex, after hearing this: "ok, then we leave it as it is" (translated).
- A palette editor, load / save of views, settings in the menu: none in DDT's designs, none here.
- Releases: experimental cores, no release-set entry, no GitHub step (Alex does releases by hand).

## §5 Decisions taken while building

**What was built (2026-10-31).** `cores/ddt_mandel` and `cores/ddt_novic` (layer2 wrapper, DDT's files in `rtl/` with `SHA256SUMS` and `.diff` for the changed ones,
our glue in `trx/`, `NOTICE`, `core.toml` experimental, `core_sources.tcl`, logo), the Verilator benches S2 to S5 (`ddt_mandel_pix`, `ddt_mandel_joy`, `ddt_mandel_pins`,
`ddt_novic_pix`, `ddt_novic_pins`, each with its controls), `tools/check_ddt_tree.py` (S9, lint), the core-app descriptors (IDs `DDTM`, `DDTN`; registers CTRL 0A0, STATUS 0A4 with
held = bit 1, FB 0C0 to 0CB, read from `ddt_regs.v` and the two status wires by `test_ddt`), `tools/check_core_id.py` (a third form: `.ID(32'h...)` in the layer2 file), menu goldens, logos
and a menu render. Reading the numbers off the code, not the prompt: the picture is 352 x 256 (`ddt_mandel`) and 352 x 200 (`ddt_novic`) RGB565 at scale 3, buffers at `0x01D00000`, stride `0x100000`.

**Engines: 8, not 20.** An engine is 6 DSP48E1 (three 25 x 25 products, 2 each); `pb_l2` has 60 DSP (`host/build.tcl`: `DSP48_X0Y0:Y39` + `X1Y0:Y19`). 20 engines = 120 DSP do not fit; 8 engines
= 48 DSP leave 12 for placement. 0.8 GigaIters/s instead of 2 (D4, D9: Alex kept it a partial core).

**Font substitution.** DDT's text uses the C64 character ROM (`c64_chargen_ucg.hex`); ROMs never go into the tree (D1). `c64_font_rom` keeps its name and ports, reads Gideon's ASCII 8 x 8 overlay font
`host/mem/ovl_font8.mem` (GPL-3, PROVENANCE section 3) and maps the C64 screen code to ASCII (`code < 32` adds 64); the glyphs differ from the C64's. The bench model reads the same file.

**Mandelbrot freeze.** `CTRL b1` (Freeze) masks the joystick only; the engines keep working and a render in progress finishes (D7 as corrected above).

**D5 hue ramp.** DDT's `hue_x = floor(x * 256 / 720)` is built from a multiply by the constant 91 (`hue_mul = x * 18'd91`, `hue_x = hue_mul[15:8]`), not from shifts; it is within 1 count of the exact value over the 704 columns and is kept as he wrote it
(`rtl/video_pattern.v.diff` moves it unchanged).

**Resource estimate (not Vivado; the CI core stage line `=== T168 ... synth` gives the real numbers).** Against `pb_l2`: 60 DSP, 50 RAMB36, about 16,800 LUT.
- `ddt_novic`: Yosys `synth_xilinx -family xc7 -flatten -nodsp` (without `-nodsp` Yosys' DSP pass asserts on the two combinational dividers, `/ 255` and `2^24 / zoom`; they are therefore LUT logic in this count; Vivado may place part of them in DSP): about 3,600 LUT (LUT2..LUT6; 883/790/684/903/360), 1,070 FF, 389 CARRY4, 17 MUXF7, 2 RAMB36, 0 DSP. About a fifth of the LUTs of the partition.
- `ddt_mandel`: Yosys cannot map the whole core (the palette ROM `ddt_palette.v`: "no valid mapping found for memory"); one engine on its own (`pixel_gen_mandelbrot`, Yosys with DSP inference) is
  6 DSP48E1, 31 CARRY4, about 124 LUT, 34 FF, so 8 engines are 48 DSP and about 1,000 LUT. The frame buffer is 22 RAMB36 (352 x 256 x 8 bit = 90,112 bytes in 4 KB blocks) plus the two-line buffer, the writer's
  128 x 128 bit line RAM (`ddt_pixw.v`, one RAMB36) and the font ROM: by count about 26 RAMB36 of 50. The LUTs of the scan writer (`fb_scanline_writer.sv`, per-engine bookkeeping in registers) and glue are not measured:
  no number is given here; 8 engines of about 124 LUT cannot make the rest exceed the partition, but that is a judgement, not a measurement.

**Timing: the engine is pipelined, two pixels per engine (CI run 1101, 2026-10-31).** The core stage failed `ddt_mandel` only: WNS -2.304 ns in `cpu_mmcm` (100 MHz), worst paths
`G_PX[n].u_px/zx_zx_50_reg0/CLK` to `.../zx_zx_50_reg0__0/CEB2` (also `zy_zy_50`, `zx_zy_50`), data path 11.68 ns, 13 levels (6 CARRY4, 1 DSP48E1, LUT2..LUT6). Cause, read in
`rtl/pixel_gen_mandelbrot.sv`: one iteration per clock, all combinational: 25 x 25 products, 50 bit add (`add_u50_carry4`), compare with 4.0, then `busy`/`zx`/`zy` update that drives the
DSP input register enables. Fix (coordinator's decision): `trx/pixel_gen_mandelbrot.sv` and `trx/fb_scanline_writer.sv` (upstream files unchanged, `.diff` in `rtl/`, NOTICE, `tools/check_ddt_tree.py`).
- Engine: loop of two register stages, two pixel contexts chase each other (one iteration per clock per engine as before, two clocks per iteration per pixel). Stage 1: `zx/zy` (DSP input regs)
  to the three products `p_xx/p_yy/p_xy` (DSP output regs). Stage 2 (context `s2`): `p_*` to `z(n+1) = z^2 + c` into `zx/zy`; `p_xx + p_yy` to the 51 bit register `mag2_r`; the tag (valid, context,
  iteration, last) to `t_*`. Stage 3, outside the loop: `mag2_r > 4.0` or `last` decides done/iter8 of the tagged context. The decision of iteration n arrives after the context has started n+1 with a
  junk z; the context stops (`act`, `busy` low), its next tag is invalid, so the result per pixel (iter8) is DDT's. Same ports per context as before (`start/busy/done/iter8` twice).
- Scheduler: `NCORES` stays the number of engines (8, D9); its bookkeeping runs over `NL = 2 * NCORES` pixel contexts (context k = engine k % NCORES, slot k / NCORES, so slot 0 of all engines fills first).
  Retire stays one pixel per clock; the retire priority is over 16 instead of 8.
- DSP: still 6 per engine, 48 in all (the second context shares the multipliers).
- Estimated worst path (not Vivado): stage 1 is DSP to DSP (cascade of the 25 x 25 product, registers on both sides) about 5 ns; stage 2 is `p_*` through one mux LUT, the 3 operand 25 bit add
  (about 7 CARRY4) and the zero-load mux into `zx/zy`, about 4 to 5 ns, 4 levels; the 50 bit add to `mag2_r` is 13 CARRY4 and a register, about 3 ns; stage 3 is the 51 bit compare (3 LUT levels) into
  the context flags, about 3 to 4 ns. Against 11.68 ns before the worst is about half of the 10 ns period; a third stage was not needed by this estimate.
- Checks: `ddt_mandel_pix` (splash on and off), `ddt_mandel_joy`, `ddt_mandel_pins` pass pixel-exact against the Python model with their controls; new control `ctl_ilv` (`-DDDT_MUT_ILV`: stage 2 adds the
  `c` of the other context) must fail the pix bench and does. `tools/benches.sh ddt_mandel -f` on the builder: pix PASS 341 s, joy PASS 248 s, pins PASS. Open: the Vivado core stage (WNS).

**Checks of this work (2026-10-31, builder).** `ci/trxapps.sh` in `u64-build-env:ci`: RISC-V host app and core app build, `tests/hostapp` and `tests/coreapp` (75 test programs, 1,069 controls) and `tests/loader` pass; `tools/benches.sh ddt_novic ddt_mandel`:
5 benches PASS; menu render of both cores (`tests/coreapp/render/run.sh`, `ddt_*-actions.png`, `ddt_*-home.png`).
