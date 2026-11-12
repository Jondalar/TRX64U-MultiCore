#include "appdlg.h"
#include "keyboard.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#define KEY_LEFT_ARROW 0x60               // the C64's own key: one step out, in the menu and here

AppDlg::AppDlg(OsPort *os, CoreFs *fs)
    : os_(os), fs_(fs), mu_(os->mutex_new()), ask_(os->mutex_new()), done_(os->sem_new()), headless_(false), pending_(false), answer_(0),
      answered_(false), abandoned_(false), deadline_ms_(0), pk_list_(0), pk_n_(0), tx_n_(0), sel_(0), first_(0), rev_(1), bar_dirty_(false)
{
    memset(&req_, 0, sizeof(req_));
    memset(&cur_, 0, sizeof(cur_));
    memset(&v_, 0, sizeof(v_));
    memset(&bar_, 0, sizeof(bar_));
    pk_dir_[0] = 0;
    pk_ext_[0] = 0;
    tx_[0] = 0;
}

// ---- the application's task ---------------------------------------------------------------------------------------------------------------------

int AppDlg::ask(const Req &r)
{
    if (headless_) {
        return r.kind == DK_CHOOSE ? -1 : 0;
    }
    os_->lock(ask_);                                  // one question at a time: a second asker waits for the first to be answered
    os_->lock(mu_);
    req_ = r;
    answered_ = false;
    abandoned_ = false;
    pending_ = true;
    os_->unlock(mu_);
    while (!answered_ && !abandoned_) {
        os_->take(done_, 50);
    }
    os_->lock(mu_);
    int a = abandoned_ && !answered_ ? (r.kind == DK_CHOOSE ? -1 : 0) : answer_;
    pending_ = false;                                 // (a request nobody took up before abandon() is withdrawn here)
    os_->unlock(mu_);
    os_->unlock(ask_);
    return a;
}

void AppDlg::message(const char *title, const char *text)
{
    Req r;
    memset(&r, 0, sizeof(r));
    r.kind = DK_MESSAGE;
    r.title = title;
    r.text = text;
    r.buttons = SVC_BUTTON_OK;
    ask(r);
}

int AppDlg::buttons(const char *title, const char *text, int b)
{
    Req r;
    memset(&r, 0, sizeof(r));
    r.kind = DK_BUTTONS;
    r.title = title;
    r.text = text;
    r.buttons = b ? b : SVC_BUTTON_OK;
    return ask(r);
}

int AppDlg::buttons_timed(const char *title, const char *text, int b, int timeout_s)
{
    Req r;
    memset(&r, 0, sizeof(r));
    r.kind = DK_BUTTONS;
    r.title = title;
    r.text = text;
    r.buttons = b ? b : SVC_BUTTON_OK;
    r.timeout_s = timeout_s > 0 ? timeout_s : 0;
    return ask(r);
}

bool AppDlg::confirm(const char *title, const char *text)
{
    return buttons(title, text, SVC_BUTTON_YES | SVC_BUTTON_NO) == SVC_BUTTON_YES;
}

int AppDlg::choose(const char *title, const char *const *items, int n, int sel, const char *keys)
{
    if (n <= 0 || !items) {
        return -1;
    }
    Req r;
    memset(&r, 0, sizeof(r));
    r.kind = DK_CHOOSE;
    r.title = title;
    r.items = items;
    r.nitems = n;
    r.sel = sel < 0 || sel >= n ? 0 : sel;
    r.keys = keys;
    return ask(r);
}

int AppDlg::choose_timed(const char *title, const char *text, const char *const *items, int n, int timeout_s)
{
    if (n <= 0 || n > DLG_TIMEOUT || !items) {
        return -1;
    }
    Req r;
    memset(&r, 0, sizeof(r));
    r.kind = DK_CHOOSE;
    r.title = title;
    r.text = text;
    r.items = items;
    r.nitems = n;
    r.timeout_s = timeout_s > 0 ? timeout_s : 0;
    return ask(r);
}

bool AppDlg::text(const char *title, char *buf, int max)
{
    if (!buf || max < 2) {
        return false;
    }
    Req r;
    memset(&r, 0, sizeof(r));
    r.kind = DK_TEXT;
    r.title = title;
    r.buf = buf;
    r.max = max < DLG_TEXT_MAX + 1 ? max : DLG_TEXT_MAX + 1;
    return ask(r) != 0;
}

bool AppDlg::pick_file(const char *start, const char *ext, char *out, int outlen)
{
    if (!out || outlen < 2) {
        return false;
    }
    Req r;
    memset(&r, 0, sizeof(r));
    r.kind = DK_PICK_FILE;
    r.title = ext && ext[0] ? ext : "";
    r.text = start ? start : "";
    r.ext = ext;
    r.buf = out;
    r.max = outlen;
    return ask(r) != 0;
}

bool AppDlg::pick_folder(const char *start, char *out, int outlen)
{
    if (!out || outlen < 2) {
        return false;
    }
    Req r;
    memset(&r, 0, sizeof(r));
    r.kind = DK_PICK_FOLDER;
    r.title = "";
    r.text = start ? start : "";
    r.buf = out;
    r.max = outlen;
    return ask(r) != 0;
}

void *AppDlg::progress_open(const char *title, int total)
{
    os_->lock(mu_);
    bar_.on = true;
    snprintf(bar_.title, sizeof(bar_.title), "%s", title ? title : "");
    bar_.text[0] = 0;
    bar_.value = 0;
    bar_.total = total > 0 ? total : 1;
    bar_dirty_ = true;
    os_->unlock(mu_);
    return &bar_;
}

void AppDlg::progress_set(void *h, int value, const char *text)
{
    if (h != &bar_) {
        return;
    }
    os_->lock(mu_);
    bar_.value += value;                              // ABI 2's progress_set(h, n, msg) steps the bar by n
    if (bar_.value > bar_.total) {
        bar_.value = bar_.total;
    }
    if (text) {
        snprintf(bar_.text, sizeof(bar_.text), "%s", text);
    }
    bar_dirty_ = true;
    os_->unlock(mu_);
}

void AppDlg::progress_close(void *h)
{
    if (h != &bar_) {
        return;
    }
    os_->lock(mu_);
    bar_.on = false;
    bar_dirty_ = true;
    os_->unlock(mu_);
}

void AppDlg::abandon()
{
    os_->lock(mu_);
    abandoned_ = true;
    bar_.on = false;
    bar_dirty_ = true;
    os_->unlock(mu_);
    os_->give(done_);
    if (cur_.kind != DK_NONE) {
        cur_.kind = DK_NONE;
        v_.kind = DK_NONE;
        rev_++;
    }
}

// ---- the menu task -----------------------------------------------------------------------------------------------------------------------------

void AppDlg::finish(int answer)
{
    os_->lock(mu_);
    answer_ = answer;
    answered_ = true;
    os_->unlock(mu_);
    cur_.kind = DK_NONE;
    v_.kind = DK_NONE;
    rev_++;
    free(pk_list_);
    pk_list_ = 0;
    os_->give(done_);
}

static bool ends_with(const char *name, const char *ext)
{
    if (!ext || !ext[0]) {
        return true;
    }
    int l = (int)strlen(name), e = (int)strlen(ext);
    return l >= e && !strcasecmp(name + l - e, ext);
}

void AppDlg::load_dir()
{
    // whole FAT long names (T180 #38): the 160 entries are 43 KB, in the list and in the raw listing, both from the heap while a picker is open
    FsEntry *raw = (FsEntry *)malloc(sizeof(FsEntry) * DLG_PICK_MAX);
    if (!pk_list_) {
        pk_list_ = (FsEntry *)malloc(sizeof(FsEntry) * DLG_PICK_MAX);
    }
    pk_n_ = 0;
    sel_ = 0;
    first_ = 0;
    if (!raw || !pk_list_) {
        free(raw);
        return;
    }
    int n = fs_ ? fs_->list(pk_dir_, raw, DLG_PICK_MAX) : -1;
    if (strcmp(pk_dir_, "/")) {
        memset(&pk_list_[0], 0, sizeof(pk_list_[0]));
        snprintf(pk_list_[0].name, sizeof(pk_list_[0].name), "..");
        pk_list_[0].dir = true;
        pk_n_ = 1;
    }
    for (int pass = 0; pass < 2; pass++) {            // folders first, then the files the extension asks for
        int from = pk_n_;
        for (int i = 0; i < n && pk_n_ < DLG_PICK_MAX; i++) {
            bool d = raw[i].dir;
            if ((pass == 0) != d || (!d && (cur_.kind == DK_PICK_FOLDER || !ends_with(raw[i].name, pk_ext_)))) {
                continue;
            }
            pk_list_[pk_n_++] = raw[i];
        }
        for (int a = from + 1; a < pk_n_; a++) {      // by name, without regard to case
            FsEntry t = pk_list_[a];
            int b = a - 1;
            while (b >= from && strcasecmp(pk_list_[b].name, t.name) > 0) {
                pk_list_[b + 1] = pk_list_[b];
                b--;
            }
            pk_list_[b + 1] = t;
        }
    }
    free(raw);
    sel_ = 0;
    first_ = 0;
}

bool AppDlg::service()
{
    bool changed = false;
    if (cur_.kind == DK_NONE && pk_list_) {            // a picker that ended (also one the application abandoned): its list goes back to the heap, on this task
        free(pk_list_);
        pk_list_ = 0;
    }
    if (cur_.kind == DK_NONE && pending_) {
        os_->lock(mu_);
        cur_ = req_;
        pending_ = false;                             // taken up: it is the open question now, not a waiting one
        os_->unlock(mu_);
        sel_ = 0;
        first_ = 0;
        if (cur_.kind == DK_CHOOSE) {
            sel_ = cur_.sel;
        } else if (cur_.kind == DK_TEXT) {
            snprintf(tx_, sizeof(tx_), "%s", cur_.buf);
            tx_n_ = (int)strlen(tx_);
        } else if (cur_.kind == DK_PICK_FILE || cur_.kind == DK_PICK_FOLDER) {
            snprintf(pk_ext_, sizeof(pk_ext_), "%s", cur_.ext ? cur_.ext : "");
            snprintf(pk_dir_, sizeof(pk_dir_), "%s", cur_.text && cur_.text[0] == '/' ? cur_.text : "/");
            if (cur_.kind == DK_PICK_FILE) {              // a start that is a file: its folder
                char *sl = strrchr(pk_dir_, '/');
                uint64_t sz;
                bool d = true;
                if (sl && sl != pk_dir_ && fs_ && fs_->stat(pk_dir_, &sz, &d) && !d) {
                    *sl = 0;
                }
            }
            size_t l = strlen(pk_dir_);
            while (l > 1 && pk_dir_[l - 1] == '/') {
                pk_dir_[--l] = 0;
            }
            load_dir();
        }
        deadline_ms_ = cur_.timeout_s ? os_->now_ms() + (uint32_t)cur_.timeout_s * 1000u : 0;      // the time runs from the moment the question is on the screen
        v_.remaining_s = cur_.timeout_s;
        rev_++;
        changed = true;
    }
    if (cur_.kind != DK_NONE && cur_.timeout_s) {
        uint32_t now = os_->now_ms();
        if ((int32_t)(now - deadline_ms_) >= 0) {
            finish(DLG_TIMEOUT);
            changed = true;
        } else {
            int left = (int)((deadline_ms_ - now + 999u) / 1000u);
            if (left != v_.remaining_s) {
                v_.remaining_s = left;
                rev_++;
                changed = true;
            }
        }
    }
    if (bar_dirty_) {
        bar_dirty_ = false;
        rev_++;
        changed = true;
    }
    return changed;
}

bool AppDlg::has_key(const char *keys, int n, int k, int *idx)
{
    if (!keys || k < 0x20 || k > 0x7E) {
        return false;
    }
    for (int i = 0; i < n && keys[i]; i++) {
        if (toupper(keys[i]) == toupper(k)) {
            *idx = i;
            return true;
        }
    }
    return false;
}


// The dialog's key table: a key is an intent (T192.3 D23); apply() is what a dialog does, whoever sent the intent (the board's keyboard, a remote `key`).
DlgIntent AppDlg::resolve(int k)
{
    static const struct { int key; int id; } T[] = {
        { KEY_UP, DI_UP }, { KEY_DOWN, DI_DOWN }, { KEY_LEFT, DI_LEFT }, { KEY_RIGHT, DI_RIGHT }, { KEY_PAGEUP, DI_PAGE_UP }, { KEY_PAGEDOWN, DI_PAGE_DOWN },
        { KEY_HOME, DI_HOME }, { KEY_END, DI_END }, { KEY_RETURN, DI_RETURN }, { KEY_SPACE, DI_SPACE }, { KEY_ESCAPE, DI_ESC }, { KEY_LEFT_ARROW, DI_ARROW },
        { KEY_BREAK, DI_STOP }, { KEY_F10, DI_STOP }, { KEY_MENU, DI_STOP }, { KEY_SCRLOCK, DI_STOP }, { KEY_BACK, DI_DEL },
    };
    DlgIntent in;
    in.id = DI_CHAR;                                  // every other key is a character: a typed letter, a hotkey of a list
    in.arg = k;
    for (unsigned i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
        if (T[i].key == k) {
            in.id = (uint8_t)T[i].id;
            break;
        }
    }
    return in;
}

bool AppDlg::take_key(int k)
{
    return apply(resolve(k));
}

bool AppDlg::apply(const DlgIntent &in)
{
    if (cur_.kind == DK_NONE) {
        return false;
    }
    bool back = in.id == DI_ARROW || in.id == DI_ESC;
    bool stop = in.id == DI_STOP;
    const int ch = (in.id == DI_CHAR || in.id == DI_SPACE) ? in.arg : -1;       // the key as a character, -1 when it is none
    switch (cur_.kind) {
    case DK_MESSAGE:
        if (in.id == DI_RETURN || in.id == DI_SPACE || back || stop) {
            finish(SVC_BUTTON_OK);
        }
        break;
    case DK_BUTTONS: {
        int b = cur_.buttons;
        bool two = (b & SVC_BUTTON_YES) && (b & SVC_BUTTON_NO);
        if (ch == 'y' || ch == 'Y') {
            finish(b & SVC_BUTTON_YES ? SVC_BUTTON_YES : 0);
        } else if (ch == 'n' || ch == 'N') {
            finish(b & SVC_BUTTON_NO ? SVC_BUTTON_NO : 0);
        } else if (two && (in.id == DI_LEFT || in.id == DI_RIGHT || in.id == DI_UP || in.id == DI_DOWN)) {
            sel_ ^= 1;                                // 0: yes, 1: no
            rev_++;
        } else if (in.id == DI_RETURN) {
            finish(two ? (sel_ ? SVC_BUTTON_NO : SVC_BUTTON_YES) : (b & SVC_BUTTON_OK ? SVC_BUTTON_OK : (b & SVC_BUTTON_YES ? SVC_BUTTON_YES : SVC_BUTTON_NO)));
        } else if (back || stop) {
            finish(two ? 0 : (b & SVC_BUTTON_OK ? SVC_BUTTON_OK : 0));
        }
        break;
    }
    case DK_CHOOSE: {
        int idx = 0;
        if (in.id == DI_UP && sel_ > 0) {
            sel_--;
            rev_++;
        } else if (in.id == DI_DOWN && sel_ + 1 < cur_.nitems) {
            sel_++;
            rev_++;
        } else if (in.id == DI_PAGE_UP) {
            sel_ = sel_ > 10 ? sel_ - 10 : 0;
            rev_++;
        } else if (in.id == DI_PAGE_DOWN) {
            sel_ = sel_ + 10 < cur_.nitems ? sel_ + 10 : cur_.nitems - 1;
            rev_++;
        } else if (in.id == DI_HOME) {
            sel_ = 0;
            rev_++;
        } else if (in.id == DI_END) {
            sel_ = cur_.nitems - 1;
            rev_++;
        } else if (in.id == DI_RETURN) {
            finish(sel_);
        } else if (ch >= 0 && has_key(cur_.keys, cur_.nitems, ch, &idx)) {
            finish(idx);
        } else if (back || stop) {
            finish(-1);
        }
        break;
    }
    case DK_TEXT:
        if (in.id == DI_RETURN) {
            snprintf(cur_.buf, cur_.max, "%s", tx_);
            finish(1);
        } else if (back || stop) {
            if (in.id == DI_ESC && tx_n_ > 0) {       // ESC clears the text first, as the menu's own prompts do
                tx_[0] = 0;
                tx_n_ = 0;
                rev_++;
            } else {
                finish(0);
            }
        } else if (in.id == DI_DEL) {
            if (tx_n_ > 0) {
                tx_[--tx_n_] = 0;
                rev_++;
            }
        } else if (ch >= 0x20 && ch <= 0x7E && tx_n_ < cur_.max - 1) {
            tx_[tx_n_++] = (char)ch;
            tx_[tx_n_] = 0;
            rev_++;
        }
        break;
    case DK_PICK_FILE:
    case DK_PICK_FOLDER: {
        bool folder = cur_.kind == DK_PICK_FOLDER;
        int total = pk_n_ + (folder ? 1 : 0);          // "[ use this folder ]" is row 0 of a folder picker
        if (in.id == DI_UP && sel_ > 0) {
            sel_--;
            rev_++;
        } else if (in.id == DI_DOWN && sel_ + 1 < total) {
            sel_++;
            rev_++;
        } else if (in.id == DI_PAGE_UP) {
            sel_ = sel_ > 10 ? sel_ - 10 : 0;
            rev_++;
        } else if (in.id == DI_PAGE_DOWN) {
            sel_ = sel_ + 10 < total ? sel_ + 10 : total - 1;
            rev_++;
        } else if (in.id == DI_HOME) {
            sel_ = 0;
            rev_++;
        } else if (in.id == DI_END) {
            sel_ = total - 1;
            rev_++;
        } else if (in.id == DI_RETURN && total > 0) {
            if (folder && sel_ == 0) {
                snprintf(cur_.buf, cur_.max, "%s", pk_dir_);
                finish(1);
            } else {
                const FsEntry &e = pk_list_[sel_ - (folder ? 1 : 0)];
                if (!strcmp(e.name, "..")) {
                    char *sl = strrchr(pk_dir_, '/');
                    if (sl && sl != pk_dir_) {
                        *sl = 0;
                    } else {
                        strcpy(pk_dir_, "/");
                    }
                    load_dir();
                } else if (e.dir) {
                    size_t l = strlen(pk_dir_);
                    if (l + 1 + strlen(e.name) < sizeof(pk_dir_)) {       // a folder path that does not fit is not entered: a cut one is another folder
                        snprintf(pk_dir_ + l, sizeof(pk_dir_) - l, "%s%s", l > 1 ? "/" : "", e.name);
                        load_dir();
                    }
                } else {
                    int w = snprintf(cur_.buf, cur_.max, "%s%s%s", pk_dir_, strcmp(pk_dir_, "/") ? "/" : "", e.name);
                    if (w < cur_.max) {
                        finish(1);
                    } else {
                        cur_.buf[0] = 0;                                  // the application's buffer cannot hold this path: the pick stays open, a cut path is another file
                    }
                }
            }
            rev_++;
        } else if (back && strcmp(pk_dir_, "/")) {
            char *sl = strrchr(pk_dir_, '/');
            if (sl && sl != pk_dir_) {
                *sl = 0;
            } else {
                strcpy(pk_dir_, "/");
            }
            load_dir();
            rev_++;
        } else if (back || stop) {
            finish(0);
        }
        break;
    }
    default:
        break;
    }
    return true;                                      // an open question swallows every key
}

void AppDlg::fill_rows()
{
    int total = 0;
    bool folder = cur_.kind == DK_PICK_FOLDER;
    if (cur_.kind == DK_CHOOSE) {
        total = cur_.nitems;
    } else if (cur_.kind == DK_PICK_FILE || cur_.kind == DK_PICK_FOLDER) {
        total = pk_n_ + (folder ? 1 : 0);
    }
    v_.nitems = total;
    int rows = total < DLG_ITEMS_MAX ? total : DLG_ITEMS_MAX;
    if (sel_ < first_) {
        first_ = sel_;
    }
    if (sel_ >= first_ + rows) {
        first_ = sel_ - rows + 1;
    }
    v_.first = first_;
    v_.sel = sel_;
    v_.rows = rows;
    for (int i = 0; i < rows; i++) {
        int ix = first_ + i;
        v_.row_dir[i] = false;
        if (cur_.kind == DK_CHOOSE) {
            v_.row[i] = cur_.items[ix];
        } else if (folder && ix == 0) {
            v_.row[i] = "[ use this folder ]";
        } else {
            const FsEntry &e = pk_list_[ix - (folder ? 1 : 0)];
            v_.row_dir[i] = e.dir;
            snprintf(pk_row_[i], sizeof(pk_row_[i]), "%s%s", e.name, e.dir && strcmp(e.name, "..") ? "/" : "");
            v_.row[i] = pk_row_[i];
        }
    }
}

const DlgView &AppDlg::view()
{
    os_->lock(mu_);
    v_.bar = bar_.on;
    snprintf(v_.bar_title, sizeof(v_.bar_title), "%s", bar_.title);
    snprintf(v_.bar_text, sizeof(v_.bar_text), "%s", bar_.text);
    v_.bar_value = bar_.value;
    v_.bar_total = bar_.total;
    os_->unlock(mu_);
    v_.kind = cur_.kind;
    if (cur_.kind == DK_NONE) {
        v_.remaining_s = 0;
        return v_;
    }
    snprintf(v_.title, sizeof(v_.title), "%s", cur_.title ? cur_.title : "");
    v_.text[0] = 0;
    v_.input[0] = 0;
    v_.caret = 0;
    v_.buttons = cur_.buttons;
    if (cur_.kind == DK_MESSAGE || cur_.kind == DK_BUTTONS || cur_.kind == DK_CHOOSE) {
        if (cur_.text) {
            snprintf(v_.text, sizeof(v_.text), "%s", cur_.text);
        }
    }
    if (cur_.kind == DK_TEXT) {
        snprintf(v_.input, sizeof(v_.input), "%s", tx_);
        v_.caret = tx_n_;
    }
    if (cur_.kind == DK_PICK_FILE || cur_.kind == DK_PICK_FOLDER) {
        snprintf(v_.input, sizeof(v_.input), "%s", pk_dir_);
        snprintf(v_.title, sizeof(v_.title), "%s", cur_.kind == DK_PICK_FOLDER ? "Choose a folder" : (pk_ext_[0] ? "Choose a file" : "Choose a file"));
        if (pk_ext_[0]) {
            snprintf(v_.text, sizeof(v_.text), "%s files", pk_ext_);
        }
    }
    if (cur_.kind == DK_BUTTONS) {
        v_.caret = sel_;                              // which of yes / no has the focus
    }
    fill_rows();
    return v_;
}
