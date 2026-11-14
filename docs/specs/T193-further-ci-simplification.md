# T193 — Further CI simplification: one run per push shows its children as a dependency graph again

**Status:** PROPOSED (2026-11-08, Alex: "let us remember this as spec 193 'further CI simplification'", translated). Not designed further, not built (2026-11-15 sync: confirmed, no generated run.yml in ci/ or .gitea/).
**Repo:** TRX64U-MultiCore — CI only (`ci/orchestrate.py`, `ci/children.txt`, `.gitea/workflows/`).
**Number:** T193 (registry: the table in `TODO.md`).
**Origin:** Alex, 2026-11-08, after run 1222 (translated): "This nice view with the dependencies of the jobs/steps, where did it go?"; "In my
Pulumi project at work we have GitLab Enterprise, with pipelines + child pipelines in such views. The pipeline.yml is built by the orchestrator
on demand"; "if the orchestrator can start the relevant steps, it can build a .yml with the steps after the push and start it?"
**Depends on:** T174 D4 (parent and child pipelines, merged).
**Milestone:** M3.

---

## §1 The problem

- **Who:** Alex, watching a push in the Gitea web UI.
- **What he wants:** one picture of the run: which children run, which wait for which (`vivado large:amiga` after `sim large:amiga`), which are
  skipped, which are red.
- **What happens today:** since T174 D4 the push runs `pipeline.yml`, one job (the parent). It plans (`ci/plan.py`) and dispatches every child
  as its own run (`workflow_dispatch`: run 1222 started 1223-1265). The dependencies live only in `ci/children.txt` and `ci/orchestrate.py`;
  Gitea sees a flat list of runs and draws no graph. The only overview is the parent's log (`=== dispatched ...`, `=== green ...`) and its end
  table.
- **Why Gitea cannot do it as GitLab does:** GitLab runs a generated YAML (`trigger: include: artifact:`, `strategy: depend`) and shows the
  child pipelines downstream. Gitea Actions 1.27 runs only workflow files that lie on a git ref; the dispatch API takes a file name and a ref,
  not content. There is no downstream view across runs.

Parts:
- **P1 Graph:** one Gitea run per push whose jobs are exactly the children the plan asks for, joined by `needs:`, so Gitea draws the graph.
- **P2 Generation:** the orchestrator writes that workflow after the plan, from `ci/children.txt`.
- **P3 Hand-over to Gitea:** the generated file reaches Gitea without touching `main`.
- **P4 What D4 gave and must not be lost:** red at once with the reason, dependents of a red child do not start, the time limit per child from
  the bench timeouts, the end table, the content-keyed cache.

## §2 Scenarios

Written when the design is decided with Alex.

## §3 Decision

Not decided. The idea from the conversation (2026-11-08), as a starting point:

1. The push starts the parent; the plan runs as today.
2. The orchestrator writes `.gitea/workflows/run.yml`: one job per child of the plan, `needs:` from `ci/children.txt`.
3. It commits that file on a throw-away branch on the pushed commit (for example `ci/run-<parent run>`).
4. It dispatches `run.yml` on that ref; Gitea shows that run as a graph. The parent links it.
5. When the run ends, the branch is deleted.

Points to settle: `ci/*` branches must not start a push run (`branches-ignore`); one helper commit per run; the per-child time limit becomes a
job's `timeout-minutes` (check that act_runner honours it); re-running one child by hand.

## §4 Out

- Moving off Gitea (GitLab or another CI): not part of this.
- Replacing the native app tests with emulator flows (discussed the same day, Alex: "let us leave that for now", translated): not part of this.

## §5 Decisions taken while building

Nothing built.
