// A model of a library's text screen (coreapp/src/textgrid.h): the rows the "machine" holds are set by the test, render() builds them, sig is a hash of what was built, and every call is counted.
// mg_install() makes it the export found under "textgrid" and "model" (menuflows.h: the finder the application gives the menu). The counts show what the generic path does: enter once per
// entry of the node, tick on every pass while it is shown, line() only on a redraw.
#ifndef TESTS_COREAPP_MODEL_GRID_H
#define TESTS_COREAPP_MODEL_GRID_H

#include <string>
#include <vector>
#include <string.h>
#include "textgrid.h"
#include "menuflows.h"

struct ModelGrid {
    std::vector<std::string> src, rows;     // what the machine holds, what the last render built
    std::vector<int> attrs;
    int cx = 0, cy = 0;
    uint32_t sig = 0;
    int n_enter = 0, n_tick = 0, n_render = 0, n_line = 0;
    bool tick_answer = false;               // what the next tick answers (read once)
    void (*on_enter)(void) = 0;
    void reset() { *this = ModelGrid(); }
};
static ModelGrid g_mg;

static void mg_render(void)
{
    g_mg.n_render++;
    g_mg.rows = g_mg.src;
    uint32_t h = 2166136261u + (uint32_t)g_mg.cx * 977u + (uint32_t)g_mg.cy;
    for (size_t i = 0; i < g_mg.rows.size(); i++) {
        for (const char *p = g_mg.rows[i].c_str(); *p; p++) {
            h = (h ^ (uint8_t)*p) * 16777619u;
        }
        h = (h ^ 0x0A) * 16777619u;
    }
    g_mg.sig = h;
}
static int mg_cols(void) { return 80; }
static int mg_rows(void) { return (int)g_mg.rows.size(); }
static const char *mg_line(int r) { g_mg.n_line++; return r >= 0 && r < (int)g_mg.rows.size() ? g_mg.rows[r].c_str() : ""; }
static int mg_attr(int r) { return r >= 0 && r < (int)g_mg.attrs.size() ? g_mg.attrs[r] : TG_NORMAL; }
static int mg_blank(void) { return 0; }
static void mg_cursor(int *x, int *y) { *x = g_mg.cx; *y = g_mg.cy; }
static uint32_t mg_sig(void) { return g_mg.sig; }
static void mg_enter(void) { g_mg.n_enter++; if (g_mg.on_enter) { g_mg.on_enter(); } }
static bool mg_tick(void) { g_mg.n_tick++; bool a = g_mg.tick_answer; g_mg.tick_answer = false; return a; }
static const TextGrid g_mg_table = { sizeof(TextGrid), mg_render, mg_cols, mg_rows, mg_line, mg_attr, mg_blank, mg_cursor, mg_sig, mg_enter, mg_tick };

static const void *mg_find(const char *name, uint32_t min_version, uint32_t *size)
{
    if ((strcmp(name, "textgrid") && strcmp(name, "model")) || (min_version >> 16) > 1) {
        return 0;
    }
    *size = sizeof(g_mg_table);
    return &g_mg_table;
}
static void mg_install(void) { g_mg.reset(); menu_set_export_finder(mg_find); }
static void mg_uninstall(void) { menu_set_export_finder(0); }

#endif
