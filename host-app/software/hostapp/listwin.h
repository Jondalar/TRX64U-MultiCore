// A window of `rows` rows over `n` items that follows a selection (T101 D4). Pure.
#ifndef HOST_LISTWIN_H
#define HOST_LISTWIN_H

// Moves *first just enough that `sel` is inside [*first, *first + rows) and the window does not run past
// n. Returns true when *first changed. A window whose selection is inside it is never moved.
static inline bool listwin_fix(int sel, int n, int rows, int *first)
{
    int f = *first;
    if (f > n - rows) {
        f = n - rows;
    }
    if (f < 0) {
        f = 0;
    }
    if (sel >= 0 && sel < f) {
        f = sel;
    }
    if (sel >= f + rows) {
        f = sel - rows + 1;
    }
    bool ch = f != *first;
    *first = f;
    return ch;
}

#endif
