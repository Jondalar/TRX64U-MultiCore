# T212 - Core menu areas: F1 quick actions, F5 machine, F7 app settings

Status: PROPOSED 2026-11-12, Alex (board photos of the A1200 core app: F1, F5, F7). Corrected 2026-11-13 (status check): decisions D1-D17 taken (Alex); built on branch t212-core-areas (b7077f8b1), not merged to main.

## §1 The problem
- P1: F1 Home is information only; the cursor walks the STATUS box without any action. Alex: "I can't do ANYTHING here, only info.
  And the cursor only goes through the status area, without action. So a waste of interaction, to be honest." (translated)
- P2: F7 ACTIONS "Quick" (reset, reload Kickstart, eject DF0-HD1, create blank ADF, mount folder as HD0/HD1) is "in large parts
  redundant to the display in F1" (translated).
- P3: F5 tab 1 mixes machine settings with app settings (STORAGE: persist mounts, enter home on startup, home directory; MENU: menu
  opens at); hotkeys sit under F5 Input; the zip behaviour of T202 needs a place.

## §2 Scenarios
Proof column: T = core app test (`tests/coreapp`, menu goldens), U = ue2emu picture via `tests/ui` (A1200, C128, VC-20; 480p, 720p, 1080p).

| # | Scenario | Proof |
|---|----------|-------|
| S1 | F1 Home has no STATUS box; the cursor walks the Reset / Reload rows and the drive rows, every one does something on RETURN | T (golden of F1: no `info` rows, `media`/`task` rows), U (F1) |
| S2 | RETURN on a mounted drive row ejects it (toast "<slot> ejected") | T (key walk), U |
| S3 | RETURN on an empty drive row mounts from the Library / creates a blank, per core with the ops the core has - OPEN, see §5 Q1 | T, U |
| S4 | Reset and Reload on top of F1 are the core's own reset ops ("Reset Amiga", "Reload Kickstart, Reset"); they close the menu | T, U |
| S5 | Save as config / Load config stay on F1 | T |
| S6 | A machine-info line on F1 from existing setting values (A1200: chipset, RAM, video mode); a core without the values shows none | T, U |
| S7 | F7 is the app settings area (label OPEN, §5 Q2) and holds Storage (persist mounts, enter home on startup, home directory), Menu (opens at), Hotkeys; the Quick list is gone | T (golden), U (F7) |
| S8 | F5 holds machine settings only: tab 1 MACHINE, Video, Audio, Input without hotkeys | T (golden), U (F5) |
| S9 | A setting that exists in the host app takes the host's value on "Reset to host defaults" in F7 too; no new host setting | T |
| S10 | F8 shows Build version, ROMs, Libraries below the existing rows, the rest unchanged | T, U (F8) |
| S11 | Keys and the foot legend of F1 / F7 match what the keys do | T (legend), U |
| S12 | One line per row, no overlap, selection visible, long names cut, at 480p / 720p / 1080p (UX R1, R2, R5, R7) | U + ux-review |
| S13 | Zip behaviour (T202): NOT built here (T202 has no build yet) | - |

## §3 Decisions
- D1 (Alex 2026-11-12, "proposal for you and the UX agent"): F5 = machine (core) settings; F7 = menu-app settings; F1 = quick actions
  with the cursor keys instead of the status box; F8 stays as it is. Replaces T205 D1 where it put every setting of a core under F5.
  Confirmed with Alex 11-12 ("for the core menu yes") on this summary: F1's drive rows are the quick actions (eject, mount, create blank), Reset and
  Reload on top, Save/Load config stay; F7 holds persist mounts, enter home on startup, home directory, menu opens at, hotkeys, the zip behaviour (T202).
- D4 (Alex 11-12): "then go, those are only app builds. Build it and tie a test-release bundle" (translated).
- D2 (Alex 11-12): every core always has its own config, with the option of the host's value as default when the same setting exists
  in the host's app.
- D3 (Alex 11-12): the STATUS box content moves to F8 ("let that move to F8"). F1 Home shows useful machine information where a core
  has it (e.g. OCS/ECS/AGA, RAM, PAL/NTSC); the drives are in the overview anyway. What else goes on Home: wait for feedback from
  completely new users.

- D5 (Alex 11-12, "yes" to the proposal): RETURN on a drive row in F1 opens a short list of everything that belongs to that drive -
  empty: mount from the Library, create blank (each format the core has, e.g. C128 D64/G64/D81); mounted: eject, save disk as, reset drive.
  No F7 action is left over; each core declares which task belongs to which drive (per-core work).

- D6 (Alex 11-12): F7 holds the settings of the generic app and of what installs itself into it (libraries, e.g. zip of T197):
  "F7 is the settings for the generic app (and what installs itself there, so zip too)" (translated). Each library brings and
  registers its own settings; F7 is not filled row by row. Rail label "APP" (Alex: "yes").

- D7 (Alex 11-12, "yes"): the STATUS rows Core, Network and Media are dropped (Core is in the top bar and on Home, Network is the
  WiFi row in F8 and the top bar, Media is the top bar and the drive rows); Build, ROMs, Libraries move to F8 (D3).

- D8 (Alex 11-12, "yes"): tasks that fit no drive: "Map buttons pad 1/2" (snes, nes, neogeo) -> F5 Input; "50 / 60 Hz" and
  "CPU speed" (zxnext) -> F5 (machine); "Sound check" (snes) and "NMI: Multiface" (zxnext) stay as actions on top of F1 beside Reset.

- D9 (Alex 11-12, "yes"): "Save the battery RAM now" (snes) and "Save the memory card and backup RAM now" (neogeo) belong to the
  mounted cartridge slot; neogeo "Load a ROM set from a folder" is a mount path of the cartridge slot: beside "Mount from Library..."
  (empty) and "Replace..." (mounted), not a separate third row.

- D10 (Alex 11-12): tape controls (Play, Stop, Rewind, Prev/Next block; cpc, zx, pet) are in the mounted Tape slot's list; "Play closes
  the menu as before, just like 'load and run' on disks" (translated); the other tape keys keep the menu open.

- D11 (Alex 11-12, "yes"): atari "Remove cartridge, cold start" moves into the cartridge slot's list under "Eject" and closes the
  menu there; the top of F1 holds only the machine's resets.

- D12 (Alex 11-12, "yes"): F7 APP has no single "Settings" tab (one tab carries no information, R11); it shows its sections directly.
  "Keys"/"Hotkeys" become one section "Hotkeys". The drive list's title is the slot name (DF0, Drive 8).

- D13 (Alex 11-12): the F1 machine-info line shows each core's model/version and memory from its existing settings: "yes, for the
  PET the version, for the VIC20 the memory, for the C264 too, and so on" (translated). C128: the running mode (C128 80 columns / C64);
  SCPU: speed and SuperCPU RAM; Amiga: chipset and RAM; every core with such settings gets the line.

- D14 (Alex 11-12): "Enter home on startup" shows No/Yes in the core; the host's Disabled/Enabled is translated internally so the host
  value still works as the default (D2).

- D15 (Alex 11-12): the machine-info line comes from the core's .toml, not from C code in the app. Architecture rule (Alex):
  "bitstream <-> API to app; .toml == glue between a bitstream and the app; app = generic". Every capability lives in the generic app
  (hotkeys, info line, drive lists, ...); a core only declares in its .toml what it has.

- D16 (Alex 11-12, "Ja", shown the C128 480p ue2emu pictures): every list area's foot reads like the drive page:
  "UP/DOWN CHOOSE  RETURN DO IT  ESC BACK". Before, Home/Library/F7/F8 showed a bare "RETURN" and "<- BACK". No "F1-F8 AREA":
  what the left rail shows is not repeated in the foot (Alex 10-21, confirmed 11-12 "Ja bitte").

- D17 (Alex 11-12, "Ja", shown A1200 and C128 Home at 480p): the reset rows read the same on every core, sentence case:
  "Reset" and "Reload <ROM name>, reset"; only the ROM name ("Kickstart", "ROMs") comes from the core's .toml.

## §4 Out
- Anything on F1 beyond D3 until new users' feedback.

## §5 Build notes
- 2026-11-12 (agent, stopped before code, nothing built): questions that are not in D1-D3 - Q1 what RETURN does on an empty drive row and where
  the tasks that fit no drive row go (Reset Drive, Save Disk as D64/G64, Mount folder as HD0/HD1, Create blank D64/G64/D81 on C128); Q2 the F7
  label ("APP" proposed, no name in code or ux skill); Q3 the Core / Network / Media info lines (not named in D3).
- 2026-11-12 (D8-D11, D14 built): a Task's `group` places it in the default route: "top" = Home beside the resets (snes Sound check; the NMI closes the menu and is
  there already), "machine" = F5's first tab (zxnext 50 / 60 Hz, CPU speed), "input" = F5 Input (pad maps of snes, nes, neogeo). `MediaSlot::eject_closes` makes the
  list's Eject close the menu (atari cartridge; the task "Remove cartridge, cold start" is gone). D9 and D10 lists (snes/neogeo cartridge, cpc/zx/pet tape) were
  already in the descriptors; test_desc now pins them. D14: the core shows No/Yes, `Settings::load` reads the host's Disabled/Enabled as No/Yes.
- 2026-11-12 (D12, D13 built): D12: `ui.cc build_route` draws no tab strip for the F7 area when it has one visible tab (the framework did not hide single-tab strips anywhere
  else; Home and Library keep theirs, D12 names only APP); the route heading "Keys" is now "Hotkeys"; the drive list's title was already the slot name (`open_drive`).
  D13: `CoreDesc::machine_info` set per core, from existing settings only: PET Model,RAM; VIC-20 Memory Expansion; C264 Model,RAM; C128 "40/80 DISPLAY";
  SCPU "SuperCPU speed"; Amiga Chipset + RAMs; Atari RAM size,Video standard; CPC Model; ZX Model; NES Console type; Neo Geo System type,BIOS. No line: Apple IIe,
  ZX Next, SNES (no model or memory setting), TRX64U (C64 cores untouched). Open: C128 C64 mode and SCPU SuperRAM are no settings (core state). test_desc pins the table.
- 2026-11-12 (render bench, R8, pictures): the core render bench follows the layout (Reset on F1, storage/menu rows in F7 under Storage/Menu, hotkey page "Settings > Hotkeys",
  F8 closes with Status: Build, ROMs, Libraries behind the Reset section, "Create blank" through the drive list; each check asserts the new place). A job started from a drive
  list runs on Home: Home now retexts a changed row in place (`ui.cc`, as the lists do) - without it every tick rebuilt the panel (T167 probe: 119k foreign pixels). Home's last
  row is checked inside its panel at 480p (A1200, 10 rows: y 423 of 443). R8: entering/leaving F7 redraws 518,400 px (strip hidden, pane height changes) = about 1.1 s at the
  board's 2.2 ms per 1,000 px, against 396,219 px (about 0.87 s) for any other area switch: +0.27 s on two transitions. Pictures: tests/ui/features_*/t212_*.feature.
  UX review (A1200, C128, VC-20; 720p mostly): FAIL until F8 Status is seen in a picture and Home after mount/eject is pictured; polish list in the report.
- 2026-11-12 (review fixes): F8 pictures were not stale: `list_window` cut trailing rows with `m.heading()`, which is also true for an information line (R10), so Status (Build, ROMs, Libraries) was cut as "headings at the window's end", the window counted shorter than the list (so "+/- PAGE" showed for one page) and the counter said 1-5 of 5; now `MenuCtl::title_row` (heading proper only) decides both, and an information line is drawn as a row with its value (it was drawn as a quiet heading, value dropped). Home panel is "Session" at every size (fits 480p). Drive page title drops the trailing colon of the slot name ("Drive A"). New pictures `t212-<core>-f1-ejected` (Eject from the drive list, toast in the status chip) at 480p/720p/1080p; the three T212 features run at each mode with `UI_VIDEO`. keywalk golden regenerated (Home panel title).
- 2026-11-16: merged main, ue2emu pictures, start script. `git merge main` (463 commits). Conflicts and how both sides hold: `desc.h` keeps `no_tone`, `rom_key` (T189, T216 D16) and `machine_info` (D13/D15), in that order
  (positional initialisers: `neogeo` `0, true, 0, "System type,BIOS"`, `pet` `0, false, pet_rom_key, "Model,RAM"`, `c128`/atari/c264/cpc/nes/scpu/vc20/zx got `false, 0` before their `machine_info`);
  `menutree.cc` settings-tab task loop = T226/T216 `task_in_settings_tab` OR T212 D8 group rule (one add, no duplicate), `dialog` flag (T216 D11), the page row after the tasks (D12);
  `settings.cc` keeps `ovr_` (T189) and `hix_` (D2/S9); `menuctl.cc` foot: main's ".." / folder keys plus the drive-mount line (`mount_slot_`); `snes.cc` Sound check "top" (D8); `fixtures.py` stick for vc20 + c64scpu (T217);
  `TODO.md` main's rows. Goldens (`ids`, `keywalk`, `menu_*`) taken from the tree and regenerated with `UPDATE_GOLDEN=1 CA_GROUP=cross tests/coreapp/run.sh` on the builder (the one run; stopped when the goldens were written, controls and other tests not run).
  Merge bug found in the pictures and fixed: F7 showed a stray "Hotkeys" heading above Storage (the T212 heading loop met T216 D12's deferred page row); the heading now stands directly above the Hotkeys row. RISC-V build ok.
  T228 overrides T212 only where T228 itself says (not built here): D4 drops the rail heading "SHIFTED" (still drawn in these pictures), D12 adds "Keys and help" to F8 Support, D18 turns F8 into the Network tab with sections; the T212 areas stay as D2 says.
  ue2emu pictures (real core app, 720p + 1080p, amiga1200 and c128: F1, drive lists, F3 mount, F5, F7, F8, eject toast): `t212-*` in scratchpad `t212emu/{720p,1080p}/`. ux-review verdict by the coordinator's reading of the pictures (R1, R2, R5, R7): PASS; open, outside T212: the "SHIFTED" heading (T228 D4).
  Interactive start: `bash <scratchpad>/t212emu/start.sh amiga1200` (window, real time; kit with the app image next to it).
- 2026-11-16 (Alex approval): checked by Alex in ue2emu with `scratchpad/t212emu/start.sh` (translated): "Menu from T212 is ok, feels good." Approved in ue2emu; merged to main; board step = with the next package.
- 2026-11-16 (review fix): the C264 cartridge slot named its mounted task "Eject cartridge, reset"; `groups_have` splits task lists on ',', so the task never matched
  the drive and stayed a plain Home task (golden `menu_c264` had captured it). Renamed "Eject cartridge and reset" (task, slot list, manual, mock-up); the slot has
  the task in its Cartridge drive list now. Every other `empty_tasks`/`mounted_tasks` label (all cores, `cbm1541_desc.h`) was checked: none has a comma.
- 2026-11-16 (run 2315 red, coreapp_render 3 checks): not a product bug. `ux_chrome_check` page legend: the bench took the first row whose value ends in ">" for a page; since T212 D8 zxnext "F3: 50 / 60 Hz" (a key task, group "machine") sits in F5 and shows ">" (T216 D11), RETURN runs it and the menu stays on the area legend. The bench now skips a row whose RETURN leaves a dialog, a wizard or a toast (as the check at the T216 D11 pages loop already did). "UX WiFi row: 8 variants, 3 failed" is the same 3 (the line prints the running failure count). Goldens moved: `ids`, `keywalk`, `menu_c264` (c264 eject task is the Cartridge drive's now).
- Board (Board test 2026-11-16, package main-13c2cde9 (run 2344), Alex in the test list): core menus F1-F8 PASS, Alex: "F1 macht so viel mehr Sinn, wahnsinnig gut !" (F1 makes so much more sense, incredibly good).
- c264 "Eject cartridge and reset" board step (same test list): not tested - Alex has no Plus/4 or C16 cartridge image ("hab ich nicht, also ein CRT für Plus4"); stays open until one is at hand.
- D18 (Alex 2026-11-15, Home cursor never on the STATUS rows) is superseded by D7: the new F1 has no STATUS rows, so the cursor cannot rest there. Board 2026-11-16 (C128, Amiga), Alex: "Ja, es ist so viel besser alles. F1 ist jetzt ein sinnvolles Menu. Passt, mach das 212 d18 weg" (yes, everything is so much better, F1 is a sensible menu now; drop T212 D18). Its branch `home-focus-media` (78c4f545b) is not merged; kept on Gitea as refs/backup/home-focus-media.
