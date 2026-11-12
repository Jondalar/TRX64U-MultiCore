/*
 * cart_screen.c
 *
 *  See cart_screen.h. Three faults turned up while this layout was first
 *  built, and each has a rule here: a line that ran past the frame wrapped
 *  into the next one and ate what was drawn there, so every line is cut at
 *  the frame; the legend is four lines tall, so a short first map still
 *  leaves room for it and the status line does not land on "OK" and "Err";
 *  and a screen is repainted by clearing the inside, never by drawing the
 *  frame again, which would nest a second frame inside the first.
 */

#include "cart_screen.h"
#include "cart_base.h"

#define COL_TEXT      1   // header, slot, file, status label, keys
#define COL_CHIP      1   // "ROML" / "ROMH"
#define COL_ROWLABEL  6   // "00:"
#define COL_CELLS     10  // sixteen cells, and the column header above them
#define COL_LEGEND    31  // legend glyph
#define COL_LEGENDTXT 33  // legend word
#define COL_STATUSMSG 10  // after "STATUS : "

#define COLOR_HEADER 1
#define COLOR_LABEL  15
#define COLOR_STATUS 1
#define COLOR_KEY    3

// The round block of the firmware's UI font. One per bank, next to each
// other: spaced out, sixteen banks need 41 columns and a frame leaves 38.
#define GLYPH_BANK 0x1A
#define GLYPH_NONE '.'

Window *cart_screen_open(void)
{
    return (Window *)g_api->ui->screen_open("", 40, CART_SCREEN_ROWS + 2);
}

void cart_layout_compute(cart_layout_t *l, int banks, bool has_romh, bool ef3)
{
    int rows = (banks + 15) / 16;
    if (rows < 1)
        rows = 1;
    if (rows > 16)
        rows = 16;
    int first = rows < 4 ? 4 : rows; // the legend is four lines tall

    int content = 1 + (ef3 ? 1 : 0) + 1 + 1 + first + (has_romh ? 1 + rows : 0) + 1 + 1 + (ef3 ? 1 : 0);
    // A 256-bank map is sixteen rows. With every blank line gone it is still
    // one line too many, and the file line is the one that can go: the header
    // names the cartridge, and the file is chosen in a dialog anyway.
    bool file_line = content <= CART_SCREEN_ROWS;
    if (!file_line)
        content--;
    int spare = CART_SCREEN_ROWS - content;

    // The blank lines of the editor's screen, in the order they are kept
    int gap_file = 0, gap_map = 0, gap_maps = 0, gap_status = 0, gap_keys = 0, gap_top = 0;
    int *gaps[6] = { &gap_file, &gap_map, &gap_maps, &gap_status, &gap_keys, &gap_top };
    for (int i = 0; i < 6 && spare > 0; i++) {
        if (gaps[i] == &gap_maps && !has_romh)
            continue;
        *gaps[i] = 1;
        spare--;
    }

    int y = gap_top;
    l->banks = banks;
    l->rows = rows;
    l->has_romh = has_romh;
    l->y_header = y++;
    l->y_slot = ef3 ? y++ : -1;
    y += gap_file;
    l->y_file = file_line ? y++ : -1;
    y += gap_map;
    l->y_roml = y;
    y += 1 + first;
    if (has_romh) {
        y += gap_maps;
        l->y_romh = y;
        y += 1 + rows;
    } else {
        l->y_romh = -1;
    }
    y += gap_status;
    l->y_status = y++;
    y += gap_keys;
    l->y_keys = y++;
    l->y_keys2 = ef3 ? y : -1;
}

static void put_clamped(Window *w, int x, int y, const char *text)
{
    char cut[CART_SCREEN_COLS + 1];
    int room = CART_SCREEN_COLS - x;
    int i = 0;
    while (text && text[i] && i < room) {
        cut[i] = text[i];
        i++;
    }
    cut[i] = 0;
    window_move_cursor(w, x, y);
    window_output(w, cut);
}

void cart_screen_line(Window *w, int y, int color, const char *text)
{
    if (!w || y < 0)
        return;
    window_reverse_mode(w, 0);
    window_move_cursor(w, 0, y);
    window_set_color(w, color);
    for (int i = 0; i < CART_SCREEN_COLS; i++)
        window_output_char(w, ' ');
    put_clamped(w, COL_TEXT, y, text);
}

void cart_screen_status(Window *w, const cart_layout_t *l, const char *msg)
{
    if (!w)
        return;
    cart_screen_line(w, l->y_status, COLOR_LABEL, "STATUS :");
    window_set_color(w, COLOR_STATUS);
    put_clamped(w, COL_STATUSMSG, l->y_status, msg);
}

void cart_screen_file(Window *w, const cart_layout_t *l, const char *name)
{
    if (!w || l->y_file < 0)
        return;
    char line[CART_SCREEN_COLS + 1];
    ui_snprintf(line, sizeof(line), "CRT: %s", (name && name[0]) ? name : "-");
    cart_screen_line(w, l->y_file, COLOR_LABEL, line);
}

void cart_screen_keys(Window *w, int y, const char *const *pairs, int npairs)
{
    if (!w || y < 0)
        return;
    cart_screen_line(w, y, COLOR_LABEL, "");
    int x = COL_TEXT;
    window_move_cursor(w, x, y);
    for (int i = 0; i + 1 < npairs * 2; i += 2) {
        char word[CART_SCREEN_COLS + 1];
        int klen = 0, wlen;
        while (pairs[i][klen])
            klen++;
        wlen = ui_snprintf(word, sizeof(word), "=%s ", pairs[i + 1]);
        if (x + klen + wlen - 1 > CART_SCREEN_COLS)
            break;
        window_set_color(w, COLOR_KEY);
        window_output(w, pairs[i]);
        window_set_color(w, COLOR_LABEL);
        window_output(w, word);
        x += klen + wlen;
    }
}

static void draw_grid(Window *w, const uint8_t *status, int banks, int rows, int y, const char *label)
{
    window_reverse_mode(w, 0);
    window_set_color(w, COLOR_LABEL);
    window_move_cursor(w, COL_CELLS, y);
    for (int c = 0; c < 16; c++)
        window_output_char(w, "0123456789ABCDEF"[c]);

    for (int r = 0; r < rows; r++) {
        window_set_color(w, COLOR_LABEL);
        if (label && r == 0) {
            window_move_cursor(w, COL_CHIP, y + 1 + r);
            window_output(w, label);
        }
        char rowlabel[5];
        ui_snprintf(rowlabel, sizeof(rowlabel), "%02X:", r * 16);
        window_move_cursor(w, COL_ROWLABEL, y + 1 + r);
        window_output(w, rowlabel);

        window_move_cursor(w, COL_CELLS, y + 1 + r);
        for (int c = 0; c < 16; c++) {
            int bank = r * 16 + c;
            uint8_t st = (bank < banks) ? status[bank] : BANK_UNKNOWN;
            window_set_color(w, cart_bank_color(st));
            window_output_char(w, (st == BANK_EMPTY || st == BANK_UNKNOWN) ? GLYPH_NONE : GLYPH_BANK);
        }
    }
}

static void draw_legend(Window *w, int y)
{
    static const uint8_t states[4] = { BANK_EMPTY, BANK_FILLED, BANK_VERIFIED, BANK_ERROR };
    // Four columns wide: the frame wants column 38, where the editor put it
    static const char *words[4] = { "Empt", "Data", "OK", "Err" };
    for (int i = 0; i < 4; i++) {
        window_reverse_mode(w, 0);
        window_move_cursor(w, COL_LEGEND, y + 1 + i);
        window_set_color(w, cart_bank_color(states[i]));
        window_output_char(w, states[i] == BANK_EMPTY ? GLYPH_NONE : GLYPH_BANK);
        window_move_cursor(w, COL_LEGENDTXT, y + 1 + i);
        window_set_color(w, COLOR_LABEL);
        window_output(w, words[i]);
    }
}

void cart_draw_bank_map(Window *w, const cart_layout_t *l, const uint8_t *status_lo, const uint8_t *status_hi)
{
    if (!w || !l)
        return;
    draw_grid(w, status_lo, l->banks, l->rows, l->y_roml, l->has_romh ? "ROML" : 0);
    draw_legend(w, l->y_roml);
    if (l->has_romh && status_hi)
        draw_grid(w, status_hi, l->banks, l->rows, l->y_romh, "ROMH");
}
