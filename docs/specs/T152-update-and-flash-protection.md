# T152 — The MultiCore system updates its own parts from a .txu package, and a large core cannot overwrite the host in flash

**Status:** BUILDING (2026-10-31). Asked by Alex 2026-10-29; 2026-10-31 (translated): "Then build T152 and we measure tomorrow." Status check 2026-11-13: BUILDING stays: D1-D11 and S8 merged (a92658e30, 11-08); board S2/S5/S6b/S7/Q5, locked text and S8 open.
Built: D3/D5 `.txu`, D4/D6/D7 flash protection, D8 inspect, S8 ESP32 flashing (2026-11-07, section 5.7; the host gateware part is T164 WP13).
Open for the board: S1, S2, S5, S6, S7 and Q5 (`docs/archive/board-tests/board-tests-pending.md`); D6 is not called done before Q5 is measured.
**Repo:** TRX64U-MultiCore — host application (`host-app/software/hostapp/`: update flow, flash lock, F8 setting), packaging
(`tools/`, CI), host gateware only if Q3 is answered yes (ESP32 boot pins). `apps/ultimate` is not changed.
**Number:** T152.
**Origin:** Alex, 2026-10-29, on the T137 build (translated):
- "Not all cores stop working, only the partial / DFX cores. An update could also concern only the app."
- "I think we should strictly separate 'install of Gideon-based firmware' and our own update logic (separation of concerns).
  Our system consists of: host, host app, partial cores bound to the host, large cores."
- "Our update could be .txu as its type. .cfw we start."
- "The only place where we would still depend on Gideon is the ESP32 update. If we could run that isolated from one of his
  files, that would be gold."
- "Another topic we had shelved: can we protect the flash against writes from a Gideon large core? That would then be a
  parameter for the F8 menu." Then: "And on the large core load we set it. In the host menu the flash is open." Then: "So
  only the .cfw write at 0x00000 and the app."
**Depends on:** T84 D8 (flash protection, decided, never built), T115 (large `.cor`, `.cfw` start), T139 (F8 settings
page), T26.0 (ESP32 facts). Splits T137: our update moves here, T137 keeps the foreign firmware (see D2).
**Milestone:** M3.

---

## §1 The problem

- **The system has four parts with different bindings**, and an update must respect them:

  | Part | Where | Bound to | Breaks when |
  |---|---|---|---|
  | Host (layer 1 bitstream, boot ROM) | flash 0x000000 | nothing | - |
  | Host app | flash 0x220000 | the host's app interface | the host changes that interface |
  | Partial (DFX) cores | `/cores/*.cor` | host key (floorplan + contract) | the host bitstream changes its key |
  | Large cores (full bitstream) | `/cores/*.cor`, `.cfw` | nothing of ours | never through a host update |

- **P1 One package for everything.** Today the T137 branch (`t137-updater`, not merged) writes host bitstream and app
  together from a `.cfw`/`.ue2`-shaped update file and mixes it with the restore of Gideon/Commodore firmware in one
  module (`fwupdate.cc`) and one browser (U/R keys). An app-only fix rewrites the bitstream too; the warning "cores stop
  working" is shown for every update, also when the host key does not change and for large cores it never applies.
- **P2 Our update has no type of its own.** A MultiCore update looks like any `.cfw`, so the host needs a marker to tell
  it from a tester package (T137 Q2).
- **P3 ESP32 update.** The ESP32 firmware is the one part we still take from Gideon's/Commodore's update files
  (T26.0: one image set, bootloader + partition table + `u64ctrl.bin`; Gideon `update_v3.14.ue2` = V1.8, Commodore
  `c64u_v1.1.0.ue2` = V1.11). Today it can only be flashed by running the whole vendor updater, which first rewrites the
  FPGA flash and replaces our host. Our host bitstream does not drive `WIFI_BOOT`/`WIFI_DIS` (T26.0 §7), so we cannot put
  the ESP32 into its ROM loader.
- **P4 A large core can overwrite the host.** A large core is a full bitstream with its own SPI flash controller; Gideon's
  firmware lifts the block protection before it writes (`protect_disable`, `boot-chain-concept.md` §"Protecting the host
  in flash"). Running his updater from a `.cfw` replaces our host at 0x000000 and our app at 0x220000. T84 D8 decided the
  fix (power-supply lock-down) on 2026-10-12; it was never built.

## §2 Scenarios

```gherkin
Scenario: S1 app-only update
  Given a .txu that holds only a host app, built for the running host's interface
  When  it is chosen in the File Browser and confirmed
  Then  only the app region at 0x220000 is erased and written, the bitstream at 0x000000 is not touched (read back equal),
        no warning about cores is shown

Scenario: S2 host update that changes the host key
  Given a .txu with host bitstream + app whose host key differs from the running one, 3 partial and 2 large cores on /cores
  When  it is chosen
  Then  the question names the 3 partial cores that will need a rebuild and says the 2 large cores are not affected

Scenario: S3 a .txu that does not fit
  Given a .txu whose app needs a host interface the package's (or the running) host does not provide
  When  it is chosen
  Then  it is refused before the first erase, with the reason (control: same package with the interface field fixed passes)

Scenario: S4 .cfw is started, not installed
  Given a .cfw or .ue2 in the File Browser
  Then  its default action is "start" (T115); no update action is offered on it from this spec's code

Scenario: S5 flash locked under a large core
  Given "Protect the flash under large cores" on (F8)
  When  a large core is started
  Then  before the ICAP handover the host sets block protect over 0x000000 to the end of the app region and the
        power-supply lock-down; a status register read-back shows both
  And   in that large core, Gideon's updater (or a test core that erases 0x000000) fails; after the power cycle the host
        boots unchanged (sha of 0x000000 and 0x220000 equal to before)

Scenario: S6 host menu keeps the flash open
  Given the host menu after a power cycle
  Then  the status register shows no lock-down and no block protect; a .txu update writes (S1)

Scenario: S7 setting off
  Given "Protect the flash under large cores" off
  When  a large core is started
  Then  no protection is set (status register read-back = 0 for BP/SRL), as today

Scenario: S8 ESP32 from a vendor file (only if Q3 = yes)
  Given Commodore's c64u_v1.1.0.ue2 on the stick
  When  "update the ESP32 from this file" is chosen in the host
  Then  only the ESP32 image set is taken from the file and flashed through the ESP32 ROM loader; the FPGA flash is not
        touched (read back equal); CMD_IDENTIFY then reports the file's version

Scenario: S9 inspect a vendor file for its ESP32 image (D8)
  Given c64u_v1.1.0.ue2 and update_v3.14.ue2 on the stick
  When  F8 -> "Inspect .cfw/.ue2 for ESP32" is run on each
  Then  it shows "ESP32 V1.11" and "ESP32 V1.8" (and the board's running version if known); a file without an ESP32
        part says so; nothing is written
```

| # | Solves | Test | Where |
|---|---|---|---|
| S1-S3 | P1, P2 | `tests/hostapp` with a fake flash (erase/program counts per region), ue2emu flash model, read-back | CI, emulator |
| S1, S2 | P1 | one app-only and one host update on the board | board |
| S4 | P2 | browser action table bench | CI |
| S5-S7 | P4 | status register read-back after start (JTAG), a test large core that tries to erase 0x000000 | board |
| S8 | P3 | `run_espflash.sh` (ROM loader model) + render bench `scenarios_t152`; the real chip: board | tier 1 + board |
| S9 | P3 | bench on Commodore 1.1.0 (V1.11) and Gideon 3.14 (V1.8) files, a file without ESP32 part as control | CI |

## §3 Decision

Decided by Alex (2026-10-29, quoted in the header):

- **D1 Four parts.** The update model is host, host app, partial cores (bound to the host key), large cores (bound to
  nothing of ours). Every warning and every check is per part: "cores stop working" is said only for partial cores and
  only when the host key changes.
- **D2 Separation of concerns.** Our own update (this spec) and installing/restoring Gideon- or Commodore-based firmware
  (T137) are separate modules with separate screens. They share only the lowest layer: flash driver, check-before-erase,
  read-back. T137 keeps: restore of the original firmware and the backup; its MultiCore-update part (P4/S4/D6) moves here.
- **D3 Our update is a `.txu` file.** A `.cfw` (and `.ue2`) is started, not installed by our update code.
- **D4 Flash protection under large cores.** When a large core is loaded, the host sets the protection; in the host menu
  the flash is open. Protected are only the host bitstream at 0x000000 and the host app at 0x220000 ("only the .cfw write
  at 0x00000 and the app"); the rest of the flash stays writable. It is a setting on the F8 page.

- **D8 Inspect for ESP32 (Alex, 2026-10-29):** "Somewhere in F8 a function 'inspect cfw/ue2 for esp32 updates' would
  still be good, I think." An F8 entry opens a `.cfw`/`.ue2` read-only and shows whether it carries an ESP32 image set
  and which version (the `IDENT_STRING` in `u64ctrl.bin`, T26.0 §1), beside the version the board's ESP32 reports, if the
  host can ask it. It writes nothing; flashing stays S8/Q3.

Decided by Alex on 2026-10-29 (each with its quote); the concrete layout and the choices made while building are in §5:

- **D5 `.txu` layout** (contents decided, Q1: host and/or app, no cores; layout decided 2026-10-29, Alex: "ja"). A small manifest (magic `TXU1`, origin `trx`, version, git commit, per part: kind `host`/`app`,
  offset, size, SHA-256, plus host key of a `host` part and the host-app interface version it needs/provides), followed
  by the parts. Only the parts present are written (S1); a part whose bytes already match the flash is skipped.
  Recognition is the magic and the manifest, so T137 Q2 disappears.
- **D6 How D4 is done** (decided 2026-10-29, Alex: "passt"). Before the ICAP handover to a large core: Write Enable for volatile status register (0x50), set
  TB = 1 and BP2:0 to the smallest bottom range that covers 0x000000 to the end of the app region, then SRL = 1 with
  SRP = 0 (power-supply lock-down), read both status registers back, refuse the start if they do not read back as set.
  The lock-down holds until the power is removed; the next power-on boots the host with an open flash (S6).
  **Never** SRP = 1 together with SRL = 1: on the W25Q128JV that is the one-time program lock and cannot be undone.
- **D7 F8 row "Protect Flash from updates"** (Alex, 2026-10-29: "'Protect Flash from updates' .. and it is *only* about
  flashing the bitstream .. Gideon's core must be allowed to store its settings", translated), on/off, default on (Q4),
  stored with the host settings. Protected is only the bitstream slot and the app slot, 0x000000-0x3FFFFF
  (`docs/c64u-flash-layout.md`); the flash drive (0x400000-0xFE8000) and the config sectors (0xFE8000-0x1000000) stay
  writable, so a large core keeps saving its settings. On the W25Q128, TB = 1 with BP = 101 protects exactly
  0x000000-0x3FFFFF (TRX64-Ultimate T44 Q4); the board's chip read JEDEC EF 40 18 and SR1 = 0x00 there (T44 §11).
  Not tested on the board yet: the power-supply lock-down itself.

## §3a Open questions

- **Q1** Answered 2026-10-29 (Alex: "Nur Host und/oder App Updates" - "Only host and/or app updates"): a `.txu`
  carries a host part, an app part, or both; never cores (D5).
- **Q2** Answered 2026-10-29 (Alex: "Ja das ist eine gute Funktion" - "Yes, that is a good function"): T137's
  "convert .cfw/.ue2 to .cor" stays (it writes no flash).
- **Q3** Answered 2026-10-29 (Alex: "Parken ja" - "Park it, yes"): flashing the ESP32 (S8) is parked; the boot pins
  `WIFI_BOOT`/`WIFI_DIS` (L13/M13, polarity to be measured on Gideon's core first, T26.0 §7) go in with the next host
  change that happens anyway, not as a rebuild of their own. D8 (inspect) does not need them.
  **Un-parked 2026-10-30 (Alex, translated: "and flashing the ESP32 too"):** S8 goes into the next host package (T164 D2). The flow and
  protocol are Gideon's (GPL, `apps/ultimate`): `update_u64ii.cc` `update_esp32_impl()` (bootloader 0x000000, partition table 0x008000,
  application 0x010000), `io/wifi/esp32.cc` (ROM bootloader over SLIP), mode bits 6:4 of the WiFi UART's flow control (`dma_uart.cc`
  `ModuleCtrl`); the host has to route those bits to WIFI_BOOT / WIFI_DIS (polarity measured on Gideon's core first). Start from the earlier work, not anew:
  TRX64-Ultimate `docs/specs/T26.0-esp32-facts.md` (ESP32-S3, 8 MB, the updater's flow without verify, open point M4 = the pin polarity) and
  the T26.2 `esp_tool` of 2026-10-11 (ROM loader reached; flash used 0x0-0xDFFFF; uart_dma patch 0007 gates button events while armed). M4 is answered: TRX64-Ultimate
  `t26-esp-ram` (`esp_ctl.v`, T26.1 board test 2026-10-10): WIFI_BOOT high = ROM loader, WIFI_DIS high = reset, both low = run.
- **Q4** Answered 2026-10-29 (Alex: "Ok." to "default on"): D7 defaults to on; to run Gideon's updater on purpose,
  switch it off in F8 first.
- **Q5** Gideon's firmware calls `protect_configure` (writes the status register) and keeps its config pages in the flash.
  Under the lock-down its status register writes are ignored. Does it boot and save its settings? Measure on the board
  (S5) before D6 is called done; where its config pages lie decides whether D4's range touches them.

To check before building D6: the JEDEC ID and both status registers of the board's chip (W25Q128JV: SRL in status
register 2), that BP/TB with TB = 1 on this chip give the 4 MiB bottom range, and where the app region ends.

- **D9 Board, 2026-10-31: the lock cannot take on this host; hidden behind a feature toggle (Alex).** With protection on, starting
  trx64g.cor stopped with "the flash could not be protected"; the log: `flash protection before the handover: the flash status registers do not
  read back as set (JEDEC ef4018, status 00 02)` - not one bit changed. Cause: the host's own write gate (`host/flash_gate.vhd`, T44,
  `ultimate_sys.vhd` "Status-register writes and 4-byte opcodes stay blocked") passes only its pass/checked lists; 0x50, 0x01 and 0x31 go out as
  0x9F (JEDEC read). The benches drove a chip model, not the gate, so they could not see it. The refusal itself worked: nothing started,
  nothing was written. Alex: "the protection is only meant to come on when the core is started" (it is: the write happens right before the
  FPGA reload, and that write is what the gate blocks), then "Yes, and if you can, hide the UI behind a feature toggle" (translated).
  Built: `HOSTAPP_FLASH_LOCK` (`flashlock.h`, 0 in the board build): the F8 row is hidden, `flashprot_before_handover` tries no lock and logs
  why, the stored default is off; the benches build with 1. **For the host package (T164):** the gate gets a lock opcode that lets exactly the
  lock pattern through (TB = 1, BP = 101, SRL = 1, SRP never), so the one-time lock is impossible in hardware too; then the toggle goes to 1.
  Board passed the same day: S6 (SR1 00, SR2 02 = QE only), S3 (refusal), S1 (nothing to write; app-only update written in 7 s and booted,
  back again). S2, S5, S6b, S7 and Q5 wait for the host package. Expected status under the lock: `34 03` (SR2 keeps QE), not `34 01`.
  **Built 2026-10-31 (T164 S15):** the gate's lock class and `HOSTAPP_FLASH_LOCK` 1; board steps S2, S5, S6b, S7, Q5 are open.

- **D10 A dialog's keys stand inside its box (Alex, 2026-11-04).** Board photos of the `.txu` update (package main-7fda7e1f): the question
  and the result box name their keys only in the footer ("Y continues  any other key cancels", "any key returns to the File Browser").
  Alex (translated): "the actions at the bottom are consistent with the framework, but the user's attention is not there. So please put them
  into the box, one line apart." So: every modal box of the host app ends with one blank line, then its key line; the footer may keep it.
- **D11 After a written update the board restarts itself (Alex, 2026-11-04).** Alex (translated): "after the update we either switch the
  board off or do a power cycle". Taken: a power cycle through the ESP32 control module (`CMD_MACHINE_REBOOT` 0x13, as the core app's
  `u64.power_cycle`, `coreapp/src/u64cfg.cc`), after the result box has stood for a few seconds with a countdown; a key starts it at once.
  The same for every host screen that today says "Switch the power off and on" (update, restore of the original firmware, `.cfw` written).
  When the module does not answer, the box says so and keeps the old text. Gideon-style power off stays the installer's (T49 Q6).

## §4 Out

- Flashing Gideon's or Commodore's firmware as the machine's firmware: T137 (restore), not here.
- Updating partial or large cores in flash: cores are files on the card, nothing to write in flash.
- Protecting the flash against our own partial cores: they already turn WREN into WRDI in hardware (`boot-chain-concept.md`).

## §5 Build notes

Built 2026-10-31 on `worktree-agent-a25ff9ddc5ec8f375`. Every bench has controls (a mutant of the source under test has to go red); the counts are the
lines the run printed.

### 5.1 What is built, and what proves it

| Scenario | Code | Proof |
|---|---|---|
| S1 app-only update | `txu.cc`, `flashfw.cc` `fw_write_parts`, `hostui.cc` `update_run` | `run_txu.sh`: fake flash counts erases/programs per region; app only = 0 in the bitstream slot, the slot reads byte for byte as before. Render bench: the same through the pages (`er_bit == 0`, `pr_bit == 0`, flash app = the file's part). Board: S1. |
| S2 host key changes | `txu.cc` `txu_check`, `txu_survey_add`, `hostui.cc` `up_survey_cores` | `run_txu.sh`: 3 partial + 2 large -> broken 3, large 2; same key -> 0; a large core is never counted. Render bench: 3 partial cores and 2 large cores on the SD tree; question 1 names "3 partial cores need a rebuild ... 2 large cores are not affected", question and result page. Board: S2. |
| S3 does not fit | `txu.cc` `txu_check` | `run_txu.sh`: `iface_bad.txu` refused with the reason, `iface_fixed.txu` (same package, field fixed) passes and stages; render bench: the refusal page before any erase (`erases == 0`). |
| S4 .cfw is started | `hostui.cc` key table | Render bench: footer of a `.txu` shows `U update` and no R/P/S; a `.cfw`/`.ue2`/`.txt` shows no `U`; `U` on them opens nothing; `R`/`P` on a `.txu` open nothing. `txu_probe` answers NOT_TXU for `.cfw`/`.ue2`-shaped files. |
| S5 lock under a large core | `flashlock.cc`, `flashlock_board.cc`, `hostui.cc` `lock_flash` | `run_flashlock.sh` (chip model): TB, BP = 101, SRL, SRP = 0, range 0x000000-0x3FFFFF exactly (0x3FF000 refused, 0x400000/0x580000/0xFE8000 writable), volatile writes only, read-back, power cycle returns an open flash. Render bench: lock before the jump (once, for a `.cfw` and a large `.cor`), a refused lock stops the start. Board: S5 (the lock-down itself is untested on the chip). |
| S6 menu keeps the flash open | `flashlock.cc` `flashlock_status`, F8 row help | Chip model: after a power cycle SR1 = SR2 = 00. The F8 row shows the live status registers (reads only). Board: S6. |
| S7 setting off | `flashlock.cc` `flashlock_apply(enabled = false)` | Chip model: no frame at all. Render bench: with the setting off the lock is not called and the core starts. |
| S9 inspect | `espinspect.cc`, `hostui.cc` `inspect_flow` | `run_espinspect.sh`: V1.8 and V1.11 in files of the vendor format, edge cases (header over the scan block, version over the hash block, no IDENT_STRING), app only / no partition table / damaged digest / another chip / plain file as controls; **the real `update_v3.14.ue2` gives V1.8 and the real `c64u_v1.1.0.ue2` V1.11, both with the application's SHA-256 matching** (run on the machine that has them, never committed). Render bench: F8 row, the pick, each result page. |
| S8 ESP32 flash | `espflash.cc`, `espflash_board.cc`, `hostui.cc` `esp_update_flow` | Section 5.7. |

### 5.2 The `.txu` layout (D5, as built)

`tools/txu_pack.py` is the writer, `host-app/software/hostapp/txu.cc` the reader; the table is the comment of `txu.h` and the docstring of the writer (the selftest
compares the interface constants). Little endian: magic `TXU1`, header length, flags (0), host key, interface provided, interface needed, number of parts (1 or 2),
origin `trx`, version (23 characters), commit (15), per part {kind 1 host / 2 app, offset, size, flash address, SHA-256}, CRC-32 of the header; the parts follow, 4-aligned,
the file ends with the last one. Host part = the `.bit` as it goes to 0x000000 (the host key is its AXSS stamp, which the reader recomputes from the bytes and compares
with the header), app part = the record and data as they go to 0x220000. The reader refuses: another magic, a header or part out of range, a CRC that does not match,
origin other than `trx`, bytes behind the last part, a part whose SHA-256 differs, a host part that is not a `.bit` of the stated length, not for the XC7A50T, or whose
stamp is not the header's key, an app record the boot ROM would not take (`fw_image_seal_parts`).

### 5.3 Choices made while building (each open to a veto)

- **Key `U`** for the update in the File Browser (`P` package and `R` restore are T137's): the letter was free and says what it does; it is offered on a `.txu` only.
- **Host-app interface (D5)**: one integer, equality. `TXU_IFACE_PROVIDED = 1` (what the host this app was built together with offers) and `TXU_IFACE_NEEDED = 1` (what
  this app expects) are constants of the running app - nothing in the gateware states the interface, so the number of the running host is the one compiled into the app
  that was built with it. A package must agree with itself (its app and its host) and with what stays (a package's single part against the running one). Change the number
  on both sides whenever the host-app interface changes in a way an old part cannot live with.
- **The host part is `null_full.bit`** (the host with the null core in the partition, T123), as in `ci/update.sh` `trx64host_update_*`, not `c64_basic.bit`; the CI package
  step (`ci/package.sh` -> `tools/pack_txu_mc.sh`, benched in `run_txu.sh`) writes `trx64host_update_<commit8>.txu` from this run's `null_full.bit` and `hostapp.app` and reads it back with `txu_pack.py inspect`; with no
  `null_full.bit` (red host lane) it writes none and marks the target red. The version field holds the branch cut to 14 characters, a dash and the commit.
- **Parts equal to the flash are skipped** (`fw_flash_equals_bit/_app`), per part; both equal = the page "Nothing to write". Order of a real write: application first with
  its record written last, then the bitstream, every page read back, then both regions read back whole (`fw_write_parts`, shared with the restore: `fw_write` is the same
  call for both regions, T137's tests stay green).
- **The lock is set in `start_full` (a `.cfw` and a large `.cor`) and in the direct boot, after the last point where the start can be aborted**, and never for a partial core. A
  lock that does not read back as set stops the start with the page of the start screen; nothing is handed over. The F8 setting lives in the T163 store
  (`/Flash/trx/host.cfg`, section `[Host Settings]`, item `Protect Flash from updates`, `Off`/`On`, default `On`).
- **SR writes are volatile only** (`0x50`, never `0x06`), for SR1 and SR2 alike, so nothing in the chip's non-volatile cells changes. SR1 is read-modify-written with SRP
  cleared in the byte that goes out (the only SR1 write of the file), SRL is written only after SR1 reads back with SRP = 0, CMP (which inverts the range) is cleared, the
  chip must answer JEDEC EF 40 18; any other chip (the emulator's Infineon S25FL128L answers 01 60 18) gets the ID read and nothing else - the core starts unprotected
  and the log says `not a W25Q128JV: the flash is not protected`. Controls in `run_flashlock.sh`: with the mask gone, with the guard gone (a chip whose SR1 write is ignored
  and reads SRP = 1), the non-volatile write enable, no read-back, a 2 MiB range, CMP kept, no lock-down, any chip, setting ignored, an extra byte in a write frame (the
  board's controller clocks a byte in on every read of its data register, so a write-only frame must never read).
- **Inspect (D8)** identifies the image set by structure (0xE9 image, chip id 9, segment walk, appended SHA-256; esp_app_desc magic; partition table with the factory app at
  0x10000), not by names or offsets of one vendor's file, and reads the version from the application's `IDENT_STRING`. **The board's own ESP32 version is not shown: the
  host app has no WiFi link to ask it** (the page says so); asking it needs the T164 work.
- **F8 page**: nine rows now (row pitch 37 instead of 46 so they fit); the Protect row's help line ends with the live status registers, which is also the S6 read.

### 5.4 Open for the board (`docs/archive/board-tests/board-tests-pending.md`, "T152 board session")

S1, S2, S3 (board), S5, S6, S6b, S7, Q5 and the inspect on the real files. **Not claimed done: D6** until Q5 is measured, and one assumption of D6 that only the chip
can answer: that a *volatile* SR2 write of SRL = 1 sets the power-supply lock-down and that it is gone after the power cycle (S6b). If SRL stayed set after a power cycle the host
could not write the flash from its menu (JTAG is the way back); the first board step reads the status registers in the host menu before and after for exactly this.
SR1/SR2 are read by the host itself (its log line at the lock and the F8 row); no independent JTAG read of the status registers exists in this repository.

### 5.5 Runs (2026-10-31, commit of the host app `c721bc07`)

- **Benches** (builder, `ci/trxapps.sh` in `u64-build-env:ci`, rc 0, 40 min): host tests `hostapp` PASS (includes `run_txu.sh` 24 controls + the package step control,
  `run_flashlock.sh` 11 controls, `run_espinspect.sh` 7 controls), `coreapp` PASS, `loader` PASS; the render bench 539 checks, 0 failed, with 12 T152 controls (U for any file, one
  question only, no lock, the lock's failure ignored, every part written, an equal part rewritten, no word about the cores, the footer without U, the inspection of any file, the
  plain side panels, a setting that does not flip) each red as it must; the RISC-V build of host app and core app passes; `tools/prepush.sh`: lint ok.
- **ue2emu** (`tests/hostapp/emu_txu.sh`, the RISC-V host app built with `HOSTAPP_EXTRA=-DHOSTAPP_EMU_FLASH`, the emulator's flash model; pictures of every page in
  `scratchpad/shots/t152` of the session): an app-only `.txu` wrote exactly the 18 sectors of the application region and left the bitstream slot byte for byte as it was; the same
  package again says "Nothing to write"; `newkey.txu` (host + the app that is in the flash already) asked "Update the host?" with "3 partial cores need a rebuild ... 2 large cores
  are not affected" and "Not written again: the host app", then wrote the host (562 sectors: the new bitstream, the rest of the slot blank again, the old-firmware pattern gone),
  the read-back page named the cores; a damaged package and the interface-2 package are refused before any erase; `U` on a `.cfw` does nothing. Beyond 0x400000 only the host's own
  flash-disk sectors of `/Flash` changed (settings it writes itself); the config sectors 0xFE8000 and up are untouched. The F8 page shows the live chip (the emulator's 016018, so
  "its status registers are not touched").
- **ESP32 inspect on the real files** (read only, from `~/Downloads`, never committed): in ue2emu on the RISC-V build `c64u_v1.1.0.ue2` shows `ESP32 firmware V1.11` and
  `update_v3.14.ue2` `V1.8`, both "SHA-256 matches"; `run_espinspect.sh` does the same natively when the files are on the machine.
- **Not run on a board:** everything under 5.4.
- **D9 benches**: the render bench builds with `-DHOSTAPP_FLASH_LOCK=${FLASH_LOCK:-1}` (every `build` line shares it). A second build with `FLASH_LOCK=0` runs only
  `nolock_scenario` (`T152_NOLOCK=1`): the F8 list is exactly Direct boot, the two picture rows, Inspect, Cores and ROMs, Bug report - no "Protect Flash from updates". Its control
  (`t152_row_at_0`) turns the `if (HOSTAPP_FLASH_LOCK)` of `set_kinds` into `if (true)` and has to go red. `flashprot_before_handover` has an `#else` branch, so the board build has no code after the early return.

### 5.6 D10 and D11 (2026-11-04, branch `t152-d10d11`)

- **D10 as built.** `fm_draw`/`fm_apply` (`hostui.cc`) show `pg.hint` a second time as the last row of the page: one blank row (the page's pitch) under the last thing the page
  says (lines, input, bar, message), then the key line (`fm_key`); the box grows by two rows and stays centred. One place for every page of the format, restore, conversion, update and
  inspect flows (61 pages in the bench); the footer keeps the same text. The two small panels that already carry their keys inside (`cf_root`, `rn_root`) are unchanged.
  Proof: `fw_geom_check` (every page the firmware flows put up) now also asks `hostui_test_fm_key`: key line shown iff the page has a hint, text equal to the hint, at least 20 px below the
  last element above it, inside the box; `T152 D10: 61 of them with the key line inside the box`. The box height is checked against the scrim as before (heights 190..362). The 480p / 576p / 720p /
  1080p / 800x600 mode pass puts the 8-line demo pages (with and without the bar) up: `view_bounds` 0 outside, 0 cut. Control `t152_d10_no_key_line` (the old box: key row hidden) fails by 62 checks.
- **D11 as built.** `espreboot.cc`/`.h` (new, board only; the render bench brings its own `esp_power_cycle`): the host app had no ESP32 link, so the WiFi UART is opened as the core app does it
  (`hw.cc` `hw_rocker_init`: 5 Mbit/s, CTS/RTS, SLIP, receive callback), CMD_IDENTIFY 0x02 first (no answer within 500 ms: "the ESP32 control module did not answer"), then one
  `CMD_MACHINE_REBOOT` 0x13 (`rpc_header_t` = command, thread, sequence; no reply). `hostapp/Makefile` gains `espreboot.cc`, `dma_uart.cc`, `cmd_buffer.c` (the io/uart pair was already in `vendor.list`).
  `hostui.cc` `restart_page()` replaces "Switch the power off and on ..." on the restore result page (`fw_result`) and the update result page (`up_result`; the `.cfw` flow ends in `fw_result`):
  the row now reads "Restarting in N s to start the new version." (5 s), key line "any key restarts now" (D10); a key sends it at once; after the send the board is waited on for 5 s - if it still
  runs, or if the module did not answer, the blank row above says why ("The restart did not happen: ...") and the old text "Switch the power off and on to start ..." stays; the notes after the flow
  ("Update written. Switch the power off and on.") are unchanged for that case. The countdown row is "Restarting in N s to start the new version." and the key line is "any key restarts now" (not one
  combined line).
  Proof (render bench, `esp_power_cycle` stubbed; `tests/hostapp/render/render.cc` D11 block, update flow on `newkey.txu`): countdown runs out: `1 request(s) after 5000 ms`, the first page says
  "Restarting in 5 s ..." and no "Switch the power off"; key: one request, under 1 s; module silent: one request, page shows both rows, hint "any key returns to the File Browser", the note stays.
  Controls: `t152_d11_never_sends` (request never sent) fails by 4 checks, `t152_d11_no_countdown` (0 s) by 3.
  Pictures: `t152_ask1_app`, `t152_done_app`, `t152_d11_countdown`, `t152_d11_noanswer` in the render bench's out directory.
- **Not done / for the board.** The real power cycle is not run (no board access); step: write a `.txu` and watch the countdown and the restart. The host app's UART setup is a copy of
  the core app's sequence, not yet run against the module from the host app.
- **Run (builder, `u64-build-env:ci`, CI cache mounted, `ci/trxapps.sh` on commit `aa7d1511`):** RISC-V host app 872548 bytes, core app 1639140 bytes (`check_app_size` ok); `ci/apptests.sh`
  hostapp PASS 47 s, coreapp PASS 106 s, loader PASS; host render bench `1202 checks, 0 failed`.

### 5.7 S8 ESP32 flashing and the locked-flash text (2026-11-07, branch `w1-t152`)

- **Entry point as built:** key `U` on the inspect result page of a file whose image set is complete and intact (ESP_FOUND). Open decision for Alex: this, or a row of its own in F8.
- **Code:** `espflash.h/.cc` (the protocol: SYNC, ATTACH_SPI, CHANGE_BAUDRATE 2 Mbit/s, SET_FLASH_PARAMS, FLASH_BEGIN/FLASH_DATA per image at 0x0, 0x8000, 0x10000, then run mode and
  CMD_IDENTIFY; no Flash object in its interface), `espflash_board.cc` (the link on the board: `esp_ctl` at 0x10060910, the WiFi UART; compiled by the RISC-V build only),
  `espinspect.cc` (`boot_len`, `ptable_len`), `hostui.cc` (`esp_up_ask` question, `esp_up_cb` progress, `esp_up_result`, retry by RETURN after a failure that changed the ESP32's flash).
- **Proof:** `tests/hostapp/run_espflash.sh` against `esp_loader_model.h` (green, 20 mutants red); render bench `scenarios_t152`: success (images at 0/0x8000/0x10000, version reported = file's,
  `BenchFlash erases == 0 && programs == 0`), cancel at the question (module never entered), failure before the first FLASH_BEGIN ("Nothing was written"), failure after it (RETURN writes
  again, no second question, whole set lands), another version reported, no identify answer, U on files without a complete set does nothing. Controls `t152_s8_u_on_any_file`,
  `t152_s8_question_skipped`. Pictures (render out dir): `t152_s8_ask`, `t152_s8_progress`, `t152_s8_done`, `t152_s8_nothing`, `t152_s8_failed`, `t152_s8_differs`, `t152_s8_noanswer`,
  and the 640 x 480 frames `t152_s8_480_ask`, `_480_progress`, `_480_done`, `_480_failed`.
- **Locked flash text:** a flash locked by Protect Flash refuses the first erase of a write and keeps its data; `fw_write` (`flashfw.cc`) now says "the flash is locked (Protect Flash): switch the power off
  and on, then update again" with `started` false (no "do not switch off"); the same refusal one sector later stays "the erase did not take" and IS a half-written flash. Test: `test_fwrestore.cc`
  (red before the change, green after; control `fw_locked_is_started`).
- **Board steps (Alex):** S2, S5, S7, Q5 (5.4); the locked-flash text (update with the flash locked); S8 on the board (needs the host gateware with `esp_ctl`, T164 WP13: U on the inspect page of
  a vendor `.ue2`, watch the write, the module reports the file's version); T137 D10 restore countdown.
