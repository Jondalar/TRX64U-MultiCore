# T247 — Every test that runs in ue2emu is a Gherkin scenario, readable for contributors, and runs on every pull request

**Status:** PROPOSED (2026-11-20). Order (Alex): after the T239 epic.
Open (one line, as in the TODO table): PROPOSED 11-20 (Alex), after T239; questions Q1-Q4 open
**Repo:** TRX64U-MultiCore — the emulator tests under `tests/` and their CI job (`.github/workflows/emu.yml`, `ci/children.txt`).
No product code.
**Number:** T247.
**Origin:** Alex, 2026-11-20 (translated), on the tests a pull request runs: "We could also add the emu tests and require them
... those are Gherkin BDD tests, aren't they?" Then: "That is what I meant: everything that runs in the emulator makes more
sense as a scenario, also for potential contributors ... that is much easier to understand than bash. Spec for it please, after
the 239."
**Depends on:** T153 (the Gherkin suite `tests/ui` and its runner), T174 D4 and T239.1 (the pipeline's children on GitHub),
T245 (the update paths as features; its Q1 and Q2 are the same questions for the updater).
**Milestone:** M2.

---

## §1 The problem

- **Who:**
  - A contributor who reads a red emulator test on a pull request and must find out what it wanted.
  - Alex, who reviews what a test proves.
- **What happens today:** two formats for the same level of test.
  - **Gherkin:** `tests/ui` (T153) has 53 feature files (trx64u 9, A1200 14, other cores 27, NES 3) on a Python runner of
    1,294 lines. It runs by hand on the Mac only (`~/Development/u64-emulator/target/release/ue2emu`), not in the pipeline.
  - **Scripts:** the pipeline's `emu` job runs `tests/hostapp/emu_host.sh` (300 lines) and `tests/coreapp/emu/run.sh` (353)
    with `library_n.sh`, `uimvp.py`, `media.py` and `crtmedia.py`. By hand there are also `tests/hostapp/emu_t135_direct.sh`
    (145) and the updater flow `tests/updater/emu_t135.sh` (379) with `emu_check.py` (215). They drive ue2emu with control
    scripts (`key`, `wait`, `png`) and grep its log.
  - The `emu` job runs only on `integration` and `main`, because it needs the `update` job (D23). A feature branch and a pull
    request run no emulator test.
- **Why it matters:**
  - A scenario says what the person does and sees; a control script says which key and how many milliseconds. A contributor
    understands the first without reading the runner.
  - A fault in the UI or the update path reaches `integration` before any emulator test sees it.
- **The parts:**
  - **P1** Each scripted flow becomes feature files on one runner: host app, core app and updater.
  - **P2** The runner runs in the emulator image of the pipeline, not only on the Mac.
  - **P3** A feature branch and a pull request run the features their change touches; a red feature blocks the merge
    (a required check).
  - **P4** A contributor can write a new scenario from the documented steps alone.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 (P1) the host flow as features
  Given the checks of tests/hostapp/emu_host.sh today (host, a core, the way back with the menu marker)
  When they run as feature files
  Then every check of the script has a scenario that checks the same, and the script is gone

Scenario: S2 (P1) the core app flows as features
  Given the checks of tests/coreapp/emu/run.sh and library_n.sh today
  When they run as feature files
  Then every check has a scenario that checks the same, and the scripts are gone

Scenario: S3 (P1) the updater as features
  Given the scenarios of tests/updater/emu_t135.sh and tests/hostapp/emu_t135_direct.sh
  When they run as feature files (shared with T245)
  Then every scenario and its control has a feature, and the scripts are gone

Scenario: S4 (P2) the features run in the pipeline
  Given a push to integration
  When the emu job runs
  Then it runs the feature files in the emulator image and reports each scenario as passed or failed

Scenario: S5 (P3) a pull request runs the features of its change
  Given a pull request from a fork that changes the core app
  When its checks run
  Then the features of the core app run, and the pull request cannot be merged while one is red

Scenario: S6 (P4) a contributor writes a scenario
  Given the step list of the runner, documented with one example per step
  When a new scenario is written from it alone
  Then it runs without a change to the runner
```

The proof of S1-S3 is a list that maps each old check to its scenario; S4 and S5 are pipeline runs; S6 is the step list and
one new scenario.

## §3 Decision

Nothing is decided yet. Alex's words in the origin give the direction: everything that runs in the emulator becomes a scenario,
and the pull request runs and requires it. The questions for Alex, asked one at a time:
- **Q1 The runner:** the `tests/ui` runner grows host and updater modes, or a new runner beside it.
- **Q2 What a pull request runs:** the features its change touches (by the plan), or all of them.
- **Q3 The required check:** which check of the pipeline the ruleset of `integration` requires.
- **Q4 T245:** its update features are built here as part of S3, or in T245 first.

## §4 Out

To be decided with Alex before building.

## §5 Built

Nothing yet.
