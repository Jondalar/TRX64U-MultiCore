# SLIM: one state path, a lean write log (2026-10-05)

Branch `slim-core`. Goal: the TRX64 part of the core (monitor, freeze, Timewarp: 4,394 LUT on main 448fc36)
shrinks to what the three functions need: the write log into the ring, one way to hold the machine at an
exact point and read/restore its state, the break comparator. Everything else either goes or moves to the app.

Alex, 2026-10-05: "Ich finde das viel zu gross ... Im Grunde müsste jede Operation des C64 einfach nur per
shadow in den Ring und dann da elegant weiterverarbeitet werden."

## 1. Where the LUTs are (measured)

Method: each module synthesized alone with yosys `synth_xilinx` (oss-cad-suite 0.69+190), once whole and once
per function with that function tied off; the difference is the function's share, scaled to the module's
Vivado total (`utilization_hier.rpt`, main 448fc36). About +/-20 %. Scripts: `tools/slim/ablate_*.py` (to add).

`trx_engine` (1,272):

| function | ~LUT |
|---|---:|
| logger core: RAM tap, 6 holding registers, 67-bit FIFO, drain into 3 rings with programmable base/mask, frame markers, counters, status handshake | 710 |
| APPLY (undo/redo in hardware, 128-bit FIFO) | 265 |
| LOG_PUT and debug records (stream 1 from software/observer) | 90 |
| COPY/FILL mover | 85 |
| colour RAM taps | 80 |
| cartridge RAM tap + old-byte queue | 40 |
| chip-block capture sequencer | 30 |
| outside-writes tap | 25 |

`freeze_resp` (1,084):

| function | ~LUT |
|---|---:|
| session responder: 128 shadow registers, VIC/CIA reads rebuilt from the state bus, PLA copy, DMA window, cartridge peek | 685 |
| commit list | 165 |
| statistics counters (Vivado: opt-frz's -144 with the list in BRAM) | 145 |
| CIA 1 keyboard matrix rebuilt | 30 |
| lens unit | 20 |
| reset commit, sidefx | ~0 |

## 2. Decisions

| # | Question | Decision |
|---|---|---|
| D1 | Keep the session responder so Gideon's freeze code may write VIC/CIA/SID while frozen? | **No (Alex 2026-10-05).** The responder and the commit list go; the app is changed so its freeze touches no C64 register. "Gideons Menü fliegt doch eh" (M2, T24 LVGL UI). |
| D2 | APPLY in hardware or in the app? | **Hardware for now (Alex 2026-10-05).** Stays as built (~265). |
| D3 | Statistics counters (sessions, accesses, real cycles, gclk, unmapped, list dropped/last, `cnt_dma`)? | **Out, all of them (Alex 2026-10-05).** Diagnosis only (JTAG, benches); simulation and trxdbg cover it. Their register addresses read 0. |
| D4 | Ring areas programmable (LOG_BASE/LOG_MASK) or fixed? | **Fixed (Alex 2026-10-05)** at the values the app always sets (`trx_ring.cc:99`): stream 0 16 MB at 0x7000000, stream 1 8 MB at 0x6000000, stream 2 4 MB at 0x5C00000. The registers go; the app stops writing them. |
| D5 | COPY/FILL | **Stays.** The app reads the ring through it because the CPU cannot address DDR2 above 64 MB (T20 §9.2, §12 mover; the ring is at 0x5C00000-0x7FFFFFF). Not a choice. |
| D6 | Stream 1 records from software (LOG_PUT) and from the debug block | **LOG_PUT goes** (no caller in the app: `trx_ring.cc:40`, cartridge flash content is not restored, A08.3). **The debug block's records stay**: the observer action LOG of the monitor (T21 §3, unit CTRL b6:5). |

## 3. What D1 means

Goes from `freeze_resp`: the shadow registers (`ov`, `ov_valid`), the VIC/CIA read rebuild, the PLA copy, the
CIA 1 matrix rebuild, the commit list and the reset commit, `vic_view` from the overlay, the sidefx path.

Stays (moved to `dma_win.v`, §6): the app's DMA window (RAM / ROM block through the second port at any time;
a real bus cycle with the machine stopped, which only an explicit access asks for), the lens unit's cartridge
peek and REAL cycle (T21 §3.4.1). The stop itself needs no `hold`: `hold` was the commit's, there is none, and
the machine is held by the gate alone (a bus cycle pulses the gate for its one cycle, c64_core's DMA path of
T12 B). **The rule that binds all of this (Alex, via the coordinator 2026-10-05): the app's own freeze makes
zero bus cycles.** A freeze/stop holds the machine at the exact point and the resume is bit-identical to a
free run, so the colour RAM got its second-port aperture back in the window (§6.1) and the app uses
C64_DMA_MEMONLY for RAM. A bus cycle exists only for an explicit access that asks for the PLA's view
(REST `machine:readmem`/`writemem` of $1000-$CFFF and $E000-$FFFF, i.e. what dma_transfer_frozen does,
without a lens). **Hazard of such a first cycle:** the first bus cycle of a pause lets the CPU run one or two
cycles before it comes (c64_top's `cpu_paused` follows the hold at the next phi2 fall); with Ultimax on, the
program would fetch its operands from an Ultimax map (the bench saw INC $C0F0 become INC $C000). Such a
cycle is made with Ultimax off, and it moves the machine by a few cycles: an explicit access is not
exactness-neutral, the app's freeze is.

App (submodule, branch `slim-core-app`):
- `C64::freeze()` / `unfreeze()` on the TRX64 bitstream: stop, `trx_video_freeze_enter()`, menu on the pixel
  layer; no `backup_io()`, `init_io()`, `restore_io()`. Every other caller of these three is listed and handled.
- the menu's keyboard in a freeze: from the matrix register (`app_input` / `ovl_own`), never a CIA scan. To
  check against the code first: how the menu reads keys today on our core.
- the C64-screen menu (`c64_text.v`): its VIC view comes from an app register instead of the responder's
  shadow (`vic_view`).
- peek/poke of I/O while frozen (REST, monitor): a real bus cycle, i.e. it changes the chip, as on a real C64
  with a cartridge freezer. The monitor's side-effect-free reads go through the lens (state bus), as today.

## 4. Open questions (one at a time, with Alex)

None left for the first build.

## 5. Tests

dbgsess and ultsys benches adapted (no responder); Lorenz subset; freeze menu on HDMI and on the C64 screen;
monitor golden test (T21); Timewarp A/B (tests/t20_apply.py) on the board.

The simulation part is built and run, see §6.3; what needs the board is §6.5.

## 6. As built (2026-10-05, branch `slim-core`, app `slim-core-app`)

### 6.1 Hardware

- `freeze_resp.v` is `dma_win.v` (module `dma_win`, instance `win` in top.v): the app's DMA window (RAM and ROM
  block through the second port, a bus cycle in c64_core when ultimate_sys says the machine is stopped) and the
  lens unit (cartridge look through pk_*/pkd_*, REAL cycle with cyc_force). Its ports: the window handshake,
  the RAM second port, the colour RAM second port (below), the bus cycle, `halted`, `app_ultimax`/`core_ultimax`,
  the lens. Gone: overlay, VIC/CIA rebuild, PLA copy, keyboard rebuild, commit list, reset commit, sidefx,
  `vic_view`, `hold`, `sess_*`, all counters (`cnt_dma` included; JTAG register 14 reads `{0, cnt_crs}`).
- What t21-p4 had put into freeze_resp for the debugger (`dbg_stop`, `dbg_hold`, `dbg_rel`, `dbg_own`, `block`,
  `sess_by_dbg`, `sess_busy`) was all session logic: none of it is left in dma_win. What stays of t21-p4: the
  D-page condition in ultimate_sys (`dbg_stop` opens the window's D page as a bus cycle), `dbg_own` masking
  C64_STOP in top.v, trx_dbg's hold/stop/rel. `sess_busy` is gone from trx_dbg and c64_core: RUN and steps
  are taken in the next clock. `dbg_rel` is still an output of trx_dbg/c64_core, nothing reads it.
- `trx_sys.v`: page 5 `0x000-0x01C` (SH_CTRL .. SH_COMMIT) read 0 and ignore writes, the overlay window
  `0x080-0x10F` reads 0, LK_REQ/LK_RES as before, new **TX_VIEW at 0x5028** (RW, 16 bit): what the C64 text
  layer takes for the VIC: b15 $D011 ECM, b14 BMM, b13 $D016 MCM, b12:6 $D018 bits 7:1, b5:2 $D021, b1:0 the
  CIA 2 bank pins (PRA | ~DDRA). Reset 0x0243 = the menu (text mode, screen $0400, charset $0800, background 0,
  bank 0). It is in the C64 clock like c64_text's fetch, no synchroniser.
- **Colour RAM aperture (no bus cycle):** with `dma_rom` and `dma_addr` 0x6000 + offset the window goes to the
  colour RAM's second port (4 bits, a read gives $Fx). ultimate_sys: **0x18F000-0x18FFFF** (mirrored; the char
  ROM mirror that was there is gone), CPU address 0xA008F000 (`U64_COLORRAM_BASE`). It is arbitrated with
  c64_text as the old responder was: dma_win's `cram_use` clock first, c64_text reads in the others
  (`cram_grant`). With C64_DMA_MEMONLY (RAM, no bus cycle) the app reaches everything its menu needs without
  a machine cycle. The ring's outside colour RAM tap (`wd_*`, record bit 63 set) is back in `trx_engine.v`,
  fed from this port as before, so Timewarp sees every colour RAM change made by the app.
- `c64_text.v`: `vic_view` is TX_VIEW; the `sess` port is gone, replaced by **`halted`**: the layer shows only
  while TX_CTRL b0, the machine is paused and TX_VIEW says standard text. Whatever ends a freeze (a debugger
  RUN with the menu open, an app that crashed) the text goes off when the machine runs. TX_CTRL reads
  b31:24 = **2** (1 = the layer with the session responder).
- `trx_io.v`: CAPS2 = 0xB7 (b3, the session, reads 0).
- `trx_engine.v` (D4, D6): the four areas are constants, LOG_BASE/LOG_MASK (0x020-0x044) ignore writes and read
  0, LOG_PUT (0x050/0x054) ignores writes; the debug block's records (`dbg_put`/`dbg_rec`) keep their holding
  register. The `wd_*` tap stays (see the aperture).
- `ultimate_sys.vhd`: **KEYB_JOY** (U64_IO_BASE + 0x06) was a constant $FF; now b0 written selects the port the
  line reads (0 port 2, 1 port 1), a read gives its five lines active low (`111` & not joy), from a new input
  `joys` (top.v: `pins_s2[9:0]` synchronised into the CPU clock). KEYB_COL/KEYB_ROW were there.
- A hazard the bench found (dma_win.v header, §3): the first bus cycle of a pause lets the CPU run one or two
  cycles first. With Ultimax on, the program would fetch its operands from an Ultimax map (INC $C0F0 became
  INC $C000). The app's freeze makes no bus cycle at all and no longer sets Ultimax; an explicit access makes
  its first cycle with Ultimax off. Sidefx commit cycles of the old design had the same exposure.

Yosys `synth_xilinx -flatten` (tools/slim/count.py; the numbers of §1 are Vivado's, these are about +-20 %):

| module | main 448fc36 | t21-p4 (slim-core base) | slim-core |
|---|---:|---:|---:|
| freeze_resp / dma_win | 1264 LUT, 810 FF, 26 RAM64M | 1275 LUT, 811 FF, 26 RAM64M | 127 LUT, 131 FF |
| trx_engine | 1556 LUT, 1943 FF, 3 RAMB36 | 1837 LUT, 2410 FF, 4 RAMB36 | 1687 LUT, 2236 FF, 4 RAMB36 |
| trx_sys | | 666 LUT, 370 FF | 442 LUT, 347 FF |
| c64_text | | 212 LUT, 234 FF | 211 LUT, 234 FF |

(Vivado's number for the whole c64_basic comes from the CI bitstream run, not measured here.)

### 6.2 App (software/ultimate-app, branch `slim-core-app` on f2a26eae)

- `trx_io_slim()` (trx_io.cc): TX_CTRL b31:24 >= 2. `trx_video_slim_active()`: slim and the pixel layer took
  the freeze (HDMI attached, buffers there).
- `C64::freeze()` with the layer up on a slim core: `freeze_slim()`: plain machine pause (not `stop(true)`,
  which reads and writes the VIC to find a bad line and whose `resume()` writes the raster back), wait for the
  gate, `C64_DMA_MEMONLY = 1` (RAM through the second port), then save $0800-$0FFF, the screen and the colour
  RAM (through the aperture), copy the charset. **No bus cycle, no Ultimax, no chip touched.** What the monitor
  and the snapshot want (`vic_backup`, CIA 2 DDRA/PRA for the VIC bank) is read over the state bus
  (`trx_chip_words`, no side effect); while an app owns the state bus window (trxmon serving) they stay 0 and
  bank 0. `unfreeze()`/destructor/Timewarp/start_cartridge: `restore_slim()`/`slim_memonly_end()`: text layer
  off, memory back, MEMONLY back. A VIC bank the monitor set while frozen is written at the end (an
  explicit bus cycle). The screen object of a slim core is made with the colour RAM aperture. Without HDMI (or on
  a core without the layer) the old Gideon path runs as before. Callers of `backup_io`/`init_io`/`restore_io`:
  `freeze()`, `unfreeze()`, `~C64()`; each handled.
- Keyboard: the C64-screen menu used the frozen CIA through `Keyboard_C64(this, &CIA1_DPB, &CIA1_DPA,
  &CIA1_DPA)` (init_io set CIA 1 DDRA/DDRB; the responder rebuilt PRA/PRB from the physical matrix and
  joysticks), the HDMI overlay menu `U64II_KEYB_ROW/COL/JOY`. On the slim core the C64 menu gets the overlay's
  registers (same scan code, which already has the second-joystick logic for them): no CIA is involved. The
  Timewarp scrub's `scrub_keyboard_io()` (CIA 1 DDR writes) does nothing there.
- `C64::set_colors` on a slim freeze sets TX_VIEW's background (the layer's $D021 view) and the colour the still
  is dimmed towards, no VIC register. `set_frozen_cia2_porta` (monitor VIC bank) writes the pin at once.
- trx_ring.cc: no LOG_BASE/LOG_MASK writes; no app code used LOG_PUT. trx_video.cc: the freeze is the machine
  pause (GATE_CTRL halted) where there is no SH_STATUS; TX_VIEW is written at `menu_on()`.
- Build: `make -C target/u64ii/riscv/ultimate` in u64-build-env (amd64, Apple container, lwip and httpd
  mounted from the main checkout) builds, `app_space.py measure u64e2_50t`: 1453.9 KiB of 1920 KiB.

### 6.3 Benches (all run on the Mac, niced)

dbgsess 214 checks PASS (resume bit-identical with the menu drawn in every stop); ultsys 36 checks PASS (KEYB_COL/ROW/JOY, the freeze with no machine cycle); statebus ok (test 4: zero bus cycles, zero clock edges, 5 frames on equal the reference; with STATEBUS_BUSCOLOR=1, the previous app, it fails 9 checks); trx_io PASS;
ring_f4, ring_trace PASS; apply PASS; slot 8 of 8 PASS; ring: boot, trace, run, log, snap, restore PASS; cart
"all passed"; dbg (T21 debug block) PASS, 0 failures; input PASS (tb_input and tb_input_core, both instantiate
the changed ultimate_sys). Not run: usb (tb_usb.vhd only got the new `joys` port connected), pixlayer/still60
(no c64_text/trx_sys/engine in them), Lorenz. Changed: dbgsess/ultsys/statebus tests of the responder (session start,
overlay reads, commit list, SIDEFX, abort/discard, SH_MODE, counters, the keyboard rebuild, commit on reset)
are removed or replaced by their opposites (no session, SH_STATUS 0, the window's reads are bus cycles that
cost a cycle and clear flags, writes reach the chip at once, the state bus and the lens stay free of side
effects, resume stays bit-identical with the monitor's looks in 14 stops and a step chain); the cartridge look
in dbgsess goes through the lens (a real cycle reads through the cartridge cache, which is not a look).
tb_ring: the area registers and LOG_PUT are written and must change nothing; no wrap test is left.
`ring/gen_top.py` was already out of step with tb_sb_top.v before this change (an assertion fails on HEAD);
tb_ring_top.v is edited by hand.

### 6.4 What a user notices

- A freeze, the menu and the resume cost the machine nothing: zero bus cycles, zero clock edges, every chip
  word as it was, and 5 frames after the resume equal a run without the freeze (statebus test 4, dbgsess run 3).
- Explicit reads through the window while frozen (REST `machine:readmem`/`writemem` without `lens=`, a monitor
  peek/poke of ROM- or RAM-visible memory above $1000 / at $E000+): bus cycles with the PLA of the frozen $01,
  as a freezer cartridge's would be; they move the machine by a few cycles and the first one by one or two CPU
  cycles (§3). Reads of $D000-$DFFF and $0000-$0FFF stay what they were: the RAM under the I/O and the saved
  menu areas, never the chips. The TRX64 monitor reads over the state bus and the lens and is not affected.
- The menu text goes off whenever the machine runs (a debugger RUN with the menu open): the layer follows
  `halted`. The still stays as before.
- On the C64-screen menu the keyboard and the joystick come from the hardware matrix; on the slim core the
  joystick of the swap setting reaches the overlay's line too (KEYB_JOY was $FF before).
- Statistics: sessions, accesses, real cycles, gclk, unmapped, commit list, `cnt_dma`: gone (read 0).

### 6.5 Not done here / open

- Board: Lorenz subset, the freeze menu on HDMI and on the C64 screen (keyboard, joystick, colours), monitor
  golden test, Timewarp A/B (`tests/t20_apply.py`), `tests/freeze_raster` (its cycle exactness cannot hold
  any more; it now skips the session checks on a slim core and shows what remains), a real cartridge on the
  port in a freeze.
- Vivado: no run here; LUT/FF are yosys numbers. Timing of the TX_VIEW path and of KEYB_JOY (cpu_clk) is untested.
- Stale elsewhere: ARCH-lean-state-access.md, T16, T29, T38-T39, T40 mention `freeze_resp` in history
  paragraphs; T12/T21 carry a banner.

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

SLIM: Slim state path (replaces the freeze session)

Milestone M1, status ✅. Open (one line, as in the TODO table): freeze_raster and monitor golden not individually reported

Decisions: [ADR-0024 SLIM: one state path, a lean write log (replaces the freeze session)](../adr/0024-slim-state-path.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- **Board 2026-10-08 morning (Claude alone, CI run 514): `tests/freeze_raster` 10 freezes:** IRQ records checked, picture 0 pixels differ, splits unchanged in every freeze. The script still reports FAIL "no freeze session": its slim detection reads TX_CTRL (JTAG trx_read 0x60C0) as 0, while c64_text.v:98 returns 2 in b31:24, so it checks the removed freeze session. Script issue, not the core. Monitor golden: not run.
- Task text: Slim state path (docs/specs/SLIM-state-path.md, decisions D1-D6 by Alex 2026-10-05; replaces the shadow-capture/freeze-session plan of docs/specs/ARCH-lean-state-access.md for the freeze): `freeze_resp.v` -> `dma_win.v` (the app's DMA window and the lens unit; 109 LUT/128 FF by yosys against 1,275/811), shadow I/O session, commit list, statistics counters, LOG_PUT and configurable ring placement removed, ring areas fixed, TX_VIEW (page 5 0x5028) feeds the C64 text layer. The freeze is a plain machine pause with zero machine cycles; RAM via MEMONLY, colour RAM via a second-port aperture at ultimate_sys 0x18F000, chips over the state bus; the menu scans KEYB_COL/ROW/JOY (KEYB_JOY implemented in `ultimate_sys`).
- **In main 2026-10-06 (ba060ae; with t21-p4; CI run 438: 25,974 LUT, WNS +0.132). Board 2026-10-06 (Alex): freeze/SLIM incl. joystick in the freeze menu passes, Timewarp scrub/rewind/RUN-STOP passes.** Not individually reported in that test: tests/freeze_raster, monitor golden; ring bench `snap` failed (row T20; fixed 2026-10-08, a bench bug).
