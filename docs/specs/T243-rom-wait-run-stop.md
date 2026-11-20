# T243 — RUN/STOP on the ROM wait screen takes the way back to the host

**Status:** BUILDING (2026-11-20).
Open (one line, as in the TODO table): Built on t239.1 (8b2279f9); units S1-S4 PASS; ue2emu S5 and board S6 open
**Repo:** TRX64U-MultiCore — the generic core application (`coreapp/src/boot.cc`, `run.cc`, `ui.cc`), its unit test.
**Number:** T243 (GitHub #106 is the origin; the row in `TODO.md` is the coordinator's).
**Origin:** GitHub issue #106 (Alex, 2026-11-19, bug): the core's ROM screen offers "RUN/STOP to return to host" and RUN/STOP does nothing. Alex asked for the fix before the integration (2026-11-20).
**Depends on:** T104.2 D2 (`can_return_host`), T115 7 H1 (the held page's way back).
**Milestone:** M1.

---

## §1 The problem

A core starts, a required ROM is missing, and the application waits for it.

- **P1 The screen promises a key.** The ROM wait screen is `ui_message("ROM files", ...)`. The shell around every message pane sets the left footer to `R/S  CLOSE` (`ui.cc` `shell_info`, `footer_left`). The literal text "RUN/STOP to return to host" is in no source file of `coreapp/` or `uicore/`; the footer is what the person reads as the promise.
- **P2 The key is not read.** `boot_start` step 4 loops `scan, message, ui->tick(), delay_ms()`. `BoardUi::tick()` is `ui_poll()` only. No key is read in that loop, so RUN/STOP does nothing.
- **P3 The wait is deaf for long stretches.** One pass sleeps 500 ms, 5000 ms after a refused ROM set. A short key press on the C64 matrix would fall between two polls.
- **P4 Boards without a way back.** The way back exists only where an ICAP loader can take the FPGA back (`BoardProfile::can_return_host`). A screen that promises it elsewhere is wrong again.

## §2 Scenarios

| # | Solves | Scenario | Proof |
|---|---|---|---|
| S1 | P2, P3 | Required ROM missing, RUN/STOP pressed on the wait screen: the host's way back is called once, the core was never released. | Unit test `test_boot_atari`, block "T243 S1" (`ui.returns == 1`, `reset_releases == 0`, `!c.started`). |
| S2 | P4 | Board without the ICAP loader: the text names no key, RUN/STOP is not even asked, nothing is called. | `test_boot_atari`, block "T243 S2" (`ui.asks == 0`, `ui.returns == 0`, text without "RUN/STOP"). |
| S3 | P2 | The way back fails (told with its reason, the wait goes on); then the ROMs arrive and the core starts as before. | `test_boot_atari`, block "T243 S3"; the unchanged first block of the test covers a ROM arriving with no key pressed. |
| S4 | P2 | The USB keyboard's ESC asks as RUN/STOP does; no other key does. | `test_boot_atari`, block "T243 S4" (`boot_leave_key`). |
| S5 | P1 | The picture of the ROM screen: the text names RUN/STOP only where the way back exists; the footer promises no close. | ue2emu by the coordinator (the menu simulator does not run `boot_start`; it cannot show this screen). |
| S6 | P2 | On the real board the matrix RUN/STOP reaches `getch_nowait` during the wait and the host comes back. | ue2emu / board by the coordinator. |

## §3 Decision

- **D1 One way back, the host's own.** The wait calls `hw_return_to_host`, the call behind "Return to host menu" (`menuctl.cc` `run_fixed`). Where the board has no loader, nothing is offered (`boot_offers_return`: `can_return_host`, a context without a board counts as the MultiCore host, as in `menutree.cc`).
- **D2 Through `BootUi`, not a new `BootResult`.** `BootUi` gets `leave_asked()` and `return_to_host()`, both with a default (no key, no way back), so every other implementation and test fake stays as it is. `boot_start` is called on the one task before the executor exists, holds the core, and has the screen; calling the way back inside the loop is what the held page does (`coreapp_held`). A result `BOOT_RETURN_HOST` would force the caller to restart the sequence when the way back fails; inside the loop a failure only prints its reason and the wait goes on.
- **D3 Keys.** `boot_leave_key`: `KEY_BREAK` (the matrix's RUN/STOP) and `KEY_ESCAPE` (the USB keyboard). `BoardUi::leave_asked()` drains `getch_nowait()`.
- **D4 The wait polls every 50 ms.** The pass's sleep is cut in 50 ms slices; `leave_asked()` runs in each. The scan and `tick()` still run once per pass.
- **D5 The text names the key.** The ROM text gets "Press RUN/STOP to return to the host menu." only when D1 offers it. A failure appends "The way back did not work: <reason>".
- **D6 The message pane's left footer is empty.** `ui_message` no longer shows `R/S  CLOSE`: nothing on a message pane closes with that key. Keys of a pane stay in its text or in `g_msg_foot`.

## §4 Out

- No change to the held page (RETURN there, T115).
- No per-core code; no ROM-wait screen of its own.
- No other message pane gets a key.

## §5 Decisions taken while building

- The literal hint of the issue is not in the tree; the footer `R/S  CLOSE` of the message shell (`ui.cc` `shell_info`) is the nearest text. D6 removes it for message panes.
- Native test run (this Mac, `c++`, the flags of `tests/coreapp/run.sh`): `test_boot_atari: 74 checks, 0 failed`. The test models "the way back does not come back" with `longjmp` (the suite builds with `-fno-exceptions`).
- S5 and S6 are open for the coordinator's ue2emu run.
