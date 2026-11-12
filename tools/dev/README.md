# tools/dev

Snapshots of helper scripts from the 1541ultimate Claude session (2026-10-13). They contain absolute paths to that
session's job directory and worktrees; adapt before use. T94 replaces them with CI stages (`pack`, `deploy`).

- `spec_answer.sh` — record Alex's answer under `Qn` of a spec, commit, push.
- `pack_after_run.sh` — wait for a CI run, fetch its bitstreams from the builder cache, pack `.cor` files, write `boot.cfg`.
- `fixedhost_exp.tcl`, `fixedhost_run.sh` — the fixed-host experiments of T87 §5 D15 (E1/E2, CONTAIN_ROUTING).
