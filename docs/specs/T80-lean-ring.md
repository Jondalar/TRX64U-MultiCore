# T80: a lean ring engine (draft)

Status: **draft 2026-10-11 night (Claude). S1 decided (Alex, 2026-10-12: build it as a switch, [ADR-0054](../adr/0054-ring-apply-behind-a-build-switch.md)); S2-S4 open.** Asked for by Alex (2026-10-11, translated): "a thought for tonight that could flow back into M1 (!): the delta ring engine. If we found an elegant way for the core to just pass data through and the app to work on the ring asynchronously in the background ... In my opinion it is also fine if Timewarp says 'Ring data prepared for scrubbing' and computes for a while."

Applies to both: the monolithic bitstream (integ-m1, main: 81.7 % LUT, 99.9 % slices in M1) and the T79 core (layer 2), where the core needs room to route.

## 1. What the engine does today

`designs/c64_basic/trx_engine.v`, 1,377 LUT (M1, measured as one module; no split per part exists). The app reaches it in exactly one file, `software/io/c64/trx_ring.cc`; trxmon, `route_timewarp.cc` and the Timewarp UI only call the `trx_ring_*` API.

| Part | Deadline | Can it leave the hardware? |
|---|---|---|
| Record former: six taps (C64 RAM, outside RAM, colour RAM, outside colour RAM, cartridge RAM, debug records), old byte from the read-first RAM | every RAM write, per C64 cycle | no |
| Frame markers F1-F3 (cycle count, VIC position, flags), ordered after every older record | per frame | no |
| Capture sequencer: chip blocks B1/B2 over the state bus while the machine runs on, tear detection | per frame | no (the state bus is the only source) |
| Capture table `CAP_TAB` (64 entries, writable by the app) | none | **its configurability**: the app never writes it, it relies on the reset table (9 entries, 58 words) |
| Common FIFO 512 x 67 bit into the DDR2 clock | not full | no |
| Drain: one masked 128-bit write per record into **four fixed areas** (streams 0-3), counters per stream | throughput | the split into four areas: see S3 |
| F4: stream 0/1 counts at each frame boundary | per frame | yes, if the streams are not split (S3) |
| Cartridge RAM old byte (read from DDR2 before the write, `cart_fill`) | at write time | no (the old content is gone afterwards) |
| Mover COPY/FILL | none | **no**: the app CPU cannot address DDR2 above 64 MB; every read of the ring is a COPY into a bounce buffer below 64 MB (`trx_ring.cc`, header; SLIM D5) |
| APPLY: undo/redo of stream 0 into C64 RAM through port B (about 265-297 LUT, SLIM D2 / T20 §17.3) | none (the machine is paused) | **yes**: the app has a CPU walk for it (`walk_range`, `collect_range`), selected when CAPS2 b5 is clear, board-tested (T20 §17.3, run 331) |
| Trace former (stream 3, one record per instruction) | per instruction | no |
| Statistics and debug counters (refactor candidate 7, docs/refactor-candidates-2026-10-10.md) | none | yes |

So the engine is already mostly "pass through": it forms records and orders them, and the app parses stream 2, builds the timeline and decides what to undo. Hardware does more than pass through in four places only: APPLY, the four-area split with F4, the writable capture table, and statistics.

## 2. Proposal, in stages

Each stage is one build switch or one commit, measured in CI (LUT of the engine, and for T79 routing), and board-tested with the Timewarp tests (`t20_apply.py`, scrub over REST).

**S1: APPLY in software.** Remove the hardware APPLY (build switch; CAPS2 b5 then reads 0 and the app takes its existing CPU walk without a code change). Cost measured in T20 §17.3: a rewind of 400 frames (1.56 M records) 156 ms with APPLY, 1,644 ms with the CPU walk; 100 frames 70 ms vs 457 ms. Gain: about 265-297 LUT and one block RAM (the 32 x 128 apply FIFO). Needs a new ADR superseding SLIM D2 ("hardware for now", Alex 2026-10-05, ADR-0024).

**S2: "Ring data prepared for scrubbing".** Make the slower walk invisible: when Timewarp opens (the machine pauses), the app walks the last N frames once in the background into its value/touch maps (the arrays `collect_range` already keeps, about 130 KB) and shows "Ring data prepared for scrubbing" with progress; a scrub step inside the prepared range then writes only the bytes that differ. Pure software, no LUT; it is what makes S1 acceptable for long rewinds.

**S3: one ordered stream instead of four areas.** The drain writes every record with its tag, in FIFO order, into one ring; the frame markers travel in the same order, so F4 and the per-stream counters go away, and the app splits the stream asynchronously into its own index (the parse task runs every 100 ms already). Gain: the four-area address logic, three of four 28-bit counters, F4 (estimate: 100-200 LUT, unmeasured). Cost: a tagged record is wider (stream 0 records are 4 bytes today; with a tag and alignment 8), so the same reach needs more DDR2 or covers fewer seconds; the app's parser, `rec_size[]`/`area_size[]` and the trace and log readers change. Largest change of the four, smallest gain: last, and only if S1 and S4 are not enough.

**S4: fixed capture table and no statistics.** The capture table becomes constants (the reset table the app relies on) and the statistics and debug counters go (refactor candidate 7). Gain: the LUTRAM table and its write path, 100-250 LUT for the counters (candidate 7's estimate).

Expected gain of S1 + S4: roughly 400-550 LUT of 1,377, with S3 perhaps 500-750. All figures are estimates until each stage is built and measured.

## 3. What stays in hardware, and why

- Record former, markers, FIFO, cartridge RAM old byte, trace former, capture sequencer: each has a deadline of a C64 cycle, an instruction or a frame against a running machine; the data cannot be reconstructed later.
- The mover: as long as the app CPU cannot address the ring, COPY is its only read path. (If the ring moved below 64 MB, the app could read it directly and COPY could go, but that area is the app's memory today.)
- A software drain is not possible: the FIFO holds 512 records, about 5 % of a busy frame, and the app cannot read the FIFO's target without COPY.

## 4. Open questions (for Alex, one at a time)

1. ~~S1: APPLY in software~~: decided, as a build switch (ADR-0054); the 1.6 s of the CPU walk to be optimised afterwards (Alex: "if need be we then optimise the 1.6 s into something faster").
2. S3: worth the app rework, or stop after S1 + S4?
3. For T79: apply S1/S4 to the core first (it needs the room), then to M1?
