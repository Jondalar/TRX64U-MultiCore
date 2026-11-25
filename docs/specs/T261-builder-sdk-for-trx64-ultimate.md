# T261 — The builder serves TRX64-Ultimate too, the SDK lane publishes each SDK as a release, and trx64u.cor comes from TRX64-Ultimate's GitHub pre-release

**Status:** ACCEPTED (2026-11-25), asked by Alex; building.
**Repo:** TRX64U-MultiCore (the SDK lane in `.github/workflows/matrix.yml`, `ci/`, `release/imports/trx64u.pin`, the contract
`docs/contracts/trx64u-firmware-handover.md`) and the builder repository `TREX/trx64mc-builder` (`pull/`).
**Number:** T261.
**Origin:** The TRX64-Ultimate session (TRX64U-FW), 2026-11-25, U20 part C, with Alex's decisions there (TRX64-Ultimate
`docs/specs/U20-github-home.md` §6, u18 23eefe24): D9 one builder serves N repositories; D10 the shared builds repository
`Jondalar/trx64mc-builds` with a tag prefix of their own; D11 the MultiCore SDK lane publishes each SDK version as a release there;
D12 MultiCore takes `trx64u.cor` from TRX64-Ultimate's pre-release there (`trx64g` stays pinned at their run 481 until trx64u works
well). Alex, here (translated): "Yes please, a spec of its own (261?) and then build at once please, this is important now so
that it goes on and I get this one thing closed."
**Depends on:** T239.1 (the builder, the builds repository, D21 the App, D42/D43 pre-releases and imports), T239.5 (the SDK lane).
**Milestone:** M3.

---

## §1 The problem

- **Who:** TRX64-Ultimate moves to GitHub with the MultiCore pattern (T239.1/T239.5; their matrix is a copy of `t239.3`
  d9ddadeb1). Their pipeline needs Vivado for `c64_basic`, an SDK release to pin, and a place to publish `trx64u.cor`.
- **What happens today** (read 2026-11-25):
  - `trx64mc-builder` is single-source: `pull/config.py` has one `SRC_REPO`, `BUILDS_REPO`, `SRC_WORKFLOW_PATH`, `KEYCODE_REF`,
    `ENV_VIVADO`/`ENV_BITSTREAMS`, `CI_BIT_CACHE`; poll, keys and publish work on that one source.
  - `Jondalar/trx64mc-builds` holds no `sdk-*` release; the SDK lane hands its tarball on as a run artifact only.
  - `release/imports/trx64u.pin` names a file of TRX64-Ultimate's Gitea package registry (`COR_FILE=trx64u_<sha8>.cor`), fetched
    by the builder's `pull/imports.py` (`TRX_FW_PKG_URL`).

Parts:
- **P1 Builder sources.** A table of sources; MultiCore's row is today's behaviour; TRX64-Ultimate's row as their part C, item 1.
- **P2 SDK release.** The SDK lane publishes `sdk-<sdk_version>-<sha8>` with the tarball, the lane's `.u2a` files and `SHA256SUMS`.
- **P3 SDK 0.4.6 backfill.** `sdk-0.4.6-6e280710` (the release TRX64-Ultimate pins) with the tarball they built against.
- **P4 Import.** The trx64u pin names TRX64-Ultimate's GitHub pre-release; the builder fetches `trx64u.cor` from it.
- **P5 Outside clicks** (Alex): the App on their repository, the token extended, their environments.

## §2 Scenarios

```gherkin
Scenario: S1 two sources in the builder's tests
  Given the source table with MultiCore's row and TRX64-Ultimate's row
  When  pull/run_tests.sh runs
  Then  every existing test passes unchanged (MultiCore's row is today's behaviour), and new tests prove for TRX64-Ultimate's row:
        its work order with one entry ^c64u-[0-9a-f]{16}$ is read, its trusted key code (ref main) computes the key, the build is
        dispatched with `ci/bitstream.sh c64u` and its own CI_BIT_CACHE, the entry is published as the pre-release fw-bit-<entry>
        (not latest) with a SHA256SUMS the publisher writes, the statuses go to its repository, its environment bitstreams is approved
  # unit: trx64mc-builder pull/run_tests.sh

Scenario: S2 MultiCore unchanged on the live builder
  Given the multi-source builder deployed (Gitea main)
  When  the next MultiCore push runs
  Then  its bit-* entries are published and its bitstreams environment approved as before
  # CI: the next t239.3 run after the deploy

Scenario: S3 TRX64-Ultimate's first bits job
  Given their first push on GitHub main
  Then  fw-bit-c64u-<key> appears in trx64mc-builds and their run goes on past bitstreams
  # CI: their run (reported by TRX64U-FW)

Scenario: S4 the SDK release
  Given a push to a branch that publishes SDK releases
  When  the SDK lane's package differs from the last release of its version
  Then  sdk-<version>-<sha8> holds trx64-sdk-<version>.tar.gz, the lane's .u2a files and SHA256SUMS; an unchanged package publishes nothing
  # CI: the matrix run's SDK lane; the release listing

Scenario: S5 the 0.4.6 backfill
  Then  sdk-0.4.6-6e280710 exists and its tarball's sha256 equals the one TRX64-Ultimate pins
  # gh release view; their sdk.pin

Scenario: S6 trx64u.cor from their pre-release
  Given release/imports/trx64u.pin naming fw-<branch>-<sha8>, COR_FILE=trx64u.cor, COR_SHA256, COR_SIZE
  When  the builder's imports run
  Then  it fetches trx64u.cor from that release, checks size and sha256, and publishes imp-trx64u-<key>; the package takes it as before
  # unit: pull/test_imports.py; CI: the next package with the new pin
```

## §3 Decisions

- **D1 (Alex, 2026-11-25):** build at once (Origin).
- **D2 Names (TRX64-Ultimate's, taken as they are):** source `Jondalar/TRX64-Ultimate`, builds `Jondalar/trx64mc-builds`, prefix
  `fw-`, releases `fw-bit-<entry>` (pre-release, not latest), workflow `.github/workflows/pipeline.yml`, work order version 1 with
  lanes `["bits:c64_basic"]` and one entry `^c64u-[0-9a-f]{16}$`, key code `ci/keys.sh` + `ci/lib.sh` from trusted ref `main`
  (`ci/keys.sh --data DIR`), `ci/vivado.env` from the commit, trusted branch `main` (`integration` once cut), environments `vivado`,
  `bitstreams`, `builds`, build command `ci/bitstream.sh c64u` with their own `CI_BIT_CACHE` and `CI_HANDOFF` (their script takes
  heavy.lock slot 0 itself), image `vivado-env:2024.1`. The publisher skips their SHA256SUMS and writes its own.
- **D3 Credentials:** the same GitHub App `trx64-builder` (their repository added to its installation) and the same fine-grained
  `SRC_API_TOKEN` extended to their repository; `BUILDS_UPLOAD_TOKEN` unchanged (same builds repository).
- **D4 SDK releases (coordinator, under D1):** tag `sdk-<sdk_version>-<sha8>` (the commit's), assets `trx64-sdk-<version>.tar.gz`,
  the lane's `.u2a` files (names kept), `SHA256SUMS`; published by the SDK lane through `ci/publish_release.py` in the environment
  `builds` with `BUILDS_TOKEN`, as the pre-release is (T239.1 D42); from `integration`, `main` and `t239.3` while it exists (as
  D42 let `t239.1` publish), and only when the package differs from the last release of its version.
- **D5 Import:** the pin says `RELEASE=fw-<branch>-<sha8>`, `COR_FILE=trx64u.cor`, `COR_SHA256`, `COR_SIZE`; the builder's
  `imports.py` fetches the asset from that release of the builds repository; the contract
  `docs/contracts/trx64u-firmware-handover.md` changes on both sides in the same step. `trx64g` keeps its pin.
- **D6 (Alex, 2026-11-25, translated: "ok, out, and TRX64-Ultimate must build that itself please"):** `trex_crt.u2a`, `softboot.u2a`
  and `online.u2a` are trx64u's features (T239.3 D21, D44). MultiCore's SDK lane no longer builds or publishes them: `ci/trxapps.sh` part
  `sdk` builds `testlib.u2a`, `unzip.u2a`, `miniz.u2a`, so an SDK release carries those three beside the tarball. TRX64-Ultimate builds the
  three in its own pipeline from its own copy of `apps/trex_crt`, `apps/softboot`, `apps/online`; the copies here are deleted once theirs
  build. `sdk-0.4.7-760763ec` (immutable) still holds them; the next SDK release does not. MultiCore's package (`ci/package.sh`)
  no longer carries them either; TRX64-Ultimate delivers them.

## §4 Out

- `trx64g.cor` from TRX64-Ultimate's pipeline (D12 there: pinned at run 481 until trx64u works well).
- The registry (T239.4).

## §5 Build notes

- **S5 / P3, 2026-11-25:** `sdk-0.4.6-6e280710` published in `Jondalar/trx64mc-builds` (pre-release, not latest). The bytes are
  the ones TRX64-Ultimate pinned, taken from the Gitea packages `TREX/generic/trx64-sdk/0.4.6-6e280710` and
  `TREX/generic/trx64-online/1.0-6e280710` (the hand-over upload of `t239.3` 6e2807103; its pipeline run 45437617746 has no SDK
  artifact): `trx64-sdk-0.4.6.tar.gz` sha256 fd3175de...5701356, 10,699,890 bytes; `online.u2a` 061c7394...ab820968, 26,147
  bytes; `SHA256SUMS` over both. Made as a draft, the server's asset digests compared, then published. The leak check
  (`tools/check_public_tree.py --files`) has 9 hits in the tarball, each one a path-scoped allowance of the tree mode
  (`_TEXT_ALLOW`: struct members shaped like host names, the ESP32 vendor default, example addresses, the stb_image author
  line) that misses only because the tarball's paths start with `menusim/` or `base/src/`; the same text is in the public tree.

### MultiCore half (2026-11-25)

- **P2 job:** `sdk-release` in `.github/workflows/matrix.yml` (after `sdk`; needs `sdk`; environment `builds`, `BUILDS_TOKEN`; runs `python3 -I ci/publish_sdk.py`). Its `if:` demands a push
  to `integration`, `main` or `t239.3`; `tools/check_workflows.py` allows environment `builds` for this job under that rule too (selftest: 19 faults planted).
  The matrix is called by `pipeline.yml` only on `t239.3` and `t239.3-*` (`pipeline.yml` `matrix:` job), so until the matrix is called on `integration`/`main` the job runs on
  `t239.3` alone; the script's own guard (`P.SDK_BRANCHES`) holds for all three. The environment `builds` allows `t239.3` since 2026-11-25 (Alex, P5; its
  deployment rules: `main`, `v*`, `t239.1`, `t239.3`); `integration` is added when the matrix runs there. `BUILDS_TOKEN` is a secret of the environment `builds`,
  not of the repository. Run 45569965584 (`1882f8375`) proved that a called workflow's job does not get it from naming the environment alone:
  `ci/publish_sdk.py: BUILDS_TOKEN is not set` (review 1.2 had warned). Fix: `secrets: inherit` on the `matrix:` call in `pipeline.yml`.
  **S4 passed 2026-11-25:** run 45574274739 (`760763ec1`) green; it published `sdk-0.4.7-760763ec` (pre-release): `trx64-sdk-0.4.7.tar.gz`
  (10,707,809 bytes), `miniz.u2a`, `online.u2a`, `softboot.u2a`, `testlib.u2a`, `trex_crt.u2a`, `unzip.u2a`, `SHA256SUMS`.
- **Decision:** `ci/publish_sdk.py` stages the lane's `trx64-sdk-<version>.tar.gz` (version = `app/sdkpkg/SDK_VERSION`, must match the tarball name) and every `*.u2a`, writes a
  fresh `SHA256SUMS` (the lane's names `./x` and its `coreapp.map` are not published), lists the releases of the builds repository (newest first), takes the newest published
  one tagged `sdk-<version>-<8 hex>`, reads its `SHA256SUMS`, and publishes only when it differs from the one written here (every file, names and SHA-256:
  a changed `.u2a` beside an unchanged tarball publishes too, review 2.4) or no such release exists. Equal: the log says
  `the package of SDK <v> is unchanged (its <n> files are those of <tag>): nothing published`. The tarball is reproducible (`tools/sdk_pack.sh`: byte-ordered files, mtime 0, `gzip -n`).
- **Assets:** `trx64-sdk-<version>.tar.gz`, the `.u2a` files of the lane (`ci/trxapps.sh` `libapp` lines: `testlib.u2a`, `unzip.u2a`, `miniz.u2a`, `trex_crt.u2a`,
  `softboot.u2a`, `online.u2a`; the online sub-app is `online.u2a`, Assembly64 / CommoServe search), `SHA256SUMS`.
- **Release kind:** pre-release, never latest: `ci/publish_release.py` `publish()` makes every release of the builds repository a draft, then `prerelease: true`, `make_latest: false`
  (the pre-release convention, T239.1 D42); `publish_sdk.py` uses that function, so the SDK releases are the same kind.
- **Shared code:** `publish_release.guard()` takes the branches and whether a v* tag publishes (`SDK_BRANCHES = integration, main, t239.3`, no tags); `publish()` takes the body text.
  `ci/fakegh.py` lists releases newest first, as GitHub does.
- **P4 pin forms:** only `ci/imports.py` decides where the file comes from (`source_url()`): release form `RELEASE=fw-<branch>-<sha8>` + `COR_FILE=trx64u.cor` ->
  `https://github.com/<MC_BUILDS_REPO>/releases/download/<RELEASE>/<COR_FILE>` (base replaceable by `TRX_FW_RELEASE_URL`), Gitea form `PACKAGE`+`VERSION` ->
  `<TRX_FW_PKG_URL>/<PACKAGE>/<VERSION>/<COR_FILE>`; `RELEASE` wins. `COR_FILE`, `COR_SHA256`, `COR_SIZE` are the same in both. The other readers
  (`ci/package.sh`, `ci/check_pkg.py`, `ci/fetch_bits.py`, `tools/check_versions.py`) read only the `.cor` by its SHA-256 or test `COR_SHA256`, so they take both forms unchanged.
  `release/imports/trx64u.pin` is left byte-for-byte as it is (the entry key `imp-trx64u-<key>` hashes the pin; a comment edit would re-key it); it moves to the release form with a
  new pin once TRX64-Ultimate's first `fw-*` pre-release exists. Documentation: `ci/imports.py` header and the contract.
- **Contract:** `docs/contracts/trx64u-firmware-handover.md` §1 (table row, header note) and §2 (the `trx64u` bullet) name the asset `trx64u.cor` of `fw-<branch>-<sha8>`, marked
  "agreed with TRX64-Ultimate session 2026-11-25, T261 D5". The text must be copied byte for byte into TRX64-Ultimate's `docs/contracts/` (the `diff` check).
- **Tests:** `ci/test_publish_sdk.py` (publish, unchanged publishes nothing, changed tarball publishes, other version, refused refs and tags, wrong package), `ci/test_imports.py`
  `test_both_pin_forms`; both are in `ci/lint.sh`.
- **Import keys (review 3.3):** `ci/imports.py` `key()` hashes `ci/imports.py` itself for a pin with `COR_SHA256`, and the builder names the import
  entries with the trusted ref's (`integration`) `ci/imports.py`. So the release form reaches the builder only when this change reaches `integration`;
  `t239.3`'s `ci/imports.py` already differs from `integration`'s, so this adds no new mismatch there. The pin moves to the release form after that.
- **Review 2026-11-25** (one agent, `9d5770752`): 1.1 (the job runs on `t239.3` only) and 1.2 (environment secret) answered above; 2.4 fixed (all files
  compared, test `online=b"on2"` publishes); 5.1 the duplicated D5 in the contract header removed; 3.3 noted above. Tests: `ci/test_publish_sdk.py` OK.

### Builder half (2026-11-25, `TREX/trx64mc-builder`)

- **P1:** `pull/config.py` holds a table of rows (`ROWS`): `multicore` is today's behaviour from today's variables; `fw` (`Jondalar/TRX64-Ultimate`)
  reads `FW_`-prefixed variables over D2's defaults. `SOURCES` (default `multicore`) switches rows on. `pull/order.py` takes the row's entry regex; the fw
  row demands `lanes == ["bits:c64_basic"]` and at most one entry. `pull/keys.py` takes the row's mirror and key code list (`pull/keycode-fw.txt`:
  `ci/keys.sh`, `ci/lib.sh`). `pull/publish.py` tags `<prefix>bit-<entry>` (`fw-bit-<entry>`), pre-release, not latest, its own `SHA256SUMS`.
  `pull/poll.py` `poll_all` polls every row; one row's error, or bad configuration, is logged and the next row is polled, the job is red at the end.
  `pull/build.sh` picks the row by repository, refuses a `bits:` lane for MultiCore and every other lane for fw, checks the claim's `source_repo`
  (an old claim without it counts as MultiCore), and runs `FW_BUILD_COMMAND` (a script under `ci/`, from configuration only) with
  `CI_BIT_CACHE` and `CI_HANDOFF`. Imports stay MultiCore's: `pull/imports.py` only passes `TRX_FW_RELEASE_URL` to `ci/imports.py`.
- **Paths (TRX64-Ultimate's request):** `FW_CI_BIT_CACHE` default `/cache/bit`, their existing bit cache (their key `c64u-0dce5fff4ef99009` is
  there, so the first fw run publishes without Vivado); their build uses `/cache` as `TRX_CACHE` and `/cache/heavy.lock` (slot 0); the build job
  mounts the whole cache read-write at `/cache`. Hand-over `/cache/trx64u/runs`.
- **Tests:** `pull/run_tests.sh`: ghapi 2, publish 12, keys 32, build 7, poll 45, imports 4, sources 24, each `OK`; the existing ones unchanged.
- **Review** (one agent, `af501f4`, `404ee75`): MultiCore unchanged with `SOURCES` unset (old claims load, no new dispatch input). Fixed in
  `a6dc4f1`: a row's bad configuration no longer stops the poll; the fw scratch build sets `TRX_CACHE=/scratch/trx`; `FW_BUILDS_REPO` defaults to
  `BUILDS_REPO`; `pull.yml` passes every `FW_` variable the README names.
- **Deployed 2026-11-25:** `a6dc4f1` on the builder repository's `main`. Gitea variables: `FW_KEYCODE_REF=u20`, `FW_TRUSTED_BRANCHES=main,u20`
  (TRX64-Ultimate's switch branch, ruleset `protected`; back to `main` when their switch is merged). `SOURCES=multicore,fw` set once `u20`
  (26be21a, `ci/keys.sh` + `ci/lib.sh`) was on GitHub.
- **S3 passed 2026-11-25:** builder poll 4023 (`a6dc4f1`): `== source multicore`, `== source fw: Jondalar/TRX64-Ultimate`, `published
  fw-bit-c64u-0dce5fff4ef99009 (8 assets, immutable=True)`, `approved environment bitstreams of run 45576354413` (their first run, `u20`
  eaf121b); the entry came from `/cache/bit`, no Vivado. The release: pre-release, `c64_basic.bit`, `ddr2_test.bit`, `hdmi_test.bit`, `from`,
  `key`, `wns.txt`, `reports.tar.gz`, `SHA256SUMS`. S2: the same poll and every poll since the deploy (4014-4023) green for MultiCore.
- **P5 (outside):** done by Alex with the TRX64-Ultimate session: the App installed on their repository and the protection rule of `bitstreams`,
  `SRC_API_TOKEN` extended, environments `vivado`, `bitstreams`, `builds`; environment `builds` here allows `t239.3` (2026-11-25).
