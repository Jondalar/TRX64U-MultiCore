# T252 — The Port Diagnostics core switches the I2C behaviours of T242 live, so a person finds the one that breaks the picture

**Status:** IN PROGRESS (2026-11-21). Asked by Alex, 2026-11-21.
Open (one line, as in the TODO table): IN PROGRESS 11-21: built; open: S1-S3
**Repo:** TRX64U-MultiCore — the core application (`coreapp/src`), the I2C driver it builds (`host-app/software/io/i2c`), the
Port Diagnostics core's capability list (`coreapp/cores/portdiag.cc`).
**Number:** T252.
**Origin:** T241 D4 on the board, 2026-11-21: the c64scpu build `c64scpu_t241fix` shows picture dropouts and a colourless
SuperCPU start animation. The same bitstream with the application of the T241 baseline (`c64scpu_241_oldapp`) shows neither.
Alex (translated): "no picture dropouts, and the start animation was right too ... so we have a regression in the app?!"
The application files that differ between the two builds include the three of T242. Alex (translated): "Sure, but then it is
OUR fix, because the contributor has it running successfully on his side. Can you build that into the diag app with switches?
Then one can play through directly what exactly triggers it." Asked which diag app: "The diag core."
**Depends on:** T242 (the behaviours switched here).
**Milestone:** M2 (release 0.2.1: T242 ships with it).

---

## §1 The problem

- **Who:** Alex at the board, looking for the cause of the picture dropouts.
- **What happens today:** T242 changed three things in the core application. Our T242 and the contributor's PR #112 differ in
  two of them. Nothing can be switched on the board, so each test needs a build.
  - **D1 (the scanner runs):** `custom_hardware_init()` ends with `i2c->enable_scan(true, false)`. Before T242 the
    application's first I2C command parked the gateware's keyboard scanner (`host/app_i2c.v`), and it stayed parked: the
    application kept the buses. Since T242 the scanner has the buses between any two locked transactions of the application.
    The scanner runs on all three buses at once (1.8 V, HDMI, 3.3 V; the comment and the wiring are in
    `cores/c64scpu/mono/top.v` and `cores/diag/mono/top.v`, `scl_lo`/`sda_lo`).
  - **D4 (no wait):** `Hw_I2C_Driver::enable_scan()` no longer calls `vTaskDelay(2)` after `scan_enable = 0`. PR #112 kept it.
  - **D5 (no expander writes):** the init no longer writes the keyboard expander 0x42 (0x06 = 0x00, 0x07 = 0xFF,
    0x02 = 0x00). PR #112 kept those writes, and before its enable it set both ports to input (0x06 = 0xFF, 0x07 = 0xFF).
  - The application's only periodic I2C user is the screen check of `picture_service()` (`coreapp/src/picture.cc`): every
    `PICTURE_POLL_MS` (500 ms) it reads two EDID bytes in a locked transaction. With the scanner running, each check parks
    the scanner and hands the buses back.
- **Why it matters:** T242 ships with 0.2.1. A c64scpu with dropouts cannot be played.
- **The parts:**
  - **P1** each of D1, D4 and D5 can be switched back on the board, live, in the core where Alex looks.
  - **P2** the periodic hand-over (the screen check) can be switched off, so the scanner's own traffic and the hand-over can be
    told apart.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 (P1, P2) the rows and their operations
  Given the core application built on the Mac (tests/coreapp)
  When the Port Diagnostics core's menu is built
  Then its settings end with the four rows of D2, each with the T242 value as default
  And a core without the capability i2c_switches gets none of them
  And each row's operation calls its board function with the value chosen, and the screen check row switches the check

Scenario: S2 (P1, P2) the board image
  Given the branch
  When the core application is built for RISC-V (tools/dev.sh riscv) and tools/check_d24.py runs
  Then both pass

Scenario: S3 (P1, P2) the board
  Given diag_t252.cor (the Port Diagnostics core with this application) started from the menu
  When Alex changes the four rows live and watches the picture
  Then §5 records for each setting whether the picture drops out
```

| # | Solves | Test | Where it runs |
|---|---|---|---|
| S1 | P1, P2 | `tests/coreapp/test_i2csw.cc` (`tests/coreapp/run.sh`) | Mac |
| S2 | P1, P2 | `tools/dev.sh riscv --branch t252-i2c-switches`, `tools/check_d24.py` | builder, Mac |
| S3 | P1, P2 | the rows by hand, the picture by eye | board (Alex) |

## §3 Decision

- **D1 (Alex, 2026-11-21): the switches go into the Port Diagnostics core** ("The diag core.").
- **D2: four rows in the core's settings tab** ("Port Diagnostics Settings", after the A/V rows). Each is applied at once
  when changed and at the core's start (`regset_apply`), and is kept in the core's `.cfg`. The defaults are T242's behaviour,
  so a fresh start is the build as it ships.
  - **P7 keyboard scanner:** On (T242 D1) | Off (before T242: the application parks the scanner with one command and keeps
    the buses).
  - **Screen check every 500 ms:** On | Off (no EDID read and no hand-over of the buses every 500 ms). Not a T242 change: P2.
  - **Wait after scanner stop:** None (T242 D4) | 2 ticks (before T242 and PR #112: `vTaskDelay(2)` after `scan_enable = 0`).
  - **Keyboard expander setup:** None (T242 D5) | Before T242 (0x06 = 0x00, 0x07 = 0xFF, 0x02 = 0x00) | PR 112 (0x06 = 0xFF,
    0x07 = 0xFF). Written on the 1.8 V bus when chosen and at the start, as the init wrote them.
  - Switching the scanner on and off happens inside the I2C lock: a task in the middle of a two-transaction read (the EDID)
    must not lose the buses between the two.
- **D3 (T192 D24): generic code, the core names a capability.** `coreapp/src/i2csw.cc` adds the rows (`i2csw_extend`, called
  by `menu_build`) and registers their operations (`ops_of` and `desc_check`) for a core whose capability list names
  `i2c_switches`. The Port Diagnostics core names it in its list (`portdiag.cc`, the same line count). The board calls are
  compiled only for the board (the native builds define `VIDEOMODE_NO_BOARD`). The driver gets `set_park_wait()`; its default
  0 is T242 D4, and the host application never calls it.
- **D4: the board build.** The Port Diagnostics core's bitstream from the pipeline's cache (no Vivado run), the application of
  this branch, packed as `diag_t252.cor` into `/SD/cores/`.

## §4 Out

- The c64scpu does not get the rows (D1). A core gets them by naming the capability.
- The host application: unchanged (`legacy.cc`, T242 D6).
- The colourless SuperCPU start animation: the Port Diagnostics core has none.
- The other application changes between the two c64scpu builds are not switched.
- The gateware: unchanged. T252 only finds the trigger.

## §5 Built

- **2026-11-21, branch `t252-i2c-switches`** (from `integration` `3ec353c7`), `553b6a56`: D2, D3. `tools/check_d24.py` ok
  (`portdiag.cc` keeps its line count).
- **S1 PASS (2026-11-21):** `CA_GROUP=shared tests/coreapp/run.sh`: "test_i2csw: 72 checks, 0 failed"; the group: "36 test
  programs passed, 40 parallel controls ..., every negative control failed as it must".
- **S2 PASS (2026-11-21):** `tools/dev.sh riscv --branch t252-i2c-switches`: "dev.sh riscv: ok", `coreapp.bin` sha256
  `4c54080b...`. `tools/dev.sh cor diag` cannot pack it: "diag is no .cor core of this tree ... a large core's .cor is packed by
  ci/package.sh".
- **The board file:** `diag_t252.cor` (3,485,659 bytes), packed with `tools/pack_large_cor.sh` as `ci/package.sh` does, named
  "Port Diagnostics T252" so the boot menu tells it from `diag.cor`. The bitstream is the pipeline's entry
  `bit-mono-diag-27d069ea27f30f6f` of the builds repository (from `cfcd9f62`, 2026-11-19, WNS +0.165 ns, SHA256SUMS checked):
  nothing under `cores/diag` (without `sd`, `superseded`, `sim`), `cores/common` or `contract` changed since. Its
  `app_i2c.v`, `c64u_keyboard.v` and `mode_ctl.v` are the c64scpu's (`cores/*/mono/host/`); `cdce_mode.v` differs.
- **Not seen in ue2emu:** `tests/ui` cannot drive this core. Its Home page redraws the live pins every 250 ms, so the
  harness's `settle` (the same revision twice, 120 ms apart) never ends: "the menu does not come to rest", with this
  image and with one without T252 (`t251-printf-sizes`).
