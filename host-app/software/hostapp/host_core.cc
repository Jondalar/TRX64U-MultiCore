// The host application's headless core: the key map, the intents' names and the generated legends (host_core.h).
#include "host_core.h"
#include "keyboard.h"
#include <string.h>
#include <stdio.h>
#include <strings.h>
#include "configlist.h"
#include "handover.h"

// ---- the intents' names ------------------------------------------------------------------------------------------------------------------
static const char *const ACT_NAME[] = {
#define HC_X(e, n) n,
    HC_ACT_LIST(HC_X)
#undef HC_X
};

const char *hc_act_name(int act) { return act > 0 && act < HCA_N ? ACT_NAME[act] : ""; }

int hc_act_by_name(const char *name)
{
    if (!name || !name[0]) {
        return HCA_NONE;
    }
    for (int i = 1; i < HCA_N; i++) {
        if (!strcmp(ACT_NAME[i], name)) {
            return i;
        }
    }
    return HCA_NONE;
}

// ---- the key map: the ONE table ----------------------------------------------------------------------------------------------------------
// A letter is two entries (the screen takes both cases); the first entry of an intent names its key in the legends. In a context the first matching entry
// wins, so HC_KEY_ANY goes last. The global entries are consulted before the context's own (not in HCX_PICTURE).
#define LET(ctx, ch, act) { ctx, ch, HCA_##act }, { ctx, (ch) - 'a' + 'A', HCA_##act }
#define K(ctx, key, act) { ctx, key, HCA_##act }

static const HcKey KEYMAP[] = {
    // every context
    K(HCX_GLOBAL, KEY_F1, NAV_MENU), K(HCX_GLOBAL, KEY_F3, NAV_BROWSE), K(HCX_GLOBAL, KEY_F5, NAV_EDIT), K(HCX_GLOBAL, KEY_F7, NAV_NET), K(HCX_GLOBAL, KEY_F8, NAV_SETTINGS),
    LET(HCX_GLOBAL, 'v', PICTURE_STEP),
    // the old web page's commands (T106): intents no key reaches; the service queues them from the page's form (web.start / default: the row is focused first; video / countdown: the argument is hc_ui_web_arg)
    K(HCX_GLOBAL, HC_KEY_NONE, WEB_START), K(HCX_GLOBAL, HC_KEY_NONE - 1, WEB_DEFAULT), K(HCX_GLOBAL, HC_KEY_NONE - 2, WEB_VIDEO), K(HCX_GLOBAL, HC_KEY_NONE - 3, WEB_COUNTDOWN),
    // a picture change waits: LEFT / ESC goes back at once (T187 D5, Q-D5.2), any other key keeps it (and is spent on that)
    K(HCX_PICTURE, KEY_LEFT, PICTURE_REVERT), K(HCX_PICTURE, KEY_ESCAPE, PICTURE_REVERT), K(HCX_PICTURE, KEY_BREAK, PICTURE_REVERT), K(HCX_PICTURE, HC_KEY_ANY, PICTURE_KEEP),
    // F1
    K(HCX_MENU, KEY_UP, CURSOR_UP), K(HCX_MENU, KEY_DOWN, CURSOR_DOWN), K(HCX_MENU, KEY_RETURN, ENTRY_START), LET(HCX_MENU, 'r', ENTRY_RECHECK),
    K(HCX_MENU, KEY_RIGHT, CONFIG_LIST), K(HCX_MENU, '+', MENU_PAGE_DOWN), K(HCX_MENU, '-', MENU_PAGE_UP), K(HCX_MENU, KEY_PAGEDOWN, MENU_PAGE_DOWN), K(HCX_MENU, KEY_PAGEUP, MENU_PAGE_UP),
    // the config overlay (T198): UP/DOWN pick, RETURN start, LEFT/ESC close, D auto, R rename, DEL delete (twice)
    K(HCX_CFG, KEY_UP, CURSOR_UP), K(HCX_CFG, KEY_DOWN, CURSOR_DOWN), K(HCX_CFG, KEY_RETURN, CONFIG_START), K(HCX_CFG, KEY_LEFT, BACK), K(HCX_CFG, KEY_ESCAPE, BACK),
    K(HCX_CFG, KEY_BREAK, BACK), LET(HCX_CFG, 'd', CONFIG_DEFAULT), LET(HCX_CFG, 'r', CONFIG_RENAME), K(HCX_CFG, KEY_DELETE, CONFIG_DELETE), K(HCX_CFG, KEY_BACK, CONFIG_DELETE),
    // F3
    K(HCX_BROWSE, KEY_RIGHT, CONFIG_LIST), K(HCX_BROWSE, KEY_UP, CURSOR_UP), K(HCX_BROWSE, KEY_DOWN, CURSOR_DOWN), K(HCX_BROWSE, '+', BROWSE_PAGE_DOWN), K(HCX_BROWSE, '-', BROWSE_PAGE_UP), K(HCX_BROWSE, KEY_PAGEDOWN, BROWSE_PAGE_DOWN), K(HCX_BROWSE, KEY_PAGEUP, BROWSE_PAGE_UP),
    K(HCX_BROWSE, KEY_LEFT, BROWSE_UP), K(HCX_BROWSE, KEY_RETURN, BROWSE_OPEN), K(HCX_BROWSE, KEY_DELETE, BROWSE_DELETE), K(HCX_BROWSE, KEY_BACK, BROWSE_DELETE),
    LET(HCX_BROWSE, 'c', BROWSE_COPY), LET(HCX_BROWSE, 'm', BROWSE_MOVE), LET(HCX_BROWSE, 'a', BROWSE_ADD), LET(HCX_BROWSE, 'p', BROWSE_CONVERT),
    LET(HCX_BROWSE, 'r', BROWSE_RESTORE), LET(HCX_BROWSE, 'u', BROWSE_UPDATE), LET(HCX_BROWSE, 'f', BROWSE_FORMAT), LET(HCX_BROWSE, 'z', BROWSE_UNZIP), LET(HCX_BROWSE, 's', BROWSE_START),
    // F3, the target pick of a copy or move
    K(HCX_BROWSE_PICK, KEY_UP, CURSOR_UP), K(HCX_BROWSE_PICK, KEY_DOWN, CURSOR_DOWN), K(HCX_BROWSE_PICK, '+', BROWSE_PAGE_DOWN), K(HCX_BROWSE_PICK, '-', BROWSE_PAGE_UP), K(HCX_BROWSE_PICK, KEY_PAGEDOWN, BROWSE_PAGE_DOWN), K(HCX_BROWSE_PICK, KEY_PAGEUP, BROWSE_PAGE_UP),
    K(HCX_BROWSE_PICK, KEY_LEFT, BROWSE_UP), K(HCX_BROWSE_PICK, KEY_RIGHT, BROWSE_ENTER), K(HCX_BROWSE_PICK, KEY_RETURN, BROWSE_OPEN),
    K(HCX_BROWSE_PICK, KEY_ESCAPE, BROWSE_PICK_CANCEL), K(HCX_BROWSE_PICK, KEY_BREAK, BROWSE_PICK_CANCEL),
    // F3, the pick of a .cfw/.ue2 to look into
    K(HCX_BROWSE_INSPECT, KEY_UP, CURSOR_UP), K(HCX_BROWSE_INSPECT, KEY_DOWN, CURSOR_DOWN), K(HCX_BROWSE_INSPECT, '+', BROWSE_PAGE_DOWN), K(HCX_BROWSE_INSPECT, '-', BROWSE_PAGE_UP), K(HCX_BROWSE_INSPECT, KEY_PAGEDOWN, BROWSE_PAGE_DOWN), K(HCX_BROWSE_INSPECT, KEY_PAGEUP, BROWSE_PAGE_UP),
    K(HCX_BROWSE_INSPECT, KEY_LEFT, BROWSE_UP), K(HCX_BROWSE_INSPECT, KEY_RIGHT, BROWSE_ENTER), K(HCX_BROWSE_INSPECT, KEY_RETURN, BROWSE_OPEN),
    K(HCX_BROWSE_INSPECT, KEY_ESCAPE, BROWSE_PICK_CANCEL), K(HCX_BROWSE_INSPECT, KEY_BREAK, BROWSE_PICK_CANCEL),
    // F3, "replace the file that is there?": RETURN says yes, every other key no
    K(HCX_BROWSE_ASK, KEY_RETURN, BROWSE_REPLACE), K(HCX_BROWSE_ASK, HC_KEY_ANY, BROWSE_REPLACE_CANCEL),
    // F5
    K(HCX_EDIT, KEY_UP, CURSOR_UP), K(HCX_EDIT, KEY_DOWN, CURSOR_DOWN), K(HCX_EDIT, '+', ENTRY_MOVE_UP), K(HCX_EDIT, '=', ENTRY_MOVE_UP), K(HCX_EDIT, '-', ENTRY_MOVE_DOWN),
    LET(HCX_EDIT, 'd', ENTRY_DEFAULT), K(HCX_EDIT, KEY_DELETE, ENTRY_REMOVE), K(HCX_EDIT, KEY_BACK, ENTRY_REMOVE), LET(HCX_EDIT, 'c', ENTRY_RENAME), K(HCX_EDIT, KEY_LEFT, ENTRY_DEFCFG_PREV), K(HCX_EDIT, KEY_RIGHT, ENTRY_DEFCFG_NEXT),
    // F8
    K(HCX_SET, KEY_UP, CURSOR_UP), K(HCX_SET, KEY_DOWN, CURSOR_DOWN), K(HCX_SET, KEY_LEFT, SET_DEC), K(HCX_SET, KEY_RIGHT, SET_INC), K(HCX_SET, KEY_RETURN, SET_ACTIVATE),
    K(HCX_SET, KEY_SPACE, SET_INC),
    // F8, the WiFi page
    K(HCX_WIFI, KEY_ESCAPE, BACK), K(HCX_WIFI, KEY_BREAK, BACK), K(HCX_WIFI, KEY_UP, CURSOR_UP), K(HCX_WIFI, KEY_DOWN, CURSOR_DOWN),
    K(HCX_WIFI, KEY_LEFT, WIFI_DEC), K(HCX_WIFI, KEY_RIGHT, WIFI_INC), K(HCX_WIFI, KEY_RETURN, WIFI_ACTIVATE), K(HCX_WIFI, KEY_SPACE, WIFI_INC),
    LET(HCX_WIFI, 's', WIFI_SCAN), LET(HCX_WIFI, 'm', WIFI_MANUAL), LET(HCX_WIFI, 'l', WIFI_LAST), LET(HCX_WIFI, 'd', WIFI_DISCONNECT), LET(HCX_WIFI, 'f', WIFI_FORGET),
    // F8, the Picture page (T187 D5): browsing applies nothing, RETURN uses the mode under the cursor
    K(HCX_PICMODES, KEY_UP, CURSOR_UP), K(HCX_PICMODES, KEY_DOWN, CURSOR_DOWN), K(HCX_PICMODES, KEY_RETURN, PM_APPLY), K(HCX_PICMODES, KEY_SPACE, PM_APPLY),
    K(HCX_PICMODES, KEY_LEFT, BACK), K(HCX_PICMODES, KEY_ESCAPE, BACK), K(HCX_PICMODES, KEY_BREAK, BACK),
    // F8, Cores and ROMs: the list, then the two questions (Y is yes; RETURN and every other key is no)
    K(HCX_CORES, KEY_UP, CURSOR_UP), K(HCX_CORES, KEY_DOWN, CURSOR_DOWN), K(HCX_CORES, KEY_ESCAPE, BACK), K(HCX_CORES, KEY_BREAK, BACK), K(HCX_CORES, KEY_LEFT, BACK),
    K(HCX_CORES, KEY_DELETE, CORES_DELETE), K(HCX_CORES, KEY_BACK, CORES_DELETE),
    K(HCX_CORES_ASK1, KEY_UP, CURSOR_UP), K(HCX_CORES_ASK1, KEY_DOWN, CURSOR_DOWN), LET(HCX_CORES_ASK1, 'y', CORES_YES), K(HCX_CORES_ASK1, KEY_RETURN, CORES_NO),
    K(HCX_CORES_ASK1, HC_KEY_ANY, CORES_NO),
    K(HCX_CORES_ASK2, KEY_UP, CURSOR_UP), K(HCX_CORES_ASK2, KEY_DOWN, CURSOR_DOWN), LET(HCX_CORES_ASK2, 'y', CORES_YES), K(HCX_CORES_ASK2, KEY_RETURN, CORES_NO),
    K(HCX_CORES_ASK2, HC_KEY_ANY, CORES_NO),
    // F7
    LET(HCX_NET, 'c', NET_COLORS),
    // the colour test: its pattern keys are lower case, X and x differ
    LET(HCX_COLORS, 'p', COLORS_REPORT), K(HCX_COLORS, 'x', COLORS_SINGLE), K(HCX_COLORS, 'X', COLORS_MULTI),
    K(HCX_COLORS, 'r', COLORS_PATTERN), K(HCX_COLORS, 'g', COLORS_PATTERN), K(HCX_COLORS, 'b', COLORS_PATTERN), K(HCX_COLORS, 'k', COLORS_PATTERN), K(HCX_COLORS, 'h', COLORS_PATTERN),
    K(HCX_COLORS, KEY_ESCAPE, COLORS_CLOSE), K(HCX_COLORS, KEY_BREAK, COLORS_CLOSE), K(HCX_COLORS, KEY_RETURN, COLORS_CLOSE),
};

const HcKey *hc_keymap(int *n)
{
    if (n) {
        *n = (int)(sizeof(KEYMAP) / sizeof(KEYMAP[0]));
    }
    return KEYMAP;
}

static int find_in(int ctx, int key)
{
    for (unsigned i = 0; i < sizeof(KEYMAP) / sizeof(KEYMAP[0]); i++) {
        if (KEYMAP[i].ctx == ctx && (KEYMAP[i].key == key || KEYMAP[i].key == HC_KEY_ANY)) {
            return KEYMAP[i].act;
        }
    }
    return HCA_NONE;
}

int hc_resolve(int ctx, int key)
{
    if (ctx != HCX_PICTURE) {
        int a = find_in(HCX_GLOBAL, key);
        if (a != HCA_NONE) {
            return a;
        }
    }
    return find_in(ctx, key);
}

bool hc_act_in_ctx(int ctx, int act)
{
    for (unsigned i = 0; i < sizeof(KEYMAP) / sizeof(KEYMAP[0]); i++) {
        if (KEYMAP[i].act == act && (KEYMAP[i].ctx == ctx || KEYMAP[i].ctx == HCX_GLOBAL)) {
            return true;
        }
    }
    return false;
}

int hc_back_act(int ctx) { return hc_resolve(ctx, KEY_ESCAPE); }

// ---- key names ---------------------------------------------------------------------------------------------------------------------------
static void name_of(int key, char *out, int cap)
{
    switch (key) {
    case KEY_RETURN: snprintf(out, cap, "RETURN"); return;
    case KEY_UP: snprintf(out, cap, "UP"); return;
    case KEY_DOWN: snprintf(out, cap, "DOWN"); return;
    case KEY_LEFT: snprintf(out, cap, "LEFT"); return;
    case KEY_RIGHT: snprintf(out, cap, "RIGHT"); return;
    case KEY_ESCAPE: case KEY_BREAK: snprintf(out, cap, "ESC"); return;
    case KEY_DELETE: case KEY_BACK: snprintf(out, cap, "DEL"); return;
    case KEY_SPACE: snprintf(out, cap, "SPACE"); return;
    case KEY_F1: snprintf(out, cap, "F1"); return;
    case KEY_F3: snprintf(out, cap, "F3"); return;
    case KEY_F5: snprintf(out, cap, "F5"); return;
    case KEY_F7: snprintf(out, cap, "F7"); return;
    case KEY_F8: snprintf(out, cap, "F8"); return;
    }
    if (key >= 'a' && key <= 'z') {
        snprintf(out, cap, "%c", key - 'a' + 'A');
    } else if (key >= 0x20 && key < 0x7F) {
        snprintf(out, cap, "%c", key);
    } else {
        snprintf(out, cap, "#%d", key);
    }
}

void hc_key_name(int ctx, int act, char *out, int cap)
{
    out[0] = 0;
    for (int pass = 0; pass < 2 && !out[0]; pass++) {
        int want = pass == 0 ? ctx : HCX_GLOBAL;
        for (unsigned i = 0; i < sizeof(KEYMAP) / sizeof(KEYMAP[0]); i++) {
            if (KEYMAP[i].ctx == want && KEYMAP[i].act == act && KEYMAP[i].key != HC_KEY_ANY) {
                name_of(KEYMAP[i].key, out, cap);
                break;
            }
        }
    }
}

// ---- the legends -------------------------------------------------------------------------------------------------------------------------
// A segment is a pattern whose each '%' takes the key name of the next intent in the footer's context; a pattern without '%' is a plain text. Segments are
// joined by four spaces. The words are the legend's; no key name is written here.
struct Seg {
    const char *pat;
    uint16_t a[3];
};
struct Foot {
    uint8_t ctx;                       // the context the key names are looked up in
    const Seg *seg;
    uint8_t n;
};

#define S1(p, a0) { p, { HCA_##a0, 0, 0 } }
#define S2(p, a0, a1) { p, { HCA_##a0, HCA_##a1, 0 } }
#define S3(p, a0, a1, a2) { p, { HCA_##a0, HCA_##a1, HCA_##a2 } }
#define SX(p) { p, { 0, 0, 0 } }
#define SEL S2("%/% select", CURSOR_UP, CURSOR_DOWN)
#define PIC S1("% picture", PICTURE_STEP)

static const Seg F_MENU[] = { SEL, S1("% start", ENTRY_START), S1("% configs", CONFIG_LIST), S1("% check entry", ENTRY_RECHECK), PIC, SX("any key stops the countdown") };
static const Seg F_MENU_PLAIN[] = { SEL, S1("% start", ENTRY_START), S1("% check entry", ENTRY_RECHECK), PIC, SX("any key stops the countdown") };
static const Seg F_BROWSE_PLAIN[] = { SEL, S1("% open", BROWSE_OPEN), S1("% add", BROWSE_ADD), S1("% start", BROWSE_START), S1("% copy", BROWSE_COPY),
                                      S1("% move", BROWSE_MOVE), S1("% twice deletes", BROWSE_DELETE), S1("% up", BROWSE_UP), PIC };
static const Seg F_BROWSE[] = { SEL, S1("% open", BROWSE_OPEN), S1("% add", BROWSE_ADD), S1("% start", BROWSE_START), S1("% configs", CONFIG_LIST), S1("% copy", BROWSE_COPY),
                                S1("% move", BROWSE_MOVE), S1("% twice deletes", BROWSE_DELETE), S1("% up", BROWSE_UP), PIC };
static const Seg F_BR_MEDIA[] = { SEL, S1("% open", BROWSE_OPEN), S1("% format as exFAT", BROWSE_FORMAT), PIC };
static const Seg F_BR_PLAIN[] = { SEL, S1("% open folder", BROWSE_OPEN), S1("% up a folder", BROWSE_UP), PIC };
static const Seg F_BR_FILE[] = { SEL, S1("% open folder", BROWSE_OPEN), S1("% copy", BROWSE_COPY), S1("% move", BROWSE_MOVE), S1("% twice deletes", BROWSE_DELETE),
                                 S1("% up a folder", BROWSE_UP), PIC };
static const Seg F_BR_DIR[] = { SEL, S1("% open folder", BROWSE_OPEN), S1("% twice deletes an empty folder", BROWSE_DELETE), S1("% up a folder", BROWSE_UP), PIC };
static const Seg F_BR_PICK_COPY[] = { SEL, S1("% opens a folder", BROWSE_OPEN), S1("% up a folder", BROWSE_UP), S1("% on .. or a file copies here", BROWSE_OPEN),
                                      S1("% cancels", BROWSE_PICK_CANCEL) };
static const Seg F_BR_PICK_MOVE[] = { SEL, S1("% opens a folder", BROWSE_OPEN), S1("% up a folder", BROWSE_UP), S1("% on .. or a file moves here", BROWSE_OPEN),
                                      S1("% cancels", BROWSE_PICK_CANCEL) };
static const Seg F_BR_ASK[] = { S1("% replaces the file", BROWSE_REPLACE), SX("any other key cancels") };
static const Seg F_BR_BACKUP[] = { SEL, S1("% add", BROWSE_ADD), S1("% start", BROWSE_START), S1("% restore", BROWSE_RESTORE), S1("% copy", BROWSE_COPY),
                                   S1("% move", BROWSE_MOVE), S1("% twice deletes", BROWSE_DELETE), S1("% up", BROWSE_UP), PIC };
static const Seg F_BR_TXU[] = { SEL, S1("% update", BROWSE_UPDATE), S1("% copy", BROWSE_COPY), S1("% move", BROWSE_MOVE), S1("% twice deletes", BROWSE_DELETE),
                                S1("% up a folder", BROWSE_UP), PIC };
static const Seg F_BR_ZIP[] = { SEL, S1("% unzip", BROWSE_UNZIP), S1("% copy", BROWSE_COPY), S1("% move", BROWSE_MOVE), S1("% twice deletes", BROWSE_DELETE),
                                S1("% up a folder", BROWSE_UP), PIC };
static const Seg F_BR_INSPECT[] = { SEL, S1("% open folder", BROWSE_ENTER), S1("% up a folder", BROWSE_UP), S1("% inspects a .cfw/.ue2", BROWSE_OPEN),
                                    S1("% back", BROWSE_PICK_CANCEL) };
static const Seg F_BR_CFW[] = { SEL, S1("% add", BROWSE_ADD), S1("% start", BROWSE_START), S1("% package", BROWSE_CONVERT), S1("% restore", BROWSE_RESTORE),
                                S1("% copy", BROWSE_COPY), S1("% move", BROWSE_MOVE), S1("% twice deletes", BROWSE_DELETE), S1("% up", BROWSE_UP), PIC };
static const Seg F_EDIT[] = { SEL, S2("%/% move", ENTRY_MOVE_UP, ENTRY_MOVE_DOWN), S1("% auto", ENTRY_DEFAULT), S1("% rename", ENTRY_RENAME),
                              S2("%/% config of this core", ENTRY_DEFCFG_PREV, ENTRY_DEFCFG_NEXT), S1("% twice removes", ENTRY_REMOVE) };
static const Seg F_EDIT_PLAIN[] = { SEL, S2("%/% move", ENTRY_MOVE_UP, ENTRY_MOVE_DOWN), S1("% auto", ENTRY_DEFAULT), S1("% rename", ENTRY_RENAME),
                                    S1("% twice removes", ENTRY_REMOVE) };   // the same without "config of this core", for a selected row that has none
static const Seg F_NET[] = { S1("% colours", NET_COLORS), PIC };
static const Seg F_SET[] = { SEL, S3("%/% or % change", SET_DEC, SET_INC, SET_ACTIVATE), PIC };
static const Seg F_CM_LIST[] = { S2("%/% scroll", CURSOR_UP, CURSOR_DOWN), S1("% delete the files that belong to no core", CORES_DELETE), S1("% back", BACK) };
static const Seg F_CM_EMPTY[] = { S2("%/% scroll", CURSOR_UP, CURSOR_DOWN), S1("% back", BACK) };
static const Seg F_CM_ASK1[] = { S1("% continues", CORES_YES), S1("% or any other key cancels (No)", CORES_NO) };
static const Seg F_CM_ASK2[] = { S1("% deletes the entries listed", CORES_YES), S1("% or any other key cancels (No)", CORES_NO) };
static const Seg F_WM[] = { SEL, S3("%/% or % change", WIFI_DEC, WIFI_INC, WIFI_ACTIVATE), S1("% AP list", WIFI_SCAN), S1("% enter AP", WIFI_MANUAL), S1("% last AP", WIFI_LAST),
                            S1("% disconnect", WIFI_DISCONNECT), S1("% forget", WIFI_FORGET), S1("% back", BACK) };
static const Seg F_PM[] = { SEL, S1("% use this mode", PM_APPLY), S1("% back", BACK) };

static const Seg F_CFG[] = { S2("%/% pick", CURSOR_UP, CURSOR_DOWN), S1("% start", CONFIG_START), S1("% back", BACK), S1("% auto", CONFIG_DEFAULT), S1("% rename", CONFIG_RENAME), S1("% twice deletes", CONFIG_DELETE) };

static const Seg F_CFG_PLAIN[] = { S2("%/% pick", CURSOR_UP, CURSOR_DOWN), S1("% start", CONFIG_START), S1("% back", BACK) };   // "Plain start": nothing to default, rename or delete

#define FT(ctx, arr) { ctx, arr, (uint8_t)(sizeof(arr) / sizeof(arr[0])) }
static const Foot FOOTS[HCF_N] = {
    FT(HCX_MENU, F_MENU), FT(HCX_BROWSE, F_BROWSE), FT(HCX_BROWSE, F_BR_MEDIA), FT(HCX_BROWSE, F_BR_PLAIN), FT(HCX_BROWSE, F_BR_FILE), FT(HCX_BROWSE, F_BR_DIR),
    FT(HCX_BROWSE_PICK, F_BR_PICK_COPY), FT(HCX_BROWSE_PICK, F_BR_PICK_MOVE), FT(HCX_BROWSE_ASK, F_BR_ASK), FT(HCX_BROWSE, F_BR_BACKUP), FT(HCX_BROWSE, F_BR_TXU),
    FT(HCX_BROWSE_INSPECT, F_BR_INSPECT), FT(HCX_BROWSE, F_BR_CFW), FT(HCX_EDIT, F_EDIT), FT(HCX_NET, F_NET), FT(HCX_SET, F_SET), FT(HCX_CORES, F_CM_LIST),
    FT(HCX_CORES_ASK1, F_CM_ASK1), FT(HCX_CORES_ASK2, F_CM_ASK2), FT(HCX_WIFI, F_WM), FT(HCX_CORES, F_CM_EMPTY), FT(HCX_PICMODES, F_PM), FT(HCX_CFG, F_CFG), FT(HCX_BROWSE, F_BR_ZIP),
    FT(HCX_MENU, F_MENU_PLAIN), FT(HCX_BROWSE, F_BROWSE_PLAIN), FT(HCX_CFG, F_CFG_PLAIN), FT(HCX_EDIT, F_EDIT_PLAIN),
};

#define FOOT_MAX 240
static char foot_buf[HCF_N][FOOT_MAX];
static bool foot_done[HCF_N];

static void build_foot(int f)
{
    const Foot &ft = FOOTS[f];
    char *o = foot_buf[f];
    int p = 0;
    o[0] = 0;
    for (int s = 0; s < ft.n; s++) {
        const Seg &sg = ft.seg[s];
        if (s) {
            p += snprintf(o + p, FOOT_MAX - p, "    ");
        }
        int ai = 0;
        for (const char *c = sg.pat; *c && p < FOOT_MAX - 1; c++) {
            if (*c == '%') {
                char nm[16];
                hc_key_name(ft.ctx, sg.a[ai++], nm, sizeof(nm));
                p += snprintf(o + p, FOOT_MAX - p, "%s", nm);
            } else {
                o[p++] = *c;
                o[p] = 0;
            }
        }
    }
}

const char *hc_foot(int foot)
{
    if (foot < 0 || foot >= HCF_N) {
        return "";
    }
    if (!foot_done[foot]) {
        build_foot(foot);
        foot_done[foot] = true;
    }
    return foot_buf[foot];
}

const char *hc_foot_of_view(int view)
{
    static const int F[] = { HCF_MENU, HCF_BROWSE, HCF_EDIT, HCF_NET, HCF_SET };
    return view >= 0 && view < 5 ? hc_foot(F[view]) : "";
}

const char *hc_nav_key(int view)
{
    static const int A[] = { HCA_NAV_MENU, HCA_NAV_BROWSE, HCA_NAV_EDIT, HCA_NAV_NET, HCA_NAV_SETTINGS };
    static char nm[5][8];
    if (view < 0 || view >= 5) {
        return "";
    }
    if (!nm[view][0]) {
        hc_key_name(HCX_GLOBAL, A[view], nm[view], sizeof(nm[view]));
    }
    return nm[view];
}

// ---- the boot menu ---------------------------------------------------------------------------------------------------------------------------
#include "flashlock.h"
#include "ftpstate.h"

namespace {
BootCfg g_cfg;
bool g_loaded, g_edited;
HcPlatform g_plat;
int g_pic_sys = VID_SYS_PAL50;                  // what the picture is set to now (the screen's mirror of hostvideo's state)
int g_pic_hdmi = -1;                            // -1: the screen was asked and nobody chose
char g_text[BOOT_MAX * 260 + 128];

void changed()
{
    if (g_plat.cfg_changed) {
        g_plat.cfg_changed();
    }
}
}  // namespace

void hc_init(const HcPlatform *p)
{
    g_plat = *p;
}

StartFs *hc_fs(void) { return g_plat.fs; }
uint32_t hc_now_ms(void) { return g_plat.now_ms ? g_plat.now_ms() : 0; }

const BootCfg &hc_cfg(void) { return g_cfg; }
bool hc_cfg_loaded(void) { return g_loaded; }
bool hc_cfg_edited(void) { return g_edited; }

void hc_cfg_clear(void)
{
    bootcfg_init(&g_cfg);
    g_loaded = false;
    g_edited = false;
}

void hc_cfg_reset(const char *text)
{
    bootcfg_init(&g_cfg);
    if (text) {
        bootcfg_parse(&g_cfg, text);
    }
    g_loaded = true;
}

int hc_cfg_save(void)
{
    changed();                                          // the entries changed: an arm on one of them does not hold
    g_edited = true;
    int n = bootcfg_format(&g_cfg, g_text, sizeof(g_text));
    if (n < 0 || !g_plat.cfg_save || !g_plat.cfg_save(g_text, n)) {
        return HCR_SAVE_FAILED;
    }
    return HCR_OK;
}

bool hc_cfg_readback_ok(int i)
{
    static char text[BOOT_MAX * 260 + 128];
    static BootCfg back;
    int n = g_plat.cfg_load ? g_plat.cfg_load(text, (int)sizeof(text) - 1) : -1;
    if (n < 0) {
        return false;
    }
    text[n] = 0;
    bootcfg_parse(&back, text);
    return i >= 0 && i < g_cfg.n && back.n == g_cfg.n && !strcmp(back.e[i].name, g_cfg.e[i].name);
}

bool hc_cfg_try_load(int *save)
{
    if (save) {
        *save = HCR_REFUSED;
    }
    int n = g_plat.cfg_load ? g_plat.cfg_load(g_text, (int)sizeof(g_text) - 1) : -1;
    if (n < 0) {
        return false;
    }
    g_text[n] = 0;
    bootcfg_parse(&g_cfg, g_text);
    g_loaded = true;
    changed();
    // T164 WP17: boot.cfg's `video=` is read once. The first start that finds a choice there and none in host.cfg moves it over and clears the line.
    if (hc_picture_load_default(g_cfg.vid_sys, g_cfg.vid_hdmi)) {
        g_cfg.vid_sys = -1;
        g_cfg.vid_hdmi = -1;
        int r = hc_cfg_save();
        if (save) {
            *save = r;
        }
    }
    return true;
}

int hc_entry_move(int i, int dir)
{
    bootcfg_move(&g_cfg, i, dir);
    return hc_cfg_save();
}

int hc_entry_default(int i)
{
    g_cfg.def = i;
    return hc_cfg_save();
}

int hc_entry_defcfg(int i, const char *name)
{
    if (!bootcfg_set_defcfg(&g_cfg, i, name)) {
        return HCR_REFUSED;
    }
    return hc_cfg_save();
}

static char g_start_cfg[CONFIG_NAME_MAX + 1];
void hc_start_config_set(const char *name)
{
    snprintf(g_start_cfg, sizeof(g_start_cfg), "%s", name ? name : "");
}

static bool g_start_safe;
void hc_start_safe_set(bool on) { g_start_safe = on; }

int hc_entry_remove(int i)
{
    bootcfg_remove(&g_cfg, i);
    return hc_cfg_save();
}

int hc_entry_rename(int i, const char *name)
{
    if (!bootcfg_rename(&g_cfg, i, name)) {
        return HCR_REFUSED;
    }
    return hc_cfg_save();
}

int hc_entry_add(const char *name, const char *path, const char *hash, unsigned size, unsigned mtime, unsigned hdr)
{
    if (!bootcfg_add(&g_cfg, name, path, hash, size, mtime, hdr)) {
        return HCR_REFUSED;
    }
    return hc_cfg_save();
}

int hc_entry_migrate(int i, StartFs &fs, bool fw)
{
    if (i < 0 || i >= g_cfg.n || !startcheck_migrate(g_cfg.e[i], fs, fw)) {
        return HCR_REFUSED;
    }
    int r = hc_cfg_save();
    g_edited = false;                                   // the host's own rewrite, not an edit by the person
    return r;
}

int hc_direct_toggle(void)
{
    g_cfg.direct = !g_cfg.direct;
    return hc_cfg_save();
}

int hc_autostart(bool on, int secs)
{
    bootcfg_autostart(&g_cfg, on, secs);
    return hc_cfg_save();
}

// ---- the settings that live in modules -------------------------------------------------------------------------------------------------------
bool hc_picture_set(int hdmi, char *why, int why_n) { return hv_user_set(hdmi, why, why_n); }
void hc_picture_init(const HostVideoIo *io) { hv_init(io); }
bool hc_picture_load_default(int boot_sys, int boot_hdmi) { return hv_load_default(boot_sys, boot_hdmi); }
void hc_picture_start(bool use_saved, int *sys, int *hdmi)
{
    hv_start(use_saved, &g_pic_sys, &g_pic_hdmi);
    if (sys) {
        *sys = g_pic_sys;
    }
    if (hdmi) {
        *hdmi = g_pic_hdmi;
    }
}
int hc_picture_sys(void) { return g_pic_sys; }
int hc_picture_hdmi(void) { return g_pic_hdmi; }
void hc_picture_sync(void)
{
    g_pic_sys = hv_state()->sys;
    g_pic_hdmi = hv_state()->hdmi;
}
void hc_picture_mirror_test(int sys, int hdmi)
{
    g_pic_sys = sys;
    g_pic_hdmi = hdmi;
}
HvEvent hc_picture_tick(void) { return hv_tick(); }
bool hc_picture_keep(void) { return hv_keep(); }
void hc_protect_set(bool on) { flashprot_set(on); }
void hc_ftp_set(bool on) { ftp_state_set(on); }

// ---- the start sequence ----------------------------------------------------------------------------------------------------------------------
#include "legacy.h"

namespace {
bool g_start_busy;
char g_lock_why[160];
struct StartGuard {
    bool own;
    StartGuard() : own(!g_start_busy) { g_start_busy = true; }
    ~StartGuard() { if (own) g_start_busy = false; }
};
}  // namespace

bool hc_start_running(void) { return g_start_busy; }

bool hc_lock_flash(const char **why)
{
    const char *m = "";
    if (flashprot_before_handover(&m)) {
        return true;
    }
    snprintf(g_lock_why, sizeof(g_lock_why), "the flash could not be protected (%s); nothing was started", m);
    *why = g_lock_why;
    return false;
}

const char *hc_start_sequence(const HcStartEntry &e, int mode, const HcStartHooks &h, HcStartOutcome *out)
{
    HcStartOutcome local;
    if (!out) {
        out = &local;
    }
    out->staged = false;
    char cfgname[CONFIG_NAME_MAX + 1];
    snprintf(cfgname, sizeof(cfgname), "%s", e.config && e.config[0] ? e.config : g_start_cfg);
    g_start_cfg[0] = 0;                                         // one-shot: a start that fails leaves nothing behind for the next one
    const bool safe = g_start_safe;                             // T224 D2: the same, for "Boot safe graphics mode"
    g_start_safe = false;
    StartGuard guard;
    if (!guard.own) {
        return HC_START_BUSY;
    }
    const bool direct = mode == HC_START_DIRECT;
    const char *why = NULL;
    h.lap(h.ctx, NULL);
    if (e.full) {
        LegacyPlan plan;
        // T115 D10: a menu entry was hashed once when it was added, so its start is a direct one: a scratch file whose stamp names this firmware is used as it is
        // (no image check, no flash compare); only a new or changed file is copied
        if (!legacy_stage(e.path, &plan, &why, h.progress, h.ctx, e.verify, !e.verify)) {
            return why;
        }
        out->staged = true;
        if (direct) {
            if (h.progress(0, 0, NULL, h.ctx)) {                // the last moment the start can stop with nothing loaded
                return CORE_ABORTED;
            }
            h.lap(h.ctx, "firmware ready");
        } else {
            h.lap(h.ctx, "firmware ready");
            int ab = h.progress(STEP_ICAP, 100, NULL, h.ctx);
            ab |= h.progress(STEP_HANDOVER, 0, NULL, h.ctx);
            ab |= h.progress(STEP_JUMP, 0, NULL, h.ctx);
            h.settle(h.ctx);
            if (ab) {
                return CORE_ABORTED;                            // the last point to stop: nothing is handed over after this check
            }
            h.pause_ms(h.ctx, 200);
            if (h.abort_key(h.ctx)) {
                return CORE_ABORTED;
            }
        }
        if (!hc_lock_flash(&why)) {
            return why;                                         // T152 D6: the flash could not be protected, the firmware is not started
        }
        h.before_jump(h.ctx, true);
        if (!cfw_is_name(e.path)) {   // T223 D4: the large core's application reads the choice from this file (the page of a .cfw start may not survive the bitstream change); written last, nothing can fail after it
            char text[STARTFILE_MAX];
            int tl = startfile_format(text, sizeof(text), e.path, cfgname, safe);
            bool ok = g_plat.start_file && tl > 0 && g_plat.start_file(e.path, text, tl);
            char ln[200];
            snprintf(ln, sizeof(ln), "[host] start file for %s: config \"%s\", safe %d: %s", e.path, cfgname, safe ? 1 : 0, ok ? "written" : "not written");
            if (g_plat.log) {
                g_plat.log(ln);
            }
        }
        legacy_jump(&plan);
    }
    core_set_start_safe(safe);
    core_set_start_config(cfgname);                             // T198 D7: every core start sets what the jump hands over ("" = plain)
    CorInfo ci;
    if (!core_stage(e.path, &ci, &why, h.progress, h.ctx, e.trusted, direct)) {
        return why;
    }
    out->staged = true;
    if (direct) {
        if (h.progress(0, 0, NULL, h.ctx)) {                    // the last moment the start can stop with nothing loaded (core_swap cuts the core off)
            return CORE_ABORTED;
        }
        h.lap(h.ctx, "bundle ready");
    } else {
        h.lap(h.ctx, "bundle ready");
        if (h.progress(STEP_VERIFY, 100, NULL, h.ctx)) {
            return CORE_ABORTED;
        }
    }
    h.lap(h.ctx, NULL);
    if (!core_swap(&ci, &why, direct ? NULL : h.progress, direct ? NULL : h.ctx)) {
        return why;
    }
    h.lap(h.ctx, "core loaded");
    if (!direct) {
        int ab = h.progress(STEP_ICAP, 100, NULL, h.ctx);
        ab |= h.progress(STEP_HANDOVER, 0, NULL, h.ctx);
        ab |= h.progress(STEP_JUMP, 0, NULL, h.ctx);
        h.settle(h.ctx);
        if (ab) {
            return CORE_LOADED_NOT_STARTED;                     // the last point to stop: nothing jumps after this check
        }
        h.pause_ms(h.ctx, 200);
        if (h.abort_key(h.ctx)) {
            return CORE_LOADED_NOT_STARTED;
        }
    }
    h.before_jump(h.ctx, false);
    core_jump(&ci);
}

// ---- the entries' state ----------------------------------------------------------------------------------------------------------------------
namespace {
struct Ent {
    EState st;
    CorInfo info;
    bool full;
};
Ent g_ent[BOOT_MAX];
uint8_t g_pend[BOOT_MAX];                               // 0 done, 1 = state and icon wait, 2 = the icon waits
bool g_mig_tried[BOOT_MAX];

void say(const char *line)
{
    if (g_plat.log) {
        g_plat.log(line);
    }
}

EState state_of_check(EntryCheck c)
{
    switch (c) {
    case EC_OK: return ES_OK;
    case EC_CHANGED: case EC_UNCHECKED: return ES_CHANGED;
    case EC_MISSING: return ES_MISSING;
    case EC_CONTRACT: return ES_CONTRACT;
    case EC_HOST: return ES_HOST;
    default: return ES_BAD;
    }
}

// T115: a .cfw as a row of the menu. The name comes from the bitstream header; CorInfo is only the carrier the rows read.
EState probe_full(CorSource &s, CorInfo &ci, cfw_result_t *res = 0)
{
    static CfwInfo cw;
    memset(&ci, 0, sizeof(ci));
    cfw_result_t r = cfw_probe(s, CFW_IDCODE_XC7A50T, cw);
    if (res) {
        *res = r;
    }
    if (r != CFW_OK) {
        return ES_FWBAD;
    }
    snprintf(ci.name, sizeof(ci.name), "%s", cw.name);
    snprintf(ci.fpga, sizeof(ci.fpga), "%s", cw.part);
    ci.file_size = cw.file_size;
    if (cw.from_cor) {                                          // T137 D1: who made it (the row's badge says foreign or backup)
        static CorInfo oc;
        if (cor_probe(s, oc) == COR_ERR_LARGE) {
            ci.origin = oc.origin;
        }
    }
    return ES_OK;
}
}  // namespace

EState hc_est(int i) { return i >= 0 && i < BOOT_MAX ? g_ent[i].st : ES_UNKNOWN; }
const CorInfo &hc_einfo(int i) { return g_ent[i < 0 || i >= BOOT_MAX ? 0 : i].info; }
bool hc_efull(int i) { return i >= 0 && i < BOOT_MAX && g_ent[i].full; }
// T223 D4: every entry takes configs and the rescue start but a foreign firmware (.cfw/.ue2): a large .cor is full firmware too, and gets them through the start file.
bool hc_ecfg_ok(int i) { return i >= 0 && i < g_cfg.n && !(g_ent[i].full && cfw_is_name(g_cfg.e[i].path)); }
void hc_entry_origin(int i, int origin) { if (i >= 0 && i < BOOT_MAX) g_ent[i].info.origin = origin; }

bool hc_startable(int i) { return i >= 0 && i < g_cfg.n && (g_ent[i].st == ES_OK || g_ent[i].st == ES_CHANGED || g_ent[i].st == ES_UNKNOWN); }

int hc_in_menu(const char *path)
{
    for (int i = 0; i < g_cfg.n; i++) {
        if (!strcmp(g_cfg.e[i].path, path)) {
            return i;
        }
    }
    return -1;
}

// T115: a row that is started as full firmware: a .cfw, or a .cor that is a LARGE core (T115 section 7).
bool hc_is_full(const char *path)
{
    if (cfw_is_name(path)) {
        return true;
    }
    size_t l = strlen(path);
    if (l < 4 || strcasecmp(path + l - 4, ".cor")) {
        return false;
    }
    static CorInfo ci;
    CorSource *s = g_plat.fs ? g_plat.fs->open(path) : 0;
    if (!s) {
        return false;
    }
    bool large = cor_probe(*s, ci) == COR_ERR_LARGE;
    delete s;
    return large;
}

// T221 D13: the version of a large core is in its .cor manifest; a .cfw names none (its row stays "full firmware")
static void large_version(const char *path, CorInfo &ci)
{
    static CorInfo lc;
    CorSource *s = g_plat.fs ? g_plat.fs->open(path) : 0;
    if (s && cor_probe(*s, lc) == COR_ERR_LARGE) {
        snprintf(ci.version, sizeof(ci.version), "%s", lc.version);
    }
    delete s;
}

void hc_probe_state(int i)
{
    Ent &e = g_ent[i];
    e.full = hc_is_full(g_cfg.e[i].path);
    if (e.full) {
        FileStat fst;
        EntryCheck ec = startcheck_firmware(g_cfg.e[i], *g_plat.fs, fst);
        memset(&e.info, 0, sizeof(e.info));
        if (ec == EC_MISSING) {
            e.st = ES_MISSING;
            return;
        }
        if (ec == EC_OK) {                           // nothing changed since adding: no bitstream is walked again
            snprintf(e.info.name, sizeof(e.info.name), "%s", g_cfg.e[i].name);
            e.st = ES_OK;
            large_version(g_cfg.e[i].path, e.info);
        } else {
            CorSource *s = g_plat.fs->open(g_cfg.e[i].path);
            e.st = s ? probe_full(*s, e.info) : ES_MISSING;
            delete s;
            large_version(g_cfg.e[i].path, e.info);
            if (e.st == ES_OK) {
                e.st = ES_CHANGED;
            }
        }
        return;
    }
    FileStat fst;
    EntryCheck ec = startcheck_entry(g_cfg.e[i], *g_plat.fs, e.info, fst);
    e.st = state_of_check(ec);
}

void hc_lazy_reset(void)
{
    for (int i = 0; i < BOOT_MAX; i++) {
        g_pend[i] = i < g_cfg.n ? 1 : 0;
        g_mig_tried[i] = false;
        g_ent[i].st = ES_UNKNOWN;
        g_ent[i].full = false;
        memset(&g_ent[i].info, 0, sizeof(g_ent[i].info));
    }
}

void hc_lazy_mark(int i)
{
    if (i >= 0 && i < g_cfg.n) {
        g_pend[i] = 1;
    }
}

// T223 D2: only the rows on screen are read; the default and the selected row always are (the countdown and the start wait for them).
static int g_win_first = 0, g_win_n = BOOT_MAX;
void hc_lazy_window(int first, int n) { g_win_first = first; g_win_n = n; }

int hc_lazy_next(int sel_hint)
{
    if (g_cfg.def >= 0 && g_cfg.def < g_cfg.n && g_pend[g_cfg.def]) {
        return g_cfg.def;                              // the default first: the countdown waits for nothing else
    }
    if (sel_hint >= 0 && sel_hint < g_cfg.n && g_pend[sel_hint]) {
        return sel_hint;
    }
    for (int i = g_win_first < 0 ? 0 : g_win_first; i < g_cfg.n && i < g_win_first + g_win_n; i++) {
        if (g_pend[i]) {
            return i;
        }
    }
    return -1;
}

// An entry of an older boot.cfg carries no mtime: the full check runs once and stores what the check needs. Not at the start and not while anything else
// waits; the whole file is read, so it takes as long as the hash does (seconds for a large package).
static bool migrate_one(int i, HcLazy *out)
{
    if (g_cfg.e[i].mtime || g_mig_tried[i] || g_ent[i].st == ES_MISSING || g_ent[i].st == ES_UNKNOWN) {
        return false;
    }
    g_mig_tried[i] = true;
    out->index = i;
    out->unit = HCL_MIGRATE;
    out->save = HCR_REFUSED;
    bool fw = hc_is_full(g_cfg.e[i].path);
    if (fw) {
        CorSource *fsrc = g_plat.fs->open(g_cfg.e[i].path);
        bool bad = !fsrc || probe_full(*fsrc, g_ent[i].info) != ES_OK;
        delete fsrc;
        if (bad) {
            return true;                             // not a usable firmware: hc_probe_state says so
        }
    }
    char b[200];
    snprintf(b, sizeof(b), "[host] boot.cfg entry %s has no stored check: full check once", g_cfg.e[i].path);
    say(b);
    int r = hc_entry_migrate(i, *g_plat.fs, fw);
    if (r != HCR_REFUSED) {
        out->save = r;
        hc_lazy_mark(i);
    }
    return true;
}

bool hc_lazy_step(bool quiet, int sel_hint, HcLazy *out)
{
    memset(out, 0, sizeof(*out));
    int i = hc_lazy_next(sel_hint);
    if (i < 0) {
        if (!quiet) {
            return false;
        }
        for (int k = 0; k < g_cfg.n; k++) {
            if (migrate_one(k, out)) {
                return true;
            }
        }
        return false;
    }
    out->index = i;
    if (g_pend[i] == 1) {
        hc_probe_state(i);
        g_pend[i] = (g_ent[i].st == ES_OK || g_ent[i].st == ES_CHANGED || g_ent[i].st == ES_CONTRACT || g_ent[i].st == ES_HOST) ? 2 : 0;
        out->unit = HCL_STATE;
        out->icon_next = g_pend[i] == 2;
    } else {
        out->unit = HCL_ICON;                            // the screen loads the picture, then says so
    }
    return true;
}

void hc_lazy_icon_done(int i)
{
    if (i >= 0 && i < BOOT_MAX) {
        g_pend[i] = 0;
    }
}

// ---- the countdown ---------------------------------------------------------------------------------------------------------------------------
namespace {
bool g_cd_run, g_cd_dead;
uint32_t g_cd_end, g_boot_ms;
}  // namespace

void hc_countdown_boot(uint32_t now) { g_boot_ms = now; }

int hc_countdown_tick(uint32_t t)
{
    int r = 0;
    if (!g_cd_dead && !g_cd_run && g_cfg.countdown > 0 && hc_startable(g_cfg.def)) {
        g_cd_run = true;                                // the countdown begins once the default can be started
        g_cd_end = t + g_cfg.countdown * 1000;
        r |= HCC_BEGAN;
        char b[120];
        snprintf(b, sizeof(b), "[host] countdown %d s for %s", g_cfg.countdown, g_cfg.e[g_cfg.def].name);
        say(b);
    }
    if (!g_cd_dead && !g_cd_run && (g_cfg.countdown == 0 || g_cfg.def < 0 || t - g_boot_ms > HC_BOOT_WAIT_MS)) {
        g_cd_dead = true;
        r |= HCC_GAVE_UP;
    }
    if (g_cd_run && (int32_t)(g_cd_end - t) <= 0) {
        g_cd_run = false;
        g_cd_dead = true;
        r |= HCC_EXPIRED;
    }
    return r;
}

void hc_countdown_stop(void)
{
    if (g_cd_run || !g_cd_dead) {
        say("[host] countdown stopped");
    }
    g_cd_run = false;
    g_cd_dead = true;
}

void hc_countdown_give_up(void) { g_cd_dead = true; }
bool hc_cd_running(void) { return g_cd_run; }
bool hc_cd_dead(void) { return g_cd_dead; }
int hc_cd_left_ms(uint32_t now) { return (int32_t)(g_cd_end - now); }
void hc_cd_set(bool run, bool dead, uint32_t end_ms) { g_cd_run = run; g_cd_dead = dead; g_cd_end = end_ms; }
