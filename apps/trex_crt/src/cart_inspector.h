/*
 * cart_inspector.h
 *
 *  Hex viewer for examining physical cartridges via DMA. Port of the
 *  firmware CartInspector (software/io/c64/cart_inspector.h).
 */

#ifndef CART_INSPECTOR_H
#define CART_INSPECTOR_H

#include "cart_base.h"

typedef struct
{
    cart_base_t base;
    Window *window;

    uint16_t view_addr;
    uint8_t bank_reg;
    bool ef3; // bank 0 shows the EAPI of an EasyFlash 3 (MX29LV640EB)
    bool failed; // the session did not open (T57): any key leaves
} cart_inspector_t;

// Opens the window, sets up the bus, selects bank 0 and draws the view
void cart_inspector_init(cart_inspector_t *t);
// Restores the bus and closes the window
void cart_inspector_deinit(cart_inspector_t *t);
// Handles one keymapped key; returns MENU_CLOSE to leave
int cart_inspector_poll(cart_inspector_t *t, int key);
void cart_inspector_redraw(cart_inspector_t *t);

#endif
