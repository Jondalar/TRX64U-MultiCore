# T94 — One TRX64HOST repository, a pipeline from host to core to release set, and GitOps for what runs on a board

**Status:** DECIDED (2026-10-13): all 10 open questions answered by Alex (Q6 revised); not built.
**Status check 2026-11-13:** DONE (Alex 2026-10-28, "Ja, mach done"), no longer "not built": host + n cores, CI by change, packages (P2-P5); not built and not wanted for now: automatic board deploy and package retention.
**Repo:** a new repository (working name TRX64HOST, Q1); this spec lives in TRX64-Ultimate until the new repository exists, then moves (D9).
**Number:** T94 (registry: the TODO table in `TODO.md`; T93 was the last).
**Milestone:** M2.
**Origin:** Alex, 2026-10-13: "I think we need a solution for the whole gitops+cicd topic"; "basically that's a TRX64HOST repo, isn't it, how would you do it? And then every core in a directory. And per config / pipeline trigger we can build HOST+lean TRX64 core for the baseline setup on purpose. Or single cores on purpose that then reuse the generic host?"; on branches: "ok, we do it like that — write it as T94 spec".
**Depends on:** T87 (host key, per-core caches, queue job; D15/D16 reference core), T84 (`.cor` format D5, contract version D3, apps and settings D10-D12), T93 (core application blueprint), T79/T79.1 (host/core split, contract). Builds on the pipeline in `.gitea/workflows/pipeline.yml` and `ci/*.sh`.

Template: C64RE spec template (`_TEMPLATE.md`).

---

## §1 The problem

**Who:** Alex and the other sessions; later every tester who gets a board.
**Trying to do:** work on several cores in parallel (VC-20, Amiga, Atari 800, C16, Apple II, Spectrum, CPC), and end with one boot menu on the C64U that holds all of them and runs on a known release.

**What happens today (read from the tree, branches and CI, 2026-10-13):**

1. *Every core branch builds its own host.* `t83-vc20`, `t85-amiga`, `t88-atari800`, `t89-apple2`, `t90-c16`, `t91-zxcpc`, `t92-cpc` each carry a copy of the host RTL, the floorplan and the contract. A partial is valid only on the bit-identical host it was routed against (T87 §1.4), so partials from two branches do not fit one boot menu unless somebody merges the hosts first by hand.
2. *Apps are built locally.* `softboot.u2a`, the VC-20 app and the coming lean apps are compiled on the Mac and copied; the CI builds the integ-m1 apps only (`ci/app.sh`, `release.sh` `EXCLUDE_APPS`). Nothing says which source produced the `.u2a` that sits on the stick.
3. *Packing and deploy are manual.* A `.cor` is packed by hand (`tools/cor_pack.py`), `boot.cfg` is edited by hand, files go to the board by FTP (192.0.2.189, `/USB1/cores`, `/Flash/boot.cfg`) or by JTAG (`tools/load.sh`, `tools/trxdbg.sh`) from the Mac. What runs on the board is not a release number but "whatever I copied last".
4. *Docs and specs are spread over branches.* The T88-T93 specs sit on their own spec branches (`origin/t93-coreapp`, ...); `main` does not know them; a decision can be on a branch nobody merged.
5. *Artefacts are found by commit grep.* Bitstreams and packages sit in cache directories of the builder (`/cache/bit/<key>/`); to find "the VC-20 partial for host X" one greps manifests. The generic package `trx64-ultimate/<branch>-<8 hex>` and `trx64-release/<id>` exist only for the monolithic integ-m1 line.
6. *Two product lines in one repository.* The 1:1 drop-in (integ-m1, full C64, T87 D16) and the multi-core line (slim reference C64, host + cores) share a repository, a pipeline and a cache, and block each other's decisions.

**Parts (MECE):**

- **P1 Repository.** One repository for the multi-core line: layout, what lives where, what is a submodule.
- **P2 Host as a base artefact.** The host (layer 1 + slim TRX64 reference core) is built once per version and published; every core builds against a published host.
- **P3 Core builds.** A core build fetches its host, implements only its partial, builds its app, packs the `.cor`, publishes.
- **P4 Release set.** One host, a list of `.cor`, the generated `boot.cfg`: one versioned package.
- **P5 GitOps.** A manifest in the repository states the wanted set per board; a deploy step brings exactly that set; a board always runs a release number.
- **P6 Triggers and branches.** Which push or dispatch builds what; short-lived branches; experimental cores.
- **P7 Migration.** From today's branches and repositories into the new one.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 a core-only push builds only that core
  Given host H 0.4.0 is published and the release set R-12 lists amiga and vc20
  When  a file under cores/amiga/ is pushed on a feature branch
  Then  the pipeline fetches host package 0.4.0, builds only the amiga partial, its app and amiga.cor
  And   no host stage runs, no other core is built
  And   the package "core amiga for host 0.4.0" is published with the commit in its manifest

Scenario: S2 a host change rebuilds the baseline and the whole core matrix
  When  a file under host/ or contract/ is pushed on a feature branch
  Then  the baseline is built: locked static, host.bit, slim TRX64 partial, host app, host version, contract version
  And   then every non-experimental core is built against that new host, one after the other
  And   a core that does not route is reported by name; the host is not published as a release candidate (D10)

Scenario: S3 manual dispatch with inputs
  When  the pipeline is dispatched with cores=amiga,c16
  Then  exactly those cores are built, against the host named by the input (default: the latest published host)
  When  it is dispatched with baseline_only=true
  Then  only host + TRX64 core are built and published

Scenario: S4 a release set is one package
  Given the latest published host and one .cor per release-listed core, all built for that host
  When  the release job runs for tag rel-<n>
  Then  one package holds host.bit, the .cor files, the generated boot.cfg and a manifest with every id and SHA-256
  And   a .cor routed against another host is refused with both host ids in the message

Scenario: S5 deploy brings exactly the manifest's set
  Given release/boards/c64u-main.toml names rel-12
  When  the deploy script runs against that board
  Then  the files on the board equal rel-12 byte for byte (extra .cor listed, not deleted without --prune)
  And   the board reports "rel-12"

Scenario: S6 an experimental core is built but not shipped
  Given cores/atari800/core.toml has experimental = true
  When  any core build or the matrix runs
  Then  atari800 is built against the current host and its result is shown
  And   it is in no release set and no boot.cfg

Scenario: S7 an Ultimate-based app is built in CI
  Given apps/ultimate/ pinned to a commit of Gideon's fork
  When  the vc20 or c16 core is built
  Then  its .u2a is compiled by the CI from the pinned submodule and the core's variant flags; no local binary is used

Scenario: S8 docs and specs are on main only
  When  a feature branch is merged
  Then  main holds the spec and TODO row; the feature branch had no spec copy
```

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P3, P6 | core-only push | push a one-line change in a probe core; steps file shows no host step; package exists | CI (probe repository first) |
| S2 | P2, P6 | host change, matrix | push a host comment-free change (new host key); all cores listed in the log, each with its host id | CI (builder) |
| S3 | P6 | dispatch inputs | dispatch with `cores=` and `baseline_only=`; job list matches | CI |
| S4 | P4 | release set | `ci/release.sh` on a test tag; unpack, check manifest, `cor_pack.py --verify` | CI + unit test |
| S5 | P5 | deploy exact set | deploy to a scratch directory (dry-run target `dir:`), compare trees; then once to the board | CI/Mac script; board by hand |
| S6 | P6 | experimental core | flag a probe core experimental; absent from release package | CI |
| S7 | P3 | Ultimate app in CI | build vc20 app from submodule pin; hash recorded in the core manifest | CI |
| S8 | P6 | docs on main | CI lint rejects `docs/specs/` or `TODO.md` changes on a non-main branch that are not in main's history of the same file (warning level, Q10) | CI lint |

## §3 Decision

Alex's decisions (2026-10-13), each as a rule:

- **D1 One repository: TRX64HOST** (name final or open: Q1). Layout:
  ```
  contract/      layer-2 interface (the ports of the empty layer 2), contract version, the null core
  host/          layer 1 RTL, floorplan, constraints, boot ROM, ICAP loader
  host-app/      lean host app (trxbase, boot menu, file browser, core list)
  coreapp/       T93 blueprint (shared services) and the core descriptors
  cores/<name>/  rtl/, layer2_<name>.v, core.toml
                 trx64   slim C64 reference core (D2, D16 of T87)
                 vc20, c16   with the Ultimate-app variant
                 amiga, atari800, apple2, zx, cpc   blueprint descriptors
  apps/ultimate/ Gideon's app fork as a pinned git submodule (C64/VC-20/C16 apps; upstream merges stay possible)
  ci/            stage scripts, workflows' helpers
  release/       release manifests (sets), board manifests
  docs/          specs, ADRs, TODO (main only, D9)
  ```
  The `core.toml` of a core holds: `id`, `name`, `version`, `contract` (the contract version it was written for), `app` (`blueprint` | `ultimate`), `experimental` (bool, default false), `sources` (path list = the core's layer, replaces the `core:<name>` section of T87's `layers.txt`), `ooc` options, `features` (flags such as `TRX_FEATURES`, `TRX_SID`), `roms`.
- **D2 The host is a versioned base artefact**, like a Docker base image. A **baseline build** = host + slim TRX64 reference core yields: the locked static (`static_locked.dcp`), `host.bit`, the C64 partial, the host app, the host version and the contract version, published as a Gitea generic package `trx64host/<host-version>` (T87's host cache entry `host-<H>/` is its build-side form; the package is the durable, addressed form).
- **D3 A core build fetches the matching host package**, implements only its partial against it, builds its app, packs the `.cor` and publishes "core X for host Y" as `trx64core-<name>/<core-version>+h<host-version>`. Without a host package of that version the core build refuses (it never builds a host on the side).
- **D4 A release set = one host + a list of `.cor` + the generated `boot.cfg`**, one versioned package `trx64set/rel-<n>`. **GitOps:** a manifest in the repository states the wanted set per board; a deploy job/script brings exactly that set to stick/flash; what runs on a board is always a release number.
- **D5 Triggers.** Path-based: `cores/<x>/` (and `apps` changes that belong to it) builds only that core; `host/` or `contract/` builds the baseline, then the whole core matrix against the new host. Plus manual dispatch with inputs (`cores=amiga,c16`, `baseline_only`).
- **D6 All apps are built by CI**, the lean ones (host app, blueprint apps) and the Ultimate-based ones (from the pinned submodule). No locally built app enters a package.
- **D7 integ-m1 stays in the existing TRX64-Ultimate repository** as its own product line (1:1 drop-in, full C64, T87 D16). TRX64HOST is the multi-core line. The two do not share a pipeline.
- **D8 Short-lived feature branches** (hours to a few days, then merge to `main`) for every change. A core in development lives as a directory on `main` from the start with `experimental = true` in `core.toml`: CI builds it against the current host; it is not part of the release set. **Amended 2026-10-25 (T115):** the host's reference core is no longer `trx64` but `c64ref` ("C64 Reference"), a slim copy of the C64U core built only to route the host against and never shipped (`build_only = true` in its `core.toml`: no `.cor`, no release set, no boot menu entry); `trx64` is an ordinary core from then on. Alex: "Nein, der Referenz core kann so bleiben wie wir ihn besprochen haben, aber nur zum bauen ! niemals ausgeliefert" ("no, the reference core can stay as discussed, but only for building, never shipped", translated); history in `docs/todo/T115.md`.
  **Extended to the simulations (Alex, 2026-10-14).** Run 31 died in the `sims` stage on a core bench, and with it
  the host bitstream, which has nothing to do with that core - the stages are a chain. Asked which way to fix it,
  Alex chose "the first one": a bench that belongs to an `experimental` core is reported and does not fail the
  stage, exactly as that core's bitstream does not fail the pipeline. `ci/sims.sh` decides whose bench it is from
  `ci/sims.deps`: a bench whose declared files touch exactly one `cores/<id>/` belongs to that core, which is
  true of every core bench and of no host bench (a host bench always reaches into `host/` or the reference core
  as well). Such failures appear in the summary as "experimental core <id>: reported, not fatal" and once more
  under `EXPERIMENTAL` at the end. Today that covers vc20, c264, amiga, amiga1200 and atari800 - everything but
  the reference core, whose benches still fail the stage.
- **D9 Specs and decisions live on `main` only.** Feature branches carry no spec copies.
- **D10 The routing limit stays (T87 D15):** a core that does not route against the locked host must slim down, or the reference changes (Alex decides), which changes the host and rebuilds every core.

**Design that follows from them (to be confirmed in Q-order below where marked):**

**Pipeline as stages with keys and caches (reuses T87; its queue job, one pipeline never interleaves with another, runner choice by `CI_RUNNER`):**

| Stage | Runs when | Input | Key | Output / cache |
|---|---|---|---|---|
| `lint` | every push | tree | none | docs/TODO check (`check_todo.py`), `core.toml` schema, layer-file ownership (a file in exactly one of host / one core) |
| `sims` | files under the changed unit | unit sources | existing `sims.deps` per unit | verilator/iverilog results |
| `host` | `host/`, `contract/` changed, or dispatch | host sources, contract, floorplan, xdc, reference core sources, Vivado | **host key `H`** (T87 D3, now including `cores/trx64/` since the reference core is routed with the host) | `host-<H>/` (T87 D6) |
| `host-app` | `host-app/`, `coreapp/`, `contract/` | sources, contract version | tree key | `host-app.bin` |
| `publish-host` | `host` or `host-app` ran, all gates green, branch is `main` (or `candidate` input) | cache entry | host version (Q3) | package `trx64host/<v>`: static dcp, host.bit, TRX64 partial, host app, `manifest.json` (`H`, `host_id`, `host_sha`, contract version, from-commit) |
| `core <x>` | `cores/<x>/` changed, host stage ran (matrix), or dispatch | host package, core sources | **core key `C(x)`** = `H` + `O(x)` + `core.tcl` + options (T87 D5); OOC cache `O(x)` shared across hosts | `core-<x>-<C>/` (T87 D6) |
| `app <x>` | with `core <x>` | blueprint: `coreapp/` + descriptor; Ultimate: submodule pin + variant flags | app key = tree key of inputs + toolchain image | `<x>.u2a` / blueprint app |
| `pack <x>` | after `core <x>` and `app <x>` | partial, app, host manifest | `C(x)` + app key | `<x>.cor` via `tools/cor_pack.py` (refuses a host mismatch, T87 D12) |
| `publish-core <x>` | `pack` green, branch `main` or dispatch | `.cor`, manifest | core version + host version | package `trx64core-<x>/<v>+h<hv>` |
| `matrix` | host changed | list of non-experimental cores (+ experimental ones, built and reported, not gating) | per core as above | one status per core; any failed non-experimental core fails the pipeline after all ran (T87 D10) |
| `release` | tag `rel-<n>` or dispatch | `release/sets/rel-<n>.toml` | the set file | package `trx64set/rel-<n>` (D4, below) |
| `deploy` | after a release tag when the board is reachable, else pending until Alex's dispatch or the Mac script (Q8) | board manifest | set id | files on the board (see Deploy path) |

Caches are the T87 ones (content keys, branch-HEAD pins, 30 GB cap); this spec adds the published packages as the durable layer on top: a cache entry may be evicted, a package is not. A core build falls back from the package to the cache entry of the same key only for speed, never to a different host.

**Package layout and versioning.**

- Host package `trx64host/<host-version>`: `manifest.json`, `host.bit`, `static_locked.dcp`, `trx64_partial.bit`, `host-app.bin`, `reports/`. Manifest: `host_version`, `contract_version`, `host_key` (H), `host_id`, `host_sha`, `from_commit`, `vivado`, `wns`, `core_ref` (the reference core's version).
- Core package `trx64core-<name>/<core-version>+h<host-version>`: `<name>.cor`, `<name>_partial.bit`, `<name>_full.bit` (JTAG test image), `manifest.json` (`core_key`, `host_id`, `host_sha`, SHA-256 of the partial and of the app, `from_commit`, submodule pin for Ultimate apps, `experimental`).
- Set package `trx64set/rel-<n>`: `manifest.json` (host version/id, per core: name, version, SHA-256, size), `host.bit`, `host-app.bin`, `cores/*.cor`, `boot.cfg` (generated; the boot menu's file as T84 D5a describes it, partial hash and file size per core), `RELEASE_NOTES.md`, `SHA256SUMS`.
- Versions (proposal, Q3): host `H.<contract>.<n>` = contract version plus a counter; a core `x.y.z`; a set a plain counter `rel-<n>`. The host key `H` (content hash) stays the identity for cache and `.cor` matching (T87); the host version is the human name for it, and the manifest maps one to the other. Same sources always give the same host key; the counter only grows when a new key is published.

**Release manifest format** (TOML, in `release/sets/rel-<n>.toml`, hand-edited by Alex, checked by `lint`):

```toml
id = "rel-12"
host = "0.4.7"                  # a published host version
note = "Amiga joystick fix"
[[core]]
name = "trx64";  version = "1.3.0"      # core package must be built for this host
default = true                  # boot menu default (T84 D4a: starts after 5 s)
[[core]]
name = "vc20";   version = "0.9.2"
[[core]]
name = "amiga";  version = "0.2.0"
```

Board manifest (`release/boards/<board>.toml`): `board`, `address` (FTP/REST host), `set = "rel-12"`, `target` (`ftp` | `dir:<path>` for dry-runs), `prune` (bool). The `release` job resolves every `[[core]]` to its package for `host`, refuses when one does not exist for that host, packs the set package and generates `boot.cfg`.

**Deploy path (what can run from CI, what needs the Mac/Blaster/board; the board is never power-cycled by any of this):**

| Step | Where | Notes |
|---|---|---|
| Build, pack, publish, release set | CI (builder/NAS) | no board needed |
| Dry-run deploy to a directory (`dir:`) and diff against the set | CI | S5 |
| Copy `.cor` files and `boot.cfg` to the board | `ci/deploy.sh` by FTP to the board address; runs on the Mac, or on a CI runner that can reach 192.0.2.189 (Q8) | pure file copy; runs while the board is on; replaces only listed files; a copy is written under a temporary name and renamed |
| Host bitstream / host app into flash (`host.bit` at 0x0, T84 D2/D8) | Mac with the USB Blaster (`tools/load.sh`), by hand | only when the set's host differs from the board's host (board reports host id); flash writes are protected (T44, T84 D8); not automatable from CI; the command is given to Alex, never run unasked |
| Activating the new set | the user, in the boot menu or by power-cycle | no script power-cycles, resets or reloads the board's core |
| Verifying | `ci/deploy.sh --check`: reads the set id the board reports, lists files by hash through FTP/REST | read-only |

Where the board reports "rel-12": `boot.cfg` carries the set id in its header; the boot app shows it and the REST info answers it (small addition to T84's boot app, listed in §4 Out of T84's scope until accepted here).

**Triggers (D5) as configuration:** path filters in the workflow (`host/**`, `contract/**` → baseline + matrix; `cores/<x>/**` → core x; `host-app/**`, `coreapp/**` → app stages of every core that uses them, no FPGA stages; `apps/ultimate` pin → the Ultimate-based cores' app stages only). `workflow_dispatch` inputs: `cores` (list, empty = path-detected), `baseline_only` (bool), `host` (published host version or `latest`), `release` (set id), `experimental` (include experimental cores in a matrix: default true). Dispatch is also how a feature branch builds against a host other than the latest.

**Branches (D8, D9):** a feature branch is built by the same pipeline as `main`; it publishes **nothing** to the durable packages (cache entries and run artefacts only). Publishing (`publish-*`) is `main` only, plus an explicit `candidate` dispatch input for a branch whose host is needed by other branches before merge. Merge to `main` is the publish event. A new core starts as `cores/<x>/` with `experimental = true` on `main`; the flag is removed in the commit that adds it to a `release/sets/` file.

**Migration (P7)** from today:

1. Create the repository; import `contract/`, `host/`, the reference core and `host-app/` from the `dfx-trial` + `t84-bootapp` + `t87-ci` lineage (the T87 layered build is the pipeline's core); history vs fresh start is Q2.
2. One core at a time from its branch: `t83-vc20` → `cores/vc20/`, `t85-amiga` → `cores/amiga/`, `t90-c16` / `t90-c16core` → `cores/c16/`, `t88-atari800`, `t89-apple2`, `t91-zxcpc` (ZX), `t92-cpc`. Each import drops the branch's private copy of the host and keeps only the core (its `layer2_<name>.v`, RTL, descriptor); a branch whose host differs from the new reference is rebuilt and checked for routing before it is marked non-experimental (D10).
3. The app branches in 1541ultimate (VC-20/C16 Ultimate-app variants) become the fork pinned at `apps/ultimate/` with the variant flags in `core.toml`; the fork's upstream-merge workflow is unchanged.
4. The T88-T93 spec branches: their spec files are copied to `docs/specs/` on the new `main` (and on TRX64-Ultimate's `main` until the move); the branches are deleted once merged (D9, memory rule: merged branches go).
5. integ-m1 and `main` of TRX64-Ultimate are not touched (D7). Their `T87` queue job and layered build are copied, not moved.
6. The first release set `rel-1` contains the reference core only; it is deployed to the C64U (Alex's board) as the proof of the path before any other core joins.

## Risks

- **Routing (D10).** The 50T reference host leaves little room (T87 D15: the full C64 at 78 % LUT does not route; slim C64 is the reference). Every host change risks a core that no longer routes; the matrix finds it, the cost is a rebuild of the matrix (minutes per core on a cached OOC) and possibly Alex's call on the reference.
- **Host and contract churn.** A new host key invalidates every published core. Mitigation: hosts are published rarely (only on `main`), the host version is visible in every core's package name, old hosts and their cores stay until a retention rule says otherwise (Q9).
- **Submodule drift** between the Ultimate fork pin and upstream: the pin only moves in an own commit with its own pipeline run.
- **One runner pair.** The matrix is serial (T87 Out); N cores times minutes; mitigated by the OOC cache. A parallel matrix is out.
- **Deploy by FTP** while a core app is running on the board: a copy to `/USB1/cores` must not disturb the running app; only listed files are written, to temporary names, and the board is not reset.
- **Package storage** on the Gitea instance (NAS): sets contain `.bit`/`.cor` files of a few MB each; retention is open (Q9).

## Open questions for Alex (one line each, in this order)

1. Q1: Final repository name: `TRX64HOST` as written, or another (Gitea `TREX/<name>`)?
   **Answered (Alex, 2026-10-13):** "b, TRX64U MultiCore": the repository is named TRX64U MultiCore (Gitea path `TREX/TRX64U-MultiCore`, Gitea allows no spaces); "TRX64HOST" in this spec means that repository.
2. Q2: Import the history of `dfx-trial`/`t84-bootapp`/`t87-ci` and the core branches into the new repository, or start fresh with a squashed first commit per unit and keep the old branches as an archive?
   **Answered (Alex, 2026-10-13):** "a": the history of `dfx-trial`, `t84-bootapp`, `t87-ci` and the core branches is imported into the new repository.
3. Q3: Version scheme: host `<contract>.<n>`, cores semver, sets `rel-<n>` (as proposed), or one global version for the whole repository?
   **Answered (Alex, 2026-10-13):** "a": host `<contract>.<n>`, cores semver, release sets `rel-<n>`.
4. Q4: Which core is in the first set `rel-1`: the reference TRX64 alone, or TRX64 plus VC-20?
   **Answered (Alex, 2026-10-13):** "b": `rel-1` holds the TRX64 reference core and the VC-20.
5. Q5: Which boards get a manifest under `release/boards/`: only your C64U, or also the testers' boards (and how does a tester's board get its set)?
   **Answered (Alex, 2026-10-13):** "it will run on the C64U and the U64 Elite II"; "that is the same board! only once with the bling board (keyboard and LEDs) and once without": both get a manifest; they share one board type, so one host and one set of partials serve both (no second matrix).
6. Q6: Does `main` publish a host automatically on every green host change, or only when you tag a host release?
   **Answered (Alex, 2026-10-13), revised the same minute:** first "b"; then "back ... I assumed a TAG, but you are quite
   right. A host update needs new cores, and then, after the cores are built, we need a bundle. So host + all cores"
   (translated). Final: a green host change on `main` publishes the host automatically, triggers the whole core matrix
   against it, and once every core is built the pipeline packs one bundle (host + all non-experimental cores + boot.cfg).
   No core keeps building against an older host on `main`. Tags pick a bundle for release.
7. Q7: May a feature branch publish a `candidate` host for other branches to build against (D8 allows it by dispatch input), or never before it is on `main`?
   **Answered (Alex, 2026-10-13):** "a": a feature branch may publish a candidate host under its own name for other branches to build against; a candidate never enters a bundle.
8. Q8: Deploy: may a CI runner copy a set to the board by FTP by itself (after a tag, or on a schedule), or is deploy always your manual dispatch or Mac script? Flash writes of `host.bit` stay by hand in either case.
   **Answered (Alex, 2026-10-13):** "preferably a, but the board will not always be online. So both": after a release tag the CI deploys by FTP by itself when the board is reachable; if it is not, the deploy is marked pending and is done by Alex's dispatch or the Mac script later (the manifest states the wanted set, so a later run catches up).
9. Q9: Retention of published hosts/cores/sets: keep all, or the last N sets and the hosts they use?
   **Answered (Alex, 2026-10-13):** "b, 5 I would say": the last 5 bundles are kept, with the hosts and cores they contain; everything else is removed.
10. Q10: Is "specs only on `main`" enforced by a lint that fails a feature branch touching `docs/specs/`, or only a rule we keep?
   **Answered (Alex, 2026-10-13):** "b": specs only on `main` is a rule we keep, no lint.

### D11 Claude project of the new repository (Alex, 2026-10-13: "fits like that!", translated)

The new repository gets its own Claude Code project: its own `CLAUDE.md` (TRX64 rules: only the coordinator pushes code
and only with an idle queue, clean room, board rules, specs only on main, deploy freeze until the first bundle) and
`.claude/` (the T79 site moves there from `1541ultimate/.claude/trx64-site`). The TRX64 memories move selectively from the
1541ultimate project memory to the new project's memory (T79 state, CI runners and triggers, Blaster/ESP32, C64U = U64 E II,
the working-style feedback); Gideon-firmware and cartridge topics stay. The helper scripts that live in job directories today
(`.cor` packing, board load, experiment Tcl) move into `tools/` and `ci/`. Work continues in a new session started in the
new repository with a hand-over note; the 1541ultimate session ends after the run-406 flash and the move.

## §4 Out

- The integ-m1 product line (D7): its repository, pipeline and releases stay as they are.
- Building cores in parallel on the 16-core builder (T87 Out).
- A 100T or other boards than the C64U (T84 Out).
- Signing of packages or `.cor` (T84: hash only).
- Automatic power-cycle, reset or core reload of the board from any pipeline step.
- Changes to the `.cor` format, the contract or the boot app beyond reporting the set id (T84, T79.1).
- The cores themselves and the blueprint (T83, T85, T88-T93).

## §5 Decisions taken while building

P1, the layout (branch `t94-layout`, 2026-10-13). D1 names the directories; these are the calls the
tree needed beyond it. Each one is derived from the code, not from taste, and each is reversible.

- **The core branches are merged first, the tree is moved second.** `t84-bootapp`, `t83-vc20`,
  `t85-amiga` and `t90-c16core` all descend from `dfx-trial` and collide with each other in
  `designs/c64_basic` (build.tcl's core switch, layers.txt, ci, TODO). Resolving that once on the old
  layout is cheaper than four merges into moved paths; the moves are then pure renames.
- **`TRX_CORE` is one comma list** (t90-c16core's form), and every core has its own out-of-context
  checkpoint variable. Before, the VC-20 and the Amiga block both wrote `l2v_dcp`, so two cores in one
  run would have overwritten each other.
- **`cores/common/` for layer-2 modules that more than one core instantiates.** `layer2_trx64.v`,
  `layer2_vc20.v` and `layer2_c264.v` all instantiate `drive_l2` and `iec_bus`: the 1541 drive and the
  IEC bus belong to no single core. They are `shared:` in layers.txt, because the host's VHDL list
  reads the drive too.
- **`vendor/` for third-party trees the HOST reads**: `vendor/icepi` (the host reads its hdmi and
  usb_hid_host sources) and `vendor/gideon` (generated, the host's RISC-V and 1541). A vendored machine
  that belongs to exactly one core stays under that core (`cores/vc20/rtl` and so on). Ownership
  follows the layer that reads it. Before this, `cores/icepi` was in the C64 core's section although
  the host read from it, so a change there did not invalidate the host key.
- **`host/layers.txt` keeps `host:`, `shared:`, `ignore:` and `reference:`; the `core:` sections are
  gone** (D1: `core.toml` replaces them). `ci/lib.sh` reads the cores, their build order and their
  sources from `cores/*/core.toml` (`layer_cores`, `layer_sections`, `layer_specs`), so a new core is
  one directory with one descriptor and no second bookkeeping file.
- **Every core owns its layer-2 synthesis**: the VC-20, Amiga and C264 blocks moved verbatim from
  `build.tcl` into `cores/<name>/core_sources.tcl`, each setting `l2_dcp` - the same name the reference
  core's recipe sets. `host/core.tcl` (the host's implementation recipe, back in `host/`) sources
  `cores/$TRX_CORE/core_sources.tcl` and is generic.
- **The core id of T90 is `c264`, not `c16`** as D1's list spells it: the core is the whole 264 family
  (C16, C116, Plus/4), the code and the partial's file name already say `c264`, and the Ultimate app's
  variant flag is `c264`. The package name is `trx64core-c264`.
- **Core versions at the start of the line**: trx64 1.0.0 (on the board), vc20 0.9.0 (first cut
  accepted, run 404), amiga 0.2.0 (run 406), c264 0.1.0 (simulation only). `experimental = true` for
  all but the reference core until a core is in a release set (D8).
- **`contract/` holds the port list and the version, not a design.** The contract is the 228 ports of
  the `l2` cell in `host/top.v`; `tools/contract_ports.py` writes them down from the reference core and
  `--check` compares every `cores/<name>/layer2_<name>.v` against them in the lint stage (all four
  match today). The **null core** (every output idle) stays T81's gateware - T84 keeps it out of its
  scope - and lands beside the port list when it is built.
- **This repository gets its own cache root on the builder** (`/cache/multicore/bit`,
  `TRX_CACHE=/cache/multicore`, handoff `/cache/multicore/runs/<run id>`), `/cache/heavy.lock` stays
  shared. The two product lines share the machine, not the cache (D7): `ci/cache_gc.py` keeps the
  newest entries per kind, so one line's matrix would evict the other's, and the two repositories'
  run ids are numbered independently and would collide in `/cache/runs`. Agreed with the
  TRX64-Ultimate session on 2026-10-13.
- **Every core's bitstreams go into the generic package** (Alex, 2026-10-13: "that is MY private NAS that Gitea runs
  on. NOT public. get rid of the rule", translated). Since T83 (5771045) `ci/package.sh` deleted everything named
  vc20, amiga or c264 from the package, because the cores are TREX-internal - so the VC-20 and Amiga `.cor` files on
  the stick had to be packed by hand from the builder, and a board session could not simply fetch a build. The rule
  was written for a RELEASE; the generic packages live in Alex's private Gitea on his own NAS, per branch and per
  commit, and they are the path a build takes to the Mac and the board. The strip is gone; the NOTICE of every
  vendored core carries the amendment. What stays forbidden: a public release, an update file for testers, a
  download outside this instance, and ROMs anywhere near the repository.
- **The queue job's runner label is written out** (`runs-on: nas`). In Gitea 1.27.3 an organisation
  variable is not visible in the caller workflow's job-level expression while the called workflows do
  see it, so `vars.CI_RUNNER == 'builder' && 'nas' || 'builder'` put the queue job on the builder -
  the pool the stages need. Runs 791-798 of this repository deadlocked that way. `ci/wait_turn.sh`
  now gives up after `WAIT_TURN_MAX` minutes (default 240) instead of holding a runner slot until the
  job timeout.

### D12 A core never edits a host or a shared file (Alex, 2026-10-14: "and NO core folder may EVER touch the host unless I allow it explicitly", translated)

The T88 merge gave `vendor/icepi/6502n/proc_interrupt.vhd` a generic `g_nmi_direct` (default false) so
the Atari's CPU could take NMI at the enable edge. Functionally the C64 was untouched - but the file
content moved, and with it the key of the reference core and of the static. The rule that follows:
core-specific behaviour lives in the core's own directory. The Atari now has
`cores/atari800/proc_interrupt_a8.vhd` and `proc_core_a8.vhd` (copies, renamed entities, the direct NMI
fixed in them, all other sub-units still shared from `vendor/icepi/6502n`), its adapter instantiates
`proc_core_a8`, and `vendor/icepi/6502n` is byte-identical to its pre-T88 content again. Both CPUs
elaborate under GHDL. The price of the copy is a second place to fix a 6502 bug; the price of the
alternative is every core being able to rebuild the C64.

### D13 The host key counts only what the host reads, and the host is pinned

Three defects came out of the same incident, all of them in the key, not in the design:

- `host/layers.txt` is itself in the `host:` section, so a line about a new core's bench directory moved
  the host key. It is now `:(exclude)`d there and listed under `ignore:`: adding a core cannot move it.
- `shared:` held all of `vendor/icepi`, although `host/build.tcl` reads exactly `vendor/icepi/hdmi/*.sv`
  and `hdmi/hdmi.v` from it. `shared:` is now `vendor/icepi/hdmi`; the rest of the tree is in the
  descriptors of the cores that read it (`cores/trx64`: all of `vendor/icepi`; `cores/atari800`:
  `vendor/icepi/6502n` for the shared sub-units). `tools/core_toml.py` therefore accepts a `vendor/`
  pathspec in `sources`.
- Nothing stopped a change to the static. `contract/host_pin.txt` now holds the git object id of every
  file the static is built from - `host:`, `shared:` and the reference core, because the host is routed
  together with it - and `tools/host_pin.sh --check` runs in the lint stage. Any change there fails the
  pipeline and names the file; an approved host change carries `tools/host_pin.sh --update` in the same
  commit. "As long as the host is immutable afterwards until we change it ourselves - fine!" (Alex).

**Approved host changes, newest first** - the pin's audit trail. Every line is one commit that carries
`tools/host_pin.sh --update`, with Alex's words for it:
- **2026-10-14, `cores/trx64/core.toml`:** `TRX_SID=redip2` dropped, so the reference core is built
  as the default 8-SID reDIP (`SID_MULTI`). That is the only thing that sets `CAPS3 b0`, and without
  it the application shows Gideon's UltiSID menus and curves instead of the reSID8 ones. Alex, after
  seeing it on the board: "I want the reSID filters ... and it should say reSID there. Not UltiSID"
  (translated). It is not more SIDs for the user (8 = two UltiSIDs split in four) and it costs no
  area: T25.4's table has `sid_multi` NS=8 at 1,501 logic LUT and 4.0 BRAM against `sid_api` NS=2 at
  1,557 and 6.5. The switch was carried along unnoticed from the T79 settings when the descriptors
  were written (`0933b5d`). Moves the host key once: the reference core is routed with the host. The rule that
  the other `TRX_SID` values are comparison builds for a bench and never for a release lives in
  T25.5 §0, not in the recipe: a comment there is part of the host key and would cost a full host
  route (Alex's rule itself: our cores show Dag's curves or none).
- **2026-10-14, `host/pins.xdc`, `host/build.tcl`, `host/core.tcl`, `tools/vivado_lib.tcl`:**
  `BITSTREAM.CONFIG.USR_ACCESS` carries the host key instead of a `TIMESTAMP`, so a `.cor` can name
  the host it was routed against and the boot app can read the running one back (T84 D5b). Alex asked
  for it - "can't that be the commit of the host bitstream?" - and approved the attempt: "then try it
  on a branch and if it works off to main with it" (translated). Two pin updates for the one approved
  change: the first put the property in the XDC, where `if` is forbidden and the block was dropped
  (run 26 built a host with no AXSS word), the second moved it into `trx_axss` in the Tcl flow. It
  moved the host key twice and re-routed the host and every core each time.

Still to do, found while reading `host/build.tcl` for this: the host compiles five files out of
`cores/trx64` (`trx_io.v`, `trx_sys.v`, `dma_win.v`, `trx_engine.v`, `drv_dbg.v`). They are host blocks
and belong in `host/`; the rename is a P2 item and moves the host key once, deliberately.
A core whose `core_sources.tcl` does not exist yet is skipped by the bitstream stage instead of failing
it: the descriptor exists for the lint and the caches, the machine is not built yet. T88's Atari was
that case until 2026-10-14; its recipe exists now, and the first build showed what a skip hides - the
core failed in six seconds of synthesis and the stage stayed green because it is experimental (D8),
with the reason only in a log that is kept on success.

### P5 The release step is a tool in the tree (Alex, 2026-10-15)

Alex asked that the release make "a .zip with all the files and put it in Gitea too" (translated). The
zip was already there and complete - `tools/make_release_mc.sh` packs the whole bundle directory, so no
file can be forgotten by a stale list, and `tools/publish_release_mc.sh --zip` attaches it. What was
*not* there is the step itself: the chain from a finished CI package to a tagged release lived in a
session scratchpad and would have died with that session.

`tools/release_mc.sh <package version> [--publish]` is that chain, one call:

1. fetch the package's `.cor`, `c64_basic.bit` and `manifest.json` from the Gitea generic package
   (never the reports - they are 30 MB of the 60 and nothing in the release reads them);
2. read the host key out of the bitstream (`cor_pack.py hostkey`) and the bitstream's commit out of the
   manifest; take the app's commit out of the app image itself (`make gitinfo` writes it in), so a
   mislabelled release needs an explicit `--app-commit` to happen at all;
3. check **every** `.cor` against that key and contract 1 - one bundle for another host stops the release
   before anything is uploaded;
4. `make_release_mc.sh`: `.cfw`/`.ue2`, `INSTALL.md`, `SHA256SUMS`, the zip. `INSTALL.md` is the whole tester
   guide (Alex, 2026-10-21: "everything has to go into the INSTALL.md", translated): install, stick tree, ROMs and
   legal sources per core, first start, in-core keys, `.cfw`, known problems, reporting. The stick may be FAT32 or
   exFAT (Alex, 2026-10-21: "sticks may be exFAT too!", translated). The per-core facts are the
   `CORES` table of `tools/release_notes_mc.py` (each line names its source); `tools/test_make_release_mc.py` checks
   a line per `cores/*/core.toml` and that every link stays on the legal hosts (`LINK_HOSTS`);
5. `release_check.py zip`: the zip has to hold every file of the bundle at the size it has on disk -
   missing, resized and stray entries each fail. `tools/test_release_check.py` is its negative control
   (four cases, in `ci/lint.sh` as "release checks test");
6. with `--publish`: the tagged Gitea release with all 12 assets.

Without `--publish` it builds and checks and says how to publish - the step a release gets wrong is the
upload, not the build, so that is the one that needs a deliberate flag. Proven against
`main-51e89821`: byte-identical `.cfw` to the release published by hand an hour earlier
(`ddd320c1a69cb81045e551870d4744b2ff3b95a78bed3a3ea525d2185336a302`), 11 files in the zip.

This is still a tool run by a person. The CI stage that calls it on a tag is P5's remaining half.

### P6 Our own applications are built by the pipeline (Alex, 2026-10-15)

Alex, after an evening of hand-built images: "Bau bitte per Pipeline, das sind laute manuelle krücken
gerade." He was right about the shape of it. Until this stage, CI built only the submodule's
`ultimate.app` (`ci/app.sh`). **Our** two applications - the host boot app (`host-app/`, T101) and the
core-application blueprint (`coreapp/`, T93) - were built on a Mac, and from there:

- the blueprint images went into the generic package `trx64-coreapps`, uploaded by hand, which
  `ci/package.sh` then downloaded to pack the `.cor` bundles. A release therefore carried whatever image
  somebody had uploaded last - that is how `trx64mc-main-51e89821` shipped `.cor` files whose application
  was not built from the commit the release names, and the `.app`-header defect of T93 5.12 reached the
  stick;
- `tools/release_mc.sh` took the host app from a local `result/` directory, so the same was true of the
  update file.

**Stage `trxapps`** (`ci/trxapps.sh`, `.gitea/workflows/trxapps.yml`) builds both in `u64-build-env:ci`,
the image and toolchain the `app` stage already uses, with the same ccache wrappers. It runs beside
`app`, needing only `lint`, because it does not wait for a bitstream; `package` now needs it. Its result
is cached by `git rev-parse HEAD:host-app HEAD:coreapp` plus the submodule commit - the trees, not the
commit, so a documentation push rebuilds nothing, and the submodule because the third-party parts (lwip,
lvgl, FreeRTOS, chan_fat, httpd) are links into it. One job per application: `all` has `gitinfo` beside
the compile targets (T93 5.15).

`ci/package.sh` takes `hostapp.app`, `hostapp.elf` and `coreapp.bin` from the stage into the package and
records `trxapps_commit` in `manifest.json`.

**The release's update file is built there too, from 2026-10-15.** The stage `update` wrote only the
tester file (`trx64_update_*`, bitstream + the Ultimate application); the MultiCore one
(`trx64host_update_*`, the same bitstream + our boot application) was built on a Mac inside
`u64-build-env` by `tools/release_mc.sh`, so a release could only be made on a machine that still had
that 13 GB image - and on 2026-10-15 that machine no longer did, which stopped a release of a finished
package. `ci/update.sh` now builds both from the same package (`build_update_host` in `ci/lib.sh`, the
application commit taken from the manifest's `trxapps_commit`) and publishes both; `make_release_mc.sh`
takes a prebuilt pair with `--update`/`--update-ue2` and only builds one when the package has none.
`tools/test_make_release_mc.py` covers it, with half a pair as the control: a bundle with a `.cfw` and
no `.ue2` installs on our firmware and not on stock, and nothing downstream would have noticed.

Run 51 failed on the first try and the reason is worth keeping: `build_update_host` handed the whole
40-character sha to `trx_pkg.py`, whose manifest record keeps 16 bytes NUL-terminated per commit
(`trx_pkg.py:40-41`), so it refused with "does not fit 16 bytes". Eight digits now, as every other
caller sends, and `tools/test_trx_pkg.py` has the case with both ends of the range - a full sha refused,
fifteen characters accepted.

**The five Gideon-base applications still come from the uploaded package, and that is the remaining
crutch.** Run 48 showed it the hard way: the first version of this stage replaced the
`trx64-coreapps` download outright, and the package came out with two `.cor` files instead of seven.
Only the blueprint cores (a descriptor in `coreapp/src/table.cc`: atari800, nes) are built here; trx64,
amiga, amiga1200, c264 and vc20 run Gideon-base applications that live in other trees and are not built
in this repository (T93 5.2). So `ci/package.sh` merges: it fetches `trx64-coreapps` for every core and
then copies the stage's images over it, so a hand-uploaded file can never overwrite an image built from
this commit, and a core with no application anywhere is named and skipped. Building them here is decided.

**Alex, 2026-10-15: "Die Apps *müssen* zu ubns, bis auf TRX64 - das teilen wir uns mit dem anderen
Projekt. Der REST nicht"** (translated: the applications must come to us, except TRX64, which we share
with the other project; the rest does not). So four of the five move into this repository and its
pipeline: `vc20.app`, `c264.app`, `amiga.app` and `amiga1200.app`. `trx64.app` stays shared with the
TRX64-Ultimate line and keeps coming from there.

What they are, measured on 2026-10-15 (and confirmed by the TRX64-Ultimate session):

| worktree | branch | builds | state |
|---|---|---|---|
| `1541ultimate-wt-c16app` (removed 2026-10-22; branch `t90-c16app` on Gitea) | `t90-c16app` | `vc20.app`, `c264.app` | pushed, `462af73f`; **contains the VC-20 line**, the two are one tree with `VC20_APP` / `C16_APP` |
| `1541ultimate-wt-amigaapp` (removed 2026-10-22; branch `t85-amigaapp` on Gitea) | `t85-amigaapp` | `amiga.app`, `amiga1200.app` | **tip `619e040b` is not pushed** (Gitea has `3dd051eb`); a separate line, but only 14 commits above the common base `8c65042f` |
| (TRX64-Ultimate CI) | - | `trx64.app` | stays shared, not traced yet |

They are branches of the 1541ultimate TREX fork - the same repository this one already carries as the
submodule `apps/ultimate` - not patches on top of Gideon's app: 283 commits above `trex/main` for the
VC-20 line, about 200 source files beside a vendored LVGL.

**How they come here was decided the same evening, and it is not by moving the fork: T104.** The first
attempt was to merge the two lines into one tree (`t90-c16app` plus the Amiga's 14 commits; all four
applications built from it) and to carry that tree here as its own repository. It worked and it was
thrown away within the hour: it drags ~17,000 files of Gideon's application with it, it brought his CI
along - a Nios II build started the moment the branch was pushed - and it keeps every machine's menus in
`#ifdef` branches of a foreign program. Alex: "Warum hast Du Gideons Verzeichnis mit den gefühlt 17.000
files geforkt und auch Makefile usw?" The answer that followed is [T104](T104-generic-core-app.md): the
four machines become descriptors in their own bundles on one generic application, and the fork is not
maintained. What stays true of P6 is the half that is built: CI builds our applications, nothing is
carried in by hand. `tools/release_mc.sh` now defaults to the package's own `hostapp.app` and reads that
commit; `--app` stays as an override. A package built before this stage has no `hostapp.app` and the tool
says so instead of silently taking a local file.

Measured locally in the same image: 1 min 25 s cold, 0 s from the cache, `hostapp.app` 645,960 bytes,
`coreapp.bin` 633,752 bytes, `coreapps/atari800.app` and `coreapps/nes.app` the same bytes as the image
(T93 D14: one image for every core with a descriptor).
