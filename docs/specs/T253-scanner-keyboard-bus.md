# T253 — The P7 keyboard scan uses the keyboard's I2C bus only, and the applications start it as PR #112 does

**Status:** IN PROGRESS (2026-11-21). Asked by Alex, 2026-11-21.
Open (one line, as in the TODO table): IN PROGRESS 11-21: c64scpu test build; open: S1-S4, the host pin
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
  wiring on this branch, built by the pipeline (S4): the 16 copies under `cores/*/mono/`, and `host/top.v` after an
  approved host change (T94 D13, `contract/host_pin.txt`): open, a question to Alex.

## §4 Out

- The screen check (`picture_service`, `video_edid_sig`) is not changed. Alex, 2026-11-21 (translated): "Please do not
  change anything in the EDID path." Its two faults stay: a NACK counts as a new screen, and a mode that is not on the
  list of the screen is applied again at every change.
- The T252 switches (branch `t252-i2c-switches`) are not continued here.

## §5 Built

- **2026-11-21, branch `t253-pr112`** (from `integration` `3ec353c7`): D2 (`b3d8f946`, `cf0f6f73`, `46180837`).
- **2026-11-21, branch `t253-scpu-test`** (from `t241-scpu-fix`): D2, and D1 in `cores/c64scpu/mono/top.v` (`c64e74e3`).
