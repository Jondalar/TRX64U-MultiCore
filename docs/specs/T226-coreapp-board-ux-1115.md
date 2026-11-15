# T226 — The core app shows "Loading cartridge" while a ROM loads, and the pad button mapping sits on the Input tab

**Status:** PROPOSED (2026-11-15, Alex at the board).
**Repo:** TRX64U-MultiCore — core app only (`coreapp/`), no gateware.
**Number:** T226.
**Origin:** Alex 2026-11-15, SNES core on the board (translated): "SNES core - 'loading cartridge' would be good ... that takes a few
seconds." and "Pad mapping is not under 'Input' but under Actions F7 ... wrong place."
**Depends on:** T156/T181 pad mapping (as built), T225 (Amiga pad mapping lands on the Input tab directly).
**Milestone:** M2.

---

## §1 The problem

- P1: loading a cartridge image (SNES, and every core that loads a ROM the same way) takes seconds with no feedback.
- P2: the pad's "Map buttons" entry is in Actions (F7); Alex wants it on the Input tab, where the input settings are.

## §2 Scenarios

- S1 a visible "Loading cartridge ..." note while a cartridge/ROM image loads, gone when the core runs; ue2emu picture (SNES).
- S2 the pad mapping entry on the Input tab of every core that has it (NES, SNES, Neo Geo), not in Actions F7; ue2emu pictures.
- S3 board: Alex sees the note on the SNES and finds the mapping on the Input tab.

## §3 Decision

As Alex said in "Origin". Open details are asked before building.

## §4 Out

## §5 Built
