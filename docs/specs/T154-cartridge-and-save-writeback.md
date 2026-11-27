# T154 — What a game writes to its cartridge or battery RAM goes back to the user's file, in the shared app base, for every core

**Status:** BUILDING (2026-10-29), asked by Alex. D6 (2026-11-27): the flash held only for the copy, the file written async (board S9 open). P1, P2 built and benched; P3 and P4 as listed in section 5. Status check 2026-11-13: BUILDING stays: P1/P2 and EF3 merged 2026-10-29 (52b0addec); board S1 closed as done 2026-11-18 with S2 and S4 (shipped and in use, §5); P3/P4 open. 2026-11-15 sync: the inventory rows 'SNES (branch t142-snes, not on this base)' and 'Neo Geo (branch t143-neogeo...)' are outdated - both merged and carry their own battery-save code (`coreapp/cores/snes.cc` saves/<image>.srm, `coreapp/cores/neogeo.cc` load_save / saves/<game>.sav; Neo Geo save seen on the board 2026-10-31, T143 todo), NOT through `wb_raw_write` (no use outside writeback.cc and tests); NES has no battery path in `coreapp/cores/nes.cc`. P4 is therefore built per core, not through the shared service; S8 (NES and SNES battery game on the board) not recorded.
**Repo:** TRX64U-MultiCore — shared base of the generic core application (`coreapp/src`), the C64 cartridge code
(`coreapp/cores/c64_cart.cc`), per-core descriptors; gateware only where a core cannot report a write today (inventory, §1).
**Number:** T154.
**Origin:** Alex, 2026-10-29 (translated), after the TRX64-FW session answered from code that our bitstream only reports
flash/EEPROM operations and the app writes the .crt as the setting says: "Then the parameter stays and we have to update
the .crt. So code into the app; C64, C128, C16/Plus4, VIC-20 need it, and in perspective the console cores too."
**Depends on:** T104 (generic app, shared base), T148 (core folder), T16.2 (cartridge write-back on the Gideon-based app,
the reference), T104.2 (setting "Save Changed Cartridge"), T151 (kept media).
**Milestone:** M3.

---

## §1 The problem

- **Today, in the generic app, a cartridge cannot keep a save.** `coreapp/cores/c64_cart.cc:230-236` acknowledges a pending
  flash operation unapplied (`C64_IRQ_CAUSE` = 0x30, `C64_CART_CMD_INVAL | ACK`) and switches flash writes off
  (`C64_CART_FL_ENABLE` = 0). An EasyFlash game's save is lost; only the manual "Save Cartridge" (`c64_cart_save`,
  line 393) writes the image as it is. The setting "Save Changed Cartridge" (Off/Ask/Auto) is read by nothing.
- **The reference exists** in the Gideon-based app (TRX64-FW session, from code, 2026-10-29; `apps/ultimate`):
  - gateware reports: `c64_cartridge.v` decodes the flash command sequence, `cart_slot.v:237-238` toggles the EEPROM/flash
    event, `trx_io.v` sets `IRQ_CAUSE` b4 (flash op pending) / b5 (EEPROM written) and holds the op busy until the ACK;
  - the app applies the program/erase into the RAM image and marks it dirty (`trx_cart.cc` `trx_cart_irq` ~186-220,
    `note_change_isr` ~175), decides "changed" (`trx_cart_pending` :293) and "due" (`trx_cart_due` :298: quiet time, max
    age, or a requested flush), and writes the file (`c64_subsys.cc` `crt_trx_writeback` :197, `crt_save_in_place` :128);
  - callers: the background task (:358, TRX branch 374-383, only on AUTO), before reset/reboot/power off/new cart
    (:218-232, only on AUTO), at menu open (:269-300: OFF nothing, ASK yes/no, "no" drops the change via `trx_cart_forget`).
- **Other cores** (C128, C16/Plus4, VIC-20, consoles) have no write path at all. Whether their gateware reports a write
  (flash command, EEPROM, battery RAM) is not known yet: inventory per core is part 0 of the build.
- Parts: **P1** a shared write-back service in the app base (dirty tracking, Off/Ask/Auto, quiet time, flush before reset,
  eject, core change and power-off request, write to a temp name and rename); **P2** the C64 flash/EEPROM event path ported
  from the reference onto P1; **P3** each further core declares its writable media and its change source; **P4** console
  battery RAM to a save file beside the game.

## §2 Scenarios

```gherkin
Scenario: S1 EasyFlash save on Auto
  Given "Save Changed Cartridge" = Auto and an EasyFlash .crt whose game saves to flash
  When  the game saves and the machine is quiet for the quiet time
  Then  the .crt on the medium holds the new banks (read back, compare with the RAM image); the rest of the file is unchanged

Scenario: S2 Ask
  Given Ask and a changed cartridge
  When  the menu opens
  Then  the question names the file; Yes writes it (S1 check), No drops the change and does not ask again for it

Scenario: S3 Off
  Given Off and a changed cartridge
  Then  the file is never written (mtime and bytes unchanged after reset, eject, core change)

Scenario: S4 before reset / eject / core change on Auto
  Given Auto and a change younger than the quiet time
  When  reset, cartridge eject or "Reload core" is chosen
  Then  the file is written first, then the action runs

Scenario: S5 read-only medium
  Given the .crt on a write-protected medium or a read-only mount
  Then  one note says it cannot be saved, once per change; nothing is lost from the RAM image until the cart is removed

Scenario: S6 negative control
  Given the event path disabled in a bench build
  Then  S1 fails (the file is unchanged)

Scenario: S7 per core (after the inventory)
  Given C128 / C16-Plus4 / VIC-20 with a writable cartridge type their gateware reports
  Then  S1-S4 hold for it; a core whose gateware reports nothing is listed in §5 with what it would need

Scenario: S8 console battery RAM (P4)
  Given a NES/SNES game with battery RAM
  When  it saves
  Then  <game>.sav (name per §3a Q2) beside the game holds it; it is loaded again at the next start

Scenario: S9 the game saves on while the file is written (D6)
  Given Auto and an EasyFlash game that saves in steps with pauses (Wasteland, TREX CRT, on trx64u)
  When  a pause reaches the quiet time and the file is written while the game goes on saving
  Then  the game's flash operations wait only for the copy of the changed blocks, not for the file; the save takes as long
        as on trx64g; the file holds the state at the copy, and what the game wrote meanwhile goes into the next write
```

| # | Solves | Test | Where |
|---|---|---|---|
| S1-S6 | P1, P2 | coreapp bench with a fake flash event source; ue2emu with an EasyFlash image (if the emulator models the TRX cart events, else board) | CI, emulator |
| S1, S2, S4 | P2 | one EasyFlash game save on the board | board |
| S7 | P3 | per core after the inventory | bench, board |
| S8 | P4 | one NES and one SNES battery game | board |
| S9 | D6 | coreapp bench (a program during the file write: acknowledged at once, in the next file, not in this one; controls `wb_read_after_release`, `wb_changed_from_old`); Wasteland's save on trx64u against trx64g | CI, board |

## §3 Decision

- **D1 (Alex, 2026-10-29)** The setting "Save Changed Cartridge" (Off / Ask / Auto, default Ask) stays and governs every
  write-back path; the app updates the user's .crt.
- **D2 (Alex, 2026-10-29)** The code lives in the app, shared: C64, C128, C16/Plus4, VIC-20 use it, the console cores later.

Proposed:

- **D3** One service in the base (`coreapp/src/writeback.{h,cc}`): a core registers a writable medium (file path, image
  location in DDR2, size, how to rebuild the file: .crt with header and chip packets, raw .sav), and calls `mark(range)`
  when its change source fires. The service owns Off/Ask/Auto, the quiet time and max age, the flush hooks (reset, eject,
  core change, menu open, power-off request) and the safe write (temp file, then rename).
- **D4** The C64 path is the reference's, ported: IRQ_CAUSE b4/b5, apply program/erase into the image, ACK, `mark`.
  `C64_CART_FL_ENABLE` is set for a writable type instead of 0.
- **D5** A core whose gateware reports no write gets no fake: it is listed with what it would need (part 0 inventory).
- **D6 (Alex, 2026-11-27)** The flash waits only for the copy; the file is written while the machine runs. Alex's board observation:
  saving in Wasteland (TREX CRT) on trx64u is about ten times slower than on trx64g, "das muss ein dirty bit lazy write sein, async".
  Cause, read in the code: `write_now` held the flash (`hold`) for the whole rebuild of `NAME.tmp` (the old file read, every image packet
  written, sync), so every quiet gap of 2 s inside a save, or the 30 s age, stopped the game's next flash operation for the length of the
  file write; the reference (`apps/ultimate` `c64_subsys.cc` `crt_trx_writeback`) held only for `take_snapshot`, a copy of the image into
  RAM, and wrote the file from the copy. Asked "may the flash be held only for the copy, the write async?", Alex: "Ja bitte".
  Built: a medium may have `snap` / `unsnap` (`WbMedium`, last fields); `write_now` takes the changes and calls `snap` under the hold, then
  ends the hold and writes. The .crt medium (`wb_crt_snap`) copies the changed 8 KiB blocks (at most 1 MiB, `WB_CRT_SNAP_MAX`) to the heap;
  the writer takes a changed block from the copy and an unchanged one from the old file, which holds it as DDR2 does (the load puts every
  packet's bytes into the image as they are). So the file is one state, the one at the copy, as before. Held over the write as before: a
  flush with the EEPROM changed (aux), a medium without an old file or with `may_create`, more than 1 MiB changed (a chip erase of an
  EasyFlash 3), no memory for the copy, and the library media (`svc_wb.h` unchanged). The core lock stays (the menu waits for the write).

## §3a Open questions

- **Q1** Answered 2026-10-29 (Alex: "Ja"): the reference's `TRX_QUIET_MS` / `TRX_MAX_AGE_MS` values are taken unchanged.
- **Q2** Answered 2026-10-29 (Alex: "a"): console saves go into the core folder's `saves/` (T148), not beside the game.
- **Q3** Answered 2026-10-29 (Alex: "Ja"): T151 "keep media" re-inserts the cartridge as its file is; on Auto the change
  is in the file already, on Ask/Off an unsaved change is gone, as the user chose.

- **Q4** Answered 2026-10-29 (Alex: "behalten" - "keep"): the first write-back keeps the original once as `NAME.bak`
  beside the file, as the reference does.

- **EasyFlash 3 is in (Alex, 2026-10-29):** "No! We defined that we have an 8 MB window that is shared with the REU's
  16 MB" (translated; ADR-0010 one 16 MB expansion-port block, T51 EF3 8 MiB window). Our C64 gateware reports it:
  `cores/trx64/trx_io.v:375` XPORT_CAPS = 0x03 (b0 8 MiB window, b1 EF3 banking). The generic app must start CRT type 90
  in that window and write it back like every other flash cartridge; its "4 MiB for every type" was behind the decision.

- **The write holds the core lock (Alex, 2026-10-29):** "We do not change that under any circumstances, it stays. Changes
  while the image is written make no sense" (translated). The menu waits. Flash operations were held during the whole write; since D6
  (Alex 2026-11-27) only while the changed blocks are copied, so the file is still one state.

## §4 Out

- Power-off by the rocker (4 s): the ESP32 cuts the power hard, the app gets no warning; a change younger than the quiet
  time is lost there (Alex, 2026-10-29: "whoever does that is out of luck, simple as that", translated). Menu and REST
  power-off are flushed.

- C128 write-back: waits for a C128 "Run cartridge", its own spec later (T155; Alex, 2026-10-29).

- Writing back to a cartridge in the real expansion port (that is trex_crt, T104.3).
- Save states (machine state).

## §5 Build notes

(filled in while building)
Built on branch `t154-writeback` (from `t104-2-board`), 2026-10-29. Commits: P1 `965ab92a5`, P2 `44cdd7a8a`, this section.

### Part 0, the inventory (what each core's gateware REPORTS, read in the RTL on 2026-10-29)

| Core | Writable medium it reports | Where (file:line) | Missing for write-back |
|---|---|---|---|
| C64 (`trx64`, the monolith's block) | flash program / sector erase / chip erase (IRQ_CAUSE b4 with the EVT word, held busy until the ACK), GMod2 EEPROM written (b5, dirty flag + WR_COUNT, 2 KB window) | `cores/trx64/trx_io.v:231` (cause bits), `:237` (WR_COUNT), `cores/trx64/cart_slot.v:183` (responder) | nothing - P2 |
| C64 reference (`c64ref`), `c64u` | the same events, same bits | `cores/c64u/c64u_io.v:223`, `cores/c64ref/c64ref_io.v` (same block) | they run Gideon's own application (`trx_cart.cc`), not this base; nothing to build here |
| C128 | the same events: the core carries the cartridge model and `c64u_io` | `cores/c128/layer2_c128.v:390` (`c64u_io cwio`), `:398` (`cart_ee_tgl`), `cores/c64u/c64u_io.v:223` | the application has no cartridge start (`coreapp/cores/c128.cc` header: no cartridge port in the descriptor). A "Run cartridge" for the C128 (C64-mode .crt, the 32 KB C128 packets, menu rows, a register check on the board) has to exist first; `c64_cart.cc` is written against `CoreIo` and does the rest. Not built: it is a feature of its own, not write-back |
| C16 / Plus/4 (`c264`) | nothing: the cartridge halves are read only arrays, `cart_active` is tied 0 | `cores/c264/layer2_c264.v:342`, `cores/c264/rtl/trx/c264_regs.v:81` (cause = frame, vsync, flip only) | a cartridge the machine writes does not exist in this core; nothing to report |
| VIC-20 (`vc20`) | nothing: RAM_EXT blocks are RAM (lost at power-off by design), the `$A000` block can be read only | `cores/vc20/layer2_vc20.v:342`, `cores/vc20/rtl/trx/vc20_regs.vhd:87` | same: no writable cartridge medium |
| NES | nothing: PRG RAM is a 8 KB block RAM inside the core, the core's `save_written` / `SAVE_out_*` ports are unconnected | `cores/nes/trx/nes_core.v:197`, `:204`, `cores/nes/trx/nes_mem.v:106`, `cores/nes/trx/nes_regs.v:78` | a readback of the PRG RAM for the app (window or DDR2 write-through) and a "written" event (a cause bit or a counter); until then P4 has a service but no source |
| Atari 8-bit | nothing: cartridge types are read only | `cores/atari800/trx/a8_regs.vhd:133` | none needed |
| Apple II, CPC, ZX Spectrum, ZX Next, Amiga | disks and tapes (own write-back paths), no cartridge | - | out of this task |
| SNES (branch `t142-snes`, not on this base) | battery RAM lives in DDR2 at `CART_BASE + 0x820000`, written through; the only counter is `WRITES`, which counts WRAM writes too | `t142-snes: cores/snes/trx/snes_regs.v:28`, `snes_mem.v:5` | a battery-RAM-only write flag or counter (a few flip-flops); or Alex decides on hashing the DDR2 image - that would be a polled fake, not a report (D5) |
| Neo Geo (branch `t143-neogeo`, not on this base) | backup RAM / memory card changed: STATUS b3, cleared by SAVE_ACK | `t143-neogeo: cores/neogeo/trx/neo_regs.v:12-13`, `:19-20` (images are moved by MV_CMD) | the application side only: the core and its descriptor are not on this base; a `WbRaw` medium over MV_CMD's save, `wb_mark` from STATUS b3 |

So P3 is: C64 (done), and for every other core a listed reason. No core got a fake.

### What was built

- **P1** `coreapp/src/writeback.{h,cc}`, `writeback_crt.{h,cc}`. The setting "Save Changed Cartridge" (Off / Ask / Auto, default Ask) has the apply
  operation `writeback.mode` (was `settings.noop`); the service reads the setting at the start and follows the apply. Auto: a task polls every 250 ms
  (`writeback.poll`, on a `PollTask` under the core lock) and writes when the change is quiet for 2 s, 30 s old, or a flush was asked for; a failed write
  is told on the screen and tried again after 10 s with the change kept. Flush first (Auto): `boot_cold_reset`, `boot_warm_reset`, `boot_reload`,
  `boot_restart` (Reload core), Return to host, `u64.power_off` / `u64.power_cycle`, `c64_cart_remove`, and a new cartridge. The action runs after a failed
  flush, with the note. Menu open (machine held): Off nothing; Ask - the first thing on the screen is the question (`MenuCtl::writeback_check`, the confirm
  row: RETURN saves, the left arrow keeps the file, an F key leaves it unanswered and it comes back at the next open); Auto writes now. A file that cannot be
  written (write protected, read only, `/flash`) is told once per change and the image keeps the change.
  The file: the new file is built under `NAME.tmp` from the old file (header, every packet that holds no image data and what follows the last packet are
  copied byte for byte; image packets get the image's bytes (D6: a changed block from the copy, an unchanged one from the old file); a changed block no packet holds gets a packet of its own unless it is erased), the file as first
  found is kept once as `NAME.bak` (the reference's rule, see open point 1), then `NAME.tmp` becomes `NAME`. The flash operations wait (the interrupt is off)
  while the changed blocks are copied; the file is written from the copy and the old file with the flash going on (D6; held over the write in the cases D6 lists).
- **P2** `coreapp/cores/c64_cart.cc`: the TRX interrupt handler (IRQ_CAUSE b4/b5) applies program (clears bits only), sector erase (64 KB of one chip) and
  chip erase to the image in DDR2 with the chip's geometry, reports the blocks, ACKs; `FL_ENABLE` and the part's magic/mask/ID registers are set for
  EasyFlash, Megabyter, TwoMegabyter, EasyFlashXl, C64MegaCart and GMod2 (the flash parts are copied into `c64_cart_desc.h` byte for byte; EasyFlash 3 follows below). GMod2 (CRT 60) now starts: its EEPROM packet goes into the window (blank when the file has none) and is written back through a WR_COUNT seqlock.
  `CoreIo` got `eeprom_read/eeprom_write` (`hw.cc`: `EEPROM_BASE`).
- **EasyFlash 3 (CRT 90, CART_TYPE 0x12)**, ported from the Gideon-based app (`apps/ultimate` `aa2ef5dc`, `software/io/c64/`), no code copied:
  start refused unless XPORT_CAPS has b0 and b1 and the block is shared (`trx_cart.cc:99-103` `trx_ef3_present`, `:48-49`; `c64_crt.cc:231`); type 90 -> 512 banks
  (`c64_crt.cc:101`, `:455`); the 8 MiB window for type 90 only (`c64.h:545`, `c64_crt.cc:567`; here `g_image_max`); descriptor row 0x12 and the MX29LV640EBT part
  (`trx_cart_desc.h:38`, `:57`, byte for byte, 8 x 8 KB boot + 127 x 64 KB sectors); the 23-bit events with EVT_HI (`trx_cart.cc:200`, `:208`); the chip-order to image
  permutation `{slot, bank_hi, ROMH, bank_lo, A12-A0}` -> `{slot, bank_hi, bank_lo, ROMH, A12-A0}` (`trx_flash_geom.h:18`); erase spans incl. the chip erase of all
  8 MiB (`trx_flash_geom.h:47`); the dirty map is 1024 blocks (`WB_BLOCKS_MAX`) and the .crt keeps its 1024 packets (the writer rewrites every image packet; more than 1 MiB changed, such as a chip erase, is written with the flash held, D6).
  Bench: type 90 refused without XPORT_CAPS, the whole image in the window, descriptor registers, a program in slot 2 / bank_hi 3 / bank_lo 5 / ROMH through EVT_HI, a
  64 KB sector (8 banks of one half), a boot sector, write-back (1024 packets, exactly the programmed and erased halves changed), chip erase and its file; 5 negative
  controls (permutation, window, capability check, EVT_HI, chip erase span). `tools/ef3_image.py` was not run on the written file (it needs the Python tool on a real
  dump; the bench builds its own image). Board: not run.
- **P4 (service side)** `WbRaw` + `wb_raw_write`, `wb_save_path` (Q2), `wb_raw_load` (the save at the next start); a medium may not exist yet
  (`may_create`). No console core reports a battery write (inventory), so nothing calls it yet.

### EasyFlash 3 parity table (reference: `apps/ultimate` `aa2ef5dc`, `software/io/c64/`; generic app: `coreapp/cores/`)

| Feature | Gideon-based app | Generic app | Bench (`test_writeback.cc`) |
|---|---|---|---|
| Presence: XPORT_CAPS b0 and b1, else type 90 refused | `trx_cart.cc:99-103`, `c64_crt.cc:231` | `c64_cart.cc:536-540`, `c64_regs.h` C64_CART_XPORT_CAPS | refused with b0 only; control `wb_ef3_caps` |
| CRT 90 -> CART_TYPE 0x12, 512 banks, slot-major bank = slot*64+bank | `c64_crt.cc:101`, `:455`, `:653-655` | `c64_cart.cc:58` | whole image in the window |
| 8 MiB window for type 90 only, 4 MiB for the rest | `c64.h:545`, `c64_crt.cc:567`, `trx_cart.h:33-34` | `c64_cart.cc:18`, `:540` (`g_image_max`) | bank 511 ROMH; control `wb_ef3_window` |
| Descriptor row 0x12 and MX29LV640EBT part (unlock $AAA/$555, mask $FFF, ID $C2/$CB, 8x8 KB + 127x64 KB) | `trx_cart_desc.h:38`, `:57` | `c64_cart_desc.h:41`, `:60` (byte-compared with the original) | magic/mask/ID registers |
| Responder enable and 23-bit events (EVT + EVT_HI) | `trx_cart.cc:200`, `:208` | `c64_cart.cc:322`, `:329` | slot 2 program; control `wb_ef3_evt_hi` |
| Chip order -> image permutation | `trx_flash_geom.h:18` | `c64_cart.cc:239` | program, 64 KB sector (8 banks of one half); control `wb_ef3_order` |
| Sector / boot-sector erase, faithful chip erase of 8 MiB | `trx_flash_geom.h:47`, `trx_cart.cc:216` | `c64_cart.cc:249`, `:283` | boot sector, sector, chip erase; control `wb_ef3_chip_erase` |
| Write-back of all 1024 packets (dirty map 1024 blocks, 8 MiB) | `trx_cart.cc:61`, `c64_crt.cc:1066`, `c64_subsys.cc:197` | `writeback.h:16`, `writeback_crt.cc`, `c64_cart.cc:173` | 1024 packets, exactly the changed halves differ; chip-erased file |
| Image EAPI left as the image has it (no patch for type 90) | `c64_crt.cc:653-655` | nothing patches the image | - |

Same in both and deliberately different: Gideon's app regenerates a flash cartridge's file as the whole chip (`c64_crt.cc:491-560`, packets for banks the file left
out); this port keeps the file's packets and adds one only for a changed block that is not erased (S1: "the rest of the file is unchanged"). An EF3 file already has
all 1024 packets, so for it the result is the same.

### EasyFlash 3 parts that are in no shipped Gideon-based app and no gateware here (listed, not built; no new questions)

| Part | Where it exists | Why it cannot be built in the generic app |
|---|---|---|
| KERNAL replacement mode (`$DE0E`, `kovr` in PLA / arbiter / turbo), `$DE0F` modes 0-7, the EF3 menu coming up from the system area, the cart-made C64 reset, buttons A/B/C | gateware `cores/icepi/c64_ef3.v` and PLA changes on `park/ef3-full` (TRX64-Ultimate); app side `trx_cart_ef3_button` writing `EF3_BTN` (+034) and the actions "EF3: Menu / Reset Slot / Reset, No Boot" in `c64_subsys.cc` on `park/ef3-full-app` (`73e031b`) | `cores/trx64/trx_io.v` has no +034 register (only `:375` XPORT_CAPS = 3), `cores/trx64` and `vendor/icepi/c64_cartridge.v` have no `c64_ef3`, no `kovr`, no reset request; an app action would write a register that does not exist. T51 scope item 2 and ADR-0041 took them out of the bitstream on 2026-10-07 |
| Start slot | not in `aa2ef5dc`; T51 scope item 5: no start-slot feature, the emulation starts slot 0 | descriptor `R2_INIT` = 0 in the gateware |

What it takes if Alex wants them: the gateware back (`c64_ef3.v` or the descriptor's KERNAL rules, `rst_req`, a button register, XPORT_CAPS unchanged) in `cores/trx64`
as its own bitstream task; the app side is then three menu rows and one register write, already written on the park tag.

### What is proven

- `tests/coreapp/test_writeback.cc` (245 checks) with 27 negative controls in `run.sh` (each breaks one thing and the bench must go red): S1 (program, sector
  erase, chip erase, bank 1 ROML / bank 2 ROMH, the other chip untouched, header and `.bak` equal to the original, new packets), S2, S3, S4 (reset, eject, a
  new cartridge, reload, power-off request), the maximum age, S5 (read only: once per change, written when the card is writable again), a full card, GMod2
  EEPROM including a torn copy, the setting's operation, S8's service side. **S6**: the bench's fake gateware still raises every event; with the marks of a
  program or an erase removed (`wb_s6_*`) S1 goes red. `tests/coreapp/test_writeback_menu.cc` (24 checks, 4 controls): the question in the real `MenuCtl`.
- The whole application builds for the board (`tools/build_coreapp.sh`'s make in `u64-build-env:ci` on the builder, 2026-10-29): `coreapp.bin` 1,401,728 bytes
  of the 1,966,068 the image may have (`tools/check_app_size.py`). It was not started on a board.

### What is not proven

- **ue2emu cannot show S1/S2/S4 for the cartridge**: its TRX block answers page 0 only; "Page 4 ... read 0" and CAPS b4 (the cartridge block) stays off
  (`u64-emulator/crates/ue2-core/src/devices/trx.rs:12`, `:45`), UE2 raises IRQ_CAUSE vsync and flip only (`crates/ue2emu/src/monitor/devices.rs:44`). The C64
  path needs the cartridge block, so this is for the board: one EasyFlash game save (spec table), with the bitstream's real responder, the ISR timing (an
  EasyFlash program is one interrupt per byte), the SD card write time and the D-cache-less store ordering (`fence` + read-back, as the reference); closed as done 2026-11-18 (end of §5). The
  EEPROM window address (`EEPROM_BASE`, `0x1004C000`) on the MultiCore `trx64` core is read from the RTL comment, not measured.
- The menu question is proven in the bench on the Atari descriptor, not on the C64's new-UI screens (ue2emu / `tests/ui` BDD).
- The existing C64 benches (`test_c64_cart.cc`, `test_menuctl.cc`) do not link or pass on this branch (they need the board sources `diag`, `monitor`,
  `trx64u_ops`, ... and the menu test has 25 older failures: root names); `gen_menu_rec.py --check` already fails here ("a bad file type action": the golden
  list is older than the menu). Not caused by T154; the golden got the two new operations. `stub_ui.cc` lets the menu bench link.

### Open points

1. **`NAME.bak`** is the reference's rule (the file as first found is kept once). It leaves a second file beside the user's .crt; it can go if Alex does not
   want it.
2. **C128 Run cartridge** (inventory): the gateware reports, the app cannot start a cartridge.
3. **EasyFlash 3 parity** (Alex 2026-10-29: rebuild 1:1 what works with Gideon's firmware): see the parity table below. Everything the Gideon-based app at
   `aa2ef5dc` does for EF3 is ported. The features of T51's full build are in the Gideon-based app only on the parked tag `park/ef3-full-app` (commit `73e031b`) and in
   the gateware only on `park/ef3-full` (TRX64-Ultimate `cores/icepi/c64_ef3.v`); neither is in `aa2ef5dc` or in `cores/trx64` here, so nothing of them "works with
   Gideon's FW" today. The coordinator's note said the chip erase is refused; `aa2ef5dc` (`trx_flash_geom.h:47`) and T51 D5 do the faithful 8 MiB erase, and so does this port.
4. A game that reprograms flash in the first seconds after a Return-to-host or a rocker power-off (4 s, no warning to the application) loses the last change
   younger than the quiet time on Auto: the power-off request of the menu and REST is flushed, the rocker is not.
5. The Auto task runs under the core lock, so a long write (a 1 MiB image on a slow card) delays menu operations for its duration.

**2026-11-18 - board steps closed as done (shipped and in use):** S1, S2, S4 (EasyFlash save goes back to the .crt: Auto, Ask, before reset / eject / core
change; the same board step as T192.3's "write-back question") - Alex 2026-11-15: "Under A there is nothing I would not class as DONE! That has been in the
builds for days/weeks, we had bugs and re-tests in part"; Alex 2026-11-18, shown these steps as open: "that is old stuff that has been running for ages, we
had this already 3-4 days ago" and "tick it off in the TODOs/specs, otherwise it shows up again with some agent" (translated).
