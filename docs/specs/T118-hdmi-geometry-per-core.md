# T118 — The HDMI picture is the same for every core, or we know which signal differs

**Status:** PROPOSED (2026-10-23). Part 1 (is the encoder's input core dependent?) answered in §5 by a bench: no. The board difference is open. 2026-11-13 status check: state confirmed: bench merged (Merge t118-hdmigeom), board difference open (row). **2026-11-15 sync:** the symptom itself is gone: the VC-20 left the vic_scaler path for the pixel layer (T119) and its menu and picture fit on the board (Alex 2026-10-23, "Klappt !"); the question "which signal differs" (P3) was never answered, only bypassed (T119 §1 P2: the scaler's lock time differs, 389,751 vs 190,241 pixel clocks). Whether T118 closes as bypassed is Alex's call (sync report).
**Repo:** TRX64U-MultiCore — a bench in `cores/vc20/sim/`; no change to `host/` (frozen) or to a core.
**Number:** T118 (registry: the table in `TODO.md`).
**Origin:** Alex, 2026-10-23, board photos of release trx64mc-main-2895ec1a (core app 6d9f3d2): the menu fills the 1080p screen on the A1200 core,
on the VIC-20 core it looks about one character (16 output pixels) too large on all four sides, as if the screen overscanned or zoomed.
**Depends on:** T83 (VC-20), T97 (A1200), T104 (generic core app), T91 §5 (the scaler's lock marker).
**Milestone:** M3.

---

## §1 The problem

- **Who:** Alex at the board, with a Samsung monitor (T104: "no overscan").
- **What happens:** same host, same app build, same menu (`trx_lv_port.cc:89`: `PX_SIZE` 960 x 540, `PX_POS` 0, scale 2 = exactly 1920 x 1080), two cores,
  two different pictures on the screen. JTAG (`tools/trxdbg.sh mode`) says the two are identical: PAL, 312 lines, 2016 clocks per line, 628,992 per
  frame, HDMI 50 frames/s, 2,969,856 pixel clocks per frame (a read in steps of 256).
- **Parts:**
  - P1 Does the signal the HDMI encoder is given (hs / vs / de / cx / cy, from `vic_scaler.v`) depend on the core's `pix_*` stream?
  - P2 Where the pixel layer's pixel (0,0) and (1919,1079) land in that signal.
  - P3 What else differs between the two cores on the way to the screen (encoder packets, pixel content, the screen itself).

The chain is `vic_scaler -> overlay -> pix_layer -> c64_text -> hdmi` (`host/top.v:1336-1420`). The only module that makes hs/vs/de/cx/cy is `vic_scaler`: its
`h`/`v` counters run on `pix_clk` alone, the core's stream sets them once (`vic_scaler.v:125`, `frame_seen && !locked`) and never again until `relock`.
`overlay`, `pix_layer` and `c64_text` copy them through a delay (`pix_layer.v:371,401`); the pixel layer places its first pixel by `cx_in == 768 + x0`,
`cy_in == 45 + y0` (`pix_layer.v:282-283,348`); the AVI VIC code comes from `vmode` (`top.v:1396-1403`), not from a core.

## §2 Scenarios

```gherkin
Scenario: S1 the encoder's input is the same for the VC-20 and the A1200 (P1, P2)
  Given the host's vic_scaler, fed with the VC-20 core's own pix_x/pix_y (GHDL dump of cores/vc20/rtl) or with the A1200's dummy raster (amiga_core.v:283-296)
  When  the scaler has locked and four frames are measured
  Then  every frame is 2,970,000 clocks, 1125 lines of 2640, hs 44 clocks, vs 5 lines, 1080 DE lines of 1920,
        layer pixel (0,0) at cx 768 / cy 45 = 192 clocks after hs rises and 108,960 after vs rises, pixel (1919,1079) at 2111 / 2,959,439
  And   the two cores' per-frame lines are identical

Scenario: S2 a core frame of another length does not change the line structure (the hypothesis)
  Given the A1200 raster with 2022 clocks per line (frame 630,864 instead of 628,992)
  Then  the same numbers as S1

Scenario: S3 the bench can fail (negative controls)
  Given no clock with px == 0 on line 20 (nomark)                 Then the scaler never locks, the verdict FAILs
  Given a scaler copy with 2639 clocks per line (H_JUMP 694)      Then the verdict FAILs and the comparison with the A1200 differs
```

| # | Solves | Test | Where it runs |
|---|---|---|---|
| S1, S2, S3 | P1, P2 | `cores/vc20/sim/run_hdmigeom.sh` (`tb_vc20_pix.vhd`, `tb_hdmigeom.v`) | `ci/sims.txt` tier 2 `hdmigeom` |
| (none yet) | P3 | board, same monitor, same order, with photos | open, §5 |

## §3 Decision

D1. The bench replays each core's stream into the host's real `vic_scaler.v` + `pal_ram.v` (`pix_layer`, `overlay`, `c64_text` and the encoder are not in it:
they take cx/cy/de/hs/vs from the scaler and cannot change them; the layer's placement is `host/sim/pixlayer`'s subject). Nothing in `host/` or a core
changes.

## §4 Out

A fix. There is nothing in the encoder's input to fix. Anything in `host/` is frozen (`tools/host_pin.sh`).

## §5 Findings

**2026-10-23, S1-S3 green (builder, trx64-sim-env:4, `OUT=/w/out sh cores/vc20/sim/run_hdmigeom.sh`, 7 minutes):**

| signal | VC-20 | A1200 | A1200, frame 630,864 | needs |
|---|---|---|---|---|
| lock (scaler) | locked, pix_clk 389,751 | locked, 190,241 | locked, 190,808 | marker `px==0 && py==20`, 4 clocks |
| frame (vs rise to vs rise) | 2,970,000 | 2,970,000 | 2,970,000 | 2640 x 1125 |
| line (hs rise to hs rise), min = max | 2640 | 2640 | 2640 | |
| hs / vs width | 44 / 13,200 (5 lines) | same | same | |
| DE lines x width | 1080 x 1920 | same | same | |
| layer (0,0): clocks after hs / vs rise | 192 / 108,960 | same | same | cx 768, cy 45 |
| layer (1919,1079): after hs / vs rise | 2111 / 2,959,439 | same | same | cx 2687, cy 1124 |

Report line (`G`, VC-20 frame 3 and A1200 frame 1 are the same text): `flen=2970000 lines=1125 ln_min=2640 ln_max=2640 hsw=44 vsw=13200 de_lines=1080 dew_min=1920 dew_max=1920 p00_hs=192 p00_vs=108960 pend_hs=2111 pend_vs=2959439 first=(768,45) last=(2687,1124)`.
Verdicts: `PASS hdmigeom vc20`, `PASS hdmigeom a1200`, `PASS hdmigeom slow`, `PASS hdmigeom: a1200 and vc20 give the same encoder input (every frame)`.
Controls: `nomark` locked 0 -> FAIL as required; `mut_line` frame 2,968,875, line 2639 -> FAIL and the comparison differs (`G ... flen=2968875 ln_min=2639`).

**The hypothesis is wrong.** The core's stream decides the phase of the first frame (one jump at the lock) and nothing else; line length, sync positions, DE
width, active lines and the layer origin do not depend on it, also not when the core's frame is longer than the output frame. Both cores run on the same
`sys_clk` (31.5278 MHz) and give the same 628,992-clock frame: the VC-20 by `vc20_nco` (71/252, `vc20_glue.vhd`), the A1200 by its dummy raster.

**What is not proven (P3):** the encoder itself (AVI/audio packets) is not in the bench; its inputs `vic_code`/`video_rate` come from `vmode` and are the same
for both (`mode PAL`), so equal packets follow from the code, not from a measurement. The pixel content of the menu is the same code for both cores. What is
left is on the board side: the screen's own handling of the signal (first frames after a lock jump, mode memory per input), and the order in which the photos
were taken. A board step that tells: same monitor, power-cycle the monitor's input (Alex), the VIC-20 core first and the A1200 second and the reverse, one photo each.
