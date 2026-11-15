# T49 — Update packages for TREX-internal testers

**Status:** spec 2026-10-03, docs only (nothing built, nothing flashed). **Task:** TODO.md T49 (M1).
**Depends on:** T15 (flash boot, stamp gate), T44 (flash write gate, open in M1).

**Decided (Alex, 2026-10-06), overrides the rest of this spec where it differs:**
- **Date and time (Alex, 2026-10-10, built 2026-10-10 on t49-time):** the app hands its wall-clock time to the updater when
  "Run Update" jumps (§4.4); the updater shows the real date and its backup files are named `TRXBK_YYYYMMDD_nnn.BIN` /
  `.CFW` (long file names). Without a valid app time (no SNTP, never set) the updater shows no date line and names the files
  `TRXBKnnn` as before. Wherever this spec says `TRXBKnnn` it means that name when the date is missing.
- **Restore bundle (Alex, 2026-10-09, [ADR-0050](../../adr/0050-updater-restore-bundle-config-asked.md)), built 2026-10-09:**
  besides `TRXBKnnn.BIN` the updater writes the backup as an installable `TRXBKnnn.CFW` (same nnn, same medium): its own
  code + the saved bitstream and app + the config pages + a manifest with the restore flag. Config is always in the backup
  and the bundle, no question at backup time; running a bundle asks "Restore your settings from the backup?" (default NO =
  Q5 holds). The updater finds its blobs through the manifest instead of link symbols, and skips the AXSS match for a
  restore (§3.3, §4.3). Dead bitstream: still JTAG. Details and test results: §4.3, §6.4, §8. Differences from the
  ADR text: none; a flash without a bitstream/app pair (erased app slot, no bitstream at all) gets the raw `.BIN` only
  and a notice, because there is nothing to restore (§4.3). A raw bitstream without `.bit` header (openFPGALoader) is bundled too (§4.3, 2026-10-10).
- **50T only, for the C64 Ultimate and the Ultimate 64 Elite II.** Both run the same 50T bitstream: Gideon's
  `do_update` picks the bitstream only by `getFpgaType()` (capability bits 29:28 of the running bitstream), never
  by board, and flashes `u64e2_50t` on both; our 50T core ran on Till's UE2-50T (T53, 2026-10-05). The pre-header
  carries the family `U64MK2` and no product (as Gideon's `PRODNAME`, commented out), so both accept it. **No
  100T:** the only 100T machine is Commodore's C77 (ships 2027; Gideon added 100T support on 2026-07-30), no tester
  has one. The updater refuses FPGA type 3 before writing anything ("no bitstream for this board").
- **No T+R+X unlock (T50) in this spec for now.** T44's gate is open in M1 (`g_low_limit` 0), so the updater writes
  on our core as on his; verify-after-write (§4.1 step 7) catches writes that do not take. The unlock comes back
  with T44/T50 (M3).
- **Files:** `.cfw` (accepted on every FPGA type) and `.ue2` (the same record without the pre-header; the stock
  firmware takes it while `getFpgaType() < 3`, `filetype_u2p.cc` 78-87), plus `trxmon.u2a` beside them.
- **Names (Alex, 2026-10-10, replaces 2026-10-06's `trx64_update.cfw`):** "die Commit-Nummer bitte in die Dateinamen und das
  Datum": `trx64_update_<commit>_<YYYYMMDD>.cfw` / `.ue2` (commit = the first 8 characters of the TRX64 commit, the id's
  last part; date = build date) and `trxmon.u2a`; the full id lives in the manifest and on the updater screen. Later the
  release tag replaces commit and date in the name (Alex: "Später kommt da unser Release Tag rein statt Commit und Datum"). `tools/make_update.sh` writes them with `SHA256SUMS` into a directory
  `trx64-<id>/`. (Replaces `trx64-<id>.cfw` in §3.1/§3.4.)
- **Screen (Alex, 2026-10-06):** the title bar reads `  ** TRX64 Ultimate Updater **` (two leading spaces to centre it; TRX64, not TREX64: Alex 2026-10-10) in orange (colour 8, which no
  Gideon updater uses), and `Package <id>` stands in the first console line right under it.
- **As built (built 2026-10-06, branch `t49-update-app` 3e96f332 in the app repo):** differences from the body, all
  deliberate: (1) §3.3 manifest layout fixed, the table did not add up (id 32 + two commits of 16 did not fit before
  offset 64); the layout now lives in `tools/trx_pkg.py` and the C header `update_u2p/trx_manifest.h` is generated from it
  (132 bytes, CRC at 128; `make_update.sh` refuses a stale header). (2) The updater build needed `usb_scsi.cc` added:
  Gideon's updater has no USB mass-storage driver, so the backup (Q5) could not reach a stick otherwise. (3) `.align 4`
  in the blob `.s` is 16 bytes on RISC-V; the checker accepts gaps up to 15 bytes (softboot's find_app allows 16).
  (4) Flow order is: check package in RAM, show installed/package, YES/NO, **backup**, app, bitstream, verify.
  NO writes nothing (not even the backup). (5) Backup file `TRXBKnnn.BIN` (8.3 name; with a date `TRXBK_YYYYMMDD_nnn.BIN`, §4.4) on the first `Usb*`/`SD*` root
  entry (or its `PartN`) with enough free clusters: 64 byte header, 0x000000-0x3FFFFF, 0xFE8000-0xFFFFFF, trailer
  `TRXC` + CRC-32 + CHK; read back from the medium and compared before any write; no medium = retry popup, else abort.
  (6) The manifest flags (html, config reset) are refused by the checker and not implemented in the updater (M1:
  only the restore flag, 2026-10-09, is allowed). (7) Inputs: `make_update.sh --manifest-json <CI manifest.json>` gives id `<branch>-<8 digit trx64 commit>`.
  (8) Not run in ue2emu: `emu_start firmware=<.ue2>` only extracts the app from the record, it does not run the updater.
  The updater had never run on any machine when this was written; USB enumeration and the backup path in
  particular were untested. **It ran on 2026-10-14** and installed the multi-core bundle
  `trx64mc-main-9eeedabb` (host bitstream to 0x0, T84 host app to 0x220000): the boot ROM's result block
  reads `length 0009A628, sum A06933A5, xrot F2A255F9, build 6D356956, flash bitstream 6D356956`, which is
  exactly the app blob of that bundle and the new host. Started from a core's application (the first
  attempt, from the Amiga core, hung: the updater takes over a C64 that is not in the socket - see
  T84-hostapp.md, where Alex decided the host app gets this function later).
**Layout:** `docs/c64u-flash-layout.md`. Gideon's code quoted below is in the app submodule
`software/ultimate-app` (`trex/main`, 9675f852); every statement is read from code, not from comments.

## 1. Goal

A tester with a C64U and a USB stick installs and updates our bitstream (0x000000) and our
app (0x220000) without JTAG, with the procedure Gideon's updaters use: put a file on the
stick, pick "Run Update" in the file browser. Nothing else in the flash changes unless the
package says so. One artifact serves both situations:

- **(a) first install** on a machine that still runs Gideon's/Commodore's firmware (no gate; his
  file browser runs our updater);
- **(b) update** on a machine that runs our core (the T44 gate is open in M1, no unlock).

## 2. What Gideon's updater does (read from the code)

### 2.1 Package, file type, loading

- Build (`target/u64ii/riscv/update/Makefile`): the updater is a normal RISC-V program linked at
  0x03000000 (`linker.x` 7, 16 MB) whose rodata carries the blobs by `.incbin`
  (`update_binaries_u64ii.s`: `u64e2_50t.swp`, `u64e2_100t.swp`, `ultimate.app`, in this order, each
  4-aligned). `u64e2_*.swp` are plain copies of `external/u64e2_*.bit` (Makefile 143-148;
  `FPGA_PATH ?= $(PATH_SW)/../external`, line 13, so it can be overridden on the command line). ROMs,
  the drive sound files, the ESP32 images and the web pages are linked the same way (`SRCS_BIN`, 120).
- `tools/checksum` makes one `#define CHK_<name> 0x…` per blob (`checksum.c`, the loop of
  `calc_checksum`, `update_common.h` 34-47: `if (c < 0) c ^= 0x801618D5; c <<= 1; c += byte`, 32 bit);
  the results are concatenated into `checksums.h` (Makefile 170) and compiled into the updater.
- `rules.mk` 57-63: `hex2bin -r` turns the ELF's ihex into `update.app`, one record
  `{load u32 LE, length u32 LE, start u32 LE, data padded to 4}` (`hex2bin.c` 31-38). The root
  `Makefile` copies it to **`update.ue2`** (379, 397). With `-F family -P product` (the `.cfw` rule)
  `hex2bin` puts a 36-byte pre-header in front (`hex2bin.c` 345-351): fingerprint 0x4115ADDE (bytes
  `DE AD 15 41`), family[16], product[16]. The built files confirm it: `update.ue2` starts
  `00 00 00 03 | E4 9D 8C 00 | 00 00 00 03` (load 0x03000000), `update.cfw` starts `DE AD 15 41 "U64MK2"`.
- Recognition (`software/filetypes/filetype_u2p.cc`, `FileTypeUpdate::test_type` 78-87): extension
  **CFW** always (format 1); the product's own extension (`UE2` for U64=2, `product.cc`
  `getProductUpdateFileExtension`) only while `getFpgaType() < 3`. Context menu: "Run Update".
- `execute` 111-141 reads the records straight to `header.load` (`load_format_0` 146-176: no buffer, the
  file goes to 0x03000000). Format 1 first checks the fingerprint ("Invalid Update Image"), then the
  family against `getProductFamily()` (`U64MK2` for the whole U64-II/C64U family) and, if set, the product
  name against `getProductString()` ("C64 Ultimate" on a bling-board machine, `product.cc` 130-140), all in
  `load_format_1` 181-219. Then `release_ownership`, IRQs off, jump to `start` (`jump_run` 93-100). The
  updater owns the machine from there.

### 2.2 What it writes (`update_u64ii.cc` `do_update` 100-202)

After the checks (§2.3), `check_flash_disk()` (152) and the confirm popup (156):

1. flash-drive files: `/flash/roms` 1581.rom, 1571.rom, 1541.rom, snds1541/1571/1581.bin (164-169;
   `write_flash_file` = `FA_CREATE_ALWAYS`, a failure aborts through `turn_off()`), `/flash/html`
   index.html, api.html, openapi.yaml (171-172);
2. `protect_disable()` (174): WREN + SR1 = 0, WREN + SR2 = 0;
3. FPGA type 2 (the C64U): `flash_buffer_at` bitstream at **0x000000** (177), app at **0x220000** (178),
   both `header = false` (the blobs as stored); type 3 would put the app at 0x3C0000;
4. `write_protect(flash2, 4096)` (179);
5. ESP32 (190-192, unless `NO_ESP`): bootloader, partition table, `u64ctrl.bin` at 0x0/0x8000/0x10000,
   only if the module's version differs (75-98);
6. `reset_config` (195): popup "Reset Configuration? (Recommended)"; yes erases all 16 config sectors
   (`update_common.h` 79-91, `W25Q_NUM_CONFIG_PAGES` 16);
7. `turn_off()` (199-201): `wifi_machine_off()` after 5 s.

`check_flash_disk` (`update_common.h` 228-262) asks "Reformat Flash Disk?" when the flash drive is not
empty: answering yes erases the tester's flash drive.

### 2.3 What it verifies

- Before any write, in RAM: `calc_checksum` over the embedded bitstream and app against `CHK_*`
  (125-150); a mismatch ends in `while(1)`. This catches a damaged file or a bad USB read.
- While writing (`prog_flash.cc` `flash_buffer_at` 45-125): each 256-byte page is programmed, read back
  (`read_page`) and compared (`my_memcmp`, 104-112), three tries per page; the 4K sector is erased just
  before its first page (84-95). Images over 9,000,000 bytes are refused (49).
- No second pass over the finished flash, no checksum of what is in the flash afterwards.

### 2.4 What it does on a failure

`flash_buffer_at` returns false after a failed erase or a page that fails three times (89-92, 115-119).
**`do_update` ignores the return value** (177-178): it goes on with the next image, the ESP32,
`reset_config` and `turn_off()` as if nothing had happened. No backup of the old images, no order that keeps
the machine bootable, no retry of a sector. A power loss while the bitstream is written leaves a flash with
no usable bitstream: the FPGA does not configure. His answer is the JTAG recovery kit
(`recovery/u64ii/README.md`: FT232H cable, `recover.py` loads bitstream and app into DDR2, then "Run
Update" again).

`protect_configure` writes nothing unless the chip's BP bits already equal the wanted ones (`w25q_flash.cc`
463: `!= bp_bits` → `return true`), and `protect_enable` is empty in the base class. His updater never sets
the chip's protection on a W25Q128 with SR1 = 0 (the board reads SR1 = 0x00, T44 §11), so `protect_disable`
is a no-op in practice.

### 2.5 What `.cfw` is

Format 1 of the same update file: the `.ue2` record with the 36-byte pre-header of §2.1, made by
`hex2bin -F U64MK2 [-P name]` (`filetype_u2p.cc` 78: "Commodore compatible multi-target firmware"). The format
is fully defined by the code above. The stock application in the flash dump contains the handler: the strings
`CFW`, `UE2`, `Run Update`, `Invalid Update Image`, `Wrong Product Family`, `Update for Wrong Product`,
`U64MK2`, `C64 Ultimate` are in the app at 0x220000 of `c64u_flash.bin` (dump 2026-09-30, header
0x30000/0x14262C/0x30000). **Not in any code we have:** Commodore's own release `.cfw` and its updater (so
whether Commodore adds a signature or a version rule is unknown), and the stock handler's control flow (only its
strings were checked, not disassembled). Open, Q7.

### 2.6 What we do not copy

The ROM and web-page writes, "Reformat Flash Disk?", `protect_*`, the ESP32 step, `reset_config`, the second
(100T) bitstream and the ignored return values. Taken over unchanged: the file format and pre-header, `CHK_*`,
`flash_buffer_at`'s page loop and read-back, the console/UI setup (`update_common.h` `setup`), `turn_off`.

## 3. Our package

### 3.1 Artifacts

`trx64-<id>.cfw` (format 1) and `trx64-<id>.ue2` (format 0, the same record without the pre-header; `<id>` =
`<branch>-<8 digit commit>`, as the CI package versions), plus `trxmon.u2a` of the same build. Pre-header: family
`U64MK2`, no product (as Gideon's, `PRODNAME` commented out), so the C64 Ultimate and the Ultimate 64 Elite II both
accept it (decided 2026-10-06: same 50T board, same bitstream). Our own app accepts it too (family `U64MK2`).

### 3.2 Inside

One format-0 record at 0x03000000 = the updater program (`update_trx`) followed by the **blob region**, in this order, each
4-aligned (changed 2026-10-09 for the restore bundle; before: bitstream, app, manifest inside rodata):

1. `trx_manifest` (§3.3), first, found by the link symbol `_trx_manifest_start` and, for host tools and `trxdbg`, by its magic;
2. our bitstream, the `.bit` file byte for byte (as `u64e2_50t.swp`; the header before the sync word is what
   Gideon's flash carries, softboot's `bit_length` parses it from flash offset 0), at most 0x220000 bytes;
3. `ultimate.app` in his record format (`{dest, length, run}` + data), data at most 0x1DFFF4 bytes (T15 §4.1);
4. only in a restore bundle: the 0x18000 bytes of config pages.

Softboot's `find_probe`/`find_app` (`apps/softboot/src/softboot.c`) need exactly "bitstream, then the app record within 16
bytes", nothing about the manifest, so "Run as Softboot" on our own package keeps working as a no-flash test of the app
(host check: `tools/test_trx_pkg.py` `softboot_scan`, on the built package and on a built bundle).

The region is the **tail of the image**: `linker.x` of `update_trx` gives `.data` its own VMA in a second region `ram`
(0x03600000, crt0 copies it there from its load address, which it did as a no-op before) and puts `.bss` and the heap
behind it, so no address in the code depends on the size of the blobs. That is what lets the updater copy "everything in
front of `_trx_manifest_start`" (text, rodata, the pristine `.data` image: 268,240 bytes in the 2026-10-09 build) into a
bundle and append other blobs. An `ASSERT` stops a build whose blobs would reach 0x03600000.

Size today: bitstream 1,727,492 (the stream flashed on 2026-10-03) + app about 1.6 MB + updater code about
0.3 MB = **about 3.7 MB** (Gideon's `update.cfw` is 8.8 MB: two bitstreams, ROMs, ESP32 images).

### 3.3 `trx_manifest` (little endian, 4-aligned)

Magic `TRXPKG02` (layout of 2026-10-09; `TRXPKG01` had no offsets and no config). The offsets `bit_off`, `app_off`,
`cfg_off` count from the start of the manifest; the layout is fixed (manifest, bitstream, app, config, each padded to 4,
nothing behind), the updater and the checker both refuse any other.

| off | size | field |
|---|---|---|
| 0 | 8 | magic `TRXPKG02` |
| 8 | 4 | manifest length in bytes (156) |
| 12 | 4 | flags: b0 write the `/flash/html` pages (Q1, refused), b1 reset the config pages (Q5, refused), **b2 restore bundle**, b3 raw bitstream (restore bundle only: blob = 48 byte header + image, only the image is flashed) |
| 16 | 32 | package id (`<branch>-<commit>`; in a bundle `TRXBKnnn`) |
| 48 | 16 | bitstream commit (display only; in a bundle `AXSS xxxxxxxx` or `no stamp`) |
| 64 | 16 | app commit (display only; empty in a bundle) |
| 80 | 4 | bitstream offset (156) |
| 84 | 4 | bitstream length |
| 88 | 4 | bitstream **AXSS**, the USR_ACCESS stamp it writes (T15 §4.2); 0 in a bundle of a bitstream without one |
| 92 | 4 | bitstream `CHK` (Gideon's algorithm) |
| 96 | 4 | bitstream CRC-32 (IEEE) |
| 100 | 4 | app offset |
| 104 | 4 | app file length (record header + data, padded to 4) |
| 108 | 12 | app dest, length, run (as in the record header) |
| 120 | 8 | app `sum` and `xrot`, as the boot ROM's `boot check` computes them (T15 §5) |
| 128 | 4 | app `CHK` |
| 132 | 4 | app CRC-32 |
| 136 | 4 | config offset (0 when none) |
| 140 | 4 | config length (0 or 0x18000, only with the restore flag) |
| 144 | 4 | config `CHK` |
| 148 | 4 | config CRC-32 |
| 152 | 4 | CRC-32 of the manifest up to here (total 156 bytes) |

The compile-time `CHK_*` constants of Gideon's updater are gone from `update_trx` (the blobs differ per package and per
bundle); the manifest's CHK and CRC-32 of every blob are checked instead, and the manifest has its own CRC-32.

The builder takes AXSS by the same packet walk as `software/bootrom/bootrom.c` `flash_axss` (181): sync word in
the first 4 KB, type-1/2 packets, first write to register 0x0D. The updater repeats the walk on the embedded
bytes and refuses a package whose stamp differs from the manifest. In a restore bundle the stamp may be absent (a stock
bitstream): then the manifest says 0 and the walk must also find none; the stamp is not matched against anything of ours. Gideon's `CHK_*` stay compile-time constants
as today; CRC-32 is added because `CHK` is a shift-add sum (fine against a bad copy, weak against anything
structured). All sums are integrity, not authenticity (Q8).

### 3.4 Build

`tools/make_update.sh <bitstream.bit> <ultimate.app>` (new; run in the app build container, MEMORY.md
"Build-Container"):

1. checks the inputs: bitstream ≤ 0x220000, AXSS found, app header valid, data ≤ 0x1DFFF4, both commits given;
2. writes the blob region (`tools/trx_pkg.py blobs`: manifest, bitstream, app), runs
   `make -C target/u64ii/riscv/update_trx BLOBS_FILE=<that file>`;
3. runs the host checker `tools/trx_pkg_check.py <file.cfw>` (pre-header, record, manifest CRC, blobs against CRC
   and `CHK`, `sum`/`xrot` as the ROM computes them, AXSS walk);
4. names the result `trx64-<id>.cfw` with its SHA-256 beside it.

`target/u64ii/riscv/update_trx/` (app repo) is a copy of `update/` with `SRCS_ASM = update_binaries_trx.s`
(the one blob file as section `.trxblobs`), no 100T bitstream, no ROM/ESP32/html `SRCS_BIN` (the html pages only when flag b0 is
built in), `-DNO_ESP`, `PRODNAME` empty (family only). It deliberately does **not** set `FLASH_EXTRA_PROTECTION`: that
flag keeps `prog_flash.cc` and `protect_*` out of the application, the updaters are the separate builds that carry
them (commit bc6019a8). T31's package stage later does the same step in CI (matching bitstream and app from one
commit); until then Alex runs the script.

## 4. The updater (`update_trx.cc`)

Standalone, like Gideon's, for both paths: path (a) needs it standalone anyway (the stock firmware cannot run a
step inside our app), and one artifact is less to test. Our running app executes from DDR2, so an in-app step
would also work on path (b), but it would be a second code path for the same writes.

### 4.1 Flow

1. `setup()` as Gideon's (UI, console), title "TRX64 Ultimate Updater". Detect: flash (`get_flash()`; must be
   the W25Q, JEDEC EF 40 18), FPGA type (must be 2; 3 → "no bitstream for this board", turn off, nothing written). C64U and UE2 both pass
   (same 50T board, decided 2026-10-06).
2. **Core.** `TRX_ID` = "TRX2" (`trx_io.h` 15) tells our core from his, for the display only. No unlock check
   (decided 2026-10-06): the gate is open in M1.
3. Verify in RAM: manifest CRC, bitstream and app against manifest CRC and `CHK_*`, the AXSS walk. Any failure:
   message, turn off (Gideon: `while(1)`).
4. Show *installed* (AXSS from the flash bitstream, app header length, on our core the running BUILD) and *package*
   (id, stamps), ask "About to update. Continue?" (YES/NO, as 154-157). NO writes nothing: this is the dry run of the
   board tests (§8).
5. **App slot first, header last** (§5.1). Erase the slot's first 4K sector (header gone: the boot ROM refuses an
   erased header, T15 §2 step 7), program everything behind it, each page read back and compared as `flash_buffer_at`
   does (three tries), then the first sector, its 16 pages in **descending** order so that page 0, the header, is
   written last. Each further sector is erased once, just before its first page.
6. **Bitstream last**: erase, program, read back from 0x000000 in ascending order, as `flash_buffer_at`. It goes last
   because it is the only step whose interruption leaves nothing that runs; everything else has verified before it
   starts.
7. **Verify after write** (new): read both ranges back from the chip (`read_linear_addr`), CRC-32 and `CHK` against the
   manifest, parse the AXSS from the flash bitstream and compare it with the manifest (exactly the condition of the
   T15 boot gate at the next power-on), read the app header and compare. A failure shows which part, and the exit is
   **not** `turn_off` but "Update FAILED, do not switch off": the failed part is written again from RAM (the package is
   still there), three times at most, then the message stays.
8. "Update OK, installed <id>", `turn_off()` after 5 s.

Every return value is checked. `reset_config`, `check_flash_disk`, ROM files, `protect_*` and the ESP32 are not called;
the flash drive is never mounted (`setup(..., false)` skips `init_flash_disk()`, which formats the drive when its probe fails). The manifest flags switch the html pages and the config reset on (Q1, Q5).

Writes use the register sequences of `w25q_flash.cc` `write_page`/`erase_sector` (0x06, 0x02 with 256 bytes, 0x20,
3-byte addresses): opcodes the T44 gate passes. The updater does not call `protect_disable` on our core; the gate
blocks status writes even while open.

### 4.2 Time

W25Q128 data sheet typicals: 4K erase 45 ms, page program 0.7 ms. Bitstream 423 sectors and 6,750 pages → about
19 s + 5 s; the app (about 390 sectors, 6,200 pages) the same: **about 50 s of writing**, several times that at the
data-sheet maximum (400 ms per erase). Measure through the gate (§8, B2), not assumed.

### 4.3 Restore bundle (built 2026-10-09, ADR-0050)

**Writing it** (inside step 5 of §4.1, "backup", before any flash write). `cfw_prepare` reads the flash: bitstream length from
the `.bit` header at 0x000000 (softboot's `bit_length`, at most 0x220000), the app record at 0x220000 (the checks of the boot
ROM's `header_ok`: length 1..0x1DFFF4, dest 4-aligned and at least 0x10000, run inside the image), CRC-32/CHK of both and of the
config pages, `sum`/`xrot` of the app, AXSS by the walk of §3.3 on the bitstream's length. The free-space test counts raw
backup plus bundle (8.1 MB on the stick with today's blobs). Then `TRXBKnnn.BIN` as before; then, with the same nnn on the same
medium, `TRXBKnnn.CFW`: 36 byte pre-header (`DE AD 15 41`, `U64MK2`, no product: byte for byte what `hex2bin -F` writes for our
package), the record header `{0x03000000, length, _start}`, the updater's own image from RAM (everything in front of
`_trx_manifest_start`), the manifest with the restore flag and the id `TRXBKnnn`, bitstream, app, config pages. The file is
closed, opened again and compared byte for byte against the bytes the writer generated (code from RAM, blobs from the flash
again), not just a sum. Any failure of either file deletes both and goes through the same retry popup / "No backup, no update"
as the `.BIN`; nothing is written to the flash before both files are verified. A nnn whose `.CFW` already exists is skipped,
so the pair always shares its number. **Raw bitstream (2026-10-10).** Gideon's updater (`update_u64ii.cc`, `u64e2_50t.swp` = a plain `cp` of the `.bit`) and ours both
write the `.bit` file with its header at 0x000000; a flash written by openFPGALoader holds the raw image instead. It is
detected by the bus-width words `00 00 00 BB 11 22 00 44` followed by the sync word `AA 99 55 66`, both in the first 256
bytes. Its length is the **last byte of the 0x220000 byte slot that is not 0xFF, rounded up to 4**: everything behind it is
erased, so writing the image back reproduces the slot exactly, whatever the end of the configuration stream looks like (a
packet walk could stop in front of trailing data; a stale tail of an older longer bitstream is simply part of the slot and
is kept). The bundle's bitstream blob is a synthesized 48 byte `.bit` header (fields a-d fixed, e = that length;
`trx_pkg.raw_header` / `raw_header_make`) plus the image, so manifest, length check, sums and AXSS walk work unchanged;
manifest flag `TRX_FLAG_RAWBIT` (8, only with the restore flag) tells the updater to write **only the image** at 0 and then
erase every non-blank sector behind it up to 0x220000. Restored, the slot holds exactly the bytes it had (raw, no header),
the state that was proven to boot; writing the header in front, as a normal install does, would also boot but would not
be a restore. The AXSS walk is done over header + image, as `check_package` does on the blob. softboot's probe scan still
needs a `.bit` header in the flash, so "Run as Softboot" of such a bundle is not offered by this change.
**Not bundled, notice, raw `.BIN` still written:** the flash holds neither a `.bit` header nor a raw bitstream at 0,
or no valid app record at 0x220000 (a half-flashed or JTAG-recovered machine): there is nothing to restore.

**Progress lines (2026-10-10).** Before each check after a write the updater prints "Verifying the flashed application..",
"Verifying the flashed bitstream..", "Verifying the settings.." (bundle with settings) and "Verifying the application once
more.."; before this the screen stayed silent for 5-10 s.

**Running it** (manifest flag `TRX_FLAG_RESTORE`). The same updater; differences: the title and the first line read
`Restore backup TRXBKnnn` instead of `Package <id>`; the AXSS match is skipped (a stock bitstream has none or another),
CRC-32/CHK/`sum`/`xrot` of every blob, the `.bit` header length, the record header and the layout are still checked and any
failure is "Package damaged. Nothing written."; after "About to restore. Continue?" (YES) it asks "Restore your settings from the
backup?" with **No** as the first, default button; it takes a fresh backup (`.BIN` + `.CFW`, so a restore can itself be undone);
writes app (header last), bitstream, and, only after YES, the config pages (24 sectors); every part is read back (CRC-32/CHK, and
for the bitstream the AXSS only if it has one), rewritten up to three times; "Restore OK, installed TRXBKnnn [with settings]".
**Why the config goes after the bitstream:** it is the only step whose interruption leaves a machine that still boots (§5.1),
so it cannot endanger the bitstream, and it starts only after app and bitstream have verified. NO touches no config sector
(Q5): the emulator test compares the whole 0xFE8000-0xFFFFFF range before and after.

### 4.4 Date and time (built 2026-10-10)

The updater has no network, and on our bitstream no RTC (ADR-0011, T61: the app keeps the last SNTP/menu time plus FreeRTOS
ticks in `rtc_dummy.cc`), so it started with 1970 ("Thursday January 1, 1970 00:00:00" on its screen). The app now hands its
clock over:

- **Handover.** `FileTypeUpdate::execute` (`filetype_u2p.cc`) writes four words at `0x03F00000` just before `jump_run`:
  magic `TRXT`, year<<16|month<<8|day, hour<<16|minute<<8|second (local time, as the app shows it), and a check word
  (`magic ^ date ^ time ^ 0x5A17C0DE`). `0x03F00000` is inside the updater's window (`update_trx/linker.x`: 0x03000000-0x03FFFFFF) but
  owned by nothing there: the image ends below 0x03600000, `.data`/`.bss`/heap grow up from that, the stack comes down from
  0x03FFFFFC. The layout is `software/io/rtc/trx_time_handover.h`, shared by both sides.
- **Only on our bitstream, only with a valid time.** The app writes it only when `trx_io_caps() != 0` (weak reference: other targets
  never link it), and a cleared record when `rtc_time_valid()` is false (clock before 2020 = never set), so a stale record of an
  earlier run cannot pass for a time. Gideon's updaters are not touched: they never read the words, and `update_common.h`
  still prints their date line unconditionally (`UPDATE_SHOW_DATE()` defaults to true; only `update_trx.cc` overrides it).
- **Updater.** `ultimate_main` takes the record first (checks magic, check word, plausible fields; clears it either way) and calls
  `rtc_adopt_local_time`, which puts `rtc_dummy.cc` into software mode (the updater cannot probe `trx_io_caps`) and sets the
  clock. The screen prints the date line only when `rtc_time_valid()`. The FAT time stamp of files it writes comes from the
  same clock. The updater has no time zone: it shows the handed local time as is.
- **Names.** FatFs in the updater is built with `FF_USE_LFN 2` (`chan_fat/full/ffconf.h`), so long names work: with a date
  `TRXBK_YYYYMMDD_nnn.BIN` and `TRXBK_YYYYMMDD_nnn.CFW` (`nnn` = first free number for that day, the pair shares it), the
  bundle's id/title is the stem (`Restore backup TRXBK_20261010_000`, 18 of the 32 id bytes). The date is fixed when the backup
  starts, so the pair and the id agree across midnight. No date: `TRXBKnnn` as before.

**ue2emu result (2026-10-10, emulator only, no board).** `--emu-test` updater built on t49-time-app f26d280f (container, `tools/make_update.sh`; bitstream and app are the 2026-10-06 bitstream and the new `ultimate.app`), host app `ultimate.elf` of the same commit, `--caps 24E40226`, net=true (SNTP answers in the emulator, TZ CET/CEST), a 64 MiB `--usb-dir`, flash with bitstream at 0 and app at 0x220000. Run Update, YES: updater screen line "Saturday October 10, 2026 10:30:09" (local time, was "Thursday January 1, 1970"); stick holds `TRXBK_20261010_000.BIN` (4,292,684 bytes) and `TRXBK_20261010_000.CFW` (3,599,236 bytes), screen "Writing the restore bundle TRXBK_20261010_000.CFW", "Backup /USB0/TRXBK_20261010_000.BIN", "Update OK". Second run without net: the emulated app still had a time (08:30:20, no TZ set, shown as is); the updater showed it. Not covered: the no-time branch (the emulator always gives the app a time, so "no date line, `TRXBKnnn`" is read from the code, not run); the handover record's checks are exercised by a host compile test of `trx_time_handover.h` (put/take/clear, bad check word, year 1970 refused); the FAT time stamp inside the image; a second backup on the same day (`_001`); anything on the board.

## 5. Failure and recovery

### 5.1 What a power loss leaves

| when | flash | the machine |
|---|---|---|
| before step 5 | untouched | boots as before |
| during the app (step 5) | bitstream good, app header erased (complete only after the last page of step 5) | boot ROM: erased header → "Empty; waiting for JTAG" (T15 §2 step 7); no jump into half an app; **the screen stays black** (the ROM writes to the console only) |
| between app and bitstream | new app, old bitstream | boots; the app may not match the old bitstream; the tester runs the update again |
| during the bitstream | no usable bitstream | the FPGA does not configure: dead, **JTAG only** (Alex writes the dump or the package's `.bit` with openFPGALoader, as on 2026-10-03) |
| during the config pages (restore bundle, answer YES) | bitstream and app good and verified; config sectors partly erased/written | boots; settings that are not readable fall back to the defaults (Q5 never wrote them before, so a tester loses only what the restore was about to replace). Run the bundle again to retry |

This is Gideon's exposure with two improvements: the machine never boots a half-written app (header last), and the
risky step comes last and starts only after everything else has verified. It is **not** a tester-proof recovery: a
dead bitstream or a dead app slot needs a JTAG cable. Testers update on mains power.

### 5.2 Why no rollback

There is no room for a second copy. Bitstream slot 0x220000 bytes, 0x1A5C04 used (500,732 bytes left, holding the
tail of Gideon's old stream, fragmentation, not a pool); app slot 0x1E0000, 0x184160 used (376,480 bytes left at
the end of the slot). A/B needs a second slot of each, i.e. a smaller flash drive and a reformat; the FPGA's MultiBoot
fallback (golden image at 0, update image at a second address) needs the same. Both belong to M3's boot chain
(T23/T26), Q2.

### 5.3 Recommended strengthening of the boot ROM (R1, Q3)

The ROM copies the whole app anyway and computes `sum` on the way (T15 §5). A trailer `{magic "TRXC", sum, xrot}`
right behind the data, written last by the updater, lets it refuse an app whose sum does not match: about 40 bytes of
ROM code, backward compatible (no trailer = boot as today, so Gideon-format images stay bootable). It also covers a
stale page inside the app that header-last does not. It needs a new bitstream before the first package, hence a
decision, not part of the minimum.

## 6. The two paths and the instructions for testers

### 6.1 (a) First install, machine runs Gideon's/Commodore's firmware

1. Copy `trx64-<id>.cfw` to a USB stick (any folder).
2. Power on the C64U on mains, open the menu as usual, stick in.
3. File browser → the stick → the `.cfw` → **Run Update**. Our updater screen appears (the stock firmware has jumped to it).
4. Read installed/package, **YES**. Do not switch off or pull the stick for about a minute. Done when "Update OK" shows;
   the machine switches itself off after 5 s.
5. Switch on. Our core boots (T15: stamp equal, flash app starts). The stock firmware is gone from the flash.

No gate and no unlock here: his core cannot be gated. The updater refuses a wrong board or product.

### 6.2 (b) Update, machine runs our core

1. Stick with the `.cfw` (or `.ue2`), file browser, **Run Update**, YES.
2. Wait for "Update OK"; it switches off.

Way back to the stock firmware: Gideon's/Commodore's own `.cfw`/`.ue2` can be run from our
menu the same way (writes 0x000000 and 0x220000, ROM files, asks about the config reset). The open gate passes his
program/erase opcodes and still blocks his `protect_disable` status write (harmless, §2.4).

### 6.3 What this does not touch

ESP32 firmware, ROMs, flash-drive files and settings stay as they are unless the manifest flags say otherwise.
Downgrades are allowed (installed and package are shown, the tester decides), Q9.

### 6.4 Backup and power-off

Before it writes anything the updater saves the current bitstream, app and config pages (0x000000-0x3FFFFF and
0xFE8000-0xFFFFFF) twice on a USB stick or SD card with enough free space (about 8 MB; without such a medium it asks for one
and does not update), both files with the same number `nnn`:

- (With the app's clock, `TRXBK_YYYYMMDD_<nnn>`; without, `TRXBK<nnn>`; §4.4.)
- `TRXBK<nnn>.BIN` (about 4.3 MB, with a checksum, read back after writing): the raw copy. Until the golden stub (T26) exists it
  can only be put back over JTAG, and it is the only way back if the bitstream is dead.
- `TRXBK<nnn>.CFW` (about 3.7-3.9 MB): the same content as an **installable package**. Put it on a stick like any update file,
  File browser -> `.CFW` -> Run Update; the updater shows `Restore backup TRXBK<nnn>`, installed vs backup, asks YES/NO, and
  then "Restore your settings from the backup?" (the default is NO: your current settings stay). This returns the machine to
  the firmware it had before the update, also Gideon's/Commodore's, **without JTAG, as long as the machine still boots to the file
  browser**; a dead bitstream still needs the `.BIN` and JTAG.

Keep both files. **The stick holds the network password in clear** (the config pages are in both files): do not pass the files
on. Update on mains power and do not switch off or pull the stick before "Update OK"; the machine then switches itself off
after 5 s, switch it on again afterwards (no automatic restart, decided 2026-10-03).

## 7. Memory

The browser's handler reads the file to 0x03000000 (`load_format_0`); the region meant for it is
0x03000000-0x03BFFFFF, 12 MB (`docs/ultimate-app-hw-spec.md` 3a; our app's cart ROM is in the expansion-port block at 0x01000000 since T60 §7, Gideon's at 0x03C00000, and is live
until the jump). The package (about 3.7 MB) fits more than three times; the builder refuses more than 0x00A00000
bytes so that softboot's `STAGE_LIMIT` (softboot.c 29) also holds. The updater does not copy the blobs: pages are
programmed straight from the image in RAM (`flash_buffer_at` with `header=false` takes the pointer). The image ends at the
blobs; `.data`, `.bss`, the heap and the stack are above 0x03600000 (§3.2), so a bundle of up to about 4.5 MB (bitstream
slot + app slot + config + code) fits below it, and `check_manifest` refuses a package whose blobs would not. Working memory is the
256-byte verify buffer and the CRC table. The CPU sees 64 MB of the 128 MB DDR2; the rest is ours and untouched here.

## 8. Tests

**Host:** `tools/trx_pkg_check.py` on a package from the current CI bitstream and app: pre-header bytes as `hex2bin`
writes them, manifest CRC, blobs equal to the inputs, AXSS equal to `trxdbg`'s `BUILD` of that build (T15 §10), the
`sum`/`xrot` equal to `trxdbg boot check` on the staged image.

**ue2emu result (2026-10-06, package main-459e9cc3, emulator only: no board, no flash on real hardware).** ue2emu has
no FPGA model, so it cannot boot our bitstream: it runs our app (main-latest `ultimate.app`), and the package is started
from a USB stick (`--usb-dir`) with the file browser's "Run Update", as a tester does. `emu_start firmware=<.ue2>` is no
substitute: it only boots the app embedded in the record, never the updater. The emulated flash is an S25FL128L (JEDEC
01 60 18, fixed in `ue2-core/src/devices/flash.rs`, no option), while the boards carry the W25Q128 (EF 40 18), so the
release updater correctly refuses it ("Not the expected flash (W25Q128). Nothing written."). The runs below use a test
build, `tools/make_update.sh --emu-test` (`-DTRX_EMU_TEST`: also accepts "Infineon S25FL128L", prints "EMULATOR TEST
BUILD - NOT FOR A REAL BOARD", output in `trx64-<id>-EMUTEST/`); same geometry (4K sectors, 256-byte pages), otherwise
the same code. Flash image: the C64U dump of 2026-09-30 with the overlay-UI config pages of a fresh image (the dump's
own config selects the freeze UI, whose screen the tools cannot read; the updater's screen was read from C64 RAM
$0400). Each run was compared with a snapshot taken after a plain app boot.

| run | result |
|---|---|
| FPGA type 3 (default caps) | "No bitstream for this board. Nothing written."; flash byte-identical |
| type 2 (`--caps 24E40226`), YES | backup `/USB0/TRXBK000.BIN` (4,292,684 bytes: 64 header + 0x000000-0x3FFFFF + 0xFE8000-0xFFFFFF + trailer), both ranges byte-equal to the flash before; app 364 sectors, bitstream 442 sectors, "Update OK, installed main-459e9cc3", switch-off; flash: bitstream at 0x0 and app at 0x220000 byte-equal to the package inputs, every other sector (config, flash drive, ROM area) byte-identical |
| one byte flipped in the app blob | "Package damaged. Nothing written."; flash byte-identical |
| read-only stick | "Writing the backup failed", "No USB/SD with 4.3 MB free. Insert one. Retry?", NO → "No backup, no update. Nothing written."; flash byte-identical |
| `.ue2` (format 0) on type 2, NO | browser offers Run Update, updater starts, package OK, installed/package shown, NO → nothing written, no backup file; flash byte-identical |

Not covered: the header-last program order (the model does not log the order), the "Update FAILED, do not switch
off" path (no way to make the model fail a verify without patching it), the boot-ROM's `sum`/`xrot` on the written app
(the emulator has no boot ROM), and the real W25Q128 path.

**ue2emu result for the restore bundle (2026-10-09, emulator only, no board, no flash on real hardware).** Same set-up as the
2026-10-06 runs (`--emu-test` updater, `--caps 24E40226`, ue2emu `main`, our `ultimate.app` as the host firmware, a 64 MiB
`--usb-dir` stick) with this layout: package `t49rb-test` built from the bitstream (1,807,969 bytes) and app (1,489,180 bytes)
of the 2026-10-06 package `main-459e9cc3` (not the latest CI build: the emulator has no FPGA, the bytes only have to be valid
and different from the flash). Flash image: bytes 0x000000-0x3FFFFF of the C64U dump of 2026-09-30 (Gideon's stock core, 2,192,121
bytes, no AXSS; stock app, 1,320,492 bytes) on a fresh ue2emu flash, so the config pages are those of a fresh image. All
comparisons are host-side on the flash image written back by `emu_stop` and the stick directory.

| run | result |
|---|---|
| A: normal package, YES | `TRXBK000.BIN` 4,292,684 bytes (both ranges byte-equal to the flash before) and `TRXBK000.CFW` 3,879,376 bytes, which is **byte-equal** to what the host twin expects (36 byte pre-header + record header + the package's updater code + `trx_pkg.restore_blobs(flash before)`); `trx_pkg_check.py --code-of <package>` PASS on it (restore bundle, flags 4, bitstream 2,192,121, app 1,320,504, config 98,304, "updater code byte-equal"). Both files written before the first flash write; app 364 sectors, bitstream 442 sectors, "Update OK, installed t49rb-test"; flash: bitstream and app equal the package, no sector outside 0x000000-0x3FFFFF changed, config identical |
| B-NO: the `.CFW` of A as update, settings **NO** (the default button) | title "Restore backup TRXBK000", installed (the package's AXSS) vs backup ("bitstream no stamp, 2192121 bytes, settings included"); a fresh backup pair under the number 001 (000 was taken by the `.CFW` on the stick): `TRXBK001.CFW` 3,663,900 bytes byte-equal to the host expectation for the flash before the run (a bundle taken by a running bundle), `.BIN` ranges equal; app 323 sectors, bitstream 536 sectors, "Restore OK, installed TRXBK000"; flash: bitstream (first 2,192,121 bytes) and app equal the original flash; **config range 0xFE8000-0xFFFFFF byte-identical to before the run** (a marker written into an unused config sector beforehand is still there; 0 config sectors changed), nothing else changed |
| B-YES: same, settings **YES** | as B-NO plus "Writing settings (24 sectors)", "Restore OK, installed TRXBK000 with settings"; flash: bitstream and app equal the original, the whole config range equals the backup (the marker is gone), no other sector changed |
| C: one byte flipped in the config blob of the `.CFW` | "Package damaged. Nothing written." (the settings sums), flash byte-identical, no backup written |
| D: read-only stick, normal package, YES | "Writing the backup failed", "No USB/SD with 8.1 MB free. Insert one. Retry?", NO -> "No backup, no update. Nothing written."; flash byte-identical, no file on the stick |

Regression: A is the normal-package run through the new updater (blobs found through the manifest, `.data` copied by crt0 to
0x03600000); the updater screen, backup, write and verify behave as on 2026-10-06. Host: `tools/test_trx_pkg.py` (9 tests: normal
package, bundle of a stock and of a stamped bitstream, nothing-to-restore, damaged byte in config/app/bitstream, truncation,
trailing bytes, other updater code, refused flags, softboot's `find_probe`/`find_app` order on a normal package and on a bundle)
OK; `softboot_scan` also on the built `.ue2` (bitstream at 268,396, app at 2,076,368) and on the built bundle.

Not covered: the type-3 refusal and the `.ue2` + NO dry run were not run again with this layout (the code in front of them is
unchanged); "Run as Softboot" itself in the emulator (only its scan order, on the host); a `.BIN` that succeeds followed by a
`.CFW` that fails; the "Update FAILED" path; the header-last order; anything on the real W25Q128 and the real USB timing (the board
item T49-RESTORE in `docs/archive/board-tests/board-tests-pending.md`); a bundle whose flash holds no bitstream/app (host only:
`test_nothing_to_restore`; the updater branch `plan.ok == false` was not run).

**ue2emu** (`--board c64u`, NOR model `flash.rs`): preload a flash image, load the package from a USB image as the file
browser does, run it: both ranges equal the blobs; the header page is written last (if the model logs program order;
to be checked); a model that fails the n-th program makes the updater retry and then report FAILED without
`turn_off`; a model that silently ignores writes (the closed gate's behaviour) ends in "verify failed" with no
write after the first failure.

**Simulation:** the T44 bench (`designs/c64_basic/sim/flash_gate/`) runs the updater's byte sequences (0x20 for every
sector of 0x000000-0x1A5000 and 0x220000-0x3A4000, 0x02 pages, 0x05 polls, 0x03 reads) with the gate open as in M1:
exactly them reach the chip.

**Board, in this order (each stage with Alex at the machine; nothing writes below 0x400000 before B3):**

- B0 read-only: build the package; `trxdbg boot check` equals the manifest's sums; "Run as Softboot" on the package
  accepts it (bitstream equals the flash's).
- B1 dry run on our core: updater, answer **NO**: shows installed/package, writes nothing (compare
  a flash dump before and after).
- B2: updater with a *test package* whose bitstream and app are the installed ones
  (identical bytes, same stamp): measures the write time through the gate and proves verify-after-write. No power-cut test on
  the board.
- B3 real update P1 → P2 (a newer build), full flash dump first, JTAG cable and the `.bit` at hand.
- B4 path (a): needs the stock firmware back in the flash first (JTAG, the dump slice of T15 "Flashed"), then the `.cfw`
  from a stick under Gideon's menu. Alex decides whether and when (Q7 first).

## 9. Open questions for Alex

1. **Scope:** write only bitstream + app (recommended), or also the three `/flash/html` pages (they follow the app's REST
   contract) and/or ROM files? (Flag b0; default off.) **Decided (Alex, 2026-10-03): bitstream + app only.** The web pages move out into an app later (M2).
2. **Brick exposure:** accept the in-place single-slot update for M1 (no tester recovery from a power loss during the
   roughly 25 s bitstream write; JTAG for Alex), A/B or MultiBoot left to M3? **Decided (Alex, 2026-10-03): accepted, same as Gideon's .cfw today (power loss during the write = JTAG). A/B or MultiBoot later (M3).**
3. **R1:** add the trailer check to the boot ROM now (new bitstream before the first package), or ship with header-last only? **Decided (Alex, 2026-10-03): header-last only for M1, no boot-ROM change.** The real safety net comes in M3: a minimal golden stub bitstream at 0x0 (RISC-V + SD/USB + SPI only), always started first, never writable (the gate blocks it even when T50 unlocks), which looks for a firmware file on SD/USB1 and otherwise MultiBoots into the main bitstream further up (TODO T26). The updater forks softboot's package parsing and bitstream compare (`apps/softboot/src/softboot.c`); testers can softboot a new app before flashing when the bitstream is unchanged.
4. **Product guard:** **Final (Alex, 2026-10-06): 50T only, C64U and UE2, no 100T (see the decision block at the top).** Earlier: pre-header product `C64 Ultimate` (strict, recommended) or empty like Gideon's (family only)? **Decided (Alex, 2026-10-03): C64U and UE2 (U64 Elite II) both.** The pre-header accepts both products; the package carries one bitstream per board (the UE2 has another FPGA and pinout — a C64U bitstream would not configure there); the updater identifies the running board and writes only the matching bitstream, and refuses before writing anything if the package has none for it. Today only C64U builds exist, so a UE2 refuses until a UE2 build is added (TODO T53, moved to M1). Gideon's package carries u64e2_50t + u64e2_100t + one ultimate.app (`update_binaries_u64ii.s`); ours follows that layout.
5. **Config pages:** never reset (recommended, keeps the tester's settings), or ask as Gideon's does? **Decided (Alex, 2026-10-03): never wipe the settings; the updater does not touch config or the flash drive.** **Backup first (Alex):** before writing, the updater saves the current bitstream + app (0x000000-0x3FFFFF) and the config pages (0xFE8000-0xFFFFFF) as one file on SD or USB (refuses to update if no medium with ~4.2 MB free), with a checksum. In M1 that file is restored over JTAG; with the M3 golden stub (T26) directly from the medium.
6. **After the update:** power off like Gideon's (recommended), or reconfigure the FPGA at once with
   `W25Q_Flash::reboot` (ICAP warm boot, `w25q_flash.cc` 370; not checked on our core)? **Decided (Alex, 2026-10-03): power off like Gideon's.**
7. **Stock handler:** disassemble the handler in the dump's stock app before the first tester package (only strings
   checked so far), and when may path (a) be tried on the board (it needs the stock firmware flashed back first)? **Answered (Alex, 2026-10-03): no signature; Alex has built many .cfw/.ue2 packages with the stock handler before (1541ultimate project builds). No disassembly needed; path (a) is tested once on the board before testers get it.**
8. **Authenticity:** CRC/CHK only (integrity), or a signature later (the updater would carry a public key; nothing in M1)? **Decided: CRC/CHK only.**
9. **Version policy:** allow downgrade and same-version re-install (tester sees installed vs package and decides)? **Decided (Alex, 2026-10-03): yes; the updater shows installed vs package version and the tester decides.**

## 10. Release pipeline

Alex, 2026-10-08: a pipeline that makes a release for the testers: the updater file and our apps, release notes along
the commits as Markdown, delivered as a zip.

**Removed 2026-11-01 (T100 D15):** `release.yml`, `ci/release.sh` and `tools/release_notes.py` are gone with the app stage; the monolith updater is TRX64-Ultimate's. The text below is history.

**Trigger.** `.gitea/workflows/release.yml`: `workflow_dispatch` (input `ref`, default `main`: what is released) and a
push of a tag `v*`. Its own concurrency group `release` (not `gateware`); the machine's heavy lock still serialises the
heavy stages. Runner as in every workflow (repo variable `CI_RUNNER`, else `dockerbuild`).

**Inputs.** Job `resolve` (`ci/release.sh resolve`) takes the commit of `ref` and looks for the CI package
`trx64-ultimate/<branch>-<8 hex>` (then any version ending in the same 8 hex). Found: it is reused, a file
`release-reuse` in the run's hand-over directory makes the stages `app`, `bit-c64u` and `package` (the same
`workflow_call` files the push pipeline uses, no copy of their logic) skip themselves (`release_reuse_skip` in
`ci/lib.sh`). Not found: the stages build and publish it (the bitstream by the gateware key from `/cache/bit`, the app by
the submodule commit from `/cache/app`, so only a changed input costs a Vivado run). The stages build the branch the run
was started on; `ref` may name another branch only if its package exists (the run says so and stops).

**Job `release`** (`ci/release.sh build`, `u64-build-env:ci`):

1. downloads `c64_basic.bit`, `ultimate.app`, `manifest.json` and the `.rpt` files of that package and checks them
   against the manifest's SHA-256 and the release commit;
2. `tools/make_update.sh --native` (the spec 3.4 build, the compile in place instead of in the `container` tool): the
   manifest id is the package's `<branch>-<8 hex>`; it runs `tools/trx_pkg_check.py` on both files, and the script runs it
   again on the final files;
3. builds every app of `apps/` of the pinned app commit as `.u2a` (RISC-V and Nios II variants, `make` in the app
   directory), except `esp_probe`; the SDK samples (`hello`, `restcheck`) are not under `apps/` and never included;
4. `tools/release_notes.py` writes `RELEASE_NOTES.md` (below), then `SHA256SUMS` over all files;
5. `trx64-<id>.zip` (folder `trx64-<id>/` inside) with `trx64_update.cfw`, `trx64_update.ue2`, the `.u2a` files,
   `RELEASE_NOTES.md`, `SHA256SUMS`. The file names inside are the decided ones (decision block at the top: the id lives
   in the manifest and on the updater screen, not in the file name); the zip carries the id;
6. publishes the generic package `trx64-release/<id>` (zip and each file) and a Gitea Release with the zip and each file
   attached and the notes as its text: a tag run releases the tag (name = tag); a dispatch run makes a prerelease
   `trx64-<id>` (tag `trx64-<id>` on the release commit). A release of the same tag from an earlier run is replaced.

**Apps.** Included: every directory of `apps/` with a Makefile except the exclusion list `EXCLUDE_APPS` in
`ci/release.sh`. Excluded: `esp_probe` (ESP32 bring-up diagnostics), the SDK samples `hello` and `restcheck` (test
programs). At the pinned app commit 6f7ea03a that gives `softboot`, `trex_crt`, `trxmon`, `unzip`. There is no
`c64re` app: it was removed on `trx64/main` (A13, "M1 writes no .c64re") and is not built; a new directory under
`apps/` is picked up without a change here. The notes name what was included and excluded.

**Release notes** (`tools/release_notes.py`, runnable on the Mac: `--id`, `--ref`, `--manifest`, `--base`; test:
`tools/test_release_notes.py`): header (id, date, release, bitstream and app commit, CI run, gateware key, WNS and LUT use
from the package manifest and reports); the install instructions (sections 6.1, 6.2 and 6.4 of this spec, read from the
file); contents with the include and exclude list; the commits since the previous release: the newest `v*` tag reachable
from the release (or `--base`; with neither, the newest 40 commits, and the text says so), first-parent history of the
superproject in three groups (merges, changes, documentation) with task ids in bold, then the app commits between the app
commits the two releases pin; known open issues: every M1 row of `TODO.md` that is not done or dropped, one line from its
Open column.

**Not covered.** Publishing is private to the Gitea (no GitHub). No signature (Q8). A tag push also starts the normal
pipeline (it builds the 100T for tags); the release run reuses its package when that finished first and otherwise builds
the 50T parts itself (caches make the second one cheap).

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

T49: Update packages (.cfw/.ue2 + trxmon.u2a) for internal testers

Milestone M1, status ✅. Open (one line, as in the TODO table): –

Decisions: [ADR-0033 Update packages built now for the internal testers: 50T only, no unlock](../../adr/0033-update-packages-now-50t-no-unlock.md); [ADR-0015 No 100T and no public release; T53 dropped](../../adr/0015-no-100t-no-public-release.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- **2026-10-10 evening, RC1 installed on the board, T49 closed (Alex):** `trx64_update_692f643d_20261010.cfw` from /USB1, Update OK, power cycle; `/v1/info`: core TRX2, app 3c8e4aa (splash merge). Backup `TRXBK_20261010_005.BIN` + `.CFW` (3.6 MB bundle of the previous state).
- **2026-10-10 evening, M1 RC1 released:** tag `m1-rc1` on integ-m1 692f643d (CI run 662, package integ-m1-692f643d, WNS +0.092); release workflow run 663 reused the package and published Gitea Release `trx64-integ-m1-692f643d` and package `trx64-release/integ-m1-692f643d`: `trx64_update_692f643d_20261010.cfw/.ue2`, `trex_crt.u2a`, `trxmon.u2a`, `softboot.u2a`, `unzip.u2a`, RELEASE_NOTES.md, SHA256SUMS, zip. Install on the board: open.
- **2026-10-10 afternoon, T49-RESTORE passed on the board (Alex, checklist §4 adapted).** Update from Gideon's state (trex_main-90f301ce, flashed for the T74 measurement) with `trx64_update_ca3be3dc_20261010.cfw`: Backing up, bundle `TRXBK_20261010_000.CFW` (3.9 MB, Gideon's state) + `.BIN`, Update OK. Restore of that bundle with settings NO: Restore OK, Gideon's state runs, the changed setting (Color Scheme C128 Style) stayed. Update back to ours (bundle `_002` of Gideon's state), setting changed again, restore with settings YES: Restore OK with settings, Gideon's state runs, Color Scheme back to the backup's Ultimate Black (Alex confirmed). Finding: an update started from Gideon's state names/stamps its backup in UTC (`_000` 15:03, `_002` 15:42 at 17:03/17:42 CEST): the time handover runs only on our bitstream, there the updater reads the clock itself, which runs in UTC, and has no TimeZone setting. Started from our state the time is local (`_001` 17:37). Not fixed.
- **2026-10-10, title fixed (Alex, photo of the updater: "TRX64 Ultimate Updater bitte. Nicht TREX64. Und 2 Leerzeichen davor damit es mittig sitzt."):** `update_trx.cc` title now `  ** TRX64 Ultimate Updater **`.
- **2026-10-10, CI builds the update files per commit (Alex: "die .cfw per Pipeline nach Commit auf integ-1 bauen"):** stage `update` of the push pipeline (T31 decision 14) builds `trx64_update_<commit>_<YYYYMMDD>.cfw/.ue2` from the package's own `c64_basic.bit` + `ultimate.app` on integ-m1, main and `v*` tags and adds them to the package version `trx64-ultimate/<branch>-<sha8>` with `SHA256SUMS`. The release workflow uses the same function (`build_update`, `ci/lib.sh`); it also now checks the renamed files (it still looked for `trx64_update.cfw`). `TRX_UPDATE_NAME` is the hook for the release tag in the name.
- **2026-10-10, file names with commit and date (Alex):** tools/make_update.sh writes `trx64_update_<commit>_<YYYYMMDD>.cfw/.ue2`, release_notes.py names them the same way. The backups keep `TRXBKnnn` (the updater has no clock: rtc_dummy).
- **2026-10-10, raw-bitstream restore bundle + verify messages (branches t49-raw-bundle / t49-raw-bundle-app, ue2emu and host only, nothing on the board).** The board's flash held an openFPGALoader image, so the updater printed "No restore bundle" (a gap against ADR-0050). Now a raw image is bundled (§4.3). ue2emu (`--emu-test` build of the package integ-m1-49847451, flash = that `.bit` without header at 0 + its app at 0x220000, `--caps 24E40226`): normal update YES -> "Raw bitstream (no .bit header), 1830764 bytes", `TRXBK000.BIN` + `TRXBK000.CFW` (3,618,856 bytes, **byte-equal to the host twin** `trx_pkg.restore_blobs`, flags 12), flash then holds the `.bit` with header; then `TRXBK000.CFW`, settings NO: "Restore OK", **flash 0x000000-0x3FFFFF byte-identical to the flash before the first update (raw, no header, tail erased again), config and all other sectors identical**; the fresh `TRXBK001.CFW` of that run (flash with `.bit` header) equals the host twin too. Host: `tools/test_trx_pkg.py` 14 tests OK (new: raw bundle with and without AXSS, length rule incl. stale tail byte, detection needs both markers, flag only with matching header, real package bitstream stripped). Verify lines added (§4.3); seen on the emulator screen. Open: the board run of a raw-flash bundle.

- **2026-10-09 night, Alex: B0-B3 need no test** ("Ich hab doch heute/gestern schon geupdated erfolgreich inkl. Backup .. das muss KEINER mehr testen"): updates incl. the raw backup ran on the board. Left: the updater packs the backup into a startable .CFW and that file is started (T49-RESTORE).
- **2026-10-09 night, merged into integ-m1 (app 7a646f65):** restore bundle on the integ line; host tests tools/test_trx_pkg.py 9/9 OK; board tests B0-B3 and T49-RESTORE still open.
- **2026-10-09, restore bundle built (t49-restore-bundle + app t49-restore-bundle-app): `TRXBKnnn.CFW` written beside the `.BIN` before any flash write, byte-checked after writing; the updater finds its blobs through the manifest (`TRXPKG02`, 156 bytes), the image ends at the blobs, `.data` in its own region; running a bundle skips the AXSS match, asks "Restore your settings from the backup?" (default NO), writes the config pages last. ue2emu (emulator only): normal update writes `.BIN` + `.CFW` (the `.CFW` byte-equal to the host expectation), bundle with NO leaves the config byte-identical and restores bitstream and app, with YES the config equals the backup, a damaged `.CFW` and a read-only stick write nothing (§8). Not on the board: item T49-RESTORE.**
- **2026-10-09, decision (Alex): restore bundle `TRXBKnnn.CFW` beside the raw backup, config always included, restore asks (default NO); ADR-0050, decision block at the top.**
- **2026-10-08 afternoon, board B4 PASSED (Alex at the C64U):** flash 0x0-0x3FFFFF restored to Gideon's firmware from the 2026-10-03 dump (JTAG, read-back equal; our previous image saved as tools fork pinmap/runs/flash-dump/ours-before-gideon-2026-10-08.bin), power cycle -> Gideon's core 1.50 / 3.15. Release trx64-main-171bb002 (`trx64_update.cfw`, SHA-256 matches SHA256SUMS) copied to /USB1, File browser -> Run Update from Gideon's firmware -> our updater -> YES -> backup /USB1/TRXBK000.BIN (4,292,684 bytes) -> written -> power cycle -> our core boots from flash (REST: core TRX2, app 6f7ea03 = the release). Alex: "es klappt!". The USB-Blaster was plugged in at power-on and the FPGA still configured from flash. B0-B3 (softboot, dry run, same-version, P1->P2 update on our core) still open.
- **Date and time built 2026-10-10 (t49-time + app t49-time-app, not merged): the app hands its clock to the updater (4 words at 0x03F00000, section 4.4), the updater shows the real date (none instead of 1970 when the app has no valid time) and writes `TRXBK_YYYYMMDD_nnn.BIN/.CFW` (long names, FF_USE_LFN 2). ue2emu (emulator only): date line right, both files with the date. Not on the board; the no-time branch is not run. Open: board with the next T49-RESTORE session.**
- **Release pipeline built 2026-10-08 (t49-release): `.gitea/workflows/release.yml`, `ci/release.sh`, `tools/release_notes.py`, `make_update.sh --native`; section 10. Alex: for the testers one run builds the updater and our apps, writes release notes along the commits and delivers a zip (Gitea Release + generic package). Dispatched on t49-release: run 521 green on a reused CI package (stages skipped, 46 s), run 520 green with the stages building it (bitstream and app from the caches); zip downloaded from the Release, SHA256SUMS and `trx_pkg_check.py` PASS on the .cfw/.ue2; test releases, tags and release packages deleted. Not run: a `v*` tag run (it also starts the full push pipeline) and the package in ue2emu. Open: board stages B0-B4 with Alex.**
- **Built 2026-10-06 (t49-pkg + app t49-update-app): ~/trx64-update/trx64-<id>/ trx64_update.cfw/.ue2 + trxmon.u2a; host checker PASS; ue2emu (test build for its S25FL flash) PASS: type-3 refusal, full update with USB backup byte-exact, corrupt package, read-only stick, .ue2 + NO (spec §8). Open: board stages B0-B4 with Alex.**
- **Alex 2026-10-06: build it now for the internal testers: .cfw + .ue2 + trxmon.u2a, 50T only, for C64U and UE2 (same board and bitstream; the updater refuses FPGA type 3, writes nothing); no T+R+X unlock in the spec for now (gate open in M1). Spec T49 decision block.** Update packages for TREX-internal testers (no JTAG): our .ue2 (and .cfw if Commodore's format is needed) with an updater, taken from Gideon's (target/u64ii/riscv/update, update_u64ii.cc, .ue2 packaging) — writes our bitstream at 0x0 and app at 0x220000 (+ verify), nothing else unless intended. First install runs under Gideon's firmware (no gate); updates on our core need the T44 §8a unlock (T50). Spec: docs/specs/done/T49-update-packages.md (one `.cfw` for both paths, own updater with header-last app write, verify after write; open questions there)
