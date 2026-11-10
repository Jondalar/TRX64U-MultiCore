# T188 — The Amiga cores can show a PAL frame every output frame at 50 Hz (options for an optional frame lock)

**Status:** PROPOSED, D1-D2 decided (2026-11-06).
**Repo:** TRX64U-MultiCore — the Amiga cores (`cores/amiga`, `cores/amiga1200`), the host mode table (`host/hdmi_modes.vh`, `tools/hdmi_modes.py`), the core app (`coreapp/cores/amiga*.cc`), the manual.
**Number:** T188 (registry: `TODO.md`).
**Origin:** GitHub issue #48 (tester, Pinball Dreams hitch at 1080p); Alex, 2026-11-06: "analyse what is possible for proposal 2" (translated). Proposal 1 of the issue is a manual note, proposal 2 an optional frame lock like the ZX Next's.
**Depends on:** T107 (A1200 output modes), T85 (Amiga core, `amiga_ce`), T88 D5 (Atari decision, 2026-10-14), T116 §5.1.3 (ZX Next lock), T164 (host table).
**Milestone:** M2.

**This spec is an analysis: §3 has no decision. §6 lists the options for Alex.**

---

## §1 The problem

- **Who:** a tester playing full-screen vertical scrollers (Pinball Dreams) on the A1200 core (Kickstart 3.1) and the A500 core (Kickstart 1.3), C64U, HDMI 1920x1080 Scale Integer, Smoothing off.
- **What they see:** a short hitch about every 10 s on the A1200 at 1080p, rarer on the A500, none noticed at 800x600. Observed by eye, not measured (issue #48).
- **Why:** the Amiga frame is written into a DDR2 buffer by the core and read by the host's pixel layer at the output rate. The output rate comes from the CDCE ratio of the mode and is free-running (T107 "Frame lock", `docs/specs/T107-application-owns-the-picture.md:286`). Two unrelated rates give a beat: once per 1/|f_out - f_core| seconds the layer shows one frame twice (or skips one). On a vertical scroller that is the hitch.

**The numbers (derived, formula `T_repeat = 1 / |f_out - f_core|`):**

- `f_core` = 28,375,160 Hz / 8 (CCK) / (227 CCK x 312 lines) = **50.0804 Hz** (T85 D3: 28.37516 MHz = `sys_clk` x 9/10, `cores/amiga/rtl/trx/amiga_ce.v:46` `+9, fire at >= 10`). The elastic stall is paid back by the debt counter (`amiga_ce.v` header), so the average is this value as long as stalls stay under 10 % of the clocks.
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

## §4 Out

Nothing decided yet. Candidates that §6 already excludes: a 60 Hz lock (a PAL machine cannot match 60 Hz), interlaced/NTSC Amiga, a core-side scaler (T107 §3 4a).

## §5 Decisions taken while building

(empty)

## §6 Options (for Alex)

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
