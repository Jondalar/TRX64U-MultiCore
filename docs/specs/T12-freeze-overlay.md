# T12 — Freeze menu on the machine pause: shadow I/O, C64 text overlay, pixel layer

> **SLIM (2026-10-05, branch `slim-core`, docs/specs/SLIM-state-path.md):** the freeze session
> responder this spec describes (shadow overlay, VIC/CIA read rebuild, PLA copy, commit list, reset
> commit, sidefx, the statistics counters, §3, §11 Q1, §14, and the debugger's session §14.8) is
> removed. `freeze_resp.v` became `dma_win.v` (the app's DMA window and the lens unit). A freeze is
> the machine pause; the app's accesses to a paused machine are bus cycles. The C64 text layer
> takes its VIC view from `trx_sys.v` TX_VIEW (page 5, 0x5028). What is left of this document:
> the machine pause (T12 B), the pixel layer and still (§4-§6, §13), the lens unit.

**Status:** draft spec, 2026-10-01. P0 and P1 built (branch arch-p1, §12 below; ARCH §15);
text layer in hardware (§13); decisions Q1-Q3 of 2026-10-03 built (branch t12-session, §14).
**Task:** TODO.md T12 (machine pause:
`docs/machine-pause.md`). **Depends on:** T12 B (merged, on the board in build m10).
**Shares with:** T20 (`docs/specs/T20-delta-ring.md`: colour RAM second port, paused-frame
buffer, DDR2 table), T21 (`docs/specs/T21-monitor.md`: side-effect-free reads, `screen`).

**Revision 2026-10-01 (ARCH).** The hardware is now part of the common design in
`docs/specs/ARCH-lean-state-access.md` ("ARCH"): session reads go through the shared state
bus instead of a copied shadow, only written registers are held (an overlay), $0400-$0FFF
and colour RAM are backed up by the engine instead of a scratch BRAM, the C64 text screen
and the dimmed still are drawn by **software** (our firmware) into the pixel layer instead
of a hardware text layer and still layer, and the registers sit in pages of one IO block.
The freeze semantics, requirements and tests stay. Parts that are gone are marked
**(see ARCH)**.

**One sentence:** while the app's freeze menu is up, every bus access it makes lands in a
shadow of the I/O chips and of $0400-$0FFF instead of the frozen machine, so the C64 clock
never opens and the machine resumes bit-identical; our firmware draws the menu from it as a 40 x 25 C64 text screen into a DDR2 pixel layer
(HDMI box, over a dimmed still of the last frame); the same pixel layer (RGB565, colour key, global alpha) is the
ground for the LVGL UI (T24).

Marks:

- **[TRX64: file]** copied or adapted from our emulator, paths relative to
  `/path/to/Development/C64/Tools/TRX64/`.
- **[new]** FPGA-specific, nothing to copy.
- Gideon's app = `1541ultimate` `upstream/master` (1e17efcb, 2026-09-30), unmodified.
  Its code is cited as `c64.cc:<line>` in that revision.

---

## 1. The problem, from the code

### 1.1 What the pause does today

- `c64_core.v`: the C64 runs on `gclk` (`top.v` `BUFGCE bg_c64`); `halt_req` shuts CE after a
  phi2 falling edge. A bus DMA request while halted sets `ce <= 1` for its cycle
  (`else if (dma_in) ce <= 1'b1`) and sets `held` (`if (dma_in && (!run || halted_r)) held <= 1`),
  which pauses the CPU on its next read through `cpu_pause_req || held`.
- `ultimate_sys.vhd`: a window access is a bus cycle (`dma_bus <= c64_stop and not dma_memonly`)
  while `C64_STOP` is set and `C64_DMA_MEMONLY` is 0; otherwise it goes to `c64_mem`'s second port.
  Reads of $D400-$D7FF never become DMA at all (stubbed $FF, `not (io_req.read = '1' and
  dma_memonly = '0' and address(15 downto 10) = "110101")`).
- `top.v`: `dma_bus` requests go to `c64_core.dma_*`, the rest to the RAM port (`dma_pend`,
  `dma_use = dma_pend && !dbg_re && !dbg_we`). `.ultimax(app_s2[5])` goes into the core live.
- `vic_scaler.blank(c64_halted)`: the C64 window (x 160-1759 of 1920, 4x) is black while paused.

### 1.2 What the app's freeze does through that window

Gideon's freeze with Interface Type = Freeze (`ultimate.cc`: overlay UI only when HPD is set and
`getPreferredType() == 1`; otherwise `c64UserInterface`, host = `C64`, screen =
`Screen_MemMappedCharMatrix(C64_SCREEN $0400, C64_COLORRAM $D800, 40, 25)`, keyboard =
`Keyboard_C64(&CIA1_DPB, &CIA1_DPA, &CIA1_DPA)`, `c64.cc:144-145`). All addresses are
`C64_MEMORY_BASE + a` = the DMA window, byte accesses (32-bit pointers split into 4 bytes).

| Step (code) | Bus accesses while `C64_STOP` = 1, `MEMONLY` = 0 |
|---|---|
| `freeze()` `c64.cc:999` | `frozen_mode = C64_MODE`; `stop(true)`; `backup_io()`; `init_io()` |
| `stop(true)` `c64.cc:414` | `C64_STOP = 1`; `C64_HAS_STOPPED` reads set at once on our hardware (`page_data` = `c64_stop & c64_stop`), so `goUltimax()` (C64_MODE = 2) then reads $D012, $D011, $D01A, $D019; `stop_mode = 1`; `determine_d012()` |
| `backup_io()` `c64.cc:894` | `goUltimax()`; reads $D000-$D030 (49); writes $D011 = 0, $D020 = 0, $D021 = 0; mute through the FPGA mixer (`u64_mute_sids`, no SID access); reads $DC00-$DC0C except $DC08/$DC0B; reads $0800-$0FFF, $D800-$DBFF, $0400-$07FF; writes the 2 KB charset to $0800-$0FFF; reads $DD02, $DD00, $DC02, $DC03; writes $DC02 = 0, $DC03 = $FF; reads $DC01; writes $DC03 = 0, $DC02 = $FF; reads $DC00, $DC0E, $DC0F |
| `init_io()` `c64.cc:958` | RMW $DD02 (`&= $FC`); writes $DC03 = 0, $DC02 = $FF (twice, with reads); RMW $DC0E, $DC0F (`&= $FD`); writes $D018 = $12, $D011 = $1B, $D012 = $F8, $D015 = 0, $D016 = $C8, $D020 = 0 |
| menu | screen writes/reads $0400-$07E7, colour $D800-$DBE7; `set_colors` $D020/$D021; keyboard scan: writes $DC00, reads $DC01 (`keyboard_c64.cc:245-287`) |
| `restore_io()` `c64.cc:1039` | $D011 = 0; writes $0800-$0FFF, $D800-$DBFF, $0400-$07FF from the backups; $DD00 (only if changed), $DD02, $DC02, $DC03, $DC00, $DC01, $DC0E, $DC0F; $D000-$D030 from the backup; `SID_DUMMY` writes $D41F, $D43F, $D51F |
| `resume()` `c64.cc:527` | stop_mode 1: RMW/poll $D012/$D011/$D01A/$D019, `C64_MODE = 0`, read $D000, `C64_STOP = 0` |
| between | `dma_transfer_frozen` (`c64.cc:734`) and `U64Machine` visible reads/writes (`u64_machine.cc`): for $1000-$CFFF/$E000-$FFFF the mode goes back to `frozen_mode` and `MEMONLY` to 0 for the chunk, one bus cycle per byte; $0800-$0FFF and $D800-$DBFF are served from the app's own backups in software |

Under T12 B every one of these bytes costs the machine one cycle (several thousand per menu
redraw), and the VIC that is supposed to show the menu is stopped. On HDMI nothing appears.

### 1.3 Requirements (Alex, 2026-10-01)

1. In Freeze mode the machine stays fully paused: zero `gclk` edges from the first access of
   the freeze to the resume, and the frozen machine is bit-identical before and after.
2. The app's freeze menu shows in the HDMI overlay box, not full screen.
3. Gideon's app stays unmodified.
4. Plain stops that are not a freeze (DMA load, REST `writemem` to I/O while running,
   `start_cartridge`) keep their effect on the real chips.

---

## 2. Architecture

**(see ARCH §7.5-§7.6, §9.2.)** The first draft's `shadow_io` with its session copy and
files, the peek port, `c64_text` and `still_layer` are replaced by:

```
 app DMA window ─► session responder (ARCH §7.6): PLA decode copy, overlay of written
                   registers, keyboard matrix, VIC read formatting
                      │ reads: state bus (zero cycles)   │ RAM/ROM/colour: port B
                      ▼                                  ▼
                   c64_core (gate shut)              c64_mem, colour RAM port B
 engine (ARCH §6): $0400-$0FFF + colour backup at session start, restore at the end
 vic_scaler ─► overlay.v (Gideon's box) ─► pixel layer (DDR2, RGB565) ─► HDMI
 still capture ─► DDR2 slots;  firmware renderer: still + C64 text ─► pixel layer
```

---

## 3. Shadow I/O

### 3.1 Session

A **shadow session** is the time from the first freeze access to the resume. Inside it, no
bus DMA reaches `c64_core`; outside it, T12 B behaviour is unchanged.

Start (all in sys_clk, session responder, ARCH §7.6):

- `SH_MODE` = 1 (auto, default): the session starts when `halt_req` is set (the app's
  `C64_STOP`, i.e. `app_s2[1] & !machine_ctl[4]`, or JTAG bit 3) **and** `app_s2[5]`
  (C64_MODE Ultimax) is set. Every freeze path sets Ultimax before its first I/O access
  (`stop(true)` -> `goUltimax()` before the $D012 read; `backup_io()` again). `stop(false)`
  (DMA load, `U64Machine` peek/poke while not frozen) does not set Ultimax, so its I/O writes
  stay real (requirement 4).
- `SH_MODE` = 2 (always): every pause is a session (for T21 and bring-up).
- `SH_MODE` = 0: off, T12 B as today.

Ordering: the app writes C64_MODE before it issues the next window access, and the IO bus is
in order; `app_s2` is 2 sys_clk stages, `dreq_s` 3, so Ultimax is seen no later than the
request. A bus request that arrives with `halt_req && ultimax_s` but before `halted` or before
the session copy is done waits (its `dma_done_tgl` is just late).

On start there is **no session copy** **(see ARCH §7.6)**: reads fall through to the state
bus, which reads the frozen chips side-effect free in zero cycles. Two things happen at the
start instead:

- the engine copies $0400-$0FFF and the colour RAM (4 KB) to DDR2 (`RAM_OUT`, ≈ 0.13 ms;
  the app's first access waits that long once per freeze, as with the first draft's copy);
- the overlay's valid bits are clear.

End:

- `halt_req` falls (the app's `C64_STOP = 0` in `resume()`): the overlay is cleared and the
  engine writes the 4 KB back (`RAM_IN`) before the resume is let through. The real chips
  were never written, and the RAM areas the menu used are back, so the machine resumes
  exactly where it stopped. `restore_io()` wrote the backup values into the overlay only;
  they equal the real values because every backup read was answered from the frozen chips.
- `cpu_reset_req` rises (C64_MODE reset): **commit on reset** (§3.6), then the session ends.
- JTAG/IO `SH_CTRL.ABORT` or `SH_MODE` written 0: discard and end.

While a session is active, `c64_core.ultimax` gets `ultimax_core` (the value at session start),
not `app_s2[5]`: the frozen machine's PLA never sees the app's Ultimax toggles. At the end it
follows `app_s2[5]` again (by then the app has written C64_MODE = 0, `c64.cc:583`/`596`).

### 3.2 Decode

Each bus request in a session is decoded with a copy of `c64_bus_arbiter.v`'s
`casez ({game, exrom, charen, hiram, loram})` block (`c64_bus_arbiter.v:474` onward), with
`loram/hiram/charen` = CPU port `pout[2:0]` from the copy, `game/exrom` = cart lines from the
copy forced by the **live** `app_s2[5]` exactly as patch 0020 does
(`cart_present || ultimax`, `game && !ultimax`, `exrom || ultimax`), `bus_we` = the request,
`vic_ba` = 1, `vic_write_ab` = 0. Result per access:

| Decode result | Read | Write |
|---|---|---|
| RAM, address $0400-$0FFF | RAM port B | RAM port B (real; backed up at the start, restored at the end) |
| RAM, elsewhere | RAM port B (the `dma_pend` path, zero cycles) | RAM port B (real: intended pokes from `U64Machine`/`dma_transfer_frozen` land) |
| char ROM / KERNAL / BASIC | `c64_mem` rom port B (new, ARCH §7.8) | (the PLA selects RAM on write; follows the decode) |
| colour RAM ($D800-$DBFF, I/O) | colour RAM port B low nibble, high nibble = $F | colour RAM port B (backed up as above) |
| VIC $D000-$D3FF | overlay if written, else state bus (§3.3) | overlay |
| SID $D400-$D7FF | never arrives (`ultimate_sys` stub) | overlay (dropped at the end), counted |
| CIA1 $DC00-$DCFF | §3.4 | overlay |
| CIA2 $DD00-$DDFF | §3.4 | overlay |
| IO1/IO2 $DE00-$DFFF (cartridge, REU $DF00) | $FF, counted in `SH_UNMAPPED` (**since §14: the cartridge model's / REU's answer, no bus cycle**) | dropped, counted (**since §14: commit list**) |
| ROML/ROMH, open Ultimax ranges | $FF, counted (**since §14: ROML/ROMH from the cartridge model**) | dropped (RAM below where the decode says RAM) |

The scratch RAMB36 of the first draft is gone **(see ARCH §7.6)**.

The RAM-port path (`MEMONLY` = 1, `dma_bus` = 0) is untouched: it always reaches the real RAM.
So `U64Machine::clear_ram` and the other `MEMONLY` paths act on the frozen machine as on
Gideon's hardware.

### 3.3 VIC shadow read

Writes store the byte as written. Reads return what the VIC would return without side effects:

- $D011 bit 7 and $D012: the raster at the pause (from the copy), not the written compare value.
- $D013/$D014, $D019 flags, $D01E/$D01F: the copy (collisions are **not** cleared by the read;
  the app reads them in `backup_io()`, a real read would clear them; the shadow is the better
  freezer here).
- Unused bits forced to 1 and $D02F-$D03F = $FF: a constant OR mask per register, copied from
  kawari's read case (`registers_scaled.v:918` onward: `$D016 = {2'b11, ...}`,
  `$D018` bit 0, `$D019 = {irq, 3'b111, ...}`, `$D01A` high nibble, colour registers high nibble).

Semantics as TRX64's two read lanes: `read_reg` (no side effects) for the shadow, never
`read_reg_mut` **[TRX64: crates/trx64-core/src/vic.rs `read_reg` :2900, `read_reg_mut` :2997]**.

`resume()`'s polling loop (`while (VIC_REG(18) != rast_lo && (VIC_REG(17) & 0x80) != rast_hi)`)
exits at once: `rast_hi = raster_hi & 0x80` equals the static shadow bit 7 unless the raster was
0, in which case `rast_lo = rast_hi = 0` and the loop ends on the 40 ms timeout as on any
stopped VIC. Nothing depends on it, because the writes after it are discarded.

### 3.4 CIA shadow read

Copied semantics: CIA `peek` returns timers, TOD, ICR and CR without catching anything up or
clearing anything **[TRX64: crates/trx64-core/src/ciacore.rs `peek` :1115]**.

- Timers, TOD, SDR, ICR, CRA/CRB: from the frozen CIA through the state bus (its combinational
  register read without a strobe), or the overlay's value for written CRA/CRB/DDR/PR.
  Timers do not count, ICR is not cleared by reading, TOD is not latched.
- Port A/B of CIA2: `pins = (PR & DDR) | (pins_at_copy & ~DDR)`.
- Port A/B of CIA1 (the keyboard): live, computed every read from the shadow PRA/DDRA/PRB/DDRB
  and the board inputs, with a verbatim copy of icepi's matrix
  (`c64_top.v:1251-1270`: `cia1_pa_low = ddra & ~pa_out`, key (c,r) pulls PB[r] when PA[c] is low
  and PA[c] when PB[r] is low) and of `c64_keyboard.v`'s joystick merge (`joy_a`/`joy_b` with
  `C64_FLAGS` bits 7-10, `c64_core.v` `C64_FLAGS`). Inputs: `keys_s2` (not the `ovl_own`-masked
  copy), `pins_s2[4:0]`, `pins_s2[9:5]`. The app's keyboard scan (`keyboard_c64.cc:245-287`) works
  unchanged and costs no cycle.

### 3.5 What is lost, by design

> Superseded 2026-10-03 (decision Q1 B): nothing is lost; the commit list of §14.2 writes
> such registers at the resume.

Writes to I/O registers inside a session that `restore_io()` would not have put back on
Gideon's hardware are dropped at the resume: SID registers, CIA timer latches, CIA ICR mask,
CIA2 PB/DDRB, REU and IO1/IO2. They are the app's own doing during a freeze (REST
`writemem` with I/O while the menu is up). VIC registers, CIA1 PA/PB/DDR/CR, CIA2 PA/DDRA
and $0400-$0FFF/colour RAM are restored by `restore_io()` on the real machine anyway, so for
them the result is the same. Open question 1.

### 3.6 Commit on reset

`start_cartridge()` (`c64.cc:1173`) runs inside a session: `goUltimax()`, `VIC_REG(32) = 0`,
`VIC_REG(17) = 0` ("required for the FC3 cartridge"), `C64_MODE = 0`, `C64_POKE(0x8005, 0)`
(RAM, real), `C64_MODE = RESET`. The VIC is not reset by `cpu_reset_req` (icepi `c64_top.v:1244`
`cpu_reset = cpu_reset_i || vic_cpu_reset || rst`; the VIC's own reset is `rst || vic_reset_req`,
`vic_reset_req` tied 0 in `c64_core.v`), so dropping those two writes would start the cartridge
with the old program's screen on. Therefore:

- The overlay's valid bits for the 64 VIC registers are the dirty bits.
- On `cpu_reset_req` rising in a session, the responder holds the reset back from the core
  (`cpu_reset_core = cpu_reset_req && !commit_busy`), replays the dirty VIC registers in address
  order as real bus DMA (the T12 B path: one machine cycle each, `held`), then lets the reset
  through. At most 47 cycles; the app waits 50 ms after the reset write (`c64.cc:1197`).
- CIA and SID overlay entries are not committed: the CIAs are reset with the CPU
  (`c64_top.v:436/488 .reset(cpu_reset)`). The engine writes the 4 KB backup of
  $0400-$0FFF and colour RAM back before the reset is let through, so RAM holds what it
  held before the freeze, as with the first draft's discarded scratch.

### 3.7 Changes to existing blocks

**(see ARCH §13.)** Patch 0051 shrinks to the colour RAM's second port (the side-effect-free
copy of kawari's read case is replaced by the VIC's state-bus words plus a small formatting
step in the responder); the ROM port B comes with ARCH's ROM repack in `c64_mem.v`;
`top.v` gets the responder between `ultimate_sys`'s DMA handshake and `c64_core`/RAM port;
`ultimate_sys.vhd` decodes the one TRX IO block.

## 4. C64 text screen: software renderer (was `c64_text.v`)

> Superseded 2026-10-02 (decision "A"): the text is drawn in hardware again, see §13.
> The rules below stay the software renderer's (the fallback without the layer).

### 4.1 What it draws

The renderer is software in our firmware (ARCH §9.2) **(see ARCH)**. It reads the screen,
colour RAM, charset and VIC registers through the DMA window (which the responder serves
during a session) and draws a 40 x 25 VIC text screen into the pixel layer, with the VIC's
palette. The rules below are the renderer's.

Rendering rules copied **[TRX64: crates/trx64-core/src/render.rs]**:

- Mode from $D011 ECM/BMM and $D016 MCM (`RenderInput::mode`). Drawn: standard text,
  multicolour text (colour bit 3 = 0 cell falls back to hires with colour & 7), ECM
  (`pixels_for_cell`). Bitmap and invalid modes: the box is filled with $D021, status bit
  `UNSUPPORTED` set.
- Screen base = bank + ($D018 & $F0) << 6; char base = bank + ($D018 & $0E) << 10
  (`screen_base`, `char_base`).
- VIC memory read: char ROM at bank-relative $1000-$1FFF in banks 0 and 2, RAM otherwise
  (`vic_read`).
- Bank = (3 - PA[1:0]) * $4000 with PA = the CIA2 **pins** `PRA | ~DDRA`
  **[TRX64: crates/trx64-core/src/c64re_snapshot.rs:2084]**. (TRX64's `text_screen` in
  `crates/trx64-monitor/src/verbs.rs:271` uses the PRA latch alone; that ignores the DDR, and
  `init_io()` relies on the DDR: it clears DDRA bits 0-1 to get bank 0.)
- XSCROLL/YSCROLL, RSEL/CSEL, sprites: ignored (the menu uses none of them). Documented
  deviation.

Palette: `vic_scaler.v`'s `palette()` function, copied, so the box matches the C64 picture.
(It is not TRX64's COLODORE table: blue is 2E2C9B there, 2724C4 in `render.rs:25`; not this
task's business.)

### 4.2 Sources and fetch

Gone **(see ARCH §9.2)**: the renderer reads through the DMA window; there is no hardware
fetcher, cell cache or line buffer.

### 4.4 Geometry

- Text area 320 x 200 C64 pixels; scale S = `TXT_SCALE` or, at 0 (auto), the largest of 1..4
  with 320 S <= 1280 and 200 S <= 800, i.e. 4: 1280 x 800, the same box `overlay.v` computes
  for 40 x 25 of 8 x 8 (its `sx/sy` rule, copied). Centred: first pixel at cx =
  767 + (1920 - 320 S)/2, first line at 45 + (1080 - 200 S)/2 (`overlay.v` `xs_m1`, `y0`).
- `TXT_CTRL.BORDER`: an extra band of `TXT_BORDER` C64 pixels (default 16) in $D020 colour
  around the text area (1408 x 928 at S = 4). Default off: the menu box sits on the dimmed
  still (§5.2) or on black.
- Layer order: the pixel layer is above `overlay.v`; in Freeze mode the app never turns
  `r_on` on, so the two do not meet.

### 4.5 When it shows

> Superseded 2026-10-03 (decision Q3 C): the app sets and clears a show bit
> (`TX_CTRL` b0) itself, §14.4; the show latch below is gone from the hardware.

The renderer shows the box when `FORCE || (AUTO && session && show_latch)`, evaluated in
software on the overlay contents (ARCH §10 page 0x5xxx).

`show_latch` follows the VIC file's writes, in `c64.cc` order:

- a write to $D018 sets `d018_since_off`;
- a write to $D011 with DEN (bit 4) = 0 clears `show_latch` and `d018_since_off`;
- a write to $D011 with DEN = 1 sets `show_latch` if `d018_since_off`.

Trace: `backup_io` $D011 = 0 (off) -> `init_io` $D018 = $12, $D011 = $1B (on) -> menu ->
`restore_io` $D011 = 0 (off), then $D000-$D030 in index order: $D011 (17) before $D018 (24),
so the program's own registers never switch the box back on -> resume, session ends.
`start_cartridge` writes $D011 = 0: off. The rule is fitted to Gideon's order; open question 3.

---

## 5. Pixel layer and still (foundation for T24)

### 5.1 One DDR2 client for video ("v") [new]

`mig_arb` gets a `cart_fill`-style command client `v_*` (commands back to back, read data in
order; `ddr2_port.v` header describes the scheme), with two internal requesters in this
priority: still capture (one word per 4 µs), pixel-layer scanout (no still read: ARCH §7.5).
Priority in `mig_arb` as built: `cart_fill`, drive, then **v** and the CPU/JTAG pair in turn
(T20's "r" / T21's trace client: not built yet), v before the pair once its line fetch is
5.3 µs old (without that, scale 1 lost a line now and then at full load). The drive stays before v: its latency
would otherwise grow by a whole line fetch (3.2 µs); v still gets every other command and
finishes a 240-read line in ≤ 14.5 µs at full load (sim/lat), inside the 17.7 µs of one
output line.

### 5.2 Still: last C64 frame behind the menu (P3)

This is T20's still slots (T20 §5.1 0x5050000-0x506FFFF), built here first; T20 then only
adds REGEN. Not counted twice.

- Size: the scaler's window is 1600 x 1080 at 4x (`vic_scaler.v` `X_WIN0 = 160`, `X_WIN1 = 1760`)
  = 400 x 270 native pixels x 4 bit = 54 000 B. (T20 wrote 384 x 272 = 52 224 B; both fit its
  64 KB slots. Use 400 x 270, the area the scaler shows.)
- Capture: while not paused, a packer on `pix_color/pix_x/pix_y` (sys_clk) collects 32 pixels
  per 128-bit word into a 1-line LUTRAM buffer (400 x 4 bit) and writes frame n into slot
  n & 1. The slot of the last complete frame is latched at `halted` rising. 2.7 MB/s.
- Show: the firmware draws the last complete slot into the pixel layer at 2 x 2, halved in
  brightness (the same dim `overlay.v` applies inside its box), once per freeze. No
  `still_layer` and no line FIFO **(see ARCH §7.5)**.
- After a resume the scaler shows the live VIC again (the frame lock is kept by T12 B).

**60 Hz (T40 System Modes NTSC, PAL-60; bug found and fixed 2026-10-04, branch `still-60hz`).**
The 50 Hz capture window (400 x 270 from line 16) needs raster lines up to 285; a 60 Hz VIC has
lines 0-262, so the capture never completed there: no `sd_tgl`, STILL_STATUS never updated, no
frozen picture (T12), no scrub picture (T20), no frame for T24 §14. The scaler's 60 Hz window is
400 x 240 from line 31 through 262 into lines 0-7 of the next frame. The still has the same 240
rows, packed the same (32 pixels per word, same slots, row pitch 400 pixels), but from **one**
VIC frame, so it is complete at line 262 and the scrub's stop on line 0 (T20 §16.7, `STOP_POS`
only stops on line 0) does not lose it: lines 0-7 are written first, into rows 232-239 (words
2,900-2,999), then lines 31-262 into rows 0-231 (words 0-2,899); done after word 2,899, 3,000
words = 48,000 B. Against the scaler only rows 232-239 differ (the next frame's lines 0-7 there:
border, unless a program changes the border within one frame). 50 Hz: unchanged (3,375 words).
`pix_layer` takes the mode from the scaler's `v60` (2-FF into `vclk`) when a capture starts
(50 Hz: line 16 x 96; 60 Hz: line 0 x 96) and keeps it for the frame.

STILL_STATUS (0x101A6068) as built: b0 valid, b1 slot of the last complete frame, **b2 that
frame is 60 Hz: 400 x 240 rows (0: 400 x 270)**, b7:3 0, b31:8 frames captured. b2 is latched with
the slot, so it describes the frame in that slot. The firmware draws the 240 rows onto the 540-row
layer with 9 layer rows per 4 still rows (2, 2, 2, 3), nearest row.

### 5.3 Pixel layer (P4)

| Item | Proposal |
|---|---|
| Resolution | 960 x 540, scale 2 to 1920 x 1080 (integer, full frame, 16:9). Also any W <= 960 at scale 1 or 3 |
| Formats | RGB565 only (decision 2026-10-02; LVGL `LV_COLOR_FORMAT_RGB565`, little-endian, R 15:11, G 10:5, B 4:0), `PX_CTRL` format 1; widened to 888 by bit replication. ARGB8888 is gone (half the DDR2 reads, no per-pixel alpha multiply in the pixel clock) |
| Line limit | one line <= 1 920 bytes (960 RGB565) |
| Line buffers | 2 lines x 1 920 B in one 256 x 128 BRAM (2 RAMB36: MIG width on the write side, 16 bits per pixel on the read side, output register used) |
| Transparency | `PX_KEY`: one RGB565 value (b15:0) shown transparent while b16 is set; no per-pixel alpha |
| Blend | `out = bg + a * (fg - bg) / 256` per channel with a = `PX_GALPHA` + its bit 7 (exact at 0 and 255): 3 DSP48E1 with A/B, M and P registers (Artix-7 50T has 120). Fixed alpha steps would save the 3 DSPs but cost ≈ 60 LUT of shift-adds; LUTs are the scarce resource, DSPs are not |
| Position | `PX_X0/Y0` output pixel of the layer's top left; outside the layer: pass-through |
| Page flip | `PX_FRONT` is read at cy = 0; a write to `PX_BACK` sets `FLIP_PENDING`, swapped at the next frame start, `FLIP_DONE` status and IRQ |
| Order | top of everything: C64 -> `overlay.v` -> pixel layer |
| Where the buffer lives | any DDR2 byte address (16-byte aligned). T24's RISC-V reaches only the low 64 MB (T20 §5.1), so its firmware places the buffers in its own 64 MB; no change to T20's table |

Bandwidth (scanout, 50 Hz, MIG peak 1.2 GB/s at 128 bit x 75 MHz, T20 §5.4):

| Mode | Per frame | MB/s | Of peak |
|---|---|---|---|
| 960 x 540 RGB565 (built) | 1.04 MB | 52 | 4.3 % |
| 800 x 540 RGB565 (freeze menu: the VIC window) | 0.86 MB | 43 | 3.6 % |
| 960 x 540 ARGB8888 (until 2026-10-02) | 2.07 MB | 104 | 8.6 % |
| still capture + read | 0.054 MB | 2.7 + 2.7 | 0.5 % |

With T20 worst case (11.8 MB/s), T21 trace (8 MB/s) and LVGL drawing (the CPU writes its
frame, order 50 MB/s at 50 fps full redraws) the default mode stays under 15 % of peak.
Per line: 120 MIG reads = 1.6 µs of the 35.5 µs a source line is on screen at scale 2.

Built: 960 x 540 RGB565, double buffered (2.1 MB of the app's memory), LVGL in direct mode
with the two buffers and the flip register.

Per TODO.md T24, the register block of §6.4 is the draft to hand to the C64U_emu session
so UE2 can model it.

---

## 6. Register map

**(see ARCH §10.)** The TV block at 0x101E0000 is gone: the session registers (`SH_*`,
overlay readback) are page 0x5xxx and the pixel layer and still capture page 0x6xxx of the
one TRX IO block at 0x101A0000, with the first draft's field layouts minus the text layer
(`TXT_*`), the still display bits and pixel formats 1-2. `PEEK_*` are replaced by the state
bus (page 0x1xxx); the digest is software.

---

## 7. Budget

**(see ARCH §11.)** The first draft's ≈ 2,150 LUT / 3 BRAM become ≈ 260 LUT (responder) plus
the shared video part (≈ 500 LUT, 2 BRAM, 3 DSP) in ARCH.

---

## 8. Test plan

### 8.1 Simulation first (Verilator, one at a time, niced)

New wrapper `sim/shadow/shadow_top.v` = `c64_core` (with the state bus) + the session
responder + the RAM-port arbiter cut from `top.v` + an engine model for the 4 KB backup; `build.sh` derived from
`sim/dma_io/build.sh` and `sim/pause/build.sh`. Bench `tb_shadow.cpp` reuses `tb_dma_io.cpp`'s
freeze sequence (stop / backup_io / init_io / menu / keyboard scan / restore_io / resume) and
`tb_pause.cpp`'s reference-run comparison.

1. **State bus = bus.** For every VIC register without read side effects and CIA registers
   0-$C except TOD hours/tenths: the responder's answer from the state bus == bus DMA read
   value (T12 B path), at 20 random points of a running program. Colour RAM port B == bus
   read for all 1024.
2. **Zero clocks.** Full freeze sequence through the responder: `gclk_edges` unchanged from
   pause to resume, `SH_REAL_CYCLES == 0`, `held` never set, drive cycles unchanged.
3. **Bit-identical.** RAM (64 K), all state-bus words, colour RAM, raster, CPU (`tb_pause`'s
   checks) equal before and after; the bench's digest before == after. Then run 5 frames and compare
   with a reference run without the freeze (as `tb_pause run`).
4. **Answers.** Every read the freeze makes returns the frozen value or the overlay's (log
   vs. state-bus image); after `init_io` the overlay holds $D018 = $12, $D011 = $1B,
   $DC02 = $FF, $DC03 = 0, $DD02 & 3 = 0; RAM holds the charset at $0800 and the menu text
   at $0400 during the session and the program's bytes after the resume.
5. **Keyboard.** `keys` bit for A (col 1, row 2): `$DC00 = $FD` -> `$DC01` bit 2 low; no key:
   $FF; joystick 1 fire: `$DC01` bit 4 low; joystick 2 up: `$DC00` bit 0 low.
6. **Plain stop is real.** No Ultimax: bus write $D020 = 5 while stopped, resume, peek
   $D020 = 5 (requirement 4).
7. **Commit on reset.** In a session write $D011 = 0, $D020 = 2; reset: peek $D011 = 0,
   $D020 = 2 after the reset; CIA reset as before.
8. **Text render** (host test of the firmware renderer, no HDL): its output as PPM against a
   Python reference that renders the same memory/regs with the `render.rs` rules (standard,
   multicolour, ECM, bank 0-3, char ROM in banks 0/2, a charset at $3000 in bank 1) and diffs.
   Box position and scale for S = 1-4 and the border band.
9. **Auto show.** `show_latch` sequence of §4.5 (software) over the overlay log of the full
   freeze; the box never shows the program's screen during `restore_io`.
10. **Regression.** `SH_MODE = 0`: `sim/pause` and `sim/dma_io` pass unchanged; `tb_scaler`,
    `tb_overlay`, `tb_mig_arb` pass.
11. **mig_arb client v** (`tb_mig_arb` extended): scanout at 960 x 540 RGB565 with
    `cart_fill` line fetches, drive reads and CPU traffic at full rate: no line underrun, cart
    and drive latency within today's bounds; capture/still writes land at the right slot.
12. **Blend** (`tb_pixlayer.v`, iverilog): RGB565 with bit replication, global alpha
    255/128, colour key on and off, flip at frame start, underrun, still.

### 8.2 Board (Alex, after a build; no JTAG from the spec side)

Build m11 = P0-P2 (P3/P4 later builds).

1. Interface Type = Freeze, a program running that shows motion (e.g. BASIC
   `10 POKE 53280,PEEK(53266):GOTO 10`). Press Menu: the menu appears in the box, C64 window
   black (P2) or the dimmed last frame (P3). Navigate, enter a sub-menu, leave with RUN/STOP.
   The program continues; the border bars look as before.
2. `trxdbg.sh` reads the session page (ARCH §10, page 0x5xxx through the JTAG page): session
   count +1, `SH_REAL_CYCLES` 0, `SH_GCLK` 0; the software digest (test mode) equal.
3. Same with Interface Type = Overlay on HDMI: as in m10 (regression).
4. Without a freeze: REST `writemem` $D020 (or the web UI poke) changes the border
   (requirement 4).
5. From the freeze menu start a cartridge (any CRT, FC3 if at hand): it starts with a clean
   screen (commit on reset). Reset from the menu: clean reset.
6. Keyboard in the menu: all cursor keys, RETURN, RUN/STOP, F-keys; joystick in port 2 if the
   menu reacts to it.
7. A raster-timed demo frozen and resumed 10 times: no visible glitch (bit identity in
   practice).
8. P4: `trxdbg.sh` writes a test pattern (RGB565 colour bars with key-coloured gaps) into DDR2 and the
   PX registers: bars over the running C64 picture, the C64 visible in the gaps, `PX_GALPHA` 128 blends; flip between two buffers.

---

## 9. Phases

Hardware phases are ARCH §12 **(see ARCH)**:

| P | Content | ARCH phase | Proof |
|---|---|---|---|
| P0 | state bus (VIC, CIA, CPU port), colour RAM port B, ROM port B | A1, A2 | sim 1 |
| P1 | session responder: detection, PLA copy, overlay, keyboard, backup through the engine, commit on reset, counters | A3, A5 | sim 2-7, 10 (digest in the bench) |
| P2 | firmware renderer into the pixel layer, auto show | A6 | sim 8-9 (host); board build = A0-A6, board 1-7 |
| P3 | still capture, client "v", dimmed still drawn by the renderer | A6 | sim 11 (part); board 1 shows the dimmed frame |
| P4 | pixel layer for T24 (flip, IRQ), register page to C64U_emu | A6 | sim 11-12; board 8 |

---

## 10. Consequences for the other specs

Settled in ARCH: T21's warning about a freeze entry can go once P1 is on the board; T21 Q2
(picture while paused) is the still + renderer; T20's paused-frame buffer is the still
capture; the colour RAM second port is patch 0051 for all three.

---

## 11. Open questions

1. **I/O writes inside a freeze** that `restore_io()` would not undo (SID, CIA timer latches,
   ICR mask, CIA2 PB, REU, IO1/IO2) are dropped at the resume. Acceptable, or keep a commit list
   for a resume as for a reset (costs bit identity only when such writes happened)?
   **Decided (Alex, 2026-10-03): B, commit list.** Such writes are collected during the session
   and executed as real bus cycles at the resume, as at a reset; bit identity holds only when
   none happened. Reason: on Gideon's core freeze writes are real cycles (drop-in), and the T21
   monitor's I/O pokes need it.
   **Target picture for the app's peek/poke (Alex, 2026-10-03):** Gideon's DMA window stays the
   app-facing interface (used all over his code: c64.cc, menu, PRG load, REST readmem/writemem);
   only the hardware behind it changes: RAM read/write through the second BRAM port (no bus
   cycle), I/O reads from the shadows (no side effect), I/O writes through the commit list,
   large blocks through the T20 engine (COPY; APPLY not built yet). freeze_resp.v is the place
   for the commit list.
2. **Cartridge and REU in a session** read $FF, writes dropped (`SH_UNMAPPED` counts them).
   Needs a `cart_slot` peek if the app ever reads ROML/ROMH or IO1/IO2 while frozen.
   **Decided (Alex, 2026-10-03): B, cartridge peek.** ROML/ROMH reads in a session go to a read
   port of the cart cache (`cart_slot`, the currently banked contents, no side effect); IO1/IO2
   registers and the REU status come from shadows (a real read would clear bits); writes go
   through the commit list of Q1. Estimate 50-100 LUT. Makes the T21 monitor usable with a
   cartridge.
3. **Auto show** is fitted to `c64.cc`'s write order (§4.5). Alternative independent of the
   order: show while the session's $D018 points at the screen the app wrote last. Keep the
   simple rule?
   **Decided (Alex, 2026-10-03): C, explicit from the app.** Our code sets a show bit in
   `TX_CTRL` when the freeze menu opens and clears it when it closes, from the existing hooks
   in `C64::freeze()` / `C64::unfreeze()`. The hardware no longer guesses from the VIC write
   order: the `show_latch` logic of §4.5 goes, and with it the dependency on Gideon's write
   order and the flash of the box at the resume.
4. **Box look:** text area only (1280 x 800) over the dimmed still, or with the $D020 border
   band as a real C64 screen?
   **Decided (Alex, 2026-10-03): A, text area only** (as built). The UI decoupled from
   Gideon's comes in stage 2 (T24, LVGL).
5. **Pixel layer default:** answered 2026-10-02: 960 x 540 RGB565 at scale 2 (52 MB/s, 1 MB
   per buffer), colour key + global alpha, no per-pixel alpha. Which LVGL version does T24
   pin?
   **Answered:** LVGL v9.6.0, vendored in `software/lvgl` (T24).
6. **LUT budget:** answered by ARCH §11 (≈ 76 % with T12, T16.2, T20, T21).
7. **App interrupt** for PX vsync/flip: answered: one shared line with a cause register, `irq_high(2)` since T28 (ARCH §14
   item 1).
8. **Digest:** now software (ARCH §7.6), test mode only; no hardware cost either way.
9. **`SH_MODE = 2` for T21:** should a JTAG/T21 pause always be a session (I/O views without
   side effects, but pokes to I/O lost at resume), or should T21 drive real cycles on purpose?
   **Decided (Alex, 2026-10-03):** a debugger pause is a session by default (reads without side
   effects; writes go through the commit list of Q1 and take effect at the resume); real bus
   cycles only on request (`sidefx on`), as TRX64's `monitor_read`/`peek` and as trxmon is built.

---

## Appendix: what is copied from TRX64

| Here | From |
|---|---|
| VIC mode decode, screen/char base, char-ROM shadow in banks 0/2, cell pixels for standard / multicolour / ECM text (§4.1) | `crates/trx64-core/src/render.rs` (`RenderInput::mode`, `screen_base`, `char_base`, `vic_read`, `pixels_for_cell`) |
| VIC bank from CIA2 pins `PRA \| ~DDRA` (§4.1) | `crates/trx64-core/src/c64re_snapshot.rs:2084` |
| text screen at the live screen pointer as a viewer (renderer outside a session) | `crates/trx64-monitor/src/verbs.rs` `text_screen` (with the DDR fix above) |
| two read lanes: peek without side effects vs. live read (§3.3, §3.4, `SH_MODE`) | `crates/trx64-core/src/vic.rs` `read_reg` / `read_reg_mut`, `crates/trx64-core/src/ciacore.rs` `peek`, `crates/trx64-monitor/src/verbs.rs` `monitor_read` (`sidefx`) |

Copied from this repo: the keyboard matrix (`cores/icepi/c64_top.v:1251-1270`), the joystick
merge (`cores/icepi/c64_keyboard.v`), the PLA decode (`cores/icepi/c64_bus_arbiter.v:474`),
the box geometry (`designs/c64_basic/overlay.v`), the palette (`designs/c64_basic/vic_scaler.v`).

---

## 12. As built (branch arch-p1, 2026-10-01/02) — for P2

Hardware for P0/P1 is in (`designs/c64_basic/freeze_resp.v`, `trx_sys.v`, `c64_core.v`;
ARCH §15 has the register map and the state bus words). What the firmware (P2) needs:

- **Turn it on:** `GATE_CTRL` (0x101A0100) b0 = 1 makes the app's C64_STOP a machine pause
  with sessions; reset value 0 = the old CPU-only pause, so Gideon's freeze menu keeps showing
  on the C64 screen until the renderer exists. Set b0 when the renderer is up (Freeze UI mode);
  `SH_CTRL` (0x101A5000) keeps SH_MODE 1 (auto) from reset. JTAG register 9 bit 4 still
  forces CPU-only.
- **Session status (b2 gone since §14.4):** `SH_STATUS` 0x101A5004: b0 session, b2 `show_latch` of §4.5, kept in
  hardware over the session's VIC writes in their order (the overlay keeps only the last
  value per register, so software could not rebuild the order). The overlay itself (what
  the app wrote to VIC, CIA, SID in this session) is at 0x101A5080-50FF with valid bits at
  0x101A5100-510F (32-bit reads, byte 0 first). Seen in simulation: `resume()` writes the
  program's own $D011 (DEN on) after `restore_io()` wrote $D018, so by the §4.5 rule the
  latch is up again for the microseconds until C64_STOP = 0 ends the session; the renderer
  should show the box only while b0 and b2 are both set and keep it for a moment (or wait
  for the session end) before drawing the still again (open question 3 stays). IRQ_CAUSE
  b0 (machine stopped) can wake the renderer.
- **Reading the menu:** through the DMA window as the app does (0x10050000 + address): inside
  a session every access is answered by the responder without a C64 cycle: $0400-$07E7 and
  $0800-$0FFF from RAM, $D800 colour RAM (high nibble $F), $D000-$D02E VIC as written by the
  app (overlay) or frozen, CIA 2 $DD00/$DD02 for the bank. The window decodes with the
  frozen CPU port and the live C64_MODE Ultimax bit, like the machine's PLA, so $D000-$DFFF
  is I/O for the menu; the char ROM's bytes are those the app wrote into `c64_mem.v`'s ROM
  block at boot (its CHAR ROM setting, by default `characters.901225-01.bin`); a renderer
  that carries its own copy must take it from the same file.
- **Decided differently from §3:** no engine backup of $0400-$0FFF/colour RAM (ARCH §15.2-2):
  RAM writes in a session are real and the app's `restore_io()` restores them, as on Gideon's
  hardware. Commit on reset as §3.6 (written VIC registers but $D013/14/1E/1F, Ultimax
  forced, then the reset). SID reads never arrive (ultimate_sys stub) and read $FF if they
  do. A request that arrives before the machine has stopped waits for the stop.
- **Proof in simulation:** `designs/c64_basic/sim/statebus` (T12 §8.1 tests 1-7 and 10: the
  digest is a CRC over RAM, the state bus words and colour RAM in the bench). Tests 8, 9, 11, 12
  belong to P2-P4.

## 13. As built: C64 text layer in hardware (branch ovl-c64text, 2026-10-02)

The software renderer cost 60-100 ms per key press (40 ms polling, byte reads over the IO
bus, 2 x 2 drawing into DDR2, flip). `designs/c64_basic/c64_text.v` draws the text instead,
every frame:

- **Layer order:** scaler -> `overlay.v` -> `pix_layer.v` -> `c64_text.v` -> HDMI. The
  still stays on the pixel layer (drawn once by the app at the freeze, mixed with the menu
  background as before); the text goes on top of it with background ($D021) transparent,
  or $D021 with `TX_CTRL.opaque`. `overlay.v` (Gideon's HDMI overlay registers) is not
  touched: in Freeze mode its box is off, and its register map is Gideon's.
- **Fetch:** in the C64 clock, per C64 line of the text area, 40 screen bytes and 40 glyph
  bytes (RAM, or the char ROM block at bank-relative $1000-$1FFF in banks 0/2) through
  `c64_mem`'s second port and 40 colour nibbles through the colour RAM's second port, into a
  two-line LUT RAM buffer, about 165 clocks of the 2,244 a line has. Port priority: the
  freeze responder, then JTAG, then the text layer; the colour RAM port is the responder's
  only in the clock it writes and in its new `S_CRAM` address clock (`cram_use`). No C64
  cycle, no change to what the responder answers.
- **Registers:** the session's VIC/CIA 2 writes as `freeze_resp.vic_view` ($D011 ECM/BMM,
  $D016 MCM, $D018, $D021, bank pins `PRA | ~DDRA`; a register not written in the session
  counts as 0, CIA 2 as bank 0). Shown while `TX_CTRL.enable`, session, show latch (§4.5)
  and standard text mode, checked per output line (since §14.4: `TX_CTRL` b0 = the app's
  show bit, session and text mode; no latch). Not drawn: scroll, 38/24, sprites,
  multicolour, ECM, bitmap.
- **Geometry:** each C64 pixel 4 x 4, the text area at (1088, 185), 1280 x 800: the
  pixel layer's C64 window (window x 40, y 35), so the text sits on the still where the
  software renderer put it.
- **Registers (page 6):** `0x101A60C0 TX_CTRL` RW b0 enable, b1 opaque, R b31:24 = 1
  (the layer exists; older bitstreams read 0). `0x101A60C4 TX_STATUS` R b0 shown now,
  b15:8 lines not drawn for a late fetch.
- **App (trx_video.cc):** on in `freeze_enter()` after the still, off first in
  `restore_io()` (`trx_video_menu_off()`: `resume()` raises the show latch again with the
  program's screen back) and at teardown; the task no longer reads or draws text. Without
  the layer the software renderer runs as before.
- **Simulation:** `sim/statebus` test 4: with the app's freeze sequence the 1080p text area
  is the menu byte-exact (1,024,000 pixels) while the app's window and JTAG use the ports,
  opaque and char ROM too, hidden after `restore_io`'s $D011 = 0; no C64 clock edge, no
  real bus cycle.
- **Cost (yosys estimate, not Vivado):** about 210 LUT logic + 32 LUT as RAM, 230 FF + the
  5-clock video delay in FF, no block RAM; five pixel clocks more to the encoder. Timing:
  every pixel-clock stage is one LUT RAM read, one 8:1 mux, one 4-input palette or one
  2:1 mux between registers.


## 14. As built: commit list, cartridge look, show bit (branch t12-session, 2026-10-03)

Decisions Q1-Q3 and the `sidefx` part of Q9 (§11). Code: `designs/c64_basic/freeze_resp.v`
(header has the rules), `trx_sys.v` (page 5), `c64_text.v`, `cart_slot.v`, `cart_fill.v`,
`c64_core.v`, `top.v`; `cores/icepi/c64_cartridge.v`, `c64_reu.v`, `c64_bus_arbiter.v`,
`c64_top.v`. App: `software/io/c64/trx_video.cc`, `c64.cc` (branch t12-app of the app).

### 14.1 What changed against §3, §4.5, §12, §13

| Was | Now |
|---|---|
| I/O writes in a session that `restore_io()` does not undo are dropped (§3.5) | commit list (§14.2): they reach the chips at the resume |
| ROML/ROMH, IO1/IO2 read $FF in a session (§3.2) | the cartridge model's answer without a bus cycle (§14.3) |
| the text layer shows on `TX_CTRL.enable` and the hardware's show latch (§4.5, §13) | on `TX_CTRL` b0 alone, set and cleared by the app (§14.4); `SH_STATUS` b2 is gone |
| every access in a session is answered without a bus cycle | `SH_CTRL` b2 SIDEFX: I/O and cartridge accesses are real bus cycles (Q9) |

The Gideon-app view is unchanged: the DMA window stays the interface; RAM through the second
port, I/O reads from the shadows, I/O writes through the commit list, cartridge reads from the
cartridge model (Q1 target picture).

### 14.2 Commit list (Q1)

**Mechanism.** Every I/O write in a session still goes into the overlay (reads see it, §3.3,
§3.4). In addition the write is listed (128 entries of {plain, address $Dxxx, data}, LUT RAM)
unless `SH_CTRL` b3 PROTO is set. When the session ends because the app's `C64_STOP` falls, the
list is replayed in order as real bus cycles (the T12 B path, Ultimax forced so I/O is seen,
one machine cycle each, the CPU held by RDY) while `hold` keeps the machine stopped from the
clock `C64_STOP` falls; then the overlay is dropped and the machine resumes at the next whole
frame as before. An empty list ends the session without a bus cycle: bit identity holds exactly
when no entry had to be written. `SH_CTRL` b8 (abort), `SH_MODE` written 0 (the ring's restore
does this) and b9 (discard) drop the list unexecuted. A full list counts what it loses
(`SH_LIST` b31:16).

**Classes** (`freeze_resp.v` `vic_plain`, `vic_none`, `w_plain`, `w_load`):

| Register | Class | Written at the commit |
|---|---|---|
| VIC $D000-$D010, $D015-$D018, $D01A-$D01D, $D020-$D02E | plain | the last value, only if it (with the unused bits set) differs from the frozen chip's read (state bus) |
| VIC $D011 | ordered | bit 7 (raster compare bit 8) as written, bits 6-0 the last value |
| VIC $D012 | ordered | as written: the raster compare (a read in the session still shows the raster) |
| VIC $D019 | ordered | as written (acknowledge). Reads in the session leave the acknowledged flags out, bit 7 from the rest and $D01A |
| VIC $D013/$D014, $D01E/$D01F, $D02F-$D03F | none | nothing: a write does nothing on the chip |
| CIA PRA, PRB, DDRA, DDRB ($x0-$x3) | plain | the last value if it differs from the port register / DDR |
| CIA CRA, CRB ($xE/$xF) | plain, and ordered with LOAD | without the LOAD bit: plain, compared with bit 4 masked (it reads 0); a write with LOAD (bit 4) is also listed as written, so the timer loads the latch written before it, in the order the program made them |
| CIA timer latches $x4-$x7 | ordered | as written (a high byte with the timer stopped loads the counter, as on the chip) |
| CIA TOD/alarm $x8-$xB | ordered | as written, in order (hours stop the clock, tenths start it; CRB b7 decides clock or alarm as on the chip at that point of the list) |
| CIA SDR $xC, ICR $xD | ordered | as written (ICR mask set/clear in order) |
| SID $D400-$D7FF (every SID the app maps) | ordered | as written (write-only: no frozen value to compare with) |
| IO1/IO2 $DE00-$DFFF (cartridge registers, REU, UCI) | ordered | as written (IO2 reaches the REU: `c64_bus_arbiter.v` arbitrates IO2 with the cartridge's own lines, not the Ultimax the DMA forces) |
| colour RAM, RAM | — | written at once, as before (§12) |

Ordered entries are written as written, one each; a plain register takes one entry at its first
(non-PROTO) write and is written once, with its last value, at the place of that first write.
Semantics as TRX64's monitor `wr io`, which routes the byte to the chip's register write with
its side effects **[TRX64: crates/trx64-monitor/src/verbs.rs `monitor_write` :303,
crates/trx64-core/src/lib.rs `poke_io`]**; here it happens at the resume, one machine cycle per
byte, because the paused chips take no write without a clock (staged writes, ARCH §5.2, are
for the hidden state, not for register writes with side effects).

**PROTO and Gideon's freeze.** Gideon's protocol (`stop(true)`, `backup_io()`, `init_io()`;
`restore_io()`, `resume()`) puts back what it changes, and on the paused machine some of its
writes would be wrong if they reached the chips: `determine_d012()` and `resume()` write $D012/
$D011 from reads of the frozen raster (the VIC does not move, so the "found" compare is the
raster), acknowledge $D019 ($81) and write the SID dummies $D41F/$D43F/$D51F. Our firmware
therefore sets `SH_CTRL` b3 around the protocol (trx_video.cc): from `trx_video_freeze_enter()`
(before `stop(true)`) to `trx_video_menu_on()` (end of `C64::freeze()`), and from
`trx_video_menu_off()` (start of `restore_io()`) to the end of the session; the hardware clears
b3 when a session ends. The menu's own writes (colours, the keyboard scan's $DC00, the text
layer test's $D018) and REST `writemem` / monitor pokes while the menu is up are listed; for a
plain freeze `restore_io()` writes the plain registers back to their frozen values, so the
commit writes nothing (board: §14.6). A REST `writemem` of $D020 while the menu is up is undone
by `restore_io()` as on Gideon's hardware; a write to $D012, a CIA timer or the SID stays.

**Reset.** A reset in a session (start_cartridge, the menu's reset) is unchanged (§3.6): the
written VIC registers but $D013/14/1E/1F are replayed whatever PROTO said (the old behaviour:
Gideon's hardware had written them for real), the rest of the list is dropped (CIAs, SID, REU
and cartridge are reset).

### 14.3 Cartridge look (Q2)

ROML/ROMH reads and IO1/IO2 reads in a session (not SIDEFX) go to `c64_core`'s look: a copy
of the cartridge model's decode for the window's address with the banking registers as they are
(`c64_cartridge.v` `pk_*`, the Action Replay's PLA a second time), combinational from the paused
model, no register written:

- IO1 and the model's IO registers: the value a read returns now (EasyFlash/Ocean/… `reg_0`/
  `reg_1` by A0, GMod2 the EEPROM's DO in bit 7, AR/RR the status byte), without the read side
  effect (Simons' Basic, C64 Game System and Dinamic switch banks on a read; the look does not).
- ROM and cartridge RAM: the byte from DDR2 (`cart_fill`'s new look port: one MIG read of the
  16-byte word, between `cart_slot`'s requests, nothing written to the cache; `cart_slot`'s
  requests go first, so a write the model queued before the stop is in DDR2 first). Image
  address exactly as `cart_slot` forms it (16 KB per bank, ROMH 8 KB above, one-chip carts in
  the lower halves).
- IO2 to the REU where a read would go there (`c64_bus_arbiter.v` `pk_reu`: REU present and no
  cartridge, or the cartridge's lines inactive and the last IO2 access was the REU's): the
  register file as `c64_reu.v` reads it, the status ($DF00) not cleared.
- Not shown: the AMD flash responder's ID/status modes (the array is shown), the UCI's
  registers (the look shows what is below them; a UCI read pops its FIFO), open bus (the look
  answers $FF where nothing drives the bus, counted in `SH_UNMAPPED`).
- **Physical port (T16, slot_port.v):** a cartridge on the real port cannot be read without a
  bus cycle. Where the port serves the read (`slot_port` `rom_port` for ROML/ROMH, `io1_rd`,
  `io2_rd`; `c64_core` `pk_ext`) the look answers $FF and counts it; `SH_CTRL` b2 SIDEFX makes
  such reads real bus cycles (one machine cycle each, with the cartridge's own side effects).

Writes to ROML/ROMH in a session (Ultimax flash writes) are dropped and counted as before.

The look is also reachable outside a session, through registers (T21 §3.4.1, `LK_REQ`/`LK_RES`
on this page, CAPS2 b4): the monitor's and REST's `cart` and `cpu` lenses use it, and for a
cartridge on the physical port the same unit makes a real bus cycle with a forced select instead
of answering $FF.

### 14.4 Show bit (Q3)

`TX_CTRL` (0x101A60C0) b0 is the show bit. `c64_text.v` draws while b0, a session and standard
text mode; the show latch (§4.5) and `SH_STATUS` b2 are gone. The app (trx_video.cc) sets b0 in
`trx_video_menu_on()` at the end of `C64::freeze()` (after `init_io()`), clears it in
`trx_video_menu_off()` at the start of `restore_io()` (and in the ring's unfreeze path), in the
teardown, and when Timewarp's bar takes the layer (`trx_video_tw_begin()`, T20 §16.7: the bar
already turned the layer off with b0 = 0; that is now the one rule, there is no other source of
"show"). The software renderer (no text layer) uses the same moment (`menu_shown`).

### 14.5 Registers (page 5, trx_sys.v; CAPS2 b3)

| Address | Name | Access | Content |
|---|---|---|---|
| 0x101A5000 | SH_CTRL | RW | b1:0 SH_MODE (0 off, 1 auto, 2 every pause), b2 SIDEFX, b3 PROTO (cleared when a session ends); W b8 abort, W b9 drop the commit list |
| 0x101A5004 | SH_STATUS | R | b0 session, b1 a commit runs (reset or list), b31:16 sessions started |
| 0x101A500C | SH_REAL | R | bus cycles the machine ran in sessions and commits |
| 0x101A5014 | SH_UNMAPPED | R | accesses nothing answers without a bus cycle (open ranges, no cartridge, the physical port, ROML/ROMH writes) |
| 0x101A5018 | SH_LIST | R | b7:0 entries in the list now, b31:16 entries a full list dropped |
| 0x101A501C | SH_COMMIT | R | b15:0 bus cycles the last list commit wrote |

`trx_io.v` CAPS2 (0x101A0005) b1 = this build (commit list, look, PROTO/SIDEFX, show bit). The
app sets PROTO only where CAPS2 b3 is set; on an older bitstream it behaves as before.

Q9 is built as far as the responder goes: `SH_CTRL` b2 SIDEFX. Making the debugger's own pause
(T21, `dbg_hold`) a session is §14.8 (2026-10-04).

### 14.6 Tests

Simulation (Verilator, niced, one at a time):

- `sim/statebus` (T12 §8.1 tests 1-7, 10, and new test 8): test 4 now plays the app's PROTO
  writes and the TX_CTRL show bit: the menu's writes are listed (2 entries: $DC00 and $D018),
  the commit at the end writes 0 bus cycles, the CRC over RAM, state bus and colour RAM equal
  before and after, 5 frames later equal to a run without the freeze, PROTO cleared at the end;
  test 8: a session writes $D020 (differs), $D021 (same), $D012, timer A latch $1234 then CRA
  $11 (LOAD), SID $D418, REU $DF02; the list holds 8 entries, the reads show $D020 and the
  raster; at the end the machine stays stopped while the commit writes 7 cycles ($D021 left out),
  then $D020 is set, timer A runs from $1234 (latch before LOAD: order kept), the next session
  reads $DF02 = $5A through the REU look; SIDEFX makes a read one bus cycle; b9 drops the list.
  All pass.
- `sim/cart` (`carts.py`, every cartridge type with `tb_cart --peek`): at the end the machine is
  paused, 18 addresses are looked at (ROML $8000-$9FFF, ROMH $E000-$FFFF, IO1, IO2) and then
  read with bus DMA cycles: every look equals the bus read where something answers (14-18 of 18
  per type; the rest open bus, the look $FF), Action Replay also while active; no cache fetch
  for the looks. All 12 cases (11 types, Action Replay twice) pass.
- benches touched by the port lists, rerun: `sim/pause`, `sim/dma_io`, `sim/ring`, `sim/uci`,
  `sim/input`, `sim/cart_unit`, `sim/trx_io` (CAPS2 now 0x03) pass; `sim/dbg` fails the same
  32 checks on origin/main as on this branch (its boot already fails there), not touched here.

Board, test 7 automated (`tests/freeze_raster/`, run.sh): `freeze_raster.asm` (64tass) runs five
colour splits per frame from a double-IRQ stable raster and logs per split (line, cycle in the
frame from CIA 1 timer A with a one-frame period, its frame counter, the machine's frame counter
from CIA 2 timer B counting CIA 2 timer A) into a ring at $4000; `freeze_raster.py` loads it over
JTAG, opens and closes the app's freeze menu with REST `machine:menu_button` N times and checks
the log across each freeze (same line and cycle per split, one record per split, both frame
counters exactly one frame on across the freeze), the session counters (no C64 clock edge, no bus
cycle in the menu, the commit wrote nothing) and the UDP video stream (a frame after each resume
equals the one before the first freeze pixel for pixel, the splits' positions printed). Results:
§14.6.1.

### 14.7 Open

- Debugger pause as a session (Q9): designed in §14.8 (2026-10-04; was open here and wrongly
  deferred to T55).
- The look does not show the AMD responder's ID/status modes or the UCI's registers (§14.3).
- LUT estimate (yosys `synth_xilinx`, not Vivado): freeze_resp 785 -> 1,191 LUT + 48 -> 104 LUT
  as RAM, 530 -> 756 FF; cart_fill +71, trx_sys +26, cart_slot with the model +63, c64_reu +19,
  c64_text +5: about +590 LUT logic, +56 LUT RAM, +250 FF before Vivado's packing. Vivado's
  numbers: §14.6.1.

### 14.8 The debugger's stop is a freeze (design, Alex 2026-10-04; answers Q9)

> A breakpoint stop IS a freeze, only under exactly deterministic conditions.

One stop mechanism. The gate already is the same code for the app's freeze and the debugger's
hold (`c64_core.v`: `run`, `ce`, `halted_r`, whole frames left out, `RESUME_PHASE`). What differs
is only the session, and that is removed:

- **Start.** `freeze_resp` gets `dbg_stop` (trx_dbg: `hold && halted_r && !stepping`, the gate is
  shut by the debugger and no step runs; as built, see below). The session starts when `sh_mode != 0 && dbg_stop` and
  none runs, a commit does not run and `block` is clear: the same `start` as the freeze, one more
  cause. `S_INIT` reads the frozen CPU port and cartridge lines from the state bus as it does for
  a freeze (the shadows hold the stop from one clock after it on). SH_MODE 0 keeps the old
  behaviour (no session).
- **Inside.** The same responder: window bytes with `dma_bus = 1` are answered from the
  shadows without a bus cycle (VIC, CIA 1/2, colour RAM, ROML/ROMH/IO1/IO2 from the cartridge
  look, SID reads $FF as in a freeze, RAM and ROM through the second port); writes go into the
  overlay and the commit list (`proto` is not set: the debugger puts nothing back); the lens
  unit is unchanged. `SH_CTRL` b2 SIDEFX makes I/O and cartridge accesses real bus cycles, as
  before.
- **End.** The session ends, with the list committed as bus cycles while `hold` keeps the machine
  stopped, **before** the debugger lets the machine go: `RUN` (page 3 CMD 3), `STEP_INSTR` (5) and
  `STEP_CYCLES` (6) are taken by `trx_dbg` as requests (`rel_req`); it releases `hold` or opens the
  gate (`go`) only when `freeze_resp` reports no session and no commit (`sess_busy` low). The
  responder ends the session when `rel_req` is set (`ending` has the cause next to `!keep`);
  `keep` for a debugger session is `dbg_hold && !rel_req`.
- **Next.** The stop that ends a run or a step (`brk`) closes the gate on its edge; `dbg_stop`
  rises again and the next session starts. A step is therefore: commit, step, session.
- **Aborts.** `sh_abort`, SH_MODE written 0 and `sh_discard` drop the list as for a freeze. A
  firmware freeze while the debugger holds (the menu button): the debugger's OWNER makes the
  app's `C64_STOP` not stop the machine (top.v), so there is no second session; the open
  question of the menu over a debugger stop stays T21's (the loader pauses with the debugger's
  hold, the app is `trx->acquire`d).
- **Registers.** `SH_STATUS` b2: the session was started by the debugger (b0 is the session).
  CAPS2 stays b3 (this build).
- **Tests (sim, Verilator, niced, one at a time):** `sim/dbgsess` (CI tier 1), 237 checks: (1) a stop of
  each kind (exec bitmap hit, PAUSE_CB, PAUSE_IB, a step's end) starts a session (SH_STATUS b0 and
  b2); a window read of every byte $D000-$DFFF in it equals the read in an app freeze (`C64_STOP`
  with Ultimax, mode 1) of the same machine on the same clock edge, `sh_real` unchanged, the
  window costs no machine cycle; the cartridge look (ROML, ROMH, $A000, IO1, IO2: 25,088 bytes) the
  same for an EasyFlash and a normal 16K cartridge; (2) $DC0D, $DD0D, $D019, $D012 read in the
  stop: the state bus words (CPU, VIC, CIA 1/2) before and after are equal, the flags stay; with
  SH_MODE 0 the same read is a bus cycle that clears the ICR flag and moves the state; (3) a write
  to $D020/$D021 and CIA registers is read back at once and is not in the chip; the step
  commits the five entries first (SH_COMMIT 5, `sh_real` +5; timer A loaded from the latch written
  before its LOAD) and only then runs its one instruction; exactly one new session for the step's
  end; SIDEFX makes each read a bus cycle that clears the flag; STATUS b6 is seen after RUN
  (hold off, gate shut); abort and discard, SH_MODE 0 in a session, the next stop a session again;
  (4) resume: a raster-IRQ program (two IRQs per frame, a log of {raster line, CIA timer} per IRQ in
  RAM), 14 breakpoint stops with reads, an identity write and `RUN`, and the same with a step chain
  (3 x STEP_INSTR, STEP_CYCLES 3) at each of them (70 sessions), against the run never stopped,
  all ending on the same CYC_CMP stop: CYCLES, INSTRS, CPU and chip state words, RAM and the IRQ
  log entry by entry, and a CRC over every pixel of every C64 clock edge are equal; (5) SH_MODE 0:
  no session, each window read a bus cycle; (7) latency of the release.
- **As built (2026-10-04, branch t21-p4).** Files and signals: `freeze_resp.v` takes `dbg_stop`,
  `dbg_hold`, `dbg_rel` and gives `sess_by_dbg` (SH_STATUS b2) and `sess_busy`; `trx_dbg.v` takes
  `sess_busy` and gives `stop`, `rel` (also STATUS b12; b7 stays set from a step's request until it
  ends); `c64_core.v` passes `dbg_hold`, `dbg_stop`, `dbg_rel` out and `sess_busy` in; `top.v` wires
  them; `trx_sys.v` has the SH_STATUS bit. Deviations from the draft above, and why:
  1. `dbg_stop` is `hold && halted_r && !stepping`, **not** `!ce`: the session's own bus cycles
     (commit, SIDEFX reads) pulse `ce`, and the stop must not end with them (it would end the
     session, or restart a new one, in the middle of a commit). For the same reason `freeze_resp`
     also takes `dbg_hold` for `keep`.
  2. `keep` of a debugger session needs `sh_mode != 0` as well: writing SH_MODE 0 in a session ends
     it and drops the list, as it does for a freeze.
  3. `block`: set at **every** end of a debugger session (the draft set it only after a commit or an
     abort) and cleared when `dbg_stop` is low. With an empty list the session ended, `block` stayed
     clear, `dbg_stop` was still true until trx_dbg let go, and a second session started.
  4. `stop_new` is cleared when RUN or a step is **requested** (as before for the app's polling of
     STATUS), not when it is taken; STATUS b7 includes the requested step; the new b12 is the
     request. ARG is read when the step starts, not when it is requested.
  5. `sess_busy` is `sess || commit || start`, combinational and without the `start`'s own
     conditions where they are redundant (about 3 LUT levels into trx_dbg's enables; a registered
     version would leave a window in which trx_dbg releases the machine one clock before a session
     starts). The session start needs no extra clock: `dbg_stop` rises on the clock after the edge
     that closed the gate, `S_INIT` reads the CPU port and cartridge lines in its clocks 4 and 5.
  6. A step after a commit with entries has FLAGS b2 ("released a CPU held by bus DMA") set: the
     commit's bus cycles set `held`, as a window read in a stop did before.
  7. SH_MODE 0 -> 1 inside a stop starts no session until the next stop (`block`).
  8. The app's own `C64_STOP` with Ultimax while the debugger holds is the same session (it was
     already started by the debugger's stop; `start` does not repeat): the app's C64_STOP does not
     stop the machine with OWNER set (top.v), but the responder looks at the raw `C64_STOP`, so the
     session ends with the debugger's RUN. Not simulated.
  Latency of RUN or a step: 2 clocks with an empty list; each committed entry one bus cycle,
  40 clocks (measured: 0 / 5 / 20 entries = 2 / 234 / 834 clocks; 128 entries at most = about 5,200
  clocks = 170 us). Cost (yosys `synth_xilinx`, not Vivado): freeze_resp +30 LUT, +1 FF; trx_dbg
  +9 LUT, +3 FF; about +40 LUT, +4 FF together; Vivado's numbers: CI run of this branch. Not
  tested in sim: the other 9 cartridge types through a debugger stop (the look code does not know
  the cause of the session; `sim/cart` compares the look with bus reads for all 17), the reset
  commit inside a debugger session, mode 2 with a debugger stop, the drive's own stop (`drv_dbg`),
  point 8, timing in Vivado.
  9. **The app's window reaches the session (found 2026-10-05, branch t21-p4).** Until then
     `ultimate_sys.vhd` opened $D000-$DFFF to the C64 side only with `dma_memonly = '1' or c64_stop = '1'`,
     else `dma_stub` (reads $FF, writes acked and dropped), and set `dma_bus` only from `c64_stop`.
     A debugger stop does not set `C64_STOP` (a stop reopened by `trx->ui_request` least of all), so in
     a monitor stop colour RAM read $FF and `wr d020` was dropped; `sim/dbgsess` passed because it drives
     `dma_*` into `freeze_resp` directly. **Rule: while a debugger stop holds the machine (`dbg_stop`),
     the app's D-page accesses behave exactly as under `C64_STOP`:** reads are the session's shadow view
     (colour RAM included, no bus cycle; `sh_mode` 0: a bus cycle on the halted machine), writes go to
     the session's overlay and commit list (colour RAM at once) and reach the chips when the machine
     resumes; `$D400-$D7FF` reads stay $FF (no SID), `C64_DMA_MEMONLY` still means the RAM under the I/O.
     Built: `ultimate_sys` gets the input `dbg_stop` (trx_dbg's, synchronized into its clock) and ORs it
     with `c64_stop` in the D-page condition and in `dma_bus` **for the D page only** (RAM and the ROM
     apertures keep their old path in a debugger stop: direct through the second port). `dbg_stop` is
     steady during the session's own bus cycles (deviation 1), so it does not flicker under a window
     access. `trx_dbg` STATUS b1 shows the same signal (`ST_DBG_STOP`) so the app knows the window is
     open. G06 (D page = a bus cycle while running unless MEMONLY) replaces the D-page condition by
     `not memonly` and the D-page `dma_bus` by `not memonly`: a debugger stop is then covered by it, the
     `dbg_stop` terms drop out, and a halted machine never gets a cycle steal because `dma_bus = 1` with
     a session reaches `freeze_resp`'s responder (`sess_now`), without one a plain bus cycle on the held
     machine. Test: `sim/ultsys` (CI tier 2; the real `ultimate_sys` by ghdl synth, the RISC-V, USB host and
     drive unit bench stand-ins, 100 MHz domain clocked only during an app access; boot 13 min, tests 1 min), 49
     checks: (1) a debugger stop without `C64_STOP`: the 3,072 D-page bytes the window passes ($D400-$D7FF is
     $FF by design) equal an app freeze's on the same edge, colour RAM included, 0 bus cycles; (2) $DC0D, $D019
     leave the state words as they were; (3) writes to $D020/$D021/$D800/$DBE7 read back at once, 2 list
     entries, chip written by the step, colour RAM kept, RUN goes on; (4) SH_MODE 0: the same reads are bus
     cycles that clear the ICR flag, a write reaches the chip; MEMONLY reads the RAM under the I/O. Control
     run with `dbg_stop` hidden from `ultimate_sys` (the old behaviour): 18 of them FAIL (all $FF, writes
     dropped).
  10. **A foreground app's `C64_STOP` no longer takes the session away (found 2026-10-05 by the UE2
     session, branch t21-p4).** `block` cleared on `!app_stop && !halt_req && !dbg_stop && !commit`, with
     `app_stop` the raw `C64_STOP` (top.v does not mask it for `freeze_resp`; it masks it for the gate and
     `halt_req` with OWNER). The loader holds `C64_STOP` for the whole run of a foreground app (trxmon,
     `app_loader.cc`) and across the overlay menu, and every end of a debugger session sets `block`
     (point 3): `block` was then never cleared again, so only the first stop got a session; later stops had
     `SH_STATUS` b0 = 0, colour RAM and the session reads answered `--` (no shadow), and so did the stop right
     after `x`. **Rule: while the debugger owns the machine (OWNER) the app's `C64_STOP` does not count for
     `block`:** it clears on `(!app_stop || dbg_own) && !halt_req && !dbg_stop && !commit`. Built: `freeze_resp`
     gets the input `dbg_own` (top.v: `dbg_own`, the OWNER bit of `trx_dbg`); the benches without a debugger tie it 0.
     An app freeze with no OWNER is unchanged (the term is the old one). Not changed: `cond`/`keep_app` still look
     at the raw `C64_STOP`, so an app's Ultimax freeze while OWNER is set is still the app's session (point 8).
     Test: `sim/dbgsess` section 8 (CI tier 1): OWNER on and `C64_STOP` held the whole time (the loader's hold, the
     machine keeps running), 6 stops of an exec bitmap hit, each right after a `RUN`, plus a step's end in the
     chain: every one has `SH_STATUS` b0 and b2, `SH_REAL` unchanged, and the 1000 cells of colour RAM read as the
     shadow has them. Control run with `dbg_own` tied 0 in the bench (the old behaviour): the first stop passes,
     the other 6 have no session (12 checks FAIL). The existing sections stay at their 237 checks.
- **Board test list:** (1) bk at a raster IRQ handler's first instruction, 10 stops with `RUN`,
  `tests/freeze_raster` log across them (same line and cycle per IRQ, no missed IRQ, 0 pixels
  different), once with a step chain at each stop; (2) `io d000`, `io dc00`, `io de00` (EF) in
  the stop equal the same in the freeze menu's monitor; (3) `m io dc0d` and `m io d019` repeated
  do not clear the flags (state words equal before and after; `rb`-style read-twice shows the
  flag both times); `sidefx on` then `m io dc0d` twice clears it; (4) `wr d020 05` then `z` shows
  the border changed after the step, `wr dc04 34`, `wr dc05 12`, `wr dc0e 11`, `z`: timer A
  counts down from $1234; (5) the stop at a bk while the app's menu button is pressed (point 8);
  (6) STATUS: b12 is seen as 0 again after RUN and the gate opens within one frame; (7) the T21
  screen's bk stop (T21 §8.5) with SH_MODE 1 and with SH_MODE 0 (old behaviour).

### 14.6.1 Board result (2026-10-03, t12-session run 304, t12-app)

`tests/freeze_raster/run.sh --freezes 10`: 10 of 10 freezes without a deviation. Steady state 5 splits per frame
(lines 58/98/138/178/218); per freeze: halted, a session, menu 2.0 s, commit list 4 entries, 0 commit cycles; 363-382
IRQ records over 63-66 frames checked — same line and cycle, no missed or extra IRQ; UDP picture after resume 0 pixels
different, splits at rows 43/83/123/163/203, x 9. Merged with the session bit moved to CAPS2 b3 (b1 = T15 BUILD,
b2 = T21 CPU write path).

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

T12: Freeze: full-core pause, hardware menu text, cartridge peek

Milestone M1, status ✅. Open (one line, as in the TODO table): Freeze without HDMI -> T35

Decisions: [ADR-0024 SLIM: one state path, a lean write log (replaces the freeze session)](../adr/0024-slim-state-path.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- Task text: Freeze: full-core pause, shadow I/O session, hardware menu text, commit list for I/O writes (Q1, PROTO for Gideon's own freeze protocol), cartridge peek without bus cycles (Q2; physical port reads $FF unless SIDEFX), explicit show bit (Q3), CAPS2 b3. Board 2026-10-03: raster freeze test 10/10 without a deviation (0 pixels, same IRQ cycles).
- **Superseded by SLIM 2026-10-05 (in main 2026-10-06, spec banner in T12-freeze-overlay.md): the shadow I/O session, the commit list (Q1), the debugger stop as a session (Q9, §14.8) and the freeze responder are gone; a freeze is a plain machine pause with zero machine cycles, app accesses are bus cycles. Board 2026-10-06 (SLIM core): freeze menu incl. joystick passes.** Freeze without HDMI -> T35
