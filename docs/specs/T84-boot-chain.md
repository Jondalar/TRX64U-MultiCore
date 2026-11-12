# T84 — The C64U always starts our host, which loads any core bundle from USB or SD without a PC

**Status:** DONE (2026-10-18, Alex: "We have that", translated): the host boots from flash, its menu finds `.cor` bundles
on USB/SD and starts them through ICAP, the host key in the `.cor` is checked (D5b). Not built when it was closed:
the update from the host app and the null core's gateware (see §5).
**Repo:** TRX64-Ultimate (gateware layer 1, boot ROM, boot app, `.cor` tooling).
**Number:** T84.
**Milestone:** M2.
**Origin:** Alex, 2026-10-12: "Yes, write the boot chain spec, and the ESP32 can go as an access path,
now with ICAP and DFX we don't need it any more." Before that, the same day: "we still need a host +
boot app that searches USB and SD for core bundles … rather `.cor`"; layer 2 at power-up: "a dummy
core that just does 'nothing'"; the swap "like our current softboot: it writes the guest core +
application, then starts the new application, and that one resets the new core".
**Depends on:** T79 (host/core split, decoupler, ICAP loader in layer 1), T79.1 (the contract), T15
(boot ROM). Supersedes `docs/boot-chain-concept.md` and T26's guest loading through the ESP32; takes
over the boot-app part of T81 and ADR-0005 (golden stub).

---

## §1 The problem

Four documents describe how the C64U should start and change cores, written a week apart, and they
no longer agree:

| Document | Date | What it says |
|---|---|---|
| `docs/boot-chain-concept.md`, T26 | 2026-10-01 | host firmware with an LVGL selector in flash; guests are **full** bitstreams that the ESP32 shifts in over JTAG; package `.trxcore` (manifest, hashes, bitstream, app, ROMs, icon); back to the host by an ESP32 `JPROGRAM` |
| ADR-0005 golden stub | 2026-10-03 | a never-writable minimal bitstream at 0x0 offers a firmware from SD/USB, else MultiBoots the main bitstream |
| T15 flash boot | 2026-10-03, built | the boot ROM loads the app from flash 0x220000 only if the flash bitstream's build stamp (`USR_ACCESS`) equals the running one; a JTAG app always wins |
| T79 / T81 | 2026-10-11/12 | layer 1 host + reconfigurable layer 2; a core swap is a partial bitstream (ICAP) plus a softboot of the matching application |

What changed since the concept: partial reconfiguration works on the board (T79.1 PoC 4: the host
and the app survive a core swap; 2.7 s over JTAG), and layer 1 now has an ICAP loader (dfx-trial
7d548a5). The concept's reason for the ESP32 ("an FPGA that loads its own successor over ICAP kills
the logic doing the loading", concept line 27-29) holds for **full** bitstreams only; a partial
bitstream leaves the host running.

Who has the problem: Alex at the C64U wants to switch on, pick a core (C64, VIC-20, later others)
from USB or SD, and be back in the selector without a PC; the CI and the release process need one
definition of what goes into flash and what goes into a bundle.

Parts:

- **P1 Power-up image.** What sits at flash 0x0 and starts first, and how it relates to the golden
  stub.
- **P2 First stage.** The boot ROM: which application it starts (flash, JTAG) and how it checks that
  application against the host.
- **P3 Boot app.** Finds `.cor` bundles on USB and SD, shows them, starts the chosen one.
- **P4 `.cor` format.** What a bundle holds and how it is checked.
- **P5 Swap.** From the running application to another core and its application.
- **P6 Return.** From a core back to the boot app (or straight to another core).
- **P7 Foreign full bitstreams.** Commodore's/Gideon's original firmware as a choice, without the ESP32.
- **P8 Flash.** Layout, protection of the host, updates.
- **P9 Failure.** A broken bundle, a hanging core, a lost flash: the board stays recoverable.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: power-up shows the core list
  Given flash 0x0 holds the host with the null core, and the boot app
  And   a USB stick with c64.cor and vc20.cor
  When  the C64U is switched on
  Then  the boot app lists both bundles within 3 s of power-on
  And   the expansion port and IEC stay idle (null core)

Scenario: picking a core starts it
  Given the boot app's list
  When  the user picks vc20.cor
  Then  the VIC-20 READY screen appears (photo), REST answers throughout
  And   the partial bitstream went in through ICAP (no JTAG, no ESP32)

Scenario: a bundle for another host is refused
  Given a .cor whose contract version differs from the host's
  When  the user picks it
  Then  the boot app says why and stays; nothing is loaded into the FPGA

Scenario: back to the selector
  Given a running core
  When  the user chooses "switch core" in its menu (or the documented key combination)
  Then  the boot app is back with its list; the old core is gone (null core loaded)
```

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1, P3 | power-up shows the core list | board, photo; time from power-on | board, by hand |
| S2 | P2 | a JTAG-loaded application still wins over flash | `tools/trxdbg.sh riscv` against a flashed board | board, by hand |
| S3 | P4 | `.cor` packer and checker round-trip; corrupt hash refused | `tools/cor_pack.py` + unit test in CI | CI `lint`/tests |
| S4 | P4 | wrong contract version refused | boot app on the board with a crafted bundle | board |
| S5 | P5 | pick a core: READY screen, REST up, ICAP used | board, photo + REST log + ICAP word count | board |
| S6 | P5 | swap time from pick to READY | board, stopwatch in the console log | board |
| S7 | P6 | back to the selector from a running core | board, photo | board |
| S8 (dropped, Q3) | P7 | Gideon's original starts from its flash slot, power cycle returns to our host | board, by hand | board |
| S9 | P8 | Gideon's updater cannot overwrite 0x0 after the host locked it | board, by hand, with a recovery path proven first | board |
| S10 | P9 | a bundle whose core hangs: the boot app comes back (watchdog or power cycle), the flash is untouched | board | board |

## §3 Decision

- **D1 No ESP32 in the boot chain (Alex).** Cores are loaded by the host itself, through ICAP. The
  ESP32's JTAG path, its new firmware and the `.trxcore` transfer over the WiFi UART are dropped.
  The ESP32 keeps its board role (power, rocker, WiFi).
- **D2 Power-up image = host + null core = golden stub.** Flash 0x0 holds one full bitstream: layer
  1 (host) and the null core in layer 2 (every output idle, every register window absent, T81).
  It is the recovery anchor of ADR-0005: written once, then locked (D8). Booting it needs no core.
- **D3 First stage stays T15's boot ROM** (in layer 1, `software/bootrom/bootrom.c`). It loads the
  boot app from flash; a JTAG load still wins. Its check changes from "same build stamp as the
  running bitstream" (`USR_ACCESS`) to "built against the same host contract version", so a boot
  app keeps working across host builds that keep the contract.
- **D4 The boot app** is a small application (LVGL) of its own, not the Ultimate app: it mounts USB
  and SD, lists `*.cor`, checks and starts them. It lives in flash next to the host; the way back to it
  is a full reload of flash 0x0 (power cycle or `IPROG`, Q2), not a softboot (P6). The `IPROG` path through
  the ICAP loader is not built yet.
- **D4a Default core after 5 s (Alex, 2026-10-12):** the boot menu starts its default entry by itself 5 s after it is shown; any key stops the countdown.
- **D5 `.cor` = one file per core**, a simple TLV container (no zip, the boot app must stay small):
  manifest (name, version, contract version, target FPGA, sizes, SHA-256 of every part), the
  partial bitstream of layer 2, the application image and its load address, optional icon; ROMs are
  files the application loads itself. Built by `tools/cor_pack.py` in CI.
- **D5a Check only when a core is added (Alex, 2026-10-12):** "check the core == only when adding". The full
  SHA-256 of every part runs once, when a `.cor` is added to the boot menu; `boot.cfg` keeps the partial's
  hash and the file size. A start with the same size and manifest hash skips the hashing, so a flipped bit
  inside a part is not caught at start.
- **D5b The `.cor` names the host it was routed against (Alex, 2026-10-14).** The manifest gains `host_sha`, the
  host key of the core's build manifest (`layers.json`, e.g. `62b75a5c...`), and the boot app compares it with its own
  host's: on a mismatch it says which host the bundle wants and loads nothing. Alex chose the hard check over a warning.
  *Why the contract version is not enough:* it rises when the partition's edge changes. A host that is re-routed with the
  same 228 ports keeps contract 1, so an old `.cor` stays formally valid and its partial hangs when the ICAP has it -
  a black picture with no reason given (the Amiga's updater run on 2026-10-14 showed what that looks like).
  *Where the running host's own number comes from:* the bitstream's AXSS register, which `host/pins.xdc` set to
  `TIMESTAMP`, now carries the host key (`TRX_HOST_KEY8`, exported by `ci/bitstream_layers.sh`), which is derived
  from the host's sources, so a cache hit that copies an older image keeps the same number where a timestamp would
  not. It is applied by `trx_axss` (`tools/vivado_lib.tcl`) on the open design before every full `write_bitstream`
  (the host's, `c64_basic.bit` and each `<core>_full.bit` in `host/core.tcl`); a partial has no configuration
  registers and needs none. It cannot be done in the XDC: an XDC is restricted Tcl, an `if` in it is
  `CRITICAL WARNING [Designutils 20-1307]` and the whole block is dropped - including its `else`, which is how run 26
  produced a host with **no** AXSS word at all. That image must never reach the flash: `flash_axss()` returns
  `found = 0`, `bootrom.c`'s `has_build && found && axss == build` fails and the board stops at "Not this bitstream's
  flash: no flash boot." with a black screen. `host/pins.xdc` keeps the unconditional `TIMESTAMP` for a hand build. The value is readable twice over without any host gateware change:
  `USR_ACCESSE2` (`host/top.v`) hands it to the design and the TRX IO block shows it as `BUILD` (offset 0x008, CAPS2
  bit 1), and `flash_axss()` (`host/bootrom/bootrom.c`) walks the flash image for the same register write. The boot
  ROM's own check (`axss == build`, T15 S4.2) keeps working and gets better: it now passes when the flash image was
  built from the same host sources, which is what it means to ask.
  *Built 2026-10-14 (Alex: "then try it on a branch and if it works off to main with it", translated):*
  `tools/cor_pack.py` writes and checks `host_key` (8 lower-case hex) and `cor_pack.py hostkey <bit>` reads the word
  out of a full bitstream - the same walk as the boot ROM's, measured against `c64_basic.bit` of run 25, which gives
  `6d356956`, the number the board printed on 2026-10-14. `tools/make_release_mc.sh` takes the key out of the very
  bitstream a bundle installs and refuses a `.cor` that disagrees. In the application, `cor_host_key`
  (`host-app/software/hostapp/cor.h`) is set once at start from the BUILD register; `cor_probe` answers
  `COR_ERR_HOST` and still fills `ci.host_key`, so the menu says "other host" and names it. The
  refusal sits in `cor_probe`, which both `coreboot.cc` (before anything reaches the ICAP) and the
  browser call, so a `boot.cfg` entry cannot walk past it either; the browser still shows such a
  bundle's manifest with both numbers ("Routed against host X; this host is Y. Cannot be added."),
  because a bundle that is refused silently is a bundle nobody can diagnose. A bitstream without the
  register (`cor_host_key == 0`) refuses nothing - otherwise a host built before this would reject every bundle.
  Old bundles **without** the field are refused, not grandfathered: a `.cor` of the T94 host line is at most a day old.
  Run 26 (branch, `7704304d`) routed the host and all five cores green but carried no key, for the XDC reason above.
  **Run 27 (`f9df7ae5`) has it:** `host_key` `6f8029334e30315e` and `cor_pack.py hostkey` reads `6f802933` out of
  `c64_basic.bit` and out of every `<core>_full.bit`. Still open until the next release is on the board: every bundle
  has to be packed again against that host, which `tools/pack_cores_mc.sh` now does in one command.
- **D6 Swap = softboot (T81).** The running application sets `core_decouple`, streams the partial
  through ICAP (0x10060610, T79), loads the new application into memory and jumps to it
  (`machine_handover()`); the new application initialises its core (registers, ROMs, release of
  `core_decouple`, C64 reset). No resident host software: every application brings its drivers.
- **D7 (dropped, Q3) Gideon's original as a full bitstream through MultiBoot.** It stays in its own flash slot;
  the host starts it with an ICAP `IPROG` and `WBSTAR` pointing there. That replaces the whole FPGA;
  the way back is a power cycle (0x0 boots our host again). No ESP32.
- **D8 Flash protection** as worked out in the concept (power-supply lock-down of the status
  register after setting BP/TB over the host's range, every power-on, before any guest runs). Kept
  from `boot-chain-concept.md` §"Protecting the host in flash", including its open checks (exact chip,
  WP# routing).
- **D9 JTAG stays the developer path** (Blaster): SRAM loads and `trxdbg.sh riscv` win over
  everything in flash, as today.
- **D10 Gideon's Ultimate app as the base only for the PoC (Alex, 2026-10-12):** "what I don't like is that the foundation of Gideon's Ultimate app then hangs in everywhere ... for the PoC it is ok, but only for that". The first swap through ICAP may extend `softboot.u2a` and run on the Ultimate app; the host app and the core applications of the target design get their own lean base (drivers for SD/USB/network, file system, LVGL) and do not depend on Gideon's app. **Base decided (Alex, 2026-10-12):** "can't you pull FreeRTOS incl. USB, SD and the network stack out of Gideon's GPL code and build a simple LVGL UI with a file browser for picking cores and setting up a boot menu?": the host app takes FreeRTOS, the USB stack (Nano CPU controller), SD/FatFS, lwIP and the file-system layer from Gideon's GPL firmware, without any C64 part, with an LVGL UI (boot menu, file browser for `.cor`, menu editor, network page; mockup in the T79 site). Built on its own branch, not on integ-m1.
- **D11 Settings of a core live next to its bundle (Alex, 2026-10-13):** "the HOST and, as far as I am concerned, our
  slim TRX64 core may store in the flash, the rest not please" (translated), after "can't there be a .cfg file for the core
  next to the .cor, with the same name?". The host and the TRX64 (C64) core keep their settings in the flash config pages
  as today; every other core (VC-20, Amiga, later ones) keeps its settings in `<name>.cfg` beside its `<name>.cor` (e.g.
  `/USB1/cores/vc20.cfg`) and never writes the flash. Gideon's `.cfg` load/save (`filetype_cfg.cc`) and the file-backed
  config pages (`CFG_FILEPATH`, `config.cc`) are the starting points; the host passes the `.cor` path to the application at
  the handover so it knows where its file is. Not built.
  **Layering (Alex, 2026-10-13):** "read yes, but save in its own .cfg then. So: host == default, core == overwrite local"
  (translated). A core application reads the flash settings (network, WiFi, clock and every shared group) as its defaults;
  whatever it saves, shared groups included, goes into its own `.cfg` and overrides the flash value for that core only. The
  flash is never written by it.
- **D12 One core application blueprint on the lean base (Alex, 2026-10-13):** "rather the lean app .. yes. That is better
  as the way forward. For VIC20/C16 I still see it with the C64 Ultimate app UI, because they are close. The rest not"
  (translated). Core applications other than the C64's are built from one blueprint ("meta-app") on the lean host-app base
  (D10): shared services once, each core described by a descriptor (ID, ROMs, settings, task entries, file types, media,
  Freeze/reset), menus as a whitelist from the descriptor. VIC-20 (T83) and C16 (T90) may stay on Gideon's Ultimate app UI;
  Amiga (T85), Atari 800 (T88), Apple II (T89), Spectrum (T91) and CPC (T92) use the blueprint. Own spec: T93.
- **D13 Set id (Alex, 2026-10-13, for T94):** "gladly" (translated): `boot.cfg` carries the release-set id in its
  header (e.g. `rel-12`); the boot app shows it and the REST info reports it, so a deploy can check which set a board runs.

Open questions for Alex (one line each):

1. Q1: Return to the selector: a menu entry in every application only, or also a hardware key
   **Answered (Alex, 2026-10-12):** "menu. The host is to get its own app, I choose it in there": every core application has a menu entry that returns to the host's own app (the boot app, D4: null core loaded, application softbooted); the core is chosen there. No host-watched key combination.
   combination that the host watches (works even when a core's application hangs)?
2. Q2: Does the boot app get its own small `.cor` (null core + boot app), or does it live only in
   **Answered (Alex, 2026-10-12):** "simply by power off/on? Then the host with its app is back from flash": the host app lives only in flash next to the host, no `.cor` of its own. Returning to it is a full reload of flash 0x0: a power cycle, or the menu entry of Q1, which sends the same reload through ICAP (`IPROG`, the sequence UG470 gives; WBSTAR 0) instead of a softboot. Both end in the same clean state; no special return path.
   flash next to the host?
3. Q3: Gideon's original (D7): needed at all on our boards, or dropped like the ESP32 path?
   **Answered (Alex, 2026-10-12):** "no, we don't need it, BECAUSE it does not work with our geometry at all": Gideon's firmware expects its own flash layout (its bitstream at 0x0, its app at 0x220000, `docs/c64u-flash-layout.md`), and 0x0 is our host. D7 is dropped; P7 and S8 go to §4. **Amended 2026-10-21 (T115, ADR-0058):** the T115 PoC starts Gideon's original from a flash-drive file by IPROG without its own layout; T115 carries it.
4. Q4: Bundles signed (only our bundles run) or only hashed (any bundle that is intact runs)?
   **Answered (Alex, 2026-10-12):** "hash only, it is TREX-internal": the SHA-256 per part (D5) checks integrity; no signatures, no key handling.

## §4 Out

- Commodore's/Gideon's original firmware as a choice (Q3): it needs its own flash layout with its bitstream at 0x0, which is our host.
- The ESP32 as a loader (D1); `docs/boot-chain-concept.md`'s ESP32 firmware, OTA and recovery
  sections are no longer pursued. The ESP32 backup/flash tools of T26.1 stay on their branch.
- Boards other than the C64U (XC7A50T) for now; the U64 Elite II has no 100T available.
- Running two cores at once.
- The VIC-20 core itself (T83) and the null core's gateware (T81).

## §5 Decisions taken while building

- **2026-10-18, closed (Alex: "We have that", translated).** Built and on the board: boot from flash into the host, the menu
  finding `.cor` bundles, the swap through ICAP, the host key check (D5b), the return to the host by IPROG (T79). Not built
  at the close: the update of host and cores from the host app, and the null core's gateware.
