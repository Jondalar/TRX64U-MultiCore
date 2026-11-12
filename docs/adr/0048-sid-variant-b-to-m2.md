# 0048. reSID variant B and sampler collision jitter move to M2

Date: 2026-10-08

Status: Accepted

## Context

reDIP carries two sample sets for the 6581's combined waveforms (variants A and B). Our hardware build has A only; B exists as the build parameter `VARIANT_B` (T25.4 §5), about +200 LUT in the 8-SID block (1,700 against 1,501), and as a swap, not a menu setting. The difference is audible only on some 6581 tunes that use combined waveforms, compared with a real chip. The device has room (about 26,200 of 32,600 LUT), but WNS is +0.12..+0.17 ns.

## Decision

Alex (2026-10-08), asked whether to move it to M2 or spend the 200 LUT now: "Dann ohne B". M1 ships variant A only; variant B (and a per-SID A/B choice) moves to M2.

Alex, same day: "ja, Jitter auch nach M2" — the sampler collision jitter (a second voice due in the same cycle steps one cycle late; -67..-70 dB with 3..7 other voices against -94 dB for a single voice, T25.6 §limits; not audible in Knight Rider or Maniac) moves to M2. A fix needs more parallel sampler logic.

## Consequences

- T25 no longer waits for variant B or the collision jitter in M1.
- In M2, decide build swap vs. runtime choice per SID (the runtime choice needs both table sets).

## Links

TODO T25; docs/todo/T25.md; docs/specs/T25.4-redip-ultisid.md; docs/specs/T25.6-sampler.md
