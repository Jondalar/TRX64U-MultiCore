# T253 — The P7 keyboard scan uses the keyboard's I2C bus only, and the applications start it as PR #112 does

**Status:** IN PROGRESS (2026-11-21). Asked by Alex, 2026-11-21.
Open (one line, as in the TODO table): IN PROGRESS 11-22: S1, S4 PASS, S3 picture and colours; open: S2 10 min, S3 P7 key, merge (pin 0.3.1)
**Repo:** TRX64U-MultiCore — the I2C wiring in the top of every large core (`cores/*/mono/top.v`, `cores/*/mono/host/top.v`)
and of the host (`host/top.v`); the start of the scan in the applications (`coreapp/src/coreapp.cc`,
`host-app/software/hostapp/hostapp.cc`, `host-app/software/system/u64ii_init.cc`, `host-app/software/io/i2c/hw_i2c_drv.cc`).
**Number:** T253.
**Origin:** T241 D4 on the board, 2026-11-21: `c64scpu_t241fix` shows picture dropouts. The same bitstream with the
application of `f5916393` (`c64scpu_241_oldapp`) does not. About T242 against PR #112, Alex (translated): "So why do we do
more/something else?" Then: "All I want from you is the same behaviour as before the patch, except for the keyboard ... So
apart from the keyboard scan NOTHING. Do it." And: "How can one mix the buses?"
**Depends on:** T242 (this changes it), PR #112.
**Milestone:** M2 (release 0.2.1).

---

## §1 The problem

- **Who:** every person with a build since T242 and a screen. Seen on the c64scpu (Alex, board).
- **What happens today:**
  - The application diff from `f5916393` (no dropouts) to `b7571876` (dropouts) has 15 files; the bitstream is the same.
    One change touches the hardware while a core runs: T242 D1, the application gives the I2C buses back to the P7
    keyboard scan between its own transactions. The others: the datasette row (its register `PIN_CTRL` has no reader
    without `TAPE`, `cores/c64scpu/layer2_c64scpu.v:514-567`), `machine.read_mem` and `machine.write_mem` (only on a REST
    request), the way back from the ROM wait (only while a ROM is missing), the layout of the status line, labels and
    comments.
  - The scan drives all three I2C buses at once and reads SDA as the AND of the three (`cores/c64scpu/mono/top.v` before
    this change: `scl_lo`, `sda_lo` = `{i2c_done & kb_scl_low, bus_scl_low, bus_scl_low}`,
    `kb_sda_in = sda18_in & sdahd_in & sda33_in`). One of the three is the HDMI bus, the DDC of the monitor.
  - The core application reads two EDID bytes from the monitor every 500 ms (`picture_service`, T164 G12). Measured on
    the board with `c64scpu_t253` (PR #112 as it is, the bitstream of `t241fix`), 2026-11-21 19:39, `GET /v1/report`:
    78 times `[core] screen changed: PAL 50 Hz 1920 x 1080 is not on its list, applying 1920 x 1080`, most of them after
    `NACK on address A0 (-1)`. `video_edid_sig` gives a NACK its own signature (`app/video/videomode.cc:473`).
    `picture_service` takes that for a new screen and applies the mode again (`coreapp/src/picture.cc:830-840`): that is
    the dropout. The monitor names no 50 Hz mode, so PAL 1080p50 is never on its list. Alex: the first dropout after
    7 s, then at irregular times.
  - The keyboard expander (7-bit 0x21; `c64u_keyboard.v`: `ADDR_W = 8'h42`) is on the 1.8 V channel on every board
    revision. Gideon's init writes it on `I2C_CHANNEL_1V8` with no condition (`host-app/software/system/u64ii_init.cc:127-136`),
    PR #112 too (`coreapp/src/coreapp.cc:88-90`). Only the two PLLs change between the 1.8 V and the HDMI channel with the
    revision (`u64ii_init.cc:157`, `166`: `hw_version == 0x15`). The application's channel 1 (1.8 V) is bus 0 of the
    gateware on every board (`app_i2c.v:81`).
  - PR #112 as it is (`c64scpu_t253`) shows the same dropouts. The cause is that the scan runs on the HDMI bus, not the
    way T242 differs from PR #112.
- **The other wiring (the review, 2026-11-21):** `host/top.v` (the host menu and the DFX cores apple2, atari800, vc20)
  and TRX64-Ultimate's `designs/c64_basic/top.v` (the bitstream of `trx64u`, taken by its pin,
  `docs/contracts/trx64u-firmware-handover.md`) scanned all three buses as well. Both applications switch the scan on
  (`coreapp/src/coreapp.cc`, `host-app/software/hostapp/hostapp.cc`), and the core application reads the EDID every
  500 ms on every core (`coreapp/src/run.cc`, `picture_tick`). So the DFX cores and `trx64u` can show the dropouts of
  this spec at 50 Hz on a screen that names no 50 Hz mode. In 0.2.0 they could not: the applications parked the scan.
  `host/top.v` gets D1; `trx64u` gets it from TRX64-Ultimate (D3).
- **Why it matters:** T242 ships with 0.2.1. A core whose picture drops out cannot be played.
- **The parts:**
  - **P1** The scan drives and reads only the bus of the keyboard expander. The other buses carry what they carried
    before T242.
  - **P2** The applications start the scan as PR #112 does.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 (P1) the c64scpu bitstream
  Given the gateware of t241-scpu-fix with the change of D1 (branch t253-scpu-test)
  When cores/c64scpu/mono/build.sh runs on the builder (T250 D5)
  Then it routes and meets timing

Scenario: S2 (P1) no screen change from the scan
  Given c64scpu_t253k (that bitstream and the application of PR #112) started on the board, a screen attached
  When it runs for ten minutes
  Then GET /v1/report shows no "screen changed" and no "NACK on address A0"

Scenario: S3 (P1, P2) the person at the board
  Given the same build
  Then Alex sees no dropout and the start animation in colour, and the P7 keyboard types in the core

Scenario: S4 (P1) every core and the host
  Given the change of D1 in every copy of the wiring (D3)
  When the branch is pushed
  Then the pipeline builds every core and the host, and the package carries them
```

| # | Solves | Test | Where it runs |
|---|---|---|---|
| S1 | P1 | `route_status.rpt`, `timing.rpt` of the build | builder |
| S2 | P1 | `GET /v1/report`, the log | board (REST) |
| S3 | P1, P2 | the picture and the keys | board (Alex) |
| S4 | P1 | the pipeline run | pipeline |

## §3 Decision

- **D1 (Alex, 2026-11-21, P1):** "So apart from the keyboard scan NOTHING. Do it." After `i2c_done` the scan drives and
  reads bus 0 (1.8 V) only. Bus 1 (HDMI) keeps the synthesizer master until `i2c_done` and is released after it. Bus 2
  (3.3 V) is released. The application's master is unchanged. The comment above the wiring says so now (it said that the
  bus of the expander changes with the revision; the code shows that only the PLLs change).
- **D2 (Alex, 2026-11-21, P2):** "So why do we do more/something else?": PR #112 as it is. T242 D1, D4 and D5 are
  reverted. The contributor's commit `d4baeda9` is cherry-picked with its author. In `legacy.cc` the lines of T242 D6
  stay: they make the same call as PR #112.
- **D3: the build.** The c64scpu first, on the builder as in T250 D5, for the board (S1-S3). Then every copy of the
  wiring on this branch, built by the pipeline (S4): the 16 copies under `cores/*/mono/`, and `host/top.v`. Alex, 2026-11-21, asked
  whether the host may be changed for it (T94 D13), answered (translated): "But yes, the file for the host and the large
  cores is changed for the HDMI. All of it. Because if we build the application again and have a similar situation with
  PAL and the monitor's clock again, there are dropouts there too otherwise." The host pin is written again in the same
  commit (`tools/host_pin.sh --update`). TRX64-Ultimate's top (`trx64u`, `trx64g`) is that repository's file; this
  repository does not touch it (CLAUDE.md). Alex, 2026-11-21 (translated): "No, write to the TRX64U-FW session that it
  should take the fix into the bitstream. Then you can build the UI normally one last time." Sent the same evening; with
  the values it sends back, `release/imports/trx64u.pin` and `trx64g.pin` are written again and the package is built
  once more: open.

## §4 Out

- The screen check (`picture_service`, `video_edid_sig`) is not changed. Alex, 2026-11-21 (translated): "Please do not
  change anything in the EDID path." Its two faults stay: a NACK counts as a new screen, and a mode that is not on the
  list of the screen is applied again at every change.
- The T252 switches (branch `t252-i2c-switches`) are not continued here.

## §5 Built

- **2026-11-21, branch `t253-pr112`** (from `integration` `3ec353c7`): D2 (`b3d8f946`, `cf0f6f73`, `e47c184a`).
- **2026-11-21, branch `t253-scpu-test`** (from `t241-scpu-fix`): D2, and D1 in `cores/c64scpu/mono/top.v` (`c64e74e3`).
  `t253-scpu-test` (with `c64e74e3` and `fd6b4d47`, the trees of the board files) is a local branch and is not pushed;
  `integ-0.2.1` carries the same changes: D1 in `9c038ee5`, D2 in `b3d8f946`, `cf0f6f73`, `e47c184a`.
- **S1 PASS (2026-11-21):** builder, `t253-vivado.sh` as T250 D5 (`docker run --init`, the heavy lock in the container),
  tree `c64e74e3`. `route_status.rpt`: 0 nets with routing errors. `timing.rpt`: "All user specified timing constraints
  are met", WNS +0.008 ns, TNS 0, WHS +0.052 ns. `utilization_hier.rpt`: top 23,696 LUT.
- **The board file:** `c64scpu_t253k.cor` (3,586,939 bytes) in `/SD/cores/`, with the ROMs and the settings of
  `c64scpu_t253`: the bitstream of `c64e74e3` (sha256 `083a03c0...`), the application of `fd6b4d47` (PR #112 as it is,
  sha256 `b5a5265a...`, the same as in `c64scpu_t253`). Between the two builds only D1 differs.
- **S2, first look (2026-11-21 20:07):** `c64scpu_t253k` started by Alex from the file browser. `GET /v1/report`: 0 lines
  `screen changed`, 0 lines `NACK on address A0` (the T253 build without D1 had 78 and most of them NACKs). The check
  after ten minutes was not made: Alex had gone back to the host menu.
- **S3, the picture (2026-11-21):** Alex (translated): "Basically it is OK for the SCPU core for now." The start animation's
  colour and the P7 keys were not named.
- **S3, the keyboard's bus (2026-11-21, measured, no key typed):** the same report's log starts at the application's start
  (`*** TRX64 core application`, `phase main`) and has no `Error sending write address` and no `NACK`. Right after
  `phase main` PR #112's block writes the keyboard expander (0x42, registers 0x06 and 0x07) on `I2C_CHANNEL_1V8`
  (`coreapp/src/coreapp.cc:87-91`); `i2c_write_byte` prints that line when the address gets no ACK
  (`host-app/software/io/i2c/i2c_drv.cc:176-185`). So the expander answers on channel 1, which is bus 0 of the gateware
  (`app_i2c.v:81`), the one bus the scan has after D1. A key typed on the P7 keyboard was not seen: the keyboard is not
  attached (Alex: attaching it means opening the case).
- **The other keyboards (2026-11-21):** Alex (translated): "The USB/bling-board keyboard works in any case, I can operate
  the menus." Neither goes through the I2C scan: the bling board's keyboard comes on its own serial line
  (`bling_keyboard bkb`, `.rx(bk_s[1])` in `cores/c64scpu/mono/top.v`), the USB keyboard through USB. D1 touches only the
  P7 header keyboard.
- **S3, the colours (2026-11-21):** Alex (translated): "Boot colours of the CMD start? Super. All good." S3's picture and colour
  parts hold; its P7 key part waits for the keyboard (not attached).
- **2026-11-21, `host/top.v` (D3):** the block from the synthesizer master to the `c64u_keyboard` instance is the one
  of the 16 copies; before, it differed from them only in the lines of D1. `tools/host_pin.sh --update`: one line moves,
  `host/top.v`; `--check`: unchanged (330 files). The 13 `mono/host/FROM` files say so. No other file of the tree
  wires the scan to the three buses (`git grep`; 17 files have `kb_sda_in = sda18_in`).
- **The bench `host/sim/tb_app_i2c.v`** modelled the old mux (the scan on all three buses, `sda_in` the AND of the
  three). It models D1 now (the scan on the 1.8 V bus; the synthesizer master is not in the bench). Run on the builder
  in `trx64-sim-env:5` with `ci/sim_iv.sh tb_app_i2c.v`: "codec writes 11, keyboard transactions 130, kb NACKs 0",
  "errors 0: PASS"; the old model gives the same lines. So the bench does not tell D1 from the old mux (no check counts
  traffic on bus 1 or 2 during the scan), and no bench reads the block of `top.v`: it proves the application's master
  and the scan with SDA read from bus 0 alone. The comment of `host/app_i2c.v` (and its 13 copies) says D1 now; the
  host pin moves with it (`host/app_i2c.v` only).
- **The build of every core:** a feature branch builds only the cores it touches, so after a host change the DFX
  cores have no bitstream against the new host and the package fails (`ci/plan.py` `full_scope`). The bundle is
  pushed as `integ-0.2.1`: full scope, every core, the sims, the package.
- **S4 PASS (2026-11-22):** run 44948262543 on `integ-0.2.1` (`572ea62e`), full scope: green. 25 jobs passed; 5 were
  skipped by their conditions (the builder's approval, `cor`, `update`, `emu`, `Pre-release`). The package has 20 `.cor`
  (13 large cores, 5 DFX cores, trx64u, trx64g), `trx64host_update_572ea62e.txu` and the applications; `SHA256SUMS`
  checked. Every build's `timing.rpt`: "All user specified timing constraints are met". WNS: host +0.109 ns, the large
  cores +0.007 ns (cpc) to +0.231 ns (c128), c64scpu +0.008 ns. trx64u and trx64g carry the pinned bitstream of
  `main-31e69921`, without D1.
- **The units (2026-11-22):** the run's apptests job ran none ("tier fast, base HEAD~1 ... will run: nothing"). On the
  Mac, tree `572ea62e`: `tests/coreapp/run.sh`: "119 test programs passed, 215 parallel controls (JOBS=2), every
  negative control failed as it must"; `tests/hostapp/run.sh`: rc 0, each test passed.
- **On the board (2026-11-22):** the package is on the SD (`tools/board_put.sh`): the `.cor` in `/SD/cores/`, the host
  update in `/SD/txu/`.
- **trx64u's bitstream (2026-11-22):** TRX64-Ultimate `c392beed` puts D1 into `designs/c64_basic/top.v` (the same mux
  and `kb_sda_in = sda18_in`). Its main `52f9a7ec` (a CI change after it) is green (run 2996). The package
  `main-52f9a7ec`: `c64_basic.bit` sha256 `c950aa551912deca...`; `ultimate.app` is the pinned one (`fa70a66d...`). From
  `31e69921` to `52f9a7ec`, `c392beed` is the one commit that changes the gateware (the others: documentation,
  `ci/sims.txt`, `tools/gh_*.py`). A changed pin builds on `integration` only: the builder publishes the import entries
  of its `KEYCODE_REF` (T239.1 D43).
- **The pin waits for TRX64-Ultimate 0.3.1 (2026-11-22):** asked which package version it names as good (contract
  §2), the TRX64U-FW session answered (translated): "please do NOT pin main-52f9a7ec - release 0.3.1 is on its way, the pin
  comes after it." It reports Alex's decision there: 0.3.1, a patch release with the I2C fix (`c392beed`). Its application
  version is 0.3.1 (`d94aa68e`), so `ultimate.app` and trx64g's `APP_SHA256` change. The bitstream is to stay
  `c950aa55...` (gateware_key `63ae8d25e42300b8`); the session confirms that on the finished package. The pin takes the Gitea
  release `trx64-main-d94aa68e` (manifest version 0.3.1); the session sends VERSION, PKG_VERSION, SEMVER, BIT_SHA256 and
  APP_SHA256 (D3). The builder's import store then needs the new `ultimate.app` beside the bitstream (T239.1 D43). No
  board has run this bitstream yet; TRX64-Ultimate's own board test is open with Alex.
- **trx64u.cor until the pin moves:** it carries the bitstream of `main-31e69921`, without D1. The core application starts
  the scan on every core (`coreapp/src/coreapp.cc:92`) and reads the EDID every 500 ms (`coreapp/src/picture.cc:794-840`).
  These are the conditions of `c64scpu_t253`, the build without D1. Not measured on trx64u.
- **The pin moves to 0.3.1 (2026-11-22):** the TRX64U-FW session sent the values of the release: VERSION `d94aa68e`, SEMVER
  0.3.1, PKG_VERSION `main-d94aa68e`, BIT_SHA256 `c950aa55...`, trx64g's APP_SHA256 `32b17761...`. Checked against Gitea's
  package `trx64-ultimate/main-d94aa68e`: `c64_basic.bit` and `ultimate.app` have these SHA-256 (`SHA256SUMS`: OK);
  `manifest.json` has "version": "0.3.1", app_commit `eb3e6be6`, gateware_key `63ae8d25e42300b8`, bitstream_from "cache
  c392beed". The bitstream is the one of `main-52f9a7ec`. The Gitea prerelease `trx64-main-d94aa68e` exists, so the pin
  carries `SEMVER=0.3.1` (contract §2). `trx64u.pin` and `trx64g.pin` are written again (D3). Until the pin is on
  `integration`, the branch's collect job is red (T239.1 D43). The builder's import store needs `c950aa55...` and
  `32b17761...` before the run on `integration` (filled by hand, T239.1 D43): filled 2026-11-22 (below).
- **0.3.1 released, the import store filled (2026-11-22):** Alex (translated): "TRX64U is tested and released as 0.3.1.
  ... Finish it now with the pin." The newest TRX64-Ultimate release on Gitea is still `trx64-main-d94aa68e` (9 files), the
  one the pins name. The builder's import store now holds `c950aa55...` (1,782,841 bytes) and `32b17761...` (1,685,036
  bytes) beside the files of 2026-11-20; `sha256sum` on the builder gives the file names. Nothing in the store was removed.
- **Both import entries packed (2026-11-22):** `ci/imports.py pack` at `f8b8800c`, in a throwaway worktree, from a store
  with the same three files. `imp-trx64u-8bc03ceb4fcf0e77` gives `trx64u.cor` (1,892,625 bytes, the stand-in application).
  `imp-trx64g-9025da3d66a0f8e8` gives `trx64g.cor` (3,470,464 bytes). `tools/cor_pack.py check` passes on both: version
  0.3.1, kind large, idcode `0362c093`. Both manifests name `c950aa55...`; `trx64g.cor` holds `ultimate.app` without its
  12-byte record (`8ba6bad7...`, named in its manifest). The builder publishes the two entries when the pin is on
  `integration` (T239.1 D43).
- **0.3.1 on the board (2026-11-22):** `/SD/cfws/trx64_update_d94aa68e_20261122.cfw` is the file of the package (SHA-256
  `7072f400...`). It holds `c64_basic.bit` and `ultimate.app` byte for byte. Started from the host's File Browser with S
  over the host's `/v1/ui`; the start question was answered yes. `GET /v1/info` then gave "firmware_version": "0.3.1",
  "git_commit_hash": "eb3e6be".
