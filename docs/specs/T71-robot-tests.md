# T71 — Robot Framework tests (BDD) for TRX64 Ultimate

Decision: [ADR-0052](../adr/0052-robot-framework-bdd-tests.md). TODO rows: T71.1 (PoC, M2 since 2026-10-09, Alex: "T71.1 == M2"), T71 (MVP and full
migration, M2).

## 1. Goal

One set of behaviour scenarios that proves a feature at every stage, from the first push to the merge into
`main`, against the emulator and against the board, written once.

Alex, 2026-10-09:

> Für App Features:
> - CI läuft um Build nachzuweisen
> - emulator wird mit App gestartet
> - BDD (Cucumber) tests via API gegen Emulator
>
> dann integration Merge
> - BDD Integration tests laufen
> - Regression Suite läuft
>
> UNd dann
> - BDD Tests auf HW
>
> dann merge nach Main

> Doch, weil wir uns in M2 von Gideon lösen ?! UNd dann migriere ich die Tests lieber nach Gherkin und
> Robotframework

> Dann Robot ... im TRX64 Ultimate Folder ... PoC noch in M1 > MVP anfang M2 > volle Umstellung als Acceptance
> Criteria für Erreichen von M2

Non-goals: replacing the RTL benches (`ci/sims.txt`, T31) or the host unit tests next to the code. They stay
as they are; Robot covers what a user or a client of the API sees on a running machine.

## 2. What exists (2026-10-09)

- **App repo** (`software/ultimate-app`, our fork of Gideon's 1541ultimate): `tests/e2e` (Gideon's 88 Python
  files plus ours), driven by `./run-tests` against a device over REST (port 80), Telnet menu (23), FTP (21),
  the DMA socket (64), syslog and UART. Our ue2emu suites run them against the emulator
  (`build/ci_emulator_suites.sh`). Host C++ unit tests sit next to the code (`software/*/tests/`).
- **This repo**: `tests/*.py` and `tests/*.sh` (Lorenz, REU round trip, app C64 test, T40 modes, trxmon client,
  cartridge tests, UE2 self-test against a fake board); RTL benches in `ci/sims.txt`.
- **CI** (T31): `lint -> sims -> app -> bit-c64u -> package`. No stage runs the app on ue2emu, and there is no
  hardware-in-the-loop runner (done audit 2026-10-04, T31 row).
- **Board sessions** are collected by hand in `docs/archive/board-tests/board-tests-pending.md`.
- **ue2emu runs our app** (Alex, 2026-10-09: "ue2emu ist schon angepasst, kann sein dass er nochmal
  nachjustiert werden muss aber im Grunde geht das schon. Auch die LVGL Layer laufen"): the emulator target
  exists, including the LVGL layers; which TRX64 registers it models in detail is still to be listed (Q1).

Gideon's tests are a toolbox, not a specification of TRX64 Ultimate. Parts of them do not apply to us
(`cartridge@computer` targets, U2/U2+ paths, REST routes that ADR-0002 freezes).

## 3. Decisions

[ADR-0052](../adr/0052-robot-framework-bdd-tests.md) in short:

1. **Robot Framework**, in its own BDD style (`Given/When/Then` prefixes on keywords with embedded arguments)
   in `.robot` files. No Cucumber runner and no `GherkinParser`: the package `robotframework-gherkin-parser` has
   had no release since 0.3.2 (2024-02-05).
2. **The tests live in this repo**, under `tests/robot/`.
3. **One scenario, two targets.** The same `.robot` file runs against ue2emu and against the board; the target
   is a variable (`--variable TARGET:emu|hw`), not a second copy.
4. **The Python e2e suites are migrated, not kept beside Robot.** In M2 we decouple from Gideon (ADR-0003); a
   migrated area drops its old suite from the gate.
5. **Full migration is an exit criterion of M2** (section 7.3).

## 4. Layout

```
tests/robot/
  README.md                 conventions (tags, naming, how to run)
  suites/<area>/*.robot     scenarios, one directory per feature area
  resources/*.resource      user-level keywords (Given/When/Then vocabulary)
  libraries/                Python keyword libraries
    trx_target.py           facade: picks the driver from ${TARGET}
    drivers/emu.py          ue2emu: start with the app, keys, screen, network API
    drivers/hw.py           board: SRAM bitstream (tools/load.sh), API on the board's address, JTAG via trxdbg.py
    api/                    clients: own API (WS JSON-RPC, ADR-0002), REST while it is still needed
    ui/                     menu driving: keys and screen text, on both targets
  run.sh                    thin wrapper: robot with the right variables, tags and output directory
```

- **Keywords speak the user's language** ("the user mounts", "the screen shows"), the libraries speak the
  machine's. A transport change (REST to WS JSON-RPC) changes a library, never a scenario.
- **The existing Python code is reused as library code** (`tests/lib` of the app repo, `tests/trxmon_client.py`,
  `tools/trxdbg.py`): the migration cost is in the scenarios, not in the transport.
- **Both doors.** A feature the user reaches through the menu gets a scenario that drives the menu (keys and
  screen), not only the API: the user at the machine wins over the API, and a state the user cannot leave by the
  menu is a bug the API path never sees.
- **Timing is stated with tolerances** ("within 500 ms"), never as exact cycle counts: the emulator is
  deterministic, the board is not.

## 5. Tags

| Tag | Meaning |
|---|---|
| `emu` | runs on ue2emu |
| `hw` | runs on the board |
| `hw-only` | needs the bitstream (ring, monitor, turbo, cartridge port …) and ue2emu does not model it yet. The tag is removed when ue2emu models the feature (hardware first, emulator after). |
| `ui`, `api` | which door the scenario uses |
| `smoke` | the minimum that says "the machine is alive and drivable" |
| `regression` | part of the regression suite on the integration branch |
| `T<id>` | the TODO row the scenario proves (`T20`, `T57` …); a row's acceptance is its tagged scenarios |
| `setup-<name>` | board setup it needs (`setup-cart-ef`, `setup-reu`, `setup-iec` …), so one board session runs one setup at a time |
| `alex` | needs Alex at the machine (flash writes, real carts, power). The hardware driver refuses these unless `--variable ALEX_PRESENT:yes` is given. |
| `slow`, `rc` | long runs (Lorenz full suite); `rc` only before a release candidate |
| `wip` | excluded from every gate |

## 6. Stages

| Stage | When | What runs | Where |
|---|---|---|---|
| Feature | every push to a feature branch | T31 pipeline (build) + `bdd-emu`: `smoke` and the branch's own `T<id>` scenarios with tag `emu` | CI, after `app`, in `u64-e2e-env:ci` |
| Integration | merge into the integration branch (`integ-*`) | all `emu` scenarios + `regression` (until an area is migrated: its old Python suite) | CI |
| Board | before the merge to `main`, per integration commit, in one session per setup | all `hw` scenarios not yet passed on that commit, grouped by `setup-*` | the Mac at the board (`tests/robot/run.sh --target hw`) |
| Main | merge `integ-*` -> `main` | nothing new: the merge needs the three stages green on the same integration commit, shown as one merged report (`rebot` of emu and hw) | – |

Rules on the board stage (unchanged project rules, enforced by the driver): bitstreams go into SRAM only;
no flash write and no power cycle without Alex (`alex` tag); the board is shared with daily TRX64 work, so a
board run is started by Alex or with his OK, never by CI on its own.

Reports: `output.xml`, `log.html`, `report.html` of every stage are kept with the run (CI: published with the
package version; board: next to the integration commit's report). A failed board run is repeated with
`--rerunfailed`, not as a whole session.

## 7. Plan and acceptance criteria

### 7.1 PoC — T71.1, in M2

Small and beside the existing gates; nothing in M1 waits for it.

- `tests/robot/` with the layout of section 4, the facade and both drivers, `run.sh`, `README.md`.
- At least three scenarios on existing M1 functions:
  - one `api` scenario on both targets (candidate: REU round trip, today `tests/reu_test.py`);
  - one `ui` scenario on both targets (candidate: mount an EasyFlash CRT from the menu and see its title screen);
  - one `hw-only` scenario (candidate: Timewarp rewind by N frames restores a known screen).
- List which TRX64 registers ue2emu models and where it still needs adjusting (question Q1); the answer fills
  the `hw-only` list.

Acceptance: the same `.robot` file passes on ue2emu and on the board (in a board session Alex has agreed to);
one CI job (`workflow_dispatch` only, not in the pipeline yet) runs the `emu` set and publishes the report.

### 7.2 MVP — T71, at the start of M2

- `bdd-emu` stage in `pipeline.yml` after `app` (section 6, feature and integration rows); a failure fails the
  run.
- Drivers for the own API (WS JSON-RPC) and for the menu on both targets.
- Board stage runs from `run.sh --target hw`; `docs/archive/board-tests/board-tests-pending.md` is generated from the `hw` scenarios
  that have not passed on the current integration commit.
- **Inventory**: every Python suite of the app repo and of this repo, per area, marked *migrate*, *drop* (with
  reason: U2 path, `cartridge@computer`, frozen REST) or *stays outside Robot* (host tool, RTL bench). The table
  goes into this spec (section 8).
- Every new M2 row states its acceptance as `T<id>` scenarios in its spec.

Acceptance: the pipeline runs `bdd-emu` on every push; one integration merge has gone through feature,
integration and board stages and reached `main` by the rule of section 6.

### 7.3 Full migration — exit criterion of M2

- Every area of the inventory is migrated or dropped; no Python e2e suite is part of a gate any more.
- Every M2 row's acceptance is its `T<id>` scenarios, green on ue2emu (where modelled) and on the board.
- Merges to `main` follow section 6 only.
- `docs/archive/board-tests/board-tests-pending.md` is generated only, no longer edited by hand.

M2 is not reached while any of these is open.

## 8. Inventory

Filled in by the MVP (section 7.2).

## 9. Open questions

- **Q1** Partly answered (Alex, 2026-10-09): ue2emu runs our app, the LVGL layers included; it may need
  adjusting. Still open: which TRX64 registers (ring, monitor, turbo, cartridge port, UCI …) it models in detail.
  Decides the first `hw-only` list; the TRX64-FW session answers it.
- **Q2** Board stage: only from the Mac by hand, or later a Gitea runner on the Mac (the hardware-in-the-loop
  runner the done audit lists as missing)? Alex's call.
- **Q3** The app repo's own CI (`software/ultimate-app`): does it keep running Gideon's suites until the
  migration ends, or only build? Alex's call.

## Status and history

- **2026-10-09** Q1 partly answered: ue2emu runs our app and the LVGL layers (Alex).
- **2026-10-09** Spec created after Alex's decision (ADR-0052). Nothing built yet.
