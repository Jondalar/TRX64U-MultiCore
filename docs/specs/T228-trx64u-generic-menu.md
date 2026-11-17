# T228 — trx64u runs the generic core menu (the structure of the other cores), not its own trx64u menu

**Status:** IN PROGRESS (2026-11-16 night, S2 on branch t228-s2; spec 2026-11-15, Alex).
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
- S2b Alex in ue2emu (D6): he drives the new menu himself in the emulator and approves it; only then S3.
- S3 board: menu actions on trx64u respond like the other cores (no 2-3 s); every TRX64 function reachable.

## §3 Decision

- D1 (Alex 2026-11-15): b) move trx64u to the generic menu; the UX specialist reworks the menu structure first (S1) because the
  trx64u menu differs strongly from the generic approach.

### Proposal S1 (UX specialist, 2026-11-15 - a proposal for Alex, not a decision)

**The rows below are the proposal as made; D2-D18 (§3 end) override them where they differ** (D4 no SHIFTED heading, D9/D11 no
flash save, D13 Timewarp switches on F5 tab TRX64, D16 own SID tab and "reDIP:SID" instead of "UltiSID").

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

| F8 Expert bar: Clear config | 1 | merge | generic F8 Reset "Reset to host defaults" (D11: no flash, no own row) |

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

- D5 (Alex 2026-11-15, Q2b, after mock-ups c2 / a3): "Apps ... but not everything (!) the port belongs to F1, TRXCRT to Apps"
  (translated): the cartridge PORT actions (mount, remove, save the cartridge in the port) stay at the cartridge port on F1; the
  TRXCRT cartridge tools (dump, inspect, flash cartridge) are an app and go to the "Apps" tab of F7.

- D6 (Alex 2026-11-15, translated: "in any case I want to test it as a human in the emulator first, before it goes out"): after S2 is
  built, Alex tries the new trx64u menu himself in ue2emu (S2b) before any board test, package or release.

- D7 (Alex 2026-11-15, Q2c): Streams (start/stop and their targets) are a section inside the F8 tab "Network", not an own tab.

- D8 (Alex 2026-11-15, Q3a, after mock-ups q03a-c: "a fits, you can make tabs there too", translated): "Reboot, clear memory",
  "Save C64 memory" and "Save REU memory" go to F1 Home, top, beside Reset. F1 may get tabs of its own when it grows.
- D9 (Alex 2026-11-15, Q4a, after mock-ups q04a/b): the Discard / Defaults / Save bar goes; every change applies at once and is
  saved by itself (as in the generic cores). Defaults: F8 > Reset.
- D10 (Alex 2026-11-15, translated: "saving configs and mounted media like the other cores must go in too!"): trx64u saves its
  settings and its mounted media the same way the generic cores do (same mechanism, same "Persist mounts" behaviour); S2 must build it.
- D11 (Alex 2026-11-15, Q5, translated: "we do not save to flash! and nothing is erased in flash there either!!! Reset to host
  defaults is the right one"): F8 > Reset has "Reset to host defaults" only, no "Clear flash config" row. The menu writes and erases
  nothing in flash; settings are not stored in flash (under the MultiCore host; a standalone install may keep a flash cfg, DC13).
- D12 (Alex 2026-11-15, Q6a, after mock-ups q06a-c): "Keys and help" is a row in F8 > Support in every core (generic), beside
  Bug report and About.
- D13 (Alex 2026-11-15, Q7a, after mock-ups q07a/b): the Timewarp ring and Instruction trace switches go to F5 C64 > tab "TRX64",
  section "Recording". The Timewarp overlay stays unchanged (D3).
- D14 (Alex 2026-11-15, Q8a, translated: "A; but only show what the core really can, no longer what was in Gideon's"): the
  Video tab has two sections, "Picture" (System mode, Picture, Scale, Smooth) and "Output" (HDMI). General rule for the whole
  menu: a row is shown only when the trx64u bitstream really implements it; rows copied from Gideon's menu that do nothing are
  dropped. This also settles Q13 (option a). S2 checks every row against the bitstream/app code before keeping it.
- D15 (Alex 2026-11-15, Q9a, after mock-ups q09a/b): one "Volume" row; "Master volume" is merged into it.
- D16 (Alex 2026-11-15, Q10a: "a! But we no longer call it UltiSID please .. with us it is called reDIP:SID", translated): the SID
  sockets and the emulated SIDs get an own F5 tab "SID"; Audio keeps volume and pan only. Every label says "reDIP:SID", never
  "UltiSID".
- D17 (Alex 2026-11-15, Q12a, translated: "a .. and CURSOR RIGHT does the same as elsewhere in the file system, it goes in. So in
  folders AND zips"): Unzip is in the Library menu of the .zip (RETURN). CURSOR RIGHT enters a .zip like a folder (browse its
  contents), the same key that enters a folder.
  - Moved to T202 (Alex 2026-11-17, asked with three options: D17 to T202 / build zip reading in T228 / settle T202 first: "D17 zu T202"):
    the core Library has no zip reading at all (only the Unzip app, which unpacks a whole archive). T228 is finished and merged without
    D17; browsing a .zip is built once, generically, for the host F3 and the core Library in T202.
- D18 (Alex 2026-11-15, Q14a, after mock-ups q14a/a2/b): F8 Network is one tab with sections (WiFi/Ethernet, Services, Streams
  per D7); "FTP hosts" is one row that opens a page with the hosts (BACK returns).
- D19 (Alex 2026-11-17, translated: "a task for later: the documentation must be updated then, both its content and the pictures"): after S2,
  the manual pages that show the core menu get new text and new ue2emu pictures of the built menu (docs/manual, e.g. 11-cores.md and its
  `img/core-*.png`). Part of T228, before it counts as done.
- Q11 needs no answer: D7 decided it. Q13 is settled by D14.
- All S1 questions Q1-Q14 are answered (D1-D18). Next: T212 merges (D2), then S2 is built, then Alex tests in ue2emu (D6).

### Decisions during S2 (decided by Claude on Alex's delegation 2026-11-16 night; Alex can overturn each at S2b)
Alex 2026-11-16 (translated): "you start T228 overnight ... be the advisor here, decide for me". Each decision follows a D-row or the
S1 table where one exists; only DC2, DC3, DC6, DC7, DC9, DC11 and DC12 are new choices (DC1 is Alex's).
- DC1 (Alex 2026-11-17, overturning Claude's first DC1, which made Softboot a Library file action on .cfw/.ue2/.u64): "DC1 raus unter
  Multicore" (translated: "DC1 out under MultiCore"). trx64u offers no Softboot while it runs under the MultiCore host: not in F7 Apps,
  not as a file action. The decision is generic, from the host context, not a core name in the code.
- DC2 The rail heading "DIRECT" goes too, in every core (generic, like D4). Why: with SHIFTED gone it heads the only group and
  separates nothing (an all-caps heading needs a reason, ux-review R11).
- DC3 The 7 Timewarp actions (start/stop recording, marker here, clear markers, back to live, save frame, save the ring) go to F1 Home,
  tab 2 "Recording". The ring and trace switches stay in F5 TRX64 > Recording (D13). Why:
  - The TRX64G Timewarp bar has keys only to move through frames (manual 07-timewarp.md), so these actions cannot live in the F2 overlay
    (D3 keeps it unchanged).
  - Alex put machine actions on F1 rather than F5 (D8, "you can make tabs there too"), and F5 holds settings.
  - The tab is named "Recording", not "Timewarp", so it does not repeat the F2 rail entry.
- DC4 F5 keeps 7 tabs (Machine, TRX64, Memory and I/O, Video, Audio, SID, Input), so the generic `TRX_TABS_MAX` goes from 6 to 8.
  Why: the tabs follow from the S1 table, D13, D14 and D16; folding Input into another tab would hide what it is.
- DC5 The Home machine-info line names the REU only while it is enabled. Why: the line describes the machine as it runs (T212 D13).
- DC6 A section heading that repeats its tab's title is not drawn (e.g. "Machine" over "MACHINE"), in every core (generic). Why: it
  repeats the title (ux-review R10).
- DC7 The old F4 Diagnostics rows that stay go to F8 System, in a tab of their own, "Diagnostics": debug log and clear/save log,
  screen EDID and save EDID, Heap, Debug register. Instruction trace stays only in F5 TRX64 > Recording (D13), with no second row.
  Why:
  - F4 opens the Monitor overlay, which owns the keyboard: "2" types into its command line (S1 finding 6, R10). A Diagnostics tab
    beside it cannot be reached.
  - The rows are device and developer values, and F8 holds the device. Bug report is already in the generic F8 Support (S1 table).
- DC8 (reading D16, not a new choice) The F5 Audio tab keeps its Speaker section. Alex's Q10a answer took the option "own F5 tab SID
  (recommended, Audio then has 3 sections: Volumes / Pan / Speaker)". D16's "volume and pan only" means without the SID rows.
- DC9 F8 Actions uses the generic rows on trx64u too: "Reload core" and "Return to host menu", not an own "Restart core". Why: the S1
  table merges trx64u's "Restart core" into the generic F8 Actions row, and one action has one name on every core.
- DC10 The F8 tabs System / Network / Device stay trx64u-only; other cores keep one System tab. Why: trx64u has 43 network rows
  and the device settings (D18, S1 table). A tab exists only where a core has rows for it (D14), so this changes no other core.
- DC11 The Home picture line ("the core's own picture") is not drawn when the core reports no picture size, in every core
  (generic). Why: on trx64u the line names an internal term and tells nothing (ux-review R5/R6). The machine line already says
  PAL/NTSC, and the output mode is the F5 Video "Picture" row.
- DC12 No compact tab strip. Step 1c built one: a strip wider than its canvas shows the open tab by name and the others as number keys.
  It is dropped again. Why:
  - Only trx64u's F5 (7 tabs, 640 px) would ever use it; no other core has more than 4 tabs per area.
  - ue2emu draws trx64u on the 960 canvas in every mode, so the compact strip can never be shown in a picture (ux-review R7, finding 4),
    and code no picture can prove does not ship.
  - S3 checks the F5 strip on the board at 480p. If it overflows there, the strip comes back from commit 4737d0039.
- DC13 (Alex 2026-11-17, replacing Claude's first DC13, which removed the flash cfg): "Mach das mal optional mit Parameter. Evtl
  ersetzen wir generell das UI mit unserem auch für standalone Installationen. Und dann muss man seine settings ja irgendwie
  persistieren können. Also auch ohne Multicore als Host System." (translated: "make it optional with a parameter; we may replace the
  UI with ours for standalone installations too, and then the settings must persist somehow, also without MultiCore as the host").
  Today a start without a .cor writes `/flash/trx64u/c64.cfg` (board.h:15); with a .cor the cfg is in the core folder. The change:
  - A generic parameter of the app's start context decides whether a start without a .cor keeps its settings in a flash cfg. It is not
    a core name in the code; the builder names where it lives.
  - Under the MultiCore host it is off (D11), so the settings of a start without a .cor stay in memory.
  - A standalone installation, our UI in place of the stock one, turns it on, so the settings persist without a host.
  - `start_migrate_cfg` stays: it reads an old flash file once.
  - The emu scenario S11 covers both values of the parameter.
- DC14 The generic F8 keeps its single-row sections (Network/WiFi, Reset) as T212 approved them. The step 1d ux-review listed folding them
  as a low item; folding would change F8 in every core, which is T212's layout, not T228's. Not done in T228.
- From the spec, not new:
  - F4 is the Monitor rail entry (D3, variant A), so the rail reads "MONITOR", not "TOOLS".
  - The old F4 Diagnostics content goes where the S1 table puts it: Bug report to F8 Support, System information/Board/Uptime to F8
    Status, the cartridge tools to F7 Apps (D5).
  - Create folder goes to F3 Library (S1 table).
- Found, outside T228 (for Alex): T212 D16 says no list area's foot repeats "F1-F8 AREA". The generic `menuctl.cc` foot still prints
  it on every area of every core; only the drive list and pages follow D16.

## §4 Out

## §5 Built

### S2 step 1 (steps 1, 1b and 1c, branch t228-s2, commit 4737d0039, 2026-11-16/17, not merged)
Three Opus pre-commit reviews, the last "COMMIT: yes" (scratchpad t228/step1c/REVIEW3.md); ux-review FAIL with 5 findings (step 1d).
- **Structure.** trx64u's menu.toml is on the T212 structure:
  - F1 Home: resets and memory actions on top (D8), then the Drive A / Cartridge port / Program slots (D5)
  - F2 Timewarp; F3 Library; F4 Monitor (D3)
  - F5: Machine, TRX64, Memory and I/O, Video, Audio, SID, Input (DC4)
  - F7 APP: Settings and Apps
  - F8: System / Network / Device (DC10)
  - F6, the Discard/Defaults/Save bars, Clear config and Auto save config are gone (D9-D11).
- **DC2** done: no "DIRECT" or "SHIFTED" rail heading in any core.
- **DC4:** TRX_TABS_MAX and MENU_STRIP_TABS are 8 (static_assert in ui.cc). The compact strip built in 1c is dropped again (DC12, step 1d).
- **DC5** done: the Home machine line names a setting with `show_state` only while that state holds (REU size while reu.on); unit in test_cut.cc.
  The unit swaps in a copy of the board's reu.on rule: accepted, and ue2emu shows the board rule (Home with the REU off, then on).
- **DC6** done on tabs with `ui = { sections = true }`: a section heading equal to the tab title is not drawn. Other layouts do not apply it
  (no core is affected today).
- **Record v4:** media drive-list fields and a top-level `machine_info`.
  - descrec.py always writes v4, and an older core app refuses it (accepted: host app and core app ship together).
  - `descrec_apply` keeps the base settings (C++ and the application's own items) when the record has none of its own, and logs when they do
    not fit (DR_CSETS 330).
  - Tests use the board's order (menu_build, then apply).
- **Smaller changes:**
  - The F3 browse "Create folder" node is gone (the generic Library has "Create folder here").
  - c64_desc sets no_tone, because TRX64-Ultimate has no tone filter.
- **Picture fix:** the Picture/Scale/Smooth lookups and menu_route_of match the op and the canonical key, because the record keeps a hidden
  "HDMI Scan Resolution" with picture.apply. Units in test_desc.cc. In ue2emu, the Picture row opens the mode page and applies 576p / 480p.
  - The hidden setting ran picture.apply at start, restart and config load (review 3 M1); it is settings.noop since step 1d.
- **check_menu_concept:** each exemption pins the built name (F4 "Monitor", F7 "APP"; F5 tabs 3-7 cite DC4).
- **Tests:** the step 1 "dev.sh test rc 0" came from runs that skipped desc and keywalk (dev.sh tier bug, fixed on main 7c1e47626); the real
  run is in step 1d+1e below. The 10 failing tests/ui steps are a1200/atari/c128/vc20/nes features that fail the same way on main (stale since
  T212); they are not caused by this branch and get rewritten separately.

### S2 step 1d+1e (branch t228-s2, commit bbfb7295b, 2026-11-17, not merged)
Opus pre-commit review 5 "COMMIT: yes" (scratchpad t228/step1e/REVIEW5.md); its comment findings 1-3 are fixed in the commit.
- **M1:** the hidden "HDMI Scan Resolution" applies settings.noop, so the application's Picture row is the only picture setting.
  - desc_check refuses a core setting that uses the key or the op of Picture, Scale or Smoothing. It runs in the tests only; on the board the
    op+key match of the lookups is the guard.
  - A Picture choice stored on main under "HDMI Scan Resolution" (or written there over REST) now does nothing; the app's Picture starts at
    Auto once. This goes into the release notes.
- **DC11** done: no Home picture line without a picture size, in every core.
- **DC12** done: the compact strip is gone; trx_strip_probe reports the tab count. The render bench skips a strip wider than its canvas and
  notes it ("c64 480p F5 tab 1: 7-tab strip ends at x 646, the canvas holds 516"). 576p is compact too (room 596), so about 50 px of tab 7
  "Input" are cut there; the render bench checks 1080p/720p/480p only, so the S3 board check covers 480p and 576p.
- **DC6:** pos_text skips only the title row.
- **Audio labels:** Master, reDIP:SID n, Socket, Enable, Volume, Off (DC8 keeps Speaker). The keywalk golden moves 1025 of 113339 probes, all
  TRX64U F5 (the key 5 jumps into Audio from every F5 tab; only status, selected label and value change).
- **Tests** (forced runs on the builder, PASS lines quoted in REVIEW5): dev.sh test 90 units PASS (descrec, libscan, desc, keywalk, cut);
  integrate render 0 failed (392 strips, 7 skipped by DC12; 504 legends); emu all scenarios passed; tests/ui 138 passed, 10 failed (the
  baseline above); riscv ok; check_d24 rc 0 (the ledger can shrink: menu.toml 2229, menu_rec.inc 2328).
- **Open:**
  - The ux-review of 1d+1e (before the merge).
  - tools/descrec.py refuses only the key "picture" at pack time, not Scale/Smoothing and not the three ops (desc_check's rule); step 2.
  - Step 2: D12, D17, D18, DC1 (no Softboot under the MultiCore host), DC3, DC7, DC13 (flash cfg by parameter; emu S11 both values).
  - D19: manual text and pictures after S2.
  - The contract note (record v4, `sections`, base-settings merge) in docs/contracts/trx64u-firmware-handover.md waits until both sides change.
