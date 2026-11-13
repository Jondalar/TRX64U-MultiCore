# T185 — The C128 core starts with the standard ROMs or with JiffyDOS, chosen in its settings

**Status:** BUILT in the code and the Mac tests (2026-11-06), D1-D4 (D4: files selected, never searched); RISC-V build, kit-file layout and S6 (board) open. Status check 2026-11-13: BUILT and merged (b99192658); board 2026-11-11 works, faster; J.3 timing not measured; kit-file layout open.
**Repo:** TRX64U-MultiCore — core app only (`coreapp/cores/c128.cc`, shared drive-ROM logic with `coreapp/cores/c64scpu.cc`); no
gateware change expected (the image goes through the existing ROM window and cache flush, `c128_regs.h` `C128_ROM_*`).
**Number:** T185.
**Origin:** GitHub issue #46 (a tester, 2026-11-06: "Add JiffyDos Rom support to C128"; "A menu to choose the ROM between Standard and
JiffyDos", as the MEGA65's C128 core offers). Alex, 2026-11-06 (translated): "Spec. And since we are still on the C128 core / have untested
changes, on top of it please" — built on the open C128 work (T140 D28, T164 "C128 black") so one board test covers all of it.
**Depends on:** T113 (C128 core, ROM image), T182 D5 (JiffyDOS setting and drive ROM pattern of the SuperCPU core).
**Milestone:** M3.

---

## §1 The problem

A C128 user with a JiffyDOS kit wants the faster serial loads and the JiffyDOS commands. Today the core takes one system ROM image of
72 KiB (`boot0.rom` layout, T113 §5.5: `+0x0000` C64 BASIC + KERNAL, `+0x4000` C128 editor + Z80 BIOS + KERNAL, `+0x8000` C128 BASIC,
`+0x10000` character ROMs), found by name (`c128.cc` RomSpec "C128 system ROM image") or picked in the browser (`c128.load_rom`). There is
one slot: switching to JiffyDOS means replacing the file, and the drive keeps its stock 1541 ROM unless "Set as 1541 ROM" is used by hand.
JiffyDOS needs both sides: the computer's KERNAL and the drive's DOS ROM; one without the other falls back to the slow protocol.

JiffyDOS uses only ATN, CLOCK and DATA (no SRQ), so it fits the contract as it is (T113 F9: no SRQ pin; the 1571 is out).

- **P1 ROM choice:** a setting "Standard" / "JiffyDOS" decides which computer ROMs the machine starts with.
- **P2 Supplying the JiffyDOS ROMs:** the person's own files are found (names, sizes, a content check); nothing is shipped.
- **P3 Drive ROM:** with JiffyDOS, drive A gets the JiffyDOS DOS ROM for its type, when supplied.
- **P4 Missing parts:** JiffyDOS chosen but a file missing gives a clear message and a machine that still starts.
- **P5 No harm to the standard path:** with "Standard" nothing changes against today (image bytes, goldens).

## §2 Scenarios

```gherkin
Scenario: S1 JiffyDOS start
  Given the setting "ROM set" = JiffyDOS and the JiffyDOS files selected by the person (D4)
  When  the machine restarts
  Then  the image in the DDR2 window reads back with the JiffyDOS KERNAL bytes at their offsets (sha256 of each region = the files),
        the cache flush finished, and the C128 start screen shows the JiffyDOS banner
  And   every other region of the image equals the standard image
```

| # | Solves | Scenario | Test | Where |
|---|---|---|---|---|
| S1 | P1 | as above | coreapp test (image assembly, read-back compare) + board photo of the banner | `tests/coreapp/run.sh`, board |
| S2 | P1/P5 | "Standard": image identical to today (sha256), goldens unchanged | coreapp test | `tests/coreapp/run.sh` |
| S3 | P2 | a file of the wrong size or failing the check is refused, the reason shown | coreapp test (planted wrong file — must fail) | `tests/coreapp/run.sh` |
| S4 | P3 | JiffyDOS + JiffyDOS 1541 and 1581 ROMs supplied: drive A's ROM reads back = file for each type; back to Standard: stock ROM again | coreapp test | `tests/coreapp/run.sh` |
| S5 | P4 | JiffyDOS chosen, nothing or no C128 KERNAL selected (or the file gone): the line, machine starts on the standard image | coreapp test | `tests/coreapp/run.sh` |
| S6 | P1-P3 | `LOAD"$",8` and a program load in C128 and C64 mode with JiffyDOS on both sides; time measured against Standard | board (Alex's own ROM files; none in the repository) | board session |

## §3 Decision

- **D1 (Alex, 2026-11-06: "A", Q1):** the person supplies the separate JiffyDOS KERNAL files of the kit; the app overlays them on
  the standard image at their regions. The region sizes and offsets are checked against real kit files before the build.

- **D2 (Alex, 2026-11-06: "Both", Q2):** JiffyDOS DOS ROMs for the 1541 and the 1581; drive A takes the one for its type when
  supplied, the stock ROM otherwise (with the message of P4).

- **D3 (Alex, 2026-11-06: "F5", Q3):** the setting "ROM set" sits on the core's F5 Settings tab, stored in the core's `.cfg`, taking
  effect at the next machine restart.
- **D4 (Alex, 2026-11-06, board, translated: "yes please. And NEVER JiffyDOS or the like as the default at boot... only 'stock', the
  other I have to select and so take it into the config explicitly"):** replaces the automatic search of D1. The kit files are never
  searched or checked by name at start, and nothing is reported about them while none is selected. The person selects each file
  explicitly: in F3 a file action "Use as JiffyDOS C128 / C64 / 1541 / 1581 ROM" (as "Set as 1541 ROM"), checked when chosen; in the
  Settings the row shows the selected file and RETURN opens a file selector (never typed). The paths are stored in the core's .cfg.
  The machine always boots Stock unless "ROM set" = JiffyDOS was chosen and its files are selected; a missing or refused selected
  file falls back to Stock with the message.
- **D5 (Alex, 2026-11-07, translated: "Build on as it is. We can improve the UI with the testers"):** no hint on "ROM set" when
  files are chosen but the set is Standard. Alex's direction for later: the Summary shows the active ROMs; UI refinements come with the testers' feedback.

## §4 Out

- Shipping any JiffyDOS ROM, in the repository, a `.cor` or a release (CLAUDE.md).
- The 1571 and fast serial (T113: "We drop the 1571"); T170 has the real drives.

## §5 Build (branch `t185-c128-jiffydos`, 2026-11-06)

**Files (D4 as built, 2026-11-06).** `coreapp/cores/c128.cc` (ROM set, the four use_jiffy_* operations, `kit_read`, `load_kits`, setting, notice), `coreapp/cores/drive_rom_pick.h`
(the stock/JiffyDOS drive-ROM pick, shared with `c64scpu.cc`; `DriveRomKind::quiet`: the drive says nothing about a missing JiffyDOS file, `load_kits` does), `coreapp/src/desc.h` /
`desc_check.cc` / `regset.cc` / `settings_ops.cc` (Setting kind 3, "a file the person selects": the text is the path, `apply` is the operation that checks and keeps it, a reset never applies it),
`coreapp/src/menuctl.cc` (the file selector: the folder picker in file mode, `pick_set_`, `begin_pick_file`, `pick_take`), `coreapp/src/menutree.cc` (dump of kind 3), `coreapp/src/corereport.cc`,
tests in `tests/coreapp/test_c128.cc` and `run.sh`. The name search is gone: the RomSpecs of the kit files, their names and `romreq_table.inc` lines no longer exist.

**Files the person selects (all optional, anywhere on a medium; nothing is shipped, no name is looked at).**

| Role (setting / F3 action) | Sizes taken | Goes to |
|---|---|---|
| JiffyDOS C128 ROM (`c128.use_jiffy_c128`) | 16,384 (kit U35), or 32,768 (the C128DCR kit's file: the C64 part first) | image +0x4000 (32 KiB: +0x0000..0x7FFF) |
| JiffyDOS C64 ROM (`c128.use_jiffy_c64`) | 16,384 (BASIC + KERNAL), or 8,192 (the KERNAL alone, reset vector $FCE2) | image +0x0000, or +0x2000 for 8,192 |
| JiffyDOS 1541 ROM (`c128.use_jiffy_1541`) | 16,384 or 32,768 (8,192 is refused: the upper half of the ROM only) | drive A as 1541 |
| JiffyDOS 1581 ROM (`c128.use_jiffy_1581`) | 32,768 | drive A as 1581 |

Content check (`kit_fault`, `jf_part_fault`): not all 0x00 / 0xFF; reset vector (last four bytes of the part) >= $E000; the 16 KiB C64 part has "CBMBASIC" at offset 4, the C128 part has not;
the 8 KiB C64 KERNAL has the cold start $FCE2. The DCR file is checked as both parts.

**Behaviour.** The four rows (kind 3, `WHEN_RESET`) are on the F5 Settings tab after "ROM set"; the row shows the file's name or "Not selected", RETURN opens the selector (folders and files, "No file" on top
clears). The same operation runs from the F3 action "Use as JiffyDOS ... ROM" (`.rom`, `.bin`). The operation reads and checks the file when it is chosen; refused, nothing is kept, the selector stays open and the
status line carries the reason with the file's name; kept, the path is in the `.cfg`. "ROM set" is Standard by default and Standard reads no kit file and says nothing (S2). With JiffyDOS the selected files are read
again at the start (`load_kits`: they may be gone or changed): nothing selected -> "ROM set JiffyDOS: no file selected, the machine starts Stock"; C128 part not selected -> standard image, no line (an unselected part simply stays Stock); refused -> standard image and the line;
only the C64 part missing -> the C128 part is overlaid, no line; a selected file that cannot be read -> that part stays Stock, the line gives "cannot open the file".
`std` (the 72 KiB image) is kept whole, the parts at their regions (`jf`), the window gets `std` or `std` with the parts over (`image_for`), only when it differs from what it holds (`place`).
Drive A: the shared pick holds the stock and the JiffyDOS ROM of each type and applies the one the setting and the drive type ask for.

**Tests (builder, `ci/trxapps.sh` in `u64-build-env:ci` with the CI cache, d9833a3eb, 2026-11-07).** "tests/coreapp: 102 test programs passed, 1513 parallel controls (JOBS=14), every negative control failed as it must"; `c128_jf_unselected_quiet` 3 checks red, `cf_dirs` 1 red.

| Control (each red when its code is taken out) | Scenario |
|---|---|
| `c128_jf_search` (a fallback to the old name search), `c128_jf_default` (ROM set JiffyDOS by default), `c128_jf_none_text`, `c128_notice_line`, `c128_jf_unselected_quiet` (an unselected part says nothing) | D4: nothing searched, Stock default, the line only for none selected or a refused file |
| `c128_jf_k8_offset` (8 KiB KERNAL at +0x0000), `c128_jf_offset`, `c128_jf_basic`, `c128_jf_vector`, `c128_jf_k8_vector`, `c128_jf_empty` | S1, S3 overlay and content |
| `c128_jf_size_c128`, `c128_jf_size_c64`, `c128_jf_size_1541`, `c128_jf_refuse_keeps` | S3 sizes, nothing kept |
| `c128_romset_when`, `c128_jf_need_image`, `c128_jf_need_mode` | D3, S5 |
| `c128_drive1541_choice`, `c128_drive1581_choice` | S4 |
| `c128_pick_reason`, `c128_pick_nofile`, `c128_pick_file` | the file selector |

S2 is the unchanged standard-path tests and goldens (`menu_c128.txt` regenerated for the four rows and the `.bin` type).

**Memory.** `C128State` is about 232 KiB (std 72 KiB, jf 32 KiB, four drive buffers of 32 KiB) of the 7.6 MiB FreeRTOS heap; the free heap was not measured.

**RISC-V / host tests / renders.** Same run: "apptests hostapp: PASS", "apptests coreapp: PASS in 916s", "apptests loader: PASS"; render bench "1250 checks, 0 failed". `coreapp.app` 1708476 B, `hostapp.app` 901124 B. The test_newdisk fix keeps the path string alive (it was read after it was freed). Board J.1-J.3 open.

**UX review (2026-11-07).** FAIL on one blocker: the refusal status wrapped under the key bar ("JiffyDOS C128 ROM: kernal8k.bin: 8192 bytes, 16384 (the C128 KERNAL) or 32768 (the C128DCR kit's file) expected"). Every kit status is now one short line ("..., needs 16384 or 32768"; which size is which part stays in the table above), and the render bench checks that the taken and the refused status fit the pane. Later, not blocking: one word for cancel in status and key bar; a hint on "ROM set" when files are chosen but the set is Standard (D5: not now). Run after the fix (cba911a50): "tests/coreapp: 102 test programs passed, 1513 parallel controls", apptests ok, `coreapp.app` 1708252 bytes; the refused status reads "JiffyDOS C128 ROM: kernal8k.bin: 8192 bytes, needs 16384 or 32768" on one line (coordinator opened the picture).

**Open / not verified.**
- The kit file layout (U32 -> +0x0000 or the KERNAL alone at +0x2000, U35 -> +0x4000, DCR 32 KiB) and the content check are not checked against real kit files; the first real file may be refused with its reason shown.
- S6 (board J.1-J.3: select the files, `LOAD"$",8` and a program load, C128 and C64 mode, times against Standard) is not done.
- The 1541 and 1581 JiffyDOS sizes follow the stock ROM sizes, not kit files.

## §6 Proposal and open questions (coordinator, for Alex)

**Proposal:** setting "ROM set" Standard / JiffyDOS on the core's F5 Settings tab (as T182 D5 amended), stored in the core's `.cfg`,
taking effect at the next machine restart (`WHEN_RESET`: the image goes into the window with the core held, then the flush). With
JiffyDOS the app builds the image from the standard image plus the person's JiffyDOS KERNAL files written over their regions; drive A
follows the setting as in the SuperCPU core (`c64scpu.cc` stock / JiffyDOS drive ROM, factored into one shared helper for both cores).

- **Q1 How the JiffyDOS ROMs come in** (answered, D1: (a)): (a) the person supplies the separate JiffyDOS KERNAL files of the kit and the app overlays them on
  the standard image; (b) the person supplies a complete 72 KiB JiffyDOS image (`boot0.rom` layout) as a second file. (a) needs the region
  sizes and offsets of the kit files checked against real files; (b) needs nothing but a second RomSpec.
- **Q2 Drive ROMs** (answered, D2: both): JiffyDOS for the 1541 only, or for the 1581 as well (drive A takes both types).
- **Q3 Placement** (answered, D3: F5): F5 Settings tab (proposal) or the F8 System page next to "System Mode".

Board 2026-11-11 (Alex, c128.cor of branch c128-cpm67; files selected with "Use as JiffyDOS ... ROM" from `/SD/cores/c128/roms/`, ROM set = JiffyDOS,
reset): JiffyDOS works, loads feel faster ("JiffyDOS works, it is 'faster', is my impression", translated). J.1/J.2 PASS by hand; J.3 (timed against
Standard) not measured.
