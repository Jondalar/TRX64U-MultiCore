# TRX64 M2 UI — Screenshot tour

This document shows the complete LVGL Pro XML prototype as a visual user
journey. The screenshots were captured from the actual 960 × 540 editor
preview on 10 October 2026.

The screens currently use preview data. They describe information hierarchy,
navigation, visual states, and confirmation flows; they do not invoke firmware
actions yet. The adapter work is specified separately in
[`../specs/T24-ui-wiring.md`](../specs/T24-ui-wiring.md).

This revision deliberately uses a minimalist hierarchy: one dominant task per
screen, flat lists instead of card grids, 11–21 px typography, and accent color
only for focus or meaningful state. The F-key rail selects an area and the
numbered top row selects a tab inside it.

![Overview of all TRX64 M2 UI screens](screenshots/00-overview.jpg)

## Controls

- The `↕` and `↔` cursor keycaps move focus.
- The `↵` keycap selects; `←` goes back and exits at the root.
- `F1`, `F3`, `F5`, and `F7` select the frequent areas; `F2`, `F4`, `F6`, and
  `F8` select the shifted secondary areas.
- Number keys `1`–`5` select the labeled tab inside the current area.
- `R/S` closes the UI; the physical Freeze/Menu button toggles it open or
  closed.

## 1. Orientation

### Home

The running C64 is the dominant surface. Drive, Timewarp, and network state are
reduced to three quiet rows; everything else is contextual.

![Home dashboard](screenshots/01-home.jpg)

[Open the screen XML](screens/home.xml)

## 2. Media journey

### Library

One browser covers recent items, SD card, USB, network sources, and favorites.
The selected item remains visible beside the list, together with the primary
action.

![Media library](screenshots/02-library.jpg)

[Open the screen XML](screens/library.xml)

### Library actions

The selected item gets one flat action list. Only format, drive state, and safe
eject remain in the supporting rail.

![Actions for a selected disk image](screenshots/03-library-actions.jpg)

[Open the screen XML](screens/library_actions.xml)

### Safe eject

Eject is a deliberate, state-aware step. The dialog explains what will happen
and shows whether pending data needs attention before the action is confirmed.

![Safe-eject confirmation](screenshots/04-media-eject.jpg)

[Open the screen XML](screens/media_eject.xml)

## 3. Machine and data safety

### C64 and media

Mounted media and essential machine state share one operational screen. Hard
reset, power, and peripheral details stay behind `F7` instead of competing with
routine controls.

![C64 and media control screen](screenshots/05-c64.jpg)

[Open the screen XML](screens/c64.xml)

### Cartridge writeback warning

Modified persistent cartridge data gets a short decision surface: `←`
cancels; the two visible actions discard or save before ejecting.

![Persistent cartridge data warning](screenshots/06-writeback-warning.jpg)

[Open the screen XML](screens/writeback_warning.xml)

## 4. Reversible session

### Timewarp

The history can be scrubbed and previewed first. The running session changes
only after `RESUME HERE`, which keeps exploration reversible and explicit.

![Timewarp history and preview](screenshots/07-timewarp.jpg)

[Open the screen XML](screens/timewarp.xml)

## 5. Tools

### Tools overview

Diagnostics, trace capture, apps, streams, and system information form one flat
list. Activity history stays behind `F7` and appears only when requested.

![Tools overview](screenshots/08-tools.jpg)

[Open the screen XML](screens/tools.xml)

### Monitor

The terminal owns nearly the entire workspace. Continue, step, and step-over
remain visible; secondary quick views move to `F7`.

![Machine monitor](screenshots/09-monitor.jpg)

[Open the screen XML](screens/monitor.xml)

## 6. Configuration

### Settings overview

Settings are one clean category list. Profile and saved state fit into the
header instead of occupying a permanent explanatory rail.

![Settings categories and persistence summary](screenshots/10-settings.jpg)

[Open the screen XML](screens/settings.xml)

### TRX64 behavior

M2 behavior and persistence controls are presented as live settings. `SAVE TO
FLASH` persists the current values; `RESET FROM FLASH` restores the stored
values.

![TRX64 behavior settings](screenshots/11-settings-trx64.jpg)

[Open the screen XML](screens/settings_trx64.xml)

### Video and overlay

Overlay appearance is tuned beside a live preview. This keeps visual changes
immediate and understandable without a separate apply transaction.

![Video and overlay settings](screenshots/12-settings-video.jpg)

[Open the screen XML](screens/settings_video.xml)

### Network

Available networks own the page. Address and common service states are compact
status lines; detailed services move to `F7`.

![Network and service settings](screenshots/13-settings-network.jpg)

[Open the screen XML](screens/settings_network.xml)

### System information

Firmware, FPGA, and storage become one status line above a short event list.
Maintenance stays at the bottom; reset remains a contextual danger action.

![System information and maintenance](screenshots/14-system.jpg)

[Open the screen XML](screens/system.xml)

## Navigation map

```mermaid
flowchart TD
    F1[F1 Home] --> H1[1 Home]
    F3[F3 Library] --> L1[1 Browse]
    F3 --> L2[2 Search]
    F3 --> L3[3 Manage]
    F5[F5 C64] --> C1[1 Summary]
    F5 --> C2[2 Machine]
    F5 --> C3[3 Drives]
    F5 --> C4[4 I/O]
    F7[F7 Actions] --> A1[1 Quick]
    F7 --> A2[2 C64]
    F7 --> A3[3 Timewarp]
    F7 --> A4[4 Library]
    F2[F2 Timewarp] --> T1[1 Timeline]
    F4[F4 Tools] --> T2[1 Overview · 2 Monitor · 3 Diag · 4 Apps · 5 Streams]
    F6[F6 Settings] --> S1[1 TRX64 · 2 Video · 3 Audio · 4 Input · 5 Network]
    F8[F8 System] --> Y1[1 Status · 2 I/O · 3 Expert · 4 Page]
```

`←` returns from a detail or confirmation screen and exits at the root. The
complete stable sequence and automation contract is in
[`NAVIGATION.md`](NAVIGATION.md).

## Design references

- [`../gideon-menu-tree.md`](../gideon-menu-tree.md) — source feature and menu inventory
- [`../specs/T24-lvgl-ui.md`](../specs/T24-lvgl-ui.md) — UI scope and architecture
- [`../specs/T24-ui-wiring.md`](../specs/T24-ui-wiring.md) — later firmware adapter work
