# TRX64 M2 UI Prototype

This is the executable **LVGL Pro XML prototype** for the TRX64 M2 user
interface. It turns the information architecture from
`../gideon-menu-tree.md` into real 960 x 540 screens while deliberately keeping
hardware and firmware behavior out of the presentation layer.

The visual language follows <https://trex64-dev.github.io>: near-black and
graphite surfaces, restrained typography, and violet/cyan/amber/coral used only
for focus and state.

The layout is intentionally sparse: one dominant task per screen, flat lists
instead of card grids, an 11–21 px type scale, and secondary actions behind
`F7`. Empty space is part of the hierarchy and is not treated as unused room.

## What's inside

- `project.xml`: the 960 x 540 M2 preview target and generated project name
- `globals.xml`: palette, typography, spacing, preview subjects, and shared styles
- `components/`: the shell, navigation, cards, rows, controls, and status patterns
- `screens/`: 38 navigable UX screens, including catalogue templates,
  warning/confirmation flows, and the eight-screen TREX CRT app flow;
  see [`screens/README.md`](screens/README.md) for the route map
- `images/`, `fonts/`: Assets, converted and compiled in by the Editor
- `sim/`: The built-in simulator, see [`sim/README.md`](sim/README.md)

## Preview

Open this folder in LVGL Pro Editor, start with `screens/home.xml`, and use the
buttons or keypad focus to navigate. **Ctrl+B** exports the XML as C; **F5** runs
the desktop simulator after exported sources exist.

The [UI screenshot tour](UI-SCREEN-TOUR.md) shows the 14 core visual states.
The full functional mapping and the dynamic catalogue rules are in
[`UI-COVERAGE.md`](UI-COVERAGE.md).

An independent, deliberately narrow 80-column interpretation of the 30 base
screens lives in
[`../lvgl_ui_terminal`](../lvgl_ui_terminal/README.md).

The XML preview uses LVGL 9.5 because that is the version bundled with the
current editor. Firmware integration targets the vendored LVGL version and is a
separate compatibility step. No screen calls firmware actions yet.

## Interaction contract

- The left rail maps the directly printed keys to F1 Home, F3 Library, F5 C64,
  and F7 Actions. The shifted logical keys map to F2 Timewarp, F4 Tools, F6
  Settings, and F8 System.
- Numbered tabs across the top select pages inside the current area. This makes
  routes short and memorable, for example `F4, 2` for Monitor.
- Cursor keys and Return remain a complete navigation path. The C64 left-arrow
  goes back and exits at the root; `R/S` closes immediately; Freeze/Menu toggles
  the UI.
- Area and tab assignments are stable on every screen. Number shortcuts are
  temporarily suspended while a text-like editor has focus.
- Current state is shown before actions.
- Destructive or persistence-affecting actions get a dedicated confirmation screen.
- One foreground command is allowed at a time; its state is always visible in the top bar.
- Settings change live when the user leaves a page. `SAVE TO FLASH` persists them;
  `RESET FROM FLASH` restores the stored values. There is deliberately no fictional
  Apply/Discard transaction.
- `globals.xml` subjects are demo data until the adapters in `T24-ui-wiring.md` (TRX64-Ultimate)
  bind them to firmware state.
- Parameter and action inventories are not duplicated in XML. Runtime rows are
  created from `ConfigManager`, `ConfigGroupCollection`, `TasksCollection`,
  file-type context actions, and the app registry as specified in
  [`UI-COVERAGE.md`](UI-COVERAGE.md).

The complete key and tab matrix, including the automation contract, is in
[`NAVIGATION.md`](NAVIGATION.md).

## Learn more

Check out the docs at **<https://lvgl.io/docs/pro>**. These are the key pages to learn XML:

- [XML overview](https://lvgl.io/docs/pro/syntax/overview): Basics of the syntax
- [Components](https://lvgl.io/docs/pro/syntax/components), [Widgets](https://lvgl.io/docs/pro/syntax/widgets) and [Screens](https://lvgl.io/docs/pro/syntax/screens): The basic building blocks of a UI
- [Built-in widgets](https://lvgl.io/docs/pro/built_in_widgets): the XML API of `lv_obj`, `lv_button`, `lv_slider`, etc, and all the style properties
- [Styles](https://lvgl.io/docs/pro/syntax/styles) and [Constants](https://lvgl.io/docs/pro/syntax/constants): To make UIs more maintainable
- [Data binding](https://lvgl.io/docs/pro/syntax/data-binding): Connect the UI to your application data
- [Testing](https://lvgl.io/docs/pro/syntax/testing) and the [CLI](https://lvgl.io/docs/pro/cli): To validate your work automatically
- [AI integration](https://lvgl.io/docs/pro/ai): For agents to learn how to work effectively with LVGL Pro and the XML

The [**lvgl/lvgl_pro**](https://github.com/lvgl/lvgl_pro) repository is also worth checking. It contains examples, tutorials, and
[`lvgl_widgets_xml/`](https://github.com/lvgl/lvgl_pro/tree/master/lvgl_widgets_xml) with the XML schema of LVGL's built-in widgets.
Both humans and agents can check exactly which properties, enums, styles, and other elements are available. See
[lv_slider.xml](https://github.com/lvgl/lvgl_pro/blob/master/lvgl_widgets_xml/v9.5.0/lv_slider.xml) as an example.
AI agents can clone the whole repo or get only one file.
