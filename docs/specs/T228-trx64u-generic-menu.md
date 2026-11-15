# T228 — trx64u runs the generic core menu (the structure of the other cores), not its own trx64u menu

**Status:** PROPOSED (2026-11-15, Alex).
**Repo:** TRX64U-MultiCore — core app (`coreapp/cores/trx64u/menu.toml`, `coreapp/cores/trx64u*`), the import pin
`release/imports/trx64u.pin` (DESC/OPS). No gateware (the bitstream stays TRX64-Ultimate's).
**Number:** T228.
**Origin:** board 2026-11-15: trx64u.cor (bitstream u16 422f9dce, core app 60bfda79) shows the old trx64u menu layout (not the generic
logic) and every menu action takes 2-3 s. Alex (translated): "The ring is not it ... that is the old app. The ring stops in the freeze."
Asked: a) find the delay in the trx64u path, or b) move trx64u to the generic menu. Answer: "b, but the UX senior developer reworks the
menu structures ... they differ strongly from the generic approach."
**Depends on:** T212 (core menu areas, parked branch t212-core-areas), the UX concept (TRX64-Ultimate/docs/lvgl_ui_project).
**Milestone:** M2.

---

## §1 The problem

- P1: trx64u has its own menu description and code path that differs strongly from the generic structure of every other core.
- P2: actions in that path take 2-3 s (cause not measured; the ring is ruled out by Alex: it stops in the freeze).
- P3: functions only trx64u has (TRX64 features) must find a place in the generic structure.

## §2 Scenarios

- S1 UX design: the UX specialist maps every trx64u menu entry to the generic structure (keep / move / merge / drop), with ue2emu
  mock pictures; Alex decides entry by entry where it is not obvious.
- S2 build: trx64u uses the generic menu; its own entries live where S1 decided; ue2emu pictures, ux-review PASS.
- S3 board: menu actions on trx64u respond like the other cores (no 2-3 s); every TRX64 function reachable.

## §3 Decision

- D1 (Alex 2026-11-15): b) move trx64u to the generic menu; the UX specialist reworks the menu structure first (S1) because the
  trx64u menu differs strongly from the generic approach.

## §4 Out

## §5 Built
