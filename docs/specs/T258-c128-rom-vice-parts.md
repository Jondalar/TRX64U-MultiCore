# T258 — The C128 core takes VICE's six ROM files as its system image, through a generic "parts of an image" in the ROM manager

**Status:** BUILT on `t258-c128-roms` and `t258-jiffy8k` (2026-11-23), merged into `c128-integ` with T257 and T170 (not on integration, Alex); the UX review of 2026-11-23 failed with three findings (notice never shown, ROMs value cut, file names wrapped), open with Alex; S1-S9 and S11-S16 by the units named in §2, S10 and S17 by hand (Alex, his own files).
**Repo:** TRX64U-MultiCore — the application only: the generic ROM manager (`coreapp/src/desc.h`, `romfind.cc`, `boot.cc`, `desc_check.cc`), the C128 descriptor's data (`coreapp/cores/c128.cc`), the host application's ROM table (`host-app/software/hostapp/`), `tools/gen_roms_txt.py`, the manual. No gateware.
**Number:** T258.
**Origin:** GitHub issue #105 "Enhance ROM import for C128 to also accept Vice rom set" (open, enhancement). Alex, 2026-11-23: build all of it (the research of the same day, `c128_mega65_report.md` §2).
Open (one line, as in the TODO table): Built (VICE parts, 8 KiB JiffyDOS half); on c128-integ with T257, T170; UX review failed (3); S10, S17 board
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
- **P7 The 1541's 8 KiB JiffyDOS half:** the 8 KiB file of the old 1541 (the upper half, the chip 901229) is taken as the JiffyDOS 1541 ROM; drive A gets the stock ROM's lower 8 KiB with it (the MEGA65 core does the same).

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

Scenario: S11 the 8 KiB half over the stock lower half
  Given a stock 1541 ROM of 16,384 or 32,768 bytes in the core's folder, an 8,192-byte JiffyDOS 1541 file selected, ROM set JiffyDOS
  When  the core starts, and when the setting is switched to Standard and back
  Then  drive A holds 16,384 bytes: the first 8 KiB of the stock 1541 part (the stock bytes 0x0000-0x1FFF, or 0x4000-0x5FFF of a 32 KiB file whose halves differ), then the file; Standard holds the stock ROM again

Scenario: S12 the half without a stock ROM
  Given the 8,192-byte file selected, no stock 1541 ROM in the folder
  When  the core starts with ROM set JiffyDOS
  Then  the notice says the stock 1541 ROM is needed too and where it goes; drive A is left without a ROM and the machine starts

Scenario: S13 a whole 1541 ROM as before
  Given a 16,384- or 32,768-byte JiffyDOS 1541 file selected
  When  the core starts with ROM set JiffyDOS
  Then  drive A holds the file whole, as before this change; a 16,383-byte file and an empty or all-0xFF 8,192-byte file are refused with their reason and nothing is kept

Scenario: S14 the T185 kit layouts as before
  Given the C128, C64 and 1581 kit files of T185
  When  the T185 scenarios run
  Then  every T185 check passes unchanged, except the one that refused 8,192 bytes for the 1541 (it now takes them)

Scenario: S15 the one-time copy
  Given a stock 1541 ROM in an old place
  When  the core folder is prepared
  Then  it is copied as before (S7); the JiffyDOS file is selected, never searched, so nothing of it is copied

Scenario: S16 the maintenance page and ROMS.txt
  Given the C128 descriptor
  When  `gen_romreq` and `gen_roms_txt.py` run
  Then  their output equals the committed `romreq_table.inc` and the test's expectation: no new row, because the JiffyDOS file is selected, not named

Scenario: S17 the board, Alex's own 8 KiB file
  Given Alex's 8 KiB JiffyDOS 1541 file selected, the stock 1541 ROM in cores/c128/roms/
  When  the C128 core starts with ROM set JiffyDOS
  Then  `LOAD"$",8` runs with the JiffyDOS protocol on drive A
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
| S11 | P7 | half over the stock lower half | `test_c128.cc` ("T258 D6" block, S11) | `run.sh` (unit `c128`) |
| S12 | P7 | half without stock | `test_c128.cc` (S12) | same |
| S13 | P7 | whole 1541 ROM as before | `test_c128.cc` (S13; the T185 drive block) | same |
| S14 | P7 | T185 as before | `test_c128.cc` (the T185 block, its 8,192 check changed to "taken") | same |
| S15 | P7 | one-time copy | `test_romfind.cc` (T258 block, unchanged: the 1541 spec's names and sizes did not move) | `run.sh` (unit `romfind`) |
| S16 | P7 | maintenance page, ROMS.txt | `gen_romreq` output against `romreq_table.inc` (`t_romreq`), `tools/test_roms_txt.py` | `run.sh`, lint |
| S17 | P7 | board, Alex's file | by hand | board session |

P4 also rests on the existing C128 unit (7,145 checks before this spec, unchanged and green after). P7 is S11-S17.

## §3 Decision

- **D1 One generic mechanism: `RomSpec::into` / `at`** (`coreapp/src/desc.h`). A ROM that is a part of another ROM's image is an ordinary `RomSpec` (its own names, its one size, optional, no put operation) with `into` = the 1-based index of the spec whose image it is part of and `at` = its offset in that image. The image's spec is found when its own file is found, or else when every spec that names it by `into` is found. Chosen over a separate parts table because the existing scan, the name matching, the size check, the "refused file" sentences and the one-time copy then work on a part unchanged, and a core declares one line per part in the array it already has.
- **D2 Scan** (`romfind.cc` `rom_scan`): the parts are scanned first (two passes over the specs), then the image's spec: a whole file wins; else all parts found = `RomResult::composed` (`hit.dir` = the first part's folder, `hit.name` empty, `hit.size` = the image size). When the parts are partly there the sentence is the part's own ("<role>: <name> is missing in <folder>/" or the wrong-size sentence) plus "The <image role> needs all its parts."; when none is there the image's sentence gets "Or its parts: <names>." Parts may come from different folders of the legacy search; in a core's own folder (T148) they are one folder.
- **D3 Put** (`rom_compose`, `boot.cc` `boot_put_roms`): the buffer is zeroed to the image size, each part is read to its offset, the image's `put_op` gets the whole image. The parts have no put operation and are skipped in the loop. The C128's `put_image` still checks the size (73,728), so no core code changed for this.
- **D4 One-time copy** (`rom_import`): a part is copied like any spec, except that parts are not copied while their image is in the core folder or has just been copied, and an image is not copied when all its parts are already there.
- **D5 `desc_check`**: a part has a role, a name, exactly one size, no put operation, no check function, not required, no set; its image is a spec that is not itself a part, has one size and comes before the part in the list (`rom_import` relies on it); the part ends inside it; two parts of one image do not overlap.
- **D6 The 1541 takes the 8 KiB JiffyDOS half (Alex, 2026-11-23).** Asked whether the 8 KiB JiffyDOS file of the old 1541 (the upper half, which replaces the chip 901229) is taken over the lower half of the stock 1541 ROM, he chose "Yes, in T258". How it is built (§5): the file is one the person selects (T185 D4: the JiffyDOS files are never searched, so it has no names), and the stock lower half is the first 8 KiB of the 1541 part of the stock ROM the drive already holds (`DriveRomPick::stock`, put from `roms[1]`, 16,384 or 32,768 bytes; the drive maps a 32 KiB ROM as the CPU window $8000-$FFFF, so the 1541 part is the last 16 KiB of such a file, `d1541_drive.cc` put_rom; VICE's `dos1541-325302-01+901229-05.bin` among its names). So the composition is a step of the shared drive-ROM pick (`drive_rom_pick.h`, a flag `DriveRomKind::half`), not a part spec of the scan: a scan part needs a name to find, and a "part of a file" in `RomSpec` would have nothing to read it from. T185's refusal of 8,192 bytes for the 1541 (`kit_size_ok(KIT_1541)`) is lifted for this case; every other size rule stays. A whole JiffyDOS ROM and an 8 KiB half cannot meet: one file is selected per role, and its size says which it is.
- **D7 The C128's data** (`c128.cc` `roms[]`): six part specs after the three that existed (indexes 0-2 unchanged), VICE's names and sizes from `tools/c128_rom_image.py`; offsets +0x0000, +0x2000, +0x4000, +0x8000, +0xC000, +0x10000. D24: the file stays at its ledger value (1,078 code lines): the six lines are paid for by taking the zero/0xFF scan of `jf_part_fault` from the existing `all_same` (it did the same loop) and by the 1541 spec on one line. No exception line, none asked for.
- **D8 Application tell-tales**: `boot.cc` `boot_start`'s message buffer is `OP_MSG_MAX` (the sentence is longer), `run.cc` `collect_rom_names` lists the parts' names for an image made from parts. The host application's table (`romreq_table.inc`, generated by `tests/coreapp/gen_romreq.cc`) gets the parts as rows with flag 8 and the image's row number in bits 4 and up; `coremaint.cc` counts the image as present when all its parts are, and counts no part as a need; `tools/gen_roms_txt.py`, `tools/release_notes_mc.py` and the manual (11.14.1, 12.2, 12.4.7) name the files.
- **D9 No ROM bytes in the tree.** The tests make their bytes (`pattern()`, fills); VICE's file names are names only.
- **D10 The warning of D6 stands at its row (Alex, 2026-11-23; UX review finding 1).** When the "JiffyDOS 1541 ROM" row holds the 8 KiB
  half and the stock 1541 ROM is missing, the row's foot note says so while the row is selected: the half needs the stock 1541 ROM, and
  where to put it. Today the warning is set when the menu opens (`menuctl.cc:548`) and no screen draws it. Asked where it should
  appear (the foot of the row, a status line on Home, or a toast when the menu opens), Alex chose the foot of the row.
  The row's note needs the C128's own drive-ROM state (`c128.cc`): one line of per-core C, a D24 ledger exception (Alex,
  2026-11-23, asked between the exception and moving the drive-ROM choice into a generic module: "Exception +1 line").
- **D11 F8's "ROMs" value says "VICE parts (6)" for an image built from parts (Alex, 2026-11-23; UX review finding 2).** A single
  file keeps its name. Today the value joins the six part names (`run.cc:376-392`), and the pane cuts it in the middle and at the edge, so
  the Character ROM part is lost. Asked between the short value, the six names wrapped over three lines, and the short names on one
  line, Alex chose "VICE parts (6)".
- **D12 The "ROM files" message lists the parts one name per line (Alex, 2026-11-23; UX review finding 3).** The sentence ends with
  "Or its parts:", and each part's file name stands on its own line, so no name is broken. Today the label wraps the names at their
  hyphens (`romfind.cc:270-290` through `ui_message`, `ui.cc:1193`). Asked between one name per line and no names in the message (the
  manual lists them), Alex chose one name per line.

## §4 Out

- The MEGA65's other accepted sizes (a 16 KiB C64 KERNAL that is all of ROM1, a 32 KiB C128 BASIC in one file, a function ROM at +0x18000): VICE does not ship them; no part spec for them. A single 32 KiB BASIC would be one more spec with its own size.
- Other VICE name variants (other KERNAL revisions, national character sets): not in `c128_rom_image.py`, not added; the core's folder accepts one name per part. More names are one more name in the part's list.
- The browser action "Load as system ROM image" still takes one 72 KiB file; parts are found in the folder, never selected one by one.
- `cores/c128/mono/core.toml`: `roms = ["boot0.rom"]` is not touched. Its comment was changed to name VICE's six files (commit d8c6cec3); `ci/code_hash.py` normalises TOML comments, so no build key moves.
- Other cores using `into` (the C64, the C16): not asked.

- A stock 1541 ROM that is only the 8 KiB lower half (`325302-01`) as its own file: not built (the stock slot takes whole 16 or 32 KiB files only). Alex, 2026-11-23, asked whether it may stand here as not built, chose "Yes, out": until a tester asks for it.

## §5 Decisions taken while building

- The part specs sit after the existing three in `roms[]`: tests and `apply_drives` address `roms[0..2]` by position.
- A scan result of an image made from parts has an empty `hit.name`; the three callers that read it (`boot.cc`, `run.cc` `collect_rom_names`, `libboard.cc` `roms_from_flash`) were read; only the first two needed a change (`roms_from_flash` already asks every spec to be found, including the optional drives, so it is false for the C128 as before).
- The sentence buffers were too short for the longer text (`text[360]` in the scan, `msg[360]` in `boot_start`, `shown[420]`); the unit caught the truncation of "needs all its parts" at 300.
- **Review fixes (2026-11-23).**
  - **Truncation: the buffers were raised, the sentence was not shortened.** The sentence of a C128 with an empty folder is about 290 characters (the folder's sentence about 130, the six names about 155), and the names are the information. `OP_MSG_MAX` (`ops.h`) went from 240 to 400, so the sentence arrives in `machine.reload_roms` (REST, menu), `boot_put_builtin_rom`, the status line and the REST/RPC replies; `menuctl.cc` `run_task` / `run_fixed` (`msg[240]` -> `OP_MSG_MAX`) and the two `char s[240]` that copy `r.msg` follow it, `boot_start`'s `msg` is `OP_MSG_MAX` and `shown` `OP_MSG_MAX + 160`. Cost on the RISC-V board: 160 bytes more in every `OpResult` and every `OP_MSG_MAX` buffer on a task stack (`menuctl.cc` two, `c128.cc` four, `drive_rom_pick.h` one, `screens.cc` one, `rpccontract.cc` one), 320 more in each of the two REST buffers, 160 in `MenuCtl::status_` and in `g_hint_text`; no stack size was measured or changed (no RISC-V build was asked for), the coordinator's RISC-V build and the board show whether a task stack is too small.
  - **The developer start** (no core folder: the list of roots, up to 200 characters) would exceed 400. `make_text` drops the sentences of the base text from the end ("Put it in roms/..." first, then "Looked in ...") until the parts' hint fits whole.
  - **Joins:** a missing-file sentence ends with the folder's "/"; every join now ends the sentence with a full stop first. A part that is missing says "The <role> needs all its parts. Put it there, or use one <name>."; a part that is refused says "... needs all its parts, or use one <name> instead."
  - `boot.cc` texts are sentences ("cannot read the parts of the image in <folder>", "made from its parts in <folder>"). The role of the character part is "Character ROM" (one name with the manual); `romreq_table.inc` follows.
  - `rom_import` stops looking for the image as soon as its last part has been copied; before, a stale whole file of a later folder was copied over the parts. `desc_check` now also refuses a part with a check function (`rom_compose` ignores it) and an image that comes after its part. `c128.cc` `all_same` returns true for `n == 0` (ledger 1,074 -> 1,073).
  - Proof: `test_c128` (the buffer of `OP_MSG_MAX`, Reload and the developer start), `test_romfind`, `test_desc`; `test_keywalk` golden: only the status line of the C128 (the new sentence) changed, 22 lines, `tests/coreapp/golden/keywalk.txt`.
- Proof run on the Mac, once (units built by hand with the flags of `tests/coreapp/run.sh`): `test_romfind` 109 checks, `test_c128` 7,162 checks, `test_desc` 2,680 checks, `test_coremaint` 0 failed, `gen_romreq` output equals `romreq_table.inc`, `tools/test_roms_txt.py` OK, `tools/check_d24.py` green.
- **D6 as built.** `drive_rom_pick.h` `drive_pick_apply`: with `DriveRomKind::half` (the C128's 1541) and a JiffyDOS file of 8,192 bytes, the drive gets `rom_half_over` (`coreapp/src/rom_pick_util.h`): the first 8 KiB of the 1541 part of the stock ROM (`stock + n_stock - 16384`: the whole file when it has 16 KiB, its upper 16 KiB when it has 32 KiB), then the file, 16,384 bytes; the result lies in `DriveRomPick::jiffy_rom` behind the half (offset 8,192), so the header holds no buffer of its own (the first review's `static` array in an inline function gave every translation unit a 16 KiB copy). The stock ROM may come before or after the file and may change: `applied` is reset by every put, so the composition follows. Without a stock ROM the notice reads "JiffyDOS 1541 ROM needs the stock ROM too. Put it in roms/c128 on a USB stick or SD card." (the folder is the core's `desc->folder`, the wording of the scan's own sentences) and the drive keeps no ROM (the pick's own line, shown although the C128's kind is `quiet`: `quiet` silences "not supplied", not this). `c128.cc`: `kit_size_ok(KIT_1541)` takes 8,192; the special refusal text in `kit_size_why` is gone, the list reads "needs 8192, 16384 or 32768"; the 8 KiB file passes `kit_fault`'s `all_same` (empty or all 0xFF refused) and no reset-vector check (a real 901229 half was not at hand to prove one). The scan, `RomSpec` and `romreq_table.inc` did not change: the JiffyDOS file has no names (T185 D4), so S15 and S16 show no new row. D24: `c128.cc` shrank (1,078 to 1,073, the last step with the `all_same` guard), `drive_rom_pick.h` shrank (98 to 92: its SHA helper moved to `rom_pick_util.h` in `coreapp/src`); both ledger values lowered, no exception line. `rom_half_over` and `rom_sha_hex` live in the generic `coreapp/src/rom_pick_util.h` because both are generic: a half laid over a base ROM, and a SHA text of a byte range; neither knows a core or a drive type. The notice line `CoreCtx::notice` is `char[200]` and cuts the C128's ROM sentence at 199 characters (the sentence itself may reach `OP_MSG_MAX` 400, the status line of the operation carries it whole); acceptable because the kit sentences are short and the part that matters, the file and the reason, comes first; not raised, as every `CoreCtx` of every core would grow. The part-of-a-file extension of `RomSpec` that the brief suggested was not built: nothing scanned needs it.
- Proof run on the Mac, once, flags of `tests/coreapp/run.sh`: `test_c128` 7,207 checks 0 failed (S11-S14: 16 and 32 KiB stock, Standard and back, the stock arriving later, a whole 16 KiB file, refusals), `test_romfind` 113, `test_desc` 2,680, `test_vc20` 225 and `test_c64scpu` 379 (the pick is shared), `test_coremaint` 0 failed, `tools/test_roms_txt.py` OK, `check_d24.py`, `check_todo.py`, `core_toml.py --check` green.
- **D10-D12 built (2026-11-23).**
  - **D10.** A generic field `Setting::note_state` (`coreapp/src/desc.h`): the name of a state; while it has a text, the foot note of that row (`MenuCtl::row`, `EK_SETTING`, kind 3; read by `sel_detail`, so it shows while the row is selected) says it in place of the selected file's path. The C128's "JiffyDOS 1541 ROM" row names the state `c128.jf1541_note`; its text comes from `rom_half_note` (`coreapp/src/rom_pick_util.h`): "The half needs the stock 1541 ROM too: put it in roms/c128 (USB stick or SD card)" - 81 characters, shorter than the longest foot already in use (`menuctl.cc` "What is mounted now, in the core's media folder - DEL deletes, RETURN saves, <- cancels", 87), empty while the stock ROM is there or the file is not the half. D24: `c128.cc` stays at its ledger value (one line in the settings row, one line that registers the state beside `c128.auto_diag`), `drive_rom_pick.h` did not grow (the text lives in `rom_pick_util.h`, which no ledger counts); `check_d24.py` green, no exception.
  - **The notice at menu open (`menuctl.cc:548`) stays.** It is the generic path for every notice (`mediamounts.cc`, `boot.cc`, `u64cfg.cc`, `winslot.cc`, `writeback.cc`, `newdisk.cc`, `trx64u_ops.cc`, `run.cc`, `c128.cc`): they set `c->notice_pending`, so removing it would drop their statuses too. The half's warning in `drive_pick_apply` stays as well: it is the reply of the operation (REST, `boot.cc` log) and the console line; only the screen was missing, and the row now has it.
  - Proof: `test_c128` (S12: the state reads the sentence while the stock ROM is missing and is empty after it arrives; length under 100).
  - **D11.** `run.cc` `collect_rom_names` counts the parts of a composed image (the parts found while their image is composed) and appends one value "VICE parts (n)" after the names of the files that are not parts; n is the count of parts actually used (6 for the C128). A whole file, or any other core's ROM, keeps its file name. The function is in the board's main file, which no unit builds: the proof is the RISC-V build and the UX re-check of the F8 System page (the coordinator's); `check_d24.py` green.
  - **D12.** `romfind.cc`: the hint of an image that can be made from parts is " Or its parts:" and one "\n" + file name per part, no commas, no closing full stop; the text ends with the last name. Where the text goes: `ui->message("ROM files", ...)` (`boot.cc:183`, a wrapping label: the newlines break lines, as the "\n\nPress RUN/STOP" already does); the console line of `ui_message` (`ui.cc`) prints it as several log lines; the REST/RPC replies (`machine.reload_roms`) carry it as a JSON string, whose escaping covers control characters (`monsrv_json_esc`); `MenuCtl::set_status` (the status line, one row) turns each newline into a space, so the status reads "Or its parts: a b c ...". `keywalk.txt`: the 22 C128 lines moved (the status text), no other core.
  - Proof: `test_c128` (S5, S5b: the sentence ends in "Or its parts:" plus the six names, each after a "\n", no comma in the part list, the text ends with the last name), `test_romfind`, `test_keywalk` (golden regenerated, C128 lines only). Pass lines of the last run: `test c128: PASS`, `test c128pic: PASS`, `test keywalk: PASS`, `test corereport: PASS`, `test menuctl: PASS`, `test romfind: PASS`; `tests/coreapp: 49 test programs passed` (cross) and `36` (shared), every negative control failed as it must.
