# T141 — A USB keyboard types into every core, not only into the menus

**Status:** BUILDING (2026-10-31, A1200 first, D4), PROPOSED 2026-10-28, asked by Alex. 2026-11-13 status check: merged in parts (w1-a500, w1-t141); A1200 board ok; A500 and C128 built, Vivado + board open (as row). 2026-11-15 sync: C128 P2 is in main (1d85da7fa, 2026-11-07): `coreapp/cores/c128.cc:1108-1111` hands the held usages to `c128_xkeys_apply` (c128.cc:1240), tests in `tests/coreapp/test_c128.cc:651-672`; the commit says 'not yet run', no pass is recorded here; S2 board open. The row text 'C128 later' is outdated.
**Repo:** TRX64U-MultiCore — the core application (`coreapp/`), the cores' keyboard inputs (`cores/c128`, `cores/amiga`,
`cores/amiga1200`), possibly the host's keyboard block (a host change needs Alex's approval).
**Number:** T141.
**Origin:** Alex, 2026-10-28 (translated), at the board with a USB keyboard plugged in: "I wanted to know whether the USB
keyboard is supported in the C128? In the TRX64 it runs and everything is fine." After the answer (menus only): "Then
build the USB path along with it. Is it the same in the Amiga? There too it would make sense to support USB keyboards...
the mouse is already in." This revises T85 Q6 ("C64U matrix only for the first build ... no USB keyboard yet",
Alex, 2026-10-12).
**Depends on:** T104 (core application), T85/T97 (Amiga input), T113 (C128 extra keys).
**Milestone:** M3.

---

## §1 The problem

Read from the code (branch `t136-a1200-colour`):
- The core application reads the USB keyboard as characters for its own menus only (`coreapp/src/run.cc:64`,
  `system_usb_keyboard.getch()`). No key reaches a machine.
- The TRX64 large core types from USB because it runs Gideon's application, whose `Keyboard_USB::usb2matrix`
  (`host-app/software/io/usb/keyboard_usb.h`) turns HID reports into C64 matrix presses.
- C128: the extra keys of the C128 have a table and a register (`c128_xkey_for`, `C128_XKEYS` 0x028-0x02A), "which
  nothing in the application calls yet" (`coreapp/cores/c128.cc:5`).
- Amiga (A500 and A1200): `cores/amiga/rtl/trx/amiga_input.v` builds Amiga raw key events from the C64 matrix only
  (T85 Q6); keys the C64 lacks (ESC is LEFT ARROW, TAB, F6-F10, keypad, DEL, HELP) are missing. The mouse already comes
  through the application (`mousesink`).

Parts:
- **P1** the core application passes USB key state to the machine (generic, for every core that reads the C64 matrix).
- **P2** C128: the extra keys (HELP, LINE FEED, NO SCROLL, keypad, ESC, TAB, ALT, 40/80) from USB.
- **P3** Amiga: the Amiga keys the C64 matrix lacks, as raw codes, from USB.
- **P4** No double keys: the C64 keyboard and a USB keyboard at the same time; the core menu's hot key still works.

## §2 Scenarios

```gherkin
Scenario: S1 typing on a C64-matrix core
  Given the C128 (C128 mode) with a USB keyboard
  When  "PRINT 1+1" RETURN is typed on USB
  Then  the screen shows 2; the same on the VC-20 and the C264

Scenario: S2 C128 extra keys
  Given the C128 in C128 mode
  When  HELP (USB Page Up), ESC, TAB and keypad 7 are pressed on USB
  Then  the C128 sees them (bench: the register C128_XKEYS / matrix gets the cells of c128_keys.h)

Scenario: S3 Amiga keys
  Given the A1200 at the Workbench shell
  When  ESC, TAB, F10, DEL, HELP and keypad 5 are pressed on USB
  Then  each produces its Amiga raw code (bench: amiga_input emits the raw code; board: the key shows in a key tester)
  And   control: a key released on USB produces the release code (bit 7) once

Scenario: S4 both keyboards
  Given a core running with the C64 keyboard and a USB keyboard
  When  keys are pressed on both
  Then  each key arrives once; the core menu key (as today) opens the menu from either keyboard
```

| # | Solves | Test | Where |
|---|---|---|---|
| S1 | P1 | board (Alex types) | board session |
| S2 | P2 | `tests/coreapp` key mapping test + board | CI, board |
| S3 | P3 | `cores/amiga/sim` input bench + board | CI, board |
| S4 | P4 | board | board session |

## §3 Decision

- **D1** USB HID reports go into the matrix the cores already read (as Gideon's `usb2matrix` does for the TRX64 core),
  so every core that reads the C64 matrix types from USB without its own code. Where that overlay lives (an existing
  register of the host's keyboard block, or a register per core) is read from the code before building (§5).
- **D2** Keys outside the C64 matrix go to the core's own register: C128 `C128_XKEYS` (table `c128_xkey_for`), Amiga a
  new raw-key register into `amiga_input` (same event spacing as the matrix keys).
- **D3** First C128 (with T140), then Amiga (after T138 is merged; both touch the A1200's files).
- **D4 (Alex, 2026-10-31, translated: "Does the Amiga 1200 core take the USB keyboard? That should go in too. So a USB one beside the
  C64 keyboard."):** the A1200 goes first, now (T138 is built); C128 follows. Both keyboards at once (P4).
- **D5 (Alex, 2026-10-31: "B"):** on a core that takes the USB keyboard as raw keys (the A1200) F10 is the machine's F10; only SCROLL LOCK
  (and the menu button) open the core menu. Built: `rawkeys_is_menu_key` = SCROLL LOCK, `rawkeys_active()` keeps `run.cc` from opening the menu on F10;
  F10 -> raw 0x59 in the table test (88 rows); control `rk_f10menu` (F10 back as a menu key) fails as it must; `rk_menukey` dropped (SCROLL LOCK has no
  Amiga code, dropping it sends nothing either). Inside the open menu F10 still closes it, as in every core.

## §4 Out

- USB keyboards on the host menu: work already.
- Key remapping by the user.

## §5 Build notes

### A1200 (D4, Alex 2026-10-31: "Does the Amiga 1200 core take the USB keyboard? That should go in too. So a USB one beside the C64 keyboard."; branch `t141-a1200-usb-keys`)

Built: P1 for the A1200, P3, P4; S3 and S4. C128 (P2) and the A500 are not in this branch.

**Path.** `usb_hid.cc` calls a keyboard hook (`usb_hid_set_keyboard_sink`, new, noted in host-app/PROVENANCE) with the combined boot report on the USB
task, before the driver's own handling (which goes on unchanged: the menus, `getch`). `coreapp/src/rawkeys.cc` (a ring of 64 reports) is the library;
the core's poll (`engines_poll`) calls `rawkeys_pace`: reports -> held Amiga codes -> press / release events by difference -> the new register
`RAWKEY` (0x1C4, A1200 build, FEATURES b4). Descriptor record `CoreDesc::rawkeys` (`RawKeySpec`: register, HID table, modifier table, lock key); the A500
took none until 2026-11-08 (now it has the same record). The table is `coreapp/cores/amiga_keys.h`.

**Gateware.** (As built 2026-10-31; the fork is gone, see "A500 shares the queue".) `cores/amiga1200/rtl/amiga_input.v` was an A1200 fork of the shared file (+ `amiga_input.diff`, `check_wrapper_fork.sh`,
`core_sources.tcl`, `run_l2.sh`); the `amiga_regs.v` / `amiga_core.v` forks got the register and the wires (diffs regenerated). A queue of 16 raw events
({release, code}) in `amiga_input`: written from `cpu_clk` by data + toggle (as the mouse), 3-flop sync, popped when nothing else is due with the matrix
keys' own spacing (gap 32768 ce28, about 1.2 ms), in front of the matrix scan and behind mouse pairs. `RAWKEY` read b0 = `rk_room` = the queue holds 8 or
fewer (one bit crosses the clock safely); the application reads it and writes at most 8 events per read, so the queue (16) never overflows. The hold /
reset empties it. The shared `amiga_input.v` (A500) is not touched: it keeps its sources and its cache key. *Superseded 2026-11-08, see "A500 shares the queue" below.*

**Key table (USB usage -> Amiga raw code; bit 7 = release).**
A-Z (04-1D) -> 20 35 33 22 12 23 24 25 17 26 27 28 37 36 18 19 10 13 21 14 16 34 11 32 15 31; 1-9,0 (1E-27) -> 01-0A;
RETURN 28 -> 44; ESC 29 -> 45; BACKSPACE 2A -> 41; TAB 2B -> 42; SPACE 2C -> 40; - 2D -> 0B; = 2E -> 0C; [ 2F -> 1A; ] 30 -> 1B; \ 31 -> 0D;
non-US # 32 -> 2B; ; 33 -> 29; ' 34 -> 2A; ` 35 -> 00; , 36 -> 38; . 37 -> 39; / 38 -> 3A; non-US \ 64 -> 30 (the key next to left SHIFT);
F1-F10 (3A-43) -> 50-59 (F10: see below); **HELP = Page Up (4B) -> 5F** (Insert is left alone; the C128's HELP is Page Up too, S2); DELETE 4C -> 46 (DEL);
cursors UP 52 -> 4C, DOWN 51 -> 4D, RIGHT 4F -> 4E, LEFT 50 -> 4F; keypad 0 (62) -> 0F, 1-3 (59-5B) -> 1D 1E 1F, 4-6 (5C-5E) -> 2D 2E 2F,
7-9 (5F-61) -> 3D 3E 3F, . 63 -> 3C, ENTER 58 -> 43, - 56 -> 4A, + 57 -> 5E, / 54 -> 5C, * 55 -> 5D, NUM LOCK 53 -> 5A "(", keypad = 67 -> 5B ")".
Modifier byte: L CTRL and R CTRL -> 63 (one key); L SHIFT -> 60; R SHIFT -> 61; L ALT -> 64; R ALT -> 65; L GUI -> 66 (left Amiga); R GUI -> 67 (right Amiga).
CAPS LOCK (39) is the Amiga's lock key: a USB press flips the state, on sends 62 (press), off sends E2 (release); the USB release sends nothing.
No Amiga key (nothing sent): F11, F12, PrtSc, Pause, Insert, Home, End, Page Down, Application.

**F10 and Scroll Lock are the core menu's keys (run.cc) and are never sent to the machine.** S3 names F10; F10 opening the menu is today's behaviour of
the application for every core and stays; the Amiga's F10 (code 59) is in the table and unreachable until the menu key is another one (question for Alex).

**P4.** The C64 matrix keys and the USB keys meet in the same event queue behind one spacing: each key once (the bench presses a matrix key and a queue
key together). The menu hot key (F10 / Scroll Lock / the hardware button) is read by the unchanged `getch` path from either keyboard. With the menu open
the poll does not run: `rawkeys_menu(true)` releases what is down in the machine and drops the reports, `rawkeys_menu(false)` takes the keys held at that
moment as a baseline (not pressed late; their release is nothing). No USB typematic reaches the machine: the Amiga repeats keys itself.

**A500 shares the queue (repair, 2026-11-08).** Alex (translated): "it ran before wave 1 is done - this is a regression"; the host side is the same for all cores
except C128, SNES and NeoGeo. The A500 had no RAWKEY, so a USB keyboard typed nothing into it. The queue now lives in the shared `cores/amiga/rtl/trx/amiga_input.v`, with
`rk_*` in the shared `amiga_regs.v` / `amiga_core.v` (register 0x1C4, FEATURES b4); the A1200's own `amiga_input.v` fork and `amiga_input.diff` are removed
(`amiga_regs.diff` / `amiga_core.diff` regenerated, they hold only the A1200's own changes; `check_wrapper_fork.sh` and `core_sources.tcl` follow). The queue logic is the A1200's, line for
line. Core app: the A500 variant has `usbkeys = true` (same `RawKeySpec`, same table). Proof: bench `amiga_rawkey` (the former `amiga1200_rawkey`, now `cores/amiga/sim/run_rawkey.sh`, the same
four mutants) on the shared file, `amiga_input` unchanged, `test_rawkeys.cc` has the A500 press / release / room case in place of "the A500 untouched". Open: Vivado run (A500 and A1200 both rebuild:
their sources include cores/amiga), board (key tester on the A500).

**Left out, on purpose (to decide with Alex).** The reset combination Ctrl + Amiga + Amiga works from the C64 matrix only (`reset_kbd`); on USB the machine's
own keyboard reset is not built. The keyboard's caps LED is not driven from the machine. More than 6 keys at once are the boot report's limit.

**C128 P2 (2026-11-07, 1d85da7fa, in main).** The C128 descriptor takes the USB keyboard through `rawkeys` in held mode (`rawkeys.cc`), `c128_xkeys_apply` writes XKEYS / XSPECIAL (registers of T113 already in `cores/c128/c128_regs.v`); no gateware change. Test and controls in `tests/coreapp/test_c128.cc`, `run.sh`; no run result recorded here; board S2 open.

**What C128 (P2) and the A500 can reuse.** The USB hook (`usb_hid_set_keyboard_sink`, `rawkeys_usb_report`), the report ring, the press / release
difference, the menu handling (open: release, close: baseline) and the test rig (`test_rawkeys.cc`, the register model with room) are core-independent.
The C128 needs only its own translation of the held usages (`c128_xkeys_apply(io, down, n)`, `c128_keys.h`) in place of the Amiga table; the A500 needs the
`RAWKEY` queue ported into the shared `amiga_input.v` and `usbkeys = true` in its variant (both done 2026-11-08).

**Proof.**
- `cores/amiga/sim/run_rawkey.sh` (bench `amiga_rawkey`, until 2026-11-08 `amiga1200_rawkey`): press, release once, 8-event burst in order and 1.0-1.5 ms apart, room drops and returns,
  matrix + queue together, reset empties; the mutants `RK_MUT_IGNORE`, `RK_MUT_DOUBLE`, `RK_MUT_NOREL`, `RK_MUT_ROOM` all fail it.
- `tests/coreapp/test_rawkeys.cc`: all 87 table rows end to end (press, release), the 8 modifiers, the no-key usages, the menu keys, difference between
  reports, the CTRL pair, a roll-over report, CAPS toggle, the room (13 events at once, a 40-report burst, the queue of 16 never overflows), menu open /
  close baseline, reset, the A500 with the same events (2026-11-08); the controls are in `run.sh` (`rk_*`).
- Results (2026-10-31, builder): `tools/benches.sh amiga1200` ok (21 benches, `amiga1200_rawkey` with its 4 mutants failing as they must);
  `tests/coreapp/run.sh`: 74 test programs, 1063 controls, all failed as they must (16 `rk_*`); `tests/hostapp/run.sh` ok; host app and core app built
  for RISC-V in `u64-build-env:ci` (`coreapp.app` 1517696 bytes, `check_app_size` ok); `ci/lint.sh` via `tools/prepush.sh`: see the commit log.
- Cost: the queue is 16 x 8 bit of distributed RAM (about 12 LUT as RAM32M), a 5-bit count, two 4-bit pointers, the 3-flop toggle sync, the room
  compare, the register's 8-bit data + toggle + a 2-flop room sync and one word in the read mux: about 50-70 LUT and 45 flip-flops, under 0.3 % of the
  XC7A50T. The path `rk_q` -> `ev_data` is one 16:1 mux inside `sys_clk`. Not measured: a Vivado run is the coordinator's.
- Assumption (as the mouse's): two RAWKEY writes are at least 3 `sys_clk` apart (the register's acknowledge alone takes 3 `cpu_clk`).
- Open: the Vivado run (coordinator), the board (S3 with a key tester on the Workbench, S4).
- A500 Vivado (run 1277, 2026-11-08): with the queue the A500 missed timing by 0.051 ns (`hout/mtim`, before: +0.019 ns, run 1244). Cause: the
  A500 still had the floppy buffer and the sd fifos in LUTs (`paula_floppy` 1,699 LUT / 999 LUTRAM, `amiga_sdc` 190 LUTRAM; the A1200 435 / 7 and 0
  since T97 5.8/5.11). Fix: the A500 gets copies of the A1200's files (Alex: "copies, not links"): `cores/amiga/rtl/a500/` paula_floppy.v,
  paula_floppy_fifo.v, trx_afifo_rr.v; `trx/amiga_sdc.v` uses `trx_afifo_rr` (the A1200's `amiga_sdc.diff` is empty now); build and A500 sims follow.
- 2026-11-08: the A1200 has its own copy of `amiga_input.v` again (`cores/amiga1200/rtl/amiga_input.v`, Alex: "copies, not links"); the w1-a500
  merge had made both Amigas read `cores/amiga/rtl/trx/amiga_input.v`. Same logic in both (the files differ in comments only); the raw key bench
  `amiga_rawkey` runs on the A500's file.
