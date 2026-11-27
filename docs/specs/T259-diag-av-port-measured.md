# T259 — The diag core measures the A/V port: which group carries what, the bit order, RGB, SYNCn and FB; the tester's working values become presets

**Status:** BUILT (P1-P4, P6) and on `integration`; board 2026-11-26 (RGB, Sony) and 2026-11-27 (luma, Sony); chroma and composite
not yet seen (Rick, #73). D6-D8 (2026-11-27): the output for the host, the C128 and the C64SCPU is built from the standards (P5, its own
change). Built 2026-11-24 on branch `t259-diag-av`. Written PROPOSED (2026-11-23) in the TRX64-Ultimate session at
Alex's request, for MultiCore to register and build; T259 was kept for it (Alex, 2026-11-23). Registered 2026-11-24, Alex
(translated): "ok, let an agent build this on an extra branch (T259) into the diag core please. I get the cable today/tomorrow
and can test it."
**Repo:** TRX64U-MultiCore — the diag large core (`cores/diag/`: `rtl/diag_avout.v`, `rtl/diag_yc.v`, `diag_regs.v`, `mono/top.v`), the
diag core app (`coreapp/cores/diag.cc`, the A/V page, the TOML test runner), `cores/diag/sd/tests/`, `tests/`.
**Number:** T259.
**Origin:** Alex, 2026-11-23 (translated): "It would be good to give OldMan (Rick, from the ticket) an extended diag core through
MultiCore." TRX64-Ultimate wants a working analogue output in its own `c64_basic` bitstream ("I would like us to get a working
analogue video out *here* and then give it to the MultiCore project", translated), and both need the same facts about the port first.
GitHub issue #73 (open).
**Depends on:** T201 (the diag A/V test, its knobs, walk and result files), T140 (the C128 encoder, the group map of D24 item 1).
**Milestone:** M3 (the multi-core work, ADR-0003); Alex may set another.

---

## §1 The problem

The analogue output works by observation, not by measurement. T140 S1 (which DAC group carries luma, chroma and composite, the bit
order inside each group, what `AVID_SYNCn` and `AVID_FB` do on the THS8136, the full scale) was never done. Today's map rests on a
boundary scan of Gideon's firmware and on what two TV sets showed, and the two sets disagree:

- **The three groups** (`cores/c64scpu/mono/c64scpu_avpins.v`, the same in the C128 and diag cores), MSB first:
  - A: G9 R9 G7 G6 G4 G3 B9 B6 (balls C20 B22 D21 E22 F21 G22 C19 E19)
  - B: R4 R3 B8 B7 B5 B4 B3 B2 (balls A19 B18 D20 D19 C18 F19 C17 F20)
  - C: R8 R7 R6 R5 R2 G8 G5 G2 (balls B21 A21 B20 A20 A18 D22 E21 G21)
- **Alex's Sony, 2026-10-31** (S-Video SCART cable, Y to the SCART video pin; the Sony takes no Y/C on SCART, so only the luma pin is
  seen): with Y on group A the luma pin showed the composite word, so the C128 moved Y to group C and CVBS to group A (`SWAP_YCV = 1`,
  diag `perm` 5 = CV C Y). Colour on this set is judged through composite only.
- **Rick's NTSC Commodore 1702 on its luma/chroma inputs** (#73, T201 D3/D4): colour only with the bits of group C reversed; his
  working preset `NTSC-50.rix01` is `perm` 0 (Y=A, C=B, CVBS=C), bits C reversed, V normal, trim +60 Hz, burst 16-90 at 100 %, chroma
  gain 50 %, sync 8, blank 88, white 9/16. His later preset `rix02` (#73 comment): vsync length 6 lines (2.5 and 3 roll at 60 Hz),
  263 lines at 60 Hz, trim +120, white 16/16; the burst window has little effect.
- These two cannot both be the port's map: Y on A (Rick) against Y on C (Sony). One of the readings is wrong, or the bit order hides it.
- **The Ultimate 64's A/V pinout** (nightfallcrew.com, "Ultimate 64 by Gideon", 2018-04-24, image `u64-rgb-pinout-ok.jpg`, for the U64's
  newer mainboards), 8-pin DIN (262 degrees):

  | DIN pin | Y/C and composite | RGB |
  |---|---|---|
  | 1 | luma (S-Video Y) | G |
  | 2 | ground | ground |
  | 3 | audio out | left audio |
  | 4 | composite video | R |
  | 5 | audio in (max 3 V) | right audio |
  | 6 | chroma (S-Video C) | B |
  | 7 | not connected | composite sync |
  | 8 | +5 V (not connected on the first two revisions) | fast switching |

  The SCART side (same page, `scart-pinout.png`): 15 R, 11 G, 7 B, 20 composite video / sync input, 16 fast switching (RGB on),
  8 function switching. The C64U's port is wired the same (Alex, 2026-11-23). It fits the balls `AVID_SYNCn` and `AVID_FB` the C64U
  has. Not yet measured on a board of ours.
- **DDT's mandelbrUlt runs on a C64U with the names' map** (github.com/0x444454/mandelbrUlt 93aee1c, `sources/src/top.v`; its
  `C64U.xdc` gives the 27 AVID balls the same names as `host/c64u.xdc`, compared line by line): `AVID_R` = composite, `AVID_G` = luma,
  `AVID_B` = chroma, each word on bits [9:2] MSB first; `AVID_SYNCn` = the composite sync; `AVID_FB` = high in the picture window. His
  photo (`media/mandelbrUlt-demo-small.jpg`) shows a colour picture from a C64U. That is the U64 pinout's order: luma = G, composite = R,
  chroma = B. And FB does not blank the DAC: his burst lies outside his FB window, yet the picture has colour. (His older look_ma_no_vic,
  `cores/ddt_novic/rtl/video_pattern.v:221`, still says FB is the THS8136's blanking input; his later design contradicts that.)
- **Consequence, hypothesis H1: the port's map is the names' map** - `AVID_G[9:2]` luma (DIN 1, G), `AVID_R[9:2]` composite (DIN 4,
  R), `AVID_B[9:2]` chroma (DIN 6, B), MSB first. The scan's three groups cut across these channels (group A holds balls of R, G and B),
  so they are most likely wrong, and no setting of the diag core's `perm` and `rev` can produce H1 (those knobs move whole groups and
  reverse bits inside a group). H1 fits every observation so far: the lattice on the Sony's luma pin, Rick's colour only with odd
  settings, his chroma 180 degrees off. **H2: `AVID_SYNCn` is the composite sync on DIN 7 and `AVID_FB` the fast switching on DIN 8.**
  Both are hypotheses until S1 and S3 have run.
- **SYNCn and FB** are tied off in every core (`cores/diag/mono/top.v:1898-1899` before T259, `cores/c64scpu/mono/top.v:1438-1439`: SYNCn = 1,
  FB = 0; the sync rides in the words). What they do is not measured; H2 below.
- **RGB.** Gideon's app offers "Analog Video Mode: CVBS + SVideo / RGB" (`u64_config.cc`: `CFG_ANALOG_OUT_SELECT`,
  `VIDEO_FMT_RGB_OUTPUT` in `C64_VIDEOFORMAT`). So the C64U puts out RGB on the same DAC channels in that mode; no core of ours can. C64U
  to SCART RGB cables exist; Alex gets one (2026-11-24 or later).
- T201 P5 (carry the tester's values into the C128 and C64SCPU encoders) is not done: neither core has the sync knobs
  (`git grep vlen` in `cores/c64scpu`, `cores/c128`: none).

Parts:
- **P1 Channel probe**: a new knob `map` chooses how the words reach the balls: "names" (H1: a word per `AVID_R`, `AVID_G`,
  `AVID_B`, bits [9:2], with `perm` and `rev` acting on these three channels) or "scan groups" (today's map). A test picture names the
  channel the TV is looking at and its bit order, by one photo.
- **P2 RGB mode**: R, G and B on the three channels, channel and bit order per knob, for SCART RGB cables (under H1: R on `AVID_R` =
  DIN 4, G on `AVID_G` = DIN 1, B on `AVID_B` = DIN 6).
- **P3 SYNCn and FB**: knobs for both pins, so their effect is seen and recorded (under H2: SYNCn the composite sync for SCART pin 20,
  FB high to switch a SCART TV to RGB).
- **P4 Presets**: the tester's and Alex's working values as named presets on the A/V page and in the walk files.
- **P6 Playground** (D3): every knob of the A/V page (the T201 knobs and P2/P3's) is editable live as a free value over its whole
  register range, not only from a fixed list of choices; the current set is saved to the SD card as a named preset (the P4 format) and
  loaded back.
- **P5 Hand-over**: the measured map and the working values go to the C128 and C64SCPU cores (T201 P5) and to TRX64-Ultimate
  (`c64_basic` analogue output, its own spec there); then, by D2, to every core where it fits and to the host. If H1 holds, the C128
  and C64SCPU need their ball map changed (`c128_avpins.v`, `c64scpu_avpins.v`: a word per named channel) before any other value of
  Rick's or the Sony's is carried over, because those values were found on the scan groups.

## §2 Scenarios

```gherkin
Scenario: S1 the channel probe names the pin the TV shows
  Given the A/V page set to "Channel probe", map "names" first (H1), then "scan groups"; channel N under test (R, G, B; or A, B, C)
  And   channel N carries a full luma word: sync tip, blanking, and in the picture eight bars, bar k with only bit k set, then a bar of all bits
  And   the other two channels carry the blanking level, no sync
  When  the TV's input (luma, composite, or the SCART video pin) is looked at for each N in turn
  Then  the TV locks only for the channel wired to that input (under H1: G for luma, R for composite)
  And   the bars of that group get brighter from bar 0 to bar 7 if the bit order is the listed one; any other order shows in the photo
  And   the answers ("Picture stands?", "Bars get brighter left to right?") and the step's settings go into the result file

Scenario: S2 RGB on a SCART RGB cable
  Given the A/V page set to "RGB", map "names" (H1), the C64's palette as R, G, B words on the channels chosen by the permutation knob,
        bit order per channel, SYNCn and FB as H2 says
  And   a step with only one channel lit (a ramp), the others at 0
  When  shown on a TV through a C64U to SCART RGB cable
  Then  each lit group shows as one primary colour, named by the tester, and the ramp runs dark to bright
  And   the colour bars show the C64's colours with the right map

Scenario: S3 SYNCn and FB
  Given knobs for AVID_SYNCn (1, 0, csync, inverted csync) and AVID_FB (0, 1, picture window, inverted window)
  When  each combination is shown in Y/C, composite and RGB
  Then  the effect is recorded per combination (no change / sync lost / blanked / TV switches to RGB)

Scenario: S4 presets
  Given the presets "Sony-PAL" (the C128's values), "NTSC-50.rix01" and "NTSC-60.rix02" (§1)
  When  one is chosen on the A/V page or named in a walk file's [base]
  Then  every knob takes the preset's value, and the result file names the preset

Scenario: S5 the walk
  Given avout.toml v3: the channel probe with map "names" (R, G, B), then "scan groups" (A, B, C) (S1), then RGB if the tester has
        the cable (S2), then SYNCn/FB (S3)
  When  a tester runs it
  Then  the result files hold every step and answer (T201 D3a/b), and the summary names the group of each input and its bit order

Scenario: S7 the playground
  Given the A/V page in playground mode
  When  a knob is set to any value of its register range
  Then  the A/V words change live as that value specifies
  And   "Save preset" writes every knob to a named file on the SD card, and "Load preset" sets every knob from it

Scenario: S6 fits
  Then  the diag core routes with WNS >= 0
```

| # | Solves | Test | Where |
|---|---|---|---|
| S1 | P1 | bench: the probe's words per group and bar (one-hot per bar, sync and blank levels); board: Alex's Sony through the SCART video pin, Rick's 1702 | `ci/sims.txt`, board, tester |
| S2 | P2 | bench: R, G, B words per permutation and bit order; board: Alex's Sony through the SCART RGB cable | `ci/sims.txt`, board |
| S3 | P3 | bench: the pins' levels per knob; board: Sony, tester | `ci/sims.txt`, board, tester |
| S4 | P4 | app test: each preset sets every knob (`tests/coreapp/test_diagtest.cc`) | `tests/` |
| S5 | P1-P4 | app test: the walk writes every step; board: a full walk | `tests/`, board |
| S7 | P6 | app test: a free value per knob reaches av_cfg; save and load give the same knobs; board: Alex tunes PAL RGB | `tests/`, board |
| S6 | All | Vivado report | CI |

No negative controls in the benches (T174 D5.4, T239.3 D30: negative tests only for persistence).

## §3 Decisions

- **D1 (Alex, 2026-11-23):** the extended diag core goes to Rick through MultiCore; TRX64-Ultimate takes the results for its own
  `c64_basic` analogue output and builds and proves that first, then gives it to MultiCore.
- **D2 (Alex, 2026-11-23, on open question 1):** once the port is decoded, the analogue output (Y/C, composite and RGB) goes into
  every core where it fits, the host included. Alex (translated): "Into the cores as soon as we have decoded it, all where it fits,
  the host too. Missing analogue video out is a big point of criticism from the retro nerds." Each core's room is measured, not
  assumed; a core where it does not fit is named, with its numbers, for Alex to decide. The host change is a host change (T140
  Repo line: Alex approves it, `host_pin` in the same commit).

- **D3 (Alex, 2026-11-23, on open question 2):** Alex tunes first, on his Sony through the SCART RGB cable, with a range editor and
  playground (P6): PAL RGB until it is right, NTSC too if the Sony takes it. Then Rick tunes NTSC S-Video. Alex (translated): "then
  hopefully we need nothing more for PAL S-Video." What carries PAL S-Video then: the group map and bit order from RGB (S1, S2), and
  PAL chroma (burst, V switch, levels) seen in composite on the Sony (SCART pin 20 carries Y + C). Y/C as two pins on a PAL set:
  Alex plans to bring up his Commodore 1084 (PAL, luma/chroma inputs); that needs a C64 8-pin DIN to cinch cable (luma, chroma,
  composite). Until that runs, PAL Y/C is untested; that is named here, not hidden.
- **D4 (Alex, 2026-11-24, on the D24 ledger):** the diag app's growth for P4 and P6 is an exception of the ledger
  (`tools/d24_ledger.txt`: `portdiag.cc` +59, `portdiag_regs.h` +1, `diagtest.cc` +113, `diagtest.h` +6). Alex (translated): "yes of
  course, I want T259 so that we can measure." Why no generic path fits: a contributed range setting has at most 16 values, the
  playground (D3) needs free values up to the register's range (0..1023), and a suite could not hold A/V walks.
- **D5 (Alex, 2026-11-26): DSP use of the analogue output is decided per core later; maybe two variants.** DDT wrote to Alex (forum PM,
  2026-11-26) that his Mandelbrot core's PAL encoder uses no DSP and asked that video output (the scaler too) leave the DSPs to the
  emulator cores; he also asked for a setting that switches the three A/V pins to analogue R, G, B and to component (Y/Pb/Pr). Alex
  (translated): "Leave the DSP thing, we will look at what suits the cores best ... maybe we also make 2 variants of video out, with DSP
  and without, because depending on the core it needs different approaches." Today `cores/diag/rtl/diag_yc.v:164` multiplies
  (`dsel * gain`). Component is not in D2's list (Y/C, composite, RGB).
- **D6 (Alex, 2026-11-27): the analogue output is built from the standards, not tuned.** Alex (translated): "These are all standards.
  You know the pins (xdc), you have one example for PAL from DDT, you have running RGB. So what keeps you from anticipating the rest from
  the standard definitions and building it? Where is information missing that is NOT defined in the standards?" None is missing. Of
  the board only these are not in a standard, and all are known:
  - the ball map and bit order (H1; RGB 2026-11-26, luma 2026-11-27);
  - the two pins (2026-11-26: `AVID_SYNCn` the composite sync the SCART TV takes, `AVID_FB` high switches it to RGB);
  - the DAC's code scale: white = 175 codes (the `Sony-RGB` white level 11/16) is right on the Sony, so 175 codes are 0.7 V; sync 0 and
    blanking 77, the C128's codes, are 0 V and 0.3 V on that scale.

  Timing, sync widths, burst position and amplitude, the subcarrier (4.43361875 / 3.579545 MHz from the System Mode's clock), the PAL
  phase alternation and the U/V weights come from the standards. DDT's mandelbrUlt (§1) is the working PAL encoder on a C64U. The person
  gets no encoder knobs; the diag core keeps them as the measuring tool.
- **D7 (Alex, 2026-11-27): who gets it.** Alex (translated): "The host gets it in any case AND the cores that could do it back then."
  That is the host (RGB only today, `host/av_rgb.v`) and the C128 and the C64SCPU (Y/C and composite on the scan groups today,
  `c128_avpins.v`, `c64scpu_avpins.v`; they move to the names map). trx64u is TRX64-Ultimate's (D1).
- **D8 (Alex, 2026-11-27): the analogue standard is the machine's.** Alex (translated): "PAL on the analogue output is ALWAYS 50 Hz,
  NTSC always 60. Full stop." and "PAL/NTSC are CPU settings first." The analogue output follows the System Mode (the machine's clock
  and raster) and has no standard setting of its own. HDMI keeps following the machine on the VIC path (`host/mode_req.v`: a C64-type
  core is held to the System Mode's family; Alex: "That can stay"). How the HDMI Picture item shows it: T235 D4.
  Amended the same day (Alex, translated): "interpret T259 D8 for these platforms [Amiga OCS/ECS/AGA, SNES] so that the native video
  timing of the core is what counts, not necessarily a C64-like PAL/NTSC raster." The analogue output carries the core's own raster
  (lines, line rate, interlace, hires) as the machine makes it; PAL/NTSC is the machine's setting that makes that raster. Alex's
  impulses of 2026-11-27 for the Amiga and the SNES (RGB first, native timings, a common layer, the tap before the HDMI scaler, a guard
  against 31 kHz modes, the cost per core) are open question 4, under review, not decided.
- **D9 (Alex, 2026-11-27, on open question 4, the 31 kHz guard): the common layer is two blocks, the foundation and the 31 kHz guard;
  only the Amiga takes the guard.** Alex (translated): "only the Amiga - nobody else has that, right? I think the common layer should
  consist of 2 building blocks, the foundation and then the 31 kHz guard. Rick may extend his Atari ST, it could be similar there (but I
  don't know)." The foundation (RGB pixel, sync, blank, timing in; DAC map, levels, optional PAL/NTSC encoding out) is in every core
  that has an analogue output; the guard (line rate outside the 15 kHz band: the port goes black) is a separate module that a core
  with 31 kHz modes adds.
- **Coordinator, 2026-11-24 (the diag core's UI is not judged for UX):** the Groups and Bits A/B/C labels keep their names under Map =
  Names (the walk texts say A = R, B = G, C = B); a playground value leaves the row's label as it was; the GOOD answer of the avrgb and
  avsync choice questions is form only (said in the files); `NTSC-50.rix01` is NTSC at 50 Hz, `NTSC-60.rix02` NTSC at 60 Hz, as §1 reads.

### Open questions for Alex

1. ~~Does the RGB mode (P2) become a setting of the cores?~~ Answered: D2.
2. ~~Does Alex run S1 and S2 on his Sony first?~~ Answered: D3.
3. One encoder module in `cores/common/` for the host and the cores (input: the picture at 15 kHz from the VIC raster or the pixel
   layer; output: the names map, the standard's levels), with the output kind as the only setting: RGB (with SYNCn and FB) or video
   (luma on G, chroma on B, composite on R at once)? Proposed 2026-11-27; Alex settles the pins first.
4. The Amiga (OCS/ECS/AGA) and the SNES (Alex's impulses, 2026-11-27, see D8): RGB as their first analogue mode, composite and Y/C
   optional; native timings kept (Amiga 15 kHz PAL/NTSC with interlace and hires, SNES 240p/288p and interlace); one common layer in
   `cores/common/` (in: RGB pixel, sync, blank, timing; out: the C64U DAC map, levels, optional PAL/NTSC encoding); the tap before the
   HDMI scaler; 31 kHz Amiga modes never reach a 15 kHz TV unchecked; DSP and LUT cost measured per core. Under review (2026-11-27).

## §4 Out

- A scope measurement of levels and full scale at the port (no scope at hand); the TV's view is what is measured.
- The menu on the analogue picture (T140 D23).
- The encoder changes in the C128, the C64SCPU, the other cores and the host: P5 and D2 are separate changes after the results
  (as T201 P5), one per core, each with its room measured.

## §5 Build notes

Built 2026-11-24 on `t259-diag-av` (P1-P4, P6; P5 is not part of this build, §4). Not yet pushed: the pipeline builds the diag bitstream after the coordinator's review.

**What is where**
- Gateware: `cores/diag/rtl/diag_avout.v` (the names map, the probe, RGB, the two pins), `diag_regs.v` 060-065 (one byte each, reset 0 = the T201 behaviour), `mono/layer2_diag.v` and `mono/top.v` (`AVID_SYNCn` / `AVID_FB` come from the core; `top.v.diff` regenerated). The ball wiring of the three scan groups in `top.v` is unchanged: the names map is made inside `diag_avout.v` by arranging the words of `ga`, `gb`, `gc` so that the same wiring puts bit n of a named word on `AVID_x[n + 2]` (H1: R = composite, G = luma, B = chroma, MSB first). `perm` and `rev` act on slot 0, 1, 2 = A, B, C (scan groups) or R, G, B (names); `perm` 4 ("Y=B, C=C, CVBS=A" on the page) is H1 exactly.
- Probe picture (S1): the channel under test carries sync tip, blanking (77), eight bars of 32 dots (bar k = the code 1 << k, so the first six are darker than blanking and look black; the last two are grey and white), then all bits to the window's end; the other two channels sit at blanking. Bars are raw DAC codes, as §2 says.
- RGB (S2): the palette as R, G, B words (full scale = the white knob / 16, at most 1), black outside the picture window, no sync in the words; the RGB picture knob lights one word as a ramp across the window, the others 0. `perm` picks the channel of each word (word 0 = R, 1 = G, 2 = B; the page's labels still say Y, C, CVBS).
- Pins (S3): SYNCn: High, Low, Csync (low in the sync pulses), Inverted csync; FB: Low, High, Window (high in the picture window), Inverted window. They are aligned with the words of the active mode (9 clocks behind the sync of the encoder modes, 4 behind it in probe and RGB).
- Application: six rows on the A/V page (Map, Mode, Probe channel, RGB picture, SYNCn, FB; keys `map`, `mode`, `probe_channel`, `rgb_picture`, `syncn`, `fb` in a walk file). Presets are files `<core folder>/presets/<name>.toml` in the T201 format; a value `"#n"` is a free register value. Shipped and installed with the core: `Sony-PAL` (the C128's values on the scan groups), `NTSC-50.rix01` (NTSC 50 Hz), `NTSC-60.rix02` (NTSC 60 Hz), both of Rick's on the scan groups. Tasks: "Load A/V preset by name", "Save A/V preset", "Set an A/V knob (playground)" (`knob=value`: any number of the knob's register range, or one of its labels). A walk names a preset with `[base] preset = "name"` (the preset's knobs, then the file's own [base] knobs win); the result files carry `preset = ` / `Preset <name>`. A preset with a free value cannot be a walk's base (it refuses, naming the knob).
- Walks (S5): `avout.toml` v3 is a suite of `avprobe.toml` (6 steps: map Names / Scan groups x channel A or R, B or G, C or B; the .txt ends with "The TV locks on" naming map, channel and whether the bars rose), `avrgb.toml` (24 steps: 6 permutations x R, G, B ramp and the palette picture) and `avsync.toml` (32 steps: Test picture / RGB x SYNCn x FB). The T201 vsync walk is `avvsync.toml` (id avvsync; the old avout.toml v2 is in `superseded/`, so an unedited copy is replaced).

**Proofs** (iverilog and the application tests run once on the Mac, `nice -n 15`)

| # | Result |
|---|---|
| S1 | `ci/sims.txt` `diag_avmap` (tb_avmap, 40 units): `PASS tb_avmap errors=0 checks=4259387 units=40`. Per map (names / scan groups) and channel: the run list sync tip (148 clocks), blanking, the eight bars (128 clocks each, values 1, 2, 4 ... 128), all bits (576), blanking; the other two channels are 77 at every clock. Board: Alex's Sony, Rick's 1702 (open) |
| S2 | the same bench: the palette bars (white, yellow, cyan, green, magenta, red, blue, 200 clocks each) in R, G, B words for all six permutations, three bit orders and the scan groups; the one-word ramp for R, G, B, never falling, 255 at the end, the other words 0. Board: Sony through the SCART RGB cable (open) |
| S3 | the same bench: SYNCn and FB follow each knob value (a constant, or the encoder's csync / window delayed to the words: 9 clocks in test picture mode, 4 in probe and RGB), in the three modes; in the probe the sync pin is low exactly where the word is the sync code. Board: Sony, tester (open) |
| S4 | `tests/coreapp/test_diagtest.cc` `test_avpresets`: each preset sets all 24 knobs to the values of section 1 through the walk's hooks; the result files name the preset; a [base] knob wins; a missing preset and a free value refuse. `test_diagtest: 617 checks, 0 failed` |
| S5 | `test_avwalk`: the suite runs avprobe (6 steps), avrgb (24), avsync (32) in this order, each with its result pair, and the probe's .txt names the channel and bit order. Same run. Board: a full walk (open) |
| S7 | `tests/coreapp/test_portdiag.cc` (where the register model lives): every knob takes lo, hi, mid and their neighbours through `pd.av_free` and the register holds exactly it, out of range and unknown refuse; every knob set free, saved, loaded by name into a fresh gateware gives the same registers. `test_portdiag: 711 checks, 0 failed`. Board: Alex tunes PAL RGB (open) |
| S6 | Vivado, the builder's build of db29691a (pipeline run 45408377169, `build_mono_diag`; the later commits change no gateware input, so the key and the bitstream in the board's `diag.cor` of 0612dccf1 are the same): `All user specified timing constraints are met.`, WNS 0.017 ns, TNS 0.000, WHS 0.053 ns; Slice LUTs 13112 of 32600 (40.22 %), Slice Registers 16345 (25.07 %), Block RAM Tile 22.5 of 75 (30.00 %), DSPs 36 of 120 (30.00 %); `diag_avout` 781 LUT (778 logic, 3 SRL), 449 FF, 12 DSP. Before the build, the Yosys estimate of `diag_avout` against the T201 module: LUT 725 -> 869 (+144), FF 451 -> 524 (+73), DSP48 10 -> 14 (+4: the three RGB scalers and the ramp), SRL16 2 -> 7; `diag_regs` +6 byte registers (about 11 FF, 20 LUT); `top.v` nothing |

The T201 benches still pass with the wider `av_cfg` (zero = the old behaviour): `diag_avenc` (`PASS tb_avenc errors=0`), `diag_avregs` (`PASS avregs`, with `PASS tb_avsync errors=0 checks=146`).

**Board, 2026-11-26 (Alex's Sony, a C64U to SCART RGB cable; `diag_new.cor` of run 45805580029 on t239.3, sha256 304b5890..., driven over the
core app's REST by the coordinator, Alex at the TV; photos in the session)**
- Gideon's firmware 3.15a ("Analog Video Mode RGB") shows a colour picture through the same cable: cable and TV work.
- `avprobe` (6 steps, SYNCn High, FB Low): no picture in any step. `avrgb` and `avsync` started from the same pins: no picture either.
- Playground, mode RGB: SYNCn Csync with FB Low gives a white screen; SYNCn Csync with FB High gives the palette picture. So on this cable
  `AVID_SYNCn` is the sync the TV takes (SCART pin 20: with FB Low the TV shows the csync itself as video, white between the pulses) and
  `AVID_FB` switches the TV to its RGB input (SCART pin 16, fast blanking). With SYNCn High the TV has no sync at all, whatever the words hold.
- Map Names with Groups "Y=C, C=B, CVBS=A" (perm 5) showed the bars white, cyan, yellow, green, magenta, blue, red: R and B swapped by the
  permutation, which acts on RGB too (`diag_avout.v:211` on t239.3, `who_f(perm)`). With Groups "Y=A, C=B, CVBS=C" (perm 0) the bars are white, yellow, cyan, green,
  purple, red, blue, black as `diag_avout.v:123-124` (t239.3) draws them, and the grey steps run black to white: H1 holds for RGB (A = R, B = G, C = B).
- R, G and B ramps (RGB picture R / G / B ramp): each a pure primary, dark to bright across the picture, no steps: MSB first on all three.
- Saved as the preset `Sony-RGB` (PAL 50 Hz, Map Names, Groups "Y=A, C=B, CVBS=C", MSB first, mode RGB, SYNCn Csync, FB High; the other
  knobs as `Sony-PAL`) through Save preset; the file read back over FTP holds these values.

**Board, 2026-11-27 (Alex's Sony, a C64U to SCART cable for S-Video: luma to SCART pin 20, chroma to pin 15; the Sony takes only
composite or RGB on SCART, so it shows pin 20 as composite. `diag.cor` 0.1.0-fixdlg of `fix-dlg-edit` a49cdc75, bitstream
mono-diag-5d605995445e3585; driven over REST by the coordinator, Alex at the TV, videos in the session)**
- `avprobe` (SYNCn High, FB Low; `timeout_s` 600 on the card): map Names channel B (G) locks with the bars rising (GOOD); Scan groups
  A and C lock with the bars not rising (both groups hold G balls in another order); Names A, Names C and Scan groups B show nothing.
  Result `results/avprobe_20261127-110124.txt`. S1 for luma: `AVID_G`, MSB first, as H1.
- Test picture, Map Names, Groups "Y=B, C=C, CVBS=A" (perm 4 = H1), SYNCn Csync, FB Low, the other knobs as `Sony-PAL`: a clean black and
  white test picture. After knob changes the Sony stayed black until it locked again (Alex: "it probably has to re-sync"), which made
  several settings look dead that were not. FB Low does not blank the DAC (the probe's step above has FB Low too).
- `Sony-RGB` on this cable: the G word as a grey picture (white, yellow, cyan, green light; magenta, red, blue dark), locked. The G word
  carries no sync in mode RGB, so the sync came over the SYNCn line; which pin of this cable carries it is not known.
- Not measurable on this set: chroma on `AVID_B` and composite on `AVID_R`.
- S2 is proven on the board. S7 (knobs live, Save preset) is proven on the board. S3 is proven for SYNCn Csync with FB Low / High only.
- Open from this session: the walks' [base] for the SCART cable (avrgb, avsync start with SYNCn High / FB Low and show nothing on it);
  avprobe cannot find a channel through SCART, because pin 20 carries SYNCn, not a DAC channel; S1 on the luma / composite inputs needs an
  S-Video or composite cable; NTSC is Rick's (D3).
- S1 through Alex's SCART cable with S-Video (luma on pin 20; the Sony takes no S-Video, so only the luma channel would show): not run.
  Alex (2026-11-26, translated): "No, let Rick do that first, his part."
- A side effect while driving the panel over REST: a script stepped the Standard row through to NTSC 50 Hz by mistake; the HDMI picture
  flickered until it was set back to PAL 50 Hz. The A/V Standard row also changes the raster the HDMI picture comes from.

**Notes and open points**
- The page's rows keep the label they had when a free value is set (the playground writes the register, not the row); the end of a walk puts the rows' values back, which also ends a playground value.
- "Groups" and "Bits A, B, C" keep their names; with the Names map A, B, C read R, G, B.
- D24: the application part grew four per-core files (`diagtest.cc` +113 code lines, `diagtest.h` +6, `portdiag.cc` +59, `portdiag_regs.h` +1); the ledger exceptions are D4 (2026-11-24).
- Board steps (Alex and the tester): flash the diag core; run avprobe on the Sony's luma pin and Rick's 1702 inputs; avrgb with the SCART cable; avsync; tune PAL RGB in the playground and save the preset.
- First pipeline run of the branch (2026-11-24, run 45408377169) was red twice: `ci/layers_check.sh` splits its table at
  spaces, so `Sony PAL.toml` was "in no section" (the only tracked file under host/, cores/, vendor/ with a space); the preset
  is `Sony-PAL` now, like the two NTSC ones. `tests/coreapp/golden/menu_portdiag.txt` lacked the six new rows and the two
  tasks; it is the dump that run printed (`test_desc.cc:47`), nothing else in it changed.
- Second run (2026-11-24, run 45412495356): `tests/coreapp/test_bundle.cc:421` still counted the diag core's install as before T259 (8 writes of
  10 files); the record holds 17 files now (14 tests, 3 presets), so the case writes 15. The count is corrected.
- The coordinator's test run (`tools/dev.sh test`, 2026-11-24) found the next one behind it: `test_keywalk` (golden `keywalk.txt`, `ids.txt`) lacked
  the eight new rows. Both are rewritten (only the Port Diagnostics lines change; the hashes of F1/home match the builder's run), and `test_cut`,
  the last unit that compiles `portdiag.cc` and `diagtest.cc`, passes (638 checks).
