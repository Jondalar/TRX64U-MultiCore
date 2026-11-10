#include "menuctl.h"
#include "keyboard.h"
#include "ops_glue.h"
#include "diag.h"
#include "settings_ops.h"
#include "contrib.h"
#include "menuvlist.h"
#include "menuflows.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

static const char *base_name_of(const char *p);

static void fm_say(void *user, const char *text, bool bad);
static bool fm_on_browser(void *user);

MenuCtl::MenuCtl(CoreCtx *c, MenuHost *h) : c_(c), h_(h), fm_(c, FileMgr::Port{ this, fm_say, fm_on_browser }), area_(0), tab_(0), status_bad_(false), chooser_(false),
    chooser_ft_(0), chooser_sel_(0), chooser_acts_(0), chooser_n_(0), entry_text_mode_(false), entry_task_(0),
    pick_(false), pick_task_(0), pick_set_(-1), pick_n_(0), pick_sel_(0), pick_ok_(true), busy_(false), job_started_(false), job_task_(0),
    entry_(false), entry_act_(0), entry_def_(0), entry_n_(0), entry_fresh_(false), app_mode_(false), flow_q_(0)
{
    follow_last_[0] = 0;
    close_req_ = false;
    entry_set_ = -1;
    le_init(&entry_le_, "", 40, 0);
    page_rows_ = 10;
    folder_sheet_ = false;
    search_edit_ = false;
    confirm_task_ = 0;
    entry_rename_ = false;
    entry_mset_ = false;
    pick_sets_ = false;
    rename_path_[0] = 0;
    confirm_node_ = 0;
    confirm_q_[0] = 0;
    ask_node_ = 0;
    ask_q_[0] = 0;
    pick_path_[0] = 0;
    job_name_[0] = 0;
    rebuild();
    memset(sel_, 0, sizeof(sel_));
    memset(last_tab_, 0, sizeof(last_tab_));
    status_[0] = 0;
    chooser_file_[0] = 0;
    entry_file_[0] = 0;
    entry_digits_[0] = 0;
    le_init(&entry_le_, "", 40, 0);
}

static void fm_say(void *user, const char *text, bool bad)
{
    ((MenuCtl *)user)->say(text, bad);
}

static bool fm_on_browser(void *user)
{
    return ((MenuCtl *)user)->browsing();
}

// The menu has opened and the machine is held: the flows a module owns get their turn (menuflows.cc: the question about a changed cartridge). An operation answers a note for the
// status line; a question it put up is shown as a confirmation (RETURN yes, <- no).
void MenuCtl::held_open()
{
    int n;
    const MenuFlow *fl = menu_flows(&n);
    for (int i = 0; i < n; i++) {
        OpResult r;
        r.msg[0] = 0;
        ops_call(c_, fl[i].open, 0, 0, &r);
        if (r.msg[0]) {
            set_status(r.msg, true);
        }
        const char *q = ops_state(c_, fl[i].ask);
        if (q && q[0]) {
            entry_ = true;
            entry_text_mode_ = true;
            entry_task_ = 0;
            entry_set_ = -1;
            ask_node_ = 0;
            confirm_node_ = 0;
            confirm_task_ = 0;
            flow_q_ = &fl[i];
            snprintf(confirm_q_, sizeof(confirm_q_), "%s", q);
            set_status(fl[i].hint, false);
            return;
        }
    }
}

// The tree from the descriptor and what is contributed to it (contrib.h). The run loop calls it again once the libraries are loaded.
void MenuCtl::rebuild()
{
    menu_build_for(c_, &tree_);
    if (area_ >= tree_.nareas) {
        area_ = 0;
    }
    if (tab_ >= tree_.a[area_].ntabs) {
        tab_ = 0;
    }
    memset(sel_, 0, sizeof(sel_));
    memset(last_tab_, 0, sizeof(last_tab_));
}

bool MenuCtl::closable() const
{
    return !(c_->desc->needs_program && !c_->program_loaded);
}

const char *MenuCtl::session_word(bool upper) const
{
    if (c_->paused) {
        return upper ? "PAUSED" : "Paused";
    }
    if (!closable()) {
        return upper ? "WAITING" : "Waiting";
    }
    if (c_->menu_open && c_->started) {
        return upper ? "FROZEN" : "Frozen";
    }
    return upper ? "RUNNING" : "Running";
}

void MenuCtl::set_status(const char *s, bool bad)
{
    snprintf(status_, sizeof(status_), "%s", s);
    status_bad_ = bad;
}

// The tab whose only entry is the browser; -1 when the core has none (it always has one today).
static void find_browser(const MenuTree &t, int *area, int *tab)
{
    *area = *tab = 0;
    for (int a = 0; a < t.nareas; a++) {
        for (int b = 0; b < t.a[a].ntabs; b++) {
            const MenuTab &tb = t.a[a].tabs[b];
            if (tb.count == 1 && t.e[tb.first].kind == EK_BROWSER) {
                *area = a;
                *tab = b;
                return;
            }
        }
    }
}

bool MenuCtl::is_browser() const
{
    const MenuTab &tb = cur();
    return tb.count == 1 && tree_.e[tb.first].kind == EK_BROWSER;
}

bool MenuCtl::is_home() const
{
    const MenuTab &tb = cur();
    return tb.count > 0 && tree_.e[tb.first].kind == EK_INFO;
}

// The dominant line of the Home panel: the program the machine is running, or what it is waiting for.
void MenuCtl::session_line(char *out, int n) const
{
    for (int i = 0; i < c_->desc->nmedia; i++) {
        const char *w = ops_state(c_, c_->desc->media[i].state);
        if (w && *w) {
            snprintf(out, n, "%s", w);
            return;
        }
    }
    snprintf(out, n, "%s", c_->started ? c_->desc->title : (c_->desc->needs_program ? c_->desc->needs_program : "held"));
}

void MenuCtl::info_line(int which, char *out, int n) const
{
    out[0] = 0;
    h_->info(which, out, n);
}

void MenuCtl::video_line(char *out, int n) const
{
    const char *w = ops_state(c_, menu_facts()->video_line);
    snprintf(out, n, "%s", w ? w : "");
}

static bool is_switch(const Setting &s);

bool MenuCtl::on_setting() const
{
    if (is_browser() || cur().count == 0) {
        return false;
    }
    const MenuEntry &e = tree_.e[cur().first + sel_[area_][tab_]];
    // a switch changes on RETURN and SPACE only: left and right are for choices and numbers (and a stray right arrow must not turn a drive off)
    return e.kind == EK_SETTING && !c_->desc->settings[e.index].kind && !is_switch(c_->desc->settings[e.index]);
}

bool MenuCtl::is_settings() const
{
    const MenuTab &tb = cur();
    return tb.count > 0 && tree_.e[tb.first].kind == EK_SETTING;
}

void MenuCtl::go(int area, int tab)
{
    if (area < 0 || area >= tree_.nareas) {
        return;
    }
    bool moved = area != area_ || tab != tab_;
    area_ = area;
    tab_ = (tab >= 0 && tab < tree_.a[area].ntabs) ? tab : 0;
    last_tab_[area] = tab_;
    if (moved && cur().layout && cur().layout[0]) {                  // a screen shows its own state: a message of the tab before it is not about this one
        status_[0] = 0;
        status_bad_ = false;
    }
    chooser_ = false;
    entry_ = false;
    pick_ = false;
    pick_sets_ = false;
    fm_.view = false;
    search_edit_ = false;
    entry_rename_ = false;
    entry_mset_ = false;
    if (app_mode_ && moved) {
        app_start();                                                // the application's own tab is shown now: its `enter` operation runs
    } else if (!app_mode_) {
        char ln[24];
        ui_name(cur().layout, ln, sizeof(ln));
        if (const char *eop = menu_layout_enter(ln)) {              // a screen of the core that starts something when it is shown (the monitor's session)
            OpResult er;
            ops_call(c_, eop, 0, 0, &er);
        }
    }
    if (is_browser()) {
        if (fm_.vmode == 1 && !search_tab()) {                         // the search's list belongs to its own tab
            fm_.vmode = 0;
        }
        fm_.refresh_roots();                                            // the media chips: an FTP host set since the last visit, a result list that came
        fm_.load_dir();
        if (search_tab() && !fm_.vmode) {
            fm_.nlist = 0;                                                      // no folder listing under a field that has not been used
            fm_.lib_sync();
            set_status("Press RETURN and type a part of the name", false);   // 1-3 stay the tab keys until a field is open
        }
    }
    fm_.lib_sync();
    settle();
}

void MenuCtl::settle()
{
    if (!screen() || is_browser() || rows() == 0) {
        return;
    }
    Item it;
    for (int tries = 0; tries < rows(); tries++) {                  // the first row that takes focus: not a value line, not hidden, not greyed
        item(sel_[area_][tab_], &it);
        if (!(it.kind == IK_VALUE || it.kind == IK_HEADING || it.kind == IK_INFO || it.hidden || it.dim)) {
            break;
        }
        int before = sel_[area_][tab_];
        move(1);
        if (sel_[area_][tab_] == before) {
            break;
        }
    }
    if (app_mode_) {                                                // an application's screen starts on something that does something: its first button (a question's rows are only text)
        int first = -1, any = -1;
        for (int i = 0; i < rows() && first < 0; i++) {
            Item b;
            item(i, &b);
            if (b.kind == IK_BUTTON && !b.hidden && b.enabled) {
                if (any < 0) {
                    any = i;
                }
                if (b.primary) {
                    first = i;
                }
            }
        }
        if (first < 0) {
            first = any;
        }
        Item cur_it;
        item(sel_[area_][tab_], &cur_it);
        if (first >= 0 && !(cur_it.kind == IK_BUTTON && !cur_it.hidden && cur_it.enabled)) {
            sel_[area_][tab_] = first;
        }
    }
}

void MenuCtl::open()
{
    chooser_ = false;
    entry_ = false;
    area_ = 0;
    tab_ = 0;
    if (!closable()) {                      // a core that waits for a program opens in the browser
        int a, b;
        find_browser(tree_, &a, &b);
        area_ = a;
        tab_ = b;
    }
    status_[0] = 0;
    status_bad_ = false;
    fm_.open_home_if_set();                                         // the Library opens where the host's File Browser does: on the media level ("/"), and later where it was left (T104.2, Alex 2026-10-29)
    if (!closable() && c_->desc->needs_program) {
        set_status(c_->desc->needs_program, false);
    }
    if (c_->notice_pending) {
        set_status(c_->notice, true);
        c_->notice_pending = false;
    }
    fm_.load_dir();
    settle();
    if (reopen_.valid && !app_mode_ && closable() && fm_.reopen_last()) {   // T192.3 D30: where it was left
        reopen_restore();
    }
}

void MenuCtl::leave()
{
    if (app_mode_) {
        return;
    }
    Reopen &r = reopen_;
    r = Reopen();
    r.valid = true;
    r.area = area_;
    r.tab = tab_;
    snprintf(r.area_name, sizeof(r.area_name), "%s", tree_.a[area_].name ? tree_.a[area_].name : "");
    snprintf(r.tab_id, sizeof(r.tab_id), "%s", tab_id());
    r.scroll = scroll_;
    if (is_browser()) {
        if (!fm_.vmode && fm_.image_ft < 0) {
            snprintf(r.folder, sizeof(r.folder), "%s", fm_.path);
        }
        if (fm_.sel >= 0 && fm_.sel < fm_.nlist) {
            snprintf(r.row, sizeof(r.row), "%s", fm_.list[fm_.sel].name);
        }
    } else if (rows() > 0 && sel_[area_][tab_] < rows()) {
        Item it;
        item(sel_[area_][tab_], &it);
        snprintf(r.row, sizeof(r.row), "%s", it.ident);
    }
}

// open() has put the menu on Home; the place of the last leave() is looked up in the tree as it is now. Whatever is gone falls back: an area to Home, a tab to the area's
// first, a folder to the nearest one above that still lists, a row to the tab's first.
void MenuCtl::reopen_restore()
{
    const Reopen &r = reopen_;
    int a = 0;
    for (int i = 0; i < tree_.nareas; i++) {
        if (!strcmp(tree_.a[i].name ? tree_.a[i].name : "", r.area_name)) {
            a = i;
            break;
        }
    }
    int t = -1;
    for (int i = 0; i < tree_.a[a].ntabs && r.tab_id[0]; i++) {
        const char *id = tree_.a[a].tabs[i].id;
        if (id && !strcmp(id, r.tab_id)) {
            t = i;
            break;
        }
    }
    bool exact = t >= 0;
    if (t < 0) {
        t = (!r.tab_id[0] && a == r.area && r.tab < tree_.a[a].ntabs) ? r.tab : 0;
        exact = !r.tab_id[0] && a == r.area && t == r.tab;
    }
    if (tab_is_hidden(a, t)) {
        t = 0;
        exact = false;
    }
    if (a != area_ || t != tab_) {
        go(a, t);
    }
    bool same_view = exact && (a == r.area || !strcmp(tree_.a[a].name ? tree_.a[a].name : "", r.area_name));
    if (is_browser()) {
        if (r.folder[0] && !fm_.vmode) {
            fm_.vmode = 0;
            fm_.image_ft = -1;
            if (strcmp(fm_.path, r.folder)) {
                snprintf(fm_.path, sizeof(fm_.path), "%s", r.folder);
                fm_.sel = 0;
                fm_.load_dir();
            }
            while (!fm_.dir_ok && strcmp(fm_.path, "/")) {
                fm_.parent_dir();                                    // the folder is gone: the nearest one above that lists
                same_view = false;
            }
        }
        int found = -1;
        for (int i = 0; i < fm_.nlist && r.row[0]; i++) {
            if (!strcmp(fm_.list[i].name, r.row)) {
                found = i;
                break;
            }
        }
        fm_.sel = found >= 0 ? found : 0;
        same_view = same_view && found >= 0;
        fm_.lib_sync();
    } else {
        int found = -1;
        for (int i = 0; i < rows() && r.row[0]; i++) {
            Item it;
            item(i, &it);
            if (!strcmp(it.ident, r.row)) {
                found = i;
                break;
            }
        }
        sel_[area_][tab_] = found >= 0 ? found : 0;
        same_view = same_view && found >= 0;
        settle();
    }
    scroll_ = same_view ? r.scroll : 0;                              // the screen starts its window there
    scroll_rev_++;
}

const char *MenuCtl::title() const
{
    if (pick_) {
        return pick_sets_ ? "Load media set" : pick_path_;
    }
    if (busy_) {
        return job_task_->label;
    }
    if (const MenuWizard *w = wizard()) {
        const char *t = ops_state(c_, w->title);
        return t ? t : "";
    }
    return is_browser() ? fm_.path : cur().name;
}







// The media the browser lists can turn up later than the menu does (a USB stick still being mounted while the core starts): the run loop asks every two seconds. The
// Library's own folder is read again and the cursor follows its entry by name (filemgr.cc). Nothing is read while a prompt, a sheet or a job holds the screen.
bool MenuCtl::relist()
{
    if (!is_browser() || chooser_ || entry_ || pick_ || busy_) {
        return false;
    }
    return fm_.relist();
}

void MenuCtl::browse_row(int i, BrowseRow *r) const
{
    char marks[160];
    marks[0] = 0;
    if (!layout_param("marks", marks, sizeof(marks))) {                  // the tab's ui `marks` = "A=state,C=state": [A] in drive A, [C] in the port, [P] the last program
        marks[0] = 0;
    }
    fm_.browse_row(i, r, marks);
}

int MenuCtl::rows()
{
    if (busy_ || wizard()) {
        return 1;
    }
    if (pick_) {
        return pick_sets_ ? pick_n_ : 1 + pick_n_;
    }
    if (entry_ && entry_text_mode_) {
        return 1;
    }
    if (is_browser()) {
        if (entry_) {
            return 1;
        }
        if (chooser_) {
            return chooser_n_;
        }
        return fm_.nlist;
    }
    return cur().count;
}

int MenuCtl::selected() const
{
    if (busy_ || wizard()) {
        return 0;
    }
    if (pick_) {
        return pick_sel_;
    }
    if (entry_ && entry_text_mode_) {
        return 0;
    }
    if (is_browser()) {
        return entry_ ? 0 : (chooser_ ? chooser_sel_ : fm_.sel);
    }
    return sel_[area_][tab_];
}

// The entry's text and the cursor in the row's value: the cursor is a '_' where the next character goes; a text that does not fit scrolls so the cursor
// stays in sight, ".." marks what is hidden (app/edit le_row). mask: a secret shows that character for every one.
void MenuCtl::entry_row(char *right, int rn, char mask)
{
    const int vis = ENTRY_VIS < rn - 1 ? ENTRY_VIS : rn - 1;
    le_row(&entry_le_, vis, mask, right, rn);
}

void MenuCtl::entry_load(const char *text, int max)
{
    le_init(&entry_le_, text, max > LE_MAX ? LE_MAX : max, 0);
}

void MenuCtl::row(int i, char *left, int ln, char *right, int rn, bool *active, char *sub, int sn)
{
    left[0] = right[0] = 0;
    if (sub && sn) {
        sub[0] = 0;
    }
    *active = true;
    if (busy_) {
        job_text(left, ln, right, rn, sub, sub ? sn : 0);
        return;
    }
    if (const MenuWizard *w = wizard()) {                           // a wizard of a module is up: its three texts
        const char *l = ops_state(c_, w->left), *r = ops_state(c_, w->right), *sb = ops_state(c_, w->sub);
        snprintf(left, ln, "%s", l ? l : "");
        snprintf(right, rn, "%s", r ? r : "");
        if (sub && sn) {
            snprintf(sub, sn, "%s", sb ? sb : "");
        }
        return;
    }
    if (pick_ && pick_sets_) {
        if (i < pick_n_) {
            snprintf(left, ln, "%s", sets_[i]);
            if (sub && sn) {
                snprintf(sub, sn, "RETURN loads this set and resets the core");
            }
        }
        return;
    }
    if (pick_ && pick_set_ >= 0) {                                  // T185 D4: the selector of a file
        if (i == 0) {
            snprintf(left, ln, "No file");
            if (sub && sn) {
                snprintf(sub, sn, "RETURN clears the selection");
            }
        } else if (i - 1 < pick_n_) {
            const FsEntry &fe = pick_list_[i - 1];
            snprintf(left, ln, "%s%s", fe.name, fe.dir && strcmp(fe.name, "..") ? "/" : "");
            if (!fe.dir && right && rn) {
                snprintf(right, rn, "%llu bytes", (unsigned long long)fe.size);
            }
            if (!pick_ok_ && sub && sn) {
                snprintf(sub, sn, "This folder cannot be read");
            }
        }
        return;
    }
    if (pick_) {
        if (i == 0) {
            snprintf(left, ln, "%s", pick_task_ && pick_task_->pick_label ? pick_task_->pick_label : "Create here");
            if (strcmp(pick_path_, "/") == 0) {
                snprintf(left, ln, "Pick a medium first");
                *active = false;
            } else if (sub && sn) {
                snprintf(sub, sn, "%s", pick_path_);
            }
        } else if (i - 1 < pick_n_) {
            snprintf(left, ln, "%s%s", pick_list_[i - 1].name, strcmp(pick_list_[i - 1].name, "..") ? "/" : "");
            if (!pick_ok_ && sub && sn) {
                snprintf(sub, sn, "This folder cannot be read");
            }
        }
        return;
    }
    if (entry_ && entry_text_mode_) {
        if (confirm_node_ || confirm_task_ || flow_q_ || remote_q_) {             // a question: RETURN does it, ESC does not
            snprintf(left, ln, "%s", confirm_q_);
            snprintf(right, rn, "RETURN yes   <- no");
            if (sub && sn) {
                snprintf(sub, sn, "Nothing happens until you answer");
            }
            return;
        }
        if (entry_mset_) {
            snprintf(left, ln, "Save media set as");
            entry_row(right, rn);
            if (sub && sn) {
                snprintf(sub, sn, "What is mounted now, in the core's media folder - DEL deletes, RETURN saves, <- cancels");
            }
            return;
        }
        if (entry_rename_ || search_edit_) {
            snprintf(left, ln, "%s", search_edit_ ? "Search the Library" : (rename_path_[0] ? "Rename to" : "New folder"));
            entry_row(right, rn);
            if (sub && sn) {
                snprintf(sub, sn, search_edit_ ? "? is any character - RETURN searches, DEL deletes, <- cancels" : "DEL deletes, RETURN renames, <- cancels");
            }
            return;
        }
        if (ask_node_) {
            snprintf(left, ln, "%s", ask_q_);
            entry_row(right, rn);
            if (sub && sn) {
                snprintf(sub, sn, "Into %s - DEL deletes, RETURN takes it, <- cancels", c_->media_dir[0] ? c_->media_dir : "the first medium");
            }
            return;
        }
        if (entry_set_ >= 0) {                                       // a text setting
            snprintf(left, ln, "%s", c_->desc->settings[entry_set_].label ? c_->desc->settings[entry_set_].label : c_->desc->settings[entry_set_].key);
            entry_row(right, rn, c_->desc->settings[entry_set_].kind == 2 ? '*' : 0);
            if (sub && sn) {
                snprintf(sub, sn, "Type the text, DEL deletes, RETURN takes it, <- cancels");
            }
            return;
        }
        snprintf(left, ln, "%s", entry_task_->ask);
        entry_row(right, rn);
        if (sub && sn) {
            if (c_->media_dir[0] && entry_task_->pick_dir) {
                snprintf(sub, sn, "Into %s - DEL deletes, RETURN takes it, <- cancels", c_->media_dir);
            } else {
                snprintf(sub, sn, "Type the name, DEL deletes, RETURN takes it, <- cancels");
            }
        }
        return;
    }
    if (is_browser()) {
        if (entry_) {
            snprintf(left, ln, "%s", entry_act_->ask);
            entry_text(right, rn);
            if (sub && sn) {
                snprintf(sub, sn, "Hex digits, DEL deletes, RETURN starts, <- cancels");
            }
            return;
        }
        if (chooser_) {
            snprintf(left, ln, "%s", chooser_acts_[i].label);
            return;
        }
        if (i >= fm_.nlist) {
            return;
        }
        snprintf(left, ln, "%s%s", fm_.list[i].name, fm_.list[i].dir ? "/" : "");
        if (!fm_.list[i].dir) {
            snprintf(right, rn, "%u KB", (unsigned)((fm_.list[i].size + 1023) >> 10));
        }
        return;
    }
    const MenuEntry &e = tree_.e[cur().first + i];
    snprintf(left, ln, "%s", e.label);
    switch (e.kind) {
    case EK_TASK:
        break;
    case EK_MEDIA: {
        const char *w = ops_state(c_, c_->desc->media[e.index].state);
        snprintf(right, rn, "%s", w ? w : "Not set");
        *active = c_->desc->media[e.index].eject_op != 0 && w != 0;
        break;
    }
    case EK_SETTING: {
        const Setting &s = c_->desc->settings[e.index];
        int v = c_->settings->value(e.index);
        if (s.kind == 3) {                                             // a selected file: the file's name, RETURN opens the selector
            const char *t = c_->settings->text(e.index);
            const char *base = strrchr(t, '/');
            snprintf(right, rn, "%s", *t ? (base ? base + 1 : t) : "Not selected");
            if (sub && sn && *t) {
                snprintf(sub, sn, "%s", t);
            }
            break;
        }
        if (s.kind) {                                                  // a text: RETURN edits it
            const char *t = c_->settings->text(e.index);
            while (*t == ' ') {
                t++;
            }
            if (s.kind == 2 && *t) {
                int n = (int)strlen(t) < 12 ? (int)strlen(t) : 12;
                for (int k = 0; k < n; k++) {
                    right[k < rn - 1 ? k : rn - 2] = '*';
                }
                right[n < rn - 1 ? n : rn - 1] = 0;
            } else {
                snprintf(right, rn, "%s", *t ? t : (s.nvalues > 1 ? s.values[1] : "Not set"));
            }
            break;
        }
        snprintf(right, rn, "%s", s.values[v]);                          // T187 D3/D9: a setting that takes effect at the next reset has no second line; row_reset() says it, the row draws a mark
        break;
    }
    case EK_INFO:
        h_->info(e.index, right, rn);
        *active = false;
        break;
    case EK_NODE: {
        const MenuNode &n = tree_.nodes[e.index];
        if (n.kind == LN_STATE) {
            const char *w = n.state ? ops_state(c_, n.state) : 0;           // a display row (a `sub` or `chip`) names no state
            snprintf(right, rn, "%s", w ? w : "Not set");
            *active = false;
        } else if (n.kind == LN_GROUP) {
            *active = false;
        } else {
            *active = node_ready(n);
            if (!*active && sub && sn) {
                snprintf(sub, sn, "Not available now");
            }
        }
        break;
    }
    default: {                                                          // the fixed entries: what a row shows is data (FixedDef)
        const FixedDef *fd = menu_fixed(e.kind, e.index);
        if (fd && fd->value_state) {
            const char *w = ops_state(c_, fd->value_state);
            snprintf(right, rn, "%s%s", w ? w : fd->value_none, fd->value_suffix ? fd->value_suffix : "");
        }
        if (fd && fd->sub_state && sub && sn) {
            const char *l = ops_state(c_, fd->sub_state);
            if (l) {
                snprintf(sub, sn, "%s", l);
            }
        }
        break;
    }
    }
}

// T187 D9: the row i of the open list is a setting whose value takes effect at the next reset (a text or a file selector has no such mark: its second line is the value).
bool MenuCtl::row_reset(int i) const
{
    if (busy_ || wizard() || pick_ || entry_ || is_browser() || i < 0 || i >= cur().count) {
        return false;
    }
    const MenuEntry &e = tree_.e[cur().first + i];
    return e.kind == EK_SETTING && c_->desc->settings[e.index].when == WHEN_RESET && !c_->desc->settings[e.index].kind;
}

// The line that explains the mark: shown at the foot of a list that has marked rows while the status row says nothing else.
const char *MenuCtl::legend_line() const
{
    if (status_[0] || busy_ || wizard() || pick_ || entry_ || is_browser()) {
        return "";
    }
    for (int i = 0; i < cur().count; i++) {
        if (row_reset(i)) {
            return reset_legend();
        }
    }
    return "";
}

void MenuCtl::move(int d)
{
    int n = rows();
    if (!n || entry_) {
        return;
    }
    int &s = is_browser() ? (chooser_ ? chooser_sel_ : fm_.sel) : sel_[area_][tab_];
    int was = s;
    s += d;
    if (s < 0) s = 0;
    if (s >= n) s = n - 1;
    // On a screen the lines that only show a value or a heading take no focus: the next one that does is the target (a step of one; a page jumps).
    if (screen() && !is_browser() && (d == 1 || d == -1)) {
        int step = d, k = s;
        while (k >= 0 && k < n) {
            Item it;
            item(k, &it);
            if (it.kind != IK_VALUE && it.kind != IK_HEADING && it.kind != IK_INFO && !it.hidden && !it.dim) {
                s = k;
                return;
            }
            k += step;
        }
        s = was;                                                    // nothing further that takes it: stay
    }
}

// T180 D7: a page. In the F3 browser (its list, not a sheet or a text) it is the rows the list shows; elsewhere the ten rows PageUp/PageDown always moved.
// move() clamps at both ends.
void MenuCtl::page(int dir)
{
    int n = is_browser() && !chooser_ ? page_rows_ : 10;
    move(dir * (n > 0 ? n : 1));
}

void MenuCtl::begin_entry(const FileAction &a, const char *file)
{
    // the default is the word the file itself holds (a PRG's load address); a file that cannot be read starts on 0
    uint8_t w[2] = { 0, 0 };
    if (CoreFile *f = c_->fs->open(file, false)) {
        if (!f->read((uint64_t)(a.ask_off < 0 ? 0 : a.ask_off), w, 2)) {
            w[0] = w[1] = 0;
        }
        c_->fs->close(f);
    }
    entry_def_ = (uint16_t)(w[0] | (w[1] << 8));
    entry_ = true;
    entry_text_mode_ = false;
    entry_act_ = &a;
    snprintf(entry_file_, sizeof(entry_file_), "%s", file);
    entry_n_ = 0;
    entry_digits_[0] = 0;
    entry_fresh_ = true;
    set_status("Type the address in hex; RETURN takes it", false);
}

void MenuCtl::entry_text(char *out, int n) const
{
    if (entry_fresh_) {
        snprintf(out, n, "$%04X", (unsigned)entry_def_);
    } else {
        snprintf(out, n, "$%s%s", entry_digits_, entry_n_ < 4 ? "_" : "");
    }
}

static int hex_value(int k)
{
    if (k >= '0' && k <= '9') return k - '0';
    if (k >= 'a' && k <= 'f') return k - 'a' + 10;
    if (k >= 'A' && k <= 'F') return k - 'A' + 10;
    return -1;
}

// Returns true when the prompt took the key. A key that leaves it (a function key, a closing key) ends the prompt and goes on to
// the normal handling; every other key is swallowed, so a digit is never a tab number while a number is being typed.
bool MenuCtl::entry_key(int k, Result *out)
{
    if (entry_text_mode_) {
        return text_key(k, out);
    }
    int h = hex_value(k);
    if (h >= 0) {
        if (entry_fresh_) {                         // the first digit replaces the default
            entry_fresh_ = false;
            entry_n_ = 0;
        }
        if (entry_n_ < 4) {
            entry_digits_[entry_n_++] = "0123456789ABCDEF"[h];
            entry_digits_[entry_n_] = 0;
        }
        *out = R_REDRAW;
        return true;
    }
    switch (k) {
    case KEY_BACK:
        if (entry_fresh_) {
            entry_fresh_ = false;
            entry_n_ = 0;
        } else if (entry_n_ > 0) {
            entry_digits_[--entry_n_] = 0;
        }
        *out = R_REDRAW;
        return true;
    case KEY_ESCAPE:
        entry_ = false;                             // back to the chooser the prompt came from; nothing runs
        set_status("Cancelled", false);
        *out = R_REDRAW;
        return true;
    case KEY_RETURN: {
        uint32_t v = entry_def_;
        if (!entry_fresh_ && entry_n_ > 0) {        // nothing typed (or typed away again): the default
            v = (uint32_t)strtoul(entry_digits_, 0, 16);
        }
        entry_ = false;
        char file[sizeof(entry_file_)];
        snprintf(file, sizeof(file), "%s", entry_file_);
        *out = run_action(*entry_act_, file, (int32_t)v);
        return true;
    }
    case KEY_F1: case KEY_F2: case KEY_F3: case KEY_F4: case KEY_F5: case KEY_F6: case KEY_F7: case KEY_F8:
    case KEY_BREAK: case KEY_F10: case KEY_SCRLOCK: case KEY_MENU:
        entry_ = false;
        return false;                               // goes on to the normal handling
    default:
        *out = R_NONE;
        return true;
    }
}

// ---- the name prompt of a task that asks (Create, Save): the text of the prompt is edited in place, the free name is the start ------------
// A new image goes into the folder the browser is on (T122 D2). On the list of the media ("/") and inside a disk image there is no folder to write
// to: the field stays empty and the first medium takes it.
void MenuCtl::note_folder()
{
    if (strcmp(fm_.path, "/") != 0 && fm_.image_ft < 0) {
        snprintf(c_->media_dir, sizeof(c_->media_dir), "%s", fm_.path);
    } else {
        c_->media_dir[0] = 0;
    }
}

// a character of a file name: not the ones FAT, the browser or a path cannot hold
static bool name_char_ok(int k)
{
    return k >= 0x20 && k <= 0x7E && !strchr("/\\:*?\"<>|", k);
}

void MenuCtl::begin_text(const Task &t)
{
    if (!t.pick_dir) {                              // a task that picked its folder has set it (begin_pick); the others write where the menu is
        note_folder();
    }
    entry_ = true;
    entry_text_mode_ = true;
    entry_task_ = &t;
    ask_node_ = 0;
    confirm_node_ = 0;
    char sug[LE_MAX + 1];
    sug[0] = 0;
    if (t.ask_state) {
        // The suggestion is a name. Whatever a state hands over is taken only as far as it is one: a character a name cannot hold, or the end
        // of the field, ends it, so a state that returned something else shows an empty field, never garbage (T122 board 2026-10-24).
        if (const char *p = ops_state(c_, t.ask_state)) {
            int n = 0;
            while (n < 24 && p[n] && name_char_ok((unsigned char)p[n])) {
                sug[n] = p[n];
                n++;
            }
            sug[n] = 0;
        }
    }
    entry_load(sug, 24);
    set_status("Type the name; RETURN takes it, an empty name is a free one", false);
}

void MenuCtl::begin_confirm_job(const Task *t, const char *question)
{
    entry_ = true;
    entry_text_mode_ = true;
    entry_task_ = 0;
    entry_set_ = -1;
    ask_node_ = 0;
    confirm_node_ = 0;
    confirm_task_ = t;
    snprintf(confirm_q_, sizeof(confirm_q_), "%s", question);
    set_status("RETURN confirms, <- cancels", false);
}

void MenuCtl::begin_confirm(const MenuNode *n, const char *question)
{
    entry_ = true;
    entry_text_mode_ = true;
    entry_task_ = 0;
    entry_set_ = -1;
    ask_node_ = 0;
    confirm_node_ = n;
    snprintf(confirm_q_, sizeof(confirm_q_), "%s", question);
    set_status("RETURN confirms, <- cancels", false);
}

void MenuCtl::begin_ask(const MenuNode *n, const char *prompt)
{
    note_folder();
    entry_ = true;
    entry_text_mode_ = true;
    entry_task_ = 0;
    entry_set_ = -1;
    ask_node_ = n;
    snprintf(ask_q_, sizeof(ask_q_), "%s", prompt);
    char sug[LE_MAX + 1];
    sug[0] = 0;
    char st[64];
    if (ui_attr(n->attr, "ask_state", st, sizeof(st))) {
        if (const char *p = ops_state(c_, st)) {
            int k = 0;
            while (k < 24 && p[k] && name_char_ok((unsigned char)p[k])) {
                sug[k] = p[k];
                k++;
            }
            sug[k] = 0;
        }
    }
    entry_load(sug, 24);
    set_status("Type the name; RETURN takes it, <- cancels", false);
}

void MenuCtl::begin_text_setting(int idx)
{
    const Setting &s = c_->desc->settings[idx];
    entry_ = true;
    entry_text_mode_ = true;
    entry_task_ = 0;
    ask_node_ = 0;
    confirm_node_ = 0;
    entry_set_ = idx;
    const int smax = s.base > 0 && s.base <= LE_MAX ? (int)s.base : 40;
    const char *t = s.kind == 2 ? "" : c_->settings->text(idx);
    while (*t == ' ') {
        t++;
    }
    entry_load(t, smax);
    set_status("Type the text; RETURN takes it, an empty text clears it, <- cancels", false);
}

// ---- the folder browser of a Create task ----------------------------------------------------------------------------------------------------
// Where it starts: the folder the menu's browser is on, else the folder of the last file the menu mounted, else the first USB stick that can be
// read. Never /sd by default (T122 board 2026-10-24: "it simply puts them in /sd"); with no stick it starts on the list of the media.
void MenuCtl::start_path(char *out, int n) const
{
    if (strcmp(fm_.path, "/") != 0 && fm_.image_ft < 0) {
        snprintf(out, n, "%s", fm_.path);
        return;
    }
    if (fm_.last_dir[0]) {
        snprintf(out, n, "%s", fm_.last_dir);
        return;
    }
    FsEntry probe[1];
    for (int i = 0; i < 4; i++) {
        char p[8];
        snprintf(p, sizeof(p), "/usb%d", i);
        if (c_->fs->list(p, probe, 1) >= 0) {
            snprintf(out, n, "%s", p);
            return;
        }
    }
    snprintf(out, n, "/");
}

void MenuCtl::load_pick()
{
    const int up = strcmp(pick_path_, "/") != 0 ? 1 : 0;           // listed into pick_list_ itself, after the ".." row; the folders are moved to the front
    int n = c_->fs->list(pick_path_, &pick_list_[up], ROWS_MAX - up);
    pick_ok_ = n >= 0;
    pick_n_ = 0;
    if (up) {
        memset(&pick_list_[0], 0, sizeof(FsEntry));
        strcpy(pick_list_[0].name, "..");
        pick_list_[0].dir = true;
        pick_n_ = 1;
    }
    for (int i = 0; i < n; i++) {
        if (pick_list_[up + i].dir || pick_set_ >= 0) {         // the folder browser of a task lists folders only, the selector of a file everything
            if (pick_n_ != up + i) {
                pick_list_[pick_n_] = pick_list_[up + i];
            }
            pick_n_++;
        }
    }
    if (pick_sel_ > pick_n_) {
        pick_sel_ = pick_n_;
    }
}

void MenuCtl::pick_status()
{
    char s[160];
    if (pick_set_ >= 0) {
        snprintf(s, sizeof(s), "Choose the file for \"%s\"; RETURN takes it, <- goes up, ESC cancels", setting_label(pick_set_));
    } else {
        snprintf(s, sizeof(s), "Choose the folder; \"%s\" takes it, <- goes up, ESC cancels", pick_task_ && pick_task_->pick_label ? pick_task_->pick_label : "Create here");
    }
    set_status(s, false);
}

// T185 D4: a setting of kind 3 is a file the person selects. The selector opens in the folder of the file selected now (when it can still be read), else
// where the folder browser of a Create task opens; "No file" on top clears the selection.
void MenuCtl::begin_pick_file(int idx)
{
    pick_ = true;
    pick_task_ = 0;
    pick_set_ = idx;
    pick_sets_ = false;
    pick_path_[0] = 0;
    const char *cur = c_->settings->text(idx);
    const char *slash = strrchr(cur, '/');
    if (slash && slash != cur && (size_t)(slash - cur) < sizeof(pick_path_)) {
        memcpy(pick_path_, cur, (size_t)(slash - cur));
        pick_path_[slash - cur] = 0;
    } else {
        start_path(pick_path_, sizeof(pick_path_));
    }
    pick_sel_ = 0;
    load_pick();
    if (!pick_ok_ && strcmp(pick_path_, "/") != 0) {
        snprintf(pick_path_, sizeof(pick_path_), "/");
        load_pick();
    }
    pick_status();
}

void MenuCtl::begin_pick(const Task &t)
{
    pick_ = true;
    pick_task_ = &t;
    pick_set_ = -1;
    start_path(pick_path_, sizeof(pick_path_));
    pick_sel_ = 0;
    load_pick();
    if (!pick_ok_ && strcmp(pick_path_, "/") != 0) {                // the remembered folder is gone: the list of the media
        snprintf(pick_path_, sizeof(pick_path_), "/");
        load_pick();
    }
    pick_status();
}

// T180 D5: "Save media set..." asks for a name like New folder does; the list of "Load media set..." is the pick screen with the sets as its rows.
void MenuCtl::begin_set_save()
{
    entry_ = true;
    entry_text_mode_ = true;
    entry_task_ = 0;
    entry_set_ = -1;
    ask_node_ = 0;
    confirm_node_ = 0;
    entry_rename_ = false;
    entry_mset_ = true;
    entry_load("", MEDIASET_NAME_MAX);
    set_status("Type the name of the set; RETURN saves what is mounted now, <- cancels", false);
}

void MenuCtl::begin_set_load()
{
    int n = mediaset_list(c_, sets_, MEDIASETS_MAX);
    if (n < 0) {
        set_status("This core has no folder of its own, so it has no media sets", true);
        return;
    }
    if (n == 0) {
        set_status("No media set saved yet - \"Save media set...\" makes one", true);
        return;
    }
    pick_ = true;
    pick_sets_ = true;
    pick_set_ = -1;
    pick_n_ = n;
    pick_sel_ = 0;
    pick_path_[0] = 0;
    set_status("Choose the set; RETURN loads it and resets the core, <- cancels", false);
}

// The file the selector took ("" clears): the setting's operation checks it; refused, the selector stays up and the status says why; kept, it closes.
MenuCtl::Result MenuCtl::pick_take(const char *path)
{
    const Setting &s = c_->desc->settings[pick_set_];
    OpIn in = in_str("path", path);
    OpResult r;
    int rc = ops_call(c_, s.apply, &in, 1, &r);
    if (rc) {
        set_status(r.msg[0] ? r.msg : "The file was not taken", true);
        return R_REDRAW;
    }
    pick_ = false;
    pick_set_ = -1;
    set_status(r.msg, false);
    return R_REDRAW;
}

bool MenuCtl::pick_key(int k, Result *out)
{
    if (pick_sets_) {
        switch (k) {
        case KEY_UP: if (pick_sel_ > 0) pick_sel_--; *out = R_REDRAW; return true;
        case KEY_DOWN: if (pick_sel_ < pick_n_ - 1) pick_sel_++; *out = R_REDRAW; return true;
        case KEY_PAGEUP: pick_sel_ = pick_sel_ > 10 ? pick_sel_ - 10 : 0; *out = R_REDRAW; return true;
        case KEY_PAGEDOWN: pick_sel_ = pick_sel_ + 10 < pick_n_ ? pick_sel_ + 10 : pick_n_ - 1; *out = R_REDRAW; return true;
        case KEY_HOME: pick_sel_ = 0; *out = R_REDRAW; return true;
        case KEY_END: pick_sel_ = pick_n_ - 1; *out = R_REDRAW; return true;
        case KEY_RETURN:
        case KEY_SPACE: {
            pick_ = pick_sets_ = false;
            OpIn in[1] = { in_str("name", sets_[pick_sel_]) };
            OpResult r;
            int rc = ops_call(c_, menu_fixed(EK_LOAD_SET, 0)->op, in, 1, &r);
            set_status(r.msg[0] ? r.msg : (rc ? "failed" : "done"), rc != 0);
            *out = rc ? R_REDRAW : R_CLOSE;                        // the core was reset: it runs
            return true;
        }
        case KEY_LEFT:
        case KEY_ESCAPE:
            pick_ = pick_sets_ = false;
            set_status("Cancelled", false);
            *out = R_REDRAW;
            return true;
        case KEY_F1: case KEY_F2: case KEY_F3: case KEY_F4: case KEY_F5: case KEY_F6: case KEY_F7: case KEY_F8:
        case KEY_BREAK: case KEY_F10: case KEY_SCRLOCK: case KEY_MENU:
            pick_ = pick_sets_ = false;
            return false;
        default:
            *out = R_NONE;
            return true;
        }
    }
    int n = 1 + pick_n_;
    switch (k) {
    case KEY_UP: if (pick_sel_ > 0) pick_sel_--; *out = R_REDRAW; return true;
    case KEY_DOWN: if (pick_sel_ < n - 1) pick_sel_++; *out = R_REDRAW; return true;
    case KEY_PAGEUP: pick_sel_ = pick_sel_ > 10 ? pick_sel_ - 10 : 0; *out = R_REDRAW; return true;
    case KEY_PAGEDOWN: pick_sel_ = pick_sel_ + 10 < n ? pick_sel_ + 10 : n - 1; *out = R_REDRAW; return true;
    case KEY_HOME: pick_sel_ = 0; *out = R_REDRAW; return true;
    case KEY_END: pick_sel_ = n - 1; *out = R_REDRAW; return true;
    case KEY_LEFT: {
        char *slash = strrchr(pick_path_, '/');
        if (!slash || slash == pick_path_) {
            strcpy(pick_path_, "/");
        } else {
            *slash = 0;
        }
        pick_sel_ = 0;
        load_pick();
        *out = R_REDRAW;
        return true;
    }
    case KEY_RETURN:
    case KEY_SPACE:
    case KEY_RIGHT: {
        if (pick_sel_ == 0 && pick_set_ >= 0) {                      // "No file": the selection is cleared
            if (k == KEY_RIGHT) {
                *out = R_NONE;
                return true;
            }
            *out = pick_take("");
            return true;
        }
        if (pick_sel_ == 0) {
            if (k == KEY_RIGHT) {
                *out = R_NONE;
                return true;
            }
            if (strcmp(pick_path_, "/") == 0) {
                set_status("Open a medium first; the list of the media is not a folder", true);
                *out = R_REDRAW;
                return true;
            }
            snprintf(c_->media_dir, sizeof(c_->media_dir), "%s", pick_path_);
            pick_ = false;
            if (!pick_task_->ask) {                                 // a job that asks for no name (copy, move): the folder is all it needs
                *out = start_job(*pick_task_, "");
                return true;
            }
            begin_text(*pick_task_);
            *out = R_REDRAW;
            return true;
        }
        const char *name = pick_list_[pick_sel_ - 1].name;
        if (!strcmp(name, "..")) {
            return pick_key(KEY_LEFT, out);
        }
        int l = (int)strlen(pick_path_);
        if (l + 1 + (int)strlen(name) >= (int)sizeof(pick_path_)) {
            set_status("The path is too long", true);
            *out = R_REDRAW;
            return true;
        }
        if (pick_set_ >= 0 && !pick_list_[pick_sel_ - 1].dir) {      // a file: the setting's operation checks it and keeps it
            if (k == KEY_RIGHT) {
                *out = R_NONE;
                return true;
            }
            char full[200];
            snprintf(full, sizeof(full), "%s%s%s", pick_path_, l > 1 ? "/" : "", name);
            *out = pick_take(full);
            return true;
        }
        snprintf(pick_path_ + l, sizeof(pick_path_) - l, "%s%s", l > 1 ? "/" : "", name);
        pick_sel_ = 0;
        load_pick();
        pick_status();
        *out = R_REDRAW;
        return true;
    }
    case KEY_ESCAPE:
        pick_ = false;
        set_status("Cancelled", false);
        *out = R_REDRAW;
        return true;
    case KEY_F1: case KEY_F2: case KEY_F3: case KEY_F4: case KEY_F5: case KEY_F6: case KEY_F7: case KEY_F8:
    case KEY_BREAK: case KEY_F10: case KEY_SCRLOCK: case KEY_MENU:
        pick_ = false;
        return false;
    default:
        *out = R_NONE;
        return true;
    }
}

// ---- the job: a new file written in steps, with a progress display ---------------------------------------------------------------------------
MenuCtl::Result MenuCtl::start_job(const Task &t, const char *name)
{
    const char *dir = c_->media_dir;
    int dl = (int)strlen(dir);
    snprintf(job_name_, sizeof(job_name_), "%s%s%s", dir, dl && dir[dl - 1] != '/' ? "/" : "", name ? name : "");
    busy_ = true;
    job_started_ = false;
    job_task_ = &t;
    c_->job_pct = 0;
    c_->job_path[0] = 0;
    c_->job_cancel = false;
    set_status(t.op && !strcmp(t.op, "sectors.mount_folder") ? "Mounting the folder ... <- cancels" : "Building the image ... <- cancels", false);   // a folder mount builds no image (T128, discussion #36)
    return R_REDRAW;
}

void MenuCtl::job_text(char *left, int ln, char *right, int rn, char *sub, int sn) const
{
    if (c_->job_path[0]) {
        snprintf(left, ln, "Writing %s", c_->job_path);
    } else {
        snprintf(left, ln, "%s", job_task_ && job_task_->op && !strcmp(job_task_->op, "sectors.mount_folder") ? "Mounting the folder" : "Building the image");
    }
    snprintf(right, rn, "%d%s", c_->job_pct, "%");                  // the firmware's printf has no %%
    if (sub && sn) {
        char bar[24];
        int fill = c_->job_pct * 20 / 100;
        for (int i = 0; i < 20; i++) {
            bar[i] = i < fill ? '#' : '-';
        }
        bar[20] = 0;
        snprintf(sub, sn, "[%s] <- cancels", bar);
    }
}

bool MenuCtl::tick()
{
    if (!busy_) {
        return false;
    }
    if (!job_started_ && c_->job_cancel) {                          // cancelled before the first chunk: nothing exists yet
        busy_ = false;
        c_->job_cancel = false;
        set_status("Cancelled: nothing was written.", false);
        return true;
    }
    const Task &t = *job_task_;
    OpResult r;
    c_->job_step = true;
    job_started_ = true;
    int rc = ops_run_bound(c_, t.op, t.bind, t.nbind, 0, &r, 0, job_name_);
    c_->job_step = false;
    if (rc) {
        busy_ = false;
        c_->job_cancel = false;
        set_status(r.msg[0] ? r.msg : "failed", true);
        if (is_browser()) {
            fm_.load_dir();
        }
        return true;
    }
    if (r.more) {
        char s[240];
        snprintf(s, sizeof(s), "Writing %s: %d%s  (<- cancels)", c_->job_path, c_->job_pct, "%");
        set_status(s, false);
        return true;
    }
    busy_ = false;
    c_->job_cancel = false;
    c_->lib.nmarked = 0;                                            // a job used the marks up
    if (is_browser()) {
        fm_.load_dir();
    }
    char s[240];
    if (r.msg[0]) {
        snprintf(s, sizeof(s), "%s", r.msg);
    } else {
        snprintf(s, sizeof(s), "%s: done", t.label);
    }
    set_status(s, false);
    return true;
}

bool MenuCtl::text_key(int k, Result *out)
{
    if (remote_q_) {                                                  // T192.1 MVP: a confirmation a remote request asked; the screen answers it like any other
        switch (k) {
        case KEY_RETURN:
            remote_q_ = false;
            remote_ans_ = 1;
            entry_ = false;
            *out = R_REDRAW;
            return true;
        case KEY_ESCAPE:
            remote_q_ = false;
            remote_ans_ = 0;
            entry_ = false;
            set_status("Cancelled", false);
            *out = R_REDRAW;
            return true;
        case KEY_F1: case KEY_F2: case KEY_F3: case KEY_F4: case KEY_F5: case KEY_F6: case KEY_F7: case KEY_F8:
        case KEY_BREAK: case KEY_F10: case KEY_SCRLOCK: case KEY_MENU:
            remote_q_ = false;
            remote_ans_ = 0;
            entry_ = false;
            set_status("Cancelled", false);                           // the same word as <- : the person sees the pending request was answered no
            return false;
        default:
            *out = R_NONE;
            return true;
        }
    }
    if (k == KEY_RETURN && entry_hook && !confirm_node_ && !confirm_task_ && !flow_q_ && entry_text_mode_ && entry_le_.buf[0]) {
        entry_hook(entry_le_.buf);                                    // T192.1 D19d: the text the person committed goes into the session's entry history
    }
    if (flow_q_) {
        const MenuFlow *f = flow_q_;
        switch (k) {
        case KEY_RETURN:
        case KEY_ESCAPE: {
            flow_q_ = 0;
            entry_ = false;
            OpIn in[1] = { in_int("value", k == KEY_RETURN ? 1 : 0) };
            OpResult r;
            int rc = ops_call(c_, f->answer, in, 1, &r);
            set_status(r.msg, rc != 0);
            *out = R_REDRAW;
            return true;
        }
        case KEY_F1: case KEY_F2: case KEY_F3: case KEY_F4: case KEY_F5: case KEY_F6: case KEY_F7: case KEY_F8:
        case KEY_BREAK: case KEY_F10: case KEY_SCRLOCK: case KEY_MENU: {
            flow_q_ = 0;                                          // unanswered: asked again at the next open
            entry_ = false;
            OpResult r;
            ops_call(c_, f->leave, 0, 0, &r);
            return false;
        }
        default:
            *out = R_NONE;
            return true;
        }
    }
    if (confirm_task_) {
        const Task *t = confirm_task_;
        switch (k) {
        case KEY_RETURN:
            confirm_task_ = 0;
            entry_ = false;
            *out = start_job(*t, "");
            return true;
        case KEY_ESCAPE:
            confirm_task_ = 0;
            entry_ = false;
            set_status("Cancelled", false);
            *out = R_REDRAW;
            return true;
        case KEY_F1: case KEY_F2: case KEY_F3: case KEY_F4: case KEY_F5: case KEY_F6: case KEY_F7: case KEY_F8:
        case KEY_BREAK: case KEY_F10: case KEY_SCRLOCK: case KEY_MENU:
            confirm_task_ = 0;
            entry_ = false;
            return false;
        default:
            *out = R_NONE;
            return true;
        }
    }
    if (confirm_node_) {
        const MenuNode *n = confirm_node_;
        switch (k) {
        case KEY_RETURN: {
            confirm_node_ = 0;
            entry_ = false;
            OpResult r;
            int rc = ops_run_bound(c_, n->op, n->bind, n->nbind, 0, &r);
            if (rc) {
                set_status(r.msg[0] ? r.msg : "failed", true);
                *out = R_REDRAW;
            } else {
                set_status(r.msg[0] ? r.msg : "done", false);
                *out = n->closes_menu ? R_CLOSE : R_REDRAW;
            }
            return true;
        }
        case KEY_ESCAPE:
            confirm_node_ = 0;
            entry_ = false;
            set_status("Cancelled", false);
            *out = R_REDRAW;
            return true;
        case KEY_F1: case KEY_F2: case KEY_F3: case KEY_F4: case KEY_F5: case KEY_F6: case KEY_F7: case KEY_F8:
        case KEY_BREAK: case KEY_F10: case KEY_SCRLOCK: case KEY_MENU:
            confirm_node_ = 0;
            entry_ = false;
            return false;
        default:
            *out = R_NONE;
            return true;
        }
    }
    if (entry_rename_ && k == KEY_RETURN) {
        entry_rename_ = false;
        entry_ = false;
        char text[LE_MAX + 1];
        snprintf(text, sizeof(text), "%s", entry_le_.buf);
        int l = (int)strlen(text);
        while (l > 0 && text[l - 1] == ' ') {
            text[--l] = 0;
        }
        fm_.do_rename(rename_path_, text);                           // a new file name, or (no path) a new folder; the Library lists the folder again
        *out = R_REDRAW;
        return true;
    }
    if (entry_rename_ && k == KEY_ESCAPE) {
        entry_rename_ = false;
    }
    if (entry_mset_ && k == KEY_RETURN) {
        entry_mset_ = false;
        entry_ = false;
        char text[LE_MAX + 1];
        snprintf(text, sizeof(text), "%s", entry_le_.buf);
        int l = (int)strlen(text);
        while (l > 0 && text[l - 1] == ' ') {
            text[--l] = 0;
        }
        OpIn in[1] = { in_str("name", text) };
        OpResult r;
        int rc = ops_call(c_, menu_fixed(EK_SAVE_SET, 0)->op, in, 1, &r);
        set_status(r.msg, rc != 0);
        *out = R_REDRAW;
        return true;
    }
    if (entry_mset_ && k == KEY_ESCAPE) {
        entry_mset_ = false;
    }
    if (search_edit_ && k == KEY_RETURN) {
        search_edit_ = false;
        entry_ = false;
        snprintf(fm_.search_q, sizeof(fm_.search_q), "%s", entry_le_.buf);
        fm_.run_search();
        *out = R_REDRAW;
        return true;
    }
    if (search_edit_ && k == KEY_ESCAPE) {
        search_edit_ = false;
    }
    if (ask_node_ && k == KEY_RETURN) {
        const MenuNode *n = ask_node_;
        ask_node_ = 0;
        entry_ = false;
        char text[LE_MAX + 1];
        snprintf(text, sizeof(text), "%s", entry_le_.buf);
        int l = (int)strlen(text);
        while (l > 0 && text[l - 1] == ' ') {
            text[--l] = 0;
        }
        OpResult r;
        int rc = ops_run_bound(c_, n->op, n->bind, n->nbind, 0, &r, 0, text);
        set_status(r.msg[0] ? r.msg : (rc ? "failed" : "done"), rc != 0);
        *out = rc ? R_REDRAW : (n->closes_menu ? R_CLOSE : R_REDRAW);
        return true;
    }
    if (ask_node_ && k == KEY_ESCAPE) {
        ask_node_ = 0;
    }
    switch (k) {
    case KEY_ESCAPE:
        entry_ = false;
        entry_set_ = -1;
        set_status("Cancelled", false);
        *out = R_REDRAW;
        return true;
    case KEY_RETURN: {
        entry_ = false;
        char text[LE_MAX + 1];
        snprintf(text, sizeof(text), "%s", entry_le_.buf);
        if (entry_set_ >= 0) {                                       // a text setting: the same operation a REST client calls
            OpResult r;
            int rc = settings_set(c_, c_->desc->settings[entry_set_].key, text[0] ? text : " ", &r);
            entry_set_ = -1;
            set_status(r.msg, rc != 0);
            return *out = R_REDRAW, true;
        }
        int l = (int)strlen(text);
        while (l > 0 && text[l - 1] == ' ') {                       // no spaces at the end of a name
            text[--l] = 0;
        }
        const char *t = text;
        while (*t == ' ') {
            t++;
        }
        *out = run_task(*entry_task_, t);
        return true;
    }
    case KEY_F1: case KEY_F2: case KEY_F3: case KEY_F4: case KEY_F5: case KEY_F6: case KEY_F7: case KEY_F8:
    case KEY_BREAK: case KEY_F10: case KEY_SCRLOCK: case KEY_MENU:
        entry_ = false;
        entry_set_ = -1;
        entry_mset_ = false;
        return false;
    default: {
        // T180 D8: the one line editor (app/edit) takes every other key of a text: characters go in at the cursor, DEL (INST/DEL) takes the one before
        // it, CRSR left / right, HOME and END (CLR = SHIFT+HOME) move it. RETURN and <- / ESC were taken above: no editing key takes or cancels.
        // The search and a setting's text take any printable character, the others (a file or folder name, a task's name) not the ones FAT or a path cannot hold.
        const bool any = search_edit_ || (entry_set_ >= 0 && !entry_rename_ && !entry_mset_);
        entry_le_.reject = any ? 0 : "/\\:*?\"<>|";
        const int cur0 = entry_le_.cur, len0 = entry_le_.len;
        le_key(&entry_le_, k);
        *out = (entry_le_.cur != cur0 || entry_le_.len != len0) ? R_REDRAW : R_NONE;
        return true;
    }
    }
}

MenuCtl::Result MenuCtl::run_task(const Task &t, const char *text)
{
    OpResult r;
    if (t.ask && t.pick_dir) {
        return start_job(t, text);
    }
    if (t.ask) {
        note_folder();
    }
    int rc = ops_run_bound(c_, t.op, t.bind, t.nbind, 0, &r, 0, text);
    if (rc) {
        set_status(r.msg[0] ? r.msg : "failed", true);
        return R_REDRAW;
    }
    if (wizard()) {                                                 // the task opened a wizard: its screen is the answer, the status line starts empty
        set_status("", false);
        return R_REDRAW;
    }
    char s[240];
    if (t.ask && r.msg[0]) {
        snprintf(s, sizeof(s), "%s", r.msg);
    } else {
        snprintf(s, sizeof(s), "%s: done", t.label);
    }
    set_status(s, false);
    return t.closes_menu ? R_CLOSE : R_REDRAW;
}


// An action that asks for an address shows the prompt first; one that does not runs at once.
MenuCtl::Result MenuCtl::start_action(const FileAction &a, const char *file)
{
    if (a.ask) {
        begin_entry(a, file);
        return R_REDRAW;
    }
    return run_action(a, file);
}

MenuCtl::Result MenuCtl::run_action(const FileAction &a, const char *file, int32_t ask)
{
    if (a.op && !strncmp(a.op, "ui.", 3)) {
        chooser_ = false;
        return ui_action(a.op, file);
    }
    if (const MenuVList *vl = menu_vlist(a.op)) {                       // the file is a list for the browser (a tape image's entries)
        chooser_ = false;
        if (!fm_.open_vlist(vl, file)) {
            return R_REDRAW;
        }
        return R_REDRAW;
    }
    if (fm_.vmode == 4) {
        ask = fm_.vl_index;                                               // the entry the sheet was opened on goes to the bound index
    }
    if (!strcmp(a.op, "menu.browse")) {
        chooser_ = false;
        fm_.browse_image(file);
        return R_REDRAW;
    }
    OpResult r;
    int rc = ops_run_bound(c_, a.op, a.bind, a.nbind, file, &r, ask);
    if (rc) {
        set_status(r.msg[0] ? r.msg : "failed", true);
        return R_REDRAW;
    }
    const char *slash = strrchr(file, '/');
    {   // the folder of the last file the menu mounted or ran: where a Create task's folder browser starts when the menu has no folder of its own
        const char *src = fm_.image_ft >= 0 ? fm_.path : file;      // inside a disk image: the image's own folder
        const char *sl = strrchr(src, '/');
        if (sl && sl != src && (size_t)(sl - src) < sizeof(fm_.last_dir)) {
            memcpy(fm_.last_dir, src, (size_t)(sl - src));
            fm_.last_dir[sl - src] = 0;
        }
    }
    char s[200];
    snprintf(s, sizeof(s), "%s: %s", a.label, slash ? slash + 1 : file);
    set_status(s, false);
    chooser_ = false;
    return menu_action_closes(c_, a) ? R_CLOSE : R_REDRAW;
}

MenuCtl::Result MenuCtl::change_setting(int dir)
{
    const MenuEntry &e = tree_.e[cur().first + sel_[area_][tab_]];
    if (e.kind != EK_SETTING) {
        return R_NONE;
    }
    const Setting &s = c_->desc->settings[e.index];
    int v = (c_->settings->value(e.index) + dir + s.nvalues) % s.nvalues;
    // the same operation a REST or WS client calls (T104.1 D3)
    OpResult r;
    int rc = settings_set(c_, s.key, s.values[v], &r);
    set_status(r.msg, rc != 0);
    return R_REDRAW;
}

bool MenuCtl::pump(bool quiet)
{
    int nk = quiet ? 0 : 1;
    bool changed = false;
    if (nk == 0 && wizard() && map_tick()) {                         // a wizard's next press (the pad mapping)
        nk++;
        changed = true;
    }
    if (nk == 0 && busy_) {                                          // a file being written (T122): one chunk per pass, so the percentage on screen moves and the core lock is taken one chunk at a time
        uint32_t ts = c_->io->now_ms();
        bool ch = tick();
        printf("[core] create step: %d percent, %u ms\n", progress(), (unsigned)(c_->io->now_ms() - ts));
        if (ch) {
            nk++;
            changed = true;
        }
    }
    uint32_t now = c_->io->now_ms();
    char ln[24];
    ui_name(cur().layout, ln, sizeof(ln));
    if (!nk && menu_layout_tick(ln)) {                               // the monitor's stops, finished run-tos and live log
        OpResult r;
        r.more = false;
        ops_call_direct(c_, menu_layout_tick(ln), 0, 0, &r);
        if (r.more) {
            nk++;
            changed = true;
        }
    }
    if (toast_expire()) {                                            // T160: the volume line ran out; the screen takes it away
        nk++;
        changed = true;
    }
    if (!nk && c_->desc->live_ms && now - last_live_ >= c_->desc->live_ms) {      // T175: state rows that show a register (the pins of the ports)
        last_live_ = now;
        nk++;
        changed = true;
    }
    if (!nk && now - last_relist_ >= 2000) {                         // media that mounted after the menu opened
        last_relist_ = now;
        if (relist()) {
            nk++;
            changed = true;
        }
    }
    return changed;
}

// T160 (board 2026-10-31): a short line over whatever page is open. The screen shows it from toast_text() until toast_expire() takes it away.
void MenuCtl::toast(const char *s)
{
    snprintf(toast_, sizeof(toast_), "%s", s);
    toast_until_ = c_->io->now_ms() + TOAST_MS;
    toast_rev_++;
}

bool MenuCtl::toast_expire()
{
    if (toast_[0] && (int32_t)(c_->io->now_ms() - toast_until_) >= 0) {
        toast_[0] = 0;
        toast_rev_++;
        return true;
    }
    return false;
}

MenuCtl::Result MenuCtl::activate(bool primary, bool all)
{
    if (is_browser()) {
        if (chooser_) {
            return start_action(chooser_acts_[chooser_sel_], chooser_file_);
        }
        if (search_tab() && !fm_.vmode) {                              // nothing found yet: RETURN asks for the query
            begin_search_edit();
            return R_REDRAW;
        }
        if (fm_.sel >= fm_.nlist) {
            return R_NONE;
        }
        const FsEntry &f = fm_.list[fm_.sel];
        char full[FS_PATH_MAX];
        fm_.full_path(fm_.sel, full, sizeof(full));
        bool up = !strcmp(f.name, "..");
        bool plain = cur().id && !strcmp(cur().id, "browse");               // the Library's own tab: not the online listing, not a search
        if (f.dir && primary && fm_.image_ft < 0 && f.name[0] != '[' && (!up || plain)) {      // RIGHT on a folder: its sheet; on ".." the sheet of the folder the browser is in (GitHub #42: a folder without a subfolder had none)
            int n = 0, nuni, nitems;
            const FileAction *uni = FileMgr::universal(&nuni);
            const FileAction *items = FileMgr::folder_items(&nitems);
            if (!up) {
                chooser_buf_[n++] = *FileMgr::folder_open();
                for (int k = 2; k < nuni; k++) {
                    chooser_buf_[n++] = uni[k];
                }
            }
            for (int k = 0; k < nitems; k++) {
                if (k == 1 && !c_->lib.nclip) {
                    continue;
                }
                chooser_buf_[n++] = items[k];
            }
            chooser_ = true;
            chooser_acts_ = chooser_buf_;
            chooser_n_ = n;
            chooser_ft_ = 0;
            chooser_sel_ = 0;
            folder_sheet_ = true;
            snprintf(chooser_file_, sizeof(chooser_file_), "%s", up ? fm_.path : full);
            return R_REDRAW;
        }
        if (f.dir) {
            if (fm_.vmode && f.name[0] != '.' ) {                      // a folder in a list: the Library goes there
                if (strlen(full) >= sizeof(fm_.path)) {
                    set_status("The path is too long", true);
                    return R_REDRAW;
                }
                snprintf(fm_.path, sizeof(fm_.path), "%s", full);
                fm_.vmode = 0;
                fm_.sel = 0;
                fm_.load_dir();
                return R_REDRAW;
            }
            fm_.enter_dir(f.name);
            return R_REDRAW;
        }
        if (fm_.vmode == 4 && fm_.vlist) {                                     // an entry of a list the browser shows for a file (a tape image): its actions
            fm_.vl_index = fm_.sel;
            chooser_ = true;
            folder_sheet_ = false;
            chooser_acts_ = fm_.vlist->acts;
            chooser_n_ = fm_.vlist->nacts;
            chooser_sel_ = 0;
            if (primary) {
                return run_action(fm_.vlist->acts[0], fm_.vl_path);
            }
            snprintf(chooser_file_, sizeof(chooser_file_), "%s", fm_.vl_path);
            return R_REDRAW;
        }
        const FileType *ft = 0;
        const FileAction *acts = 0;
        int nacts = 0;
        if (fm_.image_ft >= 0) {                                      // a file inside a disk image: the image's actions
            ft = &c_->desc->filetypes[fm_.image_ft];
            acts = ft->inner;
            nacts = ft->ninner;
        } else if (fm_.visible_ext(f.name, &ft)) {
            acts = ft->actions;
            nacts = ft->nactions;
            // T104.1 D4 size_in: an action a library contributed for files of certain sizes is offered for those files only
            bool sized = false;
            for (int k = 0; k < nacts; k++) {
                sized |= acts[k].nsize_in > 0;
            }
            if (sized) {
                int w = 0;
                for (int k = 0; k < nacts && w < FILT_MAX; k++) {
                    bool ok = acts[k].nsize_in == 0;
                    for (int z = 0; z < acts[k].nsize_in; z++) {
                        ok |= (uint64_t)f.size == acts[k].size_in[z];
                    }
                    if (ok) {
                        filt_[w++] = acts[k];
                    }
                }
                acts = filt_;
                nacts = w;
            }
        } else {
            ft = 0;
        }
        if (fm_.image_ft >= 0) {
            if (nacts == 1 || primary) {
                return start_action(acts[0], full);
            }
        } else if (!all && nacts >= 1 && primary) {
            return start_action(acts[0], full);
        } else if (!all && nacts == 0 && primary) {
            return ui_action("ui.view", full);                     // a file no action knows: RIGHT shows it
        }
        int n = 0;
        for (int k = 0; k < nacts && n < 30; k++) {
            chooser_buf_[n++] = acts[k];
        }
        if (fm_.image_ft < 0) {
            int nuni;
            const FileAction *uni = FileMgr::universal(&nuni);
            for (int k = 0; k < nuni; k++) {
                chooser_buf_[n++] = uni[k];
            }
        }
        chooser_ = true;
        folder_sheet_ = false;
        chooser_acts_ = chooser_buf_;
        chooser_n_ = n;
        chooser_ft_ = ft ? (int)(ft - c_->desc->filetypes) : 0;
        chooser_sel_ = 0;
        snprintf(chooser_file_, sizeof(chooser_file_), "%s", full);
        return R_REDRAW;
    }
    const MenuEntry &e = tree_.e[cur().first + sel_[area_][tab_]];
    char msg[240];
    msg[0] = 0;
    switch (e.kind) {
    case EK_TASK: {
        const Task &t = c_->desc->tasks[e.index];
        if (t.ask && t.pick_dir) {
            begin_pick(t);
            return R_REDRAW;
        }
        if (t.ask) {
            begin_text(t);
            return R_REDRAW;
        }
        return run_task(t, 0);
    }
    case EK_MEDIA: {
        const MediaSlot &m = c_->desc->media[e.index];
        if (m.eject_op && ops_state(c_, m.state)) {
            OpResult r;
            int rc = ops_run_bound(c_, m.eject_op, m.eject_bind, m.neject_bind, 0, &r);
            char s[120];
            snprintf(s, sizeof(s), rc ? "%s: cannot eject. %s" : "%s ejected", m.name, r.msg);
            set_status(s, rc != 0);
        }
        return R_REDRAW;
    }
    case EK_SETTING:
        if (c_->desc->settings[e.index].kind == 3) {
            begin_pick_file(e.index);
            return R_REDRAW;
        }
        if (c_->desc->settings[e.index].kind) {
            begin_text_setting(e.index);
            return R_REDRAW;
        }
        return change_setting(1);
    case EK_SAVE_SET:
        begin_set_save();
        return R_REDRAW;
    case EK_LOAD_SET:
        begin_set_load();
        return R_REDRAW;
    case EK_NODE:
        return run_node(tree_.nodes[e.index], e.label);
    case EK_BROWSER:
    case EK_INFO:
        return R_NONE;
    default:
        return run_fixed(e);
    }
    return R_NONE;
}

// A fixed entry (menutree.cc FixedDef): the operation it names, the way its answer is told; "Return to host menu" first has the media written back and then asks the host.
MenuCtl::Result MenuCtl::run_fixed(const MenuEntry &e)
{
    const FixedDef *fd = menu_fixed(e.kind, e.index);
    if (!fd) {
        return R_NONE;
    }
    char msg[240];
    OpResult r;
    msg[0] = 0;
    if (fd->pre_op) {
        if (ops_call(c_, fd->pre_op, 0, 0, &r)) {
            set_status(r.msg, true);
            return R_REDRAW;
        }
    }
    if (!fd->op) {                                                   // the way back to the host: the host's own call
        if (!h_->return_to_host(msg, sizeof(msg))) {
            set_status(msg, true);
        }
        return R_REDRAW;
    }
    int rc = ops_call(c_, fd->op, 0, 0, &r);
    if (fd->ok_text) {
        if (rc) {
            set_status(r.msg[0] ? r.msg : fd->fail_text, true);
            return R_REDRAW;
        }
        set_status(fd->ok_text, false);
        return fd->closes ? R_CLOSE : R_REDRAW;
    }
    set_status(r.msg, rc != 0);
    return R_REDRAW;
}

// What a node does when it is activated: by RETURN on its row, or by its hotkey (ui `key`) from any screen.
MenuCtl::Result MenuCtl::run_node(const MenuNode &n, const char *label)
{
        char arg[96];
        if (n.kind == LN_OP && ui_attr(n.attr, "off", arg, sizeof(arg))) {        // greyed with its reason: RETURN says why
            set_status(arg, false);
            return R_REDRAW;
        }
        if (n.kind == LN_OP && ui_attr(n.attr, "goto", arg, sizeof(arg))) {
            if (n.op && n.op[0] && node_ready(n)) {                                 // a node that has an operation runs it on the way (a bookmark of the monitor)
                OpResult gr;
                ops_run_bound(c_, n.op, n.bind, n.nbind, 0, &gr);
                fm_.apply_pending();
            }
            return goto_spec(arg) ? R_REDRAW : R_NONE;
        }
        if (n.kind == LN_OP && ui_attr(n.attr, "view", arg, sizeof(arg))) {         // the viewer on something the application keeps in memory
            static const char *const kinds[] = { "log", "edid", "trace", "info", "scan", "help" };
            for (int k = 0; k < 6; k++) {
                if (!strcmp(arg, kinds[k])) {
                    fm_.start_view_mem(label, k, k == 1);
                    return R_REDRAW;
                }
            }
        }
        if (n.kind != LN_OP || !node_ready(n)) {
            return R_NONE;
        }
        char q[120];
        if (ui_attr(n.attr, "rename")) {
            fm_.lib_sync();
            if (!c_->lib.cur[0]) {
                set_status("Nothing under the cursor: open the Browse tab and pick a file", true);
                return R_REDRAW;
            }
            begin_rename(c_->lib.cur);
            return R_REDRAW;
        }
        if (ui_attr(n.attr, "job") || ui_attr(n.attr, "pick")) {
            fm_.lib_sync();
            memset(&node_task_, 0, sizeof(node_task_));
            node_task_.label = label;
            node_task_.op = n.op;
            node_task_.bind = n.bind;
            node_task_.nbind = n.nbind;
            char pl[40];
            if (ui_attr(n.attr, "pick", pl, sizeof(pl))) {
                static char pick_text[40];
                snprintf(pick_text, sizeof(pick_text), "%s", pl);
                node_task_.pick_dir = true;
                node_task_.pick_label = pick_text;
                begin_pick(node_task_);
                return R_REDRAW;
            }
            if (ui_attr(n.attr, "confirm", q, sizeof(q))) {
                begin_confirm_job(&node_task_, q);
                return R_REDRAW;
            }
            return start_job(node_task_, "");
        }
        if (ui_attr(n.attr, "ask", q, sizeof(q))) {
            begin_ask(&n, q);
            return R_REDRAW;
        }
        if (ui_attr(n.attr, "confirm", q, sizeof(q))) {
            begin_confirm(&n, q);
            return R_REDRAW;
        }
        OpResult r;
        int rc = ops_run_bound(c_, n.op, n.bind, n.nbind, 0, &r);
        if (rc) {
            set_status(r.msg[0] ? r.msg : "failed", true);
            return R_REDRAW;
        }
        char s[240];
        snprintf(s, sizeof(s), "%s", r.msg[0] ? r.msg : label);
        set_status(s, false);
        fm_.apply_pending();
        return n.closes_menu ? R_CLOSE : R_REDRAW;
}

// A node's own letter (ui `key`): E eject, X remove the cartridge, P pause, R reset ... Each core's TOML says which; nothing here knows a letter. A node that cannot work now says why.
MenuCtl::Result MenuCtl::hotkey(int k)
{
    char u = (char)toupper(k);
    // A letter declared on an element of this page (`bkey` of a button, `key` of a row) wins; a hotkey of the core (`key` anywhere) acts only where the page claims no such letter.
    for (int pass = 0; pass < 2; pass++) {
        int from = pass == 0 ? cur().first : 0, to = pass == 0 ? cur().first + cur().count : tree_.n;
        for (int i = from; i < to && i < tree_.n; i++) {
            const MenuEntry &e = tree_.e[i];
            if (e.kind != EK_NODE) {
                continue;
            }
            const MenuNode &n = tree_.nodes[e.index];
            char kb[8];
            bool own = pass == 0 && ui_attr(n.attr, "bkey", kb, sizeof(kb)) && kb[0] == u;
            if (!own) {
                own = ui_attr(n.attr, "key", kb, sizeof(kb)) && kb[0] == u && !(kb[0] == 'F' && kb[1] >= '1' && kb[1] <= '8');
            }
            if (!own) {
                continue;
            }
            char why[120];
            if (ui_attr(n.attr, "off", why, sizeof(why))) {
                set_status(why, false);
                return R_REDRAW;
            }
            if (n.kind == LN_OP && !node_ready(n)) {
                set_status(ui_attr(n.attr, "why", why, sizeof(why)) ? why : "Not available now", false);
                return R_REDRAW;
            }
            return run_node(n, e.label);
        }
    }
    return R_NONE;
}

// The line at the foot: in the Library the keys of the row under the cursor, elsewhere the keys of the screen and the letters of the core.
void MenuCtl::footer_legend(char *out, int n)
{
    if (confirm_node_ || confirm_task_ || flow_q_ || remote_q_) {
        snprintf(out, n, "RETURN  YES   <-  NO");
        return;
    }
    if (entry_) {                                                      // a text has the keys: no hotkey of the menu runs here
        snprintf(out, n, "TYPE   DEL  DELETE   RETURN  OK   <-  CANCEL");
        return;
    }
    if (pick_ && pick_sets_) {
        snprintf(out, n, "UP/DOWN  CHOOSE   RETURN  LOAD AND RESET   <-  CANCEL");
        return;
    }
    if (pick_) {
        snprintf(out, n, "UP/DOWN  CHOOSE   RETURN  OPEN OR TAKE   <-  UP OR CANCEL");
        return;
    }
    if (busy_) {
        snprintf(out, n, "<-  CANCEL");
        return;
    }
    if (app_mode_) {                                                  // an application's screens: its tabs, the keys it binds, and whether RUN/STOP closes
        int nmax = 0, nvis = 0;
        for (int b = 0; b < tree_.a[area_].ntabs; b++) {
            if (!tab_is_hidden(area_, b)) {
                nvis++;
                if (tree_.a[area_].tabs[b].n > nmax) {
                    nmax = tree_.a[area_].tabs[b].n;
                }
            }
        }
        int p = 0;
        if (layout_is("terminal")) {
            p = snprintf(out, n, "EVERY KEY IS THE MONITOR'S");
        } else {
            p = snprintf(out, n, nvis > 1 ? "1-%d TAB  RETURN  SELECT  <- BACK" : "RETURN  SELECT  <- BACK", nmax);
        }
        for (int i = 0; i < cur().count && p < n - 20; i++) {          // the function keys of the open tab: "F3 DUMP"
            const MenuEntry &e = tree_.e[cur().first + i];
            char kb[8], lab[24];
            if (e.kind != EK_NODE || !ui_attr(tree_.nodes[e.index].attr, "key", kb, sizeof(kb)) || kb[0] != 'F') {
                continue;
            }
            if (!ui_attr(tree_.nodes[e.index].attr, "legend", lab, sizeof(lab))) {
                snprintf(lab, sizeof(lab), "%s", e.label);
                for (char *q = lab; *q; q++) {
                    if (*q == ' ') {
                        *q = 0;
                        break;
                    }
                }
            }
            for (char *q = lab; *q; q++) {
                if (*q >= 'a' && *q <= 'z') {
                    *q = (char)(*q - 32);
                }
            }
            p += snprintf(out + p, n - p, "  %s %s", kb, lab);
        }
        char why[100];
        snprintf(out + p, n - p, "  %s", locked(why, sizeof(why)) ? "R/S LOCKED" : "R/S CLOSE");
        return;
    }
    if (is_browser() && screen() && !busy_ && !fm_.view) {
        if (chooser_) {
            snprintf(out, n, "LETTER  RUN THE ITEM   RETURN  RUN   <-  CLOSE THE SHEET");
        } else if (search_tab() && !fm_.vmode) {
            snprintf(out, n, "RETURN  TYPE A NAME   1-3  TAB   <- UP");
        } else if (fm_.seek_on) {
            snprintf(out, n, "TYPE THE NAME   ? ANY CHARACTER   RETURN OR <-  END");
        } else if (media_level()) {                                    // as the host's File Browser at "/": the keys that do something
            snprintf(out, n, "UP/DOWN  SELECT   RETURN  OPEN IT   / SEEK");
        } else if (fm_.sel < fm_.nlist && fm_.list[fm_.sel].dir) {
            snprintf(out, n, "RETURN  OPEN   RIGHT  FOLDER ACTIONS   SPACE  MARK   <- UP   / SEEK");
        } else if (fm_.sel < fm_.nlist) {
            BrowseRow r;
            browse_row(fm_.sel, &r);
            snprintf(out, n, "RETURN  ACTIONS   RIGHT  %s   SPACE  MARK   <- UP   / SEEK", r.primary[0] ? r.primary : "VIEW");
            for (char *p = out; *p; p++) {
                if (*p >= 'a' && *p <= 'z') {
                    *p = (char)(*p - 32);
                }
            }
        } else {
            snprintf(out, n, "<- UP   / SEEK   F3  AREA");
        }
        return;
    }
    int nmax = 1;
    const MenuArea &ar = tree_.a[area_];
    for (int b = 0; b < ar.ntabs; b++) {
        if (ar.tabs[b].n > nmax) {
            nmax = ar.tabs[b].n;
        }
    }
    int p = snprintf(out, n, nmax > 1 ? "F1-F8 AREA  1-%d TAB  RETURN  <- BACK" : "F1-F8 AREA  RETURN  <- BACK", nmax);
    // the letters of this page's buttons first, then the core's own that this page does not claim, as far as the line holds (about 92 characters)
    for (int i = cur().first; i < cur().first + cur().count && i < tree_.n && p < n - 12; i++) {
        const MenuEntry &e = tree_.e[i];
        char kb[8];
        if (e.kind == EK_NODE && ui_attr(tree_.nodes[e.index].attr, "bkey", kb, sizeof(kb)) && tree_.nodes[e.index].kind == LN_OP) {
            char lab[20];
            int j = 0;
            for (const char *q = e.label; *q && j < 15; q++) {                // the button's own words: what the letter does on this page
                lab[j++] = (*q >= 'a' && *q <= 'z') ? (char)(*q - 32) : *q;
            }
            lab[j] = 0;
            p += snprintf(out + p, n - p, "  %c %s", kb[0], lab);
        }
    }
    for (int i = 0; i < tree_.n && p < n - 12 && p < 84; i++) {      // the letters of this core
        const MenuEntry &e = tree_.e[i];
        char kb[8];
        if (e.kind == EK_NODE && ui_attr(tree_.nodes[e.index].attr, "key", kb, sizeof(kb))) {
            bool claimed = false;
            for (int j2 = cur().first; j2 < cur().first + cur().count && j2 < tree_.n; j2++) {
                char bk[8];
                if (tree_.e[j2].kind == EK_NODE && ui_attr(tree_.nodes[tree_.e[j2].index].attr, "bkey", bk, sizeof(bk)) && bk[0] == kb[0]) {
                    claimed = true;
                }
            }
            if (claimed) {
                continue;
            }
            char lab[12];
            int j = 0;
            const char *w = e.label;
            if (!strncasecmp(w, "Open ", 5)) {
                w += 5;
            }
            for (const char *q = w; *q && *q != ' ' && j < 9; q++) {
                lab[j++] = (*q >= 'a' && *q <= 'z') ? (char)(*q - 32) : *q;
            }
            lab[j] = 0;
            p += snprintf(out + p, n - p, "  %c %s", kb[0], lab);
        }
    }
}

// ---- the screens of an application (T104.3 D0, apphost.cc) ---------------------------------------------------------------------------------
// The tab's layout parameter named `key` as a state value's text ("lock", "follow": the value is a state, with or without the @), else "".
bool MenuCtl::tab_spec(const char *key, char *out, int n) const
{
    char spec[96];
    out[0] = 0;
    if (!layout_param(key, spec, sizeof(spec))) {
        return false;
    }
    char at[100];
    snprintf(at, sizeof(at), "%s%s", spec[0] == '@' ? "" : "@", spec);
    spec_text(at, out, n);
    return out[0] != 0;
}

// Does the application let the screens close now? Else the reason (the tab's `lock` state holds it).
bool MenuCtl::locked(char *why, int n) const
{
    return app_mode_ && tab_spec("lock", why, n);
}

bool MenuCtl::tab_is_hidden(int area, int tab) const
{
    if (area < 0 || area >= tree_.nareas || tab < 0 || tab >= tree_.a[area].ntabs) {
        return false;
    }
    const char *l = tree_.a[area].tabs[tab].layout;
    const char *w = l ? strchr(l, ';') : 0;
    return w && ui_attr(w + 1, "hidden");
}

bool MenuCtl::goto_tab_id(const char *id)
{
    for (int b = 0; b < tree_.a[area_].ntabs; b++) {
        if (tree_.a[area_].tabs[b].id && !strcmp(tree_.a[area_].tabs[b].id, id)) {
            if (b != tab_) {
                go(area_, b);
            }
            return true;
        }
    }
    return false;
}

// The first tab's `follow`: a state value whose text is the id of the tab the application wants shown ("progress", "result#2" - what follows # only makes
// the value new). A value that changed moves the screens there.
bool MenuCtl::follow_tick()
{
    if (!app_mode_ || tree_.nareas < 1 || tree_.a[0].ntabs < 1) {
        return false;
    }
    const char *l = tree_.a[0].tabs[0].layout;
    const char *w = l ? strchr(l, ';') : 0;
    char spec[96], v[40];
    if (!w || !ui_attr(w + 1, "follow", spec, sizeof(spec))) {
        return false;
    }
    char at[100];
    snprintf(at, sizeof(at), "%s%s", spec[0] == '@' ? "" : "@", spec);
    spec_text(at, v, sizeof(v));
    if (!v[0] || !strcmp(v, follow_last_)) {
        return false;
    }
    snprintf(follow_last_, sizeof(follow_last_), "%s", v);
    char *hash = strchr(v, '#');
    if (hash) {
        *hash = 0;
    }
    if (!strcmp(v, "close")) {                                         // the application asks for its screens to be closed (the monitor's `x`)
        close_req_ = true;
        return true;
    }
    return goto_tab_id(v);
}

// The operation the open tab names as `enter`: the application is told its screen is shown (it initialises the tool behind it, once).
void MenuCtl::app_start()
{
    char eop[48];
    if (app_mode_ && layout_param("enter", eop, sizeof(eop))) {
        OpResult r;
        ops_call(c_, eop, 0, 0, &r);
    }
}

// The node of the open tab whose ui `key` is this function key ("F3"): runs it, or says why it cannot.
MenuCtl::Result MenuCtl::fkey_node(const char *name)
{
    for (int i = 0; i < cur().count; i++) {
        const MenuEntry &e = tree_.e[cur().first + i];
        if (e.kind != EK_NODE) {
            continue;
        }
        const MenuNode &n = tree_.nodes[e.index];
        char kb[8];
        if (!ui_attr(n.attr, "key", kb, sizeof(kb)) || strcasecmp(kb, name)) {
            continue;
        }
        char why[120];
        if (ui_attr(n.attr, "off", why, sizeof(why))) {
            set_status(why, false);
            return R_REDRAW;
        }
        if (n.kind == LN_OP && !node_ready(n)) {
            set_status(ui_attr(n.attr, "why", why, sizeof(why)) ? why : "Not available now", false);
            return R_REDRAW;
        }
        return run_node(n, e.label);
    }
    return R_NONE;
}

// ---- the screens -------------------------------------------------------------------------------------------------------------------------
bool MenuCtl::screen() const
{
    if (fm_.view) {
        return true;
    }
    return cur().layout && cur().layout[0] && !busy_ && !pick_ && !entry_;
}

const char *MenuCtl::core_title() const
{
    return c_->desc->short_title ? c_->desc->short_title : c_->desc->cor_name;
}

bool MenuCtl::layout_is(const char *name) const
{
    char n[24];
    ui_name(cur().layout, n, sizeof(n));
    return !strcmp(n, name);
}

bool MenuCtl::layout_param(const char *key, char *out, int n) const
{
    const char *w = cur().layout ? strchr(cur().layout, ';') : 0;       // the first word is the layout's name
    return w && ui_attr(w + 1, key, out, n);
}

bool MenuCtl::goto_spec(const char *spec)
{
    char area[8];
    const char *slash = strchr(spec, '/');
    size_t al = slash ? (size_t)(slash - spec) : strlen(spec);
    if (al == 0 || al >= sizeof(area)) {
        return false;
    }
    memcpy(area, spec, al);
    area[al] = 0;
    for (int a = 0; a < tree_.nareas; a++) {
        if (strcmp(tree_.a[a].key, area)) {
            continue;
        }
        int t = 0;
        for (int b = 0; slash && b < tree_.a[a].ntabs; b++) {
            if (tree_.a[a].tabs[b].id && !strcmp(tree_.a[a].tabs[b].id, slash + 1)) {
                t = b;
            }
        }
        go(a, t);
        return true;
    }
    return false;                                                         // an area this core does not have
}

// The text a node's `sub` or `chip` key names: "@state.name" (a state value), "@info.Line" (an application information line), "link.Line" (ONLINE when
// the line holds something that is not NET_NONE_TEXT, else OFFLINE) or plain text. "|text" at the end is what stands when the value is empty.
void MenuCtl::spec_text(const char *spec, char *out, int n) const
{
    out[0] = 0;
    if (!spec || !spec[0]) {
        return;
    }
    if (!strcmp(spec, "@session")) {                                       // what the machine is doing: running, or waiting for a program
        snprintf(out, n, "%s", session_word(false));
        return;
    }
    char head[192], fallback[48];
    fallback[0] = 0;
    const char *bar = strchr(spec, '|');
    size_t hl = bar ? (size_t)(bar - spec) : strlen(spec);
    if (hl >= sizeof(head)) {
        hl = sizeof(head) - 1;
    }
    memcpy(head, spec, hl);
    head[hl] = 0;
    if (bar) {
        snprintf(fallback, sizeof(fallback), "%s", bar + 1);
    }
    auto info = [&](const char *line, char *dst, int dn) {
        dst[0] = 0;
        for (int i = 0; menu_info_labels[i]; i++) {
            if (!strcasecmp(menu_info_labels[i], line)) {
                h_->info(i, dst, dn);
            }
        }
    };
    if (head[0] == '@' && !strncmp(head, "@set.", 5)) {                    // a setting's value in words ("@set.CPU Speed"); a text setting gives its text
        int ix = c_->settings ? c_->settings->find(head + 5) : -1;
        if (ix >= 0) {
            snprintf(out, n, "%s", c_->desc->settings[ix].kind ? c_->settings->text(ix) : c_->settings->label(ix));
        }
    } else if (head[0] == '@' && !strncmp(head, "@info.", 6)) {
        info(head + 6, out, n);
    } else if (head[0] == '@') {
        const char *w = ops_state(c_, head + 1);
        snprintf(out, n, "%s", w ? w : "");
    } else if (!strncmp(head, "link.", 5)) {
        char v[64];
        info(head + 5, v, sizeof(v));
        snprintf(out, n, "%s", menu_link_online(v) ? "online" : "offline");
    } else {
        snprintf(out, n, "%s", head);
    }
    if (!out[0] && fallback[0]) {
        snprintf(out, n, "%s", fallback);
    }
}

const char *MenuCtl::hint() const
{
    static char why[120];
    if (!screen() || is_browser() || cur().count == 0) {
        return 0;
    }
    const MenuEntry &e = tree_.e[cur().first + sel_[area_][tab_]];
    if (e.kind != EK_NODE) {
        return 0;
    }
    const MenuNode &n = tree_.nodes[e.index];
    if (ui_attr(n.attr, "off", why, sizeof(why))) {
        return why;
    }
    if (n.kind == LN_OP && !node_ready(n)) {
        return ui_attr(n.attr, "why", why, sizeof(why)) ? why : "Not available now";
    }
    return 0;
}

const char *MenuCtl::dialog(char *text, int n) const
{
    text[0] = 0;
    if (fm_.view) {
        snprintf(text, n, "%s", fm_.view_path);
        return "viewer";
    }
    if (busy_) {
        snprintf(text, n, "%s", status_);
        return "busy";
    }
    if (pick_) {
        snprintf(text, n, "%s", pick_sets_ ? sets_[pick_sel_] : pick_path_);
        return "pick";
    }
    if (entry_) {
        if (confirm_node_ || confirm_task_ || remote_q_ || flow_q_) {
            snprintf(text, n, "%s", confirm_q_);
            return "confirm";
        }
        if (ask_node_) {
            snprintf(text, n, "%s: %s", ask_q_, entry_le_.buf);
            return "ask";
        }
        if (entry_text_mode_) {
            snprintf(text, n, "%s", entry_le_.buf);
            return "text";
        }
        snprintf(text, n, "%s", entry_digits_);
        return "address";
    }
    if (chooser_) {
        snprintf(text, n, "%s", chooser_file_);
        return "sheet";
    }
    return "none";
}

static bool is_switch(const Setting &s)
{
    static const char *const on[] = { "enabled", "on", "yes", "true" };
    static const char *const off[] = { "disabled", "off", "no", "false" };
    if (s.nvalues != 2) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        if (!strcasecmp(s.values[0], off[i]) && !strcasecmp(s.values[1], on[i])) {
            return true;
        }
    }
    return false;
}

static int tone_of(const char *name)
{
    static const char *const t[] = { "muted", "cyan", "violet", "amber", "coral" };
    for (int i = 0; i < 5; i++) {
        if (name && !strcmp(name, t[i])) {
            return i;
        }
    }
    return -1;
}

static const char *node_state_text(void *user, const char *name);

void MenuCtl::item(int i, Item *it)
{
    memset(it, 0, sizeof(*it));
    it->tone = -1;
    it->enabled = true;
    char left[120], right[120], sub[120];
    bool active = true;
    row(i, left, sizeof(left), right, sizeof(right), &active, sub, sizeof(sub));
    snprintf(it->title, sizeof(it->title), "%s", left);
    if (is_browser() || cur().count == 0 || i < 0 || i >= cur().count) {
        snprintf(it->sub, sizeof(it->sub), "%s", sub);
        snprintf(it->value, sizeof(it->value), "%s", right);
        return;
    }
    const MenuEntry &e = tree_.e[cur().first + i];
    switch (e.kind) {
    case EK_MEDIA: {
        it->kind = IK_MEDIA;
        const char *w = ops_state(c_, c_->desc->media[e.index].state);
        snprintf(it->sub, sizeof(it->sub), "%s", w ? w : (c_->desc->media[e.index].eject_op ? "No image" : "Nothing loaded"));
        snprintf(it->chip, sizeof(it->chip), "%s", !w ? "empty" : (c_->desc->media[e.index].eject_op ? "mounted" : "loaded"));
        it->enabled = active;
        break;
    }
    case EK_SETTING: {
        const Setting &s = c_->desc->settings[e.index];
        it->kind = IK_SETTING;
        snprintf(it->sub, sizeof(it->sub), "%s", sub);
        snprintf(it->value, sizeof(it->value), "%s", right);
        it->reset = row_reset(i);
        it->dirty = c_->settings && c_->settings->item_held(e.index);
        it->toggle = !s.kind && is_switch(s);
        it->on = it->toggle && c_->settings->value(e.index) == 1;
        snprintf(it->group, sizeof(it->group), "%s", s.group ? s.group : "");
        it->hidden = s.show_state && !cond_holds(s.show_state, node_state_text, c_);       // a row of a feature that is off is not listed
        break;
    }
    case EK_TASK: {
        const Task &t = c_->desc->tasks[e.index];
        it->kind = IK_TASK;
        snprintf(it->sub, sizeof(it->sub), "%s", t.ask ? t.ask : "");
        snprintf(it->chip, sizeof(it->chip), "%s", t.ask ? (t.pick_dir ? "folder" : "name") : "");
        it->tone = 0;
        break;
    }
    case EK_INFO:
        it->kind = IK_INFO;
        snprintf(it->sub, sizeof(it->sub), "%s", right);
        it->enabled = false;
        break;
    case EK_NODE: {
        const MenuNode &n = tree_.nodes[e.index];
        char buf[160];
        it->kind = n.kind == LN_STATE ? IK_ROW : n.kind == LN_GROUP ? IK_HEADING : IK_BUTTON;
        snprintf(it->slot, sizeof(it->slot), "%s", n.group ? n.group : "");
        it->primary = ui_attr(n.attr, "primary");
        it->link = ui_attr(n.attr, "link");
        if (ui_attr(n.attr, "tone", buf, sizeof(buf))) {
            it->tone = tone_of(buf);
        }
        if (ui_attr(n.attr, "label", buf, sizeof(buf))) {                 // the label is a state's text ("Pause" / "Resume")
            char t[80];
            spec_text(buf, t, sizeof(t));
            if (t[0]) {
                snprintf(it->title, sizeof(it->title), "%s", t);
            }
        }
        if (ui_attr(n.attr, "bkey", buf, sizeof(buf)) && buf[0] && n.kind == LN_OP) {                   // the button's letter: a keycap before the label, like the tab numbers
            it->bkey = buf[0];
        }
        if (ui_attr(n.attr, "key", buf, sizeof(buf)) && buf[0] && !ui_attr(n.attr, "nokey") && !(buf[0] == 'F' && buf[1] >= '1' && buf[1] <= '8')) {          // the node's letter, boxed at the row's right edge (the one way a hotkey is shown)
            snprintf(it->chip, sizeof(it->chip), "[%c]", buf[0]);
        }
        if (ui_attr(n.attr, "hide", buf, sizeof(buf))) {                  // not shown while the state has a value
            char t[16];
            spec_text(buf, t, sizeof(t));
            it->hidden = t[0] != 0;
        }
        if (n.kind == LN_STATE) {                                         // a state line: a label and its value; with a `sub` or `chip` it is a display row
            it->kind = (ui_attr(n.attr, "chip") || ui_attr(n.attr, "off")) ? IK_ROW : IK_VALUE;
            it->enabled = false;
            if (n.state) {
                const char *w = ops_state(c_, n.state);
                snprintf(it->value, sizeof(it->value), "%s", w ? w : "Not set");
            }
        }
        if (ui_attr(n.attr, "sub", buf, sizeof(buf))) {
            spec_text(buf, it->sub, sizeof(it->sub));
            if (it->kind == IK_VALUE) {
                snprintf(it->value, sizeof(it->value), "%s", it->sub);
                it->sub[0] = 0;
            }
        }
        if (ui_attr(n.attr, "chip", buf, sizeof(buf))) {
            spec_text(buf, it->chip, sizeof(it->chip));
        }
        if (ui_attr(n.attr, "none", buf, sizeof(buf))) {                   // a medium slot that is empty: "none", greyed
            char t[8];
            spec_text(buf, t, sizeof(t));
            if (!t[0]) {
                it->dim = true;
                char em[40];
                snprintf(it->sub, sizeof(it->sub), "%s", ui_attr(n.attr, "empty", em, sizeof(em)) ? em : "none");
                it->chip[0] = 0;                                              // nothing to eject or remove: no hotkey shown
            }
        }
        if (ui_attr(n.attr, "off", buf, sizeof(buf))) {                   // greyed, with its reason
            it->dim = true;
            if (n.kind == LN_OP) {
                it->enabled = false;
            }
            snprintf(it->sub, sizeof(it->sub), "%s", buf);
        } else if (n.kind == LN_OP && !node_ready(n)) {
            it->dim = true;
            it->enabled = false;
            if (!ui_attr(n.attr, "why", it->sub, sizeof(it->sub))) {
                snprintf(it->sub, sizeof(it->sub), "Not available now");
            }
        }
        if (n.kind == LN_GROUP) {
            it->enabled = false;
        }
        break;
    }
    default:
        it->kind = IK_FIXED;
        break;
    }
    item_ident(e, it);
}

// What an item is, not where it stands (T192.1-S4): the id of the intents is made from this text.
void MenuCtl::item_ident(const MenuEntry &e, Item *it) const
{
    char *o = it->ident;
    int cap = (int)sizeof(it->ident);
    switch (e.kind) {
    case EK_SETTING: snprintf(o, cap, "set:%s", c_->desc->settings[e.index].key); break;
    case EK_TASK: snprintf(o, cap, "task:%s", c_->desc->tasks[e.index].label); break;
    case EK_MEDIA: snprintf(o, cap, "media:%s", c_->desc->media[e.index].state ? c_->desc->media[e.index].state : it->title); break;
    case EK_INFO: snprintf(o, cap, "info:%s", it->title); break;
    case EK_NODE: {
        const MenuNode &n = tree_.nodes[e.index];
        int p = snprintf(o, cap, "node:%s", n.op ? n.op : n.state ? n.state : "");
        for (int b = 0; b < n.nbind && p < cap - 1; b++) {                // a button per slot: mount D1 / mount D2 share the operation, not the argument
            p += snprintf(o + p, cap - p, ":%s", n.bind[b].kind == BIND_STR ? (n.bind[b].s ? n.bind[b].s : "") : n.bind[b].kind == BIND_INT ? "n" : "");
            if (n.bind[b].kind == BIND_INT && p < cap - 1) p += snprintf(o + p, cap - p, "%d", (int)n.bind[b].i);
        }
        if (!n.op && !n.state && p < cap - 1) snprintf(o + p, cap - p, ":%s", it->title);
        break;
    }
    default: snprintf(o, cap, "fixed:%d:%s", (int)e.kind, it->title); break;
    }
}

// ---- T192.1 MVP: the entry points the intents use ---------------------------------------------------------------------------------------------
void (*MenuCtl::entry_hook)(const char *text) = 0;

// The cursor of the open list to row i, by the steps the cursor keys take (move() skips the rows that take no focus). False when it does not land on i.
bool MenuCtl::select_index(int i)
{
    int n = rows();
    if (i < 0 || i >= n || (entry_ && !pick_)) {
        return false;
    }
    int guard = n + 2;
    while (selected() != i && guard-- > 0) {
        int was = selected();
        move(i > was ? 1 : -1);
        if (selected() == was) {
            break;
        }
    }
    return selected() == i;
}

void MenuCtl::remote_ask(const char *question)
{
    entry_ = true;
    entry_text_mode_ = true;
    entry_task_ = 0;
    entry_set_ = -1;
    ask_node_ = 0;
    confirm_node_ = 0;
    confirm_task_ = 0;
    remote_q_ = true;
    remote_ans_ = -1;
    snprintf(confirm_q_, sizeof(confirm_q_), "%s", question);
    set_status("RETURN confirms, <- cancels", false);
}

int MenuCtl::take_remote_answer()
{
    int a = remote_ans_;
    remote_ans_ = -1;
    return a;
}

void MenuCtl::remote_close()
{
    if (remote_q_) {
        remote_q_ = false;
        entry_ = false;
    }
}

bool MenuCtl::replace_text(const char *text)
{
    if (!entry_ || !entry_text_mode_ || confirm_node_ || confirm_task_ || flow_q_ || remote_q_) {
        return false;
    }
    entry_load(text, LE_MAX);
    return true;
}







static const char *node_state_text(void *user, const char *name)
{
    return ops_state((CoreCtx *)user, name);
}

// A node works while its condition holds (contrib.h cond_holds); a row whose condition fails is shown and cannot be activated.
bool MenuCtl::node_ready(const MenuNode &n) const
{
    return cond_holds(n.when_state, node_state_text, c_);
}

// ---- the key table's executor (menuintent.h, menukeytab.cc) ---------------------------------------------------------------------------------------------
// Is the state a table row waits for there: a state name that has text, or "!name" while it has none.
static bool when_holds(void *user, const char *name)
{
    CoreCtx *c = (CoreCtx *)user;
    bool neg = name[0] == '!';
    const char *w = ops_state(c, neg ? name + 1 : name);
    return neg ? !(w && *w) : (w && *w);
}

// The stages of the old key(): each is live while the controller's state says so, tested in this order.
bool MenuCtl::stage_on(int stage) const
{
    switch (stage) {
    case KS_WIZARD: return wizard() != 0;
    case KS_ALIAS: return !(layout_is("terminal") && !busy_ && !entry_ && !pick_);
    case KS_VIEWER: return fm_.view;
    case KS_SEARCH_IDLE: return is_browser() && search_tab() && !entry_ && !busy_ && !pick_ && !chooser_ && (!fm_.vmode || fm_.nlist == 0);
    case KS_SHEET: return chooser_ && !entry_ && !pick_ && !busy_;
    case KS_APP: return app_mode_ && !busy_ && !entry_ && !pick_ && !chooser_;
    case KS_APP_TERM: return layout_is("terminal") && app_mode_ && !busy_ && !entry_ && !pick_;
    case KS_MONITOR: return layout_is("terminal") && !app_mode_ && !busy_ && !entry_ && !pick_;
    case KS_TIMELINE: return layout_is("timeline") && !busy_ && !entry_ && !pick_;
    case KS_BUSY: return busy_;
    case KS_PICK: return pick_;
    case KS_ENTRY: return entry_;
    case KS_PAGING: return !app_mode_ && is_browser() && !chooser_ && !fm_.seek_on;
    case KS_VOLUME: return !app_mode_;
    default: return true;
    }
}

// The first row, in the first live stage from `stage`, that holds this key (row `from` onwards in that stage). A live stage that swallows and holds no row for the key is an
// intent of its own (MI_SWALLOW with no row).
bool MenuCtl::resolve_from(int k, int stage, int from, MenuIntent *out) const
{
    int n;
    const KeyRow *rows = menu_keymap(&n);
    for (int s = stage; s < KS_COUNT; s++) {
        if (!stage_on(s)) {
            continue;
        }
        int cur = s == stage ? from : 0;
        const KeyRow *r = menu_keymap_next(s, k, &cur, when_holds, c_);
        if (r) {
            out->id = r->intent;
            out->arg = r->arg;
            out->key = k;
            out->stage = (int8_t)s;
            out->row = (int16_t)(r - rows);
            return true;
        }
        if (menu_stage_swallows(s)) {
            out->id = MI_SWALLOW;
            out->arg = 0;
            out->key = k;
            out->stage = (int8_t)s;
            out->row = -1;
            return true;
        }
    }
    return false;
}

// Key -> intent, nothing executed: what a producer sends. A key nobody holds is MI_NONE.
MenuIntent MenuCtl::resolve(int k) const
{
    MenuIntent in;
    in.id = MI_NONE;
    in.arg = 0;
    in.key = k;
    in.stage = KS_COUNT;
    in.row = -1;
    resolve_from(k, 0, 0, &in);
    return in;
}

// The intent runs; a row that declines hands the key to the next row, then to the next stage.
MenuCtl::Result MenuCtl::apply(const MenuIntent &in)
{
    Result r = apply_run(in);
    if (r == R_CLOSE) {
        leave();
    }
    return r;
}

MenuCtl::Result MenuCtl::apply_run(const MenuIntent &in)
{
    MenuIntent cur = in;
    for (int guard = 0; guard < 64; guard++) {
        if (cur.id == MI_NONE) {
            return R_NONE;
        }
        MenuIntent next;
        if (cur.id == MI_DECLINE) {                                  // the stage does not hold this key (the monitor's closing keys, the sheet's tab digits): the next stage has it
            if (!resolve_from(cur.key, cur.stage + 1, 0, &next)) {
                return R_NONE;
            }
            cur = next;
            continue;
        }
        bool took = true;
        Result r = run_intent(cur, &took);
        if (took) {
            return r;
        }
        if (cur.row < 0 || !resolve_from(cur.key, cur.stage, cur.row + 1, &next)) {   // an intent that declined: the next row of its stage, then the stages after it
            return R_NONE;
        }
        cur = next;
    }
    return R_NONE;
}

MenuCtl::Result MenuCtl::key(int k)
{
    return apply(resolve(k));
}

// An intent by its name in the table ("return", "back", "area" 5, "tab" 2, "move" -1): the key the table gives it, resolved where the screen stands now - so "return" while a question is
// up answers the question, as the key does. A name the table does not know is MI_NONE.
MenuIntent MenuCtl::named(const char *name, int arg) const
{
    int id = menu_intent_by_name(name);
    int k = -1;
    if (id == MI_TAB) {
        k = arg >= 1 && arg <= 8 ? '0' + arg : -1;                    // the tab's key is its digit
    } else if (id > 0) {
        k = menu_keymap_key_of(id, arg);
    }
    if (k < 0) {
        MenuIntent none;
        none.id = MI_NONE;
        none.arg = 0;
        none.key = 0;
        none.stage = KS_COUNT;
        none.row = -1;
        return none;
    }
    return resolve(k);
}

MenuCtl::Result MenuCtl::apply_named(const char *name, int arg, bool *known)
{
    MenuIntent in = named(name, arg);
    *known = in.id != MI_NONE;
    return apply(in);
}

// An operation named by a table row, with the arguments its flags ask for. Not taken: the row said an "unsupported" answer leaves the key to the next row.
MenuCtl::Result MenuCtl::run_row_op(const KeyRow &row, int k, bool *took)
{
    OpIn in[1];
    int nin = 0;
    if (row.flags & KF_ARG) {
        in[nin++] = in_int("value", row.arg);
    } else if (row.flags & KF_KEYARG) {
        in[nin++] = in_int("key", k);
    }
    OpResult r;
    int rc = (row.flags & KF_DIRECT) ? ops_call_direct(c_, row.op, nin ? in : 0, nin, &r) : ops_call(c_, row.op, nin ? in : 0, nin, &r);
    if ((row.flags & KF_UNSUPPORTED_DECLINES) && rc == OPE_UNSUPPORTED) {
        *took = false;
        return R_NONE;
    }
    if (row.flags & KF_DIRECTIVE) {                          // the operation tells the controller what comes next: "close" or "goto SPEC"
        if (!strcmp(r.msg, "close")) {
            return R_CLOSE;
        }
        if (!strncmp(r.msg, "goto ", 5)) {
            goto_spec(r.msg + 5);
        }
        return R_REDRAW;
    }
    if (row.flags & (KF_STATUS | KF_STATUS_OK)) {
        set_status(r.msg, (row.flags & KF_STATUS) && rc != 0);
    }
    if ((row.flags & KF_TOAST) && rc == 0) {
        toast(r.msg);
    }
    return (row.flags & KF_CLOSE) ? R_CLOSE : R_REDRAW;
}

MenuCtl::Result MenuCtl::run_intent(const MenuIntent &in, bool *took)
{
    int n;
    const KeyRow *rows = menu_keymap(&n);
    const KeyRow *row = in.row >= 0 && in.row < n ? &rows[in.row] : 0;
    const int k = in.key;
    *took = true;
    switch (in.id) {
    case MI_DECLINE:
        *took = false;
        return R_NONE;
    case MI_SWALLOW:
        return R_NONE;
    case MI_AS: {                                            // another key, from the next stage on
        MenuIntent next;
        if (!resolve_from(in.arg, in.stage + 1, 0, &next)) {
            return R_NONE;
        }
        return apply(next);
    }
    case MI_AREA: {
        fm_.view = false;
        int a = menu_area_of_key(&tree_, k);
        if (a < 0) {
            return R_NONE;                                   // an area this core does not have: the key does nothing
        }
        go(a, a == area_ ? tab_ : last_tab_[a]);
        return R_REDRAW;
    }
    case MI_TAB: {
        // No view here takes typed text, so a number is always the tab strip's. The one place the prototype reserves digits for input - a search or path field - does not exist yet,
        // and when it does it has to swallow them before this (NAVIGATION.md, "Universal keys").
        int t = menu_tab_of_number(&tree_, area_, k - '0');
        if (t < 0) {
            return R_NONE;
        }
        go(area_, t);
        return R_REDRAW;
    }
    case MI_MOVE:
        move(in.arg);
        return R_REDRAW;
    case MI_PAGE:
        page(in.arg);
        return R_REDRAW;
    case MI_LEFT:
        if (confirm_node_ || flow_q_) { return R_NONE; }
        if (chooser_) { chooser_ = false; return R_REDRAW; }
        if (on_setting()) return change_setting(-1);
        if (is_browser() && !chooser_) { fm_.parent_dir(); return R_REDRAW; }
        if (screen() && !is_browser()) { return key(in.arg); }              // on a button the cursor-left is "back one level", the buttons are reached with RIGHT and DOWN
        return R_NONE;
    case MI_RIGHT:
        if (on_setting()) return change_setting(1);
        if (is_browser() && !chooser_) { return activate(true); }          // the file's first action, without the list
        if (screen() && !is_browser()) { move(1); return R_REDRAW; }
        return R_NONE;
    case MI_DEL:                                                  // DEL: only in a text; in the seek it takes the last letter back
        if (fm_.seek_on) {
            int l = (int)strlen(fm_.seek);
            if (l) {
                fm_.seek[l - 1] = 0;
            }
            return R_REDRAW;
        }
        return R_NONE;
    case MI_SPACE:
        if (is_browser() && !chooser_) {                            // SPACE marks the file (Gideon's multi-select) and goes on to the next
            if (fm_.sel < fm_.nlist && strcmp(fm_.list[fm_.sel].name, "..") && fm_.list[fm_.sel].name[0] != '[') {
                fm_.marked[fm_.sel] = !fm_.marked[fm_.sel];
                if (fm_.sel + 1 < fm_.nlist) {
                    fm_.sel++;
                }
                fm_.lib_sync();
            }
            return R_REDRAW;
        }
        return activate();
    case MI_RETURN:
        if (fm_.seek_on) { fm_.seek_on = false; set_status("", false); }
        return activate();
    case MI_MARK_ALL:
        if (is_browser()) {
            for (int i = 0; i < fm_.nlist; i++) {
                fm_.marked[i] = strcmp(fm_.list[i].name, "..") && fm_.list[i].name[0] != '[';
            }
            fm_.lib_sync();
            return R_REDRAW;
        }
        return R_NONE;
    case MI_COPY:
    case MI_PASTE:
        if (is_browser() && !chooser_) {
            fm_.lib_sync();
            if (in.id == MI_COPY) {
                fm_.clip(false, 0, false);
                return R_REDRAW;
            }
            return start_job(*FileMgr::task_paste(), "");
        }
        return R_NONE;
    case MI_MARK_NONE:
        if (is_browser()) {
            memset(fm_.marked, 0, sizeof(fm_.marked));
            fm_.lib_sync();
            return R_REDRAW;
        }
        return R_NONE;
    case MI_BACK:
        if (chooser_) { chooser_ = false; return R_REDRAW; }
        if (fm_.seek_on) { fm_.seek_on = false; set_status("", false); return R_REDRAW; }
        {
            // one level up: a folder, a list, the first tab, Home; on Home the menu closes
            if (is_browser() && (screen() || (!busy_ && !pick_ && !entry_))) {     // a browser tab with no layout (a generic core's) goes up a folder like the Library does
                if (fm_.vmode) {
                    fm_.vmode = 0;
                    fm_.load_dir();
                    return R_REDRAW;
                }
                if (fm_.image_ft >= 0 || strcmp(fm_.path, "/")) {                // a folder, and the top of a medium too: up to the media level; at "/" the key leaves the Library
                    fm_.parent_dir();
                    return R_REDRAW;
                }
            }
            if (tab_ != 0) {
                go(area_, 0);
                return R_REDRAW;
            }
            if (area_ != 0) {
                go(0, 0);
                return R_REDRAW;
            }
        }
        if (!closable()) {
            set_status(c_->desc->needs_program, false);
            return R_REDRAW;
        }
        return R_CLOSE;
    case MI_CLOSE:
        if (!closable()) {
            set_status(c_->desc->needs_program, false);
            return R_REDRAW;
        }
        return R_CLOSE;
    case MI_LEAVE:
        return R_CLOSE;
    case MI_SLASH:
        if (search_tab() && !chooser_) {
            begin_search_edit();
            return R_REDRAW;
        }
        if (is_browser() && !chooser_ && fm_.image_ft < 0) {                  // quick seek: / then the name (? is any character), DEL or RETURN ends it
            fm_.seek_on = true;
            fm_.seek[0] = 0;
            set_status("Seek: type the name, RETURN or <- ends", false);
            return R_REDRAW;
        }
        *took = false;                                       // the next row (MI_CHAR) may take the '/'
        return R_NONE;
    case MI_CHAR:
        if (fm_.seek_on && is_browser() && !chooser_ && k > 0x20 && k < 0x7F) {
            fm_.seek_at = c_->io->now_ms();
            fm_.seek_key(k);
            return R_REDRAW;
        }
        if (k > 0x20 && k < 0x7F && !chooser_) {
            return hotkey(k);
        }
        return R_NONE;
    case MI_SHEET_LETTER: {                                  // a letter of the sheet runs its item
        char u = (char)toupper(k);
        for (int i = 0; i < chooser_n_; i++) {
            if (chooser_acts_[i].key == u) {
                chooser_sel_ = i;
                return start_action(chooser_acts_[i], chooser_file_);
            }
        }
        *took = false;
        return R_NONE;
    }
    case MI_SEARCH_OPEN:
        begin_search_edit();                                         // the idle Search tab: RETURN opens the field
        return R_REDRAW;
    case MI_PAGE_BROWSER:
        page(in.arg);
        return R_REDRAW;
    case MI_OP:
        return row ? run_row_op(*row, k, took) : R_NONE;
    case MI_VIEW_SCROLL:
        fm_.view_scroll(in.arg);
        return R_REDRAW;
    case MI_VIEW_CLOSE:
        fm_.view = false;
        set_status("", false);
        return R_REDRAW;
    case MI_VIEW_LEAVE:
        fm_.view = false;
        return key(k);
    case MI_WIZ_CANCEL:
    case MI_WIZ_BACK:
    case MI_WIZ_NEXT:
        wizard_key(in.id);
        return R_REDRAW;
    case MI_APP_CLOSE: {
        char why[100];
        if (locked(why, sizeof(why))) {                              // a step that cannot stop: R/S LOCKED, and the reason stays on the screen
            set_status(why, true);
            return R_REDRAW;
        }
        return R_CLOSE;
    }
    case MI_APP_GRID: {
        char kop[48];
        if (layout_is("terminal") || !layout_is("grid") || !layout_param("keys", kop, sizeof(kop))) {
            *took = false;
            return R_NONE;
        }
        OpIn oi[1] = { in_int("key", k) };                           // a memory view: the cursor keys scroll it (and change the bank), whatever is selected
        OpResult kr;
        ops_call(c_, kop, oi, 1, &kr);
        return R_REDRAW;
    }
    case MI_APP_BACK: {                                              // one step out: the tab the screen names (`back`), else the first tab, else the caller
        if (layout_is("terminal")) {
            *took = false;
            return R_NONE;
        }
        char to[40];
        if (layout_param("back", to, sizeof(to)) && goto_tab_id(to)) {
            return R_REDRAW;
        }
        if (tab_ != 0) {
            go(area_, 0);
            return R_REDRAW;
        }
        char why[100];
        if (locked(why, sizeof(why))) {
            set_status(why, true);
            return R_REDRAW;
        }
        return R_CLOSE;
    }
    case MI_APP_FKEY: {                                              // a function key runs the node of the tab that names it (`key = "F3"`)
        if (layout_is("terminal")) {
            *took = false;
            return R_NONE;
        }
        char name[4] = { 'F', (char)('0' + in.arg), 0, 0 };
        Result r = fkey_node(name);
        if (r == R_NONE) {
            *took = false;
        }
        return r;
    }
    case MI_APP_TERMKEY: {                                           // an application's terminal: every key is the application's (T104.3 D5)
        char kop[48];
        if (!layout_param("keys", kop, sizeof(kop))) {
            *took = false;
            return R_NONE;
        }
        OpIn oi[1] = { in_int("key", k) };
        OpResult kr;
        ops_call(c_, kop, oi, 1, &kr);
        return R_REDRAW;
    }
    case MI_BUSY_CANCEL:
        c_->job_cancel = true;
        set_status("Cancelling ...", false);
        return R_REDRAW;
    case MI_RAW: {
        Result rr = R_NONE;
        bool t = in.stage == KS_PICK ? pick_key(k, &rr) : entry_key(k, &rr);
        *took = t;
        return rr;
    }
    default:
        *took = false;
        return R_NONE;
    }
}

// ====================================================================================================================================================
// The Library (T104.2 UI): marks, the clipboard's folder, search, quick seek, the viewer
// ====================================================================================================================================================
static const char *base_name_of(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

bool MenuCtl::search_tab() const { return cur().id && !strcmp(cur().id, "search"); }
bool MenuCtl::manage_tab() const { return cur().id && !strcmp(cur().id, "manage"); }




void MenuCtl::begin_search_edit()
{
    entry_ = true;
    entry_text_mode_ = true;
    entry_task_ = 0;
    entry_set_ = -1;
    ask_node_ = 0;
    confirm_node_ = 0;
    confirm_task_ = 0;
    entry_rename_ = false;
    search_edit_ = true;
    entry_load(fm_.search_q, 38);
    set_status("Type a part of the name; RETURN searches", false);
}



void MenuCtl::begin_rename(const char *path)
{
    entry_ = true;
    entry_text_mode_ = true;
    entry_task_ = 0;
    entry_set_ = -1;
    ask_node_ = 0;
    confirm_node_ = 0;
    confirm_task_ = 0;
    search_edit_ = false;
    entry_rename_ = true;
    snprintf(rename_path_, sizeof(rename_path_), "%s", path);
    entry_load(base_name_of(path), NAME_LONG_MAX);
    set_status("Type the new name", false);
}

MenuCtl::Result MenuCtl::ui_action(const char *op, const char *file)
{
    LibSel &l = c_->lib;
    bool marked_file = false;
    for (int m = 0; m < l.nmarked; m++) {
        char p[FS_PATH_MAX];
        snprintf(p, sizeof(p), "%s%s%s", l.dir, strcmp(l.dir, "/") ? "/" : "", l.marked[m]);
        marked_file |= !strcmp(p, file);
    }
    if (!strcmp(op, "ui.view") || !strcmp(op, "ui.hex")) {
        fm_.start_view(file, !strcmp(op, "ui.hex"));
        return R_REDRAW;
    }
    if (!strcmp(op, "ui.open")) {
        if (strlen(file) >= sizeof(fm_.path)) {
            set_status("The path is too long", true);
            return R_REDRAW;
        }
        fm_.open_folder(file);
        return R_REDRAW;
    }
    if (!strcmp(op, "ui.markall") || !strcmp(op, "ui.marknone")) {
        bool all = !strcmp(op, "ui.markall");
        fm_.mark_all(all);
        set_status(all ? "Everything here is marked" : "Nothing is marked", false);
        return R_REDRAW;
    }
    if (!strcmp(op, "ui.mkdir")) {
        note_folder();
        entry_ = true;
        entry_text_mode_ = true;
        entry_task_ = 0;
        entry_set_ = -1;
        ask_node_ = 0;
        confirm_node_ = 0;
        confirm_task_ = 0;
        search_edit_ = false;
        entry_rename_ = true;
        rename_path_[0] = 0;                                            // no path: the text is a new folder's name
        entry_load("", NAME_LONG_MAX);
        set_status("Name of the new folder", false);
        return R_REDRAW;
    }
    if (!strcmp(op, "ui.home")) {
        fm_.set_home();
        return R_REDRAW;
    }
    if (!strcmp(op, "ui.paste")) {
        fm_.lib_sync();
        return start_job(*FileMgr::task_paste(), "");
    }
    if (!strcmp(op, "ui.clip0") || !strcmp(op, "ui.clip1")) {
        fm_.clip(op[8] == '1', file, marked_file);
        return R_REDRAW;
    }
    if (!marked_file) {                                             // the action of one file acts on that file, not on marks elsewhere
        l.nmarked = 0;
    }
    snprintf(l.cur, sizeof(l.cur), "%s", file);
    if (!strcmp(op, "ui.copy")) {
        begin_pick(*FileMgr::task_copy());
        return R_REDRAW;
    }
    if (!strcmp(op, "ui.move")) {
        begin_pick(*FileMgr::task_move());
        return R_REDRAW;
    }
    if (!strcmp(op, "ui.rename")) {
        begin_rename(file);
        return R_REDRAW;
    }
    if (!strcmp(op, "ui.delete")) {
        char q[160];
        FileMgr::delete_question(c_, q, sizeof(q));
        begin_confirm_job(FileMgr::task_delete(), q);
        return R_REDRAW;
    }
    return R_NONE;
}

// ---- the viewer ---------------------------------------------------------------------------------------------------------------------------------------





// Timewarp (F2) has the keys of TRX64-Ultimate's trx_timewarp_ui.cc: LEFT and RIGHT one frame back and forward, UP and DOWN one second, PAGE UP and PAGE DOWN (F1 and F7) ten
// seconds, HOME the oldest frame, RETURN rewinds there and runs, RUN/STOP and the left arrow cancel (the machine goes on from the newest frame). Nothing else is the screen's.

// ---- a wizard of a module (menuflows.h) -------------------------------------------------------------------------------------------------------------
// The wizard that is up, null when none: a module's state says so.
const MenuWizard *MenuCtl::wizard() const
{
    int n;
    const MenuWizard *w = menu_wizards(&n);
    for (int i = 0; i < n; i++) {
        const char *a = ops_state(c_, w[i].active);
        if (a && a[0]) {
            return &w[i];
        }
    }
    return 0;
}

// A key of the wizard's stage: the module's operation for it; what it says is the status line (a step in the middle says nothing).
void MenuCtl::wizard_key(int intent)
{
    const MenuWizard *w = wizard();
    if (!w) {
        return;
    }
    const char *op = intent == MI_WIZ_NEXT ? w->next : intent == MI_WIZ_BACK ? w->back : w->cancel;
    OpResult r;
    r.msg[0] = 0;
    int rc = ops_call(c_, op, 0, 0, &r);
    if (r.msg[0]) {
        set_status(r.msg, rc != 0);
    }
}

// The run loop's look at the pad while a wizard is up: true when the screen changed.
bool MenuCtl::map_tick()
{
    const MenuWizard *w = wizard();
    if (!w) {
        return false;
    }
    OpResult r;
    r.msg[0] = 0;
    r.more = false;
    int rc = ops_call(c_, w->tick, 0, 0, &r);
    if (!r.more) {
        return false;
    }
    if (r.msg[0]) {
        set_status(r.msg, rc != 0);
    }
    return true;
}
