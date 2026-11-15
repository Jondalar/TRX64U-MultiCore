# T228 — trx64u runs the generic core menu (the structure of the other cores), not its own trx64u menu

**Status:** PROPOSED (2026-11-15, Alex).
**Repo:** TRX64U-MultiCore — core app (`coreapp/cores/trx64u/menu.toml`, `coreapp/cores/trx64u*`), the import pin
`release/imports/trx64u.pin` (DESC/OPS). No gateware (the bitstream stays TRX64-Ultimate's).
**Number:** T228.
**Origin:** board 2026-11-15: trx64u.cor (bitstream u16 422f9dce, core app 60bfda79) shows the old trx64u menu layout (not the generic
logic) and every menu action takes 2-3 s. Alex (translated): "The ring is not it ... that is the old app. The ring stops in the freeze."
Asked: a) find the delay in the trx64u path, or b) move trx64u to the generic menu. Answer: "b, but the UX senior developer reworks the
menu structures ... they differ strongly from the generic approach."
**Depends on:** T212 (core menu areas, parked branch t212-core-areas), the UX concept (TRX64-Ultimate/docs/lvgl_ui_project).
**Milestone:** M2.

---

## §1 The problem

- P1: trx64u has its own menu description and code path that differs strongly from the generic structure of every other core.
- P2: actions in that path take 2-3 s (cause not measured; the ring is ruled out by Alex: it stops in the freeze).
- P3: functions only trx64u has (TRX64 features) must find a place in the generic structure.

## §2 Scenarios

- S1 UX design: the UX specialist maps every trx64u menu entry to the generic structure (keep / move / merge / drop), with ue2emu
  mock pictures; Alex decides entry by entry where it is not obvious.
- S2 build: trx64u uses the generic menu; its own entries live where S1 decided; ue2emu pictures, ux-review PASS.
- S3 board: menu actions on trx64u respond like the other cores (no 2-3 s); every TRX64 function reachable.

## §3 Decision

- D1 (Alex 2026-11-15): b) move trx64u to the generic menu; the UX specialist reworks the menu structure first (S1) because the
  trx64u menu differs strongly from the generic approach.

### Proposal S1 (UX specialist, 2026-11-15 - a proposal for Alex, not a decision)

**Base (read, not redone):** `TRX64-Ultimate/docs/gideon-menu-tree.md` §5-6 (Gideon's tree: objects in several places, technical grouping; target
shell Home/Library/C64/Timewarp/Tools/Settings), `docs/lvgl_ui_project/NAVIGATION.md` (area keys F1..F8, numbered tabs; trx64u today is this contract
1:1: F5 C64, F6 Settings, F7 Actions), `UI-COVERAGE.md` (settings placement table; "UI does not invent an Apply/Discard transaction"; persistence
actions stay reachable), `docs/gap-audit-gideon-app.md` (checked: no menu entry here depends on a gap row; G07 serial bus mode, G08 user port, G12
drive sound are "not supported", so their rows are settings of the app that do nothing in this core - see Q13), T212 D1-D17 (the target of the
generic cores). Inventory source: `coreapp/cores/trx64u/menu.toml` (127 nodes, 160 settings, 4 tasks, 2 media), golden `ops_trx64u.txt`.

**What the generic cores have (main, c128 pictures `t228-gen-*`):** rail Home F1, Library F3, core F5 (tabs core/Video/Audio/Input), Actions F7,
System F8; no F2/F4/F6. Home = session card + STATUS box; Audio = one Volume row; Video = Picture rows; F8 = Actions/Network/Support/Reset.
T212 (parked, decisions D1-D17) moves them on: F1 quick actions with drive rows, F5 machine only, F7 APP (settings of the app and its libraries),
F8 System with the status rows. **trx64u differs in three ways:** (1) it declares its whole area list itself (menu.toml) instead of taking the
generic skeleton, (2) it has its own copies of generic settings (Persist mounts, Enter home, Menu opens at, Home directory, Picture, Volume), (3) it has
areas no other core has (F2 Timewarp, F4 Tools, F6 Settings).

**Findings on the current trx64u menu (emulator, real app image of main 18:30, `t228-cur-*`; trx64u runs in ue2emu as a 960x540 canvas in the top-left of a 1080p frame: R7 not judged here):**
1. (R10) Settings in three places: F5 (drive, machine, I/O), F6 (TRX64, video, audio, input, network), F8 Expert (21 rows incl. machine hardware). A person looking for "bus sharing" or "REU" must know which of three.
2. (R10) Objects in several places: Eject disk / Reset drive appear on F5 side pane, F7 Quick, (F1 rows open the Library); Reset on F1 and F7; Disk state on F1, F5 side pane.
3. (R10) Information-only focusables: F8 Status (13 rows, `t228-cur-f8-status.png`), F5 side panes, F1 "Settings" jump row, F3 Online right pane "SEARCH" is empty.
4. (R3) Flat long lists: Audio 45 rows ("1-10 / 45", `t228-cur-f6-audio.png`), Network 47, F7 More 20 rows under four headings (Disk and files / Machine / Timewarp mixed: `t228-cur-f7-more.png`).
5. (R10) The foot lists R RESET, P PAUSE, E EJECT, X REMOVE on every tab, also on F8 Status, F3 Online and settings tabs (keys of the F1 context leak into others).
6. (R10) F4 Monitor owns the keyboard: pressing 2 types into the command line instead of opening Diagnostics (`t228-cur-f4-diag.png` shows the monitor with ".2" typed); the tab strip hint "1-2 TAB" is wrong on that tab.
7. (R10) Discard / Defaults / Save bar on five tabs plus Restart core / Clear config on Expert: a transaction the generic cores and the concept do not have.

**Proposed rail (text mock-up; the generic app renders this from the descriptor, no new widgets):**
```
 DIRECT                         SHIFTED
 HOME      F1   session: C64 PAL, REU 512K, Turbo 2x         TIMEWARP  F2   Timeline | Recording (ring, trace)   [core-only area]
 LIBRARY   F3   1 Browse  2 Search  3 Online                 TOOLS     F4   1 Monitor 2 Diagnostics 3 Apps 4 Streams [core-only area]
 C64       F5   1 Machine 2 Drive 3 Memory and I/O 4 Video   SYSTEM    F8   Status | Network | Device | Actions | Support | Reset
               5 Audio  6 SID   7 Input  8 Expert
 APP       F7   Hotkeys, Persist mounts, Enter home, Menu opens at, Home directory, Overlay, Splash, Auto save, Zip (T212 D6/D12)
 (F6 Settings gone)
F1 Home: [Pause / resume] [Reset] [Reload ROMs, reset] [Reboot, clear memory] / Drive A -> RETURN list (mount from Library, create blank D64/G64/D81,
         mounted: eject, swap, save as D64/G64, unlink, reset drive) / Cartridge port -> list (mount, remove, save cartridge) / Program / Save config / Load config
```
Pictures: the look is the generic app's (`t228-gen-f1-home.png` .. `t228-gen-f8.png`, c128 on main; T212's F1/F7 pictures are on the parked branch,
`b7077f8b1`). No ue2emu picture of the proposal exists: the layout comes from `menu.toml`, which is compiled into the app image (a variant needs an
app build, i.e. S2); the text mock-up above stands in. Pictures: /private/tmp/claude-501/-Users-alex-Development-TRX64U-MultiCore/0ca06245-af10-4231-a6a6-09d268feb49b/scratchpad/t228/ (not in the repository; not committed).

**Mapping table** (generated from `menu.toml` by rule, so every entry is counted once; 293 entries: keep 90, move 136, merge 56, drop 11).
"Entries" = nodes, settings, tasks and media of the descriptor; the 18 Discard/Defaults/Save bar buttons and 20 Online form rows count as entries.

| Entry group | Entries | Verdict | Target and reason |
|---|---|---|---|
| Home: Disk / Cartridge / Program rows | 3 | keep | F1 drive and slot rows (T212 D5/D9/D11: RETURN opens the slot list) |

| Home: Pause/resume, Reset | 2 | keep | top of F1 beside the machine resets (T212 D1/D17: "Reset"); Pause is TRX64-only and stays here |

| F2 Timeline: Resume here | 1 | keep | Timewarp action on its own screen |

| F4 Diagnostics: debug log, clear/save log, screen EDID, save EDID, instruction trace | 6 | keep | TRX64 developer tools (Tools area) |

| F4 Diagnostics side: Heap, Debug register | 2 | keep | developer values, only here |

| F4 Monitor: Help | 1 | keep | terminal screen help |

| F3 Online tab: form rows (Library, Name, Group, Handle, Event, Choices, Choice 1-12, Search, Clear) | 20 | keep | core-only Library tab (Assembly64/CommoServe); fix: the empty SEARCH pane on the right is an info-only box (R10) |

| F8 Status: Application (Build), ROMs, Libraries | 3 | keep | generic F8 Status (T212 D3/D7) |

| F8 Status: Board, Uptime, SD card, USB sticks, Flash, USB mouse/keyboard | 8 | keep | device status, kept in F8 Status (trx64u-only content in the generic section) |

| F8 Status: Keys and help | 1 | keep | Q6: a row that opens help; generic cores have none |

| Palette definition (hidden) | 1 | keep | stays hidden |

| Audio mixer: volumes, pans, speaker (22) | 22 | keep | F5 Audio tab, sections Volumes / Pan / Speaker (R3: grouped) |

| Input: joystick, mouse (6) | 6 | keep | F5 Input tab (hotkeys come from the generic F7 APP) |

| Machine (Fast reset, Kernal / Basic / Char ROM) | 4 | keep | F5 first tab (the machine) |

| I/O (cartridge preference, REU x5, Ultimate audio map, UCI) | 8 | keep | F5 tab Memory and I/O (core-only hardware) |

| Media slots: Program, Drive A (descriptor entries) | 2 | keep | slot rows on F1 |

| F7 More: Reboot clear memory, Save C64 memory, Save REU memory | 3 | move | F1 top, machine actions beside Reset (Q3) |

| F7 More: Power cycle, Power off | 2 | move | F8 System > Actions (device action, not a session action) |

| F7 More: Create folder | 1 | move | F3 Library (file action where the folder is shown; concept: Library -> Manage) |

| F7 More: Timewarp actions (start/stop recording, marker, clear markers, back to live, save frame, save ring) | 7 | move | F2 Timewarp > Timeline (concept: Timewarp is one visual task, not entries in the action list) |

| F4 Diagnostics: apps (Cartridge dumper, inspector, Flash cartridge, Softboot, Unzip) | 5 | move | Tools > Apps tab (concept NAVIGATION: Tools 4 Apps); they are launchers, not diagnostics |

| F6 Network: Ethernet / MAC / Wi-Fi status rows | 5 | move | F8 System > Network (device status next to the WiFi row) |

| F6 Network: start/stop VIC, audio, debug stream (6) | 6 | move | Tools > Streams (concept: Tools 5 Streams; the action sits beside its settings) |

| TRX64: Freeze mode, Turbo control, CPU speed, Save changed cartridge | 4 | move | F5 machine (tab "TRX64": machine behaviour only the TRX64 core has; T212 D8 puts CPU speed in F5) |

| Drive A settings (Drive, Bus ID, swap delay, reset with C64, freeze in menu) | 5 | move | F5 machine, section Drive (F5 Drive A tab becomes a section/tab of the machine) |

| SID: sockets (14) and UltiSID (8) | 22 | move | F5 Audio tab, sections SID sockets / UltiSID, or a tab of its own (Q10) |

| TRX64: Overlay transparency, Splash, Auto save config | 3 | move | F7 APP (settings of the menu/app, T212 D6) |

| Expert: DMA ID, bus mode and sharing (4), GAME/EXROM, burst, serial bus mode, badline, SuperCPU detect, user port power (12) | 12 | move | F5 machine, tab Expert/Hardware (machine hardware, not system; today wrongly under F8) |

| Drive: Leave menu on mount | 1 | move | F7 APP (menu behaviour, not drive hardware) |

| Drive A extras (extra RAM, GCR align, GEOS, track twist, SpeedDOS cable, 1541 ROM) | 6 | move | F5 machine, section Drive (Drive extras) |

| Expert: Power on after power loss, Wake on Wi-Fi, LED select top/bot, Colour scheme | 5 | move | F8 System, section Device (system, not machine) |

| TRX64: Timewarp ring, Instruction trace | 2 | move | F2 Timewarp (Q7: or F5 tab TRX64) |

| Network: Ethernet (5), host/services (9), time (5), FTP hosts (24) | 43 | move | F8 System > Network, sections (not machine; today F6) |

| Streams: stream targets and debug mode (4) | 4 | move | Tools > Streams (Q11) |

| Home: RAM expansion / Turbo / Video state rows | 3 | merge | the one machine-info line from the descriptor (T212 D13/D15), e.g. "C64 PAL, 512K REU, Turbo 2x" |

| F5 side pane: Eject disk, Reset drive | 2 | merge | drive slot list on F1 (T212 D5: mounted drive -> eject, reset drive) |

| F7 More: Insert blank disk, Unlink disk | 2 | merge | drive slot list (T212 D5: create blank / mounted actions) |

| F7 Quick tab (7 actions) | 7 | merge | each action goes to its object: drive slot list (swap, eject, reset drive), F1 top (Reset, Pause, Reload ROMs, reset), cartridge slot list (remove); the Quick tab goes (T212 D2/D5/D11/D17) |

| F7 More: Save cartridge | 1 | merge | cartridge slot list (T212 D9: "save battery/backup RAM now" belongs to the slot) |

| Settings bars: Discard, Defaults, Save (5 tabs) | 18 | merge | see Q4 - the generic app has no transaction (live apply, config autosaved per core); Defaults -> F8 "Reset to host defaults" |

| F8 Expert bar: Restart core | 1 | merge | generic F8 Actions "Restart core"/"Reload core" |

| F8 Expert bar: Clear config | 1 | merge | generic F8 Reset "Reset to host defaults" (Q5: is "clear the flash config" a separate action?) |

| F4 Diagnostics: Bug report | 1 | merge | generic F8 Support "Bug report" |

| F4 Diagnostics: System information | 1 | merge | F8 Status (same content) |

| F4 Diagnostics side: Board, Up for | 2 | merge | F8 Status (Board, Uptime are listed there too: redundancy, R10) |

| F8 Status: WiFi | 1 | merge | generic F8 Network "WiFi" row |

| Drive: Persist medium mounts | 1 | merge | generic F7 APP "persist mounts" (T212 D1; one row for every core) |

| Video: System mode, HDMI scan resolution, scan lines, analog/digital mode, colour clock | 6 | merge | F5 Video tab together with the generic Picture/Scale/Smooth rows (Q8: System Mode vs generic Picture) |

| Audio: Volume master | 1 | merge | generic F5 Audio "Volume" (one volume row; Q9) |

| Expert: Enter home on startup, Menu opens at, Home directory, Filename overflow squeeze | 4 | merge | generic F7 APP (T212 D1/D6; same rows in every core) |

| Tasks: Save disk as D64/G64, Create blank D64/G64 | 4 | merge | drive slot list (T212 D5: create blank, save disk as) |

| Home: "Settings" jump row | 1 | drop | a row that only jumps to F5 is an info/jump row; the rail already has F5 (R10) |

| F5 side panes: Disk, Machine, Video, Turbo, Cartridge states | 5 | drop | read-only copy of values shown on F1 / in the row itself (R10 redundancy, R5) |

| F7 More: group headings (Machine, Power, Timewarp, Disk and files) | 4 | drop | the tab goes; the headings have nothing left to head |

| F8 Status: Core | 1 | drop | top bar and Home already say it (T212 D7) |


Net effect: F7 Quick and More, F6 Settings, F5 Media sets and F5 side panes disappear as places (their entries are merged into F1 drive lists, F5, F7 APP, F8
or the two core-only areas); the settings of the machine are in one area (F5), the app's in F7, the device's in F8.

**Open questions (Alex; each with the options, my recommendation first):**
- Q1 Target: build trx64u on T212's structure (F1 drive rows, F5 machine, F7 APP; T212 is unmerged, so S2 depends on merging it) or on main's present generic (F1 session+STATUS, F5 tabs, F7 Quick)? Recommended: T212 (your D1-D17 already decided it).
- Q2 Rail: keep F2 Timewarp and F4 Tools as core-only areas, dissolve F6 Settings into F5/F7/F8? Or move Tools under F8 and Timewarp under F5?
- Q3 "Reboot, clear memory", "Save C64 memory", "Save REU memory": F1 top beside Reset (recommended), F8 Actions, or F5 machine?
- Q4 Discard/Defaults/Save: drop (live apply, config autosaved as in the other cores; "Defaults" = F8 "Reset to host defaults") or keep one "Save to flash" row for the flash-stored settings (concept: persistence actions reachable from every settings page)?
- Q5 "Clear config" (erases the flash config): its own F8 row under Reset, or the same as "Reset to host defaults"?
- Q6 "Keys and help": add to every core's F8 (recommended; one help screen of hotkeys), keep trx64u-only, or drop?
- Q7 "Timewarp ring" and "Instruction trace" switches: on F2 Timewarp (recommended, the object is there) or F5 tab TRX64?
- Q8 Video: trx64u's "System mode" (PAL/NTSC family) and the generic Picture/Scale/Smooth rows (T222 made the VIC path's Picture family the System Mode's): one merged set or both listed?
- Q9 "Volume master" and the generic "Volume": one row (recommended; master is the generic one) or both?
- Q10 SID sockets and UltiSID (22 rows): sections of the F5 Audio tab or an own F5 tab "SID" (recommended, Audio then has 3 sections: Volumes / Pan / Speaker)?
- Q11 Stream settings and start/stop: Tools > Streams (recommended, concept) or F8 Network?
- Q12 "Unzip" launcher: Tools > Apps or F7 APP (T212 D6: libraries install their own settings there)?
- Q13 Rows whose function the gap audit says this core does not serve (Serial bus mode G07, Burst mode patch G07, UserPort power G08, drive volume G12): hide them (menu is a whitelist) or keep? Needs a check on the board/in the bitstream first; not decided here.
- Q14 Network (43 rows incl. FTP hosts 1-4 x6): one F8 Network tab with sections, FTP hosts opened from one row as a page (T187 D2 page route; recommended)?

**Not checked:** F2 Timeline and F3 Search/Browse pictures were produced but not judged (their content is unchanged by the mapping); no 480p/1080p variants
(trx64u has no scaled emulator mode, `ux-review` §7); the board; whether every proposed F5 tab fits the 480p row count (S2 renders it).

### Q2 mock-ups (UX specialist, 2026-11-15 - a proposal for Alex, not a decision)

Constraint (Alex via coordinator): Timewarp and Monitor are overlays over the running C64 and keep the style of the TRX64G build
(Gideon's app on the TRX64 bitstream); the mock-ups decide only from where they are reached. Reference for the overlays: the manual
images `TRX64-Ultimate/docs/manual/img/timewarp-bar.png` and `trxmon-start.png` (manual 06-monitor.md, 07-timewarp.md), used
unchanged in `overlay-timewarp-trx64g.png` / `overlay-monitor-trx64g.png` (the monitor 1280x800 fitted into 960x540 with side bars,
same for all variants). The menu pictures are static drawings in the app's look (colours sampled from `t228-gen-*`, Verdana instead of
Montserrat), not app renders; generator `/private/tmp/claude-501/-Users-alex-Development-TRX64U-MultiCore/0ca06245-af10-4231-a6a6-09d268feb49b/scratchpad/t228/q2/mock.py`. Pictures in /private/tmp/claude-501/-Users-alex-Development-TRX64U-MultiCore/0ca06245-af10-4231-a6a6-09d268feb49b/scratchpad/t228/q2/ (not in the repository).
Facts used: trx64g reaches Timewarp via C64 Machine > Timewarp (Freeze menu) and the monitor via Developer > TRX64 Monitor or C= O;
trx64g apps add entries to context menus of their file types (trex_crt: "Write to Cartridge" on .crt; unzip on .zip; softboot on
.cfw/.ue2/.u64) and to C64 Machine (Dump Cartridge, Cart Inspector, Flash Cartridge).

| Variant | Where things land | Pictures |
|---|---|---|
| A: rail F1 Home, F2 Timewarp, F3 Library, F4 Monitor, F5 C64, F7 APP, F8 System | F2/F4 open the overlay at once (as today); cartridge tools, Softboot, Unzip = F7 APP tab "Apps"; streams in F8 Network | a1-rail-timewarp, a2-rail-monitor, a3-f7-apps, a4-f8-streams |
| B: standard rail only (F1/F3/F5/F7/F8) | Timewarp + Monitor = rows on a new F5 tab "Debug" (RETURN opens the overlay; ring/trace switches beside them); cartridge tools + Unzip in F7 APP tab "Apps"; streams in F8 Network | b1-rail, b2-f5-debug, b3-f7-apps, b4-f8-streams |
| C (recommended): standard areas + a rail group "Over the C64" (Timewarp F2, Monitor F4); apps dissolve into their objects | cartridge tools in the F1 cartridge-port list; Unzip (and Softboot, Write to Cartridge) in the Library context menu of the file; streams = F8 tab "Streams"; F7 APP stays settings only | c1-rail, c2-f1-cart, c3-f3-library-zip, c4-f8-streams |
| all | the overlays | overlay-timewarp-trx64g, overlay-monitor-trx64g |

Verdicts (skill R-rules):
- A: PASS with findings. Keeps today's keys (F2/F4 muscle memory, one key = overlay). R10: F2/F4 look like areas but have no screen to
  stay in, they are launchers beside real areas (the rail mixes two kinds without saying so); F7 "Apps" mixes launchers into the
  area that T212 D6 made the home of app settings (R4/R10: settings and actions in one area), and the cartridge tools are far from
  the cartridge slot (R10 redundancy/object rule).
- B: FAIL on structure. F5 is "machine settings only" (R4); "Open Timewarp / Open Monitor" are two focusable launcher rows in a
  settings area (R10), the overlay hotkeys are lost or hidden, a key that took one press now takes F5, 7, RETURN. The ring/trace
  switches on that tab are fine (Q7) - they can go to the F5 TRX64 tab without the launchers. Apps in F7 as in A.
- C: PASS with findings. One purpose per area holds: F7 = app settings, F1 = session and slot actions, F3 = file actions, F8 =
  device incl. Streams. The group heading names what Timewarp/Monitor are (overlays), keeps F2/F4, costs one rail group (R11:
  all-caps heading has a reason: it separates launchers from areas). Findings: the cartridge tools sit under "Cartridge port" and
  are visible with an empty slot (they work on the real cartridge in the port, not on a mounted file: label them so; trx64g
  has the same entries under C64 Machine); Softboot and Write to Cartridge on .cfw/.crt not drawn (same mechanism as Unzip); an
  app whose menu entry moves into context menus needs the descriptor to allow it (S2).
Recommendation: C. Fallback A if the group heading is not wanted (then drop the apps tab from F7 and use C's placement anyway).
Questions for Alex (one per line, ask one at a time): Q2a rail group "Over the C64" for Timewarp/Monitor, or plain rail entries as in
A? Q2b cartridge tools next to the cartridge port (F1), or in one place "Apps"? Q2c Streams as own F8 tab, or inside F8 Network?
Not checked: no app render (menu.toml compiled into the app); overlays not driven from the menu; 480p/1080p; the monitor's 80x40
overlay was fitted from a 1280x800 manual picture, so its scale on the real 960x540 canvas is not shown.

- D2 (Alex 2026-11-15, Q1): a) build trx64u on T212's structure (F1 drive rows, F5 machine, F7 APP); T212 has to be merged first.

- D3 (Alex 2026-11-15, Q2a, after the mock-ups c1 / a1): variant A for the rail - Timewarp F2 and Monitor F4 as plain rail entries,
  no group "OVER THE C64" ("the 2nd is better"). The overlays keep the TRX64G style unchanged.
- D4 (Alex 2026-11-15, translated: "can the stupid SHIFTED go away"): the rail has no "SHIFTED" heading. The rail is generic, so this
  applies to every core's menu, not only trx64u.

## §4 Out

## §5 Built
