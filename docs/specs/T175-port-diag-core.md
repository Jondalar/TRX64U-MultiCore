# T175 — A diag core measures the joystick/paddle ports from TOML test files and writes the result to the SD card

**Status:** SPECIFIED (all §6 questions answered 2026-11-02); was PROPOSED (2026-11-02). Status check 2026-11-13: BUILT and merged (dd8dbb966); diag v2 with testers 11-04 (#30); S4-S8 open (wave 2). 2026-11-15 sync: the Vivado run happened (the core ran on a tester's board as pre-release trx64mc-diag-7fda7e1f); tester results 2026-11-04 (docs/todo/T175.md, discussion #30): pins 31/31 pass, stick2 4/4 pass (S5 on a tester's board, not Alex's), cd32 6/24 (pin 5 never low; hardware question open, §5). Not recorded: S4 paddle result, the S8 Vivado numbers (WNS, LUT, BRAM) in this spec, S7 board FTP fetch.
**Repo:** TRX64U-MultiCore — a LARGE core `cores/diag/` (own flat bitstream, own top in `cores/diag/mono/`, host copy per T164), its core app descriptor in `coreapp/cores/portdiag.cc` (the host's `coreapp/src/diag.cc` is taken), test files in `cores/diag/sd/tests/*.toml`.
**Number:** T175 (TODO.md table).
**Origin:** Alex, 2026-11-02, after GitHub issue 22 (a tester: second/third fire button and the Amiga CD32 pad in the A1200): "Could one check the ports programmatically? A kind of diag core? 'Plug in a paddle', 'plug in a CD32 pad', and then the program measures things and writes out a file? That would then be usable for development later." Then: "a large-core measuring part would be good. Must the measurement be in the core or could one put the measurements in toml for the app to load?" Then, on the split below: "Spec that. The UI must then load measurements from the file system (.toml) and save [results]. WiFi, FTP etc. on in this core."
**Depends on:** T164 (host copy with network, `cores/*/mono/host/`), T104 (generic core app), T112/T148 (core folder on `/SD/cores/<id>/`). T47 §4.6 for the paddle hypothesis.
**Milestone:** M3.

---

## §1 The problem

Issue 22 asks for the second/third fire button and the CD32 pad in the A1200. Nobody knows what the C64U board lets the FPGA do on the joystick connectors, and every guess today costs a Vivado run.

Read from the code (not from comments):
- `host/top.v:67` `KBJOY_1_JOY`, `KBJOY_2_JOY` are `input wire [4:0]` (pins 1-4 and 6; `host/c64u.xdc:504-` PULLUP). Pin 5 and pin 9 are not among them: the only other joystick-port inputs are `PADDLE_CMP[3:0]` (`top.v:77`, 1.8 V), with `PADDLE_DIS`/`PADDLE_ADJ` as `OBUFT` outputs (`top.v:1615-1616`), high-Z unless the core enables them. That the CMP lines are comparators on the POT pins is a hypothesis (T47 §4.6: pin map "static candidates only"), never measured.
- So today a second button (pin 9) or a CD32 pad (pin 5 load/mode out, pin 6 clock out, pin 9 data in; hundreds of kHz) cannot be said to work or not: the pins 1-4/6 cannot be driven at all, and whether pins 5/9 can be driven through `PADDLE_DIS`/`ADJ` is unknown.
- The only experiment path is a new bitstream per question, and no stimulus may be made by hand (no loose wires; only plugged peripherals).

Parts:
- **P1** What the board allows: can each of the 10 pins be driven (low, open-drain) and read back; which of `PADDLE_CMP[3:0]` follows which POT pin; what `PADDLE_DIS`/`ADJ` do.
- **P2** A generic timed probe engine in gateware: drive/release a pin, wait N clocks exactly, sample all port pins with a timestamp, count edges, time to a comparator flip. Runs a step list the app loads.
- **P3** The test cases as data: human prompts, step lists, evaluation thresholds, output format in TOML; a new measurement needs no Vivado run.
- **P4** The app: pick a `.toml` from the SD card, show the prompts, run, evaluate, save the result file.
- **P5** Network: WiFi/Ethernet/FTP on in this core, so the result file and new tests move without pulling the card.
- **P6** The first real measurements the issue needs: paddle charge time per POT, 2-button stick (Alex's Competition Pro with the 2-button mod), CD32 pad read; mouse (1351, Amiga quadrature) later.
- **P7** Area/timing: a large core of its own, but small; it must close at the host's clock.

## §2 Scenarios

```gherkin
Scenario: S1 the engine times exactly
  Given a step list "drive pin 6 low; wait 1000 clocks; release; sample" in the step RAM
  When  the engine runs against a loop-back model in the testbench
  Then  the result FIFO holds the pin transitions with timestamps exact to 1 clock, and the count is 1000 +- 0
  And   with the wait changed to 1001 the bench FAILS (control: the compare is on the exact tick)

Scenario: S2 the TOML is the test, not the bitstream
  Given tests/stick2.toml and a changed threshold in it
  When  the host-side parser (tests/coreapp) compiles it to the step list and evaluates a recorded result log
  Then  the verdict changes with the threshold, the compiled step list and the engine are byte-identical
  And   a TOML with a step the engine does not have is refused with the line number, nothing is run

Scenario: S3 pin drivability on the board (P1)
  Given the diag core on the C64U, nothing plugged into the ports
  When  test pins.toml drives each of the 10 pins low and reads it back
  Then  the result file says per pin: driven yes/no, level read back, edge time in clocks
  And   a pin that reads low without being driven (control: PULLUP gone) is reported as "stuck", not "driven"

Scenario: S4 paddle charge time (P6)
  Given a paddle in port 1, PADDLE_DIS/ADJ per the test
  When  paddle.toml runs, the user turns the paddle to both ends
  Then  per POT (X1, Y1, X2, Y2) the file holds min/max charge time in clocks and which CMP bit flipped
  And   with no paddle plugged the result is "no flip within timeout", not a number

Scenario: S5 the 2-button stick
  Given the Competition Pro with the 2-button mod in port 2
  When  stick2.toml prompts "press button 1", "press button 2", "release"
  Then  the file shows which pin or CMP bit changed for each button and the verdict per button
  And   a prompt the user skips (nothing pressed) times out and is marked MISSED

Scenario: S6 CD32 pad (P6)
  Given a CD32 pad in port 2
  When  cd32.toml pulses the clock and reads the data pin (if S3 said the pins can be driven)
  Then  the file holds 8 shifted bits per button state, ID bits, the clock period used
  And   if S3 said "not driven" the test reports "cannot run on this board" and stops (no fake data)

Scenario: S7 files and network (P4, P5)
  Given the core on the board with WiFi or Ethernet
  When  the user picks tests/stick2.toml, runs it, then fetches results/ by FTP
  Then  results/stick2_<date>.toml exists with the same bytes the app showed
  And   a read-only SD card gives a visible error on the screen and no half file (control: the write fails)

Scenario: S8 area and timing (P7)
  Given the Vivado run of cores/diag/mono
  Then  setup WNS >= 0 at the host clocks, probe engine <= 1,500 LUT and <= 4 BRAM (budget; the number comes from the utilization report)
```

| # | Solves | Test | Where it runs |
|---|---|---|---|
| S1 | P2 | `cores/diag/sim/probe/run.sh` (GHDL/iverilog, loop-back model) | `ci/sims.txt` tier 1 |
| S2 | P3 | `tests/coreapp/` TOML compile + evaluation block | `tests/coreapp/run.sh` |
| S3 | P1 | board step: pins.toml, nothing plugged | board session (Alex plugs nothing) |
| S4 | P6 | board step: a paddle pair | board session, photo of the file |
| S5 | P6 | board step: Competition Pro | board session |
| S6 | P6 | board step: CD32 pad | board session |
| S7 | P4, P5 | `tests/coreapp` file save + board FTP fetch | bench + board |
| S8 | P7 | Vivado reports of the core stage | CI `bitstream` (large) |

## §3 Decision

- **D1 Split (Alex, accepted):** the gateware holds a generic timed probe engine, the test cases are TOML loaded by the app. A new measurement is a file, not a Vivado run.
- **D2 Large core, own top:** `cores/diag/` with `mono/` (own flat bitstream, own `top.v` as the host copy of T164), network kept (not `NO_NET`): Alex "WiFi, FTP etc. on in this core". The own top makes `KBJOY_*_JOY` bidirectional (open-drain: output value 0, enable = drive-low; high = release to the PULLUP). Whether the board lets the FPGA drive them is unknown: S3 finds out; D2 only provides the means. `PADDLE_DIS`/`ADJ` are enabled by the engine.
- **D3 Probe engine** (`cores/diag/rtl/probe.v`), clock = `cpu_clk` (frequency stated from the Vivado report in §5 once built; the engine counts whole ticks, 1 tick = 1 step of the time base). Step RAM in BRAM, 256 steps x 32 bit, loaded by the app; result FIFO 512 x 32 bit in BRAM.
  Step word: `[31:28] op, [27:24] pin mask select, [23:0] arg`.
  | op | name | arg |
  |---|---|---|
  | 0 | END | - |
  | 1 | DRIVE | pin mask (10 port pins) and level in the next field of the mask; 0 = drive low, 1 = release |
  | 2 | WAIT | N clocks exact (24 bit, 16.7 M ticks) |
  | 3 | SAMPLE | push {timestamp[15:0], pins[9:0], cmp[3:0], flags} |
  | 4 | EDGES | count edges of the masked pins for N clocks, push the count |
  | 5 | TIME_TO_FLIP | time until the CMP bit set flips (or timeout N), push the time |
  | 6 | PADCTL | PADDLE_DIS/ADJ level and enable |
  | 7 | LOOP | repeat the following K steps C times |
  | 8 | WAIT_USER | halt until the app writes GO (for prompts); the engine does no timing across it |
  Registers (core window, byte offsets, own page; final numbers in §5):
  | reg | R/W | meaning |
  |---|---|---|
  | 000-003 | R | ID "DIAG" |
  | 004 | R | CAPS (b0 pins drivable by top, b1 PADDLE_DIS/ADJ) |
  | 008-00B | R | clock ticks per µs x 256 (so the app does the µs conversion) |
  | 010 | W | CTRL: b0 RUN (edge), b1 ABORT, b2 GO, b3 CLEAR FIFO |
  | 011 | R | STATUS: b0 running, b1 waiting for GO, b2 done, b3 FIFO overflow, b4 step RAM error |
  | 014-015 | R | PC |
  | 020-023 | W | STEP_ADDR/STEP_DATA (write-through, auto-increment) |
  | 030-033 | R | FIFO_DATA (pop on read of 033), 034-035 FIFO level |
  | 040-041 | R | live pin levels {PADDLE_CMP, KBJOY_2, KBJOY_1}, 2-FF synchronised |
  No result is derived in the gateware: the app evaluates. **The numbers as built differ in places from this table: §5 (2026-11-02, gateware) is the register map.**
- **D4 TOML schema** (one test per file, loaded and compiled by `coreapp/src/diagtest.cc`; unknown key or op = refuse with the line):
  ```toml
  [test]  name = "Paddle charge time"  id = "paddle"  version = 1  needs = ["paddle"]  menu_input = "any"|"keyboard" (optional, default "any")
  [[prompt]]  id = "p1"  text = "Plug a paddle into port 1, turn it fully left, press A"
  [[step]]  op = "padctl"  dis = 1  adj = 0
  [[step]]  op = "wait_user"  prompt = "p1"
  [[step]]  op = "time_to_flip"  cmp = [0,1]  timeout_us = 40000  into = "t_left"
  [[eval]]  value = "t_left"  min_us = 30  max_us = 20000  verdict = "left end ok"
  [result]  format = "toml"  keys = ["t_left","t_right"]
  ```
  `op` names mirror D3 one to one; `into` names a result slot; `[[eval]]` has `min`/`max` (in µs or counts) and a verdict string; times are written in µs in the test and compiled to ticks with the 008-00B register.
  Example tests shipped in `cores/diag/sd/tests/`: `pins.toml` (drive-low/read-back of each pin, S3), `paddle.toml` (S4: four POTs, left/right end), `stick2.toml` (S5: button 1, button 2, release, sample pins and CMP at each; v2 in §5), `cd32.toml` (S6: v2 in §5; refuses to start when `pins.toml` result `drivable` is false).
- **D5 App** (`coreapp/cores/portdiag.cc`, generic core app, TOML menu like `trx64u.cor`): a file picker over `/SD/cores/diag/tests/*.toml` (the existing file browser), the prompts on screen, a result view. Result file: **TOML** `/SD/cores/diag/results/<id>_<YYYYMMDD-HHMMSS>.toml` with `[meta]` (test id, version, core build, date, ticks per µs), raw samples as arrays, `[eval]` verdicts. TOML because it is already parsed on both sides and diffs in git; a CSV export of the sample arrays can follow without changing the format. The date comes from the host RTC if there is one, else a counter.
- **D6 Network:** the host copy keeps Ethernet and WiFi (`NO_NET` not defined); FTP is the app's existing server; the core reached by REST like the other large cores.
- **D7 Safety:** the engine never drives a pin the TOML does not name; at ABORT, END, error and core exit every output is released (`core_decouple` already tri-states the paddle outputs); drive-low only, never drive-high on a joystick pin. No pin is touched by hand: stimuli are the plugged peripherals only.
- **D8 Lost:** a fixed per-measurement RTL block (one test per Vivado run) loses to D1: cost per question; a scripting CPU in the core loses to D1: LUTs and a toolchain for no gain.

## §4 Out (to be decided with Alex, nothing assumed)

- Mouse tests (1351 proportional, Amiga quadrature): later, as TOML files, as soon as the engine needs nothing new (decide with Alex which engine ops they would need).
- Keyboard matrix, IEC, cassette, user port, cartridge port diagnostics: this spec is the joystick/paddle ports only.
- Fixing the A1200: what the results mean for the A1200 (2nd/3rd button, CD32 mapping) is a separate spec after S3-S6.
- Analog levels (an ADC): the board has comparators only; no voltage measurement.
- Pin drive-high / push-pull on the port pins.
- A GUI plot of the samples on the screen (the file only).
- Running a test over REST/FTP unattended (prompts need the user; the file is fetched afterwards).

## §5 Decisions taken while building

### 2026-11-02 gateware half (branch t175-diag-gw): `cores/diag/`, benches `diag_probe` and `diag_pins`

Built: the large core `cores/diag/mono/` (core.toml `experimental`, build.sh, build_mono.tcl, host copy `mono/host/` from `cores/amiga/mono/host`, network on, FROM
documents the pins.xdc edit), own top `mono/top.v` + `top.v.diff` (KBJOY_1_JOY / KBJOY_2_JOY inout, `diag_pads.v` = IOBUF with I = 0, T = decouple | ~low: open drain, released =
the old input, PULLUP and a 4 mA / slow drive limit in the copy's pins.xdc), `mono/layer2_diag.v` (module renamed `layer2` by the build; ports of layer2_null.v plus `kbjoy_i[9:0]`
and `kbjoy_low[9:0]`; it sits in `mono/`, not `cores/diag/`, because `tools/contract_ports.py` compares every `cores/<id>/layer2_<id>.v` with the 247-port contract), the register
block `rtl/diag_regs.v`, the engine `rtl/probe.v`, the pads `rtl/diag_pads.v`. No machine: a 504 x 312 raster for the host's scaler, page 6 forwarded to the host's pixel layer.
Clock = cpu_clk = 100 MHz (top.v CLKOUT2_DIVIDE 10 of a 1000 MHz VCO), so 1 tick = 10 ns, TICKS_US x 256 = 0x6400, WAIT max 2^24 - 1 ticks = 167.8 ms.

**Register map as built** (core window page 0, byte offsets; deviations from the §3 table in bold):
| reg | R/W | meaning |
|---|---|---|
| 000-003 | R | ID "DIAG" (bytes D I A G) |
| 004 | R | CAPS 0xC0 (the host's: b6 page 6, b7 RGB565) - **the §3 "CAPS b0/b1" moved to 006: the generic app acts on b0/b1 of 004 as gate/state bus** |
| 005 | R | CAPS2 0x02 (BUILD is there) |
| **006** | R | **DCAPS** b0 joystick pins are open-drain outputs in the top, b1 PADDLE_DIS/ADJ driven by the engine (0x03) |
| 008-00B | R | BUILD stamp (host) - **§3 had ticks/us here** |
| **00C-00F** | R | **TICKS_US x 256**, 0x00006400 |
| 010 | RW | IRQ_EN (host layout); 014 IRQ_CAUSE R/W1C: b7 page flip, b6 vsync, **b5 the engine stopped or waits for the user** |
| 011 | R | STATUS b0 running, b1 waiting for GO, b2 done, b3 FIFO overflow (sticky to CLEAR), b4 step error (unknown op, nested/ill-formed LOOP) |
| **012** | W | **CTRL** b0 RUN, b1 ABORT, b2 GO, b3 CLEAR FIFO (one-clock pulses; §3 had 010, taken by IRQ_EN) |
| **016** | R | **PC** (step index; §3 had 014-015) |
| 020-023 | W | STEP_DATA little endian; the write of 023 stores the word at STEP_ADDR, then STEP_ADDR increments |
| **024** | RW | **STEP_ADDR** (8 bit) |
| 030-033 | R | FIFO_DATA little endian; the read of 033 pops (in the clock of the read) |
| 034-035 | R | FIFO level 0..512 |
| 040-041 | R | LIVE `{2'b0, CMP[3:0], KBJOY_2[4:0], KBJOY_1[4:0]}`, high = pin high, 2-FF synchronised |
| **042-043** | R | **DRV** mask of the pins the engine drives low now (bits 9:0, order of LIVE without CMP) |
Step word: `[31:28] op, [27:24] cmp select (TTF only), [23:0] arg`. Ops: 0 END; 1 DRIVE (arg[9:0] pins named, arg[19:10] their level: 0 drive low, 1 release; unnamed pins keep their
state); 2 WAIT N; 3 SAMPLE; 4 EDGES N; 5 TTF (timeout N; [27:24] non-zero = those CMP bits, else MASK); 6 PADCTL (arg b0 DIS level, b1 DIS enable, b2 ADJ level, b3 ADJ enable);
7 LOOP (arg[7:0] K steps, arg[23:8] C repeats; one level; K, C > 0); 8 WAIT_USER; **9 MASK (new: arg[13:0] observation mask over LIVE for EDGES and TTF, reset value 0x3FFF at RUN;
§3 had no way to name 10 pins for EDGES in 4 bits)**. Other ops: step error. Result words: SAMPLE `{t[15:0], pins[9:0], cmp[3:0], twrap, lost}`; EDGES the count (32 bit);
TTF `{timeout, 7'b0, clocks[23:0]}`. `t` counts from RUN and restarts at GO, so a SAMPLE as first op reads t = 1.
Timing (measured by the bench against the TB's own clock counter, not by the engine): every op costs 2 clocks (fetch + execute), WAIT N costs 2 + N; a DRIVE reaches the pin 1 clock
after its execute clock and shows in LIVE/SAMPLE 3 clocks after it (a SAMPLE directly behind a DRIVE still reads the old level: put a WAIT 1 between); the first EDGES window clock
acts 1 clock after the op; TTF measured from the clock after the op, `clocks = K` for a comparator that flips K clocks after PADDLE_DIS is released (op directly after the PADCTL).
Safety as D7: only named pins are driven, never high; END, ABORT, a step error and reset release every pin and both paddle outputs; core_decouple releases the pads.
Overflow: the new word is dropped and OVF set (the kept words are the first 512 in order); SAMPLE flag b0 says words were lost before it.

Benches (iverilog, trx64-sim-env:4 on the builder, niced):
- `diag_probe` (`cores/diag/sim/probe/run.sh`): `PASS tb_probe errors=0 checks=54`, then 13 controls, each `fails as it must`: step list WAIT 1001 against 1000, SAMPLE directly behind a DRIVE, the edge one clock past the EDGES window, time-to-flip expected K+1, LOOP count +1, no GO expected done, LIVE in the wrong bit order; mutated engines: WAIT one clock short, EDGES window one short, TTF seen one clock late, FIFO overwriting when full, outputs not released at END/ABORT, LOOP one round too many; last line `PASS probe (engine bench passes, all controls fail)`
- `diag_pins` (`cores/diag/sim/pins/run.sh`): `PASS tb_pads errors=0 checks=2072` (released pins = the plain input over all 1024 outside combinations; one pin low per `low[k]`; decouple releases), 4 pad controls fail as they must (enable inverted, decouple ignored, drive high, wrong pin) and the wiring check in top.v / layer2_diag.v / pins.xdc passes with 3 wiring controls failing (pin instead of pad level into the synchroniser, kbjoy_low not connected, kbjoy_i not connected); last line `PASS pins (released = old input, drive = low only, wiring checked, all controls fail)`

Resource estimate (no Vivado run yet - CI `large-diag` gives the numbers for S8): 2 RAMB18 (step RAM 256 x 32, FIFO 512 x 32) = 1 BRAM tile; probe + registers about 600-700 LUT, 400 FF
(three 24-32 bit counters, equality compares, the 32 bit edge counter). Against the budget of S8 (1,500 LUT, 4 BRAM) with margin; the host and its menu overlay (3.5 tiles) come on top.

Entries the app half needed (done in the merge branch t175-diag): `tools/build_coreapp.sh` BLUEPRINT_CORES, `tools/check_core_id.py` (pair `cores/diag/rtl/diag_regs.v` / `coreapp/cores/portdiag.cc`, with a byte-form id regex and its selftest), `tools/release_notes_mc.py` (folders `tests/`, `results/`, no ROM, the examples written at the first start).

**Core app half, 2026-11-02 (branch t175-diag-app).**
- Registers and step words follow the gateware's §5 (t175-diag-gw), which moved some of the §3 table: `coreapp/cores/portdiag_regs.h` is the app's copy. An `edges` step compiles to MASK + EDGES (two words), so the compiler keeps a word map for LOOP and for the PC of a WAIT_USER.
- Files: `coreapp/src/diagtest.{h,cc}` (parser, compiler, FIFO decode, thresholds, both result files, runner), `coreapp/cores/portdiag.{h,cc}` (descriptor id `DIAG`, ops `pd.run`, `pd.pick`, `pd.install`, states `pd.port1/port2/cmp/last`). Named `portdiag`, not `diag`, because `coreapp/src/diag.cc` is the host's diagnostics and the objects would collide.
- Idle page = the F5 tab "DIAG": the four media rows Port 1, Port 2, Comparators (the 14 live bits), Last test. `CoreDesc::live_ms` (250) redraws the open menu so they are live; `menu_first` opens the menu after the start and RUN/STOP does not close it (nothing is behind it); `desc_check` accepts freeze mask 0 only with `menu_first`.
- Picker: the Library's ".toml" file action "Run test", and the task "Pick a test and run it" (the application host's file picker over `<folder>/tests`). Runner: a task of its own (`PdHost::spawn`), prompts through the host's dialogs (YES = done, NO = skip the step, ESC = stop the test), result view = a message with the verdict lines and the report name.
- Example tests: the files are `cores/diag/sd/tests/*.toml`; they ride in `diag.cor` as record 8 (2026-11-16, below) and the app writes the missing ones at the start of the core, never over a file (the package holds .cor files only, T148 D8; testers need no copy step and an edited test is kept). The first version embedded them in the application image as `portdiag_tests.inc` (`tools/gen_diag_tests.py`); that is gone.
- Result files: `<folder>/results/<id>_<YYYYMMDD-HHMMSS>.toml` and `.txt` (a counter `n000001` without RTC), both or neither (the card refuses: the first is removed again; if that is refused too the message says the file stays). A suite writes its tests' pairs and one summary pair. Schema additions to D4: `kind`, `[suite] tests/stop_on_fail`, `requires = ["pins.drivable"]`, `[[prompt]] timeout_s`, evals `id/all/each/index/mask/eq/min/max/min_us/max_us/differs/fail`.
- **Left, said loudly (merge branch t175-diag):** (1) done: the prompt timer is built, see below; (2) the board has not run the runner or the engine with it (the co-check runs the app's words on the RTL in simulation, not on hardware: S3-S7 are board steps); (3) done: `tools/check_core_id.py` and `tools/release_notes_mc.py` have their entries; (4) the task picker and the prompt/result dialogs were rendered, the running runner task was not run in ue2emu.
- Tests: `tests/coreapp/test_diagtest.cc` (243 checks), `test_portdiag.cc` (39), render scenes `diag-*.png` (idle, pressed, picker, prompt, result; 1080p and 576p), 12 + 5 negative controls in `tests/coreapp/run.sh`.

**Merge t175-diag (gateware + app), 2026-11-02: assumptions of the app proven against the RTL** (`cores/diag/sim/tb_probe.v` scen_proto, 71 checks; `diag_probe` prints `PASS probe`, 18 controls, 5 of them new):
- ABORT does **not** clear DONE: an engine that runs (or waits) stops with DONE set, an idle one is not touched, only RUN clears DONE. The app and its engine model had assumed the opposite; the app side is fixed (a 1 ms wait after the RUN write, see the app notes below), the RTL stays (control: ABORT clearing DONE fails).
- STATUS read in the clock right after the RUN write still shows the old DONE; four clocks later it shows running (checked).
- Writing 0 to STEP_ADDR (024) resets the step pointer, 024 reads back the count of stored words; words written after the reset land at step 0 (control: the write ignored fails).
- PC (016) points at the WAIT_USER word (index behind MASK / EDGES word pairs too) while the engine waits for GO and at the next one after GO (control: PC advanced past it fails).
- FIFO words come out in step order, nothing added to them: EDGES count exactly, TTF exactly `{timeout, 7'b0, clocks}` (controls: a tag in the EDGES word, the FIFO popped by byte 0).

**Co-check app compiler -> RTL engine -> app decoder (S2, `cores/diag/sim/co/run.sh`, bench `diag_co`, tier 1).** `tests/coreapp/diag_co.cc capture` runs the app's own runner (`diag_run_file`) against a recorder that keeps the step words of each shipped example test (pins, paddle, stick2, cd32); `cores/diag/sim/tb_co.v` loads them into `probe.v` + `diag_regs.v` through the register bus exactly like the runner (ABORT, CLEAR, STEP_ADDR = 0, words, RUN, poll, PC at a wait, GO, FIFO pop; after an earlier run that left STEP_ADDR at 7 and DONE set) against a model of the plugged peripherals (`co/models/<id>.hex`: pins held low per prompt phase, comparator charge times K clocks after PADDLE_DIS is let go); the engine's STATUS, FIFO words and wait PCs go to a file, `diag_co replay` feeds them to the same runner, and `co/check.py` compares the result file it wrote (decoded samples, timestamps, charge times, prompt states, eval verdicts) with `co/models/<id>.expect`. Defects it found and that are fixed in the app: the PADCTL argument had the bits ordered DIS level, ADJ level, DIS enable, ADJ enable, the engine has DIS level, DIS enable, ADJ level, ADJ enable (the gateware spec above is the reference); `paddle.toml` let PADDLE_DIS go before the charge measurement and held it during the time-to-flip (T47 hypothesis H: DIS high discharges the capacitors, so each measurement discharges with dis = 1, waits, lets go with dis = 0, then times the comparator); the runner polled STATUS right after the RUN write (a stale DONE after an ABORT: it now waits 1 ms; `tests/coreapp` engine model changed to the RTL's ABORT/DONE behaviour and lags STATUS after RUN, control `diagtest_runlag`). Controls (7, each must fail): an op code shifted in `portdiag_regs.h`, the old PADCTL bits, the SAMPLE fields swapped in the engine, PC advanced past WAIT_USER, the STEP_ADDR write ignored, the FIFO popped by byte 0, the old paddle DIS polarity.

**Prompt timer (S5 "MISSED"), merge branch t175-diag.** `AppDlg::buttons_timed(title, text, buttons, timeout_s)` (`coreapp/src/appdlg.{h,cc}`): the time runs from the moment the question is on the screen (the menu task takes it up in `service()`), `service()` closes it at the deadline with the answer `DLG_TIMEOUT` (8) and counts `DlgView::remaining_s` down in whole seconds (rounded up), which bumps the dialog revision so the card is drawn again; `ui.cc` shows "NN s left" left of the YES/NO buttons (amber in the last ten seconds). `PdHost::ask(title, text, timeout_s)` returns 1 yes, 0 no, -1 ESC, -2 time ran out; `PdUi::prompt` maps -2 to `DUI_TIMEOUT`, so the runner marks the step MISSED, sends GO and carries on; `timeout_s = 0` waits for ever. Tests: `test_appdlg` (answer in time, closes after 2 s with `DLG_TIMEOUT` between 1.9 and 3.5 s, the view counts 2, 1, no timer stays open), `test_portdiag` (stick2 with the answers yes, time out, no, yes gives OK, MISSED, SKIPPED, OK, each question carries 120 / 30 / 30 / 30 s; ESC stops the test), controls `dlg_timeout`, `dlg_seconds`, `dlg_countdown`, `portdiag_timeout`, `portdiag_tmoarg`; the render bench draws the timed prompt with the countdown.

**Render and RAM, merge branch t175-diag.** Render bench (`tests/coreapp/render/run.sh`, 195 checks, 0 failed): the picker picture goes Library -> usb0 -> cores -> diag -> tests (a relist per level, as the Library bench does) and checks that the title is the tests folder and that `cd32.toml`, `joystick.toml`, `paddle.toml`, `pins.toml`, `stick2.toml` are listed, at 1080p (960 x 540 canvas) and 576p (720 x 576); the timed prompt shows "30 s left" beside YES / NO. RAM, from `coreapp.elf` of the build (RISC-V, u64-build-env:ci): the application owns 0x00030000-0x00E8FFFF; `_end` = 0x00DE15C0, so the one contiguous hole above the image (the libc heap grows up in it, the stack down from `__heap_limit - 4`) is 715,328 bytes; the build before this merge ended at 0x00DB0944 (915,132 bytes): the diag core app costs 199,804 bytes of the image, 150,524 of them bss (statics: `g_toml` 40,960, `g_file` 40,552, `g_text` 16,385, `g_txt` 12,288, `g_res` 2,948, `g_prog` 1,668, the path buffers), the rest code and the example tests in rodata. The FreeRTOS heap is a static array inside the bss (`ucHeap` 7,995,392 bytes) and does not shrink with it; the runner task takes a 102,400 byte stack from it for the length of one run (16 x configMINIMAL_STACK_SIZE words; the "App" task has 32,768): how much of `ucHeap` is free with the menu open is not measured (T104.1 Q7). The image is one for every core, so the 200 KB are not the diag core's alone. Not shrunk: 715 KB stay contiguous and the statics are one run's worth of buffers; a smaller `g_txt` / `g_toml` (shared buffer) would give back 12 KB.

**Decision (Alex, 2026-11-03, translated): "The core is only for measuring - so it may do gateware too."** The diag core's gateware (`cores/diag/`: engine, step ops, register block, and the app side `portdiag_regs.h` / TOML vocabulary) may change for the CD32 question; never `host/`, `contract/` or another core. Checked first, from `host/c64u.xdc` and `host/top.v`: the FPGA reaches the joystick-port pot lines only through `PADDLE_DIS` (N22, output, `top.v:78`/`1615`), `PADDLE_ADJ` (AA20, output, `1616`) and the four comparator inputs `PADDLE_CMP[3:0]` (E14, L6, N5, L4, `top.v:77`); `CIA_PWM[1:0]` (N17, P21) are user-port power / LED-strip pins (`host-app/software/system/u64.h:78`) and not in `top.v`. So no FPGA pin can drive pin 5 independently of pin 9; the levers are DIS and ADJ, which PADCTL already sets to any level, enable or high-Z, with a wait of one clock resolution. A new gateware op would invent a lever the board does not show; the CD32 test stays TOML-only (§5, CD32 v2).

**cd32 v2 and stick2 v2 (2026-11-03, branch t175-cd32; first tester results: discussion #30, mouSTer USB-to-DB9 adapter, joystick / CD32).**
- **What cd32 v1 lacked.** It never drove the mode line (only `padctl dis=0 adj=0`, both outputs low at once), clocked pin 6 eight times and judged with `bits.flags`, `min = 0`, which holds for any data: on the tester's board all eight samples were equal (`P2 F0`, `CMP 0 0 1 0`) and it still passed. Version 1 also held the red button while the clock ran, so nothing in the run could show whether the pad shifted.
- **Protocol source (code, not comments).** `cores/amiga/rtl/src/minimig-aga/userio.v`: the Minimig models the pad's side of the port. Pin 5 of port 2 is `joy2_pin5 = ~(potreg[13] & ~potreg[12])` (line 153, low only while POTGO drives it low). While it is high the register loads `{_djoy2[5], _djoy2[4], _djoy2[6], _djoy2[7], _djoy2[8], _djoy2[9], _djoy2[10], 1'b1}` (lines 202-206); while it is low a rising edge of pin 6 (`_fire1_dat && !fire2_d`, line 203) shifts left with a 0 (line 207) and the MSB goes to the data line `potcap[3]` (line 165, port 2 pin 9) with pin 5 shown on `potcap[2]` (line 169). So: pin 5 low = shift mode, read 0 (before any clock) = `_djoy2[5]` (blue), read 1 = `_djoy2[4]` (red), then yellow, green, forward, reverse, play, read 7 = 1, from read 8 on 0. This is one implementation's model of the pad (the module is the emulated pad, `cd32pad` is tied to 0 in this build, line 91); it is not a measurement of a real pad, so the ID check below is marked as such. The data and mode pins reach the diag core only through the comparators; `cmp3` = port 2 pin 9 was measured with a stick (`stick2`, below), `cmp2` = pin 5 is the same pairing and still a hypothesis.
- **v2 design (`cores/diag/sd/tests/cd32.toml`, TOML only).** Three prompts: nothing held; hold red and keep holding through RETURN; let go. Blocks, each: [`padctl` of the setting], wait 5 ms, `sample` (read 0), then 9 times {pin 6 low, wait 2 ms, release (the rising edge), wait 2 ms, `sample`} (reads 1 to 9: the seven buttons, the ID bit, one more). Settings: the released engine first (`rel`, before any PADCTL: `padctl` cannot release both lines in mid-test, so it is the first block of each of the first two prompts), then `d0` (DIS low, ADJ released), `d1`, `d0a0`, `d0a1`, `d1a0`, `d1a1` (`d<level>a<level>` = both lines driven). Run order: `rel` with nothing held, `rel` with red held plus all six settings with red held, then all six with nothing held, so red is pressed once and let go once. Waits: 5 ms after a lever change and 2 ms after every edge, the discharge time of `paddle.toml`; the pot lines have capacitors of unknown value, so the waits are generous rather than derived (2 ms is the discharge time `paddle.toml` already uses, 5 ms leaves the levers time to act); the whole run takes under a second. A different value is a one-line edit.
- **Evals that can fail, per setting `x`:** `x_pin5` (read 0 with nothing held shows cmp2 low: the setting pulled pin 5 low), `x_idle` (read 1 with nothing held shows cmp3 high), `x_reads` (cmp3 of read 1 differs between red held and nothing held: `differs`), `x_ok = all [x_idle, x_reads]` (verdict "WORKS: x reads the CD32 pad"), `x_id` (read 8 low, from the Minimig model; only meaningful where the data moved). All 30 evals are independent, the file has no "any setting" eval because `all` is an AND; the answer is the set of `x_ok` lines: one PASS names the setting, six FAIL lines (each "no pad read with x") mean no lever setting reads the pad, i.e. pin 5 cannot be driven apart from pin 9 with DIS and ADJ on this board. The raw samples of every block (`n0_x`, `n_x`, `r0_x`, `r_x`; `n` nothing held, `r` red held; `_x[k]` = read k+1) are in the `.txt` and `.toml` whatever the evals say. Result size: `.txt` 14.3 KB, `g_txt` raised from 12 to 24 KB (BSS).
- **Pins checked for a lever of its own** (Alex's permission to change the gateware, quoted above): none exists, so no gateware change.
- **Negative control** (`tests/coreapp/test_diagtest.cc`, `test_cd32_v2`; the engine model got `padctl`, pin 6 rising edges and a `cmp_fn` pad hook in `diag_engine.h`): (1) the tester's recording, every sample `CMP 0 0 1 0` whatever is held: every `x_reads` and `x_ok` must FAIL (v1 passed this); (2) a pad that shifts under DIS high alone: `d1_ok` PASS "WORKS: d1 (DIS high, ADJ released) reads the CD32 pad", `d1_pin5`, `d1_reads`, `d1_id` PASS, the other five settings FAIL; (3) pin 9 held low by the lever in both runs: `d1_idle` and `d1_ok` FAIL. Source controls in `tests/coreapp/run.sh`: `diagtest_differs` (the `differs` comparison made vacuous) and `diagtest_superseded` (the replace rule off) must make the bench FAIL. Co-check `cores/diag/sim/co`: `models/cd32.hex` / `.expect` now model a pad that mirrors red on pin 9 so the engine, the compile (9 x 5-step loop, 18 blocks, 127 steps) and the decode meet in RTL.
- **stick2 v2 (version 2).** Button 2 of an Amiga stick is on pin 9, a pot line: the tester saw no port line move but comparator bit 3 go 1 -> 0, and v1 called that "FAIL button 2 moves no port 2 line". v2 has one eval `b2_moves`: the raw sample word of `b2`, masked to the port 2 pins and the four comparator bits, `differs` from `idle`: PASS when a port 2 pin or a comparator bit moved, FAIL only when neither did (the release check stays). The verdict cannot say which one (the vocabulary has no OR of two evals; `all` is an AND); the `b2` and `idle` samples in the result files show it. Prompt timeouts 300 s, and each prompt says to keep holding until RETURN (v1: 30 s and the b2 prompt was MISSED).
- **Decision (Alex, 2026-11-16, translated): "But the tests should not go into all cores!!! Why is that?" ... "They are TOML. They can be loaded / imported."** Asked how they then reach testers, Alex chose "ship them in diag.cor: the TOML files lie as a data record in diag.cor; at the first start of the diag core the app writes missing files to the SD (never overwrite). Only diag.cor carries them." This replaces the embedding in the application image (one image for every core, T93 D14), which had overflowed the image region and had been squeezed with deflate (2026-11-16, be6a1f048; removed again).
- **Record 8 "files" (2026-11-16).** Generic, not diag-specific (T192 D24 / T213: core data lives with the core, capabilities in the app). `cores/<id>/core.toml` names `install_dir` (every file below `cores/<id>/<install_dir>/` is installed into the core's folder at the same relative path: diag `sd` gives `<core folder>/tests/<name>.toml`) and `install_old` (`cores/<id>/<install_old>/<name>.v<N>.<ext>`: earlier shipped texts). `tools/pack_large_cor.sh` and `tools/pack_cores_mc.sh` read the keys and give `cor_pack.py` `--files DIR --files-old DIR`; the record is `"TRXF"`, u32 version 1, u32 count, per file `u32 entry_len, u16 path_len, u16 n_old, u32 size, n_old x {u32 size, u32 hash_hi, u32 hash_lo}, path, bytes, padding to 4` (the earlier versions are carried as size + FNV-1a 64 of the text, in the record, not in the app); manifest `files_size`, `files_sha256`, `files_count` with `"records": 1`; the host's `cor.cc` reads it as `COR_PART_FILES` and hashes it with the other parts at the start (format: docstring of `tools/cor_pack.py`, summary `docs/dev/core-guide.md`). The application (`coreapp/src/instcore.cc`, `instfiles.cc`) reads the record of its own `.cor` once at the start of the core (`run.cc`, before the core is released; only when the manifest name is the running core's), checks its SHA-256, and writes each file that is missing; a file that is there stays, except one whose size and FNV-1a 64 equal an earlier version listed for it (T175's replace rule, unchanged: an edited file, or a name nobody shipped, is never touched). `pd.install` ("Write the example tests") runs the same function. A bundle without the record, another core's bundle and a start without a `.cor` install nothing. Log line: `[core] <manifest name>: N file(s) written to <core folder>`.
- **How testers get the new files.** The writer used to write only missing files, so a tester who ran v1 would keep it. The record's earlier-version list (`cores/diag/superseded/<name>.v<N>.toml`: avout.v1, cd32.v1/v2, cd32keys.v1, stick2.v1/v2) lets an unedited copy be replaced; an edited file (any byte different), or any other name, is never touched; it runs on the next core start and on "Write the example tests". Chosen over file-name versions (two files with the id `cd32`, the old one still in the picker) and over a version number compare (it cannot tell an edit from an original). Bench: `test_bundle` (diag.cor as the packer makes it: v1 replaced, edited kept, second start writes nothing, read only card -1, damaged record refused, another core's bundle installs nothing).
- **Build notes of the move (2026-11-16, `tools/dev.sh riscv --branch followup-1116`).** Removed from the application: `coreapp/cores/portdiag_tests.inc`, `tools/gen_diag_tests.py` (no lint check is left: the record is made from the files when the `.cor` is packed, so it cannot be stale), `dt_inflate` (about 150 lines), the embedded texts and the `g_inst` buffer (16,385 bytes bss), `diag_install_examples`. Linker margin to the region end (`__heap_limit` 0x00E90000 minus `_end` of `coreapp.map`): 0x00E8AAB4 before, 21,836 bytes; 0x00E8476C after, 47252 bytes; +25416 bytes (`_edata` 0x1f4964 -> 0x1f295c). The first attempt called `cor_verify_part`, which pulls a 64 KiB static buffer (`hash_part`) into the image and overflowed it by 17,548 bytes; the record is read with `cor_read_part` (hash checked, 36 KiB heap for the length of the call) instead. The board's small printf does not take `%.*s` (a precision from the arguments): the folder names are built by hand.
- **Proof (2026-11-16).** `tests/coreapp/test_bundle.cc` 91 checks, 0 failed (diag.cor of `gen_bundle.py` through the real packer: 10 files in the record; start with cd32 v1 unedited + stick2 edited + pins edited + a file of the person's own: 8 written, v1 replaced, the three others kept, second start 0, read only card -1, a damaged record -1 and nothing written, nofiles.cor / the Test Machine's descriptor / new.cor / no bundle: 0 written); `test_portdiag` 334 checks (`pd.install`, start: 10 files then 0), `test_diagtest` 434 checks (the examples through the installer), `tools/test_cor_pack.py` 52 tests (roundtrip, damaged, bad paths, earlier versions paired); controls `diagtest_overwrite` (no `stat` guard: edited file overwritten) and `diagtest_superseded` (replace rule off) fail the bench as they must. ue2emu (`--core tests/coreapp/emu/profiles/diag.toml`, a stick with `cores/diag.cor` packed with the application of this tree): `[core] Port Diagnostics: 10 file(s) written to /Usb0/cores/diag`, the ten files are in `cores/diag/tests/`; second start with cd32.v1 unedited, pins edited, paddle removed: `2 file(s) written` (cd32 replaced, paddle written, pins kept).
- **Run 2325 fix (2026-11-16).** `test_correc` (tests/hostapp): record type 9 is the unknown one (8 is "files" since record 8), and record 8 with a payload the manifest does not announce is refused: `tests/hostapp/test_correc.cc` "all ok". `test_keywalk` (tests/coreapp): Port Diagnostics now starts like the board - `c.install_files = core_files_install`, diag.cor (DIAG_COR, the real packer) on the card only while the installer reads it, install before every probe's menu start - so the F1/home and F3/browse screens show the 10 examples again and `golden/keywalk.txt` is unchanged: `test_keywalk: 2 checks, 0 failed`.
- **How testers get the new files (first version, history; the record-8 text above is the present state).** The writer used to write only missing files, so a tester who ran v1 would keep it. The first answer was `diag_install_examples` (`coreapp/src/diagtest.cc`) replacing a file that is byte for byte a text an earlier version shipped, with `tools/gen_diag_tests.py` putting `cores/diag/superseded/<name>.v<N>.toml` into `portdiag_tests.inc` as `portdiag_superseded[]`; all three are gone (record 8 of 2026-11-16: the earlier texts travel as size + FNV-1a 64 in the record, `coreapp/src/instfiles.cc` does the replacing). The rule is unchanged: a file the person edited (any byte different), or any other name, is never touched; it runs on the next core start and on "Write the example tests". Chosen over file-name versions (two files with the id `cd32`, the old one still in the picker) and over a version number compare (it cannot tell an edit from an original). Bench: `test_bundle` (v1 replaced, edited kept, a second start writes nothing).
- **`menu_input` (2026-11-15).** Board, real CD32 pad in port 2, cd32keys v1: Alex, "with the d-pad it did not work at all. Also it looked as if red == Return" (translated). Cause: the generic core app feeds the joystick ports into the menu as keys (`coreapp/src/run.cc` `matrix_kb = new Keyboard_C64(...)`, joystick scan in `host-app/software/io/c64/keyboard_c64.cc` `scan()`: fire = RETURN, directions = cursor keys and ESC), and `getch_nowait()` hands those to the prompt dialog; a prompt that says "hold a direction/button, then press RETURN" was answered by the held pad (SKIPPED for up/down/left/right, red = RETURN). Decision (Alex, translated: "make it configurable in the test"): a key in `[test]`, `menu_input = "any"` (default, today's behaviour) or `"keyboard"` (the joystick ports feed no menu key from the start of the test until its result dialog; the keyboard and the USB keyboard answer). Mechanism: `DiagEnv.joy_menu` -> `PdHost::joystick_menu` -> `Keyboard_C64::set_keyboard_only` (existing flag, the host menu uses it since T135 Q9); restored on every exit of the run, before the result dialog. Shipped tests set it where the prompts ask to hold a pad or stick: cd32 v3, cd32keys v2, stick2 v3 (their prompts say "hold, also while you press RETURN"); pins, paddle (levers, no holding while RETURN) and the joystick suite keep "any". Earlier texts of the three are in `cores/diag/superseded/` (cd32.v2, stick2.v2, cd32keys.v1) so unedited copies are replaced. Scenario S-joy: a test that asks to hold a direction completes with the direction held when `menu_input = "keyboard"` and is answered by the direction when "any" (bench `test_menu_input` in `tests/coreapp/test_diagtest.cc`: the runner against a model of the menu input; the scan of `keyboard_c64.cc` itself is not run on the bench). Board proof pending: cd32keys v2 with the CD32 pad.
- **Open hardware question** (what the tester's board answers): can DIS / ADJ pull pin 5 low and leave pin 9 to the pad? T47 hypothesis H says DIS discharges all pot lines at once (pin 5 and pin 9 together) and ADJ is a threshold or injection; the six settings are the whole lever space of the diag core. The first v2 result file decides it.

## §6 Questions for Alex

1. Is one test file per measurement (with several prompts in it) right, or do you want a suite file that lists tests to run in a row?
   **Answered (Alex, 2026-11-02): "Both? So settable through the toml?"** A `.toml` is either one test (several prompts) or a suite that lists tests (by file name) to run in a row; the file says which (`kind = "test"` / `kind = "suite"`).
2. Result format: TOML with raw sample arrays (D5), or do you also want a plain text report next to it for reading on the screen/phone?
   **Answered (Alex, 2026-11-02): "Both."** Every run writes `<test>_<date>.toml` (raw samples, machine-readable) and `<test>_<date>.txt` (readable report for the screen and the phone).
3. Should the diag core also boot the screen with the live pin state (all 10 pins and 4 CMP bits updating) as its idle page, so you can see a button work before any test?
   **Answered (Alex, 2026-11-02): "Yes."** The idle page is the live state of the 10 port pins and the 4 comparator bits.
4. Which ports hardware do you have for S4-S6 beyond the Competition Pro (paddles, a CD32 pad, a 1351)?
   **Answered (Alex, 2026-11-02): "Only joysticks."** Alex proves the pin, drive and 2-button tests on his board; paddle, CD32 and mouse tests ship as `.toml` files for testers who own the hardware (e.g. a tester, issue 22) and come back as result files.
5. Core id and menu name: `diag` / "Port Diagnostics", experimental first?
   **Answered (Alex, 2026-11-02): "Yes."** Id `diag`, menu name "Port Diagnostics", `experimental = true` first.
6. Does the core need the C64 at all (the ports are the C64U's, but the 6510 side idle), or is it a bare "no machine" core with the host menu only?
   **Answered (Alex, 2026-11-02): "Measuring core."** No machine: probe engine, register block, pixel layer and the core app only.
- 2026-11-15, board with a real CD32 pad (Alex), app of branch diag-joy-menu: `cd32keys` v2 8/13: red pin 6 (P2 F0), blue pin 9 (CMP 1 1 0 1), d-pad U/D/L/R each its pin; yellow, green, LF, RF, play move nothing (they exist only in the shift mode). `pins` 31/31. `cd32` v3 6/24: pin 5 (cmp2) reads low only in d0a1 (DIS low, ADJ high), and there pin 9 reads low too; no setting has pin 5 low with pin 9 free. Cause read in `host/c64u.xdc`: port pins 1-4 and 6 are FPGA pins (`KBJOY_1_JOY[4:0]`, `KBJOY_2_JOY[4:0]`, :322-331), pins 5 and 9 are not: only the shared `PADDLE_DIS` (N22), `PADDLE_ADJ` (AA20) and the read-only `PADDLE_CMP[3:0]` (:408-474) reach them. The CD32 shift mode is impossible on this board; the pad works as a 2-button joystick. GitHub #22 commented and closed with Alex's go. Replacement (USB pad as CD32 pad in the Amiga cores) goes into its own spec.
- Record 8 board (Board test 2026-11-16, package main-13c2cde9 (run 2344), Alex in the test list): the example tests are there after the first start of diag.cor and one runs - PASS. Alex's note: "Aber, jedes Cursor bewegen zeichnet wieder alles neu ... das ist schlechte UX; auch wenns der diag core ist. Das braucht ein TODO nachdem wir das mit den Apps geradegebogen haben oder dabei" (every cursor move redraws everything, bad UX even on the diag core; needs a TODO, after or during the app rework) -> T231.
