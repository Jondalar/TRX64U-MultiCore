/*
 * cart_screen.h
 *
 *  The screen the dumper and the writer share: a frame, a header naming the
 *  tool and the cartridge, the EasyFlash 3 slot, the CRT file, one bank map
 *  per chip with a legend beside the first, a status line and the keys.
 *
 *  The layout is the one drawn in tools/ui_editor (screens/cart_writer.json):
 *  chip names at column 1, row labels at 6, the sixteen cells at 10 to 25,
 *  the legend at 31, inside a frame one column in from the screen edge.
 */

#ifndef CART_SCREEN_H
#define CART_SCREEN_H

#include "ui_shim.h"
#include <stdbool.h>
#include <stdint.h>

// Inside the frame: screen rows 3 to 22, columns 1 to 38. The frame stops at
// row 23; a frame on the last row scrolled the whole screen up by one when its
// bottom-right corner was written.
#define CART_SCREEN_ROWS 20
#define CART_SCREEN_COLS 38

// Where each part goes, in window rows. -1 where a part is absent.
typedef struct {
    int banks;
    int rows;           // map rows per chip, 16 banks each
    bool has_romh;
    int y_header;
    int y_slot;         // EasyFlash 3 only
    int y_file;         // -1 when the map needs the line (256 banks)
    int y_roml;         // column header of the first map
    int y_romh;         // column header of the second map
    int y_status;
    int y_keys;
    int y_keys2;        // EasyFlash 3 only: F7=Select slot
} cart_layout_t;

// The framed window, 40 x 22 at row 2
Window *cart_screen_open(void);

// Places every part for a cartridge of `banks` banks. What does not fit as the
// editor drew it gives up its blank lines first, so 256 banks still fit.
void cart_layout_compute(cart_layout_t *l, int banks, bool has_romh, bool ef3);

// One row of text from column 1, cleared to the frame and cut at it
void cart_screen_line(Window *w, int y, int color, const char *text);

void cart_screen_status(Window *w, const cart_layout_t *l, const char *msg);

// "CRT: name", or "CRT: -" without one
void cart_screen_file(Window *w, const cart_layout_t *l, const char *name);

// Keys as label/action pairs: "F1", "Scan", "F3", "Dump", ...
void cart_screen_keys(Window *w, int y, const char *const *pairs, int npairs);

void cart_draw_bank_map(Window *w, const cart_layout_t *l, const uint8_t *status_lo, const uint8_t *status_hi);

#endif
