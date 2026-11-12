# T50 — Flash unlock for updates (T+R+X at power-on)

**Status:** spec 2026-10-03, docs only. **Task:** TODO.md T50 (M1; moved up from M3 for T49).
**Base:** `docs/specs/T44-flash-write-gate.md` §8a (rules by Alex, 2026-10-03). **User:** `T49-update-packages.md`.
All hardware facts below are read from `designs/c64_basic/` (file:line), not from comments.

## 1. Goal

The T44 gate blocks program and erase below 0x400000 for everything that runs on our bitstream. A tester
without JTAG must still be able to replace the bitstream (0x000000-0x21FFFF) and the app (0x220000-0x3FFFFF). The
authorisation is a person at the machine: the three keys **T, R, X** held on the machine's own keyboard right after
power-on. While open, the gate lets program/erase opcodes through for the whole 3-byte range; everything else
stays as in T44. Nothing in software can open it.

## 2. What the code says (and what changes against T44 §8a)

1. **Which keyboard.** T44 §8a assumes the C64U's keyboard is read over I2C by `c64u_keyboard`. The code says
   otherwise: `c64u_keyboard` (`top.v` 729) scans a PCA9555 expander "behind P7 (an original C64 keyboard)" (`top.v` 723);
   the C64U's own keyboard is the Bling board behind J8, a one-way UART at 250 kbaud on `BLING_KEYB`
   (`bling_keyboard.v` 1-30, instance `top.v` 843). Both end up OR-ed in `keys_s1 <= kb_keys | bl_keys` (`top.v` 745);
   `keys_s2` is the physical state, the app's injected keys (T38) never enter it (`top.v` 974).
2. **Which source may open the gate.** `bl_keys` only, tapped at `bkb`'s output, before line 745. Nothing software
   controls reaches it: `BLING_KEYB` is an input pin, the app's only output to that board is the LED chain
   (`BLING_LED`, `bling_io`). The I2C source is **not** used: the app can own those buses (`ai2c_own`, `app_i2c.v`; the lines
   in `top.v` 695-698) and JTAG can override them (`i2c_ovr`, `top.v` 691), so a program could imitate the expander.
   Consequently T44 §8a's "refused if `ai2c_own` or the override was active" is not needed for this source. A U64-II
   with a classic keyboard on P7 has no unlock (not a M1 target, Q5).
3. **Events, not scans.** The Bling controller sends key-down/key-up bytes, no periodic frames (`bling_keyboard.v`
   32-60: 0x00-0x3F down, 0x80-0xBF up, 0x55 all up). "Seen down in every scan of a window starting at the first complete
   scan" (§8a rule 2) cannot be done literally. What the FPGA has is the state `down[]` built from the events. Whether the
   keyboard controller reports keys that were already held when the FPGA started listening is **not known** (Gideon's
   `bling_board.cc` only reads events, 175-178). It decides between two variants (§3, Q1) and is measured first (§9, E0).
4. **Matrix.** `keys[col*8 + row]`, column = CIA 1 port A bit, row = port B bit (`bling_keyboard.v` 28-29); the C64's
   column PA2 holds 5, R, D, 6, C, F, T, X for rows PB0-7: R = bit 17, T = bit 22, X = bit 23, mask `0x00C2_0000`. Three
   keys in one column cannot ghost (ghosting needs two rows in two columns).
5. **Reset.** The RISC-V reset is `~clk200_locked | machine_ctl[2]` (`top.v` 322): only JTAG (register 9) or a lost PLL
   can cause it, the app cannot. The gate resets with it (T44 §11a). The unlock state must **not**: it is reset only by
   the power-on counter (`por_rst`, `top.v` 130-133), one event per configuration.
6. **Clocks.** `sys_clk` is 31.5278 MHz PAL dot4x (`top.v` 135-142) and T40 may change it; the unlock timers run on
   `clk50` (RMII reference, fixed 50 MHz, `top.v` 128-133).

## 3. Rules (T44 §8a, as implemented)

1. **Physical keys only:** the chord is evaluated on `bl_keys` (§2.2).
2. **Only right after power-on.** A window of **W = 16 s** runs from the end of `por_rst`; after it, no chord opens anything,
   until the next configuration. Inside the window the chord must be down **continuously for H = 1 s**; one event that
   breaks it (any other key down, any of the three up) restarts the hold. Keys pressed *before* the first byte count if the
   controller reports them (variant A), keys pressed *during* the window count as well (variant B); both are the same logic.
   W is long because the machine takes seconds to configure and the Bling controller's first byte comes at an unknown
   time (§9 E0); if E0 shows variant A works from the first byte, W drops to 4 s (Q2).
3. **Exact chord:** `bl_keys & ~(1<<15) == 0x00C2_0000`. Bit 15 is the SHIFT LOCK latch (`bling_keyboard.v` 37), ignored; any
   other key down breaks the chord, so pressing a handful of keys never opens the gate.
4. **Sticky until the next configuration (power-off or a JTAG load), or an explicit relock.** The relock is one write
   (register 0x17 bit 0, §5), which can only close; the T49 updater writes it when it finishes or fails. No timeout in the
   default design (Q3).
5. **Visible** while open (§6): a red frame around the picture and blinking case LEDs, both driven by hardware, not by the app.
6. **What stays blocked even when open:** chip erase (0xC7/0x60), status/lock register writes (0x01/0x31/0x11, 0x50), security
   registers, 4-byte opcodes and everything outside T44 §4's pass/checked lists. Only the compare of the *checked* program/erase
   opcodes changes: A23..A16 ≥ 0x40 becomes ≥ 0x00.
7. **JTAG:** not a guard. Whoever holds the cable can write the flash directly (T44 §1 non-goals); JTAG activity neither opens nor
   closes the gate (Q9).

## 4. Hardware

### 4.1 New module `designs/c64_basic/flash_unlock.v` (clk50 domain)

```
module flash_unlock #(
    parameter [31:0] W = 32'd800_000_000,    // 16 s at 50 MHz
    parameter [31:0] H = 32'd50_000_000 )    // 1 s
 ( input clk, input rst,                     // clk50, por_rst
   input chord_s,                            // chord, synchronised into clk (2 FF)
   input relock_tgl_s,                       // toggle from the gate's register write, synchronised
   output reg armed, output reg open, output reg expired, output chord_now );
```

State: `t` (30 bit) counts up while `armed`; `h` (26 bit) counts while `chord_s` and clears when it is low; `open` sets when `h == H`
and `armed`; `armed` clears on `open`, on `t == W`, or on a relock toggle; `expired` = armed ended without open. `open` clears
**only** on `rst` or on the relock toggle. Once `armed` is 0 nothing sets `open` again (no re-arm without `rst`).

In `top.v`, after the `bkb` instance (843):

- `wire chord_sys = ((bl_keys & ~64'h8000) == 64'h0000_0000_00C2_0000);` registered in `sys_clk`, then two flip-flops into `clk50`.
  The 64-bit compare is the only wide logic (about 12 LUT).
- `flash_unlock fu (.clk(clk50), .rst(por_rst), …)`.
- Outputs to three consumers, each through its own two-flip-flop synchroniser (`ASYNC_REG`, as `keys_s1/s2`):
  `cpu_clk` (the gate: `open` as the `unlock` input, `armed/expired/chord_now` for the status), `pix_clk` (the frame), `clk50` (LEDs, no
  synchroniser needed).
- The gate's relock write is a toggle in `cpu_clk`, synchronised into `clk50` (one flip-flop pair).

### 4.2 Gate changes (`flash_gate.vhd`, `ultimate_sys.vhd`)

- New ports on `flash_gate`: `unlock : in std_logic`, `unlock_status : in std_logic_vector(3 downto 0)` (armed, chord, expired, spare),
  `relock_tgl : out std_logic`.
- The two compares of the checked opcodes become `unlock = '1' or unsigned(ob) >= g_low_limit` (lines 277 and 283; the 4-byte path stays
  blocked by `g_4byte` false). `g_wren_pass` is true in the release build and stays.
- Register 0x17 (§5) in the register decode (`case` at 202-209: today `when others => X"00"`); write of 1 to bit 0 toggles `relock_tgl`.
  Register 0x10 (signature `0101`, bits 3..0) is unchanged.
- `ultimate_sys.vhd`: ports `fl_unlock`, `fl_unlock_status`, `fl_relock_tgl` next to the gate instance (693-708), wired in `top.v`.
  The gate's own `reset` (cpu_rst) does not touch `unlock`: it is an input, the state is in `flash_unlock`.

### 4.3 Cost

| part | LUT | FF |
|---|---:|---:|
| chord compare + 2 FF into clk50 | 12 | 2 |
| `flash_unlock`: t (30), h (26), 4 state bits, relock edge | 45 | 62 |
| three synchronisers (open and status) | 2 | 10 |
| gate: two compares, register 0x17, toggle | 8 | 2 |
| frame over the final `rgb` (`top.v`: `c64_text` output into `hdmi_enc`, 1288): 4 compares of `cx/cy`, 24 mux LUT | 40 | 3 |
| case LED override | 4 | 0 |
| **total** | **about 110** | **about 80** |

No BRAM, no DSP. `docs/lut-budget.md`: 25,346 LUT at CI run 275, T44's gate 239 (yosys) on top, the C64U's 26,080 (T25) and
82 % for T29. 110 LUT is small against the headroom, but the free LUT count has to be taken from the
newest CI utilization report before the build (it moved since run 275).

## 5. Status register (gate window, offset 0x17, aliases every 32 bytes like 0x10-0x16)

| bit | read | write |
|---|---|---|
| 0 | **OPEN**: program/erase below 0x400000 allowed | 1 = relock (closes; cannot open) |
| 1 | ARMED: window running, not yet open | – |
| 2 | CHORD: T+R+X (exact) down now | – |
| 3 | EXPIRED: the window ended without an unlock | – |
| 7 | 1 = this bitstream has the unlock logic (bits 6..4 read 0) | – |

A gate without T50 reads 0 at 0x17 (T44 §11 item 5), so bit 7 tells the T49 updater whether "locked" can ever become "open". The register is
visible to the app, to the T49 updater and to the JTAG helper of `flashgate_test.py`; it is read-only except the relock.

## 6. Visible indication

Driven by `open` and `chord_now`, no software in the path:

- **Red frame:** while OPEN, 16 output pixels at the edge of the 1920x1080 area (`cx` 768..2687, `cy` 45..1124 per the `hdmi` generics, `top.v` 1283)
  are forced to 0xFF0000, on top of everything, including the freeze menu.
- **Case LEDs** (`top.v` 1376-1377): OPEN blinks LED 1 and 2 alternately from `blink[24]` (`top.v` 1357); while the chord is being held
  (counting the second) both are on steady; otherwise as today. They work before the picture exists.
- The T49 updater prints "Flash: UNLOCKED" or "LOCKED" (§7).

## 7. How the T49 updater uses it

On our core (`TRX_ID` "TRX2") it reads register 0x10 (signature) and 0x17: bit 7 clear → cannot update itself; bit 0 clear → "Flash is locked. Switch
off, hold T+R+X while switching on" and turn off before any write (T49 §4.1 step 2); bit 0 set → write; at the end (success, failure or user NO) it
writes 1 to bit 0 of 0x17. The unlock outlives the app's boot and the jump into the updater because nothing there resets it (§2.5). On Gideon's core
there is no gate and no register.

Gideon's own updater run from our menu (T49 §6.2 "way back") works while open and needs no change.

## 8. Tests

**Simulation** (`designs/c64_basic/sim/flash_unlock/run.sh`, GHDL for the gate, iverilog/GHDL as the existing benches; W and H as small generics):

1. `bling_keyboard` driven by a UART model with the byte codes of T, R, X (T = column 2 row 6 → keyboard code per `c64_bit`), held H: OPEN
   within H after the last key-down; the chord broken at H-1 and restored restarts the hold.
2. Negatives (no OPEN, status as expected): chord plus one more key; two of three; chord after W; chord held from before power with
   no events (variant B case, then the same with the down events at t = 0 for variant A); SHIFT LOCK latched (ignored, still opens);
   0x55 all-up in the middle.
3. **Forgery:** the CPU side injects the full chord through `MATRIX_KEYB` (T38 `app_keys`), drives `ai2c_*`/`i2c_ovr` patterns and the JTAG
   register writes: OPEN stays 0.
4. Relock: OPEN, write 1 to register 0x17 → 0; a second chord inside the old window does not reopen (`armed` is gone); reset (`por_rst`)
   re-arms.
5. **Gate with the T44 bench** (`sim/flash_gate`, its three flash models and the random streams): with OPEN = 0 the T44 results unchanged; with
   OPEN = 1 program/erase at 0x000000, 0x21F000, 0x3FF000 are executed, chip erase, status writes, 4-byte opcodes, QPI and the I/O reads
   are still substituted; the property run adds "no program/erase below 0x40 is executed in a run in which OPEN was never 1".
6. T49's byte sequences (T49 §8) run through the gate with OPEN 0 and 1.
7. A cpu_rst (machine_ctl[2] write) while OPEN: OPEN unchanged, gate state reset.

**Board.** Rule from T44 §8 stays: a command that can destroy something is only sent where the destroyed data is already dead. Scratch sectors
for every stage: **0x3FF000** (last 4K of the app slot; the app ends at 0x3A4160, 376,480 bytes of tail) and **0x21F000** (last 4K of the
bitstream slot; our stream ends at 0x1A5C04, Gideon's old stream ended at 0x217300). Both are read first and must be all 0xFF (otherwise the
script takes the next all-0xFF sector of the same tail); the A23..A16 values 0x3F and 0x21 test the `ob < 0x40` compare at the edge and well
below. Writes only with the T44 stage-2 script's discipline (list built and checked first, WRDI + WEL = 0 check, one opcode per WREN). SRAM-loaded
bitstream throughout (`tools/load.sh`), flash untouched except the scratch sectors, full dump taken before.

- **E0 (read-only, today's flashed core, answers Q1):** power the machine off, JTAG USB unplugged (power-on with the blaster attached stops the
  configuration), hold T+R+X, switch on, keep them down to the menu, then attach JTAG and read the Bling edge recorder (`top.v` 823-842; register 11 count,
  `0x300000`+n, `tools/bling_uart.py`): does the first received byte after configuration contain the down codes of T, R, X, and at what `bk_time`?
  Repeat once pressing after power-on. Nothing is written anywhere.
- **U1 (blocked without the keys):** load the T50 bitstream, do not touch the keyboard, wait 20 s. Helper reads: 0x10 signature, 0x17 = 0x88
  (present, expired). WEL = 0 probes as T44 stage 1 (erase at 0x3FF000, 0x21F000 with WRDI + WEL = 0 first): MISO 40 18, blocked count +1 each.
  Then, **with Alex's OK for exactly this**, one erase 0x20 at 0x3FF000 and one at 0x21F000 with WEL = 1 (T44 `--wel1-probe`, here safe because the
  sectors are dead): blocked, sector still all 0xFF.
- **U2 (opens with the keys):** reload, press T+R+X inside 16 s and hold. Expected, in order: both case LEDs on steady, after 1 s the red frame and
  blinking LEDs, helper read 0x17 = 0x81 (plus bit 2 while the keys are down). Erase + program (256 pattern bytes) + erase at 0x3FF000 and at 0x21F000, read-back as T44 stage 2,
  allowed count +6, blocked count unchanged. Still blocked (WEL = 0 probes only, nothing destructive on the board): 0xC7, 0x60, 0x01, 0x31, 0x50
  and the 4-byte program: blocked count +1 each, SR1-3 unchanged. Relock write: 0x17 reads 0x80 (not open, not armed, not expired), the frame is gone,
  a WEL = 0 erase probe at 0x3FF000 is blocked again.
- **U3 (negatives on the board):** four separate loads: chord plus the SPACE key; only T+R held; chord first pressed after 20 s; and with the helper
  writing the chord into `MATRIX_KEYB` for 20 s after configuration: 0x17 never shows OPEN and the stage-U1 probe stays blocked.
- **U4 (power-on, flash-booted core, after T49):** power cycle with the keys held (variant A) or pressed (B): indication, app boots, the T49
  updater's dry run (answer NO) prints "Flash: UNLOCKED"; power cycle without keys: "LOCKED". T49 B2/B3 are the real writes.

## 9. Open questions for Alex

1. **Does the Bling controller report keys held at power-on?** Variant A (hold before switching on) works only if it does. E0 reads it from the **Decided (Alex, 2026-10-03): variant A — hold T+R+X, then switch on.** E0 must confirm the controller reports keys held at power-on; if it does not, fall back to B and tell Alex.
   edge recorder, read-only, with you at the machine (JTAG unplugged for the power cycle). If not, variant B ("press within 16 s after switching on")
   is the instruction; the logic is the same.
2. **Window and hold:** W = 16 s, H = 1 s as proposed, or tighter (W = 4 s once E0 shows variant A)? **Follows from A: W = 4 s, H = 1 s** (adjust after E0).
3. **How long may it stay open:** until power-off or relock (proposed; the updater always relocks), or a hardware timeout (30 minutes would cost about 35 FF) so that a **Decided (Alex, 2026-10-03): until the next boot/power-off, or until the updater relocks after the update. No timeout.**
   forgotten open machine closes by itself?
4. **Indication:** red 16-pixel frame plus blinking case LEDs enough, or also a text (the LVGL/overlay path needs the app and is not hardware-only)? **Decided (Alex): additionally the menu shows "(unlocked)" bottom left** while the gate is open (app reads register 0x17 b0); hardware frame/LEDs as proposed.
5. **Machines without the Bling keyboard** (U64-II with a classic keyboard on P7): no unlock in M1 (their way is JTAG). Acceptable, since the testers' machines are C64Us? **Decided (Alex, 2026-10-03): OK for M1. For M2 (T53, UE2) Alex can plug a real C64 keyboard onto the C64U's keyboard header (P7, read by `c64u_keyboard`) to reproduce the classic-keyboard case.**
6. **T44 §8a rule 1 reworded:** its I2C guards (`ai2c_own`, `i2c_ovr`) are replaced by "Bling UART only". OK, and should §8a in T44 say so (a pointer is added there with this spec)? **Done 2026-10-03 (T44 §8a rule 1 rewritten).**
7. **Chip protection (T44 Q4):** stays unset in M1. If BP/TB are ever set, an update would need one SR1 write through the gate, against rule 6. Keep the answer "no SR writes, ever" and
   solve a protected chip by JTAG? **Decided (Alex, 2026-10-03): no SR writes, ever (for now).** Updates do not need them: erase/program in 0x000000-0x3FFFFF is what T+R+X opens; SR writes would only matter if the chip's BP protection were set, and it stays unset (BP = 000 measured).
8. **WEL = 1 probes on dead sectors (U1):** allowed despite T44 §8 ("never with WEL = 1") because 0x3FF000 and 0x21F000 hold nothing? Both read all 0xFF first. Needs your OK for the exact two commands. **Approved (Alex, 2026-10-03): exactly these two probes, only after both sectors read all 0xFF.** (Since the T15 flash: our app ends at 0x3A4160, our bitstream stream at 0x1A5C04 with Gideon's old tail up to about 0x217300 — the test must check both sectors again on the day.)
9. **JTAG while open:** no guard (as proposed), or refuse/close the unlock when `jtag_dbg` was written since configuration? **Decided (Alex, 2026-10-03): no guard** (JTAG can write the flash directly anyway).

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

T50: T+R+X flash unlock for updates

Milestone M3, status ⏸. Open (one line, as in the TODO table): Not needed while the gate is open; protection returns with T44/T50

Decisions: [ADR-0009 Flash write gate open in M1; T44 and T50 out of M1](../adr/0009-flash-gate-open-in-m1.md); [ADR-0004 M1 release candidate for TREX-internal testers](../adr/0004-m1-release-candidate-internal-testers.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- Task text: T44 §8a flash unlock for updates, moved from M3 to M1 for T49: hold T+R+X at power-on (physical keys only, window ~1 s, visible) opens the bitstream/app areas until the next reset; status/lock register writes stay blocked. Spec: docs/specs/T50-update-unlock.md (Bling keyboard UART is the source, not I2C; status register 0x17; E0 experiment first) Out of M1 with T44 (Alex, 2026-10-04): with the gate open no unlock is needed.
- **In main with t44-open (no unlock needed with the gate open).**
