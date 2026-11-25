// The Commodore 128 core's registers as the application uses them (T113 section 5.5). The registers are named by the driver table of the app (drvtab.h, made from the TOML A:
// cores/c128/app/gateware.toml and the blocks of the base that sdk.toml names); the offset the core's register interface (RegBind, CoreIo rd/wr) takes is the address in the table minus
// the address of the block "ident", which sits at the start of the core's window (0x101A0000). The core's own page is E (blocks page_id .. act); the rest of the window is the C64U's
// (ident, irq, tape pins) and the host's pixel layer (pixel). Reads of absent offsets give 0.
#ifndef COREAPP_C128_REGS_H
#define COREAPP_C128_REGS_H

#include "drvtab.h"
#define C128_OFS(addr)   ((addr) - DRV_ident_ADDR)

#define C128_ID0           C128_OFS(DRV_ident_id_ADDR)                        // page 0: "C128" (the host's ID block, bytes C 1 2 8)
#define C128_CAPS          C128_OFS(DRV_page_id_caps_ADDR)              // 0x01: this page, layout version 1
#define C128_IRQ_EN        C128_OFS(DRV_page_irq_irq_en_ADDR)
#define C128_IRQ_CAUSE     C128_OFS(DRV_page_irq_irq_cause_ADDR)              // R/W1C: b0 a VDC frame is complete, b1 monitor-switch key, b2 40/80 key
#define C128_MON_SEL       C128_OFS(DRV_machine_mon_sel_ADDR)              // b0 = 1: the 80-column picture (VDC) is shown; the core toggles it on the monitor-switch key
#define C128_D4080         C128_OFS(DRV_machine_d4080_ADDR)              // b0: the 40/80 DISPLAY switch as $D505 bit 7 reads it (1 = 80 columns); reset 0
#define C128_CAPSLK        C128_OFS(DRV_machine_capslk_ADDR)              // b0: the CAPS LOCK latch
#define C128_JOY           C128_OFS(DRV_machine_joy_swap_ADDR)              // b0: the host's joystick ports 1 and 2 are swapped before the core (T157); live; back to 0 by COLD
#define C128_PIN_CTRL      C128_OFS(DRV_tape_pin_ctrl_ADDR)                        // c64u_io PIN_CTRL (page 0): b0 MOTOR out, b1 WRITE out, b4 READ / SENSE pins feed the machine; b2 (streamer drives READ) and b3 (SENSE pull) stay 0 with an external datasette (T214)
#define C128_XKEYS         C128_OFS(DRV_machine_xkeys_ADDR)              // 24 bits, 028-02A: byte n = extra matrix column Kn, bit r = row r, 1 = pressed
#define C128_XSPECIAL      C128_OFS(DRV_machine_xspecial_ADDR)              // levels, the core acts on the rising edge
#define C128_XS_CAPSLK   0x01
#define C128_XS_D4080    0x02
#define C128_XS_MONITOR  0x04
#define C128_STATUS        C128_OFS(DRV_machine_status_ADDR)              // b0 C64 mode, b1 Z80 is the CPU, b2 2 MHz, b3 held (Freeze), b4 PAL
#define C128_ST_HELD     DRV_machine_status_held_MASK
#define C128_ROM_CTRL      C128_OFS(DRV_rom_ctrl_ADDR)              // W b0: flush the ROM line cache; R b0: walker busy
#define C128_ROM_HITS      C128_OFS(DRV_rom_hits_ADDR)
#define C128_ROM_MISSES    C128_OFS(DRV_rom_misses_ADDR)
#define C128_ROM_BASE      C128_OFS(DRV_rom_base_ADDR)              // 32 bit, little endian: the DDR2 byte address of the system ROM image
#define C128_FB_LATEST     C128_OFS(DRV_video_fb_latest_ADDR)              // b7 valid, b2 the picture is woven (interlace, 480 rows; else 400), b1:0 the buffer of the newest complete VDC picture
#define C128_FRAME_SEQ     C128_OFS(DRV_video_frame_seq_ADDR)
#define C128_FB_OVERRUNS   C128_OFS(DRV_video_fb_overruns_ADDR)
#define C128_FB_BASE       C128_OFS(DRV_video_fb_base_ADDR)              // 32 bit, reset 0x01D00000
#define C128_FB_STRIDE     C128_OFS(DRV_video_fb_stride_ADDR)              // 32 bit, reset 0x00100000
#define C128_VDC_CTRL      C128_OFS(DRV_video_vdc_ctrl_ADDR)              // b0 frame writer enabled (reset 1)
#define C128_CHR_ADDR      C128_OFS(DRV_chr_addr_ADDR)              // RW 13 bit (090 low, 091 high): the character ROM store's byte address (T113 5.7)
#define C128_CHR_DATA      C128_OFS(DRV_chr_data_ADDR)              // W: store the byte at CHR_ADDR, then add 1; R: the byte at CHR_ADDR (2 clocks after a CHR_ADDR write), then add 1
#define C128_CHR_BYTES   8192u                        // two sets of 4 KiB: the C64's (bank 16) and the C128's (bank 17), byte i = image byte C128_CHR_IMG_OFF + i
#define C128_CHR_IMG_OFF 0x10000u
#define C128_ROM_WAITS     C128_OFS(DRV_perf_rom_waits_ADDR)              // 32 bit: CPU cycles held by a ROM line that was not in the cache
#define C128_ROM_FILLMAX   C128_OFS(DRV_perf_rom_fillmax_ADDR)              // 16 bit: the longest fetch of a ROM line, in core clocks (31.5 MHz)
#define C128_FR_MIN        C128_OFS(DRV_perf_fr_min_ADDR)              // 20 bit: the shortest VIC frame in core clocks since DBG_CLR (628,992 in PAL)
#define C128_FR_MAX        C128_OFS(DRV_perf_fr_max_ADDR)              // 20 bit: the longest
#define C128_DBG_CLR       C128_OFS(DRV_perf_dbg_clr_ADDR)              // W: clears FR_MIN / FR_MAX
// T194 / T140 section 18: which chip is being used (c128_act.v): per VIC frame, of the last complete one. Read ACT_SEQ before and after the others: the same value means one frame's words.
#define C128_ACT_VIC       C128_OFS(DRV_act_vic_ADDR)              // CPU writes to the VIC's registers $D000-$D02E (8 bit, saturating at 255)
#define C128_ACT_CRAM      C128_OFS(DRV_act_cram_ADDR)              // CPU writes to the colour RAM $D800-$DBFF
#define C128_ACT_VDC       C128_OFS(DRV_act_vdc_ADDR)              // CPU writes to the VDC's data register $D601
#define C128_ACT_FLAGS     C128_OFS(DRV_act_flags_ADDR)              // b0 the VIC's DEN ($D011 b4) at the end of that frame
#define C128_ACT_SEQ       C128_OFS(DRV_act_seq_ADDR)              // the frame number (wraps)
#define C128_COLD          C128_OFS(DRV_cold_cold_ADDR)              // W b0 = 1: cold start (every register of the machine to its power-on value, the VIC in the
                                                      // power-on frame phase, this page's settings too); R b0: it is running (up to one frame + 256 clocks)
// the host's pixel layer (page 6), the capture of the VIC's picture (T194 D4)
#define C128_STILL_BASE   C128_OFS(DRV_pixel_still_base_ADDR)     // W 32 bit, byte 3 commits
#define C128_STILL_STATUS C128_OFS(DRV_pixel_still_status_ADDR)   // R: b0 valid, b1 60 Hz (240 rows), b3:2 slot of the last complete frame, b31:8 frames captured
#define C128_COLD_MAX_MS 200                          // the longest the application waits for it (one frame is 20 ms)

// The system ROM image (boot0.rom layout of the MiSTer core, 72 KiB): C64 BASIC + KERNAL, C128 editor / Z80 BIOS / KERNAL, C128 BASIC, the
// two character ROMs. The application writes the whole image through the DDR2 window and then asks for a flush of the line cache.
#define C128_ROM_BYTES      73728u
#define C128_ROM_DDR_ADDR   0x01C00000u               // = ROM_BASE, a DDR2 byte address
#define C128_ROM_WIN_OFF    0x00C00000u               // = C128_ROM_DDR_ADDR - 0x01000000, the offset inside the application's DDR2 window

// the picture: three buffers of 640 x 480 RGB565 (614,400 bytes; a frame that is not interlaced uses rows 0..399) at the core's reset values of FB_BASE / FB_STRIDE
#define C128_FB_ADDR          DRV_video_fb_base_reset
#define C128_FB_STRIDE_BYTES  DRV_video_fb_stride_reset

#endif
