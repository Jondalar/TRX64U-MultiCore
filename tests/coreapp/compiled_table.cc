// The application's real core table under another name, for tests that need the compiled-in descriptors next to a table of their own (test_descgen: the record's core is
// compared with the compiled-in descriptor of the same core, while the start must not find that one).
#ifdef TEST_APP_VC20
// the VIC-20's app is its own build (run.sh vc20app): its table is its core only
#include "cores.h"
const CoreDesc *const compiled_in_table[] = { &vc20_desc, 0 };
#else
#define core_table compiled_in_table
#include "table.cc"
#endif
