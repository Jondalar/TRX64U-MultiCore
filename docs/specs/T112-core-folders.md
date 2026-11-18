# T112 — Every core lives in its own folder on a medium, with its .cor, its settings and its ROMs

**Status:** BUILT for layer-2 cores (2026-10-26), host benches only; board step S1 open; S2 (large .cor) closed as done 2026-11-18 (shipped and in use, §5). D1 and Q1 decided by Alex. 2026-11-13 status check: merged to main (Merge t112-folders); D1-D3 superseded by T148; state as in row: built, board S1/S2 open (S2 since closed, 2026-11-18). **2026-11-15 sync:** T148 (core folder, done by Alex 2026-11-14: "proven in daily board use (core folders)") is the successor of this spec; no board result for S1/S2 is recorded under T112 itself, so whether T112 closes as superseded is Alex's call (listed in the sync report).
**Repo:** TRX64U-MultiCore — the host app (`host-app/software/hostapp/discover.cc`) and the generic application
(`coreapp/src/romfind.cc`); no gateware, the host stays frozen.
**Number:** T112 (registry: `TODO.md`).
**Origin:** Alex, 2026-10-19, while setting up the new exFAT stick on USB1: "I almost think there should be one directory
per core with .cor, config, ROMs ... so that it is contained. But it doesn't have to be, of course, we are just very chaotic"
(translated).
**Depends on:** T104 (generic application), T101 (host app finds `.cor` files itself).
**Milestone:** M3.

---

## §1 The problem

Alex sets up a medium on the Mac and wants every core on it to be one self-contained unit he can copy, move or delete.

What the code does today:
- **P1 Finding cores.** (Superseded 2026-10-25, T101 S10: the host app searches nothing any more; the boot menu lists what the user added, the
  Add core browser walks folders and lists `.cor`/`.cfw` in any depth.) Before S10 the host app looked in every root of `/` and in a folder `cores` directly below it, nothing deeper
  (`discover.h:17`, `discover.cc:44`). A `.cor` in `/USB1/cores/apple2/` is not found.
- **P2 Settings.** A core's settings file is `<name>.cfg` beside its `.cor` (`coreapp/src/fsif.h`). This already follows the
  `.cor` and needs no change.
- **P3 ROMs.** The generic application looks in the board's ROM directory first (the monolith's `/flash/roms`), then in
  `<root>/roms/<folder>` of `/flash`, `/usb0`-`/usb3`, `/sd` in that order, and the first hit wins (`romfind.cc:6`, `:30`).
  The medium the `.cor` came from has no precedence: with two different Kickstarts on USB0 and USB1, USB0 always wins.
- **P4 Media and the old stick.** The old stick (FAT32) has `/USB1/cores/*.cor` and `roms/<core>` at its root. It must keep
  working.

## §2 Scenarios

```gherkin
Scenario: S1 a core in its own folder is found and boots with its own ROMs
  Given /USB1/cores/apple2/ holds apple2.cor and roms/apple2e.rom, apple2e-video.rom, diskii.rom, and no other medium has them
  When  Alex starts the Apple IIe from the host menu
  Then  the host menu lists it, the console names /USB1/cores/apple2/roms as the place the ROMs came from, the machine boots
  And   apple2.cfg is written into /USB1/cores/apple2/

Scenario: S2 the core's own folder wins
  Given a Kickstart 3.1 in /USB1/cores/amiga1200/roms/ and a Kickstart 1.3 in /USB0/roms/amiga1200/
  When  the A1200 starts
  Then  the console names the 3.1 file in the core's folder

Scenario: S3 the search over all trees stays as the fallback
  Given /USB1/cores/apple2/ has no roms/ folder, /USB1/roms/apple2/ has the three files
  When  the Apple IIe starts
  Then  it boots with the files from /USB1/roms/apple2/

Scenario: S4 the old layout keeps working (negative control for the ordering: the core's folder is empty)
  Given the old stick: /USB1/cores/trx64.cor at the cores level, ROMs in /flash/roms
  When  the host scans and TRX64 starts
  Then  it is listed and boots as before
```

```gherkin
Scenario: S5 a large .cor of ours follows the same logic (Q1 a)
  Given /Usb1/cores/trx64/trx64.cor is a large .cor with roms/kernal.bin beside it, and /flash/roms/kernal.bin is another file
  When  the host starts it (legacy_stage fills the hand-over page with the path) and the application comes up on the monolith profile
  Then  the application takes the ROM from /Usb1/cores/trx64/roms, keeps trx64.cfg beside the .cor, and with an empty roms/ it finds /flash/roms
  And   a /flash/trx64u/c64.cfg of an earlier start is copied to trx64.cfg once and stays where it was

Scenario: S6 a .cfw stays on the flash layout (negative control: the page of an earlier start is wiped)
  Given a .cfw is started after a large .cor
  When  the application comes up
  Then  the hand-over page is wiped, the application has no .cor path, /flash/roms and /flash/trx64u/c64.cfg as before
```

Benches: `tests/coreapp/` host tests for the search order (S2, S3, S4 against a fake file system), a `discover` host test
for one level deeper (S1, S4); the board for S1.

## §3 Decision

**Superseded 2026-10-28 by [T148](done/T148-core-folder.md)** (one folder per core beside the `.cor`, no shared files).


- **D1 (Alex, 2026-10-19):** "No, then we do it beside the core. That would be good for the generic app. The search over
  several trees is good, we keep that. But the first location is then beside the core" (translated). The ROM search starts
  in `roms/` of the folder the `.cor` lies in; the existing search over `/flash/roms` and `<root>/roms/<folder>` of every
  medium follows unchanged.
- **D2 (Alex, 2026-10-23, T104 5.13):** "It already lies elsewhere :-) from the C64" (translated). A ROM of a shared device (the 1541 drive ROM,
  `RomSpec::shared`) is searched in the core's own folder first, then in the shared `<root>/roms` of every root, same root order. A machine
  ROM (KERNAL, BASIC ...) is never taken from `<root>/roms`.
- **D3 (superseded the same day by T148)** **Search order (Alex, 2026-10-28, translated, replaces the root order of D1):** "It should search for ROMs in this
  order: 1. the place of the .cor -> under the folder named after the core, in it roms, or ./roms/ as a folder; 2. the
  medium the core is on may have something in ../roms, go into the subdirectories there; 3. /flash; 4. other media, i.e.
  those the started .cor is not on." So: (1) `roms/` beside the `.cor` (in the core's own folder); (2) `<medium of the
  .cor>/roms/<folder>/`; (3) `/flash/roms/<folder>/`; (4) `<other medium>/roms/<folder>/`. Today the order is `/flash`,
  `/usb0-3`, `/sd` after (1), so a core started from the SD looks at the sticks before its own medium.
- Layout: `<medium>/cores/<core>/<core>.cor`, `<core>.cfg` and `roms/`. Games, demos, images and tools may lie anywhere.

## §4 Out

- Moving the C64 ROMs of TRX64 out of `/flash/roms`: they stay, the fallback finds them.
- Writing ROMs into the flash from the app.

## Q1 Decided (Alex, 2026-10-26): our large cores follow the same logic as the partial cores

A large `.cor` (T115 §7) and a `.cfw` start through `legacy_stage`/`legacy_jump`, which writes **no hand-over** (`legacy.cc` has no `handover_fill`):
the application that comes up knows neither the folder of the file nor a `.cor` path, so it runs with the monolith board profile
(`/flash/roms` first, settings in `/flash/trx64u/c64.cfg`, T104.2 Q1). D1 therefore cannot apply to them as built. Options: (a) the legacy start
also fills the hand-over page with the path, the application then searches `<folder>/roms` first and keeps `<name>.cfg` beside the file, for a
monolith profile too; (b) leave large cores as they are (flash layout), folders are for layer-2 cores only.

Alex, 2026-10-26: "No, honestly our own large cores should follow the same logic as the partial cores. So a), and if there is
nothing in the local folder, then the search cascade." (translated). **Decision: (a)** for our own large `.cor` files: the legacy start
fills the hand-over with the path, the application searches `<folder>/roms` first, then the existing cascade (`/flash/roms`, the roots),
and keeps `<name>.cfg` beside the file. A `.cfw` is not one of our large cores and is not covered by this decision.

## §5 Built (2026-10-26)

- **ROM search (D1, D2).** `romfind.cc`: `rom_own_dir(cor_path, out, n)` turns `<dir>/<name>.cor` into `<dir>/roms`; `rom_scan(..., first_dir, own_dir)`
  lists it first, before `first_dir` and the roots, the files in it as they are (no `<folder>` level). A `.cor` at a medium's top
  (`/usb0/x.cor`) has no own folder: `<root>/roms` is the shared folder and a machine ROM is never taken from it. A wrong-size or refused file in the
  own folder is remembered for the message only, a right one further on still wins. A shared device ROM (`RomSpec::shared`) finds the own folder first
  by the same list, then `<root>/roms`. The "not found" sentence names the own folder. The four callers (`boot.cc` twice, `libboard.cc`, `run.cc`) pass
  `rom_own_dir(c->cor_path)`; `romset.cc` (ROM sets, e.g. Kickstart choice) lists the own folder first too (4 lines). No `.cor` path (JTAG start, monolith):
  as before.
- **Settings (P2).** Unchanged: `handover_cfg_path` gives `<dir>/<name>.cfg` beside the `.cor`, so a core in its own folder keeps `.cfg` in that folder.
  A `.cor` moved into a new folder starts with the defaults and the old `<name>.cfg` stays where it was: nothing is deleted or migrated, the user
  moves the `.cfg` with the `.cor`.
- **Host app.** Nothing to change: the browser already enters folders, `boot.cfg` stores the full path (`core=/Usb0/cores/apple2/apple2.cor|...`), the
  start hands that path over. Paths in the web page's table are cut at 96 characters (`WEB_PATH_MAX`).
- **Old layout (S4, P4).** `/USB1/cores/trx64.cor` has the own folder `/USB1/cores/roms`; if it does not exist, the roots are searched as before.
- **Benches.** `tests/coreapp/test_romfind.cc`: S1 (own folder, and not found without it), S2 (beats `first_dir`, `/usb0`, `/flash`), S3 (fallback),
  S4 (old layout, medium top), wrong size beside the core does not shadow, message names the folder, D2 shared order; `test_handover.cc`: `.cfg` in the
  core's folder. Controls in `tests/coreapp/run.sh`: `rom_own_off` (own folder not listed), `rom_own_top` (medium top accepted).
- **Large `.cor` (Q1 a).** Host: `cfw_handover_page` (`cfw.cc`) builds the page, `legacy_stage` copies it to `HANDOVER_ADDR` after the flash file check. The page
  goes through `cfw_stage_copy` under the core's map, so the application reads it correctly with `gideon_bl4` too. Content: the path as the host opened it,
  video 0, the manifest's name as the core name (corrected 2026-10-26 after the board: with the name empty, the A1200 - whose ID AMIG the A500 shares - stayed held, "no bundle to say which"; `start_identify` picks a shared-ID descriptor by this name), version and host key empty. A `.cfw`
  gets a wiped page (S6). `host-app/` only, `host/` and `contract/` untouched. Application: `board_detect` believes a valid page on the monolith
  profile too (`has_handover`; ROM and settings directories of the profile stay), `start_paths` then takes `cor_path` and `<name>.cfg` from it,
  `rom_scan` order is own folder, `/flash/roms`, roots (S5). `start_migrate_cfg` (run.cc after `start_paths`) copies `/flash/trx64u/<folder>.cfg` to the new `.cfg`
  once when that does not exist; the original stays.
- **Benches (Q1).** `tests/hostapp/test_cfw.cc` (page of both maps, large and `.cfw`; controls `cfw_hand_unmapped`, `cfw_hand_for_cfw`, `cfw_hand_nopath`);
  `tests/coreapp/test_board.cc` (monolith with a valid and an invalid page, paths, ROM order, the settings copy; controls `board_handover_mc_only`,
  `board_migrate_overwrite`, `board_migrate_move`). The old control `board_handover_trusted` is the product now and is replaced by `board_handover_mc_only`.
- **Not measured, board step S2 (Alex; closed as done 2026-11-18, §5):** that DDR2 at `HANDOVER_ADDR` survives the IPROG and the large core's own start (the monolith's cartridge ROM
  area lies there; the boot magic at 0xFFF8 survives the same way) and that the application finds a valid page. If the large core's boot clears it,
  the application falls back to the flash layout as before. Q1 (a) is built, the board proves it.
- **Not built:** `.cfw` folders (decided out); the board step S1 (S2 closed 2026-11-18, next entry).
- **2026-11-18 - board steps closed as done (shipped and in use):** S2, the core folder's Kickstart wins (with it the large .cor's hand-over
  that "Not measured, board step S2" above left to the board) - Alex 2026-11-15: "Under A there is nothing I would not class as DONE! That has
  been in the builds for days/weeks, we had bugs and re-tests in part"; Alex 2026-11-18, shown these steps as open: "that is old stuff that has
  been running for ages, we had this already 3-4 days ago" and "tick it off in the TODOs/specs, otherwise it shows up again with some agent"
  (translated).
