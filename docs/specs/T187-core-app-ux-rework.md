# T187 — Every core's menu is laid out the same way, keeps the selection visible and never covers its rows

**Status:** PROPOSED (2026-11-06), D1-D8 decided; questions open (§6).
**Repo:** TRX64U-MultiCore — core app (`coreapp/src/menutree.cc`, `ui.cc`, `menuctl.cc`, the extend functions), its render bench;
the host app's WiFi page (T184 D3 amended) is reviewed with the same rules.
**Number:** T187.
**Origin:** board test 2026-11-06 (package main-a9ef1237): the UI work since 2026-11-01 (T180 D10, T181, T184 D3, T185) went to the board
without a UX review; Alex stopped the UI tests (translated): "if not, we abort everything with UI here and you have to rework it". UX review
of the same day: `docs/todo/T187-ux-review-2026-11-06.md` (findings, layout proposal, tests); the gate is now `.claude/skills/ux-review`.
**Depends on:** the UX concept (`TRX64-Ultimate/docs/lvgl_ui_project`), `docs/gideon-menu-tree.md`.
**Milestone:** M3.

---

## §1 The problem

- **P1 Selection leaves the view:** the list window counts every row as one line (`ui.cc:259-260`, `:337`, `:525`, `trx_row_h(false)`);
  two-line rows ("at the next reset") make the A1200's F5 list 448 px for 378 px of room; the selected row is drawn below the pane.
- **P2 Overlap:** the toast (`ui.cc:873-900`) is drawn over list rows, and the same text is printed again in the status line (`menuctl.cc:1671`).
- **P3 Structure differs per core:** only the C64 (`coreapp/cores/trx64u/menu.toml`) has the Settings area with Video/Audio/Input;
  every other core appends Picture, Volume and 7 hotkey rows flat to its F5 list (`menutree.cc:164-171`).
- **P4 F8:** actions, state and settings mixed without headings; the first focus is the destructive "Reset to host defaults"; the WiFi row
  says "Not set" where a module exists.
- **P4b Values one row off (board, Apple II F8, 2026-11-06):** the value column is drawn one row below its label (WiFi's "Not set" beside
  Persist medium mounts, the home path beside Return to host menu).
- **P5 Home folder:** a typed text field (T180 D10 amended: H in F3 or a folder selector).
- **P6 trx64u:** the menu in a quarter of a 1080p screen (the app writes `PX_OSIZE` only since 7acf1f9ca; the monolith reads `PX_CTRL`
  b5:4) and slow (unmeasured).

## §2 Scenarios

| # | Solves | Scenario | Test |
|---|---|---|---|
| S1 | P1 | for every core and every index 0..n-1 of every list, the selected row lies fully inside the pane; sum of drawn row heights <= room | render bench, all cores with the full extend chain; control UI_LIST_ONE_LINE_ROWS must fail |
| S2 | P2 | with a toast up, rows, status, toast and position label do not overlap on F1-F8; one channel per message | render bench, pairwise rectangles |
| S3 | P3 | every generic row key lands in the same area and group in every core as in the C64 | render/menu test over core_table |
| S4 | P4 | F8's first focus is not destructive; the WiFi row shows On/Off and the line, never "Not set" with a module | render bench |
| S5 | P5 | H in F3 sets the home; RETURN on the row opens a folder selector | coreapp test + render |
| S6 | P6 | trx64u: the layer covers 1920x1080 at 1080p | render/register model of the monolith's pix_layer |
| S7 | all | the UX review (`ux-review` skill) gives PASS on the rendered pictures | ux-specialist agent |

## §3 Decisions

- **D1 (Alex, 2026-11-06: "Ist ok"):** every core gets the Settings area the C64 has (Video, Audio, Input in their groups, as
  `trx64u/menu.toml`); F5 keeps only the machine's own settings. One group-to-area table in the code serves every core, with or without
  a description record.
- **D2 (Alex, 2026-11-06: "A, one row with a submenu is better"):** the hotkeys are one row "Hotkeys >" under Input that opens a
  sub-page with the modifier and the six action keys; not seven rows in the list.
- **D3 (Alex, 2026-11-06: "A"):** a setting that takes effect at the next reset is one row with a small marker at its end and one legend
  line for the list; the second line "at the next reset" goes.
- **D4 (Alex, 2026-11-06: "at the top next to the core name there is enough room for Vol 100% or so"):** a short state message of the
  open menu (volume, picture mode, ...) is a chip in the top bar next to the core name chip; no toast over the list, no second copy in the
  status line. Nothing is drawn over the running core at all (T181 D4).
- **D5 (Alex, 2026-11-06, on GitHub #50: "good idea, go for it; UX check by the agent, then build"):** the Picture item offers the modes
  the screen names (EDID) as a list to pick from; nothing is applied while browsing it; picking one applies it with the 8-second confirm.
  The same pattern for the host's F8 Picture row. The ux-specialist designs it against the concept before it is built.
- **D6 (Alex, 2026-11-07: "B", translated; options: max 100 % or up to 200 %):** the core menu's volume goes up to 200 %; values above
  100 % amplify (quiet cores such as SID or Paula get louder, loud passages may clip).
- **D7 (Alex, 2026-11-07: "A", translated):** Home directory, Enter home on startup and Persist medium mounts move from F8 to F5 Settings,
  group Storage (amends T180 D2/D10 placement); F8 keeps system actions, WiFi, Bug report and Reset to host defaults as the last row.
- **D8 (Alex, 2026-11-07: "A", translated):** F8 is named "System" (not "Expert"), grouped: System actions (Restart core, Return to host
  menu), Network (WiFi On/Off and line), Support (Bug report), and Reset to host defaults as the last row; the first focus is not destructive.
- **Finding (board photo, A1200 F1 Home, 2026-11-06):** the top bar shows 192.0.2.18 while Status > Network shows 192.0.2.189;
  one of the two reads the wrong interface or a stale address.
- **D9 (Alex, 2026-11-08, board photo A1200 F5 Settings, translated: "all these sub-lines (at the next reset) could go beside it
  (requires reset)" - "or a pictogram behind it"):** a setting that takes effect at the next reset keeps one line: the label, then a
  reset pictogram right after it (LVGL `LV_SYMBOL_REFRESH`) instead of the second line "at the next reset"; the meaning of the pictogram
  is said once (footer legend or the row's help). The list of the A1200 (23 rows, 1-18 visible) then needs about half the height.

## §4 Out

Nothing decided out yet.

## §5 Build

Board 2026-11-07 (package main-e794f15d, A1200 F5, 23 rows): the selection leaves the pane (P1) and "Volume 120%" is a card over
the hotkey rows and a second time in the status line over the last row (P2). Build started 2026-11-07 on branch t187-ux.

### Built in t192-cut-core (2026-11-09, Alex via the coordinator, board photo A1200 F5)

- **D3 as a ring (the generic row renderer, every core; the C64 record files are untouched):** a setting that takes effect at the next reset is one line
  (no second line "at the next reset"); `MenuCtl::row_reset(i)` / `Item::reset` say it, `trx_row_at` and `trx_setting_row` draw a small ring beside the label
  (`trx_reset_mark`, `app/view/trxview.cc`), the in-place update (`trx_row_retext`) keeps the ring's position. The one legend line "A ring beside a name: the
  value is used from the next reset" stands in the status row of a list that has marked rows while the status row has nothing else to say
  (`MenuCtl::legend_line()`). A text or file setting keeps its second line (its value). Bench: `tests/coreapp/render/render.cc` (ring child, retext, legend).
- **The note over the last rows:** the volume/hotkey line (`ui_toast_render`) no longer is a 40 px card over the lower rows; it is the foot line of the
  screen (the key legend's strip, 28 px, full width) for the 2 seconds it stands. No row, button or panel is under it. Bench: `volume_toast_F1..F7` pictures,
  the changed-pixel check now looks at the foot strip. T187 D4 (the chip in the top bar) is not built here.
- ux-review 2026-11-09: PASS. Finding fixed: the volume text no longer shows twice (status row and foot line): `status_line` / `hint_text` leave out a status that equals the toast. Open, low: c128 says "next start" where the legend says "reset" (a ROM message of the C128 core).
- Pictures changed: the A1200/A500/Atari/C128/NES settings pages, the Picture-row mode pictures and the seven volume-toast pictures (28 of 99); the other 71 are identical.

### D5 design (ux-specialist proposal, 2026-11-06; not yet decided by Alex)

Read first: the UX concept (README, NAVIGATION, UI-COVERAGE; "settings change live when the user leaves a page", flat lists, one
dominant task per screen), `gideon-menu-tree.md`, GitHub #50, ADR-0062. Facts from the code: the Picture item is value 0 = Auto and
value n = the n-th entry of `video_modes_hdmi_list()` (`picture.cc` `fill_values`); the stored value is that index, so the **list order
stays the enum order** (576p/480p, 720p, 1080p, 800x600, 1024x768, 1280x1024), no sorting. The EDID parse already has `native`
(`edid.h` `EdidInfo`) and each row has its raster (`edid_row`: pixel_hz, htotal, vtotal), so refresh = pixel_hz / (htotal * vtotal) and
"native" need no new EDID code; `PictureEnv` gets a `native` mask beside `mask`.

**Where it opens: a sub-page, the same mechanism as D2's "Hotkeys >".** The Picture row (Video group, same place in every core, R4) is
an "opens" row: value text is the current mode, a ">" at its end. RETURN opens the page; the shell, top bar and tabs stay, the list
body is replaced. Not an inline picker: seven rows plus info do not fit under a row in a 378 px pane (R1), and one sub-page pattern for
Hotkeys, Picture and later items is cheaper to learn and to test than two. LEFT/RIGHT on the closed row do nothing (the row is no
longer a stepper; stepping applies a mode, which is exactly what D5 removes). The page title is "Picture" with the path chip
"Settings > Video".

**Page layout (960x540 canvas; shell as every F5/F6 page; pane ~378 px; rows one line, 40-46 px as the Settings list; no scrolling,
at most 7 rows):**

```
[ A1200 ]  F1 Home  F3 Library  F5 Core  F6 Settings  F8 System        [chip: Picture 1280 x 720]   <- D4 chip, only after a result
 Settings > Video > Picture
 Your screen offers 4 modes. Nothing changes until you pick one.                          <- info line, muted, 1 line
 +---------------------------------------------------------------------------------------+
 |  Auto                    1920 x 1080 50.0 Hz now (what Auto picks on this screen)     |   <- tag "now" only on the applied row
 |  576p / 480p             50.0 Hz                                                      |
 |> 1280 x 720              50.0 Hz                                              now     |   <- cursor starts on the applied mode
 |  1920 x 1080             50.0 Hz                                             native   |
 |  1024 x 768              60.0 Hz                                                      |
 +---------------------------------------------------------------------------------------+
  UP/DOWN browse    RETURN use this mode    LEFT back                                     <- key bar, never covered
```

Row = name (left, the existing `video_hdmi_name`), refresh (second column, one decimal, the real raster of the row the gateware will be
asked for via `video_modes_resolve`, so #50's "800 x 600, 50.0 Hz" and #48's closest-to-50 Hz choice are readable), tags right-aligned:
"now" (accent, exactly one row: the mode the picture is in, or on Auto both Auto and its resolved row), "native" (muted, EDID short
video descriptor native flag), nothing else. No "recommended" tag: Auto *is* the recommendation and its row names the mode it resolves
to (`video_modes_best`); a second word for the same thing would be noise. The Auto row text is "Auto" + the resolved mode, never just
"Auto". Rows that fell back to the other family (a 60 Hz row for a PAL core, `video_modes_hdmi_list` rule) show their real Hz and the
info line says "This screen names no 50 Hz modes; 60 Hz modes are listed." (R6, honest instead of silent).

**Keys.** UP/DOWN move the cursor (wrap off; no apply, no blank screen). RETURN applies the cursor's mode through the existing
`picture.apply` (so REST, web and menu keep one path) and starts the existing 8-second confirm. RETURN on the row that is already
current closes the page and applies nothing (no blank, no confirm). LEFT / ESC / back close the page and change nothing. The cursor
starts on the current mode. Number keys and F-keys keep their jobs.

**Confirm step.** Unchanged mechanism (`picture_service`, `ui_picture_question`, `PICTURE_CONFIRM_MS`), better text, because the person
may be looking at a black screen and must be told what comes back:
"Keep this picture?" / "1280 x 720, 50.0 Hz" / "In 8 s 1920 x 1080 comes back." / "RETURN keeps it    LEFT or ESC goes back now".
Keep = RETURN or any key but LEFT/ESC; LEFT/ESC reverts at once (the natural "I do not like this"). This is the only full-panel
message; it is a modal safety question that must be seen (T181 D4 concerns toasts over the running core, and the panel is the layer the
confirm already uses).

**After keep / revert / timeout.** The page comes back with the cursor where it was, the "now" tag on the kept mode, and a D4 chip in
the top bar ("Picture 1280 x 720"). After a revert or timeout the previous value is restored (existing behaviour), the page is back,
"now" is on the old mode, the cursor stays on the rejected mode so the person steps on to the next, and the chip says "Picture: back
to 1920 x 1080". The page never closes on its own; leaving is LEFT, which is also the compare-modes loop #50 asks for.

**Edge cases.** (1) One mode: the page opens anyway (it is how the person learns what the screen offers): info line "Your screen
offers 1 mode.", Auto row + the mode; RETURN on either closes without change. (2) No/corrupt EDID (`EDID_LIST_NONE`): the six TV rows
and "The screen did not name its modes; the standard TV modes are listed." (3) Hot-plug while the page is up (`PS_LIST_CHANGED`): the
list is rebuilt, the cursor keeps its mode by identity, if that mode is gone it goes to the current one, the info line count updates.
(4) A confirm waiting from REST/web when the page opens: the panel wins, the page opens after it. (5) Cores without the gateware
(`PictureEnv.gateware` false): same page, refresh column from the stored table, no Scale/Smoothing here (they stay rows of the Video
group, unchanged).

**Host F8 equivalent.** The F8 > Picture row (`hostui.cc` settings page, "Picture" in the F8 list) opens the same page with the same
rows, tags, keys and wording, from `hv_list()` plus the host's "from the screen" entry as the Auto row. The `V` key (key bars
"V picture") opens the page instead of stepping. The confirm box (`pq_root`, `hostui.cc`) gets the same text. The web page's select
keeps its list unchanged (it already is a selection, #50); it may add the refresh to the option text. One page implementation shared by
host app and core app (the `app/view` layer), not two.

**How it follows the rules.** R1: at most 7 rows, cursor always inside the pane, asserted for every index. R2: no toast; result in the
D4 chip; the info line, rows and key bar do not overlap. R3: Picture stays one row in Video; the seven-row list is behind it, not
appended. R4: same name and place in every core and in host F8; Picture is a Video item (`group = "video"`). R5: one-line rows, tags
right-aligned, the count is in the info line so no position label is needed under 8 rows. R6: plain English, no row numbers or enum
names, errors say what to do. R7: same shell and scale as the other pages on every HDMI mode and trx64u. R8: opening redraws the body
once; a cursor move changes two rows only. R9: host F8 and cores keep one structure; the web list is the original and stays.
Out of D5: choosing Auto by closeness to the core's frame rate (#50 point 2, #48) - the refresh column makes the manual choice
possible; Auto's rule is a separate decision.

**Render-test assertions the build adds** (tests only, `tests/coreapp/render`, `tests/hostapp/render`; fixtures: EDID with 6, 4, 1
modes, none, corrupt; cores A1200, C64/trx64u, C128, Atari; 480p and 1080p shell; first/middle/last cursor; picture of each):
1. rows = Auto + `video_modes_hdmi_list()` entries, in list order; exactly one "now" row (two when Auto is current); native tags equal
   the EDID native mask; the refresh text equals pixel_hz/(htotal*vtotal) of the resolved row.
2. R1: for every cursor index the cursor row lies fully inside the pane; sum of row heights <= room; info line and key bar present.
3. R2: pairwise rectangles of info line, rows, key bar, D4 chip do not intersect; no toast object while the page is open.
4. Nothing applied while browsing: after UP/DOWN across the whole list the stub's `video_apply`/`picture.apply` call count is 0 and
   `picture_confirm_pending()` is false; RETURN on a mode: exactly one apply and a pending confirm; RETURN on the current mode and
   LEFT/ESC: 0 applies, page closed.
5. Confirm text names the new mode and the mode that comes back, with the seconds; LEFT/ESC during the wait reverts at once; timeout
   (advance the stub clock 8 s) restores value and mode, page open, cursor on the rejected mode, chip text "back to".
6. Hot-plug: `PS_LIST_CHANGED` with a smaller mask keeps the cursor by identity / falls back to current; one-mode and no-EDID info
   lines; the 50 Hz fallback info line.
7. Closed row: Picture shows ">" and the current mode; LEFT/RIGHT change nothing; same group/place in every core (S3).
8. Host: F8 Picture row and `V` open the same page; same assertions 1-5 on the host bench.
9. Negative controls that must fail: `UI_PICKER_APPLY_ON_MOVE` (a move applies), `UI_LIST_ONE_LINE_ROWS` style overflow (9 rows in a
   too small pane), `UI_PICKER_NO_NOW_TAG`.

**Answers (Alex, 2026-11-07, all "A"):** Q-D5.1 LEFT/RIGHT on the closed Picture row do nothing; Q-D5.2 in the confirm LEFT/ESC revert
at once, any other key keeps; Q-D5.3 after keep the page stays open on the new mode; Q-D5.4 Auto's rule for cores without a VIC lock
(#48, #50 point 2) is a separate task with its own spec, not part of D5.

## §6 Open questions (one at a time; full list in the review document)

none open after D8 (D5's mode list design by the ux-specialist pending).
