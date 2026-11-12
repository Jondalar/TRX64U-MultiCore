# T87 — The CI caches per layer (host, each core) and a pipeline that never interleaves or cancels another

**Status:** DECIDED (2026-10-12): all open questions answered; not built.
**Repo:** TRX64-Ultimate (`.gitea/workflows/`, `ci/`, `designs/c64_basic/build.tcl` split, `tools/cor_pack.py` check). Nothing in C64RE or ue2emu.
**Number:** T87 (registry: the TODO table in `TODO.md`; T86 was the last).
**Milestone:** M2
**Origin:** Alex, 2026-10-12, after run 382 lost its "Update files" stage: "no more interleaving or cancelling between pipelines"; "caches per layer: a host cache keyed only by the host sources, per-core caches keyed by host key plus that core's sources; a core-only change loads the cached routed static and implements only the partial"; retention "not a flat 8: what the branch HEADs reference plus some history", content keys, not branch keys, so branches with the same host share entries.
**Depends on:** T79 / T79.1 (host/core split, branch `dfx-trial`), T83 (second configuration, branch `t83-vc20`), T85 (third, `t85-amiga`), T84 (`.cor` manifest). The pipeline part (P1) depends on nothing and can go first.

Adopted 2026-10-12 from the C64RE spec template (`C64ReverseEngineeringMCP/specs/_TEMPLATE.md`,
Alex: "we use that from now on"). Older specs keep their own layout.

---

## §1 The problem

**Who:** Alex and the other sessions, pushing several branches a day to one Gitea runner pair (NAS, builder).
**Trying to do:** push a core-only change (a VIC-20 or Amiga file) and get the new partial bitstream; push a second branch meanwhile and have both finish.

**What happens today (read from the code and the run list, 2026-10-12):**

1. *Pipelines interleave and cancel.* `pipeline.yml` chains reusable workflows (lint, sims, app, bit-c64u, package, update, bit-ue2), each a separate job, under `concurrency: group: gateware, cancel-in-progress: false`. Every stage queues in the group on its own, so two pipelines alternate stage by stage. Gitea keeps ONE waiting entry per group: a newer push replaces whatever waits. Run 382 (dfx-trial `2dfff692`): `bit-c64u` and `package` succeeded, `update` was cancelled at 19:16:11 UTC while waiting, because run 384 (t83-vc20) was pushed; run 383 (t85-amiga) ran its `app` stage between 382's `package` and 382's `update`. Earlier notes: runs 280, 289, 351 (comments at the top of `.gitea/workflows/*.yml`); `heavy_lock` (T31 row 13) only keeps two heavy stages from overlapping, it does not order stages and does not stop the cancel.
2. *One cache key for the whole chip.* `gateware_key` (`ci/lib.sh`) hashes `designs`, `constraints`, `cores`, the Vivado helper scripts and the Gideon patches (tests and `build/` excluded) plus part, Vivado version, SID and slot options. A one-line change in a core re-synthesises, places and routes host and core: 20 to 40 minutes (Alex's figure for the monolithic chip). It also rebuilds `ddr2_test` and `hdmi_test`, which are built under the same key in the same stage.
3. *Flat retention.* `cache_put` keeps the 8 newest entries per kind (`c64u-*`, `ue2-*`) over all branches: eight pushes of one busy branch evict the entry that `main` or `integ-m1` still needs.
4. *Partials only fit one host.* `designs/c64_basic/build.tcl` on `t83-vc20` already implements the VC-20 module into the locked routed static of the same run (`TRX_CORE=vc20`, second configuration, `c64_basic_pb_l2_vc20_partial.bit`). A partial is valid only on the bit-identical host it was routed against, so `.cor` bundles can only be packed from one run, and nothing in the bundle says which host that was.

**Measured on the builder (read-only, 2026-10-12):** cache root 2.1 GB: `bit/` 76 MB (10 entries, 7 to 9 MB each), `ooc_c64_core_*.dcp` 35 files = 146 MB (4.7 to 4.9 MB each), `app/` 902 MB, `runs/` 675 MB (50 hand-over directories of about 14 MB), `t27/` 213 MB, three MIG IP caches of 19 MB. The DFX build of 2026-10-12 19:23 (entry `c64u-2234893c8ac1b9ca`, from `f01e940e`) took about 617 s of Vivado steps for `c64_basic` (three synth runs 36 s, 35 s, 113 s; place 101 s; route 159 s; a second opt/place/route pass of about 90 s that is the VC-20 configuration; bitstreams 26 s and 9 s) per its `c64_basic.steps.txt`. A host checkpoint (routed static) is not cached today, so its size is not measured.

**Parts (MECE):**

- **P1 Serialisation.** Pipelines run one after the other, in push order, and none is cancelled by a newer push.
- **P2 Host cache.** Layer 1 (static) is built once per host key and reused by every branch with the same host sources.
- **P3 Core caches.** A core's partial is implemented against the cached static, keyed by host key plus that core's sources; the OOC synthesis is cached under its own key.
- **P4 Identity.** A partial can never be combined with a host it was not routed against; the check is in CI, in the packer and on the board.
- **P5 Retention and disk.** What is kept, what is deleted, how much it costs.
- **P6 Migration.** The monolithic `integ-m1` / `main` build keeps working, unchanged, until the layered build is switched on.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 two pushes from different branches are both built, in order
  Given the runner is busy with a pipeline of branch A
  When  branch B is pushed, then branch C, then A again, all within ten minutes
  Then  every pipeline runs to the end in push order, each with all its stages
  And   no run has the state "cancelled" and no stage is skipped because another run was queued

Scenario: S2 a stage is never cut off between two stages of its own pipeline
  Given pipeline X has finished `package`
  When  another pipeline is pushed before X's `update` starts
  Then  X's `update` runs before any stage of the other pipeline

Scenario: S3 a core-only change implements only the partial
  Given the host key H is cached with its routed static
  When  one source file of the VC-20 core is changed and pushed on a branch
  Then  the log says "host cache hit H", no host synth/place/route step appears in the steps file
  And   one partial and one full-chip image for core vc20 are built against the cached static, pr_verify passes
  And   the Vivado steps for it take about 5 minutes (estimate, §3 D13), not about 10 to 40

Scenario: S4 a cached host entry is shared by two branches
  Given branch A built host H
  When  branch B (other branch name, same host sources, other core) is pushed
  Then  B reports "host cache hit H" and reads the same entry; the cache holds one host entry for both

Scenario: S5 a host change rebuilds the host and every core that is asked for
  Given host H and the core entries keyed on H
  When  a host file (a layer 1 module, the floorplan, `constraints/c64u.xdc`) changes
  Then  a new host H' is built; core entries of H are not used for H'; the OOC dcp of the cores IS reused

Scenario: S6 a .cor with a partial of another host is refused
  Given partial P routed against host H and a running host H'
  When  `tools/cor_pack.py` is given P with the manifest of H', and when the boot app is given a .cor whose host_id is not the running host's
  Then  the packer exits non-zero naming both ids; the boot app lists the bundle as "wrong host" and does not start the swap
  And   a .cor whose host_id matches loads (board, by hand)

Scenario: S7 an evicted and rebuilt host does not adopt old partials
  Given the host entry H was deleted and rebuilt from the same sources (bit-different bitstream)
  When  a core entry recorded against the old host_sha is looked up
  Then  it is a miss (host_sha differs); the partial is implemented again

Scenario: S8 retention keeps what a branch HEAD uses
  Given main, integ-m1 and four feature branches, 30 entries of history
  When  the janitor runs
  Then  every entry pinned by an existing branch or a v* tag is still there, the unpinned rest is cut to the history rule, and a deleted branch's pins are dropped

Scenario: S9 the monolithic build is unchanged
  Given a tree without `designs/c64_basic/layers.txt` (integ-m1, main today)
  When  the pipeline runs
  Then  `bit-c64u` builds and caches exactly as before (key, files, WNS gate); only the pipeline and retention change

Scenario: S10 a failed core does not poison the host
  Given a core whose partial misses timing (WNS < 0)
  Then  the core stage fails, nothing is cached for it, the host entry and the other cores' entries stay
```

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 | three branches + a repeat push, no cancel | probe repo with two runners labels and a 60 s sleep stage, then the same push script on the real repo; check the run states through the Gitea API (`tea`) | probe repo, then CI |
| S2 | P1 | no interleave | same probe, stage order read from the job log timestamps | probe repo |
| S3 | P2, P3 | core-only change | push of a one-line core change on a scratch branch; steps file and log asserted by `ci/test_layers.sh` | CI `bit-c64u` on the builder |
| S4 | P2 | host shared | two scratch branches, `ci/test_layers.sh` compares the entry paths | CI |
| S5 | P2, P3 | host change | one-line host change on a scratch branch | CI |
| S6 | P4 | wrong host refused | `tools/cor_pack.py` unit test (extends T84 S3); the boot app part by hand | CI `lint`/tests, board |
| S7 | P4 | rebuilt host | `ci/test_cache.sh` with a fake cache directory | CI `lint` |
| S8 | P5 | retention | `ci/test_cache_gc.sh` on a generated cache directory, dry-run output asserted | CI `lint` |
| S9 | P6 | monolith unchanged | the first pipeline on `integ-m1` after the change: key, file list and WNS equal the previous run | CI |
| S10 | P3 | failed core | scratch branch with a deliberately slowed core path | CI |

## §3 Decision

**Pipeline (P1)**

- **D1 A pipeline is one unit in the queue, not seven.** Options weighed for Gitea 1.27.3 (behaviour of a group with no waiting slot beyond one is the cause, so every option that keeps the `gateware` group still cancels):

| Option | What it is | For | Against |
|---|---|---|---|
| O1 | One job per pipeline (`ci/pipeline.sh` calls the stage scripts in order, log groups per stage), NO `concurrency:` group, runner `capacity: 1` per machine | Gitea's own queue is FIFO and never cancels without a group; no lock files; nothing can interleave because there is one job; the simplest | Gitea no longer shows seven stage boxes (stage = log group + a commit status per stage); a cheap `lint` of a new push waits behind a long build on the same machine; a stage can no longer be re-run alone from the UI (the stage workflows stay for `workflow_dispatch`) |
| O2 | O1 but `capacity` 2 or more per machine and a FIFO ticket lock in the heavy part (`ci/lib.sh heavy_lock` made fair: a ticket file per pipeline, oldest live ticket goes first) | lint/sims of a new push do not wait behind Vivado | the lock is ours to get right: stale tickets after a runner crash need a reaper (Gitea API run state); more code than O1 |
| O3 | Keep seven jobs, no group, an `enqueue` job takes a ticket and a final `release` job returns it; every stage waits for "my ticket is the oldest" | keeps the stage boxes | `release` must run when a stage failed: `if: always()` is what broke runs 332 (stage skipped = run waiting for ever); a crash leaves a ticket; most moving parts |
| O4 | One group per stage (`gateware-bitstream` …) | trivial | each group still keeps one waiting entry and cancels the older: strictly worse than today |
| O5 | One job per pipeline but keep the `gateware` group | trivial | the waiting pipeline is cancelled whole by the next push: fixes the interleave, not the cancel |

  **Proposed: O1.** It meets "no interleave, no cancel" with the least machinery; its price (no stage boxes, lint waits) is Q1. The old single-job `build.yml` branches keep their `gateware` group; they cancel only among themselves and do not take part. Behaviour of Gitea 1.27.3 with no group and `capacity: 1` is checked in the probe repo first (S1, S2) before the real workflow changes. Which machine takes a pipeline stays `CI_RUNNER` (T31 / `reference_ci_builder_runner`); each machine has its own queue.

**Layers (P2, P3)**

**Where D2 to D10 live since T94 (2026-10-13, layout T94 D1):** `designs/c64_basic/layers.txt` is `host/layers.txt`
and keeps `host:`, `shared:`, `ignore:` and `reference:`; the `core:<name>` sections are gone - the cores, their build
order and their sources come from `cores/<name>/core.toml` (`ci/lib.sh` `layer_cores`, `layer_sections`,
`layer_specs`, checked by `tools/core_toml.py --check`). The reference core is `trx64`, not `c64`. `core.tcl` is
`host/core.tcl` and sources `cores/$TRX_CORE/core_sources.tcl`, which must set `l2_dcp`; the boundary procs
(`trx_keep_boundary`, `trx_check_boundary`) are in `tools/vivado_lib.tcl`, because the host stage and every core
stage call them. The cache roots are this repository's own (`/cache/multicore/bit`, `TRX_CACHE=/cache/multicore`),
the generated IP stays in the shared root (its key does not depend on paths), `/cache/heavy.lock` stays shared. D10
holds with one addition from T94 D8: a failed core with `experimental = true` is reported and does not fail the
stage. Everything below is the record of how it was decided and built.

- **D2 One list says which file belongs to which layer: `designs/c64_basic/layers.txt`** with sections `host:`, `core:c64`, `core:vc20`, `core:amiga` (git pathspecs, one per line). A file is in exactly one section; a file both layers need is a host file and the core sees it only through the contract (T79.1) or is duplicated. `ci/layers_check.sh` (stage `lint`) fails when a tracked file under `designs/c64_basic` or `cores/` is in none or in two sections, and when a section names a path that does not exist.
- **D3 Host key `H`** = `tree_key` over: the `host:` files; the shell, i.e. the port list of the empty layer 2 (the stub `layer2.v` ports, since the contract is part of the static); `constraints/c64u.xdc`; the host build script and the floorplan (pblocks `pb_l1`, `pb_l2`, `RESET_AFTER_RECONFIG`) once `build.tcl` is split into `host.tcl` (static: synth, floorplan, place, route, lock, write checkpoint and host image) and `core.tcl` (one partial); `tools/vivado_lib.tcl`, `tools/vendor_gideon.sh`, `tools/patches/gideon`; the Gideon VHDL files the host instantiates (a `host:` list inside `layers.txt`, not the whole `cores/gideon` as `tree_key` does today); the IP cache keys (`ip_mig_ddr2_*`, `ip_gt_hdmi_*` already have their own); `KEY_EXTRA` = part, `vivado-env:2024.1`, and the host feature flags (`TRX_FEATURES`, e.g. `jtag`). NOT in `H`: anything in a `core:` section, `core_ooc.xdc`, `core.tcl`, `sid=` / `slot=`, benches, `build/` directories, `host_id` itself (it is derived from `H`, passed to synthesis as a define).
- **D4 OOC key `O(core)`** = hash of that core's `core:` files, its Gideon VHDL list, `c64_core_ooc.xdc`, the OOC part of `core.tcl`, `sid=` / `slot=` / core feature flags, part, Vivado version. It does not contain `H`: the dcp is reused across hosts. It replaces the content hash of `ooc_c64_core_<hash>.dcp` and keeps that cache directory (35 entries, 146 MB today).
- **D5 Core key `C(core)`** = `H` + `O(core)` + `core.tcl` (the implement part) + the implementation options. The entry stores `host_sha`; a lookup hits only if the stored `host_sha` equals the host entry's (S7).
- **D6 Cache layout** (`$CI_BIT_CACHE`, today `/cache/bit`):
  - `host-<H>/`: `static_locked.dcp` (routed static, `l2` a black box, routing locked), `host.bit` (static + empty layer 2, T84's "host + null core"), `host.timing.rpt`, `host.utilization*.rpt`, `host.steps.txt`, `host.vivado.log.gz`, `manifest.json` (`H`, `host_id`, `host_sha`, `from` commit, WNS, tool versions, the list of files Vivado read), `DONE`, `pins/`.
  - `core-<core>-<C>/`: `<core>_partial.bit`, `<core>_full.bit` (host + this core as one chip image for JTAG tests), the core's timing/utilization/route-status reports, `steps.txt`, `manifest.json` (`H`, `O`, `host_sha`, partial SHA-256, WNS), `DONE`, `pins/`.
  - `ooc-<core>-<O>.dcp` (+ stub), as today.
  - `c64u-<key>/` stays the kind of the monolithic build (P6).
  Atomic writes as now (`cache_put`: copy to `.tmp`, `mv`, `DONE` last). Entries are never edited after `DONE`.
- **D7 `ddr2_test` and `hdmi_test` get their own key** (their files, `constraints`, tools, part, Vivado) and entry kind. Today they are rebuilt on every core change because they share `gateware_key`; they are not layered.
- **D8 Host stage** (`ci/bitstream.sh host`): on a miss, `heavy_lock`, `vivado -source host.tcl` writes `static_locked.dcp` as: route the full design once as today, then `update_design -cell l2 -black_box`, `lock_design -level routing`, `write_checkpoint static_locked.dcp`, and writes `host.bit` from that state (the grey-box image; `T79_GREYBOX`/`T79_HOST_ONLY` in `build.tcl` are the starting points). The static is routed WITH the first core in place so the core's pins and clock loads shape the routing (as in the trial); that core is the one named in `layers.txt` under `reference:`. WNS of the whole design (static + reference core) is the gate, as today: WNS < 0 fails and nothing is cached.
- **D9 Core stage** (`ci/bitstream.sh core <name>`): look up `C(core)`; on a miss: look up `O(core)` (miss: `synth_design -mode out_of_context`, cache it), then `vivado -source core.tcl` with `TRX_CORE=<name>`:
  ```
  open_checkpoint  $host/static_locked.dcp          ;# routed static, l2 black box, routing locked
  read_checkpoint  -cell l2 $ooc/<core>.dcp         ;# the core into the black box
  opt_design; place_design; phys_opt_design; route_design; phys_opt_design
  report_timing_summary / report_route_status / report_utilization -pblocks pb_l2
  pr_verify -initial $host/static_locked.dcp -additional <routed core>.dcp   ;# fails the stage
  write_bitstream -force -cell l2 <core>_partial.bit
  write_bitstream -force <core>_full.bit
  ```
  This is the second-configuration code of `t83-vc20` (`build.tcl`: `open_checkpoint routed.dcp`, `update_design -cell l2 -black_box`, `lock_design -level routing`, `read_checkpoint -cell l2`, `write_bitstream -cell l2`) with the starting checkpoint taken from the cache instead of from the same run. To check on the first implementation (stated as a build step, not assumed): that `write_checkpoint` of a locked, black-boxed design reopens with its lock intact, and that `pr_verify` accepts it against the later routed core.
- **D10 Cores are built by the pipeline in the order `layers.txt` lists them,** one after the other under the one pipeline job; a core whose files did not change is a cache hit in seconds. Which cores are required on a branch: all `core:` sections (cheap on a hit). A failed core fails the pipeline after the others have run (so one broken core does not hide the state of the rest).

**Identity (P4)**

- **D11 Host identity = three things, all derived from the cached entry, never from a branch name:**
  1. `host_id` = the 64 bits of `H` (16 hex), put into layer 1 as a read-only register (next to the C9 identity block of T79.1, which the core answers; this one the host answers) and into `USR_ACCESS` (upper 32 bits). `USR_ACCESS` today carries the build stamp T15 compares between the flash bitstream and the running one; as it then equals "same host" instead of "same build" the T15 check keeps its meaning for the app in flash (app flash content depends on the host, not on the build), and a re-run of the same host gives the same stamp. See Q3.
  2. `host_sha` = SHA-256 of `host.bit`. Frames outside `pb_l2` are bit-identical in `host.bit` and in every `<core>_full.bit` by construction (same locked static). `host_sha` is for the packer and the cache, not for the boot app (the boot app cannot hash the running bitstream).
  3. `pr_verify` in CI on every partial (D9).
- **D12 `.cor` manifest (T84 D5) carries `host_id`, `host_sha`, `core_key` `C`, `core_id`, `contract_version`, and the SHA-256 of the partial.** `tools/cor_pack.py` takes the partial and the host entry's `manifest.json` and refuses (non-zero, both ids in the message) when `C`'s `host_sha` is not the host entry's, or the partial's file hash differs from the cache manifest. The boot app compares `host_id` with the running host's register before starting the swap and lists a mismatch as "wrong host" (T84 §3, the same place as the contract-version check). Bundles for the same host can be packed from different runs, branches and days.

**Time and disk (P3, P5)**

- **D13 Expected time, labelled estimate:** for the VC-20 core-only change the Vivado part is about 5 minutes: core OOC synth about 2 minutes (113 s in the measured entry, the cached-OOC case skips it), checkpoint load 30 to 60 s (estimate, not measured), opt/place/route/phys_opt about 90 to 130 s (the 90 s second pass of the measured DFX build is the best number there is), partial plus full bitstream 35 s. The same build is about 10 minutes today (measured, 617 s of steps) and 20 to 40 minutes on the monolithic chip (Alex). A host hit with a core hit is seconds.
- **D14 Retention by pin, not by count.** Each pipeline that uses an entry writes `pins/<branch>` with the HEAD commit in it and removes `pins/<branch>` from the entry that branch used before (per kind). The janitor (`ci/cache_gc.sh`, run at the end of a pipeline under the lock, `--dry-run` for the test) deletes an entry only if: it has no pin of a branch that still exists (`git ls-remote`), it is not used by a `v*` tag or by `main`/`integ-m1` (those pins are permanent), and it is not among the newest history. History default: the 4 newest unpinned host entries, the 4 newest unpinned entries per core, anything used in the last 14 days (Q2). Deleting a host entry deletes the `core-*` entries recorded against it (their `host_sha`); `ooc-*` entries follow the same rule per core. Content keys only, so branches with the same host share a pin set.
- **D15 Disk budget** (builder volume 888 GB, 51 GB used, 793 GB not yet used by anything, shared with all else; the cache is 2.1 GB of that). Per layered entry, with the measured size of the pieces that exist today and an estimate for the one that does not: host entry = `static_locked.dcp` (estimate 10 to 20 MB for a routed 50T design; first host build measures it) + `host.bit` 1.7 MB + reports and log about 1 MB, say 15 to 25 MB; core entry = partial 1.2 to 1.4 MB + full 1.7 MB + reports, about 4 MB; OOC dcp 4.7 to 4.9 MB. Worst case on the retention rule: 12 hosts x 25 MB + 3 cores x 12 x 4 MB + the OOC files = about 0.5 GB. A hard cap of 3 GB for `bit/` + `ooc_*` is enforced by the janitor (oldest unpinned first; pinned entries are never dropped for the cap, the janitor fails loudly instead). Not part of this: `app/` 902 MB, `runs/` 675 MB (the hand-over directories of 50 runs are not cleaned today), `t27/` 213 MB; `runs/` cleanup is one `find -mtime` in the same janitor.

**Migration (P6)**

- **D16 Three steps, each leaves `integ-m1` and `main` building as today:**
  1. *Pipeline and retention first* (no gateware change): P1 workflow change (D1, after the probe), and the pin-based janitor for the existing `c64u-*` / `ue2-*` kinds. The key stays `gateware_key`, the cache layout stays; S9 is the proof.
  2. *The split on the DFX branches:* `layers.txt`, `host.tcl` / `core.tcl`, host and core stages, the `host_id` register and `USR_ACCESS`, `cor_pack.py` check. They exist on `dfx-trial` / `t83-vc20` / `t85-amiga` first; with no `layers.txt` in the tree, `ci/bitstream.sh` runs the old monolithic path (S9).
  3. *Switch-over on `integ-m1`* when T84's boot chain makes the host + `.cor` the shipped form. Until then the monolith is the product and the layered build is only on the DFX branches. Q4.
- The `package`/`update` stages read `manifest.json` per entry: the package lists `host_id`, `host_sha`, and per core `C` and the partial's SHA-256 next to the existing `gateware_key`.
- The 100T build (`bit-ue2`) stays monolithic (Out).

**Open questions for Alex** (one at a time, in this order):

1. Q1: pipeline as one job (O1: no stage boxes in Gitea, a new push's lint waits behind a running build) or keep the stage boxes with a ticket queue over seven jobs (O3: more machinery)?
   **Answered (Alex, 2026-10-12):** "the stages are the steps; we built them in on purpose so it does not become one big black box": the stages stay visible as separate units with their own status and log. O1 as "one job, no stage boxes" is rejected; the fix must keep the stages (O3 ticket queue, or one job whose steps Gitea shows separately, if that keeps each stage visible with its own result).
2. Q2: retention defaults OK: branch-HEAD pins, `main`/`integ-m1`/`v*` permanent, plus 4 newest unpinned per kind, plus 14 days, cap 3 GB?
   **Answered (Alex, 2026-10-12):** "we have 1 TB on the builder ... it can easily be 30 GB for the caches": the pins (branch HEADs, `main`, `integ-m1`, `v*`) stay; the hard cap is 30 GB instead of 3 GB, so history is kept generously (the 4-newest/14-days floor stays as the minimum).
3. Q3: `USR_ACCESS` becomes the host key instead of the build stamp (T15 keeps working because the app in flash then depends on the host, not on the build)?
   **Answered (Alex, 2026-10-12):** "b) contract version": the boot ROM checks the host contract version (as T84 D3), not the host key; `USR_ACCESS` carries what that check needs, and the host key (`host_sha`) is used for the cache and the `.cor` partial match only.
4. Q4: switch `integ-m1` to the layered build only when T84's `.cor` boot chain ships, or as soon as the split works on the DFX branches?
   **Answered (Alex, 2026-10-12):** "b": `integ-m1` switches to the layered build as soon as the split works on the DFX branches (S9 green there), not only when T84 ships; until then its monolithic build stays unchanged.

## §4 Out

- The 100T (XC7A100T) build is not layered; it stays the optional monolithic stage.
- Caching Vivado's incremental checkpoints (`read_checkpoint -incremental`) for the monolith: separate idea, not needed once the host is cached.
- Running cores in parallel on the 16-core builder: cores are built one after the other (the host is built once, cores are minutes); the two-simulation / three-core limits of the machines apply anyway.
- Cleaning `app/`, `t27/` and the IP caches: only `runs/` housekeeping is added (D15).
- Remote cache sharing across machines: the NAS and the builder see `/cache` through NFS in places (`ci/lib.sh` comment on `heavy_lock`); whether they share one `bit/` directory is checked when the layout is built, not decided here.
- Signing partials: the T84 decision (hash only) stands.

## §5 Decisions taken while building

- **D15 Reference core = the TRX64 (C64) core (Alex, 2026-10-13):** "we build the host with the TRX64 core, as it is
  defined now, because that way it builds, and all other cores must build against THIS one host" (translated). One host
  per host key, routed together with the C64; the C64's partial comes from that host run. VC-20, Amiga and every later
  core are implemented against that locked static; a core that does not route against it must slim down (or the
  reference question comes back to Alex). Evidence the same day (builder scratch `~/scratch/exp-fixedhost`): the C64 does
  not route against its own locked static (E1, conflicts on `ain/c64_keys`, `ult/i_wifi`) but does with the 6,057
  static nets that run through `pb_l2` freed (E2, WNS +0.114 ns, same partition pins); `CONTAIN_ROUTING` on `pb_l1`
  does not converge (12,000-30,000 overlapping nodes after 4 router iterations); a compact host block does not fit the
  three block RAM columns. A host that is never co-routed with a core is therefore not possible on the 50T today.
- **D16 Which C64 is the reference (Alex, 2026-10-13):** "the slim variant for the multi-core system, yes. For our 1:1
  drop-in replacement integ-m1 *stays*" (translated). The multi-core host is routed with the slim C64 of the T87 CI
  (`TRX_FEATURES=jtag`, `TRX_SID=redip2`: no REU, sampler, UCI, turbo, streams; 2 SIDs). integ-m1 keeps the full C64 as
  the drop-in replacement; whether Q4's switch of integ-m1 to the layered build still applies is open.

---
<!--
Closing: Status → DONE (date) + which scenarios are green where; TODO row ✅ and history entry,
in the same commit as the code.
-->
