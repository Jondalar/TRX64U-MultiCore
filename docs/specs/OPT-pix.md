# OPT-pix — make pix_layer smaller without changing what the user sees

**Status:** spec 2026-10-06, branch `opt-pix`. Decision 8 of `architecture-reset-2026-10-05.md` (Alex 2026-10-06).
Rule: scanout and the still in DDR2 stay as they are; only the implementation (and the readable register set) changes.

## 1. What costs (Vivado 757 LUT; yosys `synth_xilinx -flatten -top pix_layer` ablation, scaled, +-20 %)

still capture ~210, register readback (12 x 32-bit mux + `lat`) ~125, alpha blend ~55 (3 DSP), colour key ~12, rest ~355.
yosys on main 3562bf2: LUT 954 (+ 27 RAM32M, 76 SRL16E), FF 1271.

## 2. Changes

1. **Still capture as a shift register.** Today `acc[4*kk +: 4] <= pix_d` (31-way decoder into 124 FF), two 128-bit
   buffers `st_d[0:1]`, two 32-bit addresses `st_a[0:1]` and a 128-bit mux onto `v_wdf_data`. New: a 128-bit shift
   register (`sh <= {pix_d, sh[127:4]}` per window pixel), ONE hand-off word `st_d`/`st_a` with one toggle and one ack.
   Pixel k of the window still lands at nibble k mod 32 (first pixel in the low nibble), word index, slot, 50/60 Hz
   geometry unchanged: **the DDR2 format is byte-identical, the app's still renderers do not change** (decision 7
   allows a format change; it would save nothing more here).
   Timing budget: a word completes every 32 VIC pixels (~4.06 us at 7.88 MHz); the DDR2 write must be accepted before the
   next word completes (was: before the second next, ~8 us). A word that finds the buffer still pending is lost and sets
   `torn`: the frame is not reported, its slot is used again (unchanged semantics; the last word's report is still given at
   the moment it is handed over). Proof: the benches measure the latency from hand-off to accepted write (§4).
2. **Configuration registers write-only (read 0)**, as trx_engine's. Readable: `STILL_STATUS` (068), `PX_STATUS` (0A4).
   App reads of page 6 (grep of `software/ultimate-app` at 260aeadf, `tools/`, `tests/`): only `STILL_STATUS`
   (`trx_video.cc` 349/356, `trx_ring.cc` 1439/1462). `PX_STATUS`, `PX_FRONT` and every other register are written
   only; `tools/trxdbg.py io` can read any offset, so it now shows 0 for the write-only ones. **No app change.**
   Consequences in the module: `r_front` no longer follows `r_back` at a flip (that mirror existed for the readback);
   the read mux has two entries.
3. **Redundant copies** (only bit-identical ones, each measured, §3): `rq_addr` is `la` (the line fetch address is
   already held in `la` and only changes at a frame start or together with the request toggle); the end-of-line
   compare `pidx == f_w - 1` reuses the `pidx + 1` incrementer.
   Not done: dropping the `c_*` copy (clk) next to `r_*` (clk_sys): it is the stable-until-commit rule of the CDC
   (frame start samples at an arbitrary moment against the app's write); it costs FF, no LUT.

## 3. Numbers (yosys, before -> after per change)

yosys 0.69 `synth_xilinx -flatten -top pix_layer` (logic LUT = LUT2..6; RAM32M = distributed RAM, 4 LUT each in Vivado's
"LUT as memory"; SRL16E untouched at 76; DSP48E1 3, RAMB36 2 throughout):

| step | LUT | RAM32M | FF |
|---|---:|---:|---:|
| main 3562bf2 | 954 | 27 | 1271 |
| 1 still shift register, one hand-off word | 787 | 0 | 1404 |
| 2 write-only configuration registers (+ r_front mirror gone) | 646 | 0 | 1387 |
| 3a `rq_addr` = `la` | 625 | 0 | 1364 |
| 3b `pidx + 1 == f_w` | 616 | 0 | 1364 |

Step 1 trades the two distributed-RAM buffers (27 RAM32M) and the 31-way nibble decoder for plain FF (+133 FF). Tried and
dropped: a saturating 9-bit `age` (400 instead of 1023): +16 LUT in yosys (noise on the compare), not bit-identical in the
register, no gain.
Not touched: `lb_r` (128 FF, a pipeline stage that keeps BRAM output and lane mux on separate clocks), the lane mux (~37 LUT: a
16-bit read port on the 128-bit line buffer would remove it but needs an asymmetric BRAM, which cannot be checked without
Vivado), the 3 status counters (~37 LUT, readable bits).

## 4. Equivalence and drop-free proof

Benches `pixlayer` and `still60` (`ci/sims.txt`) with a hash of every scanout word (rgb, de, hs, vs, cx, cy per
pixel clock, per frame) and of every DDR2 write (address + data, per VIC frame), compared between the old module
(main) and the new one on the same stimuli: equal for every frame except the hog frame of `pixlayer` (cart_fill holds
MIG for 32 us: lines and still words are lost by design, in both). Register read-back checks of the benches change to
"reads 0" for the write-only registers. Latency monitor: max ui_clk from the still hand-off toggle to the accepted
write, under the benches' full load (RISC-V, REU/JTAG, drive, cart_fill), against 4.06 us.

## 5. Result (2026-10-06, iverilog 14, local)

`tb_still` (`still60`): PASS, 9 VIC frames (50, 60, 50 Hz), 0 errors, "0 of 108000 pixels differ"; with RISC-V, REU/JTAG
port, drive and cart_fill lines back to back on the arbiter: still write latency (offered -> accepted) max **23 ui_clk**
(+3 crossing), budget 304 ui_clk (4.06 us at 75 MHz). Old and new print the identical `HASH still-writes` line for all 9
frames (3375 / 3000 words, same address and data stream).
`tb_pixlayer` (`pixlayer`): PASS, 0 line errors. Max still latency **34 ui_clk** outside the hog; under the hog frame (cart_fill
32 us of 35.5 us) 2411 ui_clk in old and new alike: both lose words there and do not report the frame (the hog frame E is
the only frame whose scanout and still-write hashes differ, 338 vs 342 late lines: the lines that are late depend on how many
still words wait; every late line is whole or absent, as the bench checks). `HASH scan` equal for the other 7 frames.
Old module, new benches: only the 8 "reads 0" register checks FAIL (expected, main still reads the registers back).

Risks: (1) the hand-off budget is one word time (4 us) instead of two: the measured worst case is 34 of 304 ui_clk, but
a client that holds the video client off for longer (cart_fill hog, the engine streaming) now loses a frame after 4 us
instead of 8 us (no wrong data: the frame is simply not reported, the previous slot stays valid). (2) a tool that read
page 6 configuration registers (`trxdbg io`) sees 0. (3) yosys numbers are not Vivado's; the FF +133 land in slices that
the full chip may have to pack differently. (4) `la` is the request address: it must not change within about 3 ui_clk of
a request toggle; it changes only at a frame start (4 pixel clocks before the first request) or with a request.

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

OPT-pix: Slimmer pix_layer

Milestone M1, status ✅. Open (one line, as in the TODO table): –

Decisions: [ADR-0026 Slimmer pix_layer](../adr/0026-d08-slimmer-pix-layer.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- Task text: Slimmer pix_layer (decision 8, docs/specs/OPT-pix.md): still capture as a shift register with one buffer, configuration registers no longer readable (status only); benches pixlayer/still60 updated.
- **In main 2026-10-06 (ba060ae); board 2026-10-06: freeze picture and Timewarp scrub (still capture) pass.**
