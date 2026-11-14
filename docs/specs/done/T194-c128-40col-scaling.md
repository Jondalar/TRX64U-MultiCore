# T194 — The C128's 40-column picture follows the Picture setting (Integer, Fit, Fill) as every other core's does

**Status:** DECIDED and being built (2026-11-09: Alex, "make a spec and then send an agent on a branch that fixes the C128", translated; the placement proposal answered the same day, D8-D10). Phase 2 (RTL, app, benches) on `c128-scale`; the Vivado run is the CI's (large:c128). Corrected 2026-11-13 (status check): BUILT, merged to main (4d7c8b30f, 2026-11-09); S6 board partial: D12 hotkey works, Auto switch missed the PETSCII Robots title (open).
**Repo:** TRX64U-MultiCore — the C128 large core only (`cores/c128/`, its own host copies in `cores/c128/mono/host/`) and its
core app record (`coreapp/cores/c128.cc`). The host (`host/`) and every other core stay as they are.
**Number:** T194.
**Origin:** GitHub issue #63 (2026-11-08): "Boot into 40 column mode - Screen scale options do nothing. Boot into 80 column mode -
Screen scale options do work (80column/VDC)". Alex, 2026-11-09 (translated): "we wanted the resolution/scale/fit solution to be the
same in all cores? The C128 is a large core, so why would that cause global changes?"
**Depends on:** T140 §5 item 16 (branch `c128-vicglitch`, the VIC/MMU fix for #19) - this work branches from it, so one package
carries both. T107 (pixel layer Integer/Fit/Fill), T113 (two pictures, `Video::set_hidden`).
**Milestone:** M2 (finish plan wave 2, gateware, batched with T140/T170).

---

## §1 The problem

- **Who:** a tester with a C128 in 40-column mode on a 1080p (or any other) screen.
- **What they want:** the Picture setting (Integer, Fit, Fill) and the HDMI mode change the 40-column picture as they change the
  80-column one and every other core's picture.
- **What happens today (code):** the C128 has two pictures. The VDC (80 columns) is written to DDR2 and shown by the pixel layer
  (`cores/c128/mono/host/pix_layer.v`), whose output size the app computes (`coreapp/src/video.cc` `Video::set_mode` ->
  `video_layout_scaled`, scale mode from `set_scale_mode`). The VIC-IIe (40 columns) goes through the core's copy of
  `vic_scaler.v`: a fixed 4x window of 400 x 270 VIC pixels -> 1600 x 1080 (60 Hz: 4 x 4.5), 1080p rows only, no input for a
  size. The setting is offered in both modes but acts only on the VDC.
- **Why it matters:** a setting that does nothing looks broken (#63); every other core has one picture path with one rule.

Area today (T140 §5 item 13, D29 build): Slice LUTs 27,109 of 32,600 (83.2 %), **Slice 8,122 of 8,150 (99.7 %)**, Block RAM 72.5 of
75 tiles, WNS 0.228 ns. Any addition needs room made first; the JTAG debug master (branch `dbg-jtag`) is not in this build.

Parts:
- **P1 Source:** the VIC-IIe's picture reaches DDR2 every frame as a picture the pixel layer can show.
- **P2 Show:** the pixel layer shows that picture at the size the app sets, with the VIC palette, in every HDMI mode the core offers.
- **P3 App:** the core app switches the shown picture (40 / 80 columns) and applies the same Picture setting to both.
- **P4 Area and timing:** the core still places, routes and meets timing; the room is planned before the build.
- **P5 Nothing else moves:** host, other cores, the 80-column picture and the #19 fix unchanged.

## §2 Scenarios

| # | Solves | Scenario | Test | Where |
|---|---|---|---|---|
| S1 | P1 | A VIC frame written by the capture path holds the 400 x 270 (60 Hz: 400 x 240) window pixel for pixel as `vic_scaler` would show it | sim bench of the capture (`cores/c128/sim/...`, VIC pattern in, DDR2 model words out) | builder, `tools/benches.sh c128` |
| S2 | P2 | The pixel layer shows a 4-bit indexed source through the 16-entry palette at Integer, Fit and Fill for 1080p and 720p: the output pixels equal the reference (nearest sampling) | sim bench of the C128's `pix_layer` copy | builder |
| S3 | P3 | 40 columns: the Picture setting changes the output size (Integer / Fit / Fill), the menu value is the one applied; 80 columns unchanged | core app native test + render bench of the C128 record | apptests |
| S4 | P4 | Vivado: route complete, timing met (WNS >= 0), Slice and BRAM within the device | `cores/c128/mono/build.sh` report lines | CI vivado large:c128 |
| S5 | P5 | All existing C128 benches pass (24 on `c128-vicglitch`), incl. `c128_vicbus` (#19) | `tools/benches.sh c128` | builder |
| S6 | P2, P3 | Board: 40 columns at 1080p Integer / Fit / Fill and at 720p (and one more row), BASIC screen and PETSCII Robots, photo; 80 columns still right | by hand, board session (picture on a real screen) | board |
| S7 | D11 | The five activity words count what the CPU does to the VIC, the colour RAM and the VDC per frame | sim benches `c128_act` and `c128_regs` | builder |

## §3 Decision

- **D1 (decided with D10):** the VIC-IIe goes the pixel layer's way like every other picture.
  The capture that `pix_layer.v` already has (the still: 400 x 270 VIC pixels, 4 bits each, every frame to DDR2) becomes the 40-column
  source; the C128's copy of `pix_layer.v` gets a third format, 4 bits per pixel through a 16-entry palette (the VIC palette the app
  writes, as `vic_scaler`'s). The app shows it with `video_layout_scaled` as the VDC. Lost: extending the copy of `vic_scaler.v` with
  sizes (a second scaler with its own rules; the goal is one rule for all cores).
- **D2:** the work stays in the C128's copies (`cores/c128/mono/host/`, `FROM` updated); `host/` is not touched (copies, not links).
- **D3 (Alex, 2026-11-09):** no JTAG debug master in this build. Before any Vivado build the agent brings the placement proposal
  (what is freed, what is added, estimated Slice/LUT/BRAM, the pblock or placement plan) to Alex and the coordinator; it is built
  only after that.

### §3.1 Design P1-P3 (decided with D10; written in phase 1, 2026-11-09, from the code)

What is in the code today (all in the C128's copies, `cores/c128/mono/host/`):
- `hdmi_out.v` holds `vic_scaler` (EXT = 1, on the timing's counters) and `pix_layer` side by side. `pix_layer` is instantiated with `vclk = sys_clk` and the VIC's
  `pix_color / pix_x / pix_y / v60`, so **its still capture already runs in this build, every VIC frame, continuously**: window 400 x 270 from (96, 16)
  (60 Hz: 400 x 240), 4 bit per pixel, 32 to a 128-bit word, into `STILL_BASE + slot x 64 KB`, the slot that does not hold the last complete frame
  (`last_slot`), `sd_slot / sd_tgl` at the frame's last word, a frame that lost a word is dropped (`torn`). `vic_scaler` takes the same three signals.
- Nothing in the C128 reads the still (`coreapp/cores/c128.cc` has no ring / scrub; `ring.cc`'s STILL_STATUS use is the C64 application's), so the C128's copy may
  change its packing.
- `top.v` gives `hdmi_out` `c64_type = 1`: `mode_req` accepts only the `hm_locked` rows, i.e. 1080p50 / 1080p60. That holds for both pictures today, the VDC too.
- No DFX in this build (`build_mono.tcl`: one flat design, no pb_l1 / pb_l2): a placement constraint change is not needed.

**D4 (proposed) - P1 Source: the existing capture, with the rows aligned.** Changes in the C128's `pix_layer.v` only:
1. Row-aligned packing. 400 pixels are 12.5 words, so an odd row would start in the middle of a word and the layer's line fetch (16-byte aligned) could not
   read it. Each row is padded to 416 pixels: after the row's last pixel (`px_d == X0S + 399`) the shift register takes 16 more pixel events with the value 0
   (a 4-bit AND at its input; the VIC's line has 8 pixels left after the window, the rest runs over into the next line, which is harmless because the next row's
   first pixel is far away). So `k` returns to 0 by itself, a row is 13 words = 208 bytes, a frame is 13 x 270 = 3,510 words (56,160 bytes, fits a 64 KB slot);
   60 Hz 13 x 240 = 3,120 words, rows 232-239 (lines 0-7) first at word 3,016, the frame done at word 3,015 (was 2,900 / 2,899; 50 Hz done at 3,509, was 3,374).
   The hand-off to `ui_clk` is unchanged: one word in flight, words 32 pixel events = 4.06 us apart (measured latency: §5).
2. **Three slots instead of two** (`last_slot` 2 bits, the writer takes `(last + 1) mod 3`, slot n at `STILL_BASE + n x 64 KB`): with two, the capture of frame n+2 overwrites the slot the layer
   is still reading. At 60 Hz it does so on the last 8 rows (lines 0-7 are written first, at line 263 after the completion, and the layer reads row 232 only 244 + t lines after it; any layer
   frame start later than 19 lines after the completion tears the bottom of the picture); at 50 Hz the margin is 42 lines. With three the writer is at least one frame behind the slot in
   display, also while the clocks are not locked.
3. The slots stay at `STILL_BASE` (default 0x05050000, 84 MB: inside the 128 MB DDR2, outside the RISC-V's 64 MB and outside the C128's own use: ROM image
   0x01C00000, RAM 0x01C20000, VDC buffers 0x01D00000-0x01FFFFFF); three slots take 192 KB.
4. `STILL_STATUS` stays (valid, slot, 60 Hz, frame counter) for the benches and the console line.

**D5 (proposed) - P2 Show: format 3 "VIC, 4 bit indexed, follows the capture" in `pix_layer.v`.** `PX_CTRL` b3:1 = 3 (b3:1 is the format field already; 1 and 2
stay as they are). Registers: no new one. Meaning of the set in format 3:
- `PX_SIZE` = {270 or 240, 400}, `PX_STRIDE` = 208, `PX_OSIZE` / `PX_POS` as for any picture (the app computes them), `PX_FRONT` / `PX_BACK` are ignored.
- **Page flip in the gateware**: at the frame start (cy 0, cx 8) the line address `la` is `{c_sbase[31:18], 2'b00, 16'd0} + {fol_slot, 16'd0}` (an OR: `STILL_BASE` is 256 KB aligned) instead of `c_front`, where
  `fol_slot` is `sd_slot` (2 bits, the slot of the last complete frame) two-flop synchronised into `clk`, and `c_sbase` is `STILL_BASE` copied with the other settings
  (the cfg toggle). `STILL_BASE` is masked to 256 KB (bits 17:0 dropped: the default 0x05050000 gives slots at 0x05040000, 0x05050000, 0x05060000). The slot is read once per output frame, so a frame is never
  assembled from two captures.
- **Gate**: the layer is on only after the first complete capture (`sv_seen`, set with the first `sd_tgl`, synchronised into `clk`), so a start never shows the DDR2's
  old content; a Pause (the VIC stops, so does the capture) keeps the last complete frame on the screen.
- Fetch: `rq_n` for format 3 = ceil(W / 32) = 13 words per line; the line memory (512 x 128) holds 13 words per line in both slot layouts (nearest 256-word
  slots, vertical smoothing 128-word slots).
- Pixel chain: `ridx` picks word `ridx[9:5]`, nibble `ridx[4:0]` (the lane registers grow from 3 to 5 bits), `pick()` gets the third case
  `{28'd0, w[4 * lane +: 4]}`; `to888()` becomes the palette lookup in the `ct5 / bs5` stage (edge 5): `td` (a flip-flop) -> 16-entry LUT-RAM read -> mux -> `ct5`,
  no stage is added, the alignment of `vrgb[]` / `fl[]` / `qq[]` is untouched. The palette is a `pal_ram` instance in `pix_layer` (new ports `pal_clk / pal_we /
  pal_addr / pal_wdata`, already wired in `hdmi_out` for the scaler and `c64_text`; the app's `C64_PALETTE` writes reach it with no app change). Smoothing
  (bilinear, `PX_CTRL` b6) works on the looked-up colours: it needs a second read port (the bottom pixel `bs5`), so `pal_ram` gets a second `idx / rgb` pair.
  The colour key is off in format 3 (like format 2). The sub-window case (`set_view`) is not used by the C128.
- The 50/60 Hz difference is only the height in `PX_SIZE`; the gateware does not infer it. At a System Mode change the app writes the new height; for the
  two or three frames until the capture has switched (`mode_ctl` halts the machine and restarts the VIC) the layer shows a frame of the old geometry: black or a
  displaced picture for about 40 ms, once, as the VDC does at a mode change.

**D6 (proposed) - what happens to `vic_scaler`: removed from the C128's build, not kept.** In `hdmi_out.v` (the C128's copy) the first `NO_C64VIDEO` block
(vic_sel = 0, no scaler, `sc_locked = 1`, `x_text = 0`, `align = 0`) becomes `NO_C64VIDEO || NO_VIC_SCALER`, and the C128's build defines `NO_VIC_SCALER`; the freeze text
(`c64_text`) stays. `hdmi_timing` then runs free from the mode's reset (`align` never pulses); the pixel clock is exactly 2,970,000 (2,475,000 at 60 Hz) TMDS clocks per VIC frame
from the same reference (`hm_cdce_lock`, `hm_locked` rows), so the phase between capture and display is constant for as long as the mode runs. `vic_scaler.v` stays in the copy
folder (listed in `FROM`) because `hdmi_out.v` instantiates it in the other branch; the build does not elaborate it. What is lost with it:
the lock of the output frame to the VIC (replaced by the page flip, one frame later), the HDMI "Scan lines" darkening of `vic_scaler` (T62 G09; `pix_layer` has none, see
Q2), the `x_text` calibration register (debug register 1; no consumer in `cores/c128/`, `coreapp/`, `tools/`).
**Phase between capture and display**: with three slots the layer needs no fixed phase, so the removal of the frame lock costs nothing on the locked rows, and a row that is not locked
(720p, 576p, ...) would work as well as far as the picture goes (Q1: whether the C128 should offer them is a separate question; `c64_type = 1` stays as it is).

**D7 (proposed) - P3 App: two pictures in one `Video`.** `Video` (coreapp/src/video.{h,cc}) gets an optional second descriptor, `CoreDesc::alt_video` (a trailing
pointer field, null for every other core; the positional initialisers of the other descriptors are unaffected): `{400, 270, scale 4, ..., bpp = indexed 4 bit, h60 = 240}`.
`Video::select_picture(0|1)` swaps `d_` (and `vw_ / vh_`), picks the height by the 50/60 Hz family of the last `set_mode(hdmi, sys)`, and redoes the layout with the
same `video_layout_scaled` and the same `scale_mode_ / smooth_` as for any picture, so Picture, the HDMI mode and Smoothing reach both pictures through the existing call sites
(`picture.cc`: ~8 calls on `c->video`). `write_set` for the indexed picture writes `PX_CTRL` format 3, `PX_SIZE`, `PX_STRIDE` = 208, `PX_OSIZE`, `PX_POS` and no `PX_FRONT`;
`init()` clears no buffer for it (`nbuf = 0` for the alternative), `poll()` does nothing while it is selected. `c128.cc` `follow_picture()`: `select_picture(mon80 ? 0 : 1)`, then
`show()`; `set_hidden` and the MON_SEL special case go (the layer is never dark now). `op_set_sysmode` needs nothing beyond `picture_set_system()` (which already redoes the layout for the family).

**Expected pictures** (`video_layout_scaled`, 1080p): 50 Hz 400 x 270: Integer 1600 x 1080 (4x), Fit 1600 x 1080, Fill 1920 x 1080; 60 Hz 400 x 240: Integer 1600 x 960 (4x), Fit 1800 x 1080
(today's 4 x 4.5 picture is Fit), Fill 1920 x 1080. Integer at 60 Hz is 120 lines smaller than today's picture; that is the Integer rule, not a regression.

- **D8 (Alex, 2026-11-09, Q1, translated: "everything like the host/the cores"):** the C128 offers all HDMI modes like the other cores. `top.v` ties `c64_type` to 0
  (`mode_req` accepts every `hm_valid` row, `cdce_mode` is not held to the VIC lock), 720p and the other rows are in scope; S2 and S6 cover 720p. What the lock bought
  (the picture in phase with the VIC) is not needed: three slots (§3.1 D4).
- **D9 (Alex, 2026-11-09, Q2, translated: "leave it out"):** no scan lines for the C128's 40 columns, and the `x_text` calibration register goes with `vic_scaler`.
- **D10 (Alex, 2026-11-09, Q3, translated: "Yes, option A"):** option A of §5: indexed 4 bit through the palette in the pixel layer (format 3), smoothing included, three slots,
  `vic_scaler` out of the C128's build.
- **D11 (Alex, 2026-11-09, via T140 §5 item 18, translated: "Yes, if it fits, that would be the cleanest, I think"):** Auto of "Picture shown" also detects which chip shows a
  picture. The gateware part is `c128_act.v` and the registers 0D0-0D4 of the C128's page (§3.2), built with T194; the app's rule is the other agent's (T140 §5 item 18).
- **D12 (Alex, 2026-11-09, replaces the Auto value of D11; board: PETSCII Robots stayed on the 80-column picture after "WAKING UP THE ROBOTS"):** a real C128
  has two outputs and the monitor's switch, so the menu offers what that is (translated: "40 / 80, Auto on/off, time to trigger autosync, and a key to switch the mode
  as on the C128, configurable as a hotkey"; "with T192 this should all go through the app now"). Settings: **Picture shown** 40 (VIC) / 80 (VDC) = the start
  picture; **Auto switch** on/off; **Auto switch time** (default 1 s) . With Auto on, the app watches the picture not shown: it switches when the other chip shows
  activity and the shown one has been quiet for the switch time. Activity: VIC = a hash of the captured VIC picture in DDR2 (sampled, app) or colour RAM /
  register writes above the KERNAL's idle 23 per frame; VDC = the CPU's writes to its data register (its hardware cursor blinks, so no hash). A core hotkey
  (T181) flips the shown picture as the 40/80 key does by hand. App only (no Vivado); the .cor is repacked.

### §3.2 Which chip is being used: `c128_act.v`, registers 0D0-0D4 (D11)

Programs such as PETSCII Robots C128 drive the VIC without touching the KERNAL's `$D7` flag; after an 80-column start the music runs and the VDC still shows the banner. The app
cannot see that in RAM, so the gateware counts, per VIC frame, what the CPU does to each chip. **Choice (and why):** the CPU's *writes*, not the pictures. The VDC's picture changes
by itself (its hardware cursor blinks), so a changing VDC picture says nothing about the program; a CPU write to the VDC's data register does. The VIC side is a program's drawing:
writes to its registers ($D000-$D02E: raster IRQs, sprites, scroll, colours) and to the colour RAM ($D800-$DBFF, every text screen a program draws writes it). DEN ($D011 b4)
says whether the VIC shows anything at all. No address compare against the video matrix or bitmap bank (that would be a comparator per bank mode; the colour RAM stands for it).
- `c128_act.v` (in `c64_top_c128.v`, sys_clk): 8-bit saturating counters on the rising edge of the strobes `(!vic_cen && vic_we && !reu_dma_active && !ext_dma_cyc) || fw_we_t`
  (a CPU write to a VIC register, the turbo CPU's posted writes too), `cram_tap_we` (CPU write to the colour RAM) and `!vdc_cen && vdc_we_a && vdc_rs` (VDC data register $D601);
  when the VIC's raster line wraps to 0 the counts go to the latches, DEN is sampled, the frame number counts, the counters restart. Held machine (Freeze, Pause): nothing moves.
- Registers (page E, read only, `c128_regs.v`; `coreapp/cores/c128_regs.h` names them): `0D0 ACT_VIC`, `0D1 ACT_CRAM`, `0D2 ACT_VDC` (counts of the last complete VIC frame, saturating
  at 255), `0D3 ACT_FLAGS` b0 = DEN at the end of that frame, `0D4 ACT_SEQ` (the frame number, wraps; read it before and after the others: the same value means they belong together).
  Crossed into the bus clock with the other status words (two samples agree).
- Bench: `c128_act` (`tb_act.v`: counts, saturation, the strobe lasting several clocks counts once, DEN, frame number, a write in the clock of the frame edge belongs to the new frame)
  and `c128_regs` (the five words read back through the page).

## §4 Out

- The host's `vic_scaler.v` and the C64 cores (frozen host; C64 cores untouched).
- HDMI "Scan lines" and the `x_text` calibration register for the 40-column picture (D9).
- The frame lock of the old scaler: the 40-column picture runs one frame behind, as the Amiga's.

## §5 Placement and benches (option A decided, D10; the numbers are estimates until the CI's Vivado run of large:c128)

**Baseline** (the last C128 build of the `c128-vicglitch` line, cache `multicore/bit/mono-c128-849d7fb6075d9382`, `from` baf37fb2, 2026-11-08 10:55, post-route): Slice LUTs 27,109 of 32,600,
Slice registers 26,704, Slice 8,122 of 8,150 (99.66 %), Block RAM 72.5 of 75 tiles (RAMB36 60, RAMB18 25), DSP 30, WNS +0.228. Reference of what the flat design can close:
`mono-c128-ba3efccaaa95f133` (`from` 76538a38b, the `dbg-jtag` branch with the JTAG debug master): 27,759 LUT, 29,777 FF, **Slice 8,150 (100.00 %)**, BRAM 74, route complete, WNS +0.109 -
the placer packs past 99.7 % when it must; the hard limit is the block RAM (T113 §5: "The limit is block RAM, not LUT and not routing").
There is no pblock in this build (`build_mono.tcl`: one flat design), so no placement-constraint change belongs to any option.

**What leaves** (measured, hierarchy report of the baseline, row `hout/scaler` = `vic_scaler`, `hout/(hout)` the select muxes): `vic_scaler` 85 LUT (74 logic, 11 SRL; its `pal_ram` and
the ring's read mux are in it), 120 FF, **4 RAMB36** (the 32K x 4 ring), 0 DSP. The select muxes `vic_sel ? v_* : t_*` (24 + 12 + 11 + 3 bits) and the `align` / `sc_locked` logic go with it:
at most 40 LUT, inside the 23 LUT the `(hout)` row shows, i.e. in the noise.

**What comes** (estimates by structure, not measured; LUT6 = 4:1 mux per LUT; the only measured numbers are the ones above and `pxl` = 1,392 LUT / 2,324 FF / 2 RAMB36 / 12 DSP):

| Change | LUT | FF | BRAM | Basis |
|---|---|---|---|---|
| nibble pick (4 bit out of 128, 32:1) for the top and the bottom pixel, third format in `pick()`, lane registers 3 -> 5 bit | +100 | +20 | 0 | 2 x 4 bit x ~11 LUT (32:1) + widening of the 32-bit format mux |
| palette in the pixel layer, two read ports (top / bottom pixel), write decode | +54 | 0 | 0 | `pal_ram` 16 x 24 as RAM32X1D = 2 LUT x 24 bits; +6 decode (the scaler's one is counted in its 85) |
| page flip: 2-bit `fol_slot` and valid synchronisers, `c_sbase` copy (14 bit), address mux at the frame start | +25 | +45 | 0 | counted from the signals of §3.1 D5 |
| capture: 3 slots, 416-pixel rows (pad counter, AND at the shift input), wider word counters | +25 | +12 | 0 | `widx` stays 12 bit (3,510 < 4,096), pad counter 5 bit, `last_slot` 2 bit and its modulo-3 step |
| format 3 control (fetch count, `ridx` word/lane split, smoothing enable, `en_new` gate) | +20 | +15 | 0 | |
| **Added, gross** | **about +225** (range 160-300) | **about +90** | **0** | |
| `c128_act.v` (three 8-bit saturating counters, three latches, frame number, edge detectors; D11) | +35 | +60 | 0 | counted from the module |
| the five activity bytes across into the bus clock in `c128_regs.v` (two samples and a held copy of 40 bits, read mux) | +12 | +120 | 0 | as `fr_min` / `ram_cnt` there |
| **Added, gross, with D11** | **about +272** (range 200-350) | **about +270** | **0** | |
| **Net with `vic_scaler` gone** | **about +187** (range +115 .. +265) | **about +150** | **-4 tiles (72.5 -> 68.5)** | |

Result (estimate): about 27,300 LUT (83.7 %), 26,850 FF, 68.5 BRAM tiles; the Slice count follows the LUT count by about 4 LUT per slice at best, i.e. up to +35 slices on a design that
is at 99.7 %: it has to pack as the `dbg-jtag` build did (+650 LUT, +3,073 FF, 100.00 % Slice, closed). Timing: the palette read sits between the flip-flop `td` and `ct5` (one LUT-RAM read and a
mux, no stage added to the chain, whose alignment of `vrgb[] / fl[] / qq[]` stays); the pixel chain is the clock that carries the 12 DSPs and the bilinear blend, where the baseline WNS +0.228 is
the margin. The capture and the flip are on `vclk` (31.5 MHz) and `ui_clk` (75 MHz), slow. Risk, in order: (1) the pixel chain's timing through the nibble pick (a second register stage
for the pick is the fix, 1 clock more latency in `fl[] / qq[]`); (2) Slice packing at 99.7 %; (3) nothing in routing (no DFX, no pblock).

**DDR2** (peak 600 MB/s: 16 B per 26.7 ns, OPT-ddr2 §7): the capture writes 3,510 x 16 B = 56 KB per frame, 2.8 MB/s at 50 Hz (3.0 at 60 Hz); the layer reads each source line once, the same 56 KB,
2.8 (3.0) MB/s. Together 5.6-6 MB/s = 1 % of the peak, next to the VDC (about 30 MB/s for its picture) and the RISC-V; in 40-column mode the VDC's display read is off. The capture's write already waits
in `mig_arb` behind the RISC-V and the REU; the bench `still60` measures how long (below).

**Options**
- **A (recommended): D4-D7 as written** - indexed 4 bit with the palette in the pixel layer, smoothing included, 3 slots, scaler removed. Net about +140 LUT, -4 BRAM tiles.
- **A-: the same without smoothing for format 3** (nearest only; `f_hsm / f_vsm` forced 0, one palette port): -27 LUT against A. Fill at 1080p (4.8x) then has uneven pixel widths and the Smoothing item
  would not act on the 40-column picture. Not recommended: the goal is one rule.
- **B: expand the palette at the capture and use the layer's existing format 2** (32 bit per pixel; the pixel chain unchanged, S2 shrinks to a check of the existing layer). Four pixels a word means a word every
  0.51 us (38 ui_clk) instead of 4.06 us: against the measured 26 ui_clk of write latency, plus the acknowledge's way back to `vclk`, the one-word hand-off would have a margin of only about 20 %, so it needs a small
  FIFO (4 words x 128 bit, LUT-RAM ~ +100 LUT or +256 FF) to be safe; DDR2 traffic is 8x (43 MB/s, 7 %), the slots are 3 x 432 KB. Net about +100 .. +150 LUT, i.e. the same as A, with a new clock crossing
  instead of a longer pixel chain, and without the indexed format Alex proposed (D1). Not recommended, but the closest alternative if the pixel chain's timing fails.
- **C: keep `vic_scaler` and add the path** - not an option: +225 LUT on top of 85 and no BRAM released (72.5 of 75 tiles stay).
- If A does not place: further candidates from the same report (each needs Alex): `c2n` tape recorder 314 LUT (cut in the `p_v16cuttape` / `p_v1ddr2cut` variants of T113), `c64_text` 203 LUT / 489 FF
  (the freeze text; the pixel layer's own menu would then have to carry the Freeze screen), `avout` 378 LUT / 8 DSP (the analogue output of T140: no).

**Bench measurement taken in phase 1** (`host/sim/pixlayer/run_still.sh` with `PIX=cores/c128/mono/host/pix_layer.v`, on the builder in `trx64-sim-env:4`, the existing capture on the C128's unchanged copy,
RISC-V / port / drive / cart_fill at full load); with the changed copy the new bench `c128_vicpic` measures **22 ui_clk (+3)** max (below): PASS, 0 errors (50 Hz, 60 Hz, 50 Hz; 30,931 words, every pixel of the named slot equal to the VIC's). **Write latency max 23 ui_clk (+3 for the crossing) = 347 ns** against the 4.06 us between words (304 ui_clk): the one-word hand-off has a factor 11 of margin as it is, so the longer 416-pixel rows (same word spacing) need nothing more.

**Benches (S1, S2, S3)**
- **S1 `c128_vicpic`** (new: `cores/c128/sim/tb_vicpic.v`, `run_vicpic.sh`, lane `large:c128`; built from `host/sim/pixlayer/tb_still.v`, DUT = the C128's `pix_layer.v`): synthetic VIC (63 x 312, 65 x 263,
  63 x 312 again), the other DDR2 clients at full load; per phase: every pixel of the slot `STILL_STATUS` names equals the VIC's pixel in the window at word `13 y + x / 32`, nibble `x mod 32`, the 16 pad nibbles of each row are 0, rows 232-239 at 60 Hz come from lines 0-7,
  one frame-done per VIC frame, the slot sequence is 0, 1, 2, 0, ... with no slot written twice before the next complete one, the hash of the writes, the write latency against the budget (304 ui_clk = 4.06 us).
- **S2 `c128_vicshow`** (`tb_vicshow.v`, `run_vicshow.sh`, `job_vicshow.sh`; the plan and the check are `host/sim/pixscale/model.py`'s, called with format 2 on the VIC window through the Colodore palette): the VIC capture
  runs, the layer is in format 3; for 1080p and 720p at 50 Hz (400 x 270) and 60 Hz (400 x 240): Fill, Fit, Integer and 3/4 (pixels and lines skipped), nearest exact and smoothed within 1 LSB, alpha 200 on one step,
  no late line, nothing wrong outside the layer. **`c128_vicpic_tear`** (`tb_vicpic_tear.v`): the VIC's pattern changes every frame; every frame the layer puts out is one whole VIC frame (no tear), the shown frames
  change; a small display mode so that a run takes seconds; the display's frame time equal to the VIC's at four starting phases and +-10 % (a row that is not locked: the phase runs through a whole frame every ten frames), 50 and 60 Hz. The layer staying off before the first
  complete capture is the gate of D5 (read from the code; the benches start the layer after two VIC frames).
- **S3** `tests/coreapp/test_c128.cc` (extended) and `test_scale.cc` (a two-picture core in its table): the `PxPort` recorder sees, for the C128 in 40 columns, `PX_CTRL` format 3, `PX_SIZE` {270 | 240, 400}, `PX_STRIDE` 208, and `PX_OSIZE` / `PX_POS` equal to the
  table of "Expected pictures" for Integer / Fit / Fill at 1080p 50 and 60 and for 720p; the Picture and Smoothing items change them; switching to 80 columns writes exactly the registers it writes today (golden), and back. No rendered screen changes (no new row, page or text), so no UX
  review is due; if one is added, `ux-review` runs. Built for RISC-V in `u64-build-env:ci`, `ci/apptests.sh`.
- **S5** `tools/benches.sh c128` (all 24 + S1 + S2). **S4** one Vivado run after the decision; **S6** on the board.

**Decided by Alex (2026-11-09):** Q1 all HDMI modes like the other cores (D8, `c64_type` 0), Q2 no scan lines (D9), Q3 option A (D10); the activity registers are in (D11, §3.2).

## §5.1 Decisions taken while building

- **Benches (2026-11-09, builder, `tools/benches.sh c128`, HEAD with the merge of main):** all 28 `large:c128` benches PASS, incl. `c128_vicpic` (S1, 1,561 s), `c128_vicshow` (S2, 1080p50/60 and 720p50/60, 2,351 s),
  `c128_vicpic_tear` (1,356 s), `c128_act` and `c128_regs` (S7), `c128_vicbus` (S5). `c128_scan` failed until `c128_act.v` was named in `PROVENANCE.md` (fixed). The tear bench had been given the VIC frame time in
  ns for a clock in ps (display clock 1000x too fast, runs of hours); fixed to ps (19.95 ms / 17.35 ms).
- **App (S3):** `dev.sh test` green (test_c128, test_c128pic, scale), `dev.sh riscv` green. Merged `main` (T140 18, the `$D7` Auto): the layer is never dark now, so test_c128 checks `picture()` instead of `!visible()`.
- **D8:** `top.v` ties `c64_type` to 0; the hdmi_out copy builds no VIC path under `NO_VIC_SCALER`; S2 runs 1080p and 720p. Built.
- **D11, the app side (`coreapp/cores/c128.cc`):** `act_poll()` runs in the Auto service (20 ms): a new `ACT_SEQ` (read before and after the other four words) pushes one frame (VIC = registers + colour RAM, VDC, DEN) into a window of 8 frames;
  VIC sum is taken as 0 when DEN was 0 in all of them; only VDC > 0 -> verdict VDC, only VIC > 0 -> verdict VIC, else none; a verdict counts after two frames in a row agree (`act_stable`) and is what `auto_decide` gets as `activity`
  (it wins over `$D7`; none -> `$D7` decides; C64 / Z80 mode unchanged). Cleared at reset and at a fixed Picture value. Test: `test_c128.cc` (verdict needs two frames, both = `$D7`, window leaves, DEN 0, colour RAM, repeated sequence number, fixed value).
  **Not calibrated:** whether an idle BASIC in 80 columns writes the VIC's registers or colour RAM (then the verdict would be VIC) is a board question; the rule's thresholds are 0 / not 0. S6 includes it.
- **D11, the app side above is replaced by D12 (below):** the 8-frame window, the verdict and the `$D7` rule are gone from `c128.cc`; the gateware words and `ACT_VIC_IDLE` (40) stay.
- **Area, D11:** no Vivado here; the estimate stays §5 (D11 rows: about +47 LUT, +180 FF, 0 BRAM), the measure is the CI run of large:c128.

- **D11 fix after the board (2026-11-09, T140 §5 item 18, measurement there):** `c128_act.v` ends its frame at the VIC's raster-line wrap to 0 (port `line`, was the output `vsync`: 44.8 ms in the sim, not 20 ms), `tb_act.v` follows (`c128_act` bench PASS); idle READY in 80 columns measures 23 VIC writes per frame, so `ACT_VIC_IDLE` is 40 (was 16, which flipped the idle BASIC to the VIC). Strobes count once per CPU write. Tests: `test_c128.cc` (idle 23 and bursts to 39 keep 80 columns, a PETSCII-Robots-like frame gives the VIC). Not measured: typing. Board pass and the CI run of large:c128 open.

- **D12, built (2026-11-09, app only, branch `c128-scale`, `c128.cc` + `hotkeys.{h,cc}`):** proven on the board, not by sims (Alex, 2026-11-09, translated: "no benches, no sims .. just build. I test it directly on the board"); `dev.sh test` and `dev.sh riscv` green, `test_c128.cc` (native, the board's functions fed with the register words and capture bytes), no ue2emu render check by his word.
  - Settings: **Picture shown** = 40 columns (VIC-IIe) / 80 columns (VDC), default 40 (the start picture and the picture after a Reset; matches the 40/80 DISPLAY default); a `.cfg` with the old label `Auto` is not in the list, so the item falls back to its default (40) and **Auto switch** (Off/On, default On) behaves as the old Auto did (starts at 40, follows the chip in use); **Auto switch time** 0.5 / 1 / 2 / 3 s (default 1 s). Golden `menu_c128.txt` updated (3 rows).
  - Rule (`auto_follow`): per service (20 ms) `act_poll` stamps VIC activity (colour RAM writes or register writes above `ACT_VIC_IDLE`, while DEN) and VDC activity (data register writes) with `now_ms`; `act_hash` stamps VIC activity when the hash of the capture changes. Switch when the other chip's last activity is after the last change of the picture (`sw_ms`: start, Auto, hotkey, monitor key) and the shown chip has been quiet for the switch time (its last activity, or `sw_ms` if later). Both active = the shown one is never quiet = no flip. Hotkey and monitor key are changes by hand; Auto keeps watching. C64 mode: VIC once; Z80 mode: leave.
  - Hotkey: `HkSpec` got `core_op / core_key / core_label` and the action `HKA_CORE` (T181; no core had an own function before, so this is new generic code; maps of all cores get `HK_NONE` there, the F8 item exists only where `core_op` is set). C128: **CTRL + D** ("Hotkey: picture 40 / 80", op `c128.toggle_picture`), configurable like the others.
  - Capture read: the app's CPU cannot address `STILL_BASE`'s default 0x05050000 (window = 0x01000000-0x01FFFFFF), so `c128_release` writes `STILL_BASE` = 0x01CC0000 (256 KB aligned, between the RAM at 0x01C20000+128 KB and the VDC buffers at 0x01D00000; slot n at + n x 0x10000, 56,160 bytes each, so the three end at 0x01CEDB60) through the register (byte 3 commits; the layer takes it at the next frame start, the writer at its next config toggle). A hash read that the capture ring overtakes (the frame counter moved by 2 or more during the read) is dropped, not counted as activity. The slot comes from `STILL_STATUS` (0x6068). This is a gateware-visible change of the C128's capture address: board check that the VIC picture still shows.
  - Hash: every 100 ms, every 4th row of the capture (68 of 270 rows x 208 bytes = 14 KB, FNV over 32 bit words), skipped when the frame counter did not move. RISC-V cost **not measured**: an estimate of 3.5 K word loads per sample from the uncached window at roughly 20-40 cycles each is about 0.1-0.15 M cycles, i.e. a few ms per sample and about 2-5 % of the RISC-V at 100 ms; measure on the board with the console timer if it matters.
  - **Board 2026-11-09 (Alex, D12 build f1a1107e):** CTRL + D flips the picture (good). Auto switch did not: PETSCII Robots' title (VIC: full
    picture, "LOADING" bar) came up on the 40-column screen while the 80-column picture stayed. Alex's rule (translated): "1 change within a few ms is
    enough, only when the active one has done nothing for 1 s, then" - open, to reproduce at leisure; the merge goes ahead without it.
  - **Auto miss, diagnostic (2026-11-10, branch `c128-autofix`, app only):** read from the code, no certain fault found: `act_reset` runs only at start, reset, Auto/picture setting changes and switches (not periodically); the VDC counter is the data register only (`c128_act.v` `vdc_wr`); the capture is read from the uncached window (the D-cache covers addresses below 0x00EA0000, T133 D3). The suspect left is the rule's input: any VDC data write in a frame, a game's or the KERNAL's IRQ (cursor), keeps the VDC "not quiet" so `oth=1` but `quiet=0` forever. The new read-only state `c128.auto_diag` shows it live. Read it while the title is on screen: `curl -s "http://<board>/v1/state/c128.auto_diag?t=$(cat ui.token)"` (token: `/SD/cores/c128/ui.token`, or header `Authorization: Bearer <token>`). Fields: `shown` (0 VIC, 1 VDC), `auto`, `st` (C128_STATUS), `sw` (switch ms), `since_sw`, `vic_age` / `vdc_age` (ms since last activity, -1 none), `quiet` (shown chip quiet for sw), `oth` (other active after the last change), `dec` (-2 leave, 0/1 picture), `last(vic cram vdc fl seq)` the last frame's words, `fr` frames taken, `frv` frames with VDC writes, `mv` / `md` VIC / VDC marks, `hash(ok v hf hc hc_age hs drop nonew inv)` (value, frame counter, changes and their age, samples, ring-overtaken drops, no-new-frame skips, no-capture skips), `still` raw STILL_STATUS. Reading it: `quiet=0` with `vdc_age` always below `sw` = the VDC is written (hypothesis a); `oth=0` with `hc=0` and `hs` growing = the capture does not change; `hs=0` with `inv` or `nonew` growing = no capture / frames; `drop` growing = the read is overtaken. Test: `test_c128.cc` (the Robots sequence read through the state). No rule changed.
  - Idle cursor question (does an idle 80-column READY change the VIC capture?): reasoned from the code only, **board check**. The sampled rows are the VIC capture, which in 80 columns shows the screen the KERNAL leaves for the VIC; the hardware cursor blink is the VDC's. If the capture does change (e.g. the KERNAL blinks the 40-column cursor in 80 mode), the symptom is an idle 80-column READY flipping to the VIC one switch time after boot; the fix would be to drop the hash while the VDC is shown and keep only the writes.
  - Tests (`test_c128.cc`, "T194 D12"): defaults and read-back of the three settings and the old `Auto` label; idle 80 stays (23 and 39 writes, VDC written); the Robots sequence (VDC text, then the game draws: still 80 at 0.9 s of VDC quiet, VIC at 1.1 s, stays); picture change alone switches, an unchanged picture does not; both active over 20 s does not flicker, in both directions; back to the VDC; Auto off never switches; switch time 0.5 s / 3 s; the hotkey flips (also with Auto off) and Auto goes on watching; F8 item and default key (CTRL + D) and its mask bit; Z80 leaves, C64 mode shows the VIC. `test_hotkeys.cc` rows check that cores without `core_op` keep `HK_NONE` there.
