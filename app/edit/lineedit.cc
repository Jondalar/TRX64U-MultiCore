#include "lineedit.h"
#include "keyboard.h"
#include <string.h>

void le_init(LineEdit *e, const char *text, int max, const char *reject)
{
    e->max = max > LE_MAX ? LE_MAX : max;
    e->reject = reject;
    int n = (int)strlen(text);
    if (n > e->max) {
        n = e->max;
    }
    memcpy(e->buf, text, n);
    e->buf[n] = 0;
    e->len = e->cur = n;
    e->first = 0;
}

static void insert(LineEdit *e, char ch)
{
    if (e->len >= e->max) {
        return;
    }
    memmove(e->buf + e->cur + 1, e->buf + e->cur, e->len - e->cur + 1);
    e->buf[e->cur++] = ch;
    e->len++;
}

int le_key(LineEdit *e, int key)
{
    if (key >= 32 && key < 127) {
        if (!e->reject || !strchr(e->reject, key)) {
            insert(e, (char)key);
        }
        return LE_BUSY;
    }
    switch (key) {
    case KEY_RETURN:
        return LE_DONE;
    case KEY_BREAK:
    case KEY_ESCAPE:
        return LE_CANCEL;
    case KEY_LEFT:
        if (e->cur > 0) {
            e->cur--;
        }
        break;
    case KEY_RIGHT:
        if (e->cur < e->len) {
            e->cur++;
        }
        break;
    case KEY_HOME:
    case KEY_UP:
        e->cur = 0;
        break;
    case KEY_END:
    case KEY_DOWN:
        e->cur = e->len;
        break;
    case KEY_BACK:
        if (e->cur > 0) {
            memmove(e->buf + e->cur - 1, e->buf + e->cur, e->len - e->cur + 1);
            e->cur--;
            e->len--;
        }
        break;
    case KEY_DELETE:
        if (e->cur < e->len) {
            memmove(e->buf + e->cur, e->buf + e->cur + 1, e->len - e->cur);
            e->len--;
        }
        break;
    case KEY_INSERT:
        if (e->len < e->max) {
            insert(e, ' ');
            e->cur--;
        }
        break;
    case KEY_CLEAR:
        e->buf[0] = 0;
        e->len = e->cur = 0;
        break;
    }
    return LE_BUSY;
}

int le_scroll(LineEdit *e, int width)
{
    if (width < 1) {
        width = 1;
    }
    int last = e->len > width ? e->len - width : 0;           // the window never ends in blank space
    if (e->first > last) {
        e->first = last;
    }
    if (e->first < 0) {
        e->first = 0;
    }
    if (e->cur < e->first) {
        e->first = e->cur;
    } else if (e->cur > e->first + width) {
        e->first = e->cur - width;
    }
    return e->first;
}

void le_row(LineEdit *e, int vis, char mask, char *out, int outn)
{
    if (outn < 1) {
        return;
    }
    int n = 0, from = 0, to = e->len;
    bool lmark = false, rmark = false;
    if (e->len + 1 > vis) {                                   // does not fit: the window, the markers cost 2 cells each
        int width = vis - 5;
        from = le_scroll(e, width);
        to = from + width < e->len ? from + width : e->len;
        lmark = from > 0;
        rmark = to < e->len;
    } else {
        e->first = 0;
    }
    if (lmark) {
        for (int i = 0; i < 2 && n < outn - 1; i++) {
            out[n++] = '.';
        }
    }
    for (int i = from; i <= to; i++) {
        if (i == e->cur && n < outn - 1) {
            out[n++] = '_';
        }
        if (i < to && n < outn - 1) {
            out[n++] = mask ? mask : e->buf[i];
        }
    }
    if (rmark) {
        for (int i = 0; i < 2 && n < outn - 1; i++) {
            out[n++] = '.';
        }
    }
    out[n] = 0;
}

bool le_blank(const LineEdit *e)
{
    for (int i = 0; i < e->len; i++) {
        if (e->buf[i] != ' ') {
            return false;
        }
    }
    return true;
}
