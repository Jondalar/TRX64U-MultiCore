# TRX64 M2 navigation contract

Both UI presentations use the same two-dimensional keyboard grammar. Function
keys select a stable **area**; number keys select a visible **tab** inside that
area. Cursor keys and Return remain a complete alternative for every route.

## Area keys

The four directly printed C64 function keys carry the frequent destinations.
Their shifted partners carry the less frequent destinations; the UI labels the
resulting logical key (`F2`, `F4`, `F6`, `F8`) rather than spelling out Shift.

| Direct key | Area | Shifted key | Area |
| --- | --- | --- | --- |
| F1 | Home | F2 | Timewarp |
| F3 | Library | F4 | Tools |
| F5 | C64 | F6 | Settings |
| F7 | Actions | F8 | System |

## Numbered tabs

Only tabs visible in the selected area consume number keys. Their number is
printed next to the tab name, so a route can be learned as a short stable
sequence.

| Area | Numbered tabs |
| --- | --- |
| Home | `1` Home |
| Library | `1` Browse, `2` Search, `3` Manage |
| C64 | `1` Summary, `2` Machine, `3` Drives, `4` I/O |
| Actions | `1` Quick, `2` C64, `3` Timewarp, `4` Library |
| Timewarp | `1` Timeline |
| Tools | `1` Overview, `2` Monitor, `3` Diagnostics, `4` Apps, `5` Streams |
| Settings | `1` TRX64, `2` Video, `3` Audio, `4` Input, `5` Network |
| System | `1` Status, `2` I/O, `3` Expert, `4` Page |

Examples: `F3, 2` opens Library Search; `F4, 2` opens Monitor; `F6, 5`
opens Network settings. Published assignments are compatibility identifiers:
new tabs append to an area and do not silently reorder existing sequences.

## Universal keys

| Key | Meaning |
| --- | --- |
| Cursor keys | Move focus and navigate lists/tabs |
| Return | Select or activate the focused item |
| C64 left-arrow | Back; at the root, leave the UI |
| R/S | Close the UI immediately |
| Freeze/Menu | Toggle the UI open or closed |

While a text, password, path, search, or monitor command field owns keyboard
focus, printable keys including `1` to `5` are input. Area and tab shortcuts
resume when that editor is committed or cancelled.

## Test contract

The area/tab pair is also the preferred semantic address for BDD and Robot
Framework tests. Tests should express `area=Tools, tab=Monitor` and let the
input adapter emit `F4, 2`; focus traversal is retained for accessibility and
for tests of cursor behavior itself. Back, close, and Freeze are separate
actions and must not be treated as aliases.
