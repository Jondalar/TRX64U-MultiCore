# Finish plan 2026-11-07: everything started gets to a testable state

Origin: board test 2026-11-07 aborted (T187 decided 11-06 but not built; new features built instead). Alex (translated): "make a plan to get
the buggy/untested but 'done' todos and the started ones FINISHED, with ALL acceptance criteria. Nothing gets sliced, nothing postponed,
agents may NEVER leave anything out without your or my OK." Then: "Start with 1. Pull it through."
Rule: CLAUDE.md "Done means every scenario proven". Inventory per scenario (2026-11-07): coordinator scratchpad `inventory/part1-4.md`.

Gate per todo: every §2 scenario has a proof (bench with a negative control that can fail, or a recorded board pass); UI changes run in ue2emu
with the `ux-review` skill (PASS, pictures opened by the coordinator); then merge. A wave's package goes out only when all its todos pass.

## Wave 1 - application only (no Vivado), UI first
1. T187 core menus (all of S1-S7, D1-D8, P4b, P5, P6), starting with one menu-building function shared by run.cc and the render bench
2. T163 S3 host Network page FTP row
3. T191 board time for the RTC (wall_time on the board), S7 area line
4. T97 app defects: RAM row in Home > Status, ':' typed as '=', Kickstart Auto picks 1.3
5. T141 C128 USB typing and extra keys (S1, S2)
6. T181 S5 C= + CTRL on Apple II, Atari, ZX, CPC (emulator)
7. T135 B1, B2 and the open scenarios
8. T152 S8 and the wrong "do not switch off" text on a locked flash
9. T137 D10 key restart
10. T139 PASS lines recorded for its benches
11. T146 S1 host menu picture under 1 s
12. T132 title centring
13. T107 host UI 720 wide (D7/D8/D11)
14. T153 S4 re-run, S11 contract lint, S12 host app on the SDK
15. T104.1, T104.2, T104.3 open scenarios (benches rebuilt)
16. T154 S7, S8
17. T142 S7 bench
18. T171 first CI publish recorded, T172 S5 bundle check automated
19. T148 defect (ue2emu 11-07): core folder, .cor open and the library stick wait run before USB is up and never retry
Then package 1 and one board session: the wave's board steps plus every todo whose only open step is the board.

## Wave 2 - gateware (Vivado), batched
1. T140 + T170 C128: loading artifacts, load stop in stock mode, fast serial / SRQ, menu on analogue
2. T182 SuperCPU step 7, S8, S12, S13 with real SuperCPU software
3. T175 diag S4-S8
4. T143, T149 S3, T108, T109, T115, T118
5. T92 CAT hang cause, T97 audio underruns
6. T110 release build without JTAG, last (rebuilds every bitstream once)
Then package 2 and board session 2.

## Wave 3 - decided, not started
1. T189 tone preset
2. T188 Amiga frame lock

## Not in this plan (not started or no spec)
T99, T125, T129, T130, T131, T134, T147, T150, T151, T155, T158, T165, T166, T173, T174, T176, T177, T178, T179, T183.

## Tester release
Alex, 2026-11-07 evening (translated): "Then I would rather let them starve than create new issues." The tester release planned for
2026-11-07 (package main-e794f15d) does not go out; the next package for testers is the one after wave 1.
- 2026-11-08: tester pre-release **main-2bd67963** on GitHub (Alex: "pre-release, announcement with known bugs, comment in the issues what to
  retest"): https://github.com/Jondalar/TRX64U-MultiCore/releases/tag/trx64mc-main-2bd67963 (bundle from tools/release_mc.sh, 27 assets);
  retest comments in #19 #21 #40 #42 #43 #45 #46 #51 #52.
