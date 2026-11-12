# T28 — UCI (Ultimate Command Interface) on TRX64 Ultimate

**Status:** spec 2026-10-02; built 2026-10-02 (branch `t28-uci`, §8), simulated, not yet on the board. **Task:** TODO.md T28. **Milestone:** part of
milestone 1 (Alex, 2026-10-02): drop-in replacement for Gideon's core plus the TRX64 functions.
**Shares with:** T16.2 (cart write-back event path and its borrowed `irq_in(4)`), T21
(event push to the outside), ARCH (`docs/specs/ARCH-lean-state-access.md`, IO block).

## 1. Goal

C64 programs that use the UCI (`$DF1C-$DF1F`, or `$DE1C-$DE1F` with an EasyFlash, see the
`trex/main` commit "Keep the UCI at $DE1C for every EasyFlash") work on TRX64 Ultimate
exactly as on Gideon's core. The app (`software/ultimate-app`) stays unchanged: its
command targets (DOS, control, network, softiec, http) already exist.

Non-goals: a new protocol; changes to the app's command handling.

## 2. What the app expects (read from the code)

- Base `CMD_IF_BASE` = `IOBASE + 0x44000` (`software/system/iomap.h:16`). Today this is
  inside our slot-server window and reads as zero (`designs/c64_basic/ultimate_sys.vhd`).
- Registers at +0x00..+0x0F (`software/io/command_interface/command_intf.h:14-33`):
  `SLOT_BASE`, `SLOT_ENABLE`, `HANDSHAKE_OUT`, `STATUSBYTE`, command/response/status
  start and end pointers, `STATUS_LENGTH`, `IRQMASK` (set/clear at +4/+5 on write),
  response and command lengths.
- A 2 KB RAM at +0x800 (`CMD_IF_RAM`) holding the command, response and status areas; the
  app moves data in and out of this RAM and sets the pointers.
- Handshake and state bits (`command_intf.h:35-53`): `CMD_NEW_COMMAND`,
  `CMD_DATA_ACCEPTED`, `CMD_ABORT_DATA`, state idle/processing/data-last/data-more,
  `HANDSHAKE_*`, `CMD_HS_DMA_ACTIVE`, `CMD_HS_TRIGGER_ACTIVE`.
- The interrupt is ITU `irq_in(4)` (`ITU_INTERRUPT_CMDIF`), and the app registers the
  command interface only when the capability bit says it exists.

So the app needs Gideon's register-plus-RAM model, not FIFOs. A C64-side read of the
response or status register is answered from the RAM at the current pointer in the same
bus cycle, by hardware.

## 3. Decision: vendor Gideon's open command interface

`fpga/io/command_interface/vhdl_source/` (`command_interface.vhd`, `command_protocol.vhd`,
`command_if_pkg.vhd`) is open and **not** on the clean-room list (that list is UltiSID,
Copper and lp_filter only). Take it unchanged through `tools/gideon_sources.sh`, as for
the other Gideon blocks. This is the drop-in path: the app sees the hardware it was
written for.

Rejected: a generic "I/O responder" of our own (write events to the app through the
T16.2 event path, reads from app-filled FIFOs). It would need app changes for the
pointer/RAM model and gains nothing for milestone 1. It stays an idea for TRX64-only
features later.

## 4. What we build around it

1. **Slot-bus adapter.** `command_interface` takes Gideon's `t_slot_req`/`t_slot_resp`
   (address, data, `io_read`/`io_write`, `rwn`, I/O-area strobes). Build these from our
   C64 bus in `c64_core.v`/`cart_slot.v` (IO1/IO2 decode, phi2 timing as for the cartridge
   model), and merge `slot_resp` (data + drive enable) into the C64 read mux with the
   cartridge model (UCI has priority inside its 4 bytes when `SLOT_ENABLE` is set; the
   base `$DF1C`/`$DE1C` comes from `SLOT_BASE`). Check which `slot_req` fields
   `command_protocol.vhd` actually uses and fill only those.
2. **IO bus.** Connect `io_req`/`io_resp` to the 8 KB window at `IOBASE + 0x44000` in
   `ultimate_sys.vhd` (the sub-decode already exists for the slot-server window).
3. **Interrupt.** `irq_in(4)` is borrowed today by the TRX IO block (`trx_io.v:54`,
   `trx_io.cc:9-65`, `riscv_main.c` hook `itu_irq4_handler`). Resolve it with the least
   change to Gideon's code; in order of preference:
   a. move the TRX IO block's interrupt to a free ITU line, if one exists in our ITU
      configuration;
   b. otherwise OR both sources onto `irq_in(4)` and let `trx_irq_dispatch` chain to the
      command interface's own handler when the TRX cause register is empty (the app's
      CMDIF ISR must tolerate a spurious call; check `command_intf.cc`).
4. **Capability bit.** Set the command-interface bit in `g_capabilities`
   (`ultimate_sys.vhd:59`) so the app instantiates its UCI driver; check the bit number in
   `software/system/itu.h`.
5. **`freeze` output and `write_ff00`:** check what `command_interface.vhd` does with them;
   wire `freeze` to our freeze request only if the app's UCI commands need it, else leave
   it unconnected and say so in the code.
6. **State bus (ARCH).** UCI state (pointers, handshake, 2 KB RAM) is app-owned I/O, not
   C64 state; T20 does not snapshot it. A restore while a UCI command is in flight resets
   the interface (documented limit).

## 5. Sync vs async (T28 note from 2026-10-01)

UCI itself is synchronous and polled by the C64. Event streaming to the outside (C64RE,
the monitor) is T21's WebSocket JSON-RPC push, not part of the UCI. Nothing in T28 blocks
it.

## 6. Budget

Gideon's block: one 2 KB RAM (1 RAMB18) and an estimated 200-300 LUT; adapter and mux
~50 LUT. Current build (CI run 250): 24,676 LUT (75.7 %), 57.5 BRAM tiles.

## 7. Tests

1. Simulation: a bench drives the C64 side (`$DF1C-$DF1F` reads/writes with phi2 timing)
   and the IO side (app register and RAM accesses) through one DOS command round trip
   (e.g. `DOS_CMD_IDENTIFY`), plus abort and multi-part response.
2. Board: the app's own UCI tests if any (`software/io/command_interface/tests`), and a
   C64 program that sends identify/DOS commands at `$DF1C` and at `$DE1C` with an EF
   loaded; compare replies with Gideon's firmware on the same commands.
3. Regression: Lorenz subset, cart tests (UCI shares IO1/IO2 with the cartridge model).

## 8. As built (2026-10-02)

- **Vendored** unchanged by `tools/vendor_gideon.sh`: `command_if_pkg`, `command_protocol`,
  `command_interface`, and what they need: `slot_bus_pkg`, `dpram`, `io_bus_bridge2` +
  `synchronizer_gzw` (the clock crossing).
- **`designs/c64_basic/uci_unit.vhd`**: `command_interface` runs in the C64's *ungated* clock
  (sys_clk), so the app's accesses are answered while the machine is paused; its 8 KB range
  comes from the app's clock through `io_bus_bridge2`. `command_protocol` uses only
  `bus_address`, `io_address`, `data`, `io_read`, `io_write` (and `late_write` for
  `write_ff00`); the others stay at `c_slot_req_init`. `bus_address` is the live bus address
  (the response/status RAM is read one clock after it; the C64 takes the data 16 clocks
  later); `io_*` pulse once per CPU half that touched `$DE00-$DFFF` with I/O mapped, with that
  half's address and data. Data, `reg_output` and the C64 IRQ are registered once more.
- **C64 bus** (`cores/icepi`): the arbiter has a responder of our own in `$DE00-$DFFF`
  (`io_ext_*`): a last stage of its read mux, so the UCI wins over the REU (which mirrors its
  registers over `$DF00-$DFFF` here) and the cartridge inside its 8 bytes (Gideon:
  `reg_output` covers `$xx18-$xx1F`, offsets 0-2 read `$FF`). `c64_top` counts an access
  only when it happens on the bus: not a CPU held by RDY for a pause or the cartridge's wait
  (a CPU held by BA gets no I/O from the arbiter anyway). `c64_core.v` gives `bus_addr` and
  one event per phi2 falling edge the gate lets through (`io_ev*`). The UCI's C64 IRQ is ORed
  into the CPU's IRQ.
- **`freeze`** (§4.5): wired: control bit 7 (and the `$FF00` trigger) holds the CPU through
  `cpu_pause_req`, as Gideon's `dma_n`. The app does not need it (its DMA goes to the RAM's
  second port), but C64 programs that set the bit expect the CPU to wait; it is one OR.
- **Interrupt** (§4.3): option a. `irq_in(4)` is the UCI's as in `ultimate_logic_32`; the TRX
  IO block moves to `irq_high(2)`, drive B's WD177x in Gideon's bitstreams (`c1541.cc`:
  `1 + letter - 'A'`); the TRX64 bitstream has no drive B, so the line is free here. A drive
  B later needs another answer (option b). App change (unavoidable: the old app takes
  `irq_in(4)` for the IO block): `trx_io.cc` installs its dispatcher with
  `install_high_irq(2, …)`, `riscv_main.c` is Gideon's again (no `itu_irq4_handler`);
  submodule branch `t28-uci-irq`. Old app + new bitstream (or the reverse) loses the IO
  block's interrupts: pin both together.
- **Capability**: `CAPAB_COMMAND_INTF` (bit 18) set, `g_capabilities` = `0x25C40222`.
- `irq_in(7)` (C64 reset): was 0 here; `command_intf.cc` (linked on the C64U) overrides the
  weak `ResetInterruptHandlerCmdIf` and aborts the command in flight. Wired by T39
  (`docs/specs/T38-T39-input-reset.md`).
- **Restore** (§4.6): not wired; a restore during a command leaves the interface as it is.

Tests (§7.1): `designs/c64_basic/sim/uci/run.sh` (ghdl, `uci_unit` with an app model of
`command_intf.cc` and a phi2-timed C64 bus): DOS identify at `$DF1C` with the C64 IRQ and
freeze, two-part response, abort inside a response, the busy error bit, `$DE1C` (slot base 7)
and switched off: PASS. `sim/uci/build_core.sh` + `tb_uci_core.cpp` (Verilator, c64_core with
the real 6510, REU on, no cartridge): the KERNAL boots, a program at `$C000` sends identify
with freeze and reads `ULTIMATE-II DOS V1.2` / `00,OK`; the UCI sees exactly the program's 4
writes and 54 reads: PASS.

Size (yosys `synth_xilinx`, estimates): `uci_unit` 211 LUT, 206 FF, 1 RAMB18 (Gideon's block
with the bridge); the arbiter +90 LUT (1029 vs 939; yosys on that mux is noisy); c64_core +27
FF.

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

T28: UCI (Ultimate command interface at $DF1C-$DF1F)

Milestone M1, status ✅. Open (one line, as in the TODO table): –

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- Task text: UCI (Ultimate command interface at $DF1C-$DF1F / $DE1C with EF): Gideon's open command interface VHDL vendored unchanged on our C64 bus, capability bit, the TRX IO block's interrupt moved to irq_high(2) (spec docs/specs/T28-uci.md §8); in main; event streaming to the outside is T21's WebSocket. Board-tested 2026-10-02 (m16): identify at $DF1C returns ULTIMATE-II DOS V1.2 / 00,OK.
