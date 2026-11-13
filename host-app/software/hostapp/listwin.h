// A window of `rows` rows over `n` items that follows a selection (T101 D4, T209 D3). Pure.
#ifndef HOST_LISTWIN_H
#define HOST_LISTWIN_H

#define LISTWIN_MARGIN 2          // T209 D3: the window moves while this many rows are still hidden ahead of the selection (not at the list's ends)

// Moves *first just enough that `sel` is inside [*first, *first + rows) with LISTWIN_MARGIN rows beyond it on each side (a window shorter than
// 2 * margin + 1 rows takes a smaller margin) and the window does not run past n. The margin is not kept at the ends of the list: the first window
// starts at 0, the last ends at n. Returns true when *first changed.
static inline bool listwin_fix(int sel, int n, int rows, int *first)
{
    int f = *first;
    int m = (rows - 1) / 2 < LISTWIN_MARGIN ? (rows - 1) / 2 : LISTWIN_MARGIN;
    if (m < 0) {
        m = 0;
    }
    if (f > n - rows) {
        f = n - rows;
    }
    if (f < 0) {
        f = 0;
    }
    if (sel >= 0 && sel < f + m) {
        f = sel - m;
    }
    if (sel >= 0 && sel >= f + rows - m) {
        f = sel - rows + 1 + m;
    }
    if (f > n - rows) {
        f = n - rows;
    }
    if (f < 0) {
        f = 0;
    }
    bool ch = f != *first;
    *first = f;
    return ch;
}

#endif
