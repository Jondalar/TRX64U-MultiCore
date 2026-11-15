# T227 — The c64scpu VIC picture can be smoothed (Smoothing row, as the frame-buffer cores have), at non-integer scales

**Status:** PROPOSED (2026-11-15, Alex).
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

## §4 Out

- Vertical blending (until decided).
- Other cores (they smooth through pix_layer already).

## §5 Built
