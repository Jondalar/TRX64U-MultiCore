#include "baseset.h"
#include "hookhost.h"
#include "ops.h"
#include "ops_glue.h"
#include "settings.h"
#include "trx_scheme_names.h"
#include <string.h>
#include <strings.h>

namespace {

enum { B_TRANSP, B_SCHEME, B_SQUEEZE, B_NAV, B_JOY, B_MODE, B_SENS, B_ACCEL, B_WSENS, B_WDIR, B_N, B_MOUSE0 = B_MODE };

const char *const transp_values[] = { "0 %", "25 %", "50 %", "75 %" };
const char *const squeeze_values[] = { "None", "Beginning", "Middle", "End" };
const char *const nav_values[] = { "Quick Search", "WASD Cursors" };
const char *const joy_values[] = { "Normal", "Swapped", "WASD Port 2", "WASD Port 1", "WASD P2 No KB", "WASD P1 No KB" };
const char *const mode_values[] = { "Cursor keys", "Wheel: cursor keys", "Wheel: joystick" };
const char *const step_values[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16" };
const char *const accel_values[] = { "Off", "Adaptive" };
const char *const dir_values[] = { "Normal", "Reversed" };

struct Def {
    const char *key, *label;
    const char *const *values;
    int nvalues, def;
    int32_t base;
    const char *op;
    const char *show;
    uint8_t kind;
};
#define N(a) (int)(sizeof(a) / sizeof(a[0]))
const char *const cursor_only = "set.Mouse_Mode==Cursor keys";
const Def defs[B_N] = {
    { "Overlay Transparency", "Overlay transparency", transp_values, N(transp_values), 1, 0, "base.overlay", 0, 1 },
    { "Color Scheme", "Color scheme", trx_scheme_names, TRX_SCHEME_COUNT, 1, 0, "base.scheme", 0, 1 },
    { "Filename overflow squeeze", 0, squeeze_values, N(squeeze_values), 0, 0, "base.squeeze", 0, 1 },
    { "Navigation Style", "Navigation style", nav_values, N(nav_values), 0, 0, "base.nav", 0, 1 },
    { "Joystick Input", "Joystick input", joy_values, N(joy_values), 0, 0, "base.joystick", 0, 2 },
    { "Mouse Mode", "Mouse as", mode_values, N(mode_values), 0, 0, "base.mouse_mode", 0, 2 },
    { "Mouse Sensitivity", "Mouse cursor sensitivity", step_values, N(step_values), 7, 1, "base.mouse_sens", cursor_only, 2 },
    { "Mouse Acceleration", "Mouse cursor acceleration", accel_values, N(accel_values), 0, 0, "base.mouse_accel", cursor_only, 2 },
    { "Mouse Wheel Sensitivity", "Mouse wheel sensitivity", step_values, N(step_values), 7, 1, "base.wheel_sens", 0, 2 },
    { "Mouse Wheel Direction", "Mouse wheel direction", dir_values, N(dir_values), 0, 0, "base.wheel_dir", 0, 2 },
};

CoreCtx *g_ctx;
const BaseSetPort *g_port;

// the lookup of the USB task: index per key, valid for one Settings object and its generation
const Settings *g_set;
uint32_t g_gen;
int16_t g_ix[B_N];

int val(CoreCtx *c, int id, int def)
{
    int ix = c->settings ? c->settings->find(defs[id].key) : -1;
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
            g_ix[i] = (int16_t)s->find(defs[i].key);
        }
    }
    return g_ix[id] >= 0 ? s->value(g_ix[id]) : def;
}

const uint8_t wasd_of[] = { 0x00, 0x00, 0x01, 0x01, 0x03, 0x03 };     // the host keyboard's WASD mapping per position of "Joystick Input"

int apply(CoreCtx *c, int id)
{
    const BaseSetPort *p = g_port;
    switch (id) {
    case B_TRANSP:
    case B_SCHEME:
        if (p && p->look) {
            p->look(val(c, B_TRANSP, defs[B_TRANSP].def), val(c, B_SCHEME, defs[B_SCHEME].def));
        }
        break;
    case B_SQUEEZE:
        if (p && p->squeeze) {
            p->squeeze(val(c, B_SQUEEZE, defs[B_SQUEEZE].def));
        }
        break;
    case B_NAV: {
        // The default of a machine that has never been set up is WASD Cursors on the product sold under Commodore's name; a value the person stored stays.
        int ix = c->settings ? c->settings->find(defs[B_NAV].key) : -1;
        if (ix >= 0 && c->settings->layer(ix) == Settings::L_DEFAULT && p && p->commodore && p->commodore()) {
            c->settings->override_value(ix, 1);
        }
        break;
    }
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
    hooks_base_changed(defs[id].key);
    return OPE_OK;
}

#define OP(id, name) int op_##name(CoreCtx *c, const OpArgs &, OpOut &) { return apply(c, id); }
OP(B_TRANSP, overlay) OP(B_SCHEME, scheme) OP(B_SQUEEZE, squeeze) OP(B_NAV, nav) OP(B_JOY, joystick) OP(B_MODE, mouse_mode)
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

}

const char *baseset_extend(const CoreDesc *d, CoreDesc *out, Setting *store)
{
    *out = *d;
    bool want[B_N];
    int add = 0;
    for (int i = 0; i < B_N; i++) {
        want[i] = (i < B_MOUSE0 || d->mouse) && !declared(d, defs[i].key);
        add += want[i] ? 1 : 0;
    }
    if (!add) {
        return 0;
    }
    if (d->nsettings + add > SETTINGS_MAX) {
        return "settings: no room for the items the application adds for its look, the joystick and the mouse";
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
        Setting &s = store[n++];
        s = Setting();
        s.key = defs[i].key;
        s.label = defs[i].label;
        s.values = defs[i].values;
        s.nvalues = defs[i].nvalues;
        s.def = defs[i].def;
        s.when = WHEN_LIVE;
        s.apply = defs[i].op;
        s.base = defs[i].base;
        s.group = "app";
        s.show_state = defs[i].show;
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
    return i >= 0 && i < B_N ? defs[i].key : "";
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

void baseset_register(OpRegistry &r)
{
    r.add_op("base.overlay", value_arg, 1, op_overlay);
    r.add_op("base.scheme", value_arg, 1, op_scheme);
    r.add_op("base.squeeze", value_arg, 1, op_squeeze);
    r.add_op("base.nav", value_arg, 1, op_nav);
    r.add_op("base.joystick", value_arg, 1, op_joystick);
    r.add_op("base.mouse_mode", value_arg, 1, op_mouse_mode);
    r.add_op("base.mouse_sens", value_arg, 1, op_mouse_sens);
    r.add_op("base.mouse_accel", value_arg, 1, op_mouse_accel);
    r.add_op("base.wheel_sens", value_arg, 1, op_wheel_sens);
    r.add_op("base.wheel_dir", value_arg, 1, op_wheel_dir);
}
