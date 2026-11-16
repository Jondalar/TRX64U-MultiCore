# T79: partial reconfiguration (DFX) trial: a small static layer, the C64 side swappable

Milestone M2. Status: **trial started 2026-10-11** (branch `dfx-trial`). Not a feature yet: the trial answers whether it fits, closes timing and what it costs.

**Status check 2026-11-13:** DONE as a trial and as the architecture: cores swap through ICAP and return to the host by IPROG on the board (Atari, NES), merge 185ccc6fe (2026-10-18); TODO row T79 done.

## Why

A full bitstream load clears the whole Artix (T26.2: the data must come from outside, ESP32 JTAG or MultiBoot from flash). With partial reconfiguration (Vivado Dynamic Function eXchange) a static part keeps running and loads the rest itself through ICAP, from SD, USB or DDR2, in milliseconds; the app keeps running with its state. Alex, 2026-10-11, model: "layer 1 with USB, SD, flash, DDR2 access / layer 2 our C64 part etc. / layer 3 app".

## Decisions

- **Layer 1 (static) minimal** (Alex, 2026-10-11: "Your layer 1 definition is far too big for me"; then "Yes" to): clocks and resets, DDR2 (MIG) with the memory arbiter, RISC-V with its bus, USB, SD, flash, Ethernet (the app stays reachable over REST during a swap), ICAP (the loader for layer 2), the I/O buffers of all pins and the clocking/GT primitives (7 series: these must be static).
- **Layer 2 (reconfigurable)**: everything C64-specific: `c64_core`, the 1541 (today inside `ultimate_sys.vhd`, to be pulled out), ring engine / timewarp, the UDP streams, scaler, overlay, pixel layer and the HDMI pixel side. No picture during a swap; the menu comes back with the new layer 2.
- **Layer 3**: the app in DDR2, unchanged.
- Gideon's core is never a layer 2 (Alex, 2026-10-11: "we will not change Gideon"); guests stay full bitstreams (T26.2).
- Licence: DFX needs no licence code in any Vivado edition since 2019.1 (UG909 "Dynamic Function eXchange Licensing"); our Vivado 2024.1 qualifies.

## Trial scope

In: the split (one reconfigurable partition `layer2` with today's logic, plus a grey-box/blank variant), the DFX non-project flow in the build, place and route on the XC7A50T, timing, LUT/BRAM cost against today's full build, the size of the partial bitstream. Built in CI (builder) on the branch; no merge.

Out (next steps, each a decision with Alex): the ICAP loader in layer 1, the app side (file to ICAP), a board test (a partial bitstream over JTAG first), more than one real layer-2 variant.

## Status and history

- **2026-10-11, started (Claude):** scope as above, agreed with Alex. Measured baseline for comparison: 25,346 LUT (77.7 %), BRAM 60/75 (CI 275, `docs/lut-budget.md`); bitstream 1,845,009 B compressed (CI 675).
