# T265 P4 — RGB out of the host on the A/V port: design (research, no code changed)

Read on 2026-11-26. Tree: `.claude/worktrees/t239.3-pkgdiag` (head 87d3f85cd = origin/t239.3). Specs: `T265` and `T259` from
`origin/integration`, `T140` from the worktree. Numbers of the host: package of run 45814821390 (scratchpad copy `pkg160/`, report date
2026-11-21). The sketch and its yosys runs: `docs/specs/T265/p4-sketch/` (`av_rgb.v`, `fetch.v`, `stat_*.txt`). All paths below are in the worktree.

## Result in one paragraph

In the DFX host no picture with a 15 kHz raster exists. Every DFX core that ships (VC-20, Atari 800, Apple II, ddt_novic, null) writes its
picture as RGB565 frames into DDR2. The host menu is an LVGL canvas in DDR2 too. The pixel layer reads the shown buffer at the HDMI pixel clock
only. The core gives the host a raster position (`pix_x`/`pix_y` in `sys_clk`) and nothing else: no sync, no blank, no pixel enable. So the RGB
encoder needs its own 15 kHz scan-out of the pixel layer's shown buffer: a new host module in `sys_clk` (dot4x, the clock the diag core and the
C128 already drive the DAC with), its own 504x312 / 520x263 raster (timing from `c128_avout.v`), line fetches from DDR2 through a second
requester in `pix_layer`'s fetcher, and a 2-tile line RAM. It shows the menu because the menu is the pixel layer. The contract does not
change. The register goes into the host's mode request block (`mode_req.v`, overlay window `0x10144F0A`). Estimated cost: about 560 LUT
(yosys; Vivado usually packs 10-20 % less), about 790 FF, 2 block RAM tiles, 0 DSP. That puts `pb_l1` from 76.3 % to about 79.5 %.

## (1) The host's video path today

- **What the core hands over.** The contract ports for video (`contract/layer2_ports.txt`): `pix_color[3:0]` :234, `pix_y[8:0]` :247,
  `pix_x[9:0]` :248 (outputs, `sys_clk`). The rest is register forwarding for the pixel layer: `px_we`/`px_re` :194-195, `px_rdata` :131,
  `px_ev_flip`/`px_ev_vsync` :68-69. Also `vmode_sys` :98 (an input, the System Mode). There is no sync, no blank or DE, and no pixel
  clock enable. The VIC path writes one colour per `sys_clk` at {line, x} (`host/vic_scaler.v:84`).
- **What the DFX cores put on `pix_*`.** Only a black raster that the scaler locks to: 504 dots of 4 `sys_clk`, 312 lines, colour 0
  (`cores/vc20/rtl/trx/vc20_glue.vhd:95-109`, `cores/null/layer2_null.v:283-298`, `cores/ddt_novic/layer2_ddt_novic.v:325-327`). The
  picture goes into DDR2 through the general port `e_*` (`cores/vc20/layer2_vc20.v:5-6`, the same note in nes/atari800/c264/zx/cpc/snes).
  The DFX host ties `c64_type` to 0 (`host/top.v:1407`; `host/hdmi_out.v:14-15`: "no DFX core ships on the VIC path, T164 Q4").
- **Where it enters the host.** `host/top.v:985-987` (wires) and `:1805-1817` (l2 ports) -> `hdmi_out` (`host/top.v:1405-1424`). Pixel
  layer register writes arrive from the core: the core forwards page 6 of its trx_io window (`cores/null/null_regs.v:8,36-37`), and the host
  gates them during a swap (`host/top.v:1586-1587`).
- **The chain in `hdmi_out.v`**, all in `pix_clk` (the TMDS clock of the HDMI row, 92-149 MHz, `hdmi_out.v:7, 97-110`):
  `hdmi_timing` -> `vic_scaler` (only on the 1080p rows; it also gives the `align` frame lock, `:157-165`) or the timing's black (`:166-171`) ->
  `overlay` (Gideon's char generator, `:180-188`) -> `pix_layer` (the DDR2 buffer, `:194-205`) -> `c64_text` (`:216-229`) -> `hdmi` encoder (`:247`).
- **The pixel layer** (`host/pix_layer.v`): one shown buffer at a time (PX_FRONT/PX_BACK, `:74-75`). It fetches source lines from DDR2 as
  the video client `v_*` of `mig_arb` into one 512 x 128-bit line memory with ONE read port in `pix_clk` (`:25-34`). The fetcher is in `ui_clk`
  (`:682-765`). Formats: 1 = RGB565, 2 = 32-bit (`:4-6`). Every DFX core uses format 1. Format 2 is only the A1200's, and format 3 only the
  C128's, both large cores (`coreapp/src/desc.h:206-214`).
- **The host menu** is LVGL RGB565 on the pixel layer (`host-app/software/lvgl_port/trx_lv_port.cc:22-31,111`). The canvas is 960x540, or
  the size of the HDMI row below 960 wide (`coreapp/src/video.cc:111-146`). The menu and a core's picture take turns on the one layer
  (`coreapp/src/video.h:24-27`). `overlay.v` holds only the "Starting <name>" line of a direct start (`host-app/software/hostapp/ovtext.h:1-3`).
- **Where a 15 kHz raster exists:** only as the `pix_*` position stream in `sys_clk`. For frame-buffer cores it carries no picture. The
  scaled frame exists only in `pix_clk` at the HDMI row's line rate. A 15 kHz picture of the menu or of a DFX core does not exist anywhere in
  the host. (`top.v:1090-1106` already measures the `pix_*` raster: lines per frame and clocks per line, in `sys_clk`.)

## (2) Where the RGB encoder goes

**Picture source: the pixel layer's shown buffer, read again at 15 kHz.** Three alternatives were looked at and rejected:
- Tapping the HDMI chain's output needs a fixed 2:1 line ratio. That exists only on the 576p/480p rows.
- The VIC path carries no picture for any shipping DFX core.
- AV ports in the contract would mean a contract change, a rebuild of every core and an encoder in every core (against T265 "the host puts out RGB" and D24).

**Module `host/av_rgb.v`**, instantiated in `hdmi_out.v`. That file already has `sys_clk`, the pixel layer and `mode_req`.
- **Clock:** `sys_clk` (dot4x: 31.528 / 32.726 / 32.765 / 31.585 MHz by System Mode, `c128_avout.v:23-24`). `AVID_CLK` = inverted `sys_clk`
  through an ODDR, and the words come from IOB flops. This is the scheme the diag core ran on the Sony (`cores/diag/mono/top.v:1911-1919`) and
  the C128 too (`cores/c128/mono/top.v:1426-1440`). The clock groups are already asynchronous (`host/pins.xdc:168-181`), and `sys_clk` is
  timed for 32.765 MHz (`:183-191`).
- **Raster:** its own counter, 4 clocks per dot. 50 Hz = 504 dots x 312 lines, 60 Hz = 520 x 263. Hsync 37/38 dots and three broad vsync
  lines, as `cores/c128/mono/c128_avout.v:42-73` (its S2 bench covers sync widths and periods). Window: 400 dots (1600 clocks) starting 91 dots
  after the sync edge; 288 lines (50 Hz) or 240 lines (60 Hz). It runs free. At equal standards its frame is exactly the `pix_*` raster's
  frame (same clock, same counts), so there is no drift against the core. Tearing cannot occur either: at its own frame start it takes the
  newest buffer the app named (PX_FRONT/PX_BACK), and the cores rotate 4 buffers (`coreapp/src/video.cc:329`).
- **Standard (D5):** the register field `std`. 0 = follow the core: the measured lines per frame of `pix_*` (`top.v:1090-1106`) choose
  312/263. 1 = 50 Hz, 2 = 60 Hz. The null and VC-20 rasters are 312 lines in every System Mode, so "follow" gives 50 Hz timing on them (52 Hz
  at the NTSC clock), which is the core's own frame rate.
- **Fetch:** at the start of output line L, the source line(s) of L+1 are requested (address = shown buffer + n x stride, stepped by adding,
  no multiplier). A second requester in `pix_layer`'s `ui_clk` fetcher serves it. Same `v_*` client and same `v_urgent`, so `mig_arb` does
  not change. The data goes into a separate line RAM of 4 slots x 128 words x 128 bits (2 RAMB36 = 2 tiles; write port `ui_clk`, read port
  `sys_clk`). An HDMI request waits for an RGB fetch in flight instead of stopping it (at most 2 x 120 reads, about 2.5 us). At 1080p an HDMI
  line is asked for one output line (14.8 us) ahead. Extra load: at most 240 reads per 64 us (the HDMI side at 1080p is about 520 per 64 us).
- **Geometry (constants, no register):** the source W fills the 1600-clock window (nearest, phase accumulator, W <= 960). Vertically 1:1
  with a centred crop or black bars. Source pictures taller than 1.25 x the window get 2:1 with a two-line average: the menu at
  540/576/480/600, the Apple II at 384 (its doubled lines, so the average is exact). VC-20 283 x 376 and Atari 288 x 384 come out 1:1, as on
  their own hardware. The 16:9 menu is squeezed to 4:3.
- **Levels and pins (D5 constants):** names map, MSB first: `AVID_R[9:2]` = R[7:0], `AVID_G[9:2]` = G, `AVID_B[9:2]` = B (T259 §5;
  `diag_avout.v:211-235`, names map at `:226-232`). RGB565 is widened by bit repetition (as `pix_layer.v:472-474`), then x 11/16 by shift and add
  (8x + 2x + x, no DSP). 11/16 is the "White level" of the `Sony-RGB` preset the board pass used (`cores/diag/sd/presets/Sony-RGB.toml`),
  so 0 is black and 175 is white. Black outside the window. `AVID_SYNCn` = csync (low in the pulses), `AVID_FB` = 1 in RGB mode
  (`diag_avout.v:244-245`, synm 2 / fbm 1). Mode off: words 0, SYNCn 1, FB 0.
- **Menu overlay mixed in?** Yes for the LVGL menu, because it is the pixel layer. `overlay.v` (the ovtext start line) and `c64_text` (the
  C64 freeze text) are not on RGB: they draw in `pix_clk` from their own RAMs, and no shipping DFX core uses `c64_text`.
- **What the core side must give:** nothing. The contract (`contract/layer2_ports.txt`) stays at 247 ports and `contract/VERSION` stays 2.
  `contract/version.toml` gets a MINOR bump (T221: MAJOR = contract).
- **Are the AVID pins free in the host?** Yes. They are in no port of `host/top.v` and in none of the XDC files `host/build.tcl:117-120`
  reads (`ddr2_pins`, `pins`, `gtp_pins`, `usb_pins`). `host/c64u.xdc:168,205,273-297,484-485` is Gideon's reference copy and no build
  reads it. Only the large cores drive the pins: c128 (`cores/c128/mono/top.v:117-120`, `mono/avid_pins.xdc`) and diag
  (`cores/diag/mono/top.v:117-119`, `mono/avid_pins.xdc`). Bank 16 is already 1.8 V in the host (RMII, `host/pins.xdc:2-3`).

## (3) Cost and the host's budget

yosys `synth_xilinx -family xc7 -flatten` on the sketch (`docs/specs/T265/p4-sketch/`; `nice -n 15`). The figures count LUT1-6 and FDRE/FDSE:

| part | LUT | FF | BRAM | DSP |
|---|---|---|---|---|
| `av_rgb` RGB565, 2-line blend (recommended) | 486 | 722 | 2 RAMB36 | 0 |
| `av_rgb` without the blend (2:1 nearest) | 420 | 553 | 2 RAMB36 | 0 |
| `av_rgb` with format 2 (32 bit) as well | 640 | 739 | 2 RAMB36 | 0 |
| fetcher: `fetch_new` - `fetch_old` (the ui_clk block of `pix_layer.v`) | +63 | +65 | 0 | 0 |
| `mode_req` register 0x4F0A + read-back (estimate) | ~10 | ~6 | 0 | 0 |
| **total, recommended** | **~560** | **~790** | **2 tiles** | **0** |

Most of the FFs are the two 128-bit word holders and the crossings. 26 of them go into IOBs. The D5 alternative, the diag core's full encoder,
was measured at about 870 LUT, 510 FF and 14 DSP (T265 D5).

The host now (`pkg160/host.utilization_pb_l1_opt.rpt`, post-opt; floorplan B, `ci/vivado.env:12`):
- `pb_l1` Slice LUTs 13,271 of 17,400 (76.27 %), registers 17,235 of 34,800 (49.5 %), block RAM 24.5 of 35 tiles, DSP 24 of 80.
- Whole chip: bonded IOB 196 of 250 (`host.utilization.rpt:132`). AVID adds 27 pins: 223 of 250.
- `pb_l1` = `SLICE_X50Y0:X65Y99` + `SLICE_X0Y100:X65Y149`, RAMB36 X2Y0:Y19, X1Y25:Y29, X0Y20:Y29, DSP X1 whole + X0Y40:Y59
  (`host/build.tcl:253-262`).
- Video chain today (`host.utilization_hier.rpt:168-229`): `hdmi_out` 3,845 LUT, of which `pix_layer` 1,433 (2 RAMB36, 12 DSP),
  `overlay` 439, `hdmi_timing` 477, encoder 713, `c64_text` 271, `vic_scaler` 101 (4 RAMB36).

After P4: about 13,830 LUT (79.5 %; with Vivado's tighter packing about 79 %), 26.5 tiles of block RAM, DSP unchanged. Limits on record: the
host did not route at 82 % (fpga-gateware skill §2), and placement failed at 83.1 % (`host/build.tcl:207`). P1-P3 are boot ROM and app code
and add no LUTs.

## (4) The register, the app and the menu row

- **Home:** `host/mode_req.v`, the host-owned HDMI mode block in the overlay window (`:1-14`). It is in `cpu_clk`, it is not routed through
  the core, and it keeps its value across a core start or swap: no reset; `req_val` has only an init value (`:38-42`). Pixel layer page 6 is
  the wrong home: it is reached only through the core's forwarding (`null_regs.v:36`), and not while decoupled.
  - write/read `0x4F0A` AV: b1:0 mode (0 off, 1 RGB; 2 Y/C, 3 composite reserved for T140), b3:2 std (0 follow the core, 1 50 Hz,
    2 60 Hz). Power-up value: off (question below).
  - `0x4F05` version 1 -> 2, so an app can tell a host with A/V (`hostvideo_board.cc:57` already checks `>= 1`).
  - TOML A: a new block `overlay_av_mode` at `0x10144F0A` (size 1, `rw`) beside `overlay_mode_request` / `overlay_mode_status` in
    `coreapp/gateware/host.toml:972-1020`. `tools/rtl_check.py` checks it against the RTL.
- **Host app:**
  - `hostvideo.h:88-89` gets `gw_av_write(mode, std)` next to `gw_write`, with the board side in `hostvideo_board.cc:57-58`.
  - `boot.cfg`: `video=<system>,<hdmi>,<av>` (`bootcfg.h:9`, `bootcfg.cc:49-55,105`). T139 D5 said "`video=` keeps its syntax; an analogue
    mode becomes a new value" (`docs/specs/done/T139-host-ui-files-rename-settings.md:95-96`). The old parser stops at the second comma
    (`atoi`), so an old host reads the line and drops the field when it saves.
  - Written at start-up from `boot.cfg` and on every change.
- **Menu row (T139 page, F8):**
  - A new kind `SK_AV` after `SK_HDMI` (`host_core_rows.h:151-152`, list `host_core_rows.cc:893`, text `:930`), named e.g. "Analogue out"
    with the values Off / RGB, and REST key `settings.av` (`hc_set_key_name`).
  - `HC_SET_SLOTS` goes from 10 to 11. The page already fits ten rows in the height nine had (`hostui.cc:2106`), so the layout needs the
    ux-review skill.
  - Tests: `tests/hostapp` (boot.cfg round trip, the settings rows' golden files).
  - The core app needs nothing: the register outlives the core start, and std 0 follows the core.

## (5) Risks

- **Density of `pb_l1`** (main risk): 76.3 % -> about 79.5 %, against a measured no-route at 82 %.
  - Read `=== T79 pb_l1 | Slice LUTs` and the router's overlap trend in the first host lane run.
  - Fallbacks: no blend (-66 LUT).
  - Building the VIC path out (`NO_C64VIDEO`, `hdmi_out.v:151-156,211-215`) frees about 372 LUT and 4 RAMB36. That is Alex's call: it ends
    the 1080p frame lock (`align`) for frame-buffer cores.
- **DAC clock timing:** low.
  - 30.5 ns period, IOB flops, the DAC takes the words half a period later on the inverted clock: the same as the diag core's board pass.
  - Place: the AVID IOBs are in bank 16, clock region X0Y2. `sys_clk` must reach there, and `ui_clk` and `sys_clk` reach the line RAM.
    Check the per-region global clock count (12) in the placer log; it hit 14 of 12 once ([Place 30-410], `build.tcl:181-182`).
- **The fetcher is shared with HDMI:**
  - A wrong arbitration shows as HDMI underruns (PX_STATUS b15:8).
  - The video client's extra reads also delay the drive and cartridge clients behind the `V_READS` cap (`ddr2_port.v:44-68`). Run
    `host/sim/lat` once with the RGB load.
- **Contract:** unchanged, so no core source changes. **Every DFX core rebuilt:** yes. A new host static means a new host key, and every
  partial is routed against it (`ci/lib.sh:292`). The matrix does that by itself (T265 D1 expects it).
  - The large cores do not change: they build from their own copies (`tools/check_large_copies.py`, `cores/*/mono/host/`), as S8 needs.
- **Host is pinned:** `contract/host_pin.txt` (`tools/host_pin.sh --update`) and the explicit `host:` list in `host/layers.txt` (new file
  `host/av_rgb.v`). Both are a host change Alex approves (`host/layers.txt:32-36`).
- **Picture quality not proven:**
  - The menu at 2:1 with the blend on a CRT has not been seen (board, S7).
  - 60 Hz RGB timing is bench-checked in the C128 only and has never been on a TV.
  - The 11/16 level is the preset's value. Nobody measured whether full scale is the SCART 0.7 V.
- **No bench today covers this.** A `tb_avrgb` (sync and window timing, the fetch handshake with a DDR model, levels) would be new sim
  infrastructure, which Alex's rules gate.
- **Stale text found:** `docs/specs/T140-analog-video-out.md:150` says "RGB/SCART: the port has no RGB lines", which T259 §5 disproved.
  The fpga-gateware skill §2 still gives `pb_l1` as 13,000 LUT; it is 17,400 since T164 (`build.tcl:207-209`).

## Files to change

- `host/av_rgb.v` (new)
- `host/hdmi_out.v`: instance, the `sys_clk` inputs, AVID outputs
- `host/pix_layer.v`: a second requester in the ui_clk fetcher; export of the register copies and toggles
- `host/mode_req.v`: 0x4F0A, version 2
- `host/top.v`: AVID ports, the ODDR, `vm_lines` into hdmi_out
- `host/pins.xdc`: the AVID lines, as `cores/c128/mono/avid_pins.xdc`
- `host/build.tcl:107-111`: read av_rgb.v
- `host/layers.txt`
- `contract/host_pin.txt`
- `contract/version.toml`: MINOR
- `coreapp/gateware/host.toml`: the new block
- host app: `hostvideo.h`, `hostvideo_board.cc`, `bootcfg.h/.cc`, `host_core_rows.h/.cc`, `hostui.cc`; tests in `tests/hostapp`
- Docs: `docs/specs/T265-host-update-app-guard.md` §5, the stale T140 §4 line

## Open questions

For the coordinator:
1. Register home `mode_req` 0x4F0A (host-owned), not pixel layer page 6 (via the core): agreed?
2. D5 "the standard follows the core's raster", read as std 0 = measured `pix_*` lines per frame, with 1/2 forcing 50/60: agreed?
3. The RGB fetch as a second requester inside `pix_layer` (HDMI path shared), or its own `mig_arb` client (shared arbiter touched)?

Only Alex:
1. Power-up value of the A/V mode: off, or RGB?
2. Row name and place ("Analogue out: Off / RGB" on F8 under Picture), and `boot.cfg` `video=<sys>,<hdmi>,<av>`?
3. The menu on a CRT at 2:1 with a two-line blend, squeezed 16:9 -> 4:3: acceptable, or interlace (+~100 LUT)?
4. The "Starting <name>" line (`overlay.v`) shows on HDMI only: acceptable?
