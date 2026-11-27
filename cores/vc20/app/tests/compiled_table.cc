// The application's real core table under another name, for tests that need the compiled-in descriptors next to a table of their own (test_descgen: the record's core is
// compared with the compiled-in descriptor of the same core, while the start must not find that one).
// the VIC-20's app is its own build (run.sh vc20app): its table is its core only
#include "cores.h"
extern const CoreDesc *const compiled_in_table[];   // a const array at namespace scope is internal unless declared extern: test_descgen.cc links it
const CoreDesc *const compiled_in_table[] = { &vc20_desc, 0 };
