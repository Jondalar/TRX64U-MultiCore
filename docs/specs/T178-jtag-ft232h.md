# T178 — The JTAG tools (trxdbg, load, jtag_stat, partial load) work through an FT232H as well as the USB-Blaster

**Status:** DECIDED (2026-11-03, Q1-Q3 answered by Alex); build not started.
**Repo:** TRX64U-MultiCore — `tools/` (JTAG tools), `docs/board-tooling.md`; the cable layer comes from
`chrisgleissner/1541ultimate-tools` (our fork `Jondalar/1541ultimate-tools`, Gitea `TREX/1541ultimate-tools`).
**Number:** T178.
**Origin:** Alex, 2026-11-03, after GitHub discussion #29 (a tester revived a board with an FT232H and Gideon's
`recovery/u64ii/recover.py`; our tools could not help because they speak only the USB-Blaster): "make yourself a spec, the
trxdbg stuff should be able to use the FT232H too ... makes sense anyway. You can look in Chris's repo (1541ultimate-tools)
whether we can copy/recycle something from there" (translated).
**Depends on:** T177 (recovery boot; its §4 leaves a broken host app or bitstream to the JTAG cable).
**Milestone:** M3.

---

## §1 The problem

- **Who:** testers and developers with a C64U / U64 Elite II and a JTAG cable. The cable most testers buy is an FT232H
  board (Gideon's recovery README names the Adafruit FT232H); we use an Altera USB-Blaster.
- **What happens today:** every JTAG tool of ours opens the USB-Blaster by name:
  - `tools/load.sh` calls `openFPGALoader -c usb-blaster -m`;
  - `tools/trxdbg.py` and `tools/jtag_stat.py` call `u64ii_bscan.Blaster.open()`;
  - `tools/t79/partial_load.py`, `tools/t79/icap_load.py`, `tools/s9_series.py` and `tools/legacy_boot/g4.py` do the same
    or call openFPGALoader with `-c usb-blaster`.
- **Second gap:** these tools import `u64ii_jtag` / `u64ii_bscan` from `~/Development/1541ultimate-tools/tooling`
  (`U64II_TOOLING`), a checkout outside this repository on branch `jtag/blaster-sample` of our fork. A tester has neither.

Parts:
- **P1 Cable choice.** One way to name the cable (USB-Blaster or FT232H) shared by all tools, with a slow default clock for the
  FT232H (discussion #29: 3.3 s for 2.1 MB at a requested 3 MHz, i.e. the requested clock did not apply; jumper wires).
- **P2 Cable layer source.** Where `Tap`, `Mpsse`, `Blaster` come from.
- **P3 Load check.** Every bitstream load reports DONE (openFPGALoader `done 1`, or the IR capture bits) and stops otherwise;
  every DDR2 upload reads back (trxdbg already does, l. 650-664).
- **P4 Docs.** Wiring of an FT232H to P5 and the commands, in `docs/board-tooling.md`.

### What Chris's repo already has (read 2026-11-03, `upstream/main` 62fb2fe)

- `tooling/u64ii_jtag.py` carries both cables: `Mpsse` (FT232H, `open(url, frequency)`, l. 220), `Blaster` (l. 332, our
  USB-Blaster class, merged upstream from our fork on 2026-10-08) and `open_cable(url, frequency)` (l. 517): `blaster`,
  `blaster:<serial>` or an `ftdi://altera:` URL give the Blaster, anything else the FT232H. Default URL
  `U64II_JTAG_URL` or `ftdi://ftdi:232h/1`. This is P1 and P2 as is.
- The same module's header documents the FT232H wiring to P5 (D0 TCK pin 1, D1 TDI pin 9, D2 TDO pin 3, D3 TMS pin 5, GND
  pin 2 or 10; 3.3 V not connected) and Gideon's USER4 client (register 0 = 0xDEAD1541, register 2 bit 7 = CPU reset,
  0xFFF8/0xFFFC boot magic). It runs an application on Gideon's bitstream with or without reconfiguring and checks the SRAM
  configuration (commit b19205f "Require SRAM configuration success").
- `tooling/jtag_verify.py` (commit dbf92b7): a safety-gated cable verifier for USB-Blaster and FT232H.
- The repository has no licence file. Using it as a dependency (as today) needs nothing; copying its code into this
  repository needs Chris's consent.

## §2 Scenarios (draft; proofs fixed once §3 is decided)

- S1 `trxdbg.sh regs` through an FT232H on our host bitstream reads the same registers as through the Blaster. Proof: board,
  both cables in turn.
- S2 `load.sh <bit>` through an FT232H reports `done 1` and loads; with TDI and TMS swapped on purpose (the #29 wiring) it
  stops with a load failure instead of going on. Negative control: the swapped wiring must fail S2. Proof: board.
- S3 `trxdbg.sh riscv hostapp.bin follow` through an FT232H starts the host app from DDR2, probes read back. Proof: board.
- S4 `jtag_stat.py` through an FT232H gives the same IDCODE/STAT/BOOTSTS as through the Blaster. Proof: board.
- S5 No cable named: the tools behave as today (USB-Blaster). Proof: the existing board steps unchanged.
- S6 Host tests of the cable choice (URL → class) without hardware. Proof: a unit test; a URL mapped to the wrong class must fail.

## §3 Decision

- **D1 (Q1)** Pin our checkout to Chris's repository (`upstream/main`, a fixed commit) and use its `open_cable`. What is
  general goes to Chris as several small PRs; what is TRX64-specific stays in this repository. First split (to confirm
  while building):
  - to Chris: `jtag_stat.py` (Artix-7 configuration status, STAT/BOOTSTS/COR0/WBSTAR, XADC; nothing TRX64 in it), and
    whatever the cable work shows missing in his cable layer;
  - here: `trxdbg` (our `jtag_dbg.v` on USER1), `load.sh` (fetches from our Gitea packages), `t79/partial_load.py` and
    `icap_load.py` (our DFX), `s9_series.py`, `legacy_boot/g4.py`, and the recovery kit (D2).
- **D2 (Q2)** Developer tools and a JTAG recovery kit for testers (USB-Blaster or FT232H). The kit loads a package into SRAM
  and starts its app from DDR2, nothing written to the flash; the user then installs from the running firmware. Two kinds of
  package:
  - ours: the host bitstream and host app of a TRX64 release (both are in the release `.txu`, T152), started through our
    `jtag_dbg.v` (USER1) as `trxdbg riscv` does; the host menu then offers the `.txu` update and the restore;
  - Gideon's / Commodore's: their bitstream and `ultimate.bin`, started through Gideon's USER4 client; Chris's
    `u64ii_jtag.py` already does this, so the kit calls it and ships none of their files.
  Every load checks DONE, every upload reads back, and the kit says which step failed (#29: a swapped wire showed only as
  `UserID: ffffffff` at the end).
- **D3 (Q3)** No environment variables. The cable is named with `--cable` on the command line (`blaster`,
  `blaster:<serial>`, `ft232h`, `ft232h:<url>`, optional `--freq`); without it, from a TOML file that holds everything
  (cable, clock, path of the pinned Chris checkout). Lookup: `--config <file>`, else `~/.config/trx64/jtag.toml`; the
  recovery kit carries its own `jtag.toml` beside its script. Without option and file: USB-Blaster, as today (S5). FT232H
  default clock 1 MHz. `U64II_TOOLING` goes away in our tools (the path is a TOML key); openFPGALoader gets `-c usb-blaster`
  or `-c ft232 --freq <f>` from the same choice.

## §3a Open questions

- **Q1 Dependency (P2):** keep Chris's tooling outside the repository and move our checkout from `jtag/blaster-sample` to
  `upstream/main` (pinned commit, `open_cable` from there), or copy the cable layer into `tools/` (needs Chris's consent, no
  licence file)?
  **Answered (Alex, 2026-11-03): "we simply pin to his repo first and then propose the changes to him as several bite-sized
  PRs in his repo ... what is *general* goes to him, what is TRX64-specific stays with us"** (translated). Chris is a
  collaborator (write) on `Jondalar/TRX64U-MultiCore`; we have read access to his repository.
- **Q2 Scope:** developer tools only (S1-S6), or also a recovery kit for testers (a script plus `null_full.bit` and the host
  app `.bin` that brings up our host menu over an FT232H, the case T177 §4 leaves to the cable)?
  **Answered (Alex, 2026-11-03): "a recovery kit over JTAG is surely very helpful. Possibly with a package from us or one
  from Gideon/Commodore"** (translated).
- **Q3 Cable naming (P1):** an environment variable shared by all tools (e.g. `TRX_JTAG=blaster|ft232h[:<freq>]`, mapped to
  openFPGALoader's `-c usb-blaster` / `-c ft232 --freq`) or a `--cable` option per tool? Default FT232H clock 1 MHz?
  **Answered (Alex, 2026-11-03): "Both? --cable when I give it on the command line, or a .toml that has everything
  directly, that is how I would do it. No env variables"** (translated). See D3.

## §4 Out

- Environment variables for the cable or the tooling path (D3).
- Shipping Gideon's or Commodore's bitstream and application in the kit (D2: Chris's tool loads the user's own copy).

## §5 Build notes

Not started.
