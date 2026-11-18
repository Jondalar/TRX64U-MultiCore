# T221 — Every component has a SemVer, and every display shows it: SemVer+commit on a branch build, plain SemVer on a release

**Status:** BUILDING (2026-11-15; branch t221-semver). PROPOSED 2026-11-15, Alex.
**Repo:** TRX64U-MultiCore — host (gateware, app), generic core app, every `cores/<id>/core.toml`, `ci/version.sh`, `tools/` packers, `contract/`.
**Number:** T221 (registry: the table in `TODO.md`; T220 was the last row).
**Origin:** Alex, 2026-11-15 (translated): "Let us work out by spec: SemVer for everything from now on: host, host menu, generic app, every core. And then the displays: on feature branches / integration builds SemVer+commit; on a release: SemVer."
**Depends on:** nothing (T172 D2/D4 `ci/version.sh`, T209 D16 boot menu version column, T94 Q3 contract number are the starting points).
**Milestone:** M2.

---

## §1 The problem

Today only some parts carry a version, in four different formats, and the displays do not agree:

- The host boot menu shows "full firmware" in the version column for every large core (`host_core_rows.cc:126-141`, `hc_entry_ver`: `hc_efull(i)` wins over `version`), although a large .cor carries a version (`pack_large` writes `version`, `tools/cor_pack.py:501`; `cor_pack.py check` of a cpc.cor says "version": "0.0.1"). Small cores show e.g. "0.9.0".
- The host has no SemVer at all: the gateware is a hash (host key), the host app is `ci/version.sh` (`<VERSION>+<sha8>` on `main`, `<branch>@<sha8>` elsewhere), the contract is an integer.
- The generic core app shows only the commit hash (`APP_VERSION_HASH`), not a version.

### Current state (read from code, 2026-11-15)

| Component | Version today: source | Format / who sets it | Shown where |
|---|---|---|---|
| Host gateware (bitstream) | no version; identity = host key = AXSS word of `null_full.bit` (`ci/lib.sh` `host_key`, `tools/cor_pack.py:118-121,257`); pinned in `contract/host_pin.txt` | 8 hex, computed from the content | top bar / about chip "host %08x" (`host_core_rows.cc:992`); `.cor` check (`host_core_flow_fileops.cc:272`, `host_core_rows.cc:667,684`); `manifest.json` `layers.host.host_key` (`tools/check_bundle.py:100`); `gateware_key` (`ci/package.sh:236`) |
| Host application ("host menu") | `ci/version.sh` + root `VERSION` (`0.2.0`, bumped by hand, lint checks format only: `ci/lint.sh:175`); stamped into the image field `TRXVSTAMP:` (`host-app/target/common/rules.mk:55-56`, `tools/stamp_version.py`, `ci/package.sh:105-107`) | `main` = `0.2.0+<sha8>`, tag `v*` = `0.2.0`, other branch = `<branch>@<sha8>`; a plain feature branch has no SemVer | splash (`hostui.cc:3961` with build date), startlog/console (`hostapp.cc:67`), app chip "app %s" (`host_core_rows.cc:996`, `APP_VERSION_STRING` via `hostui.cc:1496`), `manifest.json` `version` (`ci/package.sh:218-235`); `APP_VERSION_HASH` (short commit) in web UI wire (`hostui.cc:3354`), bug report / startlog (`hostui.cc:2718`) |
| Contract | `contract/VERSION` = `2` (integer, raised when `layer2_ports.txt` changes; `contract/README.md`); `COR_HOST_CONTRACT` (`cor.h:9`); `CONTRACT = 2` (`tools/cor_pack.py:77`); `tools/pack_cores_mc.sh:69` | integer, by hand with the port change; "first part of the host version `<contract>.<n>`" (README, T94 Q3) — that host version is not implemented | chip "contract %d" (`host_core_rows.cc:987`), row state "contract %u != %d" (`:118`), `.cor` manifest `contract` |
| Generic core app (coreapp) | same `ci/version.sh` string, but the sources show only `APP_VERSION_HASH` + `APP_BUILD_DATE` (`coreapp.cc:74`, `corereport.cc:104`, `run.cc:302,421`) | commit hash, no SemVer | core app console, bug report "Core application", Home page |
| Small core gateware | `cores/<id>/core.toml` `version` (read by `tools/core_toml.py get <core> version`, `ci/cor.sh:21`, `ci/package.sh:173`); on `main` only 7 of the core dirs have a descriptor: apple2 0.1.0, atari800 0.0.1, c64ref 1.0.0, ddt_mandel 0.1.0, ddt_novic 0.1.0, null 1.0.0, vc20 0.9.0 | free text, by hand, no rule; no start value rule | `.cor` manifest `version` (`cor_pack.py:321`), boot menu version column (`hc_entry_ver`), "%s v%s, %s" (`host_core_model.cc:419`), release notes (`tools/release_notes_mc.py:354-357`), core app `core_version` (Home, REST `"version"` `rest.cc:565`, `/rpc` `rpccontract.cc:312-314`, bug report `corereport.cc:101`, `run.cc:297`) |
| Large core (own gateware) | same `core.toml` `version`, packed by `pack_large` (`cor_pack.py:472-501`, `tools/pack_large_cor.sh --version`) | same as small | same, except the boot menu shows "full firmware" instead (`hc_entry_ver`) |
| Imported large core (trx64u, trx64g) | `release/imports/<id>.pin` `VERSION=8f1dc184` = the short commit of TREX/TRX64-Ultimate / Gideon bitstream package, `PKG_VERSION=main-8f1dc184` (`tools/pack_import_cor.sh:33-61`) | a commit hash, set by whoever renews the pin | as large core; the version column shows a hash today |
| Update file `.txu` | `tools/pack_txu_mc.sh:27`: `--version "${BRANCH:0:14}-$SHA"` | branch + sha8, not SemVer | `host_core_rows.cc:511-513` "MultiCore update %s", `host_core_flow.cc:716` "Version %s" |
| CI package | `manifest.json` `version` = `ci/version.sh`; Gitea package versions `<branch>-<sha8>`, `<branch>-latest` (`ci/lib.sh:313`) | as host app | `manifest.json`, package list |
| Release notes | `tools/release_notes_mc.py` prints each `.cor` `name` + `version` from the .cor meta (`cor_meta`, `:308`) | follows the .cor | the GitHub release text |
| ESP32 firmware / imported ESP image | own versions (`host_core_flow_esp.cc`), not ours | foreign | out of scope here (§4 is open) |

Not found in code: any place where a version is compared (newer/older) — versions are labels only; the host decides "ready/other host/contract" by contract number and host key, never by version.

**Parts (MECE):**
- **P1** Which components have a SemVer, where each one lives and how it is bumped (source of truth).
- **P2** How a build knows whether it is a release, and what string each build kind produces.
- **P3** Host-side displays: boot menu rows (small and large cores, "full firmware"), chips (host, contract, app), update/about pages.
- **P4** Core-side displays: generic core app (Home, F8/about, console, bug report, REST `/v1` info, web UI).
- **P5** Artifacts: `.cor` metadata, `.txu`, `manifest.json`, Gitea package version, release notes.
- **P6** The two numbers that are not versions today: the host key (gateware identity) and the contract number.

## §2 Scenarios — what proves it is solved

Version string forms: B = branch/integration build `X.Y.Z<sep>+<sha8>` (form open, question Q1); R = release `X.Y.Z`.

| # | Solves | Scenario | Test (bench / board step) | Where it runs |
|---|---|---|---|---|
| S1 | P2 | `ci/version.sh` on a feature branch and on `integ-*` prints B, on `main` prints B, on tag `v*` prints R; each of the three components reads its own version file | `ci/test_version.sh` (extends the check `ci/test_bundle.sh:18-24`) with fake refs | lint / CI lint stage |
| S2 | P1 | Every `cores/<id>/core.toml` has a `version` that is valid SemVer 2.0; so have the host-app and coreapp version files; the imported pins carry a SemVer, not a hash | `ci/lint.sh` check (format) | CI lint |
| S3 | P3 | Boot menu version column of a large core shows its version (B or R), not "full firmware" (or both, Q9) | ue2emu picture of the list with a large .cor, ux-review | ue2emu + `ux-review` |
| S4 | P3 | Boot menu version column of a small core shows its version in the same form | same picture | ue2emu |
| S5 | P3 | Host app splash, startlog and the "app" chip show the host-menu SemVer in form B / R | ue2emu picture; `stamp_version.py read` of the package image | ue2emu + CI package |
| S6 | P3, P6 | The "host" chip shows the host SemVer (and the host key, if Q10 keeps it); the "contract" chip shows the contract per Q8 | ue2emu picture of the chips | ue2emu |
| S7 | P3 | The update row/ask for a `.txu` shows its SemVer in the same form | ue2emu picture of the update ask; `txu_pack.py inspect` | ue2emu + CI package |
| S8 | P4 | Core app Home page, F8/about and console line show the core app SemVer in form B / R, next to the core's version | ue2emu picture of a core app | ue2emu |
| S9 | P4 | REST `/v1` info and `/rpc` runtime info return the core version, core app version and host version as SemVer strings | coreapp REST test in the pipeline (`ci/apptests.sh`) | CI apptests |
| S10 | P4 | The web UI (host and core) shows the same strings as the menu | web UI picture / page text from ue2emu | ue2emu |
| S11 | P4 | The bug report (`corereport.cc`, startlog) names core, core app and host app by SemVer in form B / R | apptest of the report text | CI apptests |
| S12 | P5 | `cor_pack.py check` of a small and a large .cor prints the SemVer of its `core.toml` (B on a branch build, R on release) | `tools/cor_pack.py check` over the package of a run | CI package |
| S13 | P5 | `manifest.json` carries `version` (B/R) plus the three component versions; Gitea package version follows Q2 | `tools/check_bundle.py` extended | CI package |
| S14 | P5 | Release notes list each core with its R version | `tools/release_notes_mc.py` run over a release set, text compared | CI package / by hand at release |
| S15 | P1 | Imported trx64u/trx64g: the version shown is Q11's answer, not the 8-hex commit | `pack_import_cor.sh` output + `cor_pack.py check` | CI package |
| S16 | P2 | A real release (tag) build shows R in every display above, a branch build B; one board look at both | board session (batched with the next release), photo of boot menu + chips | board |

Negative controls: none (no flash/persistence operation is touched).

## §3 Decision

Alex's decisions (2026-11-15, translated from his request):

- **D1** "SemVer for everything from now on: host, host menu, generic app, every core."
- **D2** Display rule: "on feature branches / integration builds SemVer+commit; on a release: SemVer."
- **D3** (Q1, 2026-11-15): Branch/integration builds show `X.Y.Z-dev+<sha8>` (e.g. `1.4.0-dev+1a2b3c4d`, pre-release, sorts below the release); a release shows `X.Y.Z`.
- **D4** (Q2, 2026-11-15): A release is not a new build (Alex: "a release is only copying the .cor out of the non-release and patching the metadata"): a tool takes a tested dev package and rewrites its version strings to `X.Y.Z` (the TRXVSTAMP field of the apps via `tools/stamp_version.py`, already done per package in `ci/package.sh:103-108`; the `.cor` metadata `version`; hashes and the `.txu` repacked). Everything else stays byte-identical to the tested build. Open detail: the generic core app has no TRXVSTAMP field yet and needs one.
- **D5** (Q3, 2026-11-15): Two separate SemVers: host gateware and host menu (host app) count on their own.
- **D6** (Q4, 2026-11-15): Both (Alex: "every deliverable has its own version.toml and for the release we have a bundle.toml"): each deliverable (host gateware, host menu, generic core app, each core) carries its own version file; a release has a `bundle.toml` with the bundle SemVer and the version of every part in it.
- **D7** (Q4b, 2026-11-15): Cores keep their version in `core.toml` (it is already the core descriptor); host gateware, host menu and generic core app get a `version.toml` each.
- **D8** (Q5, 2026-11-15): Versions are bumped on the way to `main` (Alex: "on the feature branch no versioning, the state stays; via integ -> main and THERE it is versioned"): a feature branch never changes a version; the merge into `main` raises the version of every deliverable it changed.
- **D9** (Q5b, 2026-11-15): The bump level is set by hand at the merge into `main` for now (whoever merges; Claude asks Alex before a MAJOR). A script proposes the level from the spec/TODO kind and the merge-commit prefix so it is not worked out by an LLM per merge; the human approves. Later, with more contributors and builds from elsewhere, the script decides and a human releases (Alex: "first 3 ... then by script some day and the human approves").
- **D10** (Q6, 2026-11-15): Start values: host gateware, host menu and generic core app start at 0.2.0. The cores are gone through one by one with Alex afterwards (open, one question per core).
  **Answered 2026-11-15** (Alex, translated: "A, but vc20 is also 0.3.0 please"; A = keep today's `core.toml` version): every core starts at the version its `core.toml` holds today, except
  vc20 = 0.3.0 (was 0.9.0). Table: amiga1200 0.3.0, amiga 0.2.0, c128 0.1.0, c264 0.1.0, c64scpu 0.1.0, pet 0.1.0, diag 0.1.0, apple2 0.1.0, vc20 0.3.0, atari800 0.0.1, cpc 0.0.1,
  nes 0.0.1, snes 0.0.1, neogeo 0.0.1, zx 0.0.1, zxnext 0.0.1, ddt_mandel 0.1.0, ddt_novic 0.1.0. `c64ref` (1.0.0, build_only) and `null` (1.0.0, flash filler) are never packed as a `.cor`,
  listed in a release or shown in a menu, so no display shows their version: they keep the value (it must be valid SemVer, `tools/check_versions.py` checks it) and nothing else is done
  with it. Only `cores/vc20/core.toml` changes (last commit of the branch).
- **D11** (Q7, 2026-11-15): MAJOR for a core: either a user-visible break (old settings, saves or media no longer work, menu structure breaks) or a contract change (an older host no longer loads the core).
- **D12** (Q8, 2026-11-15): The contract number folds into the host gateware version: a contract change is a MAJOR of the host gateware; the separate number and the "contract N" chip go. Consequence to settle in the design: the .cor check that today compares the contract integer (`COR_HOST_CONTRACT`, `cor.h:9`; `cor_pack.py` `CONTRACT`) then compares the host gateware MAJOR.
- **D13** (Q9, 2026-11-15): Boot menu, large cores: the version column shows the version as for every core; "full" becomes a small badge at the icon/name (layout with a mock-up and ux-review before it is built).
- **D14** (Q10, 2026-11-15): Host chip: a release shows only the SemVer (`host 0.2.0`); a test build shows SemVer plus the host key (`host 0.2.0-dev+<sha8> · 17CFD66A`). The key stays in F8, the bug report and the .cor refusal text.
- **D15** (Q11, 2026-11-15): trx64u/trx64g take their SemVer from TRX64-Ultimate: its package/manifest carries the version, the pin copies it (a contract line to agree with TRX64-Ultimate in docs/contracts/trx64u-firmware-handover.md).
- **D16** (Q9b, 2026-11-15): Refines D13 (Alex): the badge is a small purple icon beside the version: "L" for a large core, "P" for a partial (small) core; mock-up and ux-review before it is built.
- **D17** (Q12, 2026-11-15): The `.txu` and the Gitea package carry the bundle version from `bundle.toml` (release `1.4.0`, test build `1.4.0-dev+<sha8>`); the part versions are inside (bundle.toml, .cor metadata).
- **D18** (Q13, 2026-11-15): The generic core app counts its own version (follows from D6, its own deliverable).
- **D19** (Q14, 2026-11-15): Besides the host gateware MAJOR check (D12), an update or core install of an older version warns and lets the user go on (downgrade allowed with a warning). The core-install part is replaced by OPEN-5's answer (2026-11-18): no warning.

Everything else is open, asked one at a time, answers recorded here when given.

### Open questions for Alex

1. Exact form of the branch string (SemVer 2.0): (a) `1.4.0+1a2b3c4d` (build metadata, what `main` shows today), (b) `1.4.0-dev+1a2b3c4d` (pre-release, sorts below the release), (c) `1.4.0-<branch>.1+1a2b3c4d`.
2. How a build knows it is a release: (a) tag `v*` (today's `ci/version.sh`), (b) a release branch, (c) a pipeline dispatch input; and does the Gitea package version `<branch>-<sha8>` change to the SemVer string?
3. What is "the host" version: (a) one SemVer for gateware + host app together, (b) two (host gateware, host menu), as your list names "host" and "host menu" separately.
4. Where each version lives: (a) `core.toml` for cores plus one `VERSION` file per component (host, host app, coreapp), (b) a single file with all.
5. Who bumps and when: (a) by hand per change, (b) by the spec/commit type (feat = MINOR, fix = PATCH), (c) only at release time.
6. Start values: (a) keep today's (cores 0.0.1-1.0.0, repo 0.2.0), (b) every component 0.1.0, (c) 1.0.0 for everything that is released.
7. What counts as MAJOR for a core: (a) a contract change, (b) a user-visible break (settings, saves, media), (c) no rule below 1.0.0 (today's lint comment).
8. Contract number: (a) stays an integer and is shown as "contract 2", (b) becomes a SemVer (a port added = MINOR, a port changed/removed = MAJOR), (c) is folded into the host version as README T94 Q3 planned (`<contract>.<n>`).
9. Large cores' "full firmware" label: (a) replaced by the version, (b) version first, "full" as a badge elsewhere, (c) both in the column.
10. Host top-bar chip "host 17CFD66A": (a) replaced by the host SemVer, (b) SemVer plus the key (`0.2.0 · 17CFD66A`), (c) unchanged; the key must stay in the `.cor` refusal text either way.
11. Imported trx64u/trx64g `.cor`: their version comes from TRX64-Ultimate/Gideon, today an 8-hex commit: (a) we assign a SemVer in the `.pin` file by hand per renewed pin, (b) TRX64-Ultimate publishes one (contract change on their side, handover doc), (c) leave as is, display the commit.
12. The update file `.txu` and the Gitea package: same SemVer string as the host app, or the highest of its parts?
13. Does the generic core app version equal the host-menu version (same tree, same commit) or does it count on its own?
14. Is a version ever compared (e.g. refuse an older host app), or stay a label only as today?

**Release decision (Alex 2026-11-16):** the tester pre-release `trx64mc-main-13c2cde9` ships the board-tested bytes, versions shown as
`x.y.z-dev+13c2cde9`, not promoted ("A"). After it, `main` moves reliably to SemVer releases: "danach stellen wir den Kram hier auf Main jetzt
verlässlich auf Release per SemVer um, also das nächste Release wird eines mit Tag und reiner Versionsnummer, ohne diesen Branchkram dahinter"
(the next release is a tag with a plain version number, no branch/sha suffix). The release promotes the board-tested bytes (only the version
text changes; no rebuild, no second board test - Alex: "Das ist doch Ressourcenverschwendung"). Open for that step: `release_promote.py`
promotes the .cor files and the app but not the app inside the update files (.cfw/.ue2), so `release_mc.sh`'s installer check fails; the
promote step must cover every part. How (Alex 2026-11-16): "Sonst nimm die Gitea-Registry dafür, Artefakte einchecken per Pipeline-Step, und auschecken" - a release
pipeline step checks the tested package out of the Gitea registry, promotes every part and checks the release package back in.

## §4 Out

Nothing decided out yet.

## §5 Decisions taken while building

Built on branch `t221-semver` (2026-11-15). Files: `bundle.toml`, `contract/version.toml`, `host-app/version.toml`, `coreapp/version.toml` (D6/D7; the host gateware file is
under `contract/`, not `host/`, so a bump cannot move the host key); `tools/semver.py`, `ci/version.sh [bundle|hostapp|coreapp|host|with X.Y.Z]`, `tools/check_versions.py`,
`ci/test_version.sh`, `tools/release_promote.py`.

- **B1 (form).** A source file holds `x.y.z` only (lint, `tools/check_versions.py`); every build that is not a tag shows `x.y.z-dev+<sha8>`, `main` too (D3; the old
  `X.Y.Z+<sha8>` of `main` and `<branch>@<sha8>` are gone, `ci/test_version.sh` has negative controls for both). A tree without a commit (the dev tree of `tools/dev.sh`)
  shows `+00000000`. `ci/version.sh` has no python (the app build image has none; the package stage runs python).
- **B2 (stamps).** The apps carry three stamp fields (`TRXVSTAMP` own version, `TRXHSTAMP` host gateware, `TRXBSTAMP` bundle; `tools/stamp_version.py --marker`);
  the core app image (`coreapp.bin`, the blueprint `.app` copies, so also the app inside every `.cor`) carries its own `TRXVSTAMP`. `ci/package.sh` stamps all of them
  before a `.cor` or the `.txu` is packed; the large and imported `.cor` take `$out/coreapp.bin` (the stamped one), not the stage's cached image.
- **B3 (artifacts).** `.cor` version = `ci/version.sh with <core.toml version>` (`pack_cores_mc.sh`, `package.sh` large cores); `.txu` and `manifest.json` `version` = bundle
  version (D17); `manifest.json` has `parts` = host gateware, host menu, core app, and every core as read back from its `.cor` (D6). `tools/check_bundle.py` checks all three
  app fields against the manifest (its selftest passes). The Gitea package coordinates stay `<branch>-<sha8>` / `<branch>-latest` (the pipeline's lookup key,
  `ci/lib.sh publish_files`); the version is inside. OPEN-1 below.
- **B4 (release, D4).** `tools/release_promote.py <package dir>` rewrites `x.y.z-dev+<sha8>` to `x.y.z` in the apps, every `.cor` (manifest and the app inside, size and SHA-256
  following), the `.txu` (repacked), `manifest.json` and `SHA256SUMS`; selftest with a negative control (a release is not promoted twice). `tools/release_mc.sh` calls it right
  after the download (marker `.promoted`). The `.cfw`/`.ue2` of stage update carry their branch-sha identity and are made again from the promoted package, not patched;
  `release_mc.sh` still repacks the large cores itself as before (their `core.toml` version is already the release form).
- **B5 (host displays).** Boot menu version column: every row with a version shows it, large cores included (`full firmware` stays only for a full image that names none,
  e.g. a `.cfw`); chips: "contract N" removed (D12), `host <ver>` on a release, `host <ver> / <key>` on a test build (key lower-case hex; the "·" has no glyph in the font, it drew a box) (D14), `app <ver>`; splash, startlog, web hello (`app`,
  `host_version`), bug report ("Host app", "Host bitstream <ver>, key <key>"); update ask shows the `.txu` bundle version and, if it is older than the running bundle, a
  warning line and goes on (D19, `semver.h`, `txu_running_version`).
- **B6 (core app).** Splash, Home "Application", console and bug report show the core app SemVer; the host gateware version reaches it in a new trailing field of the
  hand-over page (`host_version`, own check word, older readers and older hosts unaffected); REST `/v1/info` has `app` and `host`, `/rpc` ping has `core_version`,
  `app_version`, `host_version`; the bug report names the host as `<ver>, key <key>`.
- **B7 (Vivado keys).** Everything except the last commit moves none: no file under `host/`, `cores/` or `vendor/` is touched (the host gateware version is `contract/version.toml`). The last
  commit (`cores/vc20/core.toml` version 0.9.0 -> 0.3.0) moves exactly vc20's own keys: its OOC key (`ooc_key`: `cores/vc20` is in its `sources`) and so its core key (`core_key`); measured
  with `ci/lib.sh` in raw mode, before/after in the commit message. The host key, c64ref's and every other core's keys are unchanged. `contract/` is in no key (`ci/lib.sh`, `host/layers.txt`). The trxapps key moves (apps changed; `ci/version.sh` is now one of its inputs because `gitinfo.c` is made from
  it), so every `.cor` is repacked; no bitstream is rebuilt.
- **B8 (dev tree).** Images of `tools/dev.sh` carry no version (no commit in the dev tree); `tests/hostapp/emu_host.sh` stamps copies before it runs, so its pictures show real strings.

### Pictures and ux-review (2026-11-16, ue2emu, flow `semver_flow` of `tests/hostapp/emu_host.sh`)

Found and fixed with the pictures: (1) the "·" in the host chip drew a missing-glyph box, now " / "; (2) the chip holder (640) dropped the app chip at 1080p/720p when the key
followed the host SemVer, holder now 770 at the 960 canvas; (3) a large core still showed "full firmware" (`hc_probe_state` never read the version of a `.cor` large core), now
`large_version()` reads it; (4) the version column was 104 px, a test build's "0.3.0-dev+85acf2b0" was cut, now 150 (compact 96: kept, the name keeps its room); (5) the
downgrade line was cut at 480p, now "Downgrade from the running <ver>."; (6) the flow did not add the cores (bundle check needs 12 s) nor press U, so the two pictures were
the file browser twice. The emulator has no host key register, so the key chip was drawn with a temporary forced key (not committed); the pictures show `/ 2BB1DE24`.

Known at 480p (640 canvas): a test build cannot show "app", "host ... / key" and the net/USB/SD chips together; the app chip is left out there (`chip()` drops what has no room,
a release build's short strings fit), and a large core's test version is cut to "0.3.0-dev+8..." (SemVer core visible). Not a regression of the 480p layout, a finding for Alex.

| # | Result | Evidence (scratchpad `t221/`) |
|---|---|---|
| S3 | PASS | `semver-list-{480p60,720p60,1080p}.png`: Amiga 1200 row `0.3.0-dev+85acf2b0` (1080p/720p full, 480p cut), no "full firmware" |
| S4 | PASS | same pictures: VIC-20 `0.9.0` |
| S5 | PASS | `semver-splash-1080p.png` (`0.2.0-dev+bd16dfdd`), app chip in the list pictures at 720p/1080p; app chip absent at 480p (see above); startlog/`stamp_version.py` not pictured |
| S6 | PASS | chips `host 0.2.0-dev+bd16dfdd / 2BB1DE24`, fits its holder at 480p, 720p, 1080p; no "contract" chip |
| S7 | PASS | `semver-update-{480p60,720p60,1080p}.png`: "Version 0.1.0-dev+0123abcd", amber "Downgrade from the running ..." line, the ask goes on |
| S8 | PARTIAL | core app Home "Application 0.2.0-dev+00000000" and "Core VIC-20 0.9.0" seen (`.ci/dev/t221-semver/emu/core-menu.png`, 1080p frame); core splash, F8 and console line not pictured; the date after "built" is empty in the dev tree |
| S10 | NOT PROVEN | the host web page needs the network shim; only code (`fill_snapshot` -> `app`, `host_version`) |

### Open (not decided; Alex must answer, one at a time)

- **OPEN-1 (D10): answered** (see D10; vc20 0.3.0, all others unchanged). The vc20 edit moves vc20's Vivado keys, see B7.
- **OPEN-2 (D12 consequence): answered 2026-11-16 (Alex, "the integer stays (recommended)"):** the `.cor` check keeps the contract integer (`contract/VERSION`), a number of its own beside the SemVer strings; nothing changes. Older text: The `.cor` check still compares the contract integer (`COR_HOST_CONTRACT` = 2, `contract/VERSION`). Folding it into the host gateware MAJOR with a
  start of 0.2.0 gives MAJOR 0, not 2: either the host gateware starts at 2.0.0 or the check stays an integer. The chip is gone either way (D12); the row refusal text "contract 3 != 2"
  stays until this is answered.
- **OPEN-3 (D15). Moved to T230** (2026-11-16: FW-1 answered by the firmware session, version from `manifest.json`; both pins carry `SEMVER=0.3.0`, the release `main-8f1dc184`). Older text: Imported trx64u/trx64g: `pack_import_cor.sh` reads a `SEMVER=` from the pin and falls back to the commit with a message; no pin has one. Proposal text for
  `docs/contracts/trx64u-firmware-handover.md` (not edited: that file changes only with TRX64-Ultimate): "TRX64-Ultimate publishes with every CI package a `version.txt` (one line,
  SemVer 2.0 `x.y.z`, no build metadata) beside `c64_basic.bit`; TRX64U-MultiCore copies it into the pin as `SEMVER=`. A change of the bitstream is a new version; a
  `-dev+<sha8>` is added by the MultiCore packer on a test build." Needs the other repo's side.
- **OPEN-4 (D16).** BUILT 2026-11-16 as mock-up t1b (T223 D7 §5: purple square right of the version, L/P; pictogram position switchable, default t1b). Older text: not built; until then a large core is no longer marked in the list (the version replaces
  "full firmware"). Mock-ups: `scratchpad/t221/badge-*.png`.
- **OPEN-5 (D19): answered 2026-11-18 (Alex, translated: "No warning, just do it"):** a `.cor` of an older version is copied and added (A) like
  any other, without a warning; nothing to build. The `.txu` update keeps its warning (D19). Older text: "Core install of an older version warns": the host has no install step for a `.cor` (it is copied by hand and added with A). Only the `.txu` update warns. Where the
  core warning belongs (adding an entry whose id is already in the menu with a newer version?) is not said.
- **OPEN-6 (D17): answered 2026-11-16 (Alex, "yes ok then", after asking whether a later move of lint and tests to GitHub with builds here by hook is hindered - it is not, the commit is the same on both):** Gitea package coordinates stay branch-sha. Older text: Gitea package coordinates stay branch-sha (see B3); changing them to the SemVer string touches `ci/lib.sh` publish/fetch and the update stage.
- **OPEN-7.** `bundle.toml` carries only the bundle version; the part versions are written into `manifest.json` `parts` at package time (a hand-kept list would be a second copy).

## History

- 2026-11-15: PROPOSED on Alex's request; §1 read from code, open questions Q1-Q14 listed.
- 2026-11-15: Q1-Q14 decided with Alex the same day (D3-D19); open: start version per core (one by one), the TRX64-Ultimate contract line (D15), the L/P badge mock-up (D16).
- 2026-11-15: status BUILDING; branch t221-semver (worktree ../wt-t221-semver); §5 holds what is built and what is open.
- 2026-11-16: pictures and ux-review of the host side (S3-S7), fixes see §5; S8 partial, S10 open.

### Render bench fixes on integ-1116 (2026-11-16)
- Area budgets: the chip holder is 770 wide at the 960 canvas (§5 pictures, fix 2), so the measured chip redraw is 30030 px (770 x 39), a deliberate widening, not an accidental redraw; `areas()` limits are now `770u * 40` (F1 chips) and `770u * 40 + 6000` (F7 net) instead of 640.
- The boot row of a converted large `.cor` shows its manifest version ("Test Vendor image|9.9|foreign|", D13 `large_version`) instead of "full firmware"; the render bench expects that.

- **Main run 2247 (2026-11-16):** the package stage stamped `out/coreapp.elf`, which `ci/package.sh` never copied out of the trxapps stage (only hostapp.app/.elf and coreapp.bin): `FileNotFoundError`. The ELF is copied now (branch runs build no package, so it showed first on main).
