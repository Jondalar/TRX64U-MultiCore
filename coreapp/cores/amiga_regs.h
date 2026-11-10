// The Amiga core's register block and DDR2 window (cores/amiga/rtl/trx/amiga_regs.v, T85 D4). Byte registers at the core window (page 0).
#ifndef COREAPP_AMIGA_REGS_H
#define COREAPP_AMIGA_REGS_H

#define AM_FEAT         0x00C   // b0 the HDF size registers are in 512-byte sectors, not bytes (HDF_SECT; 0 in a gateware without the register)
#define AM_FEAT_RAWKEY  0x10    // b4 RAWKEY (both Amiga builds, T141)
#define AM_FEAT_FBWIN   0x08    // b3 the display window (AM_DIW_*) and the crop window (AM_FB_*) registers are there (the A1200 core, T97 5.10)
#define AM_CTRL         0x0A0   // b0 HOLD (machine in reset; 1 after power-up), b1 VIDEO_OFF
#define AM_HOLD         0x01
#define AM_VIDEO_OFF    0x02    // the frame writer takes no new frame
#define AM_MEM_CFG      0x0A1   // [1:0] chip 0/1/2/3 = 512K/1M/1.5M/2M, [3:2] slow 0/512K/1M/1.5M
#define AM_FAST_CFG     0x0A2   // fastram_config: 0 none, 1 2 MB, 2 4 MB, 3 4 MB + the 1.5 MB the slow RAM gives up (5.5 MB), 4 one Zorro III board of 32 MB (A1200 only)
#define AM_CPU_CFG      0x0A3   // 0 = 68000
#define AM_CHIPSET_CFG  0x0A4   // [3] ECS Agnus + Denise, [4] AGA; bit 1 stays 0 (PAL)
#define AM_FLOPPY_CFG   0x0A5   // [1] write protect all, [3:2] number of drives - 1
#define AM_IDE_CFG      0x0A6   // 0x21 = IDE on
#define AM_JOY_CFG      0x0A7   // b0 swaps the host's joystick ports 1 and 2 before the Amiga's two ports (T157); written at once, no commit
#define AM_RTC_EN       0x0E0   // T191: b0 the clock at $DC0000 answers (the setting Real-time clock); R/W, no commit
#define AM_RTC_VAL      0x0E1   // T191: W a nibble of the time (amiga_rtc.v)
#define AM_RTC_IDX      0x0E2   // T191: W commits AM_RTC_VAL: 0-12 register S1..W, 14 loader hold (VAL b0), 15 zero the second + loaded
#define AM_RTC_STAT     0x0E3   // T191: R b0 the clock holds the time (a bitstream load clears it)
#define AM_STATUS       0x0A8   // b6 PAUSED (Freeze)
#define AM_STAT_SEL     0x0B8   // which counter STAT_VAL shows (amiga_regs.v: 8 audio over/under, 9 fill min/max, 10 fill now, 11 catch-up)
#define AM_STAT_VAL     0x0BC   // 32 bit, little endian
#define AM_ST_PAUSED    0x40
#define AM_FB_LATEST    0x0AC   // b7 valid, b1:0 buffer
#define AM_FRAME_SEQ    0x0AF
#define AM_FLOPPY_MOUNT 0x100   // + 0x10 * drive
#define AM_HDF_MOUNT    0x180   // + 8 * unit; the 32 bit size register is at +4
#define AM_MBOX_STATUS  0x1A0   // b0 pending, b1 write, b2 unit
#define AM_MBOX_LBA     0x1A4
#define AM_MBOX_DONE    0x1A8
#define AM_MOUSE_DX     0x1C0
#define AM_MOUSE_DY     0x1C1   // this write sends the pair
#define AM_MOUSE_BTN    0x1C2
#define AM_RAWKEY       0x1C4   // T141 (both Amiga builds): W = one Amiga key event {release, raw code}; R b0 = room for 8 more events
#define AM_DLOG_STATUS  0x1F0   // b0 not empty, b1 overflow (write 0x02 clears)
#define AM_DLOG_TRACK   0x1F1
#define AM_DLOG_DRIVE   0x1F2
#define AM_DLOG_POP     0x1F3

// T97 5.10 (the A1200 core's gateware only): the decoded display window and the picture's crop window
#define AM_DIW_H        0x0C0   // 32 bit: HDIWSTRT [8:0], HDIWSTOP [24:16] (lores pixels)
#define AM_DIW_V        0x0C4   // 32 bit: VDIWSTRT [10:0], VDIWSTOP [26:16] (lines)
#define AM_FB_SKIP_X    0x0D0   // 9 bit (2 bytes): hires pixels after hblank
#define AM_FB_SKIP_Y    0x0D2   // 8 bit: lines after vblank
#define AM_FB_COLS      0x0D4   // 7 bit: words of 8 hires pixels, <= 90
#define AM_FB_LINES     0x0D6   // 9 bit (2 bytes): <= 270

#define AM_CHS_ECS      0x08
#define AM_CHS_AGA      0x10

// the DDR2 window: offsets from 0x01000000
#define AM_W_ROM        0x780000u            // Kickstart, 512 KB (a 256 KB ROM also at +0x7C0000)
#define AM_W_ADF        0x900000u            // + n * AM_ADF_STRIDE
#define AM_ADF_STRIDE   0xE0000u
#define AM_W_STAGE      0xC80000u            // HDF staging sector
#define AM_W_FB         0xD00000u            // three buffers, 1 MiB apart (FB_BUFS = 3, gateware parameters)
#define AM_FB32_ADDR    0x04000000u          // T136 D3, the A1200: three 32-bit buffers at this DDR2 byte address, 2 MiB apart
#define AM_FB32_STRIDE  0x200000u
#define AM_ADF_SIZE     901120u
#define AM_ADF_TRACKS   160
#define AM_ADF_TRACK_BYTES 5632u             // 11 sectors * 512

#endif
