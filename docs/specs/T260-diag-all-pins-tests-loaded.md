# T260 — The diag core reaches every connector pin of the board once, and a new test is a TOML file on the SD card: no Vivado run, no app build, no new .cor

**Status:** ACCEPTED (2026-11-25), asked by Alex; building on `t260-diag` (from `t239.3`, T239.5 D15).
**Repo:** TRX64U-MultiCore — the diag large core (`cores/diag/`: `rtl/probe.v`, `rtl/diag_regs.v`, `rtl/diag_pads.v`, `mono/top.v`),
its own app from the SDK (`cores/diag/app/`, out of the shared app), the test files (`cores/diag/sd/`), its matrix lane.
**Number:** T260.
**Origin:** Alex, 2026-11-25 (translated): "Can you take on the diag core on T239? And above all the tests should not be compiled
in but loaded. Basically one should be able to measure everything conceivable on the board ... so prophylactically make every pin,
port, xyz measurable, and the test defines what and how. In my opinion that is not the case today." Then: "It would be nice if we
could add tests without having to rebuild the core every time. Do you understand?" On the proposal (§3): "Good. Have an agent
build it."
**Depends on:** T175 (the probe engine, the TOML runner), T259 (the A/V port: its generators, presets and tests; branch
`t259-diag-av`, not merged, board test open), T239.5 D13/D15 (a core on the matrix leaves the shared app), T213.3 (the pattern of a
large core with its own app).
**Milestone:** M3.

---

## §1 The problem

- **Who:** Alex and the testers at the board, and every later spec that needs a fact about the board (T175 issue 22, T259 #73, #125).
- **What they try to do:** ask the board a question ("does pin 9 of port 2 go low when I press button 2?", "which DAC group carries
  luma?", "does the cartridge port's IRQ line move?") by writing a test, not by building a core.
- **What happens today** (read from the code, `t239.3` and `t259-diag-av`):
  - The tests are TOML (`cores/diag/sd/tests/*.toml`, T175 D1), but the probe engine (`rtl/probe.v`) reaches only the ten joystick
    pins (`KBJOY_1_JOY`, `KBJOY_2_JOY`), `PADDLE_CMP[3:0]` and `PADDLE_DIS`/`ADJ`. The step word has a 10-pin mask. Every other port
    (cartridge slot, IEC, C2N, the A/V DAC, RESTORE, the buttons) cannot be read or driven by a test: a question about it is a new
    RTL block and a Vivado run, as T259 was (`diag_avout.v`, `diag_yc.v`).
  - T259's A/V page and its preset editor are C in the shared app (`coreapp/cores/portdiag.cc`, a D24 ledger exception, T259 D4).
    A new A/V setting is an app build.
  - The example tests ride in `diag.cor` (record 8, `tools/cor_pack.py:45`): a new test in the shipped set is a new `.cor`.
  - The diag app is still part of the shared app (T239.5 D13: a core on the matrix must leave it).
- **Why it matters:** every board question costs a build and a board round (memory "one measurement core": record everything in
  one run). The testers cannot add a test themselves.

Parts:
- **P1 Pin bank.** Every connector pin of the board, numbered, readable in one snapshot; drivable where the board allows it.
- **P2 Self-description.** The bitstream carries its pin table (index, name, direction, drivable, guard), so a test names pins by
  name and always matches the bitstream it runs on.
- **P3 Engine v2.** The step engine works on any pin of the bank: drive, release, wait, sample all, count edges, time to an edge,
  loop, wait for the user, write a core register (to set a generator), and capture: chosen pins every N ticks into a DDR2 ring.
- **P4 Safety.** A test cannot hurt the board or a plugged device (§3 D4).
- **P5 Generic runner.** The app has no test-specific code: prompts, runs, values, verdicts, and panels of registers (the T259 A/V
  page: rows with LEFT/RIGHT, presets load/save) all come from TOML.
- **P6 Tests outside the .cor.** The shipped tests are a folder beside the core, not a record in it; a tester adds a file by FTP.
- **P7 Own app, matrix lane.** The diag app from the SDK in `cores/diag/app/`, out of the shared app; the lane runs steps 1-13.
- **P8 Area and timing.** The core still closes at the host's clock; the numbers go into §5.

## §2 Scenarios

```gherkin
Scenario: S1 every pin is read in one snapshot
  Given the pin bank of the diag top
  When  a bench drives each input pin of the table in turn and the engine runs SAMPLE
  Then  the snapshot has that pin's bit at its table index and no other bit moved
  # bench: cores/diag/sim tb_probe (v2), iverilog in trx64-sim-env:5 on the builder

Scenario: S2 drive, guard and release
  Given a step list that drives each drivable pin, then a pin marked not drivable, then a cartridge-port pin with GAME low
  When  the engine runs it
  Then  each drivable pin's output enable follows the step; the not-drivable pin and the guarded pin stay released and STATUS
        flags the step as refused; at END, ABORT, a step error and decouple every output is released
  # bench: tb_probe (v2)

Scenario: S3 the bitstream describes its pins
  Given the pin table ROM of the build
  When  the app reads it through the registers
  Then  it gets every pin of the bank with name, direction and flags, and a lint check finds the table equal to the top's pin list
  # bench: tb_probe (v2) reads the ROM; lint: the check script in ci/lint.sh

Scenario: S4 capture into DDR2
  Given a CAPTURE step of 4 pins every 1 tick for 4096 samples
  When  the bench toggles the pins in a known pattern
  Then  the ring in the DDR2 model holds the pattern sample for sample, and the app's reader decodes it to the same values
  # bench: tb_probe (v2) with a memory model; unit: the reader in the diag app's tests

Scenario: S5 a test the build has never seen
  Given a TOML written by the unit test at run time that names pins only by name (no index, no op the build added for it)
  When  the runner loads, compiles and runs it against the register model
  Then  the step words name the pins' table indexes, and the result file has its values and verdicts
  # unit: the diag app's tests (test_diagtest v2)

Scenario: S6 the old tests still run
  Given every file in cores/diag/sd/tests (T175 and T259's av*.toml)
  When  each is compiled by the runner
  Then  each compiles (converted to the pin names where needed) and its step words are those of the old compiler for the same pins
  # unit: test_diagtest v2

Scenario: S7 the A/V page is a TOML panel
  Given the T259 A/V rows and the presets in cores/diag/sd/presets
  When  the panel file is loaded, a preset applied and a row changed with RIGHT
  Then  the register writes equal those of the old C page for the same preset and key; save writes a preset TOML that loads back
  # unit: register trace in the diag app's tests; ue2emu picture of the panel (no UX gate: memory "diag core without UX")

Scenario: S8 own app, out of the shared app
  Given the diag app built from the SDK in cores/diag/app
  Then  the shared app has no portdiag/diagtest code and no diag table row, the D24 ledger has no diag exception left, and the
        matrix lane of diag is green, steps 1-13, with "phase core identified" for the diag core in ue2emu
  # CI: the matrix run of the push

Scenario: S9 tests outside the .cor
  Given the diag lane's delivery
  Then  diag.cor has no test record, and the delivery holds the tests folder that goes to /SD/cores/diag/tests
  # CI: step 11/13 listing; tools/test_cor_pack.py

Scenario: S10 the board
  Given the diag .cor of the lane on the board and the tests folder on the SD card
  When  a tester runs pins.toml, then copies a new TOML by FTP and runs it
  Then  both write their result files, fetched by FTP; no build between the two
  # board: Alex or a tester

Scenario: S11 timing and area
  Given the diag build of the lane
  Then  timing is met; LUT, FF and BRAM of the diag blocks are recorded in §5 with the report lines
  # CI: the bitstream step's report
```

## §3 Decisions

- **D1 (Alex, 2026-11-25):** the diag core moves to the SDK and the matrix on T239 (T239.5 D15) and leaves the shared app (D13).
- **D2 (Alex, 2026-11-25, "Good" to the proposal):** one pin bank in the gateware with every connector pin of the board: the
  cartridge slot (address, data, the control lines, the buffers' direction and enable lines), joystick 1/2 and the paddle lines,
  C2N, IEC, the A/V DAC (`AVID_*`), RESTORE and the keyboard interrupt, the buttons, the LEDs. A test reads all of them and drives
  the ones D4 allows; it names pins by name. The engine adds a capture mode (chosen pins every N ticks into a DDR2 ring) so one run
  records everything.
- **D3 (Alex, same):** the internal buses stay with the host and are not in the bank: DDR2, flash, SD card, USB (ULPI), Ethernet
  (RMII, MDIO), WiFi UART, HDMI and the I2C buses. Driving them stops the board.
- **D4 Safety** (proposed 2026-11-25; Alex did not answer the question and gave the go for the whole proposal): reading is free.
  A pin is driven only when the table marks it drivable and the test names it. Joystick, IEC and C2N lines are open drain, driven
  low only (T175 D7). The cartridge port is driven only with the slot empty: the gateware refuses while `SLOT_GAMEn` or
  `SLOT_EXROMn` is low, and a test that drives it starts with a prompt. Power enables (`SLOT_PWR`, `CIA_PWR_EN`) are read, never
  driven by a test. Every output is released at END, ABORT, a step error, decouple and core exit.
- **D5 (Alex, same):** the app is a generic runner; the A/V page and the preset editor of T259 become a TOML panel (rows bound to
  registers, LEFT/RIGHT with min/max/step, presets loaded and saved as TOML). No test-specific C.
- **D6 (Alex, same):** the shipped tests and presets leave the `.cor`; they are a folder of the delivery that goes to
  `/SD/cores/diag/`. The pin table is in the bitstream (P2), so nothing in that folder depends on the build.
- **D7 (Alex, same):** a new kind of signal the step engine cannot make (video timing, a MHz protocol) still needs a generator
  block in RTL, set through registers from TOML, as `diag_avout` is. A new test needs no build.
- **D8 Base:** the work starts from `t239.3` with `t259-diag-av` merged, so T259's generators, presets and tests are kept. T259's
  board test can run on T260's build.

## §4 Out

- The internal buses (D3).
- Analogue levels: the board has comparators only (T175 §4).
- Driving an open-drain line high, and driving any pin by hand (T175 D7).
- Running a test unattended over REST (T175 §4).
- How the package or the registry carries the core (T239.5 D15: later).

## §5 Build notes

**App half, branch `t260-app` (the gateware half is `t260-gw`).**

*Step 1, S7 golden (3b765e9f4).* `tests/coreapp/test_diag_avtrace.cc` records the old A/V page's register writes: each of the three shipped presets loaded by name
(`pd.av_preset`), then RIGHT (next value, the last wraps) on each of the 24 rows from that preset's state. `tests/coreapp/golden/diag_av_trace.txt`, 301 lines;
`test diag_avtrace: PASS` (77 checks). The same unit still passes after the move below with registers named by the driver table (offsets unchanged).

*Step 2, P7/S8 (a79d6cc76 and the commit after it).*
- Own app `cores/diag/app`: `sdk.toml` (TOML B, `desc = "portdiag_desc"`, libraries diag + pad, base blocks as the A1200's), `gateware.toml` (TOML A: blocks ident, engine, live, av = 4
  blocks, 37 properties), `rtl_check_sources.tcl`, `lib/` = `portdiag.cc/.h`, `portdiag_regs.h` (registers by the driver table), `diagtest.cc/.h` (git mv from `coreapp/`).
  `rtl_check`: `4 block(s), 37 properties agree with the RTL of host` (case added to `tests/rtl_check/test_rtl_check.py`, not run to the end here: the file takes minutes).
  Two findings that shaped the TOML A: `TICKS_US` (00C) is a module parameter, rtl_check cannot read it, the app names it as a number (`PD_TICKS`); PC (016) is 8 bit, not 16.
- Out of the shared app: `coreapp/src/diagtest.cc/.h`, `coreapp/cores/portdiag.cc/.h/_regs.h` (about 3400 lines), the table row, the Makefile's two sources, `diag` in
  `BLUEPRINT_CORES`, the D24 ledger lines of diag (`file` x5, `exception` x4, `ids run.cc diag 2 -> 1`) and the targets, `SRC_PER_CORE` of `check_d24.py`; `base_sources = ["portdiag.cc"]`
  of vc20 and amiga1200 and of the SDK example (it was there only because `run.cc` called `portdiag_attach`). `check_d24.py` is green.
- The hook is generic now: `coreapp/src/coredlg.h` `CoreDialogs` (the old `PdHost`, same methods), `CoreCtx::dialogs`, `run.cc` fills it (`HostDialogs`). The diag lib asks through `c->dialogs`.
- Units, built as the diag app (`run.sh` `diagapp`, `tests/coreapp/diag_drv.sh`): `diagtest` (610 checks), `portdiag` (705), `diag_avtrace` (77), `diag_desc` (7, new: the descriptor cases
  the shared test_desc/test_hotkeys/test_memops held for diag, now without diag); `diagtest_halffile` stays a control. The TOML B names them (`libtab.py tests`). The shared units no longer list diag.
  Gone: the controls `diagtest_overwrite` and `diagtest_superseded` (they broke `instfiles.cc` through the diag unit; the installer is now exercised by `test_bundle` on a fixture core
  `instfiles`, no control there), the golden `menu_portdiag.txt` (needs a diag-app build of `test_desc`'s golden(), open).
- Lane: `tools/core_toml.py lanes` lists `diag large`; `ci/package.sh` and `ci/cor.sh` leave it out loudly by the TOML B switch (generic, as for the A1200); `ci/emu_kit.sh --core diag`
  and `tests/hostapp/emu_host.sh` (EMU_OWN_APP=diag) added - the profile is this tree's `tests/coreapp/emu/profiles/diag.toml` (the emulator release has none), hotkey and start time copied
  from the A1200's round, **not run** (step 12 runs only in the matrix).

*Step 3, P6/S9.* `cores/diag/mono/core.toml` has no `install_dir`/`install_old`; `cores/diag/superseded` is deleted (its two sample files moved to `tests/coreapp/instfiles/old`
with five test files as the fixture of `test_bundle`'s record 8 case, which is generic: the A500's sub-app test uses record 8 too). `gen_bundle.py`: `diag.cor` has no record 8
(`test_cor_pack.py` checks the descriptors name no install folder; `python3 tools/test_cor_pack.py`: OK; `test bundle: PASS`, 102 checks). `ci/lane.sh` step 13 copies
`cores/diag/sd` to `SD/cores/diag/` inside the delivery (`deliver-diag/SD/cores/diag/{tests,presets}`), listed in its `SHA256SUMS`: copy that folder to `/SD/cores/diag/`.

*Open (not built in this session).* Step 4 (TOML panel runner and the A/V page as a panel file, S7 proof against the golden) and step 5 (runner v2: pins by name, S4 reader, S5, S6)
are not built: the gateware half had not published its register map v2 on `t260-gw` (HEAD there is the merge of t259-diag-av), and the panel needs a decision that is Alex's (below).
`render.cc` still names `portdiag_desc` and `portdiag.h` (the render bench was not built here; `srclist.sh` needs `-I cores/diag/app/lib` and the diag table for it).
