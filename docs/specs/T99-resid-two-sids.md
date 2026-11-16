# T99 — Two SIDs, each with its own of Dag's filter settings

**Status:** PROPOSED (2026-10-14).
**Status check 2026-11-13:** Unchanged: PROPOSED, nothing built (no commit besides the spec, 865f6d642); TODO row T99 planned.
**2026-11-15 sync:** still nothing built: `sid_multi.sv:82` still computes the engine as `u = i >> 2`; paths in the Repo line repointed to `cores/c64u` (cores/trx64 was deleted).
**Repo:** TRX64U-MultiCore — `vendor/icepi/c64_redip_sid/sid_multi.sv` (our wrapper around Dag Lem's
`sid_core`), `cores/c64u/trx_sid/rtl/trx_sid_c64u.v` (where it is instantiated; the old `cores/trx64` is deleted, T115 D12).
**Number:** T99 (registry: the table in `TODO.md`).
**Origin:** Alex, 2026-10-14, after seeing Gideon's UltiSID menu on our core: "I want the reSID
filters, not more than 2 SIDs" and "why are the filters tied to the number of SIDs??? we use Dag's
reSID in both 2 and 8, so his filters have to be in there too" (translated).
**Depends on:** T25.4 (the reDIP engine at UltiSID level), T25.5 (the menus and CAPS3), and T25.5 §0
(the rule that our cores show Dag's curves or none).
**Milestone:** M2.

---

## §1 The problem

Alex wants a C64 with two SIDs whose filters are Dag Lem's, each SID with its own model, cutoff
curve, resonance and digi setting. He does not want eight SID addresses he will never use.

What exists today, read from the code on 2026-10-14:

- The filters are **not** tied to the number of SIDs. Both builds run the same pipeline, Dag's
  `sid_core`. What differs is the wrapper: his two-SID top assigns the filter configuration as
  constants, while our `sid_multi` routes the same values from registers and uses the selectable
  curve tables. The runtime menus therefore exist only in the second one, and `CAPS3 b0`, the bit the
  application reads to decide "reSID or UltiSID", hangs on that build, not on the count.
- `sid_multi` takes the number of SIDs as a parameter, and the two-SID case is already measured:
  **1,325 logic LUT and 4.0 BRAM tiles**, the smallest of the three builds in T25.4's table (against
  1,501 / 4.0 for eight and 1,557 / 6.5 for the fixed two-SID top we shipped until today).
- One thing in `sid_multi` stands in the way: the engine a SID belongs to is computed as "SID number
  divided by four", hardwired. With two SIDs both land on engine 1, so the application's second
  engine — its model, its curve, its volume — would control nothing. Inside an engine the second
  chip of reDIP's pair also carries a small deliberate detuning, and with one SID per engine that
  correction would land on the wrong chip.

Why it matters: the reference core is the densest core in the partition, and this is the cheapest of
the three options while being the one Alex asked for. It also removes an oddity: a user who wants two
SIDs currently pays for eight pipelines.

**Parts (MECE):**

- **P1 Engine mapping.** A SID belongs to an engine by a parameter, not by a hardwired division, so
  two SIDs can be two engines.
- **P2 The pair correction.** The detuning of the second chip of a pair follows the index within the
  engine, not the absolute SID number.
- **P3 The build.** A `TRX_SID` value that builds the wrapper with two SIDs, with `CAPS3 b0` set and
  the filter control present.
- **P4 Sound unchanged.** Nothing about the default sound moves: with the defaults, the engine is
  still bit-exact with Dag's original, which is what T25.4 and T25.5 promise.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: two SIDs, two sets of settings (P1)
  Given the two-SID build, SID 1 set to 6581 and SID 2 to 8580 with different cutoff curves
  When  both play the same notes
  Then  each follows its own model and its own curve
  And   changing engine 2's settings changes only SID 2

Scenario: the pair correction lands where it belongs (P2)
  Given the two-SID build with both SIDs set identically
  When  both play the same register writes
  Then  their outputs are identical sample for sample
  And   neither carries the small offset meant for the second chip of a pair

Scenario: the application shows reSID (P3)
  Given a core built with the two-SID value of TRX_SID
  When  the application starts
  Then  CAPS3 b0 is set and the menu offers Dag's curves, including "off"
  And   Gideon's curve names appear nowhere

Scenario: the sound did not move (P4)
  Given the two-SID build with every setting at its default
  When  the equivalence bench plays its reference stream through both this and Dag's original
  Then  every sample is identical
```

| # | Solves | Scenario | Test (bench / board step) | Where it runs |
|---|---|---|---|---|
| S1 | P1 | two sets of settings | the SID bench, two SIDs with different models and curves | `ci/sims.txt` |
| S2 | P2 | the pair correction | the same bench, both SIDs identical | `ci/sims.txt` |
| S3 | P3 | the application shows reSID | board session: the menu, with a photo | by hand |
| S4 | P4 | the sound did not move | the existing equivalence bench against the original | `ci/sims.txt` |

The negative that has to bite: S2 compares two runs that must be identical, so it only proves
something if the bench is known to notice a difference — the same control the Freeze bench lacked for
weeks (T88 §5.10). S1 is that control: change one engine, see exactly one SID change.

## §3 Decision

- **D1 A parameter for SIDs per engine.** The engine index becomes a division by that parameter
  instead of by a constant four. Eight SIDs keep four per engine and are unchanged; two SIDs take one
  each.
- **D2 The pair correction follows the index within the engine**, which with one SID per engine means
  no correction.
- **D3 A third `TRX_SID` value** builds the wrapper with two SIDs. The existing values keep their
  meaning: the default stays the eight-SID build, and the old fixed-configuration two-SID top stays a
  comparison build for a bench and never for a release (T25.5 §0).
- **D4 Which build the reference core ships with is Alex's call**, taken after S1-S4 are green and
  the area is measured. Until then the reference core keeps the eight-SID build it has since today.

## §4 Out

- No change to Dag's `sid_core` itself. Everything here is in our wrapper around it.
- No new filter model, no new curves: the choice is the one T25.5 already defines.
- No change to the application. It reads `CAPS3 b0` and offers what the hardware has; it does not
  need to know how many pipelines are behind it.

## §5 Decisions taken while building

Empty — nothing built yet.
