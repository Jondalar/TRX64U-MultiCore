// Renders the shared view layer natively (LVGL, no firmware) to PNG, upscaled 2x like the pixel layer's
// scale 2, and checks the geometry against the prototype's consts (T104 5.8).
//   tests/view/run.sh <outdir>
#include "lvgl.h"
#include "trxview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint16_t fbs[2][960 * 540];
static uint16_t *fb = fbs[0];
static uint32_t tick_cb(void) { static uint32_t t; return t += 50; }

static void lv_up(void)
{
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(960, 540);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, fbs[0], fbs[1], sizeof(fbs[0]), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, [](lv_display_t *dd, const lv_area_t *, uint8_t *px) {
        if (lv_display_flush_is_last(dd)) { fb = (uint16_t *)px; }
        lv_display_flush_ready(dd);
    });
}

static void put_be(unsigned char *p, unsigned v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void chunk(FILE *f, const char *t, const unsigned char *d, unsigned n)
{
    unsigned char h[4];
    put_be(h, n);
    fwrite(h, 1, 4, f);
    uLong c = crc32(0, (const Bytef *)t, 4);
    fwrite(t, 1, 4, f);
    if (n) { fwrite(d, 1, n, f); c = crc32(c, d, n); }
    put_be(h, (unsigned)c);
    fwrite(h, 1, 4, f);
}

static void png(const char *path, int scale)
{
    int W = 960 * scale, H = 540 * scale;
    unsigned char *raw = (unsigned char *)malloc((size_t)(W * 3 + 1) * H), *p = raw;
    for (int y = 0; y < H; y++) {
        *p++ = 0;
        for (int x = 0; x < W; x++) {
            uint16_t v = fb[(y / scale) * 960 + x / scale];
            unsigned r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
            *p++ = (r << 3) | (r >> 2); *p++ = (g << 2) | (g >> 4); *p++ = (b << 3) | (b >> 2);
        }
    }
    uLongf cl = compressBound((W * 3 + 1) * H);
    unsigned char *z = (unsigned char *)malloc(cl);
    compress(z, &cl, raw, (W * 3 + 1) * H);
    FILE *f = fopen(path, "wb");
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    unsigned char ih[13];
    put_be(ih, W); put_be(ih + 4, H); ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
    chunk(f, "IHDR", ih, 13); chunk(f, "IDAT", z, cl); chunk(f, "IEND", 0, 0);
    fclose(f);
    free(raw); free(z);
    printf("%s\n", path);
}

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// The VC-20's route of T104 5.9, as the descriptor will carry it.
static const TrxTab tabs_home[]    = { { 1, "HOME", 0, 0 } };
static const TrxTab tabs_library[] = { { 1, "BROWSE", 1, 0 } };
static const TrxTab tabs_machine[] = { { 1, "SUMMARY", 2, 0 }, { 2, "MEMORY", 3, "memory" }, { 3, "DRIVES", 4, 0 } };
static const TrxTab tabs_actions[] = { { 1, "QUICK", 5, 0 } };
static const TrxTab tabs_set[]     = { { 1, "VIDEO", 6, 0 }, { 2, "AUDIO", 6, 0 }, { 3, "INPUT", 6, 0 }, { 4, "NETWORK", 6, 0 } };
static const TrxTab tabs_system[]  = { { 1, "STATUS", 7, 0 } };

static const TrxArea areas[] = {
    { "F1", "HOME",     tabs_home,    1 },
    { "F3", "LIBRARY",  tabs_library, 1 },
    { "F5", "VC-20",    tabs_machine, 3 },
    { "F7", "ACTIONS",  tabs_actions, 1 },
    { "F6", "SETTINGS", tabs_set,     4 },
    { "F8", "SYSTEM",   tabs_system,  1 },
};
static const TrxRoute route = { areas, 6 };

static TrxShellInfo info(void)
{
    TrxShellInfo i;
    memset(&i, 0, sizeof(i));
    i.product = "TRX64";
    i.section = "/ UI";
    i.activity = "IDLE";
    i.chips[0].label = "VC-20"; i.chips[0].tone = 0;
    i.chips[1].label = "DRIVE A"; i.chips[1].tone = 0;
    i.nchips = 2;
    i.footer_left = "R/S  CLOSE";
    i.footer_right = "F1-F8 AREA   1-5 TAB   RETURN SELECT   R/S CLOSE";
    return i;
}

static void draw_rows(lv_obj_t *c, int sel)
{
    const char *names[] = { "Memory Expansion", "Block 5 ($A000), when expanded", "Interface Type", "Colour Scheme" };
    const char *vals[]  = { "+24K", "RAM", "Freeze", "Violet" };
    int y = trx_theme().space_md;
    y = trx_kicker(c, trx_theme().space_lg, y, "MEMORY");
    for (int i = 0; i < 4; i++) {
        TrxRow r;
        memset(&r, 0, sizeof(r));
        r.label = names[i];
        r.value = vals[i];
        r.selected = (i == sel);
        r.strong = (i == sel);
        y = trx_row(c, trx_theme().space_lg, y, trx_content_w() - 2 * trx_theme().space_lg, r);
    }
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "/tmp/view-render";
    lv_up();
    char p[512];

    for (int skin = 0; skin < 2; skin++) {
        trx_skin_set(skin ? TRX_SKIN_TERMINAL : TRX_SKIN_PANEL);
        const TrxTheme &t = trx_theme();
        CHECK(t.sidebar_w == 148 && t.topbar_h == 36 && t.tabbar_h == 38 && t.footer_h == 28);

        TrxShellInfo i = info();
        lv_obj_t *c = trx_shell(lv_screen_active(), i, route, 2, 1);
        CHECK(trx_content_w() == 960 - 148);
        CHECK(trx_content_h() == 540 - 36 - 28 - 38);
        draw_rows(c, 1);
        lv_timer_handler();
        snprintf(p, sizeof(p), "%s/%s-machine-memory.png", out, skin ? "terminal" : "panel");
        png(p, 2);

        // an area with one tab: the strip is still there, the content starts below it
        i.section = "/ BOOT";
        c = trx_shell(lv_screen_active(), i, route, 0, 0);
        CHECK(trx_content_h() == 540 - 36 - 28 - 38);
        draw_rows(c, 0);
        lv_timer_handler();
        snprintf(p, sizeof(p), "%s/%s-home.png", out, skin ? "terminal" : "panel");
        png(p, 2);
    }

    // the frame is kept while only the content changes; a tab, an area or a word of the top bar redraws only its part, and the frame is
    // built again only when what never changes there did (T104 5.10, 5.11)
    trx_skin_set(TRX_SKIN_PANEL);
    TrxShellInfo fi = info();
    trx_shell(lv_screen_active(), fi, route, 2, 1);
    uint32_t f0 = trx_shell_frames(), q0 = trx_shell_partials();
    for (int k = 0; k < 10; k++) {
        lv_obj_t *cc = trx_shell(lv_screen_active(), fi, route, 2, 1);
        draw_rows(cc, k % 4);
    }
    CHECK(trx_shell_frames() == f0 && trx_shell_partials() == q0);   // ten redraws of the list: nothing of the frame
    trx_shell(lv_screen_active(), fi, route, 2, 2);
    CHECK(trx_shell_frames() == f0 && trx_shell_partials() == q0 + 1);   // another tab: two tabs of the strip
    trx_shell(lv_screen_active(), fi, route, 0, 0);
    CHECK(trx_shell_frames() == f0 && trx_shell_partials() == q0 + 2);   // another area: two rail items and the strip
    fi.activity = "WAITING";
    trx_shell(lv_screen_active(), fi, route, 0, 0);
    CHECK(trx_shell_frames() == f0 && trx_shell_partials() == q0 + 3);   // a word in the top bar changed: the bar
    fi.product = "OTHER";
    trx_shell(lv_screen_active(), fi, route, 0, 0);
    CHECK(trx_shell_frames() == f0 + 1);                  // what never changes did: the frame is built again
    trx_shell_forget();
    trx_shell(lv_screen_active(), fi, route, 0, 0);
    CHECK(trx_shell_frames() == f0 + 2);                  // the caller cleared the screen behind us
    // the partly redrawn frame is the one built from scratch: pixel for pixel
    {
        static uint16_t kept[960 * 540];
        trx_shell(lv_screen_active(), fi, route, 2, 1);
        trx_shell(lv_screen_active(), fi, route, 2, 2);
        trx_shell(lv_screen_active(), fi, route, 0, 0);
        lv_timer_handler();
        memcpy(kept, fb, sizeof(kept));
        trx_shell_forget();
        trx_shell(lv_screen_active(), fi, route, 0, 0);
        lv_timer_handler();
        CHECK(memcmp(kept, fb, sizeof(kept)) == 0);
    }

    // the window: the selection is always inside it, and the last page is full
    TrxWindow w = trx_window(14, 0, 0, 9);
    CHECK(w.first == 0 && w.count == 9 && w.total == 14);
    w = trx_window(14, 11, 0, 9);
    CHECK(w.first == 5 && w.count == 9);                  // T209 D3: two rows stay below the selection (11 + 2 = the window's last row 13)
    w = trx_window(14, 13, 3, 9);
    CHECK(w.first == 5 && w.count == 9);
    w = trx_window(4, 0, 0, 9);
    CHECK(w.first == 0 && w.count == 4);
    w = trx_window(0, 0, 0, 9);
    CHECK(w.first == 0 && w.count == 0);

    printf("view render: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
