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

// One page (T209 D7, T223): the window moves by exactly `rows` rows in `dir` (+1 / -1), clamped to the list, and the selection keeps its row in the window
// (inside the margin while the window is not at an end), so no row is skipped between two pages. Already at the end the selection goes to the last / first
// item. Returns true when *sel or *first changed. Pure.
static inline bool listwin_page(int *sel, int *first, int n, int rows, int dir)
{
    if (n <= 0 || rows <= 0) {
        return false;
    }
    int m = (rows - 1) / 2 < LISTWIN_MARGIN ? (rows - 1) / 2 : LISTWIN_MARGIN;
    int maxf = n > rows ? n - rows : 0;
    int f = *first > maxf ? maxf : (*first < 0 ? 0 : *first);
    int f2 = f + dir * rows;
    f2 = f2 > maxf ? maxf : (f2 < 0 ? 0 : f2);
    int off = *sel - f;
    off = off < 0 ? 0 : (off > rows - 1 ? rows - 1 : off);
    if (f2 > 0) {
        off = off < m ? m : off;
    }
    if (f2 < maxf) {
        off = off > rows - 1 - m ? rows - 1 - m : off;
    }
    int s2 = f2 + off;
    if (f2 == f) {
        s2 = dir > 0 ? n - 1 : 0;
    }
    s2 = s2 >= n ? n - 1 : (s2 < 0 ? 0 : s2);
    bool ch = s2 != *sel || f2 != *first;
    *sel = s2;
    *first = f2;
    return ch;
}

#endif
