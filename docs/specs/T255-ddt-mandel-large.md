# T255 — DDT's Mandelbrot demo is a large core: his engines, a frame buffer, HDMI, nothing else

**Status:** PROPOSED (2026-11-22, Alex: GitHub issue #123).
**Repo:** TRX64U-MultiCore — `cores/ddt_mandel/mono/` (the large core), `cores/ddt_mandel/{rtl,trx}` (his engines, our glue), tools and CI where the partial lane was named.
**Number:** T255 (registry: the table in `TODO.md`).
**Origin:** Alex, 2026-11-22 (translated): "Basically give DDT's demo *only* HDMI. No menu, nothing, and then pack it as a .cor. Large core." In #123: "I will rebuild it as a large core, currently it is a partial/dfx and that limits space and capabilities a lot." DDT in #123: an engine is 6 DSP, the 50T has 120, so 20 engines; "if you really need 6 DSPs for HDMI, then there should be no problem instantiating 19 Mandelbrot engines instead of 20".
**Depends on:** T168 (the partial core this replaces; DONE), T164 (large cores, own copies of the host files), T115 §7 (the large .cor).
**Milestone:** M2.

---

## §1 The problem

`ddt_mandel` (T168) is a partial core: 6 engines (36 DSP of the 40 DSP of `pb_l2`), its picture as RGB565 in the DDR2 through the host's pixel layer, steered by the generic core application. DDT's own design has 20 engines (all 120 DSP of the XC7A50T) and keeps the picture in a 352 x 256 x 8 bit BRAM frame buffer. He asked for what the partition took away.

- **P1 The engine count.** 6 engines (0.6 GigaIters/s) instead of 20. A large core owns the whole device; the DSP budget is 120.
- **P2 The picture path.** The partial core goes through DDR2, the pixel layer and the application. The large core has no host: it needs its own HDMI output (clocks, CDCE synthesizer, GTP transmitters, encoder) from the host's own HDMI pieces, without a DSP, because every DSP is an engine.
- **P3 The demo's own controls.** Joystick port 2 pans, zooms, changes the iterations, resets (DDT's `joystick_buttons.sv`): the pins are read directly, the application is not there.
- **P4 The package.** A large `.cor` needs a bitstream, a lane (`ci/plan.py`, `ci/bitstream_mono.sh`) and an application record; this core has no application.
- **P5 The way out and the old lane.** No menu: the way back to the host is the power cycle. The partial lane, its application descriptor and its benches go; `ddt_novic` shares files of `cores/ddt_mandel/trx` and stays working.
- **P6 Honest numbers.** The splash text says the real engine count; his licence asks that changes are marked.

## §2 Scenarios

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P4 | `ddt_mandel.cor` is a valid large .cor (`cor_pack.py check`), the host lists it as full firmware | `tools/pack_large_cor.sh` ends with `cor_pack.py check`; the host row (`cor_probe == COR_ERR_LARGE`) | CI `package`, then board: boot menu lists "DDT Mandelbrot" |
| S2 | P1, P2 | The flat design routes at 100 MHz (engines) and 148.87 MHz (pixel): WNS >= 0; DSP48E1 = 6 x engines, nothing else in DSP | Vivado `utilization.rpt`, `timing.rpt` of lane `large-ddt_mandel` | CI `bit-mono` |
| S3 | P2 | From the boot menu the core starts; HDMI shows 1920 x 1080 / 50 Hz, the Mandelbrot in the middle, the splash text for about 10 s | board step by hand, photo | board session |
| S4 | P6 | The splash row 2 names the engine count the design has (the parameter `MANDEL_CORES`) | bench `ddt_mandel_pix` (text frame, engines = `DDT_NE`) and the photo of S3 | CI `sims`, board |
| S5 | P3 | Fire + direction on port 2: pan, zoom in/out, iterations +/-, RESTORE resets the view; the picture follows | bench `ddt_mandel_joy` (fire + up = zoom in, against `mandel_model.py`); the rest of the keys by hand on the board, photo | CI `sims` (integration), board |
| S6 | P2 | The picture scanned out by the HDMI timing equals DDT's picture (model written from his code), scaled 4 x, centred, text overlay as his | bench `ddt_mandel_pix`: every pixel of a 1080p frame against `mandel_model.py` | CI `sims` (integration), local |
| S7 | P5 | A power cycle brings the host back (the scratch file does not start itself); when the demo is the boot default, C= or port-1 fire at power-on shows the menu | board step: power cycle after S3, and once with the demo as the default entry; reading of `legacy.h`, `bootcfg.h` | board session |
| S8 | P5 | The partial lane is gone and nothing else names it: lanes, plan, lint, `ddt_novic` still build | `python3 ci/orchestrate.py check`, `tools/core_toml.py --check`, `tools/check_large_copies.py`, `tools/check_d24.py`, `tools/check_ddt_tree.py`, `tools/check_todo.py` | local, CI lint |

Negative controls: none (no flash, no persistence).

## §3 Decision

Alex's: the demo plus HDMI only; no menu; a large core packed as `.cor`; replaces the partial core. Everything below is a technical choice of the author, marked PROPOSAL, for Alex to overrule.

- **D1 One flat design, a slim top (PROPOSAL).** `cores/ddt_mandel/mono/top.v`: clock input (50 MHz), the HDMI chain, the CDCE I2C sequencer, joystick pins, the demo. No RISC-V, no DDR2, no Ethernet, no SD, no USB, no keyboard scan. Instead of a copy of the 100 KB host top: less to route, DSP columns free.
- **D2 HDMI (PROPOSAL).** Mode 2 (1080p50, the host's proven row; frame-buffer cores use the PAL reference) with the host's chain as own copies in `mono/host/`: `cdce_mode`, `hdmi_timing`, `hdmi_enc/`, `tmds_rep`, `hdmi_gtp` (Vivado gtwizard `gt_hdmi`, same configuration as the host), the clock chain of `top.v` (`mmcm_sys` 31.53 MHz -> `mmcm_ref` 37.89 MHz -> `HDMI_CLK_SYNTH`), `mmcm_pix`. Audio channel silent. `HDMI_DDC_EN` is high when the CDCE sequence is done and HPD is seen.
- **D3 Picture (PROPOSAL).** 352 x 256 shown 4 x (1408 x 1024), centred in 1920 x 1080 (256 / 28 black borders); read from the BRAM frame buffer (his `framebuffer_bram.sv`, port B on the pixel clock) through a 8-stage pipeline: row base, address, BRAM, palette index, palette ROM (RGB565), text overlay, expand to 24 bit. Palette rotation, 44 x 25 splash text and its 501 frames are his, counted in HDMI frames (50 Hz as his PAL).
- **D4 Engines (PROPOSAL).** `MANDEL_CORES = 20` = 120 DSP = all of the device. The HDMI path, the video path and every glue module use no multiplier (`use_dsp = "no"` where a module could be taken for one). If the Vivado run needs a DSP elsewhere, the result is red and the count goes to 19 (DDT agreed in #123); the splash text takes the digits from the parameter, so it cannot say a wrong number.
- **D5 Controls.** Port 2 joystick pins and RESTORE, active low with the host's pull-ups, 2-FF synchronised; `CIA_BUFFER_EN`, `CIA_PWR_EN` high (without them the joystick lines do not reach the FPGA: `mono/host/top.v` comment of the host).
- **D6 Application record (PROPOSAL).** No application runs. `app = "none"` in `mono/core.toml`; `ci/package.sh` packs a 128 byte zero file as the record. The host stages it in DDR2 (rounded to 128 bytes), the trampoline copies it to 0x30000 and the IPROG replaces the machine; no CPU exists in the new bitstream to execute it. `tools/core_toml.py` accepts `app = "none"` for a large core. The generic core application is not shipped in this `.cor`.
- **D7 The way back.** None in the core. `legacy.h`: "Nothing returns: a power cycle loads the host again" (the scratch file `/Flash/legacy/fullcore.bit` is read only for the IPROG, the configuration flash is not written). Alex power-cycles.
- **D8 Partial core removed (PROPOSAL).** Deleted: `cores/ddt_mandel/{core.toml,core_sources.tcl,layer2_ddt_mandel.v}`, the glue only the partial core used (`trx/ddt_mandel_core.v`, `ddt_mandel_video.v`, `ddt_palette.v`), its Verilator benches (`ddt_mandel_pix/joy/pins`), `coreapp/cores/ddt_mandel.cc` with its table, header, Makefile, ledger, test and golden entries. Kept: `rtl/`, `trx/` files `ddt_novic` reads (`ddt_regs.v`, `ddt_dots.v`, `ddt_pixw.v`, `ddt_clk_load.v`, `c64_font_rom.v`), `ddt_regs.h`, the sim helpers `ddt_novic` uses.
- **D9 Own copies.** The HDMI files and the two ROM images (palette, font) are copies in `cores/ddt_mandel/mono/` (T164 3a Q1: "Copies instead."); `mono/host/FROM` names the source commit and lists the files (`tools/check_large_copies.py`).

## §4 Out

- Menu, application, settings, media, network, USB, SD, keyboard: none (Alex).
- Audio: DDT's design has none; the HDMI audio channel carries silence.
- 100T (40 engines): not built (Alex: 50T only).
- DDT's PAL / composite modulator and the A/V port: not built (HDMI only).
- Resolution choice, EDID reading, hot-plug re-lock after the sink is replaced: the picture is 1080p50 fixed; a re-plug is a power cycle.
- `ddt_novic`: unchanged except that it no longer shares a tree with a lane.
- New CI infrastructure: none. The two benches keep their names `ddt_mandel_pix` / `ddt_mandel_joy` in `ci/sims.txt` (tier 2: they run on pushes to `integration`, not before a merge: Alex, "No benches before a merge"); the pin bench `ddt_mandel_pins` goes (no partition edge).

## §5 Decisions taken while building

**Built (2026-11-22, local checks only; no Vivado, no board).** `cores/ddt_mandel/mono/`: `core.toml` (lane `large-ddt_mandel`, `mono_env = ["TRX_ENGINES=20"]`, `app = "none"`), `top.v`, `ddt_hdmi.v`, `ddt_mandel_hd.v`,
`build_mono.tcl`, `build.sh`, `pins.xdc`, `host/` (the copy, `FROM`), `mem/ovl_font8.mem`. `trx/screen_matrix.v` gets the parameter `ENGINES` and a registered lookup.

- **Bench result (Mac, Verilator 5.052, 2 engines):** `sh cores/ddt_mandel/sim/run.sh pix joy`: all three frames (splash text; splash over; after fire + up, zoomed) equal the model in all 2,073,600 pixels (4 x 4 scaling, borders black, the output counters travel with the pixel);
  11 s and 16 s. This proves the picture path, the splash digits (" 2" for 2 engines; two-digit path by the same code), the joystick path and the frame counters.
- **Engine count `TRX_ENGINES` (env, default 20)** instead of an edit of the source: the cache key moves with it (`mono_key` hashes `mono_env`), and 19 is a one-line change of `core.toml`.
- **`HDMI_DDC_EN`** = i2c_done and HPD (2-FF). The host application switches it on its own later; whether a monitor needs it for this picture is a board question (S3). It is held off while the CDCE sequence uses the HDMI I2C bus.
- **`BITSTREAM.CONFIG.UNUSEDPIN PULLNONE`:** the host's design has every ball as a port; here most balls are no port. Vivado's default pulls unused balls down; that would pull the ESP32's enable lines (`WIFI_DIS`, `WIFI_BOOT`), the slot's `SLOT_RST_O` and the IEC outputs low. Floating is the state before configuration. Whether the board behaves (ESP32, case LEDs, power key) with these balls floating is board step S3.
- **Include shadow:** the copy of `hdmi_modes.vh` is `ddt_hdmi_modes.vh`, so Vivado (cwd `host/`) cannot read `host/hdmi_modes.vh` for it.
- **No DSP outside the engines:** the arithmetic of the video path is shifts and adds; the encoder (`hdmi_enc`), timing and repeater have no multiplier (read: no `*` on signals; `localparam` constants only). Each glue module is `use_dsp = "no"`. 20 engines = 120 DSP48E1 = 100 %: whether placement closes at 100 MHz with a full DSP column set is the first thing the Vivado run tells (S2); the fallback is 19.
- **Resource estimate (not Vivado; the lane's `=== T255 synth` lines give the real numbers).** DSP 120 (6 per engine, `rtl` unchanged by us: three 25 x 25 products of 2 DSP). BRAM: frame buffer 90,112 bytes = 22 RAMB36, line buffer 1, text 1,100 bytes and font 1,024 bytes 1 each, palette 256 x 16 in distributed ROM: about 25 of 75. LUT: an engine about 124 LUT (T168 yosys of the unpipelined one) plus the pipeline stages of the T168 §5 fix: about 300 LUT, 250 FF per engine, so about 6,000 LUT and 5,000 FF; the HDMI encoder and GTP logic about 3,000 LUT (the host's `hdmi_out` share); scheduler 40 contexts and the glue about 2,000. Total about 11,000 LUT of 32,600, 8,000 FF. The 4 GTP lanes and 4 MMCM (sys, ref, cpu, pix) of 5.
- **Timing risks named before the run:** engines 100 MHz at 100 % DSP (T168 closed 6 engines in `pb_l2`); the pixel pipeline at 148.87 MHz (every stage one adder or one ROM read, the text lookup in two stages for that reason); clock-domain crossings are synchronisers and the BRAM's two ports (`pins.xdc` async groups).
- **Application record:** `ci/package.sh` packs a 128 byte zero file when the descriptor says `app = "none"` (`tools/core_toml.py` accepts it for `delivery = "cfw"` only; `tools/test_cfw_core.py` has the positive case, the control with `ultimate` still fails). `legacy_stage` / `legacy_jump` stage it in DDR2 and the trampoline copies it to 0x30000 before the IPROG, as for any large core; the new bitstream has no CPU, nothing executes it. Cost: no 400 KB application to stage.
- **Way back (read in `legacy.h`, `legacy.cc`):** "Nothing returns: a power cycle loads the host again". The scratch file `/Flash/legacy/fullcore.bit` is read by the IPROG only; the configuration flash is not written by a large start, so the next power-on loads the host from flash, not this core. Nothing traps the person in the demo.
- **What the host starts after the power cycle (read in `bootcfg.h` / `bootcfg.cc`, not tried on the board):** the entry named by `default=` in `/Flash/boot.cfg`; with `direct=1` (absent = 1) it starts at power-on without the menu, with `countdown>0` after the countdown. `bootcfg_add` makes the entry the default when there is none (`def < 0`). So on a board with no default, adding `ddt_mandel.cor` makes the demo the default and every power-on starts it again; the escape is the one the host has for every default: a held C= key or fire on port 1 at power-on shows the menu (`bootcfg.h`). Not changed here (the host is frozen); the board step S7 must include "added as the first entry, power cycle, C= / port-1 fire shows the menu". Open question for Alex below.
- **`host/layers.txt`:** one line in `ignore:` (`cores/ddt_mandel`, with `trx/` excluded because `ddt_novic` owns that in its key), the same line every large core added; the file is not part of the host pin (`tools/host_pin.sh --check` clean).
- **Removed with the partial lane:** `core.toml`, `core_sources.tcl`, `layer2_ddt_mandel.v`, `trx/ddt_{mandel_core,mandel_video,palette}.v`, `sim/tb_ddt_mandel.v`, `coreapp/cores/ddt_mandel.cc` and its entries (`table.cc`, `cores.h`, Makefile, `d24_ledger.txt` file line and the `cores.h` count 23 -> 22, `check_core_id.py`, `tests/coreapp/*`, golden `menu_ddt_mandel.txt`), `ddt_mandel_pins` in `ci/sims.*`. Kept for `ddt_novic`: `trx/{ddt_regs,ddt_dots,ddt_pixw,ddt_clk_load,c64_font_rom}.v`, `sim/{mk_l2ports.py,ddr2e_model.v,run_pins.sh}`, `coreapp/cores/ddt_regs.h`.

---

Open (one line, as in the TODO table): IN PROGRESS 11-22: spec, code; open: Vivado WNS/DSP, board S3, S7; 20 or 19 engines
