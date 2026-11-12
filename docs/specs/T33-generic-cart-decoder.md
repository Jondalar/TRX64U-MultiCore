# T33 — Descriptor-driven cartridge decoder (TREX-PROP-002 on our core)

**Status:** spec 2026-10-03, branch `m1-specs`; **built 2026-10-03** (branches `t33-decoder`, app `t33-app`; §8 "As built" is
the reference where it differs from §3). **Task:** TODO.md T33. **Milestone:** M1 (TwoMegabyter first; every type the
app's table has keeps working).
**Source spec:** TREX-PROP-002 Rev A, "Cartridge Mapping Descriptor" (draft 2026-09-30,
`docs/specs/PROP-002-cartridge-mapping-descriptor.md`, 177 lines: purpose, model, rules, fields, window map,
constraints, descriptor table for 13 types, JSON transport, acceptance). **It exists and is not repeated here.** T16.2 (§7.2) already depends on it. This spec adds only what PROP-002 does not say: how our core realises it. Shares with: T16.2 (profile table,
app side), T16 (the internal model only; the real port is separate), T20/T21 (`st_bank` state).
Evidence: code only (our RTL, app `software/ultimate-app/software`).

## 1. Goal

A new cartridge type of the common kind (registers in IO1/IO2, bank and mode fields, fixed ROML/ROMH windows) is **data written by the app**, not a bitstream
change. TwoMegabyter (CRT 89) is the first user: the app already sends it (`CART_TYPE_MEGABYTER | VARIANT_1`, `io/c64/c64_crt.cc:762-765`) and our `cart_slot`
refuses it (`cart_slot.v:147`: `m_ok = cart_type[7:5] == 3'd0`, "TwoMegabyter: no"). After the migration T16.2's per-type hardware fixes (§6.6) are descriptor
entries, the legacy per-type `case` statements can go.

Non-goals: freezers (Action/Retro Replay, `c64_action_replay`, stays dedicated), Epyx (timing), Megabit 128 (PROP-002 §2.1); GMod3/GMod4 (SPI flash);
EasyFlash 3 (a physical cart); the real expansion port (T16).

## 2. What exists today

### 2.1 App side

`C64_CRT::configure_cart` (`c64_crt.cc:614-`) turns a CRT type into `CART_TYPE` (`c64.h:127-165`: b4:0 logic id, b7:5 variant) plus a "prohibit" mask (`CART_PROHIBIT_DEXX/DFXX/IO`); the CRT header's EXROM/GAME
are only used for `CART_NORMAL` (`:623-630`). `CART_TYPE` is a single byte at 0x10040005 (page 000 offset 05, `ultimate_sys.vhd:1125`). T16.2 §7.2 plans one PROP-002 JSON record per CRT type
(`tools/cart_descriptors/*.json` -> a C table), carrying `regs/rules/bank/mode/table` "and ignored until the hardware has the generic decode": **this task is that hardware**.

### 2.2 Hardware side (icepi model inside `cart_slot`)

- `cart_slot.v:125-155`: `CART_TYPE` -> model id (`m_id`) and boot lines (`m_exrom/m_game`) by a case; taken at every CPU reset and `cart_force` (`:160-170`).
- `cores/icepi/c64_cartridge.v`: **`case (cart_id)` at `:143`** gives `exrom`, `game`, `bank_lo`, `bank_hi`, `io2_we_valid`, initial register values per type; **`case (cart_id)` at `:249`** is the register-write
  rule per type (default: write at `A7:0 == 0` -> R0, any other address -> R1, `:274-282`; Simons Basic, C64GS, Dinamic, MegaByter special `:250-273`); `:370` a third `case` chooses the flash chip select and chip address per type (`:361-390`); GMod2's EEPROM is
  bound to R0 bits 6/5/4 (`:289-300`). ROM address `{bank, addr[12:0]}`, 21 bits = 2 MiB (`:300-302`).
- Two registers `reg_0/reg_1` (`:96-98`, state bus word `st_bank`), the same as PROP-002's `R0/R1`.
- Types in the model: 0, 1/36 (AR, separate module), 4, 5, 15, 17, 19, 32/33, 60, 85, 86, 88.

### 2.3 Where our model and PROP-002 disagree (found by reading both)

These are what the descriptor table must settle **before** the entries are frozen; "code" is `c64_cartridge.v`. **Settled 2026-10-03 against VICE** (reading `vice/src/c64/cart/`, code only; Alex's review pending). File:line below are in `vice/src/c64/cart/` unless a path is given; the corrections are in PROP-002 Rev B (§4.4, §6.2, §8, §8.2). Principle (Alex, Q5): the core knows no cart type; every answer below is a descriptor field, not model logic.

| # | Type | PROP-002 | Code | Consequence | Settled (VICE) |
|---|---|---|---|---|---|
| D1 | Magic Desk, EasyFlash and every `default` type | IO1 write rules with address given only for 86/87/EF (`$00`, `$02`); MD "IO1 W DATA -> R0" without address | R0 only for A7:0 == 0, **any other IO1 address writes R1** (`:274-282`); MegaByter decodes A1 only (`:266-273`) | The descriptor's `match/mask` must be written for every row (the JSON example has them, the §8 table does not); EasyFlash's real decode (A1? exact?) comes from VICE/the cart, not from either source | **Both wrong, in different places.** EasyFlash decodes **A1 only** (`easyflash.c:215` `addr & 2`; device range `$DE00-$DEFF`, mask 3, `:267`): A1=0 -> bank (`& 0x3F`, `:218`), A1=1 -> mode (`& 0x87`, `:222`). Rules: `match 0x00 mask 0x02` -> R0, `match 0x02 mask 0x02` -> R1; the model's "A7:0 == 0 else R1" is wrong for EF. Magic Desk, Magic Desk 16K, Ocean, GMod2 decode **no** address bit: every `$DE00-$DEFF` write is the one register (`magicdesk.c:106`, `magicdesk16.c:111`, `ocean.c:127`, `gmod2.c:118`) -> `match 0x00 mask 0x00`, no R1; the model's default branch loses writes to `$DE01...`. MegaByter A1 only (`megabyter.c:146`): model right. IO1 registers are write-only everywhere (reads open bus, `magicdesk.c:107`, `easyflash.c:268`, `ocean.c:128`, `megabyter.c:126`); the model answers IO1 reads with `reg_0`/`reg_1` (`c64_cartridge.v:295-297`, `:432`, `cart_slot.v:178`) -> descriptor `io1.read OPEN` (PROP-002 §6.2). Model change: EF rule A1, other default types one rule with mask 0x00. |
| D2 | 85 Magic Desk 2 | row "as 19" (8K, `L8`, `OFF` in bit 7), footnote 4 "needs the MD2 layout (#516)" | `exrom = game = reg_0[7]`, banks `{reg_0[6:0], half}` (16K), `:222-229`; `cart_slot.v:140` calls it "Magic Desk 16" | row 85 cannot be frozen as written | **Model right, PROP-002 wrong.** CRT 85 = VICE "Magic Desk 16K" (`cartridge.h:276`, `:351`), CRT 19 = 8K Magic Desk (`cartridge.h:201`). 85: 16K lines, ROML = first, ROMH = second 8 KiB of each 16 KiB bank (`magicdesk16.c:137-142`, `:195`), bank = `value & mask`, 7 bit, up to 128 banks = 2 MiB (`magicdesk16.c:70`, `:77-79`), bit 7 = 0 -> EXROM and GAME low, bit 7 = 1 -> both high = OFF (`:80-88`). Entry: `layout L16`, `bank R0[6:0]`, `mode R0.7`, `table [{exrom 0, game 0}, {off 1}]`; row 19 stays `L8`, table `[{0,1},{off}]` (`magicdesk.c:77-84`). No model change. |
| D3 | 88 C64MegaCart | mode table 00 8K, 10 OFF, 11 Ultimax | `exrom = reg_1[7]`, `game = !reg_1[6]`: **01 gives 16K lines** (`:211-220`) | entry 01 missing in PROP-002 | **Settled (Piper/TRX64), 2026-10-03.** Mainline VICE has no C64MegaCart, Martin Piper's fork does (`martinpiper/Vice-3.1-with-C64MegaCart`, `vice-3.1/src/c64/cart/c64megacart.c`, byte-identical to `C64ReverseEngineeringMCP/vice-refs/c64megacart/c64megacart.c`; glue `c64cartmem.c`), and TRX64 implements it (`crates/trx64-core/src/cart.rs:1831-1990`). Mode = `$DF00` bits 7:6 (`c64megacart.c:198-207`): 00 8K, 10 cart off, 11 Ultimax, **01 changes nothing** (no branch: the CPU-side mode is held, bank bits still load; `cart.rs:1919-1924`). The model's `01` = 16K (`:211-220`) is wrong. Ultimax is a write window only: reads are the normal C64 map (`c64cartmem.c:919-921`, `:1237-1243`, `:1352-1357`, `:1451-1459`), writes to `$E000-$FFFF` reach the flash (`:998-999`); the model's static `exrom 1, game 0` is wrong for reads. More model errors: IO1 and IO2 each decode the whole page (`c64megacart.c:125`, `:141`, `:171-177`, `:191-216`, so `mask 0x00`, not `0xFF`), IO2 has no RAM and both pages read open bus (`:159-164`, `:179-184`), bank = `$DE00` byte plus `$DF00[5:0]` (14 bit; the 2 MiB board uses 8). CRT ID in Piper's fork is 61 (`vice-3.1/src/cartridge.h:210`); ours stays 88. Descriptor: PROP-002 §8 row 88 and footnote 8. New per-entry fields `HOLD` and `GAMEW` (PROP-002 §5.3), BANK wider than 8 bit missing (8 MiB board only). Unresolved, not a code question: on the latch-only board `01` cannot hold (PROP-002 fn 8), no software writes it. |
| D4 | 5 Ocean | footnote 3: ROML and ROMH show the same bank "as decoded by the IcePi core", to be checked against VICE | `bank_hi = bank_lo` (`:161-168`) | unchecked reference, 16K Ocean carts may be wrong | **Model right.** ROMH read returns `roml_banks[(addr & 0x1FFF) + (roml_bank << 13)]` (`ocean.c:148-152`), wired for `$A000` and `$E000` (`c64cartmem.c:940`, `:1082`): same bank as ROML, `L8_MIRROR`. Corrections beyond the question: lines are 8K only for a 512 KiB image, 16K for every other size, header not read (`ocean.c:161-166`, `:178-184`); bank = `value & (size/8K - 1) & 0x3F` (`ocean.c:104`, `:211`, `:243`). Two entries (PROP-002 §8.2), the app picks by image size. |
| D5 | 89 TwoMegabyter | `L16`, EXROM from `R1.0`, GAME from `R1.1` (T16.2 §6.6) | absent from model and from `cart_map` | the task | **Settled for 00/01/11 from Protovision's API code, 2026-10-03** (`twomegabyter-code-1.0.0/twomegabyter.ca65.s`, `boot.ca65.s`; VICE has no CRT 87, `cartridge.h:278`). `$DE02`: `$01` = Ultimax (`:147-148`), `$00` = 16K (`:167-168`), `$03` = off (`boot.ca65.s:43-44`): EXROM = bit 0, GAME = bit 1 as levels; the swapped polarity would turn `$01` into 8K and the API (RAM-resident because of Ultimax, `:12-13`) would not work. Bank `$DE00` 7 bits (`:65-73`); ROML/ROMH = first/second 8 KiB of the bank, written at `$8000`/`$E000` in Ultimax (`:75-82`, `:108-110`) = `L16`; flash `$AA`@`$8555`, `$55`@`$82AA` (`:152-155`) = `0x555`/`0x2AA`, DQ7 polling, 32 × 64 KiB (`:231-262`). **Mode 10 (8K) is written by no code** (follows from the bit levels, unobserved); the A1-only register decode is the Megabyter's (`megabyter.c:146`), not proven by the API; the device ID bytes are not read by any code. Details: PROP-002 fn 7. Not in the neighbour: Megabyter 86 has bit 1 = EXROM level, bit 0 = GAME **low** when 1 (`megabyter.c:69-74`, table `:103-108`), `R1 = 01` is 16K lines there, not 8K. |
| D6 | IO2 RAM | `IO2_RAM` flag, only EasyFlash | `ram_select = !io2_cen` for **every** non-AR type (`:305`): an IO2 read returns RAM contents even where the real cart has none | the descriptor makes it explicit; reads of `$DFxx` on those types change from RAM data to open bus | **PROP-002 right, model wrong.** Among 5, 19, 32, 60, 85, 86 only EasyFlash registers an IO2 device (`easyflash.c:280-295`: 256 bytes, read always valid, write stores). The other five register nothing at `$DF00`; a read finds no valid source and returns the VIC's last byte (`c64io.c:353-354`), a write is ignored. Model write side is already right (`io2_we_valid` only for 32/33/88, `:138`, `:195`, `:215`), the read side is not (`ram_select = !io2_cen`, `:305`). Descriptor: `io2 {read OPEN, write []}` default, EF `io2 {read RAM, write [RAM]}` (PROP-002 §6.2) replacing the `IO2_RAM` bit. Behaviour change accepted by VICE: `$DFxx` reads become open bus. |
| D7 | Header EXROM/GAME | `H` in the mode table and in reset values (GMod2 bit 6, MD bit 7, EF R1 bit 1) | `cart_slot` passes `f_exrom/f_game` from `m_exrom/m_game`: constants per type, header bits only for Normal (`cart_slot.v:131`) and EF's forced boot (`cart_slot.v:148`) | the app must supply the header lines | **Neither: VICE never reads the header lines for these types.** `crt.c:171-172` parses EXROM/GAME, only the GTK preview uses them (`arch/gtk3/widgets/crtpreviewwidget.c:364-368`). Reset values are constants: EF R0 = R1 = 0 (`easyflash.c:479-480`: EXROM high, mode 0); MD R0 = 0, 8K (`magicdesk.c:129-130`); MD16 R0 = 0, 16K (`magicdesk16.c:132-135`); GMod2 8K (`gmod2.c:246-249`); Megabyter 8K (`megabyter.c:292-296`); Ocean by image size (`ocean.c:154-168`). The EF mode-0 GAME level is the **EasyFlash jumper**, a user resource defaulting to off = GAME low (`easyflash.c:342`, `:76-96`, `:223`), not the header. The model's constants (`cart_slot.v:131`, `:148`) already match; PROP-002's `H` in resets and in the EF mode table is dropped (Rev B: constants; `H` only for type 0). The app need **not** supply header lines except for Normal (`c64_crt.cc:623-630`); T33 §3.2 `INIT_H`/`H` can shrink to type 0. |

Also in VICE, covered by neither document (only for the types above; from the same files):

- **IO1 reads** return open bus for EF, MD, MD16, Ocean, Megabyter; GMod2 answers only while its EEPROM CS is set, bit 7 = data out and bits 6-0 = open bus (`gmod2.c:139-149`). The model returns register contents (D1) and `{ee_dout, 7'b0}` for GMod2 regardless of CS (`c64_cartridge.v:296`). Descriptor: `io1.read` `OPEN`/`DEV`.
- **Bank masking by image size**: MD and MD16 mask the register to the image's bank count (`magicdesk.c:193-214`, `magicdesk16.c:205-226`), Ocean likewise (`ocean.c:211`); EF always 6 bits (`easyflash.c:55-57`), Megabyter 7 (`megabyter.c:84-86`). Banks beyond the image wrap in VICE; our 21-bit ROM address reads whatever lies beyond the image. The app has to narrow `bank.width` per image.
- **Ocean 8K/16K decision differs from the app**: VICE = image size 512 KiB; the app = any chip loaded at `$A000` (`c64_crt.cc:582-588`). A 128 KiB Ocean (all chips at `$8000`) gets 16K lines in VICE, 8K lines from the app.
- **Megabyter ROMH**: not populated in VICE (D5 row); the model mirrors ROML into ROMH (`c64_cartridge.v:236-237`).
- **EasyFlash jumper on** (mode bit ignored, `easyflash.c:88-95`) is a second table, not expressible with `H`; not planned in the app (no jumper source).
- **CRT ID 61**: mainline VICE = MAX Basic (`cartridge.h:252`); Piper's C64MegaCart fork writes it as 61 (`vice-3.1/src/cartridge.h:210`, TRX64 `cart.rs:580`, TRX_CRT_cli `examples/c64megacart.toml:1`). The app maps 61 to C64MegaCart (`c64_crt.cc:72`); ours are 87 and 88 (Alex, 2026-10-03), the fix to 88 is in T33.
- **GMod2 mode**: `$DE00` bit 7 alone changes nothing (00 and 10 = 8K, 01 = off, 11 = Ultimax; `gmod2.c:163-170`), flash write goes through `$E000` (`gmod2.c:192-195`): matches PROP-002.

## 3. Design

### 3.1 Data path

```
app (cart killed)           trx_io page 4 (0x101A4000+0x40..)      cart_slot                   c64_cartridge
configure_cart:              shadow (cpu_clk), 36 bytes      -->    commit toggle, 2 FF   -->   generic block
 write descriptor bytes  --> DESC_COMMIT (CMD b2)                    copy into C64 clock          selects by cart_id==GENERIC
 CART_TYPE = GENERIC (0x1E)                                          at cart_force / reset
 force
```

`CART_TYPE` logic id **0x1E** (free in `c64.h:127-165`, b4:0 values 0x12-0x17, 0x1D, 0x1E unused) means "use the descriptor". `cart_slot` maps it to model id `GENERIC` (8'd255) and loads the committed block at the same
instant it takes `id`/`f_exrom/f_game` (`cart_slot.v:160-170`); a descriptor commit without a following force changes nothing (the block is quasi-static between reset/force). Old apps (no descriptor) keep the legacy path.

### 3.2 Descriptor registers (page 4, offsets 0x040-0x063, write-only, little endian bytes; the app writes all of them, order free)

| Offset | Name | Content |
|---|---|---|
| 0x40, 0x41 | `R0_INIT`, `R1_INIT` | reset values |
| 0x42 | `INIT_H` | b1:0 source (0 none, 1 EXROM, 2 GAME, 3 EXROM&GAME of the CRT header), b2 register (0/1), b5:3 bit, b6 invert: one header-derived reset bit (GMod2, MD, EF, MD2) |
| 0x43 | `H` | b0 header EXROM, b1 header GAME (the app writes `crt_header[CRTHDR_EXROM/GAME]`, `c64_crt.cc:624-629`): value of the mode table's `H` |
| 0x44-0x53 | rules 0-3, 4 bytes each | `[0]` b7 valid, b0 area (0 IO1, 1 IO2), b1 on (0 write, 1 read), b2 target (R0/R1), b4:3 src (0 DATA, 1 ADDR A7:0, 2 CONST); `[1]` match, `[2]` mask (compare `(A7:0 & mask) == match & mask`), `[3]` value for CONST |
| 0x54, 0x55 | `BANK_A`, `BANK_B` | each: b7:4 width (0 = unused), b3:1 lsb, b0 register; concatenated A high, B low, up to 8 bits |
| 0x56 | `MODE` | b7:4 width 0-3, b3:1 lsb, b0 register |
| 0x58-0x5F | mode table 0-7 | b1:0 EXROM (0, 1, 2 = H), b3:2 GAME (same), b4 OFF, b5 W (informational) |
| 0x60 | `LAYOUT` | b1:0 `L8`, `L8_MIRROR`, `L16`, `SPLIT`; b2 `IO2_RAM` (D6); b3 EEPROM bound (the GMod2 93C86 path) |
| 0x61 | `EE_BITS` | CS, CLK, DI bit numbers of R0 (3 bits each, PROP-002 §6.3) |
| 0x62 | `FLASH_MAP` | which window selects flash chip 0/1 and writes through ROMH (the `case` of `:370-390`: GMod2/MegaCart read `$8000` and write through `$E000`, EF ROML = chip 0 / ROMH = chip 1, MegaByter ROML) |
| 0x63 | `CMD` | b0 commit (copy shadow into the C64-clock block at the next force/reset) |

D3 needs more than this layout holds: the mode-table byte (`0x58-0x5F`) is full, `HOLD` and `GAMEW` (valid + level, PROP-002 §5.3) need three more bits per entry (second table byte: 8 more bytes of shadow, +64 FF per copy), the generic block needs a 2-bit register for the held lines and a GAME mux on `phi2 & !rw & a15` (write-cycle GAME), and `BANK` stays 8 bit (the 8 MiB board's 14 bit is not in M1).

The shadow lives in `trx_io.v` page 4 as flip-flops (36 bytes = 288 FF; a LUTRAM could not be read in parallel). The flash part registers (`FL_*`, T16.2) stay as they are.

### 3.3 The generic block (in `c64_cartridge.v`)

Replaces, for `cart_id == GENERIC`, the three `case` statements:

- **Rules:** four comparators on `addr[7:0]` with `io1_cen/io2_cen`, read/write and phi2 (`phi2_h` as the legacy write strobe); source mux DATA / ADDR / CONST into `reg_0/reg_1` (`:249-286`).
- **Fields:** `BANK` = two shifter/mask segments (8 bits), `MODE` = one 3-bit segment, mode table lookup (8 entries) giving `exrom/game/off`.
- **ROM address** from `LAYOUT` (all 8K units, `rom_addr = {unit, addr[12:0]}`): `L8`: ROML = `BANK`, ROMH none; `L8_MIRROR`: ROMH = same unit as ROML; `L16` and `SPLIT`: ROML = `{BANK, 0}`, ROMH = `{BANK, 1}`
  (the image is interleaved per bank as the app stores it, `trx_io.v:18-20`; `SPLIT` differs from `L16` only in the flash chip index of `FLASH_MAP`).
- `io2_we_valid` and `ram_select` from `IO2_RAM`; the EEPROM select (`ee_enable`, `:289`) from the bit; the flash `cs/addr` (`:361-390`) from `FLASH_MAP` and `LAYOUT`; `st_bank` unchanged (`{reg_1, reg_0}`).
- `OFF` -> exrom = game = 1 and the model's ROM/IO selects drop (the legacy `cart_en` gating stays).
- Every field decodes inside one clock of the bus cycle (PROP-002 §7.1): registers and table are flip-flops; the decode is combinational from `addr`, `reg_0/1`.

The descriptor entry for TwoMegabyter (PROP-002 §8.1, T16.2 §6.4): rules `IO1 W A1=0 -> R0 := DATA`, `IO1 W A1=1 -> R1 := DATA` (match 0x00/0x02, mask 0x02); `BANK` = R0[6:0]; `MODE` = R1[1:0]; table 00 `(0,0)` 16K, 01
`(1,0)` Ultimax W, 10 `(0,1)` 8K, 11 OFF; `L16`; `IO2_RAM` 0; flash MX29F016 (T16.2 §6.4).

### 3.4 Migration (each step its own commit, each with the equivalence test of §4.1)

1. Generic block + registers + `cart_slot` path + TwoMegabyter entry (new function; legacy untouched). 2. Descriptor entries for 5, 15, 17, 19, 32, 60, 86, 88, 85/4 (after D1-D4 are settled), the app writes them for those CRT types; legacy `case`s stay but unused.
3. Remove the legacy `case` for each type that has passed the equivalence test and a board run; `cart_slot.v:125-155` shrinks to "GENERIC or AR". The app keeps writing both `CART_TYPE` (kept for the ABI; `GENERIC` for descriptor types) and the descriptor.

## 4. Test plan

### 4.1 Simulation (`designs/c64_basic/sim/cart_unit` extended, `sim/cart`)

1. **Equivalence:** for every legacy type, the same random stream of IO1/IO2/ROML/ROMH accesses (reads and writes, all banks) into the legacy decode and into the generic block loaded with that type's descriptor: `exrom`, `game`, `rom_addr`, `io2_we_valid`, `ram_select`, `dout` source, flash `cs/addr`, `st_bank` equal in every cycle (except the intended D1, D3, D6 changes, which are listed in the test as expected diffs).
2. TwoMegabyter: every bank 0-127 reads its marker in 16K mode (ROML/ROMH), `$DE02` = 0/1/2/3 drive the lines and windows, status polling in 16K mode (T16.2 §9 item 5), flash program/erase through the responder.
2a. C64MegaCart: `$DF00` = `$00`, `$80`, `$C0` and `$40` in every order (`01` after `10` and after `11` as well: the held lines of the descriptor entry), bank register `$DE00` over all 256 banks, reads of `$E000` in mode 11 are the normal map, a write to `$E000-$FFFF` in mode 11 reaches the flash and one to `$8000-$9FFF` does not, IO1/IO2 reads are open bus, no register decode by address (every `$DE00-$DEFF` / `$DF00-$DFFF` address loads).
3. Commit/force: a commit without force changes nothing; reset restores `R*_INIT` and `INIT_H`; header bits `H`.
4. `tests/cart/flash_amd_test.py` and `easyflash_eapi_test.py` pass unchanged on the generic EasyFlash/GMod2/MegaByter/C64MegaCart.
5. Garbage descriptors (all rules invalid, width 0): the cart reads as `OFF`/open, no X in the bus.

### 4.2 Board

B1 EasyFlash CRT (internal), GMod2 (Sam's Journey EEPROM), MegaByter (Yeti Mountain), C64MegaCart through the generic path, each as in T16.2 §10.4; B2 TwoMegabyter CRT with the Protovision flash API example (needs the CRT, Q3); B3 a CRT of each read-only type; B4 `CART_TYPE` only (old app) still works;
B5 C64MegaCart test image `C64ReverseEngineeringMCP/samples/commercial/gp_cars_ptv_c64megacart[ptv].bin`: a raw `.bin` without header, 1 048 576 bytes = 128 banks of 8 KiB (half of the 256-bank / 2 MiB flash; the rest of a cart image is `$FF`, `c64megacart.c:399-402`), 21 banks hold data (0-19 and 24), bank 0 starts with CBM80 at `$8004`, cold start `$8009`. The app builds the CRT from it (8 KiB chips at `$8000`, bank = chip number, `c64megacart.c:421-433`). It is the same game as `gp_cars_ptv_megab8r[ptv].bin` (Megabyter variant); the two files differ only in banks 0 and 3, the bank-switch code. Banks 0 and 3 store to the registers: `LDA #$40 STA $DE00` (`$807E`), `$C0`, `$00`, `$80` to `$DF00` (`$8147`, `$8157`, `$81A8`, `$8246`; bank 3 `$8878`, `$8908`) (byte scan of the two banks, not run): the game uses modes 00, 10 and 11, never 01, and writes no bank bits 13:8 (`$DF00[5:0]` is always 0). The `$C0` store suggests an in-game flash save (not traced). Check: boots from bank 0 in 8K mode; every one of the 21 banks reads its content after a `$DE00` switch; `$DF00 = $80` shows RAM at `$8000`; `$DF00 = $C0` followed by the flash command sequence through `$E000` programs the flash (CWBI flash test) while reads of `$E000` in that mode stay the normal KERNAL; the game loads and plays to its first screen. The same image is the C64MegaCart input of the §4.1 equivalence stream.

### 4.3 Measurement on Gideon's core

None needed for the register semantics. Gideon's own cart logic is open VHDL (`fpga/cart_slot/vhdl_source/all_carts_v5.vhd`, in the app submodule; not on the clean-room list): its `c_megabyter` (`:369-389`) is a table-like per-type block (`bank_bits`, `mode_bits`, `rom_mode`, `serve_*`) and its **variant 1 (TwoMegabyter) drives GAME from mode bit 0 and EXROM from bit 1** (`:381-388`), the opposite of T16.2 §6.6 / PROP-002 §8.1 (EXROM = bit 0, GAME = bit 1; the defect of CWBI §2.3). Our descriptor follows the real cart (T16.2 decision), so a TwoMegabyter title that works on Gideon's core because of the swapped bits cannot exist (its flash mode `$01` is unreachable there). Where our model and PROP-002 disagree (D1-D4) the reference is VICE and the real cart, not Gideon.
The one useful measurement: how Gideon's core treats **IO2 reads on types without IO2 RAM** (D6): a probe PRG reading `$DF00-$DFFF` with a Magic Desk and an Ocean CRT started on his core (RAM contents or open bus?).

## 5. Resources (rough, not measured)

| Block | LUT | FF | BRAM | DSP |
|---|---:|---:|---:|---:|
| descriptor shadow + commit/sync (`trx_io` page 4, `cart_slot`) | 25 | 300 | 0 | 0 |
| C64-clock descriptor block (copy) | 0 | 290 | 0 | 0 |
| rules (4 comparators, source mux, register load) | 45 | 0 | 0 | 0 |
| bank/mode segments, mode table, layout mux, flash map | 85 | 0 | 0 | 0 |
| Generic block, total | about 155 | about 590 | 0 | 0 |
| Removed with the legacy `case`s (step 3), estimate | about -100 to -200 | 0 | 0 | 0 |

The legacy decode's LUT count is not in `docs/lut-budget.md`: read `cart_slot/model` from the next CI package's `utilization_hier.rpt` before step 3. Baseline 25,346 LUT, 77.7 %. Net after step 3: about 0 to +100 LUT; flip-flops +590
(the two copies of 36 bytes; the cost of "no firmware in this path", PROP-002 §7.1). A cheaper variant (one copy: the app writes the block only while the cart is killed, so the shadow could be the live block through a
write-enable gate) saves about 290 FF; it needs the app to never touch the block while a cart runs (Q4).

## 6. Open questions (Alex)

1. PROP-002 (the descriptor format) is untracked in the 1541ultimate checkout. Commit it (where: `docs/specs/` here as the source, or the 1541ultimate repo)? T16.2 §7.2 already points at it. **Decided (Alex, 2026-10-03): here, `docs/specs/PROP-002-cartridge-mapping-descriptor.md` (with PROP-001, the write-back interface). They are our specs now, not proposals to Gideon.**
2. D1-D4: who decides the EasyFlash/Magic Desk IO1 decode, Magic Desk 2, C64MegaCart mode 01 and the Ocean ROMH bank? Proposal: I settle them against VICE's cart sources (reading code, no board) and put the answers into the JSON entries, you review. **Answered (VICE, reviewed by Alex pending), 2026-10-03:** D1, D2, D4 (and D6, D7) from VICE code, see the "Settled" column of §2.3 and PROP-002 Rev B; D3 (C64MegaCart `01`) and D5 (TwoMegabyter polarity) are not in mainline VICE; **settled afterwards (2026-10-03):** D3 from Piper's VICE fork and TRX64, D5 (00/01/11) from Protovision's API code, see the "Settled" column. Open from them: C64MegaCart `01` on the latch-only board (PROP-002 fn 8), TwoMegabyter mode 10 (unobserved).
3. Do you have a TwoMegabyter CRT (or the Protovision API test image) to run on the board? (B2) **Answered (Alex, 2026-10-03): no CRT, but Protovision's TwoMegabyter code 1.0.0 (Chester Kollschen / Knights of Bytes; via Tim-Jakob): `twomegabyter.ca65.s` (flash API, Ultimax), `boot.ca65.s` (boot block), `twomegabyter-examples.ca65.s`, `twomegabyter-highscore-demo.ca65.s`, kept at `~/Downloads/twomegabyter-code-1.0.0.zip` (not in the repo: third-party, no licence stated). B2 builds a test CRT from it with ca65/ld65 (installed) and runs boot + highscore save on the board.**
4. Descriptor storage: two copies of 36 bytes (shadow + live, +590 FF, safe against a write during run) or one with a rule "the app writes only with the cart killed" (+300 FF)? Proposal: two copies. **Decided (Alex, 2026-10-03): two copies.**
5. Behaviour change D6 (IO2 reads of types without IO2 RAM become open bus): accept, or keep returning the RAM for compatibility with whatever depends on it? **Decided (Alex, 2026-10-03): correct it, but generically: the core knows no cart types and no special cases. Whether IO1/IO2 answer with RAM, registers or open bus is a field of the descriptor (cart config), set per type by the app; the core only executes what the descriptor says. Open bus = the last byte on the bus (to be measured on Gideon's core for IO2 reads).**
6. Migration order: only TwoMegabyter through the descriptor now (legacy stays), or migrate the whole table in the same task? Proposal: TwoMegabyter first (step 1), the rest as follow-ups behind the equivalence test. **Decided (Alex, 2026-10-03): everything in T33 (M1). All types move to the descriptor and the per-type `case` code in `c64_cartridge.v` is removed — that is where the LUTs are saved. Equivalence test old vs new per type before the old code goes.**

## Rule: REU or cartridge, never both (Alex)

A real expansion port holds one module. On TRX64-Ultimate an active cartridge (emulated or in the physical port)
switches the REU off, for every type — unlike Gideon's app, which keeps the REU for carts that leave $DFxx free
(`c64.cc:1705`, `CART_PROHIBIT_*`). App change: every cartridge entry prohibits `CART_REU | CART_MAXREU`. The REU's
16 MB of DDR2 become the expansion-port-module area shared by REU and cart images (T51, EasyFlash 3 needs 8 MB).
Deliberate exception to "Gideon measured wins"; who wants both uses Gideon's firmware.

## 8. As built (2026-10-03; branch `t33-decoder`, app `t33-app`)

Where this differs from §3 it replaces it. Evidence: the code named here.

### 8.1 What the hardware does

- **No type in the core.** `cores/icepi/c64_cartridge.v` has no per-type `case` left; `designs/c64_basic/cart_slot.v` no
  `cart_map`, no `lin8`/`gmod2`/`id == 0` special cases. Everything type-specific is the 37-byte descriptor below. The one
  dedicated device is the Action Replay block (spec non-goal), chosen by a descriptor bit, not by `CART_TYPE`.
- **No `GENERIC` CART_TYPE, no commit command** (§3.1 changed): the app keeps writing its own `CART_TYPE` (no ABI change,
  Gideon's code untouched); `cart_slot` turns the cartridge on at a CPU reset when `CART_TYPE[4:0] != 0` and the
  descriptor's VALID bit is set. The live copy (C64 clock) is taken at every CPU reset and every cartridge force, the
  points where `CART_TYPE` was taken before; a descriptor written between them changes nothing (tested, §8.3). Two
  copies as decided (Q4): the shadow in `trx_io` page 4, the live one in `cart_slot`.
- **Register map** (`trx_io` page 4, write only, byte `0x40 + n` = `desc[8n +: 8]`; the field list is the header of
  `c64_cartridge.v`):

| n | Field | Content |
|---|---|---|
| 0, 1 | `R0_INIT`, `R1_INIT` | reset values |
| 2, 3 | `R0_WMASK`, `R1_WMASK` | bits a rule may set (MegaByter's 7-bit R0, EasyFlash's VICE masks `0x3F`/`0x87`) |
| 4-19 | rules 0-3 | as §3.2; the **first** matching rule fires (one per access) |
| 20, 21 | `BANK_A`, `BANK_B` | as §3.2, width 4 bits (0-8) |
| 22 | `MODE` | as §3.2 |
| 23 | `IO` | per area: read `OPEN`/`REG`/`RAM`/`DEV` (2 bits each), write RAM (1 bit each), b6 `io2_we_valid` (PROP-002 §6.2) |
| 24-31 | mode table | b0 EXROM, b1 GAME, b2 `HOLD`, b3 `GAMEW`, b4 `KILL` |
| 32 | `LAYOUT` | b0 ROMH = upper half of the bank's 16 KiB image slot (else the lower: mirror), b1 flash chip linear `{BANK[6:0], ROMH, A12-A0}`, b2 ROMH = flash chip 1, b3 93C86 on R0, b4 Action Replay |
| 33, 34 | `EE` | R0 bit numbers of CS, CLK, DI, and DO's bit position in the IO read |
| 35 | `FLASH_SEL` | the accesses the responder sees: ROML read/write, ROMH read/write |
| 36 | `VALID` | b0 |

- Dropped from §3.2: `INIT_H` and `H` (D7: no type of §8 reads the header; Normal's lines are four descriptors keyed by
  the CART_TYPE variant the app already picks), `OFF` as its own bit (an entry with EXROM = GAME = 1; the registers and
  the IO RAM stay reachable, as VICE's Magic Desk and EasyFlash do).
- Added: `KILL` replaces `cart_slot`'s Normal-only "$40 to $DFFF switches the cartridge off" (the app's boot cartridge):
  Normal's descriptor has a rule IO2 write `$DFFF` → R1, mode R1[7:6], entry 01 = KILL. `GAMEW`: GAME low while the
  CPU (or REU) writes to `$8000-$FFFF` (combinational from R/W and A15, before the PLA in `c64_bus_arbiter.v`); `HOLD`:
  a 3-bit register keeps the last non-HOLD entry's EXROM/GAME/GAMEW.
- IO reads the descriptor does not answer are not served: `cart_slot` returns the last bus byte (`last_bus`), and no
  DDR2 line is fetched. `DEV` drives only the EEPROM's DO bit while CS is set (`dout_mask`), the other bits are open bus.
- Image (DDR2 0x3C00000 as built here, since T60 §7 the app puts it at 0x1000000, in the REU's block, with trx_io ROM_SEL 1; 4 MiB, 16 KiB per bank): 16 KiB per bank. ROML of bank n at n·16K, ROMH at
  n·16K + 8K (`LAYOUT` b0 = 1) or n·16K (b0 = 0). Flash chip address: `{BANK, A12-A0}`, or with b1 `{BANK[6:0], ROMH,
  A12-A0}` (TwoMegabyter: chip address = image offset). EasyFlashXl (8-bit bank, `SPLIT`) fills the 4 MiB exactly.
- Ocean keeps the image's upper half for ROMH (the app stores `$A000` chips there; D4's same-bank mirror is VICE's
  answer for images whose chips are all at `$8000`, which the app runs with 8K lines). The app's 8K/16K decision (any
  chip at `$A000`) is unchanged; VICE's (image size) is not adopted.
- `cores/icepi/c64_top.v`'s own (unused, `ext_cart_en`) cartridge instance gets an empty descriptor.
- **T12's look (`pk_*`, merged from main 2026-10-03):** on the descriptor decode. ROM: `pk_rom_addr` = the image offset
  as the bus access forms it, `{BANK, pk_romh && LAYOUT[0], A12-A0}` (22 bits; `cart_slot` uses it as is, no `lin8`
  remap). IO: the `IO` byte of the area: `REG` gives R0/R1, `DEV` the EEPROM's DO bit (other bits 0) while CS is set,
  `RAM` the cartridge RAM (IO2 at 0, IO1 at 0x100), `OPEN` selects nothing. Action Replay as T12 built it.
  `sim/cart_equiv` checks that the look at the address of every read equals the access (hit, register value, image/RAM
  address), 120 644 reads; one listed exception: the Action Replay switched off by its `$DE00` bit 2, where the bus
  path still answers `$DE00/$DE01` (as the old `cart_slot` did) and T12's AR look does not.

### 8.2 Descriptor data and the app

- Source of truth: `tools/cart_descriptors/*.json`, one per CART_TYPE byte (PROP-002 §9.2 schema plus `cart_type`,
  `wmask`, `io2.we_valid`, `eeprom` bits, `flash.select`, `ar`, `gamew`, `kill`); `tools/gen_cart_descriptors.py` makes
  the app's `software/io/c64/trx_cart_desc.h` (generated, committed in the app), the Verilog function and C header the
  benches use. The flash part, its sector runs and "linear" come from the same entry; `trx_cart.cc`'s own profile table
  and its fixed sector enum are gone (EasyFlashXl's 32 × 64 KiB per chip is data).
- Types (CART_TYPE: CRT): `0x01`/`0x41`/`0x21`/`0xA1` Normal 16K/8K/Ultimax (0), `0x05` Simons Basic (4), `0x08`/`0x69`
  Ocean 8K/16K (5), `0x0A` C64GS (15), `0x88` Dinamic (17), `0x28` Magic Desk (19), `0x11` EasyFlash (32), `0x31`
  EasyFlashXl (232), `0x1B`/`0x3B` Action/Retro Replay (1/36), `0x48` GMod2 (60), `0xA8` Magic Desk 16K (85), `0x0F`
  MegaByter (86), `0x68` C64MegaCart (88), `0x2F` TwoMegabyter (89). CRT IDs live only in `c64_crt.cc`'s table.
- `trx_cart_configure` writes the descriptor (or 37 zero bytes: no descriptor, the cartridge stays off) before the
  flash part. `c64_crt.cc`: 61 = MAX Basic (not emulated), 87 = GMod4 (not emulated), 88 C64MegaCart, 89 TwoMegabyter,
  232 EasyFlashXl (own CART_TYPE `0x31`, TRX bitstream only; banks up to 255, 4 MiB); TwoMegabyter's flash is saved as
  one 16K packet per bank.
- REU rule (above): `configure_cart` adds `CART_REU | CART_MAXREU` to every CRT's prohibit set on the TRX bitstream;
  `effectuate_settings` keeps the REU off while a cartridge (ours, or one in the port) is there. Gideon's bitstreams
  keep his behaviour.
- An app without descriptor writes leaves every cartridge off on this bitstream (B4 is gone): app and bitstream go
  together.

### 8.3 Tests

- `sim/cart_equiv` (new, iverilog): the old `cart_slot` + `c64_cartridge` (from git `2701ce9`) and the new one side by
  side, per type 12 000 random bus cycles (IO1/IO2/ROML/ROMH/RAM, reads and writes, resets), compared every cycle:
  lines, cartridge on, `io2_we_valid`, served/IO value, RAM reads/writes and their address, image address, flash
  select/chip/address, EEPROM inputs, registers. **Run A** (descriptors reproducing the old model,
  `sim/cart_equiv/legacy/*.json`): equal for all 17 types except two old behaviours no descriptor expresses (Normal's
  `$DFFF` switch-off sharing R1 with IO1 writes; GMod2's `$DE00` read answering without CS). **Run B** (the app's
  descriptors): equal except the listed intended changes (D1 IO1 decode, D1/D6 open-bus IO reads, D3 C64MegaCart
  01/11/IO2 RAM, Normal/AR registers). PASSED, 399 051 cycles. Four injected descriptor faults (bank width, EEPROM bits,
  layout, flash select) are all caught.
- `sim/cart_unit` (145 checks, PASS): the old tests on the app's descriptors (C64MegaCart now reads ID/status in mode
  `$00`; EEPROM ready is bit 7 only) plus: open bus and no DDR2 read for IO reads (Magic Desk, C64MegaCart, GMod2 CS
  low), `$DE37`/`$DE04`/`$DE06` decode, TwoMegabyter (128 banks, the four modes, A1 decode, autoselect, ROMH program
  polled in 16K, SA31 erase), C64MegaCart (256 banks through any `$DExx`, HOLD after 00/10/11, `$C0` reads not Ultimax,
  `$E000` write programs, `$9333` write does not), EasyFlashXl (256 banks, erase of bank 200's sector leaves 199), a
  descriptor written without force/reset changes nothing, force starts off, empty descriptor = off and open bus,
  random valid descriptors give no X.
- `sim/cart` (Verilator, `carts.py`, all 16 pass): the old 11 plus Magic Desk 16K, Dinamic, TwoMegabyter, C64MegaCart,
  EasyFlashXl. `tests/cart/twomegabyter/build.sh` (Protovision code from the zip, not committed) in the same bench:
  banks 0-123, modes, one highscore save through Protovision's API (256 programs), read back: PASS.
- Resources: see §8.4.

After the merge with main (`805a5d1`): `cart_equiv` PASS (with the look check), `cart_unit` 145/145, `carts.py`
17/17 with T12's `--peek` comparisons, `sim/statebus` (T12 freeze session) ok; app `t33-app` merged with
`trx64/main` (`449046d0`) builds.

### 8.4 Board (2026-10-03, bitstream `t33-decoder-latest` (CI run 309) in SRAM, app `t33-app` over JTAG)

- `tests/cart/carts_board.py` (the simulation's type tests as CRTs, FTP + REST `run_crt` + `readmem`): 14/14 pass
  (Ultimax, Ocean 8K/16K, Magic Desk, Magic Desk 16K, C64GS, Dinamic, Simons Basic, EasyFlash, EasyFlashXl, GMod2,
  TwoMegabyter, C64MegaCart, Action Replay), flash program/erase included.
- `tests/cart/twomegabyter/tmb_board.py` (Protovision's boot block, flash API, highscore demo): banks, modes, one save
  per start; the second start reads counter 2 from the written-back CRT: PASS.
- `tests/run_cart_tests.sh` (T16.2: EasyFlash EAPI, AMD flash on EasyFlash/GMod2/C64MegaCart/MegaByter, GMod2
  EEPROM): every C64-side check passes; three loads each show write and erase surviving the CRT write-back: PASS. The
  check of the file fetched back over FTP fails on this board (the download stalls after the first segments, as the
  harness notes); `TRX64_SKIP_FILE=1` skips that fetch, the reloads prove the file.
- Real cartridges boot to their title: GP Cars (`gp_cars_ptv_c64megacart`, wrapped as CRT 88 by `tests/cart/bin2crt.py`;
  Protovision intro, then the title picture), Wasteland EF (CRT 232, EasyFlashXl, 4 MiB; intro), Jupiter Fracture,
  Ultima VI EF, Impossible Mission 3+6 (EasyFlash), A Pig Quest (MegaByter).

- `tests/run_vice_testprogs.sh` (VICE cartridge testprogs, baseline `docs/cart-vice-baseline.md` of the old model,
  32 pass / 10 FAIL): **33 pass / 9 FAIL**, judged 7, manual 14, unsupported 15. `ocean-io` now passes (IO1 reads are
  open bus, D1); every remaining failure is one of the baseline's (Action/Retro Replay block 7, `rr-reu` REU
  arbitration, `gauntlet` open I/O), none new.

Re-test after the merge with main (bitstream run 317 in SRAM, app `t33-app` `449046d0`): `carts_board.py` 15/15
(with T12's `action_replay_on`), Jupiter Fracture (EasyFlash) to its title, `tests/freeze_raster/run.sh --freezes 5`
(T12's freeze sessions) 5/5 without a deviation. The cartridge look itself is checked in simulation only: REST
`readmem` reads C64 RAM, not the cartridge, running or frozen, so it cannot show the look on the board.

### 8.5 Resources

Vivado (CI `c64_basic.utilization_hier.rpt`, main `c8753262` → `t33-decoder` run 309):

| block | LUT before | LUT after | FF before | FF after |
|---|---:|---:|---:|---:|
| `cart_slot` total | 685 | 903 | 495 | 741 |
| `cart_slot` own (copy of the descriptor, cache) | 209 | 181 | 348 | 591 |
| `c64_cartridge` own (the decode) | 74 | 128 | 17 | 20 |
| Action Replay | 59 | 30 | 17 | 17 |
| 93C86 EEPROM | 267 | 443 | 70 | 70 |
| AMD flash responder | 82 | 122 | 43 | 43 |
| `trx_io` (descriptor shadow) | 42 | 37 | 231 | 476 |
| design | 25 749 (79.0 %) | 25 994 (79.7 %) | 27 160 | 27 779 |

Net +245 LUT, +619 FF; timing met (WNS +0.032 ns). After the merge with main (T12's look on both sides; main-latest
vs run 317): `cart_slot` 792 → 951 LUT, design 26,325 → 26,511 LUT (+186), 27,536 → 27,981 FF; timing met (WNS
+0.148 ns). The move saves nothing: the old decode was small (74 LUT), and
the generic one pays for variable fields (bit selects of the EEPROM's CS/CLK/DI, the flash window and chip address
now muxed instead of constant per type: EEPROM +176, flash +40). Not trimmed (instruction). Where LUTs could come
back if needed: fixed EEPROM bits (GMod2's are the only ones), one shared field shifter. Yosys (xc7) of `cart_slot`
alone, old vs new: 924 → 1074 LUT, 504 → 750 FF.

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

T33: Generic cartridge decoder per Alex's mapping descriptor

Milestone M1, status ✅. Open (one line, as in the TODO table): –

Decisions: [ADR-0041 EF3 goes into the generic decoder: banking and EasyFlash behaviour only](../adr/0041-ef3-into-the-generic-decoder.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- Task text: Generic cartridge decoder per Alex's mapping descriptor (TREX-PROP-002): banking/decode from configuration instead of fixed types in c64_cartridge.v, so new types (TwoMegabyter first) are app configuration only; then T16.2's per-type code can go (spec first) Spec: docs/specs/T33-generic-cart-decoder.md (register block, generic block, migration; TwoMegabyter first; PROP-002 vs model differences D1-D7). Spec settled 2026-10-03 (docs/specs/T33-generic-cart-decoder.md, PROP-002 Rev B: D1-D7 against VICE, IO1/IO2 behaviour as descriptor fields, two descriptor copies, migrate all types and drop the per-type code). Common CRT IDs agreed with TRX64 (2026-10-03): 87 = GMod4, 88 = C64MegaCart, 89 = TwoMegabyter (PROP-002 Rev C). App fix in T33: c64_crt.cc:72 maps 61 to C64MegaCart — 61 is MAX Basic: map 88 to CART_C64MEGACART, 89 to TwoMegabyter, 61 back to MAX Basic. D3 (C64MegaCart) settled 2026-10-03 from Piper's VICE fork + TRX64: 01 = hold, Ultimax = write window only, whole-page register decode, no IO2 RAM; needs new descriptor fields HOLD and GAMEW (BANK > 8 bit only for the 8 MiB board). D5 (TwoMegabyter) settled for modes 00/01/11, bank, windows, flash commands from Protovision's API code; mode 10 is written by no code. Test images: Protovision TwoMegabyter code 1.0.0 (above) and the C64MegaCart game image gp_cars_ptv_c64megacart (spec §4.2 B5). App differs from VICE on Ocean 8K/16K (app: any chip at $A000; VICE: image size). Test CRT: Protovision TwoMegabyter code 1.0.0. Also EasyFlashXl (232, TRX64-internal 4 MB EF for LN_Engine, Alex wants it): app-config-only proof of the generic decoder; definition requested from TRX64; image fits the 4 MB cart window at DDR2 0x3C00000 exactly
- **Built 2026-10-03 (branches t33-decoder, app t33-app; spec §8 as built):** c64_cartridge/cart_slot have no per-type code; 37-byte descriptor in trx_io page 4 (0x040-0x064), copied at reset/force; JSON per CART_TYPE in tools/cart_descriptors -> app header trx_cart_desc.h + bench files. Sim: cart_equiv old vs new per type PASS (17 types, run A exact, run B only listed intended differences), cart_unit 145 checks PASS, carts.py 16/16 PASS incl. TwoMegabyter, C64MegaCart, EasyFlashXl; Protovision TwoMegabyter test CRT (tests/cart/twomegabyter, from the zip) PASS in sim. App: IDs 61 MAX Basic/87 GMod4/88/89/232, EasyFlashXl as config, REU rule. Board (CI run 309, 2026-10-03): carts_board 14/14, TwoMegabyter Protovision save persists through the write-back, T16.2 write-back suite PASS (EF EAPI, AMD flash EF/GMod2/C64MegaCart/MegaByter, GMod2 EEPROM; TRX64_SKIP_FILE=1 for the stalling FTP fetch), GP Cars (C64MegaCart), Wasteland (EasyFlashXl) and 4 EF/MegaByter games boot; VICE testprogs 33 pass / 9 FAIL (baseline 32/10, ocean-io fixed, no new failure). Vivado: cart_slot 685 -> 903 LUT, design 25,749 -> 25,994 LUT (+245), +619 FF, timing met. Main merged into t33-decoder (T12's look ported to the descriptor decode, cart_equiv checks look = access), t33-app merged with trx64/main; run 317: timing met, +186 LUT vs main; board re-test carts 15/15, EF title, freeze_raster 5/5. Merged to main 2026-10-03 with app trx64/main d1784fe0 (app and bitstream ship together).
