# T187 — Every core's menu is laid out the same way, keeps the selection visible and never covers its rows

**Status:** PROPOSED (2026-11-06), D1-D8 decided; questions open (§6). Status check 2026-11-13: BUILT and merged into main 2026-11-11 (18b14d2b4): D1-D9 incl. host Picture page, ux-review PASS; open: S2, S5, S6. 'PROPOSED' and 'questions open' are outdated.
**Superseded in part by T205 (2026-11-11):** F6 is gone; the settings of a core are in F5 (core tab, Video, Audio, Input), F8 holds actions only. Where this text says F6, read F5.
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
- **D9 (Alex, 2026-11-11, on the picture page with "now" on both Auto and 1920 x 1080: "the now twice is nonsense. The currently chosen
  mode should have NOW or a * and already be selected on opening", translated):** one mark only, on the row that is chosen (Auto when
  Auto is set, its resolved mode stays in brackets; otherwise the fixed mode); the cursor starts on that row. Host F8 Picture and the core
  app's picture page alike.
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

### Part 1 built (2026-11-07, branch t187-ux): S1, D3, the shared menu chain

- **One chain:** `menu_build` (`coreapp/src/menubuild.cc`) holds picture, volume, persist, home, hk; `run.cc` and every render case that shows
  what the board shows (`board_desc()` in `tests/coreapp/render/render.cc`: areas, a1200, modes, rtc, ddt, c128 kit, main Atari, toast, job) call it.
  Tests of one extend in isolation stay (`test_picture`, `test_volume`, `test_home`, `test_hotkeys`, the hk pixel diff).
  The render bench uses the board's own `core_table` (`table.cc`, 18 cores), no short copy.
- **P1:** `trx_window_h` (`app/view/trxview.cc`) takes the rows' real heights; `list_window` (`ui.cc`) is the one call of build, signature and in-place
  pass. The position label sits on the title line and says "3-20 of 23 (2 above, 3 below)".
- **D3:** superseded by D9 (the ring and `legend_line()` above, from main); the `*` marker and the extra legend line of this first cut were dropped in the rebase of 2026-11-10.
- **S1 bench** (`list_rules`, `tests/coreapp/render/render.cc`): every core of the table x 480p/720p/1080p canvas x every flat list x every selection
  index (walk down and up with the keys; files get a path so some rows are two lines). Checks: selected row whole inside the rows' room, sum of drawn rows
  <= room, position label above the first row and naming what is hidden, no "next reset" second line, legend exactly when a row is marked.
- **Red first** (commit 10a9b3fdd, old window logic, builder): `S1 context: Amiga 1200 (AGA, 68020) 480p ... selected row 10 of 23 at y 358-378, the
  rows' room is 36-362 (drawn 15 rows)`; 226 such lines, `S1 list rules: 18 cores, 201 lists, 5349 selection states, 6036 rows with a second line,
  0 marked rows, 0 legends, 7489 failed`.
- **Green** (builder, `ci/trxapps.sh`, rc 0, 43m50s): `S1 list rules: 18 cores, 201 lists, 5349 selection states, 708 rows with a second line, 5328
  marked rows, 1626 legends, 0 failed`; `coreapp render: 87690 checks, 0 failed`. Control `UI_LIST_ONE_LINE_ROWS` (in `tests/coreapp/render/run.sh`):
  `coreapp render: 87690 checks, 193 failed`, `test control_UI_LIST_ONE_LINE_ROWS: PASS`. Sizes: coreapp.app 1,710,668 bytes (slot 1,966,068), hostapp.app 901,884.
- **ue2emu for a non-C64 core:** not possible today. The ID block of ue2emu is the constant "TRX2" (`trx.rs:43`), so the core app always takes the
  TRX64U descriptor; see the interim note to the coordinator (core id, hand-over page, shared-ID feature register).
- Not in part 1: S2 (toast), S3 (groups), S4, S5, S6, D1/D2/D4-D8.

### Part 2 built (2026-11-07/08, branch t187-ux): D1, D2, D7, D8, S1 headings, S3, S4, P4b (parts 2, 2b, 2c)

- **Routing table** (`menu_route_of`, `g_routes[]`, `coreapp/src/menutree.cc`): a generic row (Picture/Scale/Smoothing -> video, Volume -> audio, hotkey map ->
  hotkeys, Persist/Home -> storage) lands in the same area and tab in every core: F6 Settings (Video, Audio, Input), F5 Storage. A core without such a tab lists the
  rows in the area's tab "settings"; a heading ("Machine", "Storage") tells two blocks apart there. The descriptor group `system` of Persist/Home was renamed
  `storage` (D7: F8 keeps the system actions). C64: the firmware's record keeps Storage in its tab "Drive A" (F5) and Home directory / Enter home on startup in F8 "Expert".
- **D1 headings:** `EK_HEADING` (a quiet divider, never selected: `MenuCtl::heading/unheading/move/settle`; never the last line of a window: `list_window`).
- **D2:** the seven hotkey items are one row "Hotkeys >" in F6 Input (`EK_OPENS`, a hidden page tab of the area); RETURN or RIGHT opens the page (modifier + six keys),
  ESC returns to the same tab with the same selection and first row (rule 5, `first_` kept per tab).
- **D8:** F8 "System": System (Restart/Reload core, Return to host menu), Network (WiFi), Support (Bug report), core nodes, Reset (Reset to host defaults, last row);
  the first focus is Restart core.
- **Assertion changes of part 2b (old -> new):** Picture/Volume tests `F5 / tab 2` -> `F6 / tab 1`; Amiga mouse rows F5 tab 2 -> F6 tab 3 (Input); Persist/Home
  `F8 present` -> `F5 present, under the heading "Storage"` (`test_home`, `test_media_mounts*`, `test_desc`); `!F6/!F5 Persist` -> `!F6/!F8`; tab number check
  `tb.n == b + 1` -> `tb.page || tb.n == b + 1`; Reset test moves with KEY_UP (reset is last); desc check accepts EK_HEADING, EK_OPENS, EK_NODE; goldens `menu_*.txt` follow.
- **Part 2c bench** (`tests/coreapp/render/render.cc`): (1) S1 walk expects the rows minus the headings and asserts `!sel_heading`, `!last_heading` per state;
  (2) `ux_rules`: D2 per core with a hotkey map (exactly one "Hotkeys >" row, no hotkey item outside the page, page = 7 rows, ESC back to tab/selection/first row),
  S3 (every generic row of the routing table in the F-key and group the table names, checked against the C64's own F6 tab names; page entries "Input > Hotkeys"; the
  three storage rows in F5 Storage - the C64 record is not their yardstick), S4 (F8 first focus neither heading nor Reset, Reset last), P4b (the value's middle is the
  label's middle in every drawn row, S1 walk = window moves and in-place retext, plus a value change in every F5/F6 list); (3) the hotkeys pixel check compares the
  core's Input tab with its opened Hotkeys page. Controls added to `tests/coreapp/render/run.sh`: `UI_HOTKEYS_SEVEN_ROWS`, `TRX_FOCUS_ROW0`,
  `TRX_HEADING_SELECTABLE`, `TRX_VALUE_ROW_OFF`.
- **P4b (Apple II F8, 11-06: value one row below its label):** not reproduced in the bench at 480p/720p/1080p: no value deviates more than 2 px from its label's middle
  (see the report lines); the control `TRX_VALUE_ROW_OFF` turns it red. Open for the board: the photo may be an older build.
- **UX pictures** (`ux_shots`, 1080p and 480p, `ux-<core>-<state>-<res>.png`): A1200 F5 Settings top/end, F6 Video/Audio/Input, Hotkeys page, F8 System; C128 F5 end;
  Atari F5; Apple II F8; C64 F5 Drive A.
- **Report lines:** builder, commit 5bfa5017b, `ci/apptests.sh coreapp hostapp`: `tests/coreapp: 103 test programs passed, 1521 parallel controls (JOBS=14), every negative control failed as it must`
  (620 s), hostapp PASS; `ci/trxapps.sh` rc 0 (29m03s): `S1 list rules: 18 cores, 252 lists, 5670 selection states, 564 rows with a second line, 3840 marked rows, 1170 legends, 0 failed`;
  `UX rules: 18 cores, 15 with a hotkey map, 170 generic rows compared with the C64 (0 not in the C64), 51 storage rows in F5 (51 where the C64 record differs), 18 F8 focuses, 1317 value changes, 0 failed`;
  `coreapp render: 90694 checks, 0 failed`. Controls (all `fails as it must`): UI_HOTKEYS_SEVEN_ROWS 31 failed, TRX_FOCUS_ROW0 82, TRX_HEADING_SELECTABLE 1083, TRX_VALUE_ROW_OFF 5855.
  Sizes: coreapp.app 1,714,316 bytes (slot 1,966,068), hostapp.app 901,884. Before 2c the render was red: 96 x `render.cc:1126 v == n` (the walk counted headings).
- **Open (C64 record, firmware side):** Home directory and Enter home on startup stay in the C64's F8 "Expert", Persist medium mounts in "Drive A"; D7 is not applied to the
  firmware's record (change only with both sides, handover contract). A core whose F5 Settings holds only the storage rows shows them with no "Storage" heading (group = tab name).

### Part 3 (2026-11-10, branch t187-ux): D5 core part built; state of D1-D9

`tools/dev.sh test --branch t187-ux` rc 0 (518 s), `riscv` rc 0, `emu` rc 0 (host flow PASS, `tests/coreapp/emu` all scenarios, `tests/ui` 90 passed, 0 failed);
`check_menuctl_gate` ok, `check_no_v1_ui` 0 findings. Not run: `ci/render.sh` (render bench, goldens of the pictures) and the board.

| D | Built where | Evidence |
|---|---|---|
| D1 | `menu_route_of` / `g_routes[]` (`menutree.cc`): F6 Video, Audio, Input in every core | Part 2 report lines; `test_desc`, `menu_*.txt`, `tests/ui` A1200 F6 |
| D2 | `EK_OPENS` row "Hotkeys >", hidden page tab | Part 2; `test_hotkeys` (nes now sets `WIN / CMD + ESC`, the non-C64 names) |
| D3 | superseded by D9 | - |
| D4 | `MenuCtl::say` / `toast` chip in the top bar; D5 result goes there (`picture_take_result` in `run.cc`) | `test_picture` (status/chip not asserted) |
| D5 core | `picture_modes` (`picture.cc`), `begin_pick_modes` / `pick_key` (`menuctl.cc`), key bar, confirm text with both modes (`ui_picture_question`), LEFT/ESC revert (`run.cc`) | `test_picture` `through_the_menu`: RIGHT does nothing, RETURN opens "Picture", browsing applies nothing, RETURN applies via `settings.set` (same path as REST), "now" tag, LEFT closes |
| D6 | volume to 200 % (Part 2/main) | `test_volume` |
| D7 | Persist/Home dir/Enter home on F5 Storage; C64 record: Persist in "Drive A" (`menu.toml` group `drives`, `menu_rec.inc` regenerated, `menu_c64.txt` golden); "Menu opens at" stays on System (D30) | `test_media_mounts_engines`, `test_reopen`, `tests/ui` settings.feature |
| D8 | F8 "System" with headings, Reset last, first focus Restart core | `keywalk.txt` / `ids.txt` goldens regenerated (headings are rows now); `tests/hostapp/emu_host.sh` goes one down to "Return to host menu" |
| D9 | reset pictogram (main) | unchanged |

Fixes on the way: a heading entry was a "fixed" item of the UI service and answered "takes no focus" in the cover test (`MenuCtl::item` gives `IK_HEADING`);
`picture_modes` needs no video object (benches); `PictureEnv` initialisers got the `native` field; "Menu opens at" no longer shows in F5 Machine as well.
**Not built (D5):** the host's F8 Picture row and its `V` key as the same page (`hostui.cc`, shared `app/view` page), the shared confirm text in `pq_root`;
`PictureEnv::native` is read from the EDID but the render fixtures (6, 4, 1 modes, none, corrupt) and the §D5 render assertions are not written; the info line
for "60 Hz modes listed" differs from the spec wording; hot-plug rebuild is wired (`rebuild()`) but has no test; no ux-specialist run, no pictures of the page yet.

### Part 4 (2026-11-11, branch t187-ux): D5 host part (built; verified in Parts 5-6, merged into main 2026-11-11)

- `tools/dev.sh riscv --branch t187-ux` at 7b0bf4c7e: rc 0, host app and core app link (`hostapp.app`, `coreapp.bin` produced).
- The F8 Picture row, the `V` key and the confirm box (`pq_root`) use `coreapp/src/picmodes.*`; host render bench cases exist in `tests/hostapp/render/render.cc`
  (`page_pick`, `picnote_kept`, the 8-second question with both modes).
- Changed in this step: the fallback and no-EDID info lines now read exactly as the D5 spec ("This screen names no 50 Hz modes; 60 Hz modes are listed.");
  `test_picture` gained the D4 chip texts (`picmodes_kept_text` / `picmodes_back_text`) and the hot-plug rebuild of `PicPage` (cursor by name, fallback to the mode in force).
- 2026-11-11 results at 9fa756af5 + golden commit: `tools/dev.sh test --branch t187-ux` first run red (`test_keywalk` : VIC-20 F5/summary, the changed fallback info line);
  golden `tests/coreapp/golden/keywalk.txt` regenerated (38 lines, all VIC-20 F5 probes; no other golden changed), then `test` rc 0 (262 s), `emu` rc 0 (host flow PASS,
  `tests/coreapp/emu` all scenarios, `tests/ui` 90 passed, 0 failed).
- Not run: `ci/render.sh` (the host render bench `tests/hostapp/render` with the D5 cases is compiled and run only there, not in `apptests --tier fast`), the render fixtures
  6/4/1/none/corrupt EDID, ue2emu pictures of the picture page (core and host), the ux-specialist run, the board.

### D5 design (ux-specialist proposal, 2026-11-06; decided by Alex 2026-11-07: Q-D5.1-4 all "A", answers at the end of this section)

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

none open (the D5 design is decided: Alex, 2026-11-07, Q-D5.1-4 all "A").

### Part 5 (2026-11-11, branch t187-ux at 1f523e3b9): verification

- `tools/dev.sh integrate --branch t187-ux`: rc 0 (tests/coreapp/emu "all scenarios passed, every control failed as it must"; host emu flow PASS `host->vc20->menu->host`; tests/ui "100 passed, 0 failed, 0 errors").
- `tools/dev.sh emu --branch t187-ux`: rc 0; tests/ui "100 passed, 0 failed, 0 errors (226 s)".
- PNGs in `/tmp/t187emu/` (copied from `tests/ui/out/`, 28 files: `a1200-picture-{browse,page,question,reverted}`, `a1200-f6-video`, `nes-picture-{browse,page,question,reverted}`, `amiga1200-f1/f5/f6/f8*`, `nes-f1/f5/f6/f8*`; host emu `.ci/dev/t187-ux/emu/` as `host-*`: `core-menu`, `host-back`, `host-start`).
- Opened: `a1200-picture-page.png` (core Settings > Video, Picture list, "now" marks, hint line: correct), `amiga1200-f5-settings.png` (core F5 Settings: correct), `host-host-start.png` (host boot menu, hint "V picture").
- Not covered: the host emu flow writes no picture of the host F8 Picture page. The only host picture PNGs on the builder (`.dev-out/ha/picnote_*`) are from the older render run (app 0.2.0+85acf2b, old info text), not this commit; `ci/render.sh` not run.

### Part 6 (2026-11-11, branch t187-ux): UX review findings F1-F7 fixed

Review on the ue2emu pictures of Part 5 (`/tmp/t187emu/`); fixes in `6000baa83`, the render-bench follow-up and the in-place question text.

| Finding | Fix | Evidence |
|---|---|---|
| F1 HIGH: NES "Keep this picture?" panel empty | Cause: the question is rebuilt (clean + build) every second, and in the same pass the NES waiting menu's `ui_menu_render` painted over it; the capture caught an empty pane. `ui_picture_question` now sets the text of the existing label in place; `ui_menu_render` returns while the question stands (`pq_up`, cleared by `ui_message_clear`). Text size `t.heading`, panel height follows the text. | new step `the picture "x" has text in the box` (bright pixels in the panel, `tests/ui/steps.py`) in both picture features: tests/ui 100 passed; `nes-picture-question.png` opened: title, mode line, countdown line, key line drawn |
| F2 HIGH: chip cut at 31 characters | `chip_say[96]`, `toast_[96]` (the longest result is 17 + 48 characters) | render bench `ui_chip_say_text()` equals the whole result "Picture: back to ..." after the revert; not asserted in ue2emu: the emulator runs at ~30x real time (idle skipped), the 2 s toast is gone before a REST poll |
| F3 MED: Hz column shifts | tags stand left of the Hz in the right-aligned value, the Hz is the right edge on every row (the tags are not a fixed column: one value string per row) | `a1200-picture-page.png` opened; render bench expects `now native   50.1 Hz` |
| F4 MED: ring row without legend when a status shows | `MenuCtl::foot_line()`: status, then the ring legend on the same line when the list has a ring row (`ui.cc`, `screens.cc`) | `tests/coreapp/golden/keywalk.txt` regenerated (hashes of the F5 rows); no dedicated assert of the joined line |
| F5 MED: "Files page" | "Library (F3)" in nes, snes, neogeo, zxnext | `nes-f5-first-tab.png`, `nes-f1-home.png` opened: "Choose a .nes file in Library (F3)" |
| F6 MED: 60 Hz modes without a word, no path | info line "Your screen offers N modes. The 60 Hz ones are listed too: the core runs at 50 Hz." when a listed row is of the other rate family (PC modes have only a 60 Hz row, so the old fallback line never fired); chip "Settings > Video" on the page (replaces the media chip: the bar holds 4 chips) | `a1200-picture-page.png` opened: path chip and info line; render bench fixtures six/four |
| F7 LOW: Home two-line rows | `trx_action_row_at`: two-line row 36 px (was 30), value line at y 17 | not looked at in a picture (the Home status list of the host/core F1 page is not among the saved PNGs of the emu run) |

Runs on the builder, final tree: `dev.sh test`, `riscv`, `integrate` (render benches, tests/coreapp/emu, tests/ui 100 passed), `emu` (tests/ui 100 passed): all rc 0. Pictures refreshed in `/tmp/t187emu/` (34 PNGs from `tests/ui/out/`).

Host pictures (2026-11-11): `tests/hostapp/emu_host.sh` now also runs the host Picture flow in ue2emu (home with one entry = two-line row, F8 list, V page, applied mode; four different screens asserted, PNGs `host-*.png`). The emulator has no mode request register (`hv_gateware()` false), so the 8 s question and the revert exist only in the render bench: `picnote_settings_1080p.png` (question) and the new `picnote_revert_case` -> `picnote_settings_800x600_reverted.png` (mode back, toast "Picture: back to 1920 x 1080").

D9 built (2026-11-11): `picmodes_set_row` marks only the chosen row (Auto's row, else the fixed mode's; the resolved mode keeps none), both apps' special case removed; the page already opened on `cur`. Question keys: the key bar shows `picmodes_confirm_keys()` while the question is up (host `pm_update`, core `footer_legend`). LOW (b) "trying ..." is not in the code: the note already reads "Picture: 1920 x 1080". Evidence: `test_picture` (one mark and cursor per Auto/fixed), both render benches (host with a fixed-mode round trip), `dev.sh test`/`riscv`/`emu` rc 0; core render bench: 2 WiFi "No module" failures not from this change.

### Part 7 (2026-11-11): merged into main

D9 (one "now" mark, cursor on it, the question's keys in the key bar) built in 654dfe650; `tools/dev.sh integrate` rc 0 after the main merge (the
WiFi row fallback "No module" lost in that merge restored, keywalk golden regenerated: 170 F8 hash lines of the 16 cores with a WiFi row).
Evidence limits: in ue2emu the host has no mode request register, so the 8-second question and the revert are proven by the render bench
(`picnote_*`, `picnote_revert_case`), not by the emulator flow. Still open: S2, S5, S6.
