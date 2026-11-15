# T222 — The C64 + SuperCPU core respects the HDMI resolutions: 576p, 720p, 1080p at 50 Hz; 480p, 720p, 1080p, 800 x 600 at 60 Hz, each locked to the VIC frame, at Integer / Fit / Fill

**Status:** IN PROGRESS (2026-11-15, Alex): gateware and app built, benches S1-S6 green on the builder (§5); open: the Vivado run (S9), the app tests in CI (S7), the board (S11). Branch `scpu-hdmi-modes`, not pushed.
**Repo:** TRX64U-MultiCore — the core `cores/c64scpu` (its own copy of the host video files `mono/host/`, `mono/top.v`), the generic core app (`coreapp/src/picture.*`, `app/video/videomode.*`), CI (`ci/sims.*`). `host/`, `cores/common/`, `contract/` are not touched.
**Number:** T222 (registry: the table in `TODO.md`; T221 was the last row).
**Origin:** Alex, 2026-11-15 (translated): "the SuperCPU is done; what is missing is respecting the resolutions, TRX64-Ultimate just built that."
**Depends on:** TRX64-Ultimate U16 (branch `u16`, gateware unchanged since `d48734d8`, head `422f9dce`; spec `docs/specs/U16-hdmi-modes.md` D1-D8) as the source of the files. T182 (the core), T164 (the host's HDMI path this core copies).
**Milestone:** M2.

---

## §1 The problem

c64scpu shows a picture at 1080p only. A screen that does not take 1080p (the projector of U16's origin) shows nothing from this core, and the app's Picture row offers modes the gateware then refuses.

### Read from the code (2026-11-15)

- The video path of `cores/c64scpu/mono/top.v` is `hdmi_out #(.SMOOTH(1)) hout (... .c64_type(1'b1) ...)` (`top.v:1401`): the copy `mono/host/hdmi_out.v` of the host's module (T164 WP8). `c64_type = 1` makes `mode_req.v` (`:53`, `ok = in_rng && hm_locked(c_row)`) accept only the rows `hm_locked` marks: the two 1080p rows (`mono/host/hdmi_modes.vh`). Every other request is refused; `hdmi_out.v` shows hdmi_timing's black picture on every row but those (`vic_sel = vic_row`).
- The VIC path is `mono/host/vic_scaler.v` with `EXT = 1`: the old 1080p-only scaler (4x, 4.5x at 60 Hz, 1600 x 1080 window) riding on hdmi_timing's counters, align as a one-clock pulse, `en` = the row is locked.
- It is the same scaler U16 started from: our `vic_scaler.v` differs from TRX64-Ultimate's pre-U16 one only by the EXT/`en`/`relock` generate; `hdmi_timing.v`, `cdce_mode.v`, `mode_req.v`, `tmds_rep.v`, `hdmi_enc/` are the files TRX64-Ultimate imported from here (their headers say so). So the port is a delta, not a new path.
- The CDCE input is already the one U16's `hm_cdce_lock` bytes assume: `mmcm_ref` makes 37.894 MHz = 125/104 of dot4x (`top.v:213-214`, `CLKFBOUT_MULT_F 31.25 / CLKOUT0_DIVIDE_F 26`). The VIC raster constants of our scaler (X0 96, Y0 16, Y0_60 31) equal U16's.
- Menu layers already follow the active area: `overlay.v` is byte-identical to U16's (but for the `$readmemh` path), `pix_layer.v` takes `hact / vact / xact0 / yact0 / ce`. `c64_text.v` is the one layer that differs: ours derives its geometry from `hact / vact` (whole-number step 1-4), U16's takes `hm_geom` (fractional steps, follows the scale).
- The app: the Picture row writes the row at 0x4F00 for every core (`app/video/videomode.cc:218`, `video_gw_apply_io`); the Scale and Smoothing rows exist only for frame-buffer cores (`coreapp/src/picture.cc:62`, `scaled = g_gw && d->video.w != 0`), c64scpu has `video.w = 0`. Nothing writes 0x4F01.
- Last green flat build in the builder cache (`mono-c64scpu-f11764a1`, from `60bfda79`, `scpu-ba-hold` / main): Slice LUTs 22,977 of 32,600 (70.48 %), registers 22,940, **slices 7,881 of 8,150 (96.70 %)**, block RAM tiles 58.5 of 75, DSP 30, WNS +0.082 ns, 0 routing errors.
- U16's cost on its c64u build, measured by TRX64-Ultimate (run 1924): LUT +691, FF +477, BRAM +0, slices 8,141 -> 8,147 of 8,150, WNS +0.018 -> +0.109. That figure includes mode_req, cdce_mode, hdmi_timing and tmds_rep, which this core already has; ours is the smaller delta (scaler, text geometry, scale register, the new table).

### Parts

- **P1** The timing: every row of U16 D2 runs frame-locked (hdmi_timing, cdce_mode with `hm_cdce_lock`, the table).
- **P2** The request: 0x4F00 accepts the seven rows, 0x4F01 the scale, 0x4F0A reads it; a System Mode switch keeps a valid row.
- **P3** The picture: the VIC window scaled to the active area at Integer / Fit / Fill, frame-locked, in every row.
- **P4** The layers over the picture (menu, pixel layer, freeze text) are placed in every row and scale.
- **P5** The encoder and the transmitters: gear 1, 2, 3, 5, the audio clock rate, the AVI InfoFrame, data islands in the shortest blanking.
- **P6** The application: the Picture row's modes work, a Scale row exists, rows the gateware does not lock are mapped.
- **P7** Area and timing: the core still routes (slices 96.7 % today) and keeps WNS >= 0.
- **P8** The rest of the core is untouched (the other benches stay green, `host_pin` check green).

## §2 Scenarios

Rows: 576p50, 720p50, 1080p50 (hm 0, 1, 2) and 480p60 (800 x 526), 720p60, 1080p60, 800x600@60 (hm 6, 7, 8, 9). Scales: Integer, Fit, Fill.

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 | Given each of the 7 rows (and the refused ones: 3, 4, 5, 10, 11, 12..15), when hdmi_timing takes it, then clocks per frame, active area, sync positions and polarity, gear, `htot_m1` / `vtot_m1`, and the frame length against the VIC frame are exactly the table's; align on a locked row restarts at the active origin | `cores/c64scpu/sim/hdmi/run_timing.sh` (U16 `tb_hdmi_timing.v`) | `ci/sims.txt` tier 1 |
| S2 | P2 | Given the request path, when 0x4F00 gets a row of either family, 0x4F01 a scale, then the row, the scale (at the next frame start), the counters and 0x4F04-0x4F0A read as U16 D4 says, a refused value changes nothing, a System Mode switch moves the row to the other family (800x600 at 50 Hz -> 576p), the CDCE bytes of every row are `hm_cdce_lock` | `run_modereq.sh` (U16 `tb_mode_req.v`) | tier 1 |
| S3 | P5 | Given gear 1, 2, 3, 5, when tmds_rep repeats each bit, then the GTP words decode back to the symbols | `run_tmds.sh` (U16 `tb_tmds_rep.v`) | tier 1 |
| S4 | P3 | Given each row at each scale (21 runs), two frames after the lock, when vic_scaler maps the ring, then every output pixel shows the VIC pixel the geometry says, the window sits where `hm_geom` puts it, a scale change re-aligns once, the palette and the scan lines work; 3 mutants are caught | `run_scaler.sh` (U16 `tb_scaler.v`) | tier 1 |
| S5 | P4 | Given each row at each scale, when c64_text draws the freeze text, then its area starts at `xact0 + xt0`, steps by `hn/hd` and `tvn/tvd`; the mutant is caught | `run_text.sh` (U16 `tb_text_geom.v`) | tier 1 |
| S6 | P3 P4 P5 | Given `hdmi_out.v` (our copy), when 1080p50 and 576p50 are requested, then the GTP words decode to the C64 picture, the InfoFrames carry the row's VIC code, aspect and the clock rate of the System Mode; the VIC lock comes | `run_hdmi_out.sh` (host's `tb_hdmi_out.v` adapted: the VIC path in every row) | tier 2 |
| S7 | P6 | Given a gateware that answers version 2 and the C64-type flag, when the app builds the settings, then the core has Picture and Scale, Scale writes 0x4F01, 1024 x 768 / 1280 x 1024 go out as 800 x 600 (50 Hz: 576p); an old host shows neither | native app test `tests/coreapp` (pure functions: mapping, extend; CI only, T174 D1 note) | CI apptests |
| S8 | P6 | The core app builds for the board | `tools/dev.sh riscv` | CI / builder |
| S9 | P7 | The flat build routes, WNS >= 0, slices below 100 % | report lines `Slice`, `WNS` of stage `bitstream` | CI `vivado large:c64scpu` |
| S10 | P8 | `tools/benches.sh c64scpu -f` all green; `tools/host_pin.sh --check` green; yosys lint of `top.v` has no implicit net | those commands | builder / Mac |
| S11 | P1-P6 | **Board (Alex):** each of the 7 rows, each scale, at 50 and 60 Hz System Mode, a stable picture and menu on a screen; 1080p unchanged against today; freeze menu text where the C64 window is; audio present | board session, photos | by hand, bundled with the board list |

Negative controls: only the ones inside the ported benches (scaler, text). No flash or persistence operation here.

## §3 Decision

- **D1 Port as Alex asked, every row and scale.** The files come from TRX64-Ultimate `u16` `d48734d8` as our own copies in `cores/c64scpu/mono/host/` (copies, never links; no file of `host/`, `cores/common/`, `contract/` changes): `hdmi_modes.vh` (the 7 rows, `hm_geom`, `hm_align`, `hm_rate_alt`; our 9-row frame-buffer table goes), `hdmi_timing.v` (adds `htot_m1`, `vtot_m1`; `align` becomes a level held until a `ce`), `cdce_mode.v` (always `hm_cdce_lock`, no `lock` port), `mode_req.v` (adds 0x4F01 scale, `fstart`, `scale_pix`, 0x4F0A; the family fallback), `vic_scaler.v` (rewritten: 64-line ring, fractional accumulators), `c64_text.v` (`hm_geom` geometry), `hdmi_enc/hdmi.v` (one registered compare, T181). `tmds_rep.v`, `overlay.v`, `pix_layer.v`, `hdmi_gtp.v`, `mode_ctl.v` stay (identical or comment-only differences). Provenance in `mono/host/FROM`.
- **D2 `hdmi_out.v` and `top.v` are edited by us.** `hdmi_out.v` (this core's copy): the VIC path in every row (no `en`, no `vic_sel`, no black-picture mux, no `relock`: this flat core has no core swap), `c64_type` is constant 1 inside `mode_req` (the port goes), `scale_pix` / `fstart` / `geom` / `htot_m1` wired as U16's `top.v:1514-1553`, a `vmode` input for the encoder's clock rate (`hm_rate` in a family's first System Mode, `hm_rate_alt` in its second, as U16 `top.v:1620-1635`); `mono/top.v` passes `vmode` (and `top.v.diff` is updated).
- **D3 Version 2 at 0x4F05.** Our `mode_req` reports version 2 (U16 keeps 1: its register file has the scale but the host's generic registers do not). The generic app reads version >= 2 with the C64-type flag (0x4F06 bit 4) as "this gateware has the VIC scaler with a scale register"; the host's own `mode_req` (all other cores) stays version 1, so no other core shows a Scale row it cannot use.
- **D4 App.** `picture_env_read` reads version and flag; with both, `picture_extend` adds a Scale row (Integer / Fit / Fill, default Fit as the gateware powers up) for a core whose picture is the VIC path (`video.w == 0`), no Smoothing row; `scale_apply` for such a core writes 0x4F01 (new `video_gw_set_scale` in `app/video/videomode.*`, through `GwIo` like the request). `video_gw_apply_io` maps a row the gateware does not lock to the one it takes: hdmi 4 and 5 (1024 x 768, 1280 x 1024) -> 800 x 600 at 60 Hz, hdmi 3..5 at 50 Hz -> 576p (U16 D5), only when the flag and version say so. Generic code otherwise unchanged.
- **D5 Benches are our own copies** in `cores/c64scpu/sim/hdmi/` of U16's `tb_hdmi_timing`, `tb_mode_req`, `tb_tmds_rep`, `tb_scaler`, `tb_text_geom` (their `run.sh` pointed at `mono/host/`), plus the host's `tb_hdmi_out` adapted; registered in `ci/sims.txt`, `ci/sims.lane` (`large:c64scpu`), `ci/sims.deps`.
- **D6 Budget.** Estimate in §5; if the build does not route, the first lever is dropping rows (an open question for Alex), not the picture quality.

## §4 Out

Nothing decided out yet. Not part of this spec by nature: 1024 x 768 and 1280 x 1024 as real rows (U16 D2: they do not lock), a frame buffer (U16 D1), the host's (all other cores') HDMI path.

## Open questions for Alex

None yet. The first can come from the Vivado run (§5 area): if the flat build does not route at 96.7 % slices plus the delta, the choice is which rows to drop (D6).

## §5 Decisions taken while building

**Read-compare-plan (2026-11-15).** The c64scpu copy of the host video path is the T164 host's (hdmi_out + hdmi_timing / mode_req / cdce_mode / tmds_rep / vic_scaler EXT=1 / overlay / pix_layer / c64_text / hdmi_enc), the same lineage U16 imported. Against U16 `d48734d8`:

| File (mono/host) | Ours before | U16 | Taken |
|---|---|---|---|
| `hdmi_modes.vh` | 9 rows, 1080p locked, `hm_cdce` + `hm_cdce_lock` | 7 locked rows, `hm_geom`, `hm_align`, `hm_rate_alt`, row 6 vfp 11 | U16's (rows 10, 11 gone) |
| `hdmi_timing.v` | `align` pulse, no `htot_m1` / `vtot_m1` ports | `align` level, `htot_m1`, `vtot_m1` outputs | U16's |
| `cdce_mode.v` | `lock` port picks `hm_cdce` / `hm_cdce_lock` | always `hm_cdce_lock` | U16's |
| `mode_req.v` | `c64_type` port, 1080p only locked | constant C64 type, scale 0x4F01 / 0x4F0A, `fstart`, `scale_pix`, family fallback | U16's, version byte 2 (D3) |
| `vic_scaler.v` | EXT counters, 1600 x 1080 only, `en`, `relock`, `v60` | rewritten: ring of 64 lines, fractional accumulators, `geom`, `align` level | U16's |
| `c64_text.v` | whole-number step from `hact / vact` | `hm_geom` step and origin | U16's |
| `hdmi_enc/hdmi.v` | `cy == ey_e - 1` | registered `ey_m1` (T181) | the three lines edited in (names stay `trx_*`) |
| `hdmi_out.v` | `c64_type`, `core_decouple`, picture mux, `en`, `relock` | (U16's `top.v` wires the same modules) | ours, edited (D2) |
| `overlay.v`, `pix_layer.v`, `tmds_rep.v`, `hdmi_gtp.v`, `mode_ctl.v` | same | same (comments only) | unchanged |
| `mono/top.v` | `.c64_type(1'b1)`, `.core_decouple` | - | `.vmode(vmode)`, the two ports gone; `top.v.diff` regenerated |

Consumers read: `x_text` (still driven by the scaler: calibration, unchanged semantics), `scanlines` (`scan`), `blank = c64_halted`, `pix_*` raster (the same X0 / Y0 / Y0_60 constants as ours), the freeze text (`xt0 yt0 hn hd tvn tvd` from `geom`), `fam_v60` (mode_ctl), `i2c_done` (mode_ctl step 5), the audio clock rate (`vmode`). `pix_layer` still takes `v60` and the raster for the still capture; nothing there changed.

**Area (estimate, then measurement).** Estimate from a yosys `synth_xilinx` of the old and new modules alone (relative only, not the Vivado number): `vic_scaler` +426 LUT +231 FF, `mode_req` +18 / +42, `hdmi_timing` -84 / -6, `c64_text` +-0, block RAM +0 (the ring is 12 RAMB36 in both). About +360 LUT, +270 FF, 0 BRAM, 0 DSP: from 22,977 LUT (70.48 %) to about 23,340 (71.6 %), registers 22,940 -> about 23,210. U16's own cost (+691 LUT, +477 FF) was higher because it also added the modules this core already has. Slices are the limit: 7,881 of 8,150 now (96.7 %); +360 LUT / +270 FF is about +90..130 slices if packing stays as it is, so about 7,970-8,010 (98 %), which fits the device arithmetically; TRX64-Ultimate's build routed at 8,147 / 8,150. **The decisive number is the Vivado run (S9), not this estimate**; it is open.

**Benches (builder, `tools/benches.sh`, `-f`, 2026-11-15, commit of this branch):**
- S1 `c64scpu_hdmi_timing` PASS (7 rows exact, refused rows, align restarts every row), 380 s.
- S2 `c64scpu_mode_req` PASS (path, I2C phase, scale phase; after `tb_mode_req` was changed to expect version 2), 370 s.
- S3 `c64scpu_tmds_rep` PASS (gears 1 2 3 5 3, 3000 pixels, 4 lanes exact), 9 s.
- S4 `c64scpu_scaler` PASS: "7 rows x 3 scales, 4 scale changes, palette and scan lines, 3 controls caught" (also run on the Mac: the same lines); 1,386 s. One earlier builder run of this bench reported "exit 1" with its log lost (the helper deletes the log of a run that exits 0); the repeat and the Mac run are green.
- S5 `c64scpu_text_geom` PASS (7 rows x 3 scales, control caught), 1,579 s.
- S6 `c64scpu_hdmi_out` PASS (1080p50 and 576p50, the GTP words decoded: geometry, preambles and guards, pixels, AVI / ACR / SPD / audio InfoFrames, one island per line); 2,047 s. The first run failed in 576p because the bench measured before the VIC lock re-phased the frame: the bench now waits for `mtim.aligned` in every row (a bench fix, no gateware change). The 576p picture is checked inside the pixel layer's rectangle (640 x 480 at (40, 48)); the VIC picture around it (720 x 486 at Fit) is checked by S4, not here.
- S7 app: `tests/coreapp/test_gwstatus.cc` (row mapping, `has_scale`, scale write / readback, apply through the mapping) and `test_scale.cc` (the VIC-path core gets Picture + Scale, default Fit, no Smoothing, `scale_apply` writes the gateware; a gateware without the register: Picture alone) are written; `test_gwstatus.cc` compiled and run on the Mac (51 checks, 0 failed); the rest runs in CI only (no app tests by agents).
- S8 `tools/dev.sh riscv`: ok (hostapp.app, coreapp.bin).
- S10: `tools/host_pin.sh --check`: host unchanged (329 files). yosys `read_verilog -sv` with the build's defines: no implicit net in `top.v`, `hdmi_out.v`, `vic_scaler.v`, `c64_text.v`, `mode_req.v` (the detector fires on a test file). Full `tools/benches.sh c64scpu -f`: 12 of 13 PASS (base, emu, map, speed, mirror, irq, mdust, hdmi_timing, mode_req, tmds_rep, scaler, hdmi_out); c64scpu_text_geom was still running when the coordinator stopped the run (Alex 2026-11-15: no benches before a merge); it passed earlier (1,579 s).

**What was changed in generic code (D4):** `app/video/videomode.{h,cc}` (`GW_SCALE`, `GW_SCALE_AT`, `video_gw_has_scale`, `video_gw_row_for`, `video_gw_set_scale_io`, board `video_gateware_vic_scale` / `video_set_scale_gateware`; `video_gw_apply_io` maps through `video_gw_row_for`, which is the plain index unless the gateware answers version >= 2 with the C64-type flag), `coreapp/src/picture.{h,cc}` (`PictureEnv::vic_scale`, the Scale item for `video.w == 0` cores, `scale_apply` / `picture_start` write 0x4F01). For every other host and core the behaviour is as before (version 1 / no flag / `vic_scale` false).

**Not checked:** the Vivado build (area, WNS, routing: S9); the app tests in CI (S7 beyond `test_gwstatus.cc`); the board (S11): no picture of any row, no screen, no 50/60 Hz System Mode switch with HDMI up, audio, the freeze menu text position, the Picture / Scale rows on a real screen (`ux-review` pictures of the new Scale row were not made); whether anything the core app draws on the pixel layer at the freeze (a captured still, `video.cc` format 3) is placed right in the other rows (U16 D6 changed TRX64-Ultimate's own app for it; this repository's core app was not read for it; the freeze text itself goes through `c64_text`, whose geometry follows the scale and is proven by S5); `hdmi_gtp.v` / `mode_ctl.v` comment updates were not taken (comments only).


---
- 2026-11-15, run 1960 red at synthesis: `unable to resolve 'hm_align'` in vic_scaler.v. Vivado runs with `host/` as its working
  directory and finds `host/hdmi_modes.vh` (the host's table, no `hm_align`) before the include dir `cores/c64scpu/mono/host`.
  Fix: the core's table is renamed `scpu_hdmi_modes.vh` (includes, benches, `ci/sims.deps`, FROM updated), so no host file shadows it.
