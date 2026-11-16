# T189 — every core has a tone preset (Flat / Bass boost / Treble boost) beside the master volume, applied in the host

**Status:** DECIDED (2026-11-07), D1-D8; building since 2026-11-15 (D8: go for the host change) (2026-11-15 sync: confirmed, no tone filter in host/ or coreapp/).
**Repo:** TRX64U-MultiCore — host gateware (a shelf filter before the T160 gain), the generic core application (setting, register write), `.cfg`.
**Number:** T189 (`TODO.md`).
**Origin:** GitHub issue #49 (a user of the C64U, 2026-11-05): "On small or TV speakers the Amiga cores sound thin in the low end. A simple choice of
tone, without a full equaliser ... Flat (default), Bass boost, Treble boost ... kept per core in the `.cfg`, like the volume ... HDMI and the analogue
output alike, in the same place as the T160 gain." Alex, 2026-11-06: "analysis and spec".
**Depends on:** T160 (done: `host/trx_gain.v`, register `U64_AUDIO_MIXER + 0x50`), T187 (Settings groups). Touches the frozen host: needs Alex's go and
`tools/host_pin.sh --update` (CLAUDE.md, `contract/host_pin.txt`).
**Milestone:** M3.

---

## §1 The problem

- **Who:** a user on small or TV speakers; the Amiga cores sound thin. T160 §4 put "tone controls" out; the issue asks for the smallest useful one.
- **Today (read from the code):** `host/top.v:969-975`: `audio_l/r` (24 bit, `mclk` = 12.288 MHz, one sample per 48 kHz frame) -> `trx_gain mgain` -> `snd_l/r`
  (`pause_mclk` cuts to 0) -> **both** `snd_hold` (HDMI, `:1383-1389`) and `i2s_out` (codec and the built-in speaker, `:1419`). So `snd_l/r` is the one
  place that serves HDMI and the analogue output; T140 is the analogue *picture* only and touches no audio. The register block has `0x50` = master volume,
  `0x51..0x5F` unused (`host/ultimate_sys.vhd:1308`, `case` has no `X"51"`), `0x60..` the hotkeys.
- **App side:** `coreapp/src/volume.cc` adds a generic "Volume" item (group `audio`, `WHEN_LIVE`, `apply = volume.apply`) to every core without `own_volume`;
  `op_volume` calls `CoreIo::master_volume()` = `hw.cc:149` byte write to `U64_AUDIO_MIXER + 0x50`; the `.cfg` keeps the step, `regset_apply` rewrites it at start.
- **Parts:** **P1** host: a selectable tone filter on `audio_l/r`, Flat = exactly today; **P2** headroom: a boost must not wrap or add a second clip point
  beside the gain's; **P3** area/timing in `pb_l1`; **P4** register + application setting (per core, `.cfg`, group `audio`); **P5** the sound (corner
  frequencies and gains that help small speakers).

## §2 Scenarios

```gherkin
Scenario: S1 Flat is today (P1)
  Given preset 0 (reset value), any input: 0, +-1, both extremes, 200 random samples, a full-scale sine
  Then  tone output == input bit for bit, with no added latency, and snd_l/r == today's trx_gain output
  And   control: the bypass mux replaced by the filter with unity coefficients rounded to 15 bit MUST fail (off-by-LSB), and a stuck-at-bass mutation MUST fail

Scenario: S2 shelf gains (P1, P5)
  Given preset Bass, a sine of 0.25 full scale at 50, 100, 200, 400, 1000, 4000, 16000 Hz, 0.5 s each, settle 0.1 s
  Then  the measured gain (RMS out/in) equals the ideal double-precision biquad within 0.5 dB at every point (about +6.0, +5.7, +3.0, +0.4, 0, 0, 0 dB for 200 Hz / +6 dB)
  And   preset Treble: the mirror (0 dB up to 1 kHz, +3.0 at the corner, +5.9 at 10 kHz); presets selected the other way round MUST fail the bench (control: swapped coefficient sets)

Scenario: S3 no overflow, one clip point (P2)
  Given preset Bass/Treble, a full-scale sine at the shelf's top, volume byte 255 (200 %)
  Then  the output is the ideal result clipped to +-2^23 (no sign flip, no wrap); with volume 128 the same
  And   control: the saturation removed MUST fail (wrap seen)

Scenario: S4 stable, silent when idle (P1)
  Given a full-scale burst, then zeros; and a DC step
  Then  after 0.2 s the output stays within +-2 LSB (no audible limit cycle), the DC step settles to the ideal value within 0.01 dB; switching preset while a signal plays clears the state, no sample out of range

Scenario: S5 both consumers see the same sample (P1)
  Given the filter running, frame_en and the I2S frame start
  Then  the HDMI hold (taken at frame_en) and the I2S pair (taken at the next frame start) are the same sample pair (new property, today they follow the core's own timing)

Scenario: S6 area (P3)
  Then  the CI report of the run that carries it: `=== T79 pb_l1 | Slice LUTs` <= 74.5 % (run 1122: 12,802 / 17,400 = 73.57 %), DSP +1..2 of 80, BRAM tiles unchanged, WNS >= 0

Scenario: S7 setting and register (P4)
  Given a core menu, Audio group
  Then  Tone has three values, default Flat, is kept in the `.cfg`, written at start (`regset_apply`) and live on change; register `0x51` reset 0
  And   tests/coreapp: change writes `0x51`; controls: register not written, default not Flat, not saved MUST fail; host wiring grep as `host_gain`

Scenario: S8 the sound (P5) - board, by hand
  Given A1200 core (Amiga, the issue's case), a Sony TV via HDMI and the analogue output into an amplifier, one known track/game
  When  Flat -> Bass -> Treble, each on HDMI and on analogue
  Then  Alex hears bass fuller / treble brighter, Flat identical to the current build, no crackle on switching, no distortion at Volume 100 %
```

| # | Part | Test | Where |
|---|---|---|---|
| S1-S5 | P1, P2 | new `host/sim/tone` (iverilog, python reference table generated in the bench), entry in `ci/sims.txt` tier 1 | CI sims, `tools/benches.sh` |
| S6 | P3 | Vivado report line of the host stage | CI `bitstream` |
| S7 | P4 | `tests/coreapp` (`tone` + controls), `host/sim/gain`-style wiring grep | `tests/coreapp/run.sh` |
| S8 | P5 | by hand with Alex (hearing cannot be a bench) | board session |

## §3 Decision

- **D1 (Alex, 2026-11-07: "A", translated; question 1 of §6):** headroom option A: the tone sits before the gain with a 25-bit output, the
  gain's input is widened to 25 bit and its `sat24` stays the only clip point; Flat is bit-exact. Not in the 2026-11-07 tester release (host change).
- **D2 (Alex, 2026-11-07: "A"; question 2):** bass shelf 200 Hz +6 dB, treble shelf 4 kHz +6 dB.
- **D3 (Alex, 2026-11-07: "A"; question 4):** three presets only (Flat, Bass boost, Treble boost); one biquad section per channel.
- **D4 (Alex, 2026-11-07: "B"; question 3):** one DSP per channel (2 DSP, host at 20 of 80), no time multiplexing.
- **D5 (Alex, 2026-11-07: "A, but the settings per core ... so NO host settings with inheritance, because the host has no audio!", translated;
  question 5):** the C64 line (trx64u) gets the Tone row too. The value is stored only in each core's own `.cfg`; there is no host-level
  Tone setting and no default inherited from the host. The host gateware only carries the filter the core app sets.
- **D6 (Alex, 2026-11-07: "A"; question 6):** every core gets the filter, the large mono builds (snes, zxnext, c128, neogeo) list the new file
  beside `trx_gain.v`; neogeo gets it once its flat build places again, until then neogeo shows no Tone row.
- **D7 (Alex, 2026-11-07: "A"; question 7):** the issue author gets the tester package once T189 is built and runs board step S8 on his C64U,
  reporting on #49; the gateware stays ours. All seven questions answered: T189 is DECIDED, build open (host change, `host_pin` update).
- **D8 (Alex, 2026-11-15, "T189 a"):** go for the host change: build it tonight, with the host and DFX-core rebuild it costs and the host pin moved (`tools/host_pin.sh --update`).

## §4 Out (proposed, to confirm with Alex)

- A parametric or graphic equaliser, free corner/gain values, per-channel tone, per-voice mixing (T160 §4 stays for those).
- Loudness compensation tied to the volume step; a tone for the speaker amplifier separately from the line out.
- Changes to a core's own mixer (the C64 cores' SID mixer stays as it is; see Q6).

## §5 Decisions taken while building

- **Host gateware (2026-11-15, branch `t189-tone`):** `host/trx_tone.v` (new), `trx_gain.v` input widened to 25 bit, `top.v` (`mtone` before `mgain`, `tone_sel`),
  `ultimate_sys.vhd` (register `0x51`, reset 0, port `tone_sel`), `build.tcl` / `layers.txt` list the file, `contract/host_pin.txt` updated.
- **Large cores (D6):** the 12 large mono builds except neogeo (amiga, amiga1200, c128, c264, c64scpu, cpc, diag, nes, pet, snes, zx, zxnext) carry the same change in their
  OWN copy (`mono/host/{trx_tone.v,trx_gain.v,top.v,ultimate_sys.vhd}` + their `mono/top.v` for c128, c64scpu, diag, `build_mono.tcl`, `FROM`; no shared file).
  neogeo is left alone (D6: once its flat build places again; its copy has no filter, so its app shows no Tone row).
- **App:** `coreapp/src/tone.{h,cc}` (`tone_extend` after `volume_extend` in `menu_build`, group `audio`, `tone.apply` -> `CoreIo::master_tone` -> `hw.cc` byte at
  `U64_AUDIO_MIXER + 0x51`), `CoreDesc::no_tone` (trailing field; neogeo sets it), `menutree.cc` routes the item with Volume. `tools/dev.sh riscv` passes.
  `tests/coreapp/test_tone.cc` (S7) is written but NOT run here (agents run no app tests; the pipeline does).
- **Proof (S1-S5, iverilog, `host/sim/tone/run.sh`, in `trx64-sim-env:4` on the builder, 2026-11-15): PASS.** S1 Flat bit-exact (0, +-1, extremes, 200 random, again after
  switching back); S2 measured vs ideal (bass 50/100/200/400/1k/4k/16k Hz: 5.99/5.62/2.97/0.38/0.01/0/0 dB; treble 0/0/0/0/0.02/3.00/6.00 dB, all within 0.1 dB of the double-precision
  reference); S3 no wrap at volume 255 and 128 (worst deviation from the clipped ideal 1.2 % of full scale, the coefficient quantisation, a wrap would be 200 %); S4 silent after a
  burst (0 of 4800 samples outside +-2 LSB), DC gain bass +6.02 dB (ideal 6.00), treble +0.001 dB; S5 HDMI hold == I2S take in 395 of 395 frames, both presets. Controls: filter stuck on
  and swapped coefficient sets both FAIL the bench. The saturation-removed control of S3 and the S7 controls are not built (CLAUDE.md: controls only for flash/persistence).
- **Cost (S6), no Vivado run yet:** last host build (cache `host-e9617b5de4d9f7bb`, commit de1061fe, 2026-11-15): `pb_l1` 12,862 / 17,400 LUT = 73.92 %, 16,920 / 34,800 FF, 20 / 80 DSP,
  24.5 / 35 BRAM, WNS 0.002 ns. yosys `synth_xilinx` of `trx_tone` alone: about 425 LUT, 410 FF, 2 DSP48E1, 0 BRAM (Vivado usually packs the accumulate into the DSP, so this is the
  upper end): `pb_l1` would land at about 76.4 % LUT, DSP 22 / 80. **That is above the 74.5 % bound of S6 and inside the 75-77 % that routed (skill); the pipeline run decides.**
  WNS 0.002 is the host's own; the new paths are in the 12.288 MHz domain (81 ns, multiply registered, no long combinational path).
- Bass set is Q2.16, treble set Q3.15 (18-bit coefficients; the bass poles sit so close to 1 that Q3.15 lost 0.06 dB at 100 Hz and left a +-1000 LSB dead band).
  The rounding remainder of each sample goes into the next sample's sum (first-order error feedback): no dead band, no DC offset after a burst.
- Timing in the frame: the filter captures the core's pair 16 clocks after `frame_en`, the output register changes about 30 clocks after it and holds for the rest
  of the frame, so the HDMI hold (next `frame_en`) and the I2S take (`frame_en` + 127) are the same pair (S5). A filtered sample is one frame (21 us) late; Flat is combinational.
- S4 DC criterion: 0.01 dB is not reachable with 18-bit coefficients for the bass shelf (1 + a1 + a2 is 32 counts of 65536); measured 0.0206 dB, bench limit 0.05 dB (to confirm with Alex).

## §6 Design options and questions

### 6.1 Where it sits (measured from `host/top.v`)

`audio_l/r` -> **tone** -> `trx_gain` -> pause cut -> `snd_l/r` -> {HDMI hold, I2S codec, speaker}. One place for HDMI and analogue; the speaker path
(mono mix of `snd_l/r`) follows the tone as it follows the volume.

Headroom: a +6 dB shelf lifts a full-scale low or high tone by up to 2x. Options:
- **A (proposed): tone before the gain, output 25 bit (+-2x full scale, exactly the +6 dB boost), `trx_gain` input widened to 25 bit** (a 25x9 product still
  fits one DSP48E1, A port is 25 bit). The gain's `sat24` stays the **only** clip point, as with the 200 % volume today. Flat sign-extends: bit-exact.
- **B:** tone after the gain: the filter would colour already clipped samples (volume above 100 % saturates by design) and needs its own clip: two clip points.
- **C:** tone before the gain, clipped to 24 bit: smallest change in `trx_gain`, but a second clip point and a boost on loud material distorts before the volume.
- **D:** attenuate the passband so the shelf never exceeds 0 dB (Bass = highs -6 dB): never clips, Flat is louder than the presets; compensate with Volume (up to 200 %).

### 6.2 Filter structure

Presets are exclusive (never bass and treble together), so **one** biquad section per channel with a coefficient set chosen by the preset (not two sections):
- **Direct form I**, 5 coefficients (b0 b1 b2 a1 a2) per preset, **Q3.15 in 18 bit** (a treble shelf has |coef| up to 2.8 and does not fit Q2.16; Q3.15 against the ideal
  response: within 0.1 dB at 50 Hz..1 kHz for the 200 Hz bass shelf, checked in double precision), 24-bit samples, 48-bit accumulator, round to nearest, state = the last
  2 inputs and 2 outputs per channel (8 x 24 bit).
- **Time-multiplexed, one DSP48E1 for both channels:** 10 multiply-accumulates per 48 kHz sample against 256 `mclk` cycles per frame (12.288 MHz / 48 kHz); there is room for
  4 more presets or a parametric version without more DSP. Alternative: one DSP per channel, one cycle each, a few LUT less FSM, 1 more DSP (51 of 60 were free in T107 §5.7; run 1122: 20 of 80 used).
- **Coefficient table** as `localparam`s generated by a script (`host/sim/tone/gen_coeffs.py`, RBJ cookbook shelf, S = 1, Fs = 48 kHz) and checked by the bench against
  a second implementation; Flat = bypass mux (not unity coefficients), so S1 is exact. Preset change clears the state (no thump from stale state).
- Strobe: the host's own `frame_en` (`i2s_out`, 48 kHz) delayed so the new sample is ready between the HDMI hold and the I2S frame start (S5).
- A core's `audio_l/r` must be a 48 kHz stream (all do: the host asks for a pair per frame). Nothing else in the cores changes; no contract change (the 247 ports stay).

### 6.3 Corner frequencies and gains (proposal, to listen to)

Reasoning: TV and small speakers lose below about 150-200 Hz; a shelf at the corner where they start to roll off lifts the audible low end without lifting rumble
the speaker cannot play (a larger boost mostly makes the amplifier clip). Treble: Amiga/8-bit material has most of its edge at 2-8 kHz; a corner at 4 kHz.

| Preset | Type | Corner (-3 dB of the shelf) | Gain | Ideal response (dB) at 50 / 100 / 200 / 400 / 1k / 4k / 10k Hz |
|---|---|---|---|---|
| Flat | bypass | - | 0 | 0 at all |
| Bass boost | low shelf, S = 1 | 200 Hz | +6 | +6.0 / +5.6 / +3.0 / +0.4 / 0 / 0 / 0 |
| Treble boost | high shelf, S = 1 | 4 kHz | +6 | 0 / 0 / 0 / 0 / 0 / +3.0 / +5.9 |

(computed with the RBJ shelf in double precision; coefficients: bass `b = 1.00645 -1.96861 0.96312`, `a = -1.96885 0.96933`; treble `b = 1.7611 -2.44086 0.94119`, `a = -1.15813 0.41955`).
Alternatives: +9 dB bass (`b0 = 1.0097`, Q3.15 holds), or a fourth value "Bass + treble" (the "smile"). Not decided.

### 6.4 Cost (estimate, no Vivado run; evidence for the pieces)

- `trx_gain` measured on the A1200: 37 LUT / 72 FF / 2 DSP. The tone block: coefficient mux (3 x 5 x 18 bit constants, a few LUT per bit), 8 x 24 bit state in distributed RAM or FFs
  (about 24 LUT as LUTRAM or about 190 FF), FSM and a 48-bit accumulator in the DSP: **estimate 150-300 LUT, 200-300 FF, 1 DSP (2 in the parallel variant), 0 BRAM.**
- Against run 1122 (`pb_l1` 12,802 / 17,400 LUT = 73.57 %, DSP 20 / 80, BRAM 24.5 / 35, WNS 0.138): +1-2 % LUT, within the 75-78 % that routed. The path is `mclk`
  12.288 MHz: no timing risk beyond the DSP's own pipeline. Needs one Vivado run to measure (S6). Note: the skill text still names 13,000 LUT / 50 DSP; the numbers above are the run's report.
- Mono builds that list `trx_gain.v` (snes, zxnext, neogeo, c128) must list the new file too; neogeo's flat build did not place in run 1122 (its LUT is on top of 82 %): the tone adds to it
  (T164 §5, open there).

### 6.5 Register and application

- Host: `U64_AUDIO_MIXER + 0x51`, byte, write only, reset 0 (Flat), values 0..2 (3 and up = Flat), two-flop synchroniser as the volume byte; new output `tone_sel` of `ultimate_sys.vhd`.
- App: new `coreapp/src/tone.{h,cc}` next to `volume.cc`: `tone_extend` adds "Tone" (values Flat / Bass boost / Treble boost, default Flat, `WHEN_LIVE`, `apply = tone.apply`, group `audio`)
  after Volume; `CoreIo::master_tone(uint8_t)` default ignore, `hw.cc` writes `+0x51`; the .cfg key "Tone" is stored like any setting and rewritten by `regset_apply`. A core that
  declares its own "Tone" key keeps it (as Volume).
- UX (T187 rules): one row in the Audio group under Volume, in every core's Settings area; a value list of three, no new page, no hotkey (Volume has SHIFT+/-; tone needs none);
  a status line "Tone: Bass boost" is the row itself. UX review (`ux-specialist`) before merge as for every rendered row; render bench for the new row.

### Questions for Alex (one at a time)

1. Headroom: option A (tone before the gain, 25 bit, one clip point), B, C or D?
2. Corner frequencies and gains: bass 200 Hz +6 dB, treble 4 kHz +6 dB as proposed, or other values (+9 dB, 150/250 Hz, 3 kHz)?
3. One time-multiplexed DSP (more control logic, room for more presets) or one DSP per channel (simpler)?
4. Only the three presets, or also "Bass + treble" and/or a second strength (Low / High)?
5. Does the C64 line (own SID mixer, `own_volume`) also get the generic Tone item? The host path serves it as well; the proposal says yes.
6. Which cores' gateware gets rebuilt for this: only the host changes (host_pin update, every core's package rebuilt on the host pin), or are the mono builds touched (neogeo: no room)?
7. Does the issue author get to contribute/test the build (he offered)? Gateware stays ours; he could test the board step S8 on his C64U.
