# T212 - Core menu areas: F1 quick actions, F5 machine, F7 app settings

Status: PROPOSED 2026-11-12, Alex (board photos of the A1200 core app: F1, F5, F7).

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

## §4 Out
- Anything on F1 beyond D3 until new users' feedback.

## §5 Build notes
- 2026-11-12 (agent, stopped before code, nothing built): questions that are not in D1-D3 - Q1 what RETURN does on an empty drive row and where
  the tasks that fit no drive row go (Reset Drive, Save Disk as D64/G64, Mount folder as HD0/HD1, Create blank D64/G64/D81 on C128); Q2 the F7
  label ("APP" proposed, no name in code or ux skill); Q3 the Core / Network / Media info lines (not named in D3).
