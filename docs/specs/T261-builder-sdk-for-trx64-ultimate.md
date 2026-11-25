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
