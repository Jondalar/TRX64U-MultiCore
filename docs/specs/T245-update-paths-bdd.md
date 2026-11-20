# T245 — Both update paths run as BDD features in ue2emu with our own builds: the updater from the stock firmware, and the host's `.txu` update

**Status:** PROPOSED (2026-11-20).
Open (one line, as in the TODO table): PROPOSED 11-20 (Alex); questions Q1-Q5 open
**Repo:** TRX64U-MultiCore — the features and their runner under `tests/` (the updater and the host app in ue2emu). No product code.
The emulator is UE2 (`u64-emulator`), specs S46 and S47, release 0.7.4.
**Number:** T245.
**Origin:** Alex, 2026-11-20 (translated): "The emulator session extended the emulator so that we can test both update paths with
BDD tests too -> write a spec?" He then approved the night plan that included this spec: "A fits".
**Depends on:**
- UE2 S46 (`run --firmware X.txu`, `install --update X.txu`) and S47 (`--board c64u`: the W25Q128JV and the XC7A50T, the model
  of the flash lock). Both are BUILT in ue2emu 0.7.4 (2026-11-20).
- T135 and T132: the updater (`vendor/gideon/software/application/update_u2p/update_trx.cc`) and its emulator flow
  `tests/updater/emu_t135.sh`.
- T152: the flash lock (`host-app/software/hostapp/flashlock.cc`).
- T240: 32 boot entries, 64 bundle files, the core folders.
- The host's `.txu` update: `host-app/software/hostapp/txu.cc` (`txu_probe`, `txu_stage`) and `flashfw.cc` (`fw_write_parts`).
**Milestone:** M1.

---

## §1 The problem

- **Who:**
  - A tester who installs or updates MultiCore on a C64U or a U64 Elite II.
  - Alex, who must know before a release that both paths still write the flash correctly.
- **The use case, in parts:**
  - **P1 The first install.** On the stock firmware (Commodore 1.1.0 or Gideon 3.15) the tester starts the updater `.ue2` of a
    package from a USB stick (File Browser, Run Update). The updater checks the flash chip and the FPGA type. It writes the host
    bitstream at 0x000000 and the application at 0x220000. It shows "Update OK" and asks for the power-off.
  - **P2 The way back.** The restore bundle `TRXBK_<date>_nnn.CFW`, which the installer writes, runs through the same Run Update.
    It puts the previous flash content back (manual 14.3).
  - **P3 The update from the host.** The host app opens a `.txu` in the File Browser (U, Y). It checks the package
    (`txu_stage`) and writes its parts (`fw_write_parts`). A package that is already installed is reported as installed.
  - **P4 The flash lock (T152).** Before a large core starts, the host locks 0x000000-0x3FFFFF (status `34 01`). A write there
    is ignored until the power goes off.
  - **P5 The FPGA size (cross-cutting).** The same packages on a 100T. T53 was lifted on 2026-11-20.
- **What happens today:**
  - P1 and P2: `tests/updater/emu_t135.sh` runs only an `--emu-test` build of the updater (`TRX_EMU_TEST`). It runs on the
    emulator's older flash model (S25FL128L), by hand on the Mac, and not in CI. The release updater refused the emulated
    machine, because it wants the 50T type and a W25Q (S46 §1, S47).
  - P3: ue2emu could not load a `.txu`. Its `load_firmware` knew ELF, `.app` and `.ue2` only (S46 §1). The host's `.txu`
    path is proven on the board only (package A `t239.1-0210226e`, 2026-11-20).
  - P4: the lock is read on the board only.
  - ue2emu 0.7.4 now has all of it:
    - `--board c64u` gives the W25Q128JV (JEDEC `ef4018`) and FPGA type 2;
    - `run --firmware X.txu` boots the host from a package;
    - `install --update X.txu` writes a package as the host app does;
    - the W25Q model keeps the T152 lock until the next run.
  - The emulator's own tests pass: S46 T1-T3 and S47 T1-T6. The tests with our builds are ours: S46 T4 and T5, and S47 T7.
- **Why it matters:**
  - Both paths write the flash of a tester's machine. A fault there leaves a machine that needs the Blaster or a recovery.
  - Today each release is proven by hand on Alex's board.

## §2 Scenarios — what proves it is solved

Each scenario is one feature in ue2emu with the builds of the commit under test. The flash image is read back after the run.

```gherkin
Scenario: S1 (P1) the first install from Commodore's firmware 1.1.0
  Given a flash written by `install --board c64u --update c64u_v1.1.0.ue2` and a USB stick with the package's updater and cores
  When the release updater (not an --emu-test build) is started from the File Browser, as a person starts it
  Then it passes the flash and FPGA checks, the flash holds the package's bitstream at 0x000000 and its application at
       0x220000 byte for byte, and the screen says "Update OK" and asks for the power-off
  And no byte outside the regions the updater names has changed

Scenario: S2 (P1) the first install from Gideon's firmware 3.15
  Given a flash with 3.15 (`ultimate.elf`, its config pages at 0xFF0000) and the same USB stick
  When the release updater is started from the File Browser
  Then the result is the one of S1, and the config pages of 3.15 are unchanged

Scenario: S3 (P2) the way back with the restore bundle
  Given the flash after S1 and the restore bundle the updater wrote in S1
  When the restore bundle is started with Run Update and the tester says yes to the configuration
  Then the flash holds the content from before S1 in the slots and in the config pages, and the screen says "Update OK"

Scenario: S4 (P3) a .txu from the File Browser writes what `install --update` writes (S46 T5)
  Given packages A and B (host and app) from tools/txu_pack.py, and the flash F that `install --update A.txu` makes
  When the host app runs on a copy of F and installs B from its File Browser (U, Y), which gives F1
  And `install --update B.txu` runs on another copy of F, which gives F2
  Then F1 and F2 are equal byte for byte in [0x000000, 0x400000)

Scenario: S5 (P3) an app-only package
  Given package C with the application only
  When it is installed as in S4
  Then F1 and F2 are equal in [0x000000, 0x400000), and the host region is unchanged

Scenario: S6 (P3) a package whose check fails writes nothing (negative control: a flash write)
  Given a package with a wrong SHA-256 or a wrong IDCODE
  When `install --update` runs, and when the host app opens it from the File Browser
  Then both refuse it with their reason, and the flash is byte-identical before and after

Scenario: S7 (P3) boot from a package, and "already installed" (S46 T4)
  Given a .txu with the real host bitstream and hostapp.app
  When `run --board c64u --icap --firmware X.txu` runs, a core starts and the IPROG goes back to the host
  Then the host menu shows before and after the core, and on the flash that `install --update X.txu` made, the host
       reports the package as installed

Scenario: S8 (P4) the flash lock before a large core
  Given the host in `run --board c64u`
  When a large core is started
  Then the host log reads "JEDEC ef4018, status 34 01", a write into 0x000000-0x3FFFFF is ignored until the run ends,
       and the next run starts unlocked

Scenario: S9 (P5) the same on a 100T
  Given `--board c64u --caps 34000226` (a 100T with the W25Q)
  When S1 and S4 run
  Then they give the same results with the 100T package
```

## §3 Decision

Nothing is decided yet. The questions for Alex, asked one at a time:
- **Q1 Where the features live:** the Gherkin runner of `tests/ui` gets a host and updater mode, or `tests/updater/` gets
  features beside `emu_t135.sh`.
- **Q2 Where they run:** by hand on the Mac (as `emu_t135.sh` today), in the pipeline's `emu` stage on the builder, or
  nightly.
- **Q3 Which builds:** the pre-release package of the commit (its `.ue2`, `.txu` and restore bundle), or the images built in
  the run.
- **Q4 The 100T (S9):** now or later.
- **Q5 `emu_t135.sh`:** replaced by the features, or kept for its `--emu-test` controls (the old updater, the title).

## §4 Out

To be decided with Alex before building.

## §5 Built

Nothing yet.
