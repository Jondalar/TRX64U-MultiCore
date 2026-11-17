# T233 — The CPC's floppy controller never stops the machine: a disk event at any moment ends in a working drive or an answered "not ready", and the CPC always reaches Ready

**Status:** PROPOSED (2026-11-17).
**Repo:** TRX64U-MultiCore — gateware of the CPC core (`cores/cpc/rtl/u765` via `tools/cpc_gen.py` -> `cores/cpc/gen/u765.sv`,
`cores/cpc/trx/cpc_fdc.v`), its benches (`cores/cpc/sim`), the CPC's app files only where §3 names them.
**Number:** T233 (TODO.md table).
**Origin:** Alex 2026-11-17, after the #17 fix build 011cdc4d (t92-cpc17b, run 2358) hung on the board after "Mount A: and reset" on the
664 and the 6128: "Why is it always works / does not work / works ... something is fundamentally wrong" and then "Write a spec. If the
MiSTer disc controller is that bad, look for another one or repair it. And then build the core again tonight and test it" (translated).
**Depends on:** T92 (#17 history, D-17a..e, docs/specs/T92-cpc-core.md §3 and §5); the board-good test core 66f89900 (board PASS
2026-11-15, run 1934).
**Milestone:** M3.

---

## §1 The problem

Alex at the board puts a DSK into the CPC's drive A: from the F1 drive list ("Mount A: and reset"). The firmware banner comes
("Amstrad 64K Microcomputer (v2)"), then nothing: no "BASIC 1.1", no Ready, no cursor, the floppy LED stays dark. Same on the 6128.
Earlier rounds of #17: CAT hung (main-736c16d9, main-13c2cde9), a fix worked (66f89900), the next build hung again. Four rounds, each
green on the benches and red on the board.

Read in the code (T92 §5, 2026-11-17): AMSDOS's init sends SPECIFY through `amsdos.rom` &C95C, which polls the FDC's main status
register with no timeout until RQM is set. u765 in COMMAND_IDLE sets RQM = `!image_scan_state[0] & !image_scan_state[1]`
(`gen/u765.sv:787`, upstream). A disk scan that never ends therefore stops the CPC before BASIC starts. In 011cdc4d the scan's start
waits on more conditions than in 66f89900 (H4, `:425`) and a disk event changes drive state outside the ce-gated machines (H1, `:394-396`).

- **P1 Boot after a disk event.** A pending scan holds RQM low, and AMSDOS waits forever: one stalled scan = a dead CPC at start.
- **P2 Unbounded waits.** Every u765 wait on the SD/DDR2 channel (scan block, trackinfo, sector) and `cpc_fdc`'s wait for the DDR2
  (`S_RWAIT`/`S_WWAIT`, a toggle handshake across sys_clk/ui_clk) has no time limit. One lost answer stops the FDC until a reset, and
  with a disk in the drive the reset starts the same scan again (P1).
- **P3 Disk events race the FDC.** The app's mount/eject/replace strobe acts on scan, seek and lock state in the clock it arrives, while
  the ce-gated scan, seek, trackinfo load, sector search and command machines are mid-step, also while the menu holds `ce` (`hold_q`).
  H1-H4 patched four of these races one by one; each patch is a new condition that can itself stay set (P1).
- **P4 The benches are not the board.** `tb_fdcscan.v` answers the SD interface itself (a byte every 64 sys clocks, no `cpc_fdc`, no
  DDR2 latency or clock crossing, no ROMs). 761 of 761 passed on the RTL that hangs on the board.
- **P5 Delivery.** Alex wants the core rebuilt and tested tonight; the CPC stays out of packages until it passes on the board
  (T92 D-17d).

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 boot after "Mount A: and reset"
  Given the CPC 664 or 6128 at Ready with A: empty, the menu open (hold_q)
  When  the app runs its "Mount A: and reset" register sequence (DDR2 copy, base/size, MOUNT strobe, HOLD, release)
  Then  BASIC prints its line and Ready within 3 s of CPC time, and a following CAT lists the disk
  And   the FDC's RQM is never low for longer than one scan (about 120 us) outside a command
```

| # | Solves | Scenario | Test (bench / board step) | Where it runs |
|---|---|---|---|---|
| S0 | P4 | The new board-path bench (S1's flow, real `cpc_fdc`, a DDR2 model with the ui_clk crossing, real ROMs) hangs on 011cdc4d's u765 exactly as the board did (banner, no Ready). If it does not, the bench is not the board: stop and report. | new mode in `cores/cpc/sim/run.sh` | manual (real ROMs, never committed) |
| S1 | P1 | Mount A: and reset on the 664 and on the 6128: Ready, then CAT lists the disk | S0's bench on the fixed RTL; board | manual bench; board |
| S2 | P1 | Power-on with A: and B: stored (T180 restore, both mounted in reset): Ready, CAT of A: and of B: | S0's bench; board | manual bench; board |
| S3 | P2 | A DDR2 answer that never comes (the bench's DDR2 model drops one): the command ends with ST0 0x40 / ST1 0x05 within the limit, the CPC keeps running, the next command works | S0's bench, one case | manual bench |
| S4 | P2 | A core reset during a DDR2 transfer of `cpc_fdc`: the next request is served (the toggle handshake realigns) | S0's bench, one case | manual bench |
| S5 | P3 | A mount/eject/replace at every phase of scan, seek, trackinfo load, sector search and a READ/WRITE data phase: every following READ DATA ends (fdcscan3's sweep on the new RTL) | `tb_fdcscan.v` fdcscan3 group | `ci/sims.txt` row `cpc_fdcscan3` |
| S6 | P3 | Replace the disk of a filled A: while a program loads (D-17b swap without reset): no hang | fdcscan3 group; the board step was dropped by Alex (T92 D-17c) | bench only |
| S7 | P1, P3 | CAT with a disk in A: lists it (#17's own step) | board | board, autonomous if §3 D6 works |
| S8 | P5 | The CPC routes: WNS >= 0 | CI `large:cpc` report line | CI |

No mutation tests, no negative controls (T174 D5.4; this is not a flash or persistence operation). S0 is not a control: it proves the
bench reproduces the board before it is trusted. The run count stays small: S0 once on the old RTL, S1-S5 once on the new one, again
only after a change.

## §3 Decision

**Repair, not replace** (Claude as advisor, 2026-11-17; Alex delegated: "look for another one or repair it"). A search for another
open HDL uPD765 found none (2026-11-17: the only other open 765 for Amstrad machines is John Elliott's C library lib765, software).
u765's command decoding and data path have worked on the board since 2026-11-02 (CAT, games); what failed every round is what
surrounds it: the scan's hold on RQM, unbounded waits, and disk events applied asynchronously. Those are repaired as a design, not
patched per race.

- **D1 Base.** Start from u765 as in 66f89900 (board-good). H1-H4 and the race skips of 011cdc4d are replaced by D3-D4, not kept.
  `tools/cpc_gen.py` stays the only way the generated file changes.
- **D2 RQM never waits for a scan.** In COMMAND_IDLE, RQM no longer depends on `image_scan_state`. A command that needs its drive's
  image (READ/WRITE DATA, READ ID, READ TRACK, FORMAT, SCAN) while that drive's scan is pending waits in its execution phase; the
  scan may start in that wait as in IDLE. Commands that do not read the image (SPECIFY, SENSE INTERRUPT STATUS, SENSE DRIVE STATUS,
  RECALIBRATE, SEEK, VERSION, INVALID) are always served. AMSDOS's init therefore never waits on a disk.
- **D3 Every wait is bounded.** A u765 request on the SD channel that is not answered (sd_ack rise and fall) within a limit (about
  20 ms; a 512-byte DDR2 read takes microseconds) is withdrawn and its busy flag cleared: a scan ends with the drive not ready, a
  trackinfo load with an empty track, a command with ST0 0x40 / ST1 0x05 (as SETUP). `cpc_fdc` gives up `S_RWAIT`/`S_WWAIT` after
  the same kind of limit and ends the transfer (ack falls), and its request/done toggles realign on a reset so a transfer dropped by a
  reset cannot steal the next one's answer.
- **D4 One owner of drive state.** A mount/eject strobe only latches the event per drive (size, write protect). The ce-gated FDC
  applies it at a safe point: no SD request of that drive in flight, its seek/scan/search idle; a command running on that drive is
  ended with not ready first. Then scan, seek and secinfo of the drive start over. Nothing outside the ce-gated machine writes their
  state. If `ce` is held (menu), the event waits with it; a reset applies it at once.
- **D5 The board-path bench.** One new bench mode: the CPC core's FDC path as on the board (`cpc_core`'s u765 + `cpc_fdc` + a DDR2
  model behind the ui_clk crossing with a realistic latency), real 664/6128/AMSDOS ROMs from a directory outside the repo
  (`CPC_ROMDIR`; the board's SD has them), the app's register sequence for S1/S2. It reports `PASS`/`HANG` with the FDC state
  (`state`, `image_scan_state`, locks, busy flags, `cpc_fdc` st) at the hang.
- **D6 Board test tonight, autonomous where the API allows.** After the CI run is green: `cpc.cor` to `/SD/cores` by FTP; the core app's
  REST (`/v1/ops`: `cpc.mount`, `machine.cold_reset`; the screen read from the CPC's frame buffer or RAM) drives S1, S2, S7. What the
  API cannot drive becomes at most three steps for Alex in the morning. Never a power cycle.
- **D7 Pipeline.** T92 D-17e takes the CPC out of the pipeline on `main` (`pipeline = false`, branch t92-off). The T233 branch sets it
  back to `true`, so only that branch builds the CPC; the merge to `main` after the board pass brings the CPC back into the packages.

## §4 Out

- Another controller (none found, §3).
- A bench at the real drive speed (6.5M sys clocks a revolution); the D3 limits and D4 do not depend on the speed.
- The 464 with a DDI-1; write-back and the dirty marks (`cpc_disk.cc`) unchanged.
- App changes beyond what D4 needs from the app (none expected: the strobes stay as they are).

## §5 Build log

(empty)
