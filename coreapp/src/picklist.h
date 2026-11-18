// The rows of a pick list the menu shows (T228 R21): a list of choices made at run time. A state holds them as text, one row a line:
//     value TAB label TAB right
// `label` and `right` may be left out (the label is then the value), `right` is a second column (a signal strength, a size). A line that starts with '!' is a note, not a
// choice (a scan that is still running): it is shown and never taken. Pure: the same file runs in tests/coreapp/.
#ifndef COREAPP_PICKLIST_H
#define COREAPP_PICKLIST_H

#define PICKLIST_MAX 40

struct PickRow {
    char value[66];                   // what the answer is ("" is a choice: the empty text)
    char label[62];
    char right[20];
    bool note;                        // not a choice
};

struct PickRows {
    int n;
    PickRow row[PICKLIST_MAX];
};

// Reads the text into rows (null or empty text: none). Returns the number of rows; a row past PICKLIST_MAX is dropped.
int picklist_parse(PickRows *out, const char *text);
// The first choice whose value is `value`, -1 when none.
int picklist_find(const PickRows &p, const char *value);

#endif
