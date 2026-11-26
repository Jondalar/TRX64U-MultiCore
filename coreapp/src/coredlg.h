// The dialogs of the application host that a core's code may ask through (base 3.7, T239.3 D54). Code that waits for the person (a test runner, a panel) runs on a task
// of its own (spawn) and asks through this; the application gives every core one (CoreCtx::dialogs), a native test a model of its own.
#ifndef COREAPP_COREDLG_H
#define COREAPP_COREDLG_H

// One row of an edit list (CoreDialogs::edit_list): the label on the left, the value text right-aligned; an action row has no value and RETURN on it answers the list.
struct DlgRow {
    char label[32];
    char value[40];
    bool action;
};
enum { DLG_EDIT_LEFT = 1, DLG_EDIT_RIGHT = 2, DLG_EDIT_ENTER = 3 };

class CoreDialogs {
public:
    virtual ~CoreDialogs() { }
    // Runs fn(arg) on a task of its own; false when there is none.
    virtual bool spawn(void (*fn)(void *), void *arg) = 0;
    // A question with two answers and a way out: 1 yes, 0 no, -1 the person stopped (ESC). timeout_s > 0: the question closes by itself after that many seconds
    // and answers -2 (the dialog shows the seconds left); 0: waits for ever.
    virtual int ask(const char *title, const char *text, int timeout_s) = 0;
    // As ask() with no timer, for a text the caller rewrites: `text` is the caller's buffer, live(ud) refills it about once a second (on the menu task) and the dialog
    // changes the text in place. The default asks it as ask() does, once.
    virtual int ask_live(const char *title, const char *text, void (*live)(void *), void *ud) { (void)live; (void)ud; return ask(title, text, 0); }
    // A question with a choice of answers (items, at most 8): the index of the one picked, -1 the person stopped (ESC), -2 the time ran out (timeout_s as ask()).
    virtual int choose(const char *title, const char *text, const char *const *items, int n, int timeout_s) = 0;
    // A message that waits for OK.
    virtual void notice(const char *title, const char *text) = 0;
    // A file picker over `start` (a folder) for .toml files; false when the person cancelled.
    virtual bool pick(const char *start, char *out, int outlen) = 0;
    // A line of text typed by the person into `buf` (max bytes with the NUL); false when cancelled (ESC), buf untouched then.
    virtual bool text(const char *title, char *buf, int max) { (void)title; (void)buf; (void)max; return false; }
    // A list of rows the person edits (T260 P5): rows[0..n-1] is the caller's array, shown as label and value; UP/DOWN move the cursor (it starts on `sel`); LEFT/RIGHT on a row that is no
    // action, and RETURN on one, call cb(ud, row, DLG_EDIT_LEFT / _RIGHT / _ENTER) on the menu task (quick work only: no dialog of its own may be opened from cb), and the caller rewrites
    // rows[] there - the list is drawn again after every call. RETURN on an action row closes the list and answers its index (the caller does the action and may open the list again with
    // that `sel`); ESC closes it and answers -1. At most 64 rows. The default shows nothing and answers -1.
    virtual int edit_list(const char *title, const char *text, DlgRow *rows, int n, int sel, void (*cb)(void *, int, int), void *ud)
    {
        (void)title; (void)text; (void)rows; (void)n; (void)sel; (void)cb; (void)ud;
        return -1;
    }
    // "YYYYMMDD-HHMMSS" from the clock, false when it has none.
    virtual bool stamp(char out[16]) = 0;
    // false: the joystick ports feed no menu key (only the keyboards answer a prompt); true: as always. The default does nothing.
    virtual void joystick_menu(bool on) { (void)on; }
};

#endif
