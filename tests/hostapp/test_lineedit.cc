// Host test of the one-line text field's keys and window (app/edit/lineedit.cc, T139 D3, T180 D8).
#include "lineedit.h"
#include "keyboard.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void type(LineEdit &e, const char *s) { for (; *s; s++) le_key(&e, (unsigned char)*s); }

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);                                                 // a mutant that overruns the buffer may die: its FAIL lines must be out by then
    LineEdit e;
    le_init(&e, "Amiga", 47, "|");
    CHECK(!strcmp(e.buf, "Amiga") && e.cur == 5 && e.len == 5);                    // prefilled, cursor at the end
    type(e, " 1200");
    CHECK(!strcmp(e.buf, "Amiga 1200") && e.cur == 10);
    le_key(&e, KEY_LEFT); le_key(&e, KEY_LEFT);
    type(e, "-");
    CHECK(!strcmp(e.buf, "Amiga 12-00") && e.cur == 9);                            // inserted at the cursor, not appended
    le_key(&e, KEY_BACK);
    CHECK(!strcmp(e.buf, "Amiga 1200") && e.cur == 8);                             // BACK removes the one before
    le_key(&e, KEY_DELETE);
    CHECK(!strcmp(e.buf, "Amiga 120") && e.cur == 8);                              // DEL removes the one under the cursor
    le_key(&e, KEY_HOME);
    CHECK(e.cur == 0);
    le_key(&e, KEY_BACK);
    CHECK(!strcmp(e.buf, "Amiga 120") && e.cur == 0);                              // nothing before the first
    le_key(&e, KEY_LEFT);
    CHECK(e.cur == 0);
    le_key(&e, KEY_END);
    CHECK(e.cur == e.len && e.len == 9);
    le_key(&e, KEY_DELETE);
    CHECK(e.len == 9);                                                             // nothing under the cursor behind the end
    le_key(&e, KEY_RIGHT);
    CHECK(e.cur == 9);
    le_key(&e, KEY_UP);
    CHECK(e.cur == 0);
    le_key(&e, KEY_DOWN);
    CHECK(e.cur == 9);
    le_key(&e, KEY_HOME); le_key(&e, KEY_INSERT);
    CHECK(!strcmp(e.buf, " Amiga 120") && e.cur == 0 && e.len == 10);              // INS opens a blank, the cursor stays
    CHECK(le_key(&e, KEY_RETURN) == LE_DONE && !strcmp(e.buf, " Amiga 120"));
    CHECK(le_key(&e, KEY_ESCAPE) == LE_CANCEL && le_key(&e, KEY_BREAK) == LE_CANCEL);
    CHECK(le_key(&e, 'x') == LE_BUSY);
    // the limit
    le_init(&e, "", 47, "|");
    for (int i = 0; i < 60; i++) {
        le_key(&e, 'a' + i % 26);
    }
    CHECK(e.len == 47 && e.buf[47] == 0 && e.cur == 47);                           // 47, not 48
    le_key(&e, KEY_INSERT);
    CHECK(e.len == 47);                                                            // INS does not grow it either
    le_init(&e, "0123456789012345678901234567890123456789012345678901", 47, 0);
    CHECK(e.len == 47);                                                            // a longer prefill is cut
    // rejected characters and the control codes that are not text
    le_init(&e, "ab", 47, "|");
    le_key(&e, '|'); le_key(&e, 0x01); le_key(&e, 200);
    CHECK(!strcmp(e.buf, "ab") && e.cur == 2);
    // blank and clear (CLEAR = SHIFT+HOME on the C64 keyboard empties the field, as it did before T180 D8)
    le_init(&e, "   ", 47, "|");
    CHECK(le_blank(&e));
    le_key(&e, 'x');
    CHECK(!le_blank(&e));
    le_key(&e, KEY_HOME);
    le_key(&e, KEY_CLEAR);
    CHECK(e.len == 0 && e.cur == 0 && e.buf[0] == 0 && le_blank(&e));
    // T180 D8: insert / delete at the start, the middle and the end, the bounds of the cursor
    le_init(&e, "bcd", 255, 0);
    le_key(&e, KEY_HOME); type(e, "a");
    CHECK(!strcmp(e.buf, "abcd") && e.cur == 1 && e.len == 4);                     // at the start
    le_key(&e, KEY_END); type(e, "e");
    CHECK(!strcmp(e.buf, "abcde") && e.cur == 5);                                  // at the end
    le_key(&e, KEY_LEFT); le_key(&e, KEY_LEFT); type(e, "X");
    CHECK(!strcmp(e.buf, "abcXde") && e.cur == 4);                                 // in the middle
    le_key(&e, KEY_BACK);
    CHECK(!strcmp(e.buf, "abcde") && e.cur == 3);                                  // DEL takes the one before the cursor
    le_key(&e, KEY_HOME); le_key(&e, KEY_BACK);
    CHECK(!strcmp(e.buf, "abcde") && e.cur == 0);                                  // nothing before the start
    for (int i = 0; i < 9; i++) le_key(&e, KEY_RIGHT);
    CHECK(e.cur == 5);                                                             // RIGHT stops behind the last
    for (int i = 0; i < 9; i++) le_key(&e, KEY_LEFT);
    CHECK(e.cur == 0);                                                             // LEFT stops at the first
    // 255 characters (#38): the limit is the name's, the cursor edits any of them
    le_init(&e, "", 255, 0);
    for (int i = 0; i < 300; i++) le_key(&e, 'a' + i % 26);
    CHECK(e.len == 255 && e.cur == 255 && e.buf[255] == 0);
    le_key(&e, KEY_HOME); le_key(&e, 'x');
    CHECK(e.len == 255 && e.buf[0] == 'a');                                        // full: an insert in front is refused too
    le_key(&e, KEY_DELETE); type(e, "x");
    CHECK(e.len == 255 && e.buf[0] == 'x' && e.buf[1] == 'b' && e.cur == 1);       // one out, one in
    // the window: the cursor stays inside, the least movement, never blank space behind the end
    le_init(&e, "0123456789", 255, 0);
    CHECK(le_scroll(&e, 20) == 0);                                                 // fits: no window
    le_init(&e, "0123456789ABCDEFGHIJ", 255, 0);                                   // 20 characters, cursor at 20, window 8
    CHECK(le_scroll(&e, 8) == 12 && e.first == 12);                                // 12..19, cursor behind the 20th
    le_key(&e, KEY_LEFT); le_key(&e, KEY_LEFT);
    CHECK(le_scroll(&e, 8) == 12);                                                 // the cursor (18) is inside: the window stays
    for (int i = 0; i < 6; i++) le_key(&e, KEY_LEFT);
    CHECK(e.cur == 12 && le_scroll(&e, 8) == 12);                                  // at the window's left edge: still
    le_key(&e, KEY_LEFT);
    CHECK(le_scroll(&e, 8) == 11);                                                 // one more: the window follows by one
    le_key(&e, KEY_HOME);
    CHECK(le_scroll(&e, 8) == 0);
    le_key(&e, KEY_END);
    CHECK(le_scroll(&e, 8) == 12);
    le_key(&e, KEY_BACK); le_key(&e, KEY_BACK); le_key(&e, KEY_BACK);
    CHECK(e.len == 17 && le_scroll(&e, 8) == 9);                                   // shorter: the window follows, no blank tail
    // the row: the cursor '_' at its place, ".." where text is hidden, never wider than the cell count
    char row[64];
    le_init(&e, "abcd", 255, 0);
    le_row(&e, 20, 0, row, sizeof(row));
    CHECK(!strcmp(row, "abcd_"));
    le_key(&e, KEY_LEFT); le_key(&e, KEY_LEFT);
    le_row(&e, 20, 0, row, sizeof(row));
    CHECK(!strcmp(row, "ab_cd"));                                                  // the cursor drawn where it is
    le_key(&e, KEY_HOME);
    le_row(&e, 20, 0, row, sizeof(row));
    CHECK(!strcmp(row, "_abcd"));
    le_row(&e, 20, '*', row, sizeof(row));
    CHECK(!strcmp(row, "_****"));                                                  // a secret shows the mask, the cursor as well
    le_init(&e, "0123456789ABCDEFGHIJ", 255, 0);                                   // 20 + cursor in 12 cells: window of 7
    le_row(&e, 12, 0, row, sizeof(row));
    CHECK(!strcmp(row, "..DEFGHIJ_") && (int)strlen(row) <= 12);                   // the tail with the cursor, hidden start marked
    for (int i = 0; i < 20; i++) le_key(&e, KEY_LEFT);
    le_row(&e, 12, 0, row, sizeof(row));
    CHECK(!strcmp(row, "_0123456..") && (int)strlen(row) <= 12);                   // at the start: hidden end marked
    for (int i = 0; i < 10; i++) le_key(&e, KEY_RIGHT);
    le_row(&e, 12, 0, row, sizeof(row));
    CHECK(!strcmp(row, "..3456789_..") && (int)strlen(row) <= 12);                 // the middle: both
    printf(fails ? "FAILED %d\n" : "test_lineedit: all ok\n", fails);
    return fails != 0;
}
