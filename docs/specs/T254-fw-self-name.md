# T254 — A foreign firmware shows the name and version it gives itself, not its bitstream's date

**Status:** IN PROGRESS (2026-11-22). Asked by Alex, 2026-11-22.
Open (one line, as in the TODO table): IN PROGRESS 11-22: built, units PASS; open: emulator pictures, review, merge
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
```

| # | Solves | Test | Where it runs |
|---|---|---|---|
| S1 | P1 | a scratch reader over `cfw.cc` with the four files (the files are not in the repository) | Mac, by hand |
| S2 | P2 | `tests/hostapp/emu_t254_fwname.sh`: pictures `t254-1` to `t254-4`, `t254-8` | builder (ue2emu) |
| S3 | P3 | the same script: the `boot.cfg` line; `tests/hostapp/test_cfwicon.cc` | builder; host tests |
| S4 | P4 | the same script: picture `t254-5-start-ask` | builder (ue2emu) |
| S5 | P5 | the same script: the manifest; `tests/hostapp/test_fwconvert.cc` (the table first) | builder; host tests |
| S6 | P6 | `tests/hostapp/test_cfw.cc` `s_self` (the reads of the scan) | host tests |

## §3 Decision

- **D1 (Alex, 2026-11-22, "Yes, put that in too"):** the rule of the backup. The line `Software version:` in the application
  gives the version. The first digit of the version gives the vendor: 1 is "Commodore", 3 is "Gideon", any other is
  "Firmware". The text "TRX64 Ultimate" in the application gives "TRX64 Ultimate" (the proposal Alex accepted: "TRX64 is
  recognised by the text 'TRX64 Ultimate' in the application"). The name is "<vendor> <version>". A file without the line
  keeps today's names.
- **D2:** the places are the File Browser panel (title; Version row "3.15, full firmware"), the name of the entry that A adds,
  the start question of S, and the manifest that P writes. The table of official files stays first for P.
- **D3:** the reader reads only the last 512 KiB of the application, in 16 KiB pieces. This covers the measured distances of
  §1 with margin. A line further from the end is not found: the host then shows the `.bit` header's name, as today. Reason:
  P6. The whole application costs 1 s or more for each selected row.
- **D4:** the boot menu does not read the file at start: the icon of a firmware entry comes from the entry's name. The first
  word of the new names picks the icon that the `.bit` header picked before: "Gideon" and "Commodore" give Gideon's icon
  (Commodore's bitstreams have the design `u64_mk2_artix` too), "TRX64" gives the TRX64 icon.
- **D5:** the boot menu reads nothing new and `boot.cfg` does not change. The Version column of a `.cfw` entry stays "full
  firmware": the entry's name has the version.

## §4 Out of scope

- **To decide with Alex (asked 2026-11-22):** an entry added before T254 keeps its name. T198 D17: the person names the entries;
  A again refreshes only the hash. A `.cor` packaged before T254 keeps "bit <date>" as the manifest version, so the boot menu
  shows that date until P packages the file again.

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
