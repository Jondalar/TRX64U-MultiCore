# T230 — The TRX64 Ultimate firmware's version reaches the boot menu, and trx64u starts with a config, its media and an autostart

**Status:** PROPOSED (2026-11-16). Nothing built; §3 items marked "proposal" wait for Alex and, where the contract moves, for the
TRX64-Ultimate firmware session (§3a, one question at a time).
**Repo:** TRX64U-MultiCore (host app, generic core app, `tools/pack_import_cor.sh`, `release/imports/*.pin`) and TREX/TRX64-Ultimate
(its CI package, `ci/package.sh`). The shared text is `docs/contracts/trx64u-firmware-handover.md`, the same in both repositories.
**Number:** T230.
**Origin:** Alex, 2026-11-16 (translated): "Can you clarify two things with the TRX64U-FW? 1. the version number has to reach us, how
that works. 2. In perspective I want to phase out the Gideon UI ... but it will run in parallel for a while ... What we want with 'our'
UI is that when the core starts we can also load config, medium persist, including autostart if possible. Who has to do what, adapt
the contract etc." Then: "Will you write a spec on our side and ask the FW session to work on it with us, please?"
**Depends on:** `docs/contracts/trx64u-firmware-handover.md` (§1-§6), T221 D15 / OPEN-3 (SemVer of imported cores), T223 D4 (start
file `trx64-start.txt`), T180 D2 (Persist medium mounts), T217 (Load + run), T228 (trx64u on the generic menu, D10), T104.2 §5 (the
board probes of the generic core app on the monolith).
**Milestone:** M3.

---

## §1 The problem

Two cores come from the TRX64-Ultimate firmware: `trx64u` (its monolith bitstream with OUR generic core app) and `trx64g` (the same
bitstream with Gideon's application). The Gideon UI is to be phased out; for a while both stay in the menu. Read from the code today:

- **P1 Version.** `release/imports/trx64u.pin` and `trx64g.pin` carry `VERSION=8f1dc184` (a commit) and no `SEMVER=`.
  `tools/pack_import_cor.sh:38` takes a `SEMVER=` when the pin has one and otherwise logs "no SEMVER, the version shown is the commit"
  (T221 D15). So the boot menu shows `cad182c4` for TRX64U where every other core shows `x.y.z`. The TRX64-Ultimate package's
  `manifest.json` (written by its `ci/package.sh:40`) has `trx64_commit`, `app_commit`, `branch` and `files`, no version.
- **P2 Config at the start of trx64u.** Built for large cores (T223 D4): the host writes `<medium of the .cor>/trx64-start.txt`
  (`TRXSTART1`, `cor=`, `config=`, `safe=`) before the full-firmware start; the core app (`coreapp/src/start.cc` `start_file_take`)
  reads it once and deletes it. Proven in ue2emu with the Amiga 1200 large core. On the monolith the core app reads the hand-over page when
  `board.has_handover` (`board.cc:13`, `HwBoardProbe::handover_valid`; T112 Q1): `cfw_handover_page` (`legacy.cc:494`) fills it for every
  `.cor` the host starts (`ci.from_cor`), a `.cfw` leaves a wiped page, and the comment in `legacy.cc` says that whether DDR2 keeps it across the
  IPROG is board step S2 (not measured there). With a valid page `start_file_take` looks at `<medium of h->path>/trx64-start.txt` and
  takes it only when `cor=` equals `h->path`: correct. The bug is in the branch WITHOUT a valid page (JTAG/SRAM start, a `.cfw` start,
  a page that did not survive, the ue2emu monolith profile): `start.cc` then looked only at `/SD/trx64-start.txt` and accepted ANY `cor=`:
  a `trx64u.cor` on a USB stick gets its file written to `/USB0/` and is not found; and trx64g started from `/SD` leaves
  `cor=/SD/cores/trx64g.cor` there (Gideon's application never deletes it), a later page-less trx64u start reads that leftover, sets
  `cfg_path` to `/SD/cores/trx64g/trx64g.cfg` and writes its `[section]` settings into Gideon's file, which contract §4 forbids.
  Whether a real `.cor` start from the boot menu ever lands in that branch depends on S2 (page survival); not measured here.
- **P3 Medium persist on trx64u.** The core app has "Persist medium mounts" (T180 D2: `mediamounts.cc` `persist_extend`, added by
  `menubuild.cc:29` for a core with media slots; `boot.cc:157` `mounts_restore`). trx64u's menu is its own TOML descriptor
  (`coreapp/cores/trx64u/menu.toml`); T228 D10 (Alex 2026-11-15: "saving configs and mounted media like the other cores must go in
  too!") puts it on the generic mechanism, to be built in T228 S2. Not proven on trx64u today.
- **P4 Autostart.** There is no autostart at core start. "Load + run" (T217) exists as a menu action of some cores; mount, LOAD and RUN
  are there as building blocks. Nothing says which program a start should run.
- **P5 The contract does not name what trx64u relies on.** The generic core app on the monolith depends on the bitstream's address map
  (application `ORIGIN 0x00030000`, `LENGTH 0xE60000`), the board probes of T104.2 §5 (ICAP byte, ITU capability word `0x25E40202`,
  `TRX_CAPS`, the IEC stub's version byte `0x25`) and the drive and memory windows its menu uses. The contract (§6) asks for an
  announcement only for "the application's address map". The start file of T223 D4 is not in the contract either, although the host
  also writes it before `trx64g` (it is a large `.cor`), where Gideon's application ignores it and leaves it on the medium.
- **P6 trx64g in parallel.** It starts plain and keeps its settings in `<core folder>/trx64g.cfg` (contract §4). It must keep working
  unchanged while it is phased out.

## §2 Scenarios — what proves it is solved

| # | Solves | Scenario | Test (bench / board step) | Where it runs |
|---|---|---|---|---|
| S1 | P1 | A TRX64-Ultimate package carries its version; a pin bump copies it as `SEMVER=`; the TRX64U and trx64g rows of the boot menu show `x.y.z`, and the package stage logs no "no SEMVER" line | package log of the run after the pin bump; ue2emu picture of the boot menu | CI `package`; Mac ue2emu |
| S2 | P1 | A pin whose `SEMVER=` differs from the version in the pinned package's manifest makes the package stage red with a message naming both | `tools/pack_import_cor.sh` run in the package stage | CI `package` |
| S3 | P2 | trx64u on `/SD/cores`, config "Demo" chosen with RIGHT in the boot menu: core log `start file ... read and deleted: config "Demo"`, the Demo settings applied, no file left | ue2emu (monolith profile, our core app); then board | Mac ue2emu; board |
| S4 | P2 | The same with `trx64u.cor` on `/USB0/cores`: the file is found on the stick and applied | ue2emu; board | Mac ue2emu; board |
| S4b | P2 | A start file whose `cor=` names another core (trx64g's leftover) is dropped on trx64u: log "not for this core, dropped", `cfg_path` stays trx64u's, `trx64g.cfg` untouched | ue2emu (leftover file prepared on the card) | Mac ue2emu |
| S5 | P3 | trx64u with "Persist medium mounts" = Yes, a `.d64` mounted, back to the host, trx64u started again: the disk is mounted from the first cycle (`mounts_restore` log), the directory loads | ue2emu; board | Mac ue2emu; board |
| S6 | P4 | trx64u started with a config that names an autostart program: after the boot the program runs without a key press (screen shows it), the log names the autostart | ue2emu; board | Mac ue2emu; board |
| S7 | P5 | The contract text with the version field, the start file and the list of what the core app needs from the bitstream is the same in both repositories | `diff` of the two files, both commits named in §5 | both repos |
| S8 | P6 | trx64g starts and saves as before; a start file it left on the medium does not disturb the next trx64u start (log `config ""` or a matching `cor=`) | board: trx64g start, back to the host, trx64u plain start | board |

## §3 Decision

- **D1 (Alex 2026-11-16, origin):** the version number of the TRX64-Ultimate firmware reaches the MultiCore boot menu.
- **D2 (Alex 2026-11-16, origin):** the Gideon UI (`trx64g`) is phased out in perspective and runs in parallel for a while; "our" UI
  (`trx64u`) gets, at core start, load config, medium persist and, if possible, autostart.
- **D3 (Alex 2026-11-16, on the firmware session's ownership split and the go he had given it, translated: "That is good too. Sorry."):**
  the contract's §1 becomes the ownership table of FW-2's proposal, and both copies are made byte-identical again; the firmware session
  drafts, MultiCore reviews, the final text goes to Alex before either side commits it.
- **D3 (Alex 2026-11-16, after reading contract draft v2, translated: "Separation of concerns is not enough for me there. If I change
  something about the features with TRX64-Ultimate, the contract breaks because MultiCore owns the toml for the app"; on the proposed
  split: "Yes, in perspective. If that is so big now, we do it after we have a release"):** target split, built after the next release:
  TRX64-Ultimate owns the bitstream, its features, the trx64u menu TOML (today `coreapp/cores/trx64u/menu.toml`, 125 ops) and a feature
  library (today MultiCore's `trx64u_ops`, `ring*`, `monitor`, `monsrv`, streams, U64 registers), both shipped in its package and pinned
  by hash; MultiCore owns the host, the generic core app as the base, the library interface (`libapi.h`, T104.1) and the TOML format
  with the list of generic operations, published with versions; the contract then covers only interfaces (library interface version,
  TOML format, generic operations, hand-over page, start file). Until then the v2 split (MultiCore owns the trx64u menu and its ops)
  is the interim state.
- **D4 (Alex 2026-11-16, on rewriting the target in T213 terms, translated: "Let's try it that way then. But first close all loose
  ends and a release."):** D3's target is expressed in T192 D24 / T213 D1 (option B) terms: TRX64-Ultimate owns trx64u's API TOML and UI
  TOML; the feature logic (Timewarp ring, monitor, streams, U64 registers) becomes generic C modules in MultiCore's service, chosen and
  parametrised by the API TOML; no feature library. Contract §10 is rewritten that way. Built after the next release, after the loose ends.
- **D5 (Alex 2026-11-16, asked "Go for contract text v4?", answer "Go"):** the merged contract text v4 (firmware session's draft,
  reviewed here against MultiCore's code, §10 in T213 terms) is agreed. Both repositories commit the same bytes as
  `docs/contracts/trx64u-firmware-handover.md`; MultiCore's copy lands together with the code it promises (P-1 packer, in main; P-2 reader).
- **D6 (Alex 2026-11-16, asked whether a page-less trx64u start keeps waiting up to 4 s for a USB stick, answer "keep 4 s
  (recommended)"):** the wait stays (`lib_board_wait_stick`, `LIB_MOUNT_WAIT_MS`). It costs only on a start without a valid hand-over page;
  whether the page survives the full reconfiguration of a boot-menu start is to be read once on the board (core log "no hand-over path").
- **D7 (Alex 2026-11-16, Q-A answered "in the config (recommended)"):** the autostart target lives in the core's config: one setting
  "Autostart" (file + Mount & Run / Load & Run) in the core settings, saved with "Save as config"; choosing that config in the boot menu
  (RIGHT) starts the program. No new field in the start file. The place of the row in the menu is asked separately, with pictures.
- **D8 (Alex 2026-11-16, Q-B answered "yes, trx64g stays as it is (recommended)"):** P-6 holds: trx64g starts without a config choice,
  keeps `trx64g/trx64g.cfg`, does not read the start file; no work in Gideon's application. The config choice exists only in our UI (trx64u).
- **Proposal P-1 (version, after FW-1):** TRX64-Ultimate's `ci/package.sh` writes `"version": "x.y.z"` from the packed application's
  `APPL_VERSION_NUMBER` into `manifest.json`. MultiCore copies it into the pin as `SEMVER=` when the pin moves, but only when
  `PKG_VERSION` is a release (Gitea release `trx64-<PKG_VERSION>` exists); a non-release pin gets `x.y.z-dev+<sha8>`.
  `pack_import_cor.sh` checks the pin's `SEMVER=` (without `-dev+...`) against the manifest (S2). Contract §1 and §2 change.
  For today's pins no firmware change is needed: `main-8f1dc184` is the release 0.3.0 (Gitea prerelease `trx64-main-8f1dc184`, checked
  here), so both pins can carry `SEMVER=0.3.0` now.
- **Proposal P-2 (start file on the monolith):** the core app looks for `trx64-start.txt` on `/SD`, then `/USB0`, `/USB1`; without a
  page it takes a file only when its `cor=` names a `.cor` of its own core (core folder = its descriptor's), drops any other and logs
  it (S4b). MultiCore only; it is a bug fix (contract §4) and is built now.
- **Proposal P-3 (medium persist):** no new work here; it is T228 D10, proven in T228 S2 and by S5 of this spec.
- **Proposal P-4 (autostart):** open (§3a Q-A). MultiCore only: the generic core app does it with the building blocks of T217.
- **Proposal P-5 (contract):** §3 gets a paragraph "Start file" (place, format, one-shot, written before every large `.cor` including
  `trx64g`, ignored by Gideon's application); §6 gets the list of what the generic core app needs from the bitstream (P5's list), with
  the rule that a change to any of it is announced before it is pushed.
- **Proposal P-6 (trx64g):** nothing changes for trx64g. Choosing a config for it at start would need Gideon's application to read
  the start file, work on the TRX64-Ultimate side for a UI that is being phased out.

### §3a Questions (one at a time)

For the TRX64-Ultimate firmware session:
- **FW-1: answered (firmware session, 2026-11-16).** Yes, in `manifest.json` (already under `SHA256SUMS`, read by our packer; not a
  `version.txt`). `x.y.z` is `APPL_VERSION_NUMBER` in the application's `software/application/versions.h:44` (`"0.3.0"`, checked here),
  bumped by hand in its own commit per release, Alex picks the number; it is what Gideon's application and REST `/v1/info` show. No
  `v*` tag; a release is the Gitea prerelease `trx64-<PKG_VERSION>` plus the package `trx64-release/<PKG_VERSION>`, made by
  `release.yml` from the CI package byte for byte. Caveat: every CI package carries the `versions.h` of its commit, so a package
  between releases repeats the last release's number and cannot know whether it will be released. Their proposal: the pin carries
  `SEMVER=x.y.z` only when `PKG_VERSION` is a release (the Gitea release `trx64-<PKG_VERSION>` exists, checked when the pin is
  reviewed), otherwise `x.y.z-dev+<sha8>` or the commit. Their side (the `ci/package.sh:40` line, contract §1/§2) waits for Alex's go.
- **FW-2: answered (firmware session, 2026-11-16).** P5's items hold, read from their code: app region `linker.x:7`; capability word
  `top.v:606` `{2'b00, FPGA_TYPE, 28'h5E40202}` (0x25E40202 is the 50T C64U build, bits 29:28 follow `FPGA_TYPE`); IEC stub
  `ultimate_sys.vhd:1347-1351` (offset 0 = 0x25, 1 and 2 = 0x01); `trx_io.v:334-335` CAPS = 0xFF, CAPS2 = 0xF7, CAPS3 at 0x006. They
  announce any change to the list before pushing. Four items to add: (1) the boot ROM's `FLASH_APPL` 0x220000 `{dest, length, run}`
  and `MAX_APPL_SIZE` 0x1E0000-12 (`bootrom.c:74-75`, read by our `tools/check_app_size.py`); (2) ICAP 0x10060600 is undecoded and
  reads 0 on the monolith - our monolith detection depends on it, so decoding anything there is a change; (3) their U16 D7 picture
  registers at IOBASE+0x144F00: mode request 0x4F00, scale 0x4F01, read-back 0x4F04-0x4F0A (magic 0x48, version 1, flags, row,
  scale), `PX_POS` counted from the running row's active area; power-up row 1080p, Fit, kept unless our core app writes 0x4F00;
  (4) the DDR2 windows (below).
  **Contract drift found (S7 fails today):** the two copies differ. Theirs has §6 "DDR2 windows" (agreed 2026-10-30, their commit
  4f86e227: 0x03800000-0x03A00000 for their debug service U5, our STAGE_APP/STAGE_PART only before the jump, zero-on-start and
  give-up-on-update rules) and its §7 is our §6; ours has the §3 paragraph "Page tail after id_sum/core_id" (T191, T198) that theirs
  lacks. Checked here (section headings of both files).
  **Their proposal:** §1 of the contract becomes an ownership table and both copies become byte-identical again. TRX64-Ultimate:
  bitstream, Gideon's application (trx64g), `APPL_VERSION_NUMBER` and the releases, the package with the manifest version, the list of
  what the bitstream offers and announcing changes, trx64g's `.cfg` in the core folder (§4), the DDR2 window 0x03800000-0x03A00000.
  MultiCore: host and boot menu, pins and `.cor` packing (SEMVER rule), the generic core app on trx64u (config at start, persist,
  autostart, `menu.toml`), the start file (writer and reader), the hand-over page layout, the STAGE_* windows. Both with Alex: the
  contract text. The firmware session drafts the merged text; MultiCore reviews it against its code; each side commits the same bytes
  after Alex's go.
- **FW-3: answered (firmware session, 2026-11-16).** No objection: nothing in Gideon's application reads a `.txt` from a medium root; the
  file browser shows `trx64-start.txt` as an ordinary file and never deletes it. Older question text: The host writes `trx64-start.txt` to the medium root before `trx64g` too. Any objection from Gideon's application (file
  browser, a scan of the root)?

For Alex:
- **Q-A: answered (D7).** Where does the autostart target come from: (a) a row in the core's config ("Autostart" = a file + mode), saved with
  "Save as config"; (b) a choice in the host's RIGHT overlay, carried in the start file (`run=`); (c) both?
- **Q-B: answered (D8).** Does P-6 hold: no config choice for trx64g at start?

## §4 Out

- No change of the hand-over page layout or address (contract §3 stays as it is for the page).
- Nothing published to GitHub; releases stay by hand (CLAUDE.md).
- trx64g gets no config choice at start (D8).

## §5 Implementation log

- **P-1, MultiCore side (2026-11-16, branch t230-semver):** `tools/pack_import_cor.sh` takes `SEMVER=x.y.z` (as before, `ci/version.sh with`) or
  `x.y.z-dev+<sha8>` (sha8 must be the one of `PKG_VERSION`, the string is the .cor version as it is, no second suffix); anything else is refused naming the pin.
  It fetches the package's `manifest.json` next to the bitstream (or takes `--manifest`); with `"version"` the x.y.z of `SEMVER` must equal it, else
  exit 1 naming both; without it one log line. With `--bit` and no `--manifest` nothing is fetched, the check is skipped. `tools/check_versions.py`
  already parsed the dev form (docstring only). The contract is not edited (changes only with the firmware side and Alex). Evidence for S2
  (`tools/test_pack_import_cor.sh`, section 5): `PASS SEMVER=0.3.1-dev+8f1dc184 ... packed as it is`, `PASS SEMVER=0.3.1-dev+deadbeef ... refused`,
  `PASS manifest version 0.3.0 equals SEMVER 0.3.0: packed`, `PASS manifest version 0.4.0 differs from SEMVER 0.3.0: refused, message names both`,
  `PASS manifest without version: packed, one log line`. S1 stays open until a pin bump to a package that carries the field.
- Run 2310 (main 5516eb716, 2026-11-16), package stage: `release/imports/trx64u.pin: manifest of main-8f1dc184 has no version (older package), SEMVER=0.3.0 not checked against it` (same for trx64g); trx64u.cor 3650513 bytes, trx64g.cor 3414513 bytes. S1 stays open until a pin points at a package whose manifest has "version".
**P-2 built (2026-11-16, branch `t230-startfile`).** `start_file_take` (`coreapp/src/start.cc`), page branch unchanged. Without a valid
page it looks at `/SD`, `/USB0`, `/USB1` in that order (`startfile_path` of `/<medium>/x`), and takes a file only when the base name of its
`cor=` equals the descriptor's `core_id`, `cor_name` or `folder` (case-insensitive; trx64u: `cor_name` "TRX64U"; `folder` is "c64", the ROMs'
name, so the folder alone would never match the `.cor`). Any other file is dropped with
`[core] start file <path>: for <cor>, not for this core, dropped`; `cor_path`/`cfg_path` stay. Every file found is deleted, taken or not
(also an unparseable one): the host writes it right before `legacy_jump` (`host_core.cc`, "written last"), so a file that a running
core finds and does not take belongs to a start whose application never deleted it (trx64g); no start is pending that it could still be for.
After a drop the next medium is still looked at. Log lines of the taken case are unchanged. Names of other cores without `core_id` match on
`cor_name`/`folder` only; a page-less start of such a core with a start file whose base name is neither is dropped (was: taken).
Proof: unit test `tests/coreapp/test_startfile.cc` (`run.sh` unit `startfile`, real `start_file_take` over FakeFs, trx64u descriptor):
S3 `/SD` taken, deleted, cfg_path `/SD/cores/trx64u/trx64u.cfg`; S4 `/USB0` taken, cfg_path `/USB0/cores/trx64u/trx64u.cfg`; S4b leftover
`/SD` trx64g file dropped and deleted, `/USB0` file taken, `trx64g.cfg` untouched; leftover alone: nothing taken, cor_path empty, cfg_path
unchanged. Log: `[core] start file /SD/trx64-start.txt: for /SD/cores/trx64g.cor, not for this core, dropped`.
ue2emu (monolith profile, `coreapp.app` of `tools/dev.sh riscv --branch t230-startfile`, sticks as `--usb-dir`; runs in the session scratch `t230/`):
- S4: stick with `trx64-start.txt` (`cor=/USB0/cores/trx64u.cor`, `config=Demo`): `[core] start file: stick wait: a USB mass-storage device is
  enumerating: stick present after 1680 ms`, `[core] start file: no hand-over path, the core folder of /USB0/cores/trx64u.cor is used`,
  `[core] start file /USB0/trx64-start.txt read and deleted: config "Demo", safe 0`, then `[core] settings C64 Settings: /USB0/cores/trx64u/trx64u.cfg`
  (the config name was passed on; the stick has no "Demo" config, so `config_start` said "not found, plain start": the application of a real config
  is not shown here).
- S4b (foreign leftover, on /USB0 because the emulator run has no /SD image): `[core] start file /USB0/trx64-start.txt: for /SD/cores/trx64g.cor, not
  for this core, dropped`, settings stay `/flash/trx64u/c64.cfg`; no trx64g folder is made. The file is gone from the card per the unit test (the
  emulator's stick directory is not written back within the run).
- Not run in ue2emu: S3 (no /SD image in this profile; the unit test covers `/SD`).
**Found while proving it:** at t+300 ms, where `start_file_take` runs, the stack has not found the USB hub yet (ue2emu: hub t+620 ms, stick
t+1700 ms), so without a hand-over page there is no stick to read and the file would never be seen. The page-less start therefore calls
`lib_board_wait_stick` (`libboard.cc`, `lib_wait_for_stick` with an env that says "a device is coming") when `/SD` has no start file: it waits at
most `LIB_MOUNT_WAIT_MS` (4 s) and ends at the first stick root that lists. **Cost: a page-less start with no stick at all (JTAG, a monolith
power-up) now takes up to 4 s longer.** Decided (D6, Alex 2026-11-16): keep the 4 s.
Only one stick root is waited for: a second stick that comes up later is not seen by the same take.
- 2026-11-16 (review fix, D6 scope): the `lib_board_wait_stick` call in `coreapp/src/run.cc` runs only when `board->kind == BOARD_MONOLITH` (and no page): on the MultiCore host every core start fills the page, so a page-less start there is a JTAG/SRAM dev start and never waits 4 s. Not reachable in `test_startfile` (the gate sits in `coreapp_run`, which the unit does not call); the unit still covers `start_file_take` itself.
- S7 (2026-11-16): contract v4 committed on both sides with the same bytes - MultiCore 91f6f3cf6, TRX64-Ultimate 31e69921 (branch u16, pushed with their next batch), `docs/contracts/trx64u-firmware-handover.md` sha256 5be42f9fedd02403...; their manifest "version" field is commit 65775d54. S1 waits for a pin bump to their next green main package.
