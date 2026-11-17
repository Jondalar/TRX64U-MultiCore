// The menu simulator's window and keys (T236), shared by menusim.cc (the core application) and hostsim.cc (the host application): the key names of
// the headless --keys list, the SDL key table, and the window loop. A program includes it after keyboard.h and fills a SimApp.
#ifndef TOOLS_MENUSIM_SIMWIN_H
#define TOOLS_MENUSIM_SIMWIN_H

#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <vector>
#include <sys/stat.h>

// What the window needs of a program.
struct SimApp {
    const char *what;                     // the window title
    int frame_w, frame_h;                 // the output frame of the mode
    void (*key)(int k);                   // one key of keyboard.h, logged
    void (*idle)(void);                   // between events: the application's own ticks, a reload
    void (*compose)(unsigned char *rgb);  // the frame, frame_w x frame_h pixels of three bytes
    void (*shot)(const char *path);       // Cmd+S
    void (*reload)(void);                 // Cmd+R, null: nothing to reload
    const char *shot_name;                // the screenshot's file name, before "-<n>.png"
};

static bool sim_dirty = true;             // a frame was finished since the window last showed one (the program's frame hook sets it)
static void sim_on_frame(void) { sim_dirty = true; }
static uint32_t sim_wall_tick(void) { return SDL_GetTicks(); }

static const char *sim_key_name(int k)
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

// A key of the --keys list by name (F1, DOWN, RETURN, ESC, ...) or one character; -1 for none.
static int sim_key_of(const char *n)
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

// The --keys list split at the commas.
static std::vector<std::string> sim_split_keys(const char *list)
{
    std::vector<std::string> out;
    std::string ks = list ? list : "";
    size_t at = 0;
    while (at < ks.size()) {
        size_t c = ks.find(',', at);
        std::string k = ks.substr(at, c == std::string::npos ? std::string::npos : c - at);
        at = c == std::string::npos ? ks.size() : c + 1;
        if (!k.empty()) { out.push_back(k); }
    }
    return out;
}

// The file's change time in ms (whole seconds where the platform gives no more).
static long sim_mtime(const char *p)
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

// A Mac key as the C64 keyboard of the board gives it: F1-F8 (shift+F1 = F2, as on a C64), cursor keys, RETURN, ESC, ...; -1 for none.
static int sim_sdl_key(const SDL_Keysym &ks)
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

// The window: the frame scaled to fit the screen (or by `scale`), keys to the application, Cmd (Ctrl) + R reload, + S screenshot, + Q quit.
static int sim_window(const SimApp &a, int scale)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "menusim: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    int num = 1, den = 1;
    if (scale > 0) {
        num = scale;
    } else {
        SDL_Rect usable;
        if (SDL_GetDisplayUsableBounds(0, &usable) == 0) {
            while (a.frame_w * num / den > usable.w * 9 / 10 || a.frame_h * num / den > usable.h * 9 / 10) { den++; }
        }
    }
    SDL_Window *win = SDL_CreateWindow(a.what, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, a.frame_w * num / den, a.frame_h * num / den,
                                       SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : 0;
    if (win && !ren) { ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE); }    // no GPU renderer (a remote session, SDL's dummy driver)
    SDL_Texture *tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, a.frame_w, a.frame_h) : 0;
    if (!tex) {
        fprintf(stderr, "menusim: SDL window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    std::vector<unsigned char> rgb((size_t)a.frame_w * a.frame_h * 3);
    printf("menusim: F1-F8 need fn on a Mac keyboard unless \"Use F1, F2, etc. keys as standard function keys\" is on; shift+F1 = F2.\n"
           "         Cmd+R reload, Cmd+S screenshot, Cmd+Q quit.\n");
    int shots = 0;
    bool quit = false;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { quit = true; }
            if (e.type != SDL_KEYDOWN) { continue; }
            bool cmd = e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL);
            if (cmd && e.key.keysym.sym == SDLK_q) { quit = true; continue; }
            if (cmd && e.key.keysym.sym == SDLK_r) {
                if (a.reload) { a.reload(); } else { printf("reload: nothing to reload here\n"); }
                continue;
            }
            if (cmd && e.key.keysym.sym == SDLK_s) {
                char p[256];
                snprintf(p, sizeof(p), "%s-%d.png", a.shot_name, ++shots);
                a.shot(p);
                continue;
            }
            if (cmd) { continue; }
            int k = sim_sdl_key(e.key.keysym);
            if (k >= 0) { a.key(k); }
        }
        a.idle();
        if (sim_dirty) {
            sim_dirty = false;
            a.compose(rgb.data());
            SDL_UpdateTexture(tex, 0, rgb.data(), a.frame_w * 3);
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

#endif
