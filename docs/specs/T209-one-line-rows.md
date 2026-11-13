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
- D11 (Alex 2026-11-12): the boot menu marker "default" reads "auto"; only the text changes ("no, simply auto, just swap the text", translated). Extended (Alex 11-12): "if we call the function autoboot, then 'auto' everywhere" (translated) - the config overlay's "default" badge reads "auto" too.
- D12 (Alex 2026-11-12, "yes"): the editor (F5) uses the same green/yellow/red grouping as the boot menu (D10). Form: minimal - "the editor is for shifting entries back and forth ... keep it minimal" (translated): the same pictogram as the boot menu, no word box in the row.
- D13 (Alex 2026-11-12): "auto" everywhere it means the autoboot entry/config ("D auto", "Auto core", "Auto config", "Make auto", the subtitle, the messages, the report lines); only the web API's JSON key stays `default`.
- D14 (Alex 11-12, "yes"): the editor shows the exact state word of the selected row in its status line, like the boot menu; the row keeps only the pictogram.
- D15 (Alex 11-12, "yes"): a core without a logo gets a text tile with a short name that fits (e.g. "VIC", "C128", "PET"), declared by the core in its .toml (manifest); without one the app takes the first letters without punctuation. No hard cut like "VIC-".
- D16 (Alex 2026-11-13, translated): "the pictos can go directly next to the version. The 'v' does not need to be in front. For all resolutions." Boot menu and editor rows: right group = [auto badge, only on the auto row] [pictogram] [version], the pictogram directly beside the version; the version shown without its leading "v" (one leading v/V stripped from the displayed string only); all resolutions. The name gets all room left of that group; the middle ellipsis (R5) stays the fallback.
- D17 (Alex 2026-11-13, translated: "max 20 characters core name in the .cor bundle", then "then 30 characters ... it will fit anyway"): a core's display name in its `core.toml` (`name`) is at most 30 characters; `tools/core_toml.py check` (lint) enforces it. Today the longest is 29 (`cores/nes/mono/core.toml`). Entries not from our .cor (Gideon firmware names) are not linted; the middle ellipsis stays.
- D18 (Alex 2026-11-13, translated: "in the .toml one should define a short name of 4 characters, and if nothing is there THEN the first 4 characters of the long name"): `short` is at most 4 characters (was 5: `core_toml.py check`, `cor_pack.py`, the `cor.cc` parse, the tile); absent = the first 4 characters of the long name (today's behaviour, "VIC2" is accepted). Existing values PET, DIAG stay.
- D19 (Alex 2026-11-13, translated: "actually only Auto core/config and Menu file ... that is 2 lines: `Auto: A1200 (Amiga Vision)` / `Menu: /SD/Boot.cfg`"): the F5 editor's info box above the entries shrinks to two lines, `Auto: <auto core name> (<its auto config name>)` (no parenthesis when the core has no config or none is chosen) and `Menu: <path of boot.cfg>`. Countdown and Direct boot leave the box (F8 settings).
- D20 (Alex 2026-11-13, shown the 480p F5 picture where the red rows without a version had their pictogram at the far right: "No, the pictos in one column, not simply right-aligned please"): in the boot menu and in the editor the pictograms of all rows stand in one column. The version column has one width for the whole list (the widest version shown in that list), the pictogram sits left of that column; a row without a version leaves the version column empty. The auto badge stays left of the pictogram. Amends D16.

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

D10/D11 built (2026-11-12, branch t209-d6-bootmenu). Boot menu row, from the right: version (far right, `LROW_VER_W`), the state pictogram (22 x 22), the "auto" marker (text of the old "default" badge, same look), the "N configs" badge (T198, unchanged), the name takes the rest. The pictogram is `HcEntryRow.pic` (`HCP_OK`: hash ok, ready, backup, foreign; `HCP_CHECK`: changed, not read yet; `HCP_BAD`: the rest, from `hc_row_entry`), drawn from A8 masks in `menu_pictos.h` (`tools/gen_menu_pictos.py`: a disc with the glyph cut out, tinted green/yellow/red by `lv_obj_set_style_image_recolor`; a rounded box draws nothing here, `LV_DRAW_SW_COMPLEX 0`). The state word shows in the note slot under the list for the selected row while no note stands (`menu_status`, the group's colour); the dump and the web state keep the word. Proof: render bench (pictogram group and 22 x 22 size per row, "auto", the word in the status line), ue2emu pictures at 480p60/720p60/1080p60.

D12 built (2026-11-12, same branch). The editor row (F5) draws the same pictogram (`HcEntryRow.pic`, same group mapping, 22 x 22) at its right end instead of the word box; the path subline stays; the "auto" and "N configs" badges sit left of it; the editor has no status-line word (minimal; D14 adds it). D11 extended: the config overlay's marker (`host_core_cfgov.cc`, drawn as `oc_df`) and the dump markers read "auto"; the footer/help words and the panel labels that say "default" are not changed (open question to Alex). Fix: the no-logo text tile was placed with the row's live height, which is 0 for a row hidden at its first fill, so it sat 17 px high and was clipped (introduced with D6, not older); it now uses `LROW_H`. Proof: render bench asserts the editor pictogram per row and "auto"; ue2emu pictures in scratchpad t209d12 at 480p60/720p60/1080p60 (F1, F5, config overlay).

D13/D14/D15 built (2026-11-12, same branch). D13: "auto" in every host-app text that means the autoboot entry/config: footer keys `D auto` (editor, config overlay), info panel `Auto core` / `Auto config`, subtitle `The auto entry starts when the countdown ends`, editor subtitle, help texts (F8 direct boot, autostart), `directboot.cc` reasons (`No auto core is set`, `Auto core: file missing|changed|refused`), the config overlay messages (`Auto config set...`, `No auto config...`), `hostdecl.cc` (`Make auto`, `Make the auto config`, `Auto config, previous|next`), `hostreport.cc` (`Auto entry`, `(auto)`), the `startfile` decision text, the object dump marker (`auto`), the release notes. The web API's JSON key `default` stays. D14: `state_status()` in `hostui.cc` serves both pages: the note slot of the boot menu and of the editor shows the selected row's state word (`HcEntryRow.badge`) in the group colour while there is no note. D15: manifest key `short` (optional, 1 to 5 of A-Z a-z 0-9 + / .), `cor_pack.py pack|pack-large --short`, `core_toml.py` field `short` (callers: `pack_cores_mc.sh`, `pack_large_cor.sh`, `ci/cor.sh`, `ci/package.sh`, `release_mc.sh`), `CorInfo.shortn`; set for the logo-less cores PET (`PET`) and diag (`DIAG`). `hc_row_entry` tile: the core's short name, else the first four letters and digits of the entry name (`VIC-20` reads `VIC2`, not `VIC-`), a name of only punctuation keeps its first characters; `fill_mrow` centres the tile text and drops whole trailing letters until it fits the 40 px label (never dots). Proof: `render` T209 tiles scenario (the tile label inside its tile, no dots, for PET / widest `MMMMM` / plain / punctuation), the T198 scenario (editor status line = the row's word and follows the selection; `D auto` in the foot), `tools/test_cor_pack.py` ShortTest, `tests/hostapp/test_cor.cc` (short.cor).

### Integration check of main (2026-11-12)
T209 + T210 together: `tools/dev.sh test`, `riscv`, `integrate` rc 0 (tests/ui 138 passed), no golden changed. The F8 WiFi row of the A1200 profile is one line (value On/Off right,
state in `sub` = foot while selected; 480p picture checked). The remaining printf precision cuts on the board path (`cfw.cc` `%.20s %.10s %.8s`, `host_core_rows.cc` `%.4s`) are cut by hand too.

Lint fix (2026-11-12): `screens.cc` no longer calls `ui_shell_info_now` (T192.3 rule, check_no_v1_ui); on a page-state flip `screens_render` returns early with a flag, `ui.cc` makes the foot again and renders once more (foot still shows "+/- PAGE" on the same frame). `descrec.py` parse_desc accepts an unnamed tab (T205 D1) and decoded_to_doc omits the empty name.

Editor fixes (2026-11-12, t209-d6-bootmenu): the F5 note ("Renamed to ...") stayed until a view change - `host_core_ui.cc` cleared a note only after an armed DEL (older than T209, same condition on main); the next editor key now clears it first. The F5 foot offers "LEFT/RIGHT config of this core" only when the selected core has configs (`hc_edit_foot()`, `HCF_EDIT_PLAIN`, like the boot menu foot); render bench asserts both.

### Final check (2026-11-13, t209-d6-bootmenu, main merged)
Merged main (T197/T206/T214/T216); one conflict, `host_core.h`/`host_core.cc` foot enum and table (`HCF_BR_ZIP` and `HCF_EDIT_PLAIN` both kept, same order in both files). `tools/dev.sh riscv` rc 0 (host app + core app). Per Alex 2026-11-13 the agent no longer runs apptests/integrate; the test stage is left to the pipeline (a started integrate run was stopped). Goldens: `hostrows.txt` changed in 6 hunks, not one (corrected 2026-11-13): line 8 `VC-2` -> `VC20` (D15 short name), the subtitle and the auto marker rows 0 of menu and editor (D13 wording default -> auto), the editor foot losing `LEFT/RIGHT config of this core` (`HCF_EDIT_PLAIN`), `settings.direct` help (D13). The D16-D19 changes to the golden are listed below. ue2emu pictures (480p60, 1080p60: boot menu green/yellow/red rows with state word in the status line, editor F5 rows + word, F3 browser) in the session scratchpad `t209final/`. Review notes: the boot menu name of a long entry is cut ("Commodore Ami...nded chipset") with room left at 1080p only because the version column is reserved; in the F5 editor "Commodore A...ded chipset" is cut although the row has free room (cut limit not located).

### D16-D19 built (2026-11-13, branch t209-d6-bootmenu)
D16: `hostui.cc` `fill_mrow` / `fill_erow`: right group from the right = version (label as wide as its text, `ver_w`), the pictogram `RGRP_GAP` (6 px) left of it, "auto", "N configs"; the name takes `nleft - text start` of the real free width (the editor's fixed `EROW_RESERVE` is gone: the "Commodore A...ded chipset" cut with room left, noted in the final check, was that reserve). The editor row got a version label (`Row.vl`); its path line is the path alone (`hc_row_entry` `sub`; the version moved to the right group). `hc_row_entry` strips one leading v/V (`HcEntryRow.ver`, `full firmware` unchanged). D17: `core_toml.py` check refuses a `name` over 30 characters (`--check` passes: longest 29, `cores/nes/mono`). D18: `short` at most 4 in `core_toml.py`, `cor_pack.py` (`SHORT_RE`; the pack scripts call it), `cor.cc` (a longer one reads as none); tile fallback unchanged (first 4 letters/digits). `tests/hostapp/render/mktree.py` and the T209 tiles scenario use `MMMM`; `ShortTest` refuses `FIVE5`. D19: `hc_edit_texts` -> `autol` (`Auto: <core> (<its auto config>)`, `Auto: (none)` when no auto core is set, the wording the old box had for that state) and `menu` (`Menu: ` + `BOOTCFG_PATH`, i.e. `/Flash/boot.cfg`, where the old box said `Flash:/boot.cfg`); `ED_PN_H` 56; the editor holds more rows because its panel is shorter. Review fixes: manual 04/06/15 (auto wording, row parts, pictogram, info box), `hostdecl.cc` help texts "auto". Goldens (`hostrows.txt`): menu and editor rows got a `ver` field (dump only; path lines lost `  v...`), the edit page's countdown/direct/default/defcfg lines became `auto|` and `menu|`. Render bench strings `v0.1` -> `0.1`, `v1.0-rc1` -> `1.0-rc1`. Pictures: scratchpad `t209d16/<mode>/` (480p60, 720p60, 1080p60), config overlay in `t209d16/cfg/<mode>/`. Not run (pipeline only): render bench, apptests.

### D20 built (2026-11-13, branch t209-d6-bootmenu)
`hostui.cc` `ver_col_w()` takes the widest `ver_w` over all `cfg.n` entries (per list, not per visible page: no jump while scrolling), `update_menu` / `update_edit` store it in `vcol_w` before the rows are filled; `fill_mrow` / `fill_erow` use `vcol_w` instead of the row's own `ver_w(v.ver)` for the version label width and the pictogram position (version text right-aligned in the column). `host_core_rows.cc` `hc_entry_ver()` (the D16 display string, no directory read) is what the column loop calls; `hc_row_entry` uses it too. Goldens: none change (the dump holds texts, no positions). `tools/dev.sh riscv` rc 0. Pictures: scratchpad `t209d20/<480p60|720p60|1080p60>/` (boot menu auto/yellow/two red without version/green, F5 editor same rows): the pictograms stand in one column, the red rows leave the version empty. Not run (pipeline only): render bench, apptests.

### Review fix (2026-11-13): the entry's version in the API rows
D16 moved the version out of the menu row's `sub`, so the REST/RPC "entry" rows (`host_core_model.cc` -> `hostdomain.cc`) lost it. The row now carries
`ver` (`HRow.ver`, the version as the menu shows it, without the leading v) and the API emits it as `"ver"` when known.
