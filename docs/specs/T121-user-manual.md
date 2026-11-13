# T121 — The repository explains itself to a user: a README and a manual in engineering-manual style

**Status:** DECIDED (2026-10-23, Alex). 2026-11-13 status check: DONE: S1 README + manual and S3 site built and merged (Merge t121-manual, t121-site); todo/row done.
**Repo:** TRX64U-MultiCore — `README.md`, `docs/manual/`, `docs/img/`, the architecture site `.claude/trx64-site/`
(published artifact (internal page)).
**Number:** T121 (TODO table).
**Origin:** Alex, 2026-10-23, after the first GitHub snapshot: "The README makes no sense like this any more, does
it? It is even from before the fork. Make a new one. Goal: MultiCore FPGA firmware for the Ultimate Elite II (and so
the C64U too). What it is; what works; what does NOT work (everything works, haha); that it leaves the *original*
.cfw/.ue2 usable; list of the cores that run and their state (PoC, testing, works); screenshots of the host app with
the boot loader. Then documentation in docs: install, usage, and so on. Update the architecture website, TODOs out,
everything that is a development diary out. Everything in Boeing engineering manual style." (translated)
**Depends on:** T94 (host + cores), T101/T104 (host UI, core apps), T115 (.cfw), T120 (what may be public).
**Milestone:** M3.

---

## §1 The problem

- P1 `README.md` describes the single-core TRX64 Ultimate from before the multi-core fork.
- P2 There is no user documentation: install, first start, core selection, ROMs, full firmwares, recovery, known
  limits are spread over specs, TODO history and release notes (`tools/release_notes_mc.py`).
- P3 The architecture site mixes the architecture with open tasks and dated development notes.
- P4 The texts go to GitHub (T120): no ROMs, no internal hosts, no remarks about people.

## §2 Scenarios

```gherkin
Scenario: S1 a reader who knows nothing finds out what this is from README.md alone
  Then README.md states purpose, applicability (U64 Elite II, C64 Ultimate), what works, what does not, that the
       original firmware (.cfw/.ue2) stays usable, the core list with a state per core and its evidence, and screenshots
  And  every state claim names its evidence (board test, bench, or "not tested on the board")

Scenario: S2 a user installs and uses it from docs/manual/ only
  Then docs/manual/ has numbered chapters: install, first start, host menu and boot loader, cores, ROMs, full
       firmwares (.cfw), recovery/way back, troubleshooting, each with steps, WARNING/CAUTION/NOTE where they apply
  And  each step is checked against the code it describes (file:line in the agent's report, not in the manual)

Scenario: S3 the architecture site shows architecture only
  Then no TODO lists, no dated diary entries, no "open" items remain in .claude/trx64-site/*.html
  And  the published artifact is updated from those files
```

## §3 Decision

Style: engineering-manual (numbered paragraphs, scope and applicability first, WARNING / CAUTION / NOTE blocks,
short imperative steps, Simplified Technical English, no narrative). Content from the code and measured results
only; development history stays in `docs/todo/` and the specs.

## §4 Out

- Rewriting specs, ADRs or `docs/todo/`.
- Public release decisions (T120).

## §5 Built

**S1 and S2 (2026-10-23, branch `t121-manual`).** S3 (the architecture site) is built by another session.

- `README.md` replaced: purpose, applicability, capabilities, limitations, original-firmware compatibility, core table with
  state and evidence per row, screenshots, documentation links, licence (the take-offline sentence of `PROVENANCE.md`).
  A state is not higher than its evidence: VC-20, C16, Apple IIe and others carry the board date and what ran; ZX, CPC and ZX
  Next are PoC.
- `docs/manual/` chapters 00-10, numbered paragraphs, WARNING/CAUTION/NOTE. Chapter 2 and 8 walk the whole install:
  update file, Run Update from a C64 core, "About to update. Continue?", the automatic backup (`TRXBK_<date>_nnn.BIN` and the
  restore bundle `.CFW`, config pages always included, ADR-0050), power cycle, boot menu; restore bundle, "Restore your
  settings from the backup?" (default No), stock `.ue2` path, cable. Board evidence for backup and restore: 2026-10-10
  (`docs/archive/board-tests/AlexTest101026.md`, section 4, passed).
- Screenshots: `docs/img/host-boot-menu-found.png`, `host-boot-menu.png`, `host-edit-menu.png`, `host-network.png` from the
  real host application in the ue2 emulator (`tools/ui_emu.sh --headless --script`, two runs, 960 x 540 pixel layer);
  `core-icons-32.png` is the 32 px contact sheet of `tools/core_logos`. The updater screens (file browser, question, backup,
  "Update OK", restore questions) are not photographed: the figures are marked "to be added" in chapters 2 and 8.
- Found while reading the code: the backup is not a question. The only question before the write is "About to update.
  Continue?"; the backup follows without one. A folder `CFW Files` is not searched, only the root of a medium and `cores`.

Open: photos of the updater screens (figures 2-1 to 2-4, 8-1 to 8-4); a render of the core application's menu (the render bench
`tests/coreapp/render/run.sh` was not run: the builder was busy with CI).

### 5.S3 The architecture site (`.claude/trx64-site/`, branch `t121-site`)

- Pages: `index.html` (frame, four tabs), `overview.html` (Part 1, system description), `arc42.html` (Part 2),
  `bootmenu-mock.html` (Part 3, the boot menu as an operable figure), `floorplan.html` (Part 4). Numbered sections,
  purpose and applicability first, NOTE / CAUTION blocks, short declarative sentences. Every page is self-contained:
  the Google Fonts links are gone, the only URL left is the SVG namespace string.
- Removed: all status tags (Built / Decided / Open), the status-at-a-glance columns, "open" and "not built" lists, run
  and CI numbers, dated entries, board results, the release name, agent and session talk, the internal addresses of
  the mock-up (now documentation-style example data), the floorplan history of runs 909-914.
- Added or corrected from the code: the cores table from every `cores/*/core.toml` (version, application kind,
  picture path, ROM names); the video chain in the order of `host/top.v` (vic_scaler, overlay, pix_layer, c64_text,
  TMDS, GTP) with the two picture paths of a core (palette raster for trx64 and c64u, DDR2 frame buffer for every other
  core, T119); the swap sequence as `coreboot.cc` runs it (the host releases the decoupler before the jump, the
  application only releases the machine reset); the ICAP and register-window map of `ultimate_sys.vhd`; the packet
  port is `pp_*`; libraries are `<root>/trx/lib/*.u2a` files, not bundle parts; the boot menu screens, keys and badges of
  `hostui.cc`; the `.cfw` start of `legacy.cc`; the floorplan B regions of `host/build.tcl`.
- Dropped as not true of the code: "all windows answer 0 while decoupled" (five windows do), "host-checked memory
  window" (the host gates the DDR2 ports and checks no address), core folders `cores/<name>/` with ROMs beside the core
  (the search is `<root>/roms/<folder>` only, the scan goes one folder deep), "REST in the host app" (FTP and a web
  page with JSON; since T192 the host serves REST/WS and the Network screen shows its state).
- The artifact is published by the coordinator from these files (not done here).
- Not verifiable from the code, taken from the pipeline's reports as before: the LUT and block-RAM figures of the
  floorplan page, the 18,000 / 13,000 LUT region limits, the 80 % routing limit. exFAT support of the file system
  (submodule `apps/ultimate` is not checked out in the worktree) is therefore not stated.
