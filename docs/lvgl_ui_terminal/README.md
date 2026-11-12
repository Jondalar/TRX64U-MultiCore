# TRX64 M2 terminal UI

This is the deliberately minimal companion to
[`../lvgl_ui_project`](../lvgl_ui_project/README.md). It exposes the same 30
screens and the same runtime catalogues, but renders them as a narrow,
text-first terminal instead of a graphical dashboard.

## Visual contract

- 960 x 540 output with a centered 772 px frame: a narrow F-key rail beside a
  640 px, approximately 80-column content area
- pure black background
- IBM Plex Mono in 10, 12 and 14 px sizes
- phosphor green for content and focus, quiet grey for secondary information
- ASCII-only labels, dividers, key legends and control representations
- no icons, images, rounded cards, shadows or decorative color
- inverse-video keyboard focus and compact one-line rows

The design rules and example grammar are documented in
[`TERMINAL-UI.md`](TERMINAL-UI.md).

The rendered user journey is available in
[`UI-SCREEN-TOUR.md`](UI-SCREEN-TOUR.md), including 14 actual LVGL preview
screenshots and a compact overview sheet.

## Structure

- `project.xml`: independent LVGL Pro project named `trx64_ui_terminal`
- `globals.xml`: 80-column geometry, two-color palette, compact type scale and
  preview subjects
- `components/`: terminal shell, text rows and ASCII controls
- `screens/`: the same 30 routes as the primary prototype
- `fonts/`: IBM Plex Mono plus its SIL Open Font License
- `sim/`: copied simulator scaffold for independent export and testing

Open this directory as its own LVGL Pro project and start with
`screens/home.xml`. It does not replace or modify the graphical prototype.

## Interaction

The left rail assigns the direct keys to F1 Home, F3 Library, F5 C64 and F7
Actions, and the shifted logical keys to F2 Timewarp, F4 Tools, F6 Settings and
F8 System. Numbered tabs select pages inside that area, while cursor keys and
Return remain a complete alternative. `[<-]` goes back and exits at the root,
`[R/S]` closes immediately, and `[FRZ]` toggles the UI.

Both prototypes share the full matrix and automation rules in
[`../lvgl_ui_project/NAVIGATION.md`](../lvgl_ui_project/NAVIGATION.md).

## Scope

This is still a presentation prototype. Step 2 binds both variants to the same
adapters described in [`../specs/T24-ui-wiring.md`](../specs/T24-ui-wiring.md).
The authoritative menu coverage remains in the primary prototype's
[`UI-COVERAGE.md`](../lvgl_ui_project/UI-COVERAGE.md).
