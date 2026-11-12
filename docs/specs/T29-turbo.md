# T29 — Turbo: a C64 CPU faster than 1 MHz

**Status:** spec 2026-10-03, revised the same day against Xander Mol's manual and programs and the TRX64 code (§3.3); Q1-Q10 decided, Q11-Q15 answered by measurement (§10a/§10b) or decided in §12. **Build started 2026-10-04 (branch t29-turbo); §12 is the as-built design and wins over §3-§7 where they differ.** **Task:** TODO.md T29 (🔨). **Milestone:** 1 (drop-in
replacement for Gideon's core plus the TRX64 functions).
**Shares with:** T20 (write log, snap, state bus), T21 (cycle counters, stops, watch units),
T28 (UCI bus events), T12 (pause, freeze responder), ARCH (`docs/specs/ARCH-lean-state-access.md`).
**Behaviour references, in this order where they conflict (§3.3):** (1) Xander Mol's
real-hardware statements and programs (`/path/to/Development/u64-emulator/run/xander/`,
`TURBOCONTROLMANUAL.md`); (2) TRX64 Specs 815, 851, 856, 868 and BUG-061
(`/path/to/Development/C64/Tools/TRX64/docs/_archive/`; code in
`crates/trx64-core/src/c64_6510core.rs`, `vic.rs`, `lib.rs`), copied wherever the hardware
allows; (3) Gideon's manual. A measurement beats all three.
**Comparison machine:** Alex's C64U running Gideon's firmware and bitstream from flash (§8.4).
**Test material:** Xander Mol's Ultimate projects in `/path/to/Development/u64-emulator/run/xander/`
— `mandelbrot-upic` (64 MHz, one `$D020` store per pixel, `$D012` row sync), `UltimateDemo2026`
(64 MHz demo, `TURBOCONTROLMANUAL.md`), `heartbeat-demo` (turbo + CIA1 timer A tempo + 8 SIDs +
16 MB REU), `GeoUTools` and `UBoot64-v2` (UCI, CIA TOD waits).

Line numbers: app files are in `software/ultimate-app` at the pinned commit 6836bc3a; hardware
files at `main` c198c80.

---

## 1. Goal and non-goals

### Goal

Milestone 1: a drop-in replacement of Gideon's U64 / C64U turbo. The app's **Turbo Settings** (Turbo Control, CPU Speed, Badline Timing, SuperCPU Detect) and the
C64-side turbo registers (`$D030`, `$D031`, `$D07A/$D07B`, `$D0BC`) behave as on Gideon's
U64 / C64 Ultimate. With turbo on, the 6510 runs up to 64 CPU cycles per C64 (PHI2) cycle on internal RAM and ROM,
on its own turbo registers, on the VIC's colour registers (one store per pixel, UPic) and on the
raster registers. Every other access is a real C64 bus cycle at normal speed, with the C64's
BA/AEC rules. **Time stays real time:** PHI2, the VIC, the CIA timers and TOD, the SID and the
drive run at their normal rates whatever the CPU does (§5.0). The app stays unchanged.

At 1 MHz (Turbo Control "Off", or a speed index of 0) the machine is the machine of today,
bit for bit: same CPU clock enable, same bus cycles, Lorenz 297/297.

### Non-goals

- Sub-PHI2 access to the CIAs, the SIDs, colour RAM and the VIC registers other than the colour
  and raster ones. They stay one bus cycle each in milestone 1 (§5.3). Whether Gideon's core makes
  them faster is measured on the C64U (§8.4, Q12); TRX64 assumes it does (851 D3, unmeasured).
- Fast cartridge ROM. ROML/ROMH reads stay bus cycles (phase C in §9 looks at cache hits).
- Timewarp in turbo. The T20 ring records at 1 MHz only (Q7, §6.1).
- A 65816 or a SuperCPU memory map. `$D0BC` only answers the detection probe, as in TRX64 851 §7.
- Making fastloaders or cycle-counted code work in turbo. They break as they break on a U64 (§6.7).
- NTSC. The bitstream is PAL only (`c64_core.v` `FRAME`).

---

## 2. What the app drives (read from the code)

| Item | Code | What it does |
|---|---|---|
| Register base | `software/system/u64.h:49` | `C64_IO_BASE = U2P_IO_BASE + 0x80000` = 0x10180000, our page "011" (`designs/c64_basic/ultimate_sys.vhd:775`) |
| `C64_TURBOREGS_EN` | `u64.h:106` | +0x02, the enable word |
| `C64_SPEED_PREFER` | `u64.h:134` | +0x2D, the preferred speed |
| `C64_SPEED_UPDATE` | `u64.h:135` | +0x2E, written 1 after the other two: the apply strobe |
| Menu items | `software/u64/u64_config.cc:393-400` | "Turbo Control" (0-3), "CPU Speed" (0-15), "Badline Timing" (default Enabled), "SuperCPU Detect (D0BC)" (default Disabled) |
| Turbo Control values | `u64_config.cc:330-331` | Off, Manual, U64 Turbo Registers, TurboEnable Bit → enable word `$00, $00, $01, $05` |
| Speed tables | `u64_config.cc:326-327` | U64: 1 2 3 4 5 6 8 10 12 14 16 20 24 32 40 48; U64-II/C64U: 1 2 3 4 6 8 10 12 14 16 20 24 32 40 48 64. Our app builds with `-DU64=2` (`target/u64ii/riscv/ultimate/Makefile`), so the second table, `u64_config.cc:394-395` |
| `setCpuSpeed` | `u64_config.cc:1630-1665` | Off: `PREFER = $80`, `TURBOREGS_EN = $00`. Else `PREFER = speed | BADLINES_EN << 7`, `TURBOREGS_EN = table[mode] + (SCPU ? $02 : 0)`. Then `UPDATE = 1`. Change hooks on all four items (`:937-940`); also called at every `effectuate_settings` (`:1106`) |
| Badline bit sense | `components/config.cc:937`, `u64_config.cc:1657` | `en_dis` = {"Disabled", "Enabled"}: **bit 7 of PREFER = 1 means badline timing on**. "Off" writes `$80` = 1 MHz with badline timing |
| Freeze backup | T12 §2 (`c64.cc:894`, `:1039`) | `backup_io` reads `$D000-$D030` (49 bytes), `restore_io` writes them back: `$D030` is read and written by the menu, `$D031` is not touched |

No other app code touches turbo (`grep turbo|SPEED|BADLINE` over `software/`: the modem's "turbo
enable bit" is the Turbo232 ACIA, unrelated). Today our page "011" decodes none of 0x02, 0x2D,
0x2E on write (`ultimate_sys.vhd:940-957`): the settings are dropped.

**Decision A1.** Page "011" gets three write registers with Gideon's offsets: 0x02
`TURBOREGS_EN` (b0 `$D031`, b1 `$D0BC`, b2 `$D030`; TRX64 `vic.rs` `u64_regs_en`), 0x2D
`SPEED_PREFER` (b3:0 index, b7 badline timing on), 0x2E `SPEED_UPDATE`. They are held in the app's
clock; a write of 0x2E toggles a flag that carries both values into the C64 clock (the
`cart_type`/`cart_kill_tgl` pattern, `ultimate_sys.vhd:925-932`). Reads of 0x02/0x2D return the
last written value (the app does not read them; cheap to have). No app change.

---

## 3. The C64-side registers

### 3.1 What is known, and from where

| Fact | Source |
|---|---|
| `$D031` R/W only with enable bit 0, else reads `$FF`. Bits 3:0 speed index, **bit 7 = 1 suppresses the badline CPU stalls** (0 = normal stalls). | Xander Mol, `TURBOCONTROLMANUAL.md` §4 (`TURBO_BADLINES_OFF = $80`, `TURBO_FULL = $8F`; library used on real U64/U64-II/C64U); Gideon's docs, `config/turbo_mode.rst` "Turbo Control registers" (github.com/GideonZ/1541u-documentation), same sense |
| `$D031` reads `$00` in "TurboEnable Bit" mode until a program writes it. | Xander, `TURBOCONTROLMANUAL.md` §3. Unmeasured in his code (the `$D030` fallback that text describes is not in `include/turbo.c`); taken by the source rule (§3.3) |
| `$D030` only with enable bit 2 ("TurboEnable Bit" mode), else `$FF`. Write bit 0: 0 = 1 MHz + badlines, 1 = menu settings. Read bit 0: turbo off/on. | same |
| `$D07A` / `$D07B` write-only, SuperCPU "normal" / "turbo" speed select, with enable bit 0 or 2. | same |
| `$D0BC` read-only SuperCPU detect, only with enable bit 1. | same; TRX64 851 §3 (VICE `scpu64mem.c:671-676`) |
| "The CPU and memory subsystem run at 48 MHz (U64) or 64 MHz (U64E2); the setting determines how many of the time slots go to the CPU. The VIC still steals cycles." "Badline timing does not influence BA on the cartridge port." "When badlines are disabled, the CPU can continue even when BA=0, given that the access is to an internal address." External accesses "always work at 1 MHz", only with BA = 1. | same, "Notes on Bad Lines in Turbo Mode" |
| After a reset the Ultimate holds the C64 at 1 MHz for **2^22 PHI2 cycles** (4.26 s PAL, 4.10 s NTSC; measured on Gideon's core, §10b, superseding TRX64 BUG-061's 2.06 s), whatever speed is set. A strobe inside the hold is kept and applies when it ends; a change without reset applies at once. | TRX64 BUG-061, `vic.rs:1221-1240`, `lib.rs:2826-2840` |
| A `$D031` write is adopted at the **next PHI2 edge**, and it restarts the sub-PHI2 phase. | TRX64 868 §5a (measured with UPic against the real firmware: row period 63 PHI2, not 126). Xander's "takes effect on the very next CPU cycle" (§6 `turbo_set`) is an instruction-level statement; only the edge model lets his UPic resync pair cost nothing, which is what his hardware-tuned row timing needs (§3.3 C4) |
| `$D030` reads `value | $FC`. | TRX64 `vic.rs:2946-2948` — an assumption, marked as such there |
| A C64 reset clears `$D030`/`$D031` (and "written"); the enable word and preferred speed are the firmware's and survive it. | TRX64 `lib.rs:1462-1472`, `:2789-2798` |

**Bit 7: two registers, two senses (Decision A2, revised 2026-10-03).** `SPEED_PREFER` (written
by the app) and `$D031` (written by a C64 program) are different registers and their bit 7 has
**opposite** sense:

| Register | Writer | Bit 7 = 1 means | Source |
|---|---|---|---|
| `SPEED_PREFER` (page "011" +0x2D) | app, `setCpuSpeed` | badline timing **on** (C64 stalls) | `u64_config.cc:1657`; "Off" writes `$80` = 1 MHz, timing on |
| `$D031` | C64 program | badline stalls **suppressed** (timing off) | Xander (real hardware), Gideon's manual |

In hardware the two meet only in the effective-speed function (§3.2): whichever was written
last (Q4) is the source, and its bit 7 is converted to one internal `bl_on` with that
register's sense (`bl_on = src_d031 ? !d031[7] : prefer[7]`). Neither register is ever copied
into the other, so neither sense has to be translated on a read: `$D031` reads back what the
program wrote. The first version of this spec (and TRX64) took the `$D031` sense from PREFER;
that was a derivation across two registers, not a source — see §10 Q1 and §11.

### 3.2 Behaviour (copied from TRX64 851/868 + BUG-061, plus `$D07A/B`)

State in the C64 clock: `regs_en[2:0]`, `prefer[7:0]` (from the app), `d030_b0`, `d031[7:0]`,
`d031_wr`, `scpu_slow`, `hold[20:0]`.

- **Decode:** exact addresses only, I/O mapped (`io_enable` in `c64_bus_arbiter.v`). No mirrors:
  `$D070`, `$D071` stay the VIC's (`$FF`). Q3 (decided).
- **Reads.** At 1 MHz a last stage in the arbiter's bus mux, next to the UCI's at
  `c64_bus_arbiter.v:387-390`; in turbo the same values are answered on the fast path without a
  bus cycle (§5.3): `$D030` → `{6'h3F, 1, d030_b0}` with `regs_en[2]`; `$D031` → `d031` (the
  last program write, `$00` after reset, C3) with `regs_en[0]`; `$D0BC-$D0BF` → `$00` with
  `regs_en[1]` (TRX64 `u64_extra_read`). Otherwise kawari answers `$FF`
  (`registers_scaled.v:1060-1067`, `WITH_EXTENSIONS` is off in `vicii_kawari.vh`).
- **Writes.** At 1 MHz snooped at the phi2 falling edge of the bus cycle, like T28's `io_ev`; in
  turbo taken on the fast path at the CPU cycle of the store (§5.3), which is what lets UPic's
  resync pair cost nothing (C4). `$D030` → `d030_b0` (regs_en[2]); `$D031` → `d031`,
  `d031_wr = 1`, phase restart (regs_en[0]); `$D07A` → `scpu_slow = 1`, `$D07B` →
  `scpu_slow = 0` (regs_en[0] or [2]). Writes by the app's bus DMA count like CPU writes
  (`restore_io` writes `$D030` back).
- **SPEED_UPDATE:** takes `regs_en`, `prefer`. **Decision A3** (Q4: the last write wins): it
  also clears `d031_wr`, so the menu's speed applies until the program writes `$D031` again.
  `d031` itself is not touched: `$D031` still reads the program's last write (unmeasured; §8.4).
- **Reset** (`cpu_reset`): `d030_b0 = 0`, `d031 = 0`, `d031_wr = 0`, `scpu_slow = 0`,
  `hold = 2^22` (4,194,304 PHI2 cycles, 23-bit counter; §10b, measured 4,194,166-4,194,248). `hold` counts down once per PHI2.
- **Effective speed** (TRX64 `u64_speed()`):

  ```
  if hold > 0                                  -> 1 MHz
  src_d031 = regs_en[0] && d031_wr             (last of $D031 store / UPDATE, Q4)
  index    = src_d031 ? d031[3:0] : prefer[3:0]
  bl_on    = src_d031 ? !d031[7] : prefer[7]   (each register's own sense, C1)
  if regs_en[2] && !d030_b0                    -> index 0
  if scpu_slow                                 -> index 0
  N = min(TABLE_U64II[index], N_MAX)           CPU cycles per PHI2 cycle
  ```

  Before the first program write the menu's speed applies even in "U64 Turbo Registers" mode,
  while `$D031` reads `$00` (TRX64's speed rule kept; unmeasured, §8.4).

  `N = 1` means the turbo path is off and the machine runs exactly as today (badline timing does
  not matter at 1 MHz, as in TRX64 where `check_ba` is skipped only with `turbo_div > 1`).
- **Adoption:** a new `N`/`bl_on` is computed at the store and takes effect at the next PHI2
  falling edge (TRX64 868 §5a claim 1). UPic's resync pair `STA $D031 #$80 / STX $D031 #$8F`
  lands both stores inside one PHI2 cycle at 64 MHz, so `$80` is never in force at an edge and
  the pair costs nothing, as on the real machine.
- **Phase restart (868 §5a claim 2) — not built (C7).** TRX64's CPU is the master clock, so
  restarting its sub-PHI2 counter moves the next PHI2 edge relative to the CPU. Ours cannot: PHI2
  is the board's real time, and the slot grid is re-zeroed at every PHI2 falling edge (§5.2), which
  is what keeps a CPU cycle from straddling an edge. At N = 64 every clock is a CPU slot, so there
  is no phase to restart; below 64 a restart would matter only to sub-PHI2-timed code at that
  speed, of which none is known. 868 itself records claim 2 as not derivable and harmless for
  rows arriving at a constant phase. §8.4 (UPic picture position on the C64U) checks it;
  Q13.

### 3.3 Where the sources disagree

Rule (Alex, 2026-10-03): where sources conflict and nothing is measured, Xander Mol's
real-hardware statements win, TRX64 second, Gideon's manual third. A measurement beats any
statement, including one by the same author. The choices made under that rule:

| # | Question | Taken | Over | Why |
|---|---|---|---|---|
| C1 | `$D031` bit 7 sense | 1 = stalls suppressed | TRX64 (1 = on) | Xander + Gideon agree; TRX64 derived its sense from PREFER |
| C2 | Are CIA timers and the raster clocked at CPU speed in turbo? | **No: PHI2 = real time** (§5.0) | Xander's manual §2/§7 sentence | Xander's own hardware-verified programs contradict that sentence: heartbeat-demo runs `turbo_fast()` and drives its tempo from CIA1 timer A periods computed for 985,248 Hz (`hbplayer.c` `hb_set_tempo`, `main.c:202`), and Xander reports the tempo verified by listening on hardware (`ARCHITECTURE.md` §8, where a turbo-not-engaged tempo bug was found that way); mandelbrot-upic syncs every picture row to `$D012` at 64 MHz (`upic_viewer.c` `render_frame`, row delay `$87` bisected on hardware). Neither works if timers or `$D012` count CPU cycles. TRX64's code agrees (`u64_turbo_gate.rs` `time_is_phi2_time_at_every_speed`) |
| C3 | `$D031` read before any program write | `$00` | TRX64 (reads PREFER) | Xander §3 (TurboEnable mode); with C1 a PREFER image would read `$80` = "suppress" for the firmware's "1 MHz, timing on", which is wrong in either case |
| C4 | When a speed write takes effect | next PHI2 edge | Xander's "very next CPU cycle" | measured indirectly: TRX64 868 §5a with UPic on real firmware; the instruction-level wording is compatible |
| C5 | Interrupt latency in turbo | two PHI2 cycles (TRX64) | the natural fast-6502 latency | Alex, Q6 |
| C6 | Does turbo change the picture or BA? | no, the VIC always has priority | — | Gideon #665 (TRX64 815 §3.1); no conflict |
| C7 | Does a `$D031` write restart the sub-PHI2 phase? | no (the slot grid follows PHI2) | TRX64 868 §5a claim 2 | no physical analogue with a real-time PHI2; a no-op at N = 64 (§3.2) |

C2's sentence in Xander's manual is a stated explanation, not a reported measurement; the
benchmark it describes ("a tight loop always takes the same number of timer ticks") would also
come out 1:1 with turbo not engaged, the failure heartbeat-demo documents for its own tempo bug.
That is a hypothesis; the §8.4 C64U measurement settles it.

---

## 4. Speeds we can give

The C64 runs on `clk` = dot4x, 31.528 MHz, 32 clocks per PHI2 cycle (`c64_core.v` `FRAME` =
312 × 63 × 32). `clk2x` (63.06 MHz, same MMCM, from T25) is exactly 64 clocks per PHI2 cycle —
the C64U's "64 slots per µs" (Gideon #665, TRX64 815 §3.1). The target is the full U64-II table up
to 64 MHz (Q9), built in two stages:

| Stage | Turbo CPU clock | CPU cycle | Slots per PHI2 | `N_MAX` | Highest U64-II index |
|---|---|---|---|---|---|
| 1 | `gclk` (dot4x) | 2 clocks: the address leaves the CPU after its enable edge, the RAM registers it on the next edge, the CPU takes the data on the edge after (`c64_mem.v:97-106`, synchronous read) | 16 | 16 | 9 (16 MHz) |
| 2 | `clk2x` through its own BUFGCE | **1 clock**: port B of `c64_mem` on a phase-shifted copy of `clk2x` from the same MMCM, so the RAM registers the address mid-cycle and the data is at the CPU before the next edge | 64 | 64 | 15 (64 MHz) |

Stage 2 is the risk: one 15.9 ns clock holds CPU address register → fast decode → RAM address
setup (first part) and RAM clock-to-out → data mux → the core's input logic (second part); the
phase shift splits the period between the two and is chosen by timing. Evidence it can close: the
same 6502N core (`cores/icepi/cpu6510.v:20`, Gideon's `proc_core.vhd`) gives 64 CPU cycles per µs
in Gideon's bitstream on this board's FPGA family; how his memory side does it is closed. The CI
WNS gate decides. If stage 2 does not close at one clock per cycle, the fallback is two clocks on
`clk2x` (32 slots, `N_MAX` 32), and UPic, which needs 64 (TRX64 868 §5), does not run (Q14).

Requests map as follows (U64-II table; "real" = N × 0.985 MHz):

| Index | Menu | Stage 1 gives | Stage 2 gives | Stage 2 fallback |
|---|---|---|---|---|
| 0-9 | 1-16 MHz | as asked | as asked | as asked |
| 10-12 | 20, 24, 32 | **16** | as asked | as asked |
| 13-15 | 40, 48, 64 | **16** | as asked | **32** |

**Decision A4 — cap and report.** The registers keep what was written (`$D031` reads back the
requested index: a program that tests its own write sees what it wrote). The delivered `N`, the
cap flag and the hold are in a new read-only word `TURBO_STAT` (trx_sys page 0, 0x140:
`{hold active b31, capped b30, bl_on b29, N b22:16, index b3:0}`) for trxmon, the T32 menu and
REST. The app's menu text still shows Gideon's numbers (Q5).

---

## 5. Architecture

### 5.0 What runs on the CPU clock, and what on real time

Turbo is CPU-only, as on the Ultimate (TRX64 856 §1). PHI2 stays the board's real time
(985,248 Hz from dot4x / 32), and everything that counts time stays on it:

| Item | Clock in turbo | Source / reason |
|---|---|---|
| 6510 core, fast RAM/ROM accesses, `$00/$01` | **CPU** (N slots per PHI2) | §5.2 |
| Turbo registers `$D030/$D031/$D0BC/$D07A/B` | CPU (fast path) | §3.2; UPic's resync pair must cost nothing |
| VIC colour registers `$D020-$D02E`, writes | CPU (fast path), in force from the next pixel | Q2; UPic; §5.3 |
| VIC raster `$D011` bit 7 / `$D012`, reads | CPU (fast path) reading the **real-time** raster | §5.3 |
| VIC display: raster counter, fetches, BA, IRQ, pixels | **PHI2** (dot4x) | Gideon #665: the VIC always has priority; C2 |
| CIA 1/2 timers A/B, ICR, serial | **PHI2** | C2: heartbeat-demo's tempo, TRX64 `time_is_phi2_time_at_every_speed` |
| CIA TOD | **PHI2-derived 50 Hz** = real time, as today | Xander §2 (the one point his manual and his programs agree on) |
| SID (reDIP / T25 engine) | **PHI2**: the oscillators run at 1 MHz, the SID stays in tune | TRX64 856 §1; register accesses are bus cycles (§5.3) |
| 1541 drive, IEC | own 1 MHz, unchanged | §6.7 |
| Port fall-off counters in `cpu6510.v` | PHI2 (they count time) | §5.2 item 5 |
| IRQ/NMI delay | **PHI2**: two PHI2 edges, as TRX64 | Q6, §5.6 |
| T21 `CYCLES`, stops, frames | PHI2 | §6.3 |
| T21 `INSTRS`, new `CPU_CYCLES` | CPU | §6.3 |

Xander's manual says the CIA timers and the raster counter are CPU-clocked on the U64; this design
does not follow that sentence (C2, §3.3). The VIC display could not be: a raster counter running at
CPU speed would be a different video standard.

### 5.1 Today: one CPU access per PHI2 cycle

- `cpu6510.v`: the core's clock enable is `phi2_l` (`:189`), once per PHI2 cycle, ~280 ns into
  phi1 (`vicii.v:451`). Read data, RDY, IRQ, NMI and the port pins are latched at `phi2_n`
  (`:116-123`); `clk_phi` selects live vs. latched (`:135-137`). `$00/$01` writes need `clk_phi`
  (`:143`). The floating port bits' fall-off counters tick at `phi2_n` (`:155-170`).
- `c64_top.v:460`: `cpu_rdy = vic_ba && !cpu_paused && !reu_dma_active && !ext_cart_wait`.
- `c64_bus_arbiter.v:202-262`: RAM/ROM accesses are strobed from kawari's RAS/CAS, one per half
  cycle, onto `c64_mem` port A through `c64_top.v`'s mapping (`:1464-1530`). The PLA is the
  `casez` at `:491`; the data bus mux is `:349-391`.
- `c64_mem.v`: port A (`gclk`) serves VIC and CPU; port B (`clk`, `:75-88`) serves JTAG, the
  freeze responder, the app's RAM DMA (`top.v:976-979`, also while the machine runs with
  `C64_DMA_MEMONLY`). Port A's writes feed the T20 tap (`:90-95`).

### 5.2 The fast path

A new block `c64_turbo.v` in `c64_top`, plus small changes in `cpu6510.v`, `c64_mem.v`,
`c64_top.v`, `c64_core.v`, `trx_dbg.v`.

1. **Slot sequencer** (a DDA instead of the first draft's pattern ROM). An accumulator `acc`
   (log2 of the slot count, 4 bits in stage 1, 6 in stage 2) is cleared at every phi2 falling
   edge and adds `N` at every slot position; a carry is a slot hit. Over the S positions of a PHI2
   cycle that gives exactly N hits, evenly spaced, and **the last hit is always the last
   position, whose clock-enable edge is the phi2 falling edge itself** (S·N/S = N). So a CPU
   cycle never straddles a PHI2 edge: at every edge the gate may stop on, the CPU is between
   cycles (§6.5) and the edge's CPU cycle is the one T20's snap and T21's edge logic already
   look at (§6.2, §6.3). At N = 64 (stage 2) every clock is a slot.
2. **CPU clock enable.** `cpu_ce = turbo ? (slot_hit && go) : phi2_l`. `go` is low while a slow
   access waits (§5.4), while RDY holds a read (§5.5), and while a debugger park is on (§6.3).
3. **Fast decode.** A copy of the arbiter's PLA case on `cpu_addr` with the live `$01`
   (`cpu_pout`), GAME/EXROM and `ext_ultimax` (the freeze responder already carries such a copy,
   `freeze_resp.v` header): `RAM`, `BASIC`, `KERNAL`, `CHAR`, `TREG` (the turbo registers),
   `VCOL` (`$D020-$D02E` with I/O mapped in), `VRAS` (`$D011`/`$D012` reads), or `SLOW`
   (everything else in I/O, ROML, ROMH, open Ultimax ranges). Writes to ROM-mapped addresses are
   RAM writes, as on the C64.
4. **Fast RAM/ROM access on `c64_mem` port B** (Decision A5). The turbo CPU uses port B; port A
   stays the VIC's and the arbiter's, so no VIC slot has to be reserved and all 16 (64) slots are
   usable. ROM reads use port B's ROM half (`dbg_rom`). The read data goes straight to the CPU (a
   new input of `cpu6510`, muxed in front of `din_r`), as do `TREG` and `VRAS` reads.
5. **`cpu6510.v`:** `din_i = turbo_fast ? fast_din : din_r`; the `$00/$01` write and the
   RDY/IRQ/NMI latches use `cpu_ce`-based enables in turbo; the port fall-off counters stay on
   `phi2_n` (they count time). At `N = 1` the selects are constant and the module is today's.

### 5.3 Which accesses run fast

| Access | Turbo | Why |
|---|---|---|
| RAM, any address the PLA maps to RAM (incl. under ROM/I/O when mapped out, writes under ROM) | fast, port B | internal |
| BASIC, KERNAL, CHAR ROM | fast, port B ROM half | internal block RAM |
| `$00`/`$01` | fast, inside `cpu6510` | the port is in the CPU; the RAM write underneath behaves as the 1 MHz path does (sim A/B, §8.1) |
| `$D030/$D031/$D0BC`, `$D07A/B` | fast, `c64_turbo` | our own registers (§3.2); UPic's resync pair costs nothing only if both stores land in one PHI2 cycle |
| VIC colour registers `$D020-$D02E`, **writes** | **fast**, side port into kawari (below) | Q2: drop-in for Gideon; UPic needs eight stores per PHI2 cycle (TRX64 868 §1) |
| `$D011`, `$D012` **reads** | fast: raster from kawari's position (the T21 `POS` tap), other `$D011` bits from a write shadow | UPic's row sync polls `$D012` ~38 times per row on TRX64 (868 §9); as bus cycles each poll costs ~1 PHI2 and the row start moves by up to 8 pixels |
| Other VIC registers (incl. `$D019`, collision `$D01E/F` reads, all non-colour writes), SID, CIA 1/2, colour RAM | **slow** (one PHI2 bus cycle) | sub-cycle access inside reDIP/CIA cores and the VIC's non-colour logic is a larger machine (TRX64 868 §7); Gideon's cost unknown (Q12) |
| `$DE00-$DFFF`: UCI (T28), REU registers, cartridge IO1/IO2 | slow | bus cycle, `io_ev` |
| ROML/ROMH (cart_slot) | slow | cart_slot answers in the PHI2 bus cycle with its cache and RDY wait |
| Open bus, Ultimax holes | slow | the open bus is the last bus value |

**Fast colour writes (stage 2, with 64 MHz).** kawari samples every colour register once per
pixel: `pixel_sequencer.v:169-175` (`ec`, `b0c`-`b3c` at `dot_rising[2]`) and `:128-134` (sprite
colours and multicolours at `dot_rising[1]`); a pixel is four dot4x clocks. So a register written
at the right moment is already a per-pixel colour, with the 6569's own pipeline latency — no slot
array as in TRX64 868 §4 is needed. A `VCOL` store in turbo writes the register through a new side
port of `registers_scaled.v` (register index, data, strobe), retimed from `clk2x` to the next
`gclk` edge (≤ half a dot4x clock late, an eighth of a pixel); kawari's bus write path for the
same registers is idle then (the arbiter sees a parked CPU, Decision A6). At N = 64 a store every
eight CPU cycles is one per pixel, which is UPic. A read of the register returns the last write,
as on the C64. The absolute pixel at which a store lands, relative to the PHI2 edge, follows from
kawari's pipeline and is checked against the C64U's picture of UPic (§8.4, Q13).

TRX64 models every I/O access without stretch (851 D3, unmeasured). Our CIA/SID/other-VIC accesses
cost about one PHI2 cycle each; §8.4 measures Gideon's cost, Q12.

### 5.4 Slow accesses

The CPU presents a `SLOW` address: `go` drops, the CPU holds address, R/W and data. The access is
made by today's path, in the CPU half of the **next** PHI2 cycle whose half has not started yet
(it must be stable at `phi2_p`, where the arbiter and the chips sample it) and in which the CPU
may use the bus (`vic_ba` high for a read, AEC high for any; RDY rules as today). Data is latched
into `din_r` at `phi2_n` as today, the CPU takes it at the last slot's enable on that edge, and
fast cycles go on from the first slot. Average cost about one PHI2 cycle per I/O access (half a cycle waiting
for the next half to begin, half a cycle for the half).

While no slow access is pending, the arbiter must not see the turbo CPU's changing address: a
CIA read there would clear ICR flags, a write would hit a chip. **Decision A6:** in turbo,
`c64_top` gives the arbiter a parked CPU: `cpu_we = 0` and an address that decodes as RAM (a
phantom port A read, no side effect) unless a slow access is latched for this half. `io_ev`
(T28) and the T21 watch units then see only real bus accesses on the bus side (§6.3).

### 5.5 BA, RDY, DMA

- **Badline timing on:** `vic_ba` low holds turbo **reads** (the 6502N `ready` input stalls read
  cycles only, as RDY on a 6510); writes go on. The CPU loses the BA-low cycles like a 6510.
- **Badline timing off** (`bl_on` = 0: `$D031` bit 7 set, or PREFER bit 7 clear): `vic_ba` does
  not hold fast cycles, including `TREG`/`VCOL`/`VRAS` accesses. A slow access still waits for BA
  high (Gideon: "you will still run into the badline limitations" for external and SID accesses).
- **REU DMA** (`reu_dma_active`): no fast cycles at all; the REU owns the bus, one byte per PHI2
  as today. REU registers are slow accesses.
- **`cpu_pause_req`** (the app's C64_STOP, CPU only) and the gate's `held` (bus DMA in a pause):
  no fast cycles, as `cpu_paused` holds the 1 MHz CPU today.
- **Cartridge wait** (`ext_cart_wait`, cart_slot cache miss): only matters for the slow ROML/ROMH
  access that caused it.
- **The app's bus DMA** (`ext_dma_*`, while paused): unchanged.

### 5.6 IRQ and NMI (Q6: as TRX64)

The sources (VIC, CIAs, cartridge, REU, UCI) change at PHI2 phases, as today. In turbo the IRQ and
NMI lines reach the 6502N core through an age gate: a 2-bit counter per line, cleared while the
line is inactive and counted up at each phi2 falling edge to 2; the core sees the line only once
its age is 2. The core then recognises it at its next instruction end as usual. That is TRX64's
model (VICE TurboMaster: `irq_delay_cycles` bumped once per PHI2 cycle in `clk_inc`,
`c64_6510core.rs:829-878`; 851 §4) — the 6510's two-cycle delay counted in PHI2 cycles, so an
interrupt is taken at the first instruction end at least two PHI2 edges after the source fired.
NMI is an edge: the falling edge is delayed the same way. At N = 1 the gate is bypassed and the
core's own sampling is today's, bit for bit.

### 5.7 Port B sharing

Stage 1: the turbo CPU uses port B on one clock of its two-clock cycle (the address clock). The
others are served on the free clock: the trx_io RAM DMA through its existing grant (`tx_go`,
`top.v:976-979`), JTAG through a one-entry hold so its one-clock pulse is not lost, the freeze
responder only while the machine is paused (no conflict). Stage 2 at N = 64: every clock is a CPU
slot, so a DMA or JTAG access takes one slot from the CPU (`go` low for one clock); below 64 they
use the free clocks. Block RAM collision: CPU write on port B and VIC read on port A to the same
address in the same clock — the ports are read-first; the VIC gets the old byte, as when the write
lands one clock later. Checked in sim (§8.1) and by Vivado's collision report.

### 5.8 Stage 2: clk2x

The turbo CPU, the fast decode and port B move to `clk2x` through a second BUFGCE whose CE is
`gate_ce` retimed so that both gated clocks stop and start on the same PHI2 edge; port B's RAM
clock is the phase-shifted copy (§4). DDA 6 bits wide. The rest of the C64 stays on `gclk`. The
slow-path handshake, the colour side port and the PHI2 signals cross between phase-locked clocks
(2:1, related), no asynchronous CDC. If timing does not close at one clock per CPU cycle, stage 2
falls back to two clocks per cycle on `clk2x` (N ≤ 32, §4, Q14).

---

## 6. Interactions

### 6.1 T20 delta ring: 1 MHz only (Q7)

**Decided (Alex, 2026-10-03): the ring does not record above 1 MHz.** "Langsam rantasten": the
ring and everything built on it are made right for the 1 MHz machine first.

- **Pause.** While the effective `N` > 1 (§3.2; the post-reset hold counts as 1 MHz) the ring
  stops recording, by the same mechanism as T20's own pause. The menu and REST report "Timewarp not
  available in turbo". When `N` returns to 1 it resumes with a fresh keyframe at the next snap.
- **What survives the gap.** The frames before the turbo span cannot be reached by undo from after
  it (the turbo span's writes are not logged). Default: the ring restarts on resume and the older
  content is dropped (Q15).
- **No turbo write path in T20.** The CPU's port B writes in turbo need no tap: the first draft's
  tap 0 source mux, the `wb_we` split and the four-records-per-burst drain packing (old Decision A7)
  are dropped, and so is the bandwidth and window table. A port B CPU write never reaches stream 0;
  the app's and JTAG's port B writes stay external as today.
- No T20 coalescing for turbo (T20's open question 10 stays T20's).

### 6.2 T20 snap, state bus, restore, `.c64re`

- **Snap in turbo — only for the single dump (Q8).** The ring's snaps run at 1 MHz only (§6.1),
  but a single `.c64re` dump must work in turbo. The snap edge is a phi2 falling edge on line 0
  that ends an opcode fetch (`l_sync`, `c64_core.v:531`). With the last slot on the edge (§5.2),
  `l_sync` at that edge means "the CPU cycle that completes on this edge is a fetch" — the same
  statement as at 1 MHz. A slow access completing on the edge is a normal CPU cycle there.
- **No sub-PHI2 state.** At every edge the gate stops on, the CPU is between cycles and the slot
  sequence restarts at the edge: nothing of the turbo timing needs saving except the registers. A
  slow access pending at a stop is re-presented by the restored CPU (its address and data are in
  the 209-bit CPU vector) and waits again: equivalent. A fast colour write is complete in the
  register at the edge (no per-cycle slot array to save, unlike TRX64 868 §9's question).
- **New state words** (chip 0 w1, `c64_core.v` map): `{d031 8, d030_b0, d031_wr, scpu_slow,
  regs_en 3, prefer 8, ...}` and w2 `{hold 21}`. Added to the capture table (`CAP_TAB_*`) by the
  app. Restore writes them through `TURBO_SET` (trx_sys page 0, 0x144, W, only with the gate
  shut). The `$D011` shadow of the `VRAS` read path (§5.3) is restored with kawari's registers.
- **`.c64re` (Q8, decided A):** `trx64Hw` gets `turbo {regsEn, prefer, d030, d031, d031Written,
  scpuSlow, hold}` — the enable word, PREFER with its own bit-7 sense, `$D030`, `$D031` with bit 7
  as written, and the hold. We write and read it; TRX64 reads it on load (§11) so a dumped turbo
  session resumes at the same speed (today a restored `u64` session starts from the menu default,
  851 §8, and its `c64re_snapshot.rs` restore recomputes the divider from `$D031` alone). No
  "I/O stretched" mode is asked of TRX64. After the first CIA/SID access the two machines diverge
  by the I/O cost difference (§5.3, Q12).

### 6.3 T21 monitor

| T21 item | In turbo |
|---|---|
| `CYCLES` (0x120), `CYC_CMP` (0x130) | stay **PHI2 cycles** (C64 time): raster, CIA timers and TOD, SID, frame and ring are keyed on them (§5.0), as TRX64 keeps `clk` the PHI2 clock (851 D3). |
| `STEP_CYCLES n` | n PHI2 cycles; the CPU runs as many turbo cycles as fit. Invisible like today (§11 test 9 of T21). |
| `INSTRS` (0x128) | counts IBs at every `cpu_ce`, not only at PHI2 edges. |
| **new `CPU_CYCLES`** (0x138, hi latch 0x13C) | CPU cycles that completed (fast and slow), 48 bit, DSP adder like `CYCLES`. `CPU_CYCLES / CYCLES` = the effective speed. |
| `PAUSE_IB`, `STEP_INSTR`, exec bitmap, watch-unit stops | **park**: at the IB / the matching access the sequencer stops issuing slots (`go = 0`); the gate shuts at the next phi2 falling edge. The CPU is exactly at the stop; the chips are up to one PHI2 cycle further. The parked fraction is lost turbo time: debugging perturbs turbo timing, documented, nothing to compare against on a U64. |
| Watch units: `edge_`, `ib`, `cyc` (`trx_dbg.v:130-132`) | fed by a mux: in turbo, `cpu_edge = cpu_ce`, address/data/R/W from the fast path; type 5 "every phi2 cycle" stays on `edge_`. Count and log actions do not park. The exec bitmap lookup (`trx_dbg.v` `bmp_q`, one clock) fits the two-clock cycle; verified in sim. |
| Log record cycle field | PHI2 cycles, low 15 bits, unchanged. |
| Stop record `POS` | VIC position of the stop edge, unchanged. |
| T21 verb `turbo` (`T21-monitor.md:272`, "not available (T29 later)") | becomes: show `TURBO_STAT`; set writes the app's PREFER/UPDATE path (not `$D031`), so its bit 7 has PREFER's sense (1 = badline timing on). |
| Fast colour writes, `TREG` accesses | seen by the watch units through the `cpu_edge` mux like any fast access; never on the bus, so not in `io_ev` (T28). |

### 6.4 T28 UCI

Unchanged. UCI registers are slow accesses, so `io_ev` (`c64_core.v:333-341`) still sees exactly
one real access per PHI2 edge, and only real ones (Decision A6). UCI timeouts in Xander's libraries
count CIA TOD (`UCILIBMANUAL.md`, `uii_wait_for_uci`), which stays real time (§5.0). A turbo program polls the UCI
more often per frame; the protocol is handshake-based.

### 6.5 Pause and freeze (T12)

- The gate stops on a phi2 falling edge (`c64_core.v:284-312`). With the last slot on that edge
  the CPU is between cycles; `gclk` (and in stage 2 the `clk2x` BUFGCE) stopping freezes the
  sequencer. Resume after whole frames: the DDA restarts at the edge. Nothing else changes.
- **Freeze session** (`freeze_resp.v`): VIC offsets `$2F` and up read `$FF` there (`:292`), so the
  menu's `backup_io` gets `$FF` for `$D030` and `restore_io` writes `$FF` into the overlay, which
  is dropped: the turbo state is untouched by a freeze, which is the intent (Gideon's machine
  reads and writes the real value back: same result). The commit-on-reset replay must skip
  offsets `$2F+` (else a `$FF` reaches `$D030` just before the reset, which clears it anyway).
- The app's C64-screen menu (CPU-only pause, GATE_CTRL b0 = 0): the CPU is held, no fast cycles.

### 6.6 Cartridge (cart_slot)

ROML/ROMH reads stay slow: a cartridge game running from ROM gains little in turbo (its RAM work
speeds up). Writes to cartridge registers (IO1/IO2) and cartridge RAM are bus cycles as today.
cart_slot's cache, RDY wait and flash responder see the same traffic as at 1 MHz. Phase C (§9).
Freezer cartridges and T12's freeze stay as at 1 MHz: the NMI goes through the age gate (§5.6).

### 6.7 Drive, IEC, fastloaders

**Corrected 2026-10-06 (integ-m1):** the assumption below, that IEC "breaks as on a U64", is wrong. On Alex's
C64U with Gideon's core `LOAD"$",8` works at every speed (docs/turbo-measurements-gideon.md, round 4), on our
T29 bitstream (board 2026-10-04) it does not: 4 MHz "?DEVICE NOT PRESENT" at once, 16/64 MHz hang at
"SEARCHING FOR $". That is a defect of this engine. **Cause found 2026-10-08, fixed (IEC window, §12.3), board test pending:** the
KERNAL times the serial bus with CPU loops between writes to `$DD00`; at 4 MHz the CLK-high pulse is 5 us (a
turbo CPU shrinks the loops, CIA 2 accesses stay 1 us), the 1541's receive loop (`$EA0B`, 14 cycles) misses the
bits and the LOAD ends in ?DEVICE NOT PRESENT (sim/drive bench, §12.8). Until the board confirms it turbo stays
off by default (the app's Turbo Control and CPU Speed defaults are Off / 1 MHz).


The drive (`drive_unit.vhd`) keeps its own 1 MHz. CIA 2 accesses are slow, so the bus handshakes
happen at PHI2 times, but the code between them runs faster. Expected, as on a U64: fastloaders
and anything that counts cycles against the drive break; KERNAL delay loops (e.g. the 1 ms loop
used around ATN) shrink by N. Not fixed in T29; the 2^22-PHI2 reset hold keeps the KERNAL's PAL/NTSC
race at 1 MHz (BUG-061). §8.4 compares `LOAD"$",8` and a KERNAL `LOAD` at 4, 16 and 64 MHz against
the C64U with Gideon's core.

### 6.8 Raster code, SID, timers

Raster IRQs, CIA timer IRQs and music players keep their real-time rate: the VIC and the CIAs count
PHI2 (§5.0); heartbeat-demo's tempo, set in 985 kHz CIA counts while the CPU runs at 64 MHz, is the
check. Stable-raster tricks and anything cycle-exact in the main loop break, as on a U64. SID
register writes are slow accesses; the SID itself stays in tune; digis timed by CPU loops play
faster. UPic-style per-pixel colour code works at 64 MHz (§5.3).

---

## 7. Resources

Baseline main: ~77.8 % LUT (≈ 25,360 of 32,600), 60 of 75 BRAM tiles. T29's ceiling is **82 %**
(Alex, Q9) = 26,732 LUT, so **~1,370 LUT** are available to it; timing is guarded by the CI WNS gate.

| Part | Stage | LUT | FF | BRAM | DSP |
|---|---|---|---|---|---|
| Register block: page "011" regs + toggle CDC, `$D030/31/7A/7B/BC` decode and fast read/write, hold counter (21 b), effective-speed function with the two bit-7 senses | 1 | 120 | 70 | — | — |
| Fast PLA decode on `cpu_addr` (`RAM/ROM/TREG/VCOL/VRAS/SLOW`) | 1 | 75 | — | — | — |
| Slot sequencer: DDA, `go` logic, slow-access latch/handshake | 1 | 50 | 20 | — | — |
| `cpu6510.v` muxes (din, port write enable, RDY select) | 1 | 30 | — | — | — |
| IRQ/NMI age gate, two PHI2 edges (Q6) | 1 | 15 | 6 | — | — |
| Arbiter parked-CPU mux (Decision A6) | 1 | 25 | — | — | — |
| `c64_mem` port B: CPU requester, JTAG hold, tx grant gating | 1 | 50 | 30 | — | — |
| `VRAS`: `$D011` shadow + raster read mux | 1 | 20 | 8 | — | — |
| T20: ring pause while N > 1, keyframe on resume, status bit (Q7) | 1 | 15 | 5 | — | — |
| T21: `cpu_edge` mux for units/IB/bitmap, park request, `CPU_CYCLES` | 1 | 70 | 50 | — | 1 |
| State bus words, `TURBO_SET`, `TURBO_STAT` | 1 | 60 | 40 | — | — |
| **Stage 1 total** (N ≤ 16) | | **≈ 530 (range 430-680)** | ≈ 230 | 0 | 1 |
| BUFGCE + CE retiming, phase-shifted RAM clock, 6-bit DDA, slot steal for DMA/JTAG | 2 | +40-60 | +30 | 0 | 0 |
| Colour side port in `registers_scaled.v` + `clk2x`→`gclk` retiming (§5.3) | 2 | +60-100 | +30 | 0 | 0 |
| **Stage 2 total** (N ≤ 64, UPic) | | **≈ 660 (range 530-840)** | ≈ 290 | 0 | 1 |

Dropped against the first draft: the T20 tap 0 mux and `wb_we` split (−25), drain packing
(old A7, ~+60 in phase B) and the 16 × 16 pattern ROM (now a DDA). Stage 1 lands at ~25,890 LUT
(79.4 %), stage 2 at ~26,020 (79.8 %), worst case ~26,200 (80.4 %): inside 82 % with ≥ 500 LUT to
spare. No block RAM. The phase-shifted RAM clock needs a free MMCM output (one BUFG); to check
against the clocking plan before stage 2. Timing: stage 1 puts the fast decode and port B in front
of the CPU's data input on a 31.7 ns clock (easy); stage 2's one-clock CPU cycle at 15.9 ns is the
risk (§4, §5.8).

---

## 8. Test plan

### 8.1 Simulation (Verilator, `designs/c64_basic/sim/turbo`, like `sim/lorenz`, `sim/dbg`)

1. **1 MHz identity.** With the enable word 0 and with `$D031` index 0: bus trace (address,
   data, R/W at every phi2 falling edge) equal to main over 3 frames of BASIC and of
   `sim/lorenz`'s test set; `cpu_ce` equal to `phi2_l`; IRQ age gate bypassed.
2. **Registers.** Enable word `$00`: `$D030`, `$D031`, `$D0BC` read `$FF`. `$01`: `$D031` R/W,
   reads `$00` before a write, `$8F` after writing `$8F`, with `bl_on` = 0; PREFER `$83` gives
   `bl_on` = 1 before a `$D031` write. `$05`: `$D030` bit 0 switches between index 0 and the
   speed. `$02`: `$D0BC` reads `$00`. `$D07A`/`$D07B`. Reset clears `$D030/$D031`, arms the hold;
   UPDATE inside the hold is kept and applies at its end (BUG-061 gate, shortened hold
   parameter). UPDATE clears `d031_wr` and leaves the `$D031` read value.
3. **Speed.** `INC $02 / JMP` loop (TRX64 851 §5 gate): loops per frame at N = 2, 4, 8, 16 (and
   32, 64 in stage 2) equal N × the 1 MHz count within the badline share; badline timing off raises
   it by that share (TRX64 measured 7138 vs 6748 at 4 MHz). `CPU_CYCLES / CYCLES` = N.
4. **Time is PHI2 time.** CIA 1 timers A and B, TOD tenths and `$D012` reach the same values at
   the same `CYCLES` at N = 1, 16 and 64 (TRX64 `time_is_phi2_time_at_every_speed`).
5. **Slow accesses.** A loop with one `STA $D418` per iteration: each costs about one PHI2 cycle;
   no chip select fires outside a slow request (assert on the arbiter's `*_cen` while the CPU runs
   fast code that never addresses I/O; CIA 1 ICR flags stay set).
6. **BA.** Badline on: no turbo read completes while `vic_ba` is low; badline off: fast cycles
   (incl. `VCOL`/`TREG`) go on, a slow access waits. Sprite DMA as well.
7. **Port B.** A CPU write and a VIC fetch of the same address in one clock; JTAG peeks and trx_io
   DMA while the CPU runs at N = 16 and 64: none lost, results equal to a sequential model.
8. **IRQ/NMI.** A raster IRQ and a CIA 1 timer IRQ at N = 16 and 64 are taken at the first
   instruction end at least two PHI2 edges after the source — the same PHI2 cycle as TRX64 for the
   same program — and return; NMI from CIA 2.
9. **T20.** Turbo on: the ring stops, no stream-0 records; back to 1 MHz: it resumes with a fresh
   keyframe, `DROPPED` 0, undo works inside the new span. A single dump at N = 64, restored, run 50
   frames = the original (registers in the block).
10. **T21.** `STEP_INSTR` at N = 16 and 64 stops at each IB with registers as at 1 MHz for the same
    instructions; `STEP_CYCLES 1000` invisible; `CYC_CMP` in PHI2 cycles; watch unit on a RAM
    write and on a fast `$D020` write inside a fast run stops with the right address/data;
    `CPU_CYCLES` moves.
11. **Speed change.** UPic's resync pair `STA $D031 #$80 / STX $D031 #$8F` at N = 64 costs no PHI2
    cycle; the `$80` is never in force at an edge (TRX64 `the_speed_change_pair_costs_nothing`).
12. **Colour per pixel** (stage 2). At N = 64, eight `STA $D020` per PHI2 cycle, eight cycles
    apart, give eight different border pixels in kawari's output for that cycle; a store's pixel
    is fixed relative to the PHI2 edge, the same in every line. Xander's synthetic `upic_test`
    pattern through `render_frame()` matches the expected 384 × 256 grid (TRX64 868 §8.3), up to
    one constant horizontal offset, which is reported (Q13).
13. **Raster reads.** A fast `$D012`/`$D011` read returns the value a bus read in the same PHI2
    cycle returns.

### 8.2 Board (Alex)

- `tests/run_lorenz.sh` with turbo **Off**: 297/297. Again with "U64 Turbo Registers" at 1 MHz
  (enable word `$01`, index 0): 297/297.
- `LOAD"$",8` and a KERNAL `LOAD` of a PRG at 1 MHz (unchanged), 4, 16 and 64 MHz.
- Timewarp: in turbo the menu/REST say "Timewarp not available in turbo"; back at 1 MHz the ring
  records again.
- Xander's programs on our bitstream: mandelbrot-upic at 64 MHz (complete picture, all four
  corner markers), UltimateDemo2026, heartbeat-demo (tempo by ear and by the audio stream),
  GeoUTools and UBoot64 (UCI with TOD-based timeouts).

### 8.3 Benchmarks

Programs: the `INC zp / JMP` loop (RAM only); the same with one `STA $D020` per iteration (fast
colour write); with one `STA $D418` (SID) and one `LDA $DC0D` (CIA) per iteration (slow I/O); a
BASIC `FOR I=1 TO 10000:NEXT` timed with TI; Xander's `benchmark_delay(1000)` (CIA TOD tenths).
Each at 1, 2, 4, 8, 16 MHz (stage 1) and 32, 64 MHz (stage 2), badline timing on/off.

### 8.4 Against Alex's C64U with Gideon's core (Q10)

Procedure: power cycle from the menu (C= B "Power cycle", `userinterface.cc:115` for `U64==2`)
boots Gideon's firmware and bitstream from flash; run the programs over REST, read the results from
the video and audio UDP streams, JTAG SAMPLE read-only; then load our bitstream and app into SRAM
again and run the same. No flash writes. TRX64 and ue2emu are secondary references.

- RAM loop: ours = Gideon's within the VIC's slot share (~2 %; his VIC takes slots of the 64, ours
  uses port A).
- **Time base (C2):** CIA 1 timer A ticks counted over a TOD-timed interval, and `$D012` lines per
  TOD tenth, at 1 and 64 MHz. Expected equal at both speeds; if not, Xander's sentence was right
  and §5.0 is reopened.
- I/O loops: Gideon's cost per CIA, SID, non-colour VIC and colour RAM access (Q12).
- `$D031` bit 7: `$84` vs `$04` on a screen with badlines; `$84` must be the faster one (C1,
  confirms Xander's sense).
- `$D031` read after reset in modes `$01` and `$05`, after a write, after UPDATE (C3, Q4, Q11);
  whether the menu's CPU speed applies in mode `$01` before a program writes `$D031` (Q11);
  `$D0BC` with SCPU detect on; `$D070`/`$D071` (mirrors, Q3).
- UPic: picture position and the four corner markers, ours vs. Gideon's capture (Q13).
- heartbeat-demo tempo (audio stream): equal on both.

---

## 9. Phases

| Phase | Content | Gate |
|---|---|---|
| A0 | Registers (both bit-7 senses), app path, hold, `TURBO_STAT`; CPU still 1 MHz | §8.1 test 2, Lorenz |
| A1 | Stage 1 on `gclk`, N ≤ 16: port B, DDA sequencer, slow accesses, BA, fast `TREG`/`VRAS`, IRQ age gate | §8.1 1-8, 11, 13, benchmarks |
| A2 | T20 pause in turbo, T21 integration, state words, single dump in turbo | §8.1 9-10 |
| B | Stage 2 on `clk2x`, N ≤ 64, one clock per CPU cycle; fast colour writes (UPic) | CI WNS gate, §8.1 at N = 64 and test 12, §8.2 UPic, §8.4 |
| C (optional) | Fast cartridge ROM on cache hits; faster CIA/SID/VIC/colour-RAM access if §8.4 shows Gideon's core has it (Q12) | each its own spec |

---

## 10a. Decisions after the measurement on Gideon's core (2026-10-03)

Measured on Alex's C64U with Gideon's core (`docs/turbo-measurements-gideon.md`,
`tests/turbo-gideon/`):

- **Q14 decided (Alex): 64 MHz is mandatory for milestone 1, no 32 MHz fallback state.**
  Exactly as Gideon: index 15 = **63** CPU cycles per PHI2, index 14 = **47** (measured, four
  loop sizes); our slot sequencer gives N = 63 (not 64) at index 15 and 47 at index 14, the
  remaining slots free.
- **Q1 confirmed by measurement:** `$D031` bit 7 = 1 removes the bad-line stalls (5.43 % loss
  with 0 = 25 bad lines x ~43 cycles); also at index 0 (`$80` = 1 MHz without stalls). Menu
  "Badline Timing Enabled" = stalls; in registers mode a menu change appears in `$D031`
  inverted (16 MHz + Disabled reads `$89`, + Enabled `$09`).
- **Time base confirmed:** CIA timers 985,250-985,950 ticks per TOD second at all 16 speeds;
  raster in real time; picture stable at all speeds.
- **Q4 confirmed:** last write wins between `$D031` and the menu.
- **Q11 answered:** after a reset in registers mode `$D031` reads `$00` and the CPU stays at
  1 MHz with stalls until a `$D031` write or a menu change; Manual mode: ~1.3 s at 1 MHz, then
  the menu speed. Off/Manual: `$D030`/`$D031` read `$FF`; registers mode: `$D030` reads `$FF`.
- **Q6 re-decided (Alex, 2026-10-03): as measured on Gideon's core** — IRQ/NMI latency in
  turbo under one PHI2 cycle (0, or +1 at 16 MHz; normal lines, bad lines, badline timing on):
  the interrupt lines are sampled at the turbo CPU's own rate, no two-PHI2 delay. Supersedes the
  earlier "as TRX64" decision; TRX64 informed via C64U_emu.
- Not measured yet: I/O access cost in turbo (Q12), colour-store position (Q13), TurboEnable
  Bit mode, `$D07A/$D07B`, `$D0BC`, mirrors at `$D070`.

## 10b. Round 2 on Gideon's core (2026-10-03) — measured facts win (Alex's rule)

From `docs/turbo-measurements-gideon.md` "Round 2" (`tests/turbo-gideon/data/`):

- **Q12 answered — cost per access at 64 MHz, in PHI2 cycles:** RAM/ROM 0.065 (reference);
  **CIA1/CIA2 read or write 1.000** (a bus cycle); **UCI `$DF1C` / IO1 `$DE00` read 1.000**;
  **SID, all VIC registers (incl. `$D011`/`$D012`/`$D019` reads and every register write),
  colour RAM, `$D031`: fast** — writes cost as RAM, reads one extra CPU cycle at 64 MHz, none
  at 16 MHz. `$D011`/`$D012` reads return the live raster without a bus cycle. Overlap: CPU work
  between CIA reads is free up to ~62 of 63 cycles; a CIA write must be issued ~12 cycles
  earlier (work beyond ~44-50 cycles costs a second PHI2). With bit 7 = 1, CIA/UCI accesses are
  not slowed on bad lines; with bit 7 = 0 reads cost 1.061.
  → Our design: SID, VIC (all registers) and colour RAM become fast in turbo (not only the
  colour registers), CIA and IO1/IO2 stay one bus cycle.
- **Q13 answered:** the VIC samples `$D020` once per pixel (later store in a pixel wins); at 63
  CPU cycles per PHI2 = 7.875 per pixel. The pixel grid is aligned to the PHI2 edge within about
  one CPU cycle (boundaries at c ≈ −0.9, 7.0, 14.9 after the edge). No offset correction needed.
- **Q3 overridden by measurement:** `$D030/$D031` are **mirrored every `$40`**
  (`$D070/71`, `$D0B0/B1`, `$D0F0/F1`) and the mirrors act (`$D071 = $89` → 16x). We decode the
  mirrors like Gideon.
- **TurboEnable Bit mode:** after reset `$D030` reads `$FE`, `$D031` `$00`, 1x; `$D030 = 1` →
  `$FF`, `$D031` reads the menu value, menu speed; `$D030 = 0` back. `$D031` is writable in this
  mode too. In registers mode `$D030` reads `$FF`, writes have no effect.
- **`$D07A`/`$D07B`:** `$D07A` sets `$D031` to `$00` (1x); `$D07B` loads the menu value (not the
  program's earlier value); both read `$FF`.
- **`$D0BC`:** `$01` with SuperCPU Detect enabled, `$FF` disabled (mirrors not checked).
- **Menu side effect:** any config change in "U64 Specific Settings" re-applies the menu speed
  (even an unrelated item) — part of "last write wins".
- **Post-reset hold:** REST `machine:reset` → exactly 2^22 = 4,194,304 PHI2 cycles (4.257 s),
  within 80 cycles over five runs, at 16 and 64 MHz; a `$D031` write during the hold is kept and
  applies at its end. `run_prg` ends the hold ~3.2 s after the call (path differs; open).
  Replaces BUG-061's 2.06 s.
- Still open: why `run_prg`'s hold differs; absolute VIC x of the store pixel; why fast reads
  cost one extra cycle at 64 MHz only; `$D0BC` mirrors; I/O cost with a real external cart.

## 10. Open questions for Alex

1. `$D031` bit 7: the manual says 1 = badline timing **off**, the app's PREFER and TRX64 use 1 =
   **on**. Default to the app's sense until a measurement on a Gideon-core Ultimate (§8.4)?
   ~~Settled (2026-10-03, already decided for TRX64): 1 = badline timing on~~ — **withdrawn the
   same day.** That answer took TRX64 851's reading, and 851 derived the `$D031` sense from a
   different register: the firmware's `C64_SPEED_PREFER = speed | badlines << 7` ("Off" writes
   `$80`). PREFER's sense is right for PREFER; nothing says `$D031` shares it (815 §3.1 calls
   PREFER bit 7 "the image of `$D031` bit 7", without a source). Xander Mol's
   `TURBOCONTROLMANUAL.md`, written against real U64/U64-II/C64U hardware, and Gideon's manual both
   say `$D031` bit 7 = 1 **suppresses** the badline stalls (`TURBO_FULL = $8F` = maximum speed,
   no stalls).
   **Decided (Alex, 2026-10-03): Xander's sense for `$D031`** (1 = stalls suppressed); PREFER keeps
   the firmware's sense (1 = timing on). The two are separate registers, converted to one internal
   `bl_on` in the effective-speed function (§3.1 A2, §3.2); no parameter. Gideon's #665 facts
   quoted in TRX64 815 §3.1 still stand (VIC always has priority, BA never changed, the setting
   only decides whether the CPU halts while BA is low, external accesses at 1 MHz and only with BA
   high, 64 slots per µs on the C64U). TRX64 deviates (§11).
2. Fast I/O (several VIC/SID/CIA accesses per PHI2, like the U64 does at least for `$D020`) is a
   non-goal here. Acceptable for milestone 1, or should phase C's VIC colour writes be in scope?
   **Decided (Alex, 2026-10-03): B, in scope for milestone 1** — milestone 1 is a drop-in
   replacement of Gideon's core, so turbo I/O must behave like his. Behaviour reference: TRX64
   spec 868 (`docs/_archive/868-a-colour-per-pixel-under-turbo.md`). Phase C (VIC colour writes
   faster than one per C64 cycle, at least `$D020`/`$D021`) becomes part of T29's milestone-1
   scope; the spec's §1 non-goal is revised accordingly.
3. `$D030/$D031` decoded exactly (no mirrors at `$D070` etc.): fine, or measure first?
   **Decided (Alex, 2026-10-03): exact decode**, as TRX64; the mirrors show the normal VIC
   value. (Consequence of Q2: milestone 1 needs up to 64 MHz on `clk2x` and pixel-exact VIC
   colour registers for UPic — see Q9.)
4. App UPDATE after a program wrote `$D031`: menu wins (our A3) or the program's value stays
   (TRX64)?
   **Decided (Alex, 2026-10-03): the last write wins**, program (`$D031`) or menu (UPDATE) alike.
   TRX64 to align (today it keeps the program's value until reset); if Gideon's core is shown to
   differ, both follow it.
5. The menu still offers 20-64 MHz and we deliver at most 16 (A) / 32 (B). Leave Gideon's menu
   text and report the delivered speed only in `TURBO_STAT`/trxmon/T32, or patch the app's table
   for our build?
   **Decided (Alex, 2026-10-03): A**, Gideon's menu unchanged; the delivered speed is reported in
   `TURBO_STAT`, trxmon and later T32. Target is the full table: `clk2x` (63.05 MHz) is exactly 64
   clocks per C64 cycle (dot4x = 32 per PHI2), the same 64 slots per cycle as the C64U.
6. Interrupt latency in turbo: natural (sampled every turbo cycle), or TRX64's "two PHI2 cycles"?
   The natural one is cheaper and is what a fast 6502 does.
   **Decided (Alex, 2026-10-03): as TRX64** (two PHI2 cycles) — expensively tested there. Test
   material on disk: Xander Mol's Ultimate projects in `/path/to/Development/u64-emulator/run/xander/`
   (mandelbrot-upic, UltimateDemo2026 incl. `TURBOCONTROLMANUAL.md`, heartbeat-demo, GeoUTools,
   UBoot64), as used by the UE2 session for TRX64's turbo gates (`crates/trx64-core/tests/u64_turbo_gate.rs`,
   `subpixel_colour_gate.rs`, `cia_tod_gate.rs`).
7. T20 window collapses to ~1 s at 16 MHz. Accept for now (turbo and timewarp rarely together), or
   pull T20's coalescing (its open question 10) in before T29?
   **Decided (Alex, 2026-10-03): A — the ring does not record above 1 MHz.** It pauses
   automatically while the CPU runs faster ("Timewarp not available in turbo" in menu/REST) and
   records again at 1 MHz. No coalescing for now ("langsam rantasten": the ring for the 1 MHz
   machine first). §6.1; the turbo write tap and drain packing are gone.
8. `.c64re`: add a `turbo` field to `trx64Hw` and ask the TRX64 side to read it (and to offer an
   "I/O stretched" mode for A/B against our hardware)?
   **Decided (Alex, 2026-10-03): A** — a `turbo` field (enable word, PREFER, `$D030`, `$D031` incl.
   bit 7, `d031Written`, `scpuSlow`, hold); we write and read it, TRX64 reads it on load (§11). No
   "I/O stretched" mode: we build fast I/O like Gideon (Q2). A single dump works in turbo; the ring
   does not (Q7). §6.2.
9. Phase B (32 MHz, clk2x, timing risk, LUT at the 80 % ceiling) worth it, or is 16 MHz enough?
   **Decided (Alex, 2026-10-03): 64 MHz** (clk2x, 64 clocks per C64 cycle) for milestone 1, in
   stages: up to 16 MHz on the 31.5 MHz clock first, then 64 MHz on `clk2x` together with the fast
   VIC colour writes (UPic). T29's LUT ceiling is raised to **82 %**; timing is guarded by the CI
   WNS gate. §4, §7, §9.
10. Is a C64U/U64 with Gideon's core available for §8.4, or only TRX64 and ue2emu as references?
    **Decided (Alex, 2026-10-03): Alex's own C64U.** A menu power cycle (C= B, `U64==2`,
    `userinterface.cc:115`) boots Gideon's firmware and bitstream from flash; measure there
    (Xander's demos over REST, video/audio UDP streams, JTAG SAMPLE read-only), then load our
    bitstream and app into SRAM again and measure the same. No flash writes. TRX64 and ue2emu are
    secondary references. §8.4.

New after the 2026-10-03 revision (source review against Xander's manual and programs and the
TRX64 code):

11. **`$D031` before a program writes it.** Taken by the source rule: it reads `$00` (Xander, for
    "TurboEnable Bit" mode; extended here to "U64 Turbo Registers" mode), while the speed in force
    is the menu's (TRX64's rule, kept). On Gideon's core the menu speed might instead not apply in
    "U64 Turbo Registers" mode until a program writes `$D031` (then the machine boots at 1 MHz in
    that mode whatever the CPU Speed item says). Keep the default until §8.4 shows it?
12. **I/O cost beyond the colour, raster and turbo registers.** CIA, SID, the other VIC registers
    and colour RAM are one bus cycle each here (~1 PHI2 per access, §5.3); TRX64 charges nothing
    (851 D3, unmeasured); Gideon's manual only says *external* accesses run at 1 MHz, which suggests
    his internal chips are faster. Build the bus-cycle version for milestone 1 and let §8.4 decide
    whether phase C makes them fast (sub-cycle ports into reDIP and the CIA cores)?
13. **Where a fast colour store lands, and the phase restart.** kawari's pipeline fixes the pixel a
    store paints relative to the PHI2 edge; Gideon's may differ by a few pixels, which moves UPic's
    picture (Xander bisected his row delay `$87` on hardware, the corner markers need ~+54 pixels of
    it). Also TRX64 868 §5a claim 2 (a `$D031` write restarts the sub-PHI2 phase) is not built
    (C7). If §8.4 shows an offset, correct it with a constant delay in the side port (cheap), or
    leave it as measured?
14. **Stage 2 fallback.** If the one-clock CPU cycle at 63 MHz does not close timing, the delivered
    maximum is 32 MHz (two clocks on `clk2x`) and UPic does not run. Acceptable as an intermediate
    milestone-1 state, or is 64 MHz a hard gate for milestone 1 (then the memory side needs another
    design, e.g. a 126 MHz RAM clock)?
15. **Ring content across a turbo span.** Default: the ring restarts when it resumes at 1 MHz and
    the frames before the turbo span are dropped (they cannot be reached by undo). Or keep the old
    span as a separately restorable segment from its own keyframe (T20 change)?

---

## 11. Report to TRX64

**Status 2026-10-03:** TRX64 adopted all items (measured findings 1-7, the `.c64re` turbo block, per-pixel stores for `$D020-$D02E`, IRQ/NMI latency in turbo-clock cycles) as TRX64 spec 890 (`docs/890-turbo-as-measured.md` in the TRX64 repo), relayed via the C64U_emu session; build pending there.

Deviations of TRX64 from the references, found in this revision; TRX64 is read-only from here.

1. **`$D031` bit 7 sense.** TRX64 treats `$D031` bit 7 = 1 as badline timing **on**
   (`vic.rs` `u64_speed()`, `(v & 0x7f, v & 0x80 != 0)` applied to `$D031` and PREFER alike; gate
   `u64_turbo_gate.rs` `d031_80_is_one_mhz_with_badline_timing_and_not_turbo`; 851 §3/D2, 815 §3.1
   "PREFER bit 7, the image of `$D031` bit 7"). Xander and Gideon: 1 = stalls **suppressed**.
   PREFER keeps bit 7 = 1 = timing on. Fix: invert bit 7 for the `$D031` source only.
   (UPic runs with `DEN=0`, so its `$80`/`$8F` resync is unaffected.)
2. **`$D031` read before a write.** TRX64 returns PREFER verbatim (`u64_d031()`); Xander: `$00`.
   With item 1 fixed, a PREFER image would also carry the wrong bit-7 sense.
3. **Menu UPDATE vs `$D031`** (Q4): the last write wins; TRX64's `set_u64_turbo` keeps
   `u64_d031_written`, so the program's value wins until reset.
4. **`.c64re` turbo block** (Q8): read `trx64Hw.turbo` on load and restore enable word, PREFER,
   `$D030`, `$D031`, `d031Written`, `scpuSlow` and the reset hold (today 851 §8: not in the dump;
   `c64re_snapshot.rs` recomputes the divider from `$D031` alone), and write it on dump.
5. **Stale comment.** `crates/trx64-core/tests/cia_tod_gate.rs` header says that on the `u64`
   profile "the timers and the raster are CPU-clocked and TOD is the only thing left carrying real
   time". TRX64's code does the opposite, correctly: `clk_inc` advances `clk`, alarms and the VIC
   only every `turbo_div`-th CPU cycle (`c64_6510core.rs:829-878`), and
   `u64_turbo_gate.rs` `time_is_phi2_time_at_every_speed` asserts it. The comment repeats
   Xander's manual sentence, which his own programs contradict (C2).
6. **Colour registers other than `$D020`.** TRX64 resolves sub-PHI2 stores for `$D020` only (868
   §6/§7); the U64's VIC — and kawari — sample all colour registers `$D020-$D02E` per pixel. Not a
   bug for the corpus, a gap for `$D021`/sprite-colour tricks.

---

---

## Appendix: what is copied from TRX64

| TRX64 | Here |
|---|---|
| 851 D2: enable word, PREFER, U64-II table, register visibility per enable bit | §3.2 (bit 7 of `$D031` not copied, §3.3 C1) |
| 851 D3: the PHI2 clock stays the time base; CPU cycles counted separately | §5.0, §6.3 `CYCLES` vs `CPU_CYCLES` |
| 851: badline timing off = the turbo CPU does not wait for BA (`check_ba` skipped) | §5.5 |
| 851 §8 / `lib.rs:2789-2798`: reset clears `$D030/$D031`, firmware settings survive | §3.2 |
| 851 §4 / `c64_6510core.rs` `clk_inc`: interrupt delay counted in PHI2 cycles | §5.6 (Q6) |
| 868 §5a claim 1: adoption at the next PHI2 edge | §3.2, §8.1 test 11 |
| 868 §1-§4: one `$D020` store per pixel at 64 MHz | §5.3 (by kawari's per-pixel sampling, no slot array) |
| BUG-061: 2.06 s 1 MHz hold after reset, strobe kept | §3.2 |
| `u64_extra_read`: `$D0BC-$D0BF` read `$00` | §3.2 |
| `u64_turbo_gate.rs` tests (loop counts, PHI2 time, IRQ at 48 MHz, resync pair) | §8.1 tests 3, 4, 8, 11 |
| Not copied: no I/O stretch for all I/O (851 D3) — CIA/SID/other VIC are bus cycles (§5.3, Q12); 868 §5a claim 2, phase restart (C7); `$D031` read = PREFER (C3) |  |


---

## 12. As-built design (build of 2026-10-04) — wins over §3-§7 where they differ

Rule as in §3.3: measured on Gideon's core (§10a, §10b, `docs/turbo-measurements-gideon.md`) beats every
statement. §12 lists what the build takes from the measurements, the architecture decisions made while
reading the code, and what is left out of the first build with the reason. It is kept current as the code
is written (§12.9 is the log).

### 12.1 Registers as measured (replaces §3.2's register rules)

State (clk2x, `ge`-enabled, see §12.3): `regs_en[2:0]`, `prefer[7:0]` (the app), `d030_b0`, `d031[7:0]`,
`hold[22:0]`. There is no `d031_wr`, `scpu_slow` or phase restart.

- **One value register `d031`.** Reset: `d031 = $00`, `d030_b0 = 0`, `hold = 2^22`. `menu = {~prefer[7],
  prefer[6:0]}` is what the menu would put in `$D031` (measured: 16 MHz + Badline Timing Disabled reads
  `$89`, Enabled `$09`; bit 7 of `$D031` = NOT "Badline Timing"). UPDATE strobe (every config change in
  "U64 Specific Settings", also an unrelated item): `d031 := menu`, `d030_b0 := 1`.
- **Speed in force** (one place):
  ```
  if !regs_en[0]                         idx = prefer[3:0], bl_on = prefer[7]   (Off and Manual)
  elif regs_en[2] && !d030_b0            idx = 0,          bl_on = 1            (TurboEnable Bit, bit off)
  else                                   idx = d031[3:0],  bl_on = !d031[7]
  hold != 0                              idx = 0,          bl_on = 1
  N = table[idx], table = 1 2 3 4 6 8 10 12 14 16 20 24 32 40 48 64 with 48 -> 47 and 64 -> 63 (measured)
  ```
  The turbo engine runs when `N > 1 || !bl_on` (measured: `$80` is 1 MHz **without** the stalls, not
  cycle-exact); otherwise the machine is exactly the 1 MHz machine of today.
- **`$D030/$D031`** decode: `$D0xx` with `a[5:0] = $30/$31`, any `a[7:6]` (mirrors `$D070/71 $D0B0/B1
  $D0F0/F1`, writes act). `$D031` reads `d031` with `regs_en[0]`, else `$FF`; a write sets `d031`.
  `$D030` reads `{7'h7F, d030_b0}` with `regs_en[2]`, else `$FF` (Manual/Off/Registers mode: `$FF`, writes
  have no effect); a write of bit 0 = 1 with `d030_b0 = 0` sets `d030_b0` and `d031 := menu`, a write of 0
  clears `d030_b0` and `d031 := 0`; a write of 1 with `d030_b0 = 1` changes nothing.
- **`$D07A/$D07B`** (`regs_en[0]` or `[2]`; `a[7:0]` in `7A 7B FA FB`): `$D07A` sets `d031 := 0`, `$D07B`
  sets `d031 := menu`; reads `$FF`.
- **`$D0BC`** (`regs_en[1]`; `a[5:0] = $3C`, any `a[7:6]`): reads `$01`, else `$FF`; `$D0BD-$D0BF` read `$FF`.
- Reset hold: 2^22 PHI2 cycles after every C64 reset (REST `machine:reset`), strobes inside it are kept.
- **Menu text unchanged** (Q5): the app's table is Gideon's, indices 14 and 15 give 47 and 63 CPU cycles per
  PHI2, as measured; the real clock at index 15 is `63 * 985,248 = 62.07 MHz` (PAL), 65 x 63 x ... in the
  60 Hz modes (the slot grid is 64 per PHI2 in every System Mode, §12.3).

### 12.2 Decisions that close Q11-Q15 and change §5-§6

| # | Decision | Source |
|---|---|---|
| D1 | **No stage 1.** The CPU runs on `clk2x` from the first build (64 slots per PHI2, `N` up to 63). Q14: no 32 MHz fallback. | Q14 (Alex) |
| D2 | Q11 as measured (§12.1): registers mode reads `$00` and stays at 1 MHz until a `$D031` write or a menu change. | §10b |
| D3 | Q12: **writes** to the VIC registers (all of `$D000-$D02E`, incl. mirrors) are posted fast writes; `$D011/$D012` reads are fast with the live raster and no extra cycle; other VIC reads, SID, CIA, colour RAM, IO1/IO2, cartridge are bus cycles in the first build (§12.6 lists the gap against Gideon). CIA/IO1/IO2/UCI are bus cycles on Gideon's core too (1.000 PHI2). | §10b |
| D4 | Q13: no pixel offset correction; the pixel grid follows PHI2 within about one CPU cycle on Gideon's core, ours follows the PHI2 edge the same way (the colour is taken by kawari's own per-pixel sampling). | §10b |
| D5 | Q15: the ring is not kept across a turbo span. The core reports a sticky `turbo_ran` bit (TURBO_STAT) that the app uses to restart the ring; the hardware only pauses `log_on` while the turbo engine runs (§12.7). | Q7, default of Q15 |
| D6 | Q6 as measured (§10a): IRQ and NMI reach the CPU unsampled-delay (natural, at the CPU's own rate in turbo); no age gate. | §10a |
| D7 | **Stops leave turbo first.** Any request that stops the machine at an exact point (the gate: `halt_req`, a position stop, the debugger's `hold`/break sources; the app's CPU-only pause) switches the engine back to the normal 1 MHz mode at the next PHI2 falling edge; the stop itself is taken at the edge after that. So `.c64re` dumps, snaps, debugger stops and freeze sessions see exactly the machine of today (CPU state vector, `din_r`, no sub-PHI2 state), and the T21 units need no turbo mux. Cost: a stop in turbo comes up to two PHI2 cycles late. While any T21 unit/step/bitmap/CYC_CMP is armed the engine stays off (N = 1). §6.2-§6.3 (park, `CPU_CYCLES`, `cpu_edge` mux) are not built. | architecture |
| D8 | The turbo state is written into the state bus as chip 0 words 1 and 2 (§12.7) for dumps; restore writes it through `TURBO_SET`. | §6.2 |

### 12.3 Clocks and the slot grid

- **CPU core on `clk2x`** (the SID engine's clock, `sid2x_clk`, 2 x dot4x, same MMCM, **not gated**), with
  clock enables; no second BUFGCE (the spec's §5.8 gated copy is dropped). The rest of the C64 stays on
  `gclk`. `c64_turbo` makes two clk2x-domain strobes from c64_core's `ce` (the BUFGCE's enable for the next
  gclk edge, a clk register): `run2` (this clk2x edge passes: `ce && ce_s`, `ce_s` = `ce` sampled on the
  previous clk2x edge, so the edges inside a stop and the half edge before a restart do not pass) and
  `g_edge` (`run2` on the clk2x edge that coincides with a gclk edge: `ts == tg`, `tg` toggles on every
  passing gclk edge, `ts` is `tg` sampled on clk2x). At normal speed the core's clock enable is
  `g_edge && phi2_l`: the same edge as today. Measured in the sim: the boot to READY takes the same
  2,163,267 cycles as main and the CPU's bus trace is equal over the first 300,000 cycles.
- `cpu6510.v`: `proc_core` and the CPU port registers (`$00/$01`) run on `clk2x`; `din_r`, `rdy_r`,
  `irq_r`, `nmi_r`, `pin_r`, the floating-bit counters stay on `gclk` (they count PHI2). `sb_ld` reaches the
  core as `sb_ld && g_edge`, once, on the opening edge as today. The block RAM's bytes enter `din_i` in the
  last mux only (`din_early` = write byte, port, `din_r`).
- **The arbiter sees the CPU through a stage on `gclk`** (`addr_a`, `we_a`, `dout_a`, `pout_a` in
  `c64_top`): the arbiter's deep decode is gclk to gclk (30 ns), only wires cross from clk2x. At normal speed
  the stage follows the CPU every clock (one clock later than before; bus trace identical, above). In turbo
  the stage is parked (address `$0000`, read) unless a slow access holds it, so no chip is selected by a
  CPU address that only a fast cycle uses (Decision A6 of §5.4).
- **Slot grid.** 64 clk2x edges per PHI2 cycle in every System Mode. DDA: `acc` (6 bit) is cleared at the
  PHI2 falling edge (`g_edge && phi2_n`), `acc += N` at every passing edge, a carry is a slot; the edge
  of the falling edge is a slot too (so the last slot is always on the edge: the CPU is
  between cycles at every PHI2 edge). `N` = 63 gives 63 of 64 edges. Measured in the sim (INC abs / BNE
  loop, 9 CPU cycles per iteration): N = 1, 2, 4, 8, 16, 32, 63 give N x the 1 MHz rate within 2 %
  (badline stalls cost 5.5 % with timing on, nothing with it off), N = 63 gives 62.8 cycles per PHI2.
- **Adoption** of a new `N`/`bl_on`/engine mode is at the next PHI2 falling edge. UPic's `$80`/`$8F` pair
  inside one PHI2 cycle never has `$80` in force at an edge.
- **Mode hand-over.** Normal to turbo at a falling edge `E`: the CPU's pending cycle was a bus cycle
  whose `din_r` was latched at `E`; the first turbo step takes it (`slow_data`, below). Turbo to normal at `E`: the
  CPU's pending address is presented to the bus in the next half and the first `phi2_l` step is skipped,
  the second completes it. Leaving is held back one PHI2 cycle while a slow access completes at `E`.
- **Port B of `c64_mem`** is the turbo CPU's memory port, clocked on the falling edge of `clk2x`
  (half a CPU cycle after the address, data to the CPU before the next edge). `dbg_*` (JTAG, freeze
  responder, trx_io DMA, text layer) enter through a stage on `clk`; `dbg_req` (top.v `port_req`) marks a
  request, which takes the first falling edge after it rises (and again when the staged access changes) and
  whose data is held in `dbg_rdata`. The turbo CPU loses that falling edge: `tb_took` denies its next step
  (one slot per outside access) and it reads its address again. ROM reads use an address from the address
  bits only (`$A000-$BFFF` BASIC, `$D000-$DFFF` char, `$E000-$FFFF` KERNAL), the PLA only selects the data.
  The four lane bytes go to the CPU raw and are picked by `cpu_addr[0]` (AND-OR with early selects).
  If the falling edge does not close timing the RAM clock moves to a phase-shifted MMCM output (CLKOUT2,
  DRP words for the four System Modes in `mode_ctl.v`).
- **Port B writes.** The CPU's write goes into the block RAM half a cycle after the step, with the byte
  from `cpu_dout_early` (the core's data out without its two late sources, `proc_registers`
  `data_early`: a few LUTs behind registers). Two kinds of byte are not ready that early: a read-modify-write's
  result (the 6502N has one write cycle after the read, the shifter's output is the byte) and the SHA
  family's AND (`data_late` from `proc_core`). Those are copied on the next clk2x edge (`wd_late`) and written
  half a cycle after it; if that edge is a slot the CPU holds it (one CPU cycle more, only above 32 cycles per
  PHI2, where slots can be adjacent: INC abs is 7 cycles at 63, 6 below). First CI run (2026-10-04, run 335):
  the shifter result to the block RAM's data pin was 9.1 ns for the 7.9 ns half cycle (-2.4 ns); this
  split is the fix.
- **Slow access** (CIA, SID, the VIC's non-fast reads, colour RAM, ROML/ROMH, open bus, `$DE00-$DFFF`,
  a write to `$FF00`): the CPU holds (`go` low) with its address stable; the issue logic loads the gclk stage
  with it at a gclk edge in the first 11 edges after the PHI2 fall (24 for the CIAs and the SID, which take
  the access during the whole high phase; the VIC latches its address at RAS), the access runs in the next half as
  at 1 MHz and ends at the falling edge that ends it if `cpu_rdy` held (BA high, CPU not paused, no REU
  DMA, no cartridge wait; otherwise it is issued again). The byte is copied from `din_r` into `slow_data` on
  the first clk2x edge after that fall (`din_r` is overwritten at every fall; the CPU may wait many cycles
  for BA). Two CIA reads back to back at 63 MHz cost 1.000 PHI2 each (sim: 0.94 iterations per PHI2 with
  the badline share), as on Gideon's core.
- **IEC window** (`c64_turbo.v`, parameter `IEC_WIN` = 100): any change of ATN, CLK or DATA as the CIA 2 drives
  them (`cia2_iec_lines` from the arbiter, synchronised into clk2x) opens a window of `IEC_WIN` PHI2 cycles,
  renewed by the next change. While it is open the slot grid gives the CPU one step per PHI2 cycle (the DDA's
  carry slots are dropped, the slot on the falling edge stays), so every CPU-timed gap between two bus writes is
  that of a 1 MHz C64 (the drive samples at 1 MHz). RAM/ROM/fast classes keep their port-B path; only the
  cadence changes, no mode switch. VIC bank writes to `$DD00` that leave bits 3-5 alone do not open it. Outside
  the window (waiting loops for the drive, everything else) the CPU runs at the selected speed. The value 100
  covers the longest gap between two writes in the KERNAL's send path (about 70 us at 1 MHz). **Not Gideon's
  measured mechanism** (his core's `$DD00` costs are in §10b, his IEC behaviour is only known as a result:
  `LOAD"$",8` works at every speed); the fix is ours and is judged by the LOAD, not by a copy.
- `go` in turbo: the CPU steps on a slot unless a read waits for BA (`bl_on` only), the outside took the
  falling edge, REU DMA runs, or a slow access has not completed. IRQ and NMI are the raw lines.

### 12.4 Fast classes (the fast PLA decode, §5.2 item 3)

A copy of the arbiter's PLA `casez` on `cpu_addr` with the live `$01` bits (`cpu_pout`), `game`, `exrom`
(after the app's Ultimax) and the VIC's `ba` taken as high (I/O is decoded whatever BA does; BA acts on `go`).

| Class | Addresses | Turbo |
|---|---|---|
| RAM | the PLA's RAM, writes under ROM | port B |
| ROM | BASIC, KERNAL, char reads | port B ROM half |
| PORT | `$00/$01` | inside `cpu6510` (the RAM write underneath as at 1 MHz) |
| TREG | §12.1 | register file, no bus cycle |
| VRAS | `$D011`/`$D012` reads (mirrors in `$D000-$D3FF`) | live raster, no bus cycle |
| VIC write | `$D000-$D3FF` writes | posted: side port in `registers_scaled.v` sharing the bus write's `case`, held two clk2x cycles for the clk handover |
| SLOW | everything else in I/O, ROML/ROMH, open Ultimax ranges | bus cycle |

### 12.5 Timing and state of the fast path

`$D020-$D02E` are sampled by kawari once per pixel (§5.3); the VIC write port makes a store a per-pixel
colour; the fast read of `$D012` returns kawari's raster counter without latency beyond one clk2x flop.

### 12.6 Not built in the first build, and the gap to Gideon's core

As built, gap to Gideon's core after the t29-gap work (2026-10-10, sim numbers in the history): CIA 1/2 accesses except the ICR are posted
(a lone read costs ~0, back to back one PHI2 each, BA low does not hold them; not the port registers' reads, which wait for PHI2's rising edge as the CIA samples its pins there), VIC register reads (not `$D01E/$D01F`), colour RAM and SID
accesses are fast (`fr_q`, the colour block RAM's port A, the SID's op FIFO; Gideon 0.065-0.082 PHI2 per access at 64 MHz, ours 0.25-0.33 for a loop
of 15 CPU cycles, i.e. the loop itself plus 0-5 CPU cycles per access). Not built: BA-independent IO1/IO2, UCI, ROML/ROMH, `$D01E/$D01F`
and the ICR (still bus cycles, waiting for BA high and for a window: IO1/IO2 14 gclk edges after the fall, the rest 12);
a slow access completes at the fall and the CPU takes the byte one slot after it (Gideon in the fall slot), so a loop of exactly one PHI2
with a held access costs one PHI2 more; SID write bursts of different registers drain at one op per C64 cycle after 32; `run_prg`'s shorter hold is not
reproduced (the cause is not known: Gideon's value is a different path of the firmware, not derivable from the C64 side, 2026-10-03 measurement). Measured on the board (history 2026-10-10) what is still different from Gideon's core: the top index 62.65 of 63.01; a store costs one CPU cycle more than a load above 32 cycles per PHI2 (`reg_axy` is a late byte in `proc_core`); colour RAM reads 0.127 PHI2 (0.082); IO1/IO2 1.0216 (1.0001); a CIA port read at `$80` costs one PHI2 more. T21 stops, steps and watch units are exact only at normal
speed (D7): the engine falls back for them.

### 12.7 Interfaces to T20/T21 and the app

- `TURBO_STAT` (trx_sys page 0, 0x140, R): `{hold active b31, engine drives the CPU b30, badline timing in
  force b29, turbo ran since cleared b28 (sticky), stops force normal speed b27, CPU cycles per PHI2 b22:16,
  enable word b10:8, $D030 bit 0 b7, speed index b3:0}`. `TURBO_CTRL` (0x144, W): b0 clears "ran".
  `TURBO_W1` (0x148, R, live) `{$D031 b31:24, $D030 bit 0 b11, enable word b10:8, PREFER b7:0}`, `TURBO_W2`
  (0x14C, R) `{0, reset hold left in PHI2 cycles}`. `tools/trxdbg.sh turbo` prints them.
- **State bus, chip 0 word 1** (one packed word, as T20 §17.5 asks): `{hold[22:8] b31:17, $D031 b16:9,
  $D030 bit 0 b8, enable word b7:5, PREFER bit 7 b4, PREFER index b3:0}`. Captured with the shadows; a
  restore stages it through the same word (gate shut) and loads it with SB_LOAD bit 0 at the next opening
  edge (the hold in units of 256 PHI2 cycles, PREFER bits 6:4 are not kept: nothing reads them). The engine's
  default capture table is unchanged (58 words, T20's sims and file layout stay): **the app adds the table
  entry** `{count 1, chip 0 word 1}` (CAP_TAB_IDX 9, CAP_TAB_LEN 10), which makes it block word 58.
  T20's `turbo` node (`.c64re trx64Hw.turbo`) is then: `enable` = word b7:5, `menuIndex` = b3:0, `menuStalls`
  = b4, `d031` = b16:9, `d030` = `{6'h3F, 1, b8}`, `resetHold` = b31:17 << 8. T29 has no `d031Written` or
  `scpuSlow` (§12.1).
- `log_on` to `c64_core` is `ring_on && !turbo_on` (top.v): while the engine drives the CPU the ring takes
  no new frame; the RAM writes of that span are not logged (port B), so the ring cannot be walked back
  across it. `turbo_ran` (TURBO_STAT b28) tells the app that this happened; the app restarts the ring
  (LOG_CTRL b8) and clears the bit. A stop first returns the machine to normal speed (D7), so a snap or a
  `.c64re` dump never sees a turbo cycle.

### 12.8 Test status and results

Simulation (Verilator, `designs/c64_basic/sim/turbo`, `build.sh`, `run.py`; the hold shortened to 5000 PHI2
cycles, `TURBO_HOLD`), 2026-10-04, all pass (80 checks):

| Test | Result |
|---|---|
| regs | enable word 0: `$D030/$D031/$D0BC` read `$FF`; registers mode: `$D031` and its mirrors read `$00` after the UPDATE of prefer `$80`, `$85` after a write, `$D07A` 0, `$D0FB` menu value; UPDATE prefer `$89` reads `$09`, `$09` reads `$89`; `$D0BC` `$01` and `$D03C/7C/FC`, `$D0BD` `$FF`; TurboEnable bit: `$D030` `$FF`, `$FE` after a write of 0 with `$D031` `$00`, `$FF` and the menu value after a 1 |
| speed | N = 1 2 4 8 16 32 63 (`$D031` index 0 1 3 5 9 12 15), badline timing on and off: INC abs/BNE loop = N x 1 MHz within 2 % (63: 62.8 cycles per PHI2; 5.5 % badline loss with timing on, none with it off; index 0 with bit 7 set runs 1.00 without stalls) |
| phase | CIA2 DDRB write and read back at 64 different phases against the PHI2 cycle, 5 speeds, 0 mismatches |
| irq | raster IRQ 10 in 10 frames and CIA timer IRQs in PHI2 time (98 of 98.2) at index 0, 5, 9, 15, both badline senses |
| stress | random `$D031` changes every iteration with RAM, CIA, VIC register and stack checks: 0 errors in 786 iterations |
| upic | eight `$D020` stores 8 cycles apart on one line: at 63 seven one-pixel runs (the eighth is overwritten by the next store 6 cycles later), at 32 two-pixel runs, at 16 four-pixel runs |
| state / hold / cpupause / halt | the packed state word reads and restores (`$D031` `$8C`, 32 cycles after the restore); the hold keeps 1 MHz for HOLD cycles after a reset and registers mode stays at 1 MHz after it; a CPU-only pause stops the program and the engine, 40 gate stops and resumes at random times with 0 errors |
| Lorenz (1 MHz, the core's own bus path) | boot to READY in the same 2,163,267 cycles as main; ADCB, ASOZ, MMU, CPUPORT, CPUTIMING, BRANCHWRAP pass; with the 6526 build IRQ, NMI, ICR01, IMR, ONESHOT, LOADTH, CNTO2 pass; SHAAY times out at 30 M cycles in this sim (main: see 12.9) |
| IEC / drive (`sim/drive`, `build.sh`, `TURBO=<idx> tb_drive load`) | C64 core with drive A on the serial bus: LOAD"$",8, LIST, LOAD"$",9 at index 0, 3, 9, 15: 0 failures (before the IEC window index 3: ?DEVICE NOT PRESENT) |
| regression sims (normal speed) | `sim/statebus` (state bus, capture, freeze sessions, halt, text layer) passes; `sim/dbg` (T21: stops, steps, watch units, bitmap) passes; their tops tie `dbg_req` and drive `clk2x` now |

Board and CI: §12.9.

### 12.9 Build log

- 2026-10-04: §12 written from a read of `cpu6510.v`, `c64_core.v`, `c64_top.v`, `c64_bus_arbiter.v`, `c64_mem.v`, `trx_dbg.v`, the kawari register file.
- CI 335: WNS -2.7 ns (RMW/shifter byte to block RAM data pins, trst to WEBWE). Fixed with the `data_early`/`data_late` split in the core and registered `game_q`/`exrom_q`.
- CI 342: WNS -3.619 ns. CI 350 (full reports): the 60 worst paths all start in the clk domain (`app_s2` through `rsp/start`, the cartridge model's tag RAM, `xc_wait`) and end on the core's registers through `cpu_rdy`: 18 ns of path against 15.86 ns, because the core moved to clk2x. Second group: block RAM port B, address -1.1 ns, WE -1.5 ns, data -0.5 ns, 80 to 87 % of it route (the staged-request register with 65 loads, 2.7 ns).
- Fix (CI 354): `cpu_rdy` is a dont_touch net and `build.tcl` gives every path through it that ends on a clk2x register two clk2x periods (multicycle 2 setup / 1 hold). This is exact: the core samples RDY at normal speed only on `g_edge && phi2_l`, in turbo RDY is tied high, and the engine's slow-access completion looks at it only at `pn` or while the CPU stands still on `slow_wait`. `cpu6510.v` gives the engine `pout3_q`, the CPU port's bits 2:0 as a register (decoded from the next-state of `pdir`/`pout_i`; the input pins lag one clk2x edge), so the PLA loses two LUT levels before the address. `c64_mem.v` keeps the external-access select in three dont_touch registers (address, write enable, data pins). `build.tcl`: `phys_opt_design -directive AggressiveFanoutOpt` after placement, and a pblock for `c64/mem` that excludes clock region X1Y0 (run 352 failed in placement: 13 clocks in X1Y0, [Place 30-410]; c64_mem's block RAMs carry gclk, clk and clk2x).
- **CI 354 (t29-turbo a59020a): timing met, WNS +0.082 ns**, bitstream `t29-turbo-latest`. Utilization of that run not recorded (the report is published only on failure); run 352's failed netlist had 28,328 LUT (86.9 %), 70 of 75 block RAM tiles. Main was merged again afterwards (T31 pipeline, T53): CI of the merged head pending.
- CI 497 (integ-m1 88bf94d, EF3 + reSID8 + T25.5 + A08): WNS -0.033 ns (passes: ExtraTimingOpt -0.065, AltSpreadLogic_high -0.199, ExtraNetDelay_high -0.033), 25,443 LUT (78 %). Worst path: `cpu6510_0/CPU_6502N.cpu/regs/adl_reg[4]` -> `c64/mem/ram_hi_reg_0_1/WEBWE[0]`, group sid2x_mmcm, requirement 7.930 ns (rise to fall of clk2x), data path 6.688 ns, 80 % route, 7 levels (LUT4, LUT5, 5 x LUT6): the core's address mux, the `cpu_addr == $FF00` compare of `ff00w` (needs the low byte), `c_ram`, `tb_we`, `use_ext_w` select and the `b_addr[0]` lane AND in `c64_mem`. Run 493 (without reSID8) had met timing; the ~2,000 LUT of reSID8 moved the placement.
- Fix (fix-timing-integ): (1) `tb_we` in `c64_turbo.v` is `tmode && cpu_we && p_ram && (!cpu_dout_late || late_done)`, no `ff00w`: the low address byte leaves the cone of the write enable. A write to `$FF00` still goes to the bus (`c_ram` false, `c_slow`), now also lands in RAM at the turbo step like every other turbo RAM write; the bus write stores the same byte. A late byte (RMW result) is not written early there, since `late_done` is only set through `late_hold`, which still needs `c_ram`. (2) `c64_mem.v`: `b_wram_lo/hi` replace `b_wram && b_addr[0]`: the outside's lane decode (`e_wr_lo/e_wr_hi`) is registered on dbg_clk with `e_*` (same function of the same inputs), so each WEBWE pin is `use_ext_w ? e_wr_x : tb_we && tb_addr[0]` = one LUT behind `tb_we`. Benches unchanged: turbo (118 ok, 0 FAIL, output identical to the unmodified tree), dbgsess 214 checks, steal, slot.
- CI 499 (fix-timing-integ 4034b43 = 3dc3052 + the fix, workflow_dispatch; the push itself did not start a run: its head commit was docs-only): **timing met, WNS +0.198 ns at pass 1 (ExtraTimingOpt), no second pass needed**. Same content without the fix, run 498: pass 1 -0.185, AltSpreadLogic_high +0.114 kept. LUT is not printed on success.
- Board 2026-10-04 (C64U, our bitstream in SRAM, t29-app firmware, `tests/turbo-gideon/ours_speeds.py`): CPU cycles per PHI2 cycle for `$D031` 80..8F (bit 7 = 0 identical): 1.00, 1.99, 2.99, 3.99, 5.98, 7.97, 9.89, 11.85, 13.85, 15.85, 19.64, 23.49, 31.32, 38.69, 45.37, 59.80; Gideon: 1, 2, 3, 4, 6, 8, 10, 12, 13.99, 15.99, 20, 23.99, 31.98, 39.99, 46.98, 63.01. Ours is 0.3 % low at index 1 and up to 5 % low at index 15 (cause not yet analysed: the measuring loop's slow accesses and the VIC share are the candidates). CIA 2 stayed at 985.25..985.46 kHz and frames at 50/s at every speed: time base unchanged.
- Board: Lorenz with turbo Off 297/297 (all four disks, Disk3new). Lorenz in registers mode at index 0 was cut off by the board being needed elsewhere (41/297, 0 failures). Not yet run: `LOAD"$",8` at 1/4/16/64, Xander's programs, Gideon comparison items of 8.4 other than the speed table.

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

T29: Turbo: CPU up to 63 cycles per C64 cycle

Milestone M1, status ✅. Open (one line, as in the TODO table): –

Decisions: [ADR-0027 Turbo (T29) back into M1](../adr/0027-d09-turbo-back-into-m1.md); [ADR-0018 Turbo out of the budget](../adr/0018-d02-turbo-out-of-the-budget.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- **2026-10-10 night, T29 done: full Lorenz on the merged core passed (Claude, package integ-m1-49847451 = CI 634 with T62 + T29 gap, WNS +0.183 ns, SRAM + staged app, Turbo Control U64 Turbo Registers): 297/297 ok, ANEB included; die 74.7-75.1 C. Same package: tests/t62_board.py 8/8, tests/turbo-gideon/iec_board.py LOAD/LIST ok at 1, 4, 16, 64 MHz.** Left as known differences to Gideon (§12.6): top speed 0.6 % low, a store costs one more CPU cycle above 32 cycles per PHI2, run_prg's shorter hold not reproduced.
- **2026-10-10, t29-gap merged into integ-m1 (with T62):** gaps 1-3 of §12.6 closed or narrowed (board: top speed 62.65 of Gideon's 63.01 cycles per PHI2, SID/VIC/colour RAM 0.07-0.13 PHI2, CIA reads 1.0001); gap 4 (run_prg hold) not built; WNS +0.026 ns, +510 LUT. Known edge: in turbo a write to $D7FF goes into the SID op FIFO when a SID is mapped at $D7xx and then does not reach the T62 debug register (1 MHz and the default SID addressing are not affected). Left: a full Lorenz run in turbo on the merged core (only disks 1, 3, 4 ran on the branch: 84 ok, 1 timeout ANEB as on integ-m1).
- **2026-10-10 (agent, branch t29-gap, board solo over REST/JTAG): the speed gap is closed on the board; CI numbers.** Package `t29-gap-4a0febe2` (CI run 632, bitstream built from 4a0febe2, app 4a0febe2's, loaded with `tools/load.sh` + `trxdbg boot staged`, speaker off, nothing written to flash). **CI:** WNS +0.026 ns (run 631, bitstream of d4f01b8, the commit before the CIA port fix: +0.101; integ-m1 c2c404a3: +0.106), LUT 26,684 (81.85 %) against 26,174 (80.29 %) on integ-m1 c2c404a3 = +510 LUT, registers 31,336 against 31,322, block RAM 68.5 tiles (unchanged). **Speed table** (`tests/turbo-gideon/ours_speeds.py`, the method of the 2026-10-04 table; CPU cycles per PHI2 cycle for `$D031` 80..8F; before -> after, Gideon): 1.00, 1.99->2.00, 2.99, 3.99, 5.98->5.99, 7.97->7.99, 9.89->9.98, 11.85->11.98, 13.85->13.97, 15.85->15.97, 19.64->19.96, 23.49->23.95, 31.32->31.92, 38.69->39.87, 45.37->46.86, 59.80->62.65 (Gideon 1, 2, 3, 4, 6, 8, 10, 12, 13.99, 15.99, 20, 23.99, 31.98, 39.99, 46.98, 63.01): the top index lost 5.1 % before and loses 0.6 % now, every other index 0.1-0.4 %. CIA 2 985.24-985.38 kHz and 50.0-50.5 frames per second at every index: the time base is unchanged. Data `tests/turbo-gideon/data/ours/speeds_d031.jsonl`. **IEC** (`tests/turbo-gideon/iec_board.py`, `/USB1/t29.d64` on drive A, Turbo Control Manual, reset, 6.5 s, `LOAD"$",8` and `LIST`): 1, 4, 16 and 64 MHz all ok (LOAD, directory with `T29TEST`, 662 blocks free). **Cost of an access** (`tests/turbo-gideon/ours_io_cost.py`, Q12 of the measurements, PHI2 cycles per access, 64 MHz / 16 MHz; Gideon 64 / 16): CIA1/CIA2 read and write 1.0001 / 1.0002 (1.0001 / 1.0002); SID read and write 0.0807 / 0.257 (0.0818 / 0.257); VIC read `$D019` and `$D020` 0.0807 / 0.257 (0.0818 / 0.257), `$D011/$D012` 0.0653 (0.0818), VIC write 0.0653 (0.0653); colour RAM write 0.0661 / 0.257 (0.0653 / 0.257), read 0.127 / 0.320 (0.0818 / 0.257); IO1 read 1.0216 / 1.0369 (1.0001 / 1.0157); RAM read 0.0653 / 0.257 (0.0654 / 0.257), RAM write 0.0807 (0.0653): a store costs one CPU cycle more than a load at 63 cycles per PHI2 (`proc_core` marks `reg_axy` as a late byte, so every STA/STX/STY waits a slot above 32 cycles per PHI2; not touched). Raw table `tests/turbo-gideon/data/ours/io_cost_t29-gap-4a0febe2.txt`. At `$80` (1 MHz without stalls) a CIA port read ($xx0/$xx1) costs 5 PHI2 for 4 cycles (a held access: Gideon 4.11); the other CIA reads 4. **Lorenz** in Turbo Control "U64 Turbo Registers" (the engine idle at 1 MHz), disks 3 (Disk3new, the CIA tests) and 4: 55 of 56 ok; ANEB times out (it times out the same on integ-m1 c2c404a3, loaded on the same board, and in the 2026-10-09 results), so no regression. Disk 1 (CPU instructions) was stopped after 29 tests ok, 0 failures, to free the board; disk 2 not run (the engine is idle at 1 MHz there). The board was free (`pgrep` empty) before every step.
- **2026-10-10 (agent, branch t29-gap): fix, CIA port reads are not instant.** The CIA returns the pins of its ports as it sampled them at PHI2's rising edge (`cia_ports.sv`: `pra <= pa` at `phi2_up`), so a read right after a write to the port (the keyboard scan: `STA $DC00`, `LDA $DC01`) returned the old pins when answered at once. New bench `run.py ciaport` (CIA 2 DDRB = $FF, write PRB and read it back at once): 30 errors in 400 passes at 63, 32, 16 cycles per PHI2 and 192 at 6 before the fix. Reads of registers 0 and 1 of both CIAs are held accesses again (they complete at the next fall, which is after a rising edge); writes to them stay posted. All other CIA registers change at the falling edge only, and the throttle (one access per PHI2) puts a read after the write that precedes it. `run.py` complete: PASS, 153 ok.
- **2026-10-10 (agent, branch t29-gap): SID accesses are fast through an op FIFO (sim only).** Gap 1, third part, the one the spec hint asked for. In front of the SID's op latch (`cores/trx_sid/rtl/trx_sid.v`, the bus interface only; the engine is untouched, the default SID_MULTI build and the T25.1 engine of the sim share the code; the two-SID reDIP build keeps bus cycles): the turbo CPU pushes a write into a 32 entry FIFO of ops (sid, register, byte) with a two clk2x clock pulse (`ps_*`) and goes on; the FIFO feeds the op latch one op per C64 cycle, a bus op of that cycle goes first. A write to the register the last queued op wrote replaces that op's byte (a loop on `$D418` runs at the CPU's rate, not at the drain rate); the voice controls, AD and SR are never merged (gate, test, sync, ring and the ADSR delay bug are events). 24 or more queued ops stall the next SID write (`sid_full`, three entries of margin for the pushes in flight). A read waits until the FIFO is empty and the last op has been in the engine for two C64 cycles (`f_quiet`), then returns the read-back registers through a second read port (`frd`, same bytes as the bus read: POT, OSC3, ENV3, the bus value); a read does not update the engine's bus value (a bus read of `$19-$1C` does). The decode of the CPU's address (`fhit`) is a register in `c64_turbo.v` (`sid_hit_q`), the class is known one edge after the address is stable (`age2` also gates the slow-access issue, else a stale class would send the same write down the bus as well). The engine stays in turbo until the FIFO is empty (`leave_ok`) and fast SID accesses stop when it wants to leave (`want`), so a bus op never meets queued ops. A write takes effect up to the FIFO's depth in C64 cycles later (one cycle when the FIFO is empty). Sim, iterations per PHI2 of `run.py io` (one SID write per 13-cycle loop pass) at 63 cycles per PHI2: 0.940 -> 3.286 (the same loop with a VIC write: 3.485); `run.py slotwin` badline timing off, loop of 15 CPU cycles, PHI2 per access: SID read 1.037 -> 0.270, SID write 1.037 -> 0.270 (RAM write 0.251); at 16 MHz 2.073 -> 0.938. New bench `run.py sidw` (voice 3 envelope as the observer: gate on/off written back to back must end in the last state, a burst of 40 writes to voices 1 and 2 and the filter then gate on, a read right after a write; 1 MHz, 63, 16, 32, 6 cycles per PHI2: 0 errors). Gideon: 0.082 PHI2 (read and write, 64 MHz). Left: a burst of different registers is limited to 32 ops, then one op per C64 cycle (Gideon's UltiSID takes writes at once; the engine here takes one op per cycle).
- **2026-10-10 (agent, branch t29-gap): colour RAM is fast (sim only).** Gap 1, second part. The colour RAM's block RAM port A (`c64_top.v`) serves the VIC and the 1 MHz CPU path at `sdram_pending`, twice per PHI2 cycle; the turbo CPU uses it in every other gclk clock, one request at a time (`cq_*`: a toggle each way, `c64_turbo.v`). A write is posted at the step (the CPU waits only while the previous request is out), a read posts a request once the address has been stable for an edge and steps when the block RAM has answered (the nibble is captured in the clk2x clock the answer is seen in). A turbo access leaves `color_rd_a` low, so `ram_color_dout` stays what the VIC read (`color_hold`). The upper nibble of a read is 0 (open bus at 1 MHz: 0 in the bench's program, not constant in general). Sim, PHI2 cycles per access of a loop of 15 CPU cycles, badline timing off (`run.py slotwin`): 63 per PHI2: `STA $D800` 1.037 -> 0.251 (RAM write 0.251), `LDA $D800` 1.037 -> 0.333 (5 CPU cycles over RAM; Gideon +1); 16 MHz: write 2.073 -> 0.938, read -> 1.001. New benches: `run.py colr` (fill, read back, scroll with `LDA abs,x / STA abs,x`, `INC` and `ASL abs,x` against a model in the program; 1 MHz and 63, 16, 24, 6 cycles per PHI2: 0 errors; the fill, check and scroll takes 9002 PHI2 at 63 before, 2000 now) and `run.py colvic` (a colour RAM rewritten for ever with the same values, the VIC pixel stream of a frame at 63 and 16 cycles per PHI2, both badline senses, equals the 1 MHz picture: 0 of 156,745 pixels differ). `sim/statebus` passes. Gideon: 0.065 PHI2 (write), 0.082 (read).
- **2026-10-10 (agent, branch t29-gap): VIC register reads are fast (sim only).** Gap 1, first part. Every read of `$D000-$D3FF` except `$D011/$D012` (fast already) and the collision registers `$D01E/$D01F` (their read clears them; they stay bus cycles) returns the byte of kawari's `sb_regs` (the vector its bus output is made of) through `fr_q`, with the age rule of every fast I/O read: the byte is taken one clk2x edge after the address changed, so a read costs one CPU cycle more than RAM at 63 cycles per PHI2 (Gideon measured +1.03 cycles at 64 MHz, +0 at 16 MHz, a fixed time rather than a fixed number of cycles; ours behaves the same way). Sim, PHI2 cycles per access of a loop of 15 CPU cycles (`run.py slotwin`, badline timing off): `LDA $D019` at 63: 1.037 -> 0.270 (the same loop with `STA $D015`, already fast: 0.251); at 16 MHz 2.073 -> 0.938. New bench `run.py vicrd`: a pattern is written to every register of `$D000-$D02F` and read back at once and again in a row, at 1 MHz, 16, 32 and 63 cycles per PHI2: the tables are identical (0 mismatches of 128 entries per speed). Gideon: 0.0818 PHI2 (63 MHz).
- **2026-10-10 (agent, branch t29-gap): CIA 1/2 accesses are posted, CIA reads answer at once (sim only).** This is the cause of the speed table's 5 % at the top index (59.8 of 63, found 2026-10-08): a lone CIA read (the TOD read in the board's loop) held the CPU until the next PHI2 fall, plus a whole second PHI2 when it missed the window, Gideon's costs nothing when the bus is free and one PHI2 only back to back. Now (`c64_turbo.v` `c_cp`, `c64_top.v` `pk_g`): for every CIA register except the ICR ($xD) a read returns the byte of the CIA's own register vector (`sb_cia1`/`sb_cia2`, the vector the CIA's bus output is made of, TOD latch state included) through the registered fast I/O read register `fr_q`, and the access itself (a read's side effect such as the TOD latch, or a write) is posted: the stage is loaded as before, the CPU goes on as soon as it is (`ld_done`), the next slow access waits for the stage (one access per PHI2, as measured on Gideon's core). While `pk_g` is set the CIA's inputs come from the stage, not from the arbiter, so the access completes at the next fall whatever the VIC does with the bus (BA low) and whatever `$01` says by then (gap 2 for the CIAs). Only a DMA (REU, app) holds it (`pk_ok`). A posted write to CIA 2's port A or direction register ($DD00/$DD02, the serial bus) opens the IEC window when the stage takes it (`iec_open`), not at the line change: without that the CPU ran up to 15 instructions' worth of cycles ahead of the write's effect and `LOAD"$",8` failed at 16 and 63 MHz in sim/drive (?DEVICE NOT PRESENT, the same mechanism as 2026-10-08); with it 0/3/9/15 pass. The ICR stays a held access (its flags and IRQ line change at the fall; the CPU must not see the old line). The 1 MHz machine is untouched (`pk_g` needs the engine). Sim, PHI2-cycle numbers before -> after: dexloop with one CIA 1 read per pass (`run.py dexloop`, CPU cycles per PHI2) at 63, badline timing off 58.45 -> 62.91 (the loop alone 63.05, Gideon 63.01); at 47 44.65 -> 46.55, 32 31.12 -> 32.02, 16 15.63 -> 16.03; badline timing on at 63 58.37 -> 59.51 (alone 59.63). `run.py slotwin` BL=0 idx 15, CIA read with 1000 cycles of work: 0.29 PHI2 over a fast access -> 0.02; back to back CIA reads at 63 with badline timing off 1.037 -> 1.000 (Gideon 1.0001); `run.py io` CIA read at 16 MHz 0.470 -> 0.939 iterations per PHI2. New bench `run.py ciaseq` (writes, read-modify-writes and reads of both CIAs' DDRB in a pseudo random order against a model, idx 15/12/9/4: 300 passes, 0 errors); `run.py phase` and `icr` unchanged (0 mismatches); `run.py` complete: PASS; sim/drive `TURBO=0/3/9/15 tb_drive load`: 0 failures.
- **2026-10-10 (agent, branch t29-gap): gap 3, the issue window (sim only).** The stage load is one clock earlier: the decode of the CPU's address is registered (`sq`, `age_ok`: one clk2x edge after a step) and `st_ld` is the issue itself, so a slow access issued at gph N has its stage visible at N+1 instead of N+2 (was: `sl_st` 0, 1, 2). Windows per class, in gclk edges after the fall (the value before the edge): CIA 1/2 30 (was 24; a stage visible during the last clock before the fall is taken by the CIA read and write: `run.py phase` 0 mismatches, new `run.py icr` ICR read flag and clear 0 mismatches at 64 phases, idx 4/9/15), SID 25 and the rest 12 (same stage-visible edge as before), IO1/IO2 14 (was 11 +1; the REU registers `run.py phaseio` pass up to 22 and fail from 26, so 14 keeps 8 edges of margin; the UCI and the cartridge IO are not benched in turbo). Result (`run.py slotwin`, bl off, PHI2 per access, loop = access + NOPs + `inc zp / bne / jmp`): 16 MHz, loop of 15 CPU cycles: CIA read and write 2.07 -> 1.04 (Gideon 1.03 up to 16 cycles); 16 cycles and more still 2.07 (see 12.6). `run.py` complete: PASS, 124 ok; sim/drive `TURBO=0/3/9/15 tb_drive load`: 0 failures.
- **2026-10-09 night, Alex: no new fastloader measurement** ("Du hast doch auf Gideons Bitstream gemessen zum Turbo, warum jetzt nochmal was nachmessen ?!"): the turbo behaviour measured on Gideon's core (docs/turbo-measurements-gideon.md: IEC LOAD at every speed, load speed) is what M1 must match, and our core passed it on the board 2026-10-08; fastloaders are not a test item (non-goal, §1). Left: the measured speed gap of §12.6.
- **2026-10-09 evening, design-loss investigation (agent, board autonomous, nothing flashed):** full Lorenz in Turbo Registers mode at 1 MHz on t72-io-off-f72c567d (app a2ec48fe), EF1 in the port but not active: 297/297 ok in about 2 h, including the SBCB -> SBCZ step where the design was lost at noon. Die temperature 74.7-75.8 C, VCCINT 0.98 V, VCCAUX 1.707 V throughout. 400 REST machine resets with the EF1 active: no loss. Cause of the noon loss still open; event 1 had no active cartridge, so the IO-device conflict of ADR-0053 is unlikely there. Open leads, none shown: ddr2_lean calibrated once without temperature tracking (since 2026-10-06), cdce_i2c re-sends on a clocks_ok drop, the 0x09 JTAG reading seen in both losses. Nothing in the RTL triggers a reconfiguration (STARTUPE2 PROG_USR false, no ICAP). New tools/jtag_stat.py (IDCODE, INIT/DONE, STAT, BOOTSTS, COR0, WBSTAR, XADC; does not disturb the design): run it right after the next loss, before any reload.
- **2026-10-09, board (agent, solo): LOR-2 (full Lorenz, Turbo Control "U64 Turbo Registers" at 1 MHz, package main-acbd9c35 = run 589) ABORTED after 113 ok, 0 fail: the last ok was `disk1 SBCB` (12:47); the next reset step read garbage over JTAG (`read of 0x0004A0 got 0x09; is a TRX64 design loaded?`), HDMI black, no ping: the design was gone or hung. Cause open (the test after SBCB in registers mode, or something else). Board reloaded (load.sh + boot staged). Log /tmp/b/lorenz_run.log. To do: rerun from the test after SBCB in registers mode, then with Turbo Off for comparison.**
- **2026-10-08, board (Claude, solo over REST): IEC window PASSED.** Package `t29-iec-2c37adf9` (run 556), app 6f7ea03, `/USB1/t29.d64` on drive A (device 8), Turbo Control Manual. `LOAD"$",8` after a reset at CPU Speed 1, 4, 16 and 64: SEARCHING FOR $, LOADING, READY at every speed (before the fix: 4 MHz ?DEVICE NOT PRESENT, 64 MHz hang). At 64: `LIST` shows the directory (T29TEST, "TEST" PRG, 663 blocks free); `SAVE"X",8`, `NEW`, `LOAD"X",8`, `LIST` round-trips. Not tested: a long file, fastloaders. Settings restored (CPU Speed 1, Turbo Control "U64 Turbo Registers").
- **2026-10-08 (Claude, branch t29-iec): IEC under turbo, cause found and fixed in simulation; board test pending.** Cause: the KERNAL's serial send loop times the CLK pulse with CPU instructions between writes to `$DD00` (4 NOPs plus the read-modify-write, 20 us at 1 MHz). A turbo CPU shrinks that to 5 us at 4 MHz (the CIA 2 accesses stay 1 PHI2 each, all else is fast); the 1541's receive loop (`$EA0B`, `LDA $1800 / EOR / LSR / AND / BNE`, 14 cycles) misses the pulses, the frame is never acknowledged and the KERNAL gives ?DEVICE NOT PRESENT (about 1.3 ms after the byte), at 16/64 MHz the pulses are shorter still (the board's hang there is taken to be the same mechanism; the pre-fix run was only done at 4 MHz). Evidence: sim/drive bench with the C64 core in turbo and drive A (`TURBO=3 IEC_LOG=n LOAD_ONLY=1 tb_drive load`): before the fix CLK high 5.0 us per bit, no frame ACK, ATN released at +9162 us with the DATA line high = DEVICE NOT PRESENT; after: CLK high 25 us. Fix: IEC window (§12.3), commit 013fb0b. Sim after the fix (`TURBO=<idx> tb_drive load`, test disk, LOAD"$",8 + LIST + device 9): index 0 (1 MHz), 3 (4 MHz), 9 (16 MHz), 15 (63 cycles) all `load ok, 0 failures`, listing identical. Before the fix index 3 failed as on the board. sim/turbo `run.py` after the fix: PASS (118 checks). Not Gideon's mechanism (unknown; his core is only measured as a result), so the board decides: LOAD at 1/4/16/64 MHz, SAVE, a fastloader is not expected to work. Speed table 5 % low at 64 MHz: **cause found, not fixed.** The board's measuring loop (turbomeas, K = 1) is one pass of 256 x DEX/BNE (1292 cycles = 20.5 PHI2 at 63) plus a CIA 1 TOD read per pass; our slow access holds the CPU for a whole PHI2 cycle (no overlap of CPU work with the bus cycle), Gideon's core overlaps it (§10b, 'work between CIA reads is free up to ~62 cycles'). One lost PHI2 per pass = 1/21.5 = 4.9 % at index 15, 3.6 % at 14, 0.9 % at 9, 0.15 % at 1, which is the board's shape (59.8 of 63, 45.4 of 47, 15.85 of 16). Sim (`run.py dexloop`): the loop alone gives 63.05 at index 15 with badline timing off; with one `LDA $DC08` per pass 58.4. It is the §12.6 gap (an op FIFO / posted CIA access), a design change, left open.
- **Board 2026-10-08 morning (Claude alone, CI run 514):** T29-FF00 passed: REU stash and fetch each triggered by a write to $FF00 (command $80/$81), page compare equal, 3 runs each at 1 MHz and at 64 MHz (Turbo Control Manual). T29-SPD measured, unchanged: x 59.81 at the top index against Gideon 63.01, equal up to 8x (data/ours/speeds_d031.jsonl).
- **Board 2026-10-07 evening (Alex, Freeze UI; CI run 514, package integ-m1-e2b2ca47, app 301ef8c3, trxmon.u2a built at app 9a1b996b = same trxmon source as integ-m1-app): IEC under turbo still broken (same as 2026-10-04).** Turbo Control Manual 4 MHz: `LOAD"$",8` gives ?DEVICE NOT PRESENT at once; 64 MHz: hangs (no return). 1 MHz works (blank D64 /USB1/t29.d64: SAVE"TEST",8, LOAD"$",8, LOAD"TEST",8). Compare on Gideon's core during SMP-PAN.
- Task text: Turbo: CPU up to 63 cycles per C64 cycle (index 15 = 63, 14 = 47 as on Gideon's core, measured), RAM/ROM, own registers, `$D011/$D012` reads and all VIC register writes fast, everything else a bus cycle; stops leave turbo first; spec docs/specs/T29-turbo.md §12 (as built). Branch t29-turbo (+ app t29-app). Sim green (registers incl. mirrors, speeds 1-63 x, phase, IRQ, stress, UPic, state words, hold, gate stops; Lorenz sample at 1 MHz), CI run 354: WNS +0.082 ns (cpu_rdy paths two clk2x periods, registered port bits, c64_mem pblock; §12.9). Board 2026-10-04: speed table 1..59.8 x against Gideon's 1..63, time base unchanged, Lorenz Off 297/297. App: turbo node from the core, a file's turbo state restored, ring restarts after a turbo run (t29-app). Not built: fast SID/colour RAM/other VIC reads, CIA during VIC DMA (§12.6).
- **In main 2026-10-06 (ba060ae; decision 9; the app side t29-app is in integ-m1-app). Board 2026-10-06 (Alex): turbo 1-64 MHz without a disk passes; Xander's UltimateDemo2026 detects Turbo 64 MHz, all parts stable.** Turbo stays off by default (Turbo Control Off, CPU Speed 1 MHz in the app's config defaults; the engine runs only after the user picks a speed or a program writes $D031). **Open:** (1)
- **IEC under turbo is still broken** (board 2026-10-04, T29 bitstream: 4 MHz ?DEVICE NOT PRESENT at once, 16/64 MHz hang at SEARCHING FOR $; Gideon's core keeps the bus working, docs/turbo-measurements-gideon.md round 4; spec §6.7's 'breaks as on a U64' is wrong, cause not found, no drive in the turbo bench); (2) registers-mode Lorenz, LOAD, speed table 5 % low at 64 MHz (2026-10-04 board, cause open); (3) CI numbers: integ-m1 run 499/500 (EF3, reSID8, A08, sampler, timing fix) WNS +0.198 ns; run 502/504 (Dag curves) 25,976 LUT, BRAM 68.5. CI 497 (integ-m1 88bf94d) failed timing, WNS -0.033 ns, path `adl_reg` -> `$FF00` compare -> `tb_we` -> block RAM WEBWE (half cycle, 7 levels); fixed on branch fix-timing-integ (`tb_we` without `ff00w`, registered lane decode in `c64_mem`), CI 499: WNS +0.198 ns at pass 1 (498 without the fix: -0.185 pass 1, +0.114 after the alt pass); details in spec §12.9.
