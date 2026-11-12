# T191 — The Amiga cores can have a battery-backed clock at $DC0000 whose time the board sets

**Status:** DECIDED (2026-11-06).
**Repo:** TRX64U-MultiCore — gateware in `cores/amiga` (shared with the A1200 copy in `cores/amiga1200/rtl`), the core app in `coreapp/cores/amiga.cc`.
**Number:** T191 (registry: `TODO.md`).
**Origin:** GitHub issue #52 (a tester, Amiga Test Kit v1.21 reports "No Clock Detected"). Alex (2026-11-06, translated): "write the spec, set the TODO, start".
**Depends on:** T85 (the Minimig core), T97 (the A1200), T104 (the core app's descriptor), T116 Q10 (the app supplies the time).
**Milestone:** M3.

---

## §1 The problem

Neither an A500 nor an A1200 has a clock without a trapdoor board or a clock board, so the stock machine - and our cores as built -
has none. Amiga Test Kit's battclock page says "No Clock Detected", Workbench starts on the date of the Kickstart/disk, files get
wrong stamps, and `SetClock` has nothing to read.

What is in the tree (read from the code, not from the issue):

- `gary.v` L179-L183 decodes `sel_rtc` = $DC0000-$DCFFFF **only under `ENABLE_RTC`**; without it `sel_rtc` is a constant 0 and
  $DCxxxx is chip-register space (`sel_reg` L185).
- `minimig.v` L873-L1002 holds the BCD registers, a 28,375,160-clock counter and a 12-bit `rtc` input (`rtc[11]` toggles = "new value",
  `rtc[10:8]` field, `rtc[7:0]` value). `nanomig.v` L66-L69 exposes it. **No build defines `ENABLE_RTC`** (`cores/amiga/core_sources.tcl`,
  `cores/amiga/mono/build_mono.tcl`, `cores/amiga1200/core_sources.tcl` and its mono script).
- The block **is not usable as it is**: `rtc_out` returns constants 0, 0, 4 for the control registers D, E, F and ignores writes; the
  carry chain has index errors (the minutes tens digit increments `rtc_reg_2`, the hours digits `rtc_reg_3`/`rtc_reg_4` are the
  wrong ones, a comment ends the chain at hours); nothing drives `rtc` in `layer2_amiga.v` / `amiga_core.v`. Amiga Test Kit
  (`testkit/battclock.c` L98-L161) writes HOLD to register D and wants it read back.

Parts (MECE):

- **P1 Chip.** An MSM6242-compatible register file at $DC0000 that software can read, write and set: time/date counters that really
  tick (including carries into day, month, year, weekday), control registers D/E/F with HOLD that read back.
- **P2 Switch.** The stock machine stays as it is: a setting "Real-time clock" Off (default) / On; Off = the bus does not answer at $DC0000
  (Test Kit says "No Clock Detected" exactly as today).
- **P3 Time source.** The core app loads the board's time at core start (the ZX Next pattern: `CoreIo::wall_time`), through registers.
- **P4 Cost.** Area and timing in both Amiga builds, the A1200 one being dense.
- **P5 Proof on the machine.** Amiga Test Kit and Workbench on the board.

## §2 Scenarios

```gherkin
Scenario: S1 the time loaded by the app is read back as BCD (P1, P3)
  Given the RTC enabled and the app's register sequence for 2026-11-06 (Friday) 14:05:09
  When  the 68000 bus reads $DC0003, $DC0007, ... $DC0033 (register 0..C at address 4n+3, low nibble)
  Then  the nibbles are 9 0 5 0 4 1 6 0 1 1 6 2 and weekday 5 (S=09, M=05, H=14, D=06, MO=11, Y=26)
Scenario: S2 the seconds tick and carry (P1)
  Given the time 2026-12-31 23:59:58, weekday register 6
  When  3 seconds have passed (the bench shortens the divider with a parameter)
  Then  it reads 2027-01-01 00:00:01, weekday 0 (6 wraps); and 2028-02-28 23:59:59 + 1 s = 2028-02-29 00:00:00 (leap year); 2027-02-28 + 1 s = 03-01
Scenario: S3 HOLD stops the visible counters and reads back (P1)
  When  the bus writes 1 to register D and reads it
  Then  D reads 1; during HOLD the seconds do not change over 3 s; after writing 0 they catch up by the held second(s)
  And   a write of 1 to E and a write of 4 to F read back as written (D bit 1 BUSY reads 0)
Scenario: S4 the register map as Test Kit probes it (P1)
  When  the bench runs the probe sequence of battclock.c (read D, write HOLD, read back, write 0, read back; read F/E)
  Then  all reads equal the writes; the same sequence against a build with read-only D/E/F is detected as "no clock"
Scenario: S5 Off is the stock machine (P2)
  Given rtc_en = 0
  When  the bus reads or writes $DC0003
  Then  nothing answers on the data bus (rtc_out = 0) and sel_rtc = 0 (the address falls to the chip-register decode as today)
Scenario: S6 the setting and the time load (P2, P3)
  Given the core app tests (test_amiga) with a fake CoreIo (wall_time set / unset)
  Then  the descriptor has "Real-time clock (at next reset)" Off/On bound to AM_RTC_EN, default Off; with On and the clock not loaded the
        app writes the 13 nibbles for the fake time then marks it loaded; a second release does not load again; with Off nothing is written
  And   with wall_time() false the app loads a fixed date (as the ZX Next app) and a negative control (tests mutate the BCD/order) fails
Scenario: S7 area and timing (P4)
  Then  the module costs <= 250 LUT / 120 FF; the Amiga flat builds stay routed with the same WNS class (Vivado branch run, coordinator)
Scenario: S9 the board's time reaches the core app (P3; board finding 2026-11-07)
  Given a hand-over page of a host that has a valid clock, and one without a time (older host, or no clock set)
  When  the core app starts and `CoreIo::wall_time` is asked (real handover.cc, rtc_dummy.cc, walltime.h)
  Then  with the time: true and that date/time, ticking on; without it: false (the fallback date); noise, another core's block and an
        implausible date are refused; negative controls (clock never set, check dropped/unmixed, plausibility dropped, page validity dropped) fail
Scenario: S8 the machine, board (P5)
  Given Off: Amiga Test Kit 1.21 battclock page: "No Clock Detected"
  When  On, next reset, Test Kit's page opens
  Then  it shows the board's time and ticking; Workbench `date` and file stamps show the date; SetClock SAVE / RESET works within the session
```

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 P3 | time read back | `cores/amiga/sim/run_rtc.sh` (`tb_rtc.sv`) | `ci/sims.txt` tier 1 `amiga_rtc` |
| S2 | P1 | ticks, carries, leap | same | same |
| S3 | P1 | HOLD, E, F | same | same |
| S4 | P1 | Test Kit probe; control `AMIGA_RTC_MUT_RO` (D/E/F read-only) must FAIL | same | same |
| S5 | P2 | Off = no answer; control `AMIGA_RTC_MUT_NOEN` (enable ignored) must FAIL | same | same |
| S6 | P2 P3 | setting and load | `tests/coreapp` (`test_amiga`), mutation checks | `ci/apptests.sh coreapp` |
| S7 | P4 | LUT/FF of `amiga_rtc` (yosys) now, Vivado report line in the branch run | `=== T85 amiga1200 synth` / mono report | CI `bitstream` |
| S9 | P3 | host time -> hand-over page -> core app clock -> `wall_time` | `tests/hostapp/test_walltime.cc` + 6 controls | `ci/apptests.sh hostapp` |
| S8 | P5 | Test Kit, Workbench | by hand with a photo: it is the only proof against a real OS | board session |

The negative controls are built into the bench: a gateware with read-only D/E/F (the inherited behaviour) and one without the enable
must make the same bench fail.

## §3 Decision

- **D1 (Alex, 2026-11-06, the issue's proposal): optional, default Off.** Setting "Real-time clock (at next reset)" Off/On for `amiga` and
  `amiga1200`, in the machine settings (F5, with chip/slow/fast RAM). On = an MSM6242-compatible clock at $DC0000; the core app loads the
  board's time at core start the way the ZX Next core does (T116 Q10); the control registers D, E, F accept writes and read back (at
  least HOLD) so that Amiga Test Kit, `battclock.resource` and `SetClock` work.
- **D2 the chip is a new module `cores/amiga/rtl/trx/amiga_rtc.v`**, used by both cores (the A1200 build takes `trx/` files unless it keeps
  an own copy); the inherited inline block in `minimig.v` is replaced by one instance. Reason: the inherited block is wrong (§1) and one
  testable module beats a patched 120-line block in a vendored file. The vendored files change as little as possible (instance, enable
  port, ports through `nanomig.v`); the A1200's own copies and their `.diff` files follow.
- **D3 always built, enabled at run time.** `ENABLE_RTC` is defined in all four builds (both ooc and both flat scripts); the setting drives
  `rtc_en` (register AM_RTC_EN). Reason: a compile-time option cannot be a setting; the cost is the module's few hundred cells (§2 S7).
- **D4 register file (MSM6242B).** Register n at $DC0000+4n+3 (low byte, nibble in D3:D0; word access at 4n+2 reads the same nibble in the
  low byte, as the inherited decode). 0 S1, 1 S10, 2 MI1, 3 MI10, 4 H1, 5 H10, 6 D1, 7 D10, 8 MO1, 9 MO10, A Y1, B Y10, C W (0-6),
  D: b3 30s-ADJ (write 1: seconds to 0, minute +1 if seconds >= 30; reads 0), b2 IRQ flag (reads 0), b1 BUSY (reads 0), b0 HOLD;
  E: stored nibble (t1, t0, ITRPT, MASK: no interrupt is generated); F: stored nibble, resets to 4 (24 h), only 24 h counting is implemented.
  Writes to 0-C set the counters (SetClock). 1 s is `28,375,160` ce28 ticks, as the inherited counter.
- **D5 HOLD** freezes the readable counters; one held second is kept and applied on release (the datasheet's carry-in-hold; a longer HOLD
  loses seconds, which software never does: Test Kit and AmigaOS hold for microseconds).
- **D6 the time load** is a nibble file through the register block (cpu_clk side): `AM_RTC_VAL` (0x0E1) the nibble, `AM_RTC_IDX` (0x0E2)
  the index (0-12 = register 0..C, 14 = loader hold b0, 15 = zero the sub-second counter and set LOADED); writing IDX commits. `AM_RTC_EN`
  (0x0E0) b0 = enable (read/write), AM_RTC_STAT (0x0E3 R) b0 = LOADED, a flip-flop that the bitstream load clears, so a reloaded core is
  loaded again without a flag in the app. The app loads in `am_release` when enabled and not LOADED. No state of the app is kept.
- **D7 time source:** `CoreIo::wall_time` (the board's real-time clock); without one, the fixed date of the ZX Next app 2026-01-01 plus uptime.
  The year is the two digits 00-99 (AmigaOS adds 2000 below 78).
- **D7a how the board's time gets there (2026-11-07):** the core app's clock (`rtc_dummy.cc`) is a software clock that only SNTP, the menu or an
  adopted hand-over sets; in the core app none did, so `get_fattime` read 1980 and `wall_time` was false (the board finding). The host (which has
  NTP, T164) writes its local time into the hand-over page behind `id_sum` (`time_sum`, `time_date`, `time_time`, 12 bytes at offset 388, outside
  `handover_sum`/`id_sum`: older readers stay valid, an older host's block carries none; the check is mixed with `sum` like the id's);
  `coreboot.cc` fills it when `rtc_time_valid()`, `coreapp.cc` calls `walltime_adopt()` right after copying the page and the clock runs on from
  FreeRTOS ticks. A large `.cor` (the A1200) is started through `legacy_stage` -> `cfw_handover_page`, not `coreboot.cc`: that path
  passes the time too since 2026-11-08 (board finding below); a plain `.cfw` still gets a wiped page.
- **D8 the setting is WHEN_RESET**, bound to AM_RTC_EN like the other machine settings; the label says "(at next reset)".
- **D9 (Alex, 2026-11-07: "A", translated):** T191 goes into the next tester release once the Vivado branch run is green; the board
  step S8 (Amiga Test Kit, Workbench `date`) is done by Alex before the release is published. The setting defaults to Off.

## §4 Out

- Writing the time back to the board (a `SetClock` on the Amiga does not change the board's clock; the Amiga clock resets to the board
  time at the next bitstream load). Decided with the issue: the board is the source.
- NTSC, other cores (only `amiga` and `amiga1200`); a second RTC chip (RF5C01A / Ricoh at $D80000 that Test Kit also probes) - not requested.
- 12-hour mode, the interrupt output (IRQ pulses), the alarm: not implemented; the registers read back, they do nothing.
- Battery backup across a power-off: the board's clock is the persistence.

## §5 Decisions taken while building

- **D3 changed: no `ENABLE_RTC` define.** The `ifdef` is gone from `gary.v`/`minimig.v`; `sel_rtc` is `rtc_en && addr[23:16]==DC` and `amiga_rtc` is always
  compiled, so none of the four build scripts needed a change (checked: no `ENABLE_RTC` outside the vendored RTL).
- **Benches that instantiate `nanomig` directly** (`tb_aga`, `tb_ideal`, `tb_mem`) now tie `rtc_en(1'b0)` and `rtc_ld(9'd0)`: an undriven enable is Z and made `sel_rtc` X.
- **Core app:** setting "Real-time clock (at next reset)" Off/On, `WHEN_RESET`, bound to `AM_RTC_EN` (0x0E0), in `coreapp/cores/amiga.cc`; `rtc_load()` runs in `am_release`
  when the setting is On and `AM_RTC_STAT` b0 (LOADED) is clear.
- **Measurements (native, Mac):**
  - `test_amiga500`: 78 checks, 0 failed; `test_desc`: 1512 checks, 0 failed. The goldens `menu_amiga.txt` / `menu_amiga1200.txt` regenerated with
    `UPDATE_GOLDEN=1` are byte-identical to the hand edit.
  - Controls `a500_rtc_unbound`, `a500_rtc_again`, `a500_rtc_nohold`, `a500_rtc_nodow` each fail as they must (report line `control a500_rtc_nodow: fails as it must (2 check(s) red)`).
  - Bench `amiga_rtc` (iverilog, local): PASS; both gateware controls (`AMIGA_RTC_MUT_RO`, `AMIGA_RTC_MUT_NOEN`) fail.


---
  - Builder, `tools/benches.sh amiga amiga1200 amiga_rtc` (report lines `amiga_rtc PASS 1s`, `amiga_l2 PASS`, `amiga1200_l2 PASS`, `amiga_cfg PASS`, `amiga_fuzz PASS`, `amiga_cpu020 PASS`, ...): all PASS.
    `amiga_equiv`, `amiga_mem`, `amiga_rate` first FAILED in 0-1 s (the Verilator source lists of `run_ideal.sh`, `run_mem.sh` lacked `amiga_rtc.v`; experimental, so not fatal in the lane);
    fixed in the run scripts, then `amiga_equiv PASS 70s`, `amiga_mem PASS 164s`, `amiga_rate PASS 264s`.
  - Builder, RISC-V build of host app and core app in `u64-build-env:ci`: both OK; `check_app_size`: coreapp.app 1,705,952 bytes, slot 1,966,068, 260,116 unused.
    `ci/apptests.sh coreapp`: PASS after `test_regset` learned that the boot writes seven registers (the clock's enable): `units (dyn): ran 246 (tests 19, controls 227), from the cache 1355`; `hostapp` and `loader` PASS.
  - Render bench (`tests/coreapp/render/run.sh`, new `rtc_menu`): `coreapp render: 261 checks, 1 failed`. **The failure is this change:** `MODES 480p canvas 640x480: 42 pages, 4 with an object outside or cut`
    (`BOUNDS 480p F3 tab 2..5 ... 95 objects, 3 outside`): the A1200 Settings page with its 14th row no longer fits the 640 x 480 canvas (1080p, 720p, 800x600, 576p: 0 outside). The same bench on the merge base: `254 checks, 0 failed`. Superseded by the merge of main below: that failure was the false positive T190 fixed in `tests/view_bounds.h`.
  - Merge of `main` (T190 mouse rows) into this branch: `amiga.cc` merged without a conflict in the rows (RTC row at index 7, Joystick ports 8, LED load, then the two mouse rows: 12 at most, `g_settings[2][12]` holds them); goldens regenerated from the real output are identical to the merged ones. Builder (`ci/apptests.sh hostapp coreapp loader`: all PASS, coreapp `ran 1613`, controls `scale_*` and `a500_rtc_*` fail as they must; `ci/trxapps.sh`: RISC-V build OK, coreapp.app 1,706,616 bytes, `coreapp render: 266 checks, 0 failed` at all modes incl. 480p, hostapp render PASS). T190's `view_bounds.h` used `lv_area_intersect`, which is private in the vendored LVGL: replaced by a plain overlap test.

UX review (ux-specialist, 2026-11-07, HEAD 58785f1f3): PASS for the Real-time clock row on A500 and A1200 (R1, R2, R4, R5, R6 on the
rendered Settings pages). Not shown by the bench: the last row of the A1200 list selected, and a 480p Settings page (the 480p picture is the
Summary tab); both are T187 S1 work. Noted for T187 D3: `amiga.cc:281` labels "Floppy write protect (next reset)", the other rows "(at next reset)".

Vivado branch run 1189 (90dd97b3, 2026-11-07), all 30 jobs green: "WNS mono amiga: 0.019 ns" (16m41s), "WNS mono amiga1200: 0.076 ns"
(24m48s); amiga_mono.bit 1729713 bytes, amiga1200_mono.bit 1751997 bytes. Board S8 (Test Kit, Workbench `date`) open, Alex.

Board 2026-11-07 (package main-e794f15d, A1200, Real-time clock On, reset): Workbench `date` prints "Thursday 01-Jan-26 00:02:07", the D7
fallback (2026-01-01 plus uptime): the clock is found, but `CoreIo::wall_time` (`coreapp/src/hw.cc:204`) returned false on the board. S8 open;
the S1/S6 benches use a fake CoreIo and never run this path.

### T191 S9 / board finding: built (2026-11-07, branch w1-t191)
Cause, from the code: `CoreIo::wall_time` (`hw.cc`) reads `get_fattime()` of `rtc_dummy.cc`, a software clock that SNTP, the menu or
`rtc_adopt_local_time` set; the core app calls none of them, so it read 1980 and the `year < 2020` gate returned false. Fix: D7a above
(`handover.h/.cc` `handover_set_time/handover_time`, `coreboot.cc` fills, `coreapp/src/walltime.h` + `coreapp.cc` adopt, `hw.cc` uses
`fattime_to_wall`). Bench `tests/hostapp/test_walltime.cc` (builder, `ci/apptests.sh hostapp`): with the time branch code removed
(= the old behaviour) the bench is red: `FAIL test_walltime.cc:36 wall(t)`; fixed: `PASS walltime`, `test wt_ok: PASS`; controls
`wt_clock_never_set` (5 red), `wt_time_unchecked` (11), `wt_time_sum_unmixed` (2), `wt_time_not_plausible` (4), `wt_unset_clock_taken` (2),
`wt_invalid_page_taken` (2) all "fails as it must". `ci/apptests.sh coreapp` (incl. S6 `test_amiga`, `test_handover` 38 checks) and `loader`
PASS. ue2emu S44 (`--handover`) has no time field: it would need `time=YYYY-MM-DD,HH:MM:SS` that writes the three words at page offset 388
with `time_sum` (hash in `handover.cc`), or the real host flow with a host clock set; not run. Board S8 (Workbench `date` shows the real
date, which now also needs a host with NTP time) stays Alex's.
S7 area: the package main-e794f15d carries no utilization report for the flat Amiga builds (only `*.timing.rpt`, `*.vivado.log.gz`, `*.bit`;
the logs have no per-module numbers; run 1189 WNS lines above). yosys `synth_xilinx -family xc7` of `amiga_rtc.v`: 248 LUT (6 LUT1, 50 LUT2,
19 LUT3, 36 LUT4, 86 LUT5, 51 LUT6), 97 FF, 20 CARRY4: within <= 250 / 120 (only 2 LUT of margin; Vivado will differ). The Vivado per-module
line needs `utilization_amiga_hier_synth.rpt` packaged or read from the builder's build dir.

### T191 S9 / board finding 2: the A1200 had no time (2026-11-08, package main-2bd67963)
Board (Alex, translated): Workbench `date` on the A1200 showed `Thursday 01-Jan-26 00:00:52`, the fallback. Cause in the code: the S9 fix
filled the time only in `coreboot.cc` (layer-2 cores); the A1200 is a large `.cor`, started by `legacy_stage`, whose page comes from
`cfw_handover_page` (`cfw.cc`), which never called `handover_set_time`; the spec had marked that path as read by no core app, wrongly (the
A1200 takes its id from it). Fix: `cfw_handover_page` takes the host's time (`legacy.cc` passes it when `rtc_time_valid`);
`tests/hostapp/test_corlarge.cc` checks no time without a clock and the exact time with one, read back through the map. Board S8 open again.
**Board S8 PASS (Alex, 2026-11-08, host 454d2ea1 .txu):** A1200 Workbench shell `date` = `Sunday 08-Nov-26 20:36:59`, the board's time.
