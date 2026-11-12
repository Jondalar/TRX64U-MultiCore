# T209 — Every list row in host and core app is one line, rows sit closer, lists scroll before the edge, and the paging keys are shown

**Status:** PROPOSED (2026-11-11, Alex).
**Repo:** TRX64U-MultiCore — core app (`coreapp/src/ui.cc`, `screens.cc`, `menuctl.cc`, `app/view/trxview.cc`), host app (`host-app/software/hostapp/hostui.cc`).
**Number:** T209.
**Origin:** Alex 2026-11-11 (translated): "these sublines ... I want to question their sense strongly from a UX view"; on the
mock-ups (artifact G6Bx88v9pTHtBfp3DrTvuh): "Yes, that is much better. I even find the gap between the rows still too big. With longer
lists the scrolling should be looked at too. It need not wait for the very top or bottom. And the keys for paging (+ -) are not shown
at the bottom."
**Depends on:** T205 (settings in one place) merged first; it rewrites the same screens.
**Milestone:** M2.

---

## §1 The problem

- P1 **Sublines.** Two-line rows (name over a small grey line) halve what fits at 480p and mostly repeat the same text. Inventory
  2026-11-11 (row counts at 480p from the code's row heights, with / without): core F3 Library 5/8 (size, "Folder", "Medium"),
  core F5-F8 media slots 7/10 (mounted file or "No image"), core settings with a reason 6/8 ("Not available now", a path), core flat
  lists 9/15 (key hint, the same on every row), host F1 boot menu 3 at 480p (path + version), host config overlay (media + " - all settings").
- P2 **Row gap.** Even one-line rows are spaced too far apart (Alex, mock-up at pitch 29 px at 480p).
- P3 **Scrolling.** The list scrolls only when the selection reaches the first or last visible row; the next rows are not seen ahead.
- P4 **Paging keys.** Lists page with +/- (PAGEUP/PAGEDOWN, `menuctl.cc:1374`, `:1407`), but the foot never names them.

## §2 Scenarios

Proof = the real app in ue2emu (`tests/ui` features, `I save the picture`) at 480p and 1080p, ux-review PASS (skill R5), plus
the R1/R2 geometry asserts.

- S1 Library: one line per entry, the size right-aligned as the value, no "Folder"/"Parent folder"/"Medium" text; a 31+ character
  name is cut with an ellipsis and never covers the size.
- S2 Media slots: the mounted file is the row's value, cut in the middle when long; "No image" is the value of an empty slot.
- S3 Settings and flat lists: no subline; a reason ("Not available now", why) or a key hint shows in the foot for the selected row only.
- S4 Host F1 boot menu and config overlay: name only; version / "all settings" as a badge; the selected entry's path in the foot.
- S5 Row pitch reduced on every list (value chosen with Alex from emulator pictures); at 480p the Library shows more than 8 entries.
- S6 Scroll margin: moving down, the list scrolls while 2 rows are still below the selection (and up likewise), except at the list's
  ends; the selection is never on the first/last visible row unless the list ends there.
- S7 Every list longer than one screen names its paging keys in the foot (e.g. "+/- PAGE"), in the same words in host and core app.
- S8 Board: Alex looks at F3, F5 and the host menu at 480p and 1080p.

## §3 Decisions

- D1 (Alex 11-11, mock-ups): one line per row on all four screens as drawn; the subline is the exception, justified in this spec.
- D2 (Alex 11-11): rows closer together than the mock-up's pitch.
- D3 (Alex 11-11): scrolling starts before the edge. Margin 2 rows (Alex 11-11: "Ok").
- D4 (Alex 11-11): paging keys shown in the foot.
- D5 (Alex 11-11, asked whether T209 goes into this test release or the next: "No, still with T209 please"): into this test release.
- D6 (Alex 2026-11-12): the slim list must not change the boot menu: "the slim file list view should not change the boot menu!!!"; logos back "in full, as big as before". The row is the mock-up's: logo left (pre-T209 size), name, version right-aligned; "I said nothing about the path" - no path, no subline in the row (translated). Margin 2, 480p subtitle/legend and printf fixes stay.
- D7 (Alex 2026-11-12): the boot menu pages with + and - like the browser (T180 D7), keys bound per context in host_core.cc, named in the foot; "it would work in the boot menu too" (translated). The editor keeps +/- for moving an entry.
- D8 (Alex 2026-11-12): the boot menu shows no path; the path shows in the editor view (F5), "because there it makes sense to see where it comes from" (translated). In every editor row (Alex 11-12: "in every row"); the editor row is the pre-T209 boot menu row copied without the logo: name, the path smaller under it (Alex 11-12: "smaller under the name as before in the boot menu, just copy that, only without logo").
- D9 (Alex 2026-11-12, asked whether two editor rows at 480p are acceptable): "leave it like this for now" (translated).
- D10 (Alex 2026-11-12): boot menu row: version far right; a pictogram instead of the wide badge box ("hash ok"/"not verified"). Three groups: green = usable (hash ok, ready, backup, foreign), yellow = check with R (changed, not read yet "?"), red = not usable (missing, unusable, contract N != M, other host, damaged). The exact word goes into the status line at the bottom for the selected row ("details gladly"). Pictograms (Alex: "exactly that"): green filled circle with a check mark, yellow circle with a question mark, red circle with a cross, all the same size.
- D11 (Alex 2026-11-12): the boot menu marker "default" reads "auto"; only the text changes ("no, simply auto, just swap the text", translated).

## §4 Out

Nothing yet.

## §5 Build notes

WIP 2026-11-12 (branch t209-rows, not finished): core app done in code - rows one line (`trx_row_h` 18, status row 22, setting row 28; pitch +1), `trx_fit_mid`
middle cut for values and Library names, Library value = size, media slot value = mounted file / "No image", `MenuCtl::sel_detail()` puts a row's detail in the foot
(`foot_line`), scroll margin `TRX_SCROLL_MARGIN` 2 in `trx_window`, `trx_window_h`, `window_first`, "+/- PAGE" in `footer_legend` via `note_paged`. Pictures: 480p A1200 and
trx64u (the emulator offers no mode choice for it, only its own 1080p frame with the 960x540 canvas top-left). NOT done: host app (boot menu rows, config overlay,
480p subtitle cut and legend wrap), ux-review, goldens/bench updates (`tests/coreapp/render/render.cc` uses `legend_line()`, removed), 720p/1080p picture sets,
board S8. Open: Home STATUS panel (`trx_action_row_at`) still label-over-value; dim rows take no focus on screens, so their reason never reaches the foot.

Core app finish 2026-11-12: dimmed rows (`it.dim`) take focus now (`MenuCtl::move`, `settle` no longer skip them; activation stays governed by `enabled`), so
their reason (`it.sub`) reaches the foot (S3). `render.cc` uses `foot_line()`. `keywalk.txt` golden regenerated (focus hashes; `ids.txt` unchanged). Pictures: A1200 profile
at 480p/720p/1080p (`UI_VIDEO`), trx64u only as its own fixed 1080p frame (ue2emu: the scaled pixel layer is a MultiCore-host feature, `--screen` does not change the
C64U board's frame), cropped to the 960x540 canvas. Open: Home STATUS panel (label over value), host app part, board S8.

UX-review fixes 2026-11-12 (core app): the review's FAILs were one defect and two layout gaps. (1) Long names showed as `......` before the whole name (and bare `...`
before): `trx_fit_mid` built "start...end" with `snprintf("%.*s")`, which the firmware's printf does not honour (`small_printf`); it joins by hand now (`join_mid`),
and when nothing fits it keeps the first and last two characters, never a bare "...". The render bench cannot see this (host libc honours `%.*s`); the ue2emu pictures
of the RISC-V image did (`t209-lib-big-end`). (2) Page title (`trx_head`) and status-row title are cut in the middle to their room and clipped to one line (no wrap
into the line below); the SELECTED pane name is one line, cut in the middle. (3) A focused dimmed status row uses the muted ink (readable), an unfocused one the dimmer ink.
Bench asserts: `trx_fit_mid` at 120..520 px for four real names (never "...", never wider than asked), the Library row's label holds the cut name whole inside its box,
`txt_overlaps` on the A1200 Home at 480p and at the full canvas (no two texts on each other); two Library area budgets raised to PANE + 10000 (362,326 / 360,739 px
measured; the failure was already there before these fixes); `tests/view/render.cc` and the "two-line row" case no longer use `TrxRow::sub`. trx64u frame: plain
TRX64-Ultimate mode (fixed 1080p, canvas 1x), scaled pictures come from core profiles only. Open: `host-app/.../hostui.cc:1616-1617` uses `%.*s` too (host app part, another branch).
Second review round (same day): the Home session title and video line (`ui_home`), the Status panel's label-over-value rows and the subtitle (`trx_action_row_at`) are cut in the
middle to their room and clipped to one line; `tests/view/render.cc` expects `trx_window(14, 11, 0, 9)` first row 5 (margin 2). ux-review of the core pictures (A1200 480p/720p/1080p,
trx64u): PASS (open, low: the Home media name is cut by the media state text before the width fit, so "Disk_1_of_3" is lost at 720p/1080p; "(AMIG)" core id in the Core value;
the core app greys no Eject row of an empty slot, trx64u does).
### Host (built 2026-11-12)

hostui.cc MROW_H/MROW_P, build_row/fill_row without icon, row_detail (path and version in the note slot), set_txt_mid (middle ellipsis),
set_foot (compact break between keys); layout_counts computes menu and editor rows separately (erows_n, hc_nav_geometry(..., edit_rows))
and runs before build_ui at start; listwin.h LISTWIN_MARGIN 2; hc_browse_foot appends '+/- PAGE' when bn > rows.
The editor rows (F5) carry no icon: a 32 px picture does not fit a 24 px line (the boot menu is restored by D6, below). `hostui_test_dump` lists `rows=` and `detail=`; the scroll scenario
builds menu rows + 4 entries instead of 9. Selection-move area limits of the editor carry the detail line (+6000 px).
The config overlay scrolls through listwin_fix too (host_core_cfgov.cc window(): it had its own window without the margin). The two `%.*s`/`%.40s` snprintf cuts in hostui.cc/host_core_cfgov.cc are hand joins (the RISC-V printf ignores a precision; set_txt_mid already was one).
Open: host menu/editor paging keys (Alex), S8 board.

D6/D7/D8 built (2026-11-12, branch t209-d6-bootmenu). Boot menu row (hostui.cc `build_mrow`/`fill_mrow`): the mock-up row, 38 px high, 40 px pitch: the logo at its pre-T209 size (32 x 32 in the 48 px tile, text tile while there is no picture), the name (F_TEXT, middle ellipsis), the version right-aligned (`HcEntryRow.ver`: `v0.9.0`, `full firmware`, empty while unknown; column 104 px, 96 compact), then the badges; no path anywhere in the menu, note slot = `nv.note` as before. Row count = what fits (`(CH - MENU_ROW0 - 80 + 2) / 40`: 7 on the 960x540 canvas, 6 at 480p). D7: `+`/`-` page the menu (`HCA_MENU_PAGE_DOWN/UP`, `hc_nav_act` HCX_MENU, a page = `g_mrows`, stops at the ends); `hc_menu_foot` appends `+/- PAGE` when the menu is longer than its window; render bench S3 walks the keys. D8: the editor row (`build_erow`/`fill_erow`) is the pre-T209 menu row (fcd8abd9a^1) without the logo: name F_TEXT at y 6, subline F_SMALL at y 30 (the old subline = path + `  v<version>` or `  full firmware`, from `hc_row_entry(i, true)`), badges at y 17, row 56 high, pitch 62; the editor holds what fits under its panel: 4 rows on the 960x540 canvas, 2 at 480p (the row count gives, not the row). The editor keeps `+`/`-` for moving an entry. Kept: margin 2, middle ellipsis, `set_foot`, title-line position, layout_counts before build_ui, the hand joins. Restored: the T101/T114 logo asserts (S4, S4b, scenarios_ic) and the MROW area limits. Open with Alex: where the path of a boot menu entry may show (none for now).

### Integration check of main (2026-11-12)
T209 + T210 together: `tools/dev.sh test`, `riscv`, `integrate` rc 0 (tests/ui 138 passed), no golden changed. The F8 WiFi row of the A1200 profile is one line (value On/Off right,
state in `sub` = foot while selected; 480p picture checked). The remaining printf precision cuts on the board path (`cfw.cc` `%.20s %.10s %.8s`, `host_core_rows.cc` `%.4s`) are cut by hand too.

Lint fix (2026-11-12): `screens.cc` no longer calls `ui_shell_info_now` (T192.3 rule, check_no_v1_ui); on a page-state flip `screens_render` returns early with a flag, `ui.cc` makes the foot again and renders once more (foot still shows "+/- PAGE" on the same frame). `descrec.py` parse_desc accepts an unnamed tab (T205 D1) and decoded_to_doc omits the empty name.

Editor fixes (2026-11-12, t209-d6-bootmenu): the F5 note ("Renamed to ...") stayed until a view change - `host_core_ui.cc` cleared a note only after an armed DEL (older than T209, same condition on main); the next editor key now clears it first. The F5 foot offers "LEFT/RIGHT config of this core" only when the selected core has configs (`hc_edit_foot()`, `HCF_EDIT_PLAIN`, like the boot menu foot); render bench asserts both.
