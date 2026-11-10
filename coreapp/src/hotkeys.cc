#include "hotkeys.h"
#include "ops.h"
#include "ops_glue.h"
#include "io.h"
#include "settings.h"
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

// the value labels of an action key item: "Off", then the 65 keys
static const char *g_vals[HK_NKEYS + 1];
static bool g_vals_made;
static void make_vals()
{
    if (g_vals_made) {
        return;
    }
    g_vals[0] = "Off";
    for (int i = 0; i < HK_NKEYS; i++) {
        g_vals[i + 1] = g_keys[i];
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
static const char *g_pre_names[NPRE];
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

void hk_regs(const HkMap &m, uint8_t out[HK_NREGS])
{
    memset(out, 0, HK_NREGS);
    if (m.mod_a >= HK_NKEYS) {
        return;
    }
    out[0] = (uint8_t)(0x80 | m.mod_a);
    out[1] = m.mod_b < HK_NKEYS ? (uint8_t)(0x80 | m.mod_b) : 0;
    for (int a = 0; a < HK_NACT; a++) {
        int k = m.act[a];
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
    for (int a = 0; a < HK_NACT; a++) {
        int k = m.act[a];
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

static HkMap g_usb_map = { HK_NONE, HK_NONE, { HK_NONE, HK_NONE, HK_NONE, HK_NONE, HK_NONE, HK_NONE, HK_NONE } };
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
        for (int a = 0; a < HK_NACT; a++) {
            act |= g_usb_map.act[a] == k;
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
struct ItemDef { const char *key, *label; };
static const ItemDef g_items[HK_NACT] = {
    { "Hotkey menu", "Hotkey: menu open / close" },
    { "Hotkey volume up", "Hotkey: volume up" },
    { "Hotkey volume down", "Hotkey: volume down" },
    { "Hotkey reset", "Hotkey: reset" },
    { "Hotkey swap joystick", "Hotkey: swap joystick ports" },
    { "Hotkey picture mode", "Hotkey: picture mode" },
    { 0, 0 },                                              // HKA_CORE: named by the descriptor's HkSpec
};
static ItemDef item_of(const HkSpec *sp, int a)
{
    if (a == HKA_CORE) {
        ItemDef d = { sp ? sp->core_key : 0, sp ? sp->core_label : 0 };
        return d;
    }
    return g_items[a];
}
static int n_items(const HkSpec *sp)
{
    return sp && sp->core_op ? HK_NACT : HK_NACT - 1;
}
static const HkSpec *spec_of(const CoreDesc *d)
{
    return d ? d->hotkeys : 0;
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
    make_vals();
    for (int i = 0; i < NPRE; i++) {
        g_pre_names[i] = g_pre[i].name;
    }
    if (store != d->settings) {
        for (int i = 0; i < d->nsettings; i++) {
            store[i] = d->settings[i];
        }
    }
    int n = d->nsettings;
    Setting &m = store[n++];
    m = Setting();
    m.key = HK_KEY_MOD;
    m.label = "Hotkey modifier";
    m.values = g_pre_names;
    m.nvalues = NPRE;
    m.def = preset_of(sp->def);
    m.when = WHEN_LIVE;
    m.apply = HK_OP;
    m.group = "input";
    for (int a = 0; a < n_items(sp); a++) {
        Setting &s = store[n++];
        s = Setting();
        s.key = item_of(sp, a).key;
        s.label = item_of(sp, a).label;
        s.values = g_vals;
        s.nvalues = HK_NKEYS + 1;
        s.def = val_of_key(sp->def.act[a]);
        s.when = WHEN_LIVE;
        s.apply = HK_OP;
        s.group = "input";
    }
    out->settings = store;
    out->nsettings = n;
    return 0;
}

bool hk_is_generic(const Setting &s)
{
    return s.apply && !strcmp(s.apply, HK_OP);
}

HkMap hk_map_of(const CoreCtx *c)
{
    HkMap m = { HK_NONE, HK_NONE, { HK_NONE, HK_NONE, HK_NONE, HK_NONE, HK_NONE, HK_NONE, HK_NONE } };
    const HkSpec *sp = spec_of(c->desc);
    if (!sp) {
        return m;
    }
    m = sp->def;
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
    for (int a = 0; a < n_items(sp); a++) {
        int j = c->settings->find(item_of(sp, a).key);
        if (j < 0) {
            continue;
        }
        int w = c->settings->value(j);
        m.act[a] = w <= 0 || w > HK_NKEYS ? HK_NONE : (uint8_t)(w - 1);
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

static HkMap g_current = { HK_NONE, HK_NONE, { HK_NONE, HK_NONE, HK_NONE, HK_NONE, HK_NONE, HK_NONE, HK_NONE } };

void hk_apply(CoreCtx *c)
{
    HkMap m = hk_map_of(c);
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
    HkKeys keys = hk_usb_keys();
    uint8_t mt[8];
    bool rs = false;
    if (c->io->keys_read(mt, &rs)) {
        for (int i = 0; i < 8; i++) {
            keys.k |= (uint64_t)mt[i] << (8 * i);
        }
        keys.restore = keys.restore || rs;
    }
    return det.poll(hm, keys, now);
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
    if (f & (1u << HKA_RESET)) {
        char rl[64];
        hk_reset(c, rl, sizeof(rl));
        printf("[core] hotkey reset: %s\n", rl);
    }
    return show;
}
