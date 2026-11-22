// The C64 table (T239.3 D20, base 3.2): the bus of a C64-family machine while it is stopped, as the cartridge tools and the shared libraries use it. It is an SDK
// header because the cores of that family provide it, not one core: a library exports it under the name C64_API_NAME at the major C64_API_MAJOR, and a library
// or an application that uses it asks `api->lib->find(C64_API_NAME, U2A3_VERSION(C64_API_MAJOR, 0), &size)`. The base answers that call with its own table
// as long as no library exports that name (the members of `api->c64`, `SvcC64`, and `reset`); `api->c64` stays until the cut of LibApi. This name is the one
// word of the C64 family in the generic tree, by Alex's decision of D20 (the tools/d24_ledger.txt lines for this file and for the two that include it).
//
// The rules of every exported table: `size` first; within a major, members are only appended; removing or reordering one makes the next major. A caller checks
// `size` before it uses a member and a member is there when its pointer is not null.
#ifndef COREAPP_C64_API_H
#define COREAPP_C64_API_H

#include <stdint.h>

#define C64_API_NAME "c64"
#define C64_API_MAJOR 1

typedef struct C64Api {
    uint32_t size;
    uint8_t (*peek)(uint16_t addr);
    void (*poke)(uint16_t addr, uint8_t val);
    int (*dma_read)(uint16_t addr, void *dst, int len);
    int (*dma_write)(uint16_t addr, const void *src, int len);
    // Saves the bus registers, DMA memory only and the CPU port, then maps the cartridge: the window sees the C64's memory map (I/O, cartridge), only the
    // external bus is on and the CPU port selects BASIC, KERNAL and I/O. The mode is left alone. cart_restore_bus puts everything back.
    void (*cart_setup_bus)(void);
    void (*cart_restore_bus)(void);
    void (*cart_mode)(uint8_t mode);      // 0 normal, 2 ultimax, ...
    uint8_t (*cart_detect)(void);         // GAME (bit 0) and EXROM (bit 1) as read from the expansion port
    // Appended with the major 1 of this table, in this order:
    int (*dma_read_frozen)(uint16_t addr, void *dst, int len);   // C64 memory as the machine's own tools see it while it is frozen (the screen it had, not the menu over it); returns the bytes read
    int (*cart_preference)(void);         // Cartridge Preference: 0 Auto, 1 Internal, 2 External, 3 Manual
    void (*reset)(void);                  // a C64 reset (the cartridge tools end with one: the bank registers are write only); replaces SvcTrx.reset_c64
} C64Api;

#endif
