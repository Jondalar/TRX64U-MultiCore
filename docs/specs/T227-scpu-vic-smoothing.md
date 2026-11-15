# T227 — The c64scpu VIC picture can be smoothed (Smoothing row, as the frame-buffer cores have), at non-integer scales

**Status:** BUILT on branch t227-scpu-smooth, not yet through Vivado or the board (S1/S3 open) (2026-11-15).
**Repo:** TRX64U-MultiCore — gateware of `cores/c64scpu/mono/host/vic_scaler.v` (own copy, U16 lineage), the core app (Smoothing row
for the VIC path). The same scaler lives in TRX64-Ultimate u16; a working change can be offered there.
**Number:** T227.
**Origin:** T222 board test 2026-11-15: at Fit/Fill the VIC pixels come out unequal in width (nearest neighbour at a non-integer
step); the frame-buffer cores have a Smoothing row (pix_layer SMOOTH, PX_CTRL b6), the VIC path has none. Alex (translated): "it
would be cool, wouldn't it?" -> "Ok" to: measure the slice headroom first, then horizontal blending of two neighbours.
**Depends on:** T222 (merged).
**Milestone:** M2.

---

## §1 The problem

- P1: no smoothing in `vic_scaler.v` (step-based nearest neighbour), so Fit/Fill show uneven pixel columns and rows.
- P2: the c64scpu flat build is near full (T222: slices 7,881 of 8,150 = 96.7 % before T222; real numbers of run 1965 to be read).

## §2 Scenarios

- S0 measurement: LUT/FF/slice/BRAM of run 1965 (c64scpu) from its Vivado reports; the estimated cost of horizontal blending; go/no-go
  recorded here before any code.
- S1 gateware: a Smoothing bit (register next to the scale) blends two horizontal neighbours by the step's fraction; off = today's
  picture bit for bit. Proof: Vivado green, timing met.
- S2 app: a Smoothing row (Off/On) on the Video tab of c64scpu only, live. Proof: ue2emu picture.
- S3 board: Fit/Fill at 720p and 1080p with Smoothing On show even columns; Off unchanged.

## §3 Decision

Measure first (S0), then horizontal only. Vertical blending is not part of this spec unless Alex decides it after S0.

- D1 (Alex 2026-11-15, after S0: "yes, try it"): build the DSP variant of horizontal blending.
- D2 (Alex 2026-11-15, translated: "we had 2 SIDs in there, didn't we? we really need only 1 there"): the c64scpu build drops the
  second SID (build define SID2 and whatever the app offers for it) to win room; one SID stays.

## §4 Out

- Vertical blending (until decided).
- Other cores (they smooth through pix_layer already).

## §5 Built
- **S0 measurement 2026-11-15** (run 1965, builder cache `parent-1965/large-c64scpu/c64scpu_mono.utilization.rpt`, `wns.txt`):
  Slice LUTs 23083/32600 (70.81 %), FFs 22966/65200 (35.22 %), **Slices 7835/8150 (96.13 %, 315 free)**, BRAM 58.5/75, DSP 30/120;
  WNS +0.004 ns after phys_opt (router intermediate -0.298..-1.161), WHS +0.053. `vic_scaler`: 148 LUT, 152 FF, 4 RAMB36.
  Estimate for horizontal blending (read of `vic_scaler.v`): keep the previous VIC pixel in a register (upscaling: neighbours come
  in sequence, no second ring read), second palette lookup, blend per channel with 3 DSP48 by a 4-bit fraction of `hacc`:
  about +40 LUT, +90 FF, 3 DSP, no BRAM (~+10-25 slices); LUT-multiplier variant +100-150 LUT (+25-40 slices). Assessment: go for
  the DSP variant; the risk is timing closure (WNS +0.004 today), only a Vivado run decides.
- **D2 built 2026-11-15** (`cores/c64scpu/mono/core.toml`, same shape as 2d17cccb0): `SID2` out of `TRX_DEFS` and the comment. Where the second SID was wired:
  the define only picks `C64_FLAGS` bit 2 in `c64_core_c64scpu.v:24-28` (`sid_dual`, passed to `c64_redip_sid` at `c64_top_c64scpu.v:769`). The app has no SID2
  row for c64scpu (its descriptor `coreapp/cores/c64scpu.cc` has none; `u64cfg.cc`'s SID rows belong to other cores): nothing to remove there.
  **What the build loses is open:** `vendor/icepi/c64_redip_sid/sid_api.sv:61` has `NS = 2` fixed, so both SID cores stay instantiated; `sid_dual = 0` only
  makes `sid_mono` 1 (second SID's address decode = the first's, `sid_api.sv:74`). Run 1965's hierarchy (`c64_redip_sid_0` 1678 LUT, 1156 FF, 4 RAMB36, 5 RAMB18,
  2 DSP; `sid_core` 1171 LUT) therefore drops only what constant propagation of `sid_mono` / `sid_dual` removes (the pan / mono-timer logic, some of the 308 LUT
  wrapper), not a SID. The ~100 slices that 2d17cccb0 / run 852 noted are not explained by the code; the Vivado report of this branch gives the number.
  A real second-SID removal needs `NS = 1` in `vendor/icepi` (shared with c64u) or a core-owned copy: question to Alex.
