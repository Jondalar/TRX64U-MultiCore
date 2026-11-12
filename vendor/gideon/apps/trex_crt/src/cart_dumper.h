/*
 * cart_dumper.h
 *
 *  Dump physical cartridges to .CRT files via the U64 cart slot, with a
 *  visual bank map showing scan/dump progress. Port of the firmware
 *  CartDumper (software/io/c64/cart_dumper.h).
 */

#ifndef CART_DUMPER_H
#define CART_DUMPER_H

#include "cart_base.h"
#include "cart_screen.h"

typedef struct
{
    int hw_type;
    const char *type_name;
    bool banking_found;
    int bank_count;
} cart_probe_result_t;

typedef struct
{
    cart_base_t base;
    Window *window;

    // Detection
    int det_banks;
    int crt_hw_type;
    bool has_romh;      // true for 16K/Ultimax carts
    uint16_t romh_addr; // $A000 (16K) or $E000 (Ultimax)

    // Bank map (separate arrays for ROML and ROMH chips)
    uint8_t bank_status_lo[MAX_CART_BANKS];
    uint8_t bank_status_hi[MAX_CART_BANKS];

    // Detected probe result (saved for later use in the dump)
    cart_probe_result_t probe_result;

    // Where each part of the screen goes, and the file last written
    cart_layout_t layout;
    char file[40];
} cart_dumper_t;

// Opens the window, sets up the bus, probes and scans the cartridge
void cart_dumper_init(cart_dumper_t *d);
// Restores the bus and closes the window
void cart_dumper_deinit(cart_dumper_t *d);
// Handles one keymapped key; returns MENU_CLOSE to leave
int cart_dumper_poll(cart_dumper_t *d, int key);
void cart_dumper_redraw(cart_dumper_t *d);

#endif
