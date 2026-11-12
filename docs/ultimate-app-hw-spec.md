# Ultimate app on TRX64 hardware: hardware contract

What our C64U bitstream (XC7A50T) must provide so Gideon's unmodified U64-II `ultimate`
application (RISC-V, FreeRTOS) boots, reaches its main loop and brings up REST/Telnet/FTP.

Sources (paths): firmware `1541ultimate/` (`software/`, `target/u64ii/riscv/`), open VHDL
`1541ultimate/fpga/`, emulator `u64-emulator/docs/hw/00..12-*.md` + `crates/ue2-core/src/devices/*.rs`.
ue2emu boots the unmodified ELF on these models, so its doc 00 ("boot hazard checklist") is the
executable proof of every "REQ" below. Build defines of the app:
`-march=rv32im -mabi=ilp32 -mno-div -DU64=2 -DIOBASE=0x10000000 -DU2P_IO_BASE=0x10100000 -DCLOCK_FREQ=100000000`.

Tier marks: **REQ** = needed to reach the main loop / network. **STUB** = a fixed read value or
latch is enough for now. **OPT** = RAZ/WI (read 0, ignore writes) is fine. **REAL** = the board has
the chip, use the real block on the real pins.

---

## 1. CPU

| Item | Value | Evidence |
|---|---|---|
| Core | Gideon's **rvlite** ("Frenix"), `fpga/cpu_unit/rvlite/vhdl_source/rvlite_wrapper.vhd` | bootloader Makefile writes `bootrom_u64ii_pkg.vhd` into the rvlite tree; app Makefile "core has no divider" |
| ISA | RV32I + Zicsr + **MUL/MULH/MULHSU/MULHU only** (no DIV/REM; rvlite decodes them as MUL). No C, A, F, D. M-mode only | `decode_comb.vhd`, `multiply.vhd`; ELF: 0 DIV, no compressed |
| Generics | `g_mult=true, g_icache=true, g_bootrom=true, g_start_addr=X"80000000"` (defaults) | `rvlite_wrapper.vhd:19-25` |
| Clock | **100 MHz** sys clock is assumed by the binary: tick reload `(100e6>>8)/200-1 = 0x07A0` → 200 Hz FreeRTOS tick; WiFi UART divisor `((2*CLOCK_FREQ/bps)+1)/2-1` | `riscv_main.c:173-186`, `dma_uart.cc:139-144` |
| Reset vector | `0x80000000` (boot BRAM: code 0x1C00 + data 0x400, 8 KB image) | `bootloader/linker.x`, `fetch.vhd` |
| CSRs | mstatus (MIE b3, MPIE b7 only), mie (MEIE b11 only), mtvec (bits 1:0 forced 0), mepc, mcause, mscratch; mtval reads 0; all others RAZ/WI, **never trap** | `csr.vhd` |
| Traps | mepc = PC (also for ECALL), MPIE←MIE, MIE←0, PC←mtvec. `mret`: MIE←MPIE. WFI/FENCE.I = NOP. Illegal → cause 2 | ue2emu doc 01 |
| Interrupts | One level line `irq_i` from ITU `irq_out` → MEIP, mcause `0x8000000B`. No CLINT/MTIME, no FIRQ. Software demux via ITU regs | `riscv_main.c:82-134` |
| Bus split | Data address bit 28 = 0 → DDR (26-bit, 64 MB, wraps); bit 28 = 1 → 8-bit IO bus (24-bit addr). Instruction fetch always DDR (except boot BRAM page 0x8000xxxx). **16/32-bit IO accesses become 2/4 byte strobes at addr+0..3, LSB first** — the app relies on it (RMII, flash DATA_32, WiFi). No bus errors exist | `bus_converter.vhd:56,82-185` |

Use `rvlite_wrapper` unchanged; it already contains the bus converter with this behaviour.

## 2. Boot ROM and how the app gets into DDR

`software/portable/riscv/bootloader_u64ii.c` (prebuilt into `fpga/cpu_unit/rvlite/vhdl_source/bootrom_u64ii_pkg.vhd`):

1. SPI flash CS: CTRL←03, DATA←FF, CTRL←01, DATA←FF, CTRL←03.
2. Read capabilities (0x1000000C-F) and BOARDREV (0x1010000C).
3. If `CAPAB_SIMULATION` (bit 31): wait for magic. Else I2C `scan_enable←0`, **`initializeDDR2()` (Gideon's own DDR2 PHY regs at 0x10100100, DQS calibration, RAM test 0x10000-0x2FFFF)**, `init_ext_pll()` (I2C 0xC8, reg 0x14, 8 bytes, NTSC + HDMI-60 blobs; channel 1V8/HDMI swapped if BOARDREV>>3 == 0x15).
4. If `[0x0000FFFC] == 0x1571BABE`: clear it, jump to `[0x0000FFF8]` (JTAG/warm start).
5. Else if not `CAPAB_BOOT_FPGA` (bit 30): flash offset = `getFpgaType()==3 ? 0x3C0000 : 0x220000`.
   Flash read: CTRL←01 (CS low), DATA←0x03, addr[23:16], addr[15:8], addr[7:0], then
   `dest = DATA_32; length = DATA_32; run = DATA_32;` copy `length` bytes word-wise to `dest`
   **only if 0 < length < 0x180000**, CTRL←00, then if `ITU_BUTTON_REG bit7 == 0` jump to `run`
   (also when nothing was copied because the length was too big).
6. `length == -1` (erased): print and loop waiting for the magic of step 4.

Image: `ultimate.app` = `hex2bin -r` of the ihex = **one record** `{dest u32 LE, length u32 LE, run u32 LE, data}`;
dest = run = `0x00030000` (`_start`). No product header. Current app in the C64U flash: 0x220000..0x362700
(1.29 MB, under the 1.5 MB copy limit; our app, 2026-10-02, is 0x181EFC bytes, over it). Partition table `w25q_flash.cc:51-63` (50T): BOOTFPGA 0x000000
(0x220000), APPL 0x220000 (0x1E0000), FLASHDRIVE 0x400000 (0xBE8000), CONFIG 0xFE8000.

**For us:** step 3 cannot run as-is (our DDR2 is MIG, not Gideon's PHY). Build our own boot ROM from the
same file with `initializeDDR2()` replaced by "wait for MIG `init_calib_complete`" (expose it as a status
bit, e.g. a read of 0x10100100) and keep steps 4-6. Step 4 is the bring-up path: load `ultimate.bin`
(`objcopy -O binary`, LMA = 0x30000) over our JTAG debug port to DDR 0x30000, write 0x00030000 to
0xFFF8 and 0x1571BABE to 0xFFFC (same as `recover.py`). `init_ext_pll` is optional (the app reprograms
the PLLs later in `SetVideoPll`/`SetExternalPLL`).

As built (T15, `docs/specs/T15-flash-boot.md`): our ROM does steps 4-6 with the APPL partition
(0x1E0000 - 12) as the length limit and no jump on a bad header; it boots the flash image on its own
only when the bitstream in flash carries the running bitstream's USR_ACCESS stamp (trx_io BUILD), and
takes JTAG requests at 0xFFF4 (check the flash image, boot a DDR2-staged copy).

State left for the app: MIE=0, mie=0. crt0 sets sp=0x00E8FFFC, gp, clears .bss (~3.3 M stores), runs
C++ constructors, `main`. Registers need no particular value.

## 3. Memory map as the app sees it

### 3a. DDR (26-bit, 64 MB at 0x00000000; board has 128 MB — the upper 64 MB are invisible to the app and free for TRX64)

| Range | Use | Who touches it besides the CPU |
|---|---|---|
| 0x0000FFF8 / 0x0000FFFC | boot magic jump addr / 0x1571BABE | boot ROM, JTAG |
| 0x00010000-0x0002FFFF | boot ROM RAM test scratch | — |
| 0x00030000-0x00E8FFFF | app .text/.rodata/.data/.bss; FreeRTOS heap `ucHeap` 8 MB in .bss (all `new`/`malloc`, incl. **RMII RX pool, WiFi buffers, USB buffers**); initial sp 0x00E8FFFC | RMII, WiFi, USB DMA |
| 0x00EA8000 (32 K) | `__kernal_area` | slot server (`g_kernal_base`) |
| 0x00EB0000 / 0x00EC0000 | drive B / A sound samples | drive sound |
| 0x00ED0000 / 0x00EE0000 | drive B / A 64 K 6502 image (ROM at +0x8000) | 1541 drive CPUs |
| 0x00EF0000 (64 K) | cart RAM | slot server (`g_ram_base_cart`) |
| 0x00F00000 (1 MB) | **ours (T60 §7):** built-in cartridge slot (Run PRG boot cart, SID/MUS/MOD players), trx_io ROM_SEL 2; Gideon's U64 leaves it unused | cart_fill |
| 0x01000000 (16 MB) | REU / GeoRAM; sampler default. **Ours (T60 §7): also the CRT image (ROM_SEL 1), REU or cartridge, never both** | slot server REU (`g_ram_base_reu`) |
| 0x02000000 (16 MB) | RAM disk "Temp", mkfs at every boot — **must exist** | — |
| 0x03000000-0x03BFFFFF | `__updater_start` (no user) | — |
| 0x03C00000 (4 MB) | cart ROM (CRT banks, boot cart): Gideon's layout, what his firmware writes. Our bitstream reads it with trx_io ROM_SEL 0 (the reset value), our app uses ROM_SEL 1/2 and leaves this range to the ring | slot server (`g_rom_base_cart` **must be 0x3C00000**, not the open default 0x0F00000) |
| 0x80000000-0x80001FFF | rvlite boot BRAM | — |

Bus masters: CPU I + D (rvlite), RMII RX/TX, WiFi UART DMA, USB nano, slot server (REU, cart, kernal), drives,
sampler. Record `t_mem_req_32` = {tag 8, request, read_writen, address 25:0, byte_en 3:0, data 32};
resp = {data 32, rack, rack_tag, dack_tag} (`ip/busses/vhdl_source/mem_bus_pkg.vhd`). Arbiter:
`mem_bus_arbiter_pri_32.vhd`. Our MIG bridge must implement this tagged request/ack protocol.

### 3b. IO bus (byte-wide, 0x10000000-0x10FFFFFF; record `t_io_req`/`t_io_resp`, `io_bus_pkg.vhd`)

**Every IO access must be acked** (`io_resp.ack`), else rvlite stalls forever. Unmapped space → `io_dummy`/
splitter default port (ack, data 0). RAZ = 0, never 0xFF: 0xFF breaks UART (TX full), I2C (busy), SD (card
present), WD177x, UCI.

| Address | Block | Open VHDL | Tier for first bring-up |
|---|---|---|---|
| 0x10000000-3F | ITU: IRQ ctrl, timers, buttons, version, capabilities, console UART, ms timer, high IRQs | `io/itu/itu.vhd` (+ `uart_lite/uart_peripheral_io.vhd`) | **REQ** |
| 0x10020000-0x1002580F | Drive A (+0) / B (+0x4000) regs, dirty, param RAM, WD177x | `1541/*` (`drive_registers.vhd`, `floppy*.vhd`, `wd177x.vhd`) | OPT (RAZ) |
| 0x10028000-0FFF | IEC processor regs + code RAM | `io/iec_interface/iec_processor_io.vhd` | STUB (R1=R2=0x01) |
| 0x10040000-0x1005FFFF | slot server: cart/machine regs, legacy SID, UCI, copper/timing, sampler, ACIA, EEPROM, **C64 DMA window 0x10050000** | `cart_slot/slot_server_v4.vhd` | **STUB** (see §6) |
| 0x10060000-FF | SD card SPI | `io/spi/spi_peripheral_io.vhd` | OPT (RAZ = no card) / REAL later |
| 0x10060100 | RTC (I2C, no user) | — | OPT |
| 0x10060200-0F | **SPI flash** | `io/spi/spi_peripheral_io.vhd` | **REQ, REAL** |
| 0x10060304/05 | profiler sub/task (written on every context switch) | — | OPT (WI) |
| 0x10060400-0F | RTC seconds timer | `ip/clock/real_time_clock.vhd` | OPT (0 = 1970 dates) |
| 0x10060500-0F | GCR codec | `1541/gcr_codec.vhd` | OPT |
| 0x10060604/08 | ICAP pulse/write (reboot after update) | `io/icap/*` (Spartan only): inert here; 0x10060600 reads 0x7A and 0x10060610.. is the T79 ICAP loader (T79.1 section 7) | OPT |
| 0x10060700 | audio select | `io/audio_select/audio_select.vhd` | OPT |
| 0x10060800-2F | **RMII MAC** | `io/rmii/ethernet_rmii.vhd` + `ip/free_queue/free_queue.vhd` | **REQ, REAL** |
| 0x10060900-0F | WiFi DMA UART to the ESP32 | `io/uart_lite/uart_dma.vhd` | REAL (see §4.8) |
| 0x10080000-0FFF | USB nano CPU (2 K BRAM, NANO_START 0x10080800) | `io/usb2/usb_host_nano.vhd` | REAL on c64_basic (T07, capability bit 23 on) |
| 0x100A0000 / 0x100C0000 | C2N playback / record | `io/c2n_*` | OPT (bits 7/8 off) |
| 0x10100000-1F | U2P IO: GET_MDIO +06, SET_MDC +0A, SET_MDIO +0B, BOARDREV(R)/SPEAKER_EN(W) +0C, HUB_RESET +0D, ULPI_RESET +0F | `altera/u2p_io.vhd` (generic RTL despite the dir) | **REQ** |
| 0x10100100-0F | DDR2 PHY regs (boot ROM only) | closed (Altera/Lattice PHY only) | ours: MIG status |
| 0x10100200 | clock measure (no user) | — | OPT |
| 0x10100300-0E | MATRIX_KEYB (+ WASD-to-joy 32-bit at +0x0B, unaligned) | closed | STUB (latch) |
| 0x10100400-0F | U64 board IO (HDMI/HPD, restore, cart detect, joy, keyboard col/row, LEDs, eth-stream) | closed | **STUB** (table §5) |
| 0x10100500-89 | audio mixer, speaker mixer, resampler (write-only) | closed | OPT |
| 0x10100600-FF | LED strip | closed | OPT |
| 0x10100700-08 | **HW I2C master** (4 channels: 0 HDMI, 1 1V8, 2 3V3) | `io/i2c/i2c_master.vhd` | **REQ, REAL** |
| 0x10100800-03 / 0x10100900 | Bling board keyboard RX / LEDs | closed | OPT |
| 0x10140000-0D / +0x1000 / +0x2000 | overlay char-gen regs / screen RAM 4 K / colour RAM 4 K | `ip/video/char_generator_peripheral_12.vhd` (U64-II variant closed) | **REQ: 2 × 4 K R/W RAM** |
| 0x10144000-1D / 0x10145000-3F / 0x10148000 | HDMI timing / HDMI palette / VIC cropper | closed | OPT (palette may be RAM) |
| 0x10180000-37 | C64 core config (video format, DMA_MEMONLY +03, SID bases, CORE_VERSION +10, bus routing, joystick/paddle inject +30..37) | closed | STUB (latch, CORE_VERSION const) |
| 0x10180080-87 | SID voice ADSR levels | closed | OPT |
| 0x10180800 / 0x10180C00 | C64 palette RGB / YUV | closed | OPT |
| 0x10181000-1F | C64 PLD (U64-I; on II only JOYCTRL +1E) | closed | STUB (RAM) |
| 0x10181800 | U64 debug register | closed | OPT |
| 0x10185000-0x10185FFF | UltiSID 1/2 filter curves | closed (sid6581 restricted, §7) | OPT |
| 0x10188000 / 0x1018A000 / 0x1018C000 | BASIC / KERNAL / CHAR ROM windows (written at boot, read back by monitor) | closed | DONE: the C64's ROM block (`c64_mem.v`), read/write, through the DMA handshake (`ultimate_sys.vhd` r_rom, `freeze_resp.v` dma_rom) |
| 0x10190000-FF | UDP stream header templates | closed | OPT |
| 0x10200000-FF | MMCM DRP (16-bit index i at byte 2i) + reset 0x102000FF | closed | OPT |

Splitter shape of the open U2+ RISC-V top (`fpga_top/ultimate_fpga/vhdl_source/u2p_riscv.vhd:353-415`):
addr bit 20 separates the `ultimate_logic_32` space (0x100xxxxx) from the U2P space (0x101xxxxx); inside U2P,
bit 16 small/big, bits 9:8 pick u2p_io / DDR2 / remote. ITU sub-decode: `addr[5:4]` (0 IRQ/timer, 1 UART,
2 ms/LED/high IRQ, 3 dummy).

## 4. Blocks: what the app does, what ue2emu returns

### 4.1 ITU 0x10000000 — REQ (ue2emu: full model, `itu.rs`)

| Off | Name | Semantics (itu.vhd) | App use |
|---|---|---|---|
| 00 | IRQ_GLOBAL | b0 irq_en, reset 1 | ←1 at scheduler start |
| 01 | IRQ_ENABLE | mask \|= data (read = mask) | ←01 tick, 04 USB, 08 tape, 10 UCI, 20 RMII RX, 80 C64 reset |
| 02 | IRQ_DISABLE | mask &= ~data | ←FF at start |
| 03 | IRQ_EDGE | edge select; **instantiate `g_edge_init="10000101"`, `g_edge_write=false`** (bits 0,2,7 edge; 3-6 level) | never written |
| 04 | IRQ_CLEAR | edge_flag &= ~data | ISR writes back ACTIVE |
| 05 | IRQ_ACTIVE | (edge_flag \| src & ~edge) & mask | ISR |
| 06 | ITU_TIMER | 8-bit down counter, −1 per 5 µs, sticks at 0 | `wait_ms`: ←200, poll to 0 (**before interrupts are on**) |
| 07 | IRQ_TIMER_EN | b0 en, b1 select (0 = sys clock) | ←0, then ←1 |
| 08/09 | IRQ_TIMER_HI/LO | reload; read = counter | ←07, ←A0 → period (0x7A0+1)×256 = 499 968 clocks ≈ 5 ms |
| 0A | BUTTON_REG | b5/b6/b7 = buttons 0/1/2, b6 also btn_menu | main loop edge-detects b6 (menu); **b7 set at boot = boot ROM "Lock"** |
| 0B | FPGA_VERSION | g_version (0x25 in U2+ top) | cosmetic |
| 0C-0F | CAPABILITIES | big-endian u32, **valid from reset** (read in C++ ctors before `main`) | see §5 |
| 10 | UART_DATA | W: TX FIFO push | `outbyte` |
| 12 | UART_FLAGS | b4 TX almost full, b6 TX idle, b7 RX avail | `while (FLAGS & 0x10)` before each byte — **must not stick at 1** |
| 1F | (UART ICTRL alias) | — | crt0 dummy trap handler writes 0x49 |
| 22/23 | MS_TIMER HI/LO | free 16-bit ms counter | flash busy timeouts (re-read until stable) |
| 24/25/29 | USB/SD/printer busy LED | b0 | WI ok |
| 27 | IRQ_HIGH_EN | mask_high, R/W (read-modify-write) | 03 WiFi, 05 HDMI HPD, 06 unlock, 00 ACIA, 01/02 drives |
| 28 | IRQ_HIGH_ACT | irq_high & mask_high, level, no latch | ISR; an active bit without handler gets its EN cleared |

`irq_out = irq_en & ((ACTIVE≠0) | (irq_high & mask_high ≠ 0))`. Wiring (U2+ top `ultimate_logic_32.vhd:517-536`):
`irq_in(2)` USB, (3) tape, (4) UCI, (5) **RMII RX**, (6) RMII TX, (7) C64 reset; `irq_high(0)` ACIA, (1)/(2)
WD177x A/B, (3) WiFi, (4) Bling, (5) HDMI HPD, (6) unlock, (7) guru. Ticks: `tick_4MHz`, `tick_1us`,
`tick_1ms` from `ip/clock/fractional_div.vhd` (100 MHz × 4/25 = 16 MHz → /4, /16, /16000).
c64_basic: `irq_in(4)` is the UCI (T28); `irq_high(2)` (drive B there; we have no drive B) is our IO block 0x101A0000
(`trx_io.v`: cause register, level), e.g. the cartridge's pending flash operation and EEPROM writes.
Console UART: `g_baudrate` 115 200 in the open top (U64-II value unknown), pins UART_TXD N15 / UART_RXD P15.

### 4.2 SPI flash 0x10060200 — REQ, REAL (ue2emu: NOR model `flash.rs`)

`spi_peripheral_io` (instance with `g_fixed_rate=>true`), decodes `addr[3:2]`:
+00 W: one 8-bit transfer, R: transfer with MOSI=FF and return MISO (stalls); 32-bit access = 4 transfers,
first byte = LSB; +04 rate (fixed, reads 1); +08 CTRL W: b0 FORCE_SS, b1 LEVEL_SS (0x00 = CS per byte,
0x01 = CS held low, 0x03 = CS held high); +0C CRC (unused). Reset: CS idle high.
App: `get_flash()` probes W25Q (9F → EF 40 xx), S25FL, S25FL-L (66 99 reset, `wait_ms(1)`×3) at every call;
config pages; FAT flash drive at 0x400000 (opcodes 0x13/0x03, DATA_32); **`read_serial` 0x4B (4 dummy +
8 UID bytes) → MAC `02:15:41:s1^s5:s2^s6:s3^s7` and hostname**. `wait_ready` polls SR1 BUSY against the ms timer
with interrupts masked. The board chip is a **W25Q128** (JEDEC EF 40 18; JTAG dump 2026-09-30; ue2emu emulates
S25FL128L instead — irrelevant for us). Pins: MOSI P22, MISO R22, CSn T19; **SCK is the CCLK pin → drive it
through STARTUPE2.USRCCLKO**. `U64_RESTORE_REG` (0x10100402) == 1 would select safe mode: keep 0.

### 4.3 HW I2C 0x10100700 — REQ, REAL (ue2emu: never busy + EDID, `i2c.rs`)

`t_hw_i2c`: +0 data_out (write = send byte; first byte after idle = auto START + address), +1 status
(b7 BUSY, b2 NACK/error, b3 missed write, b0 started; read clears b2/b3), +2 repeated_start, +3 stop,
+4 receive, +5 receive_ack, +6 channel (0 HDMI, 1 1V8, 2 3V3, 4 lines in `i2c_master.vhd`), +7 soft_reset,
+8 scan_enable (output `scan_en`; the keyboard scanner it gates is closed). **Status b7 is polled with no
timeout** (pre-scheduler). Boot traffic: NAU8822 codec 0x34 (ch1), USB2513 hub 0x58/0x5A (ch2), expanders
0x40/0x42 (ch1, keyboard column/row), ext PLL 0xC8 reg 0x14/0x81 (ch1, ch0 if BOARDREV 0x15), EDID 0xA0 (ch0,
only if HPD set). NACKs only print. Generic `g_frequency` = sys clock.
Pins: I2C18_SDA W6 / SCL U7, I2C33_SDA H15 / SCL K16, HDMI_SDA/SCL (see XDC).

### 4.4 U2P IO 0x10100000 — REQ (ue2emu: table + MDIO PHY, `board.rs`)

`u2p_io.vhd`: R +06 = mdio_i, W +0A = MDC level, W +0B = MDIO drive (1 = release/high, 0 = low; open drain
with pull-up), R +0C = `board_rev(4:0)` in bits 7:3 + speaker_en b0, W +0C = speaker_en/vol (app writes 0xFF;
**BOARDREV must not follow writes**), +0D hub reset, +0F ULPI reset / buffer_en. Tie `board_rev = "10111"`
(0x17 → read 0xB8, `isEliteBoard` true; never 0x15 = prototype, which swaps the PLL I2C channels).

### 4.5 RMII Ethernet 0x10060800 — REQ, REAL (ue2emu: functional MAC + slirp, `rmii.rs`)

Use `ethernet_rmii.vhd` (MAC + `rmii_transceiver`) or `ethernet_interface.vhd` + own transceiver (as U2+ top).
Ports: `eth_clock` = RMII_REFCLK (50 MHz **input** from the PHY, pin B17), `rmii_crs_dv` A14, `rmii_rxd` C13/A13,
`rmii_tx_en` B15, `rmii_txd` A16/B16; RX_ER C14 and RMII_IRQn A15 unused (`GET_IRQ` hard-wired 1). Sub-blocks by `addr[5:4]`:

| Addr | Dir | Meaning |
|---|---|---|
| +00..05 | W | own MAC octet 0..5 |
| +07 | W | b0 promiscuous (app: 0 → unicast-to-me + broadcast only; **multicast dropped**) |
| +08 | W | b0 RX enable |
| +10..13 | W | TX address (bits 25:0) |
| +14..15 | W | TX length (11:0); app pads to ≥ 60 |
| +18 | W | TX start (busy←1) |
| +19 | W | TX IRQ ack (never written) |
| +1A | R | b0 TX busy — must drop, else every TX is lost ("tx is busy") |
| +21..23 | W | buffer pool base bits 25:8 (+20 ignored) |
| +24 | W | push free buffer ID |
| +28 | R | ID of presented RX frame (0xFF after pop) |
| +2A..2B | R | RX frame length without FCS (12 bit) |
| +2E | W | soft reset of the 4 FIFO pointers |
| +2F | R/W | R b0 `used_valid` (= RX IRQ level on ITU bit 5); W = pop |

Buffer layout: pool = `new uint8_t[32*1536+256]`, base 256-aligned; buffer `id` at `base + id*1536`;
**frame byte 0 at buffer+2** (32-bit LE writes); max frame 1530 without FCS; drop on bad CRC / no free ID.
Init (task "RMII Driver Task"): MAC→+00..05, +07←0, +08←0; on link: +08←0, 1 tick, +2E←1, +2F←1, +2E←1,
+24←0..31, +08←1, ITU_IRQ_ENABLE←0x20. ISR: R +2F, R +2A/+2B, R +28, W +2F (pop), queue. Free via +24 when lwIP
releases the pbuf. TX: W +10..13, +14..15, +18. Link drop (MDIO reg1 b2 = 0 after 1 s idle) → +08←0, disable IRQ.
Capability bit 24 (`CAPAB_ETH_RMII`) gates the whole driver.

**PHY (KSZ8081, MDIO bit-banged through u2p_io +06/+0A/+0B, `software/io/mdio/mdio.c`):** 32×1 preamble, ST 01,
OP 10 read / 01 write, PHY address bits both = `addr != 0` → **only addresses 0 and 3**; TA; data MSB first.
App reads reg2 at PHY 0 then 3, expects **0x0022** (else prints "could not find Ethernet PHY", uses 3, goes on);
writes reg4=0x01E1, reg0x1B=0x0500, reg0x16=0x0002, reg0=0x1200; polls reg1 b2 (link) every 250 ms while
down, every 1 s idle while up. PHY reset/strap pins are not touched by the app: REF_CLK out mode must come
from the board straps (it does on stock firmware). MDIO_CLK B13, MDIO_DATA D14.

lwIP 2.2.1 on top: DHCP default on (static fallback 192.168.2.64), Telnet 23, FTP 21 (PASV 51000-61000),
HTTP/REST 80, socket 64 TCP/UDP, mDNS (but multicast is filtered by the MAC), SNTP, syslog.

### 4.6 Overlay 0x10140000 — REQ as RAM (ue2emu: RAM + renderer, `overlay.rs`)

The UI fills and **reads back** screen RAM 0x10141000 and colour RAM 0x10142000 (cursor XOR, backup/restore,
scroll): two 4 K R/W RAMs. Regs 0x10140000-0D are write-only latches. The overlay object is created
unconditionally on U64==2.
The menu goes to the overlay only while U64_HDMI_REG bit 2 (HPD) reads 1 and "Interface Type" is
"Overlay on HDMI" (default: Freeze). c64_basic: `overlay.v` (regs, both RAMs, HDMI palette 0x10145000;
drawn as `char_generator_slave12`, but centred and scaled 3x/2x/1x on our fixed 1080p instead of
X_ON/Y_ON); simulation `designs/c64_basic/sim/overlay_sim.py`.

### 4.7 U64 board IO 0x10100400 — STUB (ue2emu: latches + constants, `u64io.rs`)

| Off | Name | Read value for bring-up | Notes |
|---|---|---|---|
| 00 | HDMI_REG | 0x00 (no overlay UI, no EDID) or 0x04 (HPD; then EDID on ch0, NACK harmless) | W 0x20/0x10/0x08 |
| 02 | RESTORE_REG | **0x00** | 1 = safe mode, no /flash |
| 03 | CART_DETECT | **0x03** (GAME/EXROM high = no external cart) | |
| 06 | KEYB_JOY | **0xFF** | W = swap bit |
| 07 | BLACKBOARD | 0x01 | |
| 0A / 0B | KEYB_COL (W) / KEYB_ROW (R) | ROW **0xFF stable** (read-until-stable loop, no timeout) | |
| 04,05,08,09,0C-0F | PLL reset, userport, HDMI enable, connectors, LED strip, PWM, case LED, eth-stream | latch with readback (0C-0F are read-modify-write) | |

### 4.8 WiFi DMA UART 0x10060900 — REAL (ue2emu: block + stub u64ctrl, `wifi.rs`)

`uart_dma.vhd`: +0/+1 baud divisor (app writes 19 for 5 Mbit/s at 100 MHz), +2 W ictrl / R status
(b0 rx_irq, b1 tx_irq, b2 buf_irq — ISR loops `while(status & 7)`, must be live), +3 flowctrl (**b7 reads 0**;
b7 write = soft reset), +4..7 TX addr, +8..9 TX len (W) / RX len (R), +A tx_push, +B rx_pop, +C..F RX addr
(write to +F arms). DMA into heap buffers (12 TX, 12 RX × 1552 B). IRQ = ITU high bit 3 (level). The C64U's
ESP32-S3 runs `software/u64ctrl` and answers (IDENTIFY, power control, front-panel events). Pins WIFI_TXD P14,
WIFI_RXD N13. Without this block (RAZ): boot and network are fine, but the UI task **hangs when the WiFi row is
opened**, and power-off/WiFi config RPCs block forever (doc 04 H5/H6). Include it from step B on.

### 4.9 Other blocks the app touches at boot

| Block | Boot access | RAZ OK? |
|---|---|---|
| USB nano 0x10080000 | `custom_hardware_init`: W 0x10080800, 1024×W16 into BRAM, hub I2C, 50×R 0x1010000D; with bit 23: blob load + NANO_START | real block since T07 (bit 23 on, ITU irq 2) |
| SD 0x10060000 | "SD Card Manager" polls +08 every 100 ms (CD) | yes (= no card) |
| RTC timer 0x10060400 | `get_fattime` | yes |
| IEC 0x10028000 | with bit 5: R +0, code load to +0x800, W 0x100287FF (stray), server polls +2 every 2 ticks | stub R1 = R2 = 0x01 (avoids a busy poll) |
| Drives 0x10020000/24000 | with bits 1/2: DRIVETYPE readback, `while (R 0x10021806 & 0x80)` (**0xFF hangs**), DIRTY must not be RAM | yes (RAZ) |
| UCI 0x10044000 | only with bit 18 (ctor before `main`) | keep bit 18 off |
| MMCM 0x10200000, mixers, resampler (512-675 × W32), LED strip, palettes | write-only | yes |
| Profiler 0x10060304/05 | W on every context switch | yes |

### 4.10 ue2emu model status (real vs stub)

Real models: CPU, ITU, flash NOR, SD card, RMII (+ host bridge), WiFi block + virtual u64ctrl, USB HLE nano,
overlay renderer, I2C (EDID only, never busy), MDIO PHY, RTC timer, C64 via TRX64 backend (or T0 stubs with
`--c64 none`), drives, IEC (TRX64 parts). Stubs/latches: U64 board IO, mixers, LED strip, MMCM, HDMI regs,
Bling board, ICAP (WI), GCR codec, audio select. Default capability word 0x34000226 (type 3), boots the
unmodified ELF with 0 unmapped IO accesses for 60 s emulated.

## 5. Minimal bring-up set

### Step A: CPU + DDR + console (goal: `-- Custom Hardware Init --` … `-- Start Scheduler --` and the boot banner)

- rvlite @ 100 MHz, our boot ROM (§2), MIG DDR2 behind a `t_mem_req_32` bridge (64 MB window), JTAG app load.
- ITU (real, generics §4.1) with capabilities valid from reset; console UART on N15/P15.
- IO default responder: ack + 0 everywhere.
- HW I2C master on the real buses (or a stub: status 0x00, writes ignored).
- U2P IO with BOARDREV 0x17.
- Overlay screen/colour RAM (8 K BRAM).
- Constants: §4.7 table; flash (real) — `ConfigManager` probes it in the first InitFunction.
- C64 stub (§6.1). This reaches `All linked modules have been initialized` (ue2emu milestone M1).

### Step B: network

- Capability bit 24, RMII MAC on the real PHY pins + MDIO in u2p_io, ITU irq_in(5) ← `used_valid`.
- Real flash (MAC from the W25Q unique ID).
- WiFi DMA UART to the ESP32.
- Result: DHCP lease; REST 80, Telnet 23, FTP 21 reachable (ue2emu M5).

### Capability word

Bits: 0x04000000 ULTIMATE64 (U64 configurator, matrix keyboard, overlay geometry; **gates the C64 boot probes**),
0x00000200 CARTRIDGE (C64 machine + main UI loop), 0x00000020 HARDWARE_IEC (**required**: without it the
SoftIEC drive runs on an uninitialised `IecInterface` = undefined behaviour), 0x00000002 DRIVE_1541_1 (without it
only REST/socket drive commands crash), 0x01000000 ETH_RMII, type field 0x30000000.
**Type must be 2 (XC7A50T)**: type 3 moves APPL to 0x3C0000 and the flash drive to 0x580000, which does not match
the C64U flash. ue2emu uses type 3 — do not copy its word.

- Step A: **0x24000222**. Step B: **0x25000222**. USB (T07): **0x25800222**. GMod2 EEPROM window (icepi cartridge
  model, `trx_io.v`): **0x25C00222** (bit 18 UCI is on since T28; bit 21 sampler is on in the T25.6 build: 0x25E40222). Keep 0 for bits
  27 (ACIA), 7/8 (tape), 25 (U2+), 30 (BOOT_FPGA), 31 (SIMULATION) until their block exists.
- Untested alternative without any C64 stub: 0x21000020 (no ULTIMATE64/CARTRIDGE → no C64 probes, main task
  suspends after init, network InitFunctions still run). Not exercised in ue2emu; ROM loading then uses the U2
  flash table (`W25Q_Flash::read_image`).

## 6. C64 side

### 6.1 What the boot needs from the C64 side (stub until our core is coupled)

Without these the app **hangs** (all are unbounded polls, some with interrupts masked):

| Reg | Required behaviour | Hazard |
|---|---|---|
| C64_STOP 0x10040001 | read = req \| req<<1 (bit1 STOPPED follows bit0 at once) | `hard_stop`: `while(!(C64_STOP & 2))` |
| C64_STOP_MODE 0x10040002, C64_MODE 0x10040000 | latch with readback | MODE←0x08 then … |
| C64_CLOCK_DETECT 0x10040003 | **0x01** (bit4 RESET sense = 0, bit0 PHI2 present) | `while (CLOCK_DETECT & 0x10)` |
| DMA window 0x10050000-FFFF | `$D012` must read 0xFF (constant or counter); `$DC01` stable; `$DC00` reads 0xFF after 0xFF written; SID probe regs 0x00/0xFF/RAM all mean "no SID" | `while (PEEK($D012) != 0xFF)` inside `portENTER_CRITICAL` |
| ITU_TIMER | must count (C64 reset uses it) | |
| other 0x1004000x, 0x10180000-37 | latch; CORE_VERSION fixed | |
| ROM windows 0x10188000-0x1018CFFF | RAM (written at boot, read by the monitor) | |

Simplest stub: all reads in the DMA window = 0xFF, writes ignored; or 64 K BRAM with $D000-$DFFF reading 0xFF.

### 6.2 How the app drives the C64 (map onto our core)

Recommended: instantiate `slot_server_v4` (GPL) with `g_implement_sid=false`, `g_vic_copper=false`,
`g_rom_base_cart=X"3C00000"`, `g_ram_base_cart=X"0EF0000"`, `g_ram_base_reu=X"1000000"`,
`g_kernal_base=X"0EA8000"`, `g_clock_freq=100_000_000`, and feed its slot pins (`phi2_i`, `ba_i`, `rwn_i`,
`slot_addr_*`, `slot_data_*`, `io1n/io2n/romln/romhn`, `exromn/gamen`, `irqn/nmin`, `dman_o`, `rstn_o`) from our
C64 core's internal bus instead of the cartridge port. It owns 0x10040000-0x1005FFFF.

| Function | Registers (0x1004xxxx unless noted) | Semantics |
|---|---|---|
| Reset | MODE +00: W 0x04 assert, 0x08 release, 0x02 ULTIMAX, 0x10 NMI; R b1/b2/b4 | `C64::reset` = MODE←0, ←0x04, wait 20×5 µs, ←0x08. REST `machine:reset` |
| Pause/resume | STOP +01 (b0 req, R b1 stopped), STOP_MODE +02 (0 in bad line / BA low ≥ 14 cycles, 1 after write-then-read, 2 immediate) | only the 6510 halts; VIC/CIA/SID run on. REST `machine:pause/resume` |
| DMA | window 0x10050000 + C64 addr; one byte = one C64 bus cycle **through the current PLA map** (`io_to_dma_bridge` → `slot_master_v4`); reads stall until served; app adds a flush read after writes. `C64_DMA_MEMONLY` 0x10180003 = 1 → RAM only | REST `machine:readmem/writemem`, socket 64 DMA commands, every PEEK/POKE |
| Cart emulation | CART_TYPE +05 (b4:0 type, b7:5 variant), KILL/ACTIVE +06 (W b0 kill, b1 update strobe; R b0 cart active), SERVE_CONTROL +0D (b0 serve while stopped); ROM from DDR 0x03C00000 (bank × 16 K), cart RAM 0x00EF0000 | cartridge reads need an answer inside the C64 cycle (bank cache) |
| Run PRG | `C64_Subsys::dma_load`: stop; DMA $0172 ← runcode, $014F sync, $0175 name, $0174 len, **$0002 ← 0x80**; start boot cart (type 0x41, 8 K at $8000, image at 0x03C00000); wait 100 ms; stop; $0173 ← drive, **$0002 ← 0x40**; loop resume/25 ms/stop until **$0002 == 0x01**; DMA the file; set $2D-$32, $AE/$AF, $90, $35/$36; **$0002 ← 0**; resume. The 6502 boot cart then starts the program per runcode and writes $40 to $DFFF (cart off); app waits for ACTIVE=0 and re-installs the configured cart | REST `runners:run_prg`, socket 64 DMA-run commands |
| Keyboard buffer | socket 64 `SOCKET_CMD_KEYB`: DMA write text to $0277.., then $00C6 ← length | pure DMA |
| Matrix keyboard | MATRIX_KEYB 0x10100300: bytes 0-7 = key bits, **1 = pressed**, byte = scancode>>3 (CIA1 PA column), bit = scancode&7 (PB row); byte 8 reset, 9 RESTORE, 10 freeze; 0x1010030B (32-bit) WASD→joystick | USB keyboard and REST key taps; our core ORs it into the CIA1 matrix |
| Joystick inject | 0x10180030/31 (active low b4:0 \| 0xE0), paddles 0x10180032-35, mouse enable 36/37 | |
| ROMs | BASIC 0x10188000, KERNAL 0x1018A000, CHAR 0x1018C000 (8 K/8 K/4 K), written on every `init_system_roms`; KERNAL also at run time (CRT with kernal); the app writes them linearly (memcpy / `load_file`, byte by byte through the IO bus) and reads them back (`capture_pristine_rom_image`, the monitor) | done: these windows are the C64's ROM block; the bitstream holds no ROM; C64_MODE's reset is set from the FPGA/RISC-V reset on (`g_cartreset_init = '1'` as in `u2p_riscv_lattice.vhd`), so the C64 runs only once `C64::init_cartridge` has written the ROMs and released it |
| REU | REU_ENABLE +08, REU_SIZE +09 (0=128 K … 7=16 M); memory DDR 0x01000000 | slot server `reu.vhd` |
| Video mode | C64_VIDEOFORMAT 0x10180001 (b0 NTSC enc, b1 60 Hz, b5:4 cycles/line 63/64/65) | PAL/NTSC switch |
| SID mapping | 0x10180008-0F bases/masks, 0x10180011/12 enables, 0x10180014 stereo select | |
| C64 reset sense | ITU irq_in(7) (edge) | UCI only |

## 7. Gideon's open VHDL per block, and licences

The repo root `LICENSE.txt` is **GPL-3.0**. Most VHDL files carry only a copyright line ("(C) COPYRIGHT Gideon's
Logic Architectures" / "Gideon's Logic B.V. - Copyright 2023") with no extra clause, so the repo GPL-3 applies.
`1541/vhdl_source/via6522.vhd` states GPL 3.0 itself.

**Restricted — "Note that this file is copyrighted, and is not supposed to be used in other projects without
written permission from the author."** (22 files):
- `fpga/sid6581/vhdl_source/*` (16 files: UltiSID — `sid_top`, `sid_peripheral`, `sid_filter`, `oscillator`, `adsr_multi`, …)
- `fpga/io/copper/vhdl_source/{copper,copper_engine,copper_pkg,copper_regs}.vhd`
- `fpga/io/audio/lp_filter.vhd`, `lp_filter2.vhd` (used by `io/audio/sys_to_aud.vhd`)

`slot_server_v4.vhd` instantiates `sid_peripheral` and `copper` inside `if g_implement_sid / g_vic_copper generate`:
set both false and give Vivado empty stub entities (or remove the branches) so the restricted files are not
compiled. Do not use `sys_to_aud.vhd`.

| Block | Files (under `1541ultimate/fpga/`) |
|---|---|
| CPU | `cpu_unit/rvlite/vhdl_source/*` (`rvlite_wrapper`, `core`, `bus_converter`, `csr`, `icache`, …; boot ROM `bootrom_u64ii_pkg.vhd` = generated from `bootloader_u64ii.c`) |
| Buses | `ip/busses/vhdl_source/{io_bus_pkg,mem_bus_pkg,dma_bus_pkg,slot_bus_pkg,io_bus_splitter,io_dummy,mem_bus_arbiter_pri_32,io_to_dma_bridge,io_bus_bridge2}.vhd` |
| ITU + console UART | `io/itu/vhdl_source/{itu,itu_pkg}.vhd`, `io/uart_lite/vhdl_source/{uart_peripheral_io,tx,rx}.vhd`, `ip/clock/vhdl_source/fractional_div.vhd` |
| SPI flash / SD | `io/spi/vhdl_source/{spi_peripheral_io,spi}.vhd` |
| I2C | `io/i2c/vhdl_source/i2c_master.vhd` |
| U2P IO (MDIO, BOARDREV) | `altera/u2p_io.vhd` |
| RMII | `io/rmii/vhdl_source/{ethernet_rmii,ethernet_interface,eth_filter,eth_transmit,rmii_transceiver,crc32}.vhd`, `ip/free_queue/vhdl_source/{free_queue,block_bus_pkg}.vhd` |
| WiFi UART | `io/uart_lite/vhdl_source/{uart_dma,tx_dma,rx_dma,slip_*}.vhd` |
| USB | `io/usb2/vhdl_source/usb_host_nano.vhd` (+ `ulpi_*`, `usb_*`), `ip/nano_cpu/vhdl_source/*` |
| C64 coupling (cart regs, DMA, REU, carts, UCI, ACIA, EEPROM, sampler) | `cart_slot/vhdl_source/{slot_server_v4,cart_slot_registers,slot_master_v4,slot_slave,all_carts_v5,reu,freezer,slot_timing}.vhd`, `io/command_interface/*`, `io/acia/*`, `io/sampler/*` |
| Drives / IEC | `1541/vhdl_source/*` (`c1541_drive`, `floppy*`, `wd177x`, `gcr_codec`, `drive_registers`, CPU in `6502/`), `io/iec_interface/vhdl_source/iec_processor_io.vhd` |
| Tape | `io/c2n_playback/*`, `io/c2n_record/*` |
| RTC timer | `ip/clock/vhdl_source/real_time_clock.vhd` |
| Overlay | `ip/video/vhdl_source/char_generator_peripheral_12.vhd` (+ `char_generator_*`) |
| Reference tops | `fpga_top/ultimate_fpga/vhdl_source/u2p_riscv.vhd` (rvlite + splitters + u2p_io), `ultimate_logic_32.vhd` (ITU/IRQ wiring, all peripherals) |

**Closed / not in the repo** (must be ours): U64-II top level, DDR2 PHY for Artix (open ones are Altera/Lattice;
use MIG), HDMI/video pipeline and its regs (0x10144000+, palette, cropper), MMCM DRP, U64 C64 core and its
config block (0x10180000), keyboard scanner behind I2C `scan_en`, Bling board, audio mixer/resampler, LED strip,
UDP stream generator, ICAPE2 wrapper. Vendor IP under `fpga/nios`, `fpga/altera` megafunctions, `fpga/lattice` is
vendor-licensed and irrelevant.

## 8. Open points

- Console baud on the real U64-II (115 200 assumed); ITU `g_version` (cosmetic).
- rvlite timing at 100 MHz on our floorplan; DDR latency through MIG (75 MHz UI clock, 128-bit) on the CPU path.
- Front-panel/menu button path on the C64U: UI_BUTTON[2:0] pins (P17/R18/N18) vs ESP32 events.
- PHY address (0 or 3) and straps on the C64U — the app tolerates both.
- The `mdns_resp_init` omission in the firmware (lwIP client-data slot shared with DHCP): real hardware gets
  DHCP leases, so treat it as benign until shown otherwise.
