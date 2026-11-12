# T57 — trex_crt (cart dumper / writer / inspector) on both cores

**Status:** spec 2026-10-04; built 2026-10-05 on `t16-complete` / `t16-complete-app` (§12: as built, deviations, assumptions). Board test with Alex's carts open. M1, release-candidate relevant.
Sources read: `apps/trex_crt/src` (`cart_base.c`, `cart_dumper.c`, `cart_writer.c`, `cart_inspector.c`,
`main.c`) at app `029da8ef`; `software/app_loader` (`app_api.h`, `app_api_impl.cc`, `app_loader.cc`);
`software/io/c64/trx_io.h`, `c64.cc` (`ConfigureU64SystemBus`); `designs/c64_basic/trx_io.v`, `trx_sys.v`,
`slot_port.v`; `cores/icepi/c64_top.v`; T16 (§5, §6.1, §8, §13), `T16-board-session.md`, T16.2, T21 §3.4.1,
T33, T44; board facts of 2026-10-04 (stated in the task, not re-measured here).

## 1. Goal and rules

One `trex_crt.u2a`, no second build. At start it works out which core it runs on and serves both:

1. **Gideon's bitstream (stock):** the app does exactly what it does today. The register traffic of the three tools
   on this path must not change by one access (the March 2026 board work on Gideon's core is the reference). Every
   new line is behind `is_trx`.
2. **Our bitstream (TRX2):** dump, inspect, write and verify work for Alex's carts without JTAG and without a
   preference trick, and nothing the app does can leave the cartridge writable or the bus mapping changed.
3. No change on Gideon's side of the firmware ABI: `api->trx == NULL` there, and that is the detection (§3).
4. Firmware stays minimal (Alex): primitives and policy in the firmware, the cartridge logic stays in the app.
   `ROM_WR_EN` is reachable by the app through `trx->regs` already; **no new ABI call is needed** (§4.1).

Non-goals: the lens path (known bug, §9), GMod4, EF3 as an emulated cartridge (T51), cartridge DMA (T16 P6),
protecting cartridges from a game that writes to them (a game on a real C64 can do that too).

## 2. What the code does today (read from the code, not from comments)

| # | Fact | Where |
|---|---|---|
| F1 | The three tools open with `cart_setup_bus()` (ABI: save `C64_BUS_BRIDGE/INTERNAL/EXTERNAL`, `SERVE_CONTROL`, `DMA_MEMONLY`, `$00`, `$01`; then `DMA_MEMONLY = 0`, `SERVE_WHILE_STOPPED`, bridge `3`, internal `0`, external `15`, poke `$00 = $2F`, `$01 = $37`). `cart_restore_bus()` puts them back. | `app_api_impl.cc` `api_c64_cart_setup_bus` / `_restore_bus` |
| F2 | `cart_check_dma_access()` returns false for preference Auto or External with a cartridge on the pins and shows "Set Cartridge Preference to Internal and reboot". It does not look at the core. The dumper and the writer call it; the inspector does not. | `cart_base.c:29`, `cart_dumper.c:548`, `cart_writer.c:1450` |
| F3 | Only the dumper clears the app's Ultimax (`cart_mode(MODE_NORMAL)` and a discarded peek of `$8000`). Writer and inspector do not. On our core `ext_ultimax` keeps ROML/ROMH strobes off the port (`rom_port = rom_sel && !ultimax`, T16 §13.2). | `cart_dumper.c:535`; none in writer/inspector |
| F4 | Nothing in the app touches `0x101A7000` (`SLOT_CTRL`). Every ROML/ROMH write cycle (flash command, program, erase, EAPI) is blocked by `slot_port.v` unless `rom_wr_en`; IO1/IO2 writes (bank registers, `$DE02`, `$DF00`, the GMod2 EEPROM bit-bang) always pass. | `slot_port.v:133-134` |
| F5 | `cart_present` of the PLA is latched **only at a CPU reset** from `flags[11]` (`cart_type != 0` or "the slot claims a resource"); `cart_game/cart_exrom` and the cartridge IRQ/NMI count only while it is 1. Masks written by `cart_setup_bus` while the machine is paused arm the port but do not change `cart_present`. | `cores/icepi/c64_top.v:1412-1418`, `c64_core.v:287` |
| F6 | `C64_POKE(0x01, ..)` is a DMA write to RAM. On our core it does not reach the 6510 port (T16 §13.8: Q8 not built). The PLA the DMA cycle goes through uses the frozen program's `$01`. | T16 §8.3, §13.8 |
| F7 | Detection and dump read through the PLA. The EF dump uses Ultimax (`$DE02 = 5`, ROML `$8000`, ROMH `$E000`) and does not depend on `$01` for ROML/ROMH; I/O (`$DE00`, `$DF00`, `$DE01`) needs `$01` to show I/O in the non-Ultimax states. 8K/16K carts need `$01` with LORAM/HIRAM set for ROML/ROMH. | `cart_switch_bank`, `probe_cart_type` |
| F8 | Bank registers and `$DE02`/`$DF00` are write-only on every cart; the app never restores them. After any tool the running program sees another bank. | `cart_restore_bus` restores only bus registers and `$00/$01` |
| F9 | The writer's F3 is the decision (no second question): it erases the whole chip (EF1, GMod2, MegaByter, C64MegaCart; EF3 only its slot) and then programs. Writes happen inside one blocking `poll()`; there is no key check in the loops, so the only abort paths are error returns, the app's exit, and `ui_run` returning `MENU_EXIT` (firmware-requested exit). | `cart_writer.c:1071`, `ui_shim.c` `ui_run` |
| F10 | The dumper types the CRT by the probe: MegaCart is written as type **19** (Magic Desk's ID), `megacart_info` carries 19 too; T33 settled 88 (C64MegaCart), 89 (TwoMegabyter), 61 (MAX Basic), 87 (GMod4). There is no TwoMegabyter detection, dump type or writer entry. | `cart_dumper.c` STEP 6, `cart_writer.c:24-30` |
| F11 | The app can request a program, a CRT or **a C64 reset** only deferred: `media->reset()` is carried out after `app_main` returned. | `app_api.h` media, `app_api_impl.cc:866-935` |
| F12 | The loader starts an app only while the machine is stopped (the user froze it, or REST stopped it). The stop is a CPU pause; `GATE_CTRL b0` (whole-machine pause) is not set by the app. DMA bytes are single machine cycles through the port (T16 §8.3). | `app_loader.cc:262-275`, T16 §8.3 |
| F13 | `app_trx_cleanup()` runs when the app returns (normal or error) and releases the debug block. It does not know the slot block. | `app_api_impl.cc:364` |

Board facts (2026-10-04) that this spec builds on: Gideon's default DMA `readmem` path (CPU paused) read EF1 Timo's
Castle, 64 banks, byte-exact twice; the lens REAL path drops sporadic bytes to `$00` and crashed a game when read
while running (separate bug, not used here); the plain path sees ROML/ROMH only where the PLA maps them
(MegaByter game with `$01 = $35` reads RAM); flash program/erase and an in-game EAPI save worked with
`ROM_WR_EN = 1` and survived a power cycle.

## 3. Detection (which core, which capabilities)

```
is_trx   = api->trx != NULL
           && api->trx->regs[0..3] == "TRX2"            // ID at 0x101A0000
has_slot = is_trx && (SLOT_STATUS b31 == 1)             // 0x101A7004; page 7 unmapped reads 0
```

- `api->trx` is non-NULL only when the firmware found `TRX2` and CAPS has the debug page
  (`app_trx_table`, `trx_io_caps`). On Gideon's bitstream the block reads 0, so `api->trx == NULL` and the
  app takes the stock path **without reading or writing anything at 0x101A0000**. An old TRX bitstream without
  the debug page also gives NULL; the app then runs the stock path, which on such a core is the old (broken)
  behaviour. App and bitstream ship together (T33 note), so this is not a supported combination.
- `is_trx && !has_slot`: a TRX bitstream without the slot page (before T16 M1): popup "bitstream too old".
- The core is decided once in `app_main` and kept in `g_core` (`CORE_GIDEON`, `CORE_TRX`).
- Page 7 is read like the other pages of the block: byte 0 first, then bytes 1-3 (ARCH §10 rule). `SLOT_CTRL` is a
  32-bit register: a write commits on byte 3, so the app writes bytes 0-3 in order (the first draft said "byte writes
  at offset 0"; corrected 2026-10-05 from `trx_io.v`/`trx_sys.v`).

**Registers used (our core only):**

| Address | Name | Used for |
|---|---|---|
| 0x101A7000 | `SLOT_CTRL` b1 `ROM_WR_EN` (RW, reset 0) | raised only inside a write guard (§5.4) |
| 0x101A7004 | `SLOT_STATUS` (R) | b1:0 pins (1 = high; b0 GAME, b1 EXROM), b2 DMA, b5 RESET sensed, b11:8 EXT mask (IO1 b8, IO2 b9, ROM b10, IRQ b11), b15:12 INT mask, b16 armed, b17 fault, b18 `ROM_WR_EN`, b23:20 fault cause, b31 slot block |
| 0x10100403 | `CART_DETECT` (ABI `cart_detect`) | pins, as today |

Not used: the lens unit (page 5 `LK_*`), `GATE_CTRL`, `SLOT_CNT*`/`SLOT_LAST` (not built, T16 §13.7), JTAG.

## 4. What the firmware and the bitstream have to provide

### 4.1 `ROM_WR_EN`: who sets it (decision proposal)

Three writers, in this order of authority:

1. **Firmware policy** (config item, default per Q1): the bit follows the policy at every C64 reset (the end of
   `ConfigureU64SystemBus`), at every change of the item, and when an app returns. This is what makes in-game
   EAPI saves on a real flash cartridge work with no app running (T16 §9 P4 end).
   - Item **"Cartridge Port Writes"**, next to "Save Changed Cartridge" in the C64 settings (the only place T24-ui-wiring
     §9 G11 has for such items today; moves with T32).
   - `Auto` (proposed default): bit = 1 exactly while the port serves ROM (`EXT.ROM = 1` and `INT.ROM = 0`, i.e.
     what `ConfigureU64SystemBus` just wrote), else 0. That is what Gideon's core does (it has no inhibit).
   - `Writer only`: bit = 0 outside the app's write guard. In-game saves to a real flash cartridge fail then
     (the cartridge never sees the command bytes).
2. **trex_crt**: raises the bit around a write operation the user started with F3 and restores the value it found
   (`wr0`, read from `SLOT_STATUS b18` at start), whatever the policy. Needs no new ABI: `trx->regs`.
3. **Loader backstop:** `app_trx_cleanup()` re-applies the policy (a new `trx_slot_apply_policy()` in the
   firmware, same function as in 1). Covers an app that returns by an error path that forgot to close.

Not covered and accepted (same as T16 risk 9): a firmware crash while the bit is 1 leaves it 1 until the next
bitstream load. A hardware timeout (bit clears after N s without a refresh) is possible but not proposed for M1.

T44's flash gate protects only the C64U's own SPI flash and is a different thing; texts must not call this
"flash lock" (§7).

### 4.2 Firmware changes (all small, none in the ABI table)

| # | Change | Why |
|---|---|---|
| FW1 | `trx_slot_apply_policy()`; call it at the end of `ConfigureU64SystemBus` (our build), from the config change hook, and from `app_trx_cleanup()`. No-op without `has_slot`. | §4.1 |
| FW2 | Config item "Cartridge Port Writes" `Auto / Writer only`. | §4.1 |
| FW3 | The `$01` route for the app (§4.3), if the firmware route is chosen. | §4.3 |

### 4.3 The CPU port (`$00/$01`) during the dump

The plain DMA path goes through the PLA with the frozen program's `$01` (F6). A plain 8K/16K cartridge in a game
that mapped ROML out (`$01 = $35`) reads RAM, and `$DE00` is not visible when the program left I/O off. The app's
`$01 = $37` must reach the 6510 port while paused, and the program's port must come back at the resume.

- **Route A (T16 Q8, decided 2026-10-03, hardware):** DMA writes to `$0000/$0001` while paused go into the 6510
  port; restored through `SB_LOAD` at the resume. Not built (T16 §13.8). Needs a bitstream.
- **Route B (firmware, no bitstream, to be verified):** the 6510 port (`$00/$01` + floating bits) is in state-bus
  chip 1 and chip 1 words are writable through the T20 staging (`SB_DATA`, `SB_LOAD b1`, ARCH §5.4). The firmware
  gets `cart_setup_bus` / `cart_restore_bus` variants that stage the port word and load it, and stage the saved
  word back at restore. Word and bit positions: `trxmon` `hw_cpu_write()` and `ARCH-state-map.md` (generated);
  **not checked in this spec**. Gate condition: "the gate is shut" (`SB_STATUS b0`), which a CPU pause under a
  freeze session satisfies; if it does not under a plain `C64_STOP`, route B needs the freeze session's gate.
- Either route sets `has_port_write`; the app needs no code difference except which ABI entry it calls
  (`cart_setup_bus` itself is changed behind the same ABI entry; no new table field).

Q2 decides. Until one exists, MegaByter/GMod2/ROM carts dump only from the cartridge's boot state (the
`$01` the KERNAL leaves is `$37`), which is the test condition for the first board pass, not the RC condition.

### 4.4 Arming and preference

The port is armed (and `cart_present` latched) at the last C64 reset when the masks then in effect were
`EXT.ROM = 1, INT.ROM = 0` (Auto with a cartridge on the pins, External, or Manual with ROM External). **Default
preference is Auto**, so the normal case needs nothing from the user. Internal (what Gideon's popup asks for) or a
cartridge plugged after the reset leaves `cart_present = 0` (F5): the PLA ignores GAME/EXROM and strobes, and
`cart_setup_bus` cannot fix that while paused.

- **M1 proposal:** the app detects it before `cart_setup_bus` (SLOT_STATUS masks as left by the last reset:
  `b16 armed && b10 && !b14` means latched) and tells the user to set Auto or External and **reset the C64**
  (U3). No hardware change.
- Option B1 (Q4): re-latch `cart_present` while halted, at the same moment the masks are taken (icepi patch in
  `c64_top.v`, one condition on the existing latch; sim test in `sim/slot`: Internal at reset, masks switched while
  paused, pins honoured, restored). Removes U3 for the Internal case; not needed for the default.
- Optional status bit (later): `SLOT_STATUS b19 = cart_present` makes the check exact instead of inferred.

Cartridge off by the running program: a GMod2 in mode `10`, an EF in mode `4` and similar read both pins high
("No cartridge detected") on both cores, as on Gideon's. The text says so (U2); the cure is "reset, freeze at the
cartridge's start".

## 5. Register sequences on our core

All steps `TRX` only; the Gideon path keeps today's sequence (§1 rule 1).

### 5.1 Session start (all three tools)

1. `C64_STOP` is already on (F12). Read `SLOT_STATUS` → `s0`. Fault (`b17`) → U4, no further access.
   Save `wr0 = s0.b18`, `pref = cart_preference()`.
2. Arming check from `s0` (§4.4). Not latched → U3, tool shows the screen without cartridge access.
3. `cart_mode(MODE_NORMAL)` and one discarded `C64_PEEK(0x8000)` (the mode reaches the bus with the next cycle;
   the dumper does this today). **New for writer and inspector** (F3).
4. `cart_setup_bus()` (F1; `$01 = $37` reaches the port by §4.3).
5. Wait until `SLOT_STATUS` shows `b16 && b8 && b9 && b10 && !b14` (EXT IO1/IO2/ROM, INT ROM off), poll up to 10 ms.
   Not reached → U5. (The masks are taken while halted; no reset needed.)
6. `cart_detect_present()` (pins); cross-check `SLOT_STATUS b1:0`, log a mismatch only.
7. Probes and scan unchanged: only IO1/IO2 pokes and ROML/ROMH reads (`$8000`, `$A000`, `$E000`), **no ROML/ROMH write**. The bit
   stays at `wr0` for all of this (the app does not touch it outside the guard).

### 5.2 Dump

Probes, `cart_switch_bank()`, `C64_READ` of ROML/ROMH exactly as now (EF: `$DE02 = 5`, `$DE00 = bank`, ROML `$8000`,
ROMH `$E000`; others 8K/16K/Ultimax per pins). GMod2: EEPROM bit-bang over `$DE00` (IO1: not gated by
`ROM_WR_EN`). `ROM_WR_EN` is never 1 in a dump. Type IDs per F10 (88 for C64MegaCart).

### 5.3 Inspect

As today. Pokes ("write byte", `$01`, `$DE00`) are IO or RAM and can touch the cartridge; `$01` goes to the port by §4.3.
A user poke to ROML/ROMH is blocked by hardware while the bit is 0 (expected, shows no change).

### 5.4 Write guard (`ROM_WR_EN` around writes, guaranteed clear)

One function in `cart_base.c`, used by exactly these callers:

```
slot_wr_open():   TRX only. set SLOT_CTRL b1; read SLOT_STATUS b18 until 1 (<= 1 ms). Fails -> U6, caller aborts
                  before the first ROML/ROMH write.
slot_wr_close():  TRX only, idempotent, runs on every exit. restore b1 to wr0; read back b18 == wr0 (retry 3x,
                  then U7). Sets s_wr_open = false.
```

Callers (the only code regions that write ROML/ROMH): `do_write_crt` (erase + program + EAPI replace, all chips,
EF3 slot erase), `ef3_name_slot` (EF-Directory sector). Not: scan, detect, verify, the GMod2 EEPROM (IO1), the
dumper, the inspector.

Structure: `slot_wr_open(); res = op(w); slot_wr_close(); return res;` in one wrapper
`writer_guarded()`; `op` has no other exit than returning. Backstops: `cart_writer_deinit()` and `app_main`'s end call
`slot_wr_close()` (idempotent), and the firmware's `app_trx_cleanup()` (§4.1).

**Exit paths of the writer and what runs, in order:**

| Path | Sequence |
|---|---|
| Normal end of `do_write_crt` | per-bank `exit_flash_mode` (as now) → flash reset command when the last operation left a chip in command state (EF/MegaByter/MegaCart `$F0` to `$8000`/`$E000`, GMod2 `gmod2_flash_reset`; EF3 `ef3_reset_flash`) → `slot_wr_close()` → `cart_restore_bus()` |
| Error return inside `op` (erase/program/poll timeout, file read error, CRT invalid) | the failing code path already calls `exit_flash_mode`; wrapper adds the flash reset command, then `slot_wr_close()` |
| `ef3_name_slot` failure | same wrapper |
| GMod2 EEPROM step failed after the flash was written | `gmod2_write_eeprom` ends with EWDS on both paths today (`cart_gmod2_eeprom_write_enable(false)`); unchanged, no `ROM_WR_EN` involved |
| User leaves the writer screen (RUN/STOP, ESC) | `cart_writer_deinit` → `cart_ef3_restore_slot` → `slot_wr_close()` (idempotent) → `cart_restore_bus` |
| Firmware-requested exit (`ui_run` returns `MENU_EXIT`) | same as above (returns to `app_main`) |
| App returns by any path | loader `app_trx_cleanup()` → `trx_slot_apply_policy()` |
| Firmware crash / power loss with the bit 1 | cleared by the next bitstream load (reset value 0); with policy Auto the bit is 1 anyway while a cartridge is in the port, so this residual risk equals Gideon's core |

Host test (§8.1) injects a failure at **every** C64 access index of a write run and asserts: no ROML/ROMH write cycle
with the bit 0, and the bit equals `wr0` when the tool returns.

### 5.5 Exit and restore after any tool

```
1. (writer) exit flash mode, flash reset, slot_wr_close()           (§5.4)
2. EF3: cart_ef3_restore_slot()                                      (as now)
3. cart_restore_bus()  -> INT/EXT/BRIDGE/SERVE/DMA_MEMONLY, $00/$01 back (route A: SB_LOAD at resume; route B: staged word)
4. TRX and cart_touched: media->reset()  (deferred, after app_main returns)
```

Step 4 because bank registers are write-only (F8): a running game cannot be put back into its bank, so the
machine restarts through the cartridge's RESET (our C64 reset drives port RESET, T16 §5.3/§13.6; the cartridge
returns to its boot state). `cart_touched` = the app issued any IO1/IO2 access to the port after step 5 of §5.1; a
"No cartridge" or "not armed" exit does not reset. The reset re-runs `ConfigureU64SystemBus`, so arming and the
`ROM_WR_EN` policy are back as the user configured them. On exit the status line says "Resetting C64" (U8). Gideon
path: unchanged, no reset (Q3 asks whether to add it there).

### 5.6 Machine pause: CPU pause vs whole-machine pause

The app keeps the pause it was launched under (F12): CPU pause (`C64_STOP`), the freeze menu's pause, or REST's.
Plain DMA works under all of them (T16 §8.3: each byte opens the gate for one cycle; the first DMA of a pause lets the
6510 run to its next read, writes on the way reach the port like any CPU write). The app does **not** set
`GATE_CTRL b0` (that is the lens unit's requirement, T21 §3.4.1, and the lens is not used). Untested with the real
cartridge: the freeze-menu variant (T16-board-session risk 7); it is test item B3.

## 6. Behaviour matrix

Both columns: what the tool does and what it needs. "Same" = same register traffic and result on both cores.
"R" = needs `ROM_WR_EN` (our core only; Gideon's has no gate). "Boot" = proven only from the cartridge's boot
state until §4.3 exists.

| Cart (Alex's) | Function | Gideon's core (stock, unchanged) | TRX core |
|---|---|---|---|
| **ROM** plain 8K/16K, Ocean, Magic Desk, Super Games, C64GS (C64anabalt `rom16`) | Dump | Preference must be Internal (popup else); DMA through the PLA, `$01 = $37`; type by probe | Preference Auto/External (the default) and a reset since the cartridge was in; `$01 = $37` by §4.3 (Boot until then); then same |
| | Inspect | as today | same, `cart_mode(MODE_NORMAL)` first (new) |
| | Write / Verify | "No supported flash cart found!" | same |
| **EasyFlash 1** (Timo's Castle) | Dump | Ultimax `$DE02 = 5`, `$DE00 = bank`, ROML `$8000`, ROMH `$E000`, 64 banks, type 32 | same; I/O visibility needs `$01` (§4.3); ROM windows do not (Ultimax) |
| | Inspect | as today | same |
| | Write | AM29F040 x2: whole-chip erase, program per byte at bank 0 unlock, EAPI replaced | same sequence; **R** inside the guard (§5.4); ends with flash reset |
| | Verify | read compare | same |
| **EasyFlash 3** (Alex has one; not in the session set, Q7) | Dump | `$DE01` slot detection, EF-Directory, slot pick, dump per slot as type 32 | same. Risk: the EF3 keeps its own clock and PHI2 stops between DMA bytes on our core (T16 §8.1/Q3 = A); untested, trigger for T16 option B if it misbehaves |
| | Write | slot sectors only (16), never a chip erase, directory written | same, **R** for slot sectors and the directory sector |
| | Verify | as today | same |
| **MegaByter** (Lykia) | Dump | 8K mode, `$DE00 = bank` (128 banks), probe `$DE02 = 3` at `$E000`; type 86 | same, **Boot** (ROML visible only with `$01` LORAM/HIRAM = 1) |
| | Write | `$DE02 = 3` Ultimax window, ROML unlock `$8AAA/$8555`, 1 MB | same, **R** |
| | Verify | as today | same |
| **GMod2** (Sam's Journey) | Dump | EEPROM probe over `$DE00`, 64 banks of 8K, 2 KB EEPROM as CHIP packet at `$DE00`, type 60 | same (IO1 bit-bang, no **R**), **Boot** |
| | EEPROM write | EWEN, word write, EWDS, read back | same (IO1, no **R**) |
| | Flash write | `$DE00 = $C0` Ultimax, command cycles through `$E000` | same, **R** |
| | Verify | flash then EEPROM | same |
| **C64MegaCart** (no hardware here) | Dump | probe `$DF00 = $C0`, 256 banks; type written **88** (was 19, F10) | same |
| | Write | `$DF00 = $C0` Ultimax window at `$E000`, M29F160FT, 2 MB; CRT type 88 | same, **R**; host-tested only (Q6) |
| **TwoMegabyter** (no hardware here) | all | not supported (no probe, no type, no writer entry) | not supported; not in the RC (Q6). Untested what the probe makes of it (boots 16K, so likely typed Ocean 16K, type 5) |
| **GMod3 / GMod4** | all | GMod3 type 62 probe as today; GMod4 out of scope | same |

Differences between the columns, all of them: (1) preference rule and message, (2) `$01` route, (3) `ROM_WR_EN`,
(4) `cart_mode(MODE_NORMAL)` also in writer and inspector, (5) reset on exit, (6) PHI2 stops on our core. Common
change to both: CRT type 88 for C64MegaCart (a file-format fix, T33), plus the guarded, TRX-only calls.

## 7. Error and UX texts

The screen is 40 columns; status lines at most 39 characters, popups use `\n` lines of at most 32.

| # | Where | Text | When |
|---|---|---|---|
| U1 | popup, Gideon path | unchanged: "No access to the cartridge.\nSet 'Cartridge Preference'\nto 'Internal' and reboot." | Gideon core, Auto/External with a cartridge |
| U2 | status | "No cartridge detected." and, TRX, popup: "No cartridge on the pins.\nA running program can switch\nit off: reset, then freeze at\nits start." | pins both high |
| U3 | popup + status | "The port is not enabled.\nSet Cartridge Preference to\nAuto or External, reset the\nC64 and freeze again." / status "Port not armed: reset needed" | TRX, masks at last reset not external ROM |
| U4 | popup | "Port fault (cause %d).\nReset the C64." | `SLOT_STATUS b17` |
| U5 | status | "Port did not arm (SLOT_STATUS)." | §5.1 step 5 timeout |
| U6 | popup | "Cannot unlock cartridge writes.\nNothing was written." | `slot_wr_open` failed |
| U7 | popup | "Write lock did not clear!\nPower-cycle before playing." | `slot_wr_close` read-back differs |
| U8 | status | "Resetting C64..." | §5.5 step 4 |
| U9 | popup | "This bitstream has no cartridge\nport support. Update the\nTRX64 bitstream." | `is_trx && !has_slot` |
| U10 | writer, policy Writer only, first F3 | no extra popup; the status line says "Writing (cart writes on)..." | so a user who set Writer only sees the raise |

Everything else keeps its current text. The word "free" is not used for erased cartridge sectors anywhere.

## 8. Test plan

### 8.1 Host harness (no hardware, runs in CI like `apps/trxmon/host`)

`apps/trex_crt/host/`: the app's real C files built for the host against a mock `u2p_api_t` (`c64->peek/poke`,
`dma_read`, `cart_detect`, `cart_preference`, `trx->regs`, `media->reset`, `sys->tick_ms`) and C models of the
carts: ROM16, EF1 (two AM29F040, `$DE00/$DE02`, RAM at `$DF00`), MegaByter (MX29F800), GMod2 (AM29F040 + M93C86),
C64MegaCart (M29F160FT), EF3 (MX29LV640 slots + `$DE01`). The models are the T16 session's behaviour models ported
(`tests/cart/t16_sim.py` is the reference for what they do). The mock enforces the real gates.

| Test | Core mode | Pass |
|---|---|---|
| H1 stock traffic | Gideon (`trx = NULL`) | recorded access list of dump/write/verify/inspect for each cart equals a golden list taken from the unmodified app (`029da8ef`); no access to the slot block |
| H2 dump equals image | TRX | byte-identical to the model's image for every cart, EF banks in shuffled order too |
| H3 write inhibit | TRX | with the mock `ROM_WR_EN` model: zero ROML/ROMH write cycles while the bit is 0; writes in the guard succeed |
| H4 abort injection | TRX | fail the n-th access (n over the whole write run, every cart): on return the bit equals `wr0`, no write outside the guard, no hang |
| H5 arming | TRX | U3/U4/U5/U9 reachable from mock states; "not latched" inferred from masks as in §4.4 |
| H6 restore | TRX | bus registers, `$00/$01` back; `media->reset` requested exactly when `cart_touched` |
| H7 policy | both | with `wr0 = 1` (policy Auto) the bit is 1 again after every tool, with 0 it is 0 |
| H8 detection | both | `trx == NULL` -> stock; `trx` with ID `TRX2` and `b31` -> TRX; `TRX2` without `b31` -> U9 |

### 8.2 Simulation

- `sim/slot`: existing `ef_ro` (inhibit) stays; new `wrguard` (bit 1 only between two writes, a flash sequence
  reaches the cart exactly then, `SLOT_STATUS b18` follows); `late_arm` only if B1 is built (Q4).
- Route A only: `cpuport_dma` (a DMA write to `$01` reaches the port, program's port restored at resume,
  bit-identical resume as `sim/ring restore`).
- Route B: firmware host test of the staging sequence against the T20 model in `sim/dbg` (the CPU write path
  bench), plus the gate condition.

### 8.3 Board (our core; Alex: power, plug, keys; no Gideon-core run, decided 2026-10-04)

Order and conditions follow `T16-board-session.md` (rom16, ef1, megabyter, gmod2, then ef3 if Q7). The scripted part
needs an unattended mode of the app (Q5): `ctx->args` `auto` runs dump or verify without prompts to a fixed path
and returns a JSON reply (type, banks, sha256 of the written file or of the verified image). Without args the UI is
as now. Alex presses the keys in the interactive rounds.

| # | Test | Pass |
|---|---|---|
| B1 | each cart, preference Auto, power-on, boot to the cart's start, freeze, app Dump | file equals the baseline hashes (`tests/cart/data/baseline_candidates.json`; GMod2: two identical dumps + EEPROM round trip) and equals a default-path REST `readmem` dump |
| B2 | same, game running with its own `$01` (MegaByter Lykia, C64anabalt), freeze, Dump | identical to B1 (needs §4.3; if §4.3 is not there yet this is the expected FAIL and says so) |
| B3 | app started from the freeze menu | gives B1's result (§5.6). The REST start is struck (Alex 2026-10-10: "Dump via REST ist App-Sache und kann decoupled gemacht werden"; trex_crt registers no REST action) |
| B4 | preference Internal | U3, nothing read, nothing changed; Auto then reset then B1 passes |
| B5 | exit | after the tool the C64 resets, cartridge boots, picture OK (Alex), `SLOT_STATUS`: masks as before the app, `b18 = wr0`, no fault |
| B6 | EF1 write: test CRT then backup CRT (the `writer` stage of the session), policy Writer only | verify OK both rounds; `b18` is 0 before F3, 0 after; dump equals the written image |
| B7 | same with policy Auto | `b18` is 1 before and after, the write works, the app does not drop it |
| B8 | in-game EAPI save with policy Auto, power cycle, dump | highscore persisted (as the 2026-10-04 PC-side test); with Writer only the save fails and the dump equals the old image |
| B9 | MegaByter write (blank-sector rule of the session), GMod2 flash + EEPROM write | verify OK, EEPROM words as written, `b18` back to `wr0` |
| B10 | error exits: F3 with a CRT the chip rejects (ROMH data for an 8K-only cart, `FR_INVALID_PARAMETER`, returns inside the guard), and RUN/STOP right after F3 (the loops have no key check, so it takes effect after the write) | `SLOT_STATUS b18 == wr0` after exit both times |
| B11 | EF3 (if Q7): slot dump and slot write of one test slot | equals the written image; flag in the report whether PHI2-stopped DMA was enough |
| B12 | Gideon-path regression: not on the board (decided). Covered by H1, and by the unmodified path in the app when `trx == NULL` | — |

Acceptance for "100 %" (M1 RC): H1-H8 green, B1, B3-B9 green on rom16, ef1, megabyter, gmod2, B2 green with the
chosen §4.3 route; EF3 and C64MegaCart/TwoMegabyter as stated in the matrix (Q6, Q7).

## 9. Out of scope, separate bugs

- **Lens REAL path** (`LK_*`): dropped sporadic bytes to `$00`, ~8 per 2 MB even under a full machine pause, and
  continuous reads while running crashed a game. Root cause found 2026-10-05 and fixed in `trx_sys.v`: the byte
  protocol of the IO block was not atomic for several masters (T16 §13.11 item 4). It also affects this app's
  32-bit sequences (`SLOT_CTRL` write, `SLOT_STATUS` read) next to the firmware's tasks; the app reads
  `SLOT_STATUS` twice until two reads agree as a belt and braces. The lens still does not remove the write path's
  needs (`$01`, ROM_WR_EN).
- GMod4 (no hardware), TwoMegabyter dump/write (no hardware; descriptor-based support is M2 with T33 data),
  EF3 emulation (T51), cartridge DMA and SNOOP (T16 P6), option B PHI2-keeps-running (built only if B11 or a
  real cartridge needs it).

## 10. Changes by file (for the implementation, nothing done)

| File | Change |
|---|---|
| `apps/trex_crt/src/cart_base.c/.h` | `g_core`, `core_detect()`, `slot_status()`, `slot_wr_open/close()`, `slot_armed_check()`, `cart_session_begin()` (§5.1 steps 1-6, TRX only), `cart_session_end()` (§5.5); `cart_check_dma_access()` returns true on TRX (Q5 of T16 answered) and keeps today's text on Gideon |
| `cart_dumper.c` | session begin/end; type 88 for MegaCart; `cart_touched` |
| `cart_writer.c` | session begin/end; `cart_mode(MODE_NORMAL)` on TRX; guard around `do_write_crt` and `ef3_name_slot`; flash reset on error paths; `megacart_info` CRT type 88 |
| `cart_inspector.c` | session begin/end; `cart_mode(MODE_NORMAL)` on TRX |
| `main.c` | `core_detect()` once; reset request at the end when `cart_touched`; optional `auto` args (Q5) |
| `software/app_loader/app_api_impl.cc` | `cart_setup_bus/restore_bus` via §4.3 (route B), `app_trx_cleanup()` calls `trx_slot_apply_policy()` |
| `software/io/c64/c64.cc`, `trx_cart.cc` (or a new `trx_slot.cc`) | policy item, `trx_slot_apply_policy()` |
| `designs/c64_basic` | route A only: `cpuport_dma`; B1 only if Q4: `late_arm`; optional `SLOT_STATUS b19` |
| `apps/trex_crt/host/` | harness (§8.1) |
| `README.md` of the app | the preference note differs per core (today only Gideon's is written) |

## 11. Open questions for Alex

One list, each answerable in a word or two.

1. **Default of "Cartridge Port Writes":** `Auto` (bit 1 while the port serves ROM, like Gideon's core, in-game EAPI saves work) or `Writer only` (bit 0 except around the app's writes)?
   **Decided (Alex, 2026-10-04): `Auto`.** Writes are allowed while a real cartridge serves ROM, like Gideon's core; in-game saves just work. `Writer only` stays as the config alternative.
2. **`$01` during the dump:** try route B first (firmware, staging of the 6510 port word, no bitstream, to be verified) and build route A (T16 Q8, bitstream) only if B fails?
   **Decided (Alex, 2026-10-04): route A (hardware)**: "unsere DMA muss das können, auch wenn die Maschine angehalten ist". Built (T16 §13.11).
3. **Reset after a tool:** reset the C64 on exit on our core only (as proposed), or also on Gideon's path (changes stock behaviour; the bank registers are changed there too)?
   **Decided (Alex, 2026-10-04): yes**, on our core only; Gideon's path unchanged.
4. **Internal preference on our core:** accept "set Auto or External, reset, freeze again" for M1 (no hardware), or build the `cart_present` re-latch while paused (B1)?
   **Decided (Alex, 2026-10-04): yes for M1**: the U3 popup ("Auto/External, reset, freeze again"), no `cart_present` re-latch.
5. **Unattended mode** (`ctx->args` `auto`, JSON reply with sha256) in trex_crt for the scripted board tests — OK to add?
   **Decided (Alex, 2026-10-04): NO.** No unattended mode in trex_crt (the fork's `auto` args/JSON reply were removed again). The board session drives the normal menu: the script starts the tool over REST and asks Alex for the keys with exact instructions.
6. **C64MegaCart and TwoMegabyter** (no hardware here): in the RC as "host-tested, not board-tested" with CRT ID 88 fixed now and TwoMegabyter left unsupported, or removed from the RC claim?
   **Decided (Alex, 2026-10-04): yes for the RC**: C64MegaCart host-tested only (type 88), TwoMegabyter unsupported.
7. **EF3:** add Alex's EF3 to the RC board set (B11; it is not in the session's four carts, and it is the cart most exposed to PHI2 stopping)?
   **Decided (Alex, 2026-10-04): no.** EF3 is not board-tested in M1 and not part of the cart session.

## 12. As built (2026-10-05)

Branches: repo `t16-complete` (RTL, sims, this spec, `tests/cart/t16_session.py`), app `t16-complete-app` (firmware policy,
merge of `t16-complete-app-trex` = `apps/trex_crt`). Nothing is merged.

**Hardware / firmware (T16 §13.11):** route A built, also for freeze sessions (the responder makes real cycles for a
cartridge on the port); the DMA hold tick; `trx_sys` per-word byte buffers (lens root cause); the firmware item
**"Cartridge Port Writes"** `Auto` (default) / `Writer only` (`trx_slot_apply_policy()` at the end of
`ConfigureU64SystemBus()`, i.e. every C64 reset and every settings change; `app_trx_cleanup()` re-applies it when an app
returns). The item is shown only when `SLOT_STATUS b31` says the bitstream has the slot block.

**trex_crt (`apps/trex_crt`, host harness `apps/trex_crt/host/run.sh`, about 20 s, 30,113 checks):**
- Detection §3: `core_detect()` once in `app_main`; `g_core` TRX only when `api->trx` is set and the ID reads "TRX2"; the
  slot block is checked in the session (`U9` when `SLOT_STATUS b31` is 0). Nothing is read at 0x101A0000 on Gideon's path.
- Session §5.1: `cart_session_open()`/`cart_session_close()` in `cart_base.c`, used by all three tools. Order on TRX: U9, U4,
  arming check (U2 pins both high and unlatched, U3 a cartridge shows but is unlatched), `setup_bus`, `cart_mode(NORMAL)` +
  discarded peek, the wait for the masks (U5). Gideon keeps its old order exactly (dumper only: mode and peek).
- Write guard §5.4: `slot_wr_open()`/`slot_wr_close()` (close restores the value found at the start, `wr0`, idempotent, reads
  back three times: U7); `writer_guarded()` around `do_write_crt`; `ef3_name_slot` has its own guard after the name dialog;
  after a failure `flash_reset_chips()` puts the chips back to read mode before the gate closes; backstops
  `cart_session_close`, the end of `app_main` and the firmware's `app_trx_cleanup()`.
- Exit §5.5: deferred `media->reset()` when the cartridge was touched, TRX only. U8 "Resetting C64" is only logged (the
  window is closed by then).
- CRT type 88 for C64MegaCart (probe, native list, `megacart_info`); TwoMegabyter unsupported.
- Host harness H1-H8: H1 compares the Gideon-path access list (line count + FNV-64 of the bus traffic, 34 scenarios) with a
  golden taken from the unmodified sources at `029da8ef`; H4 stops the cartridge answering at every access of a write run
  (EF1, MegaByter, GMod2, C64MegaCart, EF3, wr0 0 and 1, about 7,800 runs): the gate equals `wr0` afterwards, no ROML/ROMH
  write cycle was attempted while it was closed, nothing hangs. EF3 is modelled at the register level only.
- **No unattended mode** (Q5 = no, Alex 2026-10-04): the first build had one (`args=auto`, JSON answer); it is removed again. The tools run through their screens; REST cannot drive them (an app started over REST has no screen and no keys), so the board session has Alex start them from the freeze menu / file browser and press the keys (T16-board-session.md).

**Writer streams the CRT (2026-10-04 board: "No memory for the CRT" for Timo's Castle, 1 MB, on our core).** `load_crt` read
the whole file with a doubling `realloc` (a 1 MB CRT of 1,048,640+ bytes asked for 2 MB contiguous) and the app has no such
block. Now (`cart_writer.c` `crt_reader_t`): on our core the CRT is never loaded: summary, validation, write and verify each
open the file and read the CHIP packets in sequence (header, then the 8 KB of data into the one 16 KB bank buffer), one
bank in memory at a time. On Gideon's core the whole-file load stays as it was (the EasyFlash 3 write keeps the C64 bus
busy for half a minute and the stick dropped off then; reading from RAM was the fix), and falls back to the stream when the
heap has no block for it. The validation pass before the erase now also reads the last byte of every packet's data, so a CRT
that ends inside a packet is refused with the cartridge untouched (before: found after the erase). The register traffic
of both cores is unchanged (golden and the H4 access counts identical). Host harness H9: a full 64-bank EasyFlash CRT with a
heap that has no block above 32 KB, written and verified on our core and on Gideon's (fallback), the largest request on
our core is one bank; truncated CRTs (EF1, MegaByter, GMod2, both cores) change nothing on the cartridge; no file left open.

**Deviations from the spec text, all small:** setup_bus runs before the Normal-mode peek (today's dumper order; the peek is only
a bus cycle with `DMA_MEMONLY` 0, which setup_bus sets); U2 takes precedence over U3 when both pins are high; the flash reset
command is sent on failure only, a normal end leaves the chip in read mode; `core_detect()` reads `regs[0..3]` directly (near
page).

**Assumptions for the open questions (Alex 2026-10-04: Q1 = Auto decided; the rest built to the proposed default):**
Q2 route A (hardware; it was already decided in T16 Q8); Q3 reset after a tool on our core only; Q4 U3 popup ("set Auto or
External, reset, freeze again"), no `cart_present` re-latch while paused; Q5 no unattended mode (Alex); Q6 C64MegaCart in the RC
as host-tested only with type 88, TwoMegabyter not supported; Q7 EF3 stays in the code path, not board-tested, not in the
session script (add it if Alex wants B11).

**Not done:** B11 (EF3 on the board), the RUN/STOP-after-F3 abort on the board (the loops have no key check, so it behaves
like a normal end; the failure paths are the host's H4), the 8K "save as" menu of the dumper still lists "Magic Desk (19)"
first (the probe now gives 88 for a MegaCart).


## Decision: no write protection on the expansion port (Alex, 2026-10-05)

"Weg mit dem Schreibschutz. Den hat ein echter C64 auch nicht." ROM_WR_EN is always on while the slot block is present; writes reach the port cartridge as on a real C64. The "Cartridge Port Writes" setting (Q1: Auto / Writer only) is removed from the app; trex_crt's write guard stays harmless (it finds the gate already open). Replaces Q1 and §6.1's gating.

## 13. EasyFlash 3, whole cart (T51, Alex 2026-10-07; as built on `t51-ef3-app`)

Three additions to the EasyFlash 3 rows of §6, all of them behind explicit user answers; the stock register traffic of the existing flows (one-slot dump, slot write, verify, inspect) is unchanged (H1 golden digests identical).

| Function | Real EF3 in the port |
|---|---|
| Dump, whole cart | Dumper F7 picker: slots 1-7 and "Full EF3 (all slots, type 90)" (same wording in the writer). One CRT type 90 (PROP-002 §8.4), header EXROM 1 / GAME 0, 1024 CHIP packets (type 2) slot-major, bank field `slot*64+bank`, ROML `$8000`, ROMH `$A000`. Per slot `$DE01`, per bank `$DE02=$05`, `$DE00`, ROML read at `$8000`, ROMH at `$E000`; slot 0 (system area) is selected like every slot. Needs EF mode (power-on / menu button), see T51 §4. Read only; no `ROM_WR_EN` use |
| Erase, whole cart | Writer F7 picker: slots 1-7, "Full EF3 (all slots, type 90)", then last "ERASE WHOLE CART...". Popup YES/NO naming the loss, then the typed word ERASE; else nothing is sent. Chip erase `$AA $55 $80 $AA $55 $10` (`$8AAA/$8555`, slot 0 bank 0), done at two `$FF` reads of `$8000` (DQ7), timeout 300 s, seconds on the status line, spot check of 16 bytes of every 8 KiB half. Inside the write guard (§5.4), flash reset after a failure |
| Write, type 90 image | Writer, picker "Full EF3" + CRT type 90: validated first (nothing touched when invalid), popup YES/NO + typed word WRITE; the slots the file has packets for are erased (slot 0: 8 boot sectors of 8 KiB, then 64 KiB sectors) and programmed, other slots untouched; F5 verifies (left-out banks against `$FF`). The way back after a whole erase: a type 90 file with slot 0 only. Slot (type 32) writes still refuse slot 0 but the directory. A type 90 CRT on another cartridge: refused |

Tests (host harness, 34,248 checks): whole dump byte-exact on both cores, whole erase confirmed and refused three ways, 1016 abort injections of the erase (no write outside the guard, lock restored, no hang), image write with untouched other slots and erased left-out banks, recovery (erase, slot-0 image, verify), a flipped byte found by verify. The mock EF3 has the MX29LV640EB bottom boot (8 KiB sectors below 64 KiB) and a chip erase. Not hardware-tested: board steps in T51 §7. Not built: an abort key during the 8 MB dump, a read-back check inside the dump (dump twice and compare with `ef3_image.py cmp`).

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

T57: trex_crt (dump / write / inspect) 100 % on both cores

Milestone M1, status ✅. Open (one line, as in the TODO table): –

Decisions: [ADR-0006 trex_crt (dump / write / inspect) 100 % on both cores](../adr/0006-trex-crt-on-both-cores.md); [ADR-0042 EF3 chip erase is a real 8 MB erase](../adr/0042-ef3-chip-erase-is-a-real-8mb-erase.md); [ADR-0043 EF3 scope: no freezer modes, no USB, complete dump included](../adr/0043-ef3-scope-no-freezer-no-usb-complete-dump.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.
- **2026-10-10 evening, B11 passed, T57 closed (Alex gave slot 3 for the write).** Writer (integ-m1-app with staging, installed to /Flash/apps): `t16_test_ef1.crt` into EF3 slot 3, 64 banks OK, "Write complete", the EF-Directory names the slot "Timos Castle". Slot 3 dumped as type 32 (to /Temp): all 128 CHIP packets equal the written image, header EXROM/GAME equal. F5 verify did not react to the REST key (the slot dump replaced it). PHI2-stopped DMA was enough (no option B needed).
- **2026-10-10 evening, B11 slot dump half passed (EF3 slot 2, see T51 history: data equal to the whole dump, EF1 EAPI put in); the slot write half needs Alex's go (writes an empty EF3 slot).**
- **2026-10-10, B1-B10 done on our core (EF1, MegaByter, GMod2; Alex and Claude at the board):** B1 dumps = baselines/first backups; B2 MegaByter dump with Lykia running; B3 from the freeze menu (many times); B4 port-off popup; B5 exit, reset, cart boots; B6/B7/B9 writer TEST+BACKUP (EF1 2026-10-09, MegaByter) and GMod2 flash+EEPROM; B8 the EF1 highscore saved 2026-10-09 survived power cycles (bank 16 in today's dump); B10 open-time popup for broken CRTs. B3's REST start struck by Alex (app matter, decoupled). Left: B11 EF3.
- **2026-10-10, probe fix passed on the board (Claude alone, real MegaByter Lykia, app 3eeb4372 run from /USB1):** before, F1 showed "EasyFlash (1024KB)" (Alex: "Das ist falsch !"). Cause: `cart_detect_easyflash()` writes $A5 to $DF00 and reads it back; on our core a read that no device answers returns the last byte on the bus, i.e. the app's own write, so every cart looked like EasyFlash RAM. Fix in the app only (cart_base.c, our core only): two cells $DF00/$DF01 with different values, an echo fails. Board console: "$DF00/$DF01 echo test: 5A 5A / A5 A5 -> open bus", "Result: MegaByter (type 86, 128 banks)". The app dump (128 chips, type 86) equals the port dump and Alex's lykianew.crt (sha b0e7e0dfcc2640a2). Alex's earlier manual dump with type MegaByter had only 513 KB (64 banks from the wrong probe). Open: whether our core's open bus (echo of the last CPU write) should behave like Gideon's.
- **2026-10-10, open-time CRT check passed on the board with the real EF1 (Alex):** opening `t16_bad_ef1.crt` for writing gave the popup "The CRT has no chips to write" at once, no writer screen; a fresh dump afterwards equals the one before (a3d20dd8).
- **2026-10-10, validate the CRT when it is opened for writing (Alex: "den Writer Screen gar nicht erst starten"; branches t57-validate-open / t57-validate-open-app from integ-m1, host-tested, board-checked).** `main.c` `run_writer` calls the new `cart_writer_check_crt(path, name)` (cart_writer.c) before `cart_writer_init`: one pass over the file with the existing streamed reader (`crt_read`/`crt_seek`/`next_chip`/`skip_packet_data`, no second parser) that checks the 64-byte header, a CRT type the writer flashes (32, 60, 86, 19/88, 90), whole CHIP packets to the end of the file (last byte of each packet read), packet length == 16 + size and size <= 8 KiB, bank < the type's bank count, at least one chip. Invalid: one popup with the reason, the app returns to the file browser, no writer screen, no bus access, no reset, nothing erased. Valid: the writer opens as before and takes name, type, chip count and byte total from the check (`s_pre`), so the file is walked once more, not twice. The cartridge-dependent refusals (ROMH data on an 8K flash, type 90 vs. single slot) stay in the writer, because they need the cart in the port. Same entry for action `verify`. Behaviour change to know: a CRT of a type the writer has no cartridge for (type 0 plain ROM etc.) is now refused at open (before: writer screen, then "No supported flash cart" or, on an EasyFlash, it was written); the stock-traffic golden `rom16_write` goes from 14 lines to 0 for that reason. Host: new H10 (zero chips, wrong type, bank out of range, wrong packet size, no signature, cut header, cut inside a packet, trailing bytes, missing file; each on our core and Gideon's: exactly 1 popup with the reason, 0 bus accesses, no reset, no open file, cart unchanged; valid EF1 on both cores: no popup, written and verified, summary taken from the check); RED on the old main.c (65 failures), GREEN 34,411 checks, 0 failures. Board (C64U 192.0.2.189, trex_crt.u2a from this branch in /Flash/apps, driven over the telnet menu with the machine paused by REST, `machine:menu_screen` stayed 404 because a trxmon owns the machine): `t16_bad_ef1.crt` (the zero-chip file) -> popup "The CRT has no chips to write." then Ok -> back in the file browser, no writer screen, no extra RUN/STOP; `t57_cut_ef1.crt` (1 MB, 100 bytes cut off) -> "The CRT file is cut off inside a chip packet." (checked within 6 s); `t57_type0.crt` -> "CRT type 0 is no cartridge the writer can flash."; valid 1 MB `timo_restore.crt` -> writer screen opened within 2.4 s of the keypress (no F3 pressed). The cartridge was NOT visible on the board (BASIC after reset, writer: "No cartridge on the pins"), so "cart untouched" rests on the host check (0 bus accesses) and the app never reaching `cart_session_open`; no dump compare was possible. JTAG status after the Blaster came back: SLOT_STATUS 0x8004F01F, game=1 exrom=1, ext mask 0, armed False, fault False (no cartridge pulling GAME/EXROM, port not armed). Not tested: the valid write itself (Alex, docs/archive/board-tests/board-tests-pending.md).
- **2026-10-09 evening, design loss after the zero-chip CRT refusal not reproduced (agent, board autonomous):** the app_err sequence (trex_crt writer on t16_bad_ef1.crt, F1, F3, exit) 15 times on t72-io-off-f72c567d and 8 times on the exact event pair t16-r21-dbg-5d097fe5 + app 5e1482fd, EF1 active: no loss; the refusal shows "CRT has no chips to write", "Nothing was erased", "WRITE FAILED!" and needs one extra RUN/STOP to leave. Untested: the same pair with REU, Ultimate Audio and UCI Enabled (all three were Disabled in that run). Alex's hypothesis (the IO devices together with the cartridge) is neither supported nor refuted. See T29 history for the JTAG status tool.
- **2026-10-09 evening, in-game save on a real EF1 (Timo's Castle "Reset Highscore"): root cause = internal IO2 devices answer over the cartridge's RAM.** Board: bitstream t16-r21-dbg-5d097fe5, app 5e1482fd, trxmon, `tests/cart/ef_ingame_debug.py`.
  - What the game does: the EAPI (copy of bank 0 ROMH `$B800`, called at `$C814`) copies its code and variables to the EasyFlash RAM `$DF80-$DFFB` and checks each byte by `sta`/`cmp` (ERR_RAM on mismatch); the game itself also copies a loader to `$DF00-$DF59` (bank 3 `$8C86`, vectors `$0330/$0332` -> `$DF00/$DF06`). So the whole EF RAM `$DF00-$DFFF` holds code that runs.
  - Measured (Ultimate Audio `Map Ultimate Audio $DF20-DFFF` Enabled, as configured): the CPU writes a 124-byte pattern to `$DF80` and reads back 123 of 124 wrong (`00 01 00 ...`, sampler registers). Disabled at runtime (REST, not saved): 0 of 124 wrong, EAPIInit returns clean (A/X = `$A4`/`$01` the AM29F040 IDs), autoselect runs from `$DFD5` with `JMP $DFBB`.
  - RTL reason: `c64_core.v` hands `io_oe | smp_oe` to `c64_bus_arbiter.v`, whose last stage (`io_ext_oe && io_ext_sel && !bus_we`) overrides every read in `$DE00-$DFFF`, the port's data (`port_rd`) included. The UCI (`uci_oe`) and the sampler (`smp_en`) are not gated by the port's IO2 claim (`slot_port.v io2_rd`) or by `C64_BUS_INTERNAL/EXTERNAL`.
  - UCI: with the port armed, a CPU dump of `$DF00-$DFFF` shows `$DF1D = $C9` (UCI ID) inside the otherwise `ff 00` EF RAM pattern, i.e. the UCI answers at `$DF1C-$DF1F` over the cartridge (app `set_emulation_flags` sets `CMD_IF_SLOT_BASE = 0x47` = `$DF1C` for a physical cart; the "UCI at `$DE1C` for every EasyFlash" commit only covers CRTs). Those bytes are game code.
  - REU: no REU register behaviour seen at `$DF00-$DF0A` (the EF RAM pattern was read); app `effectuate_settings` clears `C64_REU_ENABLE` when `ConfigureU64SystemBus()` sees the cart (a3c726f5). The register itself was not read back; not proven either way.
  - With Audio off alone the game still ended in a KERNAL cold start (UCI still on, still overlapping `$DF1C-$DF1F`). Alex then ran with Ultimate Audio, REU and UCI all Disabled (runtime): Reset Highscore works. Not yet measured: that the highscore sector content changed (default score equals the old one?), flash dump after the save.
  - Proposed fix (not built): (a) app: while a physical cart is armed (`ext` in `ConfigureU64SystemBus`), set `C64_SAMPLER_ENABLE = 0` and `CMD_IF_SLOT_ENABLE = 0` (or UCI to `$DE1C`, free in IO1 of an EF), like the REU; (b) RTL: gate `io_ext_oe` with the port's IO2/IO1 read claim so an internal device never overrides a cartridge read the port serves. The dump is unaffected (DMA reads do not see EF RAM through I/O).
  - Scripts: `tests/cart/ef_ingame_debug.py io2` (EF RAM read-back test), `cfg`, `set`. The monitor's `m cpu/cart df80` shows RAM/zeros, not the EF RAM: use a CPU routine. Side finding: `r pc=` after a `z` often fails "mid-instruction" (stop at a breakpoint then `z` once works only sometimes).
- **2026-10-09 afternoon, T16 EF1 run 3: in-game save BUG on our core.** After a reset the alive check saw only 1 PC (FAIL; Alex: game on its menu). Alex chose "reset highscore" in Timo's Castle (the game's EAPI writes the flash): the game crashed with SID noise; the dump afterwards equals the backup (nothing saved). Power cycle: game back. To debug with trxmon (breakpoint on the game's EAPI / $DE00 writes, step through). The script's power-cycle stage crashed on the flashed core (fixed, 16eac5b); persist not run.
- **2026-10-09 afternoon, T16 EF1 run 2 (flashed core t16-r21-dbg-5d097fe5 + app 5e1482fd from integ-m1 run 597, incl. the `_lseek` fix): PASS dump (plain == lens), app_pref (U3 popup with preference Internal), script write test bank 57 (inhibit, autoselect, program, read-back, sector erase, dump == backup), app_write TEST CRT (F3, F5 verified OK, host dump == image) and BACKUP CRT (verified OK). app_err: the zero-chip CRT was refused, nothing erased (Alex), BUT right after it JTAG read garbage (`read of 0x101001 got 0x09; is a TRX64 design loaded?`), no ping, HDMI black until Alex power-cycled — the second loss of the design today (also LOR-2, T29). Cause open. Alex: validate the CRT when it is opened in the file browser, do not open the writer screen for an invalid file (to build).**
- **2026-10-09, board (Alex): fixed writer PASSED.** trex_crt.u2a from t57-crt-stream-app c87c2496 (app-only fix; the firmware on the board was still ff56dedb without the `_lseek` fix) in /Flash/apps, power cycle, `/USB1/timo_restore.crt` (= t16_backup_ef1.crt) "Write to Cartridge": F1, F3 wrote bank by bank, F5 "verified OK", Timo's Castle runs. Alex's cart is restored.
- **2026-10-09, root cause of the writer bug found and fixed (branches t57-crt-stream-app / t57-crt-stream; host-tested, NOT board-tested).** Board console (JTAG) of a second write, complete 1,050,688-byte CRT: "CRT is streamed", "CRT: 1 chips, 8192 bytes", "CRT validated: 1 chips", erase, "Write complete" with no programming. The streamed reader (`crt_seek`, apps/trex_crt/src/cart_writer.c, was `file_seek` = `fs->fseek(f, off, 0)` in ui_shim.c) lost its place after the first CHIP packet. Mechanism: newlib 4.2 `__sflush_r` (fflush.c, called by `_fseeko_r` "dumb" path) drops a read buffer that still holds bytes by calling `lseek(fd, 0, SEEK_CUR)` while `__SOFF` is not set, i.e. before the file was ever seeked; `_lseek` in software/filemanager/ult_syscalls.cc returned -1 (errno != ESPIPE/EINVAL) for SEEK_CUR, so `fseek` returned EOF and did not move. `_fstat` reports S_IFCHR, so the read-ahead optimisation is off and every fseek takes that path. `crt_seek` ignored the result and set its own `pos`, so the reader was behind the file (the first seek to $40 happened to be a no-op at the right place; the skip over the 8 KiB of chip 1 was not). The next header read found data, not "CHIP": walk ended, 1 chip; after the erase the write loop's `break` on a non-CHIP header returned FR_OK with 0 banks. Only the streamed path is hit (our core always, Gideon's when the heap has no 2 MB block); the in-memory path never seeks. The earlier hypothesis (DQ7 poll answered by RAM, branch t57-writer-poll caada55: RTL bench `dma_prog`, AM29F040 model) is refuted by this; that branch stays as the bench and its notes. Fix, app: `crt_seek` reads forward and discards, goes back by reopening, never calls fseek; `crt_seek`/`skip_bytes`/`skip_packet_data` return failure; new `next_chip` tells clean end of file from garbage. Guards: the validation walk must end exactly at the end of the file and find at least one chip before anything is erased (popup "Nothing was erased"); the write must read as many packets as validated, end clean and program at least one bank, else FR_DISK_ERR "WRITE INCOMPLETE" (never "Write complete"); verify counts a short read, garbage or zero compared banks as errors. Fix, firmware (root): `_lseek` tracks the offset per fd (read/write/seek), answers SEEK_CUR, returns the real offset for SEEK_END and reports a failed `File::seek`; needs a CI package. unzip and softboot apps use fseek the same way and are fixed by the firmware part only. Harness: the mock fs models newlib's FILE with the failing SEEK_CUR (`mock_seekcur_works`); H9 runs the full 128-chip EF CRT on today's firmware, on a firmware with SEEK_CUR and on Gideon's core (small heap), and refuses trailing bytes and a damaged packet before the erase. RED on the old reader: 30 failures (bank 0 stays erased, 6 failed fseeks, truncated/garbage CRTs erase the cart); GREEN after: 34,273 checks, 0 failures, H1 golden digests unchanged. Restore: board retest, docs/archive/board-tests/board-tests-pending.md section 2.
- **2026-10-09, root cause (console over JTAG, second F3 on the blank cart): `CRT: 1 chips, 8192 bytes` / `CRT validated: 1 chips (ROML only)` for the 128-chip file: on our core the writer streams the CRT, newlib's fseek needs `_lseek(SEEK_CUR)`, which `ult_syscalls.cc` did not implement, `crt_seek` ignored the failure, the walk ended after chip 1 and the write loop wrote 0 banks and said "Write complete". The DQ7-poll suspicion below is refuted (t57-writer-poll caada55, RTL bench). Fixed on t57-crt-stream / t57-crt-stream-app (reader without fseek + guards, `_lseek` offset per file), harness H9 red (30 fails) -> green (34,273 checks), CI run 595 green, package t57-crt-stream-7cb3dc72. Fixed trex_crt.u2a put on the board (/Flash/apps) for the restore.**
- **2026-10-09, board (Alex, T16 EF1 run, bitstream t16-r21-dbg-5d097fe5, app ff56dedb): writer BUG on our core.** Timo's Castle EF1, "Write to Cartridge" with the app's own complete dump t16_ef1_app.crt, F3: erase reached the flash, programming did not; F5: "VERIFY FAILED: 52 OK, 12 err" = exactly the data banks (ROML+ROMH 00-0A, 10). The dumper passed on the same cart the same day. Cart is blank in its data banks; backup /path/to/.cache/trx64-t16/ef1/dump/t16_backup_ef1.crt (byte-exact, verified twice). Suspected: the DQ7 poll reads RAM/MEMONLY during the freeze instead of the cart and "succeeds" at once (erase waits a fixed second, so it passes). Fix in work on t57-writer-poll(-app); restore = board retest of the fixed writer.

- Task text: trex_crt (dump / write / inspect) 100 % on both cores for the M1 RC (Alex 2026-10-03/04: the app evaluates the firmware/bitstream and serves both). Spec: docs/specs/T57-trex-crt-both-cores.md. Detection `api->trx != NULL` + ID "TRX2" + SLOT_STATUS b31; Gideon path = today's register traffic unchanged. Our core: skip the Auto/External refusal, `cart_mode(MODE_NORMAL)` in all three tools, `$01` to the 6510 port during the dump (route B firmware staging or T16 Q8 hardware), ROM_WR_EN firmware policy "Cartridge Port Writes" (Auto/Writer only) + raised only inside a write guard in the app (cleared on every exit, loader backstop), reset on exit (bank registers are write-only), CRT type 88 for C64MegaCart. Tests: host harness with cart models and abort injection, sim/slot `wrguard`, board with rom16/ef1/megabyter/gmod2 (EF3 if Alex agrees). Lens REAL byte drops are a separate bug.
- **In main 2026-10-06 (ba060ae) via app integ-m1-app (t16-complete-app incl. the writer that streams the CRT). Open: board B1-B10 with real carts (T16-board-session.md, needs Alex's carts; Alex's 2026-10-06 test ran CRT images and did not report the trex_crt dumper/writer/inspector on a real cart).**
- **Built 2026-10-05 (app t16-complete-app, spec §12):** detection, session, write guard, deferred reset, type 88, host harness H1-H8 (apps/trex_crt/host/run.sh, 23 s, 30,220 checks, H4 abort injection ~7,800 runs), firmware policy; Q1 = Auto decided, Q2-Q7 built to the proposed defaults (Q2 route A hardware, Q3 reset on our core only, Q4 U3 popup, Q5 NO unattended mode (removed), Q6 MegaCart host-tested only/type 88/TwoMegabyter unsupported, Q7 EF3 not board-tested, decided by Alex).
- **Alex decided 2026-10-04:** Q1 Auto, Q2 route A, Q3 reset on our core only, Q4 U3 popup no re-latch, Q5 NO unattended mode (removed; the board session drives the menu, Alex presses the keys), Q6 MegaCart host-tested/type 88, TwoMegabyter unsupported, Q7 EF3 not board-tested in M1 and not in the cart session
- **2026-10-10, MegaByter typed as EasyFlash on our core (Alex: "Das ist falsch ! Da müsste Megabyter stehen"; branches t57-mb-probe / t57-mb-probe-app from integ-m1).** Board scan F1 of Lykia gave "EasyFlash (1024KB)" with 64 ROML + 64 ROMH banks. Root cause: `probe_cart_type` (cart_dumper.c STEP 2) takes the EasyFlash branch on `cart_detect_easyflash()` (cart_base.c), which wrote $A5 to $DF00 and read it straight back. A MegaByter drives nothing at $DF00; on our board an undriven read returns the last bus cycle (T16 §4.4, board-seen for $DE00), and the last cycle is the app's own write, so the test passed on any cart and STEP 3 then fixed the bank count at 64 and the dump read ROML+ROMH in Ultimax. Fix (our core only, Gideon's register traffic unchanged, golden H1 identical): after the old test passes, two neighbouring cells get $A5/$5A and are read back, then swapped ($DF00/$DF01); an echo of the last cycle fails the first read. Host: mock `open_echo` (undriven read = last bus byte), H2 dumps all six carts with it; RED before the fix (rom16, megabyter, gmod2, megacart typed 32, 10 failures), GREEN after, 34,491 checks. Not yet run on the board (Alex): F1 on Lykia must say MegaByter, 128 Bank(s); the open-bus echo itself is inferred from the T16 $DE00 observation and the symptom, not measured at $DF00.
