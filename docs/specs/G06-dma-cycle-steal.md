# G06 — App DMA into the C64's I/O area while the C64 runs (cycle steal)

**Status:** spec 2026-10-04; built on branch `g06-cycle-steal` (§9 as-built). **Task:** TODO.md G06
(M1, Alex 2026-10-04). **Source:** `docs/gap-audit-gideon-app.md` G06.
**Touches:** `ultimate_sys.vhd` (dma_bus decision, watchdog), `c64_core.v` (pause source, snap guard),
`cores/icepi/c64_top.v` (start condition of the DMA cycle). **Overlaps with:** T16 Q8 (memonly DMA
writes to $00/$01), §8.

## 1. Requirement

On Gideon's core an app access to $D000-$DFFF works while the C64 runs, without pausing it.
Measured on the board (2026-10-04, image 3.15 / core 1.50): REST `machine:readmem` at $D020/$D021
returns real values ($FE/$F6) and a write of $02 to $D020 reads back $F2, C64 running at READY.

The app relies on this for:

- boot hotkeys (Ctrl-P/N/B/S/F read from the CIA1 matrix at power-on, `u64_config.cc` ~965),
- the UNLOCK poke to $D038 (`unlock_irq`, ~1040),
- any REST `readmem`/`writemem` into I/O without a pause, `MENU_U64_POKE`.

Our core today: with `C64_STOP` = 0 and `C64_DMA_MEMONLY` = 0 the access is `dma_stub` (reads $FF,
writes dropped), `ultimate_sys.vhd` (`dma_stub`, the `r_dma` branch).

### Must

1. A read of any address in $D000-$DFFF while the C64 runs is one real bus cycle through the PLA
   (same decode as the stopped path: I/O, colour RAM, CHAR ROM or RAM under it per the CPU port,
   the cartridge's IO1/IO2 and ROM, Ultimax from `C64_MODE`), with the chips' read side effects
   (CIA ICR clear, TOD latch, VIC collision clear), as on Gideon's core.
2. A write to the area is one real bus cycle (VIC, CIA, SID, colour RAM, cartridge IO).
3. The C64 keeps running: the 6510 is held at one read cycle (RDY, as under the REU) for the
   stolen cycle and goes on; no instruction is lost or repeated, no CPU write cycle is overwritten,
   no CPU read gets the stolen byte. IRQ entry and stack sequences are not disturbed.
4. VIC BA/badlines respected: the cycle is taken only in a phi2 where BA is high.
5. Back-to-back requests work (one byte per call, the next as soon as the app asks).
6. Cartridges, REU, freeze/stop, T12 shadow sessions, Q8 behaviour unchanged. With the REU moving
   data the stolen byte waits for the transfer to end (§3.4).
7. T20: the stolen cycle is a non-CPU bus cycle on the state bus (`sb_cpu_bus` low, like every
   DMA cycle); the frame it falls in is marked as an inexact one (REU flag) and no snap is taken
   while it is pending.
8. Reads of $D400-$D7FF stay $FF (no SID chip on the C64U; the app's socket detection would take
   our internal SID for a socket). Writes there still reach the SID.

### Non-goals

- Interleaving with REU bytes (the steal waits, §3.4).
- Marking a stolen write in the ring's "external writes" flag (the REU flag is set instead).
- Non-I/O RAM access while running: stays the RAM's second port (no bus cycle, invisible), as today.
- `C64_DMA_MEMONLY` = 1: unchanged, RAM direct.

## 2. What exists (read from the code)

- `ultimate_sys` `r_dma` branch: hands a byte to `freeze_resp` through a toggle handshake (app
  clock to sys_clk) with `dma_bus` set only when `c64_stop` and not memonly; a bus byte becomes
  `cyc_req` in `freeze_resp` and `dma_req` in `c64_core`.
- `c64_core`: C64 on `gclk` (clk through a BUFGCE, CE = `gate_ce`; CE is 1 while the machine
  runs), so there is no further clock domain: `freeze_resp`, `c64_core` and the C64 all see `clk`.
  `ext_dma_req` -> `c64_top` `ext_dma_pend`; the cycle starts at phi2_p when `cpu_paused`, no REU
  DMA, BA high, no VIC stall; address/data on the REU's path (`ext_dma_cyc`), done at phi2_n.
- `cpu_paused` is `cpu_pause_req` latched at phi2_n of a non-write CPU cycle; `cpu_rdy` is low while
  it is set. When the machine is stopped, `held` (a DMA came) provides `cpu_pause_req`.
- While running nothing provided `cpu_pause_req` for a DMA byte: `ext_dma_pend` waited for ever and the
  app's DMA watchdog (2^23 clocks) acked $FF.

## 3. Design

The request FSM already exists in `c64_top` (`ext_dma_pend`/`ext_dma_cyc`); the cycle steal is three
small changes around it.

### 3.1 Request routing (`ultimate_sys.vhd`)

`dma_bus` is set for a D-page byte whenever memonly is off (running or stopped); for the other pages it
stays `c64_stop and not memonly`. The $D400-$D7FF read stub stays. The handshake is the existing
one: `dma_req_i` toggle, `dma_done_tgl` back, data stable until done.

### 3.2 Pause source (`c64_core.v`)

`cpu_pause_req || held || dma_busy` into `c64_top`. `dma_busy` is `ext_dma_pend` until the cycle's
phi2_n. Stopped machine: `held` already provides it (no change). Running machine: the request holds
the CPU at its next read cycle (NMOS 6510: RDY low does not stop write cycles, so the CPU runs on through a
write run such as an IRQ entry and halts at the first read).

### 3.3 Start condition (`c64_top.v`)

The cycle starts at phi2_p only when `!cpu_we`: the CPU is in a read cycle (halted by RDY or about to
be). `cpu_paused` alone is not enough: it is latched one cycle before RDY acts, so the next CPU cycle can still be
a write, and a DMA cycle in the same phi2 would overwrite it (the arbiter gives the bus to the DMA).
Applies to the stopped path too (a CPU whose next cycle is a write finishes it first, then halts); the
cost is at most a few cycles.

### 3.4 Waiting, REU, watchdog

Start also needs `!reu_dma_active` and `vic_ba`: a steal waits through a REU transfer (at most 64 KB,
roughly 65 ms at one byte per cycle) and through a badline (BA low about 40 cycles). The app's DMA watchdog
(`ultimate_sys` `dma_wd`) is raised from 2^23 to 2^25 clocks (335 ms) so a byte waiting for a long REU
transfer is not acked $FF; a machine that never answers still releases the app after that.

### 3.5 T20

`sb_cpu_bus` is already `!reu_dma_active && !ext_dma_cyc`: the stolen cycle is a non-CPU cycle on the state
bus. `c64_core` adds `steal_run = dma_busy && !halted_r && !held && !cpu_pause_req` (a steal and nothing else
holding the CPU): it is OR-ed into the engine's `reu_busy` (frame flagged) and blocks `snap`. A DMA while the
machine is stopped or CPU-paused does not set it (those paths are as before).

## 4. Behaviour

- Latency: a free cycle is at most a few CPU cycles after the request (the CPU reaches a read cycle);
  badline: up to the end of the BA window (about 43 cycles); REU transfer: until it ends.
- Cost to the machine: one cycle per byte (the CPU is held one read cycle), as the REU does.
- Reads with side effects are real (reading $DC0D clears the CIA's ICR and can swallow an IRQ the program
  was waiting for): same as Gideon's core.
- Reset: with the CPU held in reset the DMA is still served (informational in the bench).

## 5. Tests

`designs/c64_basic/sim/steal/` (Verilator, `build.sh` + `tb_steal.cpp`, c64_core level, like `sim/dma_io`):

1. READY, CPU running: $D020 reads $FE, $D021 $F6, write $02 reads back $F2 (Gideon's values), CIA1 $DC01 = $FF
   with no key, DDRB write and read back; the jiffy clock keeps counting.
2. Hammer: 4000 mixed reads/writes ($D020, $D012, $DC01, $DC03), the first 1000 back to back, then random gaps in
   dot-clock steps, while the CPU runs a loop with stack traffic (JSR/RTS/PHA/PLA) and the KERNAL IRQ; checks:
   every read value, loop invariants (counters never lose or repeat a step), CPU progress after each batch,
   some steals waited out a badline (> 20 cycles), none waited over 200 cycles, the CPU's own read of
   $D020 sees the written value.
3. REU: a loop stashes and fetches a page through a REU memory model all the time while steals go in: values,
   the REU data (stash and fetch) intact, the REU moved data meanwhile, the CPU runs on.
4. CPU in reset: informational.

Regression (all must pass unchanged): `sim/dma_io` (boot + freeze), `sim/pause`, `sim/statebus`,
`sim/ring` (`run_f4.sh`), `sim/slot`, `sim/cart_unit`, `ci/sims.txt`. Board: §9.

## 6. Open questions

- Q1: stolen writes do not set the ring's "external writes" flag; the REU flag is set. Enough for T20 (the
  frame is inexact either way)?
- Q2: the steal waits for a whole REU transfer instead of interleaving. Revisit if an app polls I/O
  during REU use.

## 7. Files

`ultimate_sys.vhd`, `c64_core.v`, `cores/icepi/c64_top.v`, `sim/steal/*`, this spec, TODO.md.

## 8. Overlap with T16 Q8 (branch t16-complete)

- `ultimate_sys.vhd`, the `r_dma` branch: both branches change the line `dma_bus <= c64_stop and not dma_memonly;`
  (Q8: an `if` for writes to $0000/$0001; G06: an `if` for the D page). Textual conflict in one hunk; the merge is
  `if D page then not memonly; elsif Q8 write to $00/$01 then c64_stop; else c64_stop and not memonly`. The header comment
  and `dma_wd` (26 bits, bit 25) are G06 only.
- `cores/icepi/c64_top.v`: no textual overlap (Q8 at the CPU instance, ~469/494; G06 at the DMA start condition, ~1649).
  Semantic: Q8's `dp_port_restore = cpu_paused && !cpu_pause_req && vic_phi2_n && !cpu_we` sees G06's pause source
  (`cpu_pause_req` into c64_top now includes `dma_busy`): a stolen byte ends with `dma_busy` low at its phi2_n, so the
  restore fires at the end of the hold as before. Q8's port write needs `ext_dma_cyc` with address $00/$01, which a running
  C64 never gets (steal is D page only).
- `c64_core.v`: no textual overlap (Q8: the `ext_cls`/`ext_ge` ports and `rom_on`; G06: the pause OR, `steal_run`, the snap term).

## 9. As built

Branch `g06-cycle-steal` (RTL commit a866235), CI run 403 (pipeline dispatched on the branch, commit 301984e), package
`trx64-ultimate/g06-cycle-steal-latest`.

**Changes** (as §3, plus):
- `ultimate_sys.vhd`: `dma_bus` for the D page = `not memonly`; `dma_wd` 26 bits, timeout at bit 25 (335 ms).
- `c64_core.v`: `cpu_pause_req || held || dma_busy`; `steal_run = dma_busy && !halted_r && !held && !cpu_pause_req`
  into `reu_busy` and `snap`.
- `c64_top.v`: `!cpu_we` in the DMA start condition (stopped path too).

**Mutation check.** Without `!cpu_we` the bench fails (15 loop-invariant breaks, the CPU stops: a CPU write in the same
phi2 is lost to the DMA byte). So the guard is required, not cosmetic.

**Simulation** (Verilator 5.053 + GHDL 7 from the OSS CAD Suite on the Mac; Debian's ghdl 2.0 / Verilator 5.006 build a model
that never leaves reset, so use the CAD suite; on macOS 27 its `libexec/ghdl` has a duplicated LC_RPATH that has to be
repaired with `install_name_tool`):
- `sim/steal` PASS: READY, `$D020` = $FE, `$D021` = $F6; write $02 reads back $F2 (Gideon's values); CIA1 `$DC01` = $FF,
  DDRB write/readback; 5576 hammer bytes (1000 back to back), mean wait 2.3 C64 cycles, max 47 (badline), 42 over 20 cycles;
  loop invariants and CPU progress hold; the CPU's own read of `$D020` sees the written value; with REU transfers (13257
  words moved meanwhile) 600 steals, mean 25.5, max 301.6 cycles (one REU page = 256+ cycles), REU stash and fetch data intact.
  The CPU held in reset still answers a stolen read (informational).
- Unchanged and passing: `sim/dma_io` (boot + freeze), `sim/statebus`, `sim/ring` (+ `run_f4.sh`), `sim/pause`, `sim/slot` (all
  T16 tests), `sim/cart_unit`, `sim/uci`. CI sims (tier 2 on the dispatch) passed.

**Build** (CI run 403, `c64_basic`, xc7a50t) against main-latest (9a42a01; main HEAD ea39883 only differs by a one-line
`top.v` change and sim files):

| | main-latest | g06 | delta |
|---|---|---|---|
| Slice LUTs | 27,644 (84.80 %) | 27,636 (84.77 %) | -8 |
| LUT as logic | 26,302 | 26,294 | -8 |
| Slice registers | 29,658 | 29,698 | +40 |
| Block RAM tiles / DSP | 62 / 13 | 62 / 13 | 0 |
| WNS / WHS | +0.010 / +0.051 ns | +0.014 / +0.045 ns | placement noise |

Timing met, no fix needed. The change is about three gates and a 26-bit instead of 24-bit watchdog counter; the LUT delta
is below the run-to-run noise of the placer.

**Board smoke** (C64 Ultimate, bitstream loaded to SRAM, `trxdbg boot staged` with the package's `ultimate.app`, speaker
disabled; C64 at the READY prompt, never paused):

```
readmem D020 len 2      -> FE F6
writemem D020=02        -> errors []
readmem D020            -> F2          (Gideon's core: F2)
readmem DC00 len 4      -> 7F FF FF 00 (CIA1 PRA PRB DDRA DDRB; DC01 alone FF)
writemem D020=0E, read  -> FE
readmem D41C            -> FF          (no SID: stub kept)
```
`trxdbg sb` three times: PC E5D4, E5D4, E5D1 (KERNAL cursor wait loop, moving), `$00 = 2F`, `$01 = 37`. After 100 rounds of
readmem `$D020` + writemem `$D021` + readmem `$DC00/1` the jiffy clock (`$A2`) still counted (`00 0D 0A` -> `00 0D 4A`)
and `$D020/1` read FE F6.

**Open** (also §6): Q1 (REU flag instead of an external-write flag for stolen writes), Q2 (no interleaving with REU bytes).
Not tested on the board: boot hotkeys at power-on (needs the app's `u64_config` path; the CIA1 read itself works), a
stolen byte during a long REU transfer (sim only), a real cartridge on the port.

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

T63: App DMA into I/O ($D000-$DFFF) while the C64 runs (G06)

Milestone M1, status ✅. Open (one line, as in the TODO table): –

Decisions: [ADR-0012 App DMA into I/O while the C64 runs is built in M1 (G06)](../adr/0012-app-dma-into-io-in-m1.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- **2026-10-09 late evening, T63 done:** power on with C= + N on the merged state (flashed package t63-oneshot-2574f05f): the menu (F2, Video Configuration) shows "System Mode  NTSC (boot key)", REST current "PAL" / effective "NTSC". Merged into integ-m1 (b084885, app 7c1c3fdb).
- **2026-10-09 late evening, T63 one-shot boot hotkey passed on the board (Claude, Alex power-cycled):** package t63-oneshot-2574f05f (bitstream + app) flashed; saved config before: System Mode PAL. Power on with C= + N held: REST shows System Mode current "PAL", effective "NTSC". Power on without a key: current "PAL", no effective override. The hotkey value is no longer saved. Same run: with the EF1 in the port, REU and Command Interface show current "Enabled", effective "Disabled (cartridge)" (T72).
- **2026-10-09, built on branch (not board-tested): boot hotkey is a one-shot (T63).** App `t63-oneshot-app` 53ff64de, superproject `t63-oneshot`. Generic runtime value on `ConfigItem` instead of `set_value`: `ConfigStore::set_oneshot` / `ConfigItem::setOneShot` put the hotkey value into the effective `value` (hooks, `get_value`, `effectuate_registered_settings` see it) and keep the saved one in `oneshot_saved`; no flash-write flag is set. `pack` (flash page), the `.cfg` writer and the REST `current` use `getSavedValue()`, so a later save of any other item writes the saved mode, never the hotkey's. The detail query adds `effective` while a one-shot is active; the menu appends ` (boot key)` to the value when it fits the row. A real edit (menu, REST PUT, `.cfg` load, flash reload) makes the new value the saved one and ends the one-shot. `u64_config.cc` boot hotkey (CTRL-P/N = System Mode, CTRL-B/S/F = C64U Model) calls `set_oneshot`; applied on Gideon's bitstream too (same app path, no bitstream dependence; his behaviour changes only in that the key no longer persists). Host test `target/pc/linux/configiotest` (debian container): 48 checks, 0 failed, covers effective value applied, saved value kept, flash image and `.cfg` hold the saved value, a second one-shot keeps the first saved value, another item changed and saved keeps the one-shot unsaved, an edit of the item itself saves it. Board items in docs/archive/board-tests/board-tests-pending.md (T63-HK1).
- **2026-10-09, decided (Alex): a boot hotkey is a one-time shot: it sets System Mode / C64U Model for this power-on only, the saved config stays and applies again at the next power-on. Found: C= + N at power-on left System Mode NTSC saved (later boots came up NTSC). To fix (app, `u64_config.cc` boot hotkey: runtime value without saving).**
- **2026-10-09, board (Alex): power-on hotkey PASSED.** Our bitstream + app flashed (t16-r21-dbg-5d097fe5 = integ-m1 + R21 bit, app ff56dedb), real power cycle with C= + N held: the app came up with System Mode NTSC (flash setting PAL); the code reads the key matrix without checking the modifier (`u64_config.cc` boot hotkey, 'ctrl-n' = key code 0x0E), so C= works as well as CTRL. Set back to PAL over REST (runtime only).
- **Board 2026-10-08 morning (Claude alone, CI run 514): T63-DMA and T63-REU passed.** I/O DMA while running: writemem D020=02 reads F2, DC00 = 7F FF FF 00, jiffy clock runs, 100/100 write/read loop. Three REU round trips (run_reu_test.sh) correct while 1199 REST readmem $D000 ran in parallel, no error, no hang; its "DDR2 holds it" check fails only because reu_test.py still looks at 0x2008000 (REU at 0x1000000 since T60).
- **Board 2026-10-07 evening (Alex, Freeze UI; CI run 514, package integ-m1-e2b2ca47, app 301ef8c3, trxmon.u2a built at app 9a1b996b = same trxmon source as integ-m1-app): PASSED (T63-HK)** boot hotkeys read at app init: CTRL-N gives System Mode NTSC, CTRL-P gives PAL (app restarted via trxdbg boot staged while the key was held). CTRL-B/S/F set "C64U Model", which only led_strip.cc/bling_board.cc read; the light effects are removed (OPT-bling), so not relevant and not tested. The power-on (flash boot) path and T63-DMA / T63-REU were not run.
- Task text: App DMA into I/O ($D000-$DFFF) while the C64 runs, as on Gideon's core (gap audit G06; Alex 2026-10-04: build in M1, wants real LUT/timing numbers): boot hotkeys (CIA1), UNLOCK $D038 poke, REST readmem/writemem to I/O without a pause. Cycle steal: the pending byte holds the 6510 at a read cycle (RDY, REU's way), the cycle starts in a free phi2 (BA high, no REU DMA, CPU read) and is a DMA cycle on the state bus. Spec: docs/specs/G06-dma-cycle-steal.md. Built and board-smoked 2026-10-04 (branch g06-cycle-steal): sim `sim/steal` PASS (REU, badlines, back to back) and all regressions pass; CI run 403: c64_basic 27,636 LUT (-8 vs main-latest 27,644), 29,698 FF (+40), WNS +0.014 ns (main +0.010); board: `readmem $D020` = FE (real), write $02 reads F2, CIA1 $DC01 readable, C64 keeps running. Details: spec §9.
- **In main 2026-10-06 (ba060ae, merged after t16-complete; the Q8 overlap in ultimate_sys.vhd is resolved: $D page = bus cycle unless MEMONLY, $00/$01 write = c64_stop; dbg_stop is gone with SLIM). With T29: a stolen byte makes the turbo engine fall back to normal speed for the cycle (cpu_pause_req includes dma_busy -> engine inhibit); not simulated together. Open: boot hotkeys at power-on on the board, REU-interleave (Q2); Alex's 2026-10-06 test did not report DMA into I/O.**
