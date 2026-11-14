# T170 — The C128 core works with real drives on the board's serial (IEC) port, 1541 and 1571 with fast serial

**Status:** PROPOSED (2026-10-31, Alex). Status check 2026-11-13: PROPOSED stays; D1/D2 decided (Alex 10-31); nothing built (cores/c128/c64_top_c128.v ties CIA sp_in/cnt_in high). 2026-11-15 sync: re-checked against the code, still nothing built (c64_top_c128.v:934,936,991,993 tie sp_in/cnt_in to 1; mono/top.v:742 ties IEC_SRQ_O to 0; no IEC_SRQ_I port). Line cites in §2 are of the 2026-10-31 file.
**Repo:** TRX64U-MultiCore — `cores/c128/` (large core), `cores/common/` (IEC bus).
**Number:** T170 (TODO table).
**Origin:** Alex, 2026-10-31, at the Sony TV: "we should support the analogue ports for the floppies (serial port)" (translated); the room for it
comes from T140 D24.
**Depends on:** T140 D24 (room in the C128 large core).
**Milestone:** M3.

---

## §1 The problem

- P1 `cores/c128/mono/top.v:21-22,101-103` joins the C128, the app's drive A and the board's IEC port (`iec_bus.v`); whether a real 1541 on
  the port loads, and with which drive number next to drive A, is not tested on the board.
- P2 A real 1571 talks fast serial (burst) with the C128: SRQ as the clock and the CIA 1 serial register as data. The port has `IEC_SRQ_I`/`_O`
  (`host/c64u.xdc:460,465`); whether the core routes them to CIA 1 SP/CNT is not checked.
- P3 The core is at 85 % LUT and 75 of 75 BRAM (run 1079): anything added needs room first (T140 D24).

## §2 Scenarios

To be written with the design (real 1541: LOAD"$",8 and a program; real 1571: burst load measured faster than slow serial; drive A and a
real drive on different numbers; negative control: SRQ cut, the burst load must fall back or fail).

Checked 2026-10-31 (T140 D24 item 6): IEC_SRQ does not reach the CIAs today - both CIAs have `sp_in` and `cnt_in` tied to 1 (`c64_top_c128.v:921-923, 978-980`),
`IEC_SRQ_O` is tied to 0 and there is no `IEC_SRQ_I` port in `mono/top.v` (nor in `host/top.v`). It has to be built (T140 §5 item 6).

## §3 Decision

- **D1** (Alex, 2026-10-31): "NO 1571 on the C128 as an emulation." (translated) Only real drives on the port get fast serial; the
  emulated drive A stays a 1541.
- **D2** (Alex, 2026-10-31): "I have no drives in hardware. The testers have to test that, and we may need dumps/logs." (translated)
  Proof on the board comes from the testers (T122, T166 checklists). The core therefore records what happened on the bus: a trace of
  ATN / CLK / DATA / SRQ edges with timestamps and the FSDIR state, written by the core app into the bug report file (T162), so a
  tester's failed load can be read here without the drive.

## §4 Out

- An emulated 1571 (D1).

## §5 Built

Nothing yet.

2026-11-01 (t164-c128): re-checked against the code after the merge onto the T164 host copy - still nothing built: `c64_top_c128.v` ties both CIAs' `sp_in` / `cnt_in` to 1, `mono/top.v` ties `IEC_SRQ_O` to 0 and has no `IEC_SRQ_I` port, no bus log exists. Open: SRQ routing (just work, needs the room Vivado shows), the bus log of D2 (design needed: record format, ring size, storage), board proof by testers.
