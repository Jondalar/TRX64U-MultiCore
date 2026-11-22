# T254 — A foreign firmware shows the name and version it gives itself, not its bitstream's date

**Status:** IN PROGRESS (2026-11-22). Asked by Alex, 2026-11-22.
Open (one line, as in the TODO table): IN PROGRESS 11-22: S1-S8 PASS; D7, D8 in PR #132 (t254-old-entries); open: merge, board
**Repo:** TRX64U-MultiCore — the host application (`host-app/software/hostapp`).
**Number:** T254.
**Origin:** Alex, 2026-11-22 (translated): "Can we take something small into 0.2.1 that bothers me visually a lot? ... If it
is a foreign firmware, so ue2 or cfw, packed as a core or not: then we show the date of the bitstreams as the version, not the
version in the firmware package. It is in there, and Gideon's UI shows 3.15 or 1.2.1." On the proposal "the host uses the same
rule as the backup": "Yes, put that in too."
**Depends on:** nothing.
**Milestone:** M2 (release 0.2.1).

---

## §1 The problem

- **Who:** a person with a vendor firmware file (`.cfw`, `.ue2`), or with a `.cor` packaged from one, in the host's File
  Browser and boot menu.
- **What happens today:** the host names such a file by the `.bit` header of its bitstream: "<design> <date> <time>".
  - The File Browser panel has the title `u64_mk2_artix 2026/09/29 20:08:54` for Commodore 1.2.1. Its Version row says "full
    firmware" (`host_core_rows.cc`, `panel_full`).
  - A adds the boot menu entry with that name (`host_core_flow_fileops.cc`, `hc_fo_browse_add`).
  - S asks "u64_mk2_artix 2026/09/29 20:08:54 replaces the host." (`host_core_ui.cc`, `HCA_BROWSE_START`).
  - P writes that name and the version "bit 2026/09/29" into the manifest (`fwconvert.cc`, `fwcv_names`). The table of
    official files (`known_fw.h`) has Commodore 1.1.0 and Gideon 3.12a, 3.14, 3.14d. It does not have 1.2.1 or 3.15.
- **What the file holds:** the version report of the application has the line `Software version:   <version>`. Measured
  2026-11-22 in Commodore 1.1.0 and 1.2.1, Gideon 3.15a and TRX64-Ultimate 0.3.1: the line is 249 to 254 KB before the end
  of the application. The text "TRX64 Ultimate" is only in TRX64-Ultimate's application, 353 and 389 KB before its end. The
  backup reads the same line for its name (`update_trx.cc`, `fw_version`): "Commodore 1.2.1", "Gideon 3.15".
- **The parts:**
  - **P1** the reading: the name and the version from the application.
  - **P2** the File Browser panel: the title and the Version row. This includes a `.cor` packaged before T254 (its manifest
    version is "bit <date>").
  - **P3** A: the name of the entry, and its icon. The boot menu picks the icon of a firmware entry by the entry's name
    (`cfwicon.cc`, `cfw_icon_kind`).
  - **P4** S: the start question.
  - **P5** P: the name and the version in the manifest.
  - **P7** what was made before T254: the boot menu entry keeps the `.bit` header's name (T198 D17: A again refreshes only the
    hash), and a `.cor` packaged then shows its manifest version "bit <date>" in the boot menu's Version column (§4 until D7, D8).
  - **P6** the cost: the host reads its SD card at 12.5 MHz (T149 D5). The File Browser reads the selected row when the
    selection moves. An application has 1.0 to 1.7 MB.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 (P1) the four measured files
  Given the files Commodore 1.1.0 (.cor), Gideon 3.15a (.cor), TRX64-Ultimate 0.3.1 (.cfw) and Commodore 1.2.1 (.cfw)
  When the host's reader reads them
  Then it names them "Commodore 1.1.0", "Gideon 3.15", "TRX64 Ultimate 0.3.1" and "Commodore 1.2.1"

Scenario: S2 (P2) the File Browser panel
  Given a stick with a Gideon-like .cfw (3.15), a TRX64-Ultimate-like .ue2 (0.3.1), a .cfw without the line and a .cor packaged before T254 (1.2.1)
  When the File Browser selects each of them
  Then the panel shows "Gideon 3.15", "TRX64 Ultimate 0.3.1", the .bit header's name, and "Commodore 1.2.1", each with its version

Scenario: S3 (P3) A
  Given the Gideon-like .cfw selected in the File Browser
  When A adds it to the boot menu
  Then boot.cfg names the entry "Gideon 3.15", and the entry has the icon of Gideon's firmware

Scenario: S4 (P4) S
  Given the Gideon-like .cfw selected in the File Browser
  When S is pressed
  Then the question says "Gideon 3.15 replaces the host."

Scenario: S5 (P5) P
  Given the Gideon-like .cfw, which the table of official files does not have
  When P packages it
  Then the manifest of the .cor has the name "Gideon 3.15" and the version "3.15"; a file of the table keeps the table's name

Scenario: S6 (P6) the cost is bounded
  Given an application without the TRX64 text
  When the host reads it
  Then it reads only the last 512 KiB of the application

Scenario: S7 (P7) A on an entry from before T254
  Given entries that A added under the .bit header's name: a .cfw and a packaged .cor whose application had no version line then
  When the files carry the version line ("3.14", "1.1.0") and A adds each of them again
  Then the entries take the names "Gideon 3.14" and "Commodore 1.1.0"; the other entries keep theirs

Scenario: S8 (P7) the Version column of a firmware packaged without its version
  Given the entry of a .cor whose manifest version is "bit 2026/10/25"
  When the boot menu shows it
  Then its Version column says "packaged firmware"
```

| # | Solves | Test | Where it runs |
|---|---|---|---|
| S1 | P1 | a scratch reader over `cfw.cc` with the four files (the files are not in the repository) | Mac, by hand |
| S2 | P2 | `tests/hostapp/emu_t254_fwname.sh`: pictures `t254-1` to `t254-4`, `t254-8` | builder (ue2emu) |
| S3 | P3 | the same script: the `boot.cfg` line; `tests/hostapp/test_cfwicon.cc` | builder; host tests |
| S4 | P4 | the same script: picture `t254-5-start-ask` | builder (ue2emu) |
| S5 | P5 | the same script: the manifest; `tests/hostapp/test_fwconvert.cc` (the table first) | builder; host tests |
| S6 | P6 | `tests/hostapp/test_cfw.cc` `s_self` (the reads of the scan) | host tests |
| S7 | P7 | `tests/hostapp/emu_t254_fwname.sh` run 2: the `boot.cfg` lines; `tests/hostapp/test_hostcore.cc` (D7: by the path; another name stays) | builder (ue2emu); host tests |
| S8 | P7 | the same script: pictures `t254-9-menu` (run 1) and `t254-10-renamed` (run 2) | builder (ue2emu) |

## §3 Decision

- **D1 (Alex, 2026-11-22, "Yes, put that in too"):** the rule of the backup. The line `Software version:` in the application
  gives the version. The first digit of the version gives the vendor: 1 is "Commodore", 3 is "Gideon", any other is
  "Firmware". The text "TRX64 Ultimate" in the application gives "TRX64 Ultimate" (the proposal Alex accepted: "TRX64 is
  recognised by the text 'TRX64 Ultimate' in the application"). The name is "<vendor> <version>". A file without the line
  keeps today's names.
- **D1a (the code review, 2026-11-22):** a bitstream of the design `top` is a TRX64 build (the rule of `cfw_icon_kind`).
  Without the text "TRX64 Ultimate" in its application, its line is the version of the Gideon base it was built on.
  TRX64-Ultimate RC1 (`692f643d`) says "Software version:   3.15" and has the text only in its updater: D1 alone names it
  "Gideon 3.15". Such a file keeps today's name ("top 2026/10/10 18:23:21").
- **D2:** the places are the File Browser panel (title; Version row "3.15, full firmware"), the name of the entry that A adds,
  the start question of S, and the manifest that P writes. The table of official files stays first for P. The Version row of
  a backup and of a large core of ours stays as it was ("backup of this board", "full firmware").
- **D3:** the reader reads only the last 512 KiB of the application, in 16 KiB pieces. This covers the measured distances of
  §1 with margin. A line further from the end is not found: the host then shows the `.bit` header's name, as today. Reason:
  P6. The whole application costs 1 s or more for each selected row.
- **D4:** the boot menu does not read the file at start: the icon of a firmware entry comes from the entry's name. The first
  word of the new names picks the icon that the `.bit` header picked before: "Gideon 3." and "Commodore 1." give Gideon's
  icon (Commodore's bitstreams have the design `u64_mk2_artix` too), "TRX64 Ultimate <digit>" gives the TRX64 icon. Only
  this shape counts, so the name of a large core ("Commodore 128") keeps the generic icon when its `.cor` has no icon.
  A firmware named "Firmware <version>" (D1) gets the generic icon.
- **D5:** the boot menu reads nothing new and `boot.cfg` does not change. The Version column of a `.cfw` entry stays "full
  firmware": the entry's name has the version.
- **D6:** A on a file that is in the menu keeps the entry's name (T198 D17). Its note names the entry as the menu shows it.
- **D7 (Alex, 2026-11-22, on the proposal for §4, translated: "That is good, add it too, please."):** A on a firmware (a `.cfw`, a
  `.ue2` or a packaged `.cor`) whose entry still has the name A gave it before T254, the `.bit` header's name of that file
  (`CfwInfo.name`), gives the entry the name of D1. Any other name is the person's and stays (D6). Only A does it; R in the boot
  menu keeps the name, as before. The rename and the refresh are one save of `boot.cfg` (`hc_entry_add`, `old_name`).
- **D8 (the same answer):** an entry of a full image whose version is "bit <date>" shows "packaged firmware" in the boot
  menu's Version column, as a `.cfw` shows "full firmware" (D5). `fwconvert.cc` writes "bit <date>" for a firmware without its
  version: one packaged before T254, or one whose application has no version line. The boot menu reads nothing new. The width,
  with the glyphs of `lv_font_montserrat_14`: 143 px with the margin; the column has 150 px, on the compact canvas (480p) 96 px,
  where the text is cut with "..." (ux-review R5).

## §4 Out of scope

- **Decided with Alex (2026-11-22): D7, D8.** Until then this section held: an entry added before T254 keeps its name, and a
  `.cor` packaged before T254 shows "bit <date>" in the boot menu. The manifest of such a `.cor` still says "bit <date>" until P
  packages the file again; only the Version column of the boot menu reads it differently.

## §5 Built

- **2026-11-22, branch `t254-fw-self-name`:** `cfw.cc` `cfw_read_self` (D1, D3) and `cfw_title`; `host_core_rows.cc` (the
  panel, P2); `host_core_flow_fileops.cc` (A, P3); `host_core_ui.cc` (S, P4); `host_core_flow_convert.cc` and `fwconvert.cc`
  (P, P5); `cfwicon.cc` (D4). Tests: `test_cfw.cc` `s_self`, `test_cfwicon.cc`, `test_fwconvert.cc`; the emulator script
  `tests/hostapp/emu_t254_fwname.sh`.
- **S1 PASS (2026-11-22):** a scratch reader (`cfw_probe`, `cfw_read_self` of the branch) over the four files: "Commodore
  1.1.0", "Gideon 3.15", "TRX64 Ultimate 0.3.1", "Commodore 1.2.1".
- **S6 PASS (2026-11-22):** `test_cfw`: "41065 checks, 0 failed". The four files: 32 reads of 16 KiB each (512 KiB); the
  TRX64-Ultimate file 17 reads (both texts found).
- **The units (2026-11-22, on the Mac):** `test_cfwicon` "43 checks, 0 failed"; `tests/hostapp/run_fwconvert.sh` "test_fwconvert:
  0 failed", "run_fwconvert: ok".
- **S2 to S5 PASS (2026-11-22):** `tests/hostapp/emu_t254_fwname.sh` on the builder with this branch's `hostapp_emu.elf`
  (`tools/dev.sh riscv`, rc 0) and the emulator kit of the CI cache: "PASS T254 S3: A added the entry as 'Gideon 3.15'",
  "PASS T254 S5: P wrote a_gideon.cor with the name 'Gideon 3.15' and the version '3.15'", "PASS T254 pictures: nine
  different screens". The pictures (kept outside the repository): `t254-1` the panel "Gideon 3.15", Version "3.15, full
  firmware"; `t254-2` "TRX64 Ultimate 0.3.1", "0.3.1, full firmware"; `t254-3` the file without the line: "u64_mk2_artix
  2026/10/25 12:00:00", "full firmware", as before; `t254-4` the package of before T254: "Commodore 1.2.1", "1.2.1, packaged
  firmware"; `t254-5` "Gideon 3.15 replaces the host. The way back is to switch the power off and on."; `t254-8` the .cor that
  P wrote: "Gideon 3.15", "3.15, packaged firmware"; `t254-9` the boot menu: "Gideon 3.15" with Gideon's icon, "full
  firmware". The test of S3's icon: `test_cfwicon` (above).
- **The emulator flow's first run (2026-11-22):** after P the list starts at `..` again; picture 8 showed the `.cfw`. The flow
  now presses DOWN twice; the second run is the one above.
- **UX review PASS (2026-11-22, the `ux-specialist` agent over the nine pictures):** the titles, the Version row and the
  question fit and name the same thing. Low findings: the title and the Version row both say the version (D2 keeps the
  version in its row); a file without the line still has the `.bit` header's title (D1: as before). The pictures of the
  icons of a TRX64 and a Commodore entry were missing: the flow now adds `b_trx.ue2` and a Commodore-like `e_c121.cfw` too.
  Its third run: "PASS T254 S3: A added the entries as 'Gideon 3.15', 'TRX64 Ultimate 0.3.1', 'Commodore 1.2.1'"; `t254-9`
  shows them with Gideon's icon, the TRX64 icon and Gideon's icon. Not checked: other screen modes (the emulator image on
  the builder refuses `--screen`; the host's canvas is 960x540 in every mode).
- **Seen in the UX review, not part of T254 (a proposal for Alex):** the panel of a full firmware takes the rows of a
  `.cor` ("Core: 10181 B bitstream", "SHA-256: map gideon_bl4", "Contract: none, it replaces the host"), and the FPGA row
  spells the chip two ways (`7a50tfgg484` in a `.cfw`, `xc7a50t` in a `.cor`).
- **Code review (2026-11-22, one review agent over `integration...HEAD`):**
  - **TRX64-Ultimate RC1 named "Gideon 3.15"** (the agent read `trx64_update_692f643d_20261010.cfw` and `.ue2`): fixed by
    D1a. Test: `s_self` plants RC1's line without the text into the RC1 layout (`trx64u()`).
  - **The render bench** (`tests/hostapp/render/render.cc`) expected "Version=packaged firmware" for a `.cor` packaged from a
    file of the table (version 9.9). D2 shows "9.9, packaged firmware": the expectation follows. The bench was not run (no
    benches before a merge, Alex 2026-11-15); it runs at night.
  - **The icon by name was too wide** ("Commodore 128" without an icon record got Gideon's icon): D4 now takes the exact
    shape. `test_cfwicon` checks "Commodore 128", "Commodore PET", "Commodore 264 (C16, Plus/4)", "TRX64 Ultimate (Gideon
    app)".
  - **A on an entry added before T254** said "Gideon 3.15 is in the boot menu" while the row kept its old name: D6.
  - **R (restore):** no change. A `.cfw` is restored only when the table has it (its name is the table's). A backup has
    the name of its manifest. Every other file is refused before a name is shown (`host_core_flow_restore.cc`).
  - Clean: the scan's edges (the agent swept the key and the text around every 16 KiB edge, 124,225 checks), bounds, the
    static buffer (only the UI task calls it), stale state of the panel, the A and S paths (REST too), the convert flow,
    formats, the D24 and public-tree checks.
- **After the fixes (2026-11-22, on the Mac):** `test_cfw` "41066 checks, 0 failed"; `test_cfwicon` "46 checks, 0 failed";
  `run_fwconvert` ok. The scratch reader: the four files as in S1; RC1 (`.cfw` and `.ue2`) keeps "top 2026/10/10
  18:23:21".
- **On the builder after the fixes (2026-11-22):** `tools/dev.sh riscv` rc 0. The fast tier with the CI cache: "apptests ok
  (tier fast)"; hostapp ran again: `test_cfw` "41066 checks, 0 failed", `test_cfwicon` "46 checks, 0 failed", `test_fwconvert`
  "0 failed". `tests/hostapp/emu_t254_fwname.sh`: S3, S5 and the pictures PASS as above. `tests/hostapp/emu_host.sh` ran
  before the fixes: "tests/hostapp/emu_host: all scenarios passed" (seven flows).
- **How these ran (2026-11-22):** `tools/dev.sh` mounts `/srv/trx64-build/cache` as `/cache`. On the builder that folder
  has been empty since 2026-11-21: `dev.sh test` gives "no key" for every suite, and `dev.sh emu` skips the host's
  emulator flows ("no emulator kit"). The runs above used the script `dev.sh` writes, with the builder's cache mounted by
  hand, and `UE2EMU_KIT` set for the emulator. Nothing on the builder was changed.
- **Pull request (2026-11-22):** #130 into `integration`, branch `t254-fw-self-name` (`2210942a`).
- **Merged (2026-11-22):** Alex merged #130 into `integration` (`5d1bf662`). The branch is deleted; the maintainer's
  checkout keeps its head as `refs/backup/t254-fw-self-name`.
- **The integration run of the merge (2026-11-22, 45009429281):** the package is built; `trx64host_update_5d1bf662.txu`
  holds the host bitstream of `d68f4b3a` byte for byte (`txu_pack.py inspect`: host key `5e987948`, the same SHA-256) and
  the new application. The run is red in one check of the core application's emulator flows ("FAIL freeze: 0000a9 0000c8
  0000c8 0000c8 000123"): the sixth sample's log line began with "@" (`@>R:00a0  00 01 7d`), and the check
  (`tests/coreapp/emu/run.sh`, `awk '/^>R:00a0/'`) counted five lines. The six values show the machine running, frozen
  and running again. T254 does not touch the core application. The host's emulator flows passed.
- **On the board's SD (2026-11-22):** `/SD/txu/trx64host_update_5d1bf662.txu` (`tools/board_put.sh`, the `.txu` only;
  the cores of `d68f4b3a` stay). The board test page has the card "Host: firmware names".
- **2026-11-22, branch `t254-old-entries` (D7, D8):** `host_core.cc` `hc_entry_add` takes `old_name`: the entry of the path, if
  it has that name, takes the new name in the same save. `host_core_flow_fileops.cc`: A (`hc_fo_browse_add`) passes the `.bit`
  header's name; the bundle path and R pass none. `host_core_rows.cc` `hc_entry_ver` (D8). The comment of `LROW_VER_W`
  (`hostui.cc`) names the new label. Tests: `test_hostcore.cc`, three checks (renamed in one save; another name stays; a new
  path does not rename an entry of another path): "test_hostcore: 894 passed, 0 failed" (Mac). `emu_t254_fwname.sh` run 2: the
  first run adds `f_swap.cfw` and `g_swap.cor` without the version line, so under the `.bit` header's name, as a host before
  T254 did; the second run, on the same flash with C= held (T135 S2), finds the files with the line and presses A on both. Its
  fixture files were made on the Mac and checked (`cor_pack.py check`).
- **The proof of S7, S8 (builder, 2026-11-22):** `tools/dev.sh test`: "test_hostcore: 894 passed, 0 failed", "apptests ok
  (tier fast)"; `tools/dev.sh emu`: "tests/hostapp/emu_host: all scenarios passed"; `emu_t254_fwname.sh`: "PASS T254 S3",
  "PASS T254 S7 (given): run 1 added f_swap.cfw and g_swap.cor as 'u64_mk2_artix 2026/10/25 12:00:00'", "PASS T254 S5",
  "PASS T254 S7: A renamed run 1's two entries to 'Gideon 3.14' and 'Commodore 1.1.0'; the other three kept their names;
  five entries", "PASS T254 pictures: ten different screens". S8 read off `t254-9-menu` (run 1: the packaged entry under
  its old name says "packaged firmware") and `t254-10-renamed` (run 2: the renamed entries; "packaged firmware" stays). The
  core app's emulator flows were red on the freeze check only: the branch was made before #131 fixed it; it is rebased onto
  `e3038a98` (#131 merged).
- **UX review (ux-specialist, 2026-11-22):** PASS. LOW (R5, R7): on the compact canvas (480p) the label is cut to about
  "packaged f..." (from the glyph widths; this emulator image has no `--screen`, so not seen); R5 allows the cut, and the
  review's proposal is to leave it. Widening the compact column would take room from every name on that canvas.
- **The pull request (2026-11-22):** #132 into `integration`, after the integration run of #131 (45021023294) was green.
