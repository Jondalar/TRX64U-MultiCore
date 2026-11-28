// T239 D17: the VIC-20's cases of the coreapp render bench, moved out of tests/coreapp/render/render.cc (2026-11-27). Its TOML B says
// render = true, so tests/coreapp/render/run.sh builds it as a render build of its own (RENDER_CORE=vc20, step 9 of its lane) and passes this file as RENDER_CORE_FILE;
// render.cc includes it three times: RENDER_PART 1 its headers, 2 the floors of its walks and the rows it adds to the shared walks, 3 its cases and the calls of main().
#if RENDER_PART == 1
#elif RENDER_PART == 2
static const Floors FLOOR = { 15, 351, 0, 243, 81, 1, 11, 25, 18, 24, 120, 3, 18, 0, 2 };   // measured 2026-11-27 (T239 D17)
#define HKL_PIC(s) ((s) == &vc20_desc)                 // its learned keys are pictured (hotkey_learn)
// its pages of the UX shots (ux_shots, the rows of jobs[])
#define RENDER_UX_JOBS \
        { &vc20_desc, "vc20", { { "F1-home", KEY_F1, 0, 0 }, { "F5-core-top", KEY_F5, 0, 0 }, { "F5-core-end", KEY_F5, 0, 1 }, { "F5-video", KEY_F5, "Video", 0 }, \
                                       { "F5-audio", KEY_F5, "Audio", 0 }, { "F5-input", KEY_F5, "Input", 0 }, { "F8-system", KEY_F8, 0, 0 } }, 7 },
#elif RENDER_PART == 3
#endif
