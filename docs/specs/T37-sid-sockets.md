# T37 — Real SID chips in the C64U's SID sockets

**Status (Alex, 2026-10-03):** not in the M1 release candidate; no chip to test with. Alex tries to get a Dual ARMSID. Q1 answered: no SID, FPGASID, ARMSID or SIDKick here today.

**Status:** spec 2026-10-03, branch `m1-specs`. **Task:** TODO.md T37. **Milestone:** M1 (what Gideon's
app offers for the sockets works on our core).
**Shares with:** T16 (the socket bus is the port's bus, `slot_port.v`), T25.1/T25.3 (UltiSID
decode, mixer), T14 (app I2C master), T12 (pause, shadow session), T47 (POT lines).
Evidence rule: only code (app source under `software/ultimate-app/software`, our RTL), the pin
measurements in the tools fork (`1541ultimate-tools/docs/c64u-pinmap.md`) and Gideon's XDC.
Hypotheses are marked **H**.

## 1. Goal

A real SID (6581, 8580, FPGASID, ARMSID, SwinSID, PDsid, SIDKick, ...) in socket 1 or 2 works as on
Gideon's core: the app detects it, the user enables it, it answers at the configured address, and its
audio reaches HDMI/I2S through the app's mixer. Non-goal: anything that needs the closed UltiSID
(T25).

## 2. What the app does today (code)

### 2.1 Config items (`u64/u64_config.cc`)

| Item | Where | Values |
|---|---|---|
| SID Socket 1 / 2 (`CFG_SOCKET1/2_ENABLE`) | `:408-409` | Disabled / Enabled |
| SID Detected Socket 1 / 2 (`CFG_SID1/2_TYPE`, read-only) | `:410-411`, `:285` | None, 6581, 8580, FPGASID, SwinSID Ultimate, ARMSID, ARM2SID, SidFx, FPGASID Dukestah, PDsid, SIDKick (Teensy), SIDKick Pico |
| SID Socket 1 / 2 1K Ohm Resistor (`CFG_SID1/2_SHUNT`) | `:412-413`; greyed out unless `isEliteBoard()` (`:545-548`) | Off / On |
| SID Socket 1 / 2 Capacitors (`CFG_SID1/2_CAPS`, read-only) | `:414-415`, `:287` | 470 pF / 22 nF |
| SID Socket 1 / 2 Address (`CFG_SID1/2_ADDRESS`) | `:419-420`, `:219-236` | Unmapped, `$D400-$D7E0` in steps of `$20`, `$DE00-$DFE0` (49 entries) |
| Ext DualSID Range Split (`CFG_STEREO_DIFF`) | `:421`, `:266`, `:324` | Off, A5, A6, A7, A8, A9 |
| Auto Address Mirroring | `:426`, `auto_mirror` `:2446` | clears the A5..A9 compare bits that agree across all mapped SIDs |
| Vol / Pan Socket 1, Socket 2 | `:440-461` | mixer sources 2 and 3 (T25.3 §1.1) |
| Bus Operation Mode (`CFG_BUS_MODE`, default index 0 = Quiet) | `io/c64/c64.cc:159`, `:148` | `C64_BUS_BRIDGE` = 0, 1, 2, 3, 5 |

### 2.2 Registers written (`system/u64.h:110-122`, base `C64_IO_BASE` = 0x10180000)

| Reg | Offset | Written by | Value |
|---|---|---|---|
| `C64_SID1/2_BASE` | +0x08/+0x09 | `U64SidAddressing::effectuate_settings` `:871-901`; `SetSidAddress` `:1913`; `S_SetupDetectionAddresses` `:2336` | `u64_sid_offsets[idx]` = A11..A4 of the address (`$D400` = 0x40, `$DE00` = 0xE0, unmapped = 0x01) |
| `C64_SID1/2_MASK` | +0x0C/+0x0D | same | 0xFE & ~split bit (default 0xFE; 0xF0 during detection) |
| `C64_SID1/2_EN` | +0x11/+0x12 | `U64SidSockets::effectuate_settings` `:829-832`; detection sets 1 | 0/1 |
| `C64_STEREO_ADDRSEL` | +0x14 | `:894`; detection sets 0 | index 0-5 (= Off, A5..A9); `stereo_bits` `:324` = {0x00, 0x02, 0x04, 0x08, 0x10, 0x20} are bit positions inside the A11..A4 byte, i.e. A5 = 0x02 |
| `C64_BUS_BRIDGE` | +0x2A | `C64::ConfigureU64SystemBus` `c64.cc:1877` | see §2.1 |

The split is shared by both sockets (`get_sid_addresses` `:1258-1274`). With the split on, the chip
at base `b` is two SIDs: the app sets `mask = 0xFE & ~split`, `base &= ~split`
(`fix_splits` `:1276`) and `SetSidAddress` for a type >= 3 (FPGASID, ARMSID, ...) refuses a
base that already has the split bit (`:1913-1930`). The "B" SID of a socket is the access with the
split bit set (`MapSid` slots 4/5, `:1979-2010`). **H:** `SLOT_SID_ADDR` (K17) is that address bit,
for the chip's second personality (FPGASID/ARM2SID pin that selects the second SID).

### 2.3 Supply, shunt and capacitor selection: I2C, not FPGA

`U64SidSockets::effectuate_settings` (`:767-834`) packs per socket `reg | shunt << 2 | cap << 3`
(`reg` = 0 socket disabled, 3 for a 6581 = 12 V regulator on, 2 otherwise = 9 V; bit 0 voltage,
bit 1 regulator enable, bit 2 shunt, bit 3 caps, `:772-824`), socket 2 in the high nibble, and writes
it with `i2c_write_byte(0x40, 0x01, v)`, `(0x40, 0x03, 0x00)` on the 1.8 V channel (`:817-824`): a
PCA9554-class expander (output register 1, direction register 3). `custom_hardware_init` writes
`0x00` to it at boot (`system/u64ii_init.cc:128-130`). Our `app_i2c.v` passes everything except
address 0x64 to the real pins (header, `designs/c64_basic/app_i2c.v:9-22`), so **this part already
reaches the board** and needs no FPGA work. Risk note: a wrong voltage destroys a chip; T37 never
touches this path and the first test reads the PCA registers back before any chip is inserted.

### 2.4 Detection and access (everything through DMA to `$D4xx/$D5xx`)

- `U64SidSockets::detect()` (`:669-765`, at app start): `C64_MODE_UNRESET`, wait for reset sense
  (T39), `S_SetupDetectionAddresses` (`:2336`): socket 1 at `$D400-$D4FF`, socket 2 at
  `$D500-$D5FF` (base 0x40/0x50, mask 0xF0 = compare A11..A8 only), UltiSIDs parked at `$D600`,
  `SID1/2_EN` = 1, split 0, then 1 ms wait.
- `detectFPGASID` (`:586`): `hard_stop`, DMA writes `0xEE`, `0xAB` to registers 25/26, reads 0 and 1,
  expects `0x1D 0xF5`. `detectRemakes` (`:609`): writes 'S','I','D' to 29-31 with 10 us gaps, reads 27/28
  ("SW", "NO"), then PDsid and SIDKick probes. `detectDukestahAdapter` (`:551`). `S_SidDetector`
  (`:2369`): runs `DetectSidImpl` (6502-side timing loop through C64_PEEK/POKE DMA) for 6581/8580.
- After a changed detection the sockets stay disabled until the user approves; 6581 sets caps 470 pF
  and shunt On, 8580 sets 22 nF and shunt Off (`:721-760`).
- `access_socket_pre/post` (`:2576-2610`): same detection addresses, C64 stopped, for the SID editor
  and the chip drivers (`u64/sid_device_*.cc`).
- `u64_mute_sids` (`:1345`) zeroes mixer bytes 0-7 (UltiSID 1/2 and sockets 1/2, R/L): the **freezer
  mutes the sockets only through the mixer**, so socket audio is mixed digitally in the FPGA.

### 2.5 Audio path (code facts, mapping **H**)

The codec init enables the ADCs with AUXIN to both ADCs (`io/audio/nau8822.cc:21-23, 41-42`; our
`c64u_keyboard.v:56-66` already sends the same writes) and Gideon's XDC has `AUDIO_SDO` on AB3
(`1541ultimate-tools/pinmap/c64u_gideon_issue615.xdc:80, 317`) next to MCLK/BCLK/LRCLK/SDI. Our
`pins.xdc:126-132` and `top.v:63` have no `AUDIO_SDO`. Mixer sources "Socket 1/2" are bytes 4-7
(`setMixer` `:1369-1391`). **H:** the real SIDs' analog output goes into the codec's AUXIN, ADC
data comes back on SDO, and the FPGA mixes it as sources 2/3; which ADC channel is which socket, and
what "Sampler L/R" (sources 4/5) is, is not visible in the app (M5).

## 3. What our core has / lacks (code)

| Piece | State | Where |
|---|---|---|
| Socket balls `SLOT_SID1n/2n`, `SLOT_SID_ADDR` | ports, held 1 / 1 / 0 | `designs/c64_basic/top.v:116-118`, `pins.xdc:99-104`, T16 §13.1 |
| Registers 0x08/09/0C/0D/11/12/14 | page-011 RAM latches, no effect | `ultimate_sys.vhd:996-1006` only wires 0x0A/0B/0E/0F/20-29 |
| Decode of the sockets | none; the arbiter decodes `$D400-$D7FF` as one `sid_cen` | `cores/icepi/c64_bus_arbiter.v:372-377`, `:672-677` |
| Internal SID answer | reDIP: `sid_hit = 1'b1`, **no address decode at all** (the "UltiSID Address" menu has no effect in the default build); T25.1 engine (`TRX_SID=1`): decode in `trx_sid.v:77-79`, `$D400-$D7FF` only | `cores/icepi/c64_top.v:507` |
| The port's bus machine | ROM/IO1/IO2 strobes, per-half-cycle direction, park state, contention guard | `designs/c64_basic/slot_port.v:88-239`, T16 §13.3 |
| DMA cycles to the port | ordinary cycles (app DMA while paused) | T16 §8.3, `c64_core.v:442-457`, arbiter `bus_addr` mux `c64_bus_arbiter.v:411-416` |
| Audio in from the codec | none (no `AUDIO_SDO`) | `top.v:63`, `pins.xdc:126-132` |
| Mixer sources 2/3 | not connected | T25.3 E4 (`T25.3-lp-filter.md:167`) |
| I2C to the PCA9554 (0x40) | passes to the pins | `app_i2c.v:9-22` |

Pin facts from Gideon's bitstream (SAMPLE, sockets enabled, no chip; `c64u-pinmap.md:177-215`):
socket 1 select (M20) is low only for `$D400`, socket 2 select (N20) only for `$D420` (not A5);
"in Quiet mode the bus is only driven for socket cycles, so address lines idle otherwise"; the
sockets share A0-A15, D0-D7 behind the 74ALVC164245 transceivers with the port.

## 4. Design

### 4.1 One bus, three kinds of slave

The sockets are two more chip selects on the port's bus. They get the port's machinery
(`slot_port.v`) unchanged, with four additions.

1. **Decode** (arbiter, C64 clock, next to `cart_io1_cen`): for socket `n`
   `sockn_sel = io_enable && sock_en[n] && ((addr[11:4] & mask[n]) == base[n])`, where `io_enable`
   is the PLA's "I/O visible at `$D000-$DFFF`". `$DE00-$DFFF` bases are decoded here too (the
   `u64_sid_offsets` table has them); a socket hit at `$DE00/$DF00` takes the cycle from IO1/IO2
   (Q4). Compare convention: base compared unmasked, as `trx_sid.v:78` (T25.1 Q1: same open point, one
   measurement settles both, M2). The config bytes cross from `cpu_clk` with two flip-flops as
   `trx_sid.v` does (`cfg_c1/cfg_c`).
2. **Strobes and bus gating** in `slot_port.v`: new inputs `sock1_sel`, `sock2_sel` (CPU half only,
   like `io1_n`), static `sock_en[1:0]`, `stereo_sel[2:0]`. In the CPU half of a cycle that hits a
   socket:
   - `sel_now` (`slot_port.v:129`) includes the hit, so a read sets `h_rd` and DATADIR/DATAOEn follow the existing
     schedule (T16 §13.3: DIR 0 from port tick 2, OE on 3-17, sample D at the end of tick 15,
     `:154`, `:184`); a write sets `h_wr`/`h_ws` and drives D as for IO1 writes.
   - `SLOT_SID1n/2n` (low) from port tick 3 to 17, registered like `p_io1` (`:175`), one tick
     later than the address so that A0-A4/R/W are stable at the chip when CS falls (the tick-2 address
     already sits behind the transceiver, T16 §13.3).
   - `SLOT_SID_ADDR` = `addr[3 + stereo_sel]` for `stereo_sel` 1-5 (A5..A9), 0 for 0 (**H**, Q3), registered with the
     address, held through the cycle.
   - The write inhibit of T16 §6.1 does not apply (SID writes always pass).
3. **Arming per cycle, not per machine** (Gideon's "Quiet" default): the port is armed for a
   resource that stays selected (cart ROM/IO, `present`, `:114`, T16 §13.1); a socket is *not* a standing
   selection. `p_aoen`, `p_ahi_t` (A12-A15 and R/W: the SID needs R/W), `p_doen` for a socket cycle
   come on at port tick 2 only if the decision at core tick 1 (`tk == 5'd1`, `:142`) found a socket hit,
   off again after tick 18; the address transceiver is otherwise off (`:170-171` use `act`
   today). The guard (`g_idle`, `:207`) learns a third cause "socket cycle". With a cartridge armed
   the existing every-cycle drive covers the sockets too. `SLOT_PHI2`/`DOTCLK` must run while a
   socket is enabled and its chip needs the clock (M1, Q2).
4. **Read path into the core:** `slot_port.dout` (the last D sample, already routed to
   `ext_cart_dout`, `c64_core.v:454`) gets a socket flavour: the arbiter's I/O read mux
   (`c64_bus_arbiter.v:372-377`) takes it for a socket hit in place of `sid_dout`. A hit on both a
   socket and an internal SID: the socket answers reads (Q5), writes go to both (the mirrored write is
   what a SID player with "UltiSID + socket" wants).

Register work in `ultimate_sys.vhd`: latches for 0x08/09/0C/0D/11/12/14 (0x2A stays a latch),
outputs `sock_base[15:0]`, `sock_mask[15:0]`, `sock_en[1:0]`, `stereo_sel[2:0]`, cleared by the
RISC-V reset (EN = 0, base 0x01 = unmapped, the value the app writes for "Unmapped",
`u64_config.cc:203`).

### 4.2 Detection must work through DMA

`detect()` and the chip drivers do DMA at `$D4xx/$D5xx` with the CPU stopped. The arbiter's decode
works on `bus_addr`, which carries CPU, REU and app DMA addresses (`c64_bus_arbiter.v:411-416`), so
DMA hits the sockets with no extra path. Two checks: (a) the `dma_stub` answers of `ultimate_sys`
(`ultimate_sys.vhd:413, 892, 903`) are for the time before the core runs, not after; (b) a DMA read of
a register the chip does not drive must not hang: the sample at tick 15 returns whatever the bus
holds (pull-ups on D0-D7? unknown, M7), which the app reads as "no chip" (`detectFPGASID` compares
against `0x1D 0xF5`).

### 4.3 reDIP / T25.1 and the sockets

- **Address decode for reDIP.** `sid_hit = 1'b1` (`c64_top.v:507`) means the internal SID answers
  every `$D400-$D7FF` read. With a socket mapped inside that range the internal answer must be gated:
  the reDIP build gets the same masked compare `trx_sid.v` uses (move it into the arbiter's decode,
  ~20 LUT), fed by `sid_base/sid_mask` which the build already has (`top.v:366`).
- **Priority** is Q5; the default proposal is §4.1 item 4.
- **Audio.** Socket audio is a 48 kHz stereo stream from the codec ADC, not a C64-rate sample. It is
  added at the I2S frame after the SID mix and the pause gate (`top.v:755-758`), with the app's bytes
  4-7 as gains (0x80 = 0 dB, same law as T25.3 §1.1) and the same sum into HDMI audio and UDP audio.
  New: `AUDIO_SDO` port (AB3, LVCMOS18, `pins.xdc`), a 24-bit I2S receiver in the `mclk` domain
  (BCLK/LRCLK are ours, `i2s_out.v`), two 8 x 24 multipliers shared over time (one DSP).
- **Pause.** The machine pause parks `SLOT_PHI2` low (`slot_port.v:128, :165`). A real SID with its
  clock stopped holds its output and (**H**) its state. The freezer mutes via mixer bytes 0-7
  (`u64_mute_sids`), which we honour, so the pause is silent either way. Whether a 6581/8580
  keeps its registers over a 60 s stop is B6; the alternative (free-running `PHI2` while sockets are
  enabled, CS only in phase) breaks CS/PHI2 alignment after a resume.
- **POT.** The sockets' POT pins are board lines; they see paddles only physically (T47).

## 5. Measurement plan on Gideon's core (measured on his bitstream wins)

All with the app's configuration backed up and restored (`pinmap/runs/cfg.py`), nothing saved to flash.

| # | Measures | Needs | How |
|---|---|---|---|
| M1 | `SLOT_PHI2`/`DOTCLK`/`SLOT_BA` with sockets enabled and empty vs disabled, Quiet and Compatibility | USB-Blaster, no chip | SAMPLE 300 captures per state (as the T16 reset series) |
| M2 | Which address bits select socket 1/2: base/mask compare incl. unmasked base, mask 0xF0 (detection) vs 0xFE, `$D400..$D7FF`, `$DE00/$DF00` (does a socket hit suppress IO1/IO2?), overlap of both sockets, overlap with an UltiSID | no chip | write loops from a RAM program, low share of M20/N20 vs address pattern (`pinmap/runs/addr16.py` pattern); IO1 low share while a socket sits at `$DE00` |
| M3 | `SLOT_SID_ADDR` (K17) against `C64_STEREO_ADDRSEL` 0-5: is it A5..A9 of the access, polarity, idle level | no chip | same loops with the split set through REST config ("Ext DualSID Range Split") |
| M4 | Bus drive per cycle in Quiet: AOEn/DOEn/ADDRDIR/DATADIR/A12-A15/RWn low shares, DATADIR 0 on a socket read | no chip | same captures; answers Q2 and §4.1 item 3 |
| M5 | Audio path: is the socket level in the HDMI/I2S stream proportional to mixer bytes 4-7, which channel is which socket | a real SID, an audio capture | play a tone on the chip, mixer socket volume 0 / mid / max, pan hard left / right; capture the UDP audio stream |
| M6 | Who answers a read when UltiSID and a socket hit the same address; do writes reach both | a real SID | voice 3 noise, read `$D41B` 1000 times, compare the distribution with chip-only and UltiSID-only runs |
| M7 | Read data when no chip drives D (open bus value, `detectFPGASID` result) | none | PEEK of an enabled empty socket via DMA |
| M8 | PCA9554 register after a "6581 enable" / disable cycle (supply bits only, nothing inserted) | none; a multimeter at the socket later | `tools/i2cx.py` read of 0x40 reg 1 |

## 6. Test plan

**Simulation** (`designs/c64_basic/sim/slot`, extended; Verilator):
1. Decode: both sockets, mask 0xF0/0xFE, split bit, unmapped (0x01), `$DE00` hit, overlap with IO1.
2. A SID model on the port (behavioural 6581: CS, R/W, A0-A4, D, PHI2; 200 ns read latency) beside the
   transceiver model: write/read each register through the CPU and through the app DMA; the D
   contention checker stays on.
3. Quiet: AOEn/DOEn off in non-socket cycles with no cart; with a cart armed unchanged (the T16
   regression suite passes unmodified).
4. `SID_ADDR` follows the split bit; both sockets at once (A and B personality).
5. Priority and mirrored writes (internal SID + socket at one address), reDIP gate.
6. App flow: the `detectFPGASID`/`detectRemakes` byte sequences against a model that answers the
   FPGASID and ARMSID handshakes; `S_SidDetector` is timing-dependent: smoke run only.
7. Pause/resume with a socket cycle in flight (park state, no CS pulse on resume).

**Board** (Alex, chip in, one test at a time; I2C readback before power to the chip):
B1 PCA register value per type (M8); B2 "SID Detected" shows the chip; B3 enable, play a SID in
socket 1 from `$D400` (HDMI audio, analog out); B4 socket 2 at `$D420` with the split; B5 UltiSID +
socket at once, SID player autoconfig (`SidAutoConfig` `:2070`); B6 freeze/pause for 60 s, no state
loss; B7 EasyFlash in the port while a socket plays; B8 mute through the freezer.

## 7. Resources (rough, not measured)

| Block | LUT | FF | BRAM | DSP |
|---|---:|---:|---:|---:|
| socket decode + stereo mux (arbiter) | 25 | 4 | 0 | 0 |
| `slot_port.v` additions (select, per-cycle gating, guard cause) | 30 | 10 | 0 | 0 |
| reDIP address gate (reDIP builds only) | 20 | 0 | 0 | 0 |
| register latches + synchronizers (`ultimate_sys`, `trx_sid`-style) | 25 | 100 | 0 | 0 |
| I2S receiver, socket gain/sum | 70 | 90 | 0 | 1 |
| Total | about 170 | about 200 | 0 | 1 |

Baseline 25,346 LUT, 77.7 % (`docs/lut-budget.md`). T38's measured `app_input` (42 LUT for the same kind
of latches and OR, `T38-T39-input-reset.md` §8) is the calibration.

## 8. Open questions (Alex)

1. Do you have a real SID or an FPGASID/ARMSID/SIDKick for the sockets (M5, M6, B1-B8)? Which types?
2. Quiet (bus driven only for socket/port cycles, Gideon's default) or drive every cycle once a socket
   is enabled (simpler, what T16 does for an armed cart)? Proposal: Quiet per §4.1 item 3, decided after M4.
3. `SLOT_SID_ADDR`: A5..A9 of the access (H) with idle level 0? M3 answers; no decision needed before it.
4. Socket mapped at `$DE00/$DF00`: socket wins over IO1/IO2 (proposal), or both strobes go out?
5. Read priority when an internal SID and a socket both hit: socket (proposal) or UltiSID; M6 first.
6. Pause with a real SID in the socket: park `PHI2` (proposal, consistent with T16) or keep it running?
   Decided after the 60 s test (B6).
7. Should the reDIP build get address decode now (needed for sockets inside `$D400-$D7FF`), or do we
   require `TRX_SID=1` (T25.1 engine) when sockets are used?

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

T37: Real SID chips in the C64U's SID sockets

Milestone M2, status ⏸. Open (one line, as in the TODO table): Built in the c64u core (T103 §5); a KickSID is here, the c64u board test is next

Decisions: [ADR-0004 M1 release candidate for TREX-internal testers](../adr/0004-m1-release-candidate-internal-testers.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- **2026-10-18, a chip is here:** Alex has a KickSID and tested it on 2026-10-17 on TRX64 Ultimate and on Gideon's
  firmware: "T37 == ok weil Mit KickSID astrein !" ... "Mit trx64U gestern. Und Gideons FW." (T37 is ok because it works
  perfectly with the KickSID; with trx64U yesterday, and Gideon's firmware.) The c64u core's sockets are not tested yet.
- **2026-10-18:** built in the c64u core (T103 §5, `t103-c64u` c8186401): decode in $D400-$D7FF, CS/SID_ADDR on
  the port, the ADC audio mixed in; bench `tb_c64u_sock`. The trx64 core does not have it. Board test waits for a chip;
  the channel mapping (left = socket 1) is M5's hypothesis.
- **2026-10-08:** moved to M2 (Alex: "Genauso T37. nach M2."): [ADR-0047](../adr/0047-tape-and-user-port-to-m2.md).
- Task text: Not in the M1 release candidate (Alex, 2026-10-03: no SID chip here; he tries to get a Dual ARMSID; reDIP-FPGASID is sold out). Real SID chips in the C64U's SID sockets as Gideon's app offers them (SID Socket 1/2: enable, address, shunt, caps): drive SLOT_SID1n/2n and SID_ADDR, share A0-A15/D0-D7 with the port (pins in docs/specs/T16-cartridge-port.md) Spec: docs/specs/T37-sid-sockets.md (socket decode and strobes on slot_port.v, DMA detection, AUDIO_SDO mix; measurements M1-M8 on Gideon's core).
