// The dialogs an application may ask for while it runs (T104.3 D4) and the bar it may show while it works. Pure: the tasks are an OsPort, the file system a
// CoreFs, the screen is somebody else's (ui.cc draws what view() holds), so tests/coreapp/ runs the whole of it against threads.
//
// Two sides. The application's task calls message(), buttons(), confirm(), choose(), text(), pick_file() or pick_folder(): the request is handed to the menu
// task and the caller sleeps until it was answered - one request at a time, a second caller waits for the first. progress_open/set/close do not wait: they
// only set what the bar shows. The menu task calls service() on every pass of its loop (it makes a waiting request the open one), take_key() for every
// key while a dialog is open, and reads view() to draw. RUN/STOP and the C64's left arrow cancel any question; ESC goes one folder up in a picker and
// cancels elsewhere. A request that is cancelled answers false / -1 / 0, exactly as an application without a screen would be answered.
#ifndef COREAPP_APPDLG_H
#define COREAPP_APPDLG_H

#include "exec.h"
#include <stdlib.h>
#include "fsif.h"
#include "svctab.h"
#include "coredlg.h"

#define DLG_ITEMS_MAX 14                  // rows of a list on screen
#define DLG_PICK_MAX 160                  // entries of a folder the picker lists
#define DLG_TEXT_MAX 120
#define DLG_EDIT_MAX 64

#define DLG_TIMEOUT 8                    // the answer of buttons_timed() whose time ran out (a button value of its own, beside SVC_BUTTON_*)

enum DlgKind { DK_NONE = 0, DK_MESSAGE, DK_BUTTONS, DK_CHOOSE, DK_TEXT, DK_PICK_FILE, DK_PICK_FOLDER, DK_EDIT };

// What the screen draws of the question that is open. The strings are the dialog's own copies, valid until the next service() or take_key().
struct DlgView {
    int kind;                             // DlgKind; DK_NONE: no question open
    char title[64];
    char text[520];
    char input[DLG_TEXT_MAX + 1];         // text(): what was typed; the pickers: the folder
    int caret;                            // text(): the cursor, in characters
    int nitems;                           // choose(), pickers: how many rows there are
    int first, sel;                       // the window of rows and the selected one
    int rows;                             // how many rows the window shows (DLG_ITEMS_MAX at most)
    const char *row[DLG_ITEMS_MAX];       // the text of each row of the window (an edit list: the row's label)
    const char *row_val[DLG_ITEMS_MAX];   // an edit list: the value of each row, drawn right-aligned; 0 elsewhere
    bool row_dir[DLG_ITEMS_MAX];
    int buttons;                          // SVC_BUTTON_* of buttons(): which ones are drawn
    int remaining_s;                      // a timed question: the whole seconds left (rounded up); 0: no timer
    bool live;                            // the text is rewritten in place by the application (buttons_live): the screen updates its label, not the card
    bool bar;                             // a bar is up
    char bar_title[64], bar_text[64];
    int bar_value, bar_total;
};

// What a key of an open dialog is: the table of AppDlg::resolve makes it, AppDlg::apply does it (T192.3 D23: the dialogs are a client of the same intents as the menu).
enum DlgIntentId { DI_NONE = 0, DI_UP, DI_DOWN, DI_LEFT, DI_RIGHT, DI_PAGE_UP, DI_PAGE_DOWN, DI_HOME, DI_END, DI_RETURN, DI_SPACE, DI_ESC, DI_ARROW, DI_STOP, DI_DEL, DI_CHAR };
struct DlgIntent {
    uint8_t id;
    int arg;                              // DI_CHAR, DI_SPACE: the key's character
};

class AppDlg {
public:
    AppDlg(OsPort *os, CoreFs *fs);
    ~AppDlg() { free(pk_list_); }

    // ---- the application's task (blocking, except the bar)
    void message(const char *title, const char *text);
    int buttons(const char *title, const char *text, int buttons);
    // As buttons(), and when nobody answered within timeout_s seconds of the question being on the screen it closes and answers DLG_TIMEOUT. 0: no timer.
    int buttons_timed(const char *title, const char *text, int buttons, int timeout_s);
    // As buttons(), for a text the application rewrites: `text` is a buffer of the application, live(ud) refills it (on the menu task, about once a second while the
    // question is open) and the screen changes the text in place. No timer.
    int buttons_live(const char *title, const char *text, int buttons, void (*live)(void *), void *ud);
    bool confirm(const char *title, const char *text);
    int choose(const char *title, const char *const *items, int n, int sel, const char *keys);
    // As choose() with a text under the title, and a timer as buttons_timed(): the index picked, -1 cancelled, DLG_TIMEOUT when the time ran out (so at most DLG_TIMEOUT items).
    int choose_timed(const char *title, const char *text, const char *const *items, int n, int timeout_s);
    bool text(const char *title, char *buf, int max);
    // A list of rows (label and value text) the person edits (T260 P5, CoreDialogs::edit_list): UP/DOWN/PAGE/HOME/END move; LEFT and RIGHT on a row call cb(ud, row, DLG_EDIT_LEFT / _RIGHT) on
    // the menu task, RETURN on a row that is no action calls it with DLG_EDIT_ENTER, and the dialog draws the rows again (cb rewrites rows[] in place). RETURN on an action row closes the
    // list and answers its index; ESC, RUN/STOP and the left arrow close it and answer -1. At most DLG_EDIT_MAX rows; `sel` is the row the cursor starts on.
    int edit_list(const char *title, const char *text, DlgRow *rows, int n, int sel, void (*cb)(void *, int, int), void *ud);
    bool pick_file(const char *start, const char *ext, char *out, int outlen);
    bool pick_folder(const char *start, char *out, int outlen);
    void *progress_open(const char *title, int total);
    void progress_set(void *h, int value, const char *text);
    void progress_close(void *h);
    // The screen is not there (no pixel layer): every question answers "cancelled" at once.
    void set_headless(bool on) { headless_ = on; }
    // The application ended: a question it left open is answered "cancelled" and its bar goes.
    void abandon();

    // ---- the menu task
    bool service();                       // a request became the open one, or the bar moved: true when the screen has to be drawn again
    bool open() const { return cur_.kind != DK_NONE; }
    bool active() const { return cur_.kind != DK_NONE || bar_.on || req_pending(); }
    // True when a key is the dialog's: the question is open, a key is always its own.
    bool take_key(int k);                 // resolve + apply
    static DlgIntent resolve(int k);
    bool apply(const DlgIntent &in);
    const DlgView &view();
    uint32_t revision() const { return rev_; }

private:
    struct Req {
        int kind;
        const char *title;
        const char *text;
        const char *const *items;
        int nitems;
        int sel;
        const char *keys;
        const char *ext;
        char *buf;
        int max;
        int buttons;
        int timeout_s;
        void (*live)(void *);
        void *live_ud;
        DlgRow *erows;                    // edit_list: the caller's rows, nitems of them
        void (*ecb)(void *, int, int);
    };
    struct Bar {
        bool on;
        char title[64], text[64];
        int value, total;
    };
    OsPort *os_;
    CoreFs *fs_;
    void *mu_;                            // the request slot, the answer and the bar
    void *ask_;                           // one request at a time
    void *done_;                          // the answer is there
    bool headless_;
    // the request in the slot, and the answer
    Req req_;
    volatile bool pending_;
    int answer_;
    volatile bool answered_;
    volatile bool abandoned_;
    // the open question (menu task)
    Req cur_;
    uint32_t live_ms_;                    // when cur_.live runs next
    uint32_t deadline_ms_;                // cur_'s end of time (os_->now_ms()); only while cur_.timeout_s
    DlgView v_;
    // picker state
    char pk_dir_[200];
    FsEntry *pk_list_;                    // DLG_PICK_MAX whole entries (43 KB): taken from the heap when a picker opens, given back when the question ends
    int pk_n_;
    char pk_ext_[12];
    // text state
    char tx_[DLG_TEXT_MAX + 1];
    int tx_n_;
    int sel_, first_;
    Bar bar_;
    uint32_t rev_;
    bool bar_dirty_;
    char pk_row_[DLG_ITEMS_MAX][60];

    bool req_pending() const { return pending_; }
    int ask(const Req &r);                // blocks; the answer
    void finish(int answer);              // menu task
    void load_dir();
    void fill_rows();
    static bool has_key(const char *keys, int n, int k, int *idx);
};

#endif
