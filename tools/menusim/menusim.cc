// The menu simulator (T236): the core application's real menu - ui.cc, MenuCtl, LVGL, the core's descriptor and menu record - on the Mac, in a
// window, driven by the keyboard. The board is the render bench's rig of fakes (tests/coreapp/menu_rig.h, board_stubs.cc); the picture is the
// output frame the MultiCore host puts on HDMI: the mode's active area, black, with the menu canvas at the place the application's own video
// service gives it (Video's video_place_menu, PxPort::menu_place), scaled by nearest sampling as the pixel layer does.
//
//   tools/menusim/run.sh <core> [--mode M] [--toml FILE] [--ops FILE] [--stick FILE] [--scale N]    window
//   tools/menusim/run.sh <core> --keys F3,DOWN,RETURN --shot out.png [--mode M] ...                  headless: the keys, one PNG, end
//                                       (in the list FIND:<text> moves the selection to the first row whose label starts with the text, SEL:<n> to row n)
//
// Keys in the window: cursor keys, RETURN, ESC, BACKSPACE, TAB, HOME, END, PAGE UP/DOWN, DEL, SPACE, letters and digits, F1-F8 (shift+F1 = F2 as
// on a C64 keyboard), F12 opens the menu after a close. Cmd (Ctrl elsewhere) + R reloads the menu TOML, + S writes a screenshot, + Q quits.
// The menu is built as run.cc builds it (menu_build, then bundle_merge with the description record). A core with a menu TOML
// (coreapp/cores/<core>/menu.toml, or --toml) has it compiled by the tree's tools/descrec.py at start and again when the file changes (polled every
// 250 ms); the keys pressed so far are played again on the new menu, so the page stays.
#include "overlay_probe.h"
#include "hostos.h"
#include "lvgl.h"
#include "src/display/lv_display_private.h"
#include "trx_lv_port.h"
#include "trxcanvas.h"
#include "ui.h"
#include "menuctl.h"
#include "settings.h"
#include "video.h"
#include "fakes.h"
#include "keyboard.h"
#include "picture.h"
#include "menubuild.h"
#include "cores.h"
#include "descrec.h"
#include "contrib.h"
#include "bundle.h"
#include "videomode.h"
#include "edid.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#include "table.cc"                                           // the board's own core table, as the render bench takes it
#include "menu_rig.h"

// ---- options ---------------------------------------------------------------------------------------
struct Opts {
    const char *core = 0, *mode = "1080p", *keys = 0, *shot = 0, *toml = 0, *ops = 0, *stick = 0, *root = ".";
    int scale = 0;
};
static Opts o;

static void usage(void)
{
    fprintf(stderr, "menusim <core> [--root TREE] [--mode 1080p|1080p60|720p|720p60|576p|480p|800x600|1024x768|1280x1024] [--toml FILE] [--ops FILE]\n"
                    "        [--stick FILE] [--scale N] [--keys K1,K2,... --shot out.png]\n");
    exit(2);
}

static bool same(const char *a, const char *b) { return a && b && !strcasecmp(a, b); }

static const CoreDesc *find_core(const char *name)
{
    for (int i = 0; core_table[i]; i++) {
        const CoreDesc *d = core_table[i];
        if (same(d->core_id, name) || same(d->cor_name, name) || same(d->folder, name)) {
            return d;
        }
    }
    return 0;
}

static bool exists(const std::string &p) { struct stat st; return stat(p.c_str(), &st) == 0; }
// The file's change time in ms (whole seconds where the platform gives no more).
static long mtime_of(const char *p)
{
    struct stat st;
    if (stat(p, &st) != 0) { return -1; }
#if defined(__APPLE__)
    return (long)st.st_mtime * 1000 + st.st_mtimespec.tv_nsec / 1000000;
#elif defined(_WIN32)
    return (long)st.st_mtime * 1000;
#else
    return (long)st.st_mtime * 1000 + st.st_mtim.tv_nsec / 1000000;
#endif
}

// ---- the mode --------------------------------------------------------------------------------------
struct Mode { const char *name; int hdmi, sys; };
static const Mode modes[] = {
    { "1080p", VID_HDMI_1080P, VID_SYS_PAL50 }, { "1080p60", VID_HDMI_1080P, VID_SYS_NTSC60 },
    { "720p", VID_HDMI_720P, VID_SYS_PAL50 }, { "720p60", VID_HDMI_720P, VID_SYS_NTSC60 },
    { "576p", VID_HDMI_480_576, VID_SYS_PAL50 }, { "480p", VID_HDMI_480_576, VID_SYS_NTSC60 },
    { "800x600", VID_HDMI_800x600, VID_SYS_NTSC60 }, { "1024x768", VID_HDMI_1024x768, VID_SYS_NTSC60 }, { "1280x1024", VID_HDMI_1280x1024, VID_SYS_NTSC60 },
};
static Mode mode;
static int frame_w, frame_h;

// ---- the menu: the core's descriptor, or its descriptor with the menu record merged in ---------------------------------------------
struct Menu {
    const CoreDesc *open;                 // what ui_open() gets: the core's own descriptor
    CoreDesc *desc;                       // what the menu runs on
    const MenuMerge *mm;
};

// tools/descrec.py of the tree compiles the TOML; the record's bytes, or false with the tool's sentence.
static bool compile_toml(std::vector<uint8_t> *rec, std::string *why)
{
    const char *dir = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s/menusim-rec-%d.bin", dir, (int)getpid());
    std::string cmd = std::string("python3 '") + o.root + "/tools/descrec.py' compile '" + o.toml + "' '" + tmp + "'";
    if (o.ops) { cmd += std::string(" --ops '") + o.ops + "'"; }
    cmd += " 2>&1";
    FILE *p = popen(cmd.c_str(), "r");
    std::string out;
    char b[256];
    while (p && fgets(b, sizeof(b), p)) { out += b; }
    int rc = p ? pclose(p) : -1;
    if (rc != 0) {
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) { out.pop_back(); }
        *why = out.empty() ? "descrec.py failed" : out;
        unlink(tmp);
        return false;
    }
    FILE *f = fopen(tmp, "rb");
    rec->clear();
    int c;
    while (f && (c = fgetc(f)) != EOF) { rec->push_back((uint8_t)c); }
    if (f) { fclose(f); }
    unlink(tmp);
    return true;
}

// The menu as run.cc builds it: menu_build extends the core's descriptor with Picture, Volume, Persist, Home and the hotkeys (run.cc:446, the
// bench's board_desc), then bundle_merge lays the description record over it (run.cc:590). The record is the TOML compiled by descrec.py, else
// the one the application carries (bundle_read's fallback for a start without a .cor, bundle.cc:80), else none. No libraries, no applications.
static bool make_menu(const CoreDesc *src, Menu *m, std::string *why, uint32_t *rec_n)
{
    struct Held { DescRecord rec; std::vector<uint8_t> bytes; CoreDesc out; MenuMerge mm; CoreCtx c; };
    Held *h = new Held();                                     // lives for the run: the menu points into it
    PictureEnv env = { EDID_ROW_MASK, 0, true, mode.sys };
    CoreDesc *ext = board_desc(src, &env);
    *rec_n = 0;
    if (o.toml) {
        if (!compile_toml(&h->bytes, why)) { return false; }
    } else if (src->menu_rec) {
        h->bytes.assign(src->menu_rec, src->menu_rec + src->menu_rec_n);
    }
    BundleExtras x = BundleExtras();
    if (!h->bytes.empty()) {
        char w[200];
        if (descrec_parse(h->bytes.data(), (uint32_t)h->bytes.size(), &h->rec, w, sizeof(w)) != 0) {
            *why = w;
            return false;
        }
        x.desc = &h->rec;
        x.desc_bytes = h->bytes.data();
        *rec_n = (uint32_t)h->bytes.size();
    }
    h->c.desc = ext;                                          // bundle_merge reads the core's operations through it (ops_of)
    int refused = bundle_merge(&h->c, ext, &x, 0, 0, &h->out, &h->mm, 0);
    for (int i = 0; i < h->mm.nerr; i++) { printf("[menu] refused: %s\n", h->mm.err[i]); }
    if (refused > h->mm.nerr) { printf("[menu] %d more contributions refused\n", refused - h->mm.nerr); }
    m->open = src;                                            // run.cc:422 opens the UI on the core's own descriptor
    m->desc = &h->out;
    m->mm = &h->mm;
    return true;
}

// ---- the stick -------------------------------------------------------------------------------------
static void fill_stick(Rig *r)
{
    if (o.stick) {
        FILE *f = fopen(o.stick, "r");
        if (!f) { fprintf(stderr, "menusim: cannot read %s\n", o.stick); exit(2); }
        char line[600];
        while (fgets(line, sizeof(line), f)) {
            char path[512];
            unsigned long n = 0;
            if (line[0] == '#' || sscanf(line, "%511s %lu", path, &n) < 1) { continue; }
            r->fs.put_size(path, n ? n : 1);
        }
        fclose(f);
        return;
    }
    static const struct { const char *p; size_t n; } dflt[] = {
        { "/usb0/games/Katakis.d64", 174848 }, { "/usb0/games/Elite.g64", 333744 }, { "/usb0/games/Turrican.crt", 65616 },
        { "/usb0/games/demo.prg", 2049 }, { "/usb0/games/Giana_Sisters.t64", 30000 }, { "/usb0/games/Turrican_II.adf", 901120 },
        { "/usb0/games/Workbench.hdf", 1048576 }, { "/usb0/games/Mario.nes", 40976 }, { "/usb0/games/Boulderdash.atr", 92176 },
        { "/usb0/games/notes.txt", 100 },
    };
    for (auto &f : dflt) { r->fs.put_size(f.p, f.n); }
}

// ---- the session -----------------------------------------------------------------------------------
static const CoreDesc *g_src;
static Menu g_menu;
static Rig *g_rig;
static bool g_open = true;                                    // the menu is up
static std::vector<int> g_keys;                               // every key given, played again after a reload
static bool g_dirty = true;

static const char *key_name(int k)
{
    static char b[16];
    switch (k) {
    case KEY_F1: return "F1"; case KEY_F2: return "F2"; case KEY_F3: return "F3"; case KEY_F4: return "F4";
    case KEY_F5: return "F5"; case KEY_F6: return "F6"; case KEY_F7: return "F7"; case KEY_F8: return "F8"; case KEY_F12: return "F12";
    case KEY_UP: return "UP"; case KEY_DOWN: return "DOWN"; case KEY_LEFT: return "LEFT"; case KEY_RIGHT: return "RIGHT";
    case KEY_RETURN: return "RETURN"; case KEY_ESCAPE: return "ESC"; case KEY_BACK: return "BACK"; case KEY_TAB: return "TAB";
    case KEY_HOME: return "HOME"; case KEY_END: return "END"; case KEY_PAGEUP: return "PGUP"; case KEY_PAGEDOWN: return "PGDN";
    case KEY_DELETE: return "DEL"; case KEY_SPACE: return "SPACE";
    }
    snprintf(b, sizeof(b), k >= 32 && k < 127 ? "%c" : "0x%02X", k);
    return b;
}

static int key_of(const char *n)
{
    static const struct { const char *n; int k; } t[] = {
        { "F1", KEY_F1 }, { "F2", KEY_F2 }, { "F3", KEY_F3 }, { "F4", KEY_F4 }, { "F5", KEY_F5 }, { "F6", KEY_F6 }, { "F7", KEY_F7 }, { "F8", KEY_F8 },
        { "F12", KEY_F12 }, { "UP", KEY_UP }, { "DOWN", KEY_DOWN }, { "LEFT", KEY_LEFT }, { "RIGHT", KEY_RIGHT }, { "RETURN", KEY_RETURN },
        { "ENTER", KEY_RETURN }, { "ESC", KEY_ESCAPE }, { "BACK", KEY_BACK }, { "BACKSPACE", KEY_BACK }, { "TAB", KEY_TAB }, { "HOME", KEY_HOME },
        { "END", KEY_END }, { "PGUP", KEY_PAGEUP }, { "PGDN", KEY_PAGEDOWN }, { "DEL", KEY_DELETE }, { "SPACE", KEY_SPACE },
    };
    for (auto &e : t) { if (!strcasecmp(e.n, n)) return e.k; }
    return strlen(n) == 1 ? (unsigned char)n[0] : -1;
}

// One key as run.cc's give_key hands it to the core's menu: MenuCtl::key, then the render; R_CLOSE closes the menu. F12 opens a closed menu
// (the board opens it with the menu button or the core's menu hotkey, neither of which reaches the simulator as a key).
static void give_key(int k, bool log)
{
    if (!g_open) {
        if (k == KEY_F12) {
            g_open = true;
            g_rig->m->open();
            ui_menu_visible(true);
            ui_menu_render(*g_rig->m);
            if (log) { printf("key F12 -> menu open\n"); }
        }
        return;
    }
    MenuCtl::Result r = g_rig->m->key(k);
    if (r == MenuCtl::R_CLOSE && !g_rig->c.desc->menu_first) {
        g_open = false;
        ui_menu_visible(false);
        if (log) { printf("key %s -> menu closed (F12 opens it)\n", key_name(k)); }
        return;
    }
    ui_menu_render(*g_rig->m);
    lv_timer_handler();                                       // LVGL runs between two keys, as on the board (and in the bench's steps)
    if (log) {
        printf("key %s -> area %d tab %d \"%s\" row %d\n", key_name(k), g_rig->m->area(), g_rig->m->tab(), g_rig->m->title(), g_rig->m->selected());
    }
}

// The first row of the current view whose label starts with `text`, -1 when there is none.
static int find_row(const char *text)
{
    for (int i = 0; i < g_rig->m->rows(); i++) {
        char l[160], v[160];
        bool active = false;
        g_rig->m->row(i, l, sizeof(l), v, sizeof(v), &active);
        if (!strncmp(l, text, strlen(text))) { return i; }
    }
    return -1;
}

// A new rig on the current menu, placed for the mode as run.cc places it (ui_open, then video_place_menu), and the keys of the session played again.
static void start_rig(bool replay)
{
    char cfg[160];
    snprintf(cfg, sizeof(cfg), "/usb0/cores/menusim_%s.cfg", g_src->folder ? g_src->folder : "core");
    g_rig = new Rig(g_menu.desc, cfg, g_menu.mm);                // the old rig stays allocated: the view may still point into it until the render below
    g_rig->c.started = true;
    g_rig->c.program_loaded = true;
    fill_stick(g_rig);
    video_place_menu(&g_rig->px, mode.hdmi, mode.sys);
    g_open = true;
    ui_menu_visible(true);
    ui_menu_render(*g_rig->m);
    if (replay) {
        for (int k : g_keys) { give_key(k, false); }
    }
    lv_obj_invalidate(lv_screen_active());
    g_dirty = true;
}

static void place(int *x, int *y, int *w, int *h)
{
    PxRec::Pl p = g_rig->px.place.empty() ? PxRec::Pl{ 0, 0, cvw, cvh } : g_rig->px.place.back();
    *x = p.x; *y = p.y; *w = p.w; *h = p.h;
}

// The picture as the bench's mock_shot() takes it: the menu rendered, LVGL run, then the whole page drawn again.
static void shot(const char *path)
{
    if (g_open) { ui_menu_render(*g_rig->m); }
    lv_timer_handler();
    lv_obj_invalidate(lv_screen_active());
    lv_timer_handler();
    int x, y, w, h;
    place(&x, &y, &w, &h);
    png_out(path, frame_w, frame_h, x, y, w, h);
}

// ---- reload ----------------------------------------------------------------------------------------
static void reload(void)
{
    uint32_t t0 = SDL_GetTicks(), n = 0;
    std::string why;
    Menu m;
    if (!make_menu(g_src, &m, &why, &n)) {
        printf("reload refused: %s\n", why.c_str());
        g_rig->m->say(("menu.toml refused: " + why).substr(0, 150).c_str(), true);
        ui_menu_render(*g_rig->m);
        return;
    }
    g_menu = m;
    start_rig(true);
    printf("reload: %s -> record %u bytes, menu rebuilt in %u ms\n", o.toml, (unsigned)n, (unsigned)(SDL_GetTicks() - t0));
}

// ---- the window ------------------------------------------------------------------------------------
static uint32_t wall_tick(void) { return SDL_GetTicks(); }
static void on_frame(void) { g_dirty = true; }

static int sdl_key(const SDL_Keysym &ks)
{
    bool shift = ks.mod & KMOD_SHIFT;
    switch (ks.sym) {
    case SDLK_F1: return shift ? KEY_F2 : KEY_F1;
    case SDLK_F2: return KEY_F2;
    case SDLK_F3: return shift ? KEY_F4 : KEY_F3;
    case SDLK_F4: return KEY_F4;
    case SDLK_F5: return shift ? KEY_F6 : KEY_F5;
    case SDLK_F6: return KEY_F6;
    case SDLK_F7: return shift ? KEY_F8 : KEY_F7;
    case SDLK_F8: return KEY_F8;
    case SDLK_F12: return KEY_F12;
    case SDLK_UP: return KEY_UP;
    case SDLK_DOWN: return KEY_DOWN;
    case SDLK_LEFT: return KEY_LEFT;
    case SDLK_RIGHT: return KEY_RIGHT;
    case SDLK_RETURN: case SDLK_KP_ENTER: return KEY_RETURN;
    case SDLK_ESCAPE: return KEY_ESCAPE;
    case SDLK_BACKSPACE: return KEY_BACK;
    case SDLK_TAB: return KEY_TAB;
    case SDLK_HOME: return KEY_HOME;
    case SDLK_END: return KEY_END;
    case SDLK_PAGEUP: return KEY_PAGEUP;
    case SDLK_PAGEDOWN: return KEY_PAGEDOWN;
    case SDLK_DELETE: return KEY_DELETE;
    case SDLK_SPACE: return KEY_SPACE;
    }
    if (ks.sym >= SDLK_a && ks.sym <= SDLK_z) { return shift ? (int)(ks.sym - SDLK_a + 'A') : (int)ks.sym; }
    if (ks.sym >= SDLK_0 && ks.sym <= SDLK_9) { return (int)ks.sym; }
    return -1;
}

static int run_window(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "menusim: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    int scale_num = 1, scale_den = 1;
    if (o.scale > 0) {
        scale_num = o.scale;
    } else {
        SDL_Rect usable;
        if (SDL_GetDisplayUsableBounds(0, &usable) == 0) {
            while (frame_w * scale_num / scale_den > usable.w * 9 / 10 || frame_h * scale_num / scale_den > usable.h * 9 / 10) { scale_den++; }
        }
    }
    char title[160];
    snprintf(title, sizeof(title), "menusim - %s - %s %dx%d", g_src->title ? g_src->title : o.core, mode.name, frame_w, frame_h);
    SDL_Window *win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, frame_w * scale_num / scale_den, frame_h * scale_num / scale_den,
                                       SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : 0;
    if (win && !ren) { ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE); }    // no GPU renderer (a remote session, SDL's offscreen driver)
    SDL_Texture *tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, frame_w, frame_h) : 0;
    if (!tex) {
        fprintf(stderr, "menusim: SDL window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    std::vector<unsigned char> rgb((size_t)frame_w * frame_h * 3);
    printf("menusim: F1-F8 need fn on a Mac keyboard unless \"Use F1, F2, etc. keys as standard function keys\" is on; shift+F1 = F2.\n"
           "         Cmd+R reload, Cmd+S screenshot, Cmd+Q quit, F12 opens the menu after a close.\n");
    long seen = o.toml ? mtime_of(o.toml) : -1;
    uint32_t next_poll = SDL_GetTicks() + 250;
    int shots = 0;
    bool quit = false;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { quit = true; }
            if (e.type != SDL_KEYDOWN) { continue; }
            bool cmd = e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL);
            if (cmd && e.key.keysym.sym == SDLK_q) { quit = true; continue; }
            if (cmd && e.key.keysym.sym == SDLK_r) { if (o.toml) { reload(); seen = mtime_of(o.toml); } else { printf("reload: this core has no menu TOML\n"); } continue; }
            if (cmd && e.key.keysym.sym == SDLK_s) {
                char p[256];
                snprintf(p, sizeof(p), "menusim-%s-%s-%d.png", o.core, mode.name, ++shots);
                shot(p);
                continue;
            }
            if (cmd) { continue; }
            int k = sdl_key(e.key.keysym);
            if (k >= 0) { g_keys.push_back(k); give_key(k, true); }
        }
        if (o.toml && SDL_GetTicks() >= next_poll) {
            next_poll = SDL_GetTicks() + 250;
            long now = mtime_of(o.toml);
            if (now != seen && now >= 0) { seen = now; reload(); }
        }
        if (g_rig->m->tick()) { ui_menu_render(*g_rig->m); }
        lv_timer_handler();
        if (g_dirty) {
            g_dirty = false;
            int x, y, w, h;
            place(&x, &y, &w, &h);
            rig_compose(rgb.data(), frame_w, frame_h, x, y, w, h);
            SDL_UpdateTexture(tex, 0, rgb.data(), frame_w * 3);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, 0, 0);
            SDL_RenderPresent(ren);
        } else {
            SDL_Delay(8);
        }
    }
    SDL_Quit();
    return 0;
}

// ---- main ------------------------------------------------------------------------------------------
int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IOLBF, 0);
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> const char * { if (i + 1 >= argc) usage(); return argv[++i]; };
        if (a == "--mode") { o.mode = val(); }
        else if (a == "--keys") { o.keys = val(); }
        else if (a == "--shot") { o.shot = val(); }
        else if (a == "--toml") { o.toml = val(); }
        else if (a == "--ops") { o.ops = val(); }
        else if (a == "--stick") { o.stick = val(); }
        else if (a == "--root") { o.root = val(); }
        else if (a == "--scale") { o.scale = atoi(val()); }
        else if (a == "--list") {
            for (int k = 0; core_table[k]; k++) { printf("%-12s %s\n", core_table[k]->core_id ? core_table[k]->core_id : core_table[k]->folder, core_table[k]->title); }
            return 0;
        }
        else if (a[0] == '-') { usage(); }
        else if (!o.core) { o.core = argv[i]; }
        else { usage(); }
    }
    if (!o.core) { usage(); }
    g_src = find_core(o.core);
    if (!g_src) {
        fprintf(stderr, "menusim: no core \"%s\"; the core table has:", o.core);
        for (int i = 0; core_table[i]; i++) { fprintf(stderr, " %s", core_table[i]->core_id ? core_table[i]->core_id : core_table[i]->folder); }
        fprintf(stderr, "\n");
        return 2;
    }
    bool found = false;
    for (auto &m : modes) { if (same(m.name, o.mode)) { mode = m; found = true; } }
    if (!found) { usage(); }
    video_active_size(mode.hdmi, mode.sys, &frame_w, &frame_h);
    // the menu TOML: --toml, else coreapp/cores/<core>/menu.toml of the tree; its ops list: --ops, else tests/coreapp/golden/ops_<core>.txt
    static std::string toml_s, ops_s;
    if (!o.toml) {
        toml_s = std::string(o.root) + "/coreapp/cores/" + o.core + "/menu.toml";
        if (exists(toml_s)) { o.toml = toml_s.c_str(); }
    }
    if (o.toml && !o.ops) {
        ops_s = std::string(o.root) + "/tests/coreapp/golden/ops_" + o.core + ".txt";
        if (exists(ops_s)) { o.ops = ops_s.c_str(); }
    }
    if (!o.keys && !o.shot) { rig_tick = wall_tick; }             // the window runs on the wall clock; headless keeps the bench's steps
    rig_on_frame = on_frame;

    std::string why;
    uint32_t n = 0;
    if (!make_menu(g_src, &g_menu, &why, &n)) {
        fprintf(stderr, "menusim: the menu of %s: %s\n", o.core, why.c_str());
        return 1;
    }
    printf("menusim: %s (%s), mode %s %dx%d, menu %s%s%s\n", o.core, g_src->title, mode.name, frame_w, frame_h,
           o.toml ? o.toml : (g_src->menu_rec ? "the built-in record" : "the C descriptor"), o.ops ? ", ops " : "", o.ops ? o.ops : "");
    ui_open(g_menu.open);
    start_rig(false);
    {
        int x, y, w, h;
        place(&x, &y, &w, &h);
        printf("canvas %dx%d at %d,%d shown %dx%d (frame %dx%d)\n", cvw, cvh, x, y, w, h, frame_w, frame_h);
    }
    if (o.keys || o.shot) {
        std::string ks = o.keys ? o.keys : "";
        size_t at = 0;
        while (at < ks.size()) {
            size_t c = ks.find(',', at);
            std::string k = ks.substr(at, c == std::string::npos ? std::string::npos : c - at);
            at = c == std::string::npos ? ks.size() : c + 1;
            if (k.empty()) continue;
            if (!k.compare(0, 5, "FIND:") || !k.compare(0, 4, "SEL:")) {   // the selection to a row by UP and DOWN: the first whose label starts with the text, or row n
                bool find = !k.compare(0, 5, "FIND:");
                int row = find ? find_row(k.c_str() + 5) : atoi(k.c_str() + 4);
                if (row < 0 || row >= g_rig->m->rows()) { fprintf(stderr, "menusim: no row \"%s\" here\n", k.c_str()); return 1; }
                for (int guard = 0; guard < 200 && g_rig->m->selected() != row; guard++) {
                    int code = g_rig->m->selected() > row ? KEY_UP : KEY_DOWN;
                    g_keys.push_back(code);
                    give_key(code, false);
                }
                ui_menu_render(*g_rig->m);
                printf("%s -> row %d\n", k.c_str(), g_rig->m->selected());
                continue;
            }
            int code = key_of(k.c_str());
            if (code < 0) { fprintf(stderr, "menusim: unknown key %s\n", k.c_str()); return 2; }
            g_keys.push_back(code);
            give_key(code, true);
            for (int i = 0; i < 4 && g_rig->m->tick(); i++) { ui_menu_render(*g_rig->m); }
            lv_timer_handler();
        }
        if (o.shot) { shot(o.shot); }
        return 0;
    }
    return run_window();
}
