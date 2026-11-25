// The application's real core table under another name, for tests that need the compiled-in descriptors next to a table of their own (test_descgen: the record's core is
// compared with the compiled-in descriptor of the same core, while the start must not find that one).
#ifdef TEST_APP_VC20
// the VIC-20's app is its own build (run.sh vc20app): its table is its core only
#include "cores.h"
extern const CoreDesc *const compiled_in_table[];   // a const array at namespace scope is internal unless declared extern: test_descgen.cc links it
const CoreDesc *const compiled_in_table[] = { &vc20_desc, 0 };
#elif defined(TEST_APP_A1200)
// the Amiga 1200's app is its own build (run.sh a12app): its table is its core only
#include "cores.h"
extern const CoreDesc *const compiled_in_table[];
const CoreDesc *const compiled_in_table[] = { &amiga1200_desc, 0 };
#elif defined(TEST_APP_ATARI800)
// the Atari 800 XL's app is its own build (run.sh a8app): its table is its core only
#include "cores.h"
extern const CoreDesc *const compiled_in_table[];
const CoreDesc *const compiled_in_table[] = { &atari800_desc, 0 };
#elif defined(TEST_APP_NES)
// the NES's app is its own build (run.sh nesapp): its table is its core only
#include "cores.h"
extern const CoreDesc *const compiled_in_table[];
const CoreDesc *const compiled_in_table[] = { &nes_desc, 0 };
#elif defined(TEST_APP_C264)
// the Commodore 264's app is its own build (run.sh c264app): its table is its core only
#include "cores.h"
extern const CoreDesc *const compiled_in_table[];
const CoreDesc *const compiled_in_table[] = { &c264_desc, 0 };
#else
#define core_table compiled_in_table
#include "table.cc"
#endif
