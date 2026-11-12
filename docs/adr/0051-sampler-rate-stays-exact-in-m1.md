# 0051. The sampler keeps the exact rate in M1, not Gideon's fetch slop

Date: 2026-10-09

Status: Accepted

## Context

A Lemon64 thread (Bansai, 2026-10) reports that the Ultimate Audio rate counter of Gideon's cores adds a delay per sample
for each memory fetch (one for 8 bit, two for 16 bit), of a size that differs between the 1541U cart, U64 and C64U and is
no multiple of the 6.25 MHz tick. Programs tuned on a C64U carry a correction for it (Obliterator918's LAMENOTER, 16 bit
at about 31.9 kHz, lyrics timed against the song). Our `trx_sampler.v` steps exactly every n x 160 ns (bench within
0.15 %); Gideon's real sample period was never measured (only pan and volume, 2026-10-08). "Gideon measured wins" would
mean copying the slop.

## Decision

Alex (2026-10-09): "Erstmal für M1 exakt bleiben". Our sampler keeps the exact 6.25 MHz / n rate in M1. This is a
deliberate exception to "Gideon measured wins". Alex agrees with the thread that the slop is legacy from the 1541U/U2+.

## Consequences

- Programs that compensate Gideon's slop (LAMENOTER) may drift on our core; not measured yet.
- A possible fix offered to Gideon upstream is a separate question (clean room: our sampler was built without reading
  his sampler VHDL).

## Links

TODO T25; docs/specs/T25.6-sampler.md; ADR-0049
