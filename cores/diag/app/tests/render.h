// T239 D17: the diag core's cases of the coreapp render bench, moved out of tests/coreapp/render/render.cc (2026-11-27). Its TOML B says
// render = true, so tests/coreapp/render/run.sh builds it as a render build of its own (RENDER_CORE=diag, step 9 of its lane) and passes this file as RENDER_CORE_FILE;
// render.cc includes it three times: RENDER_PART 1 its headers, 2 the floors of its walks and the rows it adds to the shared walks, 3 its cases and the calls of main().
#if RENDER_PART == 1
// T239.5 D13, T260 P7: the diag core is a build of its own (RENDER_APP_DIAG). The diag app's driver table comes first (render/run.sh: RENDER_OWN_DRVTAB, the table of its own TOML A),
// so the engine model (diag_engine.h: it serves the pin table the live page reads its pins from by name) and the app's headers name the registers through it: the runner
// (diagtest.h) and the panels (panel.h) are linked in this build
#include RENDER_OWN_DRVTAB
#include "diag_engine.h"
#include "diag_examples.h"
#include "diagtest.h"
#include "portdiag.h"
#elif RENDER_PART == 2
static const Floors FLOOR = { 9, 207, 0, 0, 0, 0, 3, 16, 15, 18, 90, 0, 0, 0, 0 };
#elif RENDER_PART == 3
// T175: the diag core's pages - the live port rows (idle page), the browser over the tests folder, a prompt of a test, the result of a run and a panel
static void diag_pages(const char *out, int cw, int ch, const char *tag)
{
    char p[512];
    ui_set_canvas(cw, ch);                                       // 960 x 540 (shown at 2x), or the compact canvas of 576p: 720 x 540 (T164 D10a, T259 D17)
    auto shot = [&](const char *name) {
        snprintf(p, sizeof(p), "%s/diag-%s%s.png", out, name, tag);
        if (cw == 960) { png(p, 2); } else { png_out(p, cw, ch, 0, 0, cw, ch); }
    };
    Rig r(&portdiag_desc, "/usb0/cores/diag/diag.cfg");
    static Engine eng;                                           // T260: the model of engine v2 serves the pin table; the rows read the snapshot by the pins' names
    eng.forced_low = 0;
    eng.cmp_now = 0;
    r.c.io = &eng;
    r.c.started = true;
    r.c.program_loaded = true;
    diag_install_examples(&r.fs, "/usb0/cores/diag");
    ui_open(&portdiag_desc);
    r.m->open();
    r.m->key(KEY_F1);                                            // T205 D1: the port lines (media rows) are on Home
    // idle: nothing pressed - then up and the fire button of port 2 and the first comparator
    ui_menu_render(*r.m);
    lv_timer_handler();
    char l[100], rt[100]; bool a;
    int p2 = -1;
    for (int i = 0; i < r.m->rows() && p2 < 0; i++) { r.m->row(i, l, 100, rt, 100, &a); if (!strcmp(l, "Port 2")) p2 = i; }
    CHECK(p2 >= 0);
    r.m->row(p2, l, 100, rt, 100, &a);
    CHECK(!strcmp(l, "Port 2") && strstr(rt, "F1"));
    shot("idle");
    eng.forced_low = 0x220;                                      // KBJOY_2_JOY0 (up) and KBJOY_2_JOY4 (fire) low
    eng.cmp_now = 1;                                             // PADDLE_CMP0 high
    ui_menu_render(*r.m);
    lv_timer_handler();
    r.m->row(p2, l, 100, rt, 100, &a);
    CHECK(strstr(rt, "U0") && strstr(rt, "F0"));
    r.m->row(p2 + 1, l, 100, rt, 100, &a);
    CHECK(!strcmp(rt, "1  0  0  0"));
    shot("idle-pressed");
    ui_open(&portdiag_desc);
    // the picker: the Library on the folder of the tests
    r.m->key(KEY_F3);
    ui_menu_render(*r.m);
    lv_timer_handler();
    r.m->relist();
    ui_menu_render(*r.m);
    lv_timer_handler();
    {
        static const char *path[4] = { "usb0", "cores", "diag", "tests" };       // the media level, then folder by folder, the way the Library bench goes in
        for (int k = 0; k < 4; k++) {
            int d = find_dir_row(r, path[k]);
            if (d < 0) { printf("  the Library has no folder %s (%s)\n", path[k], tag); }
            CHECK(d >= 0);
            select_row(r, d);
            r.m->key(KEY_RETURN);
            ui_menu_render(*r.m);
            lv_timer_handler();
            r.m->relist();
            ui_menu_render(*r.m);
            lv_timer_handler();
        }
    }
    r.m->row(0, l, 100, rt, 100, &a);
    {                                                            // the picture shows the tests folder: the example files are listed by name, the folder is the tests one
        bool seen[5] = { false, false, false, false, false };
        static const char *want[5] = { "cd32.toml", "joystick.toml", "paddle.toml", "pins.toml", "stick2.toml" };
        for (int i = 0; i < r.m->rows(); i++) {
            char l2[100], rt2[100], sub2[100];
            bool a2;
            r.m->row(i, l2, 100, rt2, 100, &a2, sub2, 100);
            for (int k = 0; k < 5; k++) { seen[k] |= !strncasecmp(l2, want[k], strlen(want[k])); }
        }
        for (int k = 0; k < 5; k++) {
            if (!seen[k]) { printf("  the picker (%s) does not list %s\n", tag, want[k]); }
            CHECK(seen[k]);
        }
        CHECK(r.m->rows() >= 5);
        CHECK(strstr(r.m->title(), "tests") != 0);
    }
    shot("picker");
    // a prompt, a result and a panel: stick2.toml run by the real runner (diag_run_file) against the engine model, panels/avout.toml opened by the real panel code
    // (panel_run); the dialogs on the real card
    {
        static HostOs dos;
        AppDlg dlg(&dos, &r.fs);
        // stick2.toml on a card of its own: the third prompt (button 2) as the app asks it (portdiag_prompt_text, its timeout_s), the result as the runner shows it
        static Engine *reng;
        static uint32_t rms;
        Engine e2;
        FakeFs rfs;
        reng = &e2;
        rms = 0;
        diag_install_examples(&rfs, "/SD/cores/diag");
        struct Cap : DiagUi {
            Engine *e;
            char pt[100], px[400], rt[100], rx[400];
            int pto = -1, seen = 0;
            int prompt(const char *t, const char *x, int idx, int total, int timeout_s) override
            {
                if (idx == 3) { portdiag_prompt_text(t, x, idx, total, pt, sizeof(pt), px, sizeof(px)); pto = timeout_s; }
                e->forced_low = idx == 2 ? 0x200 : 0;           // button 1: KBJOY_2_JOY4 (pin 6) low
                e->cmp_now = idx == 3 ? 4 : 0;                  // button 2: comparator 2
                return DUI_CONTINUE;
            }
            void progress(const char *) override { }
            void show(const char *t, const char *x) override { snprintf(rt, sizeof(rt), "%s", t); snprintf(rx, sizeof(rx), "%s", x); seen++; }
        } cap;
        cap.e = &e2;
        DiagEnv env;
        memset(&env, 0, sizeof(env));
        env.io = &e2;
        env.fs = &rfs;
        env.ui = &cap;
        env.now_ms = [] { return rms; };
        env.delay_ms = [](uint32_t n) { rms += n; reng->settle(); };
        env.stamp = [](char o[16]) { strcpy(o, "20261102-143015"); return true; };
        env.core_version = "0.1.0";
        env.folder = "/SD/cores/diag";
        DiagOutcome oc;
        CHECK(diag_run_file(&env, "/SD/cores/diag/tests/stick2.toml", &oc) == 0);
        CHECK(cap.seen == 1 && cap.pto > 0 && oc.nfail == 0 && oc.npass > 0);
        const char *pt = cap.pt, *px = cap.px, *rt = cap.rt, *rx = cap.rx;
        char qt[96], qx[400];
        portdiag_question_text("A/V output walk", "Does the picture stand still (no rolling, no jumping)?", 3, 36, qt, sizeof(qt), qx, sizeof(qx));
        char ct[96], cx[400];                                  // T201 v2: the colour question is a choice
        portdiag_question_text("A/V output walk", "Which colour is the bar right of the white one?", 3, 36, ct, sizeof(ct), cx, sizeof(cx));
        snprintf(cx, sizeof(cx), "%s", "Which colour is the bar right of the white one?");
        static const char *const cch[] = { "yellow", "blue", "other" };
        int picked = -9;
        // T260 P5: the panel the app opens (CoreDialogs::edit_list): panels/avout.toml from the card, its registers resolved by name as the app does (portdiag_reg_by_name), the rows
        // built by panel_run from the registers (the engine model holds them, at the panel's defaults); RIGHT on the first row steps it as the person does
        static Panel pan;
        static char ptext[8192];
        int pn = r.fs.load("/usb0/cores/diag/panels/avout.toml", ptext, sizeof(ptext) - 1);
        CHECK(pn > 0);
        ptext[pn > 0 ? pn : 0] = 0;
        char perr[160] = "";
        CHECK(panel_parse(ptext, (uint32_t)(pn > 0 ? pn : 0), &pan, portdiag_reg_by_name, perr, sizeof(perr)) == 0);
        if (perr[0]) { printf("  avout.toml (%s): %s\n", tag, perr); }
        panel_defaults(&r.c, pan);
        int p0 = panel_name_idx(pan.row[0], panel_get(&r.c, pan.row[0]));
        CHECK(!pan.row[0].action[0] && pan.row[0].n > 1 && p0 >= 0);
        const char *p0next = pan.row[0].names[(p0 + 1) % pan.row[0].n];
        struct ListDlg : CoreDialogs {                         // the list on the app's dialogs, as run.cc hands them to a core's code
            AppDlg *d;
            bool spawn(void (*)(void *), void *) override { return false; }
            int ask(const char *, const char *, int) override { return -1; }
            int choose(const char *, const char *, const char *const *, int, int) override { return -1; }
            void notice(const char *t, const char *x) override { d->message(t, x); }
            bool pick(const char *, char *, int) override { return false; }
            bool stamp(char[16]) override { return false; }
            int edit_list(const char *t, const char *x, DlgRow *rows, int n, int sel, void (*cb)(void *, int, int), void *ud) override
            {
                return d->edit_list(t, x, rows, n, sel, cb, ud);
            }
        } ldlg;
        ldlg.d = &dlg;
        for (int which = 0; which < 5; which++) {
            std::thread worker([&] {
                if (which == 4) { panel_run(&r.c, &ldlg, pan, "/usb0/cores/diag"); return; }
                if (which == 3) { picked = dlg.choose_timed(ct, cx, cch, 3, 30); return; }
                if (which == 0) { dlg.buttons_timed(pt, px, SVC_BUTTON_YES | SVC_BUTTON_NO, cap.pto); }   // the prompt with stick2's own timeout_s
                else if (which == 2) { dlg.buttons_timed(qt, qx, SVC_BUTTON_YES | SVC_BUTTON_NO, 30); }          // T201: a question of avout.toml (timeout_s = 30)
                else { dlg.message(rt, rx); }
            });
            for (int i = 0; i < 3000 && !dlg.open(); i++) { dlg.service(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            CHECK(dlg.open());
            CHECK(dlg.view().remaining_s == (which == 0 ? cap.pto : which == 2 ? 30 : which == 3 ? 30 : 0));
            ui_dialog_render(dlg);
            lv_timer_handler();
            shot(which == 0 ? "prompt" : which == 2 ? "avquestion" : which == 3 ? "avchoice" : which == 4 ? "panel" : "result");
            if (which == 3) { dlg.take_key(KEY_DOWN); }       // blue
            if (which == 4) {
                CHECK(dlg.view().nitems == pan.nrows);
                dlg.take_key(KEY_RIGHT);
                CHECK(!strcmp(dlg.view().row_val[0], p0next));
            }
            dlg.take_key(which == 0 || which == 2 ? 'y' : which == 4 ? KEY_ESCAPE : KEY_RETURN);
            worker.join();
            dlg.service();
            ui_dialog_render(dlg);
            lv_timer_handler();
        }
        CHECK(picked == 1);
    }
}

static int render_core_first(const char *out, int argc, char **argv)   // before the shared pages; a return of 0 or more ends main() with it
{
    char p[512];
    diag_pages(out, 960, 540, "");
    diag_pages(out, 720, 540, "-576p");
    return -1;
}
#define RENDER_CORE_FIRST render_core_first
#endif
