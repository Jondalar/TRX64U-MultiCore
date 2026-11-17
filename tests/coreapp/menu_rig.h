// The core application's menu on a native machine, shared by the render bench (tests/coreapp/render) and the menu simulator (tools/menusim,
// T236): the pixel layer as LVGL's native display, the PNG writers, the bench's CHECK, the descriptor chain the board's menu runs on (Built)
// and the rig of fakes a MenuCtl runs against (Rig). Included once per program, after the application's headers and the core table.
#ifndef TESTS_COREAPP_MENU_RIG_H
#define TESTS_COREAPP_MENU_RIG_H

// ---- the pixel layer, native ----------------------------------------------------------------------
static uint16_t fbs[2][960 * 540];
static uint16_t *fb = fbs[0];
static int cvw = 960, cvh = 540;                               // T164 D10a: the canvas of the display (the port's trx_lv_set_canvas)
// T236: the bench's clock steps 50 ms per call, so its pictures do not depend on the machine; the simulator sets the wall clock. It also
// sets rig_on_frame, which is called with every finished frame (fb holds it, cvw x cvh); the bench leaves it empty.
static uint32_t rig_tick_steps(void) { static uint32_t t; return t += 50; }
static uint32_t (*rig_tick)(void) = rig_tick_steps;
static void (*rig_on_frame)(void) = nullptr;
static uint32_t tick_cb(void) { return rig_tick(); }

bool trx_lv_open(void)
{
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(cvw, cvh);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, fbs[0], fbs[1], sizeof(fbs[0]), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, [](lv_display_t *dd, const lv_area_t *, uint8_t *px) {
        if (lv_display_flush_is_last(dd)) { fb = (uint16_t *)px; }
        lv_display_flush_ready(dd);
        if (lv_display_flush_is_last(dd) && rig_on_frame) { rig_on_frame(); }
    });
    ovp::install();
    return true;
}
int trx_lv_handler(void) { return (int)lv_timer_handler(); }
void trx_lv_set_canvas(int w, int h)
{
    if (w == cvw && h == cvh) { return; }
    cvw = w; cvh = h;
    lv_display_t *d = lv_display_get_default();
    if (d) {
        lv_display_set_resolution(d, w, h);
        lv_display_set_buffers(d, fbs[0], fbs[1], sizeof(fbs[0]), LV_DISPLAY_RENDER_MODE_DIRECT);
    }
}
void trx_lv_suspend(void) { }
void trx_lv_resume(void) { }
void trx_lv_refresh_now(void) { lv_timer_handler(); }
void trx_lv_yield_layer(void) { }
void trx_lv_set_key(bool, uint16_t) { }
bool trx_lv_set_alpha(uint8_t) { return true; }

// ---- PNG -------------------------------------------------------------------------------------------
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

// T107 5.9: what the monitor gets. The menu canvas (fb, 960 x 540) scaled by nearest sampling into the box the video service gave it (the layer does
// the same with PX_OSIZE), on a black frame of the mode's size: rgb holds W x H pixels of three bytes. The simulator's window shows the same frame.
static void rig_compose(unsigned char *rgb, int W, int H, int bx, int by, int bw, int bh)
{
    memset(rgb, 0, (size_t)W * H * 3);
    for (int y = by < 0 ? 0 : by; y < by + bh && y < H; y++) {
        for (int x = bx < 0 ? 0 : bx; x < bx + bw && x < W; x++) {
            uint16_t v = fb[((y - by) * cvh / bh) * cvw + (x - bx) * cvw / bw];
            unsigned r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
            unsigned char *q = rgb + ((size_t)W * y + x) * 3;
            q[0] = (r << 3) | (r >> 2); q[1] = (g << 2) | (g >> 4); q[2] = (b << 3) | (b >> 2);
        }
    }
}

// W x H pixels of three bytes into a PNG.
static void png_rgb(const char *path, const unsigned char *rgb, int W, int H)
{
    unsigned char *raw = (unsigned char *)malloc((size_t)(W * 3 + 1) * H);
    for (int y = 0; y < H; y++) {
        raw[(size_t)(W * 3 + 1) * y] = 0;
        memcpy(raw + (size_t)(W * 3 + 1) * y + 1, rgb + (size_t)W * 3 * y, (size_t)W * 3);
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
}

static void png_out(const char *path, int W, int H, int bx, int by, int bw, int bh)
{
    unsigned char *rgb = (unsigned char *)malloc((size_t)W * H * 3);
    rig_compose(rgb, W, H, bx, by, bw, bh);
    png_rgb(path, rgb, W, H);
    free(rgb);
    printf("%s\n", path);
}

// ---- checks ----------------------------------------------------------------------------------------
static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// T187: the descriptor the board's menu runs on - the core's own plus Picture, Volume, Persist, Home and the hotkeys, built by the one function run.cc calls.
// Every bench that shows "what the board shows" goes through here; none builds its own chain. Lives for the whole run (the Settings keep pointers into it).
struct Built {
    CoreDesc d; Setting s[SETTINGS_MAX];
    explicit Built(const CoreDesc *src, const PictureEnv *env = 0)
    {
        PictureEnv e = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };
        CHECK(!menu_build(src, &d, s, env ? env : &e));
    }
};
static CoreDesc *board_desc(const CoreDesc *src, const PictureEnv *env = 0) { return &(new Built(src, env))->d; }

// ---- the rig ---------------------------------------------------------------------------------------
struct Host : MenuHost {
    int returns;
    Host() : returns(0), media_gen(0) { }
    int media_gen;                                                   // the bench "mounts a stick": the media chip's text changes
    void info(int which, char *out, int n) { if (which == 3) snprintf(out, n, "USB %d", media_gen); else snprintf(out, n, "line %d", which); }
    bool return_to_host(char *msg, int n) { returns++; snprintf(msg, n, "this host has no ICAP"); return false; }
};

struct Rig {
    FakeIo io; FakeFs fs; PxRec px; Host host; CoreCtx c; Settings *st; Video *vid; MenuCtl *m;
    Rig(const CoreDesc *d, const char *cfg, const MenuMerge *mm = 0)
    {
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs; c.merge = mm;
        px.on_canvas = ui_set_canvas;                   // the video service's canvas reaches the real view, as on the board
        st = new Settings(d->settings, d->nsettings, d->store, &fs, cfg, 0);
        c.settings = st;
        vid = new Video(d->video, &px, &io);
        c.video = vid;
        st->load();
        m = new MenuCtl(&c, &host);
    }
};

#endif
