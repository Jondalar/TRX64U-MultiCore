# T174 — The CI spends its hours on what changed: bitstream-input keys, one test cache, early red, and the NAS runner doing the cheap jobs

**Status:** DONE (2026-11-14, Alex: "Close it. It is much better now. After T174.", translated). Closed: the CI is much better since T174. Earlier status: PROPOSED (2026-11-01). Status check 2026-11-13: BUILT and merged: D1-D5 (baf37fb2f 11-08; T192 controls out 11-09); §2 proof rows not checked.
**Repo:** TRX64U-MultiCore — CI only (`.gitea/workflows/`, `ci/`, the key helpers in `ci/lib.sh`, `host/layers.txt`, the runner configs on the builder and the NAS). No gateware, no app code.
**Number:** T174 (registry: the table in `TODO.md`).
**Origin:** Alex, 2026-11-01, after runs 1133/1134 rebuilt the host for a change that did not touch the bitstream: "write a spec how the CI can become more efficient ... important to me: include the NAS runner too." (translated)
**Depends on:** T100 (lanes, D9 build by change, D11 heavy slots, D13/D14 test cache, D16 test once), T94 D8/D9, T171/T172 (package, bundle). Reads the newest pipeline, branch `t164` (`git show t164:.gitea/workflows/pipeline.yml`, `ci/lib.sh`, `ci/lanes.py`, `ci/plan.py`).
**Milestone:** M3.

---

## §1 The problem

Written for the reader who opens this in three months. Everything below was read from the run logs and the two machines on 2026-11-01 (evening); a number marked "estimate" is arithmetic on those, not a measurement.

**Who:** every session that pushes (agents and the coordinator), and Alex waiting for a run to tell him whether a host or core bitstream is good. **What they are trying to do:** push a change and get a trustworthy green or red as soon as the change can be judged, without the builder being busy with work whose answer is already known. **What happens today:** the 49 runs of the last 2.9 days (runs 1050-1134, `tea actions runs list`) took 30.0 h of pipeline wall time (runs go one at a time, so the machine was in a pipeline 42 % of the clock); 24 of them were red and make up 1077 of the 1801 minutes (60 %); the host and the cores were rebuilt for non-bitstream edits; the app test stage ran twice in effect; a failure that Vivado shows after 16 s was reported 24 minutes after the push and the run said red 68 minutes later.

### The evidence

**E1 — runs 1119-1133, per stage** (`tea actions runs view --jobs`; the lane rows are the wrapper jobs of `pipeline.yml`, so a large-core time **includes** the wait for a heavy slot and for a runner slot; the time column of a stage is its job duration):

| run | branch | result | wall | queue+plan | lint | trxapps | host lane | DFX lanes (Σ of 6) | large lanes (Σ of 10) | longest large |
|---|---|---|---|---|---|---|---|---|---|---|
| 1119 | t164 | red | 1h38m | 18 s | 4m | 38m | 91m (placement failed) | 1m (cache) | 96m | 36m |
| 1121 | t164 | red | 52m | 24 s | 3m | 1m | 32m | 1m | 80m | 31m |
| 1122 | t164 | red | 1h08m | 18 s | 3m | 1m | 12m | 39m | 124m | 17m |
| 1125 | main | green | 24m | 19 s | 4m | 17m | 8 s | 1m | 2m | 10 s |
| 1126 | t164 | red | 1h46m | 11m | 3m | 18m | 27m | 44m | 166m | 26m |
| 1128 | t164 | green | 1h15m | 23 s | 3m | 1m | 18m | 43m | 130m | 17m |
| 1133 | t164 | red | 1h31m | 21 s | 3m | 39m | 3m (failed) | 1m (cache) | 190m | 32m |
| 1134 | t164 | running | - | 22 s | 5m | miss | host miss again | - | - | - |

Other facts from the same data: `queue+plan` is 1.1 min on average but 11 min (run 1126) and 28 min (another run) when an older run was still going; `lint` is 2-5 min on every run; a run that changes nothing in gateware or apps is 8-11 min (runs 1107, 1109), one that touches the apps is 17-26 min before a single lane matters.

**E2 — host key over scripts (run 1133, job 11975).** Between the last green run 1128 and run 1133 the only changes under `host/` were `host/build.tcl`, `host/core.tcl` and `host/trx_constraints.tcl` (the T164 fail-fast constraint check; the other `host/` changes were in `host/sim`, which the key leaves out). `ci/lib.sh` `host_key` hashes every tracked file of the `host:` section of `host/layers.txt`, which is `host` minus `sim`, `formal`, `gideon_vhdl.tcl` and `layers.txt`; so the check script moved the key: `host cache miss 1467397ee84ffc2a: building the static` (log 18:19:36). The new check then stopped the build after 3m37s (`RED target host (bit-host): no routed.dcp`, 18:23:17). Run 1134 pushed a comment change in the same file and misses again. Had run 1133 not stopped, the cost of the false miss is read from run 1128, the last full rebuild: host 18m, then every DFX core against the new static (`core_key` contains the host key): null 6, vc20 9, atari800 9, apple2 6, ddt_mandel 9, ddt_novic 4 = 43 min, i.e. **61 Vivado-slot minutes** and a fresh package for a bitstream whose inputs had not changed. In the 8 host builds between run 1117 and 1134, 2 were of this kind.

**E3 — the tests run again and the render benches are never served from the cache.** Run 1133 trxapps (job 11974, 39m23s): RISC-V build 2m43, `application host tests took 948s` (coreapp `ran 1238 of 1293 units`, a main merge had changed 52 coreapp files), `render benches took 1208s` with `units (dyn): ran 90 (tests 90, controls 0), from the cache 0`. Run 1125 (a merge that touched no app code): hostapp `ran 38, from the cache 104`, coreapp `ran 10, from the cache 1187` (together 222 s), then `render benches took 598s` with `ran 84, from the cache 0`. So the D14 by-change cache works for the host-test suites and does not for the host-app render bench: 10-20 min per trxapps run, about half of the stage. Over the 44 runs that have a trxapps job, the stage took 665 min (avg 15 min); 24 of them took 17 min or more (Σ about 540 min). D16 (test once) is decided, but today an agent that follows it runs in its own scratch cache ("from the cache 0" in the agents' reports), so CI still runs everything on the push.

**E4 — the heavy slot is held by a job that is not heavy (run 1133).** `heavy lock held: slot 0 of 2` at 18:19:36 by trxapps, released at 18:58:59 when the script exited (`ci/trxapps.sh` calls `heavy_lock` in the build branch; `lib.sh heavy_lock` keeps the fd to the end). The Vivado part of trxapps is the RISC-V build, 2m43; the other 36 min are tests and benches (T100 D14 "Heavy slot during trxapps" proposed releasing it, not done). Meanwhile `amiga1200` logged `waiting for a free heavy slot` from 18:23:22 to 18:39:48 (16m26s) and `amiga` 3m47s, with the second slot busy with `amiga`.

**E5 — runner capacity 3 is spent on waiting jobs, small jobs queue behind them.** The builder runner has `capacity: 3` (`/home/alex/act-runner/data/config.yaml`). In run 1133 the three slots were trxapps, `amiga`, `amiga1200` (waiting on `flock`) from 18:19 to 18:39; the 18 other lane jobs queued. `c128` (job 11978) reached the runner at 18:39:13, took the heavy slot at 18:39:18 and failed at 18:39:34 (`Synth 8-11365 ... named port connection 'ce' does not exist` at `vendor/icepi/hdmi/hdmi.v:5`, instantiated from `cores/c128/mono/host/tmds_rep.v`): 16 s of work after about 20 min in the queue. The six DFX lanes of that run (7-8 s each, all cache) started at 19:28, 65 min after the host lane had ended. The run's verdict (`verdict`, 8 s) came at 19:47, 91 min after the push.

**E6 — the builder is overloaded by more than CI.** `ssh builder` at 21:35: 16 threads (Ryzen 9 6900HX), load average 30.9 / 27.3 / 25.8, 27.8 GB RAM of which 1.7 GB available, swap 18.8 GB of 28.4 GB used. Running at that moment: a Vivado host build (the run-1134 job), `cc1plus` of an app control, two Verilator `tb_l2` benches, `Vtb_zx` benches, containers `wp8b2-chk`, `wp8b2-31597`, `d10p-run4` (agent sessions). The runner config sets `CI_SIMS_JOBS: 16` and `TRX_VIVADO_THREADS: 8`, and the heavy slots count Vivado runs only; no slot, no cgroup limit or `nice` covers an agent's bench. Same-lane times differ between runs for no change in the lane: `amiga` took 9 min in run 1128 and 19m37s in run 1133, of which 3m47s waited, and the Vivado part of that one is synth 3m07 + route 3m44 (log of job 11976). That the load is the cause is a hypothesis; nothing records load in the stage logs yet.

**E7 — what the NAS runner is and what it has.** `ssh admin@192.0.2.2`: Intel Celeron N5095, 4 cores at 2.0 GHz, 39.9 GB RAM, of which 31.9 GB are used and about 8 GB are available (the Gitea container alone holds 18.4 GB, `docker stats`), swap 1.6 GB used; load 2-3 from the other 12 containers (Plex helpers, the editors, AdGuard, Fizzy, two runners); 307 GB free on the 3.4 TB volume (91 % used). Runner `nas-runner-2`: label `nas`, **`capacity: 1`**, `CI_SIMS_JOBS: 3`; a second runner `trx64-runner` (capacity 1; its labels and repository were not read) is there too. It has the images `trx64-sim-env:4`, `u64-build-env:ci` and `vivado-env:2024.1`. Today it runs only `queue + plan` (21 s on a quiet queue; the same job is the one that waits for older runs, so it holds the only slot for as long as an older pipeline runs) and the tier-3 benches (`sims-long.yml`). Its CI cache is **not** the builder's: `/srv/trx64-build/cache` on the NAS holds 498 MB of old entries, the builder's own directory of the same path is on its local NVMe (754 GB free) and holds `multicore/apptests-ok` (21,601 control entries), `bit/` and the OOC checkpoints. The comment in `ci/lib.sh heavy_lock` that the builder reaches `/cache` over NFS is no longer true; the two machines share no cache and no `flock`.

**E8 — the cheap jobs are on the builder, the lint is on the critical path.** `lint.yml`, `trxapps.yml`, `package.yml`, `update.yml`, `verdict.yml` and every lane use `runs-on: ${{ vars.CI_RUNNER || 'builder' }}`; only `plan` and the tier-3 sims name `nas`. Every lane `needs: lint`, so the 2-5 min of lint (133 min over 48 runs) sit in front of every Vivado start, on the machine of E6.

### Parts (MECE)

**E9 — agents and the coordinator wrote to the wrong cache (2026-11-01 night).** The CI mounts `/srv/trx64-build/cache`
of the builder as `/cache` (`docker inspect`, run 1133 Bitstreams: `Binds: /srv/trx64-build/cache:/cache`). The briefs
of the agents and CLAUDE.md (T100 D16) said `-v /cache/multicore:/cache/multicore`, which is the builder host's own, different `/cache`
(it holds only `multicore/apptests-ok`). So every agent and coordinator test run stayed outside CI's cache ("from the cache 0") and CI ran
it again. Fixed in the brief and in CLAUDE.md (main b6764cb49) the same night; R1 builds on it.

**E10 — red runs one stale control at a time (runs 1133-1136).** A branch was merged while its report said a render control had not been
seen; each run then found the next stale control (`wp17_place_rounded_up`: "the sed changed nothing"; `cvfix`: "no mode check went red,
or something else did"), one full run each, plus a fail-fast print that read its own output (run 1134) and a runner restart at 21:40.
Alex: "this is not bearable for me as a person who cannot act; I feel completely at the mercy of it" (translated). The rule since then:
merge only with a finished PASS line for every touched test and control.

- **P1** A cache key moves for an edit that cannot change what it caches (E2).
- **P2** Tests and benches are run again, or never served from a cache, so the push stage is long (E3).
- **P3** Heavy slots and runner capacity are spent on waiting or on non-heavy work (E4, E5).
- **P4** A red that is known early is shown late (E5, E1: verdict at the end).
- **P5** The builder is oversubscribed by work outside the pipeline's accounting (E6).
- **P6** The NAS runner's capacity is unused or unusable: one slot, a slow CPU, a separate cache (E7, E8).
- **P8** Wrong cache path for agent runs (E9); merges with unfinished tests (E10).
- **P7** Run volume and red runs: 60 % of the pipeline wall is red runs, a long red run re-does stages that were already green (E1, E5).
- **P9** One push, one verdict, many features (2026-11-02, run 1142 / #297, titled after its head commit "Merge t164-zx48e: ZX 48K ..."): the ZX
  48K fix built green, the C128 large core missed timing (WNS -0.095 ns in an unchanged host-copy path, `hout/mtim/row_reg -> vtot_m1_reg`),
  and the whole run is red. Alex: "but that was the zx48 run, why does the C128 make it red? ... The spec for the pipeline optimisation
  will be NO piece of cake" (translated). The run's title names one change, the verdict covers all; a green feature is not shown green.
- **P10** The nightly tier-2 run overloads the builder (night 1144, 2026-11-03): 27 benches ran into their limit at exactly the limit, 15 at
  once; red for the run although the benches pass alone (host_hdmi_timing / amiga1200_hdmi_timing 774-1037 s alone, 1500 s timeout at night;
  pixscale 1633 s alone, 4500 s at night). Limits (s): zxnext_keys 2400, zxnext_freeze 2400, zxnext_audio 3000, zxnext_boardid 2400,
  zxnext_video 3800, zxnext_noimg 4800, zxnext_mem_a/b 5400, atari_machine 5400, cpc_media 3600, cpc_pause 1800, cpc_cpu 1800, hdmigeom 1500,
  ovl_geom 3000, still60 2400, pixlayer 2400, slot 1800, statebus 1800, apple2_pause 2400, rvtrap_ctl 1200, rvtrap_dc 1800, flash_gate 900,
  flash_boot 900, dbgsess 1200, ultsys 900, input 1200, hdmi_out 9000. Also: two benches printed their controls' FAIL lines without `ctl| `
  and turned red with every check passing (snes_brr, pixscale; fixed in ci-nightly-1103).

## §2 Scenarios — what proves it is solved

Each is run by a bench in `ci/` (fast, no Vivado) or read from the next real runs; every control is a change that must make the scenario go red. Measurements are taken on the builder with the real keys and the real cache.

```gherkin
Scenario: S1 a check/report script does not move the host key
  Given the host static is cached under key K
  When  a file that only checks or reports (no design command) changes under host/
  Then  host_key prints K again, the plan names no host build, the run takes the cache path (run <= 12 min with a green cache)
  And   nothing was rebuilt: no "host cache miss" line, no DFX lane built, package sha256 of the .bit unchanged

Scenario: S1n the key still moves for everything that reaches the bitstream
  When  one line of an RTL file, a .xdc, a flow Tcl (place/route directive), an IP .prj, the floorplan, a bootrom file, or the Vivado image tag changes
  Then  host_key prints a different value
```

```gherkin
Scenario: S2 an agent's test run is CI's test run
  Given an agent ran ci/apptests.sh and the render benches of its branch against the shared cache
  When  CI's trxapps runs the same tree
  Then  every unit is served ("units (dyn): ran 0"), trxapps takes <= 6 min (RISC-V build + lookups), and no heavy slot is held for it
  And   a unit whose inputs changed after the agent's run is run again (no stale PASS)

Scenario: S3 the render bench is cached per unit
  Given nothing under the bench's declared reads changed since the last green run
  When  tests/*/render runs
  Then  "ran 0, from the cache N" (today: ran 84-90, from the cache 0), stage <= 1 min
```

```gherkin
Scenario: S4 a job that is not heavy holds no heavy slot, a waiting job holds no runner slot
  Given two heavy slots are taken by Vivado runs and trxapps is running its tests
  When  a lane that needs only the cache starts
  Then  it is not behind the waiting Vivado lanes: it starts within 1 min and ends green in <= 30 s (today: 65 min late, run 1133)

Scenario: S5 an error that the tools show in seconds is reported in seconds
  Given a core whose top has a named-port mismatch (the run-1133 c128 hdmi case)
  When  it is pushed
  Then  the lint stage (or its first job) is red within 3 min naming the core and the port, no heavy slot was taken, and the run summary/commit status says red before any Vivado stage ends

Scenario: S6 a red target is announced when it happens
  Given a lane is red at minute 5 of a 90-minute run
  When  it ends
  Then  a commit status / run summary line "RED <target>: <reason>" exists at that minute (today: only the verdict job, minute 91)
  And   the remaining lanes still finish (D9.6 kept)
```

```gherkin
Scenario: S7 the builder's load is recorded and bounded
  When  any Vivado stage runs
  Then  its log carries load average, MemAvailable and swap at start and end, and agent benches run inside a CPU/memory-limited cgroup or a bench slot
  And   the load average during a Vivado run stays <= 16 and swap-in/out ~0 (today 31 and 18.8 GB swap)

Scenario: S8 the NAS runner takes lint and verdict and is not worse
  Given lint runs on the NAS
  When  a push arrives
  Then  lint wall <= 6 min on the NAS (measure; builder 2-5 min), lanes do not wait for it, package does, and the NAS RAM available stays >= 4 GB and load <= 3.0 on 4 cores (<= 3 cores used, Alex's rule)
  And   a red lint still stops the package and the update, and shows in the verdict

Scenario: S9 the NAS does not get a Vivado run
  Given a 50T host build is started on the NAS as a measurement (nice, 1 thread-limited)
  Then  its wall time and peak RSS are recorded; the proposal "no Vivado on the NAS" stands when peak RSS > 4 GB or wall > 2x builder or Gitea's available memory drops under 4 GB
```

| # | Solves | Scenario | Test (bench / step) | Where it runs |
|---|---|---|---|---|
| S1 | P1 | script edit keeps the host key | new `ci/test_keys.sh`: temp clone, edit a check file, compare `host_key`; control: edit an RTL line, key must change | `ci/lint.sh` + one real run |
| S1n | P1 | everything on the input list moves the key | same bench, one case per input class (RTL, xdc, flow tcl, prj, floorplan, bootrom, image tag, TRX_* option) | `ci/lint.sh` |
| S2 | P2 | agent run = CI run | agent runs `ci/apptests.sh` + render in the shared cache; CI log line `units (dyn): ran 0` and `trxapps done in <= 6m`; control: touch a file a unit reads, that unit (and only it) runs | next 3 pushes with app changes |
| S3 | P2 | render bench by unit | `tests/ctlcache_selftest.sh` scenario with a render-like unit + a live run on the builder; control: change a golden, that unit runs | `ci/lint.sh` + builder |
| S4 | P3 | no slot hoarding | `ci/test_heavy_slots.sh` extended (trxapps releases after the build); a dry-run with 3 fake lanes and capacity 3; control: keep the hold, the bench is red | `ci/lint.sh`, then a real run |
| S5 | P4 | fail in seconds | `ci/lint.sh` step "elaborate every lane top" (Verilator `--lint-only` or Vivado `synth_design -rtl`); control: the c128 hdmi port mismatch re-created in a temp tree must go red | lint |
| S6 | P4 | red announced at once | final step of each lane writes a commit status / step summary; read via `tea`; control: a lane forced red (`TRX_FORCE_RED=1` in a dispatch) shows the line while others run | one dispatch |
| S7 | P5 | load recorded and bounded | stage logs show the three numbers; `uptime` sampled every 60 s by the stage; compare 10 runs before/after | next runs |
| S8 | P6 | lint on the NAS | dispatch with lint on `nas`; `time`, `free -m`, `uptime` in the log; control: cap the container to 1 core, wall must exceed the budget and be reported | 3 dispatches |
| S9 | P6 | NAS Vivado measurement | one dispatch/ssh-run of `ci/bitstream.sh` host on the NAS in a throwaway container, 3 cores, peak RSS from `docker stats`; not part of the pipeline | once, by hand |
| S10 | P7 | red runs cost less | sum of wall of red runs / all (today 60 %), over the 20 runs after the change | read from `tea` |

Rules kept: the negative of every scenario is named and must fail without the change. S9 is a measurement, not a build; it is the only scenario that touches the NAS with a heavy job and it needs Alex's go (it can push the NAS to its 8 GB reserve).

## §3 Proposals (nothing decided; ranked by estimated saving per week)

Basis of the estimates: 49 runs in 2.9 days = about 17 runs/day = about 118 per week; 30.0 h pipeline wall per 2.9 days = about 72 h per week of wall; "slot-hours" are Vivado-slot time (two slots). All weekly figures are **estimates**, scaled from that window, which was a busy T164 period.

**R1 — One test cache for agents and CI, the render bench cached, the heavy slot released after the RISC-V build** (P2, P3). Saving: the render benches are about half of every long trxapps run: 24 runs x about 10-20 min, about 270 min per 2.9 days = **about 10-11 h of runner time per week**, and the slot-0 hold of 36 min per miss (E4) goes away, which is the 16-min wait of `amiga1200` in run 1133. Wall of a run with an app change: -10 to -20 min. Needs: (a) find why the host-app render units get no hit (the key contains the unit's reads and LVGL's pin; 84-90 units run in both runs 1125 and 1133, which had different app trees) - a log line per unit with the key and the reason of the miss; (b) the agents' containers mount the builder's `/cache/multicore/apptests-ok` read-write (they run on the builder already: `d10p-run4` is `u64-build-env:ci`), so D16 means what it says; (c) `exec 9>&-; unset HEAVY_SLOT` after the artefacts are copied (T100 D14 open item 2). Risk: low to medium. A shared cache needs the stale-PASS guard that D14 already has (manifest of reads, verified on a hit); a writer that is killed in the middle must not leave a half entry (`cache_put` is atomic). Open: macOS keeps the coarse key (D14 open 6), so a Mac run does not fill the cache, only the builder does.

**R2 — Keys over what goes into the bitstream, not over the directory** (P1). Saving: a false host miss costs about 61 slot-minutes (E2) plus a new package; 2 in the last 8 host builds; weekly estimate **2-5 slot-hours and 1-3 h of wall**, and it removes the trap that every check or report edit in `host/` rebuilds everything (including the 91-min run when placement is on the edge). Shape: (a) the `host:` section becomes an input list instead of "all of `host/` minus"; check, report and lint helpers live in `host/check/` (excluded), and a lint step fails when a file under `host/check/` calls a design-changing command (`set_property`, `create_pblock`, `add_cells_to_pblock`, `read_*`, `set_*_delay`, ...) so the exclusion cannot hide a real input; (b) `trx_constraints.tcl` is split into constraints (key) and the check (not in the key); (c) the same for `ci/lib.sh` `shared:` (`tools/vivado_lib.tcl` carries report helpers as well as flow); (d) the key prints, on a miss, **which files changed against the cached entry's file list** (store the `ls-files -s` list next to the entry), so a miss is explained in the log (today the log gives only the hash). A second line of defence is optional and open: a key over the post-synthesis netlist + constraints + knobs, so a comment or a pure-refactor edit also reuses place and route; whether `synth_design` output is reproducible enough is unmeasured. Risk: medium. A too-narrow list silently serves a stale bitstream, the worst failure of a cache; the control S1n and a nightly "rebuild and compare sha" on one core guard it.

**R3 — Fail early, report at once** (P4, P7). Saving: feedback latency, not slot-hours: the c128 failure was visible at minute about 6 (lint) instead of minute 24 (job start) and 91 (verdict); 24 of 49 runs red means this matters every second run. Shape: (a) `ci/lint.sh` elaborates every lane's top (Verilator `--lint-only -Wno-fatal` filtered to errors such as missing ports, unknown modules, or Vivado `synth_design -rtl -mode out_of_context` where Verilator cannot read the VHDL) in seconds per core, in parallel, on a cheap runner (R6); (b) each lane writes a commit status / summary line at its first red, through the Gitea API with the job token (the runs already clone with `GITEA_TOKEN`), while D9.6 `defer_red` keeps the other lanes going; (c) the verdict stays as the single place that turns the run red. Risk: low; a lint false positive costs one lane's attention, so it starts as "warn with the reason" and becomes a gate only for the error classes that have been seen to be real.

**R4 — The builder's accounting covers everything that runs on it** (P5). Saving: unknown until measured; run 1128 to 1133 shows 1.75x on the same Vivado lane (9 min vs 15.8 min net of the wait), consistent with the load of E6 but not proven. Shape: first only measurement (S7: `uptime`, `MemAvailable`, swap in the stage log at start and end); then, if the correlation holds, (a) agent benches and sims run with `--cpus` and `--memory` limits or under a "bench slot" (like `heavy_lock`, a second lock family with N=1-2), (b) `CI_SIMS_JOBS` default below the core count, (c) the runner's `capacity` stays 3 or goes up only together with a RAM guard (swap 18.8 GB used today means the next Vivado may be OOM-killed). Risk: low for measuring, medium for limits (agents' sims get slower; that is the trade).

**R5 — The NAS takes the cheap jobs, the builder keeps Vivado and the test compile** (P6). What fits, with E7's numbers:

| job | today | on the NAS | verdict |
|---|---|---|---|
| `plan` + queue | NAS | NAS | stays; raise the runner to 2 slots or move the wait to a lighter step (it holds the only slot while an older run goes) |
| `lint` | builder, 2-5 min, in front of every lane | image `trx64-sim-env:4` is there; python/shellcheck/selftests, no cache needed except `ctlcache_selftest` (35-55 s) | **yes, if lanes stop waiting for it** (they `need` plan only; package and update `need` lint); expect 1.5-3x slower than 2-5 min on 4 Celeron cores (S8 measures it) |
| `verdict`, `update`-decide steps | builder, seconds | needs only the API | yes |
| `package` (1 min), `update` (1-4 min) | builder | needs the lane outputs in the **builder's** cache | no, unless the cache is shared (below) |
| `trxapps` (RISC-V build + tests) | builder, 15 min avg | would run on 3 cores of a CPU with about a third of the single-thread speed of the builder; the by-change cache lives on the builder | no; with R1 it is mostly lookups anyway |
| tier-3 benches, nightly tier 2 | tier 3 on NAS; tier 2 on builder at 01:00 (avg 56 min, 2 runs in the window) | `CI_SIMS_JOBS=3` | tier 3 already; tier 2 could follow when the builder is the bottleneck (it is at 01:00 only when a branch run is going) - candidate |
| Vivado (host, DFX, large) | builder | 4 cores at 2.0 GHz, about 8 GB available, 18.4 GB held by Gitea | **no**: a 50T static is quoted at 4-6 GB (T100 D11) and an OOM kill on the NAS can take Gitea with it; the time would be a multiple of 12-32 min. S9 measures instead of guessing |

A shared cache is the precondition for anything beyond lint: today a job moved to the NAS misses everything. Options, none chosen: (i) NAS-only jobs are written cache-free (lint, verdict, plan); (ii) the builder exports `/cache/multicore/apptests-ok` and `bit/` over NFS or rsyncs it to the NAS after each run (small files: 21,601 entries; the `.dcp` files are hundreds of MB); (iii) a content-addressed HTTP store (key -> blob) both machines use; (ii) and (iii) add a network hop to every Vivado stage and a failure mode (a stale mount reads as a miss and rebuilds). Saving: lint off the builder and off the critical path = **2-5 min per run on the critical chain, about 4-8 h of chain time per week**, plus the builder load of lint (133 min per 48 runs, about 5-6 h/week). Risk: medium; the NAS is the machine that hosts Gitea, so every NAS job gets a cgroup limit (`--cpus 3 --memory 4g`) and a MemAvailable guard that refuses to start under 4 GB.

**R6 — Lane scheduling: the slot wait no longer eats runner capacity** (P3). The builder's `capacity: 3` is the number of *jobs*, the heavy slots are the number of *Vivado runs*; waiting jobs cost almost nothing, so raising capacity (e.g. 8) lets the cache-only lanes (7-8 s each) start behind a waiting large lane and ends E5's 65-min delay. This does not shorten the run when the large lanes dominate (they do: Σ 130-190 min over two slots, 65-95 min minimum), because the package waits for them anyway; the gain is the early information (which lanes are green) and that the DFX lanes that must build get a slot as soon as one is free instead of after the larges. Order: start the lanes by expected duration (longest first, from the last run's per-lane times kept in `/cache/multicore/lane_times`); with the run-1128 durations (8-17 min) the gain over alphabetical order is at most a few minutes (2 slots: 130 min of work, lower bound 65 min, run took 75). Estimated saving: **0-10 min of wall per run, no slot-hours**. Risk: low; capacity above 3 needs the memory guard of R4 first, because a runner that accepts more jobs also starts more `trxapps`/`lint` containers.

**R7 — Skip what is known** (P7, partly done). Unchanged DFX and large lanes already end in 7-10 s from the cache (E1 runs 1107, 1109: 8-11 min total); the remaining waste is the red run that rebuilds lanes that were green in the previous run of the same branch with the same key (they are cache hits, so no), and the full `trxapps` after a merge of main (R1). Not a separate item beyond R1-R3; listed so the reader does not look for it. Nightly benches only on `main` and no benches in push runs are already the rule (CLAUDE.md, T100 D7).

Ranking by estimated weekly saving: R1 (about 10-11 runner-hours + the slot hold), R2 (2-5 slot-hours, 1-3 h wall, and no false rebuild cascade), R5 (4-8 h of chain time, lint load off the builder), R3 (latency; hours of waiting for a known red), R6 (0-10 min per run), R4 (unknown until measured, possibly the largest if the 1.75x is load).

## §4 Out (candidates; each to decide with Alex, none is decided)

- Vivado on the NAS (host, DFX or large): not recommended by E7 (RAM 8 GB available, Gitea 18.4 GB resident, 4 slow cores); to decide after S9, if at all.
- A second builder machine, or moving Gitea off the NAS so the NAS can build: hardware; to decide.
- Changing the number of heavy slots (2) or Vivado threads (8): to decide after R4's measurements.
- Cancelling superseded runs (a newer push to the same branch): `pipeline.yml` deliberately has no `concurrency` group because a group cancelled runs in the probe (T87 D1); to decide whether a safe form exists.
- Splitting the host lane into synth / place / route stages with their own checkpoints; routing against the cached static is already done for DFX.
- Merging the 18 lane jobs into fewer jobs (each lane is a Gitea job with its own clone, 7-10 s when cached; about 3 min of runner time per run).
- Moving `package` and `update` to the NAS (needs the shared cache of R5).
- Dropping `trxapps` from push runs of branches that touch no app code (the plan can tell; the cost is a stage that sometimes is not run).
- Making `lint` a gate for the lanes again, or reducing it (its checks are the pre-push set, `tools/prepush.sh`).
- A cache for the Vivado image layers / `ccache` for host-app builds across the two machines.

## §5 Decisions taken while building

- **D1 (Alex, 2026-11-07, translated, after a day lost to test cycles: "Why does it take so long? CI and the endless testing? ...
  write 20 classes, 100000 lines of code, then build, then test, then RISC-V"; then "make it all modular, segmented, good and sensible
  caches and detection of when which tests are needed"):** the dev loop is: write a large change, build once (incremental, only what
  changed), run the fast tests that the change touches, then the RISC-V images; the long checks (negative controls, render benches,
  emulator runs, Vivado) run at integration or nightly, never per slice. Concretely:
  - build and test are separate stages; `ci/trxapps.sh` builds images only, render benches are their own keyed stage;
  - incremental: persistent build trees per branch on the builder (make decides what to rebuild), ccache kept;
  - test selection by content: each suite/unit knows the files it reads (existing keys, tests/ctlcache.sh); only touched ones run;
  - tiers: `fast` (unit tests of touched units), `integration` (all tests + negative controls + render + emulator), `nightly` (all);
  - one cache for agents and CI (R1b), heavy slot released after the RISC-V build (R1c), the host key over explicit inputs (R2),
    early red (R3), cheap jobs on the NAS (R5, capacity 2) - the coordinator answers §6 Q1-Q9 "yes" under D1, except Q7 (Vivado trial on
    the NAS: not now) and Q9 (builder capacity stays 3 app slots, Vivado alone).
  - one entry point for people and agents: `tools/dev.sh build|test|riscv|integrate [paths]`.

- **D2 (Alex, 2026-11-07, translated - the principles):**
  1. Only real code triggers anything (good detection and caching).
  2. UI / app is first built and tested with the emulator (ue2emu, native build).
  3. Gateware is tested in the simulation.
  4. Gateware that passed the simulation is built in Vivado.
  5. UI / app for RISC-V.
  6. Benches only when something fundamental was touched, otherwise at night.
  7. Single .cor files must be packable.
  8. The complete release package too.
  Items 1-7 apply dynamically on feature branches; 6-8 when it goes to main.
  Addendum (Alex, same day, translated: "on a feature branch the whole thing runs too, only for the one core and the one app;
  host + ref core + null core"): a feature-branch run builds and tests only the core(s) and app(s) the branch touches, plus host,
  reference core (c64ref) and null core; every other core comes from the cache / main.

- **D3 (Alex, 2026-11-07, translated: "Keep modularising, otherwise that is a bottleneck again"):** the coarse units left after D1/D2 get split.
  Coordinator decisions under D3:
  1. Core app tests per core: `tests/coreapp` becomes one keyed unit per core (`cores/<id>` + the shared core app sources it compiles) plus the
     shared part; a change in one core's files runs that core's tests only. The same for the render and emulator scenarios that are per core.
  2. Harness changes do not invalidate everything: a unit's key carries the harness by an explicit key version (`CC_KEY_VERSION` in
     tests/ctlcache.sh, raised when the keying or the verdict changes) instead of the bytes of every harness file; `ci/lint.sh` refuses a change
     to the key logic without a raised version. A suite's own `run.sh` stays in its own key.
  3. Version stamp: hash/version live in one generated source compiled alone (`gitinfo.c`); a new commit recompiles that file and links.
  4. The emulator kit also on the NAS cache, so the emulator suites run on both runners.
  5. `unzip` gets a key.

- **D4 (Alex, 2026-11-08, translated: "I want B, I want it done right, and separation of concerns in the CI too"; before that: "#1193 looks
  basically like before ... you had about 8-9 steps from me, like child pipelines, and they must be orchestrated dynamically per branch, commit
  content and stage; I only see everything green and then red and do not understand why"):** the push pipeline becomes a parent that starts
  child pipelines. The parent runs the plan, starts through the Gitea API (workflow_dispatch) only the children the plan names, in the order
  of their dependencies, waits for them and ends with one table (child, run number, result, time, the reason when red). Each child is its own
  workflow with one concern (the D2 steps: app native tests, app in the emulator, RISC-V images, simulation per owner, Vivado per host/core,
  .cor per core, benches, release package, update files), can be started on its own (by hand, nightly, the parent, tools/dev.sh) and hands its
  result over through the content-keyed cache. A red child is red at once with its reason in its own run; nothing is deferred to the end.
  Not chosen: one pipeline with `if:` per job (A).

- **D5 (Alex, 2026-11-08, translated, after run 1200 re-ran the 70-minute `atari_machine` for an LED change already passed on the board: "Please.
  That is why we do all this with the CI. Segment the caches better. If that is not built now, you may abort again" - run 1200 aborted):** a
  key covers exactly what its unit reads, never a whole directory. Coordinator decisions under D5:
  1. Sim benches: the key of a bench is the content of the files the simulator actually read for it (the iverilog list already exists; GHDL,
     Verilator and the files a bench opens at run time - `$readmem`, hex/ROM images, generics - get the same record), not the directory macros
     of `ci/sims.deps` (`@a8 = cores/atari800 ...`). A change to a file the bench does not read never re-runs it. Proof: touching
     `cores/atari800/layer2_atari800.v` (not read by `tb_a8_machine`) keeps `atari_machine` a cached PASS; touching a file it reads re-runs it
     (negative control). A bench whose reads are not recorded keeps the whole-tree key, as today.
  2. The same rule for every other keyed unit that still keys a directory (app test units, render, emulator, .cor, Vivado inputs): list them,
     narrow each to the files read, with a negative control per kind.
  3. A child's time limit comes from the benches it runs (their `ci/sims.txt` timeouts), not a fixed number per lane.
  4. (Alex, 2026-11-08, translated, after a one-line logo-path fix in render.cc re-ran all of the render bench's ~50 negative controls:
     "Why do we make 50 mutations for a logo test?! Where is the sensible ratio?! We are not building bank software that moves money. This is
     a hobby project.") Then, replacing the first reading (controls nightly): "The negative controls run in exactly 2 places: flash operations,
     persistence operations. Nowhere else. Never." Negative controls exist only for tests of flash writes/erases and of persistence (settings,
     saves, journals, anything written to keep); every other control is removed from the suites (not moved to nightly). Lint fails when a
     control loop exists outside those two areas.

### D5 built (2026-11-08, agent t174-d5)

1. **Read record for benches - automatic.** `ci/simreads.c` (LD_PRELOAD, built by `ci/sims.sh` with gcc in trx64-sim-env) logs every file the bench's
   process tree opens (ghdl, verilator, iverilog, make, python, sh, the files a bench opens at run time), the paths looked for and not found (`?p`)
   and directories listed (`@d`); `ci/simreads.py` reduces that to tracked files. **Every bench of ci/sims.sh now runs under the preload**; a PASS
   with a non-empty log stores its record in the runner's cache (`$CI_SIMS_OK/../reads/<bench>`) and the PASS marker goes under the key of that
   record (`(record taken)` in the table). A bench with a stored record (committed in `ci/sims.reads` or taken by the runner) is keyed by exactly
   those files (+ whether the looked-for paths exist, + the names in listed directories); a bench without one keeps its `ci/sims.deps` / whole-tree key
   until its first PASS. A run that reads more than the record extends it and is keyed by the extended record; an empty log is never cached.
   `ci/sims.sh --key NAME` prints key/kind/file count, `--record [--record-seconds N] NAME` writes the committed record. Committed: `atari_machine`,
   `atari_led`, `keyboard` (iverilog), `neogeo_pad` (Verilator, re-recorded without its removed control: identical). Lint: a committed record needs
   `@reads` in `ci/sims.deps` (`tools/check_sims_reads.py`).
   **Proofs** (`ci/sims.sh --key`, the printed lines; touched = one line appended, then `git checkout`):
   - `atari_machine` (GHDL, the 70-minute bench): before `KEY atari_machine e7ba199e4b6f106c recorded files=72`; after touching
     `cores/atari800/layer2_atari800.v` (not read by `tb_a8_machine`) the same `e7ba199e4b6f106c`; after touching
     `cores/atari800/sim/machine/tb_a8_machine.vhd` (read) `4ab350820e027ade`; restored `e7ba199e4b6f106c`.
   - `neogeo_pad` (Verilator): before `3bf1b9acaa678e02`; touching `cores/neogeo/trx/neo_mux.v` (not read) `3bf1b9acaa678e02`; touching
     `cores/neogeo/trx/neo_pad.v` (read) `70e9116de02e50e2`; restored `3bf1b9acaa678e02` (`recorded files=12` each time).
   - `keyboard` (iverilog): before `3e7fcf9dbe83302d`; touching `host/top.v` (not read) `3e7fcf9dbe83302d`; touching `host/c64u_keyboard.v` (read)
     `45a4d98e10bcae8f`; restored `3e7fcf9dbe83302d` (`recorded files=3`).
   - Automatic recording, builder, trx64-sim-env:4, `ci/sims.sh -f`: 52 bench runs (37 + 15), all PASS, each `(record taken)` (e.g. `c128_regs`); the next run
     without `-f`: `c128_regs cached PASS a1e2cc7abbe4e1c6 recorded` (`--key c128_regs` prints the same key, `files=4`).
   Not done (nothing hidden): the benches that have not passed on a runner since this change keep their old deps key until their first PASS; the
   `@reads` lines in `ci/sims.deps` of those are not rewritten (the runner's records are not committed; `--record` writes them when Alex wants them).
2. **Other keyed units** - inventoried, not changed: `ci/apptests.sh` suite keys and `ci/render.sh` stage key name whole application trees, but the units
   inside key by recorded reads (T100 D14, tests/ctlcache.sh), so a narrower stage key only skips a cheap walk; Vivado keys (`gateware_key` over
   `designs cores`, `layer_specs`/`mono_key` per core) stay as they are (coordinator decision: a narrower key risks a wrong bitstream). Emulator
   scenarios and `.cor` (`ci/pack_one.sh`) key on the built images and the cached bitstream key, no directory of their own.
3. **Time limit from the benches - built.** `ci/sims.sh --limit` prints the minutes the benches that would run may take (sum of timeout x threads / budget +
   longest + 5 min, cached = 0); `ci/child.sh` wraps the sim child in `timeout` with it; `sim.yml` `timeout-minutes` is only a ceiling (720). The 45/120/150
   of `ci/targets.txt` are Vivado limits per kind and stay (not benches).
4. **Lint** `tools/check_sims_reads.py` (+ `--selftest`, 8 cases), wired into `ci/lint.sh`.
5. **D5.4 negative controls only in flash and persistence tests** (Alex: "exactly 2 places"). `tools/check_controls.py` (+ `--selftest`, 13 cases,
   `--inventory`), allowlist `ci/controls.allow.d/*.allow` (coreapp, hostapp, sims1 = Apple II disk-image write-back, c64 = "C64 cores untouched
   (Alex)": `cores/c64u`, `c64ref`, `c64scpu` and their `ci/sims.txt` lines), wired into `ci/lint.sh`; it fails when a control exists outside the
   allowlist. Inventory (controls found; base = before D5.4 at 65a0d35b1, now = after):

   | area | base | now | kept (why) |
   |---|---|---|---|
   | tests/coreapp | 1538 | 207 | flash write-back of cartridges, noflash, settings/.cfg, media write-back, saves |
   | tests/hostapp (+ render, ui) | 217 | 17 | srcverify, FAT chain, saves, flash scan, firmware restore, flash lock |
   | tests/loader | 43 | 3 | /flash write |
   | host/sim | 50 | 0 | - |
   | cores (all simulation scripts except Apple II and C64) | 176 | 0 | - |
   | ci/sims.txt (arguments / benches) | 75 | 0 (the C64 bench lines keep theirs) | - |
   | cores/apple2/sim | 60 | 9 | disk-image write-back: write, write protect, keep-old-track, nibble gaps (persistence; decide if Alex wants them gone too) |
   | cores/c64u, c64scpu (C64, untouched) | 24 | 24 | allowlisted: C64 cores untouched (Alex) |
   | total | 2183 | 260 | |

   Benches deleted with their controls: rvtrap_ctl dcache_ctl zxnext_sd_mut zxnext_sd_seq zxnext_sd_hold_mut zxnext_clk_ctl snes_mem_mut apple2_video_ctl.
   **RTL hooks** (`ifdef *_MUT_*` / `CTL_*`) are removed from the product RTL and testbenches of every non-C64 core (zx, zxnext, cpc, nes, snes,
   neogeo, ddt_mandel, ddt_novic, c264, c128, amiga, amiga1200, diag): the non-mutated branch stays; the recorded `.diff` files of the forks are
   regenerated (`check_ddt_tree`, `check_wrapper_fork` ok). Not touched: `cores/c64scpu`, `c64u` (C64), and `host/trx_gain.v` with its copies
   `cores/*/mono/host/trx_gain.v` (host is immutable, host pin gate; `TRX_GAIN_MUT_BYPASS` stays, reported); `host/sim/portlock/tb_portlock.v` has one
   `CTL_OLD` testbench define. These cores need a Vivado rebuild anyway.
6. **Re-runs** (builder, after the edits): `tools/dev.sh test` - all 17 units PASS (cached from the run after the edits). Render stage
   (`ci/render.sh` in u64-build-env:ci): coreapp render, view, fs64, loader, hostapp suites with the remaining flash/persistence controls PASS;
   `hostapp_render` RED with 6 of 1271 checks (`render.cc:939/945/1126/1139/1140/1144`, all the TRX64 logo path), which is the bug fixed on `main` by
   `6c2a3e8ee` (not on this branch): it needs that commit. Benches `ci/sims.sh -f` in trx64-sim-env:4: 52 PASS, 0 FAIL (apple2_pins zx_pins cpc_pins
   zxnext_pins snes_pins null_regs c128_variant c128_avpins c128_scan c128_regs c128_romc c128_keys neogeo_pixw nes_padswap amiga1200_fifo/afifo/
   floppybuf/rawkey/motorwire/motorled amiga_rtc amiga_dskchg amiga1200_dskchg c1541leds_c128/c264/vc20 ddt_mandel_pins ddt_novic_pins diag_pins hotkey
   esp_ctl bling pktport tmds_rep host_gain dcache atari_led zx_pause cpc_fdcscan zx_machine cpc_video neogeo_audio ddt_mandel_pix ddt_mandel_joy
   ddt_novic_pix apple2_clk zxnext_boot zxnext_keys c128_machine c128_pause c128_ramd). Not re-run (long, minutes to hours; the scripts passed
   `bash -n`): the other benches of the edited scripts (amiga, apple2 hdd/rwts/woz/video, snes, zx video/io/switch, zxnext sd/mem/video, c128
   vdc/vic/cold, neogeo video/dl/speed, diag probe/co). Lint: `check_controls.py` ok, `--selftest` ok, `check_sims_reads.py` ok, `--selftest` ok,
   `ci/test_plan.sh` ok, `ci/orchestrate.py check` ok, zx_gen / zxnext / snes / neogeo / ddt / large-copies / host-pin checks ok.

### D4 design (2026-11-08, agent, branch t174-d4; coordinator, Alex's words translated: "fix the workflow, make the CI lean")

Lean: as few workflow files and jobs as the concerns need, no no-op job, no deferred red, the parent's table is the one place to look; dead stages,
scripts and lint rules go.

| child | workflow | started when (plan.json) | needs | script |
|---|---|---|---|---|
| lint | - (a step of the parent, on the NAS, while the children run) | not docs-only | - | `ci/lint.sh` |
| apptests | `apptests.yml` | an app, the app tests or a fundamental file changed | - | `ci/render.sh` |
| images | `images.yml` | an app changed, or `cor`/`package` runs (they read the images) | apptests | `ci/trxapps.sh` |
| emu | `emu.yml` | the host or core app changed | images | `ci/apptests.sh --tier integration hostapp_emu coreapp_emu` |
| sim `<owner>` | `sim.yml` | the plan names a bench of that owner (`ci/sims.lane`) in the run's tier | - | `ci/sims.sh -t <tier> <its benches>` |
| vivado `host` / `core:<id>` / `large:<id>` | `vivado.yml` | the plan names the host (or aux) / the core (its gateware changed) | sim of the same owner; a DFX core also vivado host | `ci/bitstream.sh c64u --lane ...`, `ci/bitstream_mono.sh --lane <id>` |
| cor `<core>` | `cor.yml` | scope `branch`: a named `.cor` core or large core | vivado of the core, images | `ci/cor.sh <core>` |
| package | `package.yml` | scope `full`, or update files asked for | everything above | `ci/package.sh` (job `collect` first: lanes this run did not build come from the cache) |
| update | `update.yml` | `ci/select.sh update` = yes | package | `ci/update.sh` |
| ue2 | `vivado.yml` (target `ue2`) | `ci/select.sh ue2` = yes | package, update | `ci/bitstream.sh ue2` |

- **Parent** (`pipeline.yml`, on push and by hand): one job on the NAS runner: fetch, wait for its turn (`ci/wait_turn.sh`), `ci/plan.sh`,
  then `ci/orchestrate.py run`: the DAG above from `ci/children.txt` (the one table: child, workflow, needs) and plan.json; it dispatches the
  ready children (`POST .../actions/workflows/<file>/dispatches?return_run_details=true`, inputs `sha`, `parent`, `target`, `plan`), runs the lint
  meanwhile, polls, and ends with one table: child, run (number and link), result, time, reason. A red child turns the parent red; its
  dependents are listed `not started: <child> red`. Docs only: `nothing to do (docs only)`, no child.
- **Children**: `on: workflow_dispatch` (plus `workflow_call` where nightly needs it), `run-name` "<child> <target> @<sha8> (parent <id>)".
  Each checks out `inputs.sha`, never the branch head, and runs `ci/child.sh <child> <target>`: hand-over directory of the parent
  (`/cache/multicore/runs/parent-<id>`, builder) and its own red directory, a commit status `ci/<child>[/<target>]` (pending, then success or
  failure with the reason), then the script. Red is red at once: `CI_RED_DEFER`, `verdict`, `sim_gate` are gone (the DAG does not start a
  Vivado run behind a red simulation). A bench TIMEOUT is red with `TIMEOUT after N s`.
- **Hand-over**: content-keyed cache as before; images and bitstream stage directories under the parent's hand-over directory (all children
  run on the builder). The plan travels as the `plan` input.
- **Gitea limits** (1.27.3): the NAS runner has capacity 2 and only parents run there (the lint is a step of the parent: a lint child queued
  behind two waiting parents would deadlock); the builder takes the children (3 slots, Vivado one at a time through `heavy_lock`).
  Dispatch rate: one dispatch per second at most. A child that is not running `CI_CHILD_START_MIN` (default 240) minutes after its dispatch
  is red `never started`. Cancel: Gitea has no cancel API; the parent cancels its children on SIGTERM through the web route (best effort), and
  a child that starts after its parent ended stops at once (`parent <id> is <status>`).
- **Retired**: the lane jobs, `ci/lanes.py`, `ci/test_lanes.sh`, the `verdict` job and `ci/verdict.sh`, `update-decide`, `lint.yml`,
  `large.yml` (into `vivado.yml`), the package's feature-branch mode (`cor` children instead), `plan.py named-lanes|scope`, `CI_RED_DEFER`,
  `sim_gate`, `TRXAPPS_NO_EMU`.
- **Test**: `ci/test_orchestrate.sh` (fake dispatcher): DAG order, only planned children, red stops dependents, docs only, feature-branch scope;
  each with a negative control.

### D4 built (2026-11-08, agent, branch t174-d4)
- `ci/test_orchestrate.sh`: `orchestrate test ok` - 1 DAG order, 2 only planned, 3 red stops dependents, 4 docs only, 5 branch scope, 6 never started,
  7 check (each with its control). `tools/prepush.sh`: `prepush: lint ok for 31ac4697`.
- Real run (push of 196b5d30, head commit with `[sims]` = tier 2; diff base 20ef7044 = Gitea main, so D3 is in the diff too): parent run 1194 (#327),
  23:39:35-00:50:44 UTC, 71 min, red. Its table, quoted:
  ```
  === children of parent 1194 (196b5d30, scope branch, sims tier 2)
  child     run       result       time     reason / link
  lint      -         green        7m04s
  apptests  #328      green        66m44s   .../actions/runs/1195
  sim host  #329      RED          30m10s   failure: sims.host: bench statebus: TIMEOUT after 1800s (exit 1, 1801s)
  images    #330      green        0m37s    .../actions/runs/1197
  emu       #331      green        2m04s    .../actions/runs/1198
  ```
  Sim child: `a budget of 8 cores on 16 cpus`, `ultsys: PASS, 44s`, `ring_core: PASS, 666s`, `statebus: TIMEOUT after 1800s` (the bounded loop of the sims-fix
  WIP did not end it inside 1800 s; cause still open). The red was in the parent's log and as commit status `ci/sim/host` at 00:10, 30 min after the push
  (run 1193: red only at the end, the cause inside a green job). Children dispatched 21 s after the parent started; images 28 s after apptests ended.
  Five children, no no-op job (run 1193: 49 jobs, 20 of them green no-ops). Run-name works: `sim host @196b5d30... (parent 1194)`.
- Times against run 1193 (1h04, render cold 63 min, sims 30 min): apptests cold 66m44s (the D3 key version invalidated every unit; it is the critical
  path), sims 30m10s (the statebus timeout), the parent 71 min. The orchestration itself added well under a minute; a warm apptests would end the run
  at about 31 min (the sim child).
- Limits seen: the job container of the parent is created with `sleep 10800` (the runner's 3 h job cap); a parent whose children run longer (a host plus
  every DFX core in Vivado) would be cut there. Not hit here; to raise in the runner config (NAS) when it is.
- Open: statebus TIMEOUT (cause unknown, the bench audit / trx64 removal is another branch); a large core's `.cor` and the host `.txu` on a feature branch
  come only with `[bundle]`/update (the package runs then); an app-only change on a branch makes no `.cor` (tools/dev.sh cor does). CLAUDE.md still describes
  the `verdict` job (line 40-42): not edited by this agent (project instructions), text for the coordinator in the hand-over.

### D1 build side (2026-11-07, agent t174-ci; the test side is another branch)

Built on branch `t174-ci`, lint green on the builder (`tools/prepush.sh`, 4m29). Not pushed, no pipeline run yet: the CI side is unproven until the
coordinator's push (the workflow edits are checked by `ci/lanes.py check` and the pipeline-graph check of `ci/lint.sh`, not by Gitea).

| part | built | where |
|---|---|---|
| build / render split | `ci/trxapps.sh` = images only (hostapp, coreapp, 14 blueprint copies, testlib.u2a, unzip.u2a), same output paths, `ci/package.sh` unchanged; `ci/render.sh` = host tests + 3 render benches + 2 fs64 benches, own key (`files_key` over what they read + driver), PASS marker `/cache/multicore/render-ok/<key>` | `ci/trxapps.sh`, `ci/render.sh`, `.gitea/workflows/render.yml` |
| pipeline | `trxapps` and `render` need `plan` only; `package` needs `trxapps`, `lint`, `plan`, lanes; `verdict` needs `bit-ue2` and `render`; render is not behind the package | `pipeline.yml`, `ci/lanes.py` |
| incremental | persistent tree `/cache/multicore/build/ci-<branch>` in CI, `~/dev-trees/<branch>` on the builder for `tools/dev.sh`; `tools/treesync.py` copies only files whose sha256 changed (no rsync on the builder or in the images: a manifest in the tree), removes deleted ones, keeps build outputs; `--clean`; make now runs `-j` (gitinfo.h written beside and moved over, so no half file is read); ccache kept (`CCACHE_BASEDIR` = the tree) | `tools/treesync.py`, `ci/test_treesync.sh`, `host-app/target/common/rules.mk` |
| dev entry | `tools/dev.sh build|test|riscv|integrate [--branch B] [--clean]` on the Mac, runs on the builder through `~/scratch/appslot.sh`; `test` calls `ci/apptests.sh --tier fast` when that exists, else `ci/apptests.sh`; `integrate` = riscv + `ci/render.sh -f` with `CONTROLS_ALL=1` + `tests/ui/run.sh` and `--controls` on the Mac when ue2emu is there (skipped out loud when not) | `tools/dev.sh` |
| R1c | heavy slot released after the images are built (`exec 9>&-`) | `ci/trxapps.sh` |
| R3 | `mark_red` posts a commit status `red/run-<id>/<stage>.<target>` (state failure) and a job-summary line at once when `CI_STATUS_TOKEN` is set (all lane, package and render steps get it); the verdict still decides alone | `ci/lib.sh report_red`, the stage workflows |
| R2 | `host:` is an explicit file list; `trx_constraints.tcl` (a pure check) moved to `host/check/`; `ci/layers_check.sh` fails a file there that calls `set_property`, `create_pblock`, `read_*`, `set_*_delay`, a flow step or `source` (selftest plants six); `ci/test_layers.sh` proves an edit in `host/check/` moves no key and one in `host/core.tcl` moves the host key; host pin updated | `host/layers.txt`, `ci/layers_check.sh` |
| R5 | `lint` runs on the NAS runner (`runs-on: nas`, timeout 30 min); `update-decide` (new job, NAS) answers `ci/select.sh update` and the builder's `update` job skips clone and build at once on "no"; lanes no longer wait for lint | `lint.yml`, `update.yml`, `pipeline.yml` |
| NAS runner | `nas-runner-2` `capacity: 2` (`/srv/Container/act-runner2/data/config.yaml`, backup `.bak-t174`), that container restarted (no job was running) | NAS |

**Deviations and findings (decide or confirm):**
- `verdict` stays on the builder. It reads the red files the lanes write into the builder's `/cache/multicore/runs/<run>.red`; the NAS has its own cache (E7), so a NAS verdict would read nothing and turn green. Moving it needs the red state in the Gitea API (the commit statuses above are posted best-effort, so they cannot be the authority) - to decide with Alex.
- The NAS has two runners with label `nas`, both serving this repository (`nas-runner-2` and `trx64-runner`, capacity 1 each), so the NAS already had 2 slots; with `nas-runner-2` at 2 it has 3 on 4 slow cores and about 8 GB available. `plan` holds one while an older run goes.
- Open in R2: (c) the `shared:` key (`tools/vivado_lib.tcl` carries report helpers as well as flow) and (d) "which files changed" on a host miss are not built. The first host build after this change misses once (`build.tcl`, `core.tcl` and the list moved): best bundled with a host change that has to build anyway.
- `dev.sh build` compiles and links both applications (make decides what is stale); it does not select "touched apps" by itself. The CI-side tree sync uses the same tool, so a branch's second push rebuilds only changed files; the first push of a branch builds from scratch in its tree (ccache warm).
- `gitinfo.h` carries the build minute, so the six sources that include it (hostui, hostapp, coreapp, run, routes, app_api_impl) recompile on every build: 17 s of the warm run below.
- The render stage compiles its benches natively every run that misses its key; it has no persistent tree (its LVGL objects and unit results are cached).

**Timings (builder, `u64-build-env:ci`, 6 cpus, ccache warm unless said; remote = inside the container incl. slot wait, wall = from the Mac):**

| run | remote | wall |
|---|---|---|
| `dev.sh riscv --clean` (empty tree, ccache warm) | 41 s | 48 s |
| `CCACHE_DISABLE=1 dev.sh riscv --clean` (cold, no ccache) | 50 s | 58 s |
| `dev.sh riscv`, nothing changed (after D2 gitinfo, previous agent) | 10 s | 13.0 s |
| `dev.sh riscv`, one line added to `coreapp/src/run.cc` (previous agent) | - | 13.3 s |
| `dev.sh riscv`, nothing changed, two runs (this agent, docs-only edit in between) | 9 s / 10 s | 13.1 s / 14.2 s |
| `dev.sh riscv`, line appended to `coreapp/src/run.cc` (`trxapps: images built in 6s`) | 10 s | 14.3 s |
| before D2 (gitinfo stamped per build) | 22 s | 25 s |
| `dev.sh emu` (host-app branch: images from the cache, no emulator script applies) | 10 s | - |
| `dev.sh integrate` (`ci/render.sh -f`, `CONTROLS_ALL=1`: `=== render e7e1d5e7878ef011: PASS in 27m05s`; render-hostapp 1328 s, render-coreapp 251 s) | 1635 s | - |
| `dev.sh build` (compile + link only), nothing changed | 14 s | 15 s |
| CI before (run 1133, serial `make`, no `-j`) | 2m43 for the RISC-V build | - |
| `tools/prepush.sh` (lint, builder) | - | 4m29 |

### D2 build side (2026-11-07, agent t174-ci; the test side is still branch `t174-tests`)

Lint and the pipeline-graph check are the gate (`ci/lanes.py check`, `ci/test_plan.sh`, `ci/test_lanes.sh`, `ci/test_layers_flow.sh`); not pushed, no pipeline run yet.

| D2 | built | where |
|---|---|---|
| gitinfo stamp | `gitinfo.h` derives from the commit alone: the build date is the commit date, the host name is the constant `trx`, and the file is replaced only when its content differs. An unchanged tree recompiles nothing; a new commit changes the hash and the version string, so the six sources that include it compile again (`make` sees a changed header). The version string itself is still stamped per package (T172). | `host-app/target/common/rules.mk` |
| 1 only code triggers | `ci/plan.py` drops `docs/`, `.claude/`, `*.md`, `TODO*`, `LICENSE` from the diff before it maps files to targets; `docs_only` and a note in plan.json. A `*.md` inside a code tree still moves that tree's content key, so a stage may find a cache miss there; the plan does not name it. Selftest `D2.1` in `ci/test_plan.sh` (a docs-only push names nothing, a code file next to it is not hidden; 2 controls). | `ci/plan.py`, `ci/test_plan.sh` |
| addendum: feature branch | plan.json `scope`: `full` on main, `integ-*`, `release/*`, tags; `branch` on any other. On `branch`, only the cores the branch touches are named, plus the host lane (host + c64ref) and `null` with the host; an app or host change no longer fans out to every DFX and large lane, and `package.fresh` lists the touched cores only. `--branch` or `GITHUB_REF_NAME` decides. Selftest `D2 addendum` (5 controls). | `ci/plan.py`, `ci/test_plan.sh` |
| 2/5 native, then emulator, then RISC-V | pipeline: `trxapps` needs `render` (the native tests); red native tests mean no RISC-V build (`red trxapps.images`, the stage ends green under defer). `tools/dev.sh integrate`: native tests (`render.sh -f`, all controls), then the images, then the emulator scripts on the Mac. `tools/dev.sh emu`: images, then the emulator scripts of the touched app(s). **Deviation:** ue2emu runs the RISC-V image itself, so "emulator before the RISC-V build" can only mean before the *stage* (size check, testlib, unzip, coreapps, package); the compile of the application is the emulator's input. The host app has no emulator script that runs without arguments (`tests/hostapp/emu_txu.sh` needs a stick, a script and a `HOSTAPP_EXTRA=-DHOSTAPP_EMU_FLASH` build): `dev.sh emu` says so out loud and does not run it. CI has no ue2emu (macOS only): the emulator scripts stay a Mac step. | `ci/trxapps.sh`, `pipeline.yml`, `trxapps.yml`, `tools/dev.sh` |
| 3/4 simulation, then Vivado | new job `sims` (needs `plan`) in front of every lane (`ci/lanes.py` writes the needs; `check` demands the job): it runs the benches the plan names (what changed, owner lane fires; cached PASS skips); a red bench files `sims.<owner>` (`host`, `dfx-<core>`, `large-<core>`), `ci/lib.sh sim_gate` stops that owner's Vivado run (`not built: simulation red`, a red target) before `build_host` / `build_core` / the flat build. Selftest `D2.3` in `ci/test_layers_flow.sh` with the fake Vivado (red bench: core.tcl not called; control: same change without the red file: built). Cost: a lane starts after `sims`; a push that changes no gateware names no bench and `sims` is green in seconds. | `ci/sims.sh`, `ci/lib.sh`, `ci/bitstream_layers.sh`, `ci/bitstream_mono.sh`, `ci/lanes.py`, `pipeline.yml`, `sims.yml` |
| 6 benches | `ci/fundamental.txt` (desc.h, `app/view`, `cores/common`, `host`, the bench harness itself `ci/render.sh` / `ci/apptests.sh` / `tests/coreapp/render`, Makefiles, `vendor/gideon/software`; `ci/` and `tools/` as a whole are not on it, coordinator decision 1 below); plan.json `fundamental` = main-like scope or a file of that list; `ci/render.sh` runs the render and file benches only then (or with `-f`), writes no stage PASS marker when it skipped them, and says so; `nightly.yml` calls `render.yml` once a night on main. The host tests (`ci/apptests.sh`) run either way (their tiers are the test side). | `ci/fundamental.txt`, `ci/plan.py`, `ci/render.sh`, `render.yml`, `nightly.yml` |
| 7 single .cor | `ci/pack_one.sh <core>`: the tree's own host / reference / core keys (build options read from the `Vivado` step of `bitstream.yml`), the entries from the bitstream cache, `tools/pack_cores_mc.sh --core`; no entry: it names the missing key and the newest entries and builds nothing. `tools/dev.sh cor <core>`: images, then that, kept in `~/scratch/cor/<branch>/` on the builder and copied to `.ci/dev/<branch>/<core>.cor`. DFX `.cor` cores only (a large core's `.cor` is packed by `ci/package.sh`). | `ci/pack_one.sh`, `tools/dev.sh` |
| 8 full package | `ci/package.sh` reads the plan's `scope` (`ci/plan.py scope`): `full` (main, `integ-*`, `release/*`, tags) builds the whole release package as before; `branch` packs only what the plan names (`ci/plan.py named-lanes`): the touched DFX core(s) through `ci/pack_one.sh`, a touched large core, the host's `.txu` with null; no imported `.cor`; nothing named = `=== package SKIPPED: feature branch, the plan names no host, core or large core`. `ci/lanes.py check` fails when `package.sh` lacks the scope (control in `ci/test_lanes.sh` 5b); selftest `D2 7/8` in `ci/test_plan.sh`. | `ci/package.sh`, `ci/plan.py`, `ci/lanes.py` |

**Coordinator decisions under Alex's D2 (2026-11-07, translated):**
1. `ci/fundamental.txt` drops the `ci` and `tools` lines: a pipeline edit is not fundamental. Only the bench harness itself stays (`ci/render.sh`, `ci/apptests.sh`, `tests/coreapp/render`). Selftest `D2.6`: an edit of `ci/lib.sh` on a feature branch asks for no full benches, an edit of `ci/render.sh` does.
2. The full release package (all `.cor`, `.cfw`, imports, bundle) is built only at scope `full` (main, `integ-*`, `release/*`, tags; D2 7-8). On a feature branch the package job packs the scoped `.cor` files only (touched cores, host update with null) via `ci/pack_one.sh`, or ends with a clear line when nothing is in scope. `ci/lanes.py check` and `ci/test_lanes.sh` agree (see row 8).
3. `trxapps` keeps waiting for `render` (Alex's principle 2: the app is tested first, then the RISC-V images).

**Verification against the cache (builder, `u64-build-env:ci`, real clone of a bundle, commit dc11e82f of main, host entry `host-2bb1de24bb837ffd`):**
- First run: `ci/pack_one.sh vc20` said `no host entry` with `host key 610089adc5554449`: the script keyed with the short part name `50t`, the lanes key with the part string of `ci/bitstream.sh` (`xc7a50tfgg484-1`, it is part of every key). Fixed: the script reads the string from `ci/bitstream.sh`.
- After the fix: `=== pack_one: /tmp/cor/vc20.cor  host 2bb1de24bb837ffd  vc20 ed0ab497371c0bad (from dc11e82f)`, `fa397d563bac4c52cb3ebb90fc98dc346951a2455639b8059884f0d3f72b65f3  vc20.cor`, 2720248 bytes. The key equals the one the lane wrote into the cache entry (negative control: the old code did not find it).

**Open points (decide or confirm):**
- The render stage on a branch without a fundamental file does not test the host app's or core app's rendered screens (the cheap tier is the host tests); `dev.sh integrate` before the merge does.
- (replaced by D2.8 below) `tools/dev.sh emu` ran an emulator script only for a touched core app; the host app had no emulator script that runs without arguments (`tests/hostapp/emu_txu.sh` needs a stick, a `.ctl` script and a `HOSTAPP_EXTRA=-DHOSTAPP_EMU_FLASH` build): said out loud by the script, not built (decided).
- (done) `dev.sh test` calls `ci/apptests.sh --tier fast` now that the test side is merged.

- **D1/D2 test side, built 2026-11-07 (branch t174-tests; `ci/apptests.sh`, `tests/ctlcache.sh`).** `--tier auto|fast|integration|nightly`, `--plan`,
  `--base`, `--emu-pass`. `auto` (default): integration on main or when a fundamental file is touched (desc.h, app/view, app/video, the test machinery,
  the app Makefiles, LVGL), else `fast` scoped to the suites the diff touches plus the emulator scripts of touched UI (docs-only: nothing runs).
  `fast` = test programs only (control wrappers return before they are keyed; a fast PASS writes no suite marker). `integration` = all tests, controls,
  the three render benches, emulator scripts listed for the Mac (`hostapp_emu`, `coreapp_emu`, PASS recorded by `--emu-pass`). `nightly` = integration
  with `-f` and `CONTROLS_ALL=1`. Plan line first (`will run: ..., skipped: ...`); `--plan` walks the units with nothing built (`CC_DRYRUN`) and prints
  `plan: would run <unit> (no entry | reads changed: <file>)`. A miss now says why (`miss NAME: key text differs ...`, text kept under `apptests-ok/keytext`).
  Measured on the builder (u64-build-env:ci, 8 cpus, `/cache` = the shared cache, via appslot.sh): `integration` cold (cache empty for this tree, the
  harness file had changed) 3057 s = hostapp 209, coreapp 1379, loader 16, view 1, coreapp_render 219, hostapp_render 1229; `integration` again
  unchanged: 83 s, every unit from the cache incl. the render benches (view/coreapp_render 1 s, hostapp_render 77 s: 115 units verified);
  `fast` after appending a comment to `coreapp/src/ui.cc`: 110 s (hostapp 38 s all cached, coreapp 69 s: 3 of 102 units re-ran, loader 0 s);
  `--plan coreapp` with the same change: 63 s, "would run 7, from the cache 95". Editing a harness file (`tests/ctlcache.sh`) invalidates every
  unit (it is an input): expect one cold run after such a commit. Render benches had a hit once the key text carried no scratch/output path (`OUT`, `B`,
  `T` left out of the closure); whether CI's earlier 0-hit came from this was not reproduced, the per-unit miss line will tell on the next CI run.

### Sims red (2026-11-08, agent sims-fix; WIP, stopped by the coordinator)
Nightly runs 1181 and 1188 and pipeline run 1193 (job Simulations). Same set red in every nightly since 11-04 (jobs 12304, 12404, 12455, 12700, 13141, 13449).
Cause 1, resource: `=== 44 of 210 benches to run, 16 at a time`, and benches that start 3 to 12 processes of their own (hdmi_out JOBS 8, ovl_geom/ovl_hold 4,
pixscale 12) on 16 cores beside the render stage. Quoted: `--- done slot: TIMEOUT after 1800s`, `rvtrap_dc: TIMEOUT after 1800s`, `ultsys: TIMEOUT after 900s`,
`still60`/`pixlayer`/`apple2_pause: TIMEOUT after 2400s`, `hdmi_out: TIMEOUT after 9000s`, `host_hdmi_timing`: `no verdict in .../hdmi_timing/build/all.log: vvp ended on its 1500 s timeout (a loaded machine) or crashed`.
In run 1193 ultsys passed in 418 s and ring_core in 1049 s (3 benches), so these limits fit a free machine; alone-durations of the rest are not measured yet.
Cause 2, verdict (fixed before, fa9eb5396): hotkey, ovl_geom, ovl_hold had PASS lines but control lines `CONTROL nomask: FAIL as wanted` / `control c_old (...): FAIL as it has to` hit the FAIL grep (`FAIL (output)`); hotkey also printed `PASS hotkey bench and all controls`.
c64scpu_irq/_speed/_mirror/_map `FAIL (exit 1)`: experimental core, soft; `make ... exited with 2` (verilator build, parallel builds of one dir; fa9eb5396 builds once under flock). Not C64-core changes made here.
Cause 3, statebus: NOT load. Measured alone on the builder (trx64-sim-env:4, nice 15, one core at 99.9 %): build + boot 3 min (`READY after 69429408 clocks`, matches the cold-boot expectation), then `tb_statebus run` printed step 1 to 3 (`CIA 1 w0 by bytes 00FFFF7F, by JTAG 00FFFF7F`) and nothing after, for 40 minutes (killed at 2572 s total). Step 4 follows (`restore(mid)`, `wait_halted`, `stop_at_gclk`). The loop `while (top->gclk_edges + 64 < g) tick();` had no bound; it is now bounded (FAIL with state). A second run with the bound was stopped before it showed a verdict. Open: whether step 4 hangs for a real RTL reason (the bench passed on 11-04).
Change so far: ci/sims.sh schedules by a core budget (CI_SIMS_CORES, else CI_SIMS_JOBS capped to half the cpus, cgroup aware) and per-bench threads from ci/sims.threads; tb_statebus.cpp bounded loop.
Sims and render: both `needs: plan` in pipeline.yml, so they run at the same time on the builder (runner capacity 3). sims calls heavy_lock (a Vivado slot, "slot 0 of 2"); render.yml does not, so the lock only keeps sims away from a third Vivado/trxapps holder, it does not separate sims from render.
Left: not run on the builder - the fixed scheduler (untested beyond `bash -n`), per-bench limits from measured durations, the nightly re-run.

## §6 Questions for Alex (one at a time, in this order)

1. May the stage logs of the Vivado stages record load average, MemAvailable and swap at start and end (R4 measurement only, no limits yet)?
2. May the host `host:` key become an explicit input list with `host/check/` excluded and a lint rule that nothing under `host/check/` changes the design (R2)?
3. May agents' app-test and render runs write into the builder's shared `apptests-ok` cache (R1b), and may `trxapps` release the heavy slot after the RISC-V build (R1c)?
4. May `lint` move to the NAS runner with the lanes no longer waiting for it (package and update still do) (R5)?
5. May the NAS runner `capacity` go from 1 to 2 so `plan` waiting for an older run does not occupy its only slot (R5, memory guard 4 GB)?
6. Is a shared cache between builder and NAS wanted (NFS, rsync or an HTTP store), or do NAS jobs stay cache-free (R5 i-iii)?
7. May S9 run once: one 50T host build on the NAS in a throwaway container with 3 cores, to put a number on "no Vivado on the NAS" (it can use the NAS's last 8 GB)?
8. Do you want a commit status / run-summary line at each lane's first red (R3b)?
9. May the builder runner `capacity` rise above 3 once the memory guard exists (R6)?

---
<!--
Closing: Status → DONE (date) + which scenarios are green where; the TODO row gets ✅ and a
history entry, in the same commit as the code.
-->

### D2.8 One fundamental list, the emulator in CI (2026-11-07, agent t174-ci; build side and test side merged on branch `t174-ci`)

- **One list.** `ci/fundamental.txt` has the sections `[all]`, `[benches]`, `[apptests]`; `ci/plan.py` reads `[all]`+`[benches]` (plan.json `fundamental`),
  `ci/apptests.sh` reads `[all]`+`[apptests]` through `plan.py fundamental apptests` (its own `fundamental=(...)` array is gone). A path before any section is an
  error. `ci/test_plan.sh` D2.6 and `ci/test_apptests.sh` (2b) cover it with controls (a stray path is refused; a `[benches]` path does not leak into the
  apptests list; a plain coreapp change stays `fast`, `app/lib` and `coreapp/src/desc.h` escalate). The apptests list lost nothing: `vendor/gideon/software/lvgl` moved in
  as its own line; the benches list is unchanged.
- **Emulator image.** `UE2EMU_IMAGE` (ci/lib.sh, the one place) defaults to `ue2emu:v0.7.0`: the S44 release of u64-emulator, tag v0.7.0, commit
  4612884e7a633d52375277060f1a7e9bbf6552ba. `ci/ue2emu.sh` runs it: the local macOS binary (or `UE2EMU_BIN`) when there is one, else `docker run` of the image
  with the working directory and every absolute path argument mounted at the same path, `--network none`, as the caller. The emulator always gets `--headless`
  (the scripts pass it). ROMs, flash image, profile and a vc20 `.cor` are the kit (`tools/ue2emu_kit.sh`; the builder's is `/srv/trx64-build/cache/ue2emu-kit`,
  21.6 MB, written with `docker run alpine`, not in git); `ci/lib.sh ue2emu_kit` finds it. Hand-over paths of large cores are `/Usb0/cores/<x>.cor`
  (u64-emulator v0.7.0 `docs/examples/core-amiga1200.toml`); the vc20 flow uses the DFX path `--cor-dir` and `/USB0/cores/vc20.cor`.
- **Host flow.** `tests/hostapp/emu_host.sh <images> <out>`: host app start, F3 > USB0 > cores > vc20.cor > S, the core application identifies VC20, CTRL+RESTORE opens
  its menu, F8 > Return to host menu, the host takes the menu marker; PNGs `host-start.png core-menu.png host-back.png`. Negative control: the same keys with no `.cor` fails.
  `ci/trxapps.sh` builds the emulator variant of the host app (`HOSTAPP_EMU_FLASH`, only `flashfw.cc` reads it: one object and the link) as `hostapp_emu.elf/.app` and
  puts the tree back to the board build.
- **apptests.** `hostapp_emu` (emu_host.sh) and `coreapp_emu` (tests/coreapp/emu/run.sh) run at `integration` on the images in `EMU_IMAGES` (default: the trxapps hand-over);
  a PASS is cached by content like the test suites. Exit 77 (no images, kit or emulator) is SKIPPED out loud, never PASS; `--emu-pass` stays only as the fallback for that case.
  **In CI (coordinator, 2026-11-07):** the job container has no docker, so the emulator goes into the job image instead: `u64-build-env:emu-<UE2EMU_TAG>`
  (`ci/emu/Dockerfile`, built and copied to builder and NAS by `ci/emu/build.sh`) = `u64-build-env:ci` + ue2emu at the pinned tag on PATH (`ci/ue2emu.sh` takes it
  before docker). Built on Ubuntu 22.04 like the job image, without the `net` feature (22.04's libslirp 4.6 lacks `slirp_handle_timer`; the suites need no network).
  `trxapps.yml` runs in it, and `ci/trxapps.sh` runs `apptests.sh --tier integration hostapp_emu coreapp_emu` on the images it just built (ue2emu runs RISC-V
  images, so this is the earliest point); keyed, an unchanged app is served from the cache; red turns the stage red. `tools/dev.sh` sets `TRXAPPS_NO_EMU=1` in its
  container and runs the emulator scripts itself.
- **dev.sh.** `tools/dev.sh test` = `ci/apptests.sh --tier fast`. `tools/dev.sh emu` builds the images in the container, then runs emu_host.sh (host touched) and
  tests/coreapp/emu/run.sh (core app touched) on the builder host through appslot.sh and brings the PNGs to `.ci/dev/<branch>/emu/`. The container gets `JOBS=${JOBS:-5}`.
- **Measured (builder, appslot, JOBS=5).** `dev.sh test` after a one-line coreapp change: 413 s (hostapp 92 s, coreapp 316 s, loader 1 s). `emu_host.sh` in the container
  image: `PASS host->vc20->menu->host: partial found, VC20 identified, back on the host with the menu marker` and `control missing .cor: fails as it must` (26 s on the Mac with the local binary).
- **Stale scenarios fixed (6e712e7a0):** tests/coreapp/emu/run.sh `reset0` (Reset now asks "Reset the machine?", RETURN confirms; the script pressed RUN/STOP),
  `drive0` (the Library stays in `roms/` after "Set as 1541 ROM": `../` first) and `drivemenu0` (drive settings moved to F5 tab 1) followed the old menus; `reset0`
  got its negative control. Mac run: `tests/coreapp/emu: all scenarios passed, every control failed as it must`. `strings` was replaced by `grep -a`.

### D3 modularising (2026-11-07, agent, branch t174-d3, built)

- **Groups.** `tests/coreapp/cores.txt` maps core files to cores; `run.sh` with `CA_GROUP` runs the units of one group; the dependency list of every unit is checked against its group
  (a control proves the check refuses). `ci/apptests.sh` has suites `coreapp` (shared), `coreapp:cross`, `coreapp:<core>`; hostapp/loader keys exclude the cores' files. Fast PASS markers (`fast-<suite>.<key>`).
  Render and emulator are NOT split per core (coordinator decision, D3.1 narrowed): the render bench is one generic binary and the emulator suite one script, and
  their keys already serve unchanged content from the cache, so a per-core split would add keys without saving a run.
- **corereport.cc** took `APP_VERSION_HASH`/`APP_BUILD_DATE` from `#ifndef ... "unknown"` fallbacks; no Makefile passes them as -D (grep), so the board report would have shown
  "unknown". It now includes `gitinfo.h` like coreapp.cc and run.cc; the host tests get a fixed stand-in `tests/coreapp/gitinfo.h`.
- **Key version.** `CC_KEY_VERSION` in tests/ctlcache.sh replaces the harness bytes in unit and suite keys; `tools/check_cc_key_version.py` (lint) refuses logic changes without a raise; selftest scenario k + `bytes` control.
- **gitinfo.c.** rules.mk writes a constant gitinfo.h and a per-commit gitinfo.c (in both Makefiles). Two commits on the builder: `Compiling gitinfo.c` twice (hostapp, coreapp), plus `flashfw.cc` twice (the D2.8 emulator variant); `dev.sh riscv on builder (rc=0) took 13s`.
- **Keys for testlib/unzip:** `=== trxapps unzip.u2a: from cache /cache/trxapps/lib-apps/8247b5f9eacdca5c`.
- **NAS kit:** copied to NAS `/srv/trx64-build/cache/ue2emu-kit` (same sha256 listing hash as the builder), found by `ue2emu_kit` as `/cache/ue2emu-kit`.
- **Measured (builder, JOBS=5):** cold after the key-version bump 653 s, first run on the new keys 187 s; nothing changed 9 s; one-line vc20.cc change 94 s
  (`dev.sh test: test on builder (rc=0) took 94s`, 160 s before D3); comment-only ctlcache.sh edit 8 s (`took 8s`). Final tree (corereport.cc change, so the
  shared core app group reran): `=== dev.sh test: test on builder (rc=0) took 185s`.
- **Suites green on the final tree:** `plan test ok`, `lanes test ok`, `test_apptests: all scenarios hold`, `ctlcache selftest: ok` (dyn mode, lint image
  `trx64-sim-env:4` on the builder, all five controls red), `tools/prepush.sh` (see the commit log).

### 5.x Sims only on demand (2026-11-08)
Alex (translated): "the sims come out of the CI ... build the app, build the bitstream, tie the package; sims only on demand", and "after W1 we
introduce an integration branch, a sim can run there". `ci/select.sh sims-tier` answers 0 on `main` and feature-branch pushes (no sim child; vivado's
`sim(=)` need falls away), 2 on `integ-*`, releases/tags, the pipeline dispatch input `sims` or "[sims]" in the head commit. Replaces D2.4's gate
"vivado needs a green sim of the same owner" for those pushes.
