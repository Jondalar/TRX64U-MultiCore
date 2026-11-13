# T172 — Any branch can get a board installer on demand, named so that its origin is visible

**Status:** PROPOSED (2026-11-01). Status check 2026-11-13: BUILT: D1-D6 (trigger, VERSION, version stamp) and S5 bundle check (98598b5a1, 11-07), merged 6ca31918f; S2-S4 proof not recorded.
**Repo:** TRX64U-MultiCore — CI (`.gitea/workflows/`, `ci/`), the installer (`.ue2`/`.cfw`/`.txu`), the version string the host app shows.
**Number:** T172.
**Origin:** Alex, 2026-11-01, after run 1122 (branch `t164`) built everything but gave no installer for the board test: "Maybe a 'create
bundle' pipeline step that can then run on feature branches too, as needed?"; on the trigger: "Both." (dispatch checkbox and a
commit-message tag); "Feature branch: put the branch name + commit somewhere. Main: think about SemVer." (translated)
**Depends on:** T171 (our package `trx64u-multicore`), T100 (pipeline stages).
**Milestone:** M2.

---

## §1 The problem

- P1: the update stage skips every branch but `integ-m1`/`main` and `v*` tags (run 1122: `update SKIPPED: only integ-m1/main (and v* tags)`),
  so a gateware branch that must be tested on the board has no installer; files are copied by hand.
- P2: an installer does not say where it comes from; the board shows no branch or commit, a test result cannot be tied to a build.
- P3: `main` builds carry only a commit hash, no version a tester can read and compare.

## §2 Scenarios

- S1: push to a feature branch with `[bundle]` in the head commit message -> the run builds the installer and publishes it under
  `trx64u-multicore` `<branch>-<sha8>`; a push without the tag builds none (negative control: run plan shows the stage skipped).
- S2: dispatch on a feature branch with the `bundle` checkbox -> same as S1.
- S3: the host app (About / boot screen) and the installer's manifest show `<branch>@<sha8>` for a feature build.
- S4: a `main` build shows a SemVer (format decided in §3, Q1).
- S5: before hand-over the bundle's content is checked (bitstream design name, host key, app version, as `feedback check-installer-content`).

## §3 Decisions

- D1 (Alex, 2026-11-01): both triggers, the dispatch checkbox `bundle` and `[bundle]` in the commit message.
- D2 (Alex): a feature-branch bundle carries branch name + commit.
- D3 (Alex): `main` gets SemVer.
- D4 (Q1, Alex, 2026-11-01: "Yes. We start with 0.2.0", translated): a `VERSION` file in the repository, bumped by hand: major = a break of the
  host/core contract, minor = a new function, patch = a fix. CI appends the commit (`0.2.0+<sha8>`); the release tag `v0.2.0` comes from the
  same file. Start: `0.2.0`.
- D5 (Alex, 2026-11-01): "And it only gets really binding from the release on, when the host-core contract is mature." (translated): while the
  version is 0.x the bump rules are a guide, not a gate (no lint on bump kind); from the first release (1.0.0, contract declared mature) a
  contract break must raise major.
- D6 (Alex, 2026-11-01): "So we take the notation now already. Later, when we let other people's code in by PR, that is done with
  Conventional Commits." (translated): the SemVer notation is used from now on; Conventional Commits (and bumps derived from them) start with
  outside contributions by PR, not now.

## §4 Out
- Conventional Commits and automatic bumps: later, with outside PRs (D6).

## §5 Built
- Trigger (D1): `ci/select.sh update` also answers yes for `BUNDLE_INPUT=true` (dispatch input `bundle` of pipeline.yml -> update.yml `bundle`)
  or `[bundle]` in the head commit message; the gate stays in the script. Negative controls in `ci/lint.sh` (check "select.sh"): plain
  feature branch -> no, `bundle=false` -> no. The publish path is unchanged: `<branch>-<sha8>` in `$MC_PKG`, `ci/check_pkg.py` still guards it.
- Version (D2-D4): `VERSION` = `0.2.0`; `ci/version.sh` prints `0.2.0+<sha8>` (main), `<branch>@<sha8>` (other), `0.2.0` (tag); lint checks
  x.y.z format only (D5). It goes into `manifest.json` (`version`) and the host app (`APP_VERSION_STRING` in `gitinfo.h`, shown in the splash,
  the status chip and the console banner). The binary installer manifest (tools/trx_pkg.py) is untouched: its id was already `<branch>-<sha8>`.
- S5 (2026-11-07): `tools/check_bundle.py` checks the bundle's content from the files: manifest.json sha256/size of c64_basic.bit, null_full.bit, hostapp.app;
  the two host images are one design (.bit header design name, XC7A50T) with one host key (AXSS) that is the manifest's; the `.txu` verifies and holds
  null_full.bit and hostapp.app, host key, version `<branch[:14]>-<sha8>`, commit; the `.cfw` and `.ue2` pass `trx_pkg_check` byte-equal to those inputs
  and carry the id `...-<sha8>`; the host app shows the manifest's version in every TRXVSTAMP field. `ci/update.sh` runs it on the downloaded inputs
  plus the freshly built update files before `publish_files` (red: nothing is published). `ci/test_bundle.sh` (lint, check "bundle path") runs its
  selftest: `good bundle: PASS` and nine controls that must fail, each quoted by name: cfw_wrong_bitstream, txu_wrong_app, txu_wrong_bitstream,
  app_old_version, txu_missing, ue2_missing, input_changed, other_host_key, other_design. On the real package `trx64u-multicore/main-e794f15d`
  (downloaded from Gitea): `bundle PASS`. Not run on a pushed `[bundle]` branch yet (that is the first CI run after the merge).
- 2026-11-01, two faults seen on the board with bundle t164-774497b1 (run 1128), fixed on t164-t172b:
  - The host app showed `t164@32d8fb59`: `make gitinfo` compiled `ci/version.sh` into the app, and stage trxapps caches the app by its tree
    (`ci/trxapps.sh` key), so a cache hit carried the string of the commit that filled the cache. Now `host-app/target/common/rules.mk` emits the
    string as a field `TRXVSTAMP:<string>` (`app_version_string()`, a volatile 64-byte array) and `ci/package.sh` patches it in the package copy of
    `hostapp.app`/`hostapp.elf` with `tools/stamp_version.py` before the `.txu` and the update files are made from it (a branch name too long for the
    53-byte field is cut, the commit stays). The cache entry is untouched, D14/D16 caching is as before (the app is not rebuilt for a commit).
  - The `.txu` was missing: stage package made and published `trx64host_update_<sha8>.txu`, then stage update's sweep of "stale update files" matched
    it by the name prefix `trx64host_update_` (it is not in the update stage's `$pub`) and deleted it from both package versions, and the same prefix
    dropped its SHA256SUMS line. The sweep and the sums filter now name `.cfw`/`.ue2` only (`update_file_names`, `update_sums_without_ours` in
    `ci/lib.sh`), and `ci/update.sh` stops when the manifest lists no `trx64host_update_<sha8>.txu`.
  - `ci/test_bundle.sh` (in lint): the real gitinfo rule compiled into a probe, stamped, run (cached image shows the old string, package copy the
    new one); the sweep on a fake listing keeps the `.txu`, and the old pattern is shown to hit it (control).
