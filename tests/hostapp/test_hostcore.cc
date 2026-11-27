// T192.2 P1/P4 (T192 D23): the host's key map as ONE table (host_core.cc).
//  - T192.2-S10: the legends the screen shows are GENERATED from the table; they equal the footers the screen showed before the cut (the strings below are the
//    expected values, kept from the old hostui.cc).
//  - the table: no (context, key) twice, HC_KEY_ANY last in its context, every intent has a name and the names round-trip, every intent is bound somewhere.
//  - resolve: the keys of each context take the intent the old on_key took (a sample per context, the corner cases of the old code).
#include "host_core.h"
#include "keyboard.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <set>
#include <utility>
#include <vector>
#include "bootcfg.h"

// the stubs of hostcore_stubs.cc
extern int stub_pic_calls, stub_pic_last, stub_keep_calls, stub_protect, stub_ftp, stub_load_default_calls, stub_video_sys, stub_video_hdmi;
extern bool stub_pic_ok, stub_pending, stub_video_moves;
extern std::vector<std::string> stub_calls;
extern bool stub_stage_ok, stub_swap_ok, stub_lock_ok;
extern int stub_stage_cb_abort;
extern int stub_av_applied;
struct StubJump { };

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) { g_pass++; } else { g_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_S(a, b) do { std::string x_ = (a), y_ = (b); if (x_ == y_) { g_pass++; } else { g_fail++; printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, x_.c_str(), y_.c_str()); } } while (0)

static void legends()
{
    CHECK_S(hc_foot(HCF_MENU), "UP/DOWN select    RETURN start    RIGHT configs    R check entry    V picture    any key stops the countdown");
    CHECK_S(hc_foot(HCF_BROWSE), "UP/DOWN select    RETURN open    A add    S start    RIGHT configs    C copy    M move    DEL twice deletes    LEFT up    V picture");
    CHECK_S(hc_foot(HCF_EDIT), "UP/DOWN select    +/- move    D auto    C rename    LEFT/RIGHT config of this core    DEL twice removes");
    CHECK_S(hc_foot(HCF_NET), "C colours    V picture");
    CHECK_S(hc_foot(HCF_SET), "UP/DOWN select    LEFT/RIGHT or RETURN change    V picture");
    CHECK_S(hc_foot(HCF_BR_MEDIA), "UP/DOWN select    RETURN open    F format as exFAT    V picture");
    CHECK_S(hc_foot(HCF_BR_PLAIN), "UP/DOWN select    RETURN open folder    LEFT up a folder    V picture");
    CHECK_S(hc_foot(HCF_BR_FILE), "UP/DOWN select    RETURN open folder    C copy    M move    DEL twice deletes    LEFT up a folder    V picture");
    CHECK_S(hc_foot(HCF_BR_DIR), "UP/DOWN select    RETURN open folder    DEL twice deletes an empty folder    LEFT up a folder    V picture");
    CHECK_S(hc_foot(HCF_BR_PICK_COPY), "UP/DOWN select    RETURN opens a folder    LEFT up a folder    RETURN on .. or a file copies here    ESC cancels");
    CHECK_S(hc_foot(HCF_BR_PICK_MOVE), "UP/DOWN select    RETURN opens a folder    LEFT up a folder    RETURN on .. or a file moves here    ESC cancels");
    CHECK_S(hc_foot(HCF_BR_ASK), "RETURN replaces the file    any other key cancels");
    CHECK_S(hc_foot(HCF_BR_BACKUP), "UP/DOWN select    A add    S start    R restore    C copy    M move    DEL twice deletes    LEFT up    V picture");
    CHECK_S(hc_foot(HCF_BR_TXU), "UP/DOWN select    U update    C copy    M move    DEL twice deletes    LEFT up a folder    V picture");
    CHECK_S(hc_foot(HCF_BR_INSPECT), "UP/DOWN select    RIGHT open folder    LEFT up a folder    RETURN inspects a .cfw/.ue2    ESC back");
    CHECK_S(hc_foot(HCF_BR_CFW), "UP/DOWN select    A add    S start    P package    R restore    C copy    M move    DEL twice deletes    LEFT up    V picture");
    CHECK_S(hc_foot(HCF_CM_LIST), "UP/DOWN scroll    DEL delete the files that belong to no core    ESC back");
    CHECK_S(hc_foot(HCF_CM_ASK1), "Y continues    RETURN or any other key cancels (No)");
    CHECK_S(hc_foot(HCF_CM_ASK2), "Y deletes the entries listed    RETURN or any other key cancels (No)");
    CHECK_S(hc_foot(HCF_WM), "UP/DOWN select    LEFT/RIGHT or RETURN change    S AP list    M enter AP    L last AP    D disconnect    F forget    ESC back");
    CHECK_S(hc_foot(HCF_PM), "UP/DOWN select    RETURN use this mode    LEFT back");      // T187 D5: the key bar of the Picture page
    CHECK(hc_resolve(HCX_PICMODES, KEY_RETURN) == HCA_PM_APPLY && hc_resolve(HCX_PICMODES, KEY_LEFT) == HCA_BACK && hc_resolve(HCX_PICMODES, KEY_ESCAPE) == HCA_BACK
          && hc_resolve(HCX_PICMODES, KEY_RIGHT) == HCA_NONE && hc_resolve(HCX_PICMODES, 'x') == HCA_NONE);
    CHECK(hc_resolve(HCX_PICMODES, 'v') == HCA_PICTURE_STEP);                                   // V opens the page; on the page it is the global key again
    CHECK(hc_resolve(HCX_SET, KEY_LEFT) == HCA_SET_DEC);       // the closed Picture row is a row of the list: LEFT / RIGHT are consumed in settings_key (Q-D5.1)
    CHECK_S(hc_foot(HCF_CM_EMPTY), "UP/DOWN scroll    ESC back");
    CHECK_S(hc_nav_key(0), "F1");
    CHECK_S(hc_nav_key(1), "F3");
    CHECK_S(hc_nav_key(2), "F5");
    CHECK_S(hc_nav_key(3), "F7");
    CHECK_S(hc_nav_key(4), "F8");
    CHECK_S(hc_foot_of_view(2), hc_foot(HCF_EDIT));
}

static void table()
{
    int n;
    const HcKey *k = hc_keymap(&n);
    std::set<std::pair<int, int> > seen;
    int last_any[HCX_N + 1];
    for (int i = 0; i <= HCX_N; i++) {
        last_any[i] = -1;
    }
    for (int i = 0; i < n; i++) {
        CHECK(seen.insert(std::make_pair((int)k[i].ctx, k[i].key)).second);       // no (context, key) twice
        CHECK(k[i].act > HCA_NONE && k[i].act < HCA_N);
        if (k[i].key == HC_KEY_ANY) {
            last_any[k[i].ctx] = i;
        }
    }
    for (int i = 0; i < n; i++) {
        CHECK(last_any[k[i].ctx] < 0 || i <= last_any[k[i].ctx]);                     // HC_KEY_ANY is the last entry of its context
    }
    for (int a = 1; a < HCA_N; a++) {
        CHECK(hc_act_name(a)[0]);
        CHECK(hc_act_by_name(hc_act_name(a)) == a);
        bool bound = false;
        for (int i = 0; i < n; i++) {
            bound = bound || k[i].act == a;
        }
        CHECK(bound);                                                              // an intent no key reaches would be dead
    }
    CHECK(hc_act_by_name("no.such") == HCA_NONE);
    CHECK(HC_IS_ACT_KEY(HC_ACT_KEY(HCA_ENTRY_START)) && !HC_IS_ACT_KEY(KEY_F8) && HC_ACT_OF_KEY(HC_ACT_KEY(HCA_ENTRY_START)) == HCA_ENTRY_START);
}

static void resolve()
{
    // the global keys beat the context's, except while a picture change waits
    CHECK(hc_resolve(HCX_MENU, KEY_F3) == HCA_NAV_BROWSE);
    CHECK(hc_resolve(HCX_COLORS, KEY_F8) == HCA_NAV_SETTINGS);
    CHECK(hc_resolve(HCX_COLORS, 'v') == HCA_PICTURE_STEP && hc_resolve(HCX_COLORS, 'V') == HCA_PICTURE_STEP);       // V is the picture even in the colour test
    CHECK(hc_resolve(HCX_PICTURE, KEY_F3) == HCA_PICTURE_KEEP && hc_resolve(HCX_PICTURE, KEY_RETURN) == HCA_PICTURE_KEEP && hc_resolve(HCX_PICTURE, 'v') == HCA_PICTURE_KEEP
          && hc_resolve(HCX_PICTURE, KEY_LEFT) == HCA_PICTURE_REVERT && hc_resolve(HCX_PICTURE, KEY_ESCAPE) == HCA_PICTURE_REVERT);   // T187 D5: LEFT / ESC goes back, V keeps like any key
    // the menu
    CHECK(hc_resolve(HCX_MENU, KEY_RETURN) == HCA_ENTRY_START && hc_resolve(HCX_MENU, 'R') == HCA_ENTRY_RECHECK && hc_resolve(HCX_MENU, 'x') == HCA_NONE);
    // the browser: the normal mode, the pick, the inspect, the question
    CHECK(hc_resolve(HCX_BROWSE, KEY_DELETE) == HCA_BROWSE_DELETE && hc_resolve(HCX_BROWSE, KEY_BACK) == HCA_BROWSE_DELETE);
    CHECK(hc_resolve(HCX_BROWSE, KEY_RIGHT) == HCA_CONFIG_LIST);                                                // T198: RIGHT lists the configs of a core (the browser's hc_ui_input turns it into BROWSE_OPEN on a folder row; a pick enters through HCA_BROWSE_ENTER)
    CHECK(hc_resolve(HCX_BROWSE_PICK, KEY_RIGHT) == HCA_BROWSE_ENTER && hc_resolve(HCX_BROWSE_INSPECT, KEY_RIGHT) == HCA_BROWSE_ENTER);
    CHECK(hc_resolve(HCX_BROWSE_PICK, 'c') == HCA_NONE && hc_resolve(HCX_BROWSE_INSPECT, 'a') == HCA_NONE);           // the copy / add keys do nothing in a pick
    CHECK(hc_resolve(HCX_BROWSE_PICK, KEY_BREAK) == HCA_BROWSE_PICK_CANCEL && hc_resolve(HCX_BROWSE_INSPECT, KEY_ESCAPE) == HCA_BROWSE_PICK_CANCEL);
    CHECK(hc_resolve(HCX_BROWSE_ASK, KEY_RETURN) == HCA_BROWSE_REPLACE && hc_resolve(HCX_BROWSE_ASK, 'y') == HCA_BROWSE_REPLACE_CANCEL && hc_resolve(HCX_BROWSE_ASK, KEY_ESCAPE) == HCA_BROWSE_REPLACE_CANCEL);
    // the editor
    CHECK(hc_resolve(HCX_EDIT, '+') == HCA_ENTRY_MOVE_UP && hc_resolve(HCX_EDIT, '=') == HCA_ENTRY_MOVE_UP && hc_resolve(HCX_EDIT, '-') == HCA_ENTRY_MOVE_DOWN);
    CHECK(hc_resolve(HCX_EDIT, KEY_BACK) == HCA_ENTRY_REMOVE && hc_resolve(HCX_EDIT, 'D') == HCA_ENTRY_DEFAULT && hc_resolve(HCX_EDIT, 'C') == HCA_ENTRY_RENAME);
    // the settings: LEFT is -1, RIGHT / SPACE +1, RETURN +1 (its own intent for the legend)
    CHECK(hc_resolve(HCX_SET, KEY_LEFT) == HCA_SET_DEC && hc_resolve(HCX_SET, KEY_RIGHT) == HCA_SET_INC && hc_resolve(HCX_SET, KEY_SPACE) == HCA_SET_INC && hc_resolve(HCX_SET, KEY_RETURN) == HCA_SET_ACTIVATE);
    // the WiFi page and Cores and ROMs
    CHECK(hc_resolve(HCX_WIFI, KEY_BREAK) == HCA_BACK && hc_resolve(HCX_WIFI, 'F') == HCA_WIFI_FORGET && hc_resolve(HCX_WIFI, 'f') == HCA_WIFI_FORGET);
    CHECK(hc_resolve(HCX_CORES, KEY_LEFT) == HCA_BACK && hc_resolve(HCX_CORES, KEY_BACK) == HCA_CORES_DELETE);
    CHECK(hc_resolve(HCX_CORES_ASK1, 'Y') == HCA_CORES_YES && hc_resolve(HCX_CORES_ASK1, KEY_RETURN) == HCA_CORES_NO && hc_resolve(HCX_CORES_ASK1, 'q') == HCA_CORES_NO);
    CHECK(hc_resolve(HCX_CORES_ASK2, KEY_UP) == HCA_CURSOR_UP);                                                  // the list still scrolls while the question is up
    // the colour test: X and x differ, only the lower case patterns
    CHECK(hc_resolve(HCX_COLORS, 'x') == HCA_COLORS_SINGLE && hc_resolve(HCX_COLORS, 'X') == HCA_COLORS_MULTI && hc_resolve(HCX_COLORS, 'r') == HCA_COLORS_PATTERN);
    CHECK(hc_resolve(HCX_COLORS, 'R') == HCA_NONE && hc_resolve(HCX_COLORS, KEY_RETURN) == HCA_COLORS_CLOSE);
    CHECK(hc_resolve(HCX_NET, 'C') == HCA_NET_COLORS);
    // an intent that no key reaches in a context does nothing there
    CHECK(hc_act_in_ctx(HCX_WIFI, HCA_WIFI_SCAN) && !hc_act_in_ctx(HCX_SET, HCA_WIFI_SCAN) && hc_act_in_ctx(HCX_SET, HCA_NAV_MENU));
}

// ---- the boot menu commands (T192.2-S2): each one equals the old call path of hostui.cc on the same boot.cfg ----------------------------------------------
static std::string g_disk;                 // what the board's boot.cfg holds
static bool g_disk_ok = true, g_disk_save_ok = true;
static int g_saves, g_changed;
static int p_load(char *buf, int cap)
{
    if (!g_disk_ok) return -1;
    int n = (int)g_disk.size() < cap ? (int)g_disk.size() : cap;
    memcpy(buf, g_disk.data(), n);
    return n;
}
static bool p_save(const char *text, int n)
{
    g_saves++;
    if (!g_disk_save_ok) return false;
    g_disk.assign(text, n);
    return true;
}
static void p_changed() { g_changed++; }

static const char *FIXTURE =
    "countdown=5\nautostart_s=5\ndirect=0\ndefault=1\n"
    "core=/SD/a.cor|Alpha|aa|100|7|1\ncore=/SD/b.cor|Beta|bb|200|8|2\ncore=/SD/c.cor|Gamma|cc|300|9|3\n";

static std::string fmt(const BootCfg &c)
{
    char b[BOOT_MAX * 260 + 128];
    int n = bootcfg_format(&c, b, sizeof(b));
    return n < 0 ? std::string() : std::string(b, n);
}

static void commands()
{
    static const HcPlatform plat = { p_load, p_save, p_changed, 0, 0, 0 };
    hc_init(&plat);
    BootCfg ref;
    auto fresh = [&]() { bootcfg_init(&ref); bootcfg_parse(&ref, FIXTURE); hc_cfg_reset(FIXTURE); g_saves = g_changed = 0; g_disk.clear(); g_disk_ok = g_disk_save_ok = true; };

    // the start state: empty, not loaded, not edited; a reset loads
    hc_cfg_clear();
    CHECK(!hc_cfg_loaded() && !hc_cfg_edited() && hc_cfg().n == 0);
    fresh();
    CHECK(hc_cfg_loaded() && !hc_cfg_edited() && hc_cfg().n == 3 && fmt(hc_cfg()) == fmt(ref));

    // move: the old path was bootcfg_move then the save
    fresh();
    bootcfg_move(&ref, 1, -1);
    CHECK(hc_entry_move(1, -1) == HCR_OK && fmt(hc_cfg()) == fmt(ref) && g_disk == fmt(ref) && g_saves == 1 && g_changed == 1 && hc_cfg_edited());
    CHECK(hc_cfg().def == 0);                                                 // the default follows
    // default
    fresh();
    ref.def = 2;
    CHECK(hc_entry_default(2) == HCR_OK && fmt(hc_cfg()) == fmt(ref) && g_disk == fmt(ref));
    // remove
    fresh();
    bootcfg_remove(&ref, 0);
    CHECK(hc_entry_remove(0) == HCR_OK && fmt(hc_cfg()) == fmt(ref) && g_disk == fmt(ref) && hc_cfg().n == 2);
    // rename: a good name, then a refused one saves nothing
    fresh();
    bootcfg_rename(&ref, 2, "Gamma 2");
    CHECK(hc_entry_rename(2, "Gamma 2") == HCR_OK && fmt(hc_cfg()) == fmt(ref) && g_disk == fmt(ref));
    int saves = g_saves, changed = g_changed;
    CHECK(hc_entry_rename(2, "a|b") == HCR_REFUSED && fmt(hc_cfg()) == fmt(ref) && g_saves == saves && g_changed == changed);
    CHECK(hc_entry_rename(2, "") == HCR_REFUSED && hc_entry_rename(9, "x") == HCR_REFUSED);
    // add: a good entry; a name with '|' is refused and nothing is saved; a full menu is refused
    fresh();
    bootcfg_add(&ref, "Delta", "/SD/d.cor", "dd", 400, 10, 4);
    CHECK(hc_entry_add("Delta", "/SD/d.cor", "dd", 400, 10, 4) == HCR_OK && fmt(hc_cfg()) == fmt(ref) && g_disk == fmt(ref));
    saves = g_saves;
    CHECK(hc_entry_add("D|elta", "/SD/e.cor", "ee", 1, 1, 1) == HCR_REFUSED && g_saves == saves && hc_cfg().n == 4);
    for (int i = hc_cfg().n; i < BOOT_MAX; i++) {
        char nm[16], pa[32];
        snprintf(nm, sizeof(nm), "E%d", i);
        snprintf(pa, sizeof(pa), "/SD/x%d.cor", i);
        CHECK(hc_entry_add(nm, pa, "", 1, 1, 1) == HCR_OK);
    }
    CHECK(hc_entry_add("one too many", "/SD/y.cor", "", 1, 1, 1) == HCR_REFUSED && hc_cfg().n == BOOT_MAX);
    // T254 D7: A on a firmware whose entry still has the name A gave it before T254 (the .bit header's) renames the entry in the same
    // save; an entry with another name keeps it (T198 D17); the rename goes by the path, never by the name alone
    fresh();
    bootcfg_rename(&ref, 1, "Gideon 3.15");
    bootcfg_add(&ref, "Gideon 3.15", "/SD/b.cor", "", 5, 6, 0);
    CHECK(hc_entry_add("Gideon 3.15", "/SD/b.cor", "", 5, 6, 0, "Beta") == HCR_OK && fmt(hc_cfg()) == fmt(ref) && g_disk == fmt(ref) && g_saves == 1);
    fresh();
    bootcfg_add(&ref, "Gideon 3.15", "/SD/b.cor", "", 5, 6, 0);
    CHECK(hc_entry_add("Gideon 3.15", "/SD/b.cor", "", 5, 6, 0, "u64_mk2_artix 2026/10/25 12:00:00") == HCR_OK && fmt(hc_cfg()) == fmt(ref)
          && !strcmp(hc_cfg().e[1].name, "Beta"));
    fresh();
    bootcfg_add(&ref, "Gideon 3.15", "/SD/n.cor", "", 5, 6, 0);
    CHECK(hc_entry_add("Gideon 3.15", "/SD/n.cor", "", 5, 6, 0, "Alpha") == HCR_OK && fmt(hc_cfg()) == fmt(ref) && !strcmp(hc_cfg().e[0].name, "Alpha"));
    // direct boot and autostart
    fresh();
    ref.direct = !ref.direct;
    CHECK(hc_direct_toggle() == HCR_OK && fmt(hc_cfg()) == fmt(ref) && hc_cfg().direct == 1);
    fresh();
    bootcfg_autostart(&ref, false, 0);
    CHECK(hc_autostart(false, 0) == HCR_OK && fmt(hc_cfg()) == fmt(ref) && hc_cfg().countdown == 0);
    bootcfg_autostart(&ref, true, 99);                                        // clamped by the module
    CHECK(hc_autostart(true, 99) == HCR_OK && fmt(hc_cfg()) == fmt(ref) && hc_cfg().countdown == BOOT_AUTOSTART_MAX);
    // a save that fails: the change stays in memory, the result says so (the screen shows its note)
    fresh();
    g_disk_save_ok = false;
    bootcfg_move(&ref, 2, -1);
    CHECK(hc_entry_move(2, -1) == HCR_SAVE_FAILED && fmt(hc_cfg()) == fmt(ref) && g_disk.empty() && hc_cfg_edited());

    // try_load: nothing readable yet = false and nothing changed; then the file is read, a late boot.cfg is not "edited"
    hc_cfg_clear();
    g_disk_save_ok = true;
    g_disk_ok = false;
    stub_load_default_calls = 0;
    CHECK(!hc_cfg_try_load() && !hc_cfg_loaded() && stub_load_default_calls == 0);
    g_disk_ok = true;
    g_disk = FIXTURE;
    g_saves = 0;
    CHECK(hc_cfg_try_load() && hc_cfg_loaded() && !hc_cfg_edited() && hc_cfg().n == 3 && hc_cfg().def == 1 && g_saves == 0 && stub_load_default_calls == 1);
    // ... a boot.cfg with the old video= moves it to host.cfg once and rewrites the file without it
    hc_cfg_clear();
    g_disk = std::string(FIXTURE) + "video=0,2\n";
    stub_video_moves = true;
    g_saves = 0;
    CHECK(hc_cfg_try_load() && stub_video_sys == 0 && stub_video_hdmi == 2 && g_saves == 1 && hc_cfg().vid_sys == -1 && hc_cfg().vid_hdmi == -1 && g_disk.find("video=-1,-1\n") != std::string::npos && stub_av_applied == 1);
    stub_video_moves = false;

    // the module settings go through the core
    hc_protect_set(true);
    CHECK(stub_protect == 1);
    hc_protect_set(false);
    CHECK(stub_protect == 0);
    hc_ftp_set(true);
    CHECK(stub_ftp == 1);
    // T265 P4: the A/V port setting is kept in boot.cfg `video=` and written to the host at once
    g_saves = 0;
    hc_av_set(false);
    CHECK(hc_cfg().vid_av == 0 && stub_av_applied == 0 && g_saves == 1 && g_disk.find(",0\n") != std::string::npos);
    hc_av_set(true);
    CHECK(hc_cfg().vid_av == 1 && stub_av_applied == 1 && g_saves == 2);
    char why[64] = "";
    stub_pic_ok = true;
    CHECK(hc_picture_set(2, why, sizeof(why)) && stub_pic_last == 2);
    stub_pic_ok = false;
    CHECK(!hc_picture_set(4, why, sizeof(why)) && !strcmp(why, "the screen does not name it"));
    stub_pending = true;
    CHECK(hc_picture_keep());
    stub_pending = false;
    CHECK(!hc_picture_keep());
}

// ---- the start sequence (T192.2-S6, S7, S8, S13): the order of the calls, the last checks, the lock rule, one start at a time ------------------------
// The fakes of hostcore_stubs.cc and the hooks below write ONE list, in the order things happened.
static int g_abort_at, g_pn;                        // progress() answers "abort" at this call (1-based); 0 never
static bool g_key_abort, g_nested;                  // the last key check says abort; the first progress call tries a second start
static std::string g_nested_why;
static HcStartHooks g_h;

static void ev(const std::string &s) { stub_calls.push_back(s); }
static int h_progress(int step, int pct, const char *, void *)
{
    g_pn++;
    ev("progress " + std::to_string(step) + " " + std::to_string(pct));
    if (g_nested && g_pn == 1) {
        HcStartEntry e2 = { "/SD/b.cor", false, 0, false };
        const char *w = hc_start_sequence(e2, HC_START_MENU, g_h, 0);
        g_nested_why = w ? w : "(null)";
    }
    return g_abort_at && g_pn == g_abort_at;
}
static bool h_abort_key(void *) { ev("abort_key"); return g_key_abort; }
static void h_pause(void *, int ms) { ev("pause " + std::to_string(ms)); }
static void h_settle(void *) { ev("settle"); }
static void h_lap(void *, const char *w) { ev(w ? std::string("lap ") + w : std::string("lap start")); }
static void h_before(void *, bool full) { ev(full ? "before_jump full" : "before_jump core"); }

struct SeqRun {
    std::string calls, why;
    bool jumped, staged;
};
static std::string join(const std::vector<std::string> &v)
{
    std::string o;
    for (size_t i = 0; i < v.size(); i++) {
        o += (i ? "|" : "") + v[i];
    }
    return o;
}
static SeqRun run_seq(bool full, bool trusted, bool verify, int mode, int abort_at = 0, bool key_abort = false)
{
    static BootEntry be;
    g_h = { h_progress, h_abort_key, h_pause, h_settle, h_lap, h_before, 0 };
    stub_calls.clear();
    g_pn = 0;
    g_abort_at = abort_at;
    g_key_abort = key_abort;
    HcStartEntry e = { "/SD/a.cor", full, trusted ? &be : 0, verify };
    HcStartOutcome oc;
    SeqRun r;
    r.jumped = false;
    const char *w = 0;
    try {
        w = hc_start_sequence(e, mode, g_h, &oc);
    } catch (StubJump &) {
        r.jumped = true;
    }
    r.why = w ? w : "";
    r.staged = oc.staged;
    r.calls = join(stub_calls);
    return r;
}
static bool has(const std::string &hay, const char *needle) { return hay.find(needle) != std::string::npos; }

static void start_sequence()
{
    stub_stage_ok = stub_swap_ok = stub_lock_ok = true;
    stub_stage_cb_abort = 0;
    g_nested = false;
    // the menu, a core: stage, the check, swap, the last steps, the pause, the last key check, jump
    SeqRun r = run_seq(false, true, false, HC_START_MENU);
    CHECK_S(r.calls, "lap start|core_stage trusted|progress 0 50|lap bundle ready|progress 0 100|lap start|core_swap cb|lap core loaded|progress 2 100|progress 3 0|progress 4 0|settle|pause 200|abort_key|before_jump core|core_jump");
    CHECK(r.jumped && r.staged && !hc_start_running());
    // ... a direct start: the held keys are the check, the swap reports nothing, no pause
    r = run_seq(false, true, false, HC_START_DIRECT);
    CHECK_S(r.calls, "lap start|core_stage skip trusted|progress 0 50|progress 0 0|lap bundle ready|lap start|core_swap nocb|lap core loaded|before_jump core|core_jump");
    CHECK(r.jumped);
    // a firmware (a .cfw, a large .cor): the menu: stage, the last steps, the key check, the lock, jump; the direct start: stage, the check, the lock, jump
    r = run_seq(true, false, false, HC_START_MENU);
    CHECK_S(r.calls, "lap start|legacy_stage direct|progress 0 50|lap firmware ready|progress 2 100|progress 3 0|progress 4 0|settle|pause 200|abort_key|lock_flash|before_jump full|legacy_jump");
    r = run_seq(true, false, true, HC_START_MENU);
    CHECK(has(r.calls, "legacy_stage verify|") && !has(r.calls, "legacy_stage verify direct"));      // a firmware started once is checked
    r = run_seq(true, false, false, HC_START_DIRECT);
    CHECK_S(r.calls, "lap start|legacy_stage direct|progress 0 50|progress 0 0|lap firmware ready|lock_flash|before_jump full|legacy_jump");

    // D13: the lock for a firmware, never for a partial core (menu and direct)
    for (int mode = 0; mode < 2; mode++) {
        r = run_seq(false, true, false, mode);
        CHECK(!has(r.calls, "lock_flash") && r.jumped);
        r = run_seq(true, false, false, mode);
        CHECK(has(r.calls, "lock_flash") && r.jumped);
    }
    // a lock that did not take refuses the start: no jump, the reason is the lock's
    stub_lock_ok = false;
    for (int mode = 0; mode < 2; mode++) {
        r = run_seq(true, false, false, mode);
        CHECK(!r.jumped && has(r.calls, "lock_flash") && !has(r.calls, "legacy_jump"));
        CHECK_S(r.why, "the flash could not be protected (the status register did not take it); nothing was started");
    }
    stub_lock_ok = true;

    // the last checks: a key before the swap stops with nothing loaded; after the swap the core is loaded but not started; the firmware stops before the lock
    r = run_seq(false, true, false, HC_START_MENU, 2);                       // progress call 2 is (VERIFY, 100) after the stage
    CHECK_S(r.why, CORE_ABORTED);
    CHECK(!r.jumped && !has(r.calls, "core_swap") && !has(r.calls, "core_jump"));
    r = run_seq(false, true, false, HC_START_DIRECT, 2);                     // the direct start's check
    CHECK_S(r.why, CORE_ABORTED);
    CHECK(!r.jumped && !has(r.calls, "core_swap"));
    r = run_seq(false, true, false, HC_START_MENU, 3);                       // call 3 is the last steps after the swap
    CHECK_S(r.why, CORE_LOADED_NOT_STARTED);
    CHECK(!r.jumped && has(r.calls, "core_swap") && !has(r.calls, "pause") && !has(r.calls, "core_jump"));
    r = run_seq(false, true, false, HC_START_MENU, 0, true);                 // the key after the pause
    CHECK_S(r.why, CORE_LOADED_NOT_STARTED);
    CHECK(!r.jumped && has(r.calls, "abort_key") && !has(r.calls, "before_jump"));
    r = run_seq(true, false, false, HC_START_MENU, 2);
    CHECK_S(r.why, CORE_ABORTED);
    CHECK(!r.jumped && !has(r.calls, "lock_flash") && !has(r.calls, "legacy_jump"));
    r = run_seq(true, false, false, HC_START_MENU, 0, true);
    CHECK_S(r.why, CORE_ABORTED);
    CHECK(!r.jumped && !has(r.calls, "lock_flash"));
    r = run_seq(true, false, false, HC_START_DIRECT, 2);
    CHECK_S(r.why, CORE_ABORTED);
    CHECK(!r.jumped && !has(r.calls, "lock_flash"));
    // a key while the stage reads (the real stage honours the callback's answer)
    stub_stage_cb_abort = 1;
    r = run_seq(false, true, false, HC_START_MENU, 1);
    CHECK_S(r.why, CORE_ABORTED);
    CHECK(!r.staged && !r.jumped && !has(r.calls, "core_swap"));
    stub_stage_cb_abort = 0;
    // a stage that fails is not the entry's fault of a later step; a swap that fails leaves the staged flag set
    stub_stage_ok = false;
    r = run_seq(false, true, false, HC_START_MENU);
    CHECK(!r.staged && r.why == "cannot read the bundle" && !r.jumped);
    stub_stage_ok = true;
    stub_swap_ok = false;
    r = run_seq(false, true, false, HC_START_MENU);
    CHECK(r.staged && r.why == "the ICAP did not answer" && !r.jumped && !has(r.calls, "core_jump"));
    stub_swap_ok = true;

    // S13: one start at a time. A second one from inside the first is refused and changes nothing; the slot is free again afterwards, also after an abort
    g_nested = true;
    g_nested_why.clear();
    r = run_seq(false, true, false, HC_START_MENU);
    CHECK_S(g_nested_why, HC_START_BUSY);
    CHECK(r.jumped && !hc_start_running());
    CHECK(!has(r.calls, "lap start|core_stage trusted|progress 0 50|lap start"));                     // the refused start called no module
    g_nested = false;
    r = run_seq(false, true, false, HC_START_MENU, 2);
    CHECK(!r.jumped && !hc_start_running());
    r = run_seq(false, true, false, HC_START_MENU);
    CHECK(r.jumped);
    // T198 S6/S5 (D7): a start with a config hands its name over, a plain start hands over "" (never the previous one)
    {
        extern std::string stub_start_config;
        g_h = { h_progress, h_abort_key, h_pause, h_settle, h_lap, h_before, 0 };
        static BootEntry be2;
        HcStartEntry e = { "/SD/a.cor", false, &be2, false, "Demo collection" };
        HcStartOutcome oc;
        try { hc_start_sequence(e, HC_START_MENU, g_h, &oc); } catch (StubJump &) { }
        CHECK(stub_start_config == "Demo collection");
        e.config = "";
        try { hc_start_sequence(e, HC_START_MENU, g_h, &oc); } catch (StubJump &) { }
        CHECK(stub_start_config.empty());
        hc_start_config_set("Games");                      // D8: the countdown / the overlay arm it, the start takes it once
        try { hc_start_sequence(e, HC_START_MENU, g_h, &oc); } catch (StubJump &) { }
        CHECK(stub_start_config == "Games");
        try { hc_start_sequence(e, HC_START_MENU, g_h, &oc); } catch (StubJump &) { }
        CHECK(stub_start_config.empty());
    }
}

// ---- the entries' state and the lazy reader (T101 S10): in the order the loop meant it. The files are not there (a fake file system); the render bench
// reads real bundles through the same code. -----------------------------------------------------------------------------------------------------
class NoFs : public StartFs {
public:
    int stats = 0, opens = 0;
    bool stat(const char *, FileStat *) override { stats++; return false; }
    CorSource *open(const char *) override { opens++; return 0; }
    int list(const char *) override { return 0; }
};

static void entries()
{
    static NoFs fs;
    static std::vector<std::string> lines;
    static const HcPlatform plat = { p_load, p_save, p_changed, &fs, [](const char *l) { lines.push_back(l); }, 0 };
    hc_init(&plat);
    hc_cfg_reset(FIXTURE);                                                    // default 1, three entries
    hc_lazy_reset();
    CHECK(hc_est(0) == ES_UNKNOWN && hc_est(1) == ES_UNKNOWN && !hc_efull(0));
    CHECK(hc_startable(0) && hc_startable(2) && !hc_startable(3) && !hc_startable(-1));           // an entry not read yet starts too
    CHECK(hc_lazy_next(2) == 1);                                              // the default first
    HcLazy z;
    CHECK(hc_lazy_step(false, 2, &z) && z.index == 1 && z.unit == HCL_STATE && !z.icon_next && hc_est(1) == ES_MISSING);        // not there: no icon follows
    CHECK(!hc_startable(1) && hc_efull(1) == false);
    CHECK(hc_lazy_next(2) == 2);                                              // then the cursor's entry
    CHECK(hc_lazy_step(false, 2, &z) && z.index == 2);
    CHECK(hc_lazy_next(2) == 0 && hc_lazy_step(false, 2, &z) && z.index == 0);
    CHECK(hc_lazy_next(2) == -1 && !hc_lazy_step(false, 2, &z));
    CHECK(fs.opens > 0 && fs.stats > 0);
    // a quiet pass migrates an old entry (no mtime) only when it exists: these do not
    CHECK(!hc_lazy_step(true, 0, &z) || z.unit != HCL_ICON);
    // mark: one entry is read again; reset: all of them
    hc_lazy_mark(0);
    CHECK(hc_lazy_next(1) == 0 && hc_est(0) == ES_MISSING);                   // the old state stays until the new one is in
    hc_lazy_reset();
    CHECK(hc_est(0) == ES_UNKNOWN && hc_lazy_next(0) == 1);
    CHECK(hc_in_menu("/SD/b.cor") == 1 && hc_in_menu("/SD/zzz.cor") == -1);
    hc_entry_origin(0, 3);
    CHECK(hc_einfo(0).origin == 3);
    CHECK(!hc_is_full("/SD/a.cor") && hc_is_full("/SD/x.cfw"));              // a .cfw is full by its name; a .cor that cannot be opened is not
}

// ---- the countdown (T192.2-S13): it runs only when the default can be started; any input stops it; no input, it ends in the start ------------------------------
static void countdown()
{
    static NoFs fs;
    static std::vector<std::string> lines;
    static const HcPlatform plat = { p_load, p_save, p_changed, &fs, [](const char *l) { lines.push_back(l); }, 0 };
    hc_init(&plat);
    hc_cfg_reset(FIXTURE);                                                    // countdown 5, default 1
    hc_lazy_reset();                                                          // the entries are not read yet: they start
    hc_countdown_boot(1000);
    hc_cd_set(false, false, 0);
    CHECK(hc_countdown_tick(1000) == HCC_BEGAN && hc_cd_running() && !hc_cd_dead() && hc_cd_left_ms(1000) == 5000);
    CHECK(!lines.empty() && lines.back() == "[host] countdown 5 s for Beta");
    CHECK(hc_countdown_tick(5999) == 0 && hc_cd_running());                   // key at second 4: nothing yet
    CHECK(hc_countdown_tick(6000) == HCC_EXPIRED && !hc_cd_running() && hc_cd_dead());      // no key: the countdown ends in the start
    CHECK(hc_countdown_tick(9000) == 0);                                      // and it does not begin again
    // any input stops it for good and says so once
    hc_cd_set(false, false, 0);
    hc_countdown_tick(1000);
    size_t n = lines.size();
    hc_countdown_stop();
    CHECK(!hc_cd_running() && hc_cd_dead() && lines.size() == n + 1 && lines.back() == "[host] countdown stopped");
    CHECK(hc_countdown_tick(7000) == 0 && !hc_cd_running());                  // after the stop it never starts
    hc_countdown_stop();
    CHECK(lines.size() == n + 1);                                             // nothing to stop: no second line
    // the default's file is not there: it cannot be started, the countdown waits for the medium and gives up after BOOT_WAIT_MS
    HcLazy z;
    hc_lazy_reset();
    while (hc_lazy_step(false, 0, &z)) {
    }
    CHECK(hc_est(1) == ES_MISSING && !hc_startable(1));
    hc_cd_set(false, false, 0);
    hc_countdown_boot(1000);
    CHECK(hc_countdown_tick(1000 + HC_BOOT_WAIT_MS) == 0 && !hc_cd_running() && !hc_cd_dead());
    CHECK(hc_countdown_tick(1001 + HC_BOOT_WAIT_MS) == HCC_GAVE_UP && hc_cd_dead());
    // countdown 0 or no default: given up at once
    hc_cfg_reset(FIXTURE);
    hc_autostart(false, 0);
    hc_cd_set(false, false, 0);
    hc_countdown_boot(0);
    CHECK(hc_countdown_tick(1) == HCC_GAVE_UP);
    hc_countdown_give_up();
    CHECK(hc_cd_dead());
}

int main()
{
    legends();
    table();
    resolve();
    commands();
    start_sequence();
    entries();
    countdown();
    printf("test_hostcore: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
