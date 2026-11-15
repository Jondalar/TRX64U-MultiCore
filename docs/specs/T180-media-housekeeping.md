# T180 — Media housekeeping in every core: a mount replaces what is mounted, mounts can persist, the library keeps up

**Status:** DECIDED (2026-11-04, Alex; D1-D7, all questions answered); BUILT and merged: D1-D10 (commits to 2026-11-10 on main, §5 per branch). Open: where the C64's media-set rows live, the footer +/- hint (§5 'Footer not changed'), names >127 on the board FileManager (vendor INFO_SIZE 128), board test. 2026-11-15 sync: the old 'rest not built' text removed; media sets are configs without settings since T198 (`mediamounts.cc`), so the media-set row question may be moot (Alex to confirm).
**Repo:** TRX64U-MultiCore — the core app (`coreapp/src/`: media slots, settings, library), every core's descriptor, the host File Browser
for P5.
**Number:** T180.
**Origin:** Alex, 2026-11-04, after the A1200 folder-HD0 reports (GitHub discussion #35, issues #36, #37): "then to a few basic housekeeping
and QoL fixes: 1) mount on an already occupied medium = unmount + mount the new medium; 2) under F8 in every core an option 'persist medium
mounts' (Y/N). That way at the next start my mounted disks/HDs etc. are mounted again. Read the issues for what else falls in this direction"
(translated).
**Depends on:** T128 (folder HD0), T148 (core folders, `.cfg` beside the core), T112, the core app settings (`settings*.cc`).
**Milestone:** M3.

---

## §1 The problem

- **Who:** everyone who changes disks or hard disks in a core (A1200 installing Workbench from ADFs, C64/C128 with D64/D81, Atari, Apple II, CPC).
- **What happens today:**
  - A mount on a drive that holds a medium: testers found that ejecting DF0 first "improves reliability", and that after about three ADF
    mounts in a row "Mount DF0" stalls ~10 s while a folder is HD0 (#35/#37, T128 history 2026-11-04).
  - Mounts are gone after a power cycle; the A1200 folder HD0 "was" kept in earlier releases (#36, two testers).
- **Parts:**
  - **P1 Replace.** A mount on an occupied slot first unmounts the old medium completely (flush, close, release), then mounts the new one.
  - **P2 Persist.** An F8 option per core keeps the mounted media (drive slots, hard disks, folder disks, cartridges where the core has them)
    across a restart; at the next start they are mounted again.
  - **P3 Library lists (#28).** `[Recent]` and `[Favorites]` in the F3 library stay empty (Neo Geo, c83a83c2).
  - **P4 File operations (#20).** In the F3 library file operations fail ("no operation files.rename"); asked for: make folder, copy/paste,
    several files at once.
  - **P5 Jump to key (#34).** In the host File Browser a letter jumps to the first entry starting with it (replaced by paging, D7).

## §2 Scenarios (draft)

- S1 Mount B on a slot holding A: A is flushed and closed (no open handle left, bench counts handles), B is mounted; ten such swaps in a row
  take the same time each (no growth). Negative control: the old path (no unmount) must fail the handle count.
- S2 Persist on: mount DF0 = X.adf and HD0 = a folder, restart the core: both mounted again, same paths and sizes. Persist off: nothing mounted.
  Control: persist on but the save skipped must fail.
- S3 A persisted medium that is gone at the next start (stick missing, file deleted): the slot stays empty with a note, the core starts.
- S4 (#28) Favouriting a file puts it in `[Favorites]`; starting one puts it in `[Recent]`; both survive a restart.
- S5 (#20) Rename in the F3 library works on SD and USB; the decided extras (P4) each with a bench.
- S7 (D4) With unsynced writes on DF0, HD0 (image) and HD0 (folder, scratch not folded back), each of unmount, replace, reset, reload
  core and return to host leaves the files/folder byte-identical to a full sync; a write-back made to fail keeps the medium mounted and
  shows the error. Negative control: the current Return to host path must fail the compare.
- S8 (D5) Save a set, change every slot, load the set: every slot is back as saved and the core was reset once; a set naming a missing
  medium leaves that slot empty with a note. Control: loading without the reset must fail the reset count.
- S6 (#34, D7) In a host File Browser folder of 3 pages, "+" twice reaches the last page, a third "+" stays there, "-" goes back one page;
  C, M, A, P, R, U, F, S, V keep their actions; the same in the core app's F3 browser, where SHIFT + "+" / "-" still change the volume
  and the bare keys do not. Control: a build without the keys must fail the selection check.

## §3 Decision

- **D1 (Alex, 2026-11-04):** a mount on an occupied medium is unmount + mount of the new medium, in every core and every slot.
- **D2 (Alex, 2026-11-04):** F8 in every core gets "Persist medium mounts" (Yes/No); with Yes the mounted media are mounted again at the next start. Default No (Q1).
  - **How (Alex, 2026-11-04: "save the mounted media as 'config' and 'mount' them at the next start before the core is started", translated):**
    the mounts go into the core's `.cfg` beside the core (T148), one key per slot (e.g. `Mount DF0 = /Usb0/WB32/Install.adf`,
    `Mount HD0 = folder /Usb0/Amiga/DH0_A600, 2 GB`), written at every mount and unmount, not only on leaving (a hard power-off has no
    leaving, §4). `boot_start` (`coreapp/src/boot.cc` l.98) mounts them after the ROMs and the settings and before `d->release`, so the
    core sees them from its first cycle (the A1200 boots from HD0). Reload core and a later return from the host take the same path.
  - A stored medium that is missing leaves the slot empty with a note; the core starts (S3). A folder mount shows "Mounting the folder..."
    during the start instead of a black screen. App only, no bitstream.
  - **Every core (Alex, 2026-11-04: "well, for all cores if at all", translated):** D2 and D5 cover every engine that holds a medium, not only the
    window drives and sector units: Apple II floppy and slot 7, Atari SIO and cartridge, CPC, the C64/C128/C264/VC-20 drive and every cartridge
    port. Each engine stores and restores its own slots through the same keys; no core gets the item or the set rows without them working.
- **D4 Write back before a medium is let go (Alex, 2026-11-04: "plan it like that. That concerns everything that can hold a medium
  open. An unmount should also do this write", translated):** every path that leaves a writable medium first writes back everything
  pending for it, completely, and only then goes on: unmount/eject, replace (D1), reset, reload core, return to host, core change. Media:
  window drives (`win_flush_all`/`flush_drive`), sector units incl. the folder disk's full fold-back (`sec_sync_all`/`eject_unit`), the
  cartridge write-back (`wb_flush`). While it runs the picture says "Saving media..."; if a write-back fails, the medium stays mounted,
  the action stops and the error is shown (no silent loss).
  - Found 2026-11-04: Return to host (`coreapp/src/menuctl.cc` l.1788) only calls `wb_flush`, not `engines_before_hold`, so pending
    disk and folder writes are lost there; `eject_unit` (`sectors.cc` l.29) syncs but ignores a failure and then deletes the folder disk.
  - A hard power-off is out (§4).
- **D5 Media sets (Alex, 2026-11-04: "it would also be nice if I could save a 'media config', so not only one", translated):** a named set
  holds every slot of the core at once (e.g. A1200 "WB32 Install": DF0 = Install.adf, HD0 = a folder), one file per set in the core's
  folder (`media/<name>.mnt`). The media menu gets "Save media set..." (asks for a name) and "Load media set..." (a list). Loading does
  D4 + D1 for every slot and then resets the core (Alex: "reset right away", translated). Persist (D2) keeps what was mounted last,
  whether it came from a set or not.

- **D3 (#28, Alex, 2026-11-04):** "#28 - in which core do we have that? I think that should go" (translated). `[Recent]` and `[Favorites]`
  are the core app's F3 Library lists (`coreapp/src/menuctl.cc` l.372, 460, 1592-1626, 2573), so every core has them: they are removed
  from the Library in every core, with "Add to / Remove from Favorites".
- **D6 File operations (#20, Alex, 2026-11-04: "let's do it like that", translated):** the F3 library already has them (SPACE marks and
  goes on, Mark all / Mark none, TAB: Copy to, Move to, Copy to clipboard, Paste here, Rename, Delete, New folder; Copy, Move and Delete
  act on every marked file, Rename on one; `coreapp/src/menuctl.cc` l.1581-1600, 2922, 3383-3485, `libfiles.cc`). Only the C64 core
  registers them (`coreapp/cores/c64.cc` l.553 `libfiles_register`), so every other core answers "no operation files.rename". Fix:
  register them for every core in the common part of the app. Transfer to and from the computer stays FTP to the host app (Gideon's
  `ftpd`, while the host runs) or the USB stick at the computer; the web file tool (Alex's earlier direction) is not built (§4).
- **D7 Paging instead of jump to letter (#34, Alex, 2026-11-04: "C= + letter does not work. Jumping to a folder by letter is out - can we
  page? With + -?", translated):** in the host File Browser "+" moves the selection one page (the rows the list shows) down, "-" one page
  up, stopping at the ends. Both keys are unused in the host app (`hostui.cc` `on_key`); the letters stay the hotkeys they are, SHIFT +
  letter included.
  - **Core app F3 browser too (Alex, 2026-11-04: "exactly, SHIFT +/- please", translated):** there the bare "+" / "-" were the T160 volume
    keys (`coreapp/src/menuctl.cc` l.2864, every menu screen) and PageUp/PageDown (l.2896) are F1/F7 on the C64 keyboard (`run.cc` l.71).
    In the F3 browser the bare "+" / "-" now page; the volume there is SHIFT + "+" / SHIFT + "-" only (C64 keyboard '{' / '}', USB '_' for
    SHIFT + "-"; a USB keyboard's SHIFT + "=" arrives as "+" and pages there). Elsewhere in the menu the volume keys stay as T160 has them.
- **D8 A cursor in every text entry (#42, from #38, JM 2026-11-05: "could it be possible to move the cursor instead of only deleting?"; Alex, 2026-11-05: "Ja"):**
  one line editor shared by the host and the core application for every text entry (Rename, New folder, search, text settings, menu entry names):
  CRSR left/right move the cursor, HOME/CLR to start/end, typed characters insert at the cursor, DEL deletes before it, the row scrolls with the cursor.
  Amended (Alex, 2026-11-05: "Leere", translated "clear"): CLR (SHIFT+HOME) empties the field as before; HOME goes to the start, CRSR down to the end.
- **D9 "Unzip here" in every core (#43, JM 2026-11-05; Alex, 2026-11-05: "'unzip here' would be better ... build on the code"):** the existing
  Unzip application (`apps/unzip`, ABI 3, file actions on `.zip`: Unzip to folder / Unzip here / Extract one file) is built by CI, shipped in the
  package and the release, and found by every core's application; no transparent mounting from a .zip.
  **Coordinator proposal (2026-11-05):** one shared applications folder `<dir of the .cor>/apps` (`/SD/cores/apps/unzip.u2a`; `<root>/cores/apps` for a start without a .cor), read by every core after its own `<core folder>/apps`; an equal name in the core's own folder wins.
- **D10 A home folder per core (#40, a tester 2026-11-05: "set a default path for media locations per core"; Alex, 2026-11-06: "Good. Please
  do it", translated, on the finding that D6 left it open):** `Home Directory` and `Enter Home on Startup` become common settings of every
  core, stored in that core's own `.cfg`; "Set as home folder" persists in every core, and with "Enter Home on Startup" F3 opens there.
- **D10 amended (Alex, 2026-11-06, board, translated: "I do not type that in. I set it with H in F3, or I get a selector here"):** the home
  folder is never typed. In F3 a key H sets the folder shown as home (the operation `files.set_home` exists, menuctl `ui.home`); the
  "Home directory" row shows the path and RETURN on it opens a folder selector, no text entry. Its place (F8 or F5) follows the UX rework.

## §3a Open questions (one at a time)

1. Default of "Persist medium mounts": Yes or No?
   **Answered (Alex, 2026-11-04):** "off by default, it has to be set actively" (translated) - default No.
2. P3-P5 (#28, #20, #34): part of T180, or each its own task?
   **Answered (Alex, 2026-11-04):** part of T180 ("with it", translated).
3. P4 scope: which file operations (rename, delete, make folder, copy/paste, several files)?
   **Answered (Alex, 2026-11-04):** the existing F3 operations with SPACE marks, in every core (D6).

## §4 Out

- Saving on a hard power-off (Alex, 2026-11-04: "whoever switches off hard like that is out of luck", translated). The ESP32 cuts
  power after a 4 s hold of the power button with no event to the FPGA beforehand (`u64ctrl/main/button_handler.c`, `regulator_enable(0,0)`
  in the same call as `BUTTON_OFF`); changing Gideon's ESP32 firmware for a warning is not done.
- Jump to an entry by its first letter (#34): the letters are hotkeys; paging instead (D7, Alex 2026-11-04).
- A web file tool in the host web UI (#20): FTP to the host and the USB stick cover the transfer (Alex, 2026-11-04, D6).

## §5 Build notes

### D3, D6, D7 core app part (branch `t180-f3`, 2026-11-04)

Built: D3, D6, and the core app's half of D7. Not built (other branches): D1, D2, D4, D5 and the host File Browser half of D7.

- **D3 (S4 changed by the decision: nothing to favourite any more).** Removed from `coreapp/src/menuctl.cc`: the `[Recent]` / `[Favorites]` rows at the
  media level, `open_virtual`, `add_recent`, `is_favorite`, `toggle_favorite`, the `ui.fav` action and "Add to / Remove from Favorites" in both sheets,
  the Recent/Favorites path chips (chips now: SD, USB0, USB1, Flash, FTP, Online, Home), the virtual list modes 2 and 3 (`vmode_` keeps 1 search and 4
  tape entries). `[Home]` stays (own row and chip, only once a home folder is set). Dead state removed with them: `files.recent0-2` and
  `files.recent0-2_none` (`libfiles.cc`), the three "Recent 1-3" Home-desk nodes and the 24 hidden `Recent n` / `Favorite n` text settings in
  `coreapp/cores/trx64u/menu.toml` (`menu_rec.inc` regenerated, goldens `ops_trx64u.txt` and `menu_c64.txt` lose the same names). An existing `.cfg`
  with `Recent n` / `Favorite n` lines keeps them as unknown keys; nothing reads them. The sheet of a file is one row shorter (8 universal rows).
  `media/recent` in `rpccontract.cc` is the emulator RPC contract's method list, not this list; it stays.
- **D6 (S5).** `libfiles_register` moved from `coreapp/cores/c64.cc` to `ops_register_base` (`coreapp/src/ops_glue.cc`): every core has the file operations
  from the common part. They only use `CoreFs` and `CoreCtx::lib`, so none needed a C64 detail. One finding: "Set as home folder" stores its value in the
  `Home Directory` setting that only the TRX64U menu declares; in any other core `settings.set` answers "no such setting". The home folder now says so in
  its status line ("until the core restarts: this core keeps no setting for it") instead of failing silently. Making it persist in every core is a
  separate decision (a common setting in every descriptor). **Done in D10 (§5 below).**
- **D7 core app (S6).** `MenuCtl::page(dir)`: in the F3 list (not a sheet, not the quick seek) the bare "+" / "-" page; a page is the rows the screen shows
  (`set_page_rows`, called from `screens.cc` `build_library` with `lib_rows(false)`; 10 until a screen has said). PageUp/PageDown share the helper
  (10 rows outside the F3 list, as before). The volume keys in the F3 list are SHIFT only: '{' '}' on the C64 keyboard, '_' for SHIFT+- on USB; a USB
  keyboard's SHIFT+= arrives as '+' and pages. Every other page keeps the T160 keys. The ue2emu state export (`uistate.cc`) does not call `set_page_rows`:
  its pages are 10 rows.

Bench: `tests/coreapp/test_f3.cc` (Atari descriptor, no C64 code; `run.sh` entry `f3`, 10 negative controls). S4: no `[Recent]` / `[Favorites]` row, no
Favorites row in a sheet, running a file leaves no `Recent` / `Favorite` key in the `.cfg`. S5: rename, New folder, SPACE marks, Mark all / Mark none, Copy
to and Move to on two marked files, Copy to clipboard + Paste here, Delete of the marked files, each on the `FakeFs`; the controls break the
registration, rename, mkdir, copy, move and delete. S6: 24 rows, page 8: "+" 0 -> 8 -> 16 -> 23 (clamp), "-" 23 -> 15 ... 0, PageUp/Down the same page,
'{' '}' '_' change the volume and not the selection, the bare keys do not change the volume there, on Home the bare keys are the volume. Controls: no
paging keys, a page of a fixed 10. `test_menuctl`, `test_contrib`, `test_cbm1541`, `test_createflow` follow the removed rows; `test_descrec` counts 160
settings of the TRX64U record instead of 184; `test_volume` does its USB-key steps on Home (its core opens on the F3 Library, where the bare '+' now pages).

Result (builder, `u64-build-env:ci`, CI cache mounted, 2026-11-04): `ci/apptests.sh` hostapp, coreapp (incl. `f3` and its 10 controls, all fail as they must),
loader: ok. RISC-V build of host app and core app: ok, `coreapp.app` 1635556 bytes of the 1966068 slot (`check_app_size.py`). Core app render bench: exit 0
(the F3 screens are drawn from the same code with two rows fewer in the sheet; no golden picture changed).

- **#38 (2026-11-04, Alex: "just fix it", translated):** Rename on a name longer than 55 characters: `entry_str_` holds 68 bytes
  (`menuctl.h:283`) and `entry_max_` is 55 (`menuctl.cc:3377`), so the prefill cuts the name and nothing can be appended (".adf").
  Fix: the entry takes a FAT long name (255), the field scrolls so the cursor stays visible; bench: an 80-character name renamed and
  extended. The other half of #38 ("no operation files.rename" in amiga1200, release t164-c83a83c2) is D6 above.
  - **§5 (built, branch `t180-rename-long`):** `entry_str_` is 260 bytes; Rename and New folder set `entry_max_` to `NAME_LONG_MAX` (255), every
    other entry (search 38, settings text, media set, ask/task names) keeps its own limit. `MenuCtl::entry_row` shows the text and the cursor
    in the row value; a text longer than `ENTRY_VIS` (56) shows `..` and the tail, so the cursor stays in sight (all entry rows use it, a text
    that fits looks as before). `files.rename` and `files.mkdir` take 255 characters (`ARG_STRING`) and build the target in 520 bytes (folder
    path plus name; at 260 a deep path would have cut the name silently). The fs layer needed no change for the write: `FileManager::rename` /
    `create_dir` take the path as given. The listing still cut a name at 55 (`FsEntry::name`, `FmFs::list`): built in the next note (`t180-list-long`).
    Bench `tests/coreapp/test_f3.cc`: 80 characters typed over the old name, 3 deleted, ".adf" appended: the row shows `..` and the last 40
    characters with `_`, RETURN gives the file with the full name on the mock fs; a 255-character name is taken whole (the 256th is not typed).
    Controls `f3_name_limit` (limit back to 55), `f3_name_scroll` (no scrolling), `f3_name_op` (`files.rename` argument back to 55): each
    fails red (4, 2 and 3 failed checks).
    Builder (`ci/trxapps.sh` in `u64-build-env:ci`, CI cache mounted, 2026-11-04): RISC-V build of host app and core app ok, `coreapp.app`
    1666680 bytes of the 1966068 slot (`check_app_size.py`); `ci/apptests.sh` ok (the three controls above fail as they must), render benches
    and fs64 benches ok; EXIT 0 in 20m35s.

  - **§5 (built, branch `t180-list-long`, 2026-11-05; "Rename takes 255, the list still cuts at 55" - a file with a long name could not be opened, mounted or renamed from the Library):**
    the listing holds the whole name. `FsEntry::name` is 256 bytes (`FS_NAME_MAX` 255, `fsif.h`); a buffer a listed name is joined into is `FS_PATH_MAX` (520). Per site:
    - *path, the whole name in `FS_PATH_MAX`*: the browser rows `list_` / `pick_list_`, the cursor path, the marks (`LibSel::marked`, 256 each), the clipboard, the search results (`vfull_`),
      the sheet's file, the view / rename / job paths, the sources and folder frames of Copy, Move and Delete (`libfiles.cc`), the path argument of an operation (`ARG_PATH`, `app.open file`:
      255 -> 519; the exported `maxlen` in `ops_trx64u.txt` and the REST contract follows), the noted-path ring, `AppHost`'s file, the 1541 library (`CbmDrive::path_`, the `DiskFs` image path, its image name
      `name_` 255: Swap Disk finds the next image by it), the FTP client's remote path and name, the mounted path of every core (Atari, Apple II, CPC, ZX tape, NES, SNES, Neo Geo, C64, C264, VC20), the
      SNES `.srm` and the Neo Geo `.sav` (named from the whole stem, the stem cut to stay a FAT name), the write-back's `NAME.tmp` / `NAME.bak` (the name part cut to 255).
    - *display, `fs_short_name` (start, `..`, the last 6 characters; the extension stays)*: `BrowseRow::name` (the F3 row), the names of the mounted media on the status line (window and sector media, `cart_name`,
      `program`, the Atari disks).
    - *kept short on purpose, `FsEntryShort` / `CoreFs::list_short` (72 bytes, cut at 55 and marked `cut`)*: the folder disk (16384 entries; its peak stays 3.1 MB, a name of 55 or more is still left out and
      reported, T128), the ROM finder and the ROM sets (they match the descriptor's names), the media-set list, the boot.cfg guard, the monitor's `dir`, the diag results. `FmFs` fills either layout straight
      from the FileManager's infos.
    - *refused, never cut*: a folder or image path over 199 (`path_`, `media_dir`, the pick path: "The path is too long"), a `.u2a` or library whose path does not fit its index, a dialog pick that does not
      fit the application's buffer. A mount path of 200 or more is mounted and not stored in the `.cfg`, as before.
    - *the board's limit, not ours (open, Alex)*: the FileManager reads a directory entry into a `FileInfo` of `INFO_SIZE` 128 bytes (`vendor/gideon/software/filemanager/filemanager.h:20`), so on the board a name of
      128 characters or more comes back cut at 127 from the listing and from every look-up by name (stat, rename, open): the core app holds 255, the board delivers at most 127. Widening it means a change in
      Gideon's FileManager that the host app compiles too (and the copy in `vendor/gideon` is renewed from the fork), so it was not made; `fmfs64` pins the limit.
    - *app ABI unchanged*: `SvcFsEntry` (72 bytes, `name[56]`) and `SvcFs::list` as they were; `LibLoader::svc_list` lists whole entries and converts (name cut to 55 as before); `libapi.h` checks the frozen
      72 bytes. No `list_long`: nothing needs the long name through the ABI yet.
    RAM, measured on the builder (`riscv32-unknown-elf-size` and `nm -S` of `coreapp.elf`, main against this branch): `coreapp.app` 1666680 -> 1670920 bytes (slot 1966068). Static `.bss` 13075600 ->
    12891044 (-184556); after `.bss` the linked region 0x30000..0xE90000 ends 330992 -> 511308 bytes later (that tail is all the region has beyond its sections; the 7995392 bytes of `ucHeap` are part of
    `.bss`, reserved for the heap). In: `LibSel` +33860 (marks 64 x 256, clipboard 64 x 520, cursor path). Out of `.bss`: the three 36864 byte listing buffers (the lists are read into `list_` itself),
    the search's, Neo Geo's parts (2 x 13712), Swap Disk's 18432, the FTP look-up's 18432, the app dialog's and the app index's lists. Heap (the 7995392 bytes): the menu's two lists are 512 x 272 = 139264
    each (were 36864: +204800 for good), `vfull_` +40960; for the call only: the search 139264 + 33280, Copy / Move / Delete frames up to 6 x 52224 (were 6 x 13824), the clipboard selection 33280, the app
    dialog 2 x 43520, Swap Disk 69632, the FTP look-up 69632, a Neo Geo start 29072, the app index 17408. The heap in use at run time was not measured (no board run).
    Bench: `test_f3` (an 80 and a 255 character `.atr`: listed, shown cut, mounted by its whole path, renamed, copied - also inside a folder -, moved, deleted); `test_media_mounts` (the Amiga DF0 pick of such
    `.adf`: the whole path reaches the mount, the status line shows the cut form, the 80 character one is stored in the `.cfg` and comes back, the 255 one is mounted and not stored); `test_cbm1541` (the drive
    pick: `.d64` of 80 and 255 characters through the `DiskFs`, name and path whole); `test_longnames` (`fs_short_name`, `list_short`, the write-back of a 255 character file keeps `.tmp` / `.bak` FAT names);
    `fmfs64` (the board's `FmFs` on exFAT: list, list_short, stat, rename, load, remove of an 80 and a 127 character name; a 255 character one comes back cut at 127); `test_libload` (the bridge: 100 characters -> 55). Controls, each fails red: `f3_long_name`, `f3_long_path`,
    `f3_long_ellipsis`, `f3_long_arg`, `f3_long_copy`, `mnt_long_name`, `mnt_long_shown`, `mnt_long_spec`, `drive_long_container`, `drive_long_name`, `drive_long_path`, `longnames_side`, `longnames_tail`,
    `longnames_list_short`, `list_longname`, `list_nocut`, `fs_list_cut`, `fs_list_fields`, `fs_list_max`. Not covered: the host app's own File Browser (`hostfs`, its own entry type), the REST / WebSocket
    body limits for a long path argument, Swap Disk (RISC-V build only: `trx64u_ops.cc` is stubbed in the native benches).
    Builder (`ci/trxapps.sh` in `u64-build-env:ci`, CI cache mounted, 2026-11-05, the committed tree): RISC-V build of host app and core app ok, `coreapp.app` 1670920 bytes of the 1966068 slot
    (`check_app_size.py`); `ci/apptests.sh` hostapp, coreapp (the run before ran 1159 units; this one 10, 1503 from the cache) and loader ok, render benches and fs64 benches ok (the CoreFs bench with
    the 80 / 127 / 255 character test and its two new controls); EXIT 0 in 5m51s.

### D1, D4 (branch `t180-d4-writeback`, 2026-11-04)

- **One write-back for every exit** (`coreapp/src/engines.cc` `media_writeback`): the cartridge (`wb_flush`, as its setting says), every window drive
  (`win_flush_all`), every sector unit incl. the folder disk's full fold-back (`sec_sync_all` -> `FolderDisk::sync`). 0, or 1 with the first reason; nothing
  is closed or dropped on a failure. Callers: `boot_cold_reset`, `boot_warm_reset`, `boot_reload`, `boot_restart` (`boot.cc` `flush_first`; the action stops
  and answers 1 with the reason, the machine is not held), Return to host (`menuctl.cc` `EK_RETURN_HOST`: status line, the core stays), `op_power_off` /
  `op_power_cycle` (`u64cfg.cc`). Before: `flush_first` and Return to host called only `wb_flush`, a failed `wb_flush` still let the reset run.
- **Unmount** (`win_eject`, `sec_eject`/`eject_unit`): written back first; a failure returns 5 with the reason ("DF0: ... It stays mounted."), the medium stays
  mounted with its dirty state. Before: `eject_unit` ignored the `sync()` result and deleted the folder disk. F8 eject shows the reason.
- **Replace (D1)**: `win_mount`, `sec_mount` open the new file, then unmount the old medium completely; a write-back that fails closes the new file and
  returns 5, the old medium stays. `sec_mount_folder` writes the old unit back first (the same folder may be mounted again, its tree is read
  afterwards), mounts the new tree, then lets the old unit go; a refused new folder leaves the old unit mounted (T128 S8).
- **A sync that failed is not forgotten** (`WinSlot::unsynced`): `flush_drive` cleared the dirty tracks before the sync, so after a failed sync the next
  flush found nothing to do and eject went on. Found by the bench; fixed with the flag.
- **"Saving media..."**: `MediaSaving` (engines.h) calls `CoreCtx::saving` for as long as a write-back that has something to write runs; `run.cc`
  `saving_note` shows `ui_message("Saving media", "Saving media...")` and renders at once, when the menu is open, and clears it at the end (the menu draws
  itself again after the key). With the menu closed (REST reset) nothing is drawn.
- The engines with their own flush (Apple II, Atari SIO, CPC, the drive library, the SNES and Neo Geo saves) are done on `t180-d4-drives`, see below.
- Bench `tests/coreapp/test_media_writeback.cc` (run.sh `media_writeback`, controls `mwb_*`): S7 on the A1200 record - DF0, HD0 image and a folder disk with
  writes no sync has taken yet; eject, replace, cold/warm reset, reload, restart and Return to host each leave the files and the folder byte-identical to a
  full sync; the sync-fail and the full-medium case keep every medium mounted, stop every action (the hold was never called), and the same actions go
  through once the medium is back. S1: ten swaps of DF0 and HD0 in a row, one handle each time, 859 us per swap every time.
- Checked 2026-11-04 on the builder (u64-build-env:ci, CI cache mounted): host app and core app build for RISC-V (core app 1642556 bytes, slot 1966068),
  `ci/apptests.sh` hostapp, coreapp (1080 units ran, 11 new `mwb_*` controls and the controls this change touched fail as they must) and loader pass.

### D2, D5 (branch `t180-persist`, 2026-11-04)

- **Where:** `coreapp/src/mediamounts.{h,cc}` (the item, the stored mounts, the restore, the sets and their operations), `settings.cc` (the keys in the `.cfg`),
  hooks in `winslot.cc` / `sectors.cc`, `boot.cc`, `menutree.cc` / `menuctl.cc` (the rows).
- **The item.** `persist_extend` adds "Persist medium mounts" (No | Yes, default No) to the settings like Picture and Volume, for a core that has window or sector
  media. It is a row of F8 (System tab, after Bug report), not of the Settings tab. Its apply operation (`media.persist_apply`) stores what is mounted now (Yes) or
  removes the keys (No).
- **The keys.** One per slot in the core's `.cfg`, `Mount <prefix><n>=<what>`: `Mount DF0=/usb0/WB32/Install.adf`, `Mount HD0=folder /usb0/Amiga/DH0, 2 GB` (the size as
  it was typed; typed nothing: `folder <path>` and the default again). `Settings` keeps them apart from the items (`mount_get`, `mount_put`, `mounts_clear`; at most 12,
  a value under 200 characters); "Reset to host defaults" removes them. Written at every mount and unmount, whatever Auto Save Config says: the write is the whole
  `.cfg`, so a change that Auto Save Config held back goes with it.
- **The hook.** `win_mount` / `win_eject` / `sec_mount` / `sec_mount_folder` / `sec_eject` record what is in the slot (`WinSlot::spec`, `SecUnit::spec`) and call
  `mounts_changed`; a replace (D1) is one store, not two. Nothing is stored before the start has restored (`Engines::mounts_ready`).
- **The start.** `boot_start` calls `mounts_restore` after the ROMs and the settings and before `d->release` (the bench reads the mount registers in the release hook).
  Item No: the keys are ignored (and removed when the item is switched to No). A missing medium (file, stick, folder): the slot stays empty, the reason goes to the log and
  to the menu's status line when it opens ("Stored media not mounted - DF0: ..."), the key stays until that slot is mounted or ejected by hand. A folder shows
  "Mounting the folder..." through `BootUi`. Reload core and Restart core unmount nothing, so the media are still there; only a start of the application (power-up,
  return from the host) takes the path.
- **Media sets (D5).** `<core folder>/media/<name>.mnt`, the same keys under the core's store name, every mounted slot, nothing for an empty one. F5 Summary (the media
  tab) gets "Save media set..." (the name prompt of New folder; at most 24 characters) and "Load media set..." (a list of the files in `media/`; the row says how many
  there are). Loading: `media_writeback` first (a failure stops it, nothing changes, no reset), then slot by slot a mount (D1: the old medium is written back and closed)
  or an unmount for a slot the set leaves empty; a medium that is not there leaves its slot empty and goes into the message ("Left empty - DF1: ..."); then one
  `boot_cold_reset`; persist stores what is mounted. The same operations (`media.save_set`, `media.load_set`, state `media.sets`) are there for REST. After a load the
  menu closes (the core runs).
- **Coverage on this branch.** The window drives and the sector units with folder disks: the Amiga (DF0-DF3, HD0, HD1; A500 and A1200) and the ZX Next (the SD image). The
  engines that keep their own mount state (Apple II, Atari, CPC, ZX tape, the drive library, the cartridge ports, the game cartridges) are covered by the section
  "D2, D5 for every core" below (branch `t180-persist-all`): there the item and the set rows are only added to a core whose slots all work.
- **Bench** `tests/coreapp/test_media_mounts.cc` (run.sh `media_mounts`, controls `mnt_*`), the A1200 record on its model with a `.cfg` and a core folder in the fake file
  system. S2: Yes, DF0 + HD0 folder (0.1 GB) + HD1, a new run: all three back, same paths, same folder size, mounted when the core is released, "Mounting the folder..."
  shown; replace, eject and a third slot stored as they happen; No: nothing stored, nothing mounted, switching to No removes the keys, keys in a file with No do not act;
  the F8 row (not on the Settings tab) switches it with RETURN.
  S3: file, image and folder gone: slots empty, core released once, the note names the three, keys kept, a mount by hand elsewhere leaves them, an eject of the empty slot
  removes its key. S8: save from the menu, change four slots, load from the list: slots as saved, the folder size the same, exactly one hold and one release; a set with
  a missing medium; a failing write-back on a slot behind the others changes nothing and does not reset.
- Checked 2026-11-04 on the builder (`ci/trxapps.sh` in u64-build-env:ci, CI cache mounted): host app and core app build for RISC-V (core app 1654564 bytes, slot 1966068),
  `ci/apptests.sh` hostapp, coreapp (1105 units ran: `media_mounts` 155 checks and its 14 controls `mnt_*`, each fails as it must) and loader pass, the render benches
  (view, core app, host app) pass. Findings on the way: `%.*s` in a format is refused by the printf lint (the board's printf has none), the control `menu_group_setting` had to
  follow the Persist row's filter in `menutree.cc`, and a first version of the bench never looked at the F8 row, so its control passed (`mnt_f8_row`) - fixed.
- **D7, host File Browser (2026-11-04, builder, `u64-build-env:ci`, CI cache mounted):** `browse_page_key()` in `host-app/software/hostapp/hostui.cc` called from the
  three V_BROWSE key branches (normal, pick target, inspect): "+" / "-" move `bsel` by `BROWS` rows (the rows the list shows), clamped to 0 and `bn - 1`;
  letters untouched. Bench S6 in `tests/hostapp/render/render.cc` on a new folder `Usb0/pages` (".." + 26 files = 13 + 13 + 1 rows): "+" 0 -> 13 -> 26,
  a third "+" stays, "-" back one page and stops at the top; a 20-bundle folder clamps to the last row; `C` opens the target pick and "+" / "-" page there too.
  Control `nopage` (run.sh): the key function disabled, 7 checks red. `ci/trxapps.sh` rc=0 (hostapp.app 873,732 bytes, render bench 1219 checks, 0 failed),
  `ci/apptests.sh hostapp` rc=0. Core app F3 browser (SHIFT volume) is not part of this build.
- **Footer not changed:** `+/- page` after `UP/DOWN select` in the browser footers made `hostui_test_foot_overflow()` red (a footer wraps) and pushed the F5-switch area
  over its limit (406704 px > 406300) on the first run; reverted, the footers stay as they were. Open for Alex: shorten another footer text to make room, or leave the key undocumented on screen.

### D4, D1 for the core-specific engines (branch `t180-d4-drives`, 2026-11-04)

Found by reading every `fs->open(..., true)`, `sync()` and `fs->save` under `coreapp/cores/` and `app/lib/cbm1541`: the engines below keep their own write-back; the
base's `media_writeback` did not know them. **One hook**: `CoreDesc::media_flush` (0, or 1 with the reason in msg; the medium stays) and `CoreDesc::media_pending`
(the "Saving media..." display), last in the record; `media_writeback` calls them after the window drives and the sector units, so every exit that was wired
already (cold/warm reset, reload, restart, Return to host, power off/cycle) reaches every engine without code of its own. Eject and replacement are each
engine's own operation and write back first inside it.

| Engine | Before | Now |
|---|---|---|
| Apple II floppy (`apple2_disk.cc`) | `a2_eject` dropped the disk although `a2_flush` failed; `a2_mount` ignored the flush and dropped; `file->sync()` result ignored; a track the file refused was acknowledged to the core and lost | `a2_flush` fails on a refused sync and on a refused track; the track's bits are kept (`A2Drive::kept`, one per drive) and written again by the next flush; `a2_eject` / `a2_mount` stop with "D1: ... It stays mounted." and the new file is closed again |
| Apple II slot 7 (`apple2_hdd.cc`) | eject and mount `drop()`ped the unit with `sync()` ignored | `sync_unit` before both; `a2_hdd_sync` answers false with the reason |
| Atari SIO (`atari_sio.cc`, `atari_media.cc`) | `flush_all` cleared `dirty_` before the sync and ignored a failure; eject / mount / .xex deleted the disk anyway | `flush_drive` / `flush_all` keep the drive dirty on a failed sync (the idle flush tries again, told once) and answer false with the reason; `set_disk` stops, the new disk is deleted again; `a8_insert_cart` writes back before it holds the machine |
| CPC (`cpc_disk.cc`) | eject refused on a failed flush (checked, held); mount did not take the FDC's marks first, so a write of the last second was lost at a replacement; a failed `sync()` cleared the pending marks and was forgotten | `cpcd_mount` polls first; `CpcDisk::unsynced` keeps a failed sync owed; `cpcd_flush_all` answers false with the reason |
| Drive library, C64 / C128 / C264 / VC-20 (`d1541_drive.cc`) | eject: `wait_for_writeback` + `discard_gate` (ask twice); mount only `discard_gate`, the head's pending tracks were not written; reset / reload / restart / return to host did not touch the drive; a track the file refused stayed in the drive and eject asked to throw it away | `CbmDrive::settle` (wait, then write the refused tracks again, `failed_tr_`) before the gate in eject and mount; `CbmDrive::flush` for the hook; the gate stays as the person's explicit "throw it away" for eject. A 1581 sector the file refused cannot be written again: every exit stops, eject asks twice. An unlinked disk has no file and stops nothing. `Save` clears the debt (`failed_tr_`) |
| SNES battery RAM (`snes.cc`) | `snes_init` flushed and ignored a failure; another game replaced the running one | the hook; `snes_run` stops before the hold when the old game's save fails, the game stays loaded |
| Neo Geo backup RAM / card (`neogeo.cc`) | `start_game` saved the leaving game and ignored a failure; a failed save was acknowledged to the core and never tried again | `NeoState::save_failed` keeps it owed (the service tries again, told once); the hook writes only when the game wrote or a save is owed (a card that cannot be written does not block resets of an unchanged save); `start_game` stops |

- Limits kept: one refused Apple II track per drive is kept (a second refused track while the first is still owed replaces it); a refused 1581
  sector is not kept at all (the controller has acknowledged it), so only the "ask twice" gate frees it.
- Bench: `test_apple2_disk` (`test_writeback`), `test_atari_sio`, `test_atari_media`, `test_cpc`, `test_cbm1541`, `test_snes`, `test_neogeo` each have S7 cases
  with a forced failure (sync refused, track refused, medium full), `test_media_writeback` the plumbing (hook asked once on every exit, its refusal stops the
  exit with the reason, the other media are written anyway, "Saving media..."), `test_desc` that every one of the nine descriptors hands its engine over. Controls in
  `run.sh` (`hook_*`, `mwb_hook_*`, `a2d_*`, `a8_*`, `sio_*`, `cpc_*`, `drv_*`, `snes_*`, `neo_*`).

- Checked 2026-11-04 on the builder (`ci/trxapps.sh` in u64-build-env:ci, CI cache mounted): host app and core app build for RISC-V (core app 1648940 bytes, slot 1966068),
  `ci/apptests.sh` hostapp, coreapp (865 units ran, every new control fails as it must; found by the controls: the CPC flush-at-reset and the Atari eject controls
  were obsolete and were retargeted, a 1541 clause was redundant and was removed) and loader pass, render benches pass.

### D2, D5 for every core (branch `t180-persist-all`, 2026-11-04)

Alex, 2026-11-04 (translated): "well, for all cores if at all". Every core that can hold a medium gets the F8 item "Persist medium mounts" and the F5 rows "Save media set..." /
"Load media set..."; the item and the rows are added only where every slot of the core works (`mounts_have_slots`).

- **The interface** (`desc.h` `MountSlots`, last member of `CoreDesc`: `mount_slots`): an engine with a mount state of its own lists its slots - `n`, the key words, `spec(c, i)` (what is
  in the slot, in the words of the file; null: empty), `mount(c, i, spec)` (D1: replaces; 0, or 1 with the reason; a slot that cannot take the medium keeps what it held) and
  `eject(c, i)`. The base's window drives and sector units need no table. The slot order is: windows, sector units, then the engine's. `desc_check` refuses a table with no
  operation, more than 8 slots, a name that is empty, longer than 9 (the key is `Mount ` and the name, at most 15 characters), has a `=` or `[`, repeats, or starts like a slot of the base, and more media than the `.cfg` keeps (12).
- **The keys** stay `Mount <name>=<spec>` in the core's `.cfg`. A spec is the medium's path (it starts with `/`), with a word in front where the engine needs one: `ro <path>` and
  `unlinked <path>` (drive library), `xex <path>` (the Atari's `.xex` boot disk in D1), `folder <folder>/<set name>` (a Neo Geo ROM set; the Amiga's `folder <path>, <GB> GB`).
  `CoreCtx::mount_changed` / `mount_notify` (ctx.h): the engine tells the application whenever one of its slots changes (the application sets `mounts_engine_changed`, an engine
  tested alone has none); the notification is in the engine's own mount and eject (a failure after the old medium went out notifies too: the slot is empty and is stored so; an
  eject of an empty slot notifies so that its key goes). The NES, SNES and Neo Geo mount ends in `boot_finish`, whose settings pass runs the Persist item's apply and stores
  every slot, so only their eject and their failure after the old game went notify. `CoreCtx::mounts_ready` replaces `Engines::mounts_ready` (an engine-only core has no `Engines`).
- **Restored before the release.** `mounts_restore` (called by `boot_start` after the ROMs and the settings, before `d->release`) sets `CoreCtx::media_restoring` while it mounts. With
  it set, `boot_cold_reset` applies the reset-time settings and returns 0 (the machine is held and `boot_start` releases it once), `boot_finish` applies the settings (the SNES's
  region follows the header) and does not release. So the engines'
  own mount functions (`a8_insert_cart`, `c64_cart_run`, `nes_run`, `snes_run`, ...) are used as they are: what they did after the image (reset, release) is the caller's. The same flag
  is set while a media set is loaded: `media_writeback` first, then slot by slot a mount or an eject, then exactly one `boot_cold_reset`. A core whose cartridge is its program (NES,
  SNES, Neo Geo) and whose set leaves the cartridge empty is left waiting for a program, with no reset (the message says so).
- **Slots by core:**

| Core | Slots (key words = the menu's labels without the colon) | Spec | Restored by |
|---|---|---|---|
| Apple IIe | D1, D2, S7.1, S7.2 | path | `a2_mount` + the Disk II boot ROM (`mount_floppy`), `a2_hdd_mount` |
| Atari 800 XL | D1, D2, D3, D4, Cartridge | path; `xex <path>` (D1); path | `a8_mount`, `a8_run_xex`, `a8_insert_cart` (the image into the window, CART1, held) |
| Amstrad CPC | A, B, Tape | path | `cpcd_mount` (the model the setting names: a 464 has no drive), `deck_insert` |
| ZX Spectrum | Tape | path | `deck_insert` |
| C64 (TRX64U) | Drive A, Cartridge | `[ro \| unlinked ]<path>`; path of the `.crt` | `CbmDrive::mount`, `c64_cart_run` |
| C128, VC-20 | Drive A | as C64 | `CbmDrive::mount` |
| C264 | Drive A, Cartridge | as C64; path of the `.bin` | `CbmDrive::mount`, `cart_load` (sets the model to Plus/4, applied by the reset-time settings) |
| NES, SNES | Cartridge | path | `nes_run` / `snes_run` (the program is loaded, the release is the start's); emptied: the machine is held, no program, the start waits for one |
| Neo Geo | Cartridge | path; `folder <folder>/<set>` | `run_file` / `run_dir`; emptied as the NES |

- **Not slots:** the "Program" rows of the C64 family (a PRG in RAM, not a medium: RAM does not survive a start), a Spectrum snapshot (the same), the Amiga ROM / Kickstart and
  every core's ROM files (the ROM manager's). The drive library's blank or `Save`d disk has a path like any other.
- **The C64's menu** is its record: `menu.toml` declares the item as a core setting (group `system`, listed by the F8 Expert tab) and a tab `Media sets` (widget `media`) in F5;
  `menutree.cc` lists a generic item in a settings tab only where the tab names its group.
- **Found by the bench, fixed here:** the Atari cartridge type (`CART1`) is written by the hold (`a8_init`), which the restore does not repeat, so `a8_insert_cart` writes it itself
  (control `mme_a8_cart_reg`); `boot_finish` under the flag first skipped the settings pass, so a SNES cartridge restored at the start kept the region of the last one (it applies them now);
  the first version of the bench could not see the NES / SNES / Neo Geo success notification because the settings pass stores it anyway - the notification was removed there instead of
  tested; the drive library and the CPC dropped nothing on an eject of an empty slot, so the stored key stayed.
- **Left out, on purpose:** the C64 family's "Program" rows (a PRG in RAM), a Spectrum snapshot and every ROM file are no media a start can mount again (RAM does not survive it).
  The tape deck is restored at its start (position 0), not where it was.
- **Bench** `tests/coreapp/test_media_mounts_engines.cc` (run.sh `media_mounts_engines`, controls `mme_*`; `desc_ms_*` for the checker): one profile per core family on the real
  descriptors - Apple IIe (4 slots), Atari (5), CPC (3), ZX (1), VC-20 / C128 / C264 / C64 (the drive, the cartridges), NES, SNES, Neo Geo - each with the same scenarios. Menu: the Persist
  row is in F8 (not in F5 or F6), the two set rows in F5. S2: persist on, each slot mounted through the engine's own operation and found in the `.cfg`, replaced (one store, the new path), ejected (the
  key goes), mounted again; the next start has nothing before `boot_start`, and at the first release (sampled in the release hook, with a register of the core for the cores where
  one shows it: slot 7 control, Atari `CART1`, NES header, C64 cartridge type, C264 cartridge bit) the specs are those of the last run, one release, no note; persist off stores and mounts
  nothing, switching it on stores, off removes the keys, keys in a file with the item off do not act. A replacement that fails after the old medium went (read budget of the fake file system
  stepped up until it happens) leaves the slot empty and the key gone. S3: every medium deleted: every slot empty, one release (a game core waits for its program, as at a first start),
  a note naming every slot, the keys kept; an eject of an empty slot removes its key, a mount by hand keeps the others. S8: a set saved with the last slot empty, every slot changed, the set
  loaded: every slot as saved, the empty one empty, exactly one release; a set naming a missing medium leaves that slot empty ("Left empty - ..."); a refused write-back (the engines' hook made
  to fail) keeps every slot and does not reset, and the same call goes through after it; a game core's set that leaves the cartridge empty does not reset (the message says why). Particular:
  Atari `xex <path>` in D1 (an ATR over it takes the word away), the drive's `ro` and `unlinked` come back in their mode, a CPC 464 leaves A and B empty with a note and restores the tape, the
  C264 cartridge sets the Plus/4 model, a Neo Geo ROM set folder comes back as a folder.
- Checked 2026-11-04 on the builder (`ci/trxapps.sh` in u64-build-env:ci, the CI cache mounted as /cache, container `t180all-riscv`): host app and core app build for RISC-V (core app
  1,666,568 bytes of the 1,966,068 slot), `ci/apptests.sh` hostapp (cached PASS of the same inputs), coreapp (93 test programs, 1403 controls, 1241 units ran: the new bench `media_mounts_engines`
  1431 checks, its 48 controls `mme_*` and the 7 `desc_ms_*` each fail as they must) and loader pass, the render benches pass (host 1219 checks, core app 250 checks; no golden picture changed).
  Findings of the run: four older controls had a line this change moved (`media_eject`, `media_xexkeep`, `cpc_nodrive`, `cpc_tape_base`) and were retargeted; `tools/test_descrec.py` and
  `test_descrec.cc` count the record's settings (161 with the Persist item). `ci/lint.sh` is green except "every bench registered": `cores/c128/sim/run_robots.sh`, `run_vicblk.sh` and
  `run_vicrom.sh` (from `main`, not this branch) are in neither `ci/sims.txt` nor `ci/sims_excluded.txt`.
- **Open for Alex:** the C64's F5 tab 4 "Media sets" (the two set rows) is a deviation from the UX concept (its media tab is "Summary", which the C64 does not have); listed in
  `tools/check_menu_concept.py` DEVIATIONS. Where else the rows could go (the F1 Home desk, the Drive A tab) is for him to say.

### D8 a cursor in every text entry (branch `t180-d8-cursor`, 2026-11-05)

- **One editor, `app/edit/lineedit.{h,cc}`** (moved from `host-app/software/hostapp/`, where the host's "Rename menu entry" had it since T139 D3; both application Makefiles read `app/edit`
  through VPATH). A `LineEdit` is buffer, length, cursor, limit, the first shown character and the characters it refuses; `le_key` is key -> edit. Buffer 255 characters (#38), was 63.
  The core application's `entry_str_` / `entry_slen_` / `entry_max_` are gone: `MenuCtl::entry_le_` holds every text entry (Rename, New folder, search, text settings, a task's name, a set's name,
  a core's `ask`); `entry_load(text, max)` starts one, `text_key` hands every key that is not RETURN, <- / ESC or a function key to `le_key`. The host app has one text entry only (Rename
  menu entry); its File Browser has none.
- **Keys.** CRSR left / right move the cursor, HOME and CRSR up go to the start, END and CRSR down to the end, typed characters insert at the cursor, DEL (INST/DEL, `KEY_BACK`) deletes before it,
  `KEY_DELETE` under it, INS opens a blank, CLEAR (SHIFT+HOME) empties the field - the last four as the host's field always had them. RETURN takes, <- / ESC cancels, unchanged. CRSR left was not
  "back" in an entry: `text_key` took no cursor key (they fell to `default` and were dropped), so <- / ESC remain the only cancel. The refused characters are as before: a name not
  `/ \ : * ? " < > |`, the search and a text setting any printable character.
- **Window.** `le_row` builds the row's value: the whole text with a `_` between the characters where the cursor is, when it fits `ENTRY_VIS` (56) cells; else `le_scroll` moves the window the least
  that shows the cursor (`first`, kept in the field, so it follows as the screen is drawn) and ".." marks hidden text on either side. A secret text setting shows `*` per character, the cursor in it.
  The hint lines under the rows are unchanged.
- **Coordinator proposal:** CLEAR keeps its meaning (empties the field), so "to the end" on the C64 keyboard is CRSR down, not CLR / SHIFT+HOME; the host's render bench (`kb_push(KEY_CLEAR)`) and the
  host field depend on it. Alex to say if CLR should be "end" instead.
- **Host application:** only the include moved and the buffer grew; the rename field draws as before (the box fits the 47 characters of `BOOT_NAME_MAX`), so no rendered pixel changes there.
- **Proof.** `tests/hostapp/test_lineedit.cc`: insert / delete at start, middle and end, the bounds of the cursor, the limit at 255 (an insert in front of a full field refused), the window follows by
  one and leaves no blank tail, `le_row` with the cursor in place, the markers and the mask; controls `le_clear_ignored`, `le_window_stuck`, `le_window_blank_tail`, `le_row_no_cursor`,
  `le_insert_appends` besides the three of T139. `tests/coreapp/test_f3.cc`: Rename of an 80-character name with HOME, CRSR right, END, CRSR left, DEL and typed characters gives the expected name,
  the row shows `_.adf` with the cursor before the extension, CRSR left does not cancel; controls `f3_cursor_keys` (CRSR left dropped), `f3_cursor_insert` (insert appends), `f3_cursor_draw`
  (no cursor in the row); `f3_name_scroll`, `menu_namelen` and `cf_garbage_len` point at the editor / `entry_load`. The editor is listed in every `tests/coreapp` program that links `menuctl.cc`.
- **Checked 2026-11-05 on the builder** (`ci/trxapps.sh` in `u64-build-env:ci`, the CI cache mounted as /cache, container `t180d8-trx`, rc 0, 28 min): RISC-V host app 874,172 bytes, core app 1,680,432 bytes
  (slot 1,966,068); `ci/apptests.sh` hostapp (48 s), coreapp (413 s) and loader PASS, every new control fails as it must; render benches: core app `render_coreapp` PASS, host app `render_main` 1219 checks,
  0 failed. Findings of the runs: a mutant that overruns the buffer died before its buffered FAIL lines were written (`test_lineedit` now has an unbuffered stdout); a first version that made CLEAR
  "end" broke 18 host render checks (the rename bench clears the field with it), hence the proposal above.
  - **§5 D9 (built, branch `t180-d9-unzip`, 2026-11-05; "Unzip here" in every core, #43):**
    - *CI and release:* `ci/trxapps.sh` builds `apps/unzip` to `unzip.u2a` beside `testlib.u2a` (the key has `apps/unzip`); `ci/package.sh` puts it flat into the package (red target `unzip` when missing);
      `tools/release_mc.sh` fetches it and `tools/make_release_mc.sh --u2a` ships it flat in the bundle (never as `cores/apps/`: `check_package_layout.py` refuses a folder below `cores/`);
      `release_notes_mc.py` lists it and gives the stick path `/cores/apps/unzip.u2a` (INSTALL.md sections 2, 4). `tools/test_make_release_mc.py`: file shipped, text present, absent without `--u2a`, missing file refused.
    - *Shared folder (coordinator proposal):* `AppIndex::folders` returns the core's own folder first, then `<dir of the .cor>/apps` (else `<root>/cores/apps` per root), up to `APP_FOLDERS_MAX` 12. The scan keeps the first of a name, so the core's own copy wins.
    - *Test* `tests/coreapp/test_unzip.cc` (45 checks, 5 controls): the index of a vc20 `.cor` finds `unzip` only in `/sd/cores/apps`; the merged descriptor offers "Unzip here / Unzip to folder / Extract one file" on `.zip`
      (from the real `app.toml`, packed by `tools/u2a_pack.py`); the real `LibLoader` loads the file and runs the Mac build of `apps/unzip` (`unzip.c`, miniz, `glue.cc`, `u2p_compat.c`) over the FakeFs: the files of a synthetic zip
      are byte-equal in the folder of the zip. Not covered: RISC-V instructions (the image is a fixture) and the F3 sheet widget (the merged file types it is built from). Controls: wrong shared name, no shared folder, no root
      folders, own folder not read, wrong target folder.
    - *What unzip does today (read, not changed):* "here" and "one" write with `fopen "wb"`, which empties an existing file - **it overwrites silently** (the test pins it). An existing folder entry is success (`mkdir`).
      Paths are joined into 256 bytes (`PATH_MAX_LEN`): `<folder>/<entry>` of 256 or more is refused and counted "Failed", never cut - a 255 character entry name cannot be written (the test pins it; 200 characters are); the
      glue takes the zip path in 199. Open (Alex): overwrite policy; 255 character names.
  - **§5 D9 fix (#43 reopened, branch `fix-unzip-43`, 2026-11-10):** a tester, release main-2bd67963: "If I press enter on a .zip file there is no 'Unzip here' option." Cause, read from the code
    and the board: the lookup is right (`AppIndex::folders` + `scan` at core start, `tests/coreapp/test_unzip.cc`), the file was never where it looks. The release ships `unzip.u2a` loose beside the `.cor`
    files; the updater (`vendor/gideon/software/application/update_u2p/update_trx.cc`, `find_bundle`) copied only `.cor` and `.cfw` into `<medium>/cores`, the `.txu` path (INSTALL.md 3a) copies nothing,
    so `/cores/apps/unzip.u2a` existed only after a manual copy that INSTALL.md section 4 mentioned in one bullet (the test board has no `/SD/cores/apps` at all, read by FTP 2026-11-10). Second hole:
    F8 "Cores and ROMs" (`coremaint.cc`) listed `/cores/apps` as "folder of no core", deletable, so "delete files that belong to no core" removed the shared folder. Fix: (1) the updater copies every `.u2a`
    beside the update file into `<cores>/apps` (creates it, no boot entry); (2) `cm_scan` skips the folder `apps`, `cm_may_delete` refuses everything below `cores/apps`
    (`tests/hostapp/test_coremaint.cc`, controls `cm_apps_orphan`, `cm_apps_deletable`); (3) INSTALL.md 3 says the installer does it, 3a (the `.txu` copies no file) says where to copy it
    (`tools/test_make_release_mc.py`). Not built: the `.txu` flow does not install applications (copy by hand, 3a); the updater change is checked by compile only, a board install is open.
  - **§5 D10 (built, branch `t180-d10-home`, 2026-11-06; a home folder per core, #40):**
    - *Mechanism:* `coreapp/src/home.{h,cc}`: `home_extend` adds "Home Directory" (a text, 60 characters) and "Enter Home on Startup" (No/Yes) after the core's own settings,
      group `system`, apply `home.apply` (a no-op operation; the F3 Library reads the values when it opens). `run.cc` calls it after `persist_extend`, so every core gets both, stored in
      that core's own `.cfg`; no descriptor carries a block for them. A description that declares the keys itself (the TRX64U menu: `menu.toml` `Home Directory`, `Enter Home on Startup`,
      group `expert`) keeps its own and gets none added, so the C64 has each key once (`home_extend` over a descriptor that has them: unchanged count; the golden menu stays).
      The F8 System page lists the added items with "Persist medium mounts" (`menutree.cc`, `home_is_generic`), the Settings tab does not.
    - *Reading:* `MenuCtl::open` takes the home folder and "Enter Home on Startup" through `home_dir` / `home_enter` (`Settings` of the core) instead of `cfg_text` / `cfg_value`, which the
      bench stubs answer with defaults. "Set as home folder" (`libfiles.cc`) stores through `settings.set` as before; the status line "(until the core restarts: this core keeps no setting for it)"
      is gone - `settings.set` can no longer answer "no such setting" in any core (the D6 note above describes the state before D10). `tests/coreapp/golden/ops_trx64u.txt` lists `home.apply`.
    - *Bench* `tests/coreapp/test_home.cc` (A1200 descriptor, `run.sh` entry `home`, 28 checks, 6 controls): `home_extend` adds two items to the A1200 and none twice; "Set as home folder" in the sheet of a
      folder row writes `Home Directory=/usb0/games` into the `.cfg`; a new session with `Enter Home on Startup=Yes` opens F3 in `/usb0/games`; with `No` the value stays and F3 opens on `/`; F8 lists both rows and
      the Settings tab neither. Controls: `home_none` (nothing added), `home_dup` (second pair), `home_not_stored`, `home_always`, `home_never`, `home_f8`; each fails red.
    - *Checked 2026-11-06 on the builder* (`ci/trxapps.sh` in `u64-build-env:ci`, the CI cache mounted as /cache, container `t180h-trxapps`, rc 0, 23 min): RISC-V host app 874,172 bytes, core app 1,681,592 bytes
      (slot 1,966,068); `ci/apptests.sh` hostapp, coreapp (419 s, 690 units ran) and loader PASS; render benches: core app 250 checks, 0 failed, host app 1219 checks, 0 failed. The first build failed
      at the link step ("undefined reference to `home_dir`"): the RISC-V source list (`host-app/target/u64ii/riscv/coreapp/Makefile`) names every file, `home.cc` was missing there.

### P4 file operations: GitHub #42 findings (branch `fix-files-42`, 2026-11-10; a tester on release main-2bd67963)
- **§5 New folder (#42: "It's not possible to create a folder"):** cause `coreapp/src/menuctl.cc` `activate()`: "Create folder here" and "Paste here" live only in the sheet of a
  folder row (RIGHT on a folder, `filemgr.cc` `g_folder_items`); a folder with no subfolder has no such row, and the sheet of a file does not carry them. Fix: RIGHT on the ".." row opens
  the same folder sheet without "Open" and without the per-item entries (Create folder here, Paste here when the clipboard holds something, Mark all / none, Set as home folder act on the
  folder the browser is in). Side effect: RIGHT on ".." no longer goes up (RETURN and LEFT do). `libfiles.cc` `op_mkdir` and `fm_fs.cc` `mkdir` were not the cause.
- **§5 Decision 2, the clipboard follows a rename (#42: "When copying to clipboard, then renaming the copied file and then pasting from clipboard gives an error 'failes, cannot read:filename'"):**
  the clipboard holds paths (`CoreCtx::lib.clip`). A rename of exactly such a path, or of a folder above it, rewrites the clipboard entry to the new name (`libfiles.cc` `op_rename`), so the
  paste copies the renamed file - the tester's "a way to duplicate a file" works. A source that is really gone (deleted, moved) is said so: "gone (renamed, moved or deleted since): <name>"
  instead of "cannot read".
- **§5 Decision 3, a copy onto a taken name is a duplicate (#42: "Copying should just add a "1" to the filename and make a duplicate file if the file exist"):** Paste here and Copy to
  (kind copy) never skip and never overwrite: a top-level item whose name is taken becomes `<name> 1.<ext>`, then `2`, `3` ... up to 999 (`libfiles.cc` `free_name`); the number goes before the
  last extension (`arc.tar 1.gz`), at the end for a folder or a name without one (`fresh 1`, `README 1`). A folder is copied whole under its new name. Move still skips a taken name ("skipped (the name exists there)"),
  because a move onto a name that is there would be a rename of something else.
- **Host app (read, not changed):** the host's File Browser (`host-app/software/hostapp/fileops.cc`) is a separate copy: no clipboard and no New folder there; copy and move onto a taken name answer `FO_EXISTS` ("a file of that name is there", the replace question). The findings do not apply; whether the host wants the same duplicate rule is open.
- *Bench:* `tests/coreapp/test_f3.cc` (block "GitHub #42"): New folder in a folder without a subfolder via the ".." sheet; Paste here in the same folder twice (`x 1.bin`, `x 2.bin`), no extension, double extension, folder with content; Copy to clipboard, Rename, paste elsewhere (clipboard path updated, file arrives); deleted source says "gone".
- *Checked 2026-11-10 on the builder:* `tools/dev.sh test` rc 0 (test_f3 125 checks), `tools/dev.sh riscv` rc 0. `tests/coreapp/golden/keywalk.txt` regenerated: RIGHT on ".." in the F3 Library is now the folder sheet (Atari, NES, Apple II, TRX64U F3/browse lines moved - TRX64U through the shared `menuctl.cc`, its own files untouched). No emulator run: the sheet is the existing one, only reachable from one more row.
