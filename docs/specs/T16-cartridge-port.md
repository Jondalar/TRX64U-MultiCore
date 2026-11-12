# T16 — The real cartridge (expansion) port

**Status:** draft spec 2026-10-01; measurement closed 2026-10-03; M1 (P1 + pass-through,
§13) built and simulated 2026-10-03 on branch `t16-port` (§13.9); board session 2026-10-04 (§13.10 results);
completion (Q8, hold tick, freeze-session cycles, lens root cause, ROM_WR_EN policy) built 2026-10-05 on `t16-complete` (§13.11); final cart session open. **Task:** TODO.md T16 (cartridge-port
part; user port and tape are separate). **Depends on:** T06 (internal cartridges:
`cart_slot.v`, `cart_logic.v`, `cart_fill.v`), T12 B (machine pause, `c64_core.v`).
**Feeds:** T20 (real carts through shadow mode and a write journal, §10),
T21 (`cart` / `io` lenses).

**One sentence:** our C64 core drives the C64U's physical expansion port the way a C64
board does (PHI2, address, R/W, BA, DOTCLK, ROML/ROMH/IO1/IO2 out; data both ways;
GAME/EXROM/IRQ/NMI/DMA/RESET in), selected per resource against the internal cartridge by
the app's existing registers. Real ROM carts and EasyFlash run, and the app's cartridge
preference, `CART_DETECT` and the trex_crt dumper/writer work on it.

**Rules for this document.**
- Evidence is code or measurement. Every pin row names its source: **XDC** = Gideon's C64U
  constraints (issue #615, copy `1541ultimate-tools/pinmap/c64u_gideon_issue615.xdc`),
  **BIT** = IOB configuration decoded from Gideon's bitstream
  (`pinmap/bitio_u64e2_50t.csv`, tools fork branch `jtag/blaster-sample`), **SAMPLE** =
  JTAG SAMPLE measurement (`docs/c64u-pinmap.md`, same branch), **VHDL** = Gideon's open
  slot code (`1541ultimate/fpga/cart_slot/vhdl_source/`). A port *name* in the XDC is
  weak evidence for function and none for polarity; rows that rest on the name alone are
  marked **unverified**.
- What is copied or adapted from our TRX64 emulator is marked **[TRX64: file]**, paths
  relative to `/path/to/Development/C64/Tools/TRX64/`.
- Times are in dot4x ticks: 1 tick = 1 / 31.527778 MHz = 31.72 ns; one C64 cycle = 32
  ticks = 1015 ns; a half cycle = 16 ticks = 507.5 ns.

---

## 1. Goals and non-goals

### Goals

1. A plain 8K/16K ROM cart and an EasyFlash (EF1-class: AM29F040 pair, `$DE00` bank,
   `$DE02` mode, 256 B RAM at `$DF00`) boot and run from the port on our core.
2. The app's selection works unchanged: Cartridge Preference Auto / Internal / External /
   Manual (`CFG_C64_CART_PREF`), GAME/EXROM On/Off (`CFG_C64_GAME_EXROM`), through the
   registers the firmware already writes (`C64::ConfigureU64SystemBus`,
   `software/io/c64/c64.cc:1849`).
3. `CART_DETECT` (0x10100403) reads the real GAME/EXROM pins.
4. The trex_crt dumper, inspector and writer (`1541ultimate` branch `trex/main`,
   `apps/trex_crt/src/`) reach the real cart through the app's DMA, while the machine is
   paused (T12 B).
5. No bus contention, ever, by construction: everything off by default, direction logic
   proven in simulation against a transceiver model with a contention checker before any
   board test.

### Non-goals (v1)

- **Cartridge DMA as bus master** (real REU 17xx, a 1541 Ultimate II in the port, Kung
  Fu Flash DMA loads). Pins are read and the hazard is handled (§5.6); serving the cart's
  DMA cycles is phase P6.
- **The SID sockets.** They share A0-A15/D0-D7 with the port (SAMPLE). T16 only keeps
  their selects inactive.
- **Quiet / Dynamic bus modes as power or EMI savers.** With an external resource enabled
  the port is driven every cycle (what Gideon calls Compatibility); §4.3.
- **Shadow mode + write journal** is sketched in §10 and built after
  T20 M1; T16 only leaves room for it.

---

## 2. What exists today (read from the code)

| Piece | Where | State |
|---|---|---|
| Slot pins in our design | `designs/c64_basic/top.v:45,88-91`, `pins.xdc` | Only `SLOT_ADDROEn`, `SLOT_DATAOEn`, `SLOT_ROMLn`, `SLOT_ROMHn` are ports, all tied to 1. Every other slot and SID-socket ball is **not a port**. |
| Unused balls | `build.tcl` sets no `BITSTREAM.CONFIG.UNUSEDPIN` | Vivado's default is pull-down: today SLOT_IO1n, IO2n, PHI2, BA, DOTCLK, both DIR lines, RST_O, SID1n/SID2n, SID_ADDR, SLOT_PWR, R/W, A0-A15, D0-D7 sit at a weak low. Whether that reaches the port depends on the unverified buffers (§3.3). **P1 makes every one of them an explicitly driven or explicitly pulled port.** |
| Core's cartridge interface | `cores/icepi/c64_top.v:195-220` (icepi patch 0030) | `ext_cart_en = 1` (`c64_core.v`): our `cart_slot` is the only cartridge. The interface carries `addr` (CPU/REU address in CPU cycles, VIC address `vic_ram_addr` in VIC cycles, `c64_bus_arbiter.v:704`), `we`, ROML/ROMH/IO1/IO2 (active low, from the PLA model), phi2 pulses, `reset`; and takes EXROM/GAME, `dout`, NMI/IRQ, `io2_we_valid`, `wait`. |
| PLA model | `cores/icepi/c64_bus_arbiter.v` | Combinational, per dot4x tick, from `{game, exrom, charen, hiram, loram}`. VIC branch: in ultimax, VA13:12 = 11 selects ROMH (`:711-718`). GAME/EXROM count only while `cart_present` (`:438-439`), which is latched **at CPU reset** from `flags[11]` = "app set a cartridge type" (`c64_top.v:1197-1199`, `c64_core.v` `flags`). IRQ/NMI from the cart are gated the same way (`c64_top.v:1250-1251`). |
| CPU timing | `c64_top.v:347-368`, `cpu6510.v`, kawari `vicii.v:446-449` | The 6510 advances at `phi2_l` (phi1 tick 9, ~285 ns after PHI2 falls): address and write data are stable from there. It latches read data at `phi2_n` (the falling edge tick). RDY = `vic_ba && !cpu_paused && !reu_dma_active && !ext_cart_wait`. |
| VIC data latch | kawari `bus_access.v` (`phi_phase_start_dav`), `common.vh:130` `DATA_DAV 0` | The VIC takes its data at `phase_start[0]`, the boundary tick at the end of its half cycle. Its address is complete only after CAS (`addressgen_spartan.v`: PAL CAS falls at tick 6/7 of the half cycle). |
| Bus stall | `c64_top.v:1370` `vic_stall` | Only when `mem_rdata_valid` is late. `c64_mem.v` answers every read one clock later, so there is no stall today; a stretched cycle would break a real cart (§4.6). |
| Machine pause | `c64_core.v` gate | The C64 runs on `gclk` (BUFGCE `bg_c64`, `top.v:120`); the gate shuts after a `phi2_n` edge; an app DMA byte opens it for one cycle; resume after whole PAL frames. |
| App's DMA | `c64_top.v:1405-1427` | One byte = one bus cycle through the REU path (`bus_addr = reu_dma_addr`), so it goes through the PLA and reaches ROML/ROMH/IO1/IO2. |
| App registers | `ultimate_sys.vhd:917` | `CART_DETECT` is the constant 0x03. `C64_CLOCK_DETECT` (0x10040003) is the constant 0x01 (no reset sense). 0x10180000-37 (`C64_BUS_BRIDGE` 0x1018002A, `C64_BUS_INTERNAL` +2B, `C64_BUS_EXTERNAL` +2C) are latches without effect (`docs/ultimate-app-hw-spec.md` §6.1). |

---

## 3. Pins

### 3.1 Table

Dir is seen from the FPGA. "Bank" gives the I/O voltage: 14 and 15 are 3.3 V, 16 and 34 are
1.8 V (BIT). Drive: BIT's decoded drive strength.

| Signal (XDC name) | Ball | Bank / std | BIT dir, drive | Port function, polarity | Evidence | Status |
|---|---|---|---|---|---|---|
| D0-D7 (`SLOT_DATA[0..7]`) | W4 V5 W2 U5 T5 U3 T4 T3 | 34, LVCMOS18 | inout, 4 mA | data, + | XDC, BIT, SAMPLE (r = -1.00 both runs, also via the SID sockets) | verified (ball, bit order) |
| A0-A7 (`SLOT_ADDR[0..7]`) | Y7 AB7 V7 AA6 Y6 AA5 U6 W5 | 34, LVCMOS18 | inout, 4 mA | address, + | XDC, BIT, SAMPLE (A0-A4 r ≈ -1.00 SID run; A5-A7 0.86-0.92 port run) | verified |
| A8-A11 (`SLOT_ADDR[8..11]`) | F16 F15 F14 F13 | 16, LVCMOS18 | inout, 4 mA | address, + | XDC, BIT, SAMPLE (0.87-0.95) | verified |
| A12-A15 (`SLOT_ADDR[12..15]`) | V19 U20 U17 T18 | 14, LVCMOS33 | inout, 12/16 mA | address, + | XDC, BIT, SAMPLE (A15 weakest, 0.79) | ball verified; **what sits between the ball and the 5 V port is unknown** (not the 1.8 V transceiver: other bank, other standard). Direction is the FPGA's own tri-state.; SAMPLE 2026-10-02 (Gideon's bitstream, C64anabalt, 300 captures, `tests/cart/data/sample_c64anabalt_2026-10-02.json`; 16 more on 2026-09-30, tools fork `pinmap/runs/sample_with_cart.json`): all four read 1 in every capture. Re-measured 2026-10-03 with the EF1 (Timo's Castle), 2,000 captures (`tests/cart/data/sample_ef1_timos_castle_2026-10-03.json`, `tests/cart/sample_slot_addr.py`): A15-A12 = 1111 in 61 %, 1000 ($8xxx) in 39 %, 1110 once; output enable on in every capture. So the balls do carry the address (ball mapping also confirmed by `addr16` on 2026-09-30); the C64anabalt run just never caught A12-A15 off 1111. ADDROEn 0 and ADDRDIR/SLOT_PWR 1 again in every capture |

Reset series 2026-10-03 (EF1, Timo's Castle, REST reset, 1,901 captures over 8 s, about 4 ms apart; `tests/cart/sample_reset_series.py`, `tests/cart/data/reset_ef1_timos_castle_2026-10-03.json`): before the reset GAMEn/EXROMn = 0/0 (16K); right after it 0/1 (Ultimax, the EF's boot mode) for about 8 ms (2 captures), then 0/0 again. A15-A12 then 1111, 1000 and 1001 ($8xxx/$9xxx). ROMLn/ROMHn are strobes too short for this capture rate (never caught low). P1/pass-through must therefore handle an Ultimax start right after reset with no settle time.
| `SLOT_ADDRDIR` | D15 | 16, LVCMOS18 | out | direction of the A0-A11 transceiver | XDC, BIT; SAMPLE 2026-10-02 (Gideon's bitstream, C64anabalt, 300 captures, `tests/cart/data/sample_c64anabalt_2026-10-02.json`; 16 more on 2026-09-30, tools fork `pinmap/runs/sample_with_cart.json`): driven 1 in every capture | 1 = FPGA towards the port (as DATADIR); 0 is expected only when a cartridge takes the bus (DMA), not seen with this cart |
| `SLOT_ADDROEn` | D16 | 16, LVCMOS18 | out | A0-A11 transceiver enable, low = on | XDC, BIT; SAMPLE 2026-10-02 (Gideon's bitstream, C64anabalt, 300 captures, `tests/cart/data/sample_c64anabalt_2026-10-02.json`; 16 more on 2026-09-30, tools fork `pinmap/runs/sample_with_cart.json`): driven 0 in every capture | low = on confirmed; Gideon keeps the address transceiver on all the time with a cart in. Whether it also gates A12-A15: open (they are FPGA balls, see their row) |
| `SLOT_DATADIR` | R6 | 34, LVCMOS18 | out | direction of the D0-D7 transceiver | XDC, BIT; SAMPLE saw R6 react to a menu event; SAMPLE 2026-10-02 (Gideon's bitstream, C64anabalt, 2,000-3,000 captures): 1 nearly always (FPGA towards the port), 0 only in the captures with ROML/ROMH low, where the FPGA's D0 driver is off and D0 reads the cart | verified: **1 = FPGA → port, 0 = port → FPGA** |
| `SLOT_DATAOEn` | T6 | 34, LVCMOS18 | out | D0-D7 transceiver enable, low = on (name) | XDC, BIT; we hold it 1 today; SAMPLE 2026-10-02 (Gideon's bitstream, C64anabalt, 2,000-3,000 captures): low in ~40 % of captures, mostly with PHI2 high and the FPGA driving D0, also low on the ROML/ROMH reads | verified: **low = transceiver on** |
| `SLOT_ROMLn` | W9 | 34, LVCMOS18 | out | ROML, low = selected | XDC, BIT, SAMPLE (low only on `$8000` reads with EF in 16K; low during reset's signature probe at `$8004`) | verified |
| `SLOT_ROMHn` | Y8 | 34, LVCMOS18 | out | ROMH, low = selected | XDC, BIT, SAMPLE (`$A000` reads in 16K) | verified |
| `SLOT_IO1n` | AB8 | 34, LVCMOS18 | out | IO1 (`$DE00-$DEFF`), low | XDC, BIT, SAMPLE | verified |
| `SLOT_IO2n` | V8 | 34, LVCMOS18 | out | IO2 (`$DF00-$DFFF`), low | XDC, BIT, SAMPLE | verified |
| `SLOT_PHI2` | W7 | 34, LVCMOS18 | out | PHI2, high = CPU half | XDC, BIT, SAMPLE 2026-10-02 (Gideon's bitstream, C64anabalt, 2,000-3,000 captures): output cell 0/1 at ~50 %, always enabled | verified (the input cell of this output-only ball always reads 1, which is why the earlier input-based search missed it) |
| `SLOT_DOTCLK` | AA8 | 34, LVCMOS18 | out | dot clock 7.88 MHz | XDC, BIT, SAMPLE 2026-10-02 (Gideon's bitstream, C64anabalt, 2,000-3,000 captures): output toggles ~50 % | verified as a running clock (frequency not measurable by SAMPLE) |
| `SLOT_BA` | Y9 | 34, LVCMOS18 | out | BA, low = VIC wants the bus | XDC, BIT, SAMPLE 2026-10-02 (Gideon's bitstream, C64anabalt, 2,000-3,000 captures): output low in 3-8 % of captures (bad lines, sprites), always enabled | verified |
| `SLOT_RWn` | V18 | 14, LVCMOS33 | inout, 12/16 mA | R/W, low = write | XDC, BIT, SAMPLE (low only in write loops) | ball and polarity verified; tri-state is the FPGA's |
| `SLOT_GAMEn` | V17 | 14, LVCMOS33 | in | GAME, low = asserted | XDC, BIT, SAMPLE (follows EF `$DE02` bit 0 exactly) | verified |
| `SLOT_EXROMn` | W17 | 14, LVCMOS33 | in | EXROM, low = asserted | XDC, BIT, SAMPLE (1 without cart, 0 with MegaByter, follows EF `$DE02` bit 1) | verified |
| `SLOT_DMAn` | W19 | 14, LVCMOS33 | in | DMA, low = cart takes the bus | XDC, BIT, SAMPLE 2026-10-02 (Gideon's bitstream, C64anabalt, 2,000-3,000 captures): never driven, reads 1 | input with an external pull-up (pull-up inferred, no cart drives DMA) |
| `SLOT_IRQn` | U18 | 14, LVCMOS33 | inout | IRQ, open drain, low | XDC, BIT; SAMPLE 2026-10-02 (Gideon's bitstream, C64anabalt, 2,000-3,000 captures): output cell fixed 0, enable on in 14-70 % of captures, pad follows | verified: open drain, Gideon's design *drives* it low with the internal IRQ (like NMI) |
| `SLOT_NMIn` | R17 | 14, LVCMOS33 | inout | NMI, open drain, low | XDC, BIT, SAMPLE (low while a CIA 2 NMI is pending; low with RESTORE) | verified: Gideon's design *drives* it with the internal NMI |
| `SLOT_RST_I` | P20 | 14, LVCMOS33 | in | port RESET sense | XDC, BIT; SAMPLE 2026-10-02 (Gideon's bitstream, C64anabalt, 2,000-3,000 captures): reads **0** while the machine runs; SAMPLE 2026-10-02 with an EF3, four reset presses (0.2-3 s), 18,029 captures: P20 reads **1 exactly while the button is held**, and in the same captures Gideon's design enables R21 (`CIA_RSTn`) driving 0; M18 stays an enabled output at 0 throughout | verified: **sense, active high** (1 = a cart holds RESET); R21 is the driver that propagates it |
| `SLOT_RST_O` | M18 | 15, LVCMOS33 | out, 4 mA | port RESET driver (likely a transistor: name follows the `IEC_*_O` = "1 pulls low" pattern) | XDC, BIT; SAMPLE 2026-10-02 with an EF3: 0 at rest; on the app's `machine:reset` and `machine:reboot` it goes to **1** for 4-135 ms while P20 reads 1 and R21 drives 0; it does not move on the EF3's own button | verified: **1 = pull the port RESET low** |
| `CIA_RSTn` | R21 | 14, LVCMOS33 | inout, pull-up | reset of the C64-side ports | XDC, BIT, SAMPLE: with an EF3 reset press the design *switched its driver on* and held it low for the whole reset | ball verified; reading: the sensed port reset (P20) is propagated here by Gideon's design. Roles measured 2026-10-02 (EF3 button and app reset): P20 senses (active high), M18 drives the port (1 = assert), R21 resets the C64-side chips in both cases. |
| `SLOT_SID1n`, `SLOT_SID2n` | M20, N20 | 15, LVCMOS33 | out, 4 mA | SID socket chip selects, low | XDC, BIT, SAMPLE | verified; T16 holds them 1 |
| `SLOT_SID_ADDR` | K17 | 15, LVCMOS33 | out | SID socket address line | XDC, BIT | unverified; held 0 |
| `SLOT_PWR` | L15 | 15, LVCMOS33 | out, 12/16 mA | probably the port's +5 V switch | XDC, BIT; SAMPLE 2026-10-02 (Gideon's bitstream, C64anabalt, 300 captures, `tests/cart/data/sample_c64anabalt_2026-10-02.json`; 16 more on 2026-09-30, tools fork `pinmap/runs/sample_with_cart.json`): driven 1 in every capture | Gideon's level with a cart in = **1**. That it switches +5 V is still inferred (name, drive strength), not measured; P1 drives it 1 like Gideon |
| `CIA_BUFFER_EN`, `CIA_PWR_EN` | H13, G18 | 15 | out | C64-side buffers and their supply | XDC; we hold them 1 (`top.v:94-95`) | in use |

### 3.2 What the code of Gideon's open slot logic says, and what it cannot say

`slot_server_v4` / `slot_master_v4` / `slot_slave` / `slot_timing` describe the
**Ultimate as a cartridge** (1541U2 role): it receives PHI2, BA, ROML/ROMH/IO1/IO2 and
drives DMA, GAME/EXROM, data. On the C64U the FPGA is the C64; the code that drives this
port (`u64_mk2_artix` top) is not public. So the VHDL is evidence for **what a cart
expects** (timing, the DMA protocol), not for our transceiver control:

- A cart samples the address at a programmable delay after PHI2 rises
  (`slot_timing.vhd` `do_sample_addr` at `timing_phi2`) and write data at
  `c_write = c_400ns - 2` (~380 ns after the rising edge it saw).
- It considers the clock gone after `g_frequency / 800_000` clocks (1.25 µs) without a
  PHI2 edge (`clock_det`); a U2 in our port sees every machine pause as "no C64".
- As DMA master it drives A/RW only in `phi2_recovered and not vic_cycle`
  (`slot_master_v4.vhd` `drive_al`), data from 80 ns after PHI2 rises for writes
  (`dma_data_out`), parks the address at `$FFFF` while it holds the C64 stopped, and
  for reads only proceeds when BA = 1 (the PLA does not select I/O for reads in the
  three AEC-high cycles after BA falls; the PLA equations in that file).
- `slot_server_v4.vhd:1077-1081`: IRQ, NMI, RESET, DMA are open-drain outputs of the
  cart. GAME/EXROM likewise (`:1083-1098`).

### 3.3 Unknowns that block driving anything

These are answered by **P0** (§9) before any T16 bitstream drives a port line:

1. ~~Polarity of `SLOT_ADDRDIR`, `SLOT_DATADIR`, `SLOT_ADDROEn`, `SLOT_DATAOEn`.~~ Measured (2026-10-02 captures, analysed 2026-10-03): DIR 1 = FPGA to port, OEn low = on.
2. What buffers A12-A15 and R/W (3.3 V balls) and whether ADDROEn gates them. Not measurable by SAMPLE (board level, and no probing: no loose wires). Moot for P1: Gideon drives these balls directly with the enable on in every capture (2026-10-02/03), and P1 does the same.
3. `SLOT_PWR`: Gideon drives 1 with a cart in (measured). Whether it switches +5 V stays inferred; P1 drives 1.
4. ~~P20 / M18 / R21: which senses the port RESET, which drives it, polarity of M18.~~ Measured 2026-10-02, see the pin table.
5. ~~PHI2, BA, DOTCLK, IRQ, DMA balls: confirmation by measurement.~~ Measured 2026-10-02 (SAMPLE with C64anabalt), see the pin table.
6. Whether the control outputs (ROML/ROMH/IO1/IO2/PHI2/BA/DOTCLK) pass a buffer that
   has its own enable (if not, today's pull-down on IO1n/IO2n/PHI2/BA reaches a cart
   as "IO1 and IO2 selected, PHI2 low, BA low"). Not measurable without probing; moot once P1
   drives every control output at Gideon's measured idle levels from configuration on.

Status 2026-10-03: nothing left that SAMPLE can answer; P1 and pass-through can be built.

---

## 4. Bus timing on our core

### 4.1 Principle

Every port output is a **registered copy** of a core signal, in IOB flip-flops, one tick
behind the core. All outputs share that one-tick offset, so their relative timing is the
core's. Port inputs are taken in IOB flip-flops at fixed ticks (data, address) or through
two-flip-flop synchronizers (GAME, EXROM, DMA, IRQ, NMI, RESET).

Below, **port tick p** counts from the edge on which the port's PHI2 rises (p = 0) to the
next rise (p = 32); p = 16 is the PHI2 falling edge. Core tick c = p - 1.

### 4.2 Cycle schedule

| p | PHI2 | A0-A15 | R/W | ROML/ROMH/IO1/IO2 | D0-D7 transceiver | Core action |
|---|---|---|---|---|---|---|
| 0 | ↑ | CPU (or app DMA) address; VIC address if AEC stays low (BA stolen) | CPU's value | — | off | |
| 1 | 1 | | | asserted from the PLA model for this address (CPU branch) | **write:** dir out, on, data = `cpu_dout` (stable since phi1 tick 9). **Read selected to the port:** dir in, on. **Other read:** off (v1) | |
| 1-15 | 1 | | | | | |
| 14 | 1 | | | | | **sample D0-D7** (IOB FF, core tick 15); this byte is `ext_cart_dout` at the core's `phi2_n` edge, where the 6510 (or the app's DMA, `ext_dma_rdata`) latches it |
| 16 | ↓ | **held** | **held** | released (PLA AEC term drops with PHI2) | write data **held** | |
| 17 | 0 | VIC address: A0-A11 = `vic_ram_addr[11:0]`, A12-A15 = 1111 | 1 | — | off (1 tick dead time before any direction change) | |
| 17-31 | 0 | | | ROMH for a VIC fetch in ultimax with VA13:12 = 11, from when the VIC branch selects it | **ultimax VIC fetch:** dir in, on from p18; otherwise off | |
| 31 | 0 | | | | | **sample D0-D7** for the VIC (core tick 15 of phi1; kawari latches at `phase_start[0]`) |

Notes:

- **A12-A15 = 1 in VIC cycles** is what a C64 board does (pull-ups; the VIC drives only
  A0-A11 there). It makes an ultimax VIC fetch from `$3000-$3FFF` land in ROMH at
  `$F000-$FFFF`, i.e. the cart's ROMH offset `$1000 + (addr & $0FFF)`
  **[TRX64: docs/803-large-cartridges.md §5.2 addendum 2026-09-21; crates/trx64-core/src/cart.rs `vic_romh`]**.
- **Hold of one tick (31.7 ns) after PHI2 falls** for address, R/W and write data:
  carts latch on the falling PHI2 or on IO1/ROML rising (EasyFlash `$DE00`/`$DE02`,
  AM29F040 CE-controlled writes latch on CE↑ = ROML↑ at p16 with t_DH = 0).
- **Read access time** to a ROM selected at p1 and sampled at p14: 13 ticks = 412 ns,
  less two transceiver passes and level shifting (budget 20 ns): ~390 ns. A 250 ns EPROM
  or a 70 ns flash fits with margin; a C64 board gives about 340 ns.
- **VIC ultimax fetch**: the address is complete only after CAS (tick 6-7 of the half cycle), so the cart
  gets about 8 ticks (~250 ns) minus pipeline. Tight for 250 ns EPROMs, fine for flash.
  P2 measures the exact tick in simulation; if it is short, the low byte can be put out
  at RAS from `vic_ado` (the C64 board latches it there in its 74LS373 too).
- **GAME/EXROM** go through a 2-FF synchronizer (2 ticks) into the PLA model. A cart that
  changes them on a write latches at PHI2↓ (stable long before the next p1). A cart that
  pulls them in response to the *address* (GMod4: per area, CPU reads only
  **[TRX64: docs/803-large-cartridges.md §3]**) answers within ~10 ns of A at p0; the PLA
  model sees it at about p3 and asserts ROML/ROMH at about p4: still 10 ticks before the
  sample. P2 covers this case explicitly.
- **R/W** is driven only while the CPU or the app's DMA owns the bus (phi2 with AEC high):
  1 in phi1. This is what the port of a C64 board shows (the 6510's R/W follows AEC).
- **BA** = kawari `ba`, registered. **DOTCLK** = dot4x / 4 from the gated clock, phase-fixed
  to PHI2 (7.88 MHz).

### 4.3 Who drives the data bus on reads not served by the port

A C64 board drives every read onto the port (RAM, ROMs, I/O); a few carts snoop it. v1
leaves the data transceiver **off** on those reads: fewer direction changes to prove, no
current into a cart that is not selected. Driving them ("Compatibility") is a register bit
for P6 (`SLOT_CTRL.SNOOP`, §7.2). Gideon's "Bus Operation Mode" (`C64_BUS_BRIDGE`) is kept
as a latch; with any `C64_BUS_EXTERNAL` bit set the address/control side is driven every
cycle whatever the mode says.

### 4.4 Open bus at IO1/IO2

A read of `$DE00`/`$DF00` routed to the port with no cart answering returns whatever the
1.8 V side of the transceiver resolves to (a C64 board returns the bus capacitance's last
VIC byte). Software relying on that "last VIC byte" (the `$DE00` open-bus trick) behaves
differently with the port selected but empty: harmless because Auto never selects the port
without GAME or EXROM low, and External without a cart is the user's choice.

### 4.5 Turnaround rules (the contention argument)

The transceivers (and the FPGA's own tri-states on A12-A15, R/W) change direction only
through an **off** tick:

- `*_DIR` changes only while `*_OEn` = off, and `*_OEn` is off on the tick before and the
  tick after a DIR change (break before make, ≥ 31.7 ns each side).
- The data transceiver is on in at most one direction per half cycle; on the FPGA side the
  D0-D7 balls are inputs whenever DIR = in.
- In v1 the address side never turns: dir = out whenever on. (Cart DMA, P6, adds the turn
  in phi2: off at p0, in from p1 to p15, off at p16, out from p17 + 1 = p18.)
- R/W and A12-A15 are tri-stated by the FPGA in exactly the half cycles in which the A0-A11
  transceiver is off or turned in.

### 4.6 Bus stalls

A stretched cycle (`vic_stall`, `ext_cart_wait`) has no counterpart on a real C64 port; a
real cart sees PHI2 high longer and still works for reads, but `cart_slot`'s RDY wait is
only for the internal cart. Rule: `ext_cart_wait` is 0 whenever ROM or IO is routed to the
port; `c64_mem` keeps its fixed one-clock answer (asserted in sim: `vic_stall` never 1
while `SLOT_CTRL.ARM`).

---

## 5. Selection: internal vs external

### 5.1 The app's registers (Gideon's semantics, read from `c64.cc`)

`ConfigureU64SystemBus()` (`c64.cc:1849-1943`) runs at every C64 reset (`c64.cc:366,
1519, 1798`) and on config changes (`u64_config.cc:925`):

- `ext_cart = (U64_CART_DETECT & 3) != 3` — bit 0 = GAME pin, bit 1 = EXROM pin, 1 = high.
- GAME/EXROM **Off**: `C64_BUS_INTERNAL = 15`, `C64_BUS_EXTERNAL = 0`.
- **Auto**: `ext_cart` → internal 0, external 15; else internal 15, external 0.
- **Internal**: 15 / 0. **External**: 0 / 15.
- **Manual**: per resource from `CFG_BUS_SHARING_{ROM,IO1,IO2,IRQ}` (Internal / External /
  Both), bits **0x01 IO1, 0x02 IO2, 0x04 ROM, 0x08 IRQ**.
- `C64_BUS_BRIDGE` = Bus Operation Mode (0 Quiet, 1 Writes, 2 Dynamic, 3 Dyn. & Writes,
  5 Compatibility); `EnableWriteMirroring()` ORs 0x01.
- If it returns true (external has preference), the firmware does **not** install its
  cartridge (`c64.cc:1514-1525`, `:1798-1804`).

### 5.2 What our hardware does with them

A new module `slot_port.v` sits between the core's cartridge interface and `cart_slot`:

| Mask bit | Internal set | External set | Both |
|---|---|---|---|
| ROM (0x04) | GAME/EXROM from `cart_slot` (and `ext_ultimax`); ROML/ROMH reads served by `cart_slot` | GAME/EXROM from the pins; ROML/ROMH strobes go to the port; read data from the port | GAME = int AND ext, EXROM likewise (wired-AND like the real bus); strobes to both; data from `cart_slot` when it serves, else from the port |
| IO1 (0x01) / IO2 (0x02) | `cart_slot` / internal REU answer | strobe and data at the port; `io2_we_valid` = 1 (the port cart claims IO2 writes) | strobe to the port, `cart_slot` answers reads |
| IRQ (0x08) | `cart_slot` IRQ/NMI | IRQ/NMI pins | OR |

- **Latched when it is safe:** the masks take effect at a CPU reset and while the machine
  is paused (`halted`), never mid-run. The dumper changes them while paused
  (`app_api_impl.cc` `api_c64_cart_setup_bus`: bridge 3, internal 0, external 15) and expects
  the next DMA byte to see them.
- **`cart_present`** (icepi, latched at reset from the cart type) becomes
  `(INTERNAL.ROM and cart_type ≠ 0) or EXTERNAL.ROM or EXTERNAL.IO1 or EXTERNAL.IO2`,
  taken at the same moments as the masks. icepi patch **0070-external-port.patch** (renumbered 2026-10-01: 0060 is the CPU state bus of `ARCH-lean-state-access.md`): export
  `vic_ba`, `vic_aec` (or `vic_write_ab`), take `cart_present` as an input; P6 adds an
  external DMA master input on the REU path.
- **`ext_ultimax`** (C64_MODE bit 1, the app's freeze map) is an internal matter: while it
  is set, ROML/ROMH strobes do **not** go to the port. The dumper clears it first
  (`cart_dumper.c:535`). Open question Q4.
- **CART_DETECT** (0x10100403): `{6'b0, EXROM pin, GAME pin}`, synchronized, live (no
  filter: the firmware reads it once per reset). Read even when the port is not armed: it
  is input only.
- **C64_CLOCK_DETECT** (0x10040003) bit 4 = port RESET sensed low; bit 0 stays 1.

### 5.3 Reset

- Our C64 reset (`cpu_reset`: app MODE 0x04, JTAG reg 9 bit 0, power-on) pulls the port
  RESET for its whole length, at least 20 µs (the firmware's `C64::reset` holds 100 µs).
  EasyFlash needs it: its reset clears `$DE02` to the jumper's boot mode.
- A cart's reset button pulling RESET resets our C64 (into `cpu_reset_req`) and shows in
  `C64_CLOCK_DETECT` bit 4; it does not reset the RISC-V.
- Our own drive of RESET is ignored by the sense path while we drive it (no loop).

---

## 6. Safety

### 6.1 Default off, three independent gates

1. **Build:** `T16_SLOT = 0` in `build.tcl` gives the P1 idle bitstream: every slot ball a
   port with its idle level, no output can change.
2. **Arm:** with `T16_SLOT = 1`, outputs other than the idle levels need
   `SLOT_CTRL.ARM = 1` (JTAG reg 0x30 or app register, §7.2). Power-on, RISC-V reset and
   every fault clear it.
3. **Write inhibit** (`SLOT_CTRL.ROM_WR_EN = 0`, default): ROML/ROMH are never asserted in
   a cycle with R/W = 0. A flash on ROML/ROMH (EasyFlash, GMod2, MegaByter) cannot see a
   single command byte. IO1/IO2 writes still pass (bank registers). Only the writer phase
   sets it.

Idle levels (P1, and whenever not armed): ADDROEn/DATAOEn = off, DIRs = the "in" level
(towards the FPGA, found in P0) so that a misread polarity of OEn alone still cannot drive
the port, PHI2 = 0, DOTCLK = 0, BA = 1, ROMLn/ROMHn/IO1n/IO2n = 1, R/W and A12-A15
tri-stated with a pull-up, SID1n/SID2n = 1, SID_ADDR = 0, RST_O = the "released" level,
SLOT_PWR = the level P0 finds Gideon's design uses with a cart in (until then: not driven,
the pull-down as today).

### 6.2 Contention guard (in hardware)

A small checker in `slot_port.v`, in the ungated clock, watches the registered OEn/DIR/
tri-state controls and the synchronized DMA pin, and forces every enable off and latches
`SLOT_STATUS.FAULT` (cause code) when:

- an OEn turns on in the same tick its DIR changes, or DIR changes while OEn is on;
- the data transceiver is out while a read is routed to the port;
- address/R/W are driven in phi2 while DMA is low and BA high (a cart may be driving them),
  in any phase before P6;
- any output is on while not armed.

The guard is a second, simpler implementation of the rules in §4.5; disagreement with the
schedule is a bug that shows up as a fault in simulation, and on the board as a fault
instead of a fight.

### 6.3 Proof in simulation before any board test (P2)

- **Transceiver model** (`sim/slot/xcvr_model.v`): 74ALVC164245-like, per direction bus
  with OE/DIR, 2-5 ns delay, and a **contention checker**: any port line driven from both
  sides in the same ns is a fatal assertion, with the time and the line. A12-A15 and R/W
  are modelled as the FPGA's tri-state plus a 5 V pull-up.
- **Cartridge models** (behavioural Verilog, port side only):
  - generic 8K (EXROM low), 16K (both low), ultimax (GAME low), with ROM access times
    150 / 250 ns from ROML/ROMH and from address;
  - EasyFlash: `$DE00` bank (6 bits), `$DE02` (bit 0 GAME, bit 1 EXROM, bit 2 mode, bit 7
    LED), 256 B RAM at `$DF00`, two AM29F040 models with the unlock/program/erase state
    machine and DQ6 toggle **[adapt from TRX64: crates/trx64-core/src/flash040.rs, cart.rs
    `EasyFlashMapper`]**, reset to the jumper's boot mode;
  - late GAME/EXROM (GMod4-style, pulls EXROM within 10 ns of a `$8000` address,
    **[TRX64: cart.rs `Gmod4Mapper`]**);
  - a DMA master: Gideon's `slot_master_v4.vhd` + `slot_timing.vhd` through `ghdl synth` (as
    the bench already does for the 6502n), for P6 and for the guard's DMA rule now.
- **Bench**: extend `designs/c64_basic/sim/cart` (Verilator, `tb_cart_top.v`) with
  `slot_port.v` and the models. One simulation at a time, niced (Mac limit).
- **Tests** (all must pass with the contention checker armed):
  1. idle bitstream: no port output changes over 10 frames, cart or not;
  2. 8K / 16K cart: KERNAL finds `CBM80`, cart code runs; every byte the CPU read from
     `$8000-$BFFF` equals the model's;
  3. ultimax cart with the screen at `$3000` in VIC bank 0: the VIC's fetches equal the
     model's ROMH `$1000+` bytes; measures the VIC fetch's access-time margin (§4.2);
  4. EasyFlash boot (ultimax via jumper) → bank switch → 16K → game code from banks 0-3;
  5. write inhibit: an EAPI program sequence with `ROM_WR_EN = 0` leaves both flash models
     untouched; with `ROM_WR_EN = 1` it programs the byte and DQ6 polling ends;
  6. app DMA while paused (T12 B bench `sim/pause`): dump 8 KB of ROML through the DMA path;
     exactly one PHI2 pulse per byte at the port; bytes equal the model;
  7. pause entered during an EasyFlash 16K game and resumed: port shows the park state for
     the whole pause, then the game continues bit-equal to an unpaused run (cart has no
     clock-dependent state);
  8. DMA master asserting DMA in phi1 with `T16` < P6: guard faults, outputs off, no
     contention reported by the checker;
  9. late GAME/EXROM: the CPU reads the cart's byte, not RAM;
  10. selection: Auto with a cart model → firmware-like register sequence → port used;
      Internal → port idle while `cart_slot` runs a CRT; masks changed while paused take
      effect on the next DMA byte.
- **Optional formal check**: the OE/DIR/tri-state state machine is a few flip-flops; a
  bounded proof (SymbiYosys, depth 64) of the §4.5 rules is cheap and runs in seconds.

### 6.4 Hot-plug

The C64 expansion port is not hot-pluggable. Insert or remove a cart **only with the
C64U switched off**. On power-up Gideon's flash bitstream runs first (and handles the
cart); during the JTAG load of ours the FPGA's I/Os are tri-stated with pull-ups
(configuration), so the cart sees floating control lines for that time: harmless for ROM,
and a flash cannot be programmed without a full unlock sequence. After configuration our
design holds the port RESET low until the app releases the C64, so the cart restarts clean.

---

## 7. Register map additions

### 7.1 Registers the app already uses (now functional)

| Address | Name | Read | Write |
|---|---|---|---|
| 0x10100403 | `U64_CART_DETECT` | b0 GAME pin, b1 EXROM pin (1 = high), live | — |
| 0x10040000 | `C64_MODE` | as today | 0x04/0x08 reset also drives port RESET |
| 0x10040003 | `C64_CLOCK_DETECT` | b0 = 1, **b4 = port RESET low** | — |
| 0x1018002A | `C64_BUS_BRIDGE` | latch | latch; v1 uses only "any external bit → drive every cycle" |
| 0x1018002B | `C64_BUS_INTERNAL` | latch | IO1 b0, IO2 b1, ROM b2, IRQ b3; taken at reset / while paused |
| 0x1018002C | `C64_BUS_EXTERNAL` | latch | same bits, for the port |

### 7.2 Ours (JTAG register file, `dbg_addr` 0x1000xx; free range 0x30-0x37)

| Reg | Name | Bits |
|---|---|---|
| 0x30 | `SLOT_CTRL` (R/W) | b0 ARM, b1 ROM_WR_EN, b2 SNOOP (drive internal reads, P6), b3 DMA_EN (P6), b4 PARK_PHI2_RUN (§8.2 option B), b7 CLEAR_FAULT (write 1) |
| 0x31 | `SLOT_STATUS` (R) | b7:0 pins in {RST_I, IRQ, NMI, DMA, EXROM, GAME, R/W, -}; b11:8 masks in effect (ROM/IO1/IO2/IRQ external); b15:12 the same internal; b16 armed; b17 fault; b23:20 fault cause; b24 halted; b25 `cart_present` |
| 0x32 | `SLOT_CNT0` (R) | {port reads served (16), port writes (16)}, wrapping |
| 0x33 | `SLOT_CNT1` (R) | {app DMA cycles at the port (16), VIC ultimax fetches at the port (16)} |
| 0x34 | `SLOT_LAST` (R) | last port cycle: {R/W, strobe code (3), data (8), address (16)} |

App-side copy for the trex_crt app and T21: the same five registers in page 0x7xxx of the
one TRX IO block, at **0x101A7000** (`ARCH-lean-state-access.md` §10; decided 2026-10-01).
The block is unmapped on Gideon's bitstream and reads 0 there ("no TRX slot"). The JTAG
registers 0x30-0x34 need the tighter debug decode of ARCH §10 (`dbg_addr[7:4] == 3`), because
today's read mux decodes only `dbg_addr[3:0]`. A capability bit in the firmware's capability word is not needed: the
registers of 7.1 are Gideon's.

---

## 8. Machine pause (T12 B) and app DMA

### 8.1 What happens today's way

The port's outputs are produced by the core in `gclk`. When the gate shuts after a
`phi2_n` edge, PHI2 stops **low** (phi1), DOTCLK stops, and the port sits in the first
tick of phi1. For a cart that is a C64 whose clock has stopped:

| Cart class | Effect of a stopped PHI2 (up to whole PAL frames, ≥ 20 ms) |
|---|---|
| ROM, EasyFlash (EF1), MegaByter, GMod2, Ocean, Magic Desk: static latches clocked by IO1/PHI2 | none; state frozen, consistent with the machine |
| EF3, Kung Fu Flash, 1541U2, Chameleon: own clock and CPU/CPLD | keep running; the U2 decides "no C64" after 1.25 µs (`slot_timing.vhd` `clock_det`); KFF waits for the next edge. Behaviour after resume is the cart's business. |
| Epyx FastLoad: capacitor charged through a resistor, discharged by IO1/ROML | the ROM switches itself off during any pause longer than its RC time (some 100 µs); code running from its ROM at the pause crashes on resume. A C64 does the same if the ROM goes unread that long. |
| DRAM carts refreshed from PHI2 (GeoRAM is SRAM; real REU 17xx: refresh source unverified) | data loss if the pause exceeds the DRAM retention time. |
| A cart in the middle of its own DMA (real REU transfer) | frozen with the machine: the gate does not care about the port's DMA line, the REU's state machine runs on PHI2. Consistent. |

### 8.2 Required change: the park state, and option B

1. The port output registers run on the ungated `clk` with the gate's enable (`ce`), so
   the one-tick hold after PHI2 falls (p17) still happens on the stop edge, and a **park
   state** can be applied while `halted`: PHI2 0, DOTCLK 0, strobes 1, R/W 1 (released),
   data off, address left driven with the last VIC address, BA as last. Without this the
   port freezes at p16 with R/W possibly still low from a write.
2. **Option A (default): PHI2 stops.** The cart is part of the frozen machine.
3. **Option B (`SLOT_CTRL.PARK_PHI2_RUN`): PHI2 keeps running idle** at 0.985 MHz from the
   ungated clock, address `$FFFF`, R/W 1, no strobes, BA 1 (Gideon's stopped-state
   address, `slot_master_v4.vhd` `stopped`). Keeps clock-hungry carts alive, but every
   cart whose state advances per PHI2 (an REU mid-transfer, a cycle-counting freezer)
   then drifts from the frozen machine. Forced to A while the port's DMA is low.

### 8.3 App DMA to the real cart during the pause

Each DMA byte opens the gate for one cycle (`c64_core.v`): the port sees one full PHI2
cycle, phi1 with the VIC's address, phi2 with the DMA address, the PLA's strobes, R/W and
data exactly as in §4.2 — the cart cannot tell it from a CPU cycle. The first DMA of a
pause also lets the 6510 run to its next read (writes on the way reach the port like any
CPU write; a C64 with an REU does the same). Cost: one machine cycle per byte; an 8 KB bank
is 8192 cycles (8.3 ms of machine time; wall time is the app's per-byte access).

Checks the app path needs (P3):

- **DMA through the PLA:** `C64_DMA_MEMONLY = 0` (the dumper sets it,
  `api_c64_cart_setup_bus`).
- **The CPU port:** the dumper pokes `$00 = $2F`, `$01 = $37` by DMA. On a C64 a DMA write
  to `$0001` lands in RAM, not in the 6510's port; in icepi the REU path writes RAM too
  (`bus_addr = reu_dma_addr`, `ram_select`). So LORAM/HIRAM stay whatever the frozen
  program had. EasyFlash access does not care (ultimax via `$DE02 = $05` maps ROML/ROMH
  regardless of `$01`); a plain 8K/16K cart in a game that banked out ROML does.
  **Verify in sim (test 6 with `$01 = $30` left by the program)**; if it bites, route DMA
  writes to `$0000/$0001` into the CPU port while paused (and restore at resume) — what
  Gideon's `cpu_port_capture` logic in `c64.cc` suggests his core does. *(It bit on the board; built as Q8, §13.11 item 2.)*
- **`cart_check_dma_access`** (`cart_base.c:29`) refuses with preference Auto/External
  and a cart present, because on Gideon's v3.15 core DMA does not reach the cart then. On
  ours it does. The app needs to know: probe `0x101A7000` (ours) and skip the refusal.
  Open question Q5.

### 8.4 Memory lenses on a real cartridge (T21 §3.4.1, 2026-10-03)

A real cartridge has no DDR2 image and no model registers, so the monitor's `cpu`/`cart`
lenses and REST `readmem lens=...` cannot look at it; they read it over the port. The lens
unit (`freeze_resp.v` LK, registers 0x101A5020/24) does one DMA-style bus cycle per byte:

- **Machine paused only** (`halted`; a running machine answers "refused" and the byte is
  unreadable). One machine cycle per byte as in §8.3, so an 8 KB window is 8192 cycles of
  machine time at one request per byte. The firmware's REST path pauses and resumes around
  the whole read.
- **Not Ultimax.** `rom_port = rom_sel && !ultimax` (§13.2) keeps the app's Ultimax (the
  freeze menu's) off the port, so the unit holds `core_ultimax` at 0 for its cycle. The
  freeze session's own `SIDEFX` reads already ran with the app's mode; those are unchanged.
- **Forced select.** The unit does not depend on `$01` or the cartridge's GAME/EXROM:
  `c64_core` gets `dma_force` (ROML, ROMH, I/O) and `c64_bus_arbiter` selects that chip for
  the cycle. The address is the one asked for (`$A000` for ROMH reads what a 16K cartridge
  shows there; the cartridge sees `A13-A15` as the CPU would send them). Only the select
  strobe is forced; the port timing, write inhibit (`rom_wr_en`) and the guard are untouched
  because a read is a read cycle like §8.3's.
- **Side effects.** ROML/ROMH reads: none on the cartridges that matter (EasyFlash, GMod,
  Ocean); Simons' BASIC, C64 Game System and Dinamic bank on a ROM or IO1 read, so a lens read
  of them changes their bank; IO1/IO2 only with `sidefx on`, as with the `io` lens. A cartridge
  whose banking is a ROM-read side effect is read through the lens at the cost of that
  side effect; the monitor says so with `--` when asked without `sidefx` where the byte is I/O.
- The look path (an emulated cartridge, no port) is never used for a real cartridge:
  `pk_ext` (the port serves the read) tells the app to take the REAL path.

---

**Board 2026-10-04 (Alex's EasyFlash 1, Timo's Castle, in the port; bitstream t21-lens run 328,
app trx64/main 029da8ef):** first try read zeros: the lens unit answered `LK_REFUSED`, because
REST's `c64->stop()` pauses only the CPU (GATE_CTRL b0 clear). Fix in `trx_lens_fw.cc`: the read
sets GATE_CTRL b0 for its duration and restores it. After the fix, running and with the freeze
menu open: `lens=cart` and `lens=cpu` at $8000 = `a9 c1 8d 18 03 ...`, equal to Gideon's default
read; $8672 = `2c 00 dc` (`BIT $DC00`), the code at the CPU's PC; $A000/$E000 = $FF (the
cartridge drives nothing there in 16K mode at this point); `lens=io&sidefx=1` $DE00 = open bus.
The game runs on after each read; GATE_CTRL back to "CPU pause only". **PASS.**

Later the same day, long reads dropped single bytes to `$00` (and a continuous read while running crashed a game). Root cause: the
byte protocol of the IO block, not the lens unit: §13.11 item 4 (fixed in `trx_sys.v`, not yet board-confirmed).

## 9. Test plan and phases

### P0 — Evidence (no bitstream of ours, read-only, done by Alex with the existing tools)

With Gideon's flash bitstream, JTAG SAMPLE (`u64ii_bscan.sh`, out/oe cells; no EXTEST),
configuration backed up and restored (`pinmap/runs/cfg.py`):

| Run | Stimulus | Answers |
|---|---|---|
| a | no cart, Quiet, idle | idle levels of D15, D16, R6, T6, W7, AA8, Y9, M18, L15; oe of T18/U17/U20/V19, V18 |
| b | EF in the port, Compatibility, game running | ADDROEn/DATAOEn "on" level; ADDRDIR "out" level; PHI2 (W7) toggling ~50 %; DOTCLK; L15 level with a cart |
| c | write loop to `$DE02`/`$DF00` (existing `easyflash.py`) vs read loop | DATADIR polarity (correlate R6 with R/W V18) |
| d | `$D011` blank vs bad lines on (screen on) | BA (Y9) low share follows bad lines |
| e | EF3 reset button and app `machine:reset` | which of P20 / M18 / R21 moves, in which direction (sense vs drive) |
| f | CIA 1 timer IRQ unacknowledged with CPU in a loop | IRQ (U18) |
| g | multimeter on port pin 2/3 (+5 V) with **our** current bitstream loaded, no cart | is the port powered under our bitstream (answers SLOT_PWR) |

Result goes into `docs/c64u-pinmap.md` (tools fork) and §3.1 here.

### P1 — Idle bitstream (`T16_SLOT = 0`)

All slot/SID balls become ports at their idle levels (§6.1); `CART_DETECT` and
`C64_CLOCK_DETECT` b4 read the pins. Board test, power off → insert ROM cart → power on:
`CART_DETECT` reads 0x01 (8K) / 0x00 (16K) / 0x02 (ultimax) in the app's menu and over
JTAG reg 0x31; nothing else changes; no cart behaviour. Then the same with the EasyFlash.

### P2 — Simulation (§6.3), the gate for every board step after P1.

### P3 — Board, read-only

`T16_SLOT = 1`, `ROM_WR_EN = 0`, `SNOOP = 0`, DMA off.
1. ROM cart, preference Auto: boots into the cart. JTAG reg 0x32 counts reads.
2. trex_crt dumper on the ROM cart (paused machine, DMA): dump equals a dump made on
   Gideon's bitstream (same app, same cart) byte for byte.
3. EasyFlash, Auto: EF menu (or the game) boots, runs 10 minutes, bank switching by IO1.
4. Dumper on the EasyFlash: all 64 banks ROML and ROMH equal the Gideon-bitstream dump.
5. Preference Internal with the EF in: a CRT from the menu runs on `cart_slot`; the port
   sees PHI2 and addresses but no strobes (reg 0x32 stays 0).
6. Pause/resume (JTAG reg 9 bit 3) 100 times during the EF game: no crash, no fault.

### P4 — Writes to flash

Backup dump of the EasyFlash first (P3.4), kept twice. `ROM_WR_EN = 1` from the writer
only (the app sets it around a write, or JTAG for the test). Writer programs a known test
CRT; verify by dump; then write back the backup and verify. EAPI save in a game
(in-game save) works and survives a power cycle.

### P5 — Pause matrix on the board

Option A vs B (§8.2) with each cart Alex has; record which cart classes survive which.

### P6 — Later in T16

`SNOOP` (data out on internal reads), IRQ/NMI outputs (open drain, internal sources to the
port, with the loop exclusion), cart DMA as bus master (address/data/R/W in during phi2,
into the REU path of the core; test: Gideon's `slot_master_v4` in sim, then a real REU or
a U2 if Alex has one).

### P7 — Shadow mode + write journal (T20 coupling, §10)

---

## 10. Shadow mode and the write journal (sketch, built after T20 M1)

The paused/rewound machine cannot take a real cart back in time. Shadow mode makes a real
cart behave like an internal one for as long as T20/T21 need it:

1. **Capture:** the dumper path (§8.3) reads the cart into DDR2 at the CRT image location
   (0x1000000 with trx_io ROM_SEL 1, T60 §7; 0x3C00000 is Gideon's; 16 KB per bank) and detects the mapper (trex_crt's probe).
2. **Switch:** while paused, masks → internal, `cart_slot` takes the image with that mapper;
   the real cart sees PHI2 but no strobes (or option B idle). From here the machine is fully
   snapshot-able; the cart's banking state is `cart_slot` state (T20 §5 chain), like TRX64's
   `CartMapper::get_state/set_state` **[TRX64: crates/trx64-core/src/cart.rs]**.
3. **Journal:** every write the game makes to cart flash or cart RAM goes into the image
   and into a journal (frame, address, old, new) — the same records T20 M1 logs for
   "cart RAM/flash with RMW". A rewind undoes entries; the journal's net effect per flash
   sector is what is still owed to the real cart.
4. **Commit:** on eject, power-down request, or explicitly: pause, erase/program the owed
   sectors through the writer path (§8.3, P4), verify, clear the journal. This is the
   user's CRT auto-save idea applied to a physical cart.

Limits: cart features with their own hardware (EF3 USB/menu, KFF, U2) do not exist in shadow
mode; mappers `cart_slot` does not implement cannot be shadowed.

---

## 11. Resources (estimate)

`slot_port.v`: muxes, IOB registers for 45 balls, synchronizers, guard, counters: ~250 FF,
~300 LUT, no BRAM. Patch 0070: a few wires. No change to `cart_slot`/`cart_logic`.

---

## 12. Open questions

1. **P0 results** decide §3.3 items 1-6; until then no T16 bitstream drives anything.
2. **SLOT_PWR:** if it switches +5 V, does Gideon's design keep it on all the time or only
   with external resources enabled? (Our P1 copies whatever run b shows.)
3. **Pause behaviour default:** option A (PHI2 stops, cart frozen with the machine) or B
   (PHI2 runs idle)? Proposed: A.
   **Decided (Alex, 2026-10-03): A.** PHI2 stops, the cart freezes with the machine (park
   state of §8.2). Option B is not built unless a real cart turns up that needs a running
   clock. A real REU on the port is out of scope (Alex has none and does not need one).
4. **`ext_ultimax` with a real cart:** should the app's freeze ultimax reach the port
   strobes? Proposed: no (internal only); the dumper already clears it.
   **Decided (Alex, 2026-10-03): no.** The freeze ultimax stays internal; the real port stays
   quiet during a freeze. With the shadow session the app does not need the bus at all
   (legacy of Gideon's real-cycle freeze).
5. **trex_crt on our core:** skip `cart_check_dma_access`'s refusal when `0x101A7000`
   identifies our slot block?
   **Decided (Alex, 2026-10-03): yes, and trex_crt keeps both paths.** On our core (slot block
   identified at 0x101A7000) the refusal is skipped and the dump/write runs over the slot
   block's DMA in the pause; on Gideon's core the app behaves as today (the preference check
   and its popup stay). Reason: the app must keep working unchanged on the original firmware.
6. **Address of the app-side slot registers:** answered 2026-10-01: page 0x7xxx of the TRX
   block (0x101A7000); 0x101A0000-0x101AFFFF is unused by Gideon's app (ARCH §10).
7. **"Both" for ROM in Manual mode:** wired-AND of GAME/EXROM and internal-wins for data, as
   proposed in §5.2?
   **Decided (Alex, 2026-10-03): not supported.** A real C64 has one cartridge port, so two
   active carts at once is no real case. "Both" for ROM is treated as Internal: the emulated
   cart serves, the real port stays quiet (no strobes, GAME/EXROM from `cart_slot` only).
8. **CPU port by DMA:** route DMA writes to `$0000/$0001` into the 6510 port while paused
   (§8.3), if sim test 6 shows the dumper needs it?
   **Decided (Alex, 2026-10-03): A.** While paused, DMA writes to `$0000/$0001` go into the
   6510's port (as Gideon's `cpu_port_capture` suggests his core does), and the frozen
   program's port is restored at the resume through the T20 state-bus load path (`SB_LOAD`),
   so the resume stays bit-identical. Built regardless of sim test 6.
9. **Which carts exactly** does Alex have for P3-P5 (types, EF1 or EF3, any REU / U2 / KFF
   for P6)?
   **Answered (Alex, 2026-10-03; earlier hardware tests in the dumper/writer work):**
   C64anabalt (plain 16K ROM), EasyFlash 1 (AM29F040, Timo's Castle), EasyFlash 3
   (MX29LV640, slots), MegaByter (MX29F800CB, Yeti Mountain), GMod2 with EEPROM (Sam's
   Journey, Muddy Racer, Luftrauserz), custom carts Tenebra and Tony's Revenge. No
   C64MegaCart (only Mike has one), no REU. P3-P5 use C64anabalt (ROM), EF3 (EasyFlash +
   flash writes), Yeti Mountain (MegaByter), Sam's Journey (GMod2 + EEPROM). Physical
   freezer/fastload carts (AR, FC3, RR, Epyx …) are out of scope for the real port: they run
   emulated (T33).

---

## 13. M1: what is built (2026-10-03)

M1 = drop-in for Gideon's core: Alex's real carts (Q9) work as on his core. Built in this
order, each its own commits: P1 safety, pass-through, simulation (§6.3 subset). Where this
section and §4-§8 differ, this section is what is built.

### 13.1 P1: every ball a port

`top.v`/`pins.xdc` declare every slot and SID-socket ball of §3.1. From configuration on:

| Ball(s) | Not armed (idle) | Armed |
|---|---|---|
| `SLOT_ADDROEn`, `SLOT_DATAOEn` | 1 (off) | per §13.3 |
| `SLOT_ADDRDIR`, `SLOT_DATADIR` | 1 (FPGA to port: the transceiver never drives the FPGA side) | ADDRDIR 1; DATADIR per §13.3 |
| `SLOT_ROMLn/ROMHn/IO1n/IO2n` | 1 | strobes |
| `SLOT_PHI2`, `SLOT_DOTCLK` | 0 | running |
| `SLOT_BA` | 1 | VIC BA |
| A0-A11 | driven (behind the transceiver, which is off) | address |
| A12-A15, `SLOT_RWn` (3.3 V, direct) | tri-state, PULLUP | driven |
| D0-D7 | input (FPGA does not drive) | §13.3 |
| `SLOT_GAMEn/EXROMn/DMAn/IRQn/NMIn` | input, PULLUP | input |
| `SLOT_RST_I` (P20) | input, PULLDOWN (active-high sense) | input |
| `SLOT_RST_O` (M18) | 1 while our C64 reset is asked for, + 32 µs; else 0 | same |
| `CIA_RSTn` (R21) | input, PULLUP (Gideon's BIT pull), never driven (§13.6) | same |
| `SLOT_SID1n/SID2n` | 1 | 1 |
| `SLOT_SID_ADDR` | 0 | 0 |
| `SLOT_PWR` | 1 (Gideon's level, with and without a cart) | 1 |

Gideon's design (SAMPLE, no cart and with a cart, tools fork `pinmap/runs/sample_*.json`)
runs the bus in both cases. M1 runs it only while armed; idle is static.

**Armed** = `T16_SLOT` build switch (env, default 1; 0 = P1 idle bitstream, `c64_core`
parameter `SLOT_EN`) and the app's `C64_BUS_EXTERNAL` selects ROM (and ROM is not also
internal, Q7), IO1 or IO2, and no fault. No register of ours arms it: Gideon's app decides
(Auto + cart detected, External, Manual), through `C64::ConfigureU64SystemBus`.
`C64_BUS_INTERNAL`/`EXTERNAL` are taken while the C64 is in reset or paused (masks latched
then), arming changes at the start of a cycle.

### 13.2 Selection (replaces §5.2 for M1)

| Resource | Port when | Read data from the port when |
|---|---|---|
| ROM (GAME/EXROM, ROML/ROMH) | EXT b2 and not INT b2 (Both = Internal, Q7), and not `ext_ultimax` (Q4) | always when routed |
| IO1 / IO2 strobes | EXT b0 / b1 (Both: the strobe also goes out) | EXT and not (INT and a CRT is configured) |
| IRQ / NMI from the port | EXT b3 (ORed with `cart_slot`'s) | — |

`cart_present` (icepi, latched at CPU reset) also counts an external ROM/IO1/IO2, through
`flags[11]`. The port's IO2 claims IO2 writes (`io2_we_valid`) when external.
`CART_DETECT` = {EXROM pin, GAME pin}, live. `C64_CLOCK_DETECT` b4 = port RESET sensed.

### 13.3 Schedule (core tick N = N dot4x clocks after the core's `phi2_p` tick; every
port output is a register, so port tick = N + 1)

| Port tick | PHI2 | Address, R/W, strobes | DATADIR / DATAOEn | FPGA D balls |
|---|---|---|---|---|
| 2 | ↑ | CPU/DMA (or VIC if it owns phi2) from core tick 1 | DIR set for this half: 0 for a read routed to the port or a VIC ultimax fetch, else 1; OE off | off |
| 3-17 | 1 | held | OE on for a routed read, a write, a VIC ultimax fetch | write: on, data from core ticks 1-16 |
| 15 (end) | 1 | | **sample D** (CPU latches at core tick 16) | |
| 18 | ↓ | **held** (core tick 17 is still the CPU branch) | OE on only for a write (hold) | write: on (hold) |
| 19 | 0 | VIC address (A0-A11 the VIC's live address, valid from RAS), A12-A15 = 1111 | OE off | off |
| 20 | 0 | | DIR set: 0 in ultimax (VIC may fetch ROMH), else 1; OE off | off |
| 21-31 | 0 | | ultimax: OE on. Else (T74) OE on 27-31 | else (T74) 27-31: on, the VIC's byte from core ticks 24-29 |
| 31 (end) | 0 | | **sample D** (VIC latches at core tick 0) | |
| 0, 1 | 0 | | OE off | off |

Every DIR change has an OE-off tick before and on it; the FPGA drives D only with DIR = 1
for the tick before and the whole time. Write data: valid 15 ticks before PHI2 falls, held
one tick after. CPU read: address at port tick 2, sample end of tick 15 = 14 ticks (444 ns)
minus pad and transceiver delays. VIC ultimax fetch: address complete about port tick 23-24,
sample end of 31: measured 9 ticks (285 ns) from the last address or strobe change (§13.9).

T74, the open bus: in a C64 nobody drives D in a PHI2 half that no device answers, it keeps what the VIC fetched
in the PHI1 before. Outside ultimax the FPGA therefore puts the VIC's byte on D in port ticks 27-31 (DIR is 1 since 20, OE
is off again from tick 0, before the DIR turn); the bus keeps the level it was left at (board 2026-10-10), so the next PHI2
half, where a routed IO1/IO2 read that no cart answers samples it, sees it. Before, the port's D kept the last byte anyone drove (the
FPGA's own write data, or the previous cart read: the EasyFlash probe's `STA $DF00` / `LDA $DF00` read $A5 back).
Bench: `tb_slot openbus` (the bench's balls keep their level when nobody drives).

Write inhibit (§6.1 gate 3): ROML/ROMH never go low in a write cycle unless
`SLOT_CTRL.ROM_WR_EN` (reset 0). IO1/IO2 writes always pass.

### 13.4 Machine pause (Q3: PHI2 stops)

The port registers run on the ungated clock: on the first clock with the gate shut they
show the hold tick (port 18) once more, from then on the **park state**: PHI2 0, DOTCLK 0,
strobes 1, R/W 1, data transceiver off, FPGA D off, address and DIRs held. The first clock
after the gate opens again is still parked, so a resume never repeats a strobe. App DMA
bytes while paused (§8.3) are ordinary cycles at the port. A stop before a falling edge
(T20 position stop, a read cycle) shows PHI2 high for one more tick on resume.

### 13.5 Guard

DMA pin low while armed latches a fault (P6 does not exist yet): every output goes idle
until the next C64 reset. The schedule itself is proven by the bench's contention checker
(§13.7); the §6.2 rule checker in hardware is not built (LUTs).

### 13.6 Reset

- Port RESET (M18 = 1) while our own reset is asked for (app C64_MODE, JTAG, power-on;
  `cpu_reset_req` into `c64_core`), stretched by 32 µs. Not gated by arming (Gideon drives it
  with and without a cart).
- Sense (P20 = 1) while armed resets our C64 for as long as it is high; it is ignored while
  we drive M18 and for 32 µs after. A reset caused by the sense does not drive M18, so a
  stretched sense line cannot loop.
- R21 is not driven: Gideon pulls it low while P20 is high, but what it resets and whether
  it feeds back into P20 is unmeasured; a wrong guess could lock the reset. Open point.

### 13.7 Registers (§7.2, reduced)

JTAG and app reach them as page 7 of the TRX IO block (trx_sys, C64 clock):
`0x101A7000 SLOT_CTRL` b1 ROM_WR_EN (RW, reset 0); `0x101A7004 SLOT_STATUS` (R): b0 GAME
pin, b1 EXROM, b2 DMA, b3 IRQ, b4 NMI, b5 RESET sensed, b11:8 EXT mask in effect, b15:12
INT mask, b16 armed, b17 fault, b18 ROM_WR_EN, b31 1 (our slot block; Gideon's core reads
0 there, Q5). `tools/trxdbg.sh trx 7004` reads it. The counters 0x32-0x34 are not built.

### 13.8 Not in M1

SNOOP, IRQ/NMI outputs, cart DMA (P6); option B; ~~Q8 (DMA writes to $00/$01 into the CPU
port)~~ (built later, §13.11); trex_crt's refusal skip (Q5: on Gideon's core the dumper refuses with Auto + cart
too, so drop-in does not need it); R21. Late GAME/EXROM carts (GMod4) are out of scope: no final GMod4 hardware exists and Alex has no prototype (2026-10-03).

### 13.9 As built (2026-10-03)

- `designs/c64_basic/slot_port.v` inside `c64_core` (parameter `SLOT_EN`, build switch
  `T16_SLOT`), the muxes into icepi's cartridge interface in `c64_core.v`; `c64_top.v`
  exports `vic_write_ab` (`ext_cart_vic`), BA and the VIC's 12-bit address as the
  character ROM sees it (`ext_cart_vaddr`): icepi's `vic_ram_addr` is the DRAM latch,
  complete only after CAS, which left 7 ticks for an ultimax VIC fetch; the live address
  gives 9 (the ROMH strobe itself comes from the PLA at CAS, as on a C64 board).
- `ultimate_sys.vhd`: `C64_BUS_INTERNAL`/`EXTERNAL` as registers (reset 15/0), `CART_DETECT`
  and `C64_CLOCK_DETECT` b4 from the pins. `trx_sys.v`/`trx_io.v`: page 7 (§13.7).
- Reset: the C64 stays in reset while we drive the port RESET and, for the 32 µs after,
  while the line is still sensed low: one RESET line as on a board. The bench's
  EasyFlash was still held in reset when the CPU first wrote to it without this.
- The schedule arms only after the tick counter has seen the VIC's phase (after `rst`).
- **Bench** `designs/c64_basic/sim/slot` (`build.sh`, `run.sh`; Verilator, real 6510 and
  KERNAL; the port pins through a model of both transceivers and the 3.3 V balls to a
  behavioural cart whose data is valid only `acc` ticks after its address/strobe last
  changed, garbage before): tests `idle` (8K cart in, EXTERNAL 0: no PHI2 pulse, BASIC
  READY), `8k` (CBM80 autostart, 256 ROML bytes), `16k` (+ ROMH), `ef` (ultimax boot, IO2
  RAM, IO1 read back, flash sequence $555/$2AA/$555/$123 on ROML and $555 on ROMH reaches
  the cart, 69,523 VIC fetches from ROMH with screen $3C00 and chars $3000, bank 3/1, 16K),
  `ef_ro` (ROM_WR_EN 0: no flash write reaches the cart, IO1/IO2 writes still do),
  `pause` (park for whole frames, no PHI2 pulse; 16 DMA reads of the cart, exactly one
  PHI2 pulse each; resume, the cart's loop goes on), `btn` (the cart's reset button
  resets the C64, we do not drive M18 for it, the cart boots again). Every run checks
  port-D and FPGA-D contention, DIR turns with OE on, idle outputs, write data setup
  (8 ticks) and hold (the PHI2-falling tick keeps address, R/W, strobe, data), address
  stability under a CPU strobe, and every used read sample. **All PASS** with a 250 ns
  cart. Margins: CPU reads pass up to a 13-tick (412 ns) cart, VIC ultimax fetches up to
  9 ticks (285 ns), from the FPGA pins (minus board and transceiver delays).
- Existing benches on this tree, all PASS: `sim/uci` core (needed `TRX_SIM_ROMS`, which it
  had lost with roms-from-app) and ghdl, `sim/pause` (boot, run), `sim/cart` (`carts.py`, every
  type), `sim/cart_unit`, `sim/ring` (boot, run, bootef, log: the internal EasyFlash).
- yosys estimate (synth_xilinx): `slot_port` 97 LUT, 21 CARRY4 (counters), 186 FF;
  `trx_sys` read mux +99 LUT; glue a few tens: about +215 LUT, 77.7 % -> about 78.4 %.


### Board regression without a cart (2026-10-03, run 290, bitstream in SRAM)

No cart plugged. BASIC boots, REST answers, `trx 7004` (SLOT_STATUS) = 0x8000F01F (not armed, no
fault, all pins idle-high), `trx 7008` counters 0. An internal EasyFlash CRT
(`/USB1/jupiter_fracture_(PAL).crt` via REST `runners:run_crt`) runs normally with the port idle
(Alex checked the picture). Next: Alex's real carts with the test plan above.


### 13.10 Board session 2026-10-04 (Alex's carts), what it found

Details and the per-cart table: `T16-board-session.md`. Gideon's default DMA read path read the EF1 byte-exact;
the lens REAL path dropped single bytes to `$00` (about 8 per 2 MB EF, 46 per 1 MB MegaByter, 19 per 512 KB
GMod2) and continuous lens reads while running crashed C64anabalt; MegaByter and GMod2 reported "1 distinct
bank" (`$DE00` writes did not reach the cart); a DMA write to `$01` never reached the 6510 (Lykia and Sam's
Journey set `$01 = $35`, ROML hidden, GMod2 autoselect read ROM bytes); the app never set ROM_WR_EN; trex_crt
refused Auto/External and left Internal unarmed. §13.11 is what was done about each.

### 13.11 Completion (2026-10-05, branch `t16-complete`, app `t16-complete-app`)

**What the simulation found while building the Q8 and lens tests (sim/slot, Verilator, real 6510 and KERNAL):**

1. **App DMA cycles had no hold tick.** An app DMA cycle ends at core tick 16 (`ext_dma_cyc` falls with `phi2_n`);
   the core's cartridge signals then jump back to the paused CPU's at tick 17, so the port released strobe, R/W
   and address together with PHI2 falling. A CPU cycle keeps them (its branch of the PLA lasts to tick 17).
   A cartridge that latches a write on PHI2 falling (the bench model) never saw the DMA write; the EF1 latches
   on the strobe's rise and worked. Likely cause of the board result "1 distinct bank" for MegaByter and GMod2
   (not proven: GMod2's EEPROM bit-bang through `$DE00`, REST pokes too, did work, so its bank latch would have
   to differ from its EEPROM lines). `slot_port.v` now shows the tick-16 values of address, R/W and strobes in the
   hold tick (`q_*`, `hold_t`). Test `dma_wr` (fails without the fix): IO1 registers and a flash sequence by DMA.
   The "bank switching depends on `$01`" reading of the board result (§13.8, T16-board-session risk 1) was wrong
   for I/O: `$01 = $35` shows I/O; the register write was lost.
2. **Q8 built** (decided 2026-10-03: route A, hardware). A DMA write to `$0000`/`$0001` while the CPU is held
   goes into the 6510 port (`cpu6510.v` `dp_*`: `$00` direction, `$01` latch) as well as the RAM below it. The first
   write of a hold saves `{$00, $01 latch, floating bits, fall-off counters}`; the edge that ends the hold
   (the clock that clears `cpu_paused`, `c64_top.v dp_port_restore`) puts them back, so the program's port is
   exact again at the resume, for the whole-machine pause (`held`) and for the CPU pause alone (the app's
   `C64_STOP`, machine running). A state load (`sb_ld`) or reset drops the saved copy. Reads of `$00`/`$01` by DMA are
   unchanged (RAM; the port reads through the state bus, chip 1 word 2). The written value lives only for the
   hold: a debugger that wants a persistent change uses the T21 CPU write path. Tests `cpuport`, `cpuport_cp`:
   `$01 = $35` hides ROML in a DMA read, a DMA write of `$01 = $37` shows it (one PHI2 pulse), port back after
   the resume, program goes on, a hold without a port write leaves the port alone.
   **Board 2026-10-04/05, no cartridge (bitstreams t16-complete-77446c9a and b8def9a3, same result): Q8 works over REST.**
   Alex's Lykia observation ("`machine:writemem` of `$01` leaves the port at `$35`, `trxdbg sb` shows `$01 35`") was the
   state bus, not the port: `sb` serves the CPU words from a shadow that is loaded only while the machine is halted
   (`halted_r`) or at a capture snap (`c64_core.v` `sb_capture`), and under the app's CPU pause alone (`MENU_C64_PAUSE` =
   `stop(false)`) it keeps the value from before the pause. Measured: REST pause, `writemem 0001=35`: a DMA read of
   `$A000` returns RAM (`55 00 ff ff`) instead of BASIC ROM (`94 e3 7b e3`) = the PLA sees `$01 = $35`; `writemem 0001=37`
   shows ROM again; `sb` shows `$01 37` all the time. With `trxdbg freeze on` (GATE_CTRL b0, the pause is a whole-machine
   pause, `sb` live) `sb` shows `$00 2F $01 35` after the write, `$00 27` after a `$00` write, `$00 2F $01 36` after a
   two-byte write, and after the resume `$00 2F $01 37` with the program counting on. The REST write path is not
   MEMONLY (`dma_load_raw_buffer` is a plain `memcpy` with `C64_DMA_MEMONLY` 0; `U64Machine::before_memory_access`
   sets MEMONLY only for reads and for the frozen path), so the first guess that MEMONLY bypassed route A was not the cause
   of that observation. It is still a hole for writers that set MEMONLY (`dma_transfer_frozen`: the freezer's
   `$0000-$03FF` restore, the raw transfers there): `ultimate_sys.vhd` handed a byte to the C64 as a bus cycle only for
   `c64_stop and not dma_memonly`, so a MEMONLY byte went to the RAM's second port and never met the 6510. Built (RTL
   only, the app's path is unchanged): a window **write** to `$0000`/`$0001` is a bus cycle while the CPU is stopped,
   MEMONLY or not (`dma_bus <= c64_stop` for `address(15 downto 1) = 0` and write), so it takes the Q8 path above (port,
   save at the first write of the hold, restore at its end) and writes the RAM below as the bus cycle does. Reads stay as
   decided: a DMA read of `$00`/`$01` is RAM (a bus read; MEMONLY: the RAM port), the port is read through the state
   bus; Gideon's app does not read the port by DMA either (it captures it with an NMI stub,
   `capture_cpu_port_via_nmi`), so no caller expects the port value there. Bench `sim/dma_bus` (GHDL, `ultimate_sys`
   with a stand-in for the RISC-V that writes C64_STOP, C64_DMA_MEMONLY and the window as the app does): MEMONLY writes
   of `$0000`/`$0001` are bus cycles, of `$0002`/`$0400`/`$0500` and all MEMONLY reads are not, nothing is a bus cycle
   while the CPU runs; fails without the fix. The slot bench's `cpuport` tests cover the bus cycle from there into the
   port. Not measured on the board: a MEMONLY write (needs the freezer menu) and any of this with Lykia on the port.
3. **Freeze session (T12): the window answered a real-port cartridge from its model** (`$FF` for ROML/ROMH/IO,
   IO writes only listed for the commit, flash writes dropped, `$01` only in RAM): the freeze-menu start of trex_crt
   (T57 B3) could not have read or written a cartridge. `freeze_resp.v`: with the port serving the class
   (`x_cls`: ROM, IO1, IO2 from `slot_port`; ROM independent of the app's Ultimax, which Q4 keeps internal)
   a window access is a real bus cycle (`p_bus`), the model's GAME/EXROM are the port's pins live, and a window
   write of `$00/$01` updates the model's copy and is a real cycle (so Q8 holds in sessions). Test `session`.
4. **Lens root cause (the `$00` bytes and, very likely, the crash): the byte protocol of the TRX IO block was not
   atomic for several masters.** A 32-bit register is four byte accesses (write: bytes 0-2 collected, byte 3
   commits; read: byte 0 latches, bytes 1-3 from the latch) over one `wbuf` and one `rlatch` in `trx_sys.v`. The
   firmware has more than one master: the REST handler (lens read, ~25 us per byte, almost all of it inside
   such sequences), the "TRX Ring" task (`trx_ring.cc`, `trx_ring_poll()` every 100 ms), the video/UI tasks,
   an app. Another master's byte 0 read between the REST handler's byte 0 and byte 1-3 reads of `LK_RES`
   replaced the latch (so did every write's completion: the old `trx_sys` zeroed the latch on each write ack): bytes 1-3
   (done, busy, flags) came from the other register, `done` read 0,
   `trx_lens.c` `lk_ask()` gave "no answer" and `lens_read()` stores `v < 0` as `$00` (`bad++; v = 0`). The
   rate matches: 10 ring polls per second against about 40 KB/s of lens reads, a window of several accesses
   out of about eight per request, 1e-5..1e-4 per byte (observed 4e-6..4e-5). A torn write is worse: the
   other master's bytes 0-2 in the buffer of a `LK_REQ`, `GATE_CTRL`, `SB_LOAD`, `CAP_CTRL` write (a wrong
   lens request, the machine pause bits, a CPU state load): the plausible cause of the C64anabalt crash.
   The RTL sims do **not** show the byte drops (`lens_soak`: 200,000 look + REAL reads with bank switching,
   `lens_pause`: 300 REST-style pause cycles, 0 errors), which is what points away from the lens unit and the
   port. Reproduced at the protocol: `sim/trx_io` `interleave` (two masters, bytes interleaved at random,
   2,000 rounds): the old `trx_sys` gives 839 torn reads and 710 torn writes. Fix: `trx_sys.v` keeps the write
   bytes and the read latch **per register word address** (tag = word address, the two most recent words of each
   kind; read latches are taken by reads only, not by a write's ack); an access to a word with no entry takes the
   most recent one, as the single buffer did; with the fix 0 torn (two masters at a time: the REST handler and the
   ring poll; a third at the same moment would still tear). Cost (yosys, `trx_sys` alone): +81 LUT, +112 FF.
   **Board A/B (2026-10-04, no cart: `t16_session.sh stress`, lens reads of an emulated EF, 120-490 KB per row, errors
   against a 3-read majority):** old bitstream + old firmware 0; new bitstream + old firmware 0; old bitstream + firmware
   with the policy call 6 in 368 KB, 1 to 3 per 123 KB run, wrong bytes, half of them `$00`; new bitstream + the same
   firmware 10 in 492 KB; the same firmware with the policy call sites removed, new bitstream: 0. The policy's far-page
   access in `ConfigureU64SystemBus` (called from another task than the REST handler) was a third master of the block:
   two entries cover two masters, three tear (and the old bitstream tears with two). So the protocol hazard is
   confirmed on the board, and the hardware fix alone is not enough. The firmware fix: `trx_io_read32/write32` are one
   critical section each (`trx_io.cc`), `trx_io_lock()` around the SB_ADDR/SB_DATA sequence of the lens; the policy
   remembers what it applied and does not touch the bus when nothing changed. With it: 0 in 480 KB on the new
   bitstream, 0 in 368 KB on the old one. Apps (`trx->regs`) are covered by the per-word buffers (two masters at a time)
   only. Real-port lens reads under load: still to be seen in the final session.
   Not fixed by the buffers and still a firmware matter: two masters on the same register pair (SB_ADDR/SB_DATA are one
   shared pointer; GATE_CTRL read-modify-write). **Not confirmed on the board** (no cart now); the session
   script repeats the 3x majority comparison as a plain check (`dump` stage, read path `lens`, any mismatch is a
   failure).
5. **ROM_WR_EN policy "Cartridge Port Writes"** (T57 Q1 = Auto, Alex 2026-10-04): firmware config item
   (`Auto` default, `Writer only`) in the C64 memory settings, shown only on a bitstream with the slot block
   (`SLOT_STATUS b31`). `ConfigureU64SystemBus()` (every C64 reset, every settings change) ends with
   `trx_slot_apply_policy(internal, external)`: Auto sets `SLOT_CTRL.ROM_WR_EN` exactly while the port serves ROM
   (external ROM mask set, internal clear), else 0; Writer only keeps 0. `app_trx_cleanup()` re-applies it when
   an app returns (backstop for T57 §5.4). A running program on a flash cartridge can save in-game then, as on
   Gideon's core; a firmware crash with the bit set stays set until the next bitstream load (risk 9, accepted).

**Not changed:** `slot_port` write inhibit (§6.1 gate 3) stays the default in hardware (reset value 0); only the
firmware policy and the writer's guard (T57) ever set it. Resources: see TODO T16 (CI run).


## Decision: no write protection on the expansion port (Alex, 2026-10-05)

"Weg mit dem Schreibschutz. Den hat ein echter C64 auch nicht." ROM_WR_EN is always on while the slot block is present; writes reach the port cartridge as on a real C64. The "Cartridge Port Writes" setting (Q1: Auto / Writer only) is removed from the app; trex_crt's write guard stays harmless (it finds the gate already open). Replaces Q1 and §6.1's gating.

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

T16: Cartridge port with Alex's real carts

Milestone M1, status ✅. Open (one line, as in the TODO table): –

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- **2026-10-10 evening, T16 closed for M1 (Alex); R21 to M2 as T76** ("das R21 Ding nach M2 ... Der Reset button am EF3 funktioniert nämlich richtig gut"): the EF3 reset button resets our C64 through the P20 sense; R21 stays undriven (§13.8 already listed it as not M1).
- **2026-10-10 evening, EF3 passed (T51 §7 steps 1-2, T57 B11):** whole-EF3 dump four times identical (/Temp twice, /SD, /USB1 with staging; sha256 4ca6f007...), slot dumps as EF1 images, slot 3 write + dump equal. Left in T16: the R21 measurement.
- **2026-10-10 evening, USB dropouts during whole-EF3 dumps are port load, not our core (Alex: "Bei Gideon das selbe. Ist Last auf dem Port."):** the EF3 reading draws enough to drop the USB stick on Gideon's core as well. The dumper's staging (cart read first, file write after) is the answer; no RTL item.
- **2026-10-10 evening, dumper staging reworked and board-tested:** the heap gives no 8.4 MB block (and only 39 of 129 64 KB blocks = 2.5 MB), so the dumper now tries 64 KB heap blocks, then a staging file on /Temp (copied to the target after the read, then truncated; kept for the retry), then direct writing. Board (EF3, app trexcrt-stage 2a3a31ae): "Heap gave 39 of 129 blocks", "Staging in /Temp/...", whole-EF3 dump to /SD = the earlier dumps (sha256 4ca6f007...), /Temp left at 0 bytes. Host tests 34,541/0. USB stick dropped out twice today during a whole-EF3 dump written while reading; not reproduced with staging yet (stick needs replugging).
- **2026-10-10, trex_crt dumper stages the whole CRT in RAM (Alex: "deswegen haben wir ins FLASH bzw. RAM den Kram gelegt und dann nach der Aktion erst auf USB"):** a whole-EF3 dump to /USB1 broke when the stick dropped out ("UsbScsiDriver unavailable (EX)"). Now (app branch trexcrt-stage, merged into integ-m1-app 84835a2a) the dumper reads the cart into one heap block (whole EF3 8,454,208 bytes), then writes the file; a failed write asks "Saving failed. Retry?" without reading the cart again; no memory falls back to writing while reading. One EF3 slot dumps as a type 32 EasyFlash image (AM29F040 EAPI, EXROM 1, chip type 2; T51 D10). Host tests 34,527/0. Board test open.
- **2026-10-10, GMod2 writer status text shortened (Alex: "Text auf der Status Zeile passt bei GMod2 nicht"):** trex_crt `cart_writer.c` shows "Flash+EEPROM verified." instead of the cut-off "Flash and EEPROM written, verified.".
- **2026-10-10 afternoon, GMod2 (Sam's Journey) passed (Alex at the C64U, writer driven by Claude over REST keys):** port dump two passes = first backup (36026cb0); app dump "GMod2, 64 Bank(s)", F5 flash + EEPROM verified OK; app writer rewrite of the app's own dump: all 64 banks OK, "Flash and EEPROM written, verified" (the status line is cut after "ve": too long, app text to shorten), control dump = first backup; EEPROM write by the script: pattern A55A 5AA5 1234 FEDC at words 1000-1003 read back, originals restored; powercycle PASS (core runs), soak 60 s PASS, app_pref (cart-independent, answered from EF1/MegaByter). No in-game save (Alex: nothing to save, never played it). The script's own flash command test failed: autoselect ID $09/$80 instead of $01/$A4 (the command cycles did not reach the AM29F040; the app writes fine): script driver for GMod2 to fix. pause100/matrix: the "1 PC" false FAILs again (alive criterion).
- **2026-10-10 afternoon, MegaByter (Lykia) passed with the fixed trex_crt (Alex at the C64U):** port dump two passes = lykianew.crt (b0e7e0df); app_pref popup PASS; app writer TEST-CRT (backup + pattern in the empty bank 127) written, "verified OK", script dump = test image; BACKUP-CRT written, "verified OK" (Alex power-cycled once because the freeze menu did not open, seen on Gideon's core too), dump afterwards = first backup. pause matrix 15/15. Script fixes the same day: the first backup is the starting point when a run has no dump (Alex: "Die Daten darauf ÄNDERN sich nicht"), the app write test puts its pattern into any all-$FF bank (Lykia has no blank 64 KiB sector: banks 120-124/126 hold data; Alex: "im .crt sehen welche Banks frei sind"). Open findings: soak/pause100 "1 PC" false FAILs on Lykia (main loop on one address, all work in the IRQ: the alive criterion is too strict); one dump run had pass 1 != pass 2 in banks 0-15 (not reproduced in the three runs before and the one after); the script loses its JTAG handle when Alex power-cycles inside a stage.
- **2026-10-10, MegaByter (Lykia) on our core:** script port dump PASS (two passes equal, = lykianew.crt, 128 banks); the app probe called it EasyFlash (fixed in T57, see there); app dump with the fix = port dump. Run stopped before the writer stages; to be continued with the fixed app. Dump names Alex types now use dashes (the C64 keyboard has no underscore).
- **2026-10-10, EF1 remaining stages on the board (package integ-m1-49847451 installed with the .cfw, flash core, Alex at the C64U):** pre PASS, powercycle PASS (our core runs after power-on), soak 600 s PASS, pause100 100/100 PASS (Alex: picture normal), matrix 15/15 PASS (JTAG/REST/gated pause, freeze menu, lens run at 0.1/1/10 s), internal PASS (preference Internal: port quiet, internal copy runs, Alex: picture normal), dump two passes equal (a3d20dd8; bank 16 differs from the first backup = the highscore save). app_err PASS (popup at once, "no chips to write", no writer screen); its sampled "cart unchanged" check gave a false FAIL after Alex had used the freeze menu (a fresh dump right after: same sha; the same check without the menu use: PASS). Script fix: reset into the cart before a compare when Alex touched the machine, and a sampled mismatch is decided by a full dump. persist not run (needs ingame in the same run; the in-game save passed on 2026-10-09 with T72).
- **2026-10-10, app_err needs no backup with --app-validates-open** (Alex: "der Popup-Test schreibt doch gar nichts direkt, dazu müsste ich immer noch F3 machen ! also braucht es KEINEN Dump davor"): the stage compares the cart only when the same run made a backup.
- **2026-10-10, Alex: EF3 stays manual in the T16 suite** ("Handarbeit. Kann eh keiner außer uns."): no `--cart ef3` in tests/cart/t16_session.py; EF3 is tested by hand per T51 §7.
- **2026-10-10, board suite hardened (branch t16-harden, no Alex, no cart written):** `tests/cart/t16_session.py` after Alex's rule "run the BDD tests round in the emulator before using me as beta tester". (1) The backup of the original cart (`dump/t16_backup_<cart>.crt`) is written once (O_EXCL, read-only) and never replaced; a later dump of a changed cart goes to `t16_dump_<cart>-<time>.crt` with a WARN, and every restore round (`writer`, `app_write` BACKUP) writes the FIRST backup. (2) `check_cart`: a cart compare is skipped when the dump before already proved exactly that and nothing touched the cart since; `app_err` after a popup is a sampled check (first and last bank of each 8-bank sector), the final "cart equals the backup" line of `app_write` is no longer an unconditional PASS. (3) `app_err` accepts the validate-on-open popup (branch t57-validate-open) and the older writer-screen refusal; `--app-validates-open` makes the popup mandatory. (4) `powercycle`: German prompt, reopens JTAG in every case, a lost handle fails the later stages instead of crashing them, a foreign core without `--sram` is a clear message. (5) State bus: `Jtag.cpu()` carries `halted` and `CAP_GEN`; the port stage reads the port after a hold only after two capture snaps (`fresh_cpu`) or while halted (`halted_cpu`), so a stale word can no longer confirm the expectation. (6) `run_stages`: every exception, Ctrl-C (second one within 3 s or SIGTERM ends the run) and an unopenable JTAG ends in a FAIL line and the report, never a traceback (`last_exception.txt` in `$T16_OUT`). Offline: `t16_session.sh selftest` 67 cases incl. every new failure path (t16_sim faults stalebus, oldapp, erasebad, norefuse, alexno, notours, jtaglost, rest_exc, kbint, kbint2, sigterm). Board 2026-10-10: no USB-Blaster at the Mac, JTAG stages could not run; REST-only checks and the real JTAG-open error message passed (see docs/archive/board-tests/board-tests-pending.md, "Morgen frueh").

- Task text: Cartridge port with Alex's real carts (spec docs/specs/T16-cartridge-port.md). Done: measurement 2026-10-03 (all pins, EF1 Ultimax start ~8 ms); P1 idle levels + pass-through, port RESET sense into the T39 interrupt; lens reads on a real cartridge PASS 2026-10-04 (T16 §8.4); completion t16-complete (spec §13.11): DMA hold tick, Q8 (DMA write of $00/$01 into the 6510 port), per-word byte buffers in trx_sys (root cause of the torn lens reads), firmware policy 'Cartridge Port Writes' Auto/Writer only.
- **In main 2026-10-06 (ba060ae)**; the freeze-session parts of the branch (freeze_resp x_cls/x_real, bench 'session', ext_cls/ext_ge/rom_on) are dropped, SLIM made every window access of a stopped machine a real bus cycle. **Open:** the final real-cart session (tests/cart/t16_session.py `plan` / `all --write`, spec T16-board-session.md, ~1.5 h read-only, ~3.5 h with writes) has not run: it needs Alex's carts, and the lens fix is not board-confirmed on a real port cart (Alex's 2026-10-06 test ran CRT images from DDR2, not a cart on the port); R21 drive; cart DMA (GMod4 out of scope: no hardware); user port and tape -> T35.
