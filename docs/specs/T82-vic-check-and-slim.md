# T82 — Our VIC-II matches Gideon's measured on the raster tests, and costs only the LUTs it needs

**Status:** PROPOSED (2026-10-12).
**Repo:** TRX64-Ultimate (kawari in `cores/icepi/vicii-kawari`, its configuration `cores/icepi/vicii_kawari.vh`).
**Number:** T82.
**Milestone:** M2.
**Origin:** Alex, 2026-10-12, after looking at another VIC-II core (vic64-t9k, derived from C64_MiSTer):
"Is the MiSTer VIC-II slimmer, maybe?", then: "a kawari optimisation? I have seen some raster
flicker anyway, which I want to follow up: is it the C64 code *or* the VIC-II in our core. Own M2
spec for the optimisation?"
**Depends on:** nothing. The results apply to the monolithic core (M1) and to the T79 core
(layer 2) alike.

---

## §1 The problem

Alex has seen raster flicker on the board and does not know whether the running C64 program
causes it or our VIC-II does. At the same time the VIC-II is the largest single block in the
C64 core whose size has never been questioned.

Measured (dfx-trial a75c356, post-route `utilization_hier.rpt`, layer 2 = the core, 14,058 LUT):

| kawari part | LUT | FF |
|---|---|---|
| `vicii_kawari` total | 2,117 | 1,553 |
| `sprites` | 715 | 739 |
| `registers` | 708 | 303 |
| `raster` | 279 | 49 |
| `cycles` | 139 | 13 |
| `addressgen` | 78 | 22 |
| `pixel_sequencer` | 75 | 219 |
| `vicii` (top), `bus_access`, `matrix`, `border` | 133 | 208 |

`cores/icepi/vicii_kawari.vh` switches on `WITH_DVI` and `HAVE_SYNC_MODULE` (kawari's own video
output), while our picture comes from the host's scaler (`vic_scaler.v`). Whether Vivado already
trims that logic is not known. Part of `registers` is ours: the state-bus and side-effect-free
read additions for freeze and snapshots (ARCH-lean-state-access, about 250 LUT).

No number exists for C64_MiSTer's VIC-II on the XC7A50T; replacing kawari is not measured either.

Parts:

- **P1 Flicker cause.** For each flicker Alex saw: is it the program, or does our VIC-II differ
  from a real VIC-II (here: Gideon's core on the C64U, measured, which wins per M1 rule)?
- **P2 VIC-II conformance baseline.** A fixed set of raster test programs with known results, run
  on our core and on Gideon's, so that every later change is checked against the same list.
- **P3 Configuration waste.** kawari logic we do not use (`WITH_DVI`, `HAVE_SYNC_MODULE`, other
  switches) and what switching it off saves.
- **P4 Size beyond configuration.** `registers` (708 LUT) and `sprites` (715 LUT): what of it is
  needed, what is our addition, what could be smaller without changing behaviour.
- **P5 Alternative core.** Whether C64_MiSTer's VIC-II would be smaller or better on this chip,
  decided only on measurement.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: a flicker has a named cause
  Given a program in which Alex saw raster flicker on our core
  When  it runs on our core and on Gideon's core on the same C64U
  Then  either both flicker the same way (the program) or only ours does (our VIC-II),
        recorded with the program, the place on screen and the frame
  And   nothing in the core is changed before the cause is named

Scenario: the baseline is the same on both cores
  Given the raster test programs of P2
  When  each runs on our core and on Gideon's
  Then  every result matches, or each difference is a listed defect with its test

Scenario: switching off unused kawari logic saves LUTs and changes nothing
  Given one kawari switch turned off in vicii_kawari.vh
  When  the bitstream is built
  Then  the core's LUT count drops (number in the CI log), timing is met
  And   the P2 baseline gives the same results as before
```

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 | flicker has a named cause | Alex's programs, our core vs Gideon's, photo or capture per frame | board, by hand (third-party programs, Gideon's core) |
| S2 | P2 | baseline equal on both cores | raster test programs (e.g. VICE testprogs VIC-II tests), results in a table in this spec | board, by hand (third-party programs not checked in) |
| S3 | P3 | unused logic off: fewer LUT, same behaviour | CI bitstream (`Slice LUTs`, `utilization_hier`), then S2 again | CI + board |
| S4 | P4 | smaller registers/sprites, same behaviour | CI bitstream per step, then S2 again | CI + board |
| S5 | P5 | alternative measured before chosen | OOC synthesis of the alternative VIC-II on the XC7A50T, LUT/FF/timing next to kawari's | CI (synthesis only) |

## §3 Decision

- **D1 Order: cause first, then size.** P1 and P2 before P3-P5: no optimisation against a VIC-II
  whose behaviour is not yet known to be right. A defect found in P1/P2 is fixed (or its own TODO
  row) before slimming.
- **D2 Gideon's core is the reference.** Measured on the C64U, as for M1.
- **D3 Slim by configuration first** (P3: switches in `vicii_kawari.vh`), one switch per build,
  then by code (P4).
- **D4 Keep kawari unless P5's measurement shows a clear gain.** Replacing the VIC-II touches the
  state bus, freeze, snapshots and the ring engine (ARCH-lean-state-access, T12, T20).

## §4 Out

- New VIC-II features (kawari extensions, hires modes, 80 columns in the VIC itself).
- Changes to the host's video chain (scaler, HDMI): T79.1 C4.
- A swap of the VIC-II without the P5 measurement.

## §5 Decisions taken while building

(filled in at the end)
