#include "hotkeys.h"
#include "baseset.h"
#include "menubuild.h"
#include "ops.h"
#include "ops_glue.h"
#include "io.h"
#include "settings.h"
#include "padkeys.h"
#include <string.h>
#include <stdio.h>
#include <strings.h>

// The 64 keys of the matrix by index (col * 8 + row), the names of the C64's keycaps; 64 is RESTORE.
static const char *const g_keys[HK_NKEYS] = {
    "DEL", "RETURN", "CRSR RIGHT", "F7", "F1", "F3", "F5", "CRSR DOWN",
    "3", "W", "A", "4", "Z", "S", "E", "L SHIFT",
    "5", "R", "D", "6", "C", "F", "T", "X",
    "7", "Y", "G", "8", "B", "H", "U", "V",
    "9", "I", "J", "0", "M", "K", "O", "N",
    "+", "P", "L", "-", ".", ":", "@", ",",
    "POUND", "*", ";", "HOME", "R SHIFT", "=", "UP ARROW", "/",
    "1", "LEFT ARROW", "CTRL", "2", "SPACE", "C=", "Q", "RUN/STOP",
    "RESTORE" };
#define K_LSHIFT 15
#define K_RSHIFT 52
#define K_CTRL 58
#define K_CBM 61
#define K_STOP 63

// The value labels of an action key item - "Off", then the 65 keys - are the base UI TOML's row (coreapp/base_ui.toml, the list hotkey_keys; provider "hotkeys.keys"), the C64's keycaps. T187 gate 1 (R6): a core
// that is not a C64 family keyboard names the three keys the person presses on a USB keyboard that make C=, RUN/STOP and RESTORE (keyboard_usb.cc: a GUI key, ESC, F12), not the C64's keycaps:
// that list is the provider's.
static bool g_vals_made;
static const char *g_pc_vals[HK_NKEYS + 1];
static const char *pc_name(int i)
{
    return i == K_CBM ? "WIN / CMD" : i == K_STOP ? "ESC" : i == HK_RESTORE ? "F12" : g_keys[i];
}
static void make_vals()
{
    if (g_vals_made) {
        return;
    }
    g_pc_vals[0] = "Off";
    for (int i = 0; i < HK_NKEYS; i++) {
        g_pc_vals[i + 1] = pc_name(i);
    }
    g_vals_made = true;
}

const char *hk_key_name(int i)
{
    return i >= 0 && i < HK_NKEYS ? g_keys[i] : "?";
}

// the modifier presets of the F8 item: Off and the pairs that make sense
struct Preset { const char *name; uint8_t a, b; };
static const Preset g_pre[] = {
    { "Off", HK_NONE, HK_NONE },
    { "CTRL", K_CTRL, HK_NONE },
    { "C=", K_CBM, HK_NONE },
    { "C= + CTRL", K_CBM, K_CTRL },
    { "C= + L SHIFT", K_CBM, K_LSHIFT },
    { "C= + RUN/STOP", K_CBM, K_STOP },
    { "CTRL + L SHIFT", K_CTRL, K_LSHIFT },
    { "CTRL + RUN/STOP", K_CTRL, K_STOP },
};
#define NPRE ((int)(sizeof(g_pre) / sizeof(g_pre[0])))
static const char *g_pc_pre_names[NPRE];
static char g_pc_pre_buf[NPRE][24];
static bool g_pc_pre_made;
static void make_pc_pre()
{
    if (g_pc_pre_made) {
        return;
    }
    for (int i = 0; i < NPRE; i++) {
        const Preset &p = g_pre[i];
        if (p.a == HK_NONE) {
            snprintf(g_pc_pre_buf[i], sizeof(g_pc_pre_buf[i]), "%s", p.name);
        } else if (p.b == HK_NONE) {
            snprintf(g_pc_pre_buf[i], sizeof(g_pc_pre_buf[i]), "%s", pc_name(p.a));
        } else {
            snprintf(g_pc_pre_buf[i], sizeof(g_pc_pre_buf[i]), "%s + %s", pc_name(p.a), pc_name(p.b));
        }
        g_pc_pre_names[i] = g_pc_pre_buf[i];
    }
    g_pc_pre_made = true;
}
int hk_modifier_presets() { return NPRE; }
const char *hk_modifier_name(int i) { return i >= 0 && i < NPRE ? g_pre[i].name : "?"; }

int hk_preset_index(const HkMap &m)
{
    for (int i = 0; i < NPRE; i++) {
        if (g_pre[i].a == m.mod_a && g_pre[i].b == m.mod_b) {
            return i;
        }
    }
    return -1;
}
static int preset_of(const HkMap &m)
{
    int i = hk_preset_index(m);
    return i < 0 ? 0 : i;
}

// ---- the registers -----------------------------------------------------------------------------------------------------------------------
static bool is_mod(const HkMap &m, int k)
{
    return k == m.mod_a || k == m.mod_b;
}

// T228 R1: the keys of a map by the action's bit: the fixed actions 0 .. HK_NACT-1, then the hotkey tasks HK_NACT + j (j < m.ntask)
static int n_keys(const HkMap &m)
{
    return HK_NACT + (m.ntask < HK_TASKS_MAX ? m.ntask : HK_TASKS_MAX);
}
static int key_at(const HkMap &m, int a)
{
    return a < HK_NACT ? m.act[a] : m.task[a - HK_NACT];
}

void hk_regs(const HkMap &m, uint8_t out[HK_NREGS])
{
    memset(out, 0, HK_NREGS);
    if (m.mod_a >= HK_NKEYS) {
        return;
    }
    out[0] = (uint8_t)(0x80 | m.mod_a);
    out[1] = m.mod_b < HK_NKEYS ? (uint8_t)(0x80 | m.mod_b) : 0;
    for (int a = 0; a < n_keys(m); a++) {
        int k = key_at(m, a);
        if (k >= HK_NKEYS || is_mod(m, k)) {
            continue;
        }
        if (k == HK_RESTORE) {
            out[10] |= 1;
        } else {
            out[2 + k / 8] |= (uint8_t)(1u << (k % 8));
        }
    }
}

// ---- detection ---------------------------------------------------------------------------------------------------------------------------
bool hk_modifier_held(const HkMap &m, const HkKeys &keys)
{
    if (m.mod_a >= HK_NKEYS || !keys.down(m.mod_a)) {
        return false;
    }
    return m.mod_b >= HK_NKEYS || keys.down(m.mod_b);
}

void HkDetect::reset()
{
    active_ = 0;
    next_rep_[0] = next_rep_[1] = 0;
}

uint32_t HkDetect::poll(const HkMap &m, const HkKeys &keys, uint32_t now)
{
    if (m.mod_a >= HK_NKEYS || !hk_modifier_held(m, keys)) {
        active_ = 0;
        return 0;
    }
    uint32_t cur = 0;
    for (int a = 0; a < n_keys(m); a++) {
        int k = key_at(m, a);
        if (k < HK_NKEYS && !is_mod(m, k) && keys.down(k)) {
            cur |= 1u << a;
        }
    }
    uint32_t fire = cur & ~active_;
    for (int v = 0; v < 2; v++) {
        uint32_t bit = 1u << (HKA_VOL_UP + v);
        if (fire & bit) {
            next_rep_[v] = now + HK_REPEAT_FIRST_MS;
        } else if ((cur & bit) && (int32_t)(now - next_rep_[v]) >= 0) {
            fire |= bit;
            next_rep_[v] = now + HK_REPEAT_MS;
        }
    }
    active_ = cur;
    return fire;
}

// ---- the USB keyboard --------------------------------------------------------------------------------------------------------------------
static uint64_t pack(const uint8_t mtx[8])
{
    uint64_t k = 0;
    for (int i = 0; i < 8; i++) {
        k |= (uint64_t)mtx[i] << (8 * i);
    }
    return k;
}

void HkFilter::apply(const HkMap &m, uint8_t mtx[8], uint8_t *restore)
{
    if (m.mod_a >= HK_NKEYS) {
        reset();
        return;
    }
    HkKeys raw;
    raw.k = pack(mtx);
    raw.restore = restore && (*restore & 1);
    uint8_t regs[HK_NREGS];
    hk_regs(m, regs);
    uint64_t mask = 0;
    for (int i = 0; i < 8; i++) {
        mask |= (uint64_t)regs[2 + i] << (8 * i);
    }
    bool rmask = regs[10] & 1;
    if (hk_modifier_held(m, raw)) {
        hidden_ |= raw.k & mask;
        if (raw.restore && rmask) {
            hidden_restore_ = true;
        }
    }
    hidden_ &= raw.k;                                      // a hidden key stays hidden until it is up
    if (!raw.restore) {
        hidden_restore_ = false;
    }
    uint64_t out = raw.k & ~hidden_;
    for (int i = 0; i < 8; i++) {
        mtx[i] = (uint8_t)(out >> (8 * i));
    }
    if (restore && hidden_restore_) {
        *restore &= ~1;
    }
}

static HkMap g_usb_map = { HK_NONE, HK_NONE, { HK_NONE, HK_NONE, HK_NONE, HK_NONE, HK_NONE, HK_NONE, HK_NONE }, 0, { HK_NONE, HK_NONE, HK_NONE, HK_NONE } };
static HkFilter g_usb_filter;
static volatile uint32_t g_usb_lo, g_usb_hi;
static volatile uint8_t g_usb_restore;
static uint8_t g_hid_hidden[32];

void hk_usb_set_map(const HkMap &m)
{
    g_usb_map = m;
}

void hk_usb_matrix_filter(uint8_t mtx[8], uint8_t *restore)
{
    uint64_t raw = pack(mtx);
    g_usb_lo = (uint32_t)raw;
    g_usb_hi = (uint32_t)(raw >> 32);
    g_usb_restore = restore ? (uint8_t)(*restore & 1) : 0;
    g_usb_filter.apply(g_usb_map, mtx, restore);
}

HkKeys hk_usb_keys()
{
    HkKeys k;
    k.k = ((uint64_t)g_usb_hi << 32) | g_usb_lo;
    k.restore = g_usb_restore != 0;
    return k;
}

static uint8_t (*g_usage_matrix)(uint8_t);
void hk_usb_set_usage_map(uint8_t (*f)(uint8_t))
{
    g_usage_matrix = f;
}

void hk_usb_report_filter(const uint8_t in[8], uint8_t out[8])
{
    uint8_t (*usage_matrix)(uint8_t) = g_usage_matrix;
    memcpy(out, in, 8);
    if (g_usb_map.mod_a >= HK_NKEYS || !usage_matrix) {
        memset(g_hid_hidden, 0, sizeof(g_hid_hidden));
        return;
    }
    HkKeys raw;
    if (in[0] & 0x11) raw.k |= 1ull << K_CTRL;
    if (in[0] & 0x02) raw.k |= 1ull << K_LSHIFT;
    if (in[0] & 0x20) raw.k |= 1ull << K_RSHIFT;
    if (in[0] & 0x88) raw.k |= 1ull << K_CBM;
    for (int i = 2; i < 8; i++) {
        uint8_t k = in[i] ? usage_matrix(in[i]) : HK_NONE;
        if (k < 64) {
            raw.k |= 1ull << k;
        }
    }
    bool held = hk_modifier_held(g_usb_map, raw);
    uint8_t seen[32];
    memset(seen, 0, sizeof(seen));
    for (int i = 2; i < 8; i++) {
        uint8_t u = in[i];
        uint8_t k = u ? usage_matrix(u) : HK_NONE;
        if (!u || k >= HK_NKEYS || is_mod(g_usb_map, k)) {
            continue;
        }
        bool act = false;
        for (int a = 0; a < n_keys(g_usb_map); a++) {
            act |= key_at(g_usb_map, a) == k;
        }
        if (act && held) {
            g_hid_hidden[u / 8] |= (uint8_t)(1u << (u % 8));
        }
        seen[u / 8] |= (uint8_t)(1u << (u % 8));
        if (g_hid_hidden[u / 8] & (1u << (u % 8))) {
            out[i] = 0;
        }
    }
    for (int i = 0; i < 32; i++) {
        g_hid_hidden[i] &= seen[i];                        // a hidden key stays hidden until it is up
    }
}

// ---- the settings ------------------------------------------------------------------------------------------------------------------------
// The keys of the six fixed action items by the action's bit; the item's label and values are its row of the base UI TOML (the page that lists them is called Hotkeys, T187 R6: no prefix on a label).
struct ItemDef { const char *key, *label; };
static const char *const g_item_keys[HK_NACT] = { "Hotkey menu", "Hotkey volume up", "Hotkey volume down", "Hotkey reset", "Hotkey swap joystick", "Hotkey picture mode", 0 };   // HKA_CORE: named by the descriptor's HkSpec
static const BaseRow *item_row(int a)
{
    return a >= 0 && a < HKA_CORE ? menu_base_row(HK_OP, g_item_keys[a]) : 0;
}
static ItemDef item_of(const HkSpec *sp, int a)
{
    if (a == HKA_CORE) {
        ItemDef d = { sp ? sp->core_key : 0, sp ? sp->core_label : 0 };
        return d;
    }
    const BaseRow *r = item_row(a);
    ItemDef d = { g_item_keys[a], r && r->label ? r->label : g_item_keys[a] };
    return d;
}
static int n_items(const HkSpec *sp)
{
    return sp && sp->core_op ? HK_NACT : HK_NACT - 1;
}
static const HkSpec *spec_of(const CoreDesc *d)
{
    return d ? d->hotkeys : 0;
}

// ---- T228 R1: the tasks of the core's menu that declare a hotkey ---------------------------------------------------------------------------------
int hk_tasks(const CoreDesc *d, int idx[HK_TASKS_MAX])
{
    int n = 0;
    for (int i = 0; d && d->hotkeys && i < d->ntasks && n < HK_TASKS_MAX; i++) {
        if (d->tasks[i].hotkey) {
            idx[n++] = i;
        }
    }
    return n;
}

// The item of a hotkey task, kept for the run: the .cfg key "Hotkey <label>" and the label the Hotkeys page shows (the task's without the "..." that says
// its row opens something). The same label gives the same item, so the settings' pointers stay valid when the menu is merged again.
struct TaskItem { char key[48]; char label[40]; };
static TaskItem g_titem[16];
static int g_ntitem;
static const TaskItem *task_item(const char *task_label)
{
    char base[40];
    snprintf(base, sizeof(base), "%s", task_label ? task_label : "");
    size_t l = strlen(base);
    while (l > 0 && (base[l - 1] == '.' || base[l - 1] == ' ')) {
        base[--l] = 0;
    }
    for (int i = 0; i < g_ntitem; i++) {
        if (!strcmp(g_titem[i].label, base)) {
            return &g_titem[i];
        }
    }
    if (!base[0] || g_ntitem >= (int)(sizeof(g_titem) / sizeof(g_titem[0]))) {
        return 0;
    }
    TaskItem &t = g_titem[g_ntitem++];
    snprintf(t.label, sizeof(t.label), "%s", base);
    snprintf(t.key, sizeof(t.key), "Hotkey %s", base);
    return &t;
}

// The matrix key of a task's default letter or digit (the key whose cap is that character), HK_NONE: none.
static int key_of_char(char ch)
{
    for (int i = 0; ch && i < 64; i++) {
        if (g_keys[i][0] == ch && !g_keys[i][1]) {
            return i;
        }
    }
    return HK_NONE;
}

// The default key of hotkey task j (idx from hk_tasks): its letter, unless the modifier, a key of the core's own (own_keys), the default of a fixed action or
// of an earlier task has it - then none, and the person sets one on the Hotkeys page.
static int task_default(const CoreDesc *d, const int *idx, int j)
{
    const HkSpec *sp = spec_of(d);
    const int k = key_of_char(d->tasks[idx[j]].hotkey);
    if (!sp || k == HK_NONE || is_mod(sp->def, k) || ((sp->own_keys >> k) & 1)) {
        return HK_NONE;
    }
    for (int a = 0; a < HK_NACT; a++) {
        if (sp->def.act[a] == k) {
            return HK_NONE;
        }
    }
    for (int i = 0; i < j; i++) {
        if (key_of_char(d->tasks[idx[i]].hotkey) == k) {
            return HK_NONE;
        }
    }
    return k;
}

// The items of a map by the action's bit (n_keys, key_at): does action a have an item in this core, and which (key, label)?
static bool has_item(const HkSpec *sp, int nt, int a)
{
    return a < HK_NACT ? a < n_items(sp) : a - HK_NACT < nt;
}
static ItemDef item_at(const CoreDesc *d, int a)
{
    if (a < HK_NACT) {
        return item_of(spec_of(d), a);
    }
    int idx[HK_TASKS_MAX];
    const int nt = hk_tasks(d, idx);
    const TaskItem *t = a - HK_NACT < nt ? task_item(d->tasks[idx[a - HK_NACT]].label) : 0;
    ItemDef r = { t ? t->key : 0, t ? t->label : 0 };
    return r;
}

static int val_of_key(int k)
{
    return k < HK_NKEYS ? k + 1 : 0;
}

const char *hk_extend(const CoreDesc *d, CoreDesc *out, Setting *store)
{
    *out = *d;
    const HkSpec *sp = spec_of(d);
    if (!sp) {
        return 0;
    }
    for (int i = 0; i < d->nsettings; i++) {
        if (!strcasecmp(d->settings[i].key, HK_KEY_MOD)) {
            return 0;
        }
    }
    if (d->nsettings + 1 + n_items(sp) > SETTINGS_MAX) {
        return "settings: no room for the Hotkey items the application adds";
    }
    const BaseRow *mrow = menu_base_row(HK_OP, HK_KEY_MOD), *krow = item_row(HKA_MENU);
    if (!mrow || !krow || mrow->nvalues != NPRE || krow->nvalues != HK_NKEYS + 1) {
        return "settings: the base UI TOML has no hotkey rows that fit the map";
    }
    for (int a = 0; a < HKA_CORE; a++) {
        if (!item_row(a) || item_row(a)->nvalues != HK_NKEYS + 1) {
            return "settings: the base UI TOML has no hotkey rows that fit the map";
        }
    }
    make_vals();
    make_pc_pre();
    const bool pc = sp->pc_keys;
    if (store != d->settings) {
        for (int i = 0; i < d->nsettings; i++) {
            store[i] = d->settings[i];
        }
    }
    int n = d->nsettings;
    Setting &m = store[n++];
    menu_base_setting(m, *mrow);
    m.values = pc ? g_pc_pre_names : mrow->values;          // the provider "hotkeys.modifiers"
    m.def = preset_of(sp->def);                             // the core's map
    for (int a = 0; a < n_items(sp); a++) {
        Setting &s = store[n++];
        menu_base_setting(s, a == HKA_CORE ? *krow : *item_row(a));     // the core's own function has the shape of the others, named by its HkSpec
        s.key = item_of(sp, a).key;
        s.label = item_of(sp, a).label;
        s.values = pc ? g_pc_vals : krow->values;           // the provider "hotkeys.keys"
        s.def = val_of_key(sp->def.act[a]);
        if (a == HKA_SWAP) {                               // R20: the swap hotkey is offered only while the core has the setting it toggles (menu condition "set.<key>")
            const char *sk = sp->swap_key ? sp->swap_key : "Joystick ports";
            bool has = false;
            for (int i = 0; i < d->nsettings; i++) {
                has = has || !strcasecmp(d->settings[i].key, sk);
            }
            has = has || baseset_adds(d, sk);                  // the application's "Joystick Input" is added after this item (menubuild.cc)
            static char cond[96];
            snprintf(cond, sizeof(cond), "set.%s", sk);
            for (char *p = cond; *p; p++) {
                *p = *p == ' ' ? '_' : *p;
            }
            s.show_state = cond;
            if (!has) {
                s.def = 0;                                 // no key: the J combination reaches the C64 as it is
            }
        }
    }
    out->settings = store;
    out->nsettings = n;
    return 0;
}

const char *hk_extend_tasks(CoreDesc *d, Setting *store, int cap)
{
    int idx[HK_TASKS_MAX];
    const int nt = hk_tasks(d, idx);
    if (!nt) {
        return 0;
    }
    const TaskItem *ti[HK_TASKS_MAX];
    int need = 0;
    for (int j = 0; j < nt; j++) {
        ti[j] = task_item(d->tasks[idx[j]].label);
        for (int i = 0; ti[j] && i < d->nsettings; i++) {
            if (!strcasecmp(d->settings[i].key, ti[j]->key)) {
                ti[j] = 0;                                 // there already (a second merge with the same table)
            }
        }
        need += ti[j] ? 1 : 0;
    }
    if (!need) {
        return 0;
    }
    if (d->nsettings + need > cap) {
        return "settings: no room for the hotkey items of the core's tasks";
    }
    if (d->settings != store) {
        for (int i = 0; i < d->nsettings; i++) {
            store[i] = d->settings[i];
        }
    }
    const BaseRow *krow = item_row(HKA_MENU);
    if (!krow || krow->nvalues != HK_NKEYS + 1) {
        return "settings: the base UI TOML has no hotkey rows that fit the map";
    }
    make_vals();
    make_pc_pre();
    const bool pc = spec_of(d)->pc_keys;
    int n = d->nsettings;
    for (int j = 0; j < nt; j++) {
        if (!ti[j]) {
            continue;
        }
        Setting &s = store[n++];
        menu_base_setting(s, *krow);                       // a task's item has the shape of the action items, named by the task
        s.key = ti[j]->key;
        s.label = ti[j]->label;
        s.values = pc ? g_pc_vals : krow->values;
        s.def = val_of_key(task_default(d, idx, j));
    }
    d->settings = store;
    d->nsettings = n;
    return 0;
}

bool hk_is_generic(const Setting &s)
{
    return s.apply && !strcmp(s.apply, HK_OP);
}

HkMap hk_map_of(const CoreCtx *c)
{
    HkMap m = { HK_NONE, HK_NONE, { HK_NONE, HK_NONE, HK_NONE, HK_NONE, HK_NONE, HK_NONE, HK_NONE }, 0, { HK_NONE, HK_NONE, HK_NONE, HK_NONE } };
    const HkSpec *sp = spec_of(c->desc);
    if (!sp) {
        return m;
    }
    m = sp->def;
    int idx[HK_TASKS_MAX];
    m.ntask = (uint8_t)hk_tasks(c->desc, idx);                 // T228 R1: the hotkey tasks, their default keys until the settings say otherwise
    for (int j = 0; j < HK_TASKS_MAX; j++) {
        m.task[j] = j < m.ntask ? (uint8_t)task_default(c->desc, idx, j) : HK_NONE;
    }
    if (!c->settings) {
        return m;
    }
    int i = c->settings->find(HK_KEY_MOD);
    if (i < 0 || !hk_is_generic(c->desc->settings[i])) {
        return m;                                          // the items are not there (an extended descriptor is the menu's; a test may use the plain one)
    }
    int v = c->settings->value(i);
    if (v >= 0 && v < NPRE) {
        m.mod_a = g_pre[v].a;
        m.mod_b = g_pre[v].b;
    }
    for (int a = 0; a < n_keys(m); a++) {
        const char *key = has_item(sp, m.ntask, a) ? item_at(c->desc, a).key : 0;
        int j = key ? c->settings->find(key) : -1;
        if (j < 0) {
            continue;
        }
        int w = c->settings->value(j);
        const uint8_t k = w <= 0 || w > HK_NKEYS ? HK_NONE : (uint8_t)(w - 1);
        if (a < HK_NACT) {
            m.act[a] = k;
        } else {
            m.task[a - HK_NACT] = k;
        }
    }
    for (int j = 0; j < m.ntask; j++) {                    // gate fix 3: a task's key (default or stored) never doubles the key of a fixed action or of an earlier task
        for (int a = 0; a < HK_NACT && m.task[j] != HK_NONE; a++) {
            if (m.act[a] == m.task[j]) {
                m.task[j] = HK_NONE;
            }
        }
        for (int i = 0; i < j && m.task[j] != HK_NONE; i++) {
            if (m.task[i] == m.task[j]) {
                m.task[j] = HK_NONE;
            }
        }
    }
    return m;
}

static int op_apply(CoreCtx *c, const OpArgs &, OpOut &)
{
    hk_apply(c);
    return OPE_OK;
}
static const ArgSpec hk_args[] = { ARG_INT("value", 0, HK_NKEYS) };

void hk_register(OpRegistry &r)
{
    r.add_op(HK_OP, hk_args, 1, op_apply);
}

static HkMap g_current = { HK_NONE, HK_NONE, { HK_NONE, HK_NONE, HK_NONE, HK_NONE, HK_NONE, HK_NONE, HK_NONE }, 0, { HK_NONE, HK_NONE, HK_NONE, HK_NONE } };

void hk_apply(CoreCtx *c)
{
    HkMap m = hk_map_of(c);
    for (int j = 0; c->settings && j < m.ntask; j++) {      // gate fix 3: the Hotkeys page shows what the map holds - a task given no key by a clash shows none
        int idx[HK_TASKS_MAX];
        hk_tasks(c->desc, idx);
        const TaskItem *t = task_item(c->desc->tasks[idx[j]].label);
        const int i = t ? c->settings->find(t->key) : -1;
        if (i >= 0 && m.task[j] == HK_NONE && c->settings->value(i) != 0) {
            c->settings->set(i, 0);
        }
    }
    uint8_t regs[HK_NREGS];
    hk_regs(m, regs);
    if (c->io) {
        c->io->hotkey_regs(regs);
    }
    hk_usb_set_map(m);
    g_current = m;
}

const HkMap &hk_current()
{
    return g_current;
}

// ---- actions -----------------------------------------------------------------------------------------------------------------------------
static bool step_key(CoreCtx *c, const char *key, int dir, bool wrap, const char *what, char *line, int n)
{
    int i = c->settings ? c->settings->find(key) : -1;
    if (i < 0) {
        snprintf(line, n, "No %s in this core", what);
        return false;
    }
    const Setting &s = c->desc->settings[i];
    int v = c->settings->value(i) + dir;
    if (v < 0) v = wrap ? s.nvalues - 1 : 0;
    if (v >= s.nvalues) v = wrap ? 0 : s.nvalues - 1;
    if (v != c->settings->value(i)) {
        OpIn in[2] = { in_str("key", s.key), in_str("value", s.values[v]) };
        static OpResult r;
        if (ops_call(c, "settings.set", in, 2, &r) != 0) {
            snprintf(line, n, "%s", r.msg);
            return false;
        }
    }
    snprintf(line, n, "%s %s", s.label ? s.label : s.key, s.values[v]);
    return true;
}

bool hk_step_volume(CoreCtx *c, int dir, char *line, int n)
{
    const HkSpec *sp = spec_of(c->desc);
    return step_key(c, sp && sp->vol_key ? sp->vol_key : VOLUME_KEY, dir, false, "volume", line, n);
}

bool hk_swap_joystick(CoreCtx *c, char *line, int n)
{
    const HkSpec *sp = spec_of(c->desc);
    const char *key = sp && sp->swap_key ? sp->swap_key : "Joystick ports";
    int i = c->settings ? c->settings->find(key) : -1;
    if (i < 0) {
        snprintf(line, n, "No joystick swap in this core");
        return false;
    }
    return step_key(c, key, c->settings->value(i) == 0 ? 1 : -c->settings->value(i), false, "joystick swap", line, n);
}

bool hk_next_picture(CoreCtx *c, char *line, int n)
{
    return step_key(c, PICTURE_KEY, 1, true, "picture mode", line, n);
}

bool hk_core_action(CoreCtx *c, char *line, int n)
{
    const HkSpec *sp = spec_of(c->desc);
    if (!sp || !sp->core_op) {
        snprintf(line, n, "No such function in this core");
        return false;
    }
    static OpResult r;
    int rc = ops_call(c, sp->core_op, 0, 0, &r);
    snprintf(line, n, "%s", r.msg[0] ? r.msg : (rc ? "failed" : (sp->core_label ? sp->core_label : "done")));
    return rc == 0;
}

bool hk_run_task(CoreCtx *c, int j, char *line, int n)
{
    int idx[HK_TASKS_MAX];
    if (j < 0 || j >= hk_tasks(c->desc, idx)) {
        snprintf(line, n, "No such hotkey in this core");
        return false;
    }
    const Task &t = c->desc->tasks[idx[j]];
    static OpResult r;
    int rc = ops_run_bound(c, t.op, t.bind, t.nbind, 0, &r);
    snprintf(line, n, "%s", r.msg[0] ? r.msg : (rc ? "failed" : t.label));
    return rc == 0;
}

static const char *key_label(const HkSpec *sp, int i);
void hk_task_keys_text(const CoreCtx *c, int ti, char *out, int n)
{
    out[0] = 0;
    int idx[HK_TASKS_MAX];
    const int nt = hk_tasks(c->desc, idx);
    const HkSpec *sp = spec_of(c->desc);
    const HkMap m = hk_map_of(c);
    for (int j = 0; j < nt; j++) {
        if (idx[j] != ti || m.mod_a >= HK_NKEYS || m.task[j] >= HK_NKEYS) {
            continue;
        }
        if (m.mod_b < HK_NKEYS) {
            snprintf(out, n, "%s + %s + %s", key_label(sp, m.mod_a), key_label(sp, m.mod_b), key_label(sp, m.task[j]));
        } else {
            snprintf(out, n, "%s + %s", key_label(sp, m.mod_a), key_label(sp, m.task[j]));
        }
    }
}

bool hk_reset(CoreCtx *c, char *line, int n)
{
    static OpResult r;
    int rc = ops_call(c, "machine.cold_reset", 0, 0, &r);
    snprintf(line, n, "%s", rc ? r.msg : "Reset");
    return rc == 0;
}

// ---- the loop's side ---------------------------------------------------------------------------------------------------------------------
uint32_t HkLoop::poll(CoreCtx *c)
{
    const HkMap &hm = hk_current();
    if (!c->started || hm.mod_a >= HK_NKEYS) {
        return 0;
    }
    uint32_t now = c->io->now_ms();
    if (now - last_ms < HK_POLL_MS) {
        return 0;
    }
    last_ms = now;
    return det.poll(hm, hk_read_keys(c), now);
}

HkKeys hk_read_keys(CoreCtx *c)
{
    HkKeys keys = hk_usb_keys();
    uint8_t mt[8];
    bool rs = false;
    if (c->io && c->io->keys_read(mt, &rs)) {
        for (int i = 0; i < 8; i++) {
            keys.k |= (uint64_t)mt[i] << (8 * i);
        }
        keys.restore = keys.restore || rs;
    }
    return keys;
}

// ---- learning (T207) ---------------------------------------------------------------------------------------------------------------------
void HkLearn::begin(const HkKeys &now, bool modifier, uint32_t t, bool pad)
{
    pad_ = pad;
    ign_k_ = now.k;
    ign_r_ = now.restore;
    peak_k_ = last_k_ = 0;
    peak_r_ = last_r_ = false;
    mod_ = modifier;
    since_ = t;
}

bool HkLearn::poll(const HkKeys &now, uint32_t t, HkKeys *got)
{
    ign_k_ &= now.k;
    ign_r_ = ign_r_ && now.restore;
    const uint64_t ck = now.k & ~ign_k_;
    const bool cr = now.restore && !ign_r_;
    const uint64_t shifts = (1ull << 15) | (1ull << 52);
    if (pad_ && ck && !(ck & ~shifts) && !cr) {            // pad-key row: SHIFT held alone is the start of SHIFT + cursor, keep waiting
        if (peak_k_ & ~shifts) {                           // the key was released while SHIFT is still held: the keys that were held
            got->k = peak_k_;
            got->restore = false;
            return true;
        }
        peak_k_ = 0;
        last_k_ = 0;
        since_ = t;
        return false;
    }
    if (!ck && !cr) {
        if (mod_ && (peak_k_ || peak_r_)) {                // released before it settled: the keys that were held
            got->k = peak_k_;
            got->restore = peak_r_;
            return true;
        }
        return false;
    }
    if (!mod_) {
        got->k = 0;
        got->restore = false;
        if (ck) {
            got->k = ck & (~ck + 1);                       // the lowest key index
        } else {
            got->restore = true;
        }
        return true;
    }
    peak_k_ |= ck;
    peak_r_ = peak_r_ || cr;
    if (ck != last_k_ || cr != last_r_) {
        last_k_ = ck;
        last_r_ = cr;
        since_ = t;
        return false;
    }
    if (t - since_ >= HK_LEARN_SETTLE_MS) {
        got->k = ck;
        got->restore = cr;
        return true;
    }
    return false;
}

static const char *key_label(const HkSpec *sp, int i)
{
    return sp && sp->pc_keys ? pc_name(i) : g_keys[i];
}
// The key in a sentence: a one-character name is quoted ("-" is already Volume down), a longer one stands as it is.
static const char *key_say(const HkSpec *sp, int i, char *buf)
{
    const char *k = key_label(sp, i);
    if (strlen(k) == 1) {
        snprintf(buf, 8, "\"%s\"", k);
        return buf;
    }
    return k;
}

// The action a hotkey item stands for, -1: the setting is the modifier or no hotkey item.
static int action_of(const CoreCtx *c, int set)
{
    const HkSpec *sp = spec_of(c->desc);
    if (!sp || set < 0 || set >= c->desc->nsettings || !hk_is_generic(c->desc->settings[set])) {
        return -1;
    }
    int idx[HK_TASKS_MAX];
    const int nt = hk_tasks(c->desc, idx);
    for (int a = 0; a < HK_NACT + nt; a++) {
        const char *k = has_item(sp, nt, a) ? item_at(c->desc, a).key : 0;
        if (k && !strcasecmp(k, c->desc->settings[set].key)) {
            return a;
        }
    }
    return -1;
}

bool hk_learn_is_modifier(const CoreCtx *c, int set)
{
    return set >= 0 && set < c->desc->nsettings && hk_is_generic(c->desc->settings[set]) && !strcasecmp(c->desc->settings[set].key, HK_KEY_MOD);
}

bool hk_learn_item(const CoreCtx *c, int set)
{
    return hk_learn_is_modifier(c, set) || action_of(c, set) >= 0 || (set >= 0 && set < c->desc->nsettings && padkey_row(c->desc->settings[set]));   // T232: the pad-key rows learn the same way
}

int hk_learn_value(CoreCtx *c, int set, const HkKeys &got, char *line, int n)
{
    const HkSpec *sp = spec_of(c->desc);
    const HkMap m = hk_map_of(c);
    line[0] = 0;
    int keys[3], nk = 0;                                    // the keys pressed (a modifier may be two)
    for (int i = 0; i < 64 && nk < 3; i++) {
        if ((got.k >> i) & 1) {
            keys[nk++] = i;
        }
    }
    if (got.restore && nk < 3) {
        keys[nk++] = HK_RESTORE;
    }
    if (nk == 0) {
        snprintf(line, n, "No key");
        return -1;
    }
    if (set >= 0 && set < c->desc->nsettings && padkey_row(c->desc->settings[set])) {         // T232: pad-key row: the key's matrix place picks the list entry
        if (got.restore) {                                  // RESTORE has no matrix place, so it is no entry of the list (it said "No key" before)
            snprintf(line, n, "%s", PADKEY_NOT_AVAILABLE);
            return -1;
        }
        return padkey_learn_value(got.k, line, n);
    }
    if (hk_learn_is_modifier(c, set)) {
        for (int p = 1; p < NPRE; p++) {
            const bool one = g_pre[p].b == HK_NONE;
            if ((one ? 1 : 2) != nk) {
                continue;
            }
            bool a = false, b = one;
            for (int j = 0; j < nk; j++) {
                a = a || keys[j] == g_pre[p].a;
                b = b || keys[j] == g_pre[p].b;
            }
            if (!a || !b) {
                continue;
            }
            for (int act = 0; sp && act < n_keys(m); act++) {                   // a modifier key may not be the key of a hotkey
                const int ak = key_at(m, act);
                if (has_item(sp, m.ntask, act) && (ak == g_pre[p].a || ak == g_pre[p].b)) {
                    { char kb[8]; snprintf(line, n, "%s is already %s", key_say(sp, ak, kb), item_at(c->desc, act).label); }
                    return -1;
                }
            }
            return p;
        }
        if (nk == 1) {
            { char kb[8]; snprintf(line, n, "%s is not a modifier here", key_say(sp, keys[0], kb)); }
        } else {
            snprintf(line, n, "%s + %s is not one of the modifier pairs", key_label(sp, keys[0]), key_label(sp, keys[1]));
        }
        return -1;
    }
    const int act = action_of(c, set);
    if (act < 0) {
        snprintf(line, n, "Not a hotkey");
        return -1;
    }
    if (nk != 1) {
        snprintf(line, n, "Press one key");
        return -1;
    }
    const int k = keys[0];
    if (k == m.mod_a || k == m.mod_b) {
        { char kb[8]; snprintf(line, n, "%s is the modifier", key_say(sp, k, kb)); }
        return -1;
    }
    for (int a = 0; a < n_keys(m); a++) {
        if (a != act && has_item(sp, m.ntask, a) && key_at(m, a) == k) {
            { char kb[8]; snprintf(line, n, "%s is already %s", key_say(sp, k, kb), item_at(c->desc, a).label); }
            return -1;
        }
    }
    return val_of_key(k);
}

bool hk_value_refused(CoreCtx *c, int set, int v, char *line, int n)
{
    const HkSpec *sp = spec_of(c->desc);
    const HkMap m = hk_map_of(c);
    line[0] = 0;
    if (!sp || v <= 0) {                                       // Off / no key collides with nothing
        return false;
    }
    if (hk_learn_is_modifier(c, set)) {
        if (v >= NPRE) {
            return false;
        }
        for (int act = 0; act < n_keys(m); act++) {
            const int ak = key_at(m, act);
            if (has_item(sp, m.ntask, act) && ak != HK_NONE && (ak == g_pre[v].a || ak == g_pre[v].b)) {
                { char kb[8]; snprintf(line, n, "%s is already %s", key_say(sp, ak, kb), item_at(c->desc, act).label); }
                return true;
            }
        }
        return false;
    }
    const int act = action_of(c, set);
    if (act < 0 || v > HK_NKEYS) {
        return false;
    }
    const int k = v - 1;
    if (k == m.mod_a || k == m.mod_b) {
        { char kb[8]; snprintf(line, n, "%s is the modifier", key_say(sp, k, kb)); }
        return true;
    }
    for (int a = 0; a < n_keys(m); a++) {
        if (a != act && has_item(sp, m.ntask, a) && key_at(m, a) == k) {
            { char kb[8]; snprintf(line, n, "%s is already %s", key_say(sp, k, kb), item_at(c->desc, a).label); }
            return true;
        }
    }
    return false;
}

bool HkLoop::perform(CoreCtx *c, uint32_t f, char *line, int n)
{
    bool show = false;
    line[0] = 0;
    if (f & (1u << HKA_VOL_UP)) {
        hk_step_volume(c, 1, line, n);
        show = true;
    }
    if (f & (1u << HKA_VOL_DOWN)) {
        hk_step_volume(c, -1, line, n);
        show = true;
    }
    if (f & (1u << HKA_SWAP)) {
        hk_swap_joystick(c, line, n);
        show = true;
    }
    if (f & (1u << HKA_PICTURE)) {
        hk_next_picture(c, line, n);
        show = true;
    }
    if (f & (1u << HKA_CORE)) {
        hk_core_action(c, line, n);
        show = true;
    }
    for (int j = 0; j < HK_TASKS_MAX; j++) {                     // T228 R1: the core's hotkey tasks (a screen of the menu: the caller opens it)
        if (f & (1u << (HK_NACT + j))) {
            hk_run_task(c, j, line, n);
            show = true;
        }
    }
    if (f & (1u << HKA_RESET)) {
        char rl[64];
        hk_reset(c, rl, sizeof(rl));
        printf("[core] hotkey reset: %s\n", rl);
    }
    return show;
}
