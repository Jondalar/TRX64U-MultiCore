# T258 — The C128 core takes VICE's six ROM files as its system image, through a generic "parts of an image" in the ROM manager

**Status:** BUILT on `t258-c128-roms` (2026-11-23), not merged; S1-S9 by the units named in §2, S10 by hand (Alex, real VICE files).
**Repo:** TRX64U-MultiCore — the application only: the generic ROM manager (`coreapp/src/desc.h`, `romfind.cc`, `boot.cc`, `desc_check.cc`), the C128 descriptor's data (`coreapp/cores/c128.cc`), the host application's ROM table (`host-app/software/hostapp/`), `tools/gen_roms_txt.py`, the manual. No gateware.
**Number:** T258.
**Origin:** GitHub issue #105 "Enhance ROM import for C128 to also accept Vice rom set" (open, enhancement). Alex, 2026-11-23: build all of it (the research of the same day, `c128_mega65_report.md` §2).
Open (one line, as in the TODO table): Built on t258-c128-roms (11-23); review and builder test open; S10 board (Alex, real VICE files); 8 KiB 1541 half out
**Depends on:** T113 (C128 core, the 72 KiB image, §5.5), T148 (a core reads its ROMs from its own folder, the one-time copy), T185 (JiffyDOS on the C128), T192 D24 / T213 D4 (no per-core code in the generic app).
**Milestone:** M3.

---

## §1 The problem

A C128 user has VICE's ROM folder, not a MiSTer `boot0.rom`. VICE keeps the machine's ROMs as separate files; the core takes one file of exactly 73,728 bytes (`c128.cc` `roms[0]`, `C128_ROM_BYTES`, `put_image` refuses any other size). The user has to join six files by hand first (`tools/c128_rom_image.py` does it, but it is a developer tool for the bench and says so).

The image layout is the core's already (T113 §5.5): C64 BASIC at +0x0000, C64 KERNAL +0x2000, C128 editor / Z80 BIOS / KERNAL +0x4000, C128 BASIC +0x8000 (two 16 KiB halves), character set +0x10000. VICE's file names and sizes are in `tools/c128_rom_image.py` (`PARTS`): `basic64-901226-01.bin` 8192, `kernal64-901227-03.bin` 8192, `kernal-318020-05.bin` 16384, `basiclo-318018-04.bin` 16384, `basichi-318019-04.bin` 16384, `chargen-315079-01.bin` 8192; together 0x12000 bytes. The MEGA65 core takes per-part files at the same offsets (`rompick.c`, `sys_part[]`).

The generic ROM manager has no way to say "this ROM is these files at these offsets": a `RomSpec` is one file of one allowed size, or a member of a variant set (Spectrum, CPC). D24 forbids solving it inside `c128.cc`.

- **P1 Start from the parts:** with VICE's six files in the core's folder the machine starts with the image they make.
- **P2 Refusal, told:** a missing or wrong-sized part is told by its name, size and folder; the machine does not start on a half image.
- **P3 Where the files come from:** the one-time copy into the core's folder (T148) finds the parts in the old places, and does not copy parts beside a whole image.
- **P4 Nothing changes for the single file:** `boot0.rom` and its names behave as before; a whole image wins over parts.
- **P5 What the application tells about ROMs:** the host application's "Cores and ROMs" page and `ROMS.txt` do not call a core with VICE's six files "missing its system ROM"; the manual and the release notes name the files.
- **P6 Generic, no per-core code:** the mechanism is data in the descriptor, checked by `desc_check`; `c128.cc` does not grow (`tools/check_d24.py`).
- **P7 The 1541's 8 KiB JiffyDOS half:** whether the same mechanism takes it (the MEGA65 core does).

## §2 Scenarios

```gherkin
Scenario: S1 the six VICE files start the machine
  Given the six files of the right sizes in the C128's roms folder, no boot0.rom
  When  the core starts
  Then  the DDR2 window holds the six files joined in VICE's order (73,728 bytes), the character set (+0x10000, 8 KiB) is in the VIC's store, the cache is flushed once

Scenario: S2 a whole image wins
  Given the six files and boot0.rom in the folder
  When  the core starts
  Then  the window holds boot0.rom

Scenario: S3 a part is missing
  Given five of the six files
  When  the folder is scanned
  Then  the result is "missing", the sentence names the missing file and says the image needs all its parts; nothing is put

Scenario: S4 a part has the wrong size
  Given a chargen file of 4096 bytes and the other five right
  When  the folder is scanned
  Then  the status is wrong size and the sentence names the file, "4096 bytes" and the 8192 expected

Scenario: S5 nothing is there
  Given an empty folder
  When  the folder is scanned
  Then  the sentence names the image's files and the six VICE files

Scenario: S6 Reload takes parts that arrive later
  Given the machine running on boot0.rom, then boot0.rom removed and the six files put in
  When  "Reload ROMs, reset" runs
  Then  the window holds the six files joined

Scenario: S7 the one-time copy
  Given parts in an old place (roms/<folder> of a medium) and nothing in the core folder
  When  the core folder is prepared
  Then  the parts are copied (verified), and a core folder that already has a whole image copies no part; a folder with all parts copies no whole image

Scenario: S8 the maintenance page
  Given the six files in a C128 folder / five of them / boot0.rom only
  When  the host application counts the core's ROMs
  Then  one of one is there for six files and for boot0.rom, none for five or for a wrong size; the parts are no "need" of their own

Scenario: S9 the descriptor is checked
  Given parts that overlap, reach past the image, carry a put operation, or name no image
  When  desc_check runs
  Then  each is refused with its reason; the C128's descriptor and every other one pass

Scenario: S10 real files, the board
  Given VICE's real six files in cores/c128/roms/ (Alex)
  When  the C128 core starts on the C64U
  Then  it reaches the BASIC 7.0 READY screen as with boot0.rom
```

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 | six files start the machine | `tests/coreapp/test_c128.cc` ("T258" block, S1) | `tests/coreapp/run.sh` (unit `c128`) |
| S2 | P4 | a whole image wins | `test_c128.cc` (S2); `test_romfind.cc` (T258 block, "a whole file wins") | same |
| S3 | P2 | a missing part | `test_c128.cc` (S3); `test_romfind.cc` | same |
| S4 | P2 | a wrong-sized part | `test_c128.cc` (S4); `test_romfind.cc` | same |
| S5 | P2 | nothing there | `test_c128.cc` (S5) | same |
| S6 | P1 | Reload | `test_c128.cc` (S6) | same |
| S7 | P3 | the one-time copy | `tests/coreapp/test_romfind.cc` (T258 block, `rom_import`) | `run.sh` (unit `romfind`) |
| S8 | P5 | maintenance page | `tests/hostapp/test_coremaint.cc` (T258 block); the table `romreq_table.inc` is compared with `gen_romreq` by `run.sh` (`t_romreq`) | `tests/hostapp/run.sh`, `tests/coreapp/run.sh` |
| S9 | P6 | descriptor check; no growth of `c128.cc` | `tests/coreapp/test_desc.cc` (T258 block); `python3 tools/check_d24.py` | `run.sh` (unit `desc`), lint |
| S10 | P1 | real files on the board | by hand: Alex puts VICE's files into `cores/c128/roms/` on the board | board session |

P4 also rests on the existing C128 unit (7,145 checks before this spec, unchanged and green after). P7 has no scenario: it is out (§4, D6).

## §3 Decision

- **D1 One generic mechanism: `RomSpec::into` / `at`** (`coreapp/src/desc.h`). A ROM that is a part of another ROM's image is an ordinary `RomSpec` (its own names, its one size, optional, no put operation) with `into` = the 1-based index of the spec whose image it is part of and `at` = its offset in that image. The image's spec is found when its own file is found, or else when every spec that names it by `into` is found. Chosen over a separate parts table because the existing scan, the name matching, the size check, the "refused file" sentences and the one-time copy then work on a part unchanged, and a core declares one line per part in the array it already has.
- **D2 Scan** (`romfind.cc` `rom_scan`): the parts are scanned first (two passes over the specs), then the image's spec: a whole file wins; else all parts found = `RomResult::composed` (`hit.dir` = the first part's folder, `hit.name` empty, `hit.size` = the image size). When the parts are partly there the sentence is the part's own ("<role>: <name> is missing in <folder>/" or the wrong-size sentence) plus "The <image role> needs all its parts."; when none is there the image's sentence gets "Or its parts: <names>." Parts may come from different folders of the legacy search; in a core's own folder (T148) they are one folder.
- **D3 Put** (`rom_compose`, `boot.cc` `boot_put_roms`): the buffer is zeroed to the image size, each part is read to its offset, the image's `put_op` gets the whole image. The parts have no put operation and are skipped in the loop. The C128's `put_image` still checks the size (73,728), so no core code changed for this.
- **D4 One-time copy** (`rom_import`): a part is copied like any spec, except that parts are not copied while their image is in the core folder or has just been copied, and an image is not copied when all its parts are already there.
- **D5 `desc_check`**: a part has a role, a name, exactly one size, no put operation, not required, no set; its image is a spec that is not itself a part and has one size; the part ends inside it; two parts of one image do not overlap.
- **D6 The 1541's 8 KiB JiffyDOS half does not belong in this mechanism, and is not built.** Read from the code: the 1541 slot (`c128.cc` `roms[1]`, sizes 16384 / 32768) is a whole-ROM slot that `put_drive_rom` hands to the drive; an 8 KiB file there is the upper half only (T185 §3, `kit_size_ok(KIT_1541)` refuses 8192 "the upper half of a 1541 ROM; needs 16384 or 32768"). Taking it needs a base ROM underneath (the stock 1541 ROM, which the core does not hold) and a rule for which half it replaces: an overlay on something else, not a part of an image made from equal files. It would also lift T185's refusal. Not built; question for Alex (§4).
- **D7 The C128's data** (`c128.cc` `roms[]`): six part specs after the three that existed (indexes 0-2 unchanged), VICE's names and sizes from `tools/c128_rom_image.py`; offsets +0x0000, +0x2000, +0x4000, +0x8000, +0xC000, +0x10000. D24: the file stays at its ledger value (1,078 code lines): the six lines are paid for by taking the zero/0xFF scan of `jf_part_fault` from the existing `all_same` (it did the same loop) and by the 1541 spec on one line. No exception line, none asked for.
- **D8 Application tell-tales**: `boot.cc` `boot_start`'s message buffer is 360 bytes (the sentence is longer), `run.cc` `collect_rom_names` lists the parts' names for an image made from parts. The host application's table (`romreq_table.inc`, generated by `tests/coreapp/gen_romreq.cc`) gets the parts as rows with flag 8 and the image's row number in bits 4 and up; `coremaint.cc` counts the image as present when all its parts are, and counts no part as a need; `tools/gen_roms_txt.py`, `tools/release_notes_mc.py` and the manual (11.14.1, 12.2, 12.4.7) name the files.
- **D9 No ROM bytes in the tree.** The tests make their bytes (`pattern()`, fills); VICE's file names are names only.

## §4 Out

- The 1541's 8 KiB JiffyDOS half (D6): not built, to be decided with Alex.
- The MEGA65's other accepted sizes (a 16 KiB C64 KERNAL that is all of ROM1, a 32 KiB C128 BASIC in one file, a function ROM at +0x18000): VICE does not ship them; no part spec for them. A single 32 KiB BASIC would be one more spec with its own size.
- Other VICE name variants (other KERNAL revisions, national character sets): not in `c128_rom_image.py`, not added; the core's folder accepts one name per part. More names are one more name in the part's list.
- The browser action "Load as system ROM image" still takes one 72 KiB file; parts are found in the folder, never selected one by one.
- `cores/c128/mono/core.toml` (`roms = ["boot0.rom"]` and its comment) is not touched: a change there reaches the build keys of the core.
- Other cores using `into` (the C64, the C16): not asked.

## §5 Decisions taken while building

- The part specs sit after the existing three in `roms[]`: tests and `apply_drives` address `roms[0..2]` by position.
- A scan result of an image made from parts has an empty `hit.name`; the three callers that read it (`boot.cc`, `run.cc` `collect_rom_names`, `libboard.cc` `roms_from_flash`) were read; only the first two needed a change (`roms_from_flash` already asks every spec to be found, including the optional drives, so it is false for the C128 as before).
- The sentence buffers were too short for the longer text (`text[360]` in the scan, `msg[360]` in `boot_start`, `shown[420]`); the unit caught the truncation of "needs all its parts" at 300.
- Proof run on the Mac, once (units built by hand with the flags of `tests/coreapp/run.sh`): `test_romfind` 109 checks, `test_c128` 7,162 checks, `test_desc` 2,680 checks, `test_coremaint` 0 failed, `gen_romreq` output equals `romreq_table.inc`, `tools/test_roms_txt.py` OK, `tools/check_d24.py` green.
