// The core menu's input grammar as DATA (T192.3 S6, D23): a key is translated to an intent by a table, and the controller (menuctl.cc) only ever executes intents.
//
// A key meets the STAGES in order (the order the old 271-line key() tested its conditions in); a stage is live when the controller's state says so
// (MenuCtl::stage_on: a viewer is up, the monitor owns the keys, ...). In a live stage the rows are walked in order; the first row whose key range matches (and whose
// `when` state holds) names an intent. An intent either takes the key (the result is the key's result) or declines it (the walk goes on with the next row, then the next
// stage). A stage that swallows ends the walk with "nothing happened" when no row took the key. `KEY_*` literals live in this table (menukeytab.cc) and in the three
// raw-input handlers of the controller (the text field, the folder/file picker, the address prompt): nowhere else (tools/check_menuctl_gate.py).
//
// The same table serves every producer: the board's keyboard, a remote `key` intent and a remote intent by name ("i:up") all end in MenuCtl::apply.
#ifndef COREAPP_MENUINTENT_H
#define COREAPP_MENUINTENT_H

#include <stdint.h>

enum MenuStage {
    KS_WIZARD = 0,      // the pad mapping wizard asks for the keys
    KS_ALIAS,           // a key that is another key (the C64's left arrow is ESC), except where the monitor owns it
    KS_VIEWER,          // the file viewer (View, Hex View)
    KS_SEARCH_IDLE,     // the idle Search tab: RETURN opens the field
    KS_SHEET,           // the sheet of a file's actions: a letter runs its item
    KS_APP,             // an application's screens: its own keys before the core's grammar
    KS_APP_TERM,        // an application's terminal: every key is the application's
    KS_CORE,            // the key table of the core's own record (descrec.h section 18, MenuMerge::keys): the rows of the core's own key table, in the record's order
    KS_BUSY,            // a file is being written: ESC drops it, nothing else counts
    KS_PICK,            // the folder / file picker: its raw handler (not in the table)
    KS_ENTRY,           // the text field and the address prompt: their raw handler (not in the table)
    KS_PAGING,          // the F3 browser: bare + and - page the list
    KS_VOLUME,          // the volume keys, from every page of the idle menu
    KS_MAIN,            // the grammar of the menu: areas, tabs, cursor, RETURN, back
    KS_COUNT
};

enum MenuIntentId {
    MI_NONE = 0,
    MI_DECLINE,         // the row says: this stage does not hold the key - the next stage does (an intent that declines at run time goes to the next ROW instead)
    MI_SWALLOW,         // the key is taken and nothing happens
    MI_AS,              // the key is the key `arg` (re-enters the table)
    MI_AREA,            // the area that the key (F1 .. F8) names
    MI_TAB,             // the tab whose printed number is the key's digit
    MI_MOVE,            // the cursor: arg = steps (+-1, +-100000: the ends)
    MI_PAGE,            // the cursor by a page: arg = +-1
    MI_LEFT, MI_RIGHT,
    MI_DEL,             // DEL: the quick seek takes its last letter back
    MI_SPACE,           // SPACE: marks a file in the browser, else RETURN
    MI_RETURN,          // RETURN / activate the row under the cursor
    MI_BACK,            // one step out (ESC): a sheet, a seek, a folder, a tab, an area, the menu
    MI_CLOSE,           // the menu closes (RUN/STOP, F10, the menu button), unless the core cannot run without it
    MI_LEAVE,           // the menu hotkey: the menu closes at once (no key: a producer's own)
    MI_MARK_ALL, MI_MARK_NONE, MI_COPY, MI_PASTE,
    MI_SLASH,           // '/': the Search field, or the quick seek
    MI_CHAR,            // a printable key: the quick seek takes it, else a node's own letter
    MI_SHEET_LETTER,    // a letter of the file sheet runs its item
    MI_SEARCH_OPEN,     // RETURN on the idle Search tab
    MI_PAGE_BROWSER,    // bare + and - in the browser: arg = +-1
    MI_OP,              // the operation `op`: see KeyRow::flags
    MI_VIEW_SCROLL,     // the viewer: arg = VS_*
    MI_VIEW_CLOSE,      // the viewer closes
    MI_VIEW_LEAVE,      // the viewer closes and the key is pressed again (it leaves the menu or changes the area)
    MI_WIZ_NEXT, MI_WIZ_BACK, MI_WIZ_CANCEL,
    MI_APP_CLOSE,       // an application's screens: the closing keys (RUN/STOP, F10 ...), unless the tab says locked
    MI_APP_BACK,        // ESC in an application's screens
    MI_APP_GRID,        // the cursor keys scroll a memory view
    MI_APP_FKEY,        // a function key runs the node that names it (arg: its number)
    MI_APP_TERMKEY,     // a key of an application's terminal goes to the operation the tab names
    MI_BUSY_CANCEL,     // ESC while a job runs
    MI_RAW,             // the stage's raw handler takes the key (text field, picker, address prompt): not in the table
    MI_CTRL,            // T228 R21: C= + letter (arg) runs the node whose ui `ctrl` is that letter; Z is the fixed Restart core; declines where there is none
    MI_COUNT
};

enum { VS_UP = 0, VS_DOWN, VS_PAGE_UP, VS_PAGE_DOWN, VS_HOME, VS_END };

// KeyRow::flags
enum {
    KF_ARG = 1,         // MI_OP: the operation takes `value` = arg
    KF_KEYARG = 2,      // MI_OP: the operation takes `key` = the key pressed
    KF_STATUS = 4,      // MI_OP: the answer's text is the status line, bad when the operation failed
    KF_STATUS_OK = 8,   // MI_OP: the answer's text is the status line, never bad
    KF_TOAST = 16,      // MI_OP: the answer's text is also the line over every page (the volume keys)
    KF_CLOSE = 32,      // MI_OP: the menu closes after it
    KF_DIRECTIVE = 64,  // MI_OP: the answer's text is "close" or "goto SPEC" for the controller, not a message
    KF_DIRECT = 128,    // MI_OP: called on the menu task itself, not through the executor
    KF_UNSUPPORTED_DECLINES = 256   // MI_OP: an operation that answers "unsupported" leaves the key to the next row
};

struct KeyRow {
    uint8_t stage;
    int16_t lo, hi;     // the keys it takes, inclusive; lo -1: any key
    uint8_t intent;
    int32_t arg;
    const char *op;     // MI_OP
    uint16_t flags;
    const char *when;   // a state name: the row counts only while that state has text ("!name": while it has none)
};

// A row of the core's key table (T239.3 base 3.3, descrec.h section 18): the KeyRow (its stage is KS_CORE, lo -1: any key) and where it applies - `layout` names the layout of the
// tab that is shown (menuctl.cc layout_is), `node` the id of the shown tab; exactly one is set. Both point into the record.
struct CoreKey {
    const char *layout;
    const char *node;
    KeyRow row;
    const char *legend;   // T262 P6: the label the foot legend names this key by (descrec.h section 18, the legend table behind the rows); null: the row is not in the legend
};

// The row number `*cursor` onwards of the stage whose range holds `k`, or null. `when_ok(user, name)` asks a state.
const KeyRow *menu_keymap_next(int stage, int k, int *cursor, bool (*when_ok)(void *user, const char *name), void *user);
// The table as a whole (tests, the gate): rows and count.
const KeyRow *menu_keymap(int *n);
const char *menu_intent_name(int id);
int menu_intent_by_name(const char *name);
bool menu_stage_swallows(int stage);                  // a live stage in which no row took the key ends the walk: nothing happens
// The key of the first row with this intent and argument (an area by its number, a tab by its digit), -1 when none: a remote intent by name needs it.
int menu_keymap_key_of(int intent, int arg);

// One translated key: what a producer sends. `stage`/`row` say where the table walk stands; a row that declines continues from the next one.
struct MenuIntent {
    uint8_t id;
    int32_t arg;
    int key;            // the key it came from (areas and letters need it)
    int8_t stage;
    int16_t row;        // the row of the table (-1: no row: the stage's own default)
};

#endif
