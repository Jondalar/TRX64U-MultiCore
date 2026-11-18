# T226 — The core app shows "Loading cartridge" while a ROM loads, and the pad button mapping sits on the Input tab

**Status:** BUILT (built 2026-11-15; was PROPOSED) (2026-11-15, Alex at the board); board step S3 closed as done 2026-11-18 (shipped and in use, Alex; end of §5). Open: §5 "Open (Alex)" (the "Pad map" setting stays on the core's own tab).
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

- **S1 (built):** `MediaLoading` (`coreapp/src/engines.h`, beside `MediaSaving`) calls the new `CoreCtx::loading` hook; the application's `loading_note` (`run.cc`) shows the
  existing message view "Cartridge / Loading cartridge..." with the menu up and clears it when the operation returns (core released, or the error shown by the menu).
  A write-back inside a load puts the note back after its own "Saving media". Used in `snes_run`, `nes_run`, Neo Geo `run_file` / `run_dir`, `c64_cart_run`.
  Not on the stored-mount path at boot (menu closed: `menu_open` false, nothing is shown, as for the save note). Picture: `tests/ui/out/t226-snes-loading.png`
  (`tests/ui/features_cores/t226_snes.feature`, a synthetic 1 MiB LoROM image on the stick).
- **S2 (built):** a task whose `group` is "input" is a row of the settings tab that lists that group (Input) and not of Actions, unless a tasks tab lists the group
  (`task_in_settings_tab`, `menutree.cc`); the three cores set `group = "input"` on "Map buttons, pad 1/2" - same operation (`menu.pad_map_1/2`), same dialog and storage.
  Pictures: `t226-{snes,nes,neogeo}-f5-input.png` (Hotkeys, Map buttons pad 1, pad 2), `t226-{snes,nes,neogeo}-f7-actions.png` (no mapping entry). The golden dumps
  (`tests/coreapp/golden/menu_*.txt`) use a route without Input tab and stay as they are.
- **Emulator profiles for the pictures:** `tests/coreapp/emu/profiles/{snes,neogeo}.toml`, stick fixtures in `tests/ui/fixtures.py`, step "I press RETURN and save the picture ... while it works".
- **Open (Alex):** the setting "Pad map = Default / Custom" stays on the core's own tab; moving it to Input beside the mapping rows is not done (not in Origin).
- **2026-11-18 - board steps closed as done (shipped and in use):** S3 (the "Loading cartridge" note on the SNES; Map buttons on the Input tab) -
  Alex 2026-11-15: "Under A there is nothing I would not class as DONE! That has been in the builds for days/weeks, we had bugs and re-tests in part";
  Alex 2026-11-18, shown these steps as open: "that is old stuff that has been running for ages, we had this already 3-4 days ago" and "tick it off in
  the TODOs/specs, otherwise it shows up again with some agent" (translated).
