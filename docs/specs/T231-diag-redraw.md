# T231 — A cursor move in the diag core redraws only what changed, not the whole screen

**Status:** PROPOSED (2026-11-16).
**Repo:** TRX64U-MultiCore — the core application (`coreapp/`) as the diag core uses it; whether the fix is in the diag screens or in the generic
menu drawing is part of the design (T192 D24: no per-core C in the app).
**Number:** T231 (registry: `TODO.md`).
**Origin:** Alex, board test 2026-11-16 (package main-13c2cde9), note on the diag test: "Aber, jedes Cursor bewegen zeichnet wieder alles
neu ... das ist schlechte UX; auch wenns der diag core ist. Das braucht ein TODO nachdem wir das mit den Apps geradegebogen haben oder
dabei" (every cursor move redraws everything again; that is bad UX, even on the diag core; it needs a TODO, after we have straightened out
the apps, or while doing it).
**Depends on:** the app rework (T213, bitstream API + generic UI); built with it or after it, not before (Alex).
**Milestone:** M2.

---

## §1 The problem

- **Who:** a person moving the cursor in the diag core's menus on the board.
- **What happens today:** each cursor move redraws the whole screen (seen on the board, 2026-11-16) instead of the two rows that change.
- **Why it matters:** visible flicker and slowness; the same drawing path may serve other cores, so the cause is to be found before a fix
  is placed (diag-only or the generic menu).

Parts (P1, ...) and §2 scenarios are written with Alex when the work starts; nothing is built before.

## §2 Scenarios

To be written before any code (template rule).

## §3 Decision

Open.

## §4 Out

Open.

## §5 Build notes

None yet.
