# T24 — Own LVGL UI on the pixel layer

**Status:** spec 2026-10-02; phase 1 (LVGL in the build, display driver, boot splash) built on
branch `t24/lvgl-splash` of `software/ultimate-app` (two commits on top of `trx64/main`
b9c91b98, to be merged by the main session). Not yet on the board.
**Task:** TODO.md T24. **Builds on:** the pixel layer of T12 P4 / ARCH §7.5, §15.3
page 6 (`pix_layer.v`). **Shares with:** T12 (one owner of the layer at a time, §7), T26
(the host firmware uses the same port, §10).

**One sentence:** LVGL v9 runs in the Ultimate app on the RISC-V, draws RGB565 into two full
960 x 540 frame buffers in the app's memory, and shows them through the pixel layer's page
flip; phase 1 is a boot splash that covers the C64 while it starts and fades out.

---

## 1. What the hardware gives (ARCH §7.5, §15.3; code: `pix_layer.v`, `trx_io.v`)

| Item | As built |
|---|---|
| Format | RGB565 only, little endian, R 15:11 G 10:5 B 4:0 (`PX_CTRL` b3:1 = 1); widened to 888 by bit replication |
| Size | `PX_SIZE` {h, w}, w ≤ 960; scale 1-3 (`PX_CTRL` b5:4). Full screen: 960 x 540 at 2x = 1920 x 1080 |
| Buffers | any 16-byte aligned DDR2 address the app can reach (its own 64 MB); `PX_STRIDE` multiple of 16 |
| Flip | `PX_FRONT` is read at frame start; a write to `PX_BACK` swaps at the next frame start, then `IRQ_CAUSE` b7 (flip done) |
| Vsync | `IRQ_CAUSE` b6 at frame start (45 lines before the first visible line) |
| Blend | colour key `PX_KEY` (b16 on, one RGB565 value transparent), global alpha `PX_GALPHA` (`out = bg + a·(fg − bg)/256`, exact at 0 and 255); no per-pixel alpha |
| Interrupt | one ITU line, `irq_high(2)` (since T28), dispatched by cause in `software/io/c64/trx_io.cc` (`trx_irq_attach`, up to four handlers; every handler registered for a cause is called) |
| Detection | `ID` = "TRX2" at 0x101A0000, `CAPS` b6 video, b7 RGB565 layer; Gideon's bitstreams read 0 |

Cost of the full-screen layer: 1,036,800 bytes per buffer, 52 MB/s scanout (4.3 % of the
MIG peak, T12 §5.3).

## 2. Where it runs

Phase 1-3: inside Gideon's Ultimate app (`software/ultimate-app`, branch `trx64/main`, U64-II
RISC-V build `target/u64ii/riscv/ultimate`), next to his tasks, on FreeRTOS. Later the same
port runs in the T26 host firmware (§10). Doctrine from TRX64 (`docs/808-rewind-transport.md`
§2, rule 6 "API first, never UI without the API"): every action the LVGL UI offers is a thin
call into a function the REST API / SubsysCommand path also reaches; the UI holds no
behaviour of its own.

## 3. LVGL version and how it is vendored

- **Version: v9.6.0** (tag `v9.6.0`, commit `80ca777e37a2b176770726a02e07a6fb79ef0b39`), the
  newest v9 release on 2026-10-02. Pinned: an upgrade is a deliberate step (§3.2).
- **Licence:** MIT (`software/lvgl/LICENCE.txt`; third-party parts listed in
  `COPYRIGHTS.md`, none of them built in our configuration except Montserrat (OFL 1.1, font
  data only)). Compatible with the app's GPL-3.

### 3.1 Decision: pinned copy, not a git submodule

`software/ultimate-app/software/lvgl` holds a plain copy of upstream's `src/`, `include/`,
the top headers, the licence files and `scripts/LVGLImage.py` (28 MB of text, ~1,450 files);
`software/lvgl/VERSION` records tag and commit. Why not a submodule:

1. `software/ultimate-app` is itself a submodule of TRX64-Ultimate (Gitea). A nested
   submodule must be initialised recursively in every clone, every `git worktree` (a
   worktree gets empty submodule directories; the existing nested `software/lwip` already
   shows this) and in the NAS CI container, and fetches from GitHub each time.
2. The build must not depend on GitHub being reachable (private Gitea CI).
3. An upgrade through the script is one reviewable diff in our history; a submodule bump
   hides the change behind a hash.
4. Cost: once ~6 MB packed in the repository.

Rule: no local edits inside `software/lvgl`. Configuration goes into `lv_conf.h`; a fix goes
upstream and arrives with the next pin.

### 3.2 Upgrade

`tools/vendor_lvgl.sh v9.x.y` (in `software/ultimate-app`) replaces the copy and rewrites
`VERSION`; then build, check the size (§11) and the splash on the board.

### 3.3 Build

LVGL is a static library like lwIP: `target/libs/riscv/lvgl/makefile` compiles every C file
under `software/lvgl/src` (objects keep their directory: LVGL repeats file names across
directories, the app's flat `output/` would mix them) with the app's code model (`-march=rv32im
-mabi=ilp32 -mno-div -Os -ffunction-sections -fdata-sections`) into `liblvgl.a`. The top
`Makefile` builds it after lwIP for `u64ii` / `u64ii_no_esp`; the application links it
(`LIBS`) and `--gc-sections` keeps only what is called. C++ parts (ThorVG) are never built.

## 4. `lv_conf.h` (`software/lvgl_port/lv_conf.h`)

Only options that differ from LVGL's defaults, plus every widget, theme and font switch
(defaults turn most widgets on; each phase turns on what it uses).

| Option | Value | Why |
|---|---|---|
| `LV_COLOR_FORMAT_DEFAULT` | `LV_COLOR_FORMAT_RGB565` | the layer's only format (replaces v8's `LV_COLOR_DEPTH 16`) |
| `LV_USE_STDLIB_MALLOC` | `LV_STDLIB_CLIB` | the app's wrapped `malloc` = FreeRTOS `heap_4` (`memory_wrap.cc`, `ucHeap`): a UI that shuts down gives everything back; no fixed LVGL pool that would sit idle while the UI is parked |
| `LV_USE_STDLIB_STRING`, `_SPRINTF` | `LV_STDLIB_BUILTIN` | word-wide copies; no newlib `vsnprintf` |
| `LV_USE_OS` | `LV_OS_NONE` | one UI task calls LVGL (§6); LVGL's FreeRTOS OSAL is not needed |
| `LV_USE_FLOAT`, `LV_USE_MATRIX`, `LV_USE_VECTOR_GRAPHIC`, `LV_USE_THORVG` | 0 | no FPU (soft float only); no float code paths |
| `LV_DRAW_SW_SUPPORT_*` | RGB565, RGB565A8 (images with alpha), A8 on; the rest off | the layer is RGB565; images are converted to RGB565(A8) at build time |
| `LV_DRAW_SW_COMPLEX` | 0 in phase 1 | radius, shadow, arc, line masks; on in phase 2 (a menu needs rounded rectangles) |
| `LV_USE_SVG`, `LV_USE_LODEPNG`, `_LIBPNG`, `_BMP`, `_TJPGD`, `_GIF`, `LV_USE_FS_*` | 0 | images are converted at build time (§8.2), never decoded at run time |
| `LV_USE_THEME_*`, `LV_USE_FLEX`, `LV_USE_GRID` | 0 in phase 1 | styles set by hand; phase 2 decides on a theme |
| Fonts | `LV_FONT_MONTSERRAT_48` only, `LV_FONT_DEFAULT` = it | the splash's placeholder text; phase 2 adds the menu sizes |
| Widgets | `LV_USE_LABEL`, `LV_USE_IMAGE` only | phase 1 |
| `LV_DEF_REFR_PERIOD` | 20 ms | 50 Hz output: at most one flip per frame |
| `LV_USE_LOG`, monitors, profiler | 0 | size |
| `LV_USE_ASSERT_NULL`, `_MALLOC` | 1 | cheap, catches out-of-memory |

## 5. Display driver (`software/lvgl_port/trx_lv_port.cc`)

- One `lv_display` of 960 x 540, `LV_DISPLAY_RENDER_MODE_DIRECT`, **two full buffers**
  (`lv_display_set_buffers(disp, buf0, buf1, 1036800, DIRECT)`), each 16-byte aligned, from
  `malloc`. In direct mode LVGL draws only the invalidated areas into the back buffer at their
  screen position and itself copies the areas of the last frame over from the front buffer,
  so a change costs its area, not the screen.
- `flush_cb`: on every call, `fence` and a load of the last pixel of the area (the rvlite
  has no data cache but posted stores; the load waits for them, same rule as
  `trx_video.cc`). Only on the last area of a refresh (`lv_display_flush_is_last`):
  - first frame: `PX_CTRL` off, `PX_SIZE` 540/960, `PX_STRIDE` 1920, `PX_POS` 0, `PX_GALPHA`
    255, `PX_KEY` off, `PX_FRONT` = the buffer, `PX_CTRL` = 0x23 (RGB565, 2x, on);
  - afterwards: clear `IRQ_CAUSE` b7, write `PX_BACK`, wait for the flip-done interrupt
    (binary semaphore given by the port's `trx_irq_attach(TRX_IRQ_FLIP)` handler; 100 ms
    timeout, logged), then `lv_display_flush_ready`. The flip lands at a frame start, so
    there is no tearing, and LVGL never draws into the buffer on screen.
- Tick: `lv_tick_set_cb` = `xTaskGetTickCount() · portTICK_PERIOD_MS` (200 Hz tick, 5 ms
  resolution; enough for animations at 50 Hz).
- `trx_lv_set_alpha()` writes `PX_GALPHA` (fades without redrawing). Colour key: off in
  phase 1 (the splash is opaque). A UI over the running C64 (phase 3) uses the key: one
  RGB565 value reserved as "transparent" (e.g. 0xF81F magenta, never in the theme), the
  screen background set to it.
- Vsync (`IRQ_CAUSE` b6) is not used by the driver: the flip already synchronises. T12
  enables it only while it waits for a frame start.

## 6. Task, threading, memory

- **One UI task** ("TRX UI") owns LVGL: it alone calls `lv_*` (through `trx_lv_open`,
  `trx_lv_handler` = `lv_timer_handler` under the port lock, `trx_lv_close`). Other tasks
  never call LVGL; they send requests (later phases: a FreeRTOS queue the task drains before
  each `lv_timer_handler`).
- **Priority:** `tskIDLE_PRIORITY + 1` (= `PRIO_HW_SERVICE`/`PRIO_DRIVER`/`PRIO_NETSERVICE`,
  the same as T12's "TRX Video" renderer), above the init task / `ultimate_main` (`PRIO_MAIN`
  0) and Gideon's user interface (`PRIO_USERIFACE` 0), below lwIP (`PRIO_TCPIP` 2) and the
  real-time tasks (`PRIO_REALTIME` 3: tape, LED strip, reset). So the splash is drawn as soon
  as the task exists, network and drives are never starved by drawing, and the UI sleeps
  between frames (`vTaskDelay` of what `lv_timer_handler` returns, 20-50 ms). If a later
  phase's full redraws make Gideon's menu or the C64-side UI sluggish, the task drops to 0
  (round robin with the main loop).
- **Stack:** `2 · configMINIMAL_STACK_SIZE` = 3,200 words (12.8 KB); LVGL's software
  renderer keeps its state on the heap, the deepest stack is label/image drawing.
- **Memory:** frame buffers 2 × 1,036,800 B + LVGL objects (a few KB in phase 1), all from
  `malloc` (FreeRTOS `heap_4`, 7.625 MB in this build, §11). Taken when the UI opens, given back when it closes
  (`lv_display_delete`, `lv_deinit`, `free`). The freeze menu needs 2 × 864,000 B + 92 KB
  (still) from the same heap only after the splash is gone, so the two never add up. A UI
  that stays (phase 3) must keep its buffers; then the heap is too tight next to T12 and
  the buffers move into a reserved DDR2 range (open question 2).
- **One buffer fallback:** not in phase 1 (the splash simply does not start without two
  buffers); phase 2 may add `LV_DISPLAY_RENDER_MODE_PARTIAL` with small buffers copied into
  one frame buffer if memory demands it.

## 7. One owner of the layer (coexistence with T12)

The layer's registers belong to one user at a time:

| Owner | When |
|---|---|
| LVGL (`trx_lv_port.cc`) | from `trx_lv_open` until `trx_lv_close` or until it is taken away |
| T12 freeze renderer (`trx_video.cc`) | from `trx_video_freeze_enter` to `trx_video_freeze_leave` |

Rule: **the freeze wins.** `trx_video_freeze_enter()` first calls `trx_lv_yield_layer()`
(the one hook in T12's code): under the port lock it switches the layer off and resets
`PX_GALPHA` if LVGL had it on, and marks the layer taken; from then on the UI task's flushes
and alpha writes do not touch the registers, `trx_lv_handler` returns -1 and the UI closes
itself. The call returns only after that, so T12's `show_first` never races with an LVGL
register write. T12 itself sets every register it relies on (`PX_SIZE`, `PX_STRIDE`, `PX_POS`,
`PX_GALPHA`, `PX_KEY`, `PX_FRONT`) at each freeze.

Init order: T12's init (`InitFunction` 90) writes `PX_CTRL` off; the UI's init runs after it
(91), so it cannot switch the splash off.

Later phases (a UI that stays): after `trx_video_freeze_leave` the UI takes the layer back
and redraws in full (its buffers were not touched, but `PX_*` were). This needs a
"layer free again" call from T12 to the port (phase 3).

## 8. Phase 1: boot splash (built)

### 8.1 Flow (`software/io/c64/trx_ui.cc`)

1. `InitFunction` "TRX UI" (ordering 91, after "TRX Video" 90, before `c64->start()` in
   `ultimate_main`): only if `ID` = "TRX2" and `CAPS` b6 + b7 (`trx_lv_available`); on stock
   hardware nothing happens. Creates the UI task.
2. The UI task (priority 1, so it runs at once): `trx_lv_open` (lv_init, tick, buffers,
   display), builds the splash: screen background black and opaque, a label "TRX64-Ultimate"
   in Montserrat 48, white, centred (placeholder for the logo); with a logo (§8.2) the image
   above the text and the pair centred. The first `lv_timer_handler` renders and switches
   the layer on: opaque over the whole 1920 x 1080 (global alpha 255, key off).
3. The C64 starts behind it (`c64->start()` after the remaining init functions).
4. Wait until **(2 s since the splash appeared and the C64 at BASIC READY) or 4 s**. READY
   comes from the state bus (ARCH §15.3): the UI sets `CAP_CTRL` to a capture every 5 frames
   (only if it was 0; restored afterwards), reads chip 1 word 1 (`SB_ADDR` 0x81, `SB_DATA`
   bits 15:0 = the 6510's PC at the last capture) every 20-50 ms; a PC in the KERNAL's
   keyboard wait loop $E5CD-$E5D5 (BASIC's input line after `READY.`) means READY. A capture
   only copies state into shadow flip-flops; the C64 is not touched. Without the state bus
   (`CAPS` b0/b1 missing): 3 s. A cartridge or KERNAL that never runs that loop: 4 s.
5. Fade: `PX_GALPHA` from 238 down to 0 in 15 steps of 20 ms (300 ms).
6. Close: `PX_CTRL` off, `PX_GALPHA` back to 255, display deleted, `lv_deinit`, both buffers
   freed, the task deletes itself. The layer is free for T12.

A freeze at any point (menu button during the splash): §7, the splash disappears at once
and the freeze menu shows as usual.

Log lines (`printf`): `[TRX UI] LVGL 9.6.0 on the pixel layer: buffers at …`,
`[TRX UI] C64 at READY after n ms of splash`, `[TRX UI] splash done; UI parked` (or `ended by
a freeze`).

### 8.2 The logo

- Drop in: `tools/lvgl_logo.sh <logo.png|logo.svg> [width]` (in `software/ultimate-app`).
  An SVG is rendered at `width` with `rsvg-convert` (Homebrew `librsvg`), a PNG is scaled with
  `sips` if a width is given. Then LVGL's `LVGLImage.py` (from the pinned copy) converts it
  to `software/lvgl_port/assets/trx_logo.c` (`const lv_image_dsc_t trx_logo`). Needs
  `pip3 install pypng lz4` (LVGLImage.py imports both).
- Size on screen: the layer is 960 x 540 shown at 2x; a 480-pixel-wide logo is half the
  screen width. The script refuses anything larger than 960 x 540.
- Format: RGB565 with transparent parts blended onto black (`--background 0x000000`), 2
  bytes per pixel in the binary (480 x 200 = 192,000 bytes); `LOGO_CF=RGB565A8` keeps the
  alpha (3 bytes per pixel) for a non-black background.
- The build picks the file up by itself: `target/u64ii/riscv/ultimate/Makefile` adds
  `trx_logo.c` and `-DTRX_HAVE_LOGO=1` when it exists; `trx_ui.cc` then shows the image above
  the text. Commit the generated `.c` (and the source PNG/SVG next to it): the build
  container has no pypng. Delete `trx_logo.c` to go back to text only.
- No run-time decoder, no SVG renderer (`LV_USE_SVG` 0): an SVG costs ThorVG (C++, large)
  and float maths on a CPU without FPU.

## 9. Input (phase 2)

LVGL `indev` of type keypad, fed in the UI task from the sources Gideon's menus already use:
the U64-II matrix keyboard (`Keyboard_C64` on `U64II_KEYB_ROW/COL/JOY`, read without the
C64), the USB keyboard (`system_usb_keyboard`), joystick ports through the same `Keyboard_C64`
joystick register. Keys arrive as the app's key codes (`KEY_UP`, `KEY_RETURN`, … after
Gideon's key mapper: F1→PageUp, F3→Help, F5→Tasks, F7→PageDown) and are mapped to
`LV_KEY_*`; focus by an `lv_group`. While the LVGL UI has focus, the app's own overlay and
C64-screen menus must not read the same keys: the UI task takes the keyboard the way
`ui->run_once()` does (`system_usb_keyboard.enableMatrix(false)` around it). The menu
button (`c64->buttonPush()` in `ultimate_main`) keeps opening Gideon's menu until phase 3
decides what it opens.

## 10. T26 host firmware

`docs/boot-chain-concept.md`: the host (small bitstream + RISC-V + FreeRTOS + LVGL) shows
the core selection. It uses `software/lvgl` (same pin), `software/lvgl_port/lv_conf.h` and
`trx_lv_port.cc` unchanged; the port depends only on `trx_io.h` (register access, interrupt
dispatch) and FreeRTOS, not on the C64 classes. The host bitstream therefore keeps the IO
block page 0 (`ID`, `CAPS`, `IRQ_*`) and page 6 (pixel layer) at the same addresses.

## 11. Size and cost (phase 1, measured)

Build of `t24/lvgl-splash` 69890dd0 against `trx64/main` b9c91b98 (same container, `-Os`):

| | Bytes |
|---|---|
| `ultimate.bin` before | 1,331,320 |
| `ultimate.bin` with LVGL + splash | 1,568,688 |
| **Delta** | **+237,368** (`.text` + `.rodata`; `.data` +64, `.bss` +352 without the heap change) |
| of it Montserrat 48 (`glyph_bitmap` 91,912 + tables) | ≈ 97,000 |
| of it LVGL core, label, image, SW renderer, port and splash code | ≈ 140,000 |

**The application image is full.** `.bss` holds the FreeRTOS heap (8 MB) and Gideon's static
MPS printer instance (5 MB); before LVGL only 127 KB were left between the end of `.bss`
(0xE70CD0) and the top of the memory region (0xE90000, `linker.x`), and that is the boot and
interrupt stack, not free space. With LVGL the link overflowed by 110 KB. Fix in this build:
`FreeRTOSConfig.h` now takes `configTOTAL_HEAP_SIZE` from the build if set, and
`target/u64ii/riscv/ultimate/Makefile` sets 0x7A0000 (8 MB − 384 KB). Of the 384 KB, 237 KB
are LVGL; the other ~147 KB are kept for the logo, so the stack gap above `.bss` is 283 KB
until a logo is added (0xE4ADAC-0xE90000). A logo above ~145 KB (RGB565: e.g. 480 x 150)
needs the heap lowered further. The heap left: 7.625 MB, of which the splash takes 2 MB at
boot (given back) and the freeze menu 1.8 MB from the first freeze on; whether Gideon's own
use plus T12 still fits is checked on the board (`[TRX UI] … heap free n` in the log).

Flash: the 50T app slot had ~0.63 MB left at the end; +237 KB leaves ~0.39 MB.

CPU: the splash's single full-screen render (black fill of 518,400 pixels and the label)
happens once; afterwards `lv_timer_handler` finds nothing invalid and the UI task sleeps
20-50 ms between calls until the fade. Scanout 52 MB/s while the layer is on (≤ 4.3 s).

## 12. Phases

| Phase | Content | State |
|---|---|---|
| 1 | LVGL v9.6.0 in the u64ii build, display driver (direct mode, 2 buffers, flip IRQ), owner rule with T12, boot splash with logo slot, fade, UI parked; heap −384 KB (§11) | built (`t24/lvgl-splash` 115bc64e, 69890dd0), board test open |
| 2 | Input (§9), theme and fonts, `LV_DRAW_SW_COMPLEX` 1; a first real screen: TRX status (bitstream, CAPS, ring/capture state) opened from Gideon's menu | — |
| 3 | UI over the running C64 (colour key, global alpha), stays resident: buffers in a reserved DDR2 range, layer handed back after a freeze; T20/T21 front-ends (timeline, monitor) as thin callers of their APIs | — |
| 4 | Port into the T26 host firmware (core selection) | — |

## 13. Open questions

0. **Heap vs. LVGL** (§11): LVGL now costs the FreeRTOS heap 384 KB. Alternatives for later
   phases, which need more code and fonts: drop Montserrat 48 once the logo exists (−97 KB),
   or move the image's start / the memory region (only together with the drive and cartridge
   areas at 0xEA8000+, FPGA side).
1. **Splash earlier:** it starts at init ordering 91, after the file systems, network and
   drive init functions. To come up right after the app starts, T12's init must stop writing
   `PX_CTRL` when the layer is not its own (then the UI can run at ordering 1). Worth it?
2. **Where the resident UI's buffers live** (phase 3): 2 MB more in the 8 MB `heap_4`
   next to T12's 1.8 MB, or a reserved DDR2 range (candidates: inside the app's 64 MB above
   the RAM disk; the updater area 0x03000000 is only "unused", not free).
3. **Flash budget:** the app slot on the 50T has ~0.63 MB left (docs/c64u-flash-layout.md).
   LVGL plus a logo comes out of that; a large logo at RGB565 can cost more than LVGL itself.
4. **Splash text:** "TRX64-Ultimate" is a placeholder until the logo exists. Version string
   under it?

## 14. Live C64 picture inside the UI (picture-in-picture, phase 3)

**Requirement (Alex, 2026-10-04).** The "LIVE C64" boxes in the LVGL design
(`docs/lvgl_ui_project/`, review: `T24-ui-project-review.md`) show the running C64,
scaled down, inside the UI. Today they are text placeholders, and every screen is opaque.

**Why the existing parts do not do it:**
- A colour-key hole (`PX_KEY`) in the pixel layer shows what lies underneath: the full-screen
  scaled C64 picture. In a box that is only the cut-out behind the box, not the whole C64
  frame made smaller.
- Copying the frame into an LVGL canvas by software is far too slow: 384 × 272 pixels at
  50 Hz through the app CPU, ~0.4 µs per DDR2 round trip (T41/T48).

**What it needs (hardware, `vic_scaler.v`):**
- A **window mode**: box position (x, y in output pixels) and an integer factor, the C64
  frame drawn scaled into the box; outside the box the scaler outputs the background (or
  the full-screen picture, as today, when the window mode is off).
- Integer factors only (cheap): 1× = 384 × 272 output pixels (192 × 136 in the UI's
  960 × 540 at 2×), 2× = 768 × 544 (384 × 272 in UI pixels). The 4.5× vertical step of the
  60 Hz modes (T40) is not needed in the box; 60 Hz frames are 240 visible lines (T40).
- **A frame buffer — it exists already.** The scaler's ring holds 64 VIC lines and works only
  because the full-screen output runs in lock-step with the VIC. A box shows a whole VIC frame
  in 272 (or 544) output lines at any vertical position, so the whole frame must be stored.
  T12's **still capture** does exactly that: every VIC frame into two DDR2 slots of 64 KB,
  double buffered (400 × 270 × 4 bit, 0x5050000-0x506FFFF; T12 §5.2, T20 §5.1). The window
  mode reads the last complete slot (one frame of latency) instead of the ring; no new
  block RAM for the picture, one more DDR2 reader (~2.7 MB/s at 50 Hz) through `mig_arb`.
- The pixel layer opens a key-coloured rectangle exactly over the box (`PX_KEY`).

**Rules for the design:**
- Box sizes fixed to the factors above (1× or 2× of the C64 area, UI pixels 192 × 136 or
  384 × 272, plus the border if wanted).
- The box is a plain rectangle: no text, rounding or anti-aliased edges on it or across it
  (LVGL's anti-aliasing would make fringe pixels near the key colour). A frame line around
  it is drawn outside the rectangle.
- One box at a time (one window register set).

**Timewarp preview:** the ring stores no pictures (T20 §11 "A picture per frame in the
ring": no; §11.3): a ring frame's picture is made by restore + re-running one frame, and the
still capture writes it into a still slot. The same box therefore shows a Timewarp frame
with no extra path: the window mode reads the still slot either way.

**Open:** cost (scaler window logic and the still-slot reader, LUT/BRAM), measured with the
first build; whether the reader sits in `vic_scaler` or in `pix_layer` (which already reads
DDR2).

**Resolved (2026-10-04): [T24.1-pip-box.md](T24.1-pip-box.md).** The reader sits in `pix_layer` (as
`pip_box.v`), the box is a hardware hole that follows `PX_GALPHA` (no colour-key rectangle), the
registers are `BOX_*` at 0x101A60E0-0FC, the source area is the 400 x 270 still (not 384 x 272) and
the box size is the same in 50 and 60 Hz. The register table, sizes, cost estimate, tests and the
emulator notes are there; where it differs from the text above, T24.1 wins (its §12).

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

T24: Own LVGL UI and boot

Milestone M2, status ⏸. Open (one line, as in the TODO table): Not built (UI drafts in docs/lvgl_ui_project, docs/lvgl_ui_terminal)

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- **2026-10-10 evening: splash logo at half size** (Alex: "viel zu gross, 50% verkleinern und den schwarzen Rand grösser"): logo 600 x 184 -> 300 x 92 (Lanczos, mapped back to the 3 colours), centred on the 960 x 540 black layer. After RC1.
- **2026-10-10: boot splash = Alex's TRX64 Ultimate picture** (`TRX64.PNG`, 960 x 540, 3 colours, 6,310 bytes of runs; tools/splash_rle.py), for the M1 RC (Alex: "mit in den RC").
- **2026-10-08: new boot splash picture (Alex's TREX64 Ultimate image).** Regenerated with tools/splash_rle.py from the 960 x 540 PNG (3 colours; 5,266 bytes of runs, was 71,157); same format and draw path, no scaling needed. Source PNG: software/lvgl_port/assets/trx_splash.png in the app repo (branch `splash-app`). Board test open.
- **2026-10-06: second UI draft by Codex, terminal style, in docs/lvgl_ui_terminal (beside docs/lvgl_ui_project).** Own LVGL UI and boot (docs/specs/T24-lvgl-ui.md); hand the pixel-layer registers (T12 §6.4) to the C64U_emu session so ue2emu can model them. Wiring to every function of Gideon's UI: docs/specs/T24-ui-wiring.md; what the UI must reach, checked against the code: docs/gideon-menu-tree.md §13 Live C64 in the UI boxes (Alex 2026-10-04): picture-in-picture window mode in vic_scaler + DDR2 frame buffer + key-coloured rectangle in the pixel layer (spec T24-lvgl-ui.md §14).
