// The core menu's key table (menuintent.h): which key is which intent in which stage. This and the three raw-input handlers of menuctl.cc are the only places
// that know a `KEY_*`. A row is data; what an intent does is the controller's (navigation, dialogs, rows) or an operation's (the `op` of the row).
#include "menuintent.h"
#include "keyboard.h"
#include <string.h>

#define ANY -1, -1
#define K(c) (c), (c)
#define PRINT 0x21, 0x7E
#define ROW(stage, ...) { stage, __VA_ARGS__ }
#define F8KEYS(stage, intent) \
    ROW(stage, K(KEY_F1), intent, 1, 0, 0, 0), ROW(stage, K(KEY_F2), intent, 2, 0, 0, 0), ROW(stage, K(KEY_F3), intent, 3, 0, 0, 0), ROW(stage, K(KEY_F4), intent, 4, 0, 0, 0), \
    ROW(stage, K(KEY_F5), intent, 5, 0, 0, 0), ROW(stage, K(KEY_F6), intent, 6, 0, 0, 0), ROW(stage, K(KEY_F7), intent, 7, 0, 0, 0), ROW(stage, K(KEY_F8), intent, 8, 0, 0, 0)
#define LEAVEKEYS(stage, intent) \
    ROW(stage, K(KEY_F1), intent, 0, 0, 0, 0), ROW(stage, K(KEY_F2), intent, 0, 0, 0, 0), ROW(stage, K(KEY_F3), intent, 0, 0, 0, 0), ROW(stage, K(KEY_F4), intent, 0, 0, 0, 0), \
    ROW(stage, K(KEY_F5), intent, 0, 0, 0, 0), ROW(stage, K(KEY_F6), intent, 0, 0, 0, 0), ROW(stage, K(KEY_F7), intent, 0, 0, 0, 0), ROW(stage, K(KEY_F8), intent, 0, 0, 0, 0), \
    ROW(stage, K(KEY_BREAK), intent, 0, 0, 0, 0), ROW(stage, K(KEY_F10), intent, 0, 0, 0, 0), ROW(stage, K(KEY_SCRLOCK), intent, 0, 0, 0, 0), ROW(stage, K(KEY_MENU), intent, 0, 0, 0, 0)
#define CLOSEKEYS(stage, intent) \
    ROW(stage, K(KEY_BREAK), intent, 0, 0, 0, 0), ROW(stage, K(KEY_F10), intent, 0, 0, 0, 0), ROW(stage, K(KEY_SCRLOCK), intent, 0, 0, 0, 0), ROW(stage, K(KEY_MENU), intent, 0, 0, 0, 0)

static const KeyRow g_rows[] = {
    // the pad mapping wizard: RUN/STOP and ESC drop it, DEL and the left arrow ('`' in the key map) go back one, RETURN keeps the button
    ROW(KS_WIZARD, K(KEY_BREAK), MI_WIZ_CANCEL, 0, 0, 0, 0),
    ROW(KS_WIZARD, K(KEY_ESCAPE), MI_WIZ_CANCEL, 0, 0, 0, 0),
    ROW(KS_WIZARD, K(KEY_BACK), MI_WIZ_BACK, 0, 0, 0, 0),
    ROW(KS_WIZARD, K(0x60), MI_WIZ_BACK, 0, 0, 0, 0),
    ROW(KS_WIZARD, K(KEY_RETURN), MI_WIZ_NEXT, 0, 0, 0, 0),

    // the C64's left-arrow key is the one step out; where the monitor owns the keys it is the monitor's
    ROW(KS_ALIAS, K(0x60), MI_AS, KEY_ESCAPE, 0, 0, 0),

    // the viewer
    ROW(KS_VIEWER, K(KEY_UP), MI_VIEW_SCROLL, VS_UP, 0, 0, 0),
    ROW(KS_VIEWER, K(KEY_DOWN), MI_VIEW_SCROLL, VS_DOWN, 0, 0, 0),
    ROW(KS_VIEWER, K(KEY_PAGEUP), MI_VIEW_SCROLL, VS_PAGE_UP, 0, 0, 0),
    ROW(KS_VIEWER, K(KEY_PAGEDOWN), MI_VIEW_SCROLL, VS_PAGE_DOWN, 0, 0, 0),
    ROW(KS_VIEWER, K(KEY_HOME), MI_VIEW_SCROLL, VS_HOME, 0, 0, 0),
    ROW(KS_VIEWER, K(KEY_END), MI_VIEW_SCROLL, VS_END, 0, 0, 0),
    ROW(KS_VIEWER, K(KEY_ESCAPE), MI_VIEW_CLOSE, 0, 0, 0, 0),
    ROW(KS_VIEWER, K(KEY_LEFT), MI_VIEW_CLOSE, 0, 0, 0, 0),
    LEAVEKEYS(KS_VIEWER, MI_VIEW_LEAVE),

    // the idle Search tab
    ROW(KS_SEARCH_IDLE, K(KEY_RETURN), MI_SEARCH_OPEN, 0, 0, 0, 0),

    // the sheet of a file's actions: a letter runs its item (1-3 stay the tab keys)
    ROW(KS_SHEET, '1', '3', MI_DECLINE, 0, 0, 0, 0),
    ROW(KS_SHEET, PRINT, MI_SHEET_LETTER, 0, 0, 0, 0),

    // an application's screens
    CLOSEKEYS(KS_APP, MI_APP_CLOSE),
    ROW(KS_APP, K(KEY_UP), MI_APP_GRID, 0, 0, 0, 0),
    ROW(KS_APP, K(KEY_DOWN), MI_APP_GRID, 0, 0, 0, 0),
    ROW(KS_APP, K(KEY_LEFT), MI_APP_GRID, 0, 0, 0, 0),
    ROW(KS_APP, K(KEY_RIGHT), MI_APP_GRID, 0, 0, 0, 0),
    ROW(KS_APP, K(KEY_PAGEUP), MI_APP_GRID, 0, 0, 0, 0),
    ROW(KS_APP, K(KEY_PAGEDOWN), MI_APP_GRID, 0, 0, 0, 0),
    ROW(KS_APP, K(KEY_HOME), MI_APP_GRID, 0, 0, 0, 0),
    ROW(KS_APP, K(KEY_END), MI_APP_GRID, 0, 0, 0, 0),
    ROW(KS_APP, K(KEY_ESCAPE), MI_APP_BACK, 0, 0, 0, 0),
    F8KEYS(KS_APP, MI_APP_FKEY),

    // an application's terminal: every key is the application's, only the keys that close the menu are the menu's
    CLOSEKEYS(KS_APP_TERM, MI_DECLINE),
    ROW(KS_APP_TERM, ANY, MI_APP_TERMKEY, 0, 0, 0, 0),

    // the monitor's and Timewarp's keys are the core's own (KS_CORE): its record's key table, tried here (menuctl.cc resolve_from)

    // a job runs: ESC drops it
    ROW(KS_BUSY, K(KEY_ESCAPE), MI_BUSY_CANCEL, 0, 0, 0, 0),

    // the picker and the text field / address prompt take every key themselves
    ROW(KS_PICK, ANY, MI_RAW, 0, 0, 0, 0),
    ROW(KS_ENTRY, ANY, MI_RAW, 0, 0, 0, 0),

    // the F3 browser: the bare + and - page the list; the volume there is SHIFT only
    ROW(KS_PAGING, K('+'), MI_PAGE_BROWSER, 1, 0, 0, 0),
    ROW(KS_PAGING, K('-'), MI_PAGE_BROWSER, -1, 0, 0, 0),

    // the volume keys: the C64 keyboard delivers SHIFT+ as '{' and SHIFT- as '}'; a USB keyboard SHIFT+= (and the keypad's +) as '+', SHIFT+- as '_', the bare - as '-'
    ROW(KS_VOLUME, K('{'), MI_OP, 1, "volume.change", KF_ARG | KF_STATUS | KF_TOAST | KF_UNSUPPORTED_DECLINES, 0),
    ROW(KS_VOLUME, K('+'), MI_OP, 1, "volume.change", KF_ARG | KF_STATUS | KF_TOAST | KF_UNSUPPORTED_DECLINES, 0),
    ROW(KS_VOLUME, K('}'), MI_OP, -1, "volume.change", KF_ARG | KF_STATUS | KF_TOAST | KF_UNSUPPORTED_DECLINES, 0),
    ROW(KS_VOLUME, K('_'), MI_OP, -1, "volume.change", KF_ARG | KF_STATUS | KF_TOAST | KF_UNSUPPORTED_DECLINES, 0),
    ROW(KS_VOLUME, K('-'), MI_OP, -1, "volume.change", KF_ARG | KF_STATUS | KF_TOAST | KF_UNSUPPORTED_DECLINES, 0),

    // the grammar of the menu: a function key picks an area, a number key picks a tab, cursor keys and RETURN are a complete alternative for every route
    F8KEYS(KS_MAIN, MI_AREA),
    ROW(KS_MAIN, '1', '8', MI_TAB, 0, 0, 0, 0),
    ROW(KS_MAIN, K(KEY_UP), MI_MOVE, -1, 0, 0, 0),
    ROW(KS_MAIN, K(KEY_DOWN), MI_MOVE, 1, 0, 0, 0),
    ROW(KS_MAIN, K(KEY_PAGEUP), MI_PAGE, -1, 0, 0, 0),
    ROW(KS_MAIN, K(KEY_PAGEDOWN), MI_PAGE, 1, 0, 0, 0),
    ROW(KS_MAIN, K(KEY_HOME), MI_MOVE, -100000, 0, 0, 0),
    ROW(KS_MAIN, K(KEY_END), MI_MOVE, 100000, 0, 0, 0),
    ROW(KS_MAIN, K(KEY_LEFT), MI_LEFT, KEY_ESCAPE, 0, 0, 0),
    ROW(KS_MAIN, K(KEY_RIGHT), MI_RIGHT, 0, 0, 0, 0),
    ROW(KS_MAIN, K(KEY_BACK), MI_DEL, 0, 0, 0, 0),
    ROW(KS_MAIN, K(KEY_SPACE), MI_SPACE, 0, 0, 0, 0),
    ROW(KS_MAIN, K(KEY_RETURN), MI_RETURN, 0, 0, 0, 0),
    ROW(KS_MAIN, K(KEY_CTRL_A), MI_MARK_ALL, 0, 0, 0, 0),
    ROW(KS_MAIN, K(KEY_CTRL_C), MI_COPY, 0, 0, 0, 0),
    ROW(KS_MAIN, K(KEY_CTRL_V), MI_PASTE, 0, 0, 0, 0),
    ROW(KS_MAIN, K(KEY_CTRL_N), MI_MARK_NONE, 0, 0, 0, 0),
    ROW(KS_MAIN, K(KEY_CTRL_Z), MI_CTRL, 'Z', 0, 0, 0),             // T228 R21 (FW machine_hotkeys.h:29-38): C= Z reboot, C= B power cycle, C= K power off, in every menu
    ROW(KS_MAIN, K(KEY_CTRL_B), MI_CTRL, 'B', 0, 0, 0),
    ROW(KS_MAIN, K(KEY_CTRL_K), MI_CTRL, 'K', 0, 0, 0),
    ROW(KS_MAIN, K(KEY_ESCAPE), MI_BACK, 0, 0, 0, 0),
    CLOSEKEYS(KS_MAIN, MI_CLOSE),
    ROW(KS_MAIN, K('/'), MI_SLASH, 0, 0, 0, 0),
    ROW(KS_MAIN, PRINT, MI_CHAR, 0, 0, 0, 0),
};

static const bool g_swallow[KS_COUNT] = {
    true,    // KS_WIZARD
    false,   // KS_ALIAS
    true,    // KS_VIEWER
    false,   // KS_SEARCH_IDLE
    false,   // KS_SHEET
    false,   // KS_APP
    false,   // KS_APP_TERM
    false,   // KS_CORE
    true,    // KS_BUSY
    false,   // KS_PICK
    false,   // KS_ENTRY
    false,   // KS_PAGING
    false,   // KS_VOLUME
    true,    // KS_MAIN: a key the grammar does not know does nothing
};

static const char *const g_names[MI_COUNT] = {
    "none", "decline", "swallow", "as", "area", "tab", "move", "page", "left", "right", "del", "space", "return", "back", "close", "leave", "mark_all", "mark_none", "copy", "paste",
    "slash", "char", "sheet_letter", "search_open", "page_browser", "op", "view_scroll", "view_close", "view_leave", "wiz_next", "wiz_back", "wiz_cancel",
    "app_close", "app_back", "app_grid", "app_fkey", "app_termkey", "busy_cancel", "raw", "ctrl"
};

const KeyRow *menu_keymap(int *n)
{
    *n = (int)(sizeof(g_rows) / sizeof(g_rows[0]));
    return g_rows;
}

const KeyRow *menu_keymap_next(int stage, int k, int *cursor, bool (*when_ok)(void *, const char *), void *user)
{
    const int n = (int)(sizeof(g_rows) / sizeof(g_rows[0]));
    for (int i = *cursor; i < n; i++) {
        const KeyRow &r = g_rows[i];
        if (r.stage != stage || !(r.lo < 0 || (k >= r.lo && k <= r.hi))) {
            continue;
        }
        if (r.when && when_ok && !when_ok(user, r.when)) {
            continue;
        }
        *cursor = i + 1;
        return &r;
    }
    *cursor = n;
    return 0;
}

bool menu_stage_swallows(int stage)
{
    return stage >= 0 && stage < KS_COUNT && g_swallow[stage];
}

const char *menu_intent_name(int id)
{
    return id >= 0 && id < MI_COUNT ? g_names[id] : "";
}

int menu_intent_by_name(const char *name)
{
    for (int i = 1; i < MI_COUNT; i++) {
        if (!strcmp(g_names[i], name)) {
            return i;
        }
    }
    return -1;
}

int menu_keymap_key_of(int intent, int arg)
{
    for (unsigned i = 0; i < sizeof(g_rows) / sizeof(g_rows[0]); i++) {
        if (g_rows[i].intent == intent && g_rows[i].arg == arg && g_rows[i].lo >= 0) {
            return g_rows[i].lo;
        }
    }
    return -1;
}

int menu_keymap_wasd(int k)
{
    switch (k) {
    case 'w': return KEY_UP;
    case 'a': return KEY_LEFT;
    case 's': return KEY_DOWN;
    case 'd': return KEY_RIGHT;
    }
    return k;
}
