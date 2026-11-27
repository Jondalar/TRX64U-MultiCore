// T239 D17: the Atari 800 XL's cases of the coreapp render bench, moved out of tests/coreapp/render/render.cc (2026-11-27). Its TOML B says
// render = true, so tests/coreapp/render/run.sh builds it as a render build of its own (RENDER_CORE=atari800, step 9 of its lane) and passes this file as RENDER_CORE_FILE;
// render.cc includes it three times: RENDER_PART 1 its headers, 2 the floors of its walks and the rows it adds to the shared walks, 3 its cases and the calls of main().
#if RENDER_PART == 1
#elif RENDER_PART == 2
static const Floors FLOOR = { 15, 327, 0, 114, 57, 1, 12, 21, 18, 24, 120, 3, 21, 21, 2 };
#define HKL_PIC(s) ((s) == &atari800_desc)             // its learned keys are pictured (hotkey_learn)
// its pages of the UX shots (ux_shots, the rows of jobs[])
#define RENDER_UX_JOBS \
        { &atari800_desc, "atari", { { "F5-core-top", KEY_F5, 0, 0 } }, 1 },
#elif RENDER_PART == 3
static void areas_core(void)
{
    trx_skin_set(TRX_SKIN_PANEL);
    CoreDesc *atari_b = board_desc(&atari800_desc);
    Rig c(atari_b, "/usb0/cores/atari800.cfg");
    c.fs.put_size("/usb0/roms/atari800/atarixl.rom", 16384);
    c.fs.put_size("/usb0/roms/atari800/basic.rom", 8192);
    for (int i = 0; i < 60; i++) {
        char pn[64];
        snprintf(pn, sizeof(pn), "/usb0/zgame%02d.atr", i);        // the Library opens on a medium: the files are on the stick
        c.fs.put_text(pn, "x");
    }
    c.fs.put_text("/usb0/sub/inner.atr", "x");
    for (int i = 0; i < 40; i++) {
        char pn[64];
        snprintf(pn, sizeof(pn), "/usb0/sub2/f%02d.atr", i);
        c.fs.put_text(pn, "x");
    }
    c.c.started = true;
    c.c.program_loaded = true;
    ui_open(atari_b);

    const TrxTheme &t = trx_theme();
    const uint32_t SCREEN = 960 * 540;
    const uint32_t PANE = (960 - 148) * (540 - 36 - 28 - 38);        // the content pane under a tab strip
    const uint32_t RW = (960 - 148) - 2 * t.space_lg;                // a row's width
    // The selection of a row is a 3 px bar and a hairline around it; a selection move redraws the old and the new one and the value's colour
    const uint32_t MARK1 = 2 * RW + 4 * 20 + 2000, MARK2 = 2 * RW + 4 * 34 + 2000;
    // ONE frame buffer: the sync copy of two buffers in DIRECT mode costs as much again as the render, on the board
    {
        lv_display_t *d = lv_display_get_default();
        printf("AREA frame buffers: %d\n", d && d->buf_2 ? 2 : 1);
        CHECK(d && d->buf_1 && !d->buf_2);
    }
    // menu open as run.cc does it: the layer resumes (the screen is invalidated), the message is cleared, the menu opens and renders once
    uint32_t fr0 = trx_shell_frames();
    lv_obj_invalidate(lv_screen_active());
    ui_message_clear();
    ui_menu_visible(true);
    c.m->open();
    ui_menu_render(*c.m);
    uint32_t px = inv_pixels();
    g_last_areas = inv_areas_n();
    uint32_t open_frames = trx_shell_frames() - fr0;
    lv_timer_handler();
    printf("AREA menu open built %u frame(s)\n", (unsigned)open_frames);
    CHECK(open_frames == 1);                                         // one frame built, one pass
    area_case("menu open (one full screen)", px, SCREEN, &c);
    CHECK(big_empty_boxes(lv_screen_active()) == 0);                // Home: no empty picture box in the session panel
    // Home: the status rows; a selection move
    area_case("Home: selection move", key_px(c, KEY_DOWN), 2 * (2 * 250 + 4 * 30 + 2000), &c);
    // an area key: the frame stands except the rail's two items and the tab strip; the pane is the new content
    uint32_t px_f3 = key_px(c, KEY_F3);
    CHECK(g_key_frames == 0 && g_key_partials == 1);                 // not a frame again: the rail's two items and the strip
    area_case("F3 library (first key of an area)", px_f3, PANE + 40000, &c);
    c.m->relist();
    ui_menu_render(*c.m); lv_timer_handler();
    area_case("Library: selection move", key_px(c, KEY_DOWN), 2 * MARK1, &c);
    for (int i = 0; i < 25; i++) key_px(c, KEY_DOWN);
    area_case("Library: scroll one row", key_px(c, KEY_DOWN), 60000, &c);
    // the top bar's media chip changes when a stick mounts (relist): the bar alone, not the screen
    {
        c.host.media_gen++;
        uint32_t f0 = trx_shell_frames();
        ui_menu_render(*c.m);
        CHECK(trx_shell_frames() == f0);                            // the top bar only
        uint32_t pxm = inv_pixels();
        g_last_areas = inv_areas_n();
        lv_timer_handler();
        area_case("relist (media chip, top bar)", pxm, 960 * 36 + PANE / 2, &c);
    }
    area_case("F5 machine", key_px(c, KEY_F5), PANE + 45000, &c);              // 396,219 px measured 2026-10-29 (the budget was PANE + 40000 before the button keycaps and the new settings rows)
    area_case("tab 2 settings", key_px(c, '2'), PANE + 15000, &c);
    area_case("Settings: selection move", key_px(c, KEY_DOWN), 2 * MARK2, &c);
    area_case("Settings: value change (first of a session)", key_px(c, KEY_RIGHT), 30000, &c);
    area_case("Settings: value change again", key_px(c, KEY_LEFT), 30000, &c);
    area_case("tab 1 machine", key_px(c, '1'), PANE + 15000, &c);
    area_case("F7 actions", key_px(c, KEY_F7), SCREEN, &c);                    // T212 D12: APP has no tab strip, so the pane's height changes and the whole screen is built (518,400 px measured 2026-11-12)
    area_case("F8 system", key_px(c, KEY_F8), SCREEN, &c);                     // T212 D12: back from the strip-less APP area: the pane changes height again
    area_case("F1 home", key_px(c, KEY_F1), PANE + 40000, &c);
    // the browser: into a folder and back
    c.m->key(KEY_F3);
    ui_menu_render(*c.m); lv_timer_handler();
    c.m->relist();
    ui_menu_render(*c.m); lv_timer_handler();
    {
        int usb = find_dir_row(c, "usb0");                          // the media level: the stick is a row
        CHECK(usb >= 0);
        select_row(c, usb);
        c.m->key(KEY_RETURN);
        ui_menu_render(*c.m); lv_timer_handler();
        c.m->relist();
        ui_menu_render(*c.m); lv_timer_handler();
    }
    {
        int dir = find_dir_row(c, "sub");
        printf("AREA library: folder row %d of %d\n", dir, c.m->rows());
        CHECK(dir >= 0);
        select_row(c, dir);
    }
    area_case("Library: RETURN on a folder", key_px(c, KEY_RETURN), PANE + 10000, &c);       // 362,326 px measured 2026-11-12 (T209: the crumb, the count and the side panel change with the folder)
    printf("AREA library title after RETURN: %s (%d rows)\n", c.m->title(), c.m->rows());
    area_case("Library: LEFT (parent folder)", key_px(c, KEY_LEFT), PANE + 10000, &c);      // 360,739 px measured 2026-11-12
    // a folder with as many rows as the window holds: the same pane, the rows are written again (title and labels), not built
    {
        int dir = find_dir_row(c, "sub2");
        CHECK(dir >= 0);
        select_row(c, dir);
        area_case("Library: RETURN on a full folder", key_px(c, KEY_RETURN), 120000, &c);
        area_case("Library: LEFT from a full folder", key_px(c, KEY_LEFT), 120000, &c);
    }
}

// T262 P9: a key that runs an operation and changes a value on a screen redraws that row, not the content area. The area LVGL holds after the key is the time (about 2.2 ms for every
// 1,000 pixels on the board): the whole pane is ~350,000 pixels, a quarter of a second in which the content stands blank and the next keys wait; a row is ~35,000.
static const char *g_t262_val = "One";
static int t262_cycle(CoreCtx *, const OpArgs &, OpOut &) { g_t262_val = !strcmp(g_t262_val, "One") ? "Two" : "One"; return OPE_OK; }
static const char *t262_state(CoreCtx *) { return g_t262_val; }
static void t262_rows(const char *out)
{
    static const LibTabDecl tabs[] = { { "F5", "form", "Form", TW_NODES, 0, 0 } };
    static const char *const looks[] = { "split;title=Form;side=Side", 0 };
    static const LibNodeDecl nodes[] = {
        { "F5/form", LN_OP, "Category", "t262.cycle", 0, 1, 0, 0, 0, 0, 0, false, "chip=@t262.val" },
        { "F5/form", LN_OP, "Name", "t262.cycle", 0, 2, 0, 0, 0, 0, 0, false, "chip=@t262.val" },
        { "F5/form", LN_OP, "Group", "t262.cycle", 0, 3, 0, 0, 0, 0, 0, false, "chip=@t262.val" },
        { "F5/form", LN_OP, "Search", "t262.cycle", 0, 4 },
    };
    LibRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.struct_size = sizeof(rec);
    rec.tabs = tabs; rec.ntabs = 1;
    rec.nodes = nodes; rec.nnodes = 4;
    rec.tab_layouts = looks;
    Contributor cs[] = { { "t262", CR_LIB, &rec } };
    CoreDesc *base = board_desc(&atari800_desc);
    Rig c(base, "/usb0/cores/t262.cfg");
    c.c.started = true;
    OpRegistry *reg = ops_of(&c.c);
    reg->contributor("t262");
    reg->add_op("t262.cycle", 0, 0, t262_cycle);
    reg->add_state("t262.val", t262_state);
    static CoreDesc merged;
    static MenuMerge mm;
    CHECK(contrib_merge(base, 0, 0, cs, 1, reg, &merged, &mm) == 0);
    c.c.desc = &merged; c.c.merge = &mm;
    ui_open(&merged);
    c.m->rebuild();
    c.m->open();
    CHECK(c.m->goto_spec("F5/form"));
    CHECK(c.m->screen());
    ui_menu_render(*c.m);
    lv_timer_handler();
    char l[100], r[100]; bool a;
    c.m->row(0, l, 100, r, 100, &a);
    CHECK(!strcmp(l, "Category"));
    CHECK(find_label(lv_screen_active(), "One") != 0);
    char p[512];
    snprintf(p, sizeof(p), "%s/t262-before.png", out);
    png(p, 2);
    const uint32_t ip0 = screens_inplace_writes(), cheap0 = screens_cheap_moves(), fr0 = trx_shell_frames();
    c.m->key(KEY_RETURN);                                                   // the operation answers "Two": the chip of every row says it
    ui_menu_render(*c.m);
    const uint32_t px = inv_pixels();
    lv_timer_handler();
    CHECK(find_label(lv_screen_active(), "Two") != 0 && find_label(lv_screen_active(), "One") == 0);
    CHECK(screens_inplace_writes() == ip0 + 1);                            // the words were written in place
    CHECK(trx_shell_frames() == fr0);
    printf("AREA t262 RETURN on an op row: %u px (the pane is ~350,000)\n", (unsigned)px);
    CHECK(px > 0 && px < 100000);
    snprintf(p, sizeof(p), "%s/t262-after.png", out);
    png(p, 2);
    c.m->key(KEY_DOWN);                                                     // the move after it is the cheap path as ever
    ui_menu_render(*c.m);
    lv_timer_handler();
    CHECK(screens_cheap_moves() == cheap0 + 1);
    c.m->key(KEY_RETURN);
    ui_menu_render(*c.m);
    const uint32_t px2 = inv_pixels();
    lv_timer_handler();
    CHECK(find_label(lv_screen_active(), "One") != 0);
    CHECK(px2 > 0 && px2 < 100000);
}

// T262 D4 (10): a form that scrolled keeps no empty space below its last row when rows hide. The selection stands on the button of the bar (not a row of the list), so the window
// has no row to follow: it must still be filled from the end.
static const char *g_t262_hide = 0;
static int t262_noop(CoreCtx *, const OpArgs &, OpOut &) { return OPE_OK; }
static const char *t262_hide_state(CoreCtx *) { return g_t262_hide; }
static void t262_form_scroll(const char *out)
{
    static const LibTabDecl tabs[] = { { "F5", "form", "Form", TW_NODES, 0, 0 } };
    static const char *const looks[] = { "split;title=Form;side=Side", 0 };
    static char labels[30][16];
    static LibNodeDecl nodes[31];
    for (int i = 0; i < 30; i++) {
        snprintf(labels[i], sizeof(labels[i]), "Field %d", i);
        nodes[i] = { "F5/form", LN_OP, labels[i], "t262.noop", 0, (int16_t)(i + 1), 0, 0, 0, 0, 0, false, i >= 18 ? "hide=@t262.hide" : 0 };
    }
    nodes[30] = { "F5/form/bar", LN_OP, "Go", "t262.noop", 0, 100, 0, 0, 0, 0, 0, false, "primary" };
    LibRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.struct_size = sizeof(rec);
    rec.tabs = tabs; rec.ntabs = 1;
    rec.nodes = nodes; rec.nnodes = 31;
    rec.tab_layouts = looks;
    Contributor cs[] = { { "t262f", CR_LIB, &rec } };
    CoreDesc *base = board_desc(&atari800_desc);
    Rig c(base, "/usb0/cores/t262f.cfg");
    c.c.started = true;
    OpRegistry *reg = ops_of(&c.c);
    reg->contributor("t262f");
    reg->add_op("t262.noop", 0, 0, t262_noop);
    reg->add_state("t262.hide", t262_hide_state);
    static CoreDesc merged;
    static MenuMerge mm;
    g_t262_hide = 0;
    CHECK(contrib_merge(base, 0, 0, cs, 1, reg, &merged, &mm) == 0);
    c.c.desc = &merged; c.c.merge = &mm;
    ui_open(&merged);
    c.m->rebuild();
    c.m->open();
    CHECK(c.m->goto_spec("F5/form"));
    ui_menu_render(*c.m);
    lv_timer_handler();
    auto shown = [&]() { int k = 0; char nm[16]; for (int i = 0; i < 30; i++) { snprintf(nm, sizeof(nm), "Field %d", i); k += find_label(lv_screen_active(), nm) != 0; } return k; };
    const int full = shown();                                              // the rows one window holds
    CHECK(full > 4 && full < 18);
    for (int i = 0; i < 20; i++) {                                         // down the rows: the window scrolls
        c.m->key(KEY_DOWN);
        ui_menu_render(*c.m);
    }
    CHECK(find_label(lv_screen_active(), "Field 0") == 0);                 // the first rows are above the window now
    c.m->key(KEY_END);                                                     // the bar's button: no row of the list holds the selection
    ui_menu_render(*c.m);
    g_t262_hide = "1";                                                     // rows 18 .. 29 hide: 18 rows stay, more than one window holds, the window began below row 8
    ui_menu_render(*c.m);
    lv_timer_handler();
    CHECK(shown() >= full);                                               // the window is full to its last row, no empty space under "Field 17"
    CHECK(find_label(lv_screen_active(), "Field 17") != 0);
    char p[512];
    snprintf(p, sizeof(p), "%s/t262-form-scroll.png", out);
    png(p, 2);
    printf("PASS t262 form scroll: rows hid under a scrolled window, the window is full to the last row\n");
}

// T262 D5 finding 16: the Library's path header is one line. A long folder name is cut in the middle (UX R5), it does not wrap into the first row (R2).
static void t262_path_header(const char *out)
{
    static const struct { const char *name; int fw, fh; } cvs[] = { { "480p", 640, 480 }, { "720p", 1280, 720 }, { "1080p", 1920, 1080 } };
    for (const auto &cv : cvs) {
        TrxCanvas want = trx_canvas_for(cv.fw, cv.fh);
        ui_set_canvas(want.w, want.h);
        CoreDesc *base = board_desc(&atari800_desc);
        Rig c(base, "/usb0/cores/t262p.cfg");
        c.fs.put_text("/usb0/ONLINE/ULTIMATE HIRES IMAGE PACK 384X272 [ULTIMATE]/pic1.atr", "x");
        c.fs.put_text("/usb0/ONLINE/ULTIMATE HIRES IMAGE PACK 384X272 [ULTIMATE]/pic2.atr", "x");
        ui_open(base);
        c.m->open();
        c.m->key(KEY_F3);
        ui_menu_render(*c.m); lv_timer_handler();
        c.m->relist();
        ui_menu_render(*c.m); lv_timer_handler();
        const char *steps[] = { "usb0", "ONLINE", "ULTIMATE HIRES IMAGE PACK 384X272 [ULTIMATE]" };
        for (const char *st : steps) {
            int r = find_dir_row(c, st);
            CHECK(r >= 0);
            if (r < 0) { break; }
            select_row(c, r);
            c.m->key(KEY_RETURN);
            ui_menu_render(*c.m); lv_timer_handler();
            c.m->relist();
            ui_menu_render(*c.m); lv_timer_handler();
        }
        char tag[64];
        snprintf(tag, sizeof(tag), "t262 path header %s", cv.name);
        lv_obj_t *scr = lv_screen_active();
        lv_obj_update_layout(scr);
        lv_obj_t *hd = find_label(scr, "/usb0");
        lv_obj_t *r0 = find_label(scr, "..");
        CHECK(strstr(c.m->title(), "ULTIMATE HIRES IMAGE PACK 384X272 [ULTIMATE]") != 0);
        CHECK(hd && r0);
        if (hd && r0) {
            lv_area_t ah, ar;
            lv_obj_get_coords(hd, &ah);
            lv_obj_get_coords(r0, &ar);
            const int lh = lv_font_get_line_height(trx_theme().display);
            const bool one_line = (ah.y2 - ah.y1 + 1) <= lh + 2;
            const bool clear = ah.y2 < ar.y1 || ah.x2 < ar.x1 || ar.x2 < ah.x1;
            printf("%s: header \"%s\" [%d,%d - %d,%d] first row [%d,%d - %d,%d] line %d px\n", tag, lv_label_get_text(hd), ah.x1, ah.y1, ah.x2, ah.y2, ar.x1, ar.y1, ar.x2, ar.y2, lh);
            CHECK(one_line);
            CHECK(clear);
            CHECK(strchr(lv_label_get_text(hd), '\n') == 0);
            if (one_line && clear) { printf("PASS t262 path header %s: one line, clear of the first row\n", cv.name); }
        }
        CHECK(txt_overlaps(tag) == 0);
        char fn[512];
        snprintf(fn, sizeof(fn), "%s/t262-path-%s.png", out, cv.name);
        lv_obj_invalidate(scr);
        lv_timer_handler();
        if (want.w == 960) { png(fn, 2); } else { png_out(fn, want.w, want.h, 0, 0, want.w, want.h); }
    }
    ui_set_canvas(960, 540);
}

// T262 P1: the page of a `list` node draws what its rows carry: one line per row (name, the right column in the row's tone), the selected row's side lines in the side panel
// (not in the foot line). At 480p, 720p and 1080p: the tone is drawn, the panel holds the lines, no text lies on another, the selection stays in view.
static std::string g_t262_list;
static const char *t262_list_state(CoreCtx *) { return g_t262_list.empty() ? 0 : g_t262_list.c_str(); }
static int t262_list_take(CoreCtx *, const OpArgs &, OpOut &) { return OPE_OK; }
static const ArgSpec t262_take_args[] = { ARG_INT("index", 0, 1000000), ARG_STRING("answer", 64) };
static void t262_list(const char *out)
{
    static const struct { const char *name; int fw, fh; } cvs[] = { { "480p", 640, 480 }, { "720p", 1280, 720 }, { "1080p", 1920, 1080 } };
    g_t262_list.clear();
    static const char *const tn[] = { "muted", "cyan", "violet", "amber", "coral" };
    for (int i = 0; i < 40; i++) {
        g_t262_list += "id" + std::to_string(i) + "\tResult " + std::to_string(i) + "\t" + std::to_string(i * 3) + " KB\t" + tn[i % 5] + "\tside one of " + std::to_string(i) + "\tside two of " + std::to_string(i) + "\tside three of " + std::to_string(i) + "\n";
    }
    for (const auto &cv : cvs) {
        TrxCanvas want = trx_canvas_for(cv.fw, cv.fh);
        ui_set_canvas(want.w, want.h);
        static const LibNodeDecl nodes[] = { { "F5/settings", LN_LIST, "Results", "t262.take", "t262.rows", 1, 0, 0, 0, 0, 0, false, 0 } };
        LibRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.struct_size = sizeof(rec);
        rec.nodes = nodes; rec.nnodes = 1;
        Contributor cs[] = { { "t262l", CR_LIB, &rec } };
        CoreDesc *base = board_desc(&atari800_desc);
        Rig c(base, "/usb0/cores/t262l.cfg");
        c.c.started = true;
        OpRegistry *reg = ops_of(&c.c);
        reg->contributor("t262l");
        reg->add_op("t262.take", t262_take_args, 2, t262_list_take);
        reg->add_state("t262.rows", t262_list_state);
        static CoreDesc merged;
        static MenuMerge mm;
        CHECK(contrib_merge(base, 0, 0, cs, 1, reg, &merged, &mm) == 0);
        c.c.desc = &merged; c.c.merge = &mm;
        ui_open(&merged);
        c.m->rebuild();
        c.m->open();
        CHECK(c.m->goto_spec("F5/settings"));
        int row = -1;
        for (int i = 0; i < c.m->rows(); i++) {
            char l[100], r[100]; bool a;
            c.m->row(i, l, 100, r, 100, &a);
            if (!strcmp(l, "Results")) { row = i; }
        }
        CHECK(row >= 0);
        while (c.m->selected() < row) { c.m->key(KEY_DOWN); }
        c.m->key(KEY_RETURN);
        CHECK(c.m->list_view());
        ui_menu_render(*c.m);
        lv_timer_handler();
        char tag[64];
        snprintf(tag, sizeof(tag), "t262 list %s", cv.name);
        const TrxTheme &t = trx_theme();
        // the tone is drawn: the right column of row 3 (amber) and row 4 (coral) stand in their tones, row 0 (muted) in the muted one
        lv_obj_t *l3 = find_label(lv_screen_active(), "9 KB"), *l4 = find_label(lv_screen_active(), "12 KB");
        CHECK(l3 && l4);
        if (l3 && l4) {
            CHECK(lv_color_to_u32(lv_obj_get_style_text_color(l3, LV_PART_MAIN)) % 0x1000000 == trx_tone_color(TT_AMBER));
            CHECK(lv_color_to_u32(lv_obj_get_style_text_color(l4, LV_PART_MAIN)) % 0x1000000 == trx_tone_color(TT_CORAL));
        }
        // the side panel holds the selected row's lines (row 0), right of the rows, and the foot line does not say them
        lv_obj_t *s1 = find_label(lv_screen_active(), "side one of 0"), *s3 = find_label(lv_screen_active(), "side three of 0");
        lv_obj_t *r0 = find_label(lv_screen_active(), "Result 0"), *sk = find_label(lv_screen_active(), "SELECTED");
        CHECK(s1 && s3 && r0 && sk);
        if (s1 && s3 && r0) {
            lv_area_t as, ar;
            lv_obj_get_coords(s1, &as);
            lv_obj_get_coords(r0, &ar);
            CHECK(as.x1 > ar.x2);
            lv_area_t a3;
            lv_obj_get_coords(s3, &a3);
            CHECK(a3.y2 <= (int)trx_canvas_h());
        }
        int sx1, sy1, sx2, sy2; char st[200];
        if (ui_status_probe(&sx1, &sy1, &sx2, &sy2, st, sizeof(st))) { CHECK(!strstr(st, "side one")); }
        CHECK(txt_overlaps(tag) == 0);
        char fn[512];
        snprintf(fn, sizeof(fn), "%s/t262-list-%s.png", out, cv.name);
        lv_obj_invalidate(lv_screen_active());
        lv_timer_handler();
        if (want.w == 960) { png(fn, 2); } else { png_out(fn, want.w, want.h, 0, 0, want.w, want.h); }
        // T262 D4 (1): a key on the page writes it in place: a move redraws two rows and the panel, a RETURN the status line, not the page (~365,000 px a build)
        {
            const uint32_t b0 = ui_listv_builds(), ip0 = ui_inplace_moves();
            c.m->key(KEY_DOWN);
            ui_menu_render(*c.m);
            const uint32_t px = inv_pixels();
            lv_timer_handler();
            CHECK(ui_listv_builds() == b0 && ui_inplace_moves() == ip0 + 1);
            CHECK(find_label(lv_screen_active(), "side one of 1") != 0 && find_label(lv_screen_active(), "side one of 0") == 0);
            CHECK(px > 0 && px < 150000);                                      // two rows and the panel, a third of the ~365,000 of a build
            c.m->key(KEY_RETURN);                                              // the library works on the row: the status line says so, the page stands
            ui_menu_render(*c.m);
            const uint32_t px2 = inv_pixels();
            lv_timer_handler();
            CHECK(ui_listv_builds() == b0 && ui_inplace_moves() == ip0 + 2);
            CHECK(px2 < 100000);
            printf("AREA t262 list %s: DOWN %u px, RETURN %u px\n", cv.name, (unsigned)px, (unsigned)px2);
            c.m->key(KEY_UP);
            ui_menu_render(*c.m);
            lv_timer_handler();
            CHECK(ui_listv_builds() == b0);
        }
        // the last row selected: the window moved, the row stands inside the card and the panel shows its lines
        c.m->key(KEY_END);
        ui_menu_render(*c.m);
        lv_timer_handler();
        lv_obj_t *e = find_label(lv_screen_active(), "Result 39");
        CHECK(e != 0);
        CHECK(find_label(lv_screen_active(), "Result 0") == 0);
        CHECK(find_label(lv_screen_active(), "side one of 39") != 0);
        if (e) {
            lv_area_t ae;
            lv_obj_get_coords(e, &ae);
            CHECK(ae.y1 >= 0 && ae.y2 <= (int)trx_canvas_h() - 20);
        }
        CHECK(txt_overlaps(tag) == 0);
        snprintf(fn, sizeof(fn), "%s/t262-list-%s-last.png", out, cv.name);
        lv_obj_invalidate(lv_screen_active());
        lv_timer_handler();
        if (want.w == 960) { png(fn, 2); } else { png_out(fn, want.w, want.h, 0, 0, want.w, want.h); }
        printf("PASS t262 list page %s: tone drawn, side lines in the panel, selection in view\n", cv.name);
    }
    ui_set_canvas(960, 540);
}

// T262 D4 (4): the idle Search tab with a library as a source: the row shows the text the form holds, and the empty tab's side panel does not say there are no media.
static void t262_search_tab(const char *out)
{
    g_t262_list.clear();
    static const LibNodeDecl nodes[] = { { "F3/search", LN_LIST, "Online results", "t262.take", "t262.rows", 1, 0, 0, 0, 0, 0, false, "search=Online;find=t262.take;fields=Group" } };
    LibRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.struct_size = sizeof(rec);
    rec.nodes = nodes; rec.nnodes = 1;
    Contributor cs[] = { { "t262s", CR_LIB, &rec } };
    static const SkelTab f3tabs[] = { { "browse", "Browse", 1, TW_BROWSER, 0, "library" }, { "search", "Search", 2, TW_BROWSER, 0, "library" } };
    static const SkelArea f3areas[] = { { "F3", "Library", false, f3tabs, 2 } };
    static const Skeleton f3sk = { f3areas, 1 };
    CoreDesc *base = board_desc(&atari800_desc);
    Rig c(base, "/usb0/cores/t262s.cfg");
    c.c.started = true;
    OpRegistry *reg = ops_of(&c.c);
    reg->contributor("t262s");
    reg->add_op("t262.take", t262_take_args, 2, t262_list_take);
    reg->add_state("t262.rows", t262_list_state);
    static CoreDesc merged;
    static MenuMerge mm;
    CHECK(contrib_merge(base, &f3sk, 0, cs, 1, reg, &merged, &mm) == 0);
    c.c.desc = &merged; c.c.merge = &mm;
    ui_open(&merged);
    c.m->rebuild();
    c.m->open();
    CHECK(c.m->goto_spec("F3/search"));
    CHECK(c.m->search_tab() && c.m->search_source_count() == 1);
    ui_menu_render(*c.m);
    lv_timer_handler();
    CHECK(find_label(lv_screen_active(), "No media found.") == 0);
    CHECK(find_label(lv_screen_active(), "This device  -  Name contains:  press RETURN to type") != 0);
    c.m->key('/');                                                          // the form: type a name in its second row
    c.m->key(KEY_DOWN);
    c.m->key(KEY_RETURN);
    c.m->key('a'); c.m->key('b');
    c.m->key(KEY_RETURN);
    c.m->key(KEY_ESCAPE);                                                   // leave the form
    CHECK(!c.m->picking());
    ui_menu_render(*c.m);
    lv_timer_handler();
    CHECK(find_label(lv_screen_active(), "This device  -  Name contains:  ab") != 0);
    CHECK(find_label(lv_screen_active(), "No media found.") == 0);
    char p[512];
    snprintf(p, sizeof(p), "%s/t262-search-tab.png", out);
    png(p, 2);
    printf("PASS t262 search tab: the form's text stands in the row, the panel does not claim there are no media\n");
}

static int render_core_first(const char *out, int argc, char **argv)   // before the shared pages; a return of 0 or more ends main() with it
{
    char p[512];
    if (getenv("T262_ONLY")) {                                      // T262 P9: only the area of a key that runs an operation
        t262_rows(out);
        t262_form_scroll(out);
        t262_list(out);
        t262_search_tab(out);
        t262_path_header(out);
        printf("coreapp render (T262_ONLY): %d checks, %d failed\n", checks, fails);
        return fails ? 1 : 0;
    }
    CoreDesc *atari_main = board_desc(&atari800_desc);
    Rig r(atari_main, "/usb0/cores/atari800.cfg");
    r.fs.put_size("/usb0/roms/atari800/atarixl.rom", 16384);
    r.fs.put_size("/usb0/roms/atari800/basic.rom", 8192);
    r.fs.put_text("/usb0/games/arkanoid.atr", "x");
    r.fs.put_text("/usb0/games/boulderdash.atr", "x");
    r.fs.put_text("/usb0/games/notes.txt", "x");
    r.c.started = true;
    r.c.program_loaded = true;

    ui_open(atari_main);
    {   // T209 (UX review): the middle cut keeps a visible start and end at every real width, never a bare "..."
        const char *names[] = { "Turrican_II_The_Final_Fight_Director_Cut_Disk_1_of_3_Fixed.d64", "Zak_McKracken_and_the_Alien_Mindbenders_Disk_1_Side_A.d64",
                                "Last_Ninja_Remix_(1990)(System_3)[cr Fairlight][t +3].d64", "Amiga 1200 (AGA, 2 MB chip, 8 MB fast)" };
        const TrxTheme &th = trx_theme();
        for (int ni = 0; ni < 4; ni++) {
            for (int w = 120; w <= 520; w += 8) {
                char nb[64];
                const char *o = trx_fit_mid(names[ni], w, th.body, nb, sizeof(nb));
                CHECK(strcmp(o, "...") != 0);
                CHECK(trx_text_w(o, th.body) <= w);
            }
        }
        for (int ni = 0; ni < 2; ni++) {                       // the row as the Library builds it: the label the user sees holds the cut name, whole, inside the row
            char nb[64], title[100];
            const int rw = 504;
            snprintf(title, sizeof(title), "%s", trx_fit_mid(names[ni], rw - 32 - trx_text_w("171 KB", th.caption) - th.space_md - 8, th.body, nb, sizeof(nb)));
            TrxStatusRow sr = { title, "171 KB", TT_VIOLET, false, false };
            lv_obj_t *row = trx_status_row(lv_screen_active(), 0, 0, rw, sr, 0);
            lv_obj_update_layout(row);
            for (uint32_t ci = 0; ci < lv_obj_get_child_count(row); ci++) {
                lv_obj_t *ch = lv_obj_get_child(row, (int32_t)ci);
                if (!lv_obj_check_type(ch, &lv_label_class) || !strcmp(lv_label_get_text(ch), "171 KB")) continue;
                printf("LIBROW title '%s' label text '%s' w %d box %d\n", title, lv_label_get_text(ch), trx_text_w(lv_label_get_text(ch), th.body), (int)lv_obj_get_width(ch));
                CHECK(strcmp(lv_label_get_text(ch), title) == 0);
                CHECK(trx_text_w(lv_label_get_text(ch), th.body) <= lv_obj_get_width(ch));
                CHECK(strncmp(lv_label_get_text(ch), "...", 3) != 0);
            }
            lv_obj_delete(row);
        }
    }
    {   // T101 S9: the splash the core app puts up first: the whole splash once, then only the bar and its label per step, then a message
        // (a ROM file missing) takes it away
        static uint16_t prev[960 * 540];
        ui_splash_open("Atari 800", "85acf2b  2026-10-12", 4);
        unsigned first = 0;
        for (size_t k = 0; k < 960 * 540; k++) first += prev[k] != fb[k];
        CHECK(first > 20000);
        memcpy(prev, fb, sizeof(prev));
        const char *labels[4] = { "Reading the settings", "Setting the picture", "Starting the libraries", "ROM files, then the core" };
        for (int i = 0; i < 4; i++) {
            ui_splash_step(i, labels[i]);
            unsigned d = 0;
            for (size_t k = 0; k < 960 * 540; k++) d += prev[k] != fb[k];
            printf("SPLASH step %d: %u px changed\n", i, d);
            CHECK(d > 0 && d < 12000);
            memcpy(prev, fb, sizeof(prev));
            snprintf(p, sizeof(p), "%s/atari-splash_%d.png", out, i);
            png(p, 2);
        }
        ui_message("ROM files", "atarixl.rom is missing", true);
        lv_timer_handler();
        CHECK(!trx_splash_up());
        snprintf(p, sizeof(p), "%s/atari-splash_rom.png", out);
        png(p, 2);
        ui_message_clear();
        lv_timer_handler();
    }
    r.m->open();

    struct { const char *name; int area_key; int tab; } shots[] = {
        { "home",     KEY_F1, 1 },
        { "library",  KEY_F3, 1 },
        { "machine",  KEY_F5, 1 },
        { "settings", KEY_F5, 2 },
        { "actions",  KEY_F7, 1 },
        { "system",   KEY_F8, 1 },
    };
    for (unsigned i = 0; i < sizeof(shots) / sizeof(shots[0]); i++) {
        r.m->key(shots[i].area_key);
        r.m->key('0' + shots[i].tab);
        ui_menu_render(*r.m);
        lv_timer_handler();
        snprintf(p, sizeof(p), "%s/atari-%s.png", out, shots[i].name);
        png(p, 2);
    }

    {   // T167 S3: a question card (veil, card) with a bar below it, and the bar alone, over the system screen: a tick of the bar changes the bar's card and
        // nothing else; no state between two draw tasks holds a pixel of what lies behind the cards
        static HostOs dos;
        AppDlg dlg(&dos, &r.fs);
        for (int variant = 0; variant < 2; variant++) {
            std::atomic<bool> done(false);
            std::thread worker;
            if (variant == 1) {
                worker = std::thread([&] { dlg.message("Unzip", "Not enough memory."); done = true; });
                for (int i = 0; i < 3000 && !dlg.open(); i++) {
                    dlg.service();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                CHECK(dlg.open());
            }
            void *h = dlg.progress_open(variant ? "Building the image" : "Writing the set", 100);
            dlg.service();
            ui_dialog_render(dlg);
            lv_timer_handler();
            snprintf(p, sizeof(p), "%s/atari-dialog-bar%d_0.png", out, variant);
            png(p, 2);
            int by = variant ? 540 - 28 - 74 - 6 : 540 - 28 - 74 - 24;
            int n = 0, bad = 0, states = 0, visits = 0, ebad = 0;
            ovp::run_reset();
            for (int i = 1; i <= 14; i++) {
                char text[32];
                snprintf(text, sizeof(text), "file %d of 14", i);
                dlg.progress_set(h, 7, text);
                dlg.service();
                ovp::begin(fb, 200, by, 560, 74);
                ui_dialog_render(dlg);
                lv_timer_handler();
                int st, vi;
                bad += ovp::end(&st, &vi);
                states += st; visits += vi; n++;
                if (variant) {                                                // the veil and the question's card stand: the band above the bar does not change at all
                    ovp::begin(fb, 0, 150, 960, 250);
                    dlg.progress_set(h, 0, "again");
                    dlg.service();
                    ui_dialog_render(dlg);
                    lv_timer_handler();
                    ebad += ovp::end(&st, &vi, 8);
                }
            }
            snprintf(p, sizeof(p), "%s/atari-dialog-bar%d_1.png", out, variant);
            png(p, 2);
            {   // the worst moment of the run, as the person saw it
                ovp::Pic saved = ovp::paste_worst(fb);
                snprintf(p, sizeof(p), "%s/overlay_dialog%d_worst.png", out, variant);
                png(p, 2);
                ovp::paste_back(fb, saved);
            }
            printf("overlay dialog %d: %d ticks, %d states, %d visits, %d foreign pixels, %d changed outside the bar\n", variant, n, states, visits, bad, ebad);
            CHECK(n == 14 && visits >= n && states >= n);
            OVCHECK(bad == 0);
            OVCHECK(ebad == 0);
            dlg.progress_close(h);
            dlg.service();
            if (variant == 1) {
                dlg.take_key(KEY_RETURN);
                worker.join();
            }
            dlg.service();
            ui_dialog_render(dlg);
            lv_timer_handler();
        }
    }

    {   // T260 fix (Rick and Alex, 2026-11-27: about 2 s per key in the diag core's A/V panel, an edit list over the veil): the card of an edit list stays.
        // RIGHT on a row changes the pixels of that row only, DOWN those of two rows, END moves the 14-row window (rows, values that come and go, the
        // "a-b of n" label) - all served in place (ui_dialog_inplace), and the picture equals a card built afresh for the same state. A frame rebuild
        // between two keys (a new canvas: the menu cleans the screen, the overlay container with it) builds the card again and writes no old row.
        static HostOs dos;
        AppDlg dlg(&dos, &r.fs);
        static DlgRow erows[24];
        for (int k = 0; k < 24; k++) {
            snprintf(erows[k].label, sizeof(erows[k].label), "knob_%02d", k);
            snprintf(erows[k].value, sizeof(erows[k].value), "%d", 100 + k);
            erows[k].action = k >= 22;                                    // Load preset, Save preset: no value
        }
        void (*cb)(void *, int, int) = [](void *, int row, int key) {
            int v = atoi(erows[row].value) + (key == DLG_EDIT_LEFT ? -1 : 1);
            snprintf(erows[row].value, sizeof(erows[row].value), "%d", v);
        };
        std::atomic<bool> done(false);
        int ans = -9;
        std::thread worker;
        auto open_at = [&](int sel) {
            done = false;
            worker = std::thread([&, sel] { ans = dlg.edit_list("A/V out", "LEFT / RIGHT change a row", erows, 24, sel, cb, nullptr); done = true; });
            for (int i = 0; i < 3000 && !dlg.open(); i++) {
                dlg.service();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            CHECK(dlg.open());
            dlg.service();
            ui_dialog_render(dlg);
            lv_timer_handler();
        };
        auto close = [&] {
            dlg.take_key(KEY_ESCAPE);
            for (int i = 0; i < 3000 && !done; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            worker.join();
            CHECK(ans == -1);
            dlg.service();
            ui_dialog_render(dlg);
            lv_timer_handler();
        };
        static uint16_t before[960 * 540], kept[960 * 540];
        auto changed = [&](const uint16_t *ref, int *y0, int *y1) {      // the pixels that differ from `ref`, and the lines they lie between
            size_t n = 0;
            *y0 = 540;
            *y1 = -1;
            for (int y = 0; y < 540; y++) {
                for (int x = 0; x < 960; x++) {
                    if (fb[y * 960 + x] != ref[y * 960 + x]) {
                        n++;
                        *y0 = y < *y0 ? y : *y0;
                        *y1 = y > *y1 ? y : *y1;
                    }
                }
            }
            return n;
        };
        auto press = [&](int key) {
            memcpy(before, fb, sizeof(before));
            dlg.take_key(key);
            dlg.service();
            ui_dialog_render(dlg);
            lv_timer_handler();
        };
        open_at(0);
        snprintf(p, sizeof(p), "%s/edit-list_0.png", out);
        png(p, 2);
        const int rh = trx_row_h() + 1;
        const uint32_t in0 = ui_dialog_inplace();
        int y0, y1;
        press(KEY_RIGHT);
        CHECK(dlg.view().row_val[0] && !strcmp(dlg.view().row_val[0], "101"));
        const size_t n_right = changed(before, &y0, &y1);
        CHECK(n_right > 0 && y1 - y0 < rh);                               // one row
        press(KEY_DOWN);
        const size_t n_down = changed(before, &y0, &y1);
        CHECK(n_down > 0 && y1 - y0 < 2 * rh);                            // the row left and the row selected
        press(KEY_END);                                                    // the window (14 rows of 24) moves to the last rows: action rows 22, 23 come in
        CHECK(dlg.view().sel == 23);
#ifndef UI_DIALOG_REBUILD                                                 // T167's control builds the card on every revision
        CHECK(ui_dialog_inplace() == in0 + 3);
#endif
        const int first = dlg.view().first;
        memcpy(kept, fb, sizeof(kept));
        snprintf(p, sizeof(p), "%s/edit-list_1.png", out);
        png(p, 2);
        close();
        open_at(23);                                                       // the same state, its card built afresh
        CHECK(dlg.view().first == first);
        const size_t n_fresh = changed(kept, &y0, &y1);
        CHECK(n_fresh == 0);                                               // the rows written in place draw the picture a fresh card draws
        const uint32_t in1 = ui_dialog_inplace();
        ui_set_canvas(720, 480);
        ui_set_canvas(960, 540);                                           // the next menu render builds the frame again (lv_obj_clean of the screen)
        dlg.take_key(KEY_UP);
        dlg.service();
        ui_menu_render(*r.m);                                              // as run.cc: the menu first, then the question
        ui_dialog_render(dlg);
        lv_timer_handler();
        CHECK(dlg.view().sel == 22);
        CHECK(ui_dialog_inplace() == in1);                                 // the card was built again: no row of the deleted card was written
        printf("edit list: %d of 24 rows shown, in place %u, RIGHT %zu px, DOWN %zu px, fresh card differs in %zu px\n", dlg.view().rows,
               (unsigned)(ui_dialog_inplace() - in0), n_right, n_down, n_fresh);
        close();
    }

    {   // T167 S4: the menu's own job display (a new image written in steps: "Building the image", NN %). The row is in the pane, not an overlay, but it is redrawn the
        // same way: every tick changes its text, and no state between two draw tasks may hold a pixel that is not the pane's own (the rows of the old and the new picture,
        // the pane colour). The probe watches the rows of the pane.
        Rig j(board_desc(&atari800_desc), "/usb0/cores/atari800j.cfg");
        j.fs.put_text("/usb0/games/x.atr", "x");
        j.c.started = true;
        j.c.program_loaded = true;
        auto pick = [&](const char *label) {
            char l[100], rt[100], sb[100];
            bool act;
            for (int i = 0; i < j.m->rows(); i++) {
                j.m->row(i, l, sizeof(l), rt, sizeof(rt), &act, sb, sizeof(sb));
                if (!strcmp(l, label)) {
                    while (j.m->selected() > i) j.m->key(KEY_UP);
                    while (j.m->selected() < i) j.m->key(KEY_DOWN);
                    return true;
                }
            }
            return false;
        };
        j.m->open();
        j.m->key(KEY_F1);                                                       // T212 D5: the task belongs to the drive: F1, the D1: row, RETURN opens its list
        bool ok = pick("D1:");
        CHECK(ok);
        if (ok) { j.m->key(KEY_RETURN); ok = pick("Create blank ATR"); CHECK(ok); }
        if (ok) {
            j.m->key(KEY_RETURN);
            for (int i = 0; i < 8 && strcmp(j.m->title(), "/") != 0; i++) j.m->key(KEY_LEFT);
            ok = pick("usb0/");
            if (ok) { j.m->key(KEY_RETURN); ok = pick("games/"); }
            if (ok) { j.m->key(KEY_RETURN); }
            CHECK(ok);
        }
        if (ok) {
            j.m->key(KEY_RETURN);                                               // "Create here"
            for (int i = 0; i < 30; i++) j.m->key(KEY_BACK);
            for (const char *c = "prog"; *c; c++) j.m->key(*c);
            j.m->key(KEY_RETURN);
            CHECK(j.m->busy());
            ui_menu_render(*j.m);
            lv_timer_handler();
            lv_area_t jr = { 190, 76, 949, 225 };                               // where the job row stands is found, not assumed: the row object of the "Building the image" label
            { lv_obj_t *scr = lv_screen_active(); lv_obj_update_layout(scr); lv_obj_t *lb = find_label(scr, "Building"); CHECK(lb != 0); if (lb) { lv_obj_get_coords(lv_obj_get_parent(lb), &jr); } }
            snprintf(p, sizeof(p), "%s/atari-job_0.png", out);
            png(p, 2);
            int n = 0, bad = 0, states = 0, visits = 0;
            for (int i = 0; i < 400 && j.m->busy(); i++) {
                j.m->tick();
                if (!j.m->busy()) { break; }                                   // the tick that ends the job swaps the job row for the page again: a rebuild of the screen, not a refresh of the row
                ovp::begin(fb, jr.x1, jr.y1, jr.x2 - jr.x1 + 1, jr.y2 - jr.y1 + 1);
                ui_menu_render(*j.m);
                lv_timer_handler();
                int st, vi;
                bad += ovp::end(&st, &vi, 6);
                states += st; visits += vi; n++;
            }
            snprintf(p, sizeof(p), "%s/atari-job_1.png", out);
            png(p, 2);
            printf("overlay job: %d ticks, %d states, %d visits, %d foreign pixels\n", n, states, visits, bad);
            CHECK(n >= 5 && visits >= n && states >= n);
            OVCHECK(bad == 0);
        }
    }

    {   // T160 (board 2026-10-31): the volume keys' line is on screen on every page, not only on F5 / Settings. A core without a Volume of its own gets the generic one;
        // per page: the pane without the line, then the key and the line (the chip in the top bar must change pixels, the rows none), then the line run out (the pane as before).
        Rig v(board_desc(&atari800_desc), "/usb0/cores/atari800v.cfg");
        v.c.started = true;
        v.m->open();
        const int pages[] = { KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F7 };
        static uint16_t before[960 * 540];
        for (int pk : pages) {
            v.m->key(pk);
            v.io.t += 5000;
            v.m->toast_expire();
            ui_menu_render(*v.m); ui_toast_render(*v.m); lv_timer_handler();
            memcpy(before, fb, sizeof(before));
            v.m->key('{');
            CHECK(v.m->toast_text()[0] != 0);
            ui_menu_render(*v.m); ui_toast_render(*v.m); lv_timer_handler();
            size_t shown = 0, card = 0;                                        // T187 D4/S2: the message is a chip in the top bar (y < 36); where the card used to stand nothing changes
            for (int y = 0; y < 34; y++) for (int x = 150; x < 960; x++) shown += fb[y * 960 + x] != before[y * 960 + x];
            if (pk != KEY_F5) {                                                // (F5 may list the Volume row, whose value changes with the key)
                int bx0 = 999, bx1 = -1, by0 = 999, by1 = -1;
                for (int y = 450; y < 490; y++) for (int x = 200; x < 760; x++) {
                    if (fb[y * 960 + x] != before[y * 960 + x]) { card++; bx0 = x < bx0 ? x : bx0; bx1 = x > bx1 ? x : bx1; by0 = y < by0 ? y : by0; by1 = y > by1 ? y : by1; }
                }
                if (card) { printf("CARDPIX F%d: %zu px in [%d,%d - %d,%d] tab '%s' title '%s' status '%s'\n", pk - KEY_F1 + 1, card, bx0, by0, bx1, by1, v.m->tab_name(), v.m->title(), v.m->status()); }
            }
            snprintf(p, sizeof(p), "%s/volume_toast_F%d.png", out, pk - KEY_F1 + 1);
            png(p, 2);
            CHECK(shown > 100);                                              // the chip with its text, whatever the page behind it
            CHECK(card == 0);                                                // and no card over the rows
            v.io.t += 5000;
            CHECK(v.m->toast_expire());
            ui_menu_render(*v.m); ui_toast_render(*v.m); lv_timer_handler();
        }
    }

    {   // T181, T187 D2: the hotkey items (modifier and one key per action) are 7 rows of the Hotkeys page (opened from F5 / Input, tab 4) of a core with a default map.
        // The same core with and without them: the opened page differs by the rows, F8 System is the same pixels (it lists none of them).
        static CoreDesc hd;
        static Setting hs[SETTINGS_MAX];
        CHECK(!hk_extend(&atari800_desc, &hd, hs));
        CHECK(hd.nsettings == atari800_desc.nsettings + 1 + HK_NACT - 1);
        Rig base(&atari800_desc, "/usb0/cores/atari800hb.cfg");
        Rig hot(&hd, "/usb0/cores/atari800hk.cfg");
        base.c.started = true; hot.c.started = true;
        base.m->open(); hot.m->open();
        static uint16_t without[960 * 540];
        // item 6: the same core, the page closed (its F5 Input tab, the row "Hotkeys >") against the page open
        auto differ = [&](int page, int tab) {
            hot.m->open(); ui_menu_forget_rows();
            int pa = -1, pp = -1;                                              // the tab that holds the page, found through the tree (T205: not tab 4 of F5 any more)
            for (int a = 0; a < hot.m->tree().nareas && pa < 0 && tab; a++) {
                for (int tt = 0; tt < hot.m->tree().a[a].ntabs; tt++) {
                    const MenuTab &tb = hot.m->tree().a[a].tabs[tt];
                    if (tb.page && tb.count > 0 && hot.m->tree().e[tb.first].kind == EK_SETTING && !strcasecmp(hot.c.desc->settings[hot.m->tree().e[tb.first].index].key, HK_KEY_MOD)) { pa = a; pp = tb.parent; break; }
                }
            }
            if (tab && pa >= 0) { page = FKEYS[hot.m->tree().a[pa].key[1] - '1']; tab = hot.m->tree().a[pa].tabs[pp].n; }
            hot.m->key(page); if (tab) hot.m->key('0' + tab);
            ui_menu_render(*hot.m); lv_timer_handler();
            memcpy(without, fb, sizeof(without));
            if (tab) {                                                         // the Hotkeys row opens its page (found by name: T205 put other rows above it)
                for (int i = 0; i < hot.m->rows(); i++) {
                    char l[200], v[200]; bool a;
                    hot.m->row(i, l, sizeof(l), v, sizeof(v), &a);
                    if (!strcmp(l, "Hotkeys")) { select_row(hot, i); break; }
                }
                hot.m->key(KEY_RETURN);
            }
            ui_menu_render(*hot.m); lv_timer_handler();
            size_t diff = 0;
            for (int y = 36; y < 540 - 28; y++) for (int x = 148; x < 960; x++) diff += fb[y * 960 + x] != without[y * 960 + x];
            return diff;
        };
        auto differ8 = [&]() {                                               // F8 System of the core without the items against the one with them: the same pixels
            base.m->key(KEY_F8);
            ui_menu_render(*base.m); lv_timer_handler();
            memcpy(without, fb, sizeof(without));
            hot.m->key(KEY_F8);
            ui_menu_render(*hot.m); lv_timer_handler();
            size_t diff = 0;
            for (int y = 36; y < 540 - 28; y++) for (int x = 148; x < 960; x++) diff += fb[y * 960 + x] != without[y * 960 + x];
            return diff;
        };
        size_t f8 = differ8();
        size_t f5 = differ(KEY_F5, 4);
        printf("hotkey rows: F5 Hotkeys page %zu pixels differ, F8 System %zu\n", f5, f8);
        snprintf(p, sizeof(p), "%s/hotkeys_F5_page.png", out);
        png(p, 2);
        CHECK(f5 > 3000);                                                    // 7 rows of about 42 px at 812 px wide, text only
        CHECK(f8 == 0);
        {   // T207 S4: RETURN on the Reset row asks for the key (entry_ is up, the row says what it waits for); ESC takes it back
            memcpy(without, fb, sizeof(without));
            for (int i = 0; i < hot.m->rows(); i++) {                          // the Reset row by its name: the page's first row is not the same since T205
                char l[200], v[200]; bool a;
                hot.m->row(i, l, sizeof(l), v, sizeof(v), &a);
                if (!hot.m->heading(i) && strstr(l, "Reset")) { select_row(hot, i); break; }
            }
            ui_menu_render(*hot.m); lv_timer_handler();
            memcpy(without, fb, sizeof(without));
            hot.m->key(KEY_RETURN);
            CHECK(hot.m->learning());
            ui_menu_render(*hot.m); lv_timer_handler();
            size_t d = 0;
            for (int y = 36; y < 540 - 28; y++) for (int x = 148; x < 960; x++) d += fb[y * 960 + x] != without[y * 960 + x];
            printf("hotkey learn prompt: %zu pixels differ from the page\n", d);
            CHECK(d > 500);
            snprintf(p, sizeof(p), "%s/hotkeys_F6_learn.png", out);
            png(p, 2);
            hot.m->key(KEY_ESCAPE);
            CHECK(!hot.m->learning());
        }
    }

    // the message view a core that cannot start shows
    ui_message("This core cannot be started", "The bundle is \"vc20\" but the core in the FPGA is \"atari800\". The core stays held.", true);
    lv_timer_handler();
    snprintf(p, sizeof(p), "%s/atari-held.png", out);
    png(p, 2);

    // T104, board 2026-10-16: a key that only moves the selection must restyle two rows, not rebuild the
    // pane. Measured here, because "feels slow" is not a number: the frame must not be built again, the
    // cheap path must have counted the move, and the area LVGL is asked to redraw must stay near two
    // rows instead of the whole pane.
    r.m->key(KEY_F5);                                        // the core's settings tab: rows, so DOWN has somewhere to go
    r.m->key('1');
    ui_menu_render(*r.m);
    lv_timer_handler();
    uint32_t frames0 = trx_shell_frames(), cheap0 = ui_cheap_moves();
    r.m->key(KEY_DOWN);
    ui_menu_render(*r.m);
    uint32_t moved_px = inv_pixels();
    lv_timer_handler();
    CHECK(ui_cheap_moves() == cheap0 + 1);                   // the move went through the cheap path
    CHECK(trx_shell_frames() == frames0);                    // and the frame was not built again
    // two rows of about 36 x 864 px are ~62,000; the whole pane is ~356,000. The bound sits between
    // them, well clear of both, so it catches a pane rebuild and not a row that grew a line.
    CHECK(moved_px > 0 && moved_px < 100000);
    printf("one key moved %u pixels of the pane\n", (unsigned)moved_px);
    // the control for the bound: the first render of another tab does rebuild, and that is a big area
    r.m->key(KEY_F7);
    r.m->key('1');
    ui_menu_render(*r.m);
    uint32_t rebuilt_px = inv_pixels();
    lv_timer_handler();
    CHECK(rebuilt_px > moved_px * 4);
    // a changed row is not a selection move: the pane is built again
    r.m->key(KEY_F5);
    r.m->key('1');
    ui_menu_render(*r.m);
    lv_timer_handler();
    uint32_t cheap1 = ui_cheap_moves();
    r.m->key(KEY_RIGHT);                                     // a setting's value changes: new text in a row
    ui_menu_render(*r.m);
    lv_timer_handler();
    CHECK(ui_cheap_moves() == cheap1);                       // nothing cheap about a row whose text moved


    // T104 (board 2026-10-22: "scrolling is ultra slow"): walking down a long list. Inside the window a step is a selection
    // move (above); a step past its last row used to rebuild the whole pane, every step from there on. Now the rows that exist
    // are written again, and only the labels whose text moved repaint. Counted per step, and the picture must be the one a
    // build from scratch makes.
    for (int i = 0; i < 60; i++) {
        snprintf(p, sizeof(p), "/zgame%02d.atr", i);
        r.fs.put_text(p, "x");
    }
    r.m->key(KEY_F3);
    r.m->key('1');
    r.m->relist();
    ui_menu_render(*r.m);
    lv_timer_handler();
    {
        uint32_t fr0 = trx_shell_frames(), ch0 = ui_cheap_moves(), ip0 = ui_inplace_moves(), total = 0, worst = 0, worst_after_edge = 0;
        int steps = 0;
        for (int i = 0; i < 40; i++) {
            r.m->key(KEY_DOWN);
            ui_menu_render(*r.m);
            uint32_t px_n = inv_pixels();
            lv_timer_handler();
            total += px_n;
            worst = px_n > worst ? px_n : worst;
            if (i >= 19 && px_n > worst_after_edge) {            // step 17 leaves the first window: its ".." row has no value, the next rows have one
                worst_after_edge = px_n;
            }
            steps++;
        }
        printf("scroll: %d steps, %u px in all, worst step %u px, worst after the first window shift %u px, %u cheap, %u in place, "
               "%u frames rebuilt\n", steps, (unsigned)total, (unsigned)worst, (unsigned)worst_after_edge, (unsigned)(ui_cheap_moves() - ch0),
               (unsigned)(ui_inplace_moves() - ip0), (unsigned)(trx_shell_frames() - fr0));
        CHECK(trx_shell_frames() == fr0);                    // the frame never moves while the list scrolls
        CHECK(ui_inplace_moves() - ip0 >= 20);               // the steps past the window's edge were written in place
        CHECK(worst_after_edge < 100000);                    // a step costs a few thousand label pixels, not the 356,000 of the pane
        CHECK(total / steps < 100000);
        // the same state built from scratch must look the same, pixel for pixel
        static uint16_t kept[960 * 540];
        memcpy(kept, fb, sizeof(kept));
        ui_menu_forget_rows();
        ui_menu_render(*r.m);
        lv_timer_handler();
        CHECK(memcmp(kept, fb, sizeof(kept)) == 0);
        // and a row whose value changed (a setting) is written in place as well, the same picture again
        r.m->key(KEY_F5);
        r.m->key('1');
        ui_menu_render(*r.m);
        lv_timer_handler();
        uint32_t ip1 = ui_inplace_moves();
        r.m->key(KEY_RIGHT);
        ui_menu_render(*r.m);
        uint32_t edit_px = inv_pixels();
        lv_timer_handler();
        CHECK(ui_inplace_moves() == ip1 + 1);
        CHECK(edit_px > 0 && edit_px < 100000);
        memcpy(kept, fb, sizeof(kept));
        ui_menu_forget_rows();
        ui_menu_render(*r.m);
        lv_timer_handler();
        CHECK(memcmp(kept, fb, sizeof(kept)) == 0);
        printf("one value change: %u px\n", (unsigned)edit_px);
    }

    // the terminal skin, same code
    trx_skin_set(TRX_SKIN_TERMINAL);
    r.m->key(KEY_F5);
    r.m->key('1');
    ui_menu_render(*r.m);
    lv_timer_handler();
    snprintf(p, sizeof(p), "%s/atari-settings-terminal.png", out);
    png(p, 2);

    // T104.2 D6: the address prompt of a file action that asks, on the screen. The core is the Atari with a .prg of a test's own whose second action asks for the start address in a row of its
    // own (the operations are named but not called here; cores/atari800/app/tests/test_prompt.cc runs them). The same ui.cc draws it; what is checked is that the prompt reaches the screen as the row the menu model gives.
    {
        trx_skin_set(TRX_SKIN_PANEL);
        static const OpBind run_bind[] = { B_FILE("path") };
        static const OpBind load_bind[] = { B_FILE("path"), B_ASK("start") };
        static const FileAction prg_actions[] = { { "Run PRG", "prompt.run", run_bind, 1, true, 0, 0 }, { "Load PRG and start from", "prompt.load", load_bind, 2, true, "Start address", 0 } };
        static const FileType prompt_types[] = { { ".prg", prg_actions, 2, 0, 0, 0 } };
        static CoreDesc pd = atari800_desc;
        pd.filetypes = prompt_types;
        pd.nfiletypes = 1;
        CoreDesc *pdd = board_desc(&pd);
        Rig c(pdd, "/usb0/cores/atari800_prompt.cfg");
        c.fs.put("/usb0/x.prg", std::vector<uint8_t>{ 0x00, 0xC0, 0x60, 0x60 });
        c.c.started = true;
        ui_open(pdd);
        c.m->open();
        c.m->key(KEY_F3);                                        // the media level: the stick is /usb0
        c.m->relist();
        int usb = find_dir_row(c, "usb0");
        CHECK(usb >= 0);
        select_row(c, usb);
        c.m->key(KEY_RETURN);                                    // opens the stick: ../ is first, x.prg after it
        c.m->relist();
        int xr = find_file_row(c, "x.prg");
        CHECK(xr >= 0);
        select_row(c, xr);
        c.m->key(KEY_RETURN);
        CHECK(c.m->chooser());
        ui_menu_render(*c.m);
        lv_timer_handler();
        snprintf(p, sizeof(p), "%s/prompt-chooser.png", out);
        png(p, 2);
        c.m->key(KEY_DOWN);
        c.m->key(KEY_RETURN);
        CHECK(c.m->entering());
        c.m->key('1'); c.m->key('0');
        ui_menu_render(*c.m);
        lv_timer_handler();
        snprintf(p, sizeof(p), "%s/prompt-prompt.png", out);
        png(p, 2);
        char l[100], r[100]; bool a;
        c.m->row(0, l, 100, r, 100, &a);
        CHECK(!strcmp(l, "Start address") && !strcmp(r, "$10_"));
        c.m->key(KEY_ESCAPE);
    }
    return -1;
}
#define RENDER_CORE_FIRST render_core_first

static void render_core_canvas(void)                                   // after main() set the 960 x 540 canvas
{
    areas_core();
}
#define RENDER_CORE_CANVAS render_core_canvas

static void render_core_late(const char *out)                          // after the shared pages
{
    t262_rows(out);
    t262_form_scroll(out);
    t262_list(out);
    t262_search_tab(out);
    t262_path_header(out);
}
#define RENDER_CORE_LATE render_core_late
#endif
