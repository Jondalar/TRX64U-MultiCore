// T239 D17: the NES's cases of the coreapp render bench, moved out of tests/coreapp/render/render.cc (2026-11-27). Its TOML B says
// render = true, so tests/coreapp/render/run.sh builds it as a render build of its own (RENDER_CORE=nes, step 9 of its lane) and passes this file as RENDER_CORE_FILE;
// render.cc includes it three times: RENDER_PART 1 its headers, 2 the floors of its walks and the rows it adds to the shared walks, 3 its cases and the calls of main().
#if RENDER_PART == 1
#elif RENDER_PART == 2
static const Floors FLOOR = { 18, 354, 0, 45, 45, 1, 12, 7, 21, 27, 135, 3, 21, 21, 2 };
#define RENDER_PM_CORE &nes_desc, "nes"                         // the core of the picture-mode pages (pm_page_fixtures)
#elif RENDER_PART == 3
// T164 WP16: the Picture page of a core that is not the A1200, on a screen that names one PC mode (a monitor): the list is Auto and every row of the table (T235; the PC mode is
// listed at 60 Hz only, the screen names 1024 x 768), Scale and Smoothing are there for every frame-buffer core. The page as the monitor gets it in 1024 x 768.
static void nes_picture_pc(const char *out)
{
    char p[512];
    {
        static CoreDesc nes_ext;
        PictureEnv pc = { (uint16_t)(1u << 10), 0, true, VID_SYS_PAL50 };
        nes_ext = *board_desc(&nes_desc, &pc);
        int pi = picture_index(&nes_ext), si = scale_index(&nes_ext), mi = smooth_index(&nes_ext);
        CHECK(pi >= 0 && si >= 0 && mi >= 0);
        CHECK(nes_ext.settings[pi].nvalues == 10 && !strcmp(nes_ext.settings[pi].values[pic_value50(VID_HDMI_1024x768)], "1024 x 768 (60 Hz)"));
        Rig n(&nes_ext, "/usb0/cores/nes.cfg");
        n.c.started = true;
        n.c.program_loaded = true;
        ui_open(&nes_ext);
        picture_start(&n.c, VID_SYS_PAL50, VID_HDMI_1080P);
        n.vid->init();
        n.st->set(pi, pic_value50(VID_HDMI_1024x768));
        n.st->set(si, VID_SCALE_FIT);
        n.st->set(mi, 1);
        n.m->open();
        n.m->key(KEY_F5);
        n.m->key('2');
        ui_menu_render(*n.m);
        lv_timer_handler();
        bool seen_pic = false, seen_scale = false, seen_smooth = false;
        for (int i = 0; i < n.m->rows(); i++) {
            char l2[120], rt2[120];
            bool a2;
            n.m->row(i, l2, sizeof(l2), rt2, sizeof(rt2), &a2);
            if (!strcmp(l2, "Picture")) { seen_pic = strstr(rt2, "1024 x 768") != 0; }
            if (!strcmp(l2, "Scale")) { seen_scale = strstr(rt2, "Fit") != 0; }
            if (!strcmp(l2, "Smoothing")) { seen_smooth = strstr(rt2, "On") != 0; }
        }
        CHECK(seen_pic && seen_scale && seen_smooth);
        CHECK(!n.px.place.empty());
        PxRec::Pl g = n.px.place.empty() ? PxRec::Pl{ 32, 114, 960, 540 } : n.px.place.back();
        snprintf(p, sizeof(p), "%s/nes-picture-page-pc.png", out);
        png_out(p, 1024, 768, g.x, g.y, g.w, g.h);
    }
}

// T164 D10c amended (Alex 2026-11-04): the Picture row of a core says which mode "Auto" is and whose it is ("Auto (1280 x 720 from host)" with the host's stored mode,
// "Auto (W x H from screen)" without one). The row has to fit at every mode the host offers: the value is never cut to dots (view_bounds names the text as a key).
static void modes_picture_row(const char *out)
{
    char p[512];
    static CoreDesc nes_ext;
    PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50 };
    nes_ext = *board_desc(&nes_desc, &env);
    int pi = picture_index(&nes_ext);
    CHECK(pi >= 0);
    struct { const char *name; int sys, hdmi, fw, fh; } modes[] = {
        { "1080p", VID_SYS_PAL50, VID_HDMI_1080P, 1920, 1080 },
        { "720p", VID_SYS_PAL50, VID_HDMI_720P, 1280, 720 },
        { "800x600", VID_SYS_PAL50, VID_HDMI_800x600, 800, 600 },
        { "1024x768", VID_SYS_PAL50, VID_HDMI_1024x768, 1024, 768 },
        { "576p", VID_SYS_PAL50, VID_HDMI_480_576, 720, 576 },
        { "480p", VID_SYS_NTSC60, VID_HDMI_480_576, 640, 480 },
    };
    for (auto &md : modes) {
        for (int host = 1; host >= 0; host--) {
            Rig r(&nes_ext, "/usb0/cores/nes.cfg");
            if (host) {
                r.fs.put_text("/Flash/trx/host.cfg", std::string("[Video]\nMode=") + video_hdmi_name(md.hdmi) + "\nSystem=PAL 50 Hz\n");
            }
            r.c.started = true;
            r.c.program_loaded = true;
            ui_open(&nes_ext);
            picture_start(&r.c, md.sys, md.hdmi);
            r.vid->init();
            TrxCanvas want = trx_canvas_for(md.fw, md.fh);
            CHECK(trx_canvas_w() == want.w && trx_canvas_h() == want.h);
            r.m->open();
            r.m->key(KEY_F5);
            r.m->key('2');
            ui_menu_render(*r.m);
            lv_timer_handler();
            char expect[64];
            snprintf(expect, sizeof(expect), "Auto (%s from %s)", host ? video_hdmi_name(md.hdmi) : "", host ? "host" : "screen");
            bool seen = false;
            for (int i = 0; i < r.m->rows(); i++) {
                char l[120], rt[120];
                bool a;
                r.m->row(i, l, sizeof(l), rt, sizeof(rt), &a);
                if (strcmp(l, "Picture")) continue;
                seen = true;
                std::string rts = rt;                                                    // D5: the closed row ends in " >" (it opens the page of the modes)
                CHECK(rts.size() > 2 && rts.compare(rts.size() - 2, 2, " >") == 0);
                if (rts.size() > 2) { rts.resize(rts.size() - 2); }
                bool ok = host ? rts == expect : (!strncmp(rts.c_str(), "Auto (", 6) && rts.size() >= 13 && !strcmp(rts.c_str() + rts.size() - 13, " from screen)"));
                if (!ok) printf("PICROW %s %s: \"%s\", wanted \"%s\"\n", md.name, host ? "host" : "screen", rt, host ? expect : "Auto (... from screen)");
                CHECK(ok);                                                              // the D10c check: the row says where Auto comes from
            }
            CHECK(seen);
            const char *keys[2] = { nes_ext.settings[pi].values[0], 0 };
            char what[64];
            snprintf(what, sizeof(what), "%s picture row (%s)", md.name, host ? "host" : "screen");
            CHECK(view_bounds(what, want.w, want.h, 0, keys) == 0);                     // and it is whole: not cut to dots, inside its row
            if (host) {                                                                  // the second page of a pass is drawn by area (the frame holds the changed part only): one picture per mode
                PxRec::Pl g = r.px.place.back();
                snprintf(p, sizeof(p), "%s/modes-%s-picture-row.png", out, md.name);
                png_out(p, md.fw, md.fh, g.x, g.y, g.w, g.h);
            }
        }
    }
    ui_set_canvas(960, 540);
}

static int render_core_first(const char *out, int argc, char **argv)   // before the shared pages; a return of 0 or more ends main() with it
{
    char p[512];
    static CoreDesc cut_base = nes_desc;                            // a core with a record's pages: golden/desc_cut.toml (argv[2], compiled by run.sh) over the NES
    cut_base.needs_program = 0;
    g_cut = new RecordMenu(&cut_base, argc > 2 ? argv[2] : "desc_cut.bin");
    // T239.3 base 3.3: a core with a record's pages. The record is tests/coreapp/golden/desc_cut.toml over the NES (the Timewarp and the Monitor page of Home); the Monitor page draws the text grid its node names
    {
        trx_skin_set(TRX_SKIN_PANEL);
        Rig c2(&g_cut->desc, "/usb0/cores/cut.cfg", &g_cut->mm);
        c2.c.started = true;
        ui_open(&g_cut->desc);
        c2.m->open();
        // T239.3 base 3.3: the Monitor page draws the text grid its node names (model_grid.h stands in for the library): the rows and their cursor, redrawn when sig changes and not on a pass that
        // changes nothing; without the grid the screen is empty
        mg_install();
        g_mg.src = { "TRX MONITOR", "> r", "PC=C000 A=00" };
        g_mg.attrs = { TG_STATUS, TG_PROMPT, TG_REGS };
        g_mg.cx = 3; g_mg.cy = 1;
        CHECK(c2.m->goto_spec("F1/monitor"));
        ui_menu_render(*c2.m);
        lv_timer_handler();
        CHECK(find_label(lv_screen_active(), "TRX MONITOR") != 0 && find_label(lv_screen_active(), "> r") != 0 && find_label(lv_screen_active(), "PC=C000") != 0);
        CHECK(g_mg.n_render == 1);
        CHECK(g_mg.n_line == 3);                                // every row read once for the first draw
        snprintf(p, sizeof(p), "%s/monitor-grid.png", out);
        png(p, 2);
        ui_menu_render(*c2.m);                                    // a pass that changed nothing: render() asks the grid, sig is the same, no row is read again
        lv_timer_handler();
        CHECK(g_mg.n_render == 2);
        CHECK(g_mg.n_line == 3);
        g_mg.src[1] = "> d C000";
        ui_menu_render(*c2.m);                                    // the machine changed what it shows: sig changed, the rows are read and the label is written
        lv_timer_handler();
        CHECK(g_mg.n_render == 3);
        CHECK(g_mg.n_line == 6);
        CHECK(find_label(lv_screen_active(), "> d C000") != 0 && find_label(lv_screen_active(), "> r") == 0);
        g_mg.src.pop_back();                                     // a shorter grid: the row that is gone is blank
        ui_menu_render(*c2.m);
        lv_timer_handler();
        CHECK(find_label(lv_screen_active(), "PC=C000") == 0 && find_label(lv_screen_active(), "> d C000") != 0);
        mg_uninstall();                                          // no grid found: an empty screen, nothing of the old rows
        ui_menu_render(*c2.m);
        lv_timer_handler();
        CHECK(find_label(lv_screen_active(), "> d C000") == 0 && find_label(lv_screen_active(), "TRX MONITOR") == 0);
    }
    nes_picture_pc(out);
    modes_picture_row(out);
    return -1;
}
#define RENDER_CORE_FIRST render_core_first
#endif
