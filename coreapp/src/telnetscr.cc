// The text screen of the Telnet remote menu (telnetscr.h). The rows are the ones the UI service's snapshot draws from MenuCtl (uiapp.cc snapshot: items, library rows,
// the file selector, the actions of a file, the prompts) - the same state, the same indexes, so a key moves the cursor on both screens alike.
#include "telnetscr.h"
#include "keyboard.h"
#include <stdio.h>
#include <string.h>

enum { A_NORM = 0, A_REV, A_DIM, A_BOLD, A_CYAN, A_AMBER, A_RED, A_N };

#define LIST_TOP 4                      // rows 0-3: the top bar, the areas, the tabs, the crumb
#define LIST_ROWS 18                    // rows 4-21
#define MSG_ROW 22
#define FOOT_ROW 23
#define ALONE_MS 150                    // VT100_ESCAPE_ALONE_MS (keyboard_vt100.h:54)

// ---- the keys ------------------------------------------------------------------------------------------------------------------------------

void TelnetKeys::reset()
{
    qh_ = qn_ = 0;
    skip_ = 0;
    state_ = 0;
    value_ = 0;
    pending_ = 0;
    started_ = 0;
    after_cr_ = false;
}

void TelnetKeys::put(uint8_t b)
{
    if (b == 0xFF) {                    // IAC: the command and its option are dropped (socket_stream.cc:20-31)
        skip_ = 2;
        return;
    }
    if (skip_ > 0) {
        skip_--;
        return;
    }
    if (qn_ < FIFO) {
        q_[(qh_ + qn_) % FIFO] = b;
        qn_++;
    }
}

int TelnetKeys::pull()
{
    if (!qn_) {
        return -1;
    }
    int b = q_[qh_];
    qh_ = (qh_ + 1) % FIFO;
    qn_--;
    return b;
}

enum { S_IDLE = 0, S_ESC, S_O, S_BRACKET };

int TelnetKeys::get(uint32_t now)
{
    static const short cursor[] = { KEY_UP, KEY_DOWN, KEY_RIGHT, KEY_LEFT };
    static const short numeric[] = {
        0, KEY_HOME, KEY_INSERT, KEY_DELETE, KEY_END, KEY_PAGEUP, KEY_PAGEDOWN, 0, 0, 0,
        0, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, 0, KEY_F6, KEY_F7, KEY_F8,
        KEY_F9, KEY_F10, 0, KEY_F11, KEY_F12, 0, 0, 0, 0, 0, 0 };
    static const short function[] = { KEY_F1, KEY_F2, KEY_F3, KEY_F4 };
    for (;;) {
        int ch;
        switch (state_) {
        case S_IDLE:
            if (pending_) {
                ch = pending_;
                pending_ = 0;
            } else {
                ch = pull();
            }
            if (ch < 0) {
                return -1;
            }
            if (ch == 0x7F) {                         // the Backspace key of a terminal (keyboard_vt100.cc:36-41)
                after_cr_ = false;
                return KEY_BACK;
            }
            if (ch == '\e') {
                state_ = S_ESC;
                started_ = now;
                continue;
            }
            if (ch == 0x12) {                         // Ctrl+R (a real down arrow comes as ESC [ B)
                return KEY_CTRL_R;
            }
            if (ch == '\r') {
                after_cr_ = true;
                return KEY_RETURN;
            }
            if ((ch == '\n' || ch == 0) && after_cr_) {   // the end of the line a telnet client sends after Enter (CR LF or CR NUL): not a key
                after_cr_ = false;
                continue;
            }
            after_cr_ = false;
            if (ch == 0) {
                continue;
            }
            return ch;
        case S_ESC:
            ch = pull();
            if (ch < 0) {
                if ((uint32_t)(now - started_) >= ALONE_MS) {   // only the gap tells a lone ESC from a sequence still arriving
                    state_ = S_IDLE;
                    return KEY_ESCAPE;
                }
                return -1;
            }
            if (ch == 'O') {
                state_ = S_O;
            } else if (ch == '[') {
                state_ = S_BRACKET;
                value_ = 0;
            } else if (ch >= '0' && ch <= '9') {
                state_ = S_IDLE;
                return key_ctrl_digit(ch - '0');
            } else if (ch == 'b' || ch == 'B') {
                state_ = S_IDLE;
                return KEY_CTRL_B;
            } else if (ch == '\e') {                  // a second ESC: the first is delivered now, the second gets its own gap
                started_ = now;
                return KEY_ESCAPE;
            } else {
                state_ = S_IDLE;
                if (ch > 0 && !pending_) {            // a key straight after ESC: handed back, not dropped
                    pending_ = ch;
                }
                return KEY_ESCAPE;
            }
            continue;
        case S_O:
            ch = pull();
            if (ch < 0) {
                return -1;
            }
            state_ = S_IDLE;
            if (ch >= 'P' && ch <= 'S') {
                return function[ch - 'P'];
            }
            continue;
        default:                                      // S_BRACKET
            ch = pull();
            if (ch < 0) {
                return -1;
            }
            if (ch >= '0' && ch <= '9') {
                value_ = value_ * 10 + (ch - '0');
                if (value_ > 9999) {
                    value_ = 9999;
                }
                continue;
            }
            state_ = S_IDLE;
            if (ch == '~') {
                if (value_ < 31 && numeric[value_]) {
                    return numeric[value_];
                }
            } else if (ch >= 'A' && ch <= 'D') {
                return cursor[ch - 'A'];
            }
            continue;
        }
    }
}

// ---- the picture ---------------------------------------------------------------------------------------------------------------------------

void TelnetScreen::reset()
{
    memset(g_, 0, sizeof(g_));
    memset(s_, 0, sizeof(s_));
    dirty_all_ = true;
    first_ = 0;
    key_ = -1;
}

int TelnetScreen::greeting(char *out, int cap)
{
    // IAC DONT LINEMODE, IAC WILL ECHO (screen_vt100.cc:13), then "\ec", the colour and the cursor home as Screen_VT100's constructor does
    static const char g[] = "\377\376\042\377\373\001\ec\e[0m\e[1;1H";
    if (cap < (int)sizeof(g)) {
        return 0;
    }
    memcpy(out, g, sizeof(g) - 1);
    return (int)sizeof(g) - 1;
}

int TelnetScreen::farewell(char *out, int cap)
{
    static const char g[] = "\ec\e[2J\e[1;1H";                // restore_terminal (screen_vt100.cc:107-110)
    if (cap < (int)sizeof(g)) {
        return 0;
    }
    memcpy(out, g, sizeof(g) - 1);
    return (int)sizeof(g) - 1;
}

void TelnetScreen::clear()
{
    for (int r = 0; r < TS_ROWS; r++) {
        for (int c = 0; c < TS_COLS; c++) {
            g_[r][c].c = ' ';
            g_[r][c].a = A_NORM;
        }
    }
}

// The text as cells: control characters are blanks, a UTF-8 sequence is one '?' (the terminal's character set is not ours to know).
void TelnetScreen::put(int row, int col, const char *s, int maxw, uint8_t attr)
{
    if (row < 0 || row >= TS_ROWS) {
        return;
    }
    for (; *s && maxw > 0 && col < TS_COLS; s++) {
        unsigned char ch = (unsigned char)*s;
        if (col < 0) {
            col++;
            continue;
        }
        if (ch >= 0x80) {
            if (ch < 0xC0) {
                continue;                                    // a continuation byte
            }
            ch = '?';
        } else if (ch < 0x20 || ch == 0x7F) {
            ch = ' ';
        }
        g_[row][col].c = (char)ch;
        g_[row][col].a = attr;
        col++;
        maxw--;
    }
}

void TelnetScreen::fill(int row, uint8_t attr)
{
    for (int c = 0; c < TS_COLS; c++) {
        g_[row][c].c = ' ';
        g_[row][c].a = attr;
    }
}

static int cells(const char *s)
{
    int n = 0;
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch < 0x80 || ch >= 0xC0) {
            n++;
        }
    }
    return n;
}

void TelnetScreen::put_right(int row, const char *s, uint8_t attr)
{
    int n = cells(s);
    if (n > TS_COLS - 2) {
        n = TS_COLS - 2;
    }
    put(row, TS_COLS - 1 - n, s, n, attr);
}

// One row of a list: the marker, the text at the left, the value at the right edge (the value keeps its place, the text is cut).
void TelnetScreen::line(int row, bool sel, const char *left, const char *right, uint8_t attr, bool heading)
{
    uint8_t a = sel ? A_REV : attr;
    fill(row, sel ? A_REV : A_NORM);
    if (heading) {
        put(row, 1, left, TS_COLS - 2, A_BOLD);
        return;
    }
    put(row, 0, sel ? ">" : " ", 1, a);
    int rw = cells(right);
    if (rw > 30) {
        rw = 30;
    }
    int lw = TS_COLS - 3 - (rw ? rw + 1 : 0);
    put(row, 2, left, lw, a);
    if (rw) {
        put(row, TS_COLS - 1 - rw, right, rw, a);
    }
}

void TelnetScreen::box(int top, int height, const char *title)
{
    for (int r = top; r < top + height && r < TS_ROWS; r++) {
        fill(r, A_NORM);
        g_[r][1].c = '|';
        g_[r][TS_COLS - 2].c = '|';
    }
    fill(top, A_NORM);
    for (int c = 1; c < TS_COLS - 1; c++) {
        g_[top][c].c = '-';
    }
    int bot = top + height - 1;
    for (int c = 1; c < TS_COLS - 1; c++) {
        g_[bot][c].c = '-';
    }
    if (title && title[0]) {
        put(top, 3, " ", 1, A_BOLD);
        put(top, 4, title, TS_COLS - 10, A_BOLD);
        int n = cells(title);
        put(top, 4 + (n < TS_COLS - 10 ? n : TS_COLS - 10), " ", 1, A_BOLD);
    }
}

// Text wrapped at the words into the box (columns 3..56), from `row` to `maxrow`; returns the next free row.
int TelnetScreen::wrap(int row, int maxrow, const char *text, uint8_t attr)
{
    const int W = TS_COLS - 6;
    while (*text && row <= maxrow) {
        while (*text == ' ') {
            text++;
        }
        int len = 0, brk = -1;
        while (text[len] && text[len] != '\n' && len < W) {
            if (text[len] == ' ') {
                brk = len;
            }
            len++;
        }
        if (text[len] && text[len] != '\n' && brk > 0) {
            len = brk;
        }
        char buf[TS_COLS + 1];
        int n = len < TS_COLS ? len : TS_COLS - 1;
        memcpy(buf, text, (size_t)n);
        buf[n] = 0;
        put(row, 3, buf, W, attr);
        row++;
        text += len;
        if (*text == '\n') {
            text++;
        }
    }
    return row;
}

// ---- what is in the context of the cursor (the same decision as the UI service's snapshot, uiapp.cc ctx_of) -----------------------------------------

enum Ctx { C_ITEMS, C_LIBRARY, C_SHEET, C_PICK, C_NONE };

static Ctx ctx_of(MenuCtl *m)
{
    char t[2];
    const char *d = m->dialog(t, sizeof(t));
    if (!strcmp(d, "pick")) return C_PICK;
    if (!strcmp(d, "busy") || !strcmp(d, "confirm") || !strcmp(d, "ask") || !strcmp(d, "text") || !strcmp(d, "address") || !strcmp(d, "learn") || !strcmp(d, "viewer")) return C_NONE;
    if (m->picking()) return C_PICK;
    if (m->chooser()) return C_SHEET;
    if (m->browsing()) return C_LIBRARY;
    return C_ITEMS;
}

static MenuCtl::Item s_it;

void TelnetScreen::bars(MenuCtl *m)
{
    char buf[160];
    fill(0, A_REV);
    snprintf(buf, sizeof(buf), " %s", m->core_title());
    put(0, 0, buf, TS_COLS - 16, A_REV);
    snprintf(buf, sizeof(buf), "%s%s ", m->unsaved() ? "UNSAVED  " : "", m->session_word(true));
    int n = cells(buf);
    put(0, TS_COLS - n, buf, n, A_REV);

    // the areas: every key, the open one with its name
    fill(1, A_NORM);
    const MenuTree &t = m->tree();
    int col = 1;
    for (int a = 0; a < t.nareas; a++) {
        if (a == m->area()) {
            snprintf(buf, sizeof(buf), " %s %s ", t.a[a].key, t.a[a].name);
        } else {
            snprintf(buf, sizeof(buf), "%s", t.a[a].key);
        }
        int w = cells(buf);
        if (col + w > TS_COLS) {
            break;
        }
        put(1, col, buf, w, a == m->area() ? A_REV : A_DIM);
        col += w + 1;
    }

    // the tabs of the area: the numbers, the open one with its name
    fill(2, A_NORM);
    const MenuArea &ar = t.a[m->area()];
    col = 1;
    for (int b = 0; b < ar.ntabs; b++) {
        if (m->tab_is_hidden(m->area(), b) || ar.tabs[b].page) {
            continue;
        }
        if (b == m->tab()) {
            snprintf(buf, sizeof(buf), " %d %s ", ar.tabs[b].n, ar.tabs[b].name);
        } else {
            snprintf(buf, sizeof(buf), "%d", ar.tabs[b].n);
        }
        int w = cells(buf);
        if (col + w > TS_COLS) {
            break;
        }
        put(2, col, buf, w, b == m->tab() ? A_REV : A_DIM);
        col += w + 1;
    }
    if (m->tab_is_hidden(m->area(), m->tab()) || ar.tabs[m->tab()].page) {     // a page a row opened: its name stands where its number would
        snprintf(buf, sizeof(buf), " %s ", ar.tabs[m->tab()].name);
        put(2, col, buf, TS_COLS - col, A_REV);
    }
}

void TelnetScreen::list(MenuCtl *m)
{
    Ctx c = ctx_of(m);
    char buf[200], l[200], r[120], sb[120];
    int n = c == C_NONE ? 0 : m->rows();
    int sel = m->selected();

    // the crumb and the position
    fill(3, A_NORM);
    if (c == C_LIBRARY || c == C_PICK) {
        snprintf(buf, sizeof(buf), "%s", c == C_LIBRARY ? m->browse_path() : m->title());
    } else if (c == C_SHEET) {
        snprintf(buf, sizeof(buf), "%s", m->chooser_file());
    } else {
        if (!m->layout_param("crumb", buf, sizeof(buf))) {
            if (!strcasecmp(m->area_name(), m->tab_name())) {
                snprintf(buf, sizeof(buf), "%s", m->area_name());
            } else {
                snprintf(buf, sizeof(buf), "%s / %s", m->area_name(), m->tab_name());
            }
        }
    }
    int key = (m->area() << 12) | (m->tab() << 6) | (int)c;
    if (key != key_) {
        key_ = key;
        first_ = 0;
    }
    put(3, 1, buf, TS_COLS - 12, A_DIM);
    if (n > 0 && sel >= 0) {
        snprintf(buf, sizeof(buf), "%d/%d ", sel + 1, n);
        int w = cells(buf);
        put(3, TS_COLS - w, buf, w, A_DIM);
    }
    if (n <= 0) {
        return;
    }

    // the window: it follows the cursor; rows that take no place (hidden) are skipped
    int idx[LIST_ROWS], cnt = 0;
    for (int pass = 0; pass < 3; pass++) {
        if (first_ < 0) first_ = 0;
        if (first_ > n - 1) first_ = n - 1;
        cnt = 0;
        for (int i = first_; i < n && cnt < LIST_ROWS; i++) {
            if (c == C_ITEMS) {
                m->item(i, &s_it);
                if (s_it.hidden) {
                    continue;
                }
            }
            idx[cnt++] = i;
        }
        if (sel < 0 || (cnt && sel >= idx[0] && sel <= idx[cnt - 1])) {
            break;
        }
        first_ = sel < first_ ? sel - 2 : sel - LIST_ROWS + 4;
    }
    for (int k = 0; k < cnt; k++) {
        int i = idx[k], row = LIST_TOP + k;
        bool is_sel = i == sel;
        l[0] = r[0] = sb[0] = 0;
        if (c == C_ITEMS) {
            m->item(i, &s_it);
            if (s_it.kind == MenuCtl::IK_HEADING) {
                line(row, false, s_it.title, "", A_BOLD, true);
                continue;
            }
            char mk[8] = "";
            if (s_it.bkey) snprintf(mk, sizeof(mk), "[%c] ", s_it.bkey);
            snprintf(l, sizeof(l), "%s%s%s%s", mk, s_it.title, s_it.dirty ? " *" : "", s_it.reset ? " o" : "");
            if (s_it.value[0]) snprintf(r, sizeof(r), "%s", s_it.value);
            else if (s_it.chip[0]) snprintf(r, sizeof(r), "%s", s_it.chip);
            else if (s_it.toggle) snprintf(r, sizeof(r), "%s", s_it.on ? "[x]" : "[ ]");
            line(row, is_sel, l, r, (s_it.dim || !s_it.enabled) ? A_DIM : A_NORM, false);
        } else if (c == C_LIBRARY) {
            MenuCtl::BrowseRow br;
            m->browse_row(i, &br);
            snprintf(l, sizeof(l), "%s%s%s%s%s", br.marked ? "* " : "", br.badge, br.badge[0] ? " " : "", br.name, br.dir ? "/" : "");
            snprintf(r, sizeof(r), "%s", br.value);
            line(row, is_sel, l, r, br.info ? A_DIM : A_NORM, false);
        } else if (c == C_PICK) {
            bool act;
            m->row(i, l, sizeof(l), r, sizeof(r), &act, sb, sizeof(sb));
            line(row, is_sel, l, r, act ? A_NORM : A_DIM, false);
        } else {
            char k = m->chooser_key(i);
            snprintf(l, sizeof(l), "%s%c%s%s", k ? "[" : "", k ? k : 0, k ? "] " : "", m->chooser_label(i));
            line(row, is_sel, l, "", A_NORM, false);
        }
    }
    if (first_ > 0) {
        put(LIST_TOP, TS_COLS - 1, "^", 1, A_DIM);
    }
    if (cnt && idx[cnt - 1] < n - 1) {
        put(LIST_TOP + LIST_ROWS - 1, TS_COLS - 1, "v", 1, A_DIM);
    }
}

// A prompt, the viewer, a question, a job: what the screen shows in place of the list.
void TelnetScreen::modal(MenuCtl *m, const char *kind, const char *text)
{
    char l[160], r[100], sb[200];
    bool a;
    if (!strcmp(kind, "viewer")) {
        char st[100];
        m->view_status(st, sizeof(st));
        fill(3, A_NORM);
        put(3, 1, m->view_title(), TS_COLS - 2, A_DIM);
        for (int vr = 0; vr < LIST_ROWS && vr < MenuCtl::VIEW_ROWS; vr++) {
            char vl[MenuCtl::VIEW_COLS + 8];
            m->view_line(vr, vl, sizeof(vl));
            put(LIST_TOP + vr, 1, vl, TS_COLS - 2, A_NORM);
        }
        put(MSG_ROW, 1, st, TS_COLS - 2, A_DIM);
        return;
    }
    box(7, 10, !strcmp(kind, "busy") ? "Working" : !strcmp(kind, "confirm") ? "Question" : "");
    int row = 8;
    if (!strcmp(kind, "confirm") || !strcmp(kind, "busy")) {
        row = wrap(row, 13, text, A_NORM);
        if (!strcmp(kind, "busy") && m->progress() >= 0) {
            int w = TS_COLS - 12, f = w * m->progress() / 100;
            char bar[TS_COLS + 1];
            for (int i = 0; i < w; i++) bar[i] = i < f ? '#' : '.';
            bar[w] = 0;
            put(14, 4, bar, w, A_CYAN);
            snprintf(sb, sizeof(sb), "%d%%", m->progress());
            put(14, 4 + w + 1, sb, 5, A_NORM);
        }
        if (!strcmp(kind, "confirm")) {
            put(15, 3, "RETURN = yes     LEFT / ESC = no", TS_COLS - 6, A_DIM);
        }
        return;
    }
    l[0] = r[0] = sb[0] = 0;
    m->row(0, l, sizeof(l), r, sizeof(r), &a, sb, sizeof(sb));
    row = wrap(row, 10, l[0] ? l : text, A_BOLD);
    if (r[0]) {
        row = wrap(row + 1, 13, r, A_NORM);
    }
    if (sb[0]) {
        wrap(row + 1, 14, sb, A_DIM);
    }
    put(15, 3, "RETURN = accept     ESC = cancel", TS_COLS - 6, A_DIM);
}

// A question of an application (appdlg.h): the card over the list.
void TelnetScreen::over_dialog(const DlgView *v)
{
    static const char *const kinds[] = { "", "Message", "Question", "Choose", "Text", "Choose a file", "Choose a folder" };
    int h = 12 + (v->kind == DK_CHOOSE || v->kind == DK_PICK_FILE || v->kind == DK_PICK_FOLDER ? 4 : 0);
    int top = 4;
    box(top, h, v->title[0] ? v->title : kinds[v->kind >= 0 && v->kind <= DK_PICK_FOLDER ? v->kind : 0]);
    int row = wrap(top + 1, top + 5, v->text, A_NORM);
    if (v->kind == DK_TEXT) {
        char b[DLG_TEXT_MAX + 8];
        snprintf(b, sizeof(b), "> %s_", v->input);
        put(row + 1, 3, b, TS_COLS - 6, A_BOLD);
    } else if (v->kind == DK_CHOOSE || v->kind == DK_PICK_FILE || v->kind == DK_PICK_FOLDER) {
        if (v->kind != DK_CHOOSE) {
            put(row, 3, v->input, TS_COLS - 6, A_DIM);
            row++;
        }
        for (int i = 0; i < v->rows && row + i < top + h - 2; i++) {
            char b[160];
            snprintf(b, sizeof(b), "%s%s", v->row_dir[i] ? "/" : "", v->row[i]);
            bool s = v->first + i == v->sel;
            fill(row + i, A_NORM);
            g_[row + i][1].c = '|';
            g_[row + i][TS_COLS - 2].c = '|';
            put(row + i, 3, s ? "> " : "  ", 2, s ? A_REV : A_NORM);
            put(row + i, 5, b, TS_COLS - 9, s ? A_REV : A_NORM);
        }
    } else if (v->kind == DK_BUTTONS) {
        char b[64] = "";
        if (v->buttons & SVC_BUTTON_OK) strcat(b, "RETURN = OK   ");
        if (v->buttons & SVC_BUTTON_YES) strcat(b, "RETURN = yes   ");
        if (v->buttons & SVC_BUTTON_NO) strcat(b, "LEFT = no");
        put(top + h - 2, 3, b, TS_COLS - 6, A_DIM);
    }
    if (v->remaining_s > 0) {
        char b[24];
        snprintf(b, sizeof(b), "%d s", v->remaining_s);
        put(top + h - 1, TS_COLS - 8, b, 6, A_DIM);
    }
}

void TelnetScreen::compose(MenuCtl *m, bool open, const TsOver &over)
{
    clear();
    if (!open) {
        fill(0, A_REV);
        char b[100];
        snprintf(b, sizeof(b), " %s", m->core_title());
        put(0, 0, b, TS_COLS - 2, A_REV);
        put(8, 3, "The menu is closed on the machine.", TS_COLS - 6, A_NORM);
        put(10, 3, "Press any key to open it again.", TS_COLS - 6, A_DIM);
        return;
    }
    bars(m);
    char dt[260];
    const char *dk = m->dialog(dt, sizeof(dt));
    bool modal_up = strcmp(dk, "none") && strcmp(dk, "sheet") && strcmp(dk, "pick");
    if (modal_up) {
        modal(m, dk, dt);
    } else if (m->mapping()) {
        modal(m, "wizard", dt);
    } else {
        list(m);
    }

    // the line under the list: a toast, else the status (and the ring's legend), else what the row says of itself
    fill(MSG_ROW, A_NORM);
    const char *msg = "";
    uint8_t ma = A_NORM;
    if (m->toast_text()[0]) {
        msg = m->toast_text();
        ma = A_CYAN;
    } else if (m->foot_line()[0]) {
        msg = m->foot_line();
        ma = m->status_bad() ? A_RED : A_NORM;
    } else if (m->sel_detail()[0]) {
        msg = m->sel_detail();
        ma = A_DIM;
    } else if (m->hint()) {
        msg = m->hint();
        ma = A_AMBER;
    }
    put(MSG_ROW, 1, msg, TS_COLS - 2, ma);
    char legend[240];
    m->footer_legend(legend, sizeof(legend));
    fill(FOOT_ROW, A_REV);
    put(FOOT_ROW, 1, legend, TS_COLS - 2, A_REV);

    if (over.dlg && over.dlg->kind != DK_NONE) {
        over_dialog(over.dlg);
    }
    if (over.dlg && over.dlg->bar) {
        char b[160];
        snprintf(b, sizeof(b), "%s %s %d/%d", over.dlg->bar_title, over.dlg->bar_text, over.dlg->bar_value, over.dlg->bar_total);
        fill(MSG_ROW, A_NORM);
        put(MSG_ROW, 1, b, TS_COLS - 2, A_CYAN);
    }
    if (over.picture) {
        box(7, 8, "Picture");
        wrap(9, 12, over.picture, A_NORM);
        put(13, 3, "Any key keeps it     LEFT / ESC goes back", TS_COLS - 6, A_DIM);
    }
}

// One row to the terminal: the cursor to it, the cells with the attribute changes between them.
int TelnetScreen::emit(int row, char *out, int cap)
{
    static const char *const sgr[A_N] = { "\e[0m", "\e[0;7m", "\e[0;2m", "\e[0;1m", "\e[0;36m", "\e[0;33m", "\e[0;31;1m" };
    int n = snprintf(out, (size_t)cap, "\e[%d;1H", row + 1);
    int cur = -1;
    for (int c = 0; c < TS_COLS; c++) {
        uint8_t a = g_[row][c].a < A_N ? g_[row][c].a : 0;
        if (a != cur) {
            int l = (int)strlen(sgr[a]);
            if (n + l >= cap) {
                return -1;
            }
            memcpy(out + n, sgr[a], (size_t)l);
            n += l;
            cur = a;
        }
        if (n + 1 >= cap) {
            return -1;
        }
        out[n++] = g_[row][c].c;
    }
    if (n + 5 >= cap) {
        return -1;
    }
    memcpy(out + n, "\e[0m", 4);
    return n + 4;
}

int TelnetScreen::frame(MenuCtl *m, bool open, const TsOver &over, char *out, int cap)
{
    compose(m, open, over);
    int n = 0;
    for (int r = 0; r < TS_ROWS; r++) {
        if (!dirty_all_ && !memcmp(g_[r], s_[r], sizeof(g_[r]))) {
            continue;
        }
        int w = emit(r, out + n, cap - n);
        if (w < 0) {
            return n;                                         // the rest comes with the next call
        }
        n += w;
        memcpy(s_[r], g_[r], sizeof(g_[r]));
    }
    bool all = true;
    for (int r = 0; r < TS_ROWS; r++) {
        if (memcmp(g_[r], s_[r], sizeof(g_[r]))) {
            all = false;
        }
    }
    if (all) {
        dirty_all_ = false;
    }
    return n;
}
