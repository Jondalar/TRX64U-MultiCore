# T167 — A changing progress bar or number inside an overlay never shows what lies behind the overlay

**Status:** DONE 2026-11-15 (Alex 2026-11-15: "under A there is nothing I would not class as DONE! That has been in the builds for days/weeks, we had bugs and re-tests in part.") Open board/scenario items are closed by Alex's decision, not separately recorded. Earlier status: BUILDING (2026-10-31). Status check 2026-11-13: BUILDING stays: merged (157716f3e, 070f87198); 5.10 passed on the board 11-02; 5.8 and 5.9 board re-check open.
**Repo:** TRX64U-MultiCore — host app (`host-app/software/hostapp/hostui.cc`), core app (`coreapp/src/ui.cc`), shared view layer
(`app/view/trxview.*`), benches (`tests/hostapp/render`, `tests/coreapp/render`, `tests/view`).
**Number:** T167 (registry: the table in `TODO.md`).
**Origin:** Alex, 2026-10-31, at the board (translated): "When there is a bar or % numbers in the overlays, they change. While drawing you
briefly see what lies behind the overlay - that is bad. If I have `abcdefghijlkmn` and `00%` lies on top at first, then `00%` becomes `ab%`
then `01%`, and you see the `ab` through it. Right would be `00%`, `01%` - or `00%`, `00%` (overwritten, only what is there, not what lies
below), `01%`." Seen on the restore/update progress pages (host app) and the ROM set loading (core app, Neo Geo/SNES).
**Depends on:** nothing.
**Milestone:** M3.

---

## §1 The problem

Who: Alex (and every user) watching a progress page of the boot menu (start/restore/update, format, copy/move) or of the core application
(question card with a progress bar, ROM set / image build).

Today: both applications render with ONE frame buffer in LVGL's DIRECT mode (`hostui.cc buffers_mode(true)`, `coreapp/src/ui.cc
single_buffer()`; T104 5.11 / T146: the second buffer cost a sync copy that reads DDR2 through a CPU without a data cache). LVGL then draws
straight into the buffer the pixel layer shows. A changed label or bar invalidates its own rectangle only, but LVGL draws that rectangle
from the bottom up: `lv_refr_area_part` (lv_refr.c) asks `lv_refr_get_top_obj()` for the topmost opaque object of the ACTIVE SCREEN that
covers the area and draws from there; then it draws `lv_layer_top()` and `lv_layer_sys()` "unconditionally". Every overlay of both
applications is a child of `lv_layer_top()`, so the search never sees it: the whole screen under the overlay (the menu rows, the frame) is
drawn into the rectangle first, then the overlay's panel, then the label. The pixel layer shows the buffer while this happens: for a few
milliseconds the rectangle holds the menu text ("ab") instead of the overlay's own pixels.

Parts:
- P1 host app overlays on the top layer: start/restore/update panel (`st_root`), format pages (`fm_root`), confirm (`cf_root`), rename
  (`rn_root`), splash.
- P2 core app question/progress cards on the top layer (`ui_dialog_render`): rebuilt as a whole on every revision (a bar tick included), with a
  dimming veil (50 %) that is drawn again with them, so the undimmed screen shows across the whole display on every tick.
- P3 core app in-pane progress (`job_text` row "Building the image NN%", app screens with a bar): not an overlay, same redraw.
- P4 the bench never looked at the states between two flushes (single buffer: `flush` is called once, after the pixels are written).

## §2 Scenarios

```gherkin
Scenario: S1 host start/restore/update page, percent and bar change
  Given the start page is up over the menu (single buffer, DIRECT, as the product)
  When  the percent label and the bar change step by step
  Then  after every draw task, every pixel of the panel rectangle is the old page's pixel, the new page's pixel, or the panel colour
  And   no pixel ever holds a colour that only the menu below has (count of such pixels = 0)

Scenario: S2 host format progress page   (same, fm_root)
Scenario: S3 core app dialog bar over the dimmed screen, bar ticks only the card changes; the veil is not drawn again
Scenario: S4 core app in-pane job row "Building the image" NN%
Scenario: S5 control: the same bench with the overlays on `lv_layer_top()` (the old parent) must report > 0 foreign pixels
Scenario: S6 look: the pictures of the finished pages are identical before and after (golden/PNG compare of the existing benches)
```

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1, P4 | start page, states between draw tasks | `tests/hostapp/render/run.sh` (check "overlay-start") | apptests hostapp |
| S2 | P1, P4 | format page | same (check "overlay-format") | apptests hostapp |
| S3 | P2, P4 | dialog bar | `tests/coreapp/render/run.sh` (check "overlay-dialog") | apptests coreapp |
| S4 | P3, P4 | job row / app bar | same (check "overlay-job") | apptests coreapp |
| S5 | P1-P4 | negative control: old parent | controls of both run.sh (`XFLAGS=-DTRX_OVERLAY_ON_TOP_LAYER`) must go red | same |
| S6 | all | pixels of the finished pages unchanged | existing PNG/golden checks of the render benches | same |

## §3 Decision

- D1 One mechanism: `trx_overlay()` (app/view/trxview) returns a transparent full-screen container that is the LAST child of the active
  screen. Every overlay panel is a child of it instead of `lv_layer_top()`. `lv_refr_get_top_obj()` descends into the container, finds
  the opaque panel that covers the invalidated rectangle and starts drawing there: nothing of the screen below is drawn. The panels
  are already opaque (`trx_box`: bg_opa COVER, radius 0); a rounded card still covers its inner rectangle (lv_obj.c COVER_CHECK).
- D2 Why not the other options: two buffers (sync copy, T104 5.11 numbers), PARTIAL mode with an off-screen buffer (a copy of every
  redrawn pixel through the uncached CPU: the same cost), a patch of LVGL (third-party submodule).
- D3 The core app dialog: veil, question card and bar card are three objects; a bar tick rebuilds only the bar card, a key only the
  question card. The veil is built once per question.
- D4 The container is recreated when a frame rebuild (`lv_obj_clean(scr)`) deletes it; the core app rebuilds a card that was deleted with it.

## §4 Out

- The panel colour shows for an instant when a label is redrawn (LVGL fills the label's rectangle with the panel, then the glyphs). That is
  "only what is there" in Alex's words; only a second buffer removes it.
  Decided 2026-10-31: told of the remaining blank instant (00%, panel, 01%) and of the option to compose the label off-screen first, Alex: "I
  will look at it. Leave it as it is." (translated)
- Layer-wide alpha (`trx_lv_set_alpha`) of the core app is the pixel layer's, unchanged.

## §5 Decisions taken while building

- 5.1 Cause confirmed in the code, not guessed: `trx_lv_port.cc` opens the display DIRECT with two buffers, but `hostui.cc buffers_mode(true)` (start-up) and
  `ui.cc single_buffer()` (ui_open) switch both applications to ONE buffer, so the pixel layer shows the buffer LVGL draws into. `lv_refr.c`
  `refr_area_part()` calls `lv_refr_get_top_obj(clip, active screen)` (line 1076), draws from that object, then draws `lv_layer_top()` and
  `lv_layer_sys()` unconditionally (lv_refr.c:1107-1109). All overlays were children of `lv_layer_top()`: the host's `rn_root`, `st_root`, `cf_root`, `fm_root`, the
  splash, the core app's veil/question card/bar card. Affected: every progress page of the host (start, restore, update, convert: one panel `st_root`; format,
  copy/move: `fm_root`), the core app's dialog bar (ROM set / image builds that report through `progress_open/set`). Not affected: the menu's own job row
  ("Building the image NN%", `MenuCtl::job_text`, retexted in place in the pane; probe 0 foreign pixels before and after).
- 5.2 `lv_bar` redraws its whole track before the indicator on every value change: the filled part flashed the track colour (not the layer below, but
  the overlay's own colour in a place that did not change). Replaced by `trx_pbar` (a track box and a fill box; only the fill changes). The first bench run
  after D1 alone still counted 33,216 / 53,520 foreign pixels (the track colour) - that is what showed it.
- 5.3 The check: `tests/overlay_probe.h` adds a draw unit at the head of LVGL's unit list; its dispatch callback runs between two draw tasks and copies the
  overlay's rectangle of the (single) frame buffer. A flush callback cannot see these states (one flush, after the last task). "Foreign" = a pixel that is
  neither the old picture's, the new picture's nor the panel's own colour at that moment. The panel colour is allowed by Alex's wording (`00%`, `00%`, `01%`).
- 5.4 Results (builder, `trx64-sim-env:4`): host start page 23 reports / 98 states: 0 foreign pixels (old parent: 232,246); format page 18 reports / 78 states:
  0 (old parent: 1,496,266); core dialog bar alone 14 ticks / 55 states: 0 (old parent: 243,304); bar below a question 0, band above the bar unchanged 0
  (control `UI_DIALOG_REBUILD`: 607,632 foreign and 4,859,834 changed pixels outside the bar); job row 0 (not an overlay).
- 5.5 Controls: hostapp `TRX_OVERLAY_ON_TOP_LAYER` must turn exactly the two overlay checks red (`run.sh`, "control ovtop"); coreapp `TRX_OVERLAY_ON_TOP_LAYER`
  and `UI_DIALOG_REBUILD` must turn red only overlay checks (`OVCHECK`), else the control itself is red.
- 5.6 The bench display of the host render bench is now ONE buffer as the product (it was two, which hid the problem).
- 5.7 Pictures (not in the repo): the worst state of each run, before (old parent) and after: `overlay_start_worst`, `overlay_format_worst`,
  `overlay_dialog{0,1}_worst` in the render bench output directories.
- 5.8 Board: not run (never touched). Alex's check: restore/update page (R/U), format (F), and a core app ROM set build; the number changes must show
  only the page's own pixels.
- 5.9 (2026-11-01) Alex at the board (translated): "Finding: the updater / txu / restore overlay part needs the fix with the background tile that shows through, too. In the core start it now looks very calm and clean, in the updater it did not. If at all, then apply it everywhere. The stuff should be encapsulated in one 'class'."
  Cause in the code: the core start shows its panel once (texts set while hidden, one area), but the format/restore/update/inspect/bug-report pages (`fm_draw`) re-text eight labels, recolour lines and toggle the field and bar of a panel that is already up, so LVGL gets one small area per label and the page rolls over line by line. Fix: `TrxPage` (app/view/trxview.h): its constructor invalidates the panel's whole rectangle first, LVGL drops every smaller area given afterwards (`lv_inv_area`: "not in one of the saved areas"), so a page change is ONE area drawn once from the panel down; its destructor shows the panel. `trx_page_hide()` is the only way to take one away. Call sites (hostui.cc): `start_screen_open`, `start_screen_update` (step change; a percent tick stays in place), `confirm_full`, `rename_flow`, `fm_draw` (every format, restore, update, convert, inspect and bug-report page; only when title, frame colour, input field, bar presence or a line colour changes, `fm_is_new_page`; a tick changes labels in place), `hostui_test_confirm_panel`, `hostui_test_rename_panel`; hides: `start_screen_close`, `fm_close`, `confirm_full`, `rename_flow`, panel creation. The splash (own bar, built once) and the menu's panes are not overlay pages and stay.
  Checks: `tests/hostapp/pages_lint.sh` (run.sh unit `pages_lint`): every panel made by `box(trx_overlay(), ...)` is shown only in a TrxPage scope and hidden only by `trx_page_hide()`, the five page functions open a scope; controls `pages_no_scope`, `pages_bare_hide`, `pages_bare_show`, `pages_new_panel` each break one rule and must fail. Render bench: "page areas" line, the start page's step change and the format page change hold ONE invalidated area, a tick holds 2..16; control `pgarea` (`TRX_PAGE_NO_INVALIDATE`) turns exactly those two checks red.
- 5.10 (2026-11-01, T164 branch `t164-progress`) Alex at the board: when a large core starts (SNES, Neo Geo, ZX Next) the loading bar of the start page "never moves, 0 %". Cause in the code: `start_screen_update` (hostui.cc) drew the bar from the percent of the CURRENT step only (`trx_pbar_set(st_bar, st_pct, 100)`), and `start_core`/`start_full` report `progress(STEP_HANDOVER, 0)` and `progress(STEP_JUMP, 0)` after the work; the bar fell from 100 to 0 at "Hand over" and stood there while the FPGA was reloaded and the core application came up (seconds for a large core, the last picture of the host). Fix: `start_bar_pct(step, pct)` = (step*100 + pct)/STEPS, five equal shares, so the bar never goes back (verify 0-20, flash write 20-40, flash check 40-60, hand over 60, jump 80); the number at the step's row stays the step's own percent. Because the bar now keeps its fill across a step change, the page redraw of 5.9 would have drawn the track colour over the filled part for one draw (636 foreign pixels in the probe, 0 before): `TrxPage(root, keep)` invalidates the panel as the strips above and below the kept object (two areas that do not overlap, LVGL does not join them), the start page keeps `st_bar`. Bench `page areas`: a step change is now at most 3 areas (strip above, strip below, the bar's own growth) instead of exactly 1; no label has an area of its own (control `pgarea` gives 6). `pages_lint.sh` accepts `TrxPage page(root, keep)`. Not touched: the core application's splash (4 steps, its bar already moves per step; its ROM load is a single `rom_read` of at most 512 KiB) and the dialog bar of `AppDlg` (moves per `progress_set`, T167 5.4).
  Check: render bench (host) "start bar" lines: 19 reports over the five steps of a start, fill 0 .. 560 px, never back (`back == 0`), at the jump report (percent 0) already 448 px (`jump0 > 0`, `>= 3/4`), 0 foreign pixels over the File Browser (`overlay start: 23 reports, 106 states, 152 visits, 0 foreign pixels`). Control `t164_start_bar_per_step` (the old `st_pct` bar) fails as it must (2 checks red); control `pgarea` still fails as it must (2 checks red). Pictures: `start_bar_s1_p50.png`, `start_bar_s4_p0.png`, `start_bar_s4_p100.png` in the bench output. Not verified: the board (the stuck 0 % was reported from the board; the cause is read from the code and the bench drives the reports a start makes, not the real staging). Alex's check: start a large core (SNES, Neo Geo, ZX Next) and watch the bar at "Hand over" and "Jump".
