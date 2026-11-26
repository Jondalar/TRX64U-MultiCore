---
name: ux-review
description: UX review of TRX64 menus (host app and generic core app, LVGL on the 960x540 canvas, shown on HDMI 480p-1080p and on trx64u). Use for EVERY change that alters a rendered screen - new rows from an extend function (home_extend, hk_extend, ...), a new page or tab, a toast or note, wording, scrolling, F5/F8 placement - before merge and before any board test. Gives PASS/FAIL with findings; the ux-specialist agent runs it.
---

# UX review: how we judge a TRX64 screen

Alex (2026-10-28): "Do not underestimate the UX stuff! I want the UI to look professional." (translated)
Alex (2026-11-06, after four features reached the board unreviewed, translated): "if not, we abort everything with UI here and
you have to rework it." This review is a gate, not a courtesy.

## 1. When (no exceptions)

- Any commit that changes what a screen shows: rows added by an extend function count, even if "only appended".
- Before a merge to `integration` of such a change, and before any board test that includes it.
- The coordinator puts this step into every agent prompt that touches a screen. The coding agent does not review itself:
  a separate `ux-specialist` agent does, and the coordinator opens the key pictures itself before Alex sees them.

## 2. Read first (every time, fully)

- The UX concept: `docs/lvgl_ui_project/` of the TRX64-Ultimate repository: `README.md`, `NAVIGATION.md`, `UI-COVERAGE.md`,
  `UI-SCREEN-TOUR.md`, `components/`, `screens/` (read-only repo). Visual language: near-black/graphite, violet/cyan/amber/coral
  only for focus and state; one dominant task per screen; flat lists; 11-21 px type scale; secondary actions behind F7.
- `docs/gideon-menu-tree.md` of this repo (information architecture, readability at 960x540 from TV distance).
- The feature's spec and its decisions (a deviation from the concept is a proposal to Alex, never silently built).

## 3. Get the pictures (the pictures are the evidence) - from the EMULATOR

Alex (2026-11-11, translated): "use the emulator! no render benches." The pictures come from the real RISC-V application image
running in ue2emu, never from a render bench. Reason (11-11): two bench captures misled reviews - the shell missing because the
bench did not invalidate the screen, and the previous core's F5 label in the sidebar because one process rendered many cores.

- Build the image: `tools/dev.sh riscv --branch <b>` (core app `.ci/dev/<b>/coreapp.app`, host app via `tools/dev.sh emu`).
- Core app: a `.feature` under `tests/ui/features*/` (first line `# core: <profile>`), steps `I press ...` and
  `I save the picture "<name>"` -> `tests/ui/out/<name>.png`; run `COREAPP=<image> tests/ui/run.sh <feature>`. A core without a
  profile `cores/<core>/app/ue2emu.toml` gets one first. Host app: `tests/hostapp/emu_host.sh` (via `tools/dev.sh emu`).
- Walk to the screen with the keys a person presses (no test-only setup). Take at least: the screen with the change, the longest
  list of every core that gets the change (A1200, C64/trx64u, C128, VC-20, Atari), the last row selected, a toast over a list, the
  longest real file/entry name (31+ characters), 480p and 1080p where the emulator offers the mode (say so when it does not).
- The emulator lacks something or shows it wrong (a mode, a key, a profile, a device, a wrong picture): no workaround and no
  falling back to a render bench. Report it to the coordinator with the evidence (what was pressed, expected, seen, picture);
  the coordinator writes to the peer session `C64U_emu`, which owns ue2emu (Alex 2026-11-11). Subagents do not message it themselves.
- Render benches (`tests/*/render/`) are regression asserts only (R1/R2 geometry); their PNGs are not review evidence.
- Open every picture you judge (Read tool). Copy them to the session scratchpad so the coordinator can open them too.

## 4. The rules (each finding names its rule)

R1 **Selection always visible.** Moving the selection scrolls the list; the selected row is never cut or outside the view.
R2 **No overlap.** Toasts, notes and hints never cover rows, values or the key bar; text never runs into another text.
R3 **Lists stay short and grouped.** A settings tab is grouped in sections (e.g. Machine / Picture & Sound / Input & Hotkeys /
   Media & Folders) when it would exceed one screen; appending generic rows to every core's flat list is a FAIL.
R4 **Placement (T212, Alex 2026-11-12).** Core app: F1 Home = the session and its quick actions (drive rows act), F3 = Library,
   F5 = machine (core) settings only, F7 = menu-app settings (each core its own value, the host's value as default where the host has the
   same setting), F8 System = system actions and status (WiFi, build, ROMs, libraries). Host: F8 = host settings. The same item has the
   same name and place in every core.
R5 **One line per row.** Alex (2026-11-11, translated): "these sublines ... I want to question their sense strongly from a UX view".
   Default: name only; state as a short badge on the right; details of the selected row in one place (foot or info pane). A subline
   is a finding unless it is the only way to tell otherwise equal rows apart, and that reason is written in the spec. Values
   right-aligned; a long value or name is cut with an ellipsis (middle for file names) and never pushes the label below a minimum
   width; scroll position shown ("1-18 of 24") and counts entries, not section headings.
R6 **Wording.** One name per action, the concept's tone, no internal terms (register names, D-numbers), English UI text; an error
   says what is wrong and what to do ("WiFi is switched off - switch it on above", not "no module" when there is one).
R7 **Scale and placement on every target.** The menu fills the screen on the host and on trx64u at every HDMI mode (the canvas is
   960x540; trx64u reads the old PX_CTRL scale bits, MultiCore reads PX_OSIZE - see the 2026-11-06 finding).
R8 **Response.** A key must answer within one frame of rendering budget on the board; a screen that redraws the whole canvas per
   key is a finding (the core app renders in software into DDR2).
R9 **Parity.** A port keeps the original's structure 1:1 unless Alex decided otherwise.
R10 **Structure first - separation of concerns (Alex 2026-11-12: the reviewer must find these himself, not wait for Alex's impulses).**
   Before judging pixels, judge the structure of every area the change touches and report a structural finding even when the change
   did not cause it:
   - one purpose per area/context; settings, actions and information do not mix (F5 machine vs F7 app settings vs F1 actions);
   - no information-only screen the cursor walks without action ("waste of interaction"); a focusable row does something;
   - no redundancy between areas (F7 eject list vs F1 drive rows): the action sits where its object is shown;
   - each context owns its key bindings and its foot names exactly those (boot menu +/- page, editor +/- move, browser +/- page);
   - a style change in one context does not leak into another (the slim file list must not change the boot menu, T209 D6);
   - states are grouped by what the person can do (green usable / yellow check with R / red not usable), shown as a pictogram,
     the exact word in the status line for the selected row - not one wide word box per internal state (T209 D10);
   - every finding comes with a proposal; questions that only Alex can answer are listed one per line, never decided by the reviewer.
R11 **Taken from Anthropic's frontend-design skill (Alex 2026-11-12: "yes", the useful part only; our design system stays the source for
   colours and fonts):**
   - critique the emulator picture and remove one element before calling a screen done;
   - structural devices (frames, numbers, dividers, labels) must carry information; numbers only for a real sequence or a key;
   - all-caps labels are a warning sign: each one needs a reason (section headings, rail groups);
   - one action keeps one name from the row to its confirmation; errors say what happened and what to do; empty states invite the
     action ("Not set" on an empty drive should say how to fill it);
   - focus always visible, contrast readable on every row state (R1).



## 5. Report

PASS or FAIL. Findings ranked by severity: screen, rule (R1-R9), picture path, the code that draws it (file:line), the fix proposed.
Then the questions that are Alex's, one line each (structure changes always are). Name what you did not render or could not check.
A render test that asserts R1 and R2 (selection inside the view, no overlapping rectangles) is part of the fix, so it cannot regress.

## 6. Known findings to check against (2026-11-06, board, main-a9ef1237)

- A1200 F5 Settings: 20+ flat rows, selection scrolled out of view, the "Picture" toast over the last rows, Home Directory out of
  sight (T180 D10, T181, T184 D3, T185 rows appended without review).
- trx64u: the menu in the top-left quarter of 1080p (app writes PX_OSIZE only since 7acf1f9ca; the monolith reads PX_CTRL b5:4),
  and slow (unmeasured).
- Host F8 WiFi: no On/Off row, misleading "There is no WiFi module to talk to" when the radio is off; top bar "no network" with WiFi up.

## 7. Learnings 2026-11-11 (board photos, T198/T205/T207)

- Boot overlay (T198): a meta subline under each config row showed garbage; the title cut "Amiga 1200 (AGA, 68020)" to
  "Amiga 12". Check every fixed-size buffer and `%.Ns` on the path of a name; never blame the person's data without reading it.
- Settings in three places (T187 F5/F6/F8): Alex "3 times settings ... makes no sense". One place per core: F5 (T205).
- Hotkeys by LEFT/RIGHT through the whole keyboard: "nonsense". Keys are learned by pressing them (T207).
- Home media values without a width limit squeeze the slot label at 480p (T205 review).
- A sidebar/area label that belongs to another core is a FAIL even if it "looks like a bench artefact": prove where it comes from.
- ue2emu S45 (C64U_emu, 11-11) follows the host's hdmi_modes: `--handover video=ntsc60:480p` / `pal50:720p`, `--screen
  576p50|480p60|720p50|720p60|1080p50|1080p60|...`; the saved PNG has the frame size; `monitor trx` works with `--c64 none`.
  `tests/ui` takes the mode from `UI_VIDEO` (default 720p). Show 480p, 720p and 1080p for every reviewed screen.
- Every core lane has a profile `cores/<core>/app/ue2emu.toml` (T239.5 D13: step 12 of the matrix reads it too), features in `tests/ui/features_cores/`.
- ue2emu: trx64u runs plain TRX64-Ultimate mode (fixed 1080p frame, canvas 1x); scaled 480p/720p pictures come from core profiles only
  (reported to C64U_emu 11-12).
- A list-style change (T209) must not change the boot menu; the boot menu keeps its logo rows, as big as they need to be: 32 px logo, 38 px row, never shrunk to fit a mode - at 480p the row count gives (Alex 11-12); the boot menu shows no path; the editor row is the old menu row without the logo (D8).
- T209 learnings: check the long-name case at the narrowest mode (480p) in every row family (Home title, slot rows, Status panel, Library
  rows, SELECTED panel); a value cut to bare "..." or a title wrapping into the line below is a FAIL; a focused dimmed row must stay readable.
- T210 (11-12): a toggle that shows no change is first a question to the emulator, not to the app: the ESP stub answered DISABLE/ENABLE with no events (fixed in ue2emu, C64U_emu). A control on another core (trx64u) tells which side owns it. The UI state snapshot of a `fixed` item (WiFi) carries no value/sub text: the row value is read off the picture, not asserted.
- A feature file is one emulator session: scenarios inherit the state of the one before (the radio stays off for the next scenario).
- Every new learning goes into this file in the same change (Alex 11-11: "keep adapting the skill, put the learnings in").

## 8. Learnings 2026-11-12 (T209, one-line rows)

- ue2emu `wait` is emulated time, not wall time; bundle checks queue keys, so wait ~120 s of emulated time after a series of adds.
- The full-canvas row count was never computed at board start (only after a canvas change): call `layout_counts` before `build_ui`.
- A compact foot breaks between keys, never inside one ("any key stops the countdown" stays whole on line 2).
- Review findings: a list that scrolls on its own (the config overlay's `window()`) bypasses `listwin_fix`: grep every list for its own
  window code before claiming S6; config files named with 55 and 74 characters were not listed at all, one of 30 was (limit not located): the overlay's long-name case is unproven.
- A picture flow that cannot reach its screen still produces valid PNGs (an empty "No cores yet" menu): open one picture before sending
  the set to review; the config flow needs three DOWN in the folder (.., the core's directory, the .cor) before A.
- Review the pillarbox of 720p (canvas 1:1 centred in 1280x720) as the output frame mode, not as a host layout finding.
- Only the editor (F5) and the browser (F3) rows are one line since T209; the boot menu (F1) keeps its logo rows (T209 D6) and the benches search the frame for the logo again.

