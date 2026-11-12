# T20 — Delta ring, dump, restore

**Removed from M1 (A13, Alex 2026-10-06):** nothing in this document about `.c64re` or `.c64rering` files is built in M1: §9 (dump), §9.4 and §10 (load, ring save), §17.4 ("Save Timewarp Ring"), the REST routes `timewarp:dump/ringdump/load`, the monitor verbs `dump/undump/ringdump/ringload`, the ABI file slots (`u2p_ring_api_t` dump/save/load, now reserved NULL), `trx_c64re.cc` and the `apps/c64re` app are gone from the code (app commits on a13-no-dump-app). The sections stay as the design for the M2 rebuild (TODO T66); numbers measured with that code (§16.3) are history. M1 Timewarp is rewind and scrub.

**Status:** draft spec, 2026-10-01. Nothing built. Depends on T12 B (machine pause,
simulated). TODO.md T20 is the task; this document replaces its former bullet list as
the design.

**Revision 2026-10-01 (ARCH).** The hardware for capture, restore, logging and the
register block is now the common design in `docs/specs/ARCH-lean-state-access.md`
("ARCH"): one state bus per chip read and written while the machine is stopped, a
one-clock capture of every chip-state bit into shadow flip-flops at the frame's snap edge
(no stop), one engine (DDR2 client) that copies the shadows into the ring,
one IO block. This spec keeps the ring itself (goals, write-log records, index, retention,
restore order, `.c64re`/ring formats, transport, tests). Parts that are gone are marked
**(see ARCH)**.

**One sentence:** every frame, at an instruction boundary on line 0, every chip-state bit
is copied into its shadow flip-flop in one clock without stopping the machine (ARCH §4),
the engine moves the shadows into the ring during the next frame, and every byte written in
that frame is logged with its old and new value, both into the upper
DDR2; from that the hardware can put the whole machine back on any frame of the last
minute, the app can write any of those frames as a `.c64re` that TRX64 opens, and the
transport plays the machine backwards the way TRX64 808 does.

**Rule for this document:** what is copied or adapted from our TRX64 emulator is marked
**[TRX64: file]**. Paths are relative to `/path/to/Development/C64/Tools/TRX64/`.
Hardware facts are from the code in this repo (paths relative to its root); numbers
from `designs/c64_basic/build/utilization*.rpt` (post-route, 2026-10-01 17:52).

---

## 1. Goals and non-goals

### Goals

1. **Ring.** Always-on per-frame record of the whole machine (C64 + drive A) in DDR2,
   60 s window by default, without touching C64 timing while recording.
2. **Restore.** Put the machine back on any frame in the ring, exactly: RAM, colour RAM,
   every chip's internal state, cartridge banking and cart RAM/flash, drive A
   (CPU, VIAs, head, GCR position, drive RAM).
3. **Dump.** Write any ring frame (or "now") as a `.c64re` that TRX64 and c64-re open
   (same container, same JSON node shapes). Our own exact state rides along in an
   extension node, so hardware → file → hardware is lossless.
4. **Load.** Restore a `.c64re` onto the hardware: exact when it carries our extension
   node, best effort (TRX64's "coarse cut") when it comes from TRX64.
5. **Save the ring.** Whole ring to SD/USB: as `.c64rering` (TRX64 opens it) and as a
   native raw copy (fast, exact, hardware only).
6. **Transport.** Rewind / scrub / play backwards on the board, driven from the
   Ultimate app's overlay, USB keyboard F9–F12, and REST, with the TRX64 808 state
   machine and mode line.
7. **Input log** per frame (keyboard matrix, joysticks, RESTORE, external IEC lines,
   interventions), so a frame can be re-run deterministically and checked.

### Non-goals (T20)

- **Instruction-level reverse step** (`rstep`, `whowrote`, `chis`). That is TRX64's
  `delta_ring.rs` / `cpu_history.rs` and belongs to T21 (monitor). The write log here is
  laid out so T21 can add it (§4.4), but T20 does not record PCs per write.
- **Branching / scenarios.** One timeline; diverging cuts the future
  **[TRX64: docs/808-rewind-transport.md §3 decision 3]**.
- **Real cartridges, real SID, real floppy on the IEC port, user port, tape.** They do
  not pause and cannot be put back. Real carts via shadow mode + write journal are
  T16 §10 and need T16.
- **REU memory in the ring** (v1). Registers and engine state are in the ring, the REU
  RAM is not; a restore across REU writes marks the machine "REU RAM uncovered"
  **[TRX64: crates/trx64-core/src/c64re_snapshot.rs `expansion` node, `set_expansion_ram_uncovered`]**.
  The RAM rides every `.c64re` dump. Logging REU writes is phase M7.
  **Superseded by A08 (2026-10-07): the ring is off while the REU is enabled, see "Decision: REU
  means no Timewarp" at the end.**
- **Disk writes undone by rewinding** (v1). Same open hole as TRX64
  **[TRX64: docs/808-rewind-transport.md §7b]**; phase M7 logs GCR writes.
- **A picture per frame in the ring.** TRX64 measured that a backward step that really
  restores the machine is cheaper than a filmstrip and keeps every view correct
  **[TRX64: docs/808-rewind-transport.md §1]**. The hardware does the same: a step
  restores and re-runs one frame (§11.3). Only the last shown frame is kept (§5).
- NTSC. PAL only, like the scaler today.

---

## 2. Terms

| Term here | Meaning | TRX64 name |
|---|---|---|
| **frame k** | the machine time from snap k to snap k+1 | anchor `cp_{frame}_{seq}` |
| **snap** | the short stop at which every chip's state is read over the state bus (ARCH §6.3); always at a 6510 instruction boundary on VIC raster line 0 at or after cycle 12 | capture at instruction boundary, `atInstructionBoundary: true` |
| **chip block** | the latched state of all chips for one snap, a TLV list (§6.1) | the non-RAM part of a `RuntimeCheckpoint` |
| **write log** | every write in a frame: address space, address, old byte, new byte | `WriteRec {addr, old_value, new_value}` in `delta_ring.rs` |
| **keyframe** | full copy of RAM, colour RAM, drive RAM and cart RAM at a snap | full anchor RAM |
| **cursor** | the frame the machine stands on while the transport has it | transport cursor |
| **head** | the newest frame (live edge) | head |
| **cut** | dropping every frame after the cursor because the machine diverged | `truncate_after`, mode CUT |
| **discard** | dropping the whole ring (reset, power, cartridge change) | ring discarded |
| **mark** | a named, pinned frame that survives a cut | mark (Spec 809) |

Naming clash: TRX64's `delta_ring.rs` is the per-instruction undo ring. The T20 "delta
ring" is the per-frame ring (TRX64's checkpoint ring) built from write logs. This
document says **frame ring** and **write log** to keep them apart.

---

## 3. What T12 B already gives (code, not comments)

From `designs/c64_basic/c64_core.v` and `top.v`:

- The C64 runs on `gclk` from `BUFGCE bg_c64` (`top.v:120`). `c64_core` drops CE right
  after a phi2 falling edge (`xc_phi2_n`), with the CPU wherever it is in its
  instruction.
- Resume only when `shut == FRAME-1`, `FRAME = 628992` dot4x clocks (312 × 63 × 32): the
  clock is always left out for whole PAL frames, so the VIC stays locked to the 1080p
  scaler. **Consequence for T20: the machine pause cannot be used once per frame to
  read state.** The capture is a one-clock shadow load without a pause (ARCH §4); the
  pause is for restore and dump only.
- `c64_mem` port B runs on `clk` (ungated): C64 RAM can be read and written while
  paused and while running, one byte per clk (31.53 MHz).
- Drive A stops with the pause through its ticks (`drive_unit.vhd`, `halt` masks
  `tick_16MHz/4MHz/1kHz`). Its ticks come from the 100 MHz domain, not from gclk.
- JTAG register 9: bit 3 machine pause, bit 4 C64_STOP CPU-only, read bit 8 paused.
- Bus DMA while paused opens the clock for one cycle per byte and has the chips' read
  side effects (ICR clear, TOD latch, collision clear). Not usable for an invisible
  snapshot, as docs/machine-pause.md already says.

Nothing reads or writes a chip's internal state today.

---

## 4. Ring design

### 4.1 Capture: snap, not pause

- **Mechanism (see ARCH §4, §5, §6.3):** at the snap edge every bit the state bus reads is
  loaded into its shadow FF by clock enable (one FF per bit, no mux; the machine runs on).
  During the next frame the engine copies the ≈ 460 B of shadows into stream 2; the app
  learns of it by IRQ or by polling, without a deadline. Frame markers pushed into streams
  0/1 at the snap edge tie the block to its position in the write log. A stop that lands
  inside the ≈ 6 µs copy makes that block torn (flagged, accepted: ARCH §4.2, §14 item 2). The shift-chain
  shadows of the first draft (a mux per bit) are dropped.
- **Snap point** (the capture edge). The first 6510 instruction boundary on VIC raster line 0 at or after
  cycle 12 (no badline and no sprite BA window can be open there) that meets ARCH's snap
  condition (border FFs set, no sprite pixel in the last 6 pixels, no VIC access in the last
  CPU cycle, REU idle). The snap position (line 0, cycle c) is stored with the frame. If the
  condition does not hold by line 15, the frame has no chip block and is flagged.
  Interrupt entry: when the IRQ/NMI sequence has been decided, the boundary is taken before
  it with the pending latch set, as VICE/TRX64 do (`cpuIntStatus`).
  **[TRX64: c64re_snapshot.rs `capture_runtime_checkpoint_with`, `"atInstructionBoundary": true`]**
- **Why at an instruction boundary:** the `.c64re` CPU node has no mid-instruction
  state (`CpuSnapshot`: pc, a, x, y, sp, flags, cycles, ba-low flags, jammed). Every
  ring frame is then dumpable without a lossy CPU step. Mid-instruction state of the
  6510 is a read-only view for T21, never restored by T20.
- **Cadence** register, default 1 (every frame), as TRX64's `cadence` verb
  **[TRX64: MONITOR.md "cadence <frames> [secs]"]**. The write log is continuous either
  way; cadence only thins the chip blocks.

### 4.2 Write log

Every write into machine memory is logged at the memory, with the byte it overwrote.
Record layout copied from TRX64 `WriteRec` (4 bytes: `addr u16, old_value u8,
new_value u8`) **[TRX64: crates/trx64-core/src/delta_ring.rs `WriteRec`]**, extended by
a space tag for the other memories:

| Stream | Space | Where the tap sits (see ARCH §7.3) | Record |
|---|---|---|---|
| A | C64 RAM, both ports | `c64_mem.v` port A (read-first BRAM gives the old byte), port B flagged external | 4 B `{addr16, old, new}`, bit in index marks port B writes as external |
| B | colour RAM | colour RAM tap (icepi patch 0051) | 8 B extended |
| B | cart RAM | tap on the cartridge model's memory port (icepi's `c64_cartridge` after the swap) | 8 B extended |
| B | cart flash | written by the app's CW ISR with CPU stores, so pushed by software through `LOG_PUT`: one record per program, one range record per erase plus the old sector copied into the ring by the engine (T16.2 open question 12 answered) | 8 B extended |
| B | EEPROM (GMod2) | 93C86 model write tap | 8 B extended |
| B | drive A RAM $0000–$07FF | `drive_mem.vhd`: drive RAM in a RAMB18 (read-first), written through to DDR2 0xEE0000 | 8 B extended |
| B | I/O $D000–$DFFF (optional, analysis only) | `c64_bus_arbiter` write strobe | 8 B extended |
| B | GCR track bytes (M7) | `floppy_stream` writes via `drive_mem` | 8 B extended |

Extended record (8 B, little endian): `[27:0] address within the space, [31:28] space,
[39:32] old, [47:40] new, [62:48] cycle within the frame, [63] external`. Spaces as
built (`trx_engine.v`): 1 colour RAM, 2 cart RAM, 3 cart flash, 4 EEPROM, 5 drive A RAM,
6 I/O, 7 GCR. Frame boundaries are not in-band in streams A/B (a 4-byte WriteRec has no
room): the marker in the chip-block stream carries both streams' record counts at the
boundary (ARCH §15.7).

Stream A stays 4 B because it is 95 % of the volume. Stream B carries the cycle so the
order of colour/cart/drive writes against each other is kept; stream A's order is
its position. FIFOs and the DDR2 drain are the engine's (ARCH §6.1) **(see ARCH)**.

**Undo** of frame k = write the `old` bytes of frame k's records in reverse order.
**Redo** = write the `new` bytes in order. Both are exact without re-running anything.
This is TRX64's reverse-step contract one level up: RAM bytes from the log, chip state
from the anchor **[TRX64: delta_ring.rs module doc, "CONTRACT"]**.

**Dirty tracking** = the log itself plus a 256-bit page bitmap per frame (which 256-byte
pages of C64 RAM were written), kept in the frame's index entry. Used to copy only
touched pages when materializing a frame and for the UI ("what changed").

Writes into the machine from outside while paused (JTAG poke, app RAM DMA, bus DMA) are
logged with the external bit. They are interventions: if the cursor is not the head,
the first one cuts the future (§11.2).

### 4.3 Keyframes

A keyframe is a full copy of C64 RAM (64 KB), colour RAM (1 KB), drive RAM (2 KB) and
cart RAM (only the cart type's used size) at a snap. Default every 250 frames (5 s).
Roles: bound the seek time (§8.3), let the app materialize any frame without walking
from the head, and resynchronize after a log overflow (§4.6).

Taken in the background by the engine's `RAM_OUT` through `c64_mem` port B while the
machine runs (ARCH §6.2): copy starts at snap k and takes ~2.1 ms (65 536 bytes at one per
clk); writes landing meanwhile are in frame k's log. Fix-up (software): apply frame k's undo
records up to the end of the copy in reverse onto the copy. For every address the result is the old value of its earliest
write in that window, or the copied value if there was none — the value at snap k.
(This is the "RAM by background DMA with writes tracked during the copy" from the
2026-09-30 vision note, made exact.)

### 4.4 Frame index entry (64 B per frame)

| Offset | Field |
|---|---|
| 0 | frame number u32, flags u16 (keyframe, mark/pinned, cut-here, log gap, external writes, REU uncovered, reset), snap cycle-in-line u8, cadence u8 |
| 8 | C64 phi2 cycle counter at snap, u64 (TRX64 `cycles`) |
| 16 | chip block offset u32, length u16, layout version u16 |
| 24 | stream A offset u32, record count u32 |
| 32 | stream B offset u32, record count u32 |
| 40 | input log offset u32, record count u16, keyframe slot u8 (0xFF none), reserved u8 |
| 48 | CRC32 of chip block u32, reserved 12 B |

The per-page bitmap (32 B) sits in front of each frame's stream A data, not in the index.
T21 can later add a PC per write to stream A (8 B records) without changing the index.

### 4.5 Retention, marks, cut, discard

Copied policy **[TRX64: crates/trx64-core/src/checkpoint_ring.rs]**:

- Two bounds, whichever hits first: the **window** (frames, from seconds; default 60 s
  = 3008 frames, `DEFAULT_CHECKPOINT_RING_SECONDS = 60.0`) and the **byte ceiling**
  (the data area, §5.1). The window decides; the ceiling is the safety net. Status
  always reports the depth actually held (TRX64 learned that a fixed budget silently
  shrank a 60 s window to 10 s, `checkpoint_ring.rs` `ANCHOR_BYTES_ESTIMATE` note).
- Eviction drops the oldest unpinned frame. With write logs a frame needs nothing older
  than itself, so eviction is just moving the tail pointer.
- **Marks:** at most 32 (`MAX_MARKS = 32`), named, pinned, listed with cycle and frame.
  Pinning a frame materializes it into a keyframe slot of its own (full RAM + chip
  block), because a cut drops the logs that lead to it. A mark survives a cut
  **[TRX64: MONITOR.md "Marks & the rewind transport"]**.
- **Cut** (`truncate_after`, keep pinned): any write from outside, key, joystick or
  resumed run while the cursor is behind the head drops the frames after the cursor.
- **Discard**, not cut, on reset, power off/on, cartridge insert/eject: the machine was
  replaced, every frame describes one that is gone
  **[TRX64: docs/808-rewind-transport.md §4a, last table row; docs/spec-power-lifecycle.md]**.
  Disk insert/swap: open question 4.

### 4.6 Overflow

The log FIFOs (§5.2) drain to DDR2 continuously. If one overflows (DDR2 starved), the
frame is flagged "log gap": frames before it can no longer be reached from the head by
undo. The engine takes a keyframe at the next snap; frames before the gap stay usable
only from an older keyframe forward up to the gap. Counter `DROPPED` (§12) counts lost
records. With the bandwidth in §5.4 this should never happen; it must not fail silently.

---

## 5. Budgets

### 5.1 DDR2 layout

Byte addresses, 128 MB board. The bottom 64 MB belong to the Ultimate app
(`docs/ultimate-app-hw-spec.md` §3a); the app's CPU cannot address above 64 MB
(26-bit bus).

| Range | Size | Owner | Note |
|---|---|---|---|
| 0x0000000–0x3FFFFFF | 64 MB | Ultimate app | incl. drive A image 0x0EE0000, cart RAM 0x0EF0000, **built-in cartridge slot 0x0F00000–0x0FFFFFF** (Run PRG boot cart, SID/MUS/MOD players; T60 §7), **expansion-port block 0x1000000–0x1FFFFFF: the REU or the CRT image, never both (T60, `top.v` `REU_BASE` word 0x0800000, Gideon's `REU_MEMORY_BASE`; cart ROM via trx_io ROM_SEL 1)**, RAM disk 0x2000000 |
| 0x3C00000–0x3FFFFFF | 4 MB | **free for the ring** with our app (T60 §7); used only by Gideon's firmware on our bitstream (ROM_SEL 0 = his cart ROM address) | the hardware reaches it; the app's address constants no longer name it |
| 0x4000000–0x4FFFFFF | 16 MB | **free** (the REU left it for 0x1000000 in T60) | for the ring later; nothing uses it |
| 0x5000000–0x500FFFF | 64 KB | T20 control: ring header, transport scratch, chip-block staging for "snap now" | |
| 0x5010000–0x504FFFF | 256 KB | T20 frame index, 4096 × 64 B | 81 s at cadence 1 |
| 0x5050000–0x506FFFF | 128 KB | still capture slots, 2 × 64 KB (400 × 270 × 4 bit = 54 000 B each; T12 §5.2, ARCH §7.5) | double buffered |
| 0x5070000–0x5BFFFFF | 11.4 MB | T20 keyframe slots: 64 × 176 KB (64 K RAM + 1 K colour + 2 K drive RAM + 64 K cart RAM + chip block + header) | 32 for marks, 32 for periodic keyframes |
| 0x5C00000–0x7FFFFFF | 36 MB | T20 ring data (chip blocks, streams A/B, input logs), circular | |

Today 0x5000000–0x7FFFFFF is claimed by nothing in `top.v`. T20 claims all 48 MB; any
later claim on the upper DDR2 (LVGL frame buffer, T24) has to come out of this table.

### 5.2 Ring cost per frame (PAL, 19 656 cycles/frame, 50.12 frames/s)

The C64 RAM write rate is not measured yet (M2 adds counters). Three cases:

| | light | TRX64 sizing ratio | worst case |
|---|---|---|---|
| C64 RAM writes / frame | 2 000 | 7 000 (1.26 writes per instruction, from `delta_ring.rs` sizing) | 19 656 (one per cycle, e.g. REU fill) |
| stream A | 8 KB | 28 KB | 77 KB |
| drive RAM writes (stream B) | ~0 (idle) | 2 KB | 157 KB (drive 6502 writing every cycle; not realistic) |
| chip block (fixed) | 2 KB | 2 KB | 2 KB |
| index + bitmap + input log | 0.1 KB | 0.1 KB | 0.2 KB |
| **per frame** | **10 KB** | **32 KB** | **236 KB** |
| **per second** | 0.5 MB | 1.6 MB | 11.8 MB |
| **window in 36 MB** | 72 s (capped at 60 s) | 22 s | 3 s |

So the 60 s default holds for light programs only; a busy game gets ~20 s, which is
twice TRX64's original 10 s scrub window. If M2 measures the middle column, options:
coalesce first/last write per address per frame with a 64-kbit dirty bitmap
(2 RAMB36), or give the ring more of the upper DDR2. Open question 10.

Chip block size estimate from the FF counts in §5.3: ~4.5 kbit = ~560 B of state plus
TLV headers; 2 KB is reserved per frame so layouts can grow without moving the format.

### 5.3 FPGA resources

**(see ARCH §11.)** The first draft's shadow chains (≈ 4,600 state FFs, +4.6 k to +7 k LUT,
4 BRAM tiles) are replaced by the shared state bus, engine and taps; T20's share is in
ARCH's budget table. The full-frame BRAM buffer of docs/machine-pause.md stays unused: the still
lives in DDR2.

### 5.4 DDR2 bandwidth

MIG: 128-bit at ui_clk 75 MHz, 1.2 GB/s peak. Ring writes at worst 11.8 MB/s, still
capture 2.7 MB/s, cart and drive traffic as today. The engine's burst command port (ARCH
§6, client "e": one MIG command per ui_clk, reads pipelined) carries the ring; a toggle
client with one 32-bit access open (~0.4 µs per read, `ddr2_port.v`) would make a 10 s seek
take seconds. The log FIFO absorbs waits.

---

## 6. State ports per chip

**(see ARCH §5.)** The per-chip shadow chains with `snap`/`load`/`sh_*` of the first draft
are replaced by the state bus: word-addressed, side-effect-free reads while stopped,
staged writes applied on the next opening edge, register restore by bus replay (ARCH §5.6).
ARCH §5.4 lists per chip what is read, what is written and what is excluded and why (VIC
sprite delay pipelines, SID audio-only voice state (not restored, ARCH §14 item 3), REU
engine state, VIA
synchronizers, caches).

**Drive timing (changed).** The first draft moved the drive's 4 MHz ticks into the gclk
domain (M0). ARCH keeps the drive on `cpu_clk` and instead makes `halt` freeze every drive
FF (Gideon patch 0003: today the floppy bit timer and the VIA edge capture keep running
while halted) and reads/writes the drive state through a quasi-static bridge. The drive's
phase against the C64 still moves by up to one 4 MHz tick per stop, and its IEC inputs
cross clock domains, so a re-run frame with disk traffic is not guaranteed to be
bit-identical (determinism test §13.1-7: open question 3).

### 6.1 Chip block layout

```
header:  'TWCB' u32, layout version u16, chip count u16, snap cycle-in-line u8,
         snap raster line u16, reserved
per chip: id u8, chip layout version u8, length u16 (bytes), state-bus words (ARCH §5.4)
ids:     0 glue  1 cpu  2 vic  3 cia1  4 cia2  5 sid0  6 sid1  7 reu  8 cart
         9 drive cpu  10 via1  11 via2  12 drive mech  13 input sampler
```

Each chip's bit order is listed in a generated table (`docs/specs/ARCH-state-map.md`,
produced from the HDL in ARCH phase A2), so the app can name every field for the `.c64re` mapping
(§9.3) and the monitor (T21).

---

## 7. Input log

Purpose: re-run a frame exactly (§11.3 picture regeneration, determinism check), and
export the inputs as a TRX64 scenario. Records 8 B, change-only, per frame:

```
[14:0]  cycle within the frame
[15]    reserved
[23:16] source: 0 keys cols 0-3, 1 keys cols 4-7, 2 joy1, 3 joy2, 4 RESTORE,
        5 IEC in (ATN/CLK/DATA from the board port), 6 paddles (when wired),
        7 cart button / freeze, 8 reset line, 9 external RAM write (count only)
[31:24] reserved
[63:32] new value
```

Tapped at the core boundary in `c64_core.v` (`keys[63:0]`, `joy1/joy2`, `restore`,
`iec_*_i`), so it records what the machine got, whoever produced it (C64U keyboard,
USB keyboard via the app's MATRIX_KEYB, REST, joystick ports).

**Replay mode** (CTRL bit, §12): the core takes these inputs from the log instead of
live. Used by picture regeneration and by the determinism test: re-running frame k from
snap k with its inputs must give chip block k+1 bit for bit, and the C64 RAM
writes must equal stream A. A mismatch is a found core bug, the same use TRX64 makes of
its PC stream **[TRX64: concepts doc §1 "the recorded PC stream doubles as a checksum"]**.

**Scenario export:** joystick/key records map onto TRX64 `ScenarioStep` `at_cycle`
entries (`Joy1`, `Joy2`, key down/up) **[TRX64: crates/trx64-core/src/scenario_player.rs]**.
`cycles` in TRX64 are absolute; ours are snap cycle + offset.

---

## 8. Restore

### 8.1 Sequence (any frame k in the ring)

1. **Pause** at the next snap point (ARCH §7.1). The open frame from the last snap to now
   is finished as frame h (head), so the head is a whole frame too.
2. **Quiesce the side paths:** cartridge memory requests done, `drive_mem` line cache
   flushed, REU idle, app bus DMA refused while the engine is busy (status bit; the app
   retries).
3. **Logging off** for engine writes (they would log themselves).
4. **Registers by replay** (ARCH §5.6 step 1): VIC/CIA/SID/REU/cartridge registers of
   block k as bus DMA cycles (≈ 200 machine cycles, all overwritten in step 6).
5. **Memory.** Pick the cheaper start: from the current state undo frames h-1 … k
   (newest record first), or from the nearest keyframe ≤ k copy it in and redo forward
   to k — the engine's `RAM_IN` / `APPLY` (ARCH §6.2).
6. **Hidden state:** `SB_IN` writes block k's W words into staging, `sb_load` for all chips;
   caches invalidated by the load.
7. **Resume phase** (ARCH §7.1). The VIC now stands at (line 0, cycle c_k); the scaler
   expects the position it had at the pause (line 0, cycle c_p). Register RESUME_PHASE =
   (c_k − c_p) × 32 dot4x, mod FRAME; the gate's resume condition becomes
   `shut == (FRAME − 1 + RESUME_PHASE) mod FRAME` instead of `FRAME − 1` (`c64_core.v`).
   Both positions are a few cycles into line 0, so the extra wait is a few µs.
8. **Cursor = k.** Either stay paused (REPLAY, standing) and show the frame (§11.3), or
   resume.

A restore is atomic for the machine: it runs only the replay cycles of step 4 between step 1
and 6. Interventions
while standing (poke, key) cut the future first (§4.5).

### 8.2 Load from a file

Same as 8.1 from step 2, with the memories written from the file and the chip block
from the `.c64re`'s extension node (§9.4), or built from the TRX64 nodes when the node is
missing (partial, §9.3). The ring is discarded (a file replaces the machine), then a
keyframe is taken at the first snap.

### 8.3 Cost

| Move | Work | Time |
|---|---|---|
| one frame back/forward | 7 000 records (TRX64-ratio frame) at 1 byte/clk + register replay + state-bus load | ~0.5 ms |
| 1 s back | 350 k records | ~11 ms (port B bound), DDR2 read 1.4 MB in parallel |
| any frame, keyframes every 5 s | keyframe copy 64 KB (2.1 ms) + redo ≤ 2.5 s (≤ 875 k records) | ≤ ~30 ms |
| without keyframes, 22 s back | 7.7 M records | ~0.25 s |

TRX64's measured backward step is 177 µs **[TRX64: docs/808-rewind-transport.md §1]**;
the hardware step is the same order, so 50 steps/s backwards is affordable as there.

---

## 9. Dump to `.c64re`

> REMOVED FROM M1 (A13, Alex 2026-10-06): design for T66 (M2); no code in M1.

### 9.1 Container (byte for byte TRX64's)

**[TRX64: crates/trx64-core/src/native_snapshot.rs]**

```
0..7    "C64RESNP"
8       format version 1
9..40   sha256(gzBody)
41..    gzBody = gzip(JSON { manifest, checkpoint, mediaPayloads })
```

- `manifest`: `kind "c64re-runtime-snapshot"`, `version 1` (checkpoint schemaVersion),
  `createdAt` ISO UTC, `machine {model "c64-pal", runtimeVersion "trx64-ultimate/<git
  id of the bitstream>/<app version>"}`, `checkpoint {encoding "ta-json-gz/1",
  payloadRef "checkpoint", cycle, pc}`, `media[] {role, format, sha256, sourceName,
  embeddedPayloadRef "media<i>"}`.
- Big arrays as `{"$ta":"Uint8Array","b64":…}` (`ta_u8`).
- `mediaPayloads`: base64 of the mounted D64/G64 file (role `drive8`) and the `.crt`
  (role `cartridge`), as the app loaded them.
- gzip: the app may write **stored** deflate blocks (no compression, CRC32 + ISIZE
  trailer): valid gzip, no compressor needed on the RISC-V. Size then: ~100 KB of
  machine state, plus the media twice (payload and the live node): a D64 adds
  ~233 KB as file and ~360 KB as `driveDiskImage` (35 GCR tracks of ~7.7 KB, base64),
  a 1 MB CRT ~1.4 MB as file and again as `cartFlash`. Real deflate (miniz) later if
  size matters (open question 9).

TRX64 rejects a file on wrong magic, version or hash; the app checks the same on load.

### 9.2 Who writes it

The Ultimate app (or a `.u2a` app, §11.5): command SNAP/SEEK to frame k, mover copies
RAM / colour RAM / drive RAM / cart RAM / chip block from the upper DDR2 into an app
buffer (the app cannot address above 64 MB, §12 mover), builds the JSON, writes the
file to SD or USB. "Dump now" = pause at the next snap point, then the same.

### 9.3 Node mapping and fidelity

Fidelity classes copied from TRX64: **faithful | partial | inspection-only**
**[TRX64: concepts doc §1, Spec 791]**. "Partial" means TRX64 resumes from it but not
cycle-identically to the board.

| `.c64re` node **[TRX64: c64re_snapshot.rs]** | Source on the board | Class |
|---|---|---|
| `schemaVersion: 1`, `atInstructionBoundary: true` | constant (snaps are at boundaries) | faithful |
| `cpu {pc,a,x,y,sp,flags,cycles}` | CPU state words; `cycles` = glue cycle counter | faithful |
| `cpu.maincpu_ba_low_flags`, `jammed` | RDY latch, JAM | partial (VICE encoding) |
| `ram` | C64 RAM | faithful |
| `cpuPortDirection`, `cpuPortValue` | CPU state words (port regs) | faithful; floating-bit counters lost → partial |
| `cia1`, `cia2` (`CiaSnapshot`: `c_cia[16]`, timers, TOD, `irqflags`…) | CIA state words mapped onto VICE fields; VICE pipeline fields get the same placeholders TRX64 emits | partial |
| `sid {regs[32], voices[3]}` (`f`, `noise`, `adsr_value`, …) | SID state words; `f` = accumulator, `noise` = LFSR | partial (no filter state, TRX64 does not carry it either; voices 1-2 envelope/noise not restored, ARCH §14 item 3) |
| `iec` | lines from CIA2/VIA state | partial |
| `cpuIntStatus` | IRQ/NMI lines + 6502n latches mapped to `pendingInt`/`intNames` | partial |
| `alarmsMaincpu: []` | constant, as TRX64 | faithful |
| `keyboard.livePressed`, `joystick1/2`, `paddles` | input sampler → TRX64 key names (`keyboard.rs` table) | faithful |
| `vic` (`regs[0x40]`, raster, `vc`/`vcbase`/`rc`/`vmli`, `idle_state`, `bad_line`, sprites, `color_ram[0x400]`, `irq_status`, collisions) | VIC state words + colour RAM | partial: kawari's pixel pipeline does not map onto `drawCycle`/`dbuf`; TRX64's "coarse-VIC cut" (registers + raster + colour RAM) resumes the vast majority **[TRX64: concepts doc §1]** |
| `vicPresentation` | framebuffers `null` (TRX64 re-sims them) | faithful |
| `drive1541` (VICE stream DRIVE8 + DRIVECPU0 1.4 + 1541VIA1D0 + VIA2D0) **[TRX64: drive_snapshot.rs]** | drive state words, written by the app in VICE module format | partial: Gideon's `floppy_stream` bit position vs VICE rotation fields |
| `drivePart` | power, unit 8 | faithful |
| `driveDiskImage` (GCRIMAGE0 3.2) | GCR tracks in DDR2 as the app holds them | faithful for data, head position partial |
| `cartBytes` | the `.crt` the app loaded | faithful |
| `cartFlash` | live image from DDR2 0x1000000 (the expansion-port block, T60 §7; EasyFlash lo‖hi layout, same as `save_crt`) | faithful |
| `cartState {currentBank, controlRegister, easyflashJumper, easyflashRam}` | cartridge state words (icepi model: `reg_0/reg_1`, flash responder, 93C86), cart RAM | faithful; `flashLoState`/`flashHiState`/`eepromState` mapped as T16.2 §7.8 |
| `expansion {kind "reu", sizeKb, regs, ram}` | REU state words + REU RAM 0x1000000 (dump only; inside the app's 64 MB since T60) | faithful |
| `media {diskPath, imageFormat}` | app's mounted path | faithful |
| **`trx64Hw`** (new, ours) | `{"$ta":"Uint8Array","b64": chip block}` + `"layout": n` + keyframe-equivalent memories are already in the nodes above | exact for the board |

What is lossy going to TRX64, in one list: VIC pixel pipeline, SID filter/analog state,
CIA/6502 internal pipeline encodings, drive rotation phase, floating CPU-port bits.
TRX64 resumes; it will not match the board cycle for cycle. Board → file → board is
exact through `trx64Hw`.

`restore_runtime_checkpoint` reads only the keys it knows (`cp.get(...)`), so the extra
node should be ignored by TRX64. This is verified by a test before relying on it
(§13.3).

### 9.4 Load direction

- With `trx64Hw` and a known layout version: chip block from it, memories from the
  nodes → exact.
- Without (a TRX64 file): build the chip block from the nodes: registers, raster
  position, timers, TOD, SID registers + accumulators/envelopes, CPU, colour RAM. The
  VIC pipeline starts empty: the first raster line after resume may differ (the coarse
  cut). Drive from `drive1541` if present, else drive reset with the disk mounted.
- Refused: wrong magic/version/hash, `machine.model` not `c64-pal`, a VIC position
  outside 312 × 63 (as TRX64's restore refuses).

---

## 10. Saving the ring

> REMOVED FROM M1 (A13, Alex 2026-10-06): design for T66 (M2); no code in M1.

### 10.1 `.c64rering` (TRX64 opens it)

**[TRX64: crates/trx64-core/src/ring_dump.rs, checkpoint_ring.rs `RuntimeCheckpointRingDump`]**

```
0..7  "C64RERNG"
8     format version 1
9..   gzip(JSON RingBufferDump)
RingBufferDump = {
  checkpoint_ring: { budget_bytes, max_entries, seq, current_id,
                     entries: [ { id "cp_<frame>_<seq>", label, frame, cycles,
                                  pinned, byte_size, created_at_ms,
                                  payload: <the same checkpoint tree as §9.3> } ] },
  delta_ring:  { entry_cap, writes_cap, enabled: false, entries: "", writes: "" },
  cpu_history: { capacity, enabled: false, entries: "" } }
```

Every entry is a full checkpoint (TRX64 rehydrates pooled media into each anchor). The
delta and cpu-history rings are empty (T20 records no per-instruction data). Marks ride
as `label` + `pinned`; `current_id` = the cursor.

Size: one entry is ~95 KB of JSON without media (64 KB RAM in base64 ≈ 87 KB). But
TRX64 splices the pooled media back into every anchor of a dump, and its restore
detaches the cartridge when `cartBytes` is null, so each entry must carry its own
`cartBytes`/`cartFlash`/`driveDiskImage`: +~0.6 MB per entry with a mounted D64,
+~2.7 MB with a 1 MB CRT. 60 s at every frame is therefore far out of reach for stored
gzip from the RISC-V. The save takes a range and a **stride** (default: last 10 s,
every 25th frame = TRX64's old cadence → 20 entries: ~2 MB bare, ~14 MB with a D64).
Entries are built one at a time from a scratch copy (keyframe + redo into an app
buffer, never the live machine). Whether TRX64 could take media once per file is
open question 5.


**Turbo node (TRX64 0.12.2, Spec 890; `c64re_snapshot.rs` `turbo_node` / `restore_turbo`).** On
the u64 profile every checkpoint payload carries a top-level `turbo`, sibling of the other
hardware nodes:

```
"turbo": { "enable": u8,      // u64_regs_en: b0 $D031 registers mode, b2 $D030 TurboEnable mode
           "menuIndex": u8,   // menu speed index 0..15
           "menuStalls": bool,// menu "Badline Timing Enabled"
           "d031": u8,        // read-back form: index | (stalls ? 0 : $80); restore masks $8F
           "d030": u8,
           "resetHold": u32 } // PHI2 cycles left of the post-reset hold (2^22 right after reset)
```

TRX64 requires enable/menuIndex/d031/d030 as bytes; menuStalls defaults true, resetHold 0.
Without the node it restores the registers-mode reset state (enable $01, 1 MHz with stalls,
$D031 $00, $D030 $00, no hold) and adds a `turbo:` note. Ours: until T29 is built, write that
reset state explicitly (the machine is at 1 MHz); with T29, write it per anchor from the core's
state (chip block), so a rewind into a boot keeps the remaining hold.

### 10.2 Native ring save (`.trxring`, hardware only)

Raw copy of the T20 area: header, index, keyframe slots in use, data area from tail to
head, plus the app's media (D64/G64, CRT) as files next to it. Exact and fast (≤ 48 MB,
mover + file write). Loading it restores ring and machine on the board. Format:
`"TRXRING1"`, u32 header length, JSON header (bitstream id, layout versions, frame range,
cursor, marks, media file names + sha256), then the raw areas. A TRX64 importer for it
is open question 5.

### 10.3 Where

SD (`/SD/timewarp/`) or the USB stick (`/USB0/timewarp/`), file name
`<label>-<frame>-<yyyymmdd-hhmmss>.c64re|.c64rering|.trxring`, mirroring TRX64's
`<label>-<ms>-<nonce>.c64re` dump naming in spirit. Write speed is not measured; the
app shows progress.

---

## 11. Transport and UI

### 11.1 Who owns the state

The transport state machine lives in **one** place: the app part that drives the
registers (§11.5). Every surface (overlay keys, USB keys, REST) sends an event and
renders the answer; none keeps a run flag of its own. Copied rule and the reason
**[TRX64: docs/808-rewind-transport.md §4b "The rebuild: the daemon owns the state"]**:
two truths about "running" produced five bugs in one afternoon in TRX64. Here the
hardware has a third party (the T12 B gate, the app's C64_STOP): the one flag is the
hardware's "paused" bit, read, never mirrored.

### 11.2 State machine (copied)

**[TRX64: docs/808-rewind-transport.md §4a]**, with the T20 meaning of each state:

```
LIVE · running     machine runs, ring records
LIVE · paused      gate closed at a snap point, ring head = cursor
REPLAY · standing  cursor < head, machine restored to the cursor, paused, frame shown
REPLAY · back      transport steps cursor-1 at the play rate
REPLAY · fwd       transport steps cursor+1 (redo), at the head → LIVE · running
CUT                an intervention while REPLAY: frames > cursor dropped, → LIVE · paused
reset / power / cart change: ring DISCARDED (not cut)
```

- **F11 is a decision, not a mapping:** moving (machine running OR transport playing) →
  stop both; still → at the head run, behind the head `play fwd`
  **[TRX64: 808 §4a "F11 is therefore a decision"; trx64-daemon/src/transport.rs `key_verb`]**.
- **Play runs the emulation from here** (808 decision 4, reversed by the owner after
  use): `play` behind the head cuts the frames ahead and the machine runs; the message
  says how many frames were dropped.
- **A forward step at the head runs one frame** (the ring grows by that frame).
- **Mode line cannot lie** (808 §5): `REPLAY`, `LIVE`, `CUT -N` shown wherever the
  transport is shown.

### 11.3 What the picture shows

A ring frame has no picture. Standing on frame k:

1. restore k (§8.1), audio muted;
2. run exactly one frame in replay mode (inputs from the log), logging into a scratch
   area, not the ring; the scaler shows it live and the still capture writes it into a
   still slot;
3. pause at snap k+1, restore k again (undo the scratch log — the same engine path).

The snap is at line 0, so the frame run in step 2 is whole: TRX64's "an anchor on a frame
boundary draws one frame and keeps it" case; the seam/`rawframe` question does not
arise **[TRX64: 808 §4a "Which frame you are shown"]**. Backward playback = this per
frame: ~20 ms run + ~0.5 ms restores → close to 50 fps. While standing, the app draws the
captured still into the pixel layer (ARCH §7.5, §9.2) instead of a scaler read path
**(see ARCH)**.
Side effects of the regeneration run that the ring does not cover (REU RAM, disk
writes in v1, external IEC devices) are exactly TRX64's §7b list; the status flags
them when the frame touched them.

### 11.4 Keys

The C64U keyboard has F1–F8 only (F2/4/6/8 shifted), so TRX64's F9–F12 rule
("the only function keys that cannot collide with the machine") holds only for a USB
keyboard **[TRX64: 808 §3 decision 5]**.

| Input | ◀\| one back | ◀◀ play back | ⏸/▶ (F11 decision) | \|▶ one forward | other |
|---|---|---|---|---|---|
| USB keyboard (any time, the app takes them before MATRIX_KEYB) | F9 | F10 | F11 | F12 | — |
| C64U keyboard, timewarp overlay open | F1 | F3 | F5 | F7 | RUN/STOP closes the overlay (state stays), M mark, G goto mark, D dump `.c64re`, R save ring |
| REST | `transport/frame -1` | `transport/play back` | `transport/toggle` | `transport/frame +1` | §11.5 |
| Rocker | Menu opens the Ultimate menu, entry "Timewarp" opens the overlay | | Freeze: open question 11 | | |

In the overlay the keys arrive through Gideon's keymapper, which turns F1/F3/F5/F7 into
PageUp/Help/Tasks/PageDown: the overlay must catch the mapped codes (`KEY_PAGEUP` …),
not `KEY_F1` (1541ultimate memory note on UI key mapping). While the overlay is open
the C64U keyboard is parked from the core (the `kb_pause/kb_parked` path in `top.v`),
so transport keys never reach the machine or the input log.

Overlay legend, the TRX64 terminal line verbatim in shape **[TRX64: 808 §4]**:

```
 REPLAY  ◀◀  frame 340/500   -3.2s   ▓▓▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░░░░░░░░░
 F1 ◀|   F3 ◀◀   F5 ⏸/▶   F7 |▶   M mark  G goto  D dump  R ring
```

### 11.5 Software split and REST

Following "firmware minimal, features in apps" (1541ultimate memory note) and App ABI v2:

- **Firmware (our build of the Ultimate app):** maps the TRX IO block (ARCH §10) and the
  engine into the ABI (`trx` table, ARCH §9.1), keeps USB F9–F12 out of MATRIX_KEYB and
  forwards them as events, nothing else.
- **`.u2a` app "timewarp":** the state machine (§11.2), the overlay, `.c64re` /
  `.c64rering` / `.trxring` writers and readers, marks. REST through ABI v2:
  `PUT /v1/apps/timewarp:run?cmd=<verb>&…`, reply JSON ≤ 4 KB.

REST verbs = TRX64 RPC names, so c64-re tooling can call either
**[TRX64: trx64-daemon/src/main.rs `transport/*`, MONITOR.md]**:

| cmd | params | TRX64 equivalent |
|---|---|---|
| `transport/status` | — | same fields: `mode`, `playing`, `direction`, `speed`, `cursor`, `frameIndex`, `frameTotal`, `anchorId`, `frame`, `cycles`, `secondsBehind`, `atHead`, `lastCut`, `line`, `keys` (`transport.rs` `status_json`) |
| `transport/play` | `direction=back\|fwd`, `speed` | `play back\|fwd [speed]` |
| `transport/pause`, `transport/toggle` | — | `pause`, F11 |
| `transport/frame` | `delta=±N` | `frame ±N` |
| `transport/goto` | `frame=` \| `cycle=` \| `target=<mark>` | `goto` |
| `transport/key` | `key=9..12` | `transport/key` |
| `checkpoint/list`, `checkpoint/pin`, `checkpoint/unpin` | `id` | same |
| `mark`, `unmark` | `name` | `mark`, `unmark` |
| `cadence`, `window` | `frames`, `seconds` | `cadence`, `window` |
| `snapshot/dump` | `frame` (default cursor), `path` | `dump` |
| `snapshot/undump` | `path` | `undump` |
| `ringbuffer/dump` | `path`, `format=c64rering\|trxring`, `from`, `to`, `stride` | `ringdump` |
| `ringbuffer/restore` | `path` | `ringload` |

Gate copied from TRX64 G2: every transport action is reachable as an overlay key, a
REST verb and a register command; a table test walks all three (§13).

---

## 12. Register map

**(see ARCH §10.)** The TW block at 0x101A0000 of the first draft becomes pages of the one
TRX IO block at the same base: state bus (page 1), engine with log areas, capture table and
mover (page 2), gate (incl. RESUME_PHASE and capture control), counters and IRQ (page 0). The first draft's commands map as:

| First draft (CMD) | Now |
|---|---|
| PAUSE_AT_SNAP | gate stop at the snap point (page 0) |
| SEEK, STEP, RESUME, REGEN, PIN/UNPIN | software in the timewarp `.u2a` over the engine and the state bus (§8.1 sequence) |
| KEYFRAME_NOW | engine `RAM_OUT` + capture |
| SNAP_TO_STAGING / LOAD_FROM_STAGING | engine `SB_OUT` / `SB_IN` |
| MOVE (mover) | engine `COPY` |
| HEAD/TAIL/CURSOR/HELD/WINDOW/CADENCE/KEYFRAME_EVERY | software state, except `CAPTURE_EVERY` |
| CYCLE, LAST_FRAME_WRITES, DROPPED, IRQ | page 0 counters, page 2 log status |

JTAG: page 0x101 (ARCH §10); register 9 bit 5 is not needed (the stop modes are in page 0).

## 13. Test plan

### 13.1 Simulation first (Verilator, `designs/c64_basic/sim/ring`, like `sim/pause`)

Mac limit: at most two simulations at a time, run with `nice`.

1. **Chain completeness gate** (ARCH §5.5; the hardware version of TRX64's self-enumerating gate):
   a script lists every sequential cell under the C64 and drive hierarchies
   (Vivado `get_cells -hier -filter IS_SEQUENTIAL`, or the Verilator netlist) and fails
   unless each is in the state map or on a reviewed exclusion list (caches, CDC, audio side,
   debug counters) with a reason.
2. **Snap/load identity:** snap at frame k, load the same image back at once; the run
   continues identical to an untouched run (compare RAM, raster, CPU bus trace hash).
3. **Round-trip fidelity**, adapted from
   **[TRX64: crates/trx64-core/tests/snapshot_roundtrip_fidelity.rs]**: scenarios
   (i) BASIC READY, (ii) EasyFlash at a bank ≠ 0 with IO2 RAM written, (iii) drive
   seeking and reading a synthetic disk, (iv) raster IRQ + sprites + CIA timer IRQ
   running, (v) REU transfer in flight. For each: run to frame k, keep running to k+n,
   restore k, run n frames again. Assert (a) byte-identical chip blocks at k+1…k+n,
   (b) identical RAM, colour RAM, drive RAM, cart RAM, (c) identical hash of
   (PC, opcode, A) per instruction and of every C64 bus cycle over the n frames
   (TRX64's N-cycle-identical continuation, `N_CYCLES = 200_000` there; here n = 10
   frames ≈ 197 k cycles). Every differing field is listed, not just "failed".
4. **Undo/redo exactness:** for random k1 < k2 in a 500-frame ring, seek k2 → k1 → k2
   via logs and via keyframes; all memories and blocks equal (808 gate G1).
5. **Keyframe fix-up:** heavy writes during the background copy; the fixed keyframe
   equals RAM at the snap.
6. **Frame lock:** with capture every frame `gclk` is never gated by a capture; after
   restores with different snap cycles the VIC stays where `tb_scaler` expects it
   (RESUME_PHASE), no lost or extra HDMI frame.
6a. **Capture = stop:** a block captured while running equals the state read after stopping
   at the same edge; a forced stop inside the copy gives a torn flag, never a mixed block.
6b. **Torn-block sweep** (deterministic, ARCH §12 A3): a stop at every `clk` offset from the
   capture edge, one run per offset; the count of torn/skipped blocks per offset gives the
   exact window. On the board: torn/skipped counters over a 1-hour monitor session.
7. **Determinism:** replay mode re-runs frame k from snap k with its input log and
   reproduces chip block k+1 and stream A (plain C64 and the icepi cartridge; not with an
   REU (frames with REU activity are flagged "not exact", ARCH §14 item 8) or drive traffic
   until this spec's question 3 is settled).
8. Regression: `sim/pause`, `sim/dma_io`, `tb_scaler`, `tb_mig_arb` still pass.

### 13.2 Board (later, by Alex; no JTAG/board use in the spec work)

1. Arm, play a game 2 min, read LAST_FRAME_WRITES / BYTES_USED: the real per-frame
   cost (fills the "light / TRX64 ratio" guess in §5.2).
2. Rewind 10 s, play: game continues as if nothing happened; repeat with an EasyFlash
   game that saves (TRX64's §7b case: "no longer got past its intro").
3. Lorenz suite run from a restored frame gives the same results as a fresh run.
4. Backward playback at full rate for 30 s: no HDMI drop-out, audio muted, status
   line correct.

### 13.3 Cross-checks with TRX64

1. A `.c64re` from the board opens in TRX64 (`undump`) and runs; TRX64 ignores
   `trx64Hw` (test on the TRX64 side: unknown top-level key accepted).
2. The same file back on the board is exact (blocks equal).
3. A TRX64-written `.c64re` loads on the board (partial: picture and program continue).
4. A `.c64rering` with empty delta/cpu-history rings loads in TRX64 (`ringload`) and its
   anchors scrub (check `DeltaRing::from_dump` / `CpuHistoryRing::from_dump` with zero
   entries before relying on it).

---

## 14. Open questions

1. **LUT budget:** answered by ARCH (§11: ≈ 76 % with T12, T16.2, T20, T21 together; the
   line-0 exclusions are ARCH §5.4).
2. **reDIP SID:** answered (ARCH §5.4): only the DC blockers and CDC halves run on
   snd_clk; the BRAMs are ROM tables; voice state sits in SRL rings that become FFs. The
   audio-only state of voices 1-2 is not restored (ARCH §14 item 3).
3. **Drive determinism:** ARCH keeps the drive on `cpu_clk` (complete halt, quasi-static
   state access). Is a drive that is exact on restore but not bit-identical on re-run
   acceptable, or should the drive move to the C64 clock domain (the first draft's M0)?
4. **Disk insert/swap mid-game:** discard the ring (TRX64 808), or keep it and log the
   swap as an event that a rewind undoes by re-mounting?
5. **TRX64 side:** accept `trx64Hw`; take media once per `.c64rering` instead of per
   entry; importer for `.trxring` (`convert-trxring` in the TRX64 CLI) or only
   `.c64rering`?
6. **App wiring:** the interrupt line is `irq_high(2)` since T28 (ARCH §14 item 1); the ABI is ARCH §9.1's `trx`
   table.
7. **IO address:** answered: 0x101A0000-0x101AFFFF is free (ARCH §10).
8. **Real cartridges** (shadow mode + journal): confirm out of T20 (needs T16).
9. **gzip:** stored blocks first, miniz later?
10. **Window vs memory:** if the busy-game rate is near the TRX64 ratio (~22 s), add
    per-frame write coalescing (2 RAMB36) or give the ring more DDR2?
11. **Rocker:** should Freeze act as ⏸/▶ (F11) when the timewarp overlay is not open?
12. **Audio during play back:** muted (planned), or play the regenerated frame's sound?

---

## 15. Phases

Hardware phases are ARCH §12 (A0-A8) **(see ARCH)**. T20's share:

| M | Content | ARCH phase | Proof |
|---|---|---|---|
| M0 | RESUME_PHASE; capture control and shadows come with the state bus (M3) | A0 | ARCH A0 |
| M1 | write-log taps, engine log drain, LAST_FRAME_WRITES | A3 | sim: logs equal the bus writes; board: real per-frame numbers |
| M2 | keyframes, `APPLY`, `RAM_IN/OUT` | A3 | sim tests 13.1-4/5; RAM-only rewind on the board |
| M3 | state bus CPU, CIA, glue; then VIC, SID, REU, cart, drive; chain map + completeness gate | A1, A2 | sim 13.1-1/2/3/6 |
| M5 | input log + replay mode; REGEN; still capture + pixel layer for the standing frame | A3, A6 | sim 13.1-7; standing shows the frame |
| M6 | `.u2a` timewarp app: transport state machine, overlay, USB F9–F12, REST verbs, `.c64re` dump/load, `.c64rering` / `.trxring` save/load | A8 | 13.2, 13.3 |
| M7 | Later: REU RAM log, GCR write log, write coalescing, T21 PCs in stream A | — | |

Builds stay one at a time on the NAS (2 cores); sim work runs beside them in its own
worktree, as for T10/T02.

---

## 16. App side as built (submodule branch t20-app, 2026-10-02)

The Ultimate app (`software/ultimate-app`, branch `t20-app`) has the ring's software half;
measured on the C64U with main's bitstream (7260428). Code is the reference:
`software/io/c64/trx_ring.cc` (timeline, restore, materialize), `trx_c64re.cc` (files),
`trx_timewarp_ui.cc` (menu), `software/api/route_timewarp.cc` (REST). It is firmware, not a
`.u2a` (§11.5): the state machine of §11.2 is not built, only the pieces below.

### 16.1 API (`trx_ring.h`)

| Call | Does |
|---|---|
| `trx_ring_start/stop/status` | areas = the engine's reset layout (stream 0 16 MB, 1 8 MB, 2 4 MB), `LOG_CTRL` 0x10F, `CAP_CTRL` 1; started by a task 6 s after boot (after the splash's `CAP_CTRL` use) |
| `trx_ring_poll` | every 100 ms: stream 2 into the timeline (≤ 4096 entries; an entry = snap position, cycles, stream 0/1 range, the 58 block words); entries the logs no longer reach are dropped from the front |
| `trx_ring_count/frame/target/find` | entries by index (moves when the front is dropped) or by `seq` (stable) |
| `trx_ring_restore(i)` | §16.2; the timeline is cut after i, the re-run logs on from the counts of now (no `LOG_CTRL` restart: the old records stay, so the cut timeline's undo ranges stay valid) |
| `trx_ring_materialize(_many)` | RAM + colour RAM of entries without restoring: a pause (session reads of colour RAM, second-port reads of RAM, ≈ 0.1 s), then the undo in software |
| `trx_ring_load_state` | a loaded state put in like a restore, then the ring restarts |
| `trx_ring_set_pending / unfreeze` | the menu's choice, done in `C64::unfreeze()` instead of `restore_io()`/`resume()` |

### 16.2 Restore sequence (as tb_ring.cpp, plus what the board needed)

1. Stop on line 0 (`STOP_POS` cycle 12) if running, then the app's stop as machine pause,
   sessions off (`SH_CTRL` 0; in the menu this ends the session, its overlay is dropped).
2. One bus DMA cycle with the log still on: it holds the CPU at its next read; the writes it
   still makes (push sequence, RMW) are logged and undone with the rest. Drain, parse, log off.
3. Ultimax, bus cycles: cartridge banking registers (chip 7: EasyFlash/MegaByter `$DE00/$DE02`,
   Ocean/Magic Desk `$DE00`; not GMod2, C64MegaCart, Dinamic, C64GS, AR, Simons), colour RAM
   undo, cartridge RAM undo (DDR2 0xEF0000 + address, then page 4 `CMD` b1), VIC registers
   ($D012-$D014, $D019, $D01E/$D01F skipped; $D011 only when bits 6-0 differ, so the
   compare's bit 8 stays), the CIA subset of §15.9 ARCH.
4. CPU-only pause, `STOP_POS` = arm | release | x/8. During the run (the CPU is held, DMA
   cycles cost nothing) the VIC's IRQ latches ($D019) and collision latches the block had
   clear are cleared by DMA until line 300 — without it the restored program took one raster
   IRQ too many. No DMA after line 300: one that arrives at the stop opens the clock again.
5. At the position: C64 RAM through the second port, staged words, `SB_LOAD` 0x1A, log on
   (`LOG_CTRL` 0x00F), `CAP_CTRL` 1, release.

Undo is coalesced: the records from the target to now are walked newest first with stores
only (old byte + a mark per address; the rvlite core loads DDR2 uncached at ≈ 1 µs, its stores
are posted), then every marked byte is written once (≈ 530 bytes for 5 s of a busy loop).

### 16.3 Measured (C64U, 2026-10-02)

| | |
|---|---|
| writes per frame | BASIC READY 2,846 (KERNAL key loop); test loop 2,000 RAM + 490 colour |
| window | stream 0 = 4 M records: 28-40 s at those rates (status `seconds`) |
| rewind latency (stop to release) | 1 s back 0.20 s, 2 s 0.36 s, 4 s 0.65 s, 5.7 s 0.95 s (≈ 1.2 µs per record); from the menu 1 s 0.26 s |
| exactness | rewind, then frame +50/+60 against the original: RAM, colour RAM, all 57 block words equal (BASIC loop with KERNAL IRQ; raster IRQ + moving sprite + colour writes); `.c64re` load → frame +50 equal too |
| `.c64re` | 97 KB (stored gzip), dump 1.3 s; loads in TRX64 (`convert-c64re`, `sandbox --seed` runs it on) |
| `.c64rering` | 10 s, stride 25: 21 entries, 2.03 MB, 11.5 s (materialize 2.0 s, JSON 7.5 s, file 2.0 s) |
| `.c64re` load | 1.1 s, exact |

### 16.4 Files

`.c64re` and `.c64rering` as §9/§10 with: CIA nodes `v: 2` (TRX64 predicts its own alarms),
colour RAM also in the RAM blob at $D800 (TRX64's VIC reads it there), `cartBytes` null, no
SID/drive/REU nodes, `cpu.pc` = chip 1 w1's PC (measured: at the snap edge it is the address
of the opcode being fetched, e.g. $E5CD/CF/D1/D4 in the KERNAL key loop). `trx64Hw` =
`{layout 1, x, line, marker, flags, block (58 words LE), ramUnderIo, colorRam}`; load accepts
only files with it and stored blocks (a TRX64-written, deflated file is refused).

### 16.5 REST (as built; §11.5's verbs are not all there)

`GET /v1/timewarp:status`, `GET timewarp:frame?frames|index|seq`, `PUT timewarp:start|stop`,
`PUT timewarp:rewind?frames|ms|index|seq` (answers latency, records, `next_seq`,
`vic_latches_exact`), `PUT timewarp:dump?path&frames|index|seq`,
`PUT timewarp:ringdump?path&seconds&stride`, `PUT timewarp:load?path`. All refuse while the
menu has the machine (409).

### 16.6 Limits and findings

- **Drain F4 (fixed, branch t20-f4-fix, 2026-10-03):** until then `trx_engine.v` D_POP set
  `d_f4` for any stream 2 record whose top byte is $F3 — also a block record whose odd word's
  high byte is $F3 (the 6510's S = $F3 at BASIC READY): the drain wrote a spurious F4 after it.
  *As built:* the record FIFO is 512 × 67 bits (still one block RAM, 512 × 72); bit 66 is set
  only when the marker former writes its F3 record (`m_take` with `m_n` = 2), every other
  write puts 0 there, and D_POP arms the F4 from that bit alone. The F4 therefore follows
  exactly the F3 marker; a block's records go through untouched. `CAPS2` (0x101A0005, `trx_io.v`)
  b0 = 1 says so; older bitstreams read 0 there.
  *App:* `trx_ring.cc` skips an F4 after a block record with $F3 on top only when `CAPS2` b0
  is 0. With the fix the skip would be wrong, not harmless: a block record whose top byte is
  $F4 right after one with $F3 (e.g. sprite 3 Y = $F3, sprite 7 Y = $F4: the tops of block
  records 2 and 3 are $D007 and $D00F) would be dropped and the frame would lose its block.
  *Bench:* `designs/c64_basic/sim/ring/run_f4.sh` (`tb_eng_f4.v`, iverilog): every state-bus
  word with $F3 on top, four frames with a block: F1 F2 F3 F4, B1, 29 word records, B2 each,
  F4 counts = the writes before each marker. PASS; the old engine fails it (116 extra F4s).
- **Speed:** no `APPLY` in the engine, so undo is software (≈ 1.2 µs per record). An engine
  `APPLY` (records → second port at one per clock) would make 5 s ≈ 25 ms and allow §11.3's
  per-frame scrub.
- **Snap condition:** a game's title screen (Jupiter Fracture, EasyFlash) never snaps (all
  frames flag 0, boundary at line 16): no restore target for those frames; a rewind lands on
  the last frame with a block before them.
- **VIC:** compare latch cleared by DMA only up to line 300 (a compare on 300-311 or 0 still
  fires once; reported). Internals and $D012 are not restored: the current compare stays.
- **Not restored:** SID, REU (registers and RAM), drive A, cartridge flash/EEPROM writes, the
  cartridge types listed in 16.2 step 3, the input log (none in hardware).
- **Keyframes:** none. Without `RAM_OUT`/`APPLY` they would only add a 64 KB DMA read; the
  reach is bounded by stream 0's area anyway.
- **UI:** superseded by §16.7: the menu entry opens a scrub that shows the picture of the
  frame it stands on; the final rewind still happens when the menu closes.

### 16.7 Timewarp scrub and bar (submodule branch t20-twbar, 2026-10-03)

Alex, 2026-10-03: "das Menu viel minimalistischer beim Timewarp, unten über den Border. Das
normale U64-Menü ausblenden, den C64-Screen bis auf dieses Mini-Overlay unten normal zeigen."
And: the picture follows the scrub position ("das ist doch der Witz am Scrubbing").

**Screen.** While Timewarp is open the menu box is hidden and the C64 picture is shown at
full brightness, with one bar over the lower border; nothing else is drawn.

- *Box hidden:* `TX_CTRL` b0 = 0 (the C64 text layer's enable, written by the app). T12 §11
  Q3 (C, the explicit show bit replacing the show latch) is not built yet: today the layer
  shows when enable *and* the session's show latch are up; enable 0 hides it either way, and
  from the first scrub step on there is no freeze session at all (below).
- *Picture:* the pixel layer (800 × 540 RGB565, scale 2, output x 160, every pixel opaque, no
  colour key) holds the still with the mixing weight t = 4/4 (no dimming).
- *Bar:* drawn into the same buffer over the lower border (VIC window lines 235-269 =
  raster 251-285 are border). Background: the still under it mixed 1/4 still, 3/4 bar colour
  (#101018), per palette colour, so it reads as a semi-transparent strip.

| element | layer px (x, y, w × h) | 1080p output px | VIC window / raster |
|---|---|---|---|
| bar | 16, 476, 768 × 48 | 192, 952, 1536 × 96 | y 238-261 = raster 254-277 |
| transport glyph (8 × 8 at 2×) | 28, 484, 16 × 16 | 216, 968, 32 × 32 | |
| position, large ("-10.4 s", char ROM at 2×) | 52, 484, 16 px per char | 264, 968 | |
| info, small ("frame 1234 / 1500", 1×) | 188, 488, 8 px per char | 536, 976 | |
| key hint, small, right-aligned ("RETURN jump  STOP cancel") | ends at 772, y 488 | ends at 1704 | |
| track: the ring's span, oldest left; cursor 4 × 10 | 28-771, 508 × 4; cursor y 505 | 216-1702, 1016 | |

Output px = 160 + 2 × layer x, 2 × layer y. Font: the Ultimate's own char ROM
(`default_chars.bin`, lower-case set). Colours: text white, info light grey, info light red
when no frame there can be restored, glyph light blue, track dark grey with the part before
the cursor grey.

**Glyph** (the transport state): ⏸ at open (nothing scrubbed), ◀◀ after a step back, ▶▶
after a step forward, |◀ on the oldest frame, ▶| on the newest.

**Keys** (after Gideon's keymapper; held keys repeat): CRSR ←/→ one frame, ↑/↓ one second,
F1/F7 (PageUp/PageDown) ten seconds, CLR/HOME the oldest frame, RETURN jump there and run,
RUN/STOP cancel. RETURN, RUN/STOP and the menu button all close the menu: the machine
runs, the menu does not come back. Without the pixel layer (no HDMI) the old window over
the menu stays (numbers only, no scrub), with the same ways out.

**Scrub (the picture follows the cursor).** A step moves the machine, paused, from the frame
it shows (D) to the cursor's frame (T) and lets it draw that frame:

1. First step only: the freeze session ends (`SH_CTRL` 0, machine pause kept), one DMA cycle
   with the log on, drain, stream 2 parsed; the newest entry's ranges are closed at the counts
   of now; capture off (`CAP_CTRL` 0); the CIA 1 port directions are set for the keyboard scan
   (`$DC02` $FF, `$DC03` 0: without a session the scan drives the real CIA; every step's and
   the final load replays the block's directions).
2. Log off; the records since the end of the last step (the previous frame run, the menu's
   own writes in between) are undone, newest first (old bytes).
3. D → T: back = the entries [T, D) undone (old bytes, newest first); forward = the entries
   [D, T) redone (new bytes, oldest first): the logged values, the program is never re-run
   to go forward, so RAM, colour RAM and cartridge RAM are the original's.
4. T's chip block put in as a restore (§16.2 steps 3-5: banking, colour RAM, VIC, CIA replay,
   run to the snap position with the CPU held, RAM through the second port, staged words).
5. Log on (no restart, capture off), release, run **one frame**, stop on line 0 cycle 4 of the
   next frame (`STOP_POS`), machine pause, one DMA cycle; the still capture now holds the
   frame drawn from T. The app copies it into the layer and redraws the bar.

So the **picture shown is frame T plus one frame of running** (the frame the VIC draws from
T's snap on, with the inputs of now, not the logged ones; there is no input log). The
machine stands one frame after T with that frame's writes in the log; step 2 of the next
step or the final rewind takes them back, so D = T. The stream 2 markers of the frame runs
are skipped (`p2` set to the count), they never become timeline entries.

Keys pressed during a step are read when it is done; every key read moves the cursor and
only then one step goes to the newest cursor (intermediate targets are dropped).

**Leaving.** RETURN: the final rewind to T from the scrub's state (steps 2-4, then the
timeline is cut after T and the log and capture run on as in §16.2) when the menu closes
(`C64::unfreeze()`). RUN/STOP without a step: the ordinary unfreeze, the machine goes on
from the freeze point. RUN/STOP after a step: the final rewind goes to the newest frame with
a chip block, at most one frame (20 ms) before the freeze point: the frame runs changed
the CPU and chips, and the freeze point itself has no chip block (it is anywhere in a frame;
`STOP_POS` stops on line 0 only). Any other unfreeze while a scrub is open takes the same
path.

**Cost (estimate, to be measured).** A step = 2 × (records of one frame) × 1.2 µs + one
frame (20 ms) + the run to the snap position (a few cycles from line 0 cycle 4; up to a
frame on the first step): one-frame steps ≈ 25-30 ms, one-second steps ≈ 0.2-0.25 s. Then
the picture: the still copied (54 KB), the layer's back buffer redrawn and flipped at the
next HDMI frame start, ≈ 10-30 ms more. Each step prints `[RING] scrub to seq …` with its
records and milliseconds.

The keyboard: on the U64 builds the C64 keyboard is scanned inside `getch()` in the user
interface task (`KEYBOARD_C64_TIMER_SCAN` 0), the task that runs the steps, so no scan
falls into a step.

**Limits.**
- Every step logs one frame into the ring's areas: the reach shrinks by one frame per step.
  While a scrub is open a target needs `SCRUB_MARGIN` (32 K records) more headroom than
  `SAFETY`, so the frame the machine stands on stays readable after the next run.
- Not in the picture or restored: SID (muted in the menu), REU, drive A, cartridge
  flash/EEPROM, the cartridge types of §16.2 step 3; frames without a snap are not targets
  (the cursor shows the nearest older frame with a block).
- The one-frame run uses live inputs; a program that reads the keyboard or joysticks in that
  frame can draw it differently from the original.
- VIC: as §16.6 (compare latch on lines 300-311/0, internals).

**Not built yet:** a single-frame pause/play transport (§11.2), marks, the F3/F5 keys of
§11.4. An engine `APPLY` (§16.6) would cut the record walk to ≈ 25 ms per 5 s.

---

### 16.8 Changes made for T21 (app branch t21-app2, 2026-10-03)

The debugger's use of the ring (T21-monitor.md §4.10.1) changed `trx_ring.cc`:

- **Land and commit** (`trx_ring_land`, `trx_ring_commit`): the scrub's walk and put-back
  with the machine left paused on the entry's snap edge (CYC_CMP armed for the load edge),
  the timeline kept; a free run commits (cut after the entry, the writes since the land are
  its frame). Reached through the app ABI's `trx->ring` table.
- **`c24`** in `TrxFrame`: F2's count, which equals the debug block's CYCLES (both count the
  same phi2 edges since configuration). Restores, scrub rewinds and commits set the entry's
  `c24` to its load edge's count and continue the cycle sum from it, so consecutive entries
  stay one frame apart.
- **No markers into the timeline while a land or scrub holds the machine** (`parse_locked`
  returns after the counts): a status call between scrub steps or debugger steps had added
  entries of a run that is not the timeline, and with a full timeline moved every index by
  one per frame.
- **Run control:** a restore, a scrub's final rewind and a load first hand run control from
  the debugger (OWNER) to C64_STOP; before, they ended with "did not reach the frame's snap
  position" while trxmon owned the machine.
- **A land ends in a session (2026-10-05, T21 §8.9):** `land_locked` writes the `SH_CTRL` mode of the stop it started
  from back when the machine stands on the load edge (`stop_machine` had zeroed it; only commit restored it), and
  zeroes it for a land from a land.
- `trx_c64re_load` also takes the current entry of a `.c64rering` written by this board, and
  checkpoints carry TRX64 0.12.2's `turbo` node in the registers-mode reset state (§10.1).

## 17. M1 completion (branches t20-m1, t20-m1-app, 2026-10-03)

What Alex asked to close T20's M1: the two board bugs, APPLY in hardware, save the ring from
the menu, the turbo node. Each part is specified here before its code; "as built" notes are
added under the part when it exists.

### 17.1 Frame rate (bug b)

`trx_timewarp_ui.cc` fixed 50 frames per second. Since T40 the frame rate is the System Mode's:
PAL and NTSC-50 run 63 x 312 = 19656 cycles/frame at the PAL clock (50.125 Hz), NTSC and PAL-60
run 65 x 263 = 17095 at the NTSC clock (59.83 Hz). Rule: whatever depends on the frame rate asks
`trx_ring_fps()` / `trx_ring_fps_x100()` / `trx_ring_frame_cycles()` (`trx_ring.h`), which read
`C64_VIDEOFORMAT` b1 (60 Hz mode, the bit `monitor_poll_hz` uses) on every call, so a live mode
switch is followed. Users: the 1 s / 10 s key steps and the time labels (`trx_timewarp_ui.cc`),
the span of a `.c64rering` save and the frame numbers in its ids (`trx_c64re.cc`), the status
`seconds_x100` and `rewind?ms=` (`route_timewarp.cc`). Not changed (owned by T21 on t21-lens):
`apps/trxmon` still reports PAL only (`rpc.c` `cyclesPerFrame` 19656, `ring.c` `CYC_FRAME`).

*As built (app commit d206ced2):* board check 2026-10-04: with the ring at 2005 frames `seconds_x100` reads 4000
in PAL and 3351 after `System Mode` NTSC (live, no restart), 4000 again after PAL.

### 17.2 Hang after scrubbing (bug a): locating tool

The RISC-V has no debug port. `rv_dbg.v` (cpu_clk) taps it: the PC of the instruction the
execute stage handed on last (`exec_o.program_counter`, exported by Gideon patch 0005 as
`dbg_pc`, a register), whether the CPU waits on an I/O access (`io_busy`) or a memory request,
the address of either, the IRQ line, how many clocks the PC has stood still (running and the
longest since reset), IRQ rising edges. JTAG `0x100040` (write: snapshot, `0x100041`: clear the
longest-stall record), words `0x100040-0x100047` (layout in `rv_dbg.v`);
`tools/trxdbg.sh rvpc [clear] [app.elf]` takes two snapshots 0.25 s apart and names the
functions from the ELF. A hung CPU is a PC that did not move between the two snapshots and a run
that grew; "I/O busy" + address says which register never answered.

*As built:* bitstream run 327 (`rv_dbg.v`, patch 0005, bench `sim/rvdbg/run.sh`). Board 2026-10-04: the
tool names the app's functions (`prvIdleTask`, `UsbBase::complete_command`, `put_back`) and a longest
stall of 436 clocks (4 us) in a normal run with Timewarp scrubs; `rvpc clear` first, since the CPU's
reset hold before the app is loaded counts as a 26 s stall.

*The hang was not reproduced.* Pattern of the report on the same hardware (tap bitstream, current
app, Wasteland EF from `testdata/` over `runners:run_crt`, ring full at 4095 frames, stream 0 wrapped):
about 200 scrub steps through the menu with the C64 keyboard injected over `POST machine:input`
(`menu_button`, F5, C64 Machine, Timewarp; CRSR up 1 s, F1 10 s, F7, HOME, RETURN, RUN/STOP, also
bursts of 3-6 keys so key repeat lands inside a step), 16 open/scrub/close cycles alternating RETURN
and RUN/STOP, repeated jumps to the oldest frame the scrub accepts, and the same through the new REST
`timewarp:scrub` / `scrubend` (a scrub without the menu, kept for tests). Every step answered; no
`[RING]` error, no stall above 6418 clocks (64 us, `put_back`'s DMA). What the code review found and
what is now guarded:

- *Unbounded waits in the app.* `eng_copy` polled `ENG_CMD` b0 for ever: a mover job that never
  finished (DDR2 read data that does not come) is a spin with no output, no GURU, as reported.
  It is now bounded (200 ms, prints `[RING] COPY did not finish`). Every other wait in `trx_ring.cc`
  was already bounded (`wait_reg`, the still-capture wait). Note the UI task runs at priority 0 and
  lwIP at 2, so a spinning UI task alone does not kill ping; that needs the CPU stalled on the bus or
  held in an interrupt.
- *An app DMA byte the C64 side never answers* holds the CPU on that access for ever (`io_busy`, no
  ack): `ultimate_sys.vhd` now gives up after 2^23 clocks (84 ms), acks `$FF` and counts it
  (`rvpc`: "app DMA bytes the C64 side never answered").
- *Freeze on a jammed 6510.* Poked a KIL opcode into Wasteland's interrupt entry: `machine:menu_button`
  then blocks the REST worker in `C64::freeze()` -> `stop(true)` (15 s curl timeout) and no menu comes
  up; the rest of the app keeps answering. A jammed core has no snap, so the ring holds no whole chip
  block from there and a scrub answers "the frame has no whole chip block". Not the reported hang (REST
  stayed alive here) but a way into a state where the menu is dead; a restore that sends the game
  into a KIL is the likely trigger of such a state.

If it happens again: `tools/trxdbg.sh rvpc app.elf` twice names the PC, what the CPU waits for and the
stall length; the console shows `[RING] COPY did not finish` when the mover is the cause.

### 17.3 APPLY in the engine (undo/redo of stream 0 by hardware)

Today every rewind or scrub step walks the stream 0 records in software: one uncached DDR2 load
per record (about 1.2 us, §16.6), so 5 s of a busy loop take about 1 s and a scrub step of one
second 0.2 s. The engine applies them itself: it reads the records from DDR2 and writes the old
byte (undo) or the new byte (redo) through the RAM's second port (c64_mem port B, one byte per
sys_clk, 31.5 MHz), the same port the app's DMA writes use.

**Semantics.** `APPLY(start, count, dir)`: `count` records of stream 0 beginning at record
index `start` (the 28-bit count of LOG_CNT, the record's place is `LOG_BASE | ((n*4) & LOG_MASK)`
as the drain computes it), in index order upwards (`dir` = redo: `RAM[addr] = new`) or downwards
(`dir` = undo: `RAM[addr] = old`; `start` is the newest record to take back). Applied one after the
other, so the last write to an address wins exactly as the software walk's coalescing leaves it.
Record = `{new 8, old 8, addr 16}` little endian, lane `n & 3` of the 16-byte word `n >> 2`.
Applies nothing else: colour RAM and cartridge RAM (stream 1) stay with software (their records
are an order of magnitude fewer: 490 colour writes per frame in the test loop). The writes are not
logged (the tap `wb_*` is masked while the engine owns the port). The port is taken only in clocks
nothing else (freeze responder, text layer, JTAG, app DMA byte) uses; the app does not touch it
while a job runs.

**Contiguity.** Consecutive timeline entries are contiguous in the stream (`e0` of an entry is
`s0` of the next) except after a restore or cut, which restarts the cut entry at the current
counts. The app therefore merges adjacent entry ranges into runs and issues one job per run: a
rewind of N frames is one or two jobs, a scrub step is the run of the last frame run (undo) plus
one run D -> T (undo if T is older, redo if newer).

**Registers** (page 2, trx_sys word accesses as the other page 2 registers):

| offset | name | |
|---|---|---|
| 0x094 | AP_START | W: record index (28 bit) of the first record in application order |
| 0x098 | AP_COUNT | W: records (28 bit); 0 = nothing to do |
| 0x09C | AP_CMD | W: b0 0 redo / 1 undo, starts the job (ignored while busy). R: b0 busy |

`CAPS2` b5: the engine has APPLY (b4 is the lens unit, T21 §3.4.1). The completion also raises
the engine-job interrupt cause (`eng_tgl`, same as COPY/FILL).

**Hardware.** ui_clk: the job reads the 16-byte words along the direction with pipelined engine
reads (mig_arb keeps them in order, `e_rd_data_valid`), at most as many in flight as the FIFO can
take: `inflight + level <= 24` of a 32-word async FIFO (`trx_afifo`, W=128, new `wlevel` output =
words written minus the read pointer as the write side sees it, never too low). sys_clk: pops a
word, applies its lanes in direction order, the first word from lane `start & 3`, the job's
last lane where `count` runs out, one record per clock when the port is free; clears busy when
`count` records are applied. A word costs two clocks over its four records: about 21 M records/s,
against 0.83 M/s in software.

**Simulation** (`designs/c64_basic/sim/apply/`, iverilog): a DDR2 model behind `e_*`, a RAM model
behind the port; test: random write log (RAM plus a reference model), undo of any range newest
first and redo oldest first restore exactly the model's states, ranges starting/ending on every
lane, wrap of the circular area, count 0, count 1, a busy port (random grants) and back-to-back
jobs.

**App.** `trx_ring.cc` replaces the stream 0 half of `collect`/`walk_range` when `CAPS2` b5 is
set: `apply_range(start, count, redo)` writes the registers and polls AP_CMD b0 with a timeout (60
ms + the count's share; on timeout the step fails with E_NOPOS-like "engine did not finish",
never loops). Without the bit the software walk stays (older bitstreams). The restore, the scrub
walk (`scrub_walk_locked`) and the land use it; the page marks for the software walk are only
filled for stream 1.

*As built:* `trx_engine.v` (APPLY: ui_clk reads, 32-word async FIFO with `wlevel`, sys_clk lane
sequencer), `top.v` (port B arbitration: responder, text layer, JTAG, APPLY; APPLY's byte is masked
out of the write-log tap), `trx_io.v` CAPS2 b5. Bench `sim/apply/run.sh` (iverilog, +quick for a
short run): 240 range tests over two logs (the second wraps the 64 KB test area), every start and end
lane, count 0, the whole log, random DDR2 latency and ready, random port grants: PASS; 0.5 records
per sys clock with grants at 75 % (about 17 M records/s), at most 8 reads in flight.

*Board (run 331, bitstream t20-m1-dfd95b68, app t20-m1-app, 2026-10-04), BASIC machine-code loop with
3,900 RAM writes per frame:* `tests/t20_apply.py` runs pairs of scrub steps, one through the engine and
one the CPU's way from another frame (so undo and redo both run on each side), and compares RAM over
JTAG: 8 pairs plus a jump to the oldest frame, the pages the program writes ($0000-$07FF, $C000-$C0FF)
and once all 64 KB: RAM equal in every pair, PASS. Time, one scrub step (it includes the frame run of
about 70 ms): 1.55 M records 133 ms engine against 2,014 ms CPU, 3.24 M records 236 ms against 4,192 ms
(1.2 us a record on the CPU, 51-64 ns on the engine: about 20x on the walk). A rewind (`timewarp:rewind`)
100 frames back, 391 K records: 70 ms (457 ms before), 400 frames, 1.56 M records: 156 ms (1,644 ms).
`ram_writes` now counts records (no coalescing); `timewarp:scrub?apply=0|1` switches between the two
ways for the test. The app falls back to the CPU walk on a bitstream without CAPS2 b5.

*CI run 331 (all of §17 in hardware):* c64_basic WNS +0.064 ns (+0.036 with the tap alone), Slice LUTs
27,394 (84.0 %) against 26,511 on main at 541879bf: +586 for `rv_dbg.v` (its 256-bit snapshot and the
synchronizer in front of JTAG are most of the +1,538 flip-flops), +297 for APPLY and the DMA watchdog;
Block RAM 62 (+2, the 32 x 128 bit FIFO); registers 29,735 (+1,754). The tap can shrink if LUTs are
needed: the second synchronizer stage and words 5-7 are the cheap cuts.

### 17.4 Save the ring from the menu

> REMOVED FROM M1 (A13, Alex 2026-10-06): the menu entry, `save_ring_action` and `trx_c64rering_dump_ex` are deleted; the text below describes what was built and removed.

The file format exists (§10.1, `trx_c64rering_dump`, REST `timewarp:ringdump`, measured §16.3). What
is missing is a way to start it without REST. Entry **"Save Timewarp Ring"** in the freeze menu's
"C64 Machine" tasks next to "Timewarp" (`trx_timewarp_ui.cc`, the same availability: the engine,
CAPS b2):

1. Refuses, with a popup, when the ring holds fewer than two frames.
2. Where (§10.3): `<volume>/timewarp/` on the first of `/Usb0`, `/Usb1`, `/SD` that is mounted and
   writable (the directory is created), name `ring-<frame>-<yyyymmdd-hhmmss>.c64rering` with the
   newest frame's number and the board's clock (uptime in seconds when no RTC time is set).
3. What: the last 10 s, every 25th frame (§10.1's default: 20 entries; stride and span are frame-rate
   aware, §17.1).
4. The write takes about 11 s (measured §16.3: materialize 2.0 s, JSON 7.5 s, file 2.0 s): the menu shows
   a progress line (`show_progress`/`update_progress`, entries done of total) and the end result in a
   popup: bytes, entries and the path, or what failed. The dump runs inside the freeze menu, the machine
   frozen (`materialize_locked` already takes a halted machine as it is).
5. `trx_c64rering_dump_ex(..., progress, ctx)` gains a progress callback; `trx_c64rering_dump` stays.

No keys in the Timewarp bar for it (the bar's keys are §16.7; the menu entry is one level up and works
before the bar is opened).

*As built (app commit "Save Timewarp Ring"):* `trx_timewarp_ui.cc` `save_ring_action`, progress box via
`UserInterface::show_progress`. Board 2026-10-04 (menu opened with `menu_button`, F5, C64 Machine, second
entry, all by `POST machine:input`): 21 frames, 1,972 KB, materialize 1,027 ms, JSON and file 10,659 ms,
written to `/Usb1/timewarp/`; the result popup shows the count, size and path. The file parses as a
`.c64rering` (container, `checkpoint_ring`, entries with `ram`, `cia1/2`, `vic`, `trx64Hw`, `turbo`).
The clock is not set on the JTAG-loaded app (`rtc` reads 2003), so the name falls back to the uptime
in seconds: `ring-<frame>-t<seconds>.c64rering`. Not tried here: TRX64 opening the file (the format is
the one measured in §16.3).

### 17.5 Turbo node (§10.1)

State of the node today (`trx_c64re.cc`, written for `.c64re` and every `.c64rering` entry by the one
checkpoint writer): the registers-mode reset state, which is exactly right for this core: it has no
turbo (T29 is not built), so the machine always runs at 1 MHz with badline stalls and `$D030`/`$D031`
at their reset values. Added by this task:

- `turbo_node()` is the one place that builds it: from a `TrxFrame`'s block words when the bitstream
  provides the state (T29), else the reset state. The writer calls only it.
- Loading a file: the node is read; a file whose node asks for more than 1 MHz (`menuIndex` > 0 with
  `resetHold` 0, or `d031` with a speed index set) loads, and the app prints once that the speed
  state is not applied because this bitstream has no turbo; a file without the node loads as before.

**What T29 must fill in** (state in T29-turbo.md §5: `regs_en[2:0]`, `prefer[7:0]`, `d030_b0`, `d031[7:0]`,
`d031_wr`, `scpu_slow`, `hold[20:0]`):

| node field | from | note |
|---|---|---|
| `enable` | `regs_en[2:0]` (b0 `$D031` mode, b2 `$D030` mode) | |
| `menuIndex` | `prefer[3:0]` | the app's speed setting |
| `menuStalls` | `prefer[7]` (the menu's "Badline Timing Enabled": 1 = stalls on) | the menu's own sense; `$D031` b7 has the opposite one (T29 §5.1: `bl_on = src_d031 ? !d031[7] : prefer[7]`) |
| `d031` | what `$D031` reads: `d031[7:0]` (`$00` while no program wrote it in `$D030` mode) | TRX64 restore masks `$8F` |
| `d030` | `{6'h3F, 1, d030_b0}` | as the register reads |
| `resetHold` | `hold[20:0]` as PHI2 cycles left (2^22 after reset) | so a rewind into a boot keeps the remaining hold |

Those registers go onto the state bus as one more word of the glue chip (chip 0), added to the capture table
so every frame's block has it, and to the staged words so a restore puts it back (a rewind across a
`$D031` write must restore the speed, and the hold counter). `turbo_node()` then reads that word; the layout
number of `trx64Hw` goes up so an older file without the word still loads (reset state). The ring runs
at 1 MHz only (T29 §6.1, Q7): a frame in turbo is not in the ring, so the node of a ring frame always
describes the state at the snap, whatever the speed is later.

*As built:* `write_turbo()` in `trx_c64re.cc` is the only writer (the `.c64re` and every `.c64rering`
entry call it); it writes the reset state, and a comment says where T29's words go. `trx_c64re_load`
reads the node and prints a note when a file asks for a speed this core cannot run. Verified on the
board: a `.c64rering` entry carries `{"enable":1,"menuIndex":0,"menuStalls":true,"d031":0,"d030":0,
"resetHold":0}`. What is not done is T29's part: the state-bus word and `turbo_node()` reading it.

## Appendix: what is copied from TRX64

| Here | From |
|---|---|
| `.c64re` container, manifest, `$ta` encoding | `crates/trx64-core/src/native_snapshot.rs` |
| checkpoint node shapes, boundary flag, tolerant restore, expansion "RAM uncovered" | `crates/trx64-core/src/c64re_snapshot.rs` |
| drive blob modules (DRIVE8, DRIVECPU0 1.4, VIA1D0, VIA2D0, GCRIMAGE0 3.2) | `crates/trx64-core/src/drive_snapshot.rs` |
| `.c64rering` container and dump structs | `crates/trx64-core/src/ring_dump.rs`, `checkpoint_ring.rs`, `delta_ring.rs`, `cpu_history.rs` |
| write record `{addr, old, new}`, undo contract | `crates/trx64-core/src/delta_ring.rs` |
| window/ceiling policy, 60 s default, marks (32, pinned), truncate keep-pinned, ids `cp_<frame>_<seq>` | `crates/trx64-core/src/checkpoint_ring.rs`, `MONITOR.md` |
| transport state machine, F11 decision, play = run from here, mode line, owner-of-state rule, gates G1/G2, §7b side effects | `docs/808-rewind-transport.md`, `crates/trx64-daemon/src/transport.rs` |
| ring dump / restore pieces, tester hand-off | `docs/spec-time-travel-tooling.md` |
| fidelity classes, coarse-VIC cut, synthesis-state vs PCM rule, PC stream as checksum | `docs/concepts-snapshots-scenarios-overlays.md` |
| reset/power/cart change discard the ring | `docs/spec-power-lifecycle.md`, 808 §4a |
| scenario export of inputs | `crates/trx64-core/src/scenario_player.rs` |
| round-trip fidelity test | `crates/trx64-core/tests/snapshot_roundtrip_fidelity.rs` |
| REST verb names | `crates/trx64-daemon/src/main.rs` (`transport/*`, `checkpoint/*`), `MONITOR.md` |


## Decision: REU means no Timewarp (Alex, 2026-10-04, repeated since 2026-10-02, confirmed again 2026-10-07: A08)

While the REU is enabled there is no ring and no Timewarp: the ring is off and the
Timewarp/scrub/rewind actions answer with a clear message. REU registers and REU memory are
never restored by the ring. This replaces every "REU in the ring" item above. Reason: the ring
does not record the REU's contents, so a rewind with an REU program restores RAM and CPU but
not the 16 MB REU, and the program may go on wrongly.

### A08 as built (app branch a08-reu-timewarp-app; app only, no hardware change)

The REU is "effectively enabled" when `C64_REU_ENABLE` (cartridge registers +8) reads 1. That
is the register the machine acts on, whoever set it: the REU setting (`set_emulation_flags`),
a cartridge's require/prohibit (`CART_REU`, `CART_MAXREU`, the MOD player cart, `init_cartridge`),
T60's CRT-in-the-shared-block rule (`reu_memory_usable`, forces it off). `.reu` load and the
preload only fill the REU's memory and do not touch the register. No other code writes the
register (grep of the app, 2026-10-07), so there is one hook set and one central function:

- `trx_ring_reu_sync()` (software/io/c64/trx_ring.cc) makes the ring follow the register. The
  decision itself is `trx_reu_gate_step()` in `trx_ring_reu.h` (no hardware in it, host test).
  REU on: log off (LOG_CTRL 0), timeline and trace epochs dropped, pending menu choice cleared.
  REU off again: `start_locked()`, the ring starts over from that moment, no frame from
  before. It restarts only when it ran (or was due to start at boot) when the REU came on: a
  ring the user had stopped with `timewarp:stop` stays stopped. While a scrub or a debugger
  land holds the machine the ring is left alone until it ends (the poll looks again).
- It runs at the end of `C64::effectuate_settings`, `C64::set_emulation_flags` and
  `C64::init_cartridge` (after the cartridge's require/prohibit has settled the register), and
  in every `trx_ring_poll()` (100 ms), which also catches a change made behind those. At boot
  the ring task's first start (6 s) does not start while the REU is on; it is due when the REU
  goes off. It never waits for the ring's mutex; a busy ring is looked at again by the poll.
- Every way into the ring checks the register itself, not the ring's state, so no poll
  latency can let one through: `trx_ring_start`, `trx_ring_restore`, `trx_ring_land`,
  `trx_ring_scrub_show` refuse (E_REU) with **"Timewarp is off while the REU is enabled (A08)"**.
  Finishing a scrub that was already open (`trx_ring_unfreeze`, `timewarp:scrubend`) is not
  refused: the machine would stay paused.
- Menu: the freeze menu's "Timewarp" task shows that text as a popup (OK) and does not open.
- REST: `timewarp:start|frame|rewind|scrub` answer 409 with that text; `scrubend` answers 409
  with it unless a scrub is open; `timewarp:status` answers 200 with `on: false` and
  `reason` (the same text) while the REU is on. `doc/api/rest_api_openapi_u64.yaml` lists
  the 409s.
- trxmon: the ring status the firmware gives apps carries `reu_off` (the former reserved
  byte of `u2p_ring_status_t`, 0 from older firmware). trxmon answers every ring RPC
  (`checkpoint/*`, `transport/*`, `mark/*`, `runtime/reverse_step`) with error -32001 and the
  text, and the monitor verbs (`rstep`, `frame`, `goto`, `mark`, `rewind`) print it. The
  status line shows `ring off(REU)`. `transport/pause` is a ring method and is refused too
  (`debug/pause` pauses). A land through the app table also returns E_REU from the firmware.
- GeoRAM (setting "RAM Expansion Unit" = GeoRAM) does not set `C64_REU_ENABLE`; it is not
  covered by A08 (open, not decided).

## Decision: SID is not restored (Alex, 2026-10-04, said repeatedly)

After a rewind the SID keeps its current registers; the sound may be off until the program
writes the SID again. Accepted, not a bug, nothing to build.

## Decision: drive A is not restored in M1 (Alex, 2026-10-04)

Restoring drive A (CPU, VIAs, head, motor) through the ring is later, not M1. A rewind into a running load may hang or break that load.

## Decision: cartridge banking in the ring, flash content not (Alex, 2026-10-04)

M1: the ring restores the bank selection of every cartridge type (GMod2, C64MegaCart, Dinamic, C64GS, Action/Retro Replay, Simons Basic as well as EasyFlash, Ocean, Magic Desk). The flash/EEPROM content (a save) is not rolled back; that is accepted for M1.

These three decisions (SID, drive A, cart flash content) are deliberate deviations from TRX64, whose checkpoint ring restores everything per anchor (cartState, cartFlash, driveDiskImage, SID; trx64-core/src/checkpoint_ring.rs). Alex, 2026-10-04: "dann ist das erstmal so".

## Decision: VIC internals not restored (Alex, 2026-10-04)

A rewind that lands on raster lines 300-311 or 0 may lose or double one raster IRQ (the VIC's compare latch is not in the ring). Accepted.

## Decision: full .c64re exchange is M2/M3 (Alex, 2026-10-04)

M1 dumps RAM, CPU, VIC, CIA and loads its own dumps. SID/drive/cart nodes in the dump and loading TRX64-written (compressed) files are M2/M3.

## Decision: no play back/forward in M1 (Alex, 2026-10-04)

Timewarp in M1 is rewind and scrub. Playing backward/forward like a video (◀◀, the F11 state machine of §11.2) is later.

## Decision: §11.4 F9-F12 keys obsolete (Alex, 2026-10-04)

Timewarp on the C64U is operated through its own UI (the Timewarp bar); the USB F9-F12 transport keys of §11.4 are dropped.

## Decision: a window of about 30 s is enough (Alex, 2026-10-04)

The measured 28-40 s replace the 60 s goal of G1.

## Decision: no .c64re / .c64rering dump or load in M1 (Alex, 2026-10-04)

M1 has neither: "Save Timewarp Ring", the .c64re dump/load, the REST `timewarp:dump/ringdump/load` routes and the monitor's `dump`/`undump`/`ringdump`/`ringload` file exports are removed (the `tracering` / `trace/build_from_ring` `.c64retrace` export followed on 2026-10-06, same decision, T66) together with their code (Alex: "Den Code darfst Du wegwerfen"); M2/M3 rebuilds them with the full exchange (A09). M1 Timewarp = rewind and scrub on the board.

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

### T20

Delta ring, rewind/scrub (Timewarp)

Milestone M1, status ✅. Open (one line, as in the TODO table): Done 2026-10-09 ($01F2 fix and `snap` bench passed on the board 2026-10-08, in main); UI extras -> T54 (M2)

Decisions: [ADR-0008 Timewarp is off while the REU is enabled (A08)](../adr/0008-timewarp-off-while-reu-on.md); [ADR-0030 No .c64re / ring dump or load and no trace export in M1 (A13)](../adr/0030-timewarp-dump-and-trace-export-out-of-m1.md); [ADR-0024 SLIM: one state path, a lean write log (replaces the freeze session)](../adr/0024-slim-state-path.md); [ADR-0025 Ring and still-picture formats are free to optimise](../adr/0025-d07-ring-and-still-formats-free.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- **2026-10-09, CI:** main run 588 failed in `ring_core` with "Can't deserialize ... wrong end-of-file signature: ../tb.state". Cause: the tier-2 entries `ring_core` and `ring_trace_core` both build in `sim/ring/build` and write `build/tb.state`, and the runner starts them at the same time; the g++ segfault of run 567 was the same race on the object files. `ci/sims.txt` has one entry `ring_core` now (boot, trace, snap, restore, scrub).
- **2026-10-08 evening, CI:** integ-m1 run 567 failed in the new tier-2 entry `ring_core`: g++ segfaulted (internal compiler error in cc1plus) building `Vtb_ring_top` with `JOBS=3` next to the other sims, most likely out of memory. `ci/sims.txt` builds it with `JOBS=1` now.
- **2026-10-08, board (Claude, solo): `t20_apply.py` PASSED.** Package `t20-fix-5924645d` (run 564), app bca98f2 from the package, REU off (Timewarp is off with the REU, A08), idle BASIC. 20 pairs on the default ranges ($0000-$07FF incl. $01F2, $C000-$C0FF): RAM equal in all 20 and on the jump to the oldest frame. `--full` (all 64 KB) with 3 pairs: equal, oldest jump 1353 frames back equal. The $01F2 difference of run 514 is gone. Test fix: the step table asked for 500 frames back while the ring held only 413 after 45 s; deep targets are now clamped to the ring.
- **2026-10-08 (Claude, branch t20-fix): both open bugs found, proven in simulation, closed in code; board retest pending.**
  - **Bug 2, ring bench `snap`: a bench bug, not RTL.** `tb_ring.cpp` saved and restored the model but not `t_c2`, the pending clk2x fall of its own clock scheduler. A run started from a saved state therefore had another clk2x phase than the continuous reference run, and its captured chip block was one instruction later (CPU block word 2 ends $37 against $3A, `sweep` offsets 161-320 "whole but different", 160 mixed). With `t_c2` in `save()`/`restore()` the sweep has 0 mixed blocks: FAIL -> PASS (commit 04b10d1). The bench is in CI tier 2 now (`ring_core`: boot, snap, restore, scrub), because a bench outside CI is what rotted.
  - **Bug 1, `t20_apply.py` $01F2: not timer phase, not the engine. The CPU walk made a bus cycle.** In the app's `put_back` the RAM is put in at the stop position with `C64_STOP` still set. `ultimate_sys.vhd` makes a write to $0000/$0001 a bus cycle even under `C64_DMA_MEMONLY` when `c64_stop = 1` (T16 Q8: `dma_bus <= c64_stop`). The KERNAL IRQ stores $01 every frame (the sim ring of the idle BASIC holds 20 records at $0001 per 15 frames), so the CPU walk's undo wrote $0001 as a DMA bus cycle at the position hold: `dma_win`/`c64_core` open the gate for one edge on it (the hazard `clear_vic_latches` documents: "a DMA at the stop opens the clock again"), the machine moves off the snap position, the block is loaded there, and the frame that runs after it lands the IRQ on another instruction ($01F2 holds the interrupted PC low byte). The engine's APPLY writes RAM only, so it never does. The test's expectation (engine = CPU walk) is right; the CPU walk was the deviation. The earlier hypothesis (timer phase not restored after landing) is refuted: with the walk done RAM-only the two ways agree in 12 of 12 pairs, including the chip block (`tb_ring scrub`, below). The 2026-10-04 pass was a machine running a BASIC program (the SP is higher there, $01F2 is no IRQ slot), so it never exercised the idle-READY IRQ.
  - **Proof (`designs/c64_basic/sim/ring`, `tb_ring scrub`, Verilator, idle BASIC, 16 frames in the ring, pairs of T by APPLY and T by the CPU walk from another frame):** RAM-only walk: 12 pairs, 0 RAM bytes and 0 chip block words differ (`$01F2` takes CF/D1/D4/CD by target, the same on both sides). `TB_SCRUB_WALKBUS=1` (the walk writes $0000/$0001 as a bus cycle with the machine at the stop): every pair differs in exactly one RAM byte, $01F2, and in 5-10 block words (PC, cycle counters), the board finding of run 514. `TB_SCRUB_DELAY` (the walk waits 0.6 s held) changes nothing: the held machine does not depend on wall time. Before this, `tb_ring_top.v` left the engine's `ap_*` port unconnected, so APPLY never ran in this bench; it is wired as in `top.v` now, and `restore` takes `TB_RING_APPLY=1`.
  - **Fix (app, branch t20-fix-app, commit bca98f2d):** `put_back` lets go of `C64_STOP` once the stop position holds the gate, before the RAM is put in, so the walk's $0000/$0001 write is a plain RAM write. The gate is shut by the position hold, nothing runs. Only the CPU-walk path (old bitstreams, `apply=0`, plans beyond MAX_RUNS) ever did the bus cycle; the default engine path was right, which is why the hand test "klappt SUPER" and `rewind` were never affected. No RTL change, so no bitstream, WNS or LUT delta.
  - **CI (dispatch on t20-fix, run 564): success.** Lint, sims tier 1, the app builds with the fix, 50T bitstream WNS +0.172 ns (RTL unchanged, so no LUT delta by construction). Tier 2 did not run: `ci/select.sh sims-tier` sees `workflow_call` inside the pipeline, not `workflow_dispatch`, so a dispatch gets tier 1; `ring_core` (snap, restore, scrub) ran only locally (Verilator, all PASS) and runs in CI on integ-* / main. Also: the lint's `select.sh` check reads HEAD's commit message, so a message that contains the tier-2 marker in brackets fails the lint (run 560).
  - **Board retest (Alex):** `tests/t20_apply.py HOST 20` with the app from this branch, machine in READY: expect PASS, and the CPU walk's $01F2 now takes the same values as the engine's. See docs/archive/board-tests/board-tests-pending.md T20-A. A re-run with `--full` after that covers all 64 KB.
- **Board 2026-10-08 morning (Claude alone, CI run 514): `t20_apply.py` FAIL.** 8 pairs (no --full): one byte differs at $01F2 in 6 of 8 pairs (stack; low byte of the IRQ return address inside the KERNAL wait loop $E5CD-$E5D4); whole ring (1100 back) equal. Probe: the CPU walk always lands with $D4, the engine with $D1/$CD/$D4 depending on target and origin. 2026-10-04 (run 331) all pairs were equal. Hypothesis, unproven: after landing one frame runs and the CIA/VIC timer phase is not restored, so the IRQ hits elsewhere (same family as the rstep finding of 2026-10-07). **Refuted 2026-10-08, see the entry above.** Machine idle in READY, no BASIC test program.
- **Board 2026-10-07 evening (Alex, Freeze UI; CI run 514, package integ-m1-e2b2ca47, app 301ef8c3, trxmon.u2a built at app 9a1b996b = same trxmon source as integ-m1-app): PASSED** T20-B: Timewarp back across a bank switch with emulated EasyFlash (timoscastle.crt), GMod2 (Sam's Journey CRT) and MegaByter (Lykia CRT): the game continues correctly. T20-REU (menu part): the popup "Timewarp is off while REU is on" fits 40 columns; with REU off Timewarp works. Not run: REST/trxmon/cart-change items of the T20-REU list, T20-A (`t20_apply.py --full`), cart banking with real port carts (T16 group 2).
- Task text: Delta ring, rewind/scrub (docs/specs/T20-delta-ring.md): hardware, app, Timewarp bar with live scrub, F4 drain fix, ring land/commit for trxmon, engine APPLY (undo/redo by the engine, rewind/scrub ~20x faster, tests/t20_apply.py PASS), FPS follows the System Mode, RISC-V tap rv_dbg.v + `trxdbg.sh rvpc` + app DMA watchdog + bounded COPY wait.
- **A08 built 2026-10-07 (app a08-reu-timewarp-app, not board-tested; T20 spec "A08 as built"): REU enabled = ring off and frames dropped, REU off again = ring restarts from then; menu popup, REST 409 (status: on=false + reason), trxmon -32001, one central `trx_ring_reu_sync()`.** **Open:** (1) board test of A08 (docs/archive/board-tests/board-tests-pending.md T20-REU); (2) cart banking of every cartridge type in the ring (audit: M1, open); (3) ring bench `snap` (`tb_ring snap`) FAILED on integ-m1, pre-existing, not from the 2026-10-06 merges (closed 2026-10-08: bench bug); (4) SID, drive A, flash content, VIC internals are not restored (accepted, A08). UI extras -> T54 (M2). Audit: docs/done-audit-2026-10-04.md (A08-A13).
- **by A13 (done 2026-10-06, app a13-no-dump-app 87050ca; Alex: dump/load out of M1, rebuilt properly in M2 as T66):** "Save Timewarp Ring", REST timewarp:dump/ringdump/load, monitor/RPC dump/ringdump/load verbs, `trx_c64re.*` and the C64RE Snapshot app are gone (the removed code is in git history before app commit 24572a6).
- **Board 2026-10-06 (Alex): Timewarp scrub/rewind/RUN-STOP "klappt SUPER".** Bug a (silent hang after scrubbing) not reproduced in ~200 scrub steps (§17.2); `rvpc` names it if it returns.
- **Superseded by SLIM 2026-10-05:** ring areas fixed (no LOG_BASE/LOG_MASK), LOG_PUT gone, statistics counters gone;
- **In main** (t20-m1 merged 2026-10-04, SLIM 2026-10-06 ba060ae).

### T66

Timewarp ring and .c64re snapshot save/load, done properly

Milestone M2, status ⏸. Open (one line, as in the TODO table): Not built; triage with TRX64/C64RE first

Decisions: [ADR-0030 No .c64re / ring dump or load and no trace export in M1 (A13)](../adr/0030-timewarp-dump-and-trace-export-out-of-m1.md); [ADR-0025 Ring and still-picture formats are free to optimise](../adr/0025-d07-ring-and-still-formats-free.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- Task text: Timewarp ring and .c64re snapshot save/load, done properly (A13, Alex 2026-10-06 "nimm es raus und dann in M2 ein Todo das sauber zu implementieren"): M1 has none of it.
- **Also the trace export (Alex 2026-10-06, "Ist ok, schieben wir nach M2"):** `tracering` / `trace/build_from_ring` / `.c64retrace` leave M1 too; the trace former (P4) and `sd` stay. **M2 planning starts with a triage with the TRX64 and C64RE sessions** to agree on APIs and data structures; the hardware is the limiting constraint. Idea to check (Alex, TBD): instead of files, an importer on the host plus a streamed trace with all data, like Gideon's debug stream but enriched with what the ring knows. With a load path, a versioned format, and the exchange with the TRX64 emulator (A09: board snapshot with SID/drive/cart, loading TRX64-written compressed .c64re, .c64rering). Design: docs/specs/T20-delta-ring.md §9, §10, §17.4 (the removed code is in git history before app commit 24572a6). Needs menu, REST and monitor entries, a file type handler for loading, and a confirm screen. Done 2026-10-06: app branch a13-trace-app 1955af33 (trxmon verb/RPC), 7a645059 (ABI `trace_writes` slot reserved NULL); in integ-m1.
