// The built-in monitor's screen as the export "textgrid" (textgrid.h): what screens.cc drew through mon_render and the accessors, and menuflows.cc started through the operations
// monitor.open and monitor.tick, now reached through the table the layout's `grid` parameter names. The session calls are made on the menu task like the operations' own bodies.
#include "libexports.h"
#include "textgrid.h"
#include "monitor.h"

static CoreCtx *g_c;

static void grid_render(void) { mon_render(); }
static int grid_cols(void) { return mon_cols(); }
static int grid_rows(void) { return mon_rows(); }
static const char *grid_line(int r) { return r >= 0 && r < mon_rows() ? mon_line(r) : ""; }
static int grid_attr(int r) { return r >= 0 && r < mon_rows() ? mon_attr(r) : TG_NORMAL; }
static int grid_blank_rows(void) { return mon_blank_rows(); }
static void grid_cursor(int *x, int *y) { mon_cursor(x, y); }
static uint32_t grid_sig(void) { return mon_sig(); }
static void grid_enter(void) { mon_open(g_c); }
static bool grid_tick(void) { return mon_tick(g_c); }

static const TextGrid g_grid = { sizeof(TextGrid), grid_render, grid_cols, grid_rows, grid_line, grid_attr, grid_blank_rows, grid_cursor, grid_sig, grid_enter, grid_tick };

const TextGrid *monitor_textgrid(CoreCtx *c)
{
    g_c = c;
    return &g_grid;
}
