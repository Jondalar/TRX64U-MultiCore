# 0052. Robot Framework BDD tests; full migration is an exit criterion of M2

Date: 2026-10-09

Status: Accepted

## Context

The functional tests of the firmware are Gideon's Python e2e suites in the app repo (REST, Telnet menu, FTP,
DMA socket), run against a device or, in our CI, against ue2emu. This repo's own tests are loose Python scripts
and RTL benches. Nothing proves a feature from the first push to `main` on both the emulator and the board with
the same test, and board sessions are collected by hand. In M2 we decouple from Gideon (ADR-0003); keeping his
suites unchanged as our regression is no longer the goal.

Options for the BDD layer: Cucumber-style `.feature` files run by Robot through `robotframework-gherkin-parser`
(no release since 0.3.2, 2024-02-05), `pytest-bdd`, or Robot Framework's own BDD style.

## Decision

Alex, 2026-10-09: "Doch, weil wir uns in M2 von Gideon lösen ?! UNd dann migriere ich die Tests lieber nach
Gherkin und Robotframework"; after the comparison: "Dann Robot ... im TRX64 Ultimate Folder"; and "PoC noch in
M1 > MVP anfang M2 > volle Umstellung als Acceptance Criteria für Erreichen von M2".

- Robot Framework in its own BDD style (`.robot` files, Given/When/Then keywords), in this repo under
  `tests/robot/`. No Gherkin parser.
- One scenario runs against ue2emu and the board; the target is a variable.
- Stages: feature push (build + emulator scenarios), integration merge (all emulator scenarios + regression),
  board (hardware scenarios), then merge to `main`.
- The Python e2e suites are migrated area by area and leave the gates; none is kept beside Robot.
- PoC in M1 (T71.1), MVP at the start of M2 (T71), full migration is an exit criterion of M2.
- Amended 2026-10-09 (Alex: "T71.1 == M2"): the PoC T71.1 moves to M2 as well; M1 does not wait for any Robot Framework work.

## Consequences

- New spec T71 (docs/specs/T71-robot-tests.md) with plan and acceptance criteria per phase.
- M2 is not reached until the migration of T71 §7.3 is complete.
- The board stage stays under the existing rules (SRAM only, no flash or power cycle without Alex).
- RTL benches and host unit tests are not affected.

## Links

TODO T71.1, T71; docs/specs/T71-robot-tests.md; ADR-0002 (own API), ADR-0003 (milestones); docs/specs/T31-ci-pipeline.md
