# Turbo reference measurements on Gideon's core (C64U, 2026-10-03)

Reference facts for T29 (`docs/specs/T29-turbo.md`), measured on Alex's C64 Ultimate with the
flashed bitstream and firmware (FPGA reconfigured from flash with `openFPGALoader -c usb-blaster
--reset`; `/v1/info`: product "Ultimate 64-II", firmware 3.15, fpga_version 125, core_version 1.50,
no `trx64` object, so not our core). PAL. Nothing was saved to flash.

Tools: `tests/turbo-gideon/turbomeas.asm` (one PRG, driven over REST through a mailbox at
`$C000`) and `tests/turbo-gideon/tm.py` (REST driver). Raw results in `tests/turbo-gideon/data/`,
screenshots (VIC stream) in `tests/turbo-gideon/shots/`.

## Method

- Real-time reference: CIA1 TOD tenths (50 Hz input). Cross-checked against the VIC frame count
  and CIA2 timers; all three agree at every speed (below).
- CPU speed: a RAM-only loop (K × 256 × `DEX/BNE` = 1286·K + 32 cycles per outer iteration;
  I/O reads only once per outer iteration) counted for 2.0 s of TOD. `x` = cycles per second /
  985,248 Hz, i.e. CPU cycles per PHI2 cycle. The formula is accurate to about 0.1 % (1 MHz,
  display off: 1.00; index 13: 39.99).
- Display on = `$D011 = $1B` (25 badlines per frame), off = `$0B`.
- CIA2 timer A free running from `$FFFF`, timer B counting A's underflows: a 32-bit tick count read
  at the start and end of each measurement.
- IRQ latency: CIA2 timer A with latch 19655 (period 19656 = one PAL frame, phase locked to the
  raster). Per sample: poll `$D012` until the line, read the timer (reference); then a raster IRQ
  on the same line one frame later, the handler's first instruction `LDA $DD04`
  (vectors in RAM, `$01 = $35`, main loop `LDA zp / BEQ`). Result = reference − IRQ reading
  in PHI2 cycles, 64 samples.

## Q-A: `$D031` bit 7

Bit 7 = **1 removes** the badline CPU stalls; bit 7 = 0 keeps C64 badline timing.

| `$D031` | display | x |
|---|---|---|
| `$0F` | on | 59.54 |
| `$8F` | on | 62.93 |
| `$0F` | off | 63.01 |
| `$8F` | off | 63.01 |

With bit 7 = 0 and the display on the loss is 5.43 % at every index (outer counts 1411/1492 at
1 MHz, 1446/1529 at 16, 1425/1508 at 64) = 25 badlines × ~43 PHI2 / 19656: the CPU halts for the
whole BA-low window, as at 1 MHz. **This also holds at index 0:** `$80` with the display on runs
1.00 (no stalls), `$00` runs 0.945. So "1 MHz" with bit 7 = 1 is not cycle-exact C64 timing.

Menu (Turbo Control = Manual): "Badline Timing = Enabled" = stalls (0.94 / 15.12 / 59.54 at
1/16/64), "Disabled" = no stalls (1.00 / 15.99 / 62.93). In "U64 Turbo Registers" mode a menu
change is readable in `$D031` in the same sense: 16 MHz + Disabled reads `$89`, 16 MHz + Enabled
reads `$09`, 64 + Disabled reads `$8F`. So `$D031` bit 7 = NOT(menu "Badline Timing"). Gideon's
manual is right; T29 §10 Q1 "1 = badline timing on" is wrong for `$D031` (what the firmware's
internal PREFER bit means cannot be seen from the C64 side).

## Q-B: CIA timers

CIA2 timers count at **PHI2 (0.985 MHz) at every speed**: 985,250-985,950 ticks per TOD second
at all 16 indices, display on/off, both bit 7 settings (expected 985,248; the spread is the
±1-outer-iteration end-of-window quantisation). Xander Mol's manual ("CIA timers and raster
counter clocked at the CPU frequency") is wrong for this core.

## Q-C: raster counter and picture

The raster counter runs in real time: 100-101 frames per 2.0 s TOD window at every index
(50.0-50.5 per s; PAL 50.12), counted from `$D011` bit 7 transitions. The picture stays stable at
all speeds (VIC stream screenshots of BASIC, Xander's demos, UPic Mandelbrot at 16/64 MHz).

## Q-D: interrupt latency

Values: IRQ-handler read − poll read, PHI2 cycles (64 samples; lines `$35`/`$33` raw readings were
offset by 3528 because the poll matched line 309/307 first, corrected here; a few 1/2 MHz
readings are off by 256 from the lo/hi timer read race and are corrected too).

| `$D031` | line `$35` | line `$33` (badline) | line `$FA` |
|---|---|---|---|
| `$00` (1 MHz) | −4..+5 | +38..+48 (badline stall) | −4..+5 |
| `$81` (2 MHz) | −2..+2 | −2..+2 | −2..+2 |
| `$85` (8 MHz) | 0 | 0 | 0 (2× −1) |
| `$89` (16 MHz) | 0 / +1 (50:50) | 0 / +1 | 0 / +1 |
| `$8F` (64 MHz) | 0 | 0 | 0 |
| `$0F` (64 MHz, badline timing on) | 0 | 0 | 0 |

`$D012` at handler entry was always the compare line. At ≥ 8 MHz the handler's first I/O read
lands in the **same PHI2 cycle** as the read after a polling loop sees the line change. The IRQ
is taken within less than one PHI2 cycle of the raster event (natural turbo-cycle sampling plus
the I/O read's PHI2 sync); a fixed "two PHI2 cycles" latency would show ≥ 2 here. At 1 MHz the
spread is the normal 6502 instruction-end jitter of both paths.

## Q-E: speeds, reset, `$D031` vs menu

Index → x (bit 7 = 1, display off; display on gives the same; MHz = x × 0.985248):

| index | menu | x | index | menu | x |
|---|---|---|---|---|---|
| 0 | 1 | 1.00 | 8 | 14 | 13.99 |
| 1 | 2 | 2.00 | 9 | 16 | 15.99 |
| 2 | 3 | 3.00 | 10 | 20 | 20.00 |
| 3 | 4 | 4.00 | 11 | 24 | 23.99 |
| 4 | 6 | 6.00 | 12 | 32 | 31.98 |
| 5 | 8 | 8.00 | 13 | 40 | 39.99 |
| 6 | 10 | 10.00 | 14 | **48** | **46.98** |
| 7 | 12 | 12.00 | 15 | **64** | **63.01** |

Every index gives exactly that many CPU cycles per PHI2 cycle, except 14 (47, not 48) and 15
(63, not 64) — repeated with K = 32/47/48/64, always 46.96-47.01 and 62.97-63.01. Real clock at
index 15: 62.08 MHz.

Menu and `$D031` ("U64 Turbo Registers" mode, monitor ring per 0.5 s, `data/last_write.txt`):
program writes `$85` → 8.0; menu CPU Speed 32 → 31.97, `$D031` reads `$8C`; program writes `$83`
→ 4.0; menu CPU Speed 32 again (same value) → 31.97, `$8C`; menu Badline Timing Enabled →
`$0C`, 30.2 (display on). **The last write wins**, and a menu PUT applies even when the value
does not change. Speed changes take effect within the 0.5 s sample.

Mode readbacks: Turbo Control Off and Manual: `$D031` and `$D030` read `$FF` (Manual still runs at
the menu speed). U64 Turbo Registers: `$D030` reads `$FF`. TurboEnable Bit mode not tested.

After a C64 reset (`runners:run_prg`):
- Manual, 16 MHz: 1 MHz for about 1.3 s after the program started (the post-reset hold), then 16.
- U64 Turbo Registers, menu 16 or 64: `$D031` reads `$00` and the CPU stays at 1 MHz with
  badline stalls (0.95) indefinitely (> 6 s observed); the menu's speed applies only after a menu
  change or a `$D031` write. In this mode the menu value is not a power-on default for programs.

## Q-F: Xander Mol's demos

Settings for the runs (memory only, restored after): U64 Turbo Registers, CPU Speed 16, Badline
Timing Enabled, REU on 16 MB, Command Interface on, Ultimate Audio mapped. Files on
`/USB1/xander/`. The demos look for their data in `idi8b/...` at a drive root (or the Home
Directory, which did not help), so the music/songs were not found: they were not placed at the USB
root.

- **UltimateDemo2026 v1.0.1** (`udemo2026.prg`): detection screen "UCI [OK] ultimate-ii dos v1.2,
  Type ultimate 64-ii, REU [OK] 16 MB, Turbo [OK] Turbo, Audio [OK] v16, Music [Fail] Not found".
  After a key all parts ran with a stable picture: gears ramp 1-2-4-8-12-20-28-36-48-64 MHz
  (labels from the demo), Mandelbrot (hires, then multicolour), 3D grid with a bouncing ball,
  wireframe cube, plasma-like colour patterns, tunnel. Shots `shots/udemo_*.png`.
- **mandelupic** (`mandelupic.prg`, config set by REST instead of its `.cfg`): renders the full
  UPic Mandelbrot in about 6-8 s of wall time, then browse mode; picture stable and identical in
  every shot. Shots `shots/mandel_*.png`.
- **heartbeat-demo**: detection "UCI [OK], REU [OK] 16 MB, Turbo [OK] 64 MHz, Audio [OK] v16, Song
  [Fail] Not found"; after a key it exits to BASIC (no song). Shots `shots/hb*.png`.

## Open

- What Gideon's firmware writes into its internal PREFER register (whether bit 7 is inverted
  before it reaches the core) is not visible from the C64; only `$D031`'s sense is measured.
- Why index 14/15 give 47/63 instead of 48/64 (one slot per PHI2 reserved?) — measured, not
  explained.
- I/O cost, TurboEnable Bit mode, `$D07A/B`, `$D0BC`, mirrors and the post-reset hold: see
  round 2 below.
- Heartbeat song playback and UltimateDemo2026 music need the data at the USB root.

---

# Round 2 (2026-10-03): I/O cost, colour-store pixel, registers, reset hold

Same board and core (flash reload, `/v1/info` core 1.50, no `trx64` object), same rules; config
restored and re-read identical to `data/orig_configs.json`. New tooling: `turbomeas.prg` command 5
runs a host-written routine at `$4000` between two CIA2 tick reads (`tm2.py` builds the routines),
`resethold.asm` (reset probe), `rowscan.py` (colour runs per row of a VIC-stream PNG). Raw data:
`data/io_cost.txt`, `data/io_cost_filler.txt`, `data/q13_pixels.txt`, `data/leftovers.txt`.

## Q12: cost of an I/O access in turbo

Loop of 64 × `LDA abs` / `STA abs` to one address, × 256 × 4, timed in PHI2 ticks (CIA2), display
off, `$D031` bit 7 = 1. PHI2 cycles per access (instruction plus 1/64 of the loop overhead):

| Access | 64 MHz (`$8F`) | 16 MHz (`$89`) | 1 MHz (`$80`) |
|---|---|---|---|
| RAM read / write `$C0F0` | 0.0654 / 0.0653 | 0.2570 | 4.11 |
| BASIC / KERNAL ROM read | 0.0653 | 0.2570 | 4.11 |
| **CIA1** read `$DC00` / write `$DC03` | **1.0001 / 1.0001** | **1.0002** | 4.11 |
| **CIA2** read `$DD00` / write `$DD03` | **1.0001 / 1.0001** | **1.0002** | 4.11 |
| SID read `$D41B` / write `$D418` | 0.0818 / 0.0818 | 0.2570 | 4.11 |
| VIC read `$D011`, `$D012`, `$D019`, `$D020` | 0.0818 | 0.2570 | 4.11 |
| VIC write `$D015`, `$D018`, `$D020`, `$D021` | 0.0653 | 0.2570 | 4.11 |
| Colour RAM read / write `$D800` | 0.0818 / 0.0653 | 0.2570 | 4.11 |
| `$D031` read | 0.0818 | 0.2570 | 4.11 |
| **UCI** read `$DF1C` (Command Interface on) | **1.0001** | **1.0157** | 4.11 |
| **IO1** read `$DE00` (nothing mapped) | **1.0001** | **1.0157** | 4.11 |

- **CIA1, CIA2, UCI and IO1/IO2 are bus cycles:** exactly one PHI2 cycle per access back to back.
- **SID, all VIC registers (not only the colours) and colour RAM are fast.** Writes cost the same
  as RAM. At 64 MHz a read costs one extra CPU cycle, 0.0164 PHI2 = 1.03 × 1/63, and at 16 MHz
  nothing extra. `$D011`/`$D012` reads do **not** cost a bus cycle, and they return the live
  raster: `$D012` read every ~520 CPU cycles at 64 MHz steps 01 01 02 02 02 03 …
- **Overlap.** CPU work between two CIA accesses is free while it fits (64 MHz, `data/io_cost_filler.txt`):
  - Reads: up to about 62 of the 63 CPU cycles of a PHI2 cycle stay at 1.000 PHI2 per access;
    from 64 cycles of work the cost is 2.000.
  - Writes: free only up to about 44–50 cycles, and from 52 cycles of work the cost is 2.000.
  - So a write has to be issued about 12 CPU cycles (~190 ns) earlier in the PHI2 cycle than a
    read to catch the same bus cycle.
  - Fast-I/O accesses (VIC `$D012` read, SID write) just add their own cost to the RAM work, with
    no PHI2 quantisation.
- **Badlines (display on).** With bit 7 = 1 nothing changes, including for CIA and UCI accesses
  (CIA1 read 1.0001, UCI 1.0001): BA does not block them. With bit 7 = 0 the costs rise:
  - RAM +10 %;
  - CIA1 read and UCI read 1.061;
  - VIC `$D012` read +9.6 %;
  - `$D020` write +5 %.
- **Colour writes faster than one per PHI2:** yes, see Q13. Eight stores 8 CPU cycles apart give
  eight different one-pixel colours in the border on one line.

## Q13: where a fast `$D020` store lands

The program and the scan:
- **Sync.** At 64 MHz (`$8F`, display off, so the whole line is border) the program polls `$D012`
  for the line, then does N × `LDA $DC0D`. Each CIA read is exactly one PHI2 cycle, so after them
  the CPU sits at a fixed phase right after a PHI2 edge E, and N moves the stores into the visible
  part of the line.
- **Stores.** After a delay of d CPU cycles (NOP/BIT), it stores colours 1..8 to `$D020`
  8 CPU cycles apart (`LDA # / STA / NOP`), then the base colour.
- **Scan.** One VIC-stream screenshot is scanned per row. Image row 84 = raster line 100.
  x below = stream pixel; the stream is 384 pixels wide.

Results (`data/q13_pixels.txt`, `shots/q13_delays.png`, `shots/q13_calib.png`):

- **One store, one pixel.** Each store paints exactly one new pixel: eight stores give the runs
  `1 2 3 4 5 6 7 8`, one pixel each. Stores 6 cycles apart (`LDA #/STA` back to back) lose one
  colour in 8: two stores in one pixel, and the later one wins. **The VIC samples `$D020` once
  per pixel.** "8 cycles = 1 pixel" holds only on average (63 cycles = 8 pixels per PHI2 cycle,
  so 7.875 cycles per pixel).
- **N × CIA reads.** N = 24 / 25 / 26 puts the first store (d = 0) at x = 89 / 97 / 105:
  8 pixels per PHI2 cycle, as expected.
- **Cycle → pixel (N = 25).** c = CPU cycle of the store's write, counted from the end of the
  last CIA read; `LDA #` (2 cycles) + `STA abs` (write in its 4th cycle) gives c = d + 6.

  | d | 0 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
  |---|---|---|---|---|---|---|---|---|---|
  | c | 6 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
  | first pixel | 97 | 98 | 98 | 98 | 98 | 98 | 98 | 98 (a later store doubles one pixel) | 99 |

  So pixel boundaries fall at c ≈ 7.0, ≈ 14.9 (and, by the 7.875 step, ≈ −0.9): one pixel per
  7.875 cycles. **The pixel grid is aligned to the PHI2 edge within about one CPU cycle.** The
  phase was the same every frame: the rows are identical in each screenshot, and repeated
  screenshots agree.
- **A 1 MHz store at the same point.** `STA $D031 #$80`, then `LDA # / STA $D020` at 1 MHz, then
  `#$8F`. The store paints from x = 137 = 40 pixels (5 PHI2 cycles) after the turbo store at d = 0.
  The colour lasts 48 pixels (6 PHI2 cycles) until the switch back to turbo and the base store.
- **The phase-restart claim (TRX64 868 §5a claim 2) was not tested separately.** At 64 MHz the
  `$80`/`$8F` pair above is consistent with a speed change adopted at the next PHI2 edge.

## Leftovers

**Mirrors.** `$D030/$D031` are mirrored every `$40` in `$D000-$D0FF`: `$D070/$D071`,
`$D0B0/$D0B1` and `$D0F0/$D0F1` read the same as `$D030/$D031`. **Writes to a mirror act:**
`$D071 = $89` gave 16×, `$D0B1 = $8C` gave 32×, and `$D031` then read `$8C`.

**TurboEnable Bit mode** (menu 16 MHz, Badline Timing Disabled):
- **After reset:** `$D030` = `$FE`, `$D031` = `$00`, 1×.
- **`$D030 = 1`:** reads `$FF`, `$D031` reads `$89` (the menu value), runs 16.00×.
  **`$D030 = 0`:** back to `$FE` / `$00` / 1×.
- **`$D031` is writable in this mode too:** with `$D030` = 1, `$D031 = $83` gave 4.02× and read
  back `$83`.
- **A menu change** (CPU Speed 32) gave `$8C` and 32×.

**`$D030` in U64 Turbo Registers mode** reads `$FF`, and writing `$00` has no effect.

**`$D07A` / `$D07B`** (registers mode and TurboEnable mode alike):
- `$D07A` (write) sets `$D031` to `$00` (reads `$00`, 1×).
- `$D07B` sets `$D031` to the **menu** value (`$89`, 16×), **not** to the value the program
  wrote before. In TurboEnable mode after `$D031 = $83`: `$D07A` gives 1×, `$D07B` gives 16×.
- `$D07A/$D07B` read `$FF`.

**`$D0BC`.** It reads `$01` with SuperCPU Detect Enabled and `$FF` with Disabled.
- **Side effect: any config PUT in "U64 Specific Settings" re-applies the menu speed.** Changing
  SuperCPU Detect turned `$D031` from `$8C` back to `$89`. "Last write wins" (round 1) therefore
  includes unrelated items in that category.

**Post-reset hold.** `resethold.asm` is written to `$8000` with a CBM80 signature, so the KERNAL
reset routine jumps into it at once, without IOINIT. It starts CIA2 as a 32-bit PHI2 counter at its
first instruction and records when the loop gets fast.

| Reset | Setting | PHI2 ticks from first instruction to fast | Time |
|---|---|---|---|
| REST `machine:reset` | Manual 16, three runs | 4,194,236 each | 4.257 s |
| REST `machine:reset` | Manual 64 | 4,194,166 | 4.257 s |
| REST `machine:reset` | Registers mode, `$D031 = $89` written at the first instruction | 4,194,244 | 4.257 s (the write is kept and applies at the end of the hold) |
| REST `machine:reset` | Registers mode, no write | stays at 1 MHz (> 6 s) | — |

- Plus ~68 cycles of reset and KERNAL before the probe starts, **the hold is 2^22 = 4,194,304
  PHI2 cycles (4.257 s) after a REST reset**, within 80 cycles over five runs. Host-side polling
  confirms it: 1× until 4.25 s after the call, 63× from 4.55 s.
- With `runners:run_prg`, the program is ready 1.8 s after the call and the hold ends about 3.2 s
  after the call (twice, 0.1 s resolution). That is shorter than 4.26 s from any reset inside the
  call. **The hold length depends on the reset path.** BUG-061's 2.06 s matches neither (2^21
  PHI2 cycles would be 2.13 s).

## Open (round 2)

- Why `run_prg`'s hold differs from `machine:reset`'s: a different reset path or a firmware-timed
  release. Not resolvable from the C64 side with REST alone.
- The absolute VIC x of the store pixel: the stream x is relative to the stream's 384-pixel window.
  The 1 MHz comparison (5 PHI2 cycles, 40 pixels) depends on when the `$80` write is adopted.
- The extra read cycle for fast I/O at 64 MHz (none at 16 MHz) was measured but not explained.
- `$D0BC` mirrors were not checked; physical cartridge (truly external) I/O was not measured (no
  cartridge plugged).

---

# Round 3 (2026-10-03): TRX64 Spec 890 checked against the device

TRX64 branch `spec-890-turbo-as-measured` (ef907b3), gate `turbo_as_measured_gate` run here (18/18
pass), compared with rounds 1-2 and with new measurements. Same board (`/v1/info` now reports
product "C64 Ultimate", firmware 3.15, FPGA 125, core 1.50), PAL unless stated. Config changes in
memory only, restored and re-read identical to `data/orig_configs.json`; nothing saved. Helpers:
`tests/turbo-gideon/verify890.py`; raw data: `data/verify890/`.

## Raster IRQ latency, repeated (`irq.txt`, 64 samples, display on)

| `$D031` | line `$35` | line `$33` | line `$FA` |
|---|---|---|---|
| `$85` | 0 (one +1) | 0 (one +1) | 0 (one −1) |
| `$89` | 0 / +1 (33:31) | 0 / +1 (31:33) | 0 / +1 (32:32) |
| `$8F` | 0 | 0 | 0 |
| `$09` | +1 (64) | 0 / +1 (12:52) | 0 / +1 (36:28) |

At 16× the +1 in about half the samples is reproducible (round 1 gave the same), with and
without stalls. TRX64 gives 0 in every sample at 16×.

## Extra CPU cycle of a fast I/O access, by index (`fastread.txt`)

`LDA $D012` and `STA $D418` minus a RAM access, in CPU cycles (64 per block, no filler):
0 at 2×-24×, 0.02 at 32×, 0.97 at 40×, 0.56 at 47×, 1.04 at 63×. Colour-RAM writes: 0 at every
index. At 40× and 47× the extra depends on the spacing (filler 0-3 NOPs: 40× 0.97 / 0.66 / 0.41 /
0.05; 47× 0.56 / 0.27 / 0.81 / 0.61), at 63× it is 1.00-1.04 for every spacing, at 32× 0.02. So
the fast access is not a fixed cycle count: it behaves like a fixed time that a 63× cycle never
covers, a 32× cycle always covers, and 40×/47× cover depending on phase.

## IO1 / IO2 (`io12.txt`, nothing mapped, Command Interface off)

Back to back (64 per block): IO1/IO2 read and write 1.0001 at 63×, 1.0002 at 32×, **1.0157 at
16×**; CIA 1.0001-1.0002 at all three. With filler (32 per block, 7 cycles of loop on one access;
cycles per access = 4 + 2·filler):

| 63× | 1.000 up to | 1.031 | 2.000 from |
|---|---|---|---|
| IO1 / IO2 read | 26 | 28-32 | 34 |
| IO1 write | 14 | 16-20 | 22 |
| CIA1 read (round 2) | 54 | 56-62 | 64 |
| CIA1 write | 44 | 46-50 | 52 |

| 16× | 1.000 up to | 1.031 | 2.000 from |
|---|---|---|---|
| IO1 / IO2 read | — | 4-8 | 10 |
| IO1 write | — | 4-6 | 8 |
| CIA1 read | 8 | 10-16 | — |
| CIA1 write | 6 | 8-12 | 14 |

An IO1/IO2 access must be issued about 30 CPU cycles earlier than a CIA access at 63× (~480 ns),
about 7 earlier at 16× (~440 ns): the same time, for reads and writes alike. That explains
UCI/IO1's 1.0157 at 16×: the 7 cycles of loop overhead push one access in 64 past its lead.

## Registers

- `$D0BC` (`d13.txt`): `$01` with SuperCPU Detect enabled, `$FF` disabled; mirrored at `$D03C`,
  `$D07C`, `$D0FC`. `$D0BD`, `$D0BE`, `$D0BF`, `$D0FD` read **`$FF`** either way.
- `$D07A`/`$D07B` (`d12_13.txt`, registers mode, after `$D031 = $85`): a store to `$D0FA` acts
  like `$D07A` (→ `$00`, 1×), `$D0FB` like `$D07B` (→ menu `$89`, 16×); `$D03A`, `$D0BA`,
  `$D03B`, `$D0BB` do nothing.
- **TurboEnable Bit mode, `$D030 = 0`, menu CPU Speed 32:** `$D030` reads `$FF`, `$D031` `$8C`,
  32×. A menu change sets the enable bit (writing `$D030 = 1` afterwards changes nothing).
- **System Mode** (`d14.txt`): registers mode, program `$D031 = $83` (4×), then System Mode PAL →
  NTSC: `$D031` reads `$89`, 16× (the menu). The program kept running (no reset).

## NTSC post-reset hold (`ntsc_hold.txt`)

System Mode NTSC (confirmed: 17,046 PHI2 per frame), REST `machine:reset`, `resethold.bin`:
Manual 16 → 4,194,236 PHI2 ticks (twice), Manual 64 → 4,194,166: the same counts as PAL, so the
hold is 2^22 PHI2 cycles on NTSC too (4.10 s there).

## Round 4 (2026-10-04, Gideon's flashed image restored: firmware 3.15, core 1.50)

Flash 0x0-0x3FFFFF written back from `pre-trx64-flash-2026-10-03.bin` (our image saved as
`ours-before-gideon-2026-10-04.bin` in the tools fork's flash-dump folder).

**IEC under turbo:** `LOAD"$",8` (drive A on, Disk1.d64 mounted, Turbo Control Manual, CPU Speed
N, after a reset) lists the directory at every speed: Off 14.5 s, 4 MHz 9.3 s, 16 MHz 9.3 s,
64 MHz 3.6 s (time until READY, polled every 0.5 s). Gideon's core keeps the serial bus working
in turbo. Our T29 bitstream (same test, 2026-10-04): 1 MHz ok, 4 MHz "?DEVICE NOT PRESENT" at
once, 16/64 MHz hang at "SEARCHING FOR $". T29 §6.7's assumption ("breaks as on a U64") is wrong:
this is an M1 bug in T29.

**UltimateDemo2026 hardware detection** (Command Interface Enabled, REU Enabled 16 MB, turbo
Off): UCI OK ("ULTIMATE-II DOS V1.2", type ULTIMATE 64-II), REU OK 16 MB, turbo "not detected
(1 MHz)" (turbo was off), audio module not found. With UCI disabled (default) it stops at
"NO ULTIMATE COMMAND INTERFACE FOUND". Reference for T60 (16 MB REU) and the T28 UCI path.

**Load speed under turbo (clean run):** a D64 built for the test (`BIG`, 120 blocks = 30,478 B at
$2000), `LOAD"BIG",8,1` after a reset, end detected by the load end pointer $AE/$AF, 3 rounds in
order Off/4/16/64: Off 79.1 / (timeout once) / 79.1 s; 4 MHz 79.7 / 80.7 / 79.7 s; 16 MHz 79.6 /
80.1 / 79.5 s; 64 MHz 81.3 / 80.9 / 77.7 s. About 80 s (~380 B/s) at every speed: the drive sets
the pace; turbo does not speed up a standard IEC load, it only must not break it. (The 14.5 / 9.3 /
3.6 s directory times above were not comparable: drive spin-up and caching.)
