# 0049. The app's mixer keeps rounding the pan factor

Date: 2026-10-08

Status: Accepted

## Context

`mixer_side` in `software/ultimate-app/software/u64/u64_config.cc` rounds `src·master·pan`, so "Left 3" at master -12 dB gives a mixer byte of 10. Gideon's measured sampler table (tests/sampler-ref, 2026-10-08) fits 9, i.e. truncation. With rounding, our extreme pan positions are about 8 % off his on the far side; truncating would bring them within 2 % but moves every SID pan step by one. The sampler's pan law and level already match Gideon (T25.6, heard OK by Alex the same day).

## Decision

Alex (2026-10-08): "Runden lassen". The mixer keeps rounding. This is a deliberate exception to "Gideon measured wins" for the mixer byte.

## Consequences

- The far-side output at extreme pan stays about 8 % above Gideon's; centre and the pan law are unchanged.
- SID pan steps stay where they are today.

## Links

TODO T25; docs/specs/T25.6-sampler.md; tests/sampler-ref
