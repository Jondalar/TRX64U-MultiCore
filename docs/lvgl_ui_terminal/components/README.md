# Terminal components

These components preserve the primary prototype's API while replacing its
visual grammar:

| Component | Terminal rendering |
| --- | --- |
| `app_shell` | narrow vertical F-key area rail beside an 80-column frame and numbered tabs |
| `nav_item` | compact `[Fn]LABEL` entry with inverse active state |
| `tab_item` | compact `[n]LABEL` tab with inverse active state |
| `action_row` | `>` prompt, title, optional detail and `[META]` |
| `ui_button` | bracketed text action |
| `status_chip` | bracketed state text |
| `stat_card` | label/value text pair |
| `setting_row` | one compact text row plus control |
| `ui_switch` | ASCII on/off state over the bound switch |
| `ui_slider`, `ui_bar` | ASCII scale and progress representations |
| `keycap`, `return_keycap`, `runstop_keycap`, `freeze_button` | text key labels |

`base_box`, `box`, `row` and `column` remain behavior-free layout primitives.
No component uses icon fonts or bitmap assets.
