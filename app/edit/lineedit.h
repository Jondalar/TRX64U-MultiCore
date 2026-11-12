// A one-line text field's state and keys, shared by the host application and the core application (T139 D3; T180 D8: one editor for every text
// entry, names up to 255 characters). The key behaviour is the generic application's UIStringEdit (cursor keys, DEL, HOME / END, RETURN, ESC);
// drawing is the caller's, le_scroll / le_row give it the window that keeps the cursor in sight. Pure code: tests/hostapp/test_lineedit.cc runs it.
#ifndef HOST_LINEEDIT_H
#define HOST_LINEEDIT_H

#define LE_MAX 255

struct LineEdit {
    char buf[LE_MAX + 1];
    int len, cur, max;            // max <= LE_MAX
    int first;                    // the first character a row shows (le_scroll keeps the cursor inside the window)
    const char *reject;           // characters the field does not take (may be 0)
};

enum { LE_CANCEL = -1, LE_BUSY = 0, LE_DONE = 1 };

// The field holds `text` (cut to max), the cursor behind it.
void le_init(LineEdit *e, const char *text, int max, const char *reject);

// One key: LE_DONE on RETURN (the caller decides whether the text is acceptable), LE_CANCEL on ESC / RUN-STOP, LE_BUSY otherwise.
//   printable      inserted at the cursor while len < max (a rejected character is dropped)
//   LEFT / RIGHT   cursor;  HOME or UP   first character;  END or DOWN (CRSR down on the C64 keyboard)   behind the last one
//   BACK (INST/DEL on the C64 keyboard)   deletes the character before the cursor;  DEL (forward)   the one under it
//   INS (SHIFT+INST/DEL)   opens a blank at the cursor;  CLEAR (SHIFT+HOME)   empties the field
int le_key(LineEdit *e, int key);

// Moves e->first the least that shows the cursor in a window of `width` characters (the cursor may stand behind the last one shown); no window
// the text does not need: it returns the first character.
int le_scroll(LineEdit *e, int width);

// The row text of a field `vis` cells wide, written to out[outn]: the whole text when it fits with the cursor, else the window of le_scroll
// with ".." where text is hidden. The cursor is a '_' between the characters, where the next typed one goes. mask != 0 shows that character
// for every one (a secret).
void le_row(LineEdit *e, int vis, char mask, char *out, int outn);

// Nothing but spaces (or nothing at all).
bool le_blank(const LineEdit *e);

#endif
