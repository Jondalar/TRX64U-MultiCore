# T159 — case LED 1 as a floppy LED (D2; D1 was a slow blinker), switchable in the menu, tried in the A1200 first

**Status:** ACCEPTED (2026-10-29), asked by Alex; D2 (2026-10-30) replaced the blinker. Status check 2026-11-13: built and merged (ecc69ebfb, 34cff5f46); board 11-04 pass A500, Atari, Apple II, CPC, C128; C264 and VC-20 not seen. 2026-11-15 sync: the pointer 'T163 D3 rework' in D5 was wrong (T163 D3 is order/scope); the host lamp is T164 and is in `host/bling_io.v`. C264/VC-20 board still not recorded (maps built: `cores/c1541/drive_leds_map.v`).
**Repo:** TRX64U-MultiCore — the generic core application (`coreapp/src`): a setting that writes the case LED selector once; since D2 also the A1200's own
gateware that drives the source (no host change). D1 was application-only. The host already wires case LED 1 to `case_leds` (`host/top.v:1541-1549`), selected by
`U64_CASELED_SELECT` (`host-app/software/system/u64.h:80`; the core app writes it already, `coreapp/src/u64cfg.cc:568`).
**Number:** T159.
**Origin:** Alex, 2026-10-29 (translated): "use it as a 'loading' indicator, so when data flows it blinks slowly"; on the two ways
(software toggling vs. a new gateware source): "a"; "can that go into the A1200 as a test of how it feels? Including the parameter
in the menu: LED Load Indicator ON/OFF, LED Color (if that works)".
**Depends on:** T104 (generic app).
**Milestone:** M3.

---

## §1 The problem

- **Today:** nobody writes the selector in the host app or the generic core app (except the C64 settings in `u64cfg.cc`), so the case
  LEDs show the bring-up meanings (`host/top.v:32-34`: LED 1 = core out of reset, LED 2 = HDMI locked). The hardware source "any
  activity" flickers per access (50 ms stretch, `host/case_leds.v:9`), it does not blink slowly.
- **Wanted:** while the application moves data for the machine (loading a ROM / Kickstart, serving floppy tracks, hard disk blocks,
  writing back), LED 1 blinks slowly; otherwise it is steady on.
- **Parts:** **P1** a setting "LED Load Indicator" On / Off (default On, kept in the core's `.cfg`); **P2** the blinker: the app
  marks "data moved" in its media paths; while the last mark is younger than 500 ms, LED 1 toggles every 250 ms (2 Hz) between
  selector 0 (On) and 1 (Off); afterwards steady On; LED 2 keeps what it shows; Off = the selector is not touched (as today);
  **P3** first in the A1200 (its descriptor opts in), the other cores later by Alex's decision.
- **Colour:** not possible on case LED 1: one pin, one colour. Coloured light is the case LED strip and the keyboard lighting
  (WS2812/APA102, `LED_DATA`, `LED_CLOCK`, `BLING_LED`), which the host does not drive (OPT-bling). Out, see §4.

## §2 Scenarios

```gherkin
Scenario: S1 On selects the drive source once (rewritten 2026-10-30, D2; D1's blinking S1 is replaced)
  Given the A1200 with "LED Load Indicator" = On
  When  the setting is applied and the machine then runs and moves data for seconds
  Then  U64_CASELED_SELECT is written exactly once with 0x04 (LED 1 = source 4 drive A activity, LED 2 = On) and never again
  # tests/coreapp test_ledload.cc; control: the selector not written -> FAIL (also: wrong source, written on every apply)

Scenario: S2 Off leaves the LED alone
  Given "LED Load Indicator" = Off (from the start)
  Then  the app never writes U64_CASELED_SELECT; switching Off after On writes 0x00 once (LED 1 and 2 On, as the D1 restore did)
  # same test; control: Off ignored -> FAIL, restore removed -> FAIL

Scenario: S3 board
  Given the A1200 .cor, an ADF game loading
  Then  case LED 1 is on while the drive motor runs (Kickstart's motor, as a real Amiga floppy LED), flashes visibly on hard disk
        accesses and is dark otherwise (Alex looks)

Scenario: S4 gateware: LED 1 follows motor and hard disk
  Given trx_motor_led (rtl/minimig.v) with the CIA-B outputs /SEL0..3 and /MTR, the IDE activity and the drives present
  Then  the LED is on from the falling edge of /SELx that took /MTR on until one that took it off, for a present drive only; on while
        hdd is set and for 50 ms after; cleared by reset; the disk DMA (fdd_led) is no input
  # cores/amiga1200/sim/motorled (iverilog, lane large:amiga1200); controls: LED tied off, absent drives unmasked, hold removed -> FAIL

Scenario: S5 lamp colours (D4; replaces D3's green / yellow / red)
  Given bling_io (A1200 fork) with lamp_en = 1
  Then  LED 71 is red, steady, while the core runs; SHIFT LOCK locked = yellow, steady, over any activity; floppy activity (drive_act) flickers the red off
        (~9 Hz, 50 %); hard disk activity (hd_act) flickers blue/off, and wins over the floppy; each activity is held for one full flicker period
        (a 10-clock pulse still shows); lamp_en = 0: dark, amber with SHIFT LOCK as before; LEDs 1-70 dark in every state
  # cores/amiga1200/sim/bling (iverilog, WS2812 decode); controls: no floppy flicker, no HD flicker, HD not split, HD below floppy, no hold, enable
  # ignored, no SHIFT LOCK priority, top.v drive_act / hd_act tied off -> FAIL
  And   the hd part reaches bling_io: trx_motor_led.hd (hdd + 50 ms hold, never the motor) -> minimig/nanomig motor_hd -> amiga_core drv_hd -> layer2 a_drv_hd
        -> drive_leds_n[1] -> bling_io hd_act (sim/motorled: hd alone, control hd includes motor; sim/motorwire: drv_hd -> hd_act, control inverted)
  # S3 on the board: red idle, floppy load flickers the red off, HD access blue (Alex looks)

```

## §3 Decision

- **D1** Software only (Alex: "a"); setting "LED Load Indicator" On/Off in the A1200's menu; 2 Hz while data moved in the last
  500 ms. No colour item (one-colour LED).
- **D2 (2026-10-30, replaces the blinker of D1)** Alex, on "pure floppy reads do not blink, the gateware reads the tracks from DDR2
  itself" (translated): "That is not what I wanted. When the gateware reads, it should be tapped there. The LED should behave like a
  floppy LED." LED 1 follows the machine's own drive activity from the gateware (minimig `fdd_led` / `_motor`, `hdd_led`), not an
  application blinker. **Signal (Alex: "let's do a"):** LED 1 is on while the drive motor runs, as on a real Amiga drive, plus hard disk
  accesses (`hdd_led`); not the disk DMA (`fdd_led`). Trial in the A1200 first (its own files), the other cores after the board test.


- **Board, D3 (2026-10-30, A1200 `.cor` of run 253, Alex):** On = green at once, already in the menu ("clean!"); loading turns it yellow but
  hardly visibly; SHIFT LOCK red: ok.
- **D4 (Alex, 2026-10-30, after the board test of D3, translated):** "loading makes it yellow, yes, but you can hardly see it." Then:
  "Power on = red, as on a real C64 (matches the case); Caps Lock = yellow; and loading then 'off' ... so just flicker? For all I care we do a
  'blue' flicker when the HD loads." Replaces D3's colours: with the setting On, LED 71 is red while the core runs; SHIFT LOCK locked = yellow
  (wins over activity, as D3's red did); a floppy drive's activity flickers the red off; hard disk activity flickers blue. Off as before.
- **D5 (Alex, 2026-10-30, translated):** "And that then for all cores as far as it fits ... except TRX64 and Gideon/Commodore 1.1.0."
  The large cores (own `top.v`: A1200, C128, ZX Next) get it in their fork; the cores routed against the host need the lamp logic in the
  host's `bling_io.v` (host frozen, `contract/host_pin.txt`), so they get it with the next host package (T164 lamp, built: `host/bling_io.v` `hd_act`/`drive_act`; not T163), each core feeding
  its floppy / hard disk activity through the contract.


**D-all (Alex, 2026-11-01, at the board with t164-774497b1, the A500 as a large core had no case LED):** "Yes, I mean the case LED as floppy
indicator. After the success on the A1200 that should go into all cores..." (translated). Every core with a drive drives `drive_leds_n[0]` from
its drive activity (motor / access): amiga (A500), atari800 (SIO disk), apple2 (Disk II), cpc (FDC), and vc20 / zx if they have a drive; c128,
c264 and c64ref already do. Cores without a drive keep it off.

## §4 Out

- LED colour, the case LED strip and keyboard lighting (needs a WS2812/APA102 driver in the host gateware: own spec if wanted).
- Other cores until the A1200 trial is judged on the board.

## §5 Decisions taken while building

- **Setting:** `LED Load Indicator` (On / Off, default On, `WHEN_LIVE`) is applied by the base operation `led.load` (`coreapp/src/engines.cc`),
  so any descriptor can opt in with one `Setting` line; only `amiga1200` does (`coreapp/cores/amiga.cc`, `AmigaVariant::led_load`). The
  A500 has no such item (its menu golden is unchanged). The value lives in the core's `.cfg` like every setting.
- **Blinker:** `coreapp/src/ledload.cc`, state `Engines::led`. `ledload_mark()` is called where the base moves data for the machine:
  the Kickstart put (`romset_put`, `romset.cc`), an image going into a floppy window (`win_mount`, once per chunk, `winslot.cc`), an entry
  of the floppy write log popped (`win_service`) and a track written back (`flush_drive`), a hard disk sector served (`serve_one`,
  `sectors.cc`). `ledload_poll()` runs in `engines_poll` (the A1200's 5 ms service): while `now - mark < 500 ms` it alternates the
  selector every 250 ms, the first step is Off; after that it writes On once and never again. Time is `io->now_ms()`.
- **Reads are not marked:** the floppy images sit in the core's DDR2 window and the gateware reads tracks from it on its own, the application
  does not see a track read. So LED 1 blinks while a disk is mounted, while Kickstart goes in, while the machine writes a disk and while
  the hard disk is served - not during pure floppy reads. Measured on the board in S3 (open).
- **LED 2:** once the selector is written, `sel_set` latches and the high nibble takes effect, so it is written with every byte. The
  bring-up meaning of LED 2 is "transmitters running and the picture locked" (`host/top.v`), which no selector source is; with the
  core running it is on, so the nibble is 0 (On). Selector bytes written: `0x00` (LED 1 On) and `0x01` (LED 1 Off).
- **Off:** the selector is never written. Switching Off while the blinker left LED 1 dark writes `0x00` once (a dark LED must not stay);
  nothing else.
- **Contract:** `CoreIo::case_led_select` (default: ignore), board implementation in `hw.cc` writes `U64_CASELED_SELECT`. The
  `ops_trx64u.txt` golden gains the line for `led.load` (base operation, every core application has it).
- **Tests:** `tests/coreapp/test_ledload.cc` (S1, S2, restore on Off, A500 untouched, the five marks) with the negative controls in `run.sh`.

### D2 (2026-10-30), replaces the blinker above
- **Replaced:** the "Blinker", "Reads are not marked", "LED 2" (selector bytes 0x00/0x01), the first sentence of "Off" and the mark tests of D1 above.
  Removed from the code: `ledload_mark`, `ledload_poll`, the five `ledload_mark` calls (`romset.cc`, `winslot.cc`, `sectors.cc`) and the poll in
  `engines_poll`. The setting keeps its name and its `.cfg` key; the operation `led.load` is unchanged (`ops_trx64u.txt` golden untouched).
- **Application:** `ledload_set(on)` (`coreapp/src/ledload.cc`): On writes `U64_CASELED_SELECT = 0x04` once (LED 1 = source 4 "drive A activity",
  LED 2 nibble 0 = On, same reason as in D1); Off writes nothing, except `0x00` once when On had written 0x04 (a latched selector cannot be
  unset). No timing, no marks.
- **Signal path (A1200 only, no shared file touched):** `trx_motor_led` (end of `cores/amiga1200/rtl/minimig.v`, instance `MOTLED1` there) ->
  `minimig.motor_led` -> `nanomig.motor_led` -> `amiga_core.drv_led` (`rtl/amiga_core.v`, sys_clk) -> in the flat build `mono/build_mono.tcl`
  edits the copy of `cores/amiga/layer2_amiga.v` (three exact-once regsubs: the `.drv_led(a_drv_led)` port, the wire, and
  `assign drive_leds_n = {2'b11, ~a_drv_led}` in place of the shared 3'b111) -> `l2_drive_leds_n[0]` -> `top.v:1559` (`cled_s1`, synchronised
  into cpu_clk) -> `case_leds` `a_act` (source 4, `top.v:1565`). The forks' `.diff` files are regenerated (`tools/check_wrapper_fork.sh` ok).
  The DFX variant of the A1200 (`core_sources.tcl`, the shared `layer2_amiga.v` unchanged) leaves `drv_led` unconnected: no LED there.
- **Choice:** ANY present drive's motor, not DF0 only (the A1200 has up to four mounted DFx; a game on DF1 must light it). "Present" = the
  drives latched during reset from `floppy_config[3:2]` (as `paula_floppy.v` `drives`), so the Kickstart drive-ID probing of an absent drive
  (it latches `motor_on` for a moment) does not light the LED. The latch is a copy of `paula_floppy.v` 638-672 (falling edge of /SELx takes
  /MTR, reset clears), stepped on `ce28 & clk7_en`, because that file's `motor_on` is not an output and `paula.v` is shared.
- **Hard disk:** `hdd_led` is the IDE `drq` (gayle.v `led`), microseconds per sector, and `case_leds` stretches only its "any activity" source,
  not `a_act`; so `trx_motor_led` holds it for `HDD_HOLD` = 354000 ticks of `ce28 & clk7_en` (7.09 MHz) = 50 ms. `fdd_led` (DMA) is not wired.
- **Tests:** `tests/coreapp/test_ledload.cc` (S1, S2, restore, A500 untouched) with controls `led_not_written`, `led_wrong_source`,
  `led_off_ignored`, `led_off_restore`, `led_written_twice`; `cores/amiga1200/sim/motorled` (S4, in `ci/sims.txt`, `sims.deps`, `sims.lane`).
  The bench cuts the module out of the fork with sed, so it tests the shipped source; it does not elaborate the chipset (the wiring from
  minimig to the pin is read, not simulated; S3 on the board proves it).
- **2026-10-30, board: the C64U's power LED stays dark (Alex, photo).** Alex (translated): "The LED top right does nothing. Are we
  using the right one? I meant the power LED that the bling board controls, i.e. over the keyboard connector." and "It can do red and
  yellow." The colour statement of §1 ("one pin, one colour") is wrong for this LED: two lines, two colours. Then: "In the TRX64
  Ultimate core we explicitly did not connect the LEDs. They were measured, I think. You would have to find out which one it is.
  Please do not throw the code away, because there are C64U users and UE2 users, and those have the LED on the pin header (the one
  built in now). Best we detect the board in the app, or simply drive both." Decision: keep `case_leds` (U64E2 header LEDs), add the
  C64U power LED; the app detects the board or drives both. Which line drives the C64U power LED: open (LED_CASE3n / G17 is held off in
  both host and TRX64-Ultimate tops; the bling board's WS2812 chain on BLING_LED has 71 LEDs).
- **2026-10-30, the C64U power LED is not on LED_CASE1n/2n (Alex: "it does not hang there").** Research (read only): no TX line from the
  FPGA to the keyboard board (`BLING_KEYB` J16 is receive only); the bling WS2812 chain (`BLING_LED`, 71 LEDs) has no power entry in
  Gideon's `bling_board.cc` (index 70 = SHIFT LOCK); G17 (`LED_CASE3n`) lit a light-blue LED low in the case when left undriven
  (TRX64-Ultimate 456f2d6) - not red/yellow; the ESP32 has two LED outputs, GPIO38 "LED_RED" switched by the supply voltage and GPIO18
  set off at start (`apps/ultimate/software/u64ctrl/main/control_main.c:39-41, 203-208`), no RPC sets them. Most likely: the power LED
  belongs to the ESP32, which the FPGA reaches only over its UART (N15/P15). Not proved.
- **2026-10-30, the line found by measurement: G17.** Boundary-scan SAMPLE (read only) of the running A1200 against the capture of
  Gideon's design (`1541ultimate-tools/pinmap/runs/sample_no_cart.json`): of the LED outputs only G17 (`LED_CASE3n`) and P16
  (`LED_BOARDn`, the LED on the FPGA module) differ, both high in Gideon's design and low in ours; H19/H17 are ruled out by Alex's test.
  G17 is the power LED (not yet seen lit; Alex is to look). Alex was in bed: "you would have to do it alone". Change in the A1200's own
  `mono/top.v` only: `LED_CASE3n = !led1n`, G17 follows case LED 1, so the one selector serves the U64E2 header (H19) and the C64U
  power LED (G17) - "simply drive both", no board detection needed. With the A1200's selector 0x04 the power LED is on while a drive
  motor runs or the hard disk is accessed. `host/top.v` still holds G17 low (host frozen): the host change follows with the next host.
  The red/yellow second colour is not found yet (no other LED line differs).
- **2026-10-30, measured on stock 1.1.0: the C64U power LED is LED 71 of the keyboard WS2812 chain (`BLING_LED`, R16).** Alex
  flashed 1.1.0 (our host, boot.cfg and /Flash backed up to the session scratchpad first). Boundary-scan watch (read only, 20 s):
  of the LED lines only R16 (`BLING_LED`), M15 (`LED_DATA`) and R14 (`LED_CLOCK`, the case strip) toggle; G17, H19, H17 do not. Gideon's
  "Power LED (if installed)" (Output 1/2 = `U64_CASELED_SELECT`) is the pin header (H19/H17), not the C64U's lamp; his "Keyboard Lights"
  drive the whole chain, and the lamp next to POWER glows in the keyboard colour (photo: royal blue). In our cores the same lamp lights
  amber with SHIFT LOCK: `bling_io.v` sends 70 dark LEDs and the 71st amber while locked, and Gideon's `bling_board.cc` names index 70
  SHIFT LOCK. So the load indicator goes into `bling_io` (LED 71), not a new pin; `case_leds` stays for the U64E2 header. The G17
  experiment in `cores/amiga1200/mono/top.v` (`LED_CASE3n = !led1n`) is disproved (G17 high, lamp dark) and goes back to 0. Also found:
  the A1200's drive-activity source never reached the selector on the board (G17/H19 stayed 0 with a disk loading), so the motor path
  needs a look too.
- **D3 (Alex, 2026-10-30: "then try a, ok").** The C64U power lamp (LED 71 of the keyboard chain) shows a base colour (green) while the
  machine runs, yellow while a drive motor runs or the hard disk is accessed, and SHIFT LOCK in its own colour (red) with priority over
  both. Trial in the A1200 first (its own fork of `bling_io.v`); "LED Load Indicator" Off = the lamp as before (dark, amber/red only
  for SHIFT LOCK). The U64E2 header (`case_leds`, source 4) keeps working in parallel.

### D3 built (2026-10-30, branch `t159-lamp`; colours and levels replaced by D4 below)
- **Lamp:** `cores/amiga1200/mono/bling_io.v` is the host's `bling_io.v` plus `bling_io.diff` (`tools/check_wrapper_fork.sh` checks it; `mono/build_mono.tcl`
  reads the fork). Inputs `drive_act` and `lamp_en`, both through a 2-flop synchroniser; the colour of LED 71 (GRB) is taken once per frame:
  `lamp_en` = 1: green `240000`, yellow `242400` while `drive_act`, red `002400` while SHIFT LOCK is locked (priority); `lamp_en` = 0: dark, amber
  `1E2400` while locked (as before). LEDs 1-70 stay dark. Levels equal the old amber's (0x24 / 0x1E on the lit channels).
- **Enable path (no new register, no app change):** `lamp_en = caseled_set && caseled_sel[3:0] == 4'd4` in `mono/top.v`, i.e. the lamp shows the activity
  colours while case LED 1's selector is source 4, exactly what `led.load` On writes (`0x04`) and Off undoes (`0x00`, which is not 4). `drive_act` is
  `cled_s2[2]`, the synchronised `drive_leds_n[0]` that also feeds `case_leds`. `case_leds` and the U64E2 header are unchanged. `trxapps.sh` not run: no
  coreapp file changed.
- **G17:** `LED_CASE3n` is back to `1'b0` (G17 high did not light the lamp, measured).
- **Motor path, found by simulation, not by reading:** the chain minimig `trx_motor_led` -> `nanomig` -> `amiga_core.drv_led` -> `layer2` (the three regsubs)
  -> `drive_leds_n[0]` -> `cled_s1/2` -> `case_leds` a_act / `bling_io` is correct. `sim/motorpath` runs the full A1200 core (TG68K) with the disk test ROM
  (CIAB PRB: motor on, select DF0): `drv_led` is low after reset and high afterwards; control: `trx_motor_led` tied off fails. `sim/motorwire` runs the patched
  `layer2_amiga.v` (regsubs read from `build_mono.tcl`), a stub `amiga_core` and `top.v`'s extracted case-LED / bling lines: `drv_led` 1 -> case LED 1 on
  and `bling_io.drive_act` high; two inversion controls fail. No defect in the gateware path was found. What the board showed (H19/G17 low with a disk
  loading) therefore does not come from this path: either the board's A1200 `.cor` predates `a77e0cf5b` (2026-10-30 07:50; the flash noted in CLAUDE.md
  is run 1033 of 10-28), or Kickstart's motor sequence differs from the test ROM's (not measurable here). S3 on a build with this commit decides.
- **Benches:** `sim/bling` (iverilog, WS2812 decode: LED 71 per state, 1-70 dark; controls: activity ignored, enable ignored, no priority), `sim/motorpath`
  (Verilator, `tb_l2.sv` `MOTORCHK`, `run_l2.sh` `MOTMUT`), `sim/motorwire`; all in `ci/sims.txt/.deps/.lane`. The earlier note that the wiring "is read, not
  simulated" is replaced by `motorpath` + `motorwire`.

### D4 built (2026-10-30, branch `t159-lamp2`)
- **Lamp (`mono/bling_io.v`, `bling_io.diff`):** `lamp_en` = 1: red `008000` (GRB) while the core runs; SHIFT LOCK locked = yellow `808000`, steady, wins over everything;
  `hd_act` -> blue `000080`/off, else `drive_act` -> red/off; one free-running phase (`FLICK_HALF` = CLK_HZ/18 clocks, ~9 Hz, 50 %), the colour is taken once per
  frame; each activity is held for `2 * FLICK_HALF` (one full period) after its last high, so a short access still shows. `lamp_en` = 0 is unchanged (dark,
  amber `1E2400` with SHIFT LOCK). **Level 0x80:** the WS2812 is a linear PWM and the eye is roughly logarithmic; 0x24 (14 %) was "hardly visible" on the
  board, 0x80 (50 %) is 3.5 times the current and clearly bright without the glare of 0xFF behind the lamp's diffuser. Yellow is G+R at 0x80 each (red-dominated
  as before). `drive_act` stays the combined motor + hard disk signal; since the hard disk wins, the floppy flicker needs no separate floppy-only wire.
- **HD split:** `trx_motor_led` got an output `hd` (= `hdd` or the 50 ms hold; `led` = motor or `hd`, so case LED 1 / the U64E2 header are exactly as before).
  It travels as `motor_hd` (minimig.v, nanomig.v: `.diff` regenerated) -> `amiga_core.drv_hd` -> `layer2` (build_mono.tcl's regsubs: `a_drv_hd`,
  `drive_leds_n = {1'b1, ~a_drv_hd, ~a_drv_led}`) -> `top.v` `.hd_act(!drive_leds_n[1])`; `bling_io` synchronises it (2 flops) like `drive_act`. `case_leds` does not see bit 1.
- **Benches:** `sim/bling` rewritten (observes 14 frames per state, 24 checks incl. short pulses; 9 controls fail), `sim/motorled` (+ hd checks, 4th control),
  `sim/motorwire` (+ drv_hd, 3rd control). `sim/motorpath` (Verilator, builder) is unchanged in content; see the report line below for its run.
- **D-all built (2026-11-01, branch `t164-led`):** `drive_leds_n[0]` (active low) is the floppy indicator in every core with a drive. A500: `trx_motor_led`
  moved from the A1200 fork into the shared `minimig.v` / `nanomig.v` / `amiga_core.v` (`drv_led`, `drv_hd`); `layer2_amiga.v` carries
  `drive_leds_n = {1'b1, ~a_drv_hd, ~a_drv_led}` for both Amigas, so the three `build_mono.tcl` regsubs of the line above are gone and the A1200 forks' `.diff`s
  shrank (`check_wrapper_fork.sh` ok). Atari 800: `trx_sio_led` in `layer2_atari800.v` on `a8_core.tap_sio` (the SIO wire is the disk): any falling edge of the command line
  or either data line holds the LED for ~95 ms (an edge, not the level: the PIA's command output is low after reset). Apple II: `a2_core.drv_led` = Disk II motor
  (`D1_ACTIVE`/`D2_ACTIVE`, the drive's own 1 s spin-down) or the slot-7 card's request open (held 50 ms); `drv_hd` = the card alone. CPC: `cpc_core.drv_led` = port `$FA7E`
  bit 0 on a 664/6128. VC-20 already feeds the 1541's `leds_n`. ZX Spectrum has no drive; ZX Next has none either (`o_BUS_P3_MTR_n` of the +3 bus is not wired). The
  DFX cores (Atari, Apple II) show the lamp only on a host with the T159 `bling_io`; the large cores carry their own host copy (`mono/host/top.v` already wires it).
  Benches: `amiga_motorpath` (A500, new; control: `trx_motor_led` tied off), `atari_led` (new; 3 controls), `apple2_woz` and `apple2_hdd` (LED checks; control `NOLED`),
  `cpc_media` (`disk`: LED low before the motor, high after; control `mut_led`), `amiga1200_motor*` (pattern checks follow the shared layer2).
- **2026-11-01, ZX Next keeps no floppy LED (Alex).** It has no real drive; its disks are images on the SD card (DivMMC, +3e DOS), and the
  +3 motor pin of `zxnext.vhd` stays unwired. Asked whether the LED should show its disk image access: "No, leave it. All good."
- **2026-11-02, board of t164-c83a83c2: the A500's case floppy LED still dark - cause: the selector is never written (`coreapp/cores/amiga.cc:50`).** The gateware path of
  the A500 is the A1200's (same `layer2_amiga.v:309`, same `mono/host/top.v` / `bling_io.v` / `case_leds.v` LED lines, diffed: identical) and `sim/motorpath` proves
  `drv_led` rises. What differed is the application: `variants[0].led_load` was `false`, so the A500 had no "LED Load Indicator" setting, `led.load` never ran,
  `U64_CASELED_SELECT` kept its default and case LED 1 showed `def1` (core running, always on / never following the motor), `bling_io.lamp_en`
  (`caseled_sel[3:0] == 4`) stayed 0. D-all had checked the gateware only; `test_ledload.cc` even asserted "the A500 writes nothing".
  Decision (Alex, 2026-11-02, translated): "The LED load indicator as implemented in the A1200. Just do exactly the same, 1:1." Fix: `led_load = true` for the A500
  (setting default On, one write of 0x04). `test_ledload.cc` now asserts the A500 has the setting, writes 0x04 once; `test_desc.cc` and `menu_amiga.txt` follow.
  Controls: the existing `led_*` controls; putting `led_load = false` back fails the A500 block of `test_ledload`.
- **2026-11-02, same cause in Atari 800, Apple II, CPC (board, Alex: DFX cores, no LED entry in their settings; "the LED load indicator as in the A1200, just the same 1:1").**
  The host side is complete and unchanged: `host/top.v:1435-1441` (`cled_s1 <= {!drive_leds_n[0], ...}`, `case_leds` `a_act`), `:1067` (`bling_io` `lamp_en = caseled_set && caseled_sel[3:0] == 4`),
  `contract/layer2_ports.txt:228` carries `drive_leds_n`; no host file is touched (`contract/host_pin.txt` stays). What was missing is again the app: `atari800.cc`, `apple2.cc`, `cpc.cc`
  had no "LED Load Indicator" row, so `led.load` never ran. Each now has the A1200's row (`{ "LED Load Indicator", led_names, 2, 0, WHEN_LIVE, "led.load" }`, last in its list);
  goldens `menu_atari800/apple2/cpc.txt` follow. Bench `tests/coreapp/test_ledcores.cc`: for A500, A1200, Atari 800, Apple II, CPC the row is there once (On/Off, default On, live), the start
  sequence (`regset_apply`) writes the selector once with 0x04, Off writes 0x00 once, the .cfg keeps it; controls `ledc_atari`, `ledc_apple2`, `ledc_cpc`, `ledc_a500`, `ledc_a1200` (row gone/renamed) fail it.
  The gateware side of each core stays as D-all built it (own benches `atari_led`, `apple2_woz`/`apple2_hdd`, `cpc_media`); the end-to-end board proof is S3 per core (Alex looks).
- **2026-11-02, the shared cause (coordinator: VC-20, C16/Plus4 dark too).** One cause for every core but the A1200: `led.load` (`coreapp/src/engines.cc:112`, `ledload.cc`) is the only code that writes
  `U64_CASELED_SELECT` for a generic-app core and it runs only through a settings row; only `amiga1200` had the row (`led_load = true`). Host (`host/top.v:1435-1441`, `:1067`), contract
  (`layer2_ports.txt:228`) and gateware were fine. VC-20, C264 and C128 take the row from the shared `CBM1541_SETTINGS` (`coreapp/cores/cbm1541_desc.h`, `CBM1541_NSETTINGS` 5), so all three get it in one place
  (the C128's video path is not touched); goldens `menu_vc20/c264/c128.txt` follow; `test_ledcores` covers all eight cores, control `ledc_vc20` (the header row renamed) fails it. ZX Next/ZX Spectrum/ddt cores
  keep none (T159 decision of 11-01). The C64 core (`c64.cc`, own LED selects in `u64cfg.cc`) is unchanged.
- **2026-11-04, board (C128): the lamp flickers blue fast at READY - the 1541's PWM motor LED lands on the hard-disk input.** Cause, from the code: the contract port `drive_leds_n[2:0]` is
  [0] floppy, [1] hard disk (`host/bling_io.v` `hd_act`, blue flicker, wins), [2] unused; the C128, C264 and VC-20 wired the drive's own `leds_n` (`cores/common/drive_l2.vhd`, `c64u_l2.vhd:162-164`: [0] `act_led_n`,
  [1] `motor_led_n`, [2] `dirty_led_n`, active low) straight into it. `motor_led_n` is not a "motor spins" level: `mm_drive.vhd:420-436` PWMs it, on for 1/3 (no disk), 2/3 (disk, motor off) or 4/4 (motor on) of the time
  whenever the drive is powered, so it flickers at READY. The brief's first mapping `~(act | motor)` would have lit the floppy lamp whenever the drive is powered; the lamp follows the drive's own LED (`act_led_n`) alone.
  Fix, once: `cores/c1541/drive_leds_map.v` (`drive_leds_n = {1'b1, 1'b1, act_n}`, no hard disk in these cores), instantiated in `layer2_c128.v`, `layer2_c264.v`, `layer2_vc20.v`; read by their `core_sources.tcl` /
  `mono/build_mono.tcl`, in the `sources` of the vc20, c264, c128 descriptors, `cores/c1541/sim` in `host/layers.txt` `ignore:`. The host is not touched (`host/top.v` reads [0] for the case LED, [1] for the lamp).
  **1541 and 1581 (C128, `c64u_drive_l2` = `mm_drive`):** one `leds_n` port for both modes; `act_led_n` comes from `mm_drive_cpu.vhd:592` (1581: CIA port A bit 6) or `:661` (1541: VIA2 port B bit 3) into the same output
  (`:697`), `motor_led_n` is one process for both modes (`mm_drive.vhd:420`). The mapping reads only the port, so it is mode-independent; the bench does not select a mode (no drive in it), this is read from the code.
  **Scope (Alex, 2026-11-04, translated):** "the drive LED fix must come for every core with a 1541/1581", then "c64ref does not need it! and c64u comes from the firmware project, we do nothing there". So:
  C128, C264, VC-20 only; `cores/c64ref` (host pin), `cores/c64u` and `cores/trx64` (firmware project, no `core.toml`) are not touched and keep the pass-through.
  Benches `c1541leds_c128`, `c1541leds_c264`, `c1541leds_vc20` (`cores/c1541/sim/run_leds.sh <core>`): `tb_drive_leds_map` over all eight inputs (the PWM motor LED alone: all bits 1; activity: [0] = 0) plus
  `check_wiring.py` (the drive's `leds_n` goes through the map, nothing else drives the port, the core's host top reads [0] as the case LED and [1] as the lamp). Controls that must fail: the old pass-through in the
  map (`PASSTHRU`) and in the wrapper (`NOMAP`), motor LED on bit 1 (`MOTOR_B1`), motor LED ORed into the floppy bit (`MOTOR_OR`), map output open (`OPEN`). Board proof (C128 lamp steady at READY, lit while the drive's LED is) is Alex's.
