#ifndef UI_MODE_H
#define UI_MODE_H

#include <stdint.h>

// Which user interface the machine presents. There is one firmware and both
// interfaces are in it; the board decides which one is offered by default and
// the user can say otherwise in the settings.
//
// Everything the two disagree about is in one table, so that a place which
// has to draw or answer differently asks the theme rather than asking which
// interface it is and deciding for itself. Adding a third would be a third
// row, not a third branch in twenty files.
//
// This header carries nothing but the question, so that the places which ask
// -- the screen, the browsers, a file type that offers one action rather than
// another -- do not have to pull the user interface in with it.

struct t_ui_theme
{
    const char *name;

    // The corner glyphs, named for where they are placed. The glyph names in
    // screen.h read the other way round: the one called LOWER_RIGHT is the
    // piece whose lines run down and to the right, so it goes in the top left.
    uint8_t corner_upper_left;
    uint8_t corner_upper_right;
    uint8_t corner_lower_left;
    uint8_t corner_lower_right;

    // Whether the title row is the machine's name over a rule, or the logo
    // header of two bar rows with the Commodore mark in it.
    bool logo_title;

    // Whether every browser sits in a frame, or only the ones that ask.
    bool browser_has_border;

    // What the four function keys that the two lay out differently mean.
    // F2, F4 and F6 agree, so they are not in here.
    uint16_t key_f1, key_f3, key_f5, key_f7;

    // The hint in the browser's last row, and the four lines of the help
    // text that name the same keys. Both follow from the four above, and are
    // written out because a screen has to read as words.
    const char *help_hint;
    const char *help_fkeys;
};

const t_ui_theme &ui_theme(void);
bool ui_is_commodore(void);

#endif
