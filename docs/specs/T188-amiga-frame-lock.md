# T188 — The Amiga cores can show a PAL frame every output frame at 50 Hz (options for an optional frame lock)

**Status:** IN PROGRESS (2026-11-10, Alex gave the go): D1/D2 built as data in the two Amiga core copies; D3 (O3, output follows the core, 2026-11-13) built and benched, no Vivado run yet; S1/S2/S4 wait for the board.
**Repo:** TRX64U-MultiCore — the Amiga cores (`cores/amiga`, `cores/amiga1200`), the host mode table (`host/hdmi_modes.vh`, `tools/hdmi_modes.py`), the core app (`coreapp/cores/amiga*.cc`), the manual.
**Number:** T188 (registry: `TODO.md`).
**Origin:** GitHub issue #48 (tester, Pinball Dreams hitch at 1080p); Alex, 2026-11-06: "analyse what is possible for proposal 2" (translated). Proposal 1 of the issue is a manual note, proposal 2 an optional frame lock like the ZX Next's.
**Depends on:** T107 (A1200 output modes), T85 (Amiga core, `amiga_ce`), T88 D5 (Atari decision, 2026-10-14), T116 §5.1.3 (ZX Next lock), T164 (host table).
**Milestone:** M2.


---

## §1 The problem

- **Who:** a tester playing full-screen vertical scrollers (Pinball Dreams) on the A1200 core (Kickstart 3.1) and the A500 core (Kickstart 1.3), C64U, HDMI 1920x1080 Scale Integer, Smoothing off.
- **What they see:** a short hitch about every 10 s on the A1200 at 1080p, rarer on the A500, none noticed at 800x600. Observed by eye, not measured (issue #48).
- **Why:** the Amiga frame is written into a DDR2 buffer by the core and read by the host's pixel layer at the output rate. The output rate comes from the CDCE ratio of the mode and is free-running (T107 "Frame lock", `docs/specs/T107-application-owns-the-picture.md:286`). Two unrelated rates give a beat: once per 1/|f_out - f_core| seconds the layer shows one frame twice (or skips one). On a vertical scroller that is the hitch.

**The numbers (derived, formula `T_repeat = 1 / |f_out - f_core|`):**

- `f_core` = 28,375,160 Hz / 8 (CCK) / (227 CCK x 312 lines) = **50.0804 Hz** (from the crystal ratio; the code's sys_clk gives 50.080128 Hz, see §5 2026-11-10) (T85 D3: 28.37516 MHz = `sys_clk` x 9/10, `cores/amiga/rtl/trx/amiga_ce.v:46` `+9, fire at >= 10`). The elastic stall is paid back by the debt counter (`amiga_ce.v` header), so the average is this value as long as stalls stay under 10 % of the clocks.
- `f_out` from `python3 tools/hdmi_modes.py table` (A1200 edition, 12 rows):

| mode | f_out Hz | f_out - f_core Hz | repeat every | machine speed that would match |
|---|---|---|---|---|
| 576p / 720p / 1080p 50 | 50.1246 | +0.0442 | **22.6 s** | +0.088 % (+1.5 cent) |
| 800x600 50 | 50.000 | -0.0804 | **12.4 s** | -0.161 % (-2.8 cent) |
| 1024x768 50 | 50.017 | -0.0634 | 15.8 s | -0.127 % |
| 1280x1024 50 | 49.998 | -0.0824 | 12.1 s | -0.165 % |
| any 60 Hz mode | 59.82-60.04 | about +9.8 | 5 of 6 frames are shown, 1 of 6 twice, every 6th frame: 10 Hz judder, not a beat | not lockable (50 Hz machine) |

- **The derived numbers do not match the observation.** 1080p should repeat every 22.6 s, not 10 s; 800x600 should be worse (12.4 s) and was not noticed; the A500 should equal the A1200. So one of these is not what the table says: the measured core frame rate (stall losses beyond the debt limit, the `st_debt` counter of `amiga_ce.v`), the repeat being invisible in some picture phase, the write/flip phase of `amiga_fb` (the app flips `wb_idx` on the frame interrupt, `amiga_fb.v:12`), or the A500 using the host's mode set instead of the A1200 set (`host/hdmi_modes.vh` has 9 rows, no 800x600/1024x768/1280x1024 at 50 Hz; whether the A500 offers 800x600 50 Hz is **not checked**). **Nothing below is worth building before the real beat is measured (S1).**

**Parts (MECE):**
- P1 What the beat really is (core frame rate and output rate, as measured, per core and mode).
- P2 What a lock changes in the machine (speed, pitch, disk, CIA TOD).
- P3 What it costs in the core and in the host (LUT, timing, files).
- P4 Which HDMI modes it covers.
- P5 What the user is told (setting, manual).

## §2 Scenarios — what proves it

```gherkin
Scenario: S1 the beat is measured
  Given amiga1200 and amiga on the board, Pinball Dreams scrolling, 1080p50 Integer, then 800x600 (A1200)
  When  the core's frame counter (amiga_fb `frames`, 16 bit) and the host's output frame counter are read by REST over 120 s
  Then  f_core and f_out are printed per mode and the repeat interval 1/|f_out - f_core| is compared with the tester's ~10 s
  And   if they differ by more than 20 %, the cause (stall debt lost, flip phase) is found before any option is built
```
```gherkin
Scenario: S2 the lock holds
  Given an option of §6 built
  When  the same counters run 600 s in every covered mode
  Then  the frame counts differ by at most 1 (a lock) or the beat period is > 10 min (a near-lock, option B)
  And   control: the lock switched off gives the S1 beat again (a bench that cannot show the beat proves nothing)
```
```gherkin
Scenario: S3 the machine stays correct
  Given option A built, lock on, 600 s run
  Then  the CIA TOD / frame-timed test ROM of `amiga_equiv` shows the speed change equal to the programmed one (+0.088 % within 20 ppm) and nothing else moved
  And   Paula pitch (T98 measurement) shifted by the same ratio
```
```gherkin
Scenario: S4 the screen takes the mode
  Given option B built (a 1126-line frame, or a retuned CDCE row)
  When  1080p50 / 720p50 / 576p50 are selected on the monitors Alex owns (TV, monitor, capture)
  Then  every one keeps the picture, no re-sync flicker, audio stays (board, photo)
```
```gherkin
Scenario: S5 area and timing
  Then  the core stage report of amiga and amiga1200 shows the LUT/FF delta below and WNS of `pr_verify` not worse than before
```

| # | Solves | Test | Where |
|---|---|---|---|
| S1 | P1 | REST read of both counters on the board; the frame counter exists (`amiga_fb.v:38`), the output counter may need a host read (to be checked) | board |
| S2 | P4 | same, 600 s per mode | board |
| S3 | P2 | `cores/amiga/sim/run_rate.sh` extended with a lock step; equiv bench for the machine itself | sims.txt tier 2 |
| S4 | P4 | by hand, per screen | board |
| S5 | P3 | Vivado report line | CI core stage |

## §3 Decision

- **D1 (Alex, 2026-11-06: "No, B is great?!", translated):** option B, the output follows the Amiga frame through the mode table
  (B1 for 1080p, B2 where a CDCE retune is needed). Open with D1: today's 50.1246 Hz rows match the C64's PAL frame (50.1245 Hz), so the
  Amiga rows must be a second row set used while an Amiga core runs, not a change of the shared rows (§6).
- **D2 (Alex, 2026-11-06: "make it automatic for the Amiga", translated):** an Amiga core (amiga, amiga1200) uses the Amiga row set by
  itself, no setting; every other core keeps today's rows.
- **D3 (Alex, 2026-11-13: "Hm ... Ja", translated; after the board S1 of §5 2026-11-13 showed the core at 49.90 Hz, not 50.0801 Hz):** option O3 -
  the output follows the core's frame instead of the fixed exact lock of D1. It is built in the Amiga cores' own copies of the host files
  (`cores/amiga/mono/host/`, `cores/amiga1200/mono/host/`, both large/mono cores); the frozen host (`host/`) is not touched.
- **D4 (Alex, 2026-11-13, "Ja" to: "the three Amiga modes are recomputed for 313 lines (49.92 Hz) before Vivado runs, so the follow only swings around 0-1 line", translated):** the Amiga rows 0..2 (576p50, 720p50, 1080p50) of both Amiga copies' `hdmi_modes.vh` target the non-interlaced PAL Amiga frame, 227 x 313 CCK (`cores/amiga/rtl/src/minimig-aga/agnus_beamcounter.v` `long_frame`), instead of 227 x 312. Answers question (a) of §5 2026-11-13 (later).

## §4 Out

Out: a 60 Hz lock (a PAL machine cannot match 60 Hz), interlaced/NTSC Amiga, a core-side scaler (T107 §3 4a).

## §5 Decisions taken while building

**2026-11-10 - how D1/D2 are built (read from code, not from the §1/§6 prose):**
- **Where the row set lives.** `cores/amiga/mono/host/` and `cores/amiga1200/mono/host/` are the cores' own copies of the host files (T164 3a Q1, `FROM`), tied to `c64_type 1'b0` in their `top.v`. So the "second row set used while an Amiga core runs" is the Amiga copies' own `hdmi_modes.vh`: rows 0, 1, 2 (576p, 720p, 1080p at 50 Hz) bent; the host, `host/hdmi_modes.vh`, and the other cores stay byte for byte as they were (D2). No selection logic, no app change, no new gateware logic: the host mode request (0x4F00) already picks rows 0..2 as before.
- **Better than B1/B2: an exact lock.** The core frame is `dot4x x 9/10 / (8 x 227 x 312)` (`amiga_ce.v` `+9, fire at >= 10`; `top.v` sys_clk = dot4x = 50 MHz x 56.75 / 90), and the TMDS clock is `ref x N/M` with `ref = 125/104 dot4x`: both come from one MMCM. So `gear x htot x vtot x f_core / ref` is a ratio of integers, and for some rasters N <= 4095, M <= 511 holds and the CDCE register rules accept it: the HDMI frame is then exactly the Amiga frame, fixed phase, no beat (not B1's 42 min). Note `f_core` is 50.080128 Hz from the code, not 50.0804 Hz of §1 (that used the crystal ratio 16/9 of the comment: 5.8 ppm apart, which would have been a 3 h beat in the wrong direction).
- **The rasters** (search over htot -60..+100 and vtot -2..+25 for the smallest deviation from the CEA raster with an exact N/M; `tools/hdmi_modes.py amiga-table`):

| row | raster | change vs CEA/host row | N/M | TMDS clock | pixel clock | f_out | residual beat |
|---|---|---|---|---|---|---|---|
| 576p50 (g 5) | 880 x 625 | hfp 12 -> 28 | 825/227 | 137.720353 MHz | 27.544071 MHz | 50.080128 Hz | none (exact) |
| 720p50 (g 2) | 1960 x 750 | hfp 440 -> 420 | 882/227 | 147.235577 MHz | 73.617788 MHz | 50.080128 Hz | none (exact) |
| 1080p50 (g 1) | 2640 x 1135 | vfp 4 -> 14 | 99/25 | 150.060096 MHz | 150.060096 MHz | 50.080128 Hz | none (exact) |
| host rows today | CEA | - | 55/14 (1080p) | 148.869143 MHz | - | 50.1246 Hz | 22.6 s |

  The host table has no 50 Hz PC rows (800x600 / 1024x768 / 1280x1024 50 Hz of §1 were removed by T164 Q5), so there is nothing more to bend; the 60 Hz rows stay (a 50 Hz machine cannot match them). "Exact" is the arithmetic of the clocks; what the machine loses to stalls beyond the debt limit (`st_debt`) is the only remaining difference and is what S2 measures.
- **Deviations the monitor must accept (S4, board):** 1080p pixel clock +1.05 % against the 148.5 MHz of CEA VIC 31 (today's row is already +0.25 %), vtot 1135 instead of 1125; 720p htot 1960 (-1 %); 576p htot 880. Pixel clocks against CEA: 576p 27.544 MHz (+2.01 % of 27.000), 720p 73.618 MHz (-0.85 % of 74.25), 1080p +1.05 %; 576p is outside the usual +-0.5 % sink tolerance, so S4 checks 576p first (review 2026-11-10). S1/S2 measure a non-interlaced screen (interlace is out). The AVI InfoFrame keeps the VIC of each row (17, 19, 31). If a sink objects to a VIC with a raster of its own, the fallback is VIC 0 for these three rows (aspect kept): not done unless S4 shows it.
- **Tool:** `tools/hdmi_modes.py amiga-gen | amiga-check | amiga-table` (`check` and `selftest` run it, so lint does); `AMIGA_RAW` holds the three porches. The generated file's lines for rows outside T188 are checked equal to the host's.
- **Benches:** `host/sim/hdmi_timing/run.sh` and `host/sim/modepath/run.sh` get `-DT188` for `HOSTDIR=cores/amiga*/...` (expected rasters, frame rate 50080, the three PLL byte sets); the Amiga copies are never C64-type, so the `+c64=1` runs are skipped for them.
- **S1 readout:** the Amiga core app prints every 10 s `[amiga] frames core <n> out <n> at <ms> ms` (core: `amiga_regs` 0x2A / byte 0xAA; output: `pix_layer` n_frames, byte 0x60A6); `tools/t188_measure.py` turns those lines into f_core, f_out and the beat (`tools/trxdbg.sh console follow | tools/t188_measure.py`). No gateware for the readout. Not run on the board (Alex has it).
- **Results (2026-11-10, no board):** `python3 tools/hdmi_modes.py check` -> `PASS hdmi_modes amiga: rows 0..2 run exactly the Amiga frame 50.080128 Hz`; `... selftest` -> `host selftest: 27 of 27 mutations caught` (two new: one more 1080p line, a 60 Hz row changed in the Amiga copy). Builder, `trx64-sim-env:4`, `HOSTDIR=cores/amiga1200/mono/host sh host/sim/hdmi_timing/run.sh` -> `PASS hdmi_timing (9 modes exact, align on 9 rows, 7 refused)`; `HOSTDIR=cores/amiga1200/mono/host sh host/sim/modepath/run.sh` -> `PASS modepath` (rows 0/1/2 send 33908bad / 37209bed / 06308bee). `tools/dev.sh test --branch t188-framelock` -> `=== apptests ok (tier fast)`; `tools/dev.sh riscv --branch t188-framelock` -> `=== dev.sh riscv: ok`. The `amiga` copy equals the `amiga1200` one (`amiga-check` reads both); only the A1200 copy was simulated.
- **Needs a Vivado run:** `amiga` and `amiga1200` (their `hdmi_modes.vh` changed). The host and every other core: no change.

**2026-11-13 - board S1: the core runs 0.36 % slow, the chipset clock loses ticks permanently (analysis from code; nothing built). SUPERSEDED by the next entry (the stall explanation below is not the cause; the arithmetic of `amiga_ce.v` is right but not what limits the rate).**
Report line (A1200, release 736c16d9, AmigaVision HDF, Pinball Dreams, 1080p50 with the exact lock; wall clock and app ms agree), 70 s of steady play:
`[amiga] frames core 8061 out 0 at 633121 ms` / `core 8560 ... at 643130 ms` / `core 9059 ... at 653130 ms` (about 499.5 frames per 10.009 s = 49.90 Hz, nominal 50.080128) and
`[amiga] audio over 0 under 41, fill now 479 min 1 max 488 (target 480), catch-up lost 65535 ticks, deepest debt 4095`. The output is exactly 50.080128 Hz, so one frame repeats every ~6 s (1 / 0.18 Hz = 5.6 s): the hitch. fbwin did not move (not the cause). The A500 (`cores/amiga`) was not measured; it runs the same `amiga_ce.v` and the same `amiga_mem.v` design.
- **The only place a tick is lost** is `amiga_ce.v` `if (fire && CATCHUP) { if (&debt) lost++ ... }`: a due tick that meets a blocked clock and a full debt counter. `pause` holds the accumulator (nothing lost). So 49.90 Hz = ticks dropped there, permanently. `st_debt` = `{lost (16 bit, saturating, never cleared), 4'd0, debt_max (12 bit, never cleared)}`, printed by `coreapp/cores/amiga.cc:158-165` (`am_stat(c, 11)`; `cu >> 16`, `cu & 0xFFF`). **Both counters are sticky since power-up**: at ~100k lost ticks per second (below) `lost` saturates in under a second, `debt_max` 4095 on the first long stall, so the two numbers say "it happened", not how often; the frame counters are the rate evidence.
- **Why the debt cannot be repaid (arithmetic of the code).** `ce28` is one pulse per `sys_clk` at most; the nominal rate is 9 ticks in 10 clocks (`acc` 4 bit, +9, fire at >= 10). A tick is made only in a clock that is not blocked (`block = pend & (phase ...)`), and debt is paid in the clocks where `fire` is 0 (`ce28 <= fire | |debt`, `debt--`), i.e. at most 1 extra tick in 10 clocks. Ticks per frame = clocks per frame - stalled clocks, so the nominal 566,592 ticks/frame (227 x 312 CCK x 8) need stalled clocks <= 10 % of the 629,547 sys_clk clocks per frame (31.5278 MHz / 50.080128 Hz) = **62,955 clocks (2.0 ms) per frame**. Above that no catch-up of any width can hold the rate: the debt (12 bit = 4095 ticks = 144 us, about 2.3 lines, repaid in 40,950 clocks = 1.3 ms) only absorbs bursts. A 0.36 % shortfall means the stalls averaged **about 10.3 % of the clocks (about 65,000 clocks = 2.06 ms per frame, 3 % over the budget)** over the play (shortfall 0.0036 x 0.9 ticks/clock = stall duty - 0.10). The bench `cores/amiga/sim/run_rate.sh` already ends at this wall (its LATS=60 case: stalls about 8 %, 0.8 % behind).
- **Who stalls (`amiga_mem.v` `pend = chip_pend & ~answer_now`, identical logic in both cores; the A1200 copy only widens the address for Z3):** (1) a chip RAM read that misses the 32 KB direct-mapped cache (2048 lines x 16 bytes; hit answers in `S_LOOK`, no stall): `S_LOOK` -> command FIFO -> `ui_clk` (75 MHz = 2.38 ui clocks per sys clock) -> DDR2 BL8 -> `crit` word back -> `S_RELOOK`; the bench figure is 14.3 stall clocks per miss at `RD_LAT 20` (board Turrican 2: 4.6 % stalled; `run_rate.sh` header), the real value on the A1200 is not measured. (2) A chip write while the 8-entry command FIFO is full (`cf_full`, write-through, one DDR2 write per chip write). (3) Queueing: a chip request that arrives while `st != S_IDLE` (a fast RAM / Z3 request of the 68020 or a prefetch lookup in the request machine, one at a time) waits in `q_*` (`take_chip && st != S_IDLE`) and `pend` stays up: CPU fast RAM traffic stalls the chipset only by occupying the single request machine (a fast RAM access itself does not raise `pend`; the CPU waits on `fastram_ready`). (4) DDR2: `ddr2_port.v` serves `c_*` before every other client (`cart = ok && c_en`), the frame writer `amiga_fb` and the disk/HDF path `amiga_sdc` use `e_*` (`amiga_core.v:255`) and can only delay `c_*` through the DDR2 controller's bank timers and refresh (`ddr2_lean.v`: refresh every 560 ui clocks = 7.47 us, tRFC 127.5 ns = at most 1.7 % of DDR2 time). The frame writer is not a stall source on the port; it is not measured on the controller.
- **Per frame (derived, bench figures, not board):** budget 62,955 stall clocks. At 14.3 clocks per miss that is 4,400 misses per frame, 3.1 % of the 141,648 chip bus cycles of a frame (4 ticks each); the loss implies about 4,540 missing reads at that latency (3.2 %). The board's actual split (misses, stall clocks per miss, writes blocked) is **not known**: the counters exist (`s_hold`, `s_miss`, `s_reads`, `s_writes`, `s_age`; `amiga_core.v:158-165`, `AM_STAT_SEL` 0..) but the 10 s line prints only audio and `st_debt`.
- **Shared file, reported not changed:** `cores/amiga1200/core_sources.tcl:28-30` takes every file of `cores/amiga/rtl/trx/` except the six in `a12_trx`; `amiga_ce.v` is not in that list, so **the A1200 core reads `cores/amiga/rtl/trx/amiga_ce.v` in place** (also `amiga_audio`, `amiga_rtc`, `trx_afifo`, `trx_tdp16`, `amiga_eport`). A change to `amiga_ce.v` is a change to both cores until an own copy is made (rule: copies, not links).
- **Options (nothing built; each one changes what the machine does, so the choice is Alex's):**
  - **O1 fewer/shorter stalls (keeps the exact machine).** Lower stalled clocks per frame below 62,955 with margin: a second prefetch line / a stream detector for the bitplane and copper streams, 2-way associativity of the cache (conflicts), deeper command FIFO. Cost: block RAM (a 2-way 32 KB cache is a second tag and data set, the A1200 has the area budget of T97 5.11 to check), tens of LUT, `sys_clk` paths in `amiga_mem` (timing risk low-medium). Effect: no speed change, no cycle change; but it moves the wall, it does not remove it: a worse game puts the rate under again. Needs the measured miss/stall split first.
  - **O2 CPU waits instead of the whole machine.** A chip RAM miss of the 68020 holds only the CPU (`chipready`, `cpu_clkena` in `cpu_wrapper.v`, as the Minimig does with SDRAM) and the chipset keeps ticking; only chipset DMA misses (sequential, prefetchable) stall everything. Effect: the chipset (display, Paula, CIA, disk) keeps real time; the CPU gets slower under load (like a real A1200 hitting a busy bus, but not cycle-exact against the real bus order). Cost: `cpu_wrapper`/`nanomig` change, cycle accuracy of the CPU/chip interleave (the 7 MHz slot order) at risk: big, needs the equiv bench. **Whether the Minimig chip bus can complete a CPU slot late is not read yet: open question.**
  - **O3 let the output follow the core.** Measure f_core and steer the output frame rate (a line-count loop): the frame lock then follows 49.90 Hz instead of 50.0801 Hz. **Chosen by D3 (2026-11-13).** It is not a host change: both Amiga cores carry their own copies of the host files (`cores/amiga*/mono/host/`, where the T188 rows already live), the frozen `host/` stays as it is. It replaces the exact-ratio design of 2026-11-10 as the means, the average output rate varies with the game.
  - **O4 accept the loss, hide the repeat.** Nothing in the core; no.
  - **Not an option:** a wider debt, a faster catch-up, or "paying" more per clock: `ce28` cannot tick more than once per clock, and `sys_clk` is the host's C64 clock (10/9 of the nominal rate by `amiga_ce.v`'s construction). So there is no clear repair inside the existing design intent (the header's own limit is the 10 % wall): **stopped here, nothing built (no bench: a bench of `amiga_ce` alone would only restate the arithmetic above; `run_rate.sh` is the whole-machine bench and needs the stall mix of O1/O2 to be of use).**
- **Questions for Alex / what is missing:** (a) Measure first: add `s_hold`, `s_miss`, `s_reads`, `s_writes` per 10 s window (deltas) to the `[amiga]` console line (app only, no gateware) and read it with Pinball Dreams, then Turrican 2 / a plain Workbench: is the 10.3 % misses or queueing behind fast RAM? (b) O1, O2 or O3? (c) Does the A500 core show the same (needs the same line on its board)? (d) Make an own `amiga_ce.v` copy in `cores/amiga1200/rtl/` before any change to it?

**2026-11-13 (later) - the cause of the rate gap is the 313-line frame, not stalls; O3 built (D3).**
Report line (Alex's board, A1200 with the stall readout, Pinball Dreams scrolling, frame counter timed with the Mac clock): `mac 170.035s app 170.109s frames 8492 -> mac 49.9427 Hz` (the app clock agrees within 0.04 %); chip RAM stall per 10 s window 4.34-5.30 % of sys_clk (budget 10 %). So the lost-tick explanation of the entry above does not hold (stalls are half the budget).
- **Cause from code:** `cores/amiga/rtl/src/minimig-aga/agnus_beamcounter.v` `long_frame` is 1 after reset and in a non-interlaced screen the frame is `vtotal + 1` = **313 lines** (312 only while a program has cleared LOF with a VPOSW write; `last_line = long_frame ? extra_line : vpos_equ_vtotal`): 28,375,160 / 8 / (227 x 313) = 49.92 Hz like a real PAL Amiga, not 50.0801 Hz (227 x 312). Measured 49.943 Hz is a mix of about 14 % 312-line frames among 313-line ones (or clock tolerance, not separated). **The target of the exact rows of 2026-11-10 (50.080128 Hz from 312 lines) was wrong for a non-interlaced screen**; they stay as they are (they are the centre of the follow at E = 0, a 313-line machine runs at E = +3.6 lines at 1080p), whether to re-target them at 313 lines is a question (below).
- **Built (D3, O3), in both Amiga cores' own copies only:**
  - `cores/amiga/mono/host/hdmi_timing.v` = `cores/amiga1200/mono/host/hdmi_timing.v` (one text, `tools/hdmi_modes.py amiga-check` compares): parameter `FOLLOW` (0 = the host's generator, the bench of the table), ports `core_tgl` (in) and `follow_e` (out). Rows 0..2 only (`m < 3`; the 60 Hz rows and everything not followed run E = 0).
  - Control law: at every frame start the phase `age` (output lines since the last core frame event, counted at each line end) is compared with the set point S = 21/32 of the output frame (`vtot x 21 >> 5`: 13 ms of 20 ms; 744 / 492 / 410 lines at 1080p / 720p / 576p); the next frame gets `E = clamp(-round((age - S) / 4), -4, +16)` lines. E > 0: that many extra lines repeating the last front porch line (cy = vfp - 1; cy 0, 1 and every active line are never repeated), E < 0: that many lines taken out of the back porch right after the sync pulse (cy values vfp + vsync ... skipped). hs, vs, de, the encoder's `ey` and the active lines are unchanged. The phase is a proportional loop with a pole at 0.75 per frame: the steady offset is 4 x E_needed lines (E = +4 at 1080p 49.90 Hz: 16 lines = 1.4 % of a frame), so the set point is held to within 1-2 % of a frame whatever the rate; a dead band or an integral term was tried first (E stepping one line per frame) and oscillated (E -4 .. +13 at 49.90 Hz), a double integrator.
  - Why S = 21/32 (three buffers, software flip): the core frame is complete (`frame_done`, `amiga_fb` after the last line was written) at t_d; the app polls `seq` every 5 ms (`Video::poll`, service_period 5 ms), reads `FB_LATEST` and writes `PX_BACK` (flip pending) at t_d + L, L = 0 .. about 6 ms (the bench uses 2-30 % of a frame); `pix_layer` flips at the next output frame start T_k and `amiga_regs` `shown <= committed` then. A frame is shown exactly once when T_k lies after t_d + L_max and before t_d + P_core + L_min: the window is (6 ms, 20 ms); S in the middle is 13 ms. The writer takes the buffer not shown, not committed, not latest (`pick`), so with three buffers a flip at that phase never meets the buffer being written (the writing frame starts at the core's vblank end, the shown one is two frames old).
  - The core event: `amiga_fb.frame_done_tgl` (sys_clk toggle) is carried out by `amiga_core.v` (new output `frame_done_o`, both copies, `cores/amiga1200/rtl/amiga_core.diff` regenerated) and `layer2_amiga.v` on the idle port `drive_dbg[255]` (**Amiga-only convention on an existing port, the contract `layer2_ports.txt` and its VERSION are untouched**; the other cores drive 0, the debug read of word 7 bit 31 of the drive map toggles in the Amiga cores); `top.v` of both copies passes it to `hdmi_out.v` (`core_fr_tgl`), which gives it to `hdmi_timing` (3-flop crossing into the pixel clock).
  - Safety: no core event for three frames (a Freeze, the core paused or in reset) = E 0 at the next frame start; E only changes at a frame start; `tools/hdmi_modes.py amiga-check` also checks the porch room (back porch - 2 >= 4 skipped lines, front porch >= 3), the wiring text and that the two copies are equal.
- **Bench** (`HOSTDIR=cores/amiga1200/mono/host sh host/sim/hdmi_timing/run.sh` on the builder, `trx64-sim-env:4`; `host/sim/hdmi_timing/tb_follow.v`, the real `hdmi_timing` on a reduced table of 24-pixel lines with the real vertical rasters, a core frame source with jitter 0.2 % and an application with a flip latency of 2-30 % of a frame; 140 frames of lock-in, then 700 output frames = 700 core frames; the unchanged 9-mode/align/refused bench runs first with FOLLOW = 0):
  `fc=49.900 mode=2 follow=1 frames=701 core_frames=700 repeats=0 skips=0 fe=[3..6] fe_end=4 len_bad=0/838`
  `fc=49.920 mode=2 follow=1 frames=701 core_frames=700 repeats=0 skips=0 fe=[2..5] fe_end=4 len_bad=0/838`
  `fc=50.080 mode=2 follow=1 frames=701 core_frames=700 repeats=0 skips=0 fe=[-1..1] fe_end=0 len_bad=0/838`
  `fc=50.200 mode=2 follow=1 frames=701 core_frames=700 repeats=0 skips=0 fe=[-4..-1] fe_end=-3 len_bad=0/838`
  `fc=49.600 mode=2 follow=1 frames=701 core_frames=700 repeats=0 skips=0 fe=[10..12] fe_end=11 len_bad=0/838`
  `mix 14 % 312-line frames: fc=49.920 mode=2 follow=1 frames=701 core_frames=700 repeats=0 skips=0 fe=[1..5] fe_end=3 len_bad=0/838`
  `mix 50 % 312-line frames: fc=49.920 mode=0 follow=1 frames=701 core_frames=700 repeats=0 skips=0 fe=[0..3] fe_end=1 len_bad=0/838`
  `fc=49.900 mode=0 follow=1 frames=701 core_frames=700 repeats=0 skips=0 fe=[1..3] fe_end=2 len_bad=0/838`
  `fc=49.900 mode=1 follow=1 frames=701 core_frames=700 repeats=0 skips=0 fe=[2..4] fe_end=3 len_bad=0/838`
  `fc=49.900 mode=8 follow=1 frames=41 core_frames=40 repeats=0 skips=0 fe=[0..0] fe_end=0 len_bad=0/178`
  `control (follow off): fc=49.900 mode=2 follow=0 frames=701 core_frames=697 repeats=38 skips=35 fe=[0..0] fe_end=0 len_bad=0/838`
  `control (follow off): fc=50.200 mode=2 follow=0 frames=701 core_frames=701 repeats=20 skips=21 fe=[0..0] fe_end=0 len_bad=0/838`
  verdict: `PASS hdmi_timing follow (no repeat, no skip at 49.60 .. 50.20 Hz, limits kept, control shows the beat)`, then `PASS hdmi_timing (9 modes exact, align on 9 rows, 7 refused)` (FOLLOW = 0 part unchanged). follow_e needed: 1080p 49.90 Hz +4 (measured 3..6), 49.92 Hz +3.6 (2..5), 50.08 Hz 0 (-1..1), 50.20 Hz -2.7 (-4..-1, the lower limit is reached by 50.2 Hz at 1080p), 49.60 Hz +11 (10..12); the 312/313-line mix (14 % and 50 %) stays inside 0..5. `len_bad=0`: every frame is exactly the table's length plus follow_e. The 60 Hz row 8 stays at E 0. Control (FOLLOW = 0): 38 repeats and 35 skips in 700 frames at 49.90 Hz, 20 / 21 at 50.20 Hz (the random flip latency makes the passing phase window glitch both ways).
  `HOSTDIR=cores/amiga1200/mono/host sh host/sim/modepath/run.sh` -> `PASS modepath` (unchanged). `python3 tools/hdmi_modes.py check` and `selftest` (27 of 27) green; yosys read of `hdmi_timing.v`, `hdmi_out.v` and `layer2_amiga.v`: no implicit net. Only the A1200 copy was simulated; the two `hdmi_timing.v` are one text (the check compares them).
- **Not covered by the bench / board:** the real raster width (the follow works in lines; the full-size rows are the unchanged first part), how the sinks (TV, monitor, capture) take a frame length that changes by up to 16 lines per frame (S4, board), the real flip latency of the app (Pinball Dreams, S2), what `pix_layer` / `vic_scaler` do with a repeated cy of the front porch (read: nothing keys on a cy between 2 and yact0 - 1 apart from `px_vblank`, not simulated at full size).
- **Cost (yosys, abc -lut 6, FOLLOW 1 against FOLLOW 0 of `hdmi_timing`; Vivado packs tighter):** +231 LUT, +73 FF per Amiga core (the 60 Hz and every other path unchanged). Vivado: not run.
- **Needs a Vivado run:** `amiga` and `amiga1200` (hdmi_timing, hdmi_out, top, amiga_core, layer2_amiga changed). Host and every other core: no change.
- **Questions for Alex:** (a) re-target the three exact rows at 313 lines (49.92 Hz, a new `amiga-gen` target) so that E sits near 0, or keep 50.0801 as the centre? (b) limits -4 / +16 lines per frame and S = 21/32: wider/narrower? (c) a readout of `follow_e` (and the phase error) for the board S2/S4 needs a register in `hdmi_out` and a line in the app's `[amiga]` output; not built. (d) a switch in the app to turn the follow off (the bench's control is a build parameter only).

## §6 Options (for Alex)

(Superseded: D1 chose B; it was built as an exact lock in the Amiga copies, see §5 2026-11-10. The options below are the analysis of 2026-11-06.)

**What exists to build on (read from code):**
- `amiga_ce.v:46` the chipset clock is a **4-bit** accumulator, 9/10 fixed. A lock needs a wider NCO there (A).
- ZX Next (`cores/zxnext/trx/zxn_core.v:136-143`): a 32-bit NCO whose step is chosen from a fixed list (`lock_off`, `mach_timing`); the register `LOCK` (`zxn_regs.v:14`) is written by the app setting "Frame lock to 50 Hz" (`coreapp/cores/zxnext.cc:91`). The Next locks to the **fixed** 50.1246 Hz of the VIC-locked scaler, not to a measured output; its default is **On** (value 0 = locked, `zxnext.cc:49`), the issue's "default off" is not what the code does. Its lock costs the Next +0.09 % to +2.66 % speed (T116 5.1.3); `zclk` is 53 LUT / 50 FF measured (T116 §5.1.2 table).
- Host mode rows are `tools/hdmi_modes.py` targets; the CDCE N/M per row is derived there, errors today -65 to +580 ppm (`table`).

### A — the core runs at the output rate (NCO, speed change)
- **Mechanism:** replace the 9/10 accumulator by a 32-bit NCO with a step per mode, written by the app (new `LOCK`/`STEP` register in `amiga_regs`, one step per mode, six for the 50 Hz modes). Rate ratio f_out/f_core; the machine is exact against the output because both come from the PAL crystal (`sys_clk` = 17.734475 MHz x 16/9, the CDCE input is the same chain), so the beat is zero in principle, not 'small'. Lock off = today's 9/10 exactly (step constant).
- **Cost (estimate):** about 50-80 LUT and 40 FF per core (`zclk` is 53/50), one register, the app table and a setting; no host change, no new files in the host. Timing: `ce28` already fans out to 58 modules (`thread_ce28.py`), a wider accumulator ahead of it is not on that path; risk low, S5 decides. The catch-up debt logic (`amiga_ce.v`) works unchanged, it counts ticks.
- **Sees/hears:** machine +0.088 % at 1080p/720p/576p 50 (pitch +1.5 cent, TOD 76 s/day fast), -0.16 % at 800x600, -0.13 % at 1024x768, -0.165 % at 1280x1024 (-2.8 cent). Disk and Paula follow the clock. Far under the Atari's rejected +0.53 % / +9 cent (`docs/todo/T88.md:45`) and the Next's +1.55 %; the T88 argument (pitch/SIO exactness) is much weaker here, but is still Alex's to weigh. A game that counts frames sees every frame shown once.
- **Modes:** all 50 Hz modes (6 of the A1200 table; for the A500 the 50 Hz rows of the host table). **Not** the 60 Hz modes.
- **Opt-in:** a setting "Frame lock" in both apps; default off keeps the exact machine.

### B — the output follows the core (timing, no machine change)
- **B1, static custom row:** give the 50 Hz rows a frame length that equals the Amiga frame. Computed: pixel clock / (htot x f_core) is **1125.99 lines at 1080p** (today 1125): one extra line, vtot 1126, gives 50.0804 Hz within **8 ppm** with today's CDCE row (beat 1 per ~42 min). 720p: 750.66 (451 ppm off 751 lines → one repeat per ~44 s), 576p: 625.55 (about 0.5 line off). So 1080p is almost free; the others need a CDCE retune (B2).
- **B2, retune CDCE N/M per row** so that htot x vtot x 50.0804 Hz is the pixel clock; `hdmi_modes.py` already searches N/M and prints the error (today -3 to -65 ppm at the 50 Hz rows); the Amiga rate as target gives the beat 1/(50.08 x err): 65 ppm → 5 min, 5 ppm → 66 min. Still a beat, only a very slow one; not a lock.
- **Cost:** host table + `tools/hdmi_modes.py` (+ `selftest`), re-run of the T107 timing bench; no LUT in the cores. **It is a host change** (`host/hdmi_modes.vh` is the host's; the host is frozen by `contract/host_pin.txt`), and a rebuild of every core that links the table (T164 one-implementation rule). Monitor tolerance: pixel clock 148.74 instead of 148.87 MHz (-0.09 %), frame 50.08 Hz, vtot 1126 are all inside what the current non-CEA rows (800x600 50.000, 1280x1024 49.998 with invented blanking) already ask of the screens; S4 on Alex's screens is the proof, not a datasheet.
- **Sees/hears:** the machine is exact (no pitch change), the hitch is gone for the covered modes except the residual beat; HDMI audio unchanged (own 48 kHz). All users get it (no setting), or a second row set 'Amiga rate' as a mode choice.
- **Modes:** 50 Hz rows only; 1080p with nearly zero effort, others after the retune.

### C — copy the ZX Next as it is
- **Mechanism:** a fixed NCO step list chosen by the setting, no mode awareness. The Next can do that because its target (the C64 scaler rate) is one number. The Amiga's target moves with the chosen mode (22.6 s / 12.4 s table above), so a single fixed step fits one mode and makes the others worse (+0.088 % step on 800x600 widens the gap from -0.080 to -0.169 Hz). **It transfers only as option A with the step per mode**; that is A.
- Cost/effect: as A, minus the per-mode table; not recommended as a single step.

### D — do nothing in the gateware, do the manual note only
- Proposal 1 of the issue. Cost zero. The beat stays (12-23 s by the table, ~10 s by the tester).
- **Manual note, draft:** "Video mode and smooth scrolling. The Amiga cores draw their picture at the machine's own 50.08 Hz. The HDMI output has its own rate: 50.12 Hz in the 576p, 720p and 1080p 50 Hz modes, 50.00 Hz at 800x600. The two never match exactly, so about once every 12 to 23 seconds one picture is shown twice. In games with smooth full-screen scrolling this shows as a short hitch. 60 Hz modes show a repeated picture every sixth frame and are not recommended for PAL games. Pick the 50 Hz mode that looks best on your screen; there is no setting that removes the repeat." (to be corrected after S1 gave the real numbers).

### Comparison

| | A NCO per mode | B1/B2 table | C fixed step | D note |
|---|---|---|---|---|
| beat removed | yes (exact) | 1080p: 42 min beat; others after retune | one mode only | no |
| machine speed | +0.088 % / -0.16 % | exact | worse in 800x600 | exact |
| gateware | ~50-80 LUT per Amiga core | none in cores | like A | none |
| host change | none | yes (table, frozen host) | none | none |
| user setting | yes, default off | no (or a mode row) | yes | no |
| 60 Hz modes | no | no | no | - |

### Recommendation (analysis only)

S1 first: the derived 22.6 s does not match the tester's ~10 s, and 800x600 should be worse, not better, so the real cause may be elsewhere (stall debt, flip phase). If the beat is as derived: **D now (cheap, honest), A as the lock the issue asked for** (opt-in, per-mode step, no host change, speed change an order of magnitude below the rejected Atari lock); B1 only if Alex wants everyone to get it without a setting and accepts a host change.

2026-11-12 (Alex): S4 is tested by the reporter of #48 (Pinball Dreams at 1080p, then 576p; a hitch about every 23 s = no lock) with the next release; the retest request goes into #48 with Alex's go. S1/S2: the core's frame counter is readable (amiga_regs 0x02A, the large Amiga cores keep JTAG and Ethernet); the host has no output frame counter.
