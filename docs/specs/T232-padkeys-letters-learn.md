# T232 — A pad or joystick can press any letter key, chosen by pressing it, so a game on IJKL (or WASD) plays from a USB pad

**Status:** IN PROGRESS (2026-11-16, Alex).
**Repo:** TRX64U-MultiCore — the generic core application (`coreapp/src/padkeys.h`, the input tab, the hotkey learn path); no core
RTL, no per-core C.
**Number:** T232.
**Origin:** Alex, 2026-11-16, after a tester's comment on GitHub #80: "Support IJKL, or let the user define keys please". Alex
(translated): "He wants to map keyboard keys from joystick/cursor. Sounds like app and cheap."
**Depends on:** T216 (pad/joystick as keys), T192 D24 / T213 D4 (generic app, no per-core code).
**Milestone:** M1.

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
| S3 | P2 | On a pad-key row, RETURN asks for a key. The person presses L on the USB keyboard and the row reads "L"; ESC leaves the old value | ue2emu step + pictures, ux-review PASS |
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

(filled in while building)
