# T128 — A folder on the stick/SD is an Amiga hard disk, read and written like an .hdf, without the .hdf

**Status:** BUILT 2026-10-26 (host benches S1-S10 green; board steps B1-B6 open). Decided 2026-10-26 (all questions answered or taken as proposed, §4 settled with Alex). Where §3 and §5.3 differ, §5.3 is what was built. 2026-11-13 status check: merged to main (Merge t128-d35); built, board B1-B6 open (as row).
**Repo:** TRX64U-MultiCore — the generic core application (`coreapp/src`, `coreapp/cores/amiga.cc`), the unit tests in `tests/coreapp`. No gateware change
is planned (§3 D1); the board steps run on the A1200 core.
**Number:** T128 (registry: the table in `TODO.md`).
**Origin:** Alex, 2026-10-24, while fast seek for disk images was being built: "Folder als hdf wäre auch noch gut!" ("a folder as an HDF would be good
too!"). Decision of the same day (Alex: "Die App erzeugt ein Blockgerät. So wie IEC bei Gideon.", "The app creates a block device. Like IEC at Gideon's.",
translated): the core application turns the folder into a filesystem image on the fly, the Amiga sees an ordinary hard disk, no handler on the Amiga side.
**Depends on:** T104 (generic application; 5.16 fast seek), T104.1 D13/D16 (memory, 64 bit sizes), T97 §5.1 (the sector service, HDF_SECT), T102.
**Milestone:** M3.

---

## §1 The problem

**Who:** Alex with the A1200 and the A500 (Q3 decided: both cores) on the board. **What he wants:** put the files of a Workbench, a game or a WHDLoad tree in an
ordinary folder on the stick or SD card, point the Amiga at it, and have it work as a hard disk — and see what the Amiga saved in that folder again on
the PC, without `amitools`, without building a 1 GiB image first, without a second copy of the data.

**What happens today** (read from the code, not from comments):

- `coreapp/src/sectors.cc` serves HD0/HD1 from one `CoreFile` per unit: the gateware posts one request (unit, read/write, LBA) in the mailbox
  (`AM_MBOX_*`), `serve_one` moves 512 bytes between `file->read/write(lba * 512)` and the staging sector (`AM_W_STAGE`), then answers. A request beyond the
  file reads zeros; a write to a read only unit is dropped without any error (the mailbox has no error answer).
- The file is an `.hdf` (`hdf_actions` in `coreapp/cores/amiga.cc`: "Mount HD0/HD1" on an `.hdf`). Nothing can present a directory.
- The gateware **requires an RDB**. `cores/amiga/rtl/src/nanomig.v` (lines 664-765) reads sector 0 of a mounted unit, stops with "no drive" unless bytes 0-3
  are `RDSK`, and takes cylinders, sectors and heads from the low 16 bits of RDB longwords 16, 17 and 18 (`cylinders/sectors/heads[drv]`); it answers IDENTIFY in
  CHS only (no LBA, T97 5.1) and computes `ide_sdc_sector = (cylinder * heads + head) * sectors + sector`. A bare partition without RDB is not a disk to it.
- The size register: A1200 (`HDF_SECT=1`, `cores/amiga1200/core_sources.tcl:35`) takes sectors, the A500 core (`layer2_amiga.v:14`, `HDF_SECT=0`) takes bytes
  and `sec_mount` refuses a size of 4 GiB and more there (`sectors.cc`: `v > 0xFFFFFFFF`).
- The existing "folder" code goes the other way: `app/lib/cbm1541/d1541_fs.h` (`DiskReader`) shows a `.d64` as a folder through `CoreFs`. The IEC of Gideon's
  firmware (`apps/ultimate/software/io/iec/iec_channel.cc`, 1,959 lines) is a **file level** protocol: it opens the directory and the files through the
  `FileManager`, renders the directory as a BASIC listing and streams bytes per channel (`read_dir_entry`, `fm->fopen`). There is no block device in it and
  nothing of it is in our tree. What carries over from Alex's "like IEC" is the principle (the host's folder is the truth, the guest sees a rendering of it
  and writes go back to real files), not the mechanism: an Amiga needs blocks, so the rendering has to be a real filesystem image (§3 D1).

**Why it matters:** an `.hdf` has to be built, grows to its full size on the stick (a 1 GiB `Horst_a600.hdf` is 1 GiB), can only be filled with tools on a
PC, and is opaque there. A folder is the way these trees are distributed and edited.

**Parts (MECE):**

- **P1 Mount** — a folder becomes HD0/HD1: RDB and geometry the gateware accepts, size encoding, refusals with a reason.
- **P2 Read** — the Amiga reads a complete, valid volume: root, directories, headers, extension blocks, data, bitmap, names, dates, protection bits.
- **P3 Names and attributes** — the mapping between the host's names/attributes/dates and the Amiga's, both directions, with the cases where it cannot be 1:1.
- **P4 Write** — what the Amiga writes ends up in the files: data of existing files, new/grown/deleted/renamed files and directories.
- **P5 Consistency and loss** — eject, reset, hold (menu), power loss and a folder that changes behind the mount.
- **P6 Limits and memory** — entry count, volume size, file size, heap numbers; refusal instead of a crash.
- **P7 Speed** — latency per request, FAT work per sector, fast seek (T104 5.16) with many files, no stall of the poll while writes are folded back.
- **P8 End to end** — it works on the A1200 on the board, from a cold start of a folder to a saved file on the PC.

## §2 Scenarios — what proves it is solved

Benches are host benches in `tests/coreapp` (new file `test_folderhd.cc`, listed in `run.sh`; the medium is the `fakes.h` / `sparsefs.h` fake, the Amiga side is
an **independent** reader written in the bench — `tests/coreapp/amiga_model.h` is the starting point — that shares no code with the synthesiser). Control
= a deliberate defect the bench has to catch (`run.sh` `control` lines, as the other benches do).

```gherkin
Scenario: S1 the folder mounts as a disk the gateware accepts
  Given a folder with 3 directories and 40 files on the fake medium, HD0 empty, A1200 descriptor (HDF_SECT = 1)
  When  "Mount folder as HD0" runs
  Then  sector 0 read through the mailbox begins with "RDSK"; longwords 16/17/18 are cylinders / 63 / 16 with cylinders * 1008 >= the announced size in sectors
  And   the size register holds the sector count, the present bit is set, and the unit name shows the folder
  And   the CONTROL (RDB with the tag "RDSX") makes the bench's nanomig model go to "no drive" (the gateware's own check, copied into the bench from nanomig.v:742-745 by line)
```
```gherkin
Scenario: S2 the volume is a valid FFS volume (the independent reader walks all of it)
  Given the mount of S1
  When  the reader follows RDB -> PART -> boot block -> root -> every hash chain -> every header -> data and extension blocks -> the bitmap
  Then  every block's checksum is 0, every header's parent and hash chain agree, every file's byte count equals the host file's, every block is claimed by exactly one owner, and the bitmap says
        "used" for exactly the claimed blocks and "free" for the rest
  And   the CONTROL (one byte of a header flipped in the synthesiser by `control` sed, a bitmap bit inverted) is reported by name by the reader
```
```gherkin
Scenario: S3 content is the host's content, also beyond 4 GiB offsets inside the volume
  Given files of 0, 1, 511, 512, 513, 72*512, 72*512+1 bytes (the extension block edges) and a sparse file of 3 GiB + 16 bytes with markers at 1 GiB and 3 GiB
  When  the reader reads each file through the block path
  Then  bytes equal; the 3 GiB file's markers arrive at the right place (a 32 bit offset would swap them: the CONTROL truncates the offset to 32 bit and the marker check fails)
```
```gherkin
Scenario: S4 names, dates, protection
  Given host names: "Readme.TXT", "a:b", "x/y" (illegal on the host side; the bench uses what the fake allows), a 40 character name, "Foo" and "foo" in two directories and
        a pair "Longname...01" / "Longname...02" that collide after truncation to 30 bytes, "Über.txt" (Latin-1), "日本.txt" (not Latin-1), a read only file, files dated 1980-01-01, 2024-02-29 23:59:58, 2107-12-31
  Then  every Amiga name is at most 30 bytes, contains neither ':' nor '/', is unique within its directory after case folding, is stable across two mounts of the same folder,
        the reverse map finds the host file for every Amiga name; datestamps (days, minutes, ticks) equal the host time to the 2 s FAT resolution; the read only file has W and D set (denied),
        every other file R W E D allowed (0)
  And   the CONTROL (no collision handling) shows two entries of one name and the reader fails
```
```gherkin
Scenario: S5 a write into an existing file ends up in the file (as built: at the fold-back, in place - section 5.5 no. 2 says why not at once)
  Given S1's mount, writable
  When  the Amiga writes block k of file F (a data block, the header unchanged) through the mailbox
  Then  the host file F has exactly those 512 bytes at (k * 512), nothing else of the folder changed (full byte compare of every other file), and no overlay block was used
  And   the CONTROL on a read only mount: the same write changes no byte of F (and the log says it was refused, §3 D8)
```
```gherkin
Scenario: S6 create, grow, shrink, rename, delete, mkdir, rmdir: the folder follows
  Given the mount of S1 and a driver in the bench that does what FFS does (allocates from the bitmap, writes data blocks into the free area, then header, hash chain, bitmap)
  When  it creates "new.txt" (3 KB), appends 100 KB to "A", truncates "B" to 10 bytes, renames "C" to "D", deletes "E", makes "dir2" with a file, removes an empty directory; and the idle sync runs
  Then  the folder holds exactly: new.txt (3 KB, equal bytes), A (old bytes + 100 KB), B (10 bytes), D (C's bytes, C gone), no E, dir2/ with its file, the empty directory gone; no other host file changed;
        the scratch file of the free area (§3 D6) is gone or empty
  And   the CONTROL (the fold-back disabled) leaves the folder untouched and the bench fails
  And   a second mount of the changed folder passes S2 and shows the same content as the driver's own view before the fold-back
```
```gherkin
Scenario: S7 loss at every step leaves a folder that mounts and is consistent
  Given the writes of S6, the medium made to fail (`FakeFs` switch: full / gone / read only) after the n-th host write, for every n of the fold-back
  Then  the folder mounts afterwards, passes S2, and every file is either its old bytes or its new bytes (never a mix, never zero length unless it was made so); a full medium reports on the console and keeps the data in the overlay for the next try
  And   the CONTROL (replace a file by writing in place instead of temp + rename) produces a mixed file at some n and the bench fails
```
```gherkin
Scenario: S8 limits are refused with a reason, not met by a crash
  Given a folder with 16,385 entries, one with a 4 GiB file, one with a path component of 300 characters, one whose scan would need more than the index budget
  When  the folder is mounted
  Then  the first is refused with the count and the limit in the message (nothing mounted, previous unit untouched); the 4 GiB file is left out and named in the mount report (D4); the long name is cut by the D4 rule;
        heap taken by index + overlay + handles never exceeds the §3 D9 caps (the bench counts allocations through a counting allocator)
  And   the CONTROL (cap removed) exceeds the counter and the bench fails
```
```gherkin
Scenario: S9 fast seek and handle policy with many files
  Given 200 files of 1 MiB, each in 64 fragments, on the FatFs-backed fake of `tests/coreapp/fmfs64` (real fm_fs.cc) and the handle LRU of §3 D7 set to 8
  When  3,000 random sector reads over all files run
  Then  FAT sectors read per request stay at or below the measured T104 S1 level for a mapped file (the number from the T104 5.16 report line, to be copied here when run), at most 8 maps exist at any time (counter), and an evicted file's table is freed
  And   the CONTROL (every file kept open with its map, no LRU) exceeds the 8 and fails
```
```gherkin
Scenario: S10 the poll is never held longer than the budget by a fold-back
  Given a 40 MB new file waiting to be folded back, the fake medium with a 5 ms write per 32 KiB
  When  the idle sync runs while read requests keep arriving every 5 ms
  Then  no request waits longer than the slice (20 ms, Q9) plus its own service time; the fold-back completes over many slices
  And   the CONTROL (fold-back in one go) blows the bound
```
| # | Solves | Scenario | Test (bench / board step) | Where it runs |
|---|---|---|---|---|
| S1 | P1 | mount, RDB, size encoding | `tests/coreapp/test_folderhd.cc` | `tests/coreapp/run.sh` |
| S2 | P2 | the volume is valid FFS | same, independent reader | same |
| S3 | P2 | content, 3 GiB offsets | same + sparse fake | same |
| S4 | P3 | names, dates, protection | same | same |
| S5 | P4 | data write into an existing file (patched in place at the fold-back) | same | same |
| S6 | P4 | create/grow/delete/rename/mkdir fold back | same | same |
| S7 | P5 | loss at every step | same | same |
| S8 | P6 | limits, heap caps | same | same |
| S9 | P7 | fast seek with many files | `tests/coreapp/fmfs64/` (real `fm_fs.cc`) | its `run.sh` |
| S10 | P7 | no stall while folding back | `test_folderhd.cc` | same |
| B1 | P8 | A1200 boots a Workbench folder (RDB bootable) to the Workbench screen, Alex's folder of Horst's A600 tree | board session, photo + console log | by hand |
| B2 | P8 | a program started from the folder; a file saved by the Amiga (Notepad/`Copy`) is in the folder on the PC afterwards, byte for byte | board session, then the stick on the Mac | by hand |
| B3 | P8 | `Delete`/`Rename`/`MakeDir` in a Shell show up in the folder; `Info` shows the advertised size and free space | board | by hand |
| B4 | P8, P5 | reset and the menu (Freeze) during a pending fold-back lose nothing; power cut during a copy leaves a mountable folder | board (Alex cuts power himself, T94 rule: never power-cycle from here) | by hand |
| B5 | P7 | the numbers of S9/S10 measured on the board: console line per request > 20 ms (the sector service already prints them), time to boot Workbench folder vs the same tree as `.hdf` | board console | by hand |
| B6 | P1 | the A500 core with a Kickstart 2.05 or 3.x: same folder (Kickstart 1.3 has no driver for the Gayle IDE of `minimig-aga/gayle.v` and no FFS in ROM, so it sees no hard disk at all, `.hdf` or folder) | board | by hand |

Rules kept: every part has a scenario; every control above is a defect the bench can show; the board steps need real Kickstarts and Workbench files that are
not in the repository (Alex's own).

## §3 Decision (the proposal; §5.3 says what was built, decision by decision)

**D1 The block device is a `CoreFile` the folder implements; the rest of the path does not change.** `coreapp/src/folderhd.{h,cc}`: `class FolderDisk : public CoreFile`
(size / read / write / sync, `fsif.h`). `sec_mount` gets a sibling `sec_mount_folder(c, unit, dir)` that hands the unit this object instead of a `CoreFs::open` file;
`serve_one`, the mailbox, the staging sector, `sec_idle`, `sec_sync_all`, the gateware and the register map stay as they are. A new operation `sectors.mount_folder`
plus a folder action in the file browser (Q6); `sec_eject` closes the object. No gateware change: the RDB is read by `nanomig.v` as it is.

**D2 The device layout: RDB + one partition + FFS (DOS\3).** Geometry 16 heads, 63 sectors (the standard CHS limit, 1008 sectors per cylinder), cylinders = volume
rounded up, at most 65,535 (the gateware keeps 16 bits). Cylinder 0: RDSK block and PART block (DosEnvec: DOS\3, reserved 2, bootable, BootPri 0, the partition
starts at cylinder 1); no FSHD (the handler is the Kickstart's). FFS with international hashing = `DOS\3`; Kickstart 1.3 and 3.1 carry FFS in ROM (to be confirmed on the board, B1/B6; if a Kickstart in use has no FFS the RDB would need an FSHD, which is then a decision of its own).
**Why FFS (and not OFS, PFS, SFS):** (a) the data block is exactly the 512 bytes of the file — block k of file F is bytes [k*512, k*512+512) of the host file; a read is one
`read(off, 512)`, a write to an existing block is one `write(off, 512)`, no header, no checksum per data block. OFS data blocks carry a 24 byte header (488 bytes of data): file offset and block
offset do not line up, every data write must be split and every data block checksummed, and a partial-block host read costs an extra block. (b) The ROM has it; PFS and SFS need a handler that
is loaded from the RDB (FSHD blocks) — foreign code we would have to ship, and not in the Kickstart of either machine; the only reason for them would be size or speed that a
synthesised volume does not need. (c) Names: FFS (international) 30 bytes, which is what we map to (D4); long-name variants (DOS\6/7) need OS 3.2.x ROM and are out (§4). The volume is virtual, so FFS's weaknesses (validation time, fragmentation)
do not exist on the medium.

**D3 Layout is a pure function of the scanned tree.** Mount scans the folder once (depth first, entries sorted by Amiga name, case folded) into an index and assigns blocks in a fixed
order: boot block (2), then one run per object: directory header, or file header + its data blocks with an extension block after every 72 data pointers; the root block sits where the
handler expects it (the middle of the partition, taken from DosEnvec; the independent reader and the amitools check of §5 confirm the formula), bitmap blocks next to it. Every block of the volume is **computed on demand** from the index — header blocks (name, comment empty, date, protection, size,
hash table of 72 slots with chains), extension blocks, bitmap and bitmap-extension blocks (counting contiguous ranges, no per-block table), checksum — except where an overlay block exists (D5).
Index entry (proposal): parent 4, first block 4, size 4, name offset 4, packed FAT date 4, hash-chain next 4 = 24 bytes plus the name in a pool (Amiga name; the host name too where it differs).
Lookup block -> node: binary search over the first-block column. Overhead blocks of a 1 GiB file: 2,097,152 data blocks + 29,127 extension blocks (1.4 %) — virtual, they cost nothing on the stick.
**Free area:** the partition is larger than the tree by an advertised free area F (Q2) so the Amiga can write; F is advertised only as far as the medium can back it (D6).

**D4 Names, attributes, dates.** Host name -> Amiga: UTF-8 to Latin-1, characters outside Latin-1 and `:` `/` -> `_`; more than 30 bytes: cut and a `~NN` suffix in the last bytes; names equal after case folding
within one directory (and after the cut) get `~NN` too; assigned in the sorted order so the result is stable across mounts. Skipped (listed in the mount report on the console): hidden/system host files and names beginning with `.` (Q4), files of 4 GiB and more (FFS size is 32 bit), entries over the cap (D9).
Amiga -> host (write-back): Latin-1 to UTF-8, characters the host cannot hold (`" * : < > ? \ |`, and `/`) -> `_`; the Amiga's directory block keeps the name it was given until the next mount shows the sanitised one — said in the console line (Q4).
Protection: host read only -> W and D denied; E, R allowed; everything else allowed (0); the Amiga's own changes of protection bits and its file comments are **not** stored (candidate §4). Dates: FAT local time with 2 s resolution <-> days since 1978-01-01, minutes, ticks (1/50 s); a date the Amiga writes goes to the host file's modification time at fold-back.

**D5 Writes: three kinds of block, one rule.** (Proposal; item 1 was withdrawn after the first build, see 5.3 D5.)
 1. Data block of an existing file, header unchanged and offset inside the file: written **at once** to the host file (S5).
 2. Everything else the Amiga writes — headers, directory blocks, hash tables, bitmap and bitmap-extension blocks, boot block, **and data blocks it writes into the free area** — goes to an **overlay** and is read back from it from then on (the Amiga must see what it wrote, immediately).
    The overlay lives in the heap up to a cap (D9) for metadata; data blocks of the free area go to the scratch file (D6).
 3. **Fold-back** at the existing idle sync (`SEC_IDLE`, 1,000 ms after the last write), at eject, at hold (menu/reset, `engines_before_hold`) and at release of the reset: the engine walks the Amiga's **current** view (overlay over synthesis) from the root exactly as the
    independent reader does, compares it with the host tree and brings the host tree to it: a new header -> create the file (data collected from the blocks its pointers name: synthesised ones from the old file, free-area ones from the scratch file); a header whose
    size/pointers changed -> materialise the file's Amiga view into a temp file and replace (an append only copies the tail); a name/parent change -> rename; a header no longer reachable -> delete (Q1); a directory header new/gone -> mkdir/rmdir; date changed -> set the mtime.
    The overlay is not dropped by a fold-back (the Amiga's handler caches those blocks and must keep reading what it wrote); see D5a.
 **D5a (the problem this raises, proposed):** a re-layout after fold-back would move blocks under a running Amiga, whose handler caches them. So the layout is **frozen for the life of the mount**: fold-back does not rebuild the index into new block numbers; the block map stays (file F still lives at the same blocks; a new file lives where the Amiga allocated it, its blocks mapped to the new host file; a deleted file's blocks are free again) and only records what the host now holds. A new layout is made at the next mount. The index is therefore a block map plus a tree, and the memory cap of D9 has to hold both (measured in §5). The scratch file is truncated after the fold-back, its blocks then read from the host files they went to.
 Replace-by-rename is how the fold-back writes any file it rewrites (write temp, then remove + rename; FatFs has no atomic replace — the window is one `f_unlink` + `f_rename`, S7 measures the effect of a cut at each step).

**D6 Free area and scratch.** Free space the Amiga sees is real: at mount `min(F configured, medium free space - reserve)`, the free space of the medium read through a new `CoreFs::free_bytes(path)` (FatFs `f_getfree`; the interface has none today). Data blocks written into the free area land in a scratch file
on the same medium (name and place Q10), grown on demand (never preallocated: the number advertised is a promise the medium can keep, not a file that takes it away), and are copied to their files at fold-back; after it the scratch is truncated. It is removed at mount if a previous run left one (its content is stale by construction: the folder is the truth).

**D7 Speed: a small handle LRU, fast seek per handle, no cache of the volume.** Metadata costs no medium access (it is computed from the index). Data blocks: a table of 8 open files (`CoreFile`, opened on demand from the node's path, read/write by need), least recently used closed first; each handle owns its `FastSeek` (T104 5.16: the map is built at the
first non-continuing access of a file of 256 KiB and more, freed with the handle — at most 8 maps, 64 KiB each, 512 KiB worst case, typically far less); a sequential read-ahead of 8 sectors (4 KiB) per handle (sized by S9 / B5 — if the measurement shows no gain it is removed, not kept "for later"). Writes extend files only at fold-back (the map is dropped before any extending write, as `fastseek.h` requires).
Fold-back runs in slices of at most 20 ms between polls (S10; Q9), so the sector service is not held longer than the machine's timeouts allow (`sectors.cc` already logs requests slower than 20 ms).

**D8 Read only is an honest mode, not a silent one.** A folder on a read only medium, or a mount made read only (Q7), accepts the Amiga's writes into the overlay like any other (so the Amiga's own view stays consistent — the mailbox has no way to say "refused") and never folds back; the unit name reads "(read only, changes lost at eject)". This differs from an `.hdf` on a read only medium, where a write is dropped and the Amiga reads the old block (`sectors.cc`: `inside && h.writable`).

**D9 Memory (real numbers; none of it is free memory).** The core application's FreeRTOS heap is `0x7A0000` = 7,995,392 bytes (T104.1 D13: `host-app/target/u64ii/riscv/coreapp/Makefile`); the two LVGL frame buffers take 2 x 1,036,800 = 2,073,600 of it; what remains, **5,921,792 bytes, is the ceiling, not a free amount** — LVGL's objects, the library loader, the descriptors and the file manager are inside it, and the free heap with a menu open is **not measured** (T104.1 S7). Caps for this feature (proposal, Q5):
index 1 MiB (24 bytes + name per entry: 16,384 entries at about 45 bytes = 720 KB, with the block map and name pool measured in §5), overlay 512 KiB (1,024 blocks), handles 8 x (FIL, not measured) + 8 maps x at most 64 KiB, read-ahead 32 KiB, working buffers 8 KiB. Sum of the caps about 2.1 MiB of the 5.9 MiB ceiling. A scan that would exceed the index cap is refused (S8); an overlay that fills triggers an immediate fold-back, and if that cannot run (medium full) writes continue into the scratch and the console says so — the mailbox has no way to refuse.
**Size limits:** entries 16,384 (proposal); file size below 4 GiB; volume cap 2 GiB (Q2: keeps every byte offset a positive 32 bit value for old handlers; 4 GiB and the A500's byte register need the board); cylinders at most 65,535 (16 bit); path length as the medium's.
Bitmap for 2 GiB: 4,194,304 blocks / 4,064 per bitmap block = 1,033 bitmap blocks (25 in the root block, 8 bitmap-extension blocks) — all virtual.

**D10 Where the code lives.** `coreapp/src/folderhd.{h,cc}` (device, index, fold-back), `coreapp/src/amiga_ffs.{h,cc}` (pure block builders and parsers: RDB, PART, root, header, extension, bitmap, hash, checksum, date — no I/O, benched on its own), `sectors.{h,cc}` (mount_folder), `fsif.h` / `fm_fs.cc` (free_bytes, mkdir, rename — what `CoreFs` lacks), `coreapp/cores/amiga.cc` (the folder action, one line per unit), `tests/coreapp/test_folderhd.cc`. Image size of the application: it must stay under the slot `0x1E0000 - 12` (`tests/coreapp/test_appsize.py`); the added code is estimated at 25-35 KB and measured in §5.

## §4 Out — candidates, **to be decided with Alex** (none of these is decided here; each is "not built unless Alex says so")

- C1 PFS / SFS / OFS as the synthesised filesystem (FFS only). **Out, decided (Alex, 2026-10-26).**
- C2 Several partitions from sub-folders (one folder = one partition DH0 only). **Out, decided (Alex, 2026-10-26).**
- C3 Persisting Amiga-only attributes: **decided in (Alex, 2026-10-26): "do it" (translated).** Protection bits and file comments are kept in FS-UAE's `.uaem` side files (format read from FS-UAE's source before building), read at mount, written at the fold-back; the `.uaem` files themselves are hidden from the Amiga (Q4).
- C4 Hard and soft links (the Amiga can create them; the host has none): created links are not folded back, or refused. **Out, decided (Alex, 2026-10-26).** A link the Amiga creates is refused.
- C5 Files of 4 GiB and more (volume size: decided in, Q2).
- C6 Names beyond 30 bytes / Latin-1 round trip (long-name DOS\6/7, UTF-8 names). **Out, decided (Alex, 2026-10-26).**
- C7 A bootable-without-RDB-flag volume, boot block code for Kickstart 1.3 (only the DOS tag and the RDB bootable flag). **Out, decided (Alex, 2026-10-26).**
- C8 Seeing host-side changes (a PC writing into the folder) while the Amiga runs; the folder is snapshotted at mount, live rescan would need a re-layout (D5a). **Out, decided with Q8 (Alex, 2026-10-26).**
- C9 Mount or change of the folder while the machine runs (mount at reset only, like a fresh disk; the IDE state machine parses the RDB at mount). **Out, decided (Alex, 2026-10-26).**
- C10 A trash folder of ours for deletes: out, decided with Q1 (the Workbench Trashcan drawer covers it).
- C11 Journalling the fold-back across power loss (S7 proves "mountable, each file old or new"; a complete multi-file transaction is not promised). **Out, decided (Alex, 2026-10-26): (a), no journal.** Each file is written to a temp file and renamed, so a cut never leaves half a file; the last second of changes and a multi-step change (move plus rename) may be lost or half done, as on a real Amiga with FFS.
- C12 DirCache (DOS\4/5), more than one folder at a time (HD0 and HD1 are two independent units, the second folder is a second mount — in or out?). **More than one folder: decided in (Alex, 2026-10-26): "both could have a folder, yes" (translated).** HD0 and HD1 each take a folder of their own; the heap caps of D9 count per unit (Q5 measures for two). DirCache stays a candidate. DirCache: **out, decided (Alex, 2026-10-26).**
- C13 Other cores (Atari ST, T130) and floppies from a folder. **Out, decided (Alex, 2026-10-26).**
- C14 A "make a folder from an .hdf" and "make an .hdf from a folder" tool (the reverse direction). **Out, decided (Alex, 2026-10-26).**

## Open questions — each answerable on its own

- **Q1 Delete. Decided (Alex, 2026-10-26): a delete deletes.** "Delete - but the Workbench's own trashcan, the OS takes care of that, right?" (translated). An Amiga delete removes the host file or directory (D5); there is no trash folder of ours. The Workbench Trashcan is an ordinary drawer on the volume: moving a file there is a rename into that drawer, emptying it is a delete, both reach the folder like any other rename and delete.
- **Q2 Volume size. Decided (Alex, 2026-10-26): set in the core menu, bounded by the medium.** "According to the format of the medium ... And then settable in the menu of the cores, i.e. which folder and how many GB" (translated). The user sets the size in GB per unit; the menu offers only what the medium behind the folder can back (its free space, `CoreFs::free_bytes`) and what the core reaches (A500: below 4 GiB, `HDF_SECT=0`; both: the CHS geometry of D2). No fixed 2 GiB cap; C5's volume part is thereby decided in. Files of 4 GiB and more stay a separate question (the medium's format decides whether the host can hold one).
- **Q3 Which cores. Decided (Alex, 2026-10-26): both Amiga cores.** "Gladly the functionality in both Amiga cores" (translated). The A500 core serves the folder disk through its byte-sized HDF path (`HDF_SECT=0`, below 4 GiB, inside the 2 GiB volume cap). Why Horst's A600 `.hdf` did not start on the A500 core (T97, 2026-10-19) stays open on purpose; Alex: "Because it is made for an A600 and surely expects something else. Leave that open" (translated). It is not a T128 blocker.
- **Q4 Names the host cannot take / hidden files. Decided (Alex, 2026-10-26).** Hidden files: "hide" (translated): `.`-files (macOS `._*`, `.DS_Store`) and files with the hidden or system attribute are not shown to the Amiga. Names: "FS-UAE as the template then, so the folders become compatible too" (translated): a character the host cannot hold is written as `%XX` (hex of the Latin-1 byte, `%` itself as `%25`) and decoded back on reading, so the Amiga always sees its own name and a folder moves between FS-UAE and the TRX64 unchanged. FS-UAE's exact rule (which characters, case of the hex, its `.uaem` side files) is to be read from FS-UAE's source or documentation before building, not taken from memory; `.uaem` belongs to C3.
- **Q5 Heap budget.** The caps of D9 (index 1 MiB, overlay 512 KiB, 8 handles): acceptable against the 5,921,792-byte ceiling, or lower? (Needs the free-heap measurement of T104.1 S7 first for the real room.) *Taken as proposed by the coordinator (2026-10-26, Alex told, no objection): the caps of D9 per unit, fixed once the free heap is measured on the board; not an Alex decision.*
- **Q6 How the folder is chosen. Decided with Q2 (Alex, 2026-10-26): in the core menu.** Per unit (HD0, HD1) a setting picks the folder and one the size in GB; no folder action in the library browser.
- **Q7 Read only mount. Decided (Alex, 2026-10-26): no.** No read-only choice in the menu; a folder is read-only only when its medium is (D8).
- **Q8 Snapshot or re-scan. Decided (Alex, 2026-10-26): snapshot at mount, no re-scan task.** "I change nothing .. the stick is in the U64/C64U" (translated). The folder is read once at mount; a change made beside the Amiga (FTP, REST) shows at the next mount.
- **Q9 Fold-back timing.** Idle sync 1,000 ms (as `.hdf`) and slices of 20 ms: right, or longer idle / explicit "Sync" task? The risk of long idle is data in the overlay when the power goes. *Taken as proposed by the coordinator (2026-10-26, Alex told, no objection): 1,000 ms idle as `.hdf`, 20 ms slices; not an Alex decision.*
- **Q10 Scratch file place. Decided (Alex, 2026-10-26): a fixed place on the medium.** "ok do it that way" (translated). One scratch file per unit on the medium of its folder, `<root>/trx64/folderhd<unit>.scratch` (e.g. `/usb0/trx64/folderhd0.scratch`); the mounted folder stays clean, and a file left by a cut is overwritten at the next mount.

## §5 Decisions taken while building

**5.1 FS-UAE's name and side-file rule (Q4, C3), read from the source, not from memory.** Repository `FrodeSolheim/fs-uae`, branch `main`, commit
`f362278ccd4c60991caac3b4d240d4a3f751bea2` (fetched 2026-10-26 with `gh api`), file `od-fs/fsdb_host.cpp`; the numbers are its line numbers:
- Amiga name -> host name, `aname_to_nname` (201-293): a byte is written as `%` and two **lower case** hex digits (`hex_chars`, 199, used at 273-274)
  when it is one of `% \ * ? " / | < >` (`evilchars`, 197), when it is below 32 (250), when it is the last character and is `.` or a space (230-233), when it is
  the third character of a Windows device name `AUX CON PRN NUL LPT# COM#` (216-227: `repl_1 = 2`, also `AUX.anything`), or when it is the last character of a name that
  ends in `.uaem`, any case (263-270). Then Latin-1 -> UTF-8 (286). `:` is not in the list (an Amiga name has none).
- Host name -> Amiga name, `nname_to_aname` (316-349): UTF-8 -> Latin-1 (322; a character outside Latin-1 makes the conversion fail, 326-329), then every `%` that has at
  least two characters after it (`i < len - 2`, 336) becomes the byte of the next two characters; `char_to_hex` (295-307) gives 0 for a character that is no hex digit, so
  `100%real` decodes `%re` to byte 0x0E - taken over as it is, because the point is that a folder moves between FS-UAE and the TRX64 unchanged.
- Name comparison on the Amiga side is `strcasecmp` (`same_aname`, 309-314); the Amiga's international upper case is used here (FFS DOS\3).
- The side file is `<host name>.uaem` (415, 696). Reading, `fsdb_get_file_info` (394-587): an optional UTF-8 BOM (453-457); eight characters
  `h s p a r w e d` in this order, the letter present = the bit set (`FIBF_HIDDEN 0x80, SCRIPT 0x40, PURE 0x20, ARCHIVE 0x10, READ 8, WRITE 4, EXECUTE 2, DELETE 1`,
  `fsdb.h` 53-60; `-` = clear; 463-473); one separator (479); a date `YYYY-MM-DD HH:MM:SS.hh ` with at least `TIME_LEN` = 22 characters left (149, 481-528; `.hh` hundredths);
  the rest of the line up to CR/LF is the comment (530-543). With no flags the defaults are r w e d (548-559). Writing, `fsdb_set_file_info` (589-800):
  `astr` of the eight characters (748-760), `" %04d-%02d-%02d %02d:%02d:%02d.%02d "` (773-776), the comment, a newline (787-798).
  The FS-UAE bit for READ/WRITE/EXECUTE/DELETE set means *allowed*; the Amiga protection long has them inverted (a set bit 0-3 means denied; `filesys_parse_mask` 129-132 is `mask ^ 0xf`):
  `protect = (h?0x80)|(s?0x40)|(p?0x20)|(a?0x10)|(r?0:8)|(w?0:4)|(e?0:2)|(d?0:1)`.
- What we write: a `.uaem` only for an object whose protection is not the default (rwed all allowed, no h s p a) or that has a comment (C3: "protection bits and comments");
  FS-UAE's default `WF_ALWAYS` (59) writes one for every object - not taken over, the folder stays clean; reading handles both.
- The device-name prefix of the code above is Windows-specific; the medium is FAT/exFAT, which has the same device-name restrictions in its long names, so the rule is kept whole.

**5.2 Format check against amitools.** `tests/coreapp/folderhd_amitools.py <image>` opens a dumped image (`FOLDERHD_DUMP=<dir> tests/coreapp/...test_folderhd`) with
amitools 0.8 (`pip install amitools`): `rdbtool info` reads the RDB (geometry 16/63, DOS3, bootable, `rdb_blks=[0:1007]`), `ADFSVolume` opens root, bitmap and all directories,
and its `Validator` (boot block, root, directory tree, files, bitmap) reports 0 errors and 0 warnings on the S2 volume (40 files, 3 directories, 0.1 GB) and on the S4 volume (side files,
Latin-1 names, collisions). The root block is `num_blocks // 2` there (`BootBlock.calc_root_blk`), the same as the synthesis.

**5.3 As built, decision by decision** (what differs from the proposal is named; nothing else was left out).
- **D1** `FolderDisk` (`coreapp/src/folderhd.{h,cc}`, the fold-back in `folderhd_fold.cc`, the block builders in `amiga_ffs.{h,cc}`, the shared inside in `folderhd_impl.h`).
  `sectors.mount_folder(path, unit)` (string argument: the menu hands over `<folder>/<size in GB>`), `sec_mount_folder`, `sec_folder_offer` (state `sectors.folder.offer`);
  `serve_one`, the mailbox and the gateware are untouched. The unit shows `<folder name> (folder)`, or `<folder name> (read only, changes lost at eject)` (D8).
- **D2** Geometry 16 x 63, cylinder 0 = RDB (LBA 0) + PART (LBA 1) + zeros, the partition from cylinder 1, `DOS\3`, reserved 2, bootable, BootPri 0. The root block is
  `nblocks / 2` (amitools `BootBlock.calc_root_blk`), the bitmap blocks follow it (`R+1 ..`), then the bitmap extension blocks; the tree is laid out from block 2 on and
  skips that root cluster. The bits of the bitmap beyond the partition are 0. A500: the size register is bytes, the largest disk is 8,321 cylinders (4,294,434,816 bytes, below 4 GiB);
  A1200: sectors, 65,535 cylinders.
- **D3** Entry = 44 bytes (not 24): header block, size, host size, first child, three name offsets, parent, host parent, hash-chain next, date, protection, flags. The children
  of a directory are consecutive (breadth first), the hash chain of every entry is stored, the heads are found by a pass over the children. A file is laid out as header, data
  blocks, extension blocks (one run); spans ascend, so a block is found by binary search. Names are in a pool (Amiga name; the host name only where it is not FS-UAE's encoding of it).
- **D4** Names as 5.1 (FS-UAE) instead of `_`; more than 30 bytes: cut to 27 and `~01`, `~02` ... in the order of the host names; names equal after case folding likewise; a plain name keeps its name.
  Dates: FAT local time <-> days/minutes/ticks (own formula, checked for 1980-01-01, 2024-02-29 23:59:58, 2107-12-31).
- **D5 (changed after the first build, QC 2026-10-26)** **No data write ever goes into a host file at the time the Amiga writes it.** (1) Metadata (boot, root, bitmap, headers, directories,
  extension blocks - recognised by position in the root cluster, by the checksum and type of a header/extension block, wherever the block lies, also in the data range of a file) go to the heap overlay, up to
  `FH_OVERLAY_BLOCKS` = 1,024 blocks, then into the scratch file (S8: 1,400 files created, nothing lost); a block that held data in the scratch file and becomes a header moves to the overlay. (2) Every data block goes to the
  scratch file (runs of blocks -> slots, never preallocated). (3) The fold-back, see D5a: a file whose header and list of blocks are as they were but whose data blocks are in the scratch file
  is **patched in place** (only those blocks are written into the host file, the last one cut at the end of the file; `OP_PATCH`, phase PATCH); every other change is a temp file and a rename. The patch
  is applied only when the Amiga's bitmap flag was valid when the run began (a disk in the middle of a change does not say who owns a block; the data waits in the overlay). On a read only medium everything stays in the overlay (D8).
  Why and the proof: 5.5 no. 2. The code of the first proposal (a full block of an unchanged, linked file written straight) is kept behind `FH_STRAIGHT_WRITES` = 0 for the bench's control.
- **D5a** The layout is frozen: the objects the Amiga created keep their blocks, their headers, extension blocks, directory blocks and the bitmap stay in the overlay for the life of the mount,
  their data is read from the host file through a table of block runs (`xruns`). The fold-back is a job of phases, one host operation or one chunk (16 KiB) per step, at most 20 ms per slice
  (`FolderDisk::idle_slice`, called by `sec_idle` after 1,000 ms without a write): WALK (the Amiga's tree, object by object, compared with the index; the Amiga's bitmap flag must be valid, up to 10 s of
  waiting) -> MKDIR -> MATERIAL (data of new and rewritten files into `<root>/trx64/folderhd<unit>.fb<n>`; a write of the Amiga up to here drops the run, nothing on the host depends on it)
  -> REN1 (a renamed or moved object goes straight to its place when nothing is in the way, else to `<root>/trx64/mv<n>`) -> DELETE (children before their folder) -> REN2 -> PLACE -> PATCH (data blocks written in place, see D5) -> PROPS
  (`.uaem` files) -> FINISH (the index takes over what the host holds; the scratch file is truncated when nothing in it is needed). A step that fails keeps its change in the overlay; the next
  run tries again after 5 s. `sync()` (eject, hold, release of the reset, `sec_sync_all`) runs the job to its end.
- **Replace** has no atomic form in FatFs: the old file is moved to `folderhd<unit>.bk<n>`, the new one is renamed into place, the backup is removed (three renames). When the second rename fails the first is
  undone; when that fails too the file is on the repair list and put back first thing in the next run. A cut at that point (power) leaves the file in `<root>/trx64` as `.fb<n>` (new) and `.bk<n>` (old):
  the mount deletes `.fb*` and the scratch file (stale by construction) and **keeps `.bk*`** and says so in the mount report.
- **D6** The free area is `min(size set, free space of the medium - 16 MiB reserve)` at mount, rounded to whole cylinders; a medium that cannot tell, or a full one, gives a disk that is the tree only.
- **D7** 8 open files (least recently used closed first; each owns its link map), read-ahead of 4 KiB **only when a read continues the previous one** (a random request reads its own sector).
- **D8** As proposed: writes are accepted into the overlay (1,024 blocks, then dropped with a console line), nothing is folded back, the name says so.
- **D9** Caps as built (`folderhd.h`): index 1 MiB at mount (`FH_INDEX_BYTES`, nodes + pool), 16,384 entries, 4,096 objects created per mount, name pool +128 KiB, overlay 1,024 blocks (512 KiB), scratch runs 8,192,
  `xruns` 16,384, changes per run 4,096, working buffer 16 KiB, 8 x 4 KiB read-ahead. Sizes: `Node` 44 bytes, `Impl` 3,072 bytes, `FsEntryShort` 72 bytes (the layout `FsEntry` had; `FsEntry` itself holds a 255 character name since T180 #38 and is 272 bytes, the folder disk reads the short one through `CoreFs::list_short`).
- **D10** Added beyond the list: `folderhd_impl.h`; `fastseek.{h,cc}` count the link maps and print at most 40 lines (a folder disk opens many files); `fm_fs.cc` opens a file with one look-up of the path.

**5.4 Measurements** (host benches; the report lines are what the programs print).
- `test_folderhd` 654 checks: S1-S8, S10, S5c (first fit), S5d and the parts the design depends on; 40 negative controls (`control fh_*` in `tests/coreapp/run.sh`), each fails as it must.
  `S7 (next fit): 58 cuts over 28 host operations, 4 windows` and `S7 (first fit): 58 cuts over 28 host operations, 4 windows` - the medium fails after the n-th host write of the fold-back, in both modes (full / gone), n = 0 .. 28: the folder mounts, passes the reader, every file is its old or
  new bytes, and the retry brings it to the new tree. The four "windows" (two per mode) are the two rewritten files cut between the move-aside and the rename: the file is then missing from the folder (and in `trx64`).
- `S8: 16384 entries: index 901165 bytes, 904245 bytes held after the mount, peak 3116112 bytes during it` = 55 bytes per entry for names of 11 bytes. 16,385 entries are refused with
  "at least 16385 ... limit 16384"; 16,000 entries with names over 30 bytes (cut + host name stored) are refused for the index (1,048,576 bytes).
- `S10: 40 MB folded back in 366 slices, the longest wait of a request 18 ms (slice 20 ms)`.
- S9 on the real stack (`tests/coreapp/fmfs64/run.sh`, exFAT, 4 KiB clusters, FatFs with fast seek):
  `FmFsFolder: 3000 random sectors over 200 files of 1 MiB in 64 fragments, 8 handles: block device sectors per request 121.22 (data 1.00, directory and FAT 120.22), max 169; ... link maps: peak 8, now 8; files open: peak 8`
  `FmFsFolder: 3000 random sectors over 8 files (all open): block device sectors per request avg 1.00, max 1; files opened again: 0` (the level of T104 5.16: avg 1.00, max 1)
  `FmFsFolder: a sequential read of one file: 256 read-ahead fills, 1791 hits for 2048 requests, block device sectors per request 1.03`.
- amitools: 0 errors on the S2 and S4 images (5.2).
- Builder (16 cores, `trx64-sim-env:4`, 2026-10-26, tree of the commit that changed D5; the coreapp, fmfs64, render and build runs started with no CI container running): `tests/coreapp/run.sh`: `tests/coreapp: 53 test programs passed, 694 parallel controls (JOBS=8), every negative control failed as it must` (4 min 34 s);
  `tests/coreapp/fmfs64/run.sh`: product green, the 5 folder controls and the older ones red; `tests/coreapp/render/run.sh`: `coreapp render: 66 checks, 0 failed` (controls red); RISC-V `coreapp.app`
  (`tools/build_coreapp.sh` commands in `u64-build-env:ci`): **904,632 bytes of data against 856,504 at the parent commit 108d9115a: +48,128 bytes** (47.0 KiB; the estimate was 25-35 KB); the slot takes 1,966,068, 1,061,436 of it unused in this image
  (`tools/check_app_size.py`: ok). The sources compile for RISC-V without a warning of their own (`-std=gnu++11 -fno-exceptions -fno-rtti`).
  The other two suites that share `tests/coreapp` headers: `tests/loader/run.sh` (`1 test program passed, 27 parallel controls`) and `tests/hostapp/run.sh` green.

**5.5 What the spec did not foresee, and what is not built**
1. **S9's bound cannot hold for random access over many files.** Every request into a file that is not one of the 8 open ones opens a file, and the FileManager's look-up of a path reads the directory: 120 sectors for a folder of 200
   files (the directory is about 38 sectors; the Ultimate firmware reads it about three times). The bound of T104 5.16 (1 sector per request) holds for the 8 open files and for a sequential read (1.03); it does not for
   200 files in one folder at random (121). `FH_HANDLES` is one constant; the number to choose is B5.
2. **Straight writes were wrong and are gone (QC 2026-10-26).** FFS writes data blocks at once and metadata later. When it frees blocks (truncate, delete) and reuses them before the metadata is on the disk, a data write
   landed in a block that, on the disk, still belonged to an unchanged, linked file: the first build wrote it into that file's host file - corruption in normal operation, not only on a power cut (the bench's writer was
   next fit and did not show it; with first fit S7's "old or new bytes" failed for the truncated file). Now nothing reaches a host file when the Amiga writes. **Proof for any allocator:** (a) a host file is
   changed only by the fold-back; (b) the fold-back replaces a file (temp file, rename) from the Amiga's *view*, which is by definition what the Amiga's disk says; (c) it patches a file in place only when the walk, from the
   root over the directory blocks as they are, found that file's header and its list of blocks unchanged and the bitmap flag valid - on a consistent FFS disk those blocks are owned by that file alone, so every
   block of its data range in the scratch file was written for it; (d) with the flag invalid nothing is patched; (e) data written for another object into the blocks of a file the Amiga freed
   is, at the fold-back, found by the walk as that object's data (read from the view) and the freed file is deleted or rewritten from its own pointer list. Bench: `s5c_first_fit` (a first-fit writer
   truncates B and deletes E, writes two new files into the blocks it just freed *before* any metadata: not a byte of a host file changes until the fold-back, then the folder is exactly right), S6 and S7 with both
   allocators (`S7 (first fit): 58 cuts over 28 host operations, 4 windows`), `s5d` (patch withheld while the flag is invalid), and the controls `fh_straight` (the old code switched on: S5/S5c go red), `fh_nopatch`,
   `fh_consistent`, `fh_meta_in_data` (a header written into the data range of a file lands in the scratch runs, the fold-back takes it away: red).
   Cost: a data block the Amiga writes into an existing file is written twice (scratch, then host); a file with in-place writes is patched, not rewritten (S5: `patched 1, rewritten 0`).
3. **Links** (hard, soft) cannot be refused at the device (the mailbox has no error answer): the fold-back leaves them out and says so on the console (C4 as far as it can be built).
4. **Not built: host names of 55 bytes and more.** `FsEntryShort::name` is 56 bytes and `list_short` cuts at that length (`cut` marks it), so a cut name cannot be opened. Such an entry is left out and named in the mount report (S8's
   "300 character component" ends up there, not in the D4 cut). The listing of a 16,384-entry folder stays 72 bytes an entry: with the 272 byte `FsEntry` its buffer would be 4.7 MB (T180 #38).
   **Not built: setting the host modification time** at fold-back (D4): FatFs is built with `FF_USE_CHMOD 0` (`apps/ultimate/software/chan_fat/*/ffconf.h`) and the FileManager has no call for it. The date the Amiga
   writes is kept in the `.uaem` of an object that has one (protection or comment), otherwise a written file has the date the medium gives it.
5. The firmware's `printf` has no precision, no `%llu`, no `%%`: all of the feature's messages keep to its grammar (`boardprintf.h` is linked into `test_folderhd`).
6. The menu offers by a **suggestion and a refusal**, not by a list: a prompt for the size in GB starts with "1" (less where the medium or the core cannot back 1 GB); a larger size is refused with the most that can be backed.
   The folder browser's first row says "Use this folder" for a Mount task (`Task::pick_label`), "Create here" for the Create tasks.
7. A fold-back that could not write (medium full or gone) repeats every 5 s; the log lines of a failing medium are limited to 40. Objects the Amiga created beyond 4,096 per mount, or names beyond the 128 KiB pool, are not saved
   (console line); remounting clears both.
8. Files left in `<root>/trx64`: `mv<n>` (a renamed object cut between its two renames) is never deleted by the mount, `.bk<n>` is kept (5.3).

**5.6 Board steps** (they need the real Kickstarts and Alex's Workbench folder; nothing of this has run on the board).
- Before: the stick with the folder, e.g. `/usb0/amiga/horst` (Horst's A600 tree), at least 1.2 x its size free; the A1200 core with Kickstart 3.1, the A500 core with Kickstart 2.05/3.x; the firmware image built from this commit.
- **B1** F7 -> "Mount folder as HD0" -> pick the folder (Create here) -> size `1` -> RETURN; the status line says `horst: 1024 MB, n entries`; Reset Amiga. Expect the Workbench screen (RDB bootable). Photo + console log.
- **B2** Start a program from the folder; save a file with Notepad or `Copy ram:x to dh0:`; wait 2 s; look at the stick on the Mac: the file is in the folder, byte for byte (`cmp`). A name with `*`, `?` or `Ü` shows as `%2a`, `%3f`, `Ü` on the PC.
- **B3** In a Shell: `Delete`, `Rename`, `MakeDir`, `Protect x +s`, `FileNote`; the folder follows (a `.uaem` appears for the protection and the comment); `Info` shows the size set and the free space.
- **B4** Copy a few MB, press F5/reset (hold) in the middle: nothing is lost that was written one second before; then cut the power during a copy (Alex does it): the folder mounts again, no file is half written; `trx64` may hold `folderhd0.fb*`.
- **B5** The console lines `[sectors] unit n: sector ... took ... ms` (requests above 20 ms) during Workbench boot and a big copy; time to boot Workbench from the folder against the same tree as `.hdf`; the `fast seek` lines. Choose `FH_HANDLES` (5.5 no. 1).
- **B6** The same folder on the A500 core with Kickstart 2.05 or 3.x (1.3 has no driver for the Gayle IDE and no FFS in ROM: no hard disk, `.hdf` or folder).

**5.7 Discussion #35 (2026-11-04; GitHub issues #36 "mount not stored after reboot" and #37 "the four points"; release c83a83c2, A1200, folder as HD0).** Alex's board reading
the same day: the A600 tree (`DH0_A600`, 14,271 files plus 14,270 `._*` files) boots as HD0 on main-91f629ce; the folder is fine, the trouble is the mount/unmount sequence.
1. **Size above 1 GB: no defect found in what the app synthesises.** `tests/coreapp` mounts 1, 2, 3, 4 and 8 GB on the A1200 (reader of `folderhd_reader.h` and amitools): RDB cylinders x 16 x 63
   equals the size register and the 32 bit CHS product of `nanomig.v` 696-722 (`cyl=8323 heads=16 sec=63 size_reg=8389584` at 4 GB); the independent reader passes, amitools reports
   0 errors at 1, 4 and 8 GB. At 2 and 3 GB amitools reports one error each (`Invalid bitmap allocation (last) ... got=00003fff expect=0003ffff`): its validator masks the *low* `32 - valid` bits of the last
   bitmap word (`BitmapScan.py` 31-36, 85-86) and counts blocks past the end as free; with 4,194,286 blocks (2 GB) that mask covers 4 nonexistent blocks, with 6,290,926 (3 GB) likewise. Its own writer (`ADFSBitmap.create`, last
   long: only the valid bits) writes what the synthesis writes, so this is the validator, not the volume; `folderhd_amitools.py` now prints the validator's findings. The gateware
   announces IDENTIFY in CHS only, no LBA words (`nanomig.v` 1187-1244); nothing in the app is size dependent (no allocation proportional to the volume). Not provable on the bench: what the
   Kickstart's `scsi.device` and FFS do with a partition of that size (a 32 bit byte offset ends at 4 GiB; the A1200 offer reaches 65,535 cylinders = 33 GB, the A500 stops below 4 GiB).
   **Needs a decision** (Alex): cap the A1200 offer at 4 GiB like the A500 (what a 32 bit `TD_READ` offset can address), or find the failing size on the board first (B-step: 1.5, 2, 3, 4 GB with the
   console and Kickstart version of the tester). Two full quick formats (a >1.9 GB volume has more than 1,024 bitmap blocks) also exceed `FH_OVERLAY_BLOCKS`; they go to the scratch file, nothing is lost.
2. **The mount is not kept - it never was in this application.** Nothing writes HD0/HD1 (folder, size or `.hdf`) to the `.cfg`: `Settings` stores the descriptor's settings only
   (`settings.cc` `on_item` keeps no other key but `Pad ...`), `sec_mount_folder` / `sec_mount` / `win_mount` touch no `Settings` (read from the code); `git log --all -S` on `HD0=`, `Hard disk 0`,
   `autoload` finds nothing (`remount` only an unrelated Timewarp commit) since the generic application (a3820e255, 10-20). The mount survives a Reset (re-announced, `sec_reannounce`) and nothing else.
   Checked in the trees of 922deb780 and c83a83c20 (and a3820e255): the only things `cfgfile_item` writes are the descriptor's settings (`settings.cc` 133) and the recorded pad maps
   (`pad.cc` 394); `sectors.cc`, `engines.cc`, `boot.cc` are identical between c83a83c20 and main. A power cycle starts the app with no medium, so after it HD0 is empty. What "earlier releases"
   kept can only be the Gideon-base Amiga/A1200 application that ran until the generic application took over on 2026-10-20 (`T102` spec: "Amiga 1200 Settings"; its source is not in this
   tree), which stored its image choices in its own `.cfg`. A narrow fix (store folder + size of HD0/HD1 as `Mount ...` keys beside the `Pad ...` extras, mount again after the start) is about
   the size of the pad-map storage; T180 D2 ("Persist medium mounts", every core, all media) would replace it. The T164 D10c head-compare change touches the label match of enum settings only. So this is a feature that was not built here (an older application of the 1:1 line may have had it), not a
   regression. **Needs a decision** (Alex): remember per unit (folder + size, and likewise `.hdf` / `.adf`?) as text settings in the `.cfg` and mount them at start (C9 allows a mount at reset).
3. **Cause, fixed: every floppy mount or eject folded the folder back synchronously.** `win_eject` (called first by `win_mount` and by Eject DFn) ran `win_flush_all` and then `sec_sync_all`, and for a
   folder `sec_sync_all` is `FolderDisk::sync()`: the whole fold-back to its end, in one go, with the sector service stopped, so the Amiga's hard disk requests wait. Whatever the install has
   written since the last idle second (scratch file to the host: new files as temp files and renames) is paid at the next disk swap: nothing before the first disks, seconds after the third. The fold-back
   is the idle work (D5a); a floppy has no business with it. Fix: `win_eject` flushes the ejected drive only (`flush_drive`); the hold and the release of the reset still sync (`engines_before_hold`,
   `am_release`). Bench S11 (`test_folderhd`, 5 ms per 32 KiB written, 40 MB pending): `S11: four floppy mounts and an eject with 40 MB pending on the folder: 6400 ms, folder dirty 0, big.bin on the host 1`
   before, `... 0 ms, folder dirty 1, big.bin on the host 0` after, and the idle slices still fold it back (`big.bin` equal). Control `fh_swap_sync` (the old call put back): S11 red with 3 failed checks.
   Not the cause: handles (the swap opens one ADF, the folder holds 8 at most), the index (read once at mount), a re-scan. Still synchronous by design: Reset Amiga and eject of the folder.
   The `._*` files: hidden by Q4, but `CoreFs::list` reads them, so a mount over the A600 tree lists 28,541 entries; the per-open directory read of 5.5 no. 1 counts them as well (not measured on that tree).
4. **Cause found, not changed (a budget decision): writes are dropped without an answer when a cap is reached between two fold-backs.** The Amiga's bursts are folded back only after 1 s without a write
   and with a valid bitmap flag (5.3 D5a); until then every block is in the overlay (1,024 blocks) or in scratch runs (`FH_SRUNS_MAX` 8,192, one run per stretch of consecutive blocks; a file's header,
   extension and data blocks make about 1.7 runs per small file). Measured with the bench's FFS writer on a 1 GB folder, `folderhd.cc` `sr_put` / `write_block`: 3,000 files of 3,000 bytes -> `scratch_runs=4989 dropped=0`;
   6,000 files -> `the overlay and the scratch file cannot take block 22465 (medium full?); the write is lost`, `scratch_runs=8192 dropped=2834`, 76 `block ... is no header; its chain is cut` lines at the fold-back, 2,901 of
   6,000 files on the host. The mailbox has no error answer, so the Amiga sees an intact write and a broken tree later. Second wall: `FH_CREATED_MAX` 4,096 objects per mount (`folderhd_fold.cc` 421); the Workbench 3.2
   install (LOCALE = hundreds of catalogs) writes thousands of objects in a session, and the intermittent character fits a race between the idle second and the install's pauses. **Needs a decision** (Alex): raise
   `FH_SRUNS_MAX` (12 bytes per run: 32,768 = 384 KB) and `FH_CREATED_MAX` (up to 16,384, about +1 MB for nodes and names; the node index is 16 bit, 16,384 + 16,384 fits) inside the 5.9 MB ceiling, and/or start the fold-back
   before the caps are reached (while the bitmap flag is invalid it can only patch nothing and hold the data back, so it helps the runs, not the flag). Not reproduced on the board.
5. **"Creating" on screen: "Building the image".** The mount task has a folder browser and a prompt like the Create tasks, so after the size it runs through the same job display:
   `MenuCtl::start_job` sets the status "Building the image ... <- cancels" (`menuctl.cc` 1106) and `job_text` the pane row "Building the image NN%" (1115), for every task that asks. A folder
   mount builds no image (it reads the folder, makes `<root>/trx64/folderhd<unit>.scratch` only at the first write). Fixed: "Mounting the folder" for `sectors.mount_folder` in both places;
   bench `test_createflow` (`check_mount_folder`: row `Mounting the folder|0%`, status without "image"), controls `cf_mountword`, `cf_frame` adjusted.

---
<!--
Closing: Status -> DONE (date) + which scenarios are green where; the TODO row gets the check mark and a
history entry, in the same commit as the code.
-->
