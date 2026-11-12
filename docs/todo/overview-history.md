# Overview history (moved from the TODO.md overview)

The paragraphs below stood in the TODO.md overview until 2026-10-07 and are kept unchanged. TODO.md keeps a one-line summary of each. The decisions in them are in docs/adr/.

Related decisions: [ADR-0016 Back to the drawing board: drop-in C64U, TRX64 monitor, Timewarp](../adr/0016-back-to-the-drawing-board.md); [ADR-0014 Goal: a private "Alex firmware" only (EF3 and Wasteland)](../adr/0014-goal-private-alex-firmware.md); [ADR-0015 No 100T and no public release; T53 dropped](../adr/0015-no-100t-no-public-release.md); [ADR-0004 M1 release candidate for TREX-internal testers](../adr/0004-m1-release-candidate-internal-testers.md); [ADR-0030 No .c64re / ring dump or load and no trace export in M1 (A13)](../adr/0030-timewarp-dump-and-trace-export-out-of-m1.md).

## Board test of integ-m1 2026-10-06

**Board test of integ-m1 2026-10-06 (Alex at the C64U, CI run 458 + app d5c5d30e; merged to main after it):** pass: freeze/SLIM incl. joystick in the freeze menu, Timewarp scrub/rewind/RUN-STOP ("klappt SUPER"), REU 16 MB (Xander's UltimateDemo2026 detects REU 16 MB + Turbo 64 MHz, all parts stable), REU Disabled = nothing at $DF00, a .reu in the REU survives Run PRG (8 x 256-byte compares DDR2 vs file), a 16K CRT (Canabalt) switches the REU off and lands in the block, .reu load with a CRT in = refused popup, EasyFlash (Timo's Castle), GMod2 (Sam's Journey), MegaByter (Lykia) with REU Enabled run, clock (T61) right, turbo 1-64 MHz without disk. Failed: trxmon is 40 columns with Interface Type Freeze (Alex: "das Freeze Overlay IST MEIN UI", 80 columns there too; built 2026-10-06 in integ-m1 as t21-80freeze, not board-tested yet: T21 §8.11, row T21). Missing: sampler / Ultimate Audio (G14, row T25): Xander's demo says "Audio [Fail] module not found", no MOD player. Found (row T67): replacing an app's .u2a file does not update /Flash/config/apps.json (the registry keeps the old path; delete + reinstall needed). Decided: A13 is carried out now (Save Timewarp Ring and all .c64re/.c64rering dump/load go; proper version in M2, T66). Done, row T20.

## Plan

**Plan (Alex 2026-10-07): test everything together tonight — reSID8 (T25.4/T25.5), EF3 (T51), sampler (T25.6), A08 and the mixer floor are merged into integ-m1 before the board session (one CI run per merge where time allows, to see which one costs timing). Merged so far: EF3 7abc6e3, reSID8 3f3bd22, A08 (app 8fb36f3c, app-only, no gateware change), T25.5 reSID8 menus + Filter On/Off (app aad9c57c), T25.6 sampler (gateware only, no app change), timing fix (run 499), T25.5 Dag-only curves + curve table out (app 0e6f42fb).**

## No 100T and no public release

**No 100T and no public release (Alex, 2026-10-04, late): T53 dropped; the release candidate below is cancelled.**

## Back to the drawing board

**Back to the drawing board (Alex, 2026-10-04, latest; replaces the goal line below): wanted is (1) a drop-in C64U replacement, (2) the TRX64 monitor, (3) Timewarp. Edge features may be sacrificed (Datasette, IEC, etc.). Claude delivers a realistic architecture proposal on 2026-10-05: what goes where and how.**

## Goal from now on

~~Goal from now on (Alex, 2026-10-04, late): a private "Alex firmware" only — debug the EasyFlash 3 and get Wasteland running on it. After that the project ends.** Every open row is judged by whether it serves that goal.~~

## M1 release candidate for TREX-internal testers

~~M1 release candidate for TREX-internal testers (Alex, 2026-10-03): needs T29 (turbo), T40 (NTSC), T49/T50
(update packages + T+R+X unlock), T53 (UE2 build), plus the M1 items in progress (T12, T16 with Alex's carts, T20, T21, T33).
Not required for the RC: T37 (no SID chip), T35 (test session at Till's/Mike's).~~

## TODO.md overview as it stood until 2026-10-09

- **Board test of integ-m1, 2026-10-06 (Alex, CI run 458 + app d5c5d30e):** freeze/SLIM, Timewarp, REU 16 MB, EasyFlash/GMod2/MegaByter, clock and turbo pass; trxmon 80 columns in Freeze failed (fixed, passed 2026-10-07); sampler missing (T25); registry bug found (T67); A13 carried out ([ADR-0030](../adr/0030-timewarp-dump-and-trace-export-out-of-m1.md)).
- **All board tests still open, in one list, grouped by setup (Alex 2026-10-06): docs/archive/board-tests/board-tests-pending.md.**
- **Plan (Alex 2026-10-07):** test everything together tonight: reSID8 (T25.4/T25.5), EF3 (T51), sampler (T25.6), A08 and the mixer floor are merged into integ-m1 before the board session.
- **No 100T and no public release (Alex, 2026-10-04, late):** T53 dropped, the release candidate is cancelled ([ADR-0015](../adr/0015-no-100t-no-public-release.md)).
- **Back to the drawing board (Alex, 2026-10-04, latest):** (1) a drop-in C64U replacement, (2) the TRX64 monitor, (3) Timewarp; edge features may be sacrificed ([ADR-0016](../adr/0016-back-to-the-drawing-board.md)). The goal "private Alex firmware" and the M1 release candidate before it are superseded (struck through in the history file).

## Moved from the TODO.md overview on 2026-10-23

Kept unchanged; the TODO.md head now says only how the file is kept, the milestones and where the board list is.

Order set by Alex (2026-10-02): T20 -> T21 -> T28 -> T33. **Done: all four are built and in main (2026-10-06); what is still open sits in their rows (T20, T21).**

Milestones (Alex, 2026-10-03; [ADR-0003](docs/adr/0003-milestones-m1-m2-m3.md)):
- **M1** drop-in replacement for Gideon's core plus the TRX64 functions (ring, monitor, UCI,
  turbo): everything Gideon's core does, booting from flash without JTAG.
- **M2** decoupling from Gideon: own UI (LVGL) and own API; Gideon's app only the base layer.
  Exit criterion: the e2e tests are fully migrated to Robot Framework (T71, [ADR-0052](docs/adr/0052-robot-framework-bdd-tests.md)).
- **M3** platform: boot chain with host and guests, other cores, source release.
- **–** extra, no milestone.

Where we are (2026-10-09; older entries in [docs/todo/overview-history.md](docs/todo/overview-history.md)):
- **Goal:** drop-in C64U replacement + TRX64 monitor + Timewarp ([ADR-0016](docs/adr/0016-back-to-the-drawing-board.md)); no 100T, no public release ([ADR-0015](docs/adr/0015-no-100t-no-public-release.md)).
- **M1:** all M1 items closed 2026-10-10; RC1 (tag `m1-rc1`, integ-m1 692f643d, release `trx64-integ-m1-692f643d`) installed on the board and handed to the testers. Next: M2, first T77/T78 (monitor hang, WiFi RPC timeout), then T75, T76 and the ⏸ rows.
- **Board session 2026-10-10 (Alex):** done: T62 human checks, T16 EF1/MegaByter/GMod2/EF3, T57 B1-B11, T74 (MegaByter + Gideon's core measured), T49 restore (settings NO and YES). RC1 installed. Checklist: [docs/AlexTest101026.md](../archive/board-tests/AlexTest101026.md).
- **Board:** the flash holds RC1 (integ-m1 692f643d, app 3c8e4aa); EF3 in the port. Board tests: [docs/board-tests-pending.md](../archive/board-tests/board-tests-pending.md).

**Board session:** the one list of what is still open on the board is [docs/board-tests-mc.md](../archive/board-tests/board-tests-mc.md)
(2026-10-16, main 8c160432). The older `docs/archive/board-tests/board-tests-pending.md` belongs to the drop-in line.

**Split (2026-10-13, Alex: "MultiCore inherits all the multi-core stuff under M3"):** this repository carries the
multi-core tasks, all under M3. The drop-in line stays in `TREX/TRX64-Ultimate` on `integ-m1`; its rows are no longer
listed here.

**Second split (2026-10-16, Alex, relayed by the TRX64-FW session, translated):** "Basically we are building a
monolith firmware here as a clone of Gideon, but with ring, Timewarp, monitor and now also the SID sockets. We stay
with Gideon's app. That is TRX64Ultimate. The rest, the decoupling, softboot and so on, that is MultiCore." So the
old **M2 of TRX64-Ultimate (decoupling from Gideon: own UI and own API) moves here as a whole**: T24, T32, T36, T48
and T52, IDs unchanged, their spec files staying there as history. Four of the five already have work here under
other numbers; each row says which, and whether to merge or close it is Alex's call.

