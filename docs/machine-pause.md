# Machine pause (T12 B)

Source: TODO.md section 4 "Machine pause (T12 B)", written 2026-10-01 (simulated, not on the
board then); moved here 2026-10-03. State of that day: T12 (`docs/specs/T12-freeze-overlay.md`),
T20 and ARCH (`docs/specs/ARCH-lean-state-access.md`) supersede it where they differ
(e.g. HDMI now shows the dimmed still, not a black window).


- How: the C64 runs on its own copy of dot4x through a BUFGCE (`top.v`
  `bg_c64`); `c64_core.v` shuts it right after a phi2 falling edge. VIC,
  6510 (mid-instruction, no RDY involved), both CIAs with TOD, SID, REU,
  cartridge (`cart_slot`), keyboard scan and the colour RAM stop on that
  edge. Drive A stops through its ticks (`drive_unit.vhd` `halt`: CPU,
  VIAs, head, GCR stream, write timer). Only `c64_mem`'s second port runs on
  (JTAG and the app's RAM DMA). icepi patch 0050 only exports
  `ext_dma_busy`; Gideon's code is untouched.
- Control: JTAG register 9 bit 3 (`trxdbg.sh pause on|off`), or the app's
  C64_STOP once `GATE_CTRL` (0x101A0100) b0 is set (arch-p1; reset value 0 =
  the old CPU-only pause until our firmware draws the freeze menu, T12 P2);
  bit 4 forces C64_STOP back to the CPU-only pause. Read bit 8 = paused.
  With b0 set a freeze is a shadow session (T12, `freeze_resp.v`): no C64
  cycle from the stop to the resume. While paused: audio muted, HDMI window black (the scaler's ring
  holds 64 lines only), overlay as usual.
- A bus DMA byte while paused (the app's freeze: VIC/CIA/SID registers,
  colour RAM, ROMs) opens the clock for its cycle: the first one also lets
  the CPU reach a read cycle and holds it there (RDY) until the resume, each
  further byte costs exactly one machine cycle, as a real DMA. The RAM path
  (C64_DMA_MEMONLY, JTAG peek/poke) costs nothing.
- Resume waits until the clock was shut for whole PAL frames (at most
  20 ms), so the VIC stays locked to the 1080p frame.
- Sim: `designs/c64_basic/sim/pause` (`build.sh`, `tb_pause boot`, `tb_pause
  run`) passes: 7 frames paused, no C64 clock, raster, CPU or drive CPU
  cycle moved; after the resume RAM, raster and clock count equal a run
  without the pause; while paused CIA 1 timer A, TOD, SID voice 3 and the
  raster line move only by the DMA reads' cycles (32 dot4x each after the
  first); bus and RAM-port writes land. sim/dma_io and tb_scaler still pass.

What a paused machine gives T20 today, and what is missing:

- Readable now without side effects: C64 RAM (RAM port), drive RAM and
  tracks (DDR2; drive_mem's line cache must be written back first), REU
  memory (DDR2). Through bus DMA, one cycle each and with the chips' read
  side effects: VIC registers, colour RAM, CIA registers (ICR clears on
  read, TOD latches on hours), SID read registers. Not usable for an
  invisible snapshot.
- Missing (needs a state port per core, read and write while paused,
  patches to icepi and Gideon's 1541): 6510 A/X/Y/S/P/PC, opcode and cycle
  of the instruction, IRQ/NMI latches, port $00/$01, RDY latch; VIC raster
  x/y, VC/VCBASE/RC, sprite MC/MCBASE/DMA/expansion flip-flops, display/idle
  and bad-line state, IRQ latch, write-only register values; CIA counters
  and latches, CRA/CRB, ICR mask and flags, TOD counters/alarm/latch and its
  50 Hz divider, SDR; SID oscillator accumulators, noise LFSR, envelope
  state/counters, filter state, write-only registers; REU registers and
  engine state; `cart_slot` bank/mode registers; `c64_top` glue (cpu_paused,
  reset state, `ext_dma`); drive 6502, both VIAs, GCR shifter and bit
  position in `floppy_stream`, head/motor, `c1541_timing` (pre_cnt,
  behind); IEC line state.
- The drive's ticks come from the 100 MHz clock, not from dot4x: after a
  pause the drive's phase to the C64 moves by up to one 4 MHz tick (250 ns).
  A replay with disk traffic is not cycle-exact until the drive is clocked
  from the C64's clock.
- An external device on the IEC port does not pause.
- Showing the last frame while paused needs a full frame buffer (400 x 270
  x 4 bit, about 12 BRAM36 or DDR2).
