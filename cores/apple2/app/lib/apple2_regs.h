// The Apple //e core's register block as the application uses it (T93 D15). The gateware's block is cores/apple2/rtl/trx/a2_regs.vhd (the registers) and a2_win.vhd (the ROM aperture: offsets 0..0x3FFF are
// contiguous for the core's window, dma_addr[13:0], so a 16 KB array goes in as it is).
#ifndef COREAPP_APPLE2_REGS_H
#define COREAPP_APPLE2_REGS_H

// The registers are named by the driver table of the app (drvtab.h, made from the TOML A: cores/apple2/app/gateware.toml and the blocks of the base that sdk.toml names); the offset the
// core's register interface (RegBind, CoreIo rd/wr) takes is the address in the table minus the address of the block "ident", which sits at the start of the core's window.
#include "drvtab.h"
#define A2_OFS(addr)    ((addr) - DRV_ident_ADDR)

#define A2_WARM_MS      20             // how long the app holds CTRL-RESET (CTRL b0); the core stretches it by WARM_CLKS machine clocks
#define A2_ID0          A2_OFS(DRV_ident_id_ADDR)          // "APL2"
#define A2_CAPS2        A2_OFS(DRV_ident_caps2_ADDR)          // b2: the bit level Disk II engine and the slot 7 card (T89.1); an older core lacks it and the app refuses to mount
#define A2_CAPS2_BITS   0x04
#define A2_CTRL         A2_OFS(DRV_a2_machine_ctrl_ADDR)          // b0 warm reset hold (CTRL-RESET), b1 halt (Freeze), b2 write 1: cold reset (power-up byte written)
#define A2_CTRL_HOLD    DRV_a2_machine_ctrl_hold_MASK
#define A2_CTRL_COLD    DRV_a2_machine_ctrl_cold_MASK
#define A2_CFG          A2_OFS(DRV_a2_machine_cfg_ADDR)          // b1:0 monitor, b3:2 palette, b4 scanlines, b5 character set, b6 swap the joystick ports
#define A2_STATUS       A2_OFS(DRV_a2_machine_status_ADDR)          // b0 in reset, b1 held (Freeze), b2 cold reset running, b3 CAPS LOCK, b4 aux RAM waits
#define A2_ST_RESET     DRV_a2_machine_status_in_reset_MASK
#define A2_ST_HELD      DRV_a2_machine_status_held_MASK
#define A2_ST_COLD      DRV_a2_machine_status_cold_MASK
#define A2_CAPS         A2_OFS(DRV_a2_machine_caps_ADDR)          // W b0: CAPS LOCK as the application knows it (the C64U's SHIFT LOCK), see a2_regs.vhd
#define A2_FRAMES       A2_OFS(DRV_a2_machine_frames_ADDR)          // 16 bit
#define A2_FB_LATEST    A2_OFS(DRV_a2_video_fb_latest_ADDR)
#define A2_FRAME_SEQ    A2_OFS(DRV_a2_video_frame_seq_ADDR)
#define A2_FB_BASE      A2_OFS(DRV_a2_video_fb_base_ADDR)
#define A2_FB_STRIDE    A2_OFS(DRV_a2_video_fb_stride_ADDR)
#define A2_AUX_BASE     A2_OFS(DRV_a2_mem_aux_base_ADDR)
#define A2_WIN_SEL      A2_OFS(DRV_a2_mem_win_sel_ADDR)          // the array the ROM aperture shows
#define A2_DRV(n)       (A2_OFS(DRV_a2_floppy_status_ADDR) + DRV_a2_floppy_status_STRIDE * (n))   // +0 STATUS (b0 request open, b1 write-back, b2 dirty, b3 valid, b4 motor), +1 REQ_TRACK (a QUARTER track,
                                            // 0..139), +2 HEAD (quarter track), +3 ACK, +4/+5 BITS (the bits of the track put in, written before ACK), +6 FLAGS (b0 noise)
#define A2_DRV_STATUS   (DRV_a2_floppy_status_ADDR - DRV_a2_floppy_status_ADDR)
#define A2_DRV_REQ      (DRV_a2_floppy_req_track_ADDR - DRV_a2_floppy_status_ADDR)
#define A2_DRV_HEAD     (DRV_a2_floppy_head_ADDR - DRV_a2_floppy_status_ADDR)
#define A2_DRV_ACK      (DRV_a2_floppy_ack_ADDR - DRV_a2_floppy_status_ADDR)
#define A2_DRV_BITS     (DRV_a2_floppy_bits_ADDR - DRV_a2_floppy_status_ADDR)              // 16 bit, low byte first
#define A2_DRV_FLAGS    (DRV_a2_floppy_flags_ADDR - DRV_a2_floppy_status_ADDR)
#define A2_DRVF_NOISE   DRV_a2_floppy_flags_noise_MASK
#define A2_DRVS_REQ     DRV_a2_floppy_status_req_MASK
#define A2_DRVS_SAVE    DRV_a2_floppy_status_save_MASK
#define A2_DRVS_DIRTY   DRV_a2_floppy_status_dirty_MASK
#define A2_DRVS_VALID   DRV_a2_floppy_status_valid_MASK
#define A2_DRVS_MOTOR   DRV_a2_floppy_status_motor_MASK
#define A2_DISK_CTL     A2_OFS(DRV_a2_floppy_disk_ctl_ADDR)          // b0/b1 drive 1/2 has an image, b2/b3 write protect 1/2
#define A2_DISK_CMD     A2_OFS(DRV_a2_floppy_disk_cmd_ADDR)          // b0/b1 image changed 1/2, b2/b3 flush 1/2
#define A2_DISK_CFG     A2_OFS(DRV_a2_floppy_disk_cfg_ADDR)          // b0: while a track loads the old one goes on playing (default: random flux)
#define A2_DISK_CLR     A2_OFS(DRV_a2_floppy_disk_clr_ADDR)          // W any: clear the counters below
#define A2_LAT_MAX(n)   (A2_OFS(DRV_a2_stats_lat_max_ADDR) + DRV_a2_stats_lat_max_STRIDE * (n))   // 32 bit: the longest a track request stayed open, in sys_clk clocks (32.7 MHz)
#define A2_REQS(n)      (A2_OFS(DRV_a2_stats_reqs_ADDR) + DRV_a2_stats_reqs_STRIDE * (n))   // 16 bit: requests opened
#define A2_STARVED      A2_OFS(DRV_a2_stats_starved_ADDR)          // 16 bit: reads of the data latch while a mounted drive's track was not in the buffer
#define A2_NOISE_CELLS  A2_OFS(DRV_a2_stats_noise_cells_ADDR)          // 32 bit: bit cells played as random flux because a track was loading
#define A2_HDD_STATUS   A2_OFS(DRV_a2_hdd_status_ADDR)          // R b0 a block is wanted, b1 it is a write, b2 unit 2, b3 LC bank 1 selected at the command
#define A2_HDD_BLOCK    A2_OFS(DRV_a2_hdd_block_ADDR)          // R 16 bit
#define A2_HDD_MEM      A2_OFS(DRV_a2_hdd_mem_ADDR)          // R 16 bit: the buffer in main RAM
#define A2_HDD_RESULT   A2_OFS(DRV_a2_hdd_result_ADDR)          // W 0 ok, else a ProDOS error
#define A2_HDD_ACK      A2_OFS(DRV_a2_hdd_ack_ADDR)          // W any
#define A2_HDD_CTL      A2_OFS(DRV_a2_hdd_ctl_ADDR)          // RW b0/b1 unit 1/2 mounted, b2/b3 protected
#define A2_HDD_SIZE(n)  (A2_OFS(DRV_a2_hdd_size_ADDR) + DRV_a2_hdd_size_STRIDE * (n))   // RW 16 bit: blocks
#define A2_HDD_CMDS     A2_OFS(DRV_a2_hdd_cmds_ADDR)          // R 16 bit
#define A2_KEY_INJ      A2_OFS(DRV_a2_keys_key_inj_ADDR)          // W a 7 bit code into the keyboard queue
#define A2_KEY_QUEUED   A2_OFS(DRV_a2_keys_key_queued_ADDR)

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
