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
(to be written with the design; every one names its ue2emu picture or test)

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
