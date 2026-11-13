# T148 — Every core has one folder of its own beside its .cor: settings, ROMs, saves; no file is shared

**Status:** PROPOSED (2026-10-28), designed with Alex; building. 2026-11-13 status check: built and merged (tests/coreapp test_corefolder, test_romfind, test_mediumwait, tests/hostapp test_coremaint; Merge w1-t148, D13 medium wait); §5 build notes still empty; board S6-S8 and P7 tidy not recorded.
**Repo:** TRX64U-MultiCore — core application (`coreapp/`: ROM search, settings, saves), host application
(`host-app/software/hostapp/`: F8 maintenance page, boot.cfg), CI packaging (`ci/package.sh`), manual chapter 6.
**Number:** T148.
**Origin:** Alex, 2026-10-28, after the Apple IIe found no ROM and the Amiga 500 took the Apple's video ROM as Kickstart
from a shared `/SD/cores/roms/` (translated): "Help me design this better? I am helpless and it looks very chaotic and
arbitrary ... Basically a core should be self-contained: bitstream, app (incl. libs and modules), specific config, ROMs
should be together." On the cores in one folder: "The advantage of all cores in one folder: download everything new,
copy it over, be happy. That makes sense. But then please the ROMs + config, as with the others, in a FOLDER per core.
Automatically, best." On sharing: "Since we do exFAT and did not want symlinks, the cores (and their ROMs etc.) should
not share files. So copying is ok, but not using the same one." On `/flash/roms`: "That is the place where Gideon's
firmware needs them. Our TRX64 has to adapt to the standard of the .cor logic." On saves: "Then create saves too. The
TRX64 core could then put its .c64re and .c64ring there for save/resume state, because it can do that technically."
Then: "Write the spec and build it directly. ... A 'cor & rom maintenance' function in F8 would be great ... see what
I have, is something missing, is something double, and also 'delete non associated files' or so."
**Supersedes:** T112 D1-D3 (ROM search beside the core and over every medium; `.cfg` beside the `.cor`).
**Depends on:** T112, T104 (core application), T139 (F8 settings page, file operations).
**Milestone:** M3.

---

## §1 The problem

Today (read from `coreapp/src/romfind.cc`, board 2026-10-28):
- ROMs are searched in `roms/` beside the `.cor` (shared by every core in that directory), then `/flash/roms/<folder>`,
  `/usb0-3/roms/<folder>`, `/sd/roms/<folder>`. A core started from the SD looks at the sticks before its own medium;
  a shared `roms/` beside the cores hands any core any file (A500 took `apple2e-video.rom` as Kickstart).
- The `.cfg` lies beside the `.cor` (`/SD/cores/apple2.cfg`); ROMs lie wherever the user put them; nothing says where.
- No core keeps a save (NES battery RAM is not kept; the TRX64 core's snapshots have no place).

Parts: **P1** layout, **P2** first start (folder created, files copied from old places), **P3** ROM lookup in one place
with one message, **P4** saves, **P5** update by copying `.cor` files over, **P6** maintenance in F8, **P7** migration of
today's cards and sticks, **P8** manual.

## §2 Scenarios

```gherkin
Scenario: S1 first start creates the core folder
  Given /SD/cores/apple2.cor and no /SD/cores/apple2/
  When  the Apple IIe starts
  Then  /SD/cores/apple2/, /SD/cores/apple2/roms/ and /SD/cores/apple2/saves/ exist; its settings are written to
        /SD/cores/apple2/apple2.cfg (an old /SD/cores/apple2.cfg is moved there once)

Scenario: S2 a ROM is copied once from an old place, then used from the core folder only
  Given apple2e.rom only in /SD/roms/apple2/
  When  the Apple starts
  Then  /SD/cores/apple2/roms/apple2e.rom exists (byte-identical), the core uses it, the log names the copy;
        the original is not touched
  And   on the next start nothing is copied and nothing outside the core folder is read for ROMs

Scenario: S3 no shared file
  Given /SD/cores/roms/ holds files of several machines (the 2026-10-28 case)
  When  the Amiga 500 starts
  Then  it never reads /SD/cores/roms/; a Kickstart is taken only from /SD/cores/amiga/roms/ (or copied there from an old
        place whose folder is the Amiga's)

Scenario: S4 a missing ROM names one place
  When  a required ROM is in no place
  Then  the message is "<file> is missing in <medium>/cores/<core>/roms/" (one path)

Scenario: S5 update by copying over
  Given a release's .cor files copied over /SD/cores/
  Then  every core folder (cfg, roms, saves) is unchanged; a release package contains no core folder

Scenario: S6 maintenance (F8)
  Given /SD/cores with cores, their folders, a stray /SD/cores/roms/, a folder without a .cor and a duplicate ROM
  When  F8 -> "Cores and ROMs" is opened
  Then  each core shows: present / folder present / required ROMs present or missing (names) / saves count;
        orphans are listed (a folder without a .cor, files in cores/ that belong to no core, old roms/ folders);
        duplicates are listed (the same ROM content in several places)
  And   "delete files that belong to no core" asks twice, lists what goes, and deletes only listed files

Scenario: S7 saves
  Given the TRX64 core saves a snapshot / ring, or a NES cartridge with battery RAM is running
  Then  the file lands in <medium>/cores/<core>/saves/

Scenario: S8 a core whose .cor lives on the USB stick (finish plan wave 1 item 19)
  Given the hand-over path is /Usb0/cores/amiga1200.cor and the stick is not up when the core application starts (t+300 ms; it mounts at
        ~t+2.9 s, and again after an IPROG)
  When  the core application starts
  Then  it waits for the medium of the hand-over path (at most 10 s), then makes /Usb0/cores/amiga1200/ (roms, saves), moves the old .cfg in,
        reopens the .cor and scans the libraries: no "could not make the core folder", no ".cor cannot be opened", no "[lib] stick wait: no
        USB mass-storage device attached: not waiting"; a medium that stays away gives those messages as before
```

| # | Solves | Test | Where |
|---|---|---|---|
| S1-S4 | P1-P3 | `tests/coreapp` romfind/corefolder tests with fake fs, each with a negative control | CI |
| S5 | P5 | `ci/package.sh` check: no core folder in a package | CI |
| S6 | P6 | `tests/hostapp` maintenance scan test + render bench page, controls | CI, board |
| S7 | P4 | the saves path helper in tests; board when a core saves | CI, board |
| S8 | P1 | `test_mediumwait.cc` (medium appears after N ms; 4 controls); ue2emu S44 phase A, old elf red / new elf green; board | CI, emulator, board |
| all | P7 | board: Alex's SD and stick tidied with S6 | board session |

## §3 Decision

- **D1 Layout.** `<medium>/cores/<core>.cor` (shipped, flat) and `<medium>/cores/<core>/` (the user's):
  `<core>.cfg`, `roms/`, `saves/`. `<core>` is the `.cor`'s file name without extension. The folder is always beside the
  `.cor`, on the medium the core was started from.
- **D2 No shared files.** A core reads ROMs only from its own `roms/`. Old places (`<cor dir>/roms/`, `<medium>/roms/<folder>/`
  of every medium, `/flash/roms/<folder>/`) are only a source for a one-time copy into the core folder; the copy is
  verified (size, and SHA-256 equal to the source) before it is used. Device ROMs (1541, 1571, 1581) are copied per core
  like any other.
- **D3 `/flash/roms`** belongs to Gideon's firmware: never written by us, never read at run time; only a copy source (D2).
  The TRX64 core follows the same `.cor` logic (its own folder) where its application reads ROMs.
- **D4 One message** for a missing file: the one path in the core folder.
- **D5 `saves/`** is created with the folder; battery RAM, snapshots (`.c64re`) and rings (`.c64ring`) go there.
- **D6 Disk, hard-disk and SD images** stay where the user put them and are written back there (unchanged).
- **D7 Maintenance page** in F8 ("Cores and ROMs"): per core status, orphans, duplicates; "delete files that belong to
  no core" with two confirmations, deleting only what was listed. It never deletes anything inside a core folder of a
  present `.cor`, never a `.cor`, never outside `cores/` except the old `roms/` folders it lists.
- **D8 Packages** contain only `.cor` files (and a `ROMS.txt` listing each core's required files, sizes and legal
  sources from manual chapter 6); never a core folder.

- **D6 (Alex, 2026-10-30, translated): the TRX64 Ultimate firmware follows the folder.** "The idea with the folder stays. That makes no
  difference for the hand-over: the TRX64 core looks there, and if something is written there, that is the place; otherwise it acts 'I am
  alone, I use the flash'." The core reads the hand-over page (HANDOVER_ADDR 0x03EFF000, `host-app/software/hostapp/handover.h`); a valid
  block names its .cor path and the settings live in `<dir>/<name>/<name>.cfg`; no valid block = standalone C64U, settings in the flash as
  Gideon's firmware does. Built in TREX/TRX64-Ultimate (session TRX64U-FW).
  The page is a one-shot mailbox: each application copies it first thing, before heap, ROM or cartridge loads reuse that DDR2.
  TRX64U-FW checked that its boot code does not write the range before main, so HANDOVER_ADDR stays (Alex, 2026-10-30: "so that is settled").

## §4 Out

- Moving media (games) into core folders.
- Writing anything into `/flash/roms`.

## §5 Build notes

(filled in while building)
