# T211 - Own key names in the host UI (F keys are the navigation)

Status: BUILT on branch `t211-key-names` 2026-11-15 (S1, S2, S3 first half); S3 "what Page Up/Down do in lists" OPEN with Alex. Proposed 2026-11-12, Alex.

## §1 The problem
- P1: the host app takes Gideon's keymapper codes: F1/F3/F5/F7 arrive as KEY_PAGEUP/KEY_HELP/KEY_TASKS/KEY_PAGEDOWN and
  `map_key` (host-app/software/hostapp/hostui.cc, removed by this task) folds them back into KEY_F1..F7. The real page keys therefore
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
- Open with Alex: paging keys in the host editor (none; editor +/- moves an entry). The boot menu pages with +/- since T209 D7 (`host_core.cc:47`); real Page Up/Down still arrive as F1/F7.

## §5 Build notes

2026-11-15, branch `t211-key-names` (not merged, not pushed):
- Built: `map_key` is gone from `hostui.cc` (both getch paths). The matrix keyboard and the USB keyboard hand F1/F3/F5/F7 over as KEY_F1/F3/F5/F7 (host-app io has no
  producer of KEY_HELP / KEY_TASKS; the old fold only ever changed a real Page Up / Page Down into F1 / F7). A real Page Up / Page Down now arrives as
  KEY_PAGEUP / KEY_PAGEDOWN; the key map (host_core.cc) has no entry for them, so they do nothing, in every list. Remote `pgup`/`pgdn` (hostdomain.cc) were never folded.
- Every place that shows a key name (grep, host app): legends come from the key map's first entry per intent (F1/F3/F5/F7/F8, `+`/`-` for pages, UP/DOWN, RETURN, letters),
  `hostdecl.cc` "Page down/up" are intent names for `+`/`-`. No screen shows a Gideon name; no foot hint or help page changes (PNG proof below: panel pixel-identical).
- Core app: has its own `coreapp/src/run.cc` / `monitor.cc` key handling and does not read `hostui.cc`; NOT touched. It still folds PAGEUP/HELP/TASKS/PAGEDOWN into F keys
  (`run.cc:79-82`) and the monitor maps F keys back to Gideon's names for trxmon (`monitor.cc:302,387`): that is the same D1 question for the core app, not decided here.
- S1: gate `tools/check_host_keys.py` (selftest plants fold, KEY_HELP, KEY_TASKS, and a comment that must not count) in `ci/lint.sh`; tree is clean.
- S2: `tools/dev.sh emu` on the builder, all three host flows PASS with the new image (F3 browser, F1/F5 menu and editor, F8 settings, V, core start and back). Not run: tests/hostapp key walks (agents run no app tests; pipeline).
- S3 first half: emu flow (`tests/hostapp/emu_host.sh`, `usbkey pageup` / `usbkey pagedown` with `--usb-keyboard`) on the home screen: panel pixels equal to before the key
  (`tests/hostapp/png_panel_same.py`, control: the F8 list differs), so neither key acts as F1 / F7. Pictures: host-home.png, host-pageup.png, host-pagedown.png.
- S3 second half OPEN (Alex): what Page Up / Page Down do in the lists (browser, boot menu, pick). Today after this change: nothing.
