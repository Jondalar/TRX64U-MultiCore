# T207 — A hotkey is set by pressing the key, not by stepping through the keyboard

**Status:** DONE 2026-11-18: S5 on the board (learn a reset hotkey) closed as done, shipped and in use (Alex, quoted at the end of §5). Earlier status: BUILT, S5 board step open (2026-11-11). Corrected 2026-11-13 (status check): merged to main (00bc8d41e, with T205); S1-S4 proven (§5); S5 board open.
**Repo:** TRX64U-MultiCore — the generic core app's hotkey page (T187 D2: "Hotkeys >" in Input; `test_hotkeys`), every core.
**Number:** T207.
**Origin:** Alex, 2026-11-11, at the board (SCPU core, hotkey rows with LEFT/RIGHT values), translated: "We wanted to *learn* the hotkeys; now you can
only step through the keyboard with left/right. That is nonsense." No earlier spec records the learn mode (grep of docs/ found none).
**Depends on:** T187 D2 (the hotkey page), T205 (where Input lives).
**Milestone:** M2.

---

## §1 The problem

- P1: each hotkey (menu open/close, volume up/down, reset, swap joystick ports, picture mode) is chosen by LEFT/RIGHT through every key of the
  keyboard.
- P2: the modifier (CTRL, C=, ...) the same way.

## §2 Scenarios

| # | Solves | Scenario | Test | Where |
|---|---|---|---|---|
| S1 | P1 | RETURN on a hotkey row: "Press the new key (ESC cancels)"; the next key pressed becomes the hotkey, stored in the .cfg | core app test | apptests |
| S2 | P1 | a key already used by another hotkey: refused with a line naming the other hotkey (or swapped - to decide) | core app test | apptests |
| S3 | P2 | the modifier is learned the same way (only modifier keys accepted) | core app test | apptests |
| S4 | All | pictures of the page and the prompt | render bench + ux-review | apptests |
| S5 | All | board: learn a new reset hotkey, it resets | by hand | board (Alex) |

## §3 Decision

- **D1 (Alex, 2026-11-11):** hotkeys are learned by pressing the key.
- **D2 (coordinator's choice, Alex may change it):** S2 refuses (no swap): the line names the other hotkey ("R is already Reset"); nothing changes.
- **D3 (build):** LEFT / RIGHT stepping stays on the rows (it costs nothing and is the only way to reach RUN/STOP, which is ESC and cancels the prompt). D2 holds for stepping too: a step skips a key another hotkey (or the modifier) uses, the status line names it (`hk_value_refused`). RETURN on a row of the Hotkeys page opens the prompt.

## §4 Out

## §5 Build notes

Code: `HkLearn`, `hk_learn_value`, `hk_read_keys` in `coreapp/src/hotkeys.*`; `MenuCtl::begin_learn / learn_poll` (prompt = entry_ text mode with `learn_`), polled from `pump`; `run.cc` runs no hotkey while `learning()`.
The key is read from the physical keys (USB raw matrix + `keys_read`), not from the menu's key codes, so F1-F8, cursor keys and RETURN can be learned. Keys already down when the prompt opens (the RETURN) are ignored until released. ESC (menu key) or RUN/STOP (matrix 63) cancels.
Action key: taken at once. Modifier: the set of held keys, taken after HK_LEARN_SETTLE_MS unchanged or on release; must be one of the presets (CTRL, C=, or a pair of CTRL / C= / L SHIFT); the .cfg format is unchanged.

| # | Evidence |
|---|---|
| S1 | `test_hotkeys` T207 block: prompt up, held key ignored, menu keys swallowed, F1 learned, .cfg `Hotkey reset=F1`, ESC / RUN/STOP cancel, RESTORE learned |
| S2 | same block: used key (Volume up) and the modifier's key refused, line names them, value unchanged |
| S3 | same block: A refused, C= taken after settle, C= + CTRL pair, C= + L SHIFT released at once |
| S4 | render bench `hotkey_learn` (render.cc): every core with a hotkey map (15) at 1080p and 480p, page found by name through the menu tree, states page / key prompt / modifier prompt / refused / learned (150 states), each with view_bounds, legend fit, and the status line (`ui_status_probe`) below the last drawn row and inside the pane; pictures for VIC-20, C128, Atari 800 XL, A1200 (`hotkeys-<core>-<1080p\|480p>-<state>.png`, opened). ux-review: pass 1 FAIL (foot did not name RETURN; fixed), pass 2 FAIL (bench capture lacked the shell, fixed with invalidate), pass 3 PASS (ux-specialist, notes: prompt hides the list = D3 entry mode; "For ..." wording; ESC repeated in row and foot) |
| S2b | same block: LEFT / RIGHT steps over another hotkey's key (names Volume up), back again, 80 steps never land on a used key or the modifier's, modifier steps skip a preset using a hotkey's key |
| S5 | closed as done 2026-11-18, shipped and in use (below); was open: Alex at the board |

**2026-11-18 - board steps closed as done (shipped and in use):** S5 (learn a reset hotkey) - Alex 2026-11-15: "Under A there is nothing I would not class as
DONE! That has been in the builds for days/weeks, we had bugs and re-tests in part"; Alex 2026-11-18, shown these steps as open: "that is old stuff that has been
running for ages, we had this already 3-4 days ago" and "tick it off in the TODOs/specs, otherwise it shows up again with some agent" (translated).
