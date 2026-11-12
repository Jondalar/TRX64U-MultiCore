# T60 — REU up to 16 MB, with Gideon's enable and size

Status: spec, written before the RTL (2026-10-06). Covers TODO T60 and the REU part of T62 (gap audit G02).
G01 (the REU at the app's 0x01000000) is done in the same change, see §5. §7 (2026-10-06, branch `t60-shared`):
the cartridge ROM image shares the REU's 16 MB block, the app's built-in cartridges get a slot of their own.

## 1. What Gideon's core does (read from his sources, not from our docs)

Source: `1541ultimate/fpga/cart_slot/vhdl_source/reu.vhd` (entity `reu`), `cart_slot_registers.vhd`,
`slot_server_v4.vhd`; the app side in `software/io/c64/c64.cc` / `c64.h`. The U64's own bitstream is binary
only, so this is the shared cart_slot code every Ultimate core instantiates. Evidence that the U64 uses the
standard (not the extended) REU: every `u2p_*` top in the repository passes `g_extended_reu => false`, the
default of `slot_server_v4`, and the capability word we copied from the U64 (`g_capabilities = 25C40222`)
has `CAPAB_EXTENDED_REU` (bit 16) clear.

Registers the app writes (`cart_slot_registers.vhd`, offsets of `C64_CARTREGS_BASE` 0x10040000):

| Offset | App name | Core |
|---|---|---|
| +0x08 | `C64_REU_ENABLE` | `control.reu_enable <= data(0)`, drives `reu`'s `enable` |
| +0x09 | `C64_REU_SIZE` | `control.reu_size <= data(2 downto 0)`, drives `size_ctrl` |

Reset values: enable 0, size 7. The app sets size from `CFG_C64_REU_SIZE` (0 = 128 KB ... 7 = 16 MB, strings in
`c64.cc` `reu_size[]`) and enable = 1 only for `CFG_C64_REU_EN == 1` (`C64::set_emulation_flags`, c64.cc:431);
"GeoRAM Mode" (2) does not set it; a cartridge that requires or prohibits the REU overrides it (c64.cc:1814-1871).

What `reu.vhd` (standard variant) does with them:

- `enable = 0`: `reg_output` is 0 and `io_read`/`io_write` are gated off. $DF00-$DF1F answer nothing (open bus);
  there is no REU. `enable` is live, not sampled at a reset.
- Size mask (`size_ctrl` -> `mask`): 0 -> 0x01, 1 -> 0x03, 2 -> 0x07, 3 -> 0x0F, 4 -> 0x1F, 5 -> 0x3F,
  6 -> 0x7F, 7 -> 0xFF (128 KB ... 16 MB). It is applied to the memory address only, bits 23:16 of it:
  `masked(18:16) = reu_addr(18:16) and mask(2:0)`, and (standard variant) `masked(23:19) = reu_base(23:19) and
  mask(7:3)`, where `reu_base` is the value last written to $DF06, not the running counter. The running address
  `reu_addr` is 24 bits and counts on unmasked. So the address wraps in the size's own range (a 128 KB REU
  reads and writes the same 128 KB at bank 2 as at bank 0), the registers keep counting, and, since the upper
  bits come from the base register, a transfer that crosses a 512 KB boundary wraps inside that 512 KB (a quirk
  of Gideon's standard REU, kept as it is: drop-in).
- $DF06 is written with all 8 bits (bank). Read-back in the standard variant: `11111` & `reu_addr(18:16)`.
- $DF00 status: bit 7 IRQ pending, bit 6 end of block, bit 5 verify error, bit 4 = `mask(1)` (1 for 256 KB and
  larger, 0 for 128 KB), bits 3:0 = 0.
- $DF09 reads `irq_en irq_done irq_error` in bits 7:5, rest 1; $DF0A bits 7:6 fix_c64 / fix_reu, rest 1.
- Memory: 16 MB window `g_ram_base_reu` = 0x1000000 (the app's address; `slot_server_v4` default), one byte
  per access.

## 2. What we have today

- `cores/icepi/c64_reu.v` (m1nl's, from MiST): expansion address 22 bits (4 MB), $DF06 written with 6 bits and
  read as `11` & addr[21:16], no size mask, status bit 4 constant 1. No enable: `c64_top` `reu_present` =
  `flags[12]`, a constant 1 in `c64_core` `C64_FLAGS`, sampled at `cpu_reset`.
- `c64_top.v` hands `{SDRAM_BANK, 1, reu_mem_addr[21:1]}` (16-bit word address); `top.v:321` used
  `reu_cmd_addr[20:0]` -> 4 MB at DDR2 byte 0x4000000 (word `REU_BASE` 0x2000000), before this change.
- `ultimate_sys.vhd` stores writes to +0x08/+0x09 in the page RAM (reads back what was written) and does
  nothing else with them. The app already writes both; no app change is needed (§4).

## 3. Change

1. `c64_reu.v`: `reu_mem_addr`/`r_exp_addr` 24 bits, input `reu_size[2:0]`, Gideon's mask on the memory address
   (bits 23:19 from the base shadow of $DF06, 18:16 from the running address), $DF06 8-bit write and the
   Gideon read-back, status bit 4 from the size. Patch note in `cores/icepi/ORIGIN.md` (the vendoring script is
   gone since 2026-10-01; the files are edited in place).
2. `c64_top.v`: input `reu_size`; command address `{1'b0, reu_mem_addr[23:1]}` (23-bit word address of 16 MB);
   `reu_present` follows `flags[12]` live (it changes only while the REU is not moving data, and at a reset), so
   an enable change by the app is seen without a reset, as on Gideon's core.
3. `c64_core.v`: inputs `reu_en`, `reu_size`; `C64_FLAGS` bit 12 off, `flags[12] = reu_en`.
4. `ultimate_sys.vhd`: outputs `reu_enable`, `reu_size`, latched from the writes to +0x08 (bit 0) and +0x09
   (bits 2:0); reset 0 and 7 (as `cart_slot_registers.vhd`). Read-back unchanged (page RAM), so the app's
   `if (C64_REU_ENABLE)` reads (`system_info.cc:97`, `c64.cc:1868`) see what it wrote.
5. `top.v`: both go through a two-flop synchroniser into the C64 clock like `cart_type`; the REU address maps
   through the new `reu_ddr_map.v` (`REU_BASE + cmd_addr[22:0]`), covering the whole 16 MB.
6. Benches: every `c64_core` instance gets `reu_en = 1`, `reu_size = 5` (4 MB) so that existing benches keep
   the REU they had. New bench `sim/reu` (§6).

After the change, at power-on the REU is OFF until the app writes `REU_ENABLE` (as Gideon's: default
"Disabled"); before that nothing answers at $DF00 (this closes the G02 observation that a $DF00 probe found
a 4 MB REU with the setting Disabled).

## 4. App

No app change: `c64.cc` already writes `C64_REU_ENABLE` (+08) and `C64_REU_SIZE` (+09) with exactly Gideon's
meaning, and `reu_sizes[]` in `reu_preloader.cc` / `c64_subsys.cc` is the 128 KB..16 MB table. The only
app-side point, G01 (the REU memory address the app uses for .reu load/preload/save, 0x01000000), is met by
moving the REU there (§5).

## 5. Layout: the REU at Gideon's address (G01)

The REU's DDR2 base moved from byte 0x4000000 to **0x1000000** (word `REU_BASE` 0x0800000), the app's
`REU_MEMORY_BASE` (`io/c64/c64.h:14`, `api/attachment_reu.h:7`), 16 MB up to 0x1FFFFFF. So .reu load, REU
preload, REST `load_reu`, socket DMA and Save REU (all at `REU_MEMORY_BASE`) now act on the memory the REU
uses (G01). Checked that nothing of ours lives in 0x1000000-0x1FFFFFF:
- the app's memory ends below it: `linker.x` of ultimate, test, factorytest and bootloader end at 0xE90000
  (0x30000 + 0xE60000), 0xEA0000 (0x30000 + 0xE70000) and 0xEA0000 (0x1000 + 0xE9F000); `ultimate-app-hw-spec.md`
  §3a lists 0x01000000 (16 MB) as REU / GeoRAM and 0x02000000 as the RAM disk. The `heap_4`/malloc (pix layer
  buffers in `trx_video.cc`, still copies, mover bounce buffers) is inside the .bss, below 0xE90000.
  The updater image (`update/linker.x`) is at 0x03000000.
- hardware: drive A 0xEE0000, cart RAM 0xEF0000 (`cart_fill` RAM_BASE word 0x778000), cart ROM 0x3C00000
  (ROM_BASE word 0x1E00000), still slots 0x5050000, engine areas 0x5C00000-0x7FFFFFF, T20 §5.1; the JTAG window can
  reach any address on purpose. Nothing else in the app sources names an address in the range
  (`control_target.cc` and `dos.cc` use 0x01000000 as Gideon's own REU/scratch area; the testers and
  `UPDATER_RUN_ADDRESS` are separate images).
- 0x4000000-0x4FFFFFF is free again (noted in T20 §5.1 for the ring later).

The cartridge ROM image moves into the same block in §7 (one block, REU or cartridge, never both; the built-in
cartridges get a slot outside it). §5 as built had left it at 0x3C00000; with ROM_SEL 0 (Gideon's firmware) it
still is there.

A08 (T20: REU enabled = no ring, no Timewarp): the only place that enforces it is the app's own check of the
REU setting, and the code today has none (no reader of `C64_REU_ENABLE` in the ring or Timewarp code). The
hardware keeps what it had: the ring flags REU transfers via `reu_busy`. `C64_REU_ENABLE` still reads back
what the app wrote, so a check on it sees "REU on" correctly.

## 6. Test

`designs/c64_basic/sim/reu` (Verilator, `c64_core` as top, the app's DMA window for register access, a sparse
byte memory behind `reu_cmd_*`) plus iverilog `tb_reu_map.v` (`reu_ddr_map` + `ddr2_port` + `mig_arb` + the
MIG model):

- 16 MB addressing: DMA to REU memory at bank $FF reaches REU byte 0xFFFFFF, and in DDR2 the 16-bit word
  0xFFFFFF (byte 0x1FFFFFF).
- Size masks: for each of the 8 sizes a write at an address above the size appears at the wrapped address
  (Gideon's formula), the counter reads back unmasked.
- Enable off: $DF00-$DF0A read like open bus; on: the registers answer and $DF00 bit 4 follows the size.
- `reu_ddr_map`/`ddr2_port`/`mig_arb` with 24-bit addresses: the command address leaving `mig_arb` is the
  expected one for the lowest, the highest and some middle addresses of the 16 MB.
- Regressions: ring, slot, input, uci, ultsys benches run unchanged (REU on, 4 MB as before).

## 7. The shared expansion-port block (REU or cartridge ROM) and the built-in cartridge slot

Alex 2026-10-04 (gap audit G01/G02): "REU && Cartridge TEILEN sich einen Space im DDR2". §5 moved the REU to
0x1000000; this section moves the cartridge ROM image next to it, into the same 16 MB.

### 7.1 Layout (DDR2 byte addresses, app window 64 MB)

| Range | Size | Use |
|---|---|---|
| 0x0F00000-0x0FFFFFF | 1 MB | **built-in cartridge slot**: the app's own small cartridges (Run PRG boot cart 8 KB; SID, MUS and MOD player 16 KB each). Only 0x0F00000-0x0F0FFFF is read (images are 16 KB at most). Free before: the gap between cart RAM (ends 0xEFFFFF) and the REU; the app's `.bss` ends at 0xE90000, `linker.x` names nothing here (Gideon's U2 had its cart ROM at 0xF00000, the U64 does not use it). |
| 0x1000000-0x1FFFFFF | 16 MB | **expansion-port block**: the REU's memory (`REU_MEMORY_BASE`, §5) **or** the CRT image (`__cart_rom_start`, 16 KB per bank, up to 4 MB), one at a time |
| 0x3C00000-0x3FFFFFF | 4 MB | Gideon's cart ROM address: only with ROM_SEL 0 (his firmware on our bitstream); our app does not use it. **Free for the ring** with the app (T20 §5.1) |
| 0x4000000-0x4FFFFFF | 16 MB | free (§5), for the ring |

The CRT window is 4 MB, not 16: `cart_slot` addresses the image with 22 bits and uses bit 22 of its request
address for the cartridge RAM. The largest CRT the app supports is EasyFlashXl (256 banks x 16 KB = 4 MB;
`get_cartridge_max_rom` = `__cart_rom_limit - __cart_rom_start` = 4 MB); EasyFlash/MegaByter/GMod2/
TwoMegabyter/C64MegaCart are 0.5-2 MB. The EasyFlash 3 (8 MB, T51) would need the window widened to 23 bits
(RAM select on bit 23); that is T51's change, the block already has the room. Beyond the window the block is
plain REU/GeoRAM space.

Nothing else of ours is in the block (§5 checked it; since then `app_format.h` puts `.u2a` apps at the top of
0x3000000-0x3BFFFFF, below 0x3C00000, not in the block).

### 7.2 Which base the C64 reads from: trx_io ROM_SEL

`cart_fill` has one ROM base per `rom_sel` (trx_io page 4 +0x030, read/write, bits 1:0, reset 0, the new input
`rom_sel` synchronised into `ui_clk`; the lens look `pk_*` uses the same base):

| ROM_SEL | ROM base | Who |
|---|---|---|
| 0 | 0x3C00000 | reset value: **Gideon's firmware on our bitstream** (drop-in: his `__cart_rom_start` and his Run PRG write there and nothing else is told) |
| 1 | 0x1000000 | our app, a CRT image (the expansion-port block) |
| 2 | 0x0F00000 | our app, a built-in cartridge |
| 3 | = 0 | reserved |

The register is written before the cartridge starts (the C64 is held in reset by `start_cartridge`: `C64_MODE_RESET`,
`C64_CARTRIDGE_TYPE = 0`, c64.cc:1672-1673 of the pin), followed by `TRX_CMD` INVAL (drop the cached lines). A bitstream
without the register reads 0 after a write of 1: the app then stays in Gideon's layout (REU and cartridge coexist
as on his machine, the §7.4 rules are the T33 rule alone). The app probes it once (`trx_xport_shared()`).

### 7.3 What Gideon's firmware does, and what is the same

Read from the app tree (Gideon's code, line numbers of the `integ-m1-app` pin 20f12432, before this change):

- Where his loader writes: `C64_CRT::load_crt(..., C64::get_cartridge_rom_addr())` (`filetype_crt.cc:72`,
  `api/route_runners.cc:186,214`, `c64.cc:1757` in `set_cartridge`), `c64.h:517-520` returns `__cart_rom_start`, `linker.x`
  (`target/u64ii/riscv/ultimate/linker.x:286-287`) sets it to 0x03C00000, limit 0x04000000. The REU
  is `REU_MEMORY_BASE 0x1000000` (`c64.h:14`, `linker.x:277-278`): two separate areas; his rule is the cart's
  `require`/`prohibit` bits, nothing else.
- `C64::set_emulation_flags` (`c64.cc:427-440`): REU on only for `CFG_C64_REU_EN == 1`, size from `CFG_C64_REU_SIZE`.
- `C64::set_cartridge` (`c64.cc:1814-1821`): `require & CART_REU` switches the REU on (config size), `CART_MAXREU`
  on with size 7 (16 MB); `c64.cc:1867-1872`: `prohibit & (CART_REU | CART_MAXREU)` switches it off and sets
  `def->disabled |= CART_REU` when it was on (the "Some features were disabled" line).
- `CART_PROHIBIT_*` per CRT type, `c64_crt.cc:647-815`: the IO-using types (`CART_PROHIBIT_IO`/`_DFXX`) prohibit
  the REU, the plain 8K/16K/Ultimax, Ocean, Zaxxon, FC3+ ... types leave it on: his REU and a cartridge run together.
- Built-in cartridges: `cart_def.custom_addr` (boot cart `c64_subsys.cc:72,1022`, SID/MUS `filetype_sid.cc:70,82`,
  MOD player `filetype_reu.cc:151`, requires `CART_MAXREU`): `set_cartridge` copies them into the same
  `cart_mem` as a CRT (`c64.cc:1807`).

What this layout does with it:

| Case | Gideon | Here (ROM_SEL shared) |
|---|---|---|
| CRT started, REU on, type prohibits the REU | REU off, `disabled` flag | **same** (`c64.cc:1867` unchanged) |
| CRT started, REU on, type leaves it (plain 8K/16K/Ultimax, Ocean, ...) | both run | **REU off**, `disabled` flag: the image is in the REU's memory. Already the T33 rule (`c64_crt.cc:822-826`), now with its reason |
| REU enabled in the menu with a CRT cartridge | REU on if the type allows | stays off (`effectuate_settings`, c64.cc:399, unchanged) |
| built-in cartridge (Run PRG, SID, MUS) with REU on | both run | **same**: the image is in the slot (ROM_SEL 2), the REU data is untouched |
| MOD player (`require CART_MAXREU`) after the .mod went into the REU | REU 16 MB on, cart ROM separate | **same** (slot) |
| load / preload / write the REU's memory (.reu, "Load into REU", REST `load_reu`, socket DMA, preload at boot, Save REU, MP3 drive probe) while a CRT image is in the block | works, REU may be off | **refused**: "A cartridge is in the expansion port; it shares the REU's memory" (REST: 409; socket DMA: ignored; MP3-drive probes: no drive). The user picks one: the REU needs the cartridge removed first. Not silent: nothing is overwritten |
| REU memory loaded, then a CRT started | REU data stays | the CRT overwrites it (the REU is off): the user picked the cartridge |

### 7.4 App changes

- `trx_cart.h/.cc`: `TRX_XPORT_BLOCK` (0x1000000, = `REU_MEMORY_BASE`), `TRX_XPORT_ROM_MAX` (4 MB),
  `TRX_BUILTIN_SLOT` (0x0F00000), the ROM_SEL register (`TRX_CART_BASE + 0x030`), `trx_xport_shared()` (probed once:
  reads non-zero, or reads 1 after a write of 1 and is put back to 0), `trx_cart_select_rom(builtin)` (ROM_SEL 2 or 1,
  then INVAL).
- `c64.h`: `get_cartridge_rom_addr()` = the block in shared mode, else `__cart_rom_start`; `get_builtin_cart_addr()` =
  the slot, else the same; `get_cartridge_max_rom()` = 4 MB in shared mode (the linker limit minus 0x1000000 would be
  48 MB); `reu_memory_usable()` / `reu_memory_busy_text()` (false while `C64_CRT::is_valid()` in shared mode).
- `c64.cc` `set_cartridge`: a cart with `custom_addr` is copied to the slot with ROM_SEL 2, every other image goes to
  the block with ROM_SEL 1; the `cart_mem[5] = 0` write at "no cartridge" only in Gideon's layout (in the block it
  would poke REU byte 5 at every reset); `CART_KERNAL` copies from the image used; after the `prohibit` handling the
  REU is also off (`disabled |= CART_REU` when it was on) while `reu_memory_usable()` is false;
  `effectuate_settings` keeps it off; `isMP3RamDrive` answers 0 (no drive) while the block holds an image.
- The REU-memory users: `filetype_reu.cc` (load / play MOD from the file browser), `user_file_interaction.cc`
  (`S_runApp`), `api/route_runners.cc` (PUT and POST `runners:modplay`, 409; `attachment_reu.h` does not store the
  upload), `network/socket_dma.cc` (REU write command, dropped with a log line), `io/c64/reu_preloader.cc` (preload and
  save at boot, status -4), `io/c64/c64_subsys.cc` (menu Save REU Memory). New `SSRET_REU_IN_USE` (`infra/return_codes.h`,
  text and HTTP 409 in `infra/subsys.h` and its test stub); the OpenAPI documents regenerated (`make openapi`).
  Not guarded on purpose: the `.u2u` firmware update (it takes the memory at 0x1000000 and restarts the machine anyway).
- `linker.x`: unchanged (its `__cart_rom_*` stay Gideon's, used with ROM_SEL 0 / no register).
- Not touched: `bist/bist.cc` (factory test on Gideon's boards: it compares the DRAM with the cartridge image through
  `get_cartridge_rom_addr()` and does not set ROM_SEL; it is not run on this bitstream).

### 7.5 Test

- `sim/cart_unit`: ROM_SEL 0/1/2/3 read the image from 0x3C00000 / 0x1000000 / 0xF00000 / 0x3C00000 (a different
  pattern per region), the lens look follows, INVAL between; `sim/trx_io`: ROM_SEL reset value, 2 bits, read-back.
- The cart benches model `cart_fill` themselves (`tb_cart.cpp`, `tb_dbgsess.cpp`, `tb_ring.cpp`); they keep
  ROM_SEL 0's address and prove the cartridge side unchanged.
- App: host tests (`trex_crt` 30113 checks, `trxmon` 672 + 64, `make host_tests`, `openapi_check`, `openapi_test`),
  the container build as `ci/app.sh` (`make -C target/u64ii/riscv/ultimate`).
- Board (not run here): `tests/cart/easyflash_eapi_test.py` reads the image at 0x1000000 now; EasyFlash, MegaByter,
  GMod2 with the REU Enabled in the menu; Run PRG with a 16 MB REU loaded (the REU data must survive); a .reu
  with a cartridge running (refused); Gideon's firmware (ROM_SEL 0) with a cartridge.

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

T60: REU as in Gideon's core, up to 16 MB

Milestone M1, status ✅. Open (one line, as in the TODO table): Gideon's firmware with a cartridge on our bitstream (ROM_SEL 0) not reported; EF3 needs the window widened (T51)

Decisions: [ADR-0010 REU as on Gideon's core: one 16 MB expansion-port block, REU or cartridge, never both](../adr/0010-reu-or-cartridge-one-16mb-block.md); [ADR-0044 Gideon's unmodified app never runs on our bitstream](../adr/0044-gideons-vanilla-app-never-on-our-bitstream.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- Task text: REU as in Gideon's core, up to 16 MB (found 2026-10-04, Xander's programs need 16 MB; spec docs/specs/T60-reu-16mb.md). Decided with Alex 2026-10-04 (gap audit G01/G02, "wie bei Gideon"): one 16 MB expansion-port block in DDR2 at Gideon's REU address 0x01000000, shared by the REU (or GeoRAM) and the cartridge ROM (never both, the user picks one); REU_ENABLE (+08, live, off = nothing at $DF00) and REU_SIZE (+09) as on Gideon's core; Gideon's size mask; no app change needed for the REU itself. Shared block (§7, t60-shared): trx_io ROM_SEL (page 4 +0x030: 0 = Gideon's 0x3C00000, the reset value; 1 = the block; 2 = built-in cartridge slot 0xF00000 for the Run-PRG boot cart and the SID/MUS/MOD players); the app writes CRT images into the block (REU forced off), built-in cartridges into the slot (REU stays on), and refuses .reu load, preload, Save REU, runners:modplay (409), socket DMA and MP3-drive probes while a CRT image is there. 0x3C00000-0x3FFFFFF and 0x4000000-0x4FFFFFF are free for the ring (T20 §5.1).
- **Decided (Alex 2026-10-07): Gideon's vanilla Ultimate app never runs on our bitstream ("NIEMALS") - no test of it; the compatibility for his layout (ROM_SEL reset value 0 = 0x3C00000, the app probe) is not a requirement and may be simplified later.**
- **In main 2026-10-06 (ba060ae: t60-reu, t60-shared; app d5c5d30e). Board 2026-10-06 (Alex): passes: Xander's UltimateDemo2026 detects REU 16 MB (all parts stable); REU Disabled = nothing at $DF00; a .reu in the REU survives Run PRG (8 x 256-byte compares DDR2 vs file); a 16K CRT (Canabalt) switches the REU off and lands in the block; .reu load with a CRT in = refused popup; EasyFlash (Timo's Castle), GMod2 (Sam's Journey), MegaByter (Lykia) run with REU Enabled.** Not reported: Gideon's firmware with a cartridge on our bitstream (ROM_SEL 0). Window is 4 MB (EasyFlashXl, the largest CRT we support); EasyFlash 3 (8 MB) needs it widened -> T51. A08 (Timewarp refusal while the REU is on) -> row T20.
