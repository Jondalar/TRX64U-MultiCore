// T239 D17: the DDT's cases of the coreapp render bench, moved out of tests/coreapp/render/render.cc (2026-11-27). Its TOML B says
// render = true, so tests/coreapp/render/run.sh builds it as a render build of its own (RENDER_CORE=ddt_novic, step 9 of its lane) and passes this file as RENDER_CORE_FILE;
// render.cc includes it three times: RENDER_PART 1 its headers, 2 the floors of its walks and the rows it adds to the shared walks, 3 its cases and the calls of main().
#if RENDER_PART == 1
#elif RENDER_PART == 2
static const Floors FLOOR = { 12, 252, 0, 0, 0, 0, 5, 18, 15, 18, 90, 0, 0, 0, 0 };
#elif RENDER_PART == 3
// T168: the menu of the two DDT cores (one task, no settings, no program): the Actions page shows Reset, the pictures go to the output directory
static void ddt_menu(const char *out)
{
    char p[512];
    const struct { const CoreDesc *d; const char *name; } cores[] = { { &ddt_novic_desc, "ddt_novic" } };
    for (const auto &k : cores) {
        CoreDesc *kb = board_desc(k.d);
        Rig r(kb, "/usb0/cores/ddt.cfg");
        r.c.started = true;
        r.c.program_loaded = true;
        ui_open(kb);
        r.m->open();
        r.m->key(KEY_F1);
        r.m->key('1');
        ui_menu_render(*r.m);
        lv_timer_handler();
        char l[100], rt[100]; bool a;
        r.m->row(0, l, 100, rt, 100, &a);
        CHECK(!strcmp(l, "Reset"));
        snprintf(p, sizeof(p), "%s/%s-actions.png", out, k.name);
        png(p, 2);
        r.m->key(KEY_F1);
        r.m->key('1');
        ui_menu_render(*r.m);
        lv_timer_handler();
        snprintf(p, sizeof(p), "%s/%s-home.png", out, k.name);
        png(p, 2);
    }
}

static int render_core_first(const char *out, int argc, char **argv)   // before the shared pages; a return of 0 or more ends main() with it
{
    char p[512];
    ddt_menu(out);
    return -1;
}
#define RENDER_CORE_FIRST render_core_first
#endif
