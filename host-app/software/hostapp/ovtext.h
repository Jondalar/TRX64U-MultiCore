// T135 D5: feedback of a direct start without LVGL: one or two centred lines in the hardware text layer (host/overlay.v, the Ultimate menu overlay
// at U64II_OVERLAY_BASE: registers +0x00..0x0D as the char generator's, screen RAM +0x1000, colour RAM +0x2000). The overlay centres its box on the
// 1920 x 1080 frame by itself and scales the character cells to the largest 1..4 that fit, so a 40 column box would be drawn 4x4 even in 720p; the box is 41 x 21 cells (D13) and comes out 3x3 in 720p, 4x4 in 1080p.
#ifndef HOST_OVTEXT_H
#define HOST_OVTEXT_H

#include <stdint.h>

// T135 D13: text up to 40 columns in a box of 41 x 21 cells (8 x 9 pixels): see ovtext.cc for why the box is that size.
#define OVTEXT_COLS 40
#define OVTEXT_BOX_COLS 41
#define OVTEXT_BOX_ROWS 21

// "Starting <name> - hold C= for the menu" in one line when it fits 40 columns, else the name on a line and the hint under it. Returns the
// number of milliseconds it took (ITU ms timer, so 0 means under a millisecond).
uint32_t ovtext_start_line(const char *name);

// A single centred line (the wait for a medium, a refusal before the menu is up).
void ovtext_line(const char *s);

// Overlay off and the screen RAM blank again, so the core that starts next finds the layer as the host found it.
void ovtext_off(void);

// For the bench: the line this text becomes (<= 40 columns), written to out. Returns the number of rows (1 or 2).
int ovtext_layout(const char *name, char out[2][41]);

// For the bench: the screen and colour RAM image of the box (OVTEXT_BOX_COLS x OVTEXT_BOX_ROWS bytes each) for 1 or 2 text rows.
void ovtext_fill(const char (*row)[41], int rows, uint8_t *scr, uint8_t *col);

#endif
