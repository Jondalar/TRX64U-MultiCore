/*
 * cart_screen.c - the screen the dumper and the writer share, as what they publish (see cart_screen.h, trex_ui.h).
 *
 * The 40 x 24 frame of the old tools is gone (T104.3 D9.1: new screens, no terminal version). Their calls are kept so that the tools stay what they were: the
 * header and the slot line, the file, the status, the bank maps. Each now sets a field of g_ui, and the description of the screens (app.toml) says where it
 * is shown.
 */

#include "cart_screen.h"
#include "cart_base.h"
#include "trex_ui.h"

trex_ui_t g_ui;

void ui_set(char *dst, int cap, const char *fmt, ...)
{
    static char tmp[1600];                              /* the grid is the longest text; one writer at a time (the application's task) */
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    u2p_vsnprintf(tmp, sizeof(tmp), fmt, ap);
    __builtin_va_end(ap);
    int n = (int)strlen(tmp);
    if (n > cap - 1) {
        n = cap - 1;
    }
    memcpy(dst, tmp, (size_t)n);                       /* the old text is overwritten in one go and ends where the new one does */
    dst[n] = 0;
}

void ui_show(const char *id)
{
    g_ui.screen_seq++;
    UI_SET(screen, "%s#%d", id, g_ui.screen_seq);
}

void ui_lock(const char *reason)
{
    UI_SET(lock, "%s", reason ? reason : "");
}

bool ui_cancelled(void)
{
    return g_api->job && g_api->job->stop_requested && g_api->job->stop_requested();
}

void ui_op_begin(const char *title, const char *phase, int total)
{
    UI_SET(title, "%s", title);
    UI_SET(phase, "%s", phase);
    g_ui.op_total = total > 0 ? total : 1;
    g_ui.op_done = 0;
    UI_SET(pct, "0");
    UI_SET(count, "0 / %d", g_ui.op_total);
}

void ui_op_step(void)
{
    if (g_ui.op_done < g_ui.op_total) {
        g_ui.op_done++;
    }
    UI_SET(pct, "%d", g_ui.op_done * 100 / g_ui.op_total);
    UI_SET(count, "%d / %d", g_ui.op_done, g_ui.op_total);
}

void ui_op_phase(const char *phase)
{
    UI_SET(phase, "%s", phase);
}

void ui_op_end(void)
{
    g_ui.op_total = 0;
    g_ui.op_done = 0;
}

Window *cart_screen_open(void)
{
    return window_open();
}

/* The positions are only names for the parts: which one a call means is what the screens bind to. */
void cart_layout_compute(cart_layout_t *l, int banks, bool has_romh, bool ef3)
{
    int rows = (banks + 15) / 16;
    l->banks = banks;
    l->rows = rows < 1 ? 1 : rows;
    l->has_romh = has_romh;
    l->y_header = 0;
    l->y_slot = ef3 ? 1 : -1;
    l->y_file = 2;
    l->y_roml = 4;
    l->y_romh = has_romh ? 5 : -1;
    l->y_status = 3;
    l->y_keys = 6;
    l->y_keys2 = ef3 ? 7 : -1;
    g_ui.banks_lo = banks;
    g_ui.banks_hi = has_romh ? banks : 0;
    g_ui.has_romh = has_romh;
    UI_SET(is_ef3, "%s", ef3 ? "1" : "");
    UI_SET(not_ef3, "%s", ef3 ? "" : "1");
}

void cart_screen_line(Window *w, int y, int color, const char *text)
{
    (void)w;
    (void)color;
    if (y == 0) {
        LOG("[TrexCrt] %s\n", text);
    } else if (y == 1) {
        UI_SET(slot, "%s", text);
    }
}

void cart_screen_status(Window *w, const cart_layout_t *l, const char *msg)
{
    (void)w;
    (void)l;
    UI_SET(status, "%s", msg);
}

void cart_screen_file(Window *w, const cart_layout_t *l, const char *name)
{
    (void)w;
    (void)l;
    UI_SET(file, "%s", (name && name[0]) ? name : "");
}

void cart_screen_keys(Window *w, int y, const char *const *pairs, int npairs)
{
    (void)w;
    (void)y;
    (void)pairs;
    (void)npairs;
}

static void detail(char *dst, int cap, const uint8_t *st, int n)
{
    int busy = -1, data = 0;
    for (int i = 0; i < n; i++) {
        if ((st[i] == BANK_WRITING || st[i] == BANK_ERASING) && busy < 0) {
            busy = i;
        }
        if (st[i] == BANK_FILLED || st[i] == BANK_VERIFIED) {
            data++;
        }
    }
    if (busy >= 0) {
        ui_set(dst, cap, "BANK $%02X", busy);
    } else {
        ui_set(dst, cap, "%d / %d DATA", data, n);
    }
}

void cart_draw_bank_map(Window *w, const cart_layout_t *l, const uint8_t *status_lo, const uint8_t *status_hi)
{
    (void)w;
    if (!l) {
        return;
    }
    int n = l->banks > UI_MAP_MAX ? UI_MAP_MAX : l->banks;
    for (int i = 0; i < n; i++) {
        g_ui.map_lo[i] = (char)('0' + status_lo[i]);
    }
    g_ui.map_lo[n] = 0;
    detail(g_ui.map_lo_d, sizeof(g_ui.map_lo_d), status_lo, n);
    if (l->has_romh && status_hi) {
        for (int i = 0; i < n; i++) {
            g_ui.map_hi[i] = (char)('0' + status_hi[i]);
        }
        g_ui.map_hi[n] = 0;
        detail(g_ui.map_hi_d, sizeof(g_ui.map_hi_d), status_hi, n);
    } else {
        g_ui.map_hi[0] = 0;
        g_ui.map_hi_d[0] = 0;
    }
}
