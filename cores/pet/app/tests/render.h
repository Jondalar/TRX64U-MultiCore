// T239 D17: the PET's cases of the coreapp render bench, moved out of tests/coreapp/render/render.cc (2026-11-27). Its TOML B says
// render = true, so tests/coreapp/render/run.sh builds it as a render build of its own (RENDER_CORE=pet, step 9 of its lane) and passes this file as RENDER_CORE_FILE;
// render.cc includes it three times: RENDER_PART 1 its headers, 2 the floors of its walks and the rows it adds to the shared walks, 3 its cases and the calls of main().
#if RENDER_PART == 1
#elif RENDER_PART == 2
static const Floors FLOOR = { 18, 426, 0, 252, 63, 1, 11, 53, 21, 27, 135, 3, 18, 18, 2 };
#elif RENDER_PART == 3
#endif
