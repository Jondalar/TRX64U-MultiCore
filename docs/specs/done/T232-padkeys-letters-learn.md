# T232 — A pad or joystick can press any letter key, chosen by pressing it, so a game on IJKL (or WASD) plays from a USB pad

**Status:** DONE (2026-11-20, closed by Alex with GitHub #80, §5); merged 2026-11-18, after release 0.2.0. Order (Alex 2026-11-17): T228, T235, T234, then this, then the CPC (T233).
**Repo:** TRX64U-MultiCore — the generic core application (`coreapp/src/padkeys.h`, the input tab, the hotkey learn path); no core
RTL, no per-core C.
**Number:** T232.
**Origin:** Alex, 2026-11-16, after a tester's comment on GitHub #80: "Support IJKL, or let the user define keys please". Alex
(translated): "He wants to map keyboard keys from joystick/cursor. Sounds like app and cheap."
**Depends on:** T216 (pad/joystick as keys), T192 D24 / T213 D4 (generic app, no per-core code).
**Milestone:** post release (Alex 2026-11-16, translated: "That is post release."): built on branch t232-padkeys, merged to
`main` only after the next release.

---

## §1 The problem

A tester (GitHub #80) drives the PET, which has no joystick port, from a USB pad. T216 gives five rows ("Pad / joystick
up/down/left/right/fire"). Each row presses one key, chosen from a fixed list of 32 in `coreapp/src/padkeys.h:16-25`:

- digits
- cursor keys
- Space, Return, Home, Del, Stop
- W A S D Q E Z X
- + - * /

I, J, K and L are not in the list, so a game that plays on IJKL cannot be set up.

- P1: the list has 8 letters; the other 18 (I, J, K, L among them) cannot be chosen.
- P2: picking one of about 50 keys by cycling a row is slow.

## §2 Scenarios

| # | Solves | Scenario | Proof |
|---|---|---|---|
| S1 | P1 | Given the PET with Pad up/down/left/right set to I, K, J, L, when the pad's up is held, the core gets the key code of I (`col*8+row` of the C64 matrix, the code the PET translates), released with the pad | coreapp unit (happy path) on the same function the board uses |
| S2 | P1 | On the Input tab, every letter A-Z is offered. The old names and defaults are unchanged, and a stored `.cfg` value from T216 still loads as the same key | coreapp unit + ue2emu pictures of the Input tab |
| S3 | P2 | On a pad-key row, RETURN asks for a key. The person presses L on the C64 keyboard and the row reads "L"; ESC leaves the old value (the USB keyboard: T237, Alex 2026-11-18 "B") | ue2emu step + pictures, ux-review PASS |
| S4 | P1, P2 | On the board with a USB pad, up/left/down/right press I/J/K/L in a PET game and the game moves | board step by hand, with T216's pending USB pad test |

## §3 Decision

- D1 (Alex 2026-11-16, asked with three options: A-Z only / A-Z + presets / A-Z + learn by key press): "A-Z + learn by key
  press". Two parts:
  - Append A-Z to the generic pad-key list. It is append-only, so stored `.cfg` values and the defaults stay valid.
  - Give the five rows a learn step like the hotkeys (the generic "LEARN KEY" path): RETURN, then press the key.
  - Both stay in the generic service; every core that declares the rows gets it, with no PET C and no RTL change.

## §4 Out

- A preset task (Digits / WASD / IJKL): not chosen (D1 picked learn by key press over presets).
- USB-keyboard keys pressing other core keys (a keyboard remap): not asked. Alex's request is the pad and joystick pressing keys.
- Wiring the pad-key rows into cores other than the PET: not in this spec.

## §5 Built

- Branch `t232-padkeys`, built 2026-11-16/17 (Claude; agents, Opus review "COMMIT: yes", ux-review PASS with 3 low findings).
- **List.** `coreapp/src/padkeys.h` appends the 18 missing letters (B C F G H I J K L M N O P R T U V Y) as indices 32-49.
  - W A S D Q E Z X stay where they were, so A-Z is offered once each, and every old index, `.cfg` text and default is unchanged.
  - Codes are C64 matrix places (col*8+row), e.g. I=33 J=34 K=37 L=42. The review checked all 50 by script against `g_keys[]`
    (hotkeys.cc) and the PET's key table.
- **Learn.** A pad-key row is recognised by its content (`padkey_row`), and `hk_learn_item` / `hk_learn_value` accept it, so the generic
  RETURN path is the hotkeys' own: prompt "Press the new key", "For <row>", ESC cancels.
  - The keys are read the hotkey way (`hk_read_keys`, bit = matrix index).
  - SHIFT counts only on a cursor key: SHIFT + cursor gives the other cursor key. SHIFT held alone keeps the learn waiting.
  - A key not in the list is refused with "That key is not available - use A-Z, 0-9, Space or a cursor key"; two keys give "Press one key".
  - The keys held are taken on release, or after 350 ms, because a USB cursor-up arrives as SHIFT + cursor-down in one poll.
  - The tab footer shows "RETURN  LEARN KEY" on such a row.
- **Proof** (S1-S3), from the T232 block of `tests/coreapp/test_hotkeys.cc`:
  - list and codes
  - an old `.cfg` index loads as the same key
  - I K J L are learned and reach the PET's registers 0x164-0x167
  - refusal, two keys, ESC, SHIFT + cursor, SHIFT alone then SHIFT + cursor
  - goldens `menu_pet.txt` and `keywalk.txt`, which change on PET lines only
  - ue2emu pictures from `tests/ui/features_cores/t232_pet_padkeys.feature` (720p/1080p)
  - `dev.sh test` rc 0, riscv ok
- "Key Stop" cannot be learned (RUN/STOP and ESC cancel the learn); it stays reachable with LEFT/RIGHT.
- **Open:** S4, the board with a USB pad. The ux-review's two inherited findings (the "ESC cancels" wording twice, the foot spacing)
  belong to the hotkey learn and are not changed here.
- **Merge onto main (2026-11-18, after release 0.2.0):** main (T228) merged into t232-padkeys (7d1320a23). Three conflicts: the tab
  foot takes T228's words (T212 D16) and a pad-key row says "RETURN  LEARN KEY   <- ->  CHANGE", as a learn row on a page does;
  both new test blocks of `test_hotkeys.cc` (T232, T228) are kept; `keywalk.txt` is made again by the test (UPDATE_GOLDEN=1).
- **Review before the merge (2026-11-18, two reviewers):** the learn never sees a USB keyboard on the board: `hk_usb_keys()` is
  written only by a USB matrix filter that has no matrix to run on, so only the C64 keyboard is learned (inherited from T207; the
  unit test and the ue2emu step press C64 matrix keys). Alex, between "A: repair now" and "B: merge with the C64 keyboard, the USB
  learn as its own item together with T207": "B". S3 names the C64 keyboard; the USB learn is T237. Fixed from the review: the pad
  row's foot names `<- ->  CHANGE`; RESTORE at the learn says "That key is not available ..." (it said "No key"); the one-poll
  SHIFT + cursor test starts from another value and checks the learn ended clean, the right SHIFT (matrix 52) and RESTORE are
  checked; the ue2emu feature asserts the foot and "Cancelled" after RUN/STOP; manual 11.19.5 names the letters and RETURN.
- **Closed (2026-11-20, Alex, translated):** "So I can teach a USB pad .. whether that is really good UX is another matter.
  Close it!" T232 is DONE with these words, and GitHub #80 is closed. S4 has no separate board report. The code is in `main`
  since 2026-11-18 and ships with the next release; the pre-releases `t239.1-*` of 2026-11-20 carry it (package A). Learning a
  key from a USB keyboard stays T237.
