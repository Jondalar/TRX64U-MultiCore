// T167: what the frame buffer holds BETWEEN two draw tasks. The applications render with one buffer that the pixel layer shows while LVGL draws
// into it (DIRECT mode, no second buffer), so every state between two tasks of a refresh is on the screen for a moment. A flush callback sees none
// of them (it is called once, after the last task). This is a draw unit that takes no task and, each time LVGL's dispatcher visits it, copies a
// rectangle of the buffer: it sits at the head of the unit list (lv_draw_create_unit), so it runs before the software unit picks the next task.
//
//   ovp::install();                       once, after the display exists
//   ovp::begin(fb, x, y, w, h);           before a change: remembers the settled rectangle, starts recording
//   ... change a label, lv_timer_handler() ...
//   int bad = ovp::end(&states);          the settled rectangle after; returns the number of (state, pixel) pairs that hold a pixel which is
//                                         neither the old picture's, the new picture's nor the panel's own colour
//
// A pixel of the menu below the overlay shows up here as "bad" because the overlay's pictures (old, new) and its own colour do not contain it.
#pragma once
#include "lvgl.h"
#include "src/draw/lv_draw_private.h"
#include "src/display/lv_display_private.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#ifndef LV_DRAW_UNIT_IDLE
#define LV_DRAW_UNIT_IDLE (-1)
#endif

namespace ovp {

typedef std::vector<uint16_t> Pic;

static uint16_t *g_fb;
static int g_x, g_y, g_w, g_h;
static bool g_armed;
static std::vector<Pic> g_states;
static Pic g_old;
static int g_visits;
static int g_areas;                  // the most invalidated areas LVGL held when a refresh began while armed (before it joins them): T167 5.9

static Pic grab(void)
{
    Pic p((size_t)g_w * g_h);
    for (int r = 0; r < g_h; r++) {
        memcpy(&p[(size_t)r * g_w], g_fb + (size_t)(g_y + r) * 960 + g_x, (size_t)g_w * 2);
    }
    return p;
}

static int32_t dispatch(lv_draw_unit_t *, lv_layer_t *)
{
    if (g_armed) {
        g_visits++;
        Pic p = grab();
        if (g_states.empty() || g_states.back() != p) {
            g_states.push_back(p);
        }
    }
    return LV_DRAW_UNIT_IDLE;
}

static void on_refr_start(lv_event_t *)
{
    lv_display_t *d = lv_display_get_default();
    if (g_armed && d && (int)d->inv_p > g_areas) {
        g_areas = (int)d->inv_p;
    }
}

static void install(void)
{
    lv_display_add_event_cb(lv_display_get_default(), on_refr_start, LV_EVENT_REFR_START, NULL);
    lv_draw_unit_t *u = (lv_draw_unit_t *)lv_draw_create_unit(sizeof(lv_draw_unit_t));
    u->dispatch_cb = dispatch;
    u->name = "probe";
}

// The product draws into ONE buffer that the pixel layer shows (hostui.cc buffers_mode(true), ui.cc single_buffer()). A bench display that still has two
// (the host bench opens two, as the port does, and its start-up scenarios use both) is switched to one on the first begin() when `g_single` is set: on the
// buffer that is shown, drawn once completely so that the picture is the same. The core bench's display is single after ui_open() and must stay as its
// own controls leave it (UI_KEEP_TWO_BUFFERS), so it does not set it.
static bool g_single;
static void begin(uint16_t *fb, int x, int y, int w, int h)
{
    lv_display_t *d = lv_display_get_default();
    if (g_single && d && lv_display_is_double_buffered(d)) {
        lv_display_set_buffers(d, fb, NULL, 960 * 540 * 2, LV_DISPLAY_RENDER_MODE_DIRECT);
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(d);
    }
    g_fb = fb;
    g_x = x; g_y = y; g_w = w; g_h = h;
    g_states.clear();
    g_visits = 0;
    g_areas = 0;
    g_old = grab();
    g_armed = true;
}

// The state of a run that is the most unlike the picture it settled on (most pixels differing from `now`), for a PNG: what the person saw at the worst moment.
static Pic g_worst;
static int g_worst_dev = -1, g_wx, g_wy, g_ww, g_wh;
static void run_reset(void) { g_worst.clear(); g_worst_dev = -1; }

// Pixel colour of the panel itself: the rectangle's pixel inset by `inset` from the top right corner (inside the border, away from the texts).
static int end(int *states, int *visits, int inset = 8, int *areas = NULL)
{
    g_armed = false;
    Pic now = grab();
    uint16_t panel = now[(size_t)inset * g_w + (g_w - 1 - inset)];
    int bad = 0;
    for (const Pic &s : g_states) {
        int dev = 0;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] != now[i]) {
                dev++;
            }
            if (s[i] != g_old[i] && s[i] != now[i] && s[i] != panel) {
                if (bad == 0 && getenv("OVP_DEBUG")) {
                    printf("ovp: first bad pixel at (%d,%d) state %d: %04x old %04x new %04x panel %04x\n", g_x + (int)(i % g_w), g_y + (int)(i / g_w), (int)(&s - &g_states[0]), s[i], g_old[i], now[i], panel);
                }
                bad++;
            }
        }
        if (dev > g_worst_dev) {
            g_worst_dev = dev;
            g_worst = s;
            g_wx = g_x; g_wy = g_y; g_ww = g_w; g_wh = g_h;
        }
    }
    if (states) *states = (int)g_states.size();
    if (visits) *visits = g_visits;
    if (areas) *areas = g_areas;
    return bad;
}

// Writes the worst state of the run into the frame buffer (for a PNG) and returns what was there; paste_back() puts it right again.
static Pic paste_worst(uint16_t *fb)
{
    Pic saved;
    if (g_worst_dev < 0) {
        return saved;
    }
    saved.resize((size_t)g_ww * g_wh);
    for (int r = 0; r < g_wh; r++) {
        memcpy(&saved[(size_t)r * g_ww], fb + (size_t)(g_wy + r) * 960 + g_wx, (size_t)g_ww * 2);
        memcpy(fb + (size_t)(g_wy + r) * 960 + g_wx, &g_worst[(size_t)r * g_ww], (size_t)g_ww * 2);
    }
    return saved;
}

static void paste_back(uint16_t *fb, const Pic &saved)
{
    for (int r = 0; r < g_wh && !saved.empty(); r++) {
        memcpy(fb + (size_t)(g_wy + r) * 960 + g_wx, &saved[(size_t)r * g_ww], (size_t)g_ww * 2);
    }
}

} // namespace ovp
