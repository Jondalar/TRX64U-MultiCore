# T111 — A stick or SD card is formatted as exFAT on the board, from the host app, so an image above 4 GiB can be used

**Status:** BUILT 2026-10-26, benches green on the builder (S1-S8, the cancel, the checker, S3 route scan, the render bench); open: S9 (link, §5) is measured, the board steps B1-B4 are Alex's. Decided 2026-10-26 (Q1-Q7 answered or taken as proposed, §4 settled with Alex). 2026-11-13 status check: merged to main (Merge t111-spec); state as in row: built, board B1-B4 are Alex's.
**Repo:** TRX64U-MultiCore — host app only (`host-app/software/hostapp/`, `host-app/software/filesystem/` stays as it is); tests in `tests/hostapp/`. No core, no gateware, no change to the core app.
**Number:** T111 (registry: the table in `TODO.md`).
**Origin:** Alex, 2026-10-18, while trying T97 S5: "no .. does not work because SD card and USB are formatted wrong ... damn. Then we will build ourselves an app that can format exFAT, later .. take that as a todo. It can then run in the host" (translated).
**Depends on:** T101 (host app UI), T84 (host app); T104 §5.16/S6 and T97 §5.1 for the reading side (built, see §1).
**Milestone:** M3.

---

## §1 The problem

**Who:** Alex at the board, with a USB stick or an SD card in the C64U. **What he wants:** run the A1200 from `MegaAGS.hdf`
(9,008,283,648 bytes) on the medium. **Today:** both media are FAT32 (MBR type 0x0C); a FAT32 file ends at 4 GiB - 1, so the image does not fit
(T97 S5, history 2026-10-18). The only way to get exFAT is a PC.

Read from the code (2026-10-26), not from comments:

- **The reading side exists.** `host-app/software/chan_fat/full/ffconf.h` (the Ultimate application's, submodule) has `FF_FS_EXFAT 1`, `FF_USE_LFN 2`,
  `FF_LBA64 0`, `FF_MULTI_PARTITION 0`, `FF_MIN_SS 512`/`FF_MAX_SS 4096`, `FF_USE_LABEL 0`, `FF_USE_MKFS 1`; FatFs is R0.14 (`ff14/documents/updates.txt`).
  The core app takes the same file (`coreapp/ffconf/ffconf.h` includes it and only adds `FF_USE_FASTSEEK`); its 64-bit path is built
  (`File::seek64`/`get_size64`, T97 §5.1) and benched on a 128 MiB exFAT volume (T104 S6). `hostfs.cc` already saturates a size of 4 GiB and more
  (`size32()`). **Not proved:** no file of 4 GiB or more has been opened by the application on the board (T97 §5.1 "Not proved"). So formatting is not useless, but
  the end-to-end proof of T97 S5 is still a later step (see §4).
- **The writing side is compiled but unused.** `FF_USE_MKFS 1` and `FF_FS_EXFAT 1` mean `f_mkfs` with exFAT is in `ff.c` of both apps; nothing calls `f_mkfs`
  or `f_fdisk` (grep over `host-app`, `coreapp`, `apps/ultimate/software`). Whether the linker already keeps it is not known; the size difference is a measurement in §5.
- **How a medium is reached.** `UsbScsi` (`io/usb/usb_scsi.cc`) and `SdCard` (`io/sd_card/sd_card.cc`) are `BlockDevice`s with `read`/`write`/`ioctl`. A `FileDevice`
  (`filemanager/file_device.cc`) owns a `Disk`; `Disk::Init` reads the MBR (or GPT with 32-bit LBA only) and builds `Partition`s (offset, length, uint32 sectors);
  one partition is mounted directly, several appear as `Part0..`; `FileSystemFAT` registers the `Partition` with `ChanFATManager` and `f_mount`s it, `diskio.cc` forwards to it.
  A medium is re-read with `FileDevice::detach_disk()` + `attach_disk(block_size)` (the USB poll and `sdcard_manager.cc` do exactly this on removal and insertion).
  A volume without a partition table is accepted by `Disk::Init` only if the boot sector has "FAT" at offset 54 or 82: **an exFAT volume without MBR is not found, so a
  format must write an MBR.**
- **What `f_mkfs` does on this setup** (`ff14/source/ff.c`, `f_mkfs`): the work buffer is not taken from the heap (`ff_memalloc` only at `FF_USE_LFN == 3`; here 2), the caller
  passes it. With `FF_MULTI_PARTITION 0` the volume is the whole logical drive; without `FM_SFD` it writes its own MBR with the partition at **sector 63** (`N_SEC_TRACK`) —
  not aligned. Alignment comes from `disk_ioctl(GET_BLOCK_SIZE)`; `UsbScsi::ioctl` answers `RES_PARERR` and `SdCard::ioctl` answers 131,072 sectors, which `f_mkfs` rejects (> 0x8000), so both
  end as 1 sector: alignment must be given in `MKFS_PARM.align`. Cluster size auto: exFAT 4 KiB (< 256 MiB), 32 KiB (256 MiB .. 32 GiB), 128 KiB (>= 32 GiB). It writes no volume label.
  `f_mkfs` is one blocking call without a progress callback. LBA is 32 bit everywhere (`Partition`, `diskio`, `UsbScsi` READ(10)/WRITE(10)): a medium above 2 TiB is out of reach.
- **Where the host boots from.** Not from a stick: bitstream at flash 0x0, host app at 0x220000, `boot.cfg` at `Flash:/boot.cfg` (`bootcfg.h`). Formatting a medium cannot stop the host
  from starting. But the boot menu rows are paths on the media (`/USB1/cores/...`): after a format they are gone and show `ES_MISSING`; the default row waits for its medium
  (`BOOT_WAIT_MS`). That is the real cost of a mistake.
- **Budget (numbers, not "free").** `hostapp.app` is 691,704 bytes (T101 §5, 2026-10-23); its flash slot is 0x220000 .. 0x400000 = 1,966,080 bytes, so 1,274,376 bytes remain at the end of that one slot
  (nothing else lives in it; it is the only hole, and the slot is rewritten as a whole by every host-app release). The app runs from DDR2; the FreeRTOS heap size is **not documented in the repository**
  and has to be read from the board (§5). The work buffer is borrowed from that heap for the duration of the format.

**Parts (MECE):**

- **P1 Choose the medium:** which device, and what the user sees before deciding (size, current filesystem, label, partitions, which boot menu rows live on it).
- **P2 Cannot happen by accident:** the data destruction needs a deliberate, local act; no remote path; no timer or countdown can start a core or a second format meanwhile.
- **P3 What is written:** one MBR partition, exFAT, valid for FatFs and for a PC.
- **P4 It works afterwards:** the medium is re-read and mounted by the host; a file of 4 GiB and more can be created, sized and read at 64 bit.
- **P5 Failure and progress:** the user sees progress; any failure (unplug, write error, cancel) leaves a state that is recognisably not a valid volume, never a half-valid one.
- **P6 Cost:** flash slot, heap, build and CI; and the consequences for the boot menu and for the stick's contents.

## §2 Scenarios — what proves it is solved

Benches run in `tests/hostapp/run.sh` (suite `hostapp` of `ci/apptests.sh`, on the builder, never on the Mac; images are sparse files, 33 GiB is fine there). They use the REAL FatFs
(`ff.c` from the submodule, the way `test_fatexpand.cc` does) over a file-backed disk, and the new host-app code. `disk_ioctl` of the bench answers like the board (`GET_BLOCK_SIZE`: `RES_PARERR`).

```gherkin
Scenario: S1 the list tells what is on each medium, and gets exFAT/FAT32/blank/GPT right
  Given disk images: FAT32 with MBR, FAT32 without MBR, FAT16, exFAT with MBR, blank, GPT protective MBR, two partitions
  When  media_sniff() reads each through a read callback
  Then  it returns type (FAT12/16/32, exFAT, none, GPT, MULTI), label (11 chars for FAT, exFAT label entry), partition count, size in sectors
  And   a FAT32 image is never reported as exFAT (control: argv[1]=0 disables the "EXFAT   " check -> the exFAT row of the bench must go red)

Scenario: S2 nothing is written before the deliberate confirmation, on every other path
  Given the confirm state machine (pure code, driven by key codes and a clock) over a counting BlockDevice
  When  the user: opens the view and leaves; selects a medium and leaves; types the wrong name; types the right name minus one character; waits past the timeout; presses a key while the
        medium is pulled; the countdown of the boot menu expires while the view is open
  Then  the write counter is 0 and no core was started in every case; only the exact name of the medium (as the list shows it), RETURN, then the second RETURN, lead to the first write
  And   (control) the same bench built with the confirmation bypassed (-DFMT_NO_CONFIRM) must show writes > 0 and the bench must go red

Scenario: S3 no remote path to a format
  Given webui route table (webui.cc, the pure route function of webui.h)
  When  every route and every verb is enumerated
  Then  none reaches fmt_*; the Media view is not part of the REST/web state either
  And   (control) a route added for it in the bench's copy makes the check go red

Scenario: S4 the written volume is a valid exFAT in an MBR, on three sizes
  Given sparse images of 64 MiB (4 KiB clusters), 2 GiB (32 KiB), 33 GiB (128 KiB), each pre-filled with 0xA5 (old data) and the 2 GiB one also with a FAT32 volume
  When  fmt_exfat() runs through the shim device
  Then  sector 0 holds one MBR entry: type 0x07, start LBA 8192, size = sectors - 8192, 0x55AA; the other three entries are zero
  And   Disk::Init logic (the real disk.cc over the bench device) registers exactly one partition; f_mount reports FS_EXFAT, cluster size 4 / 32 / 128 KiB, free = clusters - 2 system clusters
  And   the independent checker tools/exfat_check.py (boot region checksum sector 11, backup at 12..23, FAT media/EOC entries, allocation bitmap and up-case table checksum, root entries) passes
  And   (control) one byte of sector 11 / of the up-case table flipped -> the checker must FAIL on that image

Scenario: S5 a file of 4 GiB and more
  Given the 33 GiB exFAT image of S4 and, as control, a FAT32 volume of the same size made by FatFs
  When  a file is created, f_lseek to 9,008,283,648 - 4096, 4096 bytes written (marker), closed, reopened, f_size and the marker read back; then the host's hostfs_stat/list
  Then  f_size = 9,008,283,648 (64 bit), the marker is intact, hostfs reports 0xFFFFFFFF (saturated, not a wrapped 418,349,056)
  And   (control) the same on the FAT32 volume ends with an error or a file of at most 4 GiB - 1: the check "size == 9,008,283,648" must go red there

Scenario: S6 a failure at any write leaves no half-valid volume
  Given the 2 GiB image with a valid FAT32 + MBR, and the shim that fails the Nth write
  When  N runs over every write of the format (sweep), plus a failed read of the capacity, plus a state change to "no media"
  Then  for N = 0 (nothing written) the old volume still mounts unchanged; for every N >= 1 the medium has NO mountable volume (Disk::Init + f_mount fail), fmt_exfat returns the failing step as text, and the final state is never "old MBR + new boot sector"
  And   (control) the order MBR first, mkfs after (-DFMT_MBR_FIRST) leaves a mountable-looking partition table over a stale volume for some N: the sweep must go red

Scenario: S7 progress is real
  Given the 33 GiB image
  When  fmt_exfat() runs with a progress callback
  Then  the callback is called at least every 1 MiB of written data, the percentage is monotone, ends at 100 only after the MBR and the remount check, and the total of the estimate is within 5 % of the bytes the shim counted
  And   (control) a shim that swallows writes without counting makes the estimate check go red

Scenario: S8 limits are refused before any write
  Given media of: 0 sectors, 1 MiB, write-protected, state "not ready", 4,294,967,296 sectors (2 TiB), 4096-byte sectors
  When  fmt_exfat() is asked
  Then  it refuses with a named reason and the write counter is 0 (a 4096-byte sector medium only if §4 allows it)
```

Board steps (by hand, with Alex; a photo or the log line each; why not a bench: real USB/SD timing, a real PC reading the result):

| # | Solves | Step | Where |
|---|---|---|---|
| B1 | P1, P2 | The Media view lists the USB stick and the SD card with the right size and "FAT32"; the confirm screen names the boot menu rows that live on the medium; Escape leaves it with the medium unchanged (read back on the PC afterwards) | board session |
| B2 | P3, P4 | Format the stick on the board; the host's browser lists it empty; the stick on the Mac: `diskutil info` says "ExFAT", scheme "Master Boot Record"; `fsck_exfat` clean; Finder mounts it | board session |
| B3 | P4 | Copy `MegaAGS.hdf` (9,008,283,648 bytes) to the stick on the Mac; on the board the core app (T97) opens it and reports 9,008,283,648; the SD card the same | board session, then T97 S5 |
| B4 | P5 | Pull the stick during the format; the host shows the failure, the stick is blank (Mac: unreadable / unformatted), the host mounts the next insertion normally after a re-format | board session |
| S9 | P6 | `tools/build_hostapp.sh` link: the delta of `hostapp.app` against 691,704 bytes is reported; budget below | container (`trxapps` stage) |

S9 budget (proposed, to be confirmed against the first link): delta <= 32 KiB, so that at least 1,200,000 bytes remain at the end of the 1,966,080-byte slot. The heap at the format
screen is printed (`xPortGetFreeHeapSize`); the work buffer is min(64 KiB, a quarter of it) and the format is refused with a named reason if less than 4 KiB would remain.

| # | Solves | Scenario | Test |
|---|---|---|---|
| S1 | P1 | list tells the truth | `tests/hostapp/test_mediasniff.cc` |
| S2 | P2 | no write before the confirmation | `tests/hostapp/test_fmtconfirm.cc` |
| S3 | P2 | no remote path | `tests/hostapp/test_webui.cc` (extended) |
| S4 | P3 | valid exFAT in an MBR | `tests/hostapp/test_exfatfmt.cc` + `tools/exfat_check.py` |
| S5 | P4 | file above 4 GiB | `tests/hostapp/test_exfatfmt.cc` |
| S6 | P5 | failure sweep | `tests/hostapp/test_exfatfmt.cc` |
| S7 | P5 | progress | `tests/hostapp/test_exfatfmt.cc` |
| S8 | P2 | limits | `tests/hostapp/test_exfatfmt.cc` |
| B1-B4, S9 | P1-P6 | on the board / link | by hand; `tools/build_hostapp.sh` |

## §3 Decision

(Proposed. Every choice marked "Q" is open with Alex, §4/Q list at the end.)

- **D1 FatFs `f_mkfs`, no own exFAT writer.** It is already compiled (`FF_USE_MKFS 1`, `FF_FS_EXFAT 1`), written and used by the same library that will read the volume. The independent check is `tools/exfat_check.py` (S4), not a second implementation.
- **D2 Own MBR, `f_mkfs` with `FM_EXFAT | FM_SFD`.** A pure module `hostapp/mbr.cc/.h` writes the MBR: one entry, type 0x07, start LBA 8192 (4 MiB), length to the end, CHS 0xFE 0xFF 0xFF, `0x55AA`.
  `f_mkfs`'s own MBR (sector 63) is not used (unaligned; not under our control). Reason for the MBR at all: `Disk::Init` does not find an exFAT volume without one (§1). `MKFS_PARM`: `fmt = FM_EXFAT|FM_SFD`, `n_fat = 1`, `align = 8192` sectors, `au_size = 0` (auto). Q4, Q5.
- **D3 Format target = a `Partition(dev, 8192, sectors - 8192, 0x07)` of our own**, registered at `ChanFATManager` for a drive number, `f_mkfs("<n>:", &parm, work, len)`, then removed. `diskio.cc` is not changed.
- **D4 Order (P5):** (1) checks (D8); (2) detach the disk (`FileDevice::detach_disk`; it deletes the `Disk` and its partitions but NOT `info.fs`, the `FileSystemFAT` that holds the `f_mount`ed drive, which `probe()` later overwrites: the format must unmount that drive itself, `f_unmount` + `ChanFATManager::removeDrive`, before `f_mkfs`; the removal path of the USB poll has the same hole and is not changed here); (3) write zeros to sector 0 (the old MBR or super-floppy boot sector is dead from here on); (4) `f_mkfs`; (5) write the MBR; (6) `CTRL_SYNC`;
  (7) `attach_disk` and probe: `f_mount` must succeed with `FS_EXFAT` and `f_getfree` must be sane, else the result is "failed". Any state before (5) has no MBR and no valid sector 0, so `Disk::Init` and `f_mount` cannot accept a half-written medium (S6).
- **D5 One blocking worker with a device shim.** `FmtDevice : BlockDevice` wraps the real device, counts written sectors, calls the progress callback every 256 KiB (the same pattern as the ICAP loop's per-256 KiB report, T84), polls the keys there, and in the bench fails the Nth write.
  The UI is redrawn from the callback (as T84/T115 do for loading). The progress estimate is computed from the geometry (FAT = clusters x 4 bytes, bitmap = clusters / 8, up-case table, boot region 24 sectors).
- **D6 UI.** A new view V_MEDIA in `hostui.cc` (view code in a new `hostapp/mediaui.cc`; the pure logic in `hostapp/fmt.cc`, `fmtconfirm.cc`, `mediasniff.cc`, which the benches link without LVGL). Where it is reached: Q1.
  The view lists every medium the `FileManager` knows (SD, USB1, USB2, each LUN of a reader): name, vendor/size in GiB (decimal "14.4 GiB" like the browser, from `GET_SECTOR_COUNT`), current type from `media_sniff`, label, partition count, and
  "N boot menu rows live here". No row is preselected for a format.
- **D7 Confirmation (P2), two screens, nothing written before the second RETURN:**
  1. RETURN on a row opens a red page: medium name, size, type, label, partitions that will be lost, the boot rows that will be lost, "ALL DATA ON <name> WILL BE ERASED".
  2. The user types the medium's name as shown (`USB1`, `SD`) on the C64U keyboard or a USB keyboard; wrong or incomplete input is not accepted; RETURN with the exact name arms it; a second RETURN within 10 s starts. Any other key, a 60 s idle timeout or a removed medium cancels. (Q2: alternatives.)
  While the view is open the boot countdown is stopped (the same flag a key press sets, `cd_dead`) and no core start key works; a started format blocks every start until it ends. The typed name is compared against the name shown, not against a stored default.
- **D8 Refused before any write (S8):** no media / not ready / write-protected (`STA_PROTECT`), fewer than 2^13 sectors or smaller than 64 MiB, more than 2^32 - 8192 sectors (32-bit LBA, `FF_LBA64 0`), sector size other than 512 (bench-proved only for 512; Q: 4096 candidate in §4), less heap than D5's minimum.
- **D9 No REST/web/RPC path.** `webui.cc` gets no route (S3): the user at the device decides (Alex's rule, memory "User schlägt API").
- **D10 After the format:** the host re-probes the medium (D4 step 7); it shows type, label, free bytes and "0 boot rows found"; the boot menu rows are NOT removed (they turn `ES_MISSING`, as for any missing medium) so that copying the cores back repairs them. The host does not create folders (§4).
- **D11 Code stays in the host app.** The core app has no format function; its `ff.c` still contains `f_mkfs` (same ffconf) but nothing calls it. The flash slot cost is measured in S9; the heap is borrowed only while the format runs.
- **D12 Volume label:** `f_mkfs` writes none and `f_setlabel` needs `FF_USE_LABEL 1` (an overlay in `host-app/software/hostapp/ffconf.h`, which already overlays `FF_USE_EXPAND`; no struct changes). Q3.

## §4 Out

Candidates, **to be decided with Alex** (nothing here is decided; whatever stays out is written down before the work starts):

- O1 GPT and media above 2 TiB (needs `FF_LBA64 1`, which changes the `disk_*` signatures and every consumer in both apps, plus 64-bit LBA in `Disk`, `Partition`, `UsbScsi`). **Out, decided (Alex, 2026-10-26).**
- O2 Formatting as FAT32, FAT16 or any other type (the app would be "exFAT only"). **Out, decided (Alex, 2026-10-26: "no FAT32", translated).**
- O3 Media with 4096-byte sectors (`FF_MAX_SS 4096` allows it; no bench medium, no board medium). **Out, decided (Alex, 2026-10-26).**
- O4 Secure wipe / zero-filling the whole medium: the format is quick; the old file contents stay physically on the medium. **Out, decided (Alex, 2026-10-26).**
- O5 Formatting one of several existing partitions, or keeping the existing partition table; every format replaces the table (one partition). **Out, decided (Alex, 2026-10-26).**
- O6 Creating a folder structure after the format (`/cores`, T112) and restoring a backup of the old contents. **Out, decided (Alex, 2026-10-26).**
- O7 Check/repair of a volume (fsck), or showing a filesystem error list. **Out, decided (Alex, 2026-10-26).**
- O8 Formatting the internal `Flash:` drive (FAT12) — never from this view. **Out, decided (Alex, 2026-10-26).**
- O9 Cancelling a format in progress (Q6); a cancel would leave a blank medium, which D4 makes safe. **In, decided with Q6 (Alex, 2026-10-26).**
- O10 Format from the core app, the web UI or REST (D9 excludes it for the host app too). **Out, decided (Alex, 2026-10-26).**
- O11 Proving that the Amiga sees the whole 8.4 GiB through its CHS-only IDE (`nanomig.v` reports CHS only, T97 §5.1 "Not proved"): that is T97 P5, not T111. T111 is done when the medium is exFAT, a file above 4 GiB is created/read by FatFs and the core app lists it with its 64-bit size (B3). **Out, decided (Alex, 2026-10-26).**
- O12 A media speed test, SMART or a bad-block scan. **Out, decided (Alex, 2026-10-26).**

**Open questions for Alex (each answerable alone):**

- **Q1 Where is the Media view reached? Decided (Alex, 2026-10-26): (b), in the browser, which becomes the "File Browser".** "b, and then 'File Browser', and there with 'A' you have the add to boot menu for the right file type" (translated). F3 "Add core" is renamed "File Browser": a general browser over the media; `A` adds the selected file to the boot menu when its type is one the boot menu takes (`.cor`, `.cfw`, large `.cor`), and is not offered on other files. On the media level (SD, USB1, USB2) the browser shows size, filesystem and label and offers the format action of this spec. The rename and the `A` key change the host app's menu (render benches and goldens of the browser change with it).
- **Q2 What does the user do to arm it? Decided (Alex, 2026-10-26): (a), as proposed (D7):** type the medium's name, RETURN, then RETURN again within 10 s; any other key or the timeout cancels.
- **Q3 Volume label. Decided (Alex, 2026-10-26): (a), none** (as `f_mkfs` writes it; `FF_USE_LABEL` stays 0).
- **Q4 Partition start and alignment:** 4 MiB (proposed, `align 8192`) or 16 MiB (a common SDXC layout, from memory and not checked here; costs 16 MiB of the medium). The benefit cannot be measured on the board (no bench for flash-block alignment). *Taken as proposed by the coordinator (2026-10-26, Alex told, no objection); not an Alex decision.* 4 MiB.
- **Q5 Cluster size:** FatFs auto (4 KiB below 256 MiB, 32 KiB to 32 GiB, 128 KiB from 32 GiB), or a fixed 32 KiB for everything above 256 MiB (T104's fast seek stores one cluster map per file; a larger cluster means fewer links, a smaller one less slack for small files). *Taken as proposed by the coordinator (2026-10-26, Alex told, no objection); not an Alex decision.* FatFs auto.
- **Q6 May the user cancel while writing? Decided (Alex, 2026-10-26): yes.** "cancel is ok" (translated). A cancel key stops the write and leaves a blank medium (D4); the view says so and offers the format again.
- **Q7 Is the independent checker `tools/exfat_check.py` (S4) acceptable as part of the tree,** or is `fsck.exfat`/`fsck_exfat` in the builder image the judge (not checked: the builder image is not read here)? *Taken as proposed by the coordinator (2026-10-26, Alex told, no objection); not an Alex decision.* `tools/exfat_check.py` in the tree, plus `fsck.exfat` as a second judge where the builder image has it.

---

## §5 Decisions taken while building

Built 2026-10-26 on branch `t111-spec`, tested on the builder in the CI image (`u64-build-env:ci`, `nice -n 15`, `timeout`, only while no `GITEA-ACTIONS-*` container ran).

**What is where.** Pure code in `host-app/software/hostapp/`: `mbr.cc` (D2), `mediasniff.cc` (S1), `fmtconfirm.cc` (D7), `fmt.cc` (D1, D3-D5, D8: checks, shim, order, progress, cancel), `mediaui.cc` (the words, sizes, percentages). Board glue: `mediaglue.cc` (find the medium, `media_info`, `media_format`: the ops of D4). The LVGL pages and the keys are in `hostui.cc`, **not** in a new `mediaui.cc` as D6 said: the widget helpers are `static` there; `mediaui.cc` holds only the text. Vendored `filemanager/file_device.{h,cc}` are patched (`host-app/patches/filemanager_file_device.*.patch`, `PROVENANCE.md`): a list of all `FileDevice`s with `find(name)`, `get_block_device()` and `release_volume()` (the unmount D4 step 2 needs). `detach_disk()` and the USB poll's removal path are **not** changed (D4). `tools/exfat_check.py` is the independent judge (Q7).

**UI (Q1, Q2, Q6).** F3 is "File Browser" and lists every file (200 rows, was 96; sizes in 64 bit, `HostDirEntry.size64`). **A** adds the selected file when it is a `.cor`, `.cfw` or large `.cor` and does nothing on any other row; **RETURN on a file does nothing now** (it added before). **F** on the media level (the root: SD, USB, Flash) starts the act; the key letter is mine, the decision named none. The right panel on the media level shows size, contents with label, "N boot menu rows live here", access and what the format would do, or why it is refused. The footer follows the selection (media level / boot-menu file / other file). `Flash` is never formattable (O8; by its name).
The act: refusals first (a note, no page), the red page with the typed name (case does not matter: the C64 keyboard gives either), RETURN arms, a second RETURN 1 s to 10 s later starts, any other key or a medium taken away cancels, 60 s without a key cancels. A RETURN less than 150 ms after the previous one is a key repeat and does not count (a held key must not be a second confirmation; the first repeat comes 250-660 ms after the press, hence also 1 s after arming). The format runs in the UI task like a core start: no countdown, no key, no network command starts anything meanwhile (a network command that comes is answered "busy" and dropped, not kept for later). The progress page redraws every 100 ms at most; ESC or RUN/STOP cancels until the table is written. After it: a result page (free space, "0 found" of the boot rows that point there, or why it failed / that the medium is blank and F formats it again), the browser's rows and the boot menu's badges are refreshed. The boot menu rows are not removed (D10).

**The format.** As D4, with: heap is `configTOTAL_HEAP_SIZE = 0x7A0000` (8,001,536 bytes, `hostapp/Makefile`; the free heap at the page is still to be read on the board, `xPortGetFreeHeapSize()` goes to the log line of the format); work buffer 64 KiB, a quarter of the free heap below 256 KiB, refused below 16 KiB free (4 KiB buffer). Largest accepted medium: 2^32 - 8192 sectors (a device that can only say 0xFFFFFFFF is refused as "larger than 2 TiB"). The disk signature in the table (offset 440) is `get_fattime()`. The progress estimate is exact (it runs the same up-case loop and cluster arithmetic as `f_mkfs`): 86,016 bytes on 64 MiB, 321,024 on 2 GiB, 1,264,128 on 33 GiB, in 30 / 33 / 47 write calls; the longest stretch between two reports 300,544 bytes.
D4 step 7 on the board: `FileDevice::probe()` must find one volume, `get_free` must give the cluster size and the number of free clusters (clusters minus bitmap, up-case table and root directory) that were computed, and the sector 0 and boot sector read back must show type exFAT, one partition at 8192, the same length. The same read-back (`fmt_verify_layout`) runs in the bench; the mount half runs in the bench through the real `FileDevice`.
**S6 reading.** The first write is the zeroing of sector 0; if *that* fails nothing has changed and the old volume mounts again (the host re-attaches it): the spec said "no mountable volume for every N >= 1", this is N >= 2. A medium that accepts the writes and loses them is reported as failed (the read-back sees it).

**Checker.** `exfat_check.py` accepts `PartitionOffset` 0 in the boot sector (the specification says hosts ignore 0; `f_mkfs` writes the offset inside its own partition, which is 0); a non-zero value must equal the table's start.

**Tests (S1-S8, run by `tests/hostapp/run.sh` -> `run_fmt.sh`, builder, `u64-build-env:ci`).** Real: `ff.c` with the board's `ffconf.h` (asserted: only the RTOS mutex off and `f_expand` on, as `hostapp/ffconf.h`), FileManager, FileDevice, Disk::Init, FileSystemFAT, `hostfs.cc`, `mediaglue.cc`, `fmt.cc`; fake: the block device (sparse, 4 KiB pages, `GET_BLOCK_SIZE` answers `RES_PARERR` as `UsbScsi` does, old data 0xA5).
| Scenario | Test | Checks | Controls that go red |
|---|---|---|---|
| S1 | `test_mediasniff.cc` | 25 | 7 (no exFAT test, FAT12/16 by wrong count, no label, no GPT, one partition only, MBR taken as volume) |
| S2 | `test_fmtconfirm.cc` (pure machine over the real format and a counting device) + render bench (UI level, countdown, network) | 34 + the UI checks | 10 + `-DFMT_NO_CONFIRM` (29 red) + 7 in the render bench |
| S3 | `test_webui.cc` (route literals of `webui.cc`, 12 invented URIs x 6 bodies, the command enum, the header) | 13 | 1 (a `/v1/format` route added to a copy: 13 red) |
| S4, S5, S6, S7, S8, cancel | `test_exfatfmt.cc` | 163 | 20 mutants of `fmt.cc`, `mbr.cc`, `mediaglue.cc` |
| S4 judge | `tools/exfat_check.py` on 64 MiB, 2 GiB, 33 GiB | 3 images OK | 11 (one byte changed in the MBR, boot region, checksum, backup, FAT, bitmap, up-case table, root directory) |
S5: on the 33 GiB exFAT volume a file of 9,008,283,648 bytes is created, the 4096-byte marker at its end read back; `hostfs_stat` and `hostfs_list` say 0xFFFFFFFF (saturated) and `size64` 9,008,283,648 (`8.3 GiB` in the browser, cut not rounded). The control is a FAT32 volume of the same size made by FatFs: its file ends at 4,294,967,295 bytes, 0 marker bytes written; the bench asserts that the size check is red there. S6: 33 write calls in a 2 GiB format; failing each in turn: 1 leaves the old volume, 32 leave a blank medium, 0 wrong; the medium gone from write N; capacity unreadable; three formats in a row do not pile up drive slots (the control without `release_volume()` crashes the bench on the stale partition: the leak is real). `fsck.exfat` is not in the CI image; it was run separately, see the report line below.
Render bench (`tests/hostapp/render`, 232 checks, 9 new controls): fakes `media_info` / `media_format`, drives the real `hostui.cc`; its clock moves only while the UI waits and by a set gap per key, so the 1 s / 10 s windows are tested.
**Benches changed with the rename and `A`:** `render.cc` (the six places where RETURN added a file now press `a`; the browser lists every file; the hint is "A adds it ..."), `mktree.py` (a 9 GB sparse file and a text file in `Usb0/images`), `stub/task.h` (a clock), `tests/hostapp/neutral.py` (the new files), `ci/apptests.sh` (the suite key reads the vendored trees, the patches and `exfat_check.py`), `tools/release_notes_mc.py` (the F1 and F3 rows of the first-start table, which still said "A adds a found core, R rescans"). No golden image exists; what a bench asserts as text is what changed.

**S9 (link), measured 2026-10-26 on the builder, same container, same commands as `ci/trxapps.sh`:** `hostapp.app` of this branch's parent (HEAD e975e1987, the tree as it is today) 694,644 bytes (the 691,704 of T101 §5 is three days old); with T111 **714,564 bytes, +19,920**, inside the proposed 32 KiB. The slot 0x220000..0x400000 (1,966,080 bytes) has 1,251,516 bytes left at its end (`tools/check_app_size.py`), above the 1,200,000 of the budget. `riscv32-unknown-elf-size`: text 556,664, data 157,884, bss 8,353,856. Everything compiles with the board's flags (`-std=gnu++11 -fno-rtti -fno-exceptions`) without a new warning. The code is host-app only; the core app is not touched.
**Second judge.** `fsck.exfat` (exfatprogs 1.2.9, Debian container on the builder, started by hand; the CI image does not have it) on the partitions cut out of the 64 MiB and the 2 GiB image: "clean. directories 1, files 0", exit 0. The checker in `run_fmt.sh` runs it too wherever the image has it.

**Board steps (Alex).** B1: F3, the media level: size and "FAT32" right for the stick and the card, "N boot menu rows live here" right; F, type the name, ESC: nothing changed (read the stick on the Mac). B2: F, name, RETURN, RETURN: the page runs to "is formatted", `diskutil info` says ExFAT / Master Boot Record, `fsck_exfat` clean. B3: copy `MegaAGS.hdf` over, the browser lists it as 8.3 GiB. B4: pull the stick during the progress page: the failure page, a blank stick, and a second format works after re-insertion; also ESC during the progress: "cancelled", blank. Read from the log: the free heap, the time of the format and the bytes written.

---
<!--
Closing: Status → DONE (date) + which scenarios are green where; the TODO row gets ✅ and a
history entry, in the same commit as the code.
-->
