#include "mousesink.h"
#include "baseset.h"

namespace {

MouseCrit *crit;
const MouseSinkSpec *g_spec;
int acc_dx, acc_dy;
uint8_t btn, btn_sent;
MousePots *g_pots;
int16_t pot_x, pot_y;
uint8_t pot_joy = 0x1F;
uint32_t last_ms;
bool first = true;

struct Guard {
    Guard() { if (crit) crit->enter(); }
    ~Guard() { if (crit) crit->leave(); }
};

int clampi(int v, int lim)
{
    return v > lim ? lim : (v < -lim ? -lim : v);
}

}

void mouse_sink_set_crit(MouseCrit *c)
{
    crit = c;
}

void mouse_sink_set_pots(MousePots *pots)
{
    g_pots = pots;
}

void mouse_sink_reset()
{
    pot_x = pot_y = 0;
    pot_joy = 0x1F;
    acc_dx = acc_dy = 0;
    btn = btn_sent = 0;
    last_ms = 0;
    first = true;
}

void mouse_sink_configure(const MouseSinkSpec *spec)
{
    g_spec = spec;
}

void mouse_sink_motion(int dx, int dy, uint8_t buttons)
{
    if (!g_spec) {
        return;                                       // the core has no mouse sink
    }
    if (g_spec->kind == MOUSE_POT1351) {
        // the driver's own rule (HidMouseInterpreter::applyRelativeMotion): X adds, Y subtracts; the position wraps
        pot_x = (int16_t)(pot_x + dx);
        pot_y = (int16_t)(pot_y - dy);
        uint8_t joy = 0x1F;
        if (buttons & 1) joy &= ~0x10;
        if (buttons & 2) joy &= ~0x01;
        if (buttons & 4) joy &= ~0x02;
        if (g_pots) {
            g_pots->position(pot_x, pot_y);
            if (joy != pot_joy) {
                g_pots->buttons(joy);
            }
            pot_joy = joy;
        }
        return;
    }
    Guard g;
    acc_dx = clampi(acc_dx + dx, g_spec->clamp);
    acc_dy = clampi(acc_dy + dy, g_spec->clamp);
    btn = buttons;
}

bool mouse_sink_usb_report(int dx, int dy, uint8_t buttons)
{
    mouse_sink_motion(dx, dy, buttons);
    return true;
}

bool mouse_sink_pace(CoreCtx *c)
{
    const MouseSinkSpec *m = c->desc->mouse;
    if (!m) {
        return false;
    }
    uint32_t now = c->io->now_ms();
    if (!first && now - last_ms < m->interval_ms) {
        return false;
    }
    int dx, dy;
    uint8_t b;
    {
        Guard g;
        dx = clampi(acc_dx, m->step);
        dy = clampi(acc_dy, m->step);
        acc_dx -= dx;
        acc_dy -= dy;
        b = btn;
    }
    if (b != btn_sent) {
        c->io->wr(m->reg_btn, b);
        btn_sent = b;
    }
    if (dx || dy) {
        c->io->wr(m->reg_dx, (uint8_t)(int8_t)dx);
        c->io->wr(m->reg_dy, (uint8_t)(int8_t)dy);     // sends the pair
        last_ms = now;
        first = false;
        return true;
    }
    return false;
}

// The USB driver asks the application for its configuration items through this (weak in usb_hid.cc, Gideon's item numbers CFG_*, usb_hid_config.h). The mouse items are
// settings of the generic application (baseset.h); the base image defines the function, no library does.
extern "C" int u64_get_usb_hid_config_value(int key, int default_value);
extern "C" int u64_get_usb_hid_config_value(int key, int default_value)
{
    return baseset_usb_config(key, default_value);
}
