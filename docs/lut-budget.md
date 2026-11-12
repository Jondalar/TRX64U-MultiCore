# LUT budget (C64U, XC7A50T)

Measured from CI run 275 (main 46480b4 + per-module report, post-route,
`c64_basic.utilization_hier.rpt` in the package): **25,346 LUT (77.7 %)**,
BRAM 60/75 tiles. Every CI build now publishes the per-module reports
(`utilization_hier.rpt`, `utilization_hier_synth.rpt`, depth 8).

Budget: T29 turbo up to 82 %; T25 multi-SID needs about 3k more later.

## Largest blocks

| Block | LUT | Note |
|---|---:|---|
| DDR2 controller (MIG) | 3,392 | fixed IP |
| VIC (Kawari) | 1,881 | |
| RISC-V (app CPU) | 1,867 | |
| 1541 drive | 1,820 | |
| reDIP SID | 1,454 | waveform logic 370 of it |
| `trx_dbg` (monitor hardware) | 962 | core feature, stays |
| ring engine (`eng`) | 931 | core feature |
| 6510 | 803 | |
| pixel layer | 731 | 41 SRL |
| REU | 541 | drop-in feature |
| freeze responder | 531 | |
| `jtag_dbg` (dev access) | 482 | release-switch candidate |
| UDP video + audio stream | 528 | drop-in feature (Gideon's data streams), hardware only |

## External review (2026-10-03), checked against the numbers

- P0.1 reDIP waveforms as ROM (estimated 800-1,800 LUT for 8 RAMB36): **not
  worth it**, the waveform module is 370 LUT in total.
- P0.2 video delay chains as FF: overlay 33 + pixel layer 41 SRL, about 74 LUT.
- P0.3 drive line cache to BRAM: 92 LUTRAM.
- A6 one packetizer for video and audio: about 60-140 LUT; streaming itself
  must stay in hardware (about 2.6 MB/s, 3,400 packets/s are beyond the app CPU).
- Small items (P0.4 still capture shift chain, P0.5 envelope compare, P1.1
  shared shifter): tens of LUT each; take them when a build is short.

## Release switch

A release build without `jtag_dbg` saves about 480 LUT with no user-visible
loss: guests are loaded through the FPGA's configuration JTAG (T26), not
through it. Debugging a release board then means loading a dev bitstream into
SRAM. TODO T46.
