# T122 — Every core with a drive can create a new, formatted disk image from its menu

**Status:** BUILT (2026-10-23), native tests in `tests/coreapp/run.sh`; the board follow-up of 2026-10-24 (clean prompt, folder choice, progress) BUILT 2026-10-28 (Alex): on the A1200 a newly created ADF takes files from the Workbench (B2, A1200 half). 2026-11-13 status check: state confirmed: merged (Merge t122-create); built, board B1, B3-B8 go to the testers, B2 A1200 passed (as row).
**2026-11-15 sync:** a stale leftover sentence ("the board has not run either yet") is removed: B2 on the A1200 passed (Alex 2026-10-31), B1 and B3-B8 are left to the testers by decision (§5, 2026-10-31); no tester result is recorded here.
**Repo:** TRX64U-MultiCore — the generic core application (`coreapp/`), per-core descriptors and libraries; no gateware.
**Number:** T122.
**Origin:** Alex, 2026-10-23, at the board with the Apple IIe: "How do I make new disks, please? That should also be
possible on the 1541/1581, by the way, or on the Amiga and so on... So always the option to create a medium, as in
Gideon's core/app" (translated). Asked whether a new disk is formatted or raw: "formatted where possible" (translated).
**Depends on:** T104 (generic application), T104.1 (contribution model: tasks with a name prompt, `drive.create` of `cbm1541`).
**Milestone:** M3.

Open (one line, as in the TODO table): Built; board B1, B3-B8 for all cores go to the testers (Alex 10-31); B2 A1200 passed

---

## §1 The problem

Only the 1541 library offered "Create blank D64" / "Create blank G64" (`coreapp/cores/cbm1541_desc.h`). The
1581 (`.d81`), the Apple IIe, the Amiga 500/1200 (`.adf`), the Atari 800 XL (`.atr`) and the CPC (`.dsk`) can mount
images but could not make one: a game that wants a save disk, or a user who wants to write to a disk, needs a PC.

Parts (MECE):

- **P1 The entry.** Each core with a drive has a task "Create blank ..." with a name prompt that starts with a free name.
- **P2 The place.** The file lands in the folder the menu is on, is never written over an existing file, and a medium that
  cannot be written leaves nothing behind. One rule for every core, the 1541 included.
- **P3 Apple IIe image.** An empty DOS 3.3 data disk the Disk II write-back can serve.
- **P4 Amiga image.** An empty OFS volume (Amiga 500 and 1200 share the code).
- **P5 Atari image.** An empty Atari DOS 2.0S single density data disk.
- **P6 CPC image.** An empty DATA-format disk.
- **P7 1581.** A blank D81 with header, BAM and empty directory, offered by the 1581 descriptor pieces.
- **P8 The name.** The prompt starts with a clean name (board 2026-10-24: garbage characters).
- **P9 The folder.** The person sees and chooses where the file lands (board 2026-10-24: "it simply puts them in /sd").
- **P10 The wait.** The person sees that something is happening and how far (board 2026-10-24: "it takes really long and the UI does not tell me").

## Decisions

- **D1 (Alex, 2026-10-23): formatted where possible** - a new image is usable at once by the machine's own DOS:
  D81 with header, BAM and empty directory; ADF as an empty OFS volume; ATR as an Atari DOS 2.0S data disk; CPC
  as an empty DATA-format disk; Apple II as an empty DOS 3.3 data disk (VTOC and catalog, not bootable). Where a
  format cannot be written correctly the spec says so and the image is blank, decided with Alex.
- **D2:** the same shape as the 1541: a task "Create blank <type>" with a name prompt and a suggested name, the file
  lands in the current browser folder, and an existing file is never overwritten.

## §2 Scenarios

Each is run by `tests/coreapp/run.sh` (stage Sims / lint of the pipeline; the tests are native, against `FakeFs`/`FakeIo`).
"Control" is the sed mutation of the product source that the same test must fail on.

| # | Solves | Scenario | Test | Negative control (must FAIL) |
|---|---|---|---|---|
| S1 | P1 | Each of the five descriptors has the task with its prompt, its `disk.create` binding for its format, and the state with the free name ("blank", then "blank2" once blank exists) | `test_newdisk.cc` `run_case` | `nd_wire_apple/amiga/atari/cpc` (descriptor binds the wrong format), `nd_wire_noreg` (op not registered), `nd_suggest` |
| S2 | P2 | Menu driven: browser in `/usb0/games`, F7, the task, the folder browser (S12) starts there, "Create here"; the prompt reads `blank_`, the typed name lands in `/usb0/games/<name><ext>`; from the media list (`/`) the folder browser starts on the first stick (since 2026-10-24; before: the first medium took it) | `test_newdisk.cc` `run_case` (menu half) | `nd_menudir` (the menu's folder is not the start) |
| S3 | P2 | A bare name, a name with the extension typed, and a full path each refuse an existing file with "exists" and leave its bytes as they were; a read-only medium makes nothing | `test_newdisk.cc` `run_case` | `nd_bare`, `nd_exists` (the checks that refuse an existing file removed) |
| S4 | P2 | The 1541 uses the same machinery: a bare name goes to the menu's folder before the folder of the last disk; inside a disk image (no folder) to the folder of the last disk; its refusals are unchanged | `test_cbm1541.cc` (create, prompt) | `drv_newdir`, `drv_newpref`, `menu_imgfolder`, `drv_exists`, `drv_bare` |
| S5 | P3 | 143,360 bytes. VTOC at T17 S0: catalog T17 S15, DOS 3, volume 254, 122 pairs, 35 tracks, 16 sectors, 256 bytes; the catalog chain read as DOS reads it is S15 down to S1 and ends 0/0, no entry used; the bitmap has tracks 0-2 and 17 in use and 496 sectors free; every other sector zero; every track nibblizes and denibblizes (DOS order) to the same bytes | `test_newdisk.cc` `check_apple` | `nd_apple_t17`, `nd_apple_t18`, `nd_apple_chain`, `nd_apple_start`, `nd_apple_spt` |
| S6 | P4 | 901,120 bytes. Boot block "DOS\0", checksum 0 (the carry sum of the 256 boot longs is not `0xFFFFFFFF`: not bootable), root pointer 880, the rest zero; root block: type 2, hash size 72, empty hash table, bitmap valid, bitmap pointer 881, name, secondary type 1, longs sum to 0; bitmap block longs sum to 0 and block b (2-1759) is free exactly when it is not 880 or 881 (1756 free), no bit past the end; every other block zero; name without `:` and `/`, at most 30 characters, `Empty` when nothing is left | `test_newdisk.cc` `check_adf`, builder cases | `nd_adf_boot`, `nd_adf_flag`, `nd_adf_root`, `nd_adf_bitmap`, `nd_adf_free`, `nd_adf_bit`, `nd_adf_name`, `nd_adf_pointer` |
| S7 | P5 | 92,176 bytes. Opened with the product's own `atr_open`: 720 sectors of 128; VTOC sector 360: DOS code 2, 707 sectors, 707 free, and the bitmap says exactly that (used: 0-3 and 360-368); boot sectors and the eight directory sectors zero; every other sector zero; header bytes 0x96 0x02, 5760 paragraphs, sector size 128 | `test_newdisk.cc` `check_atr` | `nd_atr_free`, `nd_atr_dir`, `nd_atr_boot`, `nd_atr_head`, `nd_atr_dos` |
| S8 | P6 | 194,816 bytes. "MV - CPC" header, 40 tracks, 1 side, track size 0x1300 (the three header bytes the u765 FDC reads at 0x30, 0x31, 0x33); each track block "Track-Info", track number, N=2, 9 sectors, filler E5, sector IDs C=track H=0 R=C1-C9 N=2, no error flags, all 4,608 data bytes E5; size within `CPC_DSK_MAX` | `test_newdisk.cc` `check_cpc` | `nd_cpc_ids`, `nd_cpc_fill`, `nd_cpc_tsize`, `nd_cpc_tracks` |
| S9 | P7 | `CBM1581_TASKS` is a task with prompt "Name of the new D81" and the state `drive.suggest.d81` (looks among `.d81` files); the D81 read as a 1581 reads it: header 40/0 (link 40/3, DOS version D, name padded with A0, type 3D), BAM 40/1 and 40/2 with the disk ID, per-track counters equal the free bits, track 40 has 36 free (`F0`), 3,160 blocks free outside the directory track, one empty directory sector at 40/3; the file goes to the menu's folder | `test_cbm1541.cc` create section | `drv_d81bam`, `drv_d81bam2`, `drv_d81suggest`, `drv_newdir` |
| S10 | P1-P7 | Everything that was green stays green: the menu goldens (one new row per core) and the descriptor check (every name registered, every binding fits) | `test_desc.cc`, `golden/menu_*.txt` | the existing `desc_*`, `menu_*` controls |
| S11 | P8 | The name prompt of every Create task starts with a clean name on every state of the medium (none, gone, name taken, 99 taken, a long folder, the chosen folder); a state that hands over rubbish (control or 8-bit bytes first, unterminated) shows the clean start of it or nothing; no format of the product's code has a precision, `%%` or a length modifier (the board's printf has none) | `test_createflow.cc` `check_suggestions`, `check_junk`, the `boardprintf.h` shim in `test_createflow` and `test_cbm1541` | `cf_printf`, `cf_printf_vol`, `cf_printf_adf`, `cf_printf_pct`, `drv_printf_lastdir`, `drv_printf_stem`, `cf_garbage`, `cf_garbage_len`, `cf_fallback`, `cf_asked`, `cf_chosen` |
| S12 | P9 | Create opens a folder browser first (directories and "Create here"): it starts in the menu's folder, else in the folder of the last file the menu ran, else on the first stick that reads (`/usb0` .. `/usb3`), else on the list of the media where "Create here" refuses - never `/sd` unasked; the chosen folder is the folder (another medium, up and down), ESC and a function key leave it writing nothing; a folder that cannot be written takes no fallback to another medium; the notice has the whole path | `test_createflow.cc` `check_folder`, `test_newdisk.cc`, `test_cbm1541.cc` (create section) | `cf_start_menu`, `cf_start_last`, `cf_start_usb`, `cf_start_sd`, `cf_here_root`, `cf_lastdir`, `cf_dirs`, `cf_compose`, `cf_strict`, `cf_path_notice`, `nd_menudir`, `menu_imgfolder` |
| S13 | P10 | The image is written in chunks of `NF_CHUNK` (16 KiB), one per tick of the run loop; the first screen after the name shows "Building the image 0%" before a byte is written; the percentage and the file's whole path are on screen and rise with every chunk; no write is longer than a chunk; the file is the whole image and was closed once; keys wait while it writes | `test_createflow.cc` `check_progress_for` (Atari ATR, Amiga ADF, Apple DSK, CPC DSK, 1541 D64) | `cf_chunk`, `cf_pct`, `cf_pct_1541`, `cf_row`, `cf_frame` |
| S14 | P10 | ESC while it writes drops the job and removes the partial file; ESC before the first chunk writes nothing; a write that fails in the middle (stick pulled) leaves no half image and says so; the next create works | `test_createflow.cc` `check_progress_for` | `cf_remove`, `cf_cancel`, `cf_cancel_first`, `cf_esc` |
| B1 | P3 | Board, by hand: Apple IIe, "Create blank DSK", mount as D1:, boot a DOS 3.3 master from D2: or `BRUN`, `CATALOG` on D1 shows "DISK VOLUME 254", `SAVE` a program, mount again: it is listed | board session | - |
| B2 | P4 | Board, by hand: Amiga 500 and 1200, "Create blank ADF", mount DF1, Workbench shows a disk named as typed, copy a file in, eject, mount again: it is there. Booting with it in DF0 shows the normal "insert Workbench" screen, no guru | board session | - |
| B3 | P5 | Board, by hand: Atari 800 XL, "Create blank ATR", mount D2:, from a DOS 2 disk in D1: `DIR` of D2 lists 707 free sectors; copy a file to D2:, mount again | board session | - |
| B4 | P6 | Board, by hand: CPC, "Create blank DSK", mount A:, `CAT` is empty with 178K free, `SAVE "T"` and `CAT` again | board session | - |
| B5 | P2 | Board, by hand: one of them from a folder on the stick, and on a write-protected card | board session | - |
| B6 | P8 | Board, by hand: Atari 800 XL, F7, "Create blank ATR": the folder browser, "Create here", the name prompt reads `blank_` (or `blank2_`), no other characters | board session | - |
| B7 | P9 | Board, by hand: with a stick in: the browser starts on it (or in the folder the Library was in); walk into a folder, "Create here", name, "Created /usb1/<folder>/<name>.atr" in the status line; the file is there on a PC. Without a stick: it starts on the list of the media | board session | - |
| B8 | P10 | Board, by hand: Amiga ADF (880 KB) on the SD card: "Building the image 0%" at once, then the percentage rising with a bar; ESC in the middle leaves no file; the UART shows `[core] create step: N percent, M ms` per chunk (these are the numbers for where the time goes) | board session | - |

## §3 Decision

- **D3 One placement rule.** The folder / free name / never overwrite logic of `CbmDrive` (`folders`, `pick_dir_name`, `suggest_name`,
  `write_new`) is taken out into `coreapp/src/newfile.{h,cc}` (`nf_folders`, `nf_suggest`, `nf_write_new`); the drive class calls it.
  The 1541 behaves as before except for D4.
- **D4 The menu's folder.** `CoreCtx::media_dir` is set by `MenuCtl` (`note_folder`) when a task that asks for a name is shown and when it
  runs: the browser's folder, empty on the list of the media (`/`) and inside a disk image (there is no folder there). A bare name goes
  there first; empty: the folder of the last disk (1541 only), then `/usb0 .. /usb3`, `/sd`. The 1541's operations pass it to the drive
  (`CbmDrive::set_new_dir`), so it also puts its new D64/G64/D81 into the folder the menu is on.
- **D5 One operation for the other cores.** `coreapp/src/newdisk.{h,cc}`: the builders `nd_apple_dos33`, `nd_adf_ofs`, `nd_atr_dos2`,
  `nd_cpc_data` (pure, no file system, no allocation) and the contributor "newdisk" with `disk.create` (path, format, name) and the states
  `disk.suggest.<apple2_dsk|adf|atr|cpc_dsk>`. A descriptor adds one task `{ "Create blank X", "disk.create", bind, 3, false, prompt,
  "disk.suggest.x" }` and calls `newdisk_register(r)` last in its register function (`apple2.cc`, `amiga.cc` - one source for the A500 and
  the A1200 - `atari800.cc`, `cpc.cc`). The image is built in memory (up to 901,120 bytes, like the D81 of the 1541 library) and written with
  `nf_write_new`. Both files are in the Makefile of the application and in `BASEL` of the tests.
- **D6 1581.** The builder `d81_blank` exists in `app/lib/cbm1541/d1541_gcr.cc` and `drive.create` takes the format `d81`; this spec adds the
  state `drive.suggest.d81` and the descriptor piece `CBM1581_TASKS` (next to `CBM1581_FILETYPE`) that a core with a 1581 splices in.
- **D7 Apple IIe: `.dsk`, DOS 3.3 order.** `apple2_disk.cc` mounts `.dsk`/`.do` (DOS order), `.po` (ProDOS order) and `.nib` (raw nibbles),
  nibblizes a sector image per track and on write denibblizes it back to the same order. `.dsk` is the file every tool and emulator makes
  and the drive writes back to it sector for sector, so that is the one created (143,360 bytes, DOS order: file sector = DOS logical
  sector). The disk is a **data disk, not bootable**: tracks 0-2 (where DOS 3.3 sits on a master) are marked used and empty, so DOS never
  allocates there; no boot sector, no HELLO program, no DOS image (no third-party code is shipped). VTOC: volume 254, last track allocated
  17 and direction +1 (DOS corrects both on its first write), 122 pairs per T/S list. The name prompt names the file only; DOS 3.3 has no disk name.
- **D8 Amiga ADF: OFS, formatted not installed.** Boot block as Format writes it without Install (QC, 2026-10-23): `DOS\0` (flags 0: OFS),
  checksum 0, root pointer 880, the rest zero. Kickstart sees an invalid checksum and never executes code from the disk: a data disk, not
  bootable; no boot code is shipped. Root block 880 as AmigaDOS defines it (type 2, hash 72, bitmap flag -1, bitmap page 881,
  name length byte + 30 characters at 0x1B0, secondary type 1); bitmap block 881. All dates are 0 (1 January 1978): the application has no
  clock to ask. The volume name is the typed name (`:` and `/` removed, 30 characters), the file's name when empty.
- **D9 Atari ATR: DOS 2.0S, single density.** 16-byte header + 720 x 128 bytes. VTOC 360 (code 2, 707 / 707, bitmap bytes 10-99, sector 0
  and 720 not countable), directory 361-368 empty, boot sectors 1-3 empty: a data disk (DOS.SYS is Atari's, not shipped); boot it from a
  DOS disk in D1: and use this one as D2:.
- **D10 CPC DSK: standard, one side.** `MV - CPCEMU Disk-File` header, 40 tracks, 1 side, 9 sectors of 512 bytes with IDs C1-C9, filler E5
  (the empty directory of AMSDOS/CP/M is a directory full of E5), GAP3 4E, 194,816 bytes. Standard rather than extended: the same
  track size for every track, and the u765 reads the track size from byte 0x33 as it is written. 178 KB data, as a 3 inch disk side.

- **D11 (Alex, 2026-10-24, translated): a clean name, a folder choice, a progress display.** "creating: it is pre-filled with weird garbage
  characters. How do I know where it is created?! It simply puts them in /sd - that is not good. So create needs a path selection. It also takes
  really long and the UI does not tell me. So 'create' should have a progress display."
- **D12 The folder browser.** A task with `Task::pick_dir` (every "Create blank ..." of every core, the 1541's included) opens `MenuCtl`'s folder
  browser before the name prompt: directories only, "Create here" on top. It starts in the menu's folder, else in the folder of the last file the menu
  ran (`last_dir_`), else on the first of `/usb0 .. /usb3` that lists, else on the list of the media, where "Create here" is dead. The chosen folder
  goes to `CoreCtx::media_dir` (the prompt's free name looks there) and in front of the typed name: the operation gets `<folder>/<name>` (a full
  path, an existing file is refused), or `<folder>/` for an empty name (a free name in that folder). A path that names a folder is that folder: no other
  medium takes the file when it cannot be written (`place_new`). "Save Disk as" keeps the old rule (menu folder, then the folder of the disk).
- **D13 The writer.** `NfWriter` (`newfile.{h,cc}`): `begin` places the file by the same rule as `nf_write_new` and saves the first `NF_CHUNK` (16 KiB)
  bytes, `step` appends one chunk through `CoreFile::write`, the last one syncs and closes; a failed step or `abort` removes the partial file.
  `nf_write_new` stays for the callers that write all at once (Save).
- **D14 The steps.** `CoreCtx::job_step` set: `disk.create` and `drive.create` do one chunk per call and answer `more` (the first call builds the
  image and creates the file); the image and the open file stay in the operation between the calls. Not set (REST, tests): the operation loops over the
  chunks itself, one call, same result. `MenuCtl::busy/tick` is the caller: the run loop (`run.cc`) calls `tick()` once per pass when no key came, then
  renders, so every chunk is one short hold of the core lock and one frame (row: "Writing <path>", percent, a 20 character bar; status line the same).
  ESC sets `job_cancel`; the next call drops the job and removes the file. Other keys wait.
- **D15 No precision, no `%%`, no `%l` in a format.** The core application's printf is the firmware's `small_printf.cc` (§5.2); `nf_copy` replaces
  `%.*s`, the percent sign is an argument, `%ld` is `%d` (RV32: same width). `tests/coreapp/boardprintf.h` checks every format a test runs.

## §4 Out (for Alex's decision; nothing here is dropped silently)

- **Progress inside the first step.** Building the image (an ADF, a G64's GCR encode) and creating the file is one step; its time is not divided and
  ESC waits for it. The image is built in RAM (up to 901,120 bytes) and held until the job ends.
- **Save Disk as D64 / G64** (1541) keeps the name prompt without the folder browser and writes at once; the same writer would carry it (decide).
- **A "new folder" in the folder browser** is not offered; a folder that does not exist yet cannot be chosen.

- **1581 has no menu entry.** No core of the generic application has the 1581 (T103: it is in `cores/c64u` only, which keeps the full
  Ultimate app). `CBM1581_TASKS` and the D81 builder are built and tested, but no descriptor splices them in, so no menu shows
  "Create blank D81" today. Decision needed: leave until a core has a 1581, or give the C64 descriptor a board-probed task (`board_caps`).
- **Apple IIe bootable disk (DOS 3.3 / ProDOS system disk)** - needs the DOS image, which is Apple's; data disk only (D7). ProDOS-order
  (`.po`) and 800 KB / 3.5" images are not created.
- **Amiga:** FFS (`DOS\1`) and international / dir-cache variants, bootable disks (boot code, Install, Workbench/startup-sequence), HDF hard disk images
  (sizes and geometry are the user's choice) and 1.76 MB high-density images are not created. Dates are zero (D8).
- **Atari:** enhanced / double density ATR (1050, 256-byte sectors), DOS 2.5 (1040 sectors), SpartaDOS and DOS XL formats, a bootable disk
  with DOS.SYS/DUP.SYS are not created (D9).
- **CPC:** double sided and extended DSK (`EXTENDED CPC DSK`), 42 tracks, and the SYSTEM (sector IDs 41-49) and IBM (01-08) formats are
  not created (D10). The CP/M directory needs no format step: E5 is empty.
- **Create-and-mount in one step** ("Create blank X and mount") is not offered; create, then mount from the browser. One extra row per
  core and a drive choice per core would be needed; ask if it is wanted.
- **Not tested natively, only on the board (B1-B5):** that the Apple DOS, AmigaDOS, Atari DOS and AMSDOS accept the images. The
  checkers are written from the format definitions, not from a reference tool; no real DOS ran here.

- **Board steps go to the testers (Alex, 2026-10-31: "No. The testers should test that." - "For all cores", translated).** B1 and
  B3-B8 (a new image from the menu of every core with a drive, used in the machine) are left to the tester group, for all cores;
  Alex does not run them. A finding comes back as a GitHub issue.

## §5 As built (2026-10-23)

- New: `coreapp/src/newfile.{h,cc}`, `coreapp/src/newdisk.{h,cc}`, `tests/coreapp/test_newdisk.cc`. Changed: `app/lib/cbm1541/d1541_drive.{h,cc}`
  (placement moved, `set_new_dir`), `coreapp/cores/{cbm1541.cc,cbm1541_desc.h,apple2.cc,amiga.cc,atari800.cc,cpc.cc}`, `coreapp/src/{ctx.h,
  menuctl.h,menuctl.cc}`, the application Makefile (`host-app/target/u64ii/riscv/coreapp/Makefile`: two sources), the five menu goldens,
  `tests/coreapp/run.sh` (the two existing controls `drv_exists` and `drv_bare` now mutate `newfile.cc`).
- Tasks per menu: one row more on F7 Actions for the Apple IIe, A500, A1200, Atari 800 XL and CPC; the VC-20, C16/Plus4 and C64 menus are unchanged.
- A bug found while building: none in the product; the first version of a checker counted the D81's directory track in the "blocks free"
  (3,196) - a 1581 does not count track 40, 3,160 is the number a real one shows.
- Run (builder, `trx64-sim-env:4`, `tests/coreapp/run.sh`, 2026-10-23): "49 test programs passed, every negative control failed as it
  must", `newdisk: 43235 checks, 0 failed`, `cbm1541: 1017 checks, 0 failed`; all 41 controls of S1-S9 red (for example `nd_apple_chain`
  55, `nd_adf_bit` 60, `nd_cpc_ids` 1800 checks red).
- Tests of a missing file: `FakeFs::files[...]` on a missing key inserts an empty entry and `list()` then throws; the checks read through a
  helper that does not insert, so a broken product source is a red check and not a crash.
- Not built for RISC-V; the code size of the image is not measured (limit check: `tools/check_app_size.py`).

**2026-10-23, the board image did not link (CI 954, trxapps):** `new (std::nothrow) uint8_t[cap]` in `op_create` needs `std::nothrow` and
the nothrow `operator new[]` from libstdc++, which the RISC-V image does not link; the native tests have it, so they passed. The image
buffer is `malloc`/`free` now. `test_newdisk` 43,259 checks, 0 failed; its three controls fail as they must.

**Board, 2026-10-24 (Alex, Atari 800, release main-54e63c47):** "creating: it is pre-filled with weird garbage characters. How do I know
where it is created?! It simply puts them in /sd - that is not good. So create needs a path selection. It also takes really long and
the UI does not tell me. So 'create' should have a progress display." (translated). Decided: (a) the name prompt starts with a clean
suggested name, never garbage; (b) Create asks for the folder (a folder browser, starting at the menu's folder or the last disk's folder)
before the name; (c) a progress display while the image is built and written. Built under §5, next section.

### §5.2 As built: the board follow-up of 2026-10-24 (clean name, folder choice, progress)

**The garbage (P8), cause.** `coreapp/src/newfile.cc` `nf_suggest` made the suggested name with `snprintf(out, outlen, "%.*s", len, b)`. The core
application's printf is not libc's: `host-app/target/u64ii/riscv/coreapp/Makefile:195` builds `small_printf.cc` of the Ultimate firmware with
`-DFP_SUPPORT=1`, and `_my_vnprintf` there (`apps/ultimate/software/system/small_printf.cc:109-117, 153-154`) reads a `.` as the start of a
precision of its floating point support, takes the `*` for the precision digit, and `s` then pops the next argument - the length, an `int` - as the
string's address and prints the bytes at that address up to a NUL. So the prompt was filled from low memory. Every core with a Create task had it; the
native tests could not see it because libc's snprintf does `%.*s`. The same format was in four more places (`newdisk.cc` volume name of an ADF,
`d1541_drive.cc` `last_dir_` of the last disk, the D64/G64 volume name and `image_stem` of Save) and `ops.cc` had `%ld` (the board prints `%l` and goes on).
All five went to `nf_copy` (`newfile.{h,cc}`), `%ld` to `%d`; the new progress text uses `"%d%s", pct, "%"` because the firmware has no `%%` either (`hostui.cc:1422`
says so). `begin_text` also takes the suggestion only as far as it is a file name (24 characters, none of `/\:*?"<>|`, none outside 0x20-0x7E), so a state that
returns rubbish or an unterminated buffer shows an empty field or the clean start of it.

**The folder (P9).** D12. `Task::pick_dir` (desc.h), `MenuCtl::begin_pick/pick_key/load_pick/start_path`, rows: "Create here" (dead on the list of the media), then
`..` and the folders; the title is the path. `desc_check` refuses `pick_dir` without a prompt.

**The progress (P10).** D13, D14. `NfWriter`, `CoreCtx::job_*`, `MenuCtl::busy/tick/progress`, the run loop's `tick()`, `disk.create` (`newdisk.cc`) and
`drive.create` (`CbmDrive::create` with `step`/`cancel`, `cbm1541.cc`). The row shows "Writing <whole path>", the percentage and `[####----------------] ESC cancels`;
the status line says the same; the UART gets `[core] create step: N percent, M ms` per step.

**Why it was slow.** Not measured on the board; this is from the code. (1) The prompt opens with `nf_suggest`: for each medium tried it called `CoreFs::list`, which is
`FileManager::get_directory`: every entry of the folder read into a heap `FileInfo` and the list sorted, under the FileManager lock, only to see that the folder reads - on
a big stick or card root that is the first wait, before a name is typed. It is gone for the folder that was chosen (`asked` in `nf_suggest`: one `stat` per candidate name
remains). (2) The write: the whole image was built in RAM (92,176 bytes for the ATR: microseconds) and given to `FileManager::save_file` in ONE `File::write` - FatFS allocates the
cluster chain and pushes it through the USB/SD driver while the menu thread waits on the executor and holds the core lock; `fclose` then syncs and tells the
FileManager observers to read the directories again. There is no per-chunk FAT write in our code. (3) With no stick in, `/usb0 .. /usb3` were tried one after the other (a `list`
each) before `/sd`. Nothing in this repository can say which of the three the 2026-10-24 seconds were; B8 puts the per-step milliseconds on the UART.

**Tests** (`tests/coreapp/run.sh`, builder, `trx64-sim-env:4`): `createflow` (new, 564 checks: the suggestion of every Create task of Atari, Apple IIe, Amiga 500/1200, CPC, VIC-20 D64/G64 on
eight states of the medium; the junk state; the folder browser's start order, the chosen folder, ESC, no fallback to another medium; the progress of an ATR, ADF, Apple DSK,
CPC DSK and D64: 6 to 55 ticks, rising percentages, no write above 16 KiB, "Building 0%" before the first byte, cancel, a pulled stick, the next create), 28 controls;
`boardprintf.h` makes every `snprintf` of `createflow` and `cbm1541` fail on a format the board's printf cannot print (precision, `%%`, `%l`) - it found the `%ld` of `ops.cc`; `newdisk`
and `cbm1541` walk the new menu flow (picker, name, ticks); `desc` checks `pick_dir`. The fakes got `extend_writes`, `write_fail_at`, `deny_prefix`, `write_sizes`.

**Not proven:** anything on the board (B6-B8); that the board's `File::write` at the end of an open file extends it as `FakeFs` does (FatFS does; `FmFile::write` seeks to the old size
and writes); a run on the board.

**Run (2026-10-24):** builder, `trx64-sim-env:4`, `tests/coreapp/run.sh` whole: "50 test programs passed, every negative control failed as it must" (635 controls); `createflow: 564 checks, 0 failed`,
`newdisk: 43289 checks, 0 failed`, `cbm1541: 1032 checks, 0 failed`. RISC-V: `u64-build-env:ci`, the coreapp make as `ci/trxapps.sh` runs it: links, no undefined `std::nothrow`;
`coreapp.app` 842,928 bytes at 0x30000 (the slot takes 1,966,068; `tools/check_app_size.py` ok). `bash tools/host_pin.sh --check`: the host is unchanged.
