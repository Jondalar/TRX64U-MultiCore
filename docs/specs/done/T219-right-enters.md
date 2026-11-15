# T219 — RIGHT goes in, LEFT goes out, in every file browser (GitHub #90)

**Status:** DONE 2026-11-15 (Alex 2026-11-15: "under A there is nothing I would not class as DONE! That has been in the builds for days/weeks, we had bugs and re-tests in part.") Open board/scenario items are closed by Alex's decision, not separately recorded. Earlier status: BUILT (2026-11-14), ue2emu pictures in `docs/pictures/gh90/`; board not run. 2026-11-15 sync: merged to main (2b5d80b73), code checked (`host_core_ui.cc:634`, `menuctl.cc:2545-2547`, `filemgr.cc:863`); consistent.
**Origin:** GitHub #90 (a user): in the generic core application's file browser ENTER opens the selected folder and LEFT goes to the parent, RIGHT does not open the folder - inconsistent and unlike the Ultimate firmware. Alex, 2026-11-14 (translated): "he is right; didn't we already say with the configs and zips: LEFT out (one folder up), RIGHT in (into the folder or zip or config)".
**Builds on:** T198 D2 (RIGHT opens the config overlay on a core row / `.cor`, symmetric to LEFT = back), T197 (a `.zip` is unpacked with Z, never browsed), `docs/lvgl_ui_project/NAVIGATION.md`.

## §1 The problem
- Host F3 (`host_core.cc`, `HCX_BROWSE`): RIGHT is `CONFIG_LIST` on every row; on a folder it did nothing.
- Core app Library (`menuctl.cc` `MI_RIGHT` -> `activate(true)`): RIGHT on a folder opens the folder's action sheet (T104.2, T180 #42), not the folder.
- Core app pick browser (`pick_key`): RIGHT already enters a folder (unchanged).

## §2 Scenarios
| # | Scenario | Proof |
|---|---|---|
| a | Host F3: RIGHT on a folder enters it, same as RETURN | ue2emu picture `docs/pictures/gh90/host-1-usb0-cores-folder-selected.png` -> `host-2-right-into-cores-folder.png` (RIGHT on `cores`) |
| b | Host F3: RIGHT on a medium row at "/" enters the medium | `host-1-...` -> `host-2-...` (medium row `usb0` entered), `host-4-right-into-games-folder.png` |
| c | Host F3: LEFT goes up again | `host-3-left-back-to-usb0.png`, `host-7-left-up-to-games.png` |
| d | Host F3: RIGHT on `..` goes up (as RETURN) | `host-10-right-on-dotdot-goes-up-to-usb0.png` |
| e | Host F3: RIGHT on a `.cor` still opens the config overlay (no configs: nothing, T198 D9) | code path unchanged (`cfgov_open`); `host-6-right-on-cor-file-stays-in-sub.png` (cor selected, RIGHT does not leave the folder) |
| f | RIGHT on a `.zip` = what RETURN does on a `.zip`: nothing (zips are not browsable, only Z unpacks) | `host-8-zip-selected-in-games.png` -> `host-9-right-on-zip-nothing-happens.png` |
| g | RIGHT on a plain file: unchanged | code: only the dir branch changed |
| h | Core app Library: RIGHT on a folder enters it | `gh90-core-2-inside-big-after-right.png` |
| i | Core app Library: LEFT goes up again | `gh90-core-4-left-back-at-usb0.png` |
| j | Core app Library: RIGHT on `..` opens the sheet of the current folder with every folder action (Copy to, Move to, clipboard, Rename, Delete, Create folder, Mark all/none, Set as home) | `gh90-core-3-right-on-dotdot-folder-sheet.png` |
| k | Core app Library: Delete from inside asks for the folder and afterwards the Library stands in the parent | `gh90-core-5/6/7-*.png` |
| l | Core app Library: at the top of a medium and at "/" the `..` sheet has no Rename/Delete/Copy/Move (no folder to act on), only the folder-here items | code: `inside` in `MenuCtl::activate` |

## §3 Decision
RIGHT on a folder row of a file browser does what RETURN does there (enter; `..` goes up); a `.cor`/core row keeps its config overlay; a `.zip` is not browsable, so RIGHT does what RETURN does (nothing); LEFT unchanged. Footers keep "RETURN open"; adding RIGHT would not fit the T187/T209 width rules, and RETURN stays the documented key.
Built: `host-app/software/hostapp/host_core_ui.cc` (`hc_ui_input`: `CONFIG_LIST` on a folder row of `HCX_BROWSE` becomes `BROWSE_OPEN`).

## §3b Core app Library (Alex, 2026-11-14)
RIGHT on a folder row enters the folder, like RETURN. The actions of a folder are reached from inside: RIGHT on the `..` row opens the sheet of the folder the browser is in (Alex, translated: "there he can do it, with RIGHT going into the folder right away"). That sheet carries what the per-row folder sheet carried, applied to the current folder: Copy to, Move to, Copy/Cut to clipboard, Rename, Delete (same confirm flow as a file), Create folder here, Paste here (clipboard filled), Mark all/none, Set as home folder; "Open" is gone (it is already open). After Rename or Delete of the current folder the Library lists the nearest folder above that exists (`FileMgr::up_while_gone`). At the top of a medium and at "/" there is no folder of ours to rename or delete: the sheet holds only the folder-here items. The key line of a folder row says "RIGHT OPEN", of `..` "RIGHT FOLDER ACTIONS"; the help text (`diag.cc`) says the same.
Built: `coreapp/src/menuctl.cc` (`activate`, job end), `coreapp/src/filemgr.cc/.h` (`up_while_gone`, called after `do_rename` and a job).

## §3c Decision of 2026-11-14 (Alex, translated)
"In the core app RIGHT can go in, and in the host too. Then we keep two browsers for now. In the host there is no RETURN = actions, we have the keys for that, that is much smarter and faster." So: RIGHT enters in both browsers (§3, §3b stand); the host keeps no action sheet on RETURN; the shared-browser plan (one FileMgr for host and core app, T202 D2) is parked by Alex, two browsers stay.

Recorded direction, no build now (future work): "No action sheet on RETURN, not for the host and I do not like it in the core app either ... that is why I wanted navigation + keys + UI moved completely into .tomls." The long-term shape is navigation, keys and UI described in the `.toml`s (T202 line), not an action sheet per row. The `..` sheet of §3b stays as built until that work is specified.

## §4 Open
Nothing for T219. `tests/coreapp/golden/keywalk.txt` regenerated 2026-11-14 on the builder (`UPDATE_GOLDEN=1`, keywalk only, coordinator exception): `ids.txt` unchanged; every first divergence is RIGHT on a folder or medium row of the F3 browser (and the starts that contain RIGHT), the rest is the same FakeFs carrying the changed state into later probes.
Tests moved to the new path (2026-11-14, not run locally, the pipeline runs them): `tests/coreapp/test_home.cc` and `tests/coreapp/test_f3.cc` (New folder, Mark none/all, Paste here, Copy folder to clipboard) no longer press RIGHT on a folder row for its sheet; they go to `..` and press RIGHT, the sheet acts on the folder the Library is in. The folder-copy case enters `fresh` with RIGHT, copies it from its `..` sheet, goes up with RETURN on `..` and pastes from the `..` sheet. `tests/hostapp/test_hostcore.cc` comment corrected (`hc_resolve` still says CONFIG_LIST, `hc_ui_input` turns it into BROWSE_OPEN on a folder row).
