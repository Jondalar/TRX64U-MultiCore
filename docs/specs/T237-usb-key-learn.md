# T237 — A key pressed on a USB keyboard is learned like a key of the C64 keyboard (hotkeys and pad keys)

**Status:** PROPOSED (2026-11-18).
**Repo:** TRX64U-MultiCore — the generic core application (`coreapp/src/hotkeys.cc`, the USB keyboard path of
`host-app/software/io/usb/keyboard_usb.cc`); no core RTL, no per-core C (T192 D24, T213 D4).
**Number:** T237.
**Origin:** the review of T232 before its merge (2026-11-18) found that the learn never sees a USB keyboard on the board. Alex
chose between "A: repair it now" and "B: merge T232 with the C64 keyboard, the USB learn as its own item together with T207":
"B".
**Depends on:** T207 (hotkeys learned by pressing the key), T232 (pad keys learned by pressing the key).
**Milestone:** after release 0.2.0.

---

## §1 The problem

- **Who:** a person at the board with a USB keyboard, on any core: RETURN on a hotkey row (T207) or on a pad-key row (T232)
  shows "Press the new key".
- **What happens today** (read from the code by the T232 review, 2026-11-18):
  - The learn reads the keys with `hk_read_keys()`: the C64 keyboard (`CoreIo::keys_read`, the U64II_KEYB scan) ORed with
    `hk_usb_keys()`.
  - `hk_usb_keys()` returns `g_usb_lo/hi`, which only `hk_usb_matrix_filter` writes. Its one caller is
    `Keyboard_USB::applyMatrixState()`, which returns at `if (!matrix) return;` (`keyboard_usb.cc:227`); `setMatrix()` and
    `enableMatrix()` have no caller in `coreapp/`, `host-app/`, `app/` or `uicore/`.
  - The HID path (`run.cc`, `usb_hid_set_keyboard_sink(rawkeys_usb_report)`) does not write `g_usb_lo/hi`; for a core without raw
    keys (the PET: `rawkeys` 0) it does not run at all.
  - Result: a key pressed on the USB keyboard never ends the prompt; only USB ESC does (it reaches the menu as a key event).
  - The tests do not see it: the unit tests press keys through `FakeIo::key` (the C64 matrix), the ue2emu step `key l` holds a
    C64 matrix key.
- **Parts:** P1 the USB keys reach the learn's key state on every core; P2 a USB cursor-up (SHIFT + cursor-down in one report)
  learns as cursor up, as the C64 keyboard does; P3 nothing else changes for the cores that use the raw USB report.

## §2 Scenarios — what proves it is solved

| # | Part | Scenario | Proof |
|---|---|---|---|
| S1 | P1 | On the PET, RETURN on "Pad / joystick up", L on the USB keyboard: the row reads "Key L" | board step; a unit test that feeds a HID report |
| S2 | P1 | On a C64-family core, RETURN on a hotkey row (F7 Hotkeys), a letter on the USB keyboard: the hotkey takes it | board step; unit test |
| S3 | P2 | Cursor up on the USB keyboard at a pad-key row: "Key Cursor up" | unit test with the report the driver sends |
| S4 | P3 | A core with raw USB keys (rawkeys) types as before while no learn runs | the existing tests of that core |

## §3 Decision

Open. The review named one way: feed `g_usb_lo/hi` from the HID report for every core (`usb_usage_matrix_index` plus the modifier
and SHIFT bits, as `hk_usb_report_filter` builds `raw`).

## §4 Out

Nothing decided yet.

## §5 Built

Nothing yet.
