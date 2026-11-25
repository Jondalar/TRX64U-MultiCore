# T256 — A library shows pixels over the machine's picture through a service of the base, and only the base drives the pixel layer

**Status:** PROPOSED (2026-11-23). Asked by Alex, 2026-11-23 (T239.3 D23).
Open (one line, as in the TODO table): D1-D14 (Alex 11-23/24); D11 + fix round after T-3 built (SDK 0.4.5); T-3 by FW; D13 A: 2 new calls (0.4.6)
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
- An old defect: the menu's "Overlay Transparency" is not applied when the menu opens. `ui_show()` calls `trx_lv_resume()`, which
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

Draft. It is completed when the open questions of §3 are decided.

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 | The holder of the layer and its key and alpha are as the state table says, for every change of state | unit test `tests/coreapp/test_overlay.cc` with a fake layer | apptests |
| S2 | P1 | The menu opens with the "Overlay Transparency" the person set | ue2emu picture, then a board photo | ue2emu, board |
| S3 | P2 | A library's element stays over the running VIC picture; the picture moves under it | ue2emu feature, C64-type profile | ue2emu |
| S4 | P3 | A paused C64-type machine shows its last frame behind the PAUSED tag, not black | ue2emu feature, then a board photo | ue2emu, board |
| S5 | P3 | A standing layer core keeps its picture behind an element | ue2emu feature, layer-core profile | ue2emu |
| S6 | P3 | Timewarp shows the frame of the scrub position behind the bar | TRX64-Ultimate's trx64u tests (T239.3 D22) | their ue2emu run |
| S7 | P4 | A library built against an older table runs; an element of an unloaded library is gone | unit test | apptests |
| S8 | P5 | Render time of the first frame and of one update, measured | board, numbers in §5 | board |

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

- **D5** (Alex, 2026-11-23): the base's tag says only "PAUSED". The tag showed "PAUSED   P RESUME" (`coreapp/src/ui.cc:1307`), but
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
    the next frame. When a library is released or its core is left, its elements go, and the layer takes its state at once.
  - Limits: the TOML check refuses a place outside the canvas; a call whose buffer is not w x h x 2 bytes is refused with the
    reason; the maxima are sized from the TOML at link time. The PAUSED tag is an element of the base UI TOML (D5).
  The other answer was to talk the points through one by one first.

- **D11** (Alex, 2026-11-24, translated: "Ok - answer A fits"): the ring library owns the Timewarp picture. TRX64-Ultimate's
  T-3 found two gaps: after a scrub step the still under the open menu stays at the frame of the menu stop (S6), because the
  still is taken once and the scrub runs through the ring library, which the base does not see; and the bar would be drawn
  twice, by the base (`coreapp/src/screens.cc` `build_timeline`) and by the ring library (`tw_draw_bar`, D3). Asked which way,
  he chose A: a helper call of the SDK group paints the frame the machine drew last into a library's surface (palette, place,
  Scale, like the text helper of D3); the ring library paints frame and bar as the still (D10: "a library's surface"); the
  base's timeline bar leaves with the trx64u code set (T239.3 P2-6). `core_hooks.h` (menu_video) and api.md 9.7.13, which
  describe the picture behind the menu from before T-2, are rewritten with it. Lost: the base keeps its bar and its still and a
  call lets a library ask for a new still after a scrub (smaller, but the base-drawn building block D3 gave up).

- **D12** (Alex, 2026-11-24, translated, over a picture of the Timewarp view - the C64's frame with the bar under it: "You mean
  the picture of the C64? It should look like this!!! And the bar etc. comes from trx64. You only make it possible!!! No UI for
  apps from you"): the Timewarp bar, its time, frame counter, glyph and hints are TRX64-Ultimate's, painted by its library with
  the frame (D3, D11). The base only gives the means (`frame()`, the surface, `still_palette`, the display) and builds no UI for
  an application. So the base's own timeline bar (`coreapp/src/screens.cc` `build_timeline`) is UI for an application and goes.
  - D11 let it stay until T239.3 P2-6, and that answer was also given to TRX64-Ultimate on 11-24; both are withdrawn.
  - It goes now, not with P2-6: Alex the same day, "No UI for apps from you", and T239.3 D21/D44, the FW session builds trx64u.
    The page of the layout `timeline` stays without a bar while TRX64-Ultimate's menu still names it; its keys come from the core's
    key table.

- **D13** (Alex, 2026-11-24, translated: "Claude - for apps it must work per call: fullscreen on/off, picture of the machine
  on/off, update picture, exit directly into the machine with the app off or in the background, switch to another app or menu
  item"): the base gives an application these five as calls of the SDK, and the application draws its own UI (D12). Mirrored to
  Alex the same day. Which of them the SDK has today and which are new is mapped in §5 before any code.
  The mapping was put to Alex with three options (A: document 1-3, two new calls; B: five calls of their own; C: only 4 and 5 now);
  Alex (translated): "Yes please A" and "D13: option A, I answered above".
  - Calls 1-3 exist: an overlay element of the canvas's size shown and removed (`show`, `remove`, `canvas()`), `frame()` and
    `update()` (api.md 4.17.8-4.17.16). api.md documents them as the patterns for fullscreen, the machine's picture and the update.
  - Call 4 is new: `exit_to_machine(flags)`, with OVL_EXIT_PAUSE (the machine stays stopped) and OVL_EXIT_KEEP_APP (a resident app
    stays in the background; without it the app ends). One request in the main loop ends the session, closes the menu and applies
    the pause in the same pass; it is refused like the person's close when the core cannot run yet. Today `menu_hide` with an app
    session open closes the session, not the menu (`run.cc:983-996`), and the exit is bound to keys.
  - Call 5 is new: `goto_screen(target)`, a menu place ("F3/online") or an app ("app:name[:action]"). One request in the main
    loop opens the menu if it is closed, closes another session and goes there. It is also the goto after a job of T239.3 D41.
  - Both are appended to SvcOverlay beside `menu_hide` and `menu_show` (base 3.4, SDK 0.4.6). The operations `menu.screen` and
    `app.open` are documented in api.md 4.3.

- **D14** (Alex, 2026-11-24, relayed by the TRX64-Ultimate session, translated: "That could go too and make room in RAM, since we
  put no overlay over the menu"): while a library shows a still element and the machine stands, the base frees its own still
  (`base_still_`, up to 1,036,800 bytes at 960x540). The scene draws the library's surface then and never this buffer
  (`overlay.cc` 223-230), and the retake when the library's still goes takes a new one anyway (D11, `take_still` frees first).
  Built in the fix round after T-3 (SDK 0.4.5).

All open decisions are taken (D1-D14).

## §4 Out

- A second pixel layer in the host, and so an element over the running picture of a layer core (D1).
- The analog output. The layer never reaches it: only the C128's VIC picture goes to `c128_avout` (T140).

## §5 Decisions taken while building

Nothing yet.
- **T-3's findings for the base (TRX64-Ultimate, 2026-11-24, in ue2emu on SDK 0.4.4, not measured on the board).** T-3 (u18
  5c9ff24a, 27883f2c) shows the Timewarp still through `frame()` and paints its own bar. Each scrub step's picture is that step's
  screen in the palette in force. Three findings, fixed on `t239.3-ovfix`:
  - `frame()` takes 1,193-1,206 ms per call (their bar 45 ms), so each step's picture follows its key by 1.24 s. The time is in
    `Video::grab`, which divides per pixel to scale (`coreapp/src/video.cc` grab, both paths).
  - Closing from the Timewarp page takes 1,248 ms instead of 49-64 ms: `remove()` of the library's still sets `retake_`, and
    `settle()` grabs the base's still again although the menu closes (`overlay.cc` 397-398, 624).
  - A library learns no canvas size, so their bar, laid out for 960x540, lies partly outside at 480p, 576p and 800x600.
  - And the base's opaque bar (`screens.cc` `build_timeline`) still covers theirs; it goes (D12).
- **Fix round after T-3 built (2026-11-24, t239.3 e8b767196, SDK 0.4.5, sha256 e87e2039).** The column table in `Video::grab`, no
  grab while the menu closes, `canvas()`, no base bar (D12), D14 and two review fixes. Measured by TRX64-Ultimate on 0.4.5 in ue2emu,
  the same scenario (their run 478, u18-a294af0f): `frame()` takes 217-220 ms (was 1,193-1,206 ms), their bar 45 ms; closing from
  the Timewarp page takes 48-63 ms (was 1,249-1,252 ms); their bar is the only one, laid out with `canvas()` on 640x480, 720x540,
  800x540 and 960x540. Not measured on the board.
