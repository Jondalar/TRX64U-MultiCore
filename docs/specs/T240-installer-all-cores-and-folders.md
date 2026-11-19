# T240 — The installer puts every core it finds into the boot menu, up to 32, and makes each core's folder with roms/, saves/ and configs/

**Status:** BUILT on branch `t240-installer-cores` (2026-11-19); emulator run and board S4 open. PROPOSED 2026-11-19 (Alex).
**Repo:** TRX64U-MultiCore — the TRX updater `vendor/gideon/software/application/update_u2p/update_trx.cc` (built by the CI
stage `update` into `trx64host_update_<id>.ue2/.cfw`). No gateware; no `coreapp/` or `uicore/` code (T192 D24, T213 D4).
**Number:** T240.
**Origin:** GitHub issue #107 (a tester, release 0.2.0, C64U): "TRX64 installer only adds first 16 cores" - "the installer reads the
cores and sets up the initial boot menu, and it appears to only add 16 ... when it should add all of the cores (or at least only
stop at 32)". Alex, 2026-11-19 (translated): "#107 apparently has 16 cores for the initial boot menu fill. And while he is at it,
he should please also create the folder structures for every core found at install, the one for the ROMs too."
**Depends on:** T223 D1 (the boot menu holds 32 entries), T148 (the core folder), T135 (the installer writes boot.cfg).
**Milestone:** after release 0.2.0.

Open (one line, as in the TODO table): Emulator S1-S3 PASS 11-19 (s13-s16, control s16 shows #107); board S4 open

---

## §1 The problem

- **Who:** a person who installs the MultiCore host from a stock firmware with `trx64host_update_<version>.ue2` and a folder of
  cores (release 0.2.0: 20 `.cor` and 4 `.u2a` apps).
- **What happens today** (read 2026-11-19):
  - The updater writes `Flash:/boot.cfg` from the cores it finds (T135). Its table holds `BOOT_ENTRIES` entries, and
    `update_trx.cc:1053` defines `BOOT_ENTRIES 16` with the comment `// bootcfg.h BOOT_MAX`. The host's `BOOT_MAX` is 32 since
    T223 D1 (`host-app/software/hostapp/bootcfg.h:23`, 2026-11-15); the updater's copy was not raised. `cfg_add()` stops at the
    16th entry (`update_trx.cc:1640`).
  - The updater collects the files of the bundle folder into `bundle[BUNDLE_MAX]` with `BUNDLE_MAX 24` (`update_trx.cc:1052`,
    `find_bundle()` at 1415-1451). The 0.2.0 package alone is 24 such files (20 `.cor`, 4 `.u2a`); a folder that also holds other
    `.cor`/`.cfw` files (the reporter of #107 kept several Commodore firmware files there) loses files before the boot menu fill.
  - A core's folder (T148: `<dir>/<name>/` beside `<dir>/<name>.cor`, with `roms/`, `saves/`, `configs/`) is made by the core
    application at the core's first start (`coreapp/src/corefolder.h`, `corefolder_ensure()`). After an install there is no
    folder to put a core's ROMs in before its first start.
- **Why it matters:** cores beyond the 16th have to be added by hand after the install (#107); a person who wants to put ROMs in
  place first has to start every core once or make the folders by hand.
- **Parts:**
  - **P1** the boot menu fill takes every core found, up to the host's 32 entries.
  - **P2** the bundle scan takes every file of the bundle folder the fill and the copy need, not the first 24.
  - **P3** the install makes, for every core it finds, the core folder with `roms/`, `saves/` and `configs/`, as
    `corefolder_ensure()` does; what exists stays.

## §2 Scenarios

| # | Part | Scenario | Proof |
|---|---|---|---|
| S1 | P1 | A bundle folder with 20 `.cor` and 4 `.u2a`: `Flash:/boot.cfg` gets the backup entry of the old firmware (always the first, T135) plus 20 core lines; the apps are no entries. With 33 `.cor` the menu is full at 32 and the screen says that 2 were not added | the updater emulator run (`tests/updater/emu_t135.sh` s13, s15; control s16 with the updater before T240) |
| S2 | P2 | The same folder plus three firmware files (`.cfw`) of other vendors: still every core is found and added | updater emulator run |
| S3 | P3 | After the install every found core has `<dir>/<name>/`, `roms/`, `saves/` and `configs/`; a folder or a file that was there stays as it was | updater emulator run, listing of the medium afterwards |
| S4 | P1-P3 | Board: install the next release's `.ue2` from a stock firmware with the release folder: the boot menu lists every core, the folders exist | Alex on the board: the boot menu, and the file browser shows the folders |

## §3 Decision

- **D1 (Alex, 2026-11-19): the installer adds every core it finds to the boot menu, up to the host's 32 entries (#107).**
- **D2 (Alex, 2026-11-19): while installing, the installer makes the folder structure of every core it finds, the ROM folder
  included** ("die Ordner Strukturen anlegen für jeden gefundenen Core beim Install, also auch den für die Roms"). The layout is
  T148's (`roms/`, `saves/`, `configs/`), the same set `corefolder_ensure()` makes.

## §4 Out

Nothing decided yet.

## §5 Built

2026-11-19, branch `t240-installer-cores`. Not built, not run: the RISC-V build and the emulator run are the coordinator's.

- **P1** `update_trx.cc:1053` `BOOT_ENTRIES 32` (comment now true: bootcfg.h BOOT_MAX, T223 D1). The text buffers of `write_boot_cfg()`
  were 2048 bytes: 32 lines of about 90 bytes do not fit, so `text` and `back` are `BOOT_TEXT_MAX` = 32 x 200 + 128 = 6528 bytes each
  (`:1054`, `:1662`, `:1681`; the host's own buffer is `BOOT_MAX * 260 + 128`). `cfg_add()` counts what it cannot take in `cfg_dropped`
  (`:1644-1649`); after the `boot.cfg written` line the screen says `n core(s) not added: the menu holds 32.` (`:1886`). The backup `.cor`
  is entry 1, so 33 `.cor` files in the folder give 32 lines and 2 not added.
- **P2** `BUNDLE_MAX 64` (`:1052`). RAM: `sizeof(bundle_file)` = 64 + 4 + 1 + 1 padded = 72 bytes, 64 entries = 4608 bytes of static
  RAM (was 24 = 1728; +2880). `find_bundle()` no longer drops files silently: a matching file with no room is counted in
  `bundle_left_out` (`:1451`) and the screen says `n more file(s) left out: the updater takes 64.` (`:1741`). A 0.2.0 folder (24 files)
  plus other vendors' files fits up to 64 files together.
- **P3** `make_core_folder()` (`:1694`) makes `<dir>/<name>/`, `roms/`, `saves/`, `configs/` with `FileManager::create_dir()` (FR_EXIST is
  fine: what exists stays), the set `corefolder_ensure()` makes. It is called for the backup `.cor` (`:1823`) and for every copied `.cor`
  of the bundle, also a large-core `.cor` and one whose copy was `same` (`:1861`); not for a `.cfw` (no `.cor` extension: the core
  application's `corefolder_dir()` refuses it) and not for a `.u2a`. A file whose copy failed gets none. A folder that cannot be made is
  named on the screen and the install goes on; the line `Core folders: n, m could not be made` follows the file summary (`:1871`).
  No `coreapp/` or `uicore/` code is linked or changed.
- A `.cfw` of another vendor is still a boot entry (as before T240; `cfg_add()` for every non-`.u2a` file) and counts against the 32.
- Manual: the menu size 16 -> 32 in 3.3.2, 5.x table, 6.5.1, 8.9.3; 2.4.4, 2.4.6 and 2.7.1 say what the installer makes.
- Tests: `tests/updater/emu_t135.sh` s13-s16, `tests/updater/emu_check.py` mode `t240`; dummy files are made by the script (any bytes; the
  menu name is the file name).

| # | Proof written | Status |
|---|---|---|
| S1 | s13 (20 `.cor` + 4 `.u2a`: 21 lines = backup + 20) and s15 (33 `.cor`: 32 lines, screen `2 core(s) not added`); control s16: the pre-T240 updater gives 16 lines and the T240 checks must fail on it | written, not run |
| S2 | s14 (20 `.cor` + 4 `.u2a` + 3 `.cfw`: all 20 copied and in the menu, 24 lines); the pre-T240 updater (24 files) is the control through s16 | written, not run |
| S3 | s13 (SD with `cores/dummy01/roms/keep.bin`: the file stays, the other subfolders are made; folders of all 20 and the backup), s14, s15 (folders for all 33, also those not in the menu), none for `.cfw` | written, not run |
| S4 | board, Alex | open |
- **Review 2026-11-19 (agent, before the merge):** no blockers. Fixed: the manual's "up to 24 files" (now 64), the comment of
  `tests/hostapp/test_startcheck.cc` ("16 entries" -> BOOT_MAX), the "Core folders" line only when a folder was made. Known and
  older than T240: when the bundle folder is the cores folder itself, the backup `.cor` can be met twice (a second boot entry, a
  second folder count); the emulator scenarios do not reach it.
- **Emulator run 2026-11-19 (coordinator):** `tests/updater/emu_t135.sh` with the emu-test `.ue2` of this branch over the v0.2.0
  package (`null_full.bit`, `hostapp.app`), Commodore 1.1.0 as the vendor firmware: every scenario s1-s16 passes, "emu_t135: 0
  scenario(s) failed", 351 checks ok. The control s16 (the updater of main before T240) writes 16 lines and makes no folder: the
  fault of #107 reproduced. Found during the run: the copy phase's count lines stay on the screen only milliseconds before the
  flash phase clears it, so an expect saw them only now and then and a person would not see them at all. The final screen now
  repeats them: "Boot menu: n entries", "n core(s) not added: the menu holds 32." (red) and "Core folders: n, m could not be
  made"; the scenarios read the final screen. S1-S3 proven in the emulator; S4 (board) open.
