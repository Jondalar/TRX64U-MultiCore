# 38. Mixer floor: a channel that is on is never rounded to gain 0

Date: 2026-10-07

Status: Accepted

## Context

Board run 481 was digitally silent: Vol Master -30 dB x Vol UltiSID -12 dB gave mixer byte 0 in the app's combine_mixer_gain, trx_audio multiplied by 0 (the old build masked it via max(UltiSID, socket byte)).

## Decision

Alex 2026-10-07 (app integ-m1-app): a channel that is on is never rounded to gain 0 (source x master x pan in one step, minimum 1; OFF and a side panned away stay 0).

## Consequences

Fixes the silent reSID8 at -30 dB master. Board retest with the merged build open.

## Links

- [T25](../../docs/todo/T25.md)
- [docs/specs/T25.3-lp-filter.md](../../docs/specs/T25.3-lp-filter.md)
