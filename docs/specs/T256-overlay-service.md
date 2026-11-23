# T256 — A library shows pixels over the machine's picture through a service of the base, and only the base drives the pixel layer

**Status:** PROPOSED (2026-11-23). Asked by Alex, 2026-11-23 (T239.3 D23).
Open (one line, as in the TODO table): Decided D1-D10 (Alex 11-23); service built as T239.3 T-2 (branch t239.3-t2-overlay); TRX64-Ultimate T-3 next
**Repo:** TRX64U-MultiCore. The core application (`coreapp/src`: `ui.cc`, `screens.cc`, `video.cc`, `run.cc`), the LVGL port
(`host-app/software/lvgl_port/trx_lv_port.cc`) and the SDK (`app/sdkpkg/api.md`, a new service group). No gateware (D1).
**Number:** T256.
**Origin:** Alex, 2026-11-23 (translated): "An overlay picture over the machine's picture - SDK or library? In any case not
the app's job to solve, we all three agree on that ... or?" He chose "a base service in the SDK" (T239.3 D23), then "D23: yes,
do it". The scope: D1.
**Depends on:** T239.3 (base 3.3, the SDK, the `menu_video` hook). The parked branch `t239.3-menu-still-wip` (62bbf183, a core
hook `menu_still`) is not built. T256 replaces it (T239.3 D23).
**Milestone:** M2.

---

## §1 The problem

**Who.** A library of a core: TRX64-Ultimate's trx64u and ring libraries first (T239.3 D21), any core's library later. The base
itself: the PAUSED tag, the Timewarp layout, its messages. The person at the TV, who sees the result.

**What they want.** A small element over the machine's picture: the Timewarp bar over the frame the person scrubs to, the PAUSED
tag over the stopped picture, later a library's own display over a running machine.

**What happens today** (read on t239.3, e4f99648):

- The host has one pixel layer. The HDMI chain is `vic_scaler` (`host/hdmi_out.v:157`), Gideon's character overlay (`:180`),
  `pix_layer` (`:194`), `c64_text` (`:216`). The layer shows one rectangle from DDR2. It has one global alpha (`PX_GALPHA`) and one
  key colour (`PX_KEY`, an exact RGB565 value, format 1 only). Its registers are write-only. A change takes effect at the next frame
  start. Its cost in a large core: 1,446 LUT, 2,323 FF, 2 RAMB36, 12 DSP (`cores/c64scpu/trial/reports/build_trial_S2/
  utilization_hier_synth.rpt`, row `pxl`).
- Two writers share the layer by turns: the LVGL port (menu, splash, messages, PAUSED tag, Timewarp layout) and `Video` (a core's
  picture). The hand-over is `layer_to_menu()` and `layer_to_core()` (`coreapp/src/run.cc:172-173`).
- 16 cores put their own picture on the layer (`[video] w` is not 0): amiga, amiga1200, apple2, atari800, c128, c264, cpc,
  neogeo, nes, pet, snes, vc20, zx, zxnext, ddt_mandel, ddt_novic. Nothing is under the layer for them.
- The C64-type cores (trx64u, trx64g, c64u, c64scpu) show the VIC picture under the layer. While such a machine runs, the layer
  is off (`Video::write_set`, `coreapp/src/video.cc:9-11`; `own_picture()`, `video.h:97`).
- The scaler blanks the VIC picture while the machine is halted (`host/hdmi_out.v:162`, `.blank(c64_halted)`). Behind a
  key-coloured screen of a halted machine the person sees black.
- The PAUSED tag (`coreapp/src/ui.cc:1323-1350`) and the Timewarp layout (`coreapp/src/screens.cc:1478-1501`, `build_timeline`
  `:930-989`) are full-canvas LVGL screens with the key colour as background. On a paused C64-type machine the tag sits over black.
  On a layer core the tag would take the layer with the menu's geometry (`layer_up`, `trx_lv_port.cc:96-113`) and remove the
  core's picture. Only trx64u pauses today (`coreapp/src/trx64u_ops.cc:714-720`).
- A library cannot draw on the layer. It has the menu layouts of its TOML, `TextGrid` (the terminal layout) and the dialogs
  (`AppDlg`). No service group draws pixels.
- TRX64-Ultimate's firmware draws the Timewarp still and bar in software into its layer while the machine is frozen
  (`trx_video.cc` `grab_still`, `draw_still`, `tw_draw_bar`). It draws nothing with the pixel layer over a running C64.
- An old defect (fixed by step T-2, section 5; the lines below are the code before the fix): the menu's "Overlay Transparency" is not applied when the menu opens. `ui_show()` calls `trx_lv_resume()`, which
  clears `layer_on` (`trx_lv_port.cc:331-341`); `apply_alpha()` then writes nothing (`trx_lv_set_alpha`, `:242-251`, writes only
  while `layer_on`); the first flush writes `PX_GALPHA` 255 (`:108`). Read from the code, not seen on the board.

**Why it matters.** Timewarp shows its bar over black, so the person cannot see the frame they look for. The PAUSED tag hides
the picture it marks. The trx64u library of TRX64-Ultimate (T239.3 D21) has no way to show its still through the SDK; without one
it would write the layer registers, which belong to the base.

**Parts.**

- **P1 Ownership.** One holder of the layer, and a fixed order of what is on it in every state: menu, splash, message, picture
  question, overlay, a core's picture. Includes the old defect above.
- **P2 Over a running machine.** A library's element over the running VIC picture of a C64-type core (D1 limits it to these).
- **P3 Over a standing machine.** On every core: the last picture the person saw stays behind the element. It comes from the
  host's still capture (C64-type cores), from the core's frame buffer (layer cores), or from the library (Timewarp's ring frame).
- **P4 The SDK group.** The calls, the geometry, the task rules, the clean-up when a library goes, the size checks (api.md).
- **P5 Cost.** Render time on the RISC-V, scan-out bandwidth in DDR2, memory.
- **P6 Today's users on the service.** The PAUSED tag and the Timewarp still use the service; the parked `menu_still` branch is
  deleted.

## §2 Scenarios — what proves it is solved

Aligned with D1-D10 (2026-11-23).

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 | The holder of the layer and its key and alpha are as the state table says (`ovl_plan`, `coreapp/src/overlay.h`), for every change of state: the open menu with the person's Overlay Transparency, the menu over the still (the layer opaque, the menu blended in software), elements opaque with the key colour transparent, the machine running or standing, a C64-type core or a layer core | unit test `tests/coreapp/test_overlay.cc` with a fake layer | apptests |
| S2 | P1 | The menu opens with the "Overlay Transparency" the person set, also after the layer was off (tag, splash, core picture) | ue2emu picture, then a board photo | ue2emu, board |
| S3 | P2, D1, D3, D4, D8, D9 | A library's element (an RGB565 surface; place and size from the core's UI TOML; opaque, the key colour transparent) stays over the running VIC picture of a C64-type core while the menu is closed; the picture moves under it | ue2emu feature with a library that shows an element (TRX64-Ultimate's ring library, T-3), C64-type profile | ue2emu |
| S4 | P3, D5 | A paused C64-type machine shows its last frame behind the tag, not black; the tag says only "PAUSED" | ue2emu feature, then a board photo | ue2emu, board |
| S5 | P3, D1 | A standing layer core keeps its picture behind an element; a running layer core shows no element | ue2emu feature, layer-core profile | ue2emu |
| S6 | P3, D4 | Timewarp shows the frame of the scrub position behind the bar, also under the key-coloured pages of the menu | TRX64-Ultimate's trx64u tests (T239.3 D22) | their ue2emu run |
| S7 | P4, D10 | A library built against the table before the group runs (`LibApi` ends before `overlay`: `LIB_API_COVERS`); an element of a released library, and all elements when the service ends (`OverlaySvc::clear`), are gone at once and the layer takes its state; leaving a core on the board is the FPGA's reset (`hw_return_to_host`), the service is not told | unit test | apptests |
| S8 | P5 | Render time of the first frame and of one update, measured | board, numbers in §5 | board |
| S9 | P1, D4, D10 | While the menu is open no element shows (the open menu covers it); the still is the exception and lies under the open menu; when the menu closes the elements are back | unit test with a fake layer; ue2emu picture (`tests/ui/features/overlay.feature`) | apptests, ue2emu |
| S10 | P4, D6, D7, D10 | A library hides the menu and shows it again by a call (`menu_hide`, `menu_show`): the main loop takes the call as the person's close of the menu and as the menu key; while hidden the machine runs unless the library set Pause first, the keys go to the machine, the menu key brings the menu back | unit test (one request waits for the main loop's pass; when a library calls both in one frame, the last call counts); ue2emu feature with a library that calls them (T-3) | apptests, ue2emu |

## §3 Decision

- **D1** (Alex, 2026-11-23): no new gateware. Asked whether T256 builds a second, small pixel layer in the host (an overlay over
  every running picture, all cores rebuilt), he chose "Without hardware": software only; a live overlay on the C64-type cores; on
  the 16 other cores only while the machine stands. The second layer is out (§4) and comes as its own spec if a library needs it.
- **D2** (T239.3 D23, Alex, 2026-11-23): the overlay is a service of the base in the SDK. A library never writes the layer.

- **D3** (Alex, 2026-11-23): a library draws a pixel surface. Asked how a library draws its overlay, he chose "Pixel surface": the
  library paints an RGB565 buffer itself, the base puts it at a place over the picture, pixels in the key colour stay transparent, and
  the base offers text helpers with its fonts. The still and the elements take the same way. (Corrected 2026-11-23, read in
  TRX64-Ultimate: `tw_draw_bar`, `software/ultimate-app/software/io/c64/trx_video.cc:731`, does not fit unchanged. It paints into
  the full-screen layer buffer at the absolute place of `tw_layout()` with that buffer's stride (`:735-737`), and it mixes the still
  under the bar itself (`:739`). T-3 moves it into the ring library and paints into a surface of the service.) Lost: base-drawn building blocks (one look everywhere, but the bar rebuilt and a
  second path for the still), and both (the most work).

- **D4** (Alex, 2026-11-23): an element shows only while the menu is closed. Asked what shows while the menu is open, he chose "Only
  while closed": the open menu covers the libraries' elements. The still is the exception: it lies under the menu's key-coloured
  pages too (Timewarp), so the bar stands over the frame the person looks for, not over black. Lost: elements over the open menu
  (for example a drive LED), which would need a rule that they cover no menu row (R2).

- **D5** (Alex, 2026-11-23): the base's tag says only "PAUSED". The tag showed "PAUSED   P RESUME" (`ui_paused_tag`, `coreapp/src/ui.cc`), but
  no code handles P; Pause is set only by a library (`api->ctx->set_paused`, api.md 4.14.4) or over REST (`machine.pause_toggle`,
  `coreapp/src/trx64u_ops.cc:714-723`). Asked what the tag should be, he chose "Only PAUSED": the tag promises no key; the
  library that paused shows how to go on with an element of its own; the tag is the service's first user. Lost: a P key read by
  the base while the machine holds the keyboard, and no tag at all.

- **D6** (Alex, 2026-11-23, translated): "that also means the plugin/lib must be able to 'hide' and 'show' the menu by a
  function." A library hides the menu and shows it again with calls of the SDK group. It follows from D4: its elements show only
  while the menu is closed. What happens while it is hidden: D7.

- **D7** (Alex, 2026-11-23): hiding the menu works like the person closing it. Asked what happens while a library has hidden the
  menu, he chose "Like menu closed": the machine runs unless the library set Pause first (`api->ctx->set_paused`), the keys go to the
  machine, and the menu key always brings the menu back. Showing it works like the menu key. Today a library can only close the
  menu after an operation of its own (`closes_menu`, api.md 1621, 1683). Lost: a modal hidden menu (the machine frozen, the keys
  to the library through a new key callback).

- **D8** (Alex, 2026-11-23): an element's place comes from the TOML. Asked where a library element's place on the 960x540
  canvas comes from, he chose "From the TOML": the core's UI TOML names each element with its anchor, offset and size, as the
  menu rows take their place from the TOML (T239.3 D32), and the menu simulator shows it. The library's call hands over the
  surface and redraws it. Lost: a place the library sets at run time, from the call or as a move beside a TOML default.

- **D9** (Alex, 2026-11-23): elements are opaque. Asked how opaque a library element is over the picture while the menu is closed
  (the layer has one alpha; key-colour pixels are always transparent), he chose "Opaque": elements show at full opacity, only
  their key-colour pixels let the picture through, and the person's "Overlay Transparency" applies to the open menu only. Lost:
  elements that follow the person's transparency, and one opacity per core set in its TOML.

- **D10** (Alex, 2026-11-23): the rest of the design follows from D1-D9. Asked whether T-2 builds it as read here, he chose
  "Yes, build it":
  - The calls of the SDK group: `show(name, surface)` gives an element whose place and size come from the core's UI TOML (D8);
    `update(el)` redraws it at the next frame; `remove(el)` takes it away; `text(surface, x, y, s, colour)` writes with the
    base's fonts (D3); `menu_hide()` and `menu_show()` act as closing the menu and as the menu key (D6, D7).
  - An element shows only while the menu is closed (D4): on a C64-type core over the running picture, on a layer core only while
    the machine stands (D1). Elements are opaque (D9); the open menu has the person's Overlay Transparency.
  - The still is taken when the machine stops (Pause, or the menu's stop of a C64-type machine): from the host's still capture,
    the core's frame buffer or a library's surface. A TOML element marked as the still lies under the open menu (D4), and the
    menu is blended over it in software, so the menu looks as it does over the running picture.
  - Several libraries may show elements at once, drawn in TOML order. Calls come from any task; the UI task takes them over at
    the next frame. When a library is released, its elements go, and the layer takes its state at once.
  - Limits: the TOML check refuses a place outside the canvas; a call whose buffer is not w x h x 2 bytes is refused with the
    reason; the maxima are sized from the TOML at link time. The PAUSED tag is an element of the base UI TOML (D5).
  The other answer was to talk the points through one by one first.

All open decisions are taken (D1-D10).

## §4 Out

- A second pixel layer in the host, and so an element over the running picture of a layer core (D1).
- The analog output. The layer never reaches it: only the C128's VIC picture goes to `c128_avout` (T140).

## §5 Decisions taken while building

T-2 (T239.3 plan step), 2026-11-23, branch `t239.3-t2-overlay`. Each point is read from the code; none is proven on the board.

- **Tag (D5):** `ui_paused_tag` (`coreapp/src/ui.cc`) shows only "PAUSED". It is an element of the base UI TOML (`coreapp/base_ui.toml`, `paused`, 126 x 30, top right, 14 pixels in), painted
  by the base into its own surface with the service's `text` call and shown with `show` while Pause is set; it is removed when the machine runs. The parked branch `t239.3-menu-still-wip` (the
  pull hook `menu_still`) is deleted (local only, never pushed; its last commit was 62bbf1837, not built, P6).
- **Alpha (the old defect of section 1):** `trx_lv_port.cc` keeps `alpha_reg`. `trx_lv_set_alpha` stores it even while the layer is off, `layer_up` writes it with the first picture, and
  `trx_lv_close`, `trx_lv_yield_layer` and `trx_lv_suspend` reset it to 255. The service now writes the alpha from its plan: the person's value while the menu is open without a still, 255
  otherwise (D9, D10).
- **The service is pure** (`coreapp/src/overlay.h`, `overlay.cc`): the layer and the board are two interfaces (`OverlayLayer`, `OverlayHost`), so `tests/coreapp/test_overlay.cc` drives it with a
  fake layer. `ovl_plan` is the state table (S1): a machine STANDS when a C64-type core (`Video::own_picture()`) is stopped for any reason, and any other core when Pause is set. Menu open: the
  base holds the layer; with a still the layer is opaque and the menu is blended over the still in software (`menu_opa`), without one the layer's alpha is the person's and the key is the page's
  (`live_key`). Menu closed: with a still, the still and the elements (key off); without a still, a C64-type core shows its elements over the running picture through the key; a layer core
  that runs shows no element (D1), and one that stands without a still keeps its picture and shows none.
- **Software blend:** the still is an LVGL image on the display's bottom layer; the menu is the screen with `lv_obj_set_style_opa` = the person's alpha (`ov_apply`, `ui.cc`). An opaque child over
  an opaque panel is blended twice, so the menu is not pixel-equal to what the layer's global alpha made of it. Seen in the menu simulator (`tools/menusim/run.sh vc20 --stopped`), not on the
  board. A page that is keyed otherwise (Timewarp, `build_timeline`) is transparent where the still lies under it (`ui_still_under`).
- **The still:** taken when the machine stands (C64-type: the menu's hold or Pause; any other core: Pause only), from the host's still capture (`Video::grab`, 4 bit VIC colours, Colodore
  palette; `Video::still_start` points `STILL_BASE` at two 64 KB slots of the base's own heap at the core's start and sets `STILL_CTRL`), from the core's newest frame buffer (`ddr_abs_read`
  of the rows, nearest sampling, 32 bit pixels cut to RGB565), or from a library's surface shown as the still. The base fits the capture to the still element's size (960 x 540 in the base
  table) by nearest sampling and holds it in 1 MB of heap only while the machine stands. The user's colour palette setting is not applied (the capture uses Colodore). The cost of the capture's
  first frame is not measured (S8).
- **Calls (D10):** any task calls; `show`, `update`, `remove` check and answer at once, their effect is the UI task's next frame (`OverlaySvc::frame`, called by the main loop's `overlay_pass`);
  `text` calls are queued (16) and taken in the order of the calls; a menu call is one pending request, so the last of two calls in one frame counts (hide, then show: only the show reaches the main loop). A call that takes a surface away (`remove`, a second `show` with another surface, `drop_owner`) first drops the queued text calls that write into that surface, then waits for the
  UI task's next frame, 200 ms at most, so the library may free the surface when the call returns; the UI task does not wait. The LVGL image holds the library's pointer, so no copy is made.
- **Menu calls:** `menu_hide` is the leave intent (`uiapp_local_leave`) and `menu_show` the open intent (`uiapp_local_open`), both taken in `overlay_pass`; a request made while the menu is in the
  other state does nothing.
- **Elements, record and merge:** `[[element]]` is section 25 of the UI TOML's record (`tools/descrec.py`, `descrec.cc`, same checks as `ovl_row_check`: name, anchor, size, a place inside 960 x
  540, one still), merged over the base's table by name (`ovl_merge`, `bundle_merge`) and drawn in the order of the merged table. The simulator draws every element's rectangle (`--elements`).
- **Limits:** `OVL_ELEMENTS_MAX` (16) and `OVL_TEXT_MAX` (16) are `#ifndef` defaults in `overlay.h`. P3-2 is merged into this branch: the `[sizes]` table of a core's TOML B sizes them at link time
  through `tools/app_build.py` (`SIZES` keys `elements` and `overlay_text`, written to `appsizes.h`; `overlay.h` is a sizing header, so an object that includes it is compiled per core). The key
  `elements` also sets `ELEMENTS_MAX` of `descrec.py` (`apply_sizes`, `--sizes`), so the compiler and the reader of the record refuse the same table.
- **The canvas (review finding):** the places of the TOML are given on 960 x 540, but the canvas follows the video mode (640 x 480 at 480p, 720 x 576 at 576p, 800 x 600, T164 D10a). The service
  asks the host for the canvas of the moment (`OverlayHost::canvas`, `ui_canvas_w/h`) and places every element again at each frame (`ovl_place_on`): the anchor and the offset count from that
  canvas's edges, the size stays the row's, and what lies beyond a smaller canvas is cut by LVGL. The base's capture for the still is fitted to the still's size cut to the canvas, at the moment
  the machine stops (a mode change while it stands keeps the old capture's size). A library's surface for the still keeps its row's size and is cut off at the right and below on a smaller canvas.
  Whether elements are scaled with the canvas instead is not settled by D1-D10: a question for Alex.
- **The menu over a paused machine:** `open_menu` no longer calls `ui_paused_tag(false)`: it told the service that Pause ended, so a paused layer core lost its still under the menu. Elements
  are not drawn while the menu is open anyway (D4), so the tag needs no removal then (`test_overlay.cc`, "S1 on a layer core").
- **Leaving a core:** `hw_return_to_host` does not return on success (the IPROG reset), so `ui_overlay_end` and its calls in `run.cc` are gone; `OverlaySvc::clear` stays as the service's own end (S7).
- **Not proven:** S2 has no ue2emu picture and no board photo; S8 (render time, the capture, the memory) is not measured; `run.cc`, `hw`-side and the LVGL calls of `ui.cc` on the RISC-V build are
  not compiled here (the menu simulator runs `ui.cc` natively: the still, the tag with the base's text and the blend are seen there).
