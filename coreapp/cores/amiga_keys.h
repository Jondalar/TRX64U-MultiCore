// The Amiga's keys from a USB keyboard (T141 D4): the HID usage code (keyboard page 0x07) -> the Amiga raw key code (Amiga Hardware Reference
// Manual, "Keyboard": codes 00-67, bit 7 = released). 0xFF: the Amiga has no such key. Data only; the translation and the queue are rawkeys.cc.
//   HELP is Page Up. The Amiga's keypad has "(" and ")" where a PC has NUM LOCK and the keypad "=": NUM LOCK is "(", keypad "=" is ")".
//   SCROLL LOCK opens the core menu (run.cc) and never reaches the machine; F10 is the Amiga's F10 (T141 D5, Alex 2026-10-31).
//   Both CTRL keys are the Amiga's one CTRL; the Amiga keys are the GUI keys. CAPS LOCK is the Amiga's lock key (a press = on = 62, the next = off = E2).
#ifndef COREAPP_AMIGA_KEYS_H
#define COREAPP_AMIGA_KEYS_H

#include <stdint.h>

#define AMK_NONE 0xFF
#define AMK_CAPS_USAGE 0x39
#define AMK_CAPS_CODE  0x62

static const uint8_t amk_hid[0x68] = {
    /* 00 */ 0xFF, 0xFF, 0xFF, 0xFF, 0x20, 0x35, 0x33, 0x22,       // - - - - A B C D
    /* 08 */ 0x12, 0x23, 0x24, 0x25, 0x17, 0x26, 0x27, 0x28,       // E F G H I J K L
    /* 10 */ 0x37, 0x36, 0x18, 0x19, 0x10, 0x13, 0x21, 0x14,       // M N O P Q R S T
    /* 18 */ 0x16, 0x34, 0x11, 0x32, 0x15, 0x31, 0x01, 0x02,       // U V W X Y Z 1 2
    /* 20 */ 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,       // 3 4 5 6 7 8 9 0
    /* 28 */ 0x44, 0x45, 0x41, 0x42, 0x40, 0x0B, 0x0C, 0x1A,       // RETURN ESC BACKSPACE TAB SPACE - = [
    /* 30 */ 0x1B, 0x0D, 0x2B, 0x29, 0x2A, 0x00, 0x38, 0x39,       // ] \ (non-US #) ; ' ` , .
    /* 38 */ 0x3A, 0xFF, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55,       // / (CAPS LOCK: special) F1 F2 F3 F4 F5 F6
    /* 40 */ 0x56, 0x57, 0x58, 0x59, 0xFF, 0xFF, 0xFF, 0xFF,       // F7 F8 F9 F10 (F11 F12 PRTSC SCRLOCK: no)
    /* 48 */ 0xFF, 0xFF, 0xFF, 0x5F, 0x46, 0xFF, 0xFF, 0x4E,       // (PAUSE INSERT HOME) PAGE UP = HELP, DELETE = DEL, (END PAGE DOWN) RIGHT
    /* 50 */ 0x4F, 0x4D, 0x4C, 0x5A, 0x5C, 0x5D, 0x4A, 0x5E,       // LEFT DOWN UP NUMLOCK = ( ; keypad / * - +
    /* 58 */ 0x43, 0x1D, 0x1E, 0x1F, 0x2D, 0x2E, 0x2F, 0x3D,       // keypad ENTER 1 2 3 4 5 6 7
    /* 60 */ 0x3E, 0x3F, 0x0F, 0x3C, 0x30, 0xFF, 0xFF, 0x5B,       // keypad 8 9 0 . ; non-US \ = the key next to left SHIFT; (APPLICATION POWER) keypad = = )
};
// the report's modifier byte, bit 0..7: L CTRL, L SHIFT, L ALT, L GUI, R CTRL, R SHIFT, R ALT, R GUI
static const uint8_t amk_mod[8] = { 0x63, 0x60, 0x64, 0x66, 0x63, 0x61, 0x65, 0x67 };

#endif
