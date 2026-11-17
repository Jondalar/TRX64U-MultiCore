// The menu simulator for the host application (T236 D12): the real host menu - hostui.cc, the host core, LVGL - on the Mac, in a window, driven by
// the keyboard. The board is the host render bench's stubbed firmware (tests/hostapp/host_rig.h) over the tree tests/hostapp/render/mktree.py makes
// (Flash, Usb0, SD with real .cor bundles); boot.cfg is the tree's Flash/boot.cfg. The picture is the output frame: the mode's active area, black,
// with the menu canvas at the place the host gives it (trx_lv_set_place), scaled by nearest sampling as the pixel layer does.
//
//   tools/menusim/run.sh --host [--mode M] [--scale N]                     the window
//   tools/menusim/run.sh --host --keys DOWN,RETURN --shot out.png [...]    headless: the keys, one PNG, end
#include "host_rig.h"                       // the tree's tests/hostapp (run.sh puts it on the include path)
#include "keyboard.h"
#include "simwin.h"

extern void hostui_test_init(const char *cfgtext);
extern uint32_t hostui_test_key(int k);
extern void hostui_test_pump(void);
extern void hostui_test_mode(int sys, int hdmi);
extern int hostui_test_dump(char *out, int max);

struct Opts {
    const char *tree = 0, *mode = "1080p", *keys = 0, *shot = 0;      // 1080p: the mode stored as the user's choice, as the core simulator's default
    int scale = 0;
};
static Opts o;

static void usage(void)
{
    fprintf(stderr, "hostsim --tree TREE [--mode 1080p|1080p60|720p|720p60|576p|480p] [--scale N] [--keys K1,K2,... --shot out.png]\n"
                    "        (no PC mode: the host offers those only to a screen whose EDID names them, and this screen has none)\n");
    exit(2);
}

struct Mode { const char *name; int hdmi, sys; };
static const Mode modes[] = {
    { "1080p", VID_HDMI_1080P, VID_SYS_PAL50 }, { "1080p60", VID_HDMI_1080P, VID_SYS_NTSC60 },
    { "720p", VID_HDMI_720P, VID_SYS_PAL50 }, { "720p60", VID_HDMI_720P, VID_SYS_NTSC60 },
    { "576p", VID_HDMI_480_576, VID_SYS_PAL50 }, { "480p", VID_HDMI_480_576, VID_SYS_NTSC60 },
};   // the PC modes are offered by the host only to a screen whose EDID names them (Q9); the simulator's screen has no EDID
static int win_w, win_h;                                      // the window's frame, fixed when it opens (the host may change its mode later)

// The frame the host puts out: its mode (what it applied through video_apply / video_start), else 1080p.
static void frame_size(int *w, int *h)
{
    int hdmi = g_vid_hdmi >= 0 ? g_vid_hdmi : VID_HDMI_1080P, sys = g_vid_sys >= 0 ? g_vid_sys : VID_SYS_PAL50;
    video_active_size(hdmi, sys, w, h);
    if (*w <= 0 || *h <= 0) { *w = 1920; *h = 1080; }
}

// Where the canvas is in the frame: the host's place, else the canvas at the factor that fills 1080p.
static void place(int *x, int *y, int *w, int *h)
{
    if (g_pl_w > 0 && g_pl_h > 0) { *x = g_pl_x; *y = g_pl_y; *w = g_pl_w; *h = g_pl_h; return; }
    *x = 0; *y = 0; *w = cvw * (1920 / cvw); *h = cvh * (1080 / cvh);
}

static void compose_to(unsigned char *rgb, int frame_w, int frame_h)
{
    int bx, by, bw, bh;
    place(&bx, &by, &bw, &bh);
    memset(rgb, 0, (size_t)frame_w * frame_h * 3);
    for (int y = by < 0 ? 0 : by; y < by + bh && y < frame_h; y++) {
        for (int x = bx < 0 ? 0 : bx; x < bx + bw && x < frame_w; x++) {
            uint16_t v = fb[((y - by) * cvh / bh) * cvw + (x - bx) * cvw / bw];
            unsigned r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
            unsigned char *q = rgb + ((size_t)frame_w * y + x) * 3;
            q[0] = (r << 3) | (r >> 2); q[1] = (g << 2) | (g >> 4); q[2] = (b << 3) | (b >> 2);
        }
    }
}

static void compose(unsigned char *rgb) { compose_to(rgb, win_w, win_h); }

static void shot(const char *path)
{
    hostui_test_pump();
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    int frame_w, frame_h;
    frame_size(&frame_w, &frame_h);
    std::vector<unsigned char> rgb((size_t)frame_w * frame_h * 3);
    compose_to(rgb.data(), frame_w, frame_h);
    unsigned char *raw = (unsigned char *)malloc((size_t)(frame_w * 3 + 1) * frame_h);
    for (int y = 0; y < frame_h; y++) {
        raw[(size_t)(frame_w * 3 + 1) * y] = 0;
        memcpy(raw + (size_t)(frame_w * 3 + 1) * y + 1, rgb.data() + (size_t)frame_w * 3 * y, (size_t)frame_w * 3);
    }
    uLongf cl = compressBound((frame_w * 3 + 1) * frame_h);
    unsigned char *z = (unsigned char *)malloc(cl);
    compress(z, &cl, raw, (frame_w * 3 + 1) * frame_h);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "hostsim: cannot write %s\n", path); free(raw); free(z); return; }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    unsigned char ih[13];
    put_be(ih, frame_w); put_be(ih + 4, frame_h); ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
    chunk(f, "IHDR", ih, 13); chunk(f, "IDAT", z, cl); chunk(f, "IEND", 0, 0);
    fclose(f);
    free(raw); free(z);
    printf("%s\n", path);
}

static void give_key(int k)
{
    hostui_test_key(k);
    hostui_test_pump();
    static char d[16384];
    hostui_test_dump(d, sizeof(d));                             // its first line: the view, the selection, the page's texts
    char *nl = strchr(d, '\n');
    if (nl) { *nl = 0; }
    printf("key %s -> %.200s\n", sim_key_name(k), d);
}

static void win_idle(void) { hostui_test_pump(); lv_timer_handler(); }

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IOLBF, 0);
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> const char * { if (i + 1 >= argc) usage(); return argv[++i]; };
        if (a == "--tree") { o.tree = val(); }
        else if (a == "--mode") { o.mode = val(); }
        else if (a == "--keys") { o.keys = val(); }
        else if (a == "--shot") { o.shot = val(); }
        else if (a == "--scale") { o.scale = atoi(val()); }
        else { usage(); }
    }
    if (!o.tree) { usage(); }
    snprintf(g_root, sizeof(g_root), "%s", o.tree);
    if (!o.keys && !o.shot) { rig_tick = sim_wall_tick; }             // the window runs on the wall clock; headless keeps the bench's steps
    rig_on_frame = sim_on_frame;
    const Mode *mode = 0;
    for (auto &m : modes) { if (o.mode && !strcasecmp(m.name, o.mode)) { mode = &m; } }
    if (!mode) { usage(); }

    std::string cfg;
    {
        std::string p = std::string(o.tree) + "/Flash/boot.cfg";
        FILE *f = fopen(p.c_str(), "r");
        if (!f) { fprintf(stderr, "hostsim: no %s\n", p.c_str()); return 2; }
        char b[512];
        while (fgets(b, sizeof(b), f)) { cfg += b; }
        fclose(f);
    }
    // the mode the user chose before (host.cfg) and the picture the direct phase put up in it, as the bench's snap_save_scenario starts
    g_saved_hdmi = mode->hdmi; g_saved_sys = mode->sys;
    hostui_test_video(mode->sys, mode->hdmi);
    hostui_test_mode(mode->sys, mode->hdmi);
    hostui_test_init(cfg.c_str());
    hostui_test_pump();
    frame_size(&win_w, &win_h);
    {
        int x, y, w, h;
        place(&x, &y, &w, &h);
        printf("hostsim: tree %s, mode %s, canvas %dx%d at %d,%d shown %dx%d (frame %dx%d)\n", o.tree, video_hdmi_name(g_vid_hdmi), cvw, cvh, x, y, w, h,
               win_w, win_h);
    }
    if (o.keys || o.shot) {
        for (const std::string &k : sim_split_keys(o.keys)) {
            int code = sim_key_of(k.c_str());
            if (code < 0) { fprintf(stderr, "hostsim: unknown key %s\n", k.c_str()); return 2; }
            give_key(code);
        }
        if (o.shot) { shot(o.shot); }
        return 0;
    }
    char title[160];
    snprintf(title, sizeof(title), "menusim - host app - %s %dx%d", video_hdmi_name(g_vid_hdmi), win_w, win_h);
    SimApp a = { title, win_w, win_h, give_key, win_idle, compose, shot, 0, "menusim-host" };
    return sim_window(a, o.scale);
}
