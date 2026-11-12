# T208 — Metal Dust runs on the C64SCPU core without picture faults

**Status:** BUILT in simulation (2026-11-11): S1-S3 and S5 proven in sim (review fixes of the same day included); S4 (board) is open and Alex's.
**Repo:** TRX64U-MultiCore — `cores/c64scpu/` (scpu_mem.v mirror queue, cpu65816 IRQ, c64scpu_ramd.v).
**Number:** T208.
**Origin:** Alex, 2026-11-11 (board, CMD ROM 1.4 + JiffyDOS): Metal Dust starts; still pictures fine; while it scrolls the asteroid sprites are cut and
the HUD flickers / wrong font or bitmap; the crack's menu runs far too fast at 20 MHz and badly at 1 MHz. "That has to be debugged."
**Depends on:** T182 (SCPU CMD ROM).
**Milestone:** M2.

---

## §1 The problem (static analysis of the game, 2026-11-11; nothing measured yet)

- The game: native mode, `$D07B` (20 MHz), optimisation `$D075` (mirror $40-$7F); HUD data written to bank 3 ($FB90/$FC88/$FC18) inside short
  `$D077` (mirror all) windows; raster IRQ chain via the native vector $FFEE; HUD IRQ at line $D1 does ~23 I/O writes ($D011 $6B blank, sprites,
  `$DD00`, `$D016/$D018`, `$D011`); sprite multiplexer writes pointers at $77F8; scroll writes screen $7400 and colour RAM from SuperRAM bank 2.
- The crack's menu ($0801): no SCPU registers; raster IRQ at $F3 acks `$D019` at the very end; designed around 20 MHz.
- P1 (cut sprites, HUD faults while scrolling): candidate 1 — the mirror write queue (`scpu_mem.v` ~356-395) loses or delays bytes under a dense
  stream of mirrored writes; candidate 2 — our I/O write latency in the HUD IRQ is longer than a real SCPU's, so the split runs past the blanking.
- P2 (menu too fast at 20 MHz): candidate 3 — the IRQ handler runs twice per frame (late ack, `cpu65816_c64scpu.v` ~108-111).
- P3 (menu bad at 1 MHz): may be the game's design (a real SCPU at 1 MHz may glitch too), or a DDR2 miss stretching a 1 MHz cycle (`scpu_mem.v` ~360).

## §2 Scenarios

| # | Solves | Scenario | Test | Where |
|---|---|---|---|---|
| S1 | P1 | a bench drives the game's scroll/multiplexer write pattern at 20 MHz with badlines and sprite DMA: VIC-visible RAM equals the CPU's after every frame, `n_wlost` 0 | sim (run_mirror) | sim |
| S2 | P1 | the HUD IRQ's write sequence: the clk cycle of each `$D011/$DD00/$D018` write from the IRQ, against VICE x64scpu | sim + VICE | sim |
| S3 | P2 | the menu's $F3 handler runs once per frame at 20 MHz | sim (run_irq) | sim |
| S4 | All | board: Metal Dust scrolls without cut sprites or HUD faults; the menu runs at its speed | by hand | board (Alex) |
| S5 | P1 | the HUD stage's 23 writes take at most 24 PHI2 cycles on every chain, also when the data sweep evicts its code lines between two frames; a write into a code line that is being prefetched is never lost (self-modifying code), also not in the clock the prefetch starts and not while a killed line is invalidated | sim (run_mdust hud, hudx, stz cold, smc + smc_nokill, tb_scpu_cache races + controls) | sim |

## §3 Decision

Findings of the benches (2026-11-11, `cores/c64scpu/sim/run_mdust.sh`, synthetic ROMs of our own from `tools/gen_c64scpu_mdust.py`; the access patterns are those of §1):

- found (S1, candidate 1 negative): the mirror queue holds under the game's dense stream (optimisation mode `$D075`, `$D077` windows, 16-bit stores, `MVN`, eight double-height
  sprites over a bitmap screen). 98,734 mirrored writes: 0 wrong against the VIC's copy, 0 wrong in the DDR2 at the end, `n_wlost` 0, 98,734 accepted by the queue, 98,733 landed
  (one in flight at the end). The same with the HUD IRQ chain running: 174,924 writes, 0 / 0 / 0. The picture faults are not a lost or late mirror byte.
- found (S2, candidate 2 half confirmed, cause is not the arming window): 24 back-to-back `stz $D0xx` at 20 MHz take 23 PHI2 cycles first store to last store when the code is in
  the code cache, exactly the VICE accounting (`scpu64cpu.c` `scpu64_clock_write_stretch_io_start` / `_io`, ported in `check_mdust.py vice_model`: one PHI2 cycle per I/O store); the
  address arming window (`scpu_mem.v` `skip_now`) is not touched by this and was not changed. The first, cold call takes 27: every 16-byte line of code that is not in the cache
  (`scpu_cache.v`, direct mapped, index = address bits 12:4, refill from the DDR2 about 50 clk2x) adds one PHI2 cycle, which VICE does not have (bank-0 fetches never wait there).
  The game's HUD stage (`$9A6E-$9AE4`, 23 writes from `$D011=$6B` to `$D011=$3B`): 23 PHI2 cycles when its code is in the cache, 28 when it is not (first run; every frame when
  the scroll's reads from SuperRAM bank 2 sit on the same cache indexes: the game reads `$02:0000,X`, a sweep over all of them). Whether the game's own layout evicts the stage between
  two frames is not known from the code alone; the board decides (S4). The original Protovision game (`Metal Dust CBM-1581` Disk 1, `game.bin`) has the same bytes at `$9A6E-$9AED` as
  the crack the analysis used (the two differ only at `$8C00-$8CAF`, `$9ED0-$9F1B`, `$B71E`, `$B73F`).
  Decided 2026-11-11 (coordinator, after the board S4 showed the same flicker on the original): option 1, a next-line prefetch on a program-stream miss, built (S5 below); a separate code
  cache and a 2-way cache are not built (their cost is in section 5).
- found (S3, candidate 3 negative): the menu's `$F3` handler (acknowledge `$D019` at the very end) runs once per frame at 20 MHz: 70 entries in 70 frames, none twice, none skipped,
  every entry ends. After the switch to 1 MHz (`$D07A`) the same: 120 entries over both phases, once per frame, none skipped. The menu "too fast at 20 MHz" is the menu's own design
  (it counts frames); it concerns only the crack, its cheat menu is not part of the original game.
- board: `n_wlost` is readable in the core's register page E at `0x101AE0C6` (low) and `0x101AE0C7` (high), 16 bit little endian, `RAM_COUNTERS` word 3 of
  `{vic_miss, cpu_wait, fetch, wlost, blk1}` (`c64scpu_regs.v`); it must stay 0 while the game scrolls. `0x101AE0C0/C2/C4/C8` are `vic_miss`, `cpu_wait`, `fetch`, `blk1`.

## §4 Out

- CMD FD-2000 / `.d2m` (and `.td0`) images: no FD-2000 drive emulation (Alex, 2026-11-11: "no, we do not want that", translated). Metal Dust runs from its four `.d81` on the 1581 with JiffyDOS.

Decided 2026-11-11 (coordinator, after the board S4): of the three ways to keep the HUD stage's code warm only option 1 (the next-line prefetch, S5) is built.
- Option 2, a separate instruction cache: not built. It needs a second 512 x 141 array that every CPU write must update or invalidate (a second writer; see §5 for the cost). It would make the stage immune
  to data sweeps, which the prefetch does not; the benches (`hudx`: code evicted every frame) show the prefetch already holds writes + 1, so the extra array buys nothing measurable here.
- Option 3, a 2-way data cache of the same 8 KiB: not built. It keeps a code line against one conflicting data line but not against a sweep, so it helps less than option 2 for this game, at +~130 LUT.
- Both come back only if the board (S4) shows a stage that the prefetch does not hold; then as a new decision with Alex.

## §5 Build notes

Proof lines (`sh cores/c64scpu/sim/run_mdust.sh`, builder, `trx64-sim-env:4`, `PASS run_mdust`; in `ci/sims.txt` as `c64scpu_mdust`, nightly):

| # | Result line |
|---|---|
| S1 | `mdust| scroll ok n_wlost: n_wlost 0` / `VIC copy: 0 wrong` / `DDR2 copy: 0 wrong` / `accepted = landed: 98734 accepted by the queue, 98733 landed on the C64 bus` |
| S1 + chain | `mdust| hud ok accepted = landed: 174924 accepted by the queue, 174923 landed on the C64 bus`, 0 wrong, 0 wrong in the DDR2, `n_wlost 0` |
| S2 warm | `mdust| stz ok warm total: warm calls: 23/23 PHI2 cycles first to last store, VICE model 23` and `hud ok write spacing: 0 of 9 later chains slower than writes + 1; worst 23 writes in 23 PHI2 cycles` |
| S2 cold (S5) | `mdust| stz ok cold total: cold call: 23 ... VICE model 23 (at most +1 ...)`; `hud ok first chain (cold code): 23 writes in 23 PHI2 cycles`; `hudx ok write spacing (code evicted every frame): 0 of 9 later chains slower than writes + 1; worst 23 writes in 23 PHI2 cycles` (before the prefetch: 27 / 28 / 28, measured as `info` lines) |
| S5 smc | `mdust| smc ok no stale code line executed: marker 1 ... 2 x, marker 2 ... 0 x`; the same bench on a machine built with `SCPU_MUT_NOKILL` runs in `run_mdust.sh` and must FAIL (`mdust| control smc_nokill ...: fails as it must`) |
| S5 races | `tb_scpu_cache` (iverilog, `scpu_cache.v` alone, `run_mdust.sh`): `race_a`, `race_b` (a CPU write in the clock the prefetch starts), `race_c` (a byte write while a killed line is invalidated) PASS; `SCPU_MUT_NOPGOWR` fails race_a and race_b, `SCPU_MUT_NOINVDEFER` fails race_c |
| S3 20 MHz | `mdust| menu20 ok once per frame: ... none` / `no frame skipped` / `every entry ends: 70 handler entries, 70 handler ends` |
| S3 1 MHz | `mdust| menu1 ok once per frame ... none`, `120 handler entries, 120 handler ends`, `speed switch reached`, `run reached its end` |

`run_mirror.sh` and `run_irq.sh` PASS with the bench's `+ARMDBG` print added to `tb_c64scpu.v` (it prints only with the plusarg). The S1-S3 findings were measured with no RTL changed; S5 then changed `scpu_cache.v` (below), and after its review fixes all seven c64scpu sims were run again on the final RTL (results at the end of §5). S4 (board) is Alex's.

Final RTL (2026-11-11, builder, `trx64-sim-env:4`, one after the other): `PASS` for `run_mdust` (all passes, `smc_nokill` and the `tb_scpu_cache` controls fail as they must), `run_mirror`, `run_irq`, `run_map`,
`run_speed`, `run_emu`, `run_base`, every control of each failing as it must.

Static analysis notes and listings (not in git, game data): the session scratchpad `metaldust/notes/`.

S4 board 2026-11-11 (Alex, original Protovision Metal Dust from the four `.d81`, 1581 + JiffyDOS, c64scpu.cor of main 5cc93ede, video
2026-11-11 21-31-17): the same flicker as with the crack in the last two seconds, "when many sprites are on the way and the picture scrolls"
(translated). The fault is the core's, not the crack's. Next (coordinator's proposal of 2026-11-11, built on this condition): option 1, a
next-line prefetch on an instruction-fetch miss in `scpu_cache.v`, so the HUD stage's code stays warm.

S5 (2026-11-11, `scpu_cache.v`, `scpu_mem.v`, `cpu65816_c64scpu.v`): next-line prefetch, sequential, program stream only.
- Mechanism: the CPU's `vpa` (opcode or operand fetch) goes into scpu_mem (`b_vpa`). `stream` = the line the program stream is in: set by a program-stream miss (`dgo` with `f_if`), advanced by a
  prefetch. A program-stream read (`rd_if`, the clock after the CPU's enable, with `rd_ln` = its line) in line `stream` sets `pf_pend`; the prefetch of `stream + 1` starts when the fetch engine is idle
  (`!st_wait`, no write open, `quiet == 0`, no demand miss waiting: a demand miss has priority and waits at most for the prefetch in flight) with the same S job, same `s_req_line` / `fill` / write
  port as a demand fill (the block RAM keeps one write port with the same address mux, skill section 3). The first fetch in the prefetched line moves `stream` on, so straight code is always one
  line ahead and a loop over lines that are already in is never refetched. A jump into a line that is in the cache starts nothing.
- Coherence: the CPU runs while the line is on its way (a demand fill never needs this, the CPU is held). A write (`wr_go`) to the line of the open or just finished prefetch (`pf_cur`, the open
  fetch or the 3 clocks after its fill, `rcnt`: `cq` may still show the old line and `wr_byte` would miss) sets `kill_q`: the next clock the line's index is invalidated (`inval`, the same zero word as
  the flush) and a fill still to come is dropped (`drop`, as for a flush). Writes to other lines are untouched: they hit or go through as before. A prefetch fill that meets a `wr_byte` in the same clock
  is lost (the byte write has the port); the line is simply not cached. Review fixes (same day): (a) a prefetch does not start in the clock of a `wr_go` (`pgo` has `!pg_wr`, `pg_wr = wr_go`):
  in that clock `wb` is still 0 and `kill_q` looks at the old `s_req_line` / `st_wait` / `rcnt`, so a write to line `stream + 1` would be missed and the S job (served before the SW byte lands if the two
  toggles reach the engine one ui clock apart) could fill the old bytes; held back one clock the write opens `wb` and the fetch waits for it. The smaller fix of the two (widening `kill_q` with
  `stream_nx` would add a second 21-bit compare). (b) a `kill_q` invalidate no longer takes the port from a byte write to another line (`inval = kill_q && !wr_byte`, `kill_q` stays one more clock):
  before, that byte stayed old in a cached line; the CPU's enables are at least 3 clk2x apart so it was unreachable, now it is by construction. (c) a data miss (`f_if = 0`) leaves `stream` / `pf_pend`
  alone on purpose: `stream + 1` is the next line of code the program is still running in, the fetch is plain DDR2 (the 21-bit line number wraps inside the core's 0x02000000-0x03FFFFFF window, so no
  address outside it is ever read), and what it wastes at the end of SRAM or of the ROM is one line fetch. MVN and mirror writes: MVN's writes are CPU writes through `wr_go`; the mirror queue never touches this cache.
- Proof S5: `hud` and `hudx` (the stage's code is evicted by the data sweep every frame, 10 chains each) and the first, cold chain: 23 writes in 23 PHI2 cycles on every chain (before: 23 in 28 on
  all 9 later chains of `hudx`, 23 in 28 on the cold one; `hud` warm 23). `stz` cold call: 23 PHI2 cycles (before 27; VICE model 23). `smc` (`tools/gen_c64scpu_mdust.py smcbench`): 60 code lines in
  SuperRAM bank 2 that store the pass number into the immediate of the next line, 8 passes, code evicted before every pass (a sweep of bank 3 over the same indexes): all results right. A debug
  print at the guard showed the stores hit every phase of the fetch: 125 of 480 while the prefetch was open, 28 in the 3 clocks after the fill, 334 later (a normal hit write). Negative control
  `SCPU_MUT_NOKILL` (guard off): the same bench FAILS (marker 2, a result of the previous pass); it runs in `run_mdust.sh` as `smc_nokill`. No CPU program of the benches reached the clock of review fix (a)
  (`+PFDBG` `pfcoin|` lines: 0 in `smc`; the enables are at least 3 clk2x apart), so the two races are driven on the ports of `scpu_cache.v` in `cores/c64scpu/sim/tb_scpu_cache.v` against a DDR2 model that
  serves the S job 3 clocks and lands the SW byte 6 clocks after their toggles (the worst order): race_a / race_b / race_c, then a check that whatever the cache holds of the line equals the DDR2.
- Cost (yosys `synth_xilinx -family xc7` of `scpu_cache.v` alone, before -> after): LUT 313 -> 376 (+63; 396 before the review fixes), FF 143 -> 171 (+28), CARRY4 22 -> 29, RAMB36 2 -> 2 (yosys reports no implicitly declared net). scpu_mem.v adds 23 FF (`rd_ln`, `rd_if`,
  `b_vpa`) and no logic of note. The cache read path (`cq`, `hit`, `mem_q`) is unchanged; the new paths are a 21-bit compare from registers into one flip-flop each (`rd_ln == stream`, `w_p[24:4] == s_req_line`)
  and the 21-bit `stream + 1` into `s_req_line`.
- Not built, what it would take: (2) a separate instruction cache: a second 512 x 141 array (2 RAMB36, +~120 LUT for tag compare and the fetch / data steering) that every CPU write must also
  update or invalidate - a second writer into an array that has one, so either a true dual-port inference (two write ports, to be proven in Vivado) or an invalidate-only write port; it makes the stage
  immune to data sweeps, which the prefetch does not (a jump into a stage that the sweep evicted still pays one refill, before its first write in the bench). (3) a 2-way data cache of the same 8 KiB:
  256 sets x 2 ways, the same 2 RAMB36 but a way mux over 128 bits (+~130 LUT), LRU bits, two tag compares in the multicycle path of `hit`; it keeps a code line against a single conflicting data line
  but not against a sweep (both ways are evicted by 8 KiB of data), so it helps less than (2) for this game.

S4 board 2026-11-12 (Alex, c64scpu.cor of main-736c16d9 with the S5 prefetch): FAIL, "the flicker artefacts are still there, fix attempt (1) did not help with Metal Dust" (translated). Next: measure on the board which stage misses before choosing option 2/3 or another cause.
