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

## §4 Out
- Anything on F1 beyond D3 until new users' feedback.

## §5 Build notes
