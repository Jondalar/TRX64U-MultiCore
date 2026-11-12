// The Commodore 128 core's register page as the application uses it (T113 section 5.5, "register page"). The core window is the c64u core's
// (pages 0, 4, 7, C, D, as there) plus one page of its own, E. CoreIo::rd/wr take a byte offset from the core window 0x101A0000 (hw.cc:
// ioRead8(TRX_IO_BASE + reg); c64_regs.h and c264_regs.h address their pages the same way: 0x1000 is page 1), so the page is 0xE000 + off.
// Text of record: the table in T113 section 5.5; reads of absent offsets give 0.
#ifndef COREAPP_C128_REGS_H
#define COREAPP_C128_REGS_H

#define C128_PAGE        0xE000u
#define C128_REG(off)    (C128_PAGE + (off))

#define C128_ID0         0x000                        // page 0: "C128" (the host's ID block, bytes C 1 2 8)
#define C128_CAPS        C128_REG(0x004)              // 0x01: this page, layout version 1
#define C128_IRQ_EN      C128_REG(0x010)
#define C128_IRQ_CAUSE   C128_REG(0x014)              // R/W1C: b0 a VDC frame is complete, b1 monitor-switch key, b2 40/80 key
#define C128_MON_SEL     C128_REG(0x020)              // b0 = 1: the 80-column picture (VDC) is shown; the core toggles it on the monitor-switch key
#define C128_D4080       C128_REG(0x021)              // b0: the 40/80 DISPLAY switch as $D505 bit 7 reads it (1 = 80 columns); reset 0
#define C128_CAPSLK      C128_REG(0x022)              // b0: the CAPS LOCK latch
#define C128_JOY         C128_REG(0x024)              // b0: the host's joystick ports 1 and 2 are swapped before the core (T157); live; back to 0 by COLD
#define C128_XKEYS       C128_REG(0x028)              // 24 bits, 028-02A: byte n = extra matrix column Kn, bit r = row r, 1 = pressed
#define C128_XSPECIAL    C128_REG(0x02B)              // levels, the core acts on the rising edge
#define C128_XS_CAPSLK   0x01
#define C128_XS_D4080    0x02
#define C128_XS_MONITOR  0x04
#define C128_STATUS      C128_REG(0x030)              // b0 C64 mode, b1 Z80 is the CPU, b2 2 MHz, b3 held (Freeze), b4 PAL
#define C128_ST_HELD     0x08
#define C128_ROM_CTRL    C128_REG(0x040)              // W b0: flush the ROM line cache; R b0: walker busy
#define C128_ROM_HITS    C128_REG(0x044)
#define C128_ROM_MISSES  C128_REG(0x048)
#define C128_ROM_BASE    C128_REG(0x050)              // 32 bit, little endian: the DDR2 byte address of the system ROM image
#define C128_FB_LATEST   C128_REG(0x060)              // b7 valid, b2 the picture is woven (interlace, 480 rows; else 400), b1:0 the buffer of the newest complete VDC picture
#define C128_FRAME_SEQ   C128_REG(0x061)
#define C128_FB_OVERRUNS C128_REG(0x062)
#define C128_FB_BASE     C128_REG(0x064)              // 32 bit, reset 0x01D00000
#define C128_FB_STRIDE   C128_REG(0x068)              // 32 bit, reset 0x00100000
#define C128_VDC_CTRL    C128_REG(0x070)              // b0 frame writer enabled (reset 1)
#define C128_CHR_ADDR    C128_REG(0x090)              // RW 13 bit (090 low, 091 high): the character ROM store's byte address (T113 5.7)
#define C128_CHR_DATA    C128_REG(0x092)              // W: store the byte at CHR_ADDR, then add 1; R: the byte at CHR_ADDR (2 clocks after a CHR_ADDR write), then add 1
#define C128_CHR_BYTES   8192u                        // two sets of 4 KiB: the C64's (bank 16) and the C128's (bank 17), byte i = image byte C128_CHR_IMG_OFF + i
#define C128_CHR_IMG_OFF 0x10000u
#define C128_ROM_WAITS   C128_REG(0x0A0)              // 32 bit: CPU cycles held by a ROM line that was not in the cache
#define C128_ROM_FILLMAX C128_REG(0x0A4)              // 16 bit: the longest fetch of a ROM line, in core clocks (31.5 MHz)
#define C128_FR_MIN      C128_REG(0x0B0)              // 20 bit: the shortest VIC frame in core clocks since DBG_CLR (628,992 in PAL)
#define C128_FR_MAX      C128_REG(0x0B4)              // 20 bit: the longest
#define C128_DBG_CLR     C128_REG(0x0B8)              // W: clears FR_MIN / FR_MAX
// T194 / T140 section 18: which chip is being used (c128_act.v): per VIC frame, of the last complete one. Read ACT_SEQ before and after the others: the same value means one frame's words.
#define C128_ACT_VIC     C128_REG(0x0D0)              // CPU writes to the VIC's registers $D000-$D02E (8 bit, saturating at 255)
#define C128_ACT_CRAM    C128_REG(0x0D1)              // CPU writes to the colour RAM $D800-$DBFF
#define C128_ACT_VDC     C128_REG(0x0D2)              // CPU writes to the VDC's data register $D601
#define C128_ACT_FLAGS   C128_REG(0x0D3)              // b0 the VIC's DEN ($D011 b4) at the end of that frame
#define C128_ACT_SEQ     C128_REG(0x0D4)              // the frame number (wraps)
#define C128_COLD        C128_REG(0x080)              // W b0 = 1: cold start (every register of the machine to its power-on value, the VIC in the
                                                      // power-on frame phase, this page's settings too); R b0: it is running (up to one frame + 256 clocks)
#define C128_COLD_MAX_MS 200                          // the longest the application waits for it (one frame is 20 ms)

// The system ROM image (boot0.rom layout of the MiSTer core, 72 KiB): C64 BASIC + KERNAL, C128 editor / Z80 BIOS / KERNAL, C128 BASIC, the
// two character ROMs. The application writes the whole image through the DDR2 window and then asks for a flush of the line cache.
#define C128_ROM_BYTES      73728u
#define C128_ROM_DDR_ADDR   0x01C00000u               // = ROM_BASE, a DDR2 byte address
#define C128_ROM_WIN_OFF    0x00C00000u               // = C128_ROM_DDR_ADDR - 0x01000000, the offset inside the application's DDR2 window

// the picture: three buffers of 640 x 480 RGB565 (614,400 bytes; a frame that is not interlaced uses rows 0..399) at the core's reset values of FB_BASE / FB_STRIDE
#define C128_FB_ADDR          0x01D00000u
#define C128_FB_STRIDE_BYTES  0x00100000u

#endif
