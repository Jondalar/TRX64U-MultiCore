// The keys of the menu and what they do (T93 D8, D7, S3, S4), without a screen: the LVGL views (ui.cc) draw the rows this class
// gives. Pure, so tests/coreapp/ presses keys on a fake machine and reads the rows back.
//
// The route is the prototype's grammar (T104 5.8): a function key picks an area, a number key picks a tab inside it, and
// cursor keys and Return remain a complete alternative for every route. Which areas and tabs exist is menutree.cc's answer,
// built from the descriptor - this class only moves through them.
#ifndef COREAPP_MENUCTL_H
#define COREAPP_MENUCTL_H

#include "picture.h"
#include "ctx.h"
#include "lineedit.h"
#include "menutree.h"
#include "mediamounts.h"
#include "menuintent.h"
#include "menuflows.h"
#include "menuvlist.h"
#include "picklist.h"
#include "filemgr.h"
#include "hotkeys.h"

class MenuHost {
public:
    virtual ~MenuHost() { }
    virtual void info(int which, char *out, int n) = 0;           // the value of menu_info_labels[which]
    virtual bool return_to_host(char *msg, int n) = 0;            // false and a reason when it cannot (it does not come back otherwise)
};

class MenuCtl {
public:
    enum Result { R_NONE = 0, R_REDRAW, R_CLOSE };
    enum { VROWS = FileMgr::VROWS, VIEW_ROWS = FileMgr::VIEW_ROWS, VIEW_COLS = FileMgr::VIEW_COLS, VIEW_MAX = FileMgr::VIEW_MAX };

    MenuCtl(CoreCtx *c, MenuHost *h);
    ~MenuCtl();

    // Builds the tree again from the descriptor and ctx->merge (the libraries were loaded after the menu was made). The selection starts over.
    void rebuild();

    // The menu opens: first page (Machine), or the browser when the core waits for a program.
    void open();
    // T192.3 D30: the menu has been left (the key or the intent that closes it, or the service's close): the place is remembered - area, tab, the row by its stable ident, the
    // Library's folder, the scroll of the list. open() brings the person back to it when the core's "Menu opens at" is "Last screen" (home_reopen_last), else to Home as before.
    void leave();
    // The scroll of the open list (the first unit of the window): the screen writes what it showed (note_scroll) and, when scroll_rev() has moved, starts the window at scroll_first().
    void note_scroll(int first) { scroll_ = first; }
    int scroll_first() const { return scroll_; }
    uint32_t scroll_rev() const { return scroll_rev_; }
    Result key(int k);                                             // the shim of the key table: resolve(k), then apply
    // T192.3 S6, D23: every producer enters as an intent. resolve: key -> intent through the table (nothing runs); apply: the intent runs (a row that declines hands the key on);
    // apply_named: a remote intent by name ("area" 5, "tab" 2, "move" -1, "back", "return" ...).
    MenuIntent resolve(int k) const;
    Result apply(const MenuIntent &in);                            // apply_run, and a close remembers the place (leave)
    MenuIntent named(const char *name, int arg) const;
    Result apply_named(const char *name, int arg, bool *known);
    // Reads the browser's directory again and reports whether anything changed (media that mounted late).
    bool relist();
    // A new file is being made in steps (T122 §5): true while a progress display stands. The run loop calls tick() each pass; one call is one
    // chunk written (or the start of the job), the answer says whether the screen changed.
    bool busy() const { return busy_; }
    bool tick();
    // The controller's own clock, once per pass of the run loop (or of the service, T192.3 leaf 9): a wizard's look at its input, the next chunk of a job, the monitor's stops, the
    // toast that ran out, a register shown live, the Library's folder read again every two seconds. `quiet`: no key and no other change was taken this pass (the work that
    // needs the screen as it is waits for a quiet pass). True: the screen changed.
    bool pump(bool quiet);
    int progress() const { return busy_ ? c_->job_pct : -1; }
    bool picking() const { return pick_; }
    int list_side_n(int i) const { PickRow r; return list_view() && pl_row_at(i, &r) && !r.note && !r.more ? r.nside : 0; }   // T262 P1: the side lines of row i (the side panel shows those of the selected row)
    void list_side(int i, int k, char *out, int n) const;                  // side line k of row i into out ("" when there is none)
    bool list_view() const { return pick_dyn_ && pl_mode_ == PL_LIST; }       // T262 P1: the page on screen is the rows of a `list` node
    int list_tone(int i) const { PickRow r; return list_view() && pl_row_at(i, &r) ? r.tone : -1; }   // T262 P1: the tone of row i of the list on screen (0 muted .. 4 coral), -1 none
    bool picking_modes() const { return pick_ && pick_modes_; }          // T187 F6: the Picture page names its path in the top bar
    // T156 D5: the mapping of a pad's buttons ("Map buttons"): the menu asks for the core's buttons one by one and the person presses the pad's
    // button for each. The run loop calls map_tick() each pass while mapping(); true: the screen changed.
    bool mapping() const { return wizard() != 0; }
    bool map_tick();                         // a wizard is up: one look at what it waits for; true: the screen changed

    // view model
    int area() const { return area_; }
    int tab() const { return tab_; }
    const char *title() const;                                     // the tab name, or the browser's path
    void pos_text(char *b, int n, int first, int rows_shown);       // the Library header: "773-784 of 784" / "N items"
    int entries();                                                 // rows() without the line of a cut Library list (what the header counts)
    bool list_cut() const { return fm_.more && !fm_.vmode; }        // the Library list is cut by the heap budget (its last row is the line that says so)
    int rows();                                                    // rows of the current view
    int selected() const;
    bool row_reset(int i) const;                                   // T187 D9: row i is a setting that takes effect at the next reset
    bool ring_listed() const;
    const char *foot_line();                                 // T187 F4: status, then the ring legend when a ring row is in the list
    const char *sel_detail();                                      // T209 S3: what the selected row says about itself (why it is greyed, what RETURN does), "" when nothing
    static const char *reset_legend() { return "A ring beside a name: the value is used from the next reset"; }
    // title, description (may be ""), value (may be ""), and whether the row can be activated. The three are the
    // prototype's setting_row: title, description, control (T104 5.8).
    void row(int i, char *left, int ln, char *right, int rn, bool *active, char *sub = 0, int sn = 0);
    // T187 D1: row i of the flat list is a heading (a quiet divider): never selected, never the last line of the window. T187 rule 5: the first row of a tab's
    // window is kept per tab, so coming back to a tab (from a page it opened, or from another area) shows the rows where they were.
    bool heading(int i);
    bool title_row(int i);                       // only a heading proper (not an information line): the one a window never ends on
    int first_hint() const;
    void note_first(int first);
    static const int NAME_LONG_MAX = 255;                          // a FAT long name: what Rename and New folder take
    static const int ENTRY_VIS = 56;                               // the characters of an entry the row shows; more scroll (the tail stays)
    const char *status() const { return status_; }
    // T160: a line that stays a moment over every page (the volume keys' feedback); the application draws it (ui.cc) and polls toast_expire().
    const char *toast_text() const { return toast_; }
    uint32_t toast_revision() const { return toast_rev_; }
    void say(const char *s) { toast(s); }                          // T181: the line of a hotkey's action, over the running picture
    void toast_clear() { toast_[0] = 0; toast_rev_++; }
    bool toast_expire();                                           // true when the line just ran out
    bool status_bad() const { return status_bad_; }
    bool chooser() const { return chooser_; }
    bool entering() const { return entry_; }                      // a prompt is up (an address, or a name)
    bool learning() const { return learn_ >= 0; }                 // T207: "Press the new key" is up (entry_ too); the run loop runs no hotkey meanwhile
    const MenuTree &tree() const { return tree_; }
    bool closable() const;
    bool unsaved() const { return c_->settings && c_->settings->dirty(); }
    // T104.3: the screens of an application (apphost.cc) are a menu of their own on this class. `app_mode`: no rail of areas, the application's keys.
    void set_app_mode(bool on) { app_mode_ = on; }
    bool app_mode() const { return app_mode_; }
    bool goto_spec(const char *spec);                              // "F3", "F5/drives", "APP/flash": an area, an area and a tab
    bool action_pending() const { return pend_on_; }                // an action waits for its file to be fetched (run_action)
    bool goto_when_open(const char *spec);                         // goto_spec now when the menu is open, else open() takes it (T256 D13); false: no such place
    void say_when_open(const char *s, bool bad);                   // say() now when the menu is open, else open() shows it after its moves
    bool take_prompt();                                            // T228 R21: an operation asked something (CoreCtx::want_prompt): the text prompt or the pick list is up; true: it is
    bool take_screen();                                            // T228 R1: the screen an operation asked for (CoreCtx::want_screen, menu.screen) is shown; true: moved
    bool screen_way(const char *layout, char *out, int n) const;   // T228 R1: how the screen of that layout is opened (the keys help); false: none here
    void app_start();                                             // runs the open tab's `enter` operation (an application's screens)
    // The controller's own clock ran an action that closes the menu (a Run after a fetch, pend_tick): the loop closes the menu as it does for a key's R_CLOSE; an application's session
    // raises close_req_ instead (take_close_request).
    bool take_pump_close() { bool r = pump_close_; pump_close_ = false; return r; }
    bool take_close_request() { bool r = close_req_; close_req_ = false; return r; }   // the application asked for its screens to close (follow = "close")
    bool follow_tick();                                            // the application asked for another tab through the first tab's `follow` state; true: moved
    bool locked(char *why, int n) const;                           // the open tab's `lock` state holds a reason: RUN/STOP does not close the screens now
    void refocus();                                                // the focused row of a screen hid: the focus goes to the next row that takes it, else the one before (T262 P8)
    bool tab_off(int area, int tab) const;                         // the tab's `show` state is empty (T262 P3)
    int first_shown_tab(int area) const;
    bool tab_is_hidden(int area, int tab) const;                   // the tab's layout says `hidden`: it is on no strip and has no number
    bool tab_spec(const char *key, char *out, int n) const;        // the open tab's layout parameter `key` is a state value; its text, false when empty
    void say(const char *s, bool bad) { set_status(s, bad); }      // a line for the status row, from outside (an application that ended)
    bool is_home() const;                                          // the open tab holds the information lines
    void session_line(char *out, int n) const;                     // what is running, for the Home panel
    void video_line(char *out, int n) const;                       // its picture, as the descriptor states it
    void info_line(int which, char *out, int n) const;             // menu_info_labels[which]'s value, for the chips

    // ---- the screens (T104.2 D10): what the layouts of ui/screens.cc draw. A tab that names a layout (menu.toml `layout`) is drawn as its screen of the UX
    // concept while no prompt, job or folder picker is up; those, and a tab without a layout, are the flat list.
    enum ItemKind { IK_BUTTON = 0, IK_ROW, IK_VALUE, IK_HEADING, IK_SETTING, IK_MEDIA, IK_TASK, IK_INFO, IK_FIXED };
    struct Item {
        char slot[20];                   // the anchor's group: where the layout puts it ("head", "side", ...); "" = the tab's main list
        char group[28];                  // a setting's group (a heading over the first row of each group when a tab lists several), "" none
        char title[80], sub[120], chip[40], value[64];   // sub (T209): the foot line while this row is selected - why it is greyed, what RETURN does; never a second line of the row
        int kind;
        int tone;                        // 0 muted, 1 cyan, 2 violet, 3 amber, 4 coral; -1: by the chip's word
        bool primary, link;
        bool enabled;                    // RETURN does something
        bool dim;                        // greyed: not usable now; `sub` then says why (in the foot, while the row is selected)
        bool toggle, on;                 // a setting that is a switch
        char bkey;                       // a button's letter on its page, drawn as a keycap before the label (0: none)
        bool hidden;                     // the node says `hide` and its state has a value: it is not drawn and takes no focus
        bool dirty;                      // T192.3 S11: a setting that was changed and waits for Save (Auto Save Config Off or Ask)
        bool reset;                      // T187 D9: a setting that takes effect at the next reset: the row draws a small mark beside its label, no second line
        char ident[96];                  // T192.1: what the item IS ("set:Sound", "node:files.mount:D1"), not where it stands; the id of the intents is made from it
    };
    void item(int i, Item *it);
    bool screen() const;                                           // the open tab is drawn by its layout now
    const char *layout() const { return cur().layout; }
    bool layout_is(const char *name) const;
    bool paused() const { return c_->paused; }
    // What the machine is doing, as the top bar and the home panel say it: Pause toggle on -> Paused; a started machine with the menu on screen is held (Freeze) -> Frozen;
    // the menu closed -> Running; a core that waits for its program -> Waiting. `upper`: the shell's capitals.
    const char *session_word(bool upper) const;
    bool layout_param(const char *key, char *out, int n) const;
    const char *area_name() const { return tree_.a[area_].name; }
    const char *core_title() const;                                // the core's short title
    const char *tab_name() const { return cur().name; }
    // T154, T192.3: the menu has just opened and the machine is held: the flows a module owns get their turn (menuflows.h). A question one of them puts up (the changed
    // cartridge: RETURN saves, <- drops the change) is the first thing on screen; a note it has is the status line.
    void held_open();
    bool flow_asking() const { return flow_q_ != 0; }
    // The library: sources (the media of the root listing), the rows with what a row shows, the selected file's actions
    typedef FileMgr::BrowseRow BrowseRow;
    void browse_row(int i, BrowseRow *r) const;
    void set_page_rows(int n) { page_rows_ = n > 0 ? n : 1; }      // the screen's number of list rows: what a page is in the F3 browser
    bool media_level() const { return fm_.media_level(); }        // the browser stands on "/": the list of the media, as the host's File Browser
    bool medium_at(int i, MediumInfo *mi) const { return fm_.medium_at(i, mi); }   // what the medium of row i is (the info panel of the media level)
    int nroots() const { return nchips(); }
    const char *root(int i) const { return chip_name(i); }
    int root_active() const { return chip_active(); }
    // the media chips of the path row (uniform browser): fixed names, grey when the medium is not there, Left/Right choose, RETURN opens
    int nchips() const { return fm_.nchips(); }
    int chip_base(int vi) const { return fm_.chip_base(vi); }       // the place of the i-th shown chip among all of them
    const char *chip_name(int i) const { return fm_.chip_name(i); }
    uint8_t chip_flags(int) const { return 0; }                    // the chips only show the open medium (T104.2): none has the focus
    int chip_active() const { return fm_.chip_active(); }
    void footer_legend(char *out, int n);                          // the line at the foot: the keys of the row under the cursor, or of the screen, then "+/- PAGE" when the list is longer than its window (T209 D4)
    void note_paged(bool p) { paged_ = p; }
    bool paged() const { return paged_; }                        // told by the layout that draws the list: it has more rows than its window shows
    const char *browse_path() const { return fm_.vmode ? fm_.vlabel : fm_.path; }
    // the viewer (View and Hex View of a file): text lines or 16 bytes a row, drawn by screens.cc instead of the list
    bool viewer() const { return fm_.view; }
    const char *view_title() const { return fm_.view_path; }
    bool view_is_hex() const { return fm_.view_hex; }
    void view_line(int row, char *out, int n) const { fm_.view_line(row, out, n); }      // the text of screen row `row` (0 .. VIEW_ROWS - 1)
    void view_status(char *out, int n) const { fm_.view_status(out, n); }
    // the Search tab: the query and what it found
    bool search_tab() const;
    bool in_search_tab() const { return search_tab(); }
    bool search_active() const { return fm_.vmode == 1; }
    bool manage_tab() const;
    const char *search_query() const { return fm_.search_q; }
    const char *search_form_text() const { return sf_query_; }     // T262 D4: what the form's Name contains holds (the idle Search tab shows it)
    bool search_editing() const { return search_edit_; }
    enum { SF_FIELDS = 4 };                                         // fields of one source (contrib.h CONTRIB_SEARCH_FIELDS)
    bool search_form() const { return pick_form_; }                 // T262 P5: the search form of the Library is up (the media and the sources of libraries)
    int search_source_count() const;                               // T262 P5: how many sources of libraries the F3 search offers now
    void search_source_name(char *out, int n) const;                // the source the form searches now ("This device", or a library's name)
    bool in_image() const { return fm_.image_ft >= 0; }
    const char *chooser_file() const { return chooser_file_; }     // the file whose actions the list shows
    int chooser_count() const { return chooser_n_; }
    const char *chooser_label(int i) const { return chooser_acts_[i].label; }
    char chooser_key(int i) const { return chooser_acts_[i].key; }
    void spec_text(const char *spec, char *out, int n) const;      // "@state.name|fallback", "@info.Line", "link.Line" or plain text, as a node's ui keys hold it
    const char *hint() const;                                      // the reason the selected row is greyed, else null
    // T153 (the state query): what is up over the page - "none", "confirm", "ask", "text", "address", "pick", "busy", "sheet" or "viewer" - and its text
    // (the question, the typed text, the folder, the file). Read-only.
    const char *dialog(char *text, int n) const;
    // T192.1 MVP, the entry points the intents use. select_index: the cursor of the open list to row i by the steps the cursor keys take (false: it did not land
    // there, e.g. a row that takes no focus). remote_ask: a confirmation a remote request asked, shown and answered like the menu's own (RETURN yes, <- no);
    // take_remote_answer: -1 not yet, 0 no, 1 yes (the screen answered); remote_close: withdrawn because the answer came over the network.
    // replace_text: the text field being edited holds this text (the entry history). entry_hook: told every text the person committed.
    bool select_index(int i);
    bool browsing() const { return is_browser(); }                 // the open tab is the file browser (its rows are files and folders)
    void remote_ask(const char *question);
    bool remote_asking() const { return remote_q_; }
    int take_remote_answer();
    void remote_close();
    bool replace_text(const char *text);
    static void (*entry_hook)(const char *text);
    const char *tab_id() const { return cur().id ? cur().id : ""; }
    // T212 D5: the drive whose list is open / whose Library mount is under way (-1: none); the one-line machine description of Home ("AGA - 2 MB - PAL"), empty when
    // the core's settings say nothing of it.
    int mount_slot() const { return mount_slot_; }
    void machine_line(char *out, int n) const;

private:
    Result apply_run(const MenuIntent &in);
    void reopen_restore();
    struct Reopen {
        bool valid = false;
        int area = 0, tab = 0;
        char area_name[24] = "", tab_id[32] = "";
        char row[96] = "";                                         // the row's ident (a list), or the file's name (the Library)
        char folder[200] = "";                                     // the Library's folder, "" when it stood in a search, a list or an image
        int scroll = 0;
    } reopen_;
    int scroll_ = 0;
    uint32_t scroll_rev_ = 0;
    void set_status(const char *s, bool bad);
    bool stage_on(int stage) const;                                // the stage of the key table is live now
    bool resolve_from(int k, int stage, int from, MenuIntent *out) const;
    int core_row_next(int k, int from) const;                      // the next row of the core's key table (descrec.h section 18) for the shown tab and this key, -1 when none
    const KeyRow *row_at(int idx) const;                           // a row of the key table by its number in an intent: menukeytab.cc's, then the core's
    Result run_intent(const MenuIntent &in, bool *took);
    Result run_row_op(const KeyRow &row, int k, bool *took);
    void begin_search_edit();
    // T262 P5: the one search form of F3 when a library is a source (contrib.h MenuMerge::srcs). pick_ is up too; rows: Source, Name contains, the source's fields, Search.
    bool pick_form_ = false;
    int sf_src_ = -1;                                              // -1 This device, else the index into MenuMerge::srcs
    int sf_edit_ = -1;                                             // the row of the form being typed (1 the name, 2.. a field)
    char sf_query_[40] = "", sf_field_[SF_FIELDS][40] = {};
    int sf_after_ = -1;                                            // the search job of this source is running: its rows open as a list when it has ended
    int search_sources(int *idx, int max) const;                   // the indexes of the sources whose condition holds
    int sf_nfields() const;
    void sf_label(int row, char *out, int n) const;                // the label of a form row
    const char *sf_value(int row) const;                           // the typed text of a form row (the name, a field)
    int sf_rows() const;
    void begin_search();                                           // '/' in the Search tab: the form when a library is a source, else the one text prompt (RETURN always asks for a name)
    void begin_search_form();
    Result sf_run();
    void sf_open_results(int src);
    void begin_rename(const char *path);
    void begin_confirm_job(const Task *t, const char *question);
    Result activate(bool primary = false, bool all = false);                         // primary: a file's first action at once, without the list of its actions
    Result run_action(const FileAction &a, const char *file, int32_t ask = 0);
    Result run_action_inner(const FileAction &a, const char *file, int32_t ask);
    // T239.3 D41 (review): an action whose file is still being fetched (CoreFs::file_loading) stays pending: the status row says what the fetch does, pump() runs the action again every 300 ms until it
    // does not report a fetch any more. A key that leaves the folder, or a new open of the menu, cancels it.
    bool pend_tick(uint32_t now);
    void pend_begin();
    void pend_end(const FileAction &a, const char *file, int32_t ask);
    FileAction pend_act_ = {};                                     // a copy: the action may be one of filt_, which the next action sheet fills anew (review)
    char pend_label_[48] = "";                                     // pend_act_.label points here: its text, kept
    bool pend_on_ = false;
    char pend_file_[FS_PATH_MAX] = "";
    char pend_dir_[200] = "";
    int32_t pend_ask_ = 0;
    uint32_t pend_t_ = 0;
    Result ui_action(const char *op, const char *file);            // the actions every file has (view, copy, move, rename, delete, favourite)
    Result run_task(const Task &t, const char *text);
    Result run_node(const MenuNode &n, const char *label);
    Result run_fixed(const MenuEntry &e);                          // a fixed entry: its operation and the words of its answer are data (menutree.cc)
    Result hotkey(int k);                                          // a node whose ui `key` is this letter, from any screen (not in a sheet, a prompt or the terminal)
    // T104.1 D15 scope: a name prompt for a task that asks (Create, Save); the typed text goes to its B_TEXT binding
    void begin_text(const Task &t);
    void begin_text_setting(int idx);
    void begin_confirm(const MenuNode *n, const char *question);
    void begin_ask(const MenuNode *n, const char *prompt);
    void note_folder();                                            // the browser's folder becomes where a new image goes (CoreCtx::media_dir)
    bool text_key(int k, Result *out);
    // T122 §5: the folder browser a Create task opens first (directories only, "Create here" on top), and the job that follows the name
    void begin_pick(const Task &t);
    void begin_pick_file(int idx);                                 // T185 D4: the file selector of a setting of kind 3 (folders and files, "no file" on top)
    void begin_set_save();                                         // T180 D5: the name prompt of "Save media set..."
    void begin_set_load();                                         // T180 D5: the list of the sets of "Load media set..."
    void begin_pick_modes();                                       // T187 D5: the Picture row's page - the modes (T235: every row at 50 and 60 Hz, the screen's marked); browsing applies nothing
    void pick_modes_refresh(bool keep_cursor);
    bool picture_row(const MenuEntry &e) const;                    // the entry is the Picture setting (RETURN opens the page; LEFT / RIGHT do nothing)
    void pick_status();
    void start_path(char *out, int n) const;                       // where the folder browser starts: the menu's folder, the last disk's, the first stick
    void load_pick();
    void pick_release();
    bool pick_key(int k, Result *out);
    Result pick_take(const char *path);                            // T185 D4: the selector chose this file (or "" for none) for setting pick_set_
    const char *setting_label(int idx) const { const Setting &s = c_->desc->settings[idx]; return s.label ? s.label : s.key; }
    Result start_job(const Task &t, const char *name, bool bare = false);
    void job_text(char *left, int ln, char *right, int rn, char *sub, int sn) const;
    void job_busy(char *out, int n) const;                         // T262 P4: the caption of a node's job
    const FileAction *chooser_action(int i) const { return &chooser_acts_[i]; }
    Result start_action(const FileAction &a, const char *file);
    // T104.2 D6: the address prompt of an action with `ask` - hex digits, BACK, RETURN, ESC. Starts on the file's own word.
    void begin_entry(const FileAction &a, const char *file);
    bool entry_key(int k, Result *out);
    void entry_text(char *out, int n) const;
    void move(int d);
    void page(int dir);                                            // T180 D7: one page up (-1) or down (1); in the F3 browser a page is the rows the list shows
    Result change_setting(int dir);
    void toast(const char *s);
    bool node_ready(const MenuNode &n) const;                       // a contributed node's condition holds
    const MenuTab &cur() const { return tree_.a[area_].tabs[tab_]; }
    bool is_browser() const;                                       // the open tab is the file browser
    bool on_setting() const;                                        // the selected row is a setting with values: left/right change it
    bool is_settings() const;                                      // ... holds settings, where left/right change a value
    void go(int area, int tab);
    void settle();                                                 // the selection of a screen starts on something that takes the focus
    bool goto_tab_id(const char *id);                              // a tab of the open area by its id
    Result fkey_node(const char *name);

    CoreCtx *c_;
    MenuHost *h_;
    MenuTree tree_;
    int area_, tab_;
    int sel_[MENU_AREAS_MAX][MENU_TABS_MAX];
    int first_[MENU_AREAS_MAX][MENU_TABS_MAX];                      // the first row of the window each tab last showed
    void unheading();                                               // the selection is not on a heading
    void build_tree();                                              // menu_build_for, then the rows a flat list leaves out (a hide node, a show_state setting that is off)
    uint32_t show_sig() const;                                      // which show_state settings hold now
    bool sync_show();                                               // a show_state flipped: the tree is made again and the selection keeps its row (true: it was)
    uint8_t ran_ = 0;                                               // the intent apply_run last executed (MI_NONE: none)
    bool go_ran_ = false;                                           // go() ran an operation since apply_run began (app enter, screen start, page open)
    const char *setting_lock(const Setting &s) const;               // T228 R21: the text of the state that locks a setting row (Setting::lock_state), null: not locked
    uint32_t show_sig_ = 0;
    int last_tab_[MENU_AREAS_MAX];                                  // the tab an area was left on: its key comes back to it
    char status_[OP_MSG_MAX];                  // an operation's whole message (OpResult::msg)
    static const uint32_t TOAST_MS = 2000;                          // how long a toast stays
    char toast_[96] = "";
    mutable char foot_[240] = "";
    char detail_[120] = "";
    bool paged_ = false;
    void footer_legend_keys(char *out, int n);
    uint32_t toast_until_ = 0, toast_rev_ = 0;
    bool status_bad_;
    bool resolve_spec(const char *spec, int *ra, int *rt) const;
    bool goto_redraw_ = false;                                     // a goto_screen moved the open menu: pump() says the screen changed, once
    char pend_goto_[48] = "";                                      // goto_when_open: the place open() goes to
    char pend_say_[OP_MSG_MAX] = "";
    bool pend_say_bad_ = false;
    // the file manager: the Library's model and its file operations (filemgr.h)
    FileMgr fm_;
    bool chooser_;
    int chooser_ft_;
    char chooser_file_[FS_PATH_MAX];
    int chooser_sel_;
    int page_rows_;                                                // the rows the F3 list shows, told by the screen (set_page_rows); 10 until it has
    bool search_edit_;
    uint32_t last_relist_ = 0, last_live_ = 0, last_show_ = 0;                     // the pump: when the folder was last read again, when a live register was last shown
    // a copy of the file type's actions plus the ones every file has (view, copy, move, rename, delete, favourite)
    enum { CHOOSER_MAX = 300 };                                    // T228 R21: a tune with up to 256 songs lists them all, as FW's action list does
    FileAction chooser_buf_[CHOOSER_MAX];
    char chooser_lbl_[CHOOSER_MAX][16];                            // the label of a repeated entry ("Play Song 12")
    int repeat_count(const FileAction &a, const char *file);
    bool folder_sheet_;
    int drive_slot_ = -1;                                          // T212 D5: the media slot whose list (the "drive" page) is open
    int mount_slot_ = -1;                                          // ... whose Library mount is under way: the Library lists its types, RETURN on a file mounts it there
    char drive_title_[24] = "";
    Result open_drive(int slot);
    Result drive_row(const MenuEntry &e);
    void end_mount();
    Task node_task_;                                               // a node that runs as a job (ui `job`) or picks a folder first (ui `pick`)
    char fav_label_[2][24];
    enum { FILT_MAX = 16 };
    FileAction filt_[FILT_MAX];                                    // the actions of a file type that fit this file's size
    const FileAction *chooser_acts_;                               // the chooser's rows: the file type's actions, or the image's inner ones
    int chooser_n_;
    bool entry_text_mode_;
    const Task *entry_task_;
    LineEdit entry_le_;                                            // the text being typed: buffer, cursor, limit and window (app/edit; T180 D8)
    int learn_ = -1;                                               // T207: >= 0: the hotkey item or pad-key row (setting index) whose key is learned from the physical keys; entry_ is up
    HkLearn lrn_;
    void begin_learn(int set);
    bool learn_poll();                                             // one look at the physical keys while learn_ is up; true: the screen changed
    const MenuFlow *flow_q_;                                     // the question of a module's flow is up (confirm_q_ holds it)
    const MenuNode *confirm_node_;
    bool remote_q_ = false;                                        // T192.1: a remote request's confirmation is up (confirm_q_ holds it)
    int remote_ans_ = -1;
    void item_ident(const MenuEntry &e, Item *it) const;
    const Task *confirm_task_;                                     // a file job waiting for RETURN (delete)
    bool entry_rename_;
    char rename_path_[FS_PATH_MAX];                                 // a node waiting for RETURN (its ui `confirm` is the question); null: none
    char confirm_q_[120];
    const MenuNode *ask_node_;                                     // a node that asks for a name before it runs (ui `ask`); null: none
    char ask_q_[80];
    bool ask_any_ = false;                                         // the node's `ask_any`: the text is free (a search, a message), not a file name
    void entry_load(const char *text, int max);                    // the text field holds `text` (cut to max), the cursor behind it
    int entry_set_;                                                // >= 0: the text being typed is this setting's (a text setting)
    void entry_row(char *right, int rn, char mask = 0);                     // the entry's text and cursor as the row's value, scrolled
    // T180 D5: the name of a media set being typed (entry_ is up), and the list of the sets (pick_ is up, the rows are the sets, no "Create here")
    bool entry_mset_;
    bool pick_sets_;
    // T228 R21: a pick list made at run time (picklist.h): pick_ is up, pl_ holds the rows, pick_sel_ is the cursor. The rows are re-read from the state pl_src_ while it is up
    // (a scan that finishes). RETURN on a choice, by pl_mode_: PL_OP (an operation asked: pl_op_ is called with the phase and the value, or a setting (pl_set_ >= 0) takes the value as its text),
    // PL_NODE (a node with ui `pick = "@state"`, T262 P2: the node's operation runs with the value bound by B_TEXT), PL_LIST (a node of kind `list`, T262 P1: pl_op_ is called with the index of the
    // row; a last row that starts with '+' calls pl_more_ and reads the rows again).
    enum { PL_OP = 0, PL_NODE = 1, PL_LIST = 2 };
    bool pick_dyn_ = false;
    PickRows pl_ = {};
    char pl_src_[40] = "", pl_title_[80] = "", pl_op_[40] = "", pl_more_[40] = "";
    int pl_phase_ = 0, pl_set_ = -1, pl_mode_ = PL_OP;
    const MenuNode *pl_node_ = 0;
    bool pl_from_search_ = false;                                  // T262 D4: the list on screen is the results of the search form's source: <- goes back to the form
    bool sf_kept_ = false;                                         // the form is up again and pl_ still holds those results (the form's Results row opens them)
    int sf_res_sel_ = 0;                                           // the cursor the results had when the form was taken back
    void sf_back_to_form();                                        // <- from the results: the form again, the results kept
    void sf_drop_kept();                                           // the results the form kept are given back
    uint32_t pl_last_ = 0;
    void begin_pick_list(const char *title, const char *src, const char *op, int phase, int setting);
    void begin_pick_node(const MenuNode &n, const char *label, const char *src);   // T262 P2
    void begin_list_view(const char *title, const char *src, const char *op, const char *more);   // T262 P1
    bool pick_list_refresh();                                      // reads the rows again; true: they changed
    Result pick_list_take();
    Result list_call(const char *op, int index, const char *answer);   // the operation of a list row: it gets the arguments it declares (index, answer)
    bool pl_row_at(int i, PickRow *r) const { if (i < 0 || i >= pl_.n) return false; picklist_get(pl_, i, r); return true; }
    // T228 R21, FW config_menu.h:118-125: the drop-down of a setting ends with "Enter Manually" (the text editor of the same setting); an operation's list has no such row
    bool pick_manual() const { return pick_dyn_ && pl_set_ >= 0; }
    int pick_dyn_rows() const { return pick_manual() ? pl_.n + 1 : (pl_.n ? pl_.n : 1); }
    // T228 R21: the text prompt of an operation (ops_ask_text): RETURN calls the operation again with the text
    bool prompt_ = false, prompt_secret_ = false;
    char prompt_op_[40] = "", prompt_q_[80] = "";
    int prompt_phase_ = 0;
    void begin_prompt(const char *op, int phase, const char *q, int maxlen, bool secret);
    Result prompt_done(const char *answer);
    bool pick_modes_ = false;                                       // T187 D5: pick_ is the page of the screen's modes (pm_ holds the rows; pick_sel_ is the cursor)
    PictureModes pm_;
    char sets_[CONFIGS_MAX][CONFIG_NAME_MAX + 1];
    // the folder browser of a Create task
    bool pick_;
    const Task *pick_task_;
    int pick_set_;                                                 // >= 0: the selector chooses the file of this setting (pick_task_ is null)
    char pick_path_[200];
    FsEntry *pick_list_;                                           // the folder browser's entries: from the heap while it is open (listgrow.h)
    int pick_cap_;
    bool pick_more_;                                               // the folder is longer than the budget allows
    int pick_n_, pick_sel_;
    bool pick_ok_;
    // the job of a Create task: stepped by tick(), one chunk at a time
    bool busy_, job_started_, job_bare_;
    bool job_node_, job_seen_pct_;                                 // T262 P4: the job is a node's (its caption is the node's, not a file write's); the operation has reported a percentage above 0
    char node_busy_[64];                                           // the node's `busy` spec (ui attribute), "": the node's label
    bool app_mode_;
    char follow_last_[40];
    bool close_req_;
    bool pump_close_;
    const Task *job_task_;
    char job_name_[FS_PATH_MAX];
    // the address prompt
    bool entry_;
    const FileAction *entry_act_;
    char entry_file_[FS_PATH_MAX];
    uint16_t entry_def_;
    char entry_digits_[5];
    int entry_n_;
    bool entry_fresh_;
    // a wizard of a module (menuflows.h)
    const MenuWizard *wizard() const;                              // the one that is up, null when none
    void wizard_key(int intent);
};

#endif
