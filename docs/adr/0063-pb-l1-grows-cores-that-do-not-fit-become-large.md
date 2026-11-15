# 0063. The host block grows with the video path; a core that no longer fits becomes a large core

Date: 2026-10-31

Status: Accepted in principle (Alex, 2026-10-31, T107 D11d: "one core or another may become a large core through it"); the size of the move is Proposed (T164 §3a Q1)

## Context

`pb_l1` is 14,200 LUT (floorplan B, `host/build.tcl:250-252`) with 11,013 used (77.56 %) and 24.5 of 25 block RAM tiles (run 1104). 79.9 % did not
route (T133). The T164 items add about 1,076 LUT (860-1,360) and no block RAM, 85 % of the block (T164 §3c). `pb_l2` is 16,800 LUT; the densest result
that routed there is the A500 core at 85.1 % (14,298 LUT), the NES is at 78.5 % (13,181 LUT).

## Decision

The slices needed move from `pb_l2` to `pb_l1`: about 1,400 LUT, or about 430 if the release host leaves out the JTAG debug master (T164 §3a Q1, Q2).
Only slice columns move; the block RAM and DSP ranges of both blocks stay. A core that no longer routes in the smaller `pb_l2` becomes a large core
(`cores/<id>/mono/`), as Alex accepted; the CI shows which, one core at a time, not a guess.

## Consequences

- The A500 core (`amiga`) is the first to lose; the NES is at the edge of the smaller block.
- Moving the SID out of c64ref (T107 D9) lowers the reference partner to about 53 % of the block and makes the host easier to route; it frees nothing
  for the other cores, whose sizes set the block.
- A large core costs a whole-device build and its own flash file (`.cfw`, [ADR-0058](0058-full-bitstream-cores-by-iprog-from-a-flash-file.md)).

## Links

[T164](../specs/done/T164-host-package-2.md) §3a Q1-Q2, §3c, [T107](../specs/done/T107-application-owns-the-picture.md) §5.7, D11d
