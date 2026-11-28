# T266 — the Home directory of every core is chosen with a folder picker, as a medium is mounted

**Status:** PROPOSED (2026-11-28).
**Repo:** TRX64U-MultiCore — the generic core app (`coreapp/base_ui.toml`, the row kinds of the settings page, the file browser's picker), for every core; no core code, no gateware.
**Number:** T266 (registry: the table in `TODO.md`).
**Origin:** GitHub issue #168. Alex at the board, 2026-11-28 (board test of `main-3624ec20`, NES, APP (F7), Settings, "Home directory"), translated: "QoL: a picker here like at the medium mount. For all cores. So a new todo and an enhancement on GitHub please."
**Depends on:** T180 D10 / T192.3 D30 (the Home directory setting), the medium mount's file picker.
**Milestone:** M3.

---

## §1 The problem

- **Who:** a person in any core's menu, APP (F7), Settings.
- **What they want:** set the folder the core's Home (F1) and Library open at.
- **What happens today:** the row "Home directory" is a text row (`coreapp/base_ui.toml`, `[[core_setting]] key = "Home Directory"`, `text = true`, `maxlen = 60`;
  `coreapp/src/base_rows.inc:26`). RETURN opens a text editor: "Type the text; RETURN takes it, an empty text clears it, <- cancels". The person has to type a path such as
  `/SD/NES/games` on the C64 keyboard, without seeing which folders exist; a typo gives a folder that does not exist.
- **Why it matters:** every core has the row; mounting a medium already offers a picker that walks the folders, so the same person meets two ways to name a place.

Parts:
- **P1** The row's kind: "Home directory" opens a folder picker (the medium mount's browser, folders only, a "use this folder" choice) instead of the text editor.
- **P2** Every core: the row is base UI (`base_ui.toml`), so the change is generic; no core's TOML or code changes.
- **P3** The value: the chosen folder is stored as today (the same key, the same path text), so a saved setting stays valid; clearing it stays possible.

## §2 Scenarios — what proves it is solved

| # | Part | Behaviour | Proof |
|---|------|-----------|-------|
| S1 | P1 | APP (F7), Settings, "Home directory", RETURN: a folder picker opens at the current value (or the medium's root when empty); RETURN on a folder enters it, a "use this folder" row takes it; the row then shows the path. | ue2emu feature with pictures, `ux-review` PASS |
| S2 | P1, P3 | The picked folder is what Home (F1) opens at after the next start; an empty value (cleared) behaves as today. | ue2emu feature; board step by hand |
| S3 | P2 | The same row behaves alike on three cores of different kinds (a C64-type core, an Amiga, a cartridge console). | ue2emu features of three cores |

## §3 The decision

Open: written with Alex before it is built (the picker's rows, how "clear" is offered, whether the text entry stays as a second way).

## §4 Out

Not decided yet.

## §5 Built

Nothing built yet.
