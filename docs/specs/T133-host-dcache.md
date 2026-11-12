# T133 — The host RISC-V has a write-through data cache, so the LVGL host application builds and renders faster

**Status:** DONE (2026-10-28, merged to main; BUILDING since 2026-10-26), from branch `t133-dcache`.
**Repo:** TRX64U-MultiCore — host (`host/`: new `rv_dcache.vhd`, our `rvlite_wrapper_dc.vhd`, `ultimate_sys.vhd`, `top.v`,
`ddr2_port.v` `mig_arb`), bench `host/sim/dcache/`, CI (trial lane filter).
**Number:** T133 (registry: the table in `TODO.md`).
**Origin:** Alex, 2026-10-26, after the analysis "data cache or BRAM draw buffer": "Dann los" (translated: "Go ahead then"). Before
that, same exchange: "analyse what we need, data cache or BRAM draw buffer. And then we make a try on a branch with only the HOST +
reference core + null core and test it quickly" (translated). That is the approval to change `host/` on this branch (the host is
otherwise frozen; a merge needs `host_pin --update` and a new decision, not now).
**Depends on:** T84 (host application), T24 §2.3 (the DDR2 cost), T115 (budget of `pb_l1`).
**Milestone:** M3.

---

## §1 The problem

The host application (LVGL on the pixel layer, `host-app/software/hostapp/hostui.cc`) is slow because the host CPU (Gideon's
`rvlite`) has an instruction cache but **no data cache**: every load from DDR2 waits about 0.4 us (T24 §2.3; `ddr2_port.v` header),
stores are posted. Measured on the board, 2026-10-26, host app `main-b5cd63d8`, console log:

- `[t+1870 ms] phase Building the menu` -> `[t+9770 ms] phase menu built`: **7.9 s** in `build_ui()` (`hostui.cc:1427`: menu,
  browser, editor, network, colour views). LVGL object creation, style lookup and layout are nearly all DDR2 reads.
- `[host] view 0: update 161 ms, handler 4190 ms`: the first full LVGL render takes **4.2 s**.

Parts (MECE):

- **P1** A load of a cacheable DDR2 address is served from a local RAM when it was read or written before (speed).
- **P2** Every store still reaches DDR2 with its byte enables, and the cached copy equals DDR2 afterwards (write-through).
- **P3** Nothing but DDR2 below the application's range is cached: I/O, the boot ROM and the ranges that other masters own are
  never cached.
- **P4** No stale read: every master that writes DDR2 memory the CPU may read invalidates the cached copy (DMA, cartridge fill,
  drive, REU/JTAG window, engine, pixel layer).
- **P5** Reset and boot: after reset or power-up the cache holds nothing valid; the boot ROM and the first instructions work as before.
- **P6** Area and timing: fits `pb_l1` (13,000 LUT, 78.24 % used = 10,171 LUT, 21.5 of 25 BRAM tiles, T115) without a routing failure
  (82 % failed before).
- **P7** On the board the same log lines get shorter.

## §2 Scenarios — what proves it is solved

Bench `host/sim/dcache/` (GHDL, `ci/sims.txt` line `dcache`, control line `dcache_ctl`): the real `rv_dcache` in front of Gideon's
`bus_converter`, a DDR2 memory model behind the bus (random latencies, posted writes, reads with `dack_tag`), a second master that
writes through the same bus (the arbiter's DMA case) and a toggle input (the DDR2-controller-side writers).

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 | Given a word was loaded once, when it is loaded again, then the second load returns in 2 clocks without a bus request (`hits` counter +1, no `request` seen) | bench `dcache` directed test `hit` | `ci/sims.txt` |
| S2 | P1 | Given a cold line, when it is loaded, then the load goes to the bus and the value is the memory's; the next load is a hit | bench `dcache`, `miss` | same |
| S3 | P2 | When a word is stored (word, half, byte; each lane), then the memory model holds exactly those lanes and a following load (hit or miss) returns the merged word | bench `dcache`, `S3` (word, half, byte) and the random run, against a byte model; controls `lanes` (a store hit rewrites all lanes) and `noupdate` (a store hit does not update the line): MUST fail | `dcache`, `dcache_ctl` |
| S4 | P2 | Given a store to a word that is not cached, then the word is not allocated (the next load is a miss) | bench `dcache`, `no_alloc` | same |
| S5 | P3 | Loads and stores at `0x10000000+` (I/O), `0x80000000+` (boot ROM) and `>= 0x00EA0000` (drive images, cart RAM, REU, RAM disk, cart ROM) never hit and never fill | bench `dcache`, `uncached`; control `io_cached` (limit raised to 4 GiB: the bench MUST report an I/O or boot ROM read served twice) | `dcache`, `dcache_ctl` |
| S6 | P4 | Given a cached line, when a second bus master writes that word, then the next CPU load returns the new value | bench `dcache`, `snoop`; control `nosnoop` (snoop input tied off: the bench MUST fail with a stale read) | `dcache`, `dcache_ctl` |
| S7 | P4 | Given a miss in flight (data not yet back), when the other master writes the same word before the data arrives (every phase, 41 delays), then the next load returns the new value | bench `dcache`, `S7`; control `oneshot` (the invalidation is written only in the first clock the other master's write is visible: MUST fail) | same |
| S8 | P4 | When the DDR2-controller-side writers raise the toggle (cartridge fill, drive, REU/JTAG window, engine, video), then the whole cache is invalid within 4 clocks and nothing is served from it while it is cleared; afterwards loads fill again | bench `dcache`, `S8` (toggle with a read open, 31 phases, then the check after the walk); controls `noext` (toggle ignored) and `fill_walk` (fills allowed during the walk): MUST fail | same |
| S8b | P4 | `mig_arb` toggles `ext_wr_tgl` once for every write into the window (word address < 0x750000) from the port (REU/JTAG), the drive and cart_fill, and not for the RISC-V's client, reads, or addresses above the window | bench `mig_arb` (`tb_mig_arb.v`); checked by hand with two mutations (client 0 also toggles, no upper limit: both fail) | `ci/sims.txt` |
| S9 | P5 | After reset the first loads of a word that sits in the RAM from before the reset are misses (the RAM is cleared by a walk after reset; no hit while it runs) | bench `dcache`, `S9`; control `nowalk` (no walk after reset: MUST fail) | same |
| S10 | P5 | Random mix of loads/stores/IO/boot accesses with random waits and random snoop events for 200,000 accesses equals a flat reference memory (every load returns the value of the latest completed write before it), and at the end DDR2 equals the reference (write-through) | bench `dcache`, `S10` (3 seeds x 300 batches) | same |
| S10b | P1, P2 | The real core with the cache (`host/rvlite_wrapper_dc.vhd`) runs the trap/interrupt program of `rvtrap` with random wait states and gets the results of the run without interrupts | bench `rvtrap_dc` (`RV_DC=1 sh host/sim/rvtrap/run.sh`) | `ci/sims.txt` |
| S11 | P6 | The host stage reports <= 300 LUT and <= 2.5 BRAM tiles for `rv_dcache` in `utilization_hier_synth.rpt`; the route of `pb_l1` has WNS >= 0 | CI stage `host` (Vivado) | CI only, not run by this branch's author |
| S12 | P7 | Same log lines on the board: `phase menu built` minus `phase Building the menu`, and `view 0: update/handler` | board, console follow | by hand (Alex) |

Negative controls are mutations of `rv_dcache.vhd` made by `run.sh` (every pattern is checked to match exactly once, or the control is
vacuous); their output lines carry the prefix `ctl| `.

## §3 Decision

Facts read from the code (not from comments), they decide the design:

- **F1 Data path.** `core.vhd` dmem (`t_dmem_req`, `execute.vhd:108-112`, combinational, held while the pipeline stalls) ->
  `bus_converter` (`bus_converter.vhd`, tag `X"21"`, `g_io_bit => 28`, boot ROM when `adr(31:16) = g_start_addr(31:16)`) ->
  `mem_req` of `rvlite_wrapper` (2-port arbiter with the instruction side, tag `X"20"`) -> `cpu_mem_req` -> the 4-port
  `mem_bus_arbiter_pri_32` in `ultimate_sys.vhd:534` (eth, wifi, usb, cpu; combinational priority, no latch) -> `mem_req` ->
  `cpu_mem_bridge` (`ddr2_port.v:238`, 26-bit byte address = bottom 64 MB, **one 32-bit word per request, no burst**) -> `mig_arb`
  client 0 -> ddr2_lean (128-bit words, the bridge takes the 32-bit lane; a read returns the whole word whatever `byte_en`).
- **F2 Protocol.** The core's write-back stage waits for `dmem_resp.ena_i` (`writeback.vhd:43`); the bus converter answers
  `ena_i = 1` in `idle` and a request offered in that clock is the one it takes. So "accepted" is "`ena_i` was 1 in the clock of
  `ena_o`", the data is valid when `ena_i` is back at 1.
- **F3 Address map** (`docs/ultimate-app-hw-spec.md` 3a, DDR2 = bottom 64 MB): app `.text/.data/.bss` and the 8 MB FreeRTOS heap
  (`new`/`malloc`: LVGL objects, RMII/WiFi/USB DMA buffers, the pixel layer's frame buffers) `0x00030000-0x00E8FFFF`;
  `0x00EA8000` kernal area, `0x00EB0000-0x00EF0000` drive sounds/images/cart RAM, `0x00F00000` built-in cart, `0x01000000` REU/CRT image,
  `0x02000000` RAM disk, `0x03000000+` updater/cart ROM. I/O: `adr(28) = 1`.
- **F4 Everything that writes DDR2** (found in `host/top.v`, `ddr2_port.v`, `ultimate_sys.vhd`):

  | writer | where it enters | clock | exact address seen at |
  |---|---|---|---|
  | RISC-V data side (tag `X"21"`) | arbiter | cpu_clk | the cache itself (write-through, stays consistent) |
  | eth MAC DMA, wifi UART DMA, USB DMA (`eth_/wifi_/usb_mem_req`) | 4-port arbiter, then `cpu_mem_bridge` | cpu_clk | arbiter output `mem_req` (`ultimate_sys.vhd`, the `snoop_*` tap at the end of the file is this signal) |
  | cart_fill (`c_*`: cartridge ROM/REU fill, 128-bit lines) | `mig_arb` | ui_clk | `mig_arb` command |
  | drive A (`drv_*`, drive_mem: 64 KB image, track data) | `mig_arb` client 2 | ui_clk | same |
  | REU and the JTAG window and the sampler (`ddr2_port`, `ddr_*`) | `mig_arb` client 1 | own clocks | same |
  | engine (`e_*`: frame ring, mover; a non-C64 core's pixels into the pixel layer's frame buffer) | `mig_arb` | ui_clk | same |
  | pixel layer (`v_*`) | `mig_arb` | ui_clk | same (it writes "a still word"; `v_wdf_wren`) |

  So the narrowest point that sees **all** of them is the command that `mig_arb` hands to the controller (`take`, `s_cmd == 000`),
  in ui_clk; the CPU's own and the three DMA masters share client 0 and are seen exactly one stage earlier, in cpu_clk.
- **F5 No bursts** on the CPU's bus (F1): one word per cache line. A line of 2 or 4 words would cost 2-4 bus reads per miss at
  ~0.4 us each; there is no way to get them cheaper.

Decisions:

- **D1 Place and shape.** New module `host/rv_dcache.vhd` between the core's dmem port and the data-side `bus_converter`, written
  like Gideon's `icache` (same port style, same bus records, a core copy `host/rvlite_wrapper_dc.vhd` of the vendored wrapper with the
  cache added). Direct mapped, **1 word per line** (F5), `g_size_bits` = 12 (4 KB, 1024 lines; 13 = 8 KB costs 3.0 tiles, see
  budget). Data RAM 1024 x 32 with 4 byte-write enables (one write port), tag RAM 1024 x 13 (valid + address bits 23:12; one write
  port). A hit returns in the clock after the request, like the icache's hit, and a following request is accepted in the same clock.
- **D2 Write-through, no write-allocate.** A store always goes to the bus as today (posted); if the line is present the cache lanes
  selected by `sel` are updated in the same clock the store is forwarded. A store to an absent line is not allocated (an LVGL
  frame-buffer write stream must not evict the object data). A read miss of a cacheable address fills the line from the bus reply
  (the whole word, F1) and the reply goes to the core from a register (one clock later than the bus converter's own would), so that
  a request accepted in the fill clock can never read a tag that is about to change.
- **D3 Cacheable window.** `adr(31:16) < 0x00EA` (generic `g_limit`): everything below the kernal area, i.e. the application and
  its heap (F3). Excluded by that one compare: I/O (bit 28), the boot ROM (bit 31), and every range that cart_fill, the drives, the REU,
  the RAM disk and the cart ROM own (F3). Why not wider: those ranges have ui_clk writers that would flush the cache all the time
  (the drive A CPU writes its RAM image at 1 MHz).
- **D4 Invalidation, two paths, no stale read.**
  1. *cpu_clk, exact:* `ultimate_sys` gives the cache the arbiter output (`request`, write, address, tag != the CPU's own tags): the
     cache registers it and writes "invalid" into the tag RAM at that index (the write is active as long as the request is
     visible; harmless repeated).
  2. *ui_clk, whole cache:* `mig_arb` toggles `ext_wr_tgl` for every command that writes (`s_cmd == 000`) from a client other than
     client 0 with a 16-bit word address below `0x750000` (= byte `0xEA0000`). The cache synchronises it (2 FFs), and an edge starts a
     **clear walk**: a counter writes "invalid" to all 1024 tag entries (1024 clocks), the cache is bypassed (every access is a miss
     without fill) while it runs, and a new edge restarts it. The same walk runs after reset (P5). Why a walk and not an exact address:
     an address across the clocks needs a FIFO whose overflow would need the walk anyway; the in-window ui_clk writers are rare
     (JTAG pokes, a non-C64 core's pixel writes into a heap frame buffer); a core that draws every frame therefore keeps the cache in
     bypass, which is today's speed, not a wrong value.
  3. *Fills in flight (no stale line from an open read):* the old reply of a read that was taken by the controller before the other
     master's write comes back after the write was announced. Two things keep it out: (a) a snooped write keeps writing "invalid" to
     the tag RAM for as long as the other master's request is visible on the arbiter output, and that is until the bridge takes it,
     which is after the open read returned (the bridge has one access open; `cpu_mem_bridge` `rd_wait`) - so an invalidation is written
     in a clock after the fill (bench S7, control `oneshot` removes exactly this); (b) the toggle starts the walk, which blocks every
     fill (`walk_busy`), and the walk (1024 clocks = 10 us) outlasts any open CPU read (`sim/lat`: a drive read through
     `mig_arb` under the worst video load takes at most 730 ns; the CPU's read goes through the same arbiter in turn). A flag that drops fills seen during an event was built first and removed:
     with (a) and (b) in place the bench cannot tell it from nothing (see §5).
  4. *Bound that remains:* ui_clk writer -> invalidation takes about 3 clocks (30 ns at 100 MHz), the write itself reaches the DRAM
     later than that; a load that hits in those 3 clocks sees data from before the write, i.e. the load happened first. The bench
     S8 tests the order after the toggle's arrival, not inside the window.
- **D5 Reset.** The tag RAM starts at zero from configuration; after `reset` the walk runs, so a warm reset after the JTAG window
  loaded a new application (events seen while the CPU was in reset) cannot leave valid lines.
- **D6 Counters.** Two 20-bit counters (hits, misses of cacheable reads), the upper 12 bits of each in the free bits
  `rv_tap(127:104)` of `ultimate_sys` (JTAG `rvpc` tap), so the hit rate can be read on the board without a new build; this
  decides whether 8 KB is worth its tiles.
- **D7 Source layout.** Gideon's files are not changed. `host/rvlite_wrapper_dc.vhd` is a copy of the vendored (patch 0005) wrapper
  with the cache; it keeps the entity name `rvlite_wrapper` so that the benches whose BFMs bind that entity keep working;
  `host/gideon_vhdl.tcl` lists it instead of `$g/cpu_unit/rvlite/vhdl_source/rvlite_wrapper.vhd`.

Budget (estimate, written before the build; S11 measures it): `rv_dcache` about 200-250 LUT (tag compare 12 bit and window compare
~10, walk counter 10 bit ~12, snoop/walk/fill write mux ~25, data write mux ~32, response mux 3:1 x 32 ~50, two 20-bit counters ~40,
state ~20), about 260 FF (request register 69, reply register 32, counters 40, walk 11, rest), RAM: data 1024 x 32 = 1 RAMB36, tag
1024 x 13 = 1 RAMB18, i.e. **1.5 tiles** (`pb_l1`: 21.5 -> 23.0 of 25). Host LUT 10,171 -> about 10,400 (80.0 %): above the 78 % at
which routing worked and below the 82 % that failed; this is the risk of the trial and the reason for 4 KB. The critical path is the
tag RAM output -> compare -> `ena_i` -> the core's stall network (the icache has the same shape); a register stage is the fix if
the route misses.

## §4 Out

- A BRAM draw buffer for LVGL (decided against: helps only pixel drawing and needs about 4 tiles `pb_l1` lacks).
- Resizing `pb_l1` or the floorplan.
- Merging to `main`: needs `host_pin --update` and Alex's go after the board numbers.
- Address-exact invalidation for ui_clk writers (FIFO) and a larger or associative cache: only if the board counters (D6) ask for it.
- Caching the instruction side's data (the icache is untouched), write buffers, cache maintenance instructions for the application
  (the host application writes code only through the updater, outside the window).
- The DFX core lanes of CI for this trial (see `docs/todo/T133.md`).

## §5 Decisions taken while building

- **Measured on the builder (GHDL 08, container `trx64-sim-env:4`, 2026-10-26):** bench `dcache` PASS for seeds 1, 2, 3 (300 batches
  each, 41 DMA phases, 31 toggle phases, directed S1-S9); the hit latency is 1 clock (request taken in clock t, data in t+1, the next
  request taken in t+1); controls `nosnoop`, `oneshot`, `fill_walk`, `noext`, `nowalk`, `io_cached`, `lanes`, `noupdate` all fail the
  bench. `rvtrap` with the cache (`RV_DC=1`, 2 seeds x 2 periods x 2 waits) PASS, 0 errors. `dma_bus` and `input` (the real
  `ultimate_sys` with the new wrapper port map and a BFM CPU) PASS; `ultimate_sys` with the `ultsys` stub goes through `ghdl --synth`
  and `gen.py` (the Verilator part of `ultsys` was not run).
- **The first design had a `nofill` flag** (drop the fill when an event arrives during an open miss). The bench with its control
  (flag removed) still passed: the invalidation that continues after the fill (a) and the walk (b) already cover every case, so the
  flag was dead logic and was removed. The first negative control for it could not fail, and was replaced by `oneshot`/`fill_walk`,
  which remove the real protections.
- **Synthesis estimate (yosys `synth_xilinx -family xc7`, the cache alone, 4 KB):** 128 LUT (LUT2 22, LUT3 38, LUT4 8, LUT5 46,
  LUT6 14), 14 CARRY4, 180 FF, no distributed RAM; yosys maps the data RAM as 4 byte-lane RAMB18 and the tag RAM as 1, 5 RAMB18 = 2.5
  tiles; Vivado maps a 1024 x 32 RAM with 4 write enables to one RAMB36 and the 13-bit tag to one RAMB18, 1.5 tiles (S11 decides).
  Both fit the budget of §3. The inference of the byte-write RAM and of the read-first same-address behaviour that the invalidation
  relies on (a read of the index that is written in the same clock may return the old tag, which is a legal order for a snooped
  write) is a Vivado fact that only the CI report shows.
- `tb_mig_arb.v` (bench `mig_arb`) PASS with the toggle checks; the two mutations of `ddr2_port.v` (toggle also for client 0, no upper limit) made it FAIL
  (run by hand on the builder, not a CI control).
- **Not tested here:** the Vivado route and timing of `pb_l1` (critical path: tag RAM output -> compare -> `ena_i` -> the core's
  stall network); the wiring in `top.v` (`ext_wr_tgl` of `mig_arb` into `ultimate_sys`) and the `ultimate_sys` snoop tap exist only in synthesis and in the
  `dma_bus`/`input` elaboration - the cache's reaction to the toggle and the snoop is tested with the bench's own models of them, and
  `mig_arb`'s toggle with its own bench; the Verilator bench
  `ultsys`; the board (S12).

## §5.x Reading of the board failure (2026-10-26, run 1008)

- The counters of run 1008 were read after the UI task trapped (the CPU in its idle loop), so "256 hits per second, misses constant" shows the idle loop, not a
  cache stuck in bypass.
- Store hit then load of the same word: not a fault. A store hit is forwarded (`fwd = '1'`, `rv_dcache.vhd` `fwd`), so the state leaves `idle` and the next request is
  captured (and its RAM read taken) only after the store's data write; same for a fill (`resp` re-reads).
- `ena_i`/`rdy` equal in every state (`rdy` line), the contract of `bus_converter.vhd` (idle = `ena_i` 1) holds; the DDR2 read returns the whole word whatever `byte_en`
  is (`ddr2_port.v` `rdata[...] <= app_rd_data[...]`), so a cached line from a byte load is complete.
- Snoop tags 0x20/0x21 are the CPU's (`rvlite_wrapper_dc.vhd` generics); ext toggle covers cart_fill, port, drive, engine, video (`ddr2_port.v` `ext_wr_tgl`).
- Not decidable by reading: Vivado's mapping of the two RAMs (collision behaviour of read and write at one address in one clock) and whether some writer is missed.
  Hence the debug build: check mode reads DDR2 on every would-be hit, returns the DDR2 word (the program stays correct), counts differences and latches the first
  (address, cached word, DDR2 word).

## §5.y Decisions 2026-10-26/27

- Alex, 2026-10-26: "ich will einen Cache. Mach den L1 teil grösser, die Cores bauen ja eh 10 mal am tag alles neu" (translated: "I want a cache. Make the L1 part bigger, the cores
  get rebuilt ten times a day anyway"); "8kb probieren. Nochmal. Wir können die Cores ja alle neu bauen." ("Try 8 KB. Again. We can rebuild all the cores anyway."); "Mach den Befehls
  Cache auf 8kb und messe." ("Make the instruction cache 8 KB and measure.").
- Alex, 2026-10-27: "Hm. Besser aber ich hatte mir viel mehr erhofft. Bauen, Cores die nicht Routen -> large Core. Die large cores sollen auch die caches bekommen für ihre UI."
  (translated: "Hm. Better, but I had hoped for much more. Build; cores that do not route -> large core. The large cores should get the caches too, for their UI.")
- Result: data cache 8 KB, instruction cache 8 KB (`rv_icache.vhd`), JTAG control 0x100060 resets to caches on. Measured on the board (host app b5cd63d8): menu build 7.9 s without caches,
  4.7 s with the 4 KB dcache, 4.4 s with the 8 KB dcache, 6.7 s with the 8 KB icache alone, 3.4 s with both.
