# T40 — System Mode: PAL, NTSC, PAL-60, NTSC-50 (and the two "/L" variants)

**Status:** built on branch `t40-ntsc` (plan §9, as built §10, 2026-10-03); board test open. **Task:** TODO.md T40. **Milestone:** M1 (everything the
app's "System Mode" does works on our core).
**Shares with:** T13 (audio/video stream), T14 (app I2C), T20 (`FRAME`, frame markers: "NTSC. PAL only"
`T20-delta-ring.md:73`), T25.3 (decimator ratio), T29 (reset hold, `T29-turbo.md:48`), T12 (pause/resume).
Evidence: code only (app `software/ultimate-app/software`, our RTL, kawari HDL) plus the measurements in
`docs/turbo-measurements-gideon.md`. Where Gideon's core is closed the rule is: measured on his bitstream wins.

## 1. Goal

"System Mode" has six values; each sets the VIC variant, the C64 clock, the HDMI frame timing, the
audio resampler, the overlay and the UDP stream geometry. Switching is **live**: the running C64 program
keeps running (`turbo-measurements-gideon.md:359-360`, "System Mode PAL -> NTSC ... The program kept running (no reset)").
Today our bitstream is PAL only (`c64_core.v:24, 301, 382`, `vicii_kawari.v:224`, `vic_scaler.v:78`, `udp_stream.v:25-27`); the app's
writes are latched and ignored. Non-goals: analog video encodings (`phase_inc`, `burst_phase`, `RGB_OUTPUT`, no analog
output is built), "Adjust Color Clock" (the hook does nothing: `setPllOffset` body is commented out, `u64_config.cc:1431-1436`),
other HDMI resolutions (we only do 1080p, `top.v:1198-1280`).

## 2. What the app does (code)

### 2.1 The modes

`color_sel` (`u64_config.cc:276`), item `CFG_SYSTEM_MODE` default **index 1 = NTSC** (`:353`; a fresh config on a
U64-II asks for NTSC), boot hotkeys Ctrl-P / Ctrl-N (`:965-976`). The table the app uses (`u64/color_timings.cc:6-20`,
frequency from `SetVideoPll`, `system/u64ii_init.cc:141-148`: `(m + frac / 2^32) * 50 / 30` MHz):

| Index | Mode | `mode_bits` (`u64.h:163-170`) | VIC (cycles x lines) | dot4x | phi2 | Frame |
|---|---|---|---|---|---|---|
| 0 | PAL | `CYCLES_63` | 63 x 312 | 31.5280 MHz | 985,250 Hz | 50.125 Hz |
| 1 | NTSC | `CYCLES_65 | NTSC_ENCODING | NTSC_FREQ | 60_HZ` | 65 x 263 | 32.7273 MHz | 1,022,727 Hz | 59.826 Hz |
| 2 | PAL-60 | `CYCLES_65 | NTSC_FREQ | 60_HZ` | 65 x 263 | 32.7406 MHz | 1,023,143 Hz | 59.85 Hz |
| 3 | NTSC-50 | `CYCLES_63 | NTSC_ENCODING` | 63 x 312 | 31.5485 MHz | 985,892 Hz | 50.157 Hz |
| 4 | PAL-60/L | as PAL-60 | 65 x 263 | 32.7600 MHz | 1,023,749 Hz | 59.886 Hz |
| 5 | NTSC-50/L | as NTSC-50 | 63 x 312 | 31.5815 MHz | 986,921 Hz | 50.21 Hz |

(lines: 263 and 312 from the kawari variants below; Gideon's closed core is **H** to match, M1.) In all six
entries `CYCLES_65` goes with `60_HZ` and `NTSC_FREQ`, `CYCLES_63` with neither; `CYCLES_64` and `RESET_BURST` are never used.
So two VIC variants cover everything: **63 x 312** (PAL, NTSC-50) and **65 x 263** (NTSC, PAL-60); the modes differ in clock,
chroma encoding (analog only) and a few ppm.

### 2.2 The apply sequence (`U64Config::effectuate_settings`, `:1080-1190`)

On a changed mode (or HDMI resolution) the app does, in this order: `SetVideoPll` (CDCE PLL1 at I2C 0xC8, 1.8 V channel, block write from register 0x14,
`u64ii_init.cc:141-160`); `C64_VIDEOFORMAT` (0x10180001) = `mode_bits | RGB`; `SetVideoMode1080p` (`hdmi_scan.cc:112`, 1080p case `:277-298`):
`SetInternalPLL` (MMCM DRP at 0x10200000, `u64ii_init.cc:176`), `SetExternalPLL` (CDCE on the HDMI channel, `:162`), the HDMI timing
registers (`SetScanModeRegisters`, `hdmi_scan.cc:6-19`, incl. `VID_VIC`), `SetVicCrop`, `hscaler`, `vscaler`, `x_offset`, `resync`;
`DetermineOverlaySettings` (`:3025`; the 1080p overlay is the same for every mode: 40 x 25 at x 1438, y 240, `:3042-3044`);
`ResetHdmiPll` (`U64_HDMI_PLL_RESET` 0x10100404 = 3 then 0, `u64ii_init.cc:105-108`); `SetResampleFilter` (`:2242`: PAL 675 words / LABOR 45,
NTSC 512 words / LABOR 169, picked by `audio_div == 77`, i.e. 50 Hz vs 60 Hz clock family; T25.3 §1.2). `C64_PHASE_INCR` (+0x05) is written on every apply (analog).

1080p picture (the app's values, `hdmi_scan.cc:277-298`): PAL crop (8, 9, 384 x 270), `hscaler` 0x0C (384 -> 1440), `vscaler` 0x08 (270 -> 1080, x4), `x_offset` 240,
frame `sm_1080p50` = 148,869,856 Hz, 2640 x 1125, VIC 31; NTSC crop (8, 0, 384 x 240), `vscaler` 0x19 (240 -> 1080, **x4.5**),
frame `sm_1080p60` = 148,069,608 Hz, 2200 x 1125, VIC 16. Check: 148,069,608 / (2200 x 1125) = 59.82610 Hz = 32,727,273 / (65 x 263 x 32) =
59.82610 Hz: **Gideon locks the HDMI frame to the VIC frame** (PAL: 50.1246 vs 50.1243 Hz, 6 ppm).

### 2.3 The UDP video stream

`tests/e2e/lib/streams.py:285-286`: frame height 272 lines (PAL) or 240 (NTSC), width 384, 4 lines per packet (68 or 60 packets).

## 3. What is PAL-only in our design (code)

| Where | PAL fact |
|---|---|
| `designs/c64_basic/top.v:134-151` | `mmcm_sys`: 50 MHz x 56.75 / 3 / 30 = 31.5278 MHz, CLKOUT1 = 63.0556 MHz (SID engine, exactly 2x) |
| `top.v:153-166` | `mmcm_ref`: sys x 31.25 / 26 = 125/104 of dot4x, forwarded to both CDCEs (`CLOCK_C64_SYNTH`, `HDMI_CLK_SYNTH`) |
| `cdce_i2c.v:1-52` | CDCE PLL1 = 4 x its input (N/M = 4/1, P = 2, Q = 16): 151.58 MHz, one fixed sequence, VCO range 150-175 MHz |
| `app_i2c.v:22-24` | the app's writes to 0x64 (its `SetVideoPll`, `SetExternalPLL`) are swallowed and ACKed; its `MMCM` (0x10200000), HDMI timing regs and crop are latches/no-ops |
| `top.v:1198-1215`, `hdmi_gtp.v:1-5` | GTP at 1.516 Gb/s, REFCLK 151.58 MHz; `mmcm_pix` = usrclk x 12 / 6 (pixel clock) |
| `top.v:1270-1282` | `hdmi` parameters: `VIDEO_ID_CODE(31)`, `VIDEO_RATE(151_576_923)`, frame 2688 x 1125, screen X 768..2688, Y 45..1125 (elaboration constants; the data islands sit at `(SCREEN_X_END + 14) % FRAME_WIDTH`, `cores/icepi/hdmi/hdmi.v:84-97`) |
| `vic_scaler.v:7-20, 78-80` | 2688 x 1125 = 125/26 x the VIC frame exactly; lock **once** on the first frame (`:83-85`), no per-frame resync; window 400 x 270 VIC pixels at X0 = 96, Y0 = 16, 4x/4x |
| `overlay.v:142`, `pix_layer.v:278` | active area starts at cx = 768 |
| `vicii_kawari.v:224` | `.standard_sw(1'b1)`: kawari `chip = {0, standard_sw}` (`registers_no_eeprom.vi:23`; `common.vh:158-161`) selects 6569R3 vs 6567R8; "chip must be set when is_reset is asserted" (`registers_no_eeprom.vi:20`) |
| `c64_core.v:301` | `FRAME = 628992` dot4x clocks (312 x 63 x 32): whole-frame pause/resume |
| `c64_core.v:382`, `c64_top.v:26, 589-590, 645, 699`, `c64_keyboard.v:35, 86-90` | `SYS_CLK_FREQUENCY = 31,527,778` as a parameter: CIA TOD divider (`c64_redip_cia.v:85-101`: toggles at `INC / s`, INC 100 PAL or 120 `NTSC` parameter, on the **parameter** clock, so at 32.7 MHz the TOD would run 3.8 % fast), keyboard repeat |
| `udp_stream.v:25-27, 63` | `X_S = 104`, `Y_S = 15`, `LINES = 272` |
| `cores/trx_sid` / T25.3 | decimator ratio `49259/2400` input per output sample (`T25.3-lp-filter.md:93, 165`) |

## 4. Design

### 4.1 One mode register, one apply event

`vid_fmt` = latch of `C64_VIDEOFORMAT` (0x10180001, page 011 +0x01); only two derived bits matter: `v60 = fmt[5]` (65 cycles; 63 = 0) and `clk_ntscfam = fmt[3]`
(NTSC clock family; equal to `v60` in the six modes), plus the mode index for the dot4x choice, which the register does not carry (PAL-60 and NTSC are both `v60`
with different `m, frac`). Mode index source (Q1): the app's `m`/`frac` are only in the swallowed I2C block, so decode **`fmt[1]/[3]/[0]` + the
`VID_VIC`/HDMI timing registers** for 50/60 Hz, and take the six-way choice from the CDCE block the app writes to 0x64 on the 1.8 V channel (its eight bytes
carry N, M, P: distinct per mode; `app_i2c` ACKs them today but keeps nothing, so it needs a capture register). If that proves fragile, a 6-way index is a one-register app patch (1 line in `effectuate_settings`).
The **apply event** is the write of `U64_HDMI_PLL_RESET` = 3 (`ResetHdmiPll`, the app's last step of §2.2).

### 4.2 Clocks

- **sys (dot4x) and sid2x:** `mmcm_sys` reconfigured through its DRP (the 50T has five MMCMs, all in use, `top.v:178-179`; no spare to switch to).
  A small DRP sequencer (XAPP888 style: reset the MMCM, write the ROM entry of the mode, release, wait for lock) with a six-entry ROM. CLKOUT0 must stay an
  even integer divider so that CLKOUT1 = O/2 is exactly 2x (`top.v:136-137`). Best settings for 50 MHz input (VCO 600-1200 MHz, found by exhaustive search):

  | Mode | D | M | O0 | dot4x | vs the app's table |
  |---|---|---|---|---|---|
  | PAL | 3 | 56.75 | 30 | 31.52778 MHz | -0.7 ppm (as built) |
  | NTSC | 2 | 47.125 | 36 | 32.72569 MHz | -48 ppm |
  | PAL-60 | 4 | 57.625 | 22 | 32.74148 MHz | +27 ppm |
  | NTSC-50 | 3 | 53 | 28 | 31.54762 MHz | -28 ppm |
  | PAL-60/L | 3 | 43.25 | 22 | 32.76523 MHz | +157 ppm |
  | NTSC-50/L | 1 | 24 | 38 | 31.57895 MHz | -81 ppm |

  All within crystal tolerance; the "/L" variants only differ from their parents by 0.1-0.2 % (analog colour-lock modes) and are kept for completeness (Q2).
- **ref MMCM** (`mmcm_ref`, VCO 1022 MHz at NTSC) follows sys through its integer ratio: no change; it re-locks after sys.
- **Timing closure at the fastest clock.** The sys domain must close at 32.77 MHz (30.5 ns; Gideon's XDC says the same, `c64u_gideon_issue615.xdc:37-39`
  "NTSC is the highest freq"), +3.9 % over what CI closes today (T38 board build: WNS +0.015 ns overall). Constrain the sys clock at 30.5 ns in the build
  (`set_clock_uncertainty` on the generated clock) before anything else is built; this is the one risk that can sink the task (R1).
- **Live switch, no C64 reset.** The sequence (all in a small `mode_ctl.v`): (1) gate the C64 with the existing machine pause (T12/`c64_ce`, whole frames, no
  memory write mid-instruction); (2) mask `clocks_ok` for `sys_rst` (`top.v:172-174`: today a lost lock resets the whole sys domain, which would be a C64 reset)
  while the DRP runs; (3) DRP, wait for sys, sid2x, ref lock; (4) switch kawari `standard_sw`, reset the VIC only (it restarts phi2; the CPU, CIAs, SID, RAM keep
  their state: the measured Gideon behaviour); (5) HDMI (§4.4); (6) resume at a whole-frame boundary with the new `FRAME`.

### 4.3 VIC, CIA, SID, drive

- Kawari: `standard_sw` = !v60 as a registered input; `raster_y_max` 262 / 311 and the cycle counts come from the chip select (`vicii.v:186-225`); no other variant is needed
  (the app never asks for 64 cycles).
- `FRAME` becomes `v60 ? 547040 : 628992` (65 x 263 x 32). T20's frame markers and the line-16 snap (`T20-delta-ring.md:73`, `trx_engine.v` mk logic) are line-based and
  carry over; the ring's per-frame record count is the only known follow-up (T20 owner).
- **CIA TOD.** `CLK_FREQUENCY` of the TOD divider becomes an input (the live MMCM value), INC stays 100/120 per `v60` (**H**: Gideon makes TOD 50/60 Hz with the
  video family; real C64: mains, independent). M3 decides. The same `CLK_FREQUENCY` feeds keyboard repeat.
- SID: the reDIP/T25.1 SIDs take phi2 from the VIC; nothing changes but the decimator ratio (§4.6).
- Drive and IEC: own clocks (`T29-turbo.md:477` "keeps its own 1 MHz"), unchanged.

### 4.4 HDMI and the scaler

Two frame structures, selected by `v60`:

| | 50 Hz (PAL, NTSC-50) | 60 Hz (NTSC, PAL-60) |
|---|---|---|
| Frame | 2688 x 1125 (as built), pixel clock = 125/26 x dot4x = 151.58 MHz | **2200 x 1125** (CEA 1080p60 blanking: fp 88, sync 44, bp 148), pixel clock = (2,475,000 / 547,040) x dot4x = 61875/13676 x dot4x = 148.06 MHz |
| VIC lock | exact (125/26) | exact ratio needed: **990/263** from the 125/104 reference (`pix/ref = 61875 x 104 / (13676 x 125) = 990/263`); 263 is prime, so no MMCM ratio reaches it |
| CDCE (HDMI) | N/M = 4/1, P = 2, Q = 16, R = 0 (as `cdce_i2c.v`) | N = 990, M = 263: TI recipe `N' = N x 2^(4-P)`, `Q = int(N'/M)`, `R = N' - M x Q`; P = 1 gives N' = 7920, **Q = 30, R = 30** (valid: 16 <= Q <= 63, R <= 51); VCO 148.06 MHz -> range 125-150 MHz (the 50 Hz setting uses 150-175) |
| VIC frame in pixel clocks | 3,024,000 | 2,475,000 |
| AVI VIC | 31 | 16 |

Gideon's own 1080p60 block (`hdmi_scan.cc:94`, bytes `55 f7 82 89`) decodes by the same TI formulas to N = 1375, Q = 20, R = 240, P = 2, i.e. M = 263 (5500 - 20 x 263 = 240): he also pays for 263. The price: PFD = 39.33 MHz / 263 = 150 kHz. His reference is **H** the dot4x clock (32.73 MHz / 263 = 124 kHz), so the chip is known to lock at that PFD, but jitter at 148 MHz TMDS must be looked at on the sinks (R2): fallback = a low-denominator ratio
(e.g. 64/17, +112 ppm) plus a per-frame resync in `vic_scaler` (it locks once today, `:83-85`; the resync would stretch the last blank line by a few clocks, which sinks accept).

Scaler changes: `H_TOT`, `H_ACT0`, `H_SYNC0/1` per mode (2200: ACT0 280, sync 88..132); `V_TOT` 1125 stays. Picture: PAL as built (window 400 x 270, x4/x4). NTSC: **window 400 x 240 VIC pixels, x4 horizontally and x4.5 vertically**
(240 -> 1080, the app's own `vscaler = 0x19`): a phase accumulator in place of `y[10:2]` (`acc += 2`; when `acc >= 9` take the next VIC line: 4, 5, 4, 5 output lines). X0/Y0/ALIGN for the NTSC raster come from the
calibration of `x_text` (`vic_scaler.v:45-56`) in simulation (Q3: x4.5 as Gideon, or x4 with 60-line bars top and bottom).
`overlay.v:142` and `pix_layer.v:278` assume the active area at cx = 768: either keep that contract by letting NTSC's `cx` run from 488 (so the active area stays 768..2687) which breaks `hdmi.v`'s data-island
positions (`% FRAME_WIDTH`, `hdmi.v:84-97`), or give the three consumers an `h_act0` input and `hdmi.v` runtime frame parameters (island positions, `SCREEN_X_START`, `VIDEO_ID_CODE` in the AVI frame,
`auxiliary_video_information_info_frame.sv:54`), or instantiate a second encoder. Decision at implementation by synthesis size (Q4 is only for Alex if it changes visible behaviour).
GTP: REFCLK 148.06 MHz instead of 151.58 MHz: line rate 1.481 instead of 1.516 Gb/s, both inside the CPLL range of the wizard's divider setting (to be confirmed in `gt_hdmi`'s generated settings, R3); `gt_ready` drops and the picture is
blank for the re-lock (hundreds of ms), as on Gideon's core during `ResetHdmiPll`.
The CDCE write needs the I2C bus: a second client of `cdce_i2c.v` that waits for the app idle exactly as the keyboard scanner does (`kb_parked`, `app_i2c.v` arbitration), not a second master.

### 4.5 UDP video stream

`udp_stream.v` gets `LINES`, `Y_S`, `X_S` per mode: PAL unchanged; 60 Hz: 240 lines (60 packets, `streams.py:286, 301`), the line origin and X origin measured (M4), and the "last packet" height field.
Frame rate follows the VIC. The packet header, size 384 and 4 lines per packet are unchanged.

### 4.6 Audio

The codec clock (MCLK 12.2887 MHz from the 50 MHz crystal, 48,003.07 Hz) is independent of the mode and stays. What changes is the decimator's input-to-output ratio: input phi2 / output fs from the MMCM integers
(exact rationals, the method of T25.3 E2): PAL 49259/2400 (as built), NTSC 81809/3840, PAL-60 300111/14080, NTSC-50 1643/80 (for the settings in §4.2); one constant per mode. The app's resampler tables (PAL 675 / NTSC 512 words) stay ignored (T25.3
Q1); Gideon's stream rates (47,983 / 47,940 Hz, `T25.3-lp-filter.md:55`) differ from our fixed 48,003 Hz (M5).

### 4.7 Reset hold (T29)

Gideon's post-reset hold is counted in PHI2 cycles: 2^22 = 4,194,304 on PAL (4.257 s) and on NTSC (4.10 s), measured (`turbo-measurements-gideon.md:262-290, 362-366`: NTSC 4,194,236 / 4,194,166 PHI2 ticks, same as PAL).
A counter in PHI2 cycles needs nothing mode-specific. **Correction owed to T29:** `T29-turbo.md:92, 136` says 2.06 s = 2,029,611 PHI2 cycles (21-bit `hold`); the later rounds measured 2^22 after a REST reset (and a shorter hold after `run_prg`): the counter must be 22 bits and the length revisited there.

## 5. Measurement plan on Gideon's core

| # | Measures | How |
|---|---|---|
| M1 | PHI2 per frame, lines per frame, cycles per line in all six modes (the doc's NTSC "17,046 PHI2 per frame", `turbo-measurements-gideon.md:364`, is 49 cycles short of 65 x 263 = 17,095: which is right?) | C64 probe PRG via REST: CIA timer between two raster IRQs, max `$D012`, per mode (REST `System Mode` config) |
| M2 | Live switch: does the program survive in all 30 transitions; PHI2 ticks lost across the switch (CIA2 32-bit counter); how long HDMI and the UDP stream are dark | probe PRG, REST config PUT, UDP capture |
| M3 | CIA TOD tick rate (1/10 s register) per mode, CRA bit 7 0/1 | probe PRG, 60 s count against raster frames |
| M4 | UDP stream geometry: height (240/272), the first border line index and the text-area X in the 384-pixel window, per mode | `tools/stream_shot.py` with a border/text test screen; compare to `udp_stream.v` X_S/Y_S |
| M5 | UDP audio sample rate per mode (47,983 / 47,940 Hz as documented?) | packet count over 60 s |
| M6 | HDMI timing as seen by a sink: AVI VIC, total 2200 x 1125 or not, frame rate | capture device / sink info page (Alex's setup) |
| M7 | Whether the SID, drive and IEC speeds follow the mode (real C64: SID pitch follows phi2; 1541 does not) | SID tone and drive loop timing per mode |

## 6. Test plan

**Simulation:** (1) `sim/scaler`: both frame structures lock, line counts 1125, `de` window 1920 x 1080, 4/5 line pattern, NTSC `x_text` calibration; (2) `sim/lat` / a kawari bench:
`standard_sw` switch resets the VIC only, 263 lines x 65 cycles, `$D012` max 262; (3) `tb_input_core`-style Verilator run with the KERNAL: boot PAL, switch to NTSC mid-READY, the screen survives, CIA timer
period = 17,095 PHI2; (4) DRP sequencer against an MMCM behavioural model (reconfigure, lock, no `sys_rst` pulse); (5) CDCE byte generator vs the TI formulas for all six dot4x/HDMI pairs; (6) decimator ratio counters over 10^8 cycles per mode;
(7) `udp_stream` packet count 60/68 per frame; (8) regression: PAL builds produce bit-identical streams to today.

**Board:** B1 PAL regression (no change in the picture, `SLOT_STATUS`, stream); B2 switch PAL -> NTSC -> PAL with a running BASIC loop; B3 each mode on two HDMI sinks (picture, no flicker, audio present); B4 UDP stream decoded
by `tests/e2e/lib/streams.py` at 240 lines; B5 TOD per M3; B6 Lorenz timing tests in NTSC (65-cycle `cia1tb`-class tests) as a fidelity check; B7 SID player at NTSC pitch.

## 7. Resources (rough, not measured)

| Block | LUT | FF | BRAM | DSP |
|---|---:|---:|---:|---:|
| DRP sequencer + 6-entry ROM + mode control | 150 | 120 | 0 | 0 |
| second CDCE byte set + bus client | 60 | 40 | 0 | 0 |
| scaler: per-mode constants, 4/4.5 accumulator | 30 | 20 | 0 | 0 |
| `hdmi.v` runtime parameters (or a second encoder: about the encoder's size, to be measured) | 80 | 40 | 0 | 0 |
| TOD/keyboard clock inputs, `FRAME` mux, stream constants, decimator constants | 40 | 40 | 0 | 0 |
| Total | about 360 | about 260 | 0 | 0 |

Baseline 25,346 LUT, 77.7 % (`docs/lut-budget.md`).

Risks: **R1** timing closure of the sys domain at 32.77 MHz (§4.2); **R2** HDMI clock jitter with a 150 kHz PFD, sink acceptance (§4.4); **R3** GTP re-lock and CPLL range at 148.06 MHz (§4.4).

## 8. Open questions (Alex)

1. ~~Mode index source~~ **Decided 2026-10-03: from `C64_VIDEOFORMAT` (0x10180001), which the app already writes with `ct->mode_bits` (`u64_config.cc:1161-1174`, table `color_timings.cc:6-11`): CYCLES_63/65, NTSC_ENCODING, NTSC_FREQ, 60_HZ separate PAL-50, NTSC-60, PAL-60 and NTSC-50. No app patch, no CDCE capture.** The only modes the bits cannot tell apart are the variant pairs PAL-60 free/281.5 and NTSC-50 free/228.5, which differ only in the clock (Gideon gets it from the CDCE the app programs; our C64 clock is an MMCM) — that is Q2.
2. Do we need PAL-60/L and NTSC-50/L (analog colour-lock modes; on digital output they differ from their parents by 0.1-0.2 %)? Proposal: implement them, they are free once the table exists. **Decided (Alex, 2026-10-03): M1 builds four clocks (PAL-50, NTSC-60, PAL-60, NTSC-50); each variant pair (free / colour-locked) uses the colour-locked clock. Separate free clocks only when the analog output is taken up.**
3. NTSC picture on HDMI: 240 VIC lines x4.5 filling 1080 (Gideon's `vscaler 0x19`, uneven 4/5 line pattern) or x4 with 60-line bars? **Decided (Alex, 2026-10-03): x4.5 as Gideon. Only the vertical scale changes; the horizontal scale and the pillarbox (4:3 picture in the 16:9 frame, bars left and right) stay as in PAL.**
4. Exact 990/263 lock with a 150 kHz PFD (like Gideon) or a low-denominator ratio plus per-frame resync, if the sinks dislike the jitter? Decided after the first sink test (R2).
5. Default "System Mode" on an erased config is NTSC (index 1, `u64_config.cc:353`): is that what your flash image has, and should our first boot after an erase follow it? **Decided (Alex, 2026-10-03): PAL is our default.** Our core resets to PAL, and on trx64/main the app's default for `CFG_SYSTEM_MODE` becomes 0 (PAL) — a one-number change in `u64_config.cc:353`. CTRL-P / CTRL-N at boot (`u64_config.cc:971-977`) still switch.
6. Which sinks (and capture devices) do you want in B3/M6? **Answered (Alex, 2026-10-03): the Acer monitor only; no capture device. The TV is a PAL set (if it has HDMI, 1080p60 is still worth one try, since HDMI is not PAL/NTSC).**

## 9. Implementation plan (2026-10-03, branch `t40-ntsc`)

Decisions Q1-Q6 applied. Where this section and §4 differ, this section is the plan.

### 9.1 Mode and apply event

- `ultimate_sys.vhd` latches `C64_VIDEOFORMAT` (page 011 +0x01, reset 0x00 = PAL) as `video_fmt`.
- Mode index `vmode[1:0]` from the bits (Q1): `fmt[5]` (CYCLES_65) and `fmt[0]` (NTSC_ENCODING):
  0 PAL (63, PAL), 1 NTSC (65, NTSC), 2 PAL-60 (65, PAL), 3 NTSC-50 (63, NTSC). `v60 = (vmode == 1 || vmode == 2)`.
- Apply event: the latched mode differs from the applied one (`mode_ctl.v`, clk50). Not the
  `U64_HDMI_PLL_RESET` write: the format alone carries everything, and an unchanged mode costs nothing.
  Power-on and core reset: PAL (Q5); the static MMCM setting is PAL, so a PAL boot never runs the sequence.

### 9.2 Clocks (Q2: four clocks, the variant pairs on the colour-locked one)

| vmode | Mode | App index | D | M | O0 / O1 | dot4x (Hz) | vs app table |
|---|---|---|---|---|---|---|---|
| 0 | PAL | 0 | 3 | 56.75 | 30 / 15 | 31,527,778 | as built |
| 1 | NTSC | 1 | 3 | 47.125 | 24 / 12 | 32,725,694 | -48 ppm |
| 2 | PAL-60 | 2, 4 | 3 | 43.25 | 22 / 11 | 32,765,152 | +157 ppm vs 4 (281.5) |
| 3 | NTSC-50 | 3, 5 | 2 | 35.375 | 28 / 14 | 31,584,821 | +105 ppm vs 5 (228.5) |

(VCO 946 / 785 / 721 / 884 MHz; NTSC-50/L's best fit D=1 M=24 O=38 sits at VCO 1200 MHz, the -1 limit: not taken.)
`mmcm_sys` becomes `MMCME2_ADV` with its DRP on clk50. The DRP words come from AMD's
`mmcm_pll_drp_func_7s_mmcm.vh` (clk_wiz 6.0, in the Vivado image), evaluated once by
`tools/mmcm_drp_gen.v` (not vendored: the generator includes it from the image); the table is a
constant in `mode_ctl.v`. Written registers (read-modify-write with XAPP888's masks): power 0x28,
CLKOUT0 0x08/0x09, CLKOUT1 0x0A/0x0B, DIVCLK 0x16, CLKFBOUT 0x14/0x15, its fraction bits in 0x13
(bits 13:10 only), lock 0x18-0x1A, filter 0x4E/0x4F. CLKOUT0 stays integer in all modes, so 0x07 is left alone.

### 9.3 The switch sequence (`mode_ctl.v`, clk50)

1. Halt the machine (c64_core `halt_req`, whole C64 and drive A; not seen by freeze_resp). Proceed when
   halted, or the C64 is in reset (it never halts there), or after 84 ms.
2. Keep the app's I2C master off the buses (its `i2c_done` input held low: no new claim) and park the
   keyboard scan (`kb_pause`); wait until the app does not own the buses and the scan is parked.
3. Mask `clocks_ok` from `sys_rst`; stop sys and sid2x (their BUFGs become BUFGCEs, CE from a flop on
   an ungated copy of the MMCM output); the C64's BUFGCE CE is ANDed with the same enable.
4. DRP: MMCM reset, read-modify-write the 15 registers, release, wait `sys_locked` and `ref_locked`
   (mmcm_ref follows by itself: RST = !sys_locked), then 64 clocks; start sys and sid2x; unmask.
5. `cdce_i2c` restarts by itself on the lock loss (its reset is `!clocks_ok`) and now writes the
   byte set of the new family; `i2c_done` low resets the GTPs (`settle`), so the HDMI picture
   re-locks 50 ms after the synthesizer. Wait for `i2c_done`, release the app's master and the scan.
6. Hand `vmode` to the C64 and release the halt. c64_core sees the new mode with the gate open and
   restarts the VIC's timing only: kawari `reset_req` for 8 clocks with the new `standard_sw`, its CPU
   reset output masked for 64 clocks (`c64_top`: `cpu_reset = ... || (vic_cpu_reset && !vic_trst_mask)`);
   kawari's register file keeps its values on reset (its reset block has them commented out), so
   $D000-$D02E, the CPU, CIAs, SID and RAM keep their state. The VIC starts at raster line 0.

### 9.4 Per-mode values

| | PAL / NTSC-50 (63 x 312) | NTSC / PAL-60 (65 x 263) |
|---|---|---|
| `FRAME` (c64_core) | 628,992 | 547,040 |
| CIA TOD | `INC` 100 (50 Hz), clock = the mode's dot4x (runtime input) | `INC` 120 (60 Hz) |
| CDCE (both buses, 0x18-0x1B) | 00 40 02 0A (N 4, Q 16, R 0, P 2, VCO 150-175) | 3D E0 F3 CD (N 990, Q 30, R 30, P 3, VCO 125-150; M = 263) |
| HDMI frame | 2688 x 1125, VIC 31 | 2200 x 1125, VIC 16 |
| pixel clock | 125/26 dot4x: 151.58 / 151.85 MHz | 61875/13676 dot4x: 148.06 / 148.24 MHz |
| scaler | 400 x 270 at X0 96, Y0 16, x4 / x4 | 400 x 240 at X0 96 (sim), Y0 31, x4 / x4.5 (5,4,5,4 lines) |
| UDP stream | 272 lines from 15, X 104 | 240 lines from 31, X 104 (sim) |
| bling UART bit | 126 clocks | 131 clocks |

NTSC: the text area (lines 51-250) gets 20 border lines above and below, so the window runs 31-262 and
on through 0-7 of the next frame: scaler and stream count a "virtual" line (`py + 263` below the window start).
HDMI at 60 Hz keeps the active area at cx 768-2687 and the counters `overlay.v`, `pix_layer.v`,
`c64_text.v` and `hdmi.v` take: cx runs 0-255, then jumps to 744 (2200 per line, CEA 1080p60
blanking: front porch cx 0-87, sync 88-131, back porch 132-255 and 744-767). `hdmi.v` gets three
runtime inputs (VIC code, `VIDEO_RATE` for its 48 kHz sample tick) and a parameter capping its data
islands at 7 packets per line (they sit at cx 4-242 in both modes; 18 do not fit into 280 clocks of
blanking); PAL's HDMI stream changes only in the number of null packets per line.

### 9.5 Audio (T25.3 decimator, TRX_SID builds)

The FRAC stage's step per input sample becomes per mode, exact: `rem` advances by N/DEN with the
phase lookup `rem x m >> 15`:

| vmode | N | DEN | m |
|---|---|---|---|
| 0 PAL | 49,259 | 38,400 | 111,848 (= 2 x 55,924: bit-exact with today's) |
| 1 NTSC | 409,045 | 307,200 | 13,981 |
| 2 PAL-60 | 563,115 | 422,400 | 10,168 |
| 3 NTSC-50 | 394,785 | 307,200 | 13,981 |

(N/DEN = phi2 / 16 / 48,003.07 Hz from the MMCM integers: 78,120 x M / (D x O0) / 38,400.)
The reDIP SID (default build) follows phi2 by itself.

### 9.6 Timing

The sys domain is constrained for the fastest clock (PAL-60, 30.52 ns): setup uncertainty +1.2 ns on
sys/sys and +0.6 ns on every path touching sid2x (top level), c64_core's OOC clocks at 30.52 / 15.26 ns;
`hdmi_refclk` at 6.585 ns (NTSC-50's 151.85 MHz).

### 9.7 App (submodule, branch `t40-app` from trx64/main)

One value: `CFG_SYSTEM_MODE` default 1 -> 0 (`u64_config.cc`). Nothing else.

### 9.8 Simulation

`sim/ntsc` (iverilog): DRP sequencer against an MMCM register model (writes = table, reset/lock order,
no `sys_rst`), CDCE byte sets. `tb_scaler.v` for both frame structures (line count, 2200/2688, `de`
1920 x 1080, 4/5 pattern, lock). `tb_lorenz` gets a mode: KERNAL boot per mode ($02A6, raster lines,
cycles per line), a live switch with a running program. `udp_stream` line counts. Existing benches.

## 10. As built (2026-10-03)

Files: `designs/c64_basic/mode_ctl.v` (new), `top.v`, `ultimate_sys.vhd` (`video_fmt`), `cdce_i2c.v`
(`v60` byte set), `app_i2c.v` (`hold`/`parked`), `c64_core.v` (`vmode`, `FRAME`, VIC restart),
`cores/icepi/c64_top.v`, `vicii_kawari.v` (`standard_sw` input), `c64_redip_cia.v` / `c64_keyboard.v`
(`clk_freq`, `tod60`), `vic_scaler.v`, `udp_stream.v`, `bling_keyboard.v`, `cores/icepi/hdmi/*`
(`RUNTIME`, `MAX_PACKETS`, `vic_code`, `video_rate`), `cores/trx_sid/rtl/trx_audio.v`,
`pins.xdc` / `c64_core_ooc.xdc` (timing), `tools/mmcm_drp_gen.v`. App: branch `t40-app`, one value.

Deviations from §9: none in function. `video_fmt` is not cleared by the RISC-V's reset (an app
reload does not switch to PAL and back). The DRP check (`mism`) compares every register read in a
switch with the table of the mode in use; the first switch thus compares Vivado's static PAL
setting with XAPP888's table (JTAG register 0x100030 bit 11, address bits 6:0, value in 0x100031
bits 31:16): a difference there is information (Vivado may pick other filter/lock values), not an error.

JTAG registers (`tools/trxdbg.py`, 0x100030-0x100033): 0x30 `{switches (8), state (4), requested
mode, vmode, cdce_v60, sys_en, mism, timeouts {lock/I2C, buses, halt}, 0, mism address}` (bits
29:0); 0x31 `{mism value, last switch in 20.48 us units}`; 0x32 `{lines per frame (9), dot4x clocks
per line (12)}` as the VIC runs; 0x33 dot4x clocks per frame.

### 10.1 Simulation results

| Bench | Result |
|---|---|
| `sim/ntsc/tb_mode_ctl.v` (mode_ctl + cdce_i2c vs an MMCM DRP/lock model) | PASS: PAL->NTSC->PAL-60->NTSC-50->PAL->NTSC; all 14 registers per mode as the table, kept bits and other registers untouched, writes only under MMCM reset, sys stopped and `sys_rst` masked for every unlocked clock, halt and both parkings stand throughout; synthesizer bytes 00 40 02 0A (50 Hz) / 3D E0 F3 CD (60 Hz) on the bus; read-back check clean; 14.06 ms per switch (10 ms of it cdce_i2c's settle wait) |
| `sim/tb_scaler.v`, PAL / `-DV60` | PASS both: 2688 / 2200 clocks per line, 1125 lines, 1920 x 1080 `de`, every window pixel the right VIC pixel (60 Hz: line 31 + 2y/9, wrapping at 263) |
| `sim/ntsc/tb_udp_stream.v`, PAL / `-DV60` | PASS both: 68 / 60 packets per frame, lines 0-271 / 0-239, pixels from X 104 and lines 15.. / 31..262, 0..7 |
| `sim/tb_app_i2c.v` (with hold) | PASS |
| `sim/lorenz` `VMODE=n tb_lorenz boot` + `tb_lorenz modes` | PASS all four: KERNAL boots to READY in every mode (2,163,267 cycles at 63, 2,153,204 at 65); `$02A6` = 1 PAL / NTSC-50, 0 NTSC / PAL-60 (the KERNAL's own detection); 312 x 2016 / 263 x 2080 dot4x clocks, 628,992 / 547,040 per frame; TOD tenth = 6 x 2 x f / (100 or 120) clocks within 0.15 %; jiffy clock at the KERNAL's rate. Live switches PAL->NTSC, NTSC->PAL, PAL-60->NTSC-50, NTSC-50->PAL-60 with a running probe: VIC restarts in the new geometry, the probe loop, jiffy and TOD go on, $D011/$D018/$D020 unchanged |
| decimator (`trx_audio`, `sim/sid/tb_audio` with `VMODE`) | 1 kHz tone, 3 M cycles per mode: output frames = the exact ratio (146,166 / 140,815 / 140,646 / 145,902), FIFO level constant 3. N/DEN equal phi2 / 16 / 48,003.07 Hz exactly in all four modes (Python, Fractions); PAL's phase product bit-identical to before |
| `sim/lorenz` subset (CPUPORT, IRQ, NMI, ICR01, CIA1TA, CIA2TB; 8521 model, Disk3 = old-CIA disk) | CPUPORT ok; the old-CIA tests fail with the same message and the same cycle count as on `main` before T40 (8521 model vs 6526 disk): no change |
| `sim/tb_bling.v`, `tb_udp_audio.v` | PASS. `tb_bling_io.v` (1 error, "flags unlocked") and `overlay_sim.py` (15 pixels at y 140) fail identically on `main` before T40 |

### 10.2 Board, first bitstream (run 314, 2026-10-03)

Timing met with the 30.52 ns sys constraint (WNS +0.181 ns). c64_basic 26,665 LUT (81.79 %),
27,961 FF, 60 BRAM tiles, 13 DSP, 14 BUFGCTRL; `main` at the same base 26,325 LUT, 27,536 FF:
**+340 LUT, +425 FF** (mode_ctl 131 LUT; the rest spread over scaler, encoder, stream, CIA/keyboard,
decimator). REST `System Mode` (memory only) switched PAL -> NTSC -> PAL-60 -> NTSC-50 -> PAL:
the VIC ran 263 x 65 / 312 x 63 as asked, the UDP stream came 384 x 240 / 384 x 272, the KERNAL's
jiffy clock went on, SID pitch in PAL as expected (433.5 Hz for 0x1CD6). But every switch reset the
C64 (BASIC start screen, probe gone): `mmcm_ref` took the gated `sys_clk` as its input, so it could
not lock while mode_ctl waited for it in S_LOCK; after the 84 ms timeout sys started, reconf was
released 64 clocks later with `ref_locked` still low, and `sys_rst` fired. Fixed in the next build:
`mmcm_ref` on `sys_free`, S_RUN waits for both locks. The DRP read-back check reports register 0x28
(power) = 0x9900 in Vivado's static setting (we write 0xFFFF, as XAPP888): information only.

### 10.3 Board, fixed bitstream (run 318, 2026-10-03)

WNS +0.162 ns; 26,730 LUT (81.99 %), 28,103 FF (+405 LUT, +567 FF over `main` 26,325 / 27,536,
incl. the HDMI measurement register). App: `t40-app` built in `u64-build-env`, loaded over JTAG.
`tests/t40_modes.py` (REST `System Mode`, memory only; PAL -> NTSC -> PAL-60 -> NTSC-50 -> PAL with
a probe running): **PASS**. Per switch 14.3-14.4 ms in mode_ctl (no timeouts), then the GTPs come
back 50 ms after the synthesizer.

| Mode | VIC (JTAG) | HDMI (JTAG 0x34) | Probe / jiffy / VIC regs | SID 0x1CD6 (UDP audio) | UDP frame |
|---|---|---|---|---|---|
| PAL | 312 x 63, 628,992 | 50 frames/s, 3,023,872 (= 2688 x 1125) | runs, runs, unchanged | 433.5 Hz (want 433.5) | 384 x 272 |
| NTSC | 263 x 65, 547,040 | 60 frames/s, 2,474,752 (= 2200 x 1125) | as above | 450.0 (450.0) | 384 x 240 |
| PAL-60 | 263 x 65, 547,040 | 60 frames/s, 2,474,752 | as above | 450.5 (450.5) | 384 x 240 |
| NTSC-50 | 312 x 63, 628,992 | 50 frames/s, 3,023,872 | as above | 434.3 (434.3) | 384 x 272 |

(HDMI pixel clocks per frame are read in units of 256.) The pixel clock counts exactly one VIC
frame per HDMI frame in both families, so the 990/263 synthesizer setting locks. After a C64 reset
in NTSC the KERNAL finds NTSC (`$02A6` = 0), in PAL PAL (1). NTSC stream: text area at stream rows
20-219, columns 32-351 (centred); `x_text` = 136 in NTSC as in PAL, so X0 = 96 and X_S = 104 hold.

Open: the picture on Alex's Acer monitor in each mode (R2: sink acceptance of 1080p60 from the
150 kHz PFD; 4.5x line pattern), M1-M7 against Gideon's core (not possible while the flash holds ours),
Lorenz in NTSC on the board (B6).

### 10.4 Monitor check by Alex (2026-10-03)

On the Acer, all four modes switched from the menu without saving: 1080p50 and 1080p60 accepted without flicker, borders symmetric, the overlay menu in the same place, HDMI audio clean, picture back within about a second after a switch. PASS. Merged to main with app `trx64/main` f364c03e.

### 10.5 Bug: the still capture had no 60 Hz window (found 2026-10-04, fixed on branch `still-60hz`)

Found by reading `pix_layer.v` (C64U_emu session): the still window was fixed at 400 x 270 from
line 16, a frame completed only at word 3,374, which needs raster line 285. In NTSC and PAL-60
(263 lines) it never completed: STILL_STATUS did not change, so T12's freeze showed no C64
picture, T20's Timewarp scrub no preview, and T24 §14 would have had no frame. §4.4 and §10 covered
the scaler only; the 60 Hz sim and board runs (§10.3) did not open the freeze menu.
Fix (T12 §5.2, 60 Hz paragraph): `pix_layer` gets `v60`, the 60 Hz still is 400 x 240 (3,000
words) of one VIC frame, STILL_STATUS b2 = 60 Hz. 50 Hz path bit-identical. App: the still
renderers draw 240 rows onto the 540-row layer.

## 11. CEA line length at 50 Hz (2026-10-05, branch `hdmi-cea50`)

**Why.** On Till's monitor (Eyoyo 4:3, HDMI) our 50 Hz picture was green all over with a colour gradient; Gideon's
3.15 on the same monitor put the overlay next to the picture. Our 50 Hz frame is 2688 x 1125 at 151.58 MHz,
2 % off the CEA 1080p50 timing (2640 x 1125, 148.5 MHz); a sink that matches timings against a table does not
find it. Gideon keeps the CEA structure and only detunes the clock: `sm_1080p50` = 148,869,856 Hz, 2640 x 1125
(fp 528, sync 44, bp 148), `software/u64/hdmi_scan.cc:65`. Not proven to be the cause; this change makes our
signal what Gideon sends, and Till's monitor is the test.

**Decision (Alex, 2026-10-05):** take Gideon's 50 Hz timing.

| | as built | new |
|---|---|---|
| frame (PAL, NTSC-50) | 2688 x 1125 | **2640 x 1125** (CEA 1080p50 blanking: fp 528, sync 44, bp 148) |
| pixel clock / dot4x | 125/26 | **6875/1456** (= 2,970,000 / 628,992, exact VIC lock) |
| pixel clock PAL | 151,575,855 Hz | **148,869,143 Hz** (Gideon's `sm_1080p50`: 148,869,856, 4.8 ppm higher) |
| pixel clock NTSC-50 | 151,850,103 Hz | **149,138,494 Hz** |
| CDCE pix / ref (ref = 125/104 x dot4x) | 4/1 | **55/14** |
| AVI VIC | 31 | 31 |
| 60 Hz modes | 2200 x 1125 | unchanged |

Line layout, active area kept at cx 768-2687 (the contract of `overlay.v`, `pix_layer.v`, `c64_text.v`,
`hdmi.v` data islands at cx 4-242), the same way the 60 Hz frame does it: cx runs 0-695, then jumps to 744 (the counter extent stays 2688, `H_TOT`; the jump removes 48 clocks);
front porch cx 0-527, sync 528-571, back porch 572-695 and 744-767 (720 blanking clocks).

CDCE: N = 55, M = 14 by the TI recipe of §4.4 (`N' = N x 2^(4-P)`, `Q = int(N'/M)`, `R = N' - M x Q`,
16 <= Q <= 63, R <= 51), VCO 148.9 / 149.1 MHz in the 125-150 MHz range (the 60 Hz setting's range). GTP line
rate 1.489 / 1.491 Gb/s, between the 60 Hz (1.481) and the old 50 Hz (1.516) rates.

Changes: `cdce_i2c.v` byte set for vmode 0/3; `vic_scaler.v` H_TOT/H_ACT0/sync per mode (50 Hz like 60 Hz with
other numbers; Y and the 4x/4x window unchanged); `top.v` `hdmi_rate` 0/3; constraints `hdmi_refclk` period if
it names 151.85 MHz as the fastest (60 Hz and the new 50 Hz are all slower: tighten nothing). Audio decimator,
UDP stream, CIA TOD, c64_core FRAME: unchanged (they follow the VIC, not HDMI).

Tests: `sim/tb_scaler.v` PAL: 2640 clocks per line, 1125 lines, `de` 1920 x 1080, every window pixel the right
VIC pixel, sync at cx 528-571; 60 Hz unchanged. Board: Acer at all four modes (picture, borders, overlay,
freeze still, HDMI audio, 50.12 / 59.83 frames/s); then Till's Eyoyo.

As built (2026-10-05, `070959f` `f0c9e39` `aef10cd`): CDCE 50 Hz bytes 0x18-0x1B `03 70 33 ED` (N 55, P 1 -> N' 440,
Q 31, R 6, VCO bits 01; checked by decoding the 60 Hz set `3D E0 F3 CD` the same way to N 990, Q 30, R 30);
`vic_scaler.v` H_JUMP 695, sync 528-571; `top.v` hdmi_rate 148,869,143 / 149,138,494. Benches: tb_scaler PAL
(2640 clocks per line, 1125 lines, 1920 x 1080 de, every window pixel right, hs at 528-571) and -DV60 PASS;
sim/ntsc/run.sh (mode_ctl with the new bytes, scaler both modes, udp_stream) PASS. Not re-checked: the GTP
wizard's generated settings at 1.489 Gb/s (60 Hz already runs 1.481), the CDCE PFD 2.7 MHz on the board.

Board (Alex, 2026-10-05, C64U on the Acer, SRAM, package hdmi-cea50-latest 4f03209, CI run 427 WNS +0.054): picture, overlay on top of the picture, NTSC switch: all clean. Open: Till's Eyoyo monitor (the reason for this change).

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

T40: NTSC: PAL/NTSC/PAL-60/NTSC-50 from the app's System Mode

Milestone M1, status ✅. Open (one line, as in the TODO table): Lorenz in NTSC on the board (B6); M1-M7 against Gideon's core; Till's monitor

Decisions: [ADR-0004 M1 release candidate for TREX-internal testers](../adr/0004-m1-release-candidate-internal-testers.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- **Board 2026-10-07 evening (Alex, Freeze UI; CI run 514, package integ-m1-e2b2ca47, app 301ef8c3, trxmon.u2a built at app 9a1b996b = same trxmon source as integ-m1-app): PASSED (T40-M)** `tests/t40_modes.py` PAL NTSC PAL-60 NTSC-50 PAL all PASS (CPU, jiffies, VIC registers, SID pitch, frame height); the first run failed only because mandelbrot had IRQs off (probe never started) - test setup, not a bug. HDMI: short black at each switch (monitor relock), Alex: ok. "DRP read-back differed: register 0x28 = 0x9900" is printed at each switch (not a failure). Not run: TOD per mode, Lorenz in NTSC (B6).
- Task text: NTSC: PAL/NTSC/PAL-60/NTSC-50 from the app's System Mode, switched live (docs/specs/T40-ntsc.md §9-10). Built on branch t40-ntsc (+405 LUT): MMCM DRP (four clocks), 1080p60 with 990/263 lock and x4.5, 240-line stream, TOD/decimator/UART per mode; app t40-app (default PAL). Sim and board pass (tests/t40_modes.py: all four modes, program survives, SID pitch, HDMI 50/60 frames/s exact). Alex checked on the Acer 2026-10-03: 1080p50/60, borders, overlay position, HDMI audio, recovery after a switch all fine. Merged to main with app trx64/main f364c03e; still-60hz fixed and merged. Left for later: Lorenz in NTSC on the board (B6); M1-M7 against Gideon's core need his bitstream in SRAM. 2026-10-04: bug found, the still capture (T12 freeze picture, T20 scrub, T24 §14) never completed at 60 Hz; fixed on branch still-60hz / still-60hz-app (T40 §10.5, T12 §5.2).
- **2026-10-05 (T40 §11, branch hdmi-cea50, in main):** 50 Hz HDMI frame now 2640 x 1125 at 148.87 MHz like Gideon's (was 2688 x 1125, 151.58 MHz) after Till's Eyoyo showed a green picture; Acer board test clean (picture, overlay, NTSC). Open: Till's monitor.
