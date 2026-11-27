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
#include "libslot.h"
#include "mousesink.h"
#include "contrib.h"
extern const CoreDesc nes_desc;          // cores.h lists the shared app's cores; the NES is its own app (cores/nes/app/lib)

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
static uint32_t g_table_major;
static bool g_linked;                                     // the app is linked with the hooks library
// The slot table of this unit's app (libslot.h, base 4.0): the slots of the native builds (tests/coreapp/libtab_native.cc: the shared app's exports, among them the mouse table that
// u64_get_usb_hid_config_value reads), and the export "corehooks" of the hooks library at major 1 and one at major 2 (a table of a later major only); hooks_attach takes the slot of
// major 1 (libtab_export). The size is the table's first word, as make_table sets it.
const void *c64cart_table(void);
const void *cbm1541_table(void);
const void *sid_table(void);
const void *tempdisk_table(void);
const void *cbmprg_table(void);
const void *mouse_table(void);
const void *pad_table(void);
const void *padkeys_table(void);
static const void *hooks_m1(void) { return g_linked && g_table_major == 1 ? &g_table : 0; }
static const void *hooks_m2(void) { return g_linked && g_table_major == 2 ? &g_table : 0; }
const LibSlot g_libslot[] = {
    { "c64cart", "c64cart", 1, c64cart_table, 0 },
    { "cbm1541", "cbm1541", 1, cbm1541_table, 0 },
    { "sid", "sid", 1, sid_table, 0 },
    { "tempdisk", "tempdisk", 1, tempdisk_table, 0 },
    { "cbmprg", "cbmprg", 1, cbmprg_table, 0 },
    { "mouse", "mouse", 1, mouse_table, 0 },
    { "pad", "pad", 1, pad_table, 0 },
    { "padkeys", "padkeys", 1, padkeys_table, 0 },
    { "hooklib", "corehooks", 1, hooks_m1, 0 },
    { "hooklib2", "corehooks", 2, hooks_m2, 0 },
};
const unsigned g_libslot_n = 10;
const void *g_libslot_bound[10];
static void attach(bool linked)
{
    g_linked = linked;
    hooks_attach();
}
static void make_table(uint32_t size, bool all)
{
    memset(&g_table, 0, sizeof(g_table));
    g_table.size = size;
    g_table.start = h_start;
    if (all) { g_table.menu_closed = h_menu_closed; g_table.base_setting_changed = h_changed; g_table.menu_video = h_video; }
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
    const CoreDesc &rich_nes = rich_of(nes_desc, 2);

    // ---- the settings reach the port: the keyboard's joystick; each also tells the hook, which is not armed yet
    {
        Rig r(&rich_nes);
        g_log.clear();
        attach(false);
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
    baseset_attach(0, 0);
    DONE("baseset");
}
