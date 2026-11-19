# T239 — This repository delivers host, app and SDK; cores are built outside it and found through a registry

**Status:** PROPOSED (2026-11-19, Alex); the architecture discussion is running.
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

Open (one line, as in the TODO table): PROPOSED 11-19 (Alex vision D1); open: DFX or large outside, other cores, registry, CI/CD

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
- **Parts:** P1 the product boundary (host, app, SDK); P2 cores outside this repository (TRX64 first, the others open); P3 the
  registry for host and cores; P4 CI/CD with GitHub as the main channel.

## §2 Scenarios

Written after the decisions.

## §3 Decision

- **D1 (Alex, 2026-11-19): the vision.** This repository builds host + app + SDK. The TRX64 core moves out to TRX64-Ultimate. A
  registry offers the host and the cores centrally, as a convenience. Large cores may come from anywhere. Open: the other cores.

## §4 Out

Nothing decided yet.

## §5 Built

Nothing yet.
