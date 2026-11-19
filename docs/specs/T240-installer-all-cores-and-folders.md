# T240 — The installer puts every core it finds into the boot menu, up to 32, and makes each core's folder with roms/, saves/ and configs/

**Status:** PROPOSED (2026-11-19, Alex).
**Repo:** TRX64U-MultiCore — the TRX updater `vendor/gideon/software/application/update_u2p/update_trx.cc` (built by the CI
stage `update` into `trx64host_update_<id>.ue2/.cfw`). No gateware; no `coreapp/` or `uicore/` code (T192 D24, T213 D4).
**Number:** T240.
**Origin:** GitHub issue #107 (a tester, release 0.2.0, C64U): "TRX64 installer only adds first 16 cores" - "the installer reads the
cores and sets up the initial boot menu, and it appears to only add 16 ... when it should add all of the cores (or at least only
stop at 32)". Alex, 2026-11-19 (translated): "#107 apparently has 16 cores for the initial boot menu fill. And while he is at it,
he should please also create the folder structures for every core found at install, the one for the ROMs too."
**Depends on:** T223 D1 (the boot menu holds 32 entries), T148 (the core folder), T135 (the installer writes boot.cfg).
**Milestone:** after release 0.2.0.

Open (one line, as in the TODO table): PROPOSED 11-19 (Alex, #107): boot menu fill up to 32, BUNDLE_MAX, core folders at install

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
| S1 | P1 | A bundle folder with 20 `.cor` and 4 `.u2a`: `Flash:/boot.cfg` gets 20 core lines; with 33 `.cor` it gets 32 and the updater says that one was not added | the updater emulator run (`tests/updater/emu_t135.sh` or its successor) with such a medium, `boot.cfg` read back |
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

Nothing yet.
