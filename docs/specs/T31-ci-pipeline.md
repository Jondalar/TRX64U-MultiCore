# T31 — Staged CI: lint, sims, app, bitstreams, package

**Status:** spec 2026-10-03, branch `t31-ci`. **Task:** TODO.md T31 (Alex started it 2026-10-03, M1 scope).
**Shares with:** T53 (second bitstream job, `T53-ue2-build.md`), T49 (the package the last stage feeds),
T15 (build stamp), T21/T40 (other branches that push to the same CI).
Evidence: `.gitea/workflows/build.yml` (before), `vivado/README.md`, the sim scripts under
`designs/c64_basic/sim`, the app repo's own `.gitea/workflows/build.yml`, run times of runs 307-320.

## 1. Goal

One Gitea pipeline of linked workflow files (one per stage), strictly in sequence, each a thin call of a
script under `ci/` that also runs on the Mac:

```
lint -> sims -> app -> bit-c64u -> package -> update -> bit-ue2 (optional)
```

`update` (section 3, decision 14) builds the tester `.cfw`/`.ue2` of the package on integ-m1, main and `v*` tags only.

A failing stage skips the later ones (no Vivado hour for a push that fails a bench). The 100T stage comes
last and only when asked for; the 50T package is published before it starts and never depends on it. Every stage keeps its
own log, the bitstream stages keep the utilisation and timing reports, `package` publishes what the single
job published before and a bundle on top.

Non-goals: building the ESP32 firmware, the `.cfw` update package itself (T49), running the Ultimate app's
own e2e suites (the app repo's CI does that on every push).

## 2. What exists

- One job `build` in `vivado-env:2024.1` (`--cpus=3 --init`), group `gateware`, no cancel. Clone, submodule,
  `gideon_sources.sh`, every `designs/*/build.tcl`, publish `out/*` to the generic package `trx64-ultimate`
  (`<branch>-<sha>`, `<branch>-latest`). 28-35 min per run (runs 307-320), of which Vivado is most.
- `tools/load.sh` reads `<design>.bit` from the package: file names stay as they are.
- Sims: 24 directories under `designs/c64_basic/sim`, run by hand on the Mac (iverilog, GHDL, Verilator,
  libresidfp, Lorenz suite from a path on the Mac). No CI stage, no image with the tools.
- Images on the NAS docker: `vivado-env:2024.1`, `u64-build-env:ci` (RISC-V, ESP-IDF, Nios II; the app repo's
  CI uses it), `u64-e2e-env:ci` (same plus ue2emu), `icepi-build-env:ci` (OSS CAD Suite, no C++ compiler).

## 3. Decisions (defaults; all can be changed in one place)

| # | Decision | Why |
|---|---|---|
| 1 | **Several linked workflow files** (Alex, 2026-10-04): `lint.yml`, `sims.yml`, `app.yml`, `bitstream.yml` (input `part`: `50t` or `100t`), `package.yml`, each with `on: workflow_call` and `workflow_dispatch` so it also runs alone; a thin `pipeline.yml` chains them with `uses: ./.gitea/workflows/<x>.yml` and `needs:` (one chain, no parallel branches). Group name stays `gateware`, `cancel-in-progress: false`, **in `pipeline.yml` only**: with the group also in the fragments every called stage queued for it again, another pipeline slipped in between two stages and the run sat `waiting` with a stage left (run 351: package done, `bit-ue2` blocked behind run 352, which a newer push would have cancelled). A fragment dispatched alone is therefore not serialised: look at the queue first, as before a push. | The NAS has 2 usable cores of 4 and runs one heavy job at a time. Old `build.yml` copies on other branches use the same group name, so their runs queue with ours. **Gitea 1.27.3 supports it** (checked 2026-10-04 in a throwaway repo, since deleted): local `uses:`, `with:` inputs, `secrets: inherit`, job outputs through `needs.<job>.outputs`, a failed called job skips the jobs behind it and the run ends `failure`, a fragment dispatched alone with inputs runs. The dispatch file name changes from `build.yml` to `pipeline.yml`. |
| 2 | **Each stage = one workflow file that clones and calls `ci/<stage>.sh`.** | The same script runs on the Mac and in the job; the YAML stays short. Change detection is in the scripts, not in the top-level file: no job carries an `if:`, because in this Gitea a job with an `if:` behind a failed stage is not skipped, it stays blocked and the run stays `waiting` for ever (run 332: app failed, three jobs blocked, run neither failed nor done, until a newer run cancelled it). Without `if:` the same failure ends the run `failure` with the rest `skipped` (run 340). So: sims skip themselves when nothing they read changed, `bitstream` reuses a bitstream when the key is unchanged, the 100T stage returns at once unless asked for. |
| 3 | **Hand-over between jobs on the shared cache volume** `/cache/runs/<run_id>/` (the volume the Vivado cache already uses, mounted in every job), not artifacts. Removed by `package`; `lint` deletes run directories older than 3 days (failed runs). | A job runs in its own container and the checkout is not shared. `upload-artifact` needs Node in every image (only the icepi image has it) and the registry round trip costs minutes for a 2 MB bitstream. The run is serialised, so the volume is safe. |
| 4 | **Images**: `lint`, `sims` in the new `trx64-sim-env:1` (`ci/sim/Dockerfile`: Ubuntu 22.04, OSS CAD Suite pinned as in icepi's image, g++, make, 64tass, shellcheck, ruff, python3-yaml), `app` in `u64-build-env:ci`, bitstreams in `vivado-env:2024.1`, `package` in `u64-build-env:ci`. Built on the NAS from the repo (`ci/sim/build_image.sh`, like `vivado/build_image.sh`), never installed on the NAS file system. | Toolchains live in images. |
| 5 | **Sims in two tiers.** Tier 1 (iverilog/GHDL unit benches) on every push. Tier 2 (Verilator benches of the whole `c64_core`) on `main`, integration branches `integ-*` (Alex, 2026-10-06: tier 2 runs before the merge to main, not only after; feature branches stay tier 1 and reach main only through the integration branch), `release/*`, tags, `workflow_dispatch` and when the commit message contains `[sims]`. The lists are `ci/sims.txt` (tier, name, timeout, command). A bench that already passed on the content of exactly the files it reads is not run again (per-bench key, section 4a): marker in `/cache/sims-ok/bench/`. | One push should not cost an hour of NAS before the Vivado stage starts; an app-only push leaves the benches' inputs untouched. |
| 6 | **Benches that need things the CI does not have are not in the list**, with the reason in the file: git history of old commits (`cart_equiv`, `lat` diff against a commit), external data (Lorenz suite, libresidfp download), interactive output (analysis only, no pass/fail). A bench counts only if its exit status or a PASS line decides. | A stage that cannot fail is noise. |
| 7 | **`app`**: builds the submodule's `ultimate.app` / `ultimate.elf` (`make u64ii_no_esp`-class target, no ESP32 image) in `u64-build-env:ci` with the app CI's ccache volume, and **caches the result keyed by the submodule commit** in `/cache/app/<sha>/`. A push that does not move the submodule pointer takes the cache. The app is one RISC-V image for the C64U and the U64 Elite II (T53: the image is told apart at run time by the FPGA size), so there is one app job. | The app changes rarely compared to the gateware; ccache alone would still link and run `openapi_check` every time. |
| 8 | **`bit-c64u`**: every `designs/*/build.tcl` for the XC7A50T, as before. **`bit-ue2`**: `c64_basic` for the XC7A100T (T53), an **optional stage after `package`**, run only for a `workflow_dispatch` with input `ue2: true`, a tag `v*` or a `release/*` branch (`ci/select.sh ue2`; Alex, 2026-10-03: "only at the end, when the thing is round"). It publishes its `_100t` files into the package versions `package` made; a failure of it never touches the 50T result. T53 itself needs one dispatched run to prove the build and timing. | The 100T build is a second full place and route for a board nobody has here; development builds the 50T only. |
| 9 | **`package`** publishes the package `trx64-ultimate` as before (same versions, same file names: `.bit`, `<design>.timing.rpt`, `.utilization*.rpt`, `SHA256SUMS`) plus `ultimate.app`, `ultimate.elf`, the app size reports and `manifest.json` (TRX64 commit, app commit, `gateware_key`, whether the bitstream was built or taken from the cache, per-file sha256 and size, WNS). `main-latest` and `tools/load.sh` keep working. | T49 reads the manifest. |
| 10 | **Timing gate** stays: WNS < 0 fails the bitstream job (as today). | Unchanged rule. |
| 12 | **Change detection by content, not by path filter.** `gateware_key` = hash of the tracked files under `designs/` (without `sim/`, `build/`), `constraints/`, `cores/`, the Vivado cache script and Gideon-vendoring scripts/patches, plus the generated `cores/gideon` (Gideon's VHDL as made from the submodule), the netlist options, the part and the image tag. `bit-c64u` looks up `/cache/bit/c64u-<key>/`; a hit skips Vivado and ships the cached bitstream with its reports, so **an app-only change (submodule pin, app code, UI text) builds the app and packages it with the identical bitstream**; a change in the submodule that moves `fpga/` changes `cores/gideon` and does rebuild. Docs-only pushes build nothing (`paths-ignore`, as before). Only bitstreams with met timing are cached; the 8 newest keys per board stay. The key also serves across branches (equal sources, equal bitstream). | Alex, 2026-10-03: app-only changes must not rebuild the C64 core. A path filter on the push cannot see the submodule's content; the key can. |
| 13 | **One heavy stage at a time by lock, not only by group.** `heavy_lock` (`ci/lib.sh`): `flock` on `/cache/heavy.lock`, taken by `sims` (when it runs benches), `app` (when it builds) and `bitstream` (when it runs Vivado), held until the script exits. | The group only sees a run as busy while one of its jobs runs; between two jobs the run counts as idle and another pipeline is admitted. Seen twice (run 352 started between bit-c64u and package of run 351; run 357 started while run 356 was between sims and app). The group still orders pipelines; the lock stops two heavy stages from overlapping. Verified with two containers on the NAS (the second waits). Old single-job `build.yml` runs on other branches do not take the lock. |
| 14 | **`update`** (Alex, 2026-10-10: "die .cfw per Pipeline nach Commit auf integ-m1 bauen"): stage after `package`, `ci/update.sh` in `u64-build-env:ci`. It downloads `c64_basic.bit` and `ultimate.app` **from the package version `package` just published** (not from the hand-over directory, which `package` removes), checks both against the manifest's SHA-256 and the run's commit, builds with `build_update` (`ci/lib.sh`, shared with `ci/release.sh`: app submodule + nested, commit check, ESP-IDF env, `tools/make_update.sh --native`, `trx_pkg_check.py`) and **adds `trx64_update_<commit8>_<YYYYMMDD>.cfw/.ue2` to the same two versions** (`<branch>-<sha8>`, `<branch>-latest`); its `SHA256SUMS` gets their lines (earlier update files of the same commit, e.g. another build date, are removed). A generic package accepts new files later (as `bit-ue2` already does), so no second package. `manifest.json` stays as `package` wrote it (it describes the stage outputs, not the update files). Runs only for `integ-m1`, `main` and tags (`ci/select.sh update`); any other ref prints `update SKIPPED: only integ-m1/main` and is green in seconds. Decided in the script plus a step-level `if:` (no job-level one, decision 2). The pipeline input `update: true` (dispatch) forces it on any branch. File-name hook: `TRX_UPDATE_NAME` replaces the base name in `make_update.sh`, for the release tag later (not built). Placed **before** `bit-ue2`, not parallel: the 100T build takes hours and a parallel job could wait for the runner behind it; the price is that a failing `update` leaves `bit-ue2` skipped. No heavy lock: the updater compile is small. | The testers want the file per commit without a release run; the release workflow keeps building its own from the same function. |
| 11 | **Timeouts**: lint 10 min, sims 120, app 90, bitstream 150, package 10: a stage may wait for the heavy lock first, and the wait counts. The app repo's note that act_runner's reporter fails past about 45 minutes was not seen here: run 351's Vivado job ran 27 minutes without trouble; longer ones are not tested. | Same limits as the single job had. |

## 4. Caches

| What | Key | Where |
|---|---|---|
| Vivado IP and out-of-context checkpoints | hash of sources, settings, part, Vivado version (`tools/vivado_lib.tcl`) | `/cache` (unchanged; the part is added to the key for non-50T parts, T53) |
| App result | submodule commit | `/cache/app/<sha>/` |
| Bitstream and reports | `gateware_key` (above), per board | `/cache/bit/<board>-<key>/` |
| Sims passed | per-bench content key (section 4a) | `/cache/sims-ok/bench/<bench>.<key>` |
| Boot state of a Verilator bench | binary + boot inputs (section 4a) | `/cache/boot-state/<name>-<key>/` |
| App objects | ccache | `/srv/containers/1541ultimate-ci/ccache` (the app CI's volume) |
| Sim build trees | none | each bench builds in the job's work directory; Verilator tier 2 uses ccache in `/cache/ccache-sim` |
| Images | tag | NAS docker |

## 4a. Per-bench cache and boot state

**Per-bench key** (Alex, 2026-10-05: the sims stage took longer than the bitstream because every RTL change reran
every bench, and most benches boot a whole C64 model first). `ci/sims.sh` gives each bench of `ci/sims.txt` its own
key and skips a bench whose key has a PASS marker (`cached PASS <key>` in the log and the summary, 0 s).

- Key = sha256 over: the **content** of the tracked files that `ci/sims.deps` lists for the bench (`files_key` in
  `ci/lib.sh`: the working tree, not commits; submodules by commit), `ci/sims.sh`, the bench's name and command line
  from `ci/sims.txt`, the image tag and the iverilog/GHDL/Verilator versions. Timeouts are not part of it.
- `ci/sims.deps` is an **explicit declaration**: `bench: pathspec ...` (file, directory, `:(glob)`, `:(exclude)`;
  `@name = ...` macros: `@gideon` = the submodule, its patches and the vendoring scripts that make `cores/gideon`,
  `@vcore` = the C64 model of the Verilator benches, `@bt_vhdl`, `@mig`). Tracked files only: `build/` directories and
  generated files never count.
- **A bench without a line keeps the key of the whole tree** (designs, cores, tools, roms, ci, the submodule): any
  change reruns it. So does a bench whose line names a path that matches no tracked file (a typo cannot mean "depends
  on nothing"). Nothing is skipped on a guess.
- The iverilog benches (`ci/sim_iv.sh`) report the files the compiler read (`-M all=`, through `SIM_DEPS_LOG`); a
  file that is not under the bench's deps prints `DEPS INCOMPLETE`, and the PASS is not cached. So a new module that a
  declared file starts to instantiate is caught on the run that first reads it. The ghdl/Verilator benches list their
  sources in their own scripts (which are deps): a new source edits a dep. For all of them
  `ci/sims.sh --check-deps NAME...` runs the bench in a copy that holds only the declared files; a file missing from
  the line fails the bench there. Run it after changing a line, before pushing.
  A new bench in `ci/sims.txt` needs no line to be correct (whole-tree key); a line makes it cheaper.
- Only a PASS writes a marker. A failing or timed-out bench reruns next time. `-f` ignores markers. The newest 3 markers
  of a bench stay. The `heavy_lock` is taken only when at least one bench runs; an all-cached stage ends in seconds
  without waiting for another heavy stage.
- Changing `ci/sims.sh` (it is part of every key: it decides what a PASS is), a bench's command in `ci/sims.txt` or
  the simulator versions reruns the benches concerned once. `ci/lib.sh` and `ci/sim_iv.sh` are not in a key except
  where a line lists them (`sim_iv.sh` for the iverilog benches).
- **The benches that have to run go three at a time** (`-j`, `CI_SIMS_JOBS`, default 3 = the job's `--cpus=3`; the
  longest timeout first, so the slow ones start at once). The summary, the cache markers and the exit status are those
  of a serial run; the log of each bench is printed in list order after the workers finish, a line `--- start/done`
  shows progress. Gideon's VHDL (`cores/gideon`) is generated once before the workers start. `heavy_lock` is taken
  once for the stage as before, so still one heavy stage at a time on the NAS.
- A bench that is slow alone stays slow: `pixlayer` (one iverilog `vvp`, one core, 7 min on the Mac, 24 min on the NAS)
  is the floor of the stage. Under Verilator (`--binary --timing`, same testbench file) it compiles in 5 s but did
  not finish in 19 min of CPU: ps-resolution timing with four unrelated clocks costs more there than in `vvp`, so it
  was not adopted. The remaining option is to split the bench into its frame groups.

**Boot-state cache** (`ci/bootstate.sh`). A bench with a boot phase that saves a state and a run phase that restores it
(`tb_x boot tb.state` / `tb_x run tb.state`; `tb_ef.state` the same) calls the boot through the wrapper:

    ci/bootstate.sh -b ../tb_ring -s ../tb.state -k cores -k roms -k designs/c64_basic/mem -k tools/rom2mem.py \
        ring -- ../tb_ring boot ../tb.state && ../tb_ring trace ../tb.state

The key covers the binary's bytes (the model and the boot code: a state is only restored into the build that wrote
it), the tracked files under every `-k` (what the boot reads at run time: ROMs, `.mem` files, the scripts that make
them), the command line, the state file names and the simulator versions. On a match the stored files are copied and no
boot runs (`/cache/boot-state/<name>-<key>/`, the newest 3 per name stay); otherwise the boot runs and its files are
stored. The run phase is the same program on the same bytes either way. `BOOTSTATE_VERIFY=1` boots anyway after a
hit and compares byte for byte (a fresh boot must equal the stored state); `BOOTSTATE_OFF=1` skips the cache.
When the model changes the binary changes and the state is booted again: the cache pays when the same build is run
again (a bench that failed in its run phase and is rerun, a second bench that boots the same build, a change that
leaves the binary alone), not when the RTL moves. Section 6 lists the benches that need it; none is in `ci/sims.txt`
yet, so none calls the wrapper yet. Measured on the Mac (statebus and pause benches of `designs/c64_basic/sim`,
which have the boot/run split today): `tb_pause` boot 137 s, restored 1 s; `tb_statebus` boot 35 s, restored 1 s; the
state files of a fresh boot and a restored one have the same SHA-1, the `run` output is identical line for line, and
`BOOTSTATE_VERIFY=1` confirms a second fresh boot equals the stored state. A bench joins by putting the line above
in front of its `run` call in its `ci/sims.txt` command; its `ci/sims.deps` line is unchanged (the key of the bench
covers the whole command).

**Bench budget: `ultsys`** (2026-10-06; Alex: 45 minutes and more make no sense). Target: under 10 minutes on the 16-core
builder, a few minutes on a cache hit. Measured on the Mac (`nice -n 15`, one process), before: build 32 s (ccache cold),
boot 1738 s (69.2 M clocks at 40 k clocks/s), run 136 s, total about 31 min + 2 min; after: build 23 s, boot 87 s, run 9 s
(2 min 13 s total, cold; 20 s with the boot state cached). The cost was not clk2x (T29, `d12c084`, which doubled the evals
per clock) but `--public-flat-rw`: it made every internal signal an input that Verilator re-evaluates after each `eval()`
(the `ico` trigger set), 65 % of the run time, 25x. Only the bench's handshake registers (`x_*` of the RISC-V stand-in,
which `tb_ultsys.cpp` reads and writes) carry `/*verilator public_flat_rw*/` now. The model, the READY clock count
(69224566), the test output and the 36 checks are identical; clk2x is still driven at every clock edge (T29 coverage
unchanged). Cached: the boot state, through `ci/bootstate.sh` (key: the model binary's bytes, `cores`, `roms`,
`designs/c64_basic/mem`, `tools/rom2mem.py`): a rerun on an unchanged build skips the boot; a changed model boots again
(87 s). The bench's PASS marker (4a, whole-tree key) is unchanged. Timeout in `ci/sims.txt`: 900 s (was 4800).
Not weakened: every test runs every time. Known and not changed here: the `ULTSYS_NEGATIVE` control run (the bench is
meant to FAIL with the debugger stop hidden from ultimate_sys) passes, before and after this change.

## 5. NAS limits

- One workflow run at a time through group `gateware`; inside a run one job at a time through `needs:` (a
  single chain). Jobs run with `--cpus=3` (the NAS has four and also runs Gitea: never four); Verilator/make use `-j3`, simulators run under `nice -n 15`.
- No tool lives on the NAS file system; `/cache` holds only caches and run hand-over directories.
- The app repo's own CI (its `u64ii` job, 40 min) is a separate repository and not in our group: it can run
  beside us. Not changed here.

## 5a. Cancelling a waiting run

No way with an API token. Checked 2026-10-04 against Gitea 1.27.3's `swagger.v1.json`: under `actions/runs` there
are `GET`, `DELETE /runs/{run}` (refuses a run that is not done), `rerun`, `rerun-failed-jobs` and the workflow
dispatch; no cancel. `POST /repos/.../actions/runs/{id}/cancel` is 404. The cancel button of the web UI is a web
route (`POST /{owner}/{repo}/actions/runs/{run}/cancel`) that needs a login session and a CSRF token, and Gitea
does not accept access tokens on web routes; it was not tried with a session (no password at hand). The working
ways: a newer run of group `gateware` replaces the waiting one (concurrency keeps one waiting run), or Alex
cancels in the UI.

## 6. Later stages (not built)

- **App tests on ue2emu** in a Linux container (`u64-e2e-env:ci`, `--board c64u`): the app repo's CI already runs
  its suites there on every push. A stage here would run the *package* (our bitstream's app image plus the app
  CI's suites against the `ultimate.elf` of the submodule commit). It adds value when the suites learn about
  the TRX64 hooks (T21 monitor, T16 cartridge port); until then it would repeat the app CI.
- **Hardware in the loop at the Mac**: a runner on the Mac (USB-Blaster on the C64U's P5): `tools/load.sh`, boot,
  `LOAD"$"`, Lorenz (`tests/lorenz.py`), `tests/run_cart_tests.sh`. Needs a second runner label (`mac-hil`) and
  a job `needs: package`; the board is shared with Alex's own use, so it would be dispatch-only.
- **Remaining Verilator benches in tier 2** (`cart`, `ring`, `statebus`, `dbg`, `pause`, `dma_io`, `drive`): each
  needs a boot state made first and several `run` calls; they go into `ci/sims.txt` one at a time once their
  command line is fixed. Today tier 2 holds `slot` only.
- **Host firmware** (T26 guests) builds as another job between `app` and `package`.

## 6a. Vivado log and step times

- `build/vivado.log` could not be opened on a fresh checkout: Vivado opens its log before `build.tcl` makes
  `build/` ("CRITICAL WARNING [Common 17-183] Failed to open handle build/vivado.log. Please check access
  permission of directory 'build'", run 339). It is not a user/uid problem: the directory does not exist yet.
  `ci/bitstream.sh` creates it before Vivado starts.
- `ci/bitstream.sh` writes `<design>.steps.txt` (Vivado's own timer per command, elapsed and cpu: create_project,
  synth_design, opt_design, place_design, phys_opt_design, route_design, write_bitstream, the reports), prints
  it in the job log, and the package carries it with `<design>.vivado.log.gz`.

## 7. As built

Files: `.gitea/workflows/pipeline.yml` (top: triggers, group, the chain), `lint.yml`, `sims.yml`, `app.yml`,
`bitstream.yml`, `package.yml`, `update.yml` (fragments); `ci/lib.sh`, `lint.sh`, `sims.sh`, `sims.txt`, `sims.deps`, `bootstate.sh`, `sim_iv.sh`, `app.sh`,
`bitstream.sh`, `package.sh`, `update.sh`, `select.sh`, `submodule.sh`; `ci/sim/Dockerfile` + `build_image.sh` (image
`trx64-sim-env:4`, built on the NAS from the repo: OSS CAD Suite 2026-09-27 on Ubuntu 24.04, g++, make, ccache,
64tass, a RISC-V assembler, shellcheck, ruff, PIL).

**First green run: 356** (t31-ci, 2026-10-04), all six stages. Times, from the Gitea job records:

| Stage | Cold / first | Warm (nothing relevant changed) |
|---|---|---|
| lint | 17 s | 17 s |
| sims, tier 1 (12 benches) | 5 min 9 s (run 332, NAS busy with a Vivado run) | 20 s (skipped: key hit) |
| sims, tier 2 (adds app_i2c, flash_gate, flash_boot, input, slot) | about 35 min, measured by hand on the NAS beside a Vivado run (flash_boot 12 min, input 11 min, slot 10 min, flash_gate 6 min) | 20 s |
| app | 6 min 20 s cold ccache (by hand), 1 min 13 s in CI with ccache | 24-39 s |
| bit-c64u (c64_basic + ddr2_test + hdmi_test) | 26 min 34 s, run 351 (c64_basic 21 min: synth 7:05, place 5:46, route 3:30, opt 0:55, phys_opt 0:47+0:29, bitstream 0:49; the two test designs 3 + 2.7 min) | 22-33 s (key hit) |
| package | 37 s | 24-37 s |
| bit-ue2, not asked for | 14 s (step "bit-ue2 SKIPPED, not requested", banner in the log) | 14 s |
| bit-ue2, asked for (run 362, T53) | 35 min 13 s (28 min of Vivado steps plus the part-specific IP and core, none cached) | cache hit for the same key |
| **whole pipeline** | about 40 min with a cold bitstream | **about 2 min** (7.5 min in run 356, of which 5 min pick-up delay before `package`) |

Benches left out of tier 1 and 2, with the reason in `ci/sims.txt`: `pixlayer` (fails on main: 1 line error, 8 min) and
`bling_io` (fails on main: "flags unlocked: got 86 exp 84"); `cart_equiv`, `lat` (need git history), `lorenz` (needs
the Lorenz suite), `sid` (fetches libresidfp, reports only). Tier 2 holds `slot` as its one Verilator bench; the
others need a boot state made first (section 6).

Found on the way, all fixed in this branch:

- The nested submodules of the app (`lwip`, `httpd`) are needed by the app build; the others (neorv32, ecpprog) are not
  fetched (ecpprog failed once on DNS). `python` comes from ESP-IDF's `export.sh`, not from the image.
- A job-level `if:` leaves the run `waiting` for ever after a failed stage (decision 2).
- A `concurrency` group in the called fragments lets other pipelines slip in between two stages (decision 1).
- A called workflow sees its own `with:` values as `github.event.inputs`, not the caller's dispatch inputs;
  `github.workflow` is the called workflow's name and `GITHUB_EVENT_NAME` is `workflow_call`: the pipeline passes
  `ue2` on as an input and `select.sh` does not look at the event (runs 353, 355, 359 built or skipped the 100T
  by mistake until this was found). The name of a job that calls a workflow is not evaluated (probe repo), so a
  skipped 100T stage cannot say so in its job name; it says so in a step of its own and in a banner in the log.
- `ghdl -e` links against the suite's `libgrt`, built for glibc 2.38: Ubuntu 22.04 cannot link it (24.04 can; its tar
  needs Python to unpack under the NAS seccomp profile).
- Vivado's log could not be opened on a fresh checkout (directory missing); fixed, the log and per-step times are
  in the package.

**Decided (Alex, 2026-10-04): the app stage builds the app itself** from the submodule commit (cached by that commit, ~1 min warm), not fetched from the app repo's CI.

## 8. Builder runner (2026-10-05, branch ci-builder)

A second machine, `builder` (Debian 13, 16 cores, 27 GB, 192.0.2.166), takes the heavy
iteration; afterwards the NAS alone is enough again.

- **Switch, not mix (decided Alex, 2026-10-05):** every stage has
  `runs-on: ${{ vars.CI_RUNNER || 'dockerbuild' }}`. Repo variable `CI_RUNNER=builder` sends all
  stages of a run to the builder (runner label `builder`); variable deleted, the NAS runners
  (`dockerbuild`) take them. Both runners on one label mixed the stages of run 431 (app on the
  NAS, package on the builder, which found nothing in `/cache/runs/431/app`): Gitea hands a job to
  whichever runner's long poll is open, a shorter fetch interval gives no priority.
- **Caches per machine, no NFS (Alex):** the builder has its own `/cache` at the same host path
  `/srv/trx64-build/cache` (seeded once from the NAS). Keys are content hashes, so
  a switch costs at most one rebuild, never a wrong result.
- **CPU limit belongs to the runner:** the workflows carry only `--init`. NAS runners:
  `container.options: --cpus=3`, envs `CI_SIMS_JOBS=3`, `TRX_VIVADO_THREADS=3`. Builder: no limit,
  both envs 16. `tools/vivado_lib.tcl` reads `TRX_VIVADO_THREADS` (default 3).
- **Builder runner:** container `builder-runner` (`~/act-runner/start.sh`, config
  `~/act-runner/data/config.yaml`), repo-scoped to TREX/TRX64-Ultimate, images copied from the NAS
  with `docker save | docker load` (vivado-env:2024.1, trx64-sim-env:4, u64-build-env:ci).
- **Measured (run 431 vs 430):** bitstream c64u 9.2 min on the builder vs 32.7 min on the NAS.

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

T31: Multi-stage CI (Gitea job graph)

Milestone M1, status ✅. Open (one line, as in the TODO table): `ULTSYS_NEGATIVE=1` self-check prints PASS; lorenz/sid/turbo builds still use --public-flat-rw

Decisions: [ADR-0028 One integration branch integ-m1](../adr/0028-d10-one-integration-branch.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- **2026-10-10 stage `update` (branch ci-update-stage):** `.cfw`/`.ue2` per commit on integ-m1/main/tags, added to the package version (decision 14; `ci/update.sh`, `update.yml`, `build_update` in `ci/lib.sh` shared with the release).
- Task text: Multi-stage CI (Gitea 1.26+ job graph): lint -> sims -> app (submodule, C64U/U64-II) -> app tests on ue2emu in a Linux container (--board c64u) -> bitstreams -> package (matching bitstream + app) -> later host firmware and a hardware-in-the-loop runner at the Mac (USB-Blaster: load, boot, LOAD"$", Lorenz); new sim image (Verilator, iverilog, GHDL); jobs in sequence (NAS 2 cores) Next right after M1 (Alex, 2026-10-03): the pipeline as a Gitea workflow of separate jobs (fragments: lint, sims, app build, bitstream, app tests on ue2emu), each reusable and cacheable. Spec: docs/specs/T31-ci-pipeline.md (branch t31-ci; Alex started it 2026-10-03, so it is M1 scope, not part of the RC)
- **2026-10-06: ultsys (tier 2, main only) timed out at 2400 s in run 460 (cold cache, all benches at once; 1728 s in run 448): limit 4800 s.**
- **Tier 2 also on integ-* branches (Alex 2026-10-06), so main never sees a tier-2 bench for the first time (ci/select.sh, T31 decision 5).**
- **2026-10-06 ultsys fast (branch ci-ultsys-fast): bench 31 min -> 2 min (cause `--public-flat-rw`, not clk2x; boot state cached via ci/bootstate.sh; timeout 900 s), see spec section 4a "Bench budget".**
- **Open (2026-10-06): `ULTSYS_NEGATIVE=1` should make ultsys FAIL but prints PASS (same before the speed-up) - the negative self-check of the bench is broken; lorenz/sid/turbo builds still use `--public-flat-rw` (turbo took 1118 s in run 460) - same speed-up likely.**
- **Done 2026-10-04 on branch t31-ci (first green run 356):** pipeline.yml chains lint.yml, sims.yml, app.yml, bitstream.yml, package.yml (each also runnable alone); 100T optional last stage (dispatch `ue2: true`, tag, release/*); bitstream, app and benches reused by content key (app-only change: ~2 min, no Vivado); the ue2emu app tests and the Mac hardware-in-the-loop runner stay specs only (spec section 6)
- **2026-10-18, every stage builds the run's commit:** run 892 (main 9bbae672) failed in "Update files" with "no package
  trx64-ultimate/main-d97a55aa: stage package did not publish it" - the stages cloned the branch head, and docs commits
  pushed during the run moved it. Each workflow that clones the branch now checks out `GITHUB_SHA` after the clone
  (release.yml keeps its explicit `REL_REF`). A push during a run no longer changes what a later stage builds.
