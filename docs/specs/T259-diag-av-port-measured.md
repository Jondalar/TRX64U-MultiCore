# T259 — The diag core measures the A/V port: which group carries what, the bit order, RGB, SYNCn and FB; the tester's working values become presets

**Status:** BUILDING (2026-11-24) on branch `t259-diag-av`. Written PROPOSED (2026-11-23) in the TRX64-Ultimate session at
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
- **SYNCn and FB** are tied off in every core (`cores/diag/mono/top.v:1898-1899`, `cores/c64scpu/mono/top.v:1438-1439`: SYNCn = 1,
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
  Given the presets "Sony PAL" (the C128's values), "NTSC-50.rix01" and "NTSC-60.rix02" (§1)
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

### Open questions for Alex

1. ~~Does the RGB mode (P2) become a setting of the cores?~~ Answered: D2.
2. ~~Does Alex run S1 and S2 on his Sony first?~~ Answered: D3.

## §4 Out

- A scope measurement of levels and full scale at the port (no scope at hand); the TV's view is what is measured.
- The menu on the analogue picture (T140 D23).
- The encoder changes in the C128, the C64SCPU, the other cores and the host: P5 and D2 are separate changes after the results
  (as T201 P5), one per core, each with its room measured.

## §5 Build notes

(empty)
