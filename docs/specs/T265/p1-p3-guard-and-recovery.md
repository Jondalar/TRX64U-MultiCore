# T265 P1-P3 design: guard, recovery program, writers

Research only, nothing changed. Code read in `.claude/worktrees/t239.3-pkgdiag` (head 87d3f85cd = origin/t239.3; the files
below are identical on origin/integration). Spec: `docs/specs/T265-host-update-app-guard.md` (origin/integration).
"Measured" = read from a file or a dump; "estimate" = derived, to be measured while building.

## 0. Verdicts

| Point | Verdict |
|---|---|
| Boot ROM room | 2,892 of 8,192 bytes used (measured). CRC-32 + record + second load path: about +0.5-0.7 KiB code, +1 KiB table in .bss. Fits. |
| Boot time | +0.1 s with a byte table (estimate; the bench measures it). The SPI read of a 1.15 MB app alone is about 0.37 s. |
| Recovery size | Estimate 110-160 KB against 258,048 bytes (0x1E0000-0x21EFFF). Reading USB in 252 KiB is realistic. |
| Clocks | The recovery needs no clock synthesizer and no HDMI init for USB or SD. It needs I2C for the USB hub. |
| Flash drive | Factory C64U: 11,145,216 bytes free. Worst case with the host's own files: about 7.8 MB free. The 1.15 MB copy fits, and so does a 2.3 MB transient (temp file + rename). |
| Package | `hostrecovery.app` is a copy of the stamped `hostapp.app`, made in `ci/package.sh` after the stamp. The release bundle takes it from `--app`. |

## 1. Boot ROM

### 1.1 How it is built, how full it is

- `host/bootrom/Makefile:4-5` builds with `-march=rv32i -Os -ffunction-sections -fdata-sections`, and `:11-20` does
  gcc, objcopy binary and `mkpkg.py`. `mkpkg.py:6-10` writes 2048 LE words into `host/bootrom_pkg.vhd` and refuses
  more than 8,192 bytes. `tools/build_bootrom.sh` runs `make` by hand in the `u64-build-env` container; the .vhd is
  committed. The large cores keep their own copies (`cores/<core>/mono/host/bootrom_pkg.vhd`, 13 files). The
  Makefile writes only `../../host/bootrom_pkg.vhd`, so S8 holds by construction.
- Image today: the last non-zero word of `c_bootrom` is word 722, so 2,892 bytes are used (measured from
  `host/bootrom_pkg.vhd`).
- The 8 KiB is RAM: `bootrom_8k.vhd:22-23` has 2048 words and both ports write (`:51-53`, `:73-75`).
  `linker.ld:16-23` puts .bss in the same 8 KiB and the stack at its top. Nothing checks that .bss and the stack
  stay apart. Add `ASSERT(__bss_end <= __stack_top - 1024)` to `linker.ld`.
- Flash read: `flash_read_at` (`bootrom.c:159-168`, opcode 0x03 only), `flash_word_be` (`:170-177`) for the
  bitstream walk, and `src_word` (`:237-245`). For flash, `src_word` returns `SPI_FLASH_DATA_32`, 4 bytes
  assembled LE (the `{dest,length,run}` header is read this way, `:279-281`).
- DDR2 load: `load()` (`:259-327`). It reads the header, runs `header_ok()` (`:247-254`), then copies
  `ceil(length/4)` words to `dest` and keeps sum and xrot (`:307-313`). `boot()` (`:329-344`) jumps.
- The flash SPI runs at 25 MHz SCK (`host/ultimate_sys.vhd:823-836`, `g_init_rate 1`). A 1,154,580-byte app is
  289K words at 32 SCK each, so about 0.37 s of wire time at minimum (estimate).

### 1.2 Record (0x21F000, one 4 KiB sector; the rest of the sector stays 0xFF)

| Offset | Field |
|---|---|
| +0 | magic `"TRXG"` (0x47585254) |
| +4 | len: bytes covered = 12 + ((hdr.length + 3) & ~3), the image as at 0x220000 |
| +8 | crc: CRC-32/zlib over those bytes |
| +12 | chk = ~(magic ^ len ^ crc) |

- A partly programmed page fails `chk`, and the boot ROM then treats it as "no record" and boots as today. The cut
  analysis in 3.4 shows this is the safe reading.
- Why CRC-32 works word by word: the reflected CRC-32 of a LE word equals the byte CRC of its 4 bytes in flash
  order. The boot ROM does `crc ^= w` and then 4 byte steps. It gets the same value as `fw_crc32`
  (`flashfw.cc:13-35`) and Python `zlib.crc32`.
- 100T (`bootrom.c:381` app at 0x3C0000): derive record = appl − 0x1000 and recovery = appl − 0x40000. Do not use
  fixed constants.

### 1.3 Changes to bootrom.c

1. **Constants** (`:74-75`): `FLASH_RECOVERY = appl − 0x40000`, `FLASH_RECORD = appl − 0x1000`,
   `MAX_REC_SIZE = 0x3F000 − 12 − 4`. Add `HOST_IFACE = 1`: the host-app interface this bitstream provides, the same
   number as `txu.h:35` `TXU_IFACE_PROVIDED`. The bitstream carries the boot ROM, so no register is needed. A lint
   compares the two constants (1.6).
2. **Status values** (`:70-71`): `ST_GUARD = 9` (record mismatch, recovery started) and `ST_GUARD_STOP = 10`
   (mismatch, no usable recovery).
3. **Clear stale blocks**: after `MIG_CALIBRATED` (`:369`), before the JTAG magic check (`:372`), write
   `RES(R_MAGIC) = 0` and guard-block magic = 0. Today a JTAG magic load leaves the block of the earlier boot in DDR2
   (main writes RES only from `:387`). With this change the host app never takes a stale "booted from flash".
4. **CRC in load()**: one global `crc`. Start at ~0, add the 3 header words at `:279-281`, add each data word in the
   loop at `:307-313` (`crc ^= w; 4 x crc = tab[crc & 0xFF] ^ (crc >> 8)`), finish with ~. Build the table once into
   .bss (256 x 8 bit steps, about 15K instructions).
   - Cost per word: byte table about 28 instructions, nibble table (64 B rodata) about 56, bit-serial about 224.
   - Estimate for 289K words at 100 MHz: about 0.08, 0.16 and 0.65 s. The bench measures it (END line: `n_us`,
     `tb_flash_boot.vhd:262`).
5. **Guard in the normal branch** (`:417-426`, after the stamp check passed):
   1. Read 4 words at `FLASH_RECORD`. Record valid = magic and chk agree.
   2. Not valid: `boot(..., DO_BOOT)` as today. The CRC is still computed and reported (S6).
   3. Valid: first `load(..., DO_HEADER)`. If the header is empty (`:289`), invalid, or 12 + padded length ≠
      rec.len, the result is a mismatch and the 1.5 MB foreign app is never read.
   4. Otherwise load with CRC. Equal: jump as today (`:339-341`). Different: mismatch.
6. **Mismatch path**:
   1. Write `RES(R_STATUS) = ST_GUARD` and the guard block (1.4).
   2. `load(MODE_SRC_FLASH, FLASH_RECOVERY, DO_BOOT)` with the `MAX_REC_SIZE` limit.
   3. Read the trailer word behind the recovery data: the recovery image carries its own CRC (2.6), so the record
      does not track it.
   4. If the header is invalid or the CRC differs: `ST_GUARD_STOP`, print it on the console, and fall into
      "Waiting for JTAG image" (`:428`). That covers S4 "none anywhere".
   5. Otherwise jump to the recovery's `run`. The right-button lock (`:334-337`) also applies here.
7. **JTAG requests** (`:402-416`): no guard; the request is the developer's explicit choice. Fill the guard block for
   information, so `trxdbg.sh boot check` shows record versus flash.
8. **Size**: about +0.5-0.7 KiB code + 1 KiB .bss table. Afterwards about 3.5 KiB code, 1.1 KiB .bss, and 3.4 KiB
   left for the stack (estimate).

### 1.4 New DDR2 blocks below the result block

The result block is full: `R_FLAGS` = index 10 = 0xFFE8, and 0xFFEC is `TRX_REQ_ADDR`. The bench marker is at 0xFFB0
(`run.py:45-52`). The host app links from 0x30000 (`hostapp/linker.x:7`) and nothing else uses 0xFF60-0xFFBF (grep of
hostapp, coreapp: only 0xFFF8/0xFFFC in `cfw.h:23-24`).

- **Guard block, 0xFF80-0xFF9F, written by the boot ROM**:

  | Offset | Field |
  |---|---|
  | +0 | magic |
  | +4 | verdict: 0 no record, 1 match, 2 app empty, 3 header or length mismatch, 4 CRC mismatch |
  | +8 | rec.len |
  | +12 | rec.crc |
  | +16 | flash len |
  | +20 | flash CRC (0 when not read) |
  | +24 | `HOST_IFACE` |
  | +28 | recovery status: 0 not tried, 1 started, 2 slot empty, 3 header invalid, 4 CRC bad |

- **Recovery block, 0xFF60-0xFF7F, written by the recovery program**:

  | Offset | Field |
  |---|---|
  | +0 | magic |
  | +4 | medium: 1 flash copy, 2 flash `.new`, 3 SD, 4 USB |
  | +8 | stage address (0x03000000) |
  | +12 | stage length |
  | +16 | stage CRC |
  | +20 | copy CRC equals record: 0/1 |
  | +24 | reserved |
  | +28 | chk |

- `tools/trxdbg.py:115-118` (`BOOT_STATUS`) gets 9 and 10. `boot_result` (`:357-370`) prints both blocks.
  `boot_request` (`:377`) clears 0xFF60-0xFFFC, not only 16 words from 0xFFC0.

### 1.5 Bench `host/sim/flash_boot`

**What it does today.** `run.sh:26-35` analyses Gideon's rvlite, his SPI block, `host/bootrom_pkg.vhd`, the flash
model and `tb_flash_boot.vhd`. The CPU runs at 100 MHz (`tb:94`). `run.py` builds the flash images: bitstream
(`:79-95`), app (`:55-63`), `_flash` (`:312-317`). It checks the result block, the console, the marker 0xFFB0, the
memory at the destination, and the flash counters. The flash model refuses every opcode except 0x03/0x0B/0x9F/0x05/0xFF
(`flash_rom_model.vhd:116-119`), so "the boot ROM writes nothing" is checked in every case. The model holds 4 MiB
(`:25`), so 0x1E0000 and 0x21F000 are inside it. All 15 existing cases have an erased record area, so they become the
"no record = as today" cases (S6) with no change.

**New in run.py:**

- `record(app_bytes)` with `zlib.crc32`.
- `recovery_image(dest, MARK_REC)`: `test_app` prints "REC", writes `MARK_REC` to 0xFFB0, and has the CRC trailer.
- Cases:

  | Case | Setup | Expected |
  |---|---|---|
  | `guard_match` | record of app | boots, verdict 1 |
  | `guard_foreign_crc` (S2) | record of app A, flash holds app B of the same length | no `MARK_FLASH`, `MARK_REC`, status 9, verdict 4, console names the mismatch |
  | `guard_foreign_len` | B has another length | verdict 3, fewer than 2000 bytes of B read |
  | `guard_empty_app` | header erased + record | recovery; this is an interrupted app write |
  | `guard_bad_chk` | chk wrong | boots as today |
  | `guard_no_recovery` | slot erased | status 10, `end == waiting` |
  | `guard_rec_badcrc` | recovery trailer wrong | status 10, no jump |
  | `guard_time` | 64 KiB app, record match | reports `n_us` against the `match_after` time |

**tb change:** after the RES line (`tb_flash_boot.vhd:265-270`), write a `GUARD` line (0xFF60-0xFF9F). Shift the parse
in `run.py:129-135` by one line.

**Negative control (S2: "the same image with the guard's check removed starts it"):**

- `Makefile` target `noguard` builds `-DTRX_GUARD=0` into `host/sim/flash_boot/bootrom_noguard_pkg.vhd`. It is
  committed like `host/bootrom_pkg.vhd`, and package name and contents keep the same form.
- `run.sh` analyses it a second time with `ghdl --workdir=build/noguard` and elaborates `tb_flash_boot` there.
- `run.py` case `guard_off` runs the `guard_foreign_crc` flash on the noguard build and expects `MARK_FLASH`.
- The source must be one `#if TRX_GUARD` around the decision (5 and 6 in 1.3), nothing else. Then the control tests
  exactly the check.
- `ci/sims.deps:77` already lists the `host/sim/flash_boot` directory.

### 1.6 Consistency checks (lint, no new infrastructure)

- `tools/txu_pack.py selftest` already compares constants with `txu.h`. Add `HOST_IFACE` from `bootrom.c`, and
  record / slot addresses against `flashfw.h`.

## 2. Recovery program (flash 0x1E0000-0x21EFFF, 258,048 bytes)

### 2.1 What it has to do (D3, D6)

1. Init: FreeRTOS, I2C plus the USB hub, the USB stack, SD.
2. Read the record (for information only, 3.4).
3. Find a valid image, first match wins:
   1. `/Flash/trx/hostrecovery.app` (read-only).
   2. `/Flash/trx/hostrecovery.new`, which covers the cut between unlink and rename (3.4).
   3. `hostrecovery.app` in the SD root.
   4. `hostrecovery.app` in the root of each USB volume as it enumerates.
4. Stage the image at 0x03000000 and CRC it. Valid = `header_ok()` rules (`flashfw.cc:555-571`) and file size ==
   12 + padded length.
5. Write the recovery block. Copy `length` bytes to `dest` (0x30000). Stop the nano (`NANO_START = 0`), disable the
   ITU IRQs and `mie`, then jump to `run`.
6. If nothing is found: keep polling the media, so a stick put in later works, and print why on the console. JTAG
   still works: `trxdbg load` resets the CPU first (`tools/trxdbg.py:647`), so the boot ROM takes the magic path.
7. It writes nothing: no flash program or erase, no FAT write.

### 2.2 Where the code comes from (all already in `host-app/software`, Gideon's GPL tree)

| Need | Source (host app, `Makefile` line) | Note |
|---|---|---|
| Start-up, IRQ, tick | `portable/riscv/crt0.S`, `riscv_main.c`, `itu.c`, FreeRTOS `tasks/queue/list/timers/port/heap_4` | USB needs FreeRTOS: `usb_base.cc:191-204` creates a mutex, queues and a task; `:349` creates the event task; `usb_scsi.cc:33` creates a mutex |
| USB host | `io/usb/usb_base.cc`, `usb_device.cc`, `usb_hub.cc`, `usb_scsi.cc` (bulk-only mass storage), `nano_minimal.nan` (`SRCS_NANO`, at most 2,048 bytes of nano RAM, `usb_base.cc:328-339`) | no HID, no AX88772, no CBI |
| Hub and PHY init | `io/usb/usb_hwinit.cc:144-169` `initialize_usb_hub()`: ULPI reset, I2C channel 3V3, USB2513 (0x58) or USB2503 config, buffer enable; needs `Hw_I2C_Driver` (`hw_i2c_drv.cc`, `i2c_drv.cc`) | do not link `u64ii_init.cc`; write an own `custom_hardware_init()` of two lines (I2C + hub). `u64ii_init.cc:114-138` also does the audio codec and the HDMI/keyboard expanders, which are not needed |
| SD | `io/sd_card/sd_card.cc`, `sdio.cc` (SPI through `i_sd`, `ultimate_sys.vhd:805-821`), `sdcard_manager.cc` (polling task, `:18-41`) | |
| Flash drive | the FAT region 0x400000-0xFE8000 (`w25q_flash.cc:53`), 4096-byte sectors | must NOT call `init_flash_disk()`: when the probe fails it formats (`blockdev_flash.cc:177-181`). An own read-only attach |
| FAT | `chan_fat/ff14` (ff.c 27,048 B code in the host app) | see 2.3 |

### 2.3 Two ways to link the file layer

- **R1, Gideon's layer as is.** FileManager, FileDevice, Partition, FileSystemFAT, path. `usb_scsi.cc:121-134`
  registers volumes with the FileManager, and the recovery opens files like the host does.
  - FatFs must be the full configuration (`chan_fat/full/ffconf.h`): `filesystem_fat.cc` references f_write and
    f_mkfs, so `FF_FS_READONLY=1` does not link.
  - "Writes nothing" then rests on code: no write call in `recovery.cc`.
  - Least new code; about +35-45 KB.
- **R2, lean.** FatFs direct with an own `ffconf.h`, placed first on `-I` like `hostapp/ffconf.h:4-7`:
  `FF_FS_READONLY 1`, `FF_FS_MINIMIZE 1`, `FF_USE_LFN 1`, `FF_MAX_SS 4096` (flash) / `FF_MIN_SS 512`,
  `FF_FS_EXFAT 1`, `FF_VOLUMES 3`.
  - diskio over three readers: an own flash reader (opcode 0x03 only, about 100 B like `bootrom.c:159-177`),
    `sd_card`, and the UsbScsi BlockDevice.
  - `usb_scsi.cc`'s FileManager glue (`:120-134`, `:140-148`, `:186-188`) gets a recovery hook. That is a patch to a
    vendored file.
  - Writes are absent by construction: read-only FatFs links no disk_write, and no flash program opcode exists.
- `hostrecovery.app` is not 8.3. LFN must be on in both: `chan_fat/tiny_ro/ffconf.h:102` has `FF_USE_LFN 0` and
  `FF_MAX_SS 512` (`:196`), so Gideon's tiny_ro config cannot read the flash drive or the name.
- Recommendation: R1 for the first build (deliver fast), R2 if the size check gets tight. Both fit (2.4).

### 2.4 Size against 258,048 bytes

**Code sizes per compile unit** come from the DWARF aranges of the host app ELF
(`scratchpad/pkg160/hostapp.elf`: .text 910,808, .rodata 238,692, .data 5,064, app 1,154,580 bytes; rv32im -Os,
`hostapp/Makefile:9-12,244-245`):

| Part | Bytes of code |
|---|---|
| crt0 + riscv_main + itu + small_printf + memory_wrap | 5,072 |
| FreeRTOS (tasks 7,260, queue 3,796, timers 1,672, port_asm 848, heap_4 768, list 224, port 104) | 14,672 |
| USB (usb_base 7,192, usb_device 3,344, usb_hub 3,316, usb_scsi 5,448, usb_hwinit 552) | 19,852 |
| nano image | ≤ 2,048 |
| I2C (i2c_drv 2,328, hw_i2c_drv 400) | 2,728 |
| SD (sd_card 2,596, sdio 720, sdcard_manager 952) | 4,268 |
| flash (w25q_flash 3,200, flash 748, blockdev_flash 1,452), or an own reader of about 100 | 5,400 |
| ff.c full 27,048 (R2 read-only: estimate 13-16K) + ffunicode 396 + ffsystem 128 | 27,572 |
| R1 file layer (filemanager 12,512, disk 2,080, filesystem_fat 2,708, path 2,520, file_device 1,504, pattern 1,224, init_function 1,112, mystring 1,096, filesystem_root 1,000, file_system 920, file_partition 716, partition 556, diskio 356, blockdev 64) | 28,368 |
| recovery logic | estimate 3,000-5,000 |
| newlib and libgcc (not in the aranges) | estimate 10,000-20,000 |

- **Sum of code:** R1 about 125-135 KB, R2 about 85-95 KB. Add rodata at the host app's ratio (26 %) and data:
  **R1 about 160 KB, R2 about 110 KB, against 258,048 bytes.**
- **Upper bound, measured:** the TRX installer `update_trx` has USB, SD, flash drive, FAT, FreeRTOS, a C64 screen UI,
  HID keyboard, ESP32 and backup/restore. Its code and data in front of the blobs is 298,820 bytes
  (`dist/pkg-v0.2.1-227926a3/trx64host_update_0.2.1.ue2`: manifest at image offset 298,820). The recovery drops all
  the UI, HID (`usb_hid` alone is 16,560), ESP, audio, video and `update_trx.cc` itself.
- **Verdict:** USB fits with a margin of more than 90 KB.
- **Build check:** `tools/check_app_size.py recovery.app 0x3EFF0` (the 0x3F000 slot less the 12-byte header and the
  4-byte trailer; the tool already takes a limit argument).

### 2.5 Clocks, HDMI

None are needed, from code:

- `cpu_clk` comes from an MMCM on `clk50_ibuf` (`host/top.v:237`), which is `RMII_REFCLK`, 50 MHz from the
  Ethernet PHY (`top.v:42,158-160`).
- `ULPI_REFCLK` and `HUB_CLOCK` (24 MHz) come from `pll_usb` on `clk50` (`host/usb_phy.v:57-71`).
- `i_sd` and `i_flash` run on `cpu_clk`.
- The boot ROM writes no clock synthesizer; its only I2C write is `I2C_SCAN_ENABLE = 0` (`bootrom.c:364`).
- So: no `init_ext_pll`, no HDMI. Only I2C for the hub (2.2). The recovery shows no picture, as the spec asks
  (console only).

### 2.6 Link address, cache, hand-off

- **Link address:** link the recovery at 0x01800000 (LENGTH 0x00800000; small FreeRTOS heap) and stage the app at
  0x03000000 (`FW_STAGE_APP`, `flashfw.h:24`).
- **Why that address is safe for the icache:**
  - `rv_icache` hits only when the address is below 32 MiB (`host/rv_icache.vhd:44,92-97,273`). The tag is bits
    [24:13], 8 KiB direct-mapped (`host/cache_cfg_pkg.vhd`).
  - Code at or above 0x02000000 would run without hits, with a DDR2 read per fetch, which is slow. Its fills would
    also alias the address minus 32 MiB, and for 0x0203xxxx that is the app at 0x0003xxxx.
  - At 0x01800000 the lines are unique. The app's text (0x30000-~0x110000) was never fetched by the recovery, and the
    host's code loaders sweep the icache before running new code (`coreapp/src/libboard.cc:28-55`; trampoline sled
    in `coreboot.cc`).
- **Data cache and DMA:**
  - The dcache is write-through. CPU copies to 0x30000 update any line they hit, and DMA writes are snooped
    (`ultimate_sys.vhd:566` `dc_snoop_valid`).
  - The stage at 0x03000000 is outside the dcache window, so USB DMA into it needs nothing.
- **Hand-off:** the recovery copies and jumps itself. It does not go back through the boot ROM's staged-DDR request,
  so `RES(R_STATUS) = ST_GUARD` and the guard block stay intact for the app.
- **Recovery image:** Gideon record `{dest 0x01800000, len, run}` + data + a CRC-32 trailer over header and data.
  The boot ROM checks it (1.3 step 6).

## 3. Host app

### 3.1 Knowing how it was started (read once, first thing in `ultimate_main`, `hostapp.cc:66`)

`guard_capture()` copies 0xFF60-0xFFFF into a static and clears the three magic words.

| Case | Condition | My image is |
|---|---|---|
| Booted from flash | `RES[0] == 0x54313552 && RES[1] == 1 (ST_BOOTING) && RES[2] == 1 (flash)` + guard block valid | len/CRC = guard flash len/CRC |
| Started by the recovery | `RES[1] == 9 (ST_GUARD)` + guard and recovery block valid | stage at 0x03000000, len and CRC from the recovery block |
| Anything else | JTAG load or staged boot | the guard does nothing |

The app cannot CRC itself in RAM: `.data` lies in the image and changes (`linker.x:106-136`, single region). So it
always takes "my CRC" from its loader.

### 3.2 Self-provisioning (booted from flash; verdict "no record" or "match")

- **Where:** a background job after the first frame. Anchor in `hostui.cc:4649-4710` `startup()`, after `build_ui()`
  (`:4708`). Run it through the HcJob framework like `UpdateJob` (`host_core_flow.cc:647-825`), so other flash
  writers (update, legacy stage) wait for it.
- **Steps:**
  1. **Recovery program** (if the host app carries it, Q1): compare 0x1E0000 with the embedded image (rules.mk
     `SRCS_BIN` objcopy rule, `rules.mk:119-124`). If different: write it with `write_range` header-last plus
     readback (`flashfw.cc:283-324`).
  2. **Copy:** stamp `/Flash/trx/hostrecovery.stm` ("len crc", the `legacy.h:14` `fullcore.stm` pattern). If it is
     missing, the copy is missing, or either differs from (guard len, CRC):
     1. Read 0x220000 for len bytes into a heap buffer. The CRC must equal the guard CRC, so the flash still holds
        me.
     2. Check free space (`hostfs.cc:173-182` `free_bytes`). If it does not fit, put a menu note and delete a stale
        copy (3.4).
     3. Write `hostrecovery.new` (`hostfs_save` with read-back, `hostfs.cc:69-94`), unlink `.app`, rename
        (`hostfs.cc:231`), write `.stm`.
  3. **Record:** if it is missing or ≠ (len, CRC): erase 0x21F000 and program one page.
- **Cost:** about 282 sectors of erase and program on the flash drive, about 16 s for the copy (estimate). It happens
  once per new app, in the background.

### 3.3 Repair path (started by the recovery, D3/D6)

- **Where:** before `direct_phase()` (`hostui.cc:4660`). If started by the recovery, skip `direct_phase` (no autostart)
  and run a repair job as the first job once `trx_lv_open()` (`:4664`) is up.
- **Fit check, as `txu_check` (`txu.cc:104-124`):** `TXU_IFACE_NEEDED` (`txu.h:36`) == guard `HOST_IFACE`.
  - Different: refuse, write nothing. The page names both numbers and asks for the `hostrecovery.app` of the
    installed release.
  - The host key (`hostapp.cc:35-42`, `cor_host_key`) is shown, not compared: an app-only `.txu` also accepts
    another key.
- **Steps:**
  1. Page: "The flash application was replaced by another firmware's updater (guard verdict, record versus flash
     CRC); started from <medium>."
  2. CRC the stage and compare it with the recovery block.
  3. `fw_image_seal_parts(app = stage)` (`flashfw.cc:530-580`), then `fw_write_parts(FW_PART_APP)`
     (`flashfw.cc:393-473`: app header-last + readback).
  4. Copy from the stage, unless the medium was the flash copy and its CRC equals the stage.
  5. Record.
  6. Result page: "repaired; the next power-on starts normally".
- **Variant:** if the flash app already equals the stage (the cut window in 3.4), write only copy and record.

### 3.4 Writer order and the cut windows (P3, spec order: app, copy, record)

**In `.txu`:** `UpdateJob::run` (`host_core_flow.cc:647`) calls `fw_write_parts` at `:788`. On `wr.ok && w_app`:

1. Copy from `img.app` (staged).
2. `fw_write_record(fl, len, fw_crc32(img.app, img.app_len))`, a new function in `flashfw.cc`, pure over `Flash`.

Show both as two more phases on the progress page (`up_run_page`). The recovery program is not written here (Q1).

| Cut during or after | Flash app | Copy | Record | Next power-on |
|---|---|---|---|---|
| app write | half (header erased) | old | old | verdict 2/3 → recovery → old copy → old app repairs (update lost, board alive) |
| copy `.new` write | new | old `.app` | old | mismatch → old copy → old app repairs |
| unlink → rename | new | only `.new` | old | recovery takes `.new` → new app → flash == me → record |
| after rename | new | new | old | mismatch → copy new → new app → record |
| record write | new | new | garbled | chk fails = no record → boots new app → writes record |

- **Consequence 1, the recovery must accept a copy whose CRC ≠ record.** If it insisted on a match, the
  "after rename" row would end in the JTAG wait. It reports the match flag only.
- **Consequence 2, a stale copy must not survive a new record.** If the new copy cannot be written (drive full), the
  writer deletes the old copy before the record. Otherwise the recovery would start an older app, which writes
  itself back: a silent downgrade.

**Slot fixes, all required:**

| Place | Change | Why |
|---|---|---|
| `flashfw.h:16` `FW_BIT_SLOT` | 0x220000 → 0x1E0000 | `clear_tail` (`flashfw.cc:423`) and the blank-slot readback (`:450-462`) would otherwise erase or flag the recovery and the record on every host write. Also the AXSS walk limit (`:80,107`) |
| `tools/txu_pack.py:33` `SLOT[KIND_HOST]` | 0x220000 → 0x1E0000 | the reader `txu.cc` follows `FW_BIT_SLOT` |
| `tools/trx_pkg.py:24` `BIT_MAX` | → 0x1E0000 | installer packages refuse a bigger host |
| `vendor/gideon/.../update_trx.cc:43,401-404` | may stay | its `clear_slot_tail` erases 0x1E0000-0x21FFFF on an install. The first boot then re-provisions recovery, copy and record (if Q1 = the host app carries the recovery). Its restore of Gideon backups needs the 0x220000 slot (the Commodore bitstream ends at 0x217300, measured) |

**S5 unit test:** `tests/hostapp/test_guard.cc`. Extend the `FakeFlash` of `tests/hostapp/test_txu.cc:54-83` with
"stop after N operations", and use the stub FS for the copy. For every N, the state after the cut must give
"boots a complete app" or "recovery finds a valid image", never "boots a half app".

- The decision is one header shared by `bootrom.c` and the test (`host/bootrom/guard_rule.h`, pure C, no I/O), so
  the test runs the boot ROM's own rule.
- This is the negative control the spec allows for flash and persistence.

### 3.5 The fit data the app needs

- The running host's interface version is not readable today. The app assumes its own constant
  (`host_core_flow.cc:660-662`).
- With `HOST_IFACE` in the guard block (1.3 step 1), every app can check it, including one started from a stick of
  another release.

## 4. Free space on the flash drive

**Geometry, from code and the dump:**

- The flash drive is 0x400000-0xFE8000, 12,484,608 bytes (`w25q_flash.cc:53`).
- The factory C64U dump (`~/Development/1541ultimate-tools/pinmap/runs/flash-dump/c64u_flash.bin`, 2026-09-30) reads
  as FAT12, 4096-byte sectors, 1 sector per cluster, 3,041 clusters = 12,455,936 bytes of data.

**Commodore's files on a factory board (measured):** 1,270,245 bytes in 320 clusters:

| Folder | Bytes |
|---|---|
| CARTS | 582,832 |
| HTML | 257,079 |
| ROMS | 249,856 |
| APPS | 178,727 |
| CONFIG | 1,751 |

Free: 11,145,216 bytes. Biggest free run: 11,083,776 bytes.

**What the host itself writes (from code):**

| File | Size | Source |
|---|---|---|
| `/Flash/legacy/fullcore.bit` | the full bitstream of the firmware started, in ONE run of clusters (`f_expand`, `legacy.cc:104-121`); Commodore/Gideon 50T: 2,192,128 bytes (factory bitstream ends at 0x217300, measured) | `legacy.h:12-14` |
| `/Flash/menu.snap` | ≤ 1 MiB (`MS_LIMIT`) | `menusnap.h:14` |
| `/Flash/menu.rows` | ≤ 32 x (2,048-byte icon + entry), about 66 KiB | `menurows.h:13-15` |
| `/Flash/trx/host.cfg` | ≤ 8 KiB | `hostcfg.h:15` |
| `/Flash/boot.cfg`, `/Flash/trx/ui.token`, `fullcore.stm` | small | |
| per core (standalone builds only) | `/flash/<id>/*.cfg` | `coreapp/src/fsif.h:19-27` |

Worst case for the host: about 3.33 MB in clusters.

**Balance on a factory board:**

- 1.31 MB (Commodore) + 3.33 MB (host) = 4.64 MB used, so about 7.8 MB free.
- Copy: 1,154,580 bytes = 282 clusters. With temp + rename the transient is 564 clusters, 2.31 MB. **It fits.**
  Afterwards about 6.7 MB stay free.
- The 2.19 MB run that `fullcore.bit` needs is only at risk on a nearly full drive.
- If the host app carries the recovery program (Q1), the app and the copy grow by about 110-160 KB.

**Caveat:** the drive is shared with the user's own files under Commodore's firmware, so these are factory numbers
only. The app checks `free_bytes` before writing and says so in the menu when the copy does not fit (3.2).

## 5. Package and release (S10)

- **`ci/trxapps.sh`, part host (`:125-129`):**
  - Build `host-app/target/u64ii/riscv/hostrecovery` before `hostapp` (`host-app/` is already in the key, `:51`).
  - Size check like `:151-156`: `check_app_size.py recovery.app 0x3EFF0`.
  - Copy `recovery.app` and `recovery.elf` into the hand-over (`:157-158`).
- **`ci/lane.sh`, host build (`:77-80`):** copy them to `$HANDOFF/host` as well (for debugging; if Q1 = embedded,
  `hostapp.app` carries it).
- **`ci/package.sh`:**
  - After the stamp loop (`:115-119`), before the `.txu` (`:288-291`) and `SHA256SUMS` (`:334`):
    `cp "$out/hostapp.app" "$out/hostrecovery.app"`, then `cmp` both.
  - It must come after the stamp, so the file equals the `.txu` app part and the installer's app byte for byte.
- **`tools/make_release_mc.sh`:** after the u2a copy (`:113`), `cp "$APP" "$D/hostrecovery.app"`.
  `ci/prerelease.sh:44` passes `--app "$P/hostapp.app"`, which is the stamped package copy.
  `prerelease.sh:30` also requires `hostrecovery.app` in the package. `SHA256SUMS` (`:144`) and the zip (`:146-156`)
  then carry it.
  - The zip has a top folder `trx64mc-<id>/`, so INSTALL.md must say "copy `hostrecovery.app` to the ROOT of a stick
    or the SD card".
- **`tools/release_notes_mc.py`:**
  - "2. What is in here" (`:373-380`): a row for `hostrecovery.app`.
  - "4. The stick" (`:449`): keep it in the root.
  - "9. Known problems" (`:581`): what to do when the board stays dark after another firmware's updater.
  - Update `tools/test_make_release_mc.py`.
- **`tools/check_release_flash.py`** (prerelease `:48`): add "`hostrecovery.app` == the app part of the package's
  `.txu`".
- **The `.txu`:** no new part under Q1 = embedded. Under Q1 = separate, a part kind 3 (0x1E0000, slot 0x3F000):
  - `txu_pack.py:31-33`, `txu.h:29-30,59` (`part[2]` → 3), `txu.cc` probe and stage, `flashfw` write.
  - Plus a third blob in `update_trx` (vendored) and its manifest (`trx_manifest.h` is generated by `trx_pkg.py`).

## 6. Files to change

**Boot ROM and bench:**

- `host/bootrom/bootrom.c`, `linker.ld`, `Makefile` (noguard target)
- new `host/bootrom/guard_rule.h`
- `host/bootrom_pkg.vhd` (regenerated)
- new `host/sim/flash_boot/bootrom_noguard_pkg.vhd`
- `host/sim/flash_boot/run.sh`, `run.py`, `tb_flash_boot.vhd`

**Recovery program:**

- new `host-app/target/u64ii/riscv/hostrecovery/{Makefile,linker.x}`
- new `host-app/software/hostrecovery/recovery.cc` (+ `ffconf.h` for R2)
- R2 only: a hook in `host-app/software/io/usb/usb_scsi.cc`, plus `host-app/vendor.list` / `PROVENANCE.md`

**Host app:**

- `hostapp.cc` (capture)
- `hostui.cc` (repair before `direct_phase`, provisioning job)
- new `guard.{h,cc}` (pure) + `guard_board.cc`
- `flashfw.{h,cc}` (`FW_BIT_SLOT`, record, recovery write)
- `host_core_flow.cc` (`.txu` order)
- `hostapp/Makefile` (new sources; embedded recovery if Q1)
- `tests/hostapp/test_guard.cc` + `run_*.sh`

**Tools:**

- `tools/txu_pack.py` (`SLOT`, selftest constants)
- `tools/trx_pkg.py:24`
- `tools/trxdbg.py` (status names, blocks, clear)
- `tools/check_app_size.py` (unchanged, used with a limit)

**CI and release:**

- `ci/trxapps.sh`, `ci/lane.sh`, `ci/package.sh`, `ci/prerelease.sh`
- `tools/make_release_mc.sh`, `tools/release_notes_mc.py`, `tools/test_make_release_mc.py`,
  `tools/check_release_flash.py`

**Docs:** the T265 §5 entries, the `docs/board-tooling.md` boot ROM statuses, the manual.

## 7. Open questions (for Alex, one at a time)

1. **Q1: who writes the recovery program to 0x1E0000?**
   - (c) The host app carries it and self-provisions it like copy and record. One code path covers `.txu`, the
     installer, JTAG flash and today's boards; `update_trx` stays unchanged. Cost: about 110-160 KB more in the app
     and in the copy.
   - (b) A `.txu` part kind 3 plus a third blob in the installer. Proposal: (c).
2. **Q2: R1 (Gideon's file layer, about 160 KB, "writes nothing" by code) or R2 (read-only FatFs, about 110 KB,
   "writes nothing" by construction)?** Proposal: R1 first.
3. **Q3: the recovery accepts a flash copy whose CRC differs from the record (needed for the cut window, 3.4).** This
   is a consequence of the decided order. It also means a bit-flipped copy is only caught by its header and size. An
   optional hardening is a CRC trailer on `hostrecovery.app`, but that changes D4's "as written at 0x220000".
4. **Q4: media order inside D6 ("the first it finds"):** flash copy → `.new` → SD root → USB roots in enumeration
   order. While nothing is found it keeps polling (a stick put in later works); JTAG load stays possible.
5. **Q5: JTAG request boots** (`trxdbg boot flash`, `bootrom.c:402-416`) bypass the guard (developer's explicit
   choice) and only report the verdict. Proposal: yes.
6. **Q6: 100T:** derive record and recovery from the application address (appl − 0x1000, appl − 0x40000). No 100T
   host slot is defined yet.
