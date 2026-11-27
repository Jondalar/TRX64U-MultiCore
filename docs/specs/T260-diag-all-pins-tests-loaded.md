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
- How the package or the registry carries the core (T239.5 D15: later). Answered 2026-11-26 by T239.5 D21: every package up to the
  pre-release carries `diag.cor` and `SD/cores/diag/`; the release bundle does not.

## §5 Build notes

### Gateware half (branch `t260-gw`, 2026-11-25)

#### Register map v2 (the contract the app half reads; written before the RTL)

Core window page 0, byte offsets, as T175 §5 unless said. "Changed" marks every place that differs from the T175 map. Engine clock = `cpu_clk`
(100 MHz, 1 tick = 10 ns, TICKS_US x 256 = 0x6400). All registers are 8 bit; multi-byte values little endian.

| reg | R/W | meaning |
|---|---|---|
| 000-003 | R | ID "DIAG" (unchanged) |
| 004 / 005 | R | CAPS 0xC0 / CAPS2 0x02 (unchanged) |
| 006 | R | DCAPS **changed:** b0 joystick pins open drain (kept), b1 paddle pins driven by the engine (kept), b2 A/V encoder (kept), **b3 pin bank v2 (0C0-0DF ROM, DRIVE by index), b4 capture (100-14F)**; value 0x1F |
| 008-00B | R | BUILD (unchanged) |
| 00C-00F | R | TICKS_US x 256 (unchanged) |
| 010 / 014 | RW / R/W1C | IRQ_EN / IRQ_CAUSE, b5 = the engine stopped or waits for the user (unchanged) |
| 011 | R | STATUS **changed (b5-b7 new):** b0 running, b1 waiting for GO, b2 done, b3 FIFO overflow (sticky to CLEAR), b4 step error (unknown op, bad LOOP, bad WRITE_REG range, CAPTURE word error), **b5 refused (sticky until RUN; a step was refused and skipped, see 017/018), b6 capture sampling, b7 guard trip (sticky until RUN: a guarded output was released because GAME or EXROM went low)** |
| 012 | W | CTRL: b0 RUN, b1 ABORT, b2 GO, b3 CLEAR FIFO (unchanged), **b4 CAP RESET (ring write pointer, CAP_COUNT, CAP_LOST, overrun to 0; refused while sampling)** |
| 016 | R | PC (step index of the last fetch; unchanged) |
| **017** | R | **REF_PC: step index of the FIRST refused step since RUN** |
| **018** | R | **REF_CNT: refused steps since RUN, saturating at 255** |
| 020-023 | W | STEP_DATA, the write of 023 stores the word at STEP_ADDR and increments it (unchanged) |
| 024 | RW | STEP_ADDR (unchanged; 256 words) |
| 030-033 | R | FIFO_DATA, the read of 033 pops (unchanged) |
| 034-035 | R | FIFO_LEVEL 0..512 (unchanged) |
| **040-04F** | R | **LIVE changed: the snapshot, 128 bits, byte k = pins 8k+7..8k, bit = pin index (table below). The T175 layout (joystick in 040-041) is gone.** Synchronised (2 FF); an input pin shows the ball, an output pin what the FPGA drives (see `dir`) |
| **042-043** | | **(T175 DRV: gone, see 070)** |
| 050-065 | RW | the A/V encoder knobs of T201 / T259 (unchanged). A step list may write 050-06F with WRITE_REG |
| **070-07F** | R | **DRVEN: bit per pin index = the engine drives or overrides this pin now (release = 0)** |
| **080** | R | **PIN_COUNT = 121** |
| **081** | R | **GUARD: b0 = GAME or EXROM is low right now (guarded pins are released while it is 1)** |
| **0A0** | RW | **PIN_IDX (7 bit): the pin whose ROM entry 0C0-0DF shows** |
| **0C0-0DF** | R | **PIN_ROM: 32 bytes of entry PIN_IDX (an index >= PIN_COUNT reads 0).** bytes 0-15 name, ASCII, NUL padded (15 characters at most); 16 dir (0 in, 1 out, 2 inout); 17 class (0 none = not drivable, 1 od, 2 pull, 3 push, 4 pz, 5 pzh); 18 flags (b0 guard, b1 inv, b2 buf, b3 pwr, b4 gen, b5 host); 19 group (0 slot, 1 joy, 2 paddle, 3 c2n, 4 iec, 5 ui, 6 led, 7 av, 8 misc); 20 idle (0, 1, 2 = high-Z or host-owned); 21-31 0 |
| **100-11F** | RW | **CAP_SEL[0..31]: the pin index of capture lane j (7 bit). 0x7F = lane unused (reads 0). Reset 0x7F.** |
| **120** | R | **CAP_STATUS: b0 sampling, b1 all lines written to the ring (set when a capture ended and the writer is idle), b2 overrun (a line was dropped; the ring has a hole), b3 reader busy, b4 reader data valid** |
| **124-127** | R | **CAP_COUNT: samples taken since CAP RESET** |
| **128-12B** | R | **CAP_LOST: samples lost to overrun since CAP RESET** |
| **12C-12F** | R | **CAP_LINES: 16 byte lines of the ring used since CAP RESET (a capture ends on a line boundary, the next one starts at ring byte 16 x CAP_LINES)** |
| **130-133** | RW | **CAP_RADDR: byte offset into the ring, bits 3:0 read 0, bits 31:23 read 0** |
| **134** | W | **CAP_RCTRL: b0 = read the 16 bytes at CAP_RADDR into CAP_RDATA; CAP_RADDR += 16 when the data is valid** |
| **140-14F** | R | **CAP_RDATA: the 16 bytes of the last read line (byte 0 = ring byte at the old CAP_RADDR)** |
| page 6 | | forwarded to the host's pixel layer (unchanged) |

**Pin bank (P1).** 121 pins, index 0..120, source `cores/diag/rtl/pins.toml`, read at run time from the ROM (the app never hard-codes an index).
Groups: slot 47 (ADDR 0-15, DATA 0-7, RWn, ROML/ROMH/IO1/IO2, PHI2, DOTCLK, BA, ADDRDIR/ADDROEn/DATADIR/DATAOEn, RST_O, RST_I, GAME/EXROM/DMA/IRQ/NMI,
SID1/SID2/SID_ADDR, PWR); misc (CIA_RSTn, CIA_BUFFER_EN, CIA_PWR_EN, KBJOY_INT, BLING_KEYB, HDMI_HPD) 6; joy 11 (KBJOY_1_JOY0-4, KBJOY_2_JOY0-4, KBJOY_RESTORE); paddle 6
(CMP0-3, DIS, ADJ); c2n 4; iec 9 (ATN/CLOCK/DATA/RESET _I, ATN/CLOCK/DATA/RESET/SRQ _O); ui 3 (UI_BUTTON0-2); led 8 (LED_BOARDn, LED_CASE1n-3n, LED_DATA,
LED_CLOCK, BLING_LED, ETH_LED_YELn); av 27 (AVID_R/G/B 2-9, AVID_CLK, AVID_SYNCn, AVID_FB). Names are the top's port names, a vector bit is appended without brackets
(`SLOT_ADDR12`, `KBJOY_2_JOY4`, `UI_BUTTON1`).

**Drive semantics.** DRIVE pin MODE: 0 RELEASE, 1 LOW, 2 HIGH, 3 HIZ. "LOW" is the level at the ball for `push`, `pz`, `pzh`, `od`; for `pull` pins (IEC `_O`,
SLOT_RST_O) LOW means the line is pulled low and the output is 1 (the snapshot shows 1). A step is **refused** (skipped, STATUS b5, REF_PC/REF_CNT, the pin stays as it
was) for: an index >= PIN_COUNT; a class `none` pin; HIGH on `od` or `pull`; HIZ on a class other than `pzh`; a guarded pin while GAME or EXROM is low. RELEASE of any valid pin is
always accepted. Guarded pins are also released at once whenever GAME or EXROM goes low (b7). Never driven: power enables (SLOT_PWR, CIA_PWR_EN, CIA_BUFFER_EN), inputs, the
AVID pins (the generators own them; set them with WRITE_REG). Released: `push` pins return to the idle value (the host's own function for the LEDs), `pz` pins to high-Z, `pzh`
pins to driven 1. Every output is released at END, ABORT, a step error, decouple and reset. The slot pins act only while the host has released the expansion port (T135
D6, `port_lock` in mono/top.v); under the lock the DRIVEN state is visible in 070-07F but the balls stay idle.

**Step word (engine v2).** `[31:28]` op; one word unless said; an op costs 2 clocks plus its own time (a second word costs 1 more clock; SAMPLE costs 2 + 5 for its five pushes, measured exec to exec: 7; WAIT N: 2 + N; DRIVE: 2). A DRIVE's output changes 2 clocks after its execute clock and shows in the snapshot 2 clocks after that (the synchroniser); put a WAIT between a DRIVE and a SAMPLE that must see it. Written through STEP_DATA as before.

| op | name | word 0 | word 1 / result |
|---|---|---|---|
| 0 | END | | stop, release, done |
| 1 | DRIVE | `[27:26]` mode, `[6:0]` pin index | |
| 2 | WAIT | `[23:0]` N ticks (exact; 0 = none) | |
| 3 | SAMPLE | | result 5 words: t (32 bit, the clocks since RUN or GO at the op's execute clock), then the snapshot words 0..3 (pins 31..0, 63..32, 95..64, 127..96) |
| 4 | EDGES | `[27:26]` edge (0 any, 1 rising, 2 falling), `[6:0]` pin | word 1 = N ticks (32 bit). Result 1 word: the count of the chosen edges of that pin during N ticks |
| 5 | TIME_TO_EDGE | as EDGES | word 1 = timeout ticks (31 bit). Result 1 word `{timeout, clocks[30:0]}`; clocks = 0: the edge is seen on the first clock after the op |
| 6 | (reserved) | | step error |
| 7 | LOOP | `[7:0]` K following words, `[23:8]` C repeats (one level, K and C > 0; counts words, so a loop must not end between the two words of an op) | |
| 8 | WAIT_USER | | halt until GO, the time base restarts |
| 9 | WRITE_REG | `[27:16]` register offset (0x050-0x06F only, else step error), `[7:0]` data | the register block takes it as a bus write |
| 10 | CAPTURE | `[23:0]` K ticks per sample (K >= 1, else refused) | word 1 = M samples (1..2^21 and the ring must have room, else refused). Starts the capture in the BACKGROUND and the list goes on |
| 11 | CAPWAIT | | wait until no capture is sampling |

The observation of EDGES / TIME_TO_EDGE is of the synchronised bit (2 FF) of the snapshot; "any" counts rising and falling. The old ops MASK (9) and PADCTL (6) are gone:
PADDLE_DIS / PADDLE_ADJ are pins 67 / 68 (class `pz`: HIGH / LOW enable the output, RELEASE = high-Z), and the observation of several pins is SAMPLE or CAPTURE. END and
ABORT stop the engine; a running capture continues after END (it is only an observer) and stops at ABORT, decouple and reset.

**Capture and the ring.** Sample i is one 32 bit word: bit j = the level of the pin CAP_SEL[j] (lane j) as the outside stood in clock `X + i*K` (clocks of cpu_clk), X = the execute clock
of the CAPTURE op's second word (the 2 FF synchroniser and the lane mux pipeline are accounted for; measured by the bench against the TB's own history, S4). Samples are stored at ring byte offset 4*i in the order taken (four per 16 byte line, sample 0 in bytes 0-3, little
endian); a following capture starts on the next line (CAP_LINES says where) and goes on until CAP RESET. The ring is DDR2 bytes **0x06000000-0x067FFFFF** (8 MiB, 524,288 lines, 2,097,152 samples).
The app reads it back through CAP_RADDR / CAP_RCTRL / CAP_RDATA (the RISC-V cannot address that range: its bus ends at 64 MB). Proof of the range: see "DDR2 ring" below.

**Changed against T175 §5 in short:** 006 bits; 011 bits 5-7; 012 bit 4; 016 stays, 017 / 018 new; LIVE 040-043 replaced by the 128 bit snapshot 040-04F; DRV 042-043 replaced
by 070-07F; the step word (pin by index, MASK / PADCTL / TTF gone, EDGES / TIME_TO_EDGE two words, SAMPLE five words, WRITE_REG / CAPTURE / CAPWAIT new); ops 0, 2, 7, 8 as before.

REF_PC of a two-word op is the index of its second word. A refused EDGES / TIME_TO_EDGE (pin past the bank) still takes its second word and pushes 0xFFFFFFFF.

#### Built (what the RTL does, read back from the files)

- `cores/diag/rtl/pins.toml`: the one source (121 pins). `tools/diag_pins_gen.py` makes `rtl/diag_pintab.vh` (IX_<name>, DG_N, `dgpin_cls`: {guard, class}), `rtl/diag_pinrom.v` (ROM,
  4096 x 8, one RAMB36) and `mono/diag_dgin.vh` (dg_in); `--check` is the lint (S3): files current, every top.v port a bank pin or excluded, direction and vector range agree,
  every drivable pin named in top.v.
- `rtl/probe.v` engine v2, `rtl/diag_cap.v` capture + ring writer + reader, `rtl/diag_regs.v` register map v2 (the A/V block 050-065 untouched), `rtl/diag_pads.v` `diag_pads`
  (joystick, unchanged) and `diag_slot_pads` (A / D / R/W of the cartridge port), `mono/layer2_diag.v` (ports dg_in / dg_en / dg_o / dg_z / dg_dc, e_* from diag_cap),
  `mono/top.v` (+ `top.v.diff`): C2N, PADDLE, IEC, SID, slot control, LED, BLING_LED, ETH_LED pads take the bank's drive; `build_mono.tcl` reads the new files and `rtl/`, `mono/` as include directories.
- Guard: `dg_en` of a guarded pin is `en & ~guard_now` (combinational, 2 FF behind the balls), the pin's flops are cleared while GAME or EXROM is low: a guarded output is let go for
  good, not parked until the cartridge leaves. Decouple: `dg_dc` = `dc_cpu` clears every pin and stops the engine; the top also gates every pad with `core_decouple` / `port_off` as before.
- A0-A11 behind the address transceiver: released = driven 1 (the host's idle, as before); HIZ releases them so a test that sets ADDRDIR = 0 and ADDROEn = 0 reads the port side.
- Changed against the host's pads: C2N_MOTOR / READ / WRITE are open drain, low only (D4; the host copy drove READ / WRITE push-pull when its layer 2 enabled them, the diag layer 2 never did);
  PADDLE_DIS / PADDLE_ADJ are the bank's pins 67 / 68 (the old PADCTL).

#### DDR2 ring (P3, step 4)

Range: **bytes 0x06000000 - 0x067FFFFF** (8 MiB, MIG word address 0x3000000 + 8 per line). Who can write DDR2 in the diag build, from the code (not from a comment):
- client 0, the RISC-V (and the MAC / USB DMA behind it): `cpu_mem_bridge` takes `address[25:0]`, a BYTE address (`host/ddr2_port.v`), so it reaches bytes 0x00000000-0x03FFFFFF only.
- client 1 (`ddr_addr`, mono/top.v): REU (`reu_cmd_valid` = 0, layer2_diag.v), the sampler (`smp_f_tgl` = 0) and the JTAG debug window (`jtag_ddr_addr = {ddr_window, ...}`): the window
  writes only when a person uses it (tools/trxdbg.py); nobody may write 0x06000000+ with it while a capture is read back.
- client 2, the drive: `drv_we` = `drv_tgl` = 0 in layer2_diag.v. cart_fill's client c_: `c_en` = 0. The engine client e_: only `diag_cap` (all other e_ assigns removed).
- the video client v_ (pix_layer): the still capture, enabled by default (`r_still_en`), writes base + slot x 0x10000 + 16 B x widx, widx <= 3374: with the default STILL_BASE 0x05050000 that is
  0x05050000-0x0506D2B0 (plus the second slot), 15 MiB below the ring. A register write moves STILL_BASE; the diag app does not (it cannot address 0x06000000+ with a pointer either).
What the rest of the DDR2 is doing in this build: 0x00000000-0x03FFFFFF the RISC-V application (firmware, heap, the REU window 0x01000000-0x01FFFFFF, a cartridge ROM image at 0x3C00000);
0x05040000-0x0506D2B0 the still capture (two slots); nothing in the diag build writes 0x04000000-0x0503FFFF, 0x0506D2B1-0x05FFFFFF or 0x06800000-0x07FFFFFF (no claim is made on them: the
T20 frame-ring area, 0x04000000-0x07FFFFFF, belongs to the TRX64 core, and the diag core has no trx_engine). Only the 8 MiB above is taken.
Read-back: the RISC-V cannot address the ring, so the application reads it through CAP_RADDR / CAP_RCTRL / CAP_RDATA (16 bytes a read, e_ client reads).

#### Benches (builder, `trx64-sim-env:5`, `docker run --rm --cpus 2`)

Run on the builder (scratch copy, `docker run --rm --cpus 2 -v $PWD:/w -w /w trx64-sim-env:5 sh <script>`, build outputs removed in the container, scratch removed; 2026-11-25):

| scenario | script | PASS line |
|---|---|---|
| S1 every pin to its snapshot bit (121 pins, SAMPLE words and LIVE registers, two pins low together, SAMPLE timestamps) | `cores/diag/sim/probe/run.sh` | `PASS tb_probe errors=0 checks=3463` |
| S2 DRIVE in every mode on every pin (refusals by class / index / guard, DRVEN, REF_PC / REF_CNT), guard trip, release at END / ABORT / step error / decouple / reset, WRITE_REG, EDGES, TIME_TO_EDGE, LOOP, FIFO overflow | same bench | (the same `PASS tb_probe` line: one bench, one verdict) |
| S3 the ROM through registers 0A0 / 0C0-0DF = pins.toml (`tools/diag_pins_gen.py --dump`) | same script | `PASS S3 the pin-table ROM read through registers 0A0 / 0C0-0DF equals pins.toml (121 pins)` then `PASS probe (engine v2, pin bank, capture and ROM bench pass)` |
| S3 lint: table = top.v ports, generated files current | `cores/diag/sim/pins/run.sh` (and `python3 tools/diag_pins_gen.py --check`) | `ok   diag pins: 121 pins, the generated files are current, the table equals the connector ports of cores/diag/mono/top.v` |
| S4 capture 4 pins x 4096 samples every clock into the DDR2 model, sample for sample against the TB's own pin history; register reader lines 0-5; partial line, second capture, refusals, ABORT, END, slow-DDR2 overrun | same bench as S1 | inside `PASS tb_probe errors=0 checks=3463` (checks "S4: 4 pins x 4096 samples, sample for sample, against the TB's own history", "S4: a slow DDR2 gives an overrun") |
| pads (joystick 2072 checks, cartridge A / D / R/W 192 checks) and the wiring of the top | `cores/diag/sim/pins/run.sh` | `PASS tb_pads errors=0 checks=2072`, `PASS tb_slotpads errors=0 checks=192`, `PASS pins (released = old input, drive on one pad only, host idle kept, pin table = top ports, wiring checked)` |
| T201 / T259 blocks unchanged (the diag_regs port change reaches them) | `avregs`, `avmap`, `avenc` run scripts | `PASS avregs (every knob register changes the words as specified, live; marker freeze; the csync of each sync knob)`, `PASS avmap (names and scan-group maps, channel probe words per bar, RGB words per permutation and bit order, SYNCn / FB per knob)`, `PASS avenc (the diag encoder = the C128's, PAL / NTSC / PAL-60 / NTSC-50)` |

The bench found and fixed while building: the T175 expectation "DCAPS 0x07" of tb_avregs (now 0x1F); the DDR2 model answers like mig_arb (a command is taken in the clock e_en and e_rdy are
both high), and the capture writer sends a pending next buffer in the very next clock - with a one-command-per-three-clocks turnaround the four line buffers overran at K = 1.
What the bench does not show: the DDR2 bandwidth of the real board under video load (the model takes a command every clock). K = 1 (400 MB/s of ring writes) needs the board; CAP_STATUS b2
and CAP_LOST say when a run was too fast. The sustained rate that is safe is a board measurement (open).

CI entries this half needs and did not touch (`ci/` belongs to the app half's worktree): 
- `ci/lint.sh`, next to the other generated-file checks: `gcheck core "diag pin table (T260)" python3 tools/diag_pins_gen.py --check` (the same check also runs inside bench `diag_pins`).
- `ci/sims.deps` (the bench cache keys; without these a changed pin table would hit a stale result): `diag_probe`: `cores/diag/rtl/probe.v cores/diag/rtl/diag_regs.v cores/diag/rtl/diag_cap.v cores/diag/rtl/diag_pinrom.v cores/diag/rtl/diag_pintab.vh cores/diag/rtl/pins.toml tools/diag_pins_gen.py cores/diag/sim/tb_probe.v cores/diag/sim/probe/run.sh`;
  `diag_pins`: add `cores/diag/rtl/diag_pintab.vh cores/diag/rtl/diag_pinrom.v cores/diag/rtl/pins.toml cores/diag/mono/diag_dgin.vh tools/diag_pins_gen.py`; `diag_avregs`, `diag_avmap`: add `cores/diag/rtl/diag_pinrom.v`.
- `diag_co` (tb_co.v, the app compiler against the RTL) is NOT ported: it drives the T175 step words and the T175 ports (`.drv_low`, `.pins_async({cmp, pin})`, 14 bit live) and fails to compile against engine v2.
  It needs the app's compiler v2 first (the step encoding above); then tb_co.v takes the v2 ports (as tb_probe.v wires them) and its models the pin names.

#### Area and timing estimate (S11, no Vivado here)

The CI bitstream report must confirm: LUT / FF of `probe`, `cap`, `regs` (utilization_hier), RAMB, WNS of the cpu_clk domain. Estimate from the code:
- probe: pin synchroniser 2 x 121 FF, engine state, 32 bit counters (t, ts, cnt, elapsed, tmo, edges), snapshot 128 FF: about 650 FF, 450 LUT (69 drive decodes, the 128:1 pin select, the
  class function), 2 RAMB18 (step RAM, FIFO) = 1 BRAM tile.
- cap: the lane muxes dominate: 32 lanes x 8 chunks of 16:1 = 256 muxes of about 4.5 LUT = ~1,150 LUT + 64, 288 FF in the mux stages, 512 FF in the four line buffers, ~250 FF of
  counters and the reader's 128 FF line: about 1,050 FF, 1,500 LUT. The parameter LANES trims it (16 lanes: about -600 LUT, -150 FF).
- regs: lane selects 224 FF, the byte muxes of LIVE / DRVEN / RDATA, the pin ROM (4096 x 8 = 1 RAMB36): about 300 FF, 250 LUT, 1 BRAM tile.
- pads / top: 69 override muxes and 24 IOBUF of diag_slot_pads: about 150 LUT.
Together about 2,350 LUT, 2,000 FF, 2 BRAM tiles (T175's estimate was 700 LUT, 400 FF, 1 tile). Timing risks to look at in the report: the 128 snapshot flops fan out to 32
lane muxes (stage A, 16:1 per chunk, one register after it) and to the 128:1 pin select; the ui_clk <-> cpu_clk crossings are toggles with 2 FF (the host's clock groups make them asynchronous).

### App half (branch `t260-app`, 2026-11-25)

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
  from the A1200's round, **not run** (step 12 runs only in the matrix). Superseded on 2026-11-26 by T239.5 S8 step 12: the profile is
  `cores/diag/app/ue2emu.toml`, the round is generic (the menu button, since diag's desc.toml has no `[hotkeys]`; the large core's start time).

*Step 3, P6/S9.* `cores/diag/mono/core.toml` has no `install_dir`/`install_old`; `cores/diag/superseded` is deleted (its two sample files moved to `tests/coreapp/instfiles/old`
with five test files as the fixture of `test_bundle`'s record 8 case, which is generic: the A500's sub-app test uses record 8 too). `gen_bundle.py`: `diag.cor` has no record 8
(`test_cor_pack.py` checks the descriptors name no install folder; `python3 tools/test_cor_pack.py`: OK; `test bundle: PASS`, 102 checks). `ci/lane.sh` step 13 copies
`cores/diag/sd` to `SD/cores/diag/` inside the delivery (`deliver-diag/SD/cores/diag/{tests,presets}`), listed in its `SHA256SUMS`: copy that folder to `/SD/cores/diag/`.

*Step 4, P5/S7: the panel (second app session).* **Decision of the coordinator, 2026-11-25, under D5 (the default, not Alex's word):** a panel is a TOML file on the SD card (`/SD/cores/diag/panels/`), opened from the diag
app like a test (file picker over `/SD/cores/diag/`, or the task "Open a panel"; the `kind = "panel"` line tells it from a test), and shown through a new generic CoreDialogs method.
- `CoreDialogs::edit_list` (+ `text`), `AppDlg::edit_list` (`DK_EDIT`: rows of label and value, UP/DOWN/PAGE/HOME/END, LEFT/RIGHT/RETURN call back on the menu task, an action row answers its index, ESC -1), drawn by ui.cc
  with the value right-aligned, in the remote snapshot as kind `edit`; documented in api.md 9.8. One deviation from the brief: RETURN on an *action* row does not call back, it closes the list and answers the row, because the
  action (a file picker, a name to type) must run on the diag task - a dialog cannot be opened from the menu task's callback. `test appdlg: PASS` (new case, real keys).
- Panel format and code: `cores/diag/app/lib/panel.h/.cc` (parser, rows bound to registers by "block.property[.field]" of the TOML A or by offset, bits/mask/signed/min/max/step, value names, default, `system`/`video_format` for the
  Standard row, presets as text, Defaults, the list); names resolved by `pdnames.cc` from the driver table macros. The A/V page is `cores/diag/sd/panels/avout.toml` (24 knob rows in the T259 order, Load preset, Save preset, Defaults);
  gone: all A/V C of `portdiag.cc` (`av_put_raw`, 24 ops, `g_av_settings`, presets, playground), the `[[core_setting]]`/`[lists]` rows and the two tasks and the "Load A/V preset" action of `desc.toml` (the core has no settings page now);
  `pd.run` opens a panel when the file's kind is panel. The A/V *walks* (kind avtest) took their knob table from the panel file: `dt_av_knobs` (C) is deleted, `av_bind` in diagtest.cc checks that the first 24 rows have the keys the
  walks need; a walk with no panel file on the card is refused with that reason.
- Proof S7: `test_diag_avtrace.cc` loads the three shipped presets through the panel's own "Load preset" action and presses RIGHT / LEFT on each of the 24 rows through the ScriptDlg model of CoreDialogs; its register writes equal
  `golden/diag_av_trace.txt` (written by the old C page, unchanged): `test_diag_avtrace: 83 checks, 0 failed`, `test diag_avtrace: PASS`. Other units: `test_panel: 51 checks, 0 failed` (new: parser refusals, bits/signed/two-byte rows,
  steps, presets round trip, Defaults, the `system` hook, the list with Save preset), `test_portdiag: 60 checks`, `test_diagtest: 612 checks`, `test_diag_desc: 7 checks`, all PASS.
- Found on the way: the control `diagtest_halffile` was vacuous since the move (its mutated source, `cores/diag/app/lib/diagtest.cc`, was not copied into the mutation directory); `run.sh` copies `$DIAGLIB` now: `control diagtest_halffile: fails as it must (1 check(s) red)`.
- Leftovers of step 1: `cores.h` (declaration and D24 ledger line), `svc_board.h` comment, `coreapp.allow` line, the render bench (`render.cc`: descriptor and the two text functions by prototype, own_cores.h carries the diag app; the A/V settings pages are gone, the
  runner walk against the engine model is out of that bench - it is a unit - and a "panel" dialog picture is in; `-fsyntax-only` clean, **not run**: LVGL objects). `menu_portdiag.txt`: the A1200's app has no such golden case either
  (`test_desc` skips it under `TEST_APP_A1200`), so there is none to restore. `tests/ui/fixtures.py` copies `cores/diag/sd` to the diag card. No ue2emu picture of the panel: it needs the RISC-V image (`tools/dev.sh riscv`, not allowed here).

*Step 5, runner v2 (second app session): the part that is built.* `cores/diag/app/lib/dtv2.h/.cc`: the pin table read from the ROM through PIN_IDX / PIN_ROM (`dtv2_pins_read`, `dtv2_pin_find`), the v2 step words as
inline encoders (`dv2_drive`, `dv2_edges`, `dv2_tte`, `dv2_write_reg`, `dv2_capture`, ...), the ring reader (`dtv2_cap_read`: CAP_RADDR, CAP_RCTRL, poll CAP_STATUS b4, 16 bytes of CAP_RDATA, four samples a line;
`dtv2_cap_lines`: a following capture starts on the next line). Unit `dtv2` on a model of the register block that serves the 121-pin ROM from `tests/coreapp/golden/diag_pins.toml` (a copy of `cores/diag/rtl/pins.toml`
of t260-gw - the lint of the merged tree must find the two equal) and a synthetic ring: `test_dtv2: 399 checks, 0 failed`, `test dtv2: PASS` - S3 (the app reads every pin by name, 121 of them, a bitstream without a table is
refused) and the S4 reader (4096 samples of a known pattern, a second capture that starts on the next line, a reader that never answers is given up on). The offsets in dtv2.h are numbers from the gateware's register map;
the hardware semantics assumed there and not yet checked on a bench or the board: a write of CAP_RCTRL clears CAP_STATUS b4 until the new line is in CAP_RDATA.

*Step 5, the v2 switch (third app session, 2026-11-25).* Everything below is built and proven by a unit on the native model unless it says open.
- **TOML A v2** (`cores/diag/app/gateware.toml`): 14 blocks, 51 properties (ident with DCAPS bits bank / capture, engine with refused / sampling / guard_trip, ref_pc, ref_cnt and cap_reset, live = the 16 byte snapshot run `snap`,
  `drven`, `pinbank`, `pinsel`, `pinrom`, `capsel`, `capst`, `capcnt`, `capraddr`, `capctl`, `caprd`; CAP_LINES is a run of three bytes because the top wires its bit 31:20 to 0 and the checker counts a constant as no read). `tools/rtl_check.py` on the merged tree:
  `rtl_check: 14 block(s), 51 properties agree with the RTL of host`. `portdiag_regs.h` names every register by the driver table (PD_SNAP, PD_DRVEN, PD_PIN_*, PD_CAP_*, PD_REF_*), `dtv2.h` takes its offsets from it; PD_OP_* and the live-vector macros are gone.
- **Test language v2** (`diagtest.cc/.h`): `drive pins=[NAMES] level=low|high|hiz|release`, `wait`, `sample into=` (5 FIFO words), `edges pin= edge= us|clocks`, `time_to_edge pin= edge= timeout_us|timeout_clocks`, `write_reg reg="av.std"|0x50 value=`,
  `capture pins=[<=32] every_us|every_clocks samples= into=` (all captures of a test name the same pins; at most 4096 samples in all; not in a loop), `capwait`, `loop`, `wait_user`; `padctl` is gone. Names are kept in a pool and resolved by `dt_compile(t, pins, tpus256, ...)` against the
  table the runner reads from the ROM (`dtv2_pins_read`); an unknown name is an error with the step's line. Evals: `value = "slot.pins"` + `pins = [NAMES]` (bit j = the j-th name), `slot.ts`, `eq/min/max/differs` over it; `cmp`, `flags`, `mask` on pins are gone.
  The engine's refusals (STATUS b5, REF_PC, REF_CNT) and the guard trip (b7) go to the result files with the step and the pin name (`[refused]` in the .toml, a "Refused:" line in the .txt); the status stays `done` and the evals judge. After END the runner waits for the
  capture writer, reads the ring with `dtv2_cap_read` and writes `[slot.NAME] kind = "capture"` with the lanes and the samples. The readable report shows a sample as `NAME=level` for the pins its evals look at (else the 128 bit snapshot).
  The live page reads `diag_pins()` (the table, once) and shows KBJOY_1/2_JOY0-4 and PADDLE_CMP0-3 by name. The shipped tests are converted by script (pins, stick2, paddle, cd32, cd32keys; joystick is a suite, the av and xpad files have their own runners).
- **S6 translation rule** (golden `tests/coreapp/golden/diag_old_words.txt` = the words of the T175 compiler of b6a2a2212, recorded before the change; `test_diagtest` translates them and compares with this compiler's words for the converted file):
  DRIVE mask level -> one DRIVE per pin of the mask in bit order (mode LOW for level bit 0, RELEASE for 1; pin index 50 + bit); MASK m + EDGES n -> EDGES any on pin 50 + bit, then the word n; TIME_TO_FLIP sel n -> TIME_TO_EDGE any on PADDLE_CMP (63 + the bit of sel), then the word n;
  PADCTL a -> DRIVE PADDLE_DIS (67), DRIVE PADDLE_ADJ (68), each HIGH if its level bit is set, else LOW if its enable bit is set, else RELEASE; LOOP K C -> LOOP with K recounted in words of the translated body; WAIT, SAMPLE, WAIT_USER, END unchanged.
- **Model**: `tests/coreapp/diag_engine.h` speaks v2 (pin table from `cores/diag/rtl/pins.toml`, classes and refusals as the gateware spec has them, snapshot, DRVEN, ROM, WRITE_REG, EDGES, time_to_edge, capture ring and reader). `test_dtv2` reads `cores/diag/rtl/pins.toml` itself; the golden copy is deleted.
- **CI entries made**: `ci/lint.sh` `gcheck core "diag pin table (T260)" python3 tools/diag_pins_gen.py --check`; `ci/sims.deps` keys of `diag_probe`, `diag_pins`, `diag_avregs`, `diag_avmap` as in the gateware notes, and `diag_co` names the app lib, TOML A, model and fakes.
- **Units** (g++-16, one at a time, `CA_UNITS=<unit> tests/coreapp/run.sh`): `test_diagtest: 620 checks, 0 failed` (S5, S6, the files byte for byte, refusals, the card), `test_dtv2: 399 checks, 0 failed`, `test_portdiag: 60 checks, 0 failed`, `test_diag_avtrace: 83 checks, 0 failed`,
  `test_panel: 51 checks, 0 failed`, `test_diag_desc: 7 checks, 0 failed`, `test_hotkeys: 1000 checks, 0 failed`; `memops` and `bundle` PASS; `check_d24.py`, `check_api_doc.py` (539 symbols), `test_cor_pack.py`, `check_todo.py` and `diag_pins_gen.py --check` green.
- **Open points of that session (all three closed in the next block):** (1) cd32 under the 5 word SAMPLE and the 512 word FIFO, (2) the bench `diag_co` not ported, (3) `test desc` aborting. (3) also set `n205 >= 16` to 15 (the cores left in the shared table since diag moved out); `test_appdlg: 161 checks, 0 failed`.

*Step 5, the open points closed (fourth app session, 2026-11-25).*
- **cd32 (decision of the coordinator, 2026-11-25, not Alex's word):** the test stays one file, unchanged in content. The runner pops the result FIFO while the engine waits at a `wait_user` and polls it while the engine runs
  (`drain_fifo` in `cores/diag/app/lib/diagtest.cc`: level read high byte first - the level only grows while the engine runs, so a read across a carry is too small, never too big; a pop works in every engine state, `probe.v`
  `fifo_rd` is not gated by `running` or `waiting`). The 512 words of the hardware FIFO bind **per segment** (the steps between two `wait_user`); `dt_plan` refuses a test whose segment alone pushes more, naming the segment
  (`segment 2 (after the wait_user of line 31) pushes more than the 512 words ...`), and a run of more than `DT_WORDS_MAX` = 1024 words in all (the result buffers). STATUS b3 stays the run-time guard: an overflow ends the test
  with "the result FIFO overflowed" in the result file. `test_cd32_v2` now proves cd32 (700 words in three segments) runs to `status = "done"` with its three prompts, and a one-segment overflow is refused with the engine not started:
  `test_diagtest: 628 checks, 0 failed`, `test diagtest: PASS`.
- **`test desc` abort, cause:** `tests/coreapp/run.sh` ran the unit `desc` (which opens `$T/bundles/desc_own.bin` through `RecordMenu`) before the only `gen_bundle.py` call; step 3 (`bdb0f350b`) had removed the early call that
  existed for the diag.cor export, so the file was missing and `RecordMenu` aborted (`record_menu.h:39`) after the last table core. Not a T260 gateware or table problem: t239.3's tree has the early call and passes. Fixed by the early
  `gen_bundle.py` call before the `desc` line: `test_desc: 1954 checks, 0 failed`.

- **Bench `diag_co` ported to engine v2.** `cores/diag/sim/tb_co.v` wires `diag_regs.v` and `probe.v` as `tb_probe.v` does (pin model of the class table, PADDLE_DIS = pin 67 model: the comparator pins PADDLE_CMP0-3 (63-66) flip K clocks after DIS stopped being driven high; the ten joystick pins 50-59 come from the model hex, `models/<id>.hex` unchanged), without `diag_cap.v` (the four tests capture nothing; with it the bench is six times slower: 260 s against 43 s for paddle; `tb_probe.v` covers the capture). It drives the registers as the app does (ABORT, CLEAR, CAP RESET, STEP_ADDR 0, words, RUN, STATUS; at a wait PC, pop the FIFO, GO; at done pop the rest, REF_PC / REF_CNT) and writes STATUS, the FIFO words, the PCs, the words popped up to each wait, REF_PC, REF_CNT. `tests/coreapp/diag_co.cc` runs the app's runner on the model engine of `diag_engine.h` (ROM, ID, TICKS, pin table from `cores/diag/rtl/pins.toml`) in two modes: capture records the step words the runner writes, replay serves STATUS / PC / FIFO from the RTL's record. `co/run.sh` generates the driver table (`tests/coreapp/diag_drv.sh`), builds the app from `cores/diag/app/lib` and runs the four tests side by side, each with its own log. The `.expect` files are rewritten for the v2 result format (levels by pin name, `ts`, eval verdicts). Findings: the engine's `TIME_TO_EDGE` reads K - 2 against the model's K (paddle: 4998 ... 349998, all eight), the timestamps of the first samples are 5007 (pins) and 500003 (cd32); cd32 runs 57.4 million clocks, about 12 minutes of iverilog on the builder (`ci/sims.txt` timeout 2400).
- **Bench result** (builder, `docker run --rm --cpus 4 trx64-sim-env:5 sh cores/diag/sim/co/run.sh`, scratch copy, removed afterwards): `check paddle.expect: 13 lines, 0 wrong`, `check pins.expect: 23 lines, 0 wrong`, `check stick2.expect: 14 lines, 0 wrong`, `check cd32.expect: 22 lines, 0 wrong` (700 FIFO words in three waits, 0 refused), `PASS co (the application's compile and decode meet engine v2 on all four example tests)`.
- Step 4 (g++-16, one at a time): `test_diagtest: 628 checks, 0 failed`, `test_portdiag: 60`, `test_diag_avtrace: 83`, `test_dtv2: 399`, `test_panel: 51`, `test_diag_desc: 7`, `test_desc: 1954`, `test_memops: 159`, `test_bundle: 102`, `test_appdlg: 161` all 0 failed, `test_hotkeys` PASS; `control diagtest_halffile: fails as it must (1 check(s) red)`; `check_d24`, `test_cor_pack`, `check_api_doc` (539 symbols), `diag_pins_gen --check` (121 pins), `check_todo` green; `rtl_check tests: PASS`. The render bench was not rebuilt (LVGL does not link here; `render.cc` names neither `dt_plan` nor the FIFO size).

*Not built, handed over (S5, S6, the rest of S4 and the v2 switch of the app).* The app still runs the v1 engine: `portdiag_regs.h` (PD_OP_*, PD_LIVE, the 10-pin mask), `gateware.toml` (live block 0x40-0x43), the old compiler and
evaluator of `diagtest.cc`, `diag_engine.h` (the model), `portdiag.cc` (the live page: ports and comparators from LIVE). Against a v2 bitstream they do not work. What is to be done, in this order, one commit each:
1. `gateware.toml` v2 and `portdiag_regs.h` v2 (registers named through the driver table; the engine block, `live` = 16 byte snapshot, the pin and capture registers; PD_OP_* replaced by dtv2.h's words) and the
   `rtl_check` line: the case reads `cores/diag/rtl/diag_regs.v`, which is v1 in this branch - run it in a scratch worktree of t260-gw with this branch's `gateware.toml` (`tools/rtl_check.py`, case `diag` of
   `tests/rtl_check`). The live page (`pd.port1`, `pd.port2`, `pd.cmp`) reads pins 50-54 / 55-59 / 63-66 of the snapshot by name from the table (not by number).
2. Test language v2 (all pins by NAME, resolved against the table at compile time - `dt_compile` takes `const DtPins &`): `drive pins=[NAME..] level=low|high|hiz|release` (one DRIVE word per pin), `wait`, `sample into=`
   (5 FIFO words: t and the snapshot), `edges pin= edge=any|rising|falling us=|clocks=`, `time_to_edge pin= edge= timeout_us=|timeout_clocks=` (the old `time_to_flip cmp=[n]` is `time_to_edge pin="PADDLE_CMPn"`),
   `write_reg reg="av.std"|0x50 value=` (names through `portdiag_reg_by_name`, only 0x050-0x06F), `capture pins=[<= 32 names] every_us=|every_clocks= samples= into=` (CAP RESET before RUN; the runner reads the lines back
   with `dtv2_cap_read`, the first line of each capture is the sum of `dtv2_cap_lines` of the earlier ones; at most 4096 samples a test, said at compile time), `capwait`; `padctl` goes (PADDLE_DIS / PADDLE_ADJ are pins
   67 / 68, class pz: HIGH / LOW enable, RELEASE = high-Z). Evals: `value = "slot.pins"` with `pins = ["NAME", ..]` in place of `mask` (bit j of the value = the j-th name), `eq`/`min`/`max`/`differs` over that value;
   `slot.ts` the 32 bit t; the fields `cmp` and `flags` go. LOOP counts WORDS (a drive of n pins is n words, EDGES / TTE / CAPTURE two). A refused EDGES / TTE still pushes 0xFFFFFFFF (gateware §5). `STATUS` b5 / REF_PC / REF_CNT
   are the "refused" status of a run (REF_PC of a two word op is its second word).
3. S5: a unit test writes a TOML at run time (no shipped file) that names pins only by name, loads, compiles (the words name the table indexes - compare with `dv2_*` of `dtv2_pin_find`), runs on the model, and the result
   files have its values and verdicts. S6: convert the files of `cores/diag/sd/tests` (pins, stick2, paddle, cd32, cd32keys, joystick, xpad*, av* need no change: they are kinds suite / avtest / xpad) by a script, and prove that
   each compiles to the old compiler's words translated: take the old words from the unchanged HEAD `diagtest.cc` of `b6a2a2212` (golden `tests/coreapp/golden/diag_old_words.txt`, one line per file) and translate by rule in
   the test (old DRIVE mask -> one DRIVE per pin in bit order, MASK+EDGES -> EDGES, TIME_TO_FLIP sel -> TTE any, PADCTL -> DRIVE DIS, DRIVE ADJ (a part not named is RELEASE), LOOP K recounted in words), then compare.
4. `diag_engine.h` (the model) to v2 for the benches of the shipped tests (pins as a 128 bit state, open drain pins pulled high, DRIVE LOW pulls a pin low, the comparators of the paddle test), `render.cc` and
   `test_portdiag.cc` follow (they read LIVE at 0x40).

### Onto t239.3 (2026-11-26)

- Merged onto `t239.3` (T239.5 D15 pattern of the eleven moved cores) and pushed as 4266345b2. diag is an app of its own (`diagapp`, its
  descriptor out of `cores.h`, `BLUEPRINT_CORES` = apple2 snes); its eleven units (`diagtest`, `portdiag`, `diag_avtrace`, `panel`, `dtv2`,
  `diag_memops`, `diag_desc`, `diag_descgen`, `diag_hotkeys`, `diag_keywalk`, `diag_uicover`) are listed in its TOML B; the shared goldens
  lost only their "Port Diagnostics" rows; diag has its own render build (`RENDER_APP_DIAG`) whose pictures come from the real code
  (stick2.toml through `diag_run_file`, the panel from `sd/panels/avout.toml`).
- Found on the way (T239.3 D57): the eleven moved cores' TOML Bs and `tools/core_move.py` still named `base_sources = ["portdiag.cc"]`;
  removed, since portdiag.cc left the base.
- D54's `CoreDialogs` is base 3.7 (unreleased, `coredlg.h` a root of `tools/sdk_api.py`), SDK 0.5.3; it folds into base 4.0 when P2-6 lands.
- The record-8 installer's persistence controls are controls of the `bundle` unit now (`instfiles_overwrite`, `instfiles_superseded`).
- Run (fast tier, Mac): tests/coreapp 299 programs passed, both controls fail as they must; render shared 82,834 checks and diag 9,039
  checks, 0 failed; tests/loader, sdk_api, check_api_doc, check_d24, check_menuctl_gate, check_controls ok. Not run: RISC-V, the diag
  lane's gateware, ue2emu, the board.
- The diag SD folder is not in the package (§4); `tools/release_notes_mc.py` says it is copied from `cores/diag/sd` by FTP. Superseded
  by T239.5 D21 (02e28716d): the lane delivers `SD/cores/diag/`, every package and pre-release carries it as `SD.zip`.

*Fix 2026-11-27, the panel's keys (Rick, tester, and Alex: "each keypress the HDMI screen redraws and it's about a 2 second wait before I can change the value again"; Alex, translated: "I noticed it yesterday too").*
A regression of step 4: the A/V page was a settings page, whose rows are written in place since T104/T262; the panel is an edit list (`CoreDialogs::edit_list`, DK_EDIT), and `ui_dialog_render`
built the whole question card again on every revision, because `question_sig` held the window, the selection and every row's label and value. So every key, UP/DOWN as well as LEFT/RIGHT, deleted
and drew the card (a window of 14 rows, `DLG_ITEMS_MAX`) over the half-transparent veil in software. Fix (`coreapp/src/ui.cc`): for a choose or edit list the card's signature holds only its shape (kind, title, text, row count,
whether the "a-b of n" label is there, buttons); the card keeps its row objects and a key retexts them with `trx_row_retext` (the function the pages use) and the "a-b of n" label; a row that
cannot be changed in place builds the card again as before. DK_CHOOSE had the same cost and takes the same path. Found by the review: the overlay generation moved only at the next `trx_overlay()`, after `ui_dialog_render` had compared it, so after a frame rebuild in the
same pass (a new canvas, e.g. the panel's Standard row switching PAL/NTSC at 480p/576p) the dialog kept pointers to deleted objects; `ov_gone` (`app/view/trxview.cc`) moves it
now when the container is deleted. Proof: render bench (atari800 build, beside T167 S3) - RIGHT changes the pixels of one row, DOWN those of two rows, END moves the window,
the three keys served in place (`ui_dialog_inplace`); the card afterwards equals a card built afresh for the same state (0 pixels differ); a frame rebuild between two
keys builds the card again and writes no old row.
