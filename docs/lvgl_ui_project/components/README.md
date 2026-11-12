# Components

The component layer keeps the 14 screens visually consistent and gives the
firmware integration a small, stable surface.

| Component | Role |
| --- | --- |
| `base_box`, `box`, `row`, `column` | layout primitives with no behavior |
| `panel` | flat or raised graphite surface |
| `app_shell` | top status bar, F-key area rail, numbered local tabs, content slot, key legend |
| `nav_item` | persistent navigation target and active marker |
| `tab_item` | numbered tab target inside the selected F-key area |
| `ui_button` | compact focusable action |
| `action_row` | larger title/subtitle/meta action used in lists |
| `status_chip` | concise state badge; color communicates state, not decoration |
| `stat_card` | glanceable value with label and supporting detail |
| `setting_row` | setting name, explanation and a dedicated control slot |
| `ui_switch`, `ui_slider`, `ui_bar` | subject-bound controls and progress |

Palette, typography, spacing, focus and pressed styles live in `../globals.xml`.
Screens should compose these components instead of carrying one-off visual
rules. This is also the boundary for future keypad accessibility and adapter
bindings.

LVGL Pro component syntax: https://lvgl.io/docs/pro/syntax/components
