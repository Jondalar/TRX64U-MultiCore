# T246 — The lint checks what a push changed: the structure on a feature branch, the fit on `integration`, everything before `main`

**Status:** PROPOSED (2026-11-20).
Open (one line, as in the TODO table): PROPOSED 11-20 (Alex); questions Q1-Q3 open
**Repo:** TRX64U-MultiCore — the lint stage: `ci/lint.sh`, `ci/plan.py`, `.github/workflows/pipeline.yml`.
**Number:** T246.
**Origin:** Alex, 2026-11-20 (translated):
- "Hm ... the lint thing is 'weird' ... here too the linter should look only at what changes ... and on feature branches only
  the structures, on integration that it fits, and when it goes to main once more as a safety net. So only 'missing' or 'new'
  ... (although nothing should be new there any more)."
- Later, after two red runs on a feature branch: "Pipelines fail again?! That is really annoying ... why can't the lint be
  built dynamically by 'what has changed' .. I don't understand. It works in Java and TS projects too."
**Depends on:** T174 D4 (the pipeline's children), T100 D9 (build by change, `ci/plan.py`), T239.1 (the pipeline on GitHub).
**Milestone:** M1.

---

## §1 The problem

- **Who:**
  - Alex, who waits for a push to go green.
  - The sessions, whose pushes the lint holds up.
- **What happens today:**
  - `ci/lint.sh` runs 100 checks on every push that is not documentation only. 29 of them are self-tests of the CI scripts
    (`ci/test_*`), which plant faults in a scratch clone.
  - The lint does not read the plan. `ci/plan.py` knows what a push changed and uses it for the children (which cores, which
    keys), but every check runs over the whole tree.
  - It takes 6 to 10 minutes per push. Lint jobs of 2026-11-20 on GitHub: 22:07:25-22:13:45, 21:22:38-21:31:34,
    19:45:55-19:53:02, 21:22:32-21:32:11 (UTC).
  - The same 100 checks run on a feature branch, on `integration` and on `main`.
- **Why it matters:**
  - Each push pays the full lint time, also for a one-line change.
  - Alex sees every red run in his mail.
  - A red check on a file the push did not touch stops a branch that is otherwise ready.
- **Worked example:**
  - Run 44817378487 (`t241-scpu`, 2026-11-20): the first push was red in the lint, because the new bench
    `cores/c64scpu/sim/run_irqmir.sh` was in neither `ci/sims.txt` nor `ci/sims_excluded.txt`. That was a real fault of the
    change; a lint by change must still find it.
  - The second red run of that branch was not the lint: `cor (large:c64scpu)` (T239.1 §5, 2026-11-20 late).
- **The parts:**
  - **P1** Each check names the files it reads: its inputs.
  - **P2** A push runs the checks whose inputs it changed, and the level of the branch decides how much more runs (Alex: the
    structure on a feature branch, the fit on `integration`, everything as a safety net before `main`).
  - **P3** The self-tests of the CI scripts run when the CI scripts change.
  - **P4** The lint log says which checks ran and which were skipped, and why.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 (P1, P2, P4) a feature branch that changes one application file
  Given a push to a feature branch that changes only coreapp/src/ui.cc
  When the lint runs
  Then only the checks whose inputs include coreapp/ run
  And the log lists every skipped check with its reason

Scenario: S2 (P2) a new file that is not registered is still found
  Given a push to a feature branch that adds a bench script that is in neither ci/sims.txt nor ci/sims_excluded.txt
  When the lint runs
  Then it is red with the same message as today ("in neither ci/sims.txt nor ci/sims_excluded.txt")

Scenario: S3 (P2) integration checks the fit
  Given a push to integration
  When the lint runs
  Then the checks that compare files with each other run (for example TODO against the specs), as the level asks

Scenario: S4 (P2) main runs everything
  Given a push to main
  When the lint runs
  Then all checks run, as today

Scenario: S5 (P3) the self-tests follow the CI scripts
  Given a push that changes a file under ci/ or .github/
  When the lint runs
  Then the self-tests of the changed scripts run; a push that changes no CI file runs none

Scenario: S6 (P2) the time of a small push
  Given a push to a feature branch that changes one application file
  When the lint runs
  Then it finishes within the time Alex sets (Q3); today it takes 6 to 10 minutes
```

The proof of each scenario is the lint log of a GitHub run of that kind.

## §3 Decision

Nothing is decided yet. Alex's words in the origin give the levels (P2) as his wish. The questions for Alex, asked one at a time:
- **Q1 The levels:** which checks are "structure" (feature branch) and which are "fit" (`integration`). A list per check is
  proposed when the inputs (P1) are known.
- **Q2 How a check names its inputs:** a line per check in `ci/lint.sh`, or a table beside it that the lint test keeps
  complete.
- **Q3 The time:** the figure S6 must meet.

## §4 Out

To be decided with Alex before building.

## §5 Built

Nothing yet.
