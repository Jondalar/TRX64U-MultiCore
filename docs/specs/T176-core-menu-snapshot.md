# T176 — The core applications' menus are on the screen at once from a saved picture

**Status:** PROPOSED (2026-11-02), deferred: split out of T146 by Alex, not scheduled.
**Repo:** TRX64U-MultiCore — the generic core application (`coreapp/`, `tests/coreapp/`). No gateware.
**Number:** T176.
**Origin:** T146 P5, added 2026-10-29 after GitHub issue #4 (Alex: "extend", translated). On 2026-11-02 Alex split it out
(translated): "we leave #4 out of the cores for now, split that into a new feature here as a todo." T146 stays the host menu only.
**Depends on:** T146 (codec, header, truth rule, hand-over), built for the host.
**Milestone:** M3.

---

## §1 The problem

- **P1 Core menus (was T146 P5).** The generic core application's menu gets what T146 gives the host menu: measured on the
  board (922deb78, JTAG console of the Apple IIe core) "menu open 686 ms after the request" and up to 340 ms render+flush per
  key. A saved picture of the core's menu is shown at once when the menu opens; the same truth rule (T146 P2: core,
  application build, its settings) and hand-over (T146 P3).

## §2 Scenarios

| # | Part | Proof | Where |
|---|---|---|---|
| S1 | P1 | `tests/coreapp` key test with control; board console "menu open" line < 200 ms with a picture | CI, board |

## §3 Decision (proposed, was T146 D6; not decided)

- **D1 Core menus:** the same codec and header as T146; the key adds the core id and the core's `.cfg`; the file sits beside
  the `.cor` (`<core>.snap`); the shown view is decoded into the overlay before the menu's first LVGL frame.

## §4 Out

Nothing decided yet.

## §5 Build notes

Not started (T146's commit b6249f987 added P5 to the spec only; no core application code exists).
