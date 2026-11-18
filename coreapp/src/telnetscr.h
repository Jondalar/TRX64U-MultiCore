// The text screen of the Telnet remote menu (T228 R21, row F41): the generic menu (MenuCtl, the state the HDMI menu draws) as 60 x 24 cells of text and the
// VT100 sequences that bring a terminal to that picture, and the keys of a terminal as the menu's keys. Pure: no socket, no clock of its own, so it links into the
// native build too. The firmware serves its menu the same way: Screen_VT100 (60 x 24, io/stream/screen_vt100.h:25-26, its colours and "\ec" at :14-17, :36-58),
// Keyboard_VT100 (io/stream/keyboard_vt100.cc:getch, the 150 ms of a lone ESC in keyboard_vt100.h:54). The server around it is telnetgui.cc.
#ifndef COREAPP_TELNETSCR_H
#define COREAPP_TELNETSCR_H

#include <stdint.h>
#include "menuctl.h"
#include "appdlg.h"

#define TS_COLS 60                        // Screen_VT100::get_size_x
#define TS_ROWS 24                        // Screen_VT100::get_size_y

// What the screen shows besides the menu itself: the dialog of an application that asks (appdlg.h), the picture mode question (picture.h).
struct TsOver {
    const DlgView *dlg;                   // null or kind DK_NONE: none
    const char *picture;                  // the picture question's text ("..., N s"), null: none
};

// The bytes of a terminal as keys, a port of Keyboard_VT100::getch with its bytes pushed in. The telnet commands (IAC and the two bytes after it) are dropped as
// SocketStream::get_char drops them (network/socket_stream.cc:20-31).
class TelnetKeys {
public:
    TelnetKeys() { reset(); }
    void reset();
    void put(uint8_t b);                  // one byte from the socket
    // The next key (a KEY_* code of keyboard.h, or an ASCII character), or -1 when there is none yet. `now` is a millisecond clock (it only has to be monotonic).
    int get(uint32_t now);
private:
    enum { FIFO = 128 };
    uint8_t q_[FIFO];
    int qh_, qn_;
    int skip_;                            // the bytes left of a telnet command
    int state_, value_, pending_;
    uint32_t started_;
    bool after_cr_;
    int pull();
};

class TelnetScreen {
public:
    TelnetScreen() { reset(); }
    // A new terminal (or one that has to be drawn from nothing): the next frame() draws every row.
    void reset();
    // The telnet greeting of Screen_VT100's constructor (IAC DONT LINEMODE, IAC WILL ECHO, reset, colour, home); returns the length.
    static int greeting(char *out, int cap);
    // What Screen_VT100::restore_terminal sends when the session ends.
    static int farewell(char *out, int cap);
    // Composes the screen of `m` (open: the menu is up; else the notice that it is not) and writes the VT100 bytes of the rows that changed against what the terminal
    // shows; returns their number (0: nothing changed). Rows that do not fit `cap` stay unmarked and come with the next call.
    int frame(MenuCtl *m, bool open, const TsOver &over, char *out, int cap);
private:
    struct Cell { char c; uint8_t a; };
    Cell g_[TS_ROWS][TS_COLS];            // composed
    Cell s_[TS_ROWS][TS_COLS];            // what the terminal shows
    bool dirty_all_;
    int first_, key_;                     // the window of the list, and the screen it belongs to
    void clear();
    void put(int row, int col, const char *s, int maxw, uint8_t attr);
    void fill(int row, uint8_t attr);
    void put_right(int row, const char *s, uint8_t attr);
    void line(int row, bool sel, const char *left, const char *right, uint8_t attr, bool heading);
    void box(int top, int height, const char *title);
    int wrap(int row, int maxrow, const char *text, uint8_t attr);
    void compose(MenuCtl *m, bool open, const TsOver &over);
    void bars(MenuCtl *m);
    void list(MenuCtl *m);
    void modal(MenuCtl *m, const char *kind, const char *text);
    void over_dialog(const DlgView *v);
    int emit(int row, char *out, int cap);
};

#endif
