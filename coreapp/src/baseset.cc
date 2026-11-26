#include "baseset.h"
#include "menubuild.h"
#include "hookhost.h"
#include "ops.h"
#include "ops_glue.h"
#include "settings.h"
#include "mouse_api.h"
#include "libtab.h"
#include <string.h>
#include <strings.h>

namespace {

enum { B_JOY, B_MODE, B_SENS, B_ACCEL, B_WSENS, B_WDIR, B_N, B_MOUSE0 = B_MODE };

struct Def {
    const char *op;                     // the operation: the row (key, label, values, default, base, condition) is the base UI TOML's (coreapp/base_ui.toml), found by it
    uint8_t kind;                       // baseset_kind: 2, an input row (1 was the look rows, gone with T239.3 D56)
    const char *needs;                  // the capability (CoreDesc::caps) a core has to name for the row; null: any core (the mouse rows: any core with a mouse)
};
#define N(a) (int)(sizeof(a) / sizeof(a[0]))
const Def defs[B_N] = {
    { "base.joystick", 2, BASE_CAP_KEYBOARD_JOYSTICK },
    { "base.mouse_mode", 2, BASE_CAP_MOUSE_DRIVER },
    { "base.mouse_sens", 2, 0 },
    { "base.mouse_accel", 2, 0 },
    { "base.wheel_sens", 2, BASE_CAP_MOUSE_DRIVER },
    { "base.wheel_dir", 2, BASE_CAP_MOUSE_DRIVER },
};

const BaseRow *row_of(int id)
{
    static const BaseRow *cache[B_N];
    if (!cache[id]) {
        cache[id] = menu_base_row(defs[id].op);
    }
    return cache[id];
}
const char *key_of(int id) { return row_of(id) ? row_of(id)->key : ""; }
int def_of(int id) { return row_of(id) ? row_of(id)->def : 0; }

CoreCtx *g_ctx;
const BaseSetPort *g_port;

// the lookup of the USB task: index per key, valid for one Settings object and its generation
const Settings *g_set;
uint32_t g_gen;
int16_t g_ix[B_N];

int val(CoreCtx *c, int id, int def)
{
    int ix = c->settings ? c->settings->find(key_of(id)) : -1;
    return ix >= 0 ? c->settings->value(ix) : def;
}

int usb_val(int id, int def)
{
    Settings *s = g_ctx ? g_ctx->settings : 0;
    if (!s) {
        return def;
    }
    if (s != g_set || s->generation() != g_gen) {
        g_set = s;
        g_gen = s->generation();
        for (int i = 0; i < B_N; i++) {
            g_ix[i] = (int16_t)s->find(key_of(i));
        }
    }
    return g_ix[id] >= 0 ? s->value(g_ix[id]) : def;
}

const uint8_t wasd_of[] = { 0x00, 0x00, 0x01, 0x01, 0x03, 0x03 };     // the host keyboard's WASD mapping per position of "Joystick Input"

int apply(CoreCtx *c, int id)
{
    const BaseSetPort *p = g_port;
    switch (id) {
    case B_JOY: {
        int v = val(c, B_JOY, 0);
        if (p && p->keyboard_joystick && v >= 0 && v < N(wasd_of)) {
            p->keyboard_joystick((uint8_t)(v & 1), wasd_of[v]);
        }
        break;
    }
    default:
        break;                                                      // the mouse is read by the USB driver at each report
    }
    hooks_base_changed(key_of(id));
    return OPE_OK;
}

#define OP(id, name) int op_##name(CoreCtx *c, const OpArgs &, OpOut &) { return apply(c, id); }
OP(B_JOY, joystick) OP(B_MODE, mouse_mode)
OP(B_SENS, mouse_sens) OP(B_ACCEL, mouse_accel) OP(B_WSENS, wheel_sens) OP(B_WDIR, wheel_dir)
const ArgSpec value_arg[] = { ARG_INT("value", 0, 16) };

bool declared(const CoreDesc *d, const char *key)
{
    for (int i = 0; i < d->nsettings; i++) {
        if (!strcasecmp(d->settings[i].key, key)) {
            return true;
        }
    }
    return false;
}

// Does the application add row `id` to this core: the capability it needs is named by the core, a mouse row needs a mouse, and the core does not declare the key itself.
// (A core's description can hide a row of the base by its key; contrib_merge drops it when the menu is merged.)
bool wanted(const CoreDesc *d, int id)
{
    if (declared(d, key_of(id))) {
        return false;
    }
    if (id >= B_MOUSE0 && !d->mouse) {
        return false;
    }
    return !defs[id].needs || desc_has_capability(d, defs[id].needs);
}

}

bool baseset_adds(const CoreDesc *d, const char *key)
{
    for (int i = 0; i < B_N; i++) {
        if (!strcasecmp(key_of(i), key)) {
            return wanted(d, i);
        }
    }
    return false;
}

const char *baseset_extend(const CoreDesc *d, CoreDesc *out, Setting *store)
{
    *out = *d;
    bool want[B_N];
    int add = 0;
    for (int i = 0; i < B_N; i++) {
        want[i] = wanted(d, i);
        add += want[i] ? 1 : 0;
    }
    if (!add) {
        return 0;
    }
    if (d->nsettings + add > SETTINGS_MAX) {
        return "settings: no room for the items the application adds for the joystick and the mouse";
    }
    if (store != d->settings) {
        for (int i = 0; i < d->nsettings; i++) {
            store[i] = d->settings[i];
        }
    }
    int n = d->nsettings;
    for (int i = 0; i < B_N; i++) {
        if (!want[i]) {
            continue;
        }
        const BaseRow *r = row_of(i);
        if (!r) {
            return "settings: the base UI TOML lacks a row of the application's joystick or mouse";
        }
        Setting &s = store[n++];
        menu_base_setting(s, *r);
        s.show_state = r->show_state && want[B_MODE] ? r->show_state : 0;     // "only while Mouse Mode says cursor keys" needs the row that says it
    }
    out->settings = store;
    out->nsettings = n;
    return 0;
}

void baseset_attach(CoreCtx *c, const BaseSetPort *port)
{
    g_ctx = c;
    g_port = port;
    g_set = 0;
    g_gen = 0;
}

int baseset_kind(const Setting &s)
{
    if (!s.apply || strncmp(s.apply, "base.", 5)) {
        return 0;
    }
    for (int i = 0; i < B_N; i++) {
        if (!strcmp(s.apply, defs[i].op)) {
            return defs[i].kind;
        }
    }
    return 0;
}

int baseset_count(void)
{
    return B_N;
}

const char *baseset_key(int i)
{
    return i >= 0 && i < B_N ? key_of(i) : "";
}

int baseset_usb_config(int key, int default_value)
{
    if (!g_ctx || !g_ctx->settings) {
        return default_value;
    }
    switch (key) {
    case 0x55: {                                                 // CFG_MOUSE_MODE: the list's three values are the driver's modes 0, 2, 3
        static const int driver_mode[3] = { 0, 2, 3 };
        int m = usb_val(B_MODE, 0);
        return driver_mode[m >= 0 && m < 3 ? m : 0];
    }
    case 0x56: return usb_val(B_WSENS, 7) + 1;                   // CFG_SCROLL_FACTOR 1..16
    case 0x57: return usb_val(B_WDIR, 0);                        // CFG_WHEEL_DIRECTION
    case 0x58: return 0;                                         // CFG_MENU_MOUSE_NAV: never; our menus are keys only (T104.1, Alex 2026-10-29)
    case 0x5D: return usb_val(B_SENS, 7) + 1;                    // CFG_MOUSE_SENSITIVITY 1..16
    case 0x5E: return usb_val(B_ACCEL, 0);                       // CFG_MOUSE_ACCELERATION
    }
    return default_value;
}

// The USB driver asks the application for its configuration items through this (weak in usb_hid.cc, Gideon's item numbers CFG_*, usb_hid_config.h). The mouse items are settings of the
// generic application (above); the base image defines the function, so an app linked without the mouse library has it too. A core that declares the two scaling items itself (the Amiga cores:
// the operations mouse.sens / mouse.accel set them through the mouse library's table, mouse_api.h) is answered first from there; every other core from the application's settings.
extern "C" int u64_get_usb_hid_config_value(int key, int default_value);
extern "C" int u64_get_usb_hid_config_value(int key, int default_value)
{
    const MouseApi *m = (const MouseApi *)libtab_export(MOUSE_API_NAME, 1);
    int v;
    if (m && m->size >= sizeof(MouseApi) && m->scaling(key, &v)) {
        return v;
    }
    return baseset_usb_config(key, default_value);
}

void baseset_register(OpRegistry &r)
{
    r.add_op("base.joystick", value_arg, 1, op_joystick);
    r.add_op("base.mouse_mode", value_arg, 1, op_mouse_mode);
    r.add_op("base.mouse_sens", value_arg, 1, op_mouse_sens);
    r.add_op("base.mouse_accel", value_arg, 1, op_mouse_accel);
    r.add_op("base.wheel_sens", value_arg, 1, op_wheel_sens);
    r.add_op("base.wheel_dir", value_arg, 1, op_wheel_dir);
}
