# T256 — A library shows pixels over the machine's picture through a service of the base, and only the base drives the pixel layer

**Status:** PROPOSED (2026-11-23). Asked by Alex, 2026-11-23 (T239.3 D23).
Open (one line, as in the TODO table): Spec in work: D1 no new gateware, D3 pixel surfaces (Alex 11-23); next: what shows while the menu is open
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
  the base offers text helpers with its fonts. The still and the elements take the same way, and TRX64-Ultimate's pixel code (the
  Timewarp bar, `tw_draw_bar`) fits unchanged. Lost: base-drawn building blocks (one look everywhere, but the bar rebuilt and a
  second path for the still), and both (the most work).

The other decisions are asked one at a time and recorded here.

## §4 Out

- A second pixel layer in the host, and so an element over the running picture of a layer core (D1).
- The analog output. The layer never reaches it: only the C128's VIC picture goes to `c128_avout` (T140).

## §5 Decisions taken while building

Nothing yet.
