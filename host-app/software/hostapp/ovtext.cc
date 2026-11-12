#include "ovtext.h"
#include <string.h>
#include <stdio.h>
#ifndef OVTEXT_BENCH
#include "u64.h"
#include "itu.h"
#endif

// T135 D13: the text is at most OV_COLS columns, the box the gateware scales is OV_BOX_COLS x OV_BOX_ROWS cells of 8 x 9 (328 x 189 pixels).
// host/overlay.v picks the scale from the box against the active area (largest 1..4 with n x box <= area, x at most 2 x y): 41 columns
// are over 1280 / 4, 21 rows over 720 / 4, so 720p draws 3 x 3 and 1080p 4 x 4. The rest of the box is blank on palette 0 (black).
#define OV_COLS OVTEXT_COLS
#define OV_ROWS 2
#define OV_BOX_COLS OVTEXT_BOX_COLS
#define OV_BOX_ROWS OVTEXT_BOX_ROWS

// the register offsets of host/overlay.v (write only): 2 char width, 3 char height (bit 6 big font, bit 7 stretch), 4 chars per line,
// 5 active lines, 0xA/0xB text pointer, 0xD transparency (3:0), own keyboard (6), on (7). The colour byte is background << 4 | foreground.
#define OV_REG_CW  2
#define OV_REG_CH  3
#define OV_REG_CPL 4
#define OV_REG_NL  5
#define OV_REG_PH  0xA
#define OV_REG_PL  0xB
#define OV_REG_ON  0xD

int ovtext_layout(const char *name, char out[2][41])
{
    char one[160];
    snprintf(one, sizeof(one), "Starting %s - hold C= for the menu", name);
    memset(out, 0, 2 * 41);
    if (strlen(one) <= OV_COLS) {
        strcpy(out[0], one);
        return 1;
    }
    snprintf(out[0], 41, "Starting %s", name);            // cut at the 40th column
    strcpy(out[1], "Hold C= for the menu");
    return 2;
}

void ovtext_fill(const char (*row)[41], int rows, uint8_t *scr, uint8_t *col)
{
    int top = (OV_BOX_ROWS - rows) / 2;                   // the text rows in the middle of the box
    for (int r = 0; r < OV_BOX_ROWS; r++) {
        int tr = r - top;
        int len = (tr >= 0 && tr < rows) ? (int)strlen(row[tr]) : 0;
        int pad = (OV_BOX_COLS - len) / 2;
        for (int c = 0; c < OV_BOX_COLS; c++) {
            int k = c - pad;
            scr[r * OV_BOX_COLS + c] = (k >= 0 && k < len) ? (uint8_t)row[tr][k] : ' ';
            col[r * OV_BOX_COLS + c] = 0x01;              // white (palette 1) on black (palette 0)
        }
    }
}

#ifndef OVTEXT_BENCH
static volatile uint8_t *ov(int off) { return (volatile uint8_t *)(U64II_OVERLAY_BASE + off); }

static uint32_t ms_now(void) { return getMsTimer(); }

static void put_rows(const char (*row)[41], int rows)
{
    volatile uint8_t *scr = (volatile uint8_t *)U64II_CHARGEN_SCREEN_RAM;
    volatile uint8_t *col = (volatile uint8_t *)U64II_CHARGEN_COLOR_RAM;
    static uint8_t s_img[OV_BOX_COLS * OV_BOX_ROWS], c_img[OV_BOX_COLS * OV_BOX_ROWS];
    ovtext_fill(row, rows, s_img, c_img);
    *ov(OV_REG_ON) = 0;                                   // dark while the cells are written
    *ov(OV_REG_CW) = 8;
    *ov(OV_REG_CH) = 9;                                   // 8 x 9 cells, the box 328 x 189
    *ov(OV_REG_CPL) = OV_BOX_COLS;
    *ov(OV_REG_NL) = OV_BOX_ROWS;
    *ov(OV_REG_PH) = 0;
    *ov(OV_REG_PL) = 0;
    for (int i = 0; i < OV_BOX_COLS * OV_BOX_ROWS; i++) {
        scr[i] = s_img[i];
        col[i] = c_img[i];
    }
    *ov(OV_REG_ON) = 0x80 | 0x0F;                         // on; colour 15 is the transparent one, which neither the text nor the ground uses
}

uint32_t ovtext_start_line(const char *name)
{
    uint32_t t0 = ms_now();
    char rows[2][41];
    int n = ovtext_layout(name, rows);
    put_rows(rows, n);
    return (uint32_t)(uint16_t)(ms_now() - t0);
}

void ovtext_line(const char *s)
{
    char rows[2][41];
    memset(rows, 0, sizeof(rows));
    strncpy(rows[0], s, OV_COLS);
    put_rows(rows, 1);
}

void ovtext_off(void)
{
    volatile uint8_t *scr = (volatile uint8_t *)U64II_CHARGEN_SCREEN_RAM;
    *ov(OV_REG_ON) = 0;
    for (int i = 0; i < OV_BOX_COLS * OV_BOX_ROWS; i++) {
        scr[i] = ' ';
    }
}
#endif
