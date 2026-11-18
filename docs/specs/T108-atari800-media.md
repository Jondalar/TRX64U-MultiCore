# T108 — the Atari 800 XL starts what you give it: a disk, an executable or a cartridge

**Status:** IN PROGRESS. D1 built and green 2026-10-17 (S1, S2, S4 and their controls, §5); S3, S5-S8 open then (S8 closed 2026-11-18). 2026-10-28 (Alex): the cartridge works on the board; S8 still needs a game from D1: and a .xex. **2026-11-15 sync:** the application stop of 5.6 (2026-10-23) is overtaken: the Atari application ran on the board on 2026-11-01 (`.atr` boots, README of 11-02) and its menu row (floppy LED) passed on 2026-11-04 (T159, 76ad599fa); the native tests for S5-S7 exist (`tests/coreapp/test_atari_xex.cc`, `test_atari_media.cc`, `test_atari_sio.cc`) but this sync did not re-check that each §2 scenario has its proof, and S3 (OPTION/BASIC off) and S8 (a .xex, a game from D1: with a photo) have no recorded pass; not DONE. The row's "recheck the 10-23 app stop" is outdated. **2026-11-18:** S8 closed as done (shipped and in use, end of §5); its run with BASIC off goes with S3, which stays open (`coreapp/cores/atari800.cc` has no BASIC setting).
**Repo:** TRX64U-MultiCore — the core application (`coreapp/cores/atari_*.cc`), the core's SIO and console
inputs (`cores/atari800/trx/`), a new simulation under `cores/atari800/sim/`, `ci/sims.txt`.
**Number:** T108 (registry: the table in `TODO.md`).
**Origin:** Alex at the board, 2026-10-16, after Boulder Dash would not start: "Hm ... Spec ? ATARI 800
sollte mit Disks, ATR, xex, und Cartidges starten bitte" (translated: the Atari 800 should start from
disks, ATR, xex and cartridges).
**Depends on:** [T88](done/T88-atari800-core.md) (the core itself, on the board since 2026-10-15),
[T93](../todo/T93.md) (the application blueprint), [T104](done/T104-generic-core-app.md) (the file actions and
the media slots it describes).
**Milestone:** M3.

---

## §1 The problem

The Atari core boots and runs BASIC — Alex typed into it on 2026-10-15 and it answered. Nothing else
about it has ever run on hardware. T88's row says so: "menu, disks, cartridges, settings and sound
untested".

On 2026-10-16 he tried the first real disk. `boulderdash.atr` (23,568 bytes, a valid ATR: magic `0x0296`,
128-byte sectors, 184 of the 368 sectors its header declares) went on the stick, was mounted to D1: from
the browser, and **Cold start** was pressed. The screen showed `READY.` — Atari BASIC.

`READY.` is the diagnosis, not a symptom: an XL asks D1: over SIO at a cold start, and when nothing
answers within its timeout it drops into BASIC. So the boot request was never answered. The application
has the SIO handler (`coreapp/cores/atari_sio.cc`, 343 lines) and benches for it
(`tests/coreapp/test_atari_sio.cc`), but those run our model against our model. Between the model and
POKEY's serial port in the core there is **no test at all**: `cores/atari800/sim/machine/run.sh` knows
`boot`, `cart8`, `cart16` and `cartmax` — there is no disk mode.

What it costs: the core is a BASIC prompt. Every disk, every executable and every cartridge image a
person owns is unreachable, which is most of what an Atari is for.

**Parts (MECE):**

- **P1 The disk answers.** A `.atr` mounted to D1: is found by the OS at a cold start and its boot
  sectors are read, so the machine runs what is on the disk.
- **P2 The executable runs.** A `.xex` is presented as a boot disk (`a8_run_xex` builds one and cold
  starts, `atari_media.cc:61-70`) and reaches its first screen. It rides on P1's path.
- **P3 The cartridge runs.** A `.car`/`.rom` is inserted and the machine starts from it
  (`a8_insert_cart`, the type written to `A8_CART1` at the reset). The gateware side has benches
  (`sim/machine cart8|cart16|cartmax`); the application side and real images have none.
- **P4 BASIC can be switched off.** On an XL you hold OPTION at power-up; many disk titles need the RAM
  and refuse to start otherwise. The application has four settings — RAM, video, joystick ports, CPU
  speed (`coreapp/cores/atari800.cc:36-41`) — and no way to do this. The console keys exist in the core
  (`a8_core.vhd:259,292`) but come from the keyboard, so nothing can hold OPTION while the OS looks.
- **P5 The answer is fast enough.** SIO is a real-time protocol; the OS gives a drive a bounded time to
  answer each frame. The application services it from its loop (`coreapp/src/run.cc:284-289` spins
  rather than sleeps "frames come in bursts: sector after sector"), and **nobody has measured the
  latency**, in a bench or on the board.
- **P6 Inserting means starting.** Alex, 2026-10-16, asked whether a disk should start by itself:
  "Ja das bequeme" (translated: yes, the convenient one). D1: cold starts when a disk is mounted, D2:
  to D4: only mount - nothing boots from them.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 the machine boots a disk
  Given the real XL OS and BASIC in the core, and a disk image in D1: whose boot sectors load a program
  When  the machine cold starts
  Then  the OS sends the SIO command frame for sector 1 of D1:, the application answers it,
        and the picture after N frames is the program's, not the BASIC prompt
  And   no SIO frame is answered later than the OS's timeout
```

```gherkin
Scenario: S2 a drive that is not there stays not there
  Given no image in D1:
  When  the machine cold starts
  Then  the OS gets no answer and ends at the BASIC prompt
  And   the application never answers a command frame for a drive it has no image for
```

```gherkin
Scenario: S3 OPTION is held while the OS looks
  Given a disk image in D1: and the setting "BASIC" set to off
  When  the machine cold starts
  Then  the OS reads CONSOL with the OPTION bit set during its power-up check
  And   BASIC is not mapped afterwards: $A000 holds RAM, not the BASIC ROM
```

| # | Solves | Scenario | Test (bench / board step) | Where it runs |
|---|---|---|---|---|
| S1 | P1, P5 | the disk boots | **new** `cores/atari800/sim/disk/run.sh disk`: the whole core under GHDL with the real OS, the application's SIO responder driving the register window, a generated boot disk; the picture compared against the boot program's, every SIO frame's answer time recorded | `ci/sims.txt` tier 2 (64 min, not tier 1) |
| S2 | P1 | the empty drive (negative control) | same bench `empty`: no image, the OS gets no answer and the run must end at the BASIC prompt; `ghost`: a drive that answers although nothing is mounted must make the bench **fail** (no frame may be answered) | `empty` by hand (about 3 h), `ghost` in `ci/sims.txt` tier 2 |
| S3 | P4 | OPTION held, BASIC gone | same bench with the setting on: a read of $A000 after the boot returns RAM | `ci/sims.txt` tier 1 |
| S4 | P5 | the slowest answer | the bench prints the worst command-frame answer time in microseconds against the OS's limit; `slow`: a loop that sleeps 100 ms after every service call that did something has to miss it | `ci/sims.txt` tier 2 |
| S5 | P2 | a `.xex` reaches its first screen | `tests/coreapp/test_atari_xex.cc` keeps the loader honest; the picture is S1's bench with the `.xex` boot disk | native + sim |
| S6 | P3 | a real cartridge image starts | `sim/machine cart8\|cart16\|cartmax` already prove the gateware; the application side gets a native check that the type byte and the window match the image's header | native, `tests/coreapp` |
| S7 | P6 | D1: starts, D2: does not | native: mounting to D1: calls the cold start, mounting to D2: does not. Control: a build where every drive cold starts must fail it | `tests/coreapp` |
| S8 | P1, P3, P4 | the board: Boulder Dash from D1:, a `.xex`, a cartridge, once with BASIC off | by hand, with a photo — a picture out of a real machine is the only proof that the chain holds end to end | board session |

Rules kept: every part has a scenario; S2, S4 and S7 carry controls that can fail; S8 needs the board and
a real image and says so. The image Alex put on the stick (`/Usb1/roms/atari800/boulderdash.atr`) is his
and is **not** checked in — only generated disks are.

## §3 Decision

- **D1 The disk path gets a simulation of its own.** `cores/atari800/sim/disk/`, built like the machine
  bench, with the application's SIO responder compiled against the register model so the two halves meet
  for the first time. Without it every fix is a guess: the board shows one word, `READY.`
- **D2 Mounting to D1: cold starts** (Alex, 2026-10-16: "Ja das bequeme"). D2: to D4: mount only. The
  code is the line `a8_run_xex` already ends with (`boot_cold_reset`), moved into `a8_mount` behind a
  check on the drive number. **Built 2026-10-16:** the action is called "Mount D1: and cold start" and
  closes the menu like "Run" does; `test_atari_media` checks that D2: leaves the machine alone and D1:
  produces `mode_reset` / `mode_unreset`.
- **D6 "Mount D1:" swaps without a reset** (Alex, 2026-10-20, board, Apple IIe: "But I must also be able to change the disk WITHOUT a
  cold start. That is an error in the capabilities", translated; the Atari gets the same). D2 stays the default: the first file action,
  "Mount D1: and cold start", boots. The mount operation takes an explicit `boot` argument (0/1) instead of deciding by the drive number
  (`a8_mount(..., drive, boot, ...)`, `apple2.mount(path, drive, boot)`); the second action "Mount D1:" binds `boot=0`, D2: to D4: bind
  `boot=0`. **Built 2026-10-20:** `test_atari_media`, `test_menuctl` and `test_apple2` check no `mode_reset` on the swap; controls
  `a8_swapresets`, `a8_noboot`, `a2_swapresets`, `a2_nocoldmount` fail as they must. (There is no `docs/todo/T108.md`; the TODO row
  links this spec, so the history lives here.)
- **D3 BASIC becomes a setting, and it is the OPTION key.** Not a new path in the gateware, and
  **measured on 2026-10-16**: `cores/atari800/sim/machine/run.sh boot` reports `ok F5 pulls CONSOL
  OPTION low`, so the console keys already reach GTIA through the keyboard the application drives. The
  application holds that key across the cold start and the OS sees what it would see from a finger. No
  register bit needed.
- **D4 The answer time is a number in the report.** The bench prints the worst answer time per run, as
  the Amiga's audio bench prints its worst pitch deviation. A regression in the application's loop then
  shows up as a number before anyone hears about it from the board.
- **D5 Nothing third-party is checked in.** The benches generate their disks; `cores/atari800/roms/` keeps
  the OS and BASIC, as CLAUDE.md allows, and nothing else.

## §4 Out

Decided out before the work, not by omission:

- **Tape (`.cas`) and the cassette port.** No image, no demand.
- **Writing back to a `.atr` beyond what exists today.** `a8_mount` already opens read/write when the
  medium allows it; making a write-protected or short image writable is not this spec's job.
- **Fast loaders** (Happy, US Doubler, XF551 high speed). The OS's standard SIO rate is the target.
- **DOS.** A disk that carries a DOS boots it; the application does not provide one.
- **The 1050 double-density geometry beyond what `atr_geometry` already reads** (128 and 256 byte
  sectors, `atari_atr.cc:6-51`).
- **The cartridge type zoo.** The types the core knows stay the types it knows; new mappers are their
  own task.

## §5 Decisions taken while building

Built and measured 2026-10-16/17 by the agent for the coordinator; every number below is a line of the bench's own report
(`cores/atari800/sim/disk/build/work_<mode>/run_<mode>.log`, not kept in git). Simulated time is counted from the cold start.

### 5.1 The bench (D1)

- **What runs.** `cores/atari800/sim/disk/run.sh <mode>`: the whole core (`a8_core`) under GHDL with the real XL OS and BASIC
  of `cores/atari800/roms`, RAM size 64 KB (the application's default, `atari800.cc` setting 0), PAL, the machine started from
  its reset the way the application does (ROMs loaded through the window while held, then released).
  `coreapp/cores/atari_sio.cc` and `atari_atr.cc` are compiled **unchanged** into the simulator (`app_bridge.cc`, VHPIDIRECT;
  g++ and a GHDL that links: llvm or gcc backend). The engine is straight blocking code, so it runs in a thread of its own:
  every `SioLink` access, every `delay_us` becomes a request the testbench carries out on the core's register window
  (`cw_*`, the offsets of `atari800_regs.h`) and answers; simulated time passes in the testbench. The testbench plays
  `run.cc`: one `a8_service` per 5 ms (`service_period_ms`, `atari800.cc:166`), spinning for 5 ms after a call that did
  something, 1 ms per idle pass (`vTaskDelay(1)`). **Assumed, not measured:** one bridge access costs 2 us (`ACCESS_US`); the
  real cost over the host bus is unknown (S8).
- **The disk.** Generated in the bench, nothing third-party: `bootdisk.asm` (64tass) is three 128-byte boot sectors, `gen_disk.py`
  makes a 720-sector ATR of them. The program paints the screen with the colour that is the last byte of sector 3
  (`$46`), so a boot that stops after sector 2 shows another picture.
- **The wire.** The bench watches the SIO lines itself: `a8_core`/`a8_machine` got an output `tap_sio` (command line, POKEY's
  serial out, the drive's line into POKEY); nothing reads it but the benches, and the other benches leave it open (GHDL 5.0.1
  aborts on VHDL-2008 external names into the instance, as `T83` noted). It decodes the bytes at 19200 baud, and for every
  command frame measures the time from the command line's rising edge to the start bit of the first answer byte.
- **Clocks.** `cpu_clk` 15 ns and `ui_clk` 15.3 ns, not the machine bench's 5 and 5.1: 28 % less wall clock (60 s to 43 s for 28
  ms); the register bridge and the DDR2 model are functional at either.
- **The check.** The picture the pixel writer put into the DDR2 window: the field pixel (centre) is not the OS's own field
  colour (`$030E`, RGB565 of `$94`, measured) and at most 100 pixels differ from it (the 64 of the cursor). `empty`/`ghost`
  expect the OS's field with text on it (more than 150 pixels of a third colour) and no answered frame.
- **Cost.** `-O2`, 2-3 s of wall clock per simulated millisecond: `disk` 64 min on the Mac (container, 2 cores, two benches at
  a time) for 2.3 s of simulated time; the OS spends the first 1.57 s before its first SIO frame. **Not tier 1.**

### 5.2 The cause (P1) and what was not

**One sentence:** `coreapp/cores/atari_sio.cc` never wrote `SIO_DIVISOR`, so the handler's transmit divisor stayed at its reset
value 0 (`sio_handler.vhdl:150`, reloaded into the bit counter at `:285`) and every answer left the core at one bit per machine
cycle, a byte in about 6 us, which POKEY cannot read; the OS gets nothing it can use, retries to its limit and starts BASIC.

- **Proof by the wire.** First run of the bench (1.6 s, before `match_baud()` existed): the NAK `4E` to a `$3F` command left as
  three falling edges inside 5 us (`drive -> OS 00 at 1577835 us`, `...1577838`, `...1577840`; that decoder sampled at the wrong
  time scale and printed one `00` per falling edge, the log is not kept). With the fix the bytes are
  535-536 us apart (`drive -> OS 18 at 1615421 us`, `FF at 1615956`, `E0 at 1616492`: 95 machine cycles a bit; the OS's own
  bytes are 530 us apart, 94). The control `nobaud` (the product source with the `match_baud();` call cut out by `sed`, as the
  native controls do) repeats it (final source): `sio: 19 command frames on the wire, 19 answered, 0 not; the application saw 19 frames` (a start
  bit came back every time, the OS read none of them), `FAIL the picture is the boot disk's program ...: 030E, 64 pixels of a
  third colour`, and the runner says `PASS control nobaud: the bench fails as it must`. The same source with the call is S1.
- **The fix.** `SioEngine::match_baud()` at the top of `command()` (after the "not ours" return): read register 4 (the bit
  time the handler measured on the Atari's last byte), write it back as the transmit divisor; the handler applies it when the
  transmitter is idle. Native: `test_atari_sio` checks every byte went out at the divisor the Atari spoke at, and that a frame
  for a drive that is not there touches nothing; control `sio_baud` (call removed) goes red (3 checks).
- **Also changed, not found necessary.** The old `delay_us(T5_US)` after queuing the ACK counted from the moment the ACK was
  *queued* (a byte takes 521 us), so COMPLETE followed it with no gap. `after_ack()` now waits until the ACK has left, one byte
  time, then T5 (300 us) in all four ACK paths. **The OS boots without it**: the variant with no gap at all, run as a control
  of the bench, passed S1 (`nogap`: `ok the picture is the boot disk's program ...: B1B3, 64 pixels`, `PASS tb_a8_disk`), so
  it is in the code because T5 is 250 us at the least and the constant said so, not because the OS needed it. Native control
  `sio_gap` (1 check red). Alex can take it out; nothing in the simulation depends on it.
- **What else the OS does at a cold start** (the Hias 1.30 patch in `atarixl.rom`, README): before the boot it probes D1: with
  `$3F`, `S 20 80`, `$D3`, `$48`, five tries each, about 190 ms. The application NAKs `$3F`, `$D3`, `$48` (unknown command) and
  answers `S`. The OS drops the `S 20 80` probe while our ACK byte is still on the wire: the command line rose at 1612294 us,
  the ACK's start bit came at 1612944, the OS pulled the line low again at 1613158 (edge log of the 2026-10-16 probe run, not
  kept), so the probe never succeeds. Harmless, five tries, no code change.
- **Boot.** The OS then reads sectors 1-3 (`R` 1, 2, 3), 128 bytes each, 131 bytes with ACK, COMPLETE and checksum, about
  80 ms a sector, and runs the continuation at load+6. The first version of `bootdisk.asm` started its program through the DOS vector instead
  and did not run: the picture stayed the OS's field and the OS kept sending a frame of its own (`4F 40 4F 4F 2E`); why was not
  investigated. Disks that start that way may need BASIC off - that is P4/S3, not this.

### 5.3 S1, S2, S4 measured

- **S1** (`run.sh disk`), the final source: `sio: 24 command frames on the wire, 16 answered, 8 not; the application saw 16
  frames, 0 bad checksums, 9 unknown, 0 bridge timeouts`, `picture: border pixel 0000, field pixel (centre) B1B3, 64 pixels
  of a third colour`, `ok the picture is the boot disk's program (...)`, `PASS tb_a8_disk`. The three sector reads were
  answered 2256, 3831 and 4120 us after the command line rose.
- **S4, the number (D4).** `sio: worst command-frame answer time 5029 us` (S1), 5265 us in the runs before the last
  change. It is the poll period, not the work: a frame is picked up at the next 5 ms service call, then 300 us (T2) to the ACK.
  Against it, the OS's limit: an unanswered frame is repeated 33.3-36.7 ms after the line rose (`empty`: 33337, 36507, 36731
  us; fits the `ldy #$02` at `$ECBC` before the timer call `$EDE2`: two 50 Hz jiffies), so the margin for the standard
  path is a factor of six.
  The probe frames above have a much shorter window (about 0.8 ms) and are lost whatever we do.
- **S4, the control** (`run.sh slow`: the loop sleeps 100 ms after every service call that did something): `sio: 22 command frames on the wire, 9 answered, 13 not;
  the application saw 17 frames`, `sio: worst command-frame answer time 31838 us` (against 5029 us), `FAIL the picture is the boot
  disk's program ...: 030E, 64 pixels of a third colour` (the disk does not boot), `PASS control slow: the bench fails as it
  must`. The 31.8 ms are the answers that still came before the OS began its next frame; the 13 others came too late or
  were lost under the OS's repeats. 65 min.
- **S2** (`run.sh empty`, nothing mounted; 6.6 s of simulated time, 3 h 9 min): `application loop ended at 6609 ms`, `sio: 104
  command frames on the wire, 0 answered, 103 not; the application saw 104 frames`, `picture: border pixel 0000, field pixel
  (centre) 030E, 179 pixels of a third colour (cursor, text)`, `ok the picture is the BASIC prompt (...)`, `ok no command frame
  was answered: the drive is not there (104 frames, 0 answered)`, `PASS tb_a8_disk`. The frame is `READY.` and the cursor, the
  picture Alex saw on the board. The OS tries `$3F`, `S 20 80`, `$D3`, `$48` five times each, then `S` and `R` with the full
  retry count, every try 33-37 ms apart.
- **S2, the control** (`run.sh ghost`: an unmounted drive that answers, sectors of zeros): `sio: 15 command frames on the wire, 8 answered,
  6 not; the application saw 8 frames` (decided at the first answer, 1.8 s of simulated time, 46 min), `FAIL no command frame was
  answered: the drive is not there (15 frames, 8 answered)` (printed as `red ...` by the runner), `PASS control ghost: the
  bench fails as it must`. Without the ghost, `empty` passes the same check with 0 answered.
- **Controls that must fail, and do:** `nobaud` (5.2), `slow` and `ghost` above; native `sio_baud` (3 checks red) and `sio_gap`
  (1 red). The `getenv` trap does not apply: the bench's controls are modes of one script (`$1`), not environment switches.
  The runner prints a control's red lines as `red` and `PASS control <mode>` when it failed on the check it was meant for,
  so `ci/sims.sh` (which fails a bench on any `FAIL` line) sees the control for what it is. `nogap` (no gap anywhere) passed S1
  and is therefore not a control; it is not in the script.

### 5.5 Board, 2026-10-19 (Alex, photos): the .atr boots; the picture differs between two boots

- First boot: `boulderdash.atr` loads but the picture is garbage (ROM glyphs, repeated rows, colour bars). Suspicion, not
  proven: the game loads into $A000-$BFFF, which the BASIC ROM still covers (P4 of §5.4).
- Second boot, F5 (OPTION, `a8_keys.vhd:96`) held, but whether it took effect is unknown (Alex: "whether F5 helped I don't
  know", translated): the "Black Dot/AHT MegaTrainer +2 / Boulder Dash" trainer menu appears correctly. Still broken: the scroll line at the bottom shows the right font but scrambled text, and the menu
  cannot be operated with joystick, keyboard or mouse (Alex: "Both. And mouse", translated). Causes not read yet; the BASIC suspicion is checked by reading PORTB bit 1 after the boot.
- The scroll line moves smoothly (Alex: "it scrolls cleanly", translated): the CPU and the VBI run, the machine does not
  hang. Suspicion, not proven: the scroll text and the menu's state live in RAM under the OS ROM ($C000-$FFFF, PORTB bit 0)
  or under BASIC (bit 1); if the core's PORTB banking does not switch, the program reads ROM bytes there: the right font
  with wrong text, and a menu loop that never sees its input. Check in GHDL: write PORTB, read back RAM under the ROMs.

### 5.6 Board, 2026-10-23: the application stops after "released" - read from the code, NOT decided (hang hunt, t108-hang)

Board (release main-66eefd51): the log ends after `[core] Atari 800 XL released`; the rocker's menu tap does nothing, FTP and HTTP do
not answer; holding the rocker resets the machine. No fix was made: the code does not decide the cause, and none is claimed.

Refuted or narrowed by reading the code:
- **"An unacknowledged core-window read stalls the CPU for ever" is false.** `host/ultimate_sys.vhd:1449-1475` (core_cnt/core_to) acks
  every read or write of r_cw, r_rom, r_dma, r_drive and r_gcr with 0 after 65536 clocks (about 1 ms at 62.5 MHz), and at once while the
  core is decoupled. `cores/atari800/trx/a8_regs.vhd` acks every read and write in the next clock (`ack_r`), page 6 (pixel layer) and
  the unmapped rest included. A dead core makes the application slow (1 ms per read), not silent.
- **The hold-reset is pure hardware.** `host/ultimate_sys.vhd:829` (event 0x83 -> `esp_reset` 200 ms) and `:1509`
  (`c64_reset_n <= '0' when ... esp_reset /= 0`) never reach the application. The menu tap is event 0x82 -> `buttons(1)` for 200 ms
  (`:828`, `:836`), which only the application reads (`coreapp/src/hw.cc:376`). So the reset says nothing about the application; the
  dead tap does.
- The application paths after "released" are bounded and sleep: the loop `coreapp/src/run.cc:471-484` (vTaskDelay(1)); the poll
  `coreapp/src/service.cc:20-59` (burst capped at BURST_MAX_MS = 20, then a sleep); `coreapp/cores/atari_sio.cc` wait_idle at most 200
  reads, poll at most 300 words. The poll task runs at priority 2 (the tcpip task's), the loop and the netservice tasks at 1
  (`apps/ultimate/software/FreeRTOS/Source/FreeRTOSConfig.h:5-15`): an always-busy poll takes 20 of every 30 ms from them - slow,
  not dead. No code path was found that blocks all tasks.
- The Atari RTL and descriptor are unchanged since 2026-10-19 (the diff of `cores/atari800` between a9e117a37 and 66eefd51 is the
  logo only). Alex's 2026-10-19 "trainer menu" was the game's menu (5.5), not the application's: no board run of the Atari application
  menu on a build after T104.1 is on record, so "it worked until X" cannot be dated.

Open candidates the code cannot separate (a wedged CPU or memory port, not a slow loop):
1. DDR2 starvation of the RISC-V: `host/ddr2_port.v` mig_arb serves cart_fill (`c_en`, the Atari cache `a8_mem.vhd:371`) before
   everything and the engine (`e_en`, `a8_pixw.vhd:214`) next; the CPU is in the `go` term, which needs `!c_en && !eng`.
2. An exception, or a loop with interrupts off, in application code.

The discriminator is one JTAG snapshot with the application dead: `tools/trxdbg.sh rvpc` (rv_dbg.v words 0-7), taken twice, 2 s apart.
- PC moving, word 4 (clocks the PC stood still) small: the CPU runs - the application is slow or starved; then read core window 0x0A8
  (b3 = memory access under way) and the Atari frame counter 0x0AC twice.
- PC still, word 4 growing, word 7 b24 (I/O busy) set: stuck on an I/O address; word 7 b23:0 names it (0x1A0000-0x1AFFFF = core
  window, low 17 bits the register; 0x100000-0x1009FF host pages).
- PC still, word 1 b25 (memory pending) set and b24 clear: waiting for the DDR2 (candidate 1); words 1 and 2 give the address.
- PC still, nothing pending: a trap or a spin with interrupts off; word 0 is the PC, to be resolved in the application's map.

### 5.4 Not done, and why

- **S3, S5-S8** are open, as the table says (S8 closed as done 2026-11-18, end of §5); S8 (the board, Alex's `boulderdash.atr`) is the proof that matters and is not
  replaced by any of this. Boulder Dash may need BASIC off (P4).
- **CI: not in the pipeline, on the NAS by hand only** (Alex, 2026-10-17: "Raus. Wofür haben wir das NAS ? Und das soll
  nicht immer stundenlang dauern ... Nur auf Knopfdruck", translated: out; that is what the NAS is for; it must not take
  hours every time; on demand only). Four rows (`disk`, `ghost`, `nobaud`, `slow`) of 46-65 min each on the Mac against
  a sims stage of 120 min that already takes about 60 would have stopped every release. They are tier 3 in
  `ci/sims.txt`, which the pipeline never selects (it runs 1 or 2), and the workflow **Sims (long)**
  (`.gitea/workflows/sims-long.yml`, `workflow_dispatch` only, `runs-on: nas`, two at a time) runs exactly the tier-3
  rows. Unchanged Atari sources give a cached PASS in seconds. Start it before an Atari release. `empty` (6.6 s of
  simulated time, about 3 h) stays by hand.
- **The access cost** (2 us) and the loop model are the bench's assumptions; a slower bridge or loop moves the 5 ms.

**2026-11-18 - board steps closed as done (shipped and in use):** S8 (Atari on the board: a game from D1: and a `.xex`; the cartridge passed 2026-10-28;
the run with BASIC off goes with S3, which stays open) - Alex 2026-11-15: "Under A there is nothing I would not class as DONE! That has been in the builds
for days/weeks, we had bugs and re-tests in part"; Alex 2026-11-18, shown these steps as open: "that is old stuff that has been running for ages, we had
this already 3-4 days ago" and "tick it off in the TODOs/specs, otherwise it shows up again with some agent" (translated).
