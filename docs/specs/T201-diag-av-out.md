# T201 — The diag core finds, with a tester's answers, the analog A/V settings that give a stable colour picture on his TV

**Status:** v1 and v2 (D3, D4) BUILT and merged to main (`1e899f93d`, `5cc93edeb`, 2026-11-11); S1-S6 and S9-S11 proven (§5); Vivado run 1686 green (WNS +0.185 ns), `diag.cor` (sha256 df7aa4b8...) on the board SD; S7 and S8 (board, tester) open (2026-11-11, Alex: "do that for #59 please and open an issue", translated; issue #73). 2026-11-15 sync: status line rewritten; the D3c chroma-invert is dropped by D4; the "no Vivado run yet" sentences inside §5 are the state when written, run 1686 is the later line.
**Repo:** TRX64U-MultiCore — `cores/diag/` (large core: a test picture generator and Y/C/CVBS encoder on the A/V balls, registers), the
diag core app (`coreapp/cores/diag.cc` and its test runner: a live A/V page, a TOML test with questions, a result file), `tests/`.
**Number:** T201 (registry: `TODO.md`).
**Origin:** GitHub discussion #59 (2026-11-10): a tester with an NTSC Commodore 1702 on its chroma/luma inputs gets a black-and-white
picture from the C128 and SuperCPU cores, "PAL only, 60Hz is illegible rolling display". Alex, 2026-11-11 (translated): put the diag core
to it with a sequence that collects the feedback, drive the pins and change Hz and the other values over the HDMI UI.
**Depends on:** T175 (diag core: probe engine, TOML tests, prompts, result files), T140 D24 (the C128's A/V encoder `c128_yc.v`,
`c128_avout.v`, `c128_avpins.v`).
**Milestone:** M2.

---

## §1 The problem

- **Who:** a tester with an analog set (S-Video/Y-C or composite, PAL or NTSC) on the C64U's A/V port; Alex, who cannot test NTSC.
- **What they want:** a colour picture that stands still on their set, in the cores that drive the A/V port (C128, SuperCPU).
- **What happens today (code):** only the C128 and C64SCPU large cores drive the A/V balls. Several things are not measured, only decided
  from one PAL Sony TV over SCART (2026-10-31):
  - which of the three 8-ball groups carries luma, chroma and composite (`c128_avpins.v`, `SWAP_YCV`);
  - the bit order inside each group;
  - the V sign (`VINV`);
  - the burst position and amplitude;
  - the chroma gain;
  - all NTSC modes (System Mode 1/3: 3.579545 MHz, 263 lines).

  A colour decoder that sees no valid burst switches colour off, which gives a black-and-white picture. An NTSC set that is given PAL
  shows the same.
- **Why it matters:** nobody on the team has an NTSC set; guessing costs a Vivado run and a tester round per guess.

Parts:
- **P1 Picture:** the diag core puts a known test picture (colour bars, grey ramp, a moving marker) on the A/V port, encoded as Y, C and
  CVBS, while the HDMI shows the diag menu.
- **P2 Knobs:** every value of P1's encoder can be changed live from the HDMI menu, without a reset:
  - **group assignment:** the 6 permutations of Y/C/CVBS over the 3 groups;
  - **bit order per group:** MSB first or reversed;
  - **standard:** PAL, NTSC, PAL-60, NTSC-50 (lines, frame and subcarrier as `c128_avout.v`);
  - **subcarrier fine trim:** of `inc`;
  - **V sign;**
  - **burst:** start, length and amplitude;
  - **chroma gain;**
  - **levels:** sync, blank and white.
- **P3 Sequence:** a TOML test walks a set of combinations. At each step the HDMI asks one narrow question ("Do you see colour? / Does the
  picture stand still? / Which bar is red?"), and the tester answers with keys or fire.
- **P4 Result:** the answers plus the combination of each step are written to a result file on SD, which the tester uploads. The best
  combination can be saved as a preset and reloaded.
- **P5 Hand-over:** what the results find is carried into the C128 and C64SCPU encoders (own copies each). That is a separate change
  after the results, not part of this build.

## §2 Scenarios

| # | Solves | Scenario | Test | Where |
|---|---|---|---|---|
| S1 | P1 | the generator's bars: the encoder words for one line equal the C128 encoder's for the same palette and standard (PAL and NTSC) | iverilog bench against a copy of `c128_yc` | sim |
| S2 | P2 | each knob register changes the A/V words as specified (permutation, bit reverse, standard, trim, V sign, burst, gain, levels), live, without a reset | iverilog bench | sim |
| S3 | P2 | the live A/V page: each row writes its register; the values read back | core app test with the register model | apptests |
| S4 | P3, P4 | `avout.toml` runs: each step sets its combination, asks its question, records the answer or MISSED, writes the result file | core app test (runner) | apptests |
| S5 | P2-P4 | the A/V page and a question as pictures | ue2emu + ux-review | emu |
| S6 | P4 | preset save and load survive a restart | core app test | apptests |
| S7 | All | board: the PAL test picture shows colour bars on Alex's Sony TV with the C128's current combination | by hand | board (Alex) |
| S8 | All | a tester with an NTSC set runs `avout.toml` and uploads the result | tester | board (tester) |
| S9 | D3a, b | a 384-step walk writes a complete `.toml` (every step, `[result]` at the end) and a `.txt` listing every Q1-YES step | core app test (runner) | apptests |
| S10 | D3d, D4 | each sync knob gives the specified csync (equalizing and serrated pulses, length, position, 262/263 lines); the marker holds while the freeze input is set | iverilog bench | sim |
| S11 | D3e, f, D4 | v2 `avout.toml` walks the sync knobs from the tester's preset at NTSC 60 Hz; the colour choice is recorded; the A/V page shows the sync rows | core app test + render bench | apptests |

## §3 Decision

- **D1 (Alex, 2026-11-11):** build it in the diag core with a feedback sequence and live knobs on the HDMI menu, and open a GitHub issue
  for it (the topic was a discussion so far): GitHub issue #73.
- **D2 (Alex, 2026-11-11, on the ux-review FAIL of the walk and the row names: "this is the diagnosis core - it does not matter how good the UX is ...
  as long as it fulfils its purpose", translated):** UX findings do not block T201; only what stops the measurement is fixed: the answer timeout
  is 30 s (was 120 s; a missed answer counts as NO), the walk order stays (all bit orders MSB first for the first 48 steps, stop at the first all-YES).
- **D3 v2 (Alex, 2026-11-11, "Setz das um", on the first #73 results):** the NTSC tester saw colour only with bits C reversed, mostly at
  NTSC 60 Hz (14 steps), never a still picture (Q2 always NO; the marker moves), and the bar after white blue, not yellow (chroma 180 degrees
  off). The result `.toml` ended in step 192 of 384 at 40959 bytes (the 40 KB result buffer), and the `.txt` listed only all-YES steps (none).
  v2: (a) the result files hold every step: written in pieces to the card, no fixed buffer for the walk; (b) the `.txt` lists every step whose
  first answer is YES, with its combination; (c) a new knob "Chroma" Normal / Inverted: register 053 bit 1 turns the chroma 180 degrees against
  the burst (the burst stays); (d) the marker stands still while a question is open; (e) Q2 asks "Does the picture roll or jump?" (YES = bad);
  (f) Q3 is a choice of the bar's colour right of white: yellow / blue / other; (g) the walk file `avout.toml` (v2) walks NTSC 60 Hz with bits C
  reversed only: groups x V sign x chroma x bits A x bits B (96 steps), no stop at the first good step.
- **D4 v2 rework (Alex, 2026-11-11, "Bau es um ja", after the tester's later #73 comments):** the tester found a working NTSC 50 Hz picture
  by hand (preset `NTSC-50.rix01`: Y=A C=B CVBS=C, bits C reversed, V normal, trim +60 Hz, burst 16-90 at 100 %, chroma gain 50 %, sync 8,
  blank 88, white 9/16); the same values at NTSC 60 Hz give correct colours but the 1702 does not lock vertically. So: D3c (chroma invert) is
  dropped again; new knobs for the 60 Hz vertical sync: equalizing pulses off / on (NTSC: 3 lines pre-equalizing, 3 lines serrated broad
  pulses, 3 lines post-equalizing, at twice the line rate), vsync length, vsync position, and 262 or 263 lines per 60 Hz frame; the v2 walk
  starts from the tester's preset at NTSC 60 Hz and walks these sync knobs; the questions ask for a still picture first, then the colour of
  the bar right of white (yellow / blue / other).

## §4 Out

- Changing the C128 or C64SCPU encoders before results come back (P5 is the next change).
- RGB / component outputs; audio on the A/V port.

## §5 Build notes

Built 2026-11-11 on branch `t201-diag-av` (not pushed, no Vivado run yet: S7/S8 and the timing/LUT check of the diag core wait for it).
- **Gateware:** `cores/diag/rtl/diag_avout.v` (free-running raster 504/520 dots x 312/263 lines, bars + ramp + marker, group permutation and bit order),
  `diag_yc.v` (own copy of `c128_yc.v`: VINV, burst amplitude, chroma gain, white and sync levels are inputs), knob registers `050-05C` in `diag_regs.v`
  (DCAPS bit 2), `mono/avid_pins.xdc` (same balls as the C128's), AVID ports in `mono/top.v`. No file of `cores/c128` is read; the copies are named in
  `cores/diag/mono/host/FROM` ("Edits to the copies:"). `sim/av/c128_yc_ref.v` is the unedited reference for the bench only.
- **Size (yosys `synth_xilinx` of diag_avout + diag_yc, not Vivado):** about 630 LUT, 410 FF, 10 DSP48E1, no BRAM. Vivado's number comes with the next run.
- **Application:** the A/V page is the core's Settings tab (14 live rows from the knob table `dt_av_knobs` in `diagtest.cc`; a row writes its register
  and keeps its value in the `.cfg`; the standard also sets the System Mode). `avout.toml` (`kind = "avtest"`, 384 combinations, `first_gate`,
  `stop_after_ok`) runs through `run_avtest`; results `results/avout_<stamp>.toml` and `.txt` carry the combination of each step and the answers
  (YES / NO / MISSED / SKIPPED); the page's own values come back after the walk. Presets: `presets/<name>.toml` (Save A/V preset, Load A/V preset).
- **Proof (one row per scenario):** S1 `ci/sims.txt` diag_avenc (4 standards, 759959 clocks each, 0 mismatches against the reference);
  S2 diag_avregs (PASS errors=0 checks=57: standards, trim, 6 group orders, 8 bit orders, burst, gain); S3 `tests/coreapp/test_portdiag.cc`
  (every value of every row writes its register, bit orders are bits of one register); S4 `tests/coreapp/test_diagtest.cc` `test_avout` (steps, gate,
  MISSED, stop after OK, ESC, no A/V page, read-only card); S5 render bench `diag-av.png`, `diag-avquestion.png` (+ 576p) from
  `tests/coreapp/render/render.cc`: ue2emu has no diag bitstream, so the pictures are the native render of the real UI code, as for every core's page;
  S6 `test_portdiag.cc` (preset written, application restarted over the same card, preset loaded, registers equal; test file refused; read-only card).
- Found while proving: `tb_avregs.v` measured chroma deviation with an unsigned subtraction (negative values); fixed with `cdev()`.
- 2026-11-11: Vivado run 1669 (large:diag) green, WNS +0.154 ns; diag.cor (run 1672, sha256 ec2ac57f...) as GitHub pre-release
  trx64mc-diag-e4d31abb, read back byte-equal, and a comment in #73 asking the NTSC tester to run avout.toml (Alex's go, 2026-11-11). S7, S8 open.

**v2 build notes (D3 + D4, 2026-11-11, branch `t201-av-v2`, not pushed, no Vivado run yet):**
- (a) Result files: `run_avtest` writes a header pair through `write_pair` (a card that refuses writes fails before the first question) and appends the rest through
  an open file (`AvSink`, `CoreFile::write` at the end offset, 4 KB pieces; `Out` got a flush hook). No fixed buffer limits the walk; a card that gives up mid-walk
  stops it with an error, the files keep what was written. No new fs method was needed (`CoreFs::open(path, true)` extends a file at its end).
- (b) The `.txt` lists every step whose first answer is YES (full combination of all 18 knobs, all answers, `GOOD` marked); the summary line is at the end.
- (c) The D3 chroma invert is gone again (D4): no `cinv` in `diag_yc.v` (the file equals the first version again), no register bit, no knob row. Freeze (D3d) stays: 053 is two
  bits now (b0 V sign, b1 freeze), `av_cfg[66]` -> `diag_avout` holds `mx`; the walk sets it before each step's questions (`DiagEnv::av_freeze`) and clears it after the walk.
- (d) Sync knobs (D4), registers `05D` b0 equalizing, b1 262 lines, `05E` vsync length in half lines (6 = 3 lines, 5 = 2.5, 12 = 6), `05F` position in lines (signed), `av_cfg` 82 bits
  (`[67]` eq, `[68]` l262, `[73:69]` vlen, `[81:74]` vpos, same two-flop scheme). `diag_avout.v` counts the sync in half lines: slot = 2 x line + second half. Without equalizing
  the lines [VS + pos, + vlen/2) carry one broad pulse each (low for the line less the sync width, serration at its end; an odd length ends on a half line), VS = line 244 at 60 Hz, 280
  at 50 Hz: the reset values give the first version's csync exactly. With equalizing: vlen slots of narrow pulses (half the sync width, at the half-line rate), vlen slots of serrated broad
  pulses (low for the half line less the sync width), vlen slots of narrow pulses; the block starts at VS - 3 + pos, so for 3 lines the broad block begins where it does without equalizing.
  Where a block covers a line's first half the line's own sync pulse is replaced. 262 lines: `LT` is 262 instead of 263 at the 60 Hz standards (50 Hz keeps 312); Y0, ACT, VS, the chroma
  phase per line and the frame tick are absolute line numbers and do not depend on `LT`. A vsync that would run past the last line (position +8 with 6 lines and equalizing) goes on in the next frame's first lines (review fix; `tb_avsync` measures it at 262 lines).
  Page rows: Equalizing Off / On, Vsync length 3 lines / 2.5 lines / 6 lines (2.5 and 3 differ only in the last half line without equalizing), Vsync position -8 / -4 / 0 / +4 / +8,
  Lines 60 Hz 263 / 262 (the row is named without brackets so that its test-file key `lines_60_hz` is a plain TOML key).
- (e, f) `[ask]` takes `good = [one answer per question]` (default YES) and `choices_N = [2 to 4 labels]` for question N. The choice goes `DiagUi::choice` -> `PdHost::choose` -> `AppDlg::choose_timed`;
  a timeout is MISSED, ESC stops the walk. Answers are recorded as the label. (g) `avout.toml` version 2: `[base]` is the tester's NTSC preset at 60 Hz (Y=A, C=B, CVBS=C, bits A/B MSB first,
  bits C reversed, V Normal, trim +60 Hz, burst 16 .. 90 at 100 %, chroma gain 50 %, sync 8, blank 88, white 9/16; the walk's standard defaults for burst and blank do not override them: `run_avtest`
  applies them only for the values the file names neither in `[base]` nor in `[sweep]`); `[sweep]` lines x equalizing x vsync length x vsync position = 2 x 2 x 3 x 3 = 36 steps, both questions
  every time (no gate): "Does the picture stand still (no rolling, no jumping)?" (good YES) and "Which colour is the bar right of the white one?" (yellow / blue / other, good yellow), `stop_after_ok = false`.
- LUT guess (yosys `synth_xilinx` of diag_avout + diag_yc, not Vivado): 496 LC, 448 FF, 10 DSP48E1 (the same command gave 470 LC, 410 FF for the D3 state, which had the chroma invert; the v1 line above says about 630 LUT);
  D4 adds about 26 LUT and 38 FF to the D3 state (the sync slot compare, 12 bit line latch, 14 more `av_cfg` bits in two stages).
- **Proof:**
  S9 `tests/coreapp/test_diagtest.cc` `test_avout` (384 steps: the `.toml` is longer than 40 KB, written in more than 10 pieces, has `[step.384]` and `[result]` at the end; the `.txt` has all 128
  Q1-YES steps; card gone in the middle: error, walk stops; read-only card: no question asked), unchanged and green.
  S10 `ci/sims.txt` diag_avregs (one run.sh, two benches side by side): `tb_avregs` (PASS, 63 checks) register walk incl. the three new reset values and the freeze bit (b1: the marker holds two frames with it set, moves
  again after it is cleared); `tb_avsync` (137 checks, 0 failed, 17.8 M clocks; 15 min wall for the whole diag_avregs on the builder, budget 1500 s) measures csync at the encoder input for 17 settings, one whole frame each, every pulse with line, position, width and
  start time against expectations computed in the bench: one pulse per line without equalizing (263 / 262 / 312 pulses), broad lines = vlen / 2 rounded up of width HT - W (the odd half line HTH),
  position VS + pos for -8 / 0 / +8; with equalizing 2 x vlen narrow (W / 2) and vlen broad (HTH - W) pulses a half line apart, first narrow pulse at line VS - 3 + pos, first broad slot vlen half lines
  later (line and half), the normal pulses outside the blocks, vlen 6 / 5 / 12, positions -8 / -4 / 0 / +8; frame length LT x HT x 4 clocks and active lines per frame for 263, 262 (NTSC, PAL-60) and 312
  (PAL, NTSC-50, where the 262 bit changes nothing). Printed: "sync default NTSC: 263 pulses in 263 lines, broad at line 244, 3 broad", "sync eq NTSC 3 lines: 272 pulses, 12 narrow, 6 broad,
  254 normal, first narrow line 241, first broad line 244 half 0", "sync 262 lines: 262 pulses". S1 diag_avenc unchanged (PASS, 0 mismatches in 4 standards at the default `av_cfg`).
  S11 `test_avout` (the shipped v2 file: 36 steps, 72 questions, `[knobs]` has every [base] value as the tester's preset, the first step applies them (burst 16 / 90, blank 88 not the NTSC defaults),
  the last step is 262 / On / 6 lines / +4; answers NO / other, YES / yellow, MISSED / blue, YES / blue recorded; the .txt lists steps 2 (GOOD) and 4; summary "36 combinations walked, 2 with the first
  answer YES, 1 good"), `test_portdiag.cc` (every value of every row writes its register; 053 and 05D keep the other bit; the preset covers 050-05F) and render bench pictures `diag-av.png`, `diag-avsync.png`
  (the page scrolled to the new rows), `diag-avquestion.png`, `diag-avchoice.png` from `tests/coreapp/render/render.cc` (opened: all 18 rows, the last four are Equalizing, Vsync length, Vsync position,
  Lines 60 Hz; the choice dialog shows the new question). S1-S6 stay green (`tools/dev.sh test`, rc 0; `tools/dev.sh riscv`, rc 0). Goldens `ids.txt`, `keywalk.txt`, `menu_portdiag.txt` regenerated with
  `UPDATE_GOLDEN=1 CA_GROUP=cross tests/coreapp/run.sh`; the diff touches only Port Diagnostics.
- Known: the choice dialog does not show the answer timer's seconds (the list dialog has no timer line); the timer runs and counts as MISSED. UX does not matter here (D2).
- 2026-11-11 D4: branch run 1686 green (Vivado 1688 large:diag `WNS mono diag: 0.185 ns`; cor 1691, diag.cor sha256 df7aa4b8...,
  3568185 bytes), put on the board SD as `/SD/cores/diag.cor` and read back byte-equal. Review fixes before the push: no question after ESC,
  v1 `avout.toml` in `cores/diag/superseded/`, 05D comment, the vsync block wraps past the frame end (`tb_avsync` 146 checks).
