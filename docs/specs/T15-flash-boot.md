# T15 — Boot the Ultimate app from flash

**Status:** spec 2026-10-03, branch `t15-boot`. **Task:** TODO.md T15 (M1: everything
Gideon's core does, booting from flash without JTAG). **Layout:** `docs/c64u-flash-layout.md`.
**Hard rule:** nothing in this item writes the flash. The boot ROM only reads it (opcodes
0x03 and the dummy 0xFF of the preamble); the eventual write of our bitstream and app into
flash is Alex's step, with his explicit OK.

## 1. Goal

Our boot ROM (rvlite, `software/bootrom/bootrom.c` -> `designs/c64_basic/bootrom_pkg.vhd`)
loads the Ultimate app from the SPI flash at 0x220000 and starts it, as Gideon's boot loader
does, so that a C64U with our bitstream at 0x000000 and our app at 0x220000 comes up
without JTAG. A JTAG load (`tools/trxdbg.sh riscv ultimate.bin`) keeps working and always
wins over the flash app. The ROMs (BASIC/KERNAL/CHAR) keep coming from the app through its
ROM windows; nothing changes there.

## 2. What Gideon's boot loader does (read from the code)

`software/ultimate-app/software/portable/riscv/bootloader_u64ii.c` `main()`, built into his
`bootrom_u64ii_pkg.vhd` by `target/u64ii/riscv/bootloader/Makefile`:

1. Flash CS preamble: CTRL <- 03 (CS high), DATA <- FF, CTRL <- 01 (CS low), DATA <- FF,
   CTRL <- 03.
2. Capabilities, board revision; with `CAPAB_SIMULATION` (bit 31) straight to the wait loop.
3. Keyboard scan off, `initializeDDR2()` (his PHY), `init_ext_pll()`.
4. `[0xFFFC] == 0x1571BABE`: clear it, jump to `[0xFFF8]` (the JTAG / warm start path).
5. Else, unless `CAPAB_BOOT_FPGA` (bit 30): flash address `getFpgaType() == 3 ? 0x3C0000 :
   0x220000` (FPGA type = capability bits 29:28; ours are 0x25C40222 -> type 2 -> 0x220000).
   CTRL <- 01, DATA <- 0x03 and three address bytes, then three 32-bit reads of DATA:
   `dest`, `length`, `run`. A 32-bit IO read becomes four byte reads, LSB first
   (`bus_converter.vhd`), so the header words are little endian in flash.
6. If `length != 0xFFFFFFFF`: copy `while (length > 0 && length < 0x180000) { *dest++ =
   DATA_32; length -= 4; }`, CTRL <- 00, then unless ITU button 2 (`ITU_BUTTON_REG` bit 7)
   is pressed jump to `run` (pressed: print "Lock", loop for ever). **The jump happens even
   when `length >= 0x180000`**: the loop does not run, nothing is copied.
7. `length == 0xFFFFFFFF` (erased): "Empty; waiting for JTAG image to boot." and the magic
   loop of step 4 for ever.

There is no checksum and no product header: the image is `ultimate.app` written raw, from
`target/common/rules.mk` `hex2bin -r` of the ELF's ihex: one record `{dest u32 LE, length
u32 LE, run u32 LE, data padded to 4}` (`tools/hex2bin.c write_record`). The updater writes
it unchanged (`update_u64ii.cc:178`, `flash_buffer_at(..., 0x220000, false, ...)`, header
false). The image in the C64U flash today (Gideon's app): `0x00030000 0x0014262C 0x00030000`.

## 3. What our ROM does today

`software/bootrom/bootrom.c`: preamble, capabilities, keyboard scan off, wait for the DDR2
controller's calibration (0x10100100 bit 0; the MIG until OPT-ddr2 §9, now `cal_seq` in hardware), the DDR2 access test at 0x10000, then only the magic loop
(step 4). Its header comment says the flash is not wired; that is out of date: `i_flash` in
`ultimate_sys.vhd` reaches the flash through STARTUPE2 and the app reads it (T01).

`tools/trxdbg.py riscv` holds the RISC-V in reset (JTAG register 9 bit 2), writes the binary
to DDR2, `[0xFFF8]` = address and `[0xFFFC]` = 0x1571BABE, releases the reset. The ROM
restarts and takes the magic. This stays.

## 4. Why not exactly Gideon's flow: two deviations

**4.1 Size limit.** Our app (`trx64/main`, 2026-10-02 build) is 0x181EFC bytes, more than
Gideon's 0x180000. His ROM would copy nothing and jump into empty memory. Ours takes any
length that fits the APPL partition: `0 < length <= 0x1E0000 - 12` (0x1DFFF4), and refuses
(no jump, wait for JTAG) when it does not, instead of jumping anyway. Further checks, all
true for every image `hex2bin -r` makes: `dest` 4-aligned, `dest >= 0x10000` (above the
boot words), `dest + length <= 0x04000000` (the CPU's 64 MB), `dest <= run < dest + length`.

**4.2 Identity gate: the flash app belongs to the flash bitstream.** Test bitstreams are
loaded into SRAM (`tools/load.sh`) while the flash still holds Gideon's bitstream and
Gideon's app. A ROM that boots the flash app unconditionally would start Gideon's app on our
hardware after every SRAM load, seconds before trxdbg can stop it (the CPU runs from the
end of configuration). So the ROM boots the flash app automatically only when the
bitstream in flash is the one that is running:

- Every bitstream we build carries a 32-bit build stamp,
  `BITSTREAM.CONFIG.USR_ACCESS TIMESTAMP` (pins.xdc). The configuration logic gets it as a
  type-1 write to the AXSS register (0x0D) in the bitstream; the fabric reads it through
  `USR_ACCESSE2` (top.v), and the ROM through our IO block: page 0 `BUILD` (0x101A0008,
  4 bytes, little endian), present when `CAPS2` (0x101A0005) bit 1 is set.
- The ROM parses the configuration packets in flash from 0x000000: it looks for the sync
  word 0xAA995566 in the first 4 KB, then walks type-1 and type-2 packets (big-endian
  words). Type-2 data (the frames) and long type-1 data are skipped by starting a new
  0x03 read behind them, so the walk reads a few hundred bytes, wherever Vivado puts the
  AXSS write. It stops at the first AXSS write (found), at a CMD DESYNC (0x0D), at 0x220000
  or after 65536 packets (not found). Our compressed bitstream (main, 2026-10-03) has 8736
  packets and some 60 KB of inline packet data before its DESYNC: about 45 ms of reading.
- Found and equal to `BUILD`: boot from flash as in §2 step 5-6 (with §4.1). Otherwise:
  report both values and the app header, do not jump, wait for JTAG. Gideon's bitstream
  has no AXSS write (dump: 149 packets walked up to its DESYNC at 0x216CC5, none), so on today's C64U no SRAM-loaded build
  ever starts the flash app on its own.
- Once Alex writes our bitstream and our app into flash, the stamps match at every power-on
  and the machine boots as with Gideon's. An SRAM load of any other build over it waits
  for JTAG as today.

A JTAG request (§5, mode FLASH) skips the gate, for an explicit test of the full flash
path.

## 5. Boot flow (ours)

Unchanged from today up to the DDR2 test (preamble, capabilities, keyboard scan off, DDR2
calibration, test_access). Then:

1. `[0xFFFC] == 0x1571BABE`: clear it, jump to `[0xFFF8]` (JTAG load; first, as Gideon).
2. `[0xFFF4] == 0x7158B007` (TRX boot request, written over JTAG with the RISC-V in reset):
   clear it, take mode `[0xFFF0]`:
   - bits 1:0 source: 1 flash at 0x220000, 2 DDR2 at `[0xFFEC]` (a staged copy of a
     flash-format image);
   - bit 8 CHECK: validate and read the whole image, compute its sums, report, do not jump;
   - without bit 8: boot from that source (flash: without the identity gate of §4.2).
   Other values: reported as a bad request, then step 5.
3. `CAPAB_BOOT_FPGA`: wait (Gideon's step 5 condition).
4. Identity gate (§4.2); on a match boot from flash.
5. Wait loop for the magic of step 1, for ever (Gideon's step 7).

Loading (flash or DDR2 source, same code): read the header, validate (§4.1), copy
`ceil(length / 4)` words to `dest`, computing on the way `sum` (32-bit add of the words) and
`xrot` (`x = rotl(x, 1) ^ word`); flash: CTRL <- 00 afterwards as Gideon. A staged image
must not overlap its destination. Then Gideon's Lock check (ITU button 2, the rocker's
freeze within 200 ms of an ESP32 event) and the jump.

The ROM leaves a result block in DDR2 for trxdbg and the bench (32-bit words):

| addr | content |
|---|---|
| 0xFFC0 | 0x54313552 ("T15R"): the block is valid |
| 0xFFC4 | status: 1 booting, 2 checked ok, 3 empty (erased header), 4 header invalid, 5 identity mismatch, 6 locked, 7 bad request, 8 overlap |
| 0xFFC8 | source: 1 flash, 2 DDR2 |
| 0xFFCC/D0/D4 | dest, length, run as read |
| 0xFFD8 / 0xFFDC | sum, xrot (0 when the image was not read) |
| 0xFFE0 | BUILD of the running bitstream |
| 0xFFE4 | AXSS of the flash bitstream |
| 0xFFE8 | bit 0 AXSS found in flash, bit 1 CAPS2 bit 1 (BUILD present) |

Everything also goes to the console (`trxdbg.sh console`). 0xFFC0-0xFFFF is below the app
(0x30000) and the DDR2 test (0x10000).

## 6. Bitstream properties for flash configuration

Gideon's C64U constraints (`constraints/c64u.xdc`, from issue #615) set `CONFIG_MODE SPIx1`
and `BITSTREAM.CONFIG.CONFIGRATE 33`; our `pins.xdc` sets neither (default 3 MHz CCLK, about
6 s to configure 2.2 MB). Taken over into `pins.xdc` with `USR_ACCESS TIMESTAMP`. Checked
on the CI bitstream against Gideon's in the dump: COR0 (Gideon 0x02223FE5), the BSPI write
(Gideon 0x0000000B). Uncompressed size stays under the 0x220000 slot (as Gideon's,
0x217300). An SRAM load over JTAG ignores all three.

## 7. trxdbg

`tools/trxdbg.sh boot ...`, all with the RISC-V held in reset while DDR2 is written, then
released, then the console followed until the ROM's result block appears (and, for a boot,
on as `follow`):

- `boot check`: mode 0x101, the flash image at 0x220000 read and summed by the ROM; trxdbg
  prints the result block and, given `--dump file` (the 16 MB backup dump), compares
  header and sums with those computed from the dump.
- `boot staged file [addr]`: `file` is a flash-format image (`.app`) or a raw `ultimate.bin`
  (wrapped as `{0x30000, size, 0x30000}`, which is what `hex2bin -r` gives for our single
  record); written to DDR2 at addr (default 0x03000000, the app's unused `__updater_start`
  area), mode 2: the ROM loads and starts it with the flash loader's code. trxdbg compares
  the ROM's sums with its own.
- `boot flash`: mode 1, boot from flash without the gate. Not used in this item's tests
  (it would start Gideon's app on our bitstream).
- `boot result`: print the result block.

`riscv file.bin` stays as it is.

## 8. Tests

**Simulation** (`designs/c64_basic/sim/flash_boot/`, ghdl): Gideon's rvlite with our boot ROM
package, Gideon's `spi_peripheral_io` with the flash parameters of `i_flash`, a read-only
flash model that flags every opcode other than 0x03/0x0B/0x05/0x9F/0xFF and loads its
contents from a file, a sparse DDR2 model, stubs for ITU (UART, capabilities, buttons), MIG
status and our page 0 (`BUILD`, `CAPS2`). Images made by a Python script (a fake bitstream
with AXSS before or after a type-2 FDRI, a small RV32I test app that writes a marker).
Cases: gate match -> boot (memory equals image, marker); AXSS before FDRI; mismatch -> no
boot; no AXSS (the real first 4 MB of the dump: Gideon's bitstream and app header) -> no
boot, header 0x30000/0x14262C/0x30000 reported; erased header; length too big; JTAG magic
wins; CHECK sums equal the script's; staged DDR2 boot; staged overlap refused; mode FLASH
boots past a mismatch; Lock button. `trx_io` `BUILD`/`CAPS2` in its own iverilog bench.

**Board** (SRAM bitstream only, no flash writes):

1. Load the T15 bitstream: the console shows the identity mismatch (Gideon's bitstream in
   flash has no AXSS) and Gideon's app header, and the ROM waits; Gideon's app does not
   start.
2. `boot check --dump`: header and sums of the image at 0x220000 equal those of the dump.
3. `boot staged ultimate.app` (our app, `trx64/main`): the app boots from the DDR2-staged
   flash-format image (menu, ROMs loaded, BASIC prompt).
4. `riscv ultimate.bin follow`: the normal JTAG load still works.

## 9. Not in this item

- Writing our bitstream (as `.bin`) and our app (`ultimate.app`) into flash: Alex's step.
  With both written, power-on boots through §5 step 4.
- Gideon's updater, the flash drive, the config area: unchanged. 0x3C0000 (100T) is not
  ours.
- The flash write gate (T44, branch `t44-gate`): the ROM sends only 0xFF (preamble) and
  0x03, both on its pass list.

## 10. As built

Branch `t15-boot` (2026-10-03).

- `software/bootrom/bootrom.c` as in §4-5 (2893 bytes of the 8 KB boot RAM; `.bss` 12
  bytes, stack at the top); `designs/c64_basic/bootrom_pkg.vhd` rebuilt with
  `tools/build_bootrom.sh`.
- `trx_io.v`: page 0 `BUILD` 0x008-0x00B, `CAPS2` 0x03. `top.v`: `USR_ACCESSE2` into it.
  `pins.xdc`: `CONFIG_MODE SPIx1`, `CONFIGRATE 33`, `USR_ACCESS TIMESTAMP`.
  LUT estimate: about 10 (a 4-byte read mux in `trx_io`); no BRAM change.
- `tools/trxdbg.py boot check|staged|flash|result` as in §7.
- Simulation `designs/c64_basic/sim/flash_boot/run.sh` (ghdl, about 3 min): 16 cases, all
  PASS: gate match with AXSS after/before FDRI and after 4400 MFWR/FAR packets as in a
  compressed bitstream (memory equals image, test app runs, sums equal, a length of 4n+2
  copied whole); mismatch, no `BUILD` register, Gideon's real
  bitstream and app from the dump (no stamp, header 0x30000/0x14262C/0x30000), erased
  header, length 0x1E0000-11: no jump; the limit 0x1DFFF4 itself starts loading; JTAG
  magic wins without a flash read; CHECK sums equal the script's; staged DDR2 boot;
  staged overlap refused; FLASH mode boots past a mismatch; Lock; bad request. The flash
  model saw only 0xFF and 0x03 in every case. `sim/trx_io` PASS (BUILD read back);
  `sim/cart_unit` PASS. `sim/pixlayer` fails as on main (1 line error, unrelated).
- Found on the way, simulation only: in Gideon's `rvlite/decode_comb.vhd` the main
  process's sensitivity list lacks `inst_rd`/`inst_rs1`/`inst_rs2`, so in a simulation an
  instruction that differs from the previous one only in those fields (here `mv s2,a0`
  then `mv a0,a3`) is executed with the old register numbers. Synthesis ignores the list;
  the bench analyses a copy with `process(all)`. Any other bench that runs code on rvlite
  needs the same.

- CI build `t15-boot-451f55b2` (run 291): WNS +0.030 ns, 25367 LUT (77.8 %), 60 BRAM.
  Configuration packets in the `.bit`: BSPI 0x0000000B (as Gideon's), AXSS 0x1D34DAF3 at
  offset 0xE9 (in the header, before the first FDRI: the ROM's walk is a few hundred bytes),
  COR0 0x02423FE5. Gideon's COR0 is 0x02223FE5: the two differ only in OSCFSEL (bits
  22:17, ours 0x21, his 0x11), so his flashed bitstream was not built with CONFIGRATE 33
  even though his XDC says so; ours configures from flash with a faster CCLK than his. Open
  for Alex before the flash write (§9): keep 33 or match his OSCFSEL.

Board test 2026-10-03 (C64U, bitstream in SRAM, nothing written to flash):

1. `tools/load.sh c64_basic t15-boot-451f55b2`: console "Build 1D34DAF3 flash bitstream (no
   stamp)", "Not this bitstream's flash: no flash boot.", Gideon's header
   00030000 0014262C 00030000 valid, "Waiting for JTAG image."; status 5. Gideon's app did
   not start. BUILD equals the AXSS of the `.bit`.
2. `boot check --dump c64u_flash.bin`: sum 80C38DEE xrot C96E0866 over the 1.32 MB image at
   0x220000, equal to the dump's; the ROM read it in about 1 s.
3. `boot staged ultimate.app` (our app, `trx64/main` 06de2ea3, length 0x18416C, over
   Gideon's 0x180000 limit): sums equal the file's, "Running", the app reached "All linked
   modules have been initialized", USB and the C64 came up (Ultimate start screen).
4. `riscv ultimate.bin follow`: "Magic: 00030000", the app boots, C64 shows
   "**** COMMODORE 64 BASIC V2 ****". JTAG loading unchanged.

Not testable without a flash write: the automatic boot on a stamp match (simulation only,
cases match_after/match_before/compressed). Needs Alex: writing our bitstream (as `.bin`)
and our `ultimate.app` into flash.

## Flashed (2026-10-03, Alex's OK, Alex at the machine)

- Backup first: full read-only dump `1541ultimate-tools/pinmap/runs/flash-dump/pre-trx64-flash-2026-10-03.bin`
  (SHA-256 cb4c1d7a2a95c25a37345087fb7f95c570e90d81dbc70a26c527e32e681c844c); 0x000000-0x3FFFFF identical to the
  older backup `c64u_flash.bin` (Gideon's bitstream and app).
- Written with openFPGALoader's spiOverJtag bridge (`-c usb-blaster --fpga-part xc7a50tfgg484`), so our gate was not
  involved:
  - 0x000000: `c64_basic.bit` from main c8753262 (CI run 302, WNS +0.456 ns), stream 1,727,492 bytes, COR0
    0x02223FE5 (configuration clock as Gideon's flashed bitstream; `CONFIGRATE 16`), USR_ACCESS stamp 0x1D34FEE5.
    Read back equal. Beyond the stream, the rest of Gideon's longer bitstream remains in the unerased sectors
    (harmless: configuration ends at our stream's end).
  - 0x220000: `ultimate.app` from app d8deef3b (header dest 0x30000, length 0x184154, run 0x30000), 1,589,600 bytes,
    SHA-256 867d2d30f21d67c5dfc4057e1ab10ed17fa68355c22e3c6db47708a00bd93c4d. Read back equal.
- Boot: JTAG reconfiguration from flash, then a real power cycle by Alex: our core comes up with the splash, BASIC
  and the menu, app booted by the boot ROM (REST `trx64: TRX2, C64U`). Gideon's speaker crackle at power-on is gone
  (accepted).
- Open: the BUILD register (0x101A0008) reads 0 over JTAG (`trxdbg trx 0008`) although the stamp gate worked; check
  how JTAG reaches page 0.
- Back to Gideon's firmware: write 0x000000-0x3FFFFF of the dump above with the same tool (`-f --offset 0
  --file-type bin` on that slice), then power-cycle.

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

T15: Boot from flash without JTAG

Milestone M1, status ✅. Open (one line, as in the TODO table): –

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- Task text: Boot from flash without JTAG (docs/specs/T15-flash-boot.md): boot ROM loads Gideon-format app images from 0x220000, JTAG load still wins, auto-boot only when the flashed bitstream carries the running bitstream's build stamp. Flashed 2026-10-03 with Alex's OK: main c8753262 (run 302, COR0 like Gideon's) at 0x000000, app d8deef3b ultimate.app at 0x220000, both verified by read-back; JTAG reconfigure and a real power cycle boot our core with splash, BASIC and menu. Back to Gideon: write 0x000000-0x3FFFFF from pinmap/runs/flash-dump/pre-trx64-flash-2026-10-03.bin
