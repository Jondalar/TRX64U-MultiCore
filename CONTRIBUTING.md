# Contributing

TRX64 MultiCore is a hobby project of a very small team. This page says what we take care of and what we do not,
so an issue or a pull request lands where it can be finished. The overview of all cores and their state is
[discussion #66](https://github.com/Jondalar/TRX64U-MultiCore/discussions/66).

## What we support

- **The host system.** The host gateware and the host application: boot menu, update, core folders and the
  multi-boot of the C64U / U64 Elite II.
- **The generic core application** (`coreapp/`). The application a core runs: menus from the core's TOML, media,
  settings, saves. It is the blueprint for building a core of your own; the experimental cores are its example
  implementations.
- **Official firmwares.** Every official firmware (`.cfw`) has to start from the host. If one does not, that is a
  fault of the host.
- **TRX64** (`trx64u.cor`, `trx64g.cor`). It is supported as a core here and standalone. Faults inside TRX64
  belong in its own repository, [TRX64-Ultimate](https://github.com/Jondalar/TRX64-Ultimate); faults in how the
  host starts it belong here.

For these, issues, enhancement requests and pull requests are welcome.

## Experimental cores

The other cores are experimental: Amiga (OCS/ECS), Amiga 1200, Apple IIe, Atari 800 XL, C128, C16/Plus4,
C64 with SuperCPU, CPC, Neo Geo, NES, PET, SNES, VIC-20, ZX Spectrum and ZX Spectrum Next. They show how a core
is built on the host and the generic core application. They are also an invitation to the community to make
something of them.

**Experimental cores come without any guarantee.** No fix, no answer and no release date is promised.

Report a fault in an experimental core with the form "Experimental core: bug". Testers of the community triage
these reports:

1. A general fault, or a fault in several programs: a tester reproduces it and sets `confirmed`. We take our
   work from these issues, one at a time.
2. Information is missing: the issue gets `needs-info`.
3. A fault in one program or demo only, or with rare hardware: the issue gets `edge-case` and is closed. It stays
   searchable. When more programs show the same fault, the issue is opened again.

The demos and the Port Diagnostics core have no issue form. Use
[Discussions](https://github.com/Jondalar/TRX64U-MultiCore/discussions) for them. A pull request that fixes
a fault is welcome for every core.

## Before you open an issue

- **Install the current release** and try again. If an open issue covers it already, comment there.
- **One fault per issue.** Do not add new findings to an issue that was closed after a fix; open a new one.
- **Read the manual chapter** for the function ([docs/manual](docs/manual/00-index.md)).

## What an issue must contain

We take an issue only if we can reproduce it end to end, exactly as you saw it. The
[forms](https://github.com/Jondalar/TRX64U-MultiCore/issues/new/choose) ask for all of it:

- **Release:** the name from the release page, for example `0.2.0`.
- **Board:** C64 Ultimate or Ultimate 64 Elite II.
- **Core:** in the experimental core form, and how wide the fault is.
- **The exact steps from switching on**, one per line: menus, keys, files.
- **What you expected and what happened.**
- **The report file:** F8, "Bug report". It is in `/SD/report/<date>.txt`, or at `http://<board>/v1/report`.
- **Photos or a short video of the screen**, when the fault shows on the screen.
- **The media you used, by name only:** title, file name and where it came from.

## What must never go into an issue or a pull request

- **No ROMs, Kickstarts, BIOS or firmware files**, no disk, tape or cartridge images, and nothing made from them.
  Name them and where they came from; that is all we need.
- **No official firmware package**, and no part of one.
- **No backup files** the installer wrote (`TRXBK*`). They contain your settings, including the network password.

## Pull requests

The work happens on our own Gitea; GitHub receives the history from there. A pull request is therefore a patch
we apply on Gitea with your name on the commit, run through our CI and ship in a release. The pull request is
then closed, naming the build that carries it. **Closed does not mean refused**; a refusal says so, with the
reason.

- Keep one change per pull request, and say what it fixes and how you checked it on the board.
- For a core: changes inside `cores/<core>/` and its core application (`coreapp/cores/<core>*`) are the easiest
  to take. A change to the host (`host/`, `host-app/`) or to files every core reads (`cores/common/`, `contract/`)
  needs a reason and is checked against all cores.
- Third-party code keeps its licence ([LICENSING.md](LICENSING.md), [PROVENANCE.md](PROVENANCE.md)). Say where
  code came from and under which licence.
- We write the specification for a change ourselves; describe the behaviour you want in the pull request.
