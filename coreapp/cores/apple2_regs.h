// The Apple //e core's register block as the application uses it (T93 D15). Source: cores/apple2/rtl/trx/a2_regs.vhd and a2_win.vhd (the
// code, read 2026-10-19); the aperture decode was checked against host/ultimate_sys.vhd (r_rom): offsets 0..0x3FFF are contiguous for the
// core's window (dma_addr[13:0]), so a 16 KB array goes in as it is.
#ifndef COREAPP_APPLE2_REGS_H
#define COREAPP_APPLE2_REGS_H

#define A2_WARM_MS      20             // how long the app holds CTRL-RESET (CTRL b0); the core stretches it by WARM_CLKS machine clocks
#define A2_ID0          0x000          // "APL2"
#define A2_CAPS2        0x005          // b2: the bit level Disk II engine and the slot 7 card (T89.1); an older core lacks it and the app refuses to mount
#define A2_CAPS2_BITS   0x04
#define A2_CTRL         0x0A0          // b0 warm reset hold (CTRL-RESET), b1 halt (Freeze), b2 write 1: cold reset (power-up byte written)
#define A2_CTRL_HOLD    0x01
#define A2_CTRL_COLD    0x04
#define A2_CFG          0x0A1          // b1:0 monitor, b3:2 palette, b4 scanlines, b5 character set, b6 swap the joystick ports
#define A2_STATUS       0x0A4          // b0 in reset, b1 held (Freeze), b2 cold reset running, b3 CAPS LOCK, b4 aux RAM waits
#define A2_ST_RESET     0x01
#define A2_ST_HELD      0x02
#define A2_ST_COLD      0x04
#define A2_CAPS         0x0A2          // W b0: CAPS LOCK as the application knows it (the C64U's SHIFT LOCK), see a2_regs.vhd
#define A2_FRAMES       0x0A8          // 16 bit
#define A2_FB_LATEST    0x0C0
#define A2_FRAME_SEQ    0x0C1
#define A2_FB_BASE      0x0C4
#define A2_FB_STRIDE    0x0C8
#define A2_AUX_BASE     0x0D0
#define A2_WIN_SEL      0x0D4          // the array the ROM aperture shows
#define A2_DRV(n)       (0x0E0 + 8 * (n))   // +0 STATUS (b0 request open, b1 write-back, b2 dirty, b3 valid, b4 motor), +1 REQ_TRACK (a QUARTER track,
                                            // 0..139), +2 HEAD (quarter track), +3 ACK, +4/+5 BITS (the bits of the track put in, written before ACK), +6 FLAGS (b0 noise)
#define A2_DRV_STATUS   0
#define A2_DRV_REQ      1
#define A2_DRV_HEAD     2
#define A2_DRV_ACK      3
#define A2_DRV_BITS     4              // 16 bit, low byte first
#define A2_DRV_FLAGS    6
#define A2_DRVF_NOISE   0x01
#define A2_DRVS_REQ     0x01
#define A2_DRVS_SAVE    0x02
#define A2_DRVS_DIRTY   0x04
#define A2_DRVS_VALID   0x08
#define A2_DRVS_MOTOR   0x10
#define A2_DISK_CTL     0x0F0          // b0/b1 drive 1/2 has an image, b2/b3 write protect 1/2
#define A2_DISK_CMD     0x0F1          // b0/b1 image changed 1/2, b2/b3 flush 1/2
#define A2_DISK_CFG     0x0F2          // b0: while a track loads the old one goes on playing (default: random flux)
#define A2_DISK_CLR     0x0F3          // W any: clear the counters below
#define A2_LAT_MAX(n)   (0x160 + 4 * (n))   // 32 bit: the longest a track request stayed open, in sys_clk clocks (32.7 MHz)
#define A2_REQS(n)      (0x168 + 2 * (n))   // 16 bit: requests opened
#define A2_STARVED      0x16C          // 16 bit: reads of the data latch while a mounted drive's track was not in the buffer
#define A2_NOISE_CELLS  0x170          // 32 bit: bit cells played as random flux because a track was loading
#define A2_HDD_STATUS   0x180          // R b0 a block is wanted, b1 it is a write, b2 unit 2, b3 LC bank 1 selected at the command
#define A2_HDD_BLOCK    0x182          // R 16 bit
#define A2_HDD_MEM      0x184          // R 16 bit: the buffer in main RAM
#define A2_HDD_RESULT   0x186          // W 0 ok, else a ProDOS error
#define A2_HDD_ACK      0x187          // W any
#define A2_HDD_CTL      0x188          // RW b0/b1 unit 1/2 mounted, b2/b3 protected
#define A2_HDD_SIZE(n)  (0x18A + 2 * (n))   // RW 16 bit: blocks
#define A2_HDD_CMDS     0x18E          // R 16 bit
#define A2_KEY_INJ      0x140          // W a 7 bit code into the keyboard queue
#define A2_KEY_QUEUED   0x141

// WIN_SEL values
#define A2_SEL_SYSROM   0
#define A2_SEL_CHRROM   1
#define A2_SEL_DISKROM  2
#define A2_SEL_TRACK1   3
#define A2_SEL_TRACK2   4
#define A2_SEL_AUX      5
#define A2_SEL_HDDROM   8              // the slot 7 card's 256 byte ROM (T89.1)

// sizes
#define A2_SYSROM_BYTES 16384u
#define A2_CHRROM_BYTES 8192u
#define A2_DISKROM_BYTES 256u
#define A2_TRACK_BYTES  8192u          // the track buffer of a drive (block RAM): the bits of a track, at most A2N_BUF_BITS (a .nib track with its sync zeros is the longest)
#define A2_HDDROM_BYTES 256u
#define A2_QTRACKS      140            // the head's quarter tracks (the stepper's count): whole track t is 4t + 2, a half track 4t + 4

// the DDR2 window (offsets from 0x01000000): the aux RAM image (A2_AUX_BASE default 0x01100000) and the four frame buffers
#define A2_W_AUX        0x100000u
#define A2_W_FB         0xD00000u

#endif
