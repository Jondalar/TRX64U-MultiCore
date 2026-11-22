# T239 — This repository delivers host, app and SDK; cores are built outside it and found through a registry

**Status:** EPIC, PROPOSED (2026-11-19, Alex); D1-D16 decided; the children T239.1-T239.4 carry the work (§2).
**Repo:** TRX64U-MultiCore (host, app, SDK); TREX/TRX64-Ultimate (the TRX64 core); the registry (place open).
**Number:** T239.
**Origin:** Alex, 2026-11-19 (translated): "We already have as a 'big' todo: the abstraction of the app (framework, services,
build, etc.). On top of it: how we manage multiple cores; how we do CI/CD now with GitHub as the main channel to the outside
world. That needs an architecture discussion, which I would like to have in peace today." Asked who builds cores in the next 6-12
months: "My vision: we build our part, host + app + SDK, we move our core out to TRX64FW-U (that is practically a 'foreign'
repo already) and the other cores I do not know yet. Plus a registry for cores and the host, so that one can get them
'centrally' as a convenience. Large cores anyone can download somewhere else anyway, that does not matter then."
**Depends on:** T192 and T213 (app layers, D24), T221 (SemVer per deliverable), T238 (support model), T100 D8 (releases on GitHub).
**Milestone:** after release 0.2.0.

Open (one line, as in the TODO table): Epic 11-19; D1-D16; A (T239.1) done 11-22; B (T239.3) started 11-22 before D (.3 D16); .4 D1-D6 decided

---

## §1 The problem

Baseline read on 2026-11-19 (three read-only agents over TODO, specs, ADRs and code):

- **App.** The target is decided (T192 D24, T213 D1-D5: an API TOML per bitstream, service modules in C chosen by the TOML, a UI
  only from declarations), but T213 has no child spec and no code. The D24 ledger holds 145 files and 44,132 lines of per-core
  code; only trx64u has a TOML menu (its ledger exceptions are about 6,000 lines). The SDK for outside developers (T131) is not
  written.
- **Cores.** Release 0.2.0 ships 20 cores: 5 partial (DFX) cores, 13 large cores with their own copy of the host files, and the two
  imports trx64u and trx64g built in TRX64-Ultimate. A new core touches about 20 places outside `cores/<id>/`; a host fix reaches
  the 13 large cores only by copying it into each by hand; no core has an owner.
- **CI/CD.** Everything runs on private infrastructure (Gitea, builder, NAS, the Vivado image, the content-keyed cache, the
  heavy.lock shared with TRX64-Ultimate). GitHub receives a filtered history replay and hand-made releases; outside pull requests
  are applied as patches on Gitea. No decision covers GitHub Actions; T100 D8 says "until I want to move the pipeline there".
- **TLS for a registry on the board** (read 2026-11-19 in GideonZ/1541ultimate, read only): our host has no TLS client (lwIP
  without TLS). Upstream issue #952 (open since 2026-10-04) plans TLS client sockets on the ESP32 for C64 programs (UCI `$21`
  OPEN_TLS, TLS 1.3, mbedTLS of ESP-IDF 6.0.3, a vendored Mozilla root store, five PRs), first for the U64/U64 Elite MK1
  (`raw_u64`); the Elite II and C64U firmware (`u64ctrl`) get only `GET_RANDOM`. The upstream maintainer stopped it until an
  approach is agreed and prefers TLS on the Ultimate itself for every product; the issue notes that the main CPU has no entropy
  source (`LWIP_RAND()` is an unseeded `rand()`). Upstream `u64ctrl` already uses HTTPS inside the ESP32 for its Wi-Fi recovery
  from a signed manifest (commits 56689d2d8 of 2026-10-18 and dc3a31f24, Mozilla roots), not as a service for the main CPU; its
  identity is V1.29. We build no ESP32 firmware (our updater builds with `NO_ESP=1`); we own the host gateware.
- **Parts:** P1 the product boundary (host, app, SDK); P2 cores outside this repository (TRX64 first, the others open); P3 the
  registry for host and cores; P4 CI/CD with GitHub as the main channel.

## §2 Scenarios

The children carry the scenarios. Order by D13 (Alex 2026-11-19: "machst Du einen Plan bitte mit Epic, und dann Todos als Specs?").

| Child | Step | What is true when it is done | Builds on | Spec |
|---|---|---|---|---|
| T239.1 | A | GitHub is the development home of host, app and SDK: pull requests and CI run there, private material sits in a Gitea side repository | D11 | [T239.1](done/T239.1-github-home.md) |
| T239.2 | D | Every host gateware release publishes its static checkpoint and a core template, so DFX cores are built outside this repository | D2, D3; T239.1 | [T239.2](T239.2-platform-checkpoint.md) |
| T239.3 | B | The app core has an API for sub-apps and libraries; trx64u and the drive libraries ride on it, and the SDK ships it | D9, D10; T239.1, T239.2 | [T239.3](T239.3-app-core-sdk.md) |
| T239.4 | C | The host menu lists the central registry over HTTPS, installs a core or a host update from it, and refuses what does not check out | D3, D5-D9; T239.2, T239.3 | [T239.4](T239.4-registry-tls.md) |

Each child lists its open questions for Alex in its §3; none is built before they are answered. State 2026-11-19, evening: every
question of every child is answered (T239.1 D1-D20, T239.2 D1-D5, T239.3 D1-D5, T239.4 D1-D6); T239.1 is the next to build.

## §3 Decision

- **D1 (Alex, 2026-11-19): the vision.** This repository builds host + app + SDK. The TRX64 core moves out to TRX64-Ultimate. A
  registry offers the host and the cores centrally, as a convenience. Large cores may come from anywhere. Open: the other cores.
- **D2 (Alex, 2026-11-19): the SDK builds outside cores of both kinds; the host gateware becomes a platform.** Outside DFX cores
  are built like ours: `host/core.tcl` routes the core into the partition of the host's `static_routed.dcp` (TRX_HOST_DCP). So
  the SDK ships, per release of the host gateware, exactly that checkpoint (today it lives only in the builder cache), with
  `host/core.tcl`, the contract (`contract/`) and a `core_sources.tcl` template; Vivado 2024.1 as the checkpoint's. A partial fits
  one host build only: the host refuses a manifest `host_key` of another build (`host-app/software/hostapp/cor.cc:285`, T84
  D5b). So the host gateware changes rarely, and every change means all DFX cores are built again; host app and core app
  releases do not touch it. The registry records per DFX core the host key it fits. Alex, on "the charm of DFX is that we ship
  host + bitstream, so someone can build their DFX core against it ... everything is open source" and the consequence above:
  "Ja, das heisst es" (yes, that is what it means). Large cores stay self-contained (their own host copy, back to the host menu
  by IPROG to flash address 0, `coreapp/src/hw.cc:498`).
- **D3 (Alex, 2026-11-19): a DFX core that does not fit the installed host stays visible, marked red.** Asked whether such a core
  disappears from the list: "They do not disappear, they are red" (translated).
- **D4 (Alex, 2026-11-19): the maintainer of a core builds it again for a new platform; no central rebuild of other people's
  cores.** Alex (translated): "Only someone who has Vivado can build himself and develop a core" (with a community core
  developer as the example). Asked against a central rebuild in our CI (other people's Tcl on our builder) and a mix.
- Idea under discussion (Alex, 2026-11-19, translated): "We could take cores into the monorepo/subrepos, the maintainer could
  build himself." Not decided yet.
- **D5 (Alex, 2026-11-19): a core in its own repository (subrepo) is resolved through the core registry, not pinned, built or
  shipped by us.** Asked whether such a core is pinned here like trx64u today (version + SHA-256, its maintainer builds and
  delivers the .cor): "Core im Subrepo würde ich gerne mit einer Core-Registry auflösen ... statt pinnen und mit bauen und
  ausliefern" (translated: I would like to resolve a core in a subrepo with a core registry, instead of pinning and building and
  shipping it along). Consequence to confirm: TRX64 (D1, already a foreign repository) then comes from the registry, and the
  release package stops carrying trx64u/trx64g through release/imports/*.pin.
- **D6 (Alex, 2026-11-19): the host reads the registry itself, from the host menu.** Asked board, PC, or PC first: "am liebsten
  wäre mir dass wir das aus dem Hostmenu machen" (translated: I would most like us to do it from the host menu), "because only
  we need it". The host therefore needs a TLS client (GitHub serves HTTPS only).
- **D7 (Alex, 2026-11-19): the hardware TLS needs goes into the null core.** Alex (translated): "the empty 'null core' would be
  exactly the place to stuff things into the hardware, right?" Checked: the null core (T123) sits in the partition while the host
  menu runs (the flash image at 0x0 is host + null) and already has a register block the host app reads (`cores/null/null_regs.v`:
  ID, build stamp, pixel page). A true random generator (ring oscillators with health tests, seeding mbedTLS's DRBG) goes there,
  AES/SHA only if a measurement asks for them. A new null core leaves the static routing and the host key as they are: no DFX core
  breaks. On the upstream side (#952) the U2 and U2+ FPGAs are 99 % full, so an all-Ultimates solution has no room; this one
  serves the C64U and U64 Elite II with our own host only.
- **D8 (Alex, 2026-11-19): one central registry first; several later, if a community forms.** Asked between one central
  registry (a GitHub repository with an index file, entries by pull request that we review), several registries (as MiSTer's
  downloader databases, added by the user in the host menu) and both: "Lieber erstmal 1, falls sich überhaupt eine Community
  findet, dann später 3" (translated: rather 1 first; if a community forms at all, then 3 later).
- **D9 (Alex, 2026-11-19): TRX64 comes from the registry; our release package stops carrying trx64u/trx64g.** Until the host
  reads the registry, the pins in `release/imports/` stay and TRX64 stays in the package. Alex (translated): "Yes, and that also
  fits that we abstract/cut the SDK and UI accordingly. Then the heap problems are where they belong": trx64u's menu TOML and
  its code in `coreapp/cores/trx64u/` (the D24 ledger exceptions of T228-R20) and its table and memory limits (STATES_MAX and
  OPS_MAX were raised on 2026-11-18 for it) belong to TRX64-Ultimate, built against the SDK.
- **D10 (Alex, 2026-11-19): data by default, a core's own library where no generic module fits; one app core with an API for
  sub-apps and libraries.** Asked between data only, data plus the core's own `.u2a` library, and both: "Auf jeden Fall 3 ...
  eine sauber abgegrenzten App-Kern mit API für 'Sub-Apps' und 'Libraries'. Und wir bauen das für unseren Teil selbst"
  (translated: definitely 3, a cleanly delimited app core with an API for sub-apps and libraries, and we build that for our part
  ourselves). "TRX64U has what it needs and does it individually in its own place. Things like 1541/71/81 benefit the other
  Commodore cores, so TRX64U gives it back into the central repo as a library with source. The VIC-20 then uses it to build its
  app." This is layer 2 of his three-layer model (T192 D24: core UI <-> core service with modules <-> bitstream). Today the
  drive library `app/lib/cbm1541/` (T104.1 D15) is source in this repository and built into the core app for VC-20, C264 and
  C64, not loaded; the `.u2a` loader exists (T104.1 D10-D12: `libfmt.h` format 3, `libapi.h`, `lib_register`). D24 stays: no
  per-core code in `coreapp/` or `uicore/`.
- **D11 (Alex, 2026-11-19): GitHub becomes the development home of host, app and SDK.** Pull requests and CI run there; GitHub
  Actions uses our builder as a self-hosted runner, Vivado stays on our machine. Gitea keeps only private material as a side
  repository (ROMs, bench media, session material); the history replay and its filters (`tools/github_snapshot.py`) go away.
  Asked against "Gitea stays home, GitHub mirrors" and "software on GitHub, gateware on Gitea", with two facts: Vivado does
  not fit GitHub-hosted runners (tens of GB, AMD's licence lets us not publish the image), and a self-hosted runner on a public
  repository may build pull requests from forks only after approval. Alex (translated): "Yes, 2 sounds the most sensible."
  Price: ROMs and bench media leave the tree. Changed by T239.1 D11-D17 (Alex, 2026-11-19): the builder carries no GitHub
  runner; it builds only Vivado, pulled through a Gitea pipeline, and stores the bitstreams on GitHub by content key; every
  other step runs on GitHub's runners; the ROMs the benches need come from their upstream on demand.
- **D12 (Alex, 2026-11-19): our 15 experimental cores stay in this repository; whoever adopts one chooses where it lives.** Our
  CI builds and packs them as today. An adopting maintainer either keeps the core here with his folder under CODEOWNERS, or
  moves it to his own repository, from where it comes through the registry (D5). Alex, on "stay as reference cores" versus "stay
  until adopted, then move": "1 und 2 widerspricht sich doch gar nicht" (1 and 2 do not contradict each other), then "Ja" to
  the combination.
- **D13 (Alex, 2026-11-19): the order of work is A, D, B, C.** A: GitHub becomes the home (D11). D: the platform checkpoint is
  published with each host gateware release (D2). B: the app core with its API for sub-apps and libraries, the SDK cut (D10).
  C: the registry with TLS in the host and the TRNG in the null core (D6-D8). Reason given with the question: after A nothing
  has to go through the export any more. Alex: "passt" (fits), and asked for an epic with the todos as specs.
- **D14 (Alex, 2026-11-22): a new community core is built in its contributor's own repository and comes in through the registry
  (D5), not into this repository.** On pull request #102 (TI-99/4A, from a fork), translated: "we can look at #102 sometime, but
  it would be one more core with us, and I actually want [the author of #102] and whoever wants to build a core themselves in a
  repository and then fill the registry." Our experimental cores stay under D12.

- **D15 (Alex, 2026-11-22): one repository until T239.3 is done; then the layout is decided again.** Asked between one
  repository as today (D12), a platform repository (host, SDK, shared libraries) next to one monorepo for our cores, and one
  repository per core. Recommended: the first until T239.3 is done, because T239.3 changes the SDK and every core together
  (T239.3 D8-D10). With it: CI builds our cores only against the SDK package and the published checkpoint, as it would build an
  outside core; a lint stops access past the SDK; every core goes into the registry on its own (T239.4). Alex (translated): "Ok,
  then we leave it like that."
- **D16 (Alex, 2026-11-22): the SDK documentation lets an outside developer build alone.** Alex (translated): "Important: the
  documentation of the SDK must enable [an outside developer] and others to build by themselves." This applies to T239.2 (the
  checkpoint and the core template) and to T239.3 (the app SDK).

## §4 Out

Nothing decided yet.

## §5 Built

Nothing yet.
