// T239 D17: the C128's cases of the coreapp render bench, moved out of tests/coreapp/render/render.cc (2026-11-27). Its TOML B says
// render = true, so tests/coreapp/render/run.sh builds it as a render build of its own (RENDER_CORE=c128, step 9 of its lane) and passes this file as RENDER_CORE_FILE;
// render.cc includes it three times: RENDER_PART 1 its headers, 2 the floors of its walks and the rows it adds to the shared walks, 3 its cases and the calls of main().
#if RENDER_PART == 1
#elif RENDER_PART == 2
static const Floors FLOOR = { 15, 399, 516, 258, 129, 1, 13, 33, 18, 24, 120, 3, 24, 0, 2 };
#define HKL_PIC(s) ((s) == &c128_desc)                 // its learned keys are pictured (hotkey_learn)
// its pages of the UX shots (ux_shots, the rows of jobs[])
#define RENDER_UX_JOBS \
        { &c128_desc, "c128", { { "F1-home", KEY_F1, 0, 0 }, { "F5-core-end", KEY_F5, 0, 1 }, { "F5-video", KEY_F5, "Video", 0 }, { "F5-audio", KEY_F5, "Audio", 0 }, \
                                       { "F5-input", KEY_F5, "Input", 0 }, { "F8-system", KEY_F8, 0, 0 } }, 6 },
#elif RENDER_PART == 3
// T185 D4: the C128's four JiffyDOS file rows on the F5 Settings tab, the file selector a row opens, the refusal of a file in it, and the F3 action list of a .bin file.
// The bench writes the pictures; whether they read well is the UX review's.
static void c128_kit_menu(const char *out)
{
    char p[512];
    CoreDesc *c128_b = board_desc(&c128_desc);
    Rig r(c128_b, "/usb0/cores/c128.cfg");
    std::vector<uint8_t> good(16384, 0x41), small(8192, 0x55);
    for (int i = 0; i < 16384; i++) good[i] = (uint8_t)(i * 7 + (i >> 8) + (i >> 13));
    memcpy(&good[4], "\x11\x11\x11\x11\x11\x11\x11\x11", 8);
    good[0x3FFC] = 0x3D; good[0x3FFD] = 0xFF;
    small[0x1FFC] = 0xE2; small[0x1FFD] = 0xFC;                    // a C64 KERNAL of 8 KiB: its reset vector is $FCE2
    r.fs.put("/usb0/jiffy/c128kernal.bin", good);
    r.fs.put("/usb0/jiffy/kernal8k.bin", small);
    r.fs.put("/usb0/jiffy/notes.txt", std::vector<uint8_t>(10, 1));
    r.fs.put("/usb0/games/disk.d64", std::vector<uint8_t>(174848, 0));
    r.c.started = true;
    r.c.program_loaded = true;
    ui_open(c128_b);
    OpResult res;
    OpIn in = in_str("path", "/usb0/jiffy/c128kernal.bin");
    CHECK(ops_call(&r.c, "c128.use_jiffy_c128", &in, 1, &res) == OPE_OK);
    r.m->open();
    r.m->key(KEY_F5);
    r.m->key('1');
    ui_menu_render(*r.m);
    lv_timer_handler();
    snprintf(p, sizeof(p), "%s/c128-kit-settings.png", out);
    png(p, 2);
    auto row_of = [&](const char *t) {
        for (int i = 0; i < r.m->rows(); i++) { char l[200], v[100]; bool a; r.m->row(i, l, 200, v, 100, &a); if (!strcmp(l, t)) return i; }
        return -1;
    };
    int row = row_of("JiffyDOS C64 ROM");
    CHECK(row >= 0);
    select_row(r, row);
    ui_menu_render(*r.m); lv_timer_handler();
    snprintf(p, sizeof(p), "%s/c128-kit-settings-c64row.png", out);
    png(p, 2);
    r.m->key(KEY_RETURN);
    CHECK(r.m->picking());
    ui_menu_render(*r.m); lv_timer_handler();
    snprintf(p, sizeof(p), "%s/c128-kit-selector.png", out);
    png(p, 2);
    int d = find_dir_row(r, "jiffy");
    if (d < 0 && row_of("usb0/") >= 0) { select_row(r, row_of("usb0/")); r.m->key(KEY_RETURN); d = find_dir_row(r, "jiffy"); }
    CHECK(d >= 0);
    select_row(r, d);
    r.m->key(KEY_RETURN);
    int f = row_of("kernal8k.bin");
    CHECK(f >= 0);
    select_row(r, f);
    ui_menu_render(*r.m); lv_timer_handler();
    snprintf(p, sizeof(p), "%s/c128-kit-selector-folder.png", out);
    png(p, 2);
    r.m->key(KEY_RETURN);                                          // 8 KiB is no C64 part with BASIC but a KERNAL: taken; the C128 role refuses it below
    ui_menu_render(*r.m); lv_timer_handler();
    snprintf(p, sizeof(p), "%s/c128-kit-selector-taken.png", out);
    png(p, 2);
    {
        const TrxTheme &t = trx_theme();                           // the status is one line: it fits the pane (UX review 2026-11-07)
        CHECK(trx_text_w(r.m->status(), t.caption) + 4 <= trx_content_w() - 2 * t.space_lg);
    }
    CHECK(!r.m->picking());
    // the refusal: the C128 row, an 8 KiB file
    select_row(r, row_of("JiffyDOS C128 ROM"));
    r.m->key(KEY_RETURN);
    f = row_of("kernal8k.bin");
    if (f >= 0) {
        select_row(r, f);
        r.m->key(KEY_RETURN);
    }
    CHECK(r.m->picking() && r.m->status_bad());
    ui_menu_render(*r.m); lv_timer_handler();
    snprintf(p, sizeof(p), "%s/c128-kit-selector-refused.png", out);
    png(p, 2);
    {
        const TrxTheme &t = trx_theme();                           // the status is one line: it fits the pane (UX review 2026-11-07)
        CHECK(trx_text_w(r.m->status(), t.caption) + 4 <= trx_content_w() - 2 * t.space_lg);
    }
    r.m->key(KEY_ESCAPE);
    // F3: the action list of a .bin file
    r.m->key(KEY_F3);
    for (int g = 0; g < 8 && strcmp(r.m->title(), "/"); g++) r.m->key(KEY_LEFT);
    select_row(r, row_of("usb0/")); r.m->key(KEY_RETURN);
    select_row(r, find_dir_row(r, "jiffy")); r.m->key(KEY_RETURN);
    select_row(r, row_of("c128kernal.bin"));
    r.m->key(KEY_RETURN);
    CHECK(r.m->chooser());
    ui_menu_render(*r.m); lv_timer_handler();
    snprintf(p, sizeof(p), "%s/c128-kit-file-actions.png", out);
    png(p, 2);
}

static int render_core_first(const char *out, int argc, char **argv)   // before the shared pages; a return of 0 or more ends main() with it
{
    char p[512];
    c128_kit_menu(out);
    return -1;
}
#define RENDER_CORE_FIRST render_core_first
#endif
