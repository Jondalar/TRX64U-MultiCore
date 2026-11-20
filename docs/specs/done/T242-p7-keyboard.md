# T242 — A C64 keyboard on header P7 works in the host menu and in every core app

**Status:** DONE (2026-11-20): D1-D6 on `integration` (`748d1d75`); board S1-S5 PASS with package A `t239.1-0210226e` and a
C64 header keyboard on P7 (D7).
**Repo:** TRX64U-MultiCore — the host app and the generic core app (`host-app/software/system/u64ii_init.cc`, built into both). No gateware change.
**Number:** T242.
**Origin:** GitHub issue #111, "P7 Keyboard on Motherboard does not work in Host- and Coreapps". The TRX64U-FW session read the cause from the code. Alex, 2026-11-20 (translated): "regarding the P7 bug, in my opinion the fix is in issue #111"; "wouldn't that be a nice attempt then, to build the host and all core apps once?"
**Depends on:** nothing. It rides on the first full GitHub run of `t239.1` (D3).
**Milestone:** M1.

---

## §1 The problem

- **Who:** a UE2 owner with a classic C64 keyboard on header P7 of the board, or a C64U owner who plugs one in there.
- **What they try:** to use the host menu and a core's menu (core app) with that keyboard, and to hold C= at power-on (T135 D2).
- **What happens:** no key reaches either menu.
  - The P7 matrix is scanned by the gateware's I2C keyboard scanner on the 1V8 bus.
  - `host/app_i2c.v` parks the scanner as soon as the app sends a command while `scan_en` is 0 (`want`, `kb_pause`). It hands the bus back only when the app writes `scan_en = 1` (register +8, bit 0) and its master is idle.
  - The driver's constructor writes `scan_enable = 0` (`io/i2c/hw_i2c_drv.h`). Then `custom_hardware_init()` talks to the codec and the two port expanders, so the scanner is parked from the first command on.
  - `i2c_lock` and `i2c_unlock` switch the scanner only when it was switched on by hand (`enable_scan(true, false)` sets `scanning`, `io/i2c/hw_i2c_drv.cc`). Neither the host app nor the core app makes that call; Gideon's firmware makes it in `u64/u64_config.cc:1168`.
  - On a C64U the built-in keyboard comes over the Bling UART (`bl_keys`), not over I2C, so it works and the fault stays hidden.
- **Why it matters:** a UE2 owner has no keyboard in the menus, and the power-on C= check cannot see a key on P7.

## §2 Scenarios

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 | A key on P7 moves the cursor in the host menu | by hand, keyboard on P7 | board |
| S2 | P1 | A key on P7 works in a core's menu (the core app) | by hand, keyboard on P7 | board |
| S3 | P1 | C= held on P7 at power-on is seen by `menukeys.cc` (T135 D2) | by hand | board |
| S4 | P1 | The legacy start of the firmware from the host still brings up its clocks (`legacy_clocks()`) | by hand | board |
| S5 | P1 | The built-in C64U keyboard (Bling) still works in both menus | by hand | board |

## §3 Decision

- **D1:** `custom_hardware_init()` ends with `i2c->enable_scan(true, false)`. From then on `i2c_lock` parks the scanner and `i2c_unlock` hands the bus back after each transaction.
  - The call only writes the register; FreeRTOS is needed only to switch the scanner off. So it is safe before the scheduler, and nothing between it and the scheduler start takes the lock (`portable/riscv/riscv_main.c`).
  - One place serves both apps, because both build this file. No per-core code (T192 D24).
- **D2:** `legacy_clocks()` (`hostapp/legacy.cc`) stays as it is. Its CDCE writes stay inside the lock, with CDCE open. The unlock's `scan_enable` write closes CDCE again, after the last I2C use before the hand-over. *Replaced by D6:* the scanner is off before `legacy_clocks()`, so the unlock writes nothing and CDCE open stays set.
- **D3:** the fix is a commit on `t239.1`, so the first full GitHub run builds the host and all core apps with it (Alex, Origin).
- **D4:** `enable_scan()` no longer sleeps after `scan_enable = 0`. `host/app_i2c.v` keeps the master busy until the scanner has stopped between two transactions, so the first command waits in hardware. With the scanner on, the firmware's `vTaskDelay(2)` cost every `i2c_lock` 5-10 ms; the core app takes the lock every 500 ms for its EDID poll in the main loop, ahead of `Video::poll`.
- **D5:** `custom_hardware_init()` no longer writes the keyboard expander 0x42. `host/c64u_keyboard.v` sets its port directions before every line it scans and may be parked inside a line; writes from the init changed them there, and after the enable that line could read as all keys down for one scan, on any board. With C= in that line the power-on check (T135 D2) would stop in the menu.
- **D6 (Alex, 2026-11-20): the scanner is off before the hand-over, as in PR #112.** `legacy_jump()` calls `i2c->enable_scan(false, false)` before `legacy_clocks()`: its first command parks the scanner between two transactions and it stays parked, so no scanner transaction runs on the I2C bus at the IPROG into the firmware or a large core, and CDCE open stays set as in 0.2.0. PR #112 (a contributor, tested on an Ultimate 64 Elite II) fixed #111 too; its hand-over line is taken over, the rest would enable the scanner a second time in each app. Alex (translated): "yes, better safe than sorry. Then take that over. Comment it in the PR accordingly."
- **D7 (Alex, 2026-11-20): S1-S5 run on the board with package A (`t239.1-0210226e`), a C64 header keyboard on P7.** Asked A (all on
  the board, a keyboard on P7), B (S4 and S5 on the board, S1-S3 by the reporter of #111 with 0.2.1) or C (no board run; ue2emu has
  no P7 scanner, only the bit `scan_enable`), Alex (translated): "A. I'll shut the board down when everything is on it, open it and
  plug in the header keyboard."

## §4 Out

- The gateware: `host/app_i2c.v` and the keyboard scanner are the same as in TRX64-Ultimate.
- The Ultimate app (`trx64g`, built from `vendor/gideon`) already makes the call.

## §5 Built

- `69db9dec` (D1) on `t239.1`, pushed at Alex's word ("I would push it, the review looks anyway", translated).
- Review of `69db9dec`: two readers (the bus hand-over; regressions), 643k tokens. Two findings, both checked against the code: the sleep in `i2c_lock` (D4) and the expander writes of the init (D5). Minor: `nau8822_enable_hpout` writes without the lock; nothing calls it.
- `d478ae57` (D4, D5) on `t239.1`; the run with the unfixed D1 was cancelled. Full GitHub run 44737131946 carries D1-D5.
- Run 44737131946: apptests and images green with D1-D5 (host app, core app and every core's app built). Its collect went red for a reason outside T242 (T239.1 §5, step 17).
- `b4b6f086` (D6) on `t239.1`, co-author the PR's author; PR #112 commented (it is not merged as it is). D1-D5 are in the pre-release `t239.1-7eb32b4b` (run 44780149112); D6 comes with the next run.
- Board test 2026-11-20 (Alex; package A `t239.1-0210226e`, the host updated from the File Browser first; checklist "Board Test
  11-20", 21:46-21:58 CET), every item PASS: S3 (C= held on the P7 keyboard at power-on with Direct boot on: the host menu, no direct
  start), S1 (P7 keys in the host menu), S2 (P7 keys in a core's menu), S4 (a full firmware started from the host), S5 (the built-in
  C64U keyboard in the host menu and in a core's menu). Alex: "alles geklappt !!!" (everything worked). T242 is DONE and moves to
  `docs/specs/done/`.
