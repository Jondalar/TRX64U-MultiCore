# T211 - Own key names in the host UI (F keys are the navigation)

Status: PROPOSED 2026-11-12, Alex.

## §1 The problem
- P1: the host app takes Gideon's keymapper codes: F1/F3/F5/F7 arrive as KEY_PAGEUP/KEY_HELP/KEY_TASKS/KEY_PAGEDOWN and
  `map_key` (host-app/software/hostapp/hostui.cc ~211-219) folds them back into KEY_F1..F7. The real page keys therefore
  cannot page, and the code reads as if F keys were page keys.
- Alex 2026-11-12: "F keys are our main navigation tool. Gideon's class may name them so, that would be worth a refactoring.
  What his code does does not matter to me. We have our own UI." (translated)

## §2 Scenarios
- S1: every key the host UI handles has our own name (F1-F8, cursor, RETURN, +/-, ...) at one place where Gideon's codes
  enter; no handler compares against KEY_PAGEUP/KEY_HELP/KEY_TASKS/KEY_PAGEDOWN meaning F keys. Proof: grep gate in lint.
- S2: the F keys behave exactly as before on C64 matrix keyboard and USB keyboard (tests/hostapp key walks, ue2emu).
- S3: real Page Up / Page Down from a USB keyboard reach the UI as page keys (no F1/F7). What they do in lists: open, to decide.

## §3 Decisions
- D1 (Alex 11-12): F keys are the main navigation; our UI uses its own key names, Gideon's naming is translated at the edge.

## §4 Out
- Open with Alex: paging keys in the host boot menu and editor (none exist; editor +/- moves an entry).

## §5 Build notes
