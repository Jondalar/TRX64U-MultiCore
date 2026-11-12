# T24 — Review of the LVGL UI project (`docs/lvgl_ui_project/`)

**Status:** review 2026-10-04, read-only analysis; nothing built, nothing measured on the board.
**Task:** TODO.md T24 (M2), T32, T36, T48, T54, T55. **Reviewed:** the XML project as committed (30 screens +
README/UI-COVERAGE/UI-SCREEN-TOUR), against `T24-lvgl-ui.md` (T24), `T24-ui-wiring.md` (WIR),
`gideon-menu-tree.md` (GMT), `T12-freeze-overlay.md` (T12), `pix_layer.v`, `overlay.v`.
Paths `P/...` = `docs/lvgl_ui_project/...`. CPU-time numbers below are **estimates from the 0.4 us read
latency (`T20-delta-ring.md:310`, `TODO.md` T41), not measurements**; T48 itself says "measure LVGL frame
times first" (TODO.md T48, details in docs/todo/T48.md).

**One sentence:** the project is a clean, flat, text-only design that fits the pixel layer well (no images,
gradients or shadows), but it is a navigation prototype: dialogs, progress, text input, the update path and
several TRX64 features have no screen, every screen is opaque (no live C64), 82 whole-screen fade transitions
would be the most expensive thing on the CPU, and the FPGA text layer can go only after four named
preconditions (§3).

---

## 1. Coverage

Legend: **full** = a screen designed for it; **template** = covered by a runtime-filled container the XML
only sketches (`P/UI-COVERAGE.md:20-22,88-98`); **partial** = screen exists, content or states missing;
**none** = no screen.

### 1.1 TRX64 features

| Feature | Screen(s) | State | What is missing |
|---|---|---|---|
| Timewarp / T20 (scrub, resume) | `timewarp.xml`, `timewarp_actions.xml` | partial | preview is text (`timewarp.xml:28-43`); slider is a 0-100 % subject (`globals.xml:38`), not ring frames/time; no ring depth/size, no mark ticks on the slider; WIR §6.3 rule 4 says the T12 Timewarp bar owns the layer during scrub, the XML assumes LVGL does |
| Timewarp extras / T54 (single frame, play, marks F3/F5, pin) | -1/+1 FRAME, PLAY PREVIEW (`timewarp.xml:46-48`), "Add marker" (`timewarp_actions.xml:18`) | partial | no mark list, no pinning, F3/F5 clash with the shell's F3 = Library (`app_shell.xml:38`) |
| Save Timewarp Ring (**removed from M1, A13; T66**) | `timewarp_actions.xml:24` row | partial | row only: no target folder (WIR G4), no progress screen for a multi-MB `.c64rering` write, no done/failed state |
| Snapshot save/load (`.c64re`) (**removed from M1, A13; T66**) | `timewarp_actions.xml:22,26` | partial | load needs the new file type (WIR G7) and a confirm screen (the footnote at `:33` promises it, no screen exists) |
| Monitor / T21 | `monitor.xml` | partial | static labels (`:20-38`) instead of the terminal widget (WIR §4.2); CONTINUE/STEP/STEP OVER only; T55 verbs (observers, `io 1541`, flow) not reachable except by typing |
| Lenses (cpu/ram/rom/io/cart/drive8, `T21-monitor.md` §3.4) | none | none | no lens selector, no "which view am I looking at" indicator; F7 "quick views" (`monitor.xml:47`) is a label |
| Cartridge write-back / T16 | `writeback_warning.xml`, switch `settings_trx64.xml:24-29` | partial | setting is a boolean switch, the app item is Off/Ask/Auto (GMT §13.6 line 892; commit 32f4b92a "Auto without the menu"); popup is worded as eject, Gideon's fires at menu enter (GMT §13.4, `c64_subsys.cc:244-280`); no progress and no verify-failed screen for the whole-chip write |
| Turbo / T29 | switch `settings_trx64.xml:42-45`; row `settings_advanced.xml:31` | partial | T29 has four items (Turbo Control 0-3, CPU Speed 0-15, Badline Timing, SuperCPU, `T29-turbo.md:60`); the design shows one 0/1 subject (`globals.xml:42`) |
| NTSC / T40 (PAL, NTSC, PAL-60, NTSC-50, live) | static text "PAL 50" (`settings_video.xml:38-43`), "PAL / 50.12 Hz" (`c64.xml:51`) | none | no mode selector; the live switch (picture back after ~1 s, `T40-ntsc.md:389`) needs a "switching..." state |
| Update / T49-T50 | button `system.xml:48` "CHECK FOR UPDATE" | none | the updater is a standalone program (`T49` §4.1: own `setup()` UI); the project has no updater screens, no "(unlocked)" indicator (`T50:202`), no FAILED/do-not-switch-off state |
| Splash / boot | `settings_trx64.xml:36` label only | partial | phase-1 splash exists in code (T24 §8); no settings control behind the label |
| Freeze menu / T12 | none | none | the freeze menu text is Gideon's C64-screen UI on `c64_text.v` (T12 §13); LVGL replaces it, but the freeze backdrop (still) is not designed, see §2.4 |
| Activity indicator (WIR Q6) | `app_shell.xml:22-27`, `subject_activity` | full (top bar) | no screen for the history ("Activity history stays behind F7", `UI-SCREEN-TOUR.md:107`) |
| Dialogs, choice, text input, progress, toast (WIR §6.4) | `media_eject.xml`, `writeback_warning.xml` only; `ui_bar` exists but is used on no screen | none | the `LvglUserInterface` modals the 227 popups / 39 string boxes / 25 progress calls need (WIR §3) |
| Error / fatal message | none | none | see §3 case 7 |

### 1.2 Gideon's functions (`P/UI-COVERAGE.md:102-135` maps all of GMT §13)

Every group has a named destination, so there is no function without a place. But only the stable shell and
~30 screens are drawn; the large groups are templates: settings pages (`settings_page.xml`), file-type action
sheet (`library_actions.xml` with demo rows), complete action list (`action_catalog.xml`), terminal bridge.

| GMT group | State |
|---|---|
| Menu open/close, navigation | full (`app_shell.xml`) |
| Library: sources, search, recent/favourites, manage, create | full for structure (`library*.xml`), runtime for rows |
| File-type actions (PRG, D64..., CRT, TAP, SID, REU, CFG, U2A...) | template |
| C64 machine, drives A/B, peripherals, save state | full structure (`c64*.xml`) |
| Settings (all stores, groups, item kinds) | template + 7 curated pages |
| Visual SID address editor, forms, Assembly64 forms, UltiCopy window, path picker | none, terminal bridge (WIR §4.2); the path picker has no screen though `library_manage` routes to it (`screens/README.md:15`) |
| Wi-Fi AP list/connect, password | `settings_network.xml` list; password entry has no screen |
| Help / key reference | one action row (`settings_controllers.xml:30`) |
| Cartridge-changed popup | `writeback_warning.xml` (wording differs, see 1.1) |
| Boot hotkeys, C= hotkeys, long-press disk swap, USB F10 | firmware, not UI (`UI-COVERAGE.md:173-179`) |

---

## 2. Feasibility on our hardware

### 2.1 What the designed screens need vs what the layer gives

| Need (source) | `pix_layer.v` / T24 | Verdict |
|---|---|---|
| 960 x 540, scale 2 (`project.xml:4`) | `PX_SIZE` w <= 960, S 1-3 (`pix_layer.v` header, T24 §1) | matches |
| Colour: RGB888 palette (`globals.xml:22-31`) | RGB565 only, widened by bit replication | OK: flat fills only. Darkest surfaces collapse to 5/6-bit steps (e.g. `0x070A12` -> 0,8,16; `0x0D1220` -> 8,16,32): still distinct, no gradients so no banding |
| Per-pixel alpha | none; key + global alpha only | design uses none (all `style_bg_opa` 0/100 %, one `style_opa="82%"`, see 2.3); `LV_DRAW_SW_SUPPORT_RGB565A8` is not needed for the UI |
| Fonts: Liberation Sans, 5 sizes 11-21 px, 4 bpp, ASCII + 3 arrows (`globals.xml:54-70`) | software text | OK. Flash: ~60-80 KB estimate for the five bitmaps; T24 §11 left 0.39 MB in the 50T app slot after LVGL; dropping Montserrat 48 gives back ~97 KB (T24 §13 Q0). The heap/stack gap (283 KB, T24 §11) must be re-checked |
| Images / icons | none (`globals.xml:46-48`) | no cost; only the splash logo |
| Animations | 82 `screen_create_event ... fade_in 120 ms` (grep over `screens/`, `components/`) | **too heavy**, see 2.3 |
| Live C64 behind the UI ("running C64 is the dominant surface", `UI-SCREEN-TOUR.md:33`) | key colour (exact RGB565 match) or global alpha | **not designed**: `style_screen` is opaque (`globals.xml:74-77`), "LIVE C64" is a text box (`home.xml:25-33`), the preview panel of Video is a text box (`settings_video.xml:47-61`) |
| LVGL version | pin 9.6.0 (T24 §3) | project is authored for 9.5.0 (`project.xml:1`, `P/README.md:37`): XML export must be re-checked against 9.6 |
| Flex, observer/subjects, slider, switch, bar, label, button, msgbox, textarea, `LV_DRAW_SW_COMPLEX` (radius, outline) | phase-1 `lv_conf.h` has label and image only, flex/grid/complex off (T24 §4) | all must be switched on (outline focus ring `globals.xml:97`, radius 20 on slider/bar `ui_slider.xml:11`) |

### 2.2 Memory

- Two 960 x 540 RGB565 buffers = 2,073,600 B (T24 §5). T24 §6: heap 7.625 MB; the freeze renderer takes
  1.8 MB (2 x 864,000 + 92 KB). A resident UI therefore holds 2.07 MB + 1.8 MB = ~3.9 MB of 7.625 MB before
  Gideon's own use, which is unmeasured (T24 §11). T24 §13 Q2 (reserved DDR2 range for the resident UI) is
  required for M2, not optional; Q2 decision "both, selectable" (WIR §10 Q2) means the buffers stay resident.
- Object heap: the design has ~6 objects per list row (`action_row.xml`) and ~40 in the shell per screen instance. Library directories (hundreds of files) and settings pages (30-60 items) must be built lazily
  (visible window of ~20 rows), never one object set per entry.
- Freeze backdrop (the C64 picture while the machine is paused) is one more 864,000 B image if it lives in
  the LVGL buffers (2.4).

### 2.3 CPU load and simplifications

Facts: no data cache, loads ~0.4 us each (T41), stores posted, rvlite ~100 MHz (TODO.md T41 and T48, details in docs/todo/T41.md, docs/todo/T48.md). A full
screen is 518,400 px = 259,200 32-bit accesses. Estimate: a pure fill is stores only (tens of ms), anything
that **reads** the destination (blend, AA text, direct-mode copy from the front buffer, T24 §5) costs
~0.4 us per 2 pixels: ~100 ms for a full-screen blend.

| Design element | Why it is heavy | Simplification |
|---|---|---|
| 82 x `fade_in 120 ms` screen loads | each animation step blends two full screens (read + write, > 100 ms per step): the fade collapses to one or two visible steps and stalls the UI task | `anim_type=none` everywhere; a visual cue by `PX_GALPHA` dip (register write, no redraw; the splash already does this, T24 §8.1 step 5) |
| Shell rebuilt per screen (`app_shell` is instantiated in every screen, `home.xml:4`) | every navigation invalidates the whole 960 x 540 incl. static sidebar/top/footer | one resident shell, swap only the `content` slot (770 x 476, 71 % of the screen); sidebar/topbar redraw only on state change |
| Scroll animation of long lists | each scroll step invalidates the whole list (~600 x 400 = 240k px) | row-snap or page jumps, no scroll animation, no elastic scroll |
| AA text over opaque backgrounds | read-modify-write per glyph pixel | keep (it is the design's whole look); avoid full-screen label updates: update labels only on change (subject binding does) and never bind a 50 Hz value to a label |
| `style_opa="82%"` panel (`settings_video.xml:55`) | blend of a 250 x ~90 area, small | replace by a pre-mixed solid colour (it is a demo of the opacity setting) |
| Monitor as one label per text row (`monitor.xml:20-38`) | 40 x 25 labels with AA and a full relayout per step | the terminal widget (WIR §4.2) as one custom draw: opaque cell background, 1-bit glyph, no AA, 1000 cells x 64 px, no destination reads |
| Timewarp scrub redraw | a 400 x 270 preview at 2x = 432,000 px per step | do not decode through LVGL: reuse T12's still renderer (T12 §5.2: 4-bit slot -> 2 x 2 RGB565, once per freeze) as the image source, scrub steps rate-limited |
| Slider/switch/bar, outline focus, radius 2-4, 1 px borders | small areas | fine; keep shadows/gradients/arcs out (already are: `style_shadow_width="0"`) |

LVGL runs at `LV_DEF_REFR_PERIOD` 20 ms and one flip per frame (T24 §4); with the simplifications a
navigation step is one content-slot redraw. Whether that is below ~100 ms on the rvlite is exactly what T48
step 1 must measure on the real shell before any hardware engine (COPY/FILL on the T20 mover) is built.

### 2.4 Backdrop: the unspecified half of the design

1. **Running (default, WIR Q2):** the picture comes from the scaler; LVGL needs a hole. `PX_KEY` is an exact
   RGB565 match, LVGL anti-aliases against it: text, rounded corners or outlines touching the key colour leave
   non-key fringe pixels on the C64 picture. Rule: the hole is a plain rectangle (radius 0), no text or AA
   edge on key pixels, text only on opaque panels. Alternative for all other screens: opaque shell with
   `PX_GALPHA` = the "Overlay opacity" setting (`settings_video.xml:19-25`; Gideon's Overlay Transparency
   0/25/50/75 %, GMT §13.6). Joystick/keys over a running game: see §4.
2. **Frozen (Freeze mode, Alex):** the scaler has no live picture while the machine is paused; T12 draws the
   dimmed still into the pixel layer (T12 §5.2, ARCH §7.5). With LVGL owning the layer (T24 §7) the still must
   be an image/background inside LVGL's buffers (864,000 B at 800 x 540, drawn once per freeze).
3. T24 §7 "layer free again" call from T12 is a prerequisite for both (not built, T24 §7 last paragraph).

---

## 3. Does anything still need the FPGA text layer (`overlay.v`)?

`overlay.v` = Gideon's char generator: 4 K screen RAM + 4 K colour RAM + HDMI palette, drawn at 1080p;
393 LUT / 3 BRAM (as given; not re-measured). Its output is **below** the pixel layer (`pix_layer.v` header:
C64 -> overlay -> pixel layer -> c64_text).

| # | Case | Needs overlay.v? | Note |
|---|---|---|---|
| 1 | Boot splash | no | pixel layer, phase 1 built (T24 §8) |
| 2 | Boot/progress messages before the UI exists | no | bootrom prints to the console UART only (`software/bootrom/bootrom.c:23`); app init uses `printf`. Nothing is on screen before the splash today (init order 91, T24 §13 Q1) |
| 3 | Popups, progress, toasts, string boxes | no | LVGL msgbox/bar via `LvglUserInterface` (WIR §4.1, §6.4); screens still to be drawn (§1.1) |
| 4 | Freeze-menu texts (T12) | no | Gideon's freeze UI draws on the C64 screen RAM through `c64_text.v` (T12 §13), not `overlay.v`; with LVGL replacing the UI, `c64_text.v` (~210 LUT + 32 LUT RAM, T12 §13) is unused too: second removal candidate |
| 5 | Monitor text (`T21-monitor.md:77` "Overlay = monitor text over the HDMI picture") | no | terminal widget in LVGL (WIR §4.2); the open question of an 80 x 50 mode (`T21-monitor.md:1189`) disappears (widget size is free) |
| 6 | Update / flash progress (T49) | **yes, today** | `update_trx` is a standalone program with Gideon's `setup()` UI (`T49` §4.1), i.e. the char screen on `overlay.v`. It runs without the main app, so no LVGL. Needs its own text path onto the pixel layer (fixed 8 x 8 font from `chargen.h` into one RGB565 buffer, no malloc, ~1 KB), or linking LVGL into the updater (+~237 KB, T24 §11; image size of the updater not checked) |
| 7 | Error / GURU / crash message | no existing implementation (no GURU code in the repo) | needs a last-resort path that does not depend on the LVGL task, the heap or a flip: the same tiny static-buffer 8 x 8 renderer as case 6, written straight to `PX_*`. Same piece serves both |
| 8 | Unlock indicator "(unlocked)" (`T50:202`) | no | LVGL status bar reads register 0x17 b0; the red frame and LEDs are hardware-only |
| 9 | **Keyboard ownership** | **yes, today** | `overlay.v` reg 0x0D b6/b7 -> `own_keyboard` (`overlay.v:92`) -> `ovl_own` -> `app_input.v:29` zeroes the physical keyboard towards the C64 (`top.v:1081,1383`). LVGL over a running C64 needs the same bit: move it into the TRX IO block (a 1-bit register, a few LUT) |
| 10 | Gideon code that touches the overlay | yes, until removed | the `Overlay` object is created unconditionally on U64 = 2 (`ultimate-app-hw-spec.md` ~line 254); WIR §7 stops creating the overlay `UserInterface`, but the class, `Overlay::checkButton` (button register, not `overlay.v`) and the interface-type item must be separated first; the Overlay Transparency item becomes `PX_GALPHA` |

**Verdict:** the designed UI itself needs none of it. `overlay.v` can be removed in M2 once (a) the ownership
bit (case 9) lives in the IO block, (b) the updater and the crash path (cases 6, 7) have the small
pixel-layer text renderer, (c) the app no longer instantiates `Overlay`/the overlay `UserInterface`
(case 10), (d) the Gideon-side telnet/`socket_gui` UI is confirmed independent of it (WIR §7: still open).
Without HDMI there is nothing to show either way (T35).

---

## 4. Input: design vs WIR

| Topic | Design | WIR / app | Finding |
|---|---|---|---|
| Group / keypad | document order, "focus follows XML" (`screens/README.md:47`); no `lv_group`, no key handler in any XML | keypad `lv_indev` over `overlay->getKeyboard()` (WIR §6.2:216-221), `lv_group` (T24 §9) | consistent in intent; the XML defines no keys at all: F-keys are labels (`nav_item.xml:23`, `app_shell.xml:82-89`), navigation works by click events |
| 2D layout | sidebar + content, cursor keys `↕ ↔` (`app_shell.xml:74-75`) | LVGL group default: up/left = prev, down/right = next | `↔` cannot cross sidebar/content with one group. Needs a group per region (nav, content) and `↔` switching `lv_indev_set_group`, or a custom key handler |
| F-keys | F1 Home, F3 Library, F5 C64, F7 actions, F2/F4/F6 (`screens/README.md:52-60`) | native screens take raw codes, not Gideon's style-dependent mapping (WIR :227); legacy terminal objects get `keymapper` (F1 -> PageUp, F3 -> Help, F5 -> Tasks, F7 -> PageDown; GMT §4.6) | no conflict for native screens. **Conflict inside the terminal widget** (monitor, forms, Assembly64, SID editor): the shell's global F1/F3/F5/F7 would steal keys the legacy object needs as PageUp/PageDown. Rule needed: terminal focus passes F-keys through the keymapper, shell F-keys suppressed there |
| RUN/STOP, `<-` | R/S closes the UI from every screen, `←` goes back (`screens/README.md:62`) | Gideon menus treat RUN/STOP, `<-`, F8, Esc as menu keys (GMT:759, `tree_browser.cc:453-462`); legacy objects (monitor) use them | native screens fine; in the terminal R/S must not close the UI if the object uses it. Two keys with two meanings is also one more thing to learn than Gideon's one |
| Menu button, USB F10/Scroll Lock | button toggles (`P/README.md:47`) | dispatcher in `ultimate_main`, REST `push_active_menu_button` (WIR :213, :261) | consistent; the open-menu count (`set_available`, WIR §9 G1) must be raised while LVGL is open |
| Joystick | not drawn (footer shows C64 keycaps only, `app_shell.xml:74-93`) | joystick via `Keyboard_C64` register (T24 §9) | fire = select, directions = move, needs a legend entry. Hardware: only the **keyboard** is masked from the C64 (`app_input.v:29`), joysticks are not (`:30-31`): in the default non-freezing mode a game gets the joystick moves of menu navigation. Needs a joystick mask bit beside the keyboard one (or "menu pauses input to the core") |
| Sliders, switches, numeric steppers | focus styles present (`ui_slider.xml:15-16`) | `lv_group` edit mode | the designed keys do not say how a focused slider changes (ENTER toggles edit mode, then `↔`, or direct `↔`). Decide once for all rows |
| Text entry (Wi-Fi password, search, rename) | no screen | `string_box` returns length (WIR §4.1) | C64 keyboard works as char input to `lv_textarea` (PETSCII -> ASCII needed); a joystick-only user needs an on-screen keyboard or Gideon's per-character entry |
| Mouse | none | decided no (WIR Q9) | consistent |

---

## 5. Gaps and what M2 implementation needs (ordered, no code)

1. **Measure first (T48 step 1):** export the XML to C, run it on the board with the phase-1 port, time a
   full content redraw, a full-screen blend and a 25-row text page. Decides whether a draw-engine unit is
   needed at all.
2. **Cut the design to what the CPU can do:** no screen transitions, one resident shell with a swapped content
   slot, no scroll animation, lazy rows, pre-mixed colours (§2.3). Re-export for LVGL 9.6.
3. **`lv_conf.h` phase 2:** flex, observer, slider/switch/bar/msgbox/textarea, `LV_DRAW_SW_COMPLEX`, the five
   Liberation sizes, drop Montserrat 48; re-check flash (0.39 MB) and the 283 KB stack gap (T24 §11).
4. **Layer ownership (T24 §7, §13 Q2):** reserved DDR2 range for resident buffers; the "layer free again"
   call from T12; backdrop modes: key hole for running, still image for frozen, `PX_GALPHA` = opacity setting.
5. **Input:** keypad indev, region groups with `↔` switching, F-key router with the terminal-focus rule,
   R/S/`←` semantics, joystick legend and joystick mask bit, `set_available` count (WIR G1), slider edit mode.
6. **Modal layer (`LvglUserInterface`, WIR §4.1, §6.4):** message/choice/string/progress/toast screens and the
   activity history; these are the missing screens that every action depends on.
7. **Terminal widget (WIR §4.2)** as one direct-pixel draw; monitor screen on it, with lens selector.
8. **Missing/partial TRX64 screens:** write-back Off/Ask/Auto + progress/failed, turbo (4 items), NTSC mode
   selector, ring depth/size and marks, `.c64re` load confirm, TRX64 settings store (T32/WIR G9, G11).
9. **Adapters and the coverage test** (WIR §4, `P/UI-COVERAGE.md:187-201`), `TRX Cmd` task (WIR §6.1).
10. **Remove `overlay.v` (and `c64_text.v`):** IO-block bit for keyboard (and joystick) ownership; small
    pixel-layer text renderer for updater and crash path; stop instantiating `Overlay`; then the bitstream
    change (LUT/BRAM saved: 393 LUT / 3 BRAM as given, plus ~210 LUT for `c64_text.v`).
11. **Updater screens (T49/T50):** drawn with the small renderer of step 10 or LVGL in the updater; indicator
    for the unlock in the LVGL status bar.

Open questions: the Gideon telnet UI without `overlay.v` (WIR §7); whether the scaler picture follows a
Timewarp scrub while paused or the still needs REGEN each step (T12 §5.2, T20); the LVGL 9.5 -> 9.6 XML export
compatibility; what "no way back to Gideon's UI" means for a machine where LVGL cannot start (WIR §7 failure
handling: the crash/last-resort renderer of case 7 is then the only visible message).
