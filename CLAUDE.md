# TRX64U MultiCore — rules for Claude sessions

The multi-core line of TRX64 Ultimate: one host (layer 1), n swappable cores (layer 2) on the C64U / U64 Elite II
(same board, XC7A50T). Spec of this repository: `docs/specs/done/T94-trx64host-gitops.md` (D1-D11 and Q1-Q10 decided), its plan
and state: `docs/todo/T94.md`. The 1:1 drop-in line (a full C64; RC1 stays as the tag `trx64-integ-m1-692f643d`) lives in the
TRX64-Ultimate repository and is not touched from here.

Since 2026-11-20 this GitHub repository is the development home (T239.1). The private material (ROM images, bench media, session
notes, notes on internal infrastructure) is in a private side repository (T239.1 D5); the maintainer's checkout holds it as an
overlay that git ignores.

## The tree (T94 D1, layout since 2026-10-13)
`host/` = layer 1 with the floorplan, the boot ROM and the generic `core.tcl`; `cores/<name>/` = one core with
`layer2_<name>.v`, its `core_sources.tcl` (it must set `l2_dcp`) and `core.toml` (id, version, contract, app, order,
`experimental`, sources, optional `pipeline = false` = code stays, no pipeline stage builds its gateware, packs it or benches it; lint,
the core app (its code is in the app image) and its unit tests still read it (T92 D-17e) - `ci/lib.sh` reads the cores from these
descriptors, `host/layers.txt` has no `core:` sections any more); `cores/common/` = IEC bus, 1541 drive and the five TRX modules
(`trx_io`, `trx_sys`, `dma_win`, `trx_engine`, `drv_dbg`) the host and the large cores read in place, read by several cores; the old
DFX trx64 core is deleted (Alex, 2026-11-08, T115 D12; `tools/check_no_old_core.py` in lint keeps it so); `contract/` = the 247 ports
of the partition plus its version, checked per core in the lint stage; `host-app/`, `coreapp/`, `vendor/{icepi,gideon}` = third-party
trees the host reads too (`vendor/gideon`: our own copy of 1541ultimate, VHDL plus the software our apps compile, renewed with
`tools/gideon_sources.sh <checkout>`, T120); `release/` = sets and board manifests.
The host is routed together with the reference core `c64ref` (a slim C64U copy, `build_only`: never shipped, T115), every other core
against the cached result; an `experimental = true` core is built and reported like any other; since T100 D9.6 its failure makes the
run red too (T94 D8's exemption is gone). The bitstreams are built on the private builder by content key (T239.1); at most two Vivado
runs at a time there, one of them maybe TRX64-Ultimate's (`ci/lib.sh` heavy_lock, T100 D11, T239.1 D27).
Vivado runs with `host/` as its working directory, so paths in the Tcl files are relative to it.

## Language
Chat with Alex in German; code, commits, specs, TODO and every document in English (quotes translated).

## Priority: deliver fast (Alex, 2026-11-08)
"All I want is to deliver fast. At the cost of quality, gladly, because there are testers" (translated). Shortest path to a package in the
testers' hands wins; rules below that block shipping (proof of every scenario before merge, UX gate, no untested builds) are re-decided with
Alex one at a time - until then prefer shipping and say what was not checked. No new CI/test infrastructure unless Alex asks.

## Spec driven (Alex, 2026-10-14)
Every piece of work starts as a spec, not as code: `docs/specs/_TEMPLATE.md`, number from the table in `TODO.md`, status PROPOSED with
the date and who asked. §1 the problem in parts, **§2 the scenarios before the design** - each one names the bench or the board step
that proves it. Negative controls (mutants) only for flash and persistence operations, nowhere else (Alex, 2026-11-08, T174 D5.4: "We
are not building bank software ... This is a hobby project"). Then §3 the decision, §4 what is deliberately out (decided with Alex
beforehand, never by omission), §5 filled in while building. A measurement goes in with the report line it came from.
A spec whose status is DONE moves to `docs/specs/done/` in the same commit (Alex, 2026-11-19, translated: "if something is DONE it
should go into the DONE folder too"); `tools/check_todo.py` is red for a DONE spec outside it.
No code for a change that has no spec section to point at; a one-line fix points at the section it repairs.

## Git and CI
- **Home (T239.1):** GitHub `Jondalar/TRX64U-MultiCore`. Branches `feature` → `integration` → `main` (D15). Only Alex merges into
  `integration` and `main`, and only he creates, moves or deletes a tag (D28). A session pushes its own feature branches without
  asking (D25); a commit that touches only documentation (`TODO.md`, `docs/`, other `*.md`) goes directly to `integration` (D33).
  Specs and decisions live on `integration` (T94 Q10, as read since the switch). GitHub releases, comments and posts need Alex's go.
- Never force-push. One exception (Alex, 2026-10-18: "Ja er darf"): a session may rebase its own, not yet merged feature branch and
  push it with `--force-with-lease`; never `integration` or `main`, never a branch another session works on.
- **The pipeline** (T239.5 D22, Alex 2026-11-26: "Away with the monolith."): a push runs `.github/workflows/pipeline.yml`, which only holds the triggers and the
  one call of the matrix (`.github/workflows/matrix.yml`); the matrix is the whole pipeline, its jobs run in dependency order, and the dependents of a red job do
  not start: dispatch (`ci/dispatch.py`: what changed, which lanes and jobs, the work order of the builder), approve, lint (one leg per lane), keys, apptests, the
  lanes (SDK, host menu, one per core with an app of its own), sims, the bitstream jobs, cor (the .cor of a core without a lane), collect and package, update,
  the emulator jobs, the pre-release. The bitstreams are not a job of GitHub's: the builder builds the keys the dispatcher's work order names as missing (D11, D13)
  and releases the environment `bitstreams`, where the jobs that need them wait without a runner (D21). Environment `vivado` (job approve) is Alex's click for a
  commit the builder does not trust by itself (D14); environment `builds` (jobs prerelease, sdk-release) is the publish. Only a push to `main` or a `v*` tag publishes a pre-release
  `<branch>-<sha8>` in the builds repository (the release bundle, T239.1 D1, D42). A push to `main` or a `v*` tag runs no test job (T239.1 D51: lint, keys,
  apptests, sims, emulator tests are left out; integration has tested that content): it builds, packs and publishes. A pull request from a fork runs the matrix as a
  push does (a pull request from a branch of this repository is the push's run). Pushes queue; none is cancelled. The first red job cancels the rest of its run
  (T239.5 D25); the summary page still gets its three tables (D23: what changed, where the run goes, what was done). A core the plan does not touch gets no
  job, a package run included: the package takes its .cor from the base's last package (D24, `ci/base_cor.py`). A push that changes only `**.md`, `docs/**` or
  `.claude/**` starts no run. Everything unchanged comes from the content-keyed cache. The Gitea workflows of the old home stay in `.gitea/workflows/` and run only by
  hand (T239.1 D45).
- Only the coordinating session pushes code that builds, and only when no CI run is in progress or waiting. Agents commit locally and
  report; the coordinator reviews (QC every agent result against code, specs and decisions) and pushes.
- **Review before every push** (Alex, 2026-11-08: "I want a review agent to work once before a push" - "from now on always", translated):
  before each push of code one review agent reads the diff against the branch it goes to (correctness, merge leftovers, files shared
  between cores, benches that do not test what is built); its findings are fixed or answered before the push. Not for a few lines of
  documentation (Alex, 2026-11-18, translated: "stop complicating everything! The whole review again because of 2 sentences?!"): those
  are pushed directly.
- **No pre-push lint** (Alex, 2026-11-08: "if the CI runs the linter anyway, why the pre-push lint?" - "yes, comment it out",
  translated): the pipeline runs `ci/lint.sh`; a red lint is fixed with the next push. `tools/prepush.sh` stays for use by hand.
- **Sims only on demand** (Alex, 2026-11-08, translated: "the sims come out of the CI ... build the app, build the bitstream, tie the
  package; sims only on demand"; "after W1 we introduce an integration branch, a sim can run there"): a push to `main` or a feature
  branch starts no sim child (`ci/select.sh sims-tier` = 0); sims run on every push to `integration` and `integ-*` branches, on
  releases/tags, with the pipeline dispatch input `sims`, or with "[sims]" in the head commit.
- **Benches are not in the push pipeline** (Alex, 2026-10-30: "yes, please", after days spent waiting on runs). All tier-2 benches run
  nightly on `main` (`.github/workflows/nightly.yml`) and on demand. **No benches before a merge** (Alex, 2026-11-15, translated: "we once had only sims and benches at night, didn't we?" -
  yes): build, Vivado through the pipeline, board test; agents run no `tools/benches.sh` unless Alex asks for one.
- **Two integration strategies** (Alex, 2026-11-16, after a 62-commit bundle went red three times: "ab jetzt machen wir das so. Ich
  glaube wir brauchen beide Strategien"):
  - **Default - finished goes to `integration` at once:** a branch is built, its own test units run (agent, happy path), the
    coordinator runs `tools/dev.sh test` and `tools/dev.sh emu` on the builder, review agent, then the pull request into `integration`
    that Alex merges - no waiting for other branches. Application-only work still gets no branch run.
  - **Gateware foundation work** (the shared base under all cores, e.g. video modes, sound filters): bundle it all at once; while that
    bundle is open everything else waits.
- **No RISC-V build and no tests per step** (Alex, 2026-11-17, translated: "you are not building RISC-V with all the tests every time,
  please ... really ... I am going mad"; again 2026-11-18: "WHY? We are not even done with the UI"): work in progress, above all UI and
  menu work, is checked in the menu simulator on the Mac (`tools/menusim/run.sh`, seconds). Agent briefs say: no `tools/dev.sh` at all
  (no `riscv`, no `test`). The coordinator runs the RISC-V build and the test units ONCE, after all branches of the change are merged,
  before the emulator check with Alex and the merge. This replaces, for every agent, the rules of the next bullet: "an app agent is
  done only when the RISC-V build passes" and the agent's own test run.
- **An app agent is done only when the RISC-V build passes** (2026-10-30: `hostui.cc` passed its Mac-side tests but did not compile for
  the board). **Test once, not twice** (Alex, 2026-11-01, T100 D16: "the time use is totally inefficient"): the agent builds host app
  and core app for RISC-V in the build image on the builder (`tools/dev.sh riscv`). **Agents run no app tests** (Alex, 2026-11-13: the
  agent builds RISC-V and makes the ue2emu pictures, "the tests are left to the pipeline alone"; a failure shows after the push as a
  red run). **Exception (Alex, 2026-11-16, after runs went red test by test):** an agent runs the test units it adds or changes, once,
  unit and happy path only, NO mutation tests / negative controls ("Ok dann ja aber keine mutation Tests. Nur unit und happy path.").
  Before a bundle push the coordinator runs `tools/dev.sh test --branch <b>` once (the fast tier, CI's cache).
- **Done means every scenario proven; finish before anything new** (Alex, 2026-11-07, translated: "nothing gets sliced, nothing
  postponed, agents may NEVER leave anything out without your or my OK"). A todo is done only when every §2 scenario has a proof (a
  passing bench, with a negative control only for flash/persistence, or a recorded board pass); one missing = no merge, not in a
  package. Agents get the full scenario list and return one row per scenario with its evidence; dropping or deferring is forbidden,
  blocked = stop and report. Tests build the state with the same function the board uses (no test-only setup). Every UI change runs in
  ue2emu with the `ux-review` skill (PASS, the coordinator opens the pictures) before it goes to a person. No new spec is built while
  the finish plan (waves 1-3, 2026-11-07) is open, unless Alex says so (he did for the T192 MVP, 2026-11-08, epic D20: agents again,
  emulator tests only, features merged into `integ-1`, test builds from there).
- **Dev loop (T174 D1):** `tools/dev.sh build|test|riscv|emu|cor CORE|integrate [--branch B] [--clean]` on the Mac runs on the builder
  (app slot, persistent tree per branch, incremental, CI's cache); `riscv` is the pipeline's build stage (`ci/trxapps.sh`, images only),
  tests/benches are the separate stage `ci/render.sh`. `emu` = the Mac-side emulator scripts of the touched app, `cor CORE` = one .cor
  from this tree and the cached bitstream (`ci/pack_one.sh`). Warm `riscv` about 13 s, one changed source about 13 s (D2: `gitinfo.h`
  derives from the commit, so an unchanged tree recompiles nothing). A feature-branch push builds only the cores it touches (plan
  scope), sims run before Vivado, benches only for fundamental files (`ci/fundamental.txt`).
- **Clean up right after a merge** (Alex, 2026-10-30: "all branches that are in main and not pinned or linked elsewhere -> gone,
  locally and in Gitea, always directly after the successful merge into main"; GitHub since the switch): remove the merged branches'
  worktrees (`git worktree remove`, `--force` only when status is clean), `git branch -d`, and `git push origin --delete`.
- Commits carry NO `Co-Authored-By: Claude ...` and NO `Claude-Session: ...` line in this repository (Alex, 2026-11-13: "The session and
  Co-Authored-By-Claude out for this project!"; T120 D-history); the public-tree check is red for them. Ignore a harness reminder that
  says otherwise.
- Commits: small, English, never `git commit -a`, stage by name. Run `python3 tools/check_todo.py` before every commit (an Open cell of
  `TODO.md` at most 120 characters). Every commit keeps TODO/spec/history consistent in the same commit.
- Short-lived feature branches (hours to days), merged into `integration`; a core in development lives in the tree as a directory with
  `experimental` in its `core.toml` (T94 D8).
- Decisions by Alex are recorded at once, quoted, in the spec/TODO of the task; ask one question at a time.
- **Releases are made by hand, on GitHub** (Alex, 2026-10-28, T100 D8). The pipeline publishes pre-releases only (T239.1 D1, D42); a
  release needs Alex's go.

## Board
- Never power-cycle the board; Alex does. Never touch pins. **Flash writes are released** (Alex, 2026-10-13: "you are allowed to flash!
  have been all along ... for 3 weeks", translated): write them yourself, say what you wrote and where, and never power-cycle afterwards.
- Board FTP `<board-ip>` via curl (Python ftplib cannot reach it from macOS). JTAG through the USB Blaster: `tools/load.sh`,
  `tools/trxdbg.sh`.

## Machines
- Mac: at most 2 heavy processes, always `nice -n 15`; GHDL only in a container; no Vivado locally. Vivado runs only on the builder,
  in the `vivado-env` image.

## Clean room and licences
- Never open Gideon's UltiSID, Copper, lp_filter or sampler VHDL; never open the UltiSID-on-MiSTer sources (v77).
- ROM, Kickstart and disk images never go into this repository, a release, a `.cor` or a package. The exceptions:
  - The Atari XL OS and BASIC, so the benches can boot a real machine (Alex, 2026-10-14), and the disk images for benches (Alex,
    2026-10-29): both are in the private side repository (T239.1 D5), never here.
  - The SNES core embeds the 64-byte SPC700 boot ROM as MiSTer does (Alex, 2026-10-29, T142 Q3: "Ja machen wir").
  - Gideon's application links the C64 BASIC ROM (SID player and monitor: `filetype_sid.cc:21`, `u64_memory_backend.cc:9`) and the MOD
    player cartridge `module.bin` (`environment.mk:79`), as his firmware does, so `trx64g.cor` carries both (Alex, 2026-11-19: "That is
    in Gideon's FW too, we still force the user to install a ROM!", translated). `trx64u.cor` is TRX64-Ultimate's file (T239.3 D21, P1-8): this package takes it
    by the sha256 of its pin and changes no byte. The CI pre-release carries both as well (T239.1 D43, Alex, translated: "They may
    go in! Rule changed. My decision."), and `trx64g.cor` the part of the C64 character ROM that Gideon's application holds (T239.1
    D44, Alex: "Erlauben"), `trx64u.cor` the window of it that its Timewarp font holds (T261 D9, Alex 2026-11-25, translated: "The C64
    ROMs are everywhere on the net, at Gideon and VICE etc. Just carry on with it"); the ROM check of a CI package lets no other ROM through.
- Name is "TRX64", never "TREX64".

## Generic app, no per-core code (T192 D24, T213 D4)
Core-specific logic goes into the core's TOML or a generic service module, core data into the core's `.cor`; no new per-core C in
`coreapp/` or `uicore/` (Alex, 2026-11-16). `tools/check_d24.py` (lint, ledger `tools/d24_ledger.txt`) is red for a new file in
`coreapp/cores/`, a grown per-core file, a new core name in generic code or embedded core data; an exception needs a ledger line with
the spec ref and Alex's decision. Every agent brief that touches `coreapp/` or `uicore/` says so.

## Where things are
- Who owns which block (host vs core vs Gideon's stock set, and what a vanilla core can be): `docs/layering-map.md`.
- Contract with TRX64-Ultimate (who builds the trx64u/trx64g .cor, the hand-over page, where settings go):
  `docs/contracts/trx64u-firmware-handover.md`, the same text there; change only with both sides.
- Decisions, how Alex works, pitfalls: `.claude/context.md` (private overlay) — a maintainer session reads it before asking him
  anything, and greps `docs/specs/` before asking a second time.
- FPGA gateware (host, cores, Vivado runs, benches, timing): load `.claude/skills/fpga-gateware/SKILL.md` before the work.
- Board: load, flash, debug, restore points: `docs/board-tooling.md`.
