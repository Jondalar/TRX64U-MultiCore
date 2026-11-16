# T211 - Own key names in the host UI (F keys are the navigation)

Status: BUILT on branch `t211-key-names` 2026-11-15 (S1, S2, S3 first half); S3 decided (D2) and built. Proposed 2026-11-12, Alex.

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
- S3: real Page Up / Page Down from a USB keyboard reach the UI as page keys (no F1/F7). What they do in lists: D2.

## §3 Decisions
- D1 (Alex 11-12): F keys are the main navigation; our UI uses its own key names, Gideon's naming is translated at the edge.
- D2 (Alex 2026-11-15, S3 = "A", after the mock-ups h4a/h4b): real Page Up / Page Down page every host list like `+` / `-` (browser, pick lists, boot menu: wherever `+`/`-` page). The F5 editor keeps no paging (T209 D7a: `+`/`-` move the entry there), so the page keys do nothing in the editor. The foot legend stays `+/- PAGE`.
- D3 (coordinator, from Alex's 2026-11-12 statement): the core app follows D1/D2 - `map_key` in `coreapp/src/run.cc` is removed; real Page Up / Page Down page lists like `+`/`-` where those page.

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
- D2 built: `host_core.cc` key map, KEY_PAGEDOWN / KEY_PAGEUP beside `+` / `-` in HCX_MENU, HCX_BROWSE, HCX_BROWSE_PICK, HCX_BROWSE_INSPECT (same intents; none in HCX_EDIT). Each is
  placed after its `+`/`-` entry, so the legends (first entry of an intent) are unchanged: the foot still reads `+/- PAGE` (browse-pagedown.png), no 480p/1080p picture needed.
- S3 proof: emu flow `T211 page keys` (80 files in a stick folder): Page Down changes the panel, Page Up brings the top back (equal), a second Page Down moves again, F1 still navigates
  (PASS on the builder with all other host flows). Pictures browse-top / -pagedown / -pageup / -pagedown2 / -f1. Boot menu and pick lists share the intents through the same table; not walked separately.
  The home-screen step of the earlier commit (Page keys change nothing there) is replaced by this flow; host-pageup/host-pagedown.png are from that earlier run.

D3 build state (2026-11-16, VERIFIED below; first written 2026-11-15 unverified):
- Built: `map_key` removed from `coreapp/src/run.cc` (`getch_nowait` returns the key as read). `menukeytab.cc` already had KEY_PAGEUP / KEY_PAGEDOWN rows in every stage (viewer, app grid,
  timeline, main menu `MI_PAGE`, appdlg `DI_PAGE_*`, picker); with the fold they never fired, now they do. F1/F3/F5/F7 are unchanged (both keyboards deliver KEY_F1...).
  Real Page Up / Page Down in the main menu are `MI_PAGE` (ten rows, or the rows a browser list shows), in `KS_PAGING` the `+`/`-` rows stay the browser's own.
- Monitor (`monitor.cc`): behaviour and look unchanged. It already receives F1/F3/F5/F7 as function keys and translates them to trxmon's PAGEUP/HELP/TASKS/PAGEDOWN itself (`mon_key`); a real
  Page Up / Page Down used to arrive as F1/F7 and became PAGEUP/PAGEDOWN there, now they arrive as PAGEUP/PAGEDOWN directly: same result. Only the stale comment above that switch was reworded.
- Test added, NOT RUN: `tests/hostapp/emu_host.sh` flow "T211 core page keys" (host starts vc20.cor, CTRL+RESTORE, F1/F3 navigate the core menu, F3 browser into a 80-file folder, usbkey pagedown/pageup
  page it, F1 navigates). Navigation keys inside the core browser (down, down, return, down, return) are a guess from the host flow and may need adjusting after the first run.
- NOT verified: the RISC-V build of the core app after this change, and the ue2emu run above (the builder's app slots were blocked by Vivado for hours). A fresh agent: `tools/dev.sh riscv --branch t211-key-names`, then `tools/dev.sh emu --branch t211-key-names`, look at core-*.png, fix the nav script if needed.
- D3 verified (2026-11-16): `tools/dev.sh riscv --branch t211-key-names` rc=0 (hostapp + coreapp build). `tools/dev.sh emu` rc=0: all host flows PASS incl. "T211 page keys" and
  "T211 core page keys". Fixes: (1) the core flow's guessed navigation was one `down` short (the stick's /USB0 lists cores/ before many/, so it landed in /USB0/cores); now two downs, picture
  core-browse-top.png shows /USB0/many "1-19 of 81", core-browse-pagedown.png the cursor moved ten rows to file10.txt (Page Down pages like +/-). (2) `tools/check_host_keys.py` flagged the four
  `K(ctx, KEY_PAGEDOWN, *_PAGE_DOWN)` key-table rows of D2 in host_core.cc (the gate was written before D2); rows of exactly that form are now ignored, the planted fold/help/tasks faults are
  still found (selftest ok, tree check ok). Pictures: scratchpad t211/core-*.png. Full `ci/lint.sh` not run, only the host-keys check.
