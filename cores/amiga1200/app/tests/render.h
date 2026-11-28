// T239 D17: the Amiga 1200's cases of the coreapp render bench, moved out of tests/coreapp/render/render.cc (2026-11-27). Its TOML B says
// render = true, so tests/coreapp/render/run.sh builds it as a render build of its own (RENDER_CORE=amiga1200, step 9 of its lane) and passes this file as RENDER_CORE_FILE;
// render.cc includes it three times: RENDER_PART 1 its headers, 2 the floors of its walks and the rows it adds to the shared walks, 3 its cases and the calls of main().
#if RENDER_PART == 1
#elif RENDER_PART == 2
static const Floors FLOOR = { 18, 414, 0, 696, 87, 1, 12, 38, 21, 27, 135, 3, 21, 21, 2 };   // measured 2026-11-27 (T239 D17)
#define HKL_PIC(s) ((s) == &amiga1200_desc)            // its learned keys are pictured (hotkey_learn)
// its pages of the UX shots (ux_shots, the rows of jobs[])
#define RENDER_UX_JOBS \
        { &amiga1200_desc, "a1200", { { "F5-core-top", KEY_F5, 0, 0 }, { "F5-core-end", KEY_F5, 0, 1 }, { "F5-video", KEY_F5, "Video", 0 }, { "F5-audio", KEY_F5, "Audio", 0 }, \
                                             { "F5-input", KEY_F5, "Input", 0 }, { "hotkeys", KEY_F5, "Input", 2 }, { "F1-home", KEY_F1, 0, 0 }, { "F8-system", KEY_F8, 0, 0 } }, 8 },
#define RENDER_RTC_CORE { &amiga1200_desc, "a1200", "amiga1200" }   // its Settings page with the real-time clock row (rtc_menu, follow_menu)
#define RENDER_PM_CORE &amiga1200_desc, "a1200"                     // the core of the picture-mode pages (pm_page_fixtures)
#elif RENDER_PART == 3
// T107 5.9, the A1200 (the core with picture modes of its own): the Settings page as the monitor gets it in 1080p, in 720p, and after the mode was
// changed from the menu itself (720p -> 1080p with the menu up). The video service hands the layer's place to the port; the bench puts the canvas there.
// (T239 D17: the checks of the row itself and of the reset ring that followed here name no core: render.cc row_rules, on the made-up core)
static void a1200_menu(const char *out)
{
    char p[512];
    static CoreDesc a12;                                               // the descriptor as the board runs it: the core's own items plus the application's whole chain (T187)
    PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };           // T164: a host whose gateware answers, a screen that names every row
    a12 = *board_desc(&amiga1200_desc, &env);
    Rig r(&a12, "/usb0/cores/amiga1200.cfg");
    r.c.started = true;
    r.c.program_loaded = true;
    ui_open(&a12);
    picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
    r.vid->init();
    r.m->open();
    r.m->key(KEY_F5);
    r.m->key('1');
    ui_menu_render(*r.m);
    lv_timer_handler();
    struct { const char *name; int value; int fw, fh; } steps[] = {
        { "1080", pic_value50(VID_HDMI_1080P), 1920, 1080 },
        { "720", pic_value50(VID_HDMI_720P), 1280, 720 },
        { "after-switch-1080", pic_value50(VID_HDMI_1080P), 1920, 1080 },
        { "800x600", pic_value50(VID_HDMI_800x600), 800, 600 },
    };
    for (unsigned i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        if (i > 0) {
            picture_apply(&r.c, steps[i].value);               // from the menu's Picture item: the layer is the menu's, the picture is off
        }
        CHECK(!r.px.place.empty());
        PxRec::Pl g = r.px.place.empty() ? PxRec::Pl{ 0, 0, 960, 540 } : r.px.place.back();
        TrxCanvas want = trx_canvas_for(steps[i].fw, steps[i].fh);        // D10a: 960 x 540 at a whole factor, or the mode's own size below 960 wide
        CHECK(g.w == want.w * want.factor && g.h == want.h * want.factor);   // a whole factor of the canvas, nothing stretched
        CHECK(trx_canvas_w() == want.w && trx_canvas_h() == want.h);
        CHECK(g.x == (steps[i].fw > g.w ? (steps[i].fw - g.w) / 2 : 0));       // centred horizontally ...
        CHECK(g.y == (steps[i].fh > g.h ? (steps[i].fh - g.h) / 2 : 0));       // ... and vertically
        ui_menu_render(*r.m);
        lv_timer_handler();
        CHECK(view_bounds(steps[i].name, want.w, want.h) == 0);             // nothing of the page lies outside the canvas
        snprintf(p, sizeof(p), "%s/a1200-settings-%s.png", out, steps[i].name);
        png_out(p, steps[i].fw, steps[i].fh, g.x, g.y, g.w, g.h);
    }
    {   // T209: the Home page at 480p (canvas 720 x 480) and at the full canvas: the title is one line and no text lies on another
        r.m->key(KEY_F1);
        for (int k = 0; k < 2; k++) {
            TrxCanvas c480 = trx_canvas_for(720, 480);
            if (k == 0) {
                trx_canvas_set(c480.w, c480.h);
                trx_shell_forget();
            }
            ui_menu_render(*r.m);
            lv_timer_handler();
            CHECK(txt_overlaps(k == 0 ? "a1200 home 480p" : "a1200 home") == 0);
            {   // T212 (the drive rows and the machine line lengthen Home): the last row of Home stands inside the panel that holds it, at 480p and at the full canvas
                char l[120], v[120]; bool ap;
                int nr = r.m->rows();
                r.m->row(nr - 1, l, sizeof(l), v, sizeof(v), &ap);
                lv_obj_t *lb = find_label(lv_screen_active(), l);
                CHECK(lb != 0);
                if (lb) {
                    lv_area_t ra, pa;
                    lv_obj_t *row = lv_obj_get_parent(lb), *panel = lv_obj_get_parent(row);
                    lv_obj_get_coords(row, &ra);
                    lv_obj_get_coords(panel, &pa);
                    printf("HOME %s: %d rows, last row \"%s\" y %d..%d, panel y %d..%d\n", k == 0 ? "480p" : "full", nr, l, (int)ra.y1, (int)ra.y2, (int)pa.y1, (int)pa.y2);
                    CHECK(ra.y2 <= pa.y2);
                }
            }
            if (k == 0) {
                TrxCanvas full = trx_canvas_for(1920, 1080);
                trx_canvas_set(full.w, full.h);
                trx_shell_forget();
            }
        }
        r.m->key(KEY_F5);                                    // back to the Settings page (F5, tab 1)
        r.m->key('1');
        ui_menu_render(*r.m);
        lv_timer_handler();
    }
    // T190: the Amiga's Mouse sensitivity / Mouse acceleration rows (group "input") on the Settings page, each selected in turn at 1080p: the selected row
    // is inside the view (R1) and nothing lies outside the canvas.
    {
        Rig m(&a12, "/usb0/cores/amiga1200.cfg");
        m.c.started = true;
        m.c.program_loaded = true;
        ui_open(&a12);
        picture_start(&m.c, VID_SYS_PAL50, VID_HDMI_1080P);
        m.vid->init();
        m.m->open();
        m.m->key(KEY_F5);
        m.m->key('4');                                       // the core, Video, Audio, Input: the Amiga's mouse rows are the fourth tab here
        int found = 0;
        for (int i = 0; i < m.m->rows(); i++) {
            char l[120], v[120]; bool ap;
            m.m->row(i, l, sizeof(l), v, sizeof(v), &ap);
            if (strncmp(l, "Mouse ", 6)) continue;
            found++;
            select_row(m, i);
            CHECK(m.m->selected() == i);
            TrxCanvas want = trx_canvas_for(1920, 1080);
            CHECK(view_bounds("a1200 mouse row", want.w, want.h) == 0);
            PxRec::Pl g = m.px.place.empty() ? PxRec::Pl{ 0, 0, 960, 540 } : m.px.place.back();
            snprintf(p, sizeof(p), "%s/a1200-settings-mouse%d.png", out, found);
            png_out(p, 1920, 1080, g.x, g.y, g.w, g.h);
        }
        CHECK(found == 2);
    }
}

// ---- T205 ux-review finding 1: a 31-character file name on a Home media row (DF0) at 480p and 1080p: the value is cut in the middle, the slot label stays whole ------------
static void ux_longname()
{
    static const struct { const char *name; int fw, fh; } cvs[] = { { "1080p", 1920, 1080 }, { "480p", 640, 480 } };
    static const char *LONG = "/usb0/games/Turrican_II_The_Final_Fight_disk1.adf";           // the file's name is 31 characters
    PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };
    for (const auto &cv : cvs) {
        TrxCanvas want = trx_canvas_for(cv.fw, cv.fh);
        ui_set_canvas(want.w, want.h);
        CoreDesc *d = board_desc(&amiga1200_desc, &env);
        Rig r(d, "/usb0/cores/uxl.cfg");
        std::vector<uint8_t> img(901120, 0);
        r.fs.put(LONG, img);
        r.c.started = true; r.c.program_loaded = true;
        ui_open(d);
        OpIn in[2] = { in_str("path", LONG), in_int("slot", 0) };
        OpResult res;
        CHECK(ops_call(&r.c, "window.mount", in, 2, &res) == 0);
        r.m->open(); ui_menu_forget_rows();
        r.m->key(KEY_F1);
        ui_menu_render(*r.m); lv_timer_handler();
        int found = 0;
        for (int i = 0; i < r.m->rows(); i++) {
            char l[200], v[200], sub[200] = ""; bool a;
            r.m->row(i, l, sizeof(l), v, sizeof(v), &a, sub, sizeof(sub));
            if (!strcmp(l, "DF0")) { found = 1; UXCHECK(strstr(v, "Turrican_II_The_Final_Fight_disk1.adf") != 0, "long name: row DF0 holds \"%s\"", v); }
        }
        UXCHECK(found, "long name %s: no DF0 row on Home", cv.name);
    }
    ui_set_canvas(960, 540);
}

// T228: its area shots (mock_shots: every area and every tab, the click dummy's pictures), with a library of its media and the pages the concept names
static const MockJob a1200_mock[] = {
    { "a1200", &amiga1200_desc,
      { { "/usb0/games/Turrican_II.adf", 901120 }, { "/usb0/games/Lotus_III.adf", 901120 }, { "/usb0/games/Workbench.hdf", 1048576 }, { "/usb0/games/notes.txt", 100 } }, 4,
      { { "F1", "home", "DF0", true, "df0" }, { "F7", "settings", "Hotkeys", false, "hotkeys" } }, 2, "Turrican" },
};

static int render_core_first(const char *out, int, char **)        // before the shared pages; a return of 0 or more ends main() with it
{
    if (getenv("MOCK_ONLY")) {                                      // T228: only the area shots (mock_shots, the click dummy's pictures), without the rest of the bench
        mock_shots(out, a1200_mock, 1);
        printf("coreapp render (MOCK_ONLY): %d checks, %d failed\n", checks, fails);
        return fails ? 1 : 0;
    }
    a1200_menu(out);
    modes_menu(out, &amiga1200_desc, "a1200", ".adf");              // the menu in every mode the host offers, with its library
    return -1;
}
#define RENDER_CORE_FIRST render_core_first

static void render_core_late(const char *out)                       // after the shared pages
{
    ux_longname();
    ux_wifi(out, &amiga1200_desc, "a1200");
    mock_shots(out, a1200_mock, 1);                                 // T228: its area shots, last, so every check above runs as before
}
#define RENDER_CORE_LATE render_core_late
#endif
