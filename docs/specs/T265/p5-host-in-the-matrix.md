# T265 P5 / S9: the host gateware in the matrix host lane (design, read-only research)

Code read: worktree `.claude/worktrees/t239.3-pkgdiag` at 87d3f85cd (= origin/t239.3); the builder repository
`~/Development/trx64mc-builder` at fb757d7 (local main, not fetched). Paths without a prefix are in the worktree; `builder:` = the builder repository.
Run evidence: run 45814821390 (t239.3, alone), jobs 139891710446 (dispatcher), 139893874683 (collect), 139906141644 (package); releases
`bit-host-5e987948adf57ecf`, `bit-core-null-fe1d6f5b1238136e` of Jondalar/trx64mc-builds.

## 0. The short answer

No GitHub job of either pipeline runs Vivado. The host gateware is built only by the builder (Gitea, trx64mc-builder), for the entries a run's
single `work-order` artifact names. "The old pipeline builds the host" means, in code: on every run except the T239 branch, the old job `plan`
writes that work order (and the old job `approve` holds the vivado click). The matrix has no job for the host bitstream; the host comes along
inside each DFX `bits:<core>` job and inside `collect`. On a feature branch a host-only change orders nothing at all, so no host is built.

## 1. Today: who builds the host gateware, when, where it goes

The build (always the builder):
- builder `.gitea/workflows/pull.yml:6-7` polls every 5 min; `pull/poll.py:99-104` reads the runs of `pipeline.yml` (`pull/config.py:26`).
- `pull/poll.py:133-145` fetches the run's artifact `work-order` (`pull/order.py:43-68`; newest one, `:49-50`); empty list = approve `bitstreams`, build nothing.
- `pull/poll.py:150-185`: recomputes the entry names with the trusted key code (`KEYCODE_REF`, `pull/config.py:30`) over the commit; a work-order name
  not among them = "key drift", rejected.
- `pull/order.py:76-83`: entries host-*, aux-*, core-<ref>-* form lane `host`; core-<id>-* lane `core:<id>`; mono-* lane `large:<id>`.
- `pull/poll.py:247-283`: the host lane is claimed first; a `core:` lane waits until `host-<H>/DONE` exists (`:267-278`); large lanes do not wait;
  at most `cfg.slots` (=2, `pull/config.py:37`) claims at once (`:279-282`).
- `pull/poll.py:359-381` dispatches Gitea `build.yml` (`builder .gitea/workflows/build.yml:52-92`, image vivado-env, `/cache` mounted) ->
  `pull/build.sh:66-86` (fetch the commit, its `ci/vivado.env`, then `ci/child.sh vivado host`).
- `ci/child.sh:53-54` -> `ci/bitstream.sh c64u --lane host`: aux designs ddr2_test/hdmi_test when `aux-<A>` is missing (`ci/bitstream.sh:56-150`,
  `heavy_lock required` `:82`), then `build_layered` (`:151-154`).
- `ci/bitstream_layers.sh:274-305` host part -> `build_host` (`:60-119`): `heavy_lock required` (`:63`), Vivado `host/build.tcl` with TRX_STAGE=host,
  TRX_CORE="" (`:73-75`); WNS < 0 = red (`:89-95`); entry `host-<H>` = c64ref_full.bit, c64ref_partial.bit, static_routed.dcp, host.bit if made,
  reports, manifest.json (`:100-117`). The reference core is not re-implemented: `derive_ref_core` (`:206-222`) makes `core-c64ref-<C>`.
- null (the flash filler, `cores/null/core.toml` flash_filler = true) is a DFX lane of its own: `build_core` (`:128-202`) against
  `host-<H>/static_routed.dcp` (`:139`), AXSS and static-diff checks for the flash filler (`:175-186`) -> `core-null-<C>` with null_full.bit.
- Lock: `ci/lib.sh:328-356` (CI_HEAVY_SLOTS default 2, slot 0 = /cache/heavy.lock shared with TRX64-Ultimate).

Outputs: builder cache `/cache/multicore/bit/<entry>` (`ci/bitstream_layers.sh:244`, `cache_put`); `pull/poll.py:220-242` publishes each entry as
release `bit-<entry>` (no .dcp: `ci/fetch_bits.py:15-16`) and pins a host entry permanently (`:236-237`, `:399-403`); `pull/poll.py:301-303`
approves environment `bitstreams` of the run when every work-order entry is published.

When (the work order):
- Who writes it: the old job `plan` (`pipeline.yml:122-130`, upload `:137-142`) on every run where `plan` runs (`pipeline.yml:63`: not t239.3/t239.3-*);
  the matrix dispatcher only when alone (`matrix.yml:101-124`, `if: !inputs.beside`).
- What it names: `ci/order.py:80-92` - every entry when the children hold package or update (scope full: main, integration, integ-*, release/*,
  v* tags; or update asked); otherwise only host + ref + core for each `core:<id>` target and the mono entry for each `large:<id>`. There is no
  "host" target (`:90-91` raises on anything else). Targets beside = `order_targets` (`ci/orchestrate.py:132-143`, `:249`): the old cor legs plus
  the matrix lanes of `matrix_lanes` (`:119-129`).
- Consequence: a feature-branch push that changes only host gateware has plan host=true, dfx=["null"] (`ci/plan.py:380-391`), null is no `.cor`
  core (`tools/core_toml.py:323`) and no matrix lane -> `order_targets` [] -> empty work order -> the builder builds no host.
  (The shape is the one `ci/test_orchestrate.py:95-99` uses; there tests=true drags the lanes in.)
- The vivado click: `pipeline.yml:145-153` (old) / `matrix.yml:127-135` (alone), skipped for Alex's own push (actor 682272).

Where the host goes afterwards (fetch only, never build):
- old `cor` legs (`pipeline.yml:234-244`, `cor.yml:85` `ci/fetch_bits.sh core:<id>` -> host + ref + core, `ci/fetch_bits.py:89-90`; `ci/pack_one.sh:34-49`).
- old `package` (`pipeline.yml:247-252`): only pull requests and t239.1.
- matrix `bits:<core>` and `collect` (section 2). The old `update` (`pipeline.yml:255-260`, `ci/update.sh:36-67`) repacks the package's null_full.bit
  into .cfw/.ue2.

## 2. Today in the matrix

Host lane = the host MENU only (`matrix.yml:278-334`): 1 lint = leg `host` (`matrix.yml:138-165`, alone only; beside the old lint runs all groups,
`ci/dispatch.py:124`); 2 `ci/lane.sh host unit` = `ci/apptests.sh --tier fast hostapp` (`ci/lane.sh:75`); 6 `ci/lane.sh host build` =
`TRXAPPS_ONLY=host ci/trxapps.sh`, copies hostapp.app/.elf/_emu.elf (`ci/lane.sh:77-80`); 12 = job `host-emu` with vc20's .cor (`matrix.yml:633-708`);
13 artifact `host` (`matrix.yml:328-334`). Steps 3-5, 7-11: empty. No gateware. The summary row "Bitstream host" (`ci/dispatch.py:69-70`) has no job.

DFX core lane (vc20, atari800, ddt_novic: `tools/core_toml.py lanes`): job `bits` (`matrix.yml:352-401`), waits at environment `bitstreams` (`:357`):
3 `ci/lane.sh bits:<id> key` = `fetch_bits --names core:<id>` (`ci/lane.sh:209-212`); 6/11 `ci/fetch_bits.sh core:<id>` (`:214`) - fetches host-<H>,
core-c64ref-<C>, core-<id>-<C>, builds nothing; 7 report WNS of host-* and core-<id>-* (`:216-233`); 13 artifact `bits-<id>` (`matrix.yml:395-401`).
Job `core` downloads it (`matrix.yml:457-460`) and step 11 packs with `ci/pack_one.sh` (`ci/lane.sh:183`). The binding to that host is by
construction: C = sha(H, O) (`ci/lib.sh:304`, `ci/keys.sh:56`), fetch_bits checks every core entry names the host_sha of the host fetched with it
(`ci/fetch_bits.py:16-17`), and the builder routes the core against that host's checkpoint (`ci/bitstream_layers.sh:139`).

Whole package (alone always, `ci/dispatch.py:114`; beside on scope full or update):
- `collect` (`matrix.yml:713-769`, env `bitstreams` `:718`): entry names (`:744-748`, `ci/lane.sh:245-255`), `ci/fetch_bits.sh` all entries (`:755-759`),
  `ci/lane.sh package collect` -> `ci/collect.sh:12-18` runs `ci/bitstream.sh c64u --lane host` / `--lane core:null` with CI_COLLECT_ONLY: c64_basic.bit
  = c64ref_full.bit (`ci/bitstream_layers.sh:357-361`), null_full.bit from core-null (`:352`); artifact `whole-bits`.
- `package` (`matrix.yml:776-832`): `ci/lane.sh package package` (`ci/lane.sh:261-273`) -> `ci/package.sh`: lanes merged (`:69-88`), *.bit copied (`:89`),
  hostapp stamped (`:115-118`), `.txu` = `tools/pack_txu_mc.sh --host null_full.bit --app hostapp.app` (`:288-290`).
- Run 45814821390: work order "0 of 22 needed entries are missing" (dispatcher); collect "cached bit-host-5e987948adf57ecf", "cached
  bit-core-null-fe1d6f5b1238136e"; package: null_full.bit 1,592,533 B, trx64host_update_b3f13bfc.txu 2,747,292 B "host + app". The host entry was not
  built in that run: its release says "built from 572ea62e" (host steps: synth 2:37, place 1:21, route 13:55, about 19 min listed; WNS 0.109 ns).

## 3. The gap and the change

Gap: (a) no matrix job for the host bitstream; (b) beside, the old job `plan` writes the work order and the old `approve` holds the click;
(c) the work order has no host target, so a feature-branch host change builds nothing; (d) the boot ROM: CI never builds `host/bootrom_pkg.vhd`
(hand-made, `tools/build_bootrom.sh:7-8`), yet `host/bootrom` is a key input (`host/layers.txt:48-49`).

Changes (no key function changes: `ci/lib.sh`, `ci/keys.sh` stay as they are, so the builder's trusted key code sees no drift):

1. `ci/order.py:80-92` `needed()`: target `host` -> groups host, aux, core-<ref>, and core-<c> for every flash-filler core (`tools/core_toml.py get <c>
   flash_filler`, as `ci/bitstream_layers.sh:175` reads it; no core name in code). `lane_of` (`:95-101`) unchanged; builder unchanged.
2. `ci/fetch_bits.py:86-98` `select()`: the same `host` target (`--names host`, `host`).
3. `ci/orchestrate.py:132-143` `order_cors(matrix=True)`: prepend `host` when `plan.host or plan.aux`. `matrix=False` (pull request, t239.1) unchanged.
4. `ci/dispatch.py`: new output `host_bits` = package or plan.host or plan.aux; alone, `cor_targets` gets `host` too; the summary row "Bitstream host"
   (`:69-70`) only when host_bits.
5. `ci/lane.sh`: lane `bits:host` before `bits:?*` (`:204-235`; `kind_of` `:35-39` would die on "host"): key = `fetch_bits --names host`; fetch =
   `fetch_bits host`; report = WNS of host-*, aux-*, core-<ref>-*, core-<filler>-* (the awk of `:224-232`).
6. `matrix.yml`: new job `host-bits` "Bitstreams: the host (reference core, flash filler, aux)" - needs [dispatch, approve, sims], `if` host_bits,
   environment `bitstreams`, steps 3 key / 6+11 fetch / 7 report / 13 artifact `bits-host` (as `bits`, `:352-401`). `package` (`:778-779`) and
   `summary` (`:836`) need it.
7. Work order and click beside (makes "the old pipeline builds no host" literal; changes the T239.5 D11 wiring, so Alex decides - open Q1):
   - `pipeline.yml:122-130` and `:137-142`: only `if: github.event_name == 'pull_request' || github.ref == 'refs/heads/t239.1'` (the complement of
     the matrix job's `if`, `:328-329`); then `missing` is empty and `approve` (`:145-153`) is skipped on matrix runs.
   - `matrix.yml:101-110`, `:118-124`: the order step and the `work-order` upload run beside too (the `plan` upload `:111-117` stays alone-only).
     Beside, CHILDREN/COR_TARGETS = the old plan job's `children` and `order_targets` (add `order_targets` to `pipeline.yml:71-78`, pass both as
     workflow_call inputs at `:330-334`), or the dispatcher downloads the artifact `plan` instead of re-running `ci/plan.sh` (`matrix.yml:70-78`) and
     calls `ci/orchestrate.py outputs` itself. Either way one plan and one work order per run (T239.5 S1); upload-artifact refuses a second
     `work-order` in a run, so the two `if`s must stay exact complements. `matrix.yml` approve (`:127-135`) then runs beside too.
   - Without item 7 (variant A): the old `plan` keeps writing the work order and names `host` through item 3; the matrix host lane fetches and reports.
8. Optional: `bits` needs `host-bits` and seeds CI_BIT_CACHE from `bits-host` (upload with `include-hidden-files: true`, else `.releases/` is dropped
   and fetch_bits refetches; `ci/fetch_bits.py:19-21`, `:252`). Gains only an explicit edge; costs up to one poll (5 min), because the builder approves
   only deployments waiting at that moment (`pull/poll.py:406-426`).
9. Boot ROM (gap d): a check in `ci/lint.sh host` that rebuilds `host/bootrom` with the build image's riscv32-unknown-elf-gcc + `mkpkg.py` and compares
   with the committed `host/bootrom_pkg.vhd` (open Q4: same gcc as u64-build-env?).

Not switched off (they read, they do not build): old `cor` legs (apple2 etc.), old `package`/work order on pull requests and t239.1, old `update`
(.cfw/.ue2 repack, open Q3), old `sim` (host benches beside), `prerelease` (publishes the matrix package).

Order (a push that changes host/, beside, variant B):
pipeline `plan` -> matrix `dispatch` (work order: host, aux, core-c64ref, core-null [+ lanes]) -> `approve` (vivado, if needed) -> builder: host lane
(aux + host + c64ref) -> then core:null and every DFX lane, large lanes in parallel, 2 at a time -> publish -> approval of `bitstreams` ->
`host-bits` (key, fetch, report), `bits:<core>`, `collect` -> `core` lanes -> `package` (needs host-bits, collect, sdk, host, core) -> pipeline `update`,
`emu`, `prerelease`. On t239.3 (alone) the same without the old jobs; there only items 1-6 are new.

S9 proof lines: host-bits step 3 "host-<H>", dispatcher "work order: ... missing: host-<H> ...", host-bits step 7 WNS of host, package manifest
`bit-host-<H>` and the .txu; the old jobs show no work-order step.

## 4. Risks

- Keys that move: any host/ file (incl. host/bootrom, host/bootrom_pkg.vhd) moves H (`ci/lib.sh:289-293`), hence every DFX core key C = sha(H, O)
  (`:304`): null, vc20, atari800, apple2, ddt_novic re-implemented, c64ref derived. Large cores do not move (`mono_specs` drops host/*, `:183-190`) -
  S8 holds by key. A change in `cores/common`, `vendor/icepi/hdmi`, `tools/vivado_lib.tcl`, the Gideon vendoring (shared, `host/layers.txt:110-116`)
  moves H, every O (`:300`) and every mono key (`:186`): host + 5 DFX + 14 large. A change to `cores/c64ref` moves H (`:292`).
- Key drift: any change to `ci/lib.sh`/`ci/keys.sh` on the T239 branch makes the builder (trusted ref) reject the run (`pull/poll.py:180-185`); the
  design above changes none.
- Cache: each new host entry is pinned permanently on the builder (`pull/poll.py:399-403`) with its static_routed.dcp; old DFX entries go by cache_gc.
  GitHub cache `bits-<hash>` rolls over (`matrix.yml:749-754`).
- Run time: host Vivado steps about 19 min measured (route 13:55; limit 150, `ci/targets.txt:15`), null steps about 2 min (limit 45 per core); 2
  Vivado at a time, slot 0 shared with TRX64-Ultimate; cores start only after the host is DONE plus a poll (5 min); every hand-off (dispatch,
  publish, approval) waits for a poll. Shared-file changes add 14 large builds (limit 120 each).
- Timing margin: host WNS 0.109 ns today; the RGB encoder (P4) can push it below 0 -> host red -> every DFX core red (`ci/bitstream_layers.sh:315-318`).
- One approval per run: a red large core rejects `bitstreams` for the whole run, so `host-bits` goes red with it (existing semantics).
- Variant B delay: the work order appears after `plan` + the dispatcher (about 4 min in run 45814821390) instead of after `plan`.
- Plan race today: beside, the matrix recomputes the plan (`matrix.yml:70-78`) while the work order comes from the old plan; a different base between
  the two would leave a lane's entries unordered. Using the old plan artifact (item 7) removes it.

## 5. Files to change

`ci/order.py`, `ci/fetch_bits.py`, `ci/orchestrate.py`, `ci/dispatch.py`, `ci/lane.sh`, `.github/workflows/matrix.yml`, `.github/workflows/pipeline.yml`
(item 7 only), tests `ci/test_order.py`, `ci/test_fetch_bits.py`, `ci/test_orchestrate.py`; optional `ci/lint.sh` (+ a bootrom check script);
docs: T239.5 §5 / D11 text, T265 §5, `docs/todo/`. Builder repository: no change.

## 6. Open questions (for the coordinator / Alex)

- Q1: variant B (the matrix owns work order and vivado click on every matrix run; changes the T239.5 D11 wiring) or variant A (old `plan` keeps them,
  matrix only adds the host lane job)?
- Q2: should a host-gateware change on a feature branch make the whole package (all DFX lanes re-bound, .txu), or only the host lane (no .txu)?
- Q3: the .cfw/.ue2 (old `update`) and the pre-release (old `prerelease`) stay old jobs reading the matrix package - enough for "packed and published"?
- Q4: boot ROM: add the regenerate-and-compare check, and is the build image's riscv gcc byte-identical to u64-build-env's?
- Q5: `bits` explicitly after `host-bits` (item 8, up to +5 min) or leave the binding to the content keys?
