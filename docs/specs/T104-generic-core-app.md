# T104 — every core, the C64 too (D9), runs one generic application that its own bundle describes

**Status:** IN PROGRESS (PROPOSED 2026-10-15, consolidated 2026-10-20). What holds, what is built and what is open: the Current Contract below. 2026-11-13 status check: DONE: todo and row say done (generic app on every DFX core; Reload core and fast seek seen on the board, Alex); T104.1/.2/.3 keep their own rows.
**Repo:** TRX64U-MultiCore — the shared base and the two binaries in `app/`, the descriptor format in
`tools/cor_pack.py` and `coreapp/`, the per-core data in `cores/<name>/`, the build in `ci/trxapps.sh`.
**Number:** T104 (registry: the table in `TODO.md`; T103 was the highest in use).
**Origin:** Alex, 2026-10-15, after an evening in which the five machine applications turned out to be
forks of Gideon's whole application and a sixth fork was nearly added: "Ich will: eine neue App, für den
HOST ... Von gideon kommen nur die Convenience Klasse, die wir sonst selbst schreiben müssten (also der
I/O und Filesystem unterbau) ... lvgl als Output technologie ... so clean wie möglich ... für HOST UND
die Cores. Perspektivisch alle CORES! ... Cores beschreiben was sie von der App brauchen, also welche
Features und Config items in die Menüs müssen. --> Am ende eine generische App die nur mit einer Config
läuft, alle Cores die Selbe App Grunde."
**Depends on:** T93 (the blueprint application and its descriptors, built and on the board), T94 P6 (the
pipeline builds our applications), T84 (the `.cor` bundle format).
**Milestone:** M2.

## Current Contract and Status (2026-10-20)

**What holds now.**
- Gideon's application is replaced for every core, TRX64 Ultimate and the C64 included, no compatibility mode (D9, Alex 2026-10-18). Except the c64u vanilla core (T103): it stays on Gideon's application (Alex 2026-10-21: "The generic app is for trx64. The C64U core stays on Gideon's app please.", translated). One generic core application; the menus result from settings, a declarative description, libraries and applications; libraries load dynamically; the application lives in this repository.
- Two binaries on one base, the host does not move when a core application changes (D4).
- Nothing is written from scratch where a built piece exists (D8).
- Decided 2026-10-20 (Alex): the Amiga and the A1200 move onto the generic application, "Yes, but please follow the architecture! So library for what is core specific, app for what makes sense as an app, and so on" (translated); an open core menu is Freeze for every core, "Core menu open == Freeze. I do not want this overlay stuff!" (translated), so there is no Overlay mode; the application always lays out for 1920 x 1080, "No, simply 1080p always" (translated), whatever the Picture setting says (T107/ADR-0057 get amended).
- D5 (the C64 keeps Gideon's application) is superseded by D9; the original is in the Implementation Log.

**What is built, tested, merged** (checked against `git log main` and `git branch --contains` on 2026-10-20; everything below is on `main`, there is no unmerged T104 branch with commits of its own: `t104-amiga` and `t104-geometry` carry none beyond `main`; `tests/coreapp/run.sh` was run on `main`'s tree: 30 test programs passed, every negative control failed as it must):
the shared view layer (`app/view/`), the core application's menu as a route, the cheap-redraw path, the CBM PRG loader library, operations by name with registry, executor and poll task (T104.1), the library loader and its start (T104.1), the C64 descriptor and the monolith profile (T104.2), the Apple II descriptor and Disk II library (T89), 64 bit file sizes (T104.1 D16).

**What is open.**
- The descriptor is still a compiled C table: no TOML compiler, no description or library record in the `.cor` (`tools/cor_pack.py` has four record types), no menu merge (T104.1 D8, D14.3).
- The A500 and `trx64` are not moved (`cores/{amiga,trx64}/core.toml` say `app = "ultimate"` or a Gideon-base build); the VC-20 and the C16 are built on branch `t104-vc20-c264` (2026-10-22, below): `cores/{vc20,c264}/core.toml` say `app = "blueprint"` and `ci/trxapps.sh` packs the generic app for `atari800 nes apple2 zx cpc vc20 c264 amiga1200`; both lack the 1541 (`cbm1541`, §5.4). `c64u` stays on Gideon's application (see above).
- The always-1080p layout is built and on main (d3db140a, T107 5.6): `video_frame_size()` returns 1920 x 1080 for every mode; bench `test_picture` 326 checks, 0 failed. No board run yet.
- Board: the cut-off UI and the misplaced NES picture (2026-10-19, photos) are not fixed; the generic application on `main` has had no board run for them.
- REST and network from T93 (T104.1 Q2, Q8).
- Open decisions for Alex: none; points 1-10 decided 2026-10-20 (table below).

Open (one line, as in the TODO table): Base, ops, loader, C64, Apple II, Amiga/A1200 on main; VC-20, C16, cbm1541 built 10-22 (no board); descriptor still C

| Item | Decided | Implemented | Bench | RISC-V | Board | Merged |
|---|---|---|---|---|---|---|
| D1 Gideon's code is a library; host and core apps are ours | yes, Alex 10-15 | yes (`host-app/`, `coreapp/`) | yes (`tests/hostapp`, `tests/coreapp`) | yes (CI `trxapps`) | yes (host app; NES/Atari core app, 10-16) | main |
| D4 two binaries on one base (step 1) | yes, Alex 10-15 | part: shared view layer and palette; host shell deliberately not moved | yes (`tests/view` 13 checks, render benches) | yes | part: UI cut-off and sluggish on blueprint-era apps (10-19) | main |
| View layer 5.8, 5.10 (route, skins, cheap redraw) | yes, Alex 10-15 | yes | yes (cheap path: 52,768 px per key against 355,656, control fails) | yes | part: not re-checked after the cheap path or the scroll fix of 5.11 | main |
| CBM PRG loader library (5.6 item 1) | yes | yes (`app/loaders/cbm_prg.cc`) | yes (`test_cbm_prg`) | not evidenced | no | main |
| D3 descriptor as data (step 2) | yes, Alex "B" | no: operations by name done (T104.1 D14.2); TOML compiler and `.cor` record not | ops yes, data no | - | - | ops part on main |
| Rest of the loader library (5.6 items 2-4, 6, 7) | yes | no (Disk II landed with T89) | - | - | - | - |
| VC-20 (step 4) | yes, Alex 10-22 ("apart from trx64 and c64u, switch everything to the generic core app for now") | yes: `coreapp/cores/vc20.cc` with the shared PRG job `cbm_run.cc`; the 1541 (`cbm1541`) is not built, so no disk | yes (`test_vc20`, 20 controls) | not built here (no RISC-V compiler in this session): CI `trxapps` | no | branch t104-vc20-c264 |
| C16 (step 5) | yes, Alex 10-22 (same) | yes: `coreapp/cores/c264.cc` with `cbm_run.cc`; cartridge images, no 1541 | yes (`test_c264`, 25 controls) | as above | no | branch t104-vc20-c264 |
| Amiga and A1200 (step 5) | yes, Alex 10-20 | yes: two descriptors, `coreapp/cores/amiga.cc`; the A1200 `.cor` ships it (`ci/trxapps.sh`), the A500 keeps its Gideon app | yes (coreapp suite) | yes | no: A1200 bitstream with it not routed yet (run 912 cancelled) | main |
| Open core menu == Freeze, no Overlay | yes, Alex 10-20 | holds: `run.cc` freezes on menu open, no Overlay mode in `coreapp/`; T104.1 P6, D6, S8 still described Overlay (corrected) | `test_freeze`, `test_service` | yes | NES/Atari menus freeze | main |
| App always lays out 1080p | yes, Alex 10-20 | yes (`video_frame_size()`, d3db140a) | yes (`test_picture`) | yes | yes: NES/Atari menu fits (Alex, photo, 10-20) | main |
| C64 and TRX64 Ultimate on the generic app (D9, D10) | yes, Alex 10-18 | part, see T104.2 | see T104.2 | see T104.2 | no | main |
| Step 6: `trxapps` builds two binaries, no machine app | yes | no | - | - | - | - |
| Step 7: the fork repository goes | yes | yes, 10-15 (per docs/todo/T104.md) | - | - | - | - |

### The ten points of 2026-10-20 (same wording in T104, T104.1 and T104.2)

Alex asked on 2026-10-20 that these ten be clarified in all three specs. A point carries an Alex decision where one is quoted; where specs contradict it is an open decision; otherwise it is PROPOSED and not decided.

| # | State | Rule | Evidence |
|---|---|---|---|
| 1 | DECIDED 2026-10-20 (Alex: "Do not start. Error and back to the host.", translated); loader not yet changed | A required library that is missing or too old keeps the core held, with a message naming library and version; an optional library may degrade visibly (the menu and the Home line say what is missing). | T104 D6 and T104.1 D11 say the core stays held; T104.1 5.5 built the opposite ("a refusal does not stop the core"), and no description names libraries yet, so required and optional cannot be told apart. Alex's Q10 ("1. mit Fehlermeldung") covers only a library whose capability the core lacks: refused, with an error message. |
| 2 | DECIDED (Alex, 2026-10-20: "Yes", to the list he set) | State values and events are published as atomic, immutable snapshots; no reader sees an unprotected or half-written value. | T104.1 5.2 documents the gap: `OpRegistry::state()` runs unguarded on the reader's task, and the revision counts reads, not changes. |
| 3 | DECIDED (Alex, 2026-10-20: "Yes", to the list he set) | Stable F-key and digit sequences must not depend on library load order or USB root order. | T104.1 D8 orders library nodes by order key, then load order; 5.5 loads by path over `/usb0..3`, so a stick moved to another port can renumber a contributed tab. T104 5.8 (published numbers are compatibility identifiers) is the rule at risk. |
| 4 | DECIDED (Alex, 2026-10-20: "Yes", to the list he set) | External native libraries need a trust/integrity model (who may provide one, how its content is verified before it runs) and deterministic duplicate-provider rules (one fixed winner, named in the log, independent of root order). | Today a library is native code without sandbox or signature (T104.1 5.3); the same name on two sticks is resolved by path order (5.5). |
| 5 | DECIDED (Alex, 2026-10-20: "Yes", to the list he set) | Board detection and capabilities are three-valued: present, absent, unknown. Unknown hardware is not assumed to be the monolith; an unknown capability is neither silently offered nor silently struck, it is shown. | T104.2 D2 (ICAP byte 0x7A is the MultiCore host, anything else the monolith) and D3 (`sid_sockets` answered by the profile, `uci` kept when the probe cannot tell) are two-valued. |
| 6 | DECIDED: location (Alex, 2026-10-19), atomic rewrite (Alex, 2026-10-20: "Yes") | The library crash counter must never endanger `/Flash/boot.cfg` through a non-atomic rewrite (write a temporary file and rename, or keep the counter in a file of its own); until then the exposure stays documented. | Alex, 2026-10-19: "let us start with it in the boot.cfg" (translated), the alternative of a separate one-key file kept open if the power-loss window (a whole-file rewrite, no temp + rename) turns out to matter (T104.1 5.5). Built as a whole-file rewrite, two per start that loads a library. |
| 7 | DECIDED (Alex, 2026-10-20: "Yes", to the list he set) | No per-core code without proven need; shared behaviour lives in libraries. (Replaces "the middle layer stays empty".) | T104 D2, T104 S8 and docs/todo/T104.md carried the old wording; the old text is kept in T104's Implementation Log. |
| 8 | DECIDED (Alex, 2026-10-19, T112 D1; monolith 2026-10-20: "/flash/roms wins", as built) | ROM search order: (1) `roms/` beside the core, in the folder its `.cor` lies in; (2) the board's own ROM directory (the monolith's `/flash/roms`); (3) `<root>/roms/<folder>` of `/flash`, `/usb0`-`/usb3`, `/sd`, first hit wins. | Alex, T112 D1: "No, then we do it beside the core. That would be good for the generic app. The search over several trees is good, we keep that. But the first location is then beside the core" (translated). Steps 2 and 3 are the code as built (`romfind.cc`); step 1 is not built (T112 is spec only). T104.2 D8 said a stick can override, T104.2 5 says the flash wins as built: the D8 sentence was wrong. Open for Alex: should a stick beat `/flash/roms` on the monolith, where there is no core folder? |
| 9 | DECIDED (Alex, 2026-10-20: "Yes", to the list he set) | Before the first flash write to 0x220000, the `.cfw` rollback from our own application (`update.run_record`) must work on the board. | T104.2 Q4 is open; Alex deferred it with S13 until just before S13 (Q3). `update.run_record` is not built. |
| 10 | DECIDED (Alex, 2026-10-20: "Yes", to the list he set) | Canonical operation names and the typed result contract are defined once, bindingly, in T104.1 section 3 H; every other spec links there and does not restate names. | Today three spellings meet: D1's `machine.reset(kind)`, T104 5.9's `op = "cold_reset"` and the registered `machine.cold_reset`; and `prg.run` against `c64.run_prg`. |

Where each point bites in this spec:

- 1, decided 2026-10-20 (see the table), was: D6 ("if it is missing the core does not start") and S2 (a descriptor asking for an unknown loader holds the core) say held; T104.1 5.5 built "carry on".
- 3: 5.8 (areas and tabs are descriptor entries, numbers are compatibility identifiers).
- 5: D3 (`capability`), 5.2 (`capability` entry).
- 7: D2 and S8 (rewritten 2026-10-20; the old text is in the Implementation Log).
- 8: 5.1 (`rom_set` rules), D3 (ROM set in the record); the order itself is T112 D1 and T104.2 D8.
- 10: 5.9 names its operations `cold_reset`, `reload_rom_set`, `run`, `load`, `mount`: those are descriptor sketch names, not registered names (T104.1 section 3 H).


---

## §1 The problem

**Who:** Alex, when he adds a core or changes a menu; the coordinator, when a release has to carry seven
applications; anyone who has to find out where a machine's behaviour is written.

**What he is trying to do:** add a machine to the MultiCore line, give it the menus and settings it
needs, and ship it in a release - without touching five other machines or a foreign repository.

**What happens today.** Two different worlds answer that:

- **T93's blueprint application** (`coreapp/`): our code on Gideon's substructure, LVGL for the picture,
  one image for every core, the core chosen at start from its own ID block. The Atari 800 XL and the NES
  run on it. Its per-core part is a C descriptor compiled into the application (`coreapp/cores/nes.cc`,
  137 lines; `atari800.cc` with the SIO/ATR/XEX modules beside it).
- **The five others** (trx64, vc20, c264, amiga, amiga1200) run Gideon's **whole** Ultimate application,
  with their machine layer and their menu trimming added as `#ifdef` branches: `VC20_APP`, `C16_APP`,
  `AMIGA_APP`, `AMIGA1200_APP`. Those live in branches of the 1541ultimate TREX fork - 283 commits above
  `trex/main` for the VC-20 line, about 200 changed source files - and are built on a Mac by hand.

The second world is the problem, and it shows in four ways:

- **P1 Ownership.** A menu change for the VC-20 is a commit in a foreign repository, in a tree of ~17,000
  files that is Gideon's application plus our deltas. Alex: "Die Apps *müssen* zu uns." Moving the fork
  here moves the ballast with it, including his CI, which started a Nios II build the moment the branch
  was pushed.
- **P2 Duplication.** Five machines carry five copies of everything an application does - file browser,
  settings, network, flash - differing only in `#ifdef`s. A fix in one is a fix in none of the others.
- **P3 The core cannot say what it needs.** Nothing in a `.cor` describes its machine. The application
  has to know the core, so a new core is an application change, and a core and its menus are versioned
  apart.
- **P4 Build and release.** Six of seven applications are built outside CI and uploaded by hand into the
  generic package `trx64-coreapps`. On 2026-10-15 that put an application into a release that nobody had
  built from the commit the release names, and a `.app` header into two bundles whose cores then executed
  their own load address.

**Why it matters:** every new machine costs a fork today, and the line Alex wants - ZX Spectrum, CPC,
Apple II, Plus/4, more - multiplies that cost. It is also the reason a release cannot be trusted to carry
what its commit says.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 a core brings its own menus
  Given a .cor whose descriptor record names two settings, one task and the file type ".atr"
  When  the application starts with that core in the FPGA
  Then  the Machine page shows that task, the Settings page those two settings with their values
  And   the browser offers ".atr" files and no other type
  And   nothing in the application's source names this core

Scenario: S2 a descriptor the application cannot serve is refused, by name
  Given a .cor whose descriptor asks for the loader "sio9" and a schema version one above ours
  When  the application starts with it
  Then  it stays held, the screen says which loader and which version it does not know
  And   it does not start the machine

Scenario: S3 the same image serves every core
  Given the bundles of the VC-20, the C16, the Amiga, the A1200, the Atari and the NES of one release
  When  their application records are compared
  Then  all six are byte-identical
  And   each bundle's descriptor record differs

Scenario: S4 host and core share a base, not a binary
  Given the two binaries of one build
  When  the shared library is changed in one line and both are rebuilt
  Then  both change, and neither carries the other's main program
  And   the host image contains no core descriptor parser

Scenario: S5 a core builds without the application
  Given a change in cores/vc20/ only
  When  the pipeline runs
  Then  the core stage rebuilds that core, the application stage reports "from cache"
  And   the release carries the new partial with the unchanged application

Scenario: S6 an application change repacks every bundle
  Given a change in app/ only
  When  the pipeline runs
  Then  no core is routed again and every .cor is packed anew with the new image
  And   each bundle's hash changes while its partial's hash does not

Scenario: S8 no per-core code without proven need (decided 2026-10-20, point 7)
  Given the bundles of the VC-20, the C16, the Amiga, the A1200 and the NES
  When  each is examined for a module record
  Then  none has one, and each starts from its descriptor alone
  And   the Atari's bundle is the only one with a module, for its SIO protocol

Scenario: S7 the machines keep what they had (the negative control with teeth)
  Given the VC-20 application of 2026-10-13 and the generic application with the VC-20 descriptor
  When  both are driven through the same menu walk on the board
  Then  the generic one offers no entry the old one hid, and hides none it offered
  And   a deliberately wrong descriptor (a setting pointing at the wrong register) makes the walk fail
```

| # | Solves | Scenario | Test (bench / board step) | Where it runs |
|---|---|---|---|---|
| S1 | P3 | a core brings its own menus | `tests/coreapp/test_descriptor.cc` on a fake machine | `ci/lint.sh` |
| S2 | P3 | unknown loader / version refused | same bench, negative case | `ci/lint.sh` |
| S3 | P2 | one image for all | `tools/release_check.py` over the release bundle | CI `release` |
| S4 | P2 | two binaries, one base | sizes and symbol lists of the two images | CI `trxapps` |
| S5 | P4 | core alone | the stage log of a core-only push | CI `pipeline` |
| S6 | P4 | application alone | bundle hashes of two runs | CI `package` |
| S7 | P1, P2 | no menu regression | menu walk on the board, photo per machine | board session |
| S8 | P3 | five cores need no code | `tools/release_check.py` over the bundles of a release | CI `release` |

The control that has to bite is S7's second half: a descriptor whose setting names the wrong register
must make the walk fail. A descriptor test that only ever passes proves nothing (T88 §5.10).

## §3 Decision

**D1 — Gideon's code is a library, not an application (Alex, 2026-10-15).** "Von gideon kommen nur die
Convenience Klasse, die wir sonst selbst schreiben müssten (also der I/O und Filesystem unterbau)." We
keep FreeRTOS, USB, SD/FatFS, the file-system layer, the flash drivers, lwIP/FTP, ITU and I²C. We do not
keep his user interface, his menus, his machine classes or his file-type handlers. That is already true
of the host application (`host-app/software/hostapp/`, our own, his UI not linked) and of the T93
blueprint; T104 makes it true for the rest.

**D2 — Three layers; no per-core code without proven need (Alex, 2026-10-15; the wording of the middle layer decided 2026-10-20, point 7).** The first cut put
"behaviour" in one box; Alex took it apart again - "Aber das sind doch core-spezifische Capabilities .. die
muss der Core mitbringen" - and the answer is that two different things were in that box:

| layer | example | where it lives | why |
|---|---|---|---|
| file formats | `.hdf` geometry, `.atr` sectors, `.nes` header, `.d64` tracks | library in the application | a format belongs to no core: `.hdf` serves the A500 and the A1200, `.d64` the C64, the VC-20 and the C16. In the core it would be written two, three, four times |
| core protocols | the Atari's SIO, live on that core's registers | a module in the bundle, only when it cannot be declared | genuinely core-specific, and the only honest reason for code to travel with a core |
| description | menus, settings, ROM set, media slots | data in the bundle (D3) | the core says what it needs |

**No per-core code without proven need; shared behaviour lives in libraries (decided, Alex 2026-10-20, point 7).** The partition
contract already standardises reset, the RAM and ROM windows and the frame buffer; when the descriptor may also say
*this file to DDR2 at offset X, then write register Y*, a core needs code only where a need is shown. A shared
behaviour (a file format, the 1541 drive) is a library, never a copy in a core. The original wording of this
paragraph is in the Implementation Log.

The loser of this weighing is the earlier plan to keep a per-core C module in the application tree
(`coreapp/cores/nes.cc` as it is today): it makes the application describe the cores again, and every new
machine an application commit.

**D3 — The descriptor is a record in the `.cor`, versioned (Alex: "B").** It carries identity, video
geometry, settings (name, values, when applied, which register), tasks, file types with their actions,
media slots, the freeze behaviour and the **ROM set** (file name, size, where it goes in the core's ROM
window, optional or not - the C16 needs four); it names the format loaders it wants. `tools/cor_pack.py`
validates it when packing, the application refuses what it cannot serve and says so (S2). A core that is
purely declarative needs no application change at all.

How far the declarative part has to reach is the open design question, and it is answered by building the
five descriptors, not by guessing: the C16's ROM set and the Amiga's media slots are the two that decide
whether a third kind of entry is needed.

**D4 — Two binaries on one base (Alex, 2026-10-15).** Weighed against one image with a role switch
(lost: every bundle would carry the boot menu, and a fault in the host role would travel into every
bundle). The shared library holds the UI, the menu engine, the loaders and the file-system layer; the
host binary and the core binary are thin main programs on top. The host is what rescues Alex from a
broken core, so it must not move whenever a core application changes.

**D5 — superseded by D9 (2026-10-18).** The C64 does not keep Gideon's application. The original decision, with Alex's quote, is in the Implementation Log.

**D6 — One plug-in mechanism, used for two things (Alex, 2026-10-15).** "Perspektivisch alle CORES" and
"wir erlauben bei TRX64/C64 Cores eine Ausnahme - die sind unsere App Struktur und erlauben das Ausführen
von Apps auf der App Loader ABI mit dem Interface gegen Gideons API. Als Zwischenschritt .. und dann kann
ich meine Apps sukzessive migrieren auf unsere ABI/API. Sind ja nur Cartridge Tools und Monitor im
Grunde." Alex on using the same ABI for core modules: "die Idee die u2a App ABI dafür zu nehmen mag ich
besonders! Muss dann aber vorinstalliert sein oder halt wie eine Lib dynamisch gezogen werden."

The services are the same - file, machine, memory, screen - and that is the reuse worth having. The
lifecycle is not, and the differences are what the design has to carry:

- **When it is loaded.** An application starts when a person picks it. A core module has to be there
  before anything happens, so it is **neither preinstalled nor fetched: it is a record in the `.cor`**
  and is loaded when the core is. Nothing to install, no path to resolve, no version to drift; if it is
  missing the core does not start and says so.
- **Who calls whom.** An application calls the API and returns. A module is called: on mount, on poll, on
  a task. That inversion is the real extra work over the application loader - registration and a fixed
  set of callbacks instead of one entry point.
- **Lifetime.** A crashing application is over; a crashing module takes the session. So: one module per
  core, loaded at start, no reloading, no unloading, and a defined way to fail.

Nothing of this is built in T104 beyond the boundary and the refusal path; the C64 migration (D5) and
Alex's own tools come later through the same door.

**D7 — The fork is not the way.** The branch merged on 2026-10-15 (`t90-c16app` plus the Amiga's 14
commits, four applications from one tree, all four built) and the repository `TREX/trx64-coreapp-fw` it
was pushed to are superseded by D2: the four machines become descriptors and modules here, and the fork
is not maintained. What it is still good for is reading - it is where the menu trimming of each machine
is written down, and S7 compares against exactly those builds.

**D8 — Reuse by default, rebuild only with a named reason (Alex, 2026-10-15).** "nicht trennen ..
aufräumen, zusammen führen/wieder verwenden und mein Ziel erreichen", and: "wenn T104 sinnvoll und besser
als das jetzige neu bauen kann, ok. Ansonsten nicht alles neu erfinden." T104 is a consolidation of two
applications that already exist, not a new one. Every piece is decided on its merit:

| piece | today | T104 | why |
|---|---|---|---|
| file-system layer, settings with `.cfg`, ROM finding, video service, freeze, boot sequencing | `coreapp/src/` | **reuse**, moved into the base | built, tested, on the board |
| format loaders: iNES, ATR, XEX, CAR, SIO | `coreapp/cores/`, `coreapp/src/` | **reuse**, moved into the loader library | the library D2 asks for is these files in one place |
| host logic: discover, `.cor` reading, ICAP boot, boot config, hand-over | `host-app/software/hostapp/` | **reuse**, moved into the host binary | the part that rescues Alex from a broken core; it does not change |
| menu engine | `coreapp/src/menuctl.cc` | **reuse the logic, replace the binding** | pages, selection, windowing and the chooser stay; what changes is that a setting's action is a named operation from the descriptor instead of a C function pointer (D3) |
| view layer | two: `coreapp/src/ui.cc` (7 KB) and `host-app/software/hostapp/hostui.cc` (68 KB), same palette, same look, written twice | **one, rebuilt out of both** | this is the duplication the consolidation is for. The host's half brings the proven machinery - the row window, icons, the render bench with its 38 checks and PNG goldens (`tests/hostapp/render/`); the core's half brings the shape that a descriptor menu needs. Neither survives whole |
| descriptor | C structs compiled into the application | **rebuilt as data** | D3; there is no way to keep the structs and have the core describe itself |
| per-core C modules | `coreapp/cores/nes.cc`, `atari800.cc` | **replaced by descriptors**, except the Atari's SIO | D2 |

Nothing else is rewritten. A piece not in this table is moved, not touched.

**D9 — Gideon's application is replaced for every core, the C64 and TRX64 Ultimate included (Alex,
2026-10-18).** Decided in a discussion led by the TRX64-FW session (TRX64-Ultimate) and relayed from there;
Alex: "T104: Sie auf main bitte!" (record it in T104 on main). Supersedes D5 at the end of the transition,
and on the TRX64-Ultimate side its ADR-0056 ("stay with Gideon's app"), which that session supersedes there.

1. **Full replacement, no compatibility mode.** Gideon's application goes for TRX64 Ultimate too; the gaps
   are fixed one by one. "Dann lassen wir den Compat Kram halt weg" (then we leave the compat stuff out).
   The safety net is Gideon's original firmware staying installable, not a mode in ours.
2. **One generic core application for all cores** - settings, media, browser, update, the app ABI. What is
   core-specific lives in libraries that only the cores needing them use: 1541/IEC with GCR for the C64,
   the VC-20 and the C16; the Amiga uses none of them.
3. **The menus are not programmed, they result** from (a) the settings, (b) a declarative description
   (structure, order, groups), (c) the libraries (a library contributes its menus, file actions and
   settings) and (d) the loaded applications (entries through the app ABI). Gideon's application is not a
   model: it does not and cannot do (b), (c) or (d). His code is at most a source of library code (GCR,
   FatFS, USB), never the structure.
4. **Libraries load dynamically**, through the same door as the `.u2a` applications: "Dynamisch". This is
   D6's mechanism extended by registration (menus, file actions, settings); it is not a build-time
   composition.
   **Revised (Alex, 2026-10-28, translated):** "Well, what are libraries ...?! It lies in the flash anyway, in the app space?
   Then it can be statically linked; if something like the TRX-CRT app is used only synchronously and in part, it can be
   loaded on demand ... so we should also have `apps/` in the core folder and put such things there." -> What a core always
   needs is linked into its application when the `.cor` is built (the pipeline builds the core-specific application from the
   core's description); what is used now and then (the cartridge tools, the monitor, ...) is an application loaded on demand
   from `<medium>/cores/<core>/apps/` (T148's core folder gets `apps/`).
5. **The generic application lives here** (`app/`, `coreapp/`), not in a repository of its own: "b".
   TRX64 Ultimate consumes it from this repository's CI (a pinned build or package; the mechanism is open).
6. **Who builds it.** The TRX64-FW session drives the full replacement for TRX64 Ultimate - done cleanly
   with libraries, applications and the declarative description, it holds every building block the
   MultiCore TRX64 and C64U cores need. It works in its own clone of this repository on its own branch;
   this repository's coordinator reviews and merges, and overlaps in `app/`, `coreapp/` and anything that
   changes `host-app/`'s behaviour are agreed first. T103 stays as it is: its application uses only
   functions Gideon's application already has.
7. **Day 1**, before the generic application replaces Gideon's on TRX64 Ultimate's main: firmware update
   and flash; browse files and start `.d64`/`.crt`/`.prg`; reset, freeze and leaving every state by menu;
   network and REST enough for tests and deploy; Timewarp; the monitor. Alex found the list "too small"
   without the last two.

**D10 — The first real library targets TRX64 Ultimate directly (Alex, 2026-10-18, relayed by TRX64-FW).** "volles
Rohr auf TRX64" (full throttle on TRX64), no VC-20 step first. Order: (1) the generic application boots on the TRX64
Ultimate monolith bitstream - a `CoreIo`/board profile for the monolith instead of this repository's host (no
hand-over, no DFX, Gideon's core register map, its own flash and boot path); (2) a C64 descriptor (ROMs, reset, PRG
start); (3) `cbm1541`. Sub-spec T104.2 (TRX64-FW). **The c64u vanilla core (T103) is not part of this:** it keeps
Gideon's application, for C64U/UE2 users (Alex 2026-10-21: "The C64U core stays on Gideon's app please.", translated).

### 5.9 The VC-20 as a descriptor, written out (design, 2026-10-15)

The `op = "..."` names in this sketch (`cold_reset`, `reload_rom_set`, `run`, `load`, `mount`, `load_rom`, `set_rom`) are sketch names, not registered ones; the registered names are fixed in T104.1 section 3 H.

The open question of §5.1 was answered by writing a descriptor, not by arguing about fields. This is the
VC-20's, every value taken from §5.1/§5.2 with the fork line beside it in the measurement, and the route
block from §5.8. **Nothing reads this yet** - it is the shape the format has to carry, and the input to
step 2 of the plan. The C16, the Amiga and the A1200 follow it; where one of them needs a field this one
does not have, the field is added here first.

```toml
[core]
name          = "vc20"                 # the key: the hand-over name, not the ID block
title         = "VC-20"
id            = "VC20"                 # sanity check only (reg 0x000..0x003)
id_mismatch   = "report"               # or "refuse"; S2 and S7 disagree, §5.7
contract      = 1
reset_ms      = 20
reset_reg     = { reg = 0x000, hold = 0x04, release = 0x08 }
paused        = { reg = 0x0A4, mask = 0x02, timeout_ms = 100 }

[video]
mode = "core_direct"                   # it has no frame buffer: cores/vc20 has no FB register at all

[capability]
has   = ["drive_a", "keyboard", "joystick"]
pins  = { system_mode = { value = "PAL", one_shot = true } }   # the host setting this core forces

# The route the menu is built from (§5.8). An area is a function key, a tab a number key.
[[area]]
key = "F1"; title = "Home";    tabs = [{ n = 1, title = "Home", view = "home" }]
[[area]]
key = "F3"; title = "Library"; tabs = [{ n = 1, title = "Browse", view = "browser" }]
[[area]]
key = "F5"; title = "VC-20"
tabs = [{ n = 1, title = "Summary", view = "summary" },
        { n = 2, title = "Memory",  view = "settings", group = "memory" },
        { n = 3, title = "Drives",  view = "media" }]
[[area]]
key = "F7"; title = "Actions"; tabs = [{ n = 1, title = "Quick", view = "tasks" }]
[[area]]
key = "F6"; title = "Settings"                       # the host's own pages, by capability
tabs = [{ n = 1, title = "Video", view = "host:video" }, { n = 2, title = "Audio", view = "host:audio" },
        { n = 3, title = "Input", view = "host:input" }, { n = 4, title = "Network", view = "host:net" }]
[[area]]
key = "F8"; title = "System";  tabs = [{ n = 1, title = "Status", view = "host:status" }]

[rom_set]
folder          = "roms/vc20"
roots           = ["/flash", "/usb0", "/usb1", "/usb2", "/usb3", "/sd"]
one_directory   = true                 # all three from the same directory, never mixed across roots
retry_ms        = { missing = 500, refused = 5000, tell_after = 10000 }

  [[rom_set.member]]
  role   = "CHAR"
  names  = ["char.bin", "chargen.bin", "chargen", "characters.bin",
            "chargen-901460-03.bin", "chargen-901460-02.bin"]        # six: ROM_NAMES_MAX 4 is too small
  sizes  = [4096]
  target = { kind = "aperture", base = "CHARROM" }
  verify = true
  [[rom_set.member]]
  role   = "BASIC"
  names  = ["basic.bin", "basic", "basic-901486-01.bin"]
  sizes  = [8192]
  target = { kind = "aperture", base = "BASIC" }
  verify = true
  [[rom_set.member]]
  role   = "KERNAL"
  names  = ["kernal.bin", "kernal", "kernal.901486-07.bin", "kernal.901486-06.bin"]
  sizes  = [8192]
  target = { kind = "aperture", base = "KERNAL" }
  verify = true

[[setting]]
key    = "Memory Expansion"
group  = "memory"
when   = "reset"
values = ["Unexpanded", "+3K", "+8K", "+16K", "+24K", "+3K +24K", "+3K +24K +8K at $A000"]
bytes  = [0x00, 0x01, 0x02, 0x06, 0x0E, 0x0F, 0x1F]        # b0 3K@$0400, b1 8K@$2000, b2 @$4000, b3 @$6000, b4 @$A000
reg    = 0x0A1
default = 0

[[setting]]
key        = "Block 5 ($A000), when expanded"
group      = "memory"
when       = "reset"
values     = ["RAM", "ROM (read only)"]
bytes      = [0x00, 0x10]
reg        = 0x0A2
default    = 0
depends_on = { setting = "Memory Expansion", value = 6 }   # inert for the other six, and the menu says so

[[task]]
label = "Reset VC-20"
op    = "cold_reset"
closes_menu = true
[[task]]
label = "Reload ROMs, Reset"
op    = "reload_rom_set"
closes_menu = true
may_abort   = true                     # a missing ROM reports the reason and does not touch the machine

[[file_type]]
ext    = [".prg", ".p00..p99"]
loader = "cbm_prg_basic"
  [file_type.loader_args]              # the ten numbers that make this loader the VC-20's
  basic_starts  = [0x1001, 0x0401, 0x1201]
  txttab        = 0x2B                 # pointer pair; plausibility window below
  txttab_window = [0x0400, 0x2000]
  end_pointers  = [0x2D, 0x2F, 0x31]
  kbd_buffer    = 0x0277
  kbd_count     = 0xC6
  ready_ms      = 2500
  p00_header    = 0x1A
  size_range    = [3, 0x10020]
  actions = [
    { label = "Run",          op = "run",   closes_menu = true },
    { label = "Load",         op = "load",  closes_menu = true },
    { label = "DMA",          op = "load",  closes_menu = false },
    { label = "Mount & Run",  op = "run",   closes_menu = true, parent_ext = ".d64", mount_first = "iec:drive_a" },
  ]

[[file_type]]
ext    = [".d64", ".g64"]
loader = "cbm_disk"                    # library, with the drive service (§5.4)
actions = [
  { label = "Mount Disk",           op = "mount", slot = "iec:drive_a", mode = "rw" },
  { label = "Mount Disk Read Only", op = "mount", slot = "iec:drive_a", mode = "ro" },
  { label = "Mount Disk Unlinked",  op = "mount", slot = "iec:drive_a", mode = "unlinked" },
]

[[file_type]]
ext    = [".bin", ".rom", ".64c"]
loader = "raw"
actions = [
  { label = "Load Drive ROM",   op = "load_rom", slot = "iec:drive_a", size_in = [16384, 32768] },
  { label = "Set as 1541 ROM",  op = "set_rom",  slot = "iec:drive_a", size_in = [16384, 32768] },
]

[[media_slot]]
id    = "iec:drive_a"
kind  = "library"                      # the drive is library code; the core knows nothing about it
drive = { type = "1541", multi_mode = false, images = [".d64", ".g64"] }
```

What writing it settled:

- **The route block is small and it is per core.** Six areas, eleven tabs, no Timewarp and no Tools -
  those are the C64's. A machine with nothing to say in an area simply does not list it, and the
  function key then does nothing, which is what the prototype's grammar already allows.
- **`group` ties a setting to a tab.** Without it the descriptor would need a second list to say where a
  setting appears; with it the Memory tab is "every setting whose group is memory".
- **A file action names a slot, not a drive number.** `iec:drive_a` is a media-slot id, so the same
  action text works for a machine whose slot is a window in DDR2 (the Amiga's DF0) - the difference
  stays in the slot, not in the action.
- **The loader's ten numbers are the only machine-specific part of the PRG path.** Everything else
  (`relink`, the pointer walk, the keyboard-buffer trick) is byte-identical between the VC-20 and the
  C16 (§5.6, point 1), so the parameter block is what makes the loader library code.
- **Two fields in `[core]` have no equivalent today** and both came out of §5.2: `id_mismatch`, because
  S2 and S7 want different things (§5.7), and `paused`, because the freeze bit is a core register while
  the stop register is the host's.

### 5.10 Built: the shell, and the core application on it (2026-10-15)

Alex: "Nimm dir das Design, Bau für alle Cores die App so wie spezifiziert." The first two steps are in.

**`app/view/`** is the shared view layer (D8's named rebuild number two), and it is the prototype made
real rather than reinterpreted: the geometry and palette are `lvgl_ui_project/globals.xml`'s consts
(sidebar 148, top bar 36, tab strip 38, footer 28, spacing 2/4/6/10/18/28, radius 4, the ten colours),
on the 960 x 540 canvas our pixel layer already has. The host application carried that palette and those
primitives (`box`, `txt`) already, so that part is a move. `TRX_SKIN_TERMINAL` is the second prototype as
a table of colours and two fonts - one navigation engine with a style table, which is what 5.8 promised.

**The menu tree is a route.** `menu_build()` now produces areas with a function key and tabs with a
number: F1 Home (the information lines), F3 Library (the browser), F5 the machine (Summary = its media,
Settings = its settings), F7 Actions (its tasks), F8 System, shifted (the two fixed entries). It is the
same entries as T93's four pages, regrouped - and `tests/coreapp/test_desc.cc` proves exactly that with
a count per kind against the descriptor, so a tab cannot swallow an entry unseen (its control shortens a
tab by one and must fail). An empty tab is never published, and an area with no tab goes with it: a core
with no settings has no Settings tab, which is the whitelist rule in the route's terms.

**`MenuCtl` kept its logic and changed its binding**, as D8's table said it would: the same `activate`,
`change_setting`, `relist`, chooser and browser code, with `area_`/`tab_` where `page_` was, the eight
function keys mapped through `menu_area_of_key()` and the number keys through `menu_tab_of_number()`. A
key for an area this core does not have returns `R_NONE` and does nothing, as the prototype's grammar
expects. Rows gained the third field the prototype's `setting_row.xml` has: title, description, control -
so "the host's default, at the next reset" moved out of the value and under the label.

**The frame is only rebuilt when it changes.** `trx_shell()` hashes everything it draws - the area, the
tab, the chips, the activity word, the skin, the route - and when nothing moved it clears the content
pane alone. Moving the selection inside a tab is the common case, and the difference is redrawing a list
against redrawing 960 x 540; the core application's menu already felt slow once for a neighbouring reason
(T93 5.14). `tests/view/run.sh` checks it: ten redraws of the list rebuild no frame, a tab, an area, a
changed word in the top bar and a screen the caller cleared each rebuild exactly one.

**Two benches render it.** `tests/view/run.sh` draws the shell in both skins with the real LVGL and no
firmware (13 checks against the prototype's numbers). `tests/coreapp/render/run.sh` draws the real
application - `ui.cc`, `MenuCtl`, the Atari's descriptor - to PNG: Home, Library, the machine, its
settings, Actions, System, the message a core that cannot start shows, and the same settings page in the
terminal skin.

**Home is the prototype's dashboard, not a list** (`home.xml`): CURRENT SESSION on the left, with the
machine's chip, what it is running and its picture in an inset screen, and STATUS on the right, one
action row per information line. The top bar carries the two status chips the prototype has - what media
are mounted and whether the network answers - and `panel`, `action_row`, `ui_button` and `status_chip`
are in the view layer, so the host application's screens can use them next.

**The host application shares the palette and the primitives already** (its ten `C_*` colours and its
`box`/`txt` are the view layer's now, one-line forwards, with the render bench unchanged at 38 checks).
Its shell is deliberately not moved yet: it is the application that rescues a broken core, its
navigation is views rather than areas, and changing the keys Alex boots with is his decision.

**The first loader is library code** (§5.6 item 1): `app/loaders/cbm_prg.cc`, the CBM BASIC program
loader the VC-20 and the C16 each carry a copy of today. Its parameter block is the ten numbers that
differ between them - the BASIC start addresses, the pointer pair that holds the start of BASIC and the
window its value must fall in, the three end pointers, the keyboard buffer and its count byte, the READY
delay, the `.P00` header and the largest file that can be meant - and the code between them is one
implementation: the relink of every line link, the three pointer writes, the keys typed into the buffer.
`tests/coreapp/test_cbm_prg.cc` drives it against a memory model (32 checks) with three controls: a
relink that does not move a link, a plausibility window that accepts anything, and an end pointer left
at zero. The window is checked at both ends, so a control that defeats one half is still seen.

Still to come, in the order of the plan: the descriptor as data, the rest of the loader library (§5.6)
and with it the four machines that still run a fork.

## §4 Out

- The C64 / TRX64 core (D5) and an application-loader ABI implementation (D6) used to be out; both are in since D9 (2026-10-18). The struck text is in the Implementation Log.
- **Gideon's REST API** and the routes that go with it: the blueprint has none today (T93 D12 open), and
  T104 does not add it.
- **New machines.** T104 moves the five that exist; the ZX Spectrum (T91), CPC (T92), Apple II (T89) and
  Plus/4 (T90) follow it, they do not wait for it.
- **Changing what a machine's menus show.** S7 demands equivalence; where a menu is wrong today it stays
  wrong until Alex decides otherwise, so that the control can bite.

## §5 Decisions taken while building

### 5.1 The inventory of the four machine applications (2026-10-15)

D2 is a claim with a number in it: five cores ship without a line of core code. It was worth nothing
until somebody had read what the four forked applications actually do, so six read-only agents did,
over `t90-c16app` (vc20 + c264, one tree) and `t85-amigaapp` (amiga + amiga1200) via `git show`, plus a
survey of the file-format code; a seventh was given their results and the single job of **refuting**
"only the Atari's SIO needs a module". Every line below carries a file:line from one of those trees or
from this one. The coreapp claims were re-checked here against the code before they were written down.

What is in the four machines, counted:

| machine   | settings | tasks | file types | media slots | ROM entries | menu rows kept | rows hidden |
|-----------|---------:|------:|-----------:|------------:|------------:|---------------:|------------:|
| vc20      | 4        | 4     | 8          | 4           | 3 + 1 rule  | 9              | 11          |
| c264      | 6        | 4     | 9          | 3           | 6           | 11             | 11          |
| amiga     | 8        | 4     | 2          | 5           | 1 + scan    | 9              | 13          |
| amiga1200 | 10       | 4     | 4          | 7           | 1 + scan    | 10             | 13          |

**The first open question is answered: yes, a ROM *set* is its own kind of entry.** Two rules belong to
the set and not to a member, and `romfind.cc` can express neither. It loops specs outside and roots
inside (`coreapp/src/romfind.cc:48-94`), so it would take `char.bin` from `/usb0` with `kernal.bin` from
`/sd` — a combination both forks refuse: the VC-20 skips any directory missing one of its three
(`vc20.cc:88-91`), the C16 stops at the first directory holding kernal *and* basic and takes the optional
function-ROM pair only from that one, or neither (`c16.cc:141-161`). And `RomWait::delay_ms()` is a fixed
500 ms (`coreapp/src/romfind.h:50`) where the forks wait 5 s after a file that was found and refused
(`vc20.cc:242`). The Amiga adds a third set rule: its members are *discovered*, not named — the first
directory with any `.rom` wins, 256/512 KB files are sorted, capped at 8, and become the value list of a
menu setting (`amiga.cc:330-400`).

**The second open question is answered: yes, a media slot needs more than a mount hook.** Three shapes
are in use, and `MediaSlot` is `{name, what(), mount hook, eject}` (`coreapp/src/desc.h:62-66`):
a *window slot* (ADF: slot at DDR2 +0x900000 + n·0xE0000, zero-filled over the full stride, mount
register 0x100+0x10n, plus a write-back engine with a 160-track dirty map fed by DLOG 0x1F0-0x1F3,
10 µs pop settle, 5632-byte tracks, flush at 1 s idle / 5 s max — `amiga.cc:83, 465-554, 584-654,
766-799`); a *sector server* (HDF: nothing of the file is parsed, the application answers single-sector
mailbox requests, with a 3 ms `taskYIELD` burst that is what makes a sequential read bearable —
`amiga.cc:673-764, 975-984`); and a *library slot* (drive A, which the core knows nothing about). The
`what()` hook has nothing to read in any of them: the forks keep **no file name per slot** (`struct
Floppy` `amiga.cc:443-453`, `struct Hdf` `:455-463`), the mount bookkeeping does.

### 5.2 Thirteen kinds of entry, and why each one exists

| kind | new? | used by | the fact that forces it |
|------|------|---------|--------------------------|
| `rom` | extended | all 5 | `ROM_NAMES_MAX` is 4 (`desc.h:19`); the VC-20 CHAR ROM tries 6 names (`vc20.cc:40`), every C16 ROM 5 (`c16.cc:83-86`). Three target kinds in use: fixed aperture + read-back (`vc20.cc:68-80`), `ROM_SEL` + `rom_write` (`c16.cc:34, 116-132`; `coreapp/cores/atari800.cc:45-59`), DDR2 window offset (`amiga.cc:402-439`). A 256 KB Kickstart is written twice, at +0x780000 and +0x7C0000 (`:423-431`). |
| `rom_set` | **new** | 4 | one-directory, optional all-or-nothing group, two retry delays, scan-by-extension+size bound to a setting (§5.1) |
| `setting` | extended | 4 | several settings in one byte (MEM_CFG 0x0A1 chip [1:0] + slow [3:2], FLOPPY_CFG 0x0A5 write-protect [1] + drives-1 [3:2], `amiga_cfg.h:39-54`); value→byte tables (`CHIPSET_CFG` 0x00/0x08/0x10; VC-20 `exp_bits = {0x00,0x01,0x02,0x06,0x0E,0x0F,0x1F}`, `vc20.cc:123`); a dependency (RAM_RO is 0x10 only when expansion==6 **and** block 5 == ROM, `vc20.cc:145`); a per-value loader limit (the C16 refuses a PRG above $4000 in 16 KB mode, `c16.cc:491-497`); a value list filled at run time; `reload_on_change` (a plain "Reset Amiga" reloads the ROM when the chosen name differs, `amiga.cc:861-869`) |
| `reset_write` | **new** | 3 | fixed bytes written with every settings commit that are not settings: CPU_CFG 0x0A3 = 0x00, IDE_CFG 0x0A6 = 0x21 (`amiga.cc:313,316`), the C16's CART 0x0B1 re-armed from media state (`c16.cc:95,265,565`). They cannot be faked as settings: `desc_check.cc:39` rejects a setting with fewer than two values. |
| `task` | extended | all 6 | every task in the four forks is one of six named operations — cold reset, warm reset, reload ROM set, eject slot, remove cartridge (`vc20.cc:425-426`, `c16.cc:644-645`, `amiga.cc:1129-1139`) — plus `may_abort`: "Reload ROMs, Reset" reports the reason and does not touch the machine (`vc20.cc:393-406`) |
| `file_action` | extended | all 6 | `size_in[]` ("Load as Cartridge" exists only for exactly 16384 or 32768 bytes, `filetype_bin.cc:110-115`), `sets_setting` (a C16 cartridge forces Model = Plus/4, `c16.cc:566-568`), `mode` (Mount / Read Only / Unlinked, `filetype_d64.cc:108-117`), `parent_ext` ("Mount & Run" only for a PRG inside a D64, `filetype_prg.cc:95`) |
| `media_slot: window` | **new** | 2 | §5.1; plus `count_from_setting` — FileTypeADF always offers DF0..DF3 (`amiga.cc:1315-1319`) while the drive count is FLOPPY_CFG [3:2] |
| `media_slot: sectors` | **new** | 2 | §5.1; the size register is in sectors when FEATURES 0x00C b0 is set and in bytes otherwise, so the encoding is **read from the core**, not written in the record (`amiga_cfg.h:59-73`, `HDF_SECT` differs between `cores/amiga/` and `cores/amiga1200/core_sources.tcl`) |
| `media_slot: library` | **new** | 2 | drive A: `multi_mode` forced off, type capped at 1541 (`c1541.cc:119-126`); the library owns mount, eject and the name shown |
| `video` | extended | 3 | `desc_check.cc:22-28` demands w, h, `nbuf >= 2`, `fb_stride`, `fb_base >= 0x01000000` — **the VC-20 cannot be described at all**: `cores/vc20/` has no frame-buffer register (it drives `pix_color/pix_x/pix_y` into the host, `contract/layer2_ports.txt:215,228,229`) and the 567-line `vc20.cc` writes no layer. The Amiga's buffers are gateware parameters (`FB_BUFS=3`, `FB_BYTE0=0x01D00000`, `amiga/rtl/trx/amiga_regs.v:33-36`) with no FB_BASE register, while `Video::init` writes `reg_fb_base`/`reg_fb_stride` unconditionally (`coreapp/src/video.cc:60-63`). |
| `host_constraint` | **new** | 4 | all four machines are PAL only and pin the host's System Mode for the run with `cfg->set_oneshot(CFG_SYSTEM_MODE, 0)` (`u64_config.cc:1214-1232`); `CoreDesc` has no field for a host value a core pins |
| `capability` | **new** | all 6 | the forks trim with six deny-lists over another application's entry texts (`vc20.cc:471-487, 504-521, 525-534, 544-566`; `amiga.cc:1157-1296`; `itu.c:28-34`; `init_function.cc:30-56`) and three of them match on `ConfigItem::get_item_name()`, the definition text, not the label on screen — "Joystick Swapper" (`u64_config.cc:392`) hides the row shown as "Joystick Input" (`:3211`). A whitelist application needs the machine to name what it **has**. |
| `mouse_sink` | **new** | 2 | `coreapp/src/menutree.h:3` already names the gap. The behaviour is seven numbers and a pacer: accumulate clamped to ±4000, at most one pair per 2 ms clamped to ±127 with the remainder kept, MOUSE_BTN 0x1C2 only on change, then 0x1C0 and 0x1C1 (`amiga.cc:803-847`). |

Identity needs one more field than `CoreDesc` has: `amiga` and `amiga1200` are **one gateware** with two
sets of generics (`cores/amiga1200/core.toml` reads `cores/amiga` sources) and the ID word is hard-wired
`{"G","I","M","A"}` in `amiga/rtl/trx/amiga_regs.v`, so `core_find(id)` cannot tell them apart and
`desc_check_table` refuses two cores sharing an ID (`desc_check.cc:73`). **Correction to the agents'
reading:** `cor_name` and `core_find_by_name` already exist (`desc.h:89,119`, `start.cc:16-22`) and
`start_identify` already refuses a bundle whose name disagrees with the core (`start.cc:41-43`) — what is
wrong is only the **order**: the ID picks the descriptor first (`start.cc:34`). The hand-over name becomes
the key, the ID block stays a sanity check with a declared policy (refuse or report).

### 5.3 What T93's descriptor cannot express today

Short version: **nothing is declarative yet.** `desc_check.cc` requires a C function in every one of its
structures — `RomSpec.put` (:34), `Setting.apply` (:42), `Task.run` (:52), `FileAction.run` (:58),
`MediaSlot.what` (:62) — and a setting must have at least two values (:40). (Line numbers as of 2026-10-18.) The checker is rewritten
around named operations before a single descriptor can be data; that is step 2 of the plan, and it is
bigger than it looked.

Four more hard limits, each with a consequence:

- `CoreFs`/`CoreFile` were 32-bit: done, 64 bit on `main` (T104.1 D16, 5.4), including the fork's `File` (pin `aa2ef5dc`). The struck text is in the Implementation Log.
- `Video::poll` masks the reported buffer with a hard-coded 3 (`coreapp/src/video.cc:67`) and the fork has
  the identical hole (`amiga.cc:229`). On the 3-buffer Amiga a reported buffer 3 addresses 0x02000000,
  past the window. `nbuf` exists as a field and is honoured nowhere.
- `desc->service` is called only while the menu is closed. The fork's second menu mode (Interface Type = Overlay) does not come
  over: an open core menu is Freeze (Alex, 2026-10-20), so the stall the Overlay mode caused cannot occur. Original text in the Implementation Log.
- `ROM_NAMES_MAX` 4 against 6 (§5.2).

### 5.4 The fifth thing: the 1541 drive is a library, and D8's table is missing it

The refutation found one piece that is neither core module nor declarable, and it is large: the 1541 with
`.d64` and `.g64` — the CBM filesystem (`filesystem_d64.cc`, 2539 lines), the GCR layer
(`disk_image.cc`, 1534 lines) and the drive service. **`coreapp/` has none of it** (a grep for
1541/iec/d64/gcr over `coreapp/` returns nothing), and vc20 and c264 cannot pass S7 without it: the fork
keeps three mount entries each for `.d64` and `.g64`, the "Drive A Settings" page, the "Built-in Drive A"
task category and four drive-ROM actions on `.bin`/`.rom`/`.64c` (`filetype_d64.cc:108-117`,
`filetype_g64.cc:72-81`, `filetype_bin.cc:117-135`, `c1541.cc:128-134`). By D2 it is a **library** — the
C64, the VC-20 and the C16 share it — so D8's reuse table gains a row: *reuse, move* (the fork's tree is
Gideon's, unchanged).

The agents left open whether the application can reach the drive at all. **Measured here: it can.** The
drive and the GCR codec are *inside layer 2* — each core instantiates `drive_l2` itself
(`cores/vc20/layer2_vc20.v:245`, `cores/c264/layer2_c264.v:246`, `cores/trx64/layer2_trx64.v:431`) — and
their register regions are 14 of the 228 contract ports, routed in by the host:
`drv_io_rd/wr/addr/wdata/rdata/ack/nc` and `gcr_io_*` (`contract/layer2_ports.txt:43-44, 50-51, 94-95,
120-121, 164-165, 171-172, 221-222`). So `.g64` survives too, hardware GCR decoder and all. What is
missing is only a `CoreIo` method for those two pages — `io.h` covers the core register window, DDR2, the
ROM aperture and the RAM window, nothing else. **And it must stay missing on the other cores:**
`cores/amiga`, `cores/nes` and `cores/atari800` do not instantiate `drive_l2`, which is exactly what the
`capability` entry (`drive_a`: vc20 and c264 yes, the rest no) is for.

### 5.5 The Amiga's shape: two records from one source

Two bundles, so two records on the wire, generated from one source with a named override list. The
deciding difference is not the size of the delta — it is three `#ifdef AMIGA1200_APP` blocks in
`amiga.cc` (`:256-262, 265-269, 291-296`) and five defaults in `amiga_cfg.h:23-33` — but the **settings
store**: `register_store(0x41313247, "Amiga 1200 Settings")` against `register_store(0x414D4947, "Amiga
Settings")`. One record cannot carry two stores, and with one store the A500's saved OCS and 512 KB chip
RAM would become the A1200's values. Override list: store id + store name, chipset values + maximum +
default, fast-RAM values + maximum, chip default, slow default — five fields, everything else identical.
The HDF size encoding stays out of the record: it is read from the core's FEATURES bit at run time.

### 5.6 The loader library, in build order

1. **CBM BASIC PRG** (`.prg`, `.P00`-`.P99`), parameterised: BASIC start addresses, the TXTTAB pointer
   pair and its plausibility window, the three end pointers, the keyboard buffer address and count byte,
   the READY delay, the P00 header length, the RAM limit. First because it blocks both vc20 and c264 and
   it is the measured duplication: `vc20.cc:264-371` and `c16.cc:462-538` differ only in the keyboard
   buffer ($0277/$C6 against $0527/$EF), the BASIC starts and the C16's $4000 guard; `relink()` is
   byte-identical in both.
2. **ROM set finding and placement** (extends `romfind.cc`): roots, name lists or extension+size scan,
   one-directory sets, optional groups, the three target kinds, read-back verify, mirroring, two retry
   delays. Needed by five cores.
3. **Raw image into a window slot** with its mount register and the dirty-log write-back engine (`.adf`).
4. **Sector server over a mailbox** (`.hdf`) — the record that forces `CoreFile` to 64 bit.
5. **Already written, moved unchanged**: iNES (`coreapp/cores/ines.cc`), ATR/XFD, XEX with its 6502 boot
   stub, CAR/raw cartridge header. The Atari's 55-row CAR-type→core-mode table and the NES's
   supported-mapper list become data in the bundles.
6. **CBM disk images** `.d64` + `.g64` with the drive service (§5.4). Last because it is the largest.
7. **Embedded file systems** for browsing into a container (`.d64`, `.t64`, `.iso`, `.fat`): registered
   unconditionally in both forks (`embedded_d64.cc:12-13` carries no machine gate), so S7 expects a
   `.d64` to be enterable even on the Amiga.
8. `.cfg` settings files — `coreapp/src/cfgfile.cc`, already Gideon's text format on purpose; moved.

### 5.8 The view layer has a design, and it fits the canvas exactly (Alex, 2026-10-15)

Alex: "Für komplexere Navigation liegt im lvgl Verzeichnis in TRX64-Ultimate auch noch mehr Mock und
Design!" (translated: for more complex navigation there is more mock and design in the lvgl directory in
TRX64-Ultimate). D8 names the view layer as one of the two rebuilds; this is what it is rebuilt **to**.

Two executable LVGL prototypes live in `TRX64-Ultimate/docs/`: `lvgl_ui_project/` (panels and cards,
violet accent) and `lvgl_ui_terminal/` (monospace, green accent). Thirty screens each, the same thirty
routes, built from `docs/gideon-menu-tree.md` (901 lines, the information architecture of Gideon's own
menus) and captured on 2026-10-10 (`UI-SCREEN-TOUR.md`, `screenshots/`).

**They are built for our canvas.** The prototype target is 960 x 540 (`project.xml`); our pixel layer is
960 x 540 at scale 2 on 1920 x 1080 (`host-app/software/lvgl_port/trx_lv_port.h:21-22`,
`PX_CTRL_ON = 0x23`). Nothing has to be re-laid-out.

**The navigation is a contract, not a picture** (`lvgl_ui_project/NAVIGATION.md`), and it is what the
descriptor has to be able to drive:

- A **function key picks an area**, the four printed keys for the frequent ones and their shifted
  partners for the rest: F1 Home, F3 Library, F5 C64, F7 Actions; F2 Timewarp, F4 Tools, F6 Settings,
  F8 System. The label shows the logical key, never "Shift".
- A **number key picks a visible tab** inside the area (`F3, 2` = Library Search; `F6, 5` = Network).
  Published numbers are compatibility identifiers: a new tab appends, it never reorders an old route.
- Cursor keys and Return stay a complete alternative for every route; C64 left-arrow is back and leaves
  the UI at the root; R/S closes at once; Freeze/Menu toggles. Back, close and Freeze are three actions,
  never aliases of one another.
- While a text field has focus, `1`-`5` are input; the shortcuts resume when it is committed or
  cancelled.

Three consequences for T104:

1. **The two skins are one view layer.** Both prototypes use the same grammar, the same components
   (`components/app_shell.xml` and twenty more) and differ in palette and typography only. So the
   rebuild produces one navigation engine with a style table, not two user interfaces - and the C64's
   terminal look is then a setting, not a fork.
2. **Areas and tabs are descriptor entries.** The eight areas above are the C64's. A generic core has
   fewer and different ones: its machine settings, its tasks, its media. The `capability` entry of §5.2
   is the right place - it already names what a machine has - but it has to carry the *route*: area,
   function key, tab number, title. That is a field list, not new behaviour, and it is what makes S7's
   menu walk expressible for a machine nobody has written code for.
3. **The host application and a core application are the same shell.** The boot menu is one area with
   one tab; the core applications add theirs. That is what D4's "two binaries on one base" buys.

What this section does **not** decide: whether the C64's own areas (Timewarp, Monitor, Tools) come over
with the shell. They were T104 §4 out of scope (D5, the C64 keeps Gideon's application); the shell is
taken, its C64-specific screens are not. D9 (2026-10-18) brings them in: Timewarp as a library, the monitor
as an application, both on the day-1 list (D9.7, T104.1 §3 F).

### 5.7 Two things that need Alex, and one scenario that contradicts itself

- **The A1200 auto-loaded the wrong Kickstart — answered.** `choose_ks` preferred the first name
  containing "1.3" or "13" and had no `AMIGA1200_APP` branch (`amiga.cc:386-398`), while
  `cores/amiga1200/core.toml:19` names Kickstart 3.1. Alex, 2026-10-16: the Amiga 500 and its kin load
  1.3, the AGA machines load 3.1, and **neither comes up with a disk in the drive**
  ([ADR-0056](../adr/0056-amiga-kickstart-per-chipset-no-disk.md)). The name is not a usable key -
  two of his three 3.1 images carry no version in their name - so Auto reads the version word at offset
  `0x0C` of the image. For the descriptor this means a `rom_set` may be chosen by a field **inside** the
  file, and no media slot auto-mounts.
- **DECIDED (Alex, 2026-10-20: "Yes"): an ID mismatch refuses, as S2 says.** The core does not start, the error shows the
  four bytes read and the expected ID, and the application returns to the host (the same shape as point 1). The forks'
  print-and-carry-on (`vc20.cc:222-229`, `c16.cc:372-379`) is not reproduced; S7 does not compare this case.
- **Noted, not asked:** the C16's two dead `.bin` entries (1571/1581 ROM, while drive A is forced to 1541
  and both matching rows are hidden) and its two dead hotkeys (C= B, C= K, answered
  `SSRET_NOT_IMPLEMENTED`) are reproduced because §4 says so. The Amiga's eight performance counters and
  its stall-info prose (`amiga.cc:912-952`) go to the debug log, not to a menu, so S7 does not see them:
  they are **deliberately dropped**.

### 5.11 Scrolling the menu: the cheap path stopped at the window's edge (Alex, board, 2026-10-22)

Alex: "the scrolling / cursor-key navigation in the core apps is ultra slow compared to the host app" (translated). Read from the code,
not from a comment: a key reaches the menu through `getch_nowait()` (`run.cc:58`), every queued key is taken before one render
(`run.cc:465`), the loop sleeps 10 ms per pass (`run.cc:491`) and the poll task is stopped while the menu is open (`ctx.frozen`); none of
that is the cost. The cost is `ui_menu_render()` (`ui.cc`): the §5.10 cheap path restyles two rows only while the selection stays inside
the window (18 rows at 960 x 540). One step past the last row moved the window, the row signature changed, and `trx_shell()` cleaned the
content pane and built every row again (3 LVGL objects per row, then all of the pane repainted).

Measured with the real LVGL, no board (`tests/coreapp/render`, 61 entries in the browser, 40 DOWN steps, invalidated pixels per step as
LVGL reports them): a step inside the window 31,040 px; **a step past the edge 355,656 px, every step, 18 rows x 3 objects rebuilt**
(40 steps: 8.7 Mpx). Now: the rows that exist are written again (`trx_row_retext()`), only labels whose text differs, label boxes as wide
as their text so a changed text repaints its own extent and not the row's width, the "a-b of n" label and the status line in place:
**37,000 px per scrolling step, 19,900 px for a changed setting value** (40 steps: 1.7 Mpx), about a tenth, with the frame, the content
pane and the row objects untouched. One step still rebuilds: the one where a row with a different structure (the ".." row without a size
value) leaves or enters the window. The picture is checked pixel for pixel against the same state built from scratch.

Not measurable here: milliseconds on the board. The emulator (`tools/ui_emu.sh`) does run a core application image (the old
`coreapp.app` boots and answers "no description for TRX2"), but a current image needs the RISC-V toolchain of `u64-build-env`, which
this machine does not have; `run.cc` now prints per key batch `[core] menu: N key(s), update X ms, render+flush Y ms` so the first board
log gives the before/after in ms (tick resolution 5 ms).

### 5.12 Every key redraws only what changes (Alex, 2026-10-22: "ok, then reduce the areas, put an agent on it")

LVGL draws about 2.2 ms per 1,000 px on the board (instruction cache only, DIRECT into DDR2), so the invalidated area is the time. The
board log of the core app (`[core] menu: ... render+flush Y ms`) gave a plain selection move ~110 ms for 31,040 px (3.5 ms per 1,000 px),
a menu open of 2.4 s, first keys of 0.8-1.3 s. Read from the code and measured with the real LVGL (`tests/coreapp/render`, `AREA` lines;
estimate in ms = px x 2.2 / 1,000, not measured on the board):

- **Two frame buffers.** `trx_lv_open()` gives LVGL two buffers in DIRECT mode; every frame then first copies the previous frame's areas
  from the other buffer (a DDR2 read through a CPU with no data cache). The host application drops the second buffer
  (`hostui.cc buffers_mode(true)`) and measured 2.2 ms per 1,000 px; the core app did not and measured 3.5. `ui_open()` now does the
  same (`single_buffer()`): the render cost per pixel drops to the host's, and the menu's one full-screen pass loses its copy. The 2.4 s
  of the menu open is therefore read as 518,400 px rendered once (1.14 s) plus its sync copy; this is the hypothesis that fits the two
  numbers, not a board measurement. What the bench does show: the open is one frame built, one pass, one area of 518,400 px.
- **The selection was a tinted row.** `trx_row_select()` restyled two rows (31,040 px for 20 px rows, 52,768 for the Settings' 34 px
  rows). Now a row has four selection strips (3 px bar on the left, a hairline above, below and right; `row_marks()`), hidden or shown:
  3,264 px (Library) and 5,464 px (Settings) per move. Visible change: the selected row has a frame and a bar, no tinted background (the
  Home panel's status rows and the host application's rows use the same mark).
- **Home rebuilt the pane on every key** (355,656 px): its status rows are kept (`trx_action_row_at()`), a selection move restyles two
  (1,160 px); a changed text still builds Home again.
- **An area or tab key built the whole frame again** (518,400 px) for two rail items and the strip. `trx_shell()` now keeps the frame
  and redraws the parts that differ (top bar, the old and the new rail item, the strip or two tabs; `frame_patch()`); the content pane is
  the new content (355,656 px). A media chip that changes while a list is open redraws the top bar alone (34,560 px).
- **The first value change of a session built the pane again** (355,656 px) because the status label did not exist before the first
  message: it always exists now, as wide as its text.
- **Entering a folder** rebuilt the pane even when it holds as many rows as the window: the title is written in place, a row that gains or
  loses its value (a folder has no size) is changed in place (`trx_row_retext()`): 38,268 px for a folder with a full window against
  355,656; a folder with fewer rows than the window still builds the pane (the row count changes).

Bench (`tests/coreapp/render/render.cc`, atari800 descriptor, 63-row library; "before" is the same bench against `e8c7eceb`'s `ui.cc` and
`trxview.cc` with only a counter shim added):

| case | before px (ms) | after px (ms) | bound of the check |
|---|---|---|---|
| menu open (layer resume + clean + build + render) | 518,400 (1,140) | 518,400 (1,140), 1 buffer | 518,400, one frame |
| Home: selection move | 355,656 (782) | 1,160 (3) | 2 marks, 5,240 |
| Library: selection move | 31,040 (68) | 3,264 (7) | 2 marks + values, 7,264 |
| Library: scroll one row | 37,208 (82) | 37,208 (82) | 60,000 |
| Settings: selection move | 52,768 (116) | 5,464 (12) | 2 marks + values, 7,376 |
| Settings: first value change | 355,656 (782) | 6,206 (14) | 30,000 |
| Settings: later value change | 19,920 (44) | 10,464 (23) | 30,000 |
| area key (F1, F3, F5, F7, F8) | 518,400 (1,140) | 391,136 (860) | pane + 40,000 |
| tab key | 518,400 (1,140) | 359,736 (791) | pane + 15,000 |
| media chip changes (top bar) | 518,400 (1,140) | 34,560 (76) | 212,388 |
| folder, RETURN / LEFT, full window | 355,656 / 355,656 (782) | 38,268 / 24,154 (84 / 53) | 120,000 |
| folder, RETURN / LEFT, fewer rows | 355,656 (782) | 355,656 (782) | pane |

The pixels that remain in an area or tab key are the new pane (355,656 px) and the strip (30,000): content that is all new. Each case
is also compared pixel for pixel with the same state built from scratch.
Controls (each puts one cause back; the run is red when it passes): `UI_KEEP_TWO_BUFFERS` (1 check red), `TRX_ROW_TINT` (9),
`TRX_SHELL_FULL_REBUILD` (10), `UI_NO_CHEAP_PATH` (17). `tests/view` checks that a tab, an area and a top-bar word redraw a part and
the frame is built only when its fixed parts change, and that the result equals a fresh build.
The 32 invalid areas (`LV_INV_BUF_SIZE`, `lv_conf.h`, raised to 128; see T101 §5) matter here too: the full-folder case holds 30 of them.

### 5.13 The drive ROM is a shared device ROM; F8 "Restart core" (Alex, board, 2026-10-23)

**Drive ROM.** Board finding: the C264 said "The drive is off (the Drive setting) or has no ROM (1541.rom in the core's roms folder)" while the
stick has the 1541 ROM in `/USB1/roms/` (`1541.bin`, `1541-ii.bin`, `dos1541-325302-01+901229-05.bin`), where the C64 side keeps it, and none
in `roms/c16/` or `roms/vc20/`. Alex: "It already lies elsewhere :-) from the C64" (translated). The drive ROM belongs to a device, not to a
machine, so it is found where the C64 keeps it and not copied into every core's folder.

`RomSpec::shared` (`desc.h`, set by `CBM1541_ROM` in `cbm1541_desc.h`; the 1581 ROM and every machine ROM leave it false). `rom_scan`
(`romfind.cc`) searches such a spec in the core's own directories first (`first_dir`, then `<root>/roms/<folder>` of every root) and then in
the shared `<root>/roms` of every root, same root order; the first file of an allowed size wins, so a copy in the core's folder beats the
shared one even under a later name. A spec without the flag never looks in `<root>/roms`: a C64 `kernal-901227-03.bin` or `kernal.bin` there is
not taken for a C16 or VC-20 KERNAL. The shared folders are listed only when a spec asks for it, up to 128 entries each (a stick keeps the
C64's whole collection there). The name list of the drive ROM gained `1541-ii.bin` and `dos1541-325302-01+901229-05.bin`
(`ROM_NAMES_MAX` is 10 now). The refusal texts of Mount & Run and Run Disk name both places. Benches: `test_cbm1541` (drive ROM only in
`/usb1/roms`, under both stick names; core folder wins; KERNAL only in `/usb1/roms` not found), `test_c264` (same for the C16 folder);
controls `rom_shared_off`, `rom_shared_all`, `rom_shared_c264`.

**Restart core.** Alex: "Restart Core under F8, all ROMs are loaded again." (translated), then: "We need something so that when we change
something I can restart the core 'quasi', i.e. like a cold boot of the core. So on a change of ROM, hardware, etc." (translated). A fixed
entry `Restart core` in the F8 System area (`EK_RESTART_CORE`, `menutree.cc`, after "Reset to host defaults") calls `machine.restart`
(`boot_restart`, `boot.cc`), the shared `reload` with `cold` set. In order: `rom_scan` of every RomSpec (the drive ROM with the shared
fallback) and of the ROM set - a ROM that is missing leaves the machine running and the menu open with the reason; the core held by `init`
(`mode_reset`); the ROM set's area cleared; the machine RAM cleared (`CoreDesc::cold_ram_bytes`, zeros through the RAM window); every ROM put;
**every** setting written, the live ones too (`regset_apply(all)`, as the start does; the reset reload writes the reset-time ones only);
`reset_ms`; `release`. A core that waits for a program (the NES) answers "<needs_program> first." and is not touched. Success closes the menu.

*What the application can do alone, and what it cannot.* Alone: the registers, the ROMs, the RAM that the RAM window reaches, the reset of
every chip the core resets with its RESET (the C264's `sreset`, the machine's own stretch), the drive library (its image stays mounted, the
drive resets with the machine: `test_cbm1541`). Set today for the C264 and the VIC-20 (64 KB window); `cold_ram_bytes` is 0 for the other
cores, whose restart is still ROMs + settings + reset until someone has read their RAM window. The RAM is zero-filled, not the pattern a
real machine shows at power-on. What stays: flip-flops that no reset reaches in the gateware (a core's RESET does not clear block RAM nor
the register block - the registers are rewritten, but the frame buffers and the cartridge bits `c264.cart_bits` stay), and everything that is
a property of the partial bitstream itself (a changed *gateware*, as after a core update, cannot be taken without reconfiguring the FPGA).
Not measured on a board.

*The host-assisted variant (an option for Alex, nothing of it built, `host/` and `host-app/` stay as they are).* The app can already leave
to the host (`hw_return_to_host`: ICAP IPROG with WBSTAR 0, `hw.cc`), but the host then shows its own menu: the hand-over page
(`Handover`, `handover.h`: magic, version, sum, video, path, core name/version/host key) goes from the host to the core only, and no flag
tells the host "start this core again". A true power-on, gateware included, needs (1) a page the *app* writes before IPROG (same DDR2
address, a new `restart` flag with the `.cor` path, which the host reads and then runs `coreboot` on that file without its menu) and (2) the
host's boot code reading it. Both are host changes; the host is frozen (CLAUDE.md, T94), so it is a decision for Alex.

**Decision (Alex, 2026-10-24, after the C128's first board run):** "I would expect 'Restart core' to put it into the C128 base state. So
all lines etc. back to default, like a cold start. That is essential for the cores, so that one can simulate the cold start. Does the
function do that? Otherwise it has to be reworked." - it does not (above) - then: "For the C128 yes [its own cold-start line], and for
the DFX cores the entry should then be called 'Reload core'." (translated). So: on a DFX core the F8 entry is **Reload core** and loads
the core's partial bitstream again (every flip-flop and block RAM to its configuration value), then ROMs, settings and release as a start
does; on a flat build (the C128) the entry stays **Restart core** and drives a cold-start line in the core that reaches every register,
the video chip's timing included. Built under T104 5.14.

*The F7 "Reset" report (Alex, board 2026-10-23: "Reset does not work", translated).* Not the same cause: Reset is `machine.cold_reset`
(`boot_cold_reset`): hold, the reset-time settings, `reset_ms`, release; it reloads nothing and clears nothing, and it printed no log line
(only Run Disk printed one, from `cbm_run.cc`). Read from the code, its sequence is the one Run Disk uses and the one the bench checks
(`test_c264`: Reset task as the menu binds it, mode_reset first, MODEL / RAM_SIZE / JOY_SWAP in the hold, mode_unreset last, the core's latch
sees them); no defect found in the application. `boot_cold_reset` now prints `[core] cold reset`, so the next board log shows whether the
task ran. Open: what the machine did after it.

### 5.14 Reload core: the partial bitstream loaded again by the core application (Alex, 2026-10-24; as built, bench only)

**Question first: can the application do it alone?** Yes, nothing of `host/` or the host application's sources changes. Read from the code: the
ICAP loader and the decoupler are registers of the host's `ultimate_sys.vhd` (`0x10060610` data, `+14` enable, `+18` word counter, `+20`
decouple request / status), reached over the same io bus by the core application's CPU (`hw_return_to_host` already writes `+14`/`+10`/`+20`
from the application). Bit reversal is in the host's gateware (`top.v` `icap_swap`), so the file's bytes go out as they are, exactly as
`coreboot.cc core_swap` does. The checks of the host application's `core_stage` are in `cor.cc`, which is shared source. The `.cor` path is in
the hand-over page (`CoreCtx::cor_path`, the host key in `host_build`, now `CoreCtx::host_key`), and the file system is the application's `CoreFs`.
The decouple also puts the host-owned page 0 and 3 registers back to power-up (`ultimate_sys.vhd` 1266-1295): the settings are written again anyway.
The scaler relocks by itself: `top.v` 1539-1542 makes `dc_pix_rel` from the falling edge of the decouple and gives it to the scaler's `relock`.

**Built.**
- `partial.h/.cc` (new): `PartialPort` (the loader and decoupler as an interface; `hw.cc` `HwPartialPort` on the board, a fake in the bench),
  `reload_is_dfx(c)`: a hand-over path ending in `.cor` and a loader that answers 0x7A. `partial_check`: opens the `.cor`, `cor_probe` (header,
  contract, **host key** against the running host's), the manifest's name against the descriptor's `cor_name`, the SHA-256 of the partial against
  the manifest - the file is read once completely before anything is touched. `partial_stream`: decouple on (waits about 10 ms and reads status bit 1
  as the host application does), enable, 16 KiB chunks from the file to the loader (four byte stores per word, as `core_swap`), enable off,
  the word counter against `size / 4`, decouple off. The partial is not staged in the DDR2: a fixed staging address would collide with a core's
  window, so the file is read twice (hash, then stream). Shared source: `cor.cc` and `sha256.c` are linked into the application now (the
  coreapp `Makefile` lists them; that file lives under `host-app/target/` but is the application's own build recipe); the stream loop is a
  second copy of `core_swap`'s inner loop, because `core_swap` is bound to the host's staging area.
- `boot_restart` (`machine.restart`): NES rule first (a core that waits for a program is not touched). A DFX core: ROM scan (a missing ROM leaves
  the machine running), `partial_check` (a missing, damaged, foreign or other-host `.cor` leaves the machine running with the reason), hold,
  `partial_stream`, hold again (`desc->init`), `drive.resync` and `engines_after_reload`, `Video::init` (frame buffers black, FB_BASE / FB_STRIDE
  told to the core), then the sequence of 5.13 unchanged (ROM set cleared, RAM cleared, ROMs, **every** setting, `reset_ms`, release). All of it runs
  as the operation under the **core lock**, so the poll task and the media service cannot touch the core during the load. After a failure
  once the core is cut off (read error, word count) it stays cut off and the menu says "Return to host menu, or power cycle"; a decouple that does
  not take effect stops before the first word.
- What the new core is told again: `CbmDrive::resync_hw` (type, address, RAM map, empty track parameters, the disk in the drive from the
  object's state, power) as operation `drive.resync`; `sec_reannounce` writes the **size** register too, not only the present bit (the window
  media's `win_reannounce` and the Amiga's hold already announce on every `init`); the picture-window state (`fbwin`) and the layer's view are
  reset to the gateware's default; the mouse sink's pace state.
- Menu: `menu_build(..., reload_core)`; `MenuCtl` passes `reload_is_dfx(c)`. DFX: **Reload core** and "Core reloaded: bitstream and ROMs loaded
  again"; otherwise **Restart core**. The golden menu lists are the flat build's and are unchanged.
- Flat build: new optional `CoreDesc::cold_start` (`int (*)(CoreCtx *, char *msg, int msglen)`, null for every core of today). "Restart core" calls
  it before the 5.13 sequence; a non-zero return stops the restart with the reason and the machine running. Nothing of the C128 is built.

**Bench** (`tests/coreapp`, fake loader and decoupler, `.cor` made in memory by `cor_build.h`; VIC-20 descriptor with the drive model; `test_sectors`
for the size register). Full run on the Mac: see the history entry of docs/todo/T104.md for the result lines. Checks: the loader gets exactly the file's
partial bytes; order hold < decouple on < stream < decouple off < hold < ROM puts < release (last event); the fake loader wipes the core's registers
and the drive model at the end of the stream, and afterwards FB_BASE, the drive power, the inserted flag and the mounted image are back; the RAM
cleared; refusals touch nothing (missing, damaged partial, other host key, other core name, not a bundle: no event at all); read error
mid-stream, word count one short: core stays cut off, no give-back; decouple not taking effect: no word sent; labels (`.cfw`, no path, no
loader: "Restart core"); the hook called first, its refusal stops the restart; the NES rule on a DFX core. Sixteen controls (`reload_*` in
`run.sh`) each fail with 1 to 28 red checks; `sec_reannounce_size` fails `test_sectors`; `sec_sizereg` was re-aimed at the moved line.

**Linked.** The RISC-V image builds in `u64-build-env:ci` on the builder (`make` of `host-app/target/u64ii/riscv/coreapp`, as `ci/trxapps.sh`
does, from a copy of this tree outside CI): it links (the board image has no libstdc++; the new code uses neither new nor exceptions), `coreapp.app` 845,532 bytes, 1,120,536 bytes of the 1,966,068-byte slot unused. The render bench (`tests/coreapp/render/run.sh`, LVGL, in the same image) builds the same source list from the Makefile - `sha256.c` is added there as `sha256_cc.cc` - and passes: 66 checks, 0 failed; its four controls fail as they must.

**Not proven (board).** Nothing of this has run on the board: the ICAP time with the application's byte stores, the time of the two reads of
the partial from the stick (the menu is blocked for both; there is no progress line on the screen, only the console), whether every core's `init`
hook is enough to bring its own state back (only the VIC-20 descriptor with the drive and the Amiga's sector media are benched), the picture
after the relock, and the sequence on a core whose partial is larger than the VIC-20's. The decouple status read and the wait of about 10 ms are the host
application's, not measured here. A `.cor` that is not on the stick any more is reported; a stick pulled mid-stream leaves the core cut off.

## Implementation Log

Superseded and historical statements moved out of the sections above on 2026-10-20 (nothing deleted). The dated build record of §5 and the history of `docs/todo/T104.md` stay where they are.

### Moved 2026-10-20

**Former D2, paragraph "The middle layer is the exception, by design"** (replaced by the wording of point 7, decided 2026-10-20):

> The middle layer is the exception, by design. The partition contract already standardises reset, the RAM and ROM windows and the frame buffer; when the descriptor may also say *this file to DDR2 at offset X, then write register Y*, almost every core needs no code at all. The measure of success is that the VC-20, the C16, the Amiga, the A1200 and the NES ship **without one line of core code**, and the Atari's SIO is then the test case for the module path, not its normal use.

Former heading of D2: "Three layers, and the middle one is meant to be empty (Alex, 2026-10-15)"; former title of S8: "the middle layer stays empty". Alex's own words in that discussion are kept in D2: "Aber das sind doch core-spezifische Capabilities .. die muss der Core mitbringen". The same wording was in `docs/todo/T104.md` ("the middle one is meant to be empty").

**Former D5, superseded by D9 (2026-10-18):**

> **D5 — The C64 keeps Gideon's application for now (Alex, 2026-10-15; superseded by D9 at the end of the transition, 2026-10-18).** "aktuell lassen wir TRX64 Core
> bitte mit der Gideon-based App. Der Rest darf aufs neue Format." The TRX64 core is the only one with a
> full C64 behind it - cartridges of every type, REU, tapes, two drives, SID, timewarp, monitor, UCI - and
> that is the scope Gideon's application was written for. `trx64.app` stays shared with the TRX64-Ultimate
> line.

**Former §4 entries, struck through when D9 came:**

> - ~~**The C64 / TRX64 core** (D5). Not in this spec, with a date to be decided.~~ In since D9 (2026-10-18).
> - ~~**An application-loader ABI implementation** (D6): the boundary and the refusal path are designed,
>   nothing is built. The first module to use it will be the Atari's SIO, and only after the five
>   declarative cores prove that it is the exception (S8).~~ In since D9.4 (2026-10-18): libraries load
>   dynamically through the `.u2a` door; the design is [T104.1](T104.1-contribution-model.md) D10-D12.

**Former §5.3 bullets:**

> ~~`CoreFs`/`CoreFile` are 32-bit (`coreapp/src/fsif.h:26-31`): an HDF of 4 GiB or more cannot be served at all.~~ 64 bit on branch `t104-generic-app` since 2026-10-18, not merged (T104.1 D16, §5.4); the fork's `File` too (branch `t104-fs64`, pointer not bumped yet).

(Both halves have since happened: the branch is merged and the submodule pin moved, `82777f2a`, `71a8e5ef`; the host-app vendored copy followed, `0d0f0ef5`.)

> `desc->service` is called only while the menu is closed (`coreapp/src/run.cc:299-306`, 2026-10-18). With the fork's second menu mode (Interface Type = Overlay, the machine keeps running) an Amiga disk access stalls for as long as the menu is open; the fork serves it from its own FreeRTOS task (`amiga.cc:954-986`).

**Former header line:** "Status: PROPOSED (2026-10-15)."

### Dated record

- 2026-10-15: spec written, D1-D7 decided with Alex, nothing built.
- 2026-10-16: shell, route menu and the first loader library built (5.10); NES board session, redraw cost.
- 2026-10-18: D9 recorded ("T104: Sie auf main bitte!"), D10, D16.
- 2026-10-19: board photos, UI cut off and picture misplaced; regression of the change that scaled the Atari and NES pictures (T107).
- 2026-10-20: Amiga and A1200 onto the generic app, open menu == Freeze, always 1080p (docs/todo/T104.md history); this consolidation.
- 2026-10-22: scrolling in place (5.11); one frame buffer, selection strips, partial frame, Home kept (5.12).

---
<!--
Closing: Status -> DONE (date) + which scenarios are green where; the TODO row gets ✅ and a
history entry, in the same commit as the code.
-->


**Board 2026-11-04: "built for another host" on F8 Reload core (cause found 2026-11-06, fixed).** Alex, package main-91f629ce, host 96F6AB08 (unchanged since
c83a83c2): a core the same host had just started answers `<path>: built for another host. The core keeps running.` on F8, again and again (no core
named; `cor.cc` `cor_result_str(COR_ERR_HOST)` through `partial.cc` `partial_check`, the only place the core application compares the key). Read from
the code: the key has one source on each side and the chain is the same function twice - `hostapp.cc` `running_host_key()` (BUILD register) ->
`cor_host_key` -> `core_stage`'s `cor_probe` (refuses on mismatch) and `core_jump`'s `handover_fill` -> page -> `start.cc` `start_paths` ->
`CoreCtx::host_key` -> `partial_check`. A mismatch at F8 therefore needs a different file at `c->cor_path` than the one the host probed (the core
application's `/usbN` or `/SD` is another stick or another copy), or a hand-over key that is not the running one; the code does neither. Bench
`tests/coreapp/test_hostkey.cc` (host key 96f6ab08 through `handover_fill`, `start_paths`, `partial_check` with the real `cor_probe`): the .cor of the
running host passes, a .cor of another host (96f6ab09) is refused; controls hostkey_parse, hostkey_fill, hostkey_unchecked, hostkey_msg fail as they
must. Built so the next report settles it: the message and the console line `[core] reload: <path> routed against host <file key>, hand-over says host
<hand-over key>, <size> bytes` name both keys. Not a fix: the cause is not found.

**Board 2026-11-06 (main-a9ef1237, Apple II, host 2BB1DE24): cause found.** The same refusal: "the file says 2BB1DE24, this host is 20010024". 0x20010024
is "2BB1DE24" read by `start.cc` `start_paths`, which took only `0-9a-f` and made every other character a 0 digit (B, D, E -> 0). The page held
capitals: `handover.cc` `handover_fill` wrote the key with `snprintf("%08x")`, and the firmware's own `snprintf` (`vendor/gideon/software/system/small_printf.cc`
11, 61, 178-188: one `hexchar[] = "0123456789ABCDEF"` table for `x` and `X`) prints hex in capitals; that also is why every `%08x` on the screens
(top bar "host ...", this message) shows capitals. Hosts whose key has no letter A-F never saw it. Fix: `start_paths` reads `0-9a-fA-F` and a
character that is no hex digit voids the key (0 = none named), and `handover_fill` writes its eight digits itself, in lowercase, whatever the libc
does. `test_hostkey` takes the capital, lowercase and mixed forms (Reload accepts the matching .cor) and three garbage keys (0); controls
hostkey_case and hostkey_garbage fail as they must.

### 5.15 The Amiga 500 runs the generic application (Alex, 2026-10-22 and 2026-10-24)

**Decision.** 2026-10-22: "apart from trx64 and c64u, switch everything to the generic core app for now"; 2026-10-24, about the A500: "the app
must also be switched to generic!" (translated). Until now the A500 (`amiga`) was the one blueprint core that shipped the Gideon-base
`amiga.app` from the hand-uploaded generic package `trx64-coreapps`, while the A1200 had run the image of `ci/trxapps.sh` since 2026-10-20.

**Built.** `amiga` is in `BLUEPRINT_CORES` (`ci/trxapps.sh`, `tools/build_coreapp.sh --dist`); the stage's `coreapps/amiga.app` wins over any
uploaded file (`ci/package.sh`), the packer and `core.toml` (`app = "blueprint"`) already treated the core as a blueprint core; `table.cc`
holds `amiga_desc` since T104 5.5. Only `trx64` still takes its application from the package.

**Read against the gateware, not against the descriptor's intent.** The record is one source with an override list; what the A500 gateware
(`cores/amiga/rtl/trx/amiga_regs.v`) does and does not have decides what the record may use:
- No display/crop window registers (`AM_DIW_*`, `AM_FB_*`, FEATURES b3 reads 0 there): `fbwin` is `false` for the A500 record, so
  `fbwin_poll` does nothing and the picture stays the fixed window of `amiga_fb` (36, 10, 90 words, 270 lines), as the old application had it.
- Hard disks: the A500 gateware has the mailbox and the HDF mount registers (`HDF_SECT = 0`: the size register counts bytes). The old
  application offered HD0/HD1 on the A500 as well, and `sectors.cc` reads FEATURES b0 and writes bytes when it is clear, so the record keeps
  HD0/HD1; an image of 4 GiB or more is refused with the message the old application gave ("the core needs the size in sectors").
- Covered from the old application (`t85-amigaapp`, `software/io/amiga/amiga.cc`): Kickstart choice with Auto 1.3 first, 512K/1M/1.5M/2M chip,
  slow, fast 0/2/4 MB, OCS/ECS, 1-4 drives, write protect, DF0-DF3 with the dirty-track write-back, HD0/HD1, the USB mouse, Freeze, Reset,
  Reload Kickstart, eject entries. Joystick and keyboard reach the core through the host's ports (`amiga_input`, wired in the gateware; the old application only hid the C64 rows that would have configured them, the generic menu never shows them); disk swap is
  mount/eject of a slot, and since 80435fedd a mount from the Freeze menu is a disk change.

**Scenarios and controls.** `tests/coreapp/test_amiga500.cc` reads the write case (`12'hXXX`) and the read case (`10'hXXX`) out of the A500's
`amiga_regs.v` and drives the A500 record through a start (settings, hold, release), four ADF mounts, two HDF mounts and a served sector,
the dirty log, the mouse, the picture-window poll and the ejects: every register written or read must be in that decode (35 writes, 132
reads), and the A1200 record, run the same way, must be caught on `0D0` / `0C0` (a checker that cannot see anything is not a checker).
Controls: `a500_fbwin` (the A500 record switches the window on: the checker fails), `a500_hdmount` and `a500_cfg` (a register outside the
decode). Not proven: the board (the old and the new application differ in start-up time and in the log lines, nothing else is known), and
the audio KPIs (`STAT_SEL` 8-11) that the old application printed every ten seconds are not in the generic application.

### 5.16 Fast seek for disk and hard-disk images (Alex, 2026-10-24; built, bench only)

**Decision.** The coordinator's proposal was "fast seek in the core app (FatFS `FF_USE_FASTSEEK` plus a cluster link map when an image is mounted)
as the real fix, the ZX Next SD hold as a safety net". Alex, 2026-10-24: "Ja bitte. Das macht total Sinn, ich vermute die hdf vom Amiga werden dann
auch schneller. Also machen." (translated: yes please, that makes total sense, I suspect the Amiga HDFs get faster too, so do it).

**§1 The problem, in parts.**
- P1 Every image the application serves goes through `CoreFile::read/write` (`coreapp/src/fm_fs.cc`): one `File::seek` + `File::read` per request. The
  Ultimate's FatFs is built with `FF_USE_FASTSEEK 0` (`chan_fat/full/ffconf.h:47`), so `f_lseek` to an earlier cluster walks the FAT chain from the
  first cluster. A block 84 MB into a 2 GiB image is thousands of FAT lookups before the data sector is read.
- P2 The ZX Next firmware times out (about 70 ms per block) reading `TBBLUE.FW` from the SD image (branch `t116-tbblue-fw`, not merged). The same path
  serves the Amiga HDF (A500 and A1200), the sector requests of `sectors.cc`, ADF/ATR/DSK/D64 images and the 1541 library.
- P3 FatFs cannot grow a file through the map: `f_write` at an offset the table does not cover ends `FR_DISK_FULL`. Images are fixed size, but a
  writer that extends one must not be broken by an optimisation.

**§2 Scenarios** (bench: `tests/coreapp/test_fastseek.cc`, the real `ff.c` over a RAM disk, run by `tests/coreapp/run.sh`).
```gherkin
Scenario: S1 a random block of a big fragmented image costs no FAT walk
  Given a 16 MiB image in 256 fragments on a FAT32 volume with one sector per cluster
  When  300 random blocks are read, the first access that is not sequential having built the map
  Then  every block is byte for byte the pattern, and no seek+read asks the disk for more than 2 FAT sectors
  And   the table was too small at first and was grown once to the size FatFs asked for (2 + 2 * 256 items)
  # control fsk_nomap (map never built): the bound goes red

Scenario: S2 the number the map removes
  Given the same file opened without the map
  Then  the same blocks cost on average 181 FAT sectors, 237 in the second half of the image (max 497)

Scenario: S3 a file too scattered for the table stays on the normal seek
  Given a file of 32768 fragments (more than the 8191 the table may hold)
  Then  no map, FIL::cltbl cleared, a [core] log line, and the blocks still read correctly
  # control fsk_keepptr (FatFs leaves cltbl set on failure): reads go through an unterminated table

Scenario: S4 a writer that extends the file is not broken
  Given an image with the map
  When  the write reaches beyond the end of the file
  Then  the map is dropped before the write, the write succeeds, the new size is on disk, and the map is not built again for this open file
  And   the same write through the map is refused by FatFs (FR_DISK_FULL), which is why it is dropped
  # control fsk_nodrop

Scenario: S5 a write inside the file lands where the plain seek puts it
  Given the map in place; a block written at 11 MiB + 1.5 KiB
  Then  a second file object without the map reads it back, and the neighbouring file is untouched

Scenario: S6 the same on exFAT (4 KiB clusters)
  # USB sticks that hold 4 GiB and larger images are exFAT; the FatFs build has FF_FS_EXFAT 1

Scenario: S7 loading a file costs no map
  Given a file below 256 KiB, or a big file read from top to bottom in 32 KiB steps
  Then  no map is built (the walk to build one costs more than it saves)
  # controls fsk_nomin, fsk_always
```
```gherkin
Scenario: S8 the board's CoreFs recognises a FatFs file and uses the policy
  Given fm_fs.cc over the real FileManager, FileSystemFAT and FatFS (FF_USE_FASTSEEK 1) on a 128 MiB exFAT volume, two files grown alternately
  When  200 random blocks of one 16 MiB image (256 fragments) are read through CoreFile::read, then a write goes beyond its end
  Then  every block is the pattern, the block device is asked for at most 4 sectors per block, and the extending write succeeds
  # bench tests/coreapp/fmfs64/test_fmfs_seek.cc; controls seek_novtable (no FileFAT recognised: the bound goes red), seek_nodrop
```
Also a control for the grow step (`fsk_nogrow`: the second table the size of the first). Not proven here: the board (S9 below).
Scenario S9 (board, open): the ZX Next reads `TBBLUE.FW` without a time-out and the SD hold becomes a safety net; an A500/A1200 HDF boots and
mounts measurably faster. The `[core] fast seek:` log lines say whether the map was built.

**§3 Decision, as built.**
- `FF_USE_FASTSEEK` is switched on for the core application only. `coreapp/ffconf/ffconf.h` includes the original `chan_fat/full/ffconf.h` and
  overrides that one option; the coreapp Makefile (`host-app/target/u64ii/riscv/coreapp/Makefile`) puts `-I$(PATH_SW)/../../coreapp/ffconf` first in
  `OPTIONS`, so `ff.h` finds it before `chan_fat/full` and before `hostapp/` (the host application's own `ffconf.h` of T115 enables `f_expand`; that
  stays the host's). The submodule `apps/ultimate` and the vendored `host-app/software/filesystem` are untouched. Every object of the coreapp is
  compiled with the same flag, so `FIL` has the same layout everywhere (one pointer more, `cltbl`).
- `coreapp/src/fastseek.{h,cc}`: `fastseek_build` (allocate 128 items, `f_lseek(CREATE_LINKMAP)`, on `FR_NOT_ENOUGH_CORE` allocate exactly the size
  FatFs reported and try once more, give up above 16384 items = 64 KiB; on any failure clear `cltbl`, free, log) and the policy class `FastSeek`.
- `coreapp/src/fm_fs.cc`: every open `FmFile` carries a `FastSeek`; `read` and `write` call `access()` first. The map is built at the first access that
  does not continue the previous one, for a file of 256 KiB and more (never for a PRG/ROM loaded in one go), once per open file. The table is freed
  with the `FmFile` (after `fclose`). A `File` is a FatFs file when its vtable pointer equals a `FileFAT`'s (no RTTI in this build); anything else
  (a file inside a mounted image, FTP) is simply not recognised and keeps the normal seek.
- Writers: `CoreFile::write` is the only writer of an open image (`sectors.cc`, the 1541 library, `atari_atr`, `apple2_disk`, `cpc_disk`, `winslot`
  through `CoreFs::open(path, true)`); `newdisk`/`newfile`/`save` create whole files with `FileManager::save_file`, no map involved. Instead of
  auditing each writer for a growth path, the rule is in one place: a write whose range ends beyond the file size drops the map first (S4).

**§4 Out.** No map for files below 256 KiB. No map rebuilt after a refusal or after growth. No attempt to make the map survive a medium that is
pulled (the table is plain heap memory; `fclose` closes the FatFs file object first). No change to the host application (it reads bitstreams
sequentially). The ZX Next SD hold is a separate safety net (branch `t116-tbblue-fw`), not part of this change.

**§5 Measured.** `tests/coreapp/run.sh`, builder, `u64-build-env:ci`: `S1 map on : 300 random blocks of a 16 MiB image in 256 fragments: FAT sectors per
seek+read: avg 0.00, max 0`; `S2 map off: ... avg 181.34, max 485; blocks in the second half: avg 237.43, max 497`; `S6 exFAT, map on: avg 0.00, max 0`;
`67644 checks, 0 failed`; `tests/coreapp/fmfs64/run.sh` (the real `fm_fs.cc`, vtable test included): `FmFsSeek: 200 random blocks of a 16 MiB image in 256
fragments: sectors read per block avg 1.00, max 1`, 2 tests passed, controls `seek_novtable` and `seek_nodrop` fail as they must; controls `fsk_nomap`, `fsk_nodrop`, `fsk_nogrow`, `fsk_keepptr`, `fsk_nomin`, `fsk_always` fail as they must. The RISC-V image
links (847892 bytes of data, the slot takes 1966068; `tools/check_app_size.py`). Not proven: the ZX Next and Amiga timings on the board (the vtable test of `FileFAT` is proven only on the Mac's compiler; the RISC-V
compiler uses the same ABI, but nothing has run it).
