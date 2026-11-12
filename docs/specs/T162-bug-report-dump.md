# T162 — the host app writes a bug report file (versions, configuration, media, log, core state) from its menu and serves it over REST

**Status:** ACCEPTED (2026-10-30), asked by Alex.
**Repo:** TRX64U-MultiCore — host app (`host-app/software/hostapp`: menu entry, file, REST `GET /v1/report`), the generic core app
(each core's own part of the state through its descriptor). No gateware.
**Number:** T162.
**Origin:** Alex, 2026-10-30 (translated): "Maybe we should also build a state dump into the next host app, and they have to deliver
it." The flow he sketched was "F8 -> bug report"; a QR code / one-click issue was dropped ("better not ... too easy == more bugs.
Data"). Summary agreed ("Yes"): F8 -> "Bug report" writes `/SD/report/<date>.txt`, also `GET /v1/report`; no QR.
**Depends on:** T139 (the host app's F8 settings page), T104 (generic app), the next host app release; T161 asks for the file.
**Milestone:** M3.

---

## §1 The problem

- **Who:** a tester reporting a fault; Alex reading it.
- **Today:** photos and words only; which bitstream, app, core, settings and image were running has to be asked back, and the
  state at the fault (registers, log) is gone once the board is switched off.
- **Parts (MECE):**
  - **P1 Trigger:** an entry "Bug report" in the host app's F8 menu (and in the core menus through the generic app), and
    `GET /v1/report`.
  - **P2 Contents (text, one file):** versions and SHAs (host bitstream, host app, core `.cor`, core app), board type, date; the active
    `<core>.cfg` and the settings; mounted media (name, size; D4: no SHA-256); the last console/log lines (ring buffer); the
    core's own state block (registers named by its descriptor, e.g. the Amiga's Kickstart, memory configuration, DMA state).
  - **P3 Output:** `/SD/report/<YYYYMMDD-HHMMSS>.txt` (folder created), the name shown on screen; the same text from REST.
  - **P4 Privacy:** no network credentials, no FTP server passwords (T158), no file contents beyond the hashes.

## §2 Scenarios

```gherkin
Scenario: S1 the file
  Given the host app with a core running and a medium mounted
  When  F8 -> "Bug report" is chosen
  Then  /SD/report/<stamp>.txt exists with the version, configuration, media, log and core sections, the name is shown
  # tests/hostapp with a fake SD and fake core state; control: a section left out -> FAIL; ue2emu walk; board step

Scenario: S2 REST
  When  GET /v1/report
  Then  the same sections as text/plain, Content-Disposition attachment; control: route missing -> FAIL

Scenario: S3 no secrets
  Given a stored FTP server with a password and network settings
  Then  neither the password nor the network key appears in the report; control: the filter off -> FAIL
```

## §3 Decision

- **D1 (Alex):** a file on the SD plus REST; no QR, no prefilled issue link (T161 D1).
- **D2 (Alex, 2026-10-30: "fits", Q1):** the report carries the last 200 log lines or 16 KB of log, whichever is smaller.
- **D3 (Alex, 2026-10-30: "a", Q2):** core state is a fixed, named register list per core, declared in its descriptor; no raw dump of
  the register window (unreadable without docs, and a read with side effects could trigger something).
- **D4 (Alex, 2026-10-30: "c"):** media are reported with name and size only, no SHA-256 (hashing a hard disk image of
  hundreds of MB in the menu would take minutes). P2's "SHA-256" for media is dropped, and so is write-protect (option c was "name and size only").
- **D5 (Alex, 2026-10-30, on the emulator screenshot without an SD card: "it could be USB too?!"):** the report goes to `/SD/report/` when an
  SD card is there, else to `report/` on the first writable USB medium; with neither, the message says so ("no SD card or USB stick").
  The REST route is unchanged.

## §3a Open questions

1. ~~Q1 Log size~~: decided, D2.
2. ~~Q2 Core state~~: decided, D3.

## §4 Out

- Screenshots inside the report (the reporter photographs the screen, T161).
- Uploading the report anywhere.

## §5 Decisions taken while building

### Core application part (built 2026-10-30)
- `coreapp/src/corereport.{h,cc}` builds the text from the context (descriptor, settings, state values of the media slots, the log tail) and writes
  `/sd/report/<stamp>.txt` (`-1`, `-2` on a name clash). It shares `bugreport.cc` (host app dir) with the host part, so the P4 filter is one piece of code.
- P1: "Bug report" is a fixed row (`EK_BUG_REPORT`) of the System tab of every record-less core (13 menu goldens +1 line); the C64's record
  (`menu.toml`) carries the node "Bug report" in F4/Diagnostics (order 35). Both call the operation `diag.bug_report`.
- D3: `CoreDesc::report_regs` / `nreport_regs` (`ReportReg { name, reg, bytes }`, at most 24, `desc_check` refuses an empty name, a width other than
  1/2/4 or a place outside the register page). Only these registers are read. Filled: amiga and amiga1200 (14: FEAT, CTRL, MEM/FAST/CPU/CHIPSET/
  FLOPPY/IDE/JOY cfg, STATUS, FB_LATEST, FRAME_SEQ, MBOX_STATUS, DLOG_STATUS; the debug-log POP is left out, it pops), apple2 (10), c64 / TRX64U (4:
  CAPS, CAPS2, GATE_CTRL, GATE_STATUS). **Empty list (the report says "declares no register list"):** atari800, nes, c128, c264, cpc, vc20, zx,
  zxnext and the test core - their registers are not named yet.
- S2: `GET /v1/report` in `rest.cc` (text/plain, `Content-Disposition: attachment; filename="<stamp>.txt"`, 501 without the hook, 405 for non-GET,
  503 when busy). `restsrv.cc` output buffer 12288 -> 28672 bytes so the report goes out whole (heap cost on the board to be looked at in the board step).
- S3: settings typed as stars (kind 2) and keys with a secret name are withheld, URLs lose `user:pass@`, log lines with credential words are replaced.
- D4 built: the core report prints per media slot `<state text>, <n> bytes (<path>)`, `<state text>, size not known` (no mount path seen
  under that name, or stat fails) or `empty`. No slot keeps its path, only its file name, so `CoreCtx::noted` (16 paths) records every
  successful `AT_PATH` argument with an extension in `OpRegistry::run`, and the report matches the slot's name to the newest one. No hash.
  Write-protect is not printed (D4: name and size only). The host report's
  "Media" line lists the mounted storage (SD, USB), not images; it carries no hash and stays as is. Not built: the Kickstart/ROM version is not in the Amiga's list (no register holds it).
- Tests: `tests/coreapp/test_corereport.cc` (60 checks) with 13 controls (cr_no_size, cr_no_state, cr_no_log, cr_raw_window, cr_empty_list, cr_stars,
  cr_overwrite, cr_no_folder, cr_no_route, cr_no_attachment, cr_no_501, cr_amiga_regs, cr_width_check), all fail as they must; `test_desc` (838 checks)
  counts the new fixed row. Board step (a report from a real Amiga/Apple/C64 core on the SD card and over REST) is open.

### D5 and two screenshot fixes (built 2026-10-30, found on emulator screenshots of the host app)
- D5: host `hostreport_write` takes `/SD/report` when the snapshot has an SD card and it takes the folder, else `/Usb0..3/report` (the first `make_dir`
  that works, only when the snapshot has a USB medium); neither: "Bug report not written: no SD card or USB stick" ("... takes a folder" when a medium is
  listed but none takes it). The emulator lists the SD slot with no card in it (chip lit, `make_dir` fails), so a listed medium that takes no folder counts as absent. The note shows the folder used (`hostreport_dir()`), the F8 row
  help and the idle value ("write to SD/USB") no longer promise `/SD/report`. Core app: `core_report_save` tries `/sd/report`, `/usb0..3/report` in this
  order (`core_report_dir()` for the note), same message; the SD card wins over a stick. REST is unchanged.
- File name with the clock not set: `nodate-<n>.txt`, n the first free from 1 (was `00000000-000000.txt`), host and core app. `br_stamp` gives "nodate".
- Boot menu without cores: the hint "No cores yet. ..." is two lines (`NO_CORES_HINT`, explicit line break); `hostui_test_hint_overflow()` measures its
  widest line against the content column (render bench), like the footers.
- Tests: `test_report.cc` and `test_corereport.cc` (no SD + USB -> file on the stick, SD wins, neither -> message and nothing written, `nodate-1/2`, the
  first free n); controls `rpt_no_usb_fallback`, `rpt_first_usb_only`, `rpt_nodate_zero` (host) and `cr_no_usb_fallback`, `cr_first_usb_only`,
  `cr_nodate_zero` (core app) fail as they must.
