# T219 — RIGHT goes in, LEFT goes out, in every file browser (GitHub #90)

**Status:** PROPOSED (2026-11-14); host F3 built, core app Library waits for one decision (§4).
**Origin:** GitHub #90 (a user): in the generic core application's file browser ENTER opens the selected folder and LEFT goes to the parent, RIGHT does not open the folder - inconsistent and unlike the Ultimate firmware. Alex, 2026-11-14 (translated): "he is right; didn't we already say with the configs and zips: LEFT out (one folder up), RIGHT in (into the folder or zip or config)".
**Builds on:** T198 D2 (RIGHT opens the config overlay on a core row / `.cor`, symmetric to LEFT = back), T197 (a `.zip` is unpacked with Z, never browsed), `docs/lvgl_ui_project/NAVIGATION.md`.

## §1 The problem
- Host F3 (`host_core.cc`, `HCX_BROWSE`): RIGHT is `CONFIG_LIST` on every row; on a folder it did nothing.
- Core app Library (`menuctl.cc` `MI_RIGHT` -> `activate(true)`): RIGHT on a folder opens the folder's action sheet (T104.2, T180 #42), not the folder.
- Core app pick browser (`pick_key`): RIGHT already enters a folder (unchanged).

## §2 Scenarios
| # | Scenario | Proof |
|---|---|---|
| a | Host F3: RIGHT on a folder enters it, same as RETURN | ue2emu picture `docs/pictures/gh90/3-right-into-games.png` (RIGHT on `cores`) |
| b | Host F3: RIGHT on a medium row at "/" enters the medium | `1`/`2` pictures |
| c | Host F3: LEFT goes up again | `6-left-back-to-games.png` |
| d | Host F3: RIGHT on `..` goes up (as RETURN) | `10-right-on-dotdot-goes-up.png` |
| e | Host F3: RIGHT on a `.cor` still opens the config overlay (no configs: nothing, T198 D9) | code path unchanged (`cfgov_open`); `5-right-into-sub.png` (cor selected, RIGHT does not leave the folder) |
| f | RIGHT on a `.zip` = what RETURN does on a `.zip`: nothing (zips are not browsable, only Z unpacks) | `8-right-on-zip.png` |
| g | RIGHT on a plain file: unchanged | code: only the dir branch changed |
| h | Core app Library: RIGHT on a folder enters it | open, §4 |

## §3 Decision
RIGHT on a folder row of a file browser does what RETURN does there (enter; `..` goes up); a `.cor`/core row keeps its config overlay; a `.zip` is not browsable, so RIGHT does what RETURN does (nothing); LEFT unchanged. Footers keep "RETURN open"; adding RIGHT would not fit the T187/T209 width rules, and RETURN stays the documented key.
Built: `host-app/software/hostapp/host_core_ui.cc` (`hc_ui_input`: `CONFIG_LIST` on a folder row of `HCX_BROWSE` becomes `BROWSE_OPEN`).

## §4 Open, with Alex
Core app Library: RIGHT on a folder is the only way to its action sheet (Open, Copy/Move to, clipboard, Rename, Delete, Set as home, Mark all/none). If RIGHT enters, rename/delete/copy of a subfolder need another key. Not built until decided.
