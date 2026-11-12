// The ZX Spectrum Next core's register block as the application uses it (T116). Source: cores/zxnext/trx/zxn_regs.v - the code, not its header
// comment - read on 2026-10-22. The SD card, the mouse and the sector service sit where the Amiga core puts them (amiga_regs.h), so the base's
// sector-server and mouse libraries drive them unchanged.
#ifndef COREAPP_ZXNEXT_REGS_H
#define COREAPP_ZXNEXT_REGS_H

#define ZXN_ID0          0x000          // "ZXN_"
#define ZXN_FEAT         0x00C          // b0 the SD size register (ZXN_SD_MOUNT + 4) is in 512 byte sectors (always set)
#define ZXN_CTRL         0x0A0          // b0 reset hold, b1 halt (Freeze)
#define ZXN_HOLD         0x01
#define ZXN_JOY          0x0A1          // b0 swaps the host's two joystick ports
#define ZXN_LOCK         0x0A2          // b0 frame lock off (default 0: the machine runs 0.09 % (48K timing) to 2.7 % (Pentagon) fast so that its frame is the host's)
#define ZXN_BOARD_ID     0x0A3          // b3:0 the id NextReg 0x0F reports (reset 3 = issue 5; 0 = issue 2, 2 = issue 4); only the report
#define ZXN_STATUS       0x0A4          // b0 in reset, b1 held (Freeze), b2 an SD image is mounted, b3 the card model is busy
#define ZXN_ST_HELD      0x02
#define ZXN_MACHINE      0x0A5          // b2:0 machine timing (000 48K, 010 128K, 011 +3, 100 Pentagon), b3 60 Hz, b5:4 CPU speed
#define ZXN_FRAMES       0x0A8          // 16 bit
#define ZXN_FB_LATEST    0x0C0
#define ZXN_FRAME_SEQ    0x0C1
#define ZXN_FB_BASE      0x0C4
#define ZXN_FB_STRIDE    0x0C8
#define ZXN_RAM_BASE     0x0D0          // 32 bit DDR2 byte address of the machine's 2 MB (default 0x01000000)
#define ZXN_STAGE_BASE   0x0D4          // 32 bit DDR2 byte address of the SD staging sector (default 0x01200000)
#define ZXN_RTC          0x0E0          // 7 bytes BCD: seconds, minutes, hours (24 h), weekday 1-7, day, month, year 00-99
#define ZXN_FKEYS        0x0F0          // 16 bit: the Next's special keys F1-F10 (bit n = F(n+1))
#define ZXN_BUTTONS      0x0F2          // b0 the Multiface NMI button
#define ZXN_SD_MOUNT     0x180          // b0 a card is present; the size in sectors at +4
#define ZXN_MBOX_STATUS  0x1A0          // b0 pending, b1 write, b2 unit, b7:4 request tag (DONE carries it back in b7:4)
#define ZXN_MBOX_LBA     0x1A4
#define ZXN_MBOX_DONE    0x1A8
#define ZXN_SD_LASTCMD   0x1B8          // the last command the card model was given: b6:0 the number, b7 it was an application command (ACMD)
#define ZXN_MOUSE_DX     0x1C0
#define ZXN_MOUSE_DY     0x1C1          // this write sends the pair
#define ZXN_MOUSE_BTN    0x1C2

#define ZXN_FKEY_F3      2              // 50 / 60 Hz
#define ZXN_FKEY_F8      7              // CPU speed

// the DDR2 window (offsets from 0x01000000): the machine's 2 MB at +0 (the core's RAM_BASE reset value), the SD staging sector, the frame buffers
// (four buffers 1 MiB apart, 640 x 512 RGB565)
#define ZXN_W_RAM        0x000000u
#define ZXN_W_STAGE      0x200000u
#define ZXN_W_FB         0xD00000u

#endif
