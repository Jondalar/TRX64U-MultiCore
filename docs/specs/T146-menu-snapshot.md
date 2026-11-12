# T146 — The host menu is on the screen at once: a saved picture of the last menu, LVGL builds behind it

**Status:** PROPOSED (2026-10-28), asked by Alex; building.
**Repo:** TRX64U-MultiCore — the host application (`host-app/software/hostapp/`, `tests/hostapp/`). No gateware.
**Number:** T146.
**Origin:** Alex, 2026-10-28, TODO review of T101 (translated): the start "still says 'Building UI' or something ... do we
pre-render the UI to make LVGL faster?" and, on the answer (no; a saved picture would be a spec of its own): "Yes, but
render and save screens, please spec it and start the build, those are really annoying UX killers."
**Depends on:** T101 (splash, start phases), T133 (caches), T135 (direct boot: no LVGL on a direct start), T139 (the
flash driver erases every sector, `blockdev_flash.cc`).
**Milestone:** M3.

---

## §1 The problem

Board log of 2026-10-28 (run 1022 host app, caches on, menu after C= held):
`[t+500 ms] first frame (splash) shown`, `[t+680 ms] phase Building the menu`, `[t+3115 ms] phase menu built, drawing
the first frame`, `[host] view 0: update 38 ms, handler 1182 ms`, `[t+4345 ms] first menu frame shown`. For 3.8 s the
user sees a splash with "Building the menu" instead of the menu, every time the menu is shown at power-on and after a
return from a core.

- **P1 Show.** A picture of the menu as it was last shown must be on the screen within a few hundred ms of the app's
  start, before LVGL has built anything.
- **P2 Truth.** The picture must not lie: it is only shown when it was taken from the same boot.cfg (entries, order,
  names, default), the same picture mode and the same host-app build; otherwise the splash as today.
- **P3 Hand-over.** When LVGL's first frame is ready it replaces the picture without a visible jump (same layout), and
  keys pressed meanwhile are not lost.
- **P4 Save.** The picture is saved when the menu has settled after a change, not on every frame (flash wear, time);
  it has to fit the flash drive.
- **P5 Core menus: moved to T176 (Alex, 2026-11-02, translated: "we leave #4 out of the cores for now, split that into a new feature").**

## §2 Scenarios

```gherkin
Scenario: S1 the saved menu is shown at once
  Given a saved picture that matches boot.cfg, mode and build
  When  the host app starts into the menu
  Then  the picture is on the pixel layer before "phase Building the menu" ends; the console logs
        "[t+N ms] menu picture shown" with N < 1000 (board), and the first LVGL frame follows as today

Scenario: S2 a stale picture is never shown
  Given a saved picture taken before an entry was moved, renamed, added or removed (or another mode, another build)
  When  the host app starts
  Then  the splash is shown as today, not the picture (bench: the key over boot.cfg/mode/build differs -> refused)
  And   control: a check that ignores boot.cfg -> the bench fails

Scenario: S3 saving
  Given the menu after a change (move, rename, default, add, remove) or with a new mode
  When  it has been unchanged for 2 s
  Then  the picture is saved once (compressed), its size and time logged; no save while nothing changed

Scenario: S4 keys during the build
  Given the picture on the screen
  When  DOWN DOWN RETURN are pressed before LVGL is ready
  Then  they act in order once the UI is up (the third entry starts)
```

| # | Solves | Test | Where |
|---|---|---|---|
| S1 | P1, P3 | board console line + photo/video | board |
| S2 | P2 | `tests/hostapp` snapshot key test with control | CI app stage |
| S3 | P4 | `tests/hostapp` (codec round trip, size), render bench (save after settle) | CI |
| S4 | P3 | render bench (keys queued), board | CI, board |
| S5 | P5 | moved to T176 S1 | - |

## §3 Decision (proposed, built so; Alex may change it)

- **D1 What is saved:** the menu view's pixel buffer (RGB565, the mode's size) compressed with a simple run-length code
  (the menu is mostly flat colour), file `/Flash/menu.snap`, header: magic, format, width, height, key (CRC-32 over
  boot.cfg's text, the picture mode and the host app's git id), compressed length, CRC of the data.
- **D2 When:** after the menu has been shown and unchanged for 2 s following a change of boot.cfg, the mode or the
  rows' badges/icons; and once after the first full menu frame of a start whose key had no picture.
- **D3 When shown:** at start, when the menu (not a direct boot) is the target and the key matches: decoded straight
  into the pixel layer's shown buffer before LVGL starts, instead of the splash; LVGL renders into the other buffer and
  the layer flips to it when the first frame is complete. If the key does not match: splash as today.
- **D4 Keys** read while the picture is up are queued and handed to the UI once it is ready.
- **D5 Size limit:** a compressed picture above 1 MB is not saved (logged); the flash drive is not filled.

- **D6 Core menus:** moved to T176 D1 (2026-11-02).
## §3a Open questions

- **Q1** Also after a return from a core (same menu) — yes by D3; anything else to exclude?

## §4 Out

- A picture for the edit, browser, network or settings views.

## §5 Build notes

(filled in while building)

- **Board, 2026-10-29 (release main-922deb78, Alex: "Wow .. es klappt", "it works"):** second start after the install,
  console: `[t+3075 ms] menu picture shown (92864 bytes)`, `[t+5565 ms] phase menu built`, `[t+6870 ms] first menu frame
  shown`; the first start saved it (`menu picture: saved, 92864 bytes, 620 ms`). Works, but S1's N < 1000 ms is not met:
  the picture comes at 3.1 s (what runs before it is the next thing to measure).
