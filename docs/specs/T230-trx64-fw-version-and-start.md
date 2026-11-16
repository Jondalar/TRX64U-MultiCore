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
  reads it once and deletes it. Proven in ue2emu with the Amiga 1200 large core. On the monolith the core app never reads the hand-over
  page (T104.2 §5, `has_handover` false), so `start.cc:160` looks only at `/SD/trx64-start.txt`: a `trx64u.cor` on a USB stick gets its
  file written to `/USB0/` and the core app does not find it. **Worse (found by the firmware session, confirmed in `start.cc:151-190`):**
  without a page the core app accepts ANY `cor=` from `/SD/trx64-start.txt` and takes its core folder: trx64g started from `/SD`
  leaves `cor=/SD/cores/trx64g.cor` there (Gideon's application never deletes it); a later trx64u start from `/USB0` reads that
  leftover, sets `cfg_path` to `/SD/cores/trx64g/trx64g.cfg` and writes its `[section]` settings into Gideon's file, which contract
  §4 forbids. The same after a JTAG/SRAM start or a failed write of the start file.
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
- **Q-A:** Where does the autostart target come from: (a) a row in the core's config ("Autostart" = a file + mode), saved with
  "Save as config"; (b) a choice in the host's RIGHT overlay, carried in the start file (`run=`); (c) both?
- **Q-B:** Does P-6 hold: no config choice for trx64g at start?

## §4 Out

- No change of the hand-over page layout or address (contract §3 stays as it is for the page).
- Nothing published to GitHub; releases stay by hand (CLAUDE.md).
- Pending §3a: whether trx64g gets anything (Q-B).

## §5 Implementation log

(empty)
