// T239.3 base 3.3: the settings of the generic application (baseset.h: the keyboard as a joystick, the USB mouse) and the core hooks (core_hooks.h,
// hookhost.h). A core whose descriptor has a mouse gets Mouse Sensitivity and Mouse Acceleration; the joystick and the mouse mode and wheel
// rows need a capability of the descriptor (none of the real cores has one: rich_of builds the descriptor of a core that does); no descriptor declares one of the six. The menu's look (transparency, colour scheme, filename squeeze, navigation style) is no row of any core (T239.3 D56).
// A change reaches the board port (the keyboard's joystick registers), the USB driver's answers come from the settings by key, and the hooks of a library's table "corehooks" are called in the order the header gives:
// start, then base_setting_changed once per setting the core has, then once per change; menu_closed on its own; a table without a member, or of another major, is no hook.
#include <string>
#include <vector>
#include "check.h"
#include "fakes.h"
#include "cores.h"
#include "regset.h"
#include "ops.h"
#include "ops_glue.h"
#include "baseset.h"
#include "hookhost.h"
#include "core_hooks.h"
#include "mousesink.h"
#include "contrib.h"
#ifdef TEST_APP_AMIGA
extern const CoreDesc amiga_desc;               // the Amiga 500 is its own app (cores/amiga/app/lib)
#endif

extern "C" int u64_get_usb_hid_config_value(int key, int default_value);

static const char *const order[6] = { "Joystick Input", "Mouse Mode",
                                       "Mouse Sensitivity", "Mouse Acceleration", "Mouse Wheel Sensitivity", "Mouse Wheel Direction" };

// A descriptor that names the two capabilities of the rows that no real core has (baseset.h): the rows a core with them would show.
// a core with a USB mouse whose descriptor declares none of the eight keys: the Amiga's without its own two mouse rows (those are the core's, D25)
static const CoreDesc &mouse_core_of(const CoreDesc &src)
{
    static CoreDesc d;
    static Setting s[SETTINGS_MAX];
    int n = 0;
    for (int i = 0; i < src.nsettings; i++) {
        if (strcmp(src.settings[i].key, "Mouse Sensitivity") && strcmp(src.settings[i].key, "Mouse Acceleration")) s[n++] = src.settings[i];
    }
    d = src;
    d.settings = s;
    d.nsettings = n;
    return d;
}

static const CoreDesc &rich_of(const CoreDesc &src, int slot)
{
    static CoreDesc d[4];
    static const char *caps[4][16];
    int n = 0;
    for (int i = 0; src.caps && i < src.ncaps && n < 14; i++) caps[slot][n++] = src.caps[i];
    caps[slot][n++] = BASE_CAP_KEYBOARD_JOYSTICK;
    caps[slot][n++] = BASE_CAP_MOUSE_DRIVER;
    d[slot] = src;
    d[slot].caps = caps[slot];
    d[slot].ncaps = n;
    return d[slot];
}

static std::vector<std::string> g_log;                    // what the port and the hooks were told, in order
static void port_joy(uint8_t port, uint8_t wasd) { g_log.push_back("joy " + std::to_string(port) + " " + std::to_string(wasd)); }
static const BaseSetPort port = { port_joy };

static bool g_fail_start;
static int h_start(char *msg, int n) { g_log.push_back("start"); if (g_fail_start) { snprintf(msg, n, "no memory for the ring buffers of this core, a reason that is longer than the message"); return 1; } return 0; }
static void h_menu_closed(void) { g_log.push_back("menu_closed"); }
static void h_changed(const char *key) { g_log.push_back(std::string("changed ") + key); }
static void h_video(int behind) { g_log.push_back("video " + std::to_string(behind)); }
static CoreHooks g_table;
static uint32_t g_table_size, g_table_major;
static const void *lib_find(const char *name, uint32_t min_version, uint32_t *size)
{
    if (strcmp(name, "corehooks") || (min_version >> 16) != g_table_major) return 0;
    if (size) *size = g_table_size;
    return &g_table;
}
static const SvcLib lib = { sizeof(SvcLib), lib_find };
static void make_table(uint32_t size, bool all)
{
    memset(&g_table, 0, sizeof(g_table));
    g_table.size = size;
    g_table.start = h_start;
    if (all) { g_table.menu_closed = h_menu_closed; g_table.base_setting_changed = h_changed; g_table.menu_video = h_video; }
    g_table_size = size;
    g_table_major = 1;
}

struct Rig {
    FakeIo io; FakeFs fs; CoreCtx c; Settings *st; CoreDesc d; Setting store[SETTINGS_MAX];
    explicit Rig(const CoreDesc *src)
    {
        CHECK(!baseset_extend(src, &d, store));
        memset(&c, 0, sizeof(c));
        c.desc = &d; c.io = &io; c.fs = &fs;
        st = new Settings(d.settings, d.nsettings, d.store, &fs, "/sd/cores/base.cfg", 0);
        st->load();
        c.settings = st;
        baseset_attach(&c, &port);
    }
    void set(const char *key, const char *value)
    {
        OpIn in[2] = { in_str("key", key), in_str("value", value) };
        OpResult res;
        CHECK_EQ(ops_call(&c, "settings.set", in, 2, &res), 0);
    }
    int ix(const char *key) { return st->find(key); }
};
static int count_key(const CoreDesc &d, const char *key)
{
    int n = 0;
    for (int i = 0; i < d.nsettings; i++) n += strcasecmp(d.settings[i].key, key) == 0;
    return n;
}

int main()
{
#ifdef TEST_APP_AMIGA
    // T239.5 D13: the Amiga 500's cases of this file (the core with a USB mouse, its rich form) run as its app (run.sh amigaapp, unit amiga_baseset); the shared unit baseset keeps
    // the cores without a mouse and the generic checks
    // ---- the rows: a core without a mouse and without a capability gets none, a core with a mouse gets two, one with both capabilities six, in the order of svc_settings.h; no descriptor declares one
    const CoreDesc &mouse_core = mouse_core_of(amiga_desc);
    CHECK(mouse_core.mouse != 0);
    // D25: the Amiga declares Mouse Sensitivity / Acceleration itself (operations mouse.sens / mouse.accel); the application adds neither
    CHECK_EQ(count_key(amiga_desc, "Mouse Sensitivity"), 1);
    CHECK(!baseset_adds(&amiga_desc, "Mouse Sensitivity") && !baseset_adds(&amiga_desc, "Mouse Acceleration"));
    const CoreDesc &rich_amiga = rich_of(mouse_core, 0);
    {
        CHECK(desc_has_capability(&rich_amiga, BASE_CAP_KEYBOARD_JOYSTICK) && desc_has_capability(&rich_amiga, BASE_CAP_MOUSE_DRIVER));
        CHECK(!desc_has_capability(&mouse_core, BASE_CAP_KEYBOARD_JOYSTICK) && !desc_has_capability(&mouse_core, BASE_CAP_MOUSE_DRIVER));
    }
    for (int k = 0; k < 6; k++) {
        CHECK_EQ(count_key(mouse_core, order[k]), 0);
    }
    {
        Rig wm(&rich_amiga);
        CHECK_EQ(wm.d.nsettings, mouse_core.nsettings + 6);
        for (int k = 0; k < 6; k++) {
            CHECK_EQ(count_key(wm.d, order[k]), 1);
            CHECK_STR(wm.d.settings[mouse_core.nsettings + k].key, order[k]);
            const Setting &s = wm.d.settings[mouse_core.nsettings + k];
            CHECK(baseset_is_generic(s) && !s.bind);
            CHECK_EQ(baseset_kind(s), 2);
            CHECK_EQ(s.when, WHEN_LIVE);
            CHECK_EQ(s.kind, 0);
        }
        // what the real cores get (T228 R20, "our menu may only show what the code can do"): no core of this tree names a capability, so the joystick row and the mouse mode and wheel rows
        // are nobody's; the sensitivity and the acceleration, which the USB driver applies before the core's mouse sink, are every core with a mouse's
        {
            Rig am(&mouse_core);
            CHECK_EQ(am.d.nsettings, mouse_core.nsettings + 2);
            for (int k = 0; k < 6; k++) {
                bool want = k == 2 || k == 3;
                CHECK_EQ(count_key(am.d, order[k]), want ? 1 : 0);
                CHECK_EQ(baseset_adds(&mouse_core, order[k]), want);
                CHECK_EQ(baseset_adds(&rich_amiga, order[k]), true);
            }
            CHECK(am.d.settings[am.ix("Mouse Sensitivity")].show_state == 0);           // there is no Mouse Mode row to be the condition
            CHECK(am.d.settings[am.ix("Mouse Acceleration")].show_state == 0);
            CHECK_EQ(am.ix("Joystick Input"), -1);
            CHECK_EQ(am.ix("Mouse Mode"), -1);
            CHECK_EQ(am.ix("Mouse Wheel Sensitivity"), -1);
            CHECK_EQ(am.ix("Mouse Wheel Direction"), -1);
            CHECK(!baseset_adds(&mouse_core, "No Such Key"));
            // the menu description hides a row of the application by its key (menu.toml `hide`): the merge drops it, nothing else
            static const char *const hide[] = { "Mouse Sensitivity", "Mouse Acceleration" };
            Overrides ov = { hide, 2, 0, 0 };
            CoreDesc merged;
            MenuMerge *mm = new MenuMerge;
            CHECK_EQ(contrib_merge(&am.d, 0, &ov, 0, 0, 0, &merged, mm), 0);
            CHECK_EQ(merged.nsettings, am.d.nsettings - 2);
            CHECK_EQ(count_key(merged, "Mouse Sensitivity"), 0);
            CHECK_EQ(count_key(merged, "Mouse Acceleration"), 0);
            CHECK_EQ(contrib_merge(&am.d, 0, 0, 0, 0, 0, &merged, mm), 0);
            CHECK_EQ(merged.nsettings, am.d.nsettings);                                 // without the list every row stays
            delete mm;
        }
        // T239.3 D56: the menu's look is no row of the application, for no core: the base has no row, no operation and no key for it
        for (const char *gone : { "Overlay Transparency", "Color Scheme", "Filename overflow squeeze", "Navigation Style" }) {
            CHECK_EQ(count_key(wm.d, gone), 0);
            CHECK(!baseset_adds(&rich_amiga, gone));
        }
        auto at = [&](Rig &r, const char *key) -> const Setting & { return r.d.settings[r.ix(key)]; };
        const Setting &jy = at(wm, "Joystick Input"), &mm = at(wm, "Mouse Mode"), &ms = at(wm, "Mouse Sensitivity"), &ma = at(wm, "Mouse Acceleration");
        const Setting &ws = at(wm, "Mouse Wheel Sensitivity"), &wd = at(wm, "Mouse Wheel Direction");
        CHECK_EQ(jy.nvalues, 6); CHECK_STR(jy.values[jy.def], "Normal"); CHECK_STR(jy.values[2], "WASD Port 2"); CHECK_STR(jy.values[5], "WASD P1 No KB");
        CHECK_EQ(mm.nvalues, 3); CHECK_STR(mm.values[mm.def], "Cursor keys"); CHECK_STR(mm.values[2], "Wheel: joystick");
        CHECK_EQ(ms.nvalues, 16); CHECK_EQ(ms.base, 1); CHECK_STR(ms.values[ms.def], "8");
        CHECK_EQ(ma.nvalues, 2); CHECK_STR(ma.values[1], "Adaptive");
        CHECK_EQ(ws.nvalues, 16); CHECK_EQ(ws.base, 1); CHECK_STR(ws.values[ws.def], "8");
        CHECK_EQ(wd.nvalues, 2); CHECK_STR(wd.values[1], "Reversed");
        CHECK_STR(ms.show_state, "set.Mouse_Mode==Cursor keys");
        CHECK_STR(jy.apply, "base.joystick");
    }

    // ---- the USB driver's answers come from the settings by key; a core without a mouse answers the driver's defaults
    {
        Rig r(&rich_amiga);
        CHECK_EQ(u64_get_usb_hid_config_value(0x5D, 99), 8);               // sensitivity default 8
        CHECK_EQ(u64_get_usb_hid_config_value(0x5E, 99), 0);
        CHECK_EQ(u64_get_usb_hid_config_value(0x55, 99), 0);               // mode: Cursor keys is the driver's 0
        CHECK_EQ(u64_get_usb_hid_config_value(0x56, 99), 8);
        CHECK_EQ(u64_get_usb_hid_config_value(0x57, 99), 0);
        CHECK_EQ(u64_get_usb_hid_config_value(0x58, 99), 0);               // the menu is keys only
        CHECK_EQ(u64_get_usb_hid_config_value(0x01, 99), 99);              // any other item: the driver's own default
        r.set("Mouse Sensitivity", "4");
        r.set("Mouse Acceleration", "Adaptive");
        r.set("Mouse Wheel Sensitivity", "16");
        r.set("Mouse Wheel Direction", "Reversed");
        CHECK_EQ(u64_get_usb_hid_config_value(0x5D, 99), 4);
        CHECK_EQ(u64_get_usb_hid_config_value(0x5E, 99), 1);
        CHECK_EQ(u64_get_usb_hid_config_value(0x56, 99), 16);
        CHECK_EQ(u64_get_usb_hid_config_value(0x57, 99), 1);
        r.set("Mouse Mode", "Wheel: cursor keys");
        CHECK_EQ(u64_get_usb_hid_config_value(0x55, 99), 2);               // the list's second value is the driver's mode 2, the third its mode 3
        r.set("Mouse Mode", "Wheel: joystick");
        CHECK_EQ(u64_get_usb_hid_config_value(0x55, 99), 3);
        // the settings are replaced (a new config is loaded, a new object): the answers follow the new object, not the old index
        static CoreDesc bare = amiga_desc;                                 // a core without the setting: the Amiga 500's record without its settings and its mouse
        bare.settings = 0; bare.nsettings = 0; bare.mouse = 0;
        Rig nm(&bare);
        baseset_attach(&r.c, &port);                                       // (a Rig attaches itself)
        r.c.settings = nm.st;
        r.c.desc = &nm.d;
        CHECK_EQ(u64_get_usb_hid_config_value(0x5D, 99), 8);               // a core without the setting: the default sensitivity
        r.c.settings = r.st;
        r.c.desc = &r.d;
        CHECK_EQ(u64_get_usb_hid_config_value(0x5D, 99), 4);
        baseset_attach(0, 0);
        CHECK_EQ(u64_get_usb_hid_config_value(0x5D, 99), 99);              // no context: the default the driver gave
    }

    // ---- the hooks, in order: start, then one base_setting_changed per setting the core has (two with a mouse, six with both capabilities and a mouse), then one per change; menu_closed apart
    {
        Rig r(&rich_amiga);
        make_table(sizeof(CoreHooks), true);
        hooks_attach(&lib);
        CHECK(hooks_present());
        g_log.clear();
        r.set("Joystick Input", "Swapped");                                // before the start: the port acts, the hook is not told
        CHECK_EQ(g_log.size(), 1u);
        CHECK_STR(g_log[0].c_str(), "joy 1 0");
        g_log.clear();
        char msg[40] = "x";
        CHECK(hooks_has_menu_closed());
        CHECK_EQ(hooks_start(&r.c, msg, sizeof(msg)), 0);
        CHECK_EQ(g_log.size(), 7u);                                       // the table's start member ran (first entry): the base makes no start call of its own
        CHECK_STR(g_log[0].c_str(), "start");
        for (int k = 0; k < 6; k++) CHECK_STR(g_log[1 + k].c_str(), (std::string("changed ") + order[k]).c_str());
        g_log.clear();
        r.set("Joystick Input", "WASD Port 1");                            // a change after the start: the port, then the hook
        CHECK_EQ(g_log.size(), 2u);
        CHECK_STR(g_log[0].c_str(), "joy 1 1");
        CHECK_STR(g_log[1].c_str(), "changed Joystick Input");
        g_log.clear();
        r.set("Mouse Sensitivity", "9");
        CHECK_EQ(g_log.size(), 1u);
        CHECK_STR(g_log[0].c_str(), "changed Mouse Sensitivity");
        g_log.clear();
        CHECK(hooks_menu_closed());
        CHECK_EQ(g_log.size(), 1u);
        CHECK_STR(g_log[0].c_str(), "menu_closed");
        g_log.clear();
        hooks_menu_video(true);                                            // menu_video: the value as the UI gives it
        hooks_menu_video(false);
        CHECK_EQ(g_log.size(), 2u);
        CHECK_STR(g_log[0].c_str(), "video 1");
        CHECK_STR(g_log[1].c_str(), "video 0");
        make_table((uint32_t)offsetof(CoreHooks, menu_video), true);      // a library of an earlier 3.3 has no menu_video: nothing is called
        hooks_attach(&lib);
        g_log.clear();
        hooks_menu_video(true);
        CHECK(g_log.empty());
        make_table(sizeof(CoreHooks), true);
        hooks_attach(&lib);

        Rig nm(&mouse_core);                                               // a core with a mouse and without a capability: the two mouse rows
        make_table(sizeof(CoreHooks), true);
        hooks_attach(&lib);
        g_log.clear();
        CHECK_EQ(hooks_start(&nm.c, msg, sizeof(msg)), 0);
        CHECK_EQ(g_log.size(), 3u);
        for (int k = 0; k < 2; k++) CHECK_STR(g_log[1 + k].c_str(), (std::string("changed ") + order[2 + k]).c_str());

        // a start that fails says why (cut to the buffer) and returns 1; the settings are still told after it
        make_table(sizeof(CoreHooks), true);
        hooks_attach(&lib);
        g_fail_start = true;
        g_log.clear();
        char small[24];
        CHECK_EQ(hooks_start(&nm.c, small, sizeof(small)), 1);
        CHECK_EQ(strlen(small), sizeof(small) - 1);
        CHECK_EQ(g_log.size(), 3u);
        g_fail_start = false;

        // a table of an older 3.x that ends before menu_closed: no such hook; one that ends before base_setting_changed: no such hook
        make_table((uint32_t)offsetof(CoreHooks, menu_closed), true);
        hooks_attach(&lib);
        g_log.clear();
        CHECK(!hooks_menu_closed());
        CHECK(!hooks_has_menu_closed());
        CHECK_EQ(hooks_start(&nm.c, msg, sizeof(msg)), 0);
        CHECK_EQ(g_log.size(), 1u);                                        // start only
        make_table((uint32_t)offsetof(CoreHooks, base_setting_changed), true);
        hooks_attach(&lib);
        g_log.clear();
        CHECK(hooks_menu_closed());
        CHECK_EQ(g_log.size(), 1u);
        // a member left null is no hook
        make_table(sizeof(CoreHooks), false);
        hooks_attach(&lib);
        g_log.clear();
        CHECK(!hooks_menu_closed());
        CHECK(!hooks_has_menu_closed());
        CHECK_EQ(hooks_start(&nm.c, msg, sizeof(msg)), 0);
        CHECK_EQ(g_log.size(), 1u);

        // a table that has base_setting_changed only (the C64's paddle swap): present, but no start and no menu_closed, so the base's own start and menu calls are due
        make_table(sizeof(CoreHooks), true);
        g_table.start = 0;
        g_table.menu_closed = 0;
        hooks_attach(&lib);
        CHECK(hooks_present());
        CHECK(!hooks_has_menu_closed());
        g_log.clear();
        CHECK_EQ(hooks_start(&nm.c, msg, sizeof(msg)), 0);
        CHECK_EQ(g_log.size(), 2u);                                        // no start, but the two settings of a core with a mouse are told
        CHECK_STR(g_log[0].c_str(), "changed Mouse Sensitivity");

        // a table of a later major only, no table, no lib: no hooks, nothing is called
        make_table(sizeof(CoreHooks), true);
        g_table_major = 2;
        hooks_attach(&lib);
        CHECK(!hooks_present());
        hooks_attach(0);
        CHECK(!hooks_present());
        g_log.clear();
        CHECK(!hooks_menu_closed());
        CHECK_EQ(hooks_start(&nm.c, msg, sizeof(msg)), 0);
        hooks_base_changed("Joystick Input");
        hooks_menu_video(true);
        CHECK(g_log.empty());
    }
#else
    // ---- the rows: a core without a mouse and without a capability gets none, a core with a mouse gets two, one with both capabilities six, in the order of svc_settings.h; no descriptor declares one
    CHECK(atari800_desc.mouse == 0);
    const CoreDesc &rich_atari = rich_of(atari800_desc, 1), &rich_nes = rich_of(nes_desc, 2);
    {
        // a capability name is 1 to 15 of [a-z0-9_] (CoreDesc::caps, desc_check, the record's section 1): a longer one could never be declared by any core
        const char *names[2] = { BASE_CAP_KEYBOARD_JOYSTICK, BASE_CAP_MOUSE_DRIVER };
        for (int k = 0; k < 2; k++) {
            size_t l = strlen(names[k]);
            CHECK(l >= 1 && l <= 15);
            for (size_t i = 0; i < l; i++) CHECK((names[k][i] >= 'a' && names[k][i] <= 'z') || (names[k][i] >= '0' && names[k][i] <= '9') || names[k][i] == '_');
        }
    }
    for (int k = 0; k < 6; k++) {
        CHECK_EQ(count_key(atari800_desc, order[k]), 0);
    }
    {
        Rig nm(&atari800_desc);
        CHECK_EQ(nm.d.nsettings, atari800_desc.nsettings);
        for (int k = 0; k < 6; k++) {
            CHECK_EQ(count_key(nm.d, order[k]), 0);                      // the joystick, the mouse and the wheel only where the code can do them
        }
        for (int k = 0; k < 6; k++) {
            CHECK_EQ(baseset_adds(&atari800_desc, order[k]), false);
            CHECK_EQ(baseset_adds(&rich_atari, order[k]), k == 0);
        }
        // the capability alone is not enough for a mouse row: a core without a mouse has none
        CHECK_EQ(count_key(Rig(&rich_atari).d, "Mouse Mode"), 0);
        CHECK_EQ(count_key(Rig(&rich_atari).d, "Joystick Input"), 1);
        CHECK_STR(baseset_key(0), "Joystick Input");
        CHECK_EQ(baseset_count(), 6);
        // T239.3 D56: the menu's look is no row of the application, for no core: the base has no row, no operation and no key for it
        for (const char *gone : { "Overlay Transparency", "Color Scheme", "Filename overflow squeeze", "Navigation Style" }) {
            CHECK_EQ(count_key(nm.d, gone), 0);
        }
        // a descriptor that declares a key itself keeps it: no second one
        static const char *const two[] = { "A", "B" };
        static Setting own[SETTINGS_MAX];
        for (int i = 0; i < atari800_desc.nsettings; i++) own[i] = atari800_desc.settings[i];
        own[atari800_desc.nsettings] = Setting{ "Joystick Input", two, 2, 0, WHEN_LIVE, 0, 0 };
        CoreDesc od = rich_atari, ox;
        static Setting os[SETTINGS_MAX];
        od.settings = own; od.nsettings = atari800_desc.nsettings + 1;
        CHECK(!baseset_extend(&od, &ox, os));
        CHECK_EQ(ox.nsettings, od.nsettings + 0);
        CHECK_EQ(count_key(ox, "Joystick Input"), 1);
    }

    // ---- the settings reach the port: the keyboard's joystick; each also tells the hook, which is not armed yet
    {
        Rig r(&rich_nes);
        g_log.clear();
        hooks_attach(0);
        const char *names[6] = { "Normal", "Swapped", "WASD Port 2", "WASD Port 1", "WASD P2 No KB", "WASD P1 No KB" };
        const char *want[6] = { "joy 0 0", "joy 1 0", "joy 0 1", "joy 1 1", "joy 0 3", "joy 1 3" };
        for (int i = 0; i < 6; i++) {
            g_log.clear();
            r.set("Joystick Input", names[i]);
            CHECK_EQ(g_log.size(), 1u);
            CHECK_STR(g_log.empty() ? "" : g_log[0].c_str(), want[i]);
        }
        // the start applies every setting once: the port gets the stored joystick
        g_log.clear();
        regset_apply(&r.c, true);
        bool saw_joy = false;
        for (size_t i = 0; i < g_log.size(); i++) { saw_joy |= g_log[i] == "joy 1 3"; }
        CHECK(saw_joy);
    }

#endif
    baseset_attach(0, 0);
    DONE("baseset");
}
