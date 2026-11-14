# T44 — Flash write gate in our SPI flash wrapper

**Status:** spec 2026-10-03; built 2026-10-03 on branch `t44-gate` with `g_wren_pass` = false,
simulation PASS, board stages 0 and 1 PASS (§11). Branch `t44-wren`: `g_wren_pass` = true (release,
Alex's OK 2026-10-03), stage 2 in the board script, board stage 2 and app-level writes PASS (§11a). **Task:** TODO.md T44 (follows T01).
**Decision (Alex, 2026-10-03, option A):** our core may program and erase only the flash
drive (0x400000-0xFE7FFF) and the config area (0xFE8000-0xFFFFFF). Bitstream
(0x000000-0x21FFFF) and app (0x220000-0x3FFFFF) are forbidden; a write there needs
Alex's explicit OK and must be impossible from software through this gate. Until the gate
is built and board-tested there are no flash writes at all.
**Layout:** `docs/c64u-flash-layout.md`.

## 1. Goal

Menu settings and flash-drive files survive a power cycle, and nothing that runs on our
bitstream (app, a softpatched app, a C64 program through the UCI, a JTAG poke into the IO
bus) can program, erase or reconfigure the lower 4 MB or the chip's status registers.

The allowed area is exactly 0x400000-0xFFFFFF, so the whole decision is one compare: the
top address byte A23..A16 must be at least 0x40. The boundary is 64 KB aligned, so no
4K/32K/64K erase and no page program (it wraps inside its 256-byte page) can cross it.

Non-goals: protecting the flash from Gideon's bitstream or app (they do not run through
our gate), from a JTAG flash programmer, or from the chip being swapped.

## 2. What runs on the SPI flash today (read from the code)

**Hardware.** `designs/c64_basic/ultimate_sys.vhd`:

- `i_flash` (633-647) is Gideon's `spi_peripheral_io` (fixed rate, `g_init_rate` 1 = 25 MHz
  SCK, no CRC) at `0x10060200` (decode 752). SCK leaves through `STARTUPE2.USRCCLKO`
  (`top.v` 358-362); CS, MOSI, MISO are plain pins (`pins.xdc`). There is no other master on
  these pins.
- Register map of `spi_peripheral_io` (only `address(3:2)` is decoded, so the 256-byte
  window aliases every 16 bytes): +0 DATA (write: send the byte; read: send 0xFF, return
  MISO), +4 SPEED (ignored, fixed rate), +8 CTRL (bit 0 force CS, bit 1 CS level; read
  returns them), +C CRC.
- CS (Gideon's `spi.vhd`): with force = 1 the pin follows the level bit, so CTRL = 0x01
  holds CS low across bytes and CTRL = 0x03 holds it high. With force = 0 every DATA access
  gets its own CS-low pulse (low at the send, high again after the 8th bit). CTRL = 0x00
  takes CS high at once.
- `spi_peripheral_io` reads `io_req.address`/`data` one clock after the write strobe (state
  `writing`); `bus_converter` holds them until the ack and splits a 32-bit store into four
  byte accesses at +0..+3, all of which decode as DATA.
- **Today's guard (T01)** (658-685, mux 802-807): a DATA write of 0x06 that is the first
  byte of a transfer goes out as 0x04, so WEL is never set and every program, erase and
  status write is ignored by the chip. It tracks the transfer start from CTRL writes, not
  from the CS pin.

**App** (`software/ultimate-app`, C64U build `target/u64ii/riscv/ultimate/Makefile`):

- Compiles `w25q_flash.cc`, `s25fl_flash.cc`, `s25fl_l_flash.cc`; the probes send 0x66,
  0x99, 0xFF and 0x9F (`s25fl_l_flash.cc` 32-60, `w25q_flash.cc` tester). The C64U answers
  EF 40 18, so `W25Q_Flash` (W25Q128) is used.
- `FLASH_EXTRA_PROTECTION=1` (Makefile 290): `protect_disable`/`protect_configure` are not
  compiled, so the app never sends 0x01/0x31.
- Commands the app sends on the C64U: 0x03 read (`read_dev_addr` 108), 0x4B unique ID,
  0x9F, 0x05 (`wait_ready` 491), 0x06/0x04 around every write, 0x02 page program
  (`write_page` 307), 0x20 4K sector erase (`erase_sector` 343). Always 3-byte addresses.
  No 0x52/0xD8/0xC7, no 4-byte opcodes.
- Sequence per write: CTRL=0, DATA 0x06 (own CS pulse); CTRL=1, opcode, A23..16, A15..8,
  A7..0, [256 data bytes for 0x02]; CTRL=3; poll 0x05 with CTRL=1 until BUSY = 0; CTRL=0,
  DATA 0x04; CTRL=3.
- Callers: `blockdev_flash.cc` (flash drive, 88/94) and `write_config_page` (269): config
  page p is 4K sector 4080+p, i.e. 0xFF0000-0xFFFFFF. `Flash::write_image` with the
  bitstream or app ID would target 0x000000/0x220000: the writes the gate must stop.
- Today a write returns "ready" (WEL = 0, nothing happens), the verify in `write_image` or
  the next read shows the old data.

## 3. Where the gate sits

A new entity `designs/c64_basic/flash_gate.vhd` (ours) between the decode and `i_flash`,
replacing the T01 process and the 0x06 → 0x04 line in the request mux:

```
CPU io_req ─ decode ─ flash_req ─▶ flash_gate ─ gate_req ─▶ i_flash ─ SSn ─▶ flash_gate ─▶ FLASH_CSn
                     flash_resp ◀─            ◀─ gate_resp ─        SCK/MOSI/MISO ──────────▶ pins
```

- The gate is the only master of `i_flash`. It forwards CTRL and plain reads as they come,
  holds a DATA write when it has to (§5) and acks the CPU itself.
- It watches the real `SPI_SSn` out of `i_flash`, not the CTRL writes, and owns the CS pin
  (two jobs: stretching CS-high gaps and the start-up flush, §5.4).
- Every master that can reach `0x10060200` (CPU, softpatched app, JTAG pokes, a UCI
  command handled by the app) passes it. Being in the bitstream, it cannot be switched off
  from software.
- Gate registers at window offset 0x10-0x1F (today an alias of DATA..CRC that no code
  uses; the gate no longer forwards them), §6.

## 4. Commands: what passes, what is checked, what is blocked

Default deny: an opcode not listed as "pass" or "checked" is blocked.

| class | opcodes (W25Q128 names) | rule |
|---|---|---|
| pass | 0x03 read, 0x0B fast read, 0x05/0x35/0x15 read SR1/2/3, 0x9F JEDEC ID, 0x4B unique ID, 0x90 ID, 0xAB release power-down, 0x5A SFDP, 0x06 WREN, 0x04 WRDI, 0x66/0x99 reset, 0xFF (probe dummy, mode reset) | forwarded unchanged |
| checked, 3-byte address | 0x02 page program, 0x32 quad page program, 0x20 4K erase, 0x52 32K erase, 0xD8 64K erase | allowed iff A23..A16 ≥ 0x40 |
| checked, 4-byte address | 0x12 page program, 0x34 quad, 0x21 4K, 0x53 32K, 0xDC 64K | allowed iff A31..A24 = 0x00 and A23..A16 ≥ 0x40 (see Q3) |
| blocked always | 0xC7/0x60 chip erase; 0x01/0x31/0x11 write SR1/2/3; 0x50 volatile SR write enable; 0x42/0x44 security register program/erase; 0x7E/0x98/0x36/0x39 block lock/unlock; 0xB7/0xE9/0xC5 4-byte mode, extended address; 0x38 QPI; 0xEB/0xBB/0xE7 (I/O reads whose continuous mode drops the opcode of the next transfer) and the other dual/quad reads; 0x75/0x7A suspend/resume; 0xB9 power-down; 0x71 write any register; all others | blocked |

Why default deny matters beyond the obvious ones: QPI (0x38), the I/O-read continuous
modes (0xEB/0xBB) and 4-byte mode (0xB7) change how the chip parses the next transfer, so
the gate's byte count would no longer match the chip's. Keeping them out keeps the chip in
plain 1-bit, 3-byte, opcode-first mode, which is the only mode the gate decodes.

WREN passes (generic `g_wren_pass`, §5.5). WEL alone does nothing: every command that can
use it is checked or blocked. WREN cannot be held back until the target is known, because
the chip needs WEL before the opcode and WREN is its own CS transfer; injecting it later
would need a second sequencer and gains nothing over checking the command itself.

## 5. How it decides

### 5.1 Transfer framing from the CS pin

- `idx` (3 bits, saturating) counts bytes clocked in the current transfer; it is cleared
  whenever `SPI_SSn` from `i_flash` is high. With CTRL = 0 every byte is therefore idx 0
  (its own transfer), with CTRL = 1 the bytes count 0, 1, 2, ...
- A byte is "clocked" for both a DATA write (the written byte) and a DATA read (0xFF). Both
  count, because the chip sees both.
- The opcode is the byte at idx 0.

### 5.2 Opcode at idx 0

- pass: forward.
- blocked: forward the substitute 0x9F instead (§6). Decided at once, nothing held.
- checked: **hold** it. The CPU's write is acked, nothing is sent; `held_op` keeps it.

### 5.3 Decision on the address byte

- 3-byte opcode, next byte (idx 1): the byte that would go out (written data, or 0xFF for a
  DATA read) is A23..A16. If ≥ 0x40, send `held_op` then that byte; else send 0x9F then that
  byte. Ack the CPU (for a DATA read: with the MISO byte of the second send).
- 4-byte opcode: hold idx 1 too (A31..A24), decide on idx 2 (A23..A16), then send 3 bytes.
- CS goes high (CTRL write, or software leaves the transfer) while an opcode is held: drop
  it. The chip saw CS low and high with no clock, which it ignores.
- All later bytes of the transfer pass unchanged: after a substitute they are clocked into
  a JEDEC ID read, after an allowed opcode they are the rest of the address and data.

The chip never sees a checked opcode before the gate has seen its target. This needs no
assumption about how the chip handles an aborted or incomplete command.

### 5.4 CS edge cases the gate must close

- **Short CS-high gaps.** CTRL 0x03 then 0x01 back to back gives a CS-high pulse of a few
  clocks. If the chip missed it while the gate counted it, a checked opcode could be read
  by the gate as idx 0 of a new (pass) transfer while the chip still sees it as an address
  or data byte, or the other way round. The gate stretches every CS-high gap: after `SSn`
  rises, it forwards nothing (and drives the pin high) for `g_cs_gap` clocks (default 10 =
  100 ns at 100 MHz, above the chip's CS deselect time, Q5). Then gate and chip agree on
  every boundary.
- **Start-up flush.** The first SCK edges after configuration are lost in `STARTUPE2`, so
  in the first transfer after power-up the chip's bit count is not the gate's. After reset
  the gate keeps the CS pin high until one byte has been clocked; the bootrom already sends
  0xFF with CS high first (`software/bootrom/bootrom.c` 108-112), so nothing changes for
  it, and nothing that comes first can be misread by the chip.
- **CTRL writes while an opcode is held:** the held opcode is dropped first, then the CTRL
  write is forwarded.

### 5.5 Generics

| generic | default | meaning |
|---|---|---|
| `g_wren_pass` | false | false: 0x06 goes out as 0x04 (today's T01 behaviour, kept). true: WREN passes. The release build flips it only after §8 stage 1 and Alex's OK |
| `g_low_limit` | 0x40 | lowest allowed A23..A16 (64 KB steps). C64U 50T layout: 0x40. A U64-II 100T layout would need 0x58 (its app ends at 0x57FFFF) |
| `g_4byte` | false | false: the 4-byte opcodes are blocked like the rest (Q3, decided); true: decoded as in §4 |
| `g_cs_gap` | 10 | minimum CS-high time in clocks |

## 6. What happens on a block

- The chip receives 0x9F instead of the opcode; the rest of the transfer is a harmless
  JEDEC ID read (MISO returns EF 40 18, then whatever the chip clocks out). Software sees
  every access acked as usual; a following `wait_ready` returns at once.
- No CS abort: cutting CS mid-byte would rely on the chip discarding a non-byte-aligned
  command, which the substitute does not need. CS stays where software puts it.
- WEL is left as it is; the app's own 0x04 after `wait_ready` clears it, and while it is
  set every usable command is still checked.
- Gate registers (read-only unless noted; reset only by system reset):

| offset | content |
|---|---|
| 0x10 | bit 0 blocked since cleared (write 1 to clear), bit 1 `g_wren_pass`, bit 2 `g_4byte`, bit 3 an opcode is held |
| 0x11 | blocked count, 8 bit, saturating |
| 0x12 | last blocked opcode |
| 0x13-0x15 | last blocked address A23..A0: the bytes clocked at idx 1-3 (idx 2-4 for 4-byte opcodes) after the block; 0 for opcodes without address |
| 0x16 | allowed program/erase count, 8 bit, saturating (checked opcodes that went out) |

- The app sees a blocked write as before (verify fails, -4 from `write_image`). Logging the
  count at boot and after a failed write is an optional app change for later, not part of
  this spec.

## 7. Simulation

GHDL, like `sim/uci`: `sim/flash_gate/run.sh` with Gideon's `spi.vhd` and
`spi_peripheral_io.vhd` (through `tools/gideon_sources.sh`), `flash_gate.vhd`, a CPU-side
driver that issues io-bus byte accesses as `bus_converter` does, and our own SPI flash
model.

**Flash model (ours):** decodes on SCK, bit count reset by CS, executes on the CS rising
edge only after a complete command (as data sheets describe), keeps WEL, SR1-3, a sparse
16 MB array (erased = FF), and logs every executed command. Two CS variants: one that sees
every CS-high pulse, one that ignores pulses shorter than the deselect time; every test
runs against both. A third variant loses the first three SCK edges after reset.

**Tests:**

1. The app's sequences, copied byte for byte from `write_page`, `erase_sector`,
   `wait_ready`, `read_dev_addr`, the three probes: at 0x400000, 0x7FF000, 0xFE8000,
   0xFFF000 the model executes them; at 0x000000, 0x21F000, 0x220000, 0x3FF000 it
   executes nothing, logs only 0x9F, array unchanged, counters as expected.
2. Boundaries: first address byte 0x3F (blocked) and 0x40 (allowed) for each checked
   opcode; 64K erase at 0x3F0000 blocked.
3. Each blocked opcode with CTRL = 0 (single-byte transfer) and CTRL = 1.
4. 4-byte opcodes: 00 40 allowed, 00 3F blocked, 01 40 blocked; with `g_4byte` = false all
   blocked.
5. Framing attacks: CTRL 1,1 between opcode and address; CTRL 3→1 back to back; CTRL 0
   mid-transfer; DATA read as the address byte (0xFF: allowed, which is what the chip
   sees); 32-bit DATA store carrying opcode + address; system reset with an opcode held.
6. Property run: long random streams of CTRL/DATA writes and reads with random opcodes
   and addresses, both model variants. Pass: the model never executes a program/erase
   with A23..A16 < 0x40, never executes a status, lock, security or mode command, and
   its log equals the gate's forwarded byte stream.
7. `g_wren_pass` false vs true: identical forwarded stream except 0x06 ↔ 0x04, so the
   board test of stage 1 (§8) covers the release build's checking logic.

## 8. Board test

Rule: on the board, a forbidden-address command is only ever sent while WEL cannot be set.
There is no safe way to test the forbidden path with WEL = 1: the gate would then be the
only barrier, and that is what is under test. The release build differs from the stage-1
build only in `g_wren_pass` (sim test 7), so stage 1 is the proof for it.

**Stage 0 — read only, today's bitstream (T01 guard).** Read SR1/SR2/SR3 (0x05/0x35/0x15)
and the JEDEC ID; record BP2..0, TB, SEC, CMP, SRP, WPS (Q4). Read the planned scratch
sector (Q2) and keep its bytes. Nothing new is risked.

**Stage 1 — gate built with `g_wren_pass` = false.** No WREN reaches the chip (T01
behaviour, kept), and the gate is the second barrier. From a small test program (or
`trxdbg.py` pokes into 0x10060200, Q6):

1. CTRL 0, DATA 0x06, then 0x05 read: WEL = 0.
2. Forbidden erase: CTRL 1, DATA 0x20, DATA 0x00, then two DATA reads → **0x40 0x18**.
   These are bytes 2 and 3 of the JEDEC ID, so the chip received 0x9F, not 0x20. A
   forwarded 0x20 would leave MISO undriven during its address phase. Gate: blocked count
   +1, last opcode 0x20, last address 0x00FFFF.
3. Same with 0x02 at 0x22 and 0xD8 at 0x3F (0x40 0x18 each), and 0x12 with 00 00 (one
   read → 0x18: two address bytes were already clocked).
4. 0xC7, 0x60, 0x01, 0x31, 0x50 with CTRL 0: blocked count +1 each; SR1-3 read back equal
   to stage 0.
5. Control on an allowed address: CTRL 1, DATA 0x20, DATA (scratch A23..16), two reads →
   **not** 0x40 0x18; allowed count +1. This sends a real erase command, which the chip
   ignores because WEL = 0 (step 1, and WREN cannot set it in this build). Optional; it
   shows the test can tell the two cases apart. Re-read the scratch sector: unchanged.
6. Normal boot, settings read, flash drive listing: the gate does not disturb reads.

**Stage 2 — release build (`g_wren_pass` = true), only with Alex's OK (given 2026-10-03).**
Recovery ready first: the full dump (`pinmap/runs/flash-dump/`, SHA-256 in the layout doc) and
the JTAG path. Then: erase + program + verify one page of the scratch sector; save settings in
the menu, reload, check; write a file to the flash drive, reload, check ("reload" = bitstream
and app again over JTAG, which re-reads the flash; no power cycle on the dev board). Repeat
stage 1 step 2 only with WEL forced off first (0x04 + 0x05 check), never with WEL = 1.

## 8a. Host update: unlock by T+R+X at power-on (M3, placeholder)

**Moved to M1 and specified in `T50-update-unlock.md`** (which corrects the key source: the C64U's keyboard is the
Bling UART, not the I2C expander; the text below is the original placeholder).

The gate has no software unlock: anything software can send, any software can send, and the
gate would be worth nothing. A legitimate update of the bitstream and app areas goes around it:

- **M1/M2:** over JTAG only (openFPGALoader / Vivado talk to the flash directly; the gate in
  the FPGA is not involved). Holding the cable is the authorisation. No logic.
- **M3 (T23/T26 boot chain, end users without JTAG):** proof of a person at the machine.
  Holding **T, R and X** at power-on or reset opens the gate for the bitstream and app areas
  (`g_low_limit` treated as 0x00, `g_wren_pass` true) until the next reset.

Rules (Alex, 2026-10-03):

1. **Physical keys only (corrected 2026-10-03, see T50):** the C64U's built-in keyboard reaches the FPGA over the Bling controller's UART (`bling_keyboard.v`), not the I2C scan (`c64u_keyboard` reads a classic keyboard on P7). The unlock listens to the Bling UART only; software cannot forge it, so the I2C/JTAG-override guards below are no longer needed. Spec: docs/specs/T50-update-unlock.md.
2. **Only right after the reset.** The keys must be seen down in every scan of a window that
   starts with the first complete scan after `i2c_done` and lasts about 1 s. Keys pressed
   later unlock nothing; one scan without them ends the window. After the window the state is
   fixed until the next reset.
3. **Visible.** While the gate is open the screen shows it plainly (e.g. a red border or
   "FLASH UNLOCKED" from the overlay, and a status bit the app reads), so the state of the
   machine is never in doubt.
4. **Matrix:** R, T and X are all in column PA2 (rows PB1, PB6, PB7). Three keys in one
   column cannot ghost (ghosting needs two rows in two columns), so no fourth key can appear
   or hide.
5. **What stays blocked even when open:** chip erase, status/lock register writes, security
   registers and the mode commands of §4 stay blocked; only program and erase opcodes get the
   wider address range.

Cost: about 20-30 LUT (window counter, three key bits, the sticky unlock flag). Built in M3
with the boot chain, not in the first gate. Open: which keyboard signal names to tap
(`c64u_keyboard.v` output before the merge), and whether the U64-II (no I2C keyboard) needs
another person-present source.

## 9. Cost

| part | LUT | FF |
|---|---|---|
| opcode classifier (pass / checked-3 / checked-4 / blocked) | 25-35 | – |
| idx, hold registers, decision compare | 15 | 20 |
| request sequencer (forward, hold, send 1-3 bytes, ack) and req mux | 40-60 | 15 |
| CS gap counter, start-up flush, pin mux | 8 | 6 |
| registers 0x10-0x16 and read mux | 25-35 | 50 |
| **total** | **~110-160** | **~90** |

No BRAM, no DSP. Minus ~10 LUT for the T01 process it replaces. Against 22,317 LUT today
and the 26,080 ceiling (T25 budget) it does not matter.

## 10. Open questions for Alex

1. Substitute opcode 0x9F (chip echoes its ID, gives stage 1 its proof) — fine, or prefer
   0x05?
2. Scratch sector for stage 1 step 5 and stage 2: 0xFE8000 is in the config area, outside
   the W25Q driver's config pages (0xFF0000+) and outside the flash drive table (ends
   0xFE8000). Use it once stage 0 shows what it holds?
3. 4-byte opcodes: decode them (`g_4byte` = true) or block them on the C64U? The app never
   sends them to the W25Q; blocking is less to test.
4. If stage 0 shows BP = 000: set the chip's own protection (TB = 1, BP = 101 protects
   exactly 0x000000-0x3FFFFF on the W25Q128) as a second barrier? It is one SR1 write,
   needs your OK, and Gideon's updater clears it again (`protect_disable`). The factory
   call `protect_configure(4096)` returns early unless the bits already match
   (`w25q_flash.cc` 463), so they are probably not set.
5. `g_cs_gap` = 100 ns: check against the CS deselect time in the W25Q128 data sheet for
   the exact part on the board.
6. Stage 1 driver: a small test program on the board's app, or `trxdbg.py` pokes over
   JTAG?
7. Later core slots carved out of the flash drive (layout doc, T26) would be writable
   through this gate. Add a second forbidden range then, or leave it for T26?

Decided 2026-10-03: Q1 0x9F (JEDEC ID) as the substitute. Q2 scratch sector 0xFE8000 (first
of the 8 config-area sectors Gideon does not use), only if stage 0 reads it all $FF. Host update: JTAG in M1/M2,
T+R+X at power-on in M3 (§8a). Q3: 4-byte opcodes blocked (`g_4byte` = false default).
Q4: the chip's own BP/TB protection is not set now; an option for M3, decided together with the
host update path (it needs one SR1 write allowed while unlocked, and Gideon's updater clears it).
Q5: 100 ns is above the W25Q128JV/FV CS deselect time for program/erase (tSHSL2 50 ns); stage 0
confirms the part by its JEDEC ID. Q6: stages 0-2 are driven by `trxdbg.py` over JTAG from the Mac
(dev/testing only; no test code in the app). Q7 void: other cores are never stored in flash,
the host soft-boots them into SRAM (T26), so no core slots need a range.

## 11. As built (branch t44-gate, 2026-10-03)

Files: `designs/c64_basic/flash_gate.vhd` (entity `flash_gate`), wired in `ultimate_sys.vhd` as
`i_flash_gate` between `flash_req`/`flash_resp` and `i_flash` (its `busy` and `SPI_SSn` now go to
the gate, the gate drives `flash_csn`); the T01 process and the 0x06 → 0x04 line in the request mux
are gone, the gate does that job (`g_wren_pass` = false). Generics as instantiated: `g_wren_pass`
false, `g_low_limit` 0x40, `g_4byte` false, `g_cs_gap` 10. Bench: `designs/c64_basic/sim/flash_gate/run.sh`.
Board script: `tools/flashgate_test.py` with `tools/flashgate/mailbox.S`.

How the gate keeps its byte count equal to the chip's: it passes one access at a time to `i_flash`,
and only while no byte is on the wire (`busy` low, two clocks after `i_flash`'s ack, CS gap over).
So at each DATA access it knows the CS level the byte will be clocked with: the pin (`SSn` of
`i_flash` or the start-up flush) low → the byte is `idx` of the running transfer; pin high and CTRL
force bit 0 → `i_flash` pulls CS for this byte alone, idx 0; pin high otherwise (CTRL 3, flush) →
the chip does not see it. The force/level bits are taken from the CTRL writes the gate forwards
(`i_flash` takes them from the same writes, and resets them the same way).

Where it differs from §3-§6, or fills in what they leave open:

1. **Bytes clocked with CS high** (CTRL 3, or during the flush) pass unchanged and are not counted;
   only WREN still becomes WRDI there. The app's `erase_sector` sends such a 0x00, which must not
   count as a block.
2. **A program/erase opcode in its own CS pulse** (CTRL 0) has no address: it goes out as 0x9F and
   counts as blocked (address 0). §5.3 would hold it and drop it at the CS rise; sending the
   substitute keeps "every DATA write clocks one byte" and shows up in the counter.
3. **A CTRL write that drops a held opcode also sets idx back to 0**: the held byte was never
   clocked, so the chip's next byte is an opcode. Without this, CTRL 1, 0x20, CTRL 1, 0x01 sent a
   status-register write to the chip (the bench's framing test, and a mutation run, show it).
4. **4-byte opcodes (only with `g_4byte` true):** if the byte at idx 1 is not 0x00, or is a DATA read,
   the opcode is blocked at once (0x9F plus that byte); only A31..A24 = 0x00 written is held too.
5. **Register 0x10** has bits 7..4 = 0101 as a signature, so the board script can tell a gate
   bitstream from today's (where 0x10 is a DATA alias) and refuse to send WREN unless bit 1
   (`g_wren_pass`) is 0. Registers alias every 32 bytes (address bit 4); 0x17 reads 0; the only
   write is 1 to bit 0 of 0x10 (clear the sticky bit).
6. **End of the start-up flush** takes a clock of its own (no access is forwarded in it); in the same
   clock the next byte would be decided as "not seen" and then be seen by the chip (found by the bench).
7. **Stages driven over JTAG:** `jtag_dbg` (top.v) has no master on the IO bus, so a `trxdbg.py`
   poke cannot reach 0x10060200. The script loads a 54-word RISC-V helper with `trxdbg`'s own path
   (RISC-V held in reset, image to DDR2 0x30000, boot ROM magic), and the helper carries out lists
   of accesses from a DDR2 mailbox at 0x38000. No hardware change, no app change; it also runs on
   today's bitstream (stage 0). The app is not running meanwhile and is loaded again afterwards.
8. **Stage 1 safety in the script:** every list is built and checked before the first is sent; a
   byte that is an opcode and not a pure read is refused unless WRDI + SR1 with WEL = 0 came first in
   the same list with no WREN since, and the helper itself checks WEL (guard) right before it and
   stops the list (WRDI, CS high) if WEL is 1. WREN (step 1) only after the gate status read shows the
   signature and `g_wren_pass` = 0. A program/erase opcode at an address ≥ 0x40 is refused except in
   the `--control` step. Stage 2: §11a.
9. **Stage 1 step 5** (`--control`, optional): sent as 0x20, 0xFE, 0x80 and one DATA read, so the
   command (address 0xFE80FF) is for the scratch sector 0xFE8000 itself; expected MISO not 0x18
   (blocked would give the third ID byte). Only with a stage-0 dump of that sector that is all 0xFF;
   the sector is read again afterwards and compared.

Simulation (`run.sh`, GHDL from the OSS CAD Suite, about 45 s): Gideon's `spi.vhd` and
`spi_peripheral_io.vhd` as `i_flash`, the gate, three flash models on the same pins (every CS pulse;
misses CS-high pulses under `g_tdesel`, 50 ns; loses the first three SCK edges after each reset).
Each of the 12 runs (MISO from model 0/1/2 × `g_wren_pass` false/true × `g_4byte` false/true) has
tests 1-6 of §7 plus the stage-1 sequence (test 8), and after each test all three models must have
seen the same transfers as the bytes the gate forwarded while the pin was low, and no model may
have received an opcode outside the pass list or a program/erase below 0x40. Random part: 20,000
accesses per run (about 12,800 transfers, 5,000 substitutions, up to 73 executed program/erase in
the `g_wren_pass` true runs, all at 0x40 or above). Test 7: the chip's streams with `g_wren_pass`
false and true are equal except 0x04/0x06 as opcode (≈2,200 swaps each). Negative control: with
`g_cs_gap` = 1 and a 90 ns model deselect time the framing test fails, with 10 it passes.
`tb_cpu` runs the board script's lists (`flashgate_test.py --sim`) on Gideon's rvlite with the
gate and the model: stage 0 and stage 1 (with `--control`, all three models) PASS, and on a
`g_wren_pass` true build the script stops at its first gate check. All PASS.

Cost (yosys `synth_xilinx` of the gate alone, from `ghdl --synth`): 239 LUT (LUT2-LUT6), 149 FF,
7 CARRY4; Vivado's number comes with the NAS build. Above the §9 estimate, mostly the latched
request, the three-byte send queue and the six registers.

Board commands (not run yet; the board stays untouched until Alex runs them):

    # stage 0, read only, today's bitstream or the gate's (load.sh first if needed)
    tools/flashgate_test.py stage0 --out flashgate-stage0-fe8000.bin
    # stage 1, gate bitstream from the t44-gate build
    tools/load.sh c64_basic t44-gate-latest
    tools/flashgate_test.py stage1 --stage0 flashgate-stage0-fe8000.bin [--control]

`--dry-run` prints every list without touching JTAG.


### Board test 2026-10-03 (bitstream t44-gate, run 286, in SRAM)

- **Stage 0 (read only):** JEDEC ID EF 40 18 (W25Q128), SR1 0x00 (WEL 0, BP 000, TB 0: the chip's own
  protection is not set), SR2 0x02, SR3 0x60, unique ID e46710d2d3362a2e; gate register 0x10 = 0x50
  (gate present, `g_wren_pass` 0, `g_4byte` 0); scratch sector 0xFE8000-0xFE8FFF all 0xFF.
- **Stage 1 (blocked commands, WEL 0), without `--control`: PASS.** WREN arrives as WRDI (WEL stays 0);
  erase 0x000000, program 0x22xxxx, 64K erase 0x3Fxxxx and the 4-byte program all answered with the
  JEDEC bytes (40 18 / 18), i.e. the chip received 0x9F; C7, 60, 01, 31, 50 blocked, SR1-3 unchanged;
  blocked count 9, allowed count 0.
- **Step 6:** our app boots on the gate bitstream, ConfigManager reads its flash pages, settings
  (hostname, MAC) intact, the flash drive lists apps/carts/config/html/roms.
- Not run: `--control` (needs Alex's OK) and stage 2 (first real write to 0xFE8000, Alex's OK).

## 11a. Release build and stage 2 (branch t44-wren, 2026-10-03)

`ultimate_sys.vhd` instantiates the gate with `g_wren_pass` = true (the entity default stays false).
Nothing else changes in the gateware; sim test 7 and stage 1 cover the checking logic.

`tools/flashgate_test.py stage2 [--out FILE]`:

1. Gate check with signature 0101 and `g_wren_pass` = 1 (guard 0xF2 → 0x52; on a false build the
   helper stops at once). Stage 1 on this build stops at its own gate check (0x50 expected), by design.
2. SR1 must be 0x00. Counters noted. The scratch sector 0xFE8000-0xFE8FFF is read and saved to FILE;
   stage 2 refuses unless it is all 0xFF, so leaving it erased leaves it as found.
3. Three writes, each one list: gate check, SR1 with BUSY = WEL = 0 (guard), WREN, SR1 with WEL = 1
   (guard), then 0x20 FE 80 00 (erase) / 0x02 FE 80 00 + 256 pattern bytes (program) / 0x20 FE 80 00
   (erase again). After each, the host polls SR1 (one list per poll) until BUSY = 0 (timeout 2 s
   erase, 0.1 s program), checks WEL = 0 and reads the sector back: all 0xFF / pattern + 0xFF / all
   0xFF. Allowed count +3, blocked count unchanged.
4. Stage 1 step 2 on the release build: WRDI + WEL = 0 check, 0x20 00 → MISO 40 18, blocked count +1.
5. SR1-3 equal to the start.

Script rules in stage 2 (checked before anything is sent): a program/erase opcode needs a WREN
before it in the same list and only one per WREN; only 0x20 and 0x02; all three address bytes
written (not DATA reads) and inside 0xFE8000-0xFE8FFF; every other non-read opcode refused. After
WRDI + WEL = 0 the stage-1 rule applies (forbidden address only). On a mismatch, WEL not set, or BUSY
stuck the script sends WRDI, reads SR1 and stops.

`--wel1-probe` (not part of the default run): one 0x20 at 0x10.... with WEL = 1 right before, then
SR1 and WRDI; expected MISO 40 18 and blocked count +1. There the gate is the only barrier, which
§8 rules out for the board; it exists for the bench (run.sh runs it on all three models) and is
run on the board only with Alex's explicit OK for exactly that.

Simulation: `run.sh` adds stage 2 with `--wel1-probe` on rvlite + gate (`g_wren_pass` true) + each of
the three models (19 checks each, PASS) and stage 2 on a `g_wren_pass` false build (stops at the
gate check, PASS). ALL PASS.

### Board test 2026-10-03 (bitstream t44-wren, run 292, in SRAM)

- **Stage 0:** JEDEC EF 40 18, SR1 0x00, SR2 0x02, SR3 0x60; register 0x10 = 0x52 (`g_wren_pass` 1);
  0xFE8000-0xFE8FFF all 0xFF.
- **Stage 1:** stops at its gate check ("no gate with g_wren_pass = 0"), nothing sent after it.
- **Stage 2: PASS.** Sector saved (all 0xFF); for each of erase, program (256-byte pattern at 0xFE8000),
  erase: SR1 0x00 before, 0x02 after WREN, BUSY cleared, WEL 0 after, read-back as expected. Allowed
  count 3, blocked 0. Erase at 0x00.... with WEL 0: MISO 40 18, blocked count 1. SR1-3 unchanged
  (00 02 60). The scratch sector is left all 0xFF, as found. `--wel1-probe` not run (§8 rule).
- **App (ultimate_f4.bin) on this bitstream:** boots, AppRegistry loads 5 apps from /Flash.
  `LED Strip Settings / Strip Intensity` 1 → 2, `PUT /v1/configs:save_to_flash` ("written"), reload of
  bitstream + app over JTAG: 2; back to 1, save, reload: 1 (restored). FTP `/Flash/t44test.txt`
  (51 bytes) written, there after a reload, deleted, gone after a reload.
- **Counters during app use: not observable.** The gate resets with `cpu_rst` (machine control bit 2,
  the RISC-V reset), and both the helper and the app are loaded through that reset, so a helper read
  after app use shows 0/0 (seen: after the first config save, blocked 0, allowed 0). The evidence that
  the app's writes were not blocked is that each of them persisted. Reading them during app use
  needs the app to report registers 0x10-0x16 (spec §6, optional app change).


## Out of M1 (Alex, 2026-10-04)

The gate stays in the design but open: `g_low_limit => 0` in `ultimate_sys.vhd`, so program and erase pass at every address as on Gideon's core. 4-byte opcodes stay blocked, and so do status-register writes except the one lock of the host app (`g_lock`, T152 D9 / T164 S15; bench `tb_flash_lock.vhd`). Reasons: the 100T flash layout needs another limit (its app slot is 0x3C0000-0x57FFFF), and T49 app updates need the app slot writable. The protection comes back together with T50 (M3).

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

T44: Flash write gate

Milestone M3, status ⏸. Open (one line, as in the TODO table): WEL=1 probe never run; protection returns with T50

Decisions: [ADR-0009 Flash write gate open in M1; T44 and T50 out of M1](../../adr/0009-flash-gate-open-in-m1.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- Task text: Flash write gate (docs/specs/done/T44-flash-write-gate.md): merged with `g_wren_pass` true; board stages 0-2 PASS 2026-10-03 (forbidden commands reach the chip as 0x9F; scratch 0xFE8000 erase/program/erase verified and left all 0xFF); settings save and /Flash files persist across a JTAG reload. App registry was never RAM-only on trx64/main (the define sat in a dead ifneq) and persists in /Flash/config/apps.json; dead define removed (app branch t44-registry). Not run: WEL=1 probe (spec §8: forbidden path never tested with WEL set). Gate counters reset with the RISC-V. Host update: JTAG now, T+R+X at power-on in M3
- **In main 2026-10-06 (t44-open, g_low_limit 0; the board ran with it on 2026-10-06 evening, settings and /Flash files in use).**
- **Out of M1 (Alex, 2026-10-04):** the gate stays in the design but open (g_low_limit 0: program/erase pass at every address, as on Gideon's core); status-register writes and 4-byte opcodes stay blocked. Reason: the 100T layout needs another limit (0x58) and T49 app updates need the app slot writable; protection comes back with T50.
