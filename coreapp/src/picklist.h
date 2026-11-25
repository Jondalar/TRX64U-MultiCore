// The rows of a pick list and of a list view the menu shows (T228 R21, T262 P1/P2): rows made at run time. A state holds them as text, one row a line, fields separated by TAB:
//     value TAB label TAB right TAB tone TAB side TAB side TAB side
// Every field after `value` may be left out (the label is then the value). `right` is a second column (a signal strength, a size). `tone` is one of muted, cyan, violet,
// amber, coral (a list view; empty: none). `side` is a line the row says about itself (a list view shows them in the foot while the row is selected), at most three.
// A line that starts with '!' is a note, not a choice (a scan that is still running): it is shown and never taken. A line that starts with '+' is the last row of a list view
// that asks for more: its text is the row's label (empty: "More"). Pure: the same file runs in tests/coreapp/.
//
// The rows are not kept as an array of a fixed size: the text is copied once (the heap, at most a quarter of the free heap, listgrow.h) with the offset of every line, and a row is read
// from its line when it is asked for. A list of 47 choices and a list of 5000 results cost their text; what the budget leaves out is counted (`cut`), never dropped in silence.
#ifndef COREAPP_PICKLIST_H
#define COREAPP_PICKLIST_H

#define PICKLIST_SIDE_MAX 3

struct PickRow {
    char value[66];                   // what the answer is ("" is a choice: the empty text)
    char label[62];
    char right[40];
    bool note;                        // not a choice
    bool more;                        // a list view's last row: RETURN asks the library for more rows
    int tone;                         // -1 none, 0 muted, 1 cyan, 2 violet, 3 amber, 4 coral
    int nside;
    char side[PICKLIST_SIDE_MAX][64];
};

struct PickRows {
    char *text;                       // the copy of the lines taken (heap), null: none
    int *off;                         // the offset of every non-empty line
    int n;
    int len;                          // bytes of the source text that were taken
    int cap;
    bool cut;                         // the text is longer than the budget: its tail is not here
};

void picklist_init(PickRows *p);
void picklist_free(PickRows *p);
// Reads the text into rows (null or empty text: none), replacing what `p` held. Returns the number of rows (lines that are not empty); `p->cut` says when the memory budget
// left the tail of the text out.
int picklist_parse(PickRows *p, const char *text);
// Does `text` hold the rows that `p` holds now?
bool picklist_same(const PickRows &p, const char *text);
// Row `i` (0 .. n - 1), read from its line.
void picklist_get(const PickRows &p, int i, PickRow *out);
// The first choice whose value is `value`, -1 when none.
int picklist_find(const PickRows &p, const char *value);
// The number of rows the text has (what picklist_parse would return without the budget), without copying it.
int picklist_count(const char *text);
// The side lines of a row joined with "  -  " (the foot of a list view).
void picklist_side_text(const PickRow &r, char *out, int n);

#endif
