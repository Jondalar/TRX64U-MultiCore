# T100 — The host has its own pipeline, and every core has its own

**Status:** PROPOSED (2026-10-14).
**Repo:** TRX64U-MultiCore — `.gitea/workflows/` (the stage files), `ci/lib.sh` (the keys),
`ci/sims.sh` (bench selection and cache), `ci/bitstream_layers.sh` (host and core stages),
`release/` (the set).
**Number:** T100 (registry: the table in `TODO.md`).
**Origin:** Alex, 2026-10-14 at the end of a long evening: "no, let it run now in god's name. But we
really have to decouple this … so one pipeline for HOST + TRX64 core, and then child pipelines for
the cores individually … this is really tiring like that" (translated).
**Depends on:** T94 (the GitOps spec; this is the design for its phases P2 and P3), T87 (the layer
caches), T94 D8 (an experimental core cannot fail the pipeline).
**Milestone:** M3.

---

## §1 The problem

One push runs one chain: `lint → sims → app → bitstream (host + every core) → package → update`.
Everything waits for everything, and work that has nothing to do with each other shares a fate.

What that cost on 2026-10-14, measured:

- **Run 31** died in `sims` on a core bench and took the host bitstream with it, although the host
  does not contain that core. The rule was widened the same evening so an experimental core's bench
  is reported and not fatal (T94 D8) — but that is a plaster on the symptom: the host's build still
  sits behind every core's benches.
- **Run 32** re-ran the whole bench suite, because `ci/sims.sh` is part of every bench's cache key
  and that file changed. The host bitstream that Alex was waiting for sat behind about an hour of
  simulations, none of which touch the host.
- Every push that changes one core rebuilds the pipeline for all of them. Today that is five cores
  plus the host; the core list is meant to grow (Apple II, Spectrum, CPC, NES, SNES).
- The one real coupling is small and already known: the reference core `trx64` is routed *with* the
  host (T87 D8), so a change there is a host change. Everything else is schedule, not substance.

Why it matters: the wait is what makes a board session slip past midnight, and a chain that long
makes people skip runs or push bundles of unrelated changes.

**Parts (MECE):**

- **P1 The host's own pipeline.** Host plus the reference core: its lint, its benches, its app, its
  route — and nothing of any other core.
- **P2 A core's own pipeline.** One core, triggered by a change under its own directory, built
  against an existing host, with its own benches and its own artefacts.
- **P3 The host as a published artefact.** A core's pipeline must be able to take a routed host by
  its key instead of routing one, and must refuse to build against a host that does not exist.
- **P4 What invalidates what.** A change to a tool or a shared file may legitimately invalidate
  everything, but it has to be visible and deliberate, not a side effect of touching one script.
- **P5 The release set.** A release gathers one host and n cores that were built in different runs
  and proves they belong together.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: a core's bench does not delay the host (P1)
  Given a change under host/ only
  When  the push runs
  Then  the host's pipeline runs its own benches and routes the host
  And   no core bench and no core route is part of that run

Scenario: a core change builds one core (P2)
  Given a change under cores/amiga/ only
  When  the push runs
  Then  exactly one core pipeline runs, for amiga, against the current host
  And   the host is not routed and no other core is built

Scenario: a core cannot invent a host (P3)
  Given a core pipeline for a host key that was never published
  When  it starts
  Then  it stops with that key named, before any Vivado run
  And   it says which host it wanted and what exists

Scenario: a new host rebuilds the cores that belong to it (P3)
  Given a host that routed with a new key
  When  the host pipeline finishes
  Then  every non-experimental core is built against that host, each in its own run
  And   each one's pr_verify names that host's checkpoint

Scenario: a tool change is visible, not silent (P4)
  Given a change to a file that is part of every key, such as ci/sims.sh
  When  the push runs
  Then  the run says in one line that it invalidates every bench, and why
  And   the host's route is not part of that cost unless the host's own key moved

Scenario: a release set is provably one set (P5)
  Given a host run and n core runs
  When  the release is assembled
  Then  every .cor carries that host's key and every partial passes pr_verify against it
  And   a core built against another host is refused by name
```

| # | Solves | Scenario | Test (bench / board step) | Where it runs |
|---|---|---|---|---|
| S1 | P1 | core bench does not delay the host | the run's own stage list | CI, observed on a host-only push |
| S2 | P2 | a core change builds one core | the run's own stage list | CI, observed on a core-only push |
| S3 | P3 | no invented host | `ci/` selftest with a key that does not exist | `ci/lint.sh` |
| S4 | P3 | a new host rebuilds its cores | the core runs that follow a host run | CI |
| S5 | P4 | a tool change is visible | the line the stage prints | `ci/lint.sh` + CI |
| S6 | P5 | one provable set | `tools/pack_cores_mc.sh` + `cor_pack.py check --host-key` | release |

The negative that has to bite: S3. A core pipeline that quietly routes its own host when the key is
missing would pass every other scenario here and give us two hosts that look like one.

### Scenarios for D9 (build by change, 2026-10-25)

```gherkin
Scenario: D9-1 a host change builds host + reference and every DFX core, nothing else
  Given the last green run of the branch at commit G
  When a push changes host/top.v
  Then the plan names: host, every DFX core, the apps that link against the host app, their sims
  And no large core is built
  Proof: ci/plan.sh on a fixture repo (diff G..HEAD) prints the target list; negative control: a plan that drops one DFX core fails the check

Scenario: D9-2 a DFX core change builds only that core
  When a push changes cores/vc20/layer2_vc20.v
  Then the plan names: vc20 (partial against the cached host), its .cor, its sims
  And the host stage takes the host from the cache and builds nothing

Scenario: D9-3 a large-core change builds only that core
  When a push changes cores/amiga1200/
  Then the plan names: amiga1200 (flat build), its package, its sims; no host, no DFX core

Scenario: D9-4 an app change rebuilds every consumer
  When a push changes coreapp/src/menuctl.cc
  Then the plan names: the core app, every .cor and every large-core package that carries it (bitstreams from the cache), the app benches

Scenario: D9-5 sims only for what is built, and of that only what changed
  When the plan builds vc20 only
  Then only benches mapped to vc20 (ci/sims.deps) whose key changed run; benches of c64u/trx64/amiga1200-DFX never run

Scenario: D9-6 the diff base is the last green run of the branch
  Given run N red after commit A, then a push of commit B
  Then the plan diffs G..B (G = last green), so A's changes are built too

Scenario: D9-7 the cache is the fallback
  When the plan does not name a target
  Then its artefact comes from the cache; if the cache misses, that target is built and the plan line says "cache miss"

Scenario: D9-8 one target that does not finish does not eat the run
  When a core's route has not finished after its time limit
  Then that core is stopped, reported red, and the stage goes on with the next one
```

## §3 Decision

Open. What has to be decided, with what is already known about each:

- **D1 How the split is expressed in Gitea.** `workflow_call` already works here (the release
  workflow uses it), and path filters already work (`paths-ignore` keeps documentation out). The
  choice is between one dispatcher workflow that calls a host workflow and n core workflows, and
  separate workflows that each trigger on their own paths. What loses, loses on one question: can a
  finished host run start the core runs that belong to it, without a human.
- **D2 Serialisation.** Today one `queue` job keeps pipelines in push order and the heavy lock keeps
  one Vivado at a time. Core runs are independent of each other and could run in parallel on the
  builder; the lock, not the queue, is what has to decide that.
- **D3 What the host publishes.** T94 P2 already says a package `trx64host/<version>`; this spec
  settles what is in it (the routed checkpoint, the key, the contract version, the reports) and how
  long it is kept.
- **D4 Which cores a new host triggers.** All of them, or only the non-experimental ones, or the set
  of the current release. Costs builder time either way; the release set is the one that has to be
  complete.
- **D6 Benches of blocks that are not built.** *(2026-10-15: the premise has a second half now —
  Alex asked for a C64 core that switches these blocks ON, T103. The five benches were deleted
  because nothing we build contains those blocks; with that core they come back, in its key, and
  only if a build without a block makes its bench fail.)* `cores/trx64/core.toml` sets `TRX_FEATURES=jtag`, so
  of the six optional blocks (`reu sampler uci jtag turbo streams`) exactly one is in the bitstream.
  Five benches test the others: `turbo` (its timeout is now 12,000 s), `reu` (1,800), `reu_map`
  (600), `uci` (600) and `udp_audio` (300, the streams). They cost that budget on every run whose
  cache was invalidated, and they prove nothing about what we ship. The decision is what to do: skip
  a bench whose block is off in the core being built (needs a bench-to-block mapping, one line per
  bench in `ci/sims.deps` or a column in `ci/sims.txt`), or leave them to the full line in
  TRX64-Ultimate, where those blocks are built. Found on 2026-10-14, when run 32 spent 40 minutes on
  `turbo` and then failed the host's build with it. Alex: "that should be off".
- **D5 The bench cache: decided 2026-10-14.** `ci/sims.sh` is **out** of the per-bench key and is
  replaced by `driver=<n>` in the version string the key is built from (`ci/sims.sh`, `versions=`).
  The content of the driver does not change what a bench means; what does is how a key is built and
  what a PASS is worth, and that is what the number stands for. It is bumped by hand, with the
  reason in the commit. What it cost before: run 32 re-ran all 46 benches because the driver had
  been edited, spent 40 minutes in one bench's timeout and ended with no artefact.
- **D7 The benches run after the package, decided 2026-10-14.** The chain is
  `lint → app → bitstream → package → update`, and `sims` hangs off `package` instead of sitting in
  front of `app`. A bench produces nothing the package contains; in front of it, it decided whether
  Alex gets a `.cor` at all (run 31 died on a core's bench and took the host bitstream with it).
  The benches still run on every push and a failing bench still makes the run red — the files
  simply exist by then. Both heavy stages take `/cache/heavy.lock` on the one builder, so this is
  an order and not parallelism: measured on run 30, it puts the package about ten minutes earlier,
  and on a run whose bench cache was invalidated, an hour earlier.
- **D7 replaced 2026-10-30: the benches leave the push pipeline (Alex: "yes, please").** Asked: "you do notice that instead of
  coding we have only been fixing builds and pipelines and waiting for about 3-4 days?" (translated). The cause: benches in every
  push run made runs red, the plan's diff base (last green run, D9) grew with each red run, and a merge rule added a second run per
  merge. Decided: (1) a push runs lint, build, package and update; no `sims` stage, so a bench cannot turn a push run red;
  (2) all tier-2 benches run once a night on `main` (scheduled workflow, the per-bench PASS markers skip what did not change) and on
  demand; (3) before a merge into `main` only the benches of the touched core run, on the builder from the Mac
  (`tools/benches.sh`), in minutes, not a pipeline run. The rule "no merge without a green `[sims]` run of the branch" (2026-10-30,
  morning) is withdrawn. Lint before every push stays.

- **D9 Build by change (Alex, 2026-10-25).** "Für die Pipeline - wir brauchen unterschiedliche Builds. Die Action muss schauen, was sich wo geändert hat, und dann dynamisch
  bauen. Host -> ich baue Host+Referenz und alle DFX neu. DFX Core -> ich baue nur den Core neu. Large Single Core -> ich baue nur
  den Core neu. App -> ich baue alle App-Verbraucher neu. Sims -> nur was auch gebaut wird und da nur Changes" ("the pipeline needs
  different builds: the action looks at what changed where and builds dynamically. Host: host + reference and every DFX core.
  DFX core: only that core. Large single core: only that core. App: every consumer of the app. Sims: only what is built, and of
  that only the changes", translated). Today's state against it: the cache keys already give
  host -> host + reference + every DFX core (a core key holds the host key) and DFX core -> that core only; the app stage rebuilds
  host app and core app by key; sims run per bench key (`ci/sims.deps`). Missing: a large-core (mono) stage that builds only the
  changed large core (T115 §7, branch `t97-a1200-large`); "app -> every consumer" for .cor/.cfw packing of a changed core app;
  sims restricted to what this run builds; a time limit per core stage (run 154: one non-routing core used the whole 150 min).
  Refined the same day (Alex: "Nein, ich meine das anders. Statt dem Cache zu vertrauen, will ich dediziert eine Analyse, welche
  Files sich in dem Commit ändern, und eine dynamisch zusammengebaute Pipeline" - "no, I mean it differently: instead of trusting the
  cache I want a dedicated analysis of which files a commit changes, and a pipeline assembled from that", translated): a first job
  diffs the push, maps the changed files to targets (host, each DFX core, each large core, apps, sims) and emits the job list; only
  those jobs run. The cache is the fallback (Alex: "Der Cache ist der Fallback!"): what the analysis does not rebuild comes from it.
  Diff base (Alex, 2026-10-25: "a)"): the last green run of the same branch, so a push of several commits, or one after a red or
  cancelled run, builds everything changed since; not only the previous commit.
- **D9 design (PROPOSED 2026-10-25, to be confirmed by Alex point by point; BUILT 2026-10-25, §5, not yet run on Gitea).** This Gitea (1.27.3) cannot skip jobs with a job-level
  `if:` (run 332: the run hangs "waiting") and has no proven dynamic matrix, so the job graph stays fixed and a new first job decides
  the content:
  1. **`plan`** (first job after `queue`, NAS pool, seconds): finds G = the commit of the last green run of this branch (Gitea API, as
     `ci/wait_turn.sh` reads runs); on no green run, G = merge-base with main, on main the last green main run. Diffs G..HEAD, maps every
     changed path to targets with one table (`ci/targets.txt`: path pattern -> target; host = layers.txt host:/shared: plus the reference
     core; DFX core = its core.toml sources; large core = its descriptor's sources; apps = host-app/, coreapp/, app/; sims = ci/sims.deps),
     expands consumers (host -> every DFX core; app -> every .cor/large package) and writes `plan.json` into the run's handoff directory
     and the job summary.
  2. Every stage reads `plan.json` and builds only its named targets; everything else comes from the cache (fallback). A cache miss of a
     target the plan did not name is built and marked in the summary.
  3. **New stage `large`** (flat builds, `ci/bitstream_mono.sh` from t113-reset / t97-a1200-large) after the DFX stage, same lock.
  4. **A time limit per target** inside the Vivado stages (one core at most N minutes; on expiry it is stopped, red, next target).
  5. `plan.json` also lists the sims to run (D9-5).
  6. **A red target (Alex, 2026-10-25: "A"):** the run is red, but every other target is still built and packed; the run does not
     stop at the first red target. This replaces the `experimental` rule of T94 D8 for failures: an experimental core that fails makes
     the run red too.
  7. **Time limits per target (Alex, 2026-10-25: "Ich denk zu lange. Aber lass mal so starten." - "I think too long, but start like
     that", translated):** DFX core 45 min, large core 120 min, host + reference 150 min; one table in `ci/targets.txt`, tightened from
     measured run times later. The job-level `timeout-minutes` follow from the plan's sum.
  8. **What `large` delivers (Alex, 2026-10-25: "a ist ok, aber ich will das b recht zeitnah bitte" - "a is fine, but I want b soon",
     translated):** a `.cfw` from `tools/pack_cfw_mc.sh` first; the extended `.cor` for large cores (T115 §7) follows soon as its own
     step, with packer, host-app reader and the check-on-add.
- **D10 Think in cores (Alex, 2026-10-25).** On the question whether every bench belongs to exactly one core: "Ja klar ... ich will,
  dass wir in Cores denken, also Host+Ref+Menu; DFX: Core 1 + Menu, Core 2 + Menu; IPROG Full Core: LargeCore 1 + Menu, usw." ("yes of
  course ... I want us to think in cores: host + reference + menu; DFX: core 1 + menu, core 2 + menu; IPROG full core: large core 1 +
  menu, and so on", translated). The unit of the pipeline is a target, not a file or a bench:
  - **Host lane:** host + reference core + host app (the menu).
  - **One lane per DFX core:** that core's partial + its core app (menu) + its .cor.
  - **One lane per large (IPROG, full-bitstream) core:** that core's flat build + its core app (menu) + its .cfw / extended .cor.
  - Every bench belongs to exactly one lane; a red bench makes only its lane red (with D9.6: the run red, the rest still built).
    A bench that today checks shared sources for two cores (`amiga_l2`: the A500 and the A1200 power-up configuration of
    `cores/amiga/rtl`) is split into one bench per core.
  - Shared sources (`cores/amiga/rtl`, `cores/common`) trigger every lane that reads them.
  - **One Gitea job per lane, and a lane runs only on its triggers (Alex, 2026-10-25):** "jeder Core 1 Bahn. Aber NUR laufen, wenn
    sich eines von diesen ändert: bei DFX Cores der Host, die App, der Core selbst" ("every core one lane, but it runs ONLY when one
    of these changes: for DFX cores the host, the app, the core itself", translated). DFX core lane: host, app or the core's own
    sources (shared ones it reads included). Large core lane: app or the core's own sources. Host lane: host sources, reference core
    or host app. This Gitea cannot drop a job (job-level `if:` hangs, run 332), so a lane without a trigger starts, skips every step
    and ends green within seconds; it builds, benches and packs nothing.
- **D11 Lanes in parallel (Alex, 2026-10-25: "Machen." - "do it", on: the order app -> bitstreams -> large is an order, not a
  dependency; the bitstream stage needs nothing from `app`, and `/cache/heavy.lock` lets one heavy stage run per machine; the builder
  has 16 cores and 27 GB, a 50T Vivado run takes about 4-6 GB).**
  - The heavy lock becomes slots: slot 0 is `/cache/heavy.lock` (shared with TRX64-Ultimate as before), further slots are this
    repository's own (`/cache/multicore/heavy.slot<n>`); `CI_HEAVY_SLOTS` (default 2) Vivado runs of this repository at a time.
  - The builder runner takes several jobs at a time (`capacity` 1 -> 3 in its config.yaml).
  - Job graph: `app`, `trxapps`, the host lane and every large-core lane start after `lint`; every DFX lane needs the host lane (it
    routes against the host's static); the package needs every lane; each lane's benches run in its lane after its build.
- **D12 Lint in seconds (Alex, 2026-10-25: "mach die CI schneller und effizient" - "make the CI faster and efficient").** Run 158:
  lint 19 min, 17 of them `tests/coreapp/run.sh` (652 controls, each a full rebuild, one after the other, never cached). The host tests
  of the applications get a key over their inputs (cached PASS as for the benches), their controls run in parallel, and they move
  out of `lint` into the app lane; `lint` keeps the structure checks.
- **D8 Whether a green `main` run publishes its own release.** Decided 2026-10-28 (Alex, translated): "No. Gitea is our
  DEV part here; releases we make by hand and on GitHub, until I want to move the pipeline there." No release from CI.
  Original question: The `.cor` files are in the package
  since today, so what is still done by hand is `tools/make_release_mc.sh` plus
  `tools/publish_release_mc.sh`: the bundle and a Gitea release with a tag. Doing it in the
  pipeline would remove the last person from the path; the cost is a tag on every push to `main`.
  Alex's call, because a release with a tag is his rule (CLAUDE.md, 2026-10-14) and the tag is what
  it costs. Not started.

- **D13 Negative controls cached by content (Alex, 2026-10-29):** "That is something sensible. Do it like that at once
  after the run" (translated). A negative control proves its test can fail; its result changes only when the test or the
  file it mutates changes. Each control is cached by the hash of the test program's sources and the mutated file (with
  its sed); a push re-runs only the controls whose hash changed, the rest is served from the cache like the suites
  (`ci/apptests.sh`). No versions, no nightly run.
  Extended the same day (Alex: "I find testing important, but not senseless testing", translated): the sims stage too -
  each sim row is cached by the hash of its inputs (`ci/sims.deps`, bench, script), independent of the last green run
  (run 1045 re-ran 57 sims because every main run since 1023 was red).
- **D14 App tests and render benches by change, per test (Alex, 2026-10-31).** On run 1090 (trxapps 24m41s: RISC-V build 2:34, host app
  tests 2:31, core app tests 8:12, render benches 10:30) and the question "only what changed (directly) or has an indirect influence has to be
  tested?" (translated): "Hm. And for the render benches too, please" (translated). Each test program of `tests/hostapp`, `tests/coreapp` and
  each render bench is keyed by the content of what it really reads - the compiler's dependency list (`-MMD` `.d`: sources and headers) plus its
  declared runtime inputs (goldens, media, scripts) - and served from the cache when the key is unchanged; its negative controls follow its key
  (D13). A shared header reaches every test that includes it, so indirect influence is covered without a hand-kept list.
- **D15 MultiCore does not build Gideon's application (Alex, 2026-11-01, in chat; translated).** TRX64-Ultimate builds the gateware and
  `ultimate.app` and publishes `trx64-ultimate/<branch>-<sha8>` (contract `trx64u-firmware-handover.md` §1); this pipeline only packs
  from those files. Asked: remove our app stage, and `trx64g.cor` takes bitstream and app from their package as two files; Alex: "Ja bitte"
  ("yes please"). Removed: stage `app` (`app.yml`, `ci/app.sh`, `ci/app_variants.sh` and the `app_ref`/`app_make` descriptor keys, which no core
  used), its job and `needs:` entries in `pipeline.yml` and `ci/lanes.py`, `ultimate.app`/`ultimate.elf`/`space_*.txt` and `app_commit` in the
  package, the tester file `trx64_update_*` in `ci/update.sh` (stage `update` makes `trx64host_update_*` only), `build_update` in `ci/lib.sh`,
  and the monolith release workflow (`release.yml`, `ci/release.sh`, `tools/release_notes.py`; D8 already ruled out CI releases) with
  `release_reuse_skip`. `apps/ultimate` stays: stage `trxapps` builds the host app and core app from parts of it, and a change there is still
  planned (`apps/ultimate -> app:ultimate` in `ci/targets.txt`). `release/imports/trx64g.pin` and `trx64u.pin` moved to
  `trx64-ultimate/main-d71d3928` (bitstream `c64_basic.bit` sha256 unchanged, `ultimate.app` 1 591 760 B).

- **D16 Test once (Alex, 2026-11-01).** After agents ran the whole app suites and render benches (about 55 min per change) and CI's `trxapps` ran
  them again on the push: "Woe if that runs again after the merge! The time use is totally inefficient ... with my teams at work we would not
  deliver at all or produce horrendous runner costs" (translated); asked whether the rule of 2026-10-30 changes to "agent: RISC-V build plus the
  tests of what it touched; CI: the whole check once on the batched push; nothing again after the merge": "Yes!". Agents run `ci/apptests.sh`
  and, when screens change, the render benches against the D14 by-change cache (`/cache/multicore`), so their results are CI's results.

## §4 Out

- The reference core keeps being routed with the host. That is T87 D8 and not a scheduling problem:
  the static image contains it.
- No change to what a stage does — this is about who waits for whom, not about Vivado flows.
- No new runner. The builder stays the one machine with 16 cores and one heavy lock.
- No cross-repository pipelines: TRX64-Ultimate keeps its own.

## §5 Decisions taken while building

**2026-10-14, measured first.** Where the wall clock goes, from the Gitea task list
(`run_started_at` to `updated_at` per stage):

| run | wall | sims | app | bitstreams | package | update |
|---|---|---|---|---|---|---|
| 27 (host routed in full, all cores) | 32 min | 0m08 | 0m10 | **30m27** | 0m58 | 0m03 |
| 28 | 20 min | **9m51** | 0m11 | 7m57 | 0m44 | 0m25 |
| 30 | 16 min | **9m48** | 0m11 | 4m33 | 0m37 | 0m25 |

So a run that is fully cached is 16-20 minutes and half of it is benches that the artefacts do not
need; a run that really routes is half an hour of Vivado. The two things that actually cost a day
are not in this table: a bench failing in front of the bitstream (run 31), and a driver edit
invalidating all 46 keys (run 32, about an hour, then a timeout and no artefact).

- **D7 built** in `.gitea/workflows/pipeline.yml`: `app` needs `lint`, `sims` needs `package`.
  The graph is checked by the `workflow` case of `ci/lint.sh` (green locally, 23 s).
- **D5 built** in `ci/sims.sh`: `declare_specs` no longer appends `ci/sims.sh`, and `versions`
  carries `driver=1`. The bench's own line of `ci/sims.txt` stays in the key through `$cmd`, so
  editing one bench's timeout still invalidates exactly that bench.
- **The `.cor` files are packed by the `package` stage** (`ci/package.sh`), not by a person on the
  Mac after every run. That was the longest wait of all and it was not in CI at all: the pipeline
  went green and Alex then waited for someone to download the package, read the host key out of
  `c64_basic.bit`, pack four files and upload them. Proof it is the same file: packed from package
  `main-8c154e93` with the applications fetched from `trx64-coreapps/1`, `trx64.cor` and `vc20.cor`
  came out **byte-identical** to the ones published in release `trx64mc-main-8c154e93`.
- **The three benches without a line in `ci/sims.deps` got one**, and the fallback turned out to be
  a hole as well as a cost. `ring_core`, `ultsys` and `statebus` took the fallback key over
  `designs cores tools roms ci apps/ultimate`, and **that set contains no `host/`** — so a change
  to `ultimate_sys.vhd` or any host module did not re-run the three benches that compile exactly
  those files, while a change to the Atari re-ran all three. The new `@hostall` line: 440 files
  left the key (atari800 127, amiga 108, c264 31, vc20 26, `designs/`), 430 joined it (`host/` 276,
  `vendor/icepi` 154). `cores/trx64/core.toml` is excluded, because it is the build descriptor and
  nothing these benches compile — it is what re-ran them in run 35.
- **Still to do:** `ci/sims.sh --check-deps ultsys statebus ring_core` on the builder (it runs each
  bench in a copy of the tree that holds only its declared files) — it needs the machine, which was
  routing the host while this was written.

**2026-10-25, D9 built, file by file** (no Vivado ran; every claim below about Gitea is unproven until the first run).

- `ci/targets.txt`: the time limits (host 150, core 45, large 120 min) and the rows that are not derived (apps, `designs`/`constraints` as
  `aux`, bench sources as `simscope:host`, the `expand` rows for consumers). Host = `host/layers.txt` `host:` + `shared:` + the
  reference core's sources; DFX core = its `core.toml` sources; large core = `cores/<id>/mono/core.toml` sources without its `sim/` and
  `mono/reports/` - the sets `ci/lib.sh` hashes, so the plan and the keys cannot disagree about what a core is.
- `ci/plan.py`: `make` (diff base..head by git pathspecs, targets, consumers, sims), `has`, `limit`, `show`. `plan.json` format in its
  docstring: `host`, `aux`, `dfx[]`, `large[]` (named), `apps{}`, `package{cor[], cfw[], fresh[]}`, `sims[]`, `universe`, `limits`, `notes`.
  `package.cor/cfw` list everything the package carries (it is published whole); `fresh` is what the push really moves.
  Sims: a bench with a `ci/sims.deps` line whose inputs (or whose lines in `sims.txt`/`sims.deps`) changed AND that belongs to something
  built (`host/` in its deps and the host named; a core it names, DFX or large, named; an app it reads and the app named) or whose own
  bench source (`host/sim`, `cores/<id>/sim`) changed. Benches of cores without a descriptor (c64u, trx64) belong to nothing.
- `ci/plan.sh`: the base G = head commit of the newest completed run of `pipeline.yml` on this branch with conclusion `success`
  (Gitea API `actions/runs`, as `ci/wait_turn.sh`, 4 pages of 50 filtered by `branch`), not this run; none or not fetchable: merge-base
  with main; main without a green run, or nothing found: `none` = every target is named (never "nothing"). `--stage`: a stage's first
  step writes `$HANDOFF/plan.json` from the input `plan`, or computes it the same way.
- Hand-over (item 2): the `plan` job (NAS pool, after `queue`, full clone) exposes `plan.json` as the **job output** `plan`; `pipeline.yml`
  passes `needs.plan.outputs.plan` as the `plan` input of the called workflows (`with:`). The builder stages cannot read the NAS pool's
  disk and `/cache/multicore/runs/<id>` is the builder's, so a file hand-over from `plan` was not possible; the output is also the only
  thing that survives `package` deleting the run's hand-over directory before `sims` (each stage rewrites `plan.json` from its input).
  **Unproven in 1.27.3:** an output into a reusable workflow's `with:`. If it arrives empty, the stage computes the plan itself (same code,
  same API; a run's base cannot move meanwhile because `queue` lets no newer run finish first), and if that fails it builds everything.
- Stages: `bitstream.yml`/`ci/bitstream.sh` + `ci/bitstream_layers.sh` (host, DFX cores, aux designs): a named target is built on a key
  miss; an unnamed one is taken from the cache and a miss is built and marked in `plan_result.txt` ("cache miss of an unnamed target");
  `large.yml` + `ci/bitstream_mono.sh` (new stage, after `bit-c64u`): the same, except that an unnamed large core with no entry of its
  current key uses the newest entry of that core, announced as "older key" (a host or shared change moves the flat build's key; Alex's
  rule is that a host change builds no large core). `tools/core_toml.py` now reads `mono/core.toml` for `list cfw`/`sources`/`get`/`env`
  (before, nothing could name amiga1200 to the stage). `package.sh` packs the `.cfw` of every large core with `build_update_core`
  (`tools/pack_cfw_mc.sh`, the generic core app of `trxapps`; the stage now has `APP_TOKEN` for the updater's submodule) and publishes
  the flat `.bit`; the extended `.cor` for large cores (D9.8 b) is still open. `sims.sh` takes the plan's benches (none planned: green at
  once; names on the command line win).
- Time limits (item 4): `vivado_limited` in `ci/lib.sh` (`timeout -k 30`, exit code in a file because the output goes through a filter;
  `CI_LIMIT_SECONDS` replaces the minutes, for the tests). On expiry the target is red ("timeout") and the loop goes on. The job
  `timeout-minutes` are static (bitstream 600, large 360, package 30): the sum of a plan cannot be an expression here, and the builder
  runner's own cap (3 h, `pipeline.yml`) still bounds a run that names the host and every core.
- Red targets (D9.6): `mark_red`/`stage_verdict` in `ci/lib.sh`; with `defer_red` (the pipeline passes it) a stage with red targets ends
  green after handing over what it built, so `package`/`large`/`sims` still run, and the new last job `verdict` (`ci/verdict.sh`, needs
  `sims` and `bit-ue2`) turns the run red. A stage run alone fails itself. The `experimental` exemption is gone from the bitstream stages;
  a red host marks every DFX core red with it ("not built: the host is red"). `ci/sims.sh` still reports a failed bench of an
  experimental core without failing (not part of D9.6 as decided; one line to change if Alex wants the same there).
- Tests (all in `ci/lint.sh`, none needs Vivado): `ci/test_plan.sh` D9-1..D9-6 on a scratch clone of the tree (one commit per scenario,
  2-3 controls each that must fail: a plan dropping a DFX core on a host change, adding a core to a core-only change, diffing against the
  previous commit instead of the last green run, ...), `ci/plan.sh --selftest` (base choice on canned API answers),
  `ci/test_layers_flow.sh` (D9-7 hit marked / miss built and marked / control; D9-8 stopped at the limit, next core still built, control
  fast core; D9.6), `ci/test_large_flow.sh` (the same for `large`, and "a host change builds no large core"), the `pipeline graph (D9)`
  check (plan after queue, stages need plan, no job-level `if:`).
- Not covered / open: benches `amiga_audio`, `atari_machine`, `c64u_sock` have no `ci/sims.deps` line and are never planned (listed in the
  plan's `notes`); an `apps/ultimate` bump names only the app consumers - if it moves `vendor/gideon` the host key moves and the stage
  builds the host as an unnamed cache miss, marked; `package.sh`'s `.cfw` step and the `verdict` job have no fixture (they need the
  updater image / a runner); a change of `ci/` scripts or workflows names no target (the keys decide).

**2026-10-25, fix after Gitea run 159:** `ci/test_plan.sh` D9-6 (no green run -> merge-base with main) failed in the lint job with "the clone
has no origin/main": the lint checkout has no such ref. The scenario now builds its own `main` in the scratch repository (`origin/main` and
`main` at the branch point, no remote) and checks that the plan's base is exactly that commit; it no longer depends on the refs of the
checkout it runs in.

**2026-10-25, D11 as built, heavy lock as slots.** `heavy_lock` in `ci/lib.sh`: slot 0 is `/cache/heavy.lock` (TRX64-Ultimate takes the same file,
one holder, unchanged), further slots `/cache/multicore/heavy.slot<n>`; `CI_HEAVY_SLOTS` (default 2) Vivado runs of this repository at a time.
The slots are tried without waiting, in turn; when all are taken the caller polls every `CI_HEAVY_POLL` s (5) until one is free. The slot held is
printed ("heavy lock held: slot 1 of 2"). It is idempotent per script (a second call keeps the slot; the old code reopened fd 9, which dropped
and retook the lock). With slot 0 busy in the other repository this repository still gets slot 1, so the two repositories can now run two or
three Vivado runs together on the 16-core builder - that is the decision of D11, not a side effect. NFS: the slot files are on the same volume as
`heavy.lock`, flock is a POSIX lock on the NAS; builder jobs see each other, a NAS-local process does not see a builder lock (as before).
`ci/test_heavy_slots.sh`: two holders at once on different slots, the third waits and takes the freed slot, one slot serialises, slot 0 held by
another process leaves slot 1 (a flock shim in Python where the machine has none, macOS).

**2026-10-25, D10 as built, one lane per core** (no Vivado ran; nothing below about Gitea is proven until the first run).

- **Job graph** (`.gitea/workflows/pipeline.yml`, the lane block is generated by `ci/lanes.py`, 22 jobs): `queue` -> `plan`, `lint`; `app`,
  `trxapps` <- lint; `bit-host` ("host + c64ref") <- lint, plan (no `app`); `bit-<core>` per DFX core (10 today: null, vc20, amiga, atari800,
  c264, nes, apple2, zx, cpc, zxnext) <- bit-host, plan; `large-amiga1200` ("amiga1200 (large)") <- lint, plan; `package` <- app, trxapps, plan
  and every lane; `sims` <- package; `update` <- package; `bit-ue2` <- update; `verdict` <- sims, bit-ue2. The old jobs `bit-c64u` and
  `large` are gone from the pipeline; `bitstream.yml` / `large.yml` take the input `lane` (empty = the whole stage, still used by `release.yml`
  and by bit-ue2). Job names are quoted strings (`"null"` is a core; unquoted it is YAML null).
- **A lane that did not fire still starts.** This Gitea cannot drop a job (run 332), and the package is published whole and needs every
  lane's files, so an idle lane does not "do nothing": it takes the files of its cache entry (seconds, no Vivado) and says "not in the plan
  (no trigger fired)". A cache miss of an idle lane is built and marked, as for every unnamed target (D9.2). This is the one place where the
  decision text "skips every step" is read as "builds nothing it can take from the cache"; the alternative (the package computing every lane's
  key itself) would copy the key logic into a second script.
- **Triggers** (`ci/plan.py` `lanes_of`, plan.json `lanes{host, dfx[], large[]}`): host lane <- host sources incl. `shared:` and the reference
  core, the aux designs' files (`designs/`, `constraints/`), the host app; DFX lane <- the host (so also the reference core), any app, the core's
  own sources (its `core.toml` sources, shared dirs included); large lane <- the host (T133, 2026-10-27: the flat build reads the host sources), any app, the core's own sources. `plan.py has <plan> lane:host |
  lane:dfx:<id> | lane:large:<id>`; a plan without `lanes` (older, hand-made) reads the named targets as the triggers.
- **Hand-over and package.** Each lane has its own stage directory (`bit-host`, `bit-<core>`, `large-<core>`): one writer per directory, because
  `stage_dir` empties the directory it is given and lanes run at the same time. `ci/package.sh` merges them (`merge_lanes`) into one directory with
  the layout the rest of the script already reads - the host lane's `key` / `from`, every lane's files, `wns.txt` / `plan_result.txt` joined,
  `layers.json` merged (the host lane writes `host` + the reference core, a core lane only its core) - so nothing downstream changed. A lane
  directory without `DONE` is a red target `lane-<id>` of the package; no host lane directory = no package. A whole-stage run (`bit-c64u`,
  `bit-mono`) is used as before. Red targets are filed under the lane's stage name (`bit-<core>.<core>`); `verdict` reads all of them.
- **Cache and parallel lanes.** A core lane reads the host entry the host lane wrote (`host-<H>` in the cache, pinned by the host lane); no entry
  = the host lane failed and the core is red with it ("not built: the host is red"), nothing is built. Only the host lane (and a whole-stage
  run) runs the cache janitor, so lanes never run it twice at once. Open risks on a real run: the out-of-context checkpoints in `$TRX_CACHE` are
  shared by parallel core lanes (one file per core, none written by two lanes); 12 lane jobs + 10 others; the package has 15 `needs`.
- **Benches owned by one lane** (`ci/sims.lane`, one row per bench, checked by `ci/lanes.py check` against `ci/sims.txt` and `ci/sims.deps`).
  Owner by what the script runs, not by name: `amiga_cfg`, `amiga_cpu020`, `amiga_fastpat`, `amiga_fbwin` build the A1200 (TG68K, AGA) and belong
  to amiga1200 (the fx68k / shared-file runs in them are negative controls). `amiga_l2`, `amiga_sdc`, `amiga_dskchg` ran both cores: each
  script takes `a500|a1200|all` (same script, `ci/sims.txt` passes the argument), the A1200 half is the new `amiga1200_l2`, `amiga1200_sdc`,
  `amiga1200_dskchg` (the A1200 audio/disk/IDE/dskchg runs, the A1200 `amiga_sdc` copy and its mount control, the A1200 `paula_floppy` copy);
  the a1200 audio run got its own `OUT_SUFFIX`, because benches run in parallel in one tree. Table: host 37 benches; core:zxnext 17, apple2 15,
  amiga 11, zx 8, cpc 6, atari800 6, c264 4, nes 3, vc20 2, null 1; large:amiga1200 10; `none` 3 (`c64u_cw`, `c64u_pktport`, `c64u_sock`:
  `cores/c64u` is a source tree that is never built, T115 - these are never planned; **open for Alex: they own no lane**). The three benches
  without a deps line got one (`amiga_audio`, `atari_machine` = the Atari tree `@a8`, `c64u_sock`). `ci/sims.sh` makes a failed bench soft only when
  its owner lane is an experimental core (before: every core its deps line named), `ci/plan.py` plans a bench when its inputs changed and its owner
  lane fires (or its own bench source changed). Not run here: the Verilator halves of `amiga_l2` (only `sh -n`; the iverilog halves `amiga_sdc`
  and `amiga_dskchg` were run for a500, a1200 and all).
- **`ci/lint.sh`** (touched, two hunks): the `pipeline graph (D9)` check no longer names `bit-c64u` / `large` (the lanes are checked by
  `ci/lanes.py check`), and one check line runs `ci/test_lanes.sh`; `ci/test_heavy_slots.sh` runs from `ci/test_layers_flow.sh`.
- Tests (no Vivado, no GHDL): `ci/test_lanes.sh` (tree passes; controls: a core without a lane job, a lane job without a core, an edited block, a
  package that forgets a lane, a bench without an owner, with two, an owner that is no lane, an owner the deps do not read), `ci/test_plan.sh`
  D10-1..8 (13 controls), `ci/test_layers_flow.sh` D10 (host lane, core lane, idle lane, no host entry = red, bad lanes, `merge_lanes`),
  `ci/test_large_flow.sh` (the large lane), `ci/test_heavy_slots.sh`.
**2026-10-25, D12 as built (measured on the builder, container `u64-build-env:ci`, `nice -n 15`, 16 cores, JOBS=14, scratch copy of the tree).**
Before (Gitea run 158): `tests/coreapp/run.sh` 17 min inside `lint` (19 min), 652 controls one after the other, every push.
- `tests/parallel.sh` (sourced by `tests/coreapp/run.sh` and `tests/loader/run.sh`): a control is a job in its own directory (`$JOBDIR`, nothing
  shared; the golden files are only read, `UPDATE_GOLDEN` is still removed for a control); `JOBS` defaults to cores - 2 (min 1; `nproc`, else
  `sysctl -n hw.ncpu`). Output is printed in submission order, the first failing job stops the suite and kills the rest. All three verdicts of a
  control are kept and were each shown to fail the suite on the builder with a planted control: sed changes nothing (vacuous), test passes
  against the mutant, mutant crashes without a red check. The plain `run` tests stay sequential (49, a few seconds each); `tests/hostapp/run.sh`
  stays as it was (its 22 controls take 13 s in all).
- Measured: `coreapp` 152 s and 180 s (two runs), `hostapp` 13 s, `loader` 3-4 s (27 controls); all three 2 min 49 s wall. The loader bench
  is now the only suite whose controls are fewer than JOBS and so runs in the time of its slowest compile.
- `ci/apptests.sh` (new): per-suite key = `files_key` over the application trees (`coreapp`, `app`, `host-app/software/{hostapp,io,lvgl_port}`, the
  pin of `apps/ultimate`), the python/shell tools the run.sh files call, `tests/parallel.sh`, the suite's own `tests/<suite>` (`loader` also
  `tests/coreapp`, whose headers it includes), the compiler's version line and a driver number. Marker `$CI_APPTESTS_OK/<suite>.<key>`
  (default `/cache/multicore/apptests-ok`, the three newest kept) written only by a PASS; unchanged key prints `cached PASS <key> (<commit> <date>)`;
  `-f` / `CI_APPTESTS_FORCE=1` runs anyway. Shown on the builder: second run of `loader` is "cached PASS". The key is by directories, not by what a
  compile read: a change anywhere in `coreapp/` or `app/` re-runs all three suites (about 3 min), a change of a tools file not in the list does not.
  If a new tool or tree is added to a run.sh and not to `common` in `ci/apptests.sh`, the cache will serve a stale PASS: that is the price of a
  declared list (the benches' `ci/sims.deps` has the same property).
- Moved: `ci/lint.sh` lost the three `run.sh` calls (it keeps the structure checks and a new `ci/test_apptests.sh`); `ci/trxapps.sh` runs
  `ci/apptests.sh` before the render benches, also on an image cache hit (the key decides, not the image); `u64-build-env:ci` has c++ 11.4 and
  python 3.10. A failing suite fails the stage `trxapps`, as it failed `lint` before.
- `ci/test_apptests.sh` (in `ci/lint.sh`, no compiler): same tree same key; a changed `coreapp/src` file moves the `coreapp` and `loader` keys;
  `tests/coreapp` moves `coreapp` and not `hostapp`; stub suites: PASS cached, `-f` and a changed source run again, a failing suite leaves no
  marker; fake controls: stable order, a failing job fails the suite and later output is not printed, four 1 s jobs at JOBS=4 take <= 2 s while
  the control (JOBS=1) takes >= 4 s.
- Not run: the full pipeline on Gitea (the first `trxapps` run on a cold marker dir pays the 3 minutes once); `hostapp`'s FatFS bench (needs
  the `apps/ultimate` checkout, not in the scratch copy: it printed its SKIPPED line); `lint` itself.

**2026-10-30, D7 replaced - built.** `pipeline.yml` has no `sims` job; `verdict` waits for `bit-ue2` only. `ci/lanes.py check` and the lint
step "pipeline graph" now require that there is no `sims` job and that `nightly.yml` calls `sims.yml` (`ci/test_lanes.sh` copies
`nightly.yml` into its scratch tree). `.gitea/workflows/nightly.yml`: cron `0 1 * * *` (UTC, runs on the default branch `main`) +
`workflow_dispatch`, calls `sims.yml` with `tier: 2` and the new input `all: 'true'`, which skips the step "Plan" so no `plan.json` exists and
`ci/sims.sh` considers every tier-2 bench; the PASS markers in `/cache/sims-ok` skip the unchanged ones. No `defer_red`: a red bench is a red
nightly, reported, nothing else. Checked: no bench script of `ci/sims.txt` reads `$HANDOFF`, `/cache/multicore/runs` or a package file (only
`ci/sims.sh` itself reads `$HANDOFF/plan.json`). `tools/benches.sh <core-id|bench>... [-f] [-n]`: resolves a core id by `ci/sims.lane`
(`host`, `core:<id>`, `large:<id>`), bundles HEAD, runs `ci/sims.sh -t 2 -j 8` on the builder in `trx64-sim-env:4` with the builder's `/cache`
mounted (markers shared with the nightly), prints the summary and `benches: ok` / `benches: FAILED, log <path>`; `-n` prints the bench list only.
Not run yet: the nightly on Gitea (first scheduled run), `tools/benches.sh` on the builder (busy; only `-n` was exercised).
- **Cache janitor does not wait (2026-10-30, run 260).** A bit stage of only cache hits took a heavy slot at its end just to run `ci/cache_gc.sh` and
  sat behind another session's Vivado ("waiting for a free heavy slot" right after "cache hit"). Now `cache_gc_if_free` (`ci/lib.sh`) runs the janitor only
  when a slot is free at once (or the stage already holds one because it built); otherwise it prints "cache_gc skipped" and a later run cleans up.
- **First nightly (Gitea run 1086, 2026-10-31): two script errors, the rest was load.** Report lines of the run: `done apple2_video_flip: FAIL (exit 1), 53s`
  with `undefined reference to work__a2_ram__...`, and `done hdmigeom: FAIL (exit 1), 0s` with `unit "vc20_pixw" not found in library "work"`.
  (1) `cores/apple2/sim/run_video.sh` gave all four `apple2_video*` benches the one directory `build/video`; run in parallel they overwrite each
  other's GHDL library, so a link found half a library (and the others timed out or failed). Now one directory per part (`build/video_<PROGS>_<EXTRA>`).
  After: `apple2_video PASS 1078s`, `apple2_video_text PASS 663s`, `apple2_video_flip PASS 852s`, `apple2_video_ctl PASS 872s` (`tools/benches.sh`, the four plus hdmigeom together at -j 8 on a loaded builder).
  (2) `cores/vc20/sim/run_hdmigeom.sh` listed the VC-20 core without `vc20_pixw.vhd`, which `vc20_core` has instantiated since the pixel writer; added in the same order as `run.sh`. After: `hdmigeom PASS 528s`.
  (3) `hdmi_timing`: `done hdmi_timing: FAIL (exit 1), 1501s` is the script's own `timeout 1500` on the all-modes `vvp` (no `PASS` line then); the RTL is fine: alone, `-f`, `hdmi_timing PASS 250s`. Load, not a regression.
  Timeouts, alone on the builder (`-f`, load average 4 to 7 from other sessions, so these are upper bounds): `flash_boot PASS 283s` (limit 900, nightly TIMEOUT 900s), `ultsys PASS 259s` (900),
  `input PASS 251s` (1200), `apple2_keys PASS 1053s` (2400). The first three need about a quarter of their limit; the nightly ran the builder's 16 cores with 3 to 4 agent sessions' builds and benches beside it
  and every bench took 3 to 4 times as long. Proposal (not done, scheduling is Alex's): the nightly (01:00 UTC) takes the heavy slots, and agents' app builds and `tools/benches.sh` wait for it to finish
  (`/cache/heavy.lock` held by the nightly for the whole run), or the nightly runs with `-j` lower than the free cores and the limits are doubled. Open: the bench limit is wall time, so any shared builder run is a false red.
  Found while measuring: `tools/benches.sh` cannot run host benches that need `vendor/gideon` (`flash_boot`, `ultsys`, `input`): the container has no `apps/ultimate` submodule (`cannot run ssh`, "gideon_sources.sh failed", 0 s). Measured with a copy of the script that ships the main checkout's `vendor/gideon`; the script itself is not changed here.

### D14 built: app tests and render benches as cached units (2026-10-31)

**Step 1, what was cached before D14, measured on the builder** (`u64-build-env:ci`, private cache, tree of commit 76817bbe2; old code, `ci/apptests.sh -f hostapp coreapp`):

| old code | hostapp | coreapp | controls from the cache |
|---|---|---|---|
| cold cache | 147 s | 564 s | 0 of 1331 |
| warm, tree unchanged | 135 s | 438 s | 497 of 1331 |
| warm, `// touch` appended to `coreapp/src/rawkeys.cc` | 121 s | 511 s | 202 of 1331 |

What the old code cached, and why that is so little:
- **Suite level** (`ci/apptests.sh:44-47`, `suite_key`): one key per suite over `common` (all of `coreapp`, `app`, `host-app/...`, `apps/ultimate`, the tools) plus the suite's own tests directory. Any change anywhere in those trees runs the whole suite script again. The marker is all or nothing.
- **Test programs** (`tests/coreapp/run.sh:41`, `run()`; `tests/hostapp/run.sh` lines 102-110, 135-139, ...): no key at all. Every one of the 68 `run` calls of coreapp, every compile-and-run pair of hostapp, the python checks, the seven `run_*.sh` sub-scripts and every ad-hoc control (`vm_always1080`, `web_*`, `rpt_*`, `ntp_*`, `cm_*`, `uires_*`, `sdcrc`, `fatexpand`, ...) run on every suite run.
- **Controls** (D13, `tests/ctlcache.sh`, `tests/ctlcache.py`): keyed from the compiler's `-MM` list, but (1) the key names every file by its absolute path (`ctlcache.py:84`, `rel = f[len(root)+1:] if f.startswith(root + "/") else f`); a file in the suite's scratch directory (`mktemp -d`, a new name each run) is therefore a new key each run. That is every control with arguments in `$T` (`CARGS="$BUN/..."`, coreapp lines 1315, 1353, 1395) and, since `export ZX_CORPUS="$ZXC"` (coreapp line 673, `ctlcache.py:77`) puts `$T/zxcorpus` in the key of every control that follows it, most of the suite: 834 of 1331 controls re-ran with nothing changed. (2) `ctlcache.py:68-76` adds to every key all data files of the suite directory, all `tools/*.py` and the product trees a few tests scan (`RUNTIME_READS`), so a golden or a tool script re-runs every control of the suite. (3) Only 1060 of the controls are cached; the ad-hoc ones above are not.
- **Why run 1090 re-ran controls of unrelated tests**: not the descriptor lists. A control's `-MM` list is the test's real dependency list, and it was right: `BASEL` (coreapp line 22), the base every core application links, contains `rawkeys.cc`, and the 162 controls (plus their test programs) that link it do include `rawkeys.h`/`rawkeys.cc`. So a change to `rawkeys.cc` legitimately re-runs those; the old code re-ran 1129 controls after it (the 834 of the unstable key and the ones that really compile it). The new numbers are in the measurement below.
- **Render benches**: no key at all (`tests/view/run.sh`, `tests/coreapp/render/run.sh`, `tests/hostapp/render/run.sh`: build and run on every trxapps run, 54 builds in the host app bench alone). Only LVGL's own objects are cached (`tests/lvgl_objs.sh`).

**Built.** One mechanism for all of it, `tests/ctlcache.sh` + `tests/ctlcache.py` + `tests/readlog.c`:
- A **unit** is a test program, a negative control or a render bench. Its cache entry (`$CONTROLS_CACHE/<key>`, under `/cache/multicore/apptests-ok/controls`) is valid when two things hold. **The key** is the hash of the unit's id (name, sed, sources, flags, compiler line), of the content of every file in the compiler's `-MM` dependency list of its unmutated build (test, sources, every header - a shared header reaches every unit that includes it, no map) and of the files of its run arguments; files are named relative to the tree (`<T>/...` under the scratch directory), never by the random name. **The manifest** is every file, directory listing and missing path under the tree that the run opened or looked at, recorded by `tests/readlog.c` (an `LD_PRELOAD` that logs `open`/`stat`/`access`/`opendir`/`fopen`) while the unit passed, with the content hash of each; the entry stores it and a hit needs all of it unchanged. A golden, a fixture, a python tool and what it imports, a product source a test scans, a scenario file, LVGL's headers are found by the recorder, nothing is declared by hand, and a file nobody thought of is found too.
- A test (or a control that failed as it must) is stored only on its pass; a unit whose key cannot be made (no `.d`) runs; where the recorder cannot run (macOS) the old coarse key is used (all suite data files and `tools/*.py` in the key) and shell-function units run every time.
- `cc_block NAME FN` makes a shell function a unit (key = its text; the manifest is recorded through the compiler and every child process, so headers need no list): used for the hostapp compile-and-run pairs, the ad-hoc controls, the `run_*.sh` sub-scripts and every render bench and control.
- Log lines: `test NAME: cached PASS <key>` / `test NAME: PASS <n>s`, `control NAME: cached (fails as it must) <key>` / `control NAME: fails as it must (...)`, `test NAME: reads changed (<path>), runs again`, and one summary line per suite, `units (dyn): ran N (tests a, controls b), from the cache M (...)`.
- Render benches: the LVGL objects live outside the tree, so their key (`tests/lvgl_objs.sh --key`: the pinned commit and `lv_conf.h`) is part of every render unit's key; the host app bench builds each control only when its control runs (a cached control is not compiled).
- `ci/apptests.sh` keeps its suite-level key as the cheap first filter (driver 2); inside a suite that has to run, the units decide.

**Selftest** `tests/ctlcache_selftest.sh` (in `ci/lint.sh`, and still the first step of `tests/hostapp/run.sh`): a throw-away project (two tests sharing a header, a shell-function test, a render-like bench with an LVGL-like header, scenario and golden, two controls) run through the real helpers; scenarios d (no change: all cached, `ran 0`), a (a line only X reads: X and its control run, the rest cached), b (shared header: every includer runs), c (a golden / scenario / header of one unit: that unit only), e (a file opened by a relative path nobody declared: found), f (a failing test is not stored), g (no `.d`: no key). Negative controls of the cache itself: a key that ignores file content, a manifest that records no reads, a verify that always agrees and a key that ignores the unit's id each make the same scenarios go red.

**Heavy slot during trxapps (coordinator's question, Alex 2026-10-31: "That the core needs 15 min in Vivado and the simple app 25+ is wrong in itself").** Measured in the log of run 1090 (job 10954): `heavy lock held: slot 0 of 2 (/cache/heavy.lock)` at 05:27:05, `=== trxapps done in 24m41s` at 05:51:46. `ci/trxapps.sh:48` calls `heavy_lock` once, in the branch that builds the RISC-V image; `lib.sh` `heavy_lock` opens the lock on fd 9 and nothing closes it, so the slot is held until the script exits: 24m40s, of which the RISC-V build (needs the slot: the shared toolchain/ccache and 16 cores) is 2m34s (05:27:05 to 05:29:39) and the remaining 22 minutes are host tests (2m31s + 8m12s + 9s), render benches (10m30s) and the fs64 benches (45s), none of which needs a slot. With `CI_HEAVY_SLOTS=2` this takes one of two Vivado slots for 22 minutes with nothing heavy running. **Proposal, not done (the slot policy is not changed here):** release the slot right after the RISC-V build (`exec 9>&-; unset HEAVY_SLOT` after the `cp` of the artefacts, before `ci/apptests.sh`); the tests then run nice 15 beside Vivado as they do on a developer machine. With D14 a typical app change (one core's app file) re-runs only the units that compile it, so the job without the build is minutes, not 22.

**Measured with D14 on the builder** (`ci/trxapps.sh` in `u64-build-env:ci`, private cache, builder load average 13-16 from other sessions, so all times are upper bounds; the RISC-V build was served by a warm ccache; same tree as above):

| `ci/trxapps.sh` | host tests (3 suites) | render benches | whole stage | units that ran |
|---|---|---|---|---|
| cold (empty test cache) | 715 s | 670 s | 23m55s | all: 142 + 1136 + 41 tests/controls, 1 + 7 + 84 render units |
| warm, nothing changed | 0 s (suite keys) / 126 s with `-f` | 18 s | 0m59s | 0 |
| warm, `// touch` on `coreapp/src/rawkeys.cc` | 545 s | 99 s | 11m26s | coreapp 901 of 1136 (850 controls, 51 tests), host app render 7 of 91 |
| (apptests only) `// touch` on `coreapp/cores/cpc.cc` | 228 s with `-f` | - | - | coreapp 175 of 1136 |

The old code on the same tree: cold 711 s (hostapp 147 + coreapp 564), warm 573 s, after `rawkeys.cc` 632 s. **A few minutes is reached for a file that few tests link (`cpc.cc`: 3.8 min under load, 175 units), not for `rawkeys.cc`: it is in `BASEL`, which nearly every coreapp test and control links, so 80 % of the units really compile it.** Further narrowing needs the test link lists to shrink (archive-style linking of only the referenced objects) or an early cut-off by the hash of the built test binary (a comment-only change then costs the compiles only); neither is built. The cold run is slower than the old cold run because of the recording (about 10 %) and the load.

**Found and fixed:** the control `t137_key_for_any_file` of the host app render bench was red only because of the dirt earlier runs left in the shared tree (`/Usb0/cores` existing); on the tree as `mktree.py` makes it the mutant passed. Every unit now starts from a pristine copy of the trees (`CC_PRE=reset_trees`) and the bench opens the page with `hostui_test_fm_keep` and checks `open=0` after P on a non-firmware row (`render.cc` scenarios_t137).

**Open:** (1) early cut-off by binary hash / smaller link lists for `rawkeys.cc`-like files; (2) the heavy slot (above) is not changed; (3) `tools/prepush.sh` run and a Gitea run have not been done by this branch; (4) the selftest takes about 35-55 s under load - trim the scenario list if lint time matters; (5) sub-scripts (`run_fmt.sh`, `run_fwconvert.sh`, ...) are one unit each, not one per test; (6) macOS keeps the coarse key and runs shell-function units every time.
